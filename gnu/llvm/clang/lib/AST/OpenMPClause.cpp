//===- OpenMPClause.cpp - Classes for OpenMP clauses ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the subclesses of Stmt class declared in OpenMPClause.h
//
//===----------------------------------------------------------------------===//

#include "clang/AST/OpenMPClause.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclOpenMP.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/OpenMPKinds.h"
#include "clang/Basic/TargetInfo.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include <algorithm>
#include <cassert>
#include <optional>

using namespace clang;
using namespace llvm;
using namespace omp;

OMPClause::child_range OMPClause::children() {
  switch (getClauseKind()) {
  default:
    break;
#define GEN_CLANG_CLAUSE_CLASS
#define CLAUSE_CLASS(Enum, Str, Class)                                         \
  case Enum:                                                                   \
    return static_cast<Class *>(this)->children();
#include "llvm/Frontend/OpenMP/OMP.inc"
  }
  llvm_unreachable("unknown OMPClause");
}

OMPClause::child_range OMPClause::used_children() {
  switch (getClauseKind()) {
#define GEN_CLANG_CLAUSE_CLASS
#define CLAUSE_CLASS(Enum, Str, Class)                                         \
  case Enum:                                                                   \
    return static_cast<Class *>(this)->used_children();
#define CLAUSE_NO_CLASS(Enum, Str)                                             \
  case Enum:                                                                   \
    break;
#include "llvm/Frontend/OpenMP/OMP.inc"
  }
  llvm_unreachable("unknown OMPClause");
}

OMPClauseWithPreInit *OMPClauseWithPreInit::get(OMPClause *C) {
  auto *Res = OMPClauseWithPreInit::get(const_cast<const OMPClause *>(C));
  return Res ? const_cast<OMPClauseWithPreInit *>(Res) : nullptr;
}

const OMPClauseWithPreInit *OMPClauseWithPreInit::get(const OMPClause *C) {
  switch (C->getClauseKind()) {
  case OMPC_schedule:
    return static_cast<const OMPScheduleClause *>(C);
  case OMPC_dist_schedule:
    return static_cast<const OMPDistScheduleClause *>(C);
  case OMPC_firstprivate:
    return static_cast<const OMPFirstprivateClause *>(C);
  case OMPC_lastprivate:
    return static_cast<const OMPLastprivateClause *>(C);
  case OMPC_reduction:
    return static_cast<const OMPReductionClause *>(C);
  case OMPC_task_reduction:
    return static_cast<const OMPTaskReductionClause *>(C);
  case OMPC_in_reduction:
    return static_cast<const OMPInReductionClause *>(C);
  case OMPC_linear:
    return static_cast<const OMPLinearClause *>(C);
  case OMPC_if:
    return static_cast<const OMPIfClause *>(C);
  case OMPC_num_threads:
    return static_cast<const OMPNumThreadsClause *>(C);
  case OMPC_num_teams:
    return static_cast<const OMPNumTeamsClause *>(C);
  case OMPC_thread_limit:
    return static_cast<const OMPThreadLimitClause *>(C);
  case OMPC_device:
    return static_cast<const OMPDeviceClause *>(C);
  case OMPC_grainsize:
    return static_cast<const OMPGrainsizeClause *>(C);
  case OMPC_num_tasks:
    return static_cast<const OMPNumTasksClause *>(C);
  case OMPC_final:
    return static_cast<const OMPFinalClause *>(C);
  case OMPC_priority:
    return static_cast<const OMPPriorityClause *>(C);
  case OMPC_novariants:
    return static_cast<const OMPNovariantsClause *>(C);
  case OMPC_nocontext:
    return static_cast<const OMPNocontextClause *>(C);
  case OMPC_filter:
    return static_cast<const OMPFilterClause *>(C);
  case OMPC_ompx_dyn_cgroup_mem:
    return static_cast<const OMPXDynCGroupMemClause *>(C);
  case OMPC_default:
  case OMPC_proc_bind:
  case OMPC_safelen:
  case OMPC_simdlen:
  case OMPC_sizes:
  case OMPC_allocator:
  case OMPC_allocate:
  case OMPC_collapse:
  case OMPC_private:
  case OMPC_shared:
  case OMPC_aligned:
  case OMPC_copyin:
  case OMPC_copyprivate:
  case OMPC_ordered:
  case OMPC_nowait:
  case OMPC_untied:
  case OMPC_mergeable:
  case OMPC_threadprivate:
  case OMPC_flush:
  case OMPC_depobj:
  case OMPC_read:
  case OMPC_write:
  case OMPC_update:
  case OMPC_capture:
  case OMPC_compare:
  case OMPC_fail:
  case OMPC_seq_cst:
  case OMPC_acq_rel:
  case OMPC_acquire:
  case OMPC_release:
  case OMPC_relaxed:
  case OMPC_depend:
  case OMPC_threads:
  case OMPC_simd:
  case OMPC_map:
  case OMPC_nogroup:
  case OMPC_hint:
  case OMPC_defaultmap:
  case OMPC_unknown:
  case OMPC_uniform:
  case OMPC_to:
  case OMPC_from:
  case OMPC_use_device_ptr:
  case OMPC_use_device_addr:
  case OMPC_is_device_ptr:
  case OMPC_has_device_addr:
  case OMPC_unified_address:
  case OMPC_unified_shared_memory:
  case OMPC_reverse_offload:
  case OMPC_dynamic_allocators:
  case OMPC_atomic_default_mem_order:
  case OMPC_at:
  case OMPC_severity:
  case OMPC_message:
  case OMPC_device_type:
  case OMPC_match:
  case OMPC_nontemporal:
  case OMPC_order:
  case OMPC_destroy:
  case OMPC_detach:
  case OMPC_inclusive:
  case OMPC_exclusive:
  case OMPC_uses_allocators:
  case OMPC_affinity:
  case OMPC_when:
  case OMPC_bind:
  case OMPC_ompx_bare:
    break;
  default:
    break;
  }

  return nullptr;
}

OMPClauseWithPostUpdate *OMPClauseWithPostUpdate::get(OMPClause *C) {
  auto *Res = OMPClauseWithPostUpdate::get(const_cast<const OMPClause *>(C));
  return Res ? const_cast<OMPClauseWithPostUpdate *>(Res) : nullptr;
}

const OMPClauseWithPostUpdate *OMPClauseWithPostUpdate::get(const OMPClause *C) {
  switch (C->getClauseKind()) {
  case OMPC_lastprivate:
    return static_cast<const OMPLastprivateClause *>(C);
  case OMPC_reduction:
    return static_cast<const OMPReductionClause *>(C);
  case OMPC_task_reduction:
    return static_cast<const OMPTaskReductionClause *>(C);
  case OMPC_in_reduction:
    return static_cast<const OMPInReductionClause *>(C);
  case OMPC_linear:
    return static_cast<const OMPLinearClause *>(C);
  case OMPC_schedule:
  case OMPC_dist_schedule:
  case OMPC_firstprivate:
  case OMPC_default:
  case OMPC_proc_bind:
  case OMPC_if:
  case OMPC_final:
  case OMPC_num_threads:
  case OMPC_safelen:
  case OMPC_simdlen:
  case OMPC_sizes:
  case OMPC_allocator:
  case OMPC_allocate:
  case OMPC_collapse:
  case OMPC_private:
  case OMPC_shared:
  case OMPC_aligned:
  case OMPC_copyin:
  case OMPC_copyprivate:
  case OMPC_ordered:
  case OMPC_nowait:
  case OMPC_untied:
  case OMPC_mergeable:
  case OMPC_threadprivate:
  case OMPC_flush:
  case OMPC_depobj:
  case OMPC_read:
  case OMPC_write:
  case OMPC_update:
  case OMPC_capture:
  case OMPC_compare:
  case OMPC_fail:
  case OMPC_seq_cst:
  case OMPC_acq_rel:
  case OMPC_acquire:
  case OMPC_release:
  case OMPC_relaxed:
  case OMPC_depend:
  case OMPC_device:
  case OMPC_threads:
  case OMPC_simd:
  case OMPC_map:
  case OMPC_num_teams:
  case OMPC_thread_limit:
  case OMPC_priority:
  case OMPC_grainsize:
  case OMPC_nogroup:
  case OMPC_num_tasks:
  case OMPC_hint:
  case OMPC_defaultmap:
  case OMPC_unknown:
  case OMPC_uniform:
  case OMPC_to:
  case OMPC_from:
  case OMPC_use_device_ptr:
  case OMPC_use_device_addr:
  case OMPC_is_device_ptr:
  case OMPC_has_device_addr:
  case OMPC_unified_address:
  case OMPC_unified_shared_memory:
  case OMPC_reverse_offload:
  case OMPC_dynamic_allocators:
  case OMPC_atomic_default_mem_order:
  case OMPC_at:
  case OMPC_severity:
  case OMPC_message:
  case OMPC_device_type:
  case OMPC_match:
  case OMPC_nontemporal:
  case OMPC_order:
  case OMPC_destroy:
  case OMPC_novariants:
  case OMPC_nocontext:
  case OMPC_detach:
  case OMPC_inclusive:
  case OMPC_exclusive:
  case OMPC_uses_allocators:
  case OMPC_affinity:
  case OMPC_when:
  case OMPC_bind:
    break;
  default:
    break;
  }

  return nullptr;
}

/// Gets the address of the original, non-captured, expression used in the
/// clause as the preinitializer.
static Stmt **getAddrOfExprAsWritten(Stmt *S) {
  if (!S)
    return nullptr;
  if (auto *DS = dyn_cast<DeclStmt>(S)) {
    assert(DS->isSingleDecl() && "Only single expression must be captured.");
    if (auto *OED = dyn_cast<OMPCapturedExprDecl>(DS->getSingleDecl()))
      return OED->getInitAddress();
  }
  return nullptr;
}

OMPClause::child_range OMPIfClause::used_children() {
  if (Stmt **C = getAddrOfExprAsWritten(getPreInitStmt()))
    return child_range(C, C + 1);
  return child_range(&Condition, &Condition + 1);
}

OMPClause::child_range OMPGrainsizeClause::used_children() {
  if (Stmt **C = getAddrOfExprAsWritten(getPreInitStmt()))
    return child_range(C, C + 1);
  return child_range(&Grainsize, &Grainsize + 1);
}

OMPClause::child_range OMPNumTasksClause::used_children() {
  if (Stmt **C = getAddrOfExprAsWritten(getPreInitStmt()))
    return child_range(C, C + 1);
  return child_range(&NumTasks, &NumTasks + 1);
}

OMPClause::child_range OMPFinalClause::used_children() {
  if (Stmt **C = getAddrOfExprAsWritten(getPreInitStmt()))
    return child_range(C, C + 1);
  return children();
}

OMPClause::child_range OMPPriorityClause::used_children() {
  if (Stmt **C = getAddrOfExprAsWritten(getPreInitStmt()))
    return child_range(C, C + 1);
  return child_range(&Priority, &Priority + 1);
}

OMPClause::child_range OMPNovariantsClause::used_children() {
  if (Stmt **C = getAddrOfExprAsWritten(getPreInitStmt()))
    return child_range(C, C + 1);
  return children();
}

OMPClause::child_range OMPNocontextClause::used_children() {
  if (Stmt **C = getAddrOfExprAsWritten(getPreInitStmt()))
    return child_range(C, C + 1);
  return children();
}

OMPOrderedClause *OMPOrderedClause::Create(const ASTContext &C, Expr *Num,
                                           unsigned NumLoops,
                                           SourceLocation StartLoc,
                                           SourceLocation LParenLoc,
                                           SourceLocation EndLoc) {
  void *Mem = C.Allocate(totalSizeToAlloc<Expr *>(2 * NumLoops));
  auto *Clause =
      new (Mem) OMPOrderedClause(Num, NumLoops, StartLoc, LParenLoc, EndLoc);
  for (unsigned I = 0; I < NumLoops; ++I) {
    Clause->setLoopNumIterations(I, nullptr);
    Clause->setLoopCounter(I, nullptr);
  }
  return Clause;
}

OMPOrderedClause *OMPOrderedClause::CreateEmpty(const ASTContext &C,
                                                unsigned NumLoops) {
  void *Mem = C.Allocate(totalSizeToAlloc<Expr *>(2 * NumLoops));
  auto *Clause = new (Mem) OMPOrderedClause(NumLoops);
  for (unsigned I = 0; I < NumLoops; ++I) {
    Clause->setLoopNumIterations(I, nullptr);
    Clause->setLoopCounter(I, nullptr);
  }
  return Clause;
}

void OMPOrderedClause::setLoopNumIterations(unsigned NumLoop,
                                            Expr *NumIterations) {
  assert(NumLoop < NumberOfLoops && "out of loops number.");
  getTrailingObjects<Expr *>()[NumLoop] = NumIterations;
}

ArrayRef<Expr *> OMPOrderedClause::getLoopNumIterations() const {
  return llvm::ArrayRef(getTrailingObjects<Expr *>(), NumberOfLoops);
}

void OMPOrderedClause::setLoopCounter(unsigned NumLoop, Expr *Counter) {
  assert(NumLoop < NumberOfLoops && "out of loops number.");
  getTrailingObjects<Expr *>()[NumberOfLoops + NumLoop] = Counter;
}

Expr *OMPOrderedClause::getLoopCounter(unsigned NumLoop) {
  assert(NumLoop < NumberOfLoops && "out of loops number.");
  return getTrailingObjects<Expr *>()[NumberOfLoops + NumLoop];
}

const Expr *OMPOrderedClause::getLoopCounter(unsigned NumLoop) const {
  assert(NumLoop < NumberOfLoops && "out of loops number.");
  return getTrailingObjects<Expr *>()[NumberOfLoops + NumLoop];
}

OMPUpdateClause *OMPUpdateClause::Create(const ASTContext &C,
                                         SourceLocation StartLoc,
                                         SourceLocation EndLoc) {
  return new (C) OMPUpdateClause(StartLoc, EndLoc, /*IsExtended=*/false);
}

OMPUpdateClause *
OMPUpdateClause::Create(const ASTContext &C, SourceLocation StartLoc,
                        SourceLocation LParenLoc, SourceLocation ArgumentLoc,
                        OpenMPDependClauseKind DK, SourceLocation EndLoc) {
  void *Mem =
      C.Allocate(totalSizeToAlloc<SourceLocation, OpenMPDependClauseKind>(2, 1),
                 alignof(OMPUpdateClause));
  auto *Clause =
      new (Mem) OMPUpdateClause(StartLoc, EndLoc, /*IsExtended=*/true);
  Clause->setLParenLoc(LParenLoc);
  Clause->setArgumentLoc(ArgumentLoc);
  Clause->setDependencyKind(DK);
  return Clause;
}

