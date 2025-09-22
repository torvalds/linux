//===-- ELFDump.cpp - ELF-specific dumper -----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements the ELF-specific dumper for llvm-objdump.
///
//===----------------------------------------------------------------------===//

#include "ELFDump.h"

#include "llvm-objdump.h"
#include "llvm/Demangle/Demangle.h"
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;
using namespace llvm::object;
using namespace llvm::objdump;

namespace {
template <typename ELFT> class ELFDumper : public Dumper {
public:
  ELFDumper(const ELFObjectFile<ELFT> &O) : Dumper(O), Obj(O) {}
  void printPrivateHeaders() override;
  void printDynamicRelocations() override;

private:
  const ELFObjectFile<ELFT> &Obj;

  const ELFFile<ELFT> &getELFFile() const { return Obj.getELFFile(); }
  void printDynamicSection();
  void printProgramHeaders();
  void printSymbolVersion();
  void printSymbolVersionDependency(const typename ELFT::Shdr &Sec);
};
} // namespace

template <class ELFT>
static std::unique_ptr<Dumper> createDumper(const ELFObjectFile<ELFT> &Obj) {
  return std::make_unique<ELFDumper<ELFT>>(Obj);
}

std::unique_ptr<Dumper>
objdump::createELFDumper(const object::ELFObjectFileBase &Obj) {
  if (const auto *O = dyn_cast<ELF32LEObjectFile>(&Obj))
    return createDumper(*O);
  if (const auto *O = dyn_cast<ELF32BEObjectFile>(&Obj))
    return createDumper(*O);
  if (const auto *O = dyn_cast<ELF64LEObjectFile>(&Obj))
    return createDumper(*O);
  return createDumper(cast<ELF64BEObjectFile>(Obj));
}

template <class ELFT>
static Expected<StringRef> getDynamicStrTab(const ELFFile<ELFT> &Elf) {
  auto DynamicEntriesOrError = Elf.dynamicEntries();
  if (!DynamicEntriesOrError)
    return DynamicEntriesOrError.takeError();

  for (const typename ELFT::Dyn &Dyn : *DynamicEntriesOrError) {
    if (Dyn.d_tag == ELF::DT_STRTAB) {
      auto MappedAddrOrError = Elf.toMappedAddr(Dyn.getPtr());
      if (!MappedAddrOrError)
        return MappedAddrOrError.takeError();
      return StringRef(reinterpret_cast<const char *>(*MappedAddrOrError));
    }
  }

  // If the dynamic segment is not present, we fall back on the sections.
  auto SectionsOrError = Elf.sections();
  if (!SectionsOrError)
    return SectionsOrError.takeError();

  for (const typename ELFT::Shdr &Sec : *SectionsOrError) {
    if (Sec.sh_type == ELF::SHT_DYNSYM)
      return Elf.getStringTableForSymtab(Sec);
  }

  return createError("dynamic string table not found");
}

