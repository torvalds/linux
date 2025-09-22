//===---- CGOpenMPRuntimeGPU.cpp - Interface to OpenMP GPU Runtimes ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This provides a generalized class for OpenMP runtime code generation
// specialized by GPU targets NVPTX and AMDGCN.
//
//===----------------------------------------------------------------------===//

#include "CGOpenMPRuntimeGPU.h"
#include "CodeGenFunction.h"
#include "clang/AST/Attr.h"
#include "clang/AST/DeclOpenMP.h"
#include "clang/AST/OpenMPClause.h"
#include "clang/AST/StmtOpenMP.h"
#include "clang/AST/StmtVisitor.h"
#include "clang/Basic/Cuda.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Frontend/OpenMP/OMPGridValues.h"
#include "llvm/Support/MathExtras.h"

using namespace clang;
using namespace CodeGen;
using namespace llvm::omp;

namespace {
/// Pre(post)-action for different OpenMP constructs specialized for NVPTX.
class NVPTXActionTy final : public PrePostActionTy {
  llvm::FunctionCallee EnterCallee = nullptr;
  ArrayRef<llvm::Value *> EnterArgs;
  llvm::FunctionCallee ExitCallee = nullptr;
  ArrayRef<llvm::Value *> ExitArgs;
  bool Conditional = false;
  llvm::BasicBlock *ContBlock = nullptr;

public:
  NVPTXActionTy(llvm::FunctionCallee EnterCallee,
                ArrayRef<llvm::Value *> EnterArgs,
                llvm::FunctionCallee ExitCallee,
                ArrayRef<llvm::Value *> ExitArgs, bool Conditional = false)
      : EnterCallee(EnterCallee), EnterArgs(EnterArgs), ExitCallee(ExitCallee),
        ExitArgs(ExitArgs), Conditional(Conditional) {}
  void Enter(CodeGenFunction &CGF) override {
    llvm::Value *EnterRes = CGF.EmitRuntimeCall(EnterCallee, EnterArgs);
    if (Conditional) {
      llvm::Value *CallBool = CGF.Builder.CreateIsNotNull(EnterRes);
      auto *ThenBlock = CGF.createBasicBlock("omp_if.then");
      ContBlock = CGF.createBasicBlock("omp_if.end");
      // Generate the branch (If-stmt)
      CGF.Builder.CreateCondBr(CallBool, ThenBlock, ContBlock);
      CGF.EmitBlock(ThenBlock);
    }
  }
  void Done(CodeGenFunction &CGF) {
    // Emit the rest of blocks/branches
    CGF.EmitBranch(ContBlock);
    CGF.EmitBlock(ContBlock, true);
  }
  void Exit(CodeGenFunction &CGF) override {
    CGF.EmitRuntimeCall(ExitCallee, ExitArgs);
  }
};

/// A class to track the execution mode when codegening directives within
/// a target region. The appropriate mode (SPMD|NON-SPMD) is set on entry
/// to the target region and used by containing directives such as 'parallel'
/// to emit optimized code.
class ExecutionRuntimeModesRAII {
private:
  CGOpenMPRuntimeGPU::ExecutionMode SavedExecMode =
      CGOpenMPRuntimeGPU::EM_Unknown;
  CGOpenMPRuntimeGPU::ExecutionMode &ExecMode;

public:
  ExecutionRuntimeModesRAII(CGOpenMPRuntimeGPU::ExecutionMode &ExecMode,
                            CGOpenMPRuntimeGPU::ExecutionMode EntryMode)
      : ExecMode(ExecMode) {
    SavedExecMode = ExecMode;
    ExecMode = EntryMode;
  }
  ~ExecutionRuntimeModesRAII() { ExecMode = SavedExecMode; }
};

static const ValueDecl *getPrivateItem(const Expr *RefExpr) {
  RefExpr = RefExpr->IgnoreParens();
  if (const auto *ASE = dyn_cast<ArraySubscriptExpr>(RefExpr)) {
    const Expr *Base = ASE->getBase()->IgnoreParenImpCasts();
    while (const auto *TempASE = dyn_cast<ArraySubscriptExpr>(Base))
      Base = TempASE->getBase()->IgnoreParenImpCasts();
    RefExpr = Base;
  } else if (auto *OASE = dyn_cast<ArraySectionExpr>(RefExpr)) {
    const Expr *Base = OASE->getBase()->IgnoreParenImpCasts();
    while (const auto *TempOASE = dyn_cast<ArraySectionExpr>(Base))
      Base = TempOASE->getBase()->IgnoreParenImpCasts();
    while (const auto *TempASE = dyn_cast<ArraySubscriptExpr>(Base))
      Base = TempASE->getBase()->IgnoreParenImpCasts();
    RefExpr = Base;
  }
  RefExpr = RefExpr->IgnoreParenImpCasts();
  if (const auto *DE = dyn_cast<DeclRefExpr>(RefExpr))
    return cast<ValueDecl>(DE->getDecl()->getCanonicalDecl());
  const auto *ME = cast<MemberExpr>(RefExpr);
  return cast<ValueDecl>(ME->getMemberDecl()->getCanonicalDecl());
}

static RecordDecl *buildRecordForGlobalizedVars(
    ASTContext &C, ArrayRef<const ValueDecl *> EscapedDecls,
    ArrayRef<const ValueDecl *> EscapedDeclsForTeams,
    llvm::SmallDenseMap<const ValueDecl *, const FieldDecl *>
        &MappedDeclsFields,
    int BufSize) {
  using VarsDataTy = std::pair<CharUnits /*Align*/, const ValueDecl *>;
  if (EscapedDecls.empty() && EscapedDeclsForTeams.empty())
    return nullptr;
  SmallVector<VarsDataTy, 4> GlobalizedVars;
  for (const ValueDecl *D : EscapedDecls)
    GlobalizedVars.emplace_back(C.getDeclAlign(D), D);
  for (const ValueDecl *D : EscapedDeclsForTeams)
    GlobalizedVars.emplace_back(C.getDeclAlign(D), D);

  // Build struct _globalized_locals_ty {
  //         /*  globalized vars  */[WarSize] align (decl_align)
  //         /*  globalized vars  */ for EscapedDeclsForTeams
  //       };
  RecordDecl *GlobalizedRD = C.buildImplicitRecord("_globalized_locals_ty");
  GlobalizedRD->startDefinition();
  llvm::SmallPtrSet<const ValueDecl *, 16> SingleEscaped(
      EscapedDeclsForTeams.begin(), EscapedDeclsForTeams.end());
  for (const auto &Pair : GlobalizedVars) {
    const ValueDecl *VD = Pair.second;
    QualType Type = VD->getType();
    if (Type->isLValueReferenceType())
      Type = C.getPointerType(Type.getNonReferenceType());
    else
      Type = Type.getNonReferenceType();
    SourceLocation Loc = VD->getLocation();
    FieldDecl *Field;
    if (SingleEscaped.count(VD)) {
      Field = FieldDecl::Create(
          C, GlobalizedRD, Loc, Loc, VD->getIdentifier(), Type,
          C.getTrivialTypeSourceInfo(Type, SourceLocation()),
          /*BW=*/nullptr, /*Mutable=*/false,
          /*InitStyle=*/ICIS_NoInit);
      Field->setAccess(AS_public);
      if (VD->hasAttrs()) {
        for (specific_attr_iterator<AlignedAttr> I(VD->getAttrs().begin()),
             E(VD->getAttrs().end());
             I != E; ++I)
          Field->addAttr(*I);
      }
    } else {
      if (BufSize > 1) {
        llvm::APInt ArraySize(32, BufSize);
        Type = C.getConstantArrayType(Type, ArraySize, nullptr,
                                      ArraySizeModifier::Normal, 0);
      }
      Field = FieldDecl::Create(
          C, GlobalizedRD, Loc, Loc, VD->getIdentifier(), Type,
          C.getTrivialTypeSourceInfo(Type, SourceLocation()),
          /*BW=*/nullptr, /*Mutable=*/false,
          /*InitStyle=*/ICIS_NoInit);
      Field->setAccess(AS_public);
      llvm::APInt Align(32, Pair.first.getQuantity());
      Field->addAttr(AlignedAttr::CreateImplicit(
          C, /*IsAlignmentExpr=*/true,
          IntegerLiteral::Create(C, Align,
                                 C.getIntTypeForBitwidth(32, /*Signed=*/0),
                                 SourceLocation()),
          {}, AlignedAttr::GNU_aligned));
    }
    GlobalizedRD->addDecl(Field);
    MappedDeclsFields.try_emplace(VD, Field);
  }
  GlobalizedRD->completeDefinition();
  return GlobalizedRD;
}

/// Get the list of variables that can escape their declaration context.
class CheckVarsEscapingDeclContext final
    : public ConstStmtVisitor<CheckVarsEscapingDeclContext> {
  CodeGenFunction &CGF;
  llvm::SetVector<const ValueDecl *> EscapedDecls;
  llvm::SetVector<const ValueDecl *> EscapedVariableLengthDecls;
  llvm::SetVector<const ValueDecl *> DelayedVariableLengthDecls;
  llvm::SmallPtrSet<const Decl *, 4> EscapedParameters;
  RecordDecl *GlobalizedRD = nullptr;
  llvm::SmallDenseMap<const ValueDecl *, const FieldDecl *> MappedDeclsFields;
  bool AllEscaped = false;
  bool IsForCombinedParallelRegion = false;

  void markAsEscaped(const ValueDecl *VD) {
    // Do not globalize declare target variables.
    if (!isa<VarDecl>(VD) ||
        OMPDeclareTargetDeclAttr::isDeclareTargetDeclaration(VD))
      return;
    VD = cast<ValueDecl>(VD->getCanonicalDecl());
    // Use user-specified allocation.
    if (VD->hasAttrs() && VD->hasAttr<OMPAllocateDeclAttr>())
      return;
    // Variables captured by value must be globalized.
    bool IsCaptured = false;
    if (auto *CSI = CGF.CapturedStmtInfo) {
      if (const FieldDecl *FD = CSI->lookup(cast<VarDecl>(VD))) {
        // Check if need to capture the variable that was already captured by
        // value in the outer region.
        IsCaptured = true;
        if (!IsForCombinedParallelRegion) {
          if (!FD->hasAttrs())
            return;
          const auto *Attr = FD->getAttr<OMPCaptureKindAttr>();
          if (!Attr)
            return;
          if (((Attr->getCaptureKind() != OMPC_map) &&
               !isOpenMPPrivate(Attr->getCaptureKind())) ||
              ((Attr->getCaptureKind() == OMPC_map) &&
               !FD->getType()->isAnyPointerType()))
            return;
        }
        if (!FD->getType()->isReferenceType()) {
          assert(!VD->getType()->isVariablyModifiedType() &&
                 "Parameter captured by value with variably modified type");
          EscapedParameters.insert(VD);
        } else if (!IsForCombinedParallelRegion) {
          return;
        }
      }
    }
    if ((!CGF.CapturedStmtInfo ||
         (IsForCombinedParallelRegion && CGF.CapturedStmtInfo)) &&
        VD->getType()->isReferenceType())
      // Do not globalize variables with reference type.
      return;
    if (VD->getType()->isVariablyModifiedType()) {
      // If not captured at the target region level then mark the escaped
      // variable as delayed.
      if (IsCaptured)
        EscapedVariableLengthDecls.insert(VD);
      else
        DelayedVariableLengthDecls.insert(VD);
    } else
      EscapedDecls.insert(VD);
  }

  void VisitValueDecl(const ValueDecl *VD) {
    if (VD->getType()->isLValueReferenceType())
      markAsEscaped(VD);
    if (const auto *VarD = dyn_cast<VarDecl>(VD)) {
      if (!isa<ParmVarDecl>(VarD) && VarD->hasInit()) {
        const bool SavedAllEscaped = AllEscaped;
        AllEscaped = VD->getType()->isLValueReferenceType();
        Visit(VarD->getInit());
        AllEscaped = SavedAllEscaped;
      }
    }
  }
  void VisitOpenMPCapturedStmt(const CapturedStmt *S,
                               ArrayRef<OMPClause *> Clauses,
                               bool IsCombinedParallelRegion) {
    if (!S)
      return;
    for (const CapturedStmt::Capture &C : S->captures()) {
      if (C.capturesVariable() && !C.capturesVariableByCopy()) {
        const ValueDecl *VD = C.getCapturedVar();
        bool SavedIsForCombinedParallelRegion = IsForCombinedParallelRegion;
        if (IsCombinedParallelRegion) {
          // Check if the variable is privatized in the combined construct and
          // those private copies must be shared in the inner parallel
          // directive.
          IsForCombinedParallelRegion = false;
          for (const OMPClause *C : Clauses) {
            if (!isOpenMPPrivate(C->getClauseKind()) ||
                C->getClauseKind() == OMPC_reduction ||
                C->getClauseKind() == OMPC_linear ||
                C->getClauseKind() == OMPC_private)
              continue;
            ArrayRef<const Expr *> Vars;
            if (const auto *PC = dyn_cast<OMPFirstprivateClause>(C))
              Vars = PC->getVarRefs();
            else if (const auto *PC = dyn_cast<OMPLastprivateClause>(C))
              Vars = PC->getVarRefs();
            else
              llvm_unreachable("Unexpected clause.");
            for (const auto *E : Vars) {
              const Decl *D =
                  cast<DeclRefExpr>(E)->getDecl()->getCanonicalDecl();
              if (D == VD->getCanonicalDecl()) {
                IsForCombinedParallelRegion = true;
                break;
              }
            }
            if (IsForCombinedParallelRegion)
              break;
          }
        }
        markAsEscaped(VD);
        if (isa<OMPCapturedExprDecl>(VD))
          VisitValueDecl(VD);
        IsForCombinedParallelRegion = SavedIsForCombinedParallelRegion;
      }
    }
  }

  void buildRecordForGlobalizedVars(bool IsInTTDRegion) {
    assert(!GlobalizedRD &&
           "Record for globalized variables is built already.");
    ArrayRef<const ValueDecl *> EscapedDeclsForParallel, EscapedDeclsForTeams;
    unsigned WarpSize = CGF.getTarget().getGridValue().GV_Warp_Size;
    if (IsInTTDRegion)
      EscapedDeclsForTeams = EscapedDecls.getArrayRef();
    else
      EscapedDeclsForParallel = EscapedDecls.getArrayRef();
    GlobalizedRD = ::buildRecordForGlobalizedVars(
        CGF.getContext(), EscapedDeclsForParallel, EscapedDeclsForTeams,
        MappedDeclsFields, WarpSize);
  }

public:
  CheckVarsEscapingDeclContext(CodeGenFunction &CGF,
                               ArrayRef<const ValueDecl *> TeamsReductions)
      : CGF(CGF), EscapedDecls(TeamsReductions.begin(), TeamsReductions.end()) {
  }
  virtual ~CheckVarsEscapingDeclContext() = default;
  void VisitDeclStmt(const DeclStmt *S) {
    if (!S)
      return;
    for (const Decl *D : S->decls())
      if (const auto *VD = dyn_cast_or_null<ValueDecl>(D))
        VisitValueDecl(VD);
  }
  void VisitOMPExecutableDirective(const OMPExecutableDirective *D) {
    if (!D)
      return;
    if (!D->hasAssociatedStmt())
      return;
    if (const auto *S =
            dyn_cast_or_null<CapturedStmt>(D->getAssociatedStmt())) {
      // Do not analyze directives that do not actually require capturing,
      // like `omp for` or `omp simd` directives.
      llvm::SmallVector<OpenMPDirectiveKind, 4> CaptureRegions;
      getOpenMPCaptureRegions(CaptureRegions, D->getDirectiveKind());
      if (CaptureRegions.size() == 1 && CaptureRegions.back() == OMPD_unknown) {
        VisitStmt(S->getCapturedStmt());
        return;
      }
      VisitOpenMPCapturedStmt(
          S, D->clauses(),
          CaptureRegions.back() == OMPD_parallel &&
              isOpenMPDistributeDirective(D->getDirectiveKind()));
    }
  }
  void VisitCapturedStmt(const CapturedStmt *S) {
    if (!S)
      return;
    for (const CapturedStmt::Capture &C : S->captures()) {
      if (C.capturesVariable() && !C.capturesVariableByCopy()) {
        const ValueDecl *VD = C.getCapturedVar();
        markAsEscaped(VD);
        if (isa<OMPCapturedExprDecl>(VD))
          VisitValueDecl(VD);
      }
    }
  }
  void VisitLambdaExpr(const LambdaExpr *E) {
    if (!E)
      return;
    for (const LambdaCapture &C : E->captures()) {
      if (C.capturesVariable()) {
        if (C.getCaptureKind() == LCK_ByRef) {
          const ValueDecl *VD = C.getCapturedVar();
          markAsEscaped(VD);
          if (E->isInitCapture(&C) || isa<OMPCapturedExprDecl>(VD))
            VisitValueDecl(VD);
        }
      }
    }
  }
  void VisitBlockExpr(const BlockExpr *E) {
    if (!E)
      return;
    for (const BlockDecl::Capture &C : E->getBlockDecl()->captures()) {
      if (C.isByRef()) {
        const VarDecl *VD = C.getVariable();
        markAsEscaped(VD);
        if (isa<OMPCapturedExprDecl>(VD) || VD->isInitCapture())
          VisitValueDecl(VD);
      }
    }
  }
  void VisitCallExpr(const CallExpr *E) {
    if (!E)
      return;
    for (const Expr *Arg : E->arguments()) {
      if (!Arg)
        continue;
      if (Arg->isLValue()) {
        const bool SavedAllEscaped = AllEscaped;
        AllEscaped = true;
        Visit(Arg);
        AllEscaped = SavedAllEscaped;
      } else {
        Visit(Arg);
      }
    }
    Visit(E->getCallee());
  }
  void VisitDeclRefExpr(const DeclRefExpr *E) {
    if (!E)
      return;
    const ValueDecl *VD = E->getDecl();
    if (AllEscaped)
      markAsEscaped(VD);
    if (isa<OMPCapturedExprDecl>(VD))
      VisitValueDecl(VD);
    else if (VD->isInitCapture())
      VisitValueDecl(VD);
  }
  void VisitUnaryOperator(const UnaryOperator *E) {
    if (!E)
      return;
    if (E->getOpcode() == UO_AddrOf) {
      const bool SavedAllEscaped = AllEscaped;
      AllEscaped = true;
      Visit(E->getSubExpr());
      AllEscaped = SavedAllEscaped;
    } else {
      Visit(E->getSubExpr());
    }
  }
  void VisitImplicitCastExpr(const ImplicitCastExpr *E) {
    if (!E)
      return;
    if (E->getCastKind() == CK_ArrayToPointerDecay) {
      const bool SavedAllEscaped = AllEscaped;
      AllEscaped = true;
      Visit(E->getSubExpr());
      AllEscaped = SavedAllEscaped;
    } else {
      Visit(E->getSubExpr());
    }
  }
  void VisitExpr(const Expr *E) {
    if (!E)
      return;
    bool SavedAllEscaped = AllEscaped;
    if (!E->isLValue())
      AllEscaped = false;
    for (const Stmt *Child : E->children())
      if (Child)
        Visit(Child);
    AllEscaped = SavedAllEscaped;
  }
  void VisitStmt(const Stmt *S) {
    if (!S)
      return;
    for (const Stmt *Child : S->children())
      if (Child)
        Visit(Child);
  }

  /// Returns the record that handles all the escaped local variables and used
  /// instead of their original storage.
  const RecordDecl *getGlobalizedRecord(bool IsInTTDRegion) {
    if (!GlobalizedRD)
      buildRecordForGlobalizedVars(IsInTTDRegion);
    return GlobalizedRD;
  }

  /// Returns the field in the globalized record for the escaped variable.
  const FieldDecl *getFieldForGlobalizedVar(const ValueDecl *VD) const {
    assert(GlobalizedRD &&
           "Record for globalized variables must be generated already.");
    return MappedDeclsFields.lookup(VD);
  }

  /// Returns the list of the escaped local variables/parameters.
  ArrayRef<const ValueDecl *> getEscapedDecls() const {
    return EscapedDecls.getArrayRef();
  }

  /// Checks if the escaped local variable is actually a parameter passed by
  /// value.
  const llvm::SmallPtrSetImpl<const Decl *> &getEscapedParameters() const {
    return EscapedParameters;
  }

  /// Returns the list of the escaped variables with the variably modified
  /// types.
  ArrayRef<const ValueDecl *> getEscapedVariableLengthDecls() const {
    return EscapedVariableLengthDecls.getArrayRef();
  }

  /// Returns the list of the delayed variables with the variably modified
  /// types.
  ArrayRef<const ValueDecl *> getDelayedVariableLengthDecls() const {
    return DelayedVariableLengthDecls.getArrayRef();
  }
};
} // anonymous namespace

