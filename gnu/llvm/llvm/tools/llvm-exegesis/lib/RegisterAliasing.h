//===-- RegisterAliasingTracker.h -------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Defines classes to keep track of register aliasing.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVM_EXEGESIS_ALIASINGTRACKER_H
#define LLVM_TOOLS_LLVM_EXEGESIS_ALIASINGTRACKER_H

#include <memory>
#include <unordered_map>

#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/PackedVector.h"
#include "llvm/MC/MCRegisterInfo.h"

namespace llvm {
namespace exegesis {

// Returns the registers that are aliased by the ones set in SourceBits.
BitVector getAliasedBits(const MCRegisterInfo &RegInfo,
                         const BitVector &SourceBits);

// Keeps track of a mapping from one register (or a register class) to its
// aliased registers.
//
// e.g.
// RegisterAliasingTracker Tracker(RegInfo, X86::EAX);
// Tracker.sourceBits() == { X86::EAX }
// Tracker.aliasedBits() == { X86::AL, X86::AH, X86::AX,
//                            X86::EAX,X86::HAX, X86::RAX }
// Tracker.getOrigin(X86::AL) == X86::EAX;
// Tracker.getOrigin(X86::BX) == -1;
struct RegisterAliasingTracker {
  // Construct a tracker from an MCRegisterClass.
  RegisterAliasingTracker(const MCRegisterInfo &RegInfo,
                          const BitVector &ReservedReg,
                          const MCRegisterClass &RegClass);

  // Construct a tracker from an MCPhysReg.
  RegisterAliasingTracker(const MCRegisterInfo &RegInfo,
                          const MCPhysReg Register);

  const BitVector &sourceBits() const { return SourceBits; }

  // Retrieves all the touched registers as a BitVector.
  const BitVector &aliasedBits() const { return AliasedBits; }

  // Returns the origin of this register or -1.
  int getOrigin(MCPhysReg Aliased) const {
    if (!AliasedBits[Aliased])
      return -1;
    return Origins[Aliased];
  }

private:
  RegisterAliasingTracker(const MCRegisterInfo &RegInfo);
  RegisterAliasingTracker(const RegisterAliasingTracker &) = delete;

  void FillOriginAndAliasedBits(const MCRegisterInfo &RegInfo,
                                const BitVector &OriginalBits);

  BitVector SourceBits;
  BitVector AliasedBits;
  PackedVector<size_t, 10> Origins; // Max 1024 physical registers.
};

// A cache of existing trackers.
struct RegisterAliasingTrackerCache {
  // RegInfo must outlive the cache.
  RegisterAliasingTrackerCache(const MCRegisterInfo &RegInfo,
                               const BitVector &ReservedReg);

  // Convenient function to retrieve a BitVector of the right size.
  const BitVector &emptyRegisters() const { return EmptyRegisters; }

  // Convenient function to retrieve the registers the function body can't use.
  const BitVector &reservedRegisters() const { return ReservedReg; }

  // Convenient function to retrieve the underlying MCRegInfo.
  const MCRegisterInfo &regInfo() const { return RegInfo; }

  // Retrieves the RegisterAliasingTracker for this particular register.
  const RegisterAliasingTracker &getRegister(MCPhysReg Reg) const;

  // Retrieves the RegisterAliasingTracker for this particular register class.
  const RegisterAliasingTracker &getRegisterClass(unsigned RegClassIndex) const;

private:
  const MCRegisterInfo &RegInfo;
  const BitVector ReservedReg;
  const BitVector EmptyRegisters;
  mutable std::unordered_map<unsigned, std::unique_ptr<RegisterAliasingTracker>>
      Registers;
  mutable std::unordered_map<unsigned, std::unique_ptr<RegisterAliasingTracker>>
      RegisterClasses;
};

// `a = a & ~b`, optimized for few bit sets in B and no allocation.
inline void remove(BitVector &A, const BitVector &B) {
  assert(A.size() == B.size());
  for (auto I : B.set_bits())
    A.reset(I);
}

// Returns a debug string for the list of registers.
std::string debugString(const MCRegisterInfo &RegInfo, const BitVector &Regs);

} // namespace exegesis
} // namespace llvm

#endif // LLVM_TOOLS_LLVM_EXEGESIS_ALIASINGTRACKER_H
