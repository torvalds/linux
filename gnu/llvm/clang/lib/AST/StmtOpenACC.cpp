//===--- StmtOpenACC.cpp - Classes for OpenACC Constructs -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the subclasses of Stmt class declared in StmtOpenACC.h
//
//===----------------------------------------------------------------------===//

#include "clang/AST/StmtOpenACC.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/StmtCXX.h"
using namespace clang;

OpenACCComputeConstruct *
OpenACCComputeConstruct::CreateEmpty(const ASTContext &C, unsigned NumClauses) {
  void *Mem = C.Allocate(
      OpenACCComputeConstruct::totalSizeToAlloc<const OpenACCClause *>(
          NumClauses));
  auto *Inst = new (Mem) OpenACCComputeConstruct(NumClauses);
  return Inst;
}

OpenACCComputeConstruct *OpenACCComputeConstruct::Create(
    const ASTContext &C, OpenACCDirectiveKind K, SourceLocation BeginLoc,
    SourceLocation DirLoc, SourceLocation EndLoc,
    ArrayRef<const OpenACCClause *> Clauses, Stmt *StructuredBlock,
    ArrayRef<OpenACCLoopConstruct *> AssociatedLoopConstructs) {
  void *Mem = C.Allocate(
      OpenACCComputeConstruct::totalSizeToAlloc<const OpenACCClause *>(
          Clauses.size()));
  auto *Inst = new (Mem) OpenACCComputeConstruct(K, BeginLoc, DirLoc, EndLoc,
                                                 Clauses, StructuredBlock);

  llvm::for_each(AssociatedLoopConstructs, [&](OpenACCLoopConstruct *C) {
    C->setParentComputeConstruct(Inst);
  });

  return Inst;
}

void OpenACCComputeConstruct::findAndSetChildLoops() {
  struct LoopConstructFinder : RecursiveASTVisitor<LoopConstructFinder> {
    OpenACCComputeConstruct *Construct = nullptr;

    LoopConstructFinder(OpenACCComputeConstruct *Construct)
        : Construct(Construct) {}

    bool TraverseOpenACCComputeConstruct(OpenACCComputeConstruct *C) {
      // Stop searching if we find a compute construct.
      return true;
    }
    bool TraverseOpenACCLoopConstruct(OpenACCLoopConstruct *C) {
      // Stop searching if we find a loop construct, after taking ownership of
      // it.
      C->setParentComputeConstruct(Construct);
      return true;
    }
  };

  LoopConstructFinder f(this);
  f.TraverseStmt(getAssociatedStmt());
}

OpenACCLoopConstruct::OpenACCLoopConstruct(unsigned NumClauses)
    : OpenACCAssociatedStmtConstruct(
          OpenACCLoopConstructClass, OpenACCDirectiveKind::Loop,
          SourceLocation{}, SourceLocation{}, SourceLocation{},
          /*AssociatedStmt=*/nullptr) {
  std::uninitialized_value_construct(
      getTrailingObjects<const OpenACCClause *>(),
      getTrailingObjects<const OpenACCClause *>() + NumClauses);
  setClauseList(
      MutableArrayRef(getTrailingObjects<const OpenACCClause *>(), NumClauses));
}

OpenACCLoopConstruct::OpenACCLoopConstruct(
    SourceLocation Start, SourceLocation DirLoc, SourceLocation End,
    ArrayRef<const OpenACCClause *> Clauses, Stmt *Loop)
    : OpenACCAssociatedStmtConstruct(OpenACCLoopConstructClass,
                                     OpenACCDirectiveKind::Loop, Start, DirLoc,
                                     End, Loop) {
  // accept 'nullptr' for the loop. This is diagnosed somewhere, but this gives
  // us some level of AST fidelity in the error case.
  assert((Loop == nullptr || isa<ForStmt, CXXForRangeStmt>(Loop)) &&
         "Associated Loop not a for loop?");
  // Initialize the trailing storage.
  std::uninitialized_copy(Clauses.begin(), Clauses.end(),
                          getTrailingObjects<const OpenACCClause *>());

  setClauseList(MutableArrayRef(getTrailingObjects<const OpenACCClause *>(),
                                Clauses.size()));
}

void OpenACCLoopConstruct::setLoop(Stmt *Loop) {
  assert((isa<ForStmt, CXXForRangeStmt>(Loop)) &&
         "Associated Loop not a for loop?");
  setAssociatedStmt(Loop);
}

OpenACCLoopConstruct *OpenACCLoopConstruct::CreateEmpty(const ASTContext &C,
                                                        unsigned NumClauses) {
  void *Mem =
      C.Allocate(OpenACCLoopConstruct::totalSizeToAlloc<const OpenACCClause *>(
          NumClauses));
  auto *Inst = new (Mem) OpenACCLoopConstruct(NumClauses);
  return Inst;
}

OpenACCLoopConstruct *
OpenACCLoopConstruct::Create(const ASTContext &C, SourceLocation BeginLoc,
                             SourceLocation DirLoc, SourceLocation EndLoc,
                             ArrayRef<const OpenACCClause *> Clauses,
                             Stmt *Loop) {
  void *Mem =
      C.Allocate(OpenACCLoopConstruct::totalSizeToAlloc<const OpenACCClause *>(
          Clauses.size()));
  auto *Inst =
      new (Mem) OpenACCLoopConstruct(BeginLoc, DirLoc, EndLoc, Clauses, Loop);
  return Inst;
}
