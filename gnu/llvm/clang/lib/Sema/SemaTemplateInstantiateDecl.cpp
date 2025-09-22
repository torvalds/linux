//===--- SemaTemplateInstantiateDecl.cpp - C++ Template Decl Instantiation ===/
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===/
//
//  This file implements C++ template instantiation for declarations.
//
//===----------------------------------------------------------------------===/

#include "TreeTransform.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTMutationListener.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/DeclVisitor.h"
#include "clang/AST/DependentDiagnostic.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/PrettyDeclStackTrace.h"
#include "clang/AST/TypeLoc.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Sema/EnterExpressionEvaluationContext.h"
#include "clang/Sema/Initialization.h"
#include "clang/Sema/Lookup.h"
#include "clang/Sema/ScopeInfo.h"
#include "clang/Sema/SemaAMDGPU.h"
#include "clang/Sema/SemaCUDA.h"
#include "clang/Sema/SemaInternal.h"
#include "clang/Sema/SemaObjC.h"
#include "clang/Sema/SemaOpenMP.h"
#include "clang/Sema/SemaSwift.h"
#include "clang/Sema/Template.h"
#include "clang/Sema/TemplateInstCallback.h"
#include "llvm/Support/TimeProfiler.h"
#include <optional>

using namespace clang;

static bool isDeclWithinFunction(const Decl *D) {
  const DeclContext *DC = D->getDeclContext();
  if (DC->isFunctionOrMethod())
    return true;

  if (DC->isRecord())
    return cast<CXXRecordDecl>(DC)->isLocalClass();

  return false;
}

template<typename DeclT>
static bool SubstQualifier(Sema &SemaRef, const DeclT *OldDecl, DeclT *NewDecl,
                           const MultiLevelTemplateArgumentList &TemplateArgs) {
  if (!OldDecl->getQualifierLoc())
    return false;

  assert((NewDecl->getFriendObjectKind() ||
          !OldDecl->getLexicalDeclContext()->isDependentContext()) &&
         "non-friend with qualified name defined in dependent context");
  Sema::ContextRAII SavedContext(
      SemaRef,
      const_cast<DeclContext *>(NewDecl->getFriendObjectKind()
                                    ? NewDecl->getLexicalDeclContext()
                                    : OldDecl->getLexicalDeclContext()));

  NestedNameSpecifierLoc NewQualifierLoc
      = SemaRef.SubstNestedNameSpecifierLoc(OldDecl->getQualifierLoc(),
                                            TemplateArgs);

  if (!NewQualifierLoc)
    return true;

  NewDecl->setQualifierInfo(NewQualifierLoc);
  return false;
}

bool TemplateDeclInstantiator::SubstQualifier(const DeclaratorDecl *OldDecl,
                                              DeclaratorDecl *NewDecl) {
  return ::SubstQualifier(SemaRef, OldDecl, NewDecl, TemplateArgs);
}

bool TemplateDeclInstantiator::SubstQualifier(const TagDecl *OldDecl,
                                              TagDecl *NewDecl) {
  return ::SubstQualifier(SemaRef, OldDecl, NewDecl, TemplateArgs);
}

// Include attribute instantiation code.
#include "clang/Sema/AttrTemplateInstantiate.inc"

static void instantiateDependentAlignedAttr(
    Sema &S, const MultiLevelTemplateArgumentList &TemplateArgs,
    const AlignedAttr *Aligned, Decl *New, bool IsPackExpansion) {
  if (Aligned->isAlignmentExpr()) {
    // The alignment expression is a constant expression.
    EnterExpressionEvaluationContext Unevaluated(
        S, Sema::ExpressionEvaluationContext::ConstantEvaluated);
    ExprResult Result = S.SubstExpr(Aligned->getAlignmentExpr(), TemplateArgs);
    if (!Result.isInvalid())
      S.AddAlignedAttr(New, *Aligned, Result.getAs<Expr>(), IsPackExpansion);
  } else {
    if (TypeSourceInfo *Result =
            S.SubstType(Aligned->getAlignmentType(), TemplateArgs,
                        Aligned->getLocation(), DeclarationName())) {
      if (!S.CheckAlignasTypeArgument(Aligned->getSpelling(), Result,
                                      Aligned->getLocation(),
                                      Result->getTypeLoc().getSourceRange()))
        S.AddAlignedAttr(New, *Aligned, Result, IsPackExpansion);
    }
  }
}

static void instantiateDependentAlignedAttr(
    Sema &S, const MultiLevelTemplateArgumentList &TemplateArgs,
    const AlignedAttr *Aligned, Decl *New) {
  if (!Aligned->isPackExpansion()) {
    instantiateDependentAlignedAttr(S, TemplateArgs, Aligned, New, false);
    return;
  }

  SmallVector<UnexpandedParameterPack, 2> Unexpanded;
  if (Aligned->isAlignmentExpr())
    S.collectUnexpandedParameterPacks(Aligned->getAlignmentExpr(),
                                      Unexpanded);
  else
    S.collectUnexpandedParameterPacks(Aligned->getAlignmentType()->getTypeLoc(),
                                      Unexpanded);
  assert(!Unexpanded.empty() && "Pack expansion without parameter packs?");

  // Determine whether we can expand this attribute pack yet.
  bool Expand = true, RetainExpansion = false;
  std::optional<unsigned> NumExpansions;
  // FIXME: Use the actual location of the ellipsis.
  SourceLocation EllipsisLoc = Aligned->getLocation();
  if (S.CheckParameterPacksForExpansion(EllipsisLoc, Aligned->getRange(),
                                        Unexpanded, TemplateArgs, Expand,
                                        RetainExpansion, NumExpansions))
    return;

  if (!Expand) {
    Sema::ArgumentPackSubstitutionIndexRAII SubstIndex(S, -1);
    instantiateDependentAlignedAttr(S, TemplateArgs, Aligned, New, true);
  } else {
    for (unsigned I = 0; I != *NumExpansions; ++I) {
      Sema::ArgumentPackSubstitutionIndexRAII SubstIndex(S, I);
      instantiateDependentAlignedAttr(S, TemplateArgs, Aligned, New, false);
    }
  }
}

static void instantiateDependentAssumeAlignedAttr(
    Sema &S, const MultiLevelTemplateArgumentList &TemplateArgs,
    const AssumeAlignedAttr *Aligned, Decl *New) {
  // The alignment expression is a constant expression.
  EnterExpressionEvaluationContext Unevaluated(
      S, Sema::ExpressionEvaluationContext::ConstantEvaluated);

  Expr *E, *OE = nullptr;
  ExprResult Result = S.SubstExpr(Aligned->getAlignment(), TemplateArgs);
  if (Result.isInvalid())
    return;
  E = Result.getAs<Expr>();

  if (Aligned->getOffset()) {
    Result = S.SubstExpr(Aligned->getOffset(), TemplateArgs);
    if (Result.isInvalid())
      return;
    OE = Result.getAs<Expr>();
  }

  S.AddAssumeAlignedAttr(New, *Aligned, E, OE);
}

static void instantiateDependentAlignValueAttr(
    Sema &S, const MultiLevelTemplateArgumentList &TemplateArgs,
    const AlignValueAttr *Aligned, Decl *New) {
  // The alignment expression is a constant expression.
  EnterExpressionEvaluationContext Unevaluated(
      S, Sema::ExpressionEvaluationContext::ConstantEvaluated);
  ExprResult Result = S.SubstExpr(Aligned->getAlignment(), TemplateArgs);
  if (!Result.isInvalid())
    S.AddAlignValueAttr(New, *Aligned, Result.getAs<Expr>());
}

static void instantiateDependentAllocAlignAttr(
    Sema &S, const MultiLevelTemplateArgumentList &TemplateArgs,
    const AllocAlignAttr *Align, Decl *New) {
  Expr *Param = IntegerLiteral::Create(
      S.getASTContext(),
      llvm::APInt(64, Align->getParamIndex().getSourceIndex()),
      S.getASTContext().UnsignedLongLongTy, Align->getLocation());
  S.AddAllocAlignAttr(New, *Align, Param);
}

static void instantiateDependentAnnotationAttr(
    Sema &S, const MultiLevelTemplateArgumentList &TemplateArgs,
    const AnnotateAttr *Attr, Decl *New) {
  EnterExpressionEvaluationContext Unevaluated(
      S, Sema::ExpressionEvaluationContext::ConstantEvaluated);

  // If the attribute has delayed arguments it will have to instantiate those
  // and handle them as new arguments for the attribute.
  bool HasDelayedArgs = Attr->delayedArgs_size();

  ArrayRef<Expr *> ArgsToInstantiate =
      HasDelayedArgs
          ? ArrayRef<Expr *>{Attr->delayedArgs_begin(), Attr->delayedArgs_end()}
          : ArrayRef<Expr *>{Attr->args_begin(), Attr->args_end()};

  SmallVector<Expr *, 4> Args;
  if (S.SubstExprs(ArgsToInstantiate,
                   /*IsCall=*/false, TemplateArgs, Args))
    return;

  StringRef Str = Attr->getAnnotation();
  if (HasDelayedArgs) {
    if (Args.size() < 1) {
      S.Diag(Attr->getLoc(), diag::err_attribute_too_few_arguments)
          << Attr << 1;
      return;
    }

    if (!S.checkStringLiteralArgumentAttr(*Attr, Args[0], Str))
      return;

    llvm::SmallVector<Expr *, 4> ActualArgs;
    ActualArgs.insert(ActualArgs.begin(), Args.begin() + 1, Args.end());
    std::swap(Args, ActualArgs);
  }
  S.AddAnnotationAttr(New, *Attr, Str, Args);
}

static Expr *instantiateDependentFunctionAttrCondition(
    Sema &S, const MultiLevelTemplateArgumentList &TemplateArgs,
    const Attr *A, Expr *OldCond, const Decl *Tmpl, FunctionDecl *New) {
  Expr *Cond = nullptr;
  {
    Sema::ContextRAII SwitchContext(S, New);
    EnterExpressionEvaluationContext Unevaluated(
        S, Sema::ExpressionEvaluationContext::ConstantEvaluated);
    ExprResult Result = S.SubstExpr(OldCond, TemplateArgs);
    if (Result.isInvalid())
      return nullptr;
    Cond = Result.getAs<Expr>();
  }
  if (!Cond->isTypeDependent()) {
    ExprResult Converted = S.PerformContextuallyConvertToBool(Cond);
    if (Converted.isInvalid())
      return nullptr;
    Cond = Converted.get();
  }

  SmallVector<PartialDiagnosticAt, 8> Diags;
  if (OldCond->isValueDependent() && !Cond->isValueDependent() &&
      !Expr::isPotentialConstantExprUnevaluated(Cond, New, Diags)) {
    S.Diag(A->getLocation(), diag::err_attr_cond_never_constant_expr) << A;
    for (const auto &P : Diags)
      S.Diag(P.first, P.second);
    return nullptr;
  }
  return Cond;
}

static void instantiateDependentEnableIfAttr(
    Sema &S, const MultiLevelTemplateArgumentList &TemplateArgs,
    const EnableIfAttr *EIA, const Decl *Tmpl, FunctionDecl *New) {
  Expr *Cond = instantiateDependentFunctionAttrCondition(
      S, TemplateArgs, EIA, EIA->getCond(), Tmpl, New);

  if (Cond)
    New->addAttr(new (S.getASTContext()) EnableIfAttr(S.getASTContext(), *EIA,
                                                      Cond, EIA->getMessage()));
}

static void instantiateDependentDiagnoseIfAttr(
    Sema &S, const MultiLevelTemplateArgumentList &TemplateArgs,
    const DiagnoseIfAttr *DIA, const Decl *Tmpl, FunctionDecl *New) {
  Expr *Cond = instantiateDependentFunctionAttrCondition(
      S, TemplateArgs, DIA, DIA->getCond(), Tmpl, New);

  if (Cond)
    New->addAttr(new (S.getASTContext()) DiagnoseIfAttr(
        S.getASTContext(), *DIA, Cond, DIA->getMessage(),
        DIA->getDiagnosticType(), DIA->getArgDependent(), New));
}

// Constructs and adds to New a new instance of CUDALaunchBoundsAttr using
// template A as the base and arguments from TemplateArgs.
static void instantiateDependentCUDALaunchBoundsAttr(
    Sema &S, const MultiLevelTemplateArgumentList &TemplateArgs,
    const CUDALaunchBoundsAttr &Attr, Decl *New) {
  // The alignment expression is a constant expression.
  EnterExpressionEvaluationContext Unevaluated(
      S, Sema::ExpressionEvaluationContext::ConstantEvaluated);

  ExprResult Result = S.SubstExpr(Attr.getMaxThreads(), TemplateArgs);
  if (Result.isInvalid())
    return;
  Expr *MaxThreads = Result.getAs<Expr>();

  Expr *MinBlocks = nullptr;
  if (Attr.getMinBlocks()) {
    Result = S.SubstExpr(Attr.getMinBlocks(), TemplateArgs);
    if (Result.isInvalid())
      return;
    MinBlocks = Result.getAs<Expr>();
  }

  Expr *MaxBlocks = nullptr;
  if (Attr.getMaxBlocks()) {
    Result = S.SubstExpr(Attr.getMaxBlocks(), TemplateArgs);
    if (Result.isInvalid())
      return;
    MaxBlocks = Result.getAs<Expr>();
  }

  S.AddLaunchBoundsAttr(New, Attr, MaxThreads, MinBlocks, MaxBlocks);
}

static void
instantiateDependentModeAttr(Sema &S,
                             const MultiLevelTemplateArgumentList &TemplateArgs,
                             const ModeAttr &Attr, Decl *New) {
  S.AddModeAttr(New, Attr, Attr.getMode(),
                /*InInstantiation=*/true);
}

/// Instantiation of 'declare simd' attribute and its arguments.
static void instantiateOMPDeclareSimdDeclAttr(
    Sema &S, const MultiLevelTemplateArgumentList &TemplateArgs,
    const OMPDeclareSimdDeclAttr &Attr, Decl *New) {
  // Allow 'this' in clauses with varlists.
  if (auto *FTD = dyn_cast<FunctionTemplateDecl>(New))
    New = FTD->getTemplatedDecl();
  auto *FD = cast<FunctionDecl>(New);
  auto *ThisContext = dyn_cast_or_null<CXXRecordDecl>(FD->getDeclContext());
  SmallVector<Expr *, 4> Uniforms, Aligneds, Alignments, Linears, Steps;
  SmallVector<unsigned, 4> LinModifiers;

  auto SubstExpr = [&](Expr *E) -> ExprResult {
    if (auto *DRE = dyn_cast<DeclRefExpr>(E->IgnoreParenImpCasts()))
      if (auto *PVD = dyn_cast<ParmVarDecl>(DRE->getDecl())) {
        Sema::ContextRAII SavedContext(S, FD);
        LocalInstantiationScope Local(S);
        if (FD->getNumParams() > PVD->getFunctionScopeIndex())
          Local.InstantiatedLocal(
              PVD, FD->getParamDecl(PVD->getFunctionScopeIndex()));
        return S.SubstExpr(E, TemplateArgs);
      }
    Sema::CXXThisScopeRAII ThisScope(S, ThisContext, Qualifiers(),
                                     FD->isCXXInstanceMember());
    return S.SubstExpr(E, TemplateArgs);
  };

  // Substitute a single OpenMP clause, which is a potentially-evaluated
  // full-expression.
  auto Subst = [&](Expr *E) -> ExprResult {
    EnterExpressionEvaluationContext Evaluated(
        S, Sema::ExpressionEvaluationContext::PotentiallyEvaluated);
    ExprResult Res = SubstExpr(E);
    if (Res.isInvalid())
      return Res;
    return S.ActOnFinishFullExpr(Res.get(), false);
  };

  ExprResult Simdlen;
  if (auto *E = Attr.getSimdlen())
    Simdlen = Subst(E);

  if (Attr.uniforms_size() > 0) {
    for(auto *E : Attr.uniforms()) {
      ExprResult Inst = Subst(E);
      if (Inst.isInvalid())
        continue;
      Uniforms.push_back(Inst.get());
    }
  }

  auto AI = Attr.alignments_begin();
  for (auto *E : Attr.aligneds()) {
    ExprResult Inst = Subst(E);
    if (Inst.isInvalid())
      continue;
    Aligneds.push_back(Inst.get());
    Inst = ExprEmpty();
    if (*AI)
      Inst = S.SubstExpr(*AI, TemplateArgs);
    Alignments.push_back(Inst.get());
    ++AI;
  }

  auto SI = Attr.steps_begin();
  for (auto *E : Attr.linears()) {
    ExprResult Inst = Subst(E);
    if (Inst.isInvalid())
      continue;
    Linears.push_back(Inst.get());
    Inst = ExprEmpty();
    if (*SI)
      Inst = S.SubstExpr(*SI, TemplateArgs);
    Steps.push_back(Inst.get());
    ++SI;
  }
  LinModifiers.append(Attr.modifiers_begin(), Attr.modifiers_end());
  (void)S.OpenMP().ActOnOpenMPDeclareSimdDirective(
      S.ConvertDeclToDeclGroup(New), Attr.getBranchState(), Simdlen.get(),
      Uniforms, Aligneds, Alignments, Linears, LinModifiers, Steps,
      Attr.getRange());
}

/// Instantiation of 'declare variant' attribute and its arguments.
static void instantiateOMPDeclareVariantAttr(
    Sema &S, const MultiLevelTemplateArgumentList &TemplateArgs,
    const OMPDeclareVariantAttr &Attr, Decl *New) {
  // Allow 'this' in clauses with varlists.
  if (auto *FTD = dyn_cast<FunctionTemplateDecl>(New))
    New = FTD->getTemplatedDecl();
  auto *FD = cast<FunctionDecl>(New);
  auto *ThisContext = dyn_cast_or_null<CXXRecordDecl>(FD->getDeclContext());

  auto &&SubstExpr = [FD, ThisContext, &S, &TemplateArgs](Expr *E) {
    if (auto *DRE = dyn_cast<DeclRefExpr>(E->IgnoreParenImpCasts()))
      if (auto *PVD = dyn_cast<ParmVarDecl>(DRE->getDecl())) {
        Sema::ContextRAII SavedContext(S, FD);
        LocalInstantiationScope Local(S);
        if (FD->getNumParams() > PVD->getFunctionScopeIndex())
          Local.InstantiatedLocal(
              PVD, FD->getParamDecl(PVD->getFunctionScopeIndex()));
        return S.SubstExpr(E, TemplateArgs);
      }
    Sema::CXXThisScopeRAII ThisScope(S, ThisContext, Qualifiers(),
                                     FD->isCXXInstanceMember());
    return S.SubstExpr(E, TemplateArgs);
  };

  // Substitute a single OpenMP clause, which is a potentially-evaluated
  // full-expression.
  auto &&Subst = [&SubstExpr, &S](Expr *E) {
    EnterExpressionEvaluationContext Evaluated(
        S, Sema::ExpressionEvaluationContext::PotentiallyEvaluated);
    ExprResult Res = SubstExpr(E);
    if (Res.isInvalid())
      return Res;
    return S.ActOnFinishFullExpr(Res.get(), false);
  };

  ExprResult VariantFuncRef;
  if (Expr *E = Attr.getVariantFuncRef()) {
    // Do not mark function as is used to prevent its emission if this is the
    // only place where it is used.
    EnterExpressionEvaluationContext Unevaluated(
        S, Sema::ExpressionEvaluationContext::ConstantEvaluated);
    VariantFuncRef = Subst(E);
  }

  // Copy the template version of the OMPTraitInfo and run substitute on all
  // score and condition expressiosn.
  OMPTraitInfo &TI = S.getASTContext().getNewOMPTraitInfo();
  TI = *Attr.getTraitInfos();

  // Try to substitute template parameters in score and condition expressions.
  auto SubstScoreOrConditionExpr = [&S, Subst](Expr *&E, bool) {
    if (E) {
      EnterExpressionEvaluationContext Unevaluated(
          S, Sema::ExpressionEvaluationContext::ConstantEvaluated);
      ExprResult ER = Subst(E);
      if (ER.isUsable())
        E = ER.get();
      else
        return true;
    }
    return false;
  };
  if (TI.anyScoreOrCondition(SubstScoreOrConditionExpr))
    return;

  Expr *E = VariantFuncRef.get();

  // Check function/variant ref for `omp declare variant` but not for `omp
  // begin declare variant` (which use implicit attributes).
  std::optional<std::pair<FunctionDecl *, Expr *>> DeclVarData =
      S.OpenMP().checkOpenMPDeclareVariantFunction(
          S.ConvertDeclToDeclGroup(New), E, TI, Attr.appendArgs_size(),
          Attr.getRange());

  if (!DeclVarData)
    return;

  E = DeclVarData->second;
  FD = DeclVarData->first;

  if (auto *VariantDRE = dyn_cast<DeclRefExpr>(E->IgnoreParenImpCasts())) {
    if (auto *VariantFD = dyn_cast<FunctionDecl>(VariantDRE->getDecl())) {
      if (auto *VariantFTD = VariantFD->getDescribedFunctionTemplate()) {
        if (!VariantFTD->isThisDeclarationADefinition())
          return;
        Sema::TentativeAnalysisScope Trap(S);
        const TemplateArgumentList *TAL = TemplateArgumentList::CreateCopy(
            S.Context, TemplateArgs.getInnermost());

        auto *SubstFD = S.InstantiateFunctionDeclaration(VariantFTD, TAL,
                                                         New->getLocation());
        if (!SubstFD)
          return;
        QualType NewType = S.Context.mergeFunctionTypes(
            SubstFD->getType(), FD->getType(),
            /* OfBlockPointer */ false,
            /* Unqualified */ false, /* AllowCXX */ true);
        if (NewType.isNull())
          return;
        S.InstantiateFunctionDefinition(
            New->getLocation(), SubstFD, /* Recursive */ true,
            /* DefinitionRequired */ false, /* AtEndOfTU */ false);
        SubstFD->setInstantiationIsPending(!SubstFD->isDefined());
        E = DeclRefExpr::Create(S.Context, NestedNameSpecifierLoc(),
                                SourceLocation(), SubstFD,
                                /* RefersToEnclosingVariableOrCapture */ false,
                                /* NameLoc */ SubstFD->getLocation(),
                                SubstFD->getType(), ExprValueKind::VK_PRValue);
      }
    }
  }

  SmallVector<Expr *, 8> NothingExprs;
  SmallVector<Expr *, 8> NeedDevicePtrExprs;
  SmallVector<OMPInteropInfo, 4> AppendArgs;

  for (Expr *E : Attr.adjustArgsNothing()) {
    ExprResult ER = Subst(E);
    if (ER.isInvalid())
      continue;
    NothingExprs.push_back(ER.get());
  }
  for (Expr *E : Attr.adjustArgsNeedDevicePtr()) {
    ExprResult ER = Subst(E);
    if (ER.isInvalid())
      continue;
    NeedDevicePtrExprs.push_back(ER.get());
  }
  for (OMPInteropInfo &II : Attr.appendArgs()) {
    // When prefer_type is implemented for append_args handle them here too.
    AppendArgs.emplace_back(II.IsTarget, II.IsTargetSync);
  }

  S.OpenMP().ActOnOpenMPDeclareVariantDirective(
      FD, E, TI, NothingExprs, NeedDevicePtrExprs, AppendArgs, SourceLocation(),
      SourceLocation(), Attr.getRange());
}

static void instantiateDependentAMDGPUFlatWorkGroupSizeAttr(
    Sema &S, const MultiLevelTemplateArgumentList &TemplateArgs,
    const AMDGPUFlatWorkGroupSizeAttr &Attr, Decl *New) {
  // Both min and max expression are constant expressions.
  EnterExpressionEvaluationContext Unevaluated(
      S, Sema::ExpressionEvaluationContext::ConstantEvaluated);

  ExprResult Result = S.SubstExpr(Attr.getMin(), TemplateArgs);
  if (Result.isInvalid())
    return;
  Expr *MinExpr = Result.getAs<Expr>();

  Result = S.SubstExpr(Attr.getMax(), TemplateArgs);
  if (Result.isInvalid())
    return;
  Expr *MaxExpr = Result.getAs<Expr>();

  S.AMDGPU().addAMDGPUFlatWorkGroupSizeAttr(New, Attr, MinExpr, MaxExpr);
}

ExplicitSpecifier Sema::instantiateExplicitSpecifier(
    const MultiLevelTemplateArgumentList &TemplateArgs, ExplicitSpecifier ES) {
  if (!ES.getExpr())
    return ES;
  Expr *OldCond = ES.getExpr();
  Expr *Cond = nullptr;
  {
    EnterExpressionEvaluationContext Unevaluated(
        *this, Sema::ExpressionEvaluationContext::ConstantEvaluated);
    ExprResult SubstResult = SubstExpr(OldCond, TemplateArgs);
    if (SubstResult.isInvalid()) {
      return ExplicitSpecifier::Invalid();
    }
    Cond = SubstResult.get();
  }
  ExplicitSpecifier Result(Cond, ES.getKind());
  if (!Cond->isTypeDependent())
    tryResolveExplicitSpecifier(Result);
  return Result;
}

static void instantiateDependentAMDGPUWavesPerEUAttr(
    Sema &S, const MultiLevelTemplateArgumentList &TemplateArgs,
    const AMDGPUWavesPerEUAttr &Attr, Decl *New) {
  // Both min and max expression are constant expressions.
  EnterExpressionEvaluationContext Unevaluated(
      S, Sema::ExpressionEvaluationContext::ConstantEvaluated);

  ExprResult Result = S.SubstExpr(Attr.getMin(), TemplateArgs);
  if (Result.isInvalid())
    return;
  Expr *MinExpr = Result.getAs<Expr>();

  Expr *MaxExpr = nullptr;
  if (auto Max = Attr.getMax()) {
    Result = S.SubstExpr(Max, TemplateArgs);
    if (Result.isInvalid())
      return;
    MaxExpr = Result.getAs<Expr>();
  }

  S.AMDGPU().addAMDGPUWavesPerEUAttr(New, Attr, MinExpr, MaxExpr);
}

static void instantiateDependentAMDGPUMaxNumWorkGroupsAttr(
    Sema &S, const MultiLevelTemplateArgumentList &TemplateArgs,
    const AMDGPUMaxNumWorkGroupsAttr &Attr, Decl *New) {
  EnterExpressionEvaluationContext Unevaluated(
      S, Sema::ExpressionEvaluationContext::ConstantEvaluated);

  ExprResult ResultX = S.SubstExpr(Attr.getMaxNumWorkGroupsX(), TemplateArgs);
  if (!ResultX.isUsable())
    return;
  ExprResult ResultY = S.SubstExpr(Attr.getMaxNumWorkGroupsY(), TemplateArgs);
  if (!ResultY.isUsable())
    return;
  ExprResult ResultZ = S.SubstExpr(Attr.getMaxNumWorkGroupsZ(), TemplateArgs);
  if (!ResultZ.isUsable())
    return;

  Expr *XExpr = ResultX.getAs<Expr>();
  Expr *YExpr = ResultY.getAs<Expr>();
  Expr *ZExpr = ResultZ.getAs<Expr>();

  S.AMDGPU().addAMDGPUMaxNumWorkGroupsAttr(New, Attr, XExpr, YExpr, ZExpr);
}

// This doesn't take any template parameters, but we have a custom action that
// needs to happen when the kernel itself is instantiated. We need to run the
// ItaniumMangler to mark the names required to name this kernel.
static void instantiateDependentSYCLKernelAttr(
    Sema &S, const MultiLevelTemplateArgumentList &TemplateArgs,
    const SYCLKernelAttr &Attr, Decl *New) {
  New->addAttr(Attr.clone(S.getASTContext()));
}

/// Determine whether the attribute A might be relevant to the declaration D.
/// If not, we can skip instantiating it. The attribute may or may not have
/// been instantiated yet.
static bool isRelevantAttr(Sema &S, const Decl *D, const Attr *A) {
  // 'preferred_name' is only relevant to the matching specialization of the
  // template.
  if (const auto *PNA = dyn_cast<PreferredNameAttr>(A)) {
    QualType T = PNA->getTypedefType();
    const auto *RD = cast<CXXRecordDecl>(D);
    if (!T->isDependentType() && !RD->isDependentContext() &&
        !declaresSameEntity(T->getAsCXXRecordDecl(), RD))
      return false;
    for (const auto *ExistingPNA : D->specific_attrs<PreferredNameAttr>())
      if (S.Context.hasSameType(ExistingPNA->getTypedefType(),
                                PNA->getTypedefType()))
        return false;
    return true;
  }

  if (const auto *BA = dyn_cast<BuiltinAttr>(A)) {
    const FunctionDecl *FD = dyn_cast<FunctionDecl>(D);
    switch (BA->getID()) {
    case Builtin::BIforward:
      // Do not treat 'std::forward' as a builtin if it takes an rvalue reference
      // type and returns an lvalue reference type. The library implementation
      // will produce an error in this case; don't get in its way.
      if (FD && FD->getNumParams() >= 1 &&
          FD->getParamDecl(0)->getType()->isRValueReferenceType() &&
          FD->getReturnType()->isLValueReferenceType()) {
        return false;
      }
      [[fallthrough]];
    case Builtin::BImove:
    case Builtin::BImove_if_noexcept:
      // HACK: Super-old versions of libc++ (3.1 and earlier) provide
      // std::forward and std::move overloads that sometimes return by value
      // instead of by reference when building in C++98 mode. Don't treat such
      // cases as builtins.
      if (FD && !FD->getReturnType()->isReferenceType())
        return false;
      break;
    }
  }

  return true;
}

static void instantiateDependentHLSLParamModifierAttr(
    Sema &S, const MultiLevelTemplateArgumentList &TemplateArgs,
    const HLSLParamModifierAttr *Attr, Decl *New) {
  ParmVarDecl *P = cast<ParmVarDecl>(New);
  P->addAttr(Attr->clone(S.getASTContext()));
  P->setType(S.getASTContext().getLValueReferenceType(P->getType()));
}

void Sema::InstantiateAttrsForDecl(
    const MultiLevelTemplateArgumentList &TemplateArgs, const Decl *Tmpl,
    Decl *New, LateInstantiatedAttrVec *LateAttrs,
    LocalInstantiationScope *OuterMostScope) {
  if (NamedDecl *ND = dyn_cast<NamedDecl>(New)) {
    // FIXME: This function is called multiple times for the same template
    // specialization. We should only instantiate attributes that were added
    // since the previous instantiation.
    for (const auto *TmplAttr : Tmpl->attrs()) {
      if (!isRelevantAttr(*this, New, TmplAttr))
        continue;

      // FIXME: If any of the special case versions from InstantiateAttrs become
      // applicable to template declaration, we'll need to add them here.
      CXXThisScopeRAII ThisScope(
          *this, dyn_cast_or_null<CXXRecordDecl>(ND->getDeclContext()),
          Qualifiers(), ND->isCXXInstanceMember());

      Attr *NewAttr = sema::instantiateTemplateAttributeForDecl(
          TmplAttr, Context, *this, TemplateArgs);
      if (NewAttr && isRelevantAttr(*this, New, NewAttr))
        New->addAttr(NewAttr);
    }
  }
}

static Sema::RetainOwnershipKind
attrToRetainOwnershipKind(const Attr *A) {
  switch (A->getKind()) {
  case clang::attr::CFConsumed:
    return Sema::RetainOwnershipKind::CF;
  case clang::attr::OSConsumed:
    return Sema::RetainOwnershipKind::OS;
  case clang::attr::NSConsumed:
    return Sema::RetainOwnershipKind::NS;
  default:
    llvm_unreachable("Wrong argument supplied");
  }
}