CGOpenMPRuntimeGPU::ExecutionMode
CGOpenMPRuntimeGPU::getExecutionMode() const {
  return CurrentExecutionMode;
}

CGOpenMPRuntimeGPU::DataSharingMode
CGOpenMPRuntimeGPU::getDataSharingMode() const {
  return CurrentDataSharingMode;
}

/// Check for inner (nested) SPMD construct, if any
static bool hasNestedSPMDDirective(ASTContext &Ctx,
                                   const OMPExecutableDirective &D) {
  const auto *CS = D.getInnermostCapturedStmt();
  const auto *Body =
      CS->getCapturedStmt()->IgnoreContainers(/*IgnoreCaptured=*/true);
  const Stmt *ChildStmt = CGOpenMPRuntime::getSingleCompoundChild(Ctx, Body);

  if (const auto *NestedDir =
          dyn_cast_or_null<OMPExecutableDirective>(ChildStmt)) {
    OpenMPDirectiveKind DKind = NestedDir->getDirectiveKind();
    switch (D.getDirectiveKind()) {
    case OMPD_target:
      if (isOpenMPParallelDirective(DKind))
        return true;
      if (DKind == OMPD_teams) {
        Body = NestedDir->getInnermostCapturedStmt()->IgnoreContainers(
            /*IgnoreCaptured=*/true);
        if (!Body)
          return false;
        ChildStmt = CGOpenMPRuntime::getSingleCompoundChild(Ctx, Body);
        if (const auto *NND =
                dyn_cast_or_null<OMPExecutableDirective>(ChildStmt)) {
          DKind = NND->getDirectiveKind();
          if (isOpenMPParallelDirective(DKind))
            return true;
        }
      }
      return false;
    case OMPD_target_teams:
      return isOpenMPParallelDirective(DKind);
    case OMPD_target_simd:
    case OMPD_target_parallel:
    case OMPD_target_parallel_for:
    case OMPD_target_parallel_for_simd:
    case OMPD_target_teams_distribute:
    case OMPD_target_teams_distribute_simd:
    case OMPD_target_teams_distribute_parallel_for:
    case OMPD_target_teams_distribute_parallel_for_simd:
    case OMPD_parallel:
    case OMPD_for:
    case OMPD_parallel_for:
    case OMPD_parallel_master:
    case OMPD_parallel_sections:
    case OMPD_for_simd:
    case OMPD_parallel_for_simd:
    case OMPD_cancel:
    case OMPD_cancellation_point:
    case OMPD_ordered:
    case OMPD_threadprivate:
    case OMPD_allocate:
    case OMPD_task:
    case OMPD_simd:
    case OMPD_sections:
    case OMPD_section:
    case OMPD_single:
    case OMPD_master:
    case OMPD_critical:
    case OMPD_taskyield:
    case OMPD_barrier:
    case OMPD_taskwait:
    case OMPD_taskgroup:
    case OMPD_atomic:
    case OMPD_flush:
    case OMPD_depobj:
    case OMPD_scan:
    case OMPD_teams:
    case OMPD_target_data:
    case OMPD_target_exit_data:
    case OMPD_target_enter_data:
    case OMPD_distribute:
    case OMPD_distribute_simd:
    case OMPD_distribute_parallel_for:
    case OMPD_distribute_parallel_for_simd:
    case OMPD_teams_distribute:
    case OMPD_teams_distribute_simd:
    case OMPD_teams_distribute_parallel_for:
    case OMPD_teams_distribute_parallel_for_simd:
    case OMPD_target_update:
    case OMPD_declare_simd:
    case OMPD_declare_variant:
    case OMPD_begin_declare_variant:
    case OMPD_end_declare_variant:
    case OMPD_declare_target:
    case OMPD_end_declare_target:
    case OMPD_declare_reduction:
    case OMPD_declare_mapper:
    case OMPD_taskloop:
    case OMPD_taskloop_simd:
    case OMPD_master_taskloop:
    case OMPD_master_taskloop_simd:
    case OMPD_parallel_master_taskloop:
    case OMPD_parallel_master_taskloop_simd:
    case OMPD_requires:
    case OMPD_unknown:
    default:
      llvm_unreachable("Unexpected directive.");
    }
  }

  return false;
}

static bool supportsSPMDExecutionMode(ASTContext &Ctx,
                                      const OMPExecutableDirective &D) {
  OpenMPDirectiveKind DirectiveKind = D.getDirectiveKind();
  switch (DirectiveKind) {
  case OMPD_target:
  case OMPD_target_teams:
    return hasNestedSPMDDirective(Ctx, D);
  case OMPD_target_parallel_loop:
  case OMPD_target_parallel:
  case OMPD_target_parallel_for:
  case OMPD_target_parallel_for_simd:
  case OMPD_target_teams_distribute_parallel_for:
  case OMPD_target_teams_distribute_parallel_for_simd:
  case OMPD_target_simd:
  case OMPD_target_teams_distribute_simd:
    return true;
  case OMPD_target_teams_distribute:
    return false;
  case OMPD_target_teams_loop:
    // Whether this is true or not depends on how the directive will
    // eventually be emitted.
    if (auto *TTLD = dyn_cast<OMPTargetTeamsGenericLoopDirective>(&D))
      return TTLD->canBeParallelFor();
    return false;
  case OMPD_parallel:
  case OMPD_for:
  case OMPD_parallel_for:
  case OMPD_parallel_master:
  case OMPD_parallel_sections:
  case OMPD_for_simd:
  case OMPD_parallel_for_simd:
  case OMPD_cancel:
  case OMPD_cancellation_point:
  case OMPD_ordered:
  case OMPD_threadprivate:
  case OMPD_allocate:
  case OMPD_task:
  case OMPD_simd:
  case OMPD_sections:
  case OMPD_section:
  case OMPD_single:
  case OMPD_master:
  case OMPD_critical:
  case OMPD_taskyield:
  case OMPD_barrier:
  case OMPD_taskwait:
  case OMPD_taskgroup:
  case OMPD_atomic:
  case OMPD_flush:
  case OMPD_depobj:
  case OMPD_scan:
  case OMPD_teams:
  case OMPD_target_data:
  case OMPD_target_exit_data:
  case OMPD_target_enter_data:
  case OMPD_distribute:
  case OMPD_distribute_simd:
  case OMPD_distribute_parallel_for:
  case OMPD_distribute_parallel_for_simd:
  case OMPD_teams_distribute:
  case OMPD_teams_distribute_simd:
  case OMPD_teams_distribute_parallel_for:
  case OMPD_teams_distribute_parallel_for_simd:
  case OMPD_target_update:
  case OMPD_declare_simd:
  case OMPD_declare_variant:
  case OMPD_begin_declare_variant:
  case OMPD_end_declare_variant:
  case OMPD_declare_target:
  case OMPD_end_declare_target:
  case OMPD_declare_reduction:
  case OMPD_declare_mapper:
  case OMPD_taskloop:
  case OMPD_taskloop_simd:
  case OMPD_master_taskloop:
  case OMPD_master_taskloop_simd:
  case OMPD_parallel_master_taskloop:
  case OMPD_parallel_master_taskloop_simd:
  case OMPD_requires:
  case OMPD_unknown:
  default:
    break;
  }
  llvm_unreachable(
      "Unknown programming model for OpenMP directive on NVPTX target.");
}

