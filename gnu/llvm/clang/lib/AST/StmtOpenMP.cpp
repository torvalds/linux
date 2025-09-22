//===--- StmtOpenMP.cpp - Classes for OpenMP directives -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the subclasses of Stmt class declared in StmtOpenMP.h
//
//===----------------------------------------------------------------------===//

#include "clang/AST/ASTContext.h"
#include "clang/AST/StmtOpenMP.h"

using namespace clang;
using namespace llvm::omp;

size_t OMPChildren::size(unsigned NumClauses, bool HasAssociatedStmt,
                         unsigned NumChildren) {
  return llvm::alignTo(
      totalSizeToAlloc<OMPClause *, Stmt *>(
          NumClauses, NumChildren + (HasAssociatedStmt ? 1 : 0)),
      alignof(OMPChildren));
}

void OMPChildren::setClauses(ArrayRef<OMPClause *> Clauses) {
  assert(Clauses.size() == NumClauses &&
         "Number of clauses is not the same as the preallocated buffer");
  llvm::copy(Clauses, getTrailingObjects<OMPClause *>());
}

MutableArrayRef<Stmt *> OMPChildren::getChildren() {
  return llvm::MutableArrayRef(getTrailingObjects<Stmt *>(), NumChildren);
}

OMPChildren *OMPChildren::Create(void *Mem, ArrayRef<OMPClause *> Clauses) {
  auto *Data = CreateEmpty(Mem, Clauses.size());
  Data->setClauses(Clauses);
  return Data;
}

OMPChildren *OMPChildren::Create(void *Mem, ArrayRef<OMPClause *> Clauses,
                                 Stmt *S, unsigned NumChildren) {
  auto *Data = CreateEmpty(Mem, Clauses.size(), S, NumChildren);
  Data->setClauses(Clauses);
  if (S)
    Data->setAssociatedStmt(S);
  return Data;
}

OMPChildren *OMPChildren::CreateEmpty(void *Mem, unsigned NumClauses,
                                      bool HasAssociatedStmt,
                                      unsigned NumChildren) {
  return new (Mem) OMPChildren(NumClauses, NumChildren, HasAssociatedStmt);
}

bool OMPExecutableDirective::isStandaloneDirective() const {
  // Special case: 'omp target enter data', 'omp target exit data',
  // 'omp target update' are stand-alone directives, but for implementation
  // reasons they have empty synthetic structured block, to simplify codegen.
  if (isa<OMPTargetEnterDataDirective>(this) ||
      isa<OMPTargetExitDataDirective>(this) ||
      isa<OMPTargetUpdateDirective>(this))
    return true;
  return !hasAssociatedStmt();
}

Stmt *OMPExecutableDirective::getStructuredBlock() {
  assert(!isStandaloneDirective() &&
         "Standalone Executable Directives don't have Structured Blocks.");
  if (auto *LD = dyn_cast<OMPLoopDirective>(this))
    return LD->getBody();
  return getRawStmt();
}

Stmt *
OMPLoopBasedDirective::tryToFindNextInnerLoop(Stmt *CurStmt,
                                              bool TryImperfectlyNestedLoops) {
  Stmt *OrigStmt = CurStmt;
  CurStmt = CurStmt->IgnoreContainers();
  // Additional work for imperfectly nested loops, introduced in OpenMP 5.0.
  if (TryImperfectlyNestedLoops) {
    if (auto *CS = dyn_cast<CompoundStmt>(CurStmt)) {
      CurStmt = nullptr;
      SmallVector<CompoundStmt *, 4> Statements(1, CS);
      SmallVector<CompoundStmt *, 4> NextStatements;
      while (!Statements.empty()) {
        CS = Statements.pop_back_val();
        if (!CS)
          continue;
        for (Stmt *S : CS->body()) {
          if (!S)
            continue;
          if (auto *CanonLoop = dyn_cast<OMPCanonicalLoop>(S))
            S = CanonLoop->getLoopStmt();
          if (isa<ForStmt>(S) || isa<CXXForRangeStmt>(S) ||
              (isa<OMPLoopBasedDirective>(S) && !isa<OMPLoopDirective>(S))) {
            // Only single loop construct is allowed.
            if (CurStmt) {
              CurStmt = OrigStmt;
              break;
            }
            CurStmt = S;
            continue;
          }
          S = S->IgnoreContainers();
          if (auto *InnerCS = dyn_cast_or_null<CompoundStmt>(S))
            NextStatements.push_back(InnerCS);
        }
        if (Statements.empty()) {
          // Found single inner loop or multiple loops - exit.
          if (CurStmt)
            break;
          Statements.swap(NextStatements);
        }
      }
      if (!CurStmt)
        CurStmt = OrigStmt;
    }
  }
  return CurStmt;
}

bool OMPLoopBasedDirective::doForAllLoops(
    Stmt *CurStmt, bool TryImperfectlyNestedLoops, unsigned NumLoops,
    llvm::function_ref<bool(unsigned, Stmt *)> Callback,
    llvm::function_ref<void(OMPLoopTransformationDirective *)>
        OnTransformationCallback) {
  CurStmt = CurStmt->IgnoreContainers();
  for (unsigned Cnt = 0; Cnt < NumLoops; ++Cnt) {
    while (true) {
      auto *Dir = dyn_cast<OMPLoopTransformationDirective>(CurStmt);
      if (!Dir)
        break;

      OnTransformationCallback(Dir);

      Stmt *TransformedStmt = Dir->getTransformedStmt();
      if (!TransformedStmt) {
        unsigned NumGeneratedLoops = Dir->getNumGeneratedLoops();
        if (NumGeneratedLoops == 0) {
          // May happen if the loop transformation does not result in a
          // generated loop (such as full unrolling).
          break;
        }
        if (NumGeneratedLoops > 0) {
          // The loop transformation construct has generated loops, but these
          // may not have been generated yet due to being in a dependent
          // context.
          return true;
        }
      }

      CurStmt = TransformedStmt;
    }
    if (auto *CanonLoop = dyn_cast<OMPCanonicalLoop>(CurStmt))
      CurStmt = CanonLoop->getLoopStmt();
    if (Callback(Cnt, CurStmt))
      return false;
    // Move on to the next nested for loop, or to the loop body.
    // OpenMP [2.8.1, simd construct, Restrictions]
    // All loops associated with the construct must be perfectly nested; that
    // is, there must be no intervening code nor any OpenMP directive between
    // any two loops.
    if (auto *For = dyn_cast<ForStmt>(CurStmt)) {
      CurStmt = For->getBody();
    } else {
      assert(isa<CXXForRangeStmt>(CurStmt) &&
             "Expected canonical for or range-based for loops.");
      CurStmt = cast<CXXForRangeStmt>(CurStmt)->getBody();
    }
    CurStmt = OMPLoopBasedDirective::tryToFindNextInnerLoop(
        CurStmt, TryImperfectlyNestedLoops);
  }
  return true;
}

void OMPLoopBasedDirective::doForAllLoopsBodies(
    Stmt *CurStmt, bool TryImperfectlyNestedLoops, unsigned NumLoops,
    llvm::function_ref<void(unsigned, Stmt *, Stmt *)> Callback) {
  bool Res = OMPLoopBasedDirective::doForAllLoops(
      CurStmt, TryImperfectlyNestedLoops, NumLoops,
      [Callback](unsigned Cnt, Stmt *Loop) {
        Stmt *Body = nullptr;
        if (auto *For = dyn_cast<ForStmt>(Loop)) {
          Body = For->getBody();
        } else {
          assert(isa<CXXForRangeStmt>(Loop) &&
                 "Expected canonical for or range-based for loops.");
          Body = cast<CXXForRangeStmt>(Loop)->getBody();
        }
        if (auto *CanonLoop = dyn_cast<OMPCanonicalLoop>(Body))
          Body = CanonLoop->getLoopStmt();
        Callback(Cnt, Loop, Body);
        return false;
      });
  assert(Res && "Expected only loops");
  (void)Res;
}

Stmt *OMPLoopDirective::getBody() {
  // This relies on the loop form is already checked by Sema.
  Stmt *Body = nullptr;
  OMPLoopBasedDirective::doForAllLoopsBodies(
      Data->getRawStmt(), /*TryImperfectlyNestedLoops=*/true,
      NumAssociatedLoops,
      [&Body](unsigned, Stmt *, Stmt *BodyStmt) { Body = BodyStmt; });
  return Body;
}

void OMPLoopDirective::setCounters(ArrayRef<Expr *> A) {
  assert(A.size() == getLoopsNumber() &&
         "Number of loop counters is not the same as the collapsed number");
  llvm::copy(A, getCounters().begin());
}

void OMPLoopDirective::setPrivateCounters(ArrayRef<Expr *> A) {
  assert(A.size() == getLoopsNumber() && "Number of loop private counters "
                                         "is not the same as the collapsed "
                                         "number");
  llvm::copy(A, getPrivateCounters().begin());
}

void OMPLoopDirective::setInits(ArrayRef<Expr *> A) {
  assert(A.size() == getLoopsNumber() &&
         "Number of counter inits is not the same as the collapsed number");
  llvm::copy(A, getInits().begin());
}

void OMPLoopDirective::setUpdates(ArrayRef<Expr *> A) {
  assert(A.size() == getLoopsNumber() &&
         "Number of counter updates is not the same as the collapsed number");
  llvm::copy(A, getUpdates().begin());
}

void OMPLoopDirective::setFinals(ArrayRef<Expr *> A) {
  assert(A.size() == getLoopsNumber() &&
         "Number of counter finals is not the same as the collapsed number");
  llvm::copy(A, getFinals().begin());
}

void OMPLoopDirective::setDependentCounters(ArrayRef<Expr *> A) {
  assert(
      A.size() == getLoopsNumber() &&
      "Number of dependent counters is not the same as the collapsed number");
  llvm::copy(A, getDependentCounters().begin());
}

void OMPLoopDirective::setDependentInits(ArrayRef<Expr *> A) {
  assert(A.size() == getLoopsNumber() &&
         "Number of dependent inits is not the same as the collapsed number");
  llvm::copy(A, getDependentInits().begin());
}

void OMPLoopDirective::setFinalsConditions(ArrayRef<Expr *> A) {
  assert(A.size() == getLoopsNumber() &&
         "Number of finals conditions is not the same as the collapsed number");
  llvm::copy(A, getFinalsConditions().begin());
}

OMPMetaDirective *OMPMetaDirective::Create(const ASTContext &C,
                                           SourceLocation StartLoc,
                                           SourceLocation EndLoc,
                                           ArrayRef<OMPClause *> Clauses,
                                           Stmt *AssociatedStmt, Stmt *IfStmt) {
  auto *Dir = createDirective<OMPMetaDirective>(
      C, Clauses, AssociatedStmt, /*NumChildren=*/1, StartLoc, EndLoc);
  Dir->setIfStmt(IfStmt);
  return Dir;
}

OMPMetaDirective *OMPMetaDirective::CreateEmpty(const ASTContext &C,
                                                unsigned NumClauses,
                                                EmptyShell) {
  return createEmptyDirective<OMPMetaDirective>(C, NumClauses,
                                                /*HasAssociatedStmt=*/true,
                                                /*NumChildren=*/1);
}

OMPParallelDirective *OMPParallelDirective::Create(
    const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
    ArrayRef<OMPClause *> Clauses, Stmt *AssociatedStmt, Expr *TaskRedRef,
    bool HasCancel) {
  auto *Dir = createDirective<OMPParallelDirective>(
      C, Clauses, AssociatedStmt, /*NumChildren=*/1, StartLoc, EndLoc);
  Dir->setTaskReductionRefExpr(TaskRedRef);
  Dir->setHasCancel(HasCancel);
  return Dir;
}

OMPParallelDirective *OMPParallelDirective::CreateEmpty(const ASTContext &C,
                                                        unsigned NumClauses,
                                                        EmptyShell) {
  return createEmptyDirective<OMPParallelDirective>(C, NumClauses,
                                                    /*HasAssociatedStmt=*/true,
                                                    /*NumChildren=*/1);
}

OMPSimdDirective *OMPSimdDirective::Create(
    const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
    unsigned CollapsedNum, ArrayRef<OMPClause *> Clauses, Stmt *AssociatedStmt,
    const HelperExprs &Exprs, OpenMPDirectiveKind ParamPrevMappedDirective) {
  auto *Dir = createDirective<OMPSimdDirective>(
      C, Clauses, AssociatedStmt, numLoopChildren(CollapsedNum, OMPD_simd),
      StartLoc, EndLoc, CollapsedNum);
  Dir->setIterationVariable(Exprs.IterationVarRef);
  Dir->setLastIteration(Exprs.LastIteration);
  Dir->setCalcLastIteration(Exprs.CalcLastIteration);
  Dir->setPreCond(Exprs.PreCond);
  Dir->setCond(Exprs.Cond);
  Dir->setInit(Exprs.Init);
  Dir->setInc(Exprs.Inc);
  Dir->setCounters(Exprs.Counters);
  Dir->setPrivateCounters(Exprs.PrivateCounters);
  Dir->setInits(Exprs.Inits);
  Dir->setUpdates(Exprs.Updates);
  Dir->setFinals(Exprs.Finals);
  Dir->setDependentCounters(Exprs.DependentCounters);
  Dir->setDependentInits(Exprs.DependentInits);
  Dir->setFinalsConditions(Exprs.FinalsConditions);
  Dir->setPreInits(Exprs.PreInits);
  Dir->setMappedDirective(ParamPrevMappedDirective);
  return Dir;
}

OMPSimdDirective *OMPSimdDirective::CreateEmpty(const ASTContext &C,
                                                unsigned NumClauses,
                                                unsigned CollapsedNum,
                                                EmptyShell) {
  return createEmptyDirective<OMPSimdDirective>(
      C, NumClauses, /*HasAssociatedStmt=*/true,
      numLoopChildren(CollapsedNum, OMPD_simd), CollapsedNum);
}

OMPForDirective *OMPForDirective::Create(
    const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
    unsigned CollapsedNum, ArrayRef<OMPClause *> Clauses, Stmt *AssociatedStmt,
    const HelperExprs &Exprs, Expr *TaskRedRef, bool HasCancel,
    OpenMPDirectiveKind ParamPrevMappedDirective) {
  auto *Dir = createDirective<OMPForDirective>(
      C, Clauses, AssociatedStmt, numLoopChildren(CollapsedNum, OMPD_for) + 1,
      StartLoc, EndLoc, CollapsedNum);
  Dir->setIterationVariable(Exprs.IterationVarRef);
  Dir->setLastIteration(Exprs.LastIteration);
  Dir->setCalcLastIteration(Exprs.CalcLastIteration);
  Dir->setPreCond(Exprs.PreCond);
  Dir->setCond(Exprs.Cond);
  Dir->setInit(Exprs.Init);
  Dir->setInc(Exprs.Inc);
  Dir->setIsLastIterVariable(Exprs.IL);
  Dir->setLowerBoundVariable(Exprs.LB);
  Dir->setUpperBoundVariable(Exprs.UB);
  Dir->setStrideVariable(Exprs.ST);
  Dir->setEnsureUpperBound(Exprs.EUB);
  Dir->setNextLowerBound(Exprs.NLB);
  Dir->setNextUpperBound(Exprs.NUB);
  Dir->setNumIterations(Exprs.NumIterations);
  Dir->setCounters(Exprs.Counters);
  Dir->setPrivateCounters(Exprs.PrivateCounters);
  Dir->setInits(Exprs.Inits);
  Dir->setUpdates(Exprs.Updates);
  Dir->setFinals(Exprs.Finals);
  Dir->setDependentCounters(Exprs.DependentCounters);
  Dir->setDependentInits(Exprs.DependentInits);
  Dir->setFinalsConditions(Exprs.FinalsConditions);
  Dir->setPreInits(Exprs.PreInits);
  Dir->setTaskReductionRefExpr(TaskRedRef);
  Dir->setHasCancel(HasCancel);
  Dir->setMappedDirective(ParamPrevMappedDirective);
  return Dir;
}

Stmt *OMPLoopTransformationDirective::getTransformedStmt() const {
  switch (getStmtClass()) {
#define STMT(CLASS, PARENT)
#define ABSTRACT_STMT(CLASS)
#define OMPLOOPTRANSFORMATIONDIRECTIVE(CLASS, PARENT)                          \
  case Stmt::CLASS##Class:                                                     \
    return static_cast<const CLASS *>(this)->getTransformedStmt();
#include "clang/AST/StmtNodes.inc"
  default:
    llvm_unreachable("Not a loop transformation");
  }
}