void Sema::InstantiateAttrs(const MultiLevelTemplateArgumentList &TemplateArgs,
                            const Decl *Tmpl, Decl *New,
                            LateInstantiatedAttrVec *LateAttrs,
                            LocalInstantiationScope *OuterMostScope) {
  for (const auto *TmplAttr : Tmpl->attrs()) {
    if (!isRelevantAttr(*this, New, TmplAttr))
      continue;

    // FIXME: This should be generalized to more than just the AlignedAttr.
    const AlignedAttr *Aligned = dyn_cast<AlignedAttr>(TmplAttr);
    if (Aligned && Aligned->isAlignmentDependent()) {
      instantiateDependentAlignedAttr(*this, TemplateArgs, Aligned, New);
      continue;
    }

    if (const auto *AssumeAligned = dyn_cast<AssumeAlignedAttr>(TmplAttr)) {
      instantiateDependentAssumeAlignedAttr(*this, TemplateArgs, AssumeAligned, New);
      continue;
    }

    if (const auto *AlignValue = dyn_cast<AlignValueAttr>(TmplAttr)) {
      instantiateDependentAlignValueAttr(*this, TemplateArgs, AlignValue, New);
      continue;
    }

    if (const auto *AllocAlign = dyn_cast<AllocAlignAttr>(TmplAttr)) {
      instantiateDependentAllocAlignAttr(*this, TemplateArgs, AllocAlign, New);
      continue;
    }

    if (const auto *Annotate = dyn_cast<AnnotateAttr>(TmplAttr)) {
      instantiateDependentAnnotationAttr(*this, TemplateArgs, Annotate, New);
      continue;
    }

    if (const auto *EnableIf = dyn_cast<EnableIfAttr>(TmplAttr)) {
      instantiateDependentEnableIfAttr(*this, TemplateArgs, EnableIf, Tmpl,
                                       cast<FunctionDecl>(New));
      continue;
    }

    if (const auto *DiagnoseIf = dyn_cast<DiagnoseIfAttr>(TmplAttr)) {
      instantiateDependentDiagnoseIfAttr(*this, TemplateArgs, DiagnoseIf, Tmpl,
                                         cast<FunctionDecl>(New));
      continue;
    }

    if (const auto *CUDALaunchBounds =
            dyn_cast<CUDALaunchBoundsAttr>(TmplAttr)) {
      instantiateDependentCUDALaunchBoundsAttr(*this, TemplateArgs,
                                               *CUDALaunchBounds, New);
      continue;
    }

    if (const auto *Mode = dyn_cast<ModeAttr>(TmplAttr)) {
      instantiateDependentModeAttr(*this, TemplateArgs, *Mode, New);
      continue;
    }

    if (const auto *OMPAttr = dyn_cast<OMPDeclareSimdDeclAttr>(TmplAttr)) {
      instantiateOMPDeclareSimdDeclAttr(*this, TemplateArgs, *OMPAttr, New);
      continue;
    }

    if (const auto *OMPAttr = dyn_cast<OMPDeclareVariantAttr>(TmplAttr)) {
      instantiateOMPDeclareVariantAttr(*this, TemplateArgs, *OMPAttr, New);
      continue;
    }

    if (const auto *AMDGPUFlatWorkGroupSize =
            dyn_cast<AMDGPUFlatWorkGroupSizeAttr>(TmplAttr)) {
      instantiateDependentAMDGPUFlatWorkGroupSizeAttr(
          *this, TemplateArgs, *AMDGPUFlatWorkGroupSize, New);
    }

    if (const auto *AMDGPUFlatWorkGroupSize =
            dyn_cast<AMDGPUWavesPerEUAttr>(TmplAttr)) {
      instantiateDependentAMDGPUWavesPerEUAttr(*this, TemplateArgs,
                                               *AMDGPUFlatWorkGroupSize, New);
    }

    if (const auto *AMDGPUMaxNumWorkGroups =
            dyn_cast<AMDGPUMaxNumWorkGroupsAttr>(TmplAttr)) {
      instantiateDependentAMDGPUMaxNumWorkGroupsAttr(
          *this, TemplateArgs, *AMDGPUMaxNumWorkGroups, New);
    }

    if (const auto *ParamAttr = dyn_cast<HLSLParamModifierAttr>(TmplAttr)) {
      instantiateDependentHLSLParamModifierAttr(*this, TemplateArgs, ParamAttr,
                                                New);
      continue;
    }

    // Existing DLL attribute on the instantiation takes precedence.
    if (TmplAttr->getKind() == attr::DLLExport ||
        TmplAttr->getKind() == attr::DLLImport) {
      if (New->hasAttr<DLLExportAttr>() || New->hasAttr<DLLImportAttr>()) {
        continue;
      }
    }

    if (const auto *ABIAttr = dyn_cast<ParameterABIAttr>(TmplAttr)) {
      Swift().AddParameterABIAttr(New, *ABIAttr, ABIAttr->getABI());
      continue;
    }

    if (isa<NSConsumedAttr>(TmplAttr) || isa<OSConsumedAttr>(TmplAttr) ||
        isa<CFConsumedAttr>(TmplAttr)) {
      ObjC().AddXConsumedAttr(New, *TmplAttr,
                              attrToRetainOwnershipKind(TmplAttr),
                              /*template instantiation=*/true);
      continue;
    }

    if (auto *A = dyn_cast<PointerAttr>(TmplAttr)) {
      if (!New->hasAttr<PointerAttr>())
        New->addAttr(A->clone(Context));
      continue;
    }

    if (auto *A = dyn_cast<OwnerAttr>(TmplAttr)) {
      if (!New->hasAttr<OwnerAttr>())
        New->addAttr(A->clone(Context));
      continue;
    }

    if (auto *A = dyn_cast<SYCLKernelAttr>(TmplAttr)) {
      instantiateDependentSYCLKernelAttr(*this, TemplateArgs, *A, New);
      continue;
    }

    assert(!TmplAttr->isPackExpansion());
    if (TmplAttr->isLateParsed() && LateAttrs) {
      // Late parsed attributes must be instantiated and attached after the
      // enclosing class has been instantiated.  See Sema::InstantiateClass.
      LocalInstantiationScope *Saved = nullptr;
      if (CurrentInstantiationScope)
        Saved = CurrentInstantiationScope->cloneScopes(OuterMostScope);
      LateAttrs->push_back(LateInstantiatedAttribute(TmplAttr, Saved, New));
    } else {
      // Allow 'this' within late-parsed attributes.
      auto *ND = cast<NamedDecl>(New);
      auto *ThisContext = dyn_cast_or_null<CXXRecordDecl>(ND->getDeclContext());
      CXXThisScopeRAII ThisScope(*this, ThisContext, Qualifiers(),
                                 ND->isCXXInstanceMember());

      Attr *NewAttr = sema::instantiateTemplateAttribute(TmplAttr, Context,
                                                         *this, TemplateArgs);
      if (NewAttr && isRelevantAttr(*this, New, TmplAttr))
        New->addAttr(NewAttr);
    }
  }
}

void Sema::updateAttrsForLateParsedTemplate(const Decl *Pattern, Decl *Inst) {
  for (const auto *Attr : Pattern->attrs()) {
    if (auto *A = dyn_cast<StrictFPAttr>(Attr)) {
      if (!Inst->hasAttr<StrictFPAttr>())
        Inst->addAttr(A->clone(getASTContext()));
      continue;
    }
  }
}

void Sema::InstantiateDefaultCtorDefaultArgs(CXXConstructorDecl *Ctor) {
  assert(Context.getTargetInfo().getCXXABI().isMicrosoft() &&
         Ctor->isDefaultConstructor());
  unsigned NumParams = Ctor->getNumParams();
  if (NumParams == 0)
    return;
  DLLExportAttr *Attr = Ctor->getAttr<DLLExportAttr>();
  if (!Attr)
    return;
  for (unsigned I = 0; I != NumParams; ++I) {
    (void)CheckCXXDefaultArgExpr(Attr->getLocation(), Ctor,
                                   Ctor->getParamDecl(I));
    CleanupVarDeclMarking();
  }
}

/// Get the previous declaration of a declaration for the purposes of template
/// instantiation. If this finds a previous declaration, then the previous
/// declaration of the instantiation of D should be an instantiation of the
/// result of this function.
template<typename DeclT>
static DeclT *getPreviousDeclForInstantiation(DeclT *D) {
  DeclT *Result = D->getPreviousDecl();

  // If the declaration is within a class, and the previous declaration was
  // merged from a different definition of that class, then we don't have a
  // previous declaration for the purpose of template instantiation.
  if (Result && isa<CXXRecordDecl>(D->getDeclContext()) &&
      D->getLexicalDeclContext() != Result->getLexicalDeclContext())
    return nullptr;

  return Result;
}

Decl *
TemplateDeclInstantiator::VisitTranslationUnitDecl(TranslationUnitDecl *D) {
  llvm_unreachable("Translation units cannot be instantiated");
}

Decl *TemplateDeclInstantiator::VisitHLSLBufferDecl(HLSLBufferDecl *Decl) {
  llvm_unreachable("HLSL buffer declarations cannot be instantiated");
}

Decl *
TemplateDeclInstantiator::VisitPragmaCommentDecl(PragmaCommentDecl *D) {
  llvm_unreachable("pragma comment cannot be instantiated");
}

Decl *TemplateDeclInstantiator::VisitPragmaDetectMismatchDecl(
    PragmaDetectMismatchDecl *D) {
  llvm_unreachable("pragma comment cannot be instantiated");
}

Decl *
TemplateDeclInstantiator::VisitExternCContextDecl(ExternCContextDecl *D) {
  llvm_unreachable("extern \"C\" context cannot be instantiated");
}

Decl *TemplateDeclInstantiator::VisitMSGuidDecl(MSGuidDecl *D) {
  llvm_unreachable("GUID declaration cannot be instantiated");
}

Decl *TemplateDeclInstantiator::VisitUnnamedGlobalConstantDecl(
    UnnamedGlobalConstantDecl *D) {
  llvm_unreachable("UnnamedGlobalConstantDecl cannot be instantiated");
}

Decl *TemplateDeclInstantiator::VisitTemplateParamObjectDecl(
    TemplateParamObjectDecl *D) {
  llvm_unreachable("template parameter objects cannot be instantiated");
}

Decl *
TemplateDeclInstantiator::VisitLabelDecl(LabelDecl *D) {
  LabelDecl *Inst = LabelDecl::Create(SemaRef.Context, Owner, D->getLocation(),
                                      D->getIdentifier());
  Owner->addDecl(Inst);
  return Inst;
}

Decl *
TemplateDeclInstantiator::VisitNamespaceDecl(NamespaceDecl *D) {
  llvm_unreachable("Namespaces cannot be instantiated");
}

Decl *
TemplateDeclInstantiator::VisitNamespaceAliasDecl(NamespaceAliasDecl *D) {
  NamespaceAliasDecl *Inst
    = NamespaceAliasDecl::Create(SemaRef.Context, Owner,
                                 D->getNamespaceLoc(),
                                 D->getAliasLoc(),
                                 D->getIdentifier(),
                                 D->getQualifierLoc(),
                                 D->getTargetNameLoc(),
                                 D->getNamespace());
  Owner->addDecl(Inst);
  return Inst;
}

Decl *TemplateDeclInstantiator::InstantiateTypedefNameDecl(TypedefNameDecl *D,
                                                           bool IsTypeAlias) {
  bool Invalid = false;
  TypeSourceInfo *DI = D->getTypeSourceInfo();
  if (DI->getType()->isInstantiationDependentType() ||
      DI->getType()->isVariablyModifiedType()) {
    DI = SemaRef.SubstType(DI, TemplateArgs,
                           D->getLocation(), D->getDeclName());
    if (!DI) {
      Invalid = true;
      DI = SemaRef.Context.getTrivialTypeSourceInfo(SemaRef.Context.IntTy);
    }
  } else {
    SemaRef.MarkDeclarationsReferencedInType(D->getLocation(), DI->getType());
  }

  // HACK: 2012-10-23 g++ has a bug where it gets the value kind of ?: wrong.
  // libstdc++ relies upon this bug in its implementation of common_type.  If we
  // happen to be processing that implementation, fake up the g++ ?:
  // semantics. See LWG issue 2141 for more information on the bug.  The bugs
  // are fixed in g++ and libstdc++ 4.9.0 (2014-04-22).
  const DecltypeType *DT = DI->getType()->getAs<DecltypeType>();
  CXXRecordDecl *RD = dyn_cast<CXXRecordDecl>(D->getDeclContext());
  if (DT && RD && isa<ConditionalOperator>(DT->getUnderlyingExpr()) &&
      DT->isReferenceType() &&
      RD->getEnclosingNamespaceContext() == SemaRef.getStdNamespace() &&
      RD->getIdentifier() && RD->getIdentifier()->isStr("common_type") &&
      D->getIdentifier() && D->getIdentifier()->isStr("type") &&
      SemaRef.getSourceManager().isInSystemHeader(D->getBeginLoc()))
    // Fold it to the (non-reference) type which g++ would have produced.
    DI = SemaRef.Context.getTrivialTypeSourceInfo(
      DI->getType().getNonReferenceType());

  // Create the new typedef
  TypedefNameDecl *Typedef;
  if (IsTypeAlias)
    Typedef = TypeAliasDecl::Create(SemaRef.Context, Owner, D->getBeginLoc(),
                                    D->getLocation(), D->getIdentifier(), DI);
  else
    Typedef = TypedefDecl::Create(SemaRef.Context, Owner, D->getBeginLoc(),
                                  D->getLocation(), D->getIdentifier(), DI);
  if (Invalid)
    Typedef->setInvalidDecl();

  // If the old typedef was the name for linkage purposes of an anonymous
  // tag decl, re-establish that relationship for the new typedef.
  if (const TagType *oldTagType = D->getUnderlyingType()->getAs<TagType>()) {
    TagDecl *oldTag = oldTagType->getDecl();
    if (oldTag->getTypedefNameForAnonDecl() == D && !Invalid) {
      TagDecl *newTag = DI->getType()->castAs<TagType>()->getDecl();
      assert(!newTag->hasNameForLinkage());
      newTag->setTypedefNameForAnonDecl(Typedef);
    }
  }

  if (TypedefNameDecl *Prev = getPreviousDeclForInstantiation(D)) {
    NamedDecl *InstPrev = SemaRef.FindInstantiatedDecl(D->getLocation(), Prev,
                                                       TemplateArgs);
    if (!InstPrev)
      return nullptr;

    TypedefNameDecl *InstPrevTypedef = cast<TypedefNameDecl>(InstPrev);

    // If the typedef types are not identical, reject them.
    SemaRef.isIncompatibleTypedef(InstPrevTypedef, Typedef);

    Typedef->setPreviousDecl(InstPrevTypedef);
  }

  SemaRef.InstantiateAttrs(TemplateArgs, D, Typedef);

  if (D->getUnderlyingType()->getAs<DependentNameType>())
    SemaRef.inferGslPointerAttribute(Typedef);

  Typedef->setAccess(D->getAccess());
  Typedef->setReferenced(D->isReferenced());

  return Typedef;
}

Decl *TemplateDeclInstantiator::VisitTypedefDecl(TypedefDecl *D) {
  Decl *Typedef = InstantiateTypedefNameDecl(D, /*IsTypeAlias=*/false);
  if (Typedef)
    Owner->addDecl(Typedef);
  return Typedef;
}

Decl *TemplateDeclInstantiator::VisitTypeAliasDecl(TypeAliasDecl *D) {
  Decl *Typedef = InstantiateTypedefNameDecl(D, /*IsTypeAlias=*/true);
  if (Typedef)
    Owner->addDecl(Typedef);
  return Typedef;
}

Decl *TemplateDeclInstantiator::InstantiateTypeAliasTemplateDecl(
    TypeAliasTemplateDecl *D) {
  // Create a local instantiation scope for this type alias template, which
  // will contain the instantiations of the template parameters.
  LocalInstantiationScope Scope(SemaRef);

  TemplateParameterList *TempParams = D->getTemplateParameters();
  TemplateParameterList *InstParams = SubstTemplateParams(TempParams);
  if (!InstParams)
    return nullptr;

  TypeAliasDecl *Pattern = D->getTemplatedDecl();
  Sema::InstantiatingTemplate InstTemplate(
      SemaRef, D->getBeginLoc(), D,
      D->getTemplateDepth() >= TemplateArgs.getNumLevels()
          ? ArrayRef<TemplateArgument>()
          : (TemplateArgs.begin() + TemplateArgs.getNumLevels() - 1 -
             D->getTemplateDepth())
                ->Args);
  if (InstTemplate.isInvalid())
    return nullptr;

  TypeAliasTemplateDecl *PrevAliasTemplate = nullptr;
  if (getPreviousDeclForInstantiation<TypedefNameDecl>(Pattern)) {
    DeclContext::lookup_result Found = Owner->lookup(Pattern->getDeclName());
    if (!Found.empty()) {
      PrevAliasTemplate = dyn_cast<TypeAliasTemplateDecl>(Found.front());
    }
  }

  TypeAliasDecl *AliasInst = cast_or_null<TypeAliasDecl>(
    InstantiateTypedefNameDecl(Pattern, /*IsTypeAlias=*/true));
  if (!AliasInst)
    return nullptr;

  TypeAliasTemplateDecl *Inst
    = TypeAliasTemplateDecl::Create(SemaRef.Context, Owner, D->getLocation(),
                                    D->getDeclName(), InstParams, AliasInst);
  AliasInst->setDescribedAliasTemplate(Inst);
  if (PrevAliasTemplate)
    Inst->setPreviousDecl(PrevAliasTemplate);

  Inst->setAccess(D->getAccess());

  if (!PrevAliasTemplate)
    Inst->setInstantiatedFromMemberTemplate(D);

  return Inst;
}

Decl *
TemplateDeclInstantiator::VisitTypeAliasTemplateDecl(TypeAliasTemplateDecl *D) {
  Decl *Inst = InstantiateTypeAliasTemplateDecl(D);
  if (Inst)
    Owner->addDecl(Inst);

  return Inst;
}

Decl *TemplateDeclInstantiator::VisitBindingDecl(BindingDecl *D) {
  auto *NewBD = BindingDecl::Create(SemaRef.Context, Owner, D->getLocation(),
                                    D->getIdentifier());
  NewBD->setReferenced(D->isReferenced());
  SemaRef.CurrentInstantiationScope->InstantiatedLocal(D, NewBD);
  return NewBD;
}

Decl *TemplateDeclInstantiator::VisitDecompositionDecl(DecompositionDecl *D) {
  // Transform the bindings first.
  SmallVector<BindingDecl*, 16> NewBindings;
  for (auto *OldBD : D->bindings())
    NewBindings.push_back(cast<BindingDecl>(VisitBindingDecl(OldBD)));
  ArrayRef<BindingDecl*> NewBindingArray = NewBindings;

  auto *NewDD = cast_or_null<DecompositionDecl>(
      VisitVarDecl(D, /*InstantiatingVarTemplate=*/false, &NewBindingArray));

  if (!NewDD || NewDD->isInvalidDecl())
    for (auto *NewBD : NewBindings)
      NewBD->setInvalidDecl();

  return NewDD;
}

Decl *TemplateDeclInstantiator::VisitVarDecl(VarDecl *D) {
  return VisitVarDecl(D, /*InstantiatingVarTemplate=*/false);
}

Decl *TemplateDeclInstantiator::VisitVarDecl(VarDecl *D,
                                             bool InstantiatingVarTemplate,
                                             ArrayRef<BindingDecl*> *Bindings) {

  // Do substitution on the type of the declaration
  TypeSourceInfo *DI = SemaRef.SubstType(
      D->getTypeSourceInfo(), TemplateArgs, D->getTypeSpecStartLoc(),
      D->getDeclName(), /*AllowDeducedTST*/true);
  if (!DI)
    return nullptr;

  if (DI->getType()->isFunctionType()) {
    SemaRef.Diag(D->getLocation(), diag::err_variable_instantiates_to_function)
      << D->isStaticDataMember() << DI->getType();
    return nullptr;
  }

  DeclContext *DC = Owner;
  if (D->isLocalExternDecl())
    SemaRef.adjustContextForLocalExternDecl(DC);

  // Build the instantiated declaration.
  VarDecl *Var;
  if (Bindings)
    Var = DecompositionDecl::Create(SemaRef.Context, DC, D->getInnerLocStart(),
                                    D->getLocation(), DI->getType(), DI,
                                    D->getStorageClass(), *Bindings);
  else
    Var = VarDecl::Create(SemaRef.Context, DC, D->getInnerLocStart(),
                          D->getLocation(), D->getIdentifier(), DI->getType(),
                          DI, D->getStorageClass());

  // In ARC, infer 'retaining' for variables of retainable type.
  if (SemaRef.getLangOpts().ObjCAutoRefCount &&
      SemaRef.ObjC().inferObjCARCLifetime(Var))
    Var->setInvalidDecl();

  if (SemaRef.getLangOpts().OpenCL)
    SemaRef.deduceOpenCLAddressSpace(Var);

  // Substitute the nested name specifier, if any.
  if (SubstQualifier(D, Var))
    return nullptr;

  SemaRef.BuildVariableInstantiation(Var, D, TemplateArgs, LateAttrs, Owner,
                                     StartingScope, InstantiatingVarTemplate);
  if (D->isNRVOVariable() && !Var->isInvalidDecl()) {
    QualType RT;
    if (auto *F = dyn_cast<FunctionDecl>(DC))
      RT = F->getReturnType();
    else if (isa<BlockDecl>(DC))
      RT = cast<FunctionType>(SemaRef.getCurBlock()->FunctionType)
               ->getReturnType();
    else
      llvm_unreachable("Unknown context type");

    // This is the last chance we have of checking copy elision eligibility
    // for functions in dependent contexts. The sema actions for building
    // the return statement during template instantiation will have no effect
    // regarding copy elision, since NRVO propagation runs on the scope exit
    // actions, and these are not run on instantiation.
    // This might run through some VarDecls which were returned from non-taken
    // 'if constexpr' branches, and these will end up being constructed on the
    // return slot even if they will never be returned, as a sort of accidental
    // 'optimization'. Notably, functions with 'auto' return types won't have it
    // deduced by this point. Coupled with the limitation described
    // previously, this makes it very hard to support copy elision for these.
    Sema::NamedReturnInfo Info = SemaRef.getNamedReturnInfo(Var);
    bool NRVO = SemaRef.getCopyElisionCandidate(Info, RT) != nullptr;
    Var->setNRVOVariable(NRVO);
  }

  Var->setImplicit(D->isImplicit());

  if (Var->isStaticLocal())
    SemaRef.CheckStaticLocalForDllExport(Var);

  if (Var->getTLSKind())
    SemaRef.CheckThreadLocalForLargeAlignment(Var);

  return Var;
}

Decl *TemplateDeclInstantiator::VisitAccessSpecDecl(AccessSpecDecl *D) {
  AccessSpecDecl* AD
    = AccessSpecDecl::Create(SemaRef.Context, D->getAccess(), Owner,
                             D->getAccessSpecifierLoc(), D->getColonLoc());
  Owner->addHiddenDecl(AD);
  return AD;
}

Decl *TemplateDeclInstantiator::VisitFieldDecl(FieldDecl *D) {
  bool Invalid = false;
  TypeSourceInfo *DI = D->getTypeSourceInfo();
  if (DI->getType()->isInstantiationDependentType() ||
      DI->getType()->isVariablyModifiedType())  {
    DI = SemaRef.SubstType(DI, TemplateArgs,
                           D->getLocation(), D->getDeclName());
    if (!DI) {
      DI = D->getTypeSourceInfo();
      Invalid = true;
    } else if (DI->getType()->isFunctionType()) {
      // C++ [temp.arg.type]p3:
      //   If a declaration acquires a function type through a type
      //   dependent on a template-parameter and this causes a
      //   declaration that does not use the syntactic form of a
      //   function declarator to have function type, the program is
      //   ill-formed.
      SemaRef.Diag(D->getLocation(), diag::err_field_instantiates_to_function)
        << DI->getType();
      Invalid = true;
    }
  } else {
    SemaRef.MarkDeclarationsReferencedInType(D->getLocation(), DI->getType());
  }

  Expr *BitWidth = D->getBitWidth();
  if (Invalid)
    BitWidth = nullptr;
  else if (BitWidth) {
    // The bit-width expression is a constant expression.
    EnterExpressionEvaluationContext Unevaluated(
        SemaRef, Sema::ExpressionEvaluationContext::ConstantEvaluated);

    ExprResult InstantiatedBitWidth
      = SemaRef.SubstExpr(BitWidth, TemplateArgs);
    if (InstantiatedBitWidth.isInvalid()) {
      Invalid = true;
      BitWidth = nullptr;
    } else
      BitWidth = InstantiatedBitWidth.getAs<Expr>();
  }

  FieldDecl *Field = SemaRef.CheckFieldDecl(D->getDeclName(),
                                            DI->getType(), DI,
                                            cast<RecordDecl>(Owner),
                                            D->getLocation(),
                                            D->isMutable(),
                                            BitWidth,
                                            D->getInClassInitStyle(),
                                            D->getInnerLocStart(),
                                            D->getAccess(),
                                            nullptr);
  if (!Field) {
    cast<Decl>(Owner)->setInvalidDecl();
    return nullptr;
  }

  SemaRef.InstantiateAttrs(TemplateArgs, D, Field, LateAttrs, StartingScope);

  if (Field->hasAttrs())
    SemaRef.CheckAlignasUnderalignment(Field);

  if (Invalid)
    Field->setInvalidDecl();

  if (!Field->getDeclName()) {
    // Keep track of where this decl came from.
    SemaRef.Context.setInstantiatedFromUnnamedFieldDecl(Field, D);
  }
  if (CXXRecordDecl *Parent= dyn_cast<CXXRecordDecl>(Field->getDeclContext())) {
    if (Parent->isAnonymousStructOrUnion() &&
        Parent->getRedeclContext()->isFunctionOrMethod())
      SemaRef.CurrentInstantiationScope->InstantiatedLocal(D, Field);
  }

  Field->setImplicit(D->isImplicit());
  Field->setAccess(D->getAccess());
  Owner->addDecl(Field);

  return Field;
}

Decl *TemplateDeclInstantiator::VisitMSPropertyDecl(MSPropertyDecl *D) {
  bool Invalid = false;
  TypeSourceInfo *DI = D->getTypeSourceInfo();

  if (DI->getType()->isVariablyModifiedType()) {
    SemaRef.Diag(D->getLocation(), diag::err_property_is_variably_modified)
      << D;
    Invalid = true;
  } else if (DI->getType()->isInstantiationDependentType())  {
    DI = SemaRef.SubstType(DI, TemplateArgs,
                           D->getLocation(), D->getDeclName());
    if (!DI) {
      DI = D->getTypeSourceInfo();
      Invalid = true;
    } else if (DI->getType()->isFunctionType()) {
      // C++ [temp.arg.type]p3:
      //   If a declaration acquires a function type through a type
      //   dependent on a template-parameter and this causes a
      //   declaration that does not use the syntactic form of a
      //   function declarator to have function type, the program is
      //   ill-formed.
      SemaRef.Diag(D->getLocation(), diag::err_field_instantiates_to_function)
      << DI->getType();
      Invalid = true;
    }
  } else {
    SemaRef.MarkDeclarationsReferencedInType(D->getLocation(), DI->getType());
  }

  MSPropertyDecl *Property = MSPropertyDecl::Create(
      SemaRef.Context, Owner, D->getLocation(), D->getDeclName(), DI->getType(),
      DI, D->getBeginLoc(), D->getGetterId(), D->getSetterId());

  SemaRef.InstantiateAttrs(TemplateArgs, D, Property, LateAttrs,
                           StartingScope);

  if (Invalid)
    Property->setInvalidDecl();

  Property->setAccess(D->getAccess());
  Owner->addDecl(Property);

  return Property;
}

Decl *TemplateDeclInstantiator::VisitIndirectFieldDecl(IndirectFieldDecl *D) {
  NamedDecl **NamedChain =
    new (SemaRef.Context)NamedDecl*[D->getChainingSize()];

  int i = 0;
  for (auto *PI : D->chain()) {
    NamedDecl *Next = SemaRef.FindInstantiatedDecl(D->getLocation(), PI,
                                              TemplateArgs);
    if (!Next)
      return nullptr;

    NamedChain[i++] = Next;
  }

  QualType T = cast<FieldDecl>(NamedChain[i-1])->getType();
  IndirectFieldDecl *IndirectField = IndirectFieldDecl::Create(
      SemaRef.Context, Owner, D->getLocation(), D->getIdentifier(), T,
      {NamedChain, D->getChainingSize()});

  for (const auto *Attr : D->attrs())
    IndirectField->addAttr(Attr->clone(SemaRef.Context));

  IndirectField->setImplicit(D->isImplicit());
  IndirectField->setAccess(D->getAccess());
  Owner->addDecl(IndirectField);
  return IndirectField;
}

Decl *TemplateDeclInstantiator::VisitFriendDecl(FriendDecl *D) {
  // Handle friend type expressions by simply substituting template
  // parameters into the pattern type and checking the result.
  if (TypeSourceInfo *Ty = D->getFriendType()) {
    TypeSourceInfo *InstTy;
    // If this is an unsupported friend, don't bother substituting template
    // arguments into it. The actual type referred to won't be used by any
    // parts of Clang, and may not be valid for instantiating. Just use the
    // same info for the instantiated friend.
    if (D->isUnsupportedFriend()) {
      InstTy = Ty;
    } else {
      InstTy = SemaRef.SubstType(Ty, TemplateArgs,
                                 D->getLocation(), DeclarationName());
    }
    if (!InstTy)
      return nullptr;

    FriendDecl *FD = FriendDecl::Create(
        SemaRef.Context, Owner, D->getLocation(), InstTy, D->getFriendLoc());
    FD->setAccess(AS_public);
    FD->setUnsupportedFriend(D->isUnsupportedFriend());
    Owner->addDecl(FD);
    return FD;
  }

  NamedDecl *ND = D->getFriendDecl();
  assert(ND && "friend decl must be a decl or a type!");

  // All of the Visit implementations for the various potential friend
  // declarations have to be carefully written to work for friend
  // objects, with the most important detail being that the target
  // decl should almost certainly not be placed in Owner.
  Decl *NewND = Visit(ND);
  if (!NewND) return nullptr;

  FriendDecl *FD =
    FriendDecl::Create(SemaRef.Context, Owner, D->getLocation(),
                       cast<NamedDecl>(NewND), D->getFriendLoc());
  FD->setAccess(AS_public);
  FD->setUnsupportedFriend(D->isUnsupportedFriend());
  Owner->addDecl(FD);
  return FD;
}

Decl *TemplateDeclInstantiator::VisitStaticAssertDecl(StaticAssertDecl *D) {
  Expr *AssertExpr = D->getAssertExpr();

  // The expression in a static assertion is a constant expression.
  EnterExpressionEvaluationContext Unevaluated(
      SemaRef, Sema::ExpressionEvaluationContext::ConstantEvaluated);

  ExprResult InstantiatedAssertExpr
    = SemaRef.SubstExpr(AssertExpr, TemplateArgs);
  if (InstantiatedAssertExpr.isInvalid())
    return nullptr;

  ExprResult InstantiatedMessageExpr =
      SemaRef.SubstExpr(D->getMessage(), TemplateArgs);
  if (InstantiatedMessageExpr.isInvalid())
    return nullptr;

  return SemaRef.BuildStaticAssertDeclaration(
      D->getLocation(), InstantiatedAssertExpr.get(),
      InstantiatedMessageExpr.get(), D->getRParenLoc(), D->isFailed());
}

Decl *TemplateDeclInstantiator::VisitEnumDecl(EnumDecl *D) {
  EnumDecl *PrevDecl = nullptr;
  if (EnumDecl *PatternPrev = getPreviousDeclForInstantiation(D)) {
    NamedDecl *Prev = SemaRef.FindInstantiatedDecl(D->getLocation(),
                                                   PatternPrev,
                                                   TemplateArgs);
    if (!Prev) return nullptr;
    PrevDecl = cast<EnumDecl>(Prev);
  }

  EnumDecl *Enum =
      EnumDecl::Create(SemaRef.Context, Owner, D->getBeginLoc(),
                       D->getLocation(), D->getIdentifier(), PrevDecl,
                       D->isScoped(), D->isScopedUsingClassTag(), D->isFixed());
  if (D->isFixed()) {
    if (TypeSourceInfo *TI = D->getIntegerTypeSourceInfo()) {
      // If we have type source information for the underlying type, it means it
      // has been explicitly set by the user. Perform substitution on it before
      // moving on.
      SourceLocation UnderlyingLoc = TI->getTypeLoc().getBeginLoc();
      TypeSourceInfo *NewTI = SemaRef.SubstType(TI, TemplateArgs, UnderlyingLoc,
                                                DeclarationName());
      if (!NewTI || SemaRef.CheckEnumUnderlyingType(NewTI))
        Enum->setIntegerType(SemaRef.Context.IntTy);
      else
        Enum->setIntegerTypeSourceInfo(NewTI);
    } else {
      assert(!D->getIntegerType()->isDependentType()
             && "Dependent type without type source info");
      Enum->setIntegerType(D->getIntegerType());
    }
  }

  SemaRef.InstantiateAttrs(TemplateArgs, D, Enum);

  Enum->setInstantiationOfMemberEnum(D, TSK_ImplicitInstantiation);
  Enum->setAccess(D->getAccess());
  // Forward the mangling number from the template to the instantiated decl.
  SemaRef.Context.setManglingNumber(Enum, SemaRef.Context.getManglingNumber(D));
  // See if the old tag was defined along with a declarator.
  // If it did, mark the new tag as being associated with that declarator.
  if (DeclaratorDecl *DD = SemaRef.Context.getDeclaratorForUnnamedTagDecl(D))
    SemaRef.Context.addDeclaratorForUnnamedTagDecl(Enum, DD);
  // See if the old tag was defined along with a typedef.
  // If it did, mark the new tag as being associated with that typedef.
  if (TypedefNameDecl *TND = SemaRef.Context.getTypedefNameForUnnamedTagDecl(D))
    SemaRef.Context.addTypedefNameForUnnamedTagDecl(Enum, TND);
  if (SubstQualifier(D, Enum)) return nullptr;
  Owner->addDecl(Enum);

  EnumDecl *Def = D->getDefinition();
  if (Def && Def != D) {
    // If this is an out-of-line definition of an enum member template, check
    // that the underlying types match in the instantiation of both
    // declarations.
    if (TypeSourceInfo *TI = Def->getIntegerTypeSourceInfo()) {
      SourceLocation UnderlyingLoc = TI->getTypeLoc().getBeginLoc();
      QualType DefnUnderlying =
        SemaRef.SubstType(TI->getType(), TemplateArgs,
                          UnderlyingLoc, DeclarationName());
      SemaRef.CheckEnumRedeclaration(Def->getLocation(), Def->isScoped(),
                                     DefnUnderlying, /*IsFixed=*/true, Enum);
    }
  }

  // C++11 [temp.inst]p1: The implicit instantiation of a class template
  // specialization causes the implicit instantiation of the declarations, but
  // not the definitions of scoped member enumerations.
  //
  // DR1484 clarifies that enumeration definitions inside of a template
  // declaration aren't considered entities that can be separately instantiated
  // from the rest of the entity they are declared inside of.
  if (isDeclWithinFunction(D) ? D == Def : Def && !Enum->isScoped()) {
    SemaRef.CurrentInstantiationScope->InstantiatedLocal(D, Enum);
    InstantiateEnumDefinition(Enum, Def);
  }

  return Enum;
}

void TemplateDeclInstantiator::InstantiateEnumDefinition(
    EnumDecl *Enum, EnumDecl *Pattern) {
  Enum->startDefinition();

  // Update the location to refer to the definition.
  Enum->setLocation(Pattern->getLocation());

  SmallVector<Decl*, 4> Enumerators;

  EnumConstantDecl *LastEnumConst = nullptr;
  for (auto *EC : Pattern->enumerators()) {
    // The specified value for the enumerator.
    ExprResult Value((Expr *)nullptr);
    if (Expr *UninstValue = EC->getInitExpr()) {
      // The enumerator's value expression is a constant expression.
      EnterExpressionEvaluationContext Unevaluated(
          SemaRef, Sema::ExpressionEvaluationContext::ConstantEvaluated);

      Value = SemaRef.SubstExpr(UninstValue, TemplateArgs);
    }

    // Drop the initial value and continue.
    bool isInvalid = false;
    if (Value.isInvalid()) {
      Value = nullptr;
      isInvalid = true;
    }

    EnumConstantDecl *EnumConst
      = SemaRef.CheckEnumConstant(Enum, LastEnumConst,
                                  EC->getLocation(), EC->getIdentifier(),
                                  Value.get());

    if (isInvalid) {
      if (EnumConst)
        EnumConst->setInvalidDecl();
      Enum->setInvalidDecl();
    }

    if (EnumConst) {
      SemaRef.InstantiateAttrs(TemplateArgs, EC, EnumConst);

      EnumConst->setAccess(Enum->getAccess());
      Enum->addDecl(EnumConst);
      Enumerators.push_back(EnumConst);
      LastEnumConst = EnumConst;

      if (Pattern->getDeclContext()->isFunctionOrMethod() &&
          !Enum->isScoped()) {
        // If the enumeration is within a function or method, record the enum
        // constant as a local.
        SemaRef.CurrentInstantiationScope->InstantiatedLocal(EC, EnumConst);
      }
    }
  }

  SemaRef.ActOnEnumBody(Enum->getLocation(), Enum->getBraceRange(), Enum,
                        Enumerators, nullptr, ParsedAttributesView());
}