void CGOpenMPRuntimeGPU::emitNonSPMDKernel(const OMPExecutableDirective &D,
                                             StringRef ParentName,
                                             llvm::Function *&OutlinedFn,
                                             llvm::Constant *&OutlinedFnID,
                                             bool IsOffloadEntry,
                                             const RegionCodeGenTy &CodeGen) {
  ExecutionRuntimeModesRAII ModeRAII(CurrentExecutionMode, EM_NonSPMD);
  EntryFunctionState EST;
  WrapperFunctionsMap.clear();

  [[maybe_unused]] bool IsBareKernel = D.getSingleClause<OMPXBareClause>();
  assert(!IsBareKernel && "bare kernel should not be at generic mode");

  // Emit target region as a standalone region.
  class NVPTXPrePostActionTy : public PrePostActionTy {
    CGOpenMPRuntimeGPU::EntryFunctionState &EST;
    const OMPExecutableDirective &D;

  public:
    NVPTXPrePostActionTy(CGOpenMPRuntimeGPU::EntryFunctionState &EST,
                         const OMPExecutableDirective &D)
        : EST(EST), D(D) {}
    void Enter(CodeGenFunction &CGF) override {
      auto &RT = static_cast<CGOpenMPRuntimeGPU &>(CGF.CGM.getOpenMPRuntime());
      RT.emitKernelInit(D, CGF, EST, /* IsSPMD */ false);
      // Skip target region initialization.
      RT.setLocThreadIdInsertPt(CGF, /*AtCurrentPoint=*/true);
    }
    void Exit(CodeGenFunction &CGF) override {
      auto &RT = static_cast<CGOpenMPRuntimeGPU &>(CGF.CGM.getOpenMPRuntime());
      RT.clearLocThreadIdInsertPt(CGF);
      RT.emitKernelDeinit(CGF, EST, /* IsSPMD */ false);
    }
  } Action(EST, D);
  CodeGen.setAction(Action);
  IsInTTDRegion = true;
  emitTargetOutlinedFunctionHelper(D, ParentName, OutlinedFn, OutlinedFnID,
                                   IsOffloadEntry, CodeGen);
  IsInTTDRegion = false;
}

void CGOpenMPRuntimeGPU::emitKernelInit(const OMPExecutableDirective &D,
                                        CodeGenFunction &CGF,
                                        EntryFunctionState &EST, bool IsSPMD) {
  int32_t MinThreadsVal = 1, MaxThreadsVal = -1, MinTeamsVal = 1,
          MaxTeamsVal = -1;
  computeMinAndMaxThreadsAndTeams(D, CGF, MinThreadsVal, MaxThreadsVal,
                                  MinTeamsVal, MaxTeamsVal);

  CGBuilderTy &Bld = CGF.Builder;
  Bld.restoreIP(OMPBuilder.createTargetInit(
      Bld, IsSPMD, MinThreadsVal, MaxThreadsVal, MinTeamsVal, MaxTeamsVal));
  if (!IsSPMD)
    emitGenericVarsProlog(CGF, EST.Loc);
}

void CGOpenMPRuntimeGPU::emitKernelDeinit(CodeGenFunction &CGF,
                                          EntryFunctionState &EST,
                                          bool IsSPMD) {
  if (!IsSPMD)
    emitGenericVarsEpilog(CGF);

  // This is temporary until we remove the fixed sized buffer.
  ASTContext &C = CGM.getContext();
  RecordDecl *StaticRD = C.buildImplicitRecord(
      "_openmp_teams_reduction_type_$_", RecordDecl::TagKind::Union);
  StaticRD->startDefinition();
  for (const RecordDecl *TeamReductionRec : TeamsReductions) {
    QualType RecTy = C.getRecordType(TeamReductionRec);
    auto *Field = FieldDecl::Create(
        C, StaticRD, SourceLocation(), SourceLocation(), nullptr, RecTy,
        C.getTrivialTypeSourceInfo(RecTy, SourceLocation()),
        /*BW=*/nullptr, /*Mutable=*/false,
        /*InitStyle=*/ICIS_NoInit);
    Field->setAccess(AS_public);
    StaticRD->addDecl(Field);
  }
  StaticRD->completeDefinition();
  QualType StaticTy = C.getRecordType(StaticRD);
  llvm::Type *LLVMReductionsBufferTy =
      CGM.getTypes().ConvertTypeForMem(StaticTy);
  const auto &DL = CGM.getModule().getDataLayout();
  uint64_t ReductionDataSize =
      TeamsReductions.empty()
          ? 0
          : DL.getTypeAllocSize(LLVMReductionsBufferTy).getFixedValue();
  CGBuilderTy &Bld = CGF.Builder;
  OMPBuilder.createTargetDeinit(Bld, ReductionDataSize,
                                C.getLangOpts().OpenMPCUDAReductionBufNum);
  TeamsReductions.clear();
}

void CGOpenMPRuntimeGPU::emitSPMDKernel(const OMPExecutableDirective &D,
                                          StringRef ParentName,
                                          llvm::Function *&OutlinedFn,
                                          llvm::Constant *&OutlinedFnID,
                                          bool IsOffloadEntry,
                                          const RegionCodeGenTy &CodeGen) {
  ExecutionRuntimeModesRAII ModeRAII(CurrentExecutionMode, EM_SPMD);
  EntryFunctionState EST;

  bool IsBareKernel = D.getSingleClause<OMPXBareClause>();

  // Emit target region as a standalone region.
  class NVPTXPrePostActionTy : public PrePostActionTy {
    CGOpenMPRuntimeGPU &RT;
    CGOpenMPRuntimeGPU::EntryFunctionState &EST;
    bool IsBareKernel;
    DataSharingMode Mode;
    const OMPExecutableDirective &D;

  public:
    NVPTXPrePostActionTy(CGOpenMPRuntimeGPU &RT,
                         CGOpenMPRuntimeGPU::EntryFunctionState &EST,
                         bool IsBareKernel, const OMPExecutableDirective &D)
        : RT(RT), EST(EST), IsBareKernel(IsBareKernel),
          Mode(RT.CurrentDataSharingMode), D(D) {}
    void Enter(CodeGenFunction &CGF) override {
      if (IsBareKernel) {
        RT.CurrentDataSharingMode = DataSharingMode::DS_CUDA;
        return;
      }
      RT.emitKernelInit(D, CGF, EST, /* IsSPMD */ true);
      // Skip target region initialization.
      RT.setLocThreadIdInsertPt(CGF, /*AtCurrentPoint=*/true);
    }
    void Exit(CodeGenFunction &CGF) override {
      if (IsBareKernel) {
        RT.CurrentDataSharingMode = Mode;
        return;
      }
      RT.clearLocThreadIdInsertPt(CGF);
      RT.emitKernelDeinit(CGF, EST, /* IsSPMD */ true);
    }
  } Action(*this, EST, IsBareKernel, D);
  CodeGen.setAction(Action);
  IsInTTDRegion = true;
  emitTargetOutlinedFunctionHelper(D, ParentName, OutlinedFn, OutlinedFnID,
                                   IsOffloadEntry, CodeGen);
  IsInTTDRegion = false;
}

void CGOpenMPRuntimeGPU::emitTargetOutlinedFunction(
    const OMPExecutableDirective &D, StringRef ParentName,
    llvm::Function *&OutlinedFn, llvm::Constant *&OutlinedFnID,
    bool IsOffloadEntry, const RegionCodeGenTy &CodeGen) {
  if (!IsOffloadEntry) // Nothing to do.
    return;

  assert(!ParentName.empty() && "Invalid target region parent name!");

  bool Mode = supportsSPMDExecutionMode(CGM.getContext(), D);
  bool IsBareKernel = D.getSingleClause<OMPXBareClause>();
  if (Mode || IsBareKernel)
    emitSPMDKernel(D, ParentName, OutlinedFn, OutlinedFnID, IsOffloadEntry,
                   CodeGen);
  else
    emitNonSPMDKernel(D, ParentName, OutlinedFn, OutlinedFnID, IsOffloadEntry,
                      CodeGen);
}

CGOpenMPRuntimeGPU::CGOpenMPRuntimeGPU(CodeGenModule &CGM)
    : CGOpenMPRuntime(CGM) {
  llvm::OpenMPIRBuilderConfig Config(
      CGM.getLangOpts().OpenMPIsTargetDevice, isGPU(),
      CGM.getLangOpts().OpenMPOffloadMandatory,
      /*HasRequiresReverseOffload*/ false, /*HasRequiresUnifiedAddress*/ false,
      hasRequiresUnifiedSharedMemory(), /*HasRequiresDynamicAllocators*/ false);
  OMPBuilder.setConfig(Config);

  if (!CGM.getLangOpts().OpenMPIsTargetDevice)
    llvm_unreachable("OpenMP can only handle device code.");

  if (CGM.getLangOpts().OpenMPCUDAMode)
    CurrentDataSharingMode = CGOpenMPRuntimeGPU::DS_CUDA;

  llvm::OpenMPIRBuilder &OMPBuilder = getOMPBuilder();
  if (CGM.getLangOpts().NoGPULib || CGM.getLangOpts().OMPHostIRFile.empty())
    return;

  OMPBuilder.createGlobalFlag(CGM.getLangOpts().OpenMPTargetDebug,
                              "__omp_rtl_debug_kind");
  OMPBuilder.createGlobalFlag(CGM.getLangOpts().OpenMPTeamSubscription,
                              "__omp_rtl_assume_teams_oversubscription");
  OMPBuilder.createGlobalFlag(CGM.getLangOpts().OpenMPThreadSubscription,
                              "__omp_rtl_assume_threads_oversubscription");
  OMPBuilder.createGlobalFlag(CGM.getLangOpts().OpenMPNoThreadState,
                              "__omp_rtl_assume_no_thread_state");
  OMPBuilder.createGlobalFlag(CGM.getLangOpts().OpenMPNoNestedParallelism,
                              "__omp_rtl_assume_no_nested_parallelism");
}

void CGOpenMPRuntimeGPU::emitProcBindClause(CodeGenFunction &CGF,
                                              ProcBindKind ProcBind,
                                              SourceLocation Loc) {
  // Nothing to do.
}

void CGOpenMPRuntimeGPU::emitNumThreadsClause(CodeGenFunction &CGF,
                                                llvm::Value *NumThreads,
                                                SourceLocation Loc) {
  // Nothing to do.
}

void CGOpenMPRuntimeGPU::emitNumTeamsClause(CodeGenFunction &CGF,
                                              const Expr *NumTeams,
                                              const Expr *ThreadLimit,
                                              SourceLocation Loc) {}

llvm::Function *CGOpenMPRuntimeGPU::emitParallelOutlinedFunction(
    CodeGenFunction &CGF, const OMPExecutableDirective &D,
    const VarDecl *ThreadIDVar, OpenMPDirectiveKind InnermostKind,
    const RegionCodeGenTy &CodeGen) {
  // Emit target region as a standalone region.
  bool PrevIsInTTDRegion = IsInTTDRegion;
  IsInTTDRegion = false;
  auto *OutlinedFun =
      cast<llvm::Function>(CGOpenMPRuntime::emitParallelOutlinedFunction(
          CGF, D, ThreadIDVar, InnermostKind, CodeGen));
  IsInTTDRegion = PrevIsInTTDRegion;
  if (getExecutionMode() != CGOpenMPRuntimeGPU::EM_SPMD) {
    llvm::Function *WrapperFun =
        createParallelDataSharingWrapper(OutlinedFun, D);
    WrapperFunctionsMap[OutlinedFun] = WrapperFun;
  }

  return OutlinedFun;
}

/// Get list of lastprivate variables from the teams distribute ... or
/// teams {distribute ...} directives.
static void
getDistributeLastprivateVars(ASTContext &Ctx, const OMPExecutableDirective &D,
                             llvm::SmallVectorImpl<const ValueDecl *> &Vars) {
  assert(isOpenMPTeamsDirective(D.getDirectiveKind()) &&
         "expected teams directive.");
  const OMPExecutableDirective *Dir = &D;
  if (!isOpenMPDistributeDirective(D.getDirectiveKind())) {
    if (const Stmt *S = CGOpenMPRuntime::getSingleCompoundChild(
            Ctx,
            D.getInnermostCapturedStmt()->getCapturedStmt()->IgnoreContainers(
                /*IgnoreCaptured=*/true))) {
      Dir = dyn_cast_or_null<OMPExecutableDirective>(S);
      if (Dir && !isOpenMPDistributeDirective(Dir->getDirectiveKind()))
        Dir = nullptr;
    }
  }
  if (!Dir)
    return;
  for (const auto *C : Dir->getClausesOfKind<OMPLastprivateClause>()) {
    for (const Expr *E : C->getVarRefs())
      Vars.push_back(getPrivateItem(E));
  }
}