template <class ELFT>
static Error getRelocationValueString(const ELFObjectFile<ELFT> *Obj,
                                      const RelocationRef &RelRef,
                                      SmallVectorImpl<char> &Result) {
  const ELFFile<ELFT> &EF = Obj->getELFFile();
  DataRefImpl Rel = RelRef.getRawDataRefImpl();
  auto SecOrErr = EF.getSection(Rel.d.a);
  if (!SecOrErr)
    return SecOrErr.takeError();

  int64_t Addend = 0;
  // If there is no Symbol associated with the relocation, we set the undef
  // boolean value to 'true'. This will prevent us from calling functions that
  // requires the relocation to be associated with a symbol.
  //
  // In SHT_REL case we would need to read the addend from section data.
  // GNU objdump does not do that and we just follow for simplicity atm.
  bool Undef = false;
  if ((*SecOrErr)->sh_type == ELF::SHT_CREL) {
    auto ERela = Obj->getCrel(Rel);
    Addend = ERela.r_addend;
    Undef = ERela.getSymbol(false) == 0;
  } else if ((*SecOrErr)->sh_type == ELF::SHT_RELA) {
    const typename ELFT::Rela *ERela = Obj->getRela(Rel);
    Addend = ERela->r_addend;
    Undef = ERela->getSymbol(false) == 0;
  } else if ((*SecOrErr)->sh_type == ELF::SHT_REL) {
    const typename ELFT::Rel *ERel = Obj->getRel(Rel);
    Undef = ERel->getSymbol(false) == 0;
  } else {
    return make_error<BinaryError>();
  }

  // Default scheme is to print Target, as well as "+ <addend>" for nonzero
  // addend. Should be acceptable for all normal purposes.
  std::string FmtBuf;
  raw_string_ostream Fmt(FmtBuf);

  if (!Undef) {
    symbol_iterator SI = RelRef.getSymbol();
    Expected<const typename ELFT::Sym *> SymOrErr =
        Obj->getSymbol(SI->getRawDataRefImpl());
    // TODO: test this error.
    if (!SymOrErr)
      return SymOrErr.takeError();

    if ((*SymOrErr)->getType() == ELF::STT_SECTION) {
      Expected<section_iterator> SymSI = SI->getSection();
      if (!SymSI)
        return SymSI.takeError();
      const typename ELFT::Shdr *SymSec =
          Obj->getSection((*SymSI)->getRawDataRefImpl());
      auto SecName = EF.getSectionName(*SymSec);
      if (!SecName)
        return SecName.takeError();
      Fmt << *SecName;
    } else {
      Expected<StringRef> SymName = SI->getName();
      if (!SymName)
        return SymName.takeError();
      Fmt << (Demangle ? demangle(*SymName) : *SymName);
    }
  } else {
    Fmt << "*ABS*";
  }
  if (Addend != 0) {
      Fmt << (Addend < 0
          ? "-"
          : "+") << format("0x%" PRIx64,
                          (Addend < 0 ? -(uint64_t)Addend : (uint64_t)Addend));
  }
  Fmt.flush();
  Result.append(FmtBuf.begin(), FmtBuf.end());
  return Error::success();
}

Error objdump::getELFRelocationValueString(const ELFObjectFileBase *Obj,
                                           const RelocationRef &Rel,
                                           SmallVectorImpl<char> &Result) {
  if (auto *ELF32LE = dyn_cast<ELF32LEObjectFile>(Obj))
    return getRelocationValueString(ELF32LE, Rel, Result);
  if (auto *ELF64LE = dyn_cast<ELF64LEObjectFile>(Obj))
    return getRelocationValueString(ELF64LE, Rel, Result);
  if (auto *ELF32BE = dyn_cast<ELF32BEObjectFile>(Obj))
    return getRelocationValueString(ELF32BE, Rel, Result);
  auto *ELF64BE = cast<ELF64BEObjectFile>(Obj);
  return getRelocationValueString(ELF64BE, Rel, Result);
}

template <class ELFT>
static uint64_t getSectionLMA(const ELFFile<ELFT> &Obj,
                              const object::ELFSectionRef &Sec) {
  auto PhdrRangeOrErr = Obj.program_headers();
  if (!PhdrRangeOrErr)
    report_fatal_error(Twine(toString(PhdrRangeOrErr.takeError())));

  // Search for a PT_LOAD segment containing the requested section. Use this
  // segment's p_addr to calculate the section's LMA.
  for (const typename ELFT::Phdr &Phdr : *PhdrRangeOrErr)
    if ((Phdr.p_type == ELF::PT_LOAD) &&
        (isSectionInSegment<ELFT>(
            Phdr, *cast<const ELFObjectFile<ELFT>>(Sec.getObject())
                       ->getSection(Sec.getRawDataRefImpl()))))
      return Sec.getAddress() - Phdr.p_vaddr + Phdr.p_paddr;

  // Return section's VMA if it isn't in a PT_LOAD segment.
  return Sec.getAddress();
}

