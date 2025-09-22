//===- MC/MCRegisterInfo.cpp - Target Register Description ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements MCRegisterInfo functions.
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/ErrorHandling.h"
#include <algorithm>
#include <cassert>
#include <cstdint>

using namespace llvm;

namespace {
/// MCRegAliasIterator enumerates all registers aliasing Reg.  This iterator
/// does not guarantee any ordering or that entries are unique.
class MCRegAliasIteratorImpl {
private:
  MCRegister Reg;
  const MCRegisterInfo *MCRI;

  MCRegUnitIterator RI;
  MCRegUnitRootIterator RRI;
  MCSuperRegIterator SI;

public:
  MCRegAliasIteratorImpl(MCRegister Reg, const MCRegisterInfo *MCRI)
      : Reg(Reg), MCRI(MCRI) {

    // Initialize the iterators.
    for (RI = MCRegUnitIterator(Reg, MCRI); RI.isValid(); ++RI) {
      for (RRI = MCRegUnitRootIterator(*RI, MCRI); RRI.isValid(); ++RRI) {
        for (SI = MCSuperRegIterator(*RRI, MCRI, true); SI.isValid(); ++SI) {
          if (Reg != *SI)
            return;
        }
      }
    }
  }

  bool isValid() const { return RI.isValid(); }

  MCRegister operator*() const {
    assert(SI.isValid() && "Cannot dereference an invalid iterator.");
    return *SI;
  }

  void advance() {
    // Assuming SI is valid.
    ++SI;
    if (SI.isValid())
      return;

    ++RRI;
    if (RRI.isValid()) {
      SI = MCSuperRegIterator(*RRI, MCRI, true);
      return;
    }

    ++RI;
    if (RI.isValid()) {
      RRI = MCRegUnitRootIterator(*RI, MCRI);
      SI = MCSuperRegIterator(*RRI, MCRI, true);
    }
  }

  MCRegAliasIteratorImpl &operator++() {
    assert(isValid() && "Cannot move off the end of the list.");
    do
      advance();
    while (isValid() && *SI == Reg);
    return *this;
  }
};
} // namespace

ArrayRef<MCPhysReg> MCRegisterInfo::getCachedAliasesOf(MCPhysReg R) const {
  auto &Aliases = RegAliasesCache[R];
  if (!Aliases.empty())
    return Aliases;

  for (MCRegAliasIteratorImpl It(R, this); It.isValid(); ++It)
    Aliases.push_back(*It);

  sort(Aliases);
  Aliases.erase(unique(Aliases), Aliases.end());
  assert(none_of(Aliases, [&](auto &Cur) { return R == Cur; }) &&
         "MCRegAliasIteratorImpl includes Self!");

  // Always put "self" at the end, so the iterator can choose to ignore it.
  // For registers without aliases, it also serves as a sentinel value that
  // tells us to not recompute the alias set.
  Aliases.push_back(R);
  Aliases.shrink_to_fit();
  return Aliases;
}

MCRegister
MCRegisterInfo::getMatchingSuperReg(MCRegister Reg, unsigned SubIdx,
                                    const MCRegisterClass *RC) const {
  for (MCPhysReg Super : superregs(Reg))
    if (RC->contains(Super) && Reg == getSubReg(Super, SubIdx))
      return Super;
  return 0;
}

MCRegister MCRegisterInfo::getSubReg(MCRegister Reg, unsigned Idx) const {
  assert(Idx && Idx < getNumSubRegIndices() &&
         "This is not a subregister index");
  // Get a pointer to the corresponding SubRegIndices list. This list has the
  // name of each sub-register in the same order as MCSubRegIterator.
  const uint16_t *SRI = SubRegIndices + get(Reg).SubRegIndices;
  for (MCPhysReg Sub : subregs(Reg)) {
    if (*SRI == Idx)
      return Sub;
    ++SRI;
  }
  return 0;
}

