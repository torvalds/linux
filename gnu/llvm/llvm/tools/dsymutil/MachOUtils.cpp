//===-- MachOUtils.cpp - Mach-o specific helpers for dsymutil  ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MachOUtils.h"
#include "BinaryHolder.h"
#include "DebugMap.h"
#include "LinkUtils.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/CodeGen/NonRelocatableStringpool.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCMachObjectWriter.h"
#include "llvm/MC/MCObjectStreamer.h"
#include "llvm/MC/MCSectionMachO.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Object/MachO.h"
#include "llvm/Support/FileUtilities.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {
namespace dsymutil {
namespace MachOUtils {

llvm::Error ArchAndFile::createTempFile() {
  SmallString<256> SS;
  std::error_code EC = sys::fs::createTemporaryFile("dsym", "dwarf", FD, SS);

  if (EC)
    return errorCodeToError(EC);

  Path = SS.str();

  return Error::success();
}

llvm::StringRef ArchAndFile::getPath() const {
  assert(!Path.empty() && "path called before createTempFile");
  return Path;
}

int ArchAndFile::getFD() const {
  assert((FD != -1) && "path called before createTempFile");
  return FD;
}

ArchAndFile::~ArchAndFile() {
  if (!Path.empty())
    sys::fs::remove(Path);
}

std::string getArchName(StringRef Arch) {
  if (Arch.starts_with("thumb"))
    return (llvm::Twine("arm") + Arch.drop_front(5)).str();
  return std::string(Arch);
}

static bool runLipo(StringRef SDKPath, SmallVectorImpl<StringRef> &Args) {
  auto Path = sys::findProgramByName("lipo", ArrayRef(SDKPath));
  if (!Path)
    Path = sys::findProgramByName("lipo");

  if (!Path) {
    WithColor::error() << "lipo: " << Path.getError().message() << "\n";
    return false;
  }

  std::string ErrMsg;
  int result =
      sys::ExecuteAndWait(*Path, Args, std::nullopt, {}, 0, 0, &ErrMsg);
  if (result) {
    WithColor::error() << "lipo: " << ErrMsg << "\n";
    return false;
  }

  return true;
}

bool generateUniversalBinary(SmallVectorImpl<ArchAndFile> &ArchFiles,
                             StringRef OutputFileName,
                             const LinkOptions &Options, StringRef SDKPath,
                             bool Fat64) {
  // No need to merge one file into a universal fat binary.
  if (ArchFiles.size() == 1) {
    llvm::StringRef TmpPath = ArchFiles.front().getPath();
    if (auto EC = sys::fs::rename(TmpPath, OutputFileName)) {
      // If we can't rename, try to copy to work around cross-device link
      // issues.
      EC = sys::fs::copy_file(TmpPath, OutputFileName);
      if (EC) {
        WithColor::error() << "while keeping " << TmpPath << " as "
                           << OutputFileName << ": " << EC.message() << "\n";
        return false;
      }
      sys::fs::remove(TmpPath);
    }
    return true;
  }

  SmallVector<StringRef, 8> Args;
  Args.push_back("lipo");
  Args.push_back("-create");

  for (auto &Thin : ArchFiles)
    Args.push_back(Thin.getPath());

  // Align segments to match dsymutil-classic alignment.
  for (auto &Thin : ArchFiles) {
    Thin.Arch = getArchName(Thin.Arch);
    Args.push_back("-segalign");
    Args.push_back(Thin.Arch);
    Args.push_back("20");
  }

  // Use a 64-bit fat header if requested.
  if (Fat64)
    Args.push_back("-fat64");

  Args.push_back("-output");
  Args.push_back(OutputFileName.data());

  if (Options.Verbose) {
    outs() << "Running lipo\n";
    for (auto Arg : Args)
      outs() << ' ' << Arg;
    outs() << "\n";
  }

  return Options.NoOutput ? true : runLipo(SDKPath, Args);
}

// Return a MachO::segment_command_64 that holds the same values as the passed
// MachO::segment_command. We do that to avoid having to duplicate the logic
// for 32bits and 64bits segments.
struct MachO::segment_command_64 adaptFrom32bits(MachO::segment_command Seg) {
  MachO::segment_command_64 Seg64;
  Seg64.cmd = Seg.cmd;
  Seg64.cmdsize = Seg.cmdsize;
  memcpy(Seg64.segname, Seg.segname, sizeof(Seg.segname));
  Seg64.vmaddr = Seg.vmaddr;
  Seg64.vmsize = Seg.vmsize;
  Seg64.fileoff = Seg.fileoff;
  Seg64.filesize = Seg.filesize;
  Seg64.maxprot = Seg.maxprot;
  Seg64.initprot = Seg.initprot;
  Seg64.nsects = Seg.nsects;
  Seg64.flags = Seg.flags;
  return Seg64;
}

// Iterate on all \a Obj segments, and apply \a Handler to them.
template <typename FunctionTy>
static void iterateOnSegments(const object::MachOObjectFile &Obj,
                              FunctionTy Handler) {
  for (const auto &LCI : Obj.load_commands()) {
    MachO::segment_command_64 Segment;
    if (LCI.C.cmd == MachO::LC_SEGMENT)
      Segment = adaptFrom32bits(Obj.getSegmentLoadCommand(LCI));
    else if (LCI.C.cmd == MachO::LC_SEGMENT_64)
      Segment = Obj.getSegment64LoadCommand(LCI);
    else
      continue;

    Handler(Segment);
  }
}

// Transfer the symbols described by \a NList to \a NewSymtab which is just the
// raw contents of the symbol table for the dSYM companion file. \returns
// whether the symbol was transferred or not.
template <typename NListTy>
static bool transferSymbol(NListTy NList, bool IsLittleEndian,
                           StringRef Strings, SmallVectorImpl<char> &NewSymtab,
                           NonRelocatableStringpool &NewStrings,
                           bool &InDebugNote) {
  // Do not transfer undefined symbols, we want real addresses.
  if ((NList.n_type & MachO::N_TYPE) == MachO::N_UNDF)
    return false;

  // Do not transfer N_AST symbols as their content is copied into a section of
  // the Mach-O companion file.
  if (NList.n_type == MachO::N_AST)
    return false;

  StringRef Name = StringRef(Strings.begin() + NList.n_strx);

  // An N_SO with a filename opens a debugging scope and another one without a
  // name closes it. Don't transfer anything in the debugging scope.
  if (InDebugNote) {
    InDebugNote =
        (NList.n_type != MachO::N_SO) || (!Name.empty() && Name[0] != '\0');
    return false;
  } else if (NList.n_type == MachO::N_SO) {
    InDebugNote = true;
    return false;
  }

  // FIXME: The + 1 is here to mimic dsymutil-classic that has 2 empty
  // strings at the start of the generated string table (There is
  // corresponding code in the string table emission).
  NList.n_strx = NewStrings.getStringOffset(Name) + 1;
  if (IsLittleEndian != sys::IsLittleEndianHost)
    MachO::swapStruct(NList);

  NewSymtab.append(reinterpret_cast<char *>(&NList),
                   reinterpret_cast<char *>(&NList + 1));
  return true;
}

// Wrapper around transferSymbol to transfer all of \a Obj symbols
// to \a NewSymtab. This function does not write in the output file.
// \returns the number of symbols in \a NewSymtab.
static unsigned transferSymbols(const object::MachOObjectFile &Obj,
                                SmallVectorImpl<char> &NewSymtab,
                                NonRelocatableStringpool &NewStrings) {
  unsigned Syms = 0;
  StringRef Strings = Obj.getStringTableData();
  bool IsLittleEndian = Obj.isLittleEndian();
  bool InDebugNote = false;

  if (Obj.is64Bit()) {
    for (const object::SymbolRef &Symbol : Obj.symbols()) {
      object::DataRefImpl DRI = Symbol.getRawDataRefImpl();
      if (transferSymbol(Obj.getSymbol64TableEntry(DRI), IsLittleEndian,
                         Strings, NewSymtab, NewStrings, InDebugNote))
        ++Syms;
    }
  } else {
    for (const object::SymbolRef &Symbol : Obj.symbols()) {
      object::DataRefImpl DRI = Symbol.getRawDataRefImpl();
      if (transferSymbol(Obj.getSymbolTableEntry(DRI), IsLittleEndian, Strings,
                         NewSymtab, NewStrings, InDebugNote))
        ++Syms;
    }
  }
  return Syms;
}

static MachO::section
getSection(const object::MachOObjectFile &Obj,
           const MachO::segment_command &Seg,
           const object::MachOObjectFile::LoadCommandInfo &LCI, unsigned Idx) {
  return Obj.getSection(LCI, Idx);
}

static MachO::section_64
getSection(const object::MachOObjectFile &Obj,
           const MachO::segment_command_64 &Seg,
           const object::MachOObjectFile::LoadCommandInfo &LCI, unsigned Idx) {
  return Obj.getSection64(LCI, Idx);
}

// Transfer \a Segment from \a Obj to the output file. This calls into \a Writer
// to write these load commands directly in the output file at the current
// position.
//
// The function also tries to find a hole in the address map to fit the __DWARF
// segment of \a DwarfSegmentSize size. \a EndAddress is updated to point at the
// highest segment address.
//
// When the __LINKEDIT segment is transferred, its offset and size are set resp.
// to \a LinkeditOffset and \a LinkeditSize.
//
// When the eh_frame section is transferred, its offset and size are set resp.
// to \a EHFrameOffset and \a EHFrameSize.
template <typename SegmentTy>
static void transferSegmentAndSections(
    const object::MachOObjectFile::LoadCommandInfo &LCI, SegmentTy Segment,
    const object::MachOObjectFile &Obj, MachObjectWriter &Writer,
    uint64_t LinkeditOffset, uint64_t LinkeditSize, uint64_t EHFrameOffset,
    uint64_t EHFrameSize, uint64_t DwarfSegmentSize, uint64_t &GapForDwarf,
    uint64_t &EndAddress) {
  if (StringRef("__DWARF") == Segment.segname)
    return;

  if (StringRef("__TEXT") == Segment.segname && EHFrameSize > 0) {
    Segment.fileoff = EHFrameOffset;
    Segment.filesize = EHFrameSize;
  } else if (StringRef("__LINKEDIT") == Segment.segname) {
    Segment.fileoff = LinkeditOffset;
    Segment.filesize = LinkeditSize;
    // Resize vmsize by rounding to the page size.
    Segment.vmsize = alignTo(LinkeditSize, 0x1000);
  } else {
    Segment.fileoff = Segment.filesize = 0;
  }

  // Check if the end address of the last segment and our current
  // start address leave a sufficient gap to store the __DWARF
  // segment.
  uint64_t PrevEndAddress = EndAddress;
  EndAddress = alignTo(EndAddress, 0x1000);
  if (GapForDwarf == UINT64_MAX && Segment.vmaddr > EndAddress &&
      Segment.vmaddr - EndAddress >= DwarfSegmentSize)
    GapForDwarf = EndAddress;

  // The segments are not necessarily sorted by their vmaddr.
  EndAddress =
      std::max<uint64_t>(PrevEndAddress, Segment.vmaddr + Segment.vmsize);
  unsigned nsects = Segment.nsects;
  if (Obj.isLittleEndian() != sys::IsLittleEndianHost)
    MachO::swapStruct(Segment);
  Writer.W.OS.write(reinterpret_cast<char *>(&Segment), sizeof(Segment));
  for (unsigned i = 0; i < nsects; ++i) {
    auto Sect = getSection(Obj, Segment, LCI, i);
    if (StringRef("__eh_frame") == Sect.sectname) {
      Sect.offset = EHFrameOffset;
      Sect.reloff = Sect.nreloc = 0;
    } else {
      Sect.offset = Sect.reloff = Sect.nreloc = 0;
    }
    if (Obj.isLittleEndian() != sys::IsLittleEndianHost)
      MachO::swapStruct(Sect);
    Writer.W.OS.write(reinterpret_cast<char *>(&Sect), sizeof(Sect));
  }
}

// Write the __DWARF segment load command to the output file.
static bool createDwarfSegment(const MCAssembler& Asm,uint64_t VMAddr, uint64_t FileOffset,
                               uint64_t FileSize, unsigned NumSections,
                                MachObjectWriter &Writer) {
  Writer.writeSegmentLoadCommand("__DWARF", NumSections, VMAddr,
                                 alignTo(FileSize, 0x1000), FileOffset,
                                 FileSize, /* MaxProt */ 7,
                                 /* InitProt =*/3);

  for (unsigned int i = 0, n = Writer.getSectionOrder().size(); i != n; ++i) {
    MCSection *Sec = Writer.getSectionOrder()[i];
    if (!Asm.getSectionFileSize(*Sec))
      continue;

    Align Alignment = Sec->getAlign();
    if (Alignment > 1) {
      VMAddr = alignTo(VMAddr, Alignment);
      FileOffset = alignTo(FileOffset, Alignment);
      if (FileOffset > UINT32_MAX)
        return error("section " + Sec->getName() +
                     "'s file offset exceeds 4GB."
                     " Refusing to produce an invalid Mach-O file.");
    }
    Writer.writeSection(Asm, *Sec, VMAddr, FileOffset, 0, 0, 0);

    FileOffset += Asm.getSectionAddressSize(*Sec);
    VMAddr += Asm.getSectionAddressSize(*Sec);
  }
  return true;
}

static bool isExecutable(const object::MachOObjectFile &Obj) {
  if (Obj.is64Bit())
    return Obj.getHeader64().filetype != MachO::MH_OBJECT;
  else
    return Obj.getHeader().filetype != MachO::MH_OBJECT;
}

static unsigned segmentLoadCommandSize(bool Is64Bit, unsigned NumSections) {
  if (Is64Bit)
    return sizeof(MachO::segment_command_64) +
           NumSections * sizeof(MachO::section_64);

  return sizeof(MachO::segment_command) + NumSections * sizeof(MachO::section);
}

// Stream a dSYM companion binary file corresponding to the binary referenced
// by \a DM to \a OutFile. The passed \a MS MCStreamer is setup to write to
// \a OutFile and it must be using a MachObjectWriter object to do so.
bool generateDsymCompanion(
    llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> VFS, const DebugMap &DM,
    MCStreamer &MS, raw_fd_ostream &OutFile,
    const std::vector<MachOUtils::DwarfRelocationApplicationInfo>
        &RelocationsToApply) {
  auto &ObjectStreamer = static_cast<MCObjectStreamer &>(MS);
  MCAssembler &MCAsm = ObjectStreamer.getAssembler();
  auto &Writer = static_cast<MachObjectWriter &>(MCAsm.getWriter());

  // Layout but don't emit.
  MCAsm.layout();

  BinaryHolder InputBinaryHolder(VFS, false);

  auto ObjectEntry = InputBinaryHolder.getObjectEntry(DM.getBinaryPath());
  if (!ObjectEntry) {
    auto Err = ObjectEntry.takeError();
    return error(Twine("opening ") + DM.getBinaryPath() + ": " +
                     toString(std::move(Err)),
                 "output file streaming");
  }

  auto Object =
      ObjectEntry->getObjectAs<object::MachOObjectFile>(DM.getTriple());
  if (!Object) {
    auto Err = Object.takeError();
    return error(Twine("opening ") + DM.getBinaryPath() + ": " +
                     toString(std::move(Err)),
                 "output file streaming");
  }

  auto &InputBinary = *Object;

  bool Is64Bit = Writer.is64Bit();
  MachO::symtab_command SymtabCmd = InputBinary.getSymtabLoadCommand();

  // Compute the number of load commands we will need.
  unsigned LoadCommandSize = 0;
  unsigned NumLoadCommands = 0;

  bool HasSymtab = false;

  // Check LC_SYMTAB and get LC_UUID and LC_BUILD_VERSION.
  MachO::uuid_command UUIDCmd;
  SmallVector<MachO::build_version_command, 2> BuildVersionCmd;
  memset(&UUIDCmd, 0, sizeof(UUIDCmd));
  for (auto &LCI : InputBinary.load_commands()) {
    switch (LCI.C.cmd) {
    case MachO::LC_UUID:
      if (UUIDCmd.cmd)
        return error("Binary contains more than one UUID");
      UUIDCmd = InputBinary.getUuidCommand(LCI);
      ++NumLoadCommands;
      LoadCommandSize += sizeof(UUIDCmd);
      break;
    case MachO::LC_BUILD_VERSION: {
      MachO::build_version_command Cmd;
      memset(&Cmd, 0, sizeof(Cmd));
      Cmd = InputBinary.getBuildVersionLoadCommand(LCI);
      ++NumLoadCommands;
      LoadCommandSize += sizeof(Cmd);
      // LLDB doesn't care about the build tools for now.
      Cmd.ntools = 0;
      BuildVersionCmd.push_back(Cmd);
      break;
    }
    case MachO::LC_SYMTAB:
      HasSymtab = true;
      break;
    default:
      break;
    }
  }

  // If we have a valid symtab to copy, do it.
  bool ShouldEmitSymtab = HasSymtab && isExecutable(InputBinary);
  if (ShouldEmitSymtab) {
    LoadCommandSize += sizeof(MachO::symtab_command);
    ++NumLoadCommands;
  }

  // If we have a valid eh_frame to copy, do it.
  uint64_t EHFrameSize = 0;
  StringRef EHFrameData;
  for (const object::SectionRef &Section : InputBinary.sections()) {
    Expected<StringRef> NameOrErr = Section.getName();
    if (!NameOrErr) {
      consumeError(NameOrErr.takeError());
      continue;
    }
    StringRef SectionName = *NameOrErr;
    SectionName = SectionName.substr(SectionName.find_first_not_of("._"));
    if (SectionName == "eh_frame") {
      if (Expected<StringRef> ContentsOrErr = Section.getContents()) {
        EHFrameData = *ContentsOrErr;
        EHFrameSize = Section.getSize();
      } else {
        consumeError(ContentsOrErr.takeError());
      }
    }
  }

  unsigned HeaderSize =
      Is64Bit ? sizeof(MachO::mach_header_64) : sizeof(MachO::mach_header);
  // We will copy every segment that isn't __DWARF.
  iterateOnSegments(InputBinary, [&](const MachO::segment_command_64 &Segment) {
    if (StringRef("__DWARF") == Segment.segname)
      return;

    ++NumLoadCommands;
    LoadCommandSize += segmentLoadCommandSize(Is64Bit, Segment.nsects);
  });

  // We will add our own brand new __DWARF segment if we have debug
  // info.
  unsigned NumDwarfSections = 0;
  uint64_t DwarfSegmentSize = 0;

  for (unsigned int i = 0, n = Writer.getSectionOrder().size(); i != n; ++i) {
    MCSection *Sec = Writer.getSectionOrder()[i];
    if (Sec->begin() == Sec->end())
      continue;

    if (uint64_t Size = MCAsm.getSectionFileSize(*Sec)) {
      DwarfSegmentSize = alignTo(DwarfSegmentSize, Sec->getAlign());
      DwarfSegmentSize += Size;
      ++NumDwarfSections;
    }
  }

  if (NumDwarfSections) {
    ++NumLoadCommands;
    LoadCommandSize += segmentLoadCommandSize(Is64Bit, NumDwarfSections);
  }

  SmallString<0> NewSymtab;
  // Legacy dsymutil puts an empty string at the start of the line table.
  // thus we set NonRelocatableStringpool(,PutEmptyString=true)
  NonRelocatableStringpool NewStrings(true);
  unsigned NListSize = Is64Bit ? sizeof(MachO::nlist_64) : sizeof(MachO::nlist);
  unsigned NumSyms = 0;
  uint64_t NewStringsSize = 0;
  if (ShouldEmitSymtab) {
    NewSymtab.reserve(SymtabCmd.nsyms * NListSize / 2);
    NumSyms = transferSymbols(InputBinary, NewSymtab, NewStrings);
    NewStringsSize = NewStrings.getSize() + 1;
  }

  uint64_t SymtabStart = LoadCommandSize;
  SymtabStart += HeaderSize;
  SymtabStart = alignTo(SymtabStart, 0x1000);

  // We gathered all the information we need, start emitting the output file.
  Writer.writeHeader(MachO::MH_DSYM, NumLoadCommands, LoadCommandSize, false);

  // Write the load commands.
  assert(OutFile.tell() == HeaderSize);
  if (UUIDCmd.cmd != 0) {
    Writer.W.write<uint32_t>(UUIDCmd.cmd);
    Writer.W.write<uint32_t>(sizeof(UUIDCmd));
    OutFile.write(reinterpret_cast<const char *>(UUIDCmd.uuid), 16);
    assert(OutFile.tell() == HeaderSize + sizeof(UUIDCmd));
  }
  for (auto Cmd : BuildVersionCmd) {
    Writer.W.write<uint32_t>(Cmd.cmd);
    Writer.W.write<uint32_t>(sizeof(Cmd));
    Writer.W.write<uint32_t>(Cmd.platform);
    Writer.W.write<uint32_t>(Cmd.minos);
    Writer.W.write<uint32_t>(Cmd.sdk);
    Writer.W.write<uint32_t>(Cmd.ntools);
  }

  assert(SymtabCmd.cmd && "No symbol table.");
  uint64_t StringStart = SymtabStart + NumSyms * NListSize;
  if (ShouldEmitSymtab)
    Writer.writeSymtabLoadCommand(SymtabStart, NumSyms, StringStart,
                                  NewStringsSize);

  uint64_t EHFrameStart = StringStart + NewStringsSize;
  EHFrameStart = alignTo(EHFrameStart, 0x1000);

  uint64_t DwarfSegmentStart = EHFrameStart + EHFrameSize;
  DwarfSegmentStart = alignTo(DwarfSegmentStart, 0x1000);

  // Write the load commands for the segments and sections we 'import' from
  // the original binary.
  uint64_t EndAddress = 0;
  uint64_t GapForDwarf = UINT64_MAX;
  for (auto &LCI : InputBinary.load_commands()) {
    if (LCI.C.cmd == MachO::LC_SEGMENT)
      transferSegmentAndSections(
          LCI, InputBinary.getSegmentLoadCommand(LCI), InputBinary, Writer,
          SymtabStart, StringStart + NewStringsSize - SymtabStart, EHFrameStart,
          EHFrameSize, DwarfSegmentSize, GapForDwarf, EndAddress);
    else if (LCI.C.cmd == MachO::LC_SEGMENT_64)
      transferSegmentAndSections(
          LCI, InputBinary.getSegment64LoadCommand(LCI), InputBinary, Writer,
          SymtabStart, StringStart + NewStringsSize - SymtabStart, EHFrameStart,
          EHFrameSize, DwarfSegmentSize, GapForDwarf, EndAddress);
  }

  uint64_t DwarfVMAddr = alignTo(EndAddress, 0x1000);
  uint64_t DwarfVMMax = Is64Bit ? UINT64_MAX : UINT32_MAX;
  if (DwarfVMAddr + DwarfSegmentSize > DwarfVMMax ||
      DwarfVMAddr + DwarfSegmentSize < DwarfVMAddr /* Overflow */) {
    // There is no room for the __DWARF segment at the end of the
    // address space. Look through segments to find a gap.
    DwarfVMAddr = GapForDwarf;
    if (DwarfVMAddr == UINT64_MAX)
      warn("not enough VM space for the __DWARF segment.",
           "output file streaming");
  }

  // Write the load command for the __DWARF segment.
  if (!createDwarfSegment(MCAsm, DwarfVMAddr, DwarfSegmentStart, DwarfSegmentSize,
                          NumDwarfSections, Writer))
    return false;

  assert(OutFile.tell() == LoadCommandSize + HeaderSize);
  OutFile.write_zeros(SymtabStart - (LoadCommandSize + HeaderSize));
  assert(OutFile.tell() == SymtabStart);

  // Transfer symbols.
  if (ShouldEmitSymtab) {
    OutFile << NewSymtab.str();
    assert(OutFile.tell() == StringStart);

    // Transfer string table.
    // FIXME: The NonRelocatableStringpool starts with an empty string, but
    // dsymutil-classic starts the reconstructed string table with 2 of these.
    // Reproduce that behavior for now (there is corresponding code in
    // transferSymbol).
    OutFile << '\0';
    std::vector<DwarfStringPoolEntryRef> Strings =
        NewStrings.getEntriesForEmission();
    for (auto EntryRef : Strings) {
      OutFile.write(EntryRef.getString().data(),
                    EntryRef.getString().size() + 1);
    }
  }
  assert(OutFile.tell() == StringStart + NewStringsSize);

  // Pad till the EH frame start.
  OutFile.write_zeros(EHFrameStart - (StringStart + NewStringsSize));
  assert(OutFile.tell() == EHFrameStart);

  // Transfer eh_frame.
  if (EHFrameSize > 0)
    OutFile << EHFrameData;
  assert(OutFile.tell() == EHFrameStart + EHFrameSize);

  // Pad till the Dwarf segment start.
  OutFile.write_zeros(DwarfSegmentStart - (EHFrameStart + EHFrameSize));
  assert(OutFile.tell() == DwarfSegmentStart);

  // Emit the Dwarf sections contents.
  for (const MCSection &Sec : MCAsm) {
    uint64_t Pos = OutFile.tell();
    OutFile.write_zeros(alignTo(Pos, Sec.getAlign()) - Pos);
    MCAsm.writeSectionData(OutFile, &Sec);
  }

  // Apply relocations to the contents of the DWARF segment.
  // We do this here because the final value written depend on the DWARF vm
  // addr, which is only calculated in this function.
  if (!RelocationsToApply.empty()) {
    if (!OutFile.supportsSeeking())
      report_fatal_error(
          "Cannot apply relocations to file that doesn't support seeking!");

    uint64_t Pos = OutFile.tell();
    for (auto &RelocationToApply : RelocationsToApply) {
      OutFile.seek(DwarfSegmentStart + RelocationToApply.AddressFromDwarfStart);
      int32_t Value = RelocationToApply.Value;
      if (RelocationToApply.ShouldSubtractDwarfVM)
        Value -= DwarfVMAddr;
      OutFile.write((char *)&Value, sizeof(int32_t));
    }
    OutFile.seek(Pos);
  }

  return true;
}
} // namespace MachOUtils
} // namespace dsymutil
} // namespace llvm