OMPUpdateClause *OMPUpdateClause::CreateEmpty(const ASTContext &C,
                                              bool IsExtended) {
  if (!IsExtended)
    return new (C) OMPUpdateClause(/*IsExtended=*/false);
  void *Mem =
      C.Allocate(totalSizeToAlloc<SourceLocation, OpenMPDependClauseKind>(2, 1),
                 alignof(OMPUpdateClause));
  auto *Clause = new (Mem) OMPUpdateClause(/*IsExtended=*/true);
  Clause->IsExtended = true;
  return Clause;
}

void OMPPrivateClause::setPrivateCopies(ArrayRef<Expr *> VL) {
  assert(VL.size() == varlist_size() &&
         "Number of private copies is not the same as the preallocated buffer");
  std::copy(VL.begin(), VL.end(), varlist_end());
}

OMPPrivateClause *
OMPPrivateClause::Create(const ASTContext &C, SourceLocation StartLoc,
                         SourceLocation LParenLoc, SourceLocation EndLoc,
                         ArrayRef<Expr *> VL, ArrayRef<Expr *> PrivateVL) {
  // Allocate space for private variables and initializer expressions.
  void *Mem = C.Allocate(totalSizeToAlloc<Expr *>(2 * VL.size()));
  OMPPrivateClause *Clause =
      new (Mem) OMPPrivateClause(StartLoc, LParenLoc, EndLoc, VL.size());
  Clause->setVarRefs(VL);
  Clause->setPrivateCopies(PrivateVL);
  return Clause;
}

OMPPrivateClause *OMPPrivateClause::CreateEmpty(const ASTContext &C,
                                                unsigned N) {
  void *Mem = C.Allocate(totalSizeToAlloc<Expr *>(2 * N));
  return new (Mem) OMPPrivateClause(N);
}

void OMPFirstprivateClause::setPrivateCopies(ArrayRef<Expr *> VL) {
  assert(VL.size() == varlist_size() &&
         "Number of private copies is not the same as the preallocated buffer");
  std::copy(VL.begin(), VL.end(), varlist_end());
}

void OMPFirstprivateClause::setInits(ArrayRef<Expr *> VL) {
  assert(VL.size() == varlist_size() &&
         "Number of inits is not the same as the preallocated buffer");
  std::copy(VL.begin(), VL.end(), getPrivateCopies().end());
}

OMPFirstprivateClause *
OMPFirstprivateClause::Create(const ASTContext &C, SourceLocation StartLoc,
                              SourceLocation LParenLoc, SourceLocation EndLoc,
                              ArrayRef<Expr *> VL, ArrayRef<Expr *> PrivateVL,
                              ArrayRef<Expr *> InitVL, Stmt *PreInit) {
  void *Mem = C.Allocate(totalSizeToAlloc<Expr *>(3 * VL.size()));
  OMPFirstprivateClause *Clause =
      new (Mem) OMPFirstprivateClause(StartLoc, LParenLoc, EndLoc, VL.size());
  Clause->setVarRefs(VL);
  Clause->setPrivateCopies(PrivateVL);
  Clause->setInits(InitVL);
  Clause->setPreInitStmt(PreInit);
  return Clause;
}

OMPFirstprivateClause *OMPFirstprivateClause::CreateEmpty(const ASTContext &C,
                                                          unsigned N) {
  void *Mem = C.Allocate(totalSizeToAlloc<Expr *>(3 * N));
  return new (Mem) OMPFirstprivateClause(N);
}

void OMPLastprivateClause::setPrivateCopies(ArrayRef<Expr *> PrivateCopies) {
  assert(PrivateCopies.size() == varlist_size() &&
         "Number of private copies is not the same as the preallocated buffer");
  std::copy(PrivateCopies.begin(), PrivateCopies.end(), varlist_end());
}

void OMPLastprivateClause::setSourceExprs(ArrayRef<Expr *> SrcExprs) {
  assert(SrcExprs.size() == varlist_size() && "Number of source expressions is "
                                              "not the same as the "
                                              "preallocated buffer");
  std::copy(SrcExprs.begin(), SrcExprs.end(), getPrivateCopies().end());
}

void OMPLastprivateClause::setDestinationExprs(ArrayRef<Expr *> DstExprs) {
  assert(DstExprs.size() == varlist_size() && "Number of destination "
                                              "expressions is not the same as "
                                              "the preallocated buffer");
  std::copy(DstExprs.begin(), DstExprs.end(), getSourceExprs().end());
}

void OMPLastprivateClause::setAssignmentOps(ArrayRef<Expr *> AssignmentOps) {
  assert(AssignmentOps.size() == varlist_size() &&
         "Number of assignment expressions is not the same as the preallocated "
         "buffer");
  std::copy(AssignmentOps.begin(), AssignmentOps.end(),
            getDestinationExprs().end());
}

OMPLastprivateClause *OMPLastprivateClause::Create(
    const ASTContext &C, SourceLocation StartLoc, SourceLocation LParenLoc,
    SourceLocation EndLoc, ArrayRef<Expr *> VL, ArrayRef<Expr *> SrcExprs,
    ArrayRef<Expr *> DstExprs, ArrayRef<Expr *> AssignmentOps,
    OpenMPLastprivateModifier LPKind, SourceLocation LPKindLoc,
    SourceLocation ColonLoc, Stmt *PreInit, Expr *PostUpdate) {
  void *Mem = C.Allocate(totalSizeToAlloc<Expr *>(5 * VL.size()));
  OMPLastprivateClause *Clause = new (Mem) OMPLastprivateClause(
      StartLoc, LParenLoc, EndLoc, LPKind, LPKindLoc, ColonLoc, VL.size());
  Clause->setVarRefs(VL);
  Clause->setSourceExprs(SrcExprs);
  Clause->setDestinationExprs(DstExprs);
  Clause->setAssignmentOps(AssignmentOps);
  Clause->setPreInitStmt(PreInit);
  Clause->setPostUpdateExpr(PostUpdate);
  return Clause;
}

OMPLastprivateClause *OMPLastprivateClause::CreateEmpty(const ASTContext &C,
                                                        unsigned N) {
  void *Mem = C.Allocate(totalSizeToAlloc<Expr *>(5 * N));
  return new (Mem) OMPLastprivateClause(N);
}

OMPSharedClause *OMPSharedClause::Create(const ASTContext &C,
                                         SourceLocation StartLoc,
                                         SourceLocation LParenLoc,
                                         SourceLocation EndLoc,
                                         ArrayRef<Expr *> VL) {
  void *Mem = C.Allocate(totalSizeToAlloc<Expr *>(VL.size()));
  OMPSharedClause *Clause =
      new (Mem) OMPSharedClause(StartLoc, LParenLoc, EndLoc, VL.size());
  Clause->setVarRefs(VL);
  return Clause;
}

OMPSharedClause *OMPSharedClause::CreateEmpty(const ASTContext &C, unsigned N) {
  void *Mem = C.Allocate(totalSizeToAlloc<Expr *>(N));
  return new (Mem) OMPSharedClause(N);
}

void OMPLinearClause::setPrivates(ArrayRef<Expr *> PL) {
  assert(PL.size() == varlist_size() &&
         "Number of privates is not the same as the preallocated buffer");
  std::copy(PL.begin(), PL.end(), varlist_end());
}

void OMPLinearClause::setInits(ArrayRef<Expr *> IL) {
  assert(IL.size() == varlist_size() &&
         "Number of inits is not the same as the preallocated buffer");
  std::copy(IL.begin(), IL.end(), getPrivates().end());
}

void OMPLinearClause::setUpdates(ArrayRef<Expr *> UL) {
  assert(UL.size() == varlist_size() &&
         "Number of updates is not the same as the preallocated buffer");
  std::copy(UL.begin(), UL.end(), getInits().end());
}

void OMPLinearClause::setFinals(ArrayRef<Expr *> FL) {
  assert(FL.size() == varlist_size() &&
         "Number of final updates is not the same as the preallocated buffer");
  std::copy(FL.begin(), FL.end(), getUpdates().end());
}

void OMPLinearClause::setUsedExprs(ArrayRef<Expr *> UE) {
  assert(
      UE.size() == varlist_size() + 1 &&
      "Number of used expressions is not the same as the preallocated buffer");
  std::copy(UE.begin(), UE.end(), getFinals().end() + 2);
}

OMPLinearClause *OMPLinearClause::Create(
    const ASTContext &C, SourceLocation StartLoc, SourceLocation LParenLoc,
    OpenMPLinearClauseKind Modifier, SourceLocation ModifierLoc,
    SourceLocation ColonLoc, SourceLocation StepModifierLoc,
    SourceLocation EndLoc, ArrayRef<Expr *> VL, ArrayRef<Expr *> PL,
    ArrayRef<Expr *> IL, Expr *Step, Expr *CalcStep, Stmt *PreInit,
    Expr *PostUpdate) {
  // Allocate space for 5 lists (Vars, Inits, Updates, Finals), 2 expressions
  // (Step and CalcStep), list of used expression + step.
  void *Mem =
      C.Allocate(totalSizeToAlloc<Expr *>(5 * VL.size() + 2 + VL.size() + 1));
  OMPLinearClause *Clause =
      new (Mem) OMPLinearClause(StartLoc, LParenLoc, Modifier, ModifierLoc,
                                ColonLoc, StepModifierLoc, EndLoc, VL.size());
  Clause->setVarRefs(VL);
  Clause->setPrivates(PL);
  Clause->setInits(IL);
  // Fill update and final expressions with zeroes, they are provided later,
  // after the directive construction.
  std::fill(Clause->getInits().end(), Clause->getInits().end() + VL.size(),
            nullptr);
  std::fill(Clause->getUpdates().end(), Clause->getUpdates().end() + VL.size(),
            nullptr);
  std::fill(Clause->getUsedExprs().begin(), Clause->getUsedExprs().end(),
            nullptr);
  Clause->setStep(Step);
  Clause->setCalcStep(CalcStep);
  Clause->setPreInitStmt(PreInit);
  Clause->setPostUpdateExpr(PostUpdate);
  return Clause;
}

OMPLinearClause *OMPLinearClause::CreateEmpty(const ASTContext &C,
                                              unsigned NumVars) {
  // Allocate space for 5 lists (Vars, Inits, Updates, Finals), 2 expressions
  // (Step and CalcStep), list of used expression + step.
  void *Mem = C.Allocate(totalSizeToAlloc<Expr *>(5 * NumVars + 2 + NumVars  +1));
  return new (Mem) OMPLinearClause(NumVars);
}

OMPClause::child_range OMPLinearClause::used_children() {
  // Range includes only non-nullptr elements.
  return child_range(
      reinterpret_cast<Stmt **>(getUsedExprs().begin()),
      reinterpret_cast<Stmt **>(llvm::find(getUsedExprs(), nullptr)));
}

OMPAlignedClause *
OMPAlignedClause::Create(const ASTContext &C, SourceLocation StartLoc,
                         SourceLocation LParenLoc, SourceLocation ColonLoc,
                         SourceLocation EndLoc, ArrayRef<Expr *> VL, Expr *A) {
  void *Mem = C.Allocate(totalSizeToAlloc<Expr *>(VL.size() + 1));
  OMPAlignedClause *Clause = new (Mem)
      OMPAlignedClause(StartLoc, LParenLoc, ColonLoc, EndLoc, VL.size());
  Clause->setVarRefs(VL);
  Clause->setAlignment(A);
  return Clause;
}

OMPAlignedClause *OMPAlignedClause::CreateEmpty(const ASTContext &C,
                                                unsigned NumVars) {
  void *Mem = C.Allocate(totalSizeToAlloc<Expr *>(NumVars + 1));
  return new (Mem) OMPAlignedClause(NumVars);
}

OMPAlignClause *OMPAlignClause::Create(const ASTContext &C, Expr *A,
                                       SourceLocation StartLoc,
                                       SourceLocation LParenLoc,
                                       SourceLocation EndLoc) {
  return new (C) OMPAlignClause(A, StartLoc, LParenLoc, EndLoc);
}

void OMPCopyinClause::setSourceExprs(ArrayRef<Expr *> SrcExprs) {
  assert(SrcExprs.size() == varlist_size() && "Number of source expressions is "
                                              "not the same as the "
                                              "preallocated buffer");
  std::copy(SrcExprs.begin(), SrcExprs.end(), varlist_end());
}

void OMPCopyinClause::setDestinationExprs(ArrayRef<Expr *> DstExprs) {
  assert(DstExprs.size() == varlist_size() && "Number of destination "
                                              "expressions is not the same as "
                                              "the preallocated buffer");
  std::copy(DstExprs.begin(), DstExprs.end(), getSourceExprs().end());
}

void OMPCopyinClause::setAssignmentOps(ArrayRef<Expr *> AssignmentOps) {
  assert(AssignmentOps.size() == varlist_size() &&
         "Number of assignment expressions is not the same as the preallocated "
         "buffer");
  std::copy(AssignmentOps.begin(), AssignmentOps.end(),
            getDestinationExprs().end());
}

OMPCopyinClause *OMPCopyinClause::Create(
    const ASTContext &C, SourceLocation StartLoc, SourceLocation LParenLoc,
    SourceLocation EndLoc, ArrayRef<Expr *> VL, ArrayRef<Expr *> SrcExprs,
    ArrayRef<Expr *> DstExprs, ArrayRef<Expr *> AssignmentOps) {
  void *Mem = C.Allocate(totalSizeToAlloc<Expr *>(4 * VL.size()));
  OMPCopyinClause *Clause =
      new (Mem) OMPCopyinClause(StartLoc, LParenLoc, EndLoc, VL.size());
  Clause->setVarRefs(VL);
  Clause->setSourceExprs(SrcExprs);
  Clause->setDestinationExprs(DstExprs);
  Clause->setAssignmentOps(AssignmentOps);
  return Clause;
}

OMPCopyinClause *OMPCopyinClause::CreateEmpty(const ASTContext &C, unsigned N) {
  void *Mem = C.Allocate(totalSizeToAlloc<Expr *>(4 * N));
  return new (Mem) OMPCopyinClause(N);
}

void OMPCopyprivateClause::setSourceExprs(ArrayRef<Expr *> SrcExprs) {
  assert(SrcExprs.size() == varlist_size() && "Number of source expressions is "
                                              "not the same as the "
                                              "preallocated buffer");
  std::copy(SrcExprs.begin(), SrcExprs.end(), varlist_end());
}

void OMPCopyprivateClause::setDestinationExprs(ArrayRef<Expr *> DstExprs) {
  assert(DstExprs.size() == varlist_size() && "Number of destination "
                                              "expressions is not the same as "
                                              "the preallocated buffer");
  std::copy(DstExprs.begin(), DstExprs.end(), getSourceExprs().end());
}