Decl *TemplateDeclInstantiator::VisitEnumConstantDecl(EnumConstantDecl *D) {
  llvm_unreachable("EnumConstantDecls can only occur within EnumDecls.");
}

Decl *
TemplateDeclInstantiator::VisitBuiltinTemplateDecl(BuiltinTemplateDecl *D) {
  llvm_unreachable("BuiltinTemplateDecls cannot be instantiated.");
}

Decl *TemplateDeclInstantiator::VisitClassTemplateDecl(ClassTemplateDecl *D) {
  bool isFriend = (D->getFriendObjectKind() != Decl::FOK_None);

  // Create a local instantiation scope for this class template, which
  // will contain the instantiations of the template parameters.
  LocalInstantiationScope Scope(SemaRef);
  TemplateParameterList *TempParams = D->getTemplateParameters();
  TemplateParameterList *InstParams = SubstTemplateParams(TempParams);
  if (!InstParams)
    return nullptr;

  CXXRecordDecl *Pattern = D->getTemplatedDecl();

  // Instantiate the qualifier.  We have to do this first in case
  // we're a friend declaration, because if we are then we need to put
  // the new declaration in the appropriate context.
  NestedNameSpecifierLoc QualifierLoc = Pattern->getQualifierLoc();
  if (QualifierLoc) {
    QualifierLoc = SemaRef.SubstNestedNameSpecifierLoc(QualifierLoc,
                                                       TemplateArgs);
    if (!QualifierLoc)
      return nullptr;
  }

  CXXRecordDecl *PrevDecl = nullptr;
  ClassTemplateDecl *PrevClassTemplate = nullptr;

  if (!isFriend && getPreviousDeclForInstantiation(Pattern)) {
    DeclContext::lookup_result Found = Owner->lookup(Pattern->getDeclName());
    if (!Found.empty()) {
      PrevClassTemplate = dyn_cast<ClassTemplateDecl>(Found.front());
      if (PrevClassTemplate)
        PrevDecl = PrevClassTemplate->getTemplatedDecl();
    }
  }

  // If this isn't a friend, then it's a member template, in which
  // case we just want to build the instantiation in the
  // specialization.  If it is a friend, we want to build it in
  // the appropriate context.
  DeclContext *DC = Owner;
  if (isFriend) {
    if (QualifierLoc) {
      CXXScopeSpec SS;
      SS.Adopt(QualifierLoc);
      DC = SemaRef.computeDeclContext(SS);
      if (!DC) return nullptr;
    } else {
      DC = SemaRef.FindInstantiatedContext(Pattern->getLocation(),
                                           Pattern->getDeclContext(),
                                           TemplateArgs);
    }

    // Look for a previous declaration of the template in the owning
    // context.
    LookupResult R(SemaRef, Pattern->getDeclName(), Pattern->getLocation(),
                   Sema::LookupOrdinaryName,
                   SemaRef.forRedeclarationInCurContext());
    SemaRef.LookupQualifiedName(R, DC);

    if (R.isSingleResult()) {
      PrevClassTemplate = R.getAsSingle<ClassTemplateDecl>();
      if (PrevClassTemplate)
        PrevDecl = PrevClassTemplate->getTemplatedDecl();
    }

    if (!PrevClassTemplate && QualifierLoc) {
      SemaRef.Diag(Pattern->getLocation(), diag::err_not_tag_in_scope)
          << llvm::to_underlying(D->getTemplatedDecl()->getTagKind())
          << Pattern->getDeclName() << DC << QualifierLoc.getSourceRange();
      return nullptr;
    }
  }

  CXXRecordDecl *RecordInst = CXXRecordDecl::Create(
      SemaRef.Context, Pattern->getTagKind(), DC, Pattern->getBeginLoc(),
      Pattern->getLocation(), Pattern->getIdentifier(), PrevDecl,
      /*DelayTypeCreation=*/true);
  if (QualifierLoc)
    RecordInst->setQualifierInfo(QualifierLoc);

  SemaRef.InstantiateAttrsForDecl(TemplateArgs, Pattern, RecordInst, LateAttrs,
                                                              StartingScope);

  ClassTemplateDecl *Inst
    = ClassTemplateDecl::Create(SemaRef.Context, DC, D->getLocation(),
                                D->getIdentifier(), InstParams, RecordInst);
  RecordInst->setDescribedClassTemplate(Inst);

  if (isFriend) {
    assert(!Owner->isDependentContext());
    Inst->setLexicalDeclContext(Owner);
    RecordInst->setLexicalDeclContext(Owner);
    Inst->setObjectOfFriendDecl();

    if (PrevClassTemplate) {
      Inst->setCommonPtr(PrevClassTemplate->getCommonPtr());
      RecordInst->setTypeForDecl(
          PrevClassTemplate->getTemplatedDecl()->getTypeForDecl());
      const ClassTemplateDecl *MostRecentPrevCT =
          PrevClassTemplate->getMostRecentDecl();
      TemplateParameterList *PrevParams =
          MostRecentPrevCT->getTemplateParameters();

      // Make sure the parameter lists match.
      if (!SemaRef.TemplateParameterListsAreEqual(
              RecordInst, InstParams, MostRecentPrevCT->getTemplatedDecl(),
              PrevParams, true, Sema::TPL_TemplateMatch))
        return nullptr;

      // Do some additional validation, then merge default arguments
      // from the existing declarations.
      if (SemaRef.CheckTemplateParameterList(InstParams, PrevParams,
                                             Sema::TPC_ClassTemplate))
        return nullptr;

      Inst->setAccess(PrevClassTemplate->getAccess());
    } else {
      Inst->setAccess(D->getAccess());
    }

    Inst->setObjectOfFriendDecl();
    // TODO: do we want to track the instantiation progeny of this
    // friend target decl?
  } else {
    Inst->setAccess(D->getAccess());
    if (!PrevClassTemplate)
      Inst->setInstantiatedFromMemberTemplate(D);
  }

  Inst->setPreviousDecl(PrevClassTemplate);

  // Trigger creation of the type for the instantiation.
  SemaRef.Context.getInjectedClassNameType(
      RecordInst, Inst->getInjectedClassNameSpecialization());

  // Finish handling of friends.
  if (isFriend) {
    DC->makeDeclVisibleInContext(Inst);
    return Inst;
  }

  if (D->isOutOfLine()) {
    Inst->setLexicalDeclContext(D->getLexicalDeclContext());
    RecordInst->setLexicalDeclContext(D->getLexicalDeclContext());
  }

  Owner->addDecl(Inst);

  if (!PrevClassTemplate) {
    // Queue up any out-of-line partial specializations of this member
    // class template; the client will force their instantiation once
    // the enclosing class has been instantiated.
    SmallVector<ClassTemplatePartialSpecializationDecl *, 4> PartialSpecs;
    D->getPartialSpecializations(PartialSpecs);
    for (unsigned I = 0, N = PartialSpecs.size(); I != N; ++I)
      if (PartialSpecs[I]->getFirstDecl()->isOutOfLine())
        OutOfLinePartialSpecs.push_back(std::make_pair(Inst, PartialSpecs[I]));
  }

  return Inst;
}

Decl *
TemplateDeclInstantiator::VisitClassTemplatePartialSpecializationDecl(
                                   ClassTemplatePartialSpecializationDecl *D) {
  ClassTemplateDecl *ClassTemplate = D->getSpecializedTemplate();

  // Lookup the already-instantiated declaration in the instantiation
  // of the class template and return that.
  DeclContext::lookup_result Found
    = Owner->lookup(ClassTemplate->getDeclName());
  if (Found.empty())
    return nullptr;

  ClassTemplateDecl *InstClassTemplate
    = dyn_cast<ClassTemplateDecl>(Found.front());
  if (!InstClassTemplate)
    return nullptr;

  if (ClassTemplatePartialSpecializationDecl *Result
        = InstClassTemplate->findPartialSpecInstantiatedFromMember(D))
    return Result;

  return InstantiateClassTemplatePartialSpecialization(InstClassTemplate, D);
}

Decl *TemplateDeclInstantiator::VisitVarTemplateDecl(VarTemplateDecl *D) {
  assert(D->getTemplatedDecl()->isStaticDataMember() &&
         "Only static data member templates are allowed.");

  // Create a local instantiation scope for this variable template, which
  // will contain the instantiations of the template parameters.
  LocalInstantiationScope Scope(SemaRef);
  TemplateParameterList *TempParams = D->getTemplateParameters();
  TemplateParameterList *InstParams = SubstTemplateParams(TempParams);
  if (!InstParams)
    return nullptr;

  VarDecl *Pattern = D->getTemplatedDecl();
  VarTemplateDecl *PrevVarTemplate = nullptr;

  if (getPreviousDeclForInstantiation(Pattern)) {
    DeclContext::lookup_result Found = Owner->lookup(Pattern->getDeclName());
    if (!Found.empty())
      PrevVarTemplate = dyn_cast<VarTemplateDecl>(Found.front());
  }

  VarDecl *VarInst =
      cast_or_null<VarDecl>(VisitVarDecl(Pattern,
                                         /*InstantiatingVarTemplate=*/true));
  if (!VarInst) return nullptr;

  DeclContext *DC = Owner;

  VarTemplateDecl *Inst = VarTemplateDecl::Create(
      SemaRef.Context, DC, D->getLocation(), D->getIdentifier(), InstParams,
      VarInst);
  VarInst->setDescribedVarTemplate(Inst);
  Inst->setPreviousDecl(PrevVarTemplate);

  Inst->setAccess(D->getAccess());
  if (!PrevVarTemplate)
    Inst->setInstantiatedFromMemberTemplate(D);

  if (D->isOutOfLine()) {
    Inst->setLexicalDeclContext(D->getLexicalDeclContext());
    VarInst->setLexicalDeclContext(D->getLexicalDeclContext());
  }

  Owner->addDecl(Inst);

  if (!PrevVarTemplate) {
    // Queue up any out-of-line partial specializations of this member
    // variable template; the client will force their instantiation once
    // the enclosing class has been instantiated.
    SmallVector<VarTemplatePartialSpecializationDecl *, 4> PartialSpecs;
    D->getPartialSpecializations(PartialSpecs);
    for (unsigned I = 0, N = PartialSpecs.size(); I != N; ++I)
      if (PartialSpecs[I]->getFirstDecl()->isOutOfLine())
        OutOfLineVarPartialSpecs.push_back(
            std::make_pair(Inst, PartialSpecs[I]));
  }

  return Inst;
}

Decl *TemplateDeclInstantiator::VisitVarTemplatePartialSpecializationDecl(
    VarTemplatePartialSpecializationDecl *D) {
  assert(D->isStaticDataMember() &&
         "Only static data member templates are allowed.");

  VarTemplateDecl *VarTemplate = D->getSpecializedTemplate();

  // Lookup the already-instantiated declaration and return that.
  DeclContext::lookup_result Found = Owner->lookup(VarTemplate->getDeclName());
  assert(!Found.empty() && "Instantiation found nothing?");

  VarTemplateDecl *InstVarTemplate = dyn_cast<VarTemplateDecl>(Found.front());
  assert(InstVarTemplate && "Instantiation did not find a variable template?");

  if (VarTemplatePartialSpecializationDecl *Result =
          InstVarTemplate->findPartialSpecInstantiatedFromMember(D))
    return Result;

  return InstantiateVarTemplatePartialSpecialization(InstVarTemplate, D);
}

Decl *
TemplateDeclInstantiator::VisitFunctionTemplateDecl(FunctionTemplateDecl *D) {
  // Create a local instantiation scope for this function template, which
  // will contain the instantiations of the template parameters and then get
  // merged with the local instantiation scope for the function template
  // itself.
  LocalInstantiationScope Scope(SemaRef);
  Sema::ConstraintEvalRAII<TemplateDeclInstantiator> RAII(*this);

  TemplateParameterList *TempParams = D->getTemplateParameters();
  TemplateParameterList *InstParams = SubstTemplateParams(TempParams);
  if (!InstParams)
    return nullptr;

  FunctionDecl *Instantiated = nullptr;
  if (CXXMethodDecl *DMethod = dyn_cast<CXXMethodDecl>(D->getTemplatedDecl()))
    Instantiated = cast_or_null<FunctionDecl>(VisitCXXMethodDecl(DMethod,
                                                                 InstParams));
  else
    Instantiated = cast_or_null<FunctionDecl>(VisitFunctionDecl(
                                                          D->getTemplatedDecl(),
                                                                InstParams));

  if (!Instantiated)
    return nullptr;

  // Link the instantiated function template declaration to the function
  // template from which it was instantiated.
  FunctionTemplateDecl *InstTemplate
    = Instantiated->getDescribedFunctionTemplate();
  InstTemplate->setAccess(D->getAccess());
  assert(InstTemplate &&
         "VisitFunctionDecl/CXXMethodDecl didn't create a template!");

  bool isFriend = (InstTemplate->getFriendObjectKind() != Decl::FOK_None);

  // Link the instantiation back to the pattern *unless* this is a
  // non-definition friend declaration.
  if (!InstTemplate->getInstantiatedFromMemberTemplate() &&
      !(isFriend && !D->getTemplatedDecl()->isThisDeclarationADefinition()))
    InstTemplate->setInstantiatedFromMemberTemplate(D);

  // Make declarations visible in the appropriate context.
  if (!isFriend) {
    Owner->addDecl(InstTemplate);
  } else if (InstTemplate->getDeclContext()->isRecord() &&
             !getPreviousDeclForInstantiation(D)) {
    SemaRef.CheckFriendAccess(InstTemplate);
  }

  return InstTemplate;
}

Decl *TemplateDeclInstantiator::VisitCXXRecordDecl(CXXRecordDecl *D) {
  CXXRecordDecl *PrevDecl = nullptr;
  if (CXXRecordDecl *PatternPrev = getPreviousDeclForInstantiation(D)) {
    NamedDecl *Prev = SemaRef.FindInstantiatedDecl(D->getLocation(),
                                                   PatternPrev,
                                                   TemplateArgs);
    if (!Prev) return nullptr;
    PrevDecl = cast<CXXRecordDecl>(Prev);
  }

  CXXRecordDecl *Record = nullptr;
  bool IsInjectedClassName = D->isInjectedClassName();
  if (D->isLambda())
    Record = CXXRecordDecl::CreateLambda(
        SemaRef.Context, Owner, D->getLambdaTypeInfo(), D->getLocation(),
        D->getLambdaDependencyKind(), D->isGenericLambda(),
        D->getLambdaCaptureDefault());
  else
    Record = CXXRecordDecl::Create(SemaRef.Context, D->getTagKind(), Owner,
                                   D->getBeginLoc(), D->getLocation(),
                                   D->getIdentifier(), PrevDecl,
                                   /*DelayTypeCreation=*/IsInjectedClassName);
  // Link the type of the injected-class-name to that of the outer class.
  if (IsInjectedClassName)
    (void)SemaRef.Context.getTypeDeclType(Record, cast<CXXRecordDecl>(Owner));

  // Substitute the nested name specifier, if any.
  if (SubstQualifier(D, Record))
    return nullptr;

  SemaRef.InstantiateAttrsForDecl(TemplateArgs, D, Record, LateAttrs,
                                                              StartingScope);

  Record->setImplicit(D->isImplicit());
  // FIXME: Check against AS_none is an ugly hack to work around the issue that
  // the tag decls introduced by friend class declarations don't have an access
  // specifier. Remove once this area of the code gets sorted out.
  if (D->getAccess() != AS_none)
    Record->setAccess(D->getAccess());
  if (!IsInjectedClassName)
    Record->setInstantiationOfMemberClass(D, TSK_ImplicitInstantiation);

  // If the original function was part of a friend declaration,
  // inherit its namespace state.
  if (D->getFriendObjectKind())
    Record->setObjectOfFriendDecl();

  // Make sure that anonymous structs and unions are recorded.
  if (D->isAnonymousStructOrUnion())
    Record->setAnonymousStructOrUnion(true);

  if (D->isLocalClass())
    SemaRef.CurrentInstantiationScope->InstantiatedLocal(D, Record);

  // Forward the mangling number from the template to the instantiated decl.
  SemaRef.Context.setManglingNumber(Record,
                                    SemaRef.Context.getManglingNumber(D));

  // See if the old tag was defined along with a declarator.
  // If it did, mark the new tag as being associated with that declarator.
  if (DeclaratorDecl *DD = SemaRef.Context.getDeclaratorForUnnamedTagDecl(D))
    SemaRef.Context.addDeclaratorForUnnamedTagDecl(Record, DD);

  // See if the old tag was defined along with a typedef.
  // If it did, mark the new tag as being associated with that typedef.
  if (TypedefNameDecl *TND = SemaRef.Context.getTypedefNameForUnnamedTagDecl(D))
    SemaRef.Context.addTypedefNameForUnnamedTagDecl(Record, TND);

  Owner->addDecl(Record);

  // DR1484 clarifies that the members of a local class are instantiated as part
  // of the instantiation of their enclosing entity.
  if (D->isCompleteDefinition() && D->isLocalClass()) {
    Sema::LocalEagerInstantiationScope LocalInstantiations(SemaRef);

    SemaRef.InstantiateClass(D->getLocation(), Record, D, TemplateArgs,
                             TSK_ImplicitInstantiation,
                             /*Complain=*/true);

    // For nested local classes, we will instantiate the members when we
    // reach the end of the outermost (non-nested) local class.
    if (!D->isCXXClassMember())
      SemaRef.InstantiateClassMembers(D->getLocation(), Record, TemplateArgs,
                                      TSK_ImplicitInstantiation);

    // This class may have local implicit instantiations that need to be
    // performed within this scope.
    LocalInstantiations.perform();
  }

  SemaRef.DiagnoseUnusedNestedTypedefs(Record);

  if (IsInjectedClassName)
    assert(Record->isInjectedClassName() && "Broken injected-class-name");

  return Record;
}

/// Adjust the given function type for an instantiation of the
/// given declaration, to cope with modifications to the function's type that
/// aren't reflected in the type-source information.
///
/// \param D The declaration we're instantiating.
/// \param TInfo The already-instantiated type.
static QualType adjustFunctionTypeForInstantiation(ASTContext &Context,
                                                   FunctionDecl *D,
                                                   TypeSourceInfo *TInfo) {
  const FunctionProtoType *OrigFunc
    = D->getType()->castAs<FunctionProtoType>();
  const FunctionProtoType *NewFunc
    = TInfo->getType()->castAs<FunctionProtoType>();
  if (OrigFunc->getExtInfo() == NewFunc->getExtInfo())
    return TInfo->getType();

  FunctionProtoType::ExtProtoInfo NewEPI = NewFunc->getExtProtoInfo();
  NewEPI.ExtInfo = OrigFunc->getExtInfo();
  return Context.getFunctionType(NewFunc->getReturnType(),
                                 NewFunc->getParamTypes(), NewEPI);
}

/// Normal class members are of more specific types and therefore
/// don't make it here.  This function serves three purposes:
///   1) instantiating function templates
///   2) substituting friend and local function declarations
///   3) substituting deduction guide declarations for nested class templates
Decl *TemplateDeclInstantiator::VisitFunctionDecl(
    FunctionDecl *D, TemplateParameterList *TemplateParams,
    RewriteKind FunctionRewriteKind) {
  // Check whether there is already a function template specialization for
  // this declaration.
  FunctionTemplateDecl *FunctionTemplate = D->getDescribedFunctionTemplate();
  if (FunctionTemplate && !TemplateParams) {
    ArrayRef<TemplateArgument> Innermost = TemplateArgs.getInnermost();

    void *InsertPos = nullptr;
    FunctionDecl *SpecFunc
      = FunctionTemplate->findSpecialization(Innermost, InsertPos);

    // If we already have a function template specialization, return it.
    if (SpecFunc)
      return SpecFunc;
  }

  bool isFriend;
  if (FunctionTemplate)
    isFriend = (FunctionTemplate->getFriendObjectKind() != Decl::FOK_None);
  else
    isFriend = (D->getFriendObjectKind() != Decl::FOK_None);

  bool MergeWithParentScope = (TemplateParams != nullptr) ||
    Owner->isFunctionOrMethod() ||
    !(isa<Decl>(Owner) &&
      cast<Decl>(Owner)->isDefinedOutsideFunctionOrMethod());
  LocalInstantiationScope Scope(SemaRef, MergeWithParentScope);

  ExplicitSpecifier InstantiatedExplicitSpecifier;
  if (auto *DGuide = dyn_cast<CXXDeductionGuideDecl>(D)) {
    InstantiatedExplicitSpecifier = SemaRef.instantiateExplicitSpecifier(
        TemplateArgs, DGuide->getExplicitSpecifier());
    if (InstantiatedExplicitSpecifier.isInvalid())
      return nullptr;
  }

  SmallVector<ParmVarDecl *, 4> Params;
  TypeSourceInfo *TInfo = SubstFunctionType(D, Params);
  if (!TInfo)
    return nullptr;
  QualType T = adjustFunctionTypeForInstantiation(SemaRef.Context, D, TInfo);

  if (TemplateParams && TemplateParams->size()) {
    auto *LastParam =
        dyn_cast<TemplateTypeParmDecl>(TemplateParams->asArray().back());
    if (LastParam && LastParam->isImplicit() &&
        LastParam->hasTypeConstraint()) {
      // In abbreviated templates, the type-constraints of invented template
      // type parameters are instantiated with the function type, invalidating
      // the TemplateParameterList which relied on the template type parameter
      // not having a type constraint. Recreate the TemplateParameterList with
      // the updated parameter list.
      TemplateParams = TemplateParameterList::Create(
          SemaRef.Context, TemplateParams->getTemplateLoc(),
          TemplateParams->getLAngleLoc(), TemplateParams->asArray(),
          TemplateParams->getRAngleLoc(), TemplateParams->getRequiresClause());
    }
  }

  NestedNameSpecifierLoc QualifierLoc = D->getQualifierLoc();
  if (QualifierLoc) {
    QualifierLoc = SemaRef.SubstNestedNameSpecifierLoc(QualifierLoc,
                                                       TemplateArgs);
    if (!QualifierLoc)
      return nullptr;
  }

  Expr *TrailingRequiresClause = D->getTrailingRequiresClause();

  // If we're instantiating a local function declaration, put the result
  // in the enclosing namespace; otherwise we need to find the instantiated
  // context.
  DeclContext *DC;
  if (D->isLocalExternDecl()) {
    DC = Owner;
    SemaRef.adjustContextForLocalExternDecl(DC);
  } else if (isFriend && QualifierLoc) {
    CXXScopeSpec SS;
    SS.Adopt(QualifierLoc);
    DC = SemaRef.computeDeclContext(SS);
    if (!DC) return nullptr;
  } else {
    DC = SemaRef.FindInstantiatedContext(D->getLocation(), D->getDeclContext(),
                                         TemplateArgs);
  }

  DeclarationNameInfo NameInfo
    = SemaRef.SubstDeclarationNameInfo(D->getNameInfo(), TemplateArgs);

  if (FunctionRewriteKind != RewriteKind::None)
    adjustForRewrite(FunctionRewriteKind, D, T, TInfo, NameInfo);

  FunctionDecl *Function;
  if (auto *DGuide = dyn_cast<CXXDeductionGuideDecl>(D)) {
    Function = CXXDeductionGuideDecl::Create(
        SemaRef.Context, DC, D->getInnerLocStart(),
        InstantiatedExplicitSpecifier, NameInfo, T, TInfo,
        D->getSourceRange().getEnd(), DGuide->getCorrespondingConstructor(),
        DGuide->getDeductionCandidateKind());
    Function->setAccess(D->getAccess());
  } else {
    Function = FunctionDecl::Create(
        SemaRef.Context, DC, D->getInnerLocStart(), NameInfo, T, TInfo,
        D->getCanonicalDecl()->getStorageClass(), D->UsesFPIntrin(),
        D->isInlineSpecified(), D->hasWrittenPrototype(), D->getConstexprKind(),
        TrailingRequiresClause);
    Function->setFriendConstraintRefersToEnclosingTemplate(
        D->FriendConstraintRefersToEnclosingTemplate());
    Function->setRangeEnd(D->getSourceRange().getEnd());
  }

  if (D->isInlined())
    Function->setImplicitlyInline();

  if (QualifierLoc)
    Function->setQualifierInfo(QualifierLoc);

  if (D->isLocalExternDecl())
    Function->setLocalExternDecl();

  DeclContext *LexicalDC = Owner;
  if (!isFriend && D->isOutOfLine() && !D->isLocalExternDecl()) {
    assert(D->getDeclContext()->isFileContext());
    LexicalDC = D->getDeclContext();
  }
  else if (D->isLocalExternDecl()) {
    LexicalDC = SemaRef.CurContext;
  }

  Function->setLexicalDeclContext(LexicalDC);

  // Attach the parameters
  for (unsigned P = 0; P < Params.size(); ++P)
    if (Params[P])
      Params[P]->setOwningFunction(Function);
  Function->setParams(Params);

  if (TrailingRequiresClause)
    Function->setTrailingRequiresClause(TrailingRequiresClause);

  if (TemplateParams) {
    // Our resulting instantiation is actually a function template, since we
    // are substituting only the outer template parameters. For example, given
    //
    //   template<typename T>
    //   struct X {
    //     template<typename U> friend void f(T, U);
    //   };
    //
    //   X<int> x;
    //
    // We are instantiating the friend function template "f" within X<int>,
    // which means substituting int for T, but leaving "f" as a friend function
    // template.
    // Build the function template itself.
    FunctionTemplate = FunctionTemplateDecl::Create(SemaRef.Context, DC,
                                                    Function->getLocation(),
                                                    Function->getDeclName(),
                                                    TemplateParams, Function);
    Function->setDescribedFunctionTemplate(FunctionTemplate);

    FunctionTemplate->setLexicalDeclContext(LexicalDC);

    if (isFriend && D->isThisDeclarationADefinition()) {
      FunctionTemplate->setInstantiatedFromMemberTemplate(
                                           D->getDescribedFunctionTemplate());
    }
  } else if (FunctionTemplate &&
             SemaRef.CodeSynthesisContexts.back().Kind !=
                 Sema::CodeSynthesisContext::BuildingDeductionGuides) {
    // Record this function template specialization.
    ArrayRef<TemplateArgument> Innermost = TemplateArgs.getInnermost();
    Function->setFunctionTemplateSpecialization(FunctionTemplate,
                            TemplateArgumentList::CreateCopy(SemaRef.Context,
                                                             Innermost),
                                                /*InsertPos=*/nullptr);
  } else if (FunctionRewriteKind == RewriteKind::None) {
    if (isFriend && D->isThisDeclarationADefinition()) {
      // Do not connect the friend to the template unless it's actually a
      // definition. We don't want non-template functions to be marked as being
      // template instantiations.
      Function->setInstantiationOfMemberFunction(D, TSK_ImplicitInstantiation);
    } else if (!isFriend) {
      // If this is not a function template, and this is not a friend (that is,
      // this is a locally declared function), save the instantiation
      // relationship for the purposes of constraint instantiation.
      Function->setInstantiatedFromDecl(D);
    }
  }

  if (isFriend) {
    Function->setObjectOfFriendDecl();
    if (FunctionTemplateDecl *FT = Function->getDescribedFunctionTemplate())
      FT->setObjectOfFriendDecl();
  }

  if (InitFunctionInstantiation(Function, D))
    Function->setInvalidDecl();

  bool IsExplicitSpecialization = false;

  LookupResult Previous(
      SemaRef, Function->getDeclName(), SourceLocation(),
      D->isLocalExternDecl() ? Sema::LookupRedeclarationWithLinkage
                             : Sema::LookupOrdinaryName,
      D->isLocalExternDecl() ? RedeclarationKind::ForExternalRedeclaration
                             : SemaRef.forRedeclarationInCurContext());

  if (DependentFunctionTemplateSpecializationInfo *DFTSI =
          D->getDependentSpecializationInfo()) {
    assert(isFriend && "dependent specialization info on "
                       "non-member non-friend function?");

    // Instantiate the explicit template arguments.
    TemplateArgumentListInfo ExplicitArgs;
    if (const auto *ArgsWritten = DFTSI->TemplateArgumentsAsWritten) {
      ExplicitArgs.setLAngleLoc(ArgsWritten->getLAngleLoc());
      ExplicitArgs.setRAngleLoc(ArgsWritten->getRAngleLoc());
      if (SemaRef.SubstTemplateArguments(ArgsWritten->arguments(), TemplateArgs,
                                         ExplicitArgs))
        return nullptr;
    }

    // Map the candidates for the primary template to their instantiations.
    for (FunctionTemplateDecl *FTD : DFTSI->getCandidates()) {
      if (NamedDecl *ND =
              SemaRef.FindInstantiatedDecl(D->getLocation(), FTD, TemplateArgs))
        Previous.addDecl(ND);
      else
        return nullptr;
    }

    if (SemaRef.CheckFunctionTemplateSpecialization(
            Function,
            DFTSI->TemplateArgumentsAsWritten ? &ExplicitArgs : nullptr,
            Previous))
      Function->setInvalidDecl();

    IsExplicitSpecialization = true;
  } else if (const ASTTemplateArgumentListInfo *ArgsWritten =
                 D->getTemplateSpecializationArgsAsWritten()) {
    // The name of this function was written as a template-id.
    SemaRef.LookupQualifiedName(Previous, DC);

    // Instantiate the explicit template arguments.
    TemplateArgumentListInfo ExplicitArgs(ArgsWritten->getLAngleLoc(),
                                          ArgsWritten->getRAngleLoc());
    if (SemaRef.SubstTemplateArguments(ArgsWritten->arguments(), TemplateArgs,
                                       ExplicitArgs))
      return nullptr;

    if (SemaRef.CheckFunctionTemplateSpecialization(Function,
                                                    &ExplicitArgs,
                                                    Previous))
      Function->setInvalidDecl();

    IsExplicitSpecialization = true;
  } else if (TemplateParams || !FunctionTemplate) {
    // Look only into the namespace where the friend would be declared to
    // find a previous declaration. This is the innermost enclosing namespace,
    // as described in ActOnFriendFunctionDecl.
    SemaRef.LookupQualifiedName(Previous, DC->getRedeclContext());

    // In C++, the previous declaration we find might be a tag type
    // (class or enum). In this case, the new declaration will hide the
    // tag type. Note that this does not apply if we're declaring a
    // typedef (C++ [dcl.typedef]p4).
    if (Previous.isSingleTagDecl())
      Previous.clear();

    // Filter out previous declarations that don't match the scope. The only
    // effect this has is to remove declarations found in inline namespaces
    // for friend declarations with unqualified names.
    if (isFriend && !QualifierLoc) {
      SemaRef.FilterLookupForScope(Previous, DC, /*Scope=*/ nullptr,
                                   /*ConsiderLinkage=*/ true,
                                   QualifierLoc.hasQualifier());
    }
  }

  // Per [temp.inst], default arguments in function declarations at local scope
  // are instantiated along with the enclosing declaration. For example:
  //
  //   template<typename T>
  //   void ft() {
  //     void f(int = []{ return T::value; }());
  //   }
  //   template void ft<int>(); // error: type 'int' cannot be used prior
  //                                      to '::' because it has no members
  //
  // The error is issued during instantiation of ft<int>() because substitution
  // into the default argument fails; the default argument is instantiated even
  // though it is never used.
  if (Function->isLocalExternDecl()) {
    for (ParmVarDecl *PVD : Function->parameters()) {
      if (!PVD->hasDefaultArg())
        continue;
      if (SemaRef.SubstDefaultArgument(D->getInnerLocStart(), PVD, TemplateArgs)) {
        // If substitution fails, the default argument is set to a
        // RecoveryExpr that wraps the uninstantiated default argument so
        // that downstream diagnostics are omitted.
        Expr *UninstExpr = PVD->getUninstantiatedDefaultArg();
        ExprResult ErrorResult = SemaRef.CreateRecoveryExpr(
            UninstExpr->getBeginLoc(), UninstExpr->getEndLoc(),
            { UninstExpr }, UninstExpr->getType());
        if (ErrorResult.isUsable())
          PVD->setDefaultArg(ErrorResult.get());
      }
    }
  }

  SemaRef.CheckFunctionDeclaration(/*Scope*/ nullptr, Function, Previous,
                                   IsExplicitSpecialization,
                                   Function->isThisDeclarationADefinition());

  // Check the template parameter list against the previous declaration. The
  // goal here is to pick up default arguments added since the friend was
  // declared; we know the template parameter lists match, since otherwise
  // we would not have picked this template as the previous declaration.
  if (isFriend && TemplateParams && FunctionTemplate->getPreviousDecl()) {
    SemaRef.CheckTemplateParameterList(
        TemplateParams,
        FunctionTemplate->getPreviousDecl()->getTemplateParameters(),
        Function->isThisDeclarationADefinition()
            ? Sema::TPC_FriendFunctionTemplateDefinition
            : Sema::TPC_FriendFunctionTemplate);
  }

  // If we're introducing a friend definition after the first use, trigger
  // instantiation.
  // FIXME: If this is a friend function template definition, we should check
  // to see if any specializations have been used.
  if (isFriend && D->isThisDeclarationADefinition() && Function->isUsed(false)) {
    if (MemberSpecializationInfo *MSInfo =
            Function->getMemberSpecializationInfo()) {
      if (MSInfo->getPointOfInstantiation().isInvalid()) {
        SourceLocation Loc = D->getLocation(); // FIXME
        MSInfo->setPointOfInstantiation(Loc);
        SemaRef.PendingLocalImplicitInstantiations.push_back(
            std::make_pair(Function, Loc));
      }
    }
  }

  if (D->isExplicitlyDefaulted()) {
    if (SubstDefaultedFunction(Function, D))
      return nullptr;
  }
  if (D->isDeleted())
    SemaRef.SetDeclDeleted(Function, D->getLocation(), D->getDeletedMessage());

  NamedDecl *PrincipalDecl =
      (TemplateParams ? cast<NamedDecl>(FunctionTemplate) : Function);

  // If this declaration lives in a different context from its lexical context,
  // add it to the corresponding lookup table.
  if (isFriend ||
      (Function->isLocalExternDecl() && !Function->getPreviousDecl()))
    DC->makeDeclVisibleInContext(PrincipalDecl);

  if (Function->isOverloadedOperator() && !DC->isRecord() &&
      PrincipalDecl->isInIdentifierNamespace(Decl::IDNS_Ordinary))
    PrincipalDecl->setNonMemberOperator();

  return Function;
}

