//===- DataflowLattice.h ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines base types for building lattices to be used in dataflow
//  analyses that run over Control-Flow Graphs (CFGs).
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_ANALYSIS_FLOWSENSITIVE_DATAFLOWLATTICE_H
#define LLVM_CLANG_ANALYSIS_FLOWSENSITIVE_DATAFLOWLATTICE_H

namespace clang {
namespace dataflow {

/// Effect indicating whether a lattice operation resulted in a new value.
enum class LatticeEffect {
  Unchanged,
  Changed,
};
// DEPRECATED. Use `LatticeEffect`.
using LatticeJoinEffect = LatticeEffect;

} // namespace dataflow
} // namespace clang

#endif // LLVM_CLANG_ANALYSIS_FLOWSENSITIVE_DATAFLOWLATTICE_H