void OMPCopyprivateClause::setAssignmentOps(ArrayRef<Expr *> AssignmentOps) {
  assert(AssignmentOps.size() == varlist_size() &&
         "Number of assignment expressions is not the same as the preallocated "
         "buffer");
  std::copy(AssignmentOps.begin(), AssignmentOps.end(),
            getDestinationExprs().end());
}

OMPCopyprivateClause *OMPCopyprivateClause::Create(
    const ASTContext &C, SourceLocation StartLoc, SourceLocation LParenLoc,
    SourceLocation EndLoc, ArrayRef<Expr *> VL, ArrayRef<Expr *> SrcExprs,
    ArrayRef<Expr *> DstExprs, ArrayRef<Expr *> AssignmentOps) {
  void *Mem = C.Allocate(totalSizeToAlloc<Expr *>(4 * VL.size()));
  OMPCopyprivateClause *Clause =
      new (Mem) OMPCopyprivateClause(StartLoc, LParenLoc, EndLoc, VL.size());
  Clause->setVarRefs(VL);
  Clause->setSourceExprs(SrcExprs);
  Clause->setDestinationExprs(DstExprs);
  Clause->setAssignmentOps(AssignmentOps);
  return Clause;
}

OMPCopyprivateClause *OMPCopyprivateClause::CreateEmpty(const ASTContext &C,
                                                        unsigned N) {
  void *Mem = C.Allocate(totalSizeToAlloc<Expr *>(4 * N));
  return new (Mem) OMPCopyprivateClause(N);
}

void OMPReductionClause::setPrivates(ArrayRef<Expr *> Privates) {
  assert(Privates.size() == varlist_size() &&
         "Number of private copies is not the same as the preallocated buffer");
  std::copy(Privates.begin(), Privates.end(), varlist_end());
}

void OMPReductionClause::setLHSExprs(ArrayRef<Expr *> LHSExprs) {
  assert(
      LHSExprs.size() == varlist_size() &&
      "Number of LHS expressions is not the same as the preallocated buffer");
  std::copy(LHSExprs.begin(), LHSExprs.end(), getPrivates().end());
}

void OMPReductionClause::setRHSExprs(ArrayRef<Expr *> RHSExprs) {
  assert(
      RHSExprs.size() == varlist_size() &&
      "Number of RHS expressions is not the same as the preallocated buffer");
  std::copy(RHSExprs.begin(), RHSExprs.end(), getLHSExprs().end());
}

void OMPReductionClause::setReductionOps(ArrayRef<Expr *> ReductionOps) {
  assert(ReductionOps.size() == varlist_size() && "Number of reduction "
                                                  "expressions is not the same "
                                                  "as the preallocated buffer");
  std::copy(ReductionOps.begin(), ReductionOps.end(), getRHSExprs().end());
}

void OMPReductionClause::setInscanCopyOps(ArrayRef<Expr *> Ops) {
  assert(Modifier == OMPC_REDUCTION_inscan && "Expected inscan reduction.");
  assert(Ops.size() == varlist_size() && "Number of copy "
                                         "expressions is not the same "
                                         "as the preallocated buffer");
  llvm::copy(Ops, getReductionOps().end());
}

void OMPReductionClause::setInscanCopyArrayTemps(
    ArrayRef<Expr *> CopyArrayTemps) {
  assert(Modifier == OMPC_REDUCTION_inscan && "Expected inscan reduction.");
  assert(CopyArrayTemps.size() == varlist_size() &&
         "Number of copy temp expressions is not the same as the preallocated "
         "buffer");
  llvm::copy(CopyArrayTemps, getInscanCopyOps().end());
}

void OMPReductionClause::setInscanCopyArrayElems(
    ArrayRef<Expr *> CopyArrayElems) {
  assert(Modifier == OMPC_REDUCTION_inscan && "Expected inscan reduction.");
  assert(CopyArrayElems.size() == varlist_size() &&
         "Number of copy temp expressions is not the same as the preallocated "
         "buffer");
  llvm::copy(CopyArrayElems, getInscanCopyArrayTemps().end());
}

OMPReductionClause *OMPReductionClause::Create(
    const ASTContext &C, SourceLocation StartLoc, SourceLocation LParenLoc,
    SourceLocation ModifierLoc, SourceLocation EndLoc, SourceLocation ColonLoc,
    OpenMPReductionClauseModifier Modifier, ArrayRef<Expr *> VL,
    NestedNameSpecifierLoc QualifierLoc, const DeclarationNameInfo &NameInfo,
    ArrayRef<Expr *> Privates, ArrayRef<Expr *> LHSExprs,
    ArrayRef<Expr *> RHSExprs, ArrayRef<Expr *> ReductionOps,
    ArrayRef<Expr *> CopyOps, ArrayRef<Expr *> CopyArrayTemps,
    ArrayRef<Expr *> CopyArrayElems, Stmt *PreInit, Expr *PostUpdate) {
  void *Mem = C.Allocate(totalSizeToAlloc<Expr *>(
      (Modifier == OMPC_REDUCTION_inscan ? 8 : 5) * VL.size()));
  auto *Clause = new (Mem)
      OMPReductionClause(StartLoc, LParenLoc, ModifierLoc, EndLoc, ColonLoc,
                         Modifier, VL.size(), QualifierLoc, NameInfo);
  Clause->setVarRefs(VL);
  Clause->setPrivates(Privates);
  Clause->setLHSExprs(LHSExprs);
  Clause->setRHSExprs(RHSExprs);
  Clause->setReductionOps(ReductionOps);
  Clause->setPreInitStmt(PreInit);
  Clause->setPostUpdateExpr(PostUpdate);
  if (Modifier == OMPC_REDUCTION_inscan) {
    Clause->setInscanCopyOps(CopyOps);
    Clause->setInscanCopyArrayTemps(CopyArrayTemps);
    Clause->setInscanCopyArrayElems(CopyArrayElems);
  } else {
    assert(CopyOps.empty() &&
           "copy operations are expected in inscan reductions only.");
    assert(CopyArrayTemps.empty() &&
           "copy array temps are expected in inscan reductions only.");
    assert(CopyArrayElems.empty() &&
           "copy array temps are expected in inscan reductions only.");
  }
  return Clause;
}

OMPReductionClause *
OMPReductionClause::CreateEmpty(const ASTContext &C, unsigned N,
                                OpenMPReductionClauseModifier Modifier) {
  void *Mem = C.Allocate(totalSizeToAlloc<Expr *>(
      (Modifier == OMPC_REDUCTION_inscan ? 8 : 5) * N));
  auto *Clause = new (Mem) OMPReductionClause(N);
  Clause->setModifier(Modifier);
  return Clause;
}

void OMPTaskReductionClause::setPrivates(ArrayRef<Expr *> Privates) {
  assert(Privates.size() == varlist_size() &&
         "Number of private copies is not the same as the preallocated buffer");
  std::copy(Privates.begin(), Privates.end(), varlist_end());
}

void OMPTaskReductionClause::setLHSExprs(ArrayRef<Expr *> LHSExprs) {
  assert(
      LHSExprs.size() == varlist_size() &&
      "Number of LHS expressions is not the same as the preallocated buffer");
  std::copy(LHSExprs.begin(), LHSExprs.end(), getPrivates().end());
}

void OMPTaskReductionClause::setRHSExprs(ArrayRef<Expr *> RHSExprs) {
  assert(
      RHSExprs.size() == varlist_size() &&
      "Number of RHS expressions is not the same as the preallocated buffer");
  std::copy(RHSExprs.begin(), RHSExprs.end(), getLHSExprs().end());
}

void OMPTaskReductionClause::setReductionOps(ArrayRef<Expr *> ReductionOps) {
  assert(ReductionOps.size() == varlist_size() && "Number of task reduction "
                                                  "expressions is not the same "
                                                  "as the preallocated buffer");
  std::copy(ReductionOps.begin(), ReductionOps.end(), getRHSExprs().end());
}

OMPTaskReductionClause *OMPTaskReductionClause::Create(
    const ASTContext &C, SourceLocation StartLoc, SourceLocation LParenLoc,
    SourceLocation EndLoc, SourceLocation ColonLoc, ArrayRef<Expr *> VL,
    NestedNameSpecifierLoc QualifierLoc, const DeclarationNameInfo &NameInfo,
    ArrayRef<Expr *> Privates, ArrayRef<Expr *> LHSExprs,
    ArrayRef<Expr *> RHSExprs, ArrayRef<Expr *> ReductionOps, Stmt *PreInit,
    Expr *PostUpdate) {
  void *Mem = C.Allocate(totalSizeToAlloc<Expr *>(5 * VL.size()));
  OMPTaskReductionClause *Clause = new (Mem) OMPTaskReductionClause(
      StartLoc, LParenLoc, EndLoc, ColonLoc, VL.size(), QualifierLoc, NameInfo);
  Clause->setVarRefs(VL);
  Clause->setPrivates(Privates);
  Clause->setLHSExprs(LHSExprs);
  Clause->setRHSExprs(RHSExprs);
  Clause->setReductionOps(ReductionOps);
  Clause->setPreInitStmt(PreInit);
  Clause->setPostUpdateExpr(PostUpdate);
  return Clause;
}

OMPTaskReductionClause *OMPTaskReductionClause::CreateEmpty(const ASTContext &C,
                                                            unsigned N) {
  void *Mem = C.Allocate(totalSizeToAlloc<Expr *>(5 * N));
  return new (Mem) OMPTaskReductionClause(N);
}

void OMPInReductionClause::setPrivates(ArrayRef<Expr *> Privates) {
  assert(Privates.size() == varlist_size() &&
         "Number of private copies is not the same as the preallocated buffer");
  std::copy(Privates.begin(), Privates.end(), varlist_end());
}

void OMPInReductionClause::setLHSExprs(ArrayRef<Expr *> LHSExprs) {
  assert(
      LHSExprs.size() == varlist_size() &&
      "Number of LHS expressions is not the same as the preallocated buffer");
  std::copy(LHSExprs.begin(), LHSExprs.end(), getPrivates().end());
}

void OMPInReductionClause::setRHSExprs(ArrayRef<Expr *> RHSExprs) {
  assert(
      RHSExprs.size() == varlist_size() &&
      "Number of RHS expressions is not the same as the preallocated buffer");
  std::copy(RHSExprs.begin(), RHSExprs.end(), getLHSExprs().end());
}

void OMPInReductionClause::setReductionOps(ArrayRef<Expr *> ReductionOps) {
  assert(ReductionOps.size() == varlist_size() && "Number of in reduction "
                                                  "expressions is not the same "
                                                  "as the preallocated buffer");
  std::copy(ReductionOps.begin(), ReductionOps.end(), getRHSExprs().end());
}

void OMPInReductionClause::setTaskgroupDescriptors(
    ArrayRef<Expr *> TaskgroupDescriptors) {
  assert(TaskgroupDescriptors.size() == varlist_size() &&
         "Number of in reduction descriptors is not the same as the "
         "preallocated buffer");
  std::copy(TaskgroupDescriptors.begin(), TaskgroupDescriptors.end(),
            getReductionOps().end());
}

OMPInReductionClause *OMPInReductionClause::Create(
    const ASTContext &C, SourceLocation StartLoc, SourceLocation LParenLoc,
    SourceLocation EndLoc, SourceLocation ColonLoc, ArrayRef<Expr *> VL,
    NestedNameSpecifierLoc QualifierLoc, const DeclarationNameInfo &NameInfo,
    ArrayRef<Expr *> Privates, ArrayRef<Expr *> LHSExprs,
    ArrayRef<Expr *> RHSExprs, ArrayRef<Expr *> ReductionOps,
    ArrayRef<Expr *> TaskgroupDescriptors, Stmt *PreInit, Expr *PostUpdate) {
  void *Mem = C.Allocate(totalSizeToAlloc<Expr *>(6 * VL.size()));
  OMPInReductionClause *Clause = new (Mem) OMPInReductionClause(
      StartLoc, LParenLoc, EndLoc, ColonLoc, VL.size(), QualifierLoc, NameInfo);
  Clause->setVarRefs(VL);
  Clause->setPrivates(Privates);
  Clause->setLHSExprs(LHSExprs);
  Clause->setRHSExprs(RHSExprs);
  Clause->setReductionOps(ReductionOps);
  Clause->setTaskgroupDescriptors(TaskgroupDescriptors);
  Clause->setPreInitStmt(PreInit);
  Clause->setPostUpdateExpr(PostUpdate);
  return Clause;
}

OMPInReductionClause *OMPInReductionClause::CreateEmpty(const ASTContext &C,
                                                        unsigned N) {
  void *Mem = C.Allocate(totalSizeToAlloc<Expr *>(6 * N));
  return new (Mem) OMPInReductionClause(N);
}

OMPSizesClause *OMPSizesClause::Create(const ASTContext &C,
                                       SourceLocation StartLoc,
                                       SourceLocation LParenLoc,
                                       SourceLocation EndLoc,
                                       ArrayRef<Expr *> Sizes) {
  OMPSizesClause *Clause = CreateEmpty(C, Sizes.size());
  Clause->setLocStart(StartLoc);
  Clause->setLParenLoc(LParenLoc);
  Clause->setLocEnd(EndLoc);
  Clause->setSizesRefs(Sizes);
  return Clause;
}

OMPSizesClause *OMPSizesClause::CreateEmpty(const ASTContext &C,
                                            unsigned NumSizes) {
  void *Mem = C.Allocate(totalSizeToAlloc<Expr *>(NumSizes));
  return new (Mem) OMPSizesClause(NumSizes);
}

OMPFullClause *OMPFullClause::Create(const ASTContext &C,
                                     SourceLocation StartLoc,
                                     SourceLocation EndLoc) {
  OMPFullClause *Clause = CreateEmpty(C);
  Clause->setLocStart(StartLoc);
  Clause->setLocEnd(EndLoc);
  return Clause;
}

OMPFullClause *OMPFullClause::CreateEmpty(const ASTContext &C) {
  return new (C) OMPFullClause();
}

OMPPartialClause *OMPPartialClause::Create(const ASTContext &C,
                                           SourceLocation StartLoc,
                                           SourceLocation LParenLoc,
                                           SourceLocation EndLoc,
                                           Expr *Factor) {
  OMPPartialClause *Clause = CreateEmpty(C);
  Clause->setLocStart(StartLoc);
  Clause->setLParenLoc(LParenLoc);
  Clause->setLocEnd(EndLoc);
  Clause->setFactor(Factor);
  return Clause;
}

OMPPartialClause *OMPPartialClause::CreateEmpty(const ASTContext &C) {
  return new (C) OMPPartialClause();
}