Decl *TemplateDeclInstantiator::VisitCXXMethodDecl(
    CXXMethodDecl *D, TemplateParameterList *TemplateParams,
    RewriteKind FunctionRewriteKind) {
  FunctionTemplateDecl *FunctionTemplate = D->getDescribedFunctionTemplate();
  if (FunctionTemplate && !TemplateParams) {
    // We are creating a function template specialization from a function
    // template. Check whether there is already a function template
    // specialization for this particular set of template arguments.
    ArrayRef<TemplateArgument> Innermost = TemplateArgs.getInnermost();

    void *InsertPos = nullptr;
    FunctionDecl *SpecFunc
      = FunctionTemplate->findSpecialization(Innermost, InsertPos);

    // If we already have a function template specialization, return it.
    if (SpecFunc)
      return SpecFunc;
  }

  bool isFriend;
  if (FunctionTemplate)
    isFriend = (FunctionTemplate->getFriendObjectKind() != Decl::FOK_None);
  else
    isFriend = (D->getFriendObjectKind() != Decl::FOK_None);

  bool MergeWithParentScope = (TemplateParams != nullptr) ||
    !(isa<Decl>(Owner) &&
      cast<Decl>(Owner)->isDefinedOutsideFunctionOrMethod());
  LocalInstantiationScope Scope(SemaRef, MergeWithParentScope);

  Sema::LambdaScopeForCallOperatorInstantiationRAII LambdaScope(
      SemaRef, const_cast<CXXMethodDecl *>(D), TemplateArgs, Scope);

  // Instantiate enclosing template arguments for friends.
  SmallVector<TemplateParameterList *, 4> TempParamLists;
  unsigned NumTempParamLists = 0;
  if (isFriend && (NumTempParamLists = D->getNumTemplateParameterLists())) {
    TempParamLists.resize(NumTempParamLists);
    for (unsigned I = 0; I != NumTempParamLists; ++I) {
      TemplateParameterList *TempParams = D->getTemplateParameterList(I);
      TemplateParameterList *InstParams = SubstTemplateParams(TempParams);
      if (!InstParams)
        return nullptr;
      TempParamLists[I] = InstParams;
    }
  }

  auto InstantiatedExplicitSpecifier = ExplicitSpecifier::getFromDecl(D);
  // deduction guides need this
  const bool CouldInstantiate =
      InstantiatedExplicitSpecifier.getExpr() == nullptr ||
      !InstantiatedExplicitSpecifier.getExpr()->isValueDependent();

  // Delay the instantiation of the explicit-specifier until after the
  // constraints are checked during template argument deduction.
  if (CouldInstantiate ||
      SemaRef.CodeSynthesisContexts.back().Kind !=
          Sema::CodeSynthesisContext::DeducedTemplateArgumentSubstitution) {
    InstantiatedExplicitSpecifier = SemaRef.instantiateExplicitSpecifier(
        TemplateArgs, InstantiatedExplicitSpecifier);

    if (InstantiatedExplicitSpecifier.isInvalid())
      return nullptr;
  } else {
    InstantiatedExplicitSpecifier.setKind(ExplicitSpecKind::Unresolved);
  }

  // Implicit destructors/constructors created for local classes in
  // DeclareImplicit* (see SemaDeclCXX.cpp) might not have an associated TSI.
  // Unfortunately there isn't enough context in those functions to
  // conditionally populate the TSI without breaking non-template related use
  // cases. Populate TSIs prior to calling SubstFunctionType to make sure we get
  // a proper transformation.
  if (cast<CXXRecordDecl>(D->getParent())->isLambda() &&
      !D->getTypeSourceInfo() &&
      isa<CXXConstructorDecl, CXXDestructorDecl>(D)) {
    TypeSourceInfo *TSI =
        SemaRef.Context.getTrivialTypeSourceInfo(D->getType());
    D->setTypeSourceInfo(TSI);
  }

  SmallVector<ParmVarDecl *, 4> Params;
  TypeSourceInfo *TInfo = SubstFunctionType(D, Params);
  if (!TInfo)
    return nullptr;
  QualType T = adjustFunctionTypeForInstantiation(SemaRef.Context, D, TInfo);

  if (TemplateParams && TemplateParams->size()) {
    auto *LastParam =
        dyn_cast<TemplateTypeParmDecl>(TemplateParams->asArray().back());
    if (LastParam && LastParam->isImplicit() &&
        LastParam->hasTypeConstraint()) {
      // In abbreviated templates, the type-constraints of invented template
      // type parameters are instantiated with the function type, invalidating
      // the TemplateParameterList which relied on the template type parameter
      // not having a type constraint. Recreate the TemplateParameterList with
      // the updated parameter list.
      TemplateParams = TemplateParameterList::Create(
          SemaRef.Context, TemplateParams->getTemplateLoc(),
          TemplateParams->getLAngleLoc(), TemplateParams->asArray(),
          TemplateParams->getRAngleLoc(), TemplateParams->getRequiresClause());
    }
  }

  NestedNameSpecifierLoc QualifierLoc = D->getQualifierLoc();
  if (QualifierLoc) {
    QualifierLoc = SemaRef.SubstNestedNameSpecifierLoc(QualifierLoc,
                                                 TemplateArgs);
    if (!QualifierLoc)
      return nullptr;
  }

  DeclContext *DC = Owner;
  if (isFriend) {
    if (QualifierLoc) {
      CXXScopeSpec SS;
      SS.Adopt(QualifierLoc);
      DC = SemaRef.computeDeclContext(SS);

      if (DC && SemaRef.RequireCompleteDeclContext(SS, DC))
        return nullptr;
    } else {
      DC = SemaRef.FindInstantiatedContext(D->getLocation(),
                                           D->getDeclContext(),
                                           TemplateArgs);
    }
    if (!DC) return nullptr;
  }

  CXXRecordDecl *Record = cast<CXXRecordDecl>(DC);
  Expr *TrailingRequiresClause = D->getTrailingRequiresClause();

  DeclarationNameInfo NameInfo
    = SemaRef.SubstDeclarationNameInfo(D->getNameInfo(), TemplateArgs);

  if (FunctionRewriteKind != RewriteKind::None)
    adjustForRewrite(FunctionRewriteKind, D, T, TInfo, NameInfo);

  // Build the instantiated method declaration.
  CXXMethodDecl *Method = nullptr;

  SourceLocation StartLoc = D->getInnerLocStart();
  if (CXXConstructorDecl *Constructor = dyn_cast<CXXConstructorDecl>(D)) {
    Method = CXXConstructorDecl::Create(
        SemaRef.Context, Record, StartLoc, NameInfo, T, TInfo,
        InstantiatedExplicitSpecifier, Constructor->UsesFPIntrin(),
        Constructor->isInlineSpecified(), false,
        Constructor->getConstexprKind(), InheritedConstructor(),
        TrailingRequiresClause);
    Method->setRangeEnd(Constructor->getEndLoc());
  } else if (CXXDestructorDecl *Destructor = dyn_cast<CXXDestructorDecl>(D)) {
    Method = CXXDestructorDecl::Create(
        SemaRef.Context, Record, StartLoc, NameInfo, T, TInfo,
        Destructor->UsesFPIntrin(), Destructor->isInlineSpecified(), false,
        Destructor->getConstexprKind(), TrailingRequiresClause);
    Method->setIneligibleOrNotSelected(true);
    Method->setRangeEnd(Destructor->getEndLoc());
    Method->setDeclName(SemaRef.Context.DeclarationNames.getCXXDestructorName(
        SemaRef.Context.getCanonicalType(
            SemaRef.Context.getTypeDeclType(Record))));
  } else if (CXXConversionDecl *Conversion = dyn_cast<CXXConversionDecl>(D)) {
    Method = CXXConversionDecl::Create(
        SemaRef.Context, Record, StartLoc, NameInfo, T, TInfo,
        Conversion->UsesFPIntrin(), Conversion->isInlineSpecified(),
        InstantiatedExplicitSpecifier, Conversion->getConstexprKind(),
        Conversion->getEndLoc(), TrailingRequiresClause);
  } else {
    StorageClass SC = D->isStatic() ? SC_Static : SC_None;
    Method = CXXMethodDecl::Create(
        SemaRef.Context, Record, StartLoc, NameInfo, T, TInfo, SC,
        D->UsesFPIntrin(), D->isInlineSpecified(), D->getConstexprKind(),
        D->getEndLoc(), TrailingRequiresClause);
  }

  if (D->isInlined())
    Method->setImplicitlyInline();

  if (QualifierLoc)
    Method->setQualifierInfo(QualifierLoc);

  if (TemplateParams) {
    // Our resulting instantiation is actually a function template, since we
    // are substituting only the outer template parameters. For example, given
    //
    //   template<typename T>
    //   struct X {
    //     template<typename U> void f(T, U);
    //   };
    //
    //   X<int> x;
    //
    // We are instantiating the member template "f" within X<int>, which means
    // substituting int for T, but leaving "f" as a member function template.
    // Build the function template itself.
    FunctionTemplate = FunctionTemplateDecl::Create(SemaRef.Context, Record,
                                                    Method->getLocation(),
                                                    Method->getDeclName(),
                                                    TemplateParams, Method);
    if (isFriend) {
      FunctionTemplate->setLexicalDeclContext(Owner);
      FunctionTemplate->setObjectOfFriendDecl();
    } else if (D->isOutOfLine())
      FunctionTemplate->setLexicalDeclContext(D->getLexicalDeclContext());
    Method->setDescribedFunctionTemplate(FunctionTemplate);
  } else if (FunctionTemplate) {
    // Record this function template specialization.
    ArrayRef<TemplateArgument> Innermost = TemplateArgs.getInnermost();
    Method->setFunctionTemplateSpecialization(FunctionTemplate,
                         TemplateArgumentList::CreateCopy(SemaRef.Context,
                                                          Innermost),
                                              /*InsertPos=*/nullptr);
  } else if (!isFriend && FunctionRewriteKind == RewriteKind::None) {
    // Record that this is an instantiation of a member function.
    Method->setInstantiationOfMemberFunction(D, TSK_ImplicitInstantiation);
  }

  // If we are instantiating a member function defined
  // out-of-line, the instantiation will have the same lexical
  // context (which will be a namespace scope) as the template.
  if (isFriend) {
    if (NumTempParamLists)
      Method->setTemplateParameterListsInfo(
          SemaRef.Context,
          llvm::ArrayRef(TempParamLists.data(), NumTempParamLists));

    Method->setLexicalDeclContext(Owner);
    Method->setObjectOfFriendDecl();
  } else if (D->isOutOfLine())
    Method->setLexicalDeclContext(D->getLexicalDeclContext());

  // Attach the parameters
  for (unsigned P = 0; P < Params.size(); ++P)
    Params[P]->setOwningFunction(Method);
  Method->setParams(Params);

  if (InitMethodInstantiation(Method, D))
    Method->setInvalidDecl();

  LookupResult Previous(SemaRef, NameInfo, Sema::LookupOrdinaryName,
                        RedeclarationKind::ForExternalRedeclaration);

  bool IsExplicitSpecialization = false;

  // If the name of this function was written as a template-id, instantiate
  // the explicit template arguments.
  if (DependentFunctionTemplateSpecializationInfo *DFTSI =
          D->getDependentSpecializationInfo()) {
    // Instantiate the explicit template arguments.
    TemplateArgumentListInfo ExplicitArgs;
    if (const auto *ArgsWritten = DFTSI->TemplateArgumentsAsWritten) {
      ExplicitArgs.setLAngleLoc(ArgsWritten->getLAngleLoc());
      ExplicitArgs.setRAngleLoc(ArgsWritten->getRAngleLoc());
      if (SemaRef.SubstTemplateArguments(ArgsWritten->arguments(), TemplateArgs,
                                         ExplicitArgs))
        return nullptr;
    }

    // Map the candidates for the primary template to their instantiations.
    for (FunctionTemplateDecl *FTD : DFTSI->getCandidates()) {
      if (NamedDecl *ND =
              SemaRef.FindInstantiatedDecl(D->getLocation(), FTD, TemplateArgs))
        Previous.addDecl(ND);
      else
        return nullptr;
    }

    if (SemaRef.CheckFunctionTemplateSpecialization(
            Method, DFTSI->TemplateArgumentsAsWritten ? &ExplicitArgs : nullptr,
            Previous))
      Method->setInvalidDecl();

    IsExplicitSpecialization = true;
  } else if (const ASTTemplateArgumentListInfo *ArgsWritten =
                 D->getTemplateSpecializationArgsAsWritten()) {
    SemaRef.LookupQualifiedName(Previous, DC);

    TemplateArgumentListInfo ExplicitArgs(ArgsWritten->getLAngleLoc(),
                                          ArgsWritten->getRAngleLoc());

    if (SemaRef.SubstTemplateArguments(ArgsWritten->arguments(), TemplateArgs,
                                       ExplicitArgs))
      return nullptr;

    if (SemaRef.CheckFunctionTemplateSpecialization(Method,
                                                    &ExplicitArgs,
                                                    Previous))
      Method->setInvalidDecl();

    IsExplicitSpecialization = true;
  } else if (!FunctionTemplate || TemplateParams || isFriend) {
    SemaRef.LookupQualifiedName(Previous, Record);

    // In C++, the previous declaration we find might be a tag type
    // (class or enum). In this case, the new declaration will hide the
    // tag type. Note that this does not apply if we're declaring a
    // typedef (C++ [dcl.typedef]p4).
    if (Previous.isSingleTagDecl())
      Previous.clear();
  }

  // Per [temp.inst], default arguments in member functions of local classes
  // are instantiated along with the member function declaration. For example:
  //
  //   template<typename T>
  //   void ft() {
  //     struct lc {
  //       int operator()(int p = []{ return T::value; }());
  //     };
  //   }
  //   template void ft<int>(); // error: type 'int' cannot be used prior
  //                                      to '::'because it has no members
  //
  // The error is issued during instantiation of ft<int>()::lc::operator()
  // because substitution into the default argument fails; the default argument
  // is instantiated even though it is never used.
  if (D->isInLocalScopeForInstantiation()) {
    for (unsigned P = 0; P < Params.size(); ++P) {
      if (!Params[P]->hasDefaultArg())
        continue;
      if (SemaRef.SubstDefaultArgument(StartLoc, Params[P], TemplateArgs)) {
        // If substitution fails, the default argument is set to a
        // RecoveryExpr that wraps the uninstantiated default argument so
        // that downstream diagnostics are omitted.
        Expr *UninstExpr = Params[P]->getUninstantiatedDefaultArg();
        ExprResult ErrorResult = SemaRef.CreateRecoveryExpr(
            UninstExpr->getBeginLoc(), UninstExpr->getEndLoc(),
            { UninstExpr }, UninstExpr->getType());
        if (ErrorResult.isUsable())
          Params[P]->setDefaultArg(ErrorResult.get());
      }
    }
  }

  SemaRef.CheckFunctionDeclaration(nullptr, Method, Previous,
                                   IsExplicitSpecialization,
                                   Method->isThisDeclarationADefinition());

  if (D->isPureVirtual())
    SemaRef.CheckPureMethod(Method, SourceRange());

  // Propagate access.  For a non-friend declaration, the access is
  // whatever we're propagating from.  For a friend, it should be the
  // previous declaration we just found.
  if (isFriend && Method->getPreviousDecl())
    Method->setAccess(Method->getPreviousDecl()->getAccess());
  else
    Method->setAccess(D->getAccess());
  if (FunctionTemplate)
    FunctionTemplate->setAccess(Method->getAccess());

  SemaRef.CheckOverrideControl(Method);

  // If a function is defined as defaulted or deleted, mark it as such now.
  if (D->isExplicitlyDefaulted()) {
    if (SubstDefaultedFunction(Method, D))
      return nullptr;
  }
  if (D->isDeletedAsWritten())
    SemaRef.SetDeclDeleted(Method, Method->getLocation(),
                           D->getDeletedMessage());

  // If this is an explicit specialization, mark the implicitly-instantiated
  // template specialization as being an explicit specialization too.
  // FIXME: Is this necessary?
  if (IsExplicitSpecialization && !isFriend)
    SemaRef.CompleteMemberSpecialization(Method, Previous);

  // If the method is a special member function, we need to mark it as
  // ineligible so that Owner->addDecl() won't mark the class as non trivial.
  // At the end of the class instantiation, we calculate eligibility again and
  // then we adjust trivility if needed.
  // We need this check to happen only after the method parameters are set,
  // because being e.g. a copy constructor depends on the instantiated
  // arguments.
  if (auto *Constructor = dyn_cast<CXXConstructorDecl>(Method)) {
    if (Constructor->isDefaultConstructor() ||
        Constructor->isCopyOrMoveConstructor())
      Method->setIneligibleOrNotSelected(true);
  } else if (Method->isCopyAssignmentOperator() ||
             Method->isMoveAssignmentOperator()) {
    Method->setIneligibleOrNotSelected(true);
  }

  // If there's a function template, let our caller handle it.
  if (FunctionTemplate) {
    // do nothing

  // Don't hide a (potentially) valid declaration with an invalid one.
  } else if (Method->isInvalidDecl() && !Previous.empty()) {
    // do nothing

  // Otherwise, check access to friends and make them visible.
  } else if (isFriend) {
    // We only need to re-check access for methods which we didn't
    // manage to match during parsing.
    if (!D->getPreviousDecl())
      SemaRef.CheckFriendAccess(Method);

    Record->makeDeclVisibleInContext(Method);

  // Otherwise, add the declaration.  We don't need to do this for
  // class-scope specializations because we'll have matched them with
  // the appropriate template.
  } else {
    Owner->addDecl(Method);
  }

  // PR17480: Honor the used attribute to instantiate member function
  // definitions
  if (Method->hasAttr<UsedAttr>()) {
    if (const auto *A = dyn_cast<CXXRecordDecl>(Owner)) {
      SourceLocation Loc;
      if (const MemberSpecializationInfo *MSInfo =
              A->getMemberSpecializationInfo())
        Loc = MSInfo->getPointOfInstantiation();
      else if (const auto *Spec = dyn_cast<ClassTemplateSpecializationDecl>(A))
        Loc = Spec->getPointOfInstantiation();
      SemaRef.MarkFunctionReferenced(Loc, Method);
    }
  }

  return Method;
}

Decl *TemplateDeclInstantiator::VisitCXXConstructorDecl(CXXConstructorDecl *D) {
  return VisitCXXMethodDecl(D);
}

Decl *TemplateDeclInstantiator::VisitCXXDestructorDecl(CXXDestructorDecl *D) {
  return VisitCXXMethodDecl(D);
}

Decl *TemplateDeclInstantiator::VisitCXXConversionDecl(CXXConversionDecl *D) {
  return VisitCXXMethodDecl(D);
}

Decl *TemplateDeclInstantiator::VisitParmVarDecl(ParmVarDecl *D) {
  return SemaRef.SubstParmVarDecl(D, TemplateArgs, /*indexAdjustment*/ 0,
                                  std::nullopt,
                                  /*ExpectParameterPack=*/false);
}

Decl *TemplateDeclInstantiator::VisitTemplateTypeParmDecl(
                                                    TemplateTypeParmDecl *D) {
  assert(D->getTypeForDecl()->isTemplateTypeParmType());

  std::optional<unsigned> NumExpanded;

  if (const TypeConstraint *TC = D->getTypeConstraint()) {
    if (D->isPackExpansion() && !D->isExpandedParameterPack()) {
      assert(TC->getTemplateArgsAsWritten() &&
             "type parameter can only be an expansion when explicit arguments "
             "are specified");
      // The template type parameter pack's type is a pack expansion of types.
      // Determine whether we need to expand this parameter pack into separate
      // types.
      SmallVector<UnexpandedParameterPack, 2> Unexpanded;
      for (auto &ArgLoc : TC->getTemplateArgsAsWritten()->arguments())
        SemaRef.collectUnexpandedParameterPacks(ArgLoc, Unexpanded);

      // Determine whether the set of unexpanded parameter packs can and should
      // be expanded.
      bool Expand = true;
      bool RetainExpansion = false;
      if (SemaRef.CheckParameterPacksForExpansion(
              cast<CXXFoldExpr>(TC->getImmediatelyDeclaredConstraint())
                  ->getEllipsisLoc(),
              SourceRange(TC->getConceptNameLoc(),
                          TC->hasExplicitTemplateArgs() ?
                          TC->getTemplateArgsAsWritten()->getRAngleLoc() :
                          TC->getConceptNameInfo().getEndLoc()),
              Unexpanded, TemplateArgs, Expand, RetainExpansion, NumExpanded))
        return nullptr;
    }
  }

  TemplateTypeParmDecl *Inst = TemplateTypeParmDecl::Create(
      SemaRef.Context, Owner, D->getBeginLoc(), D->getLocation(),
      D->getDepth() - TemplateArgs.getNumSubstitutedLevels(), D->getIndex(),
      D->getIdentifier(), D->wasDeclaredWithTypename(), D->isParameterPack(),
      D->hasTypeConstraint(), NumExpanded);

  Inst->setAccess(AS_public);
  Inst->setImplicit(D->isImplicit());
  if (auto *TC = D->getTypeConstraint()) {
    if (!D->isImplicit()) {
      // Invented template parameter type constraints will be instantiated
      // with the corresponding auto-typed parameter as it might reference
      // other parameters.
      if (SemaRef.SubstTypeConstraint(Inst, TC, TemplateArgs,
                                      EvaluateConstraints))
        return nullptr;
    }
  }
  if (D->hasDefaultArgument() && !D->defaultArgumentWasInherited()) {
    TemplateArgumentLoc Output;
    if (!SemaRef.SubstTemplateArgument(D->getDefaultArgument(), TemplateArgs,
                                       Output))
      Inst->setDefaultArgument(SemaRef.getASTContext(), Output);
  }

  // Introduce this template parameter's instantiation into the instantiation
  // scope.
  SemaRef.CurrentInstantiationScope->InstantiatedLocal(D, Inst);

  return Inst;
}

Decl *TemplateDeclInstantiator::VisitNonTypeTemplateParmDecl(
                                                 NonTypeTemplateParmDecl *D) {
  // Substitute into the type of the non-type template parameter.
  TypeLoc TL = D->getTypeSourceInfo()->getTypeLoc();
  SmallVector<TypeSourceInfo *, 4> ExpandedParameterPackTypesAsWritten;
  SmallVector<QualType, 4> ExpandedParameterPackTypes;
  bool IsExpandedParameterPack = false;
  TypeSourceInfo *DI;
  QualType T;
  bool Invalid = false;

  if (D->isExpandedParameterPack()) {
    // The non-type template parameter pack is an already-expanded pack
    // expansion of types. Substitute into each of the expanded types.
    ExpandedParameterPackTypes.reserve(D->getNumExpansionTypes());
    ExpandedParameterPackTypesAsWritten.reserve(D->getNumExpansionTypes());
    for (unsigned I = 0, N = D->getNumExpansionTypes(); I != N; ++I) {
      TypeSourceInfo *NewDI =
          SemaRef.SubstType(D->getExpansionTypeSourceInfo(I), TemplateArgs,
                            D->getLocation(), D->getDeclName());
      if (!NewDI)
        return nullptr;

      QualType NewT =
          SemaRef.CheckNonTypeTemplateParameterType(NewDI, D->getLocation());
      if (NewT.isNull())
        return nullptr;

      ExpandedParameterPackTypesAsWritten.push_back(NewDI);
      ExpandedParameterPackTypes.push_back(NewT);
    }

    IsExpandedParameterPack = true;
    DI = D->getTypeSourceInfo();
    T = DI->getType();
  } else if (D->isPackExpansion()) {
    // The non-type template parameter pack's type is a pack expansion of types.
    // Determine whether we need to expand this parameter pack into separate
    // types.
    PackExpansionTypeLoc Expansion = TL.castAs<PackExpansionTypeLoc>();
    TypeLoc Pattern = Expansion.getPatternLoc();
    SmallVector<UnexpandedParameterPack, 2> Unexpanded;
    SemaRef.collectUnexpandedParameterPacks(Pattern, Unexpanded);

    // Determine whether the set of unexpanded parameter packs can and should
    // be expanded.
    bool Expand = true;
    bool RetainExpansion = false;
    std::optional<unsigned> OrigNumExpansions =
        Expansion.getTypePtr()->getNumExpansions();
    std::optional<unsigned> NumExpansions = OrigNumExpansions;
    if (SemaRef.CheckParameterPacksForExpansion(Expansion.getEllipsisLoc(),
                                                Pattern.getSourceRange(),
                                                Unexpanded,
                                                TemplateArgs,
                                                Expand, RetainExpansion,
                                                NumExpansions))
      return nullptr;

    if (Expand) {
      for (unsigned I = 0; I != *NumExpansions; ++I) {
        Sema::ArgumentPackSubstitutionIndexRAII SubstIndex(SemaRef, I);
        TypeSourceInfo *NewDI = SemaRef.SubstType(Pattern, TemplateArgs,
                                                  D->getLocation(),
                                                  D->getDeclName());
        if (!NewDI)
          return nullptr;

        QualType NewT =
            SemaRef.CheckNonTypeTemplateParameterType(NewDI, D->getLocation());
        if (NewT.isNull())
          return nullptr;

        ExpandedParameterPackTypesAsWritten.push_back(NewDI);
        ExpandedParameterPackTypes.push_back(NewT);
      }

      // Note that we have an expanded parameter pack. The "type" of this
      // expanded parameter pack is the original expansion type, but callers
      // will end up using the expanded parameter pack types for type-checking.
      IsExpandedParameterPack = true;
      DI = D->getTypeSourceInfo();
      T = DI->getType();
    } else {
      // We cannot fully expand the pack expansion now, so substitute into the
      // pattern and create a new pack expansion type.
      Sema::ArgumentPackSubstitutionIndexRAII SubstIndex(SemaRef, -1);
      TypeSourceInfo *NewPattern = SemaRef.SubstType(Pattern, TemplateArgs,
                                                     D->getLocation(),
                                                     D->getDeclName());
      if (!NewPattern)
        return nullptr;

      SemaRef.CheckNonTypeTemplateParameterType(NewPattern, D->getLocation());
      DI = SemaRef.CheckPackExpansion(NewPattern, Expansion.getEllipsisLoc(),
                                      NumExpansions);
      if (!DI)
        return nullptr;

      T = DI->getType();
    }
  } else {
    // Simple case: substitution into a parameter that is not a parameter pack.
    DI = SemaRef.SubstType(D->getTypeSourceInfo(), TemplateArgs,
                           D->getLocation(), D->getDeclName());
    if (!DI)
      return nullptr;

    // Check that this type is acceptable for a non-type template parameter.
    T = SemaRef.CheckNonTypeTemplateParameterType(DI, D->getLocation());
    if (T.isNull()) {
      T = SemaRef.Context.IntTy;
      Invalid = true;
    }
  }

  NonTypeTemplateParmDecl *Param;
  if (IsExpandedParameterPack)
    Param = NonTypeTemplateParmDecl::Create(
        SemaRef.Context, Owner, D->getInnerLocStart(), D->getLocation(),
        D->getDepth() - TemplateArgs.getNumSubstitutedLevels(),
        D->getPosition(), D->getIdentifier(), T, DI, ExpandedParameterPackTypes,
        ExpandedParameterPackTypesAsWritten);
  else
    Param = NonTypeTemplateParmDecl::Create(
        SemaRef.Context, Owner, D->getInnerLocStart(), D->getLocation(),
        D->getDepth() - TemplateArgs.getNumSubstitutedLevels(),
        D->getPosition(), D->getIdentifier(), T, D->isParameterPack(), DI);

  if (AutoTypeLoc AutoLoc = DI->getTypeLoc().getContainedAutoTypeLoc())
    if (AutoLoc.isConstrained()) {
      SourceLocation EllipsisLoc;
      if (IsExpandedParameterPack)
        EllipsisLoc =
            DI->getTypeLoc().getAs<PackExpansionTypeLoc>().getEllipsisLoc();
      else if (auto *Constraint = dyn_cast_if_present<CXXFoldExpr>(
                   D->getPlaceholderTypeConstraint()))
        EllipsisLoc = Constraint->getEllipsisLoc();
      // Note: We attach the uninstantiated constriant here, so that it can be
      // instantiated relative to the top level, like all our other
      // constraints.
      if (SemaRef.AttachTypeConstraint(AutoLoc, /*NewConstrainedParm=*/Param,
                                       /*OrigConstrainedParm=*/D, EllipsisLoc))
        Invalid = true;
    }

  Param->setAccess(AS_public);
  Param->setImplicit(D->isImplicit());
  if (Invalid)
    Param->setInvalidDecl();

  if (D->hasDefaultArgument() && !D->defaultArgumentWasInherited()) {
    EnterExpressionEvaluationContext ConstantEvaluated(
        SemaRef, Sema::ExpressionEvaluationContext::ConstantEvaluated);
    TemplateArgumentLoc Result;
    if (!SemaRef.SubstTemplateArgument(D->getDefaultArgument(), TemplateArgs,
                                       Result))
      Param->setDefaultArgument(SemaRef.Context, Result);
  }

  // Introduce this template parameter's instantiation into the instantiation
  // scope.
  SemaRef.CurrentInstantiationScope->InstantiatedLocal(D, Param);
  return Param;
}

static void collectUnexpandedParameterPacks(
    Sema &S,
    TemplateParameterList *Params,
    SmallVectorImpl<UnexpandedParameterPack> &Unexpanded) {
  for (const auto &P : *Params) {
    if (P->isTemplateParameterPack())
      continue;
    if (NonTypeTemplateParmDecl *NTTP = dyn_cast<NonTypeTemplateParmDecl>(P))
      S.collectUnexpandedParameterPacks(NTTP->getTypeSourceInfo()->getTypeLoc(),
                                        Unexpanded);
    if (TemplateTemplateParmDecl *TTP = dyn_cast<TemplateTemplateParmDecl>(P))
      collectUnexpandedParameterPacks(S, TTP->getTemplateParameters(),
                                      Unexpanded);
  }
}

Decl *
TemplateDeclInstantiator::VisitTemplateTemplateParmDecl(
                                                  TemplateTemplateParmDecl *D) {
  // Instantiate the template parameter list of the template template parameter.
  TemplateParameterList *TempParams = D->getTemplateParameters();
  TemplateParameterList *InstParams;
  SmallVector<TemplateParameterList*, 8> ExpandedParams;

  bool IsExpandedParameterPack = false;

  if (D->isExpandedParameterPack()) {
    // The template template parameter pack is an already-expanded pack
    // expansion of template parameters. Substitute into each of the expanded
    // parameters.
    ExpandedParams.reserve(D->getNumExpansionTemplateParameters());
    for (unsigned I = 0, N = D->getNumExpansionTemplateParameters();
         I != N; ++I) {
      LocalInstantiationScope Scope(SemaRef);
      TemplateParameterList *Expansion =
        SubstTemplateParams(D->getExpansionTemplateParameters(I));
      if (!Expansion)
        return nullptr;
      ExpandedParams.push_back(Expansion);
    }

    IsExpandedParameterPack = true;
    InstParams = TempParams;
  } else if (D->isPackExpansion()) {
    // The template template parameter pack expands to a pack of template
    // template parameters. Determine whether we need to expand this parameter
    // pack into separate parameters.
    SmallVector<UnexpandedParameterPack, 2> Unexpanded;
    collectUnexpandedParameterPacks(SemaRef, D->getTemplateParameters(),
                                    Unexpanded);

    // Determine whether the set of unexpanded parameter packs can and should
    // be expanded.
    bool Expand = true;
    bool RetainExpansion = false;
    std::optional<unsigned> NumExpansions;
    if (SemaRef.CheckParameterPacksForExpansion(D->getLocation(),
                                                TempParams->getSourceRange(),
                                                Unexpanded,
                                                TemplateArgs,
                                                Expand, RetainExpansion,
                                                NumExpansions))
      return nullptr;

    if (Expand) {
      for (unsigned I = 0; I != *NumExpansions; ++I) {
        Sema::ArgumentPackSubstitutionIndexRAII SubstIndex(SemaRef, I);
        LocalInstantiationScope Scope(SemaRef);
        TemplateParameterList *Expansion = SubstTemplateParams(TempParams);
        if (!Expansion)
          return nullptr;
        ExpandedParams.push_back(Expansion);
      }

      // Note that we have an expanded parameter pack. The "type" of this
      // expanded parameter pack is the original expansion type, but callers
      // will end up using the expanded parameter pack types for type-checking.
      IsExpandedParameterPack = true;
      InstParams = TempParams;
    } else {
      // We cannot fully expand the pack expansion now, so just substitute
      // into the pattern.
      Sema::ArgumentPackSubstitutionIndexRAII SubstIndex(SemaRef, -1);

      LocalInstantiationScope Scope(SemaRef);
      InstParams = SubstTemplateParams(TempParams);
      if (!InstParams)
        return nullptr;
    }
  } else {
    // Perform the actual substitution of template parameters within a new,
    // local instantiation scope.
    LocalInstantiationScope Scope(SemaRef);
    InstParams = SubstTemplateParams(TempParams);
    if (!InstParams)
      return nullptr;
  }

  // Build the template template parameter.
  TemplateTemplateParmDecl *Param;
  if (IsExpandedParameterPack)
    Param = TemplateTemplateParmDecl::Create(
        SemaRef.Context, Owner, D->getLocation(),
        D->getDepth() - TemplateArgs.getNumSubstitutedLevels(),
        D->getPosition(), D->getIdentifier(), D->wasDeclaredWithTypename(),
        InstParams, ExpandedParams);
  else
    Param = TemplateTemplateParmDecl::Create(
        SemaRef.Context, Owner, D->getLocation(),
        D->getDepth() - TemplateArgs.getNumSubstitutedLevels(),
        D->getPosition(), D->isParameterPack(), D->getIdentifier(),
        D->wasDeclaredWithTypename(), InstParams);
  if (D->hasDefaultArgument() && !D->defaultArgumentWasInherited()) {
    NestedNameSpecifierLoc QualifierLoc =
        D->getDefaultArgument().getTemplateQualifierLoc();
    QualifierLoc =
        SemaRef.SubstNestedNameSpecifierLoc(QualifierLoc, TemplateArgs);
    TemplateName TName = SemaRef.SubstTemplateName(
        QualifierLoc, D->getDefaultArgument().getArgument().getAsTemplate(),
        D->getDefaultArgument().getTemplateNameLoc(), TemplateArgs);
    if (!TName.isNull())
      Param->setDefaultArgument(
          SemaRef.Context,
          TemplateArgumentLoc(SemaRef.Context, TemplateArgument(TName),
                              D->getDefaultArgument().getTemplateQualifierLoc(),
                              D->getDefaultArgument().getTemplateNameLoc()));
  }
  Param->setAccess(AS_public);
  Param->setImplicit(D->isImplicit());

  // Introduce this template parameter's instantiation into the instantiation
  // scope.
  SemaRef.CurrentInstantiationScope->InstantiatedLocal(D, Param);

  return Param;
}

