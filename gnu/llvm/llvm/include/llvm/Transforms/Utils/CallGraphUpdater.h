//===- CallGraphUpdater.h - A (lazy) call graph update helper ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file provides interfaces used to manipulate a call graph, regardless
/// if it is a "old style" CallGraph or an "new style" LazyCallGraph.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_CALLGRAPHUPDATER_H
#define LLVM_TRANSFORMS_UTILS_CALLGRAPHUPDATER_H

#include "llvm/Analysis/CGSCCPassManager.h"
#include "llvm/Analysis/LazyCallGraph.h"

namespace llvm {

class CallGraph;
class CallGraphSCC;

/// Wrapper to unify "old style" CallGraph and "new style" LazyCallGraph. This
/// simplifies the interface and the call sites, e.g., new and old pass manager
/// passes can share the same code.
class CallGraphUpdater {
  /// Containers for functions which we did replace or want to delete when
  /// `finalize` is called. This can happen explicitly or as part of the
  /// destructor. Dead functions in comdat sections are tracked separately
  /// because a function with discardable linakage in a COMDAT should only
  /// be dropped if the entire COMDAT is dropped, see git ac07703842cf.
  ///{
  SmallPtrSet<Function *, 16> ReplacedFunctions;
  SmallVector<Function *, 16> DeadFunctions;
  SmallVector<Function *, 16> DeadFunctionsInComdats;
  ///}

  /// New PM variables
  ///{
  LazyCallGraph *LCG = nullptr;
  LazyCallGraph::SCC *SCC = nullptr;
  CGSCCAnalysisManager *AM = nullptr;
  CGSCCUpdateResult *UR = nullptr;
  FunctionAnalysisManager *FAM = nullptr;
  ///}

public:
  CallGraphUpdater() = default;
  ~CallGraphUpdater() { finalize(); }

  /// Initializers for usage outside of a CGSCC pass, inside a CGSCC pass in
  /// the old and new pass manager (PM).
  ///{
  void initialize(LazyCallGraph &LCG, LazyCallGraph::SCC &SCC,
                  CGSCCAnalysisManager &AM, CGSCCUpdateResult &UR) {
    this->LCG = &LCG;
    this->SCC = &SCC;
    this->AM = &AM;
    this->UR = &UR;
    FAM =
        &AM.getResult<FunctionAnalysisManagerCGSCCProxy>(SCC, LCG).getManager();
  }
  ///}

  /// Finalizer that will trigger actions like function removal from the CG.
  bool finalize();

  /// Remove \p Fn from the call graph.
  void removeFunction(Function &Fn);

  /// After an CGSCC pass changes a function in ways that affect the call
  /// graph, this method can be called to update it.
  void reanalyzeFunction(Function &Fn);

  /// If a new function was created by outlining, this method can be called
  /// to update the call graph for the new function. Note that the old one
  /// still needs to be re-analyzed or manually updated.
  void registerOutlinedFunction(Function &OriginalFn, Function &NewFn);

  /// Replace \p OldFn in the call graph (and SCC) with \p NewFn. The uses
  /// outside the call graph and the function \p OldFn are not modified.
  /// Note that \p OldFn is also removed from the call graph
  /// (\see removeFunction).
  void replaceFunctionWith(Function &OldFn, Function &NewFn);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_CALLGRAPHUPDATER_H
