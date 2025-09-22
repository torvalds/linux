//===- SyntheticSections.cpp ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains linker-synthesized sections. Currently,
// synthetic sections are created either output sections or input sections,
// but we are rewriting code so that all synthetic sections are created as
// input sections.
//
//===----------------------------------------------------------------------===//

#include "SyntheticSections.h"
#include "Config.h"
#include "DWARF.h"
#include "EhFrame.h"
#include "InputFiles.h"
#include "LinkerScript.h"
#include "OutputSections.h"
#include "SymbolTable.h"
#include "Symbols.h"
#include "Target.h"
#include "Thunks.h"
#include "Writer.h"
#include "lld/Common/CommonLinkerContext.h"
#include "lld/Common/DWARF.h"
#include "lld/Common/Strings.h"
#include "lld/Common/Version.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Sequence.h"
#include "llvm/ADT/SetOperations.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/DebugInfo/DWARF/DWARFAcceleratorTable.h"
#include "llvm/DebugInfo/DWARF/DWARFDebugPubTable.h"
#include "llvm/Support/DJB.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/LEB128.h"
#include "llvm/Support/Parallel.h"
#include "llvm/Support/TimeProfiler.h"
#include <cinttypes>
#include <cstdlib>

using namespace llvm;
using namespace llvm::dwarf;
using namespace llvm::ELF;
using namespace llvm::object;
using namespace llvm::support;
using namespace lld;
using namespace lld::elf;

using llvm::support::endian::read32le;
using llvm::support::endian::write32le;
using llvm::support::endian::write64le;

constexpr size_t MergeNoTailSection::numShards;

static uint64_t readUint(uint8_t *buf) {
  return config->is64 ? read64(buf) : read32(buf);
}

static void writeUint(uint8_t *buf, uint64_t val) {
  if (config->is64)
    write64(buf, val);
  else
    write32(buf, val);
}

// Returns an LLD version string.
static ArrayRef<uint8_t> getVersion() {
  // Check LLD_VERSION first for ease of testing.
  // You can get consistent output by using the environment variable.
  // This is only for testing.
  StringRef s = getenv("LLD_VERSION");
  if (s.empty())
    s = saver().save(Twine("Linker: ") + getLLDVersion());

  // +1 to include the terminating '\0'.
  return {(const uint8_t *)s.data(), s.size() + 1};
}

// Creates a .comment section containing LLD version info.
// With this feature, you can identify LLD-generated binaries easily
// by "readelf --string-dump .comment <file>".
// The returned object is a mergeable string section.
MergeInputSection *elf::createCommentSection() {
  auto *sec = make<MergeInputSection>(SHF_MERGE | SHF_STRINGS, SHT_PROGBITS, 1,
                                      getVersion(), ".comment");
  sec->splitIntoPieces();
  return sec;
}

// .MIPS.abiflags section.
template <class ELFT>
MipsAbiFlagsSection<ELFT>::MipsAbiFlagsSection(Elf_Mips_ABIFlags flags)
    : SyntheticSection(SHF_ALLOC, SHT_MIPS_ABIFLAGS, 8, ".MIPS.abiflags"),
      flags(flags) {
  this->entsize = sizeof(Elf_Mips_ABIFlags);
}

template <class ELFT> void MipsAbiFlagsSection<ELFT>::writeTo(uint8_t *buf) {
  memcpy(buf, &flags, sizeof(flags));
}

template <class ELFT>
std::unique_ptr<MipsAbiFlagsSection<ELFT>> MipsAbiFlagsSection<ELFT>::create() {
  Elf_Mips_ABIFlags flags = {};
  bool create = false;

  for (InputSectionBase *sec : ctx.inputSections) {
    if (sec->type != SHT_MIPS_ABIFLAGS)
      continue;
    sec->markDead();
    create = true;

    std::string filename = toString(sec->file);
    const size_t size = sec->content().size();
    // Older version of BFD (such as the default FreeBSD linker) concatenate
    // .MIPS.abiflags instead of merging. To allow for this case (or potential
    // zero padding) we ignore everything after the first Elf_Mips_ABIFlags
    if (size < sizeof(Elf_Mips_ABIFlags)) {
      error(filename + ": invalid size of .MIPS.abiflags section: got " +
            Twine(size) + " instead of " + Twine(sizeof(Elf_Mips_ABIFlags)));
      return nullptr;
    }
    auto *s =
        reinterpret_cast<const Elf_Mips_ABIFlags *>(sec->content().data());
    if (s->version != 0) {
      error(filename + ": unexpected .MIPS.abiflags version " +
            Twine(s->version));
      return nullptr;
    }

    // LLD checks ISA compatibility in calcMipsEFlags(). Here we just
    // select the highest number of ISA/Rev/Ext.
    flags.isa_level = std::max(flags.isa_level, s->isa_level);
    flags.isa_rev = std::max(flags.isa_rev, s->isa_rev);
    flags.isa_ext = std::max(flags.isa_ext, s->isa_ext);
    flags.gpr_size = std::max(flags.gpr_size, s->gpr_size);
    flags.cpr1_size = std::max(flags.cpr1_size, s->cpr1_size);
    flags.cpr2_size = std::max(flags.cpr2_size, s->cpr2_size);
    flags.ases |= s->ases;
    flags.flags1 |= s->flags1;
    flags.flags2 |= s->flags2;
    flags.fp_abi = elf::getMipsFpAbiFlag(flags.fp_abi, s->fp_abi, filename);
  };

  if (create)
    return std::make_unique<MipsAbiFlagsSection<ELFT>>(flags);
  return nullptr;
}

// .MIPS.options section.
template <class ELFT>
MipsOptionsSection<ELFT>::MipsOptionsSection(Elf_Mips_RegInfo reginfo)
    : SyntheticSection(SHF_ALLOC, SHT_MIPS_OPTIONS, 8, ".MIPS.options"),
      reginfo(reginfo) {
  this->entsize = sizeof(Elf_Mips_Options) + sizeof(Elf_Mips_RegInfo);
}

template <class ELFT> void MipsOptionsSection<ELFT>::writeTo(uint8_t *buf) {
  auto *options = reinterpret_cast<Elf_Mips_Options *>(buf);
  options->kind = ODK_REGINFO;
  options->size = getSize();

  if (!config->relocatable)
    reginfo.ri_gp_value = in.mipsGot->getGp();
  memcpy(buf + sizeof(Elf_Mips_Options), &reginfo, sizeof(reginfo));
}

template <class ELFT>
std::unique_ptr<MipsOptionsSection<ELFT>> MipsOptionsSection<ELFT>::create() {
  // N64 ABI only.
  if (!ELFT::Is64Bits)
    return nullptr;

  SmallVector<InputSectionBase *, 0> sections;
  for (InputSectionBase *sec : ctx.inputSections)
    if (sec->type == SHT_MIPS_OPTIONS)
      sections.push_back(sec);

  if (sections.empty())
    return nullptr;

  Elf_Mips_RegInfo reginfo = {};
  for (InputSectionBase *sec : sections) {
    sec->markDead();

    std::string filename = toString(sec->file);
    ArrayRef<uint8_t> d = sec->content();

    while (!d.empty()) {
      if (d.size() < sizeof(Elf_Mips_Options)) {
        error(filename + ": invalid size of .MIPS.options section");
        break;
      }

      auto *opt = reinterpret_cast<const Elf_Mips_Options *>(d.data());
      if (opt->kind == ODK_REGINFO) {
        reginfo.ri_gprmask |= opt->getRegInfo().ri_gprmask;
        sec->getFile<ELFT>()->mipsGp0 = opt->getRegInfo().ri_gp_value;
        break;
      }

      if (!opt->size)
        fatal(filename + ": zero option descriptor size");
      d = d.slice(opt->size);
    }
  };

  return std::make_unique<MipsOptionsSection<ELFT>>(reginfo);
}

// MIPS .reginfo section.
template <class ELFT>
MipsReginfoSection<ELFT>::MipsReginfoSection(Elf_Mips_RegInfo reginfo)
    : SyntheticSection(SHF_ALLOC, SHT_MIPS_REGINFO, 4, ".reginfo"),
      reginfo(reginfo) {
  this->entsize = sizeof(Elf_Mips_RegInfo);
}

template <class ELFT> void MipsReginfoSection<ELFT>::writeTo(uint8_t *buf) {
  if (!config->relocatable)
    reginfo.ri_gp_value = in.mipsGot->getGp();
  memcpy(buf, &reginfo, sizeof(reginfo));
}

template <class ELFT>
std::unique_ptr<MipsReginfoSection<ELFT>> MipsReginfoSection<ELFT>::create() {
  // Section should be alive for O32 and N32 ABIs only.
  if (ELFT::Is64Bits)
    return nullptr;

  SmallVector<InputSectionBase *, 0> sections;
  for (InputSectionBase *sec : ctx.inputSections)
    if (sec->type == SHT_MIPS_REGINFO)
      sections.push_back(sec);

  if (sections.empty())
    return nullptr;

  Elf_Mips_RegInfo reginfo = {};
  for (InputSectionBase *sec : sections) {
    sec->markDead();

    if (sec->content().size() != sizeof(Elf_Mips_RegInfo)) {
      error(toString(sec->file) + ": invalid size of .reginfo section");
      return nullptr;
    }

    auto *r = reinterpret_cast<const Elf_Mips_RegInfo *>(sec->content().data());
    reginfo.ri_gprmask |= r->ri_gprmask;
    sec->getFile<ELFT>()->mipsGp0 = r->ri_gp_value;
  };

  return std::make_unique<MipsReginfoSection<ELFT>>(reginfo);
}

InputSection *elf::createInterpSection() {
  // StringSaver guarantees that the returned string ends with '\0'.
  StringRef s = saver().save(config->dynamicLinker);
  ArrayRef<uint8_t> contents = {(const uint8_t *)s.data(), s.size() + 1};

  return make<InputSection>(ctx.internalFile, SHF_ALLOC, SHT_PROGBITS, 1,
                            contents, ".interp");
}

Defined *elf::addSyntheticLocal(StringRef name, uint8_t type, uint64_t value,
                                uint64_t size, InputSectionBase &section) {
  Defined *s = makeDefined(section.file, name, STB_LOCAL, STV_DEFAULT, type,
                           value, size, &section);
  if (in.symTab)
    in.symTab->addSymbol(s);

  if (config->emachine == EM_ARM && !config->isLE && config->armBe8 &&
      (section.flags & SHF_EXECINSTR))
    // Adding Linker generated mapping symbols to the arm specific mapping
    // symbols list.
    addArmSyntheticSectionMappingSymbol(s);

  return s;
}

static size_t getHashSize() {
  switch (config->buildId) {
  case BuildIdKind::Fast:
    return 8;
  case BuildIdKind::Md5:
  case BuildIdKind::Uuid:
    return 16;
  case BuildIdKind::Sha1:
    return 20;
  case BuildIdKind::Hexstring:
    return config->buildIdVector.size();
  default:
    llvm_unreachable("unknown BuildIdKind");
  }
}

// This class represents a linker-synthesized .note.gnu.property section.
//
// In x86 and AArch64, object files may contain feature flags indicating the
// features that they have used. The flags are stored in a .note.gnu.property
// section.
//
// lld reads the sections from input files and merges them by computing AND of
// the flags. The result is written as a new .note.gnu.property section.
//
// If the flag is zero (which indicates that the intersection of the feature
// sets is empty, or some input files didn't have .note.gnu.property sections),
// we don't create this section.
GnuPropertySection::GnuPropertySection()
    : SyntheticSection(llvm::ELF::SHF_ALLOC, llvm::ELF::SHT_NOTE,
                       config->wordsize, ".note.gnu.property") {}

void GnuPropertySection::writeTo(uint8_t *buf) {
  write32(buf, 4);                          // Name size
  write32(buf + 4, getSize() - 16);         // Content size
  write32(buf + 8, NT_GNU_PROPERTY_TYPE_0); // Type
  memcpy(buf + 12, "GNU", 4);               // Name string

  uint32_t featureAndType = config->emachine == EM_AARCH64
                                ? GNU_PROPERTY_AARCH64_FEATURE_1_AND
                                : GNU_PROPERTY_X86_FEATURE_1_AND;

  unsigned offset = 16;
  if (config->andFeatures != 0) {
    write32(buf + offset + 0, featureAndType);      // Feature type
    write32(buf + offset + 4, 4);                   // Feature size
    write32(buf + offset + 8, config->andFeatures); // Feature flags
    if (config->is64)
      write32(buf + offset + 12, 0); // Padding
    offset += 16;
  }

  if (!ctx.aarch64PauthAbiCoreInfo.empty()) {
    write32(buf + offset + 0, GNU_PROPERTY_AARCH64_FEATURE_PAUTH);
    write32(buf + offset + 4, ctx.aarch64PauthAbiCoreInfo.size());
    memcpy(buf + offset + 8, ctx.aarch64PauthAbiCoreInfo.data(),
           ctx.aarch64PauthAbiCoreInfo.size());
  }
}

size_t GnuPropertySection::getSize() const {
  uint32_t contentSize = 0;
  if (config->andFeatures != 0)
    contentSize += config->is64 ? 16 : 12;
  if (!ctx.aarch64PauthAbiCoreInfo.empty())
    contentSize += 4 + 4 + ctx.aarch64PauthAbiCoreInfo.size();
  assert(contentSize != 0);
  return contentSize + 16;
}

BuildIdSection::BuildIdSection()
    : SyntheticSection(SHF_ALLOC, SHT_NOTE, 4, ".note.gnu.build-id"),
      hashSize(getHashSize()) {}

void BuildIdSection::writeTo(uint8_t *buf) {
  write32(buf, 4);                      // Name size
  write32(buf + 4, hashSize);           // Content size
  write32(buf + 8, NT_GNU_BUILD_ID);    // Type
  memcpy(buf + 12, "GNU", 4);           // Name string
  hashBuf = buf + 16;
}

void BuildIdSection::writeBuildId(ArrayRef<uint8_t> buf) {
  assert(buf.size() == hashSize);
  memcpy(hashBuf, buf.data(), hashSize);
}

BssSection::BssSection(StringRef name, uint64_t size, uint32_t alignment)
    : SyntheticSection(SHF_ALLOC | SHF_WRITE, SHT_NOBITS, alignment, name) {
  this->bss = true;
  this->size = size;
}

EhFrameSection::EhFrameSection()
    : SyntheticSection(SHF_ALLOC, SHT_PROGBITS, 1, ".eh_frame") {}

// Search for an existing CIE record or create a new one.
// CIE records from input object files are uniquified by their contents
// and where their relocations point to.
template <class ELFT, class RelTy>
CieRecord *EhFrameSection::addCie(EhSectionPiece &cie, ArrayRef<RelTy> rels) {
  Symbol *personality = nullptr;
  unsigned firstRelI = cie.firstRelocation;
  if (firstRelI != (unsigned)-1)
    personality = &cie.sec->file->getRelocTargetSym(rels[firstRelI]);

  // Search for an existing CIE by CIE contents/relocation target pair.
  CieRecord *&rec = cieMap[{cie.data(), personality}];

  // If not found, create a new one.
  if (!rec) {
    rec = make<CieRecord>();
    rec->cie = &cie;
    cieRecords.push_back(rec);
  }
  return rec;
}

// There is one FDE per function. Returns a non-null pointer to the function
// symbol if the given FDE points to a live function.
template <class ELFT, class RelTy>
Defined *EhFrameSection::isFdeLive(EhSectionPiece &fde, ArrayRef<RelTy> rels) {
  auto *sec = cast<EhInputSection>(fde.sec);
  unsigned firstRelI = fde.firstRelocation;

  // An FDE should point to some function because FDEs are to describe
  // functions. That's however not always the case due to an issue of
  // ld.gold with -r. ld.gold may discard only functions and leave their
  // corresponding FDEs, which results in creating bad .eh_frame sections.
  // To deal with that, we ignore such FDEs.
  if (firstRelI == (unsigned)-1)
    return nullptr;

  const RelTy &rel = rels[firstRelI];
  Symbol &b = sec->file->getRelocTargetSym(rel);

  // FDEs for garbage-collected or merged-by-ICF sections, or sections in
  // another partition, are dead.
  if (auto *d = dyn_cast<Defined>(&b))
    if (!d->folded && d->section && d->section->partition == partition)
      return d;
  return nullptr;
}

// .eh_frame is a sequence of CIE or FDE records. In general, there
// is one CIE record per input object file which is followed by
// a list of FDEs. This function searches an existing CIE or create a new
// one and associates FDEs to the CIE.
template <class ELFT, class RelTy>
void EhFrameSection::addRecords(EhInputSection *sec, ArrayRef<RelTy> rels) {
  offsetToCie.clear();
  for (EhSectionPiece &cie : sec->cies)
    offsetToCie[cie.inputOff] = addCie<ELFT>(cie, rels);
  for (EhSectionPiece &fde : sec->fdes) {
    uint32_t id = endian::read32<ELFT::Endianness>(fde.data().data() + 4);
    CieRecord *rec = offsetToCie[fde.inputOff + 4 - id];
    if (!rec)
      fatal(toString(sec) + ": invalid CIE reference");

    if (!isFdeLive<ELFT>(fde, rels))
      continue;
    rec->fdes.push_back(&fde);
    numFdes++;
  }
}

template <class ELFT>
void EhFrameSection::addSectionAux(EhInputSection *sec) {
  if (!sec->isLive())
    return;
  const RelsOrRelas<ELFT> rels =
      sec->template relsOrRelas<ELFT>(/*supportsCrel=*/false);
  if (rels.areRelocsRel())
    addRecords<ELFT>(sec, rels.rels);
  else
    addRecords<ELFT>(sec, rels.relas);
}

// Used by ICF<ELFT>::handleLSDA(). This function is very similar to
// EhFrameSection::addRecords().
template <class ELFT, class RelTy>
void EhFrameSection::iterateFDEWithLSDAAux(
    EhInputSection &sec, ArrayRef<RelTy> rels, DenseSet<size_t> &ciesWithLSDA,
    llvm::function_ref<void(InputSection &)> fn) {
  for (EhSectionPiece &cie : sec.cies)
    if (hasLSDA(cie))
      ciesWithLSDA.insert(cie.inputOff);
  for (EhSectionPiece &fde : sec.fdes) {
    uint32_t id = endian::read32<ELFT::Endianness>(fde.data().data() + 4);
    if (!ciesWithLSDA.contains(fde.inputOff + 4 - id))
      continue;

    // The CIE has a LSDA argument. Call fn with d's section.
    if (Defined *d = isFdeLive<ELFT>(fde, rels))
      if (auto *s = dyn_cast_or_null<InputSection>(d->section))
        fn(*s);
  }
}

template <class ELFT>
void EhFrameSection::iterateFDEWithLSDA(
    llvm::function_ref<void(InputSection &)> fn) {
  DenseSet<size_t> ciesWithLSDA;
  for (EhInputSection *sec : sections) {
    ciesWithLSDA.clear();
    const RelsOrRelas<ELFT> rels =
        sec->template relsOrRelas<ELFT>(/*supportsCrel=*/false);
    if (rels.areRelocsRel())
      iterateFDEWithLSDAAux<ELFT>(*sec, rels.rels, ciesWithLSDA, fn);
    else
      iterateFDEWithLSDAAux<ELFT>(*sec, rels.relas, ciesWithLSDA, fn);
  }
}

static void writeCieFde(uint8_t *buf, ArrayRef<uint8_t> d) {
  memcpy(buf, d.data(), d.size());
  // Fix the size field. -4 since size does not include the size field itself.
  write32(buf, d.size() - 4);
}

void EhFrameSection::finalizeContents() {
  assert(!this->size); // Not finalized.

  switch (config->ekind) {
  case ELFNoneKind:
    llvm_unreachable("invalid ekind");
  case ELF32LEKind:
    for (EhInputSection *sec : sections)
      addSectionAux<ELF32LE>(sec);
    break;
  case ELF32BEKind:
    for (EhInputSection *sec : sections)
      addSectionAux<ELF32BE>(sec);
    break;
  case ELF64LEKind:
    for (EhInputSection *sec : sections)
      addSectionAux<ELF64LE>(sec);
    break;
  case ELF64BEKind:
    for (EhInputSection *sec : sections)
      addSectionAux<ELF64BE>(sec);
    break;
  }

  size_t off = 0;
  for (CieRecord *rec : cieRecords) {
    rec->cie->outputOff = off;
    off += rec->cie->size;

    for (EhSectionPiece *fde : rec->fdes) {
      fde->outputOff = off;
      off += fde->size;
    }
  }

  // The LSB standard does not allow a .eh_frame section with zero
  // Call Frame Information records. glibc unwind-dw2-fde.c
  // classify_object_over_fdes expects there is a CIE record length 0 as a
  // terminator. Thus we add one unconditionally.
  off += 4;

  this->size = off;
}

// Returns data for .eh_frame_hdr. .eh_frame_hdr is a binary search table
// to get an FDE from an address to which FDE is applied. This function
// returns a list of such pairs.
SmallVector<EhFrameSection::FdeData, 0> EhFrameSection::getFdeData() const {
  uint8_t *buf = Out::bufferStart + getParent()->offset + outSecOff;
  SmallVector<FdeData, 0> ret;

  uint64_t va = getPartition().ehFrameHdr->getVA();
  for (CieRecord *rec : cieRecords) {
    uint8_t enc = getFdeEncoding(rec->cie);
    for (EhSectionPiece *fde : rec->fdes) {
      uint64_t pc = getFdePc(buf, fde->outputOff, enc);
      uint64_t fdeVA = getParent()->addr + fde->outputOff;
      if (!isInt<32>(pc - va)) {
        errorOrWarn(toString(fde->sec) + ": PC offset is too large: 0x" +
                    Twine::utohexstr(pc - va));
        continue;
      }
      ret.push_back({uint32_t(pc - va), uint32_t(fdeVA - va)});
    }
  }

  // Sort the FDE list by their PC and uniqueify. Usually there is only
  // one FDE for a PC (i.e. function), but if ICF merges two functions
  // into one, there can be more than one FDEs pointing to the address.
  auto less = [](const FdeData &a, const FdeData &b) {
    return a.pcRel < b.pcRel;
  };
  llvm::stable_sort(ret, less);
  auto eq = [](const FdeData &a, const FdeData &b) {
    return a.pcRel == b.pcRel;
  };
  ret.erase(std::unique(ret.begin(), ret.end(), eq), ret.end());

  return ret;
}

static uint64_t readFdeAddr(uint8_t *buf, int size) {
  switch (size) {
  case DW_EH_PE_udata2:
    return read16(buf);
  case DW_EH_PE_sdata2:
    return (int16_t)read16(buf);
  case DW_EH_PE_udata4:
    return read32(buf);
  case DW_EH_PE_sdata4:
    return (int32_t)read32(buf);
  case DW_EH_PE_udata8:
  case DW_EH_PE_sdata8:
    return read64(buf);
  case DW_EH_PE_absptr:
    return readUint(buf);
  }
  fatal("unknown FDE size encoding");
}

// Returns the VA to which a given FDE (on a mmap'ed buffer) is applied to.
// We need it to create .eh_frame_hdr section.
uint64_t EhFrameSection::getFdePc(uint8_t *buf, size_t fdeOff,
                                  uint8_t enc) const {
  // The starting address to which this FDE applies is
  // stored at FDE + 8 byte. And this offset is within
  // the .eh_frame section.
  size_t off = fdeOff + 8;
  uint64_t addr = readFdeAddr(buf + off, enc & 0xf);
  if ((enc & 0x70) == DW_EH_PE_absptr)
    return config->is64 ? addr : uint32_t(addr);
  if ((enc & 0x70) == DW_EH_PE_pcrel)
    return addr + getParent()->addr + off + outSecOff;
  fatal("unknown FDE size relative encoding");
}

void EhFrameSection::writeTo(uint8_t *buf) {
  // Write CIE and FDE records.
  for (CieRecord *rec : cieRecords) {
    size_t cieOffset = rec->cie->outputOff;
    writeCieFde(buf + cieOffset, rec->cie->data());

    for (EhSectionPiece *fde : rec->fdes) {
      size_t off = fde->outputOff;
      writeCieFde(buf + off, fde->data());

      // FDE's second word should have the offset to an associated CIE.
      // Write it.
      write32(buf + off + 4, off + 4 - cieOffset);
    }
  }

  // Apply relocations. .eh_frame section contents are not contiguous
  // in the output buffer, but relocateAlloc() still works because
  // getOffset() takes care of discontiguous section pieces.
  for (EhInputSection *s : sections)
    target->relocateAlloc(*s, buf);

  if (getPartition().ehFrameHdr && getPartition().ehFrameHdr->getParent())
    getPartition().ehFrameHdr->write();
}

GotSection::GotSection()
    : SyntheticSection(SHF_ALLOC | SHF_WRITE, SHT_PROGBITS,
                       target->gotEntrySize, ".got") {
  numEntries = target->gotHeaderEntriesNum;
}

void GotSection::addConstant(const Relocation &r) { relocations.push_back(r); }
void GotSection::addEntry(const Symbol &sym) {
  assert(sym.auxIdx == symAux.size() - 1);
  symAux.back().gotIdx = numEntries++;
}