Decl *TemplateDeclInstantiator::VisitUsingDirectiveDecl(UsingDirectiveDecl *D) {
  // Using directives are never dependent (and never contain any types or
  // expressions), so they require no explicit instantiation work.

  UsingDirectiveDecl *Inst
    = UsingDirectiveDecl::Create(SemaRef.Context, Owner, D->getLocation(),
                                 D->getNamespaceKeyLocation(),
                                 D->getQualifierLoc(),
                                 D->getIdentLocation(),
                                 D->getNominatedNamespace(),
                                 D->getCommonAncestor());

  // Add the using directive to its declaration context
  // only if this is not a function or method.
  if (!Owner->isFunctionOrMethod())
    Owner->addDecl(Inst);

  return Inst;
}

Decl *TemplateDeclInstantiator::VisitBaseUsingDecls(BaseUsingDecl *D,
                                                    BaseUsingDecl *Inst,
                                                    LookupResult *Lookup) {

  bool isFunctionScope = Owner->isFunctionOrMethod();

  for (auto *Shadow : D->shadows()) {
    // FIXME: UsingShadowDecl doesn't preserve its immediate target, so
    // reconstruct it in the case where it matters. Hm, can we extract it from
    // the DeclSpec when parsing and save it in the UsingDecl itself?
    NamedDecl *OldTarget = Shadow->getTargetDecl();
    if (auto *CUSD = dyn_cast<ConstructorUsingShadowDecl>(Shadow))
      if (auto *BaseShadow = CUSD->getNominatedBaseClassShadowDecl())
        OldTarget = BaseShadow;

    NamedDecl *InstTarget = nullptr;
    if (auto *EmptyD =
            dyn_cast<UnresolvedUsingIfExistsDecl>(Shadow->getTargetDecl())) {
      InstTarget = UnresolvedUsingIfExistsDecl::Create(
          SemaRef.Context, Owner, EmptyD->getLocation(), EmptyD->getDeclName());
    } else {
      InstTarget = cast_or_null<NamedDecl>(SemaRef.FindInstantiatedDecl(
          Shadow->getLocation(), OldTarget, TemplateArgs));
    }
    if (!InstTarget)
      return nullptr;

    UsingShadowDecl *PrevDecl = nullptr;
    if (Lookup &&
        SemaRef.CheckUsingShadowDecl(Inst, InstTarget, *Lookup, PrevDecl))
      continue;

    if (UsingShadowDecl *OldPrev = getPreviousDeclForInstantiation(Shadow))
      PrevDecl = cast_or_null<UsingShadowDecl>(SemaRef.FindInstantiatedDecl(
          Shadow->getLocation(), OldPrev, TemplateArgs));

    UsingShadowDecl *InstShadow = SemaRef.BuildUsingShadowDecl(
        /*Scope*/ nullptr, Inst, InstTarget, PrevDecl);
    SemaRef.Context.setInstantiatedFromUsingShadowDecl(InstShadow, Shadow);

    if (isFunctionScope)
      SemaRef.CurrentInstantiationScope->InstantiatedLocal(Shadow, InstShadow);
  }

  return Inst;
}

Decl *TemplateDeclInstantiator::VisitUsingDecl(UsingDecl *D) {

  // The nested name specifier may be dependent, for example
  //     template <typename T> struct t {
  //       struct s1 { T f1(); };
  //       struct s2 : s1 { using s1::f1; };
  //     };
  //     template struct t<int>;
  // Here, in using s1::f1, s1 refers to t<T>::s1;
  // we need to substitute for t<int>::s1.
  NestedNameSpecifierLoc QualifierLoc
    = SemaRef.SubstNestedNameSpecifierLoc(D->getQualifierLoc(),
                                          TemplateArgs);
  if (!QualifierLoc)
    return nullptr;

  // For an inheriting constructor declaration, the name of the using
  // declaration is the name of a constructor in this class, not in the
  // base class.
  DeclarationNameInfo NameInfo = D->getNameInfo();
  if (NameInfo.getName().getNameKind() == DeclarationName::CXXConstructorName)
    if (auto *RD = dyn_cast<CXXRecordDecl>(SemaRef.CurContext))
      NameInfo.setName(SemaRef.Context.DeclarationNames.getCXXConstructorName(
          SemaRef.Context.getCanonicalType(SemaRef.Context.getRecordType(RD))));

  // We only need to do redeclaration lookups if we're in a class scope (in
  // fact, it's not really even possible in non-class scopes).
  bool CheckRedeclaration = Owner->isRecord();
  LookupResult Prev(SemaRef, NameInfo, Sema::LookupUsingDeclName,
                    RedeclarationKind::ForVisibleRedeclaration);

  UsingDecl *NewUD = UsingDecl::Create(SemaRef.Context, Owner,
                                       D->getUsingLoc(),
                                       QualifierLoc,
                                       NameInfo,
                                       D->hasTypename());

  CXXScopeSpec SS;
  SS.Adopt(QualifierLoc);
  if (CheckRedeclaration) {
    Prev.setHideTags(false);
    SemaRef.LookupQualifiedName(Prev, Owner);

    // Check for invalid redeclarations.
    if (SemaRef.CheckUsingDeclRedeclaration(D->getUsingLoc(),
                                            D->hasTypename(), SS,
                                            D->getLocation(), Prev))
      NewUD->setInvalidDecl();
  }

  if (!NewUD->isInvalidDecl() &&
      SemaRef.CheckUsingDeclQualifier(D->getUsingLoc(), D->hasTypename(), SS,
                                      NameInfo, D->getLocation(), nullptr, D))
    NewUD->setInvalidDecl();

  SemaRef.Context.setInstantiatedFromUsingDecl(NewUD, D);
  NewUD->setAccess(D->getAccess());
  Owner->addDecl(NewUD);

  // Don't process the shadow decls for an invalid decl.
  if (NewUD->isInvalidDecl())
    return NewUD;

  // If the using scope was dependent, or we had dependent bases, we need to
  // recheck the inheritance
  if (NameInfo.getName().getNameKind() == DeclarationName::CXXConstructorName)
    SemaRef.CheckInheritingConstructorUsingDecl(NewUD);

  return VisitBaseUsingDecls(D, NewUD, CheckRedeclaration ? &Prev : nullptr);
}

Decl *TemplateDeclInstantiator::VisitUsingEnumDecl(UsingEnumDecl *D) {
  // Cannot be a dependent type, but still could be an instantiation
  EnumDecl *EnumD = cast_or_null<EnumDecl>(SemaRef.FindInstantiatedDecl(
      D->getLocation(), D->getEnumDecl(), TemplateArgs));

  if (SemaRef.RequireCompleteEnumDecl(EnumD, EnumD->getLocation()))
    return nullptr;

  TypeSourceInfo *TSI = SemaRef.SubstType(D->getEnumType(), TemplateArgs,
                                          D->getLocation(), D->getDeclName());

  if (!TSI)
    return nullptr;

  UsingEnumDecl *NewUD =
      UsingEnumDecl::Create(SemaRef.Context, Owner, D->getUsingLoc(),
                            D->getEnumLoc(), D->getLocation(), TSI);

  SemaRef.Context.setInstantiatedFromUsingEnumDecl(NewUD, D);
  NewUD->setAccess(D->getAccess());
  Owner->addDecl(NewUD);

  // Don't process the shadow decls for an invalid decl.
  if (NewUD->isInvalidDecl())
    return NewUD;

  // We don't have to recheck for duplication of the UsingEnumDecl itself, as it
  // cannot be dependent, and will therefore have been checked during template
  // definition.

  return VisitBaseUsingDecls(D, NewUD, nullptr);
}

Decl *TemplateDeclInstantiator::VisitUsingShadowDecl(UsingShadowDecl *D) {
  // Ignore these;  we handle them in bulk when processing the UsingDecl.
  return nullptr;
}

Decl *TemplateDeclInstantiator::VisitConstructorUsingShadowDecl(
    ConstructorUsingShadowDecl *D) {
  // Ignore these;  we handle them in bulk when processing the UsingDecl.
  return nullptr;
}

template <typename T>
Decl *TemplateDeclInstantiator::instantiateUnresolvedUsingDecl(
    T *D, bool InstantiatingPackElement) {
  // If this is a pack expansion, expand it now.
  if (D->isPackExpansion() && !InstantiatingPackElement) {
    SmallVector<UnexpandedParameterPack, 2> Unexpanded;
    SemaRef.collectUnexpandedParameterPacks(D->getQualifierLoc(), Unexpanded);
    SemaRef.collectUnexpandedParameterPacks(D->getNameInfo(), Unexpanded);

    // Determine whether the set of unexpanded parameter packs can and should
    // be expanded.
    bool Expand = true;
    bool RetainExpansion = false;
    std::optional<unsigned> NumExpansions;
    if (SemaRef.CheckParameterPacksForExpansion(
          D->getEllipsisLoc(), D->getSourceRange(), Unexpanded, TemplateArgs,
            Expand, RetainExpansion, NumExpansions))
      return nullptr;

    // This declaration cannot appear within a function template signature,
    // so we can't have a partial argument list for a parameter pack.
    assert(!RetainExpansion &&
           "should never need to retain an expansion for UsingPackDecl");

    if (!Expand) {
      // We cannot fully expand the pack expansion now, so substitute into the
      // pattern and create a new pack expansion.
      Sema::ArgumentPackSubstitutionIndexRAII SubstIndex(SemaRef, -1);
      return instantiateUnresolvedUsingDecl(D, true);
    }

    // Within a function, we don't have any normal way to check for conflicts
    // between shadow declarations from different using declarations in the
    // same pack expansion, but this is always ill-formed because all expansions
    // must produce (conflicting) enumerators.
    //
    // Sadly we can't just reject this in the template definition because it
    // could be valid if the pack is empty or has exactly one expansion.
    if (D->getDeclContext()->isFunctionOrMethod() && *NumExpansions > 1) {
      SemaRef.Diag(D->getEllipsisLoc(),
                   diag::err_using_decl_redeclaration_expansion);
      return nullptr;
    }

    // Instantiate the slices of this pack and build a UsingPackDecl.
    SmallVector<NamedDecl*, 8> Expansions;
    for (unsigned I = 0; I != *NumExpansions; ++I) {
      Sema::ArgumentPackSubstitutionIndexRAII SubstIndex(SemaRef, I);
      Decl *Slice = instantiateUnresolvedUsingDecl(D, true);
      if (!Slice)
        return nullptr;
      // Note that we can still get unresolved using declarations here, if we
      // had arguments for all packs but the pattern also contained other
      // template arguments (this only happens during partial substitution, eg
      // into the body of a generic lambda in a function template).
      Expansions.push_back(cast<NamedDecl>(Slice));
    }

    auto *NewD = SemaRef.BuildUsingPackDecl(D, Expansions);
    if (isDeclWithinFunction(D))
      SemaRef.CurrentInstantiationScope->InstantiatedLocal(D, NewD);
    return NewD;
  }

  UnresolvedUsingTypenameDecl *TD = dyn_cast<UnresolvedUsingTypenameDecl>(D);
  SourceLocation TypenameLoc = TD ? TD->getTypenameLoc() : SourceLocation();

  NestedNameSpecifierLoc QualifierLoc
    = SemaRef.SubstNestedNameSpecifierLoc(D->getQualifierLoc(),
                                          TemplateArgs);
  if (!QualifierLoc)
    return nullptr;

  CXXScopeSpec SS;
  SS.Adopt(QualifierLoc);

  DeclarationNameInfo NameInfo
    = SemaRef.SubstDeclarationNameInfo(D->getNameInfo(), TemplateArgs);

  // Produce a pack expansion only if we're not instantiating a particular
  // slice of a pack expansion.
  bool InstantiatingSlice = D->getEllipsisLoc().isValid() &&
                            SemaRef.ArgumentPackSubstitutionIndex != -1;
  SourceLocation EllipsisLoc =
      InstantiatingSlice ? SourceLocation() : D->getEllipsisLoc();

  bool IsUsingIfExists = D->template hasAttr<UsingIfExistsAttr>();
  NamedDecl *UD = SemaRef.BuildUsingDeclaration(
      /*Scope*/ nullptr, D->getAccess(), D->getUsingLoc(),
      /*HasTypename*/ TD, TypenameLoc, SS, NameInfo, EllipsisLoc,
      ParsedAttributesView(),
      /*IsInstantiation*/ true, IsUsingIfExists);
  if (UD) {
    SemaRef.InstantiateAttrs(TemplateArgs, D, UD);
    SemaRef.Context.setInstantiatedFromUsingDecl(UD, D);
  }

  return UD;
}

Decl *TemplateDeclInstantiator::VisitUnresolvedUsingTypenameDecl(
    UnresolvedUsingTypenameDecl *D) {
  return instantiateUnresolvedUsingDecl(D);
}

Decl *TemplateDeclInstantiator::VisitUnresolvedUsingValueDecl(
    UnresolvedUsingValueDecl *D) {
  return instantiateUnresolvedUsingDecl(D);
}

Decl *TemplateDeclInstantiator::VisitUnresolvedUsingIfExistsDecl(
    UnresolvedUsingIfExistsDecl *D) {
  llvm_unreachable("referring to unresolved decl out of UsingShadowDecl");
}

Decl *TemplateDeclInstantiator::VisitUsingPackDecl(UsingPackDecl *D) {
  SmallVector<NamedDecl*, 8> Expansions;
  for (auto *UD : D->expansions()) {
    if (NamedDecl *NewUD =
            SemaRef.FindInstantiatedDecl(D->getLocation(), UD, TemplateArgs))
      Expansions.push_back(NewUD);
    else
      return nullptr;
  }

  auto *NewD = SemaRef.BuildUsingPackDecl(D, Expansions);
  if (isDeclWithinFunction(D))
    SemaRef.CurrentInstantiationScope->InstantiatedLocal(D, NewD);
  return NewD;
}

Decl *TemplateDeclInstantiator::VisitOMPThreadPrivateDecl(
                                     OMPThreadPrivateDecl *D) {
  SmallVector<Expr *, 5> Vars;
  for (auto *I : D->varlists()) {
    Expr *Var = SemaRef.SubstExpr(I, TemplateArgs).get();
    assert(isa<DeclRefExpr>(Var) && "threadprivate arg is not a DeclRefExpr");
    Vars.push_back(Var);
  }

  OMPThreadPrivateDecl *TD =
      SemaRef.OpenMP().CheckOMPThreadPrivateDecl(D->getLocation(), Vars);

  TD->setAccess(AS_public);
  Owner->addDecl(TD);

  return TD;
}

Decl *TemplateDeclInstantiator::VisitOMPAllocateDecl(OMPAllocateDecl *D) {
  SmallVector<Expr *, 5> Vars;
  for (auto *I : D->varlists()) {
    Expr *Var = SemaRef.SubstExpr(I, TemplateArgs).get();
    assert(isa<DeclRefExpr>(Var) && "allocate arg is not a DeclRefExpr");
    Vars.push_back(Var);
  }
  SmallVector<OMPClause *, 4> Clauses;
  // Copy map clauses from the original mapper.
  for (OMPClause *C : D->clauselists()) {
    OMPClause *IC = nullptr;
    if (auto *AC = dyn_cast<OMPAllocatorClause>(C)) {
      ExprResult NewE = SemaRef.SubstExpr(AC->getAllocator(), TemplateArgs);
      if (!NewE.isUsable())
        continue;
      IC = SemaRef.OpenMP().ActOnOpenMPAllocatorClause(
          NewE.get(), AC->getBeginLoc(), AC->getLParenLoc(), AC->getEndLoc());
    } else if (auto *AC = dyn_cast<OMPAlignClause>(C)) {
      ExprResult NewE = SemaRef.SubstExpr(AC->getAlignment(), TemplateArgs);
      if (!NewE.isUsable())
        continue;
      IC = SemaRef.OpenMP().ActOnOpenMPAlignClause(
          NewE.get(), AC->getBeginLoc(), AC->getLParenLoc(), AC->getEndLoc());
      // If align clause value ends up being invalid, this can end up null.
      if (!IC)
        continue;
    }
    Clauses.push_back(IC);
  }

  Sema::DeclGroupPtrTy Res = SemaRef.OpenMP().ActOnOpenMPAllocateDirective(
      D->getLocation(), Vars, Clauses, Owner);
  if (Res.get().isNull())
    return nullptr;
  return Res.get().getSingleDecl();
}

Decl *TemplateDeclInstantiator::VisitOMPRequiresDecl(OMPRequiresDecl *D) {
  llvm_unreachable(
      "Requires directive cannot be instantiated within a dependent context");
}

Decl *TemplateDeclInstantiator::VisitOMPDeclareReductionDecl(
    OMPDeclareReductionDecl *D) {
  // Instantiate type and check if it is allowed.
  const bool RequiresInstantiation =
      D->getType()->isDependentType() ||
      D->getType()->isInstantiationDependentType() ||
      D->getType()->containsUnexpandedParameterPack();
  QualType SubstReductionType;
  if (RequiresInstantiation) {
    SubstReductionType = SemaRef.OpenMP().ActOnOpenMPDeclareReductionType(
        D->getLocation(),
        ParsedType::make(SemaRef.SubstType(
            D->getType(), TemplateArgs, D->getLocation(), DeclarationName())));
  } else {
    SubstReductionType = D->getType();
  }
  if (SubstReductionType.isNull())
    return nullptr;
  Expr *Combiner = D->getCombiner();
  Expr *Init = D->getInitializer();
  bool IsCorrect = true;
  // Create instantiated copy.
  std::pair<QualType, SourceLocation> ReductionTypes[] = {
      std::make_pair(SubstReductionType, D->getLocation())};
  auto *PrevDeclInScope = D->getPrevDeclInScope();
  if (PrevDeclInScope && !PrevDeclInScope->isInvalidDecl()) {
    PrevDeclInScope = cast<OMPDeclareReductionDecl>(
        SemaRef.CurrentInstantiationScope->findInstantiationOf(PrevDeclInScope)
            ->get<Decl *>());
  }
  auto DRD = SemaRef.OpenMP().ActOnOpenMPDeclareReductionDirectiveStart(
      /*S=*/nullptr, Owner, D->getDeclName(), ReductionTypes, D->getAccess(),
      PrevDeclInScope);
  auto *NewDRD = cast<OMPDeclareReductionDecl>(DRD.get().getSingleDecl());
  SemaRef.CurrentInstantiationScope->InstantiatedLocal(D, NewDRD);
  Expr *SubstCombiner = nullptr;
  Expr *SubstInitializer = nullptr;
  // Combiners instantiation sequence.
  if (Combiner) {
    SemaRef.OpenMP().ActOnOpenMPDeclareReductionCombinerStart(
        /*S=*/nullptr, NewDRD);
    SemaRef.CurrentInstantiationScope->InstantiatedLocal(
        cast<DeclRefExpr>(D->getCombinerIn())->getDecl(),
        cast<DeclRefExpr>(NewDRD->getCombinerIn())->getDecl());
    SemaRef.CurrentInstantiationScope->InstantiatedLocal(
        cast<DeclRefExpr>(D->getCombinerOut())->getDecl(),
        cast<DeclRefExpr>(NewDRD->getCombinerOut())->getDecl());
    auto *ThisContext = dyn_cast_or_null<CXXRecordDecl>(Owner);
    Sema::CXXThisScopeRAII ThisScope(SemaRef, ThisContext, Qualifiers(),
                                     ThisContext);
    SubstCombiner = SemaRef.SubstExpr(Combiner, TemplateArgs).get();
    SemaRef.OpenMP().ActOnOpenMPDeclareReductionCombinerEnd(NewDRD,
                                                            SubstCombiner);
  }
  // Initializers instantiation sequence.
  if (Init) {
    VarDecl *OmpPrivParm =
        SemaRef.OpenMP().ActOnOpenMPDeclareReductionInitializerStart(
            /*S=*/nullptr, NewDRD);
    SemaRef.CurrentInstantiationScope->InstantiatedLocal(
        cast<DeclRefExpr>(D->getInitOrig())->getDecl(),
        cast<DeclRefExpr>(NewDRD->getInitOrig())->getDecl());
    SemaRef.CurrentInstantiationScope->InstantiatedLocal(
        cast<DeclRefExpr>(D->getInitPriv())->getDecl(),
        cast<DeclRefExpr>(NewDRD->getInitPriv())->getDecl());
    if (D->getInitializerKind() == OMPDeclareReductionInitKind::Call) {
      SubstInitializer = SemaRef.SubstExpr(Init, TemplateArgs).get();
    } else {
      auto *OldPrivParm =
          cast<VarDecl>(cast<DeclRefExpr>(D->getInitPriv())->getDecl());
      IsCorrect = IsCorrect && OldPrivParm->hasInit();
      if (IsCorrect)
        SemaRef.InstantiateVariableInitializer(OmpPrivParm, OldPrivParm,
                                               TemplateArgs);
    }
    SemaRef.OpenMP().ActOnOpenMPDeclareReductionInitializerEnd(
        NewDRD, SubstInitializer, OmpPrivParm);
  }
  IsCorrect = IsCorrect && SubstCombiner &&
              (!Init ||
               (D->getInitializerKind() == OMPDeclareReductionInitKind::Call &&
                SubstInitializer) ||
               (D->getInitializerKind() != OMPDeclareReductionInitKind::Call &&
                !SubstInitializer));

  (void)SemaRef.OpenMP().ActOnOpenMPDeclareReductionDirectiveEnd(
      /*S=*/nullptr, DRD, IsCorrect && !D->isInvalidDecl());

  return NewDRD;
}

Decl *
TemplateDeclInstantiator::VisitOMPDeclareMapperDecl(OMPDeclareMapperDecl *D) {
  // Instantiate type and check if it is allowed.
  const bool RequiresInstantiation =
      D->getType()->isDependentType() ||
      D->getType()->isInstantiationDependentType() ||
      D->getType()->containsUnexpandedParameterPack();
  QualType SubstMapperTy;
  DeclarationName VN = D->getVarName();
  if (RequiresInstantiation) {
    SubstMapperTy = SemaRef.OpenMP().ActOnOpenMPDeclareMapperType(
        D->getLocation(),
        ParsedType::make(SemaRef.SubstType(D->getType(), TemplateArgs,
                                           D->getLocation(), VN)));
  } else {
    SubstMapperTy = D->getType();
  }
  if (SubstMapperTy.isNull())
    return nullptr;
  // Create an instantiated copy of mapper.
  auto *PrevDeclInScope = D->getPrevDeclInScope();
  if (PrevDeclInScope && !PrevDeclInScope->isInvalidDecl()) {
    PrevDeclInScope = cast<OMPDeclareMapperDecl>(
        SemaRef.CurrentInstantiationScope->findInstantiationOf(PrevDeclInScope)
            ->get<Decl *>());
  }
  bool IsCorrect = true;
  SmallVector<OMPClause *, 6> Clauses;
  // Instantiate the mapper variable.
  DeclarationNameInfo DirName;
  SemaRef.OpenMP().StartOpenMPDSABlock(llvm::omp::OMPD_declare_mapper, DirName,
                                       /*S=*/nullptr,
                                       (*D->clauselist_begin())->getBeginLoc());
  ExprResult MapperVarRef =
      SemaRef.OpenMP().ActOnOpenMPDeclareMapperDirectiveVarDecl(
          /*S=*/nullptr, SubstMapperTy, D->getLocation(), VN);
  SemaRef.CurrentInstantiationScope->InstantiatedLocal(
      cast<DeclRefExpr>(D->getMapperVarRef())->getDecl(),
      cast<DeclRefExpr>(MapperVarRef.get())->getDecl());
  auto *ThisContext = dyn_cast_or_null<CXXRecordDecl>(Owner);
  Sema::CXXThisScopeRAII ThisScope(SemaRef, ThisContext, Qualifiers(),
                                   ThisContext);
  // Instantiate map clauses.
  for (OMPClause *C : D->clauselists()) {
    auto *OldC = cast<OMPMapClause>(C);
    SmallVector<Expr *, 4> NewVars;
    for (Expr *OE : OldC->varlists()) {
      Expr *NE = SemaRef.SubstExpr(OE, TemplateArgs).get();
      if (!NE) {
        IsCorrect = false;
        break;
      }
      NewVars.push_back(NE);
    }
    if (!IsCorrect)
      break;
    NestedNameSpecifierLoc NewQualifierLoc =
        SemaRef.SubstNestedNameSpecifierLoc(OldC->getMapperQualifierLoc(),
                                            TemplateArgs);
    CXXScopeSpec SS;
    SS.Adopt(NewQualifierLoc);
    DeclarationNameInfo NewNameInfo =
        SemaRef.SubstDeclarationNameInfo(OldC->getMapperIdInfo(), TemplateArgs);
    OMPVarListLocTy Locs(OldC->getBeginLoc(), OldC->getLParenLoc(),
                         OldC->getEndLoc());
    OMPClause *NewC = SemaRef.OpenMP().ActOnOpenMPMapClause(
        OldC->getIteratorModifier(), OldC->getMapTypeModifiers(),
        OldC->getMapTypeModifiersLoc(), SS, NewNameInfo, OldC->getMapType(),
        OldC->isImplicitMapType(), OldC->getMapLoc(), OldC->getColonLoc(),
        NewVars, Locs);
    Clauses.push_back(NewC);
  }
  SemaRef.OpenMP().EndOpenMPDSABlock(nullptr);
  if (!IsCorrect)
    return nullptr;
  Sema::DeclGroupPtrTy DG = SemaRef.OpenMP().ActOnOpenMPDeclareMapperDirective(
      /*S=*/nullptr, Owner, D->getDeclName(), SubstMapperTy, D->getLocation(),
      VN, D->getAccess(), MapperVarRef.get(), Clauses, PrevDeclInScope);
  Decl *NewDMD = DG.get().getSingleDecl();
  SemaRef.CurrentInstantiationScope->InstantiatedLocal(D, NewDMD);
  return NewDMD;
}

Decl *TemplateDeclInstantiator::VisitOMPCapturedExprDecl(
    OMPCapturedExprDecl * /*D*/) {
  llvm_unreachable("Should not be met in templates");
}

Decl *TemplateDeclInstantiator::VisitFunctionDecl(FunctionDecl *D) {
  return VisitFunctionDecl(D, nullptr);
}

Decl *
TemplateDeclInstantiator::VisitCXXDeductionGuideDecl(CXXDeductionGuideDecl *D) {
  Decl *Inst = VisitFunctionDecl(D, nullptr);
  if (Inst && !D->getDescribedFunctionTemplate())
    Owner->addDecl(Inst);
  return Inst;
}

Decl *TemplateDeclInstantiator::VisitCXXMethodDecl(CXXMethodDecl *D) {
  return VisitCXXMethodDecl(D, nullptr);
}

Decl *TemplateDeclInstantiator::VisitRecordDecl(RecordDecl *D) {
  llvm_unreachable("There are only CXXRecordDecls in C++");
}

Decl *
TemplateDeclInstantiator::VisitClassTemplateSpecializationDecl(
    ClassTemplateSpecializationDecl *D) {
  // As a MS extension, we permit class-scope explicit specialization
  // of member class templates.
  ClassTemplateDecl *ClassTemplate = D->getSpecializedTemplate();
  assert(ClassTemplate->getDeclContext()->isRecord() &&
         D->getTemplateSpecializationKind() == TSK_ExplicitSpecialization &&
         "can only instantiate an explicit specialization "
         "for a member class template");

  // Lookup the already-instantiated declaration in the instantiation
  // of the class template.
  ClassTemplateDecl *InstClassTemplate =
      cast_or_null<ClassTemplateDecl>(SemaRef.FindInstantiatedDecl(
          D->getLocation(), ClassTemplate, TemplateArgs));
  if (!InstClassTemplate)
    return nullptr;

  // Substitute into the template arguments of the class template explicit
  // specialization.
  TemplateArgumentListInfo InstTemplateArgs;
  if (const ASTTemplateArgumentListInfo *TemplateArgsInfo =
          D->getTemplateArgsAsWritten()) {
    InstTemplateArgs.setLAngleLoc(TemplateArgsInfo->getLAngleLoc());
    InstTemplateArgs.setRAngleLoc(TemplateArgsInfo->getRAngleLoc());

    if (SemaRef.SubstTemplateArguments(TemplateArgsInfo->arguments(),
                                       TemplateArgs, InstTemplateArgs))
      return nullptr;
  }

  // Check that the template argument list is well-formed for this
  // class template.
  SmallVector<TemplateArgument, 4> SugaredConverted, CanonicalConverted;
  if (SemaRef.CheckTemplateArgumentList(InstClassTemplate, D->getLocation(),
                                        InstTemplateArgs, false,
                                        SugaredConverted, CanonicalConverted,
                                        /*UpdateArgsWithConversions=*/true))
    return nullptr;

  // Figure out where to insert this class template explicit specialization
  // in the member template's set of class template explicit specializations.
  void *InsertPos = nullptr;
  ClassTemplateSpecializationDecl *PrevDecl =
      InstClassTemplate->findSpecialization(CanonicalConverted, InsertPos);

  // Check whether we've already seen a conflicting instantiation of this
  // declaration (for instance, if there was a prior implicit instantiation).
  bool Ignored;
  if (PrevDecl &&
      SemaRef.CheckSpecializationInstantiationRedecl(D->getLocation(),
                                                     D->getSpecializationKind(),
                                                     PrevDecl,
                                                     PrevDecl->getSpecializationKind(),
                                                     PrevDecl->getPointOfInstantiation(),
                                                     Ignored))
    return nullptr;

  // If PrevDecl was a definition and D is also a definition, diagnose.
  // This happens in cases like:
  //
  //   template<typename T, typename U>
  //   struct Outer {
  //     template<typename X> struct Inner;
  //     template<> struct Inner<T> {};
  //     template<> struct Inner<U> {};
  //   };
  //
  //   Outer<int, int> outer; // error: the explicit specializations of Inner
  //                          // have the same signature.
  if (PrevDecl && PrevDecl->getDefinition() &&
      D->isThisDeclarationADefinition()) {
    SemaRef.Diag(D->getLocation(), diag::err_redefinition) << PrevDecl;
    SemaRef.Diag(PrevDecl->getDefinition()->getLocation(),
                 diag::note_previous_definition);
    return nullptr;
  }

  // Create the class template partial specialization declaration.
  ClassTemplateSpecializationDecl *InstD =
      ClassTemplateSpecializationDecl::Create(
          SemaRef.Context, D->getTagKind(), Owner, D->getBeginLoc(),
          D->getLocation(), InstClassTemplate, CanonicalConverted, PrevDecl);
  InstD->setTemplateArgsAsWritten(InstTemplateArgs);

  // Add this partial specialization to the set of class template partial
  // specializations.
  if (!PrevDecl)
    InstClassTemplate->AddSpecialization(InstD, InsertPos);

  // Substitute the nested name specifier, if any.
  if (SubstQualifier(D, InstD))
    return nullptr;

  InstD->setAccess(D->getAccess());
  InstD->setInstantiationOfMemberClass(D, TSK_ImplicitInstantiation);
  InstD->setSpecializationKind(D->getSpecializationKind());
  InstD->setExternKeywordLoc(D->getExternKeywordLoc());
  InstD->setTemplateKeywordLoc(D->getTemplateKeywordLoc());

  Owner->addDecl(InstD);

  // Instantiate the members of the class-scope explicit specialization eagerly.
  // We don't have support for lazy instantiation of an explicit specialization
  // yet, and MSVC eagerly instantiates in this case.
  // FIXME: This is wrong in standard C++.
  if (D->isThisDeclarationADefinition() &&
      SemaRef.InstantiateClass(D->getLocation(), InstD, D, TemplateArgs,
                               TSK_ImplicitInstantiation,
                               /*Complain=*/true))
    return nullptr;

  return InstD;
}

Decl *TemplateDeclInstantiator::VisitVarTemplateSpecializationDecl(
    VarTemplateSpecializationDecl *D) {

  TemplateArgumentListInfo VarTemplateArgsInfo;
  VarTemplateDecl *VarTemplate = D->getSpecializedTemplate();
  assert(VarTemplate &&
         "A template specialization without specialized template?");

  VarTemplateDecl *InstVarTemplate =
      cast_or_null<VarTemplateDecl>(SemaRef.FindInstantiatedDecl(
          D->getLocation(), VarTemplate, TemplateArgs));
  if (!InstVarTemplate)
    return nullptr;

  // Substitute the current template arguments.
  if (const ASTTemplateArgumentListInfo *TemplateArgsInfo =
          D->getTemplateArgsAsWritten()) {
    VarTemplateArgsInfo.setLAngleLoc(TemplateArgsInfo->getLAngleLoc());
    VarTemplateArgsInfo.setRAngleLoc(TemplateArgsInfo->getRAngleLoc());

    if (SemaRef.SubstTemplateArguments(TemplateArgsInfo->arguments(),
                                       TemplateArgs, VarTemplateArgsInfo))
      return nullptr;
  }

  // Check that the template argument list is well-formed for this template.
  SmallVector<TemplateArgument, 4> SugaredConverted, CanonicalConverted;
  if (SemaRef.CheckTemplateArgumentList(InstVarTemplate, D->getLocation(),
                                        VarTemplateArgsInfo, false,
                                        SugaredConverted, CanonicalConverted,
                                        /*UpdateArgsWithConversions=*/true))
    return nullptr;

  // Check whether we've already seen a declaration of this specialization.
  void *InsertPos = nullptr;
  VarTemplateSpecializationDecl *PrevDecl =
      InstVarTemplate->findSpecialization(CanonicalConverted, InsertPos);

  // Check whether we've already seen a conflicting instantiation of this
  // declaration (for instance, if there was a prior implicit instantiation).
  bool Ignored;
  if (PrevDecl && SemaRef.CheckSpecializationInstantiationRedecl(
                      D->getLocation(), D->getSpecializationKind(), PrevDecl,
                      PrevDecl->getSpecializationKind(),
                      PrevDecl->getPointOfInstantiation(), Ignored))
    return nullptr;

  return VisitVarTemplateSpecializationDecl(
      InstVarTemplate, D, VarTemplateArgsInfo, CanonicalConverted, PrevDecl);
}