/// Get list of reduction variables from the teams ... directives.
static void
getTeamsReductionVars(ASTContext &Ctx, const OMPExecutableDirective &D,
                      llvm::SmallVectorImpl<const ValueDecl *> &Vars) {
  assert(isOpenMPTeamsDirective(D.getDirectiveKind()) &&
         "expected teams directive.");
  for (const auto *C : D.getClausesOfKind<OMPReductionClause>()) {
    for (const Expr *E : C->privates())
      Vars.push_back(getPrivateItem(E));
  }
}

llvm::Function *CGOpenMPRuntimeGPU::emitTeamsOutlinedFunction(
    CodeGenFunction &CGF, const OMPExecutableDirective &D,
    const VarDecl *ThreadIDVar, OpenMPDirectiveKind InnermostKind,
    const RegionCodeGenTy &CodeGen) {
  SourceLocation Loc = D.getBeginLoc();

  const RecordDecl *GlobalizedRD = nullptr;
  llvm::SmallVector<const ValueDecl *, 4> LastPrivatesReductions;
  llvm::SmallDenseMap<const ValueDecl *, const FieldDecl *> MappedDeclsFields;
  unsigned WarpSize = CGM.getTarget().getGridValue().GV_Warp_Size;
  // Globalize team reductions variable unconditionally in all modes.
  if (getExecutionMode() != CGOpenMPRuntimeGPU::EM_SPMD)
    getTeamsReductionVars(CGM.getContext(), D, LastPrivatesReductions);
  if (getExecutionMode() == CGOpenMPRuntimeGPU::EM_SPMD) {
    getDistributeLastprivateVars(CGM.getContext(), D, LastPrivatesReductions);
    if (!LastPrivatesReductions.empty()) {
      GlobalizedRD = ::buildRecordForGlobalizedVars(
          CGM.getContext(), std::nullopt, LastPrivatesReductions,
          MappedDeclsFields, WarpSize);
    }
  } else if (!LastPrivatesReductions.empty()) {
    assert(!TeamAndReductions.first &&
           "Previous team declaration is not expected.");
    TeamAndReductions.first = D.getCapturedStmt(OMPD_teams)->getCapturedDecl();
    std::swap(TeamAndReductions.second, LastPrivatesReductions);
  }

  // Emit target region as a standalone region.
  class NVPTXPrePostActionTy : public PrePostActionTy {
    SourceLocation &Loc;
    const RecordDecl *GlobalizedRD;
    llvm::SmallDenseMap<const ValueDecl *, const FieldDecl *>
        &MappedDeclsFields;

  public:
    NVPTXPrePostActionTy(
        SourceLocation &Loc, const RecordDecl *GlobalizedRD,
        llvm::SmallDenseMap<const ValueDecl *, const FieldDecl *>
            &MappedDeclsFields)
        : Loc(Loc), GlobalizedRD(GlobalizedRD),
          MappedDeclsFields(MappedDeclsFields) {}
    void Enter(CodeGenFunction &CGF) override {
      auto &Rt =
          static_cast<CGOpenMPRuntimeGPU &>(CGF.CGM.getOpenMPRuntime());
      if (GlobalizedRD) {
        auto I = Rt.FunctionGlobalizedDecls.try_emplace(CGF.CurFn).first;
        I->getSecond().MappedParams =
            std::make_unique<CodeGenFunction::OMPMapVars>();
        DeclToAddrMapTy &Data = I->getSecond().LocalVarData;
        for (const auto &Pair : MappedDeclsFields) {
          assert(Pair.getFirst()->isCanonicalDecl() &&
                 "Expected canonical declaration");
          Data.insert(std::make_pair(Pair.getFirst(), MappedVarData()));
        }
      }
      Rt.emitGenericVarsProlog(CGF, Loc);
    }
    void Exit(CodeGenFunction &CGF) override {
      static_cast<CGOpenMPRuntimeGPU &>(CGF.CGM.getOpenMPRuntime())
          .emitGenericVarsEpilog(CGF);
    }
  } Action(Loc, GlobalizedRD, MappedDeclsFields);
  CodeGen.setAction(Action);
  llvm::Function *OutlinedFun = CGOpenMPRuntime::emitTeamsOutlinedFunction(
      CGF, D, ThreadIDVar, InnermostKind, CodeGen);

  return OutlinedFun;
}

void CGOpenMPRuntimeGPU::emitGenericVarsProlog(CodeGenFunction &CGF,
                                               SourceLocation Loc) {
  if (getDataSharingMode() != CGOpenMPRuntimeGPU::DS_Generic)
    return;

  CGBuilderTy &Bld = CGF.Builder;

  const auto I = FunctionGlobalizedDecls.find(CGF.CurFn);
  if (I == FunctionGlobalizedDecls.end())
    return;

  for (auto &Rec : I->getSecond().LocalVarData) {
    const auto *VD = cast<VarDecl>(Rec.first);
    bool EscapedParam = I->getSecond().EscapedParameters.count(Rec.first);
    QualType VarTy = VD->getType();

    // Get the local allocation of a firstprivate variable before sharing
    llvm::Value *ParValue;
    if (EscapedParam) {
      LValue ParLVal =
          CGF.MakeAddrLValue(CGF.GetAddrOfLocalVar(VD), VD->getType());
      ParValue = CGF.EmitLoadOfScalar(ParLVal, Loc);
    }

    // Allocate space for the variable to be globalized
    llvm::Value *AllocArgs[] = {CGF.getTypeSize(VD->getType())};
    llvm::CallBase *VoidPtr =
        CGF.EmitRuntimeCall(OMPBuilder.getOrCreateRuntimeFunction(
                                CGM.getModule(), OMPRTL___kmpc_alloc_shared),
                            AllocArgs, VD->getName());
    // FIXME: We should use the variables actual alignment as an argument.
    VoidPtr->addRetAttr(llvm::Attribute::get(
        CGM.getLLVMContext(), llvm::Attribute::Alignment,
        CGM.getContext().getTargetInfo().getNewAlign() / 8));

    // Cast the void pointer and get the address of the globalized variable.
    llvm::PointerType *VarPtrTy = CGF.ConvertTypeForMem(VarTy)->getPointerTo();
    llvm::Value *CastedVoidPtr = Bld.CreatePointerBitCastOrAddrSpaceCast(
        VoidPtr, VarPtrTy, VD->getName() + "_on_stack");
    LValue VarAddr =
        CGF.MakeNaturalAlignPointeeRawAddrLValue(CastedVoidPtr, VarTy);
    Rec.second.PrivateAddr = VarAddr.getAddress();
    Rec.second.GlobalizedVal = VoidPtr;

    // Assign the local allocation to the newly globalized location.
    if (EscapedParam) {
      CGF.EmitStoreOfScalar(ParValue, VarAddr);
      I->getSecond().MappedParams->setVarAddr(CGF, VD, VarAddr.getAddress());
    }
    if (auto *DI = CGF.getDebugInfo())
      VoidPtr->setDebugLoc(DI->SourceLocToDebugLoc(VD->getLocation()));
  }

  for (const auto *ValueD : I->getSecond().EscapedVariableLengthDecls) {
    const auto *VD = cast<VarDecl>(ValueD);
    std::pair<llvm::Value *, llvm::Value *> AddrSizePair =
        getKmpcAllocShared(CGF, VD);
    I->getSecond().EscapedVariableLengthDeclsAddrs.emplace_back(AddrSizePair);
    LValue Base = CGF.MakeAddrLValue(AddrSizePair.first, VD->getType(),
                                     CGM.getContext().getDeclAlign(VD),
                                     AlignmentSource::Decl);
    I->getSecond().MappedParams->setVarAddr(CGF, VD, Base.getAddress());
  }
  I->getSecond().MappedParams->apply(CGF);
}

bool CGOpenMPRuntimeGPU::isDelayedVariableLengthDecl(CodeGenFunction &CGF,
                                                     const VarDecl *VD) const {
  const auto I = FunctionGlobalizedDecls.find(CGF.CurFn);
  if (I == FunctionGlobalizedDecls.end())
    return false;

  // Check variable declaration is delayed:
  return llvm::is_contained(I->getSecond().DelayedVariableLengthDecls, VD);
}

std::pair<llvm::Value *, llvm::Value *>
CGOpenMPRuntimeGPU::getKmpcAllocShared(CodeGenFunction &CGF,
                                       const VarDecl *VD) {
  CGBuilderTy &Bld = CGF.Builder;

  // Compute size and alignment.
  llvm::Value *Size = CGF.getTypeSize(VD->getType());
  CharUnits Align = CGM.getContext().getDeclAlign(VD);
  Size = Bld.CreateNUWAdd(
      Size, llvm::ConstantInt::get(CGF.SizeTy, Align.getQuantity() - 1));
  llvm::Value *AlignVal =
      llvm::ConstantInt::get(CGF.SizeTy, Align.getQuantity());
  Size = Bld.CreateUDiv(Size, AlignVal);
  Size = Bld.CreateNUWMul(Size, AlignVal);

  // Allocate space for this VLA object to be globalized.
  llvm::Value *AllocArgs[] = {Size};
  llvm::CallBase *VoidPtr =
      CGF.EmitRuntimeCall(OMPBuilder.getOrCreateRuntimeFunction(
                              CGM.getModule(), OMPRTL___kmpc_alloc_shared),
                          AllocArgs, VD->getName());
  VoidPtr->addRetAttr(llvm::Attribute::get(
      CGM.getLLVMContext(), llvm::Attribute::Alignment, Align.getQuantity()));

  return std::make_pair(VoidPtr, Size);
}

void CGOpenMPRuntimeGPU::getKmpcFreeShared(
    CodeGenFunction &CGF,
    const std::pair<llvm::Value *, llvm::Value *> &AddrSizePair) {
  // Deallocate the memory for each globalized VLA object
  CGF.EmitRuntimeCall(OMPBuilder.getOrCreateRuntimeFunction(
                          CGM.getModule(), OMPRTL___kmpc_free_shared),
                      {AddrSizePair.first, AddrSizePair.second});
}

void CGOpenMPRuntimeGPU::emitGenericVarsEpilog(CodeGenFunction &CGF) {
  if (getDataSharingMode() != CGOpenMPRuntimeGPU::DS_Generic)
    return;

  const auto I = FunctionGlobalizedDecls.find(CGF.CurFn);
  if (I != FunctionGlobalizedDecls.end()) {
    // Deallocate the memory for each globalized VLA object that was
    // globalized in the prolog (i.e. emitGenericVarsProlog).
    for (const auto &AddrSizePair :
         llvm::reverse(I->getSecond().EscapedVariableLengthDeclsAddrs)) {
      CGF.EmitRuntimeCall(OMPBuilder.getOrCreateRuntimeFunction(
                              CGM.getModule(), OMPRTL___kmpc_free_shared),
                          {AddrSizePair.first, AddrSizePair.second});
    }
    // Deallocate the memory for each globalized value
    for (auto &Rec : llvm::reverse(I->getSecond().LocalVarData)) {
      const auto *VD = cast<VarDecl>(Rec.first);
      I->getSecond().MappedParams->restore(CGF);

      llvm::Value *FreeArgs[] = {Rec.second.GlobalizedVal,
                                 CGF.getTypeSize(VD->getType())};
      CGF.EmitRuntimeCall(OMPBuilder.getOrCreateRuntimeFunction(
                              CGM.getModule(), OMPRTL___kmpc_free_shared),
                          FreeArgs);
    }
  }
}

void CGOpenMPRuntimeGPU::emitTeamsCall(CodeGenFunction &CGF,
                                         const OMPExecutableDirective &D,
                                         SourceLocation Loc,
                                         llvm::Function *OutlinedFn,
                                         ArrayRef<llvm::Value *> CapturedVars) {
  if (!CGF.HaveInsertPoint())
    return;

  bool IsBareKernel = D.getSingleClause<OMPXBareClause>();

  RawAddress ZeroAddr = CGF.CreateDefaultAlignTempAlloca(CGF.Int32Ty,
                                                         /*Name=*/".zero.addr");
  CGF.Builder.CreateStore(CGF.Builder.getInt32(/*C*/ 0), ZeroAddr);
  llvm::SmallVector<llvm::Value *, 16> OutlinedFnArgs;
  // We don't emit any thread id function call in bare kernel, but because the
  // outlined function has a pointer argument, we emit a nullptr here.
  if (IsBareKernel)
    OutlinedFnArgs.push_back(llvm::ConstantPointerNull::get(CGM.VoidPtrTy));
  else
    OutlinedFnArgs.push_back(emitThreadIDAddress(CGF, Loc).emitRawPointer(CGF));
  OutlinedFnArgs.push_back(ZeroAddr.getPointer());
  OutlinedFnArgs.append(CapturedVars.begin(), CapturedVars.end());
  emitOutlinedFunctionCall(CGF, Loc, OutlinedFn, OutlinedFnArgs);
}

