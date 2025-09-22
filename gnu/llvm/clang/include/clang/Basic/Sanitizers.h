//===- Sanitizers.h - C Language Family Language Options --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// Defines the clang::SanitizerKind enum.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_BASIC_SANITIZERS_H
#define LLVM_CLANG_BASIC_SANITIZERS_H

#include "clang/Basic/LLVM.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/HashBuilder.h"
#include "llvm/Transforms/Instrumentation/AddressSanitizerOptions.h"
#include <cassert>
#include <cstdint>

namespace llvm {
class hash_code;
class Triple;
namespace opt {
class ArgList;
}
} // namespace llvm

namespace clang {

class SanitizerMask {
  // NOTE: this class assumes kNumElem == 2 in most of the constexpr functions,
  // in order to work within the C++11 constexpr function constraints. If you
  // change kNumElem, you'll need to update those member functions as well.

  /// Number of array elements.
  static constexpr unsigned kNumElem = 2;
  /// Mask value initialized to 0.
  uint64_t maskLoToHigh[kNumElem]{};
  /// Number of bits in a mask.
  static constexpr unsigned kNumBits = sizeof(decltype(maskLoToHigh)) * 8;
  /// Number of bits in a mask element.
  static constexpr unsigned kNumBitElem = sizeof(decltype(maskLoToHigh[0])) * 8;

  constexpr SanitizerMask(uint64_t mask1, uint64_t mask2)
      : maskLoToHigh{mask1, mask2} {}

public:
  SanitizerMask() = default;

  static constexpr bool checkBitPos(const unsigned Pos) {
    return Pos < kNumBits;
  }

  /// Create a mask with a bit enabled at position Pos.
  static constexpr SanitizerMask bitPosToMask(const unsigned Pos) {
    uint64_t mask1 = (Pos < kNumBitElem) ? 1ULL << (Pos % kNumBitElem) : 0;
    uint64_t mask2 = (Pos >= kNumBitElem && Pos < (kNumBitElem * 2))
                         ? 1ULL << (Pos % kNumBitElem)
                         : 0;
    return SanitizerMask(mask1, mask2);
  }

  unsigned countPopulation() const;

  void flipAllBits() {
    for (auto &Val : maskLoToHigh)
      Val = ~Val;
  }

  bool isPowerOf2() const {
    return countPopulation() == 1;
  }

  llvm::hash_code hash_value() const;

  template <typename HasherT, llvm::endianness Endianness>
  friend void addHash(llvm::HashBuilder<HasherT, Endianness> &HBuilder,
                      const SanitizerMask &SM) {
    HBuilder.addRange(&SM.maskLoToHigh[0], &SM.maskLoToHigh[kNumElem]);
  }

  constexpr explicit operator bool() const {
    return maskLoToHigh[0] || maskLoToHigh[1];
  }

  constexpr bool operator==(const SanitizerMask &V) const {
    return maskLoToHigh[0] == V.maskLoToHigh[0] &&
           maskLoToHigh[1] == V.maskLoToHigh[1];
  }

  SanitizerMask &operator&=(const SanitizerMask &RHS) {
    for (unsigned k = 0; k < kNumElem; k++)
      maskLoToHigh[k] &= RHS.maskLoToHigh[k];
    return *this;
  }

  SanitizerMask &operator|=(const SanitizerMask &RHS) {
    for (unsigned k = 0; k < kNumElem; k++)
      maskLoToHigh[k] |= RHS.maskLoToHigh[k];
    return *this;
  }

  constexpr bool operator!() const { return !bool(*this); }

  constexpr bool operator!=(const SanitizerMask &RHS) const {
    return !((*this) == RHS);
  }

  friend constexpr inline SanitizerMask operator~(SanitizerMask v) {
    return SanitizerMask(~v.maskLoToHigh[0], ~v.maskLoToHigh[1]);
  }

  friend constexpr inline SanitizerMask operator&(SanitizerMask a,
                                                  const SanitizerMask &b) {
    return SanitizerMask(a.maskLoToHigh[0] & b.maskLoToHigh[0],
                         a.maskLoToHigh[1] & b.maskLoToHigh[1]);
  }

