//=- AnalysisBasedWarnings.h - Sema warnings based on libAnalysis -*- C++ -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines AnalysisBasedWarnings, a worker object used by Sema
// that issues warnings based on dataflow-analysis.
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_SEMA_ANALYSISBASEDWARNINGS_H
#define LLVM_CLANG_SEMA_ANALYSISBASEDWARNINGS_H

#include "clang/AST/Decl.h"
#include "llvm/ADT/DenseMap.h"
#include <memory>

namespace clang {

class Decl;
class FunctionDecl;
class QualType;
class Sema;
namespace sema {
  class FunctionScopeInfo;
}

namespace sema {

class AnalysisBasedWarnings {
public:
  class Policy {
    friend class AnalysisBasedWarnings;
    // The warnings to run.
    LLVM_PREFERRED_TYPE(bool)
    unsigned enableCheckFallThrough : 1;
    LLVM_PREFERRED_TYPE(bool)
    unsigned enableCheckUnreachable : 1;
    LLVM_PREFERRED_TYPE(bool)
    unsigned enableThreadSafetyAnalysis : 1;
    LLVM_PREFERRED_TYPE(bool)
    unsigned enableConsumedAnalysis : 1;
  public:
    Policy();
    void disableCheckFallThrough() { enableCheckFallThrough = 0; }
  };

private:
  Sema &S;
  Policy DefaultPolicy;

  class InterProceduralData;
  std::unique_ptr<InterProceduralData> IPData;

  enum VisitFlag { NotVisited = 0, Visited = 1, Pending = 2 };
  llvm::DenseMap<const FunctionDecl*, VisitFlag> VisitedFD;

  /// \name Statistics
  /// @{

  /// Number of function CFGs built and analyzed.
  unsigned NumFunctionsAnalyzed;

  /// Number of functions for which the CFG could not be successfully
  /// built.
  unsigned NumFunctionsWithBadCFGs;

  /// Total number of blocks across all CFGs.
  unsigned NumCFGBlocks;

  /// Largest number of CFG blocks for a single function analyzed.
  unsigned MaxCFGBlocksPerFunction;

  /// Total number of CFGs with variables analyzed for uninitialized
  /// uses.
  unsigned NumUninitAnalysisFunctions;

  /// Total number of variables analyzed for uninitialized uses.
  unsigned NumUninitAnalysisVariables;

  /// Max number of variables analyzed for uninitialized uses in a single
  /// function.
  unsigned MaxUninitAnalysisVariablesPerFunction;

  /// Total number of block visits during uninitialized use analysis.
  unsigned NumUninitAnalysisBlockVisits;

  /// Max number of block visits during uninitialized use analysis of
  /// a single function.
  unsigned MaxUninitAnalysisBlockVisitsPerFunction;

  /// @}

public:
  AnalysisBasedWarnings(Sema &s);
  ~AnalysisBasedWarnings();

  void IssueWarnings(Policy P, FunctionScopeInfo *fscope,
                     const Decl *D, QualType BlockType);

  // Issue warnings that require whole-translation-unit analysis.
  void IssueWarnings(TranslationUnitDecl *D);

  Policy getDefaultPolicy() { return DefaultPolicy; }

  void PrintStats() const;
};

} // namespace sema
} // namespace clang

#endif