OMPAllocateClause *
OMPAllocateClause::Create(const ASTContext &C, SourceLocation StartLoc,
                          SourceLocation LParenLoc, Expr *Allocator,
                          SourceLocation ColonLoc, SourceLocation EndLoc,
                          ArrayRef<Expr *> VL) {
  // Allocate space for private variables and initializer expressions.
  void *Mem = C.Allocate(totalSizeToAlloc<Expr *>(VL.size()));
  auto *Clause = new (Mem) OMPAllocateClause(StartLoc, LParenLoc, Allocator,
                                             ColonLoc, EndLoc, VL.size());
  Clause->setVarRefs(VL);
  return Clause;
}

OMPAllocateClause *OMPAllocateClause::CreateEmpty(const ASTContext &C,
                                                  unsigned N) {
  void *Mem = C.Allocate(totalSizeToAlloc<Expr *>(N));
  return new (Mem) OMPAllocateClause(N);
}

OMPFlushClause *OMPFlushClause::Create(const ASTContext &C,
                                       SourceLocation StartLoc,
                                       SourceLocation LParenLoc,
                                       SourceLocation EndLoc,
                                       ArrayRef<Expr *> VL) {
  void *Mem = C.Allocate(totalSizeToAlloc<Expr *>(VL.size() + 1));
  OMPFlushClause *Clause =
      new (Mem) OMPFlushClause(StartLoc, LParenLoc, EndLoc, VL.size());
  Clause->setVarRefs(VL);
  return Clause;
}

OMPFlushClause *OMPFlushClause::CreateEmpty(const ASTContext &C, unsigned N) {
  void *Mem = C.Allocate(totalSizeToAlloc<Expr *>(N));
  return new (Mem) OMPFlushClause(N);
}

OMPDepobjClause *OMPDepobjClause::Create(const ASTContext &C,
                                         SourceLocation StartLoc,
                                         SourceLocation LParenLoc,
                                         SourceLocation RParenLoc,
                                         Expr *Depobj) {
  auto *Clause = new (C) OMPDepobjClause(StartLoc, LParenLoc, RParenLoc);
  Clause->setDepobj(Depobj);
  return Clause;
}

OMPDepobjClause *OMPDepobjClause::CreateEmpty(const ASTContext &C) {
  return new (C) OMPDepobjClause();
}

OMPDependClause *
OMPDependClause::Create(const ASTContext &C, SourceLocation StartLoc,
                        SourceLocation LParenLoc, SourceLocation EndLoc,
                        DependDataTy Data, Expr *DepModifier,
                        ArrayRef<Expr *> VL, unsigned NumLoops) {
  void *Mem = C.Allocate(
      totalSizeToAlloc<Expr *>(VL.size() + /*depend-modifier*/ 1 + NumLoops),
      alignof(OMPDependClause));
  OMPDependClause *Clause = new (Mem)
      OMPDependClause(StartLoc, LParenLoc, EndLoc, VL.size(), NumLoops);
  Clause->setDependencyKind(Data.DepKind);
  Clause->setDependencyLoc(Data.DepLoc);
  Clause->setColonLoc(Data.ColonLoc);
  Clause->setOmpAllMemoryLoc(Data.OmpAllMemoryLoc);
  Clause->setModifier(DepModifier);
  Clause->setVarRefs(VL);
  for (unsigned I = 0 ; I < NumLoops; ++I)
    Clause->setLoopData(I, nullptr);
  return Clause;
}

OMPDependClause *OMPDependClause::CreateEmpty(const ASTContext &C, unsigned N,
                                              unsigned NumLoops) {
  void *Mem =
      C.Allocate(totalSizeToAlloc<Expr *>(N + /*depend-modifier*/ 1 + NumLoops),
                 alignof(OMPDependClause));
  return new (Mem) OMPDependClause(N, NumLoops);
}

void OMPDependClause::setLoopData(unsigned NumLoop, Expr *Cnt) {
  assert((getDependencyKind() == OMPC_DEPEND_sink ||
          getDependencyKind() == OMPC_DEPEND_source) &&
         NumLoop < NumLoops &&
         "Expected sink or source depend + loop index must be less number of "
         "loops.");
  auto *It = std::next(getVarRefs().end(), NumLoop + 1);
  *It = Cnt;
}

Expr *OMPDependClause::getLoopData(unsigned NumLoop) {
  assert((getDependencyKind() == OMPC_DEPEND_sink ||
          getDependencyKind() == OMPC_DEPEND_source) &&
         NumLoop < NumLoops &&
         "Expected sink or source depend + loop index must be less number of "
         "loops.");
  auto *It = std::next(getVarRefs().end(), NumLoop + 1);
  return *It;
}

const Expr *OMPDependClause::getLoopData(unsigned NumLoop) const {
  assert((getDependencyKind() == OMPC_DEPEND_sink ||
          getDependencyKind() == OMPC_DEPEND_source) &&
         NumLoop < NumLoops &&
         "Expected sink or source depend + loop index must be less number of "
         "loops.");
  const auto *It = std::next(getVarRefs().end(), NumLoop + 1);
  return *It;
}

void OMPDependClause::setModifier(Expr *DepModifier) {
  *getVarRefs().end() = DepModifier;
}
Expr *OMPDependClause::getModifier() { return *getVarRefs().end(); }

unsigned OMPClauseMappableExprCommon::getComponentsTotalNumber(
    MappableExprComponentListsRef ComponentLists) {
  unsigned TotalNum = 0u;
  for (auto &C : ComponentLists)
    TotalNum += C.size();
  return TotalNum;
}

unsigned OMPClauseMappableExprCommon::getUniqueDeclarationsTotalNumber(
    ArrayRef<const ValueDecl *> Declarations) {
  unsigned TotalNum = 0u;
  llvm::SmallPtrSet<const ValueDecl *, 8> Cache;
  for (const ValueDecl *D : Declarations) {
    const ValueDecl *VD = D ? cast<ValueDecl>(D->getCanonicalDecl()) : nullptr;
    if (Cache.count(VD))
      continue;
    ++TotalNum;
    Cache.insert(VD);
  }
  return TotalNum;
}

OMPMapClause *OMPMapClause::Create(
    const ASTContext &C, const OMPVarListLocTy &Locs, ArrayRef<Expr *> Vars,
    ArrayRef<ValueDecl *> Declarations,
    MappableExprComponentListsRef ComponentLists, ArrayRef<Expr *> UDMapperRefs,
    Expr *IteratorModifier, ArrayRef<OpenMPMapModifierKind> MapModifiers,
    ArrayRef<SourceLocation> MapModifiersLoc,
    NestedNameSpecifierLoc UDMQualifierLoc, DeclarationNameInfo MapperId,
    OpenMPMapClauseKind Type, bool TypeIsImplicit, SourceLocation TypeLoc) {
  OMPMappableExprListSizeTy Sizes;
  Sizes.NumVars = Vars.size();
  Sizes.NumUniqueDeclarations = getUniqueDeclarationsTotalNumber(Declarations);
  Sizes.NumComponentLists = ComponentLists.size();
  Sizes.NumComponents = getComponentsTotalNumber(ComponentLists);

  // We need to allocate:
  // 2 x NumVars x Expr* - we have an original list expression and an associated
  // user-defined mapper for each clause list entry.
  // NumUniqueDeclarations x ValueDecl* - unique base declarations associated
  // with each component list.
  // (NumUniqueDeclarations + NumComponentLists) x unsigned - we specify the
  // number of lists for each unique declaration and the size of each component
  // list.
  // NumComponents x MappableComponent - the total of all the components in all
  // the lists.
  void *Mem = C.Allocate(
      totalSizeToAlloc<Expr *, ValueDecl *, unsigned,
                       OMPClauseMappableExprCommon::MappableComponent>(
          2 * Sizes.NumVars + 1, Sizes.NumUniqueDeclarations,
          Sizes.NumUniqueDeclarations + Sizes.NumComponentLists,
          Sizes.NumComponents));
  OMPMapClause *Clause = new (Mem)
      OMPMapClause(MapModifiers, MapModifiersLoc, UDMQualifierLoc, MapperId,
                   Type, TypeIsImplicit, TypeLoc, Locs, Sizes);

  Clause->setVarRefs(Vars);
  Clause->setUDMapperRefs(UDMapperRefs);
  Clause->setIteratorModifier(IteratorModifier);
  Clause->setClauseInfo(Declarations, ComponentLists);
  Clause->setMapType(Type);
  Clause->setMapLoc(TypeLoc);
  return Clause;
}

OMPMapClause *
OMPMapClause::CreateEmpty(const ASTContext &C,
                          const OMPMappableExprListSizeTy &Sizes) {
  void *Mem = C.Allocate(
      totalSizeToAlloc<Expr *, ValueDecl *, unsigned,
                       OMPClauseMappableExprCommon::MappableComponent>(
          2 * Sizes.NumVars + 1, Sizes.NumUniqueDeclarations,
          Sizes.NumUniqueDeclarations + Sizes.NumComponentLists,
          Sizes.NumComponents));
  OMPMapClause *Clause = new (Mem) OMPMapClause(Sizes);
  Clause->setIteratorModifier(nullptr);
  return Clause;
}

OMPToClause *OMPToClause::Create(
    const ASTContext &C, const OMPVarListLocTy &Locs, ArrayRef<Expr *> Vars,
    ArrayRef<ValueDecl *> Declarations,
    MappableExprComponentListsRef ComponentLists, ArrayRef<Expr *> UDMapperRefs,
    ArrayRef<OpenMPMotionModifierKind> MotionModifiers,
    ArrayRef<SourceLocation> MotionModifiersLoc,
    NestedNameSpecifierLoc UDMQualifierLoc, DeclarationNameInfo MapperId) {
  OMPMappableExprListSizeTy Sizes;
  Sizes.NumVars = Vars.size();
  Sizes.NumUniqueDeclarations = getUniqueDeclarationsTotalNumber(Declarations);
  Sizes.NumComponentLists = ComponentLists.size();
  Sizes.NumComponents = getComponentsTotalNumber(ComponentLists);

  // We need to allocate:
  // 2 x NumVars x Expr* - we have an original list expression and an associated
  // user-defined mapper for each clause list entry.
  // NumUniqueDeclarations x ValueDecl* - unique base declarations associated
  // with each component list.
  // (NumUniqueDeclarations + NumComponentLists) x unsigned - we specify the
  // number of lists for each unique declaration and the size of each component
  // list.
  // NumComponents x MappableComponent - the total of all the components in all
  // the lists.
  void *Mem = C.Allocate(
      totalSizeToAlloc<Expr *, ValueDecl *, unsigned,
                       OMPClauseMappableExprCommon::MappableComponent>(
          2 * Sizes.NumVars, Sizes.NumUniqueDeclarations,
          Sizes.NumUniqueDeclarations + Sizes.NumComponentLists,
          Sizes.NumComponents));

  auto *Clause = new (Mem) OMPToClause(MotionModifiers, MotionModifiersLoc,
                                       UDMQualifierLoc, MapperId, Locs, Sizes);

  Clause->setVarRefs(Vars);
  Clause->setUDMapperRefs(UDMapperRefs);
  Clause->setClauseInfo(Declarations, ComponentLists);
  return Clause;
}

OMPToClause *OMPToClause::CreateEmpty(const ASTContext &C,
                                      const OMPMappableExprListSizeTy &Sizes) {
  void *Mem = C.Allocate(
      totalSizeToAlloc<Expr *, ValueDecl *, unsigned,
                       OMPClauseMappableExprCommon::MappableComponent>(
          2 * Sizes.NumVars, Sizes.NumUniqueDeclarations,
          Sizes.NumUniqueDeclarations + Sizes.NumComponentLists,
          Sizes.NumComponents));
  return new (Mem) OMPToClause(Sizes);
}

OMPFromClause *OMPFromClause::Create(
    const ASTContext &C, const OMPVarListLocTy &Locs, ArrayRef<Expr *> Vars,
    ArrayRef<ValueDecl *> Declarations,
    MappableExprComponentListsRef ComponentLists, ArrayRef<Expr *> UDMapperRefs,
    ArrayRef<OpenMPMotionModifierKind> MotionModifiers,
    ArrayRef<SourceLocation> MotionModifiersLoc,
    NestedNameSpecifierLoc UDMQualifierLoc, DeclarationNameInfo MapperId) {
  OMPMappableExprListSizeTy Sizes;
  Sizes.NumVars = Vars.size();
  Sizes.NumUniqueDeclarations = getUniqueDeclarationsTotalNumber(Declarations);
  Sizes.NumComponentLists = ComponentLists.size();
  Sizes.NumComponents = getComponentsTotalNumber(ComponentLists);

  // We need to allocate:
  // 2 x NumVars x Expr* - we have an original list expression and an associated
  // user-defined mapper for each clause list entry.
  // NumUniqueDeclarations x ValueDecl* - unique base declarations associated
  // with each component list.
  // (NumUniqueDeclarations + NumComponentLists) x unsigned - we specify the
  // number of lists for each unique declaration and the size of each component
  // list.
  // NumComponents x MappableComponent - the total of all the components in all
  // the lists.
  void *Mem = C.Allocate(
      totalSizeToAlloc<Expr *, ValueDecl *, unsigned,
                       OMPClauseMappableExprCommon::MappableComponent>(
          2 * Sizes.NumVars, Sizes.NumUniqueDeclarations,
          Sizes.NumUniqueDeclarations + Sizes.NumComponentLists,
          Sizes.NumComponents));

  auto *Clause =
      new (Mem) OMPFromClause(MotionModifiers, MotionModifiersLoc,
                              UDMQualifierLoc, MapperId, Locs, Sizes);

  Clause->setVarRefs(Vars);
  Clause->setUDMapperRefs(UDMapperRefs);
  Clause->setClauseInfo(Declarations, ComponentLists);
  return Clause;
}

OMPFromClause *
OMPFromClause::CreateEmpty(const ASTContext &C,
                           const OMPMappableExprListSizeTy &Sizes) {
  void *Mem = C.Allocate(
      totalSizeToAlloc<Expr *, ValueDecl *, unsigned,
                       OMPClauseMappableExprCommon::MappableComponent>(
          2 * Sizes.NumVars, Sizes.NumUniqueDeclarations,
          Sizes.NumUniqueDeclarations + Sizes.NumComponentLists,
          Sizes.NumComponents));
  return new (Mem) OMPFromClause(Sizes);
}

void OMPUseDevicePtrClause::setPrivateCopies(ArrayRef<Expr *> VL) {
  assert(VL.size() == varlist_size() &&
         "Number of private copies is not the same as the preallocated buffer");
  std::copy(VL.begin(), VL.end(), varlist_end());
}