bool GotSection::addTlsDescEntry(const Symbol &sym) {
  assert(sym.auxIdx == symAux.size() - 1);
  symAux.back().tlsDescIdx = numEntries;
  numEntries += 2;
  return true;
}

bool GotSection::addDynTlsEntry(const Symbol &sym) {
  assert(sym.auxIdx == symAux.size() - 1);
  symAux.back().tlsGdIdx = numEntries;
  // Global Dynamic TLS entries take two GOT slots.
  numEntries += 2;
  return true;
}

// Reserves TLS entries for a TLS module ID and a TLS block offset.
// In total it takes two GOT slots.
bool GotSection::addTlsIndex() {
  if (tlsIndexOff != uint32_t(-1))
    return false;
  tlsIndexOff = numEntries * config->wordsize;
  numEntries += 2;
  return true;
}

uint32_t GotSection::getTlsDescOffset(const Symbol &sym) const {
  return sym.getTlsDescIdx() * config->wordsize;
}

uint64_t GotSection::getTlsDescAddr(const Symbol &sym) const {
  return getVA() + getTlsDescOffset(sym);
}

uint64_t GotSection::getGlobalDynAddr(const Symbol &b) const {
  return this->getVA() + b.getTlsGdIdx() * config->wordsize;
}

uint64_t GotSection::getGlobalDynOffset(const Symbol &b) const {
  return b.getTlsGdIdx() * config->wordsize;
}

void GotSection::finalizeContents() {
  if (config->emachine == EM_PPC64 &&
      numEntries <= target->gotHeaderEntriesNum && !ElfSym::globalOffsetTable)
    size = 0;
  else
    size = numEntries * config->wordsize;
}

bool GotSection::isNeeded() const {
  // Needed if the GOT symbol is used or the number of entries is more than just
  // the header. A GOT with just the header may not be needed.
  return hasGotOffRel || numEntries > target->gotHeaderEntriesNum;
}

void GotSection::writeTo(uint8_t *buf) {
  // On PPC64 .got may be needed but empty. Skip the write.
  if (size == 0)
    return;
  target->writeGotHeader(buf);
  target->relocateAlloc(*this, buf);
}

static uint64_t getMipsPageAddr(uint64_t addr) {
  return (addr + 0x8000) & ~0xffff;
}

static uint64_t getMipsPageCount(uint64_t size) {
  return (size + 0xfffe) / 0xffff + 1;
}

MipsGotSection::MipsGotSection()
    : SyntheticSection(SHF_ALLOC | SHF_WRITE | SHF_MIPS_GPREL, SHT_PROGBITS, 16,
                       ".got") {}

void MipsGotSection::addEntry(InputFile &file, Symbol &sym, int64_t addend,
                              RelExpr expr) {
  FileGot &g = getGot(file);
  if (expr == R_MIPS_GOT_LOCAL_PAGE) {
    if (const OutputSection *os = sym.getOutputSection())
      g.pagesMap.insert({os, {}});
    else
      g.local16.insert({{nullptr, getMipsPageAddr(sym.getVA(addend))}, 0});
  } else if (sym.isTls())
    g.tls.insert({&sym, 0});
  else if (sym.isPreemptible && expr == R_ABS)
    g.relocs.insert({&sym, 0});
  else if (sym.isPreemptible)
    g.global.insert({&sym, 0});
  else if (expr == R_MIPS_GOT_OFF32)
    g.local32.insert({{&sym, addend}, 0});
  else
    g.local16.insert({{&sym, addend}, 0});
}

void MipsGotSection::addDynTlsEntry(InputFile &file, Symbol &sym) {
  getGot(file).dynTlsSymbols.insert({&sym, 0});
}

void MipsGotSection::addTlsIndex(InputFile &file) {
  getGot(file).dynTlsSymbols.insert({nullptr, 0});
}

size_t MipsGotSection::FileGot::getEntriesNum() const {
  return getPageEntriesNum() + local16.size() + global.size() + relocs.size() +
         tls.size() + dynTlsSymbols.size() * 2;
}

size_t MipsGotSection::FileGot::getPageEntriesNum() const {
  size_t num = 0;
  for (const std::pair<const OutputSection *, FileGot::PageBlock> &p : pagesMap)
    num += p.second.count;
  return num;
}

size_t MipsGotSection::FileGot::getIndexedEntriesNum() const {
  size_t count = getPageEntriesNum() + local16.size() + global.size();
  // If there are relocation-only entries in the GOT, TLS entries
  // are allocated after them. TLS entries should be addressable
  // by 16-bit index so count both reloc-only and TLS entries.
  if (!tls.empty() || !dynTlsSymbols.empty())
    count += relocs.size() + tls.size() + dynTlsSymbols.size() * 2;
  return count;
}

MipsGotSection::FileGot &MipsGotSection::getGot(InputFile &f) {
  if (f.mipsGotIndex == uint32_t(-1)) {
    gots.emplace_back();
    gots.back().file = &f;
    f.mipsGotIndex = gots.size() - 1;
  }
  return gots[f.mipsGotIndex];
}

uint64_t MipsGotSection::getPageEntryOffset(const InputFile *f,
                                            const Symbol &sym,
                                            int64_t addend) const {
  const FileGot &g = gots[f->mipsGotIndex];
  uint64_t index = 0;
  if (const OutputSection *outSec = sym.getOutputSection()) {
    uint64_t secAddr = getMipsPageAddr(outSec->addr);
    uint64_t symAddr = getMipsPageAddr(sym.getVA(addend));
    index = g.pagesMap.lookup(outSec).firstIndex + (symAddr - secAddr) / 0xffff;
  } else {
    index = g.local16.lookup({nullptr, getMipsPageAddr(sym.getVA(addend))});
  }
  return index * config->wordsize;
}

uint64_t MipsGotSection::getSymEntryOffset(const InputFile *f, const Symbol &s,
                                           int64_t addend) const {
  const FileGot &g = gots[f->mipsGotIndex];
  Symbol *sym = const_cast<Symbol *>(&s);
  if (sym->isTls())
    return g.tls.lookup(sym) * config->wordsize;
  if (sym->isPreemptible)
    return g.global.lookup(sym) * config->wordsize;
  return g.local16.lookup({sym, addend}) * config->wordsize;
}

uint64_t MipsGotSection::getTlsIndexOffset(const InputFile *f) const {
  const FileGot &g = gots[f->mipsGotIndex];
  return g.dynTlsSymbols.lookup(nullptr) * config->wordsize;
}

uint64_t MipsGotSection::getGlobalDynOffset(const InputFile *f,
                                            const Symbol &s) const {
  const FileGot &g = gots[f->mipsGotIndex];
  Symbol *sym = const_cast<Symbol *>(&s);
  return g.dynTlsSymbols.lookup(sym) * config->wordsize;
}

const Symbol *MipsGotSection::getFirstGlobalEntry() const {
  if (gots.empty())
    return nullptr;
  const FileGot &primGot = gots.front();
  if (!primGot.global.empty())
    return primGot.global.front().first;
  if (!primGot.relocs.empty())
    return primGot.relocs.front().first;
  return nullptr;
}

unsigned MipsGotSection::getLocalEntriesNum() const {
  if (gots.empty())
    return headerEntriesNum;
  return headerEntriesNum + gots.front().getPageEntriesNum() +
         gots.front().local16.size();
}

bool MipsGotSection::tryMergeGots(FileGot &dst, FileGot &src, bool isPrimary) {
  FileGot tmp = dst;
  set_union(tmp.pagesMap, src.pagesMap);
  set_union(tmp.local16, src.local16);
  set_union(tmp.global, src.global);
  set_union(tmp.relocs, src.relocs);
  set_union(tmp.tls, src.tls);
  set_union(tmp.dynTlsSymbols, src.dynTlsSymbols);

  size_t count = isPrimary ? headerEntriesNum : 0;
  count += tmp.getIndexedEntriesNum();

  if (count * config->wordsize > config->mipsGotSize)
    return false;

  std::swap(tmp, dst);
  return true;
}

void MipsGotSection::finalizeContents() { updateAllocSize(); }

bool MipsGotSection::updateAllocSize() {
  size = headerEntriesNum * config->wordsize;
  for (const FileGot &g : gots)
    size += g.getEntriesNum() * config->wordsize;
  return false;
}

void MipsGotSection::build() {
  if (gots.empty())
    return;

  std::vector<FileGot> mergedGots(1);

  // For each GOT move non-preemptible symbols from the `Global`
  // to `Local16` list. Preemptible symbol might become non-preemptible
  // one if, for example, it gets a related copy relocation.
  for (FileGot &got : gots) {
    for (auto &p: got.global)
      if (!p.first->isPreemptible)
        got.local16.insert({{p.first, 0}, 0});
    got.global.remove_if([&](const std::pair<Symbol *, size_t> &p) {
      return !p.first->isPreemptible;
    });
  }

  // For each GOT remove "reloc-only" entry if there is "global"
  // entry for the same symbol. And add local entries which indexed
  // using 32-bit value at the end of 16-bit entries.
  for (FileGot &got : gots) {
    got.relocs.remove_if([&](const std::pair<Symbol *, size_t> &p) {
      return got.global.count(p.first);
    });
    set_union(got.local16, got.local32);
    got.local32.clear();
  }

  // Evaluate number of "reloc-only" entries in the resulting GOT.
  // To do that put all unique "reloc-only" and "global" entries
  // from all GOTs to the future primary GOT.
  FileGot *primGot = &mergedGots.front();
  for (FileGot &got : gots) {
    set_union(primGot->relocs, got.global);
    set_union(primGot->relocs, got.relocs);
    got.relocs.clear();
  }

  // Evaluate number of "page" entries in each GOT.
  for (FileGot &got : gots) {
    for (std::pair<const OutputSection *, FileGot::PageBlock> &p :
         got.pagesMap) {
      const OutputSection *os = p.first;
      uint64_t secSize = 0;
      for (SectionCommand *cmd : os->commands) {
        if (auto *isd = dyn_cast<InputSectionDescription>(cmd))
          for (InputSection *isec : isd->sections) {
            uint64_t off = alignToPowerOf2(secSize, isec->addralign);
            secSize = off + isec->getSize();
          }
      }
      p.second.count = getMipsPageCount(secSize);
    }
  }

  // Merge GOTs. Try to join as much as possible GOTs but do not exceed
  // maximum GOT size. At first, try to fill the primary GOT because
  // the primary GOT can be accessed in the most effective way. If it
  // is not possible, try to fill the last GOT in the list, and finally
  // create a new GOT if both attempts failed.
  for (FileGot &srcGot : gots) {
    InputFile *file = srcGot.file;
    if (tryMergeGots(mergedGots.front(), srcGot, true)) {
      file->mipsGotIndex = 0;
    } else {
      // If this is the first time we failed to merge with the primary GOT,
      // MergedGots.back() will also be the primary GOT. We must make sure not
      // to try to merge again with isPrimary=false, as otherwise, if the
      // inputs are just right, we could allow the primary GOT to become 1 or 2
      // words bigger due to ignoring the header size.
      if (mergedGots.size() == 1 ||
          !tryMergeGots(mergedGots.back(), srcGot, false)) {
        mergedGots.emplace_back();
        std::swap(mergedGots.back(), srcGot);
      }
      file->mipsGotIndex = mergedGots.size() - 1;
    }
  }
  std::swap(gots, mergedGots);

  // Reduce number of "reloc-only" entries in the primary GOT
  // by subtracting "global" entries in the primary GOT.
  primGot = &gots.front();
  primGot->relocs.remove_if([&](const std::pair<Symbol *, size_t> &p) {
    return primGot->global.count(p.first);
  });

  // Calculate indexes for each GOT entry.
  size_t index = headerEntriesNum;
  for (FileGot &got : gots) {
    got.startIndex = &got == primGot ? 0 : index;
    for (std::pair<const OutputSection *, FileGot::PageBlock> &p :
         got.pagesMap) {
      // For each output section referenced by GOT page relocations calculate
      // and save into pagesMap an upper bound of MIPS GOT entries required
      // to store page addresses of local symbols. We assume the worst case -
      // each 64kb page of the output section has at least one GOT relocation
      // against it. And take in account the case when the section intersects
      // page boundaries.
      p.second.firstIndex = index;
      index += p.second.count;
    }
    for (auto &p: got.local16)
      p.second = index++;
    for (auto &p: got.global)
      p.second = index++;
    for (auto &p: got.relocs)
      p.second = index++;
    for (auto &p: got.tls)
      p.second = index++;
    for (auto &p: got.dynTlsSymbols) {
      p.second = index;
      index += 2;
    }
  }

  // Update SymbolAux::gotIdx field to use this
  // value later in the `sortMipsSymbols` function.
  for (auto &p : primGot->global) {
    if (p.first->auxIdx == 0)
      p.first->allocateAux();
    symAux.back().gotIdx = p.second;
  }
  for (auto &p : primGot->relocs) {
    if (p.first->auxIdx == 0)
      p.first->allocateAux();
    symAux.back().gotIdx = p.second;
  }

  // Create dynamic relocations.
  for (FileGot &got : gots) {
    // Create dynamic relocations for TLS entries.
    for (std::pair<Symbol *, size_t> &p : got.tls) {
      Symbol *s = p.first;
      uint64_t offset = p.second * config->wordsize;
      // When building a shared library we still need a dynamic relocation
      // for the TP-relative offset as we don't know how much other data will
      // be allocated before us in the static TLS block.
      if (s->isPreemptible || config->shared)
        mainPart->relaDyn->addReloc({target->tlsGotRel, this, offset,
                                     DynamicReloc::AgainstSymbolWithTargetVA,
                                     *s, 0, R_ABS});
    }
    for (std::pair<Symbol *, size_t> &p : got.dynTlsSymbols) {
      Symbol *s = p.first;
      uint64_t offset = p.second * config->wordsize;
      if (s == nullptr) {
        if (!config->shared)
          continue;
        mainPart->relaDyn->addReloc({target->tlsModuleIndexRel, this, offset});
      } else {
        // When building a shared library we still need a dynamic relocation
        // for the module index. Therefore only checking for
        // S->isPreemptible is not sufficient (this happens e.g. for
        // thread-locals that have been marked as local through a linker script)
        if (!s->isPreemptible && !config->shared)
          continue;
        mainPart->relaDyn->addSymbolReloc(target->tlsModuleIndexRel, *this,
                                          offset, *s);
        // However, we can skip writing the TLS offset reloc for non-preemptible
        // symbols since it is known even in shared libraries
        if (!s->isPreemptible)
          continue;
        offset += config->wordsize;
        mainPart->relaDyn->addSymbolReloc(target->tlsOffsetRel, *this, offset,
                                          *s);
      }
    }

    // Do not create dynamic relocations for non-TLS
    // entries in the primary GOT.
    if (&got == primGot)
      continue;

    // Dynamic relocations for "global" entries.
    for (const std::pair<Symbol *, size_t> &p : got.global) {
      uint64_t offset = p.second * config->wordsize;
      mainPart->relaDyn->addSymbolReloc(target->relativeRel, *this, offset,
                                        *p.first);
    }
    if (!config->isPic)
      continue;
    // Dynamic relocations for "local" entries in case of PIC.
    for (const std::pair<const OutputSection *, FileGot::PageBlock> &l :
         got.pagesMap) {
      size_t pageCount = l.second.count;
      for (size_t pi = 0; pi < pageCount; ++pi) {
        uint64_t offset = (l.second.firstIndex + pi) * config->wordsize;
        mainPart->relaDyn->addReloc({target->relativeRel, this, offset, l.first,
                                     int64_t(pi * 0x10000)});
      }
    }
    for (const std::pair<GotEntry, size_t> &p : got.local16) {
      uint64_t offset = p.second * config->wordsize;
      mainPart->relaDyn->addReloc({target->relativeRel, this, offset,
                                   DynamicReloc::AddendOnlyWithTargetVA,
                                   *p.first.first, p.first.second, R_ABS});
    }
  }
}

bool MipsGotSection::isNeeded() const {
  // We add the .got section to the result for dynamic MIPS target because
  // its address and properties are mentioned in the .dynamic section.
  return !config->relocatable;
}

uint64_t MipsGotSection::getGp(const InputFile *f) const {
  // For files without related GOT or files refer a primary GOT
  // returns "common" _gp value. For secondary GOTs calculate
  // individual _gp values.
  if (!f || f->mipsGotIndex == uint32_t(-1) || f->mipsGotIndex == 0)
    return ElfSym::mipsGp->getVA(0);
  return getVA() + gots[f->mipsGotIndex].startIndex * config->wordsize + 0x7ff0;
}

void MipsGotSection::writeTo(uint8_t *buf) {
  // Set the MSB of the second GOT slot. This is not required by any
  // MIPS ABI documentation, though.
  //
  // There is a comment in glibc saying that "The MSB of got[1] of a
  // gnu object is set to identify gnu objects," and in GNU gold it
  // says "the second entry will be used by some runtime loaders".
  // But how this field is being used is unclear.
  //
  // We are not really willing to mimic other linkers behaviors
  // without understanding why they do that, but because all files
  // generated by GNU tools have this special GOT value, and because
  // we've been doing this for years, it is probably a safe bet to
  // keep doing this for now. We really need to revisit this to see
  // if we had to do this.
  writeUint(buf + config->wordsize, (uint64_t)1 << (config->wordsize * 8 - 1));
  for (const FileGot &g : gots) {
    auto write = [&](size_t i, const Symbol *s, int64_t a) {
      uint64_t va = a;
      if (s)
        va = s->getVA(a);
      writeUint(buf + i * config->wordsize, va);
    };
    // Write 'page address' entries to the local part of the GOT.
    for (const std::pair<const OutputSection *, FileGot::PageBlock> &l :
         g.pagesMap) {
      size_t pageCount = l.second.count;
      uint64_t firstPageAddr = getMipsPageAddr(l.first->addr);
      for (size_t pi = 0; pi < pageCount; ++pi)
        write(l.second.firstIndex + pi, nullptr, firstPageAddr + pi * 0x10000);
    }
    // Local, global, TLS, reloc-only  entries.
    // If TLS entry has a corresponding dynamic relocations, leave it
    // initialized by zero. Write down adjusted TLS symbol's values otherwise.
    // To calculate the adjustments use offsets for thread-local storage.
    // http://web.archive.org/web/20190324223224/https://www.linux-mips.org/wiki/NPTL
    for (const std::pair<GotEntry, size_t> &p : g.local16)
      write(p.second, p.first.first, p.first.second);
    // Write VA to the primary GOT only. For secondary GOTs that
    // will be done by REL32 dynamic relocations.
    if (&g == &gots.front())
      for (const std::pair<Symbol *, size_t> &p : g.global)
        write(p.second, p.first, 0);
    for (const std::pair<Symbol *, size_t> &p : g.relocs)
      write(p.second, p.first, 0);
    for (const std::pair<Symbol *, size_t> &p : g.tls)
      write(p.second, p.first,
            p.first->isPreemptible || config->shared ? 0 : -0x7000);
    for (const std::pair<Symbol *, size_t> &p : g.dynTlsSymbols) {
      if (p.first == nullptr && !config->shared)
        write(p.second, nullptr, 1);
      else if (p.first && !p.first->isPreemptible) {
        // If we are emitting a shared library with relocations we mustn't write
        // anything to the GOT here. When using Elf_Rel relocations the value
        // one will be treated as an addend and will cause crashes at runtime
        if (!config->shared)
          write(p.second, nullptr, 1);
        write(p.second + 1, p.first, -0x8000);
      }
    }
  }
}

// On PowerPC the .plt section is used to hold the table of function addresses
// instead of the .got.plt, and the type is SHT_NOBITS similar to a .bss
// section. I don't know why we have a BSS style type for the section but it is
// consistent across both 64-bit PowerPC ABIs as well as the 32-bit PowerPC ABI.
GotPltSection::GotPltSection()
    : SyntheticSection(SHF_ALLOC | SHF_WRITE, SHT_PROGBITS, config->wordsize,
                       ".got.plt") {
  if (config->emachine == EM_PPC) {
    name = ".plt";
  } else if (config->emachine == EM_PPC64) {
    type = SHT_NOBITS;
    name = ".plt";
  }
}

void GotPltSection::addEntry(Symbol &sym) {
  assert(sym.auxIdx == symAux.size() - 1 &&
         symAux.back().pltIdx == entries.size());
  entries.push_back(&sym);
}

size_t GotPltSection::getSize() const {
  return (target->gotPltHeaderEntriesNum + entries.size()) *
         target->gotEntrySize;
}

void GotPltSection::writeTo(uint8_t *buf) {
  target->writeGotPltHeader(buf);
  buf += target->gotPltHeaderEntriesNum * target->gotEntrySize;
  for (const Symbol *b : entries) {
    target->writeGotPlt(buf, *b);
    buf += target->gotEntrySize;
  }
}

bool GotPltSection::isNeeded() const {
  // We need to emit GOTPLT even if it's empty if there's a relocation relative
  // to it.
  return !entries.empty() || hasGotPltOffRel;
}

static StringRef getIgotPltName() {
  // On ARM the IgotPltSection is part of the GotSection.
  if (config->emachine == EM_ARM)
    return ".got";

  // On PowerPC64 the GotPltSection is renamed to '.plt' so the IgotPltSection
  // needs to be named the same.
  if (config->emachine == EM_PPC64)
    return ".plt";

  return ".got.plt";
}

// On PowerPC64 the GotPltSection type is SHT_NOBITS so we have to follow suit
// with the IgotPltSection.
IgotPltSection::IgotPltSection()
    : SyntheticSection(SHF_ALLOC | SHF_WRITE,
                       config->emachine == EM_PPC64 ? SHT_NOBITS : SHT_PROGBITS,
                       target->gotEntrySize, getIgotPltName()) {}

void IgotPltSection::addEntry(Symbol &sym) {
  assert(symAux.back().pltIdx == entries.size());
  entries.push_back(&sym);
}

size_t IgotPltSection::getSize() const {
  return entries.size() * target->gotEntrySize;
}

void IgotPltSection::writeTo(uint8_t *buf) {
  for (const Symbol *b : entries) {
    target->writeIgotPlt(buf, *b);
    buf += target->gotEntrySize;
  }
}

StringTableSection::StringTableSection(StringRef name, bool dynamic)
    : SyntheticSection(dynamic ? (uint64_t)SHF_ALLOC : 0, SHT_STRTAB, 1, name),
      dynamic(dynamic) {
  // ELF string tables start with a NUL byte.
  strings.push_back("");
  stringMap.try_emplace(CachedHashStringRef(""), 0);
  size = 1;
}

// Adds a string to the string table. If `hashIt` is true we hash and check for
// duplicates. It is optional because the name of global symbols are already
// uniqued and hashing them again has a big cost for a small value: uniquing
// them with some other string that happens to be the same.
unsigned StringTableSection::addString(StringRef s, bool hashIt) {
  if (hashIt) {
    auto r = stringMap.try_emplace(CachedHashStringRef(s), size);
    if (!r.second)
      return r.first->second;
  }
  if (s.empty())
    return 0;
  unsigned ret = this->size;
  this->size = this->size + s.size() + 1;
  strings.push_back(s);
  return ret;
}

void StringTableSection::writeTo(uint8_t *buf) {
  for (StringRef s : strings) {
    memcpy(buf, s.data(), s.size());
    buf[s.size()] = '\0';
    buf += s.size() + 1;
  }
}

// Returns the number of entries in .gnu.version_d: the number of
// non-VER_NDX_LOCAL-non-VER_NDX_GLOBAL definitions, plus 1.
// Note that we don't support vd_cnt > 1 yet.
static unsigned getVerDefNum() {
  return namedVersionDefs().size() + 1;
}

template <class ELFT>
DynamicSection<ELFT>::DynamicSection()
    : SyntheticSection(SHF_ALLOC | SHF_WRITE, SHT_DYNAMIC, config->wordsize,
                       ".dynamic") {
  this->entsize = ELFT::Is64Bits ? 16 : 8;

  // .dynamic section is not writable on MIPS and on Fuchsia OS
  // which passes -z rodynamic.
  // See "Special Section" in Chapter 4 in the following document:
  // ftp://www.linux-mips.org/pub/linux/mips/doc/ABI/mipsabi.pdf
  if (config->emachine == EM_MIPS || config->zRodynamic)
    this->flags = SHF_ALLOC;
}

// The output section .rela.dyn may include these synthetic sections:
//
// - part.relaDyn
// - in.relaPlt: this is included if a linker script places .rela.plt inside
//   .rela.dyn
//
// DT_RELASZ is the total size of the included sections.
static uint64_t addRelaSz(const RelocationBaseSection &relaDyn) {
  size_t size = relaDyn.getSize();
  if (in.relaPlt->getParent() == relaDyn.getParent())
    size += in.relaPlt->getSize();
  return size;
}

// A Linker script may assign the RELA relocation sections to the same
// output section. When this occurs we cannot just use the OutputSection
// Size. Moreover the [DT_JMPREL, DT_JMPREL + DT_PLTRELSZ) is permitted to
// overlap with the [DT_RELA, DT_RELA + DT_RELASZ).
static uint64_t addPltRelSz() { return in.relaPlt->getSize(); }