void CGOpenMPRuntimeGPU::emitParallelCall(CodeGenFunction &CGF,
                                          SourceLocation Loc,
                                          llvm::Function *OutlinedFn,
                                          ArrayRef<llvm::Value *> CapturedVars,
                                          const Expr *IfCond,
                                          llvm::Value *NumThreads) {
  if (!CGF.HaveInsertPoint())
    return;

  auto &&ParallelGen = [this, Loc, OutlinedFn, CapturedVars, IfCond,
                        NumThreads](CodeGenFunction &CGF,
                                    PrePostActionTy &Action) {
    CGBuilderTy &Bld = CGF.Builder;
    llvm::Value *NumThreadsVal = NumThreads;
    llvm::Function *WFn = WrapperFunctionsMap[OutlinedFn];
    llvm::Value *ID = llvm::ConstantPointerNull::get(CGM.Int8PtrTy);
    if (WFn)
      ID = Bld.CreateBitOrPointerCast(WFn, CGM.Int8PtrTy);
    llvm::Value *FnPtr = Bld.CreateBitOrPointerCast(OutlinedFn, CGM.Int8PtrTy);

    // Create a private scope that will globalize the arguments
    // passed from the outside of the target region.
    // TODO: Is that needed?
    CodeGenFunction::OMPPrivateScope PrivateArgScope(CGF);

    Address CapturedVarsAddrs = CGF.CreateDefaultAlignTempAlloca(
        llvm::ArrayType::get(CGM.VoidPtrTy, CapturedVars.size()),
        "captured_vars_addrs");
    // There's something to share.
    if (!CapturedVars.empty()) {
      // Prepare for parallel region. Indicate the outlined function.
      ASTContext &Ctx = CGF.getContext();
      unsigned Idx = 0;
      for (llvm::Value *V : CapturedVars) {
        Address Dst = Bld.CreateConstArrayGEP(CapturedVarsAddrs, Idx);
        llvm::Value *PtrV;
        if (V->getType()->isIntegerTy())
          PtrV = Bld.CreateIntToPtr(V, CGF.VoidPtrTy);
        else
          PtrV = Bld.CreatePointerBitCastOrAddrSpaceCast(V, CGF.VoidPtrTy);
        CGF.EmitStoreOfScalar(PtrV, Dst, /*Volatile=*/false,
                              Ctx.getPointerType(Ctx.VoidPtrTy));
        ++Idx;
      }
    }

    llvm::Value *IfCondVal = nullptr;
    if (IfCond)
      IfCondVal = Bld.CreateIntCast(CGF.EvaluateExprAsBool(IfCond), CGF.Int32Ty,
                                    /* isSigned */ false);
    else
      IfCondVal = llvm::ConstantInt::get(CGF.Int32Ty, 1);

    if (!NumThreadsVal)
      NumThreadsVal = llvm::ConstantInt::get(CGF.Int32Ty, -1);
    else
      NumThreadsVal = Bld.CreateZExtOrTrunc(NumThreadsVal, CGF.Int32Ty),

      assert(IfCondVal && "Expected a value");
    llvm::Value *RTLoc = emitUpdateLocation(CGF, Loc);
    llvm::Value *Args[] = {
        RTLoc,
        getThreadID(CGF, Loc),
        IfCondVal,
        NumThreadsVal,
        llvm::ConstantInt::get(CGF.Int32Ty, -1),
        FnPtr,
        ID,
        Bld.CreateBitOrPointerCast(CapturedVarsAddrs.emitRawPointer(CGF),
                                   CGF.VoidPtrPtrTy),
        llvm::ConstantInt::get(CGM.SizeTy, CapturedVars.size())};
    CGF.EmitRuntimeCall(OMPBuilder.getOrCreateRuntimeFunction(
                            CGM.getModule(), OMPRTL___kmpc_parallel_51),
                        Args);
  };

  RegionCodeGenTy RCG(ParallelGen);
  RCG(CGF);
}

void CGOpenMPRuntimeGPU::syncCTAThreads(CodeGenFunction &CGF) {
  // Always emit simple barriers!
  if (!CGF.HaveInsertPoint())
    return;
  // Build call __kmpc_barrier_simple_spmd(nullptr, 0);
  // This function does not use parameters, so we can emit just default values.
  llvm::Value *Args[] = {
      llvm::ConstantPointerNull::get(
          cast<llvm::PointerType>(getIdentTyPointerTy())),
      llvm::ConstantInt::get(CGF.Int32Ty, /*V=*/0, /*isSigned=*/true)};
  CGF.EmitRuntimeCall(OMPBuilder.getOrCreateRuntimeFunction(
                          CGM.getModule(), OMPRTL___kmpc_barrier_simple_spmd),
                      Args);
}

void CGOpenMPRuntimeGPU::emitBarrierCall(CodeGenFunction &CGF,
                                           SourceLocation Loc,
                                           OpenMPDirectiveKind Kind, bool,
                                           bool) {
  // Always emit simple barriers!
  if (!CGF.HaveInsertPoint())
    return;
  // Build call __kmpc_cancel_barrier(loc, thread_id);
  unsigned Flags = getDefaultFlagsForBarriers(Kind);
  llvm::Value *Args[] = {emitUpdateLocation(CGF, Loc, Flags),
                         getThreadID(CGF, Loc)};

  CGF.EmitRuntimeCall(OMPBuilder.getOrCreateRuntimeFunction(
                          CGM.getModule(), OMPRTL___kmpc_barrier),
                      Args);
}

void CGOpenMPRuntimeGPU::emitCriticalRegion(
    CodeGenFunction &CGF, StringRef CriticalName,
    const RegionCodeGenTy &CriticalOpGen, SourceLocation Loc,
    const Expr *Hint) {
  llvm::BasicBlock *LoopBB = CGF.createBasicBlock("omp.critical.loop");
  llvm::BasicBlock *TestBB = CGF.createBasicBlock("omp.critical.test");
  llvm::BasicBlock *SyncBB = CGF.createBasicBlock("omp.critical.sync");
  llvm::BasicBlock *BodyBB = CGF.createBasicBlock("omp.critical.body");
  llvm::BasicBlock *ExitBB = CGF.createBasicBlock("omp.critical.exit");

  auto &RT = static_cast<CGOpenMPRuntimeGPU &>(CGF.CGM.getOpenMPRuntime());

  // Get the mask of active threads in the warp.
  llvm::Value *Mask = CGF.EmitRuntimeCall(OMPBuilder.getOrCreateRuntimeFunction(
      CGM.getModule(), OMPRTL___kmpc_warp_active_thread_mask));
  // Fetch team-local id of the thread.
  llvm::Value *ThreadID = RT.getGPUThreadID(CGF);

  // Get the width of the team.
  llvm::Value *TeamWidth = RT.getGPUNumThreads(CGF);

  // Initialize the counter variable for the loop.
  QualType Int32Ty =
      CGF.getContext().getIntTypeForBitwidth(/*DestWidth=*/32, /*Signed=*/0);
  Address Counter = CGF.CreateMemTemp(Int32Ty, "critical_counter");
  LValue CounterLVal = CGF.MakeAddrLValue(Counter, Int32Ty);
  CGF.EmitStoreOfScalar(llvm::Constant::getNullValue(CGM.Int32Ty), CounterLVal,
                        /*isInit=*/true);

  // Block checks if loop counter exceeds upper bound.
  CGF.EmitBlock(LoopBB);
  llvm::Value *CounterVal = CGF.EmitLoadOfScalar(CounterLVal, Loc);
  llvm::Value *CmpLoopBound = CGF.Builder.CreateICmpSLT(CounterVal, TeamWidth);
  CGF.Builder.CreateCondBr(CmpLoopBound, TestBB, ExitBB);

  // Block tests which single thread should execute region, and which threads
  // should go straight to synchronisation point.
  CGF.EmitBlock(TestBB);
  CounterVal = CGF.EmitLoadOfScalar(CounterLVal, Loc);
  llvm::Value *CmpThreadToCounter =
      CGF.Builder.CreateICmpEQ(ThreadID, CounterVal);
  CGF.Builder.CreateCondBr(CmpThreadToCounter, BodyBB, SyncBB);

  // Block emits the body of the critical region.
  CGF.EmitBlock(BodyBB);

  // Output the critical statement.
  CGOpenMPRuntime::emitCriticalRegion(CGF, CriticalName, CriticalOpGen, Loc,
                                      Hint);

  // After the body surrounded by the critical region, the single executing
  // thread will jump to the synchronisation point.
  // Block waits for all threads in current team to finish then increments the
  // counter variable and returns to the loop.
  CGF.EmitBlock(SyncBB);
  // Reconverge active threads in the warp.
  (void)CGF.EmitRuntimeCall(OMPBuilder.getOrCreateRuntimeFunction(
                                CGM.getModule(), OMPRTL___kmpc_syncwarp),
                            Mask);

  llvm::Value *IncCounterVal =
      CGF.Builder.CreateNSWAdd(CounterVal, CGF.Builder.getInt32(1));
  CGF.EmitStoreOfScalar(IncCounterVal, CounterLVal);
  CGF.EmitBranch(LoopBB);

  // Block that is reached when  all threads in the team complete the region.
  CGF.EmitBlock(ExitBB, /*IsFinished=*/true);
}

/// Cast value to the specified type.
static llvm::Value *castValueToType(CodeGenFunction &CGF, llvm::Value *Val,
                                    QualType ValTy, QualType CastTy,
                                    SourceLocation Loc) {
  assert(!CGF.getContext().getTypeSizeInChars(CastTy).isZero() &&
         "Cast type must sized.");
  assert(!CGF.getContext().getTypeSizeInChars(ValTy).isZero() &&
         "Val type must sized.");
  llvm::Type *LLVMCastTy = CGF.ConvertTypeForMem(CastTy);
  if (ValTy == CastTy)
    return Val;
  if (CGF.getContext().getTypeSizeInChars(ValTy) ==
      CGF.getContext().getTypeSizeInChars(CastTy))
    return CGF.Builder.CreateBitCast(Val, LLVMCastTy);
  if (CastTy->isIntegerType() && ValTy->isIntegerType())
    return CGF.Builder.CreateIntCast(Val, LLVMCastTy,
                                     CastTy->hasSignedIntegerRepresentation());
  Address CastItem = CGF.CreateMemTemp(CastTy);
  Address ValCastItem = CastItem.withElementType(Val->getType());
  CGF.EmitStoreOfScalar(Val, ValCastItem, /*Volatile=*/false, ValTy,
                        LValueBaseInfo(AlignmentSource::Type),
                        TBAAAccessInfo());
  return CGF.EmitLoadOfScalar(CastItem, /*Volatile=*/false, CastTy, Loc,
                              LValueBaseInfo(AlignmentSource::Type),
                              TBAAAccessInfo());
}