void OMPUseDevicePtrClause::setInits(ArrayRef<Expr *> VL) {
  assert(VL.size() == varlist_size() &&
         "Number of inits is not the same as the preallocated buffer");
  std::copy(VL.begin(), VL.end(), getPrivateCopies().end());
}

OMPUseDevicePtrClause *OMPUseDevicePtrClause::Create(
    const ASTContext &C, const OMPVarListLocTy &Locs, ArrayRef<Expr *> Vars,
    ArrayRef<Expr *> PrivateVars, ArrayRef<Expr *> Inits,
    ArrayRef<ValueDecl *> Declarations,
    MappableExprComponentListsRef ComponentLists) {
  OMPMappableExprListSizeTy Sizes;
  Sizes.NumVars = Vars.size();
  Sizes.NumUniqueDeclarations = getUniqueDeclarationsTotalNumber(Declarations);
  Sizes.NumComponentLists = ComponentLists.size();
  Sizes.NumComponents = getComponentsTotalNumber(ComponentLists);

  // We need to allocate:
  // NumVars x Expr* - we have an original list expression for each clause
  // list entry.
  // NumUniqueDeclarations x ValueDecl* - unique base declarations associated
  // with each component list.
  // (NumUniqueDeclarations + NumComponentLists) x unsigned - we specify the
  // number of lists for each unique declaration and the size of each component
  // list.
  // NumComponents x MappableComponent - the total of all the components in all
  // the lists.
  void *Mem = C.Allocate(
      totalSizeToAlloc<Expr *, ValueDecl *, unsigned,
                       OMPClauseMappableExprCommon::MappableComponent>(
          3 * Sizes.NumVars, Sizes.NumUniqueDeclarations,
          Sizes.NumUniqueDeclarations + Sizes.NumComponentLists,
          Sizes.NumComponents));

  OMPUseDevicePtrClause *Clause = new (Mem) OMPUseDevicePtrClause(Locs, Sizes);

  Clause->setVarRefs(Vars);
  Clause->setPrivateCopies(PrivateVars);
  Clause->setInits(Inits);
  Clause->setClauseInfo(Declarations, ComponentLists);
  return Clause;
}

OMPUseDevicePtrClause *
OMPUseDevicePtrClause::CreateEmpty(const ASTContext &C,
                                   const OMPMappableExprListSizeTy &Sizes) {
  void *Mem = C.Allocate(
      totalSizeToAlloc<Expr *, ValueDecl *, unsigned,
                       OMPClauseMappableExprCommon::MappableComponent>(
          3 * Sizes.NumVars, Sizes.NumUniqueDeclarations,
          Sizes.NumUniqueDeclarations + Sizes.NumComponentLists,
          Sizes.NumComponents));
  return new (Mem) OMPUseDevicePtrClause(Sizes);
}

OMPUseDeviceAddrClause *
OMPUseDeviceAddrClause::Create(const ASTContext &C, const OMPVarListLocTy &Locs,
                               ArrayRef<Expr *> Vars,
                               ArrayRef<ValueDecl *> Declarations,
                               MappableExprComponentListsRef ComponentLists) {
  OMPMappableExprListSizeTy Sizes;
  Sizes.NumVars = Vars.size();
  Sizes.NumUniqueDeclarations = getUniqueDeclarationsTotalNumber(Declarations);
  Sizes.NumComponentLists = ComponentLists.size();
  Sizes.NumComponents = getComponentsTotalNumber(ComponentLists);

  // We need to allocate:
  // 3 x NumVars x Expr* - we have an original list expression for each clause
  // list entry and an equal number of private copies and inits.
  // NumUniqueDeclarations x ValueDecl* - unique base declarations associated
  // with each component list.
  // (NumUniqueDeclarations + NumComponentLists) x unsigned - we specify the
  // number of lists for each unique declaration and the size of each component
  // list.
  // NumComponents x MappableComponent - the total of all the components in all
  // the lists.
  void *Mem = C.Allocate(
      totalSizeToAlloc<Expr *, ValueDecl *, unsigned,
                       OMPClauseMappableExprCommon::MappableComponent>(
          Sizes.NumVars, Sizes.NumUniqueDeclarations,
          Sizes.NumUniqueDeclarations + Sizes.NumComponentLists,
          Sizes.NumComponents));

  auto *Clause = new (Mem) OMPUseDeviceAddrClause(Locs, Sizes);

  Clause->setVarRefs(Vars);
  Clause->setClauseInfo(Declarations, ComponentLists);
  return Clause;
}

OMPUseDeviceAddrClause *
OMPUseDeviceAddrClause::CreateEmpty(const ASTContext &C,
                                    const OMPMappableExprListSizeTy &Sizes) {
  void *Mem = C.Allocate(
      totalSizeToAlloc<Expr *, ValueDecl *, unsigned,
                       OMPClauseMappableExprCommon::MappableComponent>(
          Sizes.NumVars, Sizes.NumUniqueDeclarations,
          Sizes.NumUniqueDeclarations + Sizes.NumComponentLists,
          Sizes.NumComponents));
  return new (Mem) OMPUseDeviceAddrClause(Sizes);
}

OMPIsDevicePtrClause *
OMPIsDevicePtrClause::Create(const ASTContext &C, const OMPVarListLocTy &Locs,
                             ArrayRef<Expr *> Vars,
                             ArrayRef<ValueDecl *> Declarations,
                             MappableExprComponentListsRef ComponentLists) {
  OMPMappableExprListSizeTy Sizes;
  Sizes.NumVars = Vars.size();
  Sizes.NumUniqueDeclarations = getUniqueDeclarationsTotalNumber(Declarations);
  Sizes.NumComponentLists = ComponentLists.size();
  Sizes.NumComponents = getComponentsTotalNumber(ComponentLists);

  // We need to allocate:
  // NumVars x Expr* - we have an original list expression for each clause list
  // entry.
  // NumUniqueDeclarations x ValueDecl* - unique base declarations associated
  // with each component list.
  // (NumUniqueDeclarations + NumComponentLists) x unsigned - we specify the
  // number of lists for each unique declaration and the size of each component
  // list.
  // NumComponents x MappableComponent - the total of all the components in all
  // the lists.
  void *Mem = C.Allocate(
      totalSizeToAlloc<Expr *, ValueDecl *, unsigned,
                       OMPClauseMappableExprCommon::MappableComponent>(
          Sizes.NumVars, Sizes.NumUniqueDeclarations,
          Sizes.NumUniqueDeclarations + Sizes.NumComponentLists,
          Sizes.NumComponents));

  OMPIsDevicePtrClause *Clause = new (Mem) OMPIsDevicePtrClause(Locs, Sizes);

  Clause->setVarRefs(Vars);
  Clause->setClauseInfo(Declarations, ComponentLists);
  return Clause;
}

OMPIsDevicePtrClause *
OMPIsDevicePtrClause::CreateEmpty(const ASTContext &C,
                                  const OMPMappableExprListSizeTy &Sizes) {
  void *Mem = C.Allocate(
      totalSizeToAlloc<Expr *, ValueDecl *, unsigned,
                       OMPClauseMappableExprCommon::MappableComponent>(
          Sizes.NumVars, Sizes.NumUniqueDeclarations,
          Sizes.NumUniqueDeclarations + Sizes.NumComponentLists,
          Sizes.NumComponents));
  return new (Mem) OMPIsDevicePtrClause(Sizes);
}

OMPHasDeviceAddrClause *
OMPHasDeviceAddrClause::Create(const ASTContext &C, const OMPVarListLocTy &Locs,
                               ArrayRef<Expr *> Vars,
                               ArrayRef<ValueDecl *> Declarations,
                               MappableExprComponentListsRef ComponentLists) {
  OMPMappableExprListSizeTy Sizes;
  Sizes.NumVars = Vars.size();
  Sizes.NumUniqueDeclarations = getUniqueDeclarationsTotalNumber(Declarations);
  Sizes.NumComponentLists = ComponentLists.size();
  Sizes.NumComponents = getComponentsTotalNumber(ComponentLists);

  // We need to allocate:
  // NumVars x Expr* - we have an original list expression for each clause list
  // entry.
  // NumUniqueDeclarations x ValueDecl* - unique base declarations associated
  // with each component list.
  // (NumUniqueDeclarations + NumComponentLists) x unsigned - we specify the
  // number of lists for each unique declaration and the size of each component
  // list.
  // NumComponents x MappableComponent - the total of all the components in all
  // the lists.
  void *Mem = C.Allocate(
      totalSizeToAlloc<Expr *, ValueDecl *, unsigned,
                       OMPClauseMappableExprCommon::MappableComponent>(
          Sizes.NumVars, Sizes.NumUniqueDeclarations,
          Sizes.NumUniqueDeclarations + Sizes.NumComponentLists,
          Sizes.NumComponents));

  auto *Clause = new (Mem) OMPHasDeviceAddrClause(Locs, Sizes);

  Clause->setVarRefs(Vars);
  Clause->setClauseInfo(Declarations, ComponentLists);
  return Clause;
}

OMPHasDeviceAddrClause *
OMPHasDeviceAddrClause::CreateEmpty(const ASTContext &C,
                                    const OMPMappableExprListSizeTy &Sizes) {
  void *Mem = C.Allocate(
      totalSizeToAlloc<Expr *, ValueDecl *, unsigned,
                       OMPClauseMappableExprCommon::MappableComponent>(
          Sizes.NumVars, Sizes.NumUniqueDeclarations,
          Sizes.NumUniqueDeclarations + Sizes.NumComponentLists,
          Sizes.NumComponents));
  return new (Mem) OMPHasDeviceAddrClause(Sizes);
}

OMPNontemporalClause *OMPNontemporalClause::Create(const ASTContext &C,
                                                   SourceLocation StartLoc,
                                                   SourceLocation LParenLoc,
                                                   SourceLocation EndLoc,
                                                   ArrayRef<Expr *> VL) {
  // Allocate space for nontemporal variables + private references.
  void *Mem = C.Allocate(totalSizeToAlloc<Expr *>(2 * VL.size()));
  auto *Clause =
      new (Mem) OMPNontemporalClause(StartLoc, LParenLoc, EndLoc, VL.size());
  Clause->setVarRefs(VL);
  return Clause;
}

OMPNontemporalClause *OMPNontemporalClause::CreateEmpty(const ASTContext &C,
                                                        unsigned N) {
  void *Mem = C.Allocate(totalSizeToAlloc<Expr *>(2 * N));
  return new (Mem) OMPNontemporalClause(N);
}

void OMPNontemporalClause::setPrivateRefs(ArrayRef<Expr *> VL) {
  assert(VL.size() == varlist_size() && "Number of private references is not "
                                        "the same as the preallocated buffer");
  std::copy(VL.begin(), VL.end(), varlist_end());
}

OMPInclusiveClause *OMPInclusiveClause::Create(const ASTContext &C,
                                               SourceLocation StartLoc,
                                               SourceLocation LParenLoc,
                                               SourceLocation EndLoc,
                                               ArrayRef<Expr *> VL) {
  void *Mem = C.Allocate(totalSizeToAlloc<Expr *>(VL.size()));
  auto *Clause =
      new (Mem) OMPInclusiveClause(StartLoc, LParenLoc, EndLoc, VL.size());
  Clause->setVarRefs(VL);
  return Clause;
}

OMPInclusiveClause *OMPInclusiveClause::CreateEmpty(const ASTContext &C,
                                                    unsigned N) {
  void *Mem = C.Allocate(totalSizeToAlloc<Expr *>(N));
  return new (Mem) OMPInclusiveClause(N);
}

OMPExclusiveClause *OMPExclusiveClause::Create(const ASTContext &C,
                                               SourceLocation StartLoc,
                                               SourceLocation LParenLoc,
                                               SourceLocation EndLoc,
                                               ArrayRef<Expr *> VL) {
  void *Mem = C.Allocate(totalSizeToAlloc<Expr *>(VL.size()));
  auto *Clause =
      new (Mem) OMPExclusiveClause(StartLoc, LParenLoc, EndLoc, VL.size());
  Clause->setVarRefs(VL);
  return Clause;
}

OMPExclusiveClause *OMPExclusiveClause::CreateEmpty(const ASTContext &C,
                                                    unsigned N) {
  void *Mem = C.Allocate(totalSizeToAlloc<Expr *>(N));
  return new (Mem) OMPExclusiveClause(N);
}

void OMPUsesAllocatorsClause::setAllocatorsData(
    ArrayRef<OMPUsesAllocatorsClause::Data> Data) {
  assert(Data.size() == NumOfAllocators &&
         "Size of allocators data is not the same as the preallocated buffer.");
  for (unsigned I = 0, E = Data.size(); I < E; ++I) {
    const OMPUsesAllocatorsClause::Data &D = Data[I];
    getTrailingObjects<Expr *>()[I * static_cast<int>(ExprOffsets::Total) +
                                 static_cast<int>(ExprOffsets::Allocator)] =
        D.Allocator;
    getTrailingObjects<Expr *>()[I * static_cast<int>(ExprOffsets::Total) +
                                 static_cast<int>(
                                     ExprOffsets::AllocatorTraits)] =
        D.AllocatorTraits;
    getTrailingObjects<
        SourceLocation>()[I * static_cast<int>(ParenLocsOffsets::Total) +
                          static_cast<int>(ParenLocsOffsets::LParen)] =
        D.LParenLoc;
    getTrailingObjects<
        SourceLocation>()[I * static_cast<int>(ParenLocsOffsets::Total) +
                          static_cast<int>(ParenLocsOffsets::RParen)] =
        D.RParenLoc;
  }
}

OMPUsesAllocatorsClause::Data
OMPUsesAllocatorsClause::getAllocatorData(unsigned I) const {
  OMPUsesAllocatorsClause::Data Data;
  Data.Allocator =
      getTrailingObjects<Expr *>()[I * static_cast<int>(ExprOffsets::Total) +
                                   static_cast<int>(ExprOffsets::Allocator)];
  Data.AllocatorTraits =
      getTrailingObjects<Expr *>()[I * static_cast<int>(ExprOffsets::Total) +
                                   static_cast<int>(
                                       ExprOffsets::AllocatorTraits)];
  Data.LParenLoc = getTrailingObjects<
      SourceLocation>()[I * static_cast<int>(ParenLocsOffsets::Total) +
                        static_cast<int>(ParenLocsOffsets::LParen)];
  Data.RParenLoc = getTrailingObjects<
      SourceLocation>()[I * static_cast<int>(ParenLocsOffsets::Total) +
                        static_cast<int>(ParenLocsOffsets::RParen)];
  return Data;
}

