//===- llvm/ADT/PointerEmbeddedInt.h ----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_POINTEREMBEDDEDINT_H
#define LLVM_ADT_POINTEREMBEDDEDINT_H

#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/PointerLikeTypeTraits.h"
#include <cassert>
#include <climits>
#include <cstdint>
#include <type_traits>

namespace llvm {

/// Utility to embed an integer into a pointer-like type. This is specifically
/// intended to allow embedding integers where fewer bits are required than
/// exist in a pointer, and the integer can participate in abstractions along
/// side other pointer-like types. For example it can be placed into a \c
/// PointerSumType or \c PointerUnion.
///
/// Note that much like pointers, an integer value of zero has special utility
/// due to boolean conversions. For example, a non-null value can be tested for
/// in the above abstractions without testing the particular active member.
/// Also, the default constructed value zero initializes the integer.
template <typename IntT, int Bits = sizeof(IntT) * CHAR_BIT>
class PointerEmbeddedInt {
  uintptr_t Value = 0;

  // Note: This '<' is correct; using '<=' would result in some shifts
  // overflowing their storage types.
  static_assert(Bits < sizeof(uintptr_t) * CHAR_BIT,
                "Cannot embed more bits than we have in a pointer!");

  enum : uintptr_t {
    // We shift as many zeros into the value as we can while preserving the
    // number of bits desired for the integer.
    Shift = sizeof(uintptr_t) * CHAR_BIT - Bits,

    // We also want to be able to mask out the preserved bits for asserts.
    Mask = static_cast<uintptr_t>(-1) << Bits
  };

  struct RawValueTag {
    explicit RawValueTag() = default;
  };

  friend struct PointerLikeTypeTraits<PointerEmbeddedInt>;

  explicit PointerEmbeddedInt(uintptr_t Value, RawValueTag) : Value(Value) {}

public:
  PointerEmbeddedInt() = default;

  PointerEmbeddedInt(IntT I) { *this = I; }

  PointerEmbeddedInt &operator=(IntT I) {
    assert((std::is_signed<IntT>::value ? isInt<Bits>(I) : isUInt<Bits>(I)) &&
           "Integer has bits outside those preserved!");
    Value = static_cast<uintptr_t>(I) << Shift;
    return *this;
  }

  // Note that this implicit conversion additionally allows all of the basic
  // comparison operators to work transparently, etc.
  operator IntT() const {
    if (std::is_signed<IntT>::value)
      return static_cast<IntT>(static_cast<intptr_t>(Value) >> Shift);
    return static_cast<IntT>(Value >> Shift);
  }
};

// Provide pointer like traits to support use with pointer unions and sum
// types.
template <typename IntT, int Bits>
struct PointerLikeTypeTraits<PointerEmbeddedInt<IntT, Bits>> {
  using T = PointerEmbeddedInt<IntT, Bits>;

  static inline void *getAsVoidPointer(const T &P) {
    return reinterpret_cast<void *>(P.Value);
  }

  static inline T getFromVoidPointer(void *P) {
    return T(reinterpret_cast<uintptr_t>(P), typename T::RawValueTag());
  }

  static inline T getFromVoidPointer(const void *P) {
    return T(reinterpret_cast<uintptr_t>(P), typename T::RawValueTag());
  }

  static constexpr int NumLowBitsAvailable = T::Shift;
};

// Teach DenseMap how to use PointerEmbeddedInt objects as keys if the Int type
// itself can be a key.
template <typename IntT, int Bits>
struct DenseMapInfo<PointerEmbeddedInt<IntT, Bits>> {
  using T = PointerEmbeddedInt<IntT, Bits>;
  using IntInfo = DenseMapInfo<IntT>;

  static inline T getEmptyKey() { return IntInfo::getEmptyKey(); }
  static inline T getTombstoneKey() { return IntInfo::getTombstoneKey(); }

  static unsigned getHashValue(const T &Arg) {
    return IntInfo::getHashValue(Arg);
  }

  static bool isEqual(const T &LHS, const T &RHS) { return LHS == RHS; }
};

} // end namespace llvm

#endif // LLVM_ADT_POINTEREMBEDDEDINT_H