// Add remaining entries to complete .dynamic contents.
template <class ELFT>
std::vector<std::pair<int32_t, uint64_t>>
DynamicSection<ELFT>::computeContents() {
  elf::Partition &part = getPartition();
  bool isMain = part.name.empty();
  std::vector<std::pair<int32_t, uint64_t>> entries;

  auto addInt = [&](int32_t tag, uint64_t val) {
    entries.emplace_back(tag, val);
  };
  auto addInSec = [&](int32_t tag, const InputSection &sec) {
    entries.emplace_back(tag, sec.getVA());
  };

  for (StringRef s : config->filterList)
    addInt(DT_FILTER, part.dynStrTab->addString(s));
  for (StringRef s : config->auxiliaryList)
    addInt(DT_AUXILIARY, part.dynStrTab->addString(s));

  if (!config->rpath.empty())
    addInt(config->enableNewDtags ? DT_RUNPATH : DT_RPATH,
           part.dynStrTab->addString(config->rpath));

  for (SharedFile *file : ctx.sharedFiles)
    if (file->isNeeded)
      addInt(DT_NEEDED, part.dynStrTab->addString(file->soName));

  if (isMain) {
    if (!config->soName.empty())
      addInt(DT_SONAME, part.dynStrTab->addString(config->soName));
  } else {
    if (!config->soName.empty())
      addInt(DT_NEEDED, part.dynStrTab->addString(config->soName));
    addInt(DT_SONAME, part.dynStrTab->addString(part.name));
  }

  // Set DT_FLAGS and DT_FLAGS_1.
  uint32_t dtFlags = 0;
  uint32_t dtFlags1 = 0;
  if (config->bsymbolic == BsymbolicKind::All)
    dtFlags |= DF_SYMBOLIC;
  if (config->zGlobal)
    dtFlags1 |= DF_1_GLOBAL;
  if (config->zInitfirst)
    dtFlags1 |= DF_1_INITFIRST;
  if (config->zInterpose)
    dtFlags1 |= DF_1_INTERPOSE;
  if (config->zNodefaultlib)
    dtFlags1 |= DF_1_NODEFLIB;
  if (config->zNodelete)
    dtFlags1 |= DF_1_NODELETE;
  if (config->zNodlopen)
    dtFlags1 |= DF_1_NOOPEN;
  if (config->pie)
    dtFlags1 |= DF_1_PIE;
  if (config->zNow) {
    dtFlags |= DF_BIND_NOW;
    dtFlags1 |= DF_1_NOW;
  }
  if (config->zOrigin) {
    dtFlags |= DF_ORIGIN;
    dtFlags1 |= DF_1_ORIGIN;
  }
  if (!config->zText)
    dtFlags |= DF_TEXTREL;
  if (ctx.hasTlsIe && config->shared)
    dtFlags |= DF_STATIC_TLS;

  if (dtFlags)
    addInt(DT_FLAGS, dtFlags);
  if (dtFlags1)
    addInt(DT_FLAGS_1, dtFlags1);

  // DT_DEBUG is a pointer to debug information used by debuggers at runtime. We
  // need it for each process, so we don't write it for DSOs. The loader writes
  // the pointer into this entry.
  //
  // DT_DEBUG is the only .dynamic entry that needs to be written to. Some
  // systems (currently only Fuchsia OS) provide other means to give the
  // debugger this information. Such systems may choose make .dynamic read-only.
  // If the target is such a system (used -z rodynamic) don't write DT_DEBUG.
  if (!config->shared && !config->relocatable && !config->zRodynamic)
    addInt(DT_DEBUG, 0);

  if (part.relaDyn->isNeeded()) {
    addInSec(part.relaDyn->dynamicTag, *part.relaDyn);
    entries.emplace_back(part.relaDyn->sizeDynamicTag,
                         addRelaSz(*part.relaDyn));

    bool isRela = config->isRela;
    addInt(isRela ? DT_RELAENT : DT_RELENT,
           isRela ? sizeof(Elf_Rela) : sizeof(Elf_Rel));

    // MIPS dynamic loader does not support RELCOUNT tag.
    // The problem is in the tight relation between dynamic
    // relocations and GOT. So do not emit this tag on MIPS.
    if (config->emachine != EM_MIPS) {
      size_t numRelativeRels = part.relaDyn->getRelativeRelocCount();
      if (config->zCombreloc && numRelativeRels)
        addInt(isRela ? DT_RELACOUNT : DT_RELCOUNT, numRelativeRels);
    }
  }
  if (part.relrDyn && part.relrDyn->getParent() &&
      !part.relrDyn->relocs.empty()) {
    addInSec(config->useAndroidRelrTags ? DT_ANDROID_RELR : DT_RELR,
             *part.relrDyn);
    addInt(config->useAndroidRelrTags ? DT_ANDROID_RELRSZ : DT_RELRSZ,
           part.relrDyn->getParent()->size);
    addInt(config->useAndroidRelrTags ? DT_ANDROID_RELRENT : DT_RELRENT,
           sizeof(Elf_Relr));
  }
  if (part.relrAuthDyn && part.relrAuthDyn->getParent() &&
      !part.relrAuthDyn->relocs.empty()) {
    addInSec(DT_AARCH64_AUTH_RELR, *part.relrAuthDyn);
    addInt(DT_AARCH64_AUTH_RELRSZ, part.relrAuthDyn->getParent()->size);
    addInt(DT_AARCH64_AUTH_RELRENT, sizeof(Elf_Relr));
  }
  if (isMain && in.relaPlt->isNeeded()) {
    addInSec(DT_JMPREL, *in.relaPlt);
    entries.emplace_back(DT_PLTRELSZ, addPltRelSz());
    switch (config->emachine) {
    case EM_MIPS:
      addInSec(DT_MIPS_PLTGOT, *in.gotPlt);
      break;
    case EM_S390:
      addInSec(DT_PLTGOT, *in.got);
      break;
    case EM_SPARCV9:
      addInSec(DT_PLTGOT, *in.plt);
      break;
    case EM_AARCH64:
      if (llvm::find_if(in.relaPlt->relocs, [](const DynamicReloc &r) {
           return r.type == target->pltRel &&
                  r.sym->stOther & STO_AARCH64_VARIANT_PCS;
          }) != in.relaPlt->relocs.end())
        addInt(DT_AARCH64_VARIANT_PCS, 0);
      addInSec(DT_PLTGOT, *in.gotPlt);
      break;
    case EM_RISCV:
      if (llvm::any_of(in.relaPlt->relocs, [](const DynamicReloc &r) {
            return r.type == target->pltRel &&
                   (r.sym->stOther & STO_RISCV_VARIANT_CC);
          }))
        addInt(DT_RISCV_VARIANT_CC, 0);
      [[fallthrough]];
    default:
      addInSec(DT_PLTGOT, *in.gotPlt);
      break;
    }
    addInt(DT_PLTREL, config->isRela ? DT_RELA : DT_REL);
  }

  if (config->emachine == EM_AARCH64) {
    if (config->andFeatures & GNU_PROPERTY_AARCH64_FEATURE_1_BTI)
      addInt(DT_AARCH64_BTI_PLT, 0);
    if (config->zPacPlt)
      addInt(DT_AARCH64_PAC_PLT, 0);

    if (hasMemtag()) {
      addInt(DT_AARCH64_MEMTAG_MODE, config->androidMemtagMode == NT_MEMTAG_LEVEL_ASYNC);
      addInt(DT_AARCH64_MEMTAG_HEAP, config->androidMemtagHeap);
      addInt(DT_AARCH64_MEMTAG_STACK, config->androidMemtagStack);
      if (mainPart->memtagGlobalDescriptors->isNeeded()) {
        addInSec(DT_AARCH64_MEMTAG_GLOBALS, *mainPart->memtagGlobalDescriptors);
        addInt(DT_AARCH64_MEMTAG_GLOBALSSZ,
               mainPart->memtagGlobalDescriptors->getSize());
      }
    }
  }

  addInSec(DT_SYMTAB, *part.dynSymTab);
  addInt(DT_SYMENT, sizeof(Elf_Sym));
  addInSec(DT_STRTAB, *part.dynStrTab);
  addInt(DT_STRSZ, part.dynStrTab->getSize());
  if (!config->zText)
    addInt(DT_TEXTREL, 0);
  if (part.gnuHashTab && part.gnuHashTab->getParent())
    addInSec(DT_GNU_HASH, *part.gnuHashTab);
  if (part.hashTab && part.hashTab->getParent())
    addInSec(DT_HASH, *part.hashTab);

  if (isMain) {
    if (Out::preinitArray) {
      addInt(DT_PREINIT_ARRAY, Out::preinitArray->addr);
      addInt(DT_PREINIT_ARRAYSZ, Out::preinitArray->size);
    }
    if (Out::initArray) {
      addInt(DT_INIT_ARRAY, Out::initArray->addr);
      addInt(DT_INIT_ARRAYSZ, Out::initArray->size);
    }
    if (Out::finiArray) {
      addInt(DT_FINI_ARRAY, Out::finiArray->addr);
      addInt(DT_FINI_ARRAYSZ, Out::finiArray->size);
    }

    if (Symbol *b = symtab.find(config->init))
      if (b->isDefined())
        addInt(DT_INIT, b->getVA());
    if (Symbol *b = symtab.find(config->fini))
      if (b->isDefined())
        addInt(DT_FINI, b->getVA());
  }

  if (part.verSym && part.verSym->isNeeded())
    addInSec(DT_VERSYM, *part.verSym);
  if (part.verDef && part.verDef->isLive()) {
    addInSec(DT_VERDEF, *part.verDef);
    addInt(DT_VERDEFNUM, getVerDefNum());
  }
  if (part.verNeed && part.verNeed->isNeeded()) {
    addInSec(DT_VERNEED, *part.verNeed);
    unsigned needNum = 0;
    for (SharedFile *f : ctx.sharedFiles)
      if (!f->vernauxs.empty())
        ++needNum;
    addInt(DT_VERNEEDNUM, needNum);
  }

  if (config->emachine == EM_MIPS) {
    addInt(DT_MIPS_RLD_VERSION, 1);
    addInt(DT_MIPS_FLAGS, RHF_NOTPOT);
    addInt(DT_MIPS_BASE_ADDRESS, target->getImageBase());
    addInt(DT_MIPS_SYMTABNO, part.dynSymTab->getNumSymbols());
    addInt(DT_MIPS_LOCAL_GOTNO, in.mipsGot->getLocalEntriesNum());

    if (const Symbol *b = in.mipsGot->getFirstGlobalEntry())
      addInt(DT_MIPS_GOTSYM, b->dynsymIndex);
    else
      addInt(DT_MIPS_GOTSYM, part.dynSymTab->getNumSymbols());
    addInSec(DT_PLTGOT, *in.mipsGot);
    if (in.mipsRldMap) {
      if (!config->pie)
        addInSec(DT_MIPS_RLD_MAP, *in.mipsRldMap);
      // Store the offset to the .rld_map section
      // relative to the address of the tag.
      addInt(DT_MIPS_RLD_MAP_REL,
             in.mipsRldMap->getVA() - (getVA() + entries.size() * entsize));
    }
  }

  // DT_PPC_GOT indicates to glibc Secure PLT is used. If DT_PPC_GOT is absent,
  // glibc assumes the old-style BSS PLT layout which we don't support.
  if (config->emachine == EM_PPC)
    addInSec(DT_PPC_GOT, *in.got);

  // Glink dynamic tag is required by the V2 abi if the plt section isn't empty.
  if (config->emachine == EM_PPC64 && in.plt->isNeeded()) {
    // The Glink tag points to 32 bytes before the first lazy symbol resolution
    // stub, which starts directly after the header.
    addInt(DT_PPC64_GLINK, in.plt->getVA() + target->pltHeaderSize - 32);
  }

  if (config->emachine == EM_PPC64)
    addInt(DT_PPC64_OPT, getPPC64TargetInfo()->ppc64DynamicSectionOpt);

  addInt(DT_NULL, 0);
  return entries;
}

template <class ELFT> void DynamicSection<ELFT>::finalizeContents() {
  if (OutputSection *sec = getPartition().dynStrTab->getParent())
    getParent()->link = sec->sectionIndex;
  this->size = computeContents().size() * this->entsize;
}

template <class ELFT> void DynamicSection<ELFT>::writeTo(uint8_t *buf) {
  auto *p = reinterpret_cast<Elf_Dyn *>(buf);

  for (std::pair<int32_t, uint64_t> kv : computeContents()) {
    p->d_tag = kv.first;
    p->d_un.d_val = kv.second;
    ++p;
  }
}

uint64_t DynamicReloc::getOffset() const {
  return inputSec->getVA(offsetInSec);
}

int64_t DynamicReloc::computeAddend() const {
  switch (kind) {
  case AddendOnly:
    assert(sym == nullptr);
    return addend;
  case AgainstSymbol:
    assert(sym != nullptr);
    return addend;
  case AddendOnlyWithTargetVA:
  case AgainstSymbolWithTargetVA: {
    uint64_t ca = InputSection::getRelocTargetVA(inputSec->file, type, addend,
                                                 getOffset(), *sym, expr);
    return config->is64 ? ca : SignExtend64<32>(ca);
  }
  case MipsMultiGotPage:
    assert(sym == nullptr);
    return getMipsPageAddr(outputSec->addr) + addend;
  }
  llvm_unreachable("Unknown DynamicReloc::Kind enum");
}

uint32_t DynamicReloc::getSymIndex(SymbolTableBaseSection *symTab) const {
  if (!needsDynSymIndex())
    return 0;

  size_t index = symTab->getSymbolIndex(*sym);
  assert((index != 0 || (type != target->gotRel && type != target->pltRel) ||
          !mainPart->dynSymTab->getParent()) &&
         "GOT or PLT relocation must refer to symbol in dynamic symbol table");
  return index;
}

RelocationBaseSection::RelocationBaseSection(StringRef name, uint32_t type,
                                             int32_t dynamicTag,
                                             int32_t sizeDynamicTag,
                                             bool combreloc,
                                             unsigned concurrency)
    : SyntheticSection(SHF_ALLOC, type, config->wordsize, name),
      dynamicTag(dynamicTag), sizeDynamicTag(sizeDynamicTag),
      relocsVec(concurrency), combreloc(combreloc) {}

void RelocationBaseSection::addSymbolReloc(
    RelType dynType, InputSectionBase &isec, uint64_t offsetInSec, Symbol &sym,
    int64_t addend, std::optional<RelType> addendRelType) {
  addReloc(DynamicReloc::AgainstSymbol, dynType, isec, offsetInSec, sym, addend,
           R_ADDEND, addendRelType ? *addendRelType : target->noneRel);
}

void RelocationBaseSection::addAddendOnlyRelocIfNonPreemptible(
    RelType dynType, GotSection &sec, uint64_t offsetInSec, Symbol &sym,
    RelType addendRelType) {
  // No need to write an addend to the section for preemptible symbols.
  if (sym.isPreemptible)
    addReloc({dynType, &sec, offsetInSec, DynamicReloc::AgainstSymbol, sym, 0,
              R_ABS});
  else
    addReloc(DynamicReloc::AddendOnlyWithTargetVA, dynType, sec, offsetInSec,
             sym, 0, R_ABS, addendRelType);
}

void RelocationBaseSection::mergeRels() {
  size_t newSize = relocs.size();
  for (const auto &v : relocsVec)
    newSize += v.size();
  relocs.reserve(newSize);
  for (const auto &v : relocsVec)
    llvm::append_range(relocs, v);
  relocsVec.clear();
}

void RelocationBaseSection::partitionRels() {
  if (!combreloc)
    return;
  const RelType relativeRel = target->relativeRel;
  numRelativeRelocs =
      std::stable_partition(relocs.begin(), relocs.end(),
                            [=](auto &r) { return r.type == relativeRel; }) -
      relocs.begin();
}

void RelocationBaseSection::finalizeContents() {
  SymbolTableBaseSection *symTab = getPartition().dynSymTab.get();

  // When linking glibc statically, .rel{,a}.plt contains R_*_IRELATIVE
  // relocations due to IFUNC (e.g. strcpy). sh_link will be set to 0 in that
  // case.
  if (symTab && symTab->getParent())
    getParent()->link = symTab->getParent()->sectionIndex;
  else
    getParent()->link = 0;

  if (in.relaPlt.get() == this && in.gotPlt->getParent()) {
    getParent()->flags |= ELF::SHF_INFO_LINK;
    getParent()->info = in.gotPlt->getParent()->sectionIndex;
  }
}

void DynamicReloc::computeRaw(SymbolTableBaseSection *symtab) {
  r_offset = getOffset();
  r_sym = getSymIndex(symtab);
  addend = computeAddend();
  kind = AddendOnly; // Catch errors
}

void RelocationBaseSection::computeRels() {
  SymbolTableBaseSection *symTab = getPartition().dynSymTab.get();
  parallelForEach(relocs,
                  [symTab](DynamicReloc &rel) { rel.computeRaw(symTab); });

  auto irelative = std::stable_partition(
      relocs.begin() + numRelativeRelocs, relocs.end(),
      [t = target->iRelativeRel](auto &r) { return r.type != t; });

  // Sort by (!IsRelative,SymIndex,r_offset). DT_REL[A]COUNT requires us to
  // place R_*_RELATIVE first. SymIndex is to improve locality, while r_offset
  // is to make results easier to read.
  if (combreloc) {
    auto nonRelative = relocs.begin() + numRelativeRelocs;
    parallelSort(relocs.begin(), nonRelative,
                 [&](auto &a, auto &b) { return a.r_offset < b.r_offset; });
    // Non-relative relocations are few, so don't bother with parallelSort.
    llvm::sort(nonRelative, irelative, [&](auto &a, auto &b) {
      return std::tie(a.r_sym, a.r_offset) < std::tie(b.r_sym, b.r_offset);
    });
  }
}

template <class ELFT>
RelocationSection<ELFT>::RelocationSection(StringRef name, bool combreloc,
                                           unsigned concurrency)
    : RelocationBaseSection(name, config->isRela ? SHT_RELA : SHT_REL,
                            config->isRela ? DT_RELA : DT_REL,
                            config->isRela ? DT_RELASZ : DT_RELSZ, combreloc,
                            concurrency) {
  this->entsize = config->isRela ? sizeof(Elf_Rela) : sizeof(Elf_Rel);
}

template <class ELFT> void RelocationSection<ELFT>::writeTo(uint8_t *buf) {
  computeRels();
  for (const DynamicReloc &rel : relocs) {
    auto *p = reinterpret_cast<Elf_Rela *>(buf);
    p->r_offset = rel.r_offset;
    p->setSymbolAndType(rel.r_sym, rel.type, config->isMips64EL);
    if (config->isRela)
      p->r_addend = rel.addend;
    buf += config->isRela ? sizeof(Elf_Rela) : sizeof(Elf_Rel);
  }
}

RelrBaseSection::RelrBaseSection(unsigned concurrency, bool isAArch64Auth)
    : SyntheticSection(
          SHF_ALLOC,
          isAArch64Auth
              ? SHT_AARCH64_AUTH_RELR
              : (config->useAndroidRelrTags ? SHT_ANDROID_RELR : SHT_RELR),
          config->wordsize, isAArch64Auth ? ".relr.auth.dyn" : ".relr.dyn"),
      relocsVec(concurrency) {}

void RelrBaseSection::mergeRels() {
  size_t newSize = relocs.size();
  for (const auto &v : relocsVec)
    newSize += v.size();
  relocs.reserve(newSize);
  for (const auto &v : relocsVec)
    llvm::append_range(relocs, v);
  relocsVec.clear();
}

template <class ELFT>
AndroidPackedRelocationSection<ELFT>::AndroidPackedRelocationSection(
    StringRef name, unsigned concurrency)
    : RelocationBaseSection(
          name, config->isRela ? SHT_ANDROID_RELA : SHT_ANDROID_REL,
          config->isRela ? DT_ANDROID_RELA : DT_ANDROID_REL,
          config->isRela ? DT_ANDROID_RELASZ : DT_ANDROID_RELSZ,
          /*combreloc=*/false, concurrency) {
  this->entsize = 1;
}

template <class ELFT>
bool AndroidPackedRelocationSection<ELFT>::updateAllocSize() {
  // This function computes the contents of an Android-format packed relocation
  // section.
  //
  // This format compresses relocations by using relocation groups to factor out
  // fields that are common between relocations and storing deltas from previous
  // relocations in SLEB128 format (which has a short representation for small
  // numbers). A good example of a relocation type with common fields is
  // R_*_RELATIVE, which is normally used to represent function pointers in
  // vtables. In the REL format, each relative relocation has the same r_info
  // field, and is only different from other relative relocations in terms of
  // the r_offset field. By sorting relocations by offset, grouping them by
  // r_info and representing each relocation with only the delta from the
  // previous offset, each 8-byte relocation can be compressed to as little as 1
  // byte (or less with run-length encoding). This relocation packer was able to
  // reduce the size of the relocation section in an Android Chromium DSO from
  // 2,911,184 bytes to 174,693 bytes, or 6% of the original size.
  //
  // A relocation section consists of a header containing the literal bytes
  // 'APS2' followed by a sequence of SLEB128-encoded integers. The first two
  // elements are the total number of relocations in the section and an initial
  // r_offset value. The remaining elements define a sequence of relocation
  // groups. Each relocation group starts with a header consisting of the
  // following elements:
  //
  // - the number of relocations in the relocation group
  // - flags for the relocation group
  // - (if RELOCATION_GROUPED_BY_OFFSET_DELTA_FLAG is set) the r_offset delta
  //   for each relocation in the group.
  // - (if RELOCATION_GROUPED_BY_INFO_FLAG is set) the value of the r_info
  //   field for each relocation in the group.
  // - (if RELOCATION_GROUP_HAS_ADDEND_FLAG and
  //   RELOCATION_GROUPED_BY_ADDEND_FLAG are set) the r_addend delta for
  //   each relocation in the group.
  //
  // Following the relocation group header are descriptions of each of the
  // relocations in the group. They consist of the following elements:
  //
  // - (if RELOCATION_GROUPED_BY_OFFSET_DELTA_FLAG is not set) the r_offset
  //   delta for this relocation.
  // - (if RELOCATION_GROUPED_BY_INFO_FLAG is not set) the value of the r_info
  //   field for this relocation.
  // - (if RELOCATION_GROUP_HAS_ADDEND_FLAG is set and
  //   RELOCATION_GROUPED_BY_ADDEND_FLAG is not set) the r_addend delta for
  //   this relocation.

  size_t oldSize = relocData.size();

  relocData = {'A', 'P', 'S', '2'};
  raw_svector_ostream os(relocData);
  auto add = [&](int64_t v) { encodeSLEB128(v, os); };

  // The format header includes the number of relocations and the initial
  // offset (we set this to zero because the first relocation group will
  // perform the initial adjustment).
  add(relocs.size());
  add(0);

  std::vector<Elf_Rela> relatives, nonRelatives;

  for (const DynamicReloc &rel : relocs) {
    Elf_Rela r;
    r.r_offset = rel.getOffset();
    r.setSymbolAndType(rel.getSymIndex(getPartition().dynSymTab.get()),
                       rel.type, false);
    r.r_addend = config->isRela ? rel.computeAddend() : 0;

    if (r.getType(config->isMips64EL) == target->relativeRel)
      relatives.push_back(r);
    else
      nonRelatives.push_back(r);
  }

  llvm::sort(relatives, [](const Elf_Rel &a, const Elf_Rel &b) {
    return a.r_offset < b.r_offset;
  });

  // Try to find groups of relative relocations which are spaced one word
  // apart from one another. These generally correspond to vtable entries. The
  // format allows these groups to be encoded using a sort of run-length
  // encoding, but each group will cost 7 bytes in addition to the offset from
  // the previous group, so it is only profitable to do this for groups of
  // size 8 or larger.
  std::vector<Elf_Rela> ungroupedRelatives;
  std::vector<std::vector<Elf_Rela>> relativeGroups;
  for (auto i = relatives.begin(), e = relatives.end(); i != e;) {
    std::vector<Elf_Rela> group;
    do {
      group.push_back(*i++);
    } while (i != e && (i - 1)->r_offset + config->wordsize == i->r_offset);

    if (group.size() < 8)
      ungroupedRelatives.insert(ungroupedRelatives.end(), group.begin(),
                                group.end());
    else
      relativeGroups.emplace_back(std::move(group));
  }

  // For non-relative relocations, we would like to:
  //   1. Have relocations with the same symbol offset to be consecutive, so
  //      that the runtime linker can speed-up symbol lookup by implementing an
  //      1-entry cache.
  //   2. Group relocations by r_info to reduce the size of the relocation
  //      section.
  // Since the symbol offset is the high bits in r_info, sorting by r_info
  // allows us to do both.
  //
  // For Rela, we also want to sort by r_addend when r_info is the same. This
  // enables us to group by r_addend as well.
  llvm::sort(nonRelatives, [](const Elf_Rela &a, const Elf_Rela &b) {
    if (a.r_info != b.r_info)
      return a.r_info < b.r_info;
    if (a.r_addend != b.r_addend)
      return a.r_addend < b.r_addend;
    return a.r_offset < b.r_offset;
  });

  // Group relocations with the same r_info. Note that each group emits a group
  // header and that may make the relocation section larger. It is hard to
  // estimate the size of a group header as the encoded size of that varies
  // based on r_info. However, we can approximate this trade-off by the number
  // of values encoded. Each group header contains 3 values, and each relocation
  // in a group encodes one less value, as compared to when it is not grouped.
  // Therefore, we only group relocations if there are 3 or more of them with
  // the same r_info.
  //
  // For Rela, the addend for most non-relative relocations is zero, and thus we
  // can usually get a smaller relocation section if we group relocations with 0
  // addend as well.
  std::vector<Elf_Rela> ungroupedNonRelatives;
  std::vector<std::vector<Elf_Rela>> nonRelativeGroups;
  for (auto i = nonRelatives.begin(), e = nonRelatives.end(); i != e;) {
    auto j = i + 1;
    while (j != e && i->r_info == j->r_info &&
           (!config->isRela || i->r_addend == j->r_addend))
      ++j;
    if (j - i < 3 || (config->isRela && i->r_addend != 0))
      ungroupedNonRelatives.insert(ungroupedNonRelatives.end(), i, j);
    else
      nonRelativeGroups.emplace_back(i, j);
    i = j;
  }

  // Sort ungrouped relocations by offset to minimize the encoded length.
  llvm::sort(ungroupedNonRelatives, [](const Elf_Rela &a, const Elf_Rela &b) {
    return a.r_offset < b.r_offset;
  });

  unsigned hasAddendIfRela =
      config->isRela ? RELOCATION_GROUP_HAS_ADDEND_FLAG : 0;

  uint64_t offset = 0;
  uint64_t addend = 0;

  // Emit the run-length encoding for the groups of adjacent relative
  // relocations. Each group is represented using two groups in the packed
  // format. The first is used to set the current offset to the start of the
  // group (and also encodes the first relocation), and the second encodes the
  // remaining relocations.
  for (std::vector<Elf_Rela> &g : relativeGroups) {
    // The first relocation in the group.
    add(1);
    add(RELOCATION_GROUPED_BY_OFFSET_DELTA_FLAG |
        RELOCATION_GROUPED_BY_INFO_FLAG | hasAddendIfRela);
    add(g[0].r_offset - offset);
    add(target->relativeRel);
    if (config->isRela) {
      add(g[0].r_addend - addend);
      addend = g[0].r_addend;
    }

    // The remaining relocations.
    add(g.size() - 1);
    add(RELOCATION_GROUPED_BY_OFFSET_DELTA_FLAG |
        RELOCATION_GROUPED_BY_INFO_FLAG | hasAddendIfRela);
    add(config->wordsize);
    add(target->relativeRel);
    if (config->isRela) {
      for (const auto &i : llvm::drop_begin(g)) {
        add(i.r_addend - addend);
        addend = i.r_addend;
      }
    }

    offset = g.back().r_offset;
  }

  // Now the ungrouped relatives.
  if (!ungroupedRelatives.empty()) {
    add(ungroupedRelatives.size());
    add(RELOCATION_GROUPED_BY_INFO_FLAG | hasAddendIfRela);
    add(target->relativeRel);
    for (Elf_Rela &r : ungroupedRelatives) {
      add(r.r_offset - offset);
      offset = r.r_offset;
      if (config->isRela) {
        add(r.r_addend - addend);
        addend = r.r_addend;
      }
    }
  }

  // Grouped non-relatives.
  for (ArrayRef<Elf_Rela> g : nonRelativeGroups) {
    add(g.size());
    add(RELOCATION_GROUPED_BY_INFO_FLAG);
    add(g[0].r_info);
    for (const Elf_Rela &r : g) {
      add(r.r_offset - offset);
      offset = r.r_offset;
    }
    addend = 0;
  }

  // Finally the ungrouped non-relative relocations.
  if (!ungroupedNonRelatives.empty()) {
    add(ungroupedNonRelatives.size());
    add(hasAddendIfRela);
    for (Elf_Rela &r : ungroupedNonRelatives) {
      add(r.r_offset - offset);
      offset = r.r_offset;
      add(r.r_info);
      if (config->isRela) {
        add(r.r_addend - addend);
        addend = r.r_addend;
      }
    }
  }

  // Don't allow the section to shrink; otherwise the size of the section can
  // oscillate infinitely.
  if (relocData.size() < oldSize)
    relocData.append(oldSize - relocData.size(), 0);

  // Returns whether the section size changed. We need to keep recomputing both
  // section layout and the contents of this section until the size converges
  // because changing this section's size can affect section layout, which in
  // turn can affect the sizes of the LEB-encoded integers stored in this
  // section.
  return relocData.size() != oldSize;
}