OMPUsesAllocatorsClause *
OMPUsesAllocatorsClause::Create(const ASTContext &C, SourceLocation StartLoc,
                                SourceLocation LParenLoc, SourceLocation EndLoc,
                                ArrayRef<OMPUsesAllocatorsClause::Data> Data) {
  void *Mem = C.Allocate(totalSizeToAlloc<Expr *, SourceLocation>(
      static_cast<int>(ExprOffsets::Total) * Data.size(),
      static_cast<int>(ParenLocsOffsets::Total) * Data.size()));
  auto *Clause = new (Mem)
      OMPUsesAllocatorsClause(StartLoc, LParenLoc, EndLoc, Data.size());
  Clause->setAllocatorsData(Data);
  return Clause;
}

OMPUsesAllocatorsClause *
OMPUsesAllocatorsClause::CreateEmpty(const ASTContext &C, unsigned N) {
  void *Mem = C.Allocate(totalSizeToAlloc<Expr *, SourceLocation>(
      static_cast<int>(ExprOffsets::Total) * N,
      static_cast<int>(ParenLocsOffsets::Total) * N));
  return new (Mem) OMPUsesAllocatorsClause(N);
}

OMPAffinityClause *
OMPAffinityClause::Create(const ASTContext &C, SourceLocation StartLoc,
                          SourceLocation LParenLoc, SourceLocation ColonLoc,
                          SourceLocation EndLoc, Expr *Modifier,
                          ArrayRef<Expr *> Locators) {
  void *Mem = C.Allocate(totalSizeToAlloc<Expr *>(Locators.size() + 1));
  auto *Clause = new (Mem)
      OMPAffinityClause(StartLoc, LParenLoc, ColonLoc, EndLoc, Locators.size());
  Clause->setModifier(Modifier);
  Clause->setVarRefs(Locators);
  return Clause;
}

OMPAffinityClause *OMPAffinityClause::CreateEmpty(const ASTContext &C,
                                                  unsigned N) {
  void *Mem = C.Allocate(totalSizeToAlloc<Expr *>(N + 1));
  return new (Mem) OMPAffinityClause(N);
}

OMPInitClause *OMPInitClause::Create(const ASTContext &C, Expr *InteropVar,
                                     OMPInteropInfo &InteropInfo,
                                     SourceLocation StartLoc,
                                     SourceLocation LParenLoc,
                                     SourceLocation VarLoc,
                                     SourceLocation EndLoc) {

  void *Mem =
      C.Allocate(totalSizeToAlloc<Expr *>(InteropInfo.PreferTypes.size() + 1));
  auto *Clause = new (Mem) OMPInitClause(
      InteropInfo.IsTarget, InteropInfo.IsTargetSync, StartLoc, LParenLoc,
      VarLoc, EndLoc, InteropInfo.PreferTypes.size() + 1);
  Clause->setInteropVar(InteropVar);
  llvm::copy(InteropInfo.PreferTypes, Clause->getTrailingObjects<Expr *>() + 1);
  return Clause;
}

OMPInitClause *OMPInitClause::CreateEmpty(const ASTContext &C, unsigned N) {
  void *Mem = C.Allocate(totalSizeToAlloc<Expr *>(N));
  return new (Mem) OMPInitClause(N);
}

OMPBindClause *
OMPBindClause::Create(const ASTContext &C, OpenMPBindClauseKind K,
                      SourceLocation KLoc, SourceLocation StartLoc,
                      SourceLocation LParenLoc, SourceLocation EndLoc) {
  return new (C) OMPBindClause(K, KLoc, StartLoc, LParenLoc, EndLoc);
}

OMPBindClause *OMPBindClause::CreateEmpty(const ASTContext &C) {
  return new (C) OMPBindClause();
}

OMPDoacrossClause *
OMPDoacrossClause::Create(const ASTContext &C, SourceLocation StartLoc,
                          SourceLocation LParenLoc, SourceLocation EndLoc,
                          OpenMPDoacrossClauseModifier DepType,
                          SourceLocation DepLoc, SourceLocation ColonLoc,
                          ArrayRef<Expr *> VL, unsigned NumLoops) {
  void *Mem = C.Allocate(totalSizeToAlloc<Expr *>(VL.size() + NumLoops),
                         alignof(OMPDoacrossClause));
  OMPDoacrossClause *Clause = new (Mem)
      OMPDoacrossClause(StartLoc, LParenLoc, EndLoc, VL.size(), NumLoops);
  Clause->setDependenceType(DepType);
  Clause->setDependenceLoc(DepLoc);
  Clause->setColonLoc(ColonLoc);
  Clause->setVarRefs(VL);
  for (unsigned I = 0; I < NumLoops; ++I)
    Clause->setLoopData(I, nullptr);
  return Clause;
}

OMPDoacrossClause *OMPDoacrossClause::CreateEmpty(const ASTContext &C,
                                                  unsigned N,
                                                  unsigned NumLoops) {
  void *Mem = C.Allocate(totalSizeToAlloc<Expr *>(N + NumLoops),
                         alignof(OMPDoacrossClause));
  return new (Mem) OMPDoacrossClause(N, NumLoops);
}

void OMPDoacrossClause::setLoopData(unsigned NumLoop, Expr *Cnt) {
  assert(NumLoop < NumLoops && "Loop index must be less number of loops.");
  auto *It = std::next(getVarRefs().end(), NumLoop);
  *It = Cnt;
}

Expr *OMPDoacrossClause::getLoopData(unsigned NumLoop) {
  assert(NumLoop < NumLoops && "Loop index must be less number of loops.");
  auto *It = std::next(getVarRefs().end(), NumLoop);
  return *It;
}

const Expr *OMPDoacrossClause::getLoopData(unsigned NumLoop) const {
  assert(NumLoop < NumLoops && "Loop index must be less number of loops.");
  const auto *It = std::next(getVarRefs().end(), NumLoop);
  return *It;
}

//===----------------------------------------------------------------------===//
//  OpenMP clauses printing methods
//===----------------------------------------------------------------------===//

void OMPClausePrinter::VisitOMPIfClause(OMPIfClause *Node) {
  OS << "if(";
  if (Node->getNameModifier() != OMPD_unknown)
    OS << getOpenMPDirectiveName(Node->getNameModifier()) << ": ";
  Node->getCondition()->printPretty(OS, nullptr, Policy, 0);
  OS << ")";
}

void OMPClausePrinter::VisitOMPFinalClause(OMPFinalClause *Node) {
  OS << "final(";
  Node->getCondition()->printPretty(OS, nullptr, Policy, 0);
  OS << ")";
}

void OMPClausePrinter::VisitOMPNumThreadsClause(OMPNumThreadsClause *Node) {
  OS << "num_threads(";
  Node->getNumThreads()->printPretty(OS, nullptr, Policy, 0);
  OS << ")";
}

void OMPClausePrinter::VisitOMPAlignClause(OMPAlignClause *Node) {
  OS << "align(";
  Node->getAlignment()->printPretty(OS, nullptr, Policy, 0);
  OS << ")";
}

void OMPClausePrinter::VisitOMPSafelenClause(OMPSafelenClause *Node) {
  OS << "safelen(";
  Node->getSafelen()->printPretty(OS, nullptr, Policy, 0);
  OS << ")";
}

void OMPClausePrinter::VisitOMPSimdlenClause(OMPSimdlenClause *Node) {
  OS << "simdlen(";
  Node->getSimdlen()->printPretty(OS, nullptr, Policy, 0);
  OS << ")";
}

void OMPClausePrinter::VisitOMPSizesClause(OMPSizesClause *Node) {
  OS << "sizes(";
  bool First = true;
  for (auto *Size : Node->getSizesRefs()) {
    if (!First)
      OS << ", ";
    Size->printPretty(OS, nullptr, Policy, 0);
    First = false;
  }
  OS << ")";
}

void OMPClausePrinter::VisitOMPFullClause(OMPFullClause *Node) { OS << "full"; }

void OMPClausePrinter::VisitOMPPartialClause(OMPPartialClause *Node) {
  OS << "partial";

  if (Expr *Factor = Node->getFactor()) {
    OS << '(';
    Factor->printPretty(OS, nullptr, Policy, 0);
    OS << ')';
  }
}

void OMPClausePrinter::VisitOMPAllocatorClause(OMPAllocatorClause *Node) {
  OS << "allocator(";
  Node->getAllocator()->printPretty(OS, nullptr, Policy, 0);
  OS << ")";
}

void OMPClausePrinter::VisitOMPCollapseClause(OMPCollapseClause *Node) {
  OS << "collapse(";
  Node->getNumForLoops()->printPretty(OS, nullptr, Policy, 0);
  OS << ")";
}

void OMPClausePrinter::VisitOMPDetachClause(OMPDetachClause *Node) {
  OS << "detach(";
  Node->getEventHandler()->printPretty(OS, nullptr, Policy, 0);
  OS << ")";
}

void OMPClausePrinter::VisitOMPDefaultClause(OMPDefaultClause *Node) {
  OS << "default("
     << getOpenMPSimpleClauseTypeName(OMPC_default,
                                      unsigned(Node->getDefaultKind()))
     << ")";
}

void OMPClausePrinter::VisitOMPProcBindClause(OMPProcBindClause *Node) {
  OS << "proc_bind("
     << getOpenMPSimpleClauseTypeName(OMPC_proc_bind,
                                      unsigned(Node->getProcBindKind()))
     << ")";
}

void OMPClausePrinter::VisitOMPUnifiedAddressClause(OMPUnifiedAddressClause *) {
  OS << "unified_address";
}

void OMPClausePrinter::VisitOMPUnifiedSharedMemoryClause(
    OMPUnifiedSharedMemoryClause *) {
  OS << "unified_shared_memory";
}

void OMPClausePrinter::VisitOMPReverseOffloadClause(OMPReverseOffloadClause *) {
  OS << "reverse_offload";
}

void OMPClausePrinter::VisitOMPDynamicAllocatorsClause(
    OMPDynamicAllocatorsClause *) {
  OS << "dynamic_allocators";
}

void OMPClausePrinter::VisitOMPAtomicDefaultMemOrderClause(
    OMPAtomicDefaultMemOrderClause *Node) {
  OS << "atomic_default_mem_order("
     << getOpenMPSimpleClauseTypeName(OMPC_atomic_default_mem_order,
                                      Node->getAtomicDefaultMemOrderKind())
     << ")";
}

void OMPClausePrinter::VisitOMPAtClause(OMPAtClause *Node) {
  OS << "at(" << getOpenMPSimpleClauseTypeName(OMPC_at, Node->getAtKind())
     << ")";
}

void OMPClausePrinter::VisitOMPSeverityClause(OMPSeverityClause *Node) {
  OS << "severity("
     << getOpenMPSimpleClauseTypeName(OMPC_severity, Node->getSeverityKind())
     << ")";
}

void OMPClausePrinter::VisitOMPMessageClause(OMPMessageClause *Node) {
  OS << "message(\""
     << cast<StringLiteral>(Node->getMessageString())->getString() << "\")";
}

void OMPClausePrinter::VisitOMPScheduleClause(OMPScheduleClause *Node) {
  OS << "schedule(";
  if (Node->getFirstScheduleModifier() != OMPC_SCHEDULE_MODIFIER_unknown) {
    OS << getOpenMPSimpleClauseTypeName(OMPC_schedule,
                                        Node->getFirstScheduleModifier());
    if (Node->getSecondScheduleModifier() != OMPC_SCHEDULE_MODIFIER_unknown) {
      OS << ", ";
      OS << getOpenMPSimpleClauseTypeName(OMPC_schedule,
                                          Node->getSecondScheduleModifier());
    }
    OS << ": ";
  }
  OS << getOpenMPSimpleClauseTypeName(OMPC_schedule, Node->getScheduleKind());
  if (auto *E = Node->getChunkSize()) {
    OS << ", ";
    E->printPretty(OS, nullptr, Policy);
  }
  OS << ")";
}

void OMPClausePrinter::VisitOMPOrderedClause(OMPOrderedClause *Node) {
  OS << "ordered";
  if (auto *Num = Node->getNumForLoops()) {
    OS << "(";
    Num->printPretty(OS, nullptr, Policy, 0);
    OS << ")";
  }
}

void OMPClausePrinter::VisitOMPNowaitClause(OMPNowaitClause *) {
  OS << "nowait";
}

void OMPClausePrinter::VisitOMPUntiedClause(OMPUntiedClause *) {
  OS << "untied";
}

void OMPClausePrinter::VisitOMPNogroupClause(OMPNogroupClause *) {
  OS << "nogroup";
}

void OMPClausePrinter::VisitOMPMergeableClause(OMPMergeableClause *) {
  OS << "mergeable";
}

void OMPClausePrinter::VisitOMPReadClause(OMPReadClause *) { OS << "read"; }

void OMPClausePrinter::VisitOMPWriteClause(OMPWriteClause *) { OS << "write"; }

void OMPClausePrinter::VisitOMPUpdateClause(OMPUpdateClause *Node) {
  OS << "update";
  if (Node->isExtended()) {
    OS << "(";
    OS << getOpenMPSimpleClauseTypeName(Node->getClauseKind(),
                                        Node->getDependencyKind());
    OS << ")";
  }
}

void OMPClausePrinter::VisitOMPCaptureClause(OMPCaptureClause *) {
  OS << "capture";
}

void OMPClausePrinter::VisitOMPCompareClause(OMPCompareClause *) {
  OS << "compare";
}

void OMPClausePrinter::VisitOMPFailClause(OMPFailClause *Node) {
  OS << "fail";
  if (Node) {
    OS << "(";
    OS << getOpenMPSimpleClauseTypeName(
        Node->getClauseKind(), static_cast<int>(Node->getFailParameter()));
    OS << ")";
  }
}

void OMPClausePrinter::VisitOMPSeqCstClause(OMPSeqCstClause *) {
  OS << "seq_cst";
}

void OMPClausePrinter::VisitOMPAcqRelClause(OMPAcqRelClause *) {
  OS << "acq_rel";
}

void OMPClausePrinter::VisitOMPAcquireClause(OMPAcquireClause *) {
  OS << "acquire";
}

void OMPClausePrinter::VisitOMPReleaseClause(OMPReleaseClause *) {
  OS << "release";
}

void OMPClausePrinter::VisitOMPRelaxedClause(OMPRelaxedClause *) {
  OS << "relaxed";
}

void OMPClausePrinter::VisitOMPWeakClause(OMPWeakClause *) { OS << "weak"; }

void OMPClausePrinter::VisitOMPThreadsClause(OMPThreadsClause *) {
  OS << "threads";
}

void OMPClausePrinter::VisitOMPSIMDClause(OMPSIMDClause *) { OS << "simd"; }

