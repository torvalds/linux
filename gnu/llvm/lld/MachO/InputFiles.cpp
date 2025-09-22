//===- InputFiles.cpp -----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains functions to parse Mach-O object files. In this comment,
// we describe the Mach-O file structure and how we parse it.
//
// Mach-O is not very different from ELF or COFF. The notion of symbols,
// sections and relocations exists in Mach-O as it does in ELF and COFF.
//
// Perhaps the notion that is new to those who know ELF/COFF is "subsections".
// In ELF/COFF, sections are an atomic unit of data copied from input files to
// output files. When we merge or garbage-collect sections, we treat each
// section as an atomic unit. In Mach-O, that's not the case. Sections can
// consist of multiple subsections, and subsections are a unit of merging and
// garbage-collecting. Therefore, Mach-O's subsections are more similar to
// ELF/COFF's sections than Mach-O's sections are.
//
// A section can have multiple symbols. A symbol that does not have the
// N_ALT_ENTRY attribute indicates a beginning of a subsection. Therefore, by
// definition, a symbol is always present at the beginning of each subsection. A
// symbol with N_ALT_ENTRY attribute does not start a new subsection and can
// point to a middle of a subsection.
//
// The notion of subsections also affects how relocations are represented in
// Mach-O. All references within a section need to be explicitly represented as
// relocations if they refer to different subsections, because we obviously need
// to fix up addresses if subsections are laid out in an output file differently
// than they were in object files. To represent that, Mach-O relocations can
// refer to an unnamed location via its address. Scattered relocations (those
// with the R_SCATTERED bit set) always refer to unnamed locations.
// Non-scattered relocations refer to an unnamed location if r_extern is not set
// and r_symbolnum is zero.
//
// Without the above differences, I think you can use your knowledge about ELF
// and COFF for Mach-O.
//
//===----------------------------------------------------------------------===//

#include "InputFiles.h"
#include "Config.h"
#include "Driver.h"
#include "Dwarf.h"
#include "EhFrame.h"
#include "ExportTrie.h"
#include "InputSection.h"
#include "MachOStructs.h"
#include "ObjC.h"
#include "OutputSection.h"
#include "OutputSegment.h"
#include "SymbolTable.h"
#include "Symbols.h"
#include "SyntheticSections.h"
#include "Target.h"

#include "lld/Common/CommonLinkerContext.h"
#include "lld/Common/DWARF.h"
#include "lld/Common/Reproduce.h"
#include "llvm/ADT/iterator.h"
#include "llvm/BinaryFormat/MachO.h"
#include "llvm/LTO/LTO.h"
#include "llvm/Support/BinaryStreamReader.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/LEB128.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/TarWriter.h"
#include "llvm/Support/TimeProfiler.h"
#include "llvm/TextAPI/Architecture.h"
#include "llvm/TextAPI/InterfaceFile.h"

#include <optional>
#include <type_traits>

using namespace llvm;
using namespace llvm::MachO;
using namespace llvm::support::endian;
using namespace llvm::sys;
using namespace lld;
using namespace lld::macho;

// Returns "<internal>", "foo.a(bar.o)", or "baz.o".
std::string lld::toString(const InputFile *f) {
  if (!f)
    return "<internal>";

  // Multiple dylibs can be defined in one .tbd file.
  if (const auto *dylibFile = dyn_cast<DylibFile>(f))
    if (f->getName().ends_with(".tbd"))
      return (f->getName() + "(" + dylibFile->installName + ")").str();

  if (f->archiveName.empty())
    return std::string(f->getName());
  return (f->archiveName + "(" + path::filename(f->getName()) + ")").str();
}

std::string lld::toString(const Section &sec) {
  return (toString(sec.file) + ":(" + sec.name + ")").str();
}

SetVector<InputFile *> macho::inputFiles;
std::unique_ptr<TarWriter> macho::tar;
int InputFile::idCount = 0;

static VersionTuple decodeVersion(uint32_t version) {
  unsigned major = version >> 16;
  unsigned minor = (version >> 8) & 0xffu;
  unsigned subMinor = version & 0xffu;
  return VersionTuple(major, minor, subMinor);
}

static std::vector<PlatformInfo> getPlatformInfos(const InputFile *input) {
  if (!isa<ObjFile>(input) && !isa<DylibFile>(input))
    return {};

  const char *hdr = input->mb.getBufferStart();

  // "Zippered" object files can have multiple LC_BUILD_VERSION load commands.
  std::vector<PlatformInfo> platformInfos;
  for (auto *cmd : findCommands<build_version_command>(hdr, LC_BUILD_VERSION)) {
    PlatformInfo info;
    info.target.Platform = static_cast<PlatformType>(cmd->platform);
    info.target.MinDeployment = decodeVersion(cmd->minos);
    platformInfos.emplace_back(std::move(info));
  }
  for (auto *cmd : findCommands<version_min_command>(
           hdr, LC_VERSION_MIN_MACOSX, LC_VERSION_MIN_IPHONEOS,
           LC_VERSION_MIN_TVOS, LC_VERSION_MIN_WATCHOS)) {
    PlatformInfo info;
    switch (cmd->cmd) {
    case LC_VERSION_MIN_MACOSX:
      info.target.Platform = PLATFORM_MACOS;
      break;
    case LC_VERSION_MIN_IPHONEOS:
      info.target.Platform = PLATFORM_IOS;
      break;
    case LC_VERSION_MIN_TVOS:
      info.target.Platform = PLATFORM_TVOS;
      break;
    case LC_VERSION_MIN_WATCHOS:
      info.target.Platform = PLATFORM_WATCHOS;
      break;
    }
    info.target.MinDeployment = decodeVersion(cmd->version);
    platformInfos.emplace_back(std::move(info));
  }

  return platformInfos;
}

static bool checkCompatibility(const InputFile *input) {
  std::vector<PlatformInfo> platformInfos = getPlatformInfos(input);
  if (platformInfos.empty())
    return true;

  auto it = find_if(platformInfos, [&](const PlatformInfo &info) {
    return removeSimulator(info.target.Platform) ==
           removeSimulator(config->platform());
  });
  if (it == platformInfos.end()) {
    std::string platformNames;
    raw_string_ostream os(platformNames);
    interleave(
        platformInfos, os,
        [&](const PlatformInfo &info) {
          os << getPlatformName(info.target.Platform);
        },
        "/");
    error(toString(input) + " has platform " + platformNames +
          Twine(", which is different from target platform ") +
          getPlatformName(config->platform()));
    return false;
  }

  if (it->target.MinDeployment > config->platformInfo.target.MinDeployment)
    warn(toString(input) + " has version " +
         it->target.MinDeployment.getAsString() +
         ", which is newer than target minimum of " +
         config->platformInfo.target.MinDeployment.getAsString());

  return true;
}

template <class Header>
static bool compatWithTargetArch(const InputFile *file, const Header *hdr) {
  uint32_t cpuType;
  std::tie(cpuType, std::ignore) = getCPUTypeFromArchitecture(config->arch());

  if (hdr->cputype != cpuType) {
    Architecture arch =
        getArchitectureFromCpuType(hdr->cputype, hdr->cpusubtype);
    auto msg = config->errorForArchMismatch
                   ? static_cast<void (*)(const Twine &)>(error)
                   : warn;

    msg(toString(file) + " has architecture " + getArchitectureName(arch) +
        " which is incompatible with target architecture " +
        getArchitectureName(config->arch()));
    return false;
  }

  return checkCompatibility(file);
}

// This cache mostly exists to store system libraries (and .tbds) as they're
// loaded, rather than the input archives, which are already cached at a higher
// level, and other files like the filelist that are only read once.
// Theoretically this caching could be more efficient by hoisting it, but that
// would require altering many callers to track the state.
DenseMap<CachedHashStringRef, MemoryBufferRef> macho::cachedReads;
// Open a given file path and return it as a memory-mapped file.
std::optional<MemoryBufferRef> macho::readFile(StringRef path) {
  CachedHashStringRef key(path);
  auto entry = cachedReads.find(key);
  if (entry != cachedReads.end())
    return entry->second;

  ErrorOr<std::unique_ptr<MemoryBuffer>> mbOrErr = MemoryBuffer::getFile(path);
  if (std::error_code ec = mbOrErr.getError()) {
    error("cannot open " + path + ": " + ec.message());
    return std::nullopt;
  }

  std::unique_ptr<MemoryBuffer> &mb = *mbOrErr;
  MemoryBufferRef mbref = mb->getMemBufferRef();
  make<std::unique_ptr<MemoryBuffer>>(std::move(mb)); // take mb ownership

  // If this is a regular non-fat file, return it.
  const char *buf = mbref.getBufferStart();
  const auto *hdr = reinterpret_cast<const fat_header *>(buf);
  if (mbref.getBufferSize() < sizeof(uint32_t) ||
      read32be(&hdr->magic) != FAT_MAGIC) {
    if (tar)
      tar->append(relativeToRoot(path), mbref.getBuffer());
    return cachedReads[key] = mbref;
  }

  llvm::BumpPtrAllocator &bAlloc = lld::bAlloc();

  // Object files and archive files may be fat files, which contain multiple
  // real files for different CPU ISAs. Here, we search for a file that matches
  // with the current link target and returns it as a MemoryBufferRef.
  const auto *arch = reinterpret_cast<const fat_arch *>(buf + sizeof(*hdr));
  auto getArchName = [](uint32_t cpuType, uint32_t cpuSubtype) {
    return getArchitectureName(getArchitectureFromCpuType(cpuType, cpuSubtype));
  };

  std::vector<StringRef> archs;
  for (uint32_t i = 0, n = read32be(&hdr->nfat_arch); i < n; ++i) {
    if (reinterpret_cast<const char *>(arch + i + 1) >
        buf + mbref.getBufferSize()) {
      error(path + ": fat_arch struct extends beyond end of file");
      return std::nullopt;
    }

    uint32_t cpuType = read32be(&arch[i].cputype);
    uint32_t cpuSubtype =
        read32be(&arch[i].cpusubtype) & ~MachO::CPU_SUBTYPE_MASK;

    // FIXME: LD64 has a more complex fallback logic here.
    // Consider implementing that as well?
    if (cpuType != static_cast<uint32_t>(target->cpuType) ||
        cpuSubtype != target->cpuSubtype) {
      archs.emplace_back(getArchName(cpuType, cpuSubtype));
      continue;
    }

    uint32_t offset = read32be(&arch[i].offset);
    uint32_t size = read32be(&arch[i].size);
    if (offset + size > mbref.getBufferSize())
      error(path + ": slice extends beyond end of file");
    if (tar)
      tar->append(relativeToRoot(path), mbref.getBuffer());
    return cachedReads[key] = MemoryBufferRef(StringRef(buf + offset, size),
                                              path.copy(bAlloc));
  }

  auto targetArchName = getArchName(target->cpuType, target->cpuSubtype);
  warn(path + ": ignoring file because it is universal (" + join(archs, ",") +
       ") but does not contain the " + targetArchName + " architecture");
  return std::nullopt;
}

InputFile::InputFile(Kind kind, const InterfaceFile &interface)
    : id(idCount++), fileKind(kind), name(saver().save(interface.getPath())) {}

// Some sections comprise of fixed-size records, so instead of splitting them at
// symbol boundaries, we split them based on size. Records are distinct from
// literals in that they may contain references to other sections, instead of
// being leaf nodes in the InputSection graph.
//
// Note that "record" is a term I came up with. In contrast, "literal" is a term
// used by the Mach-O format.
static std::optional<size_t> getRecordSize(StringRef segname, StringRef name) {
  if (name == section_names::compactUnwind) {
    if (segname == segment_names::ld)
      return target->wordSize == 8 ? 32 : 20;
  }
  if (!config->dedupStrings)
    return {};

  if (name == section_names::cfString && segname == segment_names::data)
    return target->wordSize == 8 ? 32 : 16;

  if (config->icfLevel == ICFLevel::none)
    return {};

  if (name == section_names::objcClassRefs && segname == segment_names::data)
    return target->wordSize;

  if (name == section_names::objcSelrefs && segname == segment_names::data)
    return target->wordSize;
  return {};
}