template <class ELFT>
RelrSection<ELFT>::RelrSection(unsigned concurrency, bool isAArch64Auth)
    : RelrBaseSection(concurrency, isAArch64Auth) {
  this->entsize = config->wordsize;
}

template <class ELFT> bool RelrSection<ELFT>::updateAllocSize() {
  // This function computes the contents of an SHT_RELR packed relocation
  // section.
  //
  // Proposal for adding SHT_RELR sections to generic-abi is here:
  //   https://groups.google.com/forum/#!topic/generic-abi/bX460iggiKg
  //
  // The encoded sequence of Elf64_Relr entries in a SHT_RELR section looks
  // like [ AAAAAAAA BBBBBBB1 BBBBBBB1 ... AAAAAAAA BBBBBB1 ... ]
  //
  // i.e. start with an address, followed by any number of bitmaps. The address
  // entry encodes 1 relocation. The subsequent bitmap entries encode up to 63
  // relocations each, at subsequent offsets following the last address entry.
  //
  // The bitmap entries must have 1 in the least significant bit. The assumption
  // here is that an address cannot have 1 in lsb. Odd addresses are not
  // supported.
  //
  // Excluding the least significant bit in the bitmap, each non-zero bit in
  // the bitmap represents a relocation to be applied to a corresponding machine
  // word that follows the base address word. The second least significant bit
  // represents the machine word immediately following the initial address, and
  // each bit that follows represents the next word, in linear order. As such,
  // a single bitmap can encode up to 31 relocations in a 32-bit object, and
  // 63 relocations in a 64-bit object.
  //
  // This encoding has a couple of interesting properties:
  // 1. Looking at any entry, it is clear whether it's an address or a bitmap:
  //    even means address, odd means bitmap.
  // 2. Just a simple list of addresses is a valid encoding.

  size_t oldSize = relrRelocs.size();
  relrRelocs.clear();

  // Same as Config->Wordsize but faster because this is a compile-time
  // constant.
  const size_t wordsize = sizeof(typename ELFT::uint);

  // Number of bits to use for the relocation offsets bitmap.
  // Must be either 63 or 31.
  const size_t nBits = wordsize * 8 - 1;

  // Get offsets for all relative relocations and sort them.
  std::unique_ptr<uint64_t[]> offsets(new uint64_t[relocs.size()]);
  for (auto [i, r] : llvm::enumerate(relocs))
    offsets[i] = r.getOffset();
  llvm::sort(offsets.get(), offsets.get() + relocs.size());

  // For each leading relocation, find following ones that can be folded
  // as a bitmap and fold them.
  for (size_t i = 0, e = relocs.size(); i != e;) {
    // Add a leading relocation.
    relrRelocs.push_back(Elf_Relr(offsets[i]));
    uint64_t base = offsets[i] + wordsize;
    ++i;

    // Find foldable relocations to construct bitmaps.
    for (;;) {
      uint64_t bitmap = 0;
      for (; i != e; ++i) {
        uint64_t d = offsets[i] - base;
        if (d >= nBits * wordsize || d % wordsize)
          break;
        bitmap |= uint64_t(1) << (d / wordsize);
      }
      if (!bitmap)
        break;
      relrRelocs.push_back(Elf_Relr((bitmap << 1) | 1));
      base += nBits * wordsize;
    }
  }

  // Don't allow the section to shrink; otherwise the size of the section can
  // oscillate infinitely. Trailing 1s do not decode to more relocations.
  if (relrRelocs.size() < oldSize) {
    log(".relr.dyn needs " + Twine(oldSize - relrRelocs.size()) +
        " padding word(s)");
    relrRelocs.resize(oldSize, Elf_Relr(1));
  }

  return relrRelocs.size() != oldSize;
}

SymbolTableBaseSection::SymbolTableBaseSection(StringTableSection &strTabSec)
    : SyntheticSection(strTabSec.isDynamic() ? (uint64_t)SHF_ALLOC : 0,
                       strTabSec.isDynamic() ? SHT_DYNSYM : SHT_SYMTAB,
                       config->wordsize,
                       strTabSec.isDynamic() ? ".dynsym" : ".symtab"),
      strTabSec(strTabSec) {}

// Orders symbols according to their positions in the GOT,
// in compliance with MIPS ABI rules.
// See "Global Offset Table" in Chapter 5 in the following document
// for detailed description:
// ftp://www.linux-mips.org/pub/linux/mips/doc/ABI/mipsabi.pdf
static bool sortMipsSymbols(const SymbolTableEntry &l,
                            const SymbolTableEntry &r) {
  // Sort entries related to non-local preemptible symbols by GOT indexes.
  // All other entries go to the beginning of a dynsym in arbitrary order.
  if (l.sym->isInGot() && r.sym->isInGot())
    return l.sym->getGotIdx() < r.sym->getGotIdx();
  if (!l.sym->isInGot() && !r.sym->isInGot())
    return false;
  return !l.sym->isInGot();
}

void SymbolTableBaseSection::finalizeContents() {
  if (OutputSection *sec = strTabSec.getParent())
    getParent()->link = sec->sectionIndex;

  if (this->type != SHT_DYNSYM) {
    sortSymTabSymbols();
    return;
  }

  // If it is a .dynsym, there should be no local symbols, but we need
  // to do a few things for the dynamic linker.

  // Section's Info field has the index of the first non-local symbol.
  // Because the first symbol entry is a null entry, 1 is the first.
  getParent()->info = 1;

  if (getPartition().gnuHashTab) {
    // NB: It also sorts Symbols to meet the GNU hash table requirements.
    getPartition().gnuHashTab->addSymbols(symbols);
  } else if (config->emachine == EM_MIPS) {
    llvm::stable_sort(symbols, sortMipsSymbols);
  }

  // Only the main partition's dynsym indexes are stored in the symbols
  // themselves. All other partitions use a lookup table.
  if (this == mainPart->dynSymTab.get()) {
    size_t i = 0;
    for (const SymbolTableEntry &s : symbols)
      s.sym->dynsymIndex = ++i;
  }
}

// The ELF spec requires that all local symbols precede global symbols, so we
// sort symbol entries in this function. (For .dynsym, we don't do that because
// symbols for dynamic linking are inherently all globals.)
//
// Aside from above, we put local symbols in groups starting with the STT_FILE
// symbol. That is convenient for purpose of identifying where are local symbols
// coming from.
void SymbolTableBaseSection::sortSymTabSymbols() {
  // Move all local symbols before global symbols.
  auto e = std::stable_partition(
      symbols.begin(), symbols.end(),
      [](const SymbolTableEntry &s) { return s.sym->isLocal(); });
  size_t numLocals = e - symbols.begin();
  getParent()->info = numLocals + 1;

  // We want to group the local symbols by file. For that we rebuild the local
  // part of the symbols vector. We do not need to care about the STT_FILE
  // symbols, they are already naturally placed first in each group. That
  // happens because STT_FILE is always the first symbol in the object and hence
  // precede all other local symbols we add for a file.
  MapVector<InputFile *, SmallVector<SymbolTableEntry, 0>> arr;
  for (const SymbolTableEntry &s : llvm::make_range(symbols.begin(), e))
    arr[s.sym->file].push_back(s);

  auto i = symbols.begin();
  for (auto &p : arr)
    for (SymbolTableEntry &entry : p.second)
      *i++ = entry;
}

void SymbolTableBaseSection::addSymbol(Symbol *b) {
  // Adding a local symbol to a .dynsym is a bug.
  assert(this->type != SHT_DYNSYM || !b->isLocal());
  symbols.push_back({b, strTabSec.addString(b->getName(), false)});
}

size_t SymbolTableBaseSection::getSymbolIndex(const Symbol &sym) {
  if (this == mainPart->dynSymTab.get())
    return sym.dynsymIndex;

  // Initializes symbol lookup tables lazily. This is used only for -r,
  // --emit-relocs and dynsyms in partitions other than the main one.
  llvm::call_once(onceFlag, [&] {
    symbolIndexMap.reserve(symbols.size());
    size_t i = 0;
    for (const SymbolTableEntry &e : symbols) {
      if (e.sym->type == STT_SECTION)
        sectionIndexMap[e.sym->getOutputSection()] = ++i;
      else
        symbolIndexMap[e.sym] = ++i;
    }
  });

  // Section symbols are mapped based on their output sections
  // to maintain their semantics.
  if (sym.type == STT_SECTION)
    return sectionIndexMap.lookup(sym.getOutputSection());
  return symbolIndexMap.lookup(&sym);
}

template <class ELFT>
SymbolTableSection<ELFT>::SymbolTableSection(StringTableSection &strTabSec)
    : SymbolTableBaseSection(strTabSec) {
  this->entsize = sizeof(Elf_Sym);
}

static BssSection *getCommonSec(Symbol *sym) {
  if (config->relocatable)
    if (auto *d = dyn_cast<Defined>(sym))
      return dyn_cast_or_null<BssSection>(d->section);
  return nullptr;
}

static uint32_t getSymSectionIndex(Symbol *sym) {
  assert(!(sym->hasFlag(NEEDS_COPY) && sym->isObject()));
  if (!isa<Defined>(sym) || sym->hasFlag(NEEDS_COPY))
    return SHN_UNDEF;
  if (const OutputSection *os = sym->getOutputSection())
    return os->sectionIndex >= SHN_LORESERVE ? (uint32_t)SHN_XINDEX
                                             : os->sectionIndex;
  return SHN_ABS;
}

// Write the internal symbol table contents to the output symbol table.
template <class ELFT> void SymbolTableSection<ELFT>::writeTo(uint8_t *buf) {
  // The first entry is a null entry as per the ELF spec.
  buf += sizeof(Elf_Sym);

  auto *eSym = reinterpret_cast<Elf_Sym *>(buf);

  for (SymbolTableEntry &ent : symbols) {
    Symbol *sym = ent.sym;
    bool isDefinedHere = type == SHT_SYMTAB || sym->partition == partition;

    // Set st_name, st_info and st_other.
    eSym->st_name = ent.strTabOffset;
    eSym->setBindingAndType(sym->binding, sym->type);
    eSym->st_other = sym->stOther;

    if (BssSection *commonSec = getCommonSec(sym)) {
      // When -r is specified, a COMMON symbol is not allocated. Its st_shndx
      // holds SHN_COMMON and st_value holds the alignment.
      eSym->st_shndx = SHN_COMMON;
      eSym->st_value = commonSec->addralign;
      eSym->st_size = cast<Defined>(sym)->size;
    } else {
      const uint32_t shndx = getSymSectionIndex(sym);
      if (isDefinedHere) {
        eSym->st_shndx = shndx;
        eSym->st_value = sym->getVA();
        // Copy symbol size if it is a defined symbol. st_size is not
        // significant for undefined symbols, so whether copying it or not is up
        // to us if that's the case. We'll leave it as zero because by not
        // setting a value, we can get the exact same outputs for two sets of
        // input files that differ only in undefined symbol size in DSOs.
        eSym->st_size = shndx != SHN_UNDEF ? cast<Defined>(sym)->size : 0;
      } else {
        eSym->st_shndx = 0;
        eSym->st_value = 0;
        eSym->st_size = 0;
      }
    }

    ++eSym;
  }

  // On MIPS we need to mark symbol which has a PLT entry and requires
  // pointer equality by STO_MIPS_PLT flag. That is necessary to help
  // dynamic linker distinguish such symbols and MIPS lazy-binding stubs.
  // https://sourceware.org/ml/binutils/2008-07/txt00000.txt
  if (config->emachine == EM_MIPS) {
    auto *eSym = reinterpret_cast<Elf_Sym *>(buf);

    for (SymbolTableEntry &ent : symbols) {
      Symbol *sym = ent.sym;
      if (sym->isInPlt() && sym->hasFlag(NEEDS_COPY))
        eSym->st_other |= STO_MIPS_PLT;
      if (isMicroMips()) {
        // We already set the less-significant bit for symbols
        // marked by the `STO_MIPS_MICROMIPS` flag and for microMIPS PLT
        // records. That allows us to distinguish such symbols in
        // the `MIPS<ELFT>::relocate()` routine. Now we should
        // clear that bit for non-dynamic symbol table, so tools
        // like `objdump` will be able to deal with a correct
        // symbol position.
        if (sym->isDefined() &&
            ((sym->stOther & STO_MIPS_MICROMIPS) || sym->hasFlag(NEEDS_COPY))) {
          if (!strTabSec.isDynamic())
            eSym->st_value &= ~1;
          eSym->st_other |= STO_MIPS_MICROMIPS;
        }
      }
      if (config->relocatable)
        if (auto *d = dyn_cast<Defined>(sym))
          if (isMipsPIC<ELFT>(d))
            eSym->st_other |= STO_MIPS_PIC;
      ++eSym;
    }
  }
}

SymtabShndxSection::SymtabShndxSection()
    : SyntheticSection(0, SHT_SYMTAB_SHNDX, 4, ".symtab_shndx") {
  this->entsize = 4;
}

void SymtabShndxSection::writeTo(uint8_t *buf) {
  // We write an array of 32 bit values, where each value has 1:1 association
  // with an entry in .symtab. If the corresponding entry contains SHN_XINDEX,
  // we need to write actual index, otherwise, we must write SHN_UNDEF(0).
  buf += 4; // Ignore .symtab[0] entry.
  for (const SymbolTableEntry &entry : in.symTab->getSymbols()) {
    if (!getCommonSec(entry.sym) && getSymSectionIndex(entry.sym) == SHN_XINDEX)
      write32(buf, entry.sym->getOutputSection()->sectionIndex);
    buf += 4;
  }
}

bool SymtabShndxSection::isNeeded() const {
  // SHT_SYMTAB can hold symbols with section indices values up to
  // SHN_LORESERVE. If we need more, we want to use extension SHT_SYMTAB_SHNDX
  // section. Problem is that we reveal the final section indices a bit too
  // late, and we do not know them here. For simplicity, we just always create
  // a .symtab_shndx section when the amount of output sections is huge.
  size_t size = 0;
  for (SectionCommand *cmd : script->sectionCommands)
    if (isa<OutputDesc>(cmd))
      ++size;
  return size >= SHN_LORESERVE;
}

void SymtabShndxSection::finalizeContents() {
  getParent()->link = in.symTab->getParent()->sectionIndex;
}

size_t SymtabShndxSection::getSize() const {
  return in.symTab->getNumSymbols() * 4;
}

// .hash and .gnu.hash sections contain on-disk hash tables that map
// symbol names to their dynamic symbol table indices. Their purpose
// is to help the dynamic linker resolve symbols quickly. If ELF files
// don't have them, the dynamic linker has to do linear search on all
// dynamic symbols, which makes programs slower. Therefore, a .hash
// section is added to a DSO by default.
//
// The Unix semantics of resolving dynamic symbols is somewhat expensive.
// Each ELF file has a list of DSOs that the ELF file depends on and a
// list of dynamic symbols that need to be resolved from any of the
// DSOs. That means resolving all dynamic symbols takes O(m)*O(n)
// where m is the number of DSOs and n is the number of dynamic
// symbols. For modern large programs, both m and n are large.  So
// making each step faster by using hash tables substantially
// improves time to load programs.
//
// (Note that this is not the only way to design the shared library.
// For instance, the Windows DLL takes a different approach. On
// Windows, each dynamic symbol has a name of DLL from which the symbol
// has to be resolved. That makes the cost of symbol resolution O(n).
// This disables some hacky techniques you can use on Unix such as
// LD_PRELOAD, but this is arguably better semantics than the Unix ones.)
//
// Due to historical reasons, we have two different hash tables, .hash
// and .gnu.hash. They are for the same purpose, and .gnu.hash is a new
// and better version of .hash. .hash is just an on-disk hash table, but
// .gnu.hash has a bloom filter in addition to a hash table to skip
// DSOs very quickly. If you are sure that your dynamic linker knows
// about .gnu.hash, you want to specify --hash-style=gnu. Otherwise, a
// safe bet is to specify --hash-style=both for backward compatibility.
GnuHashTableSection::GnuHashTableSection()
    : SyntheticSection(SHF_ALLOC, SHT_GNU_HASH, config->wordsize, ".gnu.hash") {
}

void GnuHashTableSection::finalizeContents() {
  if (OutputSection *sec = getPartition().dynSymTab->getParent())
    getParent()->link = sec->sectionIndex;

  // Computes bloom filter size in word size. We want to allocate 12
  // bits for each symbol. It must be a power of two.
  if (symbols.empty()) {
    maskWords = 1;
  } else {
    uint64_t numBits = symbols.size() * 12;
    maskWords = NextPowerOf2(numBits / (config->wordsize * 8));
  }

  size = 16;                            // Header
  size += config->wordsize * maskWords; // Bloom filter
  size += nBuckets * 4;                 // Hash buckets
  size += symbols.size() * 4;           // Hash values
}

void GnuHashTableSection::writeTo(uint8_t *buf) {
  // Write a header.
  write32(buf, nBuckets);
  write32(buf + 4, getPartition().dynSymTab->getNumSymbols() - symbols.size());
  write32(buf + 8, maskWords);
  write32(buf + 12, Shift2);
  buf += 16;

  // Write the 2-bit bloom filter.
  const unsigned c = config->is64 ? 64 : 32;
  for (const Entry &sym : symbols) {
    // When C = 64, we choose a word with bits [6:...] and set 1 to two bits in
    // the word using bits [0:5] and [26:31].
    size_t i = (sym.hash / c) & (maskWords - 1);
    uint64_t val = readUint(buf + i * config->wordsize);
    val |= uint64_t(1) << (sym.hash % c);
    val |= uint64_t(1) << ((sym.hash >> Shift2) % c);
    writeUint(buf + i * config->wordsize, val);
  }
  buf += config->wordsize * maskWords;

  // Write the hash table.
  uint32_t *buckets = reinterpret_cast<uint32_t *>(buf);
  uint32_t oldBucket = -1;
  uint32_t *values = buckets + nBuckets;
  for (auto i = symbols.begin(), e = symbols.end(); i != e; ++i) {
    // Write a hash value. It represents a sequence of chains that share the
    // same hash modulo value. The last element of each chain is terminated by
    // LSB 1.
    uint32_t hash = i->hash;
    bool isLastInChain = (i + 1) == e || i->bucketIdx != (i + 1)->bucketIdx;
    hash = isLastInChain ? hash | 1 : hash & ~1;
    write32(values++, hash);

    if (i->bucketIdx == oldBucket)
      continue;
    // Write a hash bucket. Hash buckets contain indices in the following hash
    // value table.
    write32(buckets + i->bucketIdx,
            getPartition().dynSymTab->getSymbolIndex(*i->sym));
    oldBucket = i->bucketIdx;
  }
}

// Add symbols to this symbol hash table. Note that this function
// destructively sort a given vector -- which is needed because
// GNU-style hash table places some sorting requirements.
void GnuHashTableSection::addSymbols(SmallVectorImpl<SymbolTableEntry> &v) {
  // We cannot use 'auto' for Mid because GCC 6.1 cannot deduce
  // its type correctly.
  auto mid =
      std::stable_partition(v.begin(), v.end(), [&](const SymbolTableEntry &s) {
        return !s.sym->isDefined() || s.sym->partition != partition;
      });

  // We chose load factor 4 for the on-disk hash table. For each hash
  // collision, the dynamic linker will compare a uint32_t hash value.
  // Since the integer comparison is quite fast, we believe we can
  // make the load factor even larger. 4 is just a conservative choice.
  //
  // Note that we don't want to create a zero-sized hash table because
  // Android loader as of 2018 doesn't like a .gnu.hash containing such
  // table. If that's the case, we create a hash table with one unused
  // dummy slot.
  nBuckets = std::max<size_t>((v.end() - mid) / 4, 1);

  if (mid == v.end())
    return;

  for (SymbolTableEntry &ent : llvm::make_range(mid, v.end())) {
    Symbol *b = ent.sym;
    uint32_t hash = hashGnu(b->getName());
    uint32_t bucketIdx = hash % nBuckets;
    symbols.push_back({b, ent.strTabOffset, hash, bucketIdx});
  }

  llvm::sort(symbols, [](const Entry &l, const Entry &r) {
    return std::tie(l.bucketIdx, l.strTabOffset) <
           std::tie(r.bucketIdx, r.strTabOffset);
  });

  v.erase(mid, v.end());
  for (const Entry &ent : symbols)
    v.push_back({ent.sym, ent.strTabOffset});
}

HashTableSection::HashTableSection()
    : SyntheticSection(SHF_ALLOC, SHT_HASH, 4, ".hash") {
  this->entsize = 4;
}