unsigned MCRegisterInfo::getSubRegIndex(MCRegister Reg,
                                        MCRegister SubReg) const {
  assert(SubReg && SubReg < getNumRegs() && "This is not a register");
  // Get a pointer to the corresponding SubRegIndices list. This list has the
  // name of each sub-register in the same order as MCSubRegIterator.
  const uint16_t *SRI = SubRegIndices + get(Reg).SubRegIndices;
  for (MCPhysReg Sub : subregs(Reg)) {
    if (Sub == SubReg)
      return *SRI;
    ++SRI;
  }
  return 0;
}

int MCRegisterInfo::getDwarfRegNum(MCRegister RegNum, bool isEH) const {
  const DwarfLLVMRegPair *M = isEH ? EHL2DwarfRegs : L2DwarfRegs;
  unsigned Size = isEH ? EHL2DwarfRegsSize : L2DwarfRegsSize;

  if (!M)
    return -1;
  DwarfLLVMRegPair Key = { RegNum, 0 };
  const DwarfLLVMRegPair *I = std::lower_bound(M, M+Size, Key);
  if (I == M+Size || I->FromReg != RegNum)
    return -1;
  return I->ToReg;
}

std::optional<unsigned> MCRegisterInfo::getLLVMRegNum(unsigned RegNum,
                                                      bool isEH) const {
  const DwarfLLVMRegPair *M = isEH ? EHDwarf2LRegs : Dwarf2LRegs;
  unsigned Size = isEH ? EHDwarf2LRegsSize : Dwarf2LRegsSize;

  if (!M)
    return std::nullopt;
  DwarfLLVMRegPair Key = { RegNum, 0 };
  const DwarfLLVMRegPair *I = std::lower_bound(M, M+Size, Key);
  if (I != M + Size && I->FromReg == RegNum)
    return I->ToReg;
  return std::nullopt;
}

int MCRegisterInfo::getDwarfRegNumFromDwarfEHRegNum(unsigned RegNum) const {
  // On ELF platforms, DWARF EH register numbers are the same as DWARF
  // other register numbers.  On Darwin x86, they differ and so need to be
  // mapped.  The .cfi_* directives accept integer literals as well as
  // register names and should generate exactly what the assembly code
  // asked for, so there might be DWARF/EH register numbers that don't have
  // a corresponding LLVM register number at all.  So if we can't map the
  // EH register number to an LLVM register number, assume it's just a
  // valid DWARF register number as is.
  if (std::optional<unsigned> LRegNum = getLLVMRegNum(RegNum, true)) {
    int DwarfRegNum = getDwarfRegNum(*LRegNum, false);
    if (DwarfRegNum == -1)
      return RegNum;
    else
      return DwarfRegNum;
  }
  return RegNum;
}

int MCRegisterInfo::getSEHRegNum(MCRegister RegNum) const {
  const DenseMap<MCRegister, int>::const_iterator I = L2SEHRegs.find(RegNum);
  if (I == L2SEHRegs.end()) return (int)RegNum;
  return I->second;
}

int MCRegisterInfo::getCodeViewRegNum(MCRegister RegNum) const {
  if (L2CVRegs.empty())
    report_fatal_error("target does not implement codeview register mapping");
  const DenseMap<MCRegister, int>::const_iterator I = L2CVRegs.find(RegNum);
  if (I == L2CVRegs.end())
    report_fatal_error("unknown codeview register " + (RegNum < getNumRegs()
                                                           ? getName(RegNum)
                                                           : Twine(RegNum)));
  return I->second;
}

bool MCRegisterInfo::regsOverlap(MCRegister RegA, MCRegister RegB) const {
  // Regunits are numerically ordered. Find a common unit.
  auto RangeA = regunits(RegA);
  MCRegUnitIterator IA = RangeA.begin(), EA = RangeA.end();
  auto RangeB = regunits(RegB);
  MCRegUnitIterator IB = RangeB.begin(), EB = RangeB.end();
  do {
    if (*IA == *IB)
      return true;
  } while (*IA < *IB ? ++IA != EA : ++IB != EB);
  return false;
}