static Error parseCallGraph(ArrayRef<uint8_t> data,
                            std::vector<CallGraphEntry> &callGraph) {
  TimeTraceScope timeScope("Parsing call graph section");
  BinaryStreamReader reader(data, llvm::endianness::little);
  while (!reader.empty()) {
    uint32_t fromIndex, toIndex;
    uint64_t count;
    if (Error err = reader.readInteger(fromIndex))
      return err;
    if (Error err = reader.readInteger(toIndex))
      return err;
    if (Error err = reader.readInteger(count))
      return err;
    callGraph.emplace_back(fromIndex, toIndex, count);
  }
  return Error::success();
}

// Parse the sequence of sections within a single LC_SEGMENT(_64).
// Split each section into subsections.
template <class SectionHeader>
void ObjFile::parseSections(ArrayRef<SectionHeader> sectionHeaders) {
  sections.reserve(sectionHeaders.size());
  auto *buf = reinterpret_cast<const uint8_t *>(mb.getBufferStart());

  for (const SectionHeader &sec : sectionHeaders) {
    StringRef name =
        StringRef(sec.sectname, strnlen(sec.sectname, sizeof(sec.sectname)));
    StringRef segname =
        StringRef(sec.segname, strnlen(sec.segname, sizeof(sec.segname)));
    sections.push_back(make<Section>(this, segname, name, sec.flags, sec.addr));
    if (sec.align >= 32) {
      error("alignment " + std::to_string(sec.align) + " of section " + name +
            " is too large");
      continue;
    }
    Section &section = *sections.back();
    uint32_t align = 1 << sec.align;
    ArrayRef<uint8_t> data = {isZeroFill(sec.flags) ? nullptr
                                                    : buf + sec.offset,
                              static_cast<size_t>(sec.size)};

    auto splitRecords = [&](size_t recordSize) -> void {
      if (data.empty())
        return;
      Subsections &subsections = section.subsections;
      subsections.reserve(data.size() / recordSize);
      for (uint64_t off = 0; off < data.size(); off += recordSize) {
        auto *isec = make<ConcatInputSection>(
            section, data.slice(off, std::min(data.size(), recordSize)), align);
        subsections.push_back({off, isec});
      }
      section.doneSplitting = true;
    };

    if (sectionType(sec.flags) == S_CSTRING_LITERALS) {
      if (sec.nreloc)
        fatal(toString(this) + ": " + sec.segname + "," + sec.sectname +
              " contains relocations, which is unsupported");
      bool dedupLiterals =
          name == section_names::objcMethname || config->dedupStrings;
      InputSection *isec =
          make<CStringInputSection>(section, data, align, dedupLiterals);
      // FIXME: parallelize this?
      cast<CStringInputSection>(isec)->splitIntoPieces();
      section.subsections.push_back({0, isec});
    } else if (isWordLiteralSection(sec.flags)) {
      if (sec.nreloc)
        fatal(toString(this) + ": " + sec.segname + "," + sec.sectname +
              " contains relocations, which is unsupported");
      InputSection *isec = make<WordLiteralInputSection>(section, data, align);
      section.subsections.push_back({0, isec});
    } else if (auto recordSize = getRecordSize(segname, name)) {
      splitRecords(*recordSize);
    } else if (name == section_names::ehFrame &&
               segname == segment_names::text) {
      splitEhFrames(data, *sections.back());
    } else if (segname == segment_names::llvm) {
      if (config->callGraphProfileSort && name == section_names::cgProfile)
        checkError(parseCallGraph(data, callGraph));
      // ld64 does not appear to emit contents from sections within the __LLVM
      // segment. Symbols within those sections point to bitcode metadata
      // instead of actual symbols. Global symbols within those sections could
      // have the same name without causing duplicate symbol errors. To avoid
      // spurious duplicate symbol errors, we do not parse these sections.
      // TODO: Evaluate whether the bitcode metadata is needed.
    } else if (name == section_names::objCImageInfo &&
               segname == segment_names::data) {
      objCImageInfo = data;
    } else {
      if (name == section_names::addrSig)
        addrSigSection = sections.back();

      auto *isec = make<ConcatInputSection>(section, data, align);
      if (isDebugSection(isec->getFlags()) &&
          isec->getSegName() == segment_names::dwarf) {
        // Instead of emitting DWARF sections, we emit STABS symbols to the
        // object files that contain them. We filter them out early to avoid
        // parsing their relocations unnecessarily.
        debugSections.push_back(isec);
      } else {
        section.subsections.push_back({0, isec});
      }
    }
  }
}

void ObjFile::splitEhFrames(ArrayRef<uint8_t> data, Section &ehFrameSection) {
  EhReader reader(this, data, /*dataOff=*/0);
  size_t off = 0;
  while (off < reader.size()) {
    uint64_t frameOff = off;
    uint64_t length = reader.readLength(&off);
    if (length == 0)
      break;
    uint64_t fullLength = length + (off - frameOff);
    off += length;
    // We hard-code an alignment of 1 here because we don't actually want our
    // EH frames to be aligned to the section alignment. EH frame decoders don't
    // expect this alignment. Moreover, each EH frame must start where the
    // previous one ends, and where it ends is indicated by the length field.
    // Unless we update the length field (troublesome), we should keep the
    // alignment to 1.
    // Note that we still want to preserve the alignment of the overall section,
    // just not of the individual EH frames.
    ehFrameSection.subsections.push_back(
        {frameOff, make<ConcatInputSection>(ehFrameSection,
                                            data.slice(frameOff, fullLength),
                                            /*align=*/1)});
  }
  ehFrameSection.doneSplitting = true;
}

template <class T>
static Section *findContainingSection(const std::vector<Section *> &sections,
                                      T *offset) {
  static_assert(std::is_same<uint64_t, T>::value ||
                    std::is_same<uint32_t, T>::value,
                "unexpected type for offset");
  auto it = std::prev(llvm::upper_bound(
      sections, *offset,
      [](uint64_t value, const Section *sec) { return value < sec->addr; }));
  *offset -= (*it)->addr;
  return *it;
}

// Find the subsection corresponding to the greatest section offset that is <=
// that of the given offset.
//
// offset: an offset relative to the start of the original InputSection (before
// any subsection splitting has occurred). It will be updated to represent the
// same location as an offset relative to the start of the containing
// subsection.
template <class T>
static InputSection *findContainingSubsection(const Section &section,
                                              T *offset) {
  static_assert(std::is_same<uint64_t, T>::value ||
                    std::is_same<uint32_t, T>::value,
                "unexpected type for offset");
  auto it = std::prev(llvm::upper_bound(
      section.subsections, *offset,
      [](uint64_t value, Subsection subsec) { return value < subsec.offset; }));
  *offset -= it->offset;
  return it->isec;
}

// Find a symbol at offset `off` within `isec`.
static Defined *findSymbolAtOffset(const ConcatInputSection *isec,
                                   uint64_t off) {
  auto it = llvm::lower_bound(isec->symbols, off, [](Defined *d, uint64_t off) {
    return d->value < off;
  });
  // The offset should point at the exact address of a symbol (with no addend.)
  if (it == isec->symbols.end() || (*it)->value != off) {
    assert(isec->wasCoalesced);
    return nullptr;
  }
  return *it;
}

template <class SectionHeader>
static bool validateRelocationInfo(InputFile *file, const SectionHeader &sec,
                                   relocation_info rel) {
  const RelocAttrs &relocAttrs = target->getRelocAttrs(rel.r_type);
  bool valid = true;
  auto message = [relocAttrs, file, sec, rel, &valid](const Twine &diagnostic) {
    valid = false;
    return (relocAttrs.name + " relocation " + diagnostic + " at offset " +
            std::to_string(rel.r_address) + " of " + sec.segname + "," +
            sec.sectname + " in " + toString(file))
        .str();
  };

  if (!relocAttrs.hasAttr(RelocAttrBits::LOCAL) && !rel.r_extern)
    error(message("must be extern"));
  if (relocAttrs.hasAttr(RelocAttrBits::PCREL) != rel.r_pcrel)
    error(message(Twine("must ") + (rel.r_pcrel ? "not " : "") +
                  "be PC-relative"));
  if (isThreadLocalVariables(sec.flags) &&
      !relocAttrs.hasAttr(RelocAttrBits::UNSIGNED))
    error(message("not allowed in thread-local section, must be UNSIGNED"));
  if (rel.r_length < 2 || rel.r_length > 3 ||
      !relocAttrs.hasAttr(static_cast<RelocAttrBits>(1 << rel.r_length))) {
    static SmallVector<StringRef, 4> widths{"0", "4", "8", "4 or 8"};
    error(message("has width " + std::to_string(1 << rel.r_length) +
                  " bytes, but must be " +
                  widths[(static_cast<int>(relocAttrs.bits) >> 2) & 3] +
                  " bytes"));
  }
  return valid;
}

template <class SectionHeader>
void ObjFile::parseRelocations(ArrayRef<SectionHeader> sectionHeaders,
                               const SectionHeader &sec, Section &section) {
  auto *buf = reinterpret_cast<const uint8_t *>(mb.getBufferStart());
  ArrayRef<relocation_info> relInfos(
      reinterpret_cast<const relocation_info *>(buf + sec.reloff), sec.nreloc);

  Subsections &subsections = section.subsections;
  auto subsecIt = subsections.rbegin();
  for (size_t i = 0; i < relInfos.size(); i++) {
    // Paired relocations serve as Mach-O's method for attaching a
    // supplemental datum to a primary relocation record. ELF does not
    // need them because the *_RELOC_RELA records contain the extra
    // addend field, vs. *_RELOC_REL which omit the addend.
    //
    // The {X86_64,ARM64}_RELOC_SUBTRACTOR record holds the subtrahend,
    // and the paired *_RELOC_UNSIGNED record holds the minuend. The
    // datum for each is a symbolic address. The result is the offset
    // between two addresses.
    //
    // The ARM64_RELOC_ADDEND record holds the addend, and the paired
    // ARM64_RELOC_BRANCH26 or ARM64_RELOC_PAGE21/PAGEOFF12 holds the
    // base symbolic address.
    //
    // Note: X86 does not use *_RELOC_ADDEND because it can embed an addend into
    // the instruction stream. On X86, a relocatable address field always
    // occupies an entire contiguous sequence of byte(s), so there is no need to
    // merge opcode bits with address bits. Therefore, it's easy and convenient
    // to store addends in the instruction-stream bytes that would otherwise
    // contain zeroes. By contrast, RISC ISAs such as ARM64 mix opcode bits with
    // address bits so that bitwise arithmetic is necessary to extract and
    // insert them. Storing addends in the instruction stream is possible, but
    // inconvenient and more costly at link time.

    relocation_info relInfo = relInfos[i];
    bool isSubtrahend =
        target->hasAttr(relInfo.r_type, RelocAttrBits::SUBTRAHEND);
    int64_t pairedAddend = 0;
    if (target->hasAttr(relInfo.r_type, RelocAttrBits::ADDEND)) {
      pairedAddend = SignExtend64<24>(relInfo.r_symbolnum);
      relInfo = relInfos[++i];
    }
    assert(i < relInfos.size());
    if (!validateRelocationInfo(this, sec, relInfo))
      continue;
    if (relInfo.r_address & R_SCATTERED)
      fatal("TODO: Scattered relocations not supported");

    int64_t embeddedAddend = target->getEmbeddedAddend(mb, sec.offset, relInfo);
    assert(!(embeddedAddend && pairedAddend));
    int64_t totalAddend = pairedAddend + embeddedAddend;
    Reloc r;
    r.type = relInfo.r_type;
    r.pcrel = relInfo.r_pcrel;
    r.length = relInfo.r_length;
    r.offset = relInfo.r_address;
    if (relInfo.r_extern) {
      r.referent = symbols[relInfo.r_symbolnum];
      r.addend = isSubtrahend ? 0 : totalAddend;
    } else {
      assert(!isSubtrahend);
      const SectionHeader &referentSecHead =
          sectionHeaders[relInfo.r_symbolnum - 1];
      uint64_t referentOffset;
      if (relInfo.r_pcrel) {
        // The implicit addend for pcrel section relocations is the pcrel offset
        // in terms of the addresses in the input file. Here we adjust it so
        // that it describes the offset from the start of the referent section.
        // FIXME This logic was written around x86_64 behavior -- ARM64 doesn't
        // have pcrel section relocations. We may want to factor this out into
        // the arch-specific .cpp file.
        assert(target->hasAttr(r.type, RelocAttrBits::BYTE4));
        referentOffset = sec.addr + relInfo.r_address + 4 + totalAddend -
                         referentSecHead.addr;
      } else {
        // The addend for a non-pcrel relocation is its absolute address.
        referentOffset = totalAddend - referentSecHead.addr;
      }
      r.referent = findContainingSubsection(*sections[relInfo.r_symbolnum - 1],
                                            &referentOffset);
      r.addend = referentOffset;
    }

    // Find the subsection that this relocation belongs to.
    // Though not required by the Mach-O format, clang and gcc seem to emit
    // relocations in order, so let's take advantage of it. However, ld64 emits
    // unsorted relocations (in `-r` mode), so we have a fallback for that
    // uncommon case.
    InputSection *subsec;
    while (subsecIt != subsections.rend() && subsecIt->offset > r.offset)
      ++subsecIt;
    if (subsecIt == subsections.rend() ||
        subsecIt->offset + subsecIt->isec->getSize() <= r.offset) {
      subsec = findContainingSubsection(section, &r.offset);
      // Now that we know the relocs are unsorted, avoid trying the 'fast path'
      // for the other relocations.
      subsecIt = subsections.rend();
    } else {
      subsec = subsecIt->isec;
      r.offset -= subsecIt->offset;
    }
    subsec->relocs.push_back(r);

    if (isSubtrahend) {
      relocation_info minuendInfo = relInfos[++i];
      // SUBTRACTOR relocations should always be followed by an UNSIGNED one
      // attached to the same address.
      assert(target->hasAttr(minuendInfo.r_type, RelocAttrBits::UNSIGNED) &&
             relInfo.r_address == minuendInfo.r_address);
      Reloc p;
      p.type = minuendInfo.r_type;
      if (minuendInfo.r_extern) {
        p.referent = symbols[minuendInfo.r_symbolnum];
        p.addend = totalAddend;
      } else {
        uint64_t referentOffset =
            totalAddend - sectionHeaders[minuendInfo.r_symbolnum - 1].addr;
        p.referent = findContainingSubsection(
            *sections[minuendInfo.r_symbolnum - 1], &referentOffset);
        p.addend = referentOffset;
      }
      subsec->relocs.push_back(p);
    }
  }
}

