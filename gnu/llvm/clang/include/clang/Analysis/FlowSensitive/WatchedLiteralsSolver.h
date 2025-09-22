//===- WatchedLiteralsSolver.h ----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines a SAT solver implementation that can be used by dataflow
//  analyses.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_ANALYSIS_FLOWSENSITIVE_WATCHEDLITERALSSOLVER_H
#define LLVM_CLANG_ANALYSIS_FLOWSENSITIVE_WATCHEDLITERALSSOLVER_H

#include "clang/Analysis/FlowSensitive/Formula.h"
#include "clang/Analysis/FlowSensitive/Solver.h"
#include "llvm/ADT/ArrayRef.h"

namespace clang {
namespace dataflow {

/// A SAT solver that is an implementation of Algorithm D from Knuth's The Art
/// of Computer Programming Volume 4: Satisfiability, Fascicle 6. It is based on
/// the Davis-Putnam-Logemann-Loveland (DPLL) algorithm [1], keeps references to
/// a single "watched" literal per clause, and uses a set of "active" variables
/// for unit propagation.
//
// [1] https://en.wikipedia.org/wiki/DPLL_algorithm
class WatchedLiteralsSolver : public Solver {
  // Count of the iterations of the main loop of the solver. This spans *all*
  // calls to the underlying solver across the life of this object. It is
  // reduced with every (non-trivial) call to the solver.
  //
  // We give control over the abstract count of iterations instead of concrete
  // measurements like CPU cycles or time to ensure deterministic results.
  std::int64_t MaxIterations = std::numeric_limits<std::int64_t>::max();

public:
  WatchedLiteralsSolver() = default;

  // `Work` specifies a computational limit on the solver. Units of "work"
  // roughly correspond to attempts to assign a value to a single
  // variable. Since the algorithm is exponential in the number of variables,
  // this is the most direct (abstract) unit to target.
  explicit WatchedLiteralsSolver(std::int64_t WorkLimit)
      : MaxIterations(WorkLimit) {}

  Result solve(llvm::ArrayRef<const Formula *> Vals) override;

  bool reachedLimit() const override { return MaxIterations == 0; }
};

} // namespace dataflow
} // namespace clang

#endif // LLVM_CLANG_ANALYSIS_FLOWSENSITIVE_WATCHEDLITERALSSOLVER_H
