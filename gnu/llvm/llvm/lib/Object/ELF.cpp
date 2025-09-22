//===- ELF.cpp - ELF object file implementation ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Object/ELF.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/Support/DataExtractor.h"

using namespace llvm;
using namespace object;

#define STRINGIFY_ENUM_CASE(ns, name)                                          \
  case ns::name:                                                               \
    return #name;

#define ELF_RELOC(name, value) STRINGIFY_ENUM_CASE(ELF, name)

StringRef llvm::object::getELFRelocationTypeName(uint32_t Machine,
                                                 uint32_t Type) {
  switch (Machine) {
  case ELF::EM_68K:
    switch (Type) {
#include "llvm/BinaryFormat/ELFRelocs/M68k.def"
    default:
      break;
    }
    break;
  case ELF::EM_X86_64:
    switch (Type) {
#include "llvm/BinaryFormat/ELFRelocs/x86_64.def"
    default:
      break;
    }
    break;
  case ELF::EM_386:
  case ELF::EM_IAMCU:
    switch (Type) {
#include "llvm/BinaryFormat/ELFRelocs/i386.def"
    default:
      break;
    }
    break;
  case ELF::EM_MIPS:
    switch (Type) {
#include "llvm/BinaryFormat/ELFRelocs/Mips.def"
    default:
      break;
    }
    break;
  case ELF::EM_AARCH64:
    switch (Type) {
#include "llvm/BinaryFormat/ELFRelocs/AArch64.def"
    default:
      break;
    }
    break;
  case ELF::EM_ARM:
    switch (Type) {
#include "llvm/BinaryFormat/ELFRelocs/ARM.def"
    default:
      break;
    }
    break;
  case ELF::EM_ARC_COMPACT:
  case ELF::EM_ARC_COMPACT2:
    switch (Type) {
#include "llvm/BinaryFormat/ELFRelocs/ARC.def"
    default:
      break;
    }
    break;
  case ELF::EM_AVR:
    switch (Type) {
#include "llvm/BinaryFormat/ELFRelocs/AVR.def"
    default:
      break;
    }
    break;
  case ELF::EM_HEXAGON:
    switch (Type) {
#include "llvm/BinaryFormat/ELFRelocs/Hexagon.def"
    default:
      break;
    }
    break;
  case ELF::EM_LANAI:
    switch (Type) {
#include "llvm/BinaryFormat/ELFRelocs/Lanai.def"
    default:
      break;
    }
    break;
  case ELF::EM_PPC:
    switch (Type) {
#include "llvm/BinaryFormat/ELFRelocs/PowerPC.def"
    default:
      break;
    }
    break;
  case ELF::EM_PPC64:
    switch (Type) {
#include "llvm/BinaryFormat/ELFRelocs/PowerPC64.def"
    default:
      break;
    }
    break;
  case ELF::EM_RISCV:
    switch (Type) {
#include "llvm/BinaryFormat/ELFRelocs/RISCV.def"
    default:
      break;
    }
    break;
  case ELF::EM_S390:
    switch (Type) {
#include "llvm/BinaryFormat/ELFRelocs/SystemZ.def"
    default:
      break;
    }
    break;
  case ELF::EM_SPARC:
  case ELF::EM_SPARC32PLUS:
  case ELF::EM_SPARCV9:
    switch (Type) {
#include "llvm/BinaryFormat/ELFRelocs/Sparc.def"
    default:
      break;
    }
    break;
  case ELF::EM_AMDGPU:
    switch (Type) {
#include "llvm/BinaryFormat/ELFRelocs/AMDGPU.def"
    default:
      break;
    }
    break;
  case ELF::EM_BPF:
    switch (Type) {
#include "llvm/BinaryFormat/ELFRelocs/BPF.def"
    default:
      break;
    }
    break;
  case ELF::EM_MSP430:
    switch (Type) {
#include "llvm/BinaryFormat/ELFRelocs/MSP430.def"
    default:
      break;
    }
    break;
  case ELF::EM_VE:
    switch (Type) {
#include "llvm/BinaryFormat/ELFRelocs/VE.def"
    default:
      break;
    }
    break;
  case ELF::EM_CSKY:
    switch (Type) {
#include "llvm/BinaryFormat/ELFRelocs/CSKY.def"
    default:
      break;
    }
    break;
  case ELF::EM_LOONGARCH:
    switch (Type) {
#include "llvm/BinaryFormat/ELFRelocs/LoongArch.def"
    default:
      break;
    }
    break;
  case ELF::EM_XTENSA:
    switch (Type) {
#include "llvm/BinaryFormat/ELFRelocs/Xtensa.def"
    default:
      break;
    }
    break;
  default:
    break;
  }
  return "Unknown";
}

#undef ELF_RELOC