Decl *TemplateDeclInstantiator::VisitVarTemplateSpecializationDecl(
    VarTemplateDecl *VarTemplate, VarDecl *D,
    const TemplateArgumentListInfo &TemplateArgsInfo,
    ArrayRef<TemplateArgument> Converted,
    VarTemplateSpecializationDecl *PrevDecl) {

  // Do substitution on the type of the declaration
  TypeSourceInfo *DI =
      SemaRef.SubstType(D->getTypeSourceInfo(), TemplateArgs,
                        D->getTypeSpecStartLoc(), D->getDeclName());
  if (!DI)
    return nullptr;

  if (DI->getType()->isFunctionType()) {
    SemaRef.Diag(D->getLocation(), diag::err_variable_instantiates_to_function)
        << D->isStaticDataMember() << DI->getType();
    return nullptr;
  }

  // Build the instantiated declaration
  VarTemplateSpecializationDecl *Var = VarTemplateSpecializationDecl::Create(
      SemaRef.Context, Owner, D->getInnerLocStart(), D->getLocation(),
      VarTemplate, DI->getType(), DI, D->getStorageClass(), Converted);
  Var->setTemplateArgsAsWritten(TemplateArgsInfo);
  if (!PrevDecl) {
    void *InsertPos = nullptr;
    VarTemplate->findSpecialization(Converted, InsertPos);
    VarTemplate->AddSpecialization(Var, InsertPos);
  }

  if (SemaRef.getLangOpts().OpenCL)
    SemaRef.deduceOpenCLAddressSpace(Var);

  // Substitute the nested name specifier, if any.
  if (SubstQualifier(D, Var))
    return nullptr;

  SemaRef.BuildVariableInstantiation(Var, D, TemplateArgs, LateAttrs, Owner,
                                     StartingScope, false, PrevDecl);

  return Var;
}

Decl *TemplateDeclInstantiator::VisitObjCAtDefsFieldDecl(ObjCAtDefsFieldDecl *D) {
  llvm_unreachable("@defs is not supported in Objective-C++");
}

Decl *TemplateDeclInstantiator::VisitFriendTemplateDecl(FriendTemplateDecl *D) {
  // FIXME: We need to be able to instantiate FriendTemplateDecls.
  unsigned DiagID = SemaRef.getDiagnostics().getCustomDiagID(
                                               DiagnosticsEngine::Error,
                                               "cannot instantiate %0 yet");
  SemaRef.Diag(D->getLocation(), DiagID)
    << D->getDeclKindName();

  return nullptr;
}

Decl *TemplateDeclInstantiator::VisitConceptDecl(ConceptDecl *D) {
  llvm_unreachable("Concept definitions cannot reside inside a template");
}

Decl *TemplateDeclInstantiator::VisitImplicitConceptSpecializationDecl(
    ImplicitConceptSpecializationDecl *D) {
  llvm_unreachable("Concept specializations cannot reside inside a template");
}

Decl *
TemplateDeclInstantiator::VisitRequiresExprBodyDecl(RequiresExprBodyDecl *D) {
  return RequiresExprBodyDecl::Create(SemaRef.Context, D->getDeclContext(),
                                      D->getBeginLoc());
}

Decl *TemplateDeclInstantiator::VisitDecl(Decl *D) {
  llvm_unreachable("Unexpected decl");
}

Decl *Sema::SubstDecl(Decl *D, DeclContext *Owner,
                      const MultiLevelTemplateArgumentList &TemplateArgs) {
  TemplateDeclInstantiator Instantiator(*this, Owner, TemplateArgs);
  if (D->isInvalidDecl())
    return nullptr;

  Decl *SubstD;
  runWithSufficientStackSpace(D->getLocation(), [&] {
    SubstD = Instantiator.Visit(D);
  });
  return SubstD;
}

void TemplateDeclInstantiator::adjustForRewrite(RewriteKind RK,
                                                FunctionDecl *Orig, QualType &T,
                                                TypeSourceInfo *&TInfo,
                                                DeclarationNameInfo &NameInfo) {
  assert(RK == RewriteKind::RewriteSpaceshipAsEqualEqual);

  // C++2a [class.compare.default]p3:
  //   the return type is replaced with bool
  auto *FPT = T->castAs<FunctionProtoType>();
  T = SemaRef.Context.getFunctionType(
      SemaRef.Context.BoolTy, FPT->getParamTypes(), FPT->getExtProtoInfo());

  // Update the return type in the source info too. The most straightforward
  // way is to create new TypeSourceInfo for the new type. Use the location of
  // the '= default' as the location of the new type.
  //
  // FIXME: Set the correct return type when we initially transform the type,
  // rather than delaying it to now.
  TypeSourceInfo *NewTInfo =
      SemaRef.Context.getTrivialTypeSourceInfo(T, Orig->getEndLoc());
  auto OldLoc = TInfo->getTypeLoc().getAsAdjusted<FunctionProtoTypeLoc>();
  assert(OldLoc && "type of function is not a function type?");
  auto NewLoc = NewTInfo->getTypeLoc().castAs<FunctionProtoTypeLoc>();
  for (unsigned I = 0, N = OldLoc.getNumParams(); I != N; ++I)
    NewLoc.setParam(I, OldLoc.getParam(I));
  TInfo = NewTInfo;

  //   and the declarator-id is replaced with operator==
  NameInfo.setName(
      SemaRef.Context.DeclarationNames.getCXXOperatorName(OO_EqualEqual));
}

FunctionDecl *Sema::SubstSpaceshipAsEqualEqual(CXXRecordDecl *RD,
                                               FunctionDecl *Spaceship) {
  if (Spaceship->isInvalidDecl())
    return nullptr;

  // C++2a [class.compare.default]p3:
  //   an == operator function is declared implicitly [...] with the same
  //   access and function-definition and in the same class scope as the
  //   three-way comparison operator function
  MultiLevelTemplateArgumentList NoTemplateArgs;
  NoTemplateArgs.setKind(TemplateSubstitutionKind::Rewrite);
  NoTemplateArgs.addOuterRetainedLevels(RD->getTemplateDepth());
  TemplateDeclInstantiator Instantiator(*this, RD, NoTemplateArgs);
  Decl *R;
  if (auto *MD = dyn_cast<CXXMethodDecl>(Spaceship)) {
    R = Instantiator.VisitCXXMethodDecl(
        MD, /*TemplateParams=*/nullptr,
        TemplateDeclInstantiator::RewriteKind::RewriteSpaceshipAsEqualEqual);
  } else {
    assert(Spaceship->getFriendObjectKind() &&
           "defaulted spaceship is neither a member nor a friend");

    R = Instantiator.VisitFunctionDecl(
        Spaceship, /*TemplateParams=*/nullptr,
        TemplateDeclInstantiator::RewriteKind::RewriteSpaceshipAsEqualEqual);
    if (!R)
      return nullptr;

    FriendDecl *FD =
        FriendDecl::Create(Context, RD, Spaceship->getLocation(),
                           cast<NamedDecl>(R), Spaceship->getBeginLoc());
    FD->setAccess(AS_public);
    RD->addDecl(FD);
  }
  return cast_or_null<FunctionDecl>(R);
}

/// Instantiates a nested template parameter list in the current
/// instantiation context.
///
/// \param L The parameter list to instantiate
///
/// \returns NULL if there was an error
TemplateParameterList *
TemplateDeclInstantiator::SubstTemplateParams(TemplateParameterList *L) {
  // Get errors for all the parameters before bailing out.
  bool Invalid = false;

  unsigned N = L->size();
  typedef SmallVector<NamedDecl *, 8> ParamVector;
  ParamVector Params;
  Params.reserve(N);
  for (auto &P : *L) {
    NamedDecl *D = cast_or_null<NamedDecl>(Visit(P));
    Params.push_back(D);
    Invalid = Invalid || !D || D->isInvalidDecl();
  }

  // Clean up if we had an error.
  if (Invalid)
    return nullptr;

  Expr *InstRequiresClause = L->getRequiresClause();

  TemplateParameterList *InstL
    = TemplateParameterList::Create(SemaRef.Context, L->getTemplateLoc(),
                                    L->getLAngleLoc(), Params,
                                    L->getRAngleLoc(), InstRequiresClause);
  return InstL;
}

TemplateParameterList *
Sema::SubstTemplateParams(TemplateParameterList *Params, DeclContext *Owner,
                          const MultiLevelTemplateArgumentList &TemplateArgs,
                          bool EvaluateConstraints) {
  TemplateDeclInstantiator Instantiator(*this, Owner, TemplateArgs);
  Instantiator.setEvaluateConstraints(EvaluateConstraints);
  return Instantiator.SubstTemplateParams(Params);
}

/// Instantiate the declaration of a class template partial
/// specialization.
///
/// \param ClassTemplate the (instantiated) class template that is partially
// specialized by the instantiation of \p PartialSpec.
///
/// \param PartialSpec the (uninstantiated) class template partial
/// specialization that we are instantiating.
///
/// \returns The instantiated partial specialization, if successful; otherwise,
/// NULL to indicate an error.
ClassTemplatePartialSpecializationDecl *
TemplateDeclInstantiator::InstantiateClassTemplatePartialSpecialization(
                                            ClassTemplateDecl *ClassTemplate,
                          ClassTemplatePartialSpecializationDecl *PartialSpec) {
  // Create a local instantiation scope for this class template partial
  // specialization, which will contain the instantiations of the template
  // parameters.
  LocalInstantiationScope Scope(SemaRef);

  // Substitute into the template parameters of the class template partial
  // specialization.
  TemplateParameterList *TempParams = PartialSpec->getTemplateParameters();
  TemplateParameterList *InstParams = SubstTemplateParams(TempParams);
  if (!InstParams)
    return nullptr;

  // Substitute into the template arguments of the class template partial
  // specialization.
  const ASTTemplateArgumentListInfo *TemplArgInfo
    = PartialSpec->getTemplateArgsAsWritten();
  TemplateArgumentListInfo InstTemplateArgs(TemplArgInfo->LAngleLoc,
                                            TemplArgInfo->RAngleLoc);
  if (SemaRef.SubstTemplateArguments(TemplArgInfo->arguments(), TemplateArgs,
                                     InstTemplateArgs))
    return nullptr;

  // Check that the template argument list is well-formed for this
  // class template.
  SmallVector<TemplateArgument, 4> SugaredConverted, CanonicalConverted;
  if (SemaRef.CheckTemplateArgumentList(
          ClassTemplate, PartialSpec->getLocation(), InstTemplateArgs,
          /*PartialTemplateArgs=*/false, SugaredConverted, CanonicalConverted))
    return nullptr;

  // Check these arguments are valid for a template partial specialization.
  if (SemaRef.CheckTemplatePartialSpecializationArgs(
          PartialSpec->getLocation(), ClassTemplate, InstTemplateArgs.size(),
          CanonicalConverted))
    return nullptr;

  // Figure out where to insert this class template partial specialization
  // in the member template's set of class template partial specializations.
  void *InsertPos = nullptr;
  ClassTemplateSpecializationDecl *PrevDecl =
      ClassTemplate->findPartialSpecialization(CanonicalConverted, InstParams,
                                               InsertPos);

  // Build the canonical type that describes the converted template
  // arguments of the class template partial specialization.
  QualType CanonType = SemaRef.Context.getTemplateSpecializationType(
      TemplateName(ClassTemplate), CanonicalConverted);

  // Create the class template partial specialization declaration.
  ClassTemplatePartialSpecializationDecl *InstPartialSpec =
      ClassTemplatePartialSpecializationDecl::Create(
          SemaRef.Context, PartialSpec->getTagKind(), Owner,
          PartialSpec->getBeginLoc(), PartialSpec->getLocation(), InstParams,
          ClassTemplate, CanonicalConverted, CanonType,
          /*PrevDecl=*/nullptr);

  InstPartialSpec->setTemplateArgsAsWritten(InstTemplateArgs);

  // Substitute the nested name specifier, if any.
  if (SubstQualifier(PartialSpec, InstPartialSpec))
    return nullptr;

  InstPartialSpec->setInstantiatedFromMember(PartialSpec);

  if (PrevDecl) {
    // We've already seen a partial specialization with the same template
    // parameters and template arguments. This can happen, for example, when
    // substituting the outer template arguments ends up causing two
    // class template partial specializations of a member class template
    // to have identical forms, e.g.,
    //
    //   template<typename T, typename U>
    //   struct Outer {
    //     template<typename X, typename Y> struct Inner;
    //     template<typename Y> struct Inner<T, Y>;
    //     template<typename Y> struct Inner<U, Y>;
    //   };
    //
    //   Outer<int, int> outer; // error: the partial specializations of Inner
    //                          // have the same signature.
    SemaRef.Diag(InstPartialSpec->getLocation(),
                 diag::err_partial_spec_redeclared)
        << InstPartialSpec;
    SemaRef.Diag(PrevDecl->getLocation(), diag::note_prev_partial_spec_here)
      << SemaRef.Context.getTypeDeclType(PrevDecl);
    return nullptr;
  }

  // Check the completed partial specialization.
  SemaRef.CheckTemplatePartialSpecialization(InstPartialSpec);

  // Add this partial specialization to the set of class template partial
  // specializations.
  ClassTemplate->AddPartialSpecialization(InstPartialSpec,
                                          /*InsertPos=*/nullptr);
  return InstPartialSpec;
}

/// Instantiate the declaration of a variable template partial
/// specialization.
///
/// \param VarTemplate the (instantiated) variable template that is partially
/// specialized by the instantiation of \p PartialSpec.
///
/// \param PartialSpec the (uninstantiated) variable template partial
/// specialization that we are instantiating.
///
/// \returns The instantiated partial specialization, if successful; otherwise,
/// NULL to indicate an error.
VarTemplatePartialSpecializationDecl *
TemplateDeclInstantiator::InstantiateVarTemplatePartialSpecialization(
    VarTemplateDecl *VarTemplate,
    VarTemplatePartialSpecializationDecl *PartialSpec) {
  // Create a local instantiation scope for this variable template partial
  // specialization, which will contain the instantiations of the template
  // parameters.
  LocalInstantiationScope Scope(SemaRef);

  // Substitute into the template parameters of the variable template partial
  // specialization.
  TemplateParameterList *TempParams = PartialSpec->getTemplateParameters();
  TemplateParameterList *InstParams = SubstTemplateParams(TempParams);
  if (!InstParams)
    return nullptr;

  // Substitute into the template arguments of the variable template partial
  // specialization.
  const ASTTemplateArgumentListInfo *TemplArgInfo
    = PartialSpec->getTemplateArgsAsWritten();
  TemplateArgumentListInfo InstTemplateArgs(TemplArgInfo->LAngleLoc,
                                            TemplArgInfo->RAngleLoc);
  if (SemaRef.SubstTemplateArguments(TemplArgInfo->arguments(), TemplateArgs,
                                     InstTemplateArgs))
    return nullptr;

  // Check that the template argument list is well-formed for this
  // class template.
  SmallVector<TemplateArgument, 4> SugaredConverted, CanonicalConverted;
  if (SemaRef.CheckTemplateArgumentList(
          VarTemplate, PartialSpec->getLocation(), InstTemplateArgs,
          /*PartialTemplateArgs=*/false, SugaredConverted, CanonicalConverted))
    return nullptr;

  // Check these arguments are valid for a template partial specialization.
  if (SemaRef.CheckTemplatePartialSpecializationArgs(
          PartialSpec->getLocation(), VarTemplate, InstTemplateArgs.size(),
          CanonicalConverted))
    return nullptr;

  // Figure out where to insert this variable template partial specialization
  // in the member template's set of variable template partial specializations.
  void *InsertPos = nullptr;
  VarTemplateSpecializationDecl *PrevDecl =
      VarTemplate->findPartialSpecialization(CanonicalConverted, InstParams,
                                             InsertPos);

  // Do substitution on the type of the declaration
  TypeSourceInfo *DI = SemaRef.SubstType(
      PartialSpec->getTypeSourceInfo(), TemplateArgs,
      PartialSpec->getTypeSpecStartLoc(), PartialSpec->getDeclName());
  if (!DI)
    return nullptr;

  if (DI->getType()->isFunctionType()) {
    SemaRef.Diag(PartialSpec->getLocation(),
                 diag::err_variable_instantiates_to_function)
        << PartialSpec->isStaticDataMember() << DI->getType();
    return nullptr;
  }

  // Create the variable template partial specialization declaration.
  VarTemplatePartialSpecializationDecl *InstPartialSpec =
      VarTemplatePartialSpecializationDecl::Create(
          SemaRef.Context, Owner, PartialSpec->getInnerLocStart(),
          PartialSpec->getLocation(), InstParams, VarTemplate, DI->getType(),
          DI, PartialSpec->getStorageClass(), CanonicalConverted);

  InstPartialSpec->setTemplateArgsAsWritten(InstTemplateArgs);

  // Substitute the nested name specifier, if any.
  if (SubstQualifier(PartialSpec, InstPartialSpec))
    return nullptr;

  InstPartialSpec->setInstantiatedFromMember(PartialSpec);

  if (PrevDecl) {
    // We've already seen a partial specialization with the same template
    // parameters and template arguments. This can happen, for example, when
    // substituting the outer template arguments ends up causing two
    // variable template partial specializations of a member variable template
    // to have identical forms, e.g.,
    //
    //   template<typename T, typename U>
    //   struct Outer {
    //     template<typename X, typename Y> pair<X,Y> p;
    //     template<typename Y> pair<T, Y> p;
    //     template<typename Y> pair<U, Y> p;
    //   };
    //
    //   Outer<int, int> outer; // error: the partial specializations of Inner
    //                          // have the same signature.
    SemaRef.Diag(PartialSpec->getLocation(),
                 diag::err_var_partial_spec_redeclared)
        << InstPartialSpec;
    SemaRef.Diag(PrevDecl->getLocation(),
                 diag::note_var_prev_partial_spec_here);
    return nullptr;
  }
  // Check the completed partial specialization.
  SemaRef.CheckTemplatePartialSpecialization(InstPartialSpec);

  // Add this partial specialization to the set of variable template partial
  // specializations. The instantiation of the initializer is not necessary.
  VarTemplate->AddPartialSpecialization(InstPartialSpec, /*InsertPos=*/nullptr);

  SemaRef.BuildVariableInstantiation(InstPartialSpec, PartialSpec, TemplateArgs,
                                     LateAttrs, Owner, StartingScope);

  return InstPartialSpec;
}

TypeSourceInfo*
TemplateDeclInstantiator::SubstFunctionType(FunctionDecl *D,
                              SmallVectorImpl<ParmVarDecl *> &Params) {
  TypeSourceInfo *OldTInfo = D->getTypeSourceInfo();
  assert(OldTInfo && "substituting function without type source info");
  assert(Params.empty() && "parameter vector is non-empty at start");

  CXXRecordDecl *ThisContext = nullptr;
  Qualifiers ThisTypeQuals;
  if (CXXMethodDecl *Method = dyn_cast<CXXMethodDecl>(D)) {
    ThisContext = cast<CXXRecordDecl>(Owner);
    ThisTypeQuals = Method->getFunctionObjectParameterType().getQualifiers();
  }

  TypeSourceInfo *NewTInfo = SemaRef.SubstFunctionDeclType(
      OldTInfo, TemplateArgs, D->getTypeSpecStartLoc(), D->getDeclName(),
      ThisContext, ThisTypeQuals, EvaluateConstraints);
  if (!NewTInfo)
    return nullptr;

  TypeLoc OldTL = OldTInfo->getTypeLoc().IgnoreParens();
  if (FunctionProtoTypeLoc OldProtoLoc = OldTL.getAs<FunctionProtoTypeLoc>()) {
    if (NewTInfo != OldTInfo) {
      // Get parameters from the new type info.
      TypeLoc NewTL = NewTInfo->getTypeLoc().IgnoreParens();
      FunctionProtoTypeLoc NewProtoLoc = NewTL.castAs<FunctionProtoTypeLoc>();
      unsigned NewIdx = 0;
      for (unsigned OldIdx = 0, NumOldParams = OldProtoLoc.getNumParams();
           OldIdx != NumOldParams; ++OldIdx) {
        ParmVarDecl *OldParam = OldProtoLoc.getParam(OldIdx);
        if (!OldParam)
          return nullptr;

        LocalInstantiationScope *Scope = SemaRef.CurrentInstantiationScope;

        std::optional<unsigned> NumArgumentsInExpansion;
        if (OldParam->isParameterPack())
          NumArgumentsInExpansion =
              SemaRef.getNumArgumentsInExpansion(OldParam->getType(),
                                                 TemplateArgs);
        if (!NumArgumentsInExpansion) {
          // Simple case: normal parameter, or a parameter pack that's
          // instantiated to a (still-dependent) parameter pack.
          ParmVarDecl *NewParam = NewProtoLoc.getParam(NewIdx++);
          Params.push_back(NewParam);
          Scope->InstantiatedLocal(OldParam, NewParam);
        } else {
          // Parameter pack expansion: make the instantiation an argument pack.
          Scope->MakeInstantiatedLocalArgPack(OldParam);
          for (unsigned I = 0; I != *NumArgumentsInExpansion; ++I) {
            ParmVarDecl *NewParam = NewProtoLoc.getParam(NewIdx++);
            Params.push_back(NewParam);
            Scope->InstantiatedLocalPackArg(OldParam, NewParam);
          }
        }
      }
    } else {
      // The function type itself was not dependent and therefore no
      // substitution occurred. However, we still need to instantiate
      // the function parameters themselves.
      const FunctionProtoType *OldProto =
          cast<FunctionProtoType>(OldProtoLoc.getType());
      for (unsigned i = 0, i_end = OldProtoLoc.getNumParams(); i != i_end;
           ++i) {
        ParmVarDecl *OldParam = OldProtoLoc.getParam(i);
        if (!OldParam) {
          Params.push_back(SemaRef.BuildParmVarDeclForTypedef(
              D, D->getLocation(), OldProto->getParamType(i)));
          continue;
        }

        ParmVarDecl *Parm =
            cast_or_null<ParmVarDecl>(VisitParmVarDecl(OldParam));
        if (!Parm)
          return nullptr;
        Params.push_back(Parm);
      }
    }
  } else {
    // If the type of this function, after ignoring parentheses, is not
    // *directly* a function type, then we're instantiating a function that
    // was declared via a typedef or with attributes, e.g.,
    //
    //   typedef int functype(int, int);
    //   functype func;
    //   int __cdecl meth(int, int);
    //
    // In this case, we'll just go instantiate the ParmVarDecls that we
    // synthesized in the method declaration.
    SmallVector<QualType, 4> ParamTypes;
    Sema::ExtParameterInfoBuilder ExtParamInfos;
    if (SemaRef.SubstParmTypes(D->getLocation(), D->parameters(), nullptr,
                               TemplateArgs, ParamTypes, &Params,
                               ExtParamInfos))
      return nullptr;
  }

  return NewTInfo;
}

void Sema::addInstantiatedLocalVarsToScope(FunctionDecl *Function,
                                           const FunctionDecl *PatternDecl,
                                           LocalInstantiationScope &Scope) {
  LambdaScopeInfo *LSI = cast<LambdaScopeInfo>(getFunctionScopes().back());

  for (auto *decl : PatternDecl->decls()) {
    if (!isa<VarDecl>(decl) || isa<ParmVarDecl>(decl))
      continue;

    VarDecl *VD = cast<VarDecl>(decl);
    IdentifierInfo *II = VD->getIdentifier();

    auto it = llvm::find_if(Function->decls(), [&](Decl *inst) {
      VarDecl *InstVD = dyn_cast<VarDecl>(inst);
      return InstVD && InstVD->isLocalVarDecl() &&
             InstVD->getIdentifier() == II;
    });

    if (it == Function->decls().end())
      continue;

    Scope.InstantiatedLocal(VD, *it);
    LSI->addCapture(cast<VarDecl>(*it), /*isBlock=*/false, /*isByref=*/false,
                    /*isNested=*/false, VD->getLocation(), SourceLocation(),
                    VD->getType(), /*Invalid=*/false);
  }
}

bool Sema::addInstantiatedParametersToScope(
    FunctionDecl *Function, const FunctionDecl *PatternDecl,
    LocalInstantiationScope &Scope,
    const MultiLevelTemplateArgumentList &TemplateArgs) {
  unsigned FParamIdx = 0;
  for (unsigned I = 0, N = PatternDecl->getNumParams(); I != N; ++I) {
    const ParmVarDecl *PatternParam = PatternDecl->getParamDecl(I);
    if (!PatternParam->isParameterPack()) {
      // Simple case: not a parameter pack.
      assert(FParamIdx < Function->getNumParams());
      ParmVarDecl *FunctionParam = Function->getParamDecl(FParamIdx);
      FunctionParam->setDeclName(PatternParam->getDeclName());
      // If the parameter's type is not dependent, update it to match the type
      // in the pattern. They can differ in top-level cv-qualifiers, and we want
      // the pattern's type here. If the type is dependent, they can't differ,
      // per core issue 1668. Substitute into the type from the pattern, in case
      // it's instantiation-dependent.
      // FIXME: Updating the type to work around this is at best fragile.
      if (!PatternDecl->getType()->isDependentType()) {
        QualType T = SubstType(PatternParam->getType(), TemplateArgs,
                               FunctionParam->getLocation(),
                               FunctionParam->getDeclName());
        if (T.isNull())
          return true;
        FunctionParam->setType(T);
      }

      Scope.InstantiatedLocal(PatternParam, FunctionParam);
      ++FParamIdx;
      continue;
    }

    // Expand the parameter pack.
    Scope.MakeInstantiatedLocalArgPack(PatternParam);
    std::optional<unsigned> NumArgumentsInExpansion =
        getNumArgumentsInExpansion(PatternParam->getType(), TemplateArgs);
    if (NumArgumentsInExpansion) {
      QualType PatternType =
          PatternParam->getType()->castAs<PackExpansionType>()->getPattern();
      for (unsigned Arg = 0; Arg < *NumArgumentsInExpansion; ++Arg) {
        ParmVarDecl *FunctionParam = Function->getParamDecl(FParamIdx);
        FunctionParam->setDeclName(PatternParam->getDeclName());
        if (!PatternDecl->getType()->isDependentType()) {
          Sema::ArgumentPackSubstitutionIndexRAII SubstIndex(*this, Arg);
          QualType T =
              SubstType(PatternType, TemplateArgs, FunctionParam->getLocation(),
                        FunctionParam->getDeclName());
          if (T.isNull())
            return true;
          FunctionParam->setType(T);
        }

        Scope.InstantiatedLocalPackArg(PatternParam, FunctionParam);
        ++FParamIdx;
      }
    }
  }

  return false;
}

bool Sema::InstantiateDefaultArgument(SourceLocation CallLoc, FunctionDecl *FD,
                                      ParmVarDecl *Param) {
  assert(Param->hasUninstantiatedDefaultArg());

  // Instantiate the expression.
  //
  // FIXME: Pass in a correct Pattern argument, otherwise
  // getTemplateInstantiationArgs uses the lexical context of FD, e.g.
  //
  // template<typename T>
  // struct A {
  //   static int FooImpl();
  //
  //   template<typename Tp>
  //   // bug: default argument A<T>::FooImpl() is evaluated with 2-level
  //   // template argument list [[T], [Tp]], should be [[Tp]].
  //   friend A<Tp> Foo(int a);
  // };
  //
  // template<typename T>
  // A<T> Foo(int a = A<T>::FooImpl());
  MultiLevelTemplateArgumentList TemplateArgs =
      getTemplateInstantiationArgs(FD, FD->getLexicalDeclContext(),
                                   /*Final=*/false, /*Innermost=*/std::nullopt,
                                   /*RelativeToPrimary=*/true);

  if (SubstDefaultArgument(CallLoc, Param, TemplateArgs, /*ForCallExpr*/ true))
    return true;

  if (ASTMutationListener *L = getASTMutationListener())
    L->DefaultArgumentInstantiated(Param);

  return false;
}

void Sema::InstantiateExceptionSpec(SourceLocation PointOfInstantiation,
                                    FunctionDecl *Decl) {
  const FunctionProtoType *Proto = Decl->getType()->castAs<FunctionProtoType>();
  if (Proto->getExceptionSpecType() != EST_Uninstantiated)
    return;

  InstantiatingTemplate Inst(*this, PointOfInstantiation, Decl,
                             InstantiatingTemplate::ExceptionSpecification());
  if (Inst.isInvalid()) {
    // We hit the instantiation depth limit. Clear the exception specification
    // so that our callers don't have to cope with EST_Uninstantiated.
    UpdateExceptionSpec(Decl, EST_None);
    return;
  }
  if (Inst.isAlreadyInstantiating()) {
    // This exception specification indirectly depends on itself. Reject.
    // FIXME: Corresponding rule in the standard?
    Diag(PointOfInstantiation, diag::err_exception_spec_cycle) << Decl;
    UpdateExceptionSpec(Decl, EST_None);
    return;
  }

  // Enter the scope of this instantiation. We don't use
  // PushDeclContext because we don't have a scope.
  Sema::ContextRAII savedContext(*this, Decl);
  LocalInstantiationScope Scope(*this);

  MultiLevelTemplateArgumentList TemplateArgs =
      getTemplateInstantiationArgs(Decl, Decl->getLexicalDeclContext(),
                                   /*Final=*/false, /*Innermost=*/std::nullopt,
                                   /*RelativeToPrimary*/ true);

  // FIXME: We can't use getTemplateInstantiationPattern(false) in general
  // here, because for a non-defining friend declaration in a class template,
  // we don't store enough information to map back to the friend declaration in
  // the template.
  FunctionDecl *Template = Proto->getExceptionSpecTemplate();
  if (addInstantiatedParametersToScope(Decl, Template, Scope, TemplateArgs)) {
    UpdateExceptionSpec(Decl, EST_None);
    return;
  }

  // The noexcept specification could reference any lambda captures. Ensure
  // those are added to the LocalInstantiationScope.
  LambdaScopeForCallOperatorInstantiationRAII PushLambdaCaptures(
      *this, Decl, TemplateArgs, Scope,
      /*ShouldAddDeclsFromParentScope=*/false);

  SubstExceptionSpec(Decl, Template->getType()->castAs<FunctionProtoType>(),
                     TemplateArgs);
}

/// Initializes the common fields of an instantiation function
/// declaration (New) from the corresponding fields of its template (Tmpl).
///
/// \returns true if there was an error
bool
TemplateDeclInstantiator::InitFunctionInstantiation(FunctionDecl *New,
                                                    FunctionDecl *Tmpl) {
  New->setImplicit(Tmpl->isImplicit());

  // Forward the mangling number from the template to the instantiated decl.
  SemaRef.Context.setManglingNumber(New,
                                    SemaRef.Context.getManglingNumber(Tmpl));

  // If we are performing substituting explicitly-specified template arguments
  // or deduced template arguments into a function template and we reach this
  // point, we are now past the point where SFINAE applies and have committed
  // to keeping the new function template specialization. We therefore
  // convert the active template instantiation for the function template
  // into a template instantiation for this specific function template
  // specialization, which is not a SFINAE context, so that we diagnose any
  // further errors in the declaration itself.
  //
  // FIXME: This is a hack.
  typedef Sema::CodeSynthesisContext ActiveInstType;
  ActiveInstType &ActiveInst = SemaRef.CodeSynthesisContexts.back();
  if (ActiveInst.Kind == ActiveInstType::ExplicitTemplateArgumentSubstitution ||
      ActiveInst.Kind == ActiveInstType::DeducedTemplateArgumentSubstitution) {
    if (isa<FunctionTemplateDecl>(ActiveInst.Entity)) {
      SemaRef.InstantiatingSpecializations.erase(
          {ActiveInst.Entity->getCanonicalDecl(), ActiveInst.Kind});
      atTemplateEnd(SemaRef.TemplateInstCallbacks, SemaRef, ActiveInst);
      ActiveInst.Kind = ActiveInstType::TemplateInstantiation;
      ActiveInst.Entity = New;
      atTemplateBegin(SemaRef.TemplateInstCallbacks, SemaRef, ActiveInst);
    }
  }

  const FunctionProtoType *Proto = Tmpl->getType()->getAs<FunctionProtoType>();
  assert(Proto && "Function template without prototype?");

  if (Proto->hasExceptionSpec() || Proto->getNoReturnAttr()) {
    FunctionProtoType::ExtProtoInfo EPI = Proto->getExtProtoInfo();

    // DR1330: In C++11, defer instantiation of a non-trivial
    // exception specification.
    // DR1484: Local classes and their members are instantiated along with the
    // containing function.
    if (SemaRef.getLangOpts().CPlusPlus11 &&
        EPI.ExceptionSpec.Type != EST_None &&
        EPI.ExceptionSpec.Type != EST_DynamicNone &&
        EPI.ExceptionSpec.Type != EST_BasicNoexcept &&
        !Tmpl->isInLocalScopeForInstantiation()) {
      FunctionDecl *ExceptionSpecTemplate = Tmpl;
      if (EPI.ExceptionSpec.Type == EST_Uninstantiated)
        ExceptionSpecTemplate = EPI.ExceptionSpec.SourceTemplate;
      ExceptionSpecificationType NewEST = EST_Uninstantiated;
      if (EPI.ExceptionSpec.Type == EST_Unevaluated)
        NewEST = EST_Unevaluated;

      // Mark the function has having an uninstantiated exception specification.
      const FunctionProtoType *NewProto
        = New->getType()->getAs<FunctionProtoType>();
      assert(NewProto && "Template instantiation without function prototype?");
      EPI = NewProto->getExtProtoInfo();
      EPI.ExceptionSpec.Type = NewEST;
      EPI.ExceptionSpec.SourceDecl = New;
      EPI.ExceptionSpec.SourceTemplate = ExceptionSpecTemplate;
      New->setType(SemaRef.Context.getFunctionType(
          NewProto->getReturnType(), NewProto->getParamTypes(), EPI));
    } else {
      Sema::ContextRAII SwitchContext(SemaRef, New);
      SemaRef.SubstExceptionSpec(New, Proto, TemplateArgs);
    }
  }

  // Get the definition. Leaves the variable unchanged if undefined.
  const FunctionDecl *Definition = Tmpl;
  Tmpl->isDefined(Definition);

  SemaRef.InstantiateAttrs(TemplateArgs, Definition, New,
                           LateAttrs, StartingScope);

  return false;
}