///
/// Design of OpenMP reductions on the GPU
///
/// Consider a typical OpenMP program with one or more reduction
/// clauses:
///
/// float foo;
/// double bar;
/// #pragma omp target teams distribute parallel for \
///             reduction(+:foo) reduction(*:bar)
/// for (int i = 0; i < N; i++) {
///   foo += A[i]; bar *= B[i];
/// }
///
/// where 'foo' and 'bar' are reduced across all OpenMP threads in
/// all teams.  In our OpenMP implementation on the NVPTX device an
/// OpenMP team is mapped to a CUDA threadblock and OpenMP threads
/// within a team are mapped to CUDA threads within a threadblock.
/// Our goal is to efficiently aggregate values across all OpenMP
/// threads such that:
///
///   - the compiler and runtime are logically concise, and
///   - the reduction is performed efficiently in a hierarchical
///     manner as follows: within OpenMP threads in the same warp,
///     across warps in a threadblock, and finally across teams on
///     the NVPTX device.
///
/// Introduction to Decoupling
///
/// We would like to decouple the compiler and the runtime so that the
/// latter is ignorant of the reduction variables (number, data types)
/// and the reduction operators.  This allows a simpler interface
/// and implementation while still attaining good performance.
///
/// Pseudocode for the aforementioned OpenMP program generated by the
/// compiler is as follows:
///
/// 1. Create private copies of reduction variables on each OpenMP
///    thread: 'foo_private', 'bar_private'
/// 2. Each OpenMP thread reduces the chunk of 'A' and 'B' assigned
///    to it and writes the result in 'foo_private' and 'bar_private'
///    respectively.
/// 3. Call the OpenMP runtime on the GPU to reduce within a team
///    and store the result on the team master:
///
///     __kmpc_nvptx_parallel_reduce_nowait_v2(...,
///        reduceData, shuffleReduceFn, interWarpCpyFn)
///
///     where:
///       struct ReduceData {
///         double *foo;
///         double *bar;
///       } reduceData
///       reduceData.foo = &foo_private
///       reduceData.bar = &bar_private
///
///     'shuffleReduceFn' and 'interWarpCpyFn' are pointers to two
///     auxiliary functions generated by the compiler that operate on
///     variables of type 'ReduceData'.  They aid the runtime perform
///     algorithmic steps in a data agnostic manner.
///
///     'shuffleReduceFn' is a pointer to a function that reduces data
///     of type 'ReduceData' across two OpenMP threads (lanes) in the
///     same warp.  It takes the following arguments as input:
///
///     a. variable of type 'ReduceData' on the calling lane,
///     b. its lane_id,
///     c. an offset relative to the current lane_id to generate a
///        remote_lane_id.  The remote lane contains the second
///        variable of type 'ReduceData' that is to be reduced.
///     d. an algorithm version parameter determining which reduction
///        algorithm to use.
///
///     'shuffleReduceFn' retrieves data from the remote lane using
///     efficient GPU shuffle intrinsics and reduces, using the
///     algorithm specified by the 4th parameter, the two operands
///     element-wise.  The result is written to the first operand.
///
///     Different reduction algorithms are implemented in different
///     runtime functions, all calling 'shuffleReduceFn' to perform
///     the essential reduction step.  Therefore, based on the 4th
///     parameter, this function behaves slightly differently to
///     cooperate with the runtime to ensure correctness under
///     different circumstances.
///
///     'InterWarpCpyFn' is a pointer to a function that transfers
///     reduced variables across warps.  It tunnels, through CUDA
///     shared memory, the thread-private data of type 'ReduceData'
///     from lane 0 of each warp to a lane in the first warp.
/// 4. Call the OpenMP runtime on the GPU to reduce across teams.
///    The last team writes the global reduced value to memory.
///
///     ret = __kmpc_nvptx_teams_reduce_nowait(...,
///             reduceData, shuffleReduceFn, interWarpCpyFn,
///             scratchpadCopyFn, loadAndReduceFn)
///
///     'scratchpadCopyFn' is a helper that stores reduced
///     data from the team master to a scratchpad array in
///     global memory.
///
///     'loadAndReduceFn' is a helper that loads data from
///     the scratchpad array and reduces it with the input
///     operand.
///
///     These compiler generated functions hide address
///     calculation and alignment information from the runtime.
/// 5. if ret == 1:
///     The team master of the last team stores the reduced
///     result to the globals in memory.
///     foo += reduceData.foo; bar *= reduceData.bar
///
///
/// Warp Reduction Algorithms
///
/// On the warp level, we have three algorithms implemented in the
/// OpenMP runtime depending on the number of active lanes:
///
/// Full Warp Reduction
///
/// The reduce algorithm within a warp where all lanes are active
/// is implemented in the runtime as follows:
///
/// full_warp_reduce(void *reduce_data,
///                  kmp_ShuffleReductFctPtr ShuffleReduceFn) {
///   for (int offset = WARPSIZE/2; offset > 0; offset /= 2)
///     ShuffleReduceFn(reduce_data, 0, offset, 0);
/// }
///
/// The algorithm completes in log(2, WARPSIZE) steps.
///
/// 'ShuffleReduceFn' is used here with lane_id set to 0 because it is
/// not used therefore we save instructions by not retrieving lane_id
/// from the corresponding special registers.  The 4th parameter, which
/// represents the version of the algorithm being used, is set to 0 to
/// signify full warp reduction.
///
/// In this version, 'ShuffleReduceFn' behaves, per element, as follows:
///
/// #reduce_elem refers to an element in the local lane's data structure
/// #remote_elem is retrieved from a remote lane
/// remote_elem = shuffle_down(reduce_elem, offset, WARPSIZE);
/// reduce_elem = reduce_elem REDUCE_OP remote_elem;
///
/// Contiguous Partial Warp Reduction
///
/// This reduce algorithm is used within a warp where only the first
/// 'n' (n <= WARPSIZE) lanes are active.  It is typically used when the
/// number of OpenMP threads in a parallel region is not a multiple of
/// WARPSIZE.  The algorithm is implemented in the runtime as follows:
///
/// void
/// contiguous_partial_reduce(void *reduce_data,
///                           kmp_ShuffleReductFctPtr ShuffleReduceFn,
///                           int size, int lane_id) {
///   int curr_size;
///   int offset;
///   curr_size = size;
///   mask = curr_size/2;
///   while (offset>0) {
///     ShuffleReduceFn(reduce_data, lane_id, offset, 1);
///     curr_size = (curr_size+1)/2;
///     offset = curr_size/2;
///   }
/// }
///
/// In this version, 'ShuffleReduceFn' behaves, per element, as follows:
///
/// remote_elem = shuffle_down(reduce_elem, offset, WARPSIZE);
/// if (lane_id < offset)
///     reduce_elem = reduce_elem REDUCE_OP remote_elem
/// else
///     reduce_elem = remote_elem
///
/// This algorithm assumes that the data to be reduced are located in a
/// contiguous subset of lanes starting from the first.  When there is
/// an odd number of active lanes, the data in the last lane is not
/// aggregated with any other lane's dat but is instead copied over.
///
/// Dispersed Partial Warp Reduction
///
/// This algorithm is used within a warp when any discontiguous subset of
/// lanes are active.  It is used to implement the reduction operation
/// across lanes in an OpenMP simd region or in a nested parallel region.
///
/// void
/// dispersed_partial_reduce(void *reduce_data,
///                          kmp_ShuffleReductFctPtr ShuffleReduceFn) {
///   int size, remote_id;
///   int logical_lane_id = number_of_active_lanes_before_me() * 2;
///   do {
///       remote_id = next_active_lane_id_right_after_me();
///       # the above function returns 0 of no active lane
///       # is present right after the current lane.
///       size = number_of_active_lanes_in_this_warp();
///       logical_lane_id /= 2;
///       ShuffleReduceFn(reduce_data, logical_lane_id,
///                       remote_id-1-threadIdx.x, 2);
///   } while (logical_lane_id % 2 == 0 && size > 1);
/// }
///
/// There is no assumption made about the initial state of the reduction.
/// Any number of lanes (>=1) could be active at any position.  The reduction
/// result is returned in the first active lane.
///
/// In this version, 'ShuffleReduceFn' behaves, per element, as follows:
///
/// remote_elem = shuffle_down(reduce_elem, offset, WARPSIZE);
/// if (lane_id % 2 == 0 && offset > 0)
///     reduce_elem = reduce_elem REDUCE_OP remote_elem
/// else
///     reduce_elem = remote_elem
///
///
/// Intra-Team Reduction
///
/// This function, as implemented in the runtime call
/// '__kmpc_nvptx_parallel_reduce_nowait_v2', aggregates data across OpenMP
/// threads in a team.  It first reduces within a warp using the
/// aforementioned algorithms.  We then proceed to gather all such
/// reduced values at the first warp.
///
/// The runtime makes use of the function 'InterWarpCpyFn', which copies
/// data from each of the "warp master" (zeroth lane of each warp, where
/// warp-reduced data is held) to the zeroth warp.  This step reduces (in
/// a mathematical sense) the problem of reduction across warp masters in
/// a block to the problem of warp reduction.
///
///
/// Inter-Team Reduction
///
/// Once a team has reduced its data to a single value, it is stored in
/// a global scratchpad array.  Since each team has a distinct slot, this
/// can be done without locking.
///
/// The last team to write to the scratchpad array proceeds to reduce the
/// scratchpad array.  One or more workers in the last team use the helper
/// 'loadAndReduceDataFn' to load and reduce values from the array, i.e.,
/// the k'th worker reduces every k'th element.
///
/// Finally, a call is made to '__kmpc_nvptx_parallel_reduce_nowait_v2' to
/// reduce across workers and compute a globally reduced value.
///
void CGOpenMPRuntimeGPU::emitReduction(
    CodeGenFunction &CGF, SourceLocation Loc, ArrayRef<const Expr *> Privates,
    ArrayRef<const Expr *> LHSExprs, ArrayRef<const Expr *> RHSExprs,
    ArrayRef<const Expr *> ReductionOps, ReductionOptionsTy Options) {
  if (!CGF.HaveInsertPoint())
    return;

  bool ParallelReduction = isOpenMPParallelDirective(Options.ReductionKind);
  bool DistributeReduction = isOpenMPDistributeDirective(Options.ReductionKind);
  bool TeamsReduction = isOpenMPTeamsDirective(Options.ReductionKind);

  ASTContext &C = CGM.getContext();

  if (Options.SimpleReduction) {
    assert(!TeamsReduction && !ParallelReduction &&
           "Invalid reduction selection in emitReduction.");
    (void)ParallelReduction;
    CGOpenMPRuntime::emitReduction(CGF, Loc, Privates, LHSExprs, RHSExprs,
                                   ReductionOps, Options);
    return;
  }

  llvm::SmallDenseMap<const ValueDecl *, const FieldDecl *> VarFieldMap;
  llvm::SmallVector<const ValueDecl *, 4> PrivatesReductions(Privates.size());
  int Cnt = 0;
  for (const Expr *DRE : Privates) {
    PrivatesReductions[Cnt] = cast<DeclRefExpr>(DRE)->getDecl();
    ++Cnt;
  }
  const RecordDecl *ReductionRec = ::buildRecordForGlobalizedVars(
      CGM.getContext(), PrivatesReductions, std::nullopt, VarFieldMap, 1);

  if (TeamsReduction)
    TeamsReductions.push_back(ReductionRec);

  // Source location for the ident struct
  llvm::Value *RTLoc = emitUpdateLocation(CGF, Loc);

  using InsertPointTy = llvm::OpenMPIRBuilder::InsertPointTy;
  InsertPointTy AllocaIP(CGF.AllocaInsertPt->getParent(),
                         CGF.AllocaInsertPt->getIterator());
  InsertPointTy CodeGenIP(CGF.Builder.GetInsertBlock(),
                          CGF.Builder.GetInsertPoint());
  llvm::OpenMPIRBuilder::LocationDescription OmpLoc(
      CodeGenIP, CGF.SourceLocToDebugLoc(Loc));
  llvm::SmallVector<llvm::OpenMPIRBuilder::ReductionInfo> ReductionInfos;

  CodeGenFunction::OMPPrivateScope Scope(CGF);
  unsigned Idx = 0;
  for (const Expr *Private : Privates) {
    llvm::Type *ElementType;
    llvm::Value *Variable;
    llvm::Value *PrivateVariable;
    llvm::OpenMPIRBuilder::ReductionGenAtomicCBTy AtomicReductionGen = nullptr;
    ElementType = CGF.ConvertTypeForMem(Private->getType());
    const auto *RHSVar =
        cast<VarDecl>(cast<DeclRefExpr>(RHSExprs[Idx])->getDecl());
    PrivateVariable = CGF.GetAddrOfLocalVar(RHSVar).emitRawPointer(CGF);
    const auto *LHSVar =
        cast<VarDecl>(cast<DeclRefExpr>(LHSExprs[Idx])->getDecl());
    Variable = CGF.GetAddrOfLocalVar(LHSVar).emitRawPointer(CGF);
    llvm::OpenMPIRBuilder::EvalKind EvalKind;
    switch (CGF.getEvaluationKind(Private->getType())) {
    case TEK_Scalar:
      EvalKind = llvm::OpenMPIRBuilder::EvalKind::Scalar;
      break;
    case TEK_Complex:
      EvalKind = llvm::OpenMPIRBuilder::EvalKind::Complex;
      break;
    case TEK_Aggregate:
      EvalKind = llvm::OpenMPIRBuilder::EvalKind::Aggregate;
      break;
    }
    auto ReductionGen = [&](InsertPointTy CodeGenIP, unsigned I,
                            llvm::Value **LHSPtr, llvm::Value **RHSPtr,
                            llvm::Function *NewFunc) {
      CGF.Builder.restoreIP(CodeGenIP);
      auto *CurFn = CGF.CurFn;
      CGF.CurFn = NewFunc;

      *LHSPtr = CGF.GetAddrOfLocalVar(
                       cast<VarDecl>(cast<DeclRefExpr>(LHSExprs[I])->getDecl()))
                    .emitRawPointer(CGF);
      *RHSPtr = CGF.GetAddrOfLocalVar(
                       cast<VarDecl>(cast<DeclRefExpr>(RHSExprs[I])->getDecl()))
                    .emitRawPointer(CGF);

      emitSingleReductionCombiner(CGF, ReductionOps[I], Privates[I],
                                  cast<DeclRefExpr>(LHSExprs[I]),
                                  cast<DeclRefExpr>(RHSExprs[I]));

      CGF.CurFn = CurFn;

      return InsertPointTy(CGF.Builder.GetInsertBlock(),
                           CGF.Builder.GetInsertPoint());
    };
    ReductionInfos.emplace_back(llvm::OpenMPIRBuilder::ReductionInfo(
        ElementType, Variable, PrivateVariable, EvalKind,
        /*ReductionGen=*/nullptr, ReductionGen, AtomicReductionGen));
    Idx++;
  }

  CGF.Builder.restoreIP(OMPBuilder.createReductionsGPU(
      OmpLoc, AllocaIP, CodeGenIP, ReductionInfos, false, TeamsReduction,
      DistributeReduction, llvm::OpenMPIRBuilder::ReductionGenCBKind::Clang,
      CGF.getTarget().getGridValue(), C.getLangOpts().OpenMPCUDAReductionBufNum,
      RTLoc));
  return;
}

const VarDecl *
CGOpenMPRuntimeGPU::translateParameter(const FieldDecl *FD,
                                       const VarDecl *NativeParam) const {
  if (!NativeParam->getType()->isReferenceType())
    return NativeParam;
  QualType ArgType = NativeParam->getType();
  QualifierCollector QC;
  const Type *NonQualTy = QC.strip(ArgType);
  QualType PointeeTy = cast<ReferenceType>(NonQualTy)->getPointeeType();
  if (const auto *Attr = FD->getAttr<OMPCaptureKindAttr>()) {
    if (Attr->getCaptureKind() == OMPC_map) {
      PointeeTy = CGM.getContext().getAddrSpaceQualType(PointeeTy,
                                                        LangAS::opencl_global);
    }
  }
  ArgType = CGM.getContext().getPointerType(PointeeTy);
  QC.addRestrict();
  enum { NVPTX_local_addr = 5 };
  QC.addAddressSpace(getLangASFromTargetAS(NVPTX_local_addr));
  ArgType = QC.apply(CGM.getContext(), ArgType);
  if (isa<ImplicitParamDecl>(NativeParam))
    return ImplicitParamDecl::Create(
        CGM.getContext(), /*DC=*/nullptr, NativeParam->getLocation(),
        NativeParam->getIdentifier(), ArgType, ImplicitParamKind::Other);
  return ParmVarDecl::Create(
      CGM.getContext(),
      const_cast<DeclContext *>(NativeParam->getDeclContext()),
      NativeParam->getBeginLoc(), NativeParam->getLocation(),
      NativeParam->getIdentifier(), ArgType,
      /*TInfo=*/nullptr, SC_None, /*DefArg=*/nullptr);
}