uint32_t llvm::object::getELFRelativeRelocationType(uint32_t Machine) {
  switch (Machine) {
  case ELF::EM_X86_64:
    return ELF::R_X86_64_RELATIVE;
  case ELF::EM_386:
  case ELF::EM_IAMCU:
    return ELF::R_386_RELATIVE;
  case ELF::EM_MIPS:
    break;
  case ELF::EM_AARCH64:
    return ELF::R_AARCH64_RELATIVE;
  case ELF::EM_ARM:
    return ELF::R_ARM_RELATIVE;
  case ELF::EM_ARC_COMPACT:
  case ELF::EM_ARC_COMPACT2:
    return ELF::R_ARC_RELATIVE;
  case ELF::EM_AVR:
    break;
  case ELF::EM_HEXAGON:
    return ELF::R_HEX_RELATIVE;
  case ELF::EM_LANAI:
    break;
  case ELF::EM_PPC:
    break;
  case ELF::EM_PPC64:
    return ELF::R_PPC64_RELATIVE;
  case ELF::EM_RISCV:
    return ELF::R_RISCV_RELATIVE;
  case ELF::EM_S390:
    return ELF::R_390_RELATIVE;
  case ELF::EM_SPARC:
  case ELF::EM_SPARC32PLUS:
  case ELF::EM_SPARCV9:
    return ELF::R_SPARC_RELATIVE;
  case ELF::EM_CSKY:
    return ELF::R_CKCORE_RELATIVE;
  case ELF::EM_VE:
    return ELF::R_VE_RELATIVE;
  case ELF::EM_AMDGPU:
    break;
  case ELF::EM_BPF:
    break;
  case ELF::EM_LOONGARCH:
    return ELF::R_LARCH_RELATIVE;
  default:
    break;
  }
  return 0;
}

StringRef llvm::object::getELFSectionTypeName(uint32_t Machine, unsigned Type) {
  switch (Machine) {
  case ELF::EM_ARM:
    switch (Type) {
      STRINGIFY_ENUM_CASE(ELF, SHT_ARM_EXIDX);
      STRINGIFY_ENUM_CASE(ELF, SHT_ARM_PREEMPTMAP);
      STRINGIFY_ENUM_CASE(ELF, SHT_ARM_ATTRIBUTES);
      STRINGIFY_ENUM_CASE(ELF, SHT_ARM_DEBUGOVERLAY);
      STRINGIFY_ENUM_CASE(ELF, SHT_ARM_OVERLAYSECTION);
    }
    break;
  case ELF::EM_HEXAGON:
    switch (Type) {
      STRINGIFY_ENUM_CASE(ELF, SHT_HEX_ORDERED);
      STRINGIFY_ENUM_CASE(ELF, SHT_HEXAGON_ATTRIBUTES);
    }
    break;
  case ELF::EM_X86_64:
    switch (Type) { STRINGIFY_ENUM_CASE(ELF, SHT_X86_64_UNWIND); }
    break;
  case ELF::EM_MIPS:
  case ELF::EM_MIPS_RS3_LE:
    switch (Type) {
      STRINGIFY_ENUM_CASE(ELF, SHT_MIPS_REGINFO);
      STRINGIFY_ENUM_CASE(ELF, SHT_MIPS_OPTIONS);
      STRINGIFY_ENUM_CASE(ELF, SHT_MIPS_DWARF);
      STRINGIFY_ENUM_CASE(ELF, SHT_MIPS_ABIFLAGS);
    }
    break;
  case ELF::EM_MSP430:
    switch (Type) { STRINGIFY_ENUM_CASE(ELF, SHT_MSP430_ATTRIBUTES); }
    break;
  case ELF::EM_RISCV:
    switch (Type) { STRINGIFY_ENUM_CASE(ELF, SHT_RISCV_ATTRIBUTES); }
    break;
  case ELF::EM_AARCH64:
    switch (Type) {
      STRINGIFY_ENUM_CASE(ELF, SHT_AARCH64_AUTH_RELR);
      STRINGIFY_ENUM_CASE(ELF, SHT_AARCH64_MEMTAG_GLOBALS_DYNAMIC);
      STRINGIFY_ENUM_CASE(ELF, SHT_AARCH64_MEMTAG_GLOBALS_STATIC);
    }
  default:
    break;
  }

  switch (Type) {
    STRINGIFY_ENUM_CASE(ELF, SHT_NULL);
    STRINGIFY_ENUM_CASE(ELF, SHT_PROGBITS);
    STRINGIFY_ENUM_CASE(ELF, SHT_SYMTAB);
    STRINGIFY_ENUM_CASE(ELF, SHT_STRTAB);
    STRINGIFY_ENUM_CASE(ELF, SHT_RELA);
    STRINGIFY_ENUM_CASE(ELF, SHT_HASH);
    STRINGIFY_ENUM_CASE(ELF, SHT_DYNAMIC);
    STRINGIFY_ENUM_CASE(ELF, SHT_NOTE);
    STRINGIFY_ENUM_CASE(ELF, SHT_NOBITS);
    STRINGIFY_ENUM_CASE(ELF, SHT_REL);
    STRINGIFY_ENUM_CASE(ELF, SHT_SHLIB);
    STRINGIFY_ENUM_CASE(ELF, SHT_DYNSYM);
    STRINGIFY_ENUM_CASE(ELF, SHT_INIT_ARRAY);
    STRINGIFY_ENUM_CASE(ELF, SHT_FINI_ARRAY);
    STRINGIFY_ENUM_CASE(ELF, SHT_PREINIT_ARRAY);
    STRINGIFY_ENUM_CASE(ELF, SHT_GROUP);
    STRINGIFY_ENUM_CASE(ELF, SHT_SYMTAB_SHNDX);
    STRINGIFY_ENUM_CASE(ELF, SHT_RELR);
    STRINGIFY_ENUM_CASE(ELF, SHT_CREL);
    STRINGIFY_ENUM_CASE(ELF, SHT_ANDROID_REL);
    STRINGIFY_ENUM_CASE(ELF, SHT_ANDROID_RELA);
    STRINGIFY_ENUM_CASE(ELF, SHT_ANDROID_RELR);
    STRINGIFY_ENUM_CASE(ELF, SHT_LLVM_ODRTAB);
    STRINGIFY_ENUM_CASE(ELF, SHT_LLVM_LINKER_OPTIONS);
    STRINGIFY_ENUM_CASE(ELF, SHT_LLVM_CALL_GRAPH_PROFILE);
    STRINGIFY_ENUM_CASE(ELF, SHT_LLVM_ADDRSIG);
    STRINGIFY_ENUM_CASE(ELF, SHT_LLVM_DEPENDENT_LIBRARIES);
    STRINGIFY_ENUM_CASE(ELF, SHT_LLVM_SYMPART);
    STRINGIFY_ENUM_CASE(ELF, SHT_LLVM_PART_EHDR);
    STRINGIFY_ENUM_CASE(ELF, SHT_LLVM_PART_PHDR);
    STRINGIFY_ENUM_CASE(ELF, SHT_LLVM_BB_ADDR_MAP_V0);
    STRINGIFY_ENUM_CASE(ELF, SHT_LLVM_BB_ADDR_MAP);
    STRINGIFY_ENUM_CASE(ELF, SHT_LLVM_OFFLOADING);
    STRINGIFY_ENUM_CASE(ELF, SHT_LLVM_LTO);
    STRINGIFY_ENUM_CASE(ELF, SHT_GNU_ATTRIBUTES);
    STRINGIFY_ENUM_CASE(ELF, SHT_GNU_HASH);
    STRINGIFY_ENUM_CASE(ELF, SHT_GNU_verdef);
    STRINGIFY_ENUM_CASE(ELF, SHT_GNU_verneed);
    STRINGIFY_ENUM_CASE(ELF, SHT_GNU_versym);
  default:
    return "Unknown";
  }
}