template <class NList>
static macho::Symbol *createDefined(const NList &sym, StringRef name,
                                    InputSection *isec, uint64_t value,
                                    uint64_t size, bool forceHidden) {
  // Symbol scope is determined by sym.n_type & (N_EXT | N_PEXT):
  // N_EXT: Global symbols. These go in the symbol table during the link,
  //        and also in the export table of the output so that the dynamic
  //        linker sees them.
  // N_EXT | N_PEXT: Linkage unit (think: dylib) scoped. These go in the
  //                 symbol table during the link so that duplicates are
  //                 either reported (for non-weak symbols) or merged
  //                 (for weak symbols), but they do not go in the export
  //                 table of the output.
  // N_PEXT: llvm-mc does not emit these, but `ld -r` (wherein ld64 emits
  //         object files) may produce them. LLD does not yet support -r.
  //         These are translation-unit scoped, identical to the `0` case.
  // 0: Translation-unit scoped. These are not in the symbol table during
  //    link, and not in the export table of the output either.
  bool isWeakDefCanBeHidden =
      (sym.n_desc & (N_WEAK_DEF | N_WEAK_REF)) == (N_WEAK_DEF | N_WEAK_REF);

  assert(!(sym.n_desc & N_ARM_THUMB_DEF) && "ARM32 arch is not supported");

  if (sym.n_type & N_EXT) {
    // -load_hidden makes us treat global symbols as linkage unit scoped.
    // Duplicates are reported but the symbol does not go in the export trie.
    bool isPrivateExtern = sym.n_type & N_PEXT || forceHidden;

    // lld's behavior for merging symbols is slightly different from ld64:
    // ld64 picks the winning symbol based on several criteria (see
    // pickBetweenRegularAtoms() in ld64's SymbolTable.cpp), while lld
    // just merges metadata and keeps the contents of the first symbol
    // with that name (see SymbolTable::addDefined). For:
    // * inline function F in a TU built with -fvisibility-inlines-hidden
    // * and inline function F in another TU built without that flag
    // ld64 will pick the one from the file built without
    // -fvisibility-inlines-hidden.
    // lld will instead pick the one listed first on the link command line and
    // give it visibility as if the function was built without
    // -fvisibility-inlines-hidden.
    // If both functions have the same contents, this will have the same
    // behavior. If not, it won't, but the input had an ODR violation in
    // that case.
    //
    // Similarly, merging a symbol
    // that's isPrivateExtern and not isWeakDefCanBeHidden with one
    // that's not isPrivateExtern but isWeakDefCanBeHidden technically
    // should produce one
    // that's not isPrivateExtern but isWeakDefCanBeHidden. That matters
    // with ld64's semantics, because it means the non-private-extern
    // definition will continue to take priority if more private extern
    // definitions are encountered. With lld's semantics there's no observable
    // difference between a symbol that's isWeakDefCanBeHidden(autohide) or one
    // that's privateExtern -- neither makes it into the dynamic symbol table,
    // unless the autohide symbol is explicitly exported.
    // But if a symbol is both privateExtern and autohide then it can't
    // be exported.
    // So we nullify the autohide flag when privateExtern is present
    // and promote the symbol to privateExtern when it is not already.
    if (isWeakDefCanBeHidden && isPrivateExtern)
      isWeakDefCanBeHidden = false;
    else if (isWeakDefCanBeHidden)
      isPrivateExtern = true;
    return symtab->addDefined(
        name, isec->getFile(), isec, value, size, sym.n_desc & N_WEAK_DEF,
        isPrivateExtern, sym.n_desc & REFERENCED_DYNAMICALLY,
        sym.n_desc & N_NO_DEAD_STRIP, isWeakDefCanBeHidden);
  }
  bool includeInSymtab = !isPrivateLabel(name) && !isEhFrameSection(isec);
  return make<Defined>(
      name, isec->getFile(), isec, value, size, sym.n_desc & N_WEAK_DEF,
      /*isExternal=*/false, /*isPrivateExtern=*/false, includeInSymtab,
      sym.n_desc & REFERENCED_DYNAMICALLY, sym.n_desc & N_NO_DEAD_STRIP);
}

// Absolute symbols are defined symbols that do not have an associated
// InputSection. They cannot be weak.
template <class NList>
static macho::Symbol *createAbsolute(const NList &sym, InputFile *file,
                                     StringRef name, bool forceHidden) {
  assert(!(sym.n_desc & N_ARM_THUMB_DEF) && "ARM32 arch is not supported");

  if (sym.n_type & N_EXT) {
    bool isPrivateExtern = sym.n_type & N_PEXT || forceHidden;
    return symtab->addDefined(name, file, nullptr, sym.n_value, /*size=*/0,
                              /*isWeakDef=*/false, isPrivateExtern,
                              /*isReferencedDynamically=*/false,
                              sym.n_desc & N_NO_DEAD_STRIP,
                              /*isWeakDefCanBeHidden=*/false);
  }
  return make<Defined>(name, file, nullptr, sym.n_value, /*size=*/0,
                       /*isWeakDef=*/false,
                       /*isExternal=*/false, /*isPrivateExtern=*/false,
                       /*includeInSymtab=*/true,
                       /*isReferencedDynamically=*/false,
                       sym.n_desc & N_NO_DEAD_STRIP);
}

template <class NList>
macho::Symbol *ObjFile::parseNonSectionSymbol(const NList &sym,
                                              const char *strtab) {
  StringRef name = StringRef(strtab + sym.n_strx);
  uint8_t type = sym.n_type & N_TYPE;
  bool isPrivateExtern = sym.n_type & N_PEXT || forceHidden;
  switch (type) {
  case N_UNDF:
    return sym.n_value == 0
               ? symtab->addUndefined(name, this, sym.n_desc & N_WEAK_REF)
               : symtab->addCommon(name, this, sym.n_value,
                                   1 << GET_COMM_ALIGN(sym.n_desc),
                                   isPrivateExtern);
  case N_ABS:
    return createAbsolute(sym, this, name, forceHidden);
  case N_INDR: {
    // Not much point in making local aliases -- relocs in the current file can
    // just refer to the actual symbol itself. ld64 ignores these symbols too.
    if (!(sym.n_type & N_EXT))
      return nullptr;
    StringRef aliasedName = StringRef(strtab + sym.n_value);
    // isPrivateExtern is the only symbol flag that has an impact on the final
    // aliased symbol.
    auto *alias = make<AliasSymbol>(this, name, aliasedName, isPrivateExtern);
    aliases.push_back(alias);
    return alias;
  }
  case N_PBUD:
    error("TODO: support symbols of type N_PBUD");
    return nullptr;
  case N_SECT:
    llvm_unreachable(
        "N_SECT symbols should not be passed to parseNonSectionSymbol");
  default:
    llvm_unreachable("invalid symbol type");
  }
}

template <class NList> static bool isUndef(const NList &sym) {
  return (sym.n_type & N_TYPE) == N_UNDF && sym.n_value == 0;
}

template <class LP>
void ObjFile::parseSymbols(ArrayRef<typename LP::section> sectionHeaders,
                           ArrayRef<typename LP::nlist> nList,
                           const char *strtab, bool subsectionsViaSymbols) {
  using NList = typename LP::nlist;

  // Groups indices of the symbols by the sections that contain them.
  std::vector<std::vector<uint32_t>> symbolsBySection(sections.size());
  symbols.resize(nList.size());
  SmallVector<unsigned, 32> undefineds;
  for (uint32_t i = 0; i < nList.size(); ++i) {
    const NList &sym = nList[i];

    // Ignore debug symbols for now.
    // FIXME: may need special handling.
    if (sym.n_type & N_STAB)
      continue;

    if ((sym.n_type & N_TYPE) == N_SECT) {
      Subsections &subsections = sections[sym.n_sect - 1]->subsections;
      // parseSections() may have chosen not to parse this section.
      if (subsections.empty())
        continue;
      symbolsBySection[sym.n_sect - 1].push_back(i);
    } else if (isUndef(sym)) {
      undefineds.push_back(i);
    } else {
      symbols[i] = parseNonSectionSymbol(sym, strtab);
    }
  }

  for (size_t i = 0; i < sections.size(); ++i) {
    Subsections &subsections = sections[i]->subsections;
    if (subsections.empty())
      continue;
    std::vector<uint32_t> &symbolIndices = symbolsBySection[i];
    uint64_t sectionAddr = sectionHeaders[i].addr;
    uint32_t sectionAlign = 1u << sectionHeaders[i].align;

    // Some sections have already been split into subsections during
    // parseSections(), so we simply need to match Symbols to the corresponding
    // subsection here.
    if (sections[i]->doneSplitting) {
      for (size_t j = 0; j < symbolIndices.size(); ++j) {
        const uint32_t symIndex = symbolIndices[j];
        const NList &sym = nList[symIndex];
        StringRef name = strtab + sym.n_strx;
        uint64_t symbolOffset = sym.n_value - sectionAddr;
        InputSection *isec =
            findContainingSubsection(*sections[i], &symbolOffset);
        if (symbolOffset != 0) {
          error(toString(*sections[i]) + ":  symbol " + name +
                " at misaligned offset");
          continue;
        }
        symbols[symIndex] =
            createDefined(sym, name, isec, 0, isec->getSize(), forceHidden);
      }
      continue;
    }
    sections[i]->doneSplitting = true;

    auto getSymName = [strtab](const NList& sym) -> StringRef {
      return StringRef(strtab + sym.n_strx);
    };

    // Calculate symbol sizes and create subsections by splitting the sections
    // along symbol boundaries.
    // We populate subsections by repeatedly splitting the last (highest
    // address) subsection.
    llvm::stable_sort(symbolIndices, [&](uint32_t lhs, uint32_t rhs) {
      // Put extern weak symbols after other symbols at the same address so
      // that weak symbol coalescing works correctly. See
      // SymbolTable::addDefined() for details.
      if (nList[lhs].n_value == nList[rhs].n_value &&
          nList[lhs].n_type & N_EXT && nList[rhs].n_type & N_EXT)
        return !(nList[lhs].n_desc & N_WEAK_DEF) && (nList[rhs].n_desc & N_WEAK_DEF);
      return nList[lhs].n_value < nList[rhs].n_value;
    });
    for (size_t j = 0; j < symbolIndices.size(); ++j) {
      const uint32_t symIndex = symbolIndices[j];
      const NList &sym = nList[symIndex];
      StringRef name = getSymName(sym);
      Subsection &subsec = subsections.back();
      InputSection *isec = subsec.isec;

      uint64_t subsecAddr = sectionAddr + subsec.offset;
      size_t symbolOffset = sym.n_value - subsecAddr;
      uint64_t symbolSize =
          j + 1 < symbolIndices.size()
              ? nList[symbolIndices[j + 1]].n_value - sym.n_value
              : isec->data.size() - symbolOffset;
      // There are 4 cases where we do not need to create a new subsection:
      //   1. If the input file does not use subsections-via-symbols.
      //   2. Multiple symbols at the same address only induce one subsection.
      //      (The symbolOffset == 0 check covers both this case as well as
      //      the first loop iteration.)
      //   3. Alternative entry points do not induce new subsections.
      //   4. If we have a literal section (e.g. __cstring and __literal4).
      if (!subsectionsViaSymbols || symbolOffset == 0 ||
          sym.n_desc & N_ALT_ENTRY || !isa<ConcatInputSection>(isec)) {
        isec->hasAltEntry = symbolOffset != 0;
        symbols[symIndex] = createDefined(sym, name, isec, symbolOffset,
                                          symbolSize, forceHidden);
        continue;
      }
      auto *concatIsec = cast<ConcatInputSection>(isec);

      auto *nextIsec = make<ConcatInputSection>(*concatIsec);
      nextIsec->wasCoalesced = false;
      if (isZeroFill(isec->getFlags())) {
        // Zero-fill sections have NULL data.data() non-zero data.size()
        nextIsec->data = {nullptr, isec->data.size() - symbolOffset};
        isec->data = {nullptr, symbolOffset};
      } else {
        nextIsec->data = isec->data.slice(symbolOffset);
        isec->data = isec->data.slice(0, symbolOffset);
      }

      // By construction, the symbol will be at offset zero in the new
      // subsection.
      symbols[symIndex] = createDefined(sym, name, nextIsec, /*value=*/0,
                                        symbolSize, forceHidden);
      // TODO: ld64 appears to preserve the original alignment as well as each
      // subsection's offset from the last aligned address. We should consider
      // emulating that behavior.
      nextIsec->align = MinAlign(sectionAlign, sym.n_value);
      subsections.push_back({sym.n_value - sectionAddr, nextIsec});
    }
  }

  // Undefined symbols can trigger recursive fetch from Archives due to
  // LazySymbols. Process defined symbols first so that the relative order
  // between a defined symbol and an undefined symbol does not change the
  // symbol resolution behavior. In addition, a set of interconnected symbols
  // will all be resolved to the same file, instead of being resolved to
  // different files.
  for (unsigned i : undefineds)
    symbols[i] = parseNonSectionSymbol(nList[i], strtab);
}