Stmt *OMPLoopTransformationDirective::getPreInits() const {
  switch (getStmtClass()) {
#define STMT(CLASS, PARENT)
#define ABSTRACT_STMT(CLASS)
#define OMPLOOPTRANSFORMATIONDIRECTIVE(CLASS, PARENT)                          \
  case Stmt::CLASS##Class:                                                     \
    return static_cast<const CLASS *>(this)->getPreInits();
#include "clang/AST/StmtNodes.inc"
  default:
    llvm_unreachable("Not a loop transformation");
  }
}

OMPForDirective *OMPForDirective::CreateEmpty(const ASTContext &C,
                                              unsigned NumClauses,
                                              unsigned CollapsedNum,
                                              EmptyShell) {
  return createEmptyDirective<OMPForDirective>(
      C, NumClauses, /*HasAssociatedStmt=*/true,
      numLoopChildren(CollapsedNum, OMPD_for) + 1, CollapsedNum);
}

OMPTileDirective *
OMPTileDirective::Create(const ASTContext &C, SourceLocation StartLoc,
                         SourceLocation EndLoc, ArrayRef<OMPClause *> Clauses,
                         unsigned NumLoops, Stmt *AssociatedStmt,
                         Stmt *TransformedStmt, Stmt *PreInits) {
  OMPTileDirective *Dir = createDirective<OMPTileDirective>(
      C, Clauses, AssociatedStmt, TransformedStmtOffset + 1, StartLoc, EndLoc,
      NumLoops);
  Dir->setTransformedStmt(TransformedStmt);
  Dir->setPreInits(PreInits);
  return Dir;
}

OMPTileDirective *OMPTileDirective::CreateEmpty(const ASTContext &C,
                                                unsigned NumClauses,
                                                unsigned NumLoops) {
  return createEmptyDirective<OMPTileDirective>(
      C, NumClauses, /*HasAssociatedStmt=*/true, TransformedStmtOffset + 1,
      SourceLocation(), SourceLocation(), NumLoops);
}

OMPUnrollDirective *
OMPUnrollDirective::Create(const ASTContext &C, SourceLocation StartLoc,
                           SourceLocation EndLoc, ArrayRef<OMPClause *> Clauses,
                           Stmt *AssociatedStmt, unsigned NumGeneratedLoops,
                           Stmt *TransformedStmt, Stmt *PreInits) {
  assert(NumGeneratedLoops <= 1 && "Unrolling generates at most one loop");

  auto *Dir = createDirective<OMPUnrollDirective>(
      C, Clauses, AssociatedStmt, TransformedStmtOffset + 1, StartLoc, EndLoc);
  Dir->setNumGeneratedLoops(NumGeneratedLoops);
  Dir->setTransformedStmt(TransformedStmt);
  Dir->setPreInits(PreInits);
  return Dir;
}

OMPUnrollDirective *OMPUnrollDirective::CreateEmpty(const ASTContext &C,
                                                    unsigned NumClauses) {
  return createEmptyDirective<OMPUnrollDirective>(
      C, NumClauses, /*HasAssociatedStmt=*/true, TransformedStmtOffset + 1,
      SourceLocation(), SourceLocation());
}

OMPReverseDirective *
OMPReverseDirective::Create(const ASTContext &C, SourceLocation StartLoc,
                            SourceLocation EndLoc, Stmt *AssociatedStmt,
                            Stmt *TransformedStmt, Stmt *PreInits) {
  OMPReverseDirective *Dir = createDirective<OMPReverseDirective>(
      C, std::nullopt, AssociatedStmt, TransformedStmtOffset + 1, StartLoc,
      EndLoc);
  Dir->setTransformedStmt(TransformedStmt);
  Dir->setPreInits(PreInits);
  return Dir;
}

OMPReverseDirective *OMPReverseDirective::CreateEmpty(const ASTContext &C) {
  return createEmptyDirective<OMPReverseDirective>(
      C, /*NumClauses=*/0, /*HasAssociatedStmt=*/true,
      TransformedStmtOffset + 1, SourceLocation(), SourceLocation());
}

OMPInterchangeDirective *OMPInterchangeDirective::Create(
    const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
    ArrayRef<OMPClause *> Clauses, unsigned NumLoops, Stmt *AssociatedStmt,
    Stmt *TransformedStmt, Stmt *PreInits) {
  OMPInterchangeDirective *Dir = createDirective<OMPInterchangeDirective>(
      C, Clauses, AssociatedStmt, TransformedStmtOffset + 1, StartLoc, EndLoc,
      NumLoops);
  Dir->setTransformedStmt(TransformedStmt);
  Dir->setPreInits(PreInits);
  return Dir;
}

OMPInterchangeDirective *
OMPInterchangeDirective::CreateEmpty(const ASTContext &C, unsigned NumClauses,
                                     unsigned NumLoops) {
  return createEmptyDirective<OMPInterchangeDirective>(
      C, NumClauses, /*HasAssociatedStmt=*/true, TransformedStmtOffset + 1,
      SourceLocation(), SourceLocation(), NumLoops);
}

OMPForSimdDirective *
OMPForSimdDirective::Create(const ASTContext &C, SourceLocation StartLoc,
                            SourceLocation EndLoc, unsigned CollapsedNum,
                            ArrayRef<OMPClause *> Clauses, Stmt *AssociatedStmt,
                            const HelperExprs &Exprs) {
  auto *Dir = createDirective<OMPForSimdDirective>(
      C, Clauses, AssociatedStmt, numLoopChildren(CollapsedNum, OMPD_for_simd),
      StartLoc, EndLoc, CollapsedNum);
  Dir->setIterationVariable(Exprs.IterationVarRef);
  Dir->setLastIteration(Exprs.LastIteration);
  Dir->setCalcLastIteration(Exprs.CalcLastIteration);
  Dir->setPreCond(Exprs.PreCond);
  Dir->setCond(Exprs.Cond);
  Dir->setInit(Exprs.Init);
  Dir->setInc(Exprs.Inc);
  Dir->setIsLastIterVariable(Exprs.IL);
  Dir->setLowerBoundVariable(Exprs.LB);
  Dir->setUpperBoundVariable(Exprs.UB);
  Dir->setStrideVariable(Exprs.ST);
  Dir->setEnsureUpperBound(Exprs.EUB);
  Dir->setNextLowerBound(Exprs.NLB);
  Dir->setNextUpperBound(Exprs.NUB);
  Dir->setNumIterations(Exprs.NumIterations);
  Dir->setCounters(Exprs.Counters);
  Dir->setPrivateCounters(Exprs.PrivateCounters);
  Dir->setInits(Exprs.Inits);
  Dir->setUpdates(Exprs.Updates);
  Dir->setFinals(Exprs.Finals);
  Dir->setDependentCounters(Exprs.DependentCounters);
  Dir->setDependentInits(Exprs.DependentInits);
  Dir->setFinalsConditions(Exprs.FinalsConditions);
  Dir->setPreInits(Exprs.PreInits);
  return Dir;
}

OMPForSimdDirective *OMPForSimdDirective::CreateEmpty(const ASTContext &C,
                                                      unsigned NumClauses,
                                                      unsigned CollapsedNum,
                                                      EmptyShell) {
  return createEmptyDirective<OMPForSimdDirective>(
      C, NumClauses, /*HasAssociatedStmt=*/true,
      numLoopChildren(CollapsedNum, OMPD_for_simd), CollapsedNum);
}

OMPSectionsDirective *OMPSectionsDirective::Create(
    const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
    ArrayRef<OMPClause *> Clauses, Stmt *AssociatedStmt, Expr *TaskRedRef,
    bool HasCancel) {
  auto *Dir = createDirective<OMPSectionsDirective>(C, Clauses, AssociatedStmt,
                                                    /*NumChildren=*/1, StartLoc,
                                                    EndLoc);
  Dir->setTaskReductionRefExpr(TaskRedRef);
  Dir->setHasCancel(HasCancel);
  return Dir;
}

OMPSectionsDirective *OMPSectionsDirective::CreateEmpty(const ASTContext &C,
                                                        unsigned NumClauses,
                                                        EmptyShell) {
  return createEmptyDirective<OMPSectionsDirective>(C, NumClauses,
                                                    /*HasAssociatedStmt=*/true,
                                                    /*NumChildren=*/1);
}

OMPSectionDirective *OMPSectionDirective::Create(const ASTContext &C,
                                                 SourceLocation StartLoc,
                                                 SourceLocation EndLoc,
                                                 Stmt *AssociatedStmt,
                                                 bool HasCancel) {
  auto *Dir =
      createDirective<OMPSectionDirective>(C, std::nullopt, AssociatedStmt,
                                           /*NumChildren=*/0, StartLoc, EndLoc);
  Dir->setHasCancel(HasCancel);
  return Dir;
}

OMPSectionDirective *OMPSectionDirective::CreateEmpty(const ASTContext &C,
                                                      EmptyShell) {
  return createEmptyDirective<OMPSectionDirective>(C, /*NumClauses=*/0,
                                                   /*HasAssociatedStmt=*/true);
}

OMPScopeDirective *OMPScopeDirective::Create(const ASTContext &C,
                                             SourceLocation StartLoc,
                                             SourceLocation EndLoc,
                                             ArrayRef<OMPClause *> Clauses,
                                             Stmt *AssociatedStmt) {
  return createDirective<OMPScopeDirective>(C, Clauses, AssociatedStmt,
                                            /*NumChildren=*/0, StartLoc,
                                            EndLoc);
}

OMPScopeDirective *OMPScopeDirective::CreateEmpty(const ASTContext &C,
                                                  unsigned NumClauses,
                                                  EmptyShell) {
  return createEmptyDirective<OMPScopeDirective>(C, NumClauses,
                                                 /*HasAssociatedStmt=*/true);
}

OMPSingleDirective *OMPSingleDirective::Create(const ASTContext &C,
                                               SourceLocation StartLoc,
                                               SourceLocation EndLoc,
                                               ArrayRef<OMPClause *> Clauses,
                                               Stmt *AssociatedStmt) {
  return createDirective<OMPSingleDirective>(C, Clauses, AssociatedStmt,
                                             /*NumChildren=*/0, StartLoc,
                                             EndLoc);
}

OMPSingleDirective *OMPSingleDirective::CreateEmpty(const ASTContext &C,
                                                    unsigned NumClauses,
                                                    EmptyShell) {
  return createEmptyDirective<OMPSingleDirective>(C, NumClauses,
                                                  /*HasAssociatedStmt=*/true);
}

OMPMasterDirective *OMPMasterDirective::Create(const ASTContext &C,
                                               SourceLocation StartLoc,
                                               SourceLocation EndLoc,
                                               Stmt *AssociatedStmt) {
  return createDirective<OMPMasterDirective>(C, std::nullopt, AssociatedStmt,
                                             /*NumChildren=*/0, StartLoc,
                                             EndLoc);
}

OMPMasterDirective *OMPMasterDirective::CreateEmpty(const ASTContext &C,
                                                    EmptyShell) {
  return createEmptyDirective<OMPMasterDirective>(C, /*NumClauses=*/0,
                                                  /*HasAssociatedStmt=*/true);
}

OMPCriticalDirective *OMPCriticalDirective::Create(
    const ASTContext &C, const DeclarationNameInfo &Name,
    SourceLocation StartLoc, SourceLocation EndLoc,
    ArrayRef<OMPClause *> Clauses, Stmt *AssociatedStmt) {
  return createDirective<OMPCriticalDirective>(C, Clauses, AssociatedStmt,
                                               /*NumChildren=*/0, Name,
                                               StartLoc, EndLoc);
}

OMPCriticalDirective *OMPCriticalDirective::CreateEmpty(const ASTContext &C,
                                                        unsigned NumClauses,
                                                        EmptyShell) {
  return createEmptyDirective<OMPCriticalDirective>(C, NumClauses,
                                                    /*HasAssociatedStmt=*/true);
}

OMPParallelForDirective *OMPParallelForDirective::Create(
    const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
    unsigned CollapsedNum, ArrayRef<OMPClause *> Clauses, Stmt *AssociatedStmt,
    const HelperExprs &Exprs, Expr *TaskRedRef, bool HasCancel) {
  auto *Dir = createDirective<OMPParallelForDirective>(
      C, Clauses, AssociatedStmt,
      numLoopChildren(CollapsedNum, OMPD_parallel_for) + 1, StartLoc, EndLoc,
      CollapsedNum);
  Dir->setIterationVariable(Exprs.IterationVarRef);
  Dir->setLastIteration(Exprs.LastIteration);
  Dir->setCalcLastIteration(Exprs.CalcLastIteration);
  Dir->setPreCond(Exprs.PreCond);
  Dir->setCond(Exprs.Cond);
  Dir->setInit(Exprs.Init);
  Dir->setInc(Exprs.Inc);
  Dir->setIsLastIterVariable(Exprs.IL);
  Dir->setLowerBoundVariable(Exprs.LB);
  Dir->setUpperBoundVariable(Exprs.UB);
  Dir->setStrideVariable(Exprs.ST);
  Dir->setEnsureUpperBound(Exprs.EUB);
  Dir->setNextLowerBound(Exprs.NLB);
  Dir->setNextUpperBound(Exprs.NUB);
  Dir->setNumIterations(Exprs.NumIterations);
  Dir->setCounters(Exprs.Counters);
  Dir->setPrivateCounters(Exprs.PrivateCounters);
  Dir->setInits(Exprs.Inits);
  Dir->setUpdates(Exprs.Updates);
  Dir->setFinals(Exprs.Finals);
  Dir->setDependentCounters(Exprs.DependentCounters);
  Dir->setDependentInits(Exprs.DependentInits);
  Dir->setFinalsConditions(Exprs.FinalsConditions);
  Dir->setPreInits(Exprs.PreInits);
  Dir->setTaskReductionRefExpr(TaskRedRef);
  Dir->setHasCancel(HasCancel);
  return Dir;
}

OMPParallelForDirective *
OMPParallelForDirective::CreateEmpty(const ASTContext &C, unsigned NumClauses,
                                     unsigned CollapsedNum, EmptyShell) {
  return createEmptyDirective<OMPParallelForDirective>(
      C, NumClauses, /*HasAssociatedStmt=*/true,
      numLoopChildren(CollapsedNum, OMPD_parallel_for) + 1, CollapsedNum);
}

OMPParallelForSimdDirective *OMPParallelForSimdDirective::Create(
    const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
    unsigned CollapsedNum, ArrayRef<OMPClause *> Clauses, Stmt *AssociatedStmt,
    const HelperExprs &Exprs) {
  auto *Dir = createDirective<OMPParallelForSimdDirective>(
      C, Clauses, AssociatedStmt,
      numLoopChildren(CollapsedNum, OMPD_parallel_for_simd), StartLoc, EndLoc,
      CollapsedNum);
  Dir->setIterationVariable(Exprs.IterationVarRef);
  Dir->setLastIteration(Exprs.LastIteration);
  Dir->setCalcLastIteration(Exprs.CalcLastIteration);
  Dir->setPreCond(Exprs.PreCond);
  Dir->setCond(Exprs.Cond);
  Dir->setInit(Exprs.Init);
  Dir->setInc(Exprs.Inc);
  Dir->setIsLastIterVariable(Exprs.IL);
  Dir->setLowerBoundVariable(Exprs.LB);
  Dir->setUpperBoundVariable(Exprs.UB);
  Dir->setStrideVariable(Exprs.ST);
  Dir->setEnsureUpperBound(Exprs.EUB);
  Dir->setNextLowerBound(Exprs.NLB);
  Dir->setNextUpperBound(Exprs.NUB);
  Dir->setNumIterations(Exprs.NumIterations);
  Dir->setCounters(Exprs.Counters);
  Dir->setPrivateCounters(Exprs.PrivateCounters);
  Dir->setInits(Exprs.Inits);
  Dir->setUpdates(Exprs.Updates);
  Dir->setFinals(Exprs.Finals);
  Dir->setDependentCounters(Exprs.DependentCounters);
  Dir->setDependentInits(Exprs.DependentInits);
  Dir->setFinalsConditions(Exprs.FinalsConditions);
  Dir->setPreInits(Exprs.PreInits);
  return Dir;
}

OMPParallelForSimdDirective *
OMPParallelForSimdDirective::CreateEmpty(const ASTContext &C,
                                         unsigned NumClauses,
                                         unsigned CollapsedNum, EmptyShell) {
  return createEmptyDirective<OMPParallelForSimdDirective>(
      C, NumClauses, /*HasAssociatedStmt=*/true,
      numLoopChildren(CollapsedNum, OMPD_parallel_for_simd), CollapsedNum);
}

OMPParallelMasterDirective *OMPParallelMasterDirective::Create(
    const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
    ArrayRef<OMPClause *> Clauses, Stmt *AssociatedStmt, Expr *TaskRedRef) {
  auto *Dir = createDirective<OMPParallelMasterDirective>(
      C, Clauses, AssociatedStmt, /*NumChildren=*/1, StartLoc, EndLoc);
  Dir->setTaskReductionRefExpr(TaskRedRef);
  return Dir;
}

