//===- AdornedCFG.cpp ---------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines an `AdornedCFG` class that is used by dataflow analyses
//  that run over Control-Flow Graphs (CFGs).
//
//===----------------------------------------------------------------------===//

#include "clang/Analysis/FlowSensitive/AdornedCFG.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Stmt.h"
#include "clang/Analysis/CFG.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/Support/Error.h"
#include <utility>

namespace clang {
namespace dataflow {

/// Returns a map from statements to basic blocks that contain them.
static llvm::DenseMap<const Stmt *, const CFGBlock *>
buildStmtToBasicBlockMap(const CFG &Cfg) {
  llvm::DenseMap<const Stmt *, const CFGBlock *> StmtToBlock;
  for (const CFGBlock *Block : Cfg) {
    if (Block == nullptr)
      continue;

    for (const CFGElement &Element : *Block) {
      auto Stmt = Element.getAs<CFGStmt>();
      if (!Stmt)
        continue;

      StmtToBlock[Stmt->getStmt()] = Block;
    }
  }
  // Some terminator conditions don't appear as a `CFGElement` anywhere else -
  // for example, this is true if the terminator condition is a `&&` or `||`
  // operator.
  // We associate these conditions with the block the terminator appears in,
  // but only if the condition has not already appeared as a regular
  // `CFGElement`. (The `insert()` below does nothing if the key already exists
  // in the map.)
  for (const CFGBlock *Block : Cfg) {
    if (Block != nullptr)
      if (const Stmt *TerminatorCond = Block->getTerminatorCondition())
        StmtToBlock.insert({TerminatorCond, Block});
  }
  // Terminator statements typically don't appear as a `CFGElement` anywhere
  // else, so we want to associate them with the block that they terminate.
  // However, there are some important special cases:
  // -  The conditional operator is a type of terminator, but it also appears
  //    as a regular `CFGElement`, and we want to associate it with the block
  //    in which it appears as a `CFGElement`.
  // -  The `&&` and `||` operators are types of terminators, but like the
  //    conditional operator, they can appear as a regular `CFGElement` or
  //    as a terminator condition (see above).
  // We process terminators last to make sure that we only associate them with
  // the block they terminate if they haven't previously occurred as a regular
  // `CFGElement` or as a terminator condition.
  for (const CFGBlock *Block : Cfg) {
    if (Block != nullptr)
      if (const Stmt *TerminatorStmt = Block->getTerminatorStmt())
        StmtToBlock.insert({TerminatorStmt, Block});
  }
  return StmtToBlock;
}

static llvm::BitVector findReachableBlocks(const CFG &Cfg) {
  llvm::BitVector BlockReachable(Cfg.getNumBlockIDs(), false);

  llvm::SmallVector<const CFGBlock *> BlocksToVisit;
  BlocksToVisit.push_back(&Cfg.getEntry());
  while (!BlocksToVisit.empty()) {
    const CFGBlock *Block = BlocksToVisit.back();
    BlocksToVisit.pop_back();

    if (BlockReachable[Block->getBlockID()])
      continue;

    BlockReachable[Block->getBlockID()] = true;

    for (const CFGBlock *Succ : Block->succs())
      if (Succ)
        BlocksToVisit.push_back(Succ);
  }

  return BlockReachable;
}

static llvm::DenseSet<const CFGBlock *>
buildContainsExprConsumedInDifferentBlock(
    const CFG &Cfg,
    const llvm::DenseMap<const Stmt *, const CFGBlock *> &StmtToBlock) {
  llvm::DenseSet<const CFGBlock *> Result;

  auto CheckChildExprs = [&Result, &StmtToBlock](const Stmt *S,
                                                 const CFGBlock *Block) {
    for (const Stmt *Child : S->children()) {
      if (!isa_and_nonnull<Expr>(Child))
        continue;
      const CFGBlock *ChildBlock = StmtToBlock.lookup(Child);
      if (ChildBlock != Block)
        Result.insert(ChildBlock);
    }
  };

  for (const CFGBlock *Block : Cfg) {
    if (Block == nullptr)
      continue;

    for (const CFGElement &Element : *Block)
      if (auto S = Element.getAs<CFGStmt>())
        CheckChildExprs(S->getStmt(), Block);

    if (const Stmt *TerminatorCond = Block->getTerminatorCondition())
      CheckChildExprs(TerminatorCond, Block);
  }

  return Result;
}

llvm::Expected<AdornedCFG> AdornedCFG::build(const FunctionDecl &Func) {
  if (!Func.doesThisDeclarationHaveABody())
    return llvm::createStringError(
        std::make_error_code(std::errc::invalid_argument),
        "Cannot analyze function without a body");

  return build(Func, *Func.getBody(), Func.getASTContext());
}

llvm::Expected<AdornedCFG> AdornedCFG::build(const Decl &D, Stmt &S,
                                             ASTContext &C) {
  if (D.isTemplated())
    return llvm::createStringError(
        std::make_error_code(std::errc::invalid_argument),
        "Cannot analyze templated declarations");

  // The shape of certain elements of the AST can vary depending on the
  // language. We currently only support C++.
  if (!C.getLangOpts().CPlusPlus || C.getLangOpts().ObjC)
    return llvm::createStringError(
        std::make_error_code(std::errc::invalid_argument),
        "Can only analyze C++");

  CFG::BuildOptions Options;
  Options.PruneTriviallyFalseEdges = true;
  Options.AddImplicitDtors = true;
  Options.AddTemporaryDtors = true;
  Options.AddInitializers = true;
  Options.AddCXXDefaultInitExprInCtors = true;
  Options.AddLifetime = true;

  // Ensure that all sub-expressions in basic blocks are evaluated.
  Options.setAllAlwaysAdd();

  auto Cfg = CFG::buildCFG(&D, &S, &C, Options);
  if (Cfg == nullptr)
    return llvm::createStringError(
        std::make_error_code(std::errc::invalid_argument),
        "CFG::buildCFG failed");

  llvm::DenseMap<const Stmt *, const CFGBlock *> StmtToBlock =
      buildStmtToBasicBlockMap(*Cfg);

  llvm::BitVector BlockReachable = findReachableBlocks(*Cfg);

  llvm::DenseSet<const CFGBlock *> ContainsExprConsumedInDifferentBlock =
      buildContainsExprConsumedInDifferentBlock(*Cfg, StmtToBlock);

  return AdornedCFG(D, std::move(Cfg), std::move(StmtToBlock),
                    std::move(BlockReachable),
                    std::move(ContainsExprConsumedInDifferentBlock));
}

} // namespace dataflow
} // namespace clang