uint64_t objdump::getELFSectionLMA(const object::ELFSectionRef &Sec) {
  if (const auto *ELFObj = dyn_cast<ELF32LEObjectFile>(Sec.getObject()))
    return getSectionLMA(ELFObj->getELFFile(), Sec);
  else if (const auto *ELFObj = dyn_cast<ELF32BEObjectFile>(Sec.getObject()))
    return getSectionLMA(ELFObj->getELFFile(), Sec);
  else if (const auto *ELFObj = dyn_cast<ELF64LEObjectFile>(Sec.getObject()))
    return getSectionLMA(ELFObj->getELFFile(), Sec);
  const auto *ELFObj = cast<ELF64BEObjectFile>(Sec.getObject());
  return getSectionLMA(ELFObj->getELFFile(), Sec);
}

template <class ELFT> void ELFDumper<ELFT>::printDynamicSection() {
  const ELFFile<ELFT> &Elf = getELFFile();
  auto DynamicEntriesOrErr = Elf.dynamicEntries();
  if (!DynamicEntriesOrErr) {
    reportWarning(toString(DynamicEntriesOrErr.takeError()), Obj.getFileName());
    return;
  }
  ArrayRef<typename ELFT::Dyn> DynamicEntries = *DynamicEntriesOrErr;

  // Find the maximum tag name length to format the value column properly.
  size_t MaxLen = 0;
  for (const typename ELFT::Dyn &Dyn : DynamicEntries)
    MaxLen = std::max(MaxLen, Elf.getDynamicTagAsString(Dyn.d_tag).size());
  std::string TagFmt = "  %-" + std::to_string(MaxLen) + "s ";

  outs() << "\nDynamic Section:\n";
  for (const typename ELFT::Dyn &Dyn : DynamicEntries) {
    if (Dyn.d_tag == ELF::DT_NULL)
      continue;

    std::string Str = Elf.getDynamicTagAsString(Dyn.d_tag);

    const char *Fmt =
        ELFT::Is64Bits ? "0x%016" PRIx64 "\n" : "0x%08" PRIx64 "\n";
    if (Dyn.d_tag == ELF::DT_NEEDED || Dyn.d_tag == ELF::DT_RPATH ||
        Dyn.d_tag == ELF::DT_RUNPATH || Dyn.d_tag == ELF::DT_SONAME ||
        Dyn.d_tag == ELF::DT_AUXILIARY || Dyn.d_tag == ELF::DT_FILTER) {
      Expected<StringRef> StrTabOrErr = getDynamicStrTab(Elf);
      if (StrTabOrErr) {
        const char *Data = StrTabOrErr->data();
        outs() << format(TagFmt.c_str(), Str.c_str()) << Data + Dyn.getVal()
               << "\n";
        continue;
      }
      reportWarning(toString(StrTabOrErr.takeError()), Obj.getFileName());
      consumeError(StrTabOrErr.takeError());
    }
    outs() << format(TagFmt.c_str(), Str.c_str())
           << format(Fmt, (uint64_t)Dyn.getVal());
  }
}

