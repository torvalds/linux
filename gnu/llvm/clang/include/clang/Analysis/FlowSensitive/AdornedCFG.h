//===-- AdornedCFG.h ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines an AdornedCFG class that is used by dataflow analyses that
//  run over Control-Flow Graphs (CFGs).
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_ANALYSIS_FLOWSENSITIVE_ADORNEDCFG_H
#define LLVM_CLANG_ANALYSIS_FLOWSENSITIVE_ADORNEDCFG_H

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Stmt.h"
#include "clang/Analysis/CFG.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/Support/Error.h"
#include <memory>
#include <utility>

namespace clang {
namespace dataflow {

/// Holds CFG with additional information derived from it that is needed to
/// perform dataflow analysis.
class AdornedCFG {
public:
  /// Builds an `AdornedCFG` from a `FunctionDecl`.
  /// `Func.doesThisDeclarationHaveABody()` must be true, and
  /// `Func.isTemplated()` must be false.
  static llvm::Expected<AdornedCFG> build(const FunctionDecl &Func);

  /// Builds an `AdornedCFG` from an AST node. `D` is the function in which
  /// `S` resides. `D.isTemplated()` must be false.
  static llvm::Expected<AdornedCFG> build(const Decl &D, Stmt &S,
                                          ASTContext &C);

  /// Returns the `Decl` containing the statement used to construct the CFG, if
  /// available.
  const Decl &getDecl() const { return ContainingDecl; }

  /// Returns the CFG that is stored in this context.
  const CFG &getCFG() const { return *Cfg; }

  /// Returns a mapping from statements to basic blocks that contain them.
  const llvm::DenseMap<const Stmt *, const CFGBlock *> &getStmtToBlock() const {
    return StmtToBlock;
  }

  /// Returns whether `B` is reachable from the entry block.
  bool isBlockReachable(const CFGBlock &B) const {
    return BlockReachable[B.getBlockID()];
  }

  /// Returns whether `B` contains an expression that is consumed in a
  /// different block than `B` (i.e. the parent of the expression is in a
  /// different block).
  /// This happens if there is control flow within a full-expression (triggered
  /// by `&&`, `||`, or the conditional operator). Note that the operands of
  /// these operators are not the only expressions that can be consumed in a
  /// different block. For example, in the function call
  /// `f(&i, cond() ? 1 : 0)`, `&i` is in a different block than the `CallExpr`.
  bool containsExprConsumedInDifferentBlock(const CFGBlock &B) const {
    return ContainsExprConsumedInDifferentBlock.contains(&B);
  }

private:
  AdornedCFG(
      const Decl &D, std::unique_ptr<CFG> Cfg,
      llvm::DenseMap<const Stmt *, const CFGBlock *> StmtToBlock,
      llvm::BitVector BlockReachable,
      llvm::DenseSet<const CFGBlock *> ContainsExprConsumedInDifferentBlock)
      : ContainingDecl(D), Cfg(std::move(Cfg)),
        StmtToBlock(std::move(StmtToBlock)),
        BlockReachable(std::move(BlockReachable)),
        ContainsExprConsumedInDifferentBlock(
            std::move(ContainsExprConsumedInDifferentBlock)) {}

  /// The `Decl` containing the statement used to construct the CFG.
  const Decl &ContainingDecl;
  std::unique_ptr<CFG> Cfg;
  llvm::DenseMap<const Stmt *, const CFGBlock *> StmtToBlock;
  llvm::BitVector BlockReachable;
  llvm::DenseSet<const CFGBlock *> ContainsExprConsumedInDifferentBlock;
};

} // namespace dataflow
} // namespace clang

#endif // LLVM_CLANG_ANALYSIS_FLOWSENSITIVE_ADORNEDCFG_H
