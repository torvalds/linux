//===- llvm/MC/LaneBitmask.h ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// A common definition of LaneBitmask for use in TableGen and CodeGen.
///
/// A lane mask is a bitmask representing the covering of a register with
/// sub-registers.
///
/// This is typically used to track liveness at sub-register granularity.
/// Lane masks for sub-register indices are similar to register units for
/// physical registers. The individual bits in a lane mask can't be assigned
/// any specific meaning. They can be used to check if two sub-register
/// indices overlap.
///
/// Iff the target has a register such that:
///
///   getSubReg(Reg, A) overlaps getSubReg(Reg, B)
///
/// then:
///
///   (getSubRegIndexLaneMask(A) & getSubRegIndexLaneMask(B)) != 0

#ifndef LLVM_MC_LANEBITMASK_H
#define LLVM_MC_LANEBITMASK_H

#include "llvm/Support/Compiler.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/Printable.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {

  struct LaneBitmask {
    // When changing the underlying type, change the format string as well.
    using Type = uint64_t;
    enum : unsigned { BitWidth = 8*sizeof(Type) };
    constexpr static const char *const FormatStr = "%016llX";

    constexpr LaneBitmask() = default;
    explicit constexpr LaneBitmask(Type V) : Mask(V) {}

    constexpr bool operator== (LaneBitmask M) const { return Mask == M.Mask; }
    constexpr bool operator!= (LaneBitmask M) const { return Mask != M.Mask; }
    constexpr bool operator< (LaneBitmask M)  const { return Mask < M.Mask; }
    constexpr bool none() const { return Mask == 0; }
    constexpr bool any()  const { return Mask != 0; }
    constexpr bool all()  const { return ~Mask == 0; }

    constexpr LaneBitmask operator~() const {
      return LaneBitmask(~Mask);
    }
    constexpr LaneBitmask operator|(LaneBitmask M) const {
      return LaneBitmask(Mask | M.Mask);
    }
    constexpr LaneBitmask operator&(LaneBitmask M) const {
      return LaneBitmask(Mask & M.Mask);
    }
    LaneBitmask &operator|=(LaneBitmask M) {
      Mask |= M.Mask;
      return *this;
    }
    LaneBitmask &operator&=(LaneBitmask M) {
      Mask &= M.Mask;
      return *this;
    }

    constexpr Type getAsInteger() const { return Mask; }

    unsigned getNumLanes() const { return llvm::popcount(Mask); }
    unsigned getHighestLane() const {
      return Log2_64(Mask);
    }

    static constexpr LaneBitmask getNone() { return LaneBitmask(0); }
    static constexpr LaneBitmask getAll() { return ~LaneBitmask(0); }
    static constexpr LaneBitmask getLane(unsigned Lane) {
      return LaneBitmask(Type(1) << Lane);
    }

  private:
    Type Mask = 0;
  };

  /// Create Printable object to print LaneBitmasks on a \ref raw_ostream.
  inline Printable PrintLaneMask(LaneBitmask LaneMask) {
    return Printable([LaneMask](raw_ostream &OS) {
      OS << format(LaneBitmask::FormatStr, LaneMask.getAsInteger());
    });
  }

} // end namespace llvm

#endif // LLVM_MC_LANEBITMASK_H