OpaqueFile::OpaqueFile(MemoryBufferRef mb, StringRef segName,
                       StringRef sectName)
    : InputFile(OpaqueKind, mb) {
  const auto *buf = reinterpret_cast<const uint8_t *>(mb.getBufferStart());
  ArrayRef<uint8_t> data = {buf, mb.getBufferSize()};
  sections.push_back(make<Section>(/*file=*/this, segName.take_front(16),
                                   sectName.take_front(16),
                                   /*flags=*/0, /*addr=*/0));
  Section &section = *sections.back();
  ConcatInputSection *isec = make<ConcatInputSection>(section, data);
  isec->live = true;
  section.subsections.push_back({0, isec});
}

template <class LP>
void ObjFile::parseLinkerOptions(SmallVectorImpl<StringRef> &LCLinkerOptions) {
  using Header = typename LP::mach_header;
  auto *hdr = reinterpret_cast<const Header *>(mb.getBufferStart());

  for (auto *cmd : findCommands<linker_option_command>(hdr, LC_LINKER_OPTION)) {
    StringRef data{reinterpret_cast<const char *>(cmd + 1),
                   cmd->cmdsize - sizeof(linker_option_command)};
    parseLCLinkerOption(LCLinkerOptions, this, cmd->count, data);
  }
}

SmallVector<StringRef> macho::unprocessedLCLinkerOptions;
ObjFile::ObjFile(MemoryBufferRef mb, uint32_t modTime, StringRef archiveName,
                 bool lazy, bool forceHidden, bool compatArch,
                 bool builtFromBitcode)
    : InputFile(ObjKind, mb, lazy), modTime(modTime), forceHidden(forceHidden),
      builtFromBitcode(builtFromBitcode) {
  this->archiveName = std::string(archiveName);
  this->compatArch = compatArch;
  if (lazy) {
    if (target->wordSize == 8)
      parseLazy<LP64>();
    else
      parseLazy<ILP32>();
  } else {
    if (target->wordSize == 8)
      parse<LP64>();
    else
      parse<ILP32>();
  }
}

template <class LP> void ObjFile::parse() {
  using Header = typename LP::mach_header;
  using SegmentCommand = typename LP::segment_command;
  using SectionHeader = typename LP::section;
  using NList = typename LP::nlist;

  auto *buf = reinterpret_cast<const uint8_t *>(mb.getBufferStart());
  auto *hdr = reinterpret_cast<const Header *>(mb.getBufferStart());

  // If we've already checked the arch, then don't need to check again.
  if (!compatArch)
    return;
  if (!(compatArch = compatWithTargetArch(this, hdr)))
    return;

  // We will resolve LC linker options once all native objects are loaded after
  // LTO is finished.
  SmallVector<StringRef, 4> LCLinkerOptions;
  parseLinkerOptions<LP>(LCLinkerOptions);
  unprocessedLCLinkerOptions.append(LCLinkerOptions);

  ArrayRef<SectionHeader> sectionHeaders;
  if (const load_command *cmd = findCommand(hdr, LP::segmentLCType)) {
    auto *c = reinterpret_cast<const SegmentCommand *>(cmd);
    sectionHeaders = ArrayRef<SectionHeader>{
        reinterpret_cast<const SectionHeader *>(c + 1), c->nsects};
    parseSections(sectionHeaders);
  }

  // TODO: Error on missing LC_SYMTAB?
  if (const load_command *cmd = findCommand(hdr, LC_SYMTAB)) {
    auto *c = reinterpret_cast<const symtab_command *>(cmd);
    ArrayRef<NList> nList(reinterpret_cast<const NList *>(buf + c->symoff),
                          c->nsyms);
    const char *strtab = reinterpret_cast<const char *>(buf) + c->stroff;
    bool subsectionsViaSymbols = hdr->flags & MH_SUBSECTIONS_VIA_SYMBOLS;
    parseSymbols<LP>(sectionHeaders, nList, strtab, subsectionsViaSymbols);
  }

  // The relocations may refer to the symbols, so we parse them after we have
  // parsed all the symbols.
  for (size_t i = 0, n = sections.size(); i < n; ++i)
    if (!sections[i]->subsections.empty())
      parseRelocations(sectionHeaders, sectionHeaders[i], *sections[i]);

  parseDebugInfo();

  Section *ehFrameSection = nullptr;
  Section *compactUnwindSection = nullptr;
  for (Section *sec : sections) {
    Section **s = StringSwitch<Section **>(sec->name)
                      .Case(section_names::compactUnwind, &compactUnwindSection)
                      .Case(section_names::ehFrame, &ehFrameSection)
                      .Default(nullptr);
    if (s)
      *s = sec;
  }
  if (compactUnwindSection)
    registerCompactUnwind(*compactUnwindSection);
  if (ehFrameSection)
    registerEhFrames(*ehFrameSection);
}

template <class LP> void ObjFile::parseLazy() {
  using Header = typename LP::mach_header;
  using NList = typename LP::nlist;

  auto *buf = reinterpret_cast<const uint8_t *>(mb.getBufferStart());
  auto *hdr = reinterpret_cast<const Header *>(mb.getBufferStart());

  if (!compatArch)
    return;
  if (!(compatArch = compatWithTargetArch(this, hdr)))
    return;

  const load_command *cmd = findCommand(hdr, LC_SYMTAB);
  if (!cmd)
    return;
  auto *c = reinterpret_cast<const symtab_command *>(cmd);
  ArrayRef<NList> nList(reinterpret_cast<const NList *>(buf + c->symoff),
                        c->nsyms);
  const char *strtab = reinterpret_cast<const char *>(buf) + c->stroff;
  symbols.resize(nList.size());
  for (const auto &[i, sym] : llvm::enumerate(nList)) {
    if ((sym.n_type & N_EXT) && !isUndef(sym)) {
      // TODO: Bound checking
      StringRef name = strtab + sym.n_strx;
      symbols[i] = symtab->addLazyObject(name, *this);
      if (!lazy)
        break;
    }
  }
}

void ObjFile::parseDebugInfo() {
  std::unique_ptr<DwarfObject> dObj = DwarfObject::create(this);
  if (!dObj)
    return;

  // We do not re-use the context from getDwarf() here as that function
  // constructs an expensive DWARFCache object.
  auto *ctx = make<DWARFContext>(
      std::move(dObj), "",
      [&](Error err) {
        warn(toString(this) + ": " + toString(std::move(err)));
      },
      [&](Error warning) {
        warn(toString(this) + ": " + toString(std::move(warning)));
      });

  // TODO: Since object files can contain a lot of DWARF info, we should verify
  // that we are parsing just the info we need
  const DWARFContext::compile_unit_range &units = ctx->compile_units();
  // FIXME: There can be more than one compile unit per object file. See
  // PR48637.
  auto it = units.begin();
  compileUnit = it != units.end() ? it->get() : nullptr;
}

ArrayRef<data_in_code_entry> ObjFile::getDataInCode() const {
  const auto *buf = reinterpret_cast<const uint8_t *>(mb.getBufferStart());
  const load_command *cmd = findCommand(buf, LC_DATA_IN_CODE);
  if (!cmd)
    return {};
  const auto *c = reinterpret_cast<const linkedit_data_command *>(cmd);
  return {reinterpret_cast<const data_in_code_entry *>(buf + c->dataoff),
          c->datasize / sizeof(data_in_code_entry)};
}

ArrayRef<uint8_t> ObjFile::getOptimizationHints() const {
  const auto *buf = reinterpret_cast<const uint8_t *>(mb.getBufferStart());
  if (auto *cmd =
          findCommand<linkedit_data_command>(buf, LC_LINKER_OPTIMIZATION_HINT))
    return {buf + cmd->dataoff, cmd->datasize};
  return {};
}