void OMPClausePrinter::VisitOMPDeviceClause(OMPDeviceClause *Node) {
  OS << "device(";
  OpenMPDeviceClauseModifier Modifier = Node->getModifier();
  if (Modifier != OMPC_DEVICE_unknown) {
    OS << getOpenMPSimpleClauseTypeName(Node->getClauseKind(), Modifier)
       << ": ";
  }
  Node->getDevice()->printPretty(OS, nullptr, Policy, 0);
  OS << ")";
}

void OMPClausePrinter::VisitOMPNumTeamsClause(OMPNumTeamsClause *Node) {
  OS << "num_teams(";
  Node->getNumTeams()->printPretty(OS, nullptr, Policy, 0);
  OS << ")";
}

void OMPClausePrinter::VisitOMPThreadLimitClause(OMPThreadLimitClause *Node) {
  OS << "thread_limit(";
  Node->getThreadLimit()->printPretty(OS, nullptr, Policy, 0);
  OS << ")";
}

void OMPClausePrinter::VisitOMPPriorityClause(OMPPriorityClause *Node) {
  OS << "priority(";
  Node->getPriority()->printPretty(OS, nullptr, Policy, 0);
  OS << ")";
}

void OMPClausePrinter::VisitOMPGrainsizeClause(OMPGrainsizeClause *Node) {
  OS << "grainsize(";
  OpenMPGrainsizeClauseModifier Modifier = Node->getModifier();
  if (Modifier != OMPC_GRAINSIZE_unknown) {
    OS << getOpenMPSimpleClauseTypeName(Node->getClauseKind(), Modifier)
       << ": ";
  }
  Node->getGrainsize()->printPretty(OS, nullptr, Policy, 0);
  OS << ")";
}

void OMPClausePrinter::VisitOMPNumTasksClause(OMPNumTasksClause *Node) {
  OS << "num_tasks(";
  OpenMPNumTasksClauseModifier Modifier = Node->getModifier();
  if (Modifier != OMPC_NUMTASKS_unknown) {
    OS << getOpenMPSimpleClauseTypeName(Node->getClauseKind(), Modifier)
       << ": ";
  }
  Node->getNumTasks()->printPretty(OS, nullptr, Policy, 0);
  OS << ")";
}

void OMPClausePrinter::VisitOMPHintClause(OMPHintClause *Node) {
  OS << "hint(";
  Node->getHint()->printPretty(OS, nullptr, Policy, 0);
  OS << ")";
}

void OMPClausePrinter::VisitOMPInitClause(OMPInitClause *Node) {
  OS << "init(";
  bool First = true;
  for (const Expr *E : Node->prefs()) {
    if (First)
      OS << "prefer_type(";
    else
      OS << ",";
    E->printPretty(OS, nullptr, Policy);
    First = false;
  }
  if (!First)
    OS << "), ";
  if (Node->getIsTarget())
    OS << "target";
  if (Node->getIsTargetSync()) {
    if (Node->getIsTarget())
      OS << ", ";
    OS << "targetsync";
  }
  OS << " : ";
  Node->getInteropVar()->printPretty(OS, nullptr, Policy);
  OS << ")";
}

void OMPClausePrinter::VisitOMPUseClause(OMPUseClause *Node) {
  OS << "use(";
  Node->getInteropVar()->printPretty(OS, nullptr, Policy);
  OS << ")";
}

void OMPClausePrinter::VisitOMPDestroyClause(OMPDestroyClause *Node) {
  OS << "destroy";
  if (Expr *E = Node->getInteropVar()) {
    OS << "(";
    E->printPretty(OS, nullptr, Policy);
    OS << ")";
  }
}

void OMPClausePrinter::VisitOMPNovariantsClause(OMPNovariantsClause *Node) {
  OS << "novariants";
  if (Expr *E = Node->getCondition()) {
    OS << "(";
    E->printPretty(OS, nullptr, Policy, 0);
    OS << ")";
  }
}

void OMPClausePrinter::VisitOMPNocontextClause(OMPNocontextClause *Node) {
  OS << "nocontext";
  if (Expr *E = Node->getCondition()) {
    OS << "(";
    E->printPretty(OS, nullptr, Policy, 0);
    OS << ")";
  }
}

template<typename T>
void OMPClausePrinter::VisitOMPClauseList(T *Node, char StartSym) {
  for (typename T::varlist_iterator I = Node->varlist_begin(),
                                    E = Node->varlist_end();
       I != E; ++I) {
    assert(*I && "Expected non-null Stmt");
    OS << (I == Node->varlist_begin() ? StartSym : ',');
    if (auto *DRE = dyn_cast<DeclRefExpr>(*I)) {
      if (isa<OMPCapturedExprDecl>(DRE->getDecl()))
        DRE->printPretty(OS, nullptr, Policy, 0);
      else
        DRE->getDecl()->printQualifiedName(OS);
    } else
      (*I)->printPretty(OS, nullptr, Policy, 0);
  }
}

void OMPClausePrinter::VisitOMPAllocateClause(OMPAllocateClause *Node) {
  if (Node->varlist_empty())
    return;
  OS << "allocate";
  if (Expr *Allocator = Node->getAllocator()) {
    OS << "(";
    Allocator->printPretty(OS, nullptr, Policy, 0);
    OS << ":";
    VisitOMPClauseList(Node, ' ');
  } else {
    VisitOMPClauseList(Node, '(');
  }
  OS << ")";
}

void OMPClausePrinter::VisitOMPPrivateClause(OMPPrivateClause *Node) {
  if (!Node->varlist_empty()) {
    OS << "private";
    VisitOMPClauseList(Node, '(');
    OS << ")";
  }
}

void OMPClausePrinter::VisitOMPFirstprivateClause(OMPFirstprivateClause *Node) {
  if (!Node->varlist_empty()) {
    OS << "firstprivate";
    VisitOMPClauseList(Node, '(');
    OS << ")";
  }
}

void OMPClausePrinter::VisitOMPLastprivateClause(OMPLastprivateClause *Node) {
  if (!Node->varlist_empty()) {
    OS << "lastprivate";
    OpenMPLastprivateModifier LPKind = Node->getKind();
    if (LPKind != OMPC_LASTPRIVATE_unknown) {
      OS << "("
         << getOpenMPSimpleClauseTypeName(OMPC_lastprivate, Node->getKind())
         << ":";
    }
    VisitOMPClauseList(Node, LPKind == OMPC_LASTPRIVATE_unknown ? '(' : ' ');
    OS << ")";
  }
}

void OMPClausePrinter::VisitOMPSharedClause(OMPSharedClause *Node) {
  if (!Node->varlist_empty()) {
    OS << "shared";
    VisitOMPClauseList(Node, '(');
    OS << ")";
  }
}

void OMPClausePrinter::VisitOMPReductionClause(OMPReductionClause *Node) {
  if (!Node->varlist_empty()) {
    OS << "reduction(";
    if (Node->getModifierLoc().isValid())
      OS << getOpenMPSimpleClauseTypeName(OMPC_reduction, Node->getModifier())
         << ", ";
    NestedNameSpecifier *QualifierLoc =
        Node->getQualifierLoc().getNestedNameSpecifier();
    OverloadedOperatorKind OOK =
        Node->getNameInfo().getName().getCXXOverloadedOperator();
    if (QualifierLoc == nullptr && OOK != OO_None) {
      // Print reduction identifier in C format
      OS << getOperatorSpelling(OOK);
    } else {
      // Use C++ format
      if (QualifierLoc != nullptr)
        QualifierLoc->print(OS, Policy);
      OS << Node->getNameInfo();
    }
    OS << ":";
    VisitOMPClauseList(Node, ' ');
    OS << ")";
  }
}

void OMPClausePrinter::VisitOMPTaskReductionClause(
    OMPTaskReductionClause *Node) {
  if (!Node->varlist_empty()) {
    OS << "task_reduction(";
    NestedNameSpecifier *QualifierLoc =
        Node->getQualifierLoc().getNestedNameSpecifier();
    OverloadedOperatorKind OOK =
        Node->getNameInfo().getName().getCXXOverloadedOperator();
    if (QualifierLoc == nullptr && OOK != OO_None) {
      // Print reduction identifier in C format
      OS << getOperatorSpelling(OOK);
    } else {
      // Use C++ format
      if (QualifierLoc != nullptr)
        QualifierLoc->print(OS, Policy);
      OS << Node->getNameInfo();
    }
    OS << ":";
    VisitOMPClauseList(Node, ' ');
    OS << ")";
  }
}

void OMPClausePrinter::VisitOMPInReductionClause(OMPInReductionClause *Node) {
  if (!Node->varlist_empty()) {
    OS << "in_reduction(";
    NestedNameSpecifier *QualifierLoc =
        Node->getQualifierLoc().getNestedNameSpecifier();
    OverloadedOperatorKind OOK =
        Node->getNameInfo().getName().getCXXOverloadedOperator();
    if (QualifierLoc == nullptr && OOK != OO_None) {
      // Print reduction identifier in C format
      OS << getOperatorSpelling(OOK);
    } else {
      // Use C++ format
      if (QualifierLoc != nullptr)
        QualifierLoc->print(OS, Policy);
      OS << Node->getNameInfo();
    }
    OS << ":";
    VisitOMPClauseList(Node, ' ');
    OS << ")";
  }
}

void OMPClausePrinter::VisitOMPLinearClause(OMPLinearClause *Node) {
  if (!Node->varlist_empty()) {
    OS << "linear";
    VisitOMPClauseList(Node, '(');
    if (Node->getModifierLoc().isValid() || Node->getStep() != nullptr) {
      OS << ": ";
    }
    if (Node->getModifierLoc().isValid()) {
      OS << getOpenMPSimpleClauseTypeName(OMPC_linear, Node->getModifier());
    }
    if (Node->getStep() != nullptr) {
      if (Node->getModifierLoc().isValid()) {
        OS << ", ";
      }
      OS << "step(";
      Node->getStep()->printPretty(OS, nullptr, Policy, 0);
      OS << ")";
    }
    OS << ")";
  }
}

void OMPClausePrinter::VisitOMPAlignedClause(OMPAlignedClause *Node) {
  if (!Node->varlist_empty()) {
    OS << "aligned";
    VisitOMPClauseList(Node, '(');
    if (Node->getAlignment() != nullptr) {
      OS << ": ";
      Node->getAlignment()->printPretty(OS, nullptr, Policy, 0);
    }
    OS << ")";
  }
}

void OMPClausePrinter::VisitOMPCopyinClause(OMPCopyinClause *Node) {
  if (!Node->varlist_empty()) {
    OS << "copyin";
    VisitOMPClauseList(Node, '(');
    OS << ")";
  }
}

void OMPClausePrinter::VisitOMPCopyprivateClause(OMPCopyprivateClause *Node) {
  if (!Node->varlist_empty()) {
    OS << "copyprivate";
    VisitOMPClauseList(Node, '(');
    OS << ")";
  }
}

void OMPClausePrinter::VisitOMPFlushClause(OMPFlushClause *Node) {
  if (!Node->varlist_empty()) {
    VisitOMPClauseList(Node, '(');
    OS << ")";
  }
}

void OMPClausePrinter::VisitOMPDepobjClause(OMPDepobjClause *Node) {
  OS << "(";
  Node->getDepobj()->printPretty(OS, nullptr, Policy, 0);
  OS << ")";
}

void OMPClausePrinter::VisitOMPDependClause(OMPDependClause *Node) {
  OS << "depend(";
  if (Expr *DepModifier = Node->getModifier()) {
    DepModifier->printPretty(OS, nullptr, Policy);
    OS << ", ";
  }
  OpenMPDependClauseKind DepKind = Node->getDependencyKind();
  OpenMPDependClauseKind PrintKind = DepKind;
  bool IsOmpAllMemory = false;
  if (PrintKind == OMPC_DEPEND_outallmemory) {
    PrintKind = OMPC_DEPEND_out;
    IsOmpAllMemory = true;
  } else if (PrintKind == OMPC_DEPEND_inoutallmemory) {
    PrintKind = OMPC_DEPEND_inout;
    IsOmpAllMemory = true;
  }
  OS << getOpenMPSimpleClauseTypeName(Node->getClauseKind(), PrintKind);
  if (!Node->varlist_empty() || IsOmpAllMemory)
    OS << " :";
  VisitOMPClauseList(Node, ' ');
  if (IsOmpAllMemory) {
    OS << (Node->varlist_empty() ? " " : ",");
    OS << "omp_all_memory";
  }
  OS << ")";
}

template <typename T>
static void PrintMapper(raw_ostream &OS, T *Node,
                        const PrintingPolicy &Policy) {
  OS << '(';
  NestedNameSpecifier *MapperNNS =
      Node->getMapperQualifierLoc().getNestedNameSpecifier();
  if (MapperNNS)
    MapperNNS->print(OS, Policy);
  OS << Node->getMapperIdInfo() << ')';
}

template <typename T>
static void PrintIterator(raw_ostream &OS, T *Node,
                          const PrintingPolicy &Policy) {
  if (Expr *IteratorModifier = Node->getIteratorModifier())
    IteratorModifier->printPretty(OS, nullptr, Policy);
}

void OMPClausePrinter::VisitOMPMapClause(OMPMapClause *Node) {
  if (!Node->varlist_empty()) {
    OS << "map(";
    if (Node->getMapType() != OMPC_MAP_unknown) {
      for (unsigned I = 0; I < NumberOfOMPMapClauseModifiers; ++I) {
        if (Node->getMapTypeModifier(I) != OMPC_MAP_MODIFIER_unknown) {
          if (Node->getMapTypeModifier(I) == OMPC_MAP_MODIFIER_iterator) {
            PrintIterator(OS, Node, Policy);
          } else {
            OS << getOpenMPSimpleClauseTypeName(OMPC_map,
                                                Node->getMapTypeModifier(I));
            if (Node->getMapTypeModifier(I) == OMPC_MAP_MODIFIER_mapper)
              PrintMapper(OS, Node, Policy);
          }
          OS << ',';
        }
      }
      OS << getOpenMPSimpleClauseTypeName(OMPC_map, Node->getMapType());
      OS << ':';
    }
    VisitOMPClauseList(Node, ' ');
    OS << ")";
  }
}

