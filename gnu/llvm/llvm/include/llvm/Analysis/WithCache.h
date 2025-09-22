//===- llvm/Analysis/WithCache.h - KnownBits cache for pointers -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Store a pointer to any type along with the KnownBits information for it
// that is computed lazily (if required).
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_WITHCACHE_H
#define LLVM_ANALYSIS_WITHCACHE_H

#include "llvm/ADT/PointerIntPair.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/KnownBits.h"
#include <type_traits>

namespace llvm {
struct SimplifyQuery;
KnownBits computeKnownBits(const Value *V, unsigned Depth,
                           const SimplifyQuery &Q);

template <typename Arg> class WithCache {
  static_assert(std::is_pointer_v<Arg>, "WithCache requires a pointer type!");

  using UnderlyingType = std::remove_pointer_t<Arg>;
  constexpr static bool IsConst = std::is_const_v<Arg>;

  template <typename T, bool Const>
  using conditionally_const_t = std::conditional_t<Const, const T, T>;

  using PointerType = conditionally_const_t<UnderlyingType *, IsConst>;
  using ReferenceType = conditionally_const_t<UnderlyingType &, IsConst>;

  // Store the presence of the KnownBits information in one of the bits of
  // Pointer.
  // true  -> present
  // false -> absent
  mutable PointerIntPair<PointerType, 1, bool> Pointer;
  mutable KnownBits Known;

  void calculateKnownBits(const SimplifyQuery &Q) const {
    Known = computeKnownBits(Pointer.getPointer(), 0, Q);
    Pointer.setInt(true);
  }

public:
  WithCache(PointerType Pointer) : Pointer(Pointer, false) {}
  WithCache(PointerType Pointer, const KnownBits &Known)
      : Pointer(Pointer, true), Known(Known) {}

  [[nodiscard]] PointerType getValue() const { return Pointer.getPointer(); }

  [[nodiscard]] const KnownBits &getKnownBits(const SimplifyQuery &Q) const {
    if (!hasKnownBits())
      calculateKnownBits(Q);
    return Known;
  }

  [[nodiscard]] bool hasKnownBits() const { return Pointer.getInt(); }

  operator PointerType() const { return Pointer.getPointer(); }
  PointerType operator->() const { return Pointer.getPointer(); }
  ReferenceType operator*() const { return *Pointer.getPointer(); }
};
} // namespace llvm

#endif
