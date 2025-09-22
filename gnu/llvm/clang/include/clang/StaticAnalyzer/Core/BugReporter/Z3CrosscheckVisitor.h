//===- Z3CrosscheckVisitor.h - Crosscheck reports with Z3 -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines the visitor and utilities around it for Z3 report
//  refutation.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_STATICANALYZER_CORE_BUGREPORTER_Z3CROSSCHECKVISITOR_H
#define LLVM_CLANG_STATICANALYZER_CORE_BUGREPORTER_Z3CROSSCHECKVISITOR_H

#include "clang/StaticAnalyzer/Core/BugReporter/BugReporterVisitors.h"

namespace clang::ento {

/// The bug visitor will walk all the nodes in a path and collect all the
/// constraints. When it reaches the root node, will create a refutation
/// manager and check if the constraints are satisfiable.
class Z3CrosscheckVisitor final : public BugReporterVisitor {
public:
  struct Z3Result {
    std::optional<bool> IsSAT = std::nullopt;
    unsigned Z3QueryTimeMilliseconds = 0;
    unsigned UsedRLimit = 0;
  };
  Z3CrosscheckVisitor(Z3CrosscheckVisitor::Z3Result &Result,
                      const AnalyzerOptions &Opts);

  void Profile(llvm::FoldingSetNodeID &ID) const override;

  PathDiagnosticPieceRef VisitNode(const ExplodedNode *N,
                                   BugReporterContext &BRC,
                                   PathSensitiveBugReport &BR) override;

  void finalizeVisitor(BugReporterContext &BRC, const ExplodedNode *EndPathNode,
                       PathSensitiveBugReport &BR) override;

private:
  void addConstraints(const ExplodedNode *N,
                      bool OverwriteConstraintsOnExistingSyms);

  /// Holds the constraints in a given path.
  ConstraintMap Constraints;
  Z3Result &Result;
  const AnalyzerOptions &Opts;
};

/// The oracle will decide if a report should be accepted or rejected based on
/// the results of the Z3 solver and the statistics of the queries of a report
/// equivalenece class.
class Z3CrosscheckOracle {
public:
  explicit Z3CrosscheckOracle(const AnalyzerOptions &Opts) : Opts(Opts) {}

  enum Z3Decision {
    AcceptReport,  // The report was SAT.
    RejectReport,  // The report was UNSAT or UNDEF.
    RejectEQClass, // The heuristic suggests to skip the current eqclass.
  };

  /// Updates the internal state with the new Z3Result and makes a decision how
  /// to proceed:
  /// - Accept the report if the Z3Result was SAT.
  /// - Suggest dropping the report equvalence class based on the accumulated
  ///   statistics.
  /// - Otherwise, reject the report if the Z3Result was UNSAT or UNDEF.
  ///
  /// Conditions for dropping the equivalence class:
  /// - Accumulative time spent in Z3 checks is more than 700ms in the eqclass.
  /// - Hit the 300ms query timeout in the report eqclass.
  /// - Hit the 400'000 rlimit in the report eqclass.
  ///
  /// All these thresholds are configurable via the analyzer options.
  ///
  /// Refer to
  /// https://discourse.llvm.org/t/analyzer-rfc-taming-z3-query-times/79520 to
  /// see why this heuristic was chosen.
  Z3Decision interpretQueryResult(const Z3CrosscheckVisitor::Z3Result &Meta);

private:
  const AnalyzerOptions &Opts;
  unsigned AccumulatedZ3QueryTimeInEqClass = 0; // ms
};

} // namespace clang::ento

#endif // LLVM_CLANG_STATICANALYZER_CORE_BUGREPORTER_Z3CROSSCHECKVISITOR_H