template <class ELFT> void ELFDumper<ELFT>::printProgramHeaders() {
  outs() << "\nProgram Header:\n";
  auto ProgramHeaderOrError = getELFFile().program_headers();
  if (!ProgramHeaderOrError) {
    reportWarning("unable to read program headers: " +
                      toString(ProgramHeaderOrError.takeError()),
                  Obj.getFileName());
    return;
  }

  for (const typename ELFT::Phdr &Phdr : *ProgramHeaderOrError) {
    switch (Phdr.p_type) {
    case ELF::PT_DYNAMIC:
      outs() << " DYNAMIC ";
      break;
    case ELF::PT_GNU_EH_FRAME:
      outs() << "EH_FRAME ";
      break;
    case ELF::PT_GNU_RELRO:
      outs() << "   RELRO ";
      break;
    case ELF::PT_GNU_PROPERTY:
      outs() << "   PROPERTY ";
      break;
    case ELF::PT_GNU_STACK:
      outs() << "   STACK ";
      break;
    case ELF::PT_INTERP:
      outs() << "  INTERP ";
      break;
    case ELF::PT_LOAD:
      outs() << "    LOAD ";
      break;
    case ELF::PT_NOTE:
      outs() << "    NOTE ";
      break;
    case ELF::PT_OPENBSD_BOOTDATA:
      outs() << "OPENBSD_BOOTDATA ";
      break;
    case ELF::PT_OPENBSD_MUTABLE:
      outs() << "OPENBSD_MUTABLE ";
      break;
    case ELF::PT_OPENBSD_NOBTCFI:
      outs() << "OPENBSD_NOBTCFI ";
      break;
    case ELF::PT_OPENBSD_RANDOMIZE:
      outs() << "OPENBSD_RANDOMIZE ";
      break;
    case ELF::PT_OPENBSD_SYSCALLS:
      outs() << "OPENBSD_SYSCALLS ";
      break;
    case ELF::PT_OPENBSD_WXNEEDED:
      outs() << "OPENBSD_WXNEEDED ";
      break;
    case ELF::PT_PHDR:
      outs() << "    PHDR ";
      break;
    case ELF::PT_TLS:
      outs() << "    TLS ";
      break;
    default:
      outs() << " UNKNOWN ";
    }

    const char *Fmt = ELFT::Is64Bits ? "0x%016" PRIx64 " " : "0x%08" PRIx64 " ";

    outs() << "off    " << format(Fmt, (uint64_t)Phdr.p_offset) << "vaddr "
           << format(Fmt, (uint64_t)Phdr.p_vaddr) << "paddr "
           << format(Fmt, (uint64_t)Phdr.p_paddr)
           << format("align 2**%u\n", llvm::countr_zero<uint64_t>(Phdr.p_align))
           << "         filesz " << format(Fmt, (uint64_t)Phdr.p_filesz)
           << "memsz " << format(Fmt, (uint64_t)Phdr.p_memsz) << "flags "
           << ((Phdr.p_flags & ELF::PF_R) ? "r" : "-")
           << ((Phdr.p_flags & ELF::PF_W) ? "w" : "-")
           << ((Phdr.p_flags & ELF::PF_X) ? "x" : "-") << "\n";
  }
}

template <typename ELFT> void ELFDumper<ELFT>::printDynamicRelocations() {
  if (!any_of(Obj.sections(), [](const ELFSectionRef Sec) {
        return Sec.getType() == ELF::SHT_DYNAMIC;
      })) {
    reportError(Obj.getFileName(), "not a dynamic object");
    return;
  }

  std::vector<SectionRef> DynRelSec =
      cast<ObjectFile>(Obj).dynamic_relocation_sections();
  if (DynRelSec.empty())
    return;

  outs() << "\nDYNAMIC RELOCATION RECORDS\n";
  const uint32_t OffsetPadding = (Obj.getBytesInAddress() > 4 ? 16 : 8);
  const uint32_t TypePadding = 24;
  outs() << left_justify("OFFSET", OffsetPadding) << ' '
         << left_justify("TYPE", TypePadding) << " VALUE\n";

  StringRef Fmt = Obj.getBytesInAddress() > 4 ? "%016" PRIx64 : "%08" PRIx64;
  for (const SectionRef &Section : DynRelSec)
    for (const RelocationRef &Reloc : Section.relocations()) {
      uint64_t Address = Reloc.getOffset();
      SmallString<32> RelocName;
      SmallString<32> ValueStr;
      Reloc.getTypeName(RelocName);
      if (Error E = getELFRelocationValueString(&Obj, Reloc, ValueStr))
        reportError(std::move(E), Obj.getFileName());
      outs() << format(Fmt.data(), Address) << ' '
             << left_justify(RelocName, TypePadding) << ' ' << ValueStr << '\n';
    }
}