template <class ELFT>
std::vector<typename ELFT::Rel>
ELFFile<ELFT>::decode_relrs(Elf_Relr_Range relrs) const {
  // This function decodes the contents of an SHT_RELR packed relocation
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

  Elf_Rel Rel;
  Rel.r_info = 0;
  Rel.setType(getRelativeRelocationType(), false);
  std::vector<Elf_Rel> Relocs;

  // Word type: uint32_t for Elf32, and uint64_t for Elf64.
  using Addr = typename ELFT::uint;

  Addr Base = 0;
  for (Elf_Relr R : relrs) {
    typename ELFT::uint Entry = R;
    if ((Entry & 1) == 0) {
      // Even entry: encodes the offset for next relocation.
      Rel.r_offset = Entry;
      Relocs.push_back(Rel);
      // Set base offset for subsequent bitmap entries.
      Base = Entry + sizeof(Addr);
    } else {
      // Odd entry: encodes bitmap for relocations starting at base.
      for (Addr Offset = Base; (Entry >>= 1) != 0; Offset += sizeof(Addr))
        if ((Entry & 1) != 0) {
          Rel.r_offset = Offset;
          Relocs.push_back(Rel);
        }
      Base += (CHAR_BIT * sizeof(Entry) - 1) * sizeof(Addr);
    }
  }

  return Relocs;
}

template <class ELFT>
Expected<uint64_t>
ELFFile<ELFT>::getCrelHeader(ArrayRef<uint8_t> Content) const {
  DataExtractor Data(Content, isLE(), sizeof(typename ELFT::Addr));
  Error Err = Error::success();
  uint64_t Hdr = 0;
  Hdr = Data.getULEB128(&Hdr, &Err);
  if (Err)
    return Err;
  return Hdr;
}

template <class ELFT>
Expected<typename ELFFile<ELFT>::RelsOrRelas>
ELFFile<ELFT>::decodeCrel(ArrayRef<uint8_t> Content) const {
  std::vector<Elf_Rel> Rels;
  std::vector<Elf_Rela> Relas;
  size_t I = 0;
  bool HasAddend;
  Error Err = object::decodeCrel<ELFT::Is64Bits>(
      Content,
      [&](uint64_t Count, bool HasA) {
        HasAddend = HasA;
        if (HasAddend)
          Relas.resize(Count);
        else
          Rels.resize(Count);
      },
      [&](Elf_Crel Crel) {
        if (HasAddend) {
          Relas[I].r_offset = Crel.r_offset;
          Relas[I].setSymbolAndType(Crel.r_symidx, Crel.r_type, false);
          Relas[I++].r_addend = Crel.r_addend;
        } else {
          Rels[I].r_offset = Crel.r_offset;
          Rels[I++].setSymbolAndType(Crel.r_symidx, Crel.r_type, false);
        }
      });
  if (Err)
    return std::move(Err);
  return std::make_pair(std::move(Rels), std::move(Relas));
}

