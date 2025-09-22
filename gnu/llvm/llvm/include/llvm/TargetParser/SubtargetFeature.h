//=== llvm/TargetParser/SubtargetFeature.h - CPU characteristics-*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file Defines and manages user or tool specified CPU characteristics.
/// The intent is to be able to package specific features that should or should
/// not be used on a specific target processor.  A tool, such as llc, could, as
/// as example, gather chip info from the command line, a long with features
/// that should be used on that chip.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TARGETPARSER_SUBTARGETFEATURE_H
#define LLVM_TARGETPARSER_SUBTARGETFEATURE_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/MathExtras.h"
#include <array>
#include <initializer_list>
#include <string>
#include <vector>

namespace llvm {

class raw_ostream;
class Triple;

const unsigned MAX_SUBTARGET_WORDS = 5;
const unsigned MAX_SUBTARGET_FEATURES = MAX_SUBTARGET_WORDS * 64;

/// Container class for subtarget features.
/// This is a constexpr reimplementation of a subset of std::bitset. It would be
/// nice to use std::bitset directly, but it doesn't support constant
/// initialization.
class FeatureBitset {
  static_assert((MAX_SUBTARGET_FEATURES % 64) == 0,
                "Should be a multiple of 64!");
  std::array<uint64_t, MAX_SUBTARGET_WORDS> Bits{};

protected:
  constexpr FeatureBitset(const std::array<uint64_t, MAX_SUBTARGET_WORDS> &B)
      : Bits{B} {}

public:
  constexpr FeatureBitset() = default;
  constexpr FeatureBitset(std::initializer_list<unsigned> Init) {
    for (auto I : Init)
      set(I);
  }

  FeatureBitset &set() {
    std::fill(std::begin(Bits), std::end(Bits), -1ULL);
    return *this;
  }

  constexpr FeatureBitset &set(unsigned I) {
    Bits[I / 64] |= uint64_t(1) << (I % 64);
    return *this;
  }

  constexpr FeatureBitset &reset(unsigned I) {
    Bits[I / 64] &= ~(uint64_t(1) << (I % 64));
    return *this;
  }

  constexpr FeatureBitset &flip(unsigned I) {
    Bits[I / 64] ^= uint64_t(1) << (I % 64);
    return *this;
  }

  constexpr bool operator[](unsigned I) const {
    uint64_t Mask = uint64_t(1) << (I % 64);
    return (Bits[I / 64] & Mask) != 0;
  }

  constexpr bool test(unsigned I) const { return (*this)[I]; }

  constexpr size_t size() const { return MAX_SUBTARGET_FEATURES; }

  bool any() const {
    return llvm::any_of(Bits, [](uint64_t I) { return I != 0; });
  }
  bool none() const { return !any(); }
  size_t count() const {
    size_t Count = 0;
    for (auto B : Bits)
      Count += llvm::popcount(B);
    return Count;
  }

  constexpr FeatureBitset &operator^=(const FeatureBitset &RHS) {
    for (unsigned I = 0, E = Bits.size(); I != E; ++I) {
      Bits[I] ^= RHS.Bits[I];
    }
    return *this;
  }
  constexpr FeatureBitset operator^(const FeatureBitset &RHS) const {
    FeatureBitset Result = *this;
    Result ^= RHS;
    return Result;
  }

  constexpr FeatureBitset &operator&=(const FeatureBitset &RHS) {
    for (unsigned I = 0, E = Bits.size(); I != E; ++I)
      Bits[I] &= RHS.Bits[I];
    return *this;
  }
  constexpr FeatureBitset operator&(const FeatureBitset &RHS) const {
    FeatureBitset Result = *this;
    Result &= RHS;
    return Result;
  }

  constexpr FeatureBitset &operator|=(const FeatureBitset &RHS) {
    for (unsigned I = 0, E = Bits.size(); I != E; ++I) {
      Bits[I] |= RHS.Bits[I];
    }
    return *this;
  }
  constexpr FeatureBitset operator|(const FeatureBitset &RHS) const {
    FeatureBitset Result = *this;
    Result |= RHS;
    return Result;
  }

  constexpr FeatureBitset operator~() const {
    FeatureBitset Result = *this;
    for (auto &B : Result.Bits)
      B = ~B;
    return Result;
  }

  bool operator==(const FeatureBitset &RHS) const {
    return std::equal(std::begin(Bits), std::end(Bits), std::begin(RHS.Bits));
  }

  bool operator!=(const FeatureBitset &RHS) const { return !(*this == RHS); }

  bool operator < (const FeatureBitset &Other) const {
    for (unsigned I = 0, E = size(); I != E; ++I) {
      bool LHS = test(I), RHS = Other.test(I);
      if (LHS != RHS)
        return LHS < RHS;
    }
    return false;
  }
};

/// Class used to store the subtarget bits in the tables created by tablegen.
class FeatureBitArray : public FeatureBitset {
public:
  constexpr FeatureBitArray(const std::array<uint64_t, MAX_SUBTARGET_WORDS> &B)
      : FeatureBitset(B) {}

  const FeatureBitset &getAsBitset() const { return *this; }
};

//===----------------------------------------------------------------------===//

/// Manages the enabling and disabling of subtarget specific features.
///
/// Features are encoded as a string of the form
///   "+attr1,+attr2,-attr3,...,+attrN"
/// A comma separates each feature from the next (all lowercase.)
/// Each of the remaining features is prefixed with + or - indicating whether
/// that feature should be enabled or disabled contrary to the cpu
/// specification.
class SubtargetFeatures {
  std::vector<std::string> Features;    ///< Subtarget features as a vector

public:
  explicit SubtargetFeatures(StringRef Initial = "");

  /// Returns features as a string.
  std::string getString() const;

  /// Adds Features.
  void AddFeature(StringRef String, bool Enable = true);

  void addFeaturesVector(const ArrayRef<std::string> OtherFeatures);

  /// Returns the vector of individual subtarget features.
  const std::vector<std::string> &getFeatures() const { return Features; }

  /// Prints feature string.
  void print(raw_ostream &OS) const;

  // Dumps feature info.
  void dump() const;

  /// Adds the default features for the specified target triple.
  void getDefaultSubtargetFeatures(const Triple& Triple);

  /// Determine if a feature has a flag; '+' or '-'
  static bool hasFlag(StringRef Feature) {
    assert(!Feature.empty() && "Empty string");
    // Get first character
    char Ch = Feature[0];
    // Check if first character is '+' or '-' flag
    return Ch == '+' || Ch =='-';
  }

  /// Return string stripped of flag.
  static StringRef StripFlag(StringRef Feature) {
    return hasFlag(Feature) ? Feature.substr(1) : Feature;
  }

  /// Return true if enable flag; '+'.
  static inline bool isEnabled(StringRef Feature) {
    assert(!Feature.empty() && "Empty string");
    // Get first character
    char Ch = Feature[0];
    // Check if first character is '+' for enabled
    return Ch == '+';
  }

  /// Splits a string of comma separated items in to a vector of strings.
  static void Split(std::vector<std::string> &V, StringRef S);
};

} // end namespace llvm

#endif // LLVM_TARGETPARSER_SUBTARGETFEATURE_H