template <typename T> void OMPClausePrinter::VisitOMPMotionClause(T *Node) {
  if (Node->varlist_empty())
    return;
  OS << getOpenMPClauseName(Node->getClauseKind());
  unsigned ModifierCount = 0;
  for (unsigned I = 0; I < NumberOfOMPMotionModifiers; ++I) {
    if (Node->getMotionModifier(I) != OMPC_MOTION_MODIFIER_unknown)
      ++ModifierCount;
  }
  if (ModifierCount) {
    OS << '(';
    for (unsigned I = 0; I < NumberOfOMPMotionModifiers; ++I) {
      if (Node->getMotionModifier(I) != OMPC_MOTION_MODIFIER_unknown) {
        OS << getOpenMPSimpleClauseTypeName(Node->getClauseKind(),
                                            Node->getMotionModifier(I));
        if (Node->getMotionModifier(I) == OMPC_MOTION_MODIFIER_mapper)
          PrintMapper(OS, Node, Policy);
        if (I < ModifierCount - 1)
          OS << ", ";
      }
    }
    OS << ':';
    VisitOMPClauseList(Node, ' ');
  } else {
    VisitOMPClauseList(Node, '(');
  }
  OS << ")";
}

void OMPClausePrinter::VisitOMPToClause(OMPToClause *Node) {
  VisitOMPMotionClause(Node);
}

void OMPClausePrinter::VisitOMPFromClause(OMPFromClause *Node) {
  VisitOMPMotionClause(Node);
}

void OMPClausePrinter::VisitOMPDistScheduleClause(OMPDistScheduleClause *Node) {
  OS << "dist_schedule(" << getOpenMPSimpleClauseTypeName(
                           OMPC_dist_schedule, Node->getDistScheduleKind());
  if (auto *E = Node->getChunkSize()) {
    OS << ", ";
    E->printPretty(OS, nullptr, Policy);
  }
  OS << ")";
}

void OMPClausePrinter::VisitOMPDefaultmapClause(OMPDefaultmapClause *Node) {
  OS << "defaultmap(";
  OS << getOpenMPSimpleClauseTypeName(OMPC_defaultmap,
                                      Node->getDefaultmapModifier());
  if (Node->getDefaultmapKind() != OMPC_DEFAULTMAP_unknown) {
    OS << ": ";
    OS << getOpenMPSimpleClauseTypeName(OMPC_defaultmap,
                                        Node->getDefaultmapKind());
  }
  OS << ")";
}

void OMPClausePrinter::VisitOMPUseDevicePtrClause(OMPUseDevicePtrClause *Node) {
  if (!Node->varlist_empty()) {
    OS << "use_device_ptr";
    VisitOMPClauseList(Node, '(');
    OS << ")";
  }
}

void OMPClausePrinter::VisitOMPUseDeviceAddrClause(
    OMPUseDeviceAddrClause *Node) {
  if (!Node->varlist_empty()) {
    OS << "use_device_addr";
    VisitOMPClauseList(Node, '(');
    OS << ")";
  }
}

void OMPClausePrinter::VisitOMPIsDevicePtrClause(OMPIsDevicePtrClause *Node) {
  if (!Node->varlist_empty()) {
    OS << "is_device_ptr";
    VisitOMPClauseList(Node, '(');
    OS << ")";
  }
}

void OMPClausePrinter::VisitOMPHasDeviceAddrClause(OMPHasDeviceAddrClause *Node) {
  if (!Node->varlist_empty()) {
    OS << "has_device_addr";
    VisitOMPClauseList(Node, '(');
    OS << ")";
  }
}

void OMPClausePrinter::VisitOMPNontemporalClause(OMPNontemporalClause *Node) {
  if (!Node->varlist_empty()) {
    OS << "nontemporal";
    VisitOMPClauseList(Node, '(');
    OS << ")";
  }
}

void OMPClausePrinter::VisitOMPOrderClause(OMPOrderClause *Node) {
  OS << "order(";
  if (Node->getModifier() != OMPC_ORDER_MODIFIER_unknown) {
    OS << getOpenMPSimpleClauseTypeName(OMPC_order, Node->getModifier());
    OS << ": ";
  }
  OS << getOpenMPSimpleClauseTypeName(OMPC_order, Node->getKind()) << ")";
}

void OMPClausePrinter::VisitOMPInclusiveClause(OMPInclusiveClause *Node) {
  if (!Node->varlist_empty()) {
    OS << "inclusive";
    VisitOMPClauseList(Node, '(');
    OS << ")";
  }
}

void OMPClausePrinter::VisitOMPExclusiveClause(OMPExclusiveClause *Node) {
  if (!Node->varlist_empty()) {
    OS << "exclusive";
    VisitOMPClauseList(Node, '(');
    OS << ")";
  }
}

void OMPClausePrinter::VisitOMPUsesAllocatorsClause(
    OMPUsesAllocatorsClause *Node) {
  if (Node->getNumberOfAllocators() == 0)
    return;
  OS << "uses_allocators(";
  for (unsigned I = 0, E = Node->getNumberOfAllocators(); I < E; ++I) {
    OMPUsesAllocatorsClause::Data Data = Node->getAllocatorData(I);
    Data.Allocator->printPretty(OS, nullptr, Policy);
    if (Data.AllocatorTraits) {
      OS << "(";
      Data.AllocatorTraits->printPretty(OS, nullptr, Policy);
      OS << ")";
    }
    if (I < E - 1)
      OS << ",";
  }
  OS << ")";
}

void OMPClausePrinter::VisitOMPAffinityClause(OMPAffinityClause *Node) {
  if (Node->varlist_empty())
    return;
  OS << "affinity";
  char StartSym = '(';
  if (Expr *Modifier = Node->getModifier()) {
    OS << "(";
    Modifier->printPretty(OS, nullptr, Policy);
    OS << " :";
    StartSym = ' ';
  }
  VisitOMPClauseList(Node, StartSym);
  OS << ")";
}

void OMPClausePrinter::VisitOMPFilterClause(OMPFilterClause *Node) {
  OS << "filter(";
  Node->getThreadID()->printPretty(OS, nullptr, Policy, 0);
  OS << ")";
}

void OMPClausePrinter::VisitOMPBindClause(OMPBindClause *Node) {
  OS << "bind("
     << getOpenMPSimpleClauseTypeName(OMPC_bind, unsigned(Node->getBindKind()))
     << ")";
}

void OMPClausePrinter::VisitOMPXDynCGroupMemClause(
    OMPXDynCGroupMemClause *Node) {
  OS << "ompx_dyn_cgroup_mem(";
  Node->getSize()->printPretty(OS, nullptr, Policy, 0);
  OS << ")";
}

void OMPClausePrinter::VisitOMPDoacrossClause(OMPDoacrossClause *Node) {
  OS << "doacross(";
  OpenMPDoacrossClauseModifier DepType = Node->getDependenceType();

  switch (DepType) {
  case OMPC_DOACROSS_source:
    OS << "source:";
    break;
  case OMPC_DOACROSS_sink:
    OS << "sink:";
    break;
  case OMPC_DOACROSS_source_omp_cur_iteration:
    OS << "source: omp_cur_iteration";
    break;
  case OMPC_DOACROSS_sink_omp_cur_iteration:
    OS << "sink: omp_cur_iteration - 1";
    break;
  default:
    llvm_unreachable("unknown docaross modifier");
  }
  VisitOMPClauseList(Node, ' ');
  OS << ")";
}

void OMPClausePrinter::VisitOMPXAttributeClause(OMPXAttributeClause *Node) {
  OS << "ompx_attribute(";
  bool IsFirst = true;
  for (auto &Attr : Node->getAttrs()) {
    if (!IsFirst)
      OS << ", ";
    Attr->printPretty(OS, Policy);
    IsFirst = false;
  }
  OS << ")";
}

void OMPClausePrinter::VisitOMPXBareClause(OMPXBareClause *Node) {
  OS << "ompx_bare";
}

void OMPTraitInfo::getAsVariantMatchInfo(ASTContext &ASTCtx,
                                         VariantMatchInfo &VMI) const {
  for (const OMPTraitSet &Set : Sets) {
    for (const OMPTraitSelector &Selector : Set.Selectors) {

      // User conditions are special as we evaluate the condition here.
      if (Selector.Kind == TraitSelector::user_condition) {
        assert(Selector.ScoreOrCondition &&
               "Ill-formed user condition, expected condition expression!");
        assert(Selector.Properties.size() == 1 &&
               Selector.Properties.front().Kind ==
                   TraitProperty::user_condition_unknown &&
               "Ill-formed user condition, expected unknown trait property!");

        if (std::optional<APSInt> CondVal =
                Selector.ScoreOrCondition->getIntegerConstantExpr(ASTCtx))
          VMI.addTrait(CondVal->isZero() ? TraitProperty::user_condition_false
                                         : TraitProperty::user_condition_true,
                       "<condition>");
        else
          VMI.addTrait(TraitProperty::user_condition_false, "<condition>");
        continue;
      }

      std::optional<llvm::APSInt> Score;
      llvm::APInt *ScorePtr = nullptr;
      if (Selector.ScoreOrCondition) {
        if ((Score = Selector.ScoreOrCondition->getIntegerConstantExpr(ASTCtx)))
          ScorePtr = &*Score;
        else
          VMI.addTrait(TraitProperty::user_condition_false,
                       "<non-constant-score>");
      }

      for (const OMPTraitProperty &Property : Selector.Properties)
        VMI.addTrait(Set.Kind, Property.Kind, Property.RawString, ScorePtr);

      if (Set.Kind != TraitSet::construct)
        continue;

      // TODO: This might not hold once we implement SIMD properly.
      assert(Selector.Properties.size() == 1 &&
             Selector.Properties.front().Kind ==
                 getOpenMPContextTraitPropertyForSelector(
                     Selector.Kind) &&
             "Ill-formed construct selector!");
    }
  }
}

void OMPTraitInfo::print(llvm::raw_ostream &OS,
                         const PrintingPolicy &Policy) const {
  bool FirstSet = true;
  for (const OMPTraitSet &Set : Sets) {
    if (!FirstSet)
      OS << ", ";
    FirstSet = false;
    OS << getOpenMPContextTraitSetName(Set.Kind) << "={";

    bool FirstSelector = true;
    for (const OMPTraitSelector &Selector : Set.Selectors) {
      if (!FirstSelector)
        OS << ", ";
      FirstSelector = false;
      OS << getOpenMPContextTraitSelectorName(Selector.Kind);

      bool AllowsTraitScore = false;
      bool RequiresProperty = false;
      isValidTraitSelectorForTraitSet(
          Selector.Kind, Set.Kind, AllowsTraitScore, RequiresProperty);

      if (!RequiresProperty)
        continue;

      OS << "(";
      if (Selector.Kind == TraitSelector::user_condition) {
        if (Selector.ScoreOrCondition)
          Selector.ScoreOrCondition->printPretty(OS, nullptr, Policy);
        else
          OS << "...";
      } else {

        if (Selector.ScoreOrCondition) {
          OS << "score(";
          Selector.ScoreOrCondition->printPretty(OS, nullptr, Policy);
          OS << "): ";
        }

        bool FirstProperty = true;
        for (const OMPTraitProperty &Property : Selector.Properties) {
          if (!FirstProperty)
            OS << ", ";
          FirstProperty = false;
          OS << getOpenMPContextTraitPropertyName(Property.Kind,
                                                  Property.RawString);
        }
      }
      OS << ")";
    }
    OS << "}";
  }
}

std::string OMPTraitInfo::getMangledName() const {
  std::string MangledName;
  llvm::raw_string_ostream OS(MangledName);
  for (const OMPTraitSet &Set : Sets) {
    OS << '$' << 'S' << unsigned(Set.Kind);
    for (const OMPTraitSelector &Selector : Set.Selectors) {

      bool AllowsTraitScore = false;
      bool RequiresProperty = false;
      isValidTraitSelectorForTraitSet(
          Selector.Kind, Set.Kind, AllowsTraitScore, RequiresProperty);
      OS << '$' << 's' << unsigned(Selector.Kind);

      if (!RequiresProperty ||
          Selector.Kind == TraitSelector::user_condition)
        continue;

      for (const OMPTraitProperty &Property : Selector.Properties)
        OS << '$' << 'P'
           << getOpenMPContextTraitPropertyName(Property.Kind,
                                                Property.RawString);
    }
  }
  return MangledName;
}

OMPTraitInfo::OMPTraitInfo(StringRef MangledName) {
  unsigned long U;
  do {
    if (!MangledName.consume_front("$S"))
      break;
    if (MangledName.consumeInteger(10, U))
      break;
    Sets.push_back(OMPTraitSet());
    OMPTraitSet &Set = Sets.back();
    Set.Kind = TraitSet(U);
    do {
      if (!MangledName.consume_front("$s"))
        break;
      if (MangledName.consumeInteger(10, U))
        break;
      Set.Selectors.push_back(OMPTraitSelector());
      OMPTraitSelector &Selector = Set.Selectors.back();
      Selector.Kind = TraitSelector(U);
      do {
        if (!MangledName.consume_front("$P"))
          break;
        Selector.Properties.push_back(OMPTraitProperty());
        OMPTraitProperty &Property = Selector.Properties.back();
        std::pair<StringRef, StringRef> PropRestPair = MangledName.split('$');
        Property.RawString = PropRestPair.first;
        Property.Kind = getOpenMPContextTraitPropertyKind(
            Set.Kind, Selector.Kind, PropRestPair.first);
        MangledName = MangledName.drop_front(PropRestPair.first.size());
      } while (true);
    } while (true);
  } while (true);
}

llvm::raw_ostream &clang::operator<<(llvm::raw_ostream &OS,
                                     const OMPTraitInfo &TI) {
  LangOptions LO;
  PrintingPolicy Policy(LO);
  TI.print(OS, Policy);
  return OS;
}
llvm::raw_ostream &clang::operator<<(llvm::raw_ostream &OS,
                                     const OMPTraitInfo *TI) {
  return TI ? OS << *TI : OS;
}

TargetOMPContext::TargetOMPContext(
    ASTContext &ASTCtx, std::function<void(StringRef)> &&DiagUnknownTrait,
    const FunctionDecl *CurrentFunctionDecl,
    ArrayRef<llvm::omp::TraitProperty> ConstructTraits)
    : OMPContext(ASTCtx.getLangOpts().OpenMPIsTargetDevice,
                 ASTCtx.getTargetInfo().getTriple()),
      FeatureValidityCheck([&](StringRef FeatureName) {
        return ASTCtx.getTargetInfo().isValidFeatureName(FeatureName);
      }),
      DiagUnknownTrait(std::move(DiagUnknownTrait)) {
  ASTCtx.getFunctionFeatureMap(FeatureMap, CurrentFunctionDecl);

  for (llvm::omp::TraitProperty Property : ConstructTraits)
    addTrait(Property);
}

bool TargetOMPContext::matchesISATrait(StringRef RawString) const {
  auto It = FeatureMap.find(RawString);
  if (It != FeatureMap.end())
    return It->second;
  if (!FeatureValidityCheck(RawString))
    DiagUnknownTrait(RawString);
  return false;
}