void HashTableSection::finalizeContents() {
  SymbolTableBaseSection *symTab = getPartition().dynSymTab.get();

  if (OutputSection *sec = symTab->getParent())
    getParent()->link = sec->sectionIndex;

  unsigned numEntries = 2;               // nbucket and nchain.
  numEntries += symTab->getNumSymbols(); // The chain entries.

  // Create as many buckets as there are symbols.
  numEntries += symTab->getNumSymbols();
  this->size = numEntries * 4;
}

void HashTableSection::writeTo(uint8_t *buf) {
  SymbolTableBaseSection *symTab = getPartition().dynSymTab.get();
  unsigned numSymbols = symTab->getNumSymbols();

  uint32_t *p = reinterpret_cast<uint32_t *>(buf);
  write32(p++, numSymbols); // nbucket
  write32(p++, numSymbols); // nchain

  uint32_t *buckets = p;
  uint32_t *chains = p + numSymbols;

  for (const SymbolTableEntry &s : symTab->getSymbols()) {
    Symbol *sym = s.sym;
    StringRef name = sym->getName();
    unsigned i = sym->dynsymIndex;
    uint32_t hash = hashSysV(name) % numSymbols;
    chains[i] = buckets[hash];
    write32(buckets + hash, i);
  }
}

PltSection::PltSection()
    : SyntheticSection(SHF_ALLOC | SHF_EXECINSTR, SHT_PROGBITS, 16, ".plt"),
      headerSize(target->pltHeaderSize) {
  // On PowerPC, this section contains lazy symbol resolvers.
  if (config->emachine == EM_PPC64) {
    name = ".glink";
    addralign = 4;
  }

  // On x86 when IBT is enabled, this section contains the second PLT (lazy
  // symbol resolvers).
  if ((config->emachine == EM_386 || config->emachine == EM_X86_64) &&
      (config->andFeatures & GNU_PROPERTY_X86_FEATURE_1_IBT))
    name = ".plt.sec";
#ifdef __OpenBSD__
  else if (config->emachine == EM_X86_64)
    name = ".plt.sec";
#endif

  // The PLT needs to be writable on SPARC as the dynamic linker will
  // modify the instructions in the PLT entries.
  if (config->emachine == EM_SPARCV9)
    this->flags |= SHF_WRITE;
}

void PltSection::writeTo(uint8_t *buf) {
  // At beginning of PLT, we have code to call the dynamic
  // linker to resolve dynsyms at runtime. Write such code.
  target->writePltHeader(buf);
  size_t off = headerSize;

  for (const Symbol *sym : entries) {
    target->writePlt(buf + off, *sym, getVA() + off);
    off += target->pltEntrySize;
  }
}

void PltSection::addEntry(Symbol &sym) {
  assert(sym.auxIdx == symAux.size() - 1);
  symAux.back().pltIdx = entries.size();
  entries.push_back(&sym);
}

size_t PltSection::getSize() const {
  return headerSize + entries.size() * target->pltEntrySize;
}

bool PltSection::isNeeded() const {
  // For -z retpolineplt, .iplt needs the .plt header.
  return !entries.empty() || (config->zRetpolineplt && in.iplt->isNeeded());
}

// Used by ARM to add mapping symbols in the PLT section, which aid
// disassembly.
void PltSection::addSymbols() {
  target->addPltHeaderSymbols(*this);

  size_t off = headerSize;
  for (size_t i = 0; i < entries.size(); ++i) {
    target->addPltSymbols(*this, off);
    off += target->pltEntrySize;
  }
}

IpltSection::IpltSection()
    : SyntheticSection(SHF_ALLOC | SHF_EXECINSTR, SHT_PROGBITS, 16, ".iplt") {
  if (config->emachine == EM_PPC || config->emachine == EM_PPC64) {
    name = ".glink";
    addralign = 4;
  }
}

void IpltSection::writeTo(uint8_t *buf) {
  uint32_t off = 0;
  for (const Symbol *sym : entries) {
    target->writeIplt(buf + off, *sym, getVA() + off);
    off += target->ipltEntrySize;
  }
}

size_t IpltSection::getSize() const {
  return entries.size() * target->ipltEntrySize;
}

void IpltSection::addEntry(Symbol &sym) {
  assert(sym.auxIdx == symAux.size() - 1);
  symAux.back().pltIdx = entries.size();
  entries.push_back(&sym);
}

// ARM uses mapping symbols to aid disassembly.
void IpltSection::addSymbols() {
  size_t off = 0;
  for (size_t i = 0, e = entries.size(); i != e; ++i) {
    target->addPltSymbols(*this, off);
    off += target->pltEntrySize;
  }
}

PPC32GlinkSection::PPC32GlinkSection() {
  name = ".glink";
  addralign = 4;
}

void PPC32GlinkSection::writeTo(uint8_t *buf) {
  writePPC32GlinkSection(buf, entries.size());
}

size_t PPC32GlinkSection::getSize() const {
  return headerSize + entries.size() * target->pltEntrySize + footerSize;
}

// This is an x86-only extra PLT section and used only when a security
// enhancement feature called CET is enabled. In this comment, I'll explain what
// the feature is and why we have two PLT sections if CET is enabled.
//
// So, what does CET do? CET introduces a new restriction to indirect jump
// instructions. CET works this way. Assume that CET is enabled. Then, if you
// execute an indirect jump instruction, the processor verifies that a special
// "landing pad" instruction (which is actually a repurposed NOP instruction and
// now called "endbr32" or "endbr64") is at the jump target. If the jump target
// does not start with that instruction, the processor raises an exception
// instead of continuing executing code.
//
// If CET is enabled, the compiler emits endbr to all locations where indirect
// jumps may jump to.
//
// This mechanism makes it extremely hard to transfer the control to a middle of
// a function that is not supporsed to be a indirect jump target, preventing
// certain types of attacks such as ROP or JOP.
//
// Note that the processors in the market as of 2019 don't actually support the
// feature. Only the spec is available at the moment.
//
// Now, I'll explain why we have this extra PLT section for CET.
//
// Since you can indirectly jump to a PLT entry, we have to make PLT entries
// start with endbr. The problem is there's no extra space for endbr (which is 4
// bytes long), as the PLT entry is only 16 bytes long and all bytes are already
// used.
//
// In order to deal with the issue, we split a PLT entry into two PLT entries.
// Remember that each PLT entry contains code to jump to an address read from
// .got.plt AND code to resolve a dynamic symbol lazily. With the 2-PLT scheme,
// the former code is written to .plt.sec, and the latter code is written to
// .plt.
//
// Lazy symbol resolution in the 2-PLT scheme works in the usual way, except
// that the regular .plt is now called .plt.sec and .plt is repurposed to
// contain only code for lazy symbol resolution.
//
// In other words, this is how the 2-PLT scheme works. Application code is
// supposed to jump to .plt.sec to call an external function. Each .plt.sec
// entry contains code to read an address from a corresponding .got.plt entry
// and jump to that address. Addresses in .got.plt initially point to .plt, so
// when an application calls an external function for the first time, the
// control is transferred to a function that resolves a symbol name from
// external shared object files. That function then rewrites a .got.plt entry
// with a resolved address, so that the subsequent function calls directly jump
// to a desired location from .plt.sec.
//
// There is an open question as to whether the 2-PLT scheme was desirable or
// not. We could have simply extended the PLT entry size to 32-bytes to
// accommodate endbr, and that scheme would have been much simpler than the
// 2-PLT scheme. One reason to split PLT was, by doing that, we could keep hot
// code (.plt.sec) from cold code (.plt). But as far as I know no one proved
// that the optimization actually makes a difference.
//
// That said, the 2-PLT scheme is a part of the ABI, debuggers and other tools
// depend on it, so we implement the ABI.
IBTPltSection::IBTPltSection()
    : SyntheticSection(SHF_ALLOC | SHF_EXECINSTR, SHT_PROGBITS, 16, ".plt") {}

void IBTPltSection::writeTo(uint8_t *buf) {
  target->writeIBTPlt(buf, in.plt->getNumEntries());
}

size_t IBTPltSection::getSize() const {
  // 16 is the header size of .plt.
  return 16 + in.plt->getNumEntries() * target->pltEntrySize;
}

bool IBTPltSection::isNeeded() const { return in.plt->getNumEntries() > 0; }

RelroPaddingSection::RelroPaddingSection()
    : SyntheticSection(SHF_ALLOC | SHF_WRITE, SHT_NOBITS, 1, ".relro_padding") {
}

// The string hash function for .gdb_index.
static uint32_t computeGdbHash(StringRef s) {
  uint32_t h = 0;
  for (uint8_t c : s)
    h = h * 67 + toLower(c) - 113;
  return h;
}

// 4-byte alignment ensures that values in the hash lookup table and the name
// table are aligned.
DebugNamesBaseSection::DebugNamesBaseSection()
    : SyntheticSection(0, SHT_PROGBITS, 4, ".debug_names") {}

// Get the size of the .debug_names section header in bytes for DWARF32:
static uint32_t getDebugNamesHeaderSize(uint32_t augmentationStringSize) {
  return /* unit length */ 4 +
         /* version */ 2 +
         /* padding */ 2 +
         /* CU count */ 4 +
         /* TU count */ 4 +
         /* Foreign TU count */ 4 +
         /* Bucket Count */ 4 +
         /* Name Count */ 4 +
         /* Abbrev table size */ 4 +
         /* Augmentation string size */ 4 +
         /* Augmentation string */ augmentationStringSize;
}

static Expected<DebugNamesBaseSection::IndexEntry *>
readEntry(uint64_t &offset, const DWARFDebugNames::NameIndex &ni,
          uint64_t entriesBase, DWARFDataExtractor &namesExtractor,
          const LLDDWARFSection &namesSec) {
  auto ie = makeThreadLocal<DebugNamesBaseSection::IndexEntry>();
  ie->poolOffset = offset;
  Error err = Error::success();
  uint64_t ulebVal = namesExtractor.getULEB128(&offset, &err);
  if (err)
    return createStringError(inconvertibleErrorCode(),
                             "invalid abbrev code: %s",
                             toString(std::move(err)).c_str());
  if (!isUInt<32>(ulebVal))
    return createStringError(inconvertibleErrorCode(),
                             "abbrev code too large for DWARF32: %" PRIu64,
                             ulebVal);
  ie->abbrevCode = static_cast<uint32_t>(ulebVal);
  auto it = ni.getAbbrevs().find_as(ie->abbrevCode);
  if (it == ni.getAbbrevs().end())
    return createStringError(inconvertibleErrorCode(),
                             "abbrev code not found in abbrev table: %" PRIu32,
                             ie->abbrevCode);

  DebugNamesBaseSection::AttrValue attr, cuAttr = {0, 0};
  for (DWARFDebugNames::AttributeEncoding a : it->Attributes) {
    if (a.Index == dwarf::DW_IDX_parent) {
      if (a.Form == dwarf::DW_FORM_ref4) {
        attr.attrValue = namesExtractor.getU32(&offset, &err);
        attr.attrSize = 4;
        ie->parentOffset = entriesBase + attr.attrValue;
      } else if (a.Form != DW_FORM_flag_present)
        return createStringError(inconvertibleErrorCode(),
                                 "invalid form for DW_IDX_parent");
    } else {
      switch (a.Form) {
      case DW_FORM_data1:
      case DW_FORM_ref1: {
        attr.attrValue = namesExtractor.getU8(&offset, &err);
        attr.attrSize = 1;
        break;
      }
      case DW_FORM_data2:
      case DW_FORM_ref2: {
        attr.attrValue = namesExtractor.getU16(&offset, &err);
        attr.attrSize = 2;
        break;
      }
      case DW_FORM_data4:
      case DW_FORM_ref4: {
        attr.attrValue = namesExtractor.getU32(&offset, &err);
        attr.attrSize = 4;
        break;
      }
      default:
        return createStringError(
            inconvertibleErrorCode(),
            "unrecognized form encoding %d in abbrev table", a.Form);
      }
    }
    if (err)
      return createStringError(inconvertibleErrorCode(),
                               "error while reading attributes: %s",
                               toString(std::move(err)).c_str());
    if (a.Index == DW_IDX_compile_unit)
      cuAttr = attr;
    else if (a.Form != DW_FORM_flag_present)
      ie->attrValues.push_back(attr);
  }
  // Canonicalize abbrev by placing the CU/TU index at the end.
  ie->attrValues.push_back(cuAttr);
  return ie;
}

void DebugNamesBaseSection::parseDebugNames(
    InputChunk &inputChunk, OutputChunk &chunk,
    DWARFDataExtractor &namesExtractor, DataExtractor &strExtractor,
    function_ref<SmallVector<uint32_t, 0>(
        uint32_t numCus, const DWARFDebugNames::Header &,
        const DWARFDebugNames::DWARFDebugNamesOffsets &)>
        readOffsets) {
  const LLDDWARFSection &namesSec = inputChunk.section;
  DenseMap<uint32_t, IndexEntry *> offsetMap;
  // Number of CUs seen in previous NameIndex sections within current chunk.
  uint32_t numCus = 0;
  for (const DWARFDebugNames::NameIndex &ni : *inputChunk.llvmDebugNames) {
    NameData &nd = inputChunk.nameData.emplace_back();
    nd.hdr = ni.getHeader();
    if (nd.hdr.Format != DwarfFormat::DWARF32) {
      errorOrWarn(toString(namesSec.sec) +
                  Twine(": found DWARF64, which is currently unsupported"));
      return;
    }
    if (nd.hdr.Version != 5) {
      errorOrWarn(toString(namesSec.sec) + Twine(": unsupported version: ") +
                  Twine(nd.hdr.Version));
      return;
    }
    uint32_t dwarfSize = dwarf::getDwarfOffsetByteSize(DwarfFormat::DWARF32);
    DWARFDebugNames::DWARFDebugNamesOffsets locs = ni.getOffsets();
    if (locs.EntriesBase > namesExtractor.getData().size()) {
      errorOrWarn(toString(namesSec.sec) +
                  Twine(": entry pool start is beyond end of section"));
      return;
    }

    SmallVector<uint32_t, 0> entryOffsets = readOffsets(numCus, nd.hdr, locs);

    // Read the entry pool.
    offsetMap.clear();
    nd.nameEntries.resize(nd.hdr.NameCount);
    for (auto i : seq(nd.hdr.NameCount)) {
      NameEntry &ne = nd.nameEntries[i];
      uint64_t strOffset = locs.StringOffsetsBase + i * dwarfSize;
      ne.stringOffset = strOffset;
      uint64_t strp = namesExtractor.getRelocatedValue(dwarfSize, &strOffset);
      StringRef name = strExtractor.getCStrRef(&strp);
      ne.name = name.data();
      ne.hashValue = caseFoldingDjbHash(name);

      // Read a series of index entries that end with abbreviation code 0.
      uint64_t offset = locs.EntriesBase + entryOffsets[i];
      while (offset < namesSec.Data.size() && namesSec.Data[offset] != 0) {
        // Read & store all entries (for the same string).
        Expected<IndexEntry *> ieOrErr =
            readEntry(offset, ni, locs.EntriesBase, namesExtractor, namesSec);
        if (!ieOrErr) {
          errorOrWarn(toString(namesSec.sec) + ": " +
                      toString(ieOrErr.takeError()));
          return;
        }
        ne.indexEntries.push_back(std::move(*ieOrErr));
      }
      if (offset >= namesSec.Data.size())
        errorOrWarn(toString(namesSec.sec) +
                    Twine(": index entry is out of bounds"));

      for (IndexEntry &ie : ne.entries())
        offsetMap[ie.poolOffset] = &ie;
    }

    // Assign parent pointers, which will be used to update DW_IDX_parent index
    // attributes. Note: offsetMap[0] does not exist, so parentOffset == 0 will
    // get parentEntry == null as well.
    for (NameEntry &ne : nd.nameEntries)
      for (IndexEntry &ie : ne.entries())
        ie.parentEntry = offsetMap.lookup(ie.parentOffset);
    numCus += nd.hdr.CompUnitCount;
  }
}

// Compute the form for output DW_IDX_compile_unit attributes, similar to
// DIEInteger::BestForm. The input form (often DW_FORM_data1) may not hold all
// the merged CU indices.
std::pair<uint8_t, dwarf::Form> static getMergedCuCountForm(
    uint32_t compUnitCount) {
  if (compUnitCount > UINT16_MAX)
    return {4, DW_FORM_data4};
  if (compUnitCount > UINT8_MAX)
    return {2, DW_FORM_data2};
  return {1, DW_FORM_data1};
}

void DebugNamesBaseSection::computeHdrAndAbbrevTable(
    MutableArrayRef<InputChunk> inputChunks) {
  TimeTraceScope timeScope("Merge .debug_names", "hdr and abbrev table");
  size_t numCu = 0;
  hdr.Format = DwarfFormat::DWARF32;
  hdr.Version = 5;
  hdr.CompUnitCount = 0;
  hdr.LocalTypeUnitCount = 0;
  hdr.ForeignTypeUnitCount = 0;
  hdr.AugmentationStringSize = 0;

  // Compute CU and TU counts.
  for (auto i : seq(numChunks)) {
    InputChunk &inputChunk = inputChunks[i];
    inputChunk.baseCuIdx = numCu;
    numCu += chunks[i].compUnits.size();
    for (const NameData &nd : inputChunk.nameData) {
      hdr.CompUnitCount += nd.hdr.CompUnitCount;
      // TODO: We don't handle type units yet, so LocalTypeUnitCount &
      // ForeignTypeUnitCount are left as 0.
      if (nd.hdr.LocalTypeUnitCount || nd.hdr.ForeignTypeUnitCount)
        warn(toString(inputChunk.section.sec) +
             Twine(": type units are not implemented"));
      // If augmentation strings are not identical, use an empty string.
      if (i == 0) {
        hdr.AugmentationStringSize = nd.hdr.AugmentationStringSize;
        hdr.AugmentationString = nd.hdr.AugmentationString;
      } else if (hdr.AugmentationString != nd.hdr.AugmentationString) {
        // There are conflicting augmentation strings, so it's best for the
        // merged index to not use an augmentation string.
        hdr.AugmentationStringSize = 0;
        hdr.AugmentationString.clear();
      }
    }
  }

  // Create the merged abbrev table, uniquifyinng the input abbrev tables and
  // computing mapping from old (per-cu) abbrev codes to new (merged) abbrev
  // codes.
  FoldingSet<Abbrev> abbrevSet;
  // Determine the form for the DW_IDX_compile_unit attributes in the merged
  // index. The input form may not be big enough for all CU indices.
  dwarf::Form cuAttrForm = getMergedCuCountForm(hdr.CompUnitCount).second;
  for (InputChunk &inputChunk : inputChunks) {
    for (auto [i, ni] : enumerate(*inputChunk.llvmDebugNames)) {
      for (const DWARFDebugNames::Abbrev &oldAbbrev : ni.getAbbrevs()) {
        // Canonicalize abbrev by placing the CU/TU index at the end,
        // similar to 'parseDebugNames'.
        Abbrev abbrev;
        DWARFDebugNames::AttributeEncoding cuAttr(DW_IDX_compile_unit,
                                                  cuAttrForm);
        abbrev.code = oldAbbrev.Code;
        abbrev.tag = oldAbbrev.Tag;
        for (DWARFDebugNames::AttributeEncoding a : oldAbbrev.Attributes) {
          if (a.Index == DW_IDX_compile_unit)
            cuAttr.Index = a.Index;
          else
            abbrev.attributes.push_back({a.Index, a.Form});
        }
        // Put the CU/TU index at the end of the attributes list.
        abbrev.attributes.push_back(cuAttr);

        // Profile the abbrev, get or assign a new code, then record the abbrev
        // code mapping.
        FoldingSetNodeID id;
        abbrev.Profile(id);
        uint32_t newCode;
        void *insertPos;
        if (Abbrev *existing = abbrevSet.FindNodeOrInsertPos(id, insertPos)) {
          // Found it; we've already seen an identical abbreviation.
          newCode = existing->code;
        } else {
          Abbrev *abbrev2 =
              new (abbrevAlloc.Allocate()) Abbrev(std::move(abbrev));
          abbrevSet.InsertNode(abbrev2, insertPos);
          abbrevTable.push_back(abbrev2);
          newCode = abbrevTable.size();
          abbrev2->code = newCode;
        }
        inputChunk.nameData[i].abbrevCodeMap[oldAbbrev.Code] = newCode;
      }
    }
  }

  // Compute the merged abbrev table.
  raw_svector_ostream os(abbrevTableBuf);
  for (Abbrev *abbrev : abbrevTable) {
    encodeULEB128(abbrev->code, os);
    encodeULEB128(abbrev->tag, os);
    for (DWARFDebugNames::AttributeEncoding a : abbrev->attributes) {
      encodeULEB128(a.Index, os);
      encodeULEB128(a.Form, os);
    }
    os.write("\0", 2); // attribute specification end
  }
  os.write(0); // abbrev table end
  hdr.AbbrevTableSize = abbrevTableBuf.size();
}

void DebugNamesBaseSection::Abbrev::Profile(FoldingSetNodeID &id) const {
  id.AddInteger(tag);
  for (const DWARFDebugNames::AttributeEncoding &attr : attributes) {
    id.AddInteger(attr.Index);
    id.AddInteger(attr.Form);
  }
}

std::pair<uint32_t, uint32_t> DebugNamesBaseSection::computeEntryPool(
    MutableArrayRef<InputChunk> inputChunks) {
  TimeTraceScope timeScope("Merge .debug_names", "entry pool");
  // Collect and de-duplicate all the names (preserving all the entries).
  // Speed it up using multithreading, as the number of symbols can be in the
  // order of millions.
  const size_t concurrency =
      bit_floor(std::min<size_t>(config->threadCount, numShards));
  const size_t shift = 32 - countr_zero(numShards);
  const uint8_t cuAttrSize = getMergedCuCountForm(hdr.CompUnitCount).first;
  DenseMap<CachedHashStringRef, size_t> maps[numShards];

  parallelFor(0, concurrency, [&](size_t threadId) {
    for (auto i : seq(numChunks)) {
      InputChunk &inputChunk = inputChunks[i];
      for (auto j : seq(inputChunk.nameData.size())) {
        NameData &nd = inputChunk.nameData[j];
        // Deduplicate the NameEntry records (based on the string/name),
        // appending all IndexEntries from duplicate NameEntry records to
        // the single preserved copy.
        for (NameEntry &ne : nd.nameEntries) {
          auto shardId = ne.hashValue >> shift;
          if ((shardId & (concurrency - 1)) != threadId)
            continue;

          ne.chunkIdx = i;
          for (IndexEntry &ie : ne.entries()) {
            // Update the IndexEntry's abbrev code to match the merged
            // abbreviations.
            ie.abbrevCode = nd.abbrevCodeMap[ie.abbrevCode];
            // Update the DW_IDX_compile_unit attribute (the last one after
            // canonicalization) to have correct merged offset value and size.
            auto &back = ie.attrValues.back();
            back.attrValue += inputChunk.baseCuIdx + j;
            back.attrSize = cuAttrSize;
          }

          auto &nameVec = nameVecs[shardId];
          auto [it, inserted] = maps[shardId].try_emplace(
              CachedHashStringRef(ne.name, ne.hashValue), nameVec.size());
          if (inserted)
            nameVec.push_back(std::move(ne));
          else
            nameVec[it->second].indexEntries.append(std::move(ne.indexEntries));
        }
      }
    }
  });

  // Compute entry offsets in parallel. First, compute offsets relative to the
  // current shard.
  uint32_t offsets[numShards];
  parallelFor(0, numShards, [&](size_t shard) {
    uint32_t offset = 0;
    for (NameEntry &ne : nameVecs[shard]) {
      ne.entryOffset = offset;
      for (IndexEntry &ie : ne.entries()) {
        ie.poolOffset = offset;
        offset += getULEB128Size(ie.abbrevCode);
        for (AttrValue value : ie.attrValues)
          offset += value.attrSize;
      }
      ++offset; // index entry sentinel
    }
    offsets[shard] = offset;
  });
  // Then add shard offsets.
  std::partial_sum(offsets, std::end(offsets), offsets);
  parallelFor(1, numShards, [&](size_t shard) {
    uint32_t offset = offsets[shard - 1];
    for (NameEntry &ne : nameVecs[shard]) {
      ne.entryOffset += offset;
      for (IndexEntry &ie : ne.entries())
        ie.poolOffset += offset;
    }
  });

  // Update the DW_IDX_parent entries that refer to real parents (have
  // DW_FORM_ref4).
  parallelFor(0, numShards, [&](size_t shard) {
    for (NameEntry &ne : nameVecs[shard]) {
      for (IndexEntry &ie : ne.entries()) {
        if (!ie.parentEntry)
          continue;
        // Abbrevs are indexed starting at 1; vector starts at 0. (abbrevCode
        // corresponds to position in the merged table vector).
        const Abbrev *abbrev = abbrevTable[ie.abbrevCode - 1];
        for (const auto &[a, v] : zip_equal(abbrev->attributes, ie.attrValues))
          if (a.Index == DW_IDX_parent && a.Form == DW_FORM_ref4)
            v.attrValue = ie.parentEntry->poolOffset;
      }
    }
  });

  // Return (entry pool size, number of entries).
  uint32_t num = 0;
  for (auto &map : maps)
    num += map.size();
  return {offsets[numShards - 1], num};
}

