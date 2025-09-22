//===-- RuntimeDyldELFMips.cpp ---- ELF/Mips specific code. -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "RuntimeDyldELFMips.h"
#include "llvm/BinaryFormat/ELF.h"

#define DEBUG_TYPE "dyld"

void RuntimeDyldELFMips::resolveRelocation(const RelocationEntry &RE,
                                           uint64_t Value) {
  const SectionEntry &Section = Sections[RE.SectionID];
  if (IsMipsO32ABI)
    resolveMIPSO32Relocation(Section, RE.Offset, Value, RE.RelType, RE.Addend);
  else if (IsMipsN32ABI) {
    resolveMIPSN32Relocation(Section, RE.Offset, Value, RE.RelType, RE.Addend,
                             RE.SymOffset, RE.SectionID);
  } else if (IsMipsN64ABI)
    resolveMIPSN64Relocation(Section, RE.Offset, Value, RE.RelType, RE.Addend,
                             RE.SymOffset, RE.SectionID);
  else
    llvm_unreachable("Mips ABI not handled");
}

uint64_t RuntimeDyldELFMips::evaluateRelocation(const RelocationEntry &RE,
                                                uint64_t Value,
                                                uint64_t Addend) {
  if (IsMipsN32ABI) {
    const SectionEntry &Section = Sections[RE.SectionID];
    Value = evaluateMIPS64Relocation(Section, RE.Offset, Value, RE.RelType,
                                     Addend, RE.SymOffset, RE.SectionID);
    return Value;
  }
  llvm_unreachable("Not reachable");
}

void RuntimeDyldELFMips::applyRelocation(const RelocationEntry &RE,
                                         uint64_t Value) {
  if (IsMipsN32ABI) {
    const SectionEntry &Section = Sections[RE.SectionID];
    applyMIPSRelocation(Section.getAddressWithOffset(RE.Offset), Value,
                        RE.RelType);
    return;
  }
  llvm_unreachable("Not reachable");
}

int64_t
RuntimeDyldELFMips::evaluateMIPS32Relocation(const SectionEntry &Section,
                                             uint64_t Offset, uint64_t Value,
                                             uint32_t Type) {

  LLVM_DEBUG(dbgs() << "evaluateMIPS32Relocation, LocalAddress: 0x"
                    << format("%llx", Section.getAddressWithOffset(Offset))
                    << " FinalAddress: 0x"
                    << format("%llx", Section.getLoadAddressWithOffset(Offset))
                    << " Value: 0x" << format("%llx", Value) << " Type: 0x"
                    << format("%x", Type) << "\n");

  switch (Type) {
  default:
    llvm_unreachable("Unknown relocation type!");
    return Value;
  case ELF::R_MIPS_32:
    return Value;
  case ELF::R_MIPS_26:
    return Value >> 2;
  case ELF::R_MIPS_HI16:
    // Get the higher 16-bits. Also add 1 if bit 15 is 1.
    return (Value + 0x8000) >> 16;
  case ELF::R_MIPS_LO16:
    return Value;
  case ELF::R_MIPS_PC32: {
    uint32_t FinalAddress = Section.getLoadAddressWithOffset(Offset);
    return Value - FinalAddress;
  }
  case ELF::R_MIPS_PC16: {
    uint32_t FinalAddress = Section.getLoadAddressWithOffset(Offset);
    return (Value - FinalAddress) >> 2;
  }
  case ELF::R_MIPS_PC19_S2: {
    uint32_t FinalAddress = Section.getLoadAddressWithOffset(Offset);
    return (Value - (FinalAddress & ~0x3)) >> 2;
  }
  case ELF::R_MIPS_PC21_S2: {
    uint32_t FinalAddress = Section.getLoadAddressWithOffset(Offset);
    return (Value - FinalAddress) >> 2;
  }
  case ELF::R_MIPS_PC26_S2: {
    uint32_t FinalAddress = Section.getLoadAddressWithOffset(Offset);
    return (Value - FinalAddress) >> 2;
  }
  case ELF::R_MIPS_PCHI16: {
    uint32_t FinalAddress = Section.getLoadAddressWithOffset(Offset);
    return (Value - FinalAddress + 0x8000) >> 16;
  }
  case ELF::R_MIPS_PCLO16: {
    uint32_t FinalAddress = Section.getLoadAddressWithOffset(Offset);
    return Value - FinalAddress;
  }
  }
}

