//===- RDFRegisters.cpp ---------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/BitVector.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/RDFRegisters.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/MC/LaneBitmask.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <cstdint>
#include <set>
#include <utility>

namespace llvm::rdf {

PhysicalRegisterInfo::PhysicalRegisterInfo(const TargetRegisterInfo &tri,
                                           const MachineFunction &mf)
    : TRI(tri) {
  RegInfos.resize(TRI.getNumRegs());

  BitVector BadRC(TRI.getNumRegs());
  for (const TargetRegisterClass *RC : TRI.regclasses()) {
    for (MCPhysReg R : *RC) {
      RegInfo &RI = RegInfos[R];
      if (RI.RegClass != nullptr && !BadRC[R]) {
        if (RC->LaneMask != RI.RegClass->LaneMask) {
          BadRC.set(R);
          RI.RegClass = nullptr;
        }
      } else
        RI.RegClass = RC;
    }
  }

  UnitInfos.resize(TRI.getNumRegUnits());

  for (uint32_t U = 0, NU = TRI.getNumRegUnits(); U != NU; ++U) {
    if (UnitInfos[U].Reg != 0)
      continue;
    MCRegUnitRootIterator R(U, &TRI);
    assert(R.isValid());
    RegisterId F = *R;
    ++R;
    if (R.isValid()) {
      UnitInfos[U].Mask = LaneBitmask::getAll();
      UnitInfos[U].Reg = F;
    } else {
      for (MCRegUnitMaskIterator I(F, &TRI); I.isValid(); ++I) {
        std::pair<uint32_t, LaneBitmask> P = *I;
        UnitInfo &UI = UnitInfos[P.first];
        UI.Reg = F;
        UI.Mask = P.second;
      }
    }
  }

  for (const uint32_t *RM : TRI.getRegMasks())
    RegMasks.insert(RM);
  for (const MachineBasicBlock &B : mf)
    for (const MachineInstr &In : B)
      for (const MachineOperand &Op : In.operands())
        if (Op.isRegMask())
          RegMasks.insert(Op.getRegMask());

  MaskInfos.resize(RegMasks.size() + 1);
  for (uint32_t M = 1, NM = RegMasks.size(); M <= NM; ++M) {
    BitVector PU(TRI.getNumRegUnits());
    const uint32_t *MB = RegMasks.get(M);
    for (unsigned I = 1, E = TRI.getNumRegs(); I != E; ++I) {
      if (!(MB[I / 32] & (1u << (I % 32))))
        continue;
      for (MCRegUnit Unit : TRI.regunits(MCRegister::from(I)))
        PU.set(Unit);
    }
    MaskInfos[M].Units = PU.flip();
  }

  AliasInfos.resize(TRI.getNumRegUnits());
  for (uint32_t U = 0, NU = TRI.getNumRegUnits(); U != NU; ++U) {
    BitVector AS(TRI.getNumRegs());
    for (MCRegUnitRootIterator R(U, &TRI); R.isValid(); ++R)
      for (MCPhysReg S : TRI.superregs_inclusive(*R))
        AS.set(S);
    AliasInfos[U].Regs = AS;
  }
}

bool PhysicalRegisterInfo::alias(RegisterRef RA, RegisterRef RB) const {
  return !disjoint(getUnits(RA), getUnits(RB));
}

std::set<RegisterId> PhysicalRegisterInfo::getAliasSet(RegisterId Reg) const {
  // Do not include Reg in the alias set.
  std::set<RegisterId> AS;
  assert(!RegisterRef::isUnitId(Reg) && "No units allowed");
  if (RegisterRef::isMaskId(Reg)) {
    // XXX SLOW
    const uint32_t *MB = getRegMaskBits(Reg);
    for (unsigned i = 1, e = TRI.getNumRegs(); i != e; ++i) {
      if (MB[i / 32] & (1u << (i % 32)))
        continue;
      AS.insert(i);
    }
    return AS;
  }

  assert(RegisterRef::isRegId(Reg));
  for (MCRegAliasIterator AI(Reg, &TRI, false); AI.isValid(); ++AI)
    AS.insert(*AI);

  return AS;
}

std::set<RegisterId> PhysicalRegisterInfo::getUnits(RegisterRef RR) const {
  std::set<RegisterId> Units;

  if (RR.Reg == 0)
    return Units; // Empty

  if (RR.isReg()) {
    if (RR.Mask.none())
      return Units; // Empty
    for (MCRegUnitMaskIterator UM(RR.idx(), &TRI); UM.isValid(); ++UM) {
      auto [U, M] = *UM;
      if ((M & RR.Mask).any())
        Units.insert(U);
    }
    return Units;
  }

  assert(RR.isMask());
  unsigned NumRegs = TRI.getNumRegs();
  const uint32_t *MB = getRegMaskBits(RR.idx());
  for (unsigned I = 0, E = (NumRegs + 31) / 32; I != E; ++I) {
    uint32_t C = ~MB[I]; // Clobbered regs
    if (I == 0)          // Reg 0 should be ignored
      C &= maskLeadingOnes<unsigned>(31);
    if (I + 1 == E && NumRegs % 32 != 0) // Last word may be partial
      C &= maskTrailingOnes<unsigned>(NumRegs % 32);
    if (C == 0)
      continue;
    while (C != 0) {
      unsigned T = llvm::countr_zero(C);
      unsigned CR = 32 * I + T; // Clobbered reg
      for (MCRegUnit U : TRI.regunits(CR))
        Units.insert(U);
      C &= ~(1u << T);
    }
  }
  return Units;
}

RegisterRef PhysicalRegisterInfo::mapTo(RegisterRef RR, unsigned R) const {
  if (RR.Reg == R)
    return RR;
  if (unsigned Idx = TRI.getSubRegIndex(R, RR.Reg))
    return RegisterRef(R, TRI.composeSubRegIndexLaneMask(Idx, RR.Mask));
  if (unsigned Idx = TRI.getSubRegIndex(RR.Reg, R)) {
    const RegInfo &RI = RegInfos[R];
    LaneBitmask RCM =
        RI.RegClass ? RI.RegClass->LaneMask : LaneBitmask::getAll();
    LaneBitmask M = TRI.reverseComposeSubRegIndexLaneMask(Idx, RR.Mask);
    return RegisterRef(R, M & RCM);
  }
  llvm_unreachable("Invalid arguments: unrelated registers?");
}

bool PhysicalRegisterInfo::equal_to(RegisterRef A, RegisterRef B) const {
  if (!A.isReg() || !B.isReg()) {
    // For non-regs, or comparing reg and non-reg, use only the Reg member.
    return A.Reg == B.Reg;
  }

  if (A.Reg == B.Reg)
    return A.Mask == B.Mask;

  // Compare reg units lexicographically.
  MCRegUnitMaskIterator AI(A.Reg, &getTRI());
  MCRegUnitMaskIterator BI(B.Reg, &getTRI());
  while (AI.isValid() && BI.isValid()) {
    auto [AReg, AMask] = *AI;
    auto [BReg, BMask] = *BI;

    // If both iterators point to a unit contained in both A and B, then
    // compare the units.
    if ((AMask & A.Mask).any() && (BMask & B.Mask).any()) {
      if (AReg != BReg)
        return false;
      // Units are equal, move on to the next ones.
      ++AI;
      ++BI;
      continue;
    }

    if ((AMask & A.Mask).none())
      ++AI;
    if ((BMask & B.Mask).none())
      ++BI;
  }
  // One or both have reached the end.
  return static_cast<int>(AI.isValid()) == static_cast<int>(BI.isValid());
}

bool PhysicalRegisterInfo::less(RegisterRef A, RegisterRef B) const {
  if (!A.isReg() || !B.isReg()) {
    // For non-regs, or comparing reg and non-reg, use only the Reg member.
    return A.Reg < B.Reg;
  }

  if (A.Reg == B.Reg)
    return A.Mask < B.Mask;
  if (A.Mask == B.Mask)
    return A.Reg < B.Reg;

  // Compare reg units lexicographically.
  llvm::MCRegUnitMaskIterator AI(A.Reg, &getTRI());
  llvm::MCRegUnitMaskIterator BI(B.Reg, &getTRI());
  while (AI.isValid() && BI.isValid()) {
    auto [AReg, AMask] = *AI;
    auto [BReg, BMask] = *BI;

    // If both iterators point to a unit contained in both A and B, then
    // compare the units.
    if ((AMask & A.Mask).any() && (BMask & B.Mask).any()) {
      if (AReg != BReg)
        return AReg < BReg;
      // Units are equal, move on to the next ones.
      ++AI;
      ++BI;
      continue;
    }

    if ((AMask & A.Mask).none())
      ++AI;
    if ((BMask & B.Mask).none())
      ++BI;
  }
  // One or both have reached the end: assume invalid < valid.
  return static_cast<int>(AI.isValid()) < static_cast<int>(BI.isValid());
}

void PhysicalRegisterInfo::print(raw_ostream &OS, RegisterRef A) const {
  if (A.Reg == 0 || A.isReg()) {
    if (0 < A.idx() && A.idx() < TRI.getNumRegs())
      OS << TRI.getName(A.idx());
    else
      OS << printReg(A.idx(), &TRI);
    OS << PrintLaneMaskShort(A.Mask);
  } else if (A.isUnit()) {
    OS << printRegUnit(A.idx(), &TRI);
  } else {
    assert(A.isMask());
    // RegMask SS flag is preserved by idx().
    unsigned Idx = Register::stackSlot2Index(A.idx());
    const char *Fmt = Idx < 0x10000 ? "%04x" : "%08x";
    OS << "M#" << format(Fmt, Idx);
  }
}

void PhysicalRegisterInfo::print(raw_ostream &OS, const RegisterAggr &A) const {
  OS << '{';
  for (unsigned U : A.units())
    OS << ' ' << printRegUnit(U, &TRI);
  OS << " }";
}

bool RegisterAggr::hasAliasOf(RegisterRef RR) const {
  if (RR.isMask())
    return Units.anyCommon(PRI.getMaskUnits(RR.Reg));

  for (MCRegUnitMaskIterator U(RR.Reg, &PRI.getTRI()); U.isValid(); ++U) {
    std::pair<uint32_t, LaneBitmask> P = *U;
    if ((P.second & RR.Mask).any())
      if (Units.test(P.first))
        return true;
  }
  return false;
}

bool RegisterAggr::hasCoverOf(RegisterRef RR) const {
  if (RR.isMask()) {
    BitVector T(PRI.getMaskUnits(RR.Reg));
    return T.reset(Units).none();
  }

  for (MCRegUnitMaskIterator U(RR.Reg, &PRI.getTRI()); U.isValid(); ++U) {
    std::pair<uint32_t, LaneBitmask> P = *U;
    if ((P.second & RR.Mask).any())
      if (!Units.test(P.first))
        return false;
  }
  return true;
}

RegisterAggr &RegisterAggr::insert(RegisterRef RR) {
  if (RR.isMask()) {
    Units |= PRI.getMaskUnits(RR.Reg);
    return *this;
  }

  for (MCRegUnitMaskIterator U(RR.Reg, &PRI.getTRI()); U.isValid(); ++U) {
    std::pair<uint32_t, LaneBitmask> P = *U;
    if ((P.second & RR.Mask).any())
      Units.set(P.first);
  }
  return *this;
}

RegisterAggr &RegisterAggr::insert(const RegisterAggr &RG) {
  Units |= RG.Units;
  return *this;
}

RegisterAggr &RegisterAggr::intersect(RegisterRef RR) {
  return intersect(RegisterAggr(PRI).insert(RR));
}

RegisterAggr &RegisterAggr::intersect(const RegisterAggr &RG) {
  Units &= RG.Units;
  return *this;
}

RegisterAggr &RegisterAggr::clear(RegisterRef RR) {
  return clear(RegisterAggr(PRI).insert(RR));
}

RegisterAggr &RegisterAggr::clear(const RegisterAggr &RG) {
  Units.reset(RG.Units);
  return *this;
}

RegisterRef RegisterAggr::intersectWith(RegisterRef RR) const {
  RegisterAggr T(PRI);
  T.insert(RR).intersect(*this);
  if (T.empty())
    return RegisterRef();
  RegisterRef NR = T.makeRegRef();
  assert(NR);
  return NR;
}

RegisterRef RegisterAggr::clearIn(RegisterRef RR) const {
  return RegisterAggr(PRI).insert(RR).clear(*this).makeRegRef();
}

RegisterRef RegisterAggr::makeRegRef() const {
  int U = Units.find_first();
  if (U < 0)
    return RegisterRef();

  // Find the set of all registers that are aliased to all the units
  // in this aggregate.

  // Get all the registers aliased to the first unit in the bit vector.
  BitVector Regs = PRI.getUnitAliases(U);
  U = Units.find_next(U);

  // For each other unit, intersect it with the set of all registers
  // aliased that unit.
  while (U >= 0) {
    Regs &= PRI.getUnitAliases(U);
    U = Units.find_next(U);
  }

  // If there is at least one register remaining, pick the first one,
  // and consolidate the masks of all of its units contained in this
  // aggregate.

  int F = Regs.find_first();
  if (F <= 0)
    return RegisterRef();

  LaneBitmask M;
  for (MCRegUnitMaskIterator I(F, &PRI.getTRI()); I.isValid(); ++I) {
    std::pair<uint32_t, LaneBitmask> P = *I;
    if (Units.test(P.first))
      M |= P.second;
  }
  return RegisterRef(F, M);
}

RegisterAggr::ref_iterator::ref_iterator(const RegisterAggr &RG, bool End)
    : Owner(&RG) {
  for (int U = RG.Units.find_first(); U >= 0; U = RG.Units.find_next(U)) {
    RegisterRef R = RG.PRI.getRefForUnit(U);
    Masks[R.Reg] |= R.Mask;
  }
  Pos = End ? Masks.end() : Masks.begin();
  Index = End ? Masks.size() : 0;
}

raw_ostream &operator<<(raw_ostream &OS, const RegisterAggr &A) {
  A.getPRI().print(OS, A);
  return OS;
}

raw_ostream &operator<<(raw_ostream &OS, const PrintLaneMaskShort &P) {
  if (P.Mask.all())
    return OS;
  if (P.Mask.none())
    return OS << ":*none*";

  LaneBitmask::Type Val = P.Mask.getAsInteger();
  if ((Val & 0xffff) == Val)
    return OS << ':' << format("%04llX", Val);
  if ((Val & 0xffffffff) == Val)
    return OS << ':' << format("%08llX", Val);
  return OS << ':' << PrintLaneMask(P.Mask);
}

} // namespace llvm::rdf