void DebugNamesBaseSection::init(
    function_ref<void(InputFile *, InputChunk &, OutputChunk &)> parseFile) {
  TimeTraceScope timeScope("Merge .debug_names");
  // Collect and remove input .debug_names sections. Save InputSection pointers
  // to relocate string offsets in `writeTo`.
  SetVector<InputFile *> files;
  for (InputSectionBase *s : ctx.inputSections) {
    InputSection *isec = dyn_cast<InputSection>(s);
    if (!isec)
      continue;
    if (!(s->flags & SHF_ALLOC) && s->name == ".debug_names") {
      s->markDead();
      inputSections.push_back(isec);
      files.insert(isec->file);
    }
  }

  // Parse input .debug_names sections and extract InputChunk and OutputChunk
  // data. OutputChunk contains CU information, which will be needed by
  // `writeTo`.
  auto inputChunksPtr = std::make_unique<InputChunk[]>(files.size());
  MutableArrayRef<InputChunk> inputChunks(inputChunksPtr.get(), files.size());
  numChunks = files.size();
  chunks = std::make_unique<OutputChunk[]>(files.size());
  {
    TimeTraceScope timeScope("Merge .debug_names", "parse");
    parallelFor(0, files.size(), [&](size_t i) {
      parseFile(files[i], inputChunks[i], chunks[i]);
    });
  }

  // Compute section header (except unit_length), abbrev table, and entry pool.
  computeHdrAndAbbrevTable(inputChunks);
  uint32_t entryPoolSize;
  std::tie(entryPoolSize, hdr.NameCount) = computeEntryPool(inputChunks);
  hdr.BucketCount = dwarf::getDebugNamesBucketCount(hdr.NameCount);

  // Compute the section size. Subtract 4 to get the unit_length for DWARF32.
  uint32_t hdrSize = getDebugNamesHeaderSize(hdr.AugmentationStringSize);
  size = findDebugNamesOffsets(hdrSize, hdr).EntriesBase + entryPoolSize;
  hdr.UnitLength = size - 4;
}

template <class ELFT> DebugNamesSection<ELFT>::DebugNamesSection() {
  init([](InputFile *f, InputChunk &inputChunk, OutputChunk &chunk) {
    auto *file = cast<ObjFile<ELFT>>(f);
    DWARFContext dwarf(std::make_unique<LLDDwarfObj<ELFT>>(file));
    auto &dobj = static_cast<const LLDDwarfObj<ELFT> &>(dwarf.getDWARFObj());
    chunk.infoSec = dobj.getInfoSection();
    DWARFDataExtractor namesExtractor(dobj, dobj.getNamesSection(),
                                      ELFT::Endianness == endianness::little,
                                      ELFT::Is64Bits ? 8 : 4);
    // .debug_str is needed to get symbol names from string offsets.
    DataExtractor strExtractor(dobj.getStrSection(),
                               ELFT::Endianness == endianness::little,
                               ELFT::Is64Bits ? 8 : 4);
    inputChunk.section = dobj.getNamesSection();

    inputChunk.llvmDebugNames.emplace(namesExtractor, strExtractor);
    if (Error e = inputChunk.llvmDebugNames->extract()) {
      errorOrWarn(toString(dobj.getNamesSection().sec) + Twine(": ") +
                  toString(std::move(e)));
    }
    parseDebugNames(
        inputChunk, chunk, namesExtractor, strExtractor,
        [&chunk, namesData = dobj.getNamesSection().Data.data()](
            uint32_t numCus, const DWARFDebugNames::Header &hdr,
            const DWARFDebugNames::DWARFDebugNamesOffsets &locs) {
          // Read CU offsets, which are relocated by .debug_info + X
          // relocations. Record the section offset to be relocated by
          // `finalizeContents`.
          chunk.compUnits.resize_for_overwrite(numCus + hdr.CompUnitCount);
          for (auto i : seq(hdr.CompUnitCount))
            chunk.compUnits[numCus + i] = locs.CUsBase + i * 4;

          // Read entry offsets.
          const char *p = namesData + locs.EntryOffsetsBase;
          SmallVector<uint32_t, 0> entryOffsets;
          entryOffsets.resize_for_overwrite(hdr.NameCount);
          for (uint32_t &offset : entryOffsets)
            offset = endian::readNext<uint32_t, ELFT::Endianness, unaligned>(p);
          return entryOffsets;
        });
  });
}

template <class ELFT>
template <class RelTy>
void DebugNamesSection<ELFT>::getNameRelocs(
    const InputFile &file, DenseMap<uint32_t, uint32_t> &relocs,
    Relocs<RelTy> rels) {
  for (const RelTy &rel : rels) {
    Symbol &sym = file.getRelocTargetSym(rel);
    relocs[rel.r_offset] = sym.getVA(getAddend<ELFT>(rel));
  }
}

template <class ELFT> void DebugNamesSection<ELFT>::finalizeContents() {
  // Get relocations of .debug_names sections.
  auto relocs = std::make_unique<DenseMap<uint32_t, uint32_t>[]>(numChunks);
  parallelFor(0, numChunks, [&](size_t i) {
    InputSection *sec = inputSections[i];
    invokeOnRelocs(*sec, getNameRelocs, *sec->file, relocs.get()[i]);

    // Relocate CU offsets with .debug_info + X relocations.
    OutputChunk &chunk = chunks.get()[i];
    for (auto [j, cuOffset] : enumerate(chunk.compUnits))
      cuOffset = relocs.get()[i].lookup(cuOffset);
  });

  // Relocate string offsets in the name table with .debug_str + X relocations.
  parallelForEach(nameVecs, [&](auto &nameVec) {
    for (NameEntry &ne : nameVec)
      ne.stringOffset = relocs.get()[ne.chunkIdx].lookup(ne.stringOffset);
  });
}

template <class ELFT> void DebugNamesSection<ELFT>::writeTo(uint8_t *buf) {
  [[maybe_unused]] const uint8_t *const beginBuf = buf;
  // Write the header.
  endian::writeNext<uint32_t, ELFT::Endianness>(buf, hdr.UnitLength);
  endian::writeNext<uint16_t, ELFT::Endianness>(buf, hdr.Version);
  buf += 2; // padding
  endian::writeNext<uint32_t, ELFT::Endianness>(buf, hdr.CompUnitCount);
  endian::writeNext<uint32_t, ELFT::Endianness>(buf, hdr.LocalTypeUnitCount);
  endian::writeNext<uint32_t, ELFT::Endianness>(buf, hdr.ForeignTypeUnitCount);
  endian::writeNext<uint32_t, ELFT::Endianness>(buf, hdr.BucketCount);
  endian::writeNext<uint32_t, ELFT::Endianness>(buf, hdr.NameCount);
  endian::writeNext<uint32_t, ELFT::Endianness>(buf, hdr.AbbrevTableSize);
  endian::writeNext<uint32_t, ELFT::Endianness>(buf,
                                                hdr.AugmentationStringSize);
  memcpy(buf, hdr.AugmentationString.c_str(), hdr.AugmentationString.size());
  buf += hdr.AugmentationStringSize;

  // Write the CU list.
  for (auto &chunk : getChunks())
    for (uint32_t cuOffset : chunk.compUnits)
      endian::writeNext<uint32_t, ELFT::Endianness>(buf, cuOffset);

  // TODO: Write the local TU list, then the foreign TU list..

  // Write the hash lookup table.
  SmallVector<SmallVector<NameEntry *, 0>, 0> buckets(hdr.BucketCount);
  // Symbols enter into a bucket whose index is the hash modulo bucket_count.
  for (auto &nameVec : nameVecs)
    for (NameEntry &ne : nameVec)
      buckets[ne.hashValue % hdr.BucketCount].push_back(&ne);

  // Write buckets (accumulated bucket counts).
  uint32_t bucketIdx = 1;
  for (const SmallVector<NameEntry *, 0> &bucket : buckets) {
    if (!bucket.empty())
      endian::write32<ELFT::Endianness>(buf, bucketIdx);
    buf += 4;
    bucketIdx += bucket.size();
  }
  // Write the hashes.
  for (const SmallVector<NameEntry *, 0> &bucket : buckets)
    for (const NameEntry *e : bucket)
      endian::writeNext<uint32_t, ELFT::Endianness>(buf, e->hashValue);

  // Write the name table. The name entries are ordered by bucket_idx and
  // correspond one-to-one with the hash lookup table.
  //
  // First, write the relocated string offsets.
  for (const SmallVector<NameEntry *, 0> &bucket : buckets)
    for (const NameEntry *ne : bucket)
      endian::writeNext<uint32_t, ELFT::Endianness>(buf, ne->stringOffset);

  // Then write the entry offsets.
  for (const SmallVector<NameEntry *, 0> &bucket : buckets)
    for (const NameEntry *ne : bucket)
      endian::writeNext<uint32_t, ELFT::Endianness>(buf, ne->entryOffset);

  // Write the abbrev table.
  buf = llvm::copy(abbrevTableBuf, buf);

  // Write the entry pool. Unlike the name table, the name entries follow the
  // nameVecs order computed by `computeEntryPool`.
  for (auto &nameVec : nameVecs) {
    for (NameEntry &ne : nameVec) {
      // Write all the entries for the string.
      for (const IndexEntry &ie : ne.entries()) {
        buf += encodeULEB128(ie.abbrevCode, buf);
        for (AttrValue value : ie.attrValues) {
          switch (value.attrSize) {
          case 1:
            *buf++ = value.attrValue;
            break;
          case 2:
            endian::writeNext<uint16_t, ELFT::Endianness>(buf, value.attrValue);
            break;
          case 4:
            endian::writeNext<uint32_t, ELFT::Endianness>(buf, value.attrValue);
            break;
          default:
            llvm_unreachable("invalid attrSize");
          }
        }
      }
      ++buf; // index entry sentinel
    }
  }
  assert(uint64_t(buf - beginBuf) == size);
}

GdbIndexSection::GdbIndexSection()
    : SyntheticSection(0, SHT_PROGBITS, 1, ".gdb_index") {}

// Returns the desired size of an on-disk hash table for a .gdb_index section.
// There's a tradeoff between size and collision rate. We aim 75% utilization.
size_t GdbIndexSection::computeSymtabSize() const {
  return std::max<size_t>(NextPowerOf2(symbols.size() * 4 / 3), 1024);
}

static SmallVector<GdbIndexSection::CuEntry, 0>
readCuList(DWARFContext &dwarf) {
  SmallVector<GdbIndexSection::CuEntry, 0> ret;
  for (std::unique_ptr<DWARFUnit> &cu : dwarf.compile_units())
    ret.push_back({cu->getOffset(), cu->getLength() + 4});
  return ret;
}

static SmallVector<GdbIndexSection::AddressEntry, 0>
readAddressAreas(DWARFContext &dwarf, InputSection *sec) {
  SmallVector<GdbIndexSection::AddressEntry, 0> ret;

  uint32_t cuIdx = 0;
  for (std::unique_ptr<DWARFUnit> &cu : dwarf.compile_units()) {
    if (Error e = cu->tryExtractDIEsIfNeeded(false)) {
      warn(toString(sec) + ": " + toString(std::move(e)));
      return {};
    }
    Expected<DWARFAddressRangesVector> ranges = cu->collectAddressRanges();
    if (!ranges) {
      warn(toString(sec) + ": " + toString(ranges.takeError()));
      return {};
    }

    ArrayRef<InputSectionBase *> sections = sec->file->getSections();
    for (DWARFAddressRange &r : *ranges) {
      if (r.SectionIndex == -1ULL)
        continue;
      // Range list with zero size has no effect.
      InputSectionBase *s = sections[r.SectionIndex];
      if (s && s != &InputSection::discarded && s->isLive())
        if (r.LowPC != r.HighPC)
          ret.push_back({cast<InputSection>(s), r.LowPC, r.HighPC, cuIdx});
    }
    ++cuIdx;
  }

  return ret;
}

template <class ELFT>
static SmallVector<GdbIndexSection::NameAttrEntry, 0>
readPubNamesAndTypes(const LLDDwarfObj<ELFT> &obj,
                     const SmallVectorImpl<GdbIndexSection::CuEntry> &cus) {
  const LLDDWARFSection &pubNames = obj.getGnuPubnamesSection();
  const LLDDWARFSection &pubTypes = obj.getGnuPubtypesSection();

  SmallVector<GdbIndexSection::NameAttrEntry, 0> ret;
  for (const LLDDWARFSection *pub : {&pubNames, &pubTypes}) {
    DWARFDataExtractor data(obj, *pub, ELFT::Endianness == endianness::little,
                            ELFT::Is64Bits ? 8 : 4);
    DWARFDebugPubTable table;
    table.extract(data, /*GnuStyle=*/true, [&](Error e) {
      warn(toString(pub->sec) + ": " + toString(std::move(e)));
    });
    for (const DWARFDebugPubTable::Set &set : table.getData()) {
      // The value written into the constant pool is kind << 24 | cuIndex. As we
      // don't know how many compilation units precede this object to compute
      // cuIndex, we compute (kind << 24 | cuIndexInThisObject) instead, and add
      // the number of preceding compilation units later.
      uint32_t i = llvm::partition_point(cus,
                                         [&](GdbIndexSection::CuEntry cu) {
                                           return cu.cuOffset < set.Offset;
                                         }) -
                   cus.begin();
      for (const DWARFDebugPubTable::Entry &ent : set.Entries)
        ret.push_back({{ent.Name, computeGdbHash(ent.Name)},
                       (ent.Descriptor.toBits() << 24) | i});
    }
  }
  return ret;
}

// Create a list of symbols from a given list of symbol names and types
// by uniquifying them by name.
static std::pair<SmallVector<GdbIndexSection::GdbSymbol, 0>, size_t>
createSymbols(
    ArrayRef<SmallVector<GdbIndexSection::NameAttrEntry, 0>> nameAttrs,
    const SmallVector<GdbIndexSection::GdbChunk, 0> &chunks) {
  using GdbSymbol = GdbIndexSection::GdbSymbol;
  using NameAttrEntry = GdbIndexSection::NameAttrEntry;

  // For each chunk, compute the number of compilation units preceding it.
  uint32_t cuIdx = 0;
  std::unique_ptr<uint32_t[]> cuIdxs(new uint32_t[chunks.size()]);
  for (uint32_t i = 0, e = chunks.size(); i != e; ++i) {
    cuIdxs[i] = cuIdx;
    cuIdx += chunks[i].compilationUnits.size();
  }

  // Collect the compilation unitss for each unique name. Speed it up using
  // multi-threading as the number of symbols can be in the order of millions.
  // Shard GdbSymbols by hash's high bits.
  constexpr size_t numShards = 32;
  const size_t concurrency =
      llvm::bit_floor(std::min<size_t>(config->threadCount, numShards));
  const size_t shift = 32 - llvm::countr_zero(numShards);
  auto map =
      std::make_unique<DenseMap<CachedHashStringRef, size_t>[]>(numShards);
  auto symbols = std::make_unique<SmallVector<GdbSymbol, 0>[]>(numShards);
  parallelFor(0, concurrency, [&](size_t threadId) {
    uint32_t i = 0;
    for (ArrayRef<NameAttrEntry> entries : nameAttrs) {
      for (const NameAttrEntry &ent : entries) {
        size_t shardId = ent.name.hash() >> shift;
        if ((shardId & (concurrency - 1)) != threadId)
          continue;

        uint32_t v = ent.cuIndexAndAttrs + cuIdxs[i];
        auto [it, inserted] =
            map[shardId].try_emplace(ent.name, symbols[shardId].size());
        if (inserted)
          symbols[shardId].push_back({ent.name, {v}, 0, 0});
        else
          symbols[shardId][it->second].cuVector.push_back(v);
      }
      ++i;
    }
  });

  size_t numSymbols = 0;
  for (ArrayRef<GdbSymbol> v : ArrayRef(symbols.get(), numShards))
    numSymbols += v.size();

  // The return type is a flattened vector, so we'll copy each vector
  // contents to Ret.
  SmallVector<GdbSymbol, 0> ret;
  ret.reserve(numSymbols);
  for (SmallVector<GdbSymbol, 0> &vec :
       MutableArrayRef(symbols.get(), numShards))
    for (GdbSymbol &sym : vec)
      ret.push_back(std::move(sym));

  // CU vectors and symbol names are adjacent in the output file.
  // We can compute their offsets in the output file now.
  size_t off = 0;
  for (GdbSymbol &sym : ret) {
    sym.cuVectorOff = off;
    off += (sym.cuVector.size() + 1) * 4;
  }
  for (GdbSymbol &sym : ret) {
    sym.nameOff = off;
    off += sym.name.size() + 1;
  }
  // If off overflows, the last symbol's nameOff likely overflows.
  if (!isUInt<32>(off))
    errorOrWarn("--gdb-index: constant pool size (" + Twine(off) +
                ") exceeds UINT32_MAX");

  return {ret, off};
}

// Returns a newly-created .gdb_index section.
template <class ELFT>
std::unique_ptr<GdbIndexSection> GdbIndexSection::create() {
  llvm::TimeTraceScope timeScope("Create gdb index");

  // Collect InputFiles with .debug_info. See the comment in
  // LLDDwarfObj<ELFT>::LLDDwarfObj. If we do lightweight parsing in the future,
  // note that isec->data() may uncompress the full content, which should be
  // parallelized.
  SetVector<InputFile *> files;
  for (InputSectionBase *s : ctx.inputSections) {
    InputSection *isec = dyn_cast<InputSection>(s);
    if (!isec)
      continue;
    // .debug_gnu_pub{names,types} are useless in executables.
    // They are present in input object files solely for creating
    // a .gdb_index. So we can remove them from the output.
    if (s->name == ".debug_gnu_pubnames" || s->name == ".debug_gnu_pubtypes")
      s->markDead();
    else if (isec->name == ".debug_info")
      files.insert(isec->file);
  }
  // Drop .rel[a].debug_gnu_pub{names,types} for --emit-relocs.
  llvm::erase_if(ctx.inputSections, [](InputSectionBase *s) {
    if (auto *isec = dyn_cast<InputSection>(s))
      if (InputSectionBase *rel = isec->getRelocatedSection())
        return !rel->isLive();
    return !s->isLive();
  });

  SmallVector<GdbChunk, 0> chunks(files.size());
  SmallVector<SmallVector<NameAttrEntry, 0>, 0> nameAttrs(files.size());

  parallelFor(0, files.size(), [&](size_t i) {
    // To keep memory usage low, we don't want to keep cached DWARFContext, so
    // avoid getDwarf() here.
    ObjFile<ELFT> *file = cast<ObjFile<ELFT>>(files[i]);
    DWARFContext dwarf(std::make_unique<LLDDwarfObj<ELFT>>(file));
    auto &dobj = static_cast<const LLDDwarfObj<ELFT> &>(dwarf.getDWARFObj());

    // If the are multiple compile units .debug_info (very rare ld -r --unique),
    // this only picks the last one. Other address ranges are lost.
    chunks[i].sec = dobj.getInfoSection();
    chunks[i].compilationUnits = readCuList(dwarf);
    chunks[i].addressAreas = readAddressAreas(dwarf, chunks[i].sec);
    nameAttrs[i] = readPubNamesAndTypes<ELFT>(dobj, chunks[i].compilationUnits);
  });

  auto ret = std::make_unique<GdbIndexSection>();
  ret->chunks = std::move(chunks);
  std::tie(ret->symbols, ret->size) = createSymbols(nameAttrs, ret->chunks);

  // Count the areas other than the constant pool.
  ret->size += sizeof(GdbIndexHeader) + ret->computeSymtabSize() * 8;
  for (GdbChunk &chunk : ret->chunks)
    ret->size +=
        chunk.compilationUnits.size() * 16 + chunk.addressAreas.size() * 20;

  return ret;
}

void GdbIndexSection::writeTo(uint8_t *buf) {
  // Write the header.
  auto *hdr = reinterpret_cast<GdbIndexHeader *>(buf);
  uint8_t *start = buf;
  hdr->version = 7;
  buf += sizeof(*hdr);

  // Write the CU list.
  hdr->cuListOff = buf - start;
  for (GdbChunk &chunk : chunks) {
    for (CuEntry &cu : chunk.compilationUnits) {
      write64le(buf, chunk.sec->outSecOff + cu.cuOffset);
      write64le(buf + 8, cu.cuLength);
      buf += 16;
    }
  }

  // Write the address area.
  hdr->cuTypesOff = buf - start;
  hdr->addressAreaOff = buf - start;
  uint32_t cuOff = 0;
  for (GdbChunk &chunk : chunks) {
    for (AddressEntry &e : chunk.addressAreas) {
      // In the case of ICF there may be duplicate address range entries.
      const uint64_t baseAddr = e.section->repl->getVA(0);
      write64le(buf, baseAddr + e.lowAddress);
      write64le(buf + 8, baseAddr + e.highAddress);
      write32le(buf + 16, e.cuIndex + cuOff);
      buf += 20;
    }
    cuOff += chunk.compilationUnits.size();
  }

  // Write the on-disk open-addressing hash table containing symbols.
  hdr->symtabOff = buf - start;
  size_t symtabSize = computeSymtabSize();
  uint32_t mask = symtabSize - 1;

  for (GdbSymbol &sym : symbols) {
    uint32_t h = sym.name.hash();
    uint32_t i = h & mask;
    uint32_t step = ((h * 17) & mask) | 1;

    while (read32le(buf + i * 8))
      i = (i + step) & mask;

    write32le(buf + i * 8, sym.nameOff);
    write32le(buf + i * 8 + 4, sym.cuVectorOff);
  }

  buf += symtabSize * 8;

  // Write the string pool.
  hdr->constantPoolOff = buf - start;
  parallelForEach(symbols, [&](GdbSymbol &sym) {
    memcpy(buf + sym.nameOff, sym.name.data(), sym.name.size());
  });

  // Write the CU vectors.
  for (GdbSymbol &sym : symbols) {
    write32le(buf, sym.cuVector.size());
    buf += 4;
    for (uint32_t val : sym.cuVector) {
      write32le(buf, val);
      buf += 4;
    }
  }
}

bool GdbIndexSection::isNeeded() const { return !chunks.empty(); }

EhFrameHeader::EhFrameHeader()
    : SyntheticSection(SHF_ALLOC, SHT_PROGBITS, 4, ".eh_frame_hdr") {}

void EhFrameHeader::writeTo(uint8_t *buf) {
  // Unlike most sections, the EhFrameHeader section is written while writing
  // another section, namely EhFrameSection, which calls the write() function
  // below from its writeTo() function. This is necessary because the contents
  // of EhFrameHeader depend on the relocated contents of EhFrameSection and we
  // don't know which order the sections will be written in.
}

// .eh_frame_hdr contains a binary search table of pointers to FDEs.
// Each entry of the search table consists of two values,
// the starting PC from where FDEs covers, and the FDE's address.
// It is sorted by PC.
void EhFrameHeader::write() {
  uint8_t *buf = Out::bufferStart + getParent()->offset + outSecOff;
  using FdeData = EhFrameSection::FdeData;
  SmallVector<FdeData, 0> fdes = getPartition().ehFrame->getFdeData();

  buf[0] = 1;
  buf[1] = DW_EH_PE_pcrel | DW_EH_PE_sdata4;
  buf[2] = DW_EH_PE_udata4;
  buf[3] = DW_EH_PE_datarel | DW_EH_PE_sdata4;
  write32(buf + 4,
          getPartition().ehFrame->getParent()->addr - this->getVA() - 4);
  write32(buf + 8, fdes.size());
  buf += 12;

  for (FdeData &fde : fdes) {
    write32(buf, fde.pcRel);
    write32(buf + 4, fde.fdeVARel);
    buf += 8;
  }
}

size_t EhFrameHeader::getSize() const {
  // .eh_frame_hdr has a 12 bytes header followed by an array of FDEs.
  return 12 + getPartition().ehFrame->numFdes * 8;
}

bool EhFrameHeader::isNeeded() const {
  return isLive() && getPartition().ehFrame->isNeeded();
}

VersionDefinitionSection::VersionDefinitionSection()
    : SyntheticSection(SHF_ALLOC, SHT_GNU_verdef, sizeof(uint32_t),
                       ".gnu.version_d") {}

StringRef VersionDefinitionSection::getFileDefName() {
  if (!getPartition().name.empty())
    return getPartition().name;
  if (!config->soName.empty())
    return config->soName;
  return config->outputFile;
}

void VersionDefinitionSection::finalizeContents() {
  fileDefNameOff = getPartition().dynStrTab->addString(getFileDefName());
  for (const VersionDefinition &v : namedVersionDefs())
    verDefNameOffs.push_back(getPartition().dynStrTab->addString(v.name));

  if (OutputSection *sec = getPartition().dynStrTab->getParent())
    getParent()->link = sec->sectionIndex;

  // sh_info should be set to the number of definitions. This fact is missed in
  // documentation, but confirmed by binutils community:
  // https://sourceware.org/ml/binutils/2014-11/msg00355.html
  getParent()->info = getVerDefNum();
}