template <class ELFT>
Expected<typename ELFFile<ELFT>::RelsOrRelas>
ELFFile<ELFT>::crels(const Elf_Shdr &Sec) const {
  Expected<ArrayRef<uint8_t>> ContentsOrErr = getSectionContents(Sec);
  if (!ContentsOrErr)
    return ContentsOrErr.takeError();
  return decodeCrel(*ContentsOrErr);
}

template <class ELFT>
Expected<std::vector<typename ELFT::Rela>>
ELFFile<ELFT>::android_relas(const Elf_Shdr &Sec) const {
  // This function reads relocations in Android's packed relocation format,
  // which is based on SLEB128 and delta encoding.
  Expected<ArrayRef<uint8_t>> ContentsOrErr = getSectionContents(Sec);
  if (!ContentsOrErr)
    return ContentsOrErr.takeError();
  ArrayRef<uint8_t> Content = *ContentsOrErr;
  if (Content.size() < 4 || Content[0] != 'A' || Content[1] != 'P' ||
      Content[2] != 'S' || Content[3] != '2')
    return createError("invalid packed relocation header");
  DataExtractor Data(Content, isLE(), ELFT::Is64Bits ? 8 : 4);
  DataExtractor::Cursor Cur(/*Offset=*/4);

  uint64_t NumRelocs = Data.getSLEB128(Cur);
  uint64_t Offset = Data.getSLEB128(Cur);
  uint64_t Addend = 0;

  if (!Cur)
    return std::move(Cur.takeError());

  std::vector<Elf_Rela> Relocs;
  Relocs.reserve(NumRelocs);
  while (NumRelocs) {
    uint64_t NumRelocsInGroup = Data.getSLEB128(Cur);
    if (!Cur)
      return std::move(Cur.takeError());
    if (NumRelocsInGroup > NumRelocs)
      return createError("relocation group unexpectedly large");
    NumRelocs -= NumRelocsInGroup;

    uint64_t GroupFlags = Data.getSLEB128(Cur);
    bool GroupedByInfo = GroupFlags & ELF::RELOCATION_GROUPED_BY_INFO_FLAG;
    bool GroupedByOffsetDelta = GroupFlags & ELF::RELOCATION_GROUPED_BY_OFFSET_DELTA_FLAG;
    bool GroupedByAddend = GroupFlags & ELF::RELOCATION_GROUPED_BY_ADDEND_FLAG;
    bool GroupHasAddend = GroupFlags & ELF::RELOCATION_GROUP_HAS_ADDEND_FLAG;

    uint64_t GroupOffsetDelta;
    if (GroupedByOffsetDelta)
      GroupOffsetDelta = Data.getSLEB128(Cur);

    uint64_t GroupRInfo;
    if (GroupedByInfo)
      GroupRInfo = Data.getSLEB128(Cur);

    if (GroupedByAddend && GroupHasAddend)
      Addend += Data.getSLEB128(Cur);

    if (!GroupHasAddend)
      Addend = 0;

    for (uint64_t I = 0; Cur && I != NumRelocsInGroup; ++I) {
      Elf_Rela R;
      Offset += GroupedByOffsetDelta ? GroupOffsetDelta : Data.getSLEB128(Cur);
      R.r_offset = Offset;
      R.r_info = GroupedByInfo ? GroupRInfo : Data.getSLEB128(Cur);
      if (GroupHasAddend && !GroupedByAddend)
        Addend += Data.getSLEB128(Cur);
      R.r_addend = Addend;
      Relocs.push_back(R);
    }
    if (!Cur)
      return std::move(Cur.takeError());
  }

  return Relocs;
}