template <class ELFT>
void ELFDumper<ELFT>::printSymbolVersionDependency(
    const typename ELFT::Shdr &Sec) {
  outs() << "\nVersion References:\n";
  Expected<std::vector<VerNeed>> V =
      getELFFile().getVersionDependencies(Sec, this->WarningHandler);
  if (!V) {
    reportWarning(toString(V.takeError()), Obj.getFileName());
    return;
  }

  raw_fd_ostream &OS = outs();
  for (const VerNeed &VN : *V) {
    OS << "  required from " << VN.File << ":\n";
    for (const VernAux &Aux : VN.AuxV)
      OS << format("    0x%08x 0x%02x %02u %s\n", Aux.Hash, Aux.Flags,
                   Aux.Other, Aux.Name.c_str());
  }
}

template <class ELFT>
static void printSymbolVersionDefinition(const typename ELFT::Shdr &Shdr,
                                         ArrayRef<uint8_t> Contents,
                                         StringRef StrTab) {
  outs() << "\nVersion definitions:\n";

  const uint8_t *Buf = Contents.data();
  uint32_t VerdefIndex = 1;
  // sh_info contains the number of entries in the SHT_GNU_verdef section. To
  // make the index column have consistent width, we should insert blank spaces
  // according to sh_info.
  uint16_t VerdefIndexWidth = std::to_string(Shdr.sh_info).size();
  while (Buf) {
    auto *Verdef = reinterpret_cast<const typename ELFT::Verdef *>(Buf);
    outs() << format_decimal(VerdefIndex++, VerdefIndexWidth) << " "
           << format("0x%02" PRIx16 " ", (uint16_t)Verdef->vd_flags)
           << format("0x%08" PRIx32 " ", (uint32_t)Verdef->vd_hash);

    const uint8_t *BufAux = Buf + Verdef->vd_aux;
    uint16_t VerdauxIndex = 0;
    while (BufAux) {
      auto *Verdaux = reinterpret_cast<const typename ELFT::Verdaux *>(BufAux);
      if (VerdauxIndex)
        outs() << std::string(VerdefIndexWidth + 17, ' ');
      outs() << StringRef(StrTab.drop_front(Verdaux->vda_name).data()) << '\n';
      BufAux = Verdaux->vda_next ? BufAux + Verdaux->vda_next : nullptr;
      ++VerdauxIndex;
    }
    Buf = Verdef->vd_next ? Buf + Verdef->vd_next : nullptr;
  }
}

template <class ELFT> void ELFDumper<ELFT>::printSymbolVersion() {
  const ELFFile<ELFT> &Elf = getELFFile();
  StringRef FileName = Obj.getFileName();
  ArrayRef<typename ELFT::Shdr> Sections =
      unwrapOrError(Elf.sections(), FileName);
  for (const typename ELFT::Shdr &Shdr : Sections) {
    if (Shdr.sh_type != ELF::SHT_GNU_verneed &&
        Shdr.sh_type != ELF::SHT_GNU_verdef)
      continue;

    ArrayRef<uint8_t> Contents =
        unwrapOrError(Elf.getSectionContents(Shdr), FileName);
    const typename ELFT::Shdr *StrTabSec =
        unwrapOrError(Elf.getSection(Shdr.sh_link), FileName);
    StringRef StrTab = unwrapOrError(Elf.getStringTable(*StrTabSec), FileName);

    if (Shdr.sh_type == ELF::SHT_GNU_verneed)
      printSymbolVersionDependency(Shdr);
    else
      printSymbolVersionDefinition<ELFT>(Shdr, Contents, StrTab);
  }
}

template <class ELFT> void ELFDumper<ELFT>::printPrivateHeaders() {
  printProgramHeaders();
  printDynamicSection();
  printSymbolVersion();
}