/// Initializes common fields of an instantiated method
/// declaration (New) from the corresponding fields of its template
/// (Tmpl).
///
/// \returns true if there was an error
bool
TemplateDeclInstantiator::InitMethodInstantiation(CXXMethodDecl *New,
                                                  CXXMethodDecl *Tmpl) {
  if (InitFunctionInstantiation(New, Tmpl))
    return true;

  if (isa<CXXDestructorDecl>(New) && SemaRef.getLangOpts().CPlusPlus11)
    SemaRef.AdjustDestructorExceptionSpec(cast<CXXDestructorDecl>(New));

  New->setAccess(Tmpl->getAccess());
  if (Tmpl->isVirtualAsWritten())
    New->setVirtualAsWritten(true);

  // FIXME: New needs a pointer to Tmpl
  return false;
}

bool TemplateDeclInstantiator::SubstDefaultedFunction(FunctionDecl *New,
                                                      FunctionDecl *Tmpl) {
  // Transfer across any unqualified lookups.
  if (auto *DFI = Tmpl->getDefalutedOrDeletedInfo()) {
    SmallVector<DeclAccessPair, 32> Lookups;
    Lookups.reserve(DFI->getUnqualifiedLookups().size());
    bool AnyChanged = false;
    for (DeclAccessPair DA : DFI->getUnqualifiedLookups()) {
      NamedDecl *D = SemaRef.FindInstantiatedDecl(New->getLocation(),
                                                  DA.getDecl(), TemplateArgs);
      if (!D)
        return true;
      AnyChanged |= (D != DA.getDecl());
      Lookups.push_back(DeclAccessPair::make(D, DA.getAccess()));
    }

    // It's unlikely that substitution will change any declarations. Don't
    // store an unnecessary copy in that case.
    New->setDefaultedOrDeletedInfo(
        AnyChanged ? FunctionDecl::DefaultedOrDeletedFunctionInfo::Create(
                         SemaRef.Context, Lookups)
                   : DFI);
  }

  SemaRef.SetDeclDefaulted(New, Tmpl->getLocation());
  return false;
}

FunctionDecl *Sema::InstantiateFunctionDeclaration(
    FunctionTemplateDecl *FTD, const TemplateArgumentList *Args,
    SourceLocation Loc, CodeSynthesisContext::SynthesisKind CSC) {
  FunctionDecl *FD = FTD->getTemplatedDecl();

  sema::TemplateDeductionInfo Info(Loc);
  InstantiatingTemplate Inst(*this, Loc, FTD, Args->asArray(), CSC, Info);
  if (Inst.isInvalid())
    return nullptr;

  ContextRAII SavedContext(*this, FD);
  MultiLevelTemplateArgumentList MArgs(FTD, Args->asArray(),
                                       /*Final=*/false);

  return cast_or_null<FunctionDecl>(SubstDecl(FD, FD->getParent(), MArgs));
}

void Sema::InstantiateFunctionDefinition(SourceLocation PointOfInstantiation,
                                         FunctionDecl *Function,
                                         bool Recursive,
                                         bool DefinitionRequired,
                                         bool AtEndOfTU) {
  if (Function->isInvalidDecl() || isa<CXXDeductionGuideDecl>(Function))
    return;

  // Never instantiate an explicit specialization except if it is a class scope
  // explicit specialization.
  TemplateSpecializationKind TSK =
      Function->getTemplateSpecializationKindForInstantiation();
  if (TSK == TSK_ExplicitSpecialization)
    return;

  // Never implicitly instantiate a builtin; we don't actually need a function
  // body.
  if (Function->getBuiltinID() && TSK == TSK_ImplicitInstantiation &&
      !DefinitionRequired)
    return;

  // Don't instantiate a definition if we already have one.
  const FunctionDecl *ExistingDefn = nullptr;
  if (Function->isDefined(ExistingDefn,
                          /*CheckForPendingFriendDefinition=*/true)) {
    if (ExistingDefn->isThisDeclarationADefinition())
      return;

    // If we're asked to instantiate a function whose body comes from an
    // instantiated friend declaration, attach the instantiated body to the
    // corresponding declaration of the function.
    assert(ExistingDefn->isThisDeclarationInstantiatedFromAFriendDefinition());
    Function = const_cast<FunctionDecl*>(ExistingDefn);
  }

  // Find the function body that we'll be substituting.
  const FunctionDecl *PatternDecl = Function->getTemplateInstantiationPattern();
  assert(PatternDecl && "instantiating a non-template");

  const FunctionDecl *PatternDef = PatternDecl->getDefinition();
  Stmt *Pattern = nullptr;
  if (PatternDef) {
    Pattern = PatternDef->getBody(PatternDef);
    PatternDecl = PatternDef;
    if (PatternDef->willHaveBody())
      PatternDef = nullptr;
  }

  // FIXME: We need to track the instantiation stack in order to know which
  // definitions should be visible within this instantiation.
  if (DiagnoseUninstantiableTemplate(PointOfInstantiation, Function,
                                Function->getInstantiatedFromMemberFunction(),
                                     PatternDecl, PatternDef, TSK,
                                     /*Complain*/DefinitionRequired)) {
    if (DefinitionRequired)
      Function->setInvalidDecl();
    else if (TSK == TSK_ExplicitInstantiationDefinition ||
             (Function->isConstexpr() && !Recursive)) {
      // Try again at the end of the translation unit (at which point a
      // definition will be required).
      assert(!Recursive);
      Function->setInstantiationIsPending(true);
      PendingInstantiations.push_back(
        std::make_pair(Function, PointOfInstantiation));
    } else if (TSK == TSK_ImplicitInstantiation) {
      if (AtEndOfTU && !getDiagnostics().hasErrorOccurred() &&
          !getSourceManager().isInSystemHeader(PatternDecl->getBeginLoc())) {
        Diag(PointOfInstantiation, diag::warn_func_template_missing)
          << Function;
        Diag(PatternDecl->getLocation(), diag::note_forward_template_decl);
        if (getLangOpts().CPlusPlus11)
          Diag(PointOfInstantiation, diag::note_inst_declaration_hint)
              << Function;
      }
    }

    return;
  }

  // Postpone late parsed template instantiations.
  if (PatternDecl->isLateTemplateParsed() &&
      !LateTemplateParser) {
    Function->setInstantiationIsPending(true);
    LateParsedInstantiations.push_back(
        std::make_pair(Function, PointOfInstantiation));
    return;
  }

  llvm::TimeTraceScope TimeScope("InstantiateFunction", [&]() {
    llvm::TimeTraceMetadata M;
    llvm::raw_string_ostream OS(M.Detail);
    Function->getNameForDiagnostic(OS, getPrintingPolicy(),
                                   /*Qualified=*/true);
    if (llvm::isTimeTraceVerbose()) {
      auto Loc = SourceMgr.getExpansionLoc(Function->getLocation());
      M.File = SourceMgr.getFilename(Loc);
      M.Line = SourceMgr.getExpansionLineNumber(Loc);
    }
    return M;
  });

  // If we're performing recursive template instantiation, create our own
  // queue of pending implicit instantiations that we will instantiate later,
  // while we're still within our own instantiation context.
  // This has to happen before LateTemplateParser below is called, so that
  // it marks vtables used in late parsed templates as used.
  GlobalEagerInstantiationScope GlobalInstantiations(*this,
                                                     /*Enabled=*/Recursive);
  LocalEagerInstantiationScope LocalInstantiations(*this);

  // Call the LateTemplateParser callback if there is a need to late parse
  // a templated function definition.
  if (!Pattern && PatternDecl->isLateTemplateParsed() &&
      LateTemplateParser) {
    // FIXME: Optimize to allow individual templates to be deserialized.
    if (PatternDecl->isFromASTFile())
      ExternalSource->ReadLateParsedTemplates(LateParsedTemplateMap);

    auto LPTIter = LateParsedTemplateMap.find(PatternDecl);
    assert(LPTIter != LateParsedTemplateMap.end() &&
           "missing LateParsedTemplate");
    LateTemplateParser(OpaqueParser, *LPTIter->second);
    Pattern = PatternDecl->getBody(PatternDecl);
    updateAttrsForLateParsedTemplate(PatternDecl, Function);
  }

  // Note, we should never try to instantiate a deleted function template.
  assert((Pattern || PatternDecl->isDefaulted() ||
          PatternDecl->hasSkippedBody()) &&
         "unexpected kind of function template definition");

  // C++1y [temp.explicit]p10:
  //   Except for inline functions, declarations with types deduced from their
  //   initializer or return value, and class template specializations, other
  //   explicit instantiation declarations have the effect of suppressing the
  //   implicit instantiation of the entity to which they refer.
  if (TSK == TSK_ExplicitInstantiationDeclaration &&
      !PatternDecl->isInlined() &&
      !PatternDecl->getReturnType()->getContainedAutoType())
    return;

  if (PatternDecl->isInlined()) {
    // Function, and all later redeclarations of it (from imported modules,
    // for instance), are now implicitly inline.
    for (auto *D = Function->getMostRecentDecl(); /**/;
         D = D->getPreviousDecl()) {
      D->setImplicitlyInline();
      if (D == Function)
        break;
    }
  }

  InstantiatingTemplate Inst(*this, PointOfInstantiation, Function);
  if (Inst.isInvalid() || Inst.isAlreadyInstantiating())
    return;
  PrettyDeclStackTraceEntry CrashInfo(Context, Function, SourceLocation(),
                                      "instantiating function definition");

  // The instantiation is visible here, even if it was first declared in an
  // unimported module.
  Function->setVisibleDespiteOwningModule();

  // Copy the source locations from the pattern.
  Function->setLocation(PatternDecl->getLocation());
  Function->setInnerLocStart(PatternDecl->getInnerLocStart());
  Function->setRangeEnd(PatternDecl->getEndLoc());
  Function->setDeclarationNameLoc(PatternDecl->getNameInfo().getInfo());

  EnterExpressionEvaluationContext EvalContext(
      *this, Sema::ExpressionEvaluationContext::PotentiallyEvaluated);

  Qualifiers ThisTypeQuals;
  CXXRecordDecl *ThisContext = nullptr;
  if (CXXMethodDecl *Method = dyn_cast<CXXMethodDecl>(Function)) {
    ThisContext = Method->getParent();
    ThisTypeQuals = Method->getMethodQualifiers();
  }
  CXXThisScopeRAII ThisScope(*this, ThisContext, ThisTypeQuals);

  // Introduce a new scope where local variable instantiations will be
  // recorded, unless we're actually a member function within a local
  // class, in which case we need to merge our results with the parent
  // scope (of the enclosing function). The exception is instantiating
  // a function template specialization, since the template to be
  // instantiated already has references to locals properly substituted.
  bool MergeWithParentScope = false;
  if (CXXRecordDecl *Rec = dyn_cast<CXXRecordDecl>(Function->getDeclContext()))
    MergeWithParentScope =
        Rec->isLocalClass() && !Function->isFunctionTemplateSpecialization();

  LocalInstantiationScope Scope(*this, MergeWithParentScope);
  auto RebuildTypeSourceInfoForDefaultSpecialMembers = [&]() {
    // Special members might get their TypeSourceInfo set up w.r.t the
    // PatternDecl context, in which case parameters could still be pointing
    // back to the original class, make sure arguments are bound to the
    // instantiated record instead.
    assert(PatternDecl->isDefaulted() &&
           "Special member needs to be defaulted");
    auto PatternSM = getDefaultedFunctionKind(PatternDecl).asSpecialMember();
    if (!(PatternSM == CXXSpecialMemberKind::CopyConstructor ||
          PatternSM == CXXSpecialMemberKind::CopyAssignment ||
          PatternSM == CXXSpecialMemberKind::MoveConstructor ||
          PatternSM == CXXSpecialMemberKind::MoveAssignment))
      return;

    auto *NewRec = dyn_cast<CXXRecordDecl>(Function->getDeclContext());
    const auto *PatternRec =
        dyn_cast<CXXRecordDecl>(PatternDecl->getDeclContext());
    if (!NewRec || !PatternRec)
      return;
    if (!PatternRec->isLambda())
      return;

    struct SpecialMemberTypeInfoRebuilder
        : TreeTransform<SpecialMemberTypeInfoRebuilder> {
      using Base = TreeTransform<SpecialMemberTypeInfoRebuilder>;
      const CXXRecordDecl *OldDecl;
      CXXRecordDecl *NewDecl;

      SpecialMemberTypeInfoRebuilder(Sema &SemaRef, const CXXRecordDecl *O,
                                     CXXRecordDecl *N)
          : TreeTransform(SemaRef), OldDecl(O), NewDecl(N) {}

      bool TransformExceptionSpec(SourceLocation Loc,
                                  FunctionProtoType::ExceptionSpecInfo &ESI,
                                  SmallVectorImpl<QualType> &Exceptions,
                                  bool &Changed) {
        return false;
      }

      QualType TransformRecordType(TypeLocBuilder &TLB, RecordTypeLoc TL) {
        const RecordType *T = TL.getTypePtr();
        RecordDecl *Record = cast_or_null<RecordDecl>(
            getDerived().TransformDecl(TL.getNameLoc(), T->getDecl()));
        if (Record != OldDecl)
          return Base::TransformRecordType(TLB, TL);

        QualType Result = getDerived().RebuildRecordType(NewDecl);
        if (Result.isNull())
          return QualType();

        RecordTypeLoc NewTL = TLB.push<RecordTypeLoc>(Result);
        NewTL.setNameLoc(TL.getNameLoc());
        return Result;
      }
    } IR{*this, PatternRec, NewRec};

    TypeSourceInfo *NewSI = IR.TransformType(Function->getTypeSourceInfo());
    assert(NewSI && "Type Transform failed?");
    Function->setType(NewSI->getType());
    Function->setTypeSourceInfo(NewSI);

    ParmVarDecl *Parm = Function->getParamDecl(0);
    TypeSourceInfo *NewParmSI = IR.TransformType(Parm->getTypeSourceInfo());
    assert(NewParmSI && "Type transformation failed.");
    Parm->setType(NewParmSI->getType());
    Parm->setTypeSourceInfo(NewParmSI);
  };

  if (PatternDecl->isDefaulted()) {
    RebuildTypeSourceInfoForDefaultSpecialMembers();
    SetDeclDefaulted(Function, PatternDecl->getLocation());
  } else {
    MultiLevelTemplateArgumentList TemplateArgs = getTemplateInstantiationArgs(
        Function, Function->getLexicalDeclContext(), /*Final=*/false,
        /*Innermost=*/std::nullopt, false, PatternDecl);

    // Substitute into the qualifier; we can get a substitution failure here
    // through evil use of alias templates.
    // FIXME: Is CurContext correct for this? Should we go to the (instantiation
    // of the) lexical context of the pattern?
    SubstQualifier(*this, PatternDecl, Function, TemplateArgs);

    ActOnStartOfFunctionDef(nullptr, Function);

    // Enter the scope of this instantiation. We don't use
    // PushDeclContext because we don't have a scope.
    Sema::ContextRAII savedContext(*this, Function);

    FPFeaturesStateRAII SavedFPFeatures(*this);
    CurFPFeatures = FPOptions(getLangOpts());
    FpPragmaStack.CurrentValue = FPOptionsOverride();

    if (addInstantiatedParametersToScope(Function, PatternDecl, Scope,
                                         TemplateArgs))
      return;

    StmtResult Body;
    if (PatternDecl->hasSkippedBody()) {
      ActOnSkippedFunctionBody(Function);
      Body = nullptr;
    } else {
      if (CXXConstructorDecl *Ctor = dyn_cast<CXXConstructorDecl>(Function)) {
        // If this is a constructor, instantiate the member initializers.
        InstantiateMemInitializers(Ctor, cast<CXXConstructorDecl>(PatternDecl),
                                   TemplateArgs);

        // If this is an MS ABI dllexport default constructor, instantiate any
        // default arguments.
        if (Context.getTargetInfo().getCXXABI().isMicrosoft() &&
            Ctor->isDefaultConstructor()) {
          InstantiateDefaultCtorDefaultArgs(Ctor);
        }
      }

      // Instantiate the function body.
      Body = SubstStmt(Pattern, TemplateArgs);

      if (Body.isInvalid())
        Function->setInvalidDecl();
    }
    // FIXME: finishing the function body while in an expression evaluation
    // context seems wrong. Investigate more.
    ActOnFinishFunctionBody(Function, Body.get(), /*IsInstantiation=*/true);

    PerformDependentDiagnostics(PatternDecl, TemplateArgs);

    if (auto *Listener = getASTMutationListener())
      Listener->FunctionDefinitionInstantiated(Function);

    savedContext.pop();
  }

  DeclGroupRef DG(Function);
  Consumer.HandleTopLevelDecl(DG);

  // This class may have local implicit instantiations that need to be
  // instantiation within this scope.
  LocalInstantiations.perform();
  Scope.Exit();
  GlobalInstantiations.perform();
}

VarTemplateSpecializationDecl *Sema::BuildVarTemplateInstantiation(
    VarTemplateDecl *VarTemplate, VarDecl *FromVar,
    const TemplateArgumentList *PartialSpecArgs,
    const TemplateArgumentListInfo &TemplateArgsInfo,
    SmallVectorImpl<TemplateArgument> &Converted,
    SourceLocation PointOfInstantiation, LateInstantiatedAttrVec *LateAttrs,
    LocalInstantiationScope *StartingScope) {
  if (FromVar->isInvalidDecl())
    return nullptr;

  InstantiatingTemplate Inst(*this, PointOfInstantiation, FromVar);
  if (Inst.isInvalid())
    return nullptr;

  // Instantiate the first declaration of the variable template: for a partial
  // specialization of a static data member template, the first declaration may
  // or may not be the declaration in the class; if it's in the class, we want
  // to instantiate a member in the class (a declaration), and if it's outside,
  // we want to instantiate a definition.
  //
  // If we're instantiating an explicitly-specialized member template or member
  // partial specialization, don't do this. The member specialization completely
  // replaces the original declaration in this case.
  bool IsMemberSpec = false;
  MultiLevelTemplateArgumentList MultiLevelList;
  if (auto *PartialSpec =
          dyn_cast<VarTemplatePartialSpecializationDecl>(FromVar)) {
    assert(PartialSpecArgs);
    IsMemberSpec = PartialSpec->isMemberSpecialization();
    MultiLevelList.addOuterTemplateArguments(
        PartialSpec, PartialSpecArgs->asArray(), /*Final=*/false);
  } else {
    assert(VarTemplate == FromVar->getDescribedVarTemplate());
    IsMemberSpec = VarTemplate->isMemberSpecialization();
    MultiLevelList.addOuterTemplateArguments(VarTemplate, Converted,
                                             /*Final=*/false);
  }
  if (!IsMemberSpec)
    FromVar = FromVar->getFirstDecl();

  TemplateDeclInstantiator Instantiator(*this, FromVar->getDeclContext(),
                                        MultiLevelList);

  // TODO: Set LateAttrs and StartingScope ...

  return cast_or_null<VarTemplateSpecializationDecl>(
      Instantiator.VisitVarTemplateSpecializationDecl(
          VarTemplate, FromVar, TemplateArgsInfo, Converted));
}

VarTemplateSpecializationDecl *Sema::CompleteVarTemplateSpecializationDecl(
    VarTemplateSpecializationDecl *VarSpec, VarDecl *PatternDecl,
    const MultiLevelTemplateArgumentList &TemplateArgs) {
  assert(PatternDecl->isThisDeclarationADefinition() &&
         "don't have a definition to instantiate from");

  // Do substitution on the type of the declaration
  TypeSourceInfo *DI =
      SubstType(PatternDecl->getTypeSourceInfo(), TemplateArgs,
                PatternDecl->getTypeSpecStartLoc(), PatternDecl->getDeclName());
  if (!DI)
    return nullptr;

  // Update the type of this variable template specialization.
  VarSpec->setType(DI->getType());

  // Convert the declaration into a definition now.
  VarSpec->setCompleteDefinition();

  // Instantiate the initializer.
  InstantiateVariableInitializer(VarSpec, PatternDecl, TemplateArgs);

  if (getLangOpts().OpenCL)
    deduceOpenCLAddressSpace(VarSpec);

  return VarSpec;
}

void Sema::BuildVariableInstantiation(
    VarDecl *NewVar, VarDecl *OldVar,
    const MultiLevelTemplateArgumentList &TemplateArgs,
    LateInstantiatedAttrVec *LateAttrs, DeclContext *Owner,
    LocalInstantiationScope *StartingScope,
    bool InstantiatingVarTemplate,
    VarTemplateSpecializationDecl *PrevDeclForVarTemplateSpecialization) {
  // Instantiating a partial specialization to produce a partial
  // specialization.
  bool InstantiatingVarTemplatePartialSpec =
      isa<VarTemplatePartialSpecializationDecl>(OldVar) &&
      isa<VarTemplatePartialSpecializationDecl>(NewVar);
  // Instantiating from a variable template (or partial specialization) to
  // produce a variable template specialization.
  bool InstantiatingSpecFromTemplate =
      isa<VarTemplateSpecializationDecl>(NewVar) &&
      (OldVar->getDescribedVarTemplate() ||
       isa<VarTemplatePartialSpecializationDecl>(OldVar));

  // If we are instantiating a local extern declaration, the
  // instantiation belongs lexically to the containing function.
  // If we are instantiating a static data member defined
  // out-of-line, the instantiation will have the same lexical
  // context (which will be a namespace scope) as the template.
  if (OldVar->isLocalExternDecl()) {
    NewVar->setLocalExternDecl();
    NewVar->setLexicalDeclContext(Owner);
  } else if (OldVar->isOutOfLine())
    NewVar->setLexicalDeclContext(OldVar->getLexicalDeclContext());
  NewVar->setTSCSpec(OldVar->getTSCSpec());
  NewVar->setInitStyle(OldVar->getInitStyle());
  NewVar->setCXXForRangeDecl(OldVar->isCXXForRangeDecl());
  NewVar->setObjCForDecl(OldVar->isObjCForDecl());
  NewVar->setConstexpr(OldVar->isConstexpr());
  NewVar->setInitCapture(OldVar->isInitCapture());
  NewVar->setPreviousDeclInSameBlockScope(
      OldVar->isPreviousDeclInSameBlockScope());
  NewVar->setAccess(OldVar->getAccess());

  if (!OldVar->isStaticDataMember()) {
    if (OldVar->isUsed(false))
      NewVar->setIsUsed();
    NewVar->setReferenced(OldVar->isReferenced());
  }

  InstantiateAttrs(TemplateArgs, OldVar, NewVar, LateAttrs, StartingScope);

  LookupResult Previous(
      *this, NewVar->getDeclName(), NewVar->getLocation(),
      NewVar->isLocalExternDecl() ? Sema::LookupRedeclarationWithLinkage
                                  : Sema::LookupOrdinaryName,
      NewVar->isLocalExternDecl() ? RedeclarationKind::ForExternalRedeclaration
                                  : forRedeclarationInCurContext());

  if (NewVar->isLocalExternDecl() && OldVar->getPreviousDecl() &&
      (!OldVar->getPreviousDecl()->getDeclContext()->isDependentContext() ||
       OldVar->getPreviousDecl()->getDeclContext()==OldVar->getDeclContext())) {
    // We have a previous declaration. Use that one, so we merge with the
    // right type.
    if (NamedDecl *NewPrev = FindInstantiatedDecl(
            NewVar->getLocation(), OldVar->getPreviousDecl(), TemplateArgs))
      Previous.addDecl(NewPrev);
  } else if (!isa<VarTemplateSpecializationDecl>(NewVar) &&
             OldVar->hasLinkage()) {
    LookupQualifiedName(Previous, NewVar->getDeclContext(), false);
  } else if (PrevDeclForVarTemplateSpecialization) {
    Previous.addDecl(PrevDeclForVarTemplateSpecialization);
  }
  CheckVariableDeclaration(NewVar, Previous);

  if (!InstantiatingVarTemplate) {
    NewVar->getLexicalDeclContext()->addHiddenDecl(NewVar);
    if (!NewVar->isLocalExternDecl() || !NewVar->getPreviousDecl())
      NewVar->getDeclContext()->makeDeclVisibleInContext(NewVar);
  }

  if (!OldVar->isOutOfLine()) {
    if (NewVar->getDeclContext()->isFunctionOrMethod())
      CurrentInstantiationScope->InstantiatedLocal(OldVar, NewVar);
  }

  // Link instantiations of static data members back to the template from
  // which they were instantiated.
  //
  // Don't do this when instantiating a template (we link the template itself
  // back in that case) nor when instantiating a static data member template
  // (that's not a member specialization).
  if (NewVar->isStaticDataMember() && !InstantiatingVarTemplate &&
      !InstantiatingSpecFromTemplate)
    NewVar->setInstantiationOfStaticDataMember(OldVar,
                                               TSK_ImplicitInstantiation);

  // If the pattern is an (in-class) explicit specialization, then the result
  // is also an explicit specialization.
  if (VarTemplateSpecializationDecl *OldVTSD =
          dyn_cast<VarTemplateSpecializationDecl>(OldVar)) {
    if (OldVTSD->getSpecializationKind() == TSK_ExplicitSpecialization &&
        !isa<VarTemplatePartialSpecializationDecl>(OldVTSD))
      cast<VarTemplateSpecializationDecl>(NewVar)->setSpecializationKind(
          TSK_ExplicitSpecialization);
  }

  // Forward the mangling number from the template to the instantiated decl.
  Context.setManglingNumber(NewVar, Context.getManglingNumber(OldVar));
  Context.setStaticLocalNumber(NewVar, Context.getStaticLocalNumber(OldVar));

  // Figure out whether to eagerly instantiate the initializer.
  if (InstantiatingVarTemplate || InstantiatingVarTemplatePartialSpec) {
    // We're producing a template. Don't instantiate the initializer yet.
  } else if (NewVar->getType()->isUndeducedType()) {
    // We need the type to complete the declaration of the variable.
    InstantiateVariableInitializer(NewVar, OldVar, TemplateArgs);
  } else if (InstantiatingSpecFromTemplate ||
             (OldVar->isInline() && OldVar->isThisDeclarationADefinition() &&
              !NewVar->isThisDeclarationADefinition())) {
    // Delay instantiation of the initializer for variable template
    // specializations or inline static data members until a definition of the
    // variable is needed.
  } else {
    InstantiateVariableInitializer(NewVar, OldVar, TemplateArgs);
  }

  // Diagnose unused local variables with dependent types, where the diagnostic
  // will have been deferred.
  if (!NewVar->isInvalidDecl() &&
      NewVar->getDeclContext()->isFunctionOrMethod() &&
      OldVar->getType()->isDependentType())
    DiagnoseUnusedDecl(NewVar);
}

void Sema::InstantiateVariableInitializer(
    VarDecl *Var, VarDecl *OldVar,
    const MultiLevelTemplateArgumentList &TemplateArgs) {
  if (ASTMutationListener *L = getASTContext().getASTMutationListener())
    L->VariableDefinitionInstantiated(Var);

  // We propagate the 'inline' flag with the initializer, because it
  // would otherwise imply that the variable is a definition for a
  // non-static data member.
  if (OldVar->isInlineSpecified())
    Var->setInlineSpecified();
  else if (OldVar->isInline())
    Var->setImplicitlyInline();

  if (OldVar->getInit()) {
    EnterExpressionEvaluationContext Evaluated(
        *this, Sema::ExpressionEvaluationContext::PotentiallyEvaluated, Var);

    keepInLifetimeExtendingContext();
    // Instantiate the initializer.
    ExprResult Init;

    {
      ContextRAII SwitchContext(*this, Var->getDeclContext());
      Init = SubstInitializer(OldVar->getInit(), TemplateArgs,
                              OldVar->getInitStyle() == VarDecl::CallInit);
    }

    if (!Init.isInvalid()) {
      Expr *InitExpr = Init.get();

      if (Var->hasAttr<DLLImportAttr>() &&
          (!InitExpr ||
           !InitExpr->isConstantInitializer(getASTContext(), false))) {
        // Do not dynamically initialize dllimport variables.
      } else if (InitExpr) {
        bool DirectInit = OldVar->isDirectInit();
        AddInitializerToDecl(Var, InitExpr, DirectInit);
      } else
        ActOnUninitializedDecl(Var);
    } else {
      // FIXME: Not too happy about invalidating the declaration
      // because of a bogus initializer.
      Var->setInvalidDecl();
    }
  } else {
    // `inline` variables are a definition and declaration all in one; we won't
    // pick up an initializer from anywhere else.
    if (Var->isStaticDataMember() && !Var->isInline()) {
      if (!Var->isOutOfLine())
        return;

      // If the declaration inside the class had an initializer, don't add
      // another one to the out-of-line definition.
      if (OldVar->getFirstDecl()->hasInit())
        return;
    }

    // We'll add an initializer to a for-range declaration later.
    if (Var->isCXXForRangeDecl() || Var->isObjCForDecl())
      return;

    ActOnUninitializedDecl(Var);
  }

  if (getLangOpts().CUDA)
    CUDA().checkAllowedInitializer(Var);
}

void Sema::InstantiateVariableDefinition(SourceLocation PointOfInstantiation,
                                         VarDecl *Var, bool Recursive,
                                      bool DefinitionRequired, bool AtEndOfTU) {
  if (Var->isInvalidDecl())
    return;

  // Never instantiate an explicitly-specialized entity.
  TemplateSpecializationKind TSK =
      Var->getTemplateSpecializationKindForInstantiation();
  if (TSK == TSK_ExplicitSpecialization)
    return;

  // Find the pattern and the arguments to substitute into it.
  VarDecl *PatternDecl = Var->getTemplateInstantiationPattern();
  assert(PatternDecl && "no pattern for templated variable");
  MultiLevelTemplateArgumentList TemplateArgs =
      getTemplateInstantiationArgs(Var);

  VarTemplateSpecializationDecl *VarSpec =
      dyn_cast<VarTemplateSpecializationDecl>(Var);
  if (VarSpec) {
    // If this is a static data member template, there might be an
    // uninstantiated initializer on the declaration. If so, instantiate
    // it now.
    //
    // FIXME: This largely duplicates what we would do below. The difference
    // is that along this path we may instantiate an initializer from an
    // in-class declaration of the template and instantiate the definition
    // from a separate out-of-class definition.
    if (PatternDecl->isStaticDataMember() &&
        (PatternDecl = PatternDecl->getFirstDecl())->hasInit() &&
        !Var->hasInit()) {
      // FIXME: Factor out the duplicated instantiation context setup/tear down
      // code here.
      InstantiatingTemplate Inst(*this, PointOfInstantiation, Var);
      if (Inst.isInvalid() || Inst.isAlreadyInstantiating())
        return;
      PrettyDeclStackTraceEntry CrashInfo(Context, Var, SourceLocation(),
                                          "instantiating variable initializer");

      // The instantiation is visible here, even if it was first declared in an
      // unimported module.
      Var->setVisibleDespiteOwningModule();

      // If we're performing recursive template instantiation, create our own
      // queue of pending implicit instantiations that we will instantiate
      // later, while we're still within our own instantiation context.
      GlobalEagerInstantiationScope GlobalInstantiations(*this,
                                                         /*Enabled=*/Recursive);
      LocalInstantiationScope Local(*this);
      LocalEagerInstantiationScope LocalInstantiations(*this);

      // Enter the scope of this instantiation. We don't use
      // PushDeclContext because we don't have a scope.
      ContextRAII PreviousContext(*this, Var->getDeclContext());
      InstantiateVariableInitializer(Var, PatternDecl, TemplateArgs);
      PreviousContext.pop();

      // This variable may have local implicit instantiations that need to be
      // instantiated within this scope.
      LocalInstantiations.perform();
      Local.Exit();
      GlobalInstantiations.perform();
    }
  } else {
    assert(Var->isStaticDataMember() && PatternDecl->isStaticDataMember() &&
           "not a static data member?");
  }

  VarDecl *Def = PatternDecl->getDefinition(getASTContext());

  // If we don't have a definition of the variable template, we won't perform
  // any instantiation. Rather, we rely on the user to instantiate this
  // definition (or provide a specialization for it) in another translation
  // unit.
  if (!Def && !DefinitionRequired) {
    if (TSK == TSK_ExplicitInstantiationDefinition) {
      PendingInstantiations.push_back(
        std::make_pair(Var, PointOfInstantiation));
    } else if (TSK == TSK_ImplicitInstantiation) {
      // Warn about missing definition at the end of translation unit.
      if (AtEndOfTU && !getDiagnostics().hasErrorOccurred() &&
          !getSourceManager().isInSystemHeader(PatternDecl->getBeginLoc())) {
        Diag(PointOfInstantiation, diag::warn_var_template_missing)
          << Var;
        Diag(PatternDecl->getLocation(), diag::note_forward_template_decl);
        if (getLangOpts().CPlusPlus11)
          Diag(PointOfInstantiation, diag::note_inst_declaration_hint) << Var;
      }
      return;
    }
  }

  // FIXME: We need to track the instantiation stack in order to know which
  // definitions should be visible within this instantiation.
  // FIXME: Produce diagnostics when Var->getInstantiatedFromStaticDataMember().
  if (DiagnoseUninstantiableTemplate(PointOfInstantiation, Var,
                                     /*InstantiatedFromMember*/false,
                                     PatternDecl, Def, TSK,
                                     /*Complain*/DefinitionRequired))
    return;

  // C++11 [temp.explicit]p10:
  //   Except for inline functions, const variables of literal types, variables
  //   of reference types, [...] explicit instantiation declarations
  //   have the effect of suppressing the implicit instantiation of the entity
  //   to which they refer.
  //
  // FIXME: That's not exactly the same as "might be usable in constant
  // expressions", which only allows constexpr variables and const integral
  // types, not arbitrary const literal types.
  if (TSK == TSK_ExplicitInstantiationDeclaration &&
      !Var->mightBeUsableInConstantExpressions(getASTContext()))
    return;

  // Make sure to pass the instantiated variable to the consumer at the end.
  struct PassToConsumerRAII {
    ASTConsumer &Consumer;
    VarDecl *Var;

    PassToConsumerRAII(ASTConsumer &Consumer, VarDecl *Var)
      : Consumer(Consumer), Var(Var) { }

    ~PassToConsumerRAII() {
      Consumer.HandleCXXStaticMemberVarInstantiation(Var);
    }
  } PassToConsumerRAII(Consumer, Var);

  // If we already have a definition, we're done.
  if (VarDecl *Def = Var->getDefinition()) {
    // We may be explicitly instantiating something we've already implicitly
    // instantiated.
    Def->setTemplateSpecializationKind(Var->getTemplateSpecializationKind(),
                                       PointOfInstantiation);
    return;
  }

  InstantiatingTemplate Inst(*this, PointOfInstantiation, Var);
  if (Inst.isInvalid() || Inst.isAlreadyInstantiating())
    return;
  PrettyDeclStackTraceEntry CrashInfo(Context, Var, SourceLocation(),
                                      "instantiating variable definition");

  // If we're performing recursive template instantiation, create our own
  // queue of pending implicit instantiations that we will instantiate later,
  // while we're still within our own instantiation context.
  GlobalEagerInstantiationScope GlobalInstantiations(*this,
                                                     /*Enabled=*/Recursive);

  // Enter the scope of this instantiation. We don't use
  // PushDeclContext because we don't have a scope.
  ContextRAII PreviousContext(*this, Var->getDeclContext());
  LocalInstantiationScope Local(*this);

  LocalEagerInstantiationScope LocalInstantiations(*this);

  VarDecl *OldVar = Var;
  if (Def->isStaticDataMember() && !Def->isOutOfLine()) {
    // We're instantiating an inline static data member whose definition was
    // provided inside the class.
    InstantiateVariableInitializer(Var, Def, TemplateArgs);
  } else if (!VarSpec) {
    Var = cast_or_null<VarDecl>(SubstDecl(Def, Var->getDeclContext(),
                                          TemplateArgs));
  } else if (Var->isStaticDataMember() &&
             Var->getLexicalDeclContext()->isRecord()) {
    // We need to instantiate the definition of a static data member template,
    // and all we have is the in-class declaration of it. Instantiate a separate
    // declaration of the definition.
    TemplateDeclInstantiator Instantiator(*this, Var->getDeclContext(),
                                          TemplateArgs);

    TemplateArgumentListInfo TemplateArgInfo;
    if (const ASTTemplateArgumentListInfo *ArgInfo =
            VarSpec->getTemplateArgsAsWritten()) {
      TemplateArgInfo.setLAngleLoc(ArgInfo->getLAngleLoc());
      TemplateArgInfo.setRAngleLoc(ArgInfo->getRAngleLoc());
      for (const TemplateArgumentLoc &Arg : ArgInfo->arguments())
        TemplateArgInfo.addArgument(Arg);
    }

    Var = cast_or_null<VarDecl>(Instantiator.VisitVarTemplateSpecializationDecl(
        VarSpec->getSpecializedTemplate(), Def, TemplateArgInfo,
        VarSpec->getTemplateArgs().asArray(), VarSpec));
    if (Var) {
      llvm::PointerUnion<VarTemplateDecl *,
                         VarTemplatePartialSpecializationDecl *> PatternPtr =
          VarSpec->getSpecializedTemplateOrPartial();
      if (VarTemplatePartialSpecializationDecl *Partial =
          PatternPtr.dyn_cast<VarTemplatePartialSpecializationDecl *>())
        cast<VarTemplateSpecializationDecl>(Var)->setInstantiationOf(
            Partial, &VarSpec->getTemplateInstantiationArgs());

      // Attach the initializer.
      InstantiateVariableInitializer(Var, Def, TemplateArgs);
    }
  } else
    // Complete the existing variable's definition with an appropriately
    // substituted type and initializer.
    Var = CompleteVarTemplateSpecializationDecl(VarSpec, Def, TemplateArgs);

  PreviousContext.pop();

  if (Var) {
    PassToConsumerRAII.Var = Var;
    Var->setTemplateSpecializationKind(OldVar->getTemplateSpecializationKind(),
                                       OldVar->getPointOfInstantiation());
  }

  // This variable may have local implicit instantiations that need to be
  // instantiated within this scope.
  LocalInstantiations.perform();
  Local.Exit();
  GlobalInstantiations.perform();
}