template <class ELFT>
std::string ELFFile<ELFT>::getDynamicTagAsString(unsigned Arch,
                                                 uint64_t Type) const {
#define DYNAMIC_STRINGIFY_ENUM(tag, value)                                     \
  case value:                                                                  \
    return #tag;

#define DYNAMIC_TAG(n, v)
  switch (Arch) {
  case ELF::EM_AARCH64:
    switch (Type) {
#define AARCH64_DYNAMIC_TAG(name, value) DYNAMIC_STRINGIFY_ENUM(name, value)
#include "llvm/BinaryFormat/DynamicTags.def"
#undef AARCH64_DYNAMIC_TAG
    }
    break;

  case ELF::EM_HEXAGON:
    switch (Type) {
#define HEXAGON_DYNAMIC_TAG(name, value) DYNAMIC_STRINGIFY_ENUM(name, value)
#include "llvm/BinaryFormat/DynamicTags.def"
#undef HEXAGON_DYNAMIC_TAG
    }
    break;

  case ELF::EM_MIPS:
    switch (Type) {
#define MIPS_DYNAMIC_TAG(name, value) DYNAMIC_STRINGIFY_ENUM(name, value)
#include "llvm/BinaryFormat/DynamicTags.def"
#undef MIPS_DYNAMIC_TAG
    }
    break;

  case ELF::EM_PPC:
    switch (Type) {
#define PPC_DYNAMIC_TAG(name, value) DYNAMIC_STRINGIFY_ENUM(name, value)
#include "llvm/BinaryFormat/DynamicTags.def"
#undef PPC_DYNAMIC_TAG
    }
    break;

  case ELF::EM_PPC64:
    switch (Type) {
#define PPC64_DYNAMIC_TAG(name, value) DYNAMIC_STRINGIFY_ENUM(name, value)
#include "llvm/BinaryFormat/DynamicTags.def"
#undef PPC64_DYNAMIC_TAG
    }
    break;

  case ELF::EM_RISCV:
    switch (Type) {
#define RISCV_DYNAMIC_TAG(name, value) DYNAMIC_STRINGIFY_ENUM(name, value)
#include "llvm/BinaryFormat/DynamicTags.def"
#undef RISCV_DYNAMIC_TAG
    }
    break;
  }
#undef DYNAMIC_TAG
  switch (Type) {
// Now handle all dynamic tags except the architecture specific ones
#define AARCH64_DYNAMIC_TAG(name, value)
#define MIPS_DYNAMIC_TAG(name, value)
#define HEXAGON_DYNAMIC_TAG(name, value)
#define PPC_DYNAMIC_TAG(name, value)
#define PPC64_DYNAMIC_TAG(name, value)
#define RISCV_DYNAMIC_TAG(name, value)
// Also ignore marker tags such as DT_HIOS (maps to DT_VERNEEDNUM), etc.
#define DYNAMIC_TAG_MARKER(name, value)
#define DYNAMIC_TAG(name, value) case value: return #name;
#include "llvm/BinaryFormat/DynamicTags.def"
#undef DYNAMIC_TAG
#undef AARCH64_DYNAMIC_TAG
#undef MIPS_DYNAMIC_TAG
#undef HEXAGON_DYNAMIC_TAG
#undef PPC_DYNAMIC_TAG
#undef PPC64_DYNAMIC_TAG
#undef RISCV_DYNAMIC_TAG
#undef DYNAMIC_TAG_MARKER
#undef DYNAMIC_STRINGIFY_ENUM
  default:
    return "<unknown:>0x" + utohexstr(Type, true);
  }
}

template <class ELFT>
std::string ELFFile<ELFT>::getDynamicTagAsString(uint64_t Type) const {
  return getDynamicTagAsString(getHeader().e_machine, Type);
}

template <class ELFT>
Expected<typename ELFT::DynRange> ELFFile<ELFT>::dynamicEntries() const {
  ArrayRef<Elf_Dyn> Dyn;

  auto ProgramHeadersOrError = program_headers();
  if (!ProgramHeadersOrError)
    return ProgramHeadersOrError.takeError();

  for (const Elf_Phdr &Phdr : *ProgramHeadersOrError) {
    if (Phdr.p_type == ELF::PT_DYNAMIC) {
      const uint8_t *DynOffset = base() + Phdr.p_offset;
      if (DynOffset > end())
        return createError(
            "dynamic section offset past file size: corrupted ELF");
      Dyn = ArrayRef(reinterpret_cast<const Elf_Dyn *>(DynOffset),
                     Phdr.p_filesz / sizeof(Elf_Dyn));
      break;
    }
  }

  // If we can't find the dynamic section in the program headers, we just fall
  // back on the sections.
  if (Dyn.empty()) {
    auto SectionsOrError = sections();
    if (!SectionsOrError)
      return SectionsOrError.takeError();

    for (const Elf_Shdr &Sec : *SectionsOrError) {
      if (Sec.sh_type == ELF::SHT_DYNAMIC) {
        Expected<ArrayRef<Elf_Dyn>> DynOrError =
            getSectionContentsAsArray<Elf_Dyn>(Sec);
        if (!DynOrError)
          return DynOrError.takeError();
        Dyn = *DynOrError;
        break;
      }
    }

    if (!Dyn.data())
      return ArrayRef<Elf_Dyn>();
  }

  if (Dyn.empty())
    return createError("invalid empty dynamic section");

  if (Dyn.back().d_tag != ELF::DT_NULL)
    return createError("dynamic sections must be DT_NULL terminated");

  return Dyn;
}