OMPParallelMasterDirective *
OMPParallelMasterDirective::CreateEmpty(const ASTContext &C,
                                        unsigned NumClauses, EmptyShell) {
  return createEmptyDirective<OMPParallelMasterDirective>(
      C, NumClauses, /*HasAssociatedStmt=*/true, /*NumChildren=*/1);
}

OMPParallelMaskedDirective *OMPParallelMaskedDirective::Create(
    const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
    ArrayRef<OMPClause *> Clauses, Stmt *AssociatedStmt, Expr *TaskRedRef) {
  auto *Dir = createDirective<OMPParallelMaskedDirective>(
      C, Clauses, AssociatedStmt, /*NumChildren=*/1, StartLoc, EndLoc);
  Dir->setTaskReductionRefExpr(TaskRedRef);
  return Dir;
}

OMPParallelMaskedDirective *
OMPParallelMaskedDirective::CreateEmpty(const ASTContext &C,
                                        unsigned NumClauses, EmptyShell) {
  return createEmptyDirective<OMPParallelMaskedDirective>(
      C, NumClauses, /*HasAssociatedStmt=*/true, /*NumChildren=*/1);
}

OMPParallelSectionsDirective *OMPParallelSectionsDirective::Create(
    const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
    ArrayRef<OMPClause *> Clauses, Stmt *AssociatedStmt, Expr *TaskRedRef,
    bool HasCancel) {
  auto *Dir = createDirective<OMPParallelSectionsDirective>(
      C, Clauses, AssociatedStmt, /*NumChildren=*/1, StartLoc, EndLoc);
  Dir->setTaskReductionRefExpr(TaskRedRef);
  Dir->setHasCancel(HasCancel);
  return Dir;
}

OMPParallelSectionsDirective *
OMPParallelSectionsDirective::CreateEmpty(const ASTContext &C,
                                          unsigned NumClauses, EmptyShell) {
  return createEmptyDirective<OMPParallelSectionsDirective>(
      C, NumClauses, /*HasAssociatedStmt=*/true, /*NumChildren=*/1);
}

OMPTaskDirective *
OMPTaskDirective::Create(const ASTContext &C, SourceLocation StartLoc,
                         SourceLocation EndLoc, ArrayRef<OMPClause *> Clauses,
                         Stmt *AssociatedStmt, bool HasCancel) {
  auto *Dir = createDirective<OMPTaskDirective>(
      C, Clauses, AssociatedStmt, /*NumChildren=*/0, StartLoc, EndLoc);
  Dir->setHasCancel(HasCancel);
  return Dir;
}

OMPTaskDirective *OMPTaskDirective::CreateEmpty(const ASTContext &C,
                                                unsigned NumClauses,
                                                EmptyShell) {
  return createEmptyDirective<OMPTaskDirective>(C, NumClauses,
                                                /*HasAssociatedStmt=*/true);
}

OMPTaskyieldDirective *OMPTaskyieldDirective::Create(const ASTContext &C,
                                                     SourceLocation StartLoc,
                                                     SourceLocation EndLoc) {
  return new (C) OMPTaskyieldDirective(StartLoc, EndLoc);
}

OMPTaskyieldDirective *OMPTaskyieldDirective::CreateEmpty(const ASTContext &C,
                                                          EmptyShell) {
  return new (C) OMPTaskyieldDirective();
}

OMPErrorDirective *OMPErrorDirective::Create(const ASTContext &C,
                                             SourceLocation StartLoc,
                                             SourceLocation EndLoc,
                                             ArrayRef<OMPClause *> Clauses) {
  return createDirective<OMPErrorDirective>(
      C, Clauses, /*AssociatedStmt=*/nullptr, /*NumChildren=*/0, StartLoc,
      EndLoc);
}

OMPErrorDirective *OMPErrorDirective::CreateEmpty(const ASTContext &C,
                                                  unsigned NumClauses,
                                                  EmptyShell) {
  return createEmptyDirective<OMPErrorDirective>(C, NumClauses);
}

OMPBarrierDirective *OMPBarrierDirective::Create(const ASTContext &C,
                                                 SourceLocation StartLoc,
                                                 SourceLocation EndLoc) {
  return new (C) OMPBarrierDirective(StartLoc, EndLoc);
}

OMPBarrierDirective *OMPBarrierDirective::CreateEmpty(const ASTContext &C,
                                                      EmptyShell) {
  return new (C) OMPBarrierDirective();
}

OMPTaskwaitDirective *
OMPTaskwaitDirective::Create(const ASTContext &C, SourceLocation StartLoc,
                             SourceLocation EndLoc,
                             ArrayRef<OMPClause *> Clauses) {
  return createDirective<OMPTaskwaitDirective>(
      C, Clauses, /*AssociatedStmt=*/nullptr, /*NumChildren=*/0, StartLoc,
      EndLoc);
}

OMPTaskwaitDirective *OMPTaskwaitDirective::CreateEmpty(const ASTContext &C,
                                                        unsigned NumClauses,
                                                        EmptyShell) {
  return createEmptyDirective<OMPTaskwaitDirective>(C, NumClauses);
}

OMPTaskgroupDirective *OMPTaskgroupDirective::Create(
    const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
    ArrayRef<OMPClause *> Clauses, Stmt *AssociatedStmt, Expr *ReductionRef) {
  auto *Dir = createDirective<OMPTaskgroupDirective>(
      C, Clauses, AssociatedStmt, /*NumChildren=*/1, StartLoc, EndLoc);
  Dir->setReductionRef(ReductionRef);
  return Dir;
}

OMPTaskgroupDirective *OMPTaskgroupDirective::CreateEmpty(const ASTContext &C,
                                                          unsigned NumClauses,
                                                          EmptyShell) {
  return createEmptyDirective<OMPTaskgroupDirective>(
      C, NumClauses, /*HasAssociatedStmt=*/true, /*NumChildren=*/1);
}

OMPCancellationPointDirective *OMPCancellationPointDirective::Create(
    const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
    OpenMPDirectiveKind CancelRegion) {
  auto *Dir = new (C) OMPCancellationPointDirective(StartLoc, EndLoc);
  Dir->setCancelRegion(CancelRegion);
  return Dir;
}

OMPCancellationPointDirective *
OMPCancellationPointDirective::CreateEmpty(const ASTContext &C, EmptyShell) {
  return new (C) OMPCancellationPointDirective();
}

OMPCancelDirective *
OMPCancelDirective::Create(const ASTContext &C, SourceLocation StartLoc,
                           SourceLocation EndLoc, ArrayRef<OMPClause *> Clauses,
                           OpenMPDirectiveKind CancelRegion) {
  auto *Dir = createDirective<OMPCancelDirective>(
      C, Clauses, /*AssociatedStmt=*/nullptr, /*NumChildren=*/0, StartLoc,
      EndLoc);
  Dir->setCancelRegion(CancelRegion);
  return Dir;
}

OMPCancelDirective *OMPCancelDirective::CreateEmpty(const ASTContext &C,
                                                    unsigned NumClauses,
                                                    EmptyShell) {
  return createEmptyDirective<OMPCancelDirective>(C, NumClauses);
}

OMPFlushDirective *OMPFlushDirective::Create(const ASTContext &C,
                                             SourceLocation StartLoc,
                                             SourceLocation EndLoc,
                                             ArrayRef<OMPClause *> Clauses) {
  return createDirective<OMPFlushDirective>(
      C, Clauses, /*AssociatedStmt=*/nullptr, /*NumChildren=*/0, StartLoc,
      EndLoc);
}

OMPFlushDirective *OMPFlushDirective::CreateEmpty(const ASTContext &C,
                                                  unsigned NumClauses,
                                                  EmptyShell) {
  return createEmptyDirective<OMPFlushDirective>(C, NumClauses);
}

OMPDepobjDirective *OMPDepobjDirective::Create(const ASTContext &C,
                                               SourceLocation StartLoc,
                                               SourceLocation EndLoc,
                                               ArrayRef<OMPClause *> Clauses) {
  return createDirective<OMPDepobjDirective>(
      C, Clauses, /*AssociatedStmt=*/nullptr,
      /*NumChildren=*/0, StartLoc, EndLoc);
}

OMPDepobjDirective *OMPDepobjDirective::CreateEmpty(const ASTContext &C,
                                                    unsigned NumClauses,
                                                    EmptyShell) {
  return createEmptyDirective<OMPDepobjDirective>(C, NumClauses);
}

OMPScanDirective *OMPScanDirective::Create(const ASTContext &C,
                                           SourceLocation StartLoc,
                                           SourceLocation EndLoc,
                                           ArrayRef<OMPClause *> Clauses) {
  return createDirective<OMPScanDirective>(C, Clauses,
                                           /*AssociatedStmt=*/nullptr,
                                           /*NumChildren=*/0, StartLoc, EndLoc);
}

OMPScanDirective *OMPScanDirective::CreateEmpty(const ASTContext &C,
                                                unsigned NumClauses,
                                                EmptyShell) {
  return createEmptyDirective<OMPScanDirective>(C, NumClauses);
}

OMPOrderedDirective *OMPOrderedDirective::Create(const ASTContext &C,
                                                 SourceLocation StartLoc,
                                                 SourceLocation EndLoc,
                                                 ArrayRef<OMPClause *> Clauses,
                                                 Stmt *AssociatedStmt) {
  return createDirective<OMPOrderedDirective>(
      C, Clauses, cast_or_null<CapturedStmt>(AssociatedStmt),
      /*NumChildren=*/0, StartLoc, EndLoc);
}

OMPOrderedDirective *OMPOrderedDirective::CreateEmpty(const ASTContext &C,
                                                      unsigned NumClauses,
                                                      bool IsStandalone,
                                                      EmptyShell) {
  return createEmptyDirective<OMPOrderedDirective>(C, NumClauses,
                                                   !IsStandalone);
}

OMPAtomicDirective *
OMPAtomicDirective::Create(const ASTContext &C, SourceLocation StartLoc,
                           SourceLocation EndLoc, ArrayRef<OMPClause *> Clauses,
                           Stmt *AssociatedStmt, Expressions Exprs) {
  auto *Dir = createDirective<OMPAtomicDirective>(
      C, Clauses, AssociatedStmt, /*NumChildren=*/7, StartLoc, EndLoc);
  Dir->setX(Exprs.X);
  Dir->setV(Exprs.V);
  Dir->setR(Exprs.R);
  Dir->setExpr(Exprs.E);
  Dir->setUpdateExpr(Exprs.UE);
  Dir->setD(Exprs.D);
  Dir->setCond(Exprs.Cond);
  Dir->Flags.IsXLHSInRHSPart = Exprs.IsXLHSInRHSPart ? 1 : 0;
  Dir->Flags.IsPostfixUpdate = Exprs.IsPostfixUpdate ? 1 : 0;
  Dir->Flags.IsFailOnly = Exprs.IsFailOnly ? 1 : 0;
  return Dir;
}

OMPAtomicDirective *OMPAtomicDirective::CreateEmpty(const ASTContext &C,
                                                    unsigned NumClauses,
                                                    EmptyShell) {
  return createEmptyDirective<OMPAtomicDirective>(
      C, NumClauses, /*HasAssociatedStmt=*/true, /*NumChildren=*/7);
}

OMPTargetDirective *OMPTargetDirective::Create(const ASTContext &C,
                                               SourceLocation StartLoc,
                                               SourceLocation EndLoc,
                                               ArrayRef<OMPClause *> Clauses,
                                               Stmt *AssociatedStmt) {
  return createDirective<OMPTargetDirective>(
      C, Clauses, AssociatedStmt, /*NumChildren=*/0, StartLoc, EndLoc);
}

OMPTargetDirective *OMPTargetDirective::CreateEmpty(const ASTContext &C,
                                                    unsigned NumClauses,
                                                    EmptyShell) {
  return createEmptyDirective<OMPTargetDirective>(C, NumClauses,
                                                  /*HasAssociatedStmt=*/true);
}

OMPTargetParallelDirective *OMPTargetParallelDirective::Create(
    const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
    ArrayRef<OMPClause *> Clauses, Stmt *AssociatedStmt, Expr *TaskRedRef,
    bool HasCancel) {
  auto *Dir = createDirective<OMPTargetParallelDirective>(
      C, Clauses, AssociatedStmt, /*NumChildren=*/1, StartLoc, EndLoc);
  Dir->setTaskReductionRefExpr(TaskRedRef);
  Dir->setHasCancel(HasCancel);
  return Dir;
}

OMPTargetParallelDirective *
OMPTargetParallelDirective::CreateEmpty(const ASTContext &C,
                                        unsigned NumClauses, EmptyShell) {
  return createEmptyDirective<OMPTargetParallelDirective>(
      C, NumClauses, /*HasAssociatedStmt=*/true, /*NumChildren=*/1);
}

OMPTargetParallelForDirective *OMPTargetParallelForDirective::Create(
    const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
    unsigned CollapsedNum, ArrayRef<OMPClause *> Clauses, Stmt *AssociatedStmt,
    const HelperExprs &Exprs, Expr *TaskRedRef, bool HasCancel) {
  auto *Dir = createDirective<OMPTargetParallelForDirective>(
      C, Clauses, AssociatedStmt,
      numLoopChildren(CollapsedNum, OMPD_target_parallel_for) + 1, StartLoc,
      EndLoc, CollapsedNum);
  Dir->setIterationVariable(Exprs.IterationVarRef);
  Dir->setLastIteration(Exprs.LastIteration);
  Dir->setCalcLastIteration(Exprs.CalcLastIteration);
  Dir->setPreCond(Exprs.PreCond);
  Dir->setCond(Exprs.Cond);
  Dir->setInit(Exprs.Init);
  Dir->setInc(Exprs.Inc);
  Dir->setIsLastIterVariable(Exprs.IL);
  Dir->setLowerBoundVariable(Exprs.LB);
  Dir->setUpperBoundVariable(Exprs.UB);
  Dir->setStrideVariable(Exprs.ST);
  Dir->setEnsureUpperBound(Exprs.EUB);
  Dir->setNextLowerBound(Exprs.NLB);
  Dir->setNextUpperBound(Exprs.NUB);
  Dir->setNumIterations(Exprs.NumIterations);
  Dir->setCounters(Exprs.Counters);
  Dir->setPrivateCounters(Exprs.PrivateCounters);
  Dir->setInits(Exprs.Inits);
  Dir->setUpdates(Exprs.Updates);
  Dir->setFinals(Exprs.Finals);
  Dir->setDependentCounters(Exprs.DependentCounters);
  Dir->setDependentInits(Exprs.DependentInits);
  Dir->setFinalsConditions(Exprs.FinalsConditions);
  Dir->setPreInits(Exprs.PreInits);
  Dir->setTaskReductionRefExpr(TaskRedRef);
  Dir->setHasCancel(HasCancel);
  return Dir;
}

OMPTargetParallelForDirective *
OMPTargetParallelForDirective::CreateEmpty(const ASTContext &C,
                                           unsigned NumClauses,
                                           unsigned CollapsedNum, EmptyShell) {
  return createEmptyDirective<OMPTargetParallelForDirective>(
      C, NumClauses, /*HasAssociatedStmt=*/true,
      numLoopChildren(CollapsedNum, OMPD_target_parallel_for) + 1,
      CollapsedNum);
}

OMPTargetDataDirective *OMPTargetDataDirective::Create(
    const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
    ArrayRef<OMPClause *> Clauses, Stmt *AssociatedStmt) {
  return createDirective<OMPTargetDataDirective>(
      C, Clauses, AssociatedStmt, /*NumChildren=*/0, StartLoc, EndLoc);
}

OMPTargetDataDirective *OMPTargetDataDirective::CreateEmpty(const ASTContext &C,
                                                            unsigned N,
                                                            EmptyShell) {
  return createEmptyDirective<OMPTargetDataDirective>(
      C, N, /*HasAssociatedStmt=*/true);
}

OMPTargetEnterDataDirective *OMPTargetEnterDataDirective::Create(
    const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
    ArrayRef<OMPClause *> Clauses, Stmt *AssociatedStmt) {
  return createDirective<OMPTargetEnterDataDirective>(
      C, Clauses, AssociatedStmt, /*NumChildren=*/0, StartLoc, EndLoc);
}

OMPTargetEnterDataDirective *
OMPTargetEnterDataDirective::CreateEmpty(const ASTContext &C, unsigned N,
                                         EmptyShell) {
  return createEmptyDirective<OMPTargetEnterDataDirective>(
      C, N, /*HasAssociatedStmt=*/true);
}