void VersionDefinitionSection::writeOne(uint8_t *buf, uint32_t index,
                                        StringRef name, size_t nameOff) {
  uint16_t flags = index == 1 ? VER_FLG_BASE : 0;

  // Write a verdef.
  write16(buf, 1);                  // vd_version
  write16(buf + 2, flags);          // vd_flags
  write16(buf + 4, index);          // vd_ndx
  write16(buf + 6, 1);              // vd_cnt
  write32(buf + 8, hashSysV(name)); // vd_hash
  write32(buf + 12, 20);            // vd_aux
  write32(buf + 16, 28);            // vd_next

  // Write a veraux.
  write32(buf + 20, nameOff); // vda_name
  write32(buf + 24, 0);       // vda_next
}

void VersionDefinitionSection::writeTo(uint8_t *buf) {
  writeOne(buf, 1, getFileDefName(), fileDefNameOff);

  auto nameOffIt = verDefNameOffs.begin();
  for (const VersionDefinition &v : namedVersionDefs()) {
    buf += EntrySize;
    writeOne(buf, v.id, v.name, *nameOffIt++);
  }

  // Need to terminate the last version definition.
  write32(buf + 16, 0); // vd_next
}

size_t VersionDefinitionSection::getSize() const {
  return EntrySize * getVerDefNum();
}

// .gnu.version is a table where each entry is 2 byte long.
VersionTableSection::VersionTableSection()
    : SyntheticSection(SHF_ALLOC, SHT_GNU_versym, sizeof(uint16_t),
                       ".gnu.version") {
  this->entsize = 2;
}

void VersionTableSection::finalizeContents() {
  // At the moment of june 2016 GNU docs does not mention that sh_link field
  // should be set, but Sun docs do. Also readelf relies on this field.
  getParent()->link = getPartition().dynSymTab->getParent()->sectionIndex;
}

size_t VersionTableSection::getSize() const {
  return (getPartition().dynSymTab->getSymbols().size() + 1) * 2;
}

void VersionTableSection::writeTo(uint8_t *buf) {
  buf += 2;
  for (const SymbolTableEntry &s : getPartition().dynSymTab->getSymbols()) {
    // For an unextracted lazy symbol (undefined weak), it must have been
    // converted to Undefined and have VER_NDX_GLOBAL version here.
    assert(!s.sym->isLazy());
    write16(buf, s.sym->versionId);
    buf += 2;
  }
}

bool VersionTableSection::isNeeded() const {
  return isLive() &&
         (getPartition().verDef || getPartition().verNeed->isNeeded());
}

void elf::addVerneed(Symbol *ss) {
  auto &file = cast<SharedFile>(*ss->file);
  if (ss->versionId == VER_NDX_GLOBAL)
    return;

  if (file.vernauxs.empty())
    file.vernauxs.resize(file.verdefs.size());

  // Select a version identifier for the vernaux data structure, if we haven't
  // already allocated one. The verdef identifiers cover the range
  // [1..getVerDefNum()]; this causes the vernaux identifiers to start from
  // getVerDefNum()+1.
  if (file.vernauxs[ss->versionId] == 0)
    file.vernauxs[ss->versionId] = ++SharedFile::vernauxNum + getVerDefNum();

  ss->versionId = file.vernauxs[ss->versionId];
}

template <class ELFT>
VersionNeedSection<ELFT>::VersionNeedSection()
    : SyntheticSection(SHF_ALLOC, SHT_GNU_verneed, sizeof(uint32_t),
                       ".gnu.version_r") {}

template <class ELFT> void VersionNeedSection<ELFT>::finalizeContents() {
  for (SharedFile *f : ctx.sharedFiles) {
    if (f->vernauxs.empty())
      continue;
    verneeds.emplace_back();
    Verneed &vn = verneeds.back();
    vn.nameStrTab = getPartition().dynStrTab->addString(f->soName);
    bool isLibc = config->relrGlibc && f->soName.starts_with("libc.so.");
    bool isGlibc2 = false;
    for (unsigned i = 0; i != f->vernauxs.size(); ++i) {
      if (f->vernauxs[i] == 0)
        continue;
      auto *verdef =
          reinterpret_cast<const typename ELFT::Verdef *>(f->verdefs[i]);
      StringRef ver(f->getStringTable().data() + verdef->getAux()->vda_name);
      if (isLibc && ver.starts_with("GLIBC_2."))
        isGlibc2 = true;
      vn.vernauxs.push_back({verdef->vd_hash, f->vernauxs[i],
                             getPartition().dynStrTab->addString(ver)});
    }
    if (isGlibc2) {
      const char *ver = "GLIBC_ABI_DT_RELR";
      vn.vernauxs.push_back({hashSysV(ver),
                             ++SharedFile::vernauxNum + getVerDefNum(),
                             getPartition().dynStrTab->addString(ver)});
    }
  }

  if (OutputSection *sec = getPartition().dynStrTab->getParent())
    getParent()->link = sec->sectionIndex;
  getParent()->info = verneeds.size();
}

template <class ELFT> void VersionNeedSection<ELFT>::writeTo(uint8_t *buf) {
  // The Elf_Verneeds need to appear first, followed by the Elf_Vernauxs.
  auto *verneed = reinterpret_cast<Elf_Verneed *>(buf);
  auto *vernaux = reinterpret_cast<Elf_Vernaux *>(verneed + verneeds.size());

  for (auto &vn : verneeds) {
    // Create an Elf_Verneed for this DSO.
    verneed->vn_version = 1;
    verneed->vn_cnt = vn.vernauxs.size();
    verneed->vn_file = vn.nameStrTab;
    verneed->vn_aux =
        reinterpret_cast<char *>(vernaux) - reinterpret_cast<char *>(verneed);
    verneed->vn_next = sizeof(Elf_Verneed);
    ++verneed;

    // Create the Elf_Vernauxs for this Elf_Verneed.
    for (auto &vna : vn.vernauxs) {
      vernaux->vna_hash = vna.hash;
      vernaux->vna_flags = 0;
      vernaux->vna_other = vna.verneedIndex;
      vernaux->vna_name = vna.nameStrTab;
      vernaux->vna_next = sizeof(Elf_Vernaux);
      ++vernaux;
    }

    vernaux[-1].vna_next = 0;
  }
  verneed[-1].vn_next = 0;
}

template <class ELFT> size_t VersionNeedSection<ELFT>::getSize() const {
  return verneeds.size() * sizeof(Elf_Verneed) +
         SharedFile::vernauxNum * sizeof(Elf_Vernaux);
}

template <class ELFT> bool VersionNeedSection<ELFT>::isNeeded() const {
  return isLive() && SharedFile::vernauxNum != 0;
}

void MergeSyntheticSection::addSection(MergeInputSection *ms) {
  ms->parent = this;
  sections.push_back(ms);
  assert(addralign == ms->addralign || !(ms->flags & SHF_STRINGS));
  addralign = std::max(addralign, ms->addralign);
}

MergeTailSection::MergeTailSection(StringRef name, uint32_t type,
                                   uint64_t flags, uint32_t alignment)
    : MergeSyntheticSection(name, type, flags, alignment),
      builder(StringTableBuilder::RAW, llvm::Align(alignment)) {}

size_t MergeTailSection::getSize() const { return builder.getSize(); }

void MergeTailSection::writeTo(uint8_t *buf) { builder.write(buf); }

void MergeTailSection::finalizeContents() {
  // Add all string pieces to the string table builder to create section
  // contents.
  for (MergeInputSection *sec : sections)
    for (size_t i = 0, e = sec->pieces.size(); i != e; ++i)
      if (sec->pieces[i].live)
        builder.add(sec->getData(i));

  // Fix the string table content. After this, the contents will never change.
  builder.finalize();

  // finalize() fixed tail-optimized strings, so we can now get
  // offsets of strings. Get an offset for each string and save it
  // to a corresponding SectionPiece for easy access.
  for (MergeInputSection *sec : sections)
    for (size_t i = 0, e = sec->pieces.size(); i != e; ++i)
      if (sec->pieces[i].live)
        sec->pieces[i].outputOff = builder.getOffset(sec->getData(i));
}

void MergeNoTailSection::writeTo(uint8_t *buf) {
  parallelFor(0, numShards,
              [&](size_t i) { shards[i].write(buf + shardOffsets[i]); });
}

// This function is very hot (i.e. it can take several seconds to finish)
// because sometimes the number of inputs is in an order of magnitude of
// millions. So, we use multi-threading.
//
// For any strings S and T, we know S is not mergeable with T if S's hash
// value is different from T's. If that's the case, we can safely put S and
// T into different string builders without worrying about merge misses.
// We do it in parallel.
void MergeNoTailSection::finalizeContents() {
  // Initializes string table builders.
  for (size_t i = 0; i < numShards; ++i)
    shards.emplace_back(StringTableBuilder::RAW, llvm::Align(addralign));

  // Concurrency level. Must be a power of 2 to avoid expensive modulo
  // operations in the following tight loop.
  const size_t concurrency =
      llvm::bit_floor(std::min<size_t>(config->threadCount, numShards));

  // Add section pieces to the builders.
  parallelFor(0, concurrency, [&](size_t threadId) {
    for (MergeInputSection *sec : sections) {
      for (size_t i = 0, e = sec->pieces.size(); i != e; ++i) {
        if (!sec->pieces[i].live)
          continue;
        size_t shardId = getShardId(sec->pieces[i].hash);
        if ((shardId & (concurrency - 1)) == threadId)
          sec->pieces[i].outputOff = shards[shardId].add(sec->getData(i));
      }
    }
  });

  // Compute an in-section offset for each shard.
  size_t off = 0;
  for (size_t i = 0; i < numShards; ++i) {
    shards[i].finalizeInOrder();
    if (shards[i].getSize() > 0)
      off = alignToPowerOf2(off, addralign);
    shardOffsets[i] = off;
    off += shards[i].getSize();
  }
  size = off;

  // So far, section pieces have offsets from beginning of shards, but
  // we want offsets from beginning of the whole section. Fix them.
  parallelForEach(sections, [&](MergeInputSection *sec) {
    for (size_t i = 0, e = sec->pieces.size(); i != e; ++i)
      if (sec->pieces[i].live)
        sec->pieces[i].outputOff +=
            shardOffsets[getShardId(sec->pieces[i].hash)];
  });
}

template <class ELFT> void elf::splitSections() {
  llvm::TimeTraceScope timeScope("Split sections");
  // splitIntoPieces needs to be called on each MergeInputSection
  // before calling finalizeContents().
  parallelForEach(ctx.objectFiles, [](ELFFileBase *file) {
    for (InputSectionBase *sec : file->getSections()) {
      if (!sec)
        continue;
      if (auto *s = dyn_cast<MergeInputSection>(sec))
        s->splitIntoPieces();
      else if (auto *eh = dyn_cast<EhInputSection>(sec))
        eh->split<ELFT>();
    }
  });
}

void elf::combineEhSections() {
  llvm::TimeTraceScope timeScope("Combine EH sections");
  for (EhInputSection *sec : ctx.ehInputSections) {
    EhFrameSection &eh = *sec->getPartition().ehFrame;
    sec->parent = &eh;
    eh.addralign = std::max(eh.addralign, sec->addralign);
    eh.sections.push_back(sec);
    llvm::append_range(eh.dependentSections, sec->dependentSections);
  }

  if (!mainPart->armExidx)
    return;
  llvm::erase_if(ctx.inputSections, [](InputSectionBase *s) {
    // Ignore dead sections and the partition end marker (.part.end),
    // whose partition number is out of bounds.
    if (!s->isLive() || s->partition == 255)
      return false;
    Partition &part = s->getPartition();
    return s->kind() == SectionBase::Regular && part.armExidx &&
           part.armExidx->addSection(cast<InputSection>(s));
  });
}

MipsRldMapSection::MipsRldMapSection()
    : SyntheticSection(SHF_ALLOC | SHF_WRITE, SHT_PROGBITS, config->wordsize,
                       ".rld_map") {}

ARMExidxSyntheticSection::ARMExidxSyntheticSection()
    : SyntheticSection(SHF_ALLOC | SHF_LINK_ORDER, SHT_ARM_EXIDX,
                       config->wordsize, ".ARM.exidx") {}

static InputSection *findExidxSection(InputSection *isec) {
  for (InputSection *d : isec->dependentSections)
    if (d->type == SHT_ARM_EXIDX && d->isLive())
      return d;
  return nullptr;
}

static bool isValidExidxSectionDep(InputSection *isec) {
  return (isec->flags & SHF_ALLOC) && (isec->flags & SHF_EXECINSTR) &&
         isec->getSize() > 0;
}

bool ARMExidxSyntheticSection::addSection(InputSection *isec) {
  if (isec->type == SHT_ARM_EXIDX) {
    if (InputSection *dep = isec->getLinkOrderDep())
      if (isValidExidxSectionDep(dep)) {
        exidxSections.push_back(isec);
        // Every exidxSection is 8 bytes, we need an estimate of
        // size before assignAddresses can be called. Final size
        // will only be known after finalize is called.
        size += 8;
      }
    return true;
  }

  if (isValidExidxSectionDep(isec)) {
    executableSections.push_back(isec);
    return false;
  }

  // FIXME: we do not output a relocation section when --emit-relocs is used
  // as we do not have relocation sections for linker generated table entries
  // and we would have to erase at a late stage relocations from merged entries.
  // Given that exception tables are already position independent and a binary
  // analyzer could derive the relocations we choose to erase the relocations.
  if (config->emitRelocs && isec->type == SHT_REL)
    if (InputSectionBase *ex = isec->getRelocatedSection())
      if (isa<InputSection>(ex) && ex->type == SHT_ARM_EXIDX)
        return true;

  return false;
}

// References to .ARM.Extab Sections have bit 31 clear and are not the
// special EXIDX_CANTUNWIND bit-pattern.
static bool isExtabRef(uint32_t unwind) {
  return (unwind & 0x80000000) == 0 && unwind != 0x1;
}

// Return true if the .ARM.exidx section Cur can be merged into the .ARM.exidx
// section Prev, where Cur follows Prev in the table. This can be done if the
// unwinding instructions in Cur are identical to Prev. Linker generated
// EXIDX_CANTUNWIND entries are represented by nullptr as they do not have an
// InputSection.
static bool isDuplicateArmExidxSec(InputSection *prev, InputSection *cur) {
  // Get the last table Entry from the previous .ARM.exidx section. If Prev is
  // nullptr then it will be a synthesized EXIDX_CANTUNWIND entry.
  uint32_t prevUnwind = 1;
  if (prev)
    prevUnwind = read32(prev->content().data() + prev->content().size() - 4);
  if (isExtabRef(prevUnwind))
    return false;

  // We consider the unwind instructions of an .ARM.exidx table entry
  // a duplicate if the previous unwind instructions if:
  // - Both are the special EXIDX_CANTUNWIND.
  // - Both are the same inline unwind instructions.
  // We do not attempt to follow and check links into .ARM.extab tables as
  // consecutive identical entries are rare and the effort to check that they
  // are identical is high.

  // If Cur is nullptr then this is synthesized EXIDX_CANTUNWIND entry.
  if (cur == nullptr)
    return prevUnwind == 1;

  for (uint32_t offset = 4; offset < (uint32_t)cur->content().size(); offset +=8) {
    uint32_t curUnwind = read32(cur->content().data() + offset);
    if (isExtabRef(curUnwind) || curUnwind != prevUnwind)
      return false;
  }
  // All table entries in this .ARM.exidx Section can be merged into the
  // previous Section.
  return true;
}

// The .ARM.exidx table must be sorted in ascending order of the address of the
// functions the table describes. std::optionally duplicate adjacent table
// entries can be removed. At the end of the function the executableSections
// must be sorted in ascending order of address, Sentinel is set to the
// InputSection with the highest address and any InputSections that have
// mergeable .ARM.exidx table entries are removed from it.
void ARMExidxSyntheticSection::finalizeContents() {
  // Ensure that any fixed-point iterations after the first see the original set
  // of sections.
  if (!originalExecutableSections.empty())
    executableSections = originalExecutableSections;
  else if (config->enableNonContiguousRegions)
    originalExecutableSections = executableSections;

  // The executableSections and exidxSections that we use to derive the final
  // contents of this SyntheticSection are populated before
  // processSectionCommands() and ICF. A /DISCARD/ entry in SECTIONS command or
  // ICF may remove executable InputSections and their dependent .ARM.exidx
  // section that we recorded earlier.
  auto isDiscarded = [](const InputSection *isec) { return !isec->isLive(); };
  llvm::erase_if(exidxSections, isDiscarded);
  // We need to remove discarded InputSections and InputSections without
  // .ARM.exidx sections that if we generated the .ARM.exidx it would be out
  // of range.
  auto isDiscardedOrOutOfRange = [this](InputSection *isec) {
    if (!isec->isLive())
      return true;
    if (findExidxSection(isec))
      return false;
    int64_t off = static_cast<int64_t>(isec->getVA() - getVA());
    return off != llvm::SignExtend64(off, 31);
  };
  llvm::erase_if(executableSections, isDiscardedOrOutOfRange);

  // Sort the executable sections that may or may not have associated
  // .ARM.exidx sections by order of ascending address. This requires the
  // relative positions of InputSections and OutputSections to be known.
  auto compareByFilePosition = [](const InputSection *a,
                                  const InputSection *b) {
    OutputSection *aOut = a->getParent();
    OutputSection *bOut = b->getParent();

    if (aOut != bOut)
      return aOut->addr < bOut->addr;
    return a->outSecOff < b->outSecOff;
  };
  llvm::stable_sort(executableSections, compareByFilePosition);
  sentinel = executableSections.back();
  // std::optionally merge adjacent duplicate entries.
  if (config->mergeArmExidx) {
    SmallVector<InputSection *, 0> selectedSections;
    selectedSections.reserve(executableSections.size());
    selectedSections.push_back(executableSections[0]);
    size_t prev = 0;
    for (size_t i = 1; i < executableSections.size(); ++i) {
      InputSection *ex1 = findExidxSection(executableSections[prev]);
      InputSection *ex2 = findExidxSection(executableSections[i]);
      if (!isDuplicateArmExidxSec(ex1, ex2)) {
        selectedSections.push_back(executableSections[i]);
        prev = i;
      }
    }
    executableSections = std::move(selectedSections);
  }
  // offset is within the SyntheticSection.
  size_t offset = 0;
  size = 0;
  for (InputSection *isec : executableSections) {
    if (InputSection *d = findExidxSection(isec)) {
      d->outSecOff = offset;
      d->parent = getParent();
      offset += d->getSize();
    } else {
      offset += 8;
    }
  }
  // Size includes Sentinel.
  size = offset + 8;
}

InputSection *ARMExidxSyntheticSection::getLinkOrderDep() const {
  return executableSections.front();
}

// To write the .ARM.exidx table from the ExecutableSections we have three cases
// 1.) The InputSection has a .ARM.exidx InputSection in its dependent sections.
//     We write the .ARM.exidx section contents and apply its relocations.
// 2.) The InputSection does not have a dependent .ARM.exidx InputSection. We
//     must write the contents of an EXIDX_CANTUNWIND directly. We use the
//     start of the InputSection as the purpose of the linker generated
//     section is to terminate the address range of the previous entry.
// 3.) A trailing EXIDX_CANTUNWIND sentinel section is required at the end of
//     the table to terminate the address range of the final entry.
void ARMExidxSyntheticSection::writeTo(uint8_t *buf) {

  // A linker generated CANTUNWIND entry is made up of two words:
  // 0x0 with R_ARM_PREL31 relocation to target.
  // 0x1 with EXIDX_CANTUNWIND.
  uint64_t offset = 0;
  for (InputSection *isec : executableSections) {
    assert(isec->getParent() != nullptr);
    if (InputSection *d = findExidxSection(isec)) {
      for (int dataOffset = 0; dataOffset != (int)d->content().size();
           dataOffset += 4)
        write32(buf + offset + dataOffset,
                read32(d->content().data() + dataOffset));
      // Recalculate outSecOff as finalizeAddressDependentContent()
      // may have altered syntheticSection outSecOff.
      d->outSecOff = offset + outSecOff;
      target->relocateAlloc(*d, buf + offset);
      offset += d->getSize();
    } else {
      // A Linker generated CANTUNWIND section.
      write32(buf + offset + 0, 0x0);
      write32(buf + offset + 4, 0x1);
      uint64_t s = isec->getVA();
      uint64_t p = getVA() + offset;
      target->relocateNoSym(buf + offset, R_ARM_PREL31, s - p);
      offset += 8;
    }
  }
  // Write Sentinel CANTUNWIND entry.
  write32(buf + offset + 0, 0x0);
  write32(buf + offset + 4, 0x1);
  uint64_t s = sentinel->getVA(sentinel->getSize());
  uint64_t p = getVA() + offset;
  target->relocateNoSym(buf + offset, R_ARM_PREL31, s - p);
  assert(size == offset + 8);
}

bool ARMExidxSyntheticSection::isNeeded() const {
  return llvm::any_of(exidxSections,
                      [](InputSection *isec) { return isec->isLive(); });
}

ThunkSection::ThunkSection(OutputSection *os, uint64_t off)
    : SyntheticSection(SHF_ALLOC | SHF_EXECINSTR, SHT_PROGBITS,
                       config->emachine == EM_PPC64 ? 16 : 4, ".text.thunk") {
  this->parent = os;
  this->outSecOff = off;
}

size_t ThunkSection::getSize() const {
  if (roundUpSizeForErrata)
    return alignTo(size, 4096);
  return size;
}

void ThunkSection::addThunk(Thunk *t) {
  thunks.push_back(t);
  t->addSymbols(*this);
}

void ThunkSection::writeTo(uint8_t *buf) {
  for (Thunk *t : thunks)
    t->writeTo(buf + t->offset);
}

InputSection *ThunkSection::getTargetInputSection() const {
  if (thunks.empty())
    return nullptr;
  const Thunk *t = thunks.front();
  return t->getTargetInputSection();
}

bool ThunkSection::assignOffsets() {
  uint64_t off = 0;
  for (Thunk *t : thunks) {
    off = alignToPowerOf2(off, t->alignment);
    t->setOffset(off);
    uint32_t size = t->size();
    t->getThunkTargetSym()->size = size;
    off += size;
  }
  bool changed = off != size;
  size = off;
  return changed;
}

PPC32Got2Section::PPC32Got2Section()
    : SyntheticSection(SHF_ALLOC | SHF_WRITE, SHT_PROGBITS, 4, ".got2") {}

bool PPC32Got2Section::isNeeded() const {
  // See the comment below. This is not needed if there is no other
  // InputSection.
  for (SectionCommand *cmd : getParent()->commands)
    if (auto *isd = dyn_cast<InputSectionDescription>(cmd))
      for (InputSection *isec : isd->sections)
        if (isec != this)
          return true;
  return false;
}

void PPC32Got2Section::finalizeContents() {
  // PPC32 may create multiple GOT sections for -fPIC/-fPIE, one per file in
  // .got2 . This function computes outSecOff of each .got2 to be used in
  // PPC32PltCallStub::writeTo(). The purpose of this empty synthetic section is
  // to collect input sections named ".got2".
  for (SectionCommand *cmd : getParent()->commands)
    if (auto *isd = dyn_cast<InputSectionDescription>(cmd)) {
      for (InputSection *isec : isd->sections) {
        // isec->file may be nullptr for MergeSyntheticSection.
        if (isec != this && isec->file)
          isec->file->ppc32Got2 = isec;
      }
    }
}

// If linking position-dependent code then the table will store the addresses
// directly in the binary so the section has type SHT_PROGBITS. If linking
// position-independent code the section has type SHT_NOBITS since it will be
// allocated and filled in by the dynamic linker.
PPC64LongBranchTargetSection::PPC64LongBranchTargetSection()
    : SyntheticSection(SHF_ALLOC | SHF_WRITE,
                       config->isPic ? SHT_NOBITS : SHT_PROGBITS, 8,
                       ".branch_lt") {}

uint64_t PPC64LongBranchTargetSection::getEntryVA(const Symbol *sym,
                                                  int64_t addend) {
  return getVA() + entry_index.find({sym, addend})->second * 8;
}

std::optional<uint32_t>
PPC64LongBranchTargetSection::addEntry(const Symbol *sym, int64_t addend) {
  auto res =
      entry_index.try_emplace(std::make_pair(sym, addend), entries.size());
  if (!res.second)
    return std::nullopt;
  entries.emplace_back(sym, addend);
  return res.first->second;
}

size_t PPC64LongBranchTargetSection::getSize() const {
  return entries.size() * 8;
}

void PPC64LongBranchTargetSection::writeTo(uint8_t *buf) {
  // If linking non-pic we have the final addresses of the targets and they get
  // written to the table directly. For pic the dynamic linker will allocate
  // the section and fill it.
  if (config->isPic)
    return;

  for (auto entry : entries) {
    const Symbol *sym = entry.first;
    int64_t addend = entry.second;
    assert(sym->getVA());
    // Need calls to branch to the local entry-point since a long-branch
    // must be a local-call.
    write64(buf, sym->getVA(addend) +
                     getPPC64GlobalEntryToLocalEntryOffset(sym->stOther));
    buf += 8;
  }
}

bool PPC64LongBranchTargetSection::isNeeded() const {
  // `removeUnusedSyntheticSections()` is called before thunk allocation which
  // is too early to determine if this section will be empty or not. We need
  // Finalized to keep the section alive until after thunk creation. Finalized
  // only gets set to true once `finalizeSections()` is called after thunk
  // creation. Because of this, if we don't create any long-branch thunks we end
  // up with an empty .branch_lt section in the binary.
  return !finalized || !entries.empty();
}