template <class ELFT>
Expected<const uint8_t *>
ELFFile<ELFT>::toMappedAddr(uint64_t VAddr, WarningHandler WarnHandler) const {
  auto ProgramHeadersOrError = program_headers();
  if (!ProgramHeadersOrError)
    return ProgramHeadersOrError.takeError();

  llvm::SmallVector<Elf_Phdr *, 4> LoadSegments;

  for (const Elf_Phdr &Phdr : *ProgramHeadersOrError)
    if (Phdr.p_type == ELF::PT_LOAD)
      LoadSegments.push_back(const_cast<Elf_Phdr *>(&Phdr));

  auto SortPred = [](const Elf_Phdr_Impl<ELFT> *A,
                     const Elf_Phdr_Impl<ELFT> *B) {
    return A->p_vaddr < B->p_vaddr;
  };
  if (!llvm::is_sorted(LoadSegments, SortPred)) {
    if (Error E =
            WarnHandler("loadable segments are unsorted by virtual address"))
      return std::move(E);
    llvm::stable_sort(LoadSegments, SortPred);
  }

  const Elf_Phdr *const *I = llvm::upper_bound(
      LoadSegments, VAddr, [](uint64_t VAddr, const Elf_Phdr_Impl<ELFT> *Phdr) {
        return VAddr < Phdr->p_vaddr;
      });

  if (I == LoadSegments.begin())
    return createError("virtual address is not in any segment: 0x" +
                       Twine::utohexstr(VAddr));
  --I;
  const Elf_Phdr &Phdr = **I;
  uint64_t Delta = VAddr - Phdr.p_vaddr;
  if (Delta >= Phdr.p_filesz)
    return createError("virtual address is not in any segment: 0x" +
                       Twine::utohexstr(VAddr));

  uint64_t Offset = Phdr.p_offset + Delta;
  if (Offset >= getBufSize())
    return createError("can't map virtual address 0x" +
                       Twine::utohexstr(VAddr) + " to the segment with index " +
                       Twine(&Phdr - (*ProgramHeadersOrError).data() + 1) +
                       ": the segment ends at 0x" +
                       Twine::utohexstr(Phdr.p_offset + Phdr.p_filesz) +
                       ", which is greater than the file size (0x" +
                       Twine::utohexstr(getBufSize()) + ")");

  return base() + Offset;
}

// Helper to extract and decode the next ULEB128 value as unsigned int.
// Returns zero and sets ULEBSizeErr if the ULEB128 value exceeds the unsigned
// int limit.
// Also returns zero if ULEBSizeErr is already in an error state.
// ULEBSizeErr is an out variable if an error occurs.
template <typename IntTy, std::enable_if_t<std::is_unsigned_v<IntTy>, int> = 0>
static IntTy readULEB128As(DataExtractor &Data, DataExtractor::Cursor &Cur,
                           Error &ULEBSizeErr) {
  // Bail out and do not extract data if ULEBSizeErr is already set.
  if (ULEBSizeErr)
    return 0;
  uint64_t Offset = Cur.tell();
  uint64_t Value = Data.getULEB128(Cur);
  if (Value > std::numeric_limits<IntTy>::max()) {
    ULEBSizeErr = createError("ULEB128 value at offset 0x" +
                              Twine::utohexstr(Offset) + " exceeds UINT" +
                              Twine(std::numeric_limits<IntTy>::digits) +
                              "_MAX (0x" + Twine::utohexstr(Value) + ")");
    return 0;
  }
  return static_cast<IntTy>(Value);
}