Address
CGOpenMPRuntimeGPU::getParameterAddress(CodeGenFunction &CGF,
                                          const VarDecl *NativeParam,
                                          const VarDecl *TargetParam) const {
  assert(NativeParam != TargetParam &&
         NativeParam->getType()->isReferenceType() &&
         "Native arg must not be the same as target arg.");
  Address LocalAddr = CGF.GetAddrOfLocalVar(TargetParam);
  QualType NativeParamType = NativeParam->getType();
  QualifierCollector QC;
  const Type *NonQualTy = QC.strip(NativeParamType);
  QualType NativePointeeTy = cast<ReferenceType>(NonQualTy)->getPointeeType();
  unsigned NativePointeeAddrSpace =
      CGF.getTypes().getTargetAddressSpace(NativePointeeTy);
  QualType TargetTy = TargetParam->getType();
  llvm::Value *TargetAddr = CGF.EmitLoadOfScalar(LocalAddr, /*Volatile=*/false,
                                                 TargetTy, SourceLocation());
  // Cast to native address space.
  TargetAddr = CGF.Builder.CreatePointerBitCastOrAddrSpaceCast(
      TargetAddr,
      llvm::PointerType::get(CGF.getLLVMContext(), NativePointeeAddrSpace));
  Address NativeParamAddr = CGF.CreateMemTemp(NativeParamType);
  CGF.EmitStoreOfScalar(TargetAddr, NativeParamAddr, /*Volatile=*/false,
                        NativeParamType);
  return NativeParamAddr;
}

void CGOpenMPRuntimeGPU::emitOutlinedFunctionCall(
    CodeGenFunction &CGF, SourceLocation Loc, llvm::FunctionCallee OutlinedFn,
    ArrayRef<llvm::Value *> Args) const {
  SmallVector<llvm::Value *, 4> TargetArgs;
  TargetArgs.reserve(Args.size());
  auto *FnType = OutlinedFn.getFunctionType();
  for (unsigned I = 0, E = Args.size(); I < E; ++I) {
    if (FnType->isVarArg() && FnType->getNumParams() <= I) {
      TargetArgs.append(std::next(Args.begin(), I), Args.end());
      break;
    }
    llvm::Type *TargetType = FnType->getParamType(I);
    llvm::Value *NativeArg = Args[I];
    if (!TargetType->isPointerTy()) {
      TargetArgs.emplace_back(NativeArg);
      continue;
    }
    TargetArgs.emplace_back(
        CGF.Builder.CreatePointerBitCastOrAddrSpaceCast(NativeArg, TargetType));
  }
  CGOpenMPRuntime::emitOutlinedFunctionCall(CGF, Loc, OutlinedFn, TargetArgs);
}

/// Emit function which wraps the outline parallel region
/// and controls the arguments which are passed to this function.
/// The wrapper ensures that the outlined function is called
/// with the correct arguments when data is shared.
llvm::Function *CGOpenMPRuntimeGPU::createParallelDataSharingWrapper(
    llvm::Function *OutlinedParallelFn, const OMPExecutableDirective &D) {
  ASTContext &Ctx = CGM.getContext();
  const auto &CS = *D.getCapturedStmt(OMPD_parallel);

  // Create a function that takes as argument the source thread.
  FunctionArgList WrapperArgs;
  QualType Int16QTy =
      Ctx.getIntTypeForBitwidth(/*DestWidth=*/16, /*Signed=*/false);
  QualType Int32QTy =
      Ctx.getIntTypeForBitwidth(/*DestWidth=*/32, /*Signed=*/false);
  ImplicitParamDecl ParallelLevelArg(Ctx, /*DC=*/nullptr, D.getBeginLoc(),
                                     /*Id=*/nullptr, Int16QTy,
                                     ImplicitParamKind::Other);
  ImplicitParamDecl WrapperArg(Ctx, /*DC=*/nullptr, D.getBeginLoc(),
                               /*Id=*/nullptr, Int32QTy,
                               ImplicitParamKind::Other);
  WrapperArgs.emplace_back(&ParallelLevelArg);
  WrapperArgs.emplace_back(&WrapperArg);

  const CGFunctionInfo &CGFI =
      CGM.getTypes().arrangeBuiltinFunctionDeclaration(Ctx.VoidTy, WrapperArgs);

  auto *Fn = llvm::Function::Create(
      CGM.getTypes().GetFunctionType(CGFI), llvm::GlobalValue::InternalLinkage,
      Twine(OutlinedParallelFn->getName(), "_wrapper"), &CGM.getModule());

  // Ensure we do not inline the function. This is trivially true for the ones
  // passed to __kmpc_fork_call but the ones calles in serialized regions
  // could be inlined. This is not a perfect but it is closer to the invariant
  // we want, namely, every data environment starts with a new function.
  // TODO: We should pass the if condition to the runtime function and do the
  //       handling there. Much cleaner code.
  Fn->addFnAttr(llvm::Attribute::NoInline);

  CGM.SetInternalFunctionAttributes(GlobalDecl(), Fn, CGFI);
  Fn->setLinkage(llvm::GlobalValue::InternalLinkage);
  Fn->setDoesNotRecurse();

  CodeGenFunction CGF(CGM, /*suppressNewContext=*/true);
  CGF.StartFunction(GlobalDecl(), Ctx.VoidTy, Fn, CGFI, WrapperArgs,
                    D.getBeginLoc(), D.getBeginLoc());

  const auto *RD = CS.getCapturedRecordDecl();
  auto CurField = RD->field_begin();

  Address ZeroAddr = CGF.CreateDefaultAlignTempAlloca(CGF.Int32Ty,
                                                      /*Name=*/".zero.addr");
  CGF.Builder.CreateStore(CGF.Builder.getInt32(/*C*/ 0), ZeroAddr);
  // Get the array of arguments.
  SmallVector<llvm::Value *, 8> Args;

  Args.emplace_back(CGF.GetAddrOfLocalVar(&WrapperArg).emitRawPointer(CGF));
  Args.emplace_back(ZeroAddr.emitRawPointer(CGF));

  CGBuilderTy &Bld = CGF.Builder;
  auto CI = CS.capture_begin();

  // Use global memory for data sharing.
  // Handle passing of global args to workers.
  RawAddress GlobalArgs =
      CGF.CreateDefaultAlignTempAlloca(CGF.VoidPtrPtrTy, "global_args");
  llvm::Value *GlobalArgsPtr = GlobalArgs.getPointer();
  llvm::Value *DataSharingArgs[] = {GlobalArgsPtr};
  CGF.EmitRuntimeCall(OMPBuilder.getOrCreateRuntimeFunction(
                          CGM.getModule(), OMPRTL___kmpc_get_shared_variables),
                      DataSharingArgs);

  // Retrieve the shared variables from the list of references returned
  // by the runtime. Pass the variables to the outlined function.
  Address SharedArgListAddress = Address::invalid();
  if (CS.capture_size() > 0 ||
      isOpenMPLoopBoundSharingDirective(D.getDirectiveKind())) {
    SharedArgListAddress = CGF.EmitLoadOfPointer(
        GlobalArgs, CGF.getContext()
                        .getPointerType(CGF.getContext().VoidPtrTy)
                        .castAs<PointerType>());
  }
  unsigned Idx = 0;
  if (isOpenMPLoopBoundSharingDirective(D.getDirectiveKind())) {
    Address Src = Bld.CreateConstInBoundsGEP(SharedArgListAddress, Idx);
    Address TypedAddress = Bld.CreatePointerBitCastOrAddrSpaceCast(
        Src, CGF.SizeTy->getPointerTo(), CGF.SizeTy);
    llvm::Value *LB = CGF.EmitLoadOfScalar(
        TypedAddress,
        /*Volatile=*/false,
        CGF.getContext().getPointerType(CGF.getContext().getSizeType()),
        cast<OMPLoopDirective>(D).getLowerBoundVariable()->getExprLoc());
    Args.emplace_back(LB);
    ++Idx;
    Src = Bld.CreateConstInBoundsGEP(SharedArgListAddress, Idx);
    TypedAddress = Bld.CreatePointerBitCastOrAddrSpaceCast(
        Src, CGF.SizeTy->getPointerTo(), CGF.SizeTy);
    llvm::Value *UB = CGF.EmitLoadOfScalar(
        TypedAddress,
        /*Volatile=*/false,
        CGF.getContext().getPointerType(CGF.getContext().getSizeType()),
        cast<OMPLoopDirective>(D).getUpperBoundVariable()->getExprLoc());
    Args.emplace_back(UB);
    ++Idx;
  }
  if (CS.capture_size() > 0) {
    ASTContext &CGFContext = CGF.getContext();
    for (unsigned I = 0, E = CS.capture_size(); I < E; ++I, ++CI, ++CurField) {
      QualType ElemTy = CurField->getType();
      Address Src = Bld.CreateConstInBoundsGEP(SharedArgListAddress, I + Idx);
      Address TypedAddress = Bld.CreatePointerBitCastOrAddrSpaceCast(
          Src, CGF.ConvertTypeForMem(CGFContext.getPointerType(ElemTy)),
          CGF.ConvertTypeForMem(ElemTy));
      llvm::Value *Arg = CGF.EmitLoadOfScalar(TypedAddress,
                                              /*Volatile=*/false,
                                              CGFContext.getPointerType(ElemTy),
                                              CI->getLocation());
      if (CI->capturesVariableByCopy() &&
          !CI->getCapturedVar()->getType()->isAnyPointerType()) {
        Arg = castValueToType(CGF, Arg, ElemTy, CGFContext.getUIntPtrType(),
                              CI->getLocation());
      }
      Args.emplace_back(Arg);
    }
  }

  emitOutlinedFunctionCall(CGF, D.getBeginLoc(), OutlinedParallelFn, Args);
  CGF.FinishFunction();
  return Fn;
}

void CGOpenMPRuntimeGPU::emitFunctionProlog(CodeGenFunction &CGF,
                                              const Decl *D) {
  if (getDataSharingMode() != CGOpenMPRuntimeGPU::DS_Generic)
    return;

  assert(D && "Expected function or captured|block decl.");
  assert(FunctionGlobalizedDecls.count(CGF.CurFn) == 0 &&
         "Function is registered already.");
  assert((!TeamAndReductions.first || TeamAndReductions.first == D) &&
         "Team is set but not processed.");
  const Stmt *Body = nullptr;
  bool NeedToDelayGlobalization = false;
  if (const auto *FD = dyn_cast<FunctionDecl>(D)) {
    Body = FD->getBody();
  } else if (const auto *BD = dyn_cast<BlockDecl>(D)) {
    Body = BD->getBody();
  } else if (const auto *CD = dyn_cast<CapturedDecl>(D)) {
    Body = CD->getBody();
    NeedToDelayGlobalization = CGF.CapturedStmtInfo->getKind() == CR_OpenMP;
    if (NeedToDelayGlobalization &&
        getExecutionMode() == CGOpenMPRuntimeGPU::EM_SPMD)
      return;
  }
  if (!Body)
    return;
  CheckVarsEscapingDeclContext VarChecker(CGF, TeamAndReductions.second);
  VarChecker.Visit(Body);
  const RecordDecl *GlobalizedVarsRecord =
      VarChecker.getGlobalizedRecord(IsInTTDRegion);
  TeamAndReductions.first = nullptr;
  TeamAndReductions.second.clear();
  ArrayRef<const ValueDecl *> EscapedVariableLengthDecls =
      VarChecker.getEscapedVariableLengthDecls();
  ArrayRef<const ValueDecl *> DelayedVariableLengthDecls =
      VarChecker.getDelayedVariableLengthDecls();
  if (!GlobalizedVarsRecord && EscapedVariableLengthDecls.empty() &&
      DelayedVariableLengthDecls.empty())
    return;
  auto I = FunctionGlobalizedDecls.try_emplace(CGF.CurFn).first;
  I->getSecond().MappedParams =
      std::make_unique<CodeGenFunction::OMPMapVars>();
  I->getSecond().EscapedParameters.insert(
      VarChecker.getEscapedParameters().begin(),
      VarChecker.getEscapedParameters().end());
  I->getSecond().EscapedVariableLengthDecls.append(
      EscapedVariableLengthDecls.begin(), EscapedVariableLengthDecls.end());
  I->getSecond().DelayedVariableLengthDecls.append(
      DelayedVariableLengthDecls.begin(), DelayedVariableLengthDecls.end());
  DeclToAddrMapTy &Data = I->getSecond().LocalVarData;
  for (const ValueDecl *VD : VarChecker.getEscapedDecls()) {
    assert(VD->isCanonicalDecl() && "Expected canonical declaration");
    Data.insert(std::make_pair(VD, MappedVarData()));
  }
  if (!NeedToDelayGlobalization) {
    emitGenericVarsProlog(CGF, D->getBeginLoc());
    struct GlobalizationScope final : EHScopeStack::Cleanup {
      GlobalizationScope() = default;

      void Emit(CodeGenFunction &CGF, Flags flags) override {
        static_cast<CGOpenMPRuntimeGPU &>(CGF.CGM.getOpenMPRuntime())
            .emitGenericVarsEpilog(CGF);
      }
    };
    CGF.EHStack.pushCleanup<GlobalizationScope>(NormalAndEHCleanup);
  }
}