// Create pointers from symbols to their associated compact unwind entries.
void ObjFile::registerCompactUnwind(Section &compactUnwindSection) {
  for (const Subsection &subsection : compactUnwindSection.subsections) {
    ConcatInputSection *isec = cast<ConcatInputSection>(subsection.isec);
    // Hack!! Each compact unwind entry (CUE) has its UNSIGNED relocations embed
    // their addends in its data. Thus if ICF operated naively and compared the
    // entire contents of each CUE, entries with identical unwind info but e.g.
    // belonging to different functions would never be considered equivalent. To
    // work around this problem, we remove some parts of the data containing the
    // embedded addends. In particular, we remove the function address and LSDA
    // pointers.  Since these locations are at the start and end of the entry,
    // we can do this using a simple, efficient slice rather than performing a
    // copy.  We are not losing any information here because the embedded
    // addends have already been parsed in the corresponding Reloc structs.
    //
    // Removing these pointers would not be safe if they were pointers to
    // absolute symbols. In that case, there would be no corresponding
    // relocation. However, (AFAIK) MC cannot emit references to absolute
    // symbols for either the function address or the LSDA. However, it *can* do
    // so for the personality pointer, so we are not slicing that field away.
    //
    // Note that we do not adjust the offsets of the corresponding relocations;
    // instead, we rely on `relocateCompactUnwind()` to correctly handle these
    // truncated input sections.
    isec->data = isec->data.slice(target->wordSize, 8 + target->wordSize);
    uint32_t encoding = read32le(isec->data.data() + sizeof(uint32_t));
    // llvm-mc omits CU entries for functions that need DWARF encoding, but
    // `ld -r` doesn't. We can ignore them because we will re-synthesize these
    // CU entries from the DWARF info during the output phase.
    if ((encoding & static_cast<uint32_t>(UNWIND_MODE_MASK)) ==
        target->modeDwarfEncoding)
      continue;

    ConcatInputSection *referentIsec;
    for (auto it = isec->relocs.begin(); it != isec->relocs.end();) {
      Reloc &r = *it;
      // CUE::functionAddress is at offset 0. Skip personality & LSDA relocs.
      if (r.offset != 0) {
        ++it;
        continue;
      }
      uint64_t add = r.addend;
      if (auto *sym = cast_or_null<Defined>(r.referent.dyn_cast<Symbol *>())) {
        // Check whether the symbol defined in this file is the prevailing one.
        // Skip if it is e.g. a weak def that didn't prevail.
        if (sym->getFile() != this) {
          ++it;
          continue;
        }
        add += sym->value;
        referentIsec = cast<ConcatInputSection>(sym->isec());
      } else {
        referentIsec =
            cast<ConcatInputSection>(r.referent.dyn_cast<InputSection *>());
      }
      // Unwind info lives in __DATA, and finalization of __TEXT will occur
      // before finalization of __DATA. Moreover, the finalization of unwind
      // info depends on the exact addresses that it references. So it is safe
      // for compact unwind to reference addresses in __TEXT, but not addresses
      // in any other segment.
      if (referentIsec->getSegName() != segment_names::text)
        error(isec->getLocation(r.offset) + " references section " +
              referentIsec->getName() + " which is not in segment __TEXT");
      // The functionAddress relocations are typically section relocations.
      // However, unwind info operates on a per-symbol basis, so we search for
      // the function symbol here.
      Defined *d = findSymbolAtOffset(referentIsec, add);
      if (!d) {
        ++it;
        continue;
      }
      d->originalUnwindEntry = isec;
      // Now that the symbol points to the unwind entry, we can remove the reloc
      // that points from the unwind entry back to the symbol.
      //
      // First, the symbol keeps the unwind entry alive (and not vice versa), so
      // this keeps dead-stripping simple.
      //
      // Moreover, it reduces the work that ICF needs to do to figure out if
      // functions with unwind info are foldable.
      //
      // However, this does make it possible for ICF to fold CUEs that point to
      // distinct functions (if the CUEs are otherwise identical).
      // UnwindInfoSection takes care of this by re-duplicating the CUEs so that
      // each one can hold a distinct functionAddress value.
      //
      // Given that clang emits relocations in reverse order of address, this
      // relocation should be at the end of the vector for most of our input
      // object files, so this erase() is typically an O(1) operation.
      it = isec->relocs.erase(it);
    }
  }
}

struct CIE {
  macho::Symbol *personalitySymbol = nullptr;
  bool fdesHaveAug = false;
  uint8_t lsdaPtrSize = 0; // 0 => no LSDA
  uint8_t funcPtrSize = 0;
};

static uint8_t pointerEncodingToSize(uint8_t enc) {
  switch (enc & 0xf) {
  case dwarf::DW_EH_PE_absptr:
    return target->wordSize;
  case dwarf::DW_EH_PE_sdata4:
    return 4;
  case dwarf::DW_EH_PE_sdata8:
    // ld64 doesn't actually support sdata8, but this seems simple enough...
    return 8;
  default:
    return 0;
  };
}

static CIE parseCIE(const InputSection *isec, const EhReader &reader,
                    size_t off) {
  // Handling the full generality of possible DWARF encodings would be a major
  // pain. We instead take advantage of our knowledge of how llvm-mc encodes
  // DWARF and handle just that.
  constexpr uint8_t expectedPersonalityEnc =
      dwarf::DW_EH_PE_pcrel | dwarf::DW_EH_PE_indirect | dwarf::DW_EH_PE_sdata4;

  CIE cie;
  uint8_t version = reader.readByte(&off);
  if (version != 1 && version != 3)
    fatal("Expected CIE version of 1 or 3, got " + Twine(version));
  StringRef aug = reader.readString(&off);
  reader.skipLeb128(&off); // skip code alignment
  reader.skipLeb128(&off); // skip data alignment
  reader.skipLeb128(&off); // skip return address register
  reader.skipLeb128(&off); // skip aug data length
  uint64_t personalityAddrOff = 0;
  for (char c : aug) {
    switch (c) {
    case 'z':
      cie.fdesHaveAug = true;
      break;
    case 'P': {
      uint8_t personalityEnc = reader.readByte(&off);
      if (personalityEnc != expectedPersonalityEnc)
        reader.failOn(off, "unexpected personality encoding 0x" +
                               Twine::utohexstr(personalityEnc));
      personalityAddrOff = off;
      off += 4;
      break;
    }
    case 'L': {
      uint8_t lsdaEnc = reader.readByte(&off);
      cie.lsdaPtrSize = pointerEncodingToSize(lsdaEnc);
      if (cie.lsdaPtrSize == 0)
        reader.failOn(off, "unexpected LSDA encoding 0x" +
                               Twine::utohexstr(lsdaEnc));
      break;
    }
    case 'R': {
      uint8_t pointerEnc = reader.readByte(&off);
      cie.funcPtrSize = pointerEncodingToSize(pointerEnc);
      if (cie.funcPtrSize == 0 || !(pointerEnc & dwarf::DW_EH_PE_pcrel))
        reader.failOn(off, "unexpected pointer encoding 0x" +
                               Twine::utohexstr(pointerEnc));
      break;
    }
    default:
      break;
    }
  }
  if (personalityAddrOff != 0) {
    const auto *personalityReloc = isec->getRelocAt(personalityAddrOff);
    if (!personalityReloc)
      reader.failOn(off, "Failed to locate relocation for personality symbol");
    cie.personalitySymbol = personalityReloc->referent.get<macho::Symbol *>();
  }
  return cie;
}

// EH frame target addresses may be encoded as pcrel offsets. However, instead
// of using an actual pcrel reloc, ld64 emits subtractor relocations instead.
// This function recovers the target address from the subtractors, essentially
// performing the inverse operation of EhRelocator.
//
// Concretely, we expect our relocations to write the value of `PC -
// target_addr` to `PC`. `PC` itself is denoted by a minuend relocation that
// points to a symbol plus an addend.
//
// It is important that the minuend relocation point to a symbol within the
// same section as the fixup value, since sections may get moved around.
//
// For example, for arm64, llvm-mc emits relocations for the target function
// address like so:
//
//   ltmp:
//     <CIE start>
//     ...
//     <CIE end>
//     ... multiple FDEs ...
//     <FDE start>
//     <target function address - (ltmp + pcrel offset)>
//     ...
//
// If any of the FDEs in `multiple FDEs` get dead-stripped, then `FDE start`
// will move to an earlier address, and `ltmp + pcrel offset` will no longer
// reflect an accurate pcrel value. To avoid this problem, we "canonicalize"
// our relocation by adding an `EH_Frame` symbol at `FDE start`, and updating
// the reloc to be `target function address - (EH_Frame + new pcrel offset)`.
//
// If `Invert` is set, then we instead expect `target_addr - PC` to be written
// to `PC`.
template <bool Invert = false>
Defined *
targetSymFromCanonicalSubtractor(const InputSection *isec,
                                 std::vector<macho::Reloc>::iterator relocIt) {
  macho::Reloc &subtrahend = *relocIt;
  macho::Reloc &minuend = *std::next(relocIt);
  assert(target->hasAttr(subtrahend.type, RelocAttrBits::SUBTRAHEND));
  assert(target->hasAttr(minuend.type, RelocAttrBits::UNSIGNED));
  // Note: pcSym may *not* be exactly at the PC; there's usually a non-zero
  // addend.
  auto *pcSym = cast<Defined>(subtrahend.referent.get<macho::Symbol *>());
  Defined *target =
      cast_or_null<Defined>(minuend.referent.dyn_cast<macho::Symbol *>());
  if (!pcSym) {
    auto *targetIsec =
        cast<ConcatInputSection>(minuend.referent.get<InputSection *>());
    target = findSymbolAtOffset(targetIsec, minuend.addend);
  }
  if (Invert)
    std::swap(pcSym, target);
  if (pcSym->isec() == isec) {
    if (pcSym->value - (Invert ? -1 : 1) * minuend.addend != subtrahend.offset)
      fatal("invalid FDE relocation in __eh_frame");
  } else {
    // Ensure the pcReloc points to a symbol within the current EH frame.
    // HACK: we should really verify that the original relocation's semantics
    // are preserved. In particular, we should have
    // `oldSym->value + oldOffset == newSym + newOffset`. However, we don't
    // have an easy way to access the offsets from this point in the code; some
    // refactoring is needed for that.
    macho::Reloc &pcReloc = Invert ? minuend : subtrahend;
    pcReloc.referent = isec->symbols[0];
    assert(isec->symbols[0]->value == 0);
    minuend.addend = pcReloc.offset * (Invert ? 1LL : -1LL);
  }
  return target;
}

Defined *findSymbolAtAddress(const std::vector<Section *> &sections,
                             uint64_t addr) {
  Section *sec = findContainingSection(sections, &addr);
  auto *isec = cast<ConcatInputSection>(findContainingSubsection(*sec, &addr));
  return findSymbolAtOffset(isec, addr);
}