OMPTargetExitDataDirective *OMPTargetExitDataDirective::Create(
    const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
    ArrayRef<OMPClause *> Clauses, Stmt *AssociatedStmt) {
  return createDirective<OMPTargetExitDataDirective>(
      C, Clauses, AssociatedStmt, /*NumChildren=*/0, StartLoc, EndLoc);
}

OMPTargetExitDataDirective *
OMPTargetExitDataDirective::CreateEmpty(const ASTContext &C, unsigned N,
                                        EmptyShell) {
  return createEmptyDirective<OMPTargetExitDataDirective>(
      C, N, /*HasAssociatedStmt=*/true);
}

OMPTeamsDirective *OMPTeamsDirective::Create(const ASTContext &C,
                                             SourceLocation StartLoc,
                                             SourceLocation EndLoc,
                                             ArrayRef<OMPClause *> Clauses,
                                             Stmt *AssociatedStmt) {
  return createDirective<OMPTeamsDirective>(
      C, Clauses, AssociatedStmt, /*NumChildren=*/0, StartLoc, EndLoc);
}

OMPTeamsDirective *OMPTeamsDirective::CreateEmpty(const ASTContext &C,
                                                  unsigned NumClauses,
                                                  EmptyShell) {
  return createEmptyDirective<OMPTeamsDirective>(C, NumClauses,
                                                 /*HasAssociatedStmt=*/true);
}

OMPTaskLoopDirective *OMPTaskLoopDirective::Create(
    const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
    unsigned CollapsedNum, ArrayRef<OMPClause *> Clauses, Stmt *AssociatedStmt,
    const HelperExprs &Exprs, bool HasCancel) {
  auto *Dir = createDirective<OMPTaskLoopDirective>(
      C, Clauses, AssociatedStmt, numLoopChildren(CollapsedNum, OMPD_taskloop),
      StartLoc, EndLoc, CollapsedNum);
  Dir->setIterationVariable(Exprs.IterationVarRef);
  Dir->setLastIteration(Exprs.LastIteration);
  Dir->setCalcLastIteration(Exprs.CalcLastIteration);
  Dir->setPreCond(Exprs.PreCond);
  Dir->setCond(Exprs.Cond);
  Dir->setInit(Exprs.Init);
  Dir->setInc(Exprs.Inc);
  Dir->setIsLastIterVariable(Exprs.IL);
  Dir->setLowerBoundVariable(Exprs.LB);
  Dir->setUpperBoundVariable(Exprs.UB);
  Dir->setStrideVariable(Exprs.ST);
  Dir->setEnsureUpperBound(Exprs.EUB);
  Dir->setNextLowerBound(Exprs.NLB);
  Dir->setNextUpperBound(Exprs.NUB);
  Dir->setNumIterations(Exprs.NumIterations);
  Dir->setCounters(Exprs.Counters);
  Dir->setPrivateCounters(Exprs.PrivateCounters);
  Dir->setInits(Exprs.Inits);
  Dir->setUpdates(Exprs.Updates);
  Dir->setFinals(Exprs.Finals);
  Dir->setDependentCounters(Exprs.DependentCounters);
  Dir->setDependentInits(Exprs.DependentInits);
  Dir->setFinalsConditions(Exprs.FinalsConditions);
  Dir->setPreInits(Exprs.PreInits);
  Dir->setHasCancel(HasCancel);
  return Dir;
}

OMPTaskLoopDirective *OMPTaskLoopDirective::CreateEmpty(const ASTContext &C,
                                                        unsigned NumClauses,
                                                        unsigned CollapsedNum,
                                                        EmptyShell) {
  return createEmptyDirective<OMPTaskLoopDirective>(
      C, NumClauses, /*HasAssociatedStmt=*/true,
      numLoopChildren(CollapsedNum, OMPD_taskloop), CollapsedNum);
}

OMPTaskLoopSimdDirective *OMPTaskLoopSimdDirective::Create(
    const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
    unsigned CollapsedNum, ArrayRef<OMPClause *> Clauses, Stmt *AssociatedStmt,
    const HelperExprs &Exprs) {
  auto *Dir = createDirective<OMPTaskLoopSimdDirective>(
      C, Clauses, AssociatedStmt,
      numLoopChildren(CollapsedNum, OMPD_taskloop_simd), StartLoc, EndLoc,
      CollapsedNum);
  Dir->setIterationVariable(Exprs.IterationVarRef);
  Dir->setLastIteration(Exprs.LastIteration);
  Dir->setCalcLastIteration(Exprs.CalcLastIteration);
  Dir->setPreCond(Exprs.PreCond);
  Dir->setCond(Exprs.Cond);
  Dir->setInit(Exprs.Init);
  Dir->setInc(Exprs.Inc);
  Dir->setIsLastIterVariable(Exprs.IL);
  Dir->setLowerBoundVariable(Exprs.LB);
  Dir->setUpperBoundVariable(Exprs.UB);
  Dir->setStrideVariable(Exprs.ST);
  Dir->setEnsureUpperBound(Exprs.EUB);
  Dir->setNextLowerBound(Exprs.NLB);
  Dir->setNextUpperBound(Exprs.NUB);
  Dir->setNumIterations(Exprs.NumIterations);
  Dir->setCounters(Exprs.Counters);
  Dir->setPrivateCounters(Exprs.PrivateCounters);
  Dir->setInits(Exprs.Inits);
  Dir->setUpdates(Exprs.Updates);
  Dir->setFinals(Exprs.Finals);
  Dir->setDependentCounters(Exprs.DependentCounters);
  Dir->setDependentInits(Exprs.DependentInits);
  Dir->setFinalsConditions(Exprs.FinalsConditions);
  Dir->setPreInits(Exprs.PreInits);
  return Dir;
}

OMPTaskLoopSimdDirective *
OMPTaskLoopSimdDirective::CreateEmpty(const ASTContext &C, unsigned NumClauses,
                                      unsigned CollapsedNum, EmptyShell) {
  return createEmptyDirective<OMPTaskLoopSimdDirective>(
      C, NumClauses, /*HasAssociatedStmt=*/true,
      numLoopChildren(CollapsedNum, OMPD_taskloop_simd), CollapsedNum);
}

OMPMasterTaskLoopDirective *OMPMasterTaskLoopDirective::Create(
    const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
    unsigned CollapsedNum, ArrayRef<OMPClause *> Clauses, Stmt *AssociatedStmt,
    const HelperExprs &Exprs, bool HasCancel) {
  auto *Dir = createDirective<OMPMasterTaskLoopDirective>(
      C, Clauses, AssociatedStmt,
      numLoopChildren(CollapsedNum, OMPD_master_taskloop), StartLoc, EndLoc,
      CollapsedNum);
  Dir->setIterationVariable(Exprs.IterationVarRef);
  Dir->setLastIteration(Exprs.LastIteration);
  Dir->setCalcLastIteration(Exprs.CalcLastIteration);
  Dir->setPreCond(Exprs.PreCond);
  Dir->setCond(Exprs.Cond);
  Dir->setInit(Exprs.Init);
  Dir->setInc(Exprs.Inc);
  Dir->setIsLastIterVariable(Exprs.IL);
  Dir->setLowerBoundVariable(Exprs.LB);
  Dir->setUpperBoundVariable(Exprs.UB);
  Dir->setStrideVariable(Exprs.ST);
  Dir->setEnsureUpperBound(Exprs.EUB);
  Dir->setNextLowerBound(Exprs.NLB);
  Dir->setNextUpperBound(Exprs.NUB);
  Dir->setNumIterations(Exprs.NumIterations);
  Dir->setCounters(Exprs.Counters);
  Dir->setPrivateCounters(Exprs.PrivateCounters);
  Dir->setInits(Exprs.Inits);
  Dir->setUpdates(Exprs.Updates);
  Dir->setFinals(Exprs.Finals);
  Dir->setDependentCounters(Exprs.DependentCounters);
  Dir->setDependentInits(Exprs.DependentInits);
  Dir->setFinalsConditions(Exprs.FinalsConditions);
  Dir->setPreInits(Exprs.PreInits);
  Dir->setHasCancel(HasCancel);
  return Dir;
}

OMPMasterTaskLoopDirective *
OMPMasterTaskLoopDirective::CreateEmpty(const ASTContext &C,
                                        unsigned NumClauses,
                                        unsigned CollapsedNum, EmptyShell) {
  return createEmptyDirective<OMPMasterTaskLoopDirective>(
      C, NumClauses, /*HasAssociatedStmt=*/true,
      numLoopChildren(CollapsedNum, OMPD_master_taskloop), CollapsedNum);
}

OMPMaskedTaskLoopDirective *OMPMaskedTaskLoopDirective::Create(
    const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
    unsigned CollapsedNum, ArrayRef<OMPClause *> Clauses, Stmt *AssociatedStmt,
    const HelperExprs &Exprs, bool HasCancel) {
  auto *Dir = createDirective<OMPMaskedTaskLoopDirective>(
      C, Clauses, AssociatedStmt,
      numLoopChildren(CollapsedNum, OMPD_masked_taskloop), StartLoc, EndLoc,
      CollapsedNum);
  Dir->setIterationVariable(Exprs.IterationVarRef);
  Dir->setLastIteration(Exprs.LastIteration);
  Dir->setCalcLastIteration(Exprs.CalcLastIteration);
  Dir->setPreCond(Exprs.PreCond);
  Dir->setCond(Exprs.Cond);
  Dir->setInit(Exprs.Init);
  Dir->setInc(Exprs.Inc);
  Dir->setIsLastIterVariable(Exprs.IL);
  Dir->setLowerBoundVariable(Exprs.LB);
  Dir->setUpperBoundVariable(Exprs.UB);
  Dir->setStrideVariable(Exprs.ST);
  Dir->setEnsureUpperBound(Exprs.EUB);
  Dir->setNextLowerBound(Exprs.NLB);
  Dir->setNextUpperBound(Exprs.NUB);
  Dir->setNumIterations(Exprs.NumIterations);
  Dir->setCounters(Exprs.Counters);
  Dir->setPrivateCounters(Exprs.PrivateCounters);
  Dir->setInits(Exprs.Inits);
  Dir->setUpdates(Exprs.Updates);
  Dir->setFinals(Exprs.Finals);
  Dir->setDependentCounters(Exprs.DependentCounters);
  Dir->setDependentInits(Exprs.DependentInits);
  Dir->setFinalsConditions(Exprs.FinalsConditions);
  Dir->setPreInits(Exprs.PreInits);
  Dir->setHasCancel(HasCancel);
  return Dir;
}

OMPMaskedTaskLoopDirective *
OMPMaskedTaskLoopDirective::CreateEmpty(const ASTContext &C,
                                        unsigned NumClauses,
                                        unsigned CollapsedNum, EmptyShell) {
  return createEmptyDirective<OMPMaskedTaskLoopDirective>(
      C, NumClauses, /*HasAssociatedStmt=*/true,
      numLoopChildren(CollapsedNum, OMPD_masked_taskloop), CollapsedNum);
}

OMPMasterTaskLoopSimdDirective *OMPMasterTaskLoopSimdDirective::Create(
    const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
    unsigned CollapsedNum, ArrayRef<OMPClause *> Clauses, Stmt *AssociatedStmt,
    const HelperExprs &Exprs) {
  auto *Dir = createDirective<OMPMasterTaskLoopSimdDirective>(
      C, Clauses, AssociatedStmt,
      numLoopChildren(CollapsedNum, OMPD_master_taskloop_simd), StartLoc,
      EndLoc, CollapsedNum);
  Dir->setIterationVariable(Exprs.IterationVarRef);
  Dir->setLastIteration(Exprs.LastIteration);
  Dir->setCalcLastIteration(Exprs.CalcLastIteration);
  Dir->setPreCond(Exprs.PreCond);
  Dir->setCond(Exprs.Cond);
  Dir->setInit(Exprs.Init);
  Dir->setInc(Exprs.Inc);
  Dir->setIsLastIterVariable(Exprs.IL);
  Dir->setLowerBoundVariable(Exprs.LB);
  Dir->setUpperBoundVariable(Exprs.UB);
  Dir->setStrideVariable(Exprs.ST);
  Dir->setEnsureUpperBound(Exprs.EUB);
  Dir->setNextLowerBound(Exprs.NLB);
  Dir->setNextUpperBound(Exprs.NUB);
  Dir->setNumIterations(Exprs.NumIterations);
  Dir->setCounters(Exprs.Counters);
  Dir->setPrivateCounters(Exprs.PrivateCounters);
  Dir->setInits(Exprs.Inits);
  Dir->setUpdates(Exprs.Updates);
  Dir->setFinals(Exprs.Finals);
  Dir->setDependentCounters(Exprs.DependentCounters);
  Dir->setDependentInits(Exprs.DependentInits);
  Dir->setFinalsConditions(Exprs.FinalsConditions);
  Dir->setPreInits(Exprs.PreInits);
  return Dir;
}

OMPMasterTaskLoopSimdDirective *
OMPMasterTaskLoopSimdDirective::CreateEmpty(const ASTContext &C,
                                            unsigned NumClauses,
                                            unsigned CollapsedNum, EmptyShell) {
  return createEmptyDirective<OMPMasterTaskLoopSimdDirective>(
      C, NumClauses, /*HasAssociatedStmt=*/true,
      numLoopChildren(CollapsedNum, OMPD_master_taskloop_simd), CollapsedNum);
}

OMPMaskedTaskLoopSimdDirective *OMPMaskedTaskLoopSimdDirective::Create(
    const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
    unsigned CollapsedNum, ArrayRef<OMPClause *> Clauses, Stmt *AssociatedStmt,
    const HelperExprs &Exprs) {
  auto *Dir = createDirective<OMPMaskedTaskLoopSimdDirective>(
      C, Clauses, AssociatedStmt,
      numLoopChildren(CollapsedNum, OMPD_masked_taskloop_simd), StartLoc,
      EndLoc, CollapsedNum);
  Dir->setIterationVariable(Exprs.IterationVarRef);
  Dir->setLastIteration(Exprs.LastIteration);
  Dir->setCalcLastIteration(Exprs.CalcLastIteration);
  Dir->setPreCond(Exprs.PreCond);
  Dir->setCond(Exprs.Cond);
  Dir->setInit(Exprs.Init);
  Dir->setInc(Exprs.Inc);
  Dir->setIsLastIterVariable(Exprs.IL);
  Dir->setLowerBoundVariable(Exprs.LB);
  Dir->setUpperBoundVariable(Exprs.UB);
  Dir->setStrideVariable(Exprs.ST);
  Dir->setEnsureUpperBound(Exprs.EUB);
  Dir->setNextLowerBound(Exprs.NLB);
  Dir->setNextUpperBound(Exprs.NUB);
  Dir->setNumIterations(Exprs.NumIterations);
  Dir->setCounters(Exprs.Counters);
  Dir->setPrivateCounters(Exprs.PrivateCounters);
  Dir->setInits(Exprs.Inits);
  Dir->setUpdates(Exprs.Updates);
  Dir->setFinals(Exprs.Finals);
  Dir->setDependentCounters(Exprs.DependentCounters);
  Dir->setDependentInits(Exprs.DependentInits);
  Dir->setFinalsConditions(Exprs.FinalsConditions);
  Dir->setPreInits(Exprs.PreInits);
  return Dir;
}

OMPMaskedTaskLoopSimdDirective *
OMPMaskedTaskLoopSimdDirective::CreateEmpty(const ASTContext &C,
                                            unsigned NumClauses,
                                            unsigned CollapsedNum, EmptyShell) {
  return createEmptyDirective<OMPMaskedTaskLoopSimdDirective>(
      C, NumClauses, /*HasAssociatedStmt=*/true,
      numLoopChildren(CollapsedNum, OMPD_masked_taskloop_simd), CollapsedNum);
}

OMPParallelMasterTaskLoopDirective *OMPParallelMasterTaskLoopDirective::Create(
    const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
    unsigned CollapsedNum, ArrayRef<OMPClause *> Clauses, Stmt *AssociatedStmt,
    const HelperExprs &Exprs, bool HasCancel) {
  auto *Dir = createDirective<OMPParallelMasterTaskLoopDirective>(
      C, Clauses, AssociatedStmt,
      numLoopChildren(CollapsedNum, OMPD_parallel_master_taskloop), StartLoc,
      EndLoc, CollapsedNum);
  Dir->setIterationVariable(Exprs.IterationVarRef);
  Dir->setLastIteration(Exprs.LastIteration);
  Dir->setCalcLastIteration(Exprs.CalcLastIteration);
  Dir->setPreCond(Exprs.PreCond);
  Dir->setCond(Exprs.Cond);
  Dir->setInit(Exprs.Init);
  Dir->setInc(Exprs.Inc);
  Dir->setIsLastIterVariable(Exprs.IL);
  Dir->setLowerBoundVariable(Exprs.LB);
  Dir->setUpperBoundVariable(Exprs.UB);
  Dir->setStrideVariable(Exprs.ST);
  Dir->setEnsureUpperBound(Exprs.EUB);
  Dir->setNextLowerBound(Exprs.NLB);
  Dir->setNextUpperBound(Exprs.NUB);
  Dir->setNumIterations(Exprs.NumIterations);
  Dir->setCounters(Exprs.Counters);
  Dir->setPrivateCounters(Exprs.PrivateCounters);
  Dir->setInits(Exprs.Inits);
  Dir->setUpdates(Exprs.Updates);
  Dir->setFinals(Exprs.Finals);
  Dir->setDependentCounters(Exprs.DependentCounters);
  Dir->setDependentInits(Exprs.DependentInits);
  Dir->setFinalsConditions(Exprs.FinalsConditions);
  Dir->setPreInits(Exprs.PreInits);
  Dir->setHasCancel(HasCancel);
  return Dir;
}