void
Sema::InstantiateMemInitializers(CXXConstructorDecl *New,
                                 const CXXConstructorDecl *Tmpl,
                           const MultiLevelTemplateArgumentList &TemplateArgs) {

  SmallVector<CXXCtorInitializer*, 4> NewInits;
  bool AnyErrors = Tmpl->isInvalidDecl();

  // Instantiate all the initializers.
  for (const auto *Init : Tmpl->inits()) {
    // Only instantiate written initializers, let Sema re-construct implicit
    // ones.
    if (!Init->isWritten())
      continue;

    SourceLocation EllipsisLoc;

    if (Init->isPackExpansion()) {
      // This is a pack expansion. We should expand it now.
      TypeLoc BaseTL = Init->getTypeSourceInfo()->getTypeLoc();
      SmallVector<UnexpandedParameterPack, 4> Unexpanded;
      collectUnexpandedParameterPacks(BaseTL, Unexpanded);
      collectUnexpandedParameterPacks(Init->getInit(), Unexpanded);
      bool ShouldExpand = false;
      bool RetainExpansion = false;
      std::optional<unsigned> NumExpansions;
      if (CheckParameterPacksForExpansion(Init->getEllipsisLoc(),
                                          BaseTL.getSourceRange(),
                                          Unexpanded,
                                          TemplateArgs, ShouldExpand,
                                          RetainExpansion,
                                          NumExpansions)) {
        AnyErrors = true;
        New->setInvalidDecl();
        continue;
      }
      assert(ShouldExpand && "Partial instantiation of base initializer?");

      // Loop over all of the arguments in the argument pack(s),
      for (unsigned I = 0; I != *NumExpansions; ++I) {
        Sema::ArgumentPackSubstitutionIndexRAII SubstIndex(*this, I);

        // Instantiate the initializer.
        ExprResult TempInit = SubstInitializer(Init->getInit(), TemplateArgs,
                                               /*CXXDirectInit=*/true);
        if (TempInit.isInvalid()) {
          AnyErrors = true;
          break;
        }

        // Instantiate the base type.
        TypeSourceInfo *BaseTInfo = SubstType(Init->getTypeSourceInfo(),
                                              TemplateArgs,
                                              Init->getSourceLocation(),
                                              New->getDeclName());
        if (!BaseTInfo) {
          AnyErrors = true;
          break;
        }

        // Build the initializer.
        MemInitResult NewInit = BuildBaseInitializer(BaseTInfo->getType(),
                                                     BaseTInfo, TempInit.get(),
                                                     New->getParent(),
                                                     SourceLocation());
        if (NewInit.isInvalid()) {
          AnyErrors = true;
          break;
        }

        NewInits.push_back(NewInit.get());
      }

      continue;
    }

    // Instantiate the initializer.
    ExprResult TempInit = SubstInitializer(Init->getInit(), TemplateArgs,
                                           /*CXXDirectInit=*/true);
    if (TempInit.isInvalid()) {
      AnyErrors = true;
      continue;
    }

    MemInitResult NewInit;
    if (Init->isDelegatingInitializer() || Init->isBaseInitializer()) {
      TypeSourceInfo *TInfo = SubstType(Init->getTypeSourceInfo(),
                                        TemplateArgs,
                                        Init->getSourceLocation(),
                                        New->getDeclName());
      if (!TInfo) {
        AnyErrors = true;
        New->setInvalidDecl();
        continue;
      }

      if (Init->isBaseInitializer())
        NewInit = BuildBaseInitializer(TInfo->getType(), TInfo, TempInit.get(),
                                       New->getParent(), EllipsisLoc);
      else
        NewInit = BuildDelegatingInitializer(TInfo, TempInit.get(),
                                  cast<CXXRecordDecl>(CurContext->getParent()));
    } else if (Init->isMemberInitializer()) {
      FieldDecl *Member = cast_or_null<FieldDecl>(FindInstantiatedDecl(
                                                     Init->getMemberLocation(),
                                                     Init->getMember(),
                                                     TemplateArgs));
      if (!Member) {
        AnyErrors = true;
        New->setInvalidDecl();
        continue;
      }

      NewInit = BuildMemberInitializer(Member, TempInit.get(),
                                       Init->getSourceLocation());
    } else if (Init->isIndirectMemberInitializer()) {
      IndirectFieldDecl *IndirectMember =
         cast_or_null<IndirectFieldDecl>(FindInstantiatedDecl(
                                 Init->getMemberLocation(),
                                 Init->getIndirectMember(), TemplateArgs));

      if (!IndirectMember) {
        AnyErrors = true;
        New->setInvalidDecl();
        continue;
      }

      NewInit = BuildMemberInitializer(IndirectMember, TempInit.get(),
                                       Init->getSourceLocation());
    }

    if (NewInit.isInvalid()) {
      AnyErrors = true;
      New->setInvalidDecl();
    } else {
      NewInits.push_back(NewInit.get());
    }
  }

  // Assign all the initializers to the new constructor.
  ActOnMemInitializers(New,
                       /*FIXME: ColonLoc */
                       SourceLocation(),
                       NewInits,
                       AnyErrors);
}

// TODO: this could be templated if the various decl types used the
// same method name.
static bool isInstantiationOf(ClassTemplateDecl *Pattern,
                              ClassTemplateDecl *Instance) {
  Pattern = Pattern->getCanonicalDecl();

  do {
    Instance = Instance->getCanonicalDecl();
    if (Pattern == Instance) return true;
    Instance = Instance->getInstantiatedFromMemberTemplate();
  } while (Instance);

  return false;
}

static bool isInstantiationOf(FunctionTemplateDecl *Pattern,
                              FunctionTemplateDecl *Instance) {
  Pattern = Pattern->getCanonicalDecl();

  do {
    Instance = Instance->getCanonicalDecl();
    if (Pattern == Instance) return true;
    Instance = Instance->getInstantiatedFromMemberTemplate();
  } while (Instance);

  return false;
}

static bool
isInstantiationOf(ClassTemplatePartialSpecializationDecl *Pattern,
                  ClassTemplatePartialSpecializationDecl *Instance) {
  Pattern
    = cast<ClassTemplatePartialSpecializationDecl>(Pattern->getCanonicalDecl());
  do {
    Instance = cast<ClassTemplatePartialSpecializationDecl>(
                                                Instance->getCanonicalDecl());
    if (Pattern == Instance)
      return true;
    Instance = Instance->getInstantiatedFromMember();
  } while (Instance);

  return false;
}

static bool isInstantiationOf(CXXRecordDecl *Pattern,
                              CXXRecordDecl *Instance) {
  Pattern = Pattern->getCanonicalDecl();

  do {
    Instance = Instance->getCanonicalDecl();
    if (Pattern == Instance) return true;
    Instance = Instance->getInstantiatedFromMemberClass();
  } while (Instance);

  return false;
}

static bool isInstantiationOf(FunctionDecl *Pattern,
                              FunctionDecl *Instance) {
  Pattern = Pattern->getCanonicalDecl();

  do {
    Instance = Instance->getCanonicalDecl();
    if (Pattern == Instance) return true;
    Instance = Instance->getInstantiatedFromMemberFunction();
  } while (Instance);

  return false;
}

static bool isInstantiationOf(EnumDecl *Pattern,
                              EnumDecl *Instance) {
  Pattern = Pattern->getCanonicalDecl();

  do {
    Instance = Instance->getCanonicalDecl();
    if (Pattern == Instance) return true;
    Instance = Instance->getInstantiatedFromMemberEnum();
  } while (Instance);

  return false;
}

static bool isInstantiationOf(UsingShadowDecl *Pattern,
                              UsingShadowDecl *Instance,
                              ASTContext &C) {
  return declaresSameEntity(C.getInstantiatedFromUsingShadowDecl(Instance),
                            Pattern);
}

static bool isInstantiationOf(UsingDecl *Pattern, UsingDecl *Instance,
                              ASTContext &C) {
  return declaresSameEntity(C.getInstantiatedFromUsingDecl(Instance), Pattern);
}

template<typename T>
static bool isInstantiationOfUnresolvedUsingDecl(T *Pattern, Decl *Other,
                                                 ASTContext &Ctx) {
  // An unresolved using declaration can instantiate to an unresolved using
  // declaration, or to a using declaration or a using declaration pack.
  //
  // Multiple declarations can claim to be instantiated from an unresolved
  // using declaration if it's a pack expansion. We want the UsingPackDecl
  // in that case, not the individual UsingDecls within the pack.
  bool OtherIsPackExpansion;
  NamedDecl *OtherFrom;
  if (auto *OtherUUD = dyn_cast<T>(Other)) {
    OtherIsPackExpansion = OtherUUD->isPackExpansion();
    OtherFrom = Ctx.getInstantiatedFromUsingDecl(OtherUUD);
  } else if (auto *OtherUPD = dyn_cast<UsingPackDecl>(Other)) {
    OtherIsPackExpansion = true;
    OtherFrom = OtherUPD->getInstantiatedFromUsingDecl();
  } else if (auto *OtherUD = dyn_cast<UsingDecl>(Other)) {
    OtherIsPackExpansion = false;
    OtherFrom = Ctx.getInstantiatedFromUsingDecl(OtherUD);
  } else {
    return false;
  }
  return Pattern->isPackExpansion() == OtherIsPackExpansion &&
         declaresSameEntity(OtherFrom, Pattern);
}

static bool isInstantiationOfStaticDataMember(VarDecl *Pattern,
                                              VarDecl *Instance) {
  assert(Instance->isStaticDataMember());

  Pattern = Pattern->getCanonicalDecl();

  do {
    Instance = Instance->getCanonicalDecl();
    if (Pattern == Instance) return true;
    Instance = Instance->getInstantiatedFromStaticDataMember();
  } while (Instance);

  return false;
}

// Other is the prospective instantiation
// D is the prospective pattern
static bool isInstantiationOf(ASTContext &Ctx, NamedDecl *D, Decl *Other) {
  if (auto *UUD = dyn_cast<UnresolvedUsingTypenameDecl>(D))
    return isInstantiationOfUnresolvedUsingDecl(UUD, Other, Ctx);

  if (auto *UUD = dyn_cast<UnresolvedUsingValueDecl>(D))
    return isInstantiationOfUnresolvedUsingDecl(UUD, Other, Ctx);

  if (D->getKind() != Other->getKind())
    return false;

  if (auto *Record = dyn_cast<CXXRecordDecl>(Other))
    return isInstantiationOf(cast<CXXRecordDecl>(D), Record);

  if (auto *Function = dyn_cast<FunctionDecl>(Other))
    return isInstantiationOf(cast<FunctionDecl>(D), Function);

  if (auto *Enum = dyn_cast<EnumDecl>(Other))
    return isInstantiationOf(cast<EnumDecl>(D), Enum);

  if (auto *Var = dyn_cast<VarDecl>(Other))
    if (Var->isStaticDataMember())
      return isInstantiationOfStaticDataMember(cast<VarDecl>(D), Var);

  if (auto *Temp = dyn_cast<ClassTemplateDecl>(Other))
    return isInstantiationOf(cast<ClassTemplateDecl>(D), Temp);

  if (auto *Temp = dyn_cast<FunctionTemplateDecl>(Other))
    return isInstantiationOf(cast<FunctionTemplateDecl>(D), Temp);

  if (auto *PartialSpec =
          dyn_cast<ClassTemplatePartialSpecializationDecl>(Other))
    return isInstantiationOf(cast<ClassTemplatePartialSpecializationDecl>(D),
                             PartialSpec);

  if (auto *Field = dyn_cast<FieldDecl>(Other)) {
    if (!Field->getDeclName()) {
      // This is an unnamed field.
      return declaresSameEntity(Ctx.getInstantiatedFromUnnamedFieldDecl(Field),
                                cast<FieldDecl>(D));
    }
  }

  if (auto *Using = dyn_cast<UsingDecl>(Other))
    return isInstantiationOf(cast<UsingDecl>(D), Using, Ctx);

  if (auto *Shadow = dyn_cast<UsingShadowDecl>(Other))
    return isInstantiationOf(cast<UsingShadowDecl>(D), Shadow, Ctx);

  return D->getDeclName() &&
         D->getDeclName() == cast<NamedDecl>(Other)->getDeclName();
}

template<typename ForwardIterator>
static NamedDecl *findInstantiationOf(ASTContext &Ctx,
                                      NamedDecl *D,
                                      ForwardIterator first,
                                      ForwardIterator last) {
  for (; first != last; ++first)
    if (isInstantiationOf(Ctx, D, *first))
      return cast<NamedDecl>(*first);

  return nullptr;
}

DeclContext *Sema::FindInstantiatedContext(SourceLocation Loc, DeclContext* DC,
                          const MultiLevelTemplateArgumentList &TemplateArgs) {
  if (NamedDecl *D = dyn_cast<NamedDecl>(DC)) {
    Decl* ID = FindInstantiatedDecl(Loc, D, TemplateArgs, true);
    return cast_or_null<DeclContext>(ID);
  } else return DC;
}

/// Determine whether the given context is dependent on template parameters at
/// level \p Level or below.
///
/// Sometimes we only substitute an inner set of template arguments and leave
/// the outer templates alone. In such cases, contexts dependent only on the
/// outer levels are not effectively dependent.
static bool isDependentContextAtLevel(DeclContext *DC, unsigned Level) {
  if (!DC->isDependentContext())
    return false;
  if (!Level)
    return true;
  return cast<Decl>(DC)->getTemplateDepth() > Level;
}

NamedDecl *Sema::FindInstantiatedDecl(SourceLocation Loc, NamedDecl *D,
                          const MultiLevelTemplateArgumentList &TemplateArgs,
                          bool FindingInstantiatedContext) {
  DeclContext *ParentDC = D->getDeclContext();
  // Determine whether our parent context depends on any of the template
  // arguments we're currently substituting.
  bool ParentDependsOnArgs = isDependentContextAtLevel(
      ParentDC, TemplateArgs.getNumRetainedOuterLevels());
  // FIXME: Parameters of pointer to functions (y below) that are themselves
  // parameters (p below) can have their ParentDC set to the translation-unit
  // - thus we can not consistently check if the ParentDC of such a parameter
  // is Dependent or/and a FunctionOrMethod.
  // For e.g. this code, during Template argument deduction tries to
  // find an instantiated decl for (T y) when the ParentDC for y is
  // the translation unit.
  //   e.g. template <class T> void Foo(auto (*p)(T y) -> decltype(y())) {}
  //   float baz(float(*)()) { return 0.0; }
  //   Foo(baz);
  // The better fix here is perhaps to ensure that a ParmVarDecl, by the time
  // it gets here, always has a FunctionOrMethod as its ParentDC??
  // For now:
  //  - as long as we have a ParmVarDecl whose parent is non-dependent and
  //    whose type is not instantiation dependent, do nothing to the decl
  //  - otherwise find its instantiated decl.
  if (isa<ParmVarDecl>(D) && !ParentDependsOnArgs &&
      !cast<ParmVarDecl>(D)->getType()->isInstantiationDependentType())
    return D;
  if (isa<ParmVarDecl>(D) || isa<NonTypeTemplateParmDecl>(D) ||
      isa<TemplateTypeParmDecl>(D) || isa<TemplateTemplateParmDecl>(D) ||
      (ParentDependsOnArgs && (ParentDC->isFunctionOrMethod() ||
                               isa<OMPDeclareReductionDecl>(ParentDC) ||
                               isa<OMPDeclareMapperDecl>(ParentDC))) ||
      (isa<CXXRecordDecl>(D) && cast<CXXRecordDecl>(D)->isLambda() &&
       cast<CXXRecordDecl>(D)->getTemplateDepth() >
           TemplateArgs.getNumRetainedOuterLevels())) {
    // D is a local of some kind. Look into the map of local
    // declarations to their instantiations.
    if (CurrentInstantiationScope) {
      if (auto Found = CurrentInstantiationScope->findInstantiationOf(D)) {
        if (Decl *FD = Found->dyn_cast<Decl *>())
          return cast<NamedDecl>(FD);

        int PackIdx = ArgumentPackSubstitutionIndex;
        assert(PackIdx != -1 &&
               "found declaration pack but not pack expanding");
        typedef LocalInstantiationScope::DeclArgumentPack DeclArgumentPack;
        return cast<NamedDecl>((*Found->get<DeclArgumentPack *>())[PackIdx]);
      }
    }

    // If we're performing a partial substitution during template argument
    // deduction, we may not have values for template parameters yet. They
    // just map to themselves.
    if (isa<NonTypeTemplateParmDecl>(D) || isa<TemplateTypeParmDecl>(D) ||
        isa<TemplateTemplateParmDecl>(D))
      return D;

    if (D->isInvalidDecl())
      return nullptr;

    // Normally this function only searches for already instantiated declaration
    // however we have to make an exclusion for local types used before
    // definition as in the code:
    //
    //   template<typename T> void f1() {
    //     void g1(struct x1);
    //     struct x1 {};
    //   }
    //
    // In this case instantiation of the type of 'g1' requires definition of
    // 'x1', which is defined later. Error recovery may produce an enum used
    // before definition. In these cases we need to instantiate relevant
    // declarations here.
    bool NeedInstantiate = false;
    if (CXXRecordDecl *RD = dyn_cast<CXXRecordDecl>(D))
      NeedInstantiate = RD->isLocalClass();
    else if (isa<TypedefNameDecl>(D) &&
             isa<CXXDeductionGuideDecl>(D->getDeclContext()))
      NeedInstantiate = true;
    else
      NeedInstantiate = isa<EnumDecl>(D);
    if (NeedInstantiate) {
      Decl *Inst = SubstDecl(D, CurContext, TemplateArgs);
      CurrentInstantiationScope->InstantiatedLocal(D, Inst);
      return cast<TypeDecl>(Inst);
    }

    // If we didn't find the decl, then we must have a label decl that hasn't
    // been found yet.  Lazily instantiate it and return it now.
    assert(isa<LabelDecl>(D));

    Decl *Inst = SubstDecl(D, CurContext, TemplateArgs);
    assert(Inst && "Failed to instantiate label??");

    CurrentInstantiationScope->InstantiatedLocal(D, Inst);
    return cast<LabelDecl>(Inst);
  }

  if (CXXRecordDecl *Record = dyn_cast<CXXRecordDecl>(D)) {
    if (!Record->isDependentContext())
      return D;

    // Determine whether this record is the "templated" declaration describing
    // a class template or class template specialization.
    ClassTemplateDecl *ClassTemplate = Record->getDescribedClassTemplate();
    if (ClassTemplate)
      ClassTemplate = ClassTemplate->getCanonicalDecl();
    else if (ClassTemplateSpecializationDecl *Spec =
                 dyn_cast<ClassTemplateSpecializationDecl>(Record))
      ClassTemplate = Spec->getSpecializedTemplate()->getCanonicalDecl();

    // Walk the current context to find either the record or an instantiation of
    // it.
    DeclContext *DC = CurContext;
    while (!DC->isFileContext()) {
      // If we're performing substitution while we're inside the template
      // definition, we'll find our own context. We're done.
      if (DC->Equals(Record))
        return Record;

      if (CXXRecordDecl *InstRecord = dyn_cast<CXXRecordDecl>(DC)) {
        // Check whether we're in the process of instantiating a class template
        // specialization of the template we're mapping.
        if (ClassTemplateSpecializationDecl *InstSpec
                      = dyn_cast<ClassTemplateSpecializationDecl>(InstRecord)){
          ClassTemplateDecl *SpecTemplate = InstSpec->getSpecializedTemplate();
          if (ClassTemplate && isInstantiationOf(ClassTemplate, SpecTemplate))
            return InstRecord;
        }

        // Check whether we're in the process of instantiating a member class.
        if (isInstantiationOf(Record, InstRecord))
          return InstRecord;
      }

      // Move to the outer template scope.
      if (FunctionDecl *FD = dyn_cast<FunctionDecl>(DC)) {
        if (FD->getFriendObjectKind() &&
            FD->getNonTransparentDeclContext()->isFileContext()) {
          DC = FD->getLexicalDeclContext();
          continue;
        }
        // An implicit deduction guide acts as if it's within the class template
        // specialization described by its name and first N template params.
        auto *Guide = dyn_cast<CXXDeductionGuideDecl>(FD);
        if (Guide && Guide->isImplicit()) {
          TemplateDecl *TD = Guide->getDeducedTemplate();
          // Convert the arguments to an "as-written" list.
          TemplateArgumentListInfo Args(Loc, Loc);
          for (TemplateArgument Arg : TemplateArgs.getInnermost().take_front(
                                        TD->getTemplateParameters()->size())) {
            ArrayRef<TemplateArgument> Unpacked(Arg);
            if (Arg.getKind() == TemplateArgument::Pack)
              Unpacked = Arg.pack_elements();
            for (TemplateArgument UnpackedArg : Unpacked)
              Args.addArgument(
                  getTrivialTemplateArgumentLoc(UnpackedArg, QualType(), Loc));
          }
          QualType T = CheckTemplateIdType(TemplateName(TD), Loc, Args);
          // We may get a non-null type with errors, in which case
          // `getAsCXXRecordDecl` will return `nullptr`. For instance, this
          // happens when one of the template arguments is an invalid
          // expression. We return early to avoid triggering the assertion
          // about the `CodeSynthesisContext`.
          if (T.isNull() || T->containsErrors())
            return nullptr;
          CXXRecordDecl *SubstRecord = T->getAsCXXRecordDecl();

          if (!SubstRecord) {
            // T can be a dependent TemplateSpecializationType when performing a
            // substitution for building a deduction guide.
            assert(CodeSynthesisContexts.back().Kind ==
                   CodeSynthesisContext::BuildingDeductionGuides);
            // Return a nullptr as a sentinel value, we handle it properly in
            // the TemplateInstantiator::TransformInjectedClassNameType
            // override, which we transform it to a TemplateSpecializationType.
            return nullptr;
          }
          // Check that this template-id names the primary template and not a
          // partial or explicit specialization. (In the latter cases, it's
          // meaningless to attempt to find an instantiation of D within the
          // specialization.)
          // FIXME: The standard doesn't say what should happen here.
          if (FindingInstantiatedContext &&
              usesPartialOrExplicitSpecialization(
                  Loc, cast<ClassTemplateSpecializationDecl>(SubstRecord))) {
            Diag(Loc, diag::err_specialization_not_primary_template)
              << T << (SubstRecord->getTemplateSpecializationKind() ==
                           TSK_ExplicitSpecialization);
            return nullptr;
          }
          DC = SubstRecord;
          continue;
        }
      }

      DC = DC->getParent();
    }

    // Fall through to deal with other dependent record types (e.g.,
    // anonymous unions in class templates).
  }

  if (!ParentDependsOnArgs)
    return D;

  ParentDC = FindInstantiatedContext(Loc, ParentDC, TemplateArgs);
  if (!ParentDC)
    return nullptr;

  if (ParentDC != D->getDeclContext()) {
    // We performed some kind of instantiation in the parent context,
    // so now we need to look into the instantiated parent context to
    // find the instantiation of the declaration D.

    // If our context used to be dependent, we may need to instantiate
    // it before performing lookup into that context.
    bool IsBeingInstantiated = false;
    if (CXXRecordDecl *Spec = dyn_cast<CXXRecordDecl>(ParentDC)) {
      if (!Spec->isDependentContext()) {
        QualType T = Context.getTypeDeclType(Spec);
        const RecordType *Tag = T->getAs<RecordType>();
        assert(Tag && "type of non-dependent record is not a RecordType");
        if (Tag->isBeingDefined())
          IsBeingInstantiated = true;
        if (!Tag->isBeingDefined() &&
            RequireCompleteType(Loc, T, diag::err_incomplete_type))
          return nullptr;

        ParentDC = Tag->getDecl();
      }
    }

    NamedDecl *Result = nullptr;
    // FIXME: If the name is a dependent name, this lookup won't necessarily
    // find it. Does that ever matter?
    if (auto Name = D->getDeclName()) {
      DeclarationNameInfo NameInfo(Name, D->getLocation());
      DeclarationNameInfo NewNameInfo =
          SubstDeclarationNameInfo(NameInfo, TemplateArgs);
      Name = NewNameInfo.getName();
      if (!Name)
        return nullptr;
      DeclContext::lookup_result Found = ParentDC->lookup(Name);

      Result = findInstantiationOf(Context, D, Found.begin(), Found.end());
    } else {
      // Since we don't have a name for the entity we're looking for,
      // our only option is to walk through all of the declarations to
      // find that name. This will occur in a few cases:
      //
      //   - anonymous struct/union within a template
      //   - unnamed class/struct/union/enum within a template
      //
      // FIXME: Find a better way to find these instantiations!
      Result = findInstantiationOf(Context, D,
                                   ParentDC->decls_begin(),
                                   ParentDC->decls_end());
    }

    if (!Result) {
      if (isa<UsingShadowDecl>(D)) {
        // UsingShadowDecls can instantiate to nothing because of using hiding.
      } else if (hasUncompilableErrorOccurred()) {
        // We've already complained about some ill-formed code, so most likely
        // this declaration failed to instantiate. There's no point in
        // complaining further, since this is normal in invalid code.
        // FIXME: Use more fine-grained 'invalid' tracking for this.
      } else if (IsBeingInstantiated) {
        // The class in which this member exists is currently being
        // instantiated, and we haven't gotten around to instantiating this
        // member yet. This can happen when the code uses forward declarations
        // of member classes, and introduces ordering dependencies via
        // template instantiation.
        Diag(Loc, diag::err_member_not_yet_instantiated)
          << D->getDeclName()
          << Context.getTypeDeclType(cast<CXXRecordDecl>(ParentDC));
        Diag(D->getLocation(), diag::note_non_instantiated_member_here);
      } else if (EnumConstantDecl *ED = dyn_cast<EnumConstantDecl>(D)) {
        // This enumeration constant was found when the template was defined,
        // but can't be found in the instantiation. This can happen if an
        // unscoped enumeration member is explicitly specialized.
        EnumDecl *Enum = cast<EnumDecl>(ED->getLexicalDeclContext());
        EnumDecl *Spec = cast<EnumDecl>(FindInstantiatedDecl(Loc, Enum,
                                                             TemplateArgs));
        assert(Spec->getTemplateSpecializationKind() ==
                 TSK_ExplicitSpecialization);
        Diag(Loc, diag::err_enumerator_does_not_exist)
          << D->getDeclName()
          << Context.getTypeDeclType(cast<TypeDecl>(Spec->getDeclContext()));
        Diag(Spec->getLocation(), diag::note_enum_specialized_here)
          << Context.getTypeDeclType(Spec);
      } else {
        // We should have found something, but didn't.
        llvm_unreachable("Unable to find instantiation of declaration!");
      }
    }

    D = Result;
  }

  return D;
}

void Sema::PerformPendingInstantiations(bool LocalOnly) {
  std::deque<PendingImplicitInstantiation> delayedPCHInstantiations;
  while (!PendingLocalImplicitInstantiations.empty() ||
         (!LocalOnly && !PendingInstantiations.empty())) {
    PendingImplicitInstantiation Inst;

    if (PendingLocalImplicitInstantiations.empty()) {
      Inst = PendingInstantiations.front();
      PendingInstantiations.pop_front();
    } else {
      Inst = PendingLocalImplicitInstantiations.front();
      PendingLocalImplicitInstantiations.pop_front();
    }

    // Instantiate function definitions
    if (FunctionDecl *Function = dyn_cast<FunctionDecl>(Inst.first)) {
      bool DefinitionRequired = Function->getTemplateSpecializationKind() ==
                                TSK_ExplicitInstantiationDefinition;
      if (Function->isMultiVersion()) {
        getASTContext().forEachMultiversionedFunctionVersion(
            Function, [this, Inst, DefinitionRequired](FunctionDecl *CurFD) {
              InstantiateFunctionDefinition(/*FIXME:*/ Inst.second, CurFD, true,
                                            DefinitionRequired, true);
              if (CurFD->isDefined())
                CurFD->setInstantiationIsPending(false);
            });
      } else {
        InstantiateFunctionDefinition(/*FIXME:*/ Inst.second, Function, true,
                                      DefinitionRequired, true);
        if (Function->isDefined())
          Function->setInstantiationIsPending(false);
      }
      // Definition of a PCH-ed template declaration may be available only in the TU.
      if (!LocalOnly && LangOpts.PCHInstantiateTemplates &&
          TUKind == TU_Prefix && Function->instantiationIsPending())
        delayedPCHInstantiations.push_back(Inst);
      continue;
    }

    // Instantiate variable definitions
    VarDecl *Var = cast<VarDecl>(Inst.first);

    assert((Var->isStaticDataMember() ||
            isa<VarTemplateSpecializationDecl>(Var)) &&
           "Not a static data member, nor a variable template"
           " specialization?");

    // Don't try to instantiate declarations if the most recent redeclaration
    // is invalid.
    if (Var->getMostRecentDecl()->isInvalidDecl())
      continue;

    // Check if the most recent declaration has changed the specialization kind
    // and removed the need for implicit instantiation.
    switch (Var->getMostRecentDecl()
                ->getTemplateSpecializationKindForInstantiation()) {
    case TSK_Undeclared:
      llvm_unreachable("Cannot instantitiate an undeclared specialization.");
    case TSK_ExplicitInstantiationDeclaration:
    case TSK_ExplicitSpecialization:
      continue;  // No longer need to instantiate this type.
    case TSK_ExplicitInstantiationDefinition:
      // We only need an instantiation if the pending instantiation *is* the
      // explicit instantiation.
      if (Var != Var->getMostRecentDecl())
        continue;
      break;
    case TSK_ImplicitInstantiation:
      break;
    }

    PrettyDeclStackTraceEntry CrashInfo(Context, Var, SourceLocation(),
                                        "instantiating variable definition");
    bool DefinitionRequired = Var->getTemplateSpecializationKind() ==
                              TSK_ExplicitInstantiationDefinition;

    // Instantiate static data member definitions or variable template
    // specializations.
    InstantiateVariableDefinition(/*FIXME:*/ Inst.second, Var, true,
                                  DefinitionRequired, true);
  }

  if (!LocalOnly && LangOpts.PCHInstantiateTemplates)
    PendingInstantiations.swap(delayedPCHInstantiations);
}

void Sema::PerformDependentDiagnostics(const DeclContext *Pattern,
                       const MultiLevelTemplateArgumentList &TemplateArgs) {
  for (auto *DD : Pattern->ddiags()) {
    switch (DD->getKind()) {
    case DependentDiagnostic::Access:
      HandleDependentAccessCheck(*DD, TemplateArgs);
      break;
    }
  }
}