int64_t RuntimeDyldELFMips::evaluateMIPS64Relocation(
    const SectionEntry &Section, uint64_t Offset, uint64_t Value, uint32_t Type,
    int64_t Addend, uint64_t SymOffset, SID SectionID) {

  LLVM_DEBUG(dbgs() << "evaluateMIPS64Relocation, LocalAddress: 0x"
                    << format("%llx", Section.getAddressWithOffset(Offset))
                    << " FinalAddress: 0x"
                    << format("%llx", Section.getLoadAddressWithOffset(Offset))
                    << " Value: 0x" << format("%llx", Value) << " Type: 0x"
                    << format("%x", Type) << " Addend: 0x"
                    << format("%llx", Addend)
                    << " Offset: " << format("%llx" PRIx64, Offset)
                    << " SID: " << format("%d", SectionID)
                    << " SymOffset: " << format("%x", SymOffset) << "\n");

  switch (Type) {
  default:
    llvm_unreachable("Not implemented relocation type!");
    break;
  case ELF::R_MIPS_JALR:
  case ELF::R_MIPS_NONE:
    break;
  case ELF::R_MIPS_32:
  case ELF::R_MIPS_64:
    return Value + Addend;
  case ELF::R_MIPS_26:
    return ((Value + Addend) >> 2) & 0x3ffffff;
  case ELF::R_MIPS_GPREL16: {
    uint64_t GOTAddr = getSectionLoadAddress(SectionToGOTMap[SectionID]);
    return Value + Addend - (GOTAddr + 0x7ff0);
  }
  case ELF::R_MIPS_SUB:
    return Value - Addend;
  case ELF::R_MIPS_HI16:
    // Get the higher 16-bits. Also add 1 if bit 15 is 1.
    return ((Value + Addend + 0x8000) >> 16) & 0xffff;
  case ELF::R_MIPS_LO16:
    return (Value + Addend) & 0xffff;
  case ELF::R_MIPS_HIGHER:
    return ((Value + Addend + 0x80008000) >> 32) & 0xffff;
  case ELF::R_MIPS_HIGHEST:
    return ((Value + Addend + 0x800080008000) >> 48) & 0xffff;
  case ELF::R_MIPS_CALL16:
  case ELF::R_MIPS_GOT_DISP:
  case ELF::R_MIPS_GOT_PAGE: {
    uint8_t *LocalGOTAddr =
        getSectionAddress(SectionToGOTMap[SectionID]) + SymOffset;
    uint64_t GOTEntry = readBytesUnaligned(LocalGOTAddr, getGOTEntrySize());

    Value += Addend;
    if (Type == ELF::R_MIPS_GOT_PAGE)
      Value = (Value + 0x8000) & ~0xffff;

    if (GOTEntry)
      assert(GOTEntry == Value &&
                   "GOT entry has two different addresses.");
    else
      writeBytesUnaligned(Value, LocalGOTAddr, getGOTEntrySize());

    return (SymOffset - 0x7ff0) & 0xffff;
  }
  case ELF::R_MIPS_GOT_OFST: {
    int64_t page = (Value + Addend + 0x8000) & ~0xffff;
    return (Value + Addend - page) & 0xffff;
  }
  case ELF::R_MIPS_GPREL32: {
    uint64_t GOTAddr = getSectionLoadAddress(SectionToGOTMap[SectionID]);
    return Value + Addend - (GOTAddr + 0x7ff0);
  }
  case ELF::R_MIPS_PC16: {
    uint64_t FinalAddress = Section.getLoadAddressWithOffset(Offset);
    return ((Value + Addend - FinalAddress) >> 2) & 0xffff;
  }
  case ELF::R_MIPS_PC32: {
    uint64_t FinalAddress = Section.getLoadAddressWithOffset(Offset);
    return Value + Addend - FinalAddress;
  }
  case ELF::R_MIPS_PC18_S3: {
    uint64_t FinalAddress = Section.getLoadAddressWithOffset(Offset);
    return ((Value + Addend - (FinalAddress & ~0x7)) >> 3) & 0x3ffff;
  }
  case ELF::R_MIPS_PC19_S2: {
    uint64_t FinalAddress = Section.getLoadAddressWithOffset(Offset);
    return ((Value + Addend - (FinalAddress & ~0x3)) >> 2) & 0x7ffff;
  }
  case ELF::R_MIPS_PC21_S2: {
    uint64_t FinalAddress = Section.getLoadAddressWithOffset(Offset);
    return ((Value + Addend - FinalAddress) >> 2) & 0x1fffff;
  }
  case ELF::R_MIPS_PC26_S2: {
    uint64_t FinalAddress = Section.getLoadAddressWithOffset(Offset);
    return ((Value + Addend - FinalAddress) >> 2) & 0x3ffffff;
  }
  case ELF::R_MIPS_PCHI16: {
    uint64_t FinalAddress = Section.getLoadAddressWithOffset(Offset);
    return ((Value + Addend - FinalAddress + 0x8000) >> 16) & 0xffff;
  }
  case ELF::R_MIPS_PCLO16: {
    uint64_t FinalAddress = Section.getLoadAddressWithOffset(Offset);
    return (Value + Addend - FinalAddress) & 0xffff;
  }
  }
  return 0;
}