  friend constexpr inline SanitizerMask operator|(SanitizerMask a,
                                                  const SanitizerMask &b) {
    return SanitizerMask(a.maskLoToHigh[0] | b.maskLoToHigh[0],
                         a.maskLoToHigh[1] | b.maskLoToHigh[1]);
  }
};

// Declaring in clang namespace so that it can be found by ADL.
llvm::hash_code hash_value(const clang::SanitizerMask &Arg);

// Define the set of sanitizer kinds, as well as the set of sanitizers each
// sanitizer group expands into.
struct SanitizerKind {
  // Assign ordinals to possible values of -fsanitize= flag, which we will use
  // as bit positions.
  enum SanitizerOrdinal : uint64_t {
#define SANITIZER(NAME, ID) SO_##ID,
#define SANITIZER_GROUP(NAME, ID, ALIAS) SO_##ID##Group,
#include "clang/Basic/Sanitizers.def"
    SO_Count
  };

#define SANITIZER(NAME, ID)                                                    \
  static constexpr SanitizerMask ID = SanitizerMask::bitPosToMask(SO_##ID);    \
  static_assert(SanitizerMask::checkBitPos(SO_##ID), "Bit position too big.");
#define SANITIZER_GROUP(NAME, ID, ALIAS)                                       \
  static constexpr SanitizerMask ID = SanitizerMask(ALIAS);                    \
  static constexpr SanitizerMask ID##Group =                                   \
      SanitizerMask::bitPosToMask(SO_##ID##Group);                             \
  static_assert(SanitizerMask::checkBitPos(SO_##ID##Group),                    \
                "Bit position too big.");
#include "clang/Basic/Sanitizers.def"
}; // SanitizerKind

struct SanitizerSet {
  /// Check if a certain (single) sanitizer is enabled.
  bool has(SanitizerMask K) const {
    assert(K.isPowerOf2() && "Has to be a single sanitizer.");
    return static_cast<bool>(Mask & K);
  }

  /// Check if one or more sanitizers are enabled.
  bool hasOneOf(SanitizerMask K) const { return static_cast<bool>(Mask & K); }

  /// Enable or disable a certain (single) sanitizer.
  void set(SanitizerMask K, bool Value) {
    assert(K.isPowerOf2() && "Has to be a single sanitizer.");
    Mask = Value ? (Mask | K) : (Mask & ~K);
  }

  void set(SanitizerMask K) { Mask = K; }

  /// Disable the sanitizers specified in \p K.
  void clear(SanitizerMask K = SanitizerKind::All) { Mask &= ~K; }

  /// Returns true if no sanitizers are enabled.
  bool empty() const { return !Mask; }

  /// Bitmask of enabled sanitizers.
  SanitizerMask Mask;
};

/// Parse a single value from a -fsanitize= or -fno-sanitize= value list.
/// Returns a non-zero SanitizerMask, or \c 0 if \p Value is not known.
SanitizerMask parseSanitizerValue(StringRef Value, bool AllowGroups);

/// Serialize a SanitizerSet into values for -fsanitize= or -fno-sanitize=.
void serializeSanitizerSet(SanitizerSet Set,
                           SmallVectorImpl<StringRef> &Values);

/// For each sanitizer group bit set in \p Kinds, set the bits for sanitizers
/// this group enables.
SanitizerMask expandSanitizerGroups(SanitizerMask Kinds);

/// Return the sanitizers which do not affect preprocessing.
inline SanitizerMask getPPTransparentSanitizers() {
  return SanitizerKind::CFI | SanitizerKind::Integer |
         SanitizerKind::ImplicitConversion | SanitizerKind::Nullability |
         SanitizerKind::Undefined | SanitizerKind::FloatDivideByZero;
}

StringRef AsanDtorKindToString(llvm::AsanDtorKind kind);

llvm::AsanDtorKind AsanDtorKindFromString(StringRef kind);

StringRef AsanDetectStackUseAfterReturnModeToString(
    llvm::AsanDetectStackUseAfterReturnMode mode);

llvm::AsanDetectStackUseAfterReturnMode
AsanDetectStackUseAfterReturnModeFromString(StringRef modeStr);

} // namespace clang

#endif // LLVM_CLANG_BASIC_SANITIZERS_H