// For symbols that don't have compact unwind info, associate them with the more
// general-purpose (and verbose) DWARF unwind info found in __eh_frame.
//
// This requires us to parse the contents of __eh_frame. See EhFrame.h for a
// description of its format.
//
// While parsing, we also look for what MC calls "abs-ified" relocations -- they
// are relocations which are implicitly encoded as offsets in the section data.
// We convert them into explicit Reloc structs so that the EH frames can be
// handled just like a regular ConcatInputSection later in our output phase.
//
// We also need to handle the case where our input object file has explicit
// relocations. This is the case when e.g. it's the output of `ld -r`. We only
// look for the "abs-ified" relocation if an explicit relocation is absent.
void ObjFile::registerEhFrames(Section &ehFrameSection) {
  DenseMap<const InputSection *, CIE> cieMap;
  for (const Subsection &subsec : ehFrameSection.subsections) {
    auto *isec = cast<ConcatInputSection>(subsec.isec);
    uint64_t isecOff = subsec.offset;

    // Subtractor relocs require the subtrahend to be a symbol reloc. Ensure
    // that all EH frames have an associated symbol so that we can generate
    // subtractor relocs that reference them.
    if (isec->symbols.size() == 0)
      make<Defined>("EH_Frame", isec->getFile(), isec, /*value=*/0,
                    isec->getSize(), /*isWeakDef=*/false, /*isExternal=*/false,
                    /*isPrivateExtern=*/false, /*includeInSymtab=*/false,
                    /*isReferencedDynamically=*/false,
                    /*noDeadStrip=*/false);
    else if (isec->symbols[0]->value != 0)
      fatal("found symbol at unexpected offset in __eh_frame");

    EhReader reader(this, isec->data, subsec.offset);
    size_t dataOff = 0; // Offset from the start of the EH frame.
    reader.skipValidLength(&dataOff); // readLength() already validated this.
    // cieOffOff is the offset from the start of the EH frame to the cieOff
    // value, which is itself an offset from the current PC to a CIE.
    const size_t cieOffOff = dataOff;

    EhRelocator ehRelocator(isec);
    auto cieOffRelocIt = llvm::find_if(
        isec->relocs, [=](const Reloc &r) { return r.offset == cieOffOff; });
    InputSection *cieIsec = nullptr;
    if (cieOffRelocIt != isec->relocs.end()) {
      // We already have an explicit relocation for the CIE offset.
      cieIsec =
          targetSymFromCanonicalSubtractor</*Invert=*/true>(isec, cieOffRelocIt)
              ->isec();
      dataOff += sizeof(uint32_t);
    } else {
      // If we haven't found a relocation, then the CIE offset is most likely
      // embedded in the section data (AKA an "abs-ified" reloc.). Parse that
      // and generate a Reloc struct.
      uint32_t cieMinuend = reader.readU32(&dataOff);
      if (cieMinuend == 0) {
        cieIsec = isec;
      } else {
        uint32_t cieOff = isecOff + dataOff - cieMinuend;
        cieIsec = findContainingSubsection(ehFrameSection, &cieOff);
        if (cieIsec == nullptr)
          fatal("failed to find CIE");
      }
      if (cieIsec != isec)
        ehRelocator.makeNegativePcRel(cieOffOff, cieIsec->symbols[0],
                                      /*length=*/2);
    }
    if (cieIsec == isec) {
      cieMap[cieIsec] = parseCIE(isec, reader, dataOff);
      continue;
    }

    assert(cieMap.count(cieIsec));
    const CIE &cie = cieMap[cieIsec];
    // Offset of the function address within the EH frame.
    const size_t funcAddrOff = dataOff;
    uint64_t funcAddr = reader.readPointer(&dataOff, cie.funcPtrSize) +
                        ehFrameSection.addr + isecOff + funcAddrOff;
    uint32_t funcLength = reader.readPointer(&dataOff, cie.funcPtrSize);
    size_t lsdaAddrOff = 0; // Offset of the LSDA address within the EH frame.
    std::optional<uint64_t> lsdaAddrOpt;
    if (cie.fdesHaveAug) {
      reader.skipLeb128(&dataOff);
      lsdaAddrOff = dataOff;
      if (cie.lsdaPtrSize != 0) {
        uint64_t lsdaOff = reader.readPointer(&dataOff, cie.lsdaPtrSize);
        if (lsdaOff != 0) // FIXME possible to test this?
          lsdaAddrOpt = ehFrameSection.addr + isecOff + lsdaAddrOff + lsdaOff;
      }
    }

    auto funcAddrRelocIt = isec->relocs.end();
    auto lsdaAddrRelocIt = isec->relocs.end();
    for (auto it = isec->relocs.begin(); it != isec->relocs.end(); ++it) {
      if (it->offset == funcAddrOff)
        funcAddrRelocIt = it++; // Found subtrahend; skip over minuend reloc
      else if (lsdaAddrOpt && it->offset == lsdaAddrOff)
        lsdaAddrRelocIt = it++; // Found subtrahend; skip over minuend reloc
    }

    Defined *funcSym;
    if (funcAddrRelocIt != isec->relocs.end()) {
      funcSym = targetSymFromCanonicalSubtractor(isec, funcAddrRelocIt);
      // Canonicalize the symbol. If there are multiple symbols at the same
      // address, we want both `registerEhFrame` and `registerCompactUnwind`
      // to register the unwind entry under same symbol.
      // This is not particularly efficient, but we should run into this case
      // infrequently (only when handling the output of `ld -r`).
      if (funcSym->isec())
        funcSym = findSymbolAtOffset(cast<ConcatInputSection>(funcSym->isec()),
                                     funcSym->value);
    } else {
      funcSym = findSymbolAtAddress(sections, funcAddr);
      ehRelocator.makePcRel(funcAddrOff, funcSym, target->p2WordSize);
    }
    // The symbol has been coalesced, or already has a compact unwind entry.
    if (!funcSym || funcSym->getFile() != this || funcSym->unwindEntry()) {
      // We must prune unused FDEs for correctness, so we cannot rely on
      // -dead_strip being enabled.
      isec->live = false;
      continue;
    }

    InputSection *lsdaIsec = nullptr;
    if (lsdaAddrRelocIt != isec->relocs.end()) {
      lsdaIsec =
          targetSymFromCanonicalSubtractor(isec, lsdaAddrRelocIt)->isec();
    } else if (lsdaAddrOpt) {
      uint64_t lsdaAddr = *lsdaAddrOpt;
      Section *sec = findContainingSection(sections, &lsdaAddr);
      lsdaIsec =
          cast<ConcatInputSection>(findContainingSubsection(*sec, &lsdaAddr));
      ehRelocator.makePcRel(lsdaAddrOff, lsdaIsec, target->p2WordSize);
    }

    fdes[isec] = {funcLength, cie.personalitySymbol, lsdaIsec};
    funcSym->originalUnwindEntry = isec;
    ehRelocator.commit();
  }

  // __eh_frame is marked as S_ATTR_LIVE_SUPPORT in input files, because FDEs
  // are normally required to be kept alive if they reference a live symbol.
  // However, we've explicitly created a dependency from a symbol to its FDE, so
  // dead-stripping will just work as usual, and S_ATTR_LIVE_SUPPORT will only
  // serve to incorrectly prevent us from dead-stripping duplicate FDEs for a
  // live symbol (e.g. if there were multiple weak copies). Remove this flag to
  // let dead-stripping proceed correctly.
  ehFrameSection.flags &= ~S_ATTR_LIVE_SUPPORT;
}

std::string ObjFile::sourceFile() const {
  const char *unitName = compileUnit->getUnitDIE().getShortName();
  // DWARF allows DW_AT_name to be absolute, in which case nothing should be
  // prepended. As for the styles, debug info can contain paths from any OS, not
  // necessarily an OS we're currently running on. Moreover different
  // compilation units can be compiled on different operating systems and linked
  // together later.
  if (sys::path::is_absolute(unitName, llvm::sys::path::Style::posix) ||
      sys::path::is_absolute(unitName, llvm::sys::path::Style::windows))
    return unitName;
  SmallString<261> dir(compileUnit->getCompilationDir());
  StringRef sep = sys::path::get_separator();
  // We don't use `path::append` here because we want an empty `dir` to result
  // in an absolute path. `append` would give us a relative path for that case.
  if (!dir.ends_with(sep))
    dir += sep;
  return (dir + unitName).str();
}

lld::DWARFCache *ObjFile::getDwarf() {
  llvm::call_once(initDwarf, [this]() {
    auto dwObj = DwarfObject::create(this);
    if (!dwObj)
      return;
    dwarfCache = std::make_unique<DWARFCache>(std::make_unique<DWARFContext>(
        std::move(dwObj), "",
        [&](Error err) { warn(getName() + ": " + toString(std::move(err))); },
        [&](Error warning) {
          warn(getName() + ": " + toString(std::move(warning)));
        }));
  });

  return dwarfCache.get();
}
// The path can point to either a dylib or a .tbd file.
static DylibFile *loadDylib(StringRef path, DylibFile *umbrella) {
  std::optional<MemoryBufferRef> mbref = readFile(path);
  if (!mbref) {
    error("could not read dylib file at " + path);
    return nullptr;
  }
  return loadDylib(*mbref, umbrella);
}

// TBD files are parsed into a series of TAPI documents (InterfaceFiles), with
// the first document storing child pointers to the rest of them. When we are
// processing a given TBD file, we store that top-level document in
// currentTopLevelTapi. When processing re-exports, we search its children for
// potentially matching documents in the same TBD file. Note that the children
// themselves don't point to further documents, i.e. this is a two-level tree.
//
// Re-exports can either refer to on-disk files, or to documents within .tbd
// files.
static DylibFile *findDylib(StringRef path, DylibFile *umbrella,
                            const InterfaceFile *currentTopLevelTapi) {
  // Search order:
  // 1. Install name basename in -F / -L directories.
  {
    StringRef stem = path::stem(path);
    SmallString<128> frameworkName;
    path::append(frameworkName, path::Style::posix, stem + ".framework", stem);
    bool isFramework = path.ends_with(frameworkName);
    if (isFramework) {
      for (StringRef dir : config->frameworkSearchPaths) {
        SmallString<128> candidate = dir;
        path::append(candidate, frameworkName);
        if (std::optional<StringRef> dylibPath =
                resolveDylibPath(candidate.str()))
          return loadDylib(*dylibPath, umbrella);
      }
    } else if (std::optional<StringRef> dylibPath = findPathCombination(
                   stem, config->librarySearchPaths, {".tbd", ".dylib", ".so"}))
      return loadDylib(*dylibPath, umbrella);
  }

  // 2. As absolute path.
  if (path::is_absolute(path, path::Style::posix))
    for (StringRef root : config->systemLibraryRoots)
      if (std::optional<StringRef> dylibPath =
              resolveDylibPath((root + path).str()))
        return loadDylib(*dylibPath, umbrella);

  // 3. As relative path.

  // TODO: Handle -dylib_file

  // Replace @executable_path, @loader_path, @rpath prefixes in install name.
  SmallString<128> newPath;
  if (config->outputType == MH_EXECUTE &&
      path.consume_front("@executable_path/")) {
    // ld64 allows overriding this with the undocumented flag -executable_path.
    // lld doesn't currently implement that flag.
    // FIXME: Consider using finalOutput instead of outputFile.
    path::append(newPath, path::parent_path(config->outputFile), path);
    path = newPath;
  } else if (path.consume_front("@loader_path/")) {
    fs::real_path(umbrella->getName(), newPath);
    path::remove_filename(newPath);
    path::append(newPath, path);
    path = newPath;
  } else if (path.starts_with("@rpath/")) {
    for (StringRef rpath : umbrella->rpaths) {
      newPath.clear();
      if (rpath.consume_front("@loader_path/")) {
        fs::real_path(umbrella->getName(), newPath);
        path::remove_filename(newPath);
      }
      path::append(newPath, rpath, path.drop_front(strlen("@rpath/")));
      if (std::optional<StringRef> dylibPath = resolveDylibPath(newPath.str()))
        return loadDylib(*dylibPath, umbrella);
    }
  }

  // FIXME: Should this be further up?
  if (currentTopLevelTapi) {
    for (InterfaceFile &child :
         make_pointee_range(currentTopLevelTapi->documents())) {
      assert(child.documents().empty());
      if (path == child.getInstallName()) {
        auto *file = make<DylibFile>(child, umbrella, /*isBundleLoader=*/false,
                                     /*explicitlyLinked=*/false);
        file->parseReexports(child);
        return file;
      }
    }
  }

  if (std::optional<StringRef> dylibPath = resolveDylibPath(path))
    return loadDylib(*dylibPath, umbrella);

  return nullptr;
}

// If a re-exported dylib is public (lives in /usr/lib or
// /System/Library/Frameworks), then it is considered implicitly linked: we
// should bind to its symbols directly instead of via the re-exporting umbrella
// library.
static bool isImplicitlyLinked(StringRef path) {
  if (!config->implicitDylibs)
    return false;

  if (path::parent_path(path) == "/usr/lib")
    return true;

  // Match /System/Library/Frameworks/$FOO.framework/**/$FOO
  if (path.consume_front("/System/Library/Frameworks/")) {
    StringRef frameworkName = path.take_until([](char c) { return c == '.'; });
    return path::filename(path) == frameworkName;
  }

  return false;
}

void DylibFile::loadReexport(StringRef path, DylibFile *umbrella,
                         const InterfaceFile *currentTopLevelTapi) {
  DylibFile *reexport = findDylib(path, umbrella, currentTopLevelTapi);
  if (!reexport)
    error(toString(this) + ": unable to locate re-export with install name " +
          path);
}

DylibFile::DylibFile(MemoryBufferRef mb, DylibFile *umbrella,
                     bool isBundleLoader, bool explicitlyLinked)
    : InputFile(DylibKind, mb), refState(RefState::Unreferenced),
      explicitlyLinked(explicitlyLinked), isBundleLoader(isBundleLoader) {
  assert(!isBundleLoader || !umbrella);
  if (umbrella == nullptr)
    umbrella = this;
  this->umbrella = umbrella;

  auto *hdr = reinterpret_cast<const mach_header *>(mb.getBufferStart());

  // Initialize installName.
  if (const load_command *cmd = findCommand(hdr, LC_ID_DYLIB)) {
    auto *c = reinterpret_cast<const dylib_command *>(cmd);
    currentVersion = read32le(&c->dylib.current_version);
    compatibilityVersion = read32le(&c->dylib.compatibility_version);
    installName =
        reinterpret_cast<const char *>(cmd) + read32le(&c->dylib.name);
  } else if (!isBundleLoader) {
    // macho_executable and macho_bundle don't have LC_ID_DYLIB,
    // so it's OK.
    error(toString(this) + ": dylib missing LC_ID_DYLIB load command");
    return;
  }

  if (config->printEachFile)
    message(toString(this));
  inputFiles.insert(this);

  deadStrippable = hdr->flags & MH_DEAD_STRIPPABLE_DYLIB;

  if (!checkCompatibility(this))
    return;

  checkAppExtensionSafety(hdr->flags & MH_APP_EXTENSION_SAFE);

  for (auto *cmd : findCommands<rpath_command>(hdr, LC_RPATH)) {
    StringRef rpath{reinterpret_cast<const char *>(cmd) + cmd->path};
    rpaths.push_back(rpath);
  }

  // Initialize symbols.
  bool canBeImplicitlyLinked = findCommand(hdr, LC_SUB_CLIENT) == nullptr;
  exportingFile = (canBeImplicitlyLinked && isImplicitlyLinked(installName))
                      ? this
                      : this->umbrella;

  const auto *dyldInfo = findCommand<dyld_info_command>(hdr, LC_DYLD_INFO_ONLY);
  const auto *exportsTrie =
      findCommand<linkedit_data_command>(hdr, LC_DYLD_EXPORTS_TRIE);
  if (dyldInfo && exportsTrie) {
    // It's unclear what should happen in this case. Maybe we should only error
    // out if the two load commands refer to different data?
    error(toString(this) +
          ": dylib has both LC_DYLD_INFO_ONLY and LC_DYLD_EXPORTS_TRIE");
    return;
  }

  if (dyldInfo) {
    parseExportedSymbols(dyldInfo->export_off, dyldInfo->export_size);
  } else if (exportsTrie) {
    parseExportedSymbols(exportsTrie->dataoff, exportsTrie->datasize);
  } else {
    error("No LC_DYLD_INFO_ONLY or LC_DYLD_EXPORTS_TRIE found in " +
          toString(this));
  }
}