static uint8_t getAbiVersion() {
  // MIPS non-PIC executable gets ABI version 1.
  if (config->emachine == EM_MIPS) {
    if (!config->isPic && !config->relocatable &&
        (config->eflags & (EF_MIPS_PIC | EF_MIPS_CPIC)) == EF_MIPS_CPIC)
      return 1;
    return 0;
  }

  if (config->emachine == EM_AMDGPU && !ctx.objectFiles.empty()) {
    uint8_t ver = ctx.objectFiles[0]->abiVersion;
    for (InputFile *file : ArrayRef(ctx.objectFiles).slice(1))
      if (file->abiVersion != ver)
        error("incompatible ABI version: " + toString(file));
    return ver;
  }

  return 0;
}

template <typename ELFT> void elf::writeEhdr(uint8_t *buf, Partition &part) {
  memcpy(buf, "\177ELF", 4);

  auto *eHdr = reinterpret_cast<typename ELFT::Ehdr *>(buf);
  eHdr->e_ident[EI_CLASS] = ELFT::Is64Bits ? ELFCLASS64 : ELFCLASS32;
  eHdr->e_ident[EI_DATA] =
      ELFT::Endianness == endianness::little ? ELFDATA2LSB : ELFDATA2MSB;
  eHdr->e_ident[EI_VERSION] = EV_CURRENT;
  eHdr->e_ident[EI_OSABI] = config->osabi;
  eHdr->e_ident[EI_ABIVERSION] = getAbiVersion();
  eHdr->e_machine = config->emachine;
  eHdr->e_version = EV_CURRENT;
  eHdr->e_flags = config->eflags;
  eHdr->e_ehsize = sizeof(typename ELFT::Ehdr);
  eHdr->e_phnum = part.phdrs.size();
  eHdr->e_shentsize = sizeof(typename ELFT::Shdr);

  if (!config->relocatable) {
    eHdr->e_phoff = sizeof(typename ELFT::Ehdr);
    eHdr->e_phentsize = sizeof(typename ELFT::Phdr);
  }
}

template <typename ELFT> void elf::writePhdrs(uint8_t *buf, Partition &part) {
  // Write the program header table.
  auto *hBuf = reinterpret_cast<typename ELFT::Phdr *>(buf);
  for (PhdrEntry *p : part.phdrs) {
    hBuf->p_type = p->p_type;
    hBuf->p_flags = p->p_flags;
    hBuf->p_offset = p->p_offset;
    hBuf->p_vaddr = p->p_vaddr;
    hBuf->p_paddr = p->p_paddr;
    hBuf->p_filesz = p->p_filesz;
    hBuf->p_memsz = p->p_memsz;
    hBuf->p_align = p->p_align;
    ++hBuf;
  }
}

template <typename ELFT>
PartitionElfHeaderSection<ELFT>::PartitionElfHeaderSection()
    : SyntheticSection(SHF_ALLOC, SHT_LLVM_PART_EHDR, 1, "") {}

template <typename ELFT>
size_t PartitionElfHeaderSection<ELFT>::getSize() const {
  return sizeof(typename ELFT::Ehdr);
}

template <typename ELFT>
void PartitionElfHeaderSection<ELFT>::writeTo(uint8_t *buf) {
  writeEhdr<ELFT>(buf, getPartition());

  // Loadable partitions are always ET_DYN.
  auto *eHdr = reinterpret_cast<typename ELFT::Ehdr *>(buf);
  eHdr->e_type = ET_DYN;
}

template <typename ELFT>
PartitionProgramHeadersSection<ELFT>::PartitionProgramHeadersSection()
    : SyntheticSection(SHF_ALLOC, SHT_LLVM_PART_PHDR, 1, ".phdrs") {}

template <typename ELFT>
size_t PartitionProgramHeadersSection<ELFT>::getSize() const {
  return sizeof(typename ELFT::Phdr) * getPartition().phdrs.size();
}

template <typename ELFT>
void PartitionProgramHeadersSection<ELFT>::writeTo(uint8_t *buf) {
  writePhdrs<ELFT>(buf, getPartition());
}

PartitionIndexSection::PartitionIndexSection()
    : SyntheticSection(SHF_ALLOC, SHT_PROGBITS, 4, ".rodata") {}

size_t PartitionIndexSection::getSize() const {
  return 12 * (partitions.size() - 1);
}

void PartitionIndexSection::finalizeContents() {
  for (size_t i = 1; i != partitions.size(); ++i)
    partitions[i].nameStrTab = mainPart->dynStrTab->addString(partitions[i].name);
}

void PartitionIndexSection::writeTo(uint8_t *buf) {
  uint64_t va = getVA();
  for (size_t i = 1; i != partitions.size(); ++i) {
    write32(buf, mainPart->dynStrTab->getVA() + partitions[i].nameStrTab - va);
    write32(buf + 4, partitions[i].elfHeader->getVA() - (va + 4));

    SyntheticSection *next = i == partitions.size() - 1
                                 ? in.partEnd.get()
                                 : partitions[i + 1].elfHeader.get();
    write32(buf + 8, next->getVA() - partitions[i].elfHeader->getVA());

    va += 12;
    buf += 12;
  }
}

void InStruct::reset() {
  attributes.reset();
  riscvAttributes.reset();
  bss.reset();
  bssRelRo.reset();
  got.reset();
  gotPlt.reset();
  igotPlt.reset();
  relroPadding.reset();
  armCmseSGSection.reset();
  ppc64LongBranchTarget.reset();
  mipsAbiFlags.reset();
  mipsGot.reset();
  mipsOptions.reset();
  mipsReginfo.reset();
  mipsRldMap.reset();
  partEnd.reset();
  partIndex.reset();
  plt.reset();
  iplt.reset();
  ppc32Got2.reset();
  ibtPlt.reset();
  relaPlt.reset();
  debugNames.reset();
  gdbIndex.reset();
  shStrTab.reset();
  strTab.reset();
  symTab.reset();
  symTabShndx.reset();
}

static bool needsInterpSection() {
  return !config->relocatable && !config->shared &&
         !config->dynamicLinker.empty() && script->needsInterpSection();
}

bool elf::hasMemtag() {
  return config->emachine == EM_AARCH64 &&
         config->androidMemtagMode != ELF::NT_MEMTAG_LEVEL_NONE;
}

// Fully static executables don't support MTE globals at this point in time, as
// we currently rely on:
//   - A dynamic loader to process relocations, and
//   - Dynamic entries.
// This restriction could be removed in future by re-using some of the ideas
// that ifuncs use in fully static executables.
bool elf::canHaveMemtagGlobals() {
  return hasMemtag() &&
         (config->relocatable || config->shared || needsInterpSection());
}

constexpr char kMemtagAndroidNoteName[] = "Android";
void MemtagAndroidNote::writeTo(uint8_t *buf) {
  static_assert(
      sizeof(kMemtagAndroidNoteName) == 8,
      "Android 11 & 12 have an ABI that the note name is 8 bytes long. Keep it "
      "that way for backwards compatibility.");

  write32(buf, sizeof(kMemtagAndroidNoteName));
  write32(buf + 4, sizeof(uint32_t));
  write32(buf + 8, ELF::NT_ANDROID_TYPE_MEMTAG);
  memcpy(buf + 12, kMemtagAndroidNoteName, sizeof(kMemtagAndroidNoteName));
  buf += 12 + alignTo(sizeof(kMemtagAndroidNoteName), 4);

  uint32_t value = 0;
  value |= config->androidMemtagMode;
  if (config->androidMemtagHeap)
    value |= ELF::NT_MEMTAG_HEAP;
  // Note, MTE stack is an ABI break. Attempting to run an MTE stack-enabled
  // binary on Android 11 or 12 will result in a checkfail in the loader.
  if (config->androidMemtagStack)
    value |= ELF::NT_MEMTAG_STACK;
  write32(buf, value); // note value
}

size_t MemtagAndroidNote::getSize() const {
  return sizeof(llvm::ELF::Elf64_Nhdr) +
         /*namesz=*/alignTo(sizeof(kMemtagAndroidNoteName), 4) +
         /*descsz=*/sizeof(uint32_t);
}

void PackageMetadataNote::writeTo(uint8_t *buf) {
  write32(buf, 4);
  write32(buf + 4, config->packageMetadata.size() + 1);
  write32(buf + 8, FDO_PACKAGING_METADATA);
  memcpy(buf + 12, "FDO", 4);
  memcpy(buf + 16, config->packageMetadata.data(),
         config->packageMetadata.size());
}

size_t PackageMetadataNote::getSize() const {
  return sizeof(llvm::ELF::Elf64_Nhdr) + 4 +
         alignTo(config->packageMetadata.size() + 1, 4);
}

// Helper function, return the size of the ULEB128 for 'v', optionally writing
// it to `*(buf + offset)` if `buf` is non-null.
static size_t computeOrWriteULEB128(uint64_t v, uint8_t *buf, size_t offset) {
  if (buf)
    return encodeULEB128(v, buf + offset);
  return getULEB128Size(v);
}

// https://github.com/ARM-software/abi-aa/blob/main/memtagabielf64/memtagabielf64.rst#83encoding-of-sht_aarch64_memtag_globals_dynamic
constexpr uint64_t kMemtagStepSizeBits = 3;
constexpr uint64_t kMemtagGranuleSize = 16;
static size_t
createMemtagGlobalDescriptors(const SmallVector<const Symbol *, 0> &symbols,
                              uint8_t *buf = nullptr) {
  size_t sectionSize = 0;
  uint64_t lastGlobalEnd = 0;

  for (const Symbol *sym : symbols) {
    if (!includeInSymtab(*sym))
      continue;
    const uint64_t addr = sym->getVA();
    const uint64_t size = sym->getSize();

    if (addr <= kMemtagGranuleSize && buf != nullptr)
      errorOrWarn("address of the tagged symbol \"" + sym->getName() +
                  "\" falls in the ELF header. This is indicative of a "
                  "compiler/linker bug");
    if (addr % kMemtagGranuleSize != 0)
      errorOrWarn("address of the tagged symbol \"" + sym->getName() +
                  "\" at 0x" + Twine::utohexstr(addr) +
                  "\" is not granule (16-byte) aligned");
    if (size == 0)
      errorOrWarn("size of the tagged symbol \"" + sym->getName() +
                  "\" is not allowed to be zero");
    if (size % kMemtagGranuleSize != 0)
      errorOrWarn("size of the tagged symbol \"" + sym->getName() +
                  "\" (size 0x" + Twine::utohexstr(size) +
                  ") is not granule (16-byte) aligned");

    const uint64_t sizeToEncode = size / kMemtagGranuleSize;
    const uint64_t stepToEncode = ((addr - lastGlobalEnd) / kMemtagGranuleSize)
                                  << kMemtagStepSizeBits;
    if (sizeToEncode < (1 << kMemtagStepSizeBits)) {
      sectionSize += computeOrWriteULEB128(stepToEncode | sizeToEncode, buf, sectionSize);
    } else {
      sectionSize += computeOrWriteULEB128(stepToEncode, buf, sectionSize);
      sectionSize += computeOrWriteULEB128(sizeToEncode - 1, buf, sectionSize);
    }
    lastGlobalEnd = addr + size;
  }

  return sectionSize;
}

bool MemtagGlobalDescriptors::updateAllocSize() {
  size_t oldSize = getSize();
  std::stable_sort(symbols.begin(), symbols.end(),
                   [](const Symbol *s1, const Symbol *s2) {
                     return s1->getVA() < s2->getVA();
                   });
  return oldSize != getSize();
}

void MemtagGlobalDescriptors::writeTo(uint8_t *buf) {
  createMemtagGlobalDescriptors(symbols, buf);
}

size_t MemtagGlobalDescriptors::getSize() const {
  return createMemtagGlobalDescriptors(symbols);
}

static OutputSection *findSection(StringRef name) {
  for (SectionCommand *cmd : script->sectionCommands)
    if (auto *osd = dyn_cast<OutputDesc>(cmd))
      if (osd->osec.name == name)
        return &osd->osec;
  return nullptr;
}

static Defined *addOptionalRegular(StringRef name, SectionBase *sec,
                                   uint64_t val, uint8_t stOther = STV_HIDDEN) {
  Symbol *s = symtab.find(name);
  if (!s || s->isDefined() || s->isCommon())
    return nullptr;

  s->resolve(Defined{ctx.internalFile, StringRef(), STB_GLOBAL, stOther,
                     STT_NOTYPE, val,
                     /*size=*/0, sec});
  s->isUsedInRegularObj = true;
  return cast<Defined>(s);
}

template <class ELFT> void elf::createSyntheticSections() {
  // Initialize all pointers with NULL. This is needed because
  // you can call lld::elf::main more than once as a library.
  Out::tlsPhdr = nullptr;
  Out::preinitArray = nullptr;
  Out::initArray = nullptr;
  Out::finiArray = nullptr;

  // Add the .interp section first because it is not a SyntheticSection.
  // The removeUnusedSyntheticSections() function relies on the
  // SyntheticSections coming last.
  if (needsInterpSection()) {
    for (size_t i = 1; i <= partitions.size(); ++i) {
      InputSection *sec = createInterpSection();
      sec->partition = i;
      ctx.inputSections.push_back(sec);
    }
  }

  auto add = [](SyntheticSection &sec) { ctx.inputSections.push_back(&sec); };

  in.shStrTab = std::make_unique<StringTableSection>(".shstrtab", false);

  Out::programHeaders = make<OutputSection>("", 0, SHF_ALLOC);
  Out::programHeaders->addralign = config->wordsize;

  if (config->strip != StripPolicy::All) {
    in.strTab = std::make_unique<StringTableSection>(".strtab", false);
    in.symTab = std::make_unique<SymbolTableSection<ELFT>>(*in.strTab);
    in.symTabShndx = std::make_unique<SymtabShndxSection>();
  }

  in.bss = std::make_unique<BssSection>(".bss", 0, 1);
  add(*in.bss);

  // If there is a SECTIONS command and a .data.rel.ro section name use name
  // .data.rel.ro.bss so that we match in the .data.rel.ro output section.
  // This makes sure our relro is contiguous.
  bool hasDataRelRo = script->hasSectionsCommand && findSection(".data.rel.ro");
  in.bssRelRo = std::make_unique<BssSection>(
      hasDataRelRo ? ".data.rel.ro.bss" : ".bss.rel.ro", 0, 1);
  add(*in.bssRelRo);

  // Add MIPS-specific sections.
  if (config->emachine == EM_MIPS) {
    if (!config->shared && config->hasDynSymTab) {
      in.mipsRldMap = std::make_unique<MipsRldMapSection>();
      add(*in.mipsRldMap);
    }
    if ((in.mipsAbiFlags = MipsAbiFlagsSection<ELFT>::create()))
      add(*in.mipsAbiFlags);
    if ((in.mipsOptions = MipsOptionsSection<ELFT>::create()))
      add(*in.mipsOptions);
    if ((in.mipsReginfo = MipsReginfoSection<ELFT>::create()))
      add(*in.mipsReginfo);
  }

  StringRef relaDynName = config->isRela ? ".rela.dyn" : ".rel.dyn";

  const unsigned threadCount = config->threadCount;
  for (Partition &part : partitions) {
    auto add = [&](SyntheticSection &sec) {
      sec.partition = part.getNumber();
      ctx.inputSections.push_back(&sec);
    };

    if (!part.name.empty()) {
      part.elfHeader = std::make_unique<PartitionElfHeaderSection<ELFT>>();
      part.elfHeader->name = part.name;
      add(*part.elfHeader);

      part.programHeaders =
          std::make_unique<PartitionProgramHeadersSection<ELFT>>();
      add(*part.programHeaders);
    }

    if (config->buildId != BuildIdKind::None) {
      part.buildId = std::make_unique<BuildIdSection>();
      add(*part.buildId);
    }

    // dynSymTab is always present to simplify sym->includeInDynsym() in
    // finalizeSections.
    part.dynStrTab = std::make_unique<StringTableSection>(".dynstr", true);
    part.dynSymTab =
        std::make_unique<SymbolTableSection<ELFT>>(*part.dynStrTab);

    if (config->relocatable)
      continue;
    part.dynamic = std::make_unique<DynamicSection<ELFT>>();

    if (hasMemtag()) {
      part.memtagAndroidNote = std::make_unique<MemtagAndroidNote>();
      add(*part.memtagAndroidNote);
      if (canHaveMemtagGlobals()) {
        part.memtagGlobalDescriptors =
            std::make_unique<MemtagGlobalDescriptors>();
        add(*part.memtagGlobalDescriptors);
      }
    }

    if (config->androidPackDynRelocs)
      part.relaDyn = std::make_unique<AndroidPackedRelocationSection<ELFT>>(
          relaDynName, threadCount);
    else
      part.relaDyn = std::make_unique<RelocationSection<ELFT>>(
          relaDynName, config->zCombreloc, threadCount);

    if (config->hasDynSymTab) {
      add(*part.dynSymTab);

      part.verSym = std::make_unique<VersionTableSection>();
      add(*part.verSym);

      if (!namedVersionDefs().empty()) {
        part.verDef = std::make_unique<VersionDefinitionSection>();
        add(*part.verDef);
      }

      part.verNeed = std::make_unique<VersionNeedSection<ELFT>>();
      add(*part.verNeed);

      if (config->gnuHash) {
        part.gnuHashTab = std::make_unique<GnuHashTableSection>();
        add(*part.gnuHashTab);
      }

      if (config->sysvHash) {
        part.hashTab = std::make_unique<HashTableSection>();
        add(*part.hashTab);
      }

      add(*part.dynamic);
      add(*part.dynStrTab);
    }
    add(*part.relaDyn);

    if (config->relrPackDynRelocs) {
      part.relrDyn = std::make_unique<RelrSection<ELFT>>(threadCount);
      add(*part.relrDyn);
      part.relrAuthDyn = std::make_unique<RelrSection<ELFT>>(
          threadCount, /*isAArch64Auth=*/true);
      add(*part.relrAuthDyn);
    }

    if (config->ehFrameHdr) {
      part.ehFrameHdr = std::make_unique<EhFrameHeader>();
      add(*part.ehFrameHdr);
    }
    part.ehFrame = std::make_unique<EhFrameSection>();
    add(*part.ehFrame);

    if (config->emachine == EM_ARM) {
      // This section replaces all the individual .ARM.exidx InputSections.
      part.armExidx = std::make_unique<ARMExidxSyntheticSection>();
      add(*part.armExidx);
    }

    if (!config->packageMetadata.empty()) {
      part.packageMetadataNote = std::make_unique<PackageMetadataNote>();
      add(*part.packageMetadataNote);
    }
  }

  if (partitions.size() != 1) {
    // Create the partition end marker. This needs to be in partition number 255
    // so that it is sorted after all other partitions. It also has other
    // special handling (see createPhdrs() and combineEhSections()).
    in.partEnd =
        std::make_unique<BssSection>(".part.end", config->maxPageSize, 1);
    in.partEnd->partition = 255;
    add(*in.partEnd);

    in.partIndex = std::make_unique<PartitionIndexSection>();
    addOptionalRegular("__part_index_begin", in.partIndex.get(), 0);
    addOptionalRegular("__part_index_end", in.partIndex.get(),
                       in.partIndex->getSize());
    add(*in.partIndex);
  }

  // Add .got. MIPS' .got is so different from the other archs,
  // it has its own class.
  if (config->emachine == EM_MIPS) {
    in.mipsGot = std::make_unique<MipsGotSection>();
    add(*in.mipsGot);
  } else {
    in.got = std::make_unique<GotSection>();
    add(*in.got);
  }

  if (config->emachine == EM_PPC) {
    in.ppc32Got2 = std::make_unique<PPC32Got2Section>();
    add(*in.ppc32Got2);
  }

  if (config->emachine == EM_PPC64) {
    in.ppc64LongBranchTarget = std::make_unique<PPC64LongBranchTargetSection>();
    add(*in.ppc64LongBranchTarget);
  }

  in.gotPlt = std::make_unique<GotPltSection>();
  add(*in.gotPlt);
  in.igotPlt = std::make_unique<IgotPltSection>();
  add(*in.igotPlt);
  // Add .relro_padding if DATA_SEGMENT_RELRO_END is used; otherwise, add the
  // section in the absence of PHDRS/SECTIONS commands.
  if (config->zRelro &&
      ((script->phdrsCommands.empty() && !script->hasSectionsCommand) ||
       script->seenRelroEnd)) {
    in.relroPadding = std::make_unique<RelroPaddingSection>();
    add(*in.relroPadding);
  }

  if (config->emachine == EM_ARM) {
    in.armCmseSGSection = std::make_unique<ArmCmseSGSection>();
    add(*in.armCmseSGSection);
  }

  // _GLOBAL_OFFSET_TABLE_ is defined relative to either .got.plt or .got. Treat
  // it as a relocation and ensure the referenced section is created.
  if (ElfSym::globalOffsetTable && config->emachine != EM_MIPS) {
    if (target->gotBaseSymInGotPlt)
      in.gotPlt->hasGotPltOffRel = true;
    else
      in.got->hasGotOffRel = true;
  }

  // We always need to add rel[a].plt to output if it has entries.
  // Even for static linking it can contain R_[*]_IRELATIVE relocations.
  in.relaPlt = std::make_unique<RelocationSection<ELFT>>(
      config->isRela ? ".rela.plt" : ".rel.plt", /*sort=*/false,
      /*threadCount=*/1);
  add(*in.relaPlt);

  if ((config->emachine == EM_386 || config->emachine == EM_X86_64) &&
      (config->andFeatures & GNU_PROPERTY_X86_FEATURE_1_IBT)) {
    in.ibtPlt = std::make_unique<IBTPltSection>();
    add(*in.ibtPlt);
  }
#ifdef __OpenBSD__
  else if (config->emachine == EM_X86_64) {
    in.ibtPlt = std::make_unique<IBTPltSection>();
    add(*in.ibtPlt);
  }
#endif

  if (config->emachine == EM_PPC)
    in.plt = std::make_unique<PPC32GlinkSection>();
  else
    in.plt = std::make_unique<PltSection>();
  add(*in.plt);
  in.iplt = std::make_unique<IpltSection>();
  add(*in.iplt);

  if (config->andFeatures || !ctx.aarch64PauthAbiCoreInfo.empty())
    add(*make<GnuPropertySection>());

  if (config->debugNames) {
    in.debugNames = std::make_unique<DebugNamesSection<ELFT>>();
    add(*in.debugNames);
  }

  if (config->gdbIndex) {
    in.gdbIndex = GdbIndexSection::create<ELFT>();
    add(*in.gdbIndex);
  }

  // .note.GNU-stack is always added when we are creating a re-linkable
  // object file. Other linkers are using the presence of this marker
  // section to control the executable-ness of the stack area, but that
  // is irrelevant these days. Stack area should always be non-executable
  // by default. So we emit this section unconditionally.
  if (config->relocatable)
    add(*make<GnuStackSection>());

  if (in.symTab)
    add(*in.symTab);
  if (in.symTabShndx)
    add(*in.symTabShndx);
  add(*in.shStrTab);
  if (in.strTab)
    add(*in.strTab);
}

InStruct elf::in;

std::vector<Partition> elf::partitions;
Partition *elf::mainPart;

template void elf::splitSections<ELF32LE>();
template void elf::splitSections<ELF32BE>();
template void elf::splitSections<ELF64LE>();
template void elf::splitSections<ELF64BE>();

template void EhFrameSection::iterateFDEWithLSDA<ELF32LE>(
    function_ref<void(InputSection &)>);
template void EhFrameSection::iterateFDEWithLSDA<ELF32BE>(
    function_ref<void(InputSection &)>);
template void EhFrameSection::iterateFDEWithLSDA<ELF64LE>(
    function_ref<void(InputSection &)>);
template void EhFrameSection::iterateFDEWithLSDA<ELF64BE>(
    function_ref<void(InputSection &)>);

template class elf::SymbolTableSection<ELF32LE>;
template class elf::SymbolTableSection<ELF32BE>;
template class elf::SymbolTableSection<ELF64LE>;
template class elf::SymbolTableSection<ELF64BE>;

template void elf::writeEhdr<ELF32LE>(uint8_t *Buf, Partition &Part);
template void elf::writeEhdr<ELF32BE>(uint8_t *Buf, Partition &Part);
template void elf::writeEhdr<ELF64LE>(uint8_t *Buf, Partition &Part);
template void elf::writeEhdr<ELF64BE>(uint8_t *Buf, Partition &Part);

template void elf::writePhdrs<ELF32LE>(uint8_t *Buf, Partition &Part);
template void elf::writePhdrs<ELF32BE>(uint8_t *Buf, Partition &Part);
template void elf::writePhdrs<ELF64LE>(uint8_t *Buf, Partition &Part);
template void elf::writePhdrs<ELF64BE>(uint8_t *Buf, Partition &Part);

template void elf::createSyntheticSections<ELF32LE>();
template void elf::createSyntheticSections<ELF32BE>();
template void elf::createSyntheticSections<ELF64LE>();
template void elf::createSyntheticSections<ELF64BE>();