template <typename ELFT>
static Expected<std::vector<BBAddrMap>>
decodeBBAddrMapImpl(const ELFFile<ELFT> &EF,
                    const typename ELFFile<ELFT>::Elf_Shdr &Sec,
                    const typename ELFFile<ELFT>::Elf_Shdr *RelaSec,
                    std::vector<PGOAnalysisMap> *PGOAnalyses) {
  bool IsRelocatable = EF.getHeader().e_type == ELF::ET_REL;

  // This DenseMap maps the offset of each function (the location of the
  // reference to the function in the SHT_LLVM_BB_ADDR_MAP section) to the
  // addend (the location of the function in the text section).
  llvm::DenseMap<uint64_t, uint64_t> FunctionOffsetTranslations;
  if (IsRelocatable && RelaSec) {
    assert(RelaSec &&
           "Can't read a SHT_LLVM_BB_ADDR_MAP section in a relocatable "
           "object file without providing a relocation section.");
    Expected<typename ELFFile<ELFT>::Elf_Rela_Range> Relas = EF.relas(*RelaSec);
    if (!Relas)
      return createError("unable to read relocations for section " +
                         describe(EF, Sec) + ": " +
                         toString(Relas.takeError()));
    for (typename ELFFile<ELFT>::Elf_Rela Rela : *Relas)
      FunctionOffsetTranslations[Rela.r_offset] = Rela.r_addend;
  }
  auto GetAddressForRelocation =
      [&](unsigned RelocationOffsetInSection) -> Expected<unsigned> {
    auto FOTIterator =
        FunctionOffsetTranslations.find(RelocationOffsetInSection);
    if (FOTIterator == FunctionOffsetTranslations.end()) {
      return createError("failed to get relocation data for offset: " +
                         Twine::utohexstr(RelocationOffsetInSection) +
                         " in section " + describe(EF, Sec));
    }
    return FOTIterator->second;
  };
  Expected<ArrayRef<uint8_t>> ContentsOrErr = EF.getSectionContents(Sec);
  if (!ContentsOrErr)
    return ContentsOrErr.takeError();
  ArrayRef<uint8_t> Content = *ContentsOrErr;
  DataExtractor Data(Content, EF.isLE(), ELFT::Is64Bits ? 8 : 4);
  std::vector<BBAddrMap> FunctionEntries;

  DataExtractor::Cursor Cur(0);
  Error ULEBSizeErr = Error::success();
  Error MetadataDecodeErr = Error::success();

  // Helper lampda to extract the (possiblly relocatable) address stored at Cur.
  auto ExtractAddress = [&]() -> Expected<typename ELFFile<ELFT>::uintX_t> {
    uint64_t RelocationOffsetInSection = Cur.tell();
    auto Address =
        static_cast<typename ELFFile<ELFT>::uintX_t>(Data.getAddress(Cur));
    if (!Cur)
      return Cur.takeError();
    if (!IsRelocatable)
      return Address;
    assert(Address == 0);
    Expected<unsigned> AddressOrErr =
        GetAddressForRelocation(RelocationOffsetInSection);
    if (!AddressOrErr)
      return AddressOrErr.takeError();
    return *AddressOrErr;
  };

  uint8_t Version = 0;
  uint8_t Feature = 0;
  BBAddrMap::Features FeatEnable{};
  while (!ULEBSizeErr && !MetadataDecodeErr && Cur &&
         Cur.tell() < Content.size()) {
    if (Sec.sh_type == ELF::SHT_LLVM_BB_ADDR_MAP) {
      Version = Data.getU8(Cur);
      if (!Cur)
        break;
      if (Version > 2)
        return createError("unsupported SHT_LLVM_BB_ADDR_MAP version: " +
                           Twine(static_cast<int>(Version)));
      Feature = Data.getU8(Cur); // Feature byte
      if (!Cur)
        break;
      auto FeatEnableOrErr = BBAddrMap::Features::decode(Feature);
      if (!FeatEnableOrErr)
        return FeatEnableOrErr.takeError();
      FeatEnable = *FeatEnableOrErr;
      if (Feature != 0 && Version < 2 && Cur)
        return createError(
            "version should be >= 2 for SHT_LLVM_BB_ADDR_MAP when "
            "PGO features are enabled: version = " +
            Twine(static_cast<int>(Version)) +
            " feature = " + Twine(static_cast<int>(Feature)));
    }
    uint32_t NumBlocksInBBRange = 0;
    uint32_t NumBBRanges = 1;
    typename ELFFile<ELFT>::uintX_t RangeBaseAddress = 0;
    std::vector<BBAddrMap::BBEntry> BBEntries;
    if (FeatEnable.MultiBBRange) {
      NumBBRanges = readULEB128As<uint32_t>(Data, Cur, ULEBSizeErr);
      if (!Cur || ULEBSizeErr)
        break;
      if (!NumBBRanges)
        return createError("invalid zero number of BB ranges at offset " +
                           Twine::utohexstr(Cur.tell()) + " in " +
                           describe(EF, Sec));
    } else {
      auto AddressOrErr = ExtractAddress();
      if (!AddressOrErr)
        return AddressOrErr.takeError();
      RangeBaseAddress = *AddressOrErr;
      NumBlocksInBBRange = readULEB128As<uint32_t>(Data, Cur, ULEBSizeErr);
    }
    std::vector<BBAddrMap::BBRangeEntry> BBRangeEntries;
    uint32_t TotalNumBlocks = 0;
    for (uint32_t BBRangeIndex = 0; BBRangeIndex < NumBBRanges;
         ++BBRangeIndex) {
      uint32_t PrevBBEndOffset = 0;
      if (FeatEnable.MultiBBRange) {
        auto AddressOrErr = ExtractAddress();
        if (!AddressOrErr)
          return AddressOrErr.takeError();
        RangeBaseAddress = *AddressOrErr;
        NumBlocksInBBRange = readULEB128As<uint32_t>(Data, Cur, ULEBSizeErr);
      }
      for (uint32_t BlockIndex = 0; !MetadataDecodeErr && !ULEBSizeErr && Cur &&
                                    (BlockIndex < NumBlocksInBBRange);
           ++BlockIndex) {
        uint32_t ID = Version >= 2
                          ? readULEB128As<uint32_t>(Data, Cur, ULEBSizeErr)
                          : BlockIndex;
        uint32_t Offset = readULEB128As<uint32_t>(Data, Cur, ULEBSizeErr);
        uint32_t Size = readULEB128As<uint32_t>(Data, Cur, ULEBSizeErr);
        uint32_t MD = readULEB128As<uint32_t>(Data, Cur, ULEBSizeErr);
        if (Version >= 1) {
          // Offset is calculated relative to the end of the previous BB.
          Offset += PrevBBEndOffset;
          PrevBBEndOffset = Offset + Size;
        }
        Expected<BBAddrMap::BBEntry::Metadata> MetadataOrErr =
            BBAddrMap::BBEntry::Metadata::decode(MD);
        if (!MetadataOrErr) {
          MetadataDecodeErr = MetadataOrErr.takeError();
          break;
        }
        BBEntries.push_back({ID, Offset, Size, *MetadataOrErr});
      }
      TotalNumBlocks += BBEntries.size();
      BBRangeEntries.push_back({RangeBaseAddress, std::move(BBEntries)});
    }
    FunctionEntries.push_back({std::move(BBRangeEntries)});

    if (PGOAnalyses || FeatEnable.hasPGOAnalysis()) {
      // Function entry count
      uint64_t FuncEntryCount =
          FeatEnable.FuncEntryCount
              ? readULEB128As<uint64_t>(Data, Cur, ULEBSizeErr)
              : 0;

      std::vector<PGOAnalysisMap::PGOBBEntry> PGOBBEntries;
      for (uint32_t BlockIndex = 0;
           FeatEnable.hasPGOAnalysisBBData() && !MetadataDecodeErr &&
           !ULEBSizeErr && Cur && (BlockIndex < TotalNumBlocks);
           ++BlockIndex) {
        // Block frequency
        uint64_t BBF = FeatEnable.BBFreq
                           ? readULEB128As<uint64_t>(Data, Cur, ULEBSizeErr)
                           : 0;

        // Branch probability
        llvm::SmallVector<PGOAnalysisMap::PGOBBEntry::SuccessorEntry, 2>
            Successors;
        if (FeatEnable.BrProb) {
          auto SuccCount = readULEB128As<uint64_t>(Data, Cur, ULEBSizeErr);
          for (uint64_t I = 0; I < SuccCount; ++I) {
            uint32_t BBID = readULEB128As<uint32_t>(Data, Cur, ULEBSizeErr);
            uint32_t BrProb = readULEB128As<uint32_t>(Data, Cur, ULEBSizeErr);
            if (PGOAnalyses)
              Successors.push_back({BBID, BranchProbability::getRaw(BrProb)});
          }
        }

        if (PGOAnalyses)
          PGOBBEntries.push_back({BlockFrequency(BBF), std::move(Successors)});
      }

      if (PGOAnalyses)
        PGOAnalyses->push_back(
            {FuncEntryCount, std::move(PGOBBEntries), FeatEnable});
    }
  }
  // Either Cur is in the error state, or we have an error in ULEBSizeErr or
  // MetadataDecodeErr (but not both), but we join all errors here to be safe.
  if (!Cur || ULEBSizeErr || MetadataDecodeErr)
    return joinErrors(joinErrors(Cur.takeError(), std::move(ULEBSizeErr)),
                      std::move(MetadataDecodeErr));
  return FunctionEntries;
}

