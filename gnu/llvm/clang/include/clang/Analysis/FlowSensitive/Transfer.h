//===-- Transfer.h ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines a transfer function that evaluates a program statement and
//  updates an environment accordingly.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_ANALYSIS_FLOWSENSITIVE_TRANSFER_H
#define LLVM_CLANG_ANALYSIS_FLOWSENSITIVE_TRANSFER_H

#include "clang/AST/Stmt.h"
#include "clang/Analysis/FlowSensitive/DataflowAnalysisContext.h"
#include "clang/Analysis/FlowSensitive/DataflowEnvironment.h"
#include "clang/Analysis/FlowSensitive/TypeErasedDataflowAnalysis.h"

namespace clang {
namespace dataflow {

/// Maps statements to the environments of basic blocks that contain them.
class StmtToEnvMap {
public:
  // `CurBlockID` is the ID of the block currently being processed, and
  // `CurState` is the pending state currently associated with this block. These
  // are supplied separately as the pending state for the current block may not
  // yet be represented in `BlockToState`.
  StmtToEnvMap(const AdornedCFG &ACFG,
               llvm::ArrayRef<std::optional<TypeErasedDataflowAnalysisState>>
                   BlockToState,
               unsigned CurBlockID,
               const TypeErasedDataflowAnalysisState &CurState)
      : ACFG(ACFG), BlockToState(BlockToState), CurBlockID(CurBlockID),
        CurState(CurState) {}

  /// Returns the environment of the basic block that contains `S`.
  /// The result is guaranteed never to be null.
  const Environment *getEnvironment(const Stmt &S) const;

private:
  const AdornedCFG &ACFG;
  llvm::ArrayRef<std::optional<TypeErasedDataflowAnalysisState>> BlockToState;
  unsigned CurBlockID;
  const TypeErasedDataflowAnalysisState &CurState;
};

/// Evaluates `S` and updates `Env` accordingly.
///
/// Requirements:
///
///  `S` must not be `ParenExpr` or `ExprWithCleanups`.
void transfer(const StmtToEnvMap &StmtToEnv, const Stmt &S, Environment &Env,
              Environment::ValueModel &Model);

} // namespace dataflow
} // namespace clang

#endif // LLVM_CLANG_ANALYSIS_FLOWSENSITIVE_TRANSFER_H