void RuntimeDyldELFMips::applyMIPSRelocation(uint8_t *TargetPtr, int64_t Value,
                                             uint32_t Type) {
  uint32_t Insn = readBytesUnaligned(TargetPtr, 4);

  switch (Type) {
  default:
    llvm_unreachable("Unknown relocation type!");
    break;
  case ELF::R_MIPS_GPREL16:
  case ELF::R_MIPS_HI16:
  case ELF::R_MIPS_LO16:
  case ELF::R_MIPS_HIGHER:
  case ELF::R_MIPS_HIGHEST:
  case ELF::R_MIPS_PC16:
  case ELF::R_MIPS_PCHI16:
  case ELF::R_MIPS_PCLO16:
  case ELF::R_MIPS_CALL16:
  case ELF::R_MIPS_GOT_DISP:
  case ELF::R_MIPS_GOT_PAGE:
  case ELF::R_MIPS_GOT_OFST:
    Insn = (Insn & 0xffff0000) | (Value & 0x0000ffff);
    writeBytesUnaligned(Insn, TargetPtr, 4);
    break;
  case ELF::R_MIPS_PC18_S3:
    Insn = (Insn & 0xfffc0000) | (Value & 0x0003ffff);
    writeBytesUnaligned(Insn, TargetPtr, 4);
    break;
  case ELF::R_MIPS_PC19_S2:
    Insn = (Insn & 0xfff80000) | (Value & 0x0007ffff);
    writeBytesUnaligned(Insn, TargetPtr, 4);
    break;
  case ELF::R_MIPS_PC21_S2:
    Insn = (Insn & 0xffe00000) | (Value & 0x001fffff);
    writeBytesUnaligned(Insn, TargetPtr, 4);
    break;
  case ELF::R_MIPS_26:
  case ELF::R_MIPS_PC26_S2:
    Insn = (Insn & 0xfc000000) | (Value & 0x03ffffff);
    writeBytesUnaligned(Insn, TargetPtr, 4);
    break;
  case ELF::R_MIPS_32:
  case ELF::R_MIPS_GPREL32:
  case ELF::R_MIPS_PC32:
    writeBytesUnaligned(Value & 0xffffffff, TargetPtr, 4);
    break;
  case ELF::R_MIPS_64:
  case ELF::R_MIPS_SUB:
    writeBytesUnaligned(Value, TargetPtr, 8);
    break;
  }
}

void RuntimeDyldELFMips::resolveMIPSN32Relocation(
    const SectionEntry &Section, uint64_t Offset, uint64_t Value, uint32_t Type,
    int64_t Addend, uint64_t SymOffset, SID SectionID) {
  int64_t CalculatedValue = evaluateMIPS64Relocation(
      Section, Offset, Value, Type, Addend, SymOffset, SectionID);
  applyMIPSRelocation(Section.getAddressWithOffset(Offset), CalculatedValue,
                      Type);
}

void RuntimeDyldELFMips::resolveMIPSN64Relocation(
    const SectionEntry &Section, uint64_t Offset, uint64_t Value, uint32_t Type,
    int64_t Addend, uint64_t SymOffset, SID SectionID) {
  uint32_t r_type = Type & 0xff;
  uint32_t r_type2 = (Type >> 8) & 0xff;
  uint32_t r_type3 = (Type >> 16) & 0xff;

  // RelType is used to keep information for which relocation type we are
  // applying relocation.
  uint32_t RelType = r_type;
  int64_t CalculatedValue = evaluateMIPS64Relocation(Section, Offset, Value,
                                                     RelType, Addend,
                                                     SymOffset, SectionID);
  if (r_type2 != ELF::R_MIPS_NONE) {
    RelType = r_type2;
    CalculatedValue = evaluateMIPS64Relocation(Section, Offset, 0, RelType,
                                               CalculatedValue, SymOffset,
                                               SectionID);
  }
  if (r_type3 != ELF::R_MIPS_NONE) {
    RelType = r_type3;
    CalculatedValue = evaluateMIPS64Relocation(Section, Offset, 0, RelType,
                                               CalculatedValue, SymOffset,
                                               SectionID);
  }
  applyMIPSRelocation(Section.getAddressWithOffset(Offset), CalculatedValue,
                      RelType);
}

void RuntimeDyldELFMips::resolveMIPSO32Relocation(const SectionEntry &Section,
                                                  uint64_t Offset,
                                                  uint32_t Value, uint32_t Type,
                                                  int32_t Addend) {
  uint8_t *TargetPtr = Section.getAddressWithOffset(Offset);
  Value += Addend;

  LLVM_DEBUG(dbgs() << "resolveMIPSO32Relocation, LocalAddress: "
                    << Section.getAddressWithOffset(Offset) << " FinalAddress: "
                    << format("%p", Section.getLoadAddressWithOffset(Offset))
                    << " Value: " << format("%x", Value) << " Type: "
                    << format("%x", Type) << " Addend: " << format("%x", Addend)
                    << " SymOffset: " << format("%x", Offset) << "\n");

  Value = evaluateMIPS32Relocation(Section, Offset, Value, Type);

  applyMIPSRelocation(TargetPtr, Value, Type);
}