Address CGOpenMPRuntimeGPU::getAddressOfLocalVariable(CodeGenFunction &CGF,
                                                        const VarDecl *VD) {
  if (VD && VD->hasAttr<OMPAllocateDeclAttr>()) {
    const auto *A = VD->getAttr<OMPAllocateDeclAttr>();
    auto AS = LangAS::Default;
    switch (A->getAllocatorType()) {
      // Use the default allocator here as by default local vars are
      // threadlocal.
    case OMPAllocateDeclAttr::OMPNullMemAlloc:
    case OMPAllocateDeclAttr::OMPDefaultMemAlloc:
    case OMPAllocateDeclAttr::OMPThreadMemAlloc:
    case OMPAllocateDeclAttr::OMPHighBWMemAlloc:
    case OMPAllocateDeclAttr::OMPLowLatMemAlloc:
      // Follow the user decision - use default allocation.
      return Address::invalid();
    case OMPAllocateDeclAttr::OMPUserDefinedMemAlloc:
      // TODO: implement aupport for user-defined allocators.
      return Address::invalid();
    case OMPAllocateDeclAttr::OMPConstMemAlloc:
      AS = LangAS::cuda_constant;
      break;
    case OMPAllocateDeclAttr::OMPPTeamMemAlloc:
      AS = LangAS::cuda_shared;
      break;
    case OMPAllocateDeclAttr::OMPLargeCapMemAlloc:
    case OMPAllocateDeclAttr::OMPCGroupMemAlloc:
      break;
    }
    llvm::Type *VarTy = CGF.ConvertTypeForMem(VD->getType());
    auto *GV = new llvm::GlobalVariable(
        CGM.getModule(), VarTy, /*isConstant=*/false,
        llvm::GlobalValue::InternalLinkage, llvm::PoisonValue::get(VarTy),
        VD->getName(),
        /*InsertBefore=*/nullptr, llvm::GlobalValue::NotThreadLocal,
        CGM.getContext().getTargetAddressSpace(AS));
    CharUnits Align = CGM.getContext().getDeclAlign(VD);
    GV->setAlignment(Align.getAsAlign());
    return Address(
        CGF.Builder.CreatePointerBitCastOrAddrSpaceCast(
            GV, VarTy->getPointerTo(CGM.getContext().getTargetAddressSpace(
                    VD->getType().getAddressSpace()))),
        VarTy, Align);
  }

  if (getDataSharingMode() != CGOpenMPRuntimeGPU::DS_Generic)
    return Address::invalid();

  VD = VD->getCanonicalDecl();
  auto I = FunctionGlobalizedDecls.find(CGF.CurFn);
  if (I == FunctionGlobalizedDecls.end())
    return Address::invalid();
  auto VDI = I->getSecond().LocalVarData.find(VD);
  if (VDI != I->getSecond().LocalVarData.end())
    return VDI->second.PrivateAddr;
  if (VD->hasAttrs()) {
    for (specific_attr_iterator<OMPReferencedVarAttr> IT(VD->attr_begin()),
         E(VD->attr_end());
         IT != E; ++IT) {
      auto VDI = I->getSecond().LocalVarData.find(
          cast<VarDecl>(cast<DeclRefExpr>(IT->getRef())->getDecl())
              ->getCanonicalDecl());
      if (VDI != I->getSecond().LocalVarData.end())
        return VDI->second.PrivateAddr;
    }
  }

  return Address::invalid();
}

void CGOpenMPRuntimeGPU::functionFinished(CodeGenFunction &CGF) {
  FunctionGlobalizedDecls.erase(CGF.CurFn);
  CGOpenMPRuntime::functionFinished(CGF);
}

void CGOpenMPRuntimeGPU::getDefaultDistScheduleAndChunk(
    CodeGenFunction &CGF, const OMPLoopDirective &S,
    OpenMPDistScheduleClauseKind &ScheduleKind,
    llvm::Value *&Chunk) const {
  auto &RT = static_cast<CGOpenMPRuntimeGPU &>(CGF.CGM.getOpenMPRuntime());
  if (getExecutionMode() == CGOpenMPRuntimeGPU::EM_SPMD) {
    ScheduleKind = OMPC_DIST_SCHEDULE_static;
    Chunk = CGF.EmitScalarConversion(
        RT.getGPUNumThreads(CGF),
        CGF.getContext().getIntTypeForBitwidth(32, /*Signed=*/0),
        S.getIterationVariable()->getType(), S.getBeginLoc());
    return;
  }
  CGOpenMPRuntime::getDefaultDistScheduleAndChunk(
      CGF, S, ScheduleKind, Chunk);
}

void CGOpenMPRuntimeGPU::getDefaultScheduleAndChunk(
    CodeGenFunction &CGF, const OMPLoopDirective &S,
    OpenMPScheduleClauseKind &ScheduleKind,
    const Expr *&ChunkExpr) const {
  ScheduleKind = OMPC_SCHEDULE_static;
  // Chunk size is 1 in this case.
  llvm::APInt ChunkSize(32, 1);
  ChunkExpr = IntegerLiteral::Create(CGF.getContext(), ChunkSize,
      CGF.getContext().getIntTypeForBitwidth(32, /*Signed=*/0),
      SourceLocation());
}

void CGOpenMPRuntimeGPU::adjustTargetSpecificDataForLambdas(
    CodeGenFunction &CGF, const OMPExecutableDirective &D) const {
  assert(isOpenMPTargetExecutionDirective(D.getDirectiveKind()) &&
         " Expected target-based directive.");
  const CapturedStmt *CS = D.getCapturedStmt(OMPD_target);
  for (const CapturedStmt::Capture &C : CS->captures()) {
    // Capture variables captured by reference in lambdas for target-based
    // directives.
    if (!C.capturesVariable())
      continue;
    const VarDecl *VD = C.getCapturedVar();
    const auto *RD = VD->getType()
                         .getCanonicalType()
                         .getNonReferenceType()
                         ->getAsCXXRecordDecl();
    if (!RD || !RD->isLambda())
      continue;
    Address VDAddr = CGF.GetAddrOfLocalVar(VD);
    LValue VDLVal;
    if (VD->getType().getCanonicalType()->isReferenceType())
      VDLVal = CGF.EmitLoadOfReferenceLValue(VDAddr, VD->getType());
    else
      VDLVal = CGF.MakeAddrLValue(
          VDAddr, VD->getType().getCanonicalType().getNonReferenceType());
    llvm::DenseMap<const ValueDecl *, FieldDecl *> Captures;
    FieldDecl *ThisCapture = nullptr;
    RD->getCaptureFields(Captures, ThisCapture);
    if (ThisCapture && CGF.CapturedStmtInfo->isCXXThisExprCaptured()) {
      LValue ThisLVal =
          CGF.EmitLValueForFieldInitialization(VDLVal, ThisCapture);
      llvm::Value *CXXThis = CGF.LoadCXXThis();
      CGF.EmitStoreOfScalar(CXXThis, ThisLVal);
    }
    for (const LambdaCapture &LC : RD->captures()) {
      if (LC.getCaptureKind() != LCK_ByRef)
        continue;
      const ValueDecl *VD = LC.getCapturedVar();
      // FIXME: For now VD is always a VarDecl because OpenMP does not support
      //  capturing structured bindings in lambdas yet.
      if (!CS->capturesVariable(cast<VarDecl>(VD)))
        continue;
      auto It = Captures.find(VD);
      assert(It != Captures.end() && "Found lambda capture without field.");
      LValue VarLVal = CGF.EmitLValueForFieldInitialization(VDLVal, It->second);
      Address VDAddr = CGF.GetAddrOfLocalVar(cast<VarDecl>(VD));
      if (VD->getType().getCanonicalType()->isReferenceType())
        VDAddr = CGF.EmitLoadOfReferenceLValue(VDAddr,
                                               VD->getType().getCanonicalType())
                     .getAddress();
      CGF.EmitStoreOfScalar(VDAddr.emitRawPointer(CGF), VarLVal);
    }
  }
}

bool CGOpenMPRuntimeGPU::hasAllocateAttributeForGlobalVar(const VarDecl *VD,
                                                            LangAS &AS) {
  if (!VD || !VD->hasAttr<OMPAllocateDeclAttr>())
    return false;
  const auto *A = VD->getAttr<OMPAllocateDeclAttr>();
  switch(A->getAllocatorType()) {
  case OMPAllocateDeclAttr::OMPNullMemAlloc:
  case OMPAllocateDeclAttr::OMPDefaultMemAlloc:
  // Not supported, fallback to the default mem space.
  case OMPAllocateDeclAttr::OMPThreadMemAlloc:
  case OMPAllocateDeclAttr::OMPLargeCapMemAlloc:
  case OMPAllocateDeclAttr::OMPCGroupMemAlloc:
  case OMPAllocateDeclAttr::OMPHighBWMemAlloc:
  case OMPAllocateDeclAttr::OMPLowLatMemAlloc:
    AS = LangAS::Default;
    return true;
  case OMPAllocateDeclAttr::OMPConstMemAlloc:
    AS = LangAS::cuda_constant;
    return true;
  case OMPAllocateDeclAttr::OMPPTeamMemAlloc:
    AS = LangAS::cuda_shared;
    return true;
  case OMPAllocateDeclAttr::OMPUserDefinedMemAlloc:
    llvm_unreachable("Expected predefined allocator for the variables with the "
                     "static storage.");
  }
  return false;
}

// Get current OffloadArch and ignore any unknown values
static OffloadArch getOffloadArch(CodeGenModule &CGM) {
  if (!CGM.getTarget().hasFeature("ptx"))
    return OffloadArch::UNKNOWN;
  for (const auto &Feature : CGM.getTarget().getTargetOpts().FeatureMap) {
    if (Feature.getValue()) {
      OffloadArch Arch = StringToOffloadArch(Feature.getKey());
      if (Arch != OffloadArch::UNKNOWN)
        return Arch;
    }
  }
  return OffloadArch::UNKNOWN;
}

/// Check to see if target architecture supports unified addressing which is
/// a restriction for OpenMP requires clause "unified_shared_memory".
void CGOpenMPRuntimeGPU::processRequiresDirective(const OMPRequiresDecl *D) {
  for (const OMPClause *Clause : D->clauselists()) {
    if (Clause->getClauseKind() == OMPC_unified_shared_memory) {
      OffloadArch Arch = getOffloadArch(CGM);
      switch (Arch) {
      case OffloadArch::SM_20:
      case OffloadArch::SM_21:
      case OffloadArch::SM_30:
      case OffloadArch::SM_32_:
      case OffloadArch::SM_35:
      case OffloadArch::SM_37:
      case OffloadArch::SM_50:
      case OffloadArch::SM_52:
      case OffloadArch::SM_53: {
        SmallString<256> Buffer;
        llvm::raw_svector_ostream Out(Buffer);
        Out << "Target architecture " << OffloadArchToString(Arch)
            << " does not support unified addressing";
        CGM.Error(Clause->getBeginLoc(), Out.str());
        return;
      }
      case OffloadArch::SM_60:
      case OffloadArch::SM_61:
      case OffloadArch::SM_62:
      case OffloadArch::SM_70:
      case OffloadArch::SM_72:
      case OffloadArch::SM_75:
      case OffloadArch::SM_80:
      case OffloadArch::SM_86:
      case OffloadArch::SM_87:
      case OffloadArch::SM_89:
      case OffloadArch::SM_90:
      case OffloadArch::SM_90a:
      case OffloadArch::GFX600:
      case OffloadArch::GFX601:
      case OffloadArch::GFX602:
      case OffloadArch::GFX700:
      case OffloadArch::GFX701:
      case OffloadArch::GFX702:
      case OffloadArch::GFX703:
      case OffloadArch::GFX704:
      case OffloadArch::GFX705:
      case OffloadArch::GFX801:
      case OffloadArch::GFX802:
      case OffloadArch::GFX803:
      case OffloadArch::GFX805:
      case OffloadArch::GFX810:
      case OffloadArch::GFX9_GENERIC:
      case OffloadArch::GFX900:
      case OffloadArch::GFX902:
      case OffloadArch::GFX904:
      case OffloadArch::GFX906:
      case OffloadArch::GFX908:
      case OffloadArch::GFX909:
      case OffloadArch::GFX90a:
      case OffloadArch::GFX90c:
      case OffloadArch::GFX940:
      case OffloadArch::GFX941:
      case OffloadArch::GFX942:
      case OffloadArch::GFX10_1_GENERIC:
      case OffloadArch::GFX1010:
      case OffloadArch::GFX1011:
      case OffloadArch::GFX1012:
      case OffloadArch::GFX1013:
      case OffloadArch::GFX10_3_GENERIC:
      case OffloadArch::GFX1030:
      case OffloadArch::GFX1031:
      case OffloadArch::GFX1032:
      case OffloadArch::GFX1033:
      case OffloadArch::GFX1034:
      case OffloadArch::GFX1035:
      case OffloadArch::GFX1036:
      case OffloadArch::GFX11_GENERIC:
      case OffloadArch::GFX1100:
      case OffloadArch::GFX1101:
      case OffloadArch::GFX1102:
      case OffloadArch::GFX1103:
      case OffloadArch::GFX1150:
      case OffloadArch::GFX1151:
      case OffloadArch::GFX1152:
      case OffloadArch::GFX12_GENERIC:
      case OffloadArch::GFX1200:
      case OffloadArch::GFX1201:
      case OffloadArch::AMDGCNSPIRV:
      case OffloadArch::Generic:
      case OffloadArch::UNUSED:
      case OffloadArch::UNKNOWN:
        break;
      case OffloadArch::LAST:
        llvm_unreachable("Unexpected GPU arch.");
      }
    }
  }
  CGOpenMPRuntime::processRequiresDirective(D);
}

llvm::Value *CGOpenMPRuntimeGPU::getGPUNumThreads(CodeGenFunction &CGF) {
  CGBuilderTy &Bld = CGF.Builder;
  llvm::Module *M = &CGF.CGM.getModule();
  const char *LocSize = "__kmpc_get_hardware_num_threads_in_block";
  llvm::Function *F = M->getFunction(LocSize);
  if (!F) {
    F = llvm::Function::Create(
        llvm::FunctionType::get(CGF.Int32Ty, std::nullopt, false),
        llvm::GlobalVariable::ExternalLinkage, LocSize, &CGF.CGM.getModule());
  }
  return Bld.CreateCall(F, std::nullopt, "nvptx_num_threads");
}

llvm::Value *CGOpenMPRuntimeGPU::getGPUThreadID(CodeGenFunction &CGF) {
  ArrayRef<llvm::Value *> Args{};
  return CGF.EmitRuntimeCall(
      OMPBuilder.getOrCreateRuntimeFunction(
          CGM.getModule(), OMPRTL___kmpc_get_hardware_thread_id_in_block),
      Args);
}