OMPParallelMasterTaskLoopDirective *
OMPParallelMasterTaskLoopDirective::CreateEmpty(const ASTContext &C,
                                                unsigned NumClauses,
                                                unsigned CollapsedNum,
                                                EmptyShell) {
  return createEmptyDirective<OMPParallelMasterTaskLoopDirective>(
      C, NumClauses, /*HasAssociatedStmt=*/true,
      numLoopChildren(CollapsedNum, OMPD_parallel_master_taskloop),
      CollapsedNum);
}

OMPParallelMaskedTaskLoopDirective *OMPParallelMaskedTaskLoopDirective::Create(
    const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
    unsigned CollapsedNum, ArrayRef<OMPClause *> Clauses, Stmt *AssociatedStmt,
    const HelperExprs &Exprs, bool HasCancel) {
  auto *Dir = createDirective<OMPParallelMaskedTaskLoopDirective>(
      C, Clauses, AssociatedStmt,
      numLoopChildren(CollapsedNum, OMPD_parallel_masked_taskloop), StartLoc,
      EndLoc, CollapsedNum);
  Dir->setIterationVariable(Exprs.IterationVarRef);
  Dir->setLastIteration(Exprs.LastIteration);
  Dir->setCalcLastIteration(Exprs.CalcLastIteration);
  Dir->setPreCond(Exprs.PreCond);
  Dir->setCond(Exprs.Cond);
  Dir->setInit(Exprs.Init);
  Dir->setInc(Exprs.Inc);
  Dir->setIsLastIterVariable(Exprs.IL);
  Dir->setLowerBoundVariable(Exprs.LB);
  Dir->setUpperBoundVariable(Exprs.UB);
  Dir->setStrideVariable(Exprs.ST);
  Dir->setEnsureUpperBound(Exprs.EUB);
  Dir->setNextLowerBound(Exprs.NLB);
  Dir->setNextUpperBound(Exprs.NUB);
  Dir->setNumIterations(Exprs.NumIterations);
  Dir->setCounters(Exprs.Counters);
  Dir->setPrivateCounters(Exprs.PrivateCounters);
  Dir->setInits(Exprs.Inits);
  Dir->setUpdates(Exprs.Updates);
  Dir->setFinals(Exprs.Finals);
  Dir->setDependentCounters(Exprs.DependentCounters);
  Dir->setDependentInits(Exprs.DependentInits);
  Dir->setFinalsConditions(Exprs.FinalsConditions);
  Dir->setPreInits(Exprs.PreInits);
  Dir->setHasCancel(HasCancel);
  return Dir;
}

OMPParallelMaskedTaskLoopDirective *
OMPParallelMaskedTaskLoopDirective::CreateEmpty(const ASTContext &C,
                                                unsigned NumClauses,
                                                unsigned CollapsedNum,
                                                EmptyShell) {
  return createEmptyDirective<OMPParallelMaskedTaskLoopDirective>(
      C, NumClauses, /*HasAssociatedStmt=*/true,
      numLoopChildren(CollapsedNum, OMPD_parallel_masked_taskloop),
      CollapsedNum);
}

OMPParallelMasterTaskLoopSimdDirective *
OMPParallelMasterTaskLoopSimdDirective::Create(
    const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
    unsigned CollapsedNum, ArrayRef<OMPClause *> Clauses, Stmt *AssociatedStmt,
    const HelperExprs &Exprs) {
  auto *Dir = createDirective<OMPParallelMasterTaskLoopSimdDirective>(
      C, Clauses, AssociatedStmt,
      numLoopChildren(CollapsedNum, OMPD_parallel_master_taskloop_simd),
      StartLoc, EndLoc, CollapsedNum);
  Dir->setIterationVariable(Exprs.IterationVarRef);
  Dir->setLastIteration(Exprs.LastIteration);
  Dir->setCalcLastIteration(Exprs.CalcLastIteration);
  Dir->setPreCond(Exprs.PreCond);
  Dir->setCond(Exprs.Cond);
  Dir->setInit(Exprs.Init);
  Dir->setInc(Exprs.Inc);
  Dir->setIsLastIterVariable(Exprs.IL);
  Dir->setLowerBoundVariable(Exprs.LB);
  Dir->setUpperBoundVariable(Exprs.UB);
  Dir->setStrideVariable(Exprs.ST);
  Dir->setEnsureUpperBound(Exprs.EUB);
  Dir->setNextLowerBound(Exprs.NLB);
  Dir->setNextUpperBound(Exprs.NUB);
  Dir->setNumIterations(Exprs.NumIterations);
  Dir->setCounters(Exprs.Counters);
  Dir->setPrivateCounters(Exprs.PrivateCounters);
  Dir->setInits(Exprs.Inits);
  Dir->setUpdates(Exprs.Updates);
  Dir->setFinals(Exprs.Finals);
  Dir->setDependentCounters(Exprs.DependentCounters);
  Dir->setDependentInits(Exprs.DependentInits);
  Dir->setFinalsConditions(Exprs.FinalsConditions);
  Dir->setPreInits(Exprs.PreInits);
  return Dir;
}

OMPParallelMasterTaskLoopSimdDirective *
OMPParallelMasterTaskLoopSimdDirective::CreateEmpty(const ASTContext &C,
                                                    unsigned NumClauses,
                                                    unsigned CollapsedNum,
                                                    EmptyShell) {
  return createEmptyDirective<OMPParallelMasterTaskLoopSimdDirective>(
      C, NumClauses, /*HasAssociatedStmt=*/true,
      numLoopChildren(CollapsedNum, OMPD_parallel_master_taskloop_simd),
      CollapsedNum);
}

OMPParallelMaskedTaskLoopSimdDirective *
OMPParallelMaskedTaskLoopSimdDirective::Create(
    const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
    unsigned CollapsedNum, ArrayRef<OMPClause *> Clauses, Stmt *AssociatedStmt,
    const HelperExprs &Exprs) {
  auto *Dir = createDirective<OMPParallelMaskedTaskLoopSimdDirective>(
      C, Clauses, AssociatedStmt,
      numLoopChildren(CollapsedNum, OMPD_parallel_masked_taskloop_simd),
      StartLoc, EndLoc, CollapsedNum);
  Dir->setIterationVariable(Exprs.IterationVarRef);
  Dir->setLastIteration(Exprs.LastIteration);
  Dir->setCalcLastIteration(Exprs.CalcLastIteration);
  Dir->setPreCond(Exprs.PreCond);
  Dir->setCond(Exprs.Cond);
  Dir->setInit(Exprs.Init);
  Dir->setInc(Exprs.Inc);
  Dir->setIsLastIterVariable(Exprs.IL);
  Dir->setLowerBoundVariable(Exprs.LB);
  Dir->setUpperBoundVariable(Exprs.UB);
  Dir->setStrideVariable(Exprs.ST);
  Dir->setEnsureUpperBound(Exprs.EUB);
  Dir->setNextLowerBound(Exprs.NLB);
  Dir->setNextUpperBound(Exprs.NUB);
  Dir->setNumIterations(Exprs.NumIterations);
  Dir->setCounters(Exprs.Counters);
  Dir->setPrivateCounters(Exprs.PrivateCounters);
  Dir->setInits(Exprs.Inits);
  Dir->setUpdates(Exprs.Updates);
  Dir->setFinals(Exprs.Finals);
  Dir->setDependentCounters(Exprs.DependentCounters);
  Dir->setDependentInits(Exprs.DependentInits);
  Dir->setFinalsConditions(Exprs.FinalsConditions);
  Dir->setPreInits(Exprs.PreInits);
  return Dir;
}

OMPParallelMaskedTaskLoopSimdDirective *
OMPParallelMaskedTaskLoopSimdDirective::CreateEmpty(const ASTContext &C,
                                                    unsigned NumClauses,
                                                    unsigned CollapsedNum,
                                                    EmptyShell) {
  return createEmptyDirective<OMPParallelMaskedTaskLoopSimdDirective>(
      C, NumClauses, /*HasAssociatedStmt=*/true,
      numLoopChildren(CollapsedNum, OMPD_parallel_masked_taskloop_simd),
      CollapsedNum);
}

OMPDistributeDirective *OMPDistributeDirective::Create(
    const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
    unsigned CollapsedNum, ArrayRef<OMPClause *> Clauses, Stmt *AssociatedStmt,
    const HelperExprs &Exprs, OpenMPDirectiveKind ParamPrevMappedDirective) {
  auto *Dir = createDirective<OMPDistributeDirective>(
      C, Clauses, AssociatedStmt,
      numLoopChildren(CollapsedNum, OMPD_distribute), StartLoc, EndLoc,
      CollapsedNum);
  Dir->setIterationVariable(Exprs.IterationVarRef);
  Dir->setLastIteration(Exprs.LastIteration);
  Dir->setCalcLastIteration(Exprs.CalcLastIteration);
  Dir->setPreCond(Exprs.PreCond);
  Dir->setCond(Exprs.Cond);
  Dir->setInit(Exprs.Init);
  Dir->setInc(Exprs.Inc);
  Dir->setIsLastIterVariable(Exprs.IL);
  Dir->setLowerBoundVariable(Exprs.LB);
  Dir->setUpperBoundVariable(Exprs.UB);
  Dir->setStrideVariable(Exprs.ST);
  Dir->setEnsureUpperBound(Exprs.EUB);
  Dir->setNextLowerBound(Exprs.NLB);
  Dir->setNextUpperBound(Exprs.NUB);
  Dir->setNumIterations(Exprs.NumIterations);
  Dir->setCounters(Exprs.Counters);
  Dir->setPrivateCounters(Exprs.PrivateCounters);
  Dir->setInits(Exprs.Inits);
  Dir->setUpdates(Exprs.Updates);
  Dir->setFinals(Exprs.Finals);
  Dir->setDependentCounters(Exprs.DependentCounters);
  Dir->setDependentInits(Exprs.DependentInits);
  Dir->setFinalsConditions(Exprs.FinalsConditions);
  Dir->setPreInits(Exprs.PreInits);
  Dir->setMappedDirective(ParamPrevMappedDirective);
  return Dir;
}

OMPDistributeDirective *
OMPDistributeDirective::CreateEmpty(const ASTContext &C, unsigned NumClauses,
                                    unsigned CollapsedNum, EmptyShell) {
  return createEmptyDirective<OMPDistributeDirective>(
      C, NumClauses, /*HasAssociatedStmt=*/true,
      numLoopChildren(CollapsedNum, OMPD_distribute), CollapsedNum);
}

OMPTargetUpdateDirective *OMPTargetUpdateDirective::Create(
    const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
    ArrayRef<OMPClause *> Clauses, Stmt *AssociatedStmt) {
  return createDirective<OMPTargetUpdateDirective>(C, Clauses, AssociatedStmt,
                                                   /*NumChildren=*/0, StartLoc,
                                                   EndLoc);
}

OMPTargetUpdateDirective *
OMPTargetUpdateDirective::CreateEmpty(const ASTContext &C, unsigned NumClauses,
                                      EmptyShell) {
  return createEmptyDirective<OMPTargetUpdateDirective>(
      C, NumClauses, /*HasAssociatedStmt=*/true);
}

OMPDistributeParallelForDirective *OMPDistributeParallelForDirective::Create(
    const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
    unsigned CollapsedNum, ArrayRef<OMPClause *> Clauses, Stmt *AssociatedStmt,
    const HelperExprs &Exprs, Expr *TaskRedRef, bool HasCancel) {
  auto *Dir = createDirective<OMPDistributeParallelForDirective>(
      C, Clauses, AssociatedStmt,
      numLoopChildren(CollapsedNum, OMPD_distribute_parallel_for) + 1, StartLoc,
      EndLoc, CollapsedNum);
  Dir->setIterationVariable(Exprs.IterationVarRef);
  Dir->setLastIteration(Exprs.LastIteration);
  Dir->setCalcLastIteration(Exprs.CalcLastIteration);
  Dir->setPreCond(Exprs.PreCond);
  Dir->setCond(Exprs.Cond);
  Dir->setInit(Exprs.Init);
  Dir->setInc(Exprs.Inc);
  Dir->setIsLastIterVariable(Exprs.IL);
  Dir->setLowerBoundVariable(Exprs.LB);
  Dir->setUpperBoundVariable(Exprs.UB);
  Dir->setStrideVariable(Exprs.ST);
  Dir->setEnsureUpperBound(Exprs.EUB);
  Dir->setNextLowerBound(Exprs.NLB);
  Dir->setNextUpperBound(Exprs.NUB);
  Dir->setNumIterations(Exprs.NumIterations);
  Dir->setPrevLowerBoundVariable(Exprs.PrevLB);
  Dir->setPrevUpperBoundVariable(Exprs.PrevUB);
  Dir->setDistInc(Exprs.DistInc);
  Dir->setPrevEnsureUpperBound(Exprs.PrevEUB);
  Dir->setCounters(Exprs.Counters);
  Dir->setPrivateCounters(Exprs.PrivateCounters);
  Dir->setInits(Exprs.Inits);
  Dir->setUpdates(Exprs.Updates);
  Dir->setFinals(Exprs.Finals);
  Dir->setDependentCounters(Exprs.DependentCounters);
  Dir->setDependentInits(Exprs.DependentInits);
  Dir->setFinalsConditions(Exprs.FinalsConditions);
  Dir->setPreInits(Exprs.PreInits);
  Dir->setCombinedLowerBoundVariable(Exprs.DistCombinedFields.LB);
  Dir->setCombinedUpperBoundVariable(Exprs.DistCombinedFields.UB);
  Dir->setCombinedEnsureUpperBound(Exprs.DistCombinedFields.EUB);
  Dir->setCombinedInit(Exprs.DistCombinedFields.Init);
  Dir->setCombinedCond(Exprs.DistCombinedFields.Cond);
  Dir->setCombinedNextLowerBound(Exprs.DistCombinedFields.NLB);
  Dir->setCombinedNextUpperBound(Exprs.DistCombinedFields.NUB);
  Dir->setCombinedDistCond(Exprs.DistCombinedFields.DistCond);
  Dir->setCombinedParForInDistCond(Exprs.DistCombinedFields.ParForInDistCond);
  Dir->setTaskReductionRefExpr(TaskRedRef);
  Dir->HasCancel = HasCancel;
  return Dir;
}

OMPDistributeParallelForDirective *
OMPDistributeParallelForDirective::CreateEmpty(const ASTContext &C,
                                               unsigned NumClauses,
                                               unsigned CollapsedNum,
                                               EmptyShell) {
  return createEmptyDirective<OMPDistributeParallelForDirective>(
      C, NumClauses, /*HasAssociatedStmt=*/true,
      numLoopChildren(CollapsedNum, OMPD_distribute_parallel_for) + 1,
      CollapsedNum);
}

