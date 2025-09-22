//===- SMLoc.h - Source location for use with diagnostics -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the SMLoc class.  This class encapsulates a location in
// source code for use in diagnostics.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_SMLOC_H
#define LLVM_SUPPORT_SMLOC_H

#include <cassert>
#include <optional>

namespace llvm {

/// Represents a location in source code.
class SMLoc {
  const char *Ptr = nullptr;

public:
  constexpr SMLoc() = default;

  constexpr bool isValid() const { return Ptr != nullptr; }

  constexpr bool operator==(const SMLoc &RHS) const { return RHS.Ptr == Ptr; }
  constexpr bool operator!=(const SMLoc &RHS) const { return RHS.Ptr != Ptr; }

  constexpr const char *getPointer() const { return Ptr; }

  static SMLoc getFromPointer(const char *Ptr) {
    SMLoc L;
    L.Ptr = Ptr;
    return L;
  }
};

/// Represents a range in source code.
///
/// SMRange is implemented using a half-open range, as is the convention in C++.
/// In the string "abc", the range [1,3) represents the substring "bc", and the
/// range [2,2) represents an empty range between the characters "b" and "c".
class SMRange {
public:
  SMLoc Start, End;

  SMRange() = default;
  SMRange(std::nullopt_t) {}
  SMRange(SMLoc St, SMLoc En) : Start(St), End(En) {
    assert(Start.isValid() == End.isValid() &&
           "Start and End should either both be valid or both be invalid!");
  }

  bool isValid() const { return Start.isValid(); }
};

} // end namespace llvm

#endif // LLVM_SUPPORT_SMLOC_H