template <class ELFT>
Expected<std::vector<BBAddrMap>>
ELFFile<ELFT>::decodeBBAddrMap(const Elf_Shdr &Sec, const Elf_Shdr *RelaSec,
                               std::vector<PGOAnalysisMap> *PGOAnalyses) const {
  size_t OriginalPGOSize = PGOAnalyses ? PGOAnalyses->size() : 0;
  auto AddrMapsOrErr = decodeBBAddrMapImpl(*this, Sec, RelaSec, PGOAnalyses);
  // remove new analyses when an error occurs
  if (!AddrMapsOrErr && PGOAnalyses)
    PGOAnalyses->resize(OriginalPGOSize);
  return std::move(AddrMapsOrErr);
}

template <class ELFT>
Expected<
    MapVector<const typename ELFT::Shdr *, const typename ELFT::Shdr *>>
ELFFile<ELFT>::getSectionAndRelocations(
    std::function<Expected<bool>(const Elf_Shdr &)> IsMatch) const {
  MapVector<const Elf_Shdr *, const Elf_Shdr *> SecToRelocMap;
  Error Errors = Error::success();
  for (const Elf_Shdr &Sec : cantFail(this->sections())) {
    Expected<bool> DoesSectionMatch = IsMatch(Sec);
    if (!DoesSectionMatch) {
      Errors = joinErrors(std::move(Errors), DoesSectionMatch.takeError());
      continue;
    }
    if (*DoesSectionMatch) {
      if (SecToRelocMap.insert(std::make_pair(&Sec, (const Elf_Shdr *)nullptr))
              .second)
        continue;
    }

    if (Sec.sh_type != ELF::SHT_RELA && Sec.sh_type != ELF::SHT_REL)
      continue;

    Expected<const Elf_Shdr *> RelSecOrErr = this->getSection(Sec.sh_info);
    if (!RelSecOrErr) {
      Errors = joinErrors(std::move(Errors),
                          createError(describe(*this, Sec) +
                                      ": failed to get a relocated section: " +
                                      toString(RelSecOrErr.takeError())));
      continue;
    }
    const Elf_Shdr *ContentsSec = *RelSecOrErr;
    Expected<bool> DoesRelTargetMatch = IsMatch(*ContentsSec);
    if (!DoesRelTargetMatch) {
      Errors = joinErrors(std::move(Errors), DoesRelTargetMatch.takeError());
      continue;
    }
    if (*DoesRelTargetMatch)
      SecToRelocMap[ContentsSec] = &Sec;
  }
  if(Errors)
    return std::move(Errors);
  return SecToRelocMap;
}

template class llvm::object::ELFFile<ELF32LE>;
template class llvm::object::ELFFile<ELF32BE>;
template class llvm::object::ELFFile<ELF64LE>;
template class llvm::object::ELFFile<ELF64BE>;
