//===-- RegisterAliasing.cpp ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "RegisterAliasing.h"

namespace llvm {
namespace exegesis {

BitVector getAliasedBits(const MCRegisterInfo &RegInfo,
                         const BitVector &SourceBits) {
  BitVector AliasedBits(RegInfo.getNumRegs());
  for (const size_t PhysReg : SourceBits.set_bits()) {
    using RegAliasItr = MCRegAliasIterator;
    for (auto Itr = RegAliasItr(PhysReg, &RegInfo, true); Itr.isValid();
         ++Itr) {
      AliasedBits.set(*Itr);
    }
  }
  return AliasedBits;
}

RegisterAliasingTracker::RegisterAliasingTracker(const MCRegisterInfo &RegInfo)
    : SourceBits(RegInfo.getNumRegs()), AliasedBits(RegInfo.getNumRegs()),
      Origins(RegInfo.getNumRegs()) {}

RegisterAliasingTracker::RegisterAliasingTracker(
    const MCRegisterInfo &RegInfo, const BitVector &ReservedReg,
    const MCRegisterClass &RegClass)
    : RegisterAliasingTracker(RegInfo) {
  for (MCPhysReg PhysReg : RegClass)
    if (!ReservedReg[PhysReg]) // Removing reserved registers.
      SourceBits.set(PhysReg);
  FillOriginAndAliasedBits(RegInfo, SourceBits);
}

RegisterAliasingTracker::RegisterAliasingTracker(const MCRegisterInfo &RegInfo,
                                                 const MCPhysReg PhysReg)
    : RegisterAliasingTracker(RegInfo) {
  SourceBits.set(PhysReg);
  FillOriginAndAliasedBits(RegInfo, SourceBits);
}

void RegisterAliasingTracker::FillOriginAndAliasedBits(
    const MCRegisterInfo &RegInfo, const BitVector &SourceBits) {
  using RegAliasItr = MCRegAliasIterator;
  for (const size_t PhysReg : SourceBits.set_bits()) {
    for (auto Itr = RegAliasItr(PhysReg, &RegInfo, true); Itr.isValid();
         ++Itr) {
      AliasedBits.set(*Itr);
      Origins[*Itr] = PhysReg;
    }
  }
}

RegisterAliasingTrackerCache::RegisterAliasingTrackerCache(
    const MCRegisterInfo &RegInfo, const BitVector &ReservedReg)
    : RegInfo(RegInfo), ReservedReg(ReservedReg),
      EmptyRegisters(RegInfo.getNumRegs()) {}

const RegisterAliasingTracker &
RegisterAliasingTrackerCache::getRegister(MCPhysReg PhysReg) const {
  auto &Found = Registers[PhysReg];
  if (!Found)
    Found.reset(new RegisterAliasingTracker(RegInfo, PhysReg));
  return *Found;
}

const RegisterAliasingTracker &
RegisterAliasingTrackerCache::getRegisterClass(unsigned RegClassIndex) const {
  auto &Found = RegisterClasses[RegClassIndex];
  const auto &RegClass = RegInfo.getRegClass(RegClassIndex);
  if (!Found)
    Found.reset(new RegisterAliasingTracker(RegInfo, ReservedReg, RegClass));
  return *Found;
}

std::string debugString(const MCRegisterInfo &RegInfo, const BitVector &Regs) {
  std::string Result;
  for (const unsigned Reg : Regs.set_bits()) {
    Result.append(RegInfo.getName(Reg));
    Result.push_back(' ');
  }
  return Result;
}

} // namespace exegesis
} // namespace llvm