OMPDistributeParallelForSimdDirective *
OMPDistributeParallelForSimdDirective::Create(
    const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
    unsigned CollapsedNum, ArrayRef<OMPClause *> Clauses, Stmt *AssociatedStmt,
    const HelperExprs &Exprs) {
  auto *Dir = createDirective<OMPDistributeParallelForSimdDirective>(
      C, Clauses, AssociatedStmt,
      numLoopChildren(CollapsedNum, OMPD_distribute_parallel_for_simd),
      StartLoc, EndLoc, CollapsedNum);
  Dir->setIterationVariable(Exprs.IterationVarRef);
  Dir->setLastIteration(Exprs.LastIteration);
  Dir->setCalcLastIteration(Exprs.CalcLastIteration);
  Dir->setPreCond(Exprs.PreCond);
  Dir->setCond(Exprs.Cond);
  Dir->setInit(Exprs.Init);
  Dir->setInc(Exprs.Inc);
  Dir->setIsLastIterVariable(Exprs.IL);
  Dir->setLowerBoundVariable(Exprs.LB);
  Dir->setUpperBoundVariable(Exprs.UB);
  Dir->setStrideVariable(Exprs.ST);
  Dir->setEnsureUpperBound(Exprs.EUB);
  Dir->setNextLowerBound(Exprs.NLB);
  Dir->setNextUpperBound(Exprs.NUB);
  Dir->setNumIterations(Exprs.NumIterations);
  Dir->setPrevLowerBoundVariable(Exprs.PrevLB);
  Dir->setPrevUpperBoundVariable(Exprs.PrevUB);
  Dir->setDistInc(Exprs.DistInc);
  Dir->setPrevEnsureUpperBound(Exprs.PrevEUB);
  Dir->setCounters(Exprs.Counters);
  Dir->setPrivateCounters(Exprs.PrivateCounters);
  Dir->setInits(Exprs.Inits);
  Dir->setUpdates(Exprs.Updates);
  Dir->setFinals(Exprs.Finals);
  Dir->setDependentCounters(Exprs.DependentCounters);
  Dir->setDependentInits(Exprs.DependentInits);
  Dir->setFinalsConditions(Exprs.FinalsConditions);
  Dir->setPreInits(Exprs.PreInits);
  Dir->setCombinedLowerBoundVariable(Exprs.DistCombinedFields.LB);
  Dir->setCombinedUpperBoundVariable(Exprs.DistCombinedFields.UB);
  Dir->setCombinedEnsureUpperBound(Exprs.DistCombinedFields.EUB);
  Dir->setCombinedInit(Exprs.DistCombinedFields.Init);
  Dir->setCombinedCond(Exprs.DistCombinedFields.Cond);
  Dir->setCombinedNextLowerBound(Exprs.DistCombinedFields.NLB);
  Dir->setCombinedNextUpperBound(Exprs.DistCombinedFields.NUB);
  Dir->setCombinedDistCond(Exprs.DistCombinedFields.DistCond);
  Dir->setCombinedParForInDistCond(Exprs.DistCombinedFields.ParForInDistCond);
  return Dir;
}

OMPDistributeParallelForSimdDirective *
OMPDistributeParallelForSimdDirective::CreateEmpty(const ASTContext &C,
                                                   unsigned NumClauses,
                                                   unsigned CollapsedNum,
                                                   EmptyShell) {
  return createEmptyDirective<OMPDistributeParallelForSimdDirective>(
      C, NumClauses, /*HasAssociatedStmt=*/true,
      numLoopChildren(CollapsedNum, OMPD_distribute_parallel_for_simd),
      CollapsedNum);
}

OMPDistributeSimdDirective *OMPDistributeSimdDirective::Create(
    const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
    unsigned CollapsedNum, ArrayRef<OMPClause *> Clauses, Stmt *AssociatedStmt,
    const HelperExprs &Exprs) {
  auto *Dir = createDirective<OMPDistributeSimdDirective>(
      C, Clauses, AssociatedStmt,
      numLoopChildren(CollapsedNum, OMPD_distribute_simd), StartLoc, EndLoc,
      CollapsedNum);
  Dir->setIterationVariable(Exprs.IterationVarRef);
  Dir->setLastIteration(Exprs.LastIteration);
  Dir->setCalcLastIteration(Exprs.CalcLastIteration);
  Dir->setPreCond(Exprs.PreCond);
  Dir->setCond(Exprs.Cond);
  Dir->setInit(Exprs.Init);
  Dir->setInc(Exprs.Inc);
  Dir->setIsLastIterVariable(Exprs.IL);
  Dir->setLowerBoundVariable(Exprs.LB);
  Dir->setUpperBoundVariable(Exprs.UB);
  Dir->setStrideVariable(Exprs.ST);
  Dir->setEnsureUpperBound(Exprs.EUB);
  Dir->setNextLowerBound(Exprs.NLB);
  Dir->setNextUpperBound(Exprs.NUB);
  Dir->setNumIterations(Exprs.NumIterations);
  Dir->setCounters(Exprs.Counters);
  Dir->setPrivateCounters(Exprs.PrivateCounters);
  Dir->setInits(Exprs.Inits);
  Dir->setUpdates(Exprs.Updates);
  Dir->setFinals(Exprs.Finals);
  Dir->setDependentCounters(Exprs.DependentCounters);
  Dir->setDependentInits(Exprs.DependentInits);
  Dir->setFinalsConditions(Exprs.FinalsConditions);
  Dir->setPreInits(Exprs.PreInits);
  return Dir;
}

OMPDistributeSimdDirective *
OMPDistributeSimdDirective::CreateEmpty(const ASTContext &C,
                                        unsigned NumClauses,
                                        unsigned CollapsedNum, EmptyShell) {
  return createEmptyDirective<OMPDistributeSimdDirective>(
      C, NumClauses, /*HasAssociatedStmt=*/true,
      numLoopChildren(CollapsedNum, OMPD_distribute_simd), CollapsedNum);
}

OMPTargetParallelForSimdDirective *OMPTargetParallelForSimdDirective::Create(
    const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
    unsigned CollapsedNum, ArrayRef<OMPClause *> Clauses, Stmt *AssociatedStmt,
    const HelperExprs &Exprs) {
  auto *Dir = createDirective<OMPTargetParallelForSimdDirective>(
      C, Clauses, AssociatedStmt,
      numLoopChildren(CollapsedNum, OMPD_target_parallel_for_simd), StartLoc,
      EndLoc, CollapsedNum);
  Dir->setIterationVariable(Exprs.IterationVarRef);
  Dir->setLastIteration(Exprs.LastIteration);
  Dir->setCalcLastIteration(Exprs.CalcLastIteration);
  Dir->setPreCond(Exprs.PreCond);
  Dir->setCond(Exprs.Cond);
  Dir->setInit(Exprs.Init);
  Dir->setInc(Exprs.Inc);
  Dir->setIsLastIterVariable(Exprs.IL);
  Dir->setLowerBoundVariable(Exprs.LB);
  Dir->setUpperBoundVariable(Exprs.UB);
  Dir->setStrideVariable(Exprs.ST);
  Dir->setEnsureUpperBound(Exprs.EUB);
  Dir->setNextLowerBound(Exprs.NLB);
  Dir->setNextUpperBound(Exprs.NUB);
  Dir->setNumIterations(Exprs.NumIterations);
  Dir->setCounters(Exprs.Counters);
  Dir->setPrivateCounters(Exprs.PrivateCounters);
  Dir->setInits(Exprs.Inits);
  Dir->setUpdates(Exprs.Updates);
  Dir->setFinals(Exprs.Finals);
  Dir->setDependentCounters(Exprs.DependentCounters);
  Dir->setDependentInits(Exprs.DependentInits);
  Dir->setFinalsConditions(Exprs.FinalsConditions);
  Dir->setPreInits(Exprs.PreInits);
  return Dir;
}

OMPTargetParallelForSimdDirective *
OMPTargetParallelForSimdDirective::CreateEmpty(const ASTContext &C,
                                               unsigned NumClauses,
                                               unsigned CollapsedNum,
                                               EmptyShell) {
  return createEmptyDirective<OMPTargetParallelForSimdDirective>(
      C, NumClauses, /*HasAssociatedStmt=*/true,
      numLoopChildren(CollapsedNum, OMPD_target_parallel_for_simd),
      CollapsedNum);
}

OMPTargetSimdDirective *
OMPTargetSimdDirective::Create(const ASTContext &C, SourceLocation StartLoc,
                               SourceLocation EndLoc, unsigned CollapsedNum,
                               ArrayRef<OMPClause *> Clauses,
                               Stmt *AssociatedStmt, const HelperExprs &Exprs) {
  auto *Dir = createDirective<OMPTargetSimdDirective>(
      C, Clauses, AssociatedStmt,
      numLoopChildren(CollapsedNum, OMPD_target_simd), StartLoc, EndLoc,
      CollapsedNum);
  Dir->setIterationVariable(Exprs.IterationVarRef);
  Dir->setLastIteration(Exprs.LastIteration);
  Dir->setCalcLastIteration(Exprs.CalcLastIteration);
  Dir->setPreCond(Exprs.PreCond);
  Dir->setCond(Exprs.Cond);
  Dir->setInit(Exprs.Init);
  Dir->setInc(Exprs.Inc);
  Dir->setCounters(Exprs.Counters);
  Dir->setPrivateCounters(Exprs.PrivateCounters);
  Dir->setInits(Exprs.Inits);
  Dir->setUpdates(Exprs.Updates);
  Dir->setFinals(Exprs.Finals);
  Dir->setDependentCounters(Exprs.DependentCounters);
  Dir->setDependentInits(Exprs.DependentInits);
  Dir->setFinalsConditions(Exprs.FinalsConditions);
  Dir->setPreInits(Exprs.PreInits);
  return Dir;
}

OMPTargetSimdDirective *
OMPTargetSimdDirective::CreateEmpty(const ASTContext &C, unsigned NumClauses,
                                    unsigned CollapsedNum, EmptyShell) {
  return createEmptyDirective<OMPTargetSimdDirective>(
      C, NumClauses, /*HasAssociatedStmt=*/true,
      numLoopChildren(CollapsedNum, OMPD_target_simd), CollapsedNum);
}

OMPTeamsDistributeDirective *OMPTeamsDistributeDirective::Create(
    const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
    unsigned CollapsedNum, ArrayRef<OMPClause *> Clauses, Stmt *AssociatedStmt,
    const HelperExprs &Exprs) {
  auto *Dir = createDirective<OMPTeamsDistributeDirective>(
      C, Clauses, AssociatedStmt,
      numLoopChildren(CollapsedNum, OMPD_teams_distribute), StartLoc, EndLoc,
      CollapsedNum);
  Dir->setIterationVariable(Exprs.IterationVarRef);
  Dir->setLastIteration(Exprs.LastIteration);
  Dir->setCalcLastIteration(Exprs.CalcLastIteration);
  Dir->setPreCond(Exprs.PreCond);
  Dir->setCond(Exprs.Cond);
  Dir->setInit(Exprs.Init);
  Dir->setInc(Exprs.Inc);
  Dir->setIsLastIterVariable(Exprs.IL);
  Dir->setLowerBoundVariable(Exprs.LB);
  Dir->setUpperBoundVariable(Exprs.UB);
  Dir->setStrideVariable(Exprs.ST);
  Dir->setEnsureUpperBound(Exprs.EUB);
  Dir->setNextLowerBound(Exprs.NLB);
  Dir->setNextUpperBound(Exprs.NUB);
  Dir->setNumIterations(Exprs.NumIterations);
  Dir->setCounters(Exprs.Counters);
  Dir->setPrivateCounters(Exprs.PrivateCounters);
  Dir->setInits(Exprs.Inits);
  Dir->setUpdates(Exprs.Updates);
  Dir->setFinals(Exprs.Finals);
  Dir->setDependentCounters(Exprs.DependentCounters);
  Dir->setDependentInits(Exprs.DependentInits);
  Dir->setFinalsConditions(Exprs.FinalsConditions);
  Dir->setPreInits(Exprs.PreInits);
  return Dir;
}

OMPTeamsDistributeDirective *
OMPTeamsDistributeDirective::CreateEmpty(const ASTContext &C,
                                         unsigned NumClauses,
                                         unsigned CollapsedNum, EmptyShell) {
  return createEmptyDirective<OMPTeamsDistributeDirective>(
      C, NumClauses, /*HasAssociatedStmt=*/true,
      numLoopChildren(CollapsedNum, OMPD_teams_distribute), CollapsedNum);
}

OMPTeamsDistributeSimdDirective *OMPTeamsDistributeSimdDirective::Create(
    const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
    unsigned CollapsedNum, ArrayRef<OMPClause *> Clauses, Stmt *AssociatedStmt,
    const HelperExprs &Exprs) {
  auto *Dir = createDirective<OMPTeamsDistributeSimdDirective>(
      C, Clauses, AssociatedStmt,
      numLoopChildren(CollapsedNum, OMPD_teams_distribute_simd), StartLoc,
      EndLoc, CollapsedNum);
  Dir->setIterationVariable(Exprs.IterationVarRef);
  Dir->setLastIteration(Exprs.LastIteration);
  Dir->setCalcLastIteration(Exprs.CalcLastIteration);
  Dir->setPreCond(Exprs.PreCond);
  Dir->setCond(Exprs.Cond);
  Dir->setInit(Exprs.Init);
  Dir->setInc(Exprs.Inc);
  Dir->setIsLastIterVariable(Exprs.IL);
  Dir->setLowerBoundVariable(Exprs.LB);
  Dir->setUpperBoundVariable(Exprs.UB);
  Dir->setStrideVariable(Exprs.ST);
  Dir->setEnsureUpperBound(Exprs.EUB);
  Dir->setNextLowerBound(Exprs.NLB);
  Dir->setNextUpperBound(Exprs.NUB);
  Dir->setNumIterations(Exprs.NumIterations);
  Dir->setCounters(Exprs.Counters);
  Dir->setPrivateCounters(Exprs.PrivateCounters);
  Dir->setInits(Exprs.Inits);
  Dir->setUpdates(Exprs.Updates);
  Dir->setFinals(Exprs.Finals);
  Dir->setDependentCounters(Exprs.DependentCounters);
  Dir->setDependentInits(Exprs.DependentInits);
  Dir->setFinalsConditions(Exprs.FinalsConditions);
  Dir->setPreInits(Exprs.PreInits);
  return Dir;
}

OMPTeamsDistributeSimdDirective *OMPTeamsDistributeSimdDirective::CreateEmpty(
    const ASTContext &C, unsigned NumClauses, unsigned CollapsedNum,
    EmptyShell) {
  return createEmptyDirective<OMPTeamsDistributeSimdDirective>(
      C, NumClauses, /*HasAssociatedStmt=*/true,
      numLoopChildren(CollapsedNum, OMPD_teams_distribute_simd), CollapsedNum);
}

OMPTeamsDistributeParallelForSimdDirective *
OMPTeamsDistributeParallelForSimdDirective::Create(
    const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
    unsigned CollapsedNum, ArrayRef<OMPClause *> Clauses, Stmt *AssociatedStmt,
    const HelperExprs &Exprs) {
  auto *Dir = createDirective<OMPTeamsDistributeParallelForSimdDirective>(
      C, Clauses, AssociatedStmt,
      numLoopChildren(CollapsedNum, OMPD_teams_distribute_parallel_for_simd),
      StartLoc, EndLoc, CollapsedNum);
  Dir->setIterationVariable(Exprs.IterationVarRef);
  Dir->setLastIteration(Exprs.LastIteration);
  Dir->setCalcLastIteration(Exprs.CalcLastIteration);
  Dir->setPreCond(Exprs.PreCond);
  Dir->setCond(Exprs.Cond);
  Dir->setInit(Exprs.Init);
  Dir->setInc(Exprs.Inc);
  Dir->setIsLastIterVariable(Exprs.IL);
  Dir->setLowerBoundVariable(Exprs.LB);
  Dir->setUpperBoundVariable(Exprs.UB);
  Dir->setStrideVariable(Exprs.ST);
  Dir->setEnsureUpperBound(Exprs.EUB);
  Dir->setNextLowerBound(Exprs.NLB);
  Dir->setNextUpperBound(Exprs.NUB);
  Dir->setNumIterations(Exprs.NumIterations);
  Dir->setPrevLowerBoundVariable(Exprs.PrevLB);
  Dir->setPrevUpperBoundVariable(Exprs.PrevUB);
  Dir->setDistInc(Exprs.DistInc);
  Dir->setPrevEnsureUpperBound(Exprs.PrevEUB);
  Dir->setCounters(Exprs.Counters);
  Dir->setPrivateCounters(Exprs.PrivateCounters);
  Dir->setInits(Exprs.Inits);
  Dir->setUpdates(Exprs.Updates);
  Dir->setFinals(Exprs.Finals);
  Dir->setDependentCounters(Exprs.DependentCounters);
  Dir->setDependentInits(Exprs.DependentInits);
  Dir->setFinalsConditions(Exprs.FinalsConditions);
  Dir->setPreInits(Exprs.PreInits);
  Dir->setCombinedLowerBoundVariable(Exprs.DistCombinedFields.LB);
  Dir->setCombinedUpperBoundVariable(Exprs.DistCombinedFields.UB);
  Dir->setCombinedEnsureUpperBound(Exprs.DistCombinedFields.EUB);
  Dir->setCombinedInit(Exprs.DistCombinedFields.Init);
  Dir->setCombinedCond(Exprs.DistCombinedFields.Cond);
  Dir->setCombinedNextLowerBound(Exprs.DistCombinedFields.NLB);
  Dir->setCombinedNextUpperBound(Exprs.DistCombinedFields.NUB);
  Dir->setCombinedDistCond(Exprs.DistCombinedFields.DistCond);
  Dir->setCombinedParForInDistCond(Exprs.DistCombinedFields.ParForInDistCond);
  return Dir;
}