void DylibFile::parseExportedSymbols(uint32_t offset, uint32_t size) {
  struct TrieEntry {
    StringRef name;
    uint64_t flags;
  };

  auto *buf = reinterpret_cast<const uint8_t *>(mb.getBufferStart());
  std::vector<TrieEntry> entries;
  // Find all the $ld$* symbols to process first.
  parseTrie(buf + offset, size, [&](const Twine &name, uint64_t flags) {
    StringRef savedName = saver().save(name);
    if (handleLDSymbol(savedName))
      return;
    entries.push_back({savedName, flags});
  });

  // Process the "normal" symbols.
  for (TrieEntry &entry : entries) {
    if (exportingFile->hiddenSymbols.contains(CachedHashStringRef(entry.name)))
      continue;

    bool isWeakDef = entry.flags & EXPORT_SYMBOL_FLAGS_WEAK_DEFINITION;
    bool isTlv = entry.flags & EXPORT_SYMBOL_FLAGS_KIND_THREAD_LOCAL;

    symbols.push_back(
        symtab->addDylib(entry.name, exportingFile, isWeakDef, isTlv));
  }
}

void DylibFile::parseLoadCommands(MemoryBufferRef mb) {
  auto *hdr = reinterpret_cast<const mach_header *>(mb.getBufferStart());
  const uint8_t *p = reinterpret_cast<const uint8_t *>(mb.getBufferStart()) +
                     target->headerSize;
  for (uint32_t i = 0, n = hdr->ncmds; i < n; ++i) {
    auto *cmd = reinterpret_cast<const load_command *>(p);
    p += cmd->cmdsize;

    if (!(hdr->flags & MH_NO_REEXPORTED_DYLIBS) &&
        cmd->cmd == LC_REEXPORT_DYLIB) {
      const auto *c = reinterpret_cast<const dylib_command *>(cmd);
      StringRef reexportPath =
          reinterpret_cast<const char *>(c) + read32le(&c->dylib.name);
      loadReexport(reexportPath, exportingFile, nullptr);
    }

    // FIXME: What about LC_LOAD_UPWARD_DYLIB, LC_LAZY_LOAD_DYLIB,
    // LC_LOAD_WEAK_DYLIB, LC_REEXPORT_DYLIB (..are reexports from dylibs with
    // MH_NO_REEXPORTED_DYLIBS loaded for -flat_namespace)?
    if (config->namespaceKind == NamespaceKind::flat &&
        cmd->cmd == LC_LOAD_DYLIB) {
      const auto *c = reinterpret_cast<const dylib_command *>(cmd);
      StringRef dylibPath =
          reinterpret_cast<const char *>(c) + read32le(&c->dylib.name);
      DylibFile *dylib = findDylib(dylibPath, umbrella, nullptr);
      if (!dylib)
        error(Twine("unable to locate library '") + dylibPath +
              "' loaded from '" + toString(this) + "' for -flat_namespace");
    }
  }
}

// Some versions of Xcode ship with .tbd files that don't have the right
// platform settings.
constexpr std::array<StringRef, 3> skipPlatformChecks{
    "/usr/lib/system/libsystem_kernel.dylib",
    "/usr/lib/system/libsystem_platform.dylib",
    "/usr/lib/system/libsystem_pthread.dylib"};

static bool skipPlatformCheckForCatalyst(const InterfaceFile &interface,
                                         bool explicitlyLinked) {
  // Catalyst outputs can link against implicitly linked macOS-only libraries.
  if (config->platform() != PLATFORM_MACCATALYST || explicitlyLinked)
    return false;
  return is_contained(interface.targets(),
                      MachO::Target(config->arch(), PLATFORM_MACOS));
}

static bool isArchABICompatible(ArchitectureSet archSet,
                                Architecture targetArch) {
  uint32_t cpuType;
  uint32_t targetCpuType;
  std::tie(targetCpuType, std::ignore) = getCPUTypeFromArchitecture(targetArch);

  return llvm::any_of(archSet, [&](const auto &p) {
    std::tie(cpuType, std::ignore) = getCPUTypeFromArchitecture(p);
    return cpuType == targetCpuType;
  });
}

static bool isTargetPlatformArchCompatible(
    InterfaceFile::const_target_range interfaceTargets, Target target) {
  if (is_contained(interfaceTargets, target))
    return true;

  if (config->forceExactCpuSubtypeMatch)
    return false;

  ArchitectureSet archSet;
  for (const auto &p : interfaceTargets)
    if (p.Platform == target.Platform)
      archSet.set(p.Arch);
  if (archSet.empty())
    return false;

  return isArchABICompatible(archSet, target.Arch);
}

DylibFile::DylibFile(const InterfaceFile &interface, DylibFile *umbrella,
                     bool isBundleLoader, bool explicitlyLinked)
    : InputFile(DylibKind, interface), refState(RefState::Unreferenced),
      explicitlyLinked(explicitlyLinked), isBundleLoader(isBundleLoader) {
  // FIXME: Add test for the missing TBD code path.

  if (umbrella == nullptr)
    umbrella = this;
  this->umbrella = umbrella;

  installName = saver().save(interface.getInstallName());
  compatibilityVersion = interface.getCompatibilityVersion().rawValue();
  currentVersion = interface.getCurrentVersion().rawValue();

  if (config->printEachFile)
    message(toString(this));
  inputFiles.insert(this);

  if (!is_contained(skipPlatformChecks, installName) &&
      !isTargetPlatformArchCompatible(interface.targets(),
                                      config->platformInfo.target) &&
      !skipPlatformCheckForCatalyst(interface, explicitlyLinked)) {
    error(toString(this) + " is incompatible with " +
          std::string(config->platformInfo.target));
    return;
  }

  checkAppExtensionSafety(interface.isApplicationExtensionSafe());

  bool canBeImplicitlyLinked = interface.allowableClients().size() == 0;
  exportingFile = (canBeImplicitlyLinked && isImplicitlyLinked(installName))
                      ? this
                      : umbrella;
  auto addSymbol = [&](const llvm::MachO::Symbol &symbol,
                       const Twine &name) -> void {
    StringRef savedName = saver().save(name);
    if (exportingFile->hiddenSymbols.contains(CachedHashStringRef(savedName)))
      return;

    symbols.push_back(symtab->addDylib(savedName, exportingFile,
                                       symbol.isWeakDefined(),
                                       symbol.isThreadLocalValue()));
  };

  std::vector<const llvm::MachO::Symbol *> normalSymbols;
  normalSymbols.reserve(interface.symbolsCount());
  for (const auto *symbol : interface.symbols()) {
    if (!isArchABICompatible(symbol->getArchitectures(), config->arch()))
      continue;
    if (handleLDSymbol(symbol->getName()))
      continue;

    switch (symbol->getKind()) {
    case EncodeKind::GlobalSymbol:
    case EncodeKind::ObjectiveCClass:
    case EncodeKind::ObjectiveCClassEHType:
    case EncodeKind::ObjectiveCInstanceVariable:
      normalSymbols.push_back(symbol);
    }
  }
  // interface.symbols() order is non-deterministic.
  llvm::sort(normalSymbols,
             [](auto *l, auto *r) { return l->getName() < r->getName(); });

  // TODO(compnerd) filter out symbols based on the target platform
  for (const auto *symbol : normalSymbols) {
    switch (symbol->getKind()) {
    case EncodeKind::GlobalSymbol:
      addSymbol(*symbol, symbol->getName());
      break;
    case EncodeKind::ObjectiveCClass:
      // XXX ld64 only creates these symbols when -ObjC is passed in. We may
      // want to emulate that.
      addSymbol(*symbol, objc::symbol_names::klass + symbol->getName());
      addSymbol(*symbol, objc::symbol_names::metaclass + symbol->getName());
      break;
    case EncodeKind::ObjectiveCClassEHType:
      addSymbol(*symbol, objc::symbol_names::ehtype + symbol->getName());
      break;
    case EncodeKind::ObjectiveCInstanceVariable:
      addSymbol(*symbol, objc::symbol_names::ivar + symbol->getName());
      break;
    }
  }
}

DylibFile::DylibFile(DylibFile *umbrella)
    : InputFile(DylibKind, MemoryBufferRef{}), refState(RefState::Unreferenced),
      explicitlyLinked(false), isBundleLoader(false) {
  if (umbrella == nullptr)
    umbrella = this;
  this->umbrella = umbrella;
}

void DylibFile::parseReexports(const InterfaceFile &interface) {
  const InterfaceFile *topLevel =
      interface.getParent() == nullptr ? &interface : interface.getParent();
  for (const InterfaceFileRef &intfRef : interface.reexportedLibraries()) {
    InterfaceFile::const_target_range targets = intfRef.targets();
    if (is_contained(skipPlatformChecks, intfRef.getInstallName()) ||
        isTargetPlatformArchCompatible(targets, config->platformInfo.target))
      loadReexport(intfRef.getInstallName(), exportingFile, topLevel);
  }
}

bool DylibFile::isExplicitlyLinked() const {
  if (!explicitlyLinked)
    return false;

  // If this dylib was explicitly linked, but at least one of the symbols
  // of the synthetic dylibs it created via $ld$previous symbols is
  // referenced, then that synthetic dylib fulfils the explicit linkedness
  // and we can deadstrip this dylib if it's unreferenced.
  for (const auto *dylib : extraDylibs)
    if (dylib->isReferenced())
      return false;

  return true;
}

DylibFile *DylibFile::getSyntheticDylib(StringRef installName,
                                        uint32_t currentVersion,
                                        uint32_t compatVersion) {
  for (DylibFile *dylib : extraDylibs)
    if (dylib->installName == installName) {
      // FIXME: Check what to do if different $ld$previous symbols
      // request the same dylib, but with different versions.
      return dylib;
    }

  auto *dylib = make<DylibFile>(umbrella == this ? nullptr : umbrella);
  dylib->installName = saver().save(installName);
  dylib->currentVersion = currentVersion;
  dylib->compatibilityVersion = compatVersion;
  extraDylibs.push_back(dylib);
  return dylib;
}

// $ld$ symbols modify the properties/behavior of the library (e.g. its install
// name, compatibility version or hide/add symbols) for specific target
// versions.
bool DylibFile::handleLDSymbol(StringRef originalName) {
  if (!originalName.starts_with("$ld$"))
    return false;

  StringRef action;
  StringRef name;
  std::tie(action, name) = originalName.drop_front(strlen("$ld$")).split('$');
  if (action == "previous")
    handleLDPreviousSymbol(name, originalName);
  else if (action == "install_name")
    handleLDInstallNameSymbol(name, originalName);
  else if (action == "hide")
    handleLDHideSymbol(name, originalName);
  return true;
}

