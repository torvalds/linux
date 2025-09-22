//===-- llvm-size.cpp - Print the size of each object section ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This program is a utility that works like traditional Unix "size",
// that is, it prints out the size of each section, and the total size of all
// sections.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/APInt.h"
#include "llvm/Object/Archive.h"
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Object/MachO.h"
#include "llvm/Object/MachOUniversal.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Option/Arg.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Option/Option.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/LLVMDriver.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <string>
#include <system_error>

using namespace llvm;
using namespace object;

namespace {
using namespace llvm::opt; // for HelpHidden in Opts.inc
enum ID {
  OPT_INVALID = 0, // This is not an option ID.
#define OPTION(...) LLVM_MAKE_OPT_ID(__VA_ARGS__),
#include "Opts.inc"
#undef OPTION
};

#define PREFIX(NAME, VALUE)                                                    \
  static constexpr StringLiteral NAME##_init[] = VALUE;                        \
  static constexpr ArrayRef<StringLiteral> NAME(NAME##_init,                   \
                                                std::size(NAME##_init) - 1);
#include "Opts.inc"
#undef PREFIX

static constexpr opt::OptTable::Info InfoTable[] = {
#define OPTION(...) LLVM_CONSTRUCT_OPT_INFO(__VA_ARGS__),
#include "Opts.inc"
#undef OPTION
};

class SizeOptTable : public opt::GenericOptTable {
public:
  SizeOptTable() : GenericOptTable(InfoTable) { setGroupedShortOptions(true); }
};

enum OutputFormatTy { berkeley, sysv, darwin };
enum RadixTy { octal = 8, decimal = 10, hexadecimal = 16 };
} // namespace

static bool ArchAll = false;
static std::vector<StringRef> ArchFlags;
static bool ELFCommons;
static OutputFormatTy OutputFormat;
static bool DarwinLongFormat;
static RadixTy Radix;
static bool TotalSizes;

static std::vector<std::string> InputFilenames;

static std::string ToolName;

// States
static bool HadError = false;
static bool BerkeleyHeaderPrinted = false;
static bool MoreThanOneFile = false;
static uint64_t TotalObjectText = 0;
static uint64_t TotalObjectData = 0;
static uint64_t TotalObjectBss = 0;
static uint64_t TotalObjectTotal = 0;

static void error(const Twine &Message, StringRef File = "") {
  HadError = true;
  if (File.empty())
    WithColor::error(errs(), ToolName) << Message << '\n';
  else
    WithColor::error(errs(), ToolName)
        << "'" << File << "': " << Message << '\n';
}

// This version of error() prints the archive name and member name, for example:
// "libx.a(foo.o)" after the ToolName before the error message.  It sets
// HadError but returns allowing the code to move on to other archive members.
static void error(llvm::Error E, StringRef FileName, const Archive::Child &C,
                  StringRef ArchitectureName = StringRef()) {
  HadError = true;
  WithColor::error(errs(), ToolName) << "'" << FileName << "'";

  Expected<StringRef> NameOrErr = C.getName();
  // TODO: if we have a error getting the name then it would be nice to print
  // the index of which archive member this is and or its offset in the
  // archive instead of "???" as the name.
  if (!NameOrErr) {
    consumeError(NameOrErr.takeError());
    errs() << "(" << "???" << ")";
  } else
    errs() << "(" << NameOrErr.get() << ")";

  if (!ArchitectureName.empty())
    errs() << " (for architecture " << ArchitectureName << ") ";

  std::string Buf;
  raw_string_ostream OS(Buf);
  logAllUnhandledErrors(std::move(E), OS);
  OS.flush();
  errs() << ": " << Buf << "\n";
}

// This version of error() prints the file name and which architecture slice it // is from, for example: "foo.o (for architecture i386)" after the ToolName
// before the error message.  It sets HadError but returns allowing the code to
// move on to other architecture slices.
static void error(llvm::Error E, StringRef FileName,
                  StringRef ArchitectureName = StringRef()) {
  HadError = true;
  WithColor::error(errs(), ToolName) << "'" << FileName << "'";

  if (!ArchitectureName.empty())
    errs() << " (for architecture " << ArchitectureName << ") ";

  std::string Buf;
  raw_string_ostream OS(Buf);
  logAllUnhandledErrors(std::move(E), OS);
  OS.flush();
  errs() << ": " << Buf << "\n";
}

/// Get the length of the string that represents @p num in Radix including the
/// leading 0x or 0 for hexadecimal and octal respectively.
static size_t getNumLengthAsString(uint64_t num) {
  APInt conv(64, num);
  SmallString<32> result;
  conv.toString(result, Radix, false, true);
  return result.size();
}

/// Return the printing format for the Radix.
static const char *getRadixFmt() {
  switch (Radix) {
  case octal:
    return PRIo64;
  case decimal:
    return PRIu64;
  case hexadecimal:
    return PRIx64;
  }
  return nullptr;
}

/// Remove unneeded ELF sections from calculation
static bool considerForSize(ObjectFile *Obj, SectionRef Section) {
  if (!Obj->isELF())
    return true;
  switch (static_cast<ELFSectionRef>(Section).getType()) {
  case ELF::SHT_NULL:
  case ELF::SHT_SYMTAB:
    return false;
  case ELF::SHT_STRTAB:
  case ELF::SHT_REL:
  case ELF::SHT_RELA:
    return static_cast<ELFSectionRef>(Section).getFlags() & ELF::SHF_ALLOC;
  }
  return true;
}

/// Total size of all ELF common symbols
static Expected<uint64_t> getCommonSize(ObjectFile *Obj) {
  uint64_t TotalCommons = 0;
  for (auto &Sym : Obj->symbols()) {
    Expected<uint32_t> SymFlagsOrErr =
        Obj->getSymbolFlags(Sym.getRawDataRefImpl());
    if (!SymFlagsOrErr)
      return SymFlagsOrErr.takeError();
    if (*SymFlagsOrErr & SymbolRef::SF_Common)
      TotalCommons += Obj->getCommonSymbolSize(Sym.getRawDataRefImpl());
  }
  return TotalCommons;
}

/// Print the size of each Mach-O segment and section in @p MachO.
///
/// This is when used when @c OutputFormat is darwin and produces the same
/// output as darwin's size(1) -m output.
static void printDarwinSectionSizes(MachOObjectFile *MachO) {
  std::string fmtbuf;
  raw_string_ostream fmt(fmtbuf);
  const char *radix_fmt = getRadixFmt();
  if (Radix == hexadecimal)
    fmt << "0x";
  fmt << "%" << radix_fmt;

  uint32_t Filetype = MachO->getHeader().filetype;

  uint64_t total = 0;
  for (const auto &Load : MachO->load_commands()) {
    if (Load.C.cmd == MachO::LC_SEGMENT_64) {
      MachO::segment_command_64 Seg = MachO->getSegment64LoadCommand(Load);
      outs() << "Segment " << Seg.segname << ": "
             << format(fmt.str().c_str(), Seg.vmsize);
      if (DarwinLongFormat)
        outs() << " (vmaddr 0x" << format("%" PRIx64, Seg.vmaddr) << " fileoff "
               << Seg.fileoff << ")";
      outs() << "\n";
      total += Seg.vmsize;
      uint64_t sec_total = 0;
      for (unsigned J = 0; J < Seg.nsects; ++J) {
        MachO::section_64 Sec = MachO->getSection64(Load, J);
        if (Filetype == MachO::MH_OBJECT)
          outs() << "\tSection (" << format("%.16s", &Sec.segname) << ", "
                 << format("%.16s", &Sec.sectname) << "): ";
        else
          outs() << "\tSection " << format("%.16s", &Sec.sectname) << ": ";
        outs() << format(fmt.str().c_str(), Sec.size);
        if (DarwinLongFormat)
          outs() << " (addr 0x" << format("%" PRIx64, Sec.addr) << " offset "
                 << Sec.offset << ")";
        outs() << "\n";
        sec_total += Sec.size;
      }
      if (Seg.nsects != 0)
        outs() << "\ttotal " << format(fmt.str().c_str(), sec_total) << "\n";
    } else if (Load.C.cmd == MachO::LC_SEGMENT) {
      MachO::segment_command Seg = MachO->getSegmentLoadCommand(Load);
      uint64_t Seg_vmsize = Seg.vmsize;
      outs() << "Segment " << Seg.segname << ": "
             << format(fmt.str().c_str(), Seg_vmsize);
      if (DarwinLongFormat)
        outs() << " (vmaddr 0x" << format("%" PRIx32, Seg.vmaddr) << " fileoff "
               << Seg.fileoff << ")";
      outs() << "\n";
      total += Seg.vmsize;
      uint64_t sec_total = 0;
      for (unsigned J = 0; J < Seg.nsects; ++J) {
        MachO::section Sec = MachO->getSection(Load, J);
        if (Filetype == MachO::MH_OBJECT)
          outs() << "\tSection (" << format("%.16s", &Sec.segname) << ", "
                 << format("%.16s", &Sec.sectname) << "): ";
        else
          outs() << "\tSection " << format("%.16s", &Sec.sectname) << ": ";
        uint64_t Sec_size = Sec.size;
        outs() << format(fmt.str().c_str(), Sec_size);
        if (DarwinLongFormat)
          outs() << " (addr 0x" << format("%" PRIx32, Sec.addr) << " offset "
                 << Sec.offset << ")";
        outs() << "\n";
        sec_total += Sec.size;
      }
      if (Seg.nsects != 0)
        outs() << "\ttotal " << format(fmt.str().c_str(), sec_total) << "\n";
    }
  }
  outs() << "total " << format(fmt.str().c_str(), total) << "\n";
}

/// Print the summary sizes of the standard Mach-O segments in @p MachO.
///
/// This is when used when @c OutputFormat is berkeley with a Mach-O file and
/// produces the same output as darwin's size(1) default output.
static void printDarwinSegmentSizes(MachOObjectFile *MachO) {
  uint64_t total_text = 0;
  uint64_t total_data = 0;
  uint64_t total_objc = 0;
  uint64_t total_others = 0;
  for (const auto &Load : MachO->load_commands()) {
    if (Load.C.cmd == MachO::LC_SEGMENT_64) {
      MachO::segment_command_64 Seg = MachO->getSegment64LoadCommand(Load);
      if (MachO->getHeader().filetype == MachO::MH_OBJECT) {
        for (unsigned J = 0; J < Seg.nsects; ++J) {
          MachO::section_64 Sec = MachO->getSection64(Load, J);
          StringRef SegmentName = StringRef(Sec.segname);
          if (SegmentName == "__TEXT")
            total_text += Sec.size;
          else if (SegmentName == "__DATA")
            total_data += Sec.size;
          else if (SegmentName == "__OBJC")
            total_objc += Sec.size;
          else
            total_others += Sec.size;
        }
      } else {
        StringRef SegmentName = StringRef(Seg.segname);
        if (SegmentName == "__TEXT")
          total_text += Seg.vmsize;
        else if (SegmentName == "__DATA")
          total_data += Seg.vmsize;
        else if (SegmentName == "__OBJC")
          total_objc += Seg.vmsize;
        else
          total_others += Seg.vmsize;
      }
    } else if (Load.C.cmd == MachO::LC_SEGMENT) {
      MachO::segment_command Seg = MachO->getSegmentLoadCommand(Load);
      if (MachO->getHeader().filetype == MachO::MH_OBJECT) {
        for (unsigned J = 0; J < Seg.nsects; ++J) {
          MachO::section Sec = MachO->getSection(Load, J);
          StringRef SegmentName = StringRef(Sec.segname);
          if (SegmentName == "__TEXT")
            total_text += Sec.size;
          else if (SegmentName == "__DATA")
            total_data += Sec.size;
          else if (SegmentName == "__OBJC")
            total_objc += Sec.size;
          else
            total_others += Sec.size;
        }
      } else {
        StringRef SegmentName = StringRef(Seg.segname);
        if (SegmentName == "__TEXT")
          total_text += Seg.vmsize;
        else if (SegmentName == "__DATA")
          total_data += Seg.vmsize;
        else if (SegmentName == "__OBJC")
          total_objc += Seg.vmsize;
        else
          total_others += Seg.vmsize;
      }
    }
  }
  uint64_t total = total_text + total_data + total_objc + total_others;

  if (!BerkeleyHeaderPrinted) {
    outs() << "__TEXT\t__DATA\t__OBJC\tothers\tdec\thex\n";
    BerkeleyHeaderPrinted = true;
  }
  outs() << total_text << "\t" << total_data << "\t" << total_objc << "\t"
         << total_others << "\t" << total << "\t" << format("%" PRIx64, total)
         << "\t";
}

/// Print the size of each section in @p Obj.
///
/// The format used is determined by @c OutputFormat and @c Radix.
static void printObjectSectionSizes(ObjectFile *Obj) {
  uint64_t total = 0;
  std::string fmtbuf;
  raw_string_ostream fmt(fmtbuf);
  const char *radix_fmt = getRadixFmt();

  // If OutputFormat is darwin and we have a MachOObjectFile print as darwin's
  // size(1) -m output, else if OutputFormat is darwin and not a Mach-O object
  // let it fall through to OutputFormat berkeley.
  MachOObjectFile *MachO = dyn_cast<MachOObjectFile>(Obj);
  if (OutputFormat == darwin && MachO)
    printDarwinSectionSizes(MachO);
  // If we have a MachOObjectFile and the OutputFormat is berkeley print as
  // darwin's default berkeley format for Mach-O files.
  else if (MachO && OutputFormat == berkeley)
    printDarwinSegmentSizes(MachO);
  else if (OutputFormat == sysv) {
    // Run two passes over all sections. The first gets the lengths needed for
    // formatting the output. The second actually does the output.
    std::size_t max_name_len = strlen("section");
    std::size_t max_size_len = strlen("size");
    std::size_t max_addr_len = strlen("addr");
    for (const SectionRef &Section : Obj->sections()) {
      if (!considerForSize(Obj, Section))
        continue;
      uint64_t size = Section.getSize();
      total += size;

      Expected<StringRef> name_or_err = Section.getName();
      if (!name_or_err) {
        error(name_or_err.takeError(), Obj->getFileName());
        return;
      }

      uint64_t addr = Section.getAddress();
      max_name_len = std::max(max_name_len, name_or_err->size());
      max_size_len = std::max(max_size_len, getNumLengthAsString(size));
      max_addr_len = std::max(max_addr_len, getNumLengthAsString(addr));
    }

    // Add extra padding.
    max_name_len += 2;
    max_size_len += 2;
    max_addr_len += 2;

    // Setup header format.
    fmt << "%-" << max_name_len << "s "
        << "%" << max_size_len << "s "
        << "%" << max_addr_len << "s\n";

    // Print header
    outs() << format(fmt.str().c_str(), static_cast<const char *>("section"),
                     static_cast<const char *>("size"),
                     static_cast<const char *>("addr"));
    fmtbuf.clear();

    // Setup per section format.
    fmt << "%-" << max_name_len << "s "
        << "%#" << max_size_len << radix_fmt << " "
        << "%#" << max_addr_len << radix_fmt << "\n";

    // Print each section.
    for (const SectionRef &Section : Obj->sections()) {
      if (!considerForSize(Obj, Section))
        continue;

      Expected<StringRef> name_or_err = Section.getName();
      if (!name_or_err) {
        error(name_or_err.takeError(), Obj->getFileName());
        return;
      }

      uint64_t size = Section.getSize();
      uint64_t addr = Section.getAddress();
      outs() << format(fmt.str().c_str(), name_or_err->str().c_str(), size, addr);
    }

    if (ELFCommons) {
      if (Expected<uint64_t> CommonSizeOrErr = getCommonSize(Obj)) {
        total += *CommonSizeOrErr;
        outs() << format(fmt.str().c_str(), std::string("*COM*").c_str(),
                         *CommonSizeOrErr, static_cast<uint64_t>(0));
      } else {
        error(CommonSizeOrErr.takeError(), Obj->getFileName());
        return;
      }
    }

    // Print total.
    fmtbuf.clear();
    fmt << "%-" << max_name_len << "s "
        << "%#" << max_size_len << radix_fmt << "\n";
    outs() << format(fmt.str().c_str(), static_cast<const char *>("Total"),
                     total)
           << "\n\n";
  } else {
    // The Berkeley format does not display individual section sizes. It
    // displays the cumulative size for each section type.
    uint64_t total_text = 0;
    uint64_t total_data = 0;
    uint64_t total_bss = 0;

    // Make one pass over the section table to calculate sizes.
    for (const SectionRef &Section : Obj->sections()) {
      uint64_t size = Section.getSize();
      bool isText = Section.isBerkeleyText();
      bool isData = Section.isBerkeleyData();
      bool isBSS = Section.isBSS();
      if (isText)
        total_text += size;
      else if (isData)
        total_data += size;
      else if (isBSS)
        total_bss += size;
    }

    if (ELFCommons) {
      if (Expected<uint64_t> CommonSizeOrErr = getCommonSize(Obj))
        total_bss += *CommonSizeOrErr;
      else {
        error(CommonSizeOrErr.takeError(), Obj->getFileName());
        return;
      }
    }

    total = total_text + total_data + total_bss;

    if (TotalSizes) {
      TotalObjectText += total_text;
      TotalObjectData += total_data;
      TotalObjectBss += total_bss;
      TotalObjectTotal += total;
    }

    if (!BerkeleyHeaderPrinted) {
      outs() << "   text\t"
                "   data\t"
                "    bss\t"
                "    "
             << (Radix == octal ? "oct" : "dec")
             << "\t"
                "    hex\t"
                "filename\n";
      BerkeleyHeaderPrinted = true;
    }

    // Print result.
    fmt << "%#7" << radix_fmt << "\t"
        << "%#7" << radix_fmt << "\t"
        << "%#7" << radix_fmt << "\t";
    outs() << format(fmt.str().c_str(), total_text, total_data, total_bss);
    fmtbuf.clear();
    fmt << "%7" << (Radix == octal ? PRIo64 : PRIu64) << "\t"
        << "%7" PRIx64 "\t";
    outs() << format(fmt.str().c_str(), total, total);
  }
}

/// Checks to see if the @p O ObjectFile is a Mach-O file and if it is and there
/// is a list of architecture flags specified then check to make sure this
/// Mach-O file is one of those architectures or all architectures was
/// specificed.  If not then an error is generated and this routine returns
/// false.  Else it returns true.
static bool checkMachOAndArchFlags(ObjectFile *O, StringRef Filename) {
  auto *MachO = dyn_cast<MachOObjectFile>(O);

  if (!MachO || ArchAll || ArchFlags.empty())
    return true;

  MachO::mach_header H;
  MachO::mach_header_64 H_64;
  Triple T;
  if (MachO->is64Bit()) {
    H_64 = MachO->MachOObjectFile::getHeader64();
    T = MachOObjectFile::getArchTriple(H_64.cputype, H_64.cpusubtype);
  } else {
    H = MachO->MachOObjectFile::getHeader();
    T = MachOObjectFile::getArchTriple(H.cputype, H.cpusubtype);
  }
  if (!is_contained(ArchFlags, T.getArchName())) {
    error("no architecture specified", Filename);
    return false;
  }
  return true;
}

/// Print the section sizes for @p file. If @p file is an archive, print the
/// section sizes for each archive member.
static void printFileSectionSizes(StringRef file) {

  // Attempt to open the binary.
  Expected<OwningBinary<Binary>> BinaryOrErr = createBinary(file);
  if (!BinaryOrErr) {
    error(BinaryOrErr.takeError(), file);
    return;
  }
  Binary &Bin = *BinaryOrErr.get().getBinary();

  if (Archive *a = dyn_cast<Archive>(&Bin)) {
    // This is an archive. Iterate over each member and display its sizes.
    Error Err = Error::success();
    for (auto &C : a->children(Err)) {
      Expected<std::unique_ptr<Binary>> ChildOrErr = C.getAsBinary();
      if (!ChildOrErr) {
        if (auto E = isNotObjectErrorInvalidFileType(ChildOrErr.takeError()))
          error(std::move(E), a->getFileName(), C);
        continue;
      }
      if (ObjectFile *o = dyn_cast<ObjectFile>(&*ChildOrErr.get())) {
        MachOObjectFile *MachO = dyn_cast<MachOObjectFile>(o);
        if (!checkMachOAndArchFlags(o, file))
          return;
        if (OutputFormat == sysv)
          outs() << o->getFileName() << "   (ex " << a->getFileName() << "):\n";
        else if (MachO && OutputFormat == darwin)
          outs() << a->getFileName() << "(" << o->getFileName() << "):\n";
        printObjectSectionSizes(o);
        if (!MachO && OutputFormat == darwin)
          outs() << o->getFileName() << " (ex " << a->getFileName() << ")\n";
        if (OutputFormat == berkeley) {
          if (MachO)
            outs() << a->getFileName() << "(" << o->getFileName() << ")\n";
          else
            outs() << o->getFileName() << " (ex " << a->getFileName() << ")\n";
        }
      }
    }
    if (Err)
      error(std::move(Err), a->getFileName());
  } else if (MachOUniversalBinary *UB =
                 dyn_cast<MachOUniversalBinary>(&Bin)) {
    // If we have a list of architecture flags specified dump only those.
    if (!ArchAll && !ArchFlags.empty()) {
      // Look for a slice in the universal binary that matches each ArchFlag.
      bool ArchFound;
      for (unsigned i = 0; i < ArchFlags.size(); ++i) {
        ArchFound = false;
        for (MachOUniversalBinary::object_iterator I = UB->begin_objects(),
                                                   E = UB->end_objects();
             I != E; ++I) {
          if (ArchFlags[i] == I->getArchFlagName()) {
            ArchFound = true;
            Expected<std::unique_ptr<ObjectFile>> UO = I->getAsObjectFile();
            if (UO) {
              if (ObjectFile *o = dyn_cast<ObjectFile>(&*UO.get())) {
                MachOObjectFile *MachO = dyn_cast<MachOObjectFile>(o);
                if (OutputFormat == sysv)
                  outs() << o->getFileName() << "  :\n";
                else if (MachO && OutputFormat == darwin) {
                  if (MoreThanOneFile || ArchFlags.size() > 1)
                    outs() << o->getFileName() << " (for architecture "
                           << I->getArchFlagName() << "): \n";
                }
                printObjectSectionSizes(o);
                if (OutputFormat == berkeley) {
                  if (!MachO || MoreThanOneFile || ArchFlags.size() > 1)
                    outs() << o->getFileName() << " (for architecture "
                           << I->getArchFlagName() << ")";
                  outs() << "\n";
                }
              }
            } else if (auto E = isNotObjectErrorInvalidFileType(
                       UO.takeError())) {
              error(std::move(E), file, ArchFlags.size() > 1 ?
                    StringRef(I->getArchFlagName()) : StringRef());
              return;
            } else if (Expected<std::unique_ptr<Archive>> AOrErr =
                           I->getAsArchive()) {
              std::unique_ptr<Archive> &UA = *AOrErr;
              // This is an archive. Iterate over each member and display its
              // sizes.
              Error Err = Error::success();
              for (auto &C : UA->children(Err)) {
                Expected<std::unique_ptr<Binary>> ChildOrErr = C.getAsBinary();
                if (!ChildOrErr) {
                  if (auto E = isNotObjectErrorInvalidFileType(
                                    ChildOrErr.takeError()))
                    error(std::move(E), UA->getFileName(), C,
                          ArchFlags.size() > 1 ?
                          StringRef(I->getArchFlagName()) : StringRef());
                  continue;
                }
                if (ObjectFile *o = dyn_cast<ObjectFile>(&*ChildOrErr.get())) {
                  MachOObjectFile *MachO = dyn_cast<MachOObjectFile>(o);
                  if (OutputFormat == sysv)
                    outs() << o->getFileName() << "   (ex " << UA->getFileName()
                           << "):\n";
                  else if (MachO && OutputFormat == darwin)
                    outs() << UA->getFileName() << "(" << o->getFileName()
                           << ")"
                           << " (for architecture " << I->getArchFlagName()
                           << "):\n";
                  printObjectSectionSizes(o);
                  if (OutputFormat == berkeley) {
                    if (MachO) {
                      outs() << UA->getFileName() << "(" << o->getFileName()
                             << ")";
                      if (ArchFlags.size() > 1)
                        outs() << " (for architecture " << I->getArchFlagName()
                               << ")";
                      outs() << "\n";
                    } else
                      outs() << o->getFileName() << " (ex " << UA->getFileName()
                             << ")\n";
                  }
                }
              }
              if (Err)
                error(std::move(Err), UA->getFileName());
            } else {
              consumeError(AOrErr.takeError());
              error("mach-o universal file for architecture " +
                        StringRef(I->getArchFlagName()) +
                        " is not a mach-o file or an archive file",
                    file);
            }
          }
        }
        if (!ArchFound) {
          error("file does not contain architecture " + ArchFlags[i], file);
          return;
        }
      }
      return;
    }
    // No architecture flags were specified so if this contains a slice that
    // matches the host architecture dump only that.
    if (!ArchAll) {
      StringRef HostArchName = MachOObjectFile::getHostArch().getArchName();
      for (MachOUniversalBinary::object_iterator I = UB->begin_objects(),
                                                 E = UB->end_objects();
           I != E; ++I) {
        if (HostArchName == I->getArchFlagName()) {
          Expected<std::unique_ptr<ObjectFile>> UO = I->getAsObjectFile();
          if (UO) {
            if (ObjectFile *o = dyn_cast<ObjectFile>(&*UO.get())) {
              MachOObjectFile *MachO = dyn_cast<MachOObjectFile>(o);
              if (OutputFormat == sysv)
                outs() << o->getFileName() << "  :\n";
              else if (MachO && OutputFormat == darwin) {
                if (MoreThanOneFile)
                  outs() << o->getFileName() << " (for architecture "
                         << I->getArchFlagName() << "):\n";
              }
              printObjectSectionSizes(o);
              if (OutputFormat == berkeley) {
                if (!MachO || MoreThanOneFile)
                  outs() << o->getFileName() << " (for architecture "
                         << I->getArchFlagName() << ")";
                outs() << "\n";
              }
            }
          } else if (auto E = isNotObjectErrorInvalidFileType(UO.takeError())) {
            error(std::move(E), file);
            return;
          } else if (Expected<std::unique_ptr<Archive>> AOrErr =
                         I->getAsArchive()) {
            std::unique_ptr<Archive> &UA = *AOrErr;
            // This is an archive. Iterate over each member and display its
            // sizes.
            Error Err = Error::success();
            for (auto &C : UA->children(Err)) {
              Expected<std::unique_ptr<Binary>> ChildOrErr = C.getAsBinary();
              if (!ChildOrErr) {
                if (auto E = isNotObjectErrorInvalidFileType(
                                ChildOrErr.takeError()))
                  error(std::move(E), UA->getFileName(), C);
                continue;
              }
              if (ObjectFile *o = dyn_cast<ObjectFile>(&*ChildOrErr.get())) {
                MachOObjectFile *MachO = dyn_cast<MachOObjectFile>(o);
                if (OutputFormat == sysv)
                  outs() << o->getFileName() << "   (ex " << UA->getFileName()
                         << "):\n";
                else if (MachO && OutputFormat == darwin)
                  outs() << UA->getFileName() << "(" << o->getFileName() << ")"
                         << " (for architecture " << I->getArchFlagName()
                         << "):\n";
                printObjectSectionSizes(o);
                if (OutputFormat == berkeley) {
                  if (MachO)
                    outs() << UA->getFileName() << "(" << o->getFileName()
                           << ")\n";
                  else
                    outs() << o->getFileName() << " (ex " << UA->getFileName()
                           << ")\n";
                }
              }
            }
            if (Err)
              error(std::move(Err), UA->getFileName());
          } else {
            consumeError(AOrErr.takeError());
            error("mach-o universal file for architecture " +
                      StringRef(I->getArchFlagName()) +
                      " is not a mach-o file or an archive file",
                  file);
          }
          return;
        }
      }
    }
    // Either all architectures have been specified or none have been specified
    // and this does not contain the host architecture so dump all the slices.
    bool MoreThanOneArch = UB->getNumberOfObjects() > 1;
    for (MachOUniversalBinary::object_iterator I = UB->begin_objects(),
                                               E = UB->end_objects();
         I != E; ++I) {
      Expected<std::unique_ptr<ObjectFile>> UO = I->getAsObjectFile();
      if (UO) {
        if (ObjectFile *o = dyn_cast<ObjectFile>(&*UO.get())) {
          MachOObjectFile *MachO = dyn_cast<MachOObjectFile>(o);
          if (OutputFormat == sysv)
            outs() << o->getFileName() << "  :\n";
          else if (MachO && OutputFormat == darwin) {
            if (MoreThanOneFile || MoreThanOneArch)
              outs() << o->getFileName() << " (for architecture "
                     << I->getArchFlagName() << "):";
            outs() << "\n";
          }
          printObjectSectionSizes(o);
          if (OutputFormat == berkeley) {
            if (!MachO || MoreThanOneFile || MoreThanOneArch)
              outs() << o->getFileName() << " (for architecture "
                     << I->getArchFlagName() << ")";
            outs() << "\n";
          }
        }
      } else if (auto E = isNotObjectErrorInvalidFileType(UO.takeError())) {
        error(std::move(E), file, MoreThanOneArch ?
              StringRef(I->getArchFlagName()) : StringRef());
        return;
      } else if (Expected<std::unique_ptr<Archive>> AOrErr =
                         I->getAsArchive()) {
        std::unique_ptr<Archive> &UA = *AOrErr;
        // This is an archive. Iterate over each member and display its sizes.
        Error Err = Error::success();
        for (auto &C : UA->children(Err)) {
          Expected<std::unique_ptr<Binary>> ChildOrErr = C.getAsBinary();
          if (!ChildOrErr) {
            if (auto E = isNotObjectErrorInvalidFileType(
                              ChildOrErr.takeError()))
              error(std::move(E), UA->getFileName(), C, MoreThanOneArch ?
                    StringRef(I->getArchFlagName()) : StringRef());
            continue;
          }
          if (ObjectFile *o = dyn_cast<ObjectFile>(&*ChildOrErr.get())) {
            MachOObjectFile *MachO = dyn_cast<MachOObjectFile>(o);
            if (OutputFormat == sysv)
              outs() << o->getFileName() << "   (ex " << UA->getFileName()
                     << "):\n";
            else if (MachO && OutputFormat == darwin)
              outs() << UA->getFileName() << "(" << o->getFileName() << ")"
                     << " (for architecture " << I->getArchFlagName() << "):\n";
            printObjectSectionSizes(o);
            if (OutputFormat == berkeley) {
              if (MachO)
                outs() << UA->getFileName() << "(" << o->getFileName() << ")"
                       << " (for architecture " << I->getArchFlagName()
                       << ")\n";
              else
                outs() << o->getFileName() << " (ex " << UA->getFileName()
                       << ")\n";
            }
          }
        }
        if (Err)
          error(std::move(Err), UA->getFileName());
      } else {
        consumeError(AOrErr.takeError());
        error("mach-o universal file for architecture " +
                  StringRef(I->getArchFlagName()) +
                  " is not a mach-o file or an archive file",
              file);
      }
    }
  } else if (ObjectFile *o = dyn_cast<ObjectFile>(&Bin)) {
    if (!checkMachOAndArchFlags(o, file))
      return;
    MachOObjectFile *MachO = dyn_cast<MachOObjectFile>(o);
    if (OutputFormat == sysv)
      outs() << o->getFileName() << "  :\n";
    else if (MachO && OutputFormat == darwin && MoreThanOneFile)
      outs() << o->getFileName() << ":\n";
    printObjectSectionSizes(o);
    if (!MachO && OutputFormat == darwin)
      outs() << o->getFileName() << "\n";
    if (OutputFormat == berkeley) {
      if (!MachO || MoreThanOneFile)
        outs() << o->getFileName();
      outs() << "\n";
    }
  } else {
    error("unsupported file type", file);
  }
}

static void printBerkeleyTotals() {
  std::string fmtbuf;
  raw_string_ostream fmt(fmtbuf);
  const char *radix_fmt = getRadixFmt();
  fmt << "%#7" << radix_fmt << "\t"
      << "%#7" << radix_fmt << "\t"
      << "%#7" << radix_fmt << "\t";
  outs() << format(fmt.str().c_str(), TotalObjectText, TotalObjectData,
                   TotalObjectBss);
  fmtbuf.clear();
  fmt << "%7" << (Radix == octal ? PRIo64 : PRIu64) << "\t"
      << "%7" PRIx64 "\t";
  outs() << format(fmt.str().c_str(), TotalObjectTotal, TotalObjectTotal)
         << "(TOTALS)\n";
}

int llvm_size_main(int argc, char **argv, const llvm::ToolContext &) {
  BumpPtrAllocator A;
  StringSaver Saver(A);
  SizeOptTable Tbl;
  ToolName = argv[0];
  opt::InputArgList Args =
      Tbl.parseArgs(argc, argv, OPT_UNKNOWN, Saver, [&](StringRef Msg) {
        error(Msg);
        exit(1);
      });
  if (Args.hasArg(OPT_help)) {
    Tbl.printHelp(
        outs(),
        (Twine(ToolName) + " [options] <input object files>").str().c_str(),
        "LLVM object size dumper");
    // TODO Replace this with OptTable API once it adds extrahelp support.
    outs() << "\nPass @FILE as argument to read options from FILE.\n";
    return 0;
  }
  if (Args.hasArg(OPT_version)) {
    outs() << ToolName << '\n';
    cl::PrintVersionMessage();
    return 0;
  }

  ELFCommons = Args.hasArg(OPT_common);
  DarwinLongFormat = Args.hasArg(OPT_l);
  TotalSizes = Args.hasArg(OPT_totals);
  StringRef V = Args.getLastArgValue(OPT_format_EQ, "berkeley");
  if (V == "berkeley")
    OutputFormat = berkeley;
  else if (V == "darwin")
    OutputFormat = darwin;
  else if (V == "sysv")
    OutputFormat = sysv;
  else
    error("--format value should be one of: 'berkeley', 'darwin', 'sysv'");
  V = Args.getLastArgValue(OPT_radix_EQ, "10");
  if (V == "8")
    Radix = RadixTy::octal;
  else if (V == "10")
    Radix = RadixTy::decimal;
  else if (V == "16")
    Radix = RadixTy::hexadecimal;
  else
    error("--radix value should be one of: 8, 10, 16 ");

  for (const auto *A : Args.filtered(OPT_arch_EQ)) {
    SmallVector<StringRef, 2> Values;
    llvm::SplitString(A->getValue(), Values, ",");
    for (StringRef V : Values) {
      if (V == "all")
        ArchAll = true;
      else if (MachOObjectFile::isValidArch(V))
        ArchFlags.push_back(V);
      else {
        outs() << ToolName << ": for the -arch option: Unknown architecture "
               << "named '" << V << "'";
        return 1;
      }
    }
  }

  InputFilenames = Args.getAllArgValues(OPT_INPUT);
  if (InputFilenames.empty())
    InputFilenames.push_back("a.out");

  MoreThanOneFile = InputFilenames.size() > 1;
  llvm::for_each(InputFilenames, printFileSectionSizes);
  if (OutputFormat == berkeley && TotalSizes)
    printBerkeleyTotals();

  if (HadError)
    return 1;
  return 0;
}