OMPTeamsDistributeParallelForSimdDirective *
OMPTeamsDistributeParallelForSimdDirective::CreateEmpty(const ASTContext &C,
                                                        unsigned NumClauses,
                                                        unsigned CollapsedNum,
                                                        EmptyShell) {
  return createEmptyDirective<OMPTeamsDistributeParallelForSimdDirective>(
      C, NumClauses, /*HasAssociatedStmt=*/true,
      numLoopChildren(CollapsedNum, OMPD_teams_distribute_parallel_for_simd),
      CollapsedNum);
}

OMPTeamsDistributeParallelForDirective *
OMPTeamsDistributeParallelForDirective::Create(
    const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
    unsigned CollapsedNum, ArrayRef<OMPClause *> Clauses, Stmt *AssociatedStmt,
    const HelperExprs &Exprs, Expr *TaskRedRef, bool HasCancel) {
  auto *Dir = createDirective<OMPTeamsDistributeParallelForDirective>(
      C, Clauses, AssociatedStmt,
      numLoopChildren(CollapsedNum, OMPD_teams_distribute_parallel_for) + 1,
      StartLoc, EndLoc, CollapsedNum);
  Dir->setIterationVariable(Exprs.IterationVarRef);
  Dir->setLastIteration(Exprs.LastIteration);
  Dir->setCalcLastIteration(Exprs.CalcLastIteration);
  Dir->setPreCond(Exprs.PreCond);
  Dir->setCond(Exprs.Cond);
  Dir->setInit(Exprs.Init);
  Dir->setInc(Exprs.Inc);
  Dir->setIsLastIterVariable(Exprs.IL);
  Dir->setLowerBoundVariable(Exprs.LB);
  Dir->setUpperBoundVariable(Exprs.UB);
  Dir->setStrideVariable(Exprs.ST);
  Dir->setEnsureUpperBound(Exprs.EUB);
  Dir->setNextLowerBound(Exprs.NLB);
  Dir->setNextUpperBound(Exprs.NUB);
  Dir->setNumIterations(Exprs.NumIterations);
  Dir->setPrevLowerBoundVariable(Exprs.PrevLB);
  Dir->setPrevUpperBoundVariable(Exprs.PrevUB);
  Dir->setDistInc(Exprs.DistInc);
  Dir->setPrevEnsureUpperBound(Exprs.PrevEUB);
  Dir->setCounters(Exprs.Counters);
  Dir->setPrivateCounters(Exprs.PrivateCounters);
  Dir->setInits(Exprs.Inits);
  Dir->setUpdates(Exprs.Updates);
  Dir->setFinals(Exprs.Finals);
  Dir->setDependentCounters(Exprs.DependentCounters);
  Dir->setDependentInits(Exprs.DependentInits);
  Dir->setFinalsConditions(Exprs.FinalsConditions);
  Dir->setPreInits(Exprs.PreInits);
  Dir->setCombinedLowerBoundVariable(Exprs.DistCombinedFields.LB);
  Dir->setCombinedUpperBoundVariable(Exprs.DistCombinedFields.UB);
  Dir->setCombinedEnsureUpperBound(Exprs.DistCombinedFields.EUB);
  Dir->setCombinedInit(Exprs.DistCombinedFields.Init);
  Dir->setCombinedCond(Exprs.DistCombinedFields.Cond);
  Dir->setCombinedNextLowerBound(Exprs.DistCombinedFields.NLB);
  Dir->setCombinedNextUpperBound(Exprs.DistCombinedFields.NUB);
  Dir->setCombinedDistCond(Exprs.DistCombinedFields.DistCond);
  Dir->setCombinedParForInDistCond(Exprs.DistCombinedFields.ParForInDistCond);
  Dir->setTaskReductionRefExpr(TaskRedRef);
  Dir->HasCancel = HasCancel;
  return Dir;
}

OMPTeamsDistributeParallelForDirective *
OMPTeamsDistributeParallelForDirective::CreateEmpty(const ASTContext &C,
                                                    unsigned NumClauses,
                                                    unsigned CollapsedNum,
                                                    EmptyShell) {
  return createEmptyDirective<OMPTeamsDistributeParallelForDirective>(
      C, NumClauses, /*HasAssociatedStmt=*/true,
      numLoopChildren(CollapsedNum, OMPD_teams_distribute_parallel_for) + 1,
      CollapsedNum);
}

OMPTargetTeamsDirective *OMPTargetTeamsDirective::Create(
    const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
    ArrayRef<OMPClause *> Clauses, Stmt *AssociatedStmt) {
  return createDirective<OMPTargetTeamsDirective>(C, Clauses, AssociatedStmt,
                                                  /*NumChildren=*/0, StartLoc,
                                                  EndLoc);
}

OMPTargetTeamsDirective *
OMPTargetTeamsDirective::CreateEmpty(const ASTContext &C, unsigned NumClauses,
                                     EmptyShell) {
  return createEmptyDirective<OMPTargetTeamsDirective>(
      C, NumClauses, /*HasAssociatedStmt=*/true);
}

OMPTargetTeamsDistributeDirective *OMPTargetTeamsDistributeDirective::Create(
    const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
    unsigned CollapsedNum, ArrayRef<OMPClause *> Clauses, Stmt *AssociatedStmt,
    const HelperExprs &Exprs) {
  auto *Dir = createDirective<OMPTargetTeamsDistributeDirective>(
      C, Clauses, AssociatedStmt,
      numLoopChildren(CollapsedNum, OMPD_target_teams_distribute), StartLoc,
      EndLoc, CollapsedNum);
  Dir->setIterationVariable(Exprs.IterationVarRef);
  Dir->setLastIteration(Exprs.LastIteration);
  Dir->setCalcLastIteration(Exprs.CalcLastIteration);
  Dir->setPreCond(Exprs.PreCond);
  Dir->setCond(Exprs.Cond);
  Dir->setInit(Exprs.Init);
  Dir->setInc(Exprs.Inc);
  Dir->setIsLastIterVariable(Exprs.IL);
  Dir->setLowerBoundVariable(Exprs.LB);
  Dir->setUpperBoundVariable(Exprs.UB);
  Dir->setStrideVariable(Exprs.ST);
  Dir->setEnsureUpperBound(Exprs.EUB);
  Dir->setNextLowerBound(Exprs.NLB);
  Dir->setNextUpperBound(Exprs.NUB);
  Dir->setNumIterations(Exprs.NumIterations);
  Dir->setCounters(Exprs.Counters);
  Dir->setPrivateCounters(Exprs.PrivateCounters);
  Dir->setInits(Exprs.Inits);
  Dir->setUpdates(Exprs.Updates);
  Dir->setFinals(Exprs.Finals);
  Dir->setDependentCounters(Exprs.DependentCounters);
  Dir->setDependentInits(Exprs.DependentInits);
  Dir->setFinalsConditions(Exprs.FinalsConditions);
  Dir->setPreInits(Exprs.PreInits);
  return Dir;
}

OMPTargetTeamsDistributeDirective *
OMPTargetTeamsDistributeDirective::CreateEmpty(const ASTContext &C,
                                               unsigned NumClauses,
                                               unsigned CollapsedNum,
                                               EmptyShell) {
  return createEmptyDirective<OMPTargetTeamsDistributeDirective>(
      C, NumClauses, /*HasAssociatedStmt=*/true,
      numLoopChildren(CollapsedNum, OMPD_target_teams_distribute),
      CollapsedNum);
}

OMPTargetTeamsDistributeParallelForDirective *
OMPTargetTeamsDistributeParallelForDirective::Create(
    const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
    unsigned CollapsedNum, ArrayRef<OMPClause *> Clauses, Stmt *AssociatedStmt,
    const HelperExprs &Exprs, Expr *TaskRedRef, bool HasCancel) {
  auto *Dir = createDirective<OMPTargetTeamsDistributeParallelForDirective>(
      C, Clauses, AssociatedStmt,
      numLoopChildren(CollapsedNum, OMPD_target_teams_distribute_parallel_for) +
          1,
      StartLoc, EndLoc, CollapsedNum);
  Dir->setIterationVariable(Exprs.IterationVarRef);
  Dir->setLastIteration(Exprs.LastIteration);
  Dir->setCalcLastIteration(Exprs.CalcLastIteration);
  Dir->setPreCond(Exprs.PreCond);
  Dir->setCond(Exprs.Cond);
  Dir->setInit(Exprs.Init);
  Dir->setInc(Exprs.Inc);
  Dir->setIsLastIterVariable(Exprs.IL);
  Dir->setLowerBoundVariable(Exprs.LB);
  Dir->setUpperBoundVariable(Exprs.UB);
  Dir->setStrideVariable(Exprs.ST);
  Dir->setEnsureUpperBound(Exprs.EUB);
  Dir->setNextLowerBound(Exprs.NLB);
  Dir->setNextUpperBound(Exprs.NUB);
  Dir->setNumIterations(Exprs.NumIterations);
  Dir->setPrevLowerBoundVariable(Exprs.PrevLB);
  Dir->setPrevUpperBoundVariable(Exprs.PrevUB);
  Dir->setDistInc(Exprs.DistInc);
  Dir->setPrevEnsureUpperBound(Exprs.PrevEUB);
  Dir->setCounters(Exprs.Counters);
  Dir->setPrivateCounters(Exprs.PrivateCounters);
  Dir->setInits(Exprs.Inits);
  Dir->setUpdates(Exprs.Updates);
  Dir->setFinals(Exprs.Finals);
  Dir->setDependentCounters(Exprs.DependentCounters);
  Dir->setDependentInits(Exprs.DependentInits);
  Dir->setFinalsConditions(Exprs.FinalsConditions);
  Dir->setPreInits(Exprs.PreInits);
  Dir->setCombinedLowerBoundVariable(Exprs.DistCombinedFields.LB);
  Dir->setCombinedUpperBoundVariable(Exprs.DistCombinedFields.UB);
  Dir->setCombinedEnsureUpperBound(Exprs.DistCombinedFields.EUB);
  Dir->setCombinedInit(Exprs.DistCombinedFields.Init);
  Dir->setCombinedCond(Exprs.DistCombinedFields.Cond);
  Dir->setCombinedNextLowerBound(Exprs.DistCombinedFields.NLB);
  Dir->setCombinedNextUpperBound(Exprs.DistCombinedFields.NUB);
  Dir->setCombinedDistCond(Exprs.DistCombinedFields.DistCond);
  Dir->setCombinedParForInDistCond(Exprs.DistCombinedFields.ParForInDistCond);
  Dir->setTaskReductionRefExpr(TaskRedRef);
  Dir->HasCancel = HasCancel;
  return Dir;
}

OMPTargetTeamsDistributeParallelForDirective *
OMPTargetTeamsDistributeParallelForDirective::CreateEmpty(const ASTContext &C,
                                                          unsigned NumClauses,
                                                          unsigned CollapsedNum,
                                                          EmptyShell) {
  return createEmptyDirective<OMPTargetTeamsDistributeParallelForDirective>(
      C, NumClauses, /*HasAssociatedStmt=*/true,
      numLoopChildren(CollapsedNum, OMPD_target_teams_distribute_parallel_for) +
          1,
      CollapsedNum);
}

OMPTargetTeamsDistributeParallelForSimdDirective *
OMPTargetTeamsDistributeParallelForSimdDirective::Create(
    const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
    unsigned CollapsedNum, ArrayRef<OMPClause *> Clauses, Stmt *AssociatedStmt,
    const HelperExprs &Exprs) {
  auto *Dir = createDirective<OMPTargetTeamsDistributeParallelForSimdDirective>(
      C, Clauses, AssociatedStmt,
      numLoopChildren(CollapsedNum,
                      OMPD_target_teams_distribute_parallel_for_simd),
      StartLoc, EndLoc, CollapsedNum);
  Dir->setIterationVariable(Exprs.IterationVarRef);
  Dir->setLastIteration(Exprs.LastIteration);
  Dir->setCalcLastIteration(Exprs.CalcLastIteration);
  Dir->setPreCond(Exprs.PreCond);
  Dir->setCond(Exprs.Cond);
  Dir->setInit(Exprs.Init);
  Dir->setInc(Exprs.Inc);
  Dir->setIsLastIterVariable(Exprs.IL);
  Dir->setLowerBoundVariable(Exprs.LB);
  Dir->setUpperBoundVariable(Exprs.UB);
  Dir->setStrideVariable(Exprs.ST);
  Dir->setEnsureUpperBound(Exprs.EUB);
  Dir->setNextLowerBound(Exprs.NLB);
  Dir->setNextUpperBound(Exprs.NUB);
  Dir->setNumIterations(Exprs.NumIterations);
  Dir->setPrevLowerBoundVariable(Exprs.PrevLB);
  Dir->setPrevUpperBoundVariable(Exprs.PrevUB);
  Dir->setDistInc(Exprs.DistInc);
  Dir->setPrevEnsureUpperBound(Exprs.PrevEUB);
  Dir->setCounters(Exprs.Counters);
  Dir->setPrivateCounters(Exprs.PrivateCounters);
  Dir->setInits(Exprs.Inits);
  Dir->setUpdates(Exprs.Updates);
  Dir->setFinals(Exprs.Finals);
  Dir->setDependentCounters(Exprs.DependentCounters);
  Dir->setDependentInits(Exprs.DependentInits);
  Dir->setFinalsConditions(Exprs.FinalsConditions);
  Dir->setPreInits(Exprs.PreInits);
  Dir->setCombinedLowerBoundVariable(Exprs.DistCombinedFields.LB);
  Dir->setCombinedUpperBoundVariable(Exprs.DistCombinedFields.UB);
  Dir->setCombinedEnsureUpperBound(Exprs.DistCombinedFields.EUB);
  Dir->setCombinedInit(Exprs.DistCombinedFields.Init);
  Dir->setCombinedCond(Exprs.DistCombinedFields.Cond);
  Dir->setCombinedNextLowerBound(Exprs.DistCombinedFields.NLB);
  Dir->setCombinedNextUpperBound(Exprs.DistCombinedFields.NUB);
  Dir->setCombinedDistCond(Exprs.DistCombinedFields.DistCond);
  Dir->setCombinedParForInDistCond(Exprs.DistCombinedFields.ParForInDistCond);
  return Dir;
}

OMPTargetTeamsDistributeParallelForSimdDirective *
OMPTargetTeamsDistributeParallelForSimdDirective::CreateEmpty(
    const ASTContext &C, unsigned NumClauses, unsigned CollapsedNum,
    EmptyShell) {
  return createEmptyDirective<OMPTargetTeamsDistributeParallelForSimdDirective>(
      C, NumClauses, /*HasAssociatedStmt=*/true,
      numLoopChildren(CollapsedNum,
                      OMPD_target_teams_distribute_parallel_for_simd),
      CollapsedNum);
}

OMPTargetTeamsDistributeSimdDirective *
OMPTargetTeamsDistributeSimdDirective::Create(
    const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
    unsigned CollapsedNum, ArrayRef<OMPClause *> Clauses, Stmt *AssociatedStmt,
    const HelperExprs &Exprs) {
  auto *Dir = createDirective<OMPTargetTeamsDistributeSimdDirective>(
      C, Clauses, AssociatedStmt,
      numLoopChildren(CollapsedNum, OMPD_target_teams_distribute_simd),
      StartLoc, EndLoc, CollapsedNum);
  Dir->setIterationVariable(Exprs.IterationVarRef);
  Dir->setLastIteration(Exprs.LastIteration);
  Dir->setCalcLastIteration(Exprs.CalcLastIteration);
  Dir->setPreCond(Exprs.PreCond);
  Dir->setCond(Exprs.Cond);
  Dir->setInit(Exprs.Init);
  Dir->setInc(Exprs.Inc);
  Dir->setIsLastIterVariable(Exprs.IL);
  Dir->setLowerBoundVariable(Exprs.LB);
  Dir->setUpperBoundVariable(Exprs.UB);
  Dir->setStrideVariable(Exprs.ST);
  Dir->setEnsureUpperBound(Exprs.EUB);
  Dir->setNextLowerBound(Exprs.NLB);
  Dir->setNextUpperBound(Exprs.NUB);
  Dir->setNumIterations(Exprs.NumIterations);
  Dir->setCounters(Exprs.Counters);
  Dir->setPrivateCounters(Exprs.PrivateCounters);
  Dir->setInits(Exprs.Inits);
  Dir->setUpdates(Exprs.Updates);
  Dir->setFinals(Exprs.Finals);
  Dir->setDependentCounters(Exprs.DependentCounters);
  Dir->setDependentInits(Exprs.DependentInits);
  Dir->setFinalsConditions(Exprs.FinalsConditions);
  Dir->setPreInits(Exprs.PreInits);
  return Dir;
}