void DylibFile::handleLDPreviousSymbol(StringRef name, StringRef originalName) {
  // originalName: $ld$ previous $ <installname> $ <compatversion> $
  // <platformstr> $ <startversion> $ <endversion> $ <symbol-name> $
  StringRef installName;
  StringRef compatVersion;
  StringRef platformStr;
  StringRef startVersion;
  StringRef endVersion;
  StringRef symbolName;
  StringRef rest;

  std::tie(installName, name) = name.split('$');
  std::tie(compatVersion, name) = name.split('$');
  std::tie(platformStr, name) = name.split('$');
  std::tie(startVersion, name) = name.split('$');
  std::tie(endVersion, name) = name.split('$');
  std::tie(symbolName, rest) = name.rsplit('$');

  // FIXME: Does this do the right thing for zippered files?
  unsigned platform;
  if (platformStr.getAsInteger(10, platform) ||
      platform != static_cast<unsigned>(config->platform()))
    return;

  VersionTuple start;
  if (start.tryParse(startVersion)) {
    warn(toString(this) + ": failed to parse start version, symbol '" +
         originalName + "' ignored");
    return;
  }
  VersionTuple end;
  if (end.tryParse(endVersion)) {
    warn(toString(this) + ": failed to parse end version, symbol '" +
         originalName + "' ignored");
    return;
  }
  if (config->platformInfo.target.MinDeployment < start ||
      config->platformInfo.target.MinDeployment >= end)
    return;

  // Initialized to compatibilityVersion for the symbolName branch below.
  uint32_t newCompatibilityVersion = compatibilityVersion;
  uint32_t newCurrentVersionForSymbol = currentVersion;
  if (!compatVersion.empty()) {
    VersionTuple cVersion;
    if (cVersion.tryParse(compatVersion)) {
      warn(toString(this) +
           ": failed to parse compatibility version, symbol '" + originalName +
           "' ignored");
      return;
    }
    newCompatibilityVersion = encodeVersion(cVersion);
    newCurrentVersionForSymbol = newCompatibilityVersion;
  }

  if (!symbolName.empty()) {
    // A $ld$previous$ symbol with symbol name adds a symbol with that name to
    // a dylib with given name and version.
    auto *dylib = getSyntheticDylib(installName, newCurrentVersionForSymbol,
                                    newCompatibilityVersion);

    // The tbd file usually contains the $ld$previous symbol for an old version,
    // and then the symbol itself later, for newer deployment targets, like so:
    //    symbols: [
    //      '$ld$previous$/Another$$1$3.0$14.0$_zzz$',
    //      _zzz,
    //    ]
    // Since the symbols are sorted, adding them to the symtab in the given
    // order means the $ld$previous version of _zzz will prevail, as desired.
    dylib->symbols.push_back(symtab->addDylib(
        saver().save(symbolName), dylib, /*isWeakDef=*/false, /*isTlv=*/false));
    return;
  }

  // A $ld$previous$ symbol without symbol name modifies the dylib it's in.
  this->installName = saver().save(installName);
  this->compatibilityVersion = newCompatibilityVersion;
}

void DylibFile::handleLDInstallNameSymbol(StringRef name,
                                          StringRef originalName) {
  // originalName: $ld$ install_name $ os<version> $ install_name
  StringRef condition, installName;
  std::tie(condition, installName) = name.split('$');
  VersionTuple version;
  if (!condition.consume_front("os") || version.tryParse(condition))
    warn(toString(this) + ": failed to parse os version, symbol '" +
         originalName + "' ignored");
  else if (version == config->platformInfo.target.MinDeployment)
    this->installName = saver().save(installName);
}

void DylibFile::handleLDHideSymbol(StringRef name, StringRef originalName) {
  StringRef symbolName;
  bool shouldHide = true;
  if (name.starts_with("os")) {
    // If it's hidden based on versions.
    name = name.drop_front(2);
    StringRef minVersion;
    std::tie(minVersion, symbolName) = name.split('$');
    VersionTuple versionTup;
    if (versionTup.tryParse(minVersion)) {
      warn(toString(this) + ": failed to parse hidden version, symbol `" + originalName +
           "` ignored.");
      return;
    }
    shouldHide = versionTup == config->platformInfo.target.MinDeployment;
  } else {
    symbolName = name;
  }

  if (shouldHide)
    exportingFile->hiddenSymbols.insert(CachedHashStringRef(symbolName));
}

void DylibFile::checkAppExtensionSafety(bool dylibIsAppExtensionSafe) const {
  if (config->applicationExtension && !dylibIsAppExtensionSafe)
    warn("using '-application_extension' with unsafe dylib: " + toString(this));
}

ArchiveFile::ArchiveFile(std::unique_ptr<object::Archive> &&f, bool forceHidden)
    : InputFile(ArchiveKind, f->getMemoryBufferRef()), file(std::move(f)),
      forceHidden(forceHidden) {}

void ArchiveFile::addLazySymbols() {
  // Avoid calling getMemoryBufferRef() on zero-symbol archive
  // since that crashes.
  if (file->isEmpty() || file->getNumberOfSymbols() == 0)
    return;

  Error err = Error::success();
  auto child = file->child_begin(err);
  // Ignore the I/O error here - will be reported later.
  if (!err) {
    Expected<MemoryBufferRef> mbOrErr = child->getMemoryBufferRef();
    if (!mbOrErr) {
      llvm::consumeError(mbOrErr.takeError());
    } else {
      if (identify_magic(mbOrErr->getBuffer()) == file_magic::macho_object) {
        if (target->wordSize == 8)
          compatArch = compatWithTargetArch(
              this, reinterpret_cast<const LP64::mach_header *>(
                        mbOrErr->getBufferStart()));
        else
          compatArch = compatWithTargetArch(
              this, reinterpret_cast<const ILP32::mach_header *>(
                        mbOrErr->getBufferStart()));
        if (!compatArch)
          return;
      }
    }
  }

  for (const object::Archive::Symbol &sym : file->symbols())
    symtab->addLazyArchive(sym.getName(), this, sym);
}

static Expected<InputFile *>
loadArchiveMember(MemoryBufferRef mb, uint32_t modTime, StringRef archiveName,
                  uint64_t offsetInArchive, bool forceHidden, bool compatArch) {
  if (config->zeroModTime)
    modTime = 0;

  switch (identify_magic(mb.getBuffer())) {
  case file_magic::macho_object:
    return make<ObjFile>(mb, modTime, archiveName, /*lazy=*/false, forceHidden,
                         compatArch);
  case file_magic::bitcode:
    return make<BitcodeFile>(mb, archiveName, offsetInArchive, /*lazy=*/false,
                             forceHidden, compatArch);
  default:
    return createStringError(inconvertibleErrorCode(),
                             mb.getBufferIdentifier() +
                                 " has unhandled file type");
  }
}

Error ArchiveFile::fetch(const object::Archive::Child &c, StringRef reason) {
  if (!seen.insert(c.getChildOffset()).second)
    return Error::success();

  Expected<MemoryBufferRef> mb = c.getMemoryBufferRef();
  if (!mb)
    return mb.takeError();

  Expected<TimePoint<std::chrono::seconds>> modTime = c.getLastModified();
  if (!modTime)
    return modTime.takeError();

  Expected<InputFile *> file =
      loadArchiveMember(*mb, toTimeT(*modTime), getName(), c.getChildOffset(),
                        forceHidden, compatArch);

  if (!file)
    return file.takeError();

  inputFiles.insert(*file);
  printArchiveMemberLoad(reason, *file);
  return Error::success();
}

void ArchiveFile::fetch(const object::Archive::Symbol &sym) {
  object::Archive::Child c =
      CHECK(sym.getMember(), toString(this) +
                                 ": could not get the member defining symbol " +
                                 toMachOString(sym));

  // `sym` is owned by a LazySym, which will be replace<>()d by make<ObjFile>
  // and become invalid after that call. Copy it to the stack so we can refer
  // to it later.
  const object::Archive::Symbol symCopy = sym;

  // ld64 doesn't demangle sym here even with -demangle.
  // Match that: intentionally don't call toMachOString().
  if (Error e = fetch(c, symCopy.getName()))
    error(toString(this) + ": could not get the member defining symbol " +
          toMachOString(symCopy) + ": " + toString(std::move(e)));
}

static macho::Symbol *createBitcodeSymbol(const lto::InputFile::Symbol &objSym,
                                          BitcodeFile &file) {
  StringRef name = saver().save(objSym.getName());

  if (objSym.isUndefined())
    return symtab->addUndefined(name, &file, /*isWeakRef=*/objSym.isWeak());

  // TODO: Write a test demonstrating why computing isPrivateExtern before
  // LTO compilation is important.
  bool isPrivateExtern = false;
  switch (objSym.getVisibility()) {
  case GlobalValue::HiddenVisibility:
    isPrivateExtern = true;
    break;
  case GlobalValue::ProtectedVisibility:
    error(name + " has protected visibility, which is not supported by Mach-O");
    break;
  case GlobalValue::DefaultVisibility:
    break;
  }
  isPrivateExtern = isPrivateExtern || objSym.canBeOmittedFromSymbolTable() ||
                    file.forceHidden;

  if (objSym.isCommon())
    return symtab->addCommon(name, &file, objSym.getCommonSize(),
                             objSym.getCommonAlignment(), isPrivateExtern);

  return symtab->addDefined(name, &file, /*isec=*/nullptr, /*value=*/0,
                            /*size=*/0, objSym.isWeak(), isPrivateExtern,
                            /*isReferencedDynamically=*/false,
                            /*noDeadStrip=*/false,
                            /*isWeakDefCanBeHidden=*/false);
}

BitcodeFile::BitcodeFile(MemoryBufferRef mb, StringRef archiveName,
                         uint64_t offsetInArchive, bool lazy, bool forceHidden,
                         bool compatArch)
    : InputFile(BitcodeKind, mb, lazy), forceHidden(forceHidden) {
  this->archiveName = std::string(archiveName);
  this->compatArch = compatArch;
  std::string path = mb.getBufferIdentifier().str();
  if (config->thinLTOIndexOnly)
    path = replaceThinLTOSuffix(mb.getBufferIdentifier());

  // If the parent archive already determines that the arch is not compat with
  // target, then just return.
  if (!compatArch)
    return;

  // ThinLTO assumes that all MemoryBufferRefs given to it have a unique
  // name. If two members with the same name are provided, this causes a
  // collision and ThinLTO can't proceed.
  // So, we append the archive name to disambiguate two members with the same
  // name from multiple different archives, and offset within the archive to
  // disambiguate two members of the same name from a single archive.
  MemoryBufferRef mbref(mb.getBuffer(),
                        saver().save(archiveName.empty()
                                         ? path
                                         : archiveName + "(" +
                                               sys::path::filename(path) + ")" +
                                               utostr(offsetInArchive)));
  obj = check(lto::InputFile::create(mbref));
  if (lazy)
    parseLazy();
  else
    parse();
}

void BitcodeFile::parse() {
  // Convert LTO Symbols to LLD Symbols in order to perform resolution. The
  // "winning" symbol will then be marked as Prevailing at LTO compilation
  // time.
  symbols.resize(obj->symbols().size());

  // Process defined symbols first. See the comment at the end of
  // ObjFile<>::parseSymbols.
  for (auto it : llvm::enumerate(obj->symbols()))
    if (!it.value().isUndefined())
      symbols[it.index()] = createBitcodeSymbol(it.value(), *this);
  for (auto it : llvm::enumerate(obj->symbols()))
    if (it.value().isUndefined())
      symbols[it.index()] = createBitcodeSymbol(it.value(), *this);
}

void BitcodeFile::parseLazy() {
  symbols.resize(obj->symbols().size());
  for (const auto &[i, objSym] : llvm::enumerate(obj->symbols())) {
    if (!objSym.isUndefined()) {
      symbols[i] = symtab->addLazyObject(saver().save(objSym.getName()), *this);
      if (!lazy)
        break;
    }
  }
}

std::string macho::replaceThinLTOSuffix(StringRef path) {
  auto [suffix, repl] = config->thinLTOObjectSuffixReplace;
  if (path.consume_back(suffix))
    return (path + repl).str();
  return std::string(path);
}

void macho::extract(InputFile &file, StringRef reason) {
  if (!file.lazy)
    return;
  file.lazy = false;

  printArchiveMemberLoad(reason, &file);
  if (auto *bitcode = dyn_cast<BitcodeFile>(&file)) {
    bitcode->parse();
  } else {
    auto &f = cast<ObjFile>(file);
    if (target->wordSize == 8)
      f.parse<LP64>();
    else
      f.parse<ILP32>();
  }
}

template void ObjFile::parse<LP64>();
