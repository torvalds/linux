//===-- NoopLattice.h -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines the lattice with exactly one element.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_ANALYSIS_FLOWSENSITIVE_NOOP_LATTICE_H
#define LLVM_CLANG_ANALYSIS_FLOWSENSITIVE_NOOP_LATTICE_H

#include "clang/Analysis/FlowSensitive/DataflowLattice.h"
#include <ostream>

namespace clang {
namespace dataflow {

/// Trivial lattice for dataflow analysis with exactly one element.
///
/// Useful for analyses that only need the Environment and nothing more.
class NoopLattice {
public:
  bool operator==(const NoopLattice &Other) const { return true; }

  LatticeJoinEffect join(const NoopLattice &Other) {
    return LatticeJoinEffect::Unchanged;
  }
};

inline std::ostream &operator<<(std::ostream &OS, const NoopLattice &) {
  return OS << "noop";
}

} // namespace dataflow
} // namespace clang

#endif // LLVM_CLANG_ANALYSIS_FLOWSENSITIVE_NOOP_LATTICE_H