OMPTargetTeamsDistributeSimdDirective *
OMPTargetTeamsDistributeSimdDirective::CreateEmpty(const ASTContext &C,
                                                   unsigned NumClauses,
                                                   unsigned CollapsedNum,
                                                   EmptyShell) {
  return createEmptyDirective<OMPTargetTeamsDistributeSimdDirective>(
      C, NumClauses, /*HasAssociatedStmt=*/true,
      numLoopChildren(CollapsedNum, OMPD_target_teams_distribute_simd),
      CollapsedNum);
}

OMPInteropDirective *
OMPInteropDirective::Create(const ASTContext &C, SourceLocation StartLoc,
                            SourceLocation EndLoc,
                            ArrayRef<OMPClause *> Clauses) {
  return createDirective<OMPInteropDirective>(
      C, Clauses, /*AssociatedStmt=*/nullptr, /*NumChildren=*/0, StartLoc,
      EndLoc);
}

OMPInteropDirective *OMPInteropDirective::CreateEmpty(const ASTContext &C,
                                                      unsigned NumClauses,
                                                      EmptyShell) {
  return createEmptyDirective<OMPInteropDirective>(C, NumClauses);
}

OMPDispatchDirective *OMPDispatchDirective::Create(
    const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
    ArrayRef<OMPClause *> Clauses, Stmt *AssociatedStmt,
    SourceLocation TargetCallLoc) {
  auto *Dir = createDirective<OMPDispatchDirective>(
      C, Clauses, AssociatedStmt, /*NumChildren=*/0, StartLoc, EndLoc);
  Dir->setTargetCallLoc(TargetCallLoc);
  return Dir;
}

OMPDispatchDirective *OMPDispatchDirective::CreateEmpty(const ASTContext &C,
                                                        unsigned NumClauses,
                                                        EmptyShell) {
  return createEmptyDirective<OMPDispatchDirective>(C, NumClauses,
                                                    /*HasAssociatedStmt=*/true,
                                                    /*NumChildren=*/0);
}

OMPMaskedDirective *OMPMaskedDirective::Create(const ASTContext &C,
                                               SourceLocation StartLoc,
                                               SourceLocation EndLoc,
                                               ArrayRef<OMPClause *> Clauses,
                                               Stmt *AssociatedStmt) {
  return createDirective<OMPMaskedDirective>(C, Clauses, AssociatedStmt,
                                             /*NumChildren=*/0, StartLoc,
                                             EndLoc);
}

OMPMaskedDirective *OMPMaskedDirective::CreateEmpty(const ASTContext &C,
                                                    unsigned NumClauses,
                                                    EmptyShell) {
  return createEmptyDirective<OMPMaskedDirective>(C, NumClauses,
                                                  /*HasAssociatedStmt=*/true);
}

OMPGenericLoopDirective *OMPGenericLoopDirective::Create(
    const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
    unsigned CollapsedNum, ArrayRef<OMPClause *> Clauses, Stmt *AssociatedStmt,
    const HelperExprs &Exprs) {
  auto *Dir = createDirective<OMPGenericLoopDirective>(
      C, Clauses, AssociatedStmt, numLoopChildren(CollapsedNum, OMPD_loop),
      StartLoc, EndLoc, CollapsedNum);
  Dir->setIterationVariable(Exprs.IterationVarRef);
  Dir->setLastIteration(Exprs.LastIteration);
  Dir->setCalcLastIteration(Exprs.CalcLastIteration);
  Dir->setPreCond(Exprs.PreCond);
  Dir->setCond(Exprs.Cond);
  Dir->setInit(Exprs.Init);
  Dir->setInc(Exprs.Inc);
  Dir->setIsLastIterVariable(Exprs.IL);
  Dir->setLowerBoundVariable(Exprs.LB);
  Dir->setUpperBoundVariable(Exprs.UB);
  Dir->setStrideVariable(Exprs.ST);
  Dir->setEnsureUpperBound(Exprs.EUB);
  Dir->setNextLowerBound(Exprs.NLB);
  Dir->setNextUpperBound(Exprs.NUB);
  Dir->setNumIterations(Exprs.NumIterations);
  Dir->setCounters(Exprs.Counters);
  Dir->setPrivateCounters(Exprs.PrivateCounters);
  Dir->setInits(Exprs.Inits);
  Dir->setUpdates(Exprs.Updates);
  Dir->setFinals(Exprs.Finals);
  Dir->setDependentCounters(Exprs.DependentCounters);
  Dir->setDependentInits(Exprs.DependentInits);
  Dir->setFinalsConditions(Exprs.FinalsConditions);
  Dir->setPreInits(Exprs.PreInits);
  return Dir;
}

OMPGenericLoopDirective *
OMPGenericLoopDirective::CreateEmpty(const ASTContext &C, unsigned NumClauses,
                                     unsigned CollapsedNum, EmptyShell) {
  return createEmptyDirective<OMPGenericLoopDirective>(
      C, NumClauses, /*HasAssociatedStmt=*/true,
      numLoopChildren(CollapsedNum, OMPD_loop), CollapsedNum);
}

OMPTeamsGenericLoopDirective *OMPTeamsGenericLoopDirective::Create(
    const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
    unsigned CollapsedNum, ArrayRef<OMPClause *> Clauses, Stmt *AssociatedStmt,
    const HelperExprs &Exprs) {
  auto *Dir = createDirective<OMPTeamsGenericLoopDirective>(
      C, Clauses, AssociatedStmt,
      numLoopChildren(CollapsedNum, OMPD_teams_loop), StartLoc, EndLoc,
      CollapsedNum);
  Dir->setIterationVariable(Exprs.IterationVarRef);
  Dir->setLastIteration(Exprs.LastIteration);
  Dir->setCalcLastIteration(Exprs.CalcLastIteration);
  Dir->setPreCond(Exprs.PreCond);
  Dir->setCond(Exprs.Cond);
  Dir->setInit(Exprs.Init);
  Dir->setInc(Exprs.Inc);
  Dir->setIsLastIterVariable(Exprs.IL);
  Dir->setLowerBoundVariable(Exprs.LB);
  Dir->setUpperBoundVariable(Exprs.UB);
  Dir->setStrideVariable(Exprs.ST);
  Dir->setEnsureUpperBound(Exprs.EUB);
  Dir->setNextLowerBound(Exprs.NLB);
  Dir->setNextUpperBound(Exprs.NUB);
  Dir->setNumIterations(Exprs.NumIterations);
  Dir->setPrevLowerBoundVariable(Exprs.PrevLB);
  Dir->setPrevUpperBoundVariable(Exprs.PrevUB);
  Dir->setDistInc(Exprs.DistInc);
  Dir->setPrevEnsureUpperBound(Exprs.PrevEUB);
  Dir->setCounters(Exprs.Counters);
  Dir->setPrivateCounters(Exprs.PrivateCounters);
  Dir->setInits(Exprs.Inits);
  Dir->setUpdates(Exprs.Updates);
  Dir->setFinals(Exprs.Finals);
  Dir->setDependentCounters(Exprs.DependentCounters);
  Dir->setDependentInits(Exprs.DependentInits);
  Dir->setFinalsConditions(Exprs.FinalsConditions);
  Dir->setPreInits(Exprs.PreInits);
  Dir->setCombinedLowerBoundVariable(Exprs.DistCombinedFields.LB);
  Dir->setCombinedUpperBoundVariable(Exprs.DistCombinedFields.UB);
  Dir->setCombinedEnsureUpperBound(Exprs.DistCombinedFields.EUB);
  Dir->setCombinedInit(Exprs.DistCombinedFields.Init);
  Dir->setCombinedCond(Exprs.DistCombinedFields.Cond);
  Dir->setCombinedNextLowerBound(Exprs.DistCombinedFields.NLB);
  Dir->setCombinedNextUpperBound(Exprs.DistCombinedFields.NUB);
  Dir->setCombinedDistCond(Exprs.DistCombinedFields.DistCond);
  Dir->setCombinedParForInDistCond(Exprs.DistCombinedFields.ParForInDistCond);
  return Dir;
}

OMPTeamsGenericLoopDirective *
OMPTeamsGenericLoopDirective::CreateEmpty(const ASTContext &C,
                                          unsigned NumClauses,
                                          unsigned CollapsedNum, EmptyShell) {
  return createEmptyDirective<OMPTeamsGenericLoopDirective>(
      C, NumClauses, /*HasAssociatedStmt=*/true,
      numLoopChildren(CollapsedNum, OMPD_teams_loop), CollapsedNum);
}

OMPTargetTeamsGenericLoopDirective *OMPTargetTeamsGenericLoopDirective::Create(
    const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
    unsigned CollapsedNum, ArrayRef<OMPClause *> Clauses, Stmt *AssociatedStmt,
    const HelperExprs &Exprs, bool CanBeParallelFor) {
  auto *Dir = createDirective<OMPTargetTeamsGenericLoopDirective>(
      C, Clauses, AssociatedStmt,
      numLoopChildren(CollapsedNum, OMPD_target_teams_loop), StartLoc, EndLoc,
      CollapsedNum);
  Dir->setIterationVariable(Exprs.IterationVarRef);
  Dir->setLastIteration(Exprs.LastIteration);
  Dir->setCalcLastIteration(Exprs.CalcLastIteration);
  Dir->setPreCond(Exprs.PreCond);
  Dir->setCond(Exprs.Cond);
  Dir->setInit(Exprs.Init);
  Dir->setInc(Exprs.Inc);
  Dir->setIsLastIterVariable(Exprs.IL);
  Dir->setLowerBoundVariable(Exprs.LB);
  Dir->setUpperBoundVariable(Exprs.UB);
  Dir->setStrideVariable(Exprs.ST);
  Dir->setEnsureUpperBound(Exprs.EUB);
  Dir->setNextLowerBound(Exprs.NLB);
  Dir->setNextUpperBound(Exprs.NUB);
  Dir->setNumIterations(Exprs.NumIterations);
  Dir->setPrevLowerBoundVariable(Exprs.PrevLB);
  Dir->setPrevUpperBoundVariable(Exprs.PrevUB);
  Dir->setDistInc(Exprs.DistInc);
  Dir->setPrevEnsureUpperBound(Exprs.PrevEUB);
  Dir->setCounters(Exprs.Counters);
  Dir->setPrivateCounters(Exprs.PrivateCounters);
  Dir->setInits(Exprs.Inits);
  Dir->setUpdates(Exprs.Updates);
  Dir->setFinals(Exprs.Finals);
  Dir->setDependentCounters(Exprs.DependentCounters);
  Dir->setDependentInits(Exprs.DependentInits);
  Dir->setFinalsConditions(Exprs.FinalsConditions);
  Dir->setPreInits(Exprs.PreInits);
  Dir->setCombinedLowerBoundVariable(Exprs.DistCombinedFields.LB);
  Dir->setCombinedUpperBoundVariable(Exprs.DistCombinedFields.UB);
  Dir->setCombinedEnsureUpperBound(Exprs.DistCombinedFields.EUB);
  Dir->setCombinedInit(Exprs.DistCombinedFields.Init);
  Dir->setCombinedCond(Exprs.DistCombinedFields.Cond);
  Dir->setCombinedNextLowerBound(Exprs.DistCombinedFields.NLB);
  Dir->setCombinedNextUpperBound(Exprs.DistCombinedFields.NUB);
  Dir->setCombinedDistCond(Exprs.DistCombinedFields.DistCond);
  Dir->setCombinedParForInDistCond(Exprs.DistCombinedFields.ParForInDistCond);
  Dir->setCanBeParallelFor(CanBeParallelFor);
  return Dir;
}

OMPTargetTeamsGenericLoopDirective *
OMPTargetTeamsGenericLoopDirective::CreateEmpty(const ASTContext &C,
                                                unsigned NumClauses,
                                                unsigned CollapsedNum,
                                                EmptyShell) {
  return createEmptyDirective<OMPTargetTeamsGenericLoopDirective>(
      C, NumClauses, /*HasAssociatedStmt=*/true,
      numLoopChildren(CollapsedNum, OMPD_target_teams_loop), CollapsedNum);
}

OMPParallelGenericLoopDirective *OMPParallelGenericLoopDirective::Create(
    const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
    unsigned CollapsedNum, ArrayRef<OMPClause *> Clauses, Stmt *AssociatedStmt,
    const HelperExprs &Exprs) {
  auto *Dir = createDirective<OMPParallelGenericLoopDirective>(
      C, Clauses, AssociatedStmt,
      numLoopChildren(CollapsedNum, OMPD_parallel_loop), StartLoc, EndLoc,
      CollapsedNum);
  Dir->setIterationVariable(Exprs.IterationVarRef);
  Dir->setLastIteration(Exprs.LastIteration);
  Dir->setCalcLastIteration(Exprs.CalcLastIteration);
  Dir->setPreCond(Exprs.PreCond);
  Dir->setCond(Exprs.Cond);
  Dir->setInit(Exprs.Init);
  Dir->setInc(Exprs.Inc);
  Dir->setIsLastIterVariable(Exprs.IL);
  Dir->setLowerBoundVariable(Exprs.LB);
  Dir->setUpperBoundVariable(Exprs.UB);
  Dir->setStrideVariable(Exprs.ST);
  Dir->setEnsureUpperBound(Exprs.EUB);
  Dir->setNextLowerBound(Exprs.NLB);
  Dir->setNextUpperBound(Exprs.NUB);
  Dir->setNumIterations(Exprs.NumIterations);
  Dir->setCounters(Exprs.Counters);
  Dir->setPrivateCounters(Exprs.PrivateCounters);
  Dir->setInits(Exprs.Inits);
  Dir->setUpdates(Exprs.Updates);
  Dir->setFinals(Exprs.Finals);
  Dir->setDependentCounters(Exprs.DependentCounters);
  Dir->setDependentInits(Exprs.DependentInits);
  Dir->setFinalsConditions(Exprs.FinalsConditions);
  Dir->setPreInits(Exprs.PreInits);
  return Dir;
}

OMPParallelGenericLoopDirective *OMPParallelGenericLoopDirective::CreateEmpty(
    const ASTContext &C, unsigned NumClauses, unsigned CollapsedNum,
    EmptyShell) {
  return createEmptyDirective<OMPParallelGenericLoopDirective>(
      C, NumClauses, /*HasAssociatedStmt=*/true,
      numLoopChildren(CollapsedNum, OMPD_parallel_loop), CollapsedNum);
}

OMPTargetParallelGenericLoopDirective *
OMPTargetParallelGenericLoopDirective::Create(
    const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
    unsigned CollapsedNum, ArrayRef<OMPClause *> Clauses, Stmt *AssociatedStmt,
    const HelperExprs &Exprs) {
  auto *Dir = createDirective<OMPTargetParallelGenericLoopDirective>(
      C, Clauses, AssociatedStmt,
      numLoopChildren(CollapsedNum, OMPD_target_parallel_loop), StartLoc,
      EndLoc, CollapsedNum);
  Dir->setIterationVariable(Exprs.IterationVarRef);
  Dir->setLastIteration(Exprs.LastIteration);
  Dir->setCalcLastIteration(Exprs.CalcLastIteration);
  Dir->setPreCond(Exprs.PreCond);
  Dir->setCond(Exprs.Cond);
  Dir->setInit(Exprs.Init);
  Dir->setInc(Exprs.Inc);
  Dir->setIsLastIterVariable(Exprs.IL);
  Dir->setLowerBoundVariable(Exprs.LB);
  Dir->setUpperBoundVariable(Exprs.UB);
  Dir->setStrideVariable(Exprs.ST);
  Dir->setEnsureUpperBound(Exprs.EUB);
  Dir->setNextLowerBound(Exprs.NLB);
  Dir->setNextUpperBound(Exprs.NUB);
  Dir->setNumIterations(Exprs.NumIterations);
  Dir->setCounters(Exprs.Counters);
  Dir->setPrivateCounters(Exprs.PrivateCounters);
  Dir->setInits(Exprs.Inits);
  Dir->setUpdates(Exprs.Updates);
  Dir->setFinals(Exprs.Finals);
  Dir->setDependentCounters(Exprs.DependentCounters);
  Dir->setDependentInits(Exprs.DependentInits);
  Dir->setFinalsConditions(Exprs.FinalsConditions);
  Dir->setPreInits(Exprs.PreInits);
  return Dir;
}

OMPTargetParallelGenericLoopDirective *
OMPTargetParallelGenericLoopDirective::CreateEmpty(const ASTContext &C,
                                                   unsigned NumClauses,
                                                   unsigned CollapsedNum,
                                                   EmptyShell) {
  return createEmptyDirective<OMPTargetParallelGenericLoopDirective>(
      C, NumClauses, /*HasAssociatedStmt=*/true,
      numLoopChildren(CollapsedNum, OMPD_target_parallel_loop), CollapsedNum);
}
