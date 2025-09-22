//===--- SemaExpr.cpp - Semantic Analysis for Expressions -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file implements semantic analysis for expressions.
//
//===----------------------------------------------------------------------===//

#include "CheckExprLifetime.h"
#include "TreeTransform.h"
#include "UsedDeclVisitor.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTLambda.h"
#include "clang/AST/ASTMutationListener.h"
#include "clang/AST/CXXInheritance.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/EvaluatedExprVisitor.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ExprObjC.h"
#include "clang/AST/ExprOpenMP.h"
#include "clang/AST/OperationKinds.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/Type.h"
#include "clang/AST/TypeLoc.h"
#include "clang/Basic/Builtins.h"
#include "clang/Basic/DiagnosticSema.h"
#include "clang/Basic/PartialDiagnostic.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/Specifiers.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Basic/TypeTraits.h"
#include "clang/Lex/LiteralSupport.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Sema/AnalysisBasedWarnings.h"
#include "clang/Sema/DeclSpec.h"
#include "clang/Sema/DelayedDiagnostic.h"
#include "clang/Sema/Designator.h"
#include "clang/Sema/EnterExpressionEvaluationContext.h"
#include "clang/Sema/Initialization.h"
#include "clang/Sema/Lookup.h"
#include "clang/Sema/Overload.h"
#include "clang/Sema/ParsedTemplate.h"
#include "clang/Sema/Scope.h"
#include "clang/Sema/ScopeInfo.h"
#include "clang/Sema/SemaCUDA.h"
#include "clang/Sema/SemaFixItUtils.h"
#include "clang/Sema/SemaInternal.h"
#include "clang/Sema/SemaObjC.h"
#include "clang/Sema/SemaOpenMP.h"
#include "clang/Sema/SemaPseudoObject.h"
#include "clang/Sema/Template.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/STLForwardCompat.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/SaveAndRestore.h"
#include "llvm/Support/TypeSize.h"
#include <optional>

using namespace clang;
using namespace sema;

bool Sema::CanUseDecl(NamedDecl *D, bool TreatUnavailableAsInvalid) {
  // See if this is an auto-typed variable whose initializer we are parsing.
  if (ParsingInitForAutoVars.count(D))
    return false;

  // See if this is a deleted function.
  if (FunctionDecl *FD = dyn_cast<FunctionDecl>(D)) {
    if (FD->isDeleted())
      return false;

    // If the function has a deduced return type, and we can't deduce it,
    // then we can't use it either.
    if (getLangOpts().CPlusPlus14 && FD->getReturnType()->isUndeducedType() &&
        DeduceReturnType(FD, SourceLocation(), /*Diagnose*/ false))
      return false;

    // See if this is an aligned allocation/deallocation function that is
    // unavailable.
    if (TreatUnavailableAsInvalid &&
        isUnavailableAlignedAllocationFunction(*FD))
      return false;
  }

  // See if this function is unavailable.
  if (TreatUnavailableAsInvalid && D->getAvailability() == AR_Unavailable &&
      cast<Decl>(CurContext)->getAvailability() != AR_Unavailable)
    return false;

  if (isa<UnresolvedUsingIfExistsDecl>(D))
    return false;

  return true;
}

static void DiagnoseUnusedOfDecl(Sema &S, NamedDecl *D, SourceLocation Loc) {
  // Warn if this is used but marked unused.
  if (const auto *A = D->getAttr<UnusedAttr>()) {
    // [[maybe_unused]] should not diagnose uses, but __attribute__((unused))
    // should diagnose them.
    if (A->getSemanticSpelling() != UnusedAttr::CXX11_maybe_unused &&
        A->getSemanticSpelling() != UnusedAttr::C23_maybe_unused) {
      const Decl *DC = cast_or_null<Decl>(S.ObjC().getCurObjCLexicalContext());
      if (DC && !DC->hasAttr<UnusedAttr>())
        S.Diag(Loc, diag::warn_used_but_marked_unused) << D;
    }
  }
}

void Sema::NoteDeletedFunction(FunctionDecl *Decl) {
  assert(Decl && Decl->isDeleted());

  if (Decl->isDefaulted()) {
    // If the method was explicitly defaulted, point at that declaration.
    if (!Decl->isImplicit())
      Diag(Decl->getLocation(), diag::note_implicitly_deleted);

    // Try to diagnose why this special member function was implicitly
    // deleted. This might fail, if that reason no longer applies.
    DiagnoseDeletedDefaultedFunction(Decl);
    return;
  }

  auto *Ctor = dyn_cast<CXXConstructorDecl>(Decl);
  if (Ctor && Ctor->isInheritingConstructor())
    return NoteDeletedInheritingConstructor(Ctor);

  Diag(Decl->getLocation(), diag::note_availability_specified_here)
    << Decl << 1;
}

/// Determine whether a FunctionDecl was ever declared with an
/// explicit storage class.
static bool hasAnyExplicitStorageClass(const FunctionDecl *D) {
  for (auto *I : D->redecls()) {
    if (I->getStorageClass() != SC_None)
      return true;
  }
  return false;
}

/// Check whether we're in an extern inline function and referring to a
/// variable or function with internal linkage (C11 6.7.4p3).
///
/// This is only a warning because we used to silently accept this code, but
/// in many cases it will not behave correctly. This is not enabled in C++ mode
/// because the restriction language is a bit weaker (C++11 [basic.def.odr]p6)
/// and so while there may still be user mistakes, most of the time we can't
/// prove that there are errors.
static void diagnoseUseOfInternalDeclInInlineFunction(Sema &S,
                                                      const NamedDecl *D,
                                                      SourceLocation Loc) {
  // This is disabled under C++; there are too many ways for this to fire in
  // contexts where the warning is a false positive, or where it is technically
  // correct but benign.
  if (S.getLangOpts().CPlusPlus)
    return;

  // Check if this is an inlined function or method.
  FunctionDecl *Current = S.getCurFunctionDecl();
  if (!Current)
    return;
  if (!Current->isInlined())
    return;
  if (!Current->isExternallyVisible())
    return;

  // Check if the decl has internal linkage.
  if (D->getFormalLinkage() != Linkage::Internal)
    return;

  // Downgrade from ExtWarn to Extension if
  //  (1) the supposedly external inline function is in the main file,
  //      and probably won't be included anywhere else.
  //  (2) the thing we're referencing is a pure function.
  //  (3) the thing we're referencing is another inline function.
  // This last can give us false negatives, but it's better than warning on
  // wrappers for simple C library functions.
  const FunctionDecl *UsedFn = dyn_cast<FunctionDecl>(D);
  bool DowngradeWarning = S.getSourceManager().isInMainFile(Loc);
  if (!DowngradeWarning && UsedFn)
    DowngradeWarning = UsedFn->isInlined() || UsedFn->hasAttr<ConstAttr>();

  S.Diag(Loc, DowngradeWarning ? diag::ext_internal_in_extern_inline_quiet
                               : diag::ext_internal_in_extern_inline)
    << /*IsVar=*/!UsedFn << D;

  S.MaybeSuggestAddingStaticToDecl(Current);

  S.Diag(D->getCanonicalDecl()->getLocation(), diag::note_entity_declared_at)
      << D;
}

void Sema::MaybeSuggestAddingStaticToDecl(const FunctionDecl *Cur) {
  const FunctionDecl *First = Cur->getFirstDecl();

  // Suggest "static" on the function, if possible.
  if (!hasAnyExplicitStorageClass(First)) {
    SourceLocation DeclBegin = First->getSourceRange().getBegin();
    Diag(DeclBegin, diag::note_convert_inline_to_static)
      << Cur << FixItHint::CreateInsertion(DeclBegin, "static ");
  }
}

bool Sema::DiagnoseUseOfDecl(NamedDecl *D, ArrayRef<SourceLocation> Locs,
                             const ObjCInterfaceDecl *UnknownObjCClass,
                             bool ObjCPropertyAccess,
                             bool AvoidPartialAvailabilityChecks,
                             ObjCInterfaceDecl *ClassReceiver,
                             bool SkipTrailingRequiresClause) {
  SourceLocation Loc = Locs.front();
  if (getLangOpts().CPlusPlus && isa<FunctionDecl>(D)) {
    // If there were any diagnostics suppressed by template argument deduction,
    // emit them now.
    auto Pos = SuppressedDiagnostics.find(D->getCanonicalDecl());
    if (Pos != SuppressedDiagnostics.end()) {
      for (const PartialDiagnosticAt &Suppressed : Pos->second)
        Diag(Suppressed.first, Suppressed.second);

      // Clear out the list of suppressed diagnostics, so that we don't emit
      // them again for this specialization. However, we don't obsolete this
      // entry from the table, because we want to avoid ever emitting these
      // diagnostics again.
      Pos->second.clear();
    }

    // C++ [basic.start.main]p3:
    //   The function 'main' shall not be used within a program.
    if (cast<FunctionDecl>(D)->isMain())
      Diag(Loc, diag::ext_main_used);

    diagnoseUnavailableAlignedAllocation(*cast<FunctionDecl>(D), Loc);
  }

  // See if this is an auto-typed variable whose initializer we are parsing.
  if (ParsingInitForAutoVars.count(D)) {
    if (isa<BindingDecl>(D)) {
      Diag(Loc, diag::err_binding_cannot_appear_in_own_initializer)
        << D->getDeclName();
    } else {
      Diag(Loc, diag::err_auto_variable_cannot_appear_in_own_initializer)
        << D->getDeclName() << cast<VarDecl>(D)->getType();
    }
    return true;
  }

  if (FunctionDecl *FD = dyn_cast<FunctionDecl>(D)) {
    // See if this is a deleted function.
    if (FD->isDeleted()) {
      auto *Ctor = dyn_cast<CXXConstructorDecl>(FD);
      if (Ctor && Ctor->isInheritingConstructor())
        Diag(Loc, diag::err_deleted_inherited_ctor_use)
            << Ctor->getParent()
            << Ctor->getInheritedConstructor().getConstructor()->getParent();
      else {
        StringLiteral *Msg = FD->getDeletedMessage();
        Diag(Loc, diag::err_deleted_function_use)
            << (Msg != nullptr) << (Msg ? Msg->getString() : StringRef());
      }
      NoteDeletedFunction(FD);
      return true;
    }

    // [expr.prim.id]p4
    //   A program that refers explicitly or implicitly to a function with a
    //   trailing requires-clause whose constraint-expression is not satisfied,
    //   other than to declare it, is ill-formed. [...]
    //
    // See if this is a function with constraints that need to be satisfied.
    // Check this before deducing the return type, as it might instantiate the
    // definition.
    if (!SkipTrailingRequiresClause && FD->getTrailingRequiresClause()) {
      ConstraintSatisfaction Satisfaction;
      if (CheckFunctionConstraints(FD, Satisfaction, Loc,
                                   /*ForOverloadResolution*/ true))
        // A diagnostic will have already been generated (non-constant
        // constraint expression, for example)
        return true;
      if (!Satisfaction.IsSatisfied) {
        Diag(Loc,
             diag::err_reference_to_function_with_unsatisfied_constraints)
            << D;
        DiagnoseUnsatisfiedConstraint(Satisfaction);
        return true;
      }
    }

    // If the function has a deduced return type, and we can't deduce it,
    // then we can't use it either.
    if (getLangOpts().CPlusPlus14 && FD->getReturnType()->isUndeducedType() &&
        DeduceReturnType(FD, Loc))
      return true;

    if (getLangOpts().CUDA && !CUDA().CheckCall(Loc, FD))
      return true;

  }

  if (auto *MD = dyn_cast<CXXMethodDecl>(D)) {
    // Lambdas are only default-constructible or assignable in C++2a onwards.
    if (MD->getParent()->isLambda() &&
        ((isa<CXXConstructorDecl>(MD) &&
          cast<CXXConstructorDecl>(MD)->isDefaultConstructor()) ||
         MD->isCopyAssignmentOperator() || MD->isMoveAssignmentOperator())) {
      Diag(Loc, diag::warn_cxx17_compat_lambda_def_ctor_assign)
        << !isa<CXXConstructorDecl>(MD);
    }
  }

  auto getReferencedObjCProp = [](const NamedDecl *D) ->
                                      const ObjCPropertyDecl * {
    if (const auto *MD = dyn_cast<ObjCMethodDecl>(D))
      return MD->findPropertyDecl();
    return nullptr;
  };
  if (const ObjCPropertyDecl *ObjCPDecl = getReferencedObjCProp(D)) {
    if (diagnoseArgIndependentDiagnoseIfAttrs(ObjCPDecl, Loc))
      return true;
  } else if (diagnoseArgIndependentDiagnoseIfAttrs(D, Loc)) {
      return true;
  }

  // [OpenMP 4.0], 2.15 declare reduction Directive, Restrictions
  // Only the variables omp_in and omp_out are allowed in the combiner.
  // Only the variables omp_priv and omp_orig are allowed in the
  // initializer-clause.
  auto *DRD = dyn_cast<OMPDeclareReductionDecl>(CurContext);
  if (LangOpts.OpenMP && DRD && !CurContext->containsDecl(D) &&
      isa<VarDecl>(D)) {
    Diag(Loc, diag::err_omp_wrong_var_in_declare_reduction)
        << getCurFunction()->HasOMPDeclareReductionCombiner;
    Diag(D->getLocation(), diag::note_entity_declared_at) << D;
    return true;
  }

  // [OpenMP 5.0], 2.19.7.3. declare mapper Directive, Restrictions
  //  List-items in map clauses on this construct may only refer to the declared
  //  variable var and entities that could be referenced by a procedure defined
  //  at the same location.
  // [OpenMP 5.2] Also allow iterator declared variables.
  if (LangOpts.OpenMP && isa<VarDecl>(D) &&
      !OpenMP().isOpenMPDeclareMapperVarDeclAllowed(cast<VarDecl>(D))) {
    Diag(Loc, diag::err_omp_declare_mapper_wrong_var)
        << OpenMP().getOpenMPDeclareMapperVarName();
    Diag(D->getLocation(), diag::note_entity_declared_at) << D;
    return true;
  }

  if (const auto *EmptyD = dyn_cast<UnresolvedUsingIfExistsDecl>(D)) {
    Diag(Loc, diag::err_use_of_empty_using_if_exists);
    Diag(EmptyD->getLocation(), diag::note_empty_using_if_exists_here);
    return true;
  }

  DiagnoseAvailabilityOfDecl(D, Locs, UnknownObjCClass, ObjCPropertyAccess,
                             AvoidPartialAvailabilityChecks, ClassReceiver);

  DiagnoseUnusedOfDecl(*this, D, Loc);

  diagnoseUseOfInternalDeclInInlineFunction(*this, D, Loc);

  if (D->hasAttr<AvailableOnlyInDefaultEvalMethodAttr>()) {
    if (getLangOpts().getFPEvalMethod() !=
            LangOptions::FPEvalMethodKind::FEM_UnsetOnCommandLine &&
        PP.getLastFPEvalPragmaLocation().isValid() &&
        PP.getCurrentFPEvalMethod() != getLangOpts().getFPEvalMethod())
      Diag(D->getLocation(),
           diag::err_type_available_only_in_default_eval_method)
          << D->getName();
  }

  if (auto *VD = dyn_cast<ValueDecl>(D))
    checkTypeSupport(VD->getType(), Loc, VD);

  if (LangOpts.SYCLIsDevice ||
      (LangOpts.OpenMP && LangOpts.OpenMPIsTargetDevice)) {
    if (!Context.getTargetInfo().isTLSSupported())
      if (const auto *VD = dyn_cast<VarDecl>(D))
        if (VD->getTLSKind() != VarDecl::TLS_None)
          targetDiag(*Locs.begin(), diag::err_thread_unsupported);
  }

  if (isa<ParmVarDecl>(D) && isa<RequiresExprBodyDecl>(D->getDeclContext()) &&
      !isUnevaluatedContext()) {
    // C++ [expr.prim.req.nested] p3
    //   A local parameter shall only appear as an unevaluated operand
    //   (Clause 8) within the constraint-expression.
    Diag(Loc, diag::err_requires_expr_parameter_referenced_in_evaluated_context)
        << D;
    Diag(D->getLocation(), diag::note_entity_declared_at) << D;
    return true;
  }

  return false;
}

void Sema::DiagnoseSentinelCalls(const NamedDecl *D, SourceLocation Loc,
                                 ArrayRef<Expr *> Args) {
  const SentinelAttr *Attr = D->getAttr<SentinelAttr>();
  if (!Attr)
    return;

  // The number of formal parameters of the declaration.
  unsigned NumFormalParams;

  // The kind of declaration.  This is also an index into a %select in
  // the diagnostic.
  enum { CK_Function, CK_Method, CK_Block } CalleeKind;

  if (const auto *MD = dyn_cast<ObjCMethodDecl>(D)) {
    NumFormalParams = MD->param_size();
    CalleeKind = CK_Method;
  } else if (const auto *FD = dyn_cast<FunctionDecl>(D)) {
    NumFormalParams = FD->param_size();
    CalleeKind = CK_Function;
  } else if (const auto *VD = dyn_cast<VarDecl>(D)) {
    QualType Ty = VD->getType();
    const FunctionType *Fn = nullptr;
    if (const auto *PtrTy = Ty->getAs<PointerType>()) {
      Fn = PtrTy->getPointeeType()->getAs<FunctionType>();
      if (!Fn)
        return;
      CalleeKind = CK_Function;
    } else if (const auto *PtrTy = Ty->getAs<BlockPointerType>()) {
      Fn = PtrTy->getPointeeType()->castAs<FunctionType>();
      CalleeKind = CK_Block;
    } else {
      return;
    }

    if (const auto *proto = dyn_cast<FunctionProtoType>(Fn))
      NumFormalParams = proto->getNumParams();
    else
      NumFormalParams = 0;
  } else {
    return;
  }

  // "NullPos" is the number of formal parameters at the end which
  // effectively count as part of the variadic arguments.  This is
  // useful if you would prefer to not have *any* formal parameters,
  // but the language forces you to have at least one.
  unsigned NullPos = Attr->getNullPos();
  assert((NullPos == 0 || NullPos == 1) && "invalid null position on sentinel");
  NumFormalParams = (NullPos > NumFormalParams ? 0 : NumFormalParams - NullPos);

  // The number of arguments which should follow the sentinel.
  unsigned NumArgsAfterSentinel = Attr->getSentinel();

  // If there aren't enough arguments for all the formal parameters,
  // the sentinel, and the args after the sentinel, complain.
  if (Args.size() < NumFormalParams + NumArgsAfterSentinel + 1) {
    Diag(Loc, diag::warn_not_enough_argument) << D->getDeclName();
    Diag(D->getLocation(), diag::note_sentinel_here) << int(CalleeKind);
    return;
  }

  // Otherwise, find the sentinel expression.
  const Expr *SentinelExpr = Args[Args.size() - NumArgsAfterSentinel - 1];
  if (!SentinelExpr)
    return;
  if (SentinelExpr->isValueDependent())
    return;
  if (Context.isSentinelNullExpr(SentinelExpr))
    return;

  // Pick a reasonable string to insert.  Optimistically use 'nil', 'nullptr',
  // or 'NULL' if those are actually defined in the context.  Only use
  // 'nil' for ObjC methods, where it's much more likely that the
  // variadic arguments form a list of object pointers.
  SourceLocation MissingNilLoc = getLocForEndOfToken(SentinelExpr->getEndLoc());
  std::string NullValue;
  if (CalleeKind == CK_Method && PP.isMacroDefined("nil"))
    NullValue = "nil";
  else if (getLangOpts().CPlusPlus11)
    NullValue = "nullptr";
  else if (PP.isMacroDefined("NULL"))
    NullValue = "NULL";
  else
    NullValue = "(void*) 0";

  if (MissingNilLoc.isInvalid())
    Diag(Loc, diag::warn_missing_sentinel) << int(CalleeKind);
  else
    Diag(MissingNilLoc, diag::warn_missing_sentinel)
        << int(CalleeKind)
        << FixItHint::CreateInsertion(MissingNilLoc, ", " + NullValue);
  Diag(D->getLocation(), diag::note_sentinel_here)
      << int(CalleeKind) << Attr->getRange();
}

SourceRange Sema::getExprRange(Expr *E) const {
  return E ? E->getSourceRange() : SourceRange();
}

//===----------------------------------------------------------------------===//
//  Standard Promotions and Conversions
//===----------------------------------------------------------------------===//

/// DefaultFunctionArrayConversion (C99 6.3.2.1p3, C99 6.3.2.1p4).
ExprResult Sema::DefaultFunctionArrayConversion(Expr *E, bool Diagnose) {
  // Handle any placeholder expressions which made it here.
  if (E->hasPlaceholderType()) {
    ExprResult result = CheckPlaceholderExpr(E);
    if (result.isInvalid()) return ExprError();
    E = result.get();
  }

  QualType Ty = E->getType();
  assert(!Ty.isNull() && "DefaultFunctionArrayConversion - missing type");

  if (Ty->isFunctionType()) {
    if (auto *DRE = dyn_cast<DeclRefExpr>(E->IgnoreParenCasts()))
      if (auto *FD = dyn_cast<FunctionDecl>(DRE->getDecl()))
        if (!checkAddressOfFunctionIsAvailable(FD, Diagnose, E->getExprLoc()))
          return ExprError();

    E = ImpCastExprToType(E, Context.getPointerType(Ty),
                          CK_FunctionToPointerDecay).get();
  } else if (Ty->isArrayType()) {
    // In C90 mode, arrays only promote to pointers if the array expression is
    // an lvalue.  The relevant legalese is C90 6.2.2.1p3: "an lvalue that has
    // type 'array of type' is converted to an expression that has type 'pointer
    // to type'...".  In C99 this was changed to: C99 6.3.2.1p3: "an expression
    // that has type 'array of type' ...".  The relevant change is "an lvalue"
    // (C90) to "an expression" (C99).
    //
    // C++ 4.2p1:
    // An lvalue or rvalue of type "array of N T" or "array of unknown bound of
    // T" can be converted to an rvalue of type "pointer to T".
    //
    if (getLangOpts().C99 || getLangOpts().CPlusPlus || E->isLValue()) {
      ExprResult Res = ImpCastExprToType(E, Context.getArrayDecayedType(Ty),
                                         CK_ArrayToPointerDecay);
      if (Res.isInvalid())
        return ExprError();
      E = Res.get();
    }
  }
  return E;
}

static void CheckForNullPointerDereference(Sema &S, Expr *E) {
  // Check to see if we are dereferencing a null pointer.  If so,
  // and if not volatile-qualified, this is undefined behavior that the
  // optimizer will delete, so warn about it.  People sometimes try to use this
  // to get a deterministic trap and are surprised by clang's behavior.  This
  // only handles the pattern "*null", which is a very syntactic check.
  const auto *UO = dyn_cast<UnaryOperator>(E->IgnoreParenCasts());
  if (UO && UO->getOpcode() == UO_Deref &&
      UO->getSubExpr()->getType()->isPointerType()) {
    const LangAS AS =
        UO->getSubExpr()->getType()->getPointeeType().getAddressSpace();
    if ((!isTargetAddressSpace(AS) ||
         (isTargetAddressSpace(AS) && toTargetAddressSpace(AS) == 0)) &&
        UO->getSubExpr()->IgnoreParenCasts()->isNullPointerConstant(
            S.Context, Expr::NPC_ValueDependentIsNotNull) &&
        !UO->getType().isVolatileQualified()) {
      S.DiagRuntimeBehavior(UO->getOperatorLoc(), UO,
                            S.PDiag(diag::warn_indirection_through_null)
                                << UO->getSubExpr()->getSourceRange());
      S.DiagRuntimeBehavior(UO->getOperatorLoc(), UO,
                            S.PDiag(diag::note_indirection_through_null));
    }
  }
}

static void DiagnoseDirectIsaAccess(Sema &S, const ObjCIvarRefExpr *OIRE,
                                    SourceLocation AssignLoc,
                                    const Expr* RHS) {
  const ObjCIvarDecl *IV = OIRE->getDecl();
  if (!IV)
    return;

  DeclarationName MemberName = IV->getDeclName();
  IdentifierInfo *Member = MemberName.getAsIdentifierInfo();
  if (!Member || !Member->isStr("isa"))
    return;

  const Expr *Base = OIRE->getBase();
  QualType BaseType = Base->getType();
  if (OIRE->isArrow())
    BaseType = BaseType->getPointeeType();
  if (const ObjCObjectType *OTy = BaseType->getAs<ObjCObjectType>())
    if (ObjCInterfaceDecl *IDecl = OTy->getInterface()) {
      ObjCInterfaceDecl *ClassDeclared = nullptr;
      ObjCIvarDecl *IV = IDecl->lookupInstanceVariable(Member, ClassDeclared);
      if (!ClassDeclared->getSuperClass()
          && (*ClassDeclared->ivar_begin()) == IV) {
        if (RHS) {
          NamedDecl *ObjectSetClass =
            S.LookupSingleName(S.TUScope,
                               &S.Context.Idents.get("object_setClass"),
                               SourceLocation(), S.LookupOrdinaryName);
          if (ObjectSetClass) {
            SourceLocation RHSLocEnd = S.getLocForEndOfToken(RHS->getEndLoc());
            S.Diag(OIRE->getExprLoc(), diag::warn_objc_isa_assign)
                << FixItHint::CreateInsertion(OIRE->getBeginLoc(),
                                              "object_setClass(")
                << FixItHint::CreateReplacement(
                       SourceRange(OIRE->getOpLoc(), AssignLoc), ",")
                << FixItHint::CreateInsertion(RHSLocEnd, ")");
          }
          else
            S.Diag(OIRE->getLocation(), diag::warn_objc_isa_assign);
        } else {
          NamedDecl *ObjectGetClass =
            S.LookupSingleName(S.TUScope,
                               &S.Context.Idents.get("object_getClass"),
                               SourceLocation(), S.LookupOrdinaryName);
          if (ObjectGetClass)
            S.Diag(OIRE->getExprLoc(), diag::warn_objc_isa_use)
                << FixItHint::CreateInsertion(OIRE->getBeginLoc(),
                                              "object_getClass(")
                << FixItHint::CreateReplacement(
                       SourceRange(OIRE->getOpLoc(), OIRE->getEndLoc()), ")");
          else
            S.Diag(OIRE->getLocation(), diag::warn_objc_isa_use);
        }
        S.Diag(IV->getLocation(), diag::note_ivar_decl);
      }
    }
}

ExprResult Sema::DefaultLvalueConversion(Expr *E) {
  // Handle any placeholder expressions which made it here.
  if (E->hasPlaceholderType()) {
    ExprResult result = CheckPlaceholderExpr(E);
    if (result.isInvalid()) return ExprError();
    E = result.get();
  }

  // C++ [conv.lval]p1:
  //   A glvalue of a non-function, non-array type T can be
  //   converted to a prvalue.
  if (!E->isGLValue()) return E;

  QualType T = E->getType();
  assert(!T.isNull() && "r-value conversion on typeless expression?");

  // lvalue-to-rvalue conversion cannot be applied to types that decay to
  // pointers (i.e. function or array types).
  if (T->canDecayToPointerType())
    return E;

  // We don't want to throw lvalue-to-rvalue casts on top of
  // expressions of certain types in C++.
  if (getLangOpts().CPlusPlus) {
    if (T == Context.OverloadTy || T->isRecordType() ||
        (T->isDependentType() && !T->isAnyPointerType() &&
         !T->isMemberPointerType()))
      return E;
  }

  // The C standard is actually really unclear on this point, and
  // DR106 tells us what the result should be but not why.  It's
  // generally best to say that void types just doesn't undergo
  // lvalue-to-rvalue at all.  Note that expressions of unqualified
  // 'void' type are never l-values, but qualified void can be.
  if (T->isVoidType())
    return E;

  // OpenCL usually rejects direct accesses to values of 'half' type.
  if (getLangOpts().OpenCL &&
      !getOpenCLOptions().isAvailableOption("cl_khr_fp16", getLangOpts()) &&
      T->isHalfType()) {
    Diag(E->getExprLoc(), diag::err_opencl_half_load_store)
      << 0 << T;
    return ExprError();
  }

  CheckForNullPointerDereference(*this, E);
  if (const ObjCIsaExpr *OISA = dyn_cast<ObjCIsaExpr>(E->IgnoreParenCasts())) {
    NamedDecl *ObjectGetClass = LookupSingleName(TUScope,
                                     &Context.Idents.get("object_getClass"),
                                     SourceLocation(), LookupOrdinaryName);
    if (ObjectGetClass)
      Diag(E->getExprLoc(), diag::warn_objc_isa_use)
          << FixItHint::CreateInsertion(OISA->getBeginLoc(), "object_getClass(")
          << FixItHint::CreateReplacement(
                 SourceRange(OISA->getOpLoc(), OISA->getIsaMemberLoc()), ")");
    else
      Diag(E->getExprLoc(), diag::warn_objc_isa_use);
  }
  else if (const ObjCIvarRefExpr *OIRE =
            dyn_cast<ObjCIvarRefExpr>(E->IgnoreParenCasts()))
    DiagnoseDirectIsaAccess(*this, OIRE, SourceLocation(), /* Expr*/nullptr);

  // C++ [conv.lval]p1:
  //   [...] If T is a non-class type, the type of the prvalue is the
  //   cv-unqualified version of T. Otherwise, the type of the
  //   rvalue is T.
  //
  // C99 6.3.2.1p2:
  //   If the lvalue has qualified type, the value has the unqualified
  //   version of the type of the lvalue; otherwise, the value has the
  //   type of the lvalue.
  if (T.hasQualifiers())
    T = T.getUnqualifiedType();

  // Under the MS ABI, lock down the inheritance model now.
  if (T->isMemberPointerType() &&
      Context.getTargetInfo().getCXXABI().isMicrosoft())
    (void)isCompleteType(E->getExprLoc(), T);

  ExprResult Res = CheckLValueToRValueConversionOperand(E);
  if (Res.isInvalid())
    return Res;
  E = Res.get();

  // Loading a __weak object implicitly retains the value, so we need a cleanup to
  // balance that.
  if (E->getType().getObjCLifetime() == Qualifiers::OCL_Weak)
    Cleanup.setExprNeedsCleanups(true);

  if (E->getType().isDestructedType() == QualType::DK_nontrivial_c_struct)
    Cleanup.setExprNeedsCleanups(true);

  // C++ [conv.lval]p3:
  //   If T is cv std::nullptr_t, the result is a null pointer constant.
  CastKind CK = T->isNullPtrType() ? CK_NullToPointer : CK_LValueToRValue;
  Res = ImplicitCastExpr::Create(Context, T, CK, E, nullptr, VK_PRValue,
                                 CurFPFeatureOverrides());

  // C11 6.3.2.1p2:
  //   ... if the lvalue has atomic type, the value has the non-atomic version
  //   of the type of the lvalue ...
  if (const AtomicType *Atomic = T->getAs<AtomicType>()) {
    T = Atomic->getValueType().getUnqualifiedType();
    Res = ImplicitCastExpr::Create(Context, T, CK_AtomicToNonAtomic, Res.get(),
                                   nullptr, VK_PRValue, FPOptionsOverride());
  }

  return Res;
}

ExprResult Sema::DefaultFunctionArrayLvalueConversion(Expr *E, bool Diagnose) {
  ExprResult Res = DefaultFunctionArrayConversion(E, Diagnose);
  if (Res.isInvalid())
    return ExprError();
  Res = DefaultLvalueConversion(Res.get());
  if (Res.isInvalid())
    return ExprError();
  return Res;
}

ExprResult Sema::CallExprUnaryConversions(Expr *E) {
  QualType Ty = E->getType();
  ExprResult Res = E;
  // Only do implicit cast for a function type, but not for a pointer
  // to function type.
  if (Ty->isFunctionType()) {
    Res = ImpCastExprToType(E, Context.getPointerType(Ty),
                            CK_FunctionToPointerDecay);
    if (Res.isInvalid())
      return ExprError();
  }
  Res = DefaultLvalueConversion(Res.get());
  if (Res.isInvalid())
    return ExprError();
  return Res.get();
}

/// UsualUnaryConversions - Performs various conversions that are common to most
/// operators (C99 6.3). The conversions of array and function types are
/// sometimes suppressed. For example, the array->pointer conversion doesn't
/// apply if the array is an argument to the sizeof or address (&) operators.
/// In these instances, this routine should *not* be called.
ExprResult Sema::UsualUnaryConversions(Expr *E) {
  // First, convert to an r-value.
  ExprResult Res = DefaultFunctionArrayLvalueConversion(E);
  if (Res.isInvalid())
    return ExprError();
  E = Res.get();

  QualType Ty = E->getType();
  assert(!Ty.isNull() && "UsualUnaryConversions - missing type");

  LangOptions::FPEvalMethodKind EvalMethod = CurFPFeatures.getFPEvalMethod();
  if (EvalMethod != LangOptions::FEM_Source && Ty->isFloatingType() &&
      (getLangOpts().getFPEvalMethod() !=
           LangOptions::FPEvalMethodKind::FEM_UnsetOnCommandLine ||
       PP.getLastFPEvalPragmaLocation().isValid())) {
    switch (EvalMethod) {
    default:
      llvm_unreachable("Unrecognized float evaluation method");
      break;
    case LangOptions::FEM_UnsetOnCommandLine:
      llvm_unreachable("Float evaluation method should be set by now");
      break;
    case LangOptions::FEM_Double:
      if (Context.getFloatingTypeOrder(Context.DoubleTy, Ty) > 0)
        // Widen the expression to double.
        return Ty->isComplexType()
                   ? ImpCastExprToType(E,
                                       Context.getComplexType(Context.DoubleTy),
                                       CK_FloatingComplexCast)
                   : ImpCastExprToType(E, Context.DoubleTy, CK_FloatingCast);
      break;
    case LangOptions::FEM_Extended:
      if (Context.getFloatingTypeOrder(Context.LongDoubleTy, Ty) > 0)
        // Widen the expression to long double.
        return Ty->isComplexType()
                   ? ImpCastExprToType(
                         E, Context.getComplexType(Context.LongDoubleTy),
                         CK_FloatingComplexCast)
                   : ImpCastExprToType(E, Context.LongDoubleTy,
                                       CK_FloatingCast);
      break;
    }
  }

  // Half FP have to be promoted to float unless it is natively supported
  if (Ty->isHalfType() && !getLangOpts().NativeHalfType)
    return ImpCastExprToType(Res.get(), Context.FloatTy, CK_FloatingCast);

  // Try to perform integral promotions if the object has a theoretically
  // promotable type.
  if (Ty->isIntegralOrUnscopedEnumerationType()) {
    // C99 6.3.1.1p2:
    //
    //   The following may be used in an expression wherever an int or
    //   unsigned int may be used:
    //     - an object or expression with an integer type whose integer
    //       conversion rank is less than or equal to the rank of int
    //       and unsigned int.
    //     - A bit-field of type _Bool, int, signed int, or unsigned int.
    //
    //   If an int can represent all values of the original type, the
    //   value is converted to an int; otherwise, it is converted to an
    //   unsigned int. These are called the integer promotions. All
    //   other types are unchanged by the integer promotions.

    QualType PTy = Context.isPromotableBitField(E);
    if (!PTy.isNull()) {
      E = ImpCastExprToType(E, PTy, CK_IntegralCast).get();
      return E;
    }
    if (Context.isPromotableIntegerType(Ty)) {
      QualType PT = Context.getPromotedIntegerType(Ty);
      E = ImpCastExprToType(E, PT, CK_IntegralCast).get();
      return E;
    }
  }
  return E;
}

/// DefaultArgumentPromotion (C99 6.5.2.2p6). Used for function calls that
/// do not have a prototype. Arguments that have type float or __fp16
/// are promoted to double. All other argument types are converted by
/// UsualUnaryConversions().
ExprResult Sema::DefaultArgumentPromotion(Expr *E) {
  QualType Ty = E->getType();
  assert(!Ty.isNull() && "DefaultArgumentPromotion - missing type");

  ExprResult Res = UsualUnaryConversions(E);
  if (Res.isInvalid())
    return ExprError();
  E = Res.get();

  // If this is a 'float'  or '__fp16' (CVR qualified or typedef)
  // promote to double.
  // Note that default argument promotion applies only to float (and
  // half/fp16); it does not apply to _Float16.
  const BuiltinType *BTy = Ty->getAs<BuiltinType>();
  if (BTy && (BTy->getKind() == BuiltinType::Half ||
              BTy->getKind() == BuiltinType::Float)) {
    if (getLangOpts().OpenCL &&
        !getOpenCLOptions().isAvailableOption("cl_khr_fp64", getLangOpts())) {
      if (BTy->getKind() == BuiltinType::Half) {
        E = ImpCastExprToType(E, Context.FloatTy, CK_FloatingCast).get();
      }
    } else {
      E = ImpCastExprToType(E, Context.DoubleTy, CK_FloatingCast).get();
    }
  }
  if (BTy &&
      getLangOpts().getExtendIntArgs() ==
          LangOptions::ExtendArgsKind::ExtendTo64 &&
      Context.getTargetInfo().supportsExtendIntArgs() && Ty->isIntegerType() &&
      Context.getTypeSizeInChars(BTy) <
          Context.getTypeSizeInChars(Context.LongLongTy)) {
    E = (Ty->isUnsignedIntegerType())
            ? ImpCastExprToType(E, Context.UnsignedLongLongTy, CK_IntegralCast)
                  .get()
            : ImpCastExprToType(E, Context.LongLongTy, CK_IntegralCast).get();
    assert(8 == Context.getTypeSizeInChars(Context.LongLongTy).getQuantity() &&
           "Unexpected typesize for LongLongTy");
  }

  // C++ performs lvalue-to-rvalue conversion as a default argument
  // promotion, even on class types, but note:
  //   C++11 [conv.lval]p2:
  //     When an lvalue-to-rvalue conversion occurs in an unevaluated
  //     operand or a subexpression thereof the value contained in the
  //     referenced object is not accessed. Otherwise, if the glvalue
  //     has a class type, the conversion copy-initializes a temporary
  //     of type T from the glvalue and the result of the conversion
  //     is a prvalue for the temporary.
  // FIXME: add some way to gate this entire thing for correctness in
  // potentially potentially evaluated contexts.
  if (getLangOpts().CPlusPlus && E->isGLValue() && !isUnevaluatedContext()) {
    ExprResult Temp = PerformCopyInitialization(
                       InitializedEntity::InitializeTemporary(E->getType()),
                                                E->getExprLoc(), E);
    if (Temp.isInvalid())
      return ExprError();
    E = Temp.get();
  }

  return E;
}

Sema::VarArgKind Sema::isValidVarArgType(const QualType &Ty) {
  if (Ty->isIncompleteType()) {
    // C++11 [expr.call]p7:
    //   After these conversions, if the argument does not have arithmetic,
    //   enumeration, pointer, pointer to member, or class type, the program
    //   is ill-formed.
    //
    // Since we've already performed array-to-pointer and function-to-pointer
    // decay, the only such type in C++ is cv void. This also handles
    // initializer lists as variadic arguments.
    if (Ty->isVoidType())
      return VAK_Invalid;

    if (Ty->isObjCObjectType())
      return VAK_Invalid;
    return VAK_Valid;
  }

  if (Ty.isDestructedType() == QualType::DK_nontrivial_c_struct)
    return VAK_Invalid;

  if (Context.getTargetInfo().getTriple().isWasm() &&
      Ty.isWebAssemblyReferenceType()) {
    return VAK_Invalid;
  }

  if (Ty.isCXX98PODType(Context))
    return VAK_Valid;

  // C++11 [expr.call]p7:
  //   Passing a potentially-evaluated argument of class type (Clause 9)
  //   having a non-trivial copy constructor, a non-trivial move constructor,
  //   or a non-trivial destructor, with no corresponding parameter,
  //   is conditionally-supported with implementation-defined semantics.
  if (getLangOpts().CPlusPlus11 && !Ty->isDependentType())
    if (CXXRecordDecl *Record = Ty->getAsCXXRecordDecl())
      if (!Record->hasNonTrivialCopyConstructor() &&
          !Record->hasNonTrivialMoveConstructor() &&
          !Record->hasNonTrivialDestructor())
        return VAK_ValidInCXX11;

  if (getLangOpts().ObjCAutoRefCount && Ty->isObjCLifetimeType())
    return VAK_Valid;

  if (Ty->isObjCObjectType())
    return VAK_Invalid;

  if (getLangOpts().MSVCCompat)
    return VAK_MSVCUndefined;

  // FIXME: In C++11, these cases are conditionally-supported, meaning we're
  // permitted to reject them. We should consider doing so.
  return VAK_Undefined;
}

void Sema::checkVariadicArgument(const Expr *E, VariadicCallType CT) {
  // Don't allow one to pass an Objective-C interface to a vararg.
  const QualType &Ty = E->getType();
  VarArgKind VAK = isValidVarArgType(Ty);

  // Complain about passing non-POD types through varargs.
  switch (VAK) {
  case VAK_ValidInCXX11:
    DiagRuntimeBehavior(
        E->getBeginLoc(), nullptr,
        PDiag(diag::warn_cxx98_compat_pass_non_pod_arg_to_vararg) << Ty << CT);
    [[fallthrough]];
  case VAK_Valid:
    if (Ty->isRecordType()) {
      // This is unlikely to be what the user intended. If the class has a
      // 'c_str' member function, the user probably meant to call that.
      DiagRuntimeBehavior(E->getBeginLoc(), nullptr,
                          PDiag(diag::warn_pass_class_arg_to_vararg)
                              << Ty << CT << hasCStrMethod(E) << ".c_str()");
    }
    break;

  case VAK_Undefined:
  case VAK_MSVCUndefined:
    DiagRuntimeBehavior(E->getBeginLoc(), nullptr,
                        PDiag(diag::warn_cannot_pass_non_pod_arg_to_vararg)
                            << getLangOpts().CPlusPlus11 << Ty << CT);
    break;

  case VAK_Invalid:
    if (Ty.isDestructedType() == QualType::DK_nontrivial_c_struct)
      Diag(E->getBeginLoc(),
           diag::err_cannot_pass_non_trivial_c_struct_to_vararg)
          << Ty << CT;
    else if (Ty->isObjCObjectType())
      DiagRuntimeBehavior(E->getBeginLoc(), nullptr,
                          PDiag(diag::err_cannot_pass_objc_interface_to_vararg)
                              << Ty << CT);
    else
      Diag(E->getBeginLoc(), diag::err_cannot_pass_to_vararg)
          << isa<InitListExpr>(E) << Ty << CT;
    break;
  }
}

ExprResult Sema::DefaultVariadicArgumentPromotion(Expr *E, VariadicCallType CT,
                                                  FunctionDecl *FDecl) {
  if (const BuiltinType *PlaceholderTy = E->getType()->getAsPlaceholderType()) {
    // Strip the unbridged-cast placeholder expression off, if applicable.
    if (PlaceholderTy->getKind() == BuiltinType::ARCUnbridgedCast &&
        (CT == VariadicMethod ||
         (FDecl && FDecl->hasAttr<CFAuditedTransferAttr>()))) {
      E = ObjC().stripARCUnbridgedCast(E);

      // Otherwise, do normal placeholder checking.
    } else {
      ExprResult ExprRes = CheckPlaceholderExpr(E);
      if (ExprRes.isInvalid())
        return ExprError();
      E = ExprRes.get();
    }
  }

  ExprResult ExprRes = DefaultArgumentPromotion(E);
  if (ExprRes.isInvalid())
    return ExprError();

  // Copy blocks to the heap.
  if (ExprRes.get()->getType()->isBlockPointerType())
    maybeExtendBlockObject(ExprRes);

  E = ExprRes.get();

  // Diagnostics regarding non-POD argument types are
  // emitted along with format string checking in Sema::CheckFunctionCall().
  if (isValidVarArgType(E->getType()) == VAK_Undefined) {
    // Turn this into a trap.
    CXXScopeSpec SS;
    SourceLocation TemplateKWLoc;
    UnqualifiedId Name;
    Name.setIdentifier(PP.getIdentifierInfo("__builtin_trap"),
                       E->getBeginLoc());
    ExprResult TrapFn = ActOnIdExpression(TUScope, SS, TemplateKWLoc, Name,
                                          /*HasTrailingLParen=*/true,
                                          /*IsAddressOfOperand=*/false);
    if (TrapFn.isInvalid())
      return ExprError();

    ExprResult Call = BuildCallExpr(TUScope, TrapFn.get(), E->getBeginLoc(),
                                    std::nullopt, E->getEndLoc());
    if (Call.isInvalid())
      return ExprError();

    ExprResult Comma =
        ActOnBinOp(TUScope, E->getBeginLoc(), tok::comma, Call.get(), E);
    if (Comma.isInvalid())
      return ExprError();
    return Comma.get();
  }

  if (!getLangOpts().CPlusPlus &&
      RequireCompleteType(E->getExprLoc(), E->getType(),
                          diag::err_call_incomplete_argument))
    return ExprError();

  return E;
}

/// Convert complex integers to complex floats and real integers to
/// real floats as required for complex arithmetic. Helper function of
/// UsualArithmeticConversions()
///
/// \return false if the integer expression is an integer type and is
/// successfully converted to the (complex) float type.
static bool handleComplexIntegerToFloatConversion(Sema &S, ExprResult &IntExpr,
                                                  ExprResult &ComplexExpr,
                                                  QualType IntTy,
                                                  QualType ComplexTy,
                                                  bool SkipCast) {
  if (IntTy->isComplexType() || IntTy->isRealFloatingType()) return true;
  if (SkipCast) return false;
  if (IntTy->isIntegerType()) {
    QualType fpTy = ComplexTy->castAs<ComplexType>()->getElementType();
    IntExpr = S.ImpCastExprToType(IntExpr.get(), fpTy, CK_IntegralToFloating);
  } else {
    assert(IntTy->isComplexIntegerType());
    IntExpr = S.ImpCastExprToType(IntExpr.get(), ComplexTy,
                                  CK_IntegralComplexToFloatingComplex);
  }
  return false;
}

// This handles complex/complex, complex/float, or float/complex.
// When both operands are complex, the shorter operand is converted to the
// type of the longer, and that is the type of the result. This corresponds
// to what is done when combining two real floating-point operands.
// The fun begins when size promotion occur across type domains.
// From H&S 6.3.4: When one operand is complex and the other is a real
// floating-point type, the less precise type is converted, within it's
// real or complex domain, to the precision of the other type. For example,
// when combining a "long double" with a "double _Complex", the
// "double _Complex" is promoted to "long double _Complex".
static QualType handleComplexFloatConversion(Sema &S, ExprResult &Shorter,
                                             QualType ShorterType,
                                             QualType LongerType,
                                             bool PromotePrecision) {
  bool LongerIsComplex = isa<ComplexType>(LongerType.getCanonicalType());
  QualType Result =
      LongerIsComplex ? LongerType : S.Context.getComplexType(LongerType);

  if (PromotePrecision) {
    if (isa<ComplexType>(ShorterType.getCanonicalType())) {
      Shorter =
          S.ImpCastExprToType(Shorter.get(), Result, CK_FloatingComplexCast);
    } else {
      if (LongerIsComplex)
        LongerType = LongerType->castAs<ComplexType>()->getElementType();
      Shorter = S.ImpCastExprToType(Shorter.get(), LongerType, CK_FloatingCast);
    }
  }
  return Result;
}

/// Handle arithmetic conversion with complex types.  Helper function of
/// UsualArithmeticConversions()
static QualType handleComplexConversion(Sema &S, ExprResult &LHS,
                                        ExprResult &RHS, QualType LHSType,
                                        QualType RHSType, bool IsCompAssign) {
  // Handle (complex) integer types.
  if (!handleComplexIntegerToFloatConversion(S, RHS, LHS, RHSType, LHSType,
                                             /*SkipCast=*/false))
    return LHSType;
  if (!handleComplexIntegerToFloatConversion(S, LHS, RHS, LHSType, RHSType,
                                             /*SkipCast=*/IsCompAssign))
    return RHSType;

  // Compute the rank of the two types, regardless of whether they are complex.
  int Order = S.Context.getFloatingTypeOrder(LHSType, RHSType);
  if (Order < 0)
    // Promote the precision of the LHS if not an assignment.
    return handleComplexFloatConversion(S, LHS, LHSType, RHSType,
                                        /*PromotePrecision=*/!IsCompAssign);
  // Promote the precision of the RHS unless it is already the same as the LHS.
  return handleComplexFloatConversion(S, RHS, RHSType, LHSType,
                                      /*PromotePrecision=*/Order > 0);
}

/// Handle arithmetic conversion from integer to float.  Helper function
/// of UsualArithmeticConversions()
static QualType handleIntToFloatConversion(Sema &S, ExprResult &FloatExpr,
                                           ExprResult &IntExpr,
                                           QualType FloatTy, QualType IntTy,
                                           bool ConvertFloat, bool ConvertInt) {
  if (IntTy->isIntegerType()) {
    if (ConvertInt)
      // Convert intExpr to the lhs floating point type.
      IntExpr = S.ImpCastExprToType(IntExpr.get(), FloatTy,
                                    CK_IntegralToFloating);
    return FloatTy;
  }

  // Convert both sides to the appropriate complex float.
  assert(IntTy->isComplexIntegerType());
  QualType result = S.Context.getComplexType(FloatTy);

  // _Complex int -> _Complex float
  if (ConvertInt)
    IntExpr = S.ImpCastExprToType(IntExpr.get(), result,
                                  CK_IntegralComplexToFloatingComplex);

  // float -> _Complex float
  if (ConvertFloat)
    FloatExpr = S.ImpCastExprToType(FloatExpr.get(), result,
                                    CK_FloatingRealToComplex);

  return result;
}

/// Handle arithmethic conversion with floating point types.  Helper
/// function of UsualArithmeticConversions()
static QualType handleFloatConversion(Sema &S, ExprResult &LHS,
                                      ExprResult &RHS, QualType LHSType,
                                      QualType RHSType, bool IsCompAssign) {
  bool LHSFloat = LHSType->isRealFloatingType();
  bool RHSFloat = RHSType->isRealFloatingType();

  // N1169 4.1.4: If one of the operands has a floating type and the other
  //              operand has a fixed-point type, the fixed-point operand
  //              is converted to the floating type [...]
  if (LHSType->isFixedPointType() || RHSType->isFixedPointType()) {
    if (LHSFloat)
      RHS = S.ImpCastExprToType(RHS.get(), LHSType, CK_FixedPointToFloating);
    else if (!IsCompAssign)
      LHS = S.ImpCastExprToType(LHS.get(), RHSType, CK_FixedPointToFloating);
    return LHSFloat ? LHSType : RHSType;
  }

  // If we have two real floating types, convert the smaller operand
  // to the bigger result.
  if (LHSFloat && RHSFloat) {
    int order = S.Context.getFloatingTypeOrder(LHSType, RHSType);
    if (order > 0) {
      RHS = S.ImpCastExprToType(RHS.get(), LHSType, CK_FloatingCast);
      return LHSType;
    }

    assert(order < 0 && "illegal float comparison");
    if (!IsCompAssign)
      LHS = S.ImpCastExprToType(LHS.get(), RHSType, CK_FloatingCast);
    return RHSType;
  }

  if (LHSFloat) {
    // Half FP has to be promoted to float unless it is natively supported
    if (LHSType->isHalfType() && !S.getLangOpts().NativeHalfType)
      LHSType = S.Context.FloatTy;

    return handleIntToFloatConversion(S, LHS, RHS, LHSType, RHSType,
                                      /*ConvertFloat=*/!IsCompAssign,
                                      /*ConvertInt=*/ true);
  }
  assert(RHSFloat);
  return handleIntToFloatConversion(S, RHS, LHS, RHSType, LHSType,
                                    /*ConvertFloat=*/ true,
                                    /*ConvertInt=*/!IsCompAssign);
}

/// Diagnose attempts to convert between __float128, __ibm128 and
/// long double if there is no support for such conversion.
/// Helper function of UsualArithmeticConversions().
static bool unsupportedTypeConversion(const Sema &S, QualType LHSType,
                                      QualType RHSType) {
  // No issue if either is not a floating point type.
  if (!LHSType->isFloatingType() || !RHSType->isFloatingType())
    return false;

  // No issue if both have the same 128-bit float semantics.
  auto *LHSComplex = LHSType->getAs<ComplexType>();
  auto *RHSComplex = RHSType->getAs<ComplexType>();

  QualType LHSElem = LHSComplex ? LHSComplex->getElementType() : LHSType;
  QualType RHSElem = RHSComplex ? RHSComplex->getElementType() : RHSType;

  const llvm::fltSemantics &LHSSem = S.Context.getFloatTypeSemantics(LHSElem);
  const llvm::fltSemantics &RHSSem = S.Context.getFloatTypeSemantics(RHSElem);

  if ((&LHSSem != &llvm::APFloat::PPCDoubleDouble() ||
       &RHSSem != &llvm::APFloat::IEEEquad()) &&
      (&LHSSem != &llvm::APFloat::IEEEquad() ||
       &RHSSem != &llvm::APFloat::PPCDoubleDouble()))
    return false;

  return true;
}

typedef ExprResult PerformCastFn(Sema &S, Expr *operand, QualType toType);

namespace {
/// These helper callbacks are placed in an anonymous namespace to
/// permit their use as function template parameters.
ExprResult doIntegralCast(Sema &S, Expr *op, QualType toType) {
  return S.ImpCastExprToType(op, toType, CK_IntegralCast);
}

ExprResult doComplexIntegralCast(Sema &S, Expr *op, QualType toType) {
  return S.ImpCastExprToType(op, S.Context.getComplexType(toType),
                             CK_IntegralComplexCast);
}
}

/// Handle integer arithmetic conversions.  Helper function of
/// UsualArithmeticConversions()
template <PerformCastFn doLHSCast, PerformCastFn doRHSCast>
static QualType handleIntegerConversion(Sema &S, ExprResult &LHS,
                                        ExprResult &RHS, QualType LHSType,
                                        QualType RHSType, bool IsCompAssign) {
  // The rules for this case are in C99 6.3.1.8
  int order = S.Context.getIntegerTypeOrder(LHSType, RHSType);
  bool LHSSigned = LHSType->hasSignedIntegerRepresentation();
  bool RHSSigned = RHSType->hasSignedIntegerRepresentation();
  if (LHSSigned == RHSSigned) {
    // Same signedness; use the higher-ranked type
    if (order >= 0) {
      RHS = (*doRHSCast)(S, RHS.get(), LHSType);
      return LHSType;
    } else if (!IsCompAssign)
      LHS = (*doLHSCast)(S, LHS.get(), RHSType);
    return RHSType;
  } else if (order != (LHSSigned ? 1 : -1)) {
    // The unsigned type has greater than or equal rank to the
    // signed type, so use the unsigned type
    if (RHSSigned) {
      RHS = (*doRHSCast)(S, RHS.get(), LHSType);
      return LHSType;
    } else if (!IsCompAssign)
      LHS = (*doLHSCast)(S, LHS.get(), RHSType);
    return RHSType;
  } else if (S.Context.getIntWidth(LHSType) != S.Context.getIntWidth(RHSType)) {
    // The two types are different widths; if we are here, that
    // means the signed type is larger than the unsigned type, so
    // use the signed type.
    if (LHSSigned) {
      RHS = (*doRHSCast)(S, RHS.get(), LHSType);
      return LHSType;
    } else if (!IsCompAssign)
      LHS = (*doLHSCast)(S, LHS.get(), RHSType);
    return RHSType;
  } else {
    // The signed type is higher-ranked than the unsigned type,
    // but isn't actually any bigger (like unsigned int and long
    // on most 32-bit systems).  Use the unsigned type corresponding
    // to the signed type.
    QualType result =
      S.Context.getCorrespondingUnsignedType(LHSSigned ? LHSType : RHSType);
    RHS = (*doRHSCast)(S, RHS.get(), result);
    if (!IsCompAssign)
      LHS = (*doLHSCast)(S, LHS.get(), result);
    return result;
  }
}

/// Handle conversions with GCC complex int extension.  Helper function
/// of UsualArithmeticConversions()
static QualType handleComplexIntConversion(Sema &S, ExprResult &LHS,
                                           ExprResult &RHS, QualType LHSType,
                                           QualType RHSType,
                                           bool IsCompAssign) {
  const ComplexType *LHSComplexInt = LHSType->getAsComplexIntegerType();
  const ComplexType *RHSComplexInt = RHSType->getAsComplexIntegerType();

  if (LHSComplexInt && RHSComplexInt) {
    QualType LHSEltType = LHSComplexInt->getElementType();
    QualType RHSEltType = RHSComplexInt->getElementType();
    QualType ScalarType =
      handleIntegerConversion<doComplexIntegralCast, doComplexIntegralCast>
        (S, LHS, RHS, LHSEltType, RHSEltType, IsCompAssign);

    return S.Context.getComplexType(ScalarType);
  }

  if (LHSComplexInt) {
    QualType LHSEltType = LHSComplexInt->getElementType();
    QualType ScalarType =
      handleIntegerConversion<doComplexIntegralCast, doIntegralCast>
        (S, LHS, RHS, LHSEltType, RHSType, IsCompAssign);
    QualType ComplexType = S.Context.getComplexType(ScalarType);
    RHS = S.ImpCastExprToType(RHS.get(), ComplexType,
                              CK_IntegralRealToComplex);

    return ComplexType;
  }

  assert(RHSComplexInt);

  QualType RHSEltType = RHSComplexInt->getElementType();
  QualType ScalarType =
    handleIntegerConversion<doIntegralCast, doComplexIntegralCast>
      (S, LHS, RHS, LHSType, RHSEltType, IsCompAssign);
  QualType ComplexType = S.Context.getComplexType(ScalarType);

  if (!IsCompAssign)
    LHS = S.ImpCastExprToType(LHS.get(), ComplexType,
                              CK_IntegralRealToComplex);
  return ComplexType;
}

/// Return the rank of a given fixed point or integer type. The value itself
/// doesn't matter, but the values must be increasing with proper increasing
/// rank as described in N1169 4.1.1.
static unsigned GetFixedPointRank(QualType Ty) {
  const auto *BTy = Ty->getAs<BuiltinType>();
  assert(BTy && "Expected a builtin type.");

  switch (BTy->getKind()) {
  case BuiltinType::ShortFract:
  case BuiltinType::UShortFract:
  case BuiltinType::SatShortFract:
  case BuiltinType::SatUShortFract:
    return 1;
  case BuiltinType::Fract:
  case BuiltinType::UFract:
  case BuiltinType::SatFract:
  case BuiltinType::SatUFract:
    return 2;
  case BuiltinType::LongFract:
  case BuiltinType::ULongFract:
  case BuiltinType::SatLongFract:
  case BuiltinType::SatULongFract:
    return 3;
  case BuiltinType::ShortAccum:
  case BuiltinType::UShortAccum:
  case BuiltinType::SatShortAccum:
  case BuiltinType::SatUShortAccum:
    return 4;
  case BuiltinType::Accum:
  case BuiltinType::UAccum:
  case BuiltinType::SatAccum:
  case BuiltinType::SatUAccum:
    return 5;
  case BuiltinType::LongAccum:
  case BuiltinType::ULongAccum:
  case BuiltinType::SatLongAccum:
  case BuiltinType::SatULongAccum:
    return 6;
  default:
    if (BTy->isInteger())
      return 0;
    llvm_unreachable("Unexpected fixed point or integer type");
  }
}

/// handleFixedPointConversion - Fixed point operations between fixed
/// point types and integers or other fixed point types do not fall under
/// usual arithmetic conversion since these conversions could result in loss
/// of precsision (N1169 4.1.4). These operations should be calculated with
/// the full precision of their result type (N1169 4.1.6.2.1).
static QualType handleFixedPointConversion(Sema &S, QualType LHSTy,
                                           QualType RHSTy) {
  assert((LHSTy->isFixedPointType() || RHSTy->isFixedPointType()) &&
         "Expected at least one of the operands to be a fixed point type");
  assert((LHSTy->isFixedPointOrIntegerType() ||
          RHSTy->isFixedPointOrIntegerType()) &&
         "Special fixed point arithmetic operation conversions are only "
         "applied to ints or other fixed point types");

  // If one operand has signed fixed-point type and the other operand has
  // unsigned fixed-point type, then the unsigned fixed-point operand is
  // converted to its corresponding signed fixed-point type and the resulting
  // type is the type of the converted operand.
  if (RHSTy->isSignedFixedPointType() && LHSTy->isUnsignedFixedPointType())
    LHSTy = S.Context.getCorrespondingSignedFixedPointType(LHSTy);
  else if (RHSTy->isUnsignedFixedPointType() && LHSTy->isSignedFixedPointType())
    RHSTy = S.Context.getCorrespondingSignedFixedPointType(RHSTy);

  // The result type is the type with the highest rank, whereby a fixed-point
  // conversion rank is always greater than an integer conversion rank; if the
  // type of either of the operands is a saturating fixedpoint type, the result
  // type shall be the saturating fixed-point type corresponding to the type
  // with the highest rank; the resulting value is converted (taking into
  // account rounding and overflow) to the precision of the resulting type.
  // Same ranks between signed and unsigned types are resolved earlier, so both
  // types are either signed or both unsigned at this point.
  unsigned LHSTyRank = GetFixedPointRank(LHSTy);
  unsigned RHSTyRank = GetFixedPointRank(RHSTy);

  QualType ResultTy = LHSTyRank > RHSTyRank ? LHSTy : RHSTy;

  if (LHSTy->isSaturatedFixedPointType() || RHSTy->isSaturatedFixedPointType())
    ResultTy = S.Context.getCorrespondingSaturatedType(ResultTy);

  return ResultTy;
}

/// Check that the usual arithmetic conversions can be performed on this pair of
/// expressions that might be of enumeration type.
static void checkEnumArithmeticConversions(Sema &S, Expr *LHS, Expr *RHS,
                                           SourceLocation Loc,
                                           Sema::ArithConvKind ACK) {
  // C++2a [expr.arith.conv]p1:
  //   If one operand is of enumeration type and the other operand is of a
  //   different enumeration type or a floating-point type, this behavior is
  //   deprecated ([depr.arith.conv.enum]).
  //
  // Warn on this in all language modes. Produce a deprecation warning in C++20.
  // Eventually we will presumably reject these cases (in C++23 onwards?).
  QualType L = LHS->getEnumCoercedType(S.Context),
           R = RHS->getEnumCoercedType(S.Context);
  bool LEnum = L->isUnscopedEnumerationType(),
       REnum = R->isUnscopedEnumerationType();
  bool IsCompAssign = ACK == Sema::ACK_CompAssign;
  if ((!IsCompAssign && LEnum && R->isFloatingType()) ||
      (REnum && L->isFloatingType())) {
    S.Diag(Loc, S.getLangOpts().CPlusPlus26
                    ? diag::err_arith_conv_enum_float_cxx26
                : S.getLangOpts().CPlusPlus20
                    ? diag::warn_arith_conv_enum_float_cxx20
                    : diag::warn_arith_conv_enum_float)
        << LHS->getSourceRange() << RHS->getSourceRange() << (int)ACK << LEnum
        << L << R;
  } else if (!IsCompAssign && LEnum && REnum &&
             !S.Context.hasSameUnqualifiedType(L, R)) {
    unsigned DiagID;
    // In C++ 26, usual arithmetic conversions between 2 different enum types
    // are ill-formed.
    if (S.getLangOpts().CPlusPlus26)
      DiagID = diag::err_conv_mixed_enum_types_cxx26;
    else if (!L->castAs<EnumType>()->getDecl()->hasNameForLinkage() ||
             !R->castAs<EnumType>()->getDecl()->hasNameForLinkage()) {
      // If either enumeration type is unnamed, it's less likely that the
      // user cares about this, but this situation is still deprecated in
      // C++2a. Use a different warning group.
      DiagID = S.getLangOpts().CPlusPlus20
                    ? diag::warn_arith_conv_mixed_anon_enum_types_cxx20
                    : diag::warn_arith_conv_mixed_anon_enum_types;
    } else if (ACK == Sema::ACK_Conditional) {
      // Conditional expressions are separated out because they have
      // historically had a different warning flag.
      DiagID = S.getLangOpts().CPlusPlus20
                   ? diag::warn_conditional_mixed_enum_types_cxx20
                   : diag::warn_conditional_mixed_enum_types;
    } else if (ACK == Sema::ACK_Comparison) {
      // Comparison expressions are separated out because they have
      // historically had a different warning flag.
      DiagID = S.getLangOpts().CPlusPlus20
                   ? diag::warn_comparison_mixed_enum_types_cxx20
                   : diag::warn_comparison_mixed_enum_types;
    } else {
      DiagID = S.getLangOpts().CPlusPlus20
                   ? diag::warn_arith_conv_mixed_enum_types_cxx20
                   : diag::warn_arith_conv_mixed_enum_types;
    }
    S.Diag(Loc, DiagID) << LHS->getSourceRange() << RHS->getSourceRange()
                        << (int)ACK << L << R;
  }
}

/// UsualArithmeticConversions - Performs various conversions that are common to
/// binary operators (C99 6.3.1.8). If both operands aren't arithmetic, this
/// routine returns the first non-arithmetic type found. The client is
/// responsible for emitting appropriate error diagnostics.
QualType Sema::UsualArithmeticConversions(ExprResult &LHS, ExprResult &RHS,
                                          SourceLocation Loc,
                                          ArithConvKind ACK) {
  checkEnumArithmeticConversions(*this, LHS.get(), RHS.get(), Loc, ACK);

  if (ACK != ACK_CompAssign) {
    LHS = UsualUnaryConversions(LHS.get());
    if (LHS.isInvalid())
      return QualType();
  }

  RHS = UsualUnaryConversions(RHS.get());
  if (RHS.isInvalid())
    return QualType();

  // For conversion purposes, we ignore any qualifiers.
  // For example, "const float" and "float" are equivalent.
  QualType LHSType = LHS.get()->getType().getUnqualifiedType();
  QualType RHSType = RHS.get()->getType().getUnqualifiedType();

  // For conversion purposes, we ignore any atomic qualifier on the LHS.
  if (const AtomicType *AtomicLHS = LHSType->getAs<AtomicType>())
    LHSType = AtomicLHS->getValueType();

  // If both types are identical, no conversion is needed.
  if (Context.hasSameType(LHSType, RHSType))
    return Context.getCommonSugaredType(LHSType, RHSType);

  // If either side is a non-arithmetic type (e.g. a pointer), we are done.
  // The caller can deal with this (e.g. pointer + int).
  if (!LHSType->isArithmeticType() || !RHSType->isArithmeticType())
    return QualType();

  // Apply unary and bitfield promotions to the LHS's type.
  QualType LHSUnpromotedType = LHSType;
  if (Context.isPromotableIntegerType(LHSType))
    LHSType = Context.getPromotedIntegerType(LHSType);
  QualType LHSBitfieldPromoteTy = Context.isPromotableBitField(LHS.get());
  if (!LHSBitfieldPromoteTy.isNull())
    LHSType = LHSBitfieldPromoteTy;
  if (LHSType != LHSUnpromotedType && ACK != ACK_CompAssign)
    LHS = ImpCastExprToType(LHS.get(), LHSType, CK_IntegralCast);

  // If both types are identical, no conversion is needed.
  if (Context.hasSameType(LHSType, RHSType))
    return Context.getCommonSugaredType(LHSType, RHSType);

  // At this point, we have two different arithmetic types.

  // Diagnose attempts to convert between __ibm128, __float128 and long double
  // where such conversions currently can't be handled.
  if (unsupportedTypeConversion(*this, LHSType, RHSType))
    return QualType();

  // Handle complex types first (C99 6.3.1.8p1).
  if (LHSType->isComplexType() || RHSType->isComplexType())
    return handleComplexConversion(*this, LHS, RHS, LHSType, RHSType,
                                   ACK == ACK_CompAssign);

  // Now handle "real" floating types (i.e. float, double, long double).
  if (LHSType->isRealFloatingType() || RHSType->isRealFloatingType())
    return handleFloatConversion(*this, LHS, RHS, LHSType, RHSType,
                                 ACK == ACK_CompAssign);

  // Handle GCC complex int extension.
  if (LHSType->isComplexIntegerType() || RHSType->isComplexIntegerType())
    return handleComplexIntConversion(*this, LHS, RHS, LHSType, RHSType,
                                      ACK == ACK_CompAssign);

  if (LHSType->isFixedPointType() || RHSType->isFixedPointType())
    return handleFixedPointConversion(*this, LHSType, RHSType);

  // Finally, we have two differing integer types.
  return handleIntegerConversion<doIntegralCast, doIntegralCast>
           (*this, LHS, RHS, LHSType, RHSType, ACK == ACK_CompAssign);
}

//===----------------------------------------------------------------------===//
//  Semantic Analysis for various Expression Types
//===----------------------------------------------------------------------===//


ExprResult Sema::ActOnGenericSelectionExpr(
    SourceLocation KeyLoc, SourceLocation DefaultLoc, SourceLocation RParenLoc,
    bool PredicateIsExpr, void *ControllingExprOrType,
    ArrayRef<ParsedType> ArgTypes, ArrayRef<Expr *> ArgExprs) {
  unsigned NumAssocs = ArgTypes.size();
  assert(NumAssocs == ArgExprs.size());

  TypeSourceInfo **Types = new TypeSourceInfo*[NumAssocs];
  for (unsigned i = 0; i < NumAssocs; ++i) {
    if (ArgTypes[i])
      (void) GetTypeFromParser(ArgTypes[i], &Types[i]);
    else
      Types[i] = nullptr;
  }

  // If we have a controlling type, we need to convert it from a parsed type
  // into a semantic type and then pass that along.
  if (!PredicateIsExpr) {
    TypeSourceInfo *ControllingType;
    (void)GetTypeFromParser(ParsedType::getFromOpaquePtr(ControllingExprOrType),
                            &ControllingType);
    assert(ControllingType && "couldn't get the type out of the parser");
    ControllingExprOrType = ControllingType;
  }

  ExprResult ER = CreateGenericSelectionExpr(
      KeyLoc, DefaultLoc, RParenLoc, PredicateIsExpr, ControllingExprOrType,
      llvm::ArrayRef(Types, NumAssocs), ArgExprs);
  delete [] Types;
  return ER;
}

ExprResult Sema::CreateGenericSelectionExpr(
    SourceLocation KeyLoc, SourceLocation DefaultLoc, SourceLocation RParenLoc,
    bool PredicateIsExpr, void *ControllingExprOrType,
    ArrayRef<TypeSourceInfo *> Types, ArrayRef<Expr *> Exprs) {
  unsigned NumAssocs = Types.size();
  assert(NumAssocs == Exprs.size());
  assert(ControllingExprOrType &&
         "Must have either a controlling expression or a controlling type");

  Expr *ControllingExpr = nullptr;
  TypeSourceInfo *ControllingType = nullptr;
  if (PredicateIsExpr) {
    // Decay and strip qualifiers for the controlling expression type, and
    // handle placeholder type replacement. See committee discussion from WG14
    // DR423.
    EnterExpressionEvaluationContext Unevaluated(
        *this, Sema::ExpressionEvaluationContext::Unevaluated);
    ExprResult R = DefaultFunctionArrayLvalueConversion(
        reinterpret_cast<Expr *>(ControllingExprOrType));
    if (R.isInvalid())
      return ExprError();
    ControllingExpr = R.get();
  } else {
    // The extension form uses the type directly rather than converting it.
    ControllingType = reinterpret_cast<TypeSourceInfo *>(ControllingExprOrType);
    if (!ControllingType)
      return ExprError();
  }

  bool TypeErrorFound = false,
       IsResultDependent = ControllingExpr
                               ? ControllingExpr->isTypeDependent()
                               : ControllingType->getType()->isDependentType(),
       ContainsUnexpandedParameterPack =
           ControllingExpr
               ? ControllingExpr->containsUnexpandedParameterPack()
               : ControllingType->getType()->containsUnexpandedParameterPack();

  // The controlling expression is an unevaluated operand, so side effects are
  // likely unintended.
  if (!inTemplateInstantiation() && !IsResultDependent && ControllingExpr &&
      ControllingExpr->HasSideEffects(Context, false))
    Diag(ControllingExpr->getExprLoc(),
         diag::warn_side_effects_unevaluated_context);

  for (unsigned i = 0; i < NumAssocs; ++i) {
    if (Exprs[i]->containsUnexpandedParameterPack())
      ContainsUnexpandedParameterPack = true;

    if (Types[i]) {
      if (Types[i]->getType()->containsUnexpandedParameterPack())
        ContainsUnexpandedParameterPack = true;

      if (Types[i]->getType()->isDependentType()) {
        IsResultDependent = true;
      } else {
        // We relax the restriction on use of incomplete types and non-object
        // types with the type-based extension of _Generic. Allowing incomplete
        // objects means those can be used as "tags" for a type-safe way to map
        // to a value. Similarly, matching on function types rather than
        // function pointer types can be useful. However, the restriction on VM
        // types makes sense to retain as there are open questions about how
        // the selection can be made at compile time.
        //
        // C11 6.5.1.1p2 "The type name in a generic association shall specify a
        // complete object type other than a variably modified type."
        unsigned D = 0;
        if (ControllingExpr && Types[i]->getType()->isIncompleteType())
          D = diag::err_assoc_type_incomplete;
        else if (ControllingExpr && !Types[i]->getType()->isObjectType())
          D = diag::err_assoc_type_nonobject;
        else if (Types[i]->getType()->isVariablyModifiedType())
          D = diag::err_assoc_type_variably_modified;
        else if (ControllingExpr) {
          // Because the controlling expression undergoes lvalue conversion,
          // array conversion, and function conversion, an association which is
          // of array type, function type, or is qualified can never be
          // reached. We will warn about this so users are less surprised by
          // the unreachable association. However, we don't have to handle
          // function types; that's not an object type, so it's handled above.
          //
          // The logic is somewhat different for C++ because C++ has different
          // lvalue to rvalue conversion rules than C. [conv.lvalue]p1 says,
          // If T is a non-class type, the type of the prvalue is the cv-
          // unqualified version of T. Otherwise, the type of the prvalue is T.
          // The result of these rules is that all qualified types in an
          // association in C are unreachable, and in C++, only qualified non-
          // class types are unreachable.
          //
          // NB: this does not apply when the first operand is a type rather
          // than an expression, because the type form does not undergo
          // conversion.
          unsigned Reason = 0;
          QualType QT = Types[i]->getType();
          if (QT->isArrayType())
            Reason = 1;
          else if (QT.hasQualifiers() &&
                   (!LangOpts.CPlusPlus || !QT->isRecordType()))
            Reason = 2;

          if (Reason)
            Diag(Types[i]->getTypeLoc().getBeginLoc(),
                 diag::warn_unreachable_association)
                << QT << (Reason - 1);
        }

        if (D != 0) {
          Diag(Types[i]->getTypeLoc().getBeginLoc(), D)
            << Types[i]->getTypeLoc().getSourceRange()
            << Types[i]->getType();
          TypeErrorFound = true;
        }

        // C11 6.5.1.1p2 "No two generic associations in the same generic
        // selection shall specify compatible types."
        for (unsigned j = i+1; j < NumAssocs; ++j)
          if (Types[j] && !Types[j]->getType()->isDependentType() &&
              Context.typesAreCompatible(Types[i]->getType(),
                                         Types[j]->getType())) {
            Diag(Types[j]->getTypeLoc().getBeginLoc(),
                 diag::err_assoc_compatible_types)
              << Types[j]->getTypeLoc().getSourceRange()
              << Types[j]->getType()
              << Types[i]->getType();
            Diag(Types[i]->getTypeLoc().getBeginLoc(),
                 diag::note_compat_assoc)
              << Types[i]->getTypeLoc().getSourceRange()
              << Types[i]->getType();
            TypeErrorFound = true;
          }
      }
    }
  }
  if (TypeErrorFound)
    return ExprError();

  // If we determined that the generic selection is result-dependent, don't
  // try to compute the result expression.
  if (IsResultDependent) {
    if (ControllingExpr)
      return GenericSelectionExpr::Create(Context, KeyLoc, ControllingExpr,
                                          Types, Exprs, DefaultLoc, RParenLoc,
                                          ContainsUnexpandedParameterPack);
    return GenericSelectionExpr::Create(Context, KeyLoc, ControllingType, Types,
                                        Exprs, DefaultLoc, RParenLoc,
                                        ContainsUnexpandedParameterPack);
  }

  SmallVector<unsigned, 1> CompatIndices;
  unsigned DefaultIndex = -1U;
  // Look at the canonical type of the controlling expression in case it was a
  // deduced type like __auto_type. However, when issuing diagnostics, use the
  // type the user wrote in source rather than the canonical one.
  for (unsigned i = 0; i < NumAssocs; ++i) {
    if (!Types[i])
      DefaultIndex = i;
    else if (ControllingExpr &&
             Context.typesAreCompatible(
                 ControllingExpr->getType().getCanonicalType(),
                 Types[i]->getType()))
      CompatIndices.push_back(i);
    else if (ControllingType &&
             Context.typesAreCompatible(
                 ControllingType->getType().getCanonicalType(),
                 Types[i]->getType()))
      CompatIndices.push_back(i);
  }

  auto GetControllingRangeAndType = [](Expr *ControllingExpr,
                                       TypeSourceInfo *ControllingType) {
    // We strip parens here because the controlling expression is typically
    // parenthesized in macro definitions.
    if (ControllingExpr)
      ControllingExpr = ControllingExpr->IgnoreParens();

    SourceRange SR = ControllingExpr
                         ? ControllingExpr->getSourceRange()
                         : ControllingType->getTypeLoc().getSourceRange();
    QualType QT = ControllingExpr ? ControllingExpr->getType()
                                  : ControllingType->getType();

    return std::make_pair(SR, QT);
  };

  // C11 6.5.1.1p2 "The controlling expression of a generic selection shall have
  // type compatible with at most one of the types named in its generic
  // association list."
  if (CompatIndices.size() > 1) {
    auto P = GetControllingRangeAndType(ControllingExpr, ControllingType);
    SourceRange SR = P.first;
    Diag(SR.getBegin(), diag::err_generic_sel_multi_match)
        << SR << P.second << (unsigned)CompatIndices.size();
    for (unsigned I : CompatIndices) {
      Diag(Types[I]->getTypeLoc().getBeginLoc(),
           diag::note_compat_assoc)
        << Types[I]->getTypeLoc().getSourceRange()
        << Types[I]->getType();
    }
    return ExprError();
  }

  // C11 6.5.1.1p2 "If a generic selection has no default generic association,
  // its controlling expression shall have type compatible with exactly one of
  // the types named in its generic association list."
  if (DefaultIndex == -1U && CompatIndices.size() == 0) {
    auto P = GetControllingRangeAndType(ControllingExpr, ControllingType);
    SourceRange SR = P.first;
    Diag(SR.getBegin(), diag::err_generic_sel_no_match) << SR << P.second;
    return ExprError();
  }

  // C11 6.5.1.1p3 "If a generic selection has a generic association with a
  // type name that is compatible with the type of the controlling expression,
  // then the result expression of the generic selection is the expression
  // in that generic association. Otherwise, the result expression of the
  // generic selection is the expression in the default generic association."
  unsigned ResultIndex =
    CompatIndices.size() ? CompatIndices[0] : DefaultIndex;

  if (ControllingExpr) {
    return GenericSelectionExpr::Create(
        Context, KeyLoc, ControllingExpr, Types, Exprs, DefaultLoc, RParenLoc,
        ContainsUnexpandedParameterPack, ResultIndex);
  }
  return GenericSelectionExpr::Create(
      Context, KeyLoc, ControllingType, Types, Exprs, DefaultLoc, RParenLoc,
      ContainsUnexpandedParameterPack, ResultIndex);
}

static PredefinedIdentKind getPredefinedExprKind(tok::TokenKind Kind) {
  switch (Kind) {
  default:
    llvm_unreachable("unexpected TokenKind");
  case tok::kw___func__:
    return PredefinedIdentKind::Func; // [C99 6.4.2.2]
  case tok::kw___FUNCTION__:
    return PredefinedIdentKind::Function;
  case tok::kw___FUNCDNAME__:
    return PredefinedIdentKind::FuncDName; // [MS]
  case tok::kw___FUNCSIG__:
    return PredefinedIdentKind::FuncSig; // [MS]
  case tok::kw_L__FUNCTION__:
    return PredefinedIdentKind::LFunction; // [MS]
  case tok::kw_L__FUNCSIG__:
    return PredefinedIdentKind::LFuncSig; // [MS]
  case tok::kw___PRETTY_FUNCTION__:
    return PredefinedIdentKind::PrettyFunction; // [GNU]
  }
}

/// getPredefinedExprDecl - Returns Decl of a given DeclContext that can be used
/// to determine the value of a PredefinedExpr. This can be either a
/// block, lambda, captured statement, function, otherwise a nullptr.
static Decl *getPredefinedExprDecl(DeclContext *DC) {
  while (DC && !isa<BlockDecl, CapturedDecl, FunctionDecl, ObjCMethodDecl>(DC))
    DC = DC->getParent();
  return cast_or_null<Decl>(DC);
}

/// getUDSuffixLoc - Create a SourceLocation for a ud-suffix, given the
/// location of the token and the offset of the ud-suffix within it.
static SourceLocation getUDSuffixLoc(Sema &S, SourceLocation TokLoc,
                                     unsigned Offset) {
  return Lexer::AdvanceToTokenCharacter(TokLoc, Offset, S.getSourceManager(),
                                        S.getLangOpts());
}

/// BuildCookedLiteralOperatorCall - A user-defined literal was found. Look up
/// the corresponding cooked (non-raw) literal operator, and build a call to it.
static ExprResult BuildCookedLiteralOperatorCall(Sema &S, Scope *Scope,
                                                 IdentifierInfo *UDSuffix,
                                                 SourceLocation UDSuffixLoc,
                                                 ArrayRef<Expr*> Args,
                                                 SourceLocation LitEndLoc) {
  assert(Args.size() <= 2 && "too many arguments for literal operator");

  QualType ArgTy[2];
  for (unsigned ArgIdx = 0; ArgIdx != Args.size(); ++ArgIdx) {
    ArgTy[ArgIdx] = Args[ArgIdx]->getType();
    if (ArgTy[ArgIdx]->isArrayType())
      ArgTy[ArgIdx] = S.Context.getArrayDecayedType(ArgTy[ArgIdx]);
  }

  DeclarationName OpName =
    S.Context.DeclarationNames.getCXXLiteralOperatorName(UDSuffix);
  DeclarationNameInfo OpNameInfo(OpName, UDSuffixLoc);
  OpNameInfo.setCXXLiteralOperatorNameLoc(UDSuffixLoc);

  LookupResult R(S, OpName, UDSuffixLoc, Sema::LookupOrdinaryName);
  if (S.LookupLiteralOperator(Scope, R, llvm::ArrayRef(ArgTy, Args.size()),
                              /*AllowRaw*/ false, /*AllowTemplate*/ false,
                              /*AllowStringTemplatePack*/ false,
                              /*DiagnoseMissing*/ true) == Sema::LOLR_Error)
    return ExprError();

  return S.BuildLiteralOperatorCall(R, OpNameInfo, Args, LitEndLoc);
}

ExprResult Sema::ActOnUnevaluatedStringLiteral(ArrayRef<Token> StringToks) {
  // StringToks needs backing storage as it doesn't hold array elements itself
  std::vector<Token> ExpandedToks;
  if (getLangOpts().MicrosoftExt)
    StringToks = ExpandedToks = ExpandFunctionLocalPredefinedMacros(StringToks);

  StringLiteralParser Literal(StringToks, PP,
                              StringLiteralEvalMethod::Unevaluated);
  if (Literal.hadError)
    return ExprError();

  SmallVector<SourceLocation, 4> StringTokLocs;
  for (const Token &Tok : StringToks)
    StringTokLocs.push_back(Tok.getLocation());

  StringLiteral *Lit = StringLiteral::Create(
      Context, Literal.GetString(), StringLiteralKind::Unevaluated, false, {},
      &StringTokLocs[0], StringTokLocs.size());

  if (!Literal.getUDSuffix().empty()) {
    SourceLocation UDSuffixLoc =
        getUDSuffixLoc(*this, StringTokLocs[Literal.getUDSuffixToken()],
                       Literal.getUDSuffixOffset());
    return ExprError(Diag(UDSuffixLoc, diag::err_invalid_string_udl));
  }

  return Lit;
}

std::vector<Token>
Sema::ExpandFunctionLocalPredefinedMacros(ArrayRef<Token> Toks) {
  // MSVC treats some predefined identifiers (e.g. __FUNCTION__) as function
  // local macros that expand to string literals that may be concatenated.
  // These macros are expanded here (in Sema), because StringLiteralParser
  // (in Lex) doesn't know the enclosing function (because it hasn't been
  // parsed yet).
  assert(getLangOpts().MicrosoftExt);

  // Note: Although function local macros are defined only inside functions,
  // we ensure a valid `CurrentDecl` even outside of a function. This allows
  // expansion of macros into empty string literals without additional checks.
  Decl *CurrentDecl = getPredefinedExprDecl(CurContext);
  if (!CurrentDecl)
    CurrentDecl = Context.getTranslationUnitDecl();

  std::vector<Token> ExpandedToks;
  ExpandedToks.reserve(Toks.size());
  for (const Token &Tok : Toks) {
    if (!isFunctionLocalStringLiteralMacro(Tok.getKind(), getLangOpts())) {
      assert(tok::isStringLiteral(Tok.getKind()));
      ExpandedToks.emplace_back(Tok);
      continue;
    }
    if (isa<TranslationUnitDecl>(CurrentDecl))
      Diag(Tok.getLocation(), diag::ext_predef_outside_function);
    // Stringify predefined expression
    Diag(Tok.getLocation(), diag::ext_string_literal_from_predefined)
        << Tok.getKind();
    SmallString<64> Str;
    llvm::raw_svector_ostream OS(Str);
    Token &Exp = ExpandedToks.emplace_back();
    Exp.startToken();
    if (Tok.getKind() == tok::kw_L__FUNCTION__ ||
        Tok.getKind() == tok::kw_L__FUNCSIG__) {
      OS << 'L';
      Exp.setKind(tok::wide_string_literal);
    } else {
      Exp.setKind(tok::string_literal);
    }
    OS << '"'
       << Lexer::Stringify(PredefinedExpr::ComputeName(
              getPredefinedExprKind(Tok.getKind()), CurrentDecl))
       << '"';
    PP.CreateString(OS.str(), Exp, Tok.getLocation(), Tok.getEndLoc());
  }
  return ExpandedToks;
}

ExprResult
Sema::ActOnStringLiteral(ArrayRef<Token> StringToks, Scope *UDLScope) {
  assert(!StringToks.empty() && "Must have at least one string!");

  // StringToks needs backing storage as it doesn't hold array elements itself
  std::vector<Token> ExpandedToks;
  if (getLangOpts().MicrosoftExt)
    StringToks = ExpandedToks = ExpandFunctionLocalPredefinedMacros(StringToks);

  StringLiteralParser Literal(StringToks, PP);
  if (Literal.hadError)
    return ExprError();

  SmallVector<SourceLocation, 4> StringTokLocs;
  for (const Token &Tok : StringToks)
    StringTokLocs.push_back(Tok.getLocation());

  QualType CharTy = Context.CharTy;
  StringLiteralKind Kind = StringLiteralKind::Ordinary;
  if (Literal.isWide()) {
    CharTy = Context.getWideCharType();
    Kind = StringLiteralKind::Wide;
  } else if (Literal.isUTF8()) {
    if (getLangOpts().Char8)
      CharTy = Context.Char8Ty;
    else if (getLangOpts().C23)
      CharTy = Context.UnsignedCharTy;
    Kind = StringLiteralKind::UTF8;
  } else if (Literal.isUTF16()) {
    CharTy = Context.Char16Ty;
    Kind = StringLiteralKind::UTF16;
  } else if (Literal.isUTF32()) {
    CharTy = Context.Char32Ty;
    Kind = StringLiteralKind::UTF32;
  } else if (Literal.isPascal()) {
    CharTy = Context.UnsignedCharTy;
  }

  // Warn on u8 string literals before C++20 and C23, whose type
  // was an array of char before but becomes an array of char8_t.
  // In C++20, it cannot be used where a pointer to char is expected.
  // In C23, it might have an unexpected value if char was signed.
  if (Kind == StringLiteralKind::UTF8 &&
      (getLangOpts().CPlusPlus
           ? !getLangOpts().CPlusPlus20 && !getLangOpts().Char8
           : !getLangOpts().C23)) {
    Diag(StringTokLocs.front(), getLangOpts().CPlusPlus
                                    ? diag::warn_cxx20_compat_utf8_string
                                    : diag::warn_c23_compat_utf8_string);

    // Create removals for all 'u8' prefixes in the string literal(s). This
    // ensures C++20/C23 compatibility (but may change the program behavior when
    // built by non-Clang compilers for which the execution character set is
    // not always UTF-8).
    auto RemovalDiag = PDiag(diag::note_cxx20_c23_compat_utf8_string_remove_u8);
    SourceLocation RemovalDiagLoc;
    for (const Token &Tok : StringToks) {
      if (Tok.getKind() == tok::utf8_string_literal) {
        if (RemovalDiagLoc.isInvalid())
          RemovalDiagLoc = Tok.getLocation();
        RemovalDiag << FixItHint::CreateRemoval(CharSourceRange::getCharRange(
            Tok.getLocation(),
            Lexer::AdvanceToTokenCharacter(Tok.getLocation(), 2,
                                           getSourceManager(), getLangOpts())));
      }
    }
    Diag(RemovalDiagLoc, RemovalDiag);
  }

  QualType StrTy =
      Context.getStringLiteralArrayType(CharTy, Literal.GetNumStringChars());

  // Pass &StringTokLocs[0], StringTokLocs.size() to factory!
  StringLiteral *Lit = StringLiteral::Create(Context, Literal.GetString(),
                                             Kind, Literal.Pascal, StrTy,
                                             &StringTokLocs[0],
                                             StringTokLocs.size());
  if (Literal.getUDSuffix().empty())
    return Lit;

  // We're building a user-defined literal.
  IdentifierInfo *UDSuffix = &Context.Idents.get(Literal.getUDSuffix());
  SourceLocation UDSuffixLoc =
    getUDSuffixLoc(*this, StringTokLocs[Literal.getUDSuffixToken()],
                   Literal.getUDSuffixOffset());

  // Make sure we're allowed user-defined literals here.
  if (!UDLScope)
    return ExprError(Diag(UDSuffixLoc, diag::err_invalid_string_udl));

  // C++11 [lex.ext]p5: The literal L is treated as a call of the form
  //   operator "" X (str, len)
  QualType SizeType = Context.getSizeType();

  DeclarationName OpName =
    Context.DeclarationNames.getCXXLiteralOperatorName(UDSuffix);
  DeclarationNameInfo OpNameInfo(OpName, UDSuffixLoc);
  OpNameInfo.setCXXLiteralOperatorNameLoc(UDSuffixLoc);

  QualType ArgTy[] = {
    Context.getArrayDecayedType(StrTy), SizeType
  };

  LookupResult R(*this, OpName, UDSuffixLoc, LookupOrdinaryName);
  switch (LookupLiteralOperator(UDLScope, R, ArgTy,
                                /*AllowRaw*/ false, /*AllowTemplate*/ true,
                                /*AllowStringTemplatePack*/ true,
                                /*DiagnoseMissing*/ true, Lit)) {

  case LOLR_Cooked: {
    llvm::APInt Len(Context.getIntWidth(SizeType), Literal.GetNumStringChars());
    IntegerLiteral *LenArg = IntegerLiteral::Create(Context, Len, SizeType,
                                                    StringTokLocs[0]);
    Expr *Args[] = { Lit, LenArg };

    return BuildLiteralOperatorCall(R, OpNameInfo, Args, StringTokLocs.back());
  }

  case LOLR_Template: {
    TemplateArgumentListInfo ExplicitArgs;
    TemplateArgument Arg(Lit);
    TemplateArgumentLocInfo ArgInfo(Lit);
    ExplicitArgs.addArgument(TemplateArgumentLoc(Arg, ArgInfo));
    return BuildLiteralOperatorCall(R, OpNameInfo, std::nullopt,
                                    StringTokLocs.back(), &ExplicitArgs);
  }

  case LOLR_StringTemplatePack: {
    TemplateArgumentListInfo ExplicitArgs;

    unsigned CharBits = Context.getIntWidth(CharTy);
    bool CharIsUnsigned = CharTy->isUnsignedIntegerType();
    llvm::APSInt Value(CharBits, CharIsUnsigned);

    TemplateArgument TypeArg(CharTy);
    TemplateArgumentLocInfo TypeArgInfo(Context.getTrivialTypeSourceInfo(CharTy));
    ExplicitArgs.addArgument(TemplateArgumentLoc(TypeArg, TypeArgInfo));

    for (unsigned I = 0, N = Lit->getLength(); I != N; ++I) {
      Value = Lit->getCodeUnit(I);
      TemplateArgument Arg(Context, Value, CharTy);
      TemplateArgumentLocInfo ArgInfo;
      ExplicitArgs.addArgument(TemplateArgumentLoc(Arg, ArgInfo));
    }
    return BuildLiteralOperatorCall(R, OpNameInfo, std::nullopt,
                                    StringTokLocs.back(), &ExplicitArgs);
  }
  case LOLR_Raw:
  case LOLR_ErrorNoDiagnostic:
    llvm_unreachable("unexpected literal operator lookup result");
  case LOLR_Error:
    return ExprError();
  }
  llvm_unreachable("unexpected literal operator lookup result");
}

DeclRefExpr *
Sema::BuildDeclRefExpr(ValueDecl *D, QualType Ty, ExprValueKind VK,
                       SourceLocation Loc,
                       const CXXScopeSpec *SS) {
  DeclarationNameInfo NameInfo(D->getDeclName(), Loc);
  return BuildDeclRefExpr(D, Ty, VK, NameInfo, SS);
}

DeclRefExpr *
Sema::BuildDeclRefExpr(ValueDecl *D, QualType Ty, ExprValueKind VK,
                       const DeclarationNameInfo &NameInfo,
                       const CXXScopeSpec *SS, NamedDecl *FoundD,
                       SourceLocation TemplateKWLoc,
                       const TemplateArgumentListInfo *TemplateArgs) {
  NestedNameSpecifierLoc NNS =
      SS ? SS->getWithLocInContext(Context) : NestedNameSpecifierLoc();
  return BuildDeclRefExpr(D, Ty, VK, NameInfo, NNS, FoundD, TemplateKWLoc,
                          TemplateArgs);
}

// CUDA/HIP: Check whether a captured reference variable is referencing a
// host variable in a device or host device lambda.
static bool isCapturingReferenceToHostVarInCUDADeviceLambda(const Sema &S,
                                                            VarDecl *VD) {
  if (!S.getLangOpts().CUDA || !VD->hasInit())
    return false;
  assert(VD->getType()->isReferenceType());

  // Check whether the reference variable is referencing a host variable.
  auto *DRE = dyn_cast<DeclRefExpr>(VD->getInit());
  if (!DRE)
    return false;
  auto *Referee = dyn_cast<VarDecl>(DRE->getDecl());
  if (!Referee || !Referee->hasGlobalStorage() ||
      Referee->hasAttr<CUDADeviceAttr>())
    return false;

  // Check whether the current function is a device or host device lambda.
  // Check whether the reference variable is a capture by getDeclContext()
  // since refersToEnclosingVariableOrCapture() is not ready at this point.
  auto *MD = dyn_cast_or_null<CXXMethodDecl>(S.CurContext);
  if (MD && MD->getParent()->isLambda() &&
      MD->getOverloadedOperator() == OO_Call && MD->hasAttr<CUDADeviceAttr>() &&
      VD->getDeclContext() != MD)
    return true;

  return false;
}

NonOdrUseReason Sema::getNonOdrUseReasonInCurrentContext(ValueDecl *D) {
  // A declaration named in an unevaluated operand never constitutes an odr-use.
  if (isUnevaluatedContext())
    return NOUR_Unevaluated;

  // C++2a [basic.def.odr]p4:
  //   A variable x whose name appears as a potentially-evaluated expression e
  //   is odr-used by e unless [...] x is a reference that is usable in
  //   constant expressions.
  // CUDA/HIP:
  //   If a reference variable referencing a host variable is captured in a
  //   device or host device lambda, the value of the referee must be copied
  //   to the capture and the reference variable must be treated as odr-use
  //   since the value of the referee is not known at compile time and must
  //   be loaded from the captured.
  if (VarDecl *VD = dyn_cast<VarDecl>(D)) {
    if (VD->getType()->isReferenceType() &&
        !(getLangOpts().OpenMP && OpenMP().isOpenMPCapturedDecl(D)) &&
        !isCapturingReferenceToHostVarInCUDADeviceLambda(*this, VD) &&
        VD->isUsableInConstantExpressions(Context))
      return NOUR_Constant;
  }

  // All remaining non-variable cases constitute an odr-use. For variables, we
  // need to wait and see how the expression is used.
  return NOUR_None;
}

DeclRefExpr *
Sema::BuildDeclRefExpr(ValueDecl *D, QualType Ty, ExprValueKind VK,
                       const DeclarationNameInfo &NameInfo,
                       NestedNameSpecifierLoc NNS, NamedDecl *FoundD,
                       SourceLocation TemplateKWLoc,
                       const TemplateArgumentListInfo *TemplateArgs) {
  bool RefersToCapturedVariable = isa<VarDecl, BindingDecl>(D) &&
                                  NeedToCaptureVariable(D, NameInfo.getLoc());

  DeclRefExpr *E = DeclRefExpr::Create(
      Context, NNS, TemplateKWLoc, D, RefersToCapturedVariable, NameInfo, Ty,
      VK, FoundD, TemplateArgs, getNonOdrUseReasonInCurrentContext(D));
  MarkDeclRefReferenced(E);

  // C++ [except.spec]p17:
  //   An exception-specification is considered to be needed when:
  //   - in an expression, the function is the unique lookup result or
  //     the selected member of a set of overloaded functions.
  //
  // We delay doing this until after we've built the function reference and
  // marked it as used so that:
  //  a) if the function is defaulted, we get errors from defining it before /
  //     instead of errors from computing its exception specification, and
  //  b) if the function is a defaulted comparison, we can use the body we
  //     build when defining it as input to the exception specification
  //     computation rather than computing a new body.
  if (const auto *FPT = Ty->getAs<FunctionProtoType>()) {
    if (isUnresolvedExceptionSpec(FPT->getExceptionSpecType())) {
      if (const auto *NewFPT = ResolveExceptionSpec(NameInfo.getLoc(), FPT))
        E->setType(Context.getQualifiedType(NewFPT, Ty.getQualifiers()));
    }
  }

  if (getLangOpts().ObjCWeak && isa<VarDecl>(D) &&
      Ty.getObjCLifetime() == Qualifiers::OCL_Weak && !isUnevaluatedContext() &&
      !Diags.isIgnored(diag::warn_arc_repeated_use_of_weak, E->getBeginLoc()))
    getCurFunction()->recordUseOfWeak(E);

  const auto *FD = dyn_cast<FieldDecl>(D);
  if (const auto *IFD = dyn_cast<IndirectFieldDecl>(D))
    FD = IFD->getAnonField();
  if (FD) {
    UnusedPrivateFields.remove(FD);
    // Just in case we're building an illegal pointer-to-member.
    if (FD->isBitField())
      E->setObjectKind(OK_BitField);
  }

  // C++ [expr.prim]/8: The expression [...] is a bit-field if the identifier
  // designates a bit-field.
  if (const auto *BD = dyn_cast<BindingDecl>(D))
    if (const auto *BE = BD->getBinding())
      E->setObjectKind(BE->getObjectKind());

  return E;
}

void
Sema::DecomposeUnqualifiedId(const UnqualifiedId &Id,
                             TemplateArgumentListInfo &Buffer,
                             DeclarationNameInfo &NameInfo,
                             const TemplateArgumentListInfo *&TemplateArgs) {
  if (Id.getKind() == UnqualifiedIdKind::IK_TemplateId) {
    Buffer.setLAngleLoc(Id.TemplateId->LAngleLoc);
    Buffer.setRAngleLoc(Id.TemplateId->RAngleLoc);

    ASTTemplateArgsPtr TemplateArgsPtr(Id.TemplateId->getTemplateArgs(),
                                       Id.TemplateId->NumArgs);
    translateTemplateArguments(TemplateArgsPtr, Buffer);

    TemplateName TName = Id.TemplateId->Template.get();
    SourceLocation TNameLoc = Id.TemplateId->TemplateNameLoc;
    NameInfo = Context.getNameForTemplate(TName, TNameLoc);
    TemplateArgs = &Buffer;
  } else {
    NameInfo = GetNameFromUnqualifiedId(Id);
    TemplateArgs = nullptr;
  }
}

static void emitEmptyLookupTypoDiagnostic(
    const TypoCorrection &TC, Sema &SemaRef, const CXXScopeSpec &SS,
    DeclarationName Typo, SourceLocation TypoLoc, ArrayRef<Expr *> Args,
    unsigned DiagnosticID, unsigned DiagnosticSuggestID) {
  DeclContext *Ctx =
      SS.isEmpty() ? nullptr : SemaRef.computeDeclContext(SS, false);
  if (!TC) {
    // Emit a special diagnostic for failed member lookups.
    // FIXME: computing the declaration context might fail here (?)
    if (Ctx)
      SemaRef.Diag(TypoLoc, diag::err_no_member) << Typo << Ctx
                                                 << SS.getRange();
    else
      SemaRef.Diag(TypoLoc, DiagnosticID) << Typo;
    return;
  }

  std::string CorrectedStr = TC.getAsString(SemaRef.getLangOpts());
  bool DroppedSpecifier =
      TC.WillReplaceSpecifier() && Typo.getAsString() == CorrectedStr;
  unsigned NoteID = TC.getCorrectionDeclAs<ImplicitParamDecl>()
                        ? diag::note_implicit_param_decl
                        : diag::note_previous_decl;
  if (!Ctx)
    SemaRef.diagnoseTypo(TC, SemaRef.PDiag(DiagnosticSuggestID) << Typo,
                         SemaRef.PDiag(NoteID));
  else
    SemaRef.diagnoseTypo(TC, SemaRef.PDiag(diag::err_no_member_suggest)
                                 << Typo << Ctx << DroppedSpecifier
                                 << SS.getRange(),
                         SemaRef.PDiag(NoteID));
}

bool Sema::DiagnoseDependentMemberLookup(const LookupResult &R) {
  // During a default argument instantiation the CurContext points
  // to a CXXMethodDecl; but we can't apply a this-> fixit inside a
  // function parameter list, hence add an explicit check.
  bool isDefaultArgument =
      !CodeSynthesisContexts.empty() &&
      CodeSynthesisContexts.back().Kind ==
          CodeSynthesisContext::DefaultFunctionArgumentInstantiation;
  const auto *CurMethod = dyn_cast<CXXMethodDecl>(CurContext);
  bool isInstance = CurMethod && CurMethod->isInstance() &&
                    R.getNamingClass() == CurMethod->getParent() &&
                    !isDefaultArgument;

  // There are two ways we can find a class-scope declaration during template
  // instantiation that we did not find in the template definition: if it is a
  // member of a dependent base class, or if it is declared after the point of
  // use in the same class. Distinguish these by comparing the class in which
  // the member was found to the naming class of the lookup.
  unsigned DiagID = diag::err_found_in_dependent_base;
  unsigned NoteID = diag::note_member_declared_at;
  if (R.getRepresentativeDecl()->getDeclContext()->Equals(R.getNamingClass())) {
    DiagID = getLangOpts().MSVCCompat ? diag::ext_found_later_in_class
                                      : diag::err_found_later_in_class;
  } else if (getLangOpts().MSVCCompat) {
    DiagID = diag::ext_found_in_dependent_base;
    NoteID = diag::note_dependent_member_use;
  }

  if (isInstance) {
    // Give a code modification hint to insert 'this->'.
    Diag(R.getNameLoc(), DiagID)
        << R.getLookupName()
        << FixItHint::CreateInsertion(R.getNameLoc(), "this->");
    CheckCXXThisCapture(R.getNameLoc());
  } else {
    // FIXME: Add a FixItHint to insert 'Base::' or 'Derived::' (assuming
    // they're not shadowed).
    Diag(R.getNameLoc(), DiagID) << R.getLookupName();
  }

  for (const NamedDecl *D : R)
    Diag(D->getLocation(), NoteID);

  // Return true if we are inside a default argument instantiation
  // and the found name refers to an instance member function, otherwise
  // the caller will try to create an implicit member call and this is wrong
  // for default arguments.
  //
  // FIXME: Is this special case necessary? We could allow the caller to
  // diagnose this.
  if (isDefaultArgument && ((*R.begin())->isCXXInstanceMember())) {
    Diag(R.getNameLoc(), diag::err_member_call_without_object) << 0;
    return true;
  }

  // Tell the callee to try to recover.
  return false;
}

bool Sema::DiagnoseEmptyLookup(Scope *S, CXXScopeSpec &SS, LookupResult &R,
                               CorrectionCandidateCallback &CCC,
                               TemplateArgumentListInfo *ExplicitTemplateArgs,
                               ArrayRef<Expr *> Args, DeclContext *LookupCtx,
                               TypoExpr **Out) {
  DeclarationName Name = R.getLookupName();

  unsigned diagnostic = diag::err_undeclared_var_use;
  unsigned diagnostic_suggest = diag::err_undeclared_var_use_suggest;
  if (Name.getNameKind() == DeclarationName::CXXOperatorName ||
      Name.getNameKind() == DeclarationName::CXXLiteralOperatorName ||
      Name.getNameKind() == DeclarationName::CXXConversionFunctionName) {
    diagnostic = diag::err_undeclared_use;
    diagnostic_suggest = diag::err_undeclared_use_suggest;
  }

  // If the original lookup was an unqualified lookup, fake an
  // unqualified lookup.  This is useful when (for example) the
  // original lookup would not have found something because it was a
  // dependent name.
  DeclContext *DC =
      LookupCtx ? LookupCtx : (SS.isEmpty() ? CurContext : nullptr);
  while (DC) {
    if (isa<CXXRecordDecl>(DC)) {
      LookupQualifiedName(R, DC);

      if (!R.empty()) {
        // Don't give errors about ambiguities in this lookup.
        R.suppressDiagnostics();

        // If there's a best viable function among the results, only mention
        // that one in the notes.
        OverloadCandidateSet Candidates(R.getNameLoc(),
                                        OverloadCandidateSet::CSK_Normal);
        AddOverloadedCallCandidates(R, ExplicitTemplateArgs, Args, Candidates);
        OverloadCandidateSet::iterator Best;
        if (Candidates.BestViableFunction(*this, R.getNameLoc(), Best) ==
            OR_Success) {
          R.clear();
          R.addDecl(Best->FoundDecl.getDecl(), Best->FoundDecl.getAccess());
          R.resolveKind();
        }

        return DiagnoseDependentMemberLookup(R);
      }

      R.clear();
    }

    DC = DC->getLookupParent();
  }

  // We didn't find anything, so try to correct for a typo.
  TypoCorrection Corrected;
  if (S && Out) {
    SourceLocation TypoLoc = R.getNameLoc();
    assert(!ExplicitTemplateArgs &&
           "Diagnosing an empty lookup with explicit template args!");
    *Out = CorrectTypoDelayed(
        R.getLookupNameInfo(), R.getLookupKind(), S, &SS, CCC,
        [=](const TypoCorrection &TC) {
          emitEmptyLookupTypoDiagnostic(TC, *this, SS, Name, TypoLoc, Args,
                                        diagnostic, diagnostic_suggest);
        },
        nullptr, CTK_ErrorRecovery, LookupCtx);
    if (*Out)
      return true;
  } else if (S && (Corrected =
                       CorrectTypo(R.getLookupNameInfo(), R.getLookupKind(), S,
                                   &SS, CCC, CTK_ErrorRecovery, LookupCtx))) {
    std::string CorrectedStr(Corrected.getAsString(getLangOpts()));
    bool DroppedSpecifier =
        Corrected.WillReplaceSpecifier() && Name.getAsString() == CorrectedStr;
    R.setLookupName(Corrected.getCorrection());

    bool AcceptableWithRecovery = false;
    bool AcceptableWithoutRecovery = false;
    NamedDecl *ND = Corrected.getFoundDecl();
    if (ND) {
      if (Corrected.isOverloaded()) {
        OverloadCandidateSet OCS(R.getNameLoc(),
                                 OverloadCandidateSet::CSK_Normal);
        OverloadCandidateSet::iterator Best;
        for (NamedDecl *CD : Corrected) {
          if (FunctionTemplateDecl *FTD =
                   dyn_cast<FunctionTemplateDecl>(CD))
            AddTemplateOverloadCandidate(
                FTD, DeclAccessPair::make(FTD, AS_none), ExplicitTemplateArgs,
                Args, OCS);
          else if (FunctionDecl *FD = dyn_cast<FunctionDecl>(CD))
            if (!ExplicitTemplateArgs || ExplicitTemplateArgs->size() == 0)
              AddOverloadCandidate(FD, DeclAccessPair::make(FD, AS_none),
                                   Args, OCS);
        }
        switch (OCS.BestViableFunction(*this, R.getNameLoc(), Best)) {
        case OR_Success:
          ND = Best->FoundDecl;
          Corrected.setCorrectionDecl(ND);
          break;
        default:
          // FIXME: Arbitrarily pick the first declaration for the note.
          Corrected.setCorrectionDecl(ND);
          break;
        }
      }
      R.addDecl(ND);
      if (getLangOpts().CPlusPlus && ND->isCXXClassMember()) {
        CXXRecordDecl *Record = nullptr;
        if (Corrected.getCorrectionSpecifier()) {
          const Type *Ty = Corrected.getCorrectionSpecifier()->getAsType();
          Record = Ty->getAsCXXRecordDecl();
        }
        if (!Record)
          Record = cast<CXXRecordDecl>(
              ND->getDeclContext()->getRedeclContext());
        R.setNamingClass(Record);
      }

      auto *UnderlyingND = ND->getUnderlyingDecl();
      AcceptableWithRecovery = isa<ValueDecl>(UnderlyingND) ||
                               isa<FunctionTemplateDecl>(UnderlyingND);
      // FIXME: If we ended up with a typo for a type name or
      // Objective-C class name, we're in trouble because the parser
      // is in the wrong place to recover. Suggest the typo
      // correction, but don't make it a fix-it since we're not going
      // to recover well anyway.
      AcceptableWithoutRecovery = isa<TypeDecl>(UnderlyingND) ||
                                  getAsTypeTemplateDecl(UnderlyingND) ||
                                  isa<ObjCInterfaceDecl>(UnderlyingND);
    } else {
      // FIXME: We found a keyword. Suggest it, but don't provide a fix-it
      // because we aren't able to recover.
      AcceptableWithoutRecovery = true;
    }

    if (AcceptableWithRecovery || AcceptableWithoutRecovery) {
      unsigned NoteID = Corrected.getCorrectionDeclAs<ImplicitParamDecl>()
                            ? diag::note_implicit_param_decl
                            : diag::note_previous_decl;
      if (SS.isEmpty())
        diagnoseTypo(Corrected, PDiag(diagnostic_suggest) << Name,
                     PDiag(NoteID), AcceptableWithRecovery);
      else
        diagnoseTypo(Corrected, PDiag(diag::err_no_member_suggest)
                                  << Name << computeDeclContext(SS, false)
                                  << DroppedSpecifier << SS.getRange(),
                     PDiag(NoteID), AcceptableWithRecovery);

      // Tell the callee whether to try to recover.
      return !AcceptableWithRecovery;
    }
  }
  R.clear();

  // Emit a special diagnostic for failed member lookups.
  // FIXME: computing the declaration context might fail here (?)
  if (!SS.isEmpty()) {
    Diag(R.getNameLoc(), diag::err_no_member)
      << Name << computeDeclContext(SS, false)
      << SS.getRange();
    return true;
  }

  // Give up, we can't recover.
  Diag(R.getNameLoc(), diagnostic) << Name;
  return true;
}

/// In Microsoft mode, if we are inside a template class whose parent class has
/// dependent base classes, and we can't resolve an unqualified identifier, then
/// assume the identifier is a member of a dependent base class.  We can only
/// recover successfully in static methods, instance methods, and other contexts
/// where 'this' is available.  This doesn't precisely match MSVC's
/// instantiation model, but it's close enough.
static Expr *
recoverFromMSUnqualifiedLookup(Sema &S, ASTContext &Context,
                               DeclarationNameInfo &NameInfo,
                               SourceLocation TemplateKWLoc,
                               const TemplateArgumentListInfo *TemplateArgs) {
  // Only try to recover from lookup into dependent bases in static methods or
  // contexts where 'this' is available.
  QualType ThisType = S.getCurrentThisType();
  const CXXRecordDecl *RD = nullptr;
  if (!ThisType.isNull())
    RD = ThisType->getPointeeType()->getAsCXXRecordDecl();
  else if (auto *MD = dyn_cast<CXXMethodDecl>(S.CurContext))
    RD = MD->getParent();
  if (!RD || !RD->hasDefinition() || !RD->hasAnyDependentBases())
    return nullptr;

  // Diagnose this as unqualified lookup into a dependent base class.  If 'this'
  // is available, suggest inserting 'this->' as a fixit.
  SourceLocation Loc = NameInfo.getLoc();
  auto DB = S.Diag(Loc, diag::ext_undeclared_unqual_id_with_dependent_base);
  DB << NameInfo.getName() << RD;

  if (!ThisType.isNull()) {
    DB << FixItHint::CreateInsertion(Loc, "this->");
    return CXXDependentScopeMemberExpr::Create(
        Context, /*This=*/nullptr, ThisType, /*IsArrow=*/true,
        /*Op=*/SourceLocation(), NestedNameSpecifierLoc(), TemplateKWLoc,
        /*FirstQualifierFoundInScope=*/nullptr, NameInfo, TemplateArgs);
  }

  // Synthesize a fake NNS that points to the derived class.  This will
  // perform name lookup during template instantiation.
  CXXScopeSpec SS;
  auto *NNS =
      NestedNameSpecifier::Create(Context, nullptr, true, RD->getTypeForDecl());
  SS.MakeTrivial(Context, NNS, SourceRange(Loc, Loc));
  return DependentScopeDeclRefExpr::Create(
      Context, SS.getWithLocInContext(Context), TemplateKWLoc, NameInfo,
      TemplateArgs);
}

ExprResult
Sema::ActOnIdExpression(Scope *S, CXXScopeSpec &SS,
                        SourceLocation TemplateKWLoc, UnqualifiedId &Id,
                        bool HasTrailingLParen, bool IsAddressOfOperand,
                        CorrectionCandidateCallback *CCC,
                        bool IsInlineAsmIdentifier, Token *KeywordReplacement) {
  assert(!(IsAddressOfOperand && HasTrailingLParen) &&
         "cannot be direct & operand and have a trailing lparen");
  if (SS.isInvalid())
    return ExprError();

  TemplateArgumentListInfo TemplateArgsBuffer;

  // Decompose the UnqualifiedId into the following data.
  DeclarationNameInfo NameInfo;
  const TemplateArgumentListInfo *TemplateArgs;
  DecomposeUnqualifiedId(Id, TemplateArgsBuffer, NameInfo, TemplateArgs);

  DeclarationName Name = NameInfo.getName();
  IdentifierInfo *II = Name.getAsIdentifierInfo();
  SourceLocation NameLoc = NameInfo.getLoc();

  if (II && II->isEditorPlaceholder()) {
    // FIXME: When typed placeholders are supported we can create a typed
    // placeholder expression node.
    return ExprError();
  }

  // This specially handles arguments of attributes appertains to a type of C
  // struct field such that the name lookup within a struct finds the member
  // name, which is not the case for other contexts in C.
  if (isAttrContext() && !getLangOpts().CPlusPlus && S->isClassScope()) {
    // See if this is reference to a field of struct.
    LookupResult R(*this, NameInfo, LookupMemberName);
    // LookupName handles a name lookup from within anonymous struct.
    if (LookupName(R, S)) {
      if (auto *VD = dyn_cast<ValueDecl>(R.getFoundDecl())) {
        QualType type = VD->getType().getNonReferenceType();
        // This will eventually be translated into MemberExpr upon
        // the use of instantiated struct fields.
        return BuildDeclRefExpr(VD, type, VK_LValue, NameLoc);
      }
    }
  }

  // Perform the required lookup.
  LookupResult R(*this, NameInfo,
                 (Id.getKind() == UnqualifiedIdKind::IK_ImplicitSelfParam)
                     ? LookupObjCImplicitSelfParam
                     : LookupOrdinaryName);
  if (TemplateKWLoc.isValid() || TemplateArgs) {
    // Lookup the template name again to correctly establish the context in
    // which it was found. This is really unfortunate as we already did the
    // lookup to determine that it was a template name in the first place. If
    // this becomes a performance hit, we can work harder to preserve those
    // results until we get here but it's likely not worth it.
    AssumedTemplateKind AssumedTemplate;
    if (LookupTemplateName(R, S, SS, /*ObjectType=*/QualType(),
                           /*EnteringContext=*/false, TemplateKWLoc,
                           &AssumedTemplate))
      return ExprError();

    if (R.wasNotFoundInCurrentInstantiation() || SS.isInvalid())
      return ActOnDependentIdExpression(SS, TemplateKWLoc, NameInfo,
                                        IsAddressOfOperand, TemplateArgs);
  } else {
    bool IvarLookupFollowUp = II && !SS.isSet() && getCurMethodDecl();
    LookupParsedName(R, S, &SS, /*ObjectType=*/QualType(),
                     /*AllowBuiltinCreation=*/!IvarLookupFollowUp);

    // If the result might be in a dependent base class, this is a dependent
    // id-expression.
    if (R.wasNotFoundInCurrentInstantiation() || SS.isInvalid())
      return ActOnDependentIdExpression(SS, TemplateKWLoc, NameInfo,
                                        IsAddressOfOperand, TemplateArgs);

    // If this reference is in an Objective-C method, then we need to do
    // some special Objective-C lookup, too.
    if (IvarLookupFollowUp) {
      ExprResult E(ObjC().LookupInObjCMethod(R, S, II, true));
      if (E.isInvalid())
        return ExprError();

      if (Expr *Ex = E.getAs<Expr>())
        return Ex;
    }
  }

  if (R.isAmbiguous())
    return ExprError();

  // This could be an implicitly declared function reference if the language
  // mode allows it as a feature.
  if (R.empty() && HasTrailingLParen && II &&
      getLangOpts().implicitFunctionsAllowed()) {
    NamedDecl *D = ImplicitlyDefineFunction(NameLoc, *II, S);
    if (D) R.addDecl(D);
  }

  // Determine whether this name might be a candidate for
  // argument-dependent lookup.
  bool ADL = UseArgumentDependentLookup(SS, R, HasTrailingLParen);

  if (R.empty() && !ADL) {
    if (SS.isEmpty() && getLangOpts().MSVCCompat) {
      if (Expr *E = recoverFromMSUnqualifiedLookup(*this, Context, NameInfo,
                                                   TemplateKWLoc, TemplateArgs))
        return E;
    }

    // Don't diagnose an empty lookup for inline assembly.
    if (IsInlineAsmIdentifier)
      return ExprError();

    // If this name wasn't predeclared and if this is not a function
    // call, diagnose the problem.
    TypoExpr *TE = nullptr;
    DefaultFilterCCC DefaultValidator(II, SS.isValid() ? SS.getScopeRep()
                                                       : nullptr);
    DefaultValidator.IsAddressOfOperand = IsAddressOfOperand;
    assert((!CCC || CCC->IsAddressOfOperand == IsAddressOfOperand) &&
           "Typo correction callback misconfigured");
    if (CCC) {
      // Make sure the callback knows what the typo being diagnosed is.
      CCC->setTypoName(II);
      if (SS.isValid())
        CCC->setTypoNNS(SS.getScopeRep());
    }
    // FIXME: DiagnoseEmptyLookup produces bad diagnostics if we're looking for
    // a template name, but we happen to have always already looked up the name
    // before we get here if it must be a template name.
    if (DiagnoseEmptyLookup(S, SS, R, CCC ? *CCC : DefaultValidator, nullptr,
                            std::nullopt, nullptr, &TE)) {
      if (TE && KeywordReplacement) {
        auto &State = getTypoExprState(TE);
        auto BestTC = State.Consumer->getNextCorrection();
        if (BestTC.isKeyword()) {
          auto *II = BestTC.getCorrectionAsIdentifierInfo();
          if (State.DiagHandler)
            State.DiagHandler(BestTC);
          KeywordReplacement->startToken();
          KeywordReplacement->setKind(II->getTokenID());
          KeywordReplacement->setIdentifierInfo(II);
          KeywordReplacement->setLocation(BestTC.getCorrectionRange().getBegin());
          // Clean up the state associated with the TypoExpr, since it has
          // now been diagnosed (without a call to CorrectDelayedTyposInExpr).
          clearDelayedTypo(TE);
          // Signal that a correction to a keyword was performed by returning a
          // valid-but-null ExprResult.
          return (Expr*)nullptr;
        }
        State.Consumer->resetCorrectionStream();
      }
      return TE ? TE : ExprError();
    }

    assert(!R.empty() &&
           "DiagnoseEmptyLookup returned false but added no results");

    // If we found an Objective-C instance variable, let
    // LookupInObjCMethod build the appropriate expression to
    // reference the ivar.
    if (ObjCIvarDecl *Ivar = R.getAsSingle<ObjCIvarDecl>()) {
      R.clear();
      ExprResult E(ObjC().LookupInObjCMethod(R, S, Ivar->getIdentifier()));
      // In a hopelessly buggy code, Objective-C instance variable
      // lookup fails and no expression will be built to reference it.
      if (!E.isInvalid() && !E.get())
        return ExprError();
      return E;
    }
  }

  // This is guaranteed from this point on.
  assert(!R.empty() || ADL);

  // Check whether this might be a C++ implicit instance member access.
  // C++ [class.mfct.non-static]p3:
  //   When an id-expression that is not part of a class member access
  //   syntax and not used to form a pointer to member is used in the
  //   body of a non-static member function of class X, if name lookup
  //   resolves the name in the id-expression to a non-static non-type
  //   member of some class C, the id-expression is transformed into a
  //   class member access expression using (*this) as the
  //   postfix-expression to the left of the . operator.
  //
  // But we don't actually need to do this for '&' operands if R
  // resolved to a function or overloaded function set, because the
  // expression is ill-formed if it actually works out to be a
  // non-static member function:
  //
  // C++ [expr.ref]p4:
  //   Otherwise, if E1.E2 refers to a non-static member function. . .
  //   [t]he expression can be used only as the left-hand operand of a
  //   member function call.
  //
  // There are other safeguards against such uses, but it's important
  // to get this right here so that we don't end up making a
  // spuriously dependent expression if we're inside a dependent
  // instance method.
  if (isPotentialImplicitMemberAccess(SS, R, IsAddressOfOperand))
    return BuildPossibleImplicitMemberExpr(SS, TemplateKWLoc, R, TemplateArgs,
                                           S);

  if (TemplateArgs || TemplateKWLoc.isValid()) {

    // In C++1y, if this is a variable template id, then check it
    // in BuildTemplateIdExpr().
    // The single lookup result must be a variable template declaration.
    if (Id.getKind() == UnqualifiedIdKind::IK_TemplateId && Id.TemplateId &&
        Id.TemplateId->Kind == TNK_Var_template) {
      assert(R.getAsSingle<VarTemplateDecl>() &&
             "There should only be one declaration found.");
    }

    return BuildTemplateIdExpr(SS, TemplateKWLoc, R, ADL, TemplateArgs);
  }

  return BuildDeclarationNameExpr(SS, R, ADL);
}

ExprResult Sema::BuildQualifiedDeclarationNameExpr(
    CXXScopeSpec &SS, const DeclarationNameInfo &NameInfo,
    bool IsAddressOfOperand, TypeSourceInfo **RecoveryTSI) {
  LookupResult R(*this, NameInfo, LookupOrdinaryName);
  LookupParsedName(R, /*S=*/nullptr, &SS, /*ObjectType=*/QualType());

  if (R.isAmbiguous())
    return ExprError();

  if (R.wasNotFoundInCurrentInstantiation() || SS.isInvalid())
    return BuildDependentDeclRefExpr(SS, /*TemplateKWLoc=*/SourceLocation(),
                                     NameInfo, /*TemplateArgs=*/nullptr);

  if (R.empty()) {
    // Don't diagnose problems with invalid record decl, the secondary no_member
    // diagnostic during template instantiation is likely bogus, e.g. if a class
    // is invalid because it's derived from an invalid base class, then missing
    // members were likely supposed to be inherited.
    DeclContext *DC = computeDeclContext(SS);
    if (const auto *CD = dyn_cast<CXXRecordDecl>(DC))
      if (CD->isInvalidDecl())
        return ExprError();
    Diag(NameInfo.getLoc(), diag::err_no_member)
      << NameInfo.getName() << DC << SS.getRange();
    return ExprError();
  }

  if (const TypeDecl *TD = R.getAsSingle<TypeDecl>()) {
    // Diagnose a missing typename if this resolved unambiguously to a type in
    // a dependent context.  If we can recover with a type, downgrade this to
    // a warning in Microsoft compatibility mode.
    unsigned DiagID = diag::err_typename_missing;
    if (RecoveryTSI && getLangOpts().MSVCCompat)
      DiagID = diag::ext_typename_missing;
    SourceLocation Loc = SS.getBeginLoc();
    auto D = Diag(Loc, DiagID);
    D << SS.getScopeRep() << NameInfo.getName().getAsString()
      << SourceRange(Loc, NameInfo.getEndLoc());

    // Don't recover if the caller isn't expecting us to or if we're in a SFINAE
    // context.
    if (!RecoveryTSI)
      return ExprError();

    // Only issue the fixit if we're prepared to recover.
    D << FixItHint::CreateInsertion(Loc, "typename ");

    // Recover by pretending this was an elaborated type.
    QualType Ty = Context.getTypeDeclType(TD);
    TypeLocBuilder TLB;
    TLB.pushTypeSpec(Ty).setNameLoc(NameInfo.getLoc());

    QualType ET = getElaboratedType(ElaboratedTypeKeyword::None, SS, Ty);
    ElaboratedTypeLoc QTL = TLB.push<ElaboratedTypeLoc>(ET);
    QTL.setElaboratedKeywordLoc(SourceLocation());
    QTL.setQualifierLoc(SS.getWithLocInContext(Context));

    *RecoveryTSI = TLB.getTypeSourceInfo(Context, ET);

    return ExprEmpty();
  }

  // If necessary, build an implicit class member access.
  if (isPotentialImplicitMemberAccess(SS, R, IsAddressOfOperand))
    return BuildPossibleImplicitMemberExpr(SS,
                                           /*TemplateKWLoc=*/SourceLocation(),
                                           R, /*TemplateArgs=*/nullptr,
                                           /*S=*/nullptr);

  return BuildDeclarationNameExpr(SS, R, /*ADL=*/false);
}

ExprResult
Sema::PerformObjectMemberConversion(Expr *From,
                                    NestedNameSpecifier *Qualifier,
                                    NamedDecl *FoundDecl,
                                    NamedDecl *Member) {
  const auto *RD = dyn_cast<CXXRecordDecl>(Member->getDeclContext());
  if (!RD)
    return From;

  QualType DestRecordType;
  QualType DestType;
  QualType FromRecordType;
  QualType FromType = From->getType();
  bool PointerConversions = false;
  if (isa<FieldDecl>(Member)) {
    DestRecordType = Context.getCanonicalType(Context.getTypeDeclType(RD));
    auto FromPtrType = FromType->getAs<PointerType>();
    DestRecordType = Context.getAddrSpaceQualType(
        DestRecordType, FromPtrType
                            ? FromType->getPointeeType().getAddressSpace()
                            : FromType.getAddressSpace());

    if (FromPtrType) {
      DestType = Context.getPointerType(DestRecordType);
      FromRecordType = FromPtrType->getPointeeType();
      PointerConversions = true;
    } else {
      DestType = DestRecordType;
      FromRecordType = FromType;
    }
  } else if (const auto *Method = dyn_cast<CXXMethodDecl>(Member)) {
    if (!Method->isImplicitObjectMemberFunction())
      return From;

    DestType = Method->getThisType().getNonReferenceType();
    DestRecordType = Method->getFunctionObjectParameterType();

    if (FromType->getAs<PointerType>()) {
      FromRecordType = FromType->getPointeeType();
      PointerConversions = true;
    } else {
      FromRecordType = FromType;
      DestType = DestRecordType;
    }

    LangAS FromAS = FromRecordType.getAddressSpace();
    LangAS DestAS = DestRecordType.getAddressSpace();
    if (FromAS != DestAS) {
      QualType FromRecordTypeWithoutAS =
          Context.removeAddrSpaceQualType(FromRecordType);
      QualType FromTypeWithDestAS =
          Context.getAddrSpaceQualType(FromRecordTypeWithoutAS, DestAS);
      if (PointerConversions)
        FromTypeWithDestAS = Context.getPointerType(FromTypeWithDestAS);
      From = ImpCastExprToType(From, FromTypeWithDestAS,
                               CK_AddressSpaceConversion, From->getValueKind())
                 .get();
    }
  } else {
    // No conversion necessary.
    return From;
  }

  if (DestType->isDependentType() || FromType->isDependentType())
    return From;

  // If the unqualified types are the same, no conversion is necessary.
  if (Context.hasSameUnqualifiedType(FromRecordType, DestRecordType))
    return From;

  SourceRange FromRange = From->getSourceRange();
  SourceLocation FromLoc = FromRange.getBegin();

  ExprValueKind VK = From->getValueKind();

  // C++ [class.member.lookup]p8:
  //   [...] Ambiguities can often be resolved by qualifying a name with its
  //   class name.
  //
  // If the member was a qualified name and the qualified referred to a
  // specific base subobject type, we'll cast to that intermediate type
  // first and then to the object in which the member is declared. That allows
  // one to resolve ambiguities in, e.g., a diamond-shaped hierarchy such as:
  //
  //   class Base { public: int x; };
  //   class Derived1 : public Base { };
  //   class Derived2 : public Base { };
  //   class VeryDerived : public Derived1, public Derived2 { void f(); };
  //
  //   void VeryDerived::f() {
  //     x = 17; // error: ambiguous base subobjects
  //     Derived1::x = 17; // okay, pick the Base subobject of Derived1
  //   }
  if (Qualifier && Qualifier->getAsType()) {
    QualType QType = QualType(Qualifier->getAsType(), 0);
    assert(QType->isRecordType() && "lookup done with non-record type");

    QualType QRecordType = QualType(QType->castAs<RecordType>(), 0);

    // In C++98, the qualifier type doesn't actually have to be a base
    // type of the object type, in which case we just ignore it.
    // Otherwise build the appropriate casts.
    if (IsDerivedFrom(FromLoc, FromRecordType, QRecordType)) {
      CXXCastPath BasePath;
      if (CheckDerivedToBaseConversion(FromRecordType, QRecordType,
                                       FromLoc, FromRange, &BasePath))
        return ExprError();

      if (PointerConversions)
        QType = Context.getPointerType(QType);
      From = ImpCastExprToType(From, QType, CK_UncheckedDerivedToBase,
                               VK, &BasePath).get();

      FromType = QType;
      FromRecordType = QRecordType;

      // If the qualifier type was the same as the destination type,
      // we're done.
      if (Context.hasSameUnqualifiedType(FromRecordType, DestRecordType))
        return From;
    }
  }

  CXXCastPath BasePath;
  if (CheckDerivedToBaseConversion(FromRecordType, DestRecordType,
                                   FromLoc, FromRange, &BasePath,
                                   /*IgnoreAccess=*/true))
    return ExprError();

  return ImpCastExprToType(From, DestType, CK_UncheckedDerivedToBase,
                           VK, &BasePath);
}

bool Sema::UseArgumentDependentLookup(const CXXScopeSpec &SS,
                                      const LookupResult &R,
                                      bool HasTrailingLParen) {
  // Only when used directly as the postfix-expression of a call.
  if (!HasTrailingLParen)
    return false;

  // Never if a scope specifier was provided.
  if (SS.isNotEmpty())
    return false;

  // Only in C++ or ObjC++.
  if (!getLangOpts().CPlusPlus)
    return false;

  // Turn off ADL when we find certain kinds of declarations during
  // normal lookup:
  for (const NamedDecl *D : R) {
    // C++0x [basic.lookup.argdep]p3:
    //     -- a declaration of a class member
    // Since using decls preserve this property, we check this on the
    // original decl.
    if (D->isCXXClassMember())
      return false;

    // C++0x [basic.lookup.argdep]p3:
    //     -- a block-scope function declaration that is not a
    //        using-declaration
    // NOTE: we also trigger this for function templates (in fact, we
    // don't check the decl type at all, since all other decl types
    // turn off ADL anyway).
    if (isa<UsingShadowDecl>(D))
      D = cast<UsingShadowDecl>(D)->getTargetDecl();
    else if (D->getLexicalDeclContext()->isFunctionOrMethod())
      return false;

    // C++0x [basic.lookup.argdep]p3:
    //     -- a declaration that is neither a function or a function
    //        template
    // And also for builtin functions.
    if (const auto *FDecl = dyn_cast<FunctionDecl>(D)) {
      // But also builtin functions.
      if (FDecl->getBuiltinID() && FDecl->isImplicit())
        return false;
    } else if (!isa<FunctionTemplateDecl>(D))
      return false;
  }

  return true;
}


/// Diagnoses obvious problems with the use of the given declaration
/// as an expression.  This is only actually called for lookups that
/// were not overloaded, and it doesn't promise that the declaration
/// will in fact be used.
static bool CheckDeclInExpr(Sema &S, SourceLocation Loc, NamedDecl *D,
                            bool AcceptInvalid) {
  if (D->isInvalidDecl() && !AcceptInvalid)
    return true;

  if (isa<TypedefNameDecl>(D)) {
    S.Diag(Loc, diag::err_unexpected_typedef) << D->getDeclName();
    return true;
  }

  if (isa<ObjCInterfaceDecl>(D)) {
    S.Diag(Loc, diag::err_unexpected_interface) << D->getDeclName();
    return true;
  }

  if (isa<NamespaceDecl>(D)) {
    S.Diag(Loc, diag::err_unexpected_namespace) << D->getDeclName();
    return true;
  }

  return false;
}

// Certain multiversion types should be treated as overloaded even when there is
// only one result.
static bool ShouldLookupResultBeMultiVersionOverload(const LookupResult &R) {
  assert(R.isSingleResult() && "Expected only a single result");
  const auto *FD = dyn_cast<FunctionDecl>(R.getFoundDecl());
  return FD &&
         (FD->isCPUDispatchMultiVersion() || FD->isCPUSpecificMultiVersion());
}

ExprResult Sema::BuildDeclarationNameExpr(const CXXScopeSpec &SS,
                                          LookupResult &R, bool NeedsADL,
                                          bool AcceptInvalidDecl) {
  // If this is a single, fully-resolved result and we don't need ADL,
  // just build an ordinary singleton decl ref.
  if (!NeedsADL && R.isSingleResult() &&
      !R.getAsSingle<FunctionTemplateDecl>() &&
      !ShouldLookupResultBeMultiVersionOverload(R))
    return BuildDeclarationNameExpr(SS, R.getLookupNameInfo(), R.getFoundDecl(),
                                    R.getRepresentativeDecl(), nullptr,
                                    AcceptInvalidDecl);

  // We only need to check the declaration if there's exactly one
  // result, because in the overloaded case the results can only be
  // functions and function templates.
  if (R.isSingleResult() && !ShouldLookupResultBeMultiVersionOverload(R) &&
      CheckDeclInExpr(*this, R.getNameLoc(), R.getFoundDecl(),
                      AcceptInvalidDecl))
    return ExprError();

  // Otherwise, just build an unresolved lookup expression.  Suppress
  // any lookup-related diagnostics; we'll hash these out later, when
  // we've picked a target.
  R.suppressDiagnostics();

  UnresolvedLookupExpr *ULE = UnresolvedLookupExpr::Create(
      Context, R.getNamingClass(), SS.getWithLocInContext(Context),
      R.getLookupNameInfo(), NeedsADL, R.begin(), R.end(),
      /*KnownDependent=*/false, /*KnownInstantiationDependent=*/false);

  return ULE;
}

static void diagnoseUncapturableValueReferenceOrBinding(Sema &S,
                                                        SourceLocation loc,
                                                        ValueDecl *var);

ExprResult Sema::BuildDeclarationNameExpr(
    const CXXScopeSpec &SS, const DeclarationNameInfo &NameInfo, NamedDecl *D,
    NamedDecl *FoundD, const TemplateArgumentListInfo *TemplateArgs,
    bool AcceptInvalidDecl) {
  assert(D && "Cannot refer to a NULL declaration");
  assert(!isa<FunctionTemplateDecl>(D) &&
         "Cannot refer unambiguously to a function template");

  SourceLocation Loc = NameInfo.getLoc();
  if (CheckDeclInExpr(*this, Loc, D, AcceptInvalidDecl)) {
    // Recovery from invalid cases (e.g. D is an invalid Decl).
    // We use the dependent type for the RecoveryExpr to prevent bogus follow-up
    // diagnostics, as invalid decls use int as a fallback type.
    return CreateRecoveryExpr(NameInfo.getBeginLoc(), NameInfo.getEndLoc(), {});
  }

  if (TemplateDecl *TD = dyn_cast<TemplateDecl>(D)) {
    // Specifically diagnose references to class templates that are missing
    // a template argument list.
    diagnoseMissingTemplateArguments(SS, /*TemplateKeyword=*/false, TD, Loc);
    return ExprError();
  }

  // Make sure that we're referring to a value.
  if (!isa<ValueDecl, UnresolvedUsingIfExistsDecl>(D)) {
    Diag(Loc, diag::err_ref_non_value) << D << SS.getRange();
    Diag(D->getLocation(), diag::note_declared_at);
    return ExprError();
  }

  // Check whether this declaration can be used. Note that we suppress
  // this check when we're going to perform argument-dependent lookup
  // on this function name, because this might not be the function
  // that overload resolution actually selects.
  if (DiagnoseUseOfDecl(D, Loc))
    return ExprError();

  auto *VD = cast<ValueDecl>(D);

  // Only create DeclRefExpr's for valid Decl's.
  if (VD->isInvalidDecl() && !AcceptInvalidDecl)
    return ExprError();

  // Handle members of anonymous structs and unions.  If we got here,
  // and the reference is to a class member indirect field, then this
  // must be the subject of a pointer-to-member expression.
  if (auto *IndirectField = dyn_cast<IndirectFieldDecl>(VD);
      IndirectField && !IndirectField->isCXXClassMember())
    return BuildAnonymousStructUnionMemberReference(SS, NameInfo.getLoc(),
                                                    IndirectField);

  QualType type = VD->getType();
  if (type.isNull())
    return ExprError();
  ExprValueKind valueKind = VK_PRValue;

  // In 'T ...V;', the type of the declaration 'V' is 'T...', but the type of
  // a reference to 'V' is simply (unexpanded) 'T'. The type, like the value,
  // is expanded by some outer '...' in the context of the use.
  type = type.getNonPackExpansionType();

  switch (D->getKind()) {
    // Ignore all the non-ValueDecl kinds.
#define ABSTRACT_DECL(kind)
#define VALUE(type, base)
#define DECL(type, base) case Decl::type:
#include "clang/AST/DeclNodes.inc"
    llvm_unreachable("invalid value decl kind");

  // These shouldn't make it here.
  case Decl::ObjCAtDefsField:
    llvm_unreachable("forming non-member reference to ivar?");

  // Enum constants are always r-values and never references.
  // Unresolved using declarations are dependent.
  case Decl::EnumConstant:
  case Decl::UnresolvedUsingValue:
  case Decl::OMPDeclareReduction:
  case Decl::OMPDeclareMapper:
    valueKind = VK_PRValue;
    break;

  // Fields and indirect fields that got here must be for
  // pointer-to-member expressions; we just call them l-values for
  // internal consistency, because this subexpression doesn't really
  // exist in the high-level semantics.
  case Decl::Field:
  case Decl::IndirectField:
  case Decl::ObjCIvar:
    assert((getLangOpts().CPlusPlus || isAttrContext()) &&
           "building reference to field in C?");

    // These can't have reference type in well-formed programs, but
    // for internal consistency we do this anyway.
    type = type.getNonReferenceType();
    valueKind = VK_LValue;
    break;

  // Non-type template parameters are either l-values or r-values
  // depending on the type.
  case Decl::NonTypeTemplateParm: {
    if (const ReferenceType *reftype = type->getAs<ReferenceType>()) {
      type = reftype->getPointeeType();
      valueKind = VK_LValue; // even if the parameter is an r-value reference
      break;
    }

    // [expr.prim.id.unqual]p2:
    //   If the entity is a template parameter object for a template
    //   parameter of type T, the type of the expression is const T.
    //   [...] The expression is an lvalue if the entity is a [...] template
    //   parameter object.
    if (type->isRecordType()) {
      type = type.getUnqualifiedType().withConst();
      valueKind = VK_LValue;
      break;
    }

    // For non-references, we need to strip qualifiers just in case
    // the template parameter was declared as 'const int' or whatever.
    valueKind = VK_PRValue;
    type = type.getUnqualifiedType();
    break;
  }

  case Decl::Var:
  case Decl::VarTemplateSpecialization:
  case Decl::VarTemplatePartialSpecialization:
  case Decl::Decomposition:
  case Decl::OMPCapturedExpr:
    // In C, "extern void blah;" is valid and is an r-value.
    if (!getLangOpts().CPlusPlus && !type.hasQualifiers() &&
        type->isVoidType()) {
      valueKind = VK_PRValue;
      break;
    }
    [[fallthrough]];

  case Decl::ImplicitParam:
  case Decl::ParmVar: {
    // These are always l-values.
    valueKind = VK_LValue;
    type = type.getNonReferenceType();

    // FIXME: Does the addition of const really only apply in
    // potentially-evaluated contexts? Since the variable isn't actually
    // captured in an unevaluated context, it seems that the answer is no.
    if (!isUnevaluatedContext()) {
      QualType CapturedType = getCapturedDeclRefType(cast<VarDecl>(VD), Loc);
      if (!CapturedType.isNull())
        type = CapturedType;
    }

    break;
  }

  case Decl::Binding:
    // These are always lvalues.
    valueKind = VK_LValue;
    type = type.getNonReferenceType();
    break;

  case Decl::Function: {
    if (unsigned BID = cast<FunctionDecl>(VD)->getBuiltinID()) {
      if (!Context.BuiltinInfo.isDirectlyAddressable(BID)) {
        type = Context.BuiltinFnTy;
        valueKind = VK_PRValue;
        break;
      }
    }

    const FunctionType *fty = type->castAs<FunctionType>();

    // If we're referring to a function with an __unknown_anytype
    // result type, make the entire expression __unknown_anytype.
    if (fty->getReturnType() == Context.UnknownAnyTy) {
      type = Context.UnknownAnyTy;
      valueKind = VK_PRValue;
      break;
    }

    // Functions are l-values in C++.
    if (getLangOpts().CPlusPlus) {
      valueKind = VK_LValue;
      break;
    }

    // C99 DR 316 says that, if a function type comes from a
    // function definition (without a prototype), that type is only
    // used for checking compatibility. Therefore, when referencing
    // the function, we pretend that we don't have the full function
    // type.
    if (!cast<FunctionDecl>(VD)->hasPrototype() && isa<FunctionProtoType>(fty))
      type = Context.getFunctionNoProtoType(fty->getReturnType(),
                                            fty->getExtInfo());

    // Functions are r-values in C.
    valueKind = VK_PRValue;
    break;
  }

  case Decl::CXXDeductionGuide:
    llvm_unreachable("building reference to deduction guide");

  case Decl::MSProperty:
  case Decl::MSGuid:
  case Decl::TemplateParamObject:
    // FIXME: Should MSGuidDecl and template parameter objects be subject to
    // capture in OpenMP, or duplicated between host and device?
    valueKind = VK_LValue;
    break;

  case Decl::UnnamedGlobalConstant:
    valueKind = VK_LValue;
    break;

  case Decl::CXXMethod:
    // If we're referring to a method with an __unknown_anytype
    // result type, make the entire expression __unknown_anytype.
    // This should only be possible with a type written directly.
    if (const FunctionProtoType *proto =
            dyn_cast<FunctionProtoType>(VD->getType()))
      if (proto->getReturnType() == Context.UnknownAnyTy) {
        type = Context.UnknownAnyTy;
        valueKind = VK_PRValue;
        break;
      }

    // C++ methods are l-values if static, r-values if non-static.
    if (cast<CXXMethodDecl>(VD)->isStatic()) {
      valueKind = VK_LValue;
      break;
    }
    [[fallthrough]];

  case Decl::CXXConversion:
  case Decl::CXXDestructor:
  case Decl::CXXConstructor:
    valueKind = VK_PRValue;
    break;
  }

  auto *E =
      BuildDeclRefExpr(VD, type, valueKind, NameInfo, &SS, FoundD,
                       /*FIXME: TemplateKWLoc*/ SourceLocation(), TemplateArgs);
  // Clang AST consumers assume a DeclRefExpr refers to a valid decl. We
  // wrap a DeclRefExpr referring to an invalid decl with a dependent-type
  // RecoveryExpr to avoid follow-up semantic analysis (thus prevent bogus
  // diagnostics).
  if (VD->isInvalidDecl() && E)
    return CreateRecoveryExpr(E->getBeginLoc(), E->getEndLoc(), {E});
  return E;
}

static void ConvertUTF8ToWideString(unsigned CharByteWidth, StringRef Source,
                                    SmallString<32> &Target) {
  Target.resize(CharByteWidth * (Source.size() + 1));
  char *ResultPtr = &Target[0];
  const llvm::UTF8 *ErrorPtr;
  bool success =
      llvm::ConvertUTF8toWide(CharByteWidth, Source, ResultPtr, ErrorPtr);
  (void)success;
  assert(success);
  Target.resize(ResultPtr - &Target[0]);
}

ExprResult Sema::BuildPredefinedExpr(SourceLocation Loc,
                                     PredefinedIdentKind IK) {
  Decl *currentDecl = getPredefinedExprDecl(CurContext);
  if (!currentDecl) {
    Diag(Loc, diag::ext_predef_outside_function);
    currentDecl = Context.getTranslationUnitDecl();
  }

  QualType ResTy;
  StringLiteral *SL = nullptr;
  if (cast<DeclContext>(currentDecl)->isDependentContext())
    ResTy = Context.DependentTy;
  else {
    // Pre-defined identifiers are of type char[x], where x is the length of
    // the string.
    bool ForceElaboratedPrinting =
        IK == PredefinedIdentKind::Function && getLangOpts().MSVCCompat;
    auto Str =
        PredefinedExpr::ComputeName(IK, currentDecl, ForceElaboratedPrinting);
    unsigned Length = Str.length();

    llvm::APInt LengthI(32, Length + 1);
    if (IK == PredefinedIdentKind::LFunction ||
        IK == PredefinedIdentKind::LFuncSig) {
      ResTy =
          Context.adjustStringLiteralBaseType(Context.WideCharTy.withConst());
      SmallString<32> RawChars;
      ConvertUTF8ToWideString(Context.getTypeSizeInChars(ResTy).getQuantity(),
                              Str, RawChars);
      ResTy = Context.getConstantArrayType(ResTy, LengthI, nullptr,
                                           ArraySizeModifier::Normal,
                                           /*IndexTypeQuals*/ 0);
      SL = StringLiteral::Create(Context, RawChars, StringLiteralKind::Wide,
                                 /*Pascal*/ false, ResTy, Loc);
    } else {
      ResTy = Context.adjustStringLiteralBaseType(Context.CharTy.withConst());
      ResTy = Context.getConstantArrayType(ResTy, LengthI, nullptr,
                                           ArraySizeModifier::Normal,
                                           /*IndexTypeQuals*/ 0);
      SL = StringLiteral::Create(Context, Str, StringLiteralKind::Ordinary,
                                 /*Pascal*/ false, ResTy, Loc);
    }
  }

  return PredefinedExpr::Create(Context, Loc, ResTy, IK, LangOpts.MicrosoftExt,
                                SL);
}

ExprResult Sema::ActOnPredefinedExpr(SourceLocation Loc, tok::TokenKind Kind) {
  return BuildPredefinedExpr(Loc, getPredefinedExprKind(Kind));
}

ExprResult Sema::ActOnCharacterConstant(const Token &Tok, Scope *UDLScope) {
  SmallString<16> CharBuffer;
  bool Invalid = false;
  StringRef ThisTok = PP.getSpelling(Tok, CharBuffer, &Invalid);
  if (Invalid)
    return ExprError();

  CharLiteralParser Literal(ThisTok.begin(), ThisTok.end(), Tok.getLocation(),
                            PP, Tok.getKind());
  if (Literal.hadError())
    return ExprError();

  QualType Ty;
  if (Literal.isWide())
    Ty = Context.WideCharTy; // L'x' -> wchar_t in C and C++.
  else if (Literal.isUTF8() && getLangOpts().C23)
    Ty = Context.UnsignedCharTy; // u8'x' -> unsigned char in C23
  else if (Literal.isUTF8() && getLangOpts().Char8)
    Ty = Context.Char8Ty; // u8'x' -> char8_t when it exists.
  else if (Literal.isUTF16())
    Ty = Context.Char16Ty; // u'x' -> char16_t in C11 and C++11.
  else if (Literal.isUTF32())
    Ty = Context.Char32Ty; // U'x' -> char32_t in C11 and C++11.
  else if (!getLangOpts().CPlusPlus || Literal.isMultiChar())
    Ty = Context.IntTy;   // 'x' -> int in C, 'wxyz' -> int in C++.
  else
    Ty = Context.CharTy; // 'x' -> char in C++;
                         // u8'x' -> char in C11-C17 and in C++ without char8_t.

  CharacterLiteralKind Kind = CharacterLiteralKind::Ascii;
  if (Literal.isWide())
    Kind = CharacterLiteralKind::Wide;
  else if (Literal.isUTF16())
    Kind = CharacterLiteralKind::UTF16;
  else if (Literal.isUTF32())
    Kind = CharacterLiteralKind::UTF32;
  else if (Literal.isUTF8())
    Kind = CharacterLiteralKind::UTF8;

  Expr *Lit = new (Context) CharacterLiteral(Literal.getValue(), Kind, Ty,
                                             Tok.getLocation());

  if (Literal.getUDSuffix().empty())
    return Lit;

  // We're building a user-defined literal.
  IdentifierInfo *UDSuffix = &Context.Idents.get(Literal.getUDSuffix());
  SourceLocation UDSuffixLoc =
    getUDSuffixLoc(*this, Tok.getLocation(), Literal.getUDSuffixOffset());

  // Make sure we're allowed user-defined literals here.
  if (!UDLScope)
    return ExprError(Diag(UDSuffixLoc, diag::err_invalid_character_udl));

  // C++11 [lex.ext]p6: The literal L is treated as a call of the form
  //   operator "" X (ch)
  return BuildCookedLiteralOperatorCall(*this, UDLScope, UDSuffix, UDSuffixLoc,
                                        Lit, Tok.getLocation());
}

ExprResult Sema::ActOnIntegerConstant(SourceLocation Loc, uint64_t Val) {
  unsigned IntSize = Context.getTargetInfo().getIntWidth();
  return IntegerLiteral::Create(Context, llvm::APInt(IntSize, Val),
                                Context.IntTy, Loc);
}

static Expr *BuildFloatingLiteral(Sema &S, NumericLiteralParser &Literal,
                                  QualType Ty, SourceLocation Loc) {
  const llvm::fltSemantics &Format = S.Context.getFloatTypeSemantics(Ty);

  using llvm::APFloat;
  APFloat Val(Format);

  llvm::RoundingMode RM = S.CurFPFeatures.getRoundingMode();
  if (RM == llvm::RoundingMode::Dynamic)
    RM = llvm::RoundingMode::NearestTiesToEven;
  APFloat::opStatus result = Literal.GetFloatValue(Val, RM);

  // Overflow is always an error, but underflow is only an error if
  // we underflowed to zero (APFloat reports denormals as underflow).
  if ((result & APFloat::opOverflow) ||
      ((result & APFloat::opUnderflow) && Val.isZero())) {
    unsigned diagnostic;
    SmallString<20> buffer;
    if (result & APFloat::opOverflow) {
      diagnostic = diag::warn_float_overflow;
      APFloat::getLargest(Format).toString(buffer);
    } else {
      diagnostic = diag::warn_float_underflow;
      APFloat::getSmallest(Format).toString(buffer);
    }

    S.Diag(Loc, diagnostic) << Ty << buffer.str();
  }

  bool isExact = (result == APFloat::opOK);
  return FloatingLiteral::Create(S.Context, Val, isExact, Ty, Loc);
}

bool Sema::CheckLoopHintExpr(Expr *E, SourceLocation Loc, bool AllowZero) {
  assert(E && "Invalid expression");

  if (E->isValueDependent())
    return false;

  QualType QT = E->getType();
  if (!QT->isIntegerType() || QT->isBooleanType() || QT->isCharType()) {
    Diag(E->getExprLoc(), diag::err_pragma_loop_invalid_argument_type) << QT;
    return true;
  }

  llvm::APSInt ValueAPS;
  ExprResult R = VerifyIntegerConstantExpression(E, &ValueAPS);

  if (R.isInvalid())
    return true;

  // GCC allows the value of unroll count to be 0.
  // https://gcc.gnu.org/onlinedocs/gcc/Loop-Specific-Pragmas.html says
  // "The values of 0 and 1 block any unrolling of the loop."
  // The values doesn't have to be strictly positive in '#pragma GCC unroll' and
  // '#pragma unroll' cases.
  bool ValueIsPositive =
      AllowZero ? ValueAPS.isNonNegative() : ValueAPS.isStrictlyPositive();
  if (!ValueIsPositive || ValueAPS.getActiveBits() > 31) {
    Diag(E->getExprLoc(), diag::err_requires_positive_value)
        << toString(ValueAPS, 10) << ValueIsPositive;
    return true;
  }

  return false;
}

ExprResult Sema::ActOnNumericConstant(const Token &Tok, Scope *UDLScope) {
  // Fast path for a single digit (which is quite common).  A single digit
  // cannot have a trigraph, escaped newline, radix prefix, or suffix.
  if (Tok.getLength() == 1 || Tok.getKind() == tok::binary_data) {
    const char Val = PP.getSpellingOfSingleCharacterNumericConstant(Tok);
    return ActOnIntegerConstant(Tok.getLocation(), Val);
  }

  SmallString<128> SpellingBuffer;
  // NumericLiteralParser wants to overread by one character.  Add padding to
  // the buffer in case the token is copied to the buffer.  If getSpelling()
  // returns a StringRef to the memory buffer, it should have a null char at
  // the EOF, so it is also safe.
  SpellingBuffer.resize(Tok.getLength() + 1);

  // Get the spelling of the token, which eliminates trigraphs, etc.
  bool Invalid = false;
  StringRef TokSpelling = PP.getSpelling(Tok, SpellingBuffer, &Invalid);
  if (Invalid)
    return ExprError();

  NumericLiteralParser Literal(TokSpelling, Tok.getLocation(),
                               PP.getSourceManager(), PP.getLangOpts(),
                               PP.getTargetInfo(), PP.getDiagnostics());
  if (Literal.hadError)
    return ExprError();

  if (Literal.hasUDSuffix()) {
    // We're building a user-defined literal.
    const IdentifierInfo *UDSuffix = &Context.Idents.get(Literal.getUDSuffix());
    SourceLocation UDSuffixLoc =
      getUDSuffixLoc(*this, Tok.getLocation(), Literal.getUDSuffixOffset());

    // Make sure we're allowed user-defined literals here.
    if (!UDLScope)
      return ExprError(Diag(UDSuffixLoc, diag::err_invalid_numeric_udl));

    QualType CookedTy;
    if (Literal.isFloatingLiteral()) {
      // C++11 [lex.ext]p4: If S contains a literal operator with parameter type
      // long double, the literal is treated as a call of the form
      //   operator "" X (f L)
      CookedTy = Context.LongDoubleTy;
    } else {
      // C++11 [lex.ext]p3: If S contains a literal operator with parameter type
      // unsigned long long, the literal is treated as a call of the form
      //   operator "" X (n ULL)
      CookedTy = Context.UnsignedLongLongTy;
    }

    DeclarationName OpName =
      Context.DeclarationNames.getCXXLiteralOperatorName(UDSuffix);
    DeclarationNameInfo OpNameInfo(OpName, UDSuffixLoc);
    OpNameInfo.setCXXLiteralOperatorNameLoc(UDSuffixLoc);

    SourceLocation TokLoc = Tok.getLocation();

    // Perform literal operator lookup to determine if we're building a raw
    // literal or a cooked one.
    LookupResult R(*this, OpName, UDSuffixLoc, LookupOrdinaryName);
    switch (LookupLiteralOperator(UDLScope, R, CookedTy,
                                  /*AllowRaw*/ true, /*AllowTemplate*/ true,
                                  /*AllowStringTemplatePack*/ false,
                                  /*DiagnoseMissing*/ !Literal.isImaginary)) {
    case LOLR_ErrorNoDiagnostic:
      // Lookup failure for imaginary constants isn't fatal, there's still the
      // GNU extension producing _Complex types.
      break;
    case LOLR_Error:
      return ExprError();
    case LOLR_Cooked: {
      Expr *Lit;
      if (Literal.isFloatingLiteral()) {
        Lit = BuildFloatingLiteral(*this, Literal, CookedTy, Tok.getLocation());
      } else {
        llvm::APInt ResultVal(Context.getTargetInfo().getLongLongWidth(), 0);
        if (Literal.GetIntegerValue(ResultVal))
          Diag(Tok.getLocation(), diag::err_integer_literal_too_large)
              << /* Unsigned */ 1;
        Lit = IntegerLiteral::Create(Context, ResultVal, CookedTy,
                                     Tok.getLocation());
      }
      return BuildLiteralOperatorCall(R, OpNameInfo, Lit, TokLoc);
    }

    case LOLR_Raw: {
      // C++11 [lit.ext]p3, p4: If S contains a raw literal operator, the
      // literal is treated as a call of the form
      //   operator "" X ("n")
      unsigned Length = Literal.getUDSuffixOffset();
      QualType StrTy = Context.getConstantArrayType(
          Context.adjustStringLiteralBaseType(Context.CharTy.withConst()),
          llvm::APInt(32, Length + 1), nullptr, ArraySizeModifier::Normal, 0);
      Expr *Lit =
          StringLiteral::Create(Context, StringRef(TokSpelling.data(), Length),
                                StringLiteralKind::Ordinary,
                                /*Pascal*/ false, StrTy, &TokLoc, 1);
      return BuildLiteralOperatorCall(R, OpNameInfo, Lit, TokLoc);
    }

    case LOLR_Template: {
      // C++11 [lit.ext]p3, p4: Otherwise (S contains a literal operator
      // template), L is treated as a call fo the form
      //   operator "" X <'c1', 'c2', ... 'ck'>()
      // where n is the source character sequence c1 c2 ... ck.
      TemplateArgumentListInfo ExplicitArgs;
      unsigned CharBits = Context.getIntWidth(Context.CharTy);
      bool CharIsUnsigned = Context.CharTy->isUnsignedIntegerType();
      llvm::APSInt Value(CharBits, CharIsUnsigned);
      for (unsigned I = 0, N = Literal.getUDSuffixOffset(); I != N; ++I) {
        Value = TokSpelling[I];
        TemplateArgument Arg(Context, Value, Context.CharTy);
        TemplateArgumentLocInfo ArgInfo;
        ExplicitArgs.addArgument(TemplateArgumentLoc(Arg, ArgInfo));
      }
      return BuildLiteralOperatorCall(R, OpNameInfo, std::nullopt, TokLoc,
                                      &ExplicitArgs);
    }
    case LOLR_StringTemplatePack:
      llvm_unreachable("unexpected literal operator lookup result");
    }
  }

  Expr *Res;

  if (Literal.isFixedPointLiteral()) {
    QualType Ty;

    if (Literal.isAccum) {
      if (Literal.isHalf) {
        Ty = Context.ShortAccumTy;
      } else if (Literal.isLong) {
        Ty = Context.LongAccumTy;
      } else {
        Ty = Context.AccumTy;
      }
    } else if (Literal.isFract) {
      if (Literal.isHalf) {
        Ty = Context.ShortFractTy;
      } else if (Literal.isLong) {
        Ty = Context.LongFractTy;
      } else {
        Ty = Context.FractTy;
      }
    }

    if (Literal.isUnsigned) Ty = Context.getCorrespondingUnsignedType(Ty);

    bool isSigned = !Literal.isUnsigned;
    unsigned scale = Context.getFixedPointScale(Ty);
    unsigned bit_width = Context.getTypeInfo(Ty).Width;

    llvm::APInt Val(bit_width, 0, isSigned);
    bool Overflowed = Literal.GetFixedPointValue(Val, scale);
    bool ValIsZero = Val.isZero() && !Overflowed;

    auto MaxVal = Context.getFixedPointMax(Ty).getValue();
    if (Literal.isFract && Val == MaxVal + 1 && !ValIsZero)
      // Clause 6.4.4 - The value of a constant shall be in the range of
      // representable values for its type, with exception for constants of a
      // fract type with a value of exactly 1; such a constant shall denote
      // the maximal value for the type.
      --Val;
    else if (Val.ugt(MaxVal) || Overflowed)
      Diag(Tok.getLocation(), diag::err_too_large_for_fixed_point);

    Res = FixedPointLiteral::CreateFromRawInt(Context, Val, Ty,
                                              Tok.getLocation(), scale);
  } else if (Literal.isFloatingLiteral()) {
    QualType Ty;
    if (Literal.isHalf){
      if (getLangOpts().HLSL ||
          getOpenCLOptions().isAvailableOption("cl_khr_fp16", getLangOpts()))
        Ty = Context.HalfTy;
      else {
        Diag(Tok.getLocation(), diag::err_half_const_requires_fp16);
        return ExprError();
      }
    } else if (Literal.isFloat)
      Ty = Context.FloatTy;
    else if (Literal.isLong)
      Ty = !getLangOpts().HLSL ? Context.LongDoubleTy : Context.DoubleTy;
    else if (Literal.isFloat16)
      Ty = Context.Float16Ty;
    else if (Literal.isFloat128)
      Ty = Context.Float128Ty;
    else if (getLangOpts().HLSL)
      Ty = Context.FloatTy;
    else
      Ty = Context.DoubleTy;

    Res = BuildFloatingLiteral(*this, Literal, Ty, Tok.getLocation());

    if (Ty == Context.DoubleTy) {
      if (getLangOpts().SinglePrecisionConstants) {
        if (Ty->castAs<BuiltinType>()->getKind() != BuiltinType::Float) {
          Res = ImpCastExprToType(Res, Context.FloatTy, CK_FloatingCast).get();
        }
      } else if (getLangOpts().OpenCL && !getOpenCLOptions().isAvailableOption(
                                             "cl_khr_fp64", getLangOpts())) {
        // Impose single-precision float type when cl_khr_fp64 is not enabled.
        Diag(Tok.getLocation(), diag::warn_double_const_requires_fp64)
            << (getLangOpts().getOpenCLCompatibleVersion() >= 300);
        Res = ImpCastExprToType(Res, Context.FloatTy, CK_FloatingCast).get();
      }
    }
  } else if (!Literal.isIntegerLiteral()) {
    return ExprError();
  } else {
    QualType Ty;

    // 'z/uz' literals are a C++23 feature.
    if (Literal.isSizeT)
      Diag(Tok.getLocation(), getLangOpts().CPlusPlus
                                  ? getLangOpts().CPlusPlus23
                                        ? diag::warn_cxx20_compat_size_t_suffix
                                        : diag::ext_cxx23_size_t_suffix
                                  : diag::err_cxx23_size_t_suffix);

    // 'wb/uwb' literals are a C23 feature. We support _BitInt as a type in C++,
    // but we do not currently support the suffix in C++ mode because it's not
    // entirely clear whether WG21 will prefer this suffix to return a library
    // type such as std::bit_int instead of returning a _BitInt. '__wb/__uwb'
    // literals are a C++ extension.
    if (Literal.isBitInt)
      PP.Diag(Tok.getLocation(),
              getLangOpts().CPlusPlus ? diag::ext_cxx_bitint_suffix
              : getLangOpts().C23     ? diag::warn_c23_compat_bitint_suffix
                                      : diag::ext_c23_bitint_suffix);

    // Get the value in the widest-possible width. What is "widest" depends on
    // whether the literal is a bit-precise integer or not. For a bit-precise
    // integer type, try to scan the source to determine how many bits are
    // needed to represent the value. This may seem a bit expensive, but trying
    // to get the integer value from an overly-wide APInt is *extremely*
    // expensive, so the naive approach of assuming
    // llvm::IntegerType::MAX_INT_BITS is a big performance hit.
    unsigned BitsNeeded =
        Literal.isBitInt ? llvm::APInt::getSufficientBitsNeeded(
                               Literal.getLiteralDigits(), Literal.getRadix())
                         : Context.getTargetInfo().getIntMaxTWidth();
    llvm::APInt ResultVal(BitsNeeded, 0);

    if (Literal.GetIntegerValue(ResultVal)) {
      // If this value didn't fit into uintmax_t, error and force to ull.
      Diag(Tok.getLocation(), diag::err_integer_literal_too_large)
          << /* Unsigned */ 1;
      Ty = Context.UnsignedLongLongTy;
      assert(Context.getTypeSize(Ty) == ResultVal.getBitWidth() &&
             "long long is not intmax_t?");
    } else {
      // If this value fits into a ULL, try to figure out what else it fits into
      // according to the rules of C99 6.4.4.1p5.

      // Octal, Hexadecimal, and integers with a U suffix are allowed to
      // be an unsigned int.
      bool AllowUnsigned = Literal.isUnsigned || Literal.getRadix() != 10;

      // HLSL doesn't really have `long` or `long long`. We support the `ll`
      // suffix for portability of code with C++, but both `l` and `ll` are
      // 64-bit integer types, and we want the type of `1l` and `1ll` to be the
      // same.
      if (getLangOpts().HLSL && !Literal.isLong && Literal.isLongLong) {
        Literal.isLong = true;
        Literal.isLongLong = false;
      }

      // Check from smallest to largest, picking the smallest type we can.
      unsigned Width = 0;

      // Microsoft specific integer suffixes are explicitly sized.
      if (Literal.MicrosoftInteger) {
        if (Literal.MicrosoftInteger == 8 && !Literal.isUnsigned) {
          Width = 8;
          Ty = Context.CharTy;
        } else {
          Width = Literal.MicrosoftInteger;
          Ty = Context.getIntTypeForBitwidth(Width,
                                             /*Signed=*/!Literal.isUnsigned);
        }
      }

      // Bit-precise integer literals are automagically-sized based on the
      // width required by the literal.
      if (Literal.isBitInt) {
        // The signed version has one more bit for the sign value. There are no
        // zero-width bit-precise integers, even if the literal value is 0.
        Width = std::max(ResultVal.getActiveBits(), 1u) +
                (Literal.isUnsigned ? 0u : 1u);

        // Diagnose if the width of the constant is larger than BITINT_MAXWIDTH,
        // and reset the type to the largest supported width.
        unsigned int MaxBitIntWidth =
            Context.getTargetInfo().getMaxBitIntWidth();
        if (Width > MaxBitIntWidth) {
          Diag(Tok.getLocation(), diag::err_integer_literal_too_large)
              << Literal.isUnsigned;
          Width = MaxBitIntWidth;
        }

        // Reset the result value to the smaller APInt and select the correct
        // type to be used. Note, we zext even for signed values because the
        // literal itself is always an unsigned value (a preceeding - is a
        // unary operator, not part of the literal).
        ResultVal = ResultVal.zextOrTrunc(Width);
        Ty = Context.getBitIntType(Literal.isUnsigned, Width);
      }

      // Check C++23 size_t literals.
      if (Literal.isSizeT) {
        assert(!Literal.MicrosoftInteger &&
               "size_t literals can't be Microsoft literals");
        unsigned SizeTSize = Context.getTargetInfo().getTypeWidth(
            Context.getTargetInfo().getSizeType());

        // Does it fit in size_t?
        if (ResultVal.isIntN(SizeTSize)) {
          // Does it fit in ssize_t?
          if (!Literal.isUnsigned && ResultVal[SizeTSize - 1] == 0)
            Ty = Context.getSignedSizeType();
          else if (AllowUnsigned)
            Ty = Context.getSizeType();
          Width = SizeTSize;
        }
      }

      if (Ty.isNull() && !Literal.isLong && !Literal.isLongLong &&
          !Literal.isSizeT) {
        // Are int/unsigned possibilities?
        unsigned IntSize = Context.getTargetInfo().getIntWidth();

        // Does it fit in a unsigned int?
        if (ResultVal.isIntN(IntSize)) {
          // Does it fit in a signed int?
          if (!Literal.isUnsigned && ResultVal[IntSize-1] == 0)
            Ty = Context.IntTy;
          else if (AllowUnsigned)
            Ty = Context.UnsignedIntTy;
          Width = IntSize;
        }
      }

      // Are long/unsigned long possibilities?
      if (Ty.isNull() && !Literal.isLongLong && !Literal.isSizeT) {
        unsigned LongSize = Context.getTargetInfo().getLongWidth();

        // Does it fit in a unsigned long?
        if (ResultVal.isIntN(LongSize)) {
          // Does it fit in a signed long?
          if (!Literal.isUnsigned && ResultVal[LongSize-1] == 0)
            Ty = Context.LongTy;
          else if (AllowUnsigned)
            Ty = Context.UnsignedLongTy;
          // Check according to the rules of C90 6.1.3.2p5. C++03 [lex.icon]p2
          // is compatible.
          else if (!getLangOpts().C99 && !getLangOpts().CPlusPlus11) {
            const unsigned LongLongSize =
                Context.getTargetInfo().getLongLongWidth();
            Diag(Tok.getLocation(),
                 getLangOpts().CPlusPlus
                     ? Literal.isLong
                           ? diag::warn_old_implicitly_unsigned_long_cxx
                           : /*C++98 UB*/ diag::
                                 ext_old_implicitly_unsigned_long_cxx
                     : diag::warn_old_implicitly_unsigned_long)
                << (LongLongSize > LongSize ? /*will have type 'long long'*/ 0
                                            : /*will be ill-formed*/ 1);
            Ty = Context.UnsignedLongTy;
          }
          Width = LongSize;
        }
      }

      // Check long long if needed.
      if (Ty.isNull() && !Literal.isSizeT) {
        unsigned LongLongSize = Context.getTargetInfo().getLongLongWidth();

        // Does it fit in a unsigned long long?
        if (ResultVal.isIntN(LongLongSize)) {
          // Does it fit in a signed long long?
          // To be compatible with MSVC, hex integer literals ending with the
          // LL or i64 suffix are always signed in Microsoft mode.
          if (!Literal.isUnsigned && (ResultVal[LongLongSize-1] == 0 ||
              (getLangOpts().MSVCCompat && Literal.isLongLong)))
            Ty = Context.LongLongTy;
          else if (AllowUnsigned)
            Ty = Context.UnsignedLongLongTy;
          Width = LongLongSize;

          // 'long long' is a C99 or C++11 feature, whether the literal
          // explicitly specified 'long long' or we needed the extra width.
          if (getLangOpts().CPlusPlus)
            Diag(Tok.getLocation(), getLangOpts().CPlusPlus11
                                        ? diag::warn_cxx98_compat_longlong
                                        : diag::ext_cxx11_longlong);
          else if (!getLangOpts().C99)
            Diag(Tok.getLocation(), diag::ext_c99_longlong);
        }
      }

      // If we still couldn't decide a type, we either have 'size_t' literal
      // that is out of range, or a decimal literal that does not fit in a
      // signed long long and has no U suffix.
      if (Ty.isNull()) {
        if (Literal.isSizeT)
          Diag(Tok.getLocation(), diag::err_size_t_literal_too_large)
              << Literal.isUnsigned;
        else
          Diag(Tok.getLocation(),
               diag::ext_integer_literal_too_large_for_signed);
        Ty = Context.UnsignedLongLongTy;
        Width = Context.getTargetInfo().getLongLongWidth();
      }

      if (ResultVal.getBitWidth() != Width)
        ResultVal = ResultVal.trunc(Width);
    }
    Res = IntegerLiteral::Create(Context, ResultVal, Ty, Tok.getLocation());
  }

  // If this is an imaginary literal, create the ImaginaryLiteral wrapper.
  if (Literal.isImaginary) {
    Res = new (Context) ImaginaryLiteral(Res,
                                        Context.getComplexType(Res->getType()));

    Diag(Tok.getLocation(), diag::ext_imaginary_constant);
  }
  return Res;
}

ExprResult Sema::ActOnParenExpr(SourceLocation L, SourceLocation R, Expr *E) {
  assert(E && "ActOnParenExpr() missing expr");
  QualType ExprTy = E->getType();
  if (getLangOpts().ProtectParens && CurFPFeatures.getAllowFPReassociate() &&
      !E->isLValue() && ExprTy->hasFloatingRepresentation())
    return BuildBuiltinCallExpr(R, Builtin::BI__arithmetic_fence, E);
  return new (Context) ParenExpr(L, R, E);
}

static bool CheckVecStepTraitOperandType(Sema &S, QualType T,
                                         SourceLocation Loc,
                                         SourceRange ArgRange) {
  // [OpenCL 1.1 6.11.12] "The vec_step built-in function takes a built-in
  // scalar or vector data type argument..."
  // Every built-in scalar type (OpenCL 1.1 6.1.1) is either an arithmetic
  // type (C99 6.2.5p18) or void.
  if (!(T->isArithmeticType() || T->isVoidType() || T->isVectorType())) {
    S.Diag(Loc, diag::err_vecstep_non_scalar_vector_type)
      << T << ArgRange;
    return true;
  }

  assert((T->isVoidType() || !T->isIncompleteType()) &&
         "Scalar types should always be complete");
  return false;
}

static bool CheckVectorElementsTraitOperandType(Sema &S, QualType T,
                                                SourceLocation Loc,
                                                SourceRange ArgRange) {
  // builtin_vectorelements supports both fixed-sized and scalable vectors.
  if (!T->isVectorType() && !T->isSizelessVectorType())
    return S.Diag(Loc, diag::err_builtin_non_vector_type)
           << ""
           << "__builtin_vectorelements" << T << ArgRange;

  return false;
}

static bool checkPtrAuthTypeDiscriminatorOperandType(Sema &S, QualType T,
                                                     SourceLocation Loc,
                                                     SourceRange ArgRange) {
  if (S.checkPointerAuthEnabled(Loc, ArgRange))
    return true;

  if (!T->isFunctionType() && !T->isFunctionPointerType() &&
      !T->isFunctionReferenceType() && !T->isMemberFunctionPointerType()) {
    S.Diag(Loc, diag::err_ptrauth_type_disc_undiscriminated) << T << ArgRange;
    return true;
  }

  return false;
}

static bool CheckExtensionTraitOperandType(Sema &S, QualType T,
                                           SourceLocation Loc,
                                           SourceRange ArgRange,
                                           UnaryExprOrTypeTrait TraitKind) {
  // Invalid types must be hard errors for SFINAE in C++.
  if (S.LangOpts.CPlusPlus)
    return true;

  // C99 6.5.3.4p1:
  if (T->isFunctionType() &&
      (TraitKind == UETT_SizeOf || TraitKind == UETT_AlignOf ||
       TraitKind == UETT_PreferredAlignOf)) {
    // sizeof(function)/alignof(function) is allowed as an extension.
    S.Diag(Loc, diag::ext_sizeof_alignof_function_type)
        << getTraitSpelling(TraitKind) << ArgRange;
    return false;
  }

  // Allow sizeof(void)/alignof(void) as an extension, unless in OpenCL where
  // this is an error (OpenCL v1.1 s6.3.k)
  if (T->isVoidType()) {
    unsigned DiagID = S.LangOpts.OpenCL ? diag::err_opencl_sizeof_alignof_type
                                        : diag::ext_sizeof_alignof_void_type;
    S.Diag(Loc, DiagID) << getTraitSpelling(TraitKind) << ArgRange;
    return false;
  }

  return true;
}

static bool CheckObjCTraitOperandConstraints(Sema &S, QualType T,
                                             SourceLocation Loc,
                                             SourceRange ArgRange,
                                             UnaryExprOrTypeTrait TraitKind) {
  // Reject sizeof(interface) and sizeof(interface<proto>) if the
  // runtime doesn't allow it.
  if (!S.LangOpts.ObjCRuntime.allowsSizeofAlignof() && T->isObjCObjectType()) {
    S.Diag(Loc, diag::err_sizeof_nonfragile_interface)
      << T << (TraitKind == UETT_SizeOf)
      << ArgRange;
    return true;
  }

  return false;
}

/// Check whether E is a pointer from a decayed array type (the decayed
/// pointer type is equal to T) and emit a warning if it is.
static void warnOnSizeofOnArrayDecay(Sema &S, SourceLocation Loc, QualType T,
                                     const Expr *E) {
  // Don't warn if the operation changed the type.
  if (T != E->getType())
    return;

  // Now look for array decays.
  const auto *ICE = dyn_cast<ImplicitCastExpr>(E);
  if (!ICE || ICE->getCastKind() != CK_ArrayToPointerDecay)
    return;

  S.Diag(Loc, diag::warn_sizeof_array_decay) << ICE->getSourceRange()
                                             << ICE->getType()
                                             << ICE->getSubExpr()->getType();
}

bool Sema::CheckUnaryExprOrTypeTraitOperand(Expr *E,
                                            UnaryExprOrTypeTrait ExprKind) {
  QualType ExprTy = E->getType();
  assert(!ExprTy->isReferenceType());

  bool IsUnevaluatedOperand =
      (ExprKind == UETT_SizeOf || ExprKind == UETT_DataSizeOf ||
       ExprKind == UETT_AlignOf || ExprKind == UETT_PreferredAlignOf ||
       ExprKind == UETT_VecStep);
  if (IsUnevaluatedOperand) {
    ExprResult Result = CheckUnevaluatedOperand(E);
    if (Result.isInvalid())
      return true;
    E = Result.get();
  }

  // The operand for sizeof and alignof is in an unevaluated expression context,
  // so side effects could result in unintended consequences.
  // Exclude instantiation-dependent expressions, because 'sizeof' is sometimes
  // used to build SFINAE gadgets.
  // FIXME: Should we consider instantiation-dependent operands to 'alignof'?
  if (IsUnevaluatedOperand && !inTemplateInstantiation() &&
      !E->isInstantiationDependent() &&
      !E->getType()->isVariableArrayType() &&
      E->HasSideEffects(Context, false))
    Diag(E->getExprLoc(), diag::warn_side_effects_unevaluated_context);

  if (ExprKind == UETT_VecStep)
    return CheckVecStepTraitOperandType(*this, ExprTy, E->getExprLoc(),
                                        E->getSourceRange());

  if (ExprKind == UETT_VectorElements)
    return CheckVectorElementsTraitOperandType(*this, ExprTy, E->getExprLoc(),
                                               E->getSourceRange());

  // Explicitly list some types as extensions.
  if (!CheckExtensionTraitOperandType(*this, ExprTy, E->getExprLoc(),
                                      E->getSourceRange(), ExprKind))
    return false;

  // WebAssembly tables are always illegal operands to unary expressions and
  // type traits.
  if (Context.getTargetInfo().getTriple().isWasm() &&
      E->getType()->isWebAssemblyTableType()) {
    Diag(E->getExprLoc(), diag::err_wasm_table_invalid_uett_operand)
        << getTraitSpelling(ExprKind);
    return true;
  }

  // 'alignof' applied to an expression only requires the base element type of
  // the expression to be complete. 'sizeof' requires the expression's type to
  // be complete (and will attempt to complete it if it's an array of unknown
  // bound).
  if (ExprKind == UETT_AlignOf || ExprKind == UETT_PreferredAlignOf) {
    if (RequireCompleteSizedType(
            E->getExprLoc(), Context.getBaseElementType(E->getType()),
            diag::err_sizeof_alignof_incomplete_or_sizeless_type,
            getTraitSpelling(ExprKind), E->getSourceRange()))
      return true;
  } else {
    if (RequireCompleteSizedExprType(
            E, diag::err_sizeof_alignof_incomplete_or_sizeless_type,
            getTraitSpelling(ExprKind), E->getSourceRange()))
      return true;
  }

  // Completing the expression's type may have changed it.
  ExprTy = E->getType();
  assert(!ExprTy->isReferenceType());

  if (ExprTy->isFunctionType()) {
    Diag(E->getExprLoc(), diag::err_sizeof_alignof_function_type)
        << getTraitSpelling(ExprKind) << E->getSourceRange();
    return true;
  }

  if (CheckObjCTraitOperandConstraints(*this, ExprTy, E->getExprLoc(),
                                       E->getSourceRange(), ExprKind))
    return true;

  if (ExprKind == UETT_SizeOf) {
    if (const auto *DeclRef = dyn_cast<DeclRefExpr>(E->IgnoreParens())) {
      if (const auto *PVD = dyn_cast<ParmVarDecl>(DeclRef->getFoundDecl())) {
        QualType OType = PVD->getOriginalType();
        QualType Type = PVD->getType();
        if (Type->isPointerType() && OType->isArrayType()) {
          Diag(E->getExprLoc(), diag::warn_sizeof_array_param)
            << Type << OType;
          Diag(PVD->getLocation(), diag::note_declared_at);
        }
      }
    }

    // Warn on "sizeof(array op x)" and "sizeof(x op array)", where the array
    // decays into a pointer and returns an unintended result. This is most
    // likely a typo for "sizeof(array) op x".
    if (const auto *BO = dyn_cast<BinaryOperator>(E->IgnoreParens())) {
      warnOnSizeofOnArrayDecay(*this, BO->getOperatorLoc(), BO->getType(),
                               BO->getLHS());
      warnOnSizeofOnArrayDecay(*this, BO->getOperatorLoc(), BO->getType(),
                               BO->getRHS());
    }
  }

  return false;
}

static bool CheckAlignOfExpr(Sema &S, Expr *E, UnaryExprOrTypeTrait ExprKind) {
  // Cannot know anything else if the expression is dependent.
  if (E->isTypeDependent())
    return false;

  if (E->getObjectKind() == OK_BitField) {
    S.Diag(E->getExprLoc(), diag::err_sizeof_alignof_typeof_bitfield)
       << 1 << E->getSourceRange();
    return true;
  }

  ValueDecl *D = nullptr;
  Expr *Inner = E->IgnoreParens();
  if (DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(Inner)) {
    D = DRE->getDecl();
  } else if (MemberExpr *ME = dyn_cast<MemberExpr>(Inner)) {
    D = ME->getMemberDecl();
  }

  // If it's a field, require the containing struct to have a
  // complete definition so that we can compute the layout.
  //
  // This can happen in C++11 onwards, either by naming the member
  // in a way that is not transformed into a member access expression
  // (in an unevaluated operand, for instance), or by naming the member
  // in a trailing-return-type.
  //
  // For the record, since __alignof__ on expressions is a GCC
  // extension, GCC seems to permit this but always gives the
  // nonsensical answer 0.
  //
  // We don't really need the layout here --- we could instead just
  // directly check for all the appropriate alignment-lowing
  // attributes --- but that would require duplicating a lot of
  // logic that just isn't worth duplicating for such a marginal
  // use-case.
  if (FieldDecl *FD = dyn_cast_or_null<FieldDecl>(D)) {
    // Fast path this check, since we at least know the record has a
    // definition if we can find a member of it.
    if (!FD->getParent()->isCompleteDefinition()) {
      S.Diag(E->getExprLoc(), diag::err_alignof_member_of_incomplete_type)
        << E->getSourceRange();
      return true;
    }

    // Otherwise, if it's a field, and the field doesn't have
    // reference type, then it must have a complete type (or be a
    // flexible array member, which we explicitly want to
    // white-list anyway), which makes the following checks trivial.
    if (!FD->getType()->isReferenceType())
      return false;
  }

  return S.CheckUnaryExprOrTypeTraitOperand(E, ExprKind);
}

bool Sema::CheckVecStepExpr(Expr *E) {
  E = E->IgnoreParens();

  // Cannot know anything else if the expression is dependent.
  if (E->isTypeDependent())
    return false;

  return CheckUnaryExprOrTypeTraitOperand(E, UETT_VecStep);
}

static void captureVariablyModifiedType(ASTContext &Context, QualType T,
                                        CapturingScopeInfo *CSI) {
  assert(T->isVariablyModifiedType());
  assert(CSI != nullptr);

  // We're going to walk down into the type and look for VLA expressions.
  do {
    const Type *Ty = T.getTypePtr();
    switch (Ty->getTypeClass()) {
#define TYPE(Class, Base)
#define ABSTRACT_TYPE(Class, Base)
#define NON_CANONICAL_TYPE(Class, Base)
#define DEPENDENT_TYPE(Class, Base) case Type::Class:
#define NON_CANONICAL_UNLESS_DEPENDENT_TYPE(Class, Base)
#include "clang/AST/TypeNodes.inc"
      T = QualType();
      break;
    // These types are never variably-modified.
    case Type::Builtin:
    case Type::Complex:
    case Type::Vector:
    case Type::ExtVector:
    case Type::ConstantMatrix:
    case Type::Record:
    case Type::Enum:
    case Type::TemplateSpecialization:
    case Type::ObjCObject:
    case Type::ObjCInterface:
    case Type::ObjCObjectPointer:
    case Type::ObjCTypeParam:
    case Type::Pipe:
    case Type::BitInt:
      llvm_unreachable("type class is never variably-modified!");
    case Type::Elaborated:
      T = cast<ElaboratedType>(Ty)->getNamedType();
      break;
    case Type::Adjusted:
      T = cast<AdjustedType>(Ty)->getOriginalType();
      break;
    case Type::Decayed:
      T = cast<DecayedType>(Ty)->getPointeeType();
      break;
    case Type::ArrayParameter:
      T = cast<ArrayParameterType>(Ty)->getElementType();
      break;
    case Type::Pointer:
      T = cast<PointerType>(Ty)->getPointeeType();
      break;
    case Type::BlockPointer:
      T = cast<BlockPointerType>(Ty)->getPointeeType();
      break;
    case Type::LValueReference:
    case Type::RValueReference:
      T = cast<ReferenceType>(Ty)->getPointeeType();
      break;
    case Type::MemberPointer:
      T = cast<MemberPointerType>(Ty)->getPointeeType();
      break;
    case Type::ConstantArray:
    case Type::IncompleteArray:
      // Losing element qualification here is fine.
      T = cast<ArrayType>(Ty)->getElementType();
      break;
    case Type::VariableArray: {
      // Losing element qualification here is fine.
      const VariableArrayType *VAT = cast<VariableArrayType>(Ty);

      // Unknown size indication requires no size computation.
      // Otherwise, evaluate and record it.
      auto Size = VAT->getSizeExpr();
      if (Size && !CSI->isVLATypeCaptured(VAT) &&
          (isa<CapturedRegionScopeInfo>(CSI) || isa<LambdaScopeInfo>(CSI)))
        CSI->addVLATypeCapture(Size->getExprLoc(), VAT, Context.getSizeType());

      T = VAT->getElementType();
      break;
    }
    case Type::FunctionProto:
    case Type::FunctionNoProto:
      T = cast<FunctionType>(Ty)->getReturnType();
      break;
    case Type::Paren:
    case Type::TypeOf:
    case Type::UnaryTransform:
    case Type::Attributed:
    case Type::BTFTagAttributed:
    case Type::SubstTemplateTypeParm:
    case Type::MacroQualified:
    case Type::CountAttributed:
      // Keep walking after single level desugaring.
      T = T.getSingleStepDesugaredType(Context);
      break;
    case Type::Typedef:
      T = cast<TypedefType>(Ty)->desugar();
      break;
    case Type::Decltype:
      T = cast<DecltypeType>(Ty)->desugar();
      break;
    case Type::PackIndexing:
      T = cast<PackIndexingType>(Ty)->desugar();
      break;
    case Type::Using:
      T = cast<UsingType>(Ty)->desugar();
      break;
    case Type::Auto:
    case Type::DeducedTemplateSpecialization:
      T = cast<DeducedType>(Ty)->getDeducedType();
      break;
    case Type::TypeOfExpr:
      T = cast<TypeOfExprType>(Ty)->getUnderlyingExpr()->getType();
      break;
    case Type::Atomic:
      T = cast<AtomicType>(Ty)->getValueType();
      break;
    }
  } while (!T.isNull() && T->isVariablyModifiedType());
}

bool Sema::CheckUnaryExprOrTypeTraitOperand(QualType ExprType,
                                            SourceLocation OpLoc,
                                            SourceRange ExprRange,
                                            UnaryExprOrTypeTrait ExprKind,
                                            StringRef KWName) {
  if (ExprType->isDependentType())
    return false;

  // C++ [expr.sizeof]p2:
  //     When applied to a reference or a reference type, the result
  //     is the size of the referenced type.
  // C++11 [expr.alignof]p3:
  //     When alignof is applied to a reference type, the result
  //     shall be the alignment of the referenced type.
  if (const ReferenceType *Ref = ExprType->getAs<ReferenceType>())
    ExprType = Ref->getPointeeType();

  // C11 6.5.3.4/3, C++11 [expr.alignof]p3:
  //   When alignof or _Alignof is applied to an array type, the result
  //   is the alignment of the element type.
  if (ExprKind == UETT_AlignOf || ExprKind == UETT_PreferredAlignOf ||
      ExprKind == UETT_OpenMPRequiredSimdAlign) {
    // If the trait is 'alignof' in C before C2y, the ability to apply the
    // trait to an incomplete array is an extension.
    if (ExprKind == UETT_AlignOf && !getLangOpts().CPlusPlus &&
        ExprType->isIncompleteArrayType())
      Diag(OpLoc, getLangOpts().C2y
                      ? diag::warn_c2y_compat_alignof_incomplete_array
                      : diag::ext_c2y_alignof_incomplete_array);
    ExprType = Context.getBaseElementType(ExprType);
  }

  if (ExprKind == UETT_VecStep)
    return CheckVecStepTraitOperandType(*this, ExprType, OpLoc, ExprRange);

  if (ExprKind == UETT_VectorElements)
    return CheckVectorElementsTraitOperandType(*this, ExprType, OpLoc,
                                               ExprRange);

  if (ExprKind == UETT_PtrAuthTypeDiscriminator)
    return checkPtrAuthTypeDiscriminatorOperandType(*this, ExprType, OpLoc,
                                                    ExprRange);

  // Explicitly list some types as extensions.
  if (!CheckExtensionTraitOperandType(*this, ExprType, OpLoc, ExprRange,
                                      ExprKind))
    return false;

  if (RequireCompleteSizedType(
          OpLoc, ExprType, diag::err_sizeof_alignof_incomplete_or_sizeless_type,
          KWName, ExprRange))
    return true;

  if (ExprType->isFunctionType()) {
    Diag(OpLoc, diag::err_sizeof_alignof_function_type) << KWName << ExprRange;
    return true;
  }

  // WebAssembly tables are always illegal operands to unary expressions and
  // type traits.
  if (Context.getTargetInfo().getTriple().isWasm() &&
      ExprType->isWebAssemblyTableType()) {
    Diag(OpLoc, diag::err_wasm_table_invalid_uett_operand)
        << getTraitSpelling(ExprKind);
    return true;
  }

  if (CheckObjCTraitOperandConstraints(*this, ExprType, OpLoc, ExprRange,
                                       ExprKind))
    return true;

  if (ExprType->isVariablyModifiedType() && FunctionScopes.size() > 1) {
    if (auto *TT = ExprType->getAs<TypedefType>()) {
      for (auto I = FunctionScopes.rbegin(),
                E = std::prev(FunctionScopes.rend());
           I != E; ++I) {
        auto *CSI = dyn_cast<CapturingScopeInfo>(*I);
        if (CSI == nullptr)
          break;
        DeclContext *DC = nullptr;
        if (auto *LSI = dyn_cast<LambdaScopeInfo>(CSI))
          DC = LSI->CallOperator;
        else if (auto *CRSI = dyn_cast<CapturedRegionScopeInfo>(CSI))
          DC = CRSI->TheCapturedDecl;
        else if (auto *BSI = dyn_cast<BlockScopeInfo>(CSI))
          DC = BSI->TheDecl;
        if (DC) {
          if (DC->containsDecl(TT->getDecl()))
            break;
          captureVariablyModifiedType(Context, ExprType, CSI);
        }
      }
    }
  }

  return false;
}

ExprResult Sema::CreateUnaryExprOrTypeTraitExpr(TypeSourceInfo *TInfo,
                                                SourceLocation OpLoc,
                                                UnaryExprOrTypeTrait ExprKind,
                                                SourceRange R) {
  if (!TInfo)
    return ExprError();

  QualType T = TInfo->getType();

  if (!T->isDependentType() &&
      CheckUnaryExprOrTypeTraitOperand(T, OpLoc, R, ExprKind,
                                       getTraitSpelling(ExprKind)))
    return ExprError();

  // Adds overload of TransformToPotentiallyEvaluated for TypeSourceInfo to
  // properly deal with VLAs in nested calls of sizeof and typeof.
  if (isUnevaluatedContext() && ExprKind == UETT_SizeOf &&
      TInfo->getType()->isVariablyModifiedType())
    TInfo = TransformToPotentiallyEvaluated(TInfo);

  // C99 6.5.3.4p4: the type (an unsigned integer type) is size_t.
  return new (Context) UnaryExprOrTypeTraitExpr(
      ExprKind, TInfo, Context.getSizeType(), OpLoc, R.getEnd());
}

ExprResult
Sema::CreateUnaryExprOrTypeTraitExpr(Expr *E, SourceLocation OpLoc,
                                     UnaryExprOrTypeTrait ExprKind) {
  ExprResult PE = CheckPlaceholderExpr(E);
  if (PE.isInvalid())
    return ExprError();

  E = PE.get();

  // Verify that the operand is valid.
  bool isInvalid = false;
  if (E->isTypeDependent()) {
    // Delay type-checking for type-dependent expressions.
  } else if (ExprKind == UETT_AlignOf || ExprKind == UETT_PreferredAlignOf) {
    isInvalid = CheckAlignOfExpr(*this, E, ExprKind);
  } else if (ExprKind == UETT_VecStep) {
    isInvalid = CheckVecStepExpr(E);
  } else if (ExprKind == UETT_OpenMPRequiredSimdAlign) {
      Diag(E->getExprLoc(), diag::err_openmp_default_simd_align_expr);
      isInvalid = true;
  } else if (E->refersToBitField()) {  // C99 6.5.3.4p1.
    Diag(E->getExprLoc(), diag::err_sizeof_alignof_typeof_bitfield) << 0;
    isInvalid = true;
  } else if (ExprKind == UETT_VectorElements) {
    isInvalid = CheckUnaryExprOrTypeTraitOperand(E, UETT_VectorElements);
  } else {
    isInvalid = CheckUnaryExprOrTypeTraitOperand(E, UETT_SizeOf);
  }

  if (isInvalid)
    return ExprError();

  if (ExprKind == UETT_SizeOf && E->getType()->isVariableArrayType()) {
    PE = TransformToPotentiallyEvaluated(E);
    if (PE.isInvalid()) return ExprError();
    E = PE.get();
  }

  // C99 6.5.3.4p4: the type (an unsigned integer type) is size_t.
  return new (Context) UnaryExprOrTypeTraitExpr(
      ExprKind, E, Context.getSizeType(), OpLoc, E->getSourceRange().getEnd());
}

ExprResult
Sema::ActOnUnaryExprOrTypeTraitExpr(SourceLocation OpLoc,
                                    UnaryExprOrTypeTrait ExprKind, bool IsType,
                                    void *TyOrEx, SourceRange ArgRange) {
  // If error parsing type, ignore.
  if (!TyOrEx) return ExprError();

  if (IsType) {
    TypeSourceInfo *TInfo;
    (void) GetTypeFromParser(ParsedType::getFromOpaquePtr(TyOrEx), &TInfo);
    return CreateUnaryExprOrTypeTraitExpr(TInfo, OpLoc, ExprKind, ArgRange);
  }

  Expr *ArgEx = (Expr *)TyOrEx;
  ExprResult Result = CreateUnaryExprOrTypeTraitExpr(ArgEx, OpLoc, ExprKind);
  return Result;
}

bool Sema::CheckAlignasTypeArgument(StringRef KWName, TypeSourceInfo *TInfo,
                                    SourceLocation OpLoc, SourceRange R) {
  if (!TInfo)
    return true;
  return CheckUnaryExprOrTypeTraitOperand(TInfo->getType(), OpLoc, R,
                                          UETT_AlignOf, KWName);
}

bool Sema::ActOnAlignasTypeArgument(StringRef KWName, ParsedType Ty,
                                    SourceLocation OpLoc, SourceRange R) {
  TypeSourceInfo *TInfo;
  (void)GetTypeFromParser(ParsedType::getFromOpaquePtr(Ty.getAsOpaquePtr()),
                          &TInfo);
  return CheckAlignasTypeArgument(KWName, TInfo, OpLoc, R);
}

static QualType CheckRealImagOperand(Sema &S, ExprResult &V, SourceLocation Loc,
                                     bool IsReal) {
  if (V.get()->isTypeDependent())
    return S.Context.DependentTy;

  // _Real and _Imag are only l-values for normal l-values.
  if (V.get()->getObjectKind() != OK_Ordinary) {
    V = S.DefaultLvalueConversion(V.get());
    if (V.isInvalid())
      return QualType();
  }

  // These operators return the element type of a complex type.
  if (const ComplexType *CT = V.get()->getType()->getAs<ComplexType>())
    return CT->getElementType();

  // Otherwise they pass through real integer and floating point types here.
  if (V.get()->getType()->isArithmeticType())
    return V.get()->getType();

  // Test for placeholders.
  ExprResult PR = S.CheckPlaceholderExpr(V.get());
  if (PR.isInvalid()) return QualType();
  if (PR.get() != V.get()) {
    V = PR;
    return CheckRealImagOperand(S, V, Loc, IsReal);
  }

  // Reject anything else.
  S.Diag(Loc, diag::err_realimag_invalid_type) << V.get()->getType()
    << (IsReal ? "__real" : "__imag");
  return QualType();
}



ExprResult
Sema::ActOnPostfixUnaryOp(Scope *S, SourceLocation OpLoc,
                          tok::TokenKind Kind, Expr *Input) {
  UnaryOperatorKind Opc;
  switch (Kind) {
  default: llvm_unreachable("Unknown unary op!");
  case tok::plusplus:   Opc = UO_PostInc; break;
  case tok::minusminus: Opc = UO_PostDec; break;
  }

  // Since this might is a postfix expression, get rid of ParenListExprs.
  ExprResult Result = MaybeConvertParenListExprToParenExpr(S, Input);
  if (Result.isInvalid()) return ExprError();
  Input = Result.get();

  return BuildUnaryOp(S, OpLoc, Opc, Input);
}

/// Diagnose if arithmetic on the given ObjC pointer is illegal.
///
/// \return true on error
static bool checkArithmeticOnObjCPointer(Sema &S,
                                         SourceLocation opLoc,
                                         Expr *op) {
  assert(op->getType()->isObjCObjectPointerType());
  if (S.LangOpts.ObjCRuntime.allowsPointerArithmetic() &&
      !S.LangOpts.ObjCSubscriptingLegacyRuntime)
    return false;

  S.Diag(opLoc, diag::err_arithmetic_nonfragile_interface)
    << op->getType()->castAs<ObjCObjectPointerType>()->getPointeeType()
    << op->getSourceRange();
  return true;
}

static bool isMSPropertySubscriptExpr(Sema &S, Expr *Base) {
  auto *BaseNoParens = Base->IgnoreParens();
  if (auto *MSProp = dyn_cast<MSPropertyRefExpr>(BaseNoParens))
    return MSProp->getPropertyDecl()->getType()->isArrayType();
  return isa<MSPropertySubscriptExpr>(BaseNoParens);
}

// Returns the type used for LHS[RHS], given one of LHS, RHS is type-dependent.
// Typically this is DependentTy, but can sometimes be more precise.
//
// There are cases when we could determine a non-dependent type:
//  - LHS and RHS may have non-dependent types despite being type-dependent
//    (e.g. unbounded array static members of the current instantiation)
//  - one may be a dependent-sized array with known element type
//  - one may be a dependent-typed valid index (enum in current instantiation)
//
// We *always* return a dependent type, in such cases it is DependentTy.
// This avoids creating type-dependent expressions with non-dependent types.
// FIXME: is this important to avoid? See https://reviews.llvm.org/D107275
static QualType getDependentArraySubscriptType(Expr *LHS, Expr *RHS,
                                               const ASTContext &Ctx) {
  assert(LHS->isTypeDependent() || RHS->isTypeDependent());
  QualType LTy = LHS->getType(), RTy = RHS->getType();
  QualType Result = Ctx.DependentTy;
  if (RTy->isIntegralOrUnscopedEnumerationType()) {
    if (const PointerType *PT = LTy->getAs<PointerType>())
      Result = PT->getPointeeType();
    else if (const ArrayType *AT = LTy->getAsArrayTypeUnsafe())
      Result = AT->getElementType();
  } else if (LTy->isIntegralOrUnscopedEnumerationType()) {
    if (const PointerType *PT = RTy->getAs<PointerType>())
      Result = PT->getPointeeType();
    else if (const ArrayType *AT = RTy->getAsArrayTypeUnsafe())
      Result = AT->getElementType();
  }
  // Ensure we return a dependent type.
  return Result->isDependentType() ? Result : Ctx.DependentTy;
}

ExprResult Sema::ActOnArraySubscriptExpr(Scope *S, Expr *base,
                                         SourceLocation lbLoc,
                                         MultiExprArg ArgExprs,
                                         SourceLocation rbLoc) {

  if (base && !base->getType().isNull() &&
      base->hasPlaceholderType(BuiltinType::ArraySection)) {
    auto *AS = cast<ArraySectionExpr>(base);
    if (AS->isOMPArraySection())
      return OpenMP().ActOnOMPArraySectionExpr(
          base, lbLoc, ArgExprs.front(), SourceLocation(), SourceLocation(),
          /*Length*/ nullptr,
          /*Stride=*/nullptr, rbLoc);

    return OpenACC().ActOnArraySectionExpr(base, lbLoc, ArgExprs.front(),
                                           SourceLocation(), /*Length*/ nullptr,
                                           rbLoc);
  }

  // Since this might be a postfix expression, get rid of ParenListExprs.
  if (isa<ParenListExpr>(base)) {
    ExprResult result = MaybeConvertParenListExprToParenExpr(S, base);
    if (result.isInvalid())
      return ExprError();
    base = result.get();
  }

  // Check if base and idx form a MatrixSubscriptExpr.
  //
  // Helper to check for comma expressions, which are not allowed as indices for
  // matrix subscript expressions.
  auto CheckAndReportCommaError = [this, base, rbLoc](Expr *E) {
    if (isa<BinaryOperator>(E) && cast<BinaryOperator>(E)->isCommaOp()) {
      Diag(E->getExprLoc(), diag::err_matrix_subscript_comma)
          << SourceRange(base->getBeginLoc(), rbLoc);
      return true;
    }
    return false;
  };
  // The matrix subscript operator ([][])is considered a single operator.
  // Separating the index expressions by parenthesis is not allowed.
  if (base && !base->getType().isNull() &&
      base->hasPlaceholderType(BuiltinType::IncompleteMatrixIdx) &&
      !isa<MatrixSubscriptExpr>(base)) {
    Diag(base->getExprLoc(), diag::err_matrix_separate_incomplete_index)
        << SourceRange(base->getBeginLoc(), rbLoc);
    return ExprError();
  }
  // If the base is a MatrixSubscriptExpr, try to create a new
  // MatrixSubscriptExpr.
  auto *matSubscriptE = dyn_cast<MatrixSubscriptExpr>(base);
  if (matSubscriptE) {
    assert(ArgExprs.size() == 1);
    if (CheckAndReportCommaError(ArgExprs.front()))
      return ExprError();

    assert(matSubscriptE->isIncomplete() &&
           "base has to be an incomplete matrix subscript");
    return CreateBuiltinMatrixSubscriptExpr(matSubscriptE->getBase(),
                                            matSubscriptE->getRowIdx(),
                                            ArgExprs.front(), rbLoc);
  }
  if (base->getType()->isWebAssemblyTableType()) {
    Diag(base->getExprLoc(), diag::err_wasm_table_art)
        << SourceRange(base->getBeginLoc(), rbLoc) << 3;
    return ExprError();
  }

  // Handle any non-overload placeholder types in the base and index
  // expressions.  We can't handle overloads here because the other
  // operand might be an overloadable type, in which case the overload
  // resolution for the operator overload should get the first crack
  // at the overload.
  bool IsMSPropertySubscript = false;
  if (base->getType()->isNonOverloadPlaceholderType()) {
    IsMSPropertySubscript = isMSPropertySubscriptExpr(*this, base);
    if (!IsMSPropertySubscript) {
      ExprResult result = CheckPlaceholderExpr(base);
      if (result.isInvalid())
        return ExprError();
      base = result.get();
    }
  }

  // If the base is a matrix type, try to create a new MatrixSubscriptExpr.
  if (base->getType()->isMatrixType()) {
    assert(ArgExprs.size() == 1);
    if (CheckAndReportCommaError(ArgExprs.front()))
      return ExprError();

    return CreateBuiltinMatrixSubscriptExpr(base, ArgExprs.front(), nullptr,
                                            rbLoc);
  }

  if (ArgExprs.size() == 1 && getLangOpts().CPlusPlus20) {
    Expr *idx = ArgExprs[0];
    if ((isa<BinaryOperator>(idx) && cast<BinaryOperator>(idx)->isCommaOp()) ||
        (isa<CXXOperatorCallExpr>(idx) &&
         cast<CXXOperatorCallExpr>(idx)->getOperator() == OO_Comma)) {
      Diag(idx->getExprLoc(), diag::warn_deprecated_comma_subscript)
          << SourceRange(base->getBeginLoc(), rbLoc);
    }
  }

  if (ArgExprs.size() == 1 &&
      ArgExprs[0]->getType()->isNonOverloadPlaceholderType()) {
    ExprResult result = CheckPlaceholderExpr(ArgExprs[0]);
    if (result.isInvalid())
      return ExprError();
    ArgExprs[0] = result.get();
  } else {
    if (CheckArgsForPlaceholders(ArgExprs))
      return ExprError();
  }

  // Build an unanalyzed expression if either operand is type-dependent.
  if (getLangOpts().CPlusPlus && ArgExprs.size() == 1 &&
      (base->isTypeDependent() ||
       Expr::hasAnyTypeDependentArguments(ArgExprs)) &&
      !isa<PackExpansionExpr>(ArgExprs[0])) {
    return new (Context) ArraySubscriptExpr(
        base, ArgExprs.front(),
        getDependentArraySubscriptType(base, ArgExprs.front(), getASTContext()),
        VK_LValue, OK_Ordinary, rbLoc);
  }

  // MSDN, property (C++)
  // https://msdn.microsoft.com/en-us/library/yhfk0thd(v=vs.120).aspx
  // This attribute can also be used in the declaration of an empty array in a
  // class or structure definition. For example:
  // __declspec(property(get=GetX, put=PutX)) int x[];
  // The above statement indicates that x[] can be used with one or more array
  // indices. In this case, i=p->x[a][b] will be turned into i=p->GetX(a, b),
  // and p->x[a][b] = i will be turned into p->PutX(a, b, i);
  if (IsMSPropertySubscript) {
    assert(ArgExprs.size() == 1);
    // Build MS property subscript expression if base is MS property reference
    // or MS property subscript.
    return new (Context)
        MSPropertySubscriptExpr(base, ArgExprs.front(), Context.PseudoObjectTy,
                                VK_LValue, OK_Ordinary, rbLoc);
  }

  // Use C++ overloaded-operator rules if either operand has record
  // type.  The spec says to do this if either type is *overloadable*,
  // but enum types can't declare subscript operators or conversion
  // operators, so there's nothing interesting for overload resolution
  // to do if there aren't any record types involved.
  //
  // ObjC pointers have their own subscripting logic that is not tied
  // to overload resolution and so should not take this path.
  if (getLangOpts().CPlusPlus && !base->getType()->isObjCObjectPointerType() &&
      ((base->getType()->isRecordType() ||
        (ArgExprs.size() != 1 || isa<PackExpansionExpr>(ArgExprs[0]) ||
         ArgExprs[0]->getType()->isRecordType())))) {
    return CreateOverloadedArraySubscriptExpr(lbLoc, rbLoc, base, ArgExprs);
  }

  ExprResult Res =
      CreateBuiltinArraySubscriptExpr(base, lbLoc, ArgExprs.front(), rbLoc);

  if (!Res.isInvalid() && isa<ArraySubscriptExpr>(Res.get()))
    CheckSubscriptAccessOfNoDeref(cast<ArraySubscriptExpr>(Res.get()));

  return Res;
}

ExprResult Sema::tryConvertExprToType(Expr *E, QualType Ty) {
  InitializedEntity Entity = InitializedEntity::InitializeTemporary(Ty);
  InitializationKind Kind =
      InitializationKind::CreateCopy(E->getBeginLoc(), SourceLocation());
  InitializationSequence InitSeq(*this, Entity, Kind, E);
  return InitSeq.Perform(*this, Entity, Kind, E);
}

ExprResult Sema::CreateBuiltinMatrixSubscriptExpr(Expr *Base, Expr *RowIdx,
                                                  Expr *ColumnIdx,
                                                  SourceLocation RBLoc) {
  ExprResult BaseR = CheckPlaceholderExpr(Base);
  if (BaseR.isInvalid())
    return BaseR;
  Base = BaseR.get();

  ExprResult RowR = CheckPlaceholderExpr(RowIdx);
  if (RowR.isInvalid())
    return RowR;
  RowIdx = RowR.get();

  if (!ColumnIdx)
    return new (Context) MatrixSubscriptExpr(
        Base, RowIdx, ColumnIdx, Context.IncompleteMatrixIdxTy, RBLoc);

  // Build an unanalyzed expression if any of the operands is type-dependent.
  if (Base->isTypeDependent() || RowIdx->isTypeDependent() ||
      ColumnIdx->isTypeDependent())
    return new (Context) MatrixSubscriptExpr(Base, RowIdx, ColumnIdx,
                                             Context.DependentTy, RBLoc);

  ExprResult ColumnR = CheckPlaceholderExpr(ColumnIdx);
  if (ColumnR.isInvalid())
    return ColumnR;
  ColumnIdx = ColumnR.get();

  // Check that IndexExpr is an integer expression. If it is a constant
  // expression, check that it is less than Dim (= the number of elements in the
  // corresponding dimension).
  auto IsIndexValid = [&](Expr *IndexExpr, unsigned Dim,
                          bool IsColumnIdx) -> Expr * {
    if (!IndexExpr->getType()->isIntegerType() &&
        !IndexExpr->isTypeDependent()) {
      Diag(IndexExpr->getBeginLoc(), diag::err_matrix_index_not_integer)
          << IsColumnIdx;
      return nullptr;
    }

    if (std::optional<llvm::APSInt> Idx =
            IndexExpr->getIntegerConstantExpr(Context)) {
      if ((*Idx < 0 || *Idx >= Dim)) {
        Diag(IndexExpr->getBeginLoc(), diag::err_matrix_index_outside_range)
            << IsColumnIdx << Dim;
        return nullptr;
      }
    }

    ExprResult ConvExpr =
        tryConvertExprToType(IndexExpr, Context.getSizeType());
    assert(!ConvExpr.isInvalid() &&
           "should be able to convert any integer type to size type");
    return ConvExpr.get();
  };

  auto *MTy = Base->getType()->getAs<ConstantMatrixType>();
  RowIdx = IsIndexValid(RowIdx, MTy->getNumRows(), false);
  ColumnIdx = IsIndexValid(ColumnIdx, MTy->getNumColumns(), true);
  if (!RowIdx || !ColumnIdx)
    return ExprError();

  return new (Context) MatrixSubscriptExpr(Base, RowIdx, ColumnIdx,
                                           MTy->getElementType(), RBLoc);
}

void Sema::CheckAddressOfNoDeref(const Expr *E) {
  ExpressionEvaluationContextRecord &LastRecord = ExprEvalContexts.back();
  const Expr *StrippedExpr = E->IgnoreParenImpCasts();

  // For expressions like `&(*s).b`, the base is recorded and what should be
  // checked.
  const MemberExpr *Member = nullptr;
  while ((Member = dyn_cast<MemberExpr>(StrippedExpr)) && !Member->isArrow())
    StrippedExpr = Member->getBase()->IgnoreParenImpCasts();

  LastRecord.PossibleDerefs.erase(StrippedExpr);
}

void Sema::CheckSubscriptAccessOfNoDeref(const ArraySubscriptExpr *E) {
  if (isUnevaluatedContext())
    return;

  QualType ResultTy = E->getType();
  ExpressionEvaluationContextRecord &LastRecord = ExprEvalContexts.back();

  // Bail if the element is an array since it is not memory access.
  if (isa<ArrayType>(ResultTy))
    return;

  if (ResultTy->hasAttr(attr::NoDeref)) {
    LastRecord.PossibleDerefs.insert(E);
    return;
  }

  // Check if the base type is a pointer to a member access of a struct
  // marked with noderef.
  const Expr *Base = E->getBase();
  QualType BaseTy = Base->getType();
  if (!(isa<ArrayType>(BaseTy) || isa<PointerType>(BaseTy)))
    // Not a pointer access
    return;

  const MemberExpr *Member = nullptr;
  while ((Member = dyn_cast<MemberExpr>(Base->IgnoreParenCasts())) &&
         Member->isArrow())
    Base = Member->getBase();

  if (const auto *Ptr = dyn_cast<PointerType>(Base->getType())) {
    if (Ptr->getPointeeType()->hasAttr(attr::NoDeref))
      LastRecord.PossibleDerefs.insert(E);
  }
}

ExprResult
Sema::CreateBuiltinArraySubscriptExpr(Expr *Base, SourceLocation LLoc,
                                      Expr *Idx, SourceLocation RLoc) {
  Expr *LHSExp = Base;
  Expr *RHSExp = Idx;

  ExprValueKind VK = VK_LValue;
  ExprObjectKind OK = OK_Ordinary;

  // Per C++ core issue 1213, the result is an xvalue if either operand is
  // a non-lvalue array, and an lvalue otherwise.
  if (getLangOpts().CPlusPlus11) {
    for (auto *Op : {LHSExp, RHSExp}) {
      Op = Op->IgnoreImplicit();
      if (Op->getType()->isArrayType() && !Op->isLValue())
        VK = VK_XValue;
    }
  }

  // Perform default conversions.
  if (!LHSExp->getType()->isSubscriptableVectorType()) {
    ExprResult Result = DefaultFunctionArrayLvalueConversion(LHSExp);
    if (Result.isInvalid())
      return ExprError();
    LHSExp = Result.get();
  }
  ExprResult Result = DefaultFunctionArrayLvalueConversion(RHSExp);
  if (Result.isInvalid())
    return ExprError();
  RHSExp = Result.get();

  QualType LHSTy = LHSExp->getType(), RHSTy = RHSExp->getType();

  // C99 6.5.2.1p2: the expression e1[e2] is by definition precisely equivalent
  // to the expression *((e1)+(e2)). This means the array "Base" may actually be
  // in the subscript position. As a result, we need to derive the array base
  // and index from the expression types.
  Expr *BaseExpr, *IndexExpr;
  QualType ResultType;
  if (LHSTy->isDependentType() || RHSTy->isDependentType()) {
    BaseExpr = LHSExp;
    IndexExpr = RHSExp;
    ResultType =
        getDependentArraySubscriptType(LHSExp, RHSExp, getASTContext());
  } else if (const PointerType *PTy = LHSTy->getAs<PointerType>()) {
    BaseExpr = LHSExp;
    IndexExpr = RHSExp;
    ResultType = PTy->getPointeeType();
  } else if (const ObjCObjectPointerType *PTy =
               LHSTy->getAs<ObjCObjectPointerType>()) {
    BaseExpr = LHSExp;
    IndexExpr = RHSExp;

    // Use custom logic if this should be the pseudo-object subscript
    // expression.
    if (!LangOpts.isSubscriptPointerArithmetic())
      return ObjC().BuildObjCSubscriptExpression(RLoc, BaseExpr, IndexExpr,
                                                 nullptr, nullptr);

    ResultType = PTy->getPointeeType();
  } else if (const PointerType *PTy = RHSTy->getAs<PointerType>()) {
     // Handle the uncommon case of "123[Ptr]".
    BaseExpr = RHSExp;
    IndexExpr = LHSExp;
    ResultType = PTy->getPointeeType();
  } else if (const ObjCObjectPointerType *PTy =
               RHSTy->getAs<ObjCObjectPointerType>()) {
     // Handle the uncommon case of "123[Ptr]".
    BaseExpr = RHSExp;
    IndexExpr = LHSExp;
    ResultType = PTy->getPointeeType();
    if (!LangOpts.isSubscriptPointerArithmetic()) {
      Diag(LLoc, diag::err_subscript_nonfragile_interface)
        << ResultType << BaseExpr->getSourceRange();
      return ExprError();
    }
  } else if (LHSTy->isSubscriptableVectorType()) {
    if (LHSTy->isBuiltinType() &&
        LHSTy->getAs<BuiltinType>()->isSveVLSBuiltinType()) {
      const BuiltinType *BTy = LHSTy->getAs<BuiltinType>();
      if (BTy->isSVEBool())
        return ExprError(Diag(LLoc, diag::err_subscript_svbool_t)
                         << LHSExp->getSourceRange()
                         << RHSExp->getSourceRange());
      ResultType = BTy->getSveEltType(Context);
    } else {
      const VectorType *VTy = LHSTy->getAs<VectorType>();
      ResultType = VTy->getElementType();
    }
    BaseExpr = LHSExp; // vectors: V[123]
    IndexExpr = RHSExp;
    // We apply C++ DR1213 to vector subscripting too.
    if (getLangOpts().CPlusPlus11 && LHSExp->isPRValue()) {
      ExprResult Materialized = TemporaryMaterializationConversion(LHSExp);
      if (Materialized.isInvalid())
        return ExprError();
      LHSExp = Materialized.get();
    }
    VK = LHSExp->getValueKind();
    if (VK != VK_PRValue)
      OK = OK_VectorComponent;

    QualType BaseType = BaseExpr->getType();
    Qualifiers BaseQuals = BaseType.getQualifiers();
    Qualifiers MemberQuals = ResultType.getQualifiers();
    Qualifiers Combined = BaseQuals + MemberQuals;
    if (Combined != MemberQuals)
      ResultType = Context.getQualifiedType(ResultType, Combined);
  } else if (LHSTy->isArrayType()) {
    // If we see an array that wasn't promoted by
    // DefaultFunctionArrayLvalueConversion, it must be an array that
    // wasn't promoted because of the C90 rule that doesn't
    // allow promoting non-lvalue arrays.  Warn, then
    // force the promotion here.
    Diag(LHSExp->getBeginLoc(), diag::ext_subscript_non_lvalue)
        << LHSExp->getSourceRange();
    LHSExp = ImpCastExprToType(LHSExp, Context.getArrayDecayedType(LHSTy),
                               CK_ArrayToPointerDecay).get();
    LHSTy = LHSExp->getType();

    BaseExpr = LHSExp;
    IndexExpr = RHSExp;
    ResultType = LHSTy->castAs<PointerType>()->getPointeeType();
  } else if (RHSTy->isArrayType()) {
    // Same as previous, except for 123[f().a] case
    Diag(RHSExp->getBeginLoc(), diag::ext_subscript_non_lvalue)
        << RHSExp->getSourceRange();
    RHSExp = ImpCastExprToType(RHSExp, Context.getArrayDecayedType(RHSTy),
                               CK_ArrayToPointerDecay).get();
    RHSTy = RHSExp->getType();

    BaseExpr = RHSExp;
    IndexExpr = LHSExp;
    ResultType = RHSTy->castAs<PointerType>()->getPointeeType();
  } else {
    return ExprError(Diag(LLoc, diag::err_typecheck_subscript_value)
       << LHSExp->getSourceRange() << RHSExp->getSourceRange());
  }
  // C99 6.5.2.1p1
  if (!IndexExpr->getType()->isIntegerType() && !IndexExpr->isTypeDependent())
    return ExprError(Diag(LLoc, diag::err_typecheck_subscript_not_integer)
                     << IndexExpr->getSourceRange());

  if ((IndexExpr->getType()->isSpecificBuiltinType(BuiltinType::Char_S) ||
       IndexExpr->getType()->isSpecificBuiltinType(BuiltinType::Char_U)) &&
      !IndexExpr->isTypeDependent()) {
    std::optional<llvm::APSInt> IntegerContantExpr =
        IndexExpr->getIntegerConstantExpr(getASTContext());
    if (!IntegerContantExpr.has_value() ||
        IntegerContantExpr.value().isNegative())
      Diag(LLoc, diag::warn_subscript_is_char) << IndexExpr->getSourceRange();
  }

  // C99 6.5.2.1p1: "shall have type "pointer to *object* type". Similarly,
  // C++ [expr.sub]p1: The type "T" shall be a completely-defined object
  // type. Note that Functions are not objects, and that (in C99 parlance)
  // incomplete types are not object types.
  if (ResultType->isFunctionType()) {
    Diag(BaseExpr->getBeginLoc(), diag::err_subscript_function_type)
        << ResultType << BaseExpr->getSourceRange();
    return ExprError();
  }

  if (ResultType->isVoidType() && !getLangOpts().CPlusPlus) {
    // GNU extension: subscripting on pointer to void
    Diag(LLoc, diag::ext_gnu_subscript_void_type)
      << BaseExpr->getSourceRange();

    // C forbids expressions of unqualified void type from being l-values.
    // See IsCForbiddenLValueType.
    if (!ResultType.hasQualifiers())
      VK = VK_PRValue;
  } else if (!ResultType->isDependentType() &&
             !ResultType.isWebAssemblyReferenceType() &&
             RequireCompleteSizedType(
                 LLoc, ResultType,
                 diag::err_subscript_incomplete_or_sizeless_type, BaseExpr))
    return ExprError();

  assert(VK == VK_PRValue || LangOpts.CPlusPlus ||
         !ResultType.isCForbiddenLValueType());

  if (LHSExp->IgnoreParenImpCasts()->getType()->isVariablyModifiedType() &&
      FunctionScopes.size() > 1) {
    if (auto *TT =
            LHSExp->IgnoreParenImpCasts()->getType()->getAs<TypedefType>()) {
      for (auto I = FunctionScopes.rbegin(),
                E = std::prev(FunctionScopes.rend());
           I != E; ++I) {
        auto *CSI = dyn_cast<CapturingScopeInfo>(*I);
        if (CSI == nullptr)
          break;
        DeclContext *DC = nullptr;
        if (auto *LSI = dyn_cast<LambdaScopeInfo>(CSI))
          DC = LSI->CallOperator;
        else if (auto *CRSI = dyn_cast<CapturedRegionScopeInfo>(CSI))
          DC = CRSI->TheCapturedDecl;
        else if (auto *BSI = dyn_cast<BlockScopeInfo>(CSI))
          DC = BSI->TheDecl;
        if (DC) {
          if (DC->containsDecl(TT->getDecl()))
            break;
          captureVariablyModifiedType(
              Context, LHSExp->IgnoreParenImpCasts()->getType(), CSI);
        }
      }
    }
  }

  return new (Context)
      ArraySubscriptExpr(LHSExp, RHSExp, ResultType, VK, OK, RLoc);
}

bool Sema::CheckCXXDefaultArgExpr(SourceLocation CallLoc, FunctionDecl *FD,
                                  ParmVarDecl *Param, Expr *RewrittenInit,
                                  bool SkipImmediateInvocations) {
  if (Param->hasUnparsedDefaultArg()) {
    assert(!RewrittenInit && "Should not have a rewritten init expression yet");
    // If we've already cleared out the location for the default argument,
    // that means we're parsing it right now.
    if (!UnparsedDefaultArgLocs.count(Param)) {
      Diag(Param->getBeginLoc(), diag::err_recursive_default_argument) << FD;
      Diag(CallLoc, diag::note_recursive_default_argument_used_here);
      Param->setInvalidDecl();
      return true;
    }

    Diag(CallLoc, diag::err_use_of_default_argument_to_function_declared_later)
        << FD << cast<CXXRecordDecl>(FD->getDeclContext());
    Diag(UnparsedDefaultArgLocs[Param],
         diag::note_default_argument_declared_here);
    return true;
  }

  if (Param->hasUninstantiatedDefaultArg()) {
    assert(!RewrittenInit && "Should not have a rewitten init expression yet");
    if (InstantiateDefaultArgument(CallLoc, FD, Param))
      return true;
  }

  Expr *Init = RewrittenInit ? RewrittenInit : Param->getInit();
  assert(Init && "default argument but no initializer?");

  // If the default expression creates temporaries, we need to
  // push them to the current stack of expression temporaries so they'll
  // be properly destroyed.
  // FIXME: We should really be rebuilding the default argument with new
  // bound temporaries; see the comment in PR5810.
  // We don't need to do that with block decls, though, because
  // blocks in default argument expression can never capture anything.
  if (auto *InitWithCleanup = dyn_cast<ExprWithCleanups>(Init)) {
    // Set the "needs cleanups" bit regardless of whether there are
    // any explicit objects.
    Cleanup.setExprNeedsCleanups(InitWithCleanup->cleanupsHaveSideEffects());
    // Append all the objects to the cleanup list.  Right now, this
    // should always be a no-op, because blocks in default argument
    // expressions should never be able to capture anything.
    assert(!InitWithCleanup->getNumObjects() &&
           "default argument expression has capturing blocks?");
  }
  // C++ [expr.const]p15.1:
  //   An expression or conversion is in an immediate function context if it is
  //   potentially evaluated and [...] its innermost enclosing non-block scope
  //   is a function parameter scope of an immediate function.
  EnterExpressionEvaluationContext EvalContext(
      *this,
      FD->isImmediateFunction()
          ? ExpressionEvaluationContext::ImmediateFunctionContext
          : ExpressionEvaluationContext::PotentiallyEvaluated,
      Param);
  ExprEvalContexts.back().IsCurrentlyCheckingDefaultArgumentOrInitializer =
      SkipImmediateInvocations;
  runWithSufficientStackSpace(CallLoc, [&] {
    MarkDeclarationsReferencedInExpr(Init, /*SkipLocalVariables=*/true);
  });
  return false;
}

struct ImmediateCallVisitor : public RecursiveASTVisitor<ImmediateCallVisitor> {
  const ASTContext &Context;
  ImmediateCallVisitor(const ASTContext &Ctx) : Context(Ctx) {}

  bool HasImmediateCalls = false;
  bool shouldVisitImplicitCode() const { return true; }

  bool VisitCallExpr(CallExpr *E) {
    if (const FunctionDecl *FD = E->getDirectCallee())
      HasImmediateCalls |= FD->isImmediateFunction();
    return RecursiveASTVisitor<ImmediateCallVisitor>::VisitStmt(E);
  }

  bool VisitCXXConstructExpr(CXXConstructExpr *E) {
    if (const FunctionDecl *FD = E->getConstructor())
      HasImmediateCalls |= FD->isImmediateFunction();
    return RecursiveASTVisitor<ImmediateCallVisitor>::VisitStmt(E);
  }

  // SourceLocExpr are not immediate invocations
  // but CXXDefaultInitExpr/CXXDefaultArgExpr containing a SourceLocExpr
  // need to be rebuilt so that they refer to the correct SourceLocation and
  // DeclContext.
  bool VisitSourceLocExpr(SourceLocExpr *E) {
    HasImmediateCalls = true;
    return RecursiveASTVisitor<ImmediateCallVisitor>::VisitStmt(E);
  }

  // A nested lambda might have parameters with immediate invocations
  // in their default arguments.
  // The compound statement is not visited (as it does not constitute a
  // subexpression).
  // FIXME: We should consider visiting and transforming captures
  // with init expressions.
  bool VisitLambdaExpr(LambdaExpr *E) {
    return VisitCXXMethodDecl(E->getCallOperator());
  }

  bool VisitCXXDefaultArgExpr(CXXDefaultArgExpr *E) {
    return TraverseStmt(E->getExpr());
  }

  bool VisitCXXDefaultInitExpr(CXXDefaultInitExpr *E) {
    return TraverseStmt(E->getExpr());
  }
};

struct EnsureImmediateInvocationInDefaultArgs
    : TreeTransform<EnsureImmediateInvocationInDefaultArgs> {
  EnsureImmediateInvocationInDefaultArgs(Sema &SemaRef)
      : TreeTransform(SemaRef) {}

  // Lambda can only have immediate invocations in the default
  // args of their parameters, which is transformed upon calling the closure.
  // The body is not a subexpression, so we have nothing to do.
  // FIXME: Immediate calls in capture initializers should be transformed.
  ExprResult TransformLambdaExpr(LambdaExpr *E) { return E; }
  ExprResult TransformBlockExpr(BlockExpr *E) { return E; }

  // Make sure we don't rebuild the this pointer as it would
  // cause it to incorrectly point it to the outermost class
  // in the case of nested struct initialization.
  ExprResult TransformCXXThisExpr(CXXThisExpr *E) { return E; }

  // Rewrite to source location to refer to the context in which they are used.
  ExprResult TransformSourceLocExpr(SourceLocExpr *E) {
    DeclContext *DC = E->getParentContext();
    if (DC == SemaRef.CurContext)
      return E;

    // FIXME: During instantiation, because the rebuild of defaults arguments
    // is not always done in the context of the template instantiator,
    // we run the risk of producing a dependent source location
    // that would never be rebuilt.
    // This usually happens during overload resolution, or in contexts
    // where the value of the source location does not matter.
    // However, we should find a better way to deal with source location
    // of function templates.
    if (!SemaRef.CurrentInstantiationScope ||
        !SemaRef.CurContext->isDependentContext() || DC->isDependentContext())
      DC = SemaRef.CurContext;

    return getDerived().RebuildSourceLocExpr(
        E->getIdentKind(), E->getType(), E->getBeginLoc(), E->getEndLoc(), DC);
  }
};

ExprResult Sema::BuildCXXDefaultArgExpr(SourceLocation CallLoc,
                                        FunctionDecl *FD, ParmVarDecl *Param,
                                        Expr *Init) {
  assert(Param->hasDefaultArg() && "can't build nonexistent default arg");

  bool NestedDefaultChecking = isCheckingDefaultArgumentOrInitializer();
  bool InLifetimeExtendingContext = isInLifetimeExtendingContext();
  std::optional<ExpressionEvaluationContextRecord::InitializationContext>
      InitializationContext =
          OutermostDeclarationWithDelayedImmediateInvocations();
  if (!InitializationContext.has_value())
    InitializationContext.emplace(CallLoc, Param, CurContext);

  if (!Init && !Param->hasUnparsedDefaultArg()) {
    // Mark that we are replacing a default argument first.
    // If we are instantiating a template we won't have to
    // retransform immediate calls.
    // C++ [expr.const]p15.1:
    //   An expression or conversion is in an immediate function context if it
    //   is potentially evaluated and [...] its innermost enclosing non-block
    //   scope is a function parameter scope of an immediate function.
    EnterExpressionEvaluationContext EvalContext(
        *this,
        FD->isImmediateFunction()
            ? ExpressionEvaluationContext::ImmediateFunctionContext
            : ExpressionEvaluationContext::PotentiallyEvaluated,
        Param);

    if (Param->hasUninstantiatedDefaultArg()) {
      if (InstantiateDefaultArgument(CallLoc, FD, Param))
        return ExprError();
    }
    // CWG2631
    // An immediate invocation that is not evaluated where it appears is
    // evaluated and checked for whether it is a constant expression at the
    // point where the enclosing initializer is used in a function call.
    ImmediateCallVisitor V(getASTContext());
    if (!NestedDefaultChecking)
      V.TraverseDecl(Param);

    // Rewrite the call argument that was created from the corresponding
    // parameter's default argument.
    if (V.HasImmediateCalls || InLifetimeExtendingContext) {
      if (V.HasImmediateCalls)
        ExprEvalContexts.back().DelayedDefaultInitializationContext = {
            CallLoc, Param, CurContext};
      // Pass down lifetime extending flag, and collect temporaries in
      // CreateMaterializeTemporaryExpr when we rewrite the call argument.
      keepInLifetimeExtendingContext();
      EnsureImmediateInvocationInDefaultArgs Immediate(*this);
      ExprResult Res;
      runWithSufficientStackSpace(CallLoc, [&] {
        Res = Immediate.TransformInitializer(Param->getInit(),
                                             /*NotCopy=*/false);
      });
      if (Res.isInvalid())
        return ExprError();
      Res = ConvertParamDefaultArgument(Param, Res.get(),
                                        Res.get()->getBeginLoc());
      if (Res.isInvalid())
        return ExprError();
      Init = Res.get();
    }
  }

  if (CheckCXXDefaultArgExpr(
          CallLoc, FD, Param, Init,
          /*SkipImmediateInvocations=*/NestedDefaultChecking))
    return ExprError();

  return CXXDefaultArgExpr::Create(Context, InitializationContext->Loc, Param,
                                   Init, InitializationContext->Context);
}

ExprResult Sema::BuildCXXDefaultInitExpr(SourceLocation Loc, FieldDecl *Field) {
  assert(Field->hasInClassInitializer());

  // If we might have already tried and failed to instantiate, don't try again.
  if (Field->isInvalidDecl())
    return ExprError();

  CXXThisScopeRAII This(*this, Field->getParent(), Qualifiers());

  auto *ParentRD = cast<CXXRecordDecl>(Field->getParent());

  std::optional<ExpressionEvaluationContextRecord::InitializationContext>
      InitializationContext =
          OutermostDeclarationWithDelayedImmediateInvocations();
  if (!InitializationContext.has_value())
    InitializationContext.emplace(Loc, Field, CurContext);

  Expr *Init = nullptr;

  bool NestedDefaultChecking = isCheckingDefaultArgumentOrInitializer();

  EnterExpressionEvaluationContext EvalContext(
      *this, ExpressionEvaluationContext::PotentiallyEvaluated, Field);

  if (!Field->getInClassInitializer()) {
    // Maybe we haven't instantiated the in-class initializer. Go check the
    // pattern FieldDecl to see if it has one.
    if (isTemplateInstantiation(ParentRD->getTemplateSpecializationKind())) {
      CXXRecordDecl *ClassPattern = ParentRD->getTemplateInstantiationPattern();
      DeclContext::lookup_result Lookup =
          ClassPattern->lookup(Field->getDeclName());

      FieldDecl *Pattern = nullptr;
      for (auto *L : Lookup) {
        if ((Pattern = dyn_cast<FieldDecl>(L)))
          break;
      }
      assert(Pattern && "We must have set the Pattern!");
      if (!Pattern->hasInClassInitializer() ||
          InstantiateInClassInitializer(Loc, Field, Pattern,
                                        getTemplateInstantiationArgs(Field))) {
        Field->setInvalidDecl();
        return ExprError();
      }
    }
  }

  // CWG2631
  // An immediate invocation that is not evaluated where it appears is
  // evaluated and checked for whether it is a constant expression at the
  // point where the enclosing initializer is used in a [...] a constructor
  // definition, or an aggregate initialization.
  ImmediateCallVisitor V(getASTContext());
  if (!NestedDefaultChecking)
    V.TraverseDecl(Field);
  if (V.HasImmediateCalls) {
    ExprEvalContexts.back().DelayedDefaultInitializationContext = {Loc, Field,
                                                                   CurContext};
    ExprEvalContexts.back().IsCurrentlyCheckingDefaultArgumentOrInitializer =
        NestedDefaultChecking;

    EnsureImmediateInvocationInDefaultArgs Immediate(*this);
    ExprResult Res;
    runWithSufficientStackSpace(Loc, [&] {
      Res = Immediate.TransformInitializer(Field->getInClassInitializer(),
                                           /*CXXDirectInit=*/false);
    });
    if (!Res.isInvalid())
      Res = ConvertMemberDefaultInitExpression(Field, Res.get(), Loc);
    if (Res.isInvalid()) {
      Field->setInvalidDecl();
      return ExprError();
    }
    Init = Res.get();
  }

  if (Field->getInClassInitializer()) {
    Expr *E = Init ? Init : Field->getInClassInitializer();
    if (!NestedDefaultChecking)
      runWithSufficientStackSpace(Loc, [&] {
        MarkDeclarationsReferencedInExpr(E, /*SkipLocalVariables=*/false);
      });
    // C++11 [class.base.init]p7:
    //   The initialization of each base and member constitutes a
    //   full-expression.
    ExprResult Res = ActOnFinishFullExpr(E, /*DiscardedValue=*/false);
    if (Res.isInvalid()) {
      Field->setInvalidDecl();
      return ExprError();
    }
    Init = Res.get();

    return CXXDefaultInitExpr::Create(Context, InitializationContext->Loc,
                                      Field, InitializationContext->Context,
                                      Init);
  }

  // DR1351:
  //   If the brace-or-equal-initializer of a non-static data member
  //   invokes a defaulted default constructor of its class or of an
  //   enclosing class in a potentially evaluated subexpression, the
  //   program is ill-formed.
  //
  // This resolution is unworkable: the exception specification of the
  // default constructor can be needed in an unevaluated context, in
  // particular, in the operand of a noexcept-expression, and we can be
  // unable to compute an exception specification for an enclosed class.
  //
  // Any attempt to resolve the exception specification of a defaulted default
  // constructor before the initializer is lexically complete will ultimately
  // come here at which point we can diagnose it.
  RecordDecl *OutermostClass = ParentRD->getOuterLexicalRecordContext();
  Diag(Loc, diag::err_default_member_initializer_not_yet_parsed)
      << OutermostClass << Field;
  Diag(Field->getEndLoc(),
       diag::note_default_member_initializer_not_yet_parsed);
  // Recover by marking the field invalid, unless we're in a SFINAE context.
  if (!isSFINAEContext())
    Field->setInvalidDecl();
  return ExprError();
}

Sema::VariadicCallType
Sema::getVariadicCallType(FunctionDecl *FDecl, const FunctionProtoType *Proto,
                          Expr *Fn) {
  if (Proto && Proto->isVariadic()) {
    if (isa_and_nonnull<CXXConstructorDecl>(FDecl))
      return VariadicConstructor;
    else if (Fn && Fn->getType()->isBlockPointerType())
      return VariadicBlock;
    else if (FDecl) {
      if (CXXMethodDecl *Method = dyn_cast_or_null<CXXMethodDecl>(FDecl))
        if (Method->isInstance())
          return VariadicMethod;
    } else if (Fn && Fn->getType() == Context.BoundMemberTy)
      return VariadicMethod;
    return VariadicFunction;
  }
  return VariadicDoesNotApply;
}

namespace {
class FunctionCallCCC final : public FunctionCallFilterCCC {
public:
  FunctionCallCCC(Sema &SemaRef, const IdentifierInfo *FuncName,
                  unsigned NumArgs, MemberExpr *ME)
      : FunctionCallFilterCCC(SemaRef, NumArgs, false, ME),
        FunctionName(FuncName) {}

  bool ValidateCandidate(const TypoCorrection &candidate) override {
    if (!candidate.getCorrectionSpecifier() ||
        candidate.getCorrectionAsIdentifierInfo() != FunctionName) {
      return false;
    }

    return FunctionCallFilterCCC::ValidateCandidate(candidate);
  }

  std::unique_ptr<CorrectionCandidateCallback> clone() override {
    return std::make_unique<FunctionCallCCC>(*this);
  }

private:
  const IdentifierInfo *const FunctionName;
};
}

static TypoCorrection TryTypoCorrectionForCall(Sema &S, Expr *Fn,
                                               FunctionDecl *FDecl,
                                               ArrayRef<Expr *> Args) {
  MemberExpr *ME = dyn_cast<MemberExpr>(Fn);
  DeclarationName FuncName = FDecl->getDeclName();
  SourceLocation NameLoc = ME ? ME->getMemberLoc() : Fn->getBeginLoc();

  FunctionCallCCC CCC(S, FuncName.getAsIdentifierInfo(), Args.size(), ME);
  if (TypoCorrection Corrected = S.CorrectTypo(
          DeclarationNameInfo(FuncName, NameLoc), Sema::LookupOrdinaryName,
          S.getScopeForContext(S.CurContext), nullptr, CCC,
          Sema::CTK_ErrorRecovery)) {
    if (NamedDecl *ND = Corrected.getFoundDecl()) {
      if (Corrected.isOverloaded()) {
        OverloadCandidateSet OCS(NameLoc, OverloadCandidateSet::CSK_Normal);
        OverloadCandidateSet::iterator Best;
        for (NamedDecl *CD : Corrected) {
          if (FunctionDecl *FD = dyn_cast<FunctionDecl>(CD))
            S.AddOverloadCandidate(FD, DeclAccessPair::make(FD, AS_none), Args,
                                   OCS);
        }
        switch (OCS.BestViableFunction(S, NameLoc, Best)) {
        case OR_Success:
          ND = Best->FoundDecl;
          Corrected.setCorrectionDecl(ND);
          break;
        default:
          break;
        }
      }
      ND = ND->getUnderlyingDecl();
      if (isa<ValueDecl>(ND) || isa<FunctionTemplateDecl>(ND))
        return Corrected;
    }
  }
  return TypoCorrection();
}

// [C++26][[expr.unary.op]/p4
// A pointer to member is only formed when an explicit &
// is used and its operand is a qualified-id not enclosed in parentheses.
static bool isParenthetizedAndQualifiedAddressOfExpr(Expr *Fn) {
  if (!isa<ParenExpr>(Fn))
    return false;

  Fn = Fn->IgnoreParens();

  auto *UO = dyn_cast<UnaryOperator>(Fn);
  if (!UO || UO->getOpcode() != clang::UO_AddrOf)
    return false;
  if (auto *DRE = dyn_cast<DeclRefExpr>(UO->getSubExpr()->IgnoreParens())) {
    return DRE->hasQualifier();
  }
  if (auto *OVL = dyn_cast<OverloadExpr>(UO->getSubExpr()->IgnoreParens()))
    return OVL->getQualifier();
  return false;
}

bool
Sema::ConvertArgumentsForCall(CallExpr *Call, Expr *Fn,
                              FunctionDecl *FDecl,
                              const FunctionProtoType *Proto,
                              ArrayRef<Expr *> Args,
                              SourceLocation RParenLoc,
                              bool IsExecConfig) {
  // Bail out early if calling a builtin with custom typechecking.
  if (FDecl)
    if (unsigned ID = FDecl->getBuiltinID())
      if (Context.BuiltinInfo.hasCustomTypechecking(ID))
        return false;

  // C99 6.5.2.2p7 - the arguments are implicitly converted, as if by
  // assignment, to the types of the corresponding parameter, ...

  bool AddressOf = isParenthetizedAndQualifiedAddressOfExpr(Fn);
  bool HasExplicitObjectParameter =
      !AddressOf && FDecl && FDecl->hasCXXExplicitFunctionObjectParameter();
  unsigned ExplicitObjectParameterOffset = HasExplicitObjectParameter ? 1 : 0;
  unsigned NumParams = Proto->getNumParams();
  bool Invalid = false;
  unsigned MinArgs = FDecl ? FDecl->getMinRequiredArguments() : NumParams;
  unsigned FnKind = Fn->getType()->isBlockPointerType()
                       ? 1 /* block */
                       : (IsExecConfig ? 3 /* kernel function (exec config) */
                                       : 0 /* function */);

  // If too few arguments are available (and we don't have default
  // arguments for the remaining parameters), don't make the call.
  if (Args.size() < NumParams) {
    if (Args.size() < MinArgs) {
      TypoCorrection TC;
      if (FDecl && (TC = TryTypoCorrectionForCall(*this, Fn, FDecl, Args))) {
        unsigned diag_id =
            MinArgs == NumParams && !Proto->isVariadic()
                ? diag::err_typecheck_call_too_few_args_suggest
                : diag::err_typecheck_call_too_few_args_at_least_suggest;
        diagnoseTypo(
            TC, PDiag(diag_id)
                    << FnKind << MinArgs - ExplicitObjectParameterOffset
                    << static_cast<unsigned>(Args.size()) -
                           ExplicitObjectParameterOffset
                    << HasExplicitObjectParameter << TC.getCorrectionRange());
      } else if (MinArgs - ExplicitObjectParameterOffset == 1 && FDecl &&
                 FDecl->getParamDecl(ExplicitObjectParameterOffset)
                     ->getDeclName())
        Diag(RParenLoc,
             MinArgs == NumParams && !Proto->isVariadic()
                 ? diag::err_typecheck_call_too_few_args_one
                 : diag::err_typecheck_call_too_few_args_at_least_one)
            << FnKind << FDecl->getParamDecl(ExplicitObjectParameterOffset)
            << HasExplicitObjectParameter << Fn->getSourceRange();
      else
        Diag(RParenLoc, MinArgs == NumParams && !Proto->isVariadic()
                            ? diag::err_typecheck_call_too_few_args
                            : diag::err_typecheck_call_too_few_args_at_least)
            << FnKind << MinArgs - ExplicitObjectParameterOffset
            << static_cast<unsigned>(Args.size()) -
                   ExplicitObjectParameterOffset
            << HasExplicitObjectParameter << Fn->getSourceRange();

      // Emit the location of the prototype.
      if (!TC && FDecl && !FDecl->getBuiltinID() && !IsExecConfig)
        Diag(FDecl->getLocation(), diag::note_callee_decl)
            << FDecl << FDecl->getParametersSourceRange();

      return true;
    }
    // We reserve space for the default arguments when we create
    // the call expression, before calling ConvertArgumentsForCall.
    assert((Call->getNumArgs() == NumParams) &&
           "We should have reserved space for the default arguments before!");
  }

  // If too many are passed and not variadic, error on the extras and drop
  // them.
  if (Args.size() > NumParams) {
    if (!Proto->isVariadic()) {
      TypoCorrection TC;
      if (FDecl && (TC = TryTypoCorrectionForCall(*this, Fn, FDecl, Args))) {
        unsigned diag_id =
            MinArgs == NumParams && !Proto->isVariadic()
                ? diag::err_typecheck_call_too_many_args_suggest
                : diag::err_typecheck_call_too_many_args_at_most_suggest;
        diagnoseTypo(
            TC, PDiag(diag_id)
                    << FnKind << NumParams - ExplicitObjectParameterOffset
                    << static_cast<unsigned>(Args.size()) -
                           ExplicitObjectParameterOffset
                    << HasExplicitObjectParameter << TC.getCorrectionRange());
      } else if (NumParams - ExplicitObjectParameterOffset == 1 && FDecl &&
                 FDecl->getParamDecl(ExplicitObjectParameterOffset)
                     ->getDeclName())
        Diag(Args[NumParams]->getBeginLoc(),
             MinArgs == NumParams
                 ? diag::err_typecheck_call_too_many_args_one
                 : diag::err_typecheck_call_too_many_args_at_most_one)
            << FnKind << FDecl->getParamDecl(ExplicitObjectParameterOffset)
            << static_cast<unsigned>(Args.size()) -
                   ExplicitObjectParameterOffset
            << HasExplicitObjectParameter << Fn->getSourceRange()
            << SourceRange(Args[NumParams]->getBeginLoc(),
                           Args.back()->getEndLoc());
      else
        Diag(Args[NumParams]->getBeginLoc(),
             MinArgs == NumParams
                 ? diag::err_typecheck_call_too_many_args
                 : diag::err_typecheck_call_too_many_args_at_most)
            << FnKind << NumParams - ExplicitObjectParameterOffset
            << static_cast<unsigned>(Args.size()) -
                   ExplicitObjectParameterOffset
            << HasExplicitObjectParameter << Fn->getSourceRange()
            << SourceRange(Args[NumParams]->getBeginLoc(),
                           Args.back()->getEndLoc());

      // Emit the location of the prototype.
      if (!TC && FDecl && !FDecl->getBuiltinID() && !IsExecConfig)
        Diag(FDecl->getLocation(), diag::note_callee_decl)
            << FDecl << FDecl->getParametersSourceRange();

      // This deletes the extra arguments.
      Call->shrinkNumArgs(NumParams);
      return true;
    }
  }
  SmallVector<Expr *, 8> AllArgs;
  VariadicCallType CallType = getVariadicCallType(FDecl, Proto, Fn);

  Invalid = GatherArgumentsForCall(Call->getBeginLoc(), FDecl, Proto, 0, Args,
                                   AllArgs, CallType);
  if (Invalid)
    return true;
  unsigned TotalNumArgs = AllArgs.size();
  for (unsigned i = 0; i < TotalNumArgs; ++i)
    Call->setArg(i, AllArgs[i]);

  Call->computeDependence();
  return false;
}

bool Sema::GatherArgumentsForCall(SourceLocation CallLoc, FunctionDecl *FDecl,
                                  const FunctionProtoType *Proto,
                                  unsigned FirstParam, ArrayRef<Expr *> Args,
                                  SmallVectorImpl<Expr *> &AllArgs,
                                  VariadicCallType CallType, bool AllowExplicit,
                                  bool IsListInitialization) {
  unsigned NumParams = Proto->getNumParams();
  bool Invalid = false;
  size_t ArgIx = 0;
  // Continue to check argument types (even if we have too few/many args).
  for (unsigned i = FirstParam; i < NumParams; i++) {
    QualType ProtoArgType = Proto->getParamType(i);

    Expr *Arg;
    ParmVarDecl *Param = FDecl ? FDecl->getParamDecl(i) : nullptr;
    if (ArgIx < Args.size()) {
      Arg = Args[ArgIx++];

      if (RequireCompleteType(Arg->getBeginLoc(), ProtoArgType,
                              diag::err_call_incomplete_argument, Arg))
        return true;

      // Strip the unbridged-cast placeholder expression off, if applicable.
      bool CFAudited = false;
      if (Arg->getType() == Context.ARCUnbridgedCastTy &&
          FDecl && FDecl->hasAttr<CFAuditedTransferAttr>() &&
          (!Param || !Param->hasAttr<CFConsumedAttr>()))
        Arg = ObjC().stripARCUnbridgedCast(Arg);
      else if (getLangOpts().ObjCAutoRefCount &&
               FDecl && FDecl->hasAttr<CFAuditedTransferAttr>() &&
               (!Param || !Param->hasAttr<CFConsumedAttr>()))
        CFAudited = true;

      if (Proto->getExtParameterInfo(i).isNoEscape() &&
          ProtoArgType->isBlockPointerType())
        if (auto *BE = dyn_cast<BlockExpr>(Arg->IgnoreParenNoopCasts(Context)))
          BE->getBlockDecl()->setDoesNotEscape();

      InitializedEntity Entity =
          Param ? InitializedEntity::InitializeParameter(Context, Param,
                                                         ProtoArgType)
                : InitializedEntity::InitializeParameter(
                      Context, ProtoArgType, Proto->isParamConsumed(i));

      // Remember that parameter belongs to a CF audited API.
      if (CFAudited)
        Entity.setParameterCFAudited();

      ExprResult ArgE = PerformCopyInitialization(
          Entity, SourceLocation(), Arg, IsListInitialization, AllowExplicit);
      if (ArgE.isInvalid())
        return true;

      Arg = ArgE.getAs<Expr>();
    } else {
      assert(Param && "can't use default arguments without a known callee");

      ExprResult ArgExpr = BuildCXXDefaultArgExpr(CallLoc, FDecl, Param);
      if (ArgExpr.isInvalid())
        return true;

      Arg = ArgExpr.getAs<Expr>();
    }

    // Check for array bounds violations for each argument to the call. This
    // check only triggers warnings when the argument isn't a more complex Expr
    // with its own checking, such as a BinaryOperator.
    CheckArrayAccess(Arg);

    // Check for violations of C99 static array rules (C99 6.7.5.3p7).
    CheckStaticArrayArgument(CallLoc, Param, Arg);

    AllArgs.push_back(Arg);
  }

  // If this is a variadic call, handle args passed through "...".
  if (CallType != VariadicDoesNotApply) {
    // Assume that extern "C" functions with variadic arguments that
    // return __unknown_anytype aren't *really* variadic.
    if (Proto->getReturnType() == Context.UnknownAnyTy && FDecl &&
        FDecl->isExternC()) {
      for (Expr *A : Args.slice(ArgIx)) {
        QualType paramType; // ignored
        ExprResult arg = checkUnknownAnyArg(CallLoc, A, paramType);
        Invalid |= arg.isInvalid();
        AllArgs.push_back(arg.get());
      }

    // Otherwise do argument promotion, (C99 6.5.2.2p7).
    } else {
      for (Expr *A : Args.slice(ArgIx)) {
        ExprResult Arg = DefaultVariadicArgumentPromotion(A, CallType, FDecl);
        Invalid |= Arg.isInvalid();
        AllArgs.push_back(Arg.get());
      }
    }

    // Check for array bounds violations.
    for (Expr *A : Args.slice(ArgIx))
      CheckArrayAccess(A);
  }
  return Invalid;
}

static void DiagnoseCalleeStaticArrayParam(Sema &S, ParmVarDecl *PVD) {
  TypeLoc TL = PVD->getTypeSourceInfo()->getTypeLoc();
  if (DecayedTypeLoc DTL = TL.getAs<DecayedTypeLoc>())
    TL = DTL.getOriginalLoc();
  if (ArrayTypeLoc ATL = TL.getAs<ArrayTypeLoc>())
    S.Diag(PVD->getLocation(), diag::note_callee_static_array)
      << ATL.getLocalSourceRange();
}

void
Sema::CheckStaticArrayArgument(SourceLocation CallLoc,
                               ParmVarDecl *Param,
                               const Expr *ArgExpr) {
  // Static array parameters are not supported in C++.
  if (!Param || getLangOpts().CPlusPlus)
    return;

  QualType OrigTy = Param->getOriginalType();

  const ArrayType *AT = Context.getAsArrayType(OrigTy);
  if (!AT || AT->getSizeModifier() != ArraySizeModifier::Static)
    return;

  if (ArgExpr->isNullPointerConstant(Context,
                                     Expr::NPC_NeverValueDependent)) {
    Diag(CallLoc, diag::warn_null_arg) << ArgExpr->getSourceRange();
    DiagnoseCalleeStaticArrayParam(*this, Param);
    return;
  }

  const ConstantArrayType *CAT = dyn_cast<ConstantArrayType>(AT);
  if (!CAT)
    return;

  const ConstantArrayType *ArgCAT =
    Context.getAsConstantArrayType(ArgExpr->IgnoreParenCasts()->getType());
  if (!ArgCAT)
    return;

  if (getASTContext().hasSameUnqualifiedType(CAT->getElementType(),
                                             ArgCAT->getElementType())) {
    if (ArgCAT->getSize().ult(CAT->getSize())) {
      Diag(CallLoc, diag::warn_static_array_too_small)
          << ArgExpr->getSourceRange() << (unsigned)ArgCAT->getZExtSize()
          << (unsigned)CAT->getZExtSize() << 0;
      DiagnoseCalleeStaticArrayParam(*this, Param);
    }
    return;
  }

  std::optional<CharUnits> ArgSize =
      getASTContext().getTypeSizeInCharsIfKnown(ArgCAT);
  std::optional<CharUnits> ParmSize =
      getASTContext().getTypeSizeInCharsIfKnown(CAT);
  if (ArgSize && ParmSize && *ArgSize < *ParmSize) {
    Diag(CallLoc, diag::warn_static_array_too_small)
        << ArgExpr->getSourceRange() << (unsigned)ArgSize->getQuantity()
        << (unsigned)ParmSize->getQuantity() << 1;
    DiagnoseCalleeStaticArrayParam(*this, Param);
  }
}

/// Given a function expression of unknown-any type, try to rebuild it
/// to have a function type.
static ExprResult rebuildUnknownAnyFunction(Sema &S, Expr *fn);

/// Is the given type a placeholder that we need to lower out
/// immediately during argument processing?
static bool isPlaceholderToRemoveAsArg(QualType type) {
  // Placeholders are never sugared.
  const BuiltinType *placeholder = dyn_cast<BuiltinType>(type);
  if (!placeholder) return false;

  switch (placeholder->getKind()) {
  // Ignore all the non-placeholder types.
#define IMAGE_TYPE(ImgType, Id, SingletonId, Access, Suffix) \
  case BuiltinType::Id:
#include "clang/Basic/OpenCLImageTypes.def"
#define EXT_OPAQUE_TYPE(ExtType, Id, Ext) \
  case BuiltinType::Id:
#include "clang/Basic/OpenCLExtensionTypes.def"
  // In practice we'll never use this, since all SVE types are sugared
  // via TypedefTypes rather than exposed directly as BuiltinTypes.
#define SVE_TYPE(Name, Id, SingletonId) \
  case BuiltinType::Id:
#include "clang/Basic/AArch64SVEACLETypes.def"
#define PPC_VECTOR_TYPE(Name, Id, Size) \
  case BuiltinType::Id:
#include "clang/Basic/PPCTypes.def"
#define RVV_TYPE(Name, Id, SingletonId) case BuiltinType::Id:
#include "clang/Basic/RISCVVTypes.def"
#define WASM_TYPE(Name, Id, SingletonId) case BuiltinType::Id:
#include "clang/Basic/WebAssemblyReferenceTypes.def"
#define AMDGPU_TYPE(Name, Id, SingletonId) case BuiltinType::Id:
#include "clang/Basic/AMDGPUTypes.def"
#define PLACEHOLDER_TYPE(ID, SINGLETON_ID)
#define BUILTIN_TYPE(ID, SINGLETON_ID) case BuiltinType::ID:
#include "clang/AST/BuiltinTypes.def"
    return false;

  case BuiltinType::UnresolvedTemplate:
  // We cannot lower out overload sets; they might validly be resolved
  // by the call machinery.
  case BuiltinType::Overload:
    return false;

  // Unbridged casts in ARC can be handled in some call positions and
  // should be left in place.
  case BuiltinType::ARCUnbridgedCast:
    return false;

  // Pseudo-objects should be converted as soon as possible.
  case BuiltinType::PseudoObject:
    return true;

  // The debugger mode could theoretically but currently does not try
  // to resolve unknown-typed arguments based on known parameter types.
  case BuiltinType::UnknownAny:
    return true;

  // These are always invalid as call arguments and should be reported.
  case BuiltinType::BoundMember:
  case BuiltinType::BuiltinFn:
  case BuiltinType::IncompleteMatrixIdx:
  case BuiltinType::ArraySection:
  case BuiltinType::OMPArrayShaping:
  case BuiltinType::OMPIterator:
    return true;

  }
  llvm_unreachable("bad builtin type kind");
}

bool Sema::CheckArgsForPlaceholders(MultiExprArg args) {
  // Apply this processing to all the arguments at once instead of
  // dying at the first failure.
  bool hasInvalid = false;
  for (size_t i = 0, e = args.size(); i != e; i++) {
    if (isPlaceholderToRemoveAsArg(args[i]->getType())) {
      ExprResult result = CheckPlaceholderExpr(args[i]);
      if (result.isInvalid()) hasInvalid = true;
      else args[i] = result.get();
    }
  }
  return hasInvalid;
}

/// If a builtin function has a pointer argument with no explicit address
/// space, then it should be able to accept a pointer to any address
/// space as input.  In order to do this, we need to replace the
/// standard builtin declaration with one that uses the same address space
/// as the call.
///
/// \returns nullptr If this builtin is not a candidate for a rewrite i.e.
///                  it does not contain any pointer arguments without
///                  an address space qualifer.  Otherwise the rewritten
///                  FunctionDecl is returned.
/// TODO: Handle pointer return types.
static FunctionDecl *rewriteBuiltinFunctionDecl(Sema *Sema, ASTContext &Context,
                                                FunctionDecl *FDecl,
                                                MultiExprArg ArgExprs) {

  QualType DeclType = FDecl->getType();
  const FunctionProtoType *FT = dyn_cast<FunctionProtoType>(DeclType);

  if (!Context.BuiltinInfo.hasPtrArgsOrResult(FDecl->getBuiltinID()) || !FT ||
      ArgExprs.size() < FT->getNumParams())
    return nullptr;

  bool NeedsNewDecl = false;
  unsigned i = 0;
  SmallVector<QualType, 8> OverloadParams;

  for (QualType ParamType : FT->param_types()) {

    // Convert array arguments to pointer to simplify type lookup.
    ExprResult ArgRes =
        Sema->DefaultFunctionArrayLvalueConversion(ArgExprs[i++]);
    if (ArgRes.isInvalid())
      return nullptr;
    Expr *Arg = ArgRes.get();
    QualType ArgType = Arg->getType();
    if (!ParamType->isPointerType() || ParamType.hasAddressSpace() ||
        !ArgType->isPointerType() ||
        !ArgType->getPointeeType().hasAddressSpace() ||
        isPtrSizeAddressSpace(ArgType->getPointeeType().getAddressSpace())) {
      OverloadParams.push_back(ParamType);
      continue;
    }

    QualType PointeeType = ParamType->getPointeeType();
    if (PointeeType.hasAddressSpace())
      continue;

    NeedsNewDecl = true;
    LangAS AS = ArgType->getPointeeType().getAddressSpace();

    PointeeType = Context.getAddrSpaceQualType(PointeeType, AS);
    OverloadParams.push_back(Context.getPointerType(PointeeType));
  }

  if (!NeedsNewDecl)
    return nullptr;

  FunctionProtoType::ExtProtoInfo EPI;
  EPI.Variadic = FT->isVariadic();
  QualType OverloadTy = Context.getFunctionType(FT->getReturnType(),
                                                OverloadParams, EPI);
  DeclContext *Parent = FDecl->getParent();
  FunctionDecl *OverloadDecl = FunctionDecl::Create(
      Context, Parent, FDecl->getLocation(), FDecl->getLocation(),
      FDecl->getIdentifier(), OverloadTy,
      /*TInfo=*/nullptr, SC_Extern, Sema->getCurFPFeatures().isFPConstrained(),
      false,
      /*hasPrototype=*/true);
  SmallVector<ParmVarDecl*, 16> Params;
  FT = cast<FunctionProtoType>(OverloadTy);
  for (unsigned i = 0, e = FT->getNumParams(); i != e; ++i) {
    QualType ParamType = FT->getParamType(i);
    ParmVarDecl *Parm =
        ParmVarDecl::Create(Context, OverloadDecl, SourceLocation(),
                                SourceLocation(), nullptr, ParamType,
                                /*TInfo=*/nullptr, SC_None, nullptr);
    Parm->setScopeInfo(0, i);
    Params.push_back(Parm);
  }
  OverloadDecl->setParams(Params);
  Sema->mergeDeclAttributes(OverloadDecl, FDecl);
  return OverloadDecl;
}

static void checkDirectCallValidity(Sema &S, const Expr *Fn,
                                    FunctionDecl *Callee,
                                    MultiExprArg ArgExprs) {
  // `Callee` (when called with ArgExprs) may be ill-formed. enable_if (and
  // similar attributes) really don't like it when functions are called with an
  // invalid number of args.
  if (S.TooManyArguments(Callee->getNumParams(), ArgExprs.size(),
                         /*PartialOverloading=*/false) &&
      !Callee->isVariadic())
    return;
  if (Callee->getMinRequiredArguments() > ArgExprs.size())
    return;

  if (const EnableIfAttr *Attr =
          S.CheckEnableIf(Callee, Fn->getBeginLoc(), ArgExprs, true)) {
    S.Diag(Fn->getBeginLoc(),
           isa<CXXMethodDecl>(Callee)
               ? diag::err_ovl_no_viable_member_function_in_call
               : diag::err_ovl_no_viable_function_in_call)
        << Callee << Callee->getSourceRange();
    S.Diag(Callee->getLocation(),
           diag::note_ovl_candidate_disabled_by_function_cond_attr)
        << Attr->getCond()->getSourceRange() << Attr->getMessage();
    return;
  }
}

static bool enclosingClassIsRelatedToClassInWhichMembersWereFound(
    const UnresolvedMemberExpr *const UME, Sema &S) {

  const auto GetFunctionLevelDCIfCXXClass =
      [](Sema &S) -> const CXXRecordDecl * {
    const DeclContext *const DC = S.getFunctionLevelDeclContext();
    if (!DC || !DC->getParent())
      return nullptr;

    // If the call to some member function was made from within a member
    // function body 'M' return return 'M's parent.
    if (const auto *MD = dyn_cast<CXXMethodDecl>(DC))
      return MD->getParent()->getCanonicalDecl();
    // else the call was made from within a default member initializer of a
    // class, so return the class.
    if (const auto *RD = dyn_cast<CXXRecordDecl>(DC))
      return RD->getCanonicalDecl();
    return nullptr;
  };
  // If our DeclContext is neither a member function nor a class (in the
  // case of a lambda in a default member initializer), we can't have an
  // enclosing 'this'.

  const CXXRecordDecl *const CurParentClass = GetFunctionLevelDCIfCXXClass(S);
  if (!CurParentClass)
    return false;

  // The naming class for implicit member functions call is the class in which
  // name lookup starts.
  const CXXRecordDecl *const NamingClass =
      UME->getNamingClass()->getCanonicalDecl();
  assert(NamingClass && "Must have naming class even for implicit access");

  // If the unresolved member functions were found in a 'naming class' that is
  // related (either the same or derived from) to the class that contains the
  // member function that itself contained the implicit member access.

  return CurParentClass == NamingClass ||
         CurParentClass->isDerivedFrom(NamingClass);
}

static void
tryImplicitlyCaptureThisIfImplicitMemberFunctionAccessWithDependentArgs(
    Sema &S, const UnresolvedMemberExpr *const UME, SourceLocation CallLoc) {

  if (!UME)
    return;

  LambdaScopeInfo *const CurLSI = S.getCurLambda();
  // Only try and implicitly capture 'this' within a C++ Lambda if it hasn't
  // already been captured, or if this is an implicit member function call (if
  // it isn't, an attempt to capture 'this' should already have been made).
  if (!CurLSI || CurLSI->ImpCaptureStyle == CurLSI->ImpCap_None ||
      !UME->isImplicitAccess() || CurLSI->isCXXThisCaptured())
    return;

  // Check if the naming class in which the unresolved members were found is
  // related (same as or is a base of) to the enclosing class.

  if (!enclosingClassIsRelatedToClassInWhichMembersWereFound(UME, S))
    return;


  DeclContext *EnclosingFunctionCtx = S.CurContext->getParent()->getParent();
  // If the enclosing function is not dependent, then this lambda is
  // capture ready, so if we can capture this, do so.
  if (!EnclosingFunctionCtx->isDependentContext()) {
    // If the current lambda and all enclosing lambdas can capture 'this' -
    // then go ahead and capture 'this' (since our unresolved overload set
    // contains at least one non-static member function).
    if (!S.CheckCXXThisCapture(CallLoc, /*Explcit*/ false, /*Diagnose*/ false))
      S.CheckCXXThisCapture(CallLoc);
  } else if (S.CurContext->isDependentContext()) {
    // ... since this is an implicit member reference, that might potentially
    // involve a 'this' capture, mark 'this' for potential capture in
    // enclosing lambdas.
    if (CurLSI->ImpCaptureStyle != CurLSI->ImpCap_None)
      CurLSI->addPotentialThisCapture(CallLoc);
  }
}

// Once a call is fully resolved, warn for unqualified calls to specific
// C++ standard functions, like move and forward.
static void DiagnosedUnqualifiedCallsToStdFunctions(Sema &S,
                                                    const CallExpr *Call) {
  // We are only checking unary move and forward so exit early here.
  if (Call->getNumArgs() != 1)
    return;

  const Expr *E = Call->getCallee()->IgnoreParenImpCasts();
  if (!E || isa<UnresolvedLookupExpr>(E))
    return;
  const DeclRefExpr *DRE = dyn_cast_if_present<DeclRefExpr>(E);
  if (!DRE || !DRE->getLocation().isValid())
    return;

  if (DRE->getQualifier())
    return;

  const FunctionDecl *FD = Call->getDirectCallee();
  if (!FD)
    return;

  // Only warn for some functions deemed more frequent or problematic.
  unsigned BuiltinID = FD->getBuiltinID();
  if (BuiltinID != Builtin::BImove && BuiltinID != Builtin::BIforward)
    return;

  S.Diag(DRE->getLocation(), diag::warn_unqualified_call_to_std_cast_function)
      << FD->getQualifiedNameAsString()
      << FixItHint::CreateInsertion(DRE->getLocation(), "std::");
}

ExprResult Sema::ActOnCallExpr(Scope *Scope, Expr *Fn, SourceLocation LParenLoc,
                               MultiExprArg ArgExprs, SourceLocation RParenLoc,
                               Expr *ExecConfig) {
  ExprResult Call =
      BuildCallExpr(Scope, Fn, LParenLoc, ArgExprs, RParenLoc, ExecConfig,
                    /*IsExecConfig=*/false, /*AllowRecovery=*/true);
  if (Call.isInvalid())
    return Call;

  // Diagnose uses of the C++20 "ADL-only template-id call" feature in earlier
  // language modes.
  if (const auto *ULE = dyn_cast<UnresolvedLookupExpr>(Fn);
      ULE && ULE->hasExplicitTemplateArgs() &&
      ULE->decls_begin() == ULE->decls_end()) {
    Diag(Fn->getExprLoc(), getLangOpts().CPlusPlus20
                               ? diag::warn_cxx17_compat_adl_only_template_id
                               : diag::ext_adl_only_template_id)
        << ULE->getName();
  }

  if (LangOpts.OpenMP)
    Call = OpenMP().ActOnOpenMPCall(Call, Scope, LParenLoc, ArgExprs, RParenLoc,
                                    ExecConfig);
  if (LangOpts.CPlusPlus) {
    if (const auto *CE = dyn_cast<CallExpr>(Call.get()))
      DiagnosedUnqualifiedCallsToStdFunctions(*this, CE);

    // If we previously found that the id-expression of this call refers to a
    // consteval function but the call is dependent, we should not treat is an
    // an invalid immediate call.
    if (auto *DRE = dyn_cast<DeclRefExpr>(Fn->IgnoreParens());
        DRE && Call.get()->isValueDependent()) {
      currentEvaluationContext().ReferenceToConsteval.erase(DRE);
    }
  }
  return Call;
}

ExprResult Sema::BuildCallExpr(Scope *Scope, Expr *Fn, SourceLocation LParenLoc,
                               MultiExprArg ArgExprs, SourceLocation RParenLoc,
                               Expr *ExecConfig, bool IsExecConfig,
                               bool AllowRecovery) {
  // Since this might be a postfix expression, get rid of ParenListExprs.
  ExprResult Result = MaybeConvertParenListExprToParenExpr(Scope, Fn);
  if (Result.isInvalid()) return ExprError();
  Fn = Result.get();

  if (CheckArgsForPlaceholders(ArgExprs))
    return ExprError();

  if (getLangOpts().CPlusPlus) {
    // If this is a pseudo-destructor expression, build the call immediately.
    if (isa<CXXPseudoDestructorExpr>(Fn)) {
      if (!ArgExprs.empty()) {
        // Pseudo-destructor calls should not have any arguments.
        Diag(Fn->getBeginLoc(), diag::err_pseudo_dtor_call_with_args)
            << FixItHint::CreateRemoval(
                   SourceRange(ArgExprs.front()->getBeginLoc(),
                               ArgExprs.back()->getEndLoc()));
      }

      return CallExpr::Create(Context, Fn, /*Args=*/{}, Context.VoidTy,
                              VK_PRValue, RParenLoc, CurFPFeatureOverrides());
    }
    if (Fn->getType() == Context.PseudoObjectTy) {
      ExprResult result = CheckPlaceholderExpr(Fn);
      if (result.isInvalid()) return ExprError();
      Fn = result.get();
    }

    // Determine whether this is a dependent call inside a C++ template,
    // in which case we won't do any semantic analysis now.
    if (Fn->isTypeDependent() || Expr::hasAnyTypeDependentArguments(ArgExprs)) {
      if (ExecConfig) {
        return CUDAKernelCallExpr::Create(Context, Fn,
                                          cast<CallExpr>(ExecConfig), ArgExprs,
                                          Context.DependentTy, VK_PRValue,
                                          RParenLoc, CurFPFeatureOverrides());
      } else {

        tryImplicitlyCaptureThisIfImplicitMemberFunctionAccessWithDependentArgs(
            *this, dyn_cast<UnresolvedMemberExpr>(Fn->IgnoreParens()),
            Fn->getBeginLoc());

        return CallExpr::Create(Context, Fn, ArgExprs, Context.DependentTy,
                                VK_PRValue, RParenLoc, CurFPFeatureOverrides());
      }
    }

    // Determine whether this is a call to an object (C++ [over.call.object]).
    if (Fn->getType()->isRecordType())
      return BuildCallToObjectOfClassType(Scope, Fn, LParenLoc, ArgExprs,
                                          RParenLoc);

    if (Fn->getType() == Context.UnknownAnyTy) {
      ExprResult result = rebuildUnknownAnyFunction(*this, Fn);
      if (result.isInvalid()) return ExprError();
      Fn = result.get();
    }

    if (Fn->getType() == Context.BoundMemberTy) {
      return BuildCallToMemberFunction(Scope, Fn, LParenLoc, ArgExprs,
                                       RParenLoc, ExecConfig, IsExecConfig,
                                       AllowRecovery);
    }
  }

  // Check for overloaded calls.  This can happen even in C due to extensions.
  if (Fn->getType() == Context.OverloadTy) {
    OverloadExpr::FindResult find = OverloadExpr::find(Fn);

    // We aren't supposed to apply this logic if there's an '&' involved.
    if (!find.HasFormOfMemberPointer || find.IsAddressOfOperandWithParen) {
      if (Expr::hasAnyTypeDependentArguments(ArgExprs))
        return CallExpr::Create(Context, Fn, ArgExprs, Context.DependentTy,
                                VK_PRValue, RParenLoc, CurFPFeatureOverrides());
      OverloadExpr *ovl = find.Expression;
      if (UnresolvedLookupExpr *ULE = dyn_cast<UnresolvedLookupExpr>(ovl))
        return BuildOverloadedCallExpr(
            Scope, Fn, ULE, LParenLoc, ArgExprs, RParenLoc, ExecConfig,
            /*AllowTypoCorrection=*/true, find.IsAddressOfOperand);
      return BuildCallToMemberFunction(Scope, Fn, LParenLoc, ArgExprs,
                                       RParenLoc, ExecConfig, IsExecConfig,
                                       AllowRecovery);
    }
  }

  // If we're directly calling a function, get the appropriate declaration.
  if (Fn->getType() == Context.UnknownAnyTy) {
    ExprResult result = rebuildUnknownAnyFunction(*this, Fn);
    if (result.isInvalid()) return ExprError();
    Fn = result.get();
  }

  Expr *NakedFn = Fn->IgnoreParens();

  bool CallingNDeclIndirectly = false;
  NamedDecl *NDecl = nullptr;
  if (UnaryOperator *UnOp = dyn_cast<UnaryOperator>(NakedFn)) {
    if (UnOp->getOpcode() == UO_AddrOf) {
      CallingNDeclIndirectly = true;
      NakedFn = UnOp->getSubExpr()->IgnoreParens();
    }
  }

  if (auto *DRE = dyn_cast<DeclRefExpr>(NakedFn)) {
    NDecl = DRE->getDecl();

    FunctionDecl *FDecl = dyn_cast<FunctionDecl>(NDecl);
    if (FDecl && FDecl->getBuiltinID()) {
      // Rewrite the function decl for this builtin by replacing parameters
      // with no explicit address space with the address space of the arguments
      // in ArgExprs.
      if ((FDecl =
               rewriteBuiltinFunctionDecl(this, Context, FDecl, ArgExprs))) {
        NDecl = FDecl;
        Fn = DeclRefExpr::Create(
            Context, FDecl->getQualifierLoc(), SourceLocation(), FDecl, false,
            SourceLocation(), FDecl->getType(), Fn->getValueKind(), FDecl,
            nullptr, DRE->isNonOdrUse());
      }
    }
  } else if (auto *ME = dyn_cast<MemberExpr>(NakedFn))
    NDecl = ME->getMemberDecl();

  if (FunctionDecl *FD = dyn_cast_or_null<FunctionDecl>(NDecl)) {
    if (CallingNDeclIndirectly && !checkAddressOfFunctionIsAvailable(
                                      FD, /*Complain=*/true, Fn->getBeginLoc()))
      return ExprError();

    checkDirectCallValidity(*this, Fn, FD, ArgExprs);

    // If this expression is a call to a builtin function in HIP device
    // compilation, allow a pointer-type argument to default address space to be
    // passed as a pointer-type parameter to a non-default address space.
    // If Arg is declared in the default address space and Param is declared
    // in a non-default address space, perform an implicit address space cast to
    // the parameter type.
    if (getLangOpts().HIP && getLangOpts().CUDAIsDevice && FD &&
        FD->getBuiltinID()) {
      for (unsigned Idx = 0; Idx < ArgExprs.size() && Idx < FD->param_size();
          ++Idx) {
        ParmVarDecl *Param = FD->getParamDecl(Idx);
        if (!ArgExprs[Idx] || !Param || !Param->getType()->isPointerType() ||
            !ArgExprs[Idx]->getType()->isPointerType())
          continue;

        auto ParamAS = Param->getType()->getPointeeType().getAddressSpace();
        auto ArgTy = ArgExprs[Idx]->getType();
        auto ArgPtTy = ArgTy->getPointeeType();
        auto ArgAS = ArgPtTy.getAddressSpace();

        // Add address space cast if target address spaces are different
        bool NeedImplicitASC =
          ParamAS != LangAS::Default &&       // Pointer params in generic AS don't need special handling.
          ( ArgAS == LangAS::Default  ||      // We do allow implicit conversion from generic AS
                                              // or from specific AS which has target AS matching that of Param.
          getASTContext().getTargetAddressSpace(ArgAS) == getASTContext().getTargetAddressSpace(ParamAS));
        if (!NeedImplicitASC)
          continue;

        // First, ensure that the Arg is an RValue.
        if (ArgExprs[Idx]->isGLValue()) {
          ArgExprs[Idx] = ImplicitCastExpr::Create(
              Context, ArgExprs[Idx]->getType(), CK_NoOp, ArgExprs[Idx],
              nullptr, VK_PRValue, FPOptionsOverride());
        }

        // Construct a new arg type with address space of Param
        Qualifiers ArgPtQuals = ArgPtTy.getQualifiers();
        ArgPtQuals.setAddressSpace(ParamAS);
        auto NewArgPtTy =
            Context.getQualifiedType(ArgPtTy.getUnqualifiedType(), ArgPtQuals);
        auto NewArgTy =
            Context.getQualifiedType(Context.getPointerType(NewArgPtTy),
                                     ArgTy.getQualifiers());

        // Finally perform an implicit address space cast
        ArgExprs[Idx] = ImpCastExprToType(ArgExprs[Idx], NewArgTy,
                                          CK_AddressSpaceConversion)
                            .get();
      }
    }
  }

  if (Context.isDependenceAllowed() &&
      (Fn->isTypeDependent() || Expr::hasAnyTypeDependentArguments(ArgExprs))) {
    assert(!getLangOpts().CPlusPlus);
    assert((Fn->containsErrors() ||
            llvm::any_of(ArgExprs,
                         [](clang::Expr *E) { return E->containsErrors(); })) &&
           "should only occur in error-recovery path.");
    return CallExpr::Create(Context, Fn, ArgExprs, Context.DependentTy,
                            VK_PRValue, RParenLoc, CurFPFeatureOverrides());
  }
  return BuildResolvedCallExpr(Fn, NDecl, LParenLoc, ArgExprs, RParenLoc,
                               ExecConfig, IsExecConfig);
}

Expr *Sema::BuildBuiltinCallExpr(SourceLocation Loc, Builtin::ID Id,
                                 MultiExprArg CallArgs) {
  StringRef Name = Context.BuiltinInfo.getName(Id);
  LookupResult R(*this, &Context.Idents.get(Name), Loc,
                 Sema::LookupOrdinaryName);
  LookupName(R, TUScope, /*AllowBuiltinCreation=*/true);

  auto *BuiltInDecl = R.getAsSingle<FunctionDecl>();
  assert(BuiltInDecl && "failed to find builtin declaration");

  ExprResult DeclRef =
      BuildDeclRefExpr(BuiltInDecl, BuiltInDecl->getType(), VK_LValue, Loc);
  assert(DeclRef.isUsable() && "Builtin reference cannot fail");

  ExprResult Call =
      BuildCallExpr(/*Scope=*/nullptr, DeclRef.get(), Loc, CallArgs, Loc);

  assert(!Call.isInvalid() && "Call to builtin cannot fail!");
  return Call.get();
}

ExprResult Sema::ActOnAsTypeExpr(Expr *E, ParsedType ParsedDestTy,
                                 SourceLocation BuiltinLoc,
                                 SourceLocation RParenLoc) {
  QualType DstTy = GetTypeFromParser(ParsedDestTy);
  return BuildAsTypeExpr(E, DstTy, BuiltinLoc, RParenLoc);
}

ExprResult Sema::BuildAsTypeExpr(Expr *E, QualType DestTy,
                                 SourceLocation BuiltinLoc,
                                 SourceLocation RParenLoc) {
  ExprValueKind VK = VK_PRValue;
  ExprObjectKind OK = OK_Ordinary;
  QualType SrcTy = E->getType();
  if (!SrcTy->isDependentType() &&
      Context.getTypeSize(DestTy) != Context.getTypeSize(SrcTy))
    return ExprError(
        Diag(BuiltinLoc, diag::err_invalid_astype_of_different_size)
        << DestTy << SrcTy << E->getSourceRange());
  return new (Context) AsTypeExpr(E, DestTy, VK, OK, BuiltinLoc, RParenLoc);
}

ExprResult Sema::ActOnConvertVectorExpr(Expr *E, ParsedType ParsedDestTy,
                                        SourceLocation BuiltinLoc,
                                        SourceLocation RParenLoc) {
  TypeSourceInfo *TInfo;
  GetTypeFromParser(ParsedDestTy, &TInfo);
  return ConvertVectorExpr(E, TInfo, BuiltinLoc, RParenLoc);
}

ExprResult Sema::BuildResolvedCallExpr(Expr *Fn, NamedDecl *NDecl,
                                       SourceLocation LParenLoc,
                                       ArrayRef<Expr *> Args,
                                       SourceLocation RParenLoc, Expr *Config,
                                       bool IsExecConfig, ADLCallKind UsesADL) {
  FunctionDecl *FDecl = dyn_cast_or_null<FunctionDecl>(NDecl);
  unsigned BuiltinID = (FDecl ? FDecl->getBuiltinID() : 0);

  // Functions with 'interrupt' attribute cannot be called directly.
  if (FDecl) {
    if (FDecl->hasAttr<AnyX86InterruptAttr>()) {
      Diag(Fn->getExprLoc(), diag::err_anyx86_interrupt_called);
      return ExprError();
    }
    if (FDecl->hasAttr<ARMInterruptAttr>()) {
      Diag(Fn->getExprLoc(), diag::err_arm_interrupt_called);
      return ExprError();
    }
  }

  // X86 interrupt handlers may only call routines with attribute
  // no_caller_saved_registers since there is no efficient way to
  // save and restore the non-GPR state.
  if (auto *Caller = getCurFunctionDecl()) {
    if (Caller->hasAttr<AnyX86InterruptAttr>() ||
        Caller->hasAttr<AnyX86NoCallerSavedRegistersAttr>()) {
      const TargetInfo &TI = Context.getTargetInfo();
      bool HasNonGPRRegisters =
          TI.hasFeature("sse") || TI.hasFeature("x87") || TI.hasFeature("mmx");
      if (HasNonGPRRegisters &&
          (!FDecl || !FDecl->hasAttr<AnyX86NoCallerSavedRegistersAttr>())) {
        Diag(Fn->getExprLoc(), diag::warn_anyx86_excessive_regsave)
            << (Caller->hasAttr<AnyX86InterruptAttr>() ? 0 : 1);
        if (FDecl)
          Diag(FDecl->getLocation(), diag::note_callee_decl) << FDecl;
      }
    }
  }

  // Promote the function operand.
  // We special-case function promotion here because we only allow promoting
  // builtin functions to function pointers in the callee of a call.
  ExprResult Result;
  QualType ResultTy;
  if (BuiltinID &&
      Fn->getType()->isSpecificBuiltinType(BuiltinType::BuiltinFn)) {
    // Extract the return type from the (builtin) function pointer type.
    // FIXME Several builtins still have setType in
    // Sema::CheckBuiltinFunctionCall. One should review their definitions in
    // Builtins.td to ensure they are correct before removing setType calls.
    QualType FnPtrTy = Context.getPointerType(FDecl->getType());
    Result = ImpCastExprToType(Fn, FnPtrTy, CK_BuiltinFnToFnPtr).get();
    ResultTy = FDecl->getCallResultType();
  } else {
    Result = CallExprUnaryConversions(Fn);
    ResultTy = Context.BoolTy;
  }
  if (Result.isInvalid())
    return ExprError();
  Fn = Result.get();

  // Check for a valid function type, but only if it is not a builtin which
  // requires custom type checking. These will be handled by
  // CheckBuiltinFunctionCall below just after creation of the call expression.
  const FunctionType *FuncT = nullptr;
  if (!BuiltinID || !Context.BuiltinInfo.hasCustomTypechecking(BuiltinID)) {
  retry:
    if (const PointerType *PT = Fn->getType()->getAs<PointerType>()) {
      // C99 6.5.2.2p1 - "The expression that denotes the called function shall
      // have type pointer to function".
      FuncT = PT->getPointeeType()->getAs<FunctionType>();
      if (!FuncT)
        return ExprError(Diag(LParenLoc, diag::err_typecheck_call_not_function)
                         << Fn->getType() << Fn->getSourceRange());
    } else if (const BlockPointerType *BPT =
                   Fn->getType()->getAs<BlockPointerType>()) {
      FuncT = BPT->getPointeeType()->castAs<FunctionType>();
    } else {
      // Handle calls to expressions of unknown-any type.
      if (Fn->getType() == Context.UnknownAnyTy) {
        ExprResult rewrite = rebuildUnknownAnyFunction(*this, Fn);
        if (rewrite.isInvalid())
          return ExprError();
        Fn = rewrite.get();
        goto retry;
      }

      return ExprError(Diag(LParenLoc, diag::err_typecheck_call_not_function)
                       << Fn->getType() << Fn->getSourceRange());
    }
  }

  // Get the number of parameters in the function prototype, if any.
  // We will allocate space for max(Args.size(), NumParams) arguments
  // in the call expression.
  const auto *Proto = dyn_cast_or_null<FunctionProtoType>(FuncT);
  unsigned NumParams = Proto ? Proto->getNumParams() : 0;

  CallExpr *TheCall;
  if (Config) {
    assert(UsesADL == ADLCallKind::NotADL &&
           "CUDAKernelCallExpr should not use ADL");
    TheCall = CUDAKernelCallExpr::Create(Context, Fn, cast<CallExpr>(Config),
                                         Args, ResultTy, VK_PRValue, RParenLoc,
                                         CurFPFeatureOverrides(), NumParams);
  } else {
    TheCall =
        CallExpr::Create(Context, Fn, Args, ResultTy, VK_PRValue, RParenLoc,
                         CurFPFeatureOverrides(), NumParams, UsesADL);
  }

  if (!Context.isDependenceAllowed()) {
    // Forget about the nulled arguments since typo correction
    // do not handle them well.
    TheCall->shrinkNumArgs(Args.size());
    // C cannot always handle TypoExpr nodes in builtin calls and direct
    // function calls as their argument checking don't necessarily handle
    // dependent types properly, so make sure any TypoExprs have been
    // dealt with.
    ExprResult Result = CorrectDelayedTyposInExpr(TheCall);
    if (!Result.isUsable()) return ExprError();
    CallExpr *TheOldCall = TheCall;
    TheCall = dyn_cast<CallExpr>(Result.get());
    bool CorrectedTypos = TheCall != TheOldCall;
    if (!TheCall) return Result;
    Args = llvm::ArrayRef(TheCall->getArgs(), TheCall->getNumArgs());

    // A new call expression node was created if some typos were corrected.
    // However it may not have been constructed with enough storage. In this
    // case, rebuild the node with enough storage. The waste of space is
    // immaterial since this only happens when some typos were corrected.
    if (CorrectedTypos && Args.size() < NumParams) {
      if (Config)
        TheCall = CUDAKernelCallExpr::Create(
            Context, Fn, cast<CallExpr>(Config), Args, ResultTy, VK_PRValue,
            RParenLoc, CurFPFeatureOverrides(), NumParams);
      else
        TheCall =
            CallExpr::Create(Context, Fn, Args, ResultTy, VK_PRValue, RParenLoc,
                             CurFPFeatureOverrides(), NumParams, UsesADL);
    }
    // We can now handle the nulled arguments for the default arguments.
    TheCall->setNumArgsUnsafe(std::max<unsigned>(Args.size(), NumParams));
  }

  // Bail out early if calling a builtin with custom type checking.
  if (BuiltinID && Context.BuiltinInfo.hasCustomTypechecking(BuiltinID)) {
    ExprResult E = CheckBuiltinFunctionCall(FDecl, BuiltinID, TheCall);
    if (!E.isInvalid() && Context.BuiltinInfo.isImmediate(BuiltinID))
      E = CheckForImmediateInvocation(E, FDecl);
    return E;
  }

  if (getLangOpts().CUDA) {
    if (Config) {
      // CUDA: Kernel calls must be to global functions
      if (FDecl && !FDecl->hasAttr<CUDAGlobalAttr>())
        return ExprError(Diag(LParenLoc,diag::err_kern_call_not_global_function)
            << FDecl << Fn->getSourceRange());

      // CUDA: Kernel function must have 'void' return type
      if (!FuncT->getReturnType()->isVoidType() &&
          !FuncT->getReturnType()->getAs<AutoType>() &&
          !FuncT->getReturnType()->isInstantiationDependentType())
        return ExprError(Diag(LParenLoc, diag::err_kern_type_not_void_return)
            << Fn->getType() << Fn->getSourceRange());
    } else {
      // CUDA: Calls to global functions must be configured
      if (FDecl && FDecl->hasAttr<CUDAGlobalAttr>())
        return ExprError(Diag(LParenLoc, diag::err_global_call_not_config)
            << FDecl << Fn->getSourceRange());
    }
  }

  // Check for a valid return type
  if (CheckCallReturnType(FuncT->getReturnType(), Fn->getBeginLoc(), TheCall,
                          FDecl))
    return ExprError();

  // We know the result type of the call, set it.
  TheCall->setType(FuncT->getCallResultType(Context));
  TheCall->setValueKind(Expr::getValueKindForType(FuncT->getReturnType()));

  // WebAssembly tables can't be used as arguments.
  if (Context.getTargetInfo().getTriple().isWasm()) {
    for (const Expr *Arg : Args) {
      if (Arg && Arg->getType()->isWebAssemblyTableType()) {
        return ExprError(Diag(Arg->getExprLoc(),
                              diag::err_wasm_table_as_function_parameter));
      }
    }
  }

  if (Proto) {
    if (ConvertArgumentsForCall(TheCall, Fn, FDecl, Proto, Args, RParenLoc,
                                IsExecConfig))
      return ExprError();
  } else {
    assert(isa<FunctionNoProtoType>(FuncT) && "Unknown FunctionType!");

    if (FDecl) {
      // Check if we have too few/too many template arguments, based
      // on our knowledge of the function definition.
      const FunctionDecl *Def = nullptr;
      if (FDecl->hasBody(Def) && Args.size() != Def->param_size()) {
        Proto = Def->getType()->getAs<FunctionProtoType>();
       if (!Proto || !(Proto->isVariadic() && Args.size() >= Def->param_size()))
          Diag(RParenLoc, diag::warn_call_wrong_number_of_arguments)
          << (Args.size() > Def->param_size()) << FDecl << Fn->getSourceRange();
      }

      // If the function we're calling isn't a function prototype, but we have
      // a function prototype from a prior declaratiom, use that prototype.
      if (!FDecl->hasPrototype())
        Proto = FDecl->getType()->getAs<FunctionProtoType>();
    }

    // If we still haven't found a prototype to use but there are arguments to
    // the call, diagnose this as calling a function without a prototype.
    // However, if we found a function declaration, check to see if
    // -Wdeprecated-non-prototype was disabled where the function was declared.
    // If so, we will silence the diagnostic here on the assumption that this
    // interface is intentional and the user knows what they're doing. We will
    // also silence the diagnostic if there is a function declaration but it
    // was implicitly defined (the user already gets diagnostics about the
    // creation of the implicit function declaration, so the additional warning
    // is not helpful).
    if (!Proto && !Args.empty() &&
        (!FDecl || (!FDecl->isImplicit() &&
                    !Diags.isIgnored(diag::warn_strict_uses_without_prototype,
                                     FDecl->getLocation()))))
      Diag(LParenLoc, diag::warn_strict_uses_without_prototype)
          << (FDecl != nullptr) << FDecl;

    // Promote the arguments (C99 6.5.2.2p6).
    for (unsigned i = 0, e = Args.size(); i != e; i++) {
      Expr *Arg = Args[i];

      if (Proto && i < Proto->getNumParams()) {
        InitializedEntity Entity = InitializedEntity::InitializeParameter(
            Context, Proto->getParamType(i), Proto->isParamConsumed(i));
        ExprResult ArgE =
            PerformCopyInitialization(Entity, SourceLocation(), Arg);
        if (ArgE.isInvalid())
          return true;

        Arg = ArgE.getAs<Expr>();

      } else {
        ExprResult ArgE = DefaultArgumentPromotion(Arg);

        if (ArgE.isInvalid())
          return true;

        Arg = ArgE.getAs<Expr>();
      }

      if (RequireCompleteType(Arg->getBeginLoc(), Arg->getType(),
                              diag::err_call_incomplete_argument, Arg))
        return ExprError();

      TheCall->setArg(i, Arg);
    }
    TheCall->computeDependence();
  }

  if (CXXMethodDecl *Method = dyn_cast_or_null<CXXMethodDecl>(FDecl))
    if (Method->isImplicitObjectMemberFunction())
      return ExprError(Diag(LParenLoc, diag::err_member_call_without_object)
                       << Fn->getSourceRange() << 0);

  // Check for sentinels
  if (NDecl)
    DiagnoseSentinelCalls(NDecl, LParenLoc, Args);

  // Warn for unions passing across security boundary (CMSE).
  if (FuncT != nullptr && FuncT->getCmseNSCallAttr()) {
    for (unsigned i = 0, e = Args.size(); i != e; i++) {
      if (const auto *RT =
              dyn_cast<RecordType>(Args[i]->getType().getCanonicalType())) {
        if (RT->getDecl()->isOrContainsUnion())
          Diag(Args[i]->getBeginLoc(), diag::warn_cmse_nonsecure_union)
              << 0 << i;
      }
    }
  }

  // Do special checking on direct calls to functions.
  if (FDecl) {
    if (CheckFunctionCall(FDecl, TheCall, Proto))
      return ExprError();

    checkFortifiedBuiltinMemoryFunction(FDecl, TheCall);

    if (BuiltinID)
      return CheckBuiltinFunctionCall(FDecl, BuiltinID, TheCall);
  } else if (NDecl) {
    if (CheckPointerCall(NDecl, TheCall, Proto))
      return ExprError();
  } else {
    if (CheckOtherCall(TheCall, Proto))
      return ExprError();
  }

  return CheckForImmediateInvocation(MaybeBindToTemporary(TheCall), FDecl);
}

ExprResult
Sema::ActOnCompoundLiteral(SourceLocation LParenLoc, ParsedType Ty,
                           SourceLocation RParenLoc, Expr *InitExpr) {
  assert(Ty && "ActOnCompoundLiteral(): missing type");
  assert(InitExpr && "ActOnCompoundLiteral(): missing expression");

  TypeSourceInfo *TInfo;
  QualType literalType = GetTypeFromParser(Ty, &TInfo);
  if (!TInfo)
    TInfo = Context.getTrivialTypeSourceInfo(literalType);

  return BuildCompoundLiteralExpr(LParenLoc, TInfo, RParenLoc, InitExpr);
}

ExprResult
Sema::BuildCompoundLiteralExpr(SourceLocation LParenLoc, TypeSourceInfo *TInfo,
                               SourceLocation RParenLoc, Expr *LiteralExpr) {
  QualType literalType = TInfo->getType();

  if (literalType->isArrayType()) {
    if (RequireCompleteSizedType(
            LParenLoc, Context.getBaseElementType(literalType),
            diag::err_array_incomplete_or_sizeless_type,
            SourceRange(LParenLoc, LiteralExpr->getSourceRange().getEnd())))
      return ExprError();
    if (literalType->isVariableArrayType()) {
      // C23 6.7.10p4: An entity of variable length array type shall not be
      // initialized except by an empty initializer.
      //
      // The C extension warnings are issued from ParseBraceInitializer() and
      // do not need to be issued here. However, we continue to issue an error
      // in the case there are initializers or we are compiling C++. We allow
      // use of VLAs in C++, but it's not clear we want to allow {} to zero
      // init a VLA in C++ in all cases (such as with non-trivial constructors).
      // FIXME: should we allow this construct in C++ when it makes sense to do
      // so?
      //
      // But: C99-C23 6.5.2.5 Compound literals constraint 1: The type name
      // shall specify an object type or an array of unknown size, but not a
      // variable length array type. This seems odd, as it allows 'int a[size] =
      // {}', but forbids 'int *a = (int[size]){}'. As this is what the standard
      // says, this is what's implemented here for C (except for the extension
      // that permits constant foldable size arrays)

      auto diagID = LangOpts.CPlusPlus
                        ? diag::err_variable_object_no_init
                        : diag::err_compound_literal_with_vla_type;
      if (!tryToFixVariablyModifiedVarType(TInfo, literalType, LParenLoc,
                                           diagID))
        return ExprError();
    }
  } else if (!literalType->isDependentType() &&
             RequireCompleteType(LParenLoc, literalType,
               diag::err_typecheck_decl_incomplete_type,
               SourceRange(LParenLoc, LiteralExpr->getSourceRange().getEnd())))
    return ExprError();

  InitializedEntity Entity
    = InitializedEntity::InitializeCompoundLiteralInit(TInfo);
  InitializationKind Kind
    = InitializationKind::CreateCStyleCast(LParenLoc,
                                           SourceRange(LParenLoc, RParenLoc),
                                           /*InitList=*/true);
  InitializationSequence InitSeq(*this, Entity, Kind, LiteralExpr);
  ExprResult Result = InitSeq.Perform(*this, Entity, Kind, LiteralExpr,
                                      &literalType);
  if (Result.isInvalid())
    return ExprError();
  LiteralExpr = Result.get();

  bool isFileScope = !CurContext->isFunctionOrMethod();

  // In C, compound literals are l-values for some reason.
  // For GCC compatibility, in C++, file-scope array compound literals with
  // constant initializers are also l-values, and compound literals are
  // otherwise prvalues.
  //
  // (GCC also treats C++ list-initialized file-scope array prvalues with
  // constant initializers as l-values, but that's non-conforming, so we don't
  // follow it there.)
  //
  // FIXME: It would be better to handle the lvalue cases as materializing and
  // lifetime-extending a temporary object, but our materialized temporaries
  // representation only supports lifetime extension from a variable, not "out
  // of thin air".
  // FIXME: For C++, we might want to instead lifetime-extend only if a pointer
  // is bound to the result of applying array-to-pointer decay to the compound
  // literal.
  // FIXME: GCC supports compound literals of reference type, which should
  // obviously have a value kind derived from the kind of reference involved.
  ExprValueKind VK =
      (getLangOpts().CPlusPlus && !(isFileScope && literalType->isArrayType()))
          ? VK_PRValue
          : VK_LValue;

  if (isFileScope)
    if (auto ILE = dyn_cast<InitListExpr>(LiteralExpr))
      for (unsigned i = 0, j = ILE->getNumInits(); i != j; i++) {
        Expr *Init = ILE->getInit(i);
        ILE->setInit(i, ConstantExpr::Create(Context, Init));
      }

  auto *E = new (Context) CompoundLiteralExpr(LParenLoc, TInfo, literalType,
                                              VK, LiteralExpr, isFileScope);
  if (isFileScope) {
    if (!LiteralExpr->isTypeDependent() &&
        !LiteralExpr->isValueDependent() &&
        !literalType->isDependentType()) // C99 6.5.2.5p3
      if (CheckForConstantInitializer(LiteralExpr))
        return ExprError();
  } else if (literalType.getAddressSpace() != LangAS::opencl_private &&
             literalType.getAddressSpace() != LangAS::Default) {
    // Embedded-C extensions to C99 6.5.2.5:
    //   "If the compound literal occurs inside the body of a function, the
    //   type name shall not be qualified by an address-space qualifier."
    Diag(LParenLoc, diag::err_compound_literal_with_address_space)
      << SourceRange(LParenLoc, LiteralExpr->getSourceRange().getEnd());
    return ExprError();
  }

  if (!isFileScope && !getLangOpts().CPlusPlus) {
    // Compound literals that have automatic storage duration are destroyed at
    // the end of the scope in C; in C++, they're just temporaries.

    // Emit diagnostics if it is or contains a C union type that is non-trivial
    // to destruct.
    if (E->getType().hasNonTrivialToPrimitiveDestructCUnion())
      checkNonTrivialCUnion(E->getType(), E->getExprLoc(),
                            NTCUC_CompoundLiteral, NTCUK_Destruct);

    // Diagnose jumps that enter or exit the lifetime of the compound literal.
    if (literalType.isDestructedType()) {
      Cleanup.setExprNeedsCleanups(true);
      ExprCleanupObjects.push_back(E);
      getCurFunction()->setHasBranchProtectedScope();
    }
  }

  if (E->getType().hasNonTrivialToPrimitiveDefaultInitializeCUnion() ||
      E->getType().hasNonTrivialToPrimitiveCopyCUnion())
    checkNonTrivialCUnionInInitializer(E->getInitializer(),
                                       E->getInitializer()->getExprLoc());

  return MaybeBindToTemporary(E);
}

ExprResult
Sema::ActOnInitList(SourceLocation LBraceLoc, MultiExprArg InitArgList,
                    SourceLocation RBraceLoc) {
  // Only produce each kind of designated initialization diagnostic once.
  SourceLocation FirstDesignator;
  bool DiagnosedArrayDesignator = false;
  bool DiagnosedNestedDesignator = false;
  bool DiagnosedMixedDesignator = false;

  // Check that any designated initializers are syntactically valid in the
  // current language mode.
  for (unsigned I = 0, E = InitArgList.size(); I != E; ++I) {
    if (auto *DIE = dyn_cast<DesignatedInitExpr>(InitArgList[I])) {
      if (FirstDesignator.isInvalid())
        FirstDesignator = DIE->getBeginLoc();

      if (!getLangOpts().CPlusPlus)
        break;

      if (!DiagnosedNestedDesignator && DIE->size() > 1) {
        DiagnosedNestedDesignator = true;
        Diag(DIE->getBeginLoc(), diag::ext_designated_init_nested)
          << DIE->getDesignatorsSourceRange();
      }

      for (auto &Desig : DIE->designators()) {
        if (!Desig.isFieldDesignator() && !DiagnosedArrayDesignator) {
          DiagnosedArrayDesignator = true;
          Diag(Desig.getBeginLoc(), diag::ext_designated_init_array)
            << Desig.getSourceRange();
        }
      }

      if (!DiagnosedMixedDesignator &&
          !isa<DesignatedInitExpr>(InitArgList[0])) {
        DiagnosedMixedDesignator = true;
        Diag(DIE->getBeginLoc(), diag::ext_designated_init_mixed)
          << DIE->getSourceRange();
        Diag(InitArgList[0]->getBeginLoc(), diag::note_designated_init_mixed)
          << InitArgList[0]->getSourceRange();
      }
    } else if (getLangOpts().CPlusPlus && !DiagnosedMixedDesignator &&
               isa<DesignatedInitExpr>(InitArgList[0])) {
      DiagnosedMixedDesignator = true;
      auto *DIE = cast<DesignatedInitExpr>(InitArgList[0]);
      Diag(DIE->getBeginLoc(), diag::ext_designated_init_mixed)
        << DIE->getSourceRange();
      Diag(InitArgList[I]->getBeginLoc(), diag::note_designated_init_mixed)
        << InitArgList[I]->getSourceRange();
    }
  }

  if (FirstDesignator.isValid()) {
    // Only diagnose designated initiaization as a C++20 extension if we didn't
    // already diagnose use of (non-C++20) C99 designator syntax.
    if (getLangOpts().CPlusPlus && !DiagnosedArrayDesignator &&
        !DiagnosedNestedDesignator && !DiagnosedMixedDesignator) {
      Diag(FirstDesignator, getLangOpts().CPlusPlus20
                                ? diag::warn_cxx17_compat_designated_init
                                : diag::ext_cxx_designated_init);
    } else if (!getLangOpts().CPlusPlus && !getLangOpts().C99) {
      Diag(FirstDesignator, diag::ext_designated_init);
    }
  }

  return BuildInitList(LBraceLoc, InitArgList, RBraceLoc);
}

ExprResult
Sema::BuildInitList(SourceLocation LBraceLoc, MultiExprArg InitArgList,
                    SourceLocation RBraceLoc) {
  // Semantic analysis for initializers is done by ActOnDeclarator() and
  // CheckInitializer() - it requires knowledge of the object being initialized.

  // Immediately handle non-overload placeholders.  Overloads can be
  // resolved contextually, but everything else here can't.
  for (unsigned I = 0, E = InitArgList.size(); I != E; ++I) {
    if (InitArgList[I]->getType()->isNonOverloadPlaceholderType()) {
      ExprResult result = CheckPlaceholderExpr(InitArgList[I]);

      // Ignore failures; dropping the entire initializer list because
      // of one failure would be terrible for indexing/etc.
      if (result.isInvalid()) continue;

      InitArgList[I] = result.get();
    }
  }

  InitListExpr *E =
      new (Context) InitListExpr(Context, LBraceLoc, InitArgList, RBraceLoc);
  E->setType(Context.VoidTy); // FIXME: just a place holder for now.
  return E;
}

void Sema::maybeExtendBlockObject(ExprResult &E) {
  assert(E.get()->getType()->isBlockPointerType());
  assert(E.get()->isPRValue());

  // Only do this in an r-value context.
  if (!getLangOpts().ObjCAutoRefCount) return;

  E = ImplicitCastExpr::Create(
      Context, E.get()->getType(), CK_ARCExtendBlockObject, E.get(),
      /*base path*/ nullptr, VK_PRValue, FPOptionsOverride());
  Cleanup.setExprNeedsCleanups(true);
}

CastKind Sema::PrepareScalarCast(ExprResult &Src, QualType DestTy) {
  // Both Src and Dest are scalar types, i.e. arithmetic or pointer.
  // Also, callers should have filtered out the invalid cases with
  // pointers.  Everything else should be possible.

  QualType SrcTy = Src.get()->getType();
  if (Context.hasSameUnqualifiedType(SrcTy, DestTy))
    return CK_NoOp;

  switch (Type::ScalarTypeKind SrcKind = SrcTy->getScalarTypeKind()) {
  case Type::STK_MemberPointer:
    llvm_unreachable("member pointer type in C");

  case Type::STK_CPointer:
  case Type::STK_BlockPointer:
  case Type::STK_ObjCObjectPointer:
    switch (DestTy->getScalarTypeKind()) {
    case Type::STK_CPointer: {
      LangAS SrcAS = SrcTy->getPointeeType().getAddressSpace();
      LangAS DestAS = DestTy->getPointeeType().getAddressSpace();
      if (SrcAS != DestAS)
        return CK_AddressSpaceConversion;
      if (Context.hasCvrSimilarType(SrcTy, DestTy))
        return CK_NoOp;
      return CK_BitCast;
    }
    case Type::STK_BlockPointer:
      return (SrcKind == Type::STK_BlockPointer
                ? CK_BitCast : CK_AnyPointerToBlockPointerCast);
    case Type::STK_ObjCObjectPointer:
      if (SrcKind == Type::STK_ObjCObjectPointer)
        return CK_BitCast;
      if (SrcKind == Type::STK_CPointer)
        return CK_CPointerToObjCPointerCast;
      maybeExtendBlockObject(Src);
      return CK_BlockPointerToObjCPointerCast;
    case Type::STK_Bool:
      return CK_PointerToBoolean;
    case Type::STK_Integral:
      return CK_PointerToIntegral;
    case Type::STK_Floating:
    case Type::STK_FloatingComplex:
    case Type::STK_IntegralComplex:
    case Type::STK_MemberPointer:
    case Type::STK_FixedPoint:
      llvm_unreachable("illegal cast from pointer");
    }
    llvm_unreachable("Should have returned before this");

  case Type::STK_FixedPoint:
    switch (DestTy->getScalarTypeKind()) {
    case Type::STK_FixedPoint:
      return CK_FixedPointCast;
    case Type::STK_Bool:
      return CK_FixedPointToBoolean;
    case Type::STK_Integral:
      return CK_FixedPointToIntegral;
    case Type::STK_Floating:
      return CK_FixedPointToFloating;
    case Type::STK_IntegralComplex:
    case Type::STK_FloatingComplex:
      Diag(Src.get()->getExprLoc(),
           diag::err_unimplemented_conversion_with_fixed_point_type)
          << DestTy;
      return CK_IntegralCast;
    case Type::STK_CPointer:
    case Type::STK_ObjCObjectPointer:
    case Type::STK_BlockPointer:
    case Type::STK_MemberPointer:
      llvm_unreachable("illegal cast to pointer type");
    }
    llvm_unreachable("Should have returned before this");

  case Type::STK_Bool: // casting from bool is like casting from an integer
  case Type::STK_Integral:
    switch (DestTy->getScalarTypeKind()) {
    case Type::STK_CPointer:
    case Type::STK_ObjCObjectPointer:
    case Type::STK_BlockPointer:
      if (Src.get()->isNullPointerConstant(Context,
                                           Expr::NPC_ValueDependentIsNull))
        return CK_NullToPointer;
      return CK_IntegralToPointer;
    case Type::STK_Bool:
      return CK_IntegralToBoolean;
    case Type::STK_Integral:
      return CK_IntegralCast;
    case Type::STK_Floating:
      return CK_IntegralToFloating;
    case Type::STK_IntegralComplex:
      Src = ImpCastExprToType(Src.get(),
                      DestTy->castAs<ComplexType>()->getElementType(),
                      CK_IntegralCast);
      return CK_IntegralRealToComplex;
    case Type::STK_FloatingComplex:
      Src = ImpCastExprToType(Src.get(),
                      DestTy->castAs<ComplexType>()->getElementType(),
                      CK_IntegralToFloating);
      return CK_FloatingRealToComplex;
    case Type::STK_MemberPointer:
      llvm_unreachable("member pointer type in C");
    case Type::STK_FixedPoint:
      return CK_IntegralToFixedPoint;
    }
    llvm_unreachable("Should have returned before this");

  case Type::STK_Floating:
    switch (DestTy->getScalarTypeKind()) {
    case Type::STK_Floating:
      return CK_FloatingCast;
    case Type::STK_Bool:
      return CK_FloatingToBoolean;
    case Type::STK_Integral:
      return CK_FloatingToIntegral;
    case Type::STK_FloatingComplex:
      Src = ImpCastExprToType(Src.get(),
                              DestTy->castAs<ComplexType>()->getElementType(),
                              CK_FloatingCast);
      return CK_FloatingRealToComplex;
    case Type::STK_IntegralComplex:
      Src = ImpCastExprToType(Src.get(),
                              DestTy->castAs<ComplexType>()->getElementType(),
                              CK_FloatingToIntegral);
      return CK_IntegralRealToComplex;
    case Type::STK_CPointer:
    case Type::STK_ObjCObjectPointer:
    case Type::STK_BlockPointer:
      llvm_unreachable("valid float->pointer cast?");
    case Type::STK_MemberPointer:
      llvm_unreachable("member pointer type in C");
    case Type::STK_FixedPoint:
      return CK_FloatingToFixedPoint;
    }
    llvm_unreachable("Should have returned before this");

  case Type::STK_FloatingComplex:
    switch (DestTy->getScalarTypeKind()) {
    case Type::STK_FloatingComplex:
      return CK_FloatingComplexCast;
    case Type::STK_IntegralComplex:
      return CK_FloatingComplexToIntegralComplex;
    case Type::STK_Floating: {
      QualType ET = SrcTy->castAs<ComplexType>()->getElementType();
      if (Context.hasSameType(ET, DestTy))
        return CK_FloatingComplexToReal;
      Src = ImpCastExprToType(Src.get(), ET, CK_FloatingComplexToReal);
      return CK_FloatingCast;
    }
    case Type::STK_Bool:
      return CK_FloatingComplexToBoolean;
    case Type::STK_Integral:
      Src = ImpCastExprToType(Src.get(),
                              SrcTy->castAs<ComplexType>()->getElementType(),
                              CK_FloatingComplexToReal);
      return CK_FloatingToIntegral;
    case Type::STK_CPointer:
    case Type::STK_ObjCObjectPointer:
    case Type::STK_BlockPointer:
      llvm_unreachable("valid complex float->pointer cast?");
    case Type::STK_MemberPointer:
      llvm_unreachable("member pointer type in C");
    case Type::STK_FixedPoint:
      Diag(Src.get()->getExprLoc(),
           diag::err_unimplemented_conversion_with_fixed_point_type)
          << SrcTy;
      return CK_IntegralCast;
    }
    llvm_unreachable("Should have returned before this");

  case Type::STK_IntegralComplex:
    switch (DestTy->getScalarTypeKind()) {
    case Type::STK_FloatingComplex:
      return CK_IntegralComplexToFloatingComplex;
    case Type::STK_IntegralComplex:
      return CK_IntegralComplexCast;
    case Type::STK_Integral: {
      QualType ET = SrcTy->castAs<ComplexType>()->getElementType();
      if (Context.hasSameType(ET, DestTy))
        return CK_IntegralComplexToReal;
      Src = ImpCastExprToType(Src.get(), ET, CK_IntegralComplexToReal);
      return CK_IntegralCast;
    }
    case Type::STK_Bool:
      return CK_IntegralComplexToBoolean;
    case Type::STK_Floating:
      Src = ImpCastExprToType(Src.get(),
                              SrcTy->castAs<ComplexType>()->getElementType(),
                              CK_IntegralComplexToReal);
      return CK_IntegralToFloating;
    case Type::STK_CPointer:
    case Type::STK_ObjCObjectPointer:
    case Type::STK_BlockPointer:
      llvm_unreachable("valid complex int->pointer cast?");
    case Type::STK_MemberPointer:
      llvm_unreachable("member pointer type in C");
    case Type::STK_FixedPoint:
      Diag(Src.get()->getExprLoc(),
           diag::err_unimplemented_conversion_with_fixed_point_type)
          << SrcTy;
      return CK_IntegralCast;
    }
    llvm_unreachable("Should have returned before this");
  }

  llvm_unreachable("Unhandled scalar cast");
}

static bool breakDownVectorType(QualType type, uint64_t &len,
                                QualType &eltType) {
  // Vectors are simple.
  if (const VectorType *vecType = type->getAs<VectorType>()) {
    len = vecType->getNumElements();
    eltType = vecType->getElementType();
    assert(eltType->isScalarType());
    return true;
  }

  // We allow lax conversion to and from non-vector types, but only if
  // they're real types (i.e. non-complex, non-pointer scalar types).
  if (!type->isRealType()) return false;

  len = 1;
  eltType = type;
  return true;
}

bool Sema::isValidSveBitcast(QualType srcTy, QualType destTy) {
  assert(srcTy->isVectorType() || destTy->isVectorType());

  auto ValidScalableConversion = [](QualType FirstType, QualType SecondType) {
    if (!FirstType->isSVESizelessBuiltinType())
      return false;

    const auto *VecTy = SecondType->getAs<VectorType>();
    return VecTy && VecTy->getVectorKind() == VectorKind::SveFixedLengthData;
  };

  return ValidScalableConversion(srcTy, destTy) ||
         ValidScalableConversion(destTy, srcTy);
}

bool Sema::areMatrixTypesOfTheSameDimension(QualType srcTy, QualType destTy) {
  if (!destTy->isMatrixType() || !srcTy->isMatrixType())
    return false;

  const ConstantMatrixType *matSrcType = srcTy->getAs<ConstantMatrixType>();
  const ConstantMatrixType *matDestType = destTy->getAs<ConstantMatrixType>();

  return matSrcType->getNumRows() == matDestType->getNumRows() &&
         matSrcType->getNumColumns() == matDestType->getNumColumns();
}

bool Sema::areVectorTypesSameSize(QualType SrcTy, QualType DestTy) {
  assert(DestTy->isVectorType() || SrcTy->isVectorType());

  uint64_t SrcLen, DestLen;
  QualType SrcEltTy, DestEltTy;
  if (!breakDownVectorType(SrcTy, SrcLen, SrcEltTy))
    return false;
  if (!breakDownVectorType(DestTy, DestLen, DestEltTy))
    return false;

  // ASTContext::getTypeSize will return the size rounded up to a
  // power of 2, so instead of using that, we need to use the raw
  // element size multiplied by the element count.
  uint64_t SrcEltSize = Context.getTypeSize(SrcEltTy);
  uint64_t DestEltSize = Context.getTypeSize(DestEltTy);

  return (SrcLen * SrcEltSize == DestLen * DestEltSize);
}

bool Sema::anyAltivecTypes(QualType SrcTy, QualType DestTy) {
  assert((DestTy->isVectorType() || SrcTy->isVectorType()) &&
         "expected at least one type to be a vector here");

  bool IsSrcTyAltivec =
      SrcTy->isVectorType() && ((SrcTy->castAs<VectorType>()->getVectorKind() ==
                                 VectorKind::AltiVecVector) ||
                                (SrcTy->castAs<VectorType>()->getVectorKind() ==
                                 VectorKind::AltiVecBool) ||
                                (SrcTy->castAs<VectorType>()->getVectorKind() ==
                                 VectorKind::AltiVecPixel));

  bool IsDestTyAltivec = DestTy->isVectorType() &&
                         ((DestTy->castAs<VectorType>()->getVectorKind() ==
                           VectorKind::AltiVecVector) ||
                          (DestTy->castAs<VectorType>()->getVectorKind() ==
                           VectorKind::AltiVecBool) ||
                          (DestTy->castAs<VectorType>()->getVectorKind() ==
                           VectorKind::AltiVecPixel));

  return (IsSrcTyAltivec || IsDestTyAltivec);
}

bool Sema::areLaxCompatibleVectorTypes(QualType srcTy, QualType destTy) {
  assert(destTy->isVectorType() || srcTy->isVectorType());

  // Disallow lax conversions between scalars and ExtVectors (these
  // conversions are allowed for other vector types because common headers
  // depend on them).  Most scalar OP ExtVector cases are handled by the
  // splat path anyway, which does what we want (convert, not bitcast).
  // What this rules out for ExtVectors is crazy things like char4*float.
  if (srcTy->isScalarType() && destTy->isExtVectorType()) return false;
  if (destTy->isScalarType() && srcTy->isExtVectorType()) return false;

  return areVectorTypesSameSize(srcTy, destTy);
}

bool Sema::isLaxVectorConversion(QualType srcTy, QualType destTy) {
  assert(destTy->isVectorType() || srcTy->isVectorType());

  switch (Context.getLangOpts().getLaxVectorConversions()) {
  case LangOptions::LaxVectorConversionKind::None:
    return false;

  case LangOptions::LaxVectorConversionKind::Integer:
    if (!srcTy->isIntegralOrEnumerationType()) {
      auto *Vec = srcTy->getAs<VectorType>();
      if (!Vec || !Vec->getElementType()->isIntegralOrEnumerationType())
        return false;
    }
    if (!destTy->isIntegralOrEnumerationType()) {
      auto *Vec = destTy->getAs<VectorType>();
      if (!Vec || !Vec->getElementType()->isIntegralOrEnumerationType())
        return false;
    }
    // OK, integer (vector) -> integer (vector) bitcast.
    break;

    case LangOptions::LaxVectorConversionKind::All:
    break;
  }

  return areLaxCompatibleVectorTypes(srcTy, destTy);
}

bool Sema::CheckMatrixCast(SourceRange R, QualType DestTy, QualType SrcTy,
                           CastKind &Kind) {
  if (SrcTy->isMatrixType() && DestTy->isMatrixType()) {
    if (!areMatrixTypesOfTheSameDimension(SrcTy, DestTy)) {
      return Diag(R.getBegin(), diag::err_invalid_conversion_between_matrixes)
             << DestTy << SrcTy << R;
    }
  } else if (SrcTy->isMatrixType()) {
    return Diag(R.getBegin(),
                diag::err_invalid_conversion_between_matrix_and_type)
           << SrcTy << DestTy << R;
  } else if (DestTy->isMatrixType()) {
    return Diag(R.getBegin(),
                diag::err_invalid_conversion_between_matrix_and_type)
           << DestTy << SrcTy << R;
  }

  Kind = CK_MatrixCast;
  return false;
}

bool Sema::CheckVectorCast(SourceRange R, QualType VectorTy, QualType Ty,
                           CastKind &Kind) {
  assert(VectorTy->isVectorType() && "Not a vector type!");

  if (Ty->isVectorType() || Ty->isIntegralType(Context)) {
    if (!areLaxCompatibleVectorTypes(Ty, VectorTy))
      return Diag(R.getBegin(),
                  Ty->isVectorType() ?
                  diag::err_invalid_conversion_between_vectors :
                  diag::err_invalid_conversion_between_vector_and_integer)
        << VectorTy << Ty << R;
  } else
    return Diag(R.getBegin(),
                diag::err_invalid_conversion_between_vector_and_scalar)
      << VectorTy << Ty << R;

  Kind = CK_BitCast;
  return false;
}

ExprResult Sema::prepareVectorSplat(QualType VectorTy, Expr *SplattedExpr) {
  QualType DestElemTy = VectorTy->castAs<VectorType>()->getElementType();

  if (DestElemTy == SplattedExpr->getType())
    return SplattedExpr;

  assert(DestElemTy->isFloatingType() ||
         DestElemTy->isIntegralOrEnumerationType());

  CastKind CK;
  if (VectorTy->isExtVectorType() && SplattedExpr->getType()->isBooleanType()) {
    // OpenCL requires that we convert `true` boolean expressions to -1, but
    // only when splatting vectors.
    if (DestElemTy->isFloatingType()) {
      // To avoid having to have a CK_BooleanToSignedFloating cast kind, we cast
      // in two steps: boolean to signed integral, then to floating.
      ExprResult CastExprRes = ImpCastExprToType(SplattedExpr, Context.IntTy,
                                                 CK_BooleanToSignedIntegral);
      SplattedExpr = CastExprRes.get();
      CK = CK_IntegralToFloating;
    } else {
      CK = CK_BooleanToSignedIntegral;
    }
  } else {
    ExprResult CastExprRes = SplattedExpr;
    CK = PrepareScalarCast(CastExprRes, DestElemTy);
    if (CastExprRes.isInvalid())
      return ExprError();
    SplattedExpr = CastExprRes.get();
  }
  return ImpCastExprToType(SplattedExpr, DestElemTy, CK);
}

ExprResult Sema::CheckExtVectorCast(SourceRange R, QualType DestTy,
                                    Expr *CastExpr, CastKind &Kind) {
  assert(DestTy->isExtVectorType() && "Not an extended vector type!");

  QualType SrcTy = CastExpr->getType();

  // If SrcTy is a VectorType, the total size must match to explicitly cast to
  // an ExtVectorType.
  // In OpenCL, casts between vectors of different types are not allowed.
  // (See OpenCL 6.2).
  if (SrcTy->isVectorType()) {
    if (!areLaxCompatibleVectorTypes(SrcTy, DestTy) ||
        (getLangOpts().OpenCL &&
         !Context.hasSameUnqualifiedType(DestTy, SrcTy))) {
      Diag(R.getBegin(),diag::err_invalid_conversion_between_ext_vectors)
        << DestTy << SrcTy << R;
      return ExprError();
    }
    Kind = CK_BitCast;
    return CastExpr;
  }

  // All non-pointer scalars can be cast to ExtVector type.  The appropriate
  // conversion will take place first from scalar to elt type, and then
  // splat from elt type to vector.
  if (SrcTy->isPointerType())
    return Diag(R.getBegin(),
                diag::err_invalid_conversion_between_vector_and_scalar)
      << DestTy << SrcTy << R;

  Kind = CK_VectorSplat;
  return prepareVectorSplat(DestTy, CastExpr);
}

ExprResult
Sema::ActOnCastExpr(Scope *S, SourceLocation LParenLoc,
                    Declarator &D, ParsedType &Ty,
                    SourceLocation RParenLoc, Expr *CastExpr) {
  assert(!D.isInvalidType() && (CastExpr != nullptr) &&
         "ActOnCastExpr(): missing type or expr");

  TypeSourceInfo *castTInfo = GetTypeForDeclaratorCast(D, CastExpr->getType());
  if (D.isInvalidType())
    return ExprError();

  if (getLangOpts().CPlusPlus) {
    // Check that there are no default arguments (C++ only).
    CheckExtraCXXDefaultArguments(D);
  } else {
    // Make sure any TypoExprs have been dealt with.
    ExprResult Res = CorrectDelayedTyposInExpr(CastExpr);
    if (!Res.isUsable())
      return ExprError();
    CastExpr = Res.get();
  }

  checkUnusedDeclAttributes(D);

  QualType castType = castTInfo->getType();
  Ty = CreateParsedType(castType, castTInfo);

  bool isVectorLiteral = false;

  // Check for an altivec or OpenCL literal,
  // i.e. all the elements are integer constants.
  ParenExpr *PE = dyn_cast<ParenExpr>(CastExpr);
  ParenListExpr *PLE = dyn_cast<ParenListExpr>(CastExpr);
  if ((getLangOpts().AltiVec || getLangOpts().ZVector || getLangOpts().OpenCL)
       && castType->isVectorType() && (PE || PLE)) {
    if (PLE && PLE->getNumExprs() == 0) {
      Diag(PLE->getExprLoc(), diag::err_altivec_empty_initializer);
      return ExprError();
    }
    if (PE || PLE->getNumExprs() == 1) {
      Expr *E = (PE ? PE->getSubExpr() : PLE->getExpr(0));
      if (!E->isTypeDependent() && !E->getType()->isVectorType())
        isVectorLiteral = true;
    }
    else
      isVectorLiteral = true;
  }

  // If this is a vector initializer, '(' type ')' '(' init, ..., init ')'
  // then handle it as such.
  if (isVectorLiteral)
    return BuildVectorLiteral(LParenLoc, RParenLoc, CastExpr, castTInfo);

  // If the Expr being casted is a ParenListExpr, handle it specially.
  // This is not an AltiVec-style cast, so turn the ParenListExpr into a
  // sequence of BinOp comma operators.
  if (isa<ParenListExpr>(CastExpr)) {
    ExprResult Result = MaybeConvertParenListExprToParenExpr(S, CastExpr);
    if (Result.isInvalid()) return ExprError();
    CastExpr = Result.get();
  }

  if (getLangOpts().CPlusPlus && !castType->isVoidType())
    Diag(LParenLoc, diag::warn_old_style_cast) << CastExpr->getSourceRange();

  ObjC().CheckTollFreeBridgeCast(castType, CastExpr);

  ObjC().CheckObjCBridgeRelatedCast(castType, CastExpr);

  DiscardMisalignedMemberAddress(castType.getTypePtr(), CastExpr);

  return BuildCStyleCastExpr(LParenLoc, castTInfo, RParenLoc, CastExpr);
}

ExprResult Sema::BuildVectorLiteral(SourceLocation LParenLoc,
                                    SourceLocation RParenLoc, Expr *E,
                                    TypeSourceInfo *TInfo) {
  assert((isa<ParenListExpr>(E) || isa<ParenExpr>(E)) &&
         "Expected paren or paren list expression");

  Expr **exprs;
  unsigned numExprs;
  Expr *subExpr;
  SourceLocation LiteralLParenLoc, LiteralRParenLoc;
  if (ParenListExpr *PE = dyn_cast<ParenListExpr>(E)) {
    LiteralLParenLoc = PE->getLParenLoc();
    LiteralRParenLoc = PE->getRParenLoc();
    exprs = PE->getExprs();
    numExprs = PE->getNumExprs();
  } else { // isa<ParenExpr> by assertion at function entrance
    LiteralLParenLoc = cast<ParenExpr>(E)->getLParen();
    LiteralRParenLoc = cast<ParenExpr>(E)->getRParen();
    subExpr = cast<ParenExpr>(E)->getSubExpr();
    exprs = &subExpr;
    numExprs = 1;
  }

  QualType Ty = TInfo->getType();
  assert(Ty->isVectorType() && "Expected vector type");

  SmallVector<Expr *, 8> initExprs;
  const VectorType *VTy = Ty->castAs<VectorType>();
  unsigned numElems = VTy->getNumElements();

  // '(...)' form of vector initialization in AltiVec: the number of
  // initializers must be one or must match the size of the vector.
  // If a single value is specified in the initializer then it will be
  // replicated to all the components of the vector
  if (CheckAltivecInitFromScalar(E->getSourceRange(), Ty,
                                 VTy->getElementType()))
    return ExprError();
  if (ShouldSplatAltivecScalarInCast(VTy)) {
    // The number of initializers must be one or must match the size of the
    // vector. If a single value is specified in the initializer then it will
    // be replicated to all the components of the vector
    if (numExprs == 1) {
      QualType ElemTy = VTy->getElementType();
      ExprResult Literal = DefaultLvalueConversion(exprs[0]);
      if (Literal.isInvalid())
        return ExprError();
      Literal = ImpCastExprToType(Literal.get(), ElemTy,
                                  PrepareScalarCast(Literal, ElemTy));
      return BuildCStyleCastExpr(LParenLoc, TInfo, RParenLoc, Literal.get());
    }
    else if (numExprs < numElems) {
      Diag(E->getExprLoc(),
           diag::err_incorrect_number_of_vector_initializers);
      return ExprError();
    }
    else
      initExprs.append(exprs, exprs + numExprs);
  }
  else {
    // For OpenCL, when the number of initializers is a single value,
    // it will be replicated to all components of the vector.
    if (getLangOpts().OpenCL && VTy->getVectorKind() == VectorKind::Generic &&
        numExprs == 1) {
      QualType ElemTy = VTy->getElementType();
      ExprResult Literal = DefaultLvalueConversion(exprs[0]);
      if (Literal.isInvalid())
        return ExprError();
      Literal = ImpCastExprToType(Literal.get(), ElemTy,
                                  PrepareScalarCast(Literal, ElemTy));
      return BuildCStyleCastExpr(LParenLoc, TInfo, RParenLoc, Literal.get());
    }

    initExprs.append(exprs, exprs + numExprs);
  }
  // FIXME: This means that pretty-printing the final AST will produce curly
  // braces instead of the original commas.
  InitListExpr *initE = new (Context) InitListExpr(Context, LiteralLParenLoc,
                                                   initExprs, LiteralRParenLoc);
  initE->setType(Ty);
  return BuildCompoundLiteralExpr(LParenLoc, TInfo, RParenLoc, initE);
}

ExprResult
Sema::MaybeConvertParenListExprToParenExpr(Scope *S, Expr *OrigExpr) {
  ParenListExpr *E = dyn_cast<ParenListExpr>(OrigExpr);
  if (!E)
    return OrigExpr;

  ExprResult Result(E->getExpr(0));

  for (unsigned i = 1, e = E->getNumExprs(); i != e && !Result.isInvalid(); ++i)
    Result = ActOnBinOp(S, E->getExprLoc(), tok::comma, Result.get(),
                        E->getExpr(i));

  if (Result.isInvalid()) return ExprError();

  return ActOnParenExpr(E->getLParenLoc(), E->getRParenLoc(), Result.get());
}

ExprResult Sema::ActOnParenListExpr(SourceLocation L,
                                    SourceLocation R,
                                    MultiExprArg Val) {
  return ParenListExpr::Create(Context, L, Val, R);
}

bool Sema::DiagnoseConditionalForNull(const Expr *LHSExpr, const Expr *RHSExpr,
                                      SourceLocation QuestionLoc) {
  const Expr *NullExpr = LHSExpr;
  const Expr *NonPointerExpr = RHSExpr;
  Expr::NullPointerConstantKind NullKind =
      NullExpr->isNullPointerConstant(Context,
                                      Expr::NPC_ValueDependentIsNotNull);

  if (NullKind == Expr::NPCK_NotNull) {
    NullExpr = RHSExpr;
    NonPointerExpr = LHSExpr;
    NullKind =
        NullExpr->isNullPointerConstant(Context,
                                        Expr::NPC_ValueDependentIsNotNull);
  }

  if (NullKind == Expr::NPCK_NotNull)
    return false;

  if (NullKind == Expr::NPCK_ZeroExpression)
    return false;

  if (NullKind == Expr::NPCK_ZeroLiteral) {
    // In this case, check to make sure that we got here from a "NULL"
    // string in the source code.
    NullExpr = NullExpr->IgnoreParenImpCasts();
    SourceLocation loc = NullExpr->getExprLoc();
    if (!findMacroSpelling(loc, "NULL"))
      return false;
  }

  int DiagType = (NullKind == Expr::NPCK_CXX11_nullptr);
  Diag(QuestionLoc, diag::err_typecheck_cond_incompatible_operands_null)
      << NonPointerExpr->getType() << DiagType
      << NonPointerExpr->getSourceRange();
  return true;
}

/// Return false if the condition expression is valid, true otherwise.
static bool checkCondition(Sema &S, const Expr *Cond,
                           SourceLocation QuestionLoc) {
  QualType CondTy = Cond->getType();

  // OpenCL v1.1 s6.3.i says the condition cannot be a floating point type.
  if (S.getLangOpts().OpenCL && CondTy->isFloatingType()) {
    S.Diag(QuestionLoc, diag::err_typecheck_cond_expect_nonfloat)
      << CondTy << Cond->getSourceRange();
    return true;
  }

  // C99 6.5.15p2
  if (CondTy->isScalarType()) return false;

  S.Diag(QuestionLoc, diag::err_typecheck_cond_expect_scalar)
    << CondTy << Cond->getSourceRange();
  return true;
}

/// Return false if the NullExpr can be promoted to PointerTy,
/// true otherwise.
static bool checkConditionalNullPointer(Sema &S, ExprResult &NullExpr,
                                        QualType PointerTy) {
  if ((!PointerTy->isAnyPointerType() && !PointerTy->isBlockPointerType()) ||
      !NullExpr.get()->isNullPointerConstant(S.Context,
                                            Expr::NPC_ValueDependentIsNull))
    return true;

  NullExpr = S.ImpCastExprToType(NullExpr.get(), PointerTy, CK_NullToPointer);
  return false;
}

/// Checks compatibility between two pointers and return the resulting
/// type.
static QualType checkConditionalPointerCompatibility(Sema &S, ExprResult &LHS,
                                                     ExprResult &RHS,
                                                     SourceLocation Loc) {
  QualType LHSTy = LHS.get()->getType();
  QualType RHSTy = RHS.get()->getType();

  if (S.Context.hasSameType(LHSTy, RHSTy)) {
    // Two identical pointers types are always compatible.
    return S.Context.getCommonSugaredType(LHSTy, RHSTy);
  }

  QualType lhptee, rhptee;

  // Get the pointee types.
  bool IsBlockPointer = false;
  if (const BlockPointerType *LHSBTy = LHSTy->getAs<BlockPointerType>()) {
    lhptee = LHSBTy->getPointeeType();
    rhptee = RHSTy->castAs<BlockPointerType>()->getPointeeType();
    IsBlockPointer = true;
  } else {
    lhptee = LHSTy->castAs<PointerType>()->getPointeeType();
    rhptee = RHSTy->castAs<PointerType>()->getPointeeType();
  }

  // C99 6.5.15p6: If both operands are pointers to compatible types or to
  // differently qualified versions of compatible types, the result type is
  // a pointer to an appropriately qualified version of the composite
  // type.

  // Only CVR-qualifiers exist in the standard, and the differently-qualified
  // clause doesn't make sense for our extensions. E.g. address space 2 should
  // be incompatible with address space 3: they may live on different devices or
  // anything.
  Qualifiers lhQual = lhptee.getQualifiers();
  Qualifiers rhQual = rhptee.getQualifiers();

  LangAS ResultAddrSpace = LangAS::Default;
  LangAS LAddrSpace = lhQual.getAddressSpace();
  LangAS RAddrSpace = rhQual.getAddressSpace();

  // OpenCL v1.1 s6.5 - Conversion between pointers to distinct address
  // spaces is disallowed.
  if (lhQual.isAddressSpaceSupersetOf(rhQual))
    ResultAddrSpace = LAddrSpace;
  else if (rhQual.isAddressSpaceSupersetOf(lhQual))
    ResultAddrSpace = RAddrSpace;
  else {
    S.Diag(Loc, diag::err_typecheck_op_on_nonoverlapping_address_space_pointers)
        << LHSTy << RHSTy << 2 << LHS.get()->getSourceRange()
        << RHS.get()->getSourceRange();
    return QualType();
  }

  unsigned MergedCVRQual = lhQual.getCVRQualifiers() | rhQual.getCVRQualifiers();
  auto LHSCastKind = CK_BitCast, RHSCastKind = CK_BitCast;
  lhQual.removeCVRQualifiers();
  rhQual.removeCVRQualifiers();

  // OpenCL v2.0 specification doesn't extend compatibility of type qualifiers
  // (C99 6.7.3) for address spaces. We assume that the check should behave in
  // the same manner as it's defined for CVR qualifiers, so for OpenCL two
  // qual types are compatible iff
  //  * corresponded types are compatible
  //  * CVR qualifiers are equal
  //  * address spaces are equal
  // Thus for conditional operator we merge CVR and address space unqualified
  // pointees and if there is a composite type we return a pointer to it with
  // merged qualifiers.
  LHSCastKind =
      LAddrSpace == ResultAddrSpace ? CK_BitCast : CK_AddressSpaceConversion;
  RHSCastKind =
      RAddrSpace == ResultAddrSpace ? CK_BitCast : CK_AddressSpaceConversion;
  lhQual.removeAddressSpace();
  rhQual.removeAddressSpace();

  lhptee = S.Context.getQualifiedType(lhptee.getUnqualifiedType(), lhQual);
  rhptee = S.Context.getQualifiedType(rhptee.getUnqualifiedType(), rhQual);

  QualType CompositeTy = S.Context.mergeTypes(
      lhptee, rhptee, /*OfBlockPointer=*/false, /*Unqualified=*/false,
      /*BlockReturnType=*/false, /*IsConditionalOperator=*/true);

  if (CompositeTy.isNull()) {
    // In this situation, we assume void* type. No especially good
    // reason, but this is what gcc does, and we do have to pick
    // to get a consistent AST.
    QualType incompatTy;
    incompatTy = S.Context.getPointerType(
        S.Context.getAddrSpaceQualType(S.Context.VoidTy, ResultAddrSpace));
    LHS = S.ImpCastExprToType(LHS.get(), incompatTy, LHSCastKind);
    RHS = S.ImpCastExprToType(RHS.get(), incompatTy, RHSCastKind);

    // FIXME: For OpenCL the warning emission and cast to void* leaves a room
    // for casts between types with incompatible address space qualifiers.
    // For the following code the compiler produces casts between global and
    // local address spaces of the corresponded innermost pointees:
    // local int *global *a;
    // global int *global *b;
    // a = (0 ? a : b); // see C99 6.5.16.1.p1.
    S.Diag(Loc, diag::ext_typecheck_cond_incompatible_pointers)
        << LHSTy << RHSTy << LHS.get()->getSourceRange()
        << RHS.get()->getSourceRange();

    return incompatTy;
  }

  // The pointer types are compatible.
  // In case of OpenCL ResultTy should have the address space qualifier
  // which is a superset of address spaces of both the 2nd and the 3rd
  // operands of the conditional operator.
  QualType ResultTy = [&, ResultAddrSpace]() {
    if (S.getLangOpts().OpenCL) {
      Qualifiers CompositeQuals = CompositeTy.getQualifiers();
      CompositeQuals.setAddressSpace(ResultAddrSpace);
      return S.Context
          .getQualifiedType(CompositeTy.getUnqualifiedType(), CompositeQuals)
          .withCVRQualifiers(MergedCVRQual);
    }
    return CompositeTy.withCVRQualifiers(MergedCVRQual);
  }();
  if (IsBlockPointer)
    ResultTy = S.Context.getBlockPointerType(ResultTy);
  else
    ResultTy = S.Context.getPointerType(ResultTy);

  LHS = S.ImpCastExprToType(LHS.get(), ResultTy, LHSCastKind);
  RHS = S.ImpCastExprToType(RHS.get(), ResultTy, RHSCastKind);
  return ResultTy;
}

/// Return the resulting type when the operands are both block pointers.
static QualType checkConditionalBlockPointerCompatibility(Sema &S,
                                                          ExprResult &LHS,
                                                          ExprResult &RHS,
                                                          SourceLocation Loc) {
  QualType LHSTy = LHS.get()->getType();
  QualType RHSTy = RHS.get()->getType();

  if (!LHSTy->isBlockPointerType() || !RHSTy->isBlockPointerType()) {
    if (LHSTy->isVoidPointerType() || RHSTy->isVoidPointerType()) {
      QualType destType = S.Context.getPointerType(S.Context.VoidTy);
      LHS = S.ImpCastExprToType(LHS.get(), destType, CK_BitCast);
      RHS = S.ImpCastExprToType(RHS.get(), destType, CK_BitCast);
      return destType;
    }
    S.Diag(Loc, diag::err_typecheck_cond_incompatible_operands)
      << LHSTy << RHSTy << LHS.get()->getSourceRange()
      << RHS.get()->getSourceRange();
    return QualType();
  }

  // We have 2 block pointer types.
  return checkConditionalPointerCompatibility(S, LHS, RHS, Loc);
}

/// Return the resulting type when the operands are both pointers.
static QualType
checkConditionalObjectPointersCompatibility(Sema &S, ExprResult &LHS,
                                            ExprResult &RHS,
                                            SourceLocation Loc) {
  // get the pointer types
  QualType LHSTy = LHS.get()->getType();
  QualType RHSTy = RHS.get()->getType();

  // get the "pointed to" types
  QualType lhptee = LHSTy->castAs<PointerType>()->getPointeeType();
  QualType rhptee = RHSTy->castAs<PointerType>()->getPointeeType();

  // ignore qualifiers on void (C99 6.5.15p3, clause 6)
  if (lhptee->isVoidType() && rhptee->isIncompleteOrObjectType()) {
    // Figure out necessary qualifiers (C99 6.5.15p6)
    QualType destPointee
      = S.Context.getQualifiedType(lhptee, rhptee.getQualifiers());
    QualType destType = S.Context.getPointerType(destPointee);
    // Add qualifiers if necessary.
    LHS = S.ImpCastExprToType(LHS.get(), destType, CK_NoOp);
    // Promote to void*.
    RHS = S.ImpCastExprToType(RHS.get(), destType, CK_BitCast);
    return destType;
  }
  if (rhptee->isVoidType() && lhptee->isIncompleteOrObjectType()) {
    QualType destPointee
      = S.Context.getQualifiedType(rhptee, lhptee.getQualifiers());
    QualType destType = S.Context.getPointerType(destPointee);
    // Add qualifiers if necessary.
    RHS = S.ImpCastExprToType(RHS.get(), destType, CK_NoOp);
    // Promote to void*.
    LHS = S.ImpCastExprToType(LHS.get(), destType, CK_BitCast);
    return destType;
  }

  return checkConditionalPointerCompatibility(S, LHS, RHS, Loc);
}

/// Return false if the first expression is not an integer and the second
/// expression is not a pointer, true otherwise.
static bool checkPointerIntegerMismatch(Sema &S, ExprResult &Int,
                                        Expr* PointerExpr, SourceLocation Loc,
                                        bool IsIntFirstExpr) {
  if (!PointerExpr->getType()->isPointerType() ||
      !Int.get()->getType()->isIntegerType())
    return false;

  Expr *Expr1 = IsIntFirstExpr ? Int.get() : PointerExpr;
  Expr *Expr2 = IsIntFirstExpr ? PointerExpr : Int.get();

  S.Diag(Loc, diag::ext_typecheck_cond_pointer_integer_mismatch)
    << Expr1->getType() << Expr2->getType()
    << Expr1->getSourceRange() << Expr2->getSourceRange();
  Int = S.ImpCastExprToType(Int.get(), PointerExpr->getType(),
                            CK_IntegralToPointer);
  return true;
}

/// Simple conversion between integer and floating point types.
///
/// Used when handling the OpenCL conditional operator where the
/// condition is a vector while the other operands are scalar.
///
/// OpenCL v1.1 s6.3.i and s6.11.6 together require that the scalar
/// types are either integer or floating type. Between the two
/// operands, the type with the higher rank is defined as the "result
/// type". The other operand needs to be promoted to the same type. No
/// other type promotion is allowed. We cannot use
/// UsualArithmeticConversions() for this purpose, since it always
/// promotes promotable types.
static QualType OpenCLArithmeticConversions(Sema &S, ExprResult &LHS,
                                            ExprResult &RHS,
                                            SourceLocation QuestionLoc) {
  LHS = S.DefaultFunctionArrayLvalueConversion(LHS.get());
  if (LHS.isInvalid())
    return QualType();
  RHS = S.DefaultFunctionArrayLvalueConversion(RHS.get());
  if (RHS.isInvalid())
    return QualType();

  // For conversion purposes, we ignore any qualifiers.
  // For example, "const float" and "float" are equivalent.
  QualType LHSType =
    S.Context.getCanonicalType(LHS.get()->getType()).getUnqualifiedType();
  QualType RHSType =
    S.Context.getCanonicalType(RHS.get()->getType()).getUnqualifiedType();

  if (!LHSType->isIntegerType() && !LHSType->isRealFloatingType()) {
    S.Diag(QuestionLoc, diag::err_typecheck_cond_expect_int_float)
      << LHSType << LHS.get()->getSourceRange();
    return QualType();
  }

  if (!RHSType->isIntegerType() && !RHSType->isRealFloatingType()) {
    S.Diag(QuestionLoc, diag::err_typecheck_cond_expect_int_float)
      << RHSType << RHS.get()->getSourceRange();
    return QualType();
  }

  // If both types are identical, no conversion is needed.
  if (LHSType == RHSType)
    return LHSType;

  // Now handle "real" floating types (i.e. float, double, long double).
  if (LHSType->isRealFloatingType() || RHSType->isRealFloatingType())
    return handleFloatConversion(S, LHS, RHS, LHSType, RHSType,
                                 /*IsCompAssign = */ false);

  // Finally, we have two differing integer types.
  return handleIntegerConversion<doIntegralCast, doIntegralCast>
  (S, LHS, RHS, LHSType, RHSType, /*IsCompAssign = */ false);
}

/// Convert scalar operands to a vector that matches the
///        condition in length.
///
/// Used when handling the OpenCL conditional operator where the
/// condition is a vector while the other operands are scalar.
///
/// We first compute the "result type" for the scalar operands
/// according to OpenCL v1.1 s6.3.i. Both operands are then converted
/// into a vector of that type where the length matches the condition
/// vector type. s6.11.6 requires that the element types of the result
/// and the condition must have the same number of bits.
static QualType
OpenCLConvertScalarsToVectors(Sema &S, ExprResult &LHS, ExprResult &RHS,
                              QualType CondTy, SourceLocation QuestionLoc) {
  QualType ResTy = OpenCLArithmeticConversions(S, LHS, RHS, QuestionLoc);
  if (ResTy.isNull()) return QualType();

  const VectorType *CV = CondTy->getAs<VectorType>();
  assert(CV);

  // Determine the vector result type
  unsigned NumElements = CV->getNumElements();
  QualType VectorTy = S.Context.getExtVectorType(ResTy, NumElements);

  // Ensure that all types have the same number of bits
  if (S.Context.getTypeSize(CV->getElementType())
      != S.Context.getTypeSize(ResTy)) {
    // Since VectorTy is created internally, it does not pretty print
    // with an OpenCL name. Instead, we just print a description.
    std::string EleTyName = ResTy.getUnqualifiedType().getAsString();
    SmallString<64> Str;
    llvm::raw_svector_ostream OS(Str);
    OS << "(vector of " << NumElements << " '" << EleTyName << "' values)";
    S.Diag(QuestionLoc, diag::err_conditional_vector_element_size)
      << CondTy << OS.str();
    return QualType();
  }

  // Convert operands to the vector result type
  LHS = S.ImpCastExprToType(LHS.get(), VectorTy, CK_VectorSplat);
  RHS = S.ImpCastExprToType(RHS.get(), VectorTy, CK_VectorSplat);

  return VectorTy;
}

/// Return false if this is a valid OpenCL condition vector
static bool checkOpenCLConditionVector(Sema &S, Expr *Cond,
                                       SourceLocation QuestionLoc) {
  // OpenCL v1.1 s6.11.6 says the elements of the vector must be of
  // integral type.
  const VectorType *CondTy = Cond->getType()->getAs<VectorType>();
  assert(CondTy);
  QualType EleTy = CondTy->getElementType();
  if (EleTy->isIntegerType()) return false;

  S.Diag(QuestionLoc, diag::err_typecheck_cond_expect_nonfloat)
    << Cond->getType() << Cond->getSourceRange();
  return true;
}

/// Return false if the vector condition type and the vector
///        result type are compatible.
///
/// OpenCL v1.1 s6.11.6 requires that both vector types have the same
/// number of elements, and their element types have the same number
/// of bits.
static bool checkVectorResult(Sema &S, QualType CondTy, QualType VecResTy,
                              SourceLocation QuestionLoc) {
  const VectorType *CV = CondTy->getAs<VectorType>();
  const VectorType *RV = VecResTy->getAs<VectorType>();
  assert(CV && RV);

  if (CV->getNumElements() != RV->getNumElements()) {
    S.Diag(QuestionLoc, diag::err_conditional_vector_size)
      << CondTy << VecResTy;
    return true;
  }

  QualType CVE = CV->getElementType();
  QualType RVE = RV->getElementType();

  if (S.Context.getTypeSize(CVE) != S.Context.getTypeSize(RVE)) {
    S.Diag(QuestionLoc, diag::err_conditional_vector_element_size)
      << CondTy << VecResTy;
    return true;
  }

  return false;
}

/// Return the resulting type for the conditional operator in
///        OpenCL (aka "ternary selection operator", OpenCL v1.1
///        s6.3.i) when the condition is a vector type.
static QualType
OpenCLCheckVectorConditional(Sema &S, ExprResult &Cond,
                             ExprResult &LHS, ExprResult &RHS,
                             SourceLocation QuestionLoc) {
  Cond = S.DefaultFunctionArrayLvalueConversion(Cond.get());
  if (Cond.isInvalid())
    return QualType();
  QualType CondTy = Cond.get()->getType();

  if (checkOpenCLConditionVector(S, Cond.get(), QuestionLoc))
    return QualType();

  // If either operand is a vector then find the vector type of the
  // result as specified in OpenCL v1.1 s6.3.i.
  if (LHS.get()->getType()->isVectorType() ||
      RHS.get()->getType()->isVectorType()) {
    bool IsBoolVecLang =
        !S.getLangOpts().OpenCL && !S.getLangOpts().OpenCLCPlusPlus;
    QualType VecResTy =
        S.CheckVectorOperands(LHS, RHS, QuestionLoc,
                              /*isCompAssign*/ false,
                              /*AllowBothBool*/ true,
                              /*AllowBoolConversions*/ false,
                              /*AllowBooleanOperation*/ IsBoolVecLang,
                              /*ReportInvalid*/ true);
    if (VecResTy.isNull())
      return QualType();
    // The result type must match the condition type as specified in
    // OpenCL v1.1 s6.11.6.
    if (checkVectorResult(S, CondTy, VecResTy, QuestionLoc))
      return QualType();
    return VecResTy;
  }

  // Both operands are scalar.
  return OpenCLConvertScalarsToVectors(S, LHS, RHS, CondTy, QuestionLoc);
}

/// Return true if the Expr is block type
static bool checkBlockType(Sema &S, const Expr *E) {
  if (const CallExpr *CE = dyn_cast<CallExpr>(E)) {
    QualType Ty = CE->getCallee()->getType();
    if (Ty->isBlockPointerType()) {
      S.Diag(E->getExprLoc(), diag::err_opencl_ternary_with_block);
      return true;
    }
  }
  return false;
}

/// Note that LHS is not null here, even if this is the gnu "x ?: y" extension.
/// In that case, LHS = cond.
/// C99 6.5.15
QualType Sema::CheckConditionalOperands(ExprResult &Cond, ExprResult &LHS,
                                        ExprResult &RHS, ExprValueKind &VK,
                                        ExprObjectKind &OK,
                                        SourceLocation QuestionLoc) {

  ExprResult LHSResult = CheckPlaceholderExpr(LHS.get());
  if (!LHSResult.isUsable()) return QualType();
  LHS = LHSResult;

  ExprResult RHSResult = CheckPlaceholderExpr(RHS.get());
  if (!RHSResult.isUsable()) return QualType();
  RHS = RHSResult;

  // C++ is sufficiently different to merit its own checker.
  if (getLangOpts().CPlusPlus)
    return CXXCheckConditionalOperands(Cond, LHS, RHS, VK, OK, QuestionLoc);

  VK = VK_PRValue;
  OK = OK_Ordinary;

  if (Context.isDependenceAllowed() &&
      (Cond.get()->isTypeDependent() || LHS.get()->isTypeDependent() ||
       RHS.get()->isTypeDependent())) {
    assert(!getLangOpts().CPlusPlus);
    assert((Cond.get()->containsErrors() || LHS.get()->containsErrors() ||
            RHS.get()->containsErrors()) &&
           "should only occur in error-recovery path.");
    return Context.DependentTy;
  }

  // The OpenCL operator with a vector condition is sufficiently
  // different to merit its own checker.
  if ((getLangOpts().OpenCL && Cond.get()->getType()->isVectorType()) ||
      Cond.get()->getType()->isExtVectorType())
    return OpenCLCheckVectorConditional(*this, Cond, LHS, RHS, QuestionLoc);

  // First, check the condition.
  Cond = UsualUnaryConversions(Cond.get());
  if (Cond.isInvalid())
    return QualType();
  if (checkCondition(*this, Cond.get(), QuestionLoc))
    return QualType();

  // Handle vectors.
  if (LHS.get()->getType()->isVectorType() ||
      RHS.get()->getType()->isVectorType())
    return CheckVectorOperands(LHS, RHS, QuestionLoc, /*isCompAssign*/ false,
                               /*AllowBothBool*/ true,
                               /*AllowBoolConversions*/ false,
                               /*AllowBooleanOperation*/ false,
                               /*ReportInvalid*/ true);

  QualType ResTy =
      UsualArithmeticConversions(LHS, RHS, QuestionLoc, ACK_Conditional);
  if (LHS.isInvalid() || RHS.isInvalid())
    return QualType();

  // WebAssembly tables are not allowed as conditional LHS or RHS.
  QualType LHSTy = LHS.get()->getType();
  QualType RHSTy = RHS.get()->getType();
  if (LHSTy->isWebAssemblyTableType() || RHSTy->isWebAssemblyTableType()) {
    Diag(QuestionLoc, diag::err_wasm_table_conditional_expression)
        << LHS.get()->getSourceRange() << RHS.get()->getSourceRange();
    return QualType();
  }

  // Diagnose attempts to convert between __ibm128, __float128 and long double
  // where such conversions currently can't be handled.
  if (unsupportedTypeConversion(*this, LHSTy, RHSTy)) {
    Diag(QuestionLoc,
         diag::err_typecheck_cond_incompatible_operands) << LHSTy << RHSTy
      << LHS.get()->getSourceRange() << RHS.get()->getSourceRange();
    return QualType();
  }

  // OpenCL v2.0 s6.12.5 - Blocks cannot be used as expressions of the ternary
  // selection operator (?:).
  if (getLangOpts().OpenCL &&
      ((int)checkBlockType(*this, LHS.get()) | (int)checkBlockType(*this, RHS.get()))) {
    return QualType();
  }

  // If both operands have arithmetic type, do the usual arithmetic conversions
  // to find a common type: C99 6.5.15p3,5.
  if (LHSTy->isArithmeticType() && RHSTy->isArithmeticType()) {
    // Disallow invalid arithmetic conversions, such as those between bit-
    // precise integers types of different sizes, or between a bit-precise
    // integer and another type.
    if (ResTy.isNull() && (LHSTy->isBitIntType() || RHSTy->isBitIntType())) {
      Diag(QuestionLoc, diag::err_typecheck_cond_incompatible_operands)
          << LHSTy << RHSTy << LHS.get()->getSourceRange()
          << RHS.get()->getSourceRange();
      return QualType();
    }

    LHS = ImpCastExprToType(LHS.get(), ResTy, PrepareScalarCast(LHS, ResTy));
    RHS = ImpCastExprToType(RHS.get(), ResTy, PrepareScalarCast(RHS, ResTy));

    return ResTy;
  }

  // If both operands are the same structure or union type, the result is that
  // type.
  if (const RecordType *LHSRT = LHSTy->getAs<RecordType>()) {    // C99 6.5.15p3
    if (const RecordType *RHSRT = RHSTy->getAs<RecordType>())
      if (LHSRT->getDecl() == RHSRT->getDecl())
        // "If both the operands have structure or union type, the result has
        // that type."  This implies that CV qualifiers are dropped.
        return Context.getCommonSugaredType(LHSTy.getUnqualifiedType(),
                                            RHSTy.getUnqualifiedType());
    // FIXME: Type of conditional expression must be complete in C mode.
  }

  // C99 6.5.15p5: "If both operands have void type, the result has void type."
  // The following || allows only one side to be void (a GCC-ism).
  if (LHSTy->isVoidType() || RHSTy->isVoidType()) {
    QualType ResTy;
    if (LHSTy->isVoidType() && RHSTy->isVoidType()) {
      ResTy = Context.getCommonSugaredType(LHSTy, RHSTy);
    } else if (RHSTy->isVoidType()) {
      ResTy = RHSTy;
      Diag(RHS.get()->getBeginLoc(), diag::ext_typecheck_cond_one_void)
          << RHS.get()->getSourceRange();
    } else {
      ResTy = LHSTy;
      Diag(LHS.get()->getBeginLoc(), diag::ext_typecheck_cond_one_void)
          << LHS.get()->getSourceRange();
    }
    LHS = ImpCastExprToType(LHS.get(), ResTy, CK_ToVoid);
    RHS = ImpCastExprToType(RHS.get(), ResTy, CK_ToVoid);
    return ResTy;
  }

  // C23 6.5.15p7:
  //   ... if both the second and third operands have nullptr_t type, the
  //   result also has that type.
  if (LHSTy->isNullPtrType() && Context.hasSameType(LHSTy, RHSTy))
    return ResTy;

  // C99 6.5.15p6 - "if one operand is a null pointer constant, the result has
  // the type of the other operand."
  if (!checkConditionalNullPointer(*this, RHS, LHSTy)) return LHSTy;
  if (!checkConditionalNullPointer(*this, LHS, RHSTy)) return RHSTy;

  // All objective-c pointer type analysis is done here.
  QualType compositeType =
      ObjC().FindCompositeObjCPointerType(LHS, RHS, QuestionLoc);
  if (LHS.isInvalid() || RHS.isInvalid())
    return QualType();
  if (!compositeType.isNull())
    return compositeType;


  // Handle block pointer types.
  if (LHSTy->isBlockPointerType() || RHSTy->isBlockPointerType())
    return checkConditionalBlockPointerCompatibility(*this, LHS, RHS,
                                                     QuestionLoc);

  // Check constraints for C object pointers types (C99 6.5.15p3,6).
  if (LHSTy->isPointerType() && RHSTy->isPointerType())
    return checkConditionalObjectPointersCompatibility(*this, LHS, RHS,
                                                       QuestionLoc);

  // GCC compatibility: soften pointer/integer mismatch.  Note that
  // null pointers have been filtered out by this point.
  if (checkPointerIntegerMismatch(*this, LHS, RHS.get(), QuestionLoc,
      /*IsIntFirstExpr=*/true))
    return RHSTy;
  if (checkPointerIntegerMismatch(*this, RHS, LHS.get(), QuestionLoc,
      /*IsIntFirstExpr=*/false))
    return LHSTy;

  // Emit a better diagnostic if one of the expressions is a null pointer
  // constant and the other is not a pointer type. In this case, the user most
  // likely forgot to take the address of the other expression.
  if (DiagnoseConditionalForNull(LHS.get(), RHS.get(), QuestionLoc))
    return QualType();

  // Finally, if the LHS and RHS types are canonically the same type, we can
  // use the common sugared type.
  if (Context.hasSameType(LHSTy, RHSTy))
    return Context.getCommonSugaredType(LHSTy, RHSTy);

  // Otherwise, the operands are not compatible.
  Diag(QuestionLoc, diag::err_typecheck_cond_incompatible_operands)
    << LHSTy << RHSTy << LHS.get()->getSourceRange()
    << RHS.get()->getSourceRange();
  return QualType();
}

/// SuggestParentheses - Emit a note with a fixit hint that wraps
/// ParenRange in parentheses.
static void SuggestParentheses(Sema &Self, SourceLocation Loc,
                               const PartialDiagnostic &Note,
                               SourceRange ParenRange) {
  SourceLocation EndLoc = Self.getLocForEndOfToken(ParenRange.getEnd());
  if (ParenRange.getBegin().isFileID() && ParenRange.getEnd().isFileID() &&
      EndLoc.isValid()) {
    Self.Diag(Loc, Note)
      << FixItHint::CreateInsertion(ParenRange.getBegin(), "(")
      << FixItHint::CreateInsertion(EndLoc, ")");
  } else {
    // We can't display the parentheses, so just show the bare note.
    Self.Diag(Loc, Note) << ParenRange;
  }
}

static bool IsArithmeticOp(BinaryOperatorKind Opc) {
  return BinaryOperator::isAdditiveOp(Opc) ||
         BinaryOperator::isMultiplicativeOp(Opc) ||
         BinaryOperator::isShiftOp(Opc) || Opc == BO_And || Opc == BO_Or;
  // This only checks for bitwise-or and bitwise-and, but not bitwise-xor and
  // not any of the logical operators.  Bitwise-xor is commonly used as a
  // logical-xor because there is no logical-xor operator.  The logical
  // operators, including uses of xor, have a high false positive rate for
  // precedence warnings.
}

/// IsArithmeticBinaryExpr - Returns true if E is an arithmetic binary
/// expression, either using a built-in or overloaded operator,
/// and sets *OpCode to the opcode and *RHSExprs to the right-hand side
/// expression.
static bool IsArithmeticBinaryExpr(const Expr *E, BinaryOperatorKind *Opcode,
                                   const Expr **RHSExprs) {
  // Don't strip parenthesis: we should not warn if E is in parenthesis.
  E = E->IgnoreImpCasts();
  E = E->IgnoreConversionOperatorSingleStep();
  E = E->IgnoreImpCasts();
  if (const auto *MTE = dyn_cast<MaterializeTemporaryExpr>(E)) {
    E = MTE->getSubExpr();
    E = E->IgnoreImpCasts();
  }

  // Built-in binary operator.
  if (const auto *OP = dyn_cast<BinaryOperator>(E);
      OP && IsArithmeticOp(OP->getOpcode())) {
    *Opcode = OP->getOpcode();
    *RHSExprs = OP->getRHS();
    return true;
  }

  // Overloaded operator.
  if (const auto *Call = dyn_cast<CXXOperatorCallExpr>(E)) {
    if (Call->getNumArgs() != 2)
      return false;

    // Make sure this is really a binary operator that is safe to pass into
    // BinaryOperator::getOverloadedOpcode(), e.g. it's not a subscript op.
    OverloadedOperatorKind OO = Call->getOperator();
    if (OO < OO_Plus || OO > OO_Arrow ||
        OO == OO_PlusPlus || OO == OO_MinusMinus)
      return false;

    BinaryOperatorKind OpKind = BinaryOperator::getOverloadedOpcode(OO);
    if (IsArithmeticOp(OpKind)) {
      *Opcode = OpKind;
      *RHSExprs = Call->getArg(1);
      return true;
    }
  }

  return false;
}

/// ExprLooksBoolean - Returns true if E looks boolean, i.e. it has boolean type
/// or is a logical expression such as (x==y) which has int type, but is
/// commonly interpreted as boolean.
static bool ExprLooksBoolean(const Expr *E) {
  E = E->IgnoreParenImpCasts();

  if (E->getType()->isBooleanType())
    return true;
  if (const auto *OP = dyn_cast<BinaryOperator>(E))
    return OP->isComparisonOp() || OP->isLogicalOp();
  if (const auto *OP = dyn_cast<UnaryOperator>(E))
    return OP->getOpcode() == UO_LNot;
  if (E->getType()->isPointerType())
    return true;
  // FIXME: What about overloaded operator calls returning "unspecified boolean
  // type"s (commonly pointer-to-members)?

  return false;
}

/// DiagnoseConditionalPrecedence - Emit a warning when a conditional operator
/// and binary operator are mixed in a way that suggests the programmer assumed
/// the conditional operator has higher precedence, for example:
/// "int x = a + someBinaryCondition ? 1 : 2".
static void DiagnoseConditionalPrecedence(Sema &Self, SourceLocation OpLoc,
                                          Expr *Condition, const Expr *LHSExpr,
                                          const Expr *RHSExpr) {
  BinaryOperatorKind CondOpcode;
  const Expr *CondRHS;

  if (!IsArithmeticBinaryExpr(Condition, &CondOpcode, &CondRHS))
    return;
  if (!ExprLooksBoolean(CondRHS))
    return;

  // The condition is an arithmetic binary expression, with a right-
  // hand side that looks boolean, so warn.

  unsigned DiagID = BinaryOperator::isBitwiseOp(CondOpcode)
                        ? diag::warn_precedence_bitwise_conditional
                        : diag::warn_precedence_conditional;

  Self.Diag(OpLoc, DiagID)
      << Condition->getSourceRange()
      << BinaryOperator::getOpcodeStr(CondOpcode);

  SuggestParentheses(
      Self, OpLoc,
      Self.PDiag(diag::note_precedence_silence)
          << BinaryOperator::getOpcodeStr(CondOpcode),
      SourceRange(Condition->getBeginLoc(), Condition->getEndLoc()));

  SuggestParentheses(Self, OpLoc,
                     Self.PDiag(diag::note_precedence_conditional_first),
                     SourceRange(CondRHS->getBeginLoc(), RHSExpr->getEndLoc()));
}

/// Compute the nullability of a conditional expression.
static QualType computeConditionalNullability(QualType ResTy, bool IsBin,
                                              QualType LHSTy, QualType RHSTy,
                                              ASTContext &Ctx) {
  if (!ResTy->isAnyPointerType())
    return ResTy;

  auto GetNullability = [](QualType Ty) {
    std::optional<NullabilityKind> Kind = Ty->getNullability();
    if (Kind) {
      // For our purposes, treat _Nullable_result as _Nullable.
      if (*Kind == NullabilityKind::NullableResult)
        return NullabilityKind::Nullable;
      return *Kind;
    }
    return NullabilityKind::Unspecified;
  };

  auto LHSKind = GetNullability(LHSTy), RHSKind = GetNullability(RHSTy);
  NullabilityKind MergedKind;

  // Compute nullability of a binary conditional expression.
  if (IsBin) {
    if (LHSKind == NullabilityKind::NonNull)
      MergedKind = NullabilityKind::NonNull;
    else
      MergedKind = RHSKind;
  // Compute nullability of a normal conditional expression.
  } else {
    if (LHSKind == NullabilityKind::Nullable ||
        RHSKind == NullabilityKind::Nullable)
      MergedKind = NullabilityKind::Nullable;
    else if (LHSKind == NullabilityKind::NonNull)
      MergedKind = RHSKind;
    else if (RHSKind == NullabilityKind::NonNull)
      MergedKind = LHSKind;
    else
      MergedKind = NullabilityKind::Unspecified;
  }

  // Return if ResTy already has the correct nullability.
  if (GetNullability(ResTy) == MergedKind)
    return ResTy;

  // Strip all nullability from ResTy.
  while (ResTy->getNullability())
    ResTy = ResTy.getSingleStepDesugaredType(Ctx);

  // Create a new AttributedType with the new nullability kind.
  auto NewAttr = AttributedType::getNullabilityAttrKind(MergedKind);
  return Ctx.getAttributedType(NewAttr, ResTy, ResTy);
}

ExprResult Sema::ActOnConditionalOp(SourceLocation QuestionLoc,
                                    SourceLocation ColonLoc,
                                    Expr *CondExpr, Expr *LHSExpr,
                                    Expr *RHSExpr) {
  if (!Context.isDependenceAllowed()) {
    // C cannot handle TypoExpr nodes in the condition because it
    // doesn't handle dependent types properly, so make sure any TypoExprs have
    // been dealt with before checking the operands.
    ExprResult CondResult = CorrectDelayedTyposInExpr(CondExpr);
    ExprResult LHSResult = CorrectDelayedTyposInExpr(LHSExpr);
    ExprResult RHSResult = CorrectDelayedTyposInExpr(RHSExpr);

    if (!CondResult.isUsable())
      return ExprError();

    if (LHSExpr) {
      if (!LHSResult.isUsable())
        return ExprError();
    }

    if (!RHSResult.isUsable())
      return ExprError();

    CondExpr = CondResult.get();
    LHSExpr = LHSResult.get();
    RHSExpr = RHSResult.get();
  }

  // If this is the gnu "x ?: y" extension, analyze the types as though the LHS
  // was the condition.
  OpaqueValueExpr *opaqueValue = nullptr;
  Expr *commonExpr = nullptr;
  if (!LHSExpr) {
    commonExpr = CondExpr;
    // Lower out placeholder types first.  This is important so that we don't
    // try to capture a placeholder. This happens in few cases in C++; such
    // as Objective-C++'s dictionary subscripting syntax.
    if (commonExpr->hasPlaceholderType()) {
      ExprResult result = CheckPlaceholderExpr(commonExpr);
      if (!result.isUsable()) return ExprError();
      commonExpr = result.get();
    }
    // We usually want to apply unary conversions *before* saving, except
    // in the special case of a C++ l-value conditional.
    if (!(getLangOpts().CPlusPlus
          && !commonExpr->isTypeDependent()
          && commonExpr->getValueKind() == RHSExpr->getValueKind()
          && commonExpr->isGLValue()
          && commonExpr->isOrdinaryOrBitFieldObject()
          && RHSExpr->isOrdinaryOrBitFieldObject()
          && Context.hasSameType(commonExpr->getType(), RHSExpr->getType()))) {
      ExprResult commonRes = UsualUnaryConversions(commonExpr);
      if (commonRes.isInvalid())
        return ExprError();
      commonExpr = commonRes.get();
    }

    // If the common expression is a class or array prvalue, materialize it
    // so that we can safely refer to it multiple times.
    if (commonExpr->isPRValue() && (commonExpr->getType()->isRecordType() ||
                                    commonExpr->getType()->isArrayType())) {
      ExprResult MatExpr = TemporaryMaterializationConversion(commonExpr);
      if (MatExpr.isInvalid())
        return ExprError();
      commonExpr = MatExpr.get();
    }

    opaqueValue = new (Context) OpaqueValueExpr(commonExpr->getExprLoc(),
                                                commonExpr->getType(),
                                                commonExpr->getValueKind(),
                                                commonExpr->getObjectKind(),
                                                commonExpr);
    LHSExpr = CondExpr = opaqueValue;
  }

  QualType LHSTy = LHSExpr->getType(), RHSTy = RHSExpr->getType();
  ExprValueKind VK = VK_PRValue;
  ExprObjectKind OK = OK_Ordinary;
  ExprResult Cond = CondExpr, LHS = LHSExpr, RHS = RHSExpr;
  QualType result = CheckConditionalOperands(Cond, LHS, RHS,
                                             VK, OK, QuestionLoc);
  if (result.isNull() || Cond.isInvalid() || LHS.isInvalid() ||
      RHS.isInvalid())
    return ExprError();

  DiagnoseConditionalPrecedence(*this, QuestionLoc, Cond.get(), LHS.get(),
                                RHS.get());

  CheckBoolLikeConversion(Cond.get(), QuestionLoc);

  result = computeConditionalNullability(result, commonExpr, LHSTy, RHSTy,
                                         Context);

  if (!commonExpr)
    return new (Context)
        ConditionalOperator(Cond.get(), QuestionLoc, LHS.get(), ColonLoc,
                            RHS.get(), result, VK, OK);

  return new (Context) BinaryConditionalOperator(
      commonExpr, opaqueValue, Cond.get(), LHS.get(), RHS.get(), QuestionLoc,
      ColonLoc, result, VK, OK);
}

bool Sema::IsInvalidSMECallConversion(QualType FromType, QualType ToType) {
  unsigned FromAttributes = 0, ToAttributes = 0;
  if (const auto *FromFn =
          dyn_cast<FunctionProtoType>(Context.getCanonicalType(FromType)))
    FromAttributes =
        FromFn->getAArch64SMEAttributes() & FunctionType::SME_AttributeMask;
  if (const auto *ToFn =
          dyn_cast<FunctionProtoType>(Context.getCanonicalType(ToType)))
    ToAttributes =
        ToFn->getAArch64SMEAttributes() & FunctionType::SME_AttributeMask;

  return FromAttributes != ToAttributes;
}

// Check if we have a conversion between incompatible cmse function pointer
// types, that is, a conversion between a function pointer with the
// cmse_nonsecure_call attribute and one without.
static bool IsInvalidCmseNSCallConversion(Sema &S, QualType FromType,
                                          QualType ToType) {
  if (const auto *ToFn =
          dyn_cast<FunctionType>(S.Context.getCanonicalType(ToType))) {
    if (const auto *FromFn =
            dyn_cast<FunctionType>(S.Context.getCanonicalType(FromType))) {
      FunctionType::ExtInfo ToEInfo = ToFn->getExtInfo();
      FunctionType::ExtInfo FromEInfo = FromFn->getExtInfo();

      return ToEInfo.getCmseNSCall() != FromEInfo.getCmseNSCall();
    }
  }
  return false;
}

// checkPointerTypesForAssignment - This is a very tricky routine (despite
// being closely modeled after the C99 spec:-). The odd characteristic of this
// routine is it effectively iqnores the qualifiers on the top level pointee.
// This circumvents the usual type rules specified in 6.2.7p1 & 6.7.5.[1-3].
// FIXME: add a couple examples in this comment.
static Sema::AssignConvertType
checkPointerTypesForAssignment(Sema &S, QualType LHSType, QualType RHSType,
                               SourceLocation Loc) {
  assert(LHSType.isCanonical() && "LHS not canonicalized!");
  assert(RHSType.isCanonical() && "RHS not canonicalized!");

  // get the "pointed to" type (ignoring qualifiers at the top level)
  const Type *lhptee, *rhptee;
  Qualifiers lhq, rhq;
  std::tie(lhptee, lhq) =
      cast<PointerType>(LHSType)->getPointeeType().split().asPair();
  std::tie(rhptee, rhq) =
      cast<PointerType>(RHSType)->getPointeeType().split().asPair();

  Sema::AssignConvertType ConvTy = Sema::Compatible;

  // C99 6.5.16.1p1: This following citation is common to constraints
  // 3 & 4 (below). ...and the type *pointed to* by the left has all the
  // qualifiers of the type *pointed to* by the right;

  // As a special case, 'non-__weak A *' -> 'non-__weak const *' is okay.
  if (lhq.getObjCLifetime() != rhq.getObjCLifetime() &&
      lhq.compatiblyIncludesObjCLifetime(rhq)) {
    // Ignore lifetime for further calculation.
    lhq.removeObjCLifetime();
    rhq.removeObjCLifetime();
  }

  if (!lhq.compatiblyIncludes(rhq)) {
    // Treat address-space mismatches as fatal.
    if (!lhq.isAddressSpaceSupersetOf(rhq))
      return Sema::IncompatiblePointerDiscardsQualifiers;

    // It's okay to add or remove GC or lifetime qualifiers when converting to
    // and from void*.
    else if (lhq.withoutObjCGCAttr().withoutObjCLifetime()
                        .compatiblyIncludes(
                                rhq.withoutObjCGCAttr().withoutObjCLifetime())
             && (lhptee->isVoidType() || rhptee->isVoidType()))
      ; // keep old

    // Treat lifetime mismatches as fatal.
    else if (lhq.getObjCLifetime() != rhq.getObjCLifetime())
      ConvTy = Sema::IncompatiblePointerDiscardsQualifiers;

    // For GCC/MS compatibility, other qualifier mismatches are treated
    // as still compatible in C.
    else ConvTy = Sema::CompatiblePointerDiscardsQualifiers;
  }

  // C99 6.5.16.1p1 (constraint 4): If one operand is a pointer to an object or
  // incomplete type and the other is a pointer to a qualified or unqualified
  // version of void...
  if (lhptee->isVoidType()) {
    if (rhptee->isIncompleteOrObjectType())
      return ConvTy;

    // As an extension, we allow cast to/from void* to function pointer.
    assert(rhptee->isFunctionType());
    return Sema::FunctionVoidPointer;
  }

  if (rhptee->isVoidType()) {
    if (lhptee->isIncompleteOrObjectType())
      return ConvTy;

    // As an extension, we allow cast to/from void* to function pointer.
    assert(lhptee->isFunctionType());
    return Sema::FunctionVoidPointer;
  }

  if (!S.Diags.isIgnored(
          diag::warn_typecheck_convert_incompatible_function_pointer_strict,
          Loc) &&
      RHSType->isFunctionPointerType() && LHSType->isFunctionPointerType() &&
      !S.IsFunctionConversion(RHSType, LHSType, RHSType))
    return Sema::IncompatibleFunctionPointerStrict;

  // C99 6.5.16.1p1 (constraint 3): both operands are pointers to qualified or
  // unqualified versions of compatible types, ...
  QualType ltrans = QualType(lhptee, 0), rtrans = QualType(rhptee, 0);
  if (!S.Context.typesAreCompatible(ltrans, rtrans)) {
    // Check if the pointee types are compatible ignoring the sign.
    // We explicitly check for char so that we catch "char" vs
    // "unsigned char" on systems where "char" is unsigned.
    if (lhptee->isCharType())
      ltrans = S.Context.UnsignedCharTy;
    else if (lhptee->hasSignedIntegerRepresentation())
      ltrans = S.Context.getCorrespondingUnsignedType(ltrans);

    if (rhptee->isCharType())
      rtrans = S.Context.UnsignedCharTy;
    else if (rhptee->hasSignedIntegerRepresentation())
      rtrans = S.Context.getCorrespondingUnsignedType(rtrans);

    if (ltrans == rtrans) {
      // Types are compatible ignoring the sign. Qualifier incompatibility
      // takes priority over sign incompatibility because the sign
      // warning can be disabled.
      if (ConvTy != Sema::Compatible)
        return ConvTy;

      return Sema::IncompatiblePointerSign;
    }

    // If we are a multi-level pointer, it's possible that our issue is simply
    // one of qualification - e.g. char ** -> const char ** is not allowed. If
    // the eventual target type is the same and the pointers have the same
    // level of indirection, this must be the issue.
    if (isa<PointerType>(lhptee) && isa<PointerType>(rhptee)) {
      do {
        std::tie(lhptee, lhq) =
          cast<PointerType>(lhptee)->getPointeeType().split().asPair();
        std::tie(rhptee, rhq) =
          cast<PointerType>(rhptee)->getPointeeType().split().asPair();

        // Inconsistent address spaces at this point is invalid, even if the
        // address spaces would be compatible.
        // FIXME: This doesn't catch address space mismatches for pointers of
        // different nesting levels, like:
        //   __local int *** a;
        //   int ** b = a;
        // It's not clear how to actually determine when such pointers are
        // invalidly incompatible.
        if (lhq.getAddressSpace() != rhq.getAddressSpace())
          return Sema::IncompatibleNestedPointerAddressSpaceMismatch;

      } while (isa<PointerType>(lhptee) && isa<PointerType>(rhptee));

      if (lhptee == rhptee)
        return Sema::IncompatibleNestedPointerQualifiers;
    }

    // General pointer incompatibility takes priority over qualifiers.
    if (RHSType->isFunctionPointerType() && LHSType->isFunctionPointerType())
      return Sema::IncompatibleFunctionPointer;
    return Sema::IncompatiblePointer;
  }
  if (!S.getLangOpts().CPlusPlus &&
      S.IsFunctionConversion(ltrans, rtrans, ltrans))
    return Sema::IncompatibleFunctionPointer;
  if (IsInvalidCmseNSCallConversion(S, ltrans, rtrans))
    return Sema::IncompatibleFunctionPointer;
  if (S.IsInvalidSMECallConversion(rtrans, ltrans))
    return Sema::IncompatibleFunctionPointer;
  return ConvTy;
}

/// checkBlockPointerTypesForAssignment - This routine determines whether two
/// block pointer types are compatible or whether a block and normal pointer
/// are compatible. It is more restrict than comparing two function pointer
// types.
static Sema::AssignConvertType
checkBlockPointerTypesForAssignment(Sema &S, QualType LHSType,
                                    QualType RHSType) {
  assert(LHSType.isCanonical() && "LHS not canonicalized!");
  assert(RHSType.isCanonical() && "RHS not canonicalized!");

  QualType lhptee, rhptee;

  // get the "pointed to" type (ignoring qualifiers at the top level)
  lhptee = cast<BlockPointerType>(LHSType)->getPointeeType();
  rhptee = cast<BlockPointerType>(RHSType)->getPointeeType();

  // In C++, the types have to match exactly.
  if (S.getLangOpts().CPlusPlus)
    return Sema::IncompatibleBlockPointer;

  Sema::AssignConvertType ConvTy = Sema::Compatible;

  // For blocks we enforce that qualifiers are identical.
  Qualifiers LQuals = lhptee.getLocalQualifiers();
  Qualifiers RQuals = rhptee.getLocalQualifiers();
  if (S.getLangOpts().OpenCL) {
    LQuals.removeAddressSpace();
    RQuals.removeAddressSpace();
  }
  if (LQuals != RQuals)
    ConvTy = Sema::CompatiblePointerDiscardsQualifiers;

  // FIXME: OpenCL doesn't define the exact compile time semantics for a block
  // assignment.
  // The current behavior is similar to C++ lambdas. A block might be
  // assigned to a variable iff its return type and parameters are compatible
  // (C99 6.2.7) with the corresponding return type and parameters of the LHS of
  // an assignment. Presumably it should behave in way that a function pointer
  // assignment does in C, so for each parameter and return type:
  //  * CVR and address space of LHS should be a superset of CVR and address
  //  space of RHS.
  //  * unqualified types should be compatible.
  if (S.getLangOpts().OpenCL) {
    if (!S.Context.typesAreBlockPointerCompatible(
            S.Context.getQualifiedType(LHSType.getUnqualifiedType(), LQuals),
            S.Context.getQualifiedType(RHSType.getUnqualifiedType(), RQuals)))
      return Sema::IncompatibleBlockPointer;
  } else if (!S.Context.typesAreBlockPointerCompatible(LHSType, RHSType))
    return Sema::IncompatibleBlockPointer;

  return ConvTy;
}

/// checkObjCPointerTypesForAssignment - Compares two objective-c pointer types
/// for assignment compatibility.
static Sema::AssignConvertType
checkObjCPointerTypesForAssignment(Sema &S, QualType LHSType,
                                   QualType RHSType) {
  assert(LHSType.isCanonical() && "LHS was not canonicalized!");
  assert(RHSType.isCanonical() && "RHS was not canonicalized!");

  if (LHSType->isObjCBuiltinType()) {
    // Class is not compatible with ObjC object pointers.
    if (LHSType->isObjCClassType() && !RHSType->isObjCBuiltinType() &&
        !RHSType->isObjCQualifiedClassType())
      return Sema::IncompatiblePointer;
    return Sema::Compatible;
  }
  if (RHSType->isObjCBuiltinType()) {
    if (RHSType->isObjCClassType() && !LHSType->isObjCBuiltinType() &&
        !LHSType->isObjCQualifiedClassType())
      return Sema::IncompatiblePointer;
    return Sema::Compatible;
  }
  QualType lhptee = LHSType->castAs<ObjCObjectPointerType>()->getPointeeType();
  QualType rhptee = RHSType->castAs<ObjCObjectPointerType>()->getPointeeType();

  if (!lhptee.isAtLeastAsQualifiedAs(rhptee) &&
      // make an exception for id<P>
      !LHSType->isObjCQualifiedIdType())
    return Sema::CompatiblePointerDiscardsQualifiers;

  if (S.Context.typesAreCompatible(LHSType, RHSType))
    return Sema::Compatible;
  if (LHSType->isObjCQualifiedIdType() || RHSType->isObjCQualifiedIdType())
    return Sema::IncompatibleObjCQualifiedId;
  return Sema::IncompatiblePointer;
}

Sema::AssignConvertType
Sema::CheckAssignmentConstraints(SourceLocation Loc,
                                 QualType LHSType, QualType RHSType) {
  // Fake up an opaque expression.  We don't actually care about what
  // cast operations are required, so if CheckAssignmentConstraints
  // adds casts to this they'll be wasted, but fortunately that doesn't
  // usually happen on valid code.
  OpaqueValueExpr RHSExpr(Loc, RHSType, VK_PRValue);
  ExprResult RHSPtr = &RHSExpr;
  CastKind K;

  return CheckAssignmentConstraints(LHSType, RHSPtr, K, /*ConvertRHS=*/false);
}

/// This helper function returns true if QT is a vector type that has element
/// type ElementType.
static bool isVector(QualType QT, QualType ElementType) {
  if (const VectorType *VT = QT->getAs<VectorType>())
    return VT->getElementType().getCanonicalType() == ElementType;
  return false;
}

/// CheckAssignmentConstraints (C99 6.5.16) - This routine currently
/// has code to accommodate several GCC extensions when type checking
/// pointers. Here are some objectionable examples that GCC considers warnings:
///
///  int a, *pint;
///  short *pshort;
///  struct foo *pfoo;
///
///  pint = pshort; // warning: assignment from incompatible pointer type
///  a = pint; // warning: assignment makes integer from pointer without a cast
///  pint = a; // warning: assignment makes pointer from integer without a cast
///  pint = pfoo; // warning: assignment from incompatible pointer type
///
/// As a result, the code for dealing with pointers is more complex than the
/// C99 spec dictates.
///
/// Sets 'Kind' for any result kind except Incompatible.
Sema::AssignConvertType
Sema::CheckAssignmentConstraints(QualType LHSType, ExprResult &RHS,
                                 CastKind &Kind, bool ConvertRHS) {
  QualType RHSType = RHS.get()->getType();
  QualType OrigLHSType = LHSType;

  // Get canonical types.  We're not formatting these types, just comparing
  // them.
  LHSType = Context.getCanonicalType(LHSType).getUnqualifiedType();
  RHSType = Context.getCanonicalType(RHSType).getUnqualifiedType();

  // Common case: no conversion required.
  if (LHSType == RHSType) {
    Kind = CK_NoOp;
    return Compatible;
  }

  // If the LHS has an __auto_type, there are no additional type constraints
  // to be worried about.
  if (const auto *AT = dyn_cast<AutoType>(LHSType)) {
    if (AT->isGNUAutoType()) {
      Kind = CK_NoOp;
      return Compatible;
    }
  }

  // If we have an atomic type, try a non-atomic assignment, then just add an
  // atomic qualification step.
  if (const AtomicType *AtomicTy = dyn_cast<AtomicType>(LHSType)) {
    Sema::AssignConvertType result =
      CheckAssignmentConstraints(AtomicTy->getValueType(), RHS, Kind);
    if (result != Compatible)
      return result;
    if (Kind != CK_NoOp && ConvertRHS)
      RHS = ImpCastExprToType(RHS.get(), AtomicTy->getValueType(), Kind);
    Kind = CK_NonAtomicToAtomic;
    return Compatible;
  }

  // If the left-hand side is a reference type, then we are in a
  // (rare!) case where we've allowed the use of references in C,
  // e.g., as a parameter type in a built-in function. In this case,
  // just make sure that the type referenced is compatible with the
  // right-hand side type. The caller is responsible for adjusting
  // LHSType so that the resulting expression does not have reference
  // type.
  if (const ReferenceType *LHSTypeRef = LHSType->getAs<ReferenceType>()) {
    if (Context.typesAreCompatible(LHSTypeRef->getPointeeType(), RHSType)) {
      Kind = CK_LValueBitCast;
      return Compatible;
    }
    return Incompatible;
  }

  // Allow scalar to ExtVector assignments, and assignments of an ExtVector type
  // to the same ExtVector type.
  if (LHSType->isExtVectorType()) {
    if (RHSType->isExtVectorType())
      return Incompatible;
    if (RHSType->isArithmeticType()) {
      // CK_VectorSplat does T -> vector T, so first cast to the element type.
      if (ConvertRHS)
        RHS = prepareVectorSplat(LHSType, RHS.get());
      Kind = CK_VectorSplat;
      return Compatible;
    }
  }

  // Conversions to or from vector type.
  if (LHSType->isVectorType() || RHSType->isVectorType()) {
    if (LHSType->isVectorType() && RHSType->isVectorType()) {
      // Allow assignments of an AltiVec vector type to an equivalent GCC
      // vector type and vice versa
      if (Context.areCompatibleVectorTypes(LHSType, RHSType)) {
        Kind = CK_BitCast;
        return Compatible;
      }

      // If we are allowing lax vector conversions, and LHS and RHS are both
      // vectors, the total size only needs to be the same. This is a bitcast;
      // no bits are changed but the result type is different.
      if (isLaxVectorConversion(RHSType, LHSType)) {
        // The default for lax vector conversions with Altivec vectors will
        // change, so if we are converting between vector types where
        // at least one is an Altivec vector, emit a warning.
        if (Context.getTargetInfo().getTriple().isPPC() &&
            anyAltivecTypes(RHSType, LHSType) &&
            !Context.areCompatibleVectorTypes(RHSType, LHSType))
          Diag(RHS.get()->getExprLoc(), diag::warn_deprecated_lax_vec_conv_all)
              << RHSType << LHSType;
        Kind = CK_BitCast;
        return IncompatibleVectors;
      }
    }

    // When the RHS comes from another lax conversion (e.g. binops between
    // scalars and vectors) the result is canonicalized as a vector. When the
    // LHS is also a vector, the lax is allowed by the condition above. Handle
    // the case where LHS is a scalar.
    if (LHSType->isScalarType()) {
      const VectorType *VecType = RHSType->getAs<VectorType>();
      if (VecType && VecType->getNumElements() == 1 &&
          isLaxVectorConversion(RHSType, LHSType)) {
        if (Context.getTargetInfo().getTriple().isPPC() &&
            (VecType->getVectorKind() == VectorKind::AltiVecVector ||
             VecType->getVectorKind() == VectorKind::AltiVecBool ||
             VecType->getVectorKind() == VectorKind::AltiVecPixel))
          Diag(RHS.get()->getExprLoc(), diag::warn_deprecated_lax_vec_conv_all)
              << RHSType << LHSType;
        ExprResult *VecExpr = &RHS;
        *VecExpr = ImpCastExprToType(VecExpr->get(), LHSType, CK_BitCast);
        Kind = CK_BitCast;
        return Compatible;
      }
    }

    // Allow assignments between fixed-length and sizeless SVE vectors.
    if ((LHSType->isSVESizelessBuiltinType() && RHSType->isVectorType()) ||
        (LHSType->isVectorType() && RHSType->isSVESizelessBuiltinType()))
      if (Context.areCompatibleSveTypes(LHSType, RHSType) ||
          Context.areLaxCompatibleSveTypes(LHSType, RHSType)) {
        Kind = CK_BitCast;
        return Compatible;
      }

    // Allow assignments between fixed-length and sizeless RVV vectors.
    if ((LHSType->isRVVSizelessBuiltinType() && RHSType->isVectorType()) ||
        (LHSType->isVectorType() && RHSType->isRVVSizelessBuiltinType())) {
      if (Context.areCompatibleRVVTypes(LHSType, RHSType) ||
          Context.areLaxCompatibleRVVTypes(LHSType, RHSType)) {
        Kind = CK_BitCast;
        return Compatible;
      }
    }

    return Incompatible;
  }

  // Diagnose attempts to convert between __ibm128, __float128 and long double
  // where such conversions currently can't be handled.
  if (unsupportedTypeConversion(*this, LHSType, RHSType))
    return Incompatible;

  // Disallow assigning a _Complex to a real type in C++ mode since it simply
  // discards the imaginary part.
  if (getLangOpts().CPlusPlus && RHSType->getAs<ComplexType>() &&
      !LHSType->getAs<ComplexType>())
    return Incompatible;

  // Arithmetic conversions.
  if (LHSType->isArithmeticType() && RHSType->isArithmeticType() &&
      !(getLangOpts().CPlusPlus && LHSType->isEnumeralType())) {
    if (ConvertRHS)
      Kind = PrepareScalarCast(RHS, LHSType);
    return Compatible;
  }

  // Conversions to normal pointers.
  if (const PointerType *LHSPointer = dyn_cast<PointerType>(LHSType)) {
    // U* -> T*
    if (isa<PointerType>(RHSType)) {
      LangAS AddrSpaceL = LHSPointer->getPointeeType().getAddressSpace();
      LangAS AddrSpaceR = RHSType->getPointeeType().getAddressSpace();
      if (AddrSpaceL != AddrSpaceR)
        Kind = CK_AddressSpaceConversion;
      else if (Context.hasCvrSimilarType(RHSType, LHSType))
        Kind = CK_NoOp;
      else
        Kind = CK_BitCast;
      return checkPointerTypesForAssignment(*this, LHSType, RHSType,
                                            RHS.get()->getBeginLoc());
    }

    // int -> T*
    if (RHSType->isIntegerType()) {
      Kind = CK_IntegralToPointer; // FIXME: null?
      return IntToPointer;
    }

    // C pointers are not compatible with ObjC object pointers,
    // with two exceptions:
    if (isa<ObjCObjectPointerType>(RHSType)) {
      //  - conversions to void*
      if (LHSPointer->getPointeeType()->isVoidType()) {
        Kind = CK_BitCast;
        return Compatible;
      }

      //  - conversions from 'Class' to the redefinition type
      if (RHSType->isObjCClassType() &&
          Context.hasSameType(LHSType,
                              Context.getObjCClassRedefinitionType())) {
        Kind = CK_BitCast;
        return Compatible;
      }

      Kind = CK_BitCast;
      return IncompatiblePointer;
    }

    // U^ -> void*
    if (RHSType->getAs<BlockPointerType>()) {
      if (LHSPointer->getPointeeType()->isVoidType()) {
        LangAS AddrSpaceL = LHSPointer->getPointeeType().getAddressSpace();
        LangAS AddrSpaceR = RHSType->getAs<BlockPointerType>()
                                ->getPointeeType()
                                .getAddressSpace();
        Kind =
            AddrSpaceL != AddrSpaceR ? CK_AddressSpaceConversion : CK_BitCast;
        return Compatible;
      }
    }

    return Incompatible;
  }

  // Conversions to block pointers.
  if (isa<BlockPointerType>(LHSType)) {
    // U^ -> T^
    if (RHSType->isBlockPointerType()) {
      LangAS AddrSpaceL = LHSType->getAs<BlockPointerType>()
                              ->getPointeeType()
                              .getAddressSpace();
      LangAS AddrSpaceR = RHSType->getAs<BlockPointerType>()
                              ->getPointeeType()
                              .getAddressSpace();
      Kind = AddrSpaceL != AddrSpaceR ? CK_AddressSpaceConversion : CK_BitCast;
      return checkBlockPointerTypesForAssignment(*this, LHSType, RHSType);
    }

    // int or null -> T^
    if (RHSType->isIntegerType()) {
      Kind = CK_IntegralToPointer; // FIXME: null
      return IntToBlockPointer;
    }

    // id -> T^
    if (getLangOpts().ObjC && RHSType->isObjCIdType()) {
      Kind = CK_AnyPointerToBlockPointerCast;
      return Compatible;
    }

    // void* -> T^
    if (const PointerType *RHSPT = RHSType->getAs<PointerType>())
      if (RHSPT->getPointeeType()->isVoidType()) {
        Kind = CK_AnyPointerToBlockPointerCast;
        return Compatible;
      }

    return Incompatible;
  }

  // Conversions to Objective-C pointers.
  if (isa<ObjCObjectPointerType>(LHSType)) {
    // A* -> B*
    if (RHSType->isObjCObjectPointerType()) {
      Kind = CK_BitCast;
      Sema::AssignConvertType result =
        checkObjCPointerTypesForAssignment(*this, LHSType, RHSType);
      if (getLangOpts().allowsNonTrivialObjCLifetimeQualifiers() &&
          result == Compatible &&
          !ObjC().CheckObjCARCUnavailableWeakConversion(OrigLHSType, RHSType))
        result = IncompatibleObjCWeakRef;
      return result;
    }

    // int or null -> A*
    if (RHSType->isIntegerType()) {
      Kind = CK_IntegralToPointer; // FIXME: null
      return IntToPointer;
    }

    // In general, C pointers are not compatible with ObjC object pointers,
    // with two exceptions:
    if (isa<PointerType>(RHSType)) {
      Kind = CK_CPointerToObjCPointerCast;

      //  - conversions from 'void*'
      if (RHSType->isVoidPointerType()) {
        return Compatible;
      }

      //  - conversions to 'Class' from its redefinition type
      if (LHSType->isObjCClassType() &&
          Context.hasSameType(RHSType,
                              Context.getObjCClassRedefinitionType())) {
        return Compatible;
      }

      return IncompatiblePointer;
    }

    // Only under strict condition T^ is compatible with an Objective-C pointer.
    if (RHSType->isBlockPointerType() &&
        LHSType->isBlockCompatibleObjCPointerType(Context)) {
      if (ConvertRHS)
        maybeExtendBlockObject(RHS);
      Kind = CK_BlockPointerToObjCPointerCast;
      return Compatible;
    }

    return Incompatible;
  }

  // Conversion to nullptr_t (C23 only)
  if (getLangOpts().C23 && LHSType->isNullPtrType() &&
      RHS.get()->isNullPointerConstant(Context,
                                       Expr::NPC_ValueDependentIsNull)) {
    // null -> nullptr_t
    Kind = CK_NullToPointer;
    return Compatible;
  }

  // Conversions from pointers that are not covered by the above.
  if (isa<PointerType>(RHSType)) {
    // T* -> _Bool
    if (LHSType == Context.BoolTy) {
      Kind = CK_PointerToBoolean;
      return Compatible;
    }

    // T* -> int
    if (LHSType->isIntegerType()) {
      Kind = CK_PointerToIntegral;
      return PointerToInt;
    }

    return Incompatible;
  }

  // Conversions from Objective-C pointers that are not covered by the above.
  if (isa<ObjCObjectPointerType>(RHSType)) {
    // T* -> _Bool
    if (LHSType == Context.BoolTy) {
      Kind = CK_PointerToBoolean;
      return Compatible;
    }

    // T* -> int
    if (LHSType->isIntegerType()) {
      Kind = CK_PointerToIntegral;
      return PointerToInt;
    }

    return Incompatible;
  }

  // struct A -> struct B
  if (isa<TagType>(LHSType) && isa<TagType>(RHSType)) {
    if (Context.typesAreCompatible(LHSType, RHSType)) {
      Kind = CK_NoOp;
      return Compatible;
    }
  }

  if (LHSType->isSamplerT() && RHSType->isIntegerType()) {
    Kind = CK_IntToOCLSampler;
    return Compatible;
  }

  return Incompatible;
}

/// Constructs a transparent union from an expression that is
/// used to initialize the transparent union.
static void ConstructTransparentUnion(Sema &S, ASTContext &C,
                                      ExprResult &EResult, QualType UnionType,
                                      FieldDecl *Field) {
  // Build an initializer list that designates the appropriate member
  // of the transparent union.
  Expr *E = EResult.get();
  InitListExpr *Initializer = new (C) InitListExpr(C, SourceLocation(),
                                                   E, SourceLocation());
  Initializer->setType(UnionType);
  Initializer->setInitializedFieldInUnion(Field);

  // Build a compound literal constructing a value of the transparent
  // union type from this initializer list.
  TypeSourceInfo *unionTInfo = C.getTrivialTypeSourceInfo(UnionType);
  EResult = new (C) CompoundLiteralExpr(SourceLocation(), unionTInfo, UnionType,
                                        VK_PRValue, Initializer, false);
}

Sema::AssignConvertType
Sema::CheckTransparentUnionArgumentConstraints(QualType ArgType,
                                               ExprResult &RHS) {
  QualType RHSType = RHS.get()->getType();

  // If the ArgType is a Union type, we want to handle a potential
  // transparent_union GCC extension.
  const RecordType *UT = ArgType->getAsUnionType();
  if (!UT || !UT->getDecl()->hasAttr<TransparentUnionAttr>())
    return Incompatible;

  // The field to initialize within the transparent union.
  RecordDecl *UD = UT->getDecl();
  FieldDecl *InitField = nullptr;
  // It's compatible if the expression matches any of the fields.
  for (auto *it : UD->fields()) {
    if (it->getType()->isPointerType()) {
      // If the transparent union contains a pointer type, we allow:
      // 1) void pointer
      // 2) null pointer constant
      if (RHSType->isPointerType())
        if (RHSType->castAs<PointerType>()->getPointeeType()->isVoidType()) {
          RHS = ImpCastExprToType(RHS.get(), it->getType(), CK_BitCast);
          InitField = it;
          break;
        }

      if (RHS.get()->isNullPointerConstant(Context,
                                           Expr::NPC_ValueDependentIsNull)) {
        RHS = ImpCastExprToType(RHS.get(), it->getType(),
                                CK_NullToPointer);
        InitField = it;
        break;
      }
    }

    CastKind Kind;
    if (CheckAssignmentConstraints(it->getType(), RHS, Kind)
          == Compatible) {
      RHS = ImpCastExprToType(RHS.get(), it->getType(), Kind);
      InitField = it;
      break;
    }
  }

  if (!InitField)
    return Incompatible;

  ConstructTransparentUnion(*this, Context, RHS, ArgType, InitField);
  return Compatible;
}

Sema::AssignConvertType
Sema::CheckSingleAssignmentConstraints(QualType LHSType, ExprResult &CallerRHS,
                                       bool Diagnose,
                                       bool DiagnoseCFAudited,
                                       bool ConvertRHS) {
  // We need to be able to tell the caller whether we diagnosed a problem, if
  // they ask us to issue diagnostics.
  assert((ConvertRHS || !Diagnose) && "can't indicate whether we diagnosed");

  // If ConvertRHS is false, we want to leave the caller's RHS untouched. Sadly,
  // we can't avoid *all* modifications at the moment, so we need some somewhere
  // to put the updated value.
  ExprResult LocalRHS = CallerRHS;
  ExprResult &RHS = ConvertRHS ? CallerRHS : LocalRHS;

  if (const auto *LHSPtrType = LHSType->getAs<PointerType>()) {
    if (const auto *RHSPtrType = RHS.get()->getType()->getAs<PointerType>()) {
      if (RHSPtrType->getPointeeType()->hasAttr(attr::NoDeref) &&
          !LHSPtrType->getPointeeType()->hasAttr(attr::NoDeref)) {
        Diag(RHS.get()->getExprLoc(),
             diag::warn_noderef_to_dereferenceable_pointer)
            << RHS.get()->getSourceRange();
      }
    }
  }

  if (getLangOpts().CPlusPlus) {
    if (!LHSType->isRecordType() && !LHSType->isAtomicType()) {
      // C++ 5.17p3: If the left operand is not of class type, the
      // expression is implicitly converted (C++ 4) to the
      // cv-unqualified type of the left operand.
      QualType RHSType = RHS.get()->getType();
      if (Diagnose) {
        RHS = PerformImplicitConversion(RHS.get(), LHSType.getUnqualifiedType(),
                                        AA_Assigning);
      } else {
        ImplicitConversionSequence ICS =
            TryImplicitConversion(RHS.get(), LHSType.getUnqualifiedType(),
                                  /*SuppressUserConversions=*/false,
                                  AllowedExplicit::None,
                                  /*InOverloadResolution=*/false,
                                  /*CStyle=*/false,
                                  /*AllowObjCWritebackConversion=*/false);
        if (ICS.isFailure())
          return Incompatible;
        RHS = PerformImplicitConversion(RHS.get(), LHSType.getUnqualifiedType(),
                                        ICS, AA_Assigning);
      }
      if (RHS.isInvalid())
        return Incompatible;
      Sema::AssignConvertType result = Compatible;
      if (getLangOpts().allowsNonTrivialObjCLifetimeQualifiers() &&
          !ObjC().CheckObjCARCUnavailableWeakConversion(LHSType, RHSType))
        result = IncompatibleObjCWeakRef;
      return result;
    }

    // FIXME: Currently, we fall through and treat C++ classes like C
    // structures.
    // FIXME: We also fall through for atomics; not sure what should
    // happen there, though.
  } else if (RHS.get()->getType() == Context.OverloadTy) {
    // As a set of extensions to C, we support overloading on functions. These
    // functions need to be resolved here.
    DeclAccessPair DAP;
    if (FunctionDecl *FD = ResolveAddressOfOverloadedFunction(
            RHS.get(), LHSType, /*Complain=*/false, DAP))
      RHS = FixOverloadedFunctionReference(RHS.get(), DAP, FD);
    else
      return Incompatible;
  }

  // This check seems unnatural, however it is necessary to ensure the proper
  // conversion of functions/arrays. If the conversion were done for all
  // DeclExpr's (created by ActOnIdExpression), it would mess up the unary
  // expressions that suppress this implicit conversion (&, sizeof). This needs
  // to happen before we check for null pointer conversions because C does not
  // undergo the same implicit conversions as C++ does above (by the calls to
  // TryImplicitConversion() and PerformImplicitConversion()) which insert the
  // lvalue to rvalue cast before checking for null pointer constraints. This
  // addresses code like: nullptr_t val; int *ptr; ptr = val;
  //
  // Suppress this for references: C++ 8.5.3p5.
  if (!LHSType->isReferenceType()) {
    // FIXME: We potentially allocate here even if ConvertRHS is false.
    RHS = DefaultFunctionArrayLvalueConversion(RHS.get(), Diagnose);
    if (RHS.isInvalid())
      return Incompatible;
  }

  // The constraints are expressed in terms of the atomic, qualified, or
  // unqualified type of the LHS.
  QualType LHSTypeAfterConversion = LHSType.getAtomicUnqualifiedType();

  // C99 6.5.16.1p1: the left operand is a pointer and the right is
  // a null pointer constant <C23>or its type is nullptr_t;</C23>.
  if ((LHSTypeAfterConversion->isPointerType() ||
       LHSTypeAfterConversion->isObjCObjectPointerType() ||
       LHSTypeAfterConversion->isBlockPointerType()) &&
      ((getLangOpts().C23 && RHS.get()->getType()->isNullPtrType()) ||
       RHS.get()->isNullPointerConstant(Context,
                                        Expr::NPC_ValueDependentIsNull))) {
    if (Diagnose || ConvertRHS) {
      CastKind Kind;
      CXXCastPath Path;
      CheckPointerConversion(RHS.get(), LHSType, Kind, Path,
                             /*IgnoreBaseAccess=*/false, Diagnose);
      if (ConvertRHS)
        RHS = ImpCastExprToType(RHS.get(), LHSType, Kind, VK_PRValue, &Path);
    }
    return Compatible;
  }
  // C23 6.5.16.1p1: the left operand has type atomic, qualified, or
  // unqualified bool, and the right operand is a pointer or its type is
  // nullptr_t.
  if (getLangOpts().C23 && LHSType->isBooleanType() &&
      RHS.get()->getType()->isNullPtrType()) {
    // NB: T* -> _Bool is handled in CheckAssignmentConstraints, this only
    // only handles nullptr -> _Bool due to needing an extra conversion
    // step.
    // We model this by converting from nullptr -> void * and then let the
    // conversion from void * -> _Bool happen naturally.
    if (Diagnose || ConvertRHS) {
      CastKind Kind;
      CXXCastPath Path;
      CheckPointerConversion(RHS.get(), Context.VoidPtrTy, Kind, Path,
                             /*IgnoreBaseAccess=*/false, Diagnose);
      if (ConvertRHS)
        RHS = ImpCastExprToType(RHS.get(), Context.VoidPtrTy, Kind, VK_PRValue,
                                &Path);
    }
  }

  // OpenCL queue_t type assignment.
  if (LHSType->isQueueT() && RHS.get()->isNullPointerConstant(
                                 Context, Expr::NPC_ValueDependentIsNull)) {
    RHS = ImpCastExprToType(RHS.get(), LHSType, CK_NullToPointer);
    return Compatible;
  }

  CastKind Kind;
  Sema::AssignConvertType result =
    CheckAssignmentConstraints(LHSType, RHS, Kind, ConvertRHS);

  // C99 6.5.16.1p2: The value of the right operand is converted to the
  // type of the assignment expression.
  // CheckAssignmentConstraints allows the left-hand side to be a reference,
  // so that we can use references in built-in functions even in C.
  // The getNonReferenceType() call makes sure that the resulting expression
  // does not have reference type.
  if (result != Incompatible && RHS.get()->getType() != LHSType) {
    QualType Ty = LHSType.getNonLValueExprType(Context);
    Expr *E = RHS.get();

    // Check for various Objective-C errors. If we are not reporting
    // diagnostics and just checking for errors, e.g., during overload
    // resolution, return Incompatible to indicate the failure.
    if (getLangOpts().allowsNonTrivialObjCLifetimeQualifiers() &&
        ObjC().CheckObjCConversion(SourceRange(), Ty, E,
                                   CheckedConversionKind::Implicit, Diagnose,
                                   DiagnoseCFAudited) != SemaObjC::ACR_okay) {
      if (!Diagnose)
        return Incompatible;
    }
    if (getLangOpts().ObjC &&
        (ObjC().CheckObjCBridgeRelatedConversions(E->getBeginLoc(), LHSType,
                                                  E->getType(), E, Diagnose) ||
         ObjC().CheckConversionToObjCLiteral(LHSType, E, Diagnose))) {
      if (!Diagnose)
        return Incompatible;
      // Replace the expression with a corrected version and continue so we
      // can find further errors.
      RHS = E;
      return Compatible;
    }

    if (ConvertRHS)
      RHS = ImpCastExprToType(E, Ty, Kind);
  }

  return result;
}

namespace {
/// The original operand to an operator, prior to the application of the usual
/// arithmetic conversions and converting the arguments of a builtin operator
/// candidate.
struct OriginalOperand {
  explicit OriginalOperand(Expr *Op) : Orig(Op), Conversion(nullptr) {
    if (auto *MTE = dyn_cast<MaterializeTemporaryExpr>(Op))
      Op = MTE->getSubExpr();
    if (auto *BTE = dyn_cast<CXXBindTemporaryExpr>(Op))
      Op = BTE->getSubExpr();
    if (auto *ICE = dyn_cast<ImplicitCastExpr>(Op)) {
      Orig = ICE->getSubExprAsWritten();
      Conversion = ICE->getConversionFunction();
    }
  }

  QualType getType() const { return Orig->getType(); }

  Expr *Orig;
  NamedDecl *Conversion;
};
}

QualType Sema::InvalidOperands(SourceLocation Loc, ExprResult &LHS,
                               ExprResult &RHS) {
  OriginalOperand OrigLHS(LHS.get()), OrigRHS(RHS.get());

  Diag(Loc, diag::err_typecheck_invalid_operands)
    << OrigLHS.getType() << OrigRHS.getType()
    << LHS.get()->getSourceRange() << RHS.get()->getSourceRange();

  // If a user-defined conversion was applied to either of the operands prior
  // to applying the built-in operator rules, tell the user about it.
  if (OrigLHS.Conversion) {
    Diag(OrigLHS.Conversion->getLocation(),
         diag::note_typecheck_invalid_operands_converted)
      << 0 << LHS.get()->getType();
  }
  if (OrigRHS.Conversion) {
    Diag(OrigRHS.Conversion->getLocation(),
         diag::note_typecheck_invalid_operands_converted)
      << 1 << RHS.get()->getType();
  }

  return QualType();
}

QualType Sema::InvalidLogicalVectorOperands(SourceLocation Loc, ExprResult &LHS,
                                            ExprResult &RHS) {
  QualType LHSType = LHS.get()->IgnoreImpCasts()->getType();
  QualType RHSType = RHS.get()->IgnoreImpCasts()->getType();

  bool LHSNatVec = LHSType->isVectorType();
  bool RHSNatVec = RHSType->isVectorType();

  if (!(LHSNatVec && RHSNatVec)) {
    Expr *Vector = LHSNatVec ? LHS.get() : RHS.get();
    Expr *NonVector = !LHSNatVec ? LHS.get() : RHS.get();
    Diag(Loc, diag::err_typecheck_logical_vector_expr_gnu_cpp_restrict)
        << 0 << Vector->getType() << NonVector->IgnoreImpCasts()->getType()
        << Vector->getSourceRange();
    return QualType();
  }

  Diag(Loc, diag::err_typecheck_logical_vector_expr_gnu_cpp_restrict)
      << 1 << LHSType << RHSType << LHS.get()->getSourceRange()
      << RHS.get()->getSourceRange();

  return QualType();
}

/// Try to convert a value of non-vector type to a vector type by converting
/// the type to the element type of the vector and then performing a splat.
/// If the language is OpenCL, we only use conversions that promote scalar
/// rank; for C, Obj-C, and C++ we allow any real scalar conversion except
/// for float->int.
///
/// OpenCL V2.0 6.2.6.p2:
/// An error shall occur if any scalar operand type has greater rank
/// than the type of the vector element.
///
/// \param scalar - if non-null, actually perform the conversions
/// \return true if the operation fails (but without diagnosing the failure)
static bool tryVectorConvertAndSplat(Sema &S, ExprResult *scalar,
                                     QualType scalarTy,
                                     QualType vectorEltTy,
                                     QualType vectorTy,
                                     unsigned &DiagID) {
  // The conversion to apply to the scalar before splatting it,
  // if necessary.
  CastKind scalarCast = CK_NoOp;

  if (vectorEltTy->isIntegralType(S.Context)) {
    if (S.getLangOpts().OpenCL && (scalarTy->isRealFloatingType() ||
        (scalarTy->isIntegerType() &&
         S.Context.getIntegerTypeOrder(vectorEltTy, scalarTy) < 0))) {
      DiagID = diag::err_opencl_scalar_type_rank_greater_than_vector_type;
      return true;
    }
    if (!scalarTy->isIntegralType(S.Context))
      return true;
    scalarCast = CK_IntegralCast;
  } else if (vectorEltTy->isRealFloatingType()) {
    if (scalarTy->isRealFloatingType()) {
      if (S.getLangOpts().OpenCL &&
          S.Context.getFloatingTypeOrder(vectorEltTy, scalarTy) < 0) {
        DiagID = diag::err_opencl_scalar_type_rank_greater_than_vector_type;
        return true;
      }
      scalarCast = CK_FloatingCast;
    }
    else if (scalarTy->isIntegralType(S.Context))
      scalarCast = CK_IntegralToFloating;
    else
      return true;
  } else {
    return true;
  }

  // Adjust scalar if desired.
  if (scalar) {
    if (scalarCast != CK_NoOp)
      *scalar = S.ImpCastExprToType(scalar->get(), vectorEltTy, scalarCast);
    *scalar = S.ImpCastExprToType(scalar->get(), vectorTy, CK_VectorSplat);
  }
  return false;
}

/// Convert vector E to a vector with the same number of elements but different
/// element type.
static ExprResult convertVector(Expr *E, QualType ElementType, Sema &S) {
  const auto *VecTy = E->getType()->getAs<VectorType>();
  assert(VecTy && "Expression E must be a vector");
  QualType NewVecTy =
      VecTy->isExtVectorType()
          ? S.Context.getExtVectorType(ElementType, VecTy->getNumElements())
          : S.Context.getVectorType(ElementType, VecTy->getNumElements(),
                                    VecTy->getVectorKind());

  // Look through the implicit cast. Return the subexpression if its type is
  // NewVecTy.
  if (auto *ICE = dyn_cast<ImplicitCastExpr>(E))
    if (ICE->getSubExpr()->getType() == NewVecTy)
      return ICE->getSubExpr();

  auto Cast = ElementType->isIntegerType() ? CK_IntegralCast : CK_FloatingCast;
  return S.ImpCastExprToType(E, NewVecTy, Cast);
}

/// Test if a (constant) integer Int can be casted to another integer type
/// IntTy without losing precision.
static bool canConvertIntToOtherIntTy(Sema &S, ExprResult *Int,
                                      QualType OtherIntTy) {
  QualType IntTy = Int->get()->getType().getUnqualifiedType();

  // Reject cases where the value of the Int is unknown as that would
  // possibly cause truncation, but accept cases where the scalar can be
  // demoted without loss of precision.
  Expr::EvalResult EVResult;
  bool CstInt = Int->get()->EvaluateAsInt(EVResult, S.Context);
  int Order = S.Context.getIntegerTypeOrder(OtherIntTy, IntTy);
  bool IntSigned = IntTy->hasSignedIntegerRepresentation();
  bool OtherIntSigned = OtherIntTy->hasSignedIntegerRepresentation();

  if (CstInt) {
    // If the scalar is constant and is of a higher order and has more active
    // bits that the vector element type, reject it.
    llvm::APSInt Result = EVResult.Val.getInt();
    unsigned NumBits = IntSigned
                           ? (Result.isNegative() ? Result.getSignificantBits()
                                                  : Result.getActiveBits())
                           : Result.getActiveBits();
    if (Order < 0 && S.Context.getIntWidth(OtherIntTy) < NumBits)
      return true;

    // If the signedness of the scalar type and the vector element type
    // differs and the number of bits is greater than that of the vector
    // element reject it.
    return (IntSigned != OtherIntSigned &&
            NumBits > S.Context.getIntWidth(OtherIntTy));
  }

  // Reject cases where the value of the scalar is not constant and it's
  // order is greater than that of the vector element type.
  return (Order < 0);
}

/// Test if a (constant) integer Int can be casted to floating point type
/// FloatTy without losing precision.
static bool canConvertIntTyToFloatTy(Sema &S, ExprResult *Int,
                                     QualType FloatTy) {
  QualType IntTy = Int->get()->getType().getUnqualifiedType();

  // Determine if the integer constant can be expressed as a floating point
  // number of the appropriate type.
  Expr::EvalResult EVResult;
  bool CstInt = Int->get()->EvaluateAsInt(EVResult, S.Context);

  uint64_t Bits = 0;
  if (CstInt) {
    // Reject constants that would be truncated if they were converted to
    // the floating point type. Test by simple to/from conversion.
    // FIXME: Ideally the conversion to an APFloat and from an APFloat
    //        could be avoided if there was a convertFromAPInt method
    //        which could signal back if implicit truncation occurred.
    llvm::APSInt Result = EVResult.Val.getInt();
    llvm::APFloat Float(S.Context.getFloatTypeSemantics(FloatTy));
    Float.convertFromAPInt(Result, IntTy->hasSignedIntegerRepresentation(),
                           llvm::APFloat::rmTowardZero);
    llvm::APSInt ConvertBack(S.Context.getIntWidth(IntTy),
                             !IntTy->hasSignedIntegerRepresentation());
    bool Ignored = false;
    Float.convertToInteger(ConvertBack, llvm::APFloat::rmNearestTiesToEven,
                           &Ignored);
    if (Result != ConvertBack)
      return true;
  } else {
    // Reject types that cannot be fully encoded into the mantissa of
    // the float.
    Bits = S.Context.getTypeSize(IntTy);
    unsigned FloatPrec = llvm::APFloat::semanticsPrecision(
        S.Context.getFloatTypeSemantics(FloatTy));
    if (Bits > FloatPrec)
      return true;
  }

  return false;
}

/// Attempt to convert and splat Scalar into a vector whose types matches
/// Vector following GCC conversion rules. The rule is that implicit
/// conversion can occur when Scalar can be casted to match Vector's element
/// type without causing truncation of Scalar.
static bool tryGCCVectorConvertAndSplat(Sema &S, ExprResult *Scalar,
                                        ExprResult *Vector) {
  QualType ScalarTy = Scalar->get()->getType().getUnqualifiedType();
  QualType VectorTy = Vector->get()->getType().getUnqualifiedType();
  QualType VectorEltTy;

  if (const auto *VT = VectorTy->getAs<VectorType>()) {
    assert(!isa<ExtVectorType>(VT) &&
           "ExtVectorTypes should not be handled here!");
    VectorEltTy = VT->getElementType();
  } else if (VectorTy->isSveVLSBuiltinType()) {
    VectorEltTy =
        VectorTy->castAs<BuiltinType>()->getSveEltType(S.getASTContext());
  } else {
    llvm_unreachable("Only Fixed-Length and SVE Vector types are handled here");
  }

  // Reject cases where the vector element type or the scalar element type are
  // not integral or floating point types.
  if (!VectorEltTy->isArithmeticType() || !ScalarTy->isArithmeticType())
    return true;

  // The conversion to apply to the scalar before splatting it,
  // if necessary.
  CastKind ScalarCast = CK_NoOp;

  // Accept cases where the vector elements are integers and the scalar is
  // an integer.
  // FIXME: Notionally if the scalar was a floating point value with a precise
  //        integral representation, we could cast it to an appropriate integer
  //        type and then perform the rest of the checks here. GCC will perform
  //        this conversion in some cases as determined by the input language.
  //        We should accept it on a language independent basis.
  if (VectorEltTy->isIntegralType(S.Context) &&
      ScalarTy->isIntegralType(S.Context) &&
      S.Context.getIntegerTypeOrder(VectorEltTy, ScalarTy)) {

    if (canConvertIntToOtherIntTy(S, Scalar, VectorEltTy))
      return true;

    ScalarCast = CK_IntegralCast;
  } else if (VectorEltTy->isIntegralType(S.Context) &&
             ScalarTy->isRealFloatingType()) {
    if (S.Context.getTypeSize(VectorEltTy) == S.Context.getTypeSize(ScalarTy))
      ScalarCast = CK_FloatingToIntegral;
    else
      return true;
  } else if (VectorEltTy->isRealFloatingType()) {
    if (ScalarTy->isRealFloatingType()) {

      // Reject cases where the scalar type is not a constant and has a higher
      // Order than the vector element type.
      llvm::APFloat Result(0.0);

      // Determine whether this is a constant scalar. In the event that the
      // value is dependent (and thus cannot be evaluated by the constant
      // evaluator), skip the evaluation. This will then diagnose once the
      // expression is instantiated.
      bool CstScalar = Scalar->get()->isValueDependent() ||
                       Scalar->get()->EvaluateAsFloat(Result, S.Context);
      int Order = S.Context.getFloatingTypeOrder(VectorEltTy, ScalarTy);
      if (!CstScalar && Order < 0)
        return true;

      // If the scalar cannot be safely casted to the vector element type,
      // reject it.
      if (CstScalar) {
        bool Truncated = false;
        Result.convert(S.Context.getFloatTypeSemantics(VectorEltTy),
                       llvm::APFloat::rmNearestTiesToEven, &Truncated);
        if (Truncated)
          return true;
      }

      ScalarCast = CK_FloatingCast;
    } else if (ScalarTy->isIntegralType(S.Context)) {
      if (canConvertIntTyToFloatTy(S, Scalar, VectorEltTy))
        return true;

      ScalarCast = CK_IntegralToFloating;
    } else
      return true;
  } else if (ScalarTy->isEnumeralType())
    return true;

  // Adjust scalar if desired.
  if (ScalarCast != CK_NoOp)
    *Scalar = S.ImpCastExprToType(Scalar->get(), VectorEltTy, ScalarCast);
  *Scalar = S.ImpCastExprToType(Scalar->get(), VectorTy, CK_VectorSplat);
  return false;
}

QualType Sema::CheckVectorOperands(ExprResult &LHS, ExprResult &RHS,
                                   SourceLocation Loc, bool IsCompAssign,
                                   bool AllowBothBool,
                                   bool AllowBoolConversions,
                                   bool AllowBoolOperation,
                                   bool ReportInvalid) {
  if (!IsCompAssign) {
    LHS = DefaultFunctionArrayLvalueConversion(LHS.get());
    if (LHS.isInvalid())
      return QualType();
  }
  RHS = DefaultFunctionArrayLvalueConversion(RHS.get());
  if (RHS.isInvalid())
    return QualType();

  // For conversion purposes, we ignore any qualifiers.
  // For example, "const float" and "float" are equivalent.
  QualType LHSType = LHS.get()->getType().getUnqualifiedType();
  QualType RHSType = RHS.get()->getType().getUnqualifiedType();

  const VectorType *LHSVecType = LHSType->getAs<VectorType>();
  const VectorType *RHSVecType = RHSType->getAs<VectorType>();
  assert(LHSVecType || RHSVecType);

  // AltiVec-style "vector bool op vector bool" combinations are allowed
  // for some operators but not others.
  if (!AllowBothBool && LHSVecType &&
      LHSVecType->getVectorKind() == VectorKind::AltiVecBool && RHSVecType &&
      RHSVecType->getVectorKind() == VectorKind::AltiVecBool)
    return ReportInvalid ? InvalidOperands(Loc, LHS, RHS) : QualType();

  // This operation may not be performed on boolean vectors.
  if (!AllowBoolOperation &&
      (LHSType->isExtVectorBoolType() || RHSType->isExtVectorBoolType()))
    return ReportInvalid ? InvalidOperands(Loc, LHS, RHS) : QualType();

  // If the vector types are identical, return.
  if (Context.hasSameType(LHSType, RHSType))
    return Context.getCommonSugaredType(LHSType, RHSType);

  // If we have compatible AltiVec and GCC vector types, use the AltiVec type.
  if (LHSVecType && RHSVecType &&
      Context.areCompatibleVectorTypes(LHSType, RHSType)) {
    if (isa<ExtVectorType>(LHSVecType)) {
      RHS = ImpCastExprToType(RHS.get(), LHSType, CK_BitCast);
      return LHSType;
    }

    if (!IsCompAssign)
      LHS = ImpCastExprToType(LHS.get(), RHSType, CK_BitCast);
    return RHSType;
  }

  // AllowBoolConversions says that bool and non-bool AltiVec vectors
  // can be mixed, with the result being the non-bool type.  The non-bool
  // operand must have integer element type.
  if (AllowBoolConversions && LHSVecType && RHSVecType &&
      LHSVecType->getNumElements() == RHSVecType->getNumElements() &&
      (Context.getTypeSize(LHSVecType->getElementType()) ==
       Context.getTypeSize(RHSVecType->getElementType()))) {
    if (LHSVecType->getVectorKind() == VectorKind::AltiVecVector &&
        LHSVecType->getElementType()->isIntegerType() &&
        RHSVecType->getVectorKind() == VectorKind::AltiVecBool) {
      RHS = ImpCastExprToType(RHS.get(), LHSType, CK_BitCast);
      return LHSType;
    }
    if (!IsCompAssign &&
        LHSVecType->getVectorKind() == VectorKind::AltiVecBool &&
        RHSVecType->getVectorKind() == VectorKind::AltiVecVector &&
        RHSVecType->getElementType()->isIntegerType()) {
      LHS = ImpCastExprToType(LHS.get(), RHSType, CK_BitCast);
      return RHSType;
    }
  }

  // Expressions containing fixed-length and sizeless SVE/RVV vectors are
  // invalid since the ambiguity can affect the ABI.
  auto IsSveRVVConversion = [](QualType FirstType, QualType SecondType,
                               unsigned &SVEorRVV) {
    const VectorType *VecType = SecondType->getAs<VectorType>();
    SVEorRVV = 0;
    if (FirstType->isSizelessBuiltinType() && VecType) {
      if (VecType->getVectorKind() == VectorKind::SveFixedLengthData ||
          VecType->getVectorKind() == VectorKind::SveFixedLengthPredicate)
        return true;
      if (VecType->getVectorKind() == VectorKind::RVVFixedLengthData ||
          VecType->getVectorKind() == VectorKind::RVVFixedLengthMask) {
        SVEorRVV = 1;
        return true;
      }
    }

    return false;
  };

  unsigned SVEorRVV;
  if (IsSveRVVConversion(LHSType, RHSType, SVEorRVV) ||
      IsSveRVVConversion(RHSType, LHSType, SVEorRVV)) {
    Diag(Loc, diag::err_typecheck_sve_rvv_ambiguous)
        << SVEorRVV << LHSType << RHSType;
    return QualType();
  }

  // Expressions containing GNU and SVE or RVV (fixed or sizeless) vectors are
  // invalid since the ambiguity can affect the ABI.
  auto IsSveRVVGnuConversion = [](QualType FirstType, QualType SecondType,
                                  unsigned &SVEorRVV) {
    const VectorType *FirstVecType = FirstType->getAs<VectorType>();
    const VectorType *SecondVecType = SecondType->getAs<VectorType>();

    SVEorRVV = 0;
    if (FirstVecType && SecondVecType) {
      if (FirstVecType->getVectorKind() == VectorKind::Generic) {
        if (SecondVecType->getVectorKind() == VectorKind::SveFixedLengthData ||
            SecondVecType->getVectorKind() ==
                VectorKind::SveFixedLengthPredicate)
          return true;
        if (SecondVecType->getVectorKind() == VectorKind::RVVFixedLengthData ||
            SecondVecType->getVectorKind() == VectorKind::RVVFixedLengthMask) {
          SVEorRVV = 1;
          return true;
        }
      }
      return false;
    }

    if (SecondVecType &&
        SecondVecType->getVectorKind() == VectorKind::Generic) {
      if (FirstType->isSVESizelessBuiltinType())
        return true;
      if (FirstType->isRVVSizelessBuiltinType()) {
        SVEorRVV = 1;
        return true;
      }
    }

    return false;
  };

  if (IsSveRVVGnuConversion(LHSType, RHSType, SVEorRVV) ||
      IsSveRVVGnuConversion(RHSType, LHSType, SVEorRVV)) {
    Diag(Loc, diag::err_typecheck_sve_rvv_gnu_ambiguous)
        << SVEorRVV << LHSType << RHSType;
    return QualType();
  }

  // If there's a vector type and a scalar, try to convert the scalar to
  // the vector element type and splat.
  unsigned DiagID = diag::err_typecheck_vector_not_convertable;
  if (!RHSVecType) {
    if (isa<ExtVectorType>(LHSVecType)) {
      if (!tryVectorConvertAndSplat(*this, &RHS, RHSType,
                                    LHSVecType->getElementType(), LHSType,
                                    DiagID))
        return LHSType;
    } else {
      if (!tryGCCVectorConvertAndSplat(*this, &RHS, &LHS))
        return LHSType;
    }
  }
  if (!LHSVecType) {
    if (isa<ExtVectorType>(RHSVecType)) {
      if (!tryVectorConvertAndSplat(*this, (IsCompAssign ? nullptr : &LHS),
                                    LHSType, RHSVecType->getElementType(),
                                    RHSType, DiagID))
        return RHSType;
    } else {
      if (LHS.get()->isLValue() ||
          !tryGCCVectorConvertAndSplat(*this, &LHS, &RHS))
        return RHSType;
    }
  }

  // FIXME: The code below also handles conversion between vectors and
  // non-scalars, we should break this down into fine grained specific checks
  // and emit proper diagnostics.
  QualType VecType = LHSVecType ? LHSType : RHSType;
  const VectorType *VT = LHSVecType ? LHSVecType : RHSVecType;
  QualType OtherType = LHSVecType ? RHSType : LHSType;
  ExprResult *OtherExpr = LHSVecType ? &RHS : &LHS;
  if (isLaxVectorConversion(OtherType, VecType)) {
    if (Context.getTargetInfo().getTriple().isPPC() &&
        anyAltivecTypes(RHSType, LHSType) &&
        !Context.areCompatibleVectorTypes(RHSType, LHSType))
      Diag(Loc, diag::warn_deprecated_lax_vec_conv_all) << RHSType << LHSType;
    // If we're allowing lax vector conversions, only the total (data) size
    // needs to be the same. For non compound assignment, if one of the types is
    // scalar, the result is always the vector type.
    if (!IsCompAssign) {
      *OtherExpr = ImpCastExprToType(OtherExpr->get(), VecType, CK_BitCast);
      return VecType;
    // In a compound assignment, lhs += rhs, 'lhs' is a lvalue src, forbidding
    // any implicit cast. Here, the 'rhs' should be implicit casted to 'lhs'
    // type. Note that this is already done by non-compound assignments in
    // CheckAssignmentConstraints. If it's a scalar type, only bitcast for
    // <1 x T> -> T. The result is also a vector type.
    } else if (OtherType->isExtVectorType() || OtherType->isVectorType() ||
               (OtherType->isScalarType() && VT->getNumElements() == 1)) {
      ExprResult *RHSExpr = &RHS;
      *RHSExpr = ImpCastExprToType(RHSExpr->get(), LHSType, CK_BitCast);
      return VecType;
    }
  }

  // Okay, the expression is invalid.

  // If there's a non-vector, non-real operand, diagnose that.
  if ((!RHSVecType && !RHSType->isRealType()) ||
      (!LHSVecType && !LHSType->isRealType())) {
    Diag(Loc, diag::err_typecheck_vector_not_convertable_non_scalar)
      << LHSType << RHSType
      << LHS.get()->getSourceRange() << RHS.get()->getSourceRange();
    return QualType();
  }

  // OpenCL V1.1 6.2.6.p1:
  // If the operands are of more than one vector type, then an error shall
  // occur. Implicit conversions between vector types are not permitted, per
  // section 6.2.1.
  if (getLangOpts().OpenCL &&
      RHSVecType && isa<ExtVectorType>(RHSVecType) &&
      LHSVecType && isa<ExtVectorType>(LHSVecType)) {
    Diag(Loc, diag::err_opencl_implicit_vector_conversion) << LHSType
                                                           << RHSType;
    return QualType();
  }


  // If there is a vector type that is not a ExtVector and a scalar, we reach
  // this point if scalar could not be converted to the vector's element type
  // without truncation.
  if ((RHSVecType && !isa<ExtVectorType>(RHSVecType)) ||
      (LHSVecType && !isa<ExtVectorType>(LHSVecType))) {
    QualType Scalar = LHSVecType ? RHSType : LHSType;
    QualType Vector = LHSVecType ? LHSType : RHSType;
    unsigned ScalarOrVector = LHSVecType && RHSVecType ? 1 : 0;
    Diag(Loc,
         diag::err_typecheck_vector_not_convertable_implict_truncation)
        << ScalarOrVector << Scalar << Vector;

    return QualType();
  }

  // Otherwise, use the generic diagnostic.
  Diag(Loc, DiagID)
    << LHSType << RHSType
    << LHS.get()->getSourceRange() << RHS.get()->getSourceRange();
  return QualType();
}

QualType Sema::CheckSizelessVectorOperands(ExprResult &LHS, ExprResult &RHS,
                                           SourceLocation Loc,
                                           bool IsCompAssign,
                                           ArithConvKind OperationKind) {
  if (!IsCompAssign) {
    LHS = DefaultFunctionArrayLvalueConversion(LHS.get());
    if (LHS.isInvalid())
      return QualType();
  }
  RHS = DefaultFunctionArrayLvalueConversion(RHS.get());
  if (RHS.isInvalid())
    return QualType();

  QualType LHSType = LHS.get()->getType().getUnqualifiedType();
  QualType RHSType = RHS.get()->getType().getUnqualifiedType();

  const BuiltinType *LHSBuiltinTy = LHSType->getAs<BuiltinType>();
  const BuiltinType *RHSBuiltinTy = RHSType->getAs<BuiltinType>();

  unsigned DiagID = diag::err_typecheck_invalid_operands;
  if ((OperationKind == ACK_Arithmetic) &&
      ((LHSBuiltinTy && LHSBuiltinTy->isSVEBool()) ||
       (RHSBuiltinTy && RHSBuiltinTy->isSVEBool()))) {
    Diag(Loc, DiagID) << LHSType << RHSType << LHS.get()->getSourceRange()
                      << RHS.get()->getSourceRange();
    return QualType();
  }

  if (Context.hasSameType(LHSType, RHSType))
    return LHSType;

  if (LHSType->isSveVLSBuiltinType() && !RHSType->isSveVLSBuiltinType()) {
    if (!tryGCCVectorConvertAndSplat(*this, &RHS, &LHS))
      return LHSType;
  }
  if (RHSType->isSveVLSBuiltinType() && !LHSType->isSveVLSBuiltinType()) {
    if (LHS.get()->isLValue() ||
        !tryGCCVectorConvertAndSplat(*this, &LHS, &RHS))
      return RHSType;
  }

  if ((!LHSType->isSveVLSBuiltinType() && !LHSType->isRealType()) ||
      (!RHSType->isSveVLSBuiltinType() && !RHSType->isRealType())) {
    Diag(Loc, diag::err_typecheck_vector_not_convertable_non_scalar)
        << LHSType << RHSType << LHS.get()->getSourceRange()
        << RHS.get()->getSourceRange();
    return QualType();
  }

  if (LHSType->isSveVLSBuiltinType() && RHSType->isSveVLSBuiltinType() &&
      Context.getBuiltinVectorTypeInfo(LHSBuiltinTy).EC !=
          Context.getBuiltinVectorTypeInfo(RHSBuiltinTy).EC) {
    Diag(Loc, diag::err_typecheck_vector_lengths_not_equal)
        << LHSType << RHSType << LHS.get()->getSourceRange()
        << RHS.get()->getSourceRange();
    return QualType();
  }

  if (LHSType->isSveVLSBuiltinType() || RHSType->isSveVLSBuiltinType()) {
    QualType Scalar = LHSType->isSveVLSBuiltinType() ? RHSType : LHSType;
    QualType Vector = LHSType->isSveVLSBuiltinType() ? LHSType : RHSType;
    bool ScalarOrVector =
        LHSType->isSveVLSBuiltinType() && RHSType->isSveVLSBuiltinType();

    Diag(Loc, diag::err_typecheck_vector_not_convertable_implict_truncation)
        << ScalarOrVector << Scalar << Vector;

    return QualType();
  }

  Diag(Loc, DiagID) << LHSType << RHSType << LHS.get()->getSourceRange()
                    << RHS.get()->getSourceRange();
  return QualType();
}

// checkArithmeticNull - Detect when a NULL constant is used improperly in an
// expression.  These are mainly cases where the null pointer is used as an
// integer instead of a pointer.
static void checkArithmeticNull(Sema &S, ExprResult &LHS, ExprResult &RHS,
                                SourceLocation Loc, bool IsCompare) {
  // The canonical way to check for a GNU null is with isNullPointerConstant,
  // but we use a bit of a hack here for speed; this is a relatively
  // hot path, and isNullPointerConstant is slow.
  bool LHSNull = isa<GNUNullExpr>(LHS.get()->IgnoreParenImpCasts());
  bool RHSNull = isa<GNUNullExpr>(RHS.get()->IgnoreParenImpCasts());

  QualType NonNullType = LHSNull ? RHS.get()->getType() : LHS.get()->getType();

  // Avoid analyzing cases where the result will either be invalid (and
  // diagnosed as such) or entirely valid and not something to warn about.
  if ((!LHSNull && !RHSNull) || NonNullType->isBlockPointerType() ||
      NonNullType->isMemberPointerType() || NonNullType->isFunctionType())
    return;

  // Comparison operations would not make sense with a null pointer no matter
  // what the other expression is.
  if (!IsCompare) {
    S.Diag(Loc, diag::warn_null_in_arithmetic_operation)
        << (LHSNull ? LHS.get()->getSourceRange() : SourceRange())
        << (RHSNull ? RHS.get()->getSourceRange() : SourceRange());
    return;
  }

  // The rest of the operations only make sense with a null pointer
  // if the other expression is a pointer.
  if (LHSNull == RHSNull || NonNullType->isAnyPointerType() ||
      NonNullType->canDecayToPointerType())
    return;

  S.Diag(Loc, diag::warn_null_in_comparison_operation)
      << LHSNull /* LHS is NULL */ << NonNullType
      << LHS.get()->getSourceRange() << RHS.get()->getSourceRange();
}

static void DiagnoseDivisionSizeofPointerOrArray(Sema &S, Expr *LHS, Expr *RHS,
                                          SourceLocation Loc) {
  const auto *LUE = dyn_cast<UnaryExprOrTypeTraitExpr>(LHS);
  const auto *RUE = dyn_cast<UnaryExprOrTypeTraitExpr>(RHS);
  if (!LUE || !RUE)
    return;
  if (LUE->getKind() != UETT_SizeOf || LUE->isArgumentType() ||
      RUE->getKind() != UETT_SizeOf)
    return;

  const Expr *LHSArg = LUE->getArgumentExpr()->IgnoreParens();
  QualType LHSTy = LHSArg->getType();
  QualType RHSTy;

  if (RUE->isArgumentType())
    RHSTy = RUE->getArgumentType().getNonReferenceType();
  else
    RHSTy = RUE->getArgumentExpr()->IgnoreParens()->getType();

  if (LHSTy->isPointerType() && !RHSTy->isPointerType()) {
    if (!S.Context.hasSameUnqualifiedType(LHSTy->getPointeeType(), RHSTy))
      return;

    S.Diag(Loc, diag::warn_division_sizeof_ptr) << LHS << LHS->getSourceRange();
    if (const auto *DRE = dyn_cast<DeclRefExpr>(LHSArg)) {
      if (const ValueDecl *LHSArgDecl = DRE->getDecl())
        S.Diag(LHSArgDecl->getLocation(), diag::note_pointer_declared_here)
            << LHSArgDecl;
    }
  } else if (const auto *ArrayTy = S.Context.getAsArrayType(LHSTy)) {
    QualType ArrayElemTy = ArrayTy->getElementType();
    if (ArrayElemTy != S.Context.getBaseElementType(ArrayTy) ||
        ArrayElemTy->isDependentType() || RHSTy->isDependentType() ||
        RHSTy->isReferenceType() || ArrayElemTy->isCharType() ||
        S.Context.getTypeSize(ArrayElemTy) == S.Context.getTypeSize(RHSTy))
      return;
    S.Diag(Loc, diag::warn_division_sizeof_array)
        << LHSArg->getSourceRange() << ArrayElemTy << RHSTy;
    if (const auto *DRE = dyn_cast<DeclRefExpr>(LHSArg)) {
      if (const ValueDecl *LHSArgDecl = DRE->getDecl())
        S.Diag(LHSArgDecl->getLocation(), diag::note_array_declared_here)
            << LHSArgDecl;
    }

    S.Diag(Loc, diag::note_precedence_silence) << RHS;
  }
}

static void DiagnoseBadDivideOrRemainderValues(Sema& S, ExprResult &LHS,
                                               ExprResult &RHS,
                                               SourceLocation Loc, bool IsDiv) {
  // Check for division/remainder by zero.
  Expr::EvalResult RHSValue;
  if (!RHS.get()->isValueDependent() &&
      RHS.get()->EvaluateAsInt(RHSValue, S.Context) &&
      RHSValue.Val.getInt() == 0)
    S.DiagRuntimeBehavior(Loc, RHS.get(),
                          S.PDiag(diag::warn_remainder_division_by_zero)
                            << IsDiv << RHS.get()->getSourceRange());
}

QualType Sema::CheckMultiplyDivideOperands(ExprResult &LHS, ExprResult &RHS,
                                           SourceLocation Loc,
                                           bool IsCompAssign, bool IsDiv) {
  checkArithmeticNull(*this, LHS, RHS, Loc, /*IsCompare=*/false);

  QualType LHSTy = LHS.get()->getType();
  QualType RHSTy = RHS.get()->getType();
  if (LHSTy->isVectorType() || RHSTy->isVectorType())
    return CheckVectorOperands(LHS, RHS, Loc, IsCompAssign,
                               /*AllowBothBool*/ getLangOpts().AltiVec,
                               /*AllowBoolConversions*/ false,
                               /*AllowBooleanOperation*/ false,
                               /*ReportInvalid*/ true);
  if (LHSTy->isSveVLSBuiltinType() || RHSTy->isSveVLSBuiltinType())
    return CheckSizelessVectorOperands(LHS, RHS, Loc, IsCompAssign,
                                       ACK_Arithmetic);
  if (!IsDiv &&
      (LHSTy->isConstantMatrixType() || RHSTy->isConstantMatrixType()))
    return CheckMatrixMultiplyOperands(LHS, RHS, Loc, IsCompAssign);
  // For division, only matrix-by-scalar is supported. Other combinations with
  // matrix types are invalid.
  if (IsDiv && LHSTy->isConstantMatrixType() && RHSTy->isArithmeticType())
    return CheckMatrixElementwiseOperands(LHS, RHS, Loc, IsCompAssign);

  QualType compType = UsualArithmeticConversions(
      LHS, RHS, Loc, IsCompAssign ? ACK_CompAssign : ACK_Arithmetic);
  if (LHS.isInvalid() || RHS.isInvalid())
    return QualType();


  if (compType.isNull() || !compType->isArithmeticType())
    return InvalidOperands(Loc, LHS, RHS);
  if (IsDiv) {
    DiagnoseBadDivideOrRemainderValues(*this, LHS, RHS, Loc, IsDiv);
    DiagnoseDivisionSizeofPointerOrArray(*this, LHS.get(), RHS.get(), Loc);
  }
  return compType;
}

QualType Sema::CheckRemainderOperands(
  ExprResult &LHS, ExprResult &RHS, SourceLocation Loc, bool IsCompAssign) {
  checkArithmeticNull(*this, LHS, RHS, Loc, /*IsCompare=*/false);

  if (LHS.get()->getType()->isVectorType() ||
      RHS.get()->getType()->isVectorType()) {
    if (LHS.get()->getType()->hasIntegerRepresentation() &&
        RHS.get()->getType()->hasIntegerRepresentation())
      return CheckVectorOperands(LHS, RHS, Loc, IsCompAssign,
                                 /*AllowBothBool*/ getLangOpts().AltiVec,
                                 /*AllowBoolConversions*/ false,
                                 /*AllowBooleanOperation*/ false,
                                 /*ReportInvalid*/ true);
    return InvalidOperands(Loc, LHS, RHS);
  }

  if (LHS.get()->getType()->isSveVLSBuiltinType() ||
      RHS.get()->getType()->isSveVLSBuiltinType()) {
    if (LHS.get()->getType()->hasIntegerRepresentation() &&
        RHS.get()->getType()->hasIntegerRepresentation())
      return CheckSizelessVectorOperands(LHS, RHS, Loc, IsCompAssign,
                                         ACK_Arithmetic);

    return InvalidOperands(Loc, LHS, RHS);
  }

  QualType compType = UsualArithmeticConversions(
      LHS, RHS, Loc, IsCompAssign ? ACK_CompAssign : ACK_Arithmetic);
  if (LHS.isInvalid() || RHS.isInvalid())
    return QualType();

  if (compType.isNull() || !compType->isIntegerType())
    return InvalidOperands(Loc, LHS, RHS);
  DiagnoseBadDivideOrRemainderValues(*this, LHS, RHS, Loc, false /* IsDiv */);
  return compType;
}

/// Diagnose invalid arithmetic on two void pointers.
static void diagnoseArithmeticOnTwoVoidPointers(Sema &S, SourceLocation Loc,
                                                Expr *LHSExpr, Expr *RHSExpr) {
  S.Diag(Loc, S.getLangOpts().CPlusPlus
                ? diag::err_typecheck_pointer_arith_void_type
                : diag::ext_gnu_void_ptr)
    << 1 /* two pointers */ << LHSExpr->getSourceRange()
                            << RHSExpr->getSourceRange();
}

/// Diagnose invalid arithmetic on a void pointer.
static void diagnoseArithmeticOnVoidPointer(Sema &S, SourceLocation Loc,
                                            Expr *Pointer) {
  S.Diag(Loc, S.getLangOpts().CPlusPlus
                ? diag::err_typecheck_pointer_arith_void_type
                : diag::ext_gnu_void_ptr)
    << 0 /* one pointer */ << Pointer->getSourceRange();
}

/// Diagnose invalid arithmetic on a null pointer.
///
/// If \p IsGNUIdiom is true, the operation is using the 'p = (i8*)nullptr + n'
/// idiom, which we recognize as a GNU extension.
///
static void diagnoseArithmeticOnNullPointer(Sema &S, SourceLocation Loc,
                                            Expr *Pointer, bool IsGNUIdiom) {
  if (IsGNUIdiom)
    S.Diag(Loc, diag::warn_gnu_null_ptr_arith)
      << Pointer->getSourceRange();
  else
    S.Diag(Loc, diag::warn_pointer_arith_null_ptr)
      << S.getLangOpts().CPlusPlus << Pointer->getSourceRange();
}

/// Diagnose invalid subraction on a null pointer.
///
static void diagnoseSubtractionOnNullPointer(Sema &S, SourceLocation Loc,
                                             Expr *Pointer, bool BothNull) {
  // Null - null is valid in C++ [expr.add]p7
  if (BothNull && S.getLangOpts().CPlusPlus)
    return;

  // Is this s a macro from a system header?
  if (S.Diags.getSuppressSystemWarnings() && S.SourceMgr.isInSystemMacro(Loc))
    return;

  S.DiagRuntimeBehavior(Loc, Pointer,
                        S.PDiag(diag::warn_pointer_sub_null_ptr)
                            << S.getLangOpts().CPlusPlus
                            << Pointer->getSourceRange());
}

/// Diagnose invalid arithmetic on two function pointers.
static void diagnoseArithmeticOnTwoFunctionPointers(Sema &S, SourceLocation Loc,
                                                    Expr *LHS, Expr *RHS) {
  assert(LHS->getType()->isAnyPointerType());
  assert(RHS->getType()->isAnyPointerType());
  S.Diag(Loc, S.getLangOpts().CPlusPlus
                ? diag::err_typecheck_pointer_arith_function_type
                : diag::ext_gnu_ptr_func_arith)
    << 1 /* two pointers */ << LHS->getType()->getPointeeType()
    // We only show the second type if it differs from the first.
    << (unsigned)!S.Context.hasSameUnqualifiedType(LHS->getType(),
                                                   RHS->getType())
    << RHS->getType()->getPointeeType()
    << LHS->getSourceRange() << RHS->getSourceRange();
}

/// Diagnose invalid arithmetic on a function pointer.
static void diagnoseArithmeticOnFunctionPointer(Sema &S, SourceLocation Loc,
                                                Expr *Pointer) {
  assert(Pointer->getType()->isAnyPointerType());
  S.Diag(Loc, S.getLangOpts().CPlusPlus
                ? diag::err_typecheck_pointer_arith_function_type
                : diag::ext_gnu_ptr_func_arith)
    << 0 /* one pointer */ << Pointer->getType()->getPointeeType()
    << 0 /* one pointer, so only one type */
    << Pointer->getSourceRange();
}

/// Emit error if Operand is incomplete pointer type
///
/// \returns True if pointer has incomplete type
static bool checkArithmeticIncompletePointerType(Sema &S, SourceLocation Loc,
                                                 Expr *Operand) {
  QualType ResType = Operand->getType();
  if (const AtomicType *ResAtomicType = ResType->getAs<AtomicType>())
    ResType = ResAtomicType->getValueType();

  assert(ResType->isAnyPointerType());
  QualType PointeeTy = ResType->getPointeeType();
  return S.RequireCompleteSizedType(
      Loc, PointeeTy,
      diag::err_typecheck_arithmetic_incomplete_or_sizeless_type,
      Operand->getSourceRange());
}

/// Check the validity of an arithmetic pointer operand.
///
/// If the operand has pointer type, this code will check for pointer types
/// which are invalid in arithmetic operations. These will be diagnosed
/// appropriately, including whether or not the use is supported as an
/// extension.
///
/// \returns True when the operand is valid to use (even if as an extension).
static bool checkArithmeticOpPointerOperand(Sema &S, SourceLocation Loc,
                                            Expr *Operand) {
  QualType ResType = Operand->getType();
  if (const AtomicType *ResAtomicType = ResType->getAs<AtomicType>())
    ResType = ResAtomicType->getValueType();

  if (!ResType->isAnyPointerType()) return true;

  QualType PointeeTy = ResType->getPointeeType();
  if (PointeeTy->isVoidType()) {
    diagnoseArithmeticOnVoidPointer(S, Loc, Operand);
    return !S.getLangOpts().CPlusPlus;
  }
  if (PointeeTy->isFunctionType()) {
    diagnoseArithmeticOnFunctionPointer(S, Loc, Operand);
    return !S.getLangOpts().CPlusPlus;
  }

  if (checkArithmeticIncompletePointerType(S, Loc, Operand)) return false;

  return true;
}

/// Check the validity of a binary arithmetic operation w.r.t. pointer
/// operands.
///
/// This routine will diagnose any invalid arithmetic on pointer operands much
/// like \see checkArithmeticOpPointerOperand. However, it has special logic
/// for emitting a single diagnostic even for operations where both LHS and RHS
/// are (potentially problematic) pointers.
///
/// \returns True when the operand is valid to use (even if as an extension).
static bool checkArithmeticBinOpPointerOperands(Sema &S, SourceLocation Loc,
                                                Expr *LHSExpr, Expr *RHSExpr) {
  bool isLHSPointer = LHSExpr->getType()->isAnyPointerType();
  bool isRHSPointer = RHSExpr->getType()->isAnyPointerType();
  if (!isLHSPointer && !isRHSPointer) return true;

  QualType LHSPointeeTy, RHSPointeeTy;
  if (isLHSPointer) LHSPointeeTy = LHSExpr->getType()->getPointeeType();
  if (isRHSPointer) RHSPointeeTy = RHSExpr->getType()->getPointeeType();

  // if both are pointers check if operation is valid wrt address spaces
  if (isLHSPointer && isRHSPointer) {
    if (!LHSPointeeTy.isAddressSpaceOverlapping(RHSPointeeTy)) {
      S.Diag(Loc,
             diag::err_typecheck_op_on_nonoverlapping_address_space_pointers)
          << LHSExpr->getType() << RHSExpr->getType() << 1 /*arithmetic op*/
          << LHSExpr->getSourceRange() << RHSExpr->getSourceRange();
      return false;
    }
  }

  // Check for arithmetic on pointers to incomplete types.
  bool isLHSVoidPtr = isLHSPointer && LHSPointeeTy->isVoidType();
  bool isRHSVoidPtr = isRHSPointer && RHSPointeeTy->isVoidType();
  if (isLHSVoidPtr || isRHSVoidPtr) {
    if (!isRHSVoidPtr) diagnoseArithmeticOnVoidPointer(S, Loc, LHSExpr);
    else if (!isLHSVoidPtr) diagnoseArithmeticOnVoidPointer(S, Loc, RHSExpr);
    else diagnoseArithmeticOnTwoVoidPointers(S, Loc, LHSExpr, RHSExpr);

    return !S.getLangOpts().CPlusPlus;
  }

  bool isLHSFuncPtr = isLHSPointer && LHSPointeeTy->isFunctionType();
  bool isRHSFuncPtr = isRHSPointer && RHSPointeeTy->isFunctionType();
  if (isLHSFuncPtr || isRHSFuncPtr) {
    if (!isRHSFuncPtr) diagnoseArithmeticOnFunctionPointer(S, Loc, LHSExpr);
    else if (!isLHSFuncPtr) diagnoseArithmeticOnFunctionPointer(S, Loc,
                                                                RHSExpr);
    else diagnoseArithmeticOnTwoFunctionPointers(S, Loc, LHSExpr, RHSExpr);

    return !S.getLangOpts().CPlusPlus;
  }

  if (isLHSPointer && checkArithmeticIncompletePointerType(S, Loc, LHSExpr))
    return false;
  if (isRHSPointer && checkArithmeticIncompletePointerType(S, Loc, RHSExpr))
    return false;

  return true;
}

/// diagnoseStringPlusInt - Emit a warning when adding an integer to a string
/// literal.
static void diagnoseStringPlusInt(Sema &Self, SourceLocation OpLoc,
                                  Expr *LHSExpr, Expr *RHSExpr) {
  StringLiteral* StrExpr = dyn_cast<StringLiteral>(LHSExpr->IgnoreImpCasts());
  Expr* IndexExpr = RHSExpr;
  if (!StrExpr) {
    StrExpr = dyn_cast<StringLiteral>(RHSExpr->IgnoreImpCasts());
    IndexExpr = LHSExpr;
  }

  bool IsStringPlusInt = StrExpr &&
      IndexExpr->getType()->isIntegralOrUnscopedEnumerationType();
  if (!IsStringPlusInt || IndexExpr->isValueDependent())
    return;

  SourceRange DiagRange(LHSExpr->getBeginLoc(), RHSExpr->getEndLoc());
  Self.Diag(OpLoc, diag::warn_string_plus_int)
      << DiagRange << IndexExpr->IgnoreImpCasts()->getType();

  // Only print a fixit for "str" + int, not for int + "str".
  if (IndexExpr == RHSExpr) {
    SourceLocation EndLoc = Self.getLocForEndOfToken(RHSExpr->getEndLoc());
    Self.Diag(OpLoc, diag::note_string_plus_scalar_silence)
        << FixItHint::CreateInsertion(LHSExpr->getBeginLoc(), "&")
        << FixItHint::CreateReplacement(SourceRange(OpLoc), "[")
        << FixItHint::CreateInsertion(EndLoc, "]");
  } else
    Self.Diag(OpLoc, diag::note_string_plus_scalar_silence);
}

/// Emit a warning when adding a char literal to a string.
static void diagnoseStringPlusChar(Sema &Self, SourceLocation OpLoc,
                                   Expr *LHSExpr, Expr *RHSExpr) {
  const Expr *StringRefExpr = LHSExpr;
  const CharacterLiteral *CharExpr =
      dyn_cast<CharacterLiteral>(RHSExpr->IgnoreImpCasts());

  if (!CharExpr) {
    CharExpr = dyn_cast<CharacterLiteral>(LHSExpr->IgnoreImpCasts());
    StringRefExpr = RHSExpr;
  }

  if (!CharExpr || !StringRefExpr)
    return;

  const QualType StringType = StringRefExpr->getType();

  // Return if not a PointerType.
  if (!StringType->isAnyPointerType())
    return;

  // Return if not a CharacterType.
  if (!StringType->getPointeeType()->isAnyCharacterType())
    return;

  ASTContext &Ctx = Self.getASTContext();
  SourceRange DiagRange(LHSExpr->getBeginLoc(), RHSExpr->getEndLoc());

  const QualType CharType = CharExpr->getType();
  if (!CharType->isAnyCharacterType() &&
      CharType->isIntegerType() &&
      llvm::isUIntN(Ctx.getCharWidth(), CharExpr->getValue())) {
    Self.Diag(OpLoc, diag::warn_string_plus_char)
        << DiagRange << Ctx.CharTy;
  } else {
    Self.Diag(OpLoc, diag::warn_string_plus_char)
        << DiagRange << CharExpr->getType();
  }

  // Only print a fixit for str + char, not for char + str.
  if (isa<CharacterLiteral>(RHSExpr->IgnoreImpCasts())) {
    SourceLocation EndLoc = Self.getLocForEndOfToken(RHSExpr->getEndLoc());
    Self.Diag(OpLoc, diag::note_string_plus_scalar_silence)
        << FixItHint::CreateInsertion(LHSExpr->getBeginLoc(), "&")
        << FixItHint::CreateReplacement(SourceRange(OpLoc), "[")
        << FixItHint::CreateInsertion(EndLoc, "]");
  } else {
    Self.Diag(OpLoc, diag::note_string_plus_scalar_silence);
  }
}

/// Emit error when two pointers are incompatible.
static void diagnosePointerIncompatibility(Sema &S, SourceLocation Loc,
                                           Expr *LHSExpr, Expr *RHSExpr) {
  assert(LHSExpr->getType()->isAnyPointerType());
  assert(RHSExpr->getType()->isAnyPointerType());
  S.Diag(Loc, diag::err_typecheck_sub_ptr_compatible)
    << LHSExpr->getType() << RHSExpr->getType() << LHSExpr->getSourceRange()
    << RHSExpr->getSourceRange();
}

// C99 6.5.6
QualType Sema::CheckAdditionOperands(ExprResult &LHS, ExprResult &RHS,
                                     SourceLocation Loc, BinaryOperatorKind Opc,
                                     QualType* CompLHSTy) {
  checkArithmeticNull(*this, LHS, RHS, Loc, /*IsCompare=*/false);

  if (LHS.get()->getType()->isVectorType() ||
      RHS.get()->getType()->isVectorType()) {
    QualType compType =
        CheckVectorOperands(LHS, RHS, Loc, CompLHSTy,
                            /*AllowBothBool*/ getLangOpts().AltiVec,
                            /*AllowBoolConversions*/ getLangOpts().ZVector,
                            /*AllowBooleanOperation*/ false,
                            /*ReportInvalid*/ true);
    if (CompLHSTy) *CompLHSTy = compType;
    return compType;
  }

  if (LHS.get()->getType()->isSveVLSBuiltinType() ||
      RHS.get()->getType()->isSveVLSBuiltinType()) {
    QualType compType =
        CheckSizelessVectorOperands(LHS, RHS, Loc, CompLHSTy, ACK_Arithmetic);
    if (CompLHSTy)
      *CompLHSTy = compType;
    return compType;
  }

  if (LHS.get()->getType()->isConstantMatrixType() ||
      RHS.get()->getType()->isConstantMatrixType()) {
    QualType compType =
        CheckMatrixElementwiseOperands(LHS, RHS, Loc, CompLHSTy);
    if (CompLHSTy)
      *CompLHSTy = compType;
    return compType;
  }

  QualType compType = UsualArithmeticConversions(
      LHS, RHS, Loc, CompLHSTy ? ACK_CompAssign : ACK_Arithmetic);
  if (LHS.isInvalid() || RHS.isInvalid())
    return QualType();

  // Diagnose "string literal" '+' int and string '+' "char literal".
  if (Opc == BO_Add) {
    diagnoseStringPlusInt(*this, Loc, LHS.get(), RHS.get());
    diagnoseStringPlusChar(*this, Loc, LHS.get(), RHS.get());
  }

  // handle the common case first (both operands are arithmetic).
  if (!compType.isNull() && compType->isArithmeticType()) {
    if (CompLHSTy) *CompLHSTy = compType;
    return compType;
  }

  // Type-checking.  Ultimately the pointer's going to be in PExp;
  // note that we bias towards the LHS being the pointer.
  Expr *PExp = LHS.get(), *IExp = RHS.get();

  bool isObjCPointer;
  if (PExp->getType()->isPointerType()) {
    isObjCPointer = false;
  } else if (PExp->getType()->isObjCObjectPointerType()) {
    isObjCPointer = true;
  } else {
    std::swap(PExp, IExp);
    if (PExp->getType()->isPointerType()) {
      isObjCPointer = false;
    } else if (PExp->getType()->isObjCObjectPointerType()) {
      isObjCPointer = true;
    } else {
      return InvalidOperands(Loc, LHS, RHS);
    }
  }
  assert(PExp->getType()->isAnyPointerType());

  if (!IExp->getType()->isIntegerType())
    return InvalidOperands(Loc, LHS, RHS);

  // Adding to a null pointer results in undefined behavior.
  if (PExp->IgnoreParenCasts()->isNullPointerConstant(
          Context, Expr::NPC_ValueDependentIsNotNull)) {
    // In C++ adding zero to a null pointer is defined.
    Expr::EvalResult KnownVal;
    if (!getLangOpts().CPlusPlus ||
        (!IExp->isValueDependent() &&
         (!IExp->EvaluateAsInt(KnownVal, Context) ||
          KnownVal.Val.getInt() != 0))) {
      // Check the conditions to see if this is the 'p = nullptr + n' idiom.
      bool IsGNUIdiom = BinaryOperator::isNullPointerArithmeticExtension(
          Context, BO_Add, PExp, IExp);
      diagnoseArithmeticOnNullPointer(*this, Loc, PExp, IsGNUIdiom);
    }
  }

  if (!checkArithmeticOpPointerOperand(*this, Loc, PExp))
    return QualType();

  if (isObjCPointer && checkArithmeticOnObjCPointer(*this, Loc, PExp))
    return QualType();

  // Arithmetic on label addresses is normally allowed, except when we add
  // a ptrauth signature to the addresses.
  if (isa<AddrLabelExpr>(PExp) && getLangOpts().PointerAuthIndirectGotos) {
    Diag(Loc, diag::err_ptrauth_indirect_goto_addrlabel_arithmetic)
        << /*addition*/ 1;
    return QualType();
  }

  // Check array bounds for pointer arithemtic
  CheckArrayAccess(PExp, IExp);

  if (CompLHSTy) {
    QualType LHSTy = Context.isPromotableBitField(LHS.get());
    if (LHSTy.isNull()) {
      LHSTy = LHS.get()->getType();
      if (Context.isPromotableIntegerType(LHSTy))
        LHSTy = Context.getPromotedIntegerType(LHSTy);
    }
    *CompLHSTy = LHSTy;
  }

  return PExp->getType();
}

// C99 6.5.6
QualType Sema::CheckSubtractionOperands(ExprResult &LHS, ExprResult &RHS,
                                        SourceLocation Loc,
                                        QualType* CompLHSTy) {
  checkArithmeticNull(*this, LHS, RHS, Loc, /*IsCompare=*/false);

  if (LHS.get()->getType()->isVectorType() ||
      RHS.get()->getType()->isVectorType()) {
    QualType compType =
        CheckVectorOperands(LHS, RHS, Loc, CompLHSTy,
                            /*AllowBothBool*/ getLangOpts().AltiVec,
                            /*AllowBoolConversions*/ getLangOpts().ZVector,
                            /*AllowBooleanOperation*/ false,
                            /*ReportInvalid*/ true);
    if (CompLHSTy) *CompLHSTy = compType;
    return compType;
  }

  if (LHS.get()->getType()->isSveVLSBuiltinType() ||
      RHS.get()->getType()->isSveVLSBuiltinType()) {
    QualType compType =
        CheckSizelessVectorOperands(LHS, RHS, Loc, CompLHSTy, ACK_Arithmetic);
    if (CompLHSTy)
      *CompLHSTy = compType;
    return compType;
  }

  if (LHS.get()->getType()->isConstantMatrixType() ||
      RHS.get()->getType()->isConstantMatrixType()) {
    QualType compType =
        CheckMatrixElementwiseOperands(LHS, RHS, Loc, CompLHSTy);
    if (CompLHSTy)
      *CompLHSTy = compType;
    return compType;
  }

  QualType compType = UsualArithmeticConversions(
      LHS, RHS, Loc, CompLHSTy ? ACK_CompAssign : ACK_Arithmetic);
  if (LHS.isInvalid() || RHS.isInvalid())
    return QualType();

  // Enforce type constraints: C99 6.5.6p3.

  // Handle the common case first (both operands are arithmetic).
  if (!compType.isNull() && compType->isArithmeticType()) {
    if (CompLHSTy) *CompLHSTy = compType;
    return compType;
  }

  // Either ptr - int   or   ptr - ptr.
  if (LHS.get()->getType()->isAnyPointerType()) {
    QualType lpointee = LHS.get()->getType()->getPointeeType();

    // Diagnose bad cases where we step over interface counts.
    if (LHS.get()->getType()->isObjCObjectPointerType() &&
        checkArithmeticOnObjCPointer(*this, Loc, LHS.get()))
      return QualType();

    // Arithmetic on label addresses is normally allowed, except when we add
    // a ptrauth signature to the addresses.
    if (isa<AddrLabelExpr>(LHS.get()) &&
        getLangOpts().PointerAuthIndirectGotos) {
      Diag(Loc, diag::err_ptrauth_indirect_goto_addrlabel_arithmetic)
          << /*subtraction*/ 0;
      return QualType();
    }

    // The result type of a pointer-int computation is the pointer type.
    if (RHS.get()->getType()->isIntegerType()) {
      // Subtracting from a null pointer should produce a warning.
      // The last argument to the diagnose call says this doesn't match the
      // GNU int-to-pointer idiom.
      if (LHS.get()->IgnoreParenCasts()->isNullPointerConstant(Context,
                                           Expr::NPC_ValueDependentIsNotNull)) {
        // In C++ adding zero to a null pointer is defined.
        Expr::EvalResult KnownVal;
        if (!getLangOpts().CPlusPlus ||
            (!RHS.get()->isValueDependent() &&
             (!RHS.get()->EvaluateAsInt(KnownVal, Context) ||
              KnownVal.Val.getInt() != 0))) {
          diagnoseArithmeticOnNullPointer(*this, Loc, LHS.get(), false);
        }
      }

      if (!checkArithmeticOpPointerOperand(*this, Loc, LHS.get()))
        return QualType();

      // Check array bounds for pointer arithemtic
      CheckArrayAccess(LHS.get(), RHS.get(), /*ArraySubscriptExpr*/nullptr,
                       /*AllowOnePastEnd*/true, /*IndexNegated*/true);

      if (CompLHSTy) *CompLHSTy = LHS.get()->getType();
      return LHS.get()->getType();
    }

    // Handle pointer-pointer subtractions.
    if (const PointerType *RHSPTy
          = RHS.get()->getType()->getAs<PointerType>()) {
      QualType rpointee = RHSPTy->getPointeeType();

      if (getLangOpts().CPlusPlus) {
        // Pointee types must be the same: C++ [expr.add]
        if (!Context.hasSameUnqualifiedType(lpointee, rpointee)) {
          diagnosePointerIncompatibility(*this, Loc, LHS.get(), RHS.get());
        }
      } else {
        // Pointee types must be compatible C99 6.5.6p3
        if (!Context.typesAreCompatible(
                Context.getCanonicalType(lpointee).getUnqualifiedType(),
                Context.getCanonicalType(rpointee).getUnqualifiedType())) {
          diagnosePointerIncompatibility(*this, Loc, LHS.get(), RHS.get());
          return QualType();
        }
      }

      if (!checkArithmeticBinOpPointerOperands(*this, Loc,
                                               LHS.get(), RHS.get()))
        return QualType();

      bool LHSIsNullPtr = LHS.get()->IgnoreParenCasts()->isNullPointerConstant(
          Context, Expr::NPC_ValueDependentIsNotNull);
      bool RHSIsNullPtr = RHS.get()->IgnoreParenCasts()->isNullPointerConstant(
          Context, Expr::NPC_ValueDependentIsNotNull);

      // Subtracting nullptr or from nullptr is suspect
      if (LHSIsNullPtr)
        diagnoseSubtractionOnNullPointer(*this, Loc, LHS.get(), RHSIsNullPtr);
      if (RHSIsNullPtr)
        diagnoseSubtractionOnNullPointer(*this, Loc, RHS.get(), LHSIsNullPtr);

      // The pointee type may have zero size.  As an extension, a structure or
      // union may have zero size or an array may have zero length.  In this
      // case subtraction does not make sense.
      if (!rpointee->isVoidType() && !rpointee->isFunctionType()) {
        CharUnits ElementSize = Context.getTypeSizeInChars(rpointee);
        if (ElementSize.isZero()) {
          Diag(Loc,diag::warn_sub_ptr_zero_size_types)
            << rpointee.getUnqualifiedType()
            << LHS.get()->getSourceRange() << RHS.get()->getSourceRange();
        }
      }

      if (CompLHSTy) *CompLHSTy = LHS.get()->getType();
      return Context.getPointerDiffType();
    }
  }

  return InvalidOperands(Loc, LHS, RHS);
}

static bool isScopedEnumerationType(QualType T) {
  if (const EnumType *ET = T->getAs<EnumType>())
    return ET->getDecl()->isScoped();
  return false;
}

static void DiagnoseBadShiftValues(Sema& S, ExprResult &LHS, ExprResult &RHS,
                                   SourceLocation Loc, BinaryOperatorKind Opc,
                                   QualType LHSType) {
  // OpenCL 6.3j: shift values are effectively % word size of LHS (more defined),
  // so skip remaining warnings as we don't want to modify values within Sema.
  if (S.getLangOpts().OpenCL)
    return;

  // Check right/shifter operand
  Expr::EvalResult RHSResult;
  if (RHS.get()->isValueDependent() ||
      !RHS.get()->EvaluateAsInt(RHSResult, S.Context))
    return;
  llvm::APSInt Right = RHSResult.Val.getInt();

  if (Right.isNegative()) {
    S.DiagRuntimeBehavior(Loc, RHS.get(),
                          S.PDiag(diag::warn_shift_negative)
                              << RHS.get()->getSourceRange());
    return;
  }

  QualType LHSExprType = LHS.get()->getType();
  uint64_t LeftSize = S.Context.getTypeSize(LHSExprType);
  if (LHSExprType->isBitIntType())
    LeftSize = S.Context.getIntWidth(LHSExprType);
  else if (LHSExprType->isFixedPointType()) {
    auto FXSema = S.Context.getFixedPointSemantics(LHSExprType);
    LeftSize = FXSema.getWidth() - (unsigned)FXSema.hasUnsignedPadding();
  }
  if (Right.uge(LeftSize)) {
    S.DiagRuntimeBehavior(Loc, RHS.get(),
                          S.PDiag(diag::warn_shift_gt_typewidth)
                              << RHS.get()->getSourceRange());
    return;
  }

  // FIXME: We probably need to handle fixed point types specially here.
  if (Opc != BO_Shl || LHSExprType->isFixedPointType())
    return;

  // When left shifting an ICE which is signed, we can check for overflow which
  // according to C++ standards prior to C++2a has undefined behavior
  // ([expr.shift] 5.8/2). Unsigned integers have defined behavior modulo one
  // more than the maximum value representable in the result type, so never
  // warn for those. (FIXME: Unsigned left-shift overflow in a constant
  // expression is still probably a bug.)
  Expr::EvalResult LHSResult;
  if (LHS.get()->isValueDependent() ||
      LHSType->hasUnsignedIntegerRepresentation() ||
      !LHS.get()->EvaluateAsInt(LHSResult, S.Context))
    return;
  llvm::APSInt Left = LHSResult.Val.getInt();

  // Don't warn if signed overflow is defined, then all the rest of the
  // diagnostics will not be triggered because the behavior is defined.
  // Also don't warn in C++20 mode (and newer), as signed left shifts
  // always wrap and never overflow.
  if (S.getLangOpts().isSignedOverflowDefined() || S.getLangOpts().CPlusPlus20)
    return;

  // If LHS does not have a non-negative value then, the
  // behavior is undefined before C++2a. Warn about it.
  if (Left.isNegative()) {
    S.DiagRuntimeBehavior(Loc, LHS.get(),
                          S.PDiag(diag::warn_shift_lhs_negative)
                              << LHS.get()->getSourceRange());
    return;
  }

  llvm::APInt ResultBits =
      static_cast<llvm::APInt &>(Right) + Left.getSignificantBits();
  if (ResultBits.ule(LeftSize))
    return;
  llvm::APSInt Result = Left.extend(ResultBits.getLimitedValue());
  Result = Result.shl(Right);

  // Print the bit representation of the signed integer as an unsigned
  // hexadecimal number.
  SmallString<40> HexResult;
  Result.toString(HexResult, 16, /*Signed =*/false, /*Literal =*/true);

  // If we are only missing a sign bit, this is less likely to result in actual
  // bugs -- if the result is cast back to an unsigned type, it will have the
  // expected value. Thus we place this behind a different warning that can be
  // turned off separately if needed.
  if (ResultBits - 1 == LeftSize) {
    S.Diag(Loc, diag::warn_shift_result_sets_sign_bit)
        << HexResult << LHSType
        << LHS.get()->getSourceRange() << RHS.get()->getSourceRange();
    return;
  }

  S.Diag(Loc, diag::warn_shift_result_gt_typewidth)
      << HexResult.str() << Result.getSignificantBits() << LHSType
      << Left.getBitWidth() << LHS.get()->getSourceRange()
      << RHS.get()->getSourceRange();
}

/// Return the resulting type when a vector is shifted
///        by a scalar or vector shift amount.
static QualType checkVectorShift(Sema &S, ExprResult &LHS, ExprResult &RHS,
                                 SourceLocation Loc, bool IsCompAssign) {
  // OpenCL v1.1 s6.3.j says RHS can be a vector only if LHS is a vector.
  if ((S.LangOpts.OpenCL || S.LangOpts.ZVector) &&
      !LHS.get()->getType()->isVectorType()) {
    S.Diag(Loc, diag::err_shift_rhs_only_vector)
      << RHS.get()->getType() << LHS.get()->getType()
      << LHS.get()->getSourceRange() << RHS.get()->getSourceRange();
    return QualType();
  }

  if (!IsCompAssign) {
    LHS = S.UsualUnaryConversions(LHS.get());
    if (LHS.isInvalid()) return QualType();
  }

  RHS = S.UsualUnaryConversions(RHS.get());
  if (RHS.isInvalid()) return QualType();

  QualType LHSType = LHS.get()->getType();
  // Note that LHS might be a scalar because the routine calls not only in
  // OpenCL case.
  const VectorType *LHSVecTy = LHSType->getAs<VectorType>();
  QualType LHSEleType = LHSVecTy ? LHSVecTy->getElementType() : LHSType;

  // Note that RHS might not be a vector.
  QualType RHSType = RHS.get()->getType();
  const VectorType *RHSVecTy = RHSType->getAs<VectorType>();
  QualType RHSEleType = RHSVecTy ? RHSVecTy->getElementType() : RHSType;

  // Do not allow shifts for boolean vectors.
  if ((LHSVecTy && LHSVecTy->isExtVectorBoolType()) ||
      (RHSVecTy && RHSVecTy->isExtVectorBoolType())) {
    S.Diag(Loc, diag::err_typecheck_invalid_operands)
        << LHS.get()->getType() << RHS.get()->getType()
        << LHS.get()->getSourceRange();
    return QualType();
  }

  // The operands need to be integers.
  if (!LHSEleType->isIntegerType()) {
    S.Diag(Loc, diag::err_typecheck_expect_int)
      << LHS.get()->getType() << LHS.get()->getSourceRange();
    return QualType();
  }

  if (!RHSEleType->isIntegerType()) {
    S.Diag(Loc, diag::err_typecheck_expect_int)
      << RHS.get()->getType() << RHS.get()->getSourceRange();
    return QualType();
  }

  if (!LHSVecTy) {
    assert(RHSVecTy);
    if (IsCompAssign)
      return RHSType;
    if (LHSEleType != RHSEleType) {
      LHS = S.ImpCastExprToType(LHS.get(),RHSEleType, CK_IntegralCast);
      LHSEleType = RHSEleType;
    }
    QualType VecTy =
        S.Context.getExtVectorType(LHSEleType, RHSVecTy->getNumElements());
    LHS = S.ImpCastExprToType(LHS.get(), VecTy, CK_VectorSplat);
    LHSType = VecTy;
  } else if (RHSVecTy) {
    // OpenCL v1.1 s6.3.j says that for vector types, the operators
    // are applied component-wise. So if RHS is a vector, then ensure
    // that the number of elements is the same as LHS...
    if (RHSVecTy->getNumElements() != LHSVecTy->getNumElements()) {
      S.Diag(Loc, diag::err_typecheck_vector_lengths_not_equal)
        << LHS.get()->getType() << RHS.get()->getType()
        << LHS.get()->getSourceRange() << RHS.get()->getSourceRange();
      return QualType();
    }
    if (!S.LangOpts.OpenCL && !S.LangOpts.ZVector) {
      const BuiltinType *LHSBT = LHSEleType->getAs<clang::BuiltinType>();
      const BuiltinType *RHSBT = RHSEleType->getAs<clang::BuiltinType>();
      if (LHSBT != RHSBT &&
          S.Context.getTypeSize(LHSBT) != S.Context.getTypeSize(RHSBT)) {
        S.Diag(Loc, diag::warn_typecheck_vector_element_sizes_not_equal)
            << LHS.get()->getType() << RHS.get()->getType()
            << LHS.get()->getSourceRange() << RHS.get()->getSourceRange();
      }
    }
  } else {
    // ...else expand RHS to match the number of elements in LHS.
    QualType VecTy =
      S.Context.getExtVectorType(RHSEleType, LHSVecTy->getNumElements());
    RHS = S.ImpCastExprToType(RHS.get(), VecTy, CK_VectorSplat);
  }

  return LHSType;
}

static QualType checkSizelessVectorShift(Sema &S, ExprResult &LHS,
                                         ExprResult &RHS, SourceLocation Loc,
                                         bool IsCompAssign) {
  if (!IsCompAssign) {
    LHS = S.UsualUnaryConversions(LHS.get());
    if (LHS.isInvalid())
      return QualType();
  }

  RHS = S.UsualUnaryConversions(RHS.get());
  if (RHS.isInvalid())
    return QualType();

  QualType LHSType = LHS.get()->getType();
  const BuiltinType *LHSBuiltinTy = LHSType->castAs<BuiltinType>();
  QualType LHSEleType = LHSType->isSveVLSBuiltinType()
                            ? LHSBuiltinTy->getSveEltType(S.getASTContext())
                            : LHSType;

  // Note that RHS might not be a vector
  QualType RHSType = RHS.get()->getType();
  const BuiltinType *RHSBuiltinTy = RHSType->castAs<BuiltinType>();
  QualType RHSEleType = RHSType->isSveVLSBuiltinType()
                            ? RHSBuiltinTy->getSveEltType(S.getASTContext())
                            : RHSType;

  if ((LHSBuiltinTy && LHSBuiltinTy->isSVEBool()) ||
      (RHSBuiltinTy && RHSBuiltinTy->isSVEBool())) {
    S.Diag(Loc, diag::err_typecheck_invalid_operands)
        << LHSType << RHSType << LHS.get()->getSourceRange();
    return QualType();
  }

  if (!LHSEleType->isIntegerType()) {
    S.Diag(Loc, diag::err_typecheck_expect_int)
        << LHS.get()->getType() << LHS.get()->getSourceRange();
    return QualType();
  }

  if (!RHSEleType->isIntegerType()) {
    S.Diag(Loc, diag::err_typecheck_expect_int)
        << RHS.get()->getType() << RHS.get()->getSourceRange();
    return QualType();
  }

  if (LHSType->isSveVLSBuiltinType() && RHSType->isSveVLSBuiltinType() &&
      (S.Context.getBuiltinVectorTypeInfo(LHSBuiltinTy).EC !=
       S.Context.getBuiltinVectorTypeInfo(RHSBuiltinTy).EC)) {
    S.Diag(Loc, diag::err_typecheck_invalid_operands)
        << LHSType << RHSType << LHS.get()->getSourceRange()
        << RHS.get()->getSourceRange();
    return QualType();
  }

  if (!LHSType->isSveVLSBuiltinType()) {
    assert(RHSType->isSveVLSBuiltinType());
    if (IsCompAssign)
      return RHSType;
    if (LHSEleType != RHSEleType) {
      LHS = S.ImpCastExprToType(LHS.get(), RHSEleType, clang::CK_IntegralCast);
      LHSEleType = RHSEleType;
    }
    const llvm::ElementCount VecSize =
        S.Context.getBuiltinVectorTypeInfo(RHSBuiltinTy).EC;
    QualType VecTy =
        S.Context.getScalableVectorType(LHSEleType, VecSize.getKnownMinValue());
    LHS = S.ImpCastExprToType(LHS.get(), VecTy, clang::CK_VectorSplat);
    LHSType = VecTy;
  } else if (RHSBuiltinTy && RHSBuiltinTy->isSveVLSBuiltinType()) {
    if (S.Context.getTypeSize(RHSBuiltinTy) !=
        S.Context.getTypeSize(LHSBuiltinTy)) {
      S.Diag(Loc, diag::err_typecheck_vector_lengths_not_equal)
          << LHSType << RHSType << LHS.get()->getSourceRange()
          << RHS.get()->getSourceRange();
      return QualType();
    }
  } else {
    const llvm::ElementCount VecSize =
        S.Context.getBuiltinVectorTypeInfo(LHSBuiltinTy).EC;
    if (LHSEleType != RHSEleType) {
      RHS = S.ImpCastExprToType(RHS.get(), LHSEleType, clang::CK_IntegralCast);
      RHSEleType = LHSEleType;
    }
    QualType VecTy =
        S.Context.getScalableVectorType(RHSEleType, VecSize.getKnownMinValue());
    RHS = S.ImpCastExprToType(RHS.get(), VecTy, CK_VectorSplat);
  }

  return LHSType;
}

// C99 6.5.7
QualType Sema::CheckShiftOperands(ExprResult &LHS, ExprResult &RHS,
                                  SourceLocation Loc, BinaryOperatorKind Opc,
                                  bool IsCompAssign) {
  checkArithmeticNull(*this, LHS, RHS, Loc, /*IsCompare=*/false);

  // Vector shifts promote their scalar inputs to vector type.
  if (LHS.get()->getType()->isVectorType() ||
      RHS.get()->getType()->isVectorType()) {
    if (LangOpts.ZVector) {
      // The shift operators for the z vector extensions work basically
      // like general shifts, except that neither the LHS nor the RHS is
      // allowed to be a "vector bool".
      if (auto LHSVecType = LHS.get()->getType()->getAs<VectorType>())
        if (LHSVecType->getVectorKind() == VectorKind::AltiVecBool)
          return InvalidOperands(Loc, LHS, RHS);
      if (auto RHSVecType = RHS.get()->getType()->getAs<VectorType>())
        if (RHSVecType->getVectorKind() == VectorKind::AltiVecBool)
          return InvalidOperands(Loc, LHS, RHS);
    }
    return checkVectorShift(*this, LHS, RHS, Loc, IsCompAssign);
  }

  if (LHS.get()->getType()->isSveVLSBuiltinType() ||
      RHS.get()->getType()->isSveVLSBuiltinType())
    return checkSizelessVectorShift(*this, LHS, RHS, Loc, IsCompAssign);

  // Shifts don't perform usual arithmetic conversions, they just do integer
  // promotions on each operand. C99 6.5.7p3

  // For the LHS, do usual unary conversions, but then reset them away
  // if this is a compound assignment.
  ExprResult OldLHS = LHS;
  LHS = UsualUnaryConversions(LHS.get());
  if (LHS.isInvalid())
    return QualType();
  QualType LHSType = LHS.get()->getType();
  if (IsCompAssign) LHS = OldLHS;

  // The RHS is simpler.
  RHS = UsualUnaryConversions(RHS.get());
  if (RHS.isInvalid())
    return QualType();
  QualType RHSType = RHS.get()->getType();

  // C99 6.5.7p2: Each of the operands shall have integer type.
  // Embedded-C 4.1.6.2.2: The LHS may also be fixed-point.
  if ((!LHSType->isFixedPointOrIntegerType() &&
       !LHSType->hasIntegerRepresentation()) ||
      !RHSType->hasIntegerRepresentation())
    return InvalidOperands(Loc, LHS, RHS);

  // C++0x: Don't allow scoped enums. FIXME: Use something better than
  // hasIntegerRepresentation() above instead of this.
  if (isScopedEnumerationType(LHSType) ||
      isScopedEnumerationType(RHSType)) {
    return InvalidOperands(Loc, LHS, RHS);
  }
  DiagnoseBadShiftValues(*this, LHS, RHS, Loc, Opc, LHSType);

  // "The type of the result is that of the promoted left operand."
  return LHSType;
}

/// Diagnose bad pointer comparisons.
static void diagnoseDistinctPointerComparison(Sema &S, SourceLocation Loc,
                                              ExprResult &LHS, ExprResult &RHS,
                                              bool IsError) {
  S.Diag(Loc, IsError ? diag::err_typecheck_comparison_of_distinct_pointers
                      : diag::ext_typecheck_comparison_of_distinct_pointers)
    << LHS.get()->getType() << RHS.get()->getType()
    << LHS.get()->getSourceRange() << RHS.get()->getSourceRange();
}

/// Returns false if the pointers are converted to a composite type,
/// true otherwise.
static bool convertPointersToCompositeType(Sema &S, SourceLocation Loc,
                                           ExprResult &LHS, ExprResult &RHS) {
  // C++ [expr.rel]p2:
  //   [...] Pointer conversions (4.10) and qualification
  //   conversions (4.4) are performed on pointer operands (or on
  //   a pointer operand and a null pointer constant) to bring
  //   them to their composite pointer type. [...]
  //
  // C++ [expr.eq]p1 uses the same notion for (in)equality
  // comparisons of pointers.

  QualType LHSType = LHS.get()->getType();
  QualType RHSType = RHS.get()->getType();
  assert(LHSType->isPointerType() || RHSType->isPointerType() ||
         LHSType->isMemberPointerType() || RHSType->isMemberPointerType());

  QualType T = S.FindCompositePointerType(Loc, LHS, RHS);
  if (T.isNull()) {
    if ((LHSType->isAnyPointerType() || LHSType->isMemberPointerType()) &&
        (RHSType->isAnyPointerType() || RHSType->isMemberPointerType()))
      diagnoseDistinctPointerComparison(S, Loc, LHS, RHS, /*isError*/true);
    else
      S.InvalidOperands(Loc, LHS, RHS);
    return true;
  }

  return false;
}

static void diagnoseFunctionPointerToVoidComparison(Sema &S, SourceLocation Loc,
                                                    ExprResult &LHS,
                                                    ExprResult &RHS,
                                                    bool IsError) {
  S.Diag(Loc, IsError ? diag::err_typecheck_comparison_of_fptr_to_void
                      : diag::ext_typecheck_comparison_of_fptr_to_void)
    << LHS.get()->getType() << RHS.get()->getType()
    << LHS.get()->getSourceRange() << RHS.get()->getSourceRange();
}

static bool isObjCObjectLiteral(ExprResult &E) {
  switch (E.get()->IgnoreParenImpCasts()->getStmtClass()) {
  case Stmt::ObjCArrayLiteralClass:
  case Stmt::ObjCDictionaryLiteralClass:
  case Stmt::ObjCStringLiteralClass:
  case Stmt::ObjCBoxedExprClass:
    return true;
  default:
    // Note that ObjCBoolLiteral is NOT an object literal!
    return false;
  }
}

static bool hasIsEqualMethod(Sema &S, const Expr *LHS, const Expr *RHS) {
  const ObjCObjectPointerType *Type =
    LHS->getType()->getAs<ObjCObjectPointerType>();

  // If this is not actually an Objective-C object, bail out.
  if (!Type)
    return false;

  // Get the LHS object's interface type.
  QualType InterfaceType = Type->getPointeeType();

  // If the RHS isn't an Objective-C object, bail out.
  if (!RHS->getType()->isObjCObjectPointerType())
    return false;

  // Try to find the -isEqual: method.
  Selector IsEqualSel = S.ObjC().NSAPIObj->getIsEqualSelector();
  ObjCMethodDecl *Method =
      S.ObjC().LookupMethodInObjectType(IsEqualSel, InterfaceType,
                                        /*IsInstance=*/true);
  if (!Method) {
    if (Type->isObjCIdType()) {
      // For 'id', just check the global pool.
      Method =
          S.ObjC().LookupInstanceMethodInGlobalPool(IsEqualSel, SourceRange(),
                                                    /*receiverId=*/true);
    } else {
      // Check protocols.
      Method = S.ObjC().LookupMethodInQualifiedType(IsEqualSel, Type,
                                                    /*IsInstance=*/true);
    }
  }

  if (!Method)
    return false;

  QualType T = Method->parameters()[0]->getType();
  if (!T->isObjCObjectPointerType())
    return false;

  QualType R = Method->getReturnType();
  if (!R->isScalarType())
    return false;

  return true;
}

static void diagnoseObjCLiteralComparison(Sema &S, SourceLocation Loc,
                                          ExprResult &LHS, ExprResult &RHS,
                                          BinaryOperator::Opcode Opc){
  Expr *Literal;
  Expr *Other;
  if (isObjCObjectLiteral(LHS)) {
    Literal = LHS.get();
    Other = RHS.get();
  } else {
    Literal = RHS.get();
    Other = LHS.get();
  }

  // Don't warn on comparisons against nil.
  Other = Other->IgnoreParenCasts();
  if (Other->isNullPointerConstant(S.getASTContext(),
                                   Expr::NPC_ValueDependentIsNotNull))
    return;

  // This should be kept in sync with warn_objc_literal_comparison.
  // LK_String should always be after the other literals, since it has its own
  // warning flag.
  SemaObjC::ObjCLiteralKind LiteralKind = S.ObjC().CheckLiteralKind(Literal);
  assert(LiteralKind != SemaObjC::LK_Block);
  if (LiteralKind == SemaObjC::LK_None) {
    llvm_unreachable("Unknown Objective-C object literal kind");
  }

  if (LiteralKind == SemaObjC::LK_String)
    S.Diag(Loc, diag::warn_objc_string_literal_comparison)
      << Literal->getSourceRange();
  else
    S.Diag(Loc, diag::warn_objc_literal_comparison)
      << LiteralKind << Literal->getSourceRange();

  if (BinaryOperator::isEqualityOp(Opc) &&
      hasIsEqualMethod(S, LHS.get(), RHS.get())) {
    SourceLocation Start = LHS.get()->getBeginLoc();
    SourceLocation End = S.getLocForEndOfToken(RHS.get()->getEndLoc());
    CharSourceRange OpRange =
      CharSourceRange::getCharRange(Loc, S.getLocForEndOfToken(Loc));

    S.Diag(Loc, diag::note_objc_literal_comparison_isequal)
      << FixItHint::CreateInsertion(Start, Opc == BO_EQ ? "[" : "![")
      << FixItHint::CreateReplacement(OpRange, " isEqual:")
      << FixItHint::CreateInsertion(End, "]");
  }
}

/// Warns on !x < y, !x & y where !(x < y), !(x & y) was probably intended.
static void diagnoseLogicalNotOnLHSofCheck(Sema &S, ExprResult &LHS,
                                           ExprResult &RHS, SourceLocation Loc,
                                           BinaryOperatorKind Opc) {
  // Check that left hand side is !something.
  UnaryOperator *UO = dyn_cast<UnaryOperator>(LHS.get()->IgnoreImpCasts());
  if (!UO || UO->getOpcode() != UO_LNot) return;

  // Only check if the right hand side is non-bool arithmetic type.
  if (RHS.get()->isKnownToHaveBooleanValue()) return;

  // Make sure that the something in !something is not bool.
  Expr *SubExpr = UO->getSubExpr()->IgnoreImpCasts();
  if (SubExpr->isKnownToHaveBooleanValue()) return;

  // Emit warning.
  bool IsBitwiseOp = Opc == BO_And || Opc == BO_Or || Opc == BO_Xor;
  S.Diag(UO->getOperatorLoc(), diag::warn_logical_not_on_lhs_of_check)
      << Loc << IsBitwiseOp;

  // First note suggest !(x < y)
  SourceLocation FirstOpen = SubExpr->getBeginLoc();
  SourceLocation FirstClose = RHS.get()->getEndLoc();
  FirstClose = S.getLocForEndOfToken(FirstClose);
  if (FirstClose.isInvalid())
    FirstOpen = SourceLocation();
  S.Diag(UO->getOperatorLoc(), diag::note_logical_not_fix)
      << IsBitwiseOp
      << FixItHint::CreateInsertion(FirstOpen, "(")
      << FixItHint::CreateInsertion(FirstClose, ")");

  // Second note suggests (!x) < y
  SourceLocation SecondOpen = LHS.get()->getBeginLoc();
  SourceLocation SecondClose = LHS.get()->getEndLoc();
  SecondClose = S.getLocForEndOfToken(SecondClose);
  if (SecondClose.isInvalid())
    SecondOpen = SourceLocation();
  S.Diag(UO->getOperatorLoc(), diag::note_logical_not_silence_with_parens)
      << FixItHint::CreateInsertion(SecondOpen, "(")
      << FixItHint::CreateInsertion(SecondClose, ")");
}

// Returns true if E refers to a non-weak array.
static bool checkForArray(const Expr *E) {
  const ValueDecl *D = nullptr;
  if (const DeclRefExpr *DR = dyn_cast<DeclRefExpr>(E)) {
    D = DR->getDecl();
  } else if (const MemberExpr *Mem = dyn_cast<MemberExpr>(E)) {
    if (Mem->isImplicitAccess())
      D = Mem->getMemberDecl();
  }
  if (!D)
    return false;
  return D->getType()->isArrayType() && !D->isWeak();
}

/// Diagnose some forms of syntactically-obvious tautological comparison.
static void diagnoseTautologicalComparison(Sema &S, SourceLocation Loc,
                                           Expr *LHS, Expr *RHS,
                                           BinaryOperatorKind Opc) {
  Expr *LHSStripped = LHS->IgnoreParenImpCasts();
  Expr *RHSStripped = RHS->IgnoreParenImpCasts();

  QualType LHSType = LHS->getType();
  QualType RHSType = RHS->getType();
  if (LHSType->hasFloatingRepresentation() ||
      (LHSType->isBlockPointerType() && !BinaryOperator::isEqualityOp(Opc)) ||
      S.inTemplateInstantiation())
    return;

  // WebAssembly Tables cannot be compared, therefore shouldn't emit
  // Tautological diagnostics.
  if (LHSType->isWebAssemblyTableType() || RHSType->isWebAssemblyTableType())
    return;

  // Comparisons between two array types are ill-formed for operator<=>, so
  // we shouldn't emit any additional warnings about it.
  if (Opc == BO_Cmp && LHSType->isArrayType() && RHSType->isArrayType())
    return;

  // For non-floating point types, check for self-comparisons of the form
  // x == x, x != x, x < x, etc.  These always evaluate to a constant, and
  // often indicate logic errors in the program.
  //
  // NOTE: Don't warn about comparison expressions resulting from macro
  // expansion. Also don't warn about comparisons which are only self
  // comparisons within a template instantiation. The warnings should catch
  // obvious cases in the definition of the template anyways. The idea is to
  // warn when the typed comparison operator will always evaluate to the same
  // result.

  // Used for indexing into %select in warn_comparison_always
  enum {
    AlwaysConstant,
    AlwaysTrue,
    AlwaysFalse,
    AlwaysEqual, // std::strong_ordering::equal from operator<=>
  };

  // C++2a [depr.array.comp]:
  //   Equality and relational comparisons ([expr.eq], [expr.rel]) between two
  //   operands of array type are deprecated.
  if (S.getLangOpts().CPlusPlus20 && LHSStripped->getType()->isArrayType() &&
      RHSStripped->getType()->isArrayType()) {
    S.Diag(Loc, diag::warn_depr_array_comparison)
        << LHS->getSourceRange() << RHS->getSourceRange()
        << LHSStripped->getType() << RHSStripped->getType();
    // Carry on to produce the tautological comparison warning, if this
    // expression is potentially-evaluated, we can resolve the array to a
    // non-weak declaration, and so on.
  }

  if (!LHS->getBeginLoc().isMacroID() && !RHS->getBeginLoc().isMacroID()) {
    if (Expr::isSameComparisonOperand(LHS, RHS)) {
      unsigned Result;
      switch (Opc) {
      case BO_EQ:
      case BO_LE:
      case BO_GE:
        Result = AlwaysTrue;
        break;
      case BO_NE:
      case BO_LT:
      case BO_GT:
        Result = AlwaysFalse;
        break;
      case BO_Cmp:
        Result = AlwaysEqual;
        break;
      default:
        Result = AlwaysConstant;
        break;
      }
      S.DiagRuntimeBehavior(Loc, nullptr,
                            S.PDiag(diag::warn_comparison_always)
                                << 0 /*self-comparison*/
                                << Result);
    } else if (checkForArray(LHSStripped) && checkForArray(RHSStripped)) {
      // What is it always going to evaluate to?
      unsigned Result;
      switch (Opc) {
      case BO_EQ: // e.g. array1 == array2
        Result = AlwaysFalse;
        break;
      case BO_NE: // e.g. array1 != array2
        Result = AlwaysTrue;
        break;
      default: // e.g. array1 <= array2
        // The best we can say is 'a constant'
        Result = AlwaysConstant;
        break;
      }
      S.DiagRuntimeBehavior(Loc, nullptr,
                            S.PDiag(diag::warn_comparison_always)
                                << 1 /*array comparison*/
                                << Result);
    }
  }

  if (isa<CastExpr>(LHSStripped))
    LHSStripped = LHSStripped->IgnoreParenCasts();
  if (isa<CastExpr>(RHSStripped))
    RHSStripped = RHSStripped->IgnoreParenCasts();

  // Warn about comparisons against a string constant (unless the other
  // operand is null); the user probably wants string comparison function.
  Expr *LiteralString = nullptr;
  Expr *LiteralStringStripped = nullptr;
  if ((isa<StringLiteral>(LHSStripped) || isa<ObjCEncodeExpr>(LHSStripped)) &&
      !RHSStripped->isNullPointerConstant(S.Context,
                                          Expr::NPC_ValueDependentIsNull)) {
    LiteralString = LHS;
    LiteralStringStripped = LHSStripped;
  } else if ((isa<StringLiteral>(RHSStripped) ||
              isa<ObjCEncodeExpr>(RHSStripped)) &&
             !LHSStripped->isNullPointerConstant(S.Context,
                                          Expr::NPC_ValueDependentIsNull)) {
    LiteralString = RHS;
    LiteralStringStripped = RHSStripped;
  }

  if (LiteralString) {
    S.DiagRuntimeBehavior(Loc, nullptr,
                          S.PDiag(diag::warn_stringcompare)
                              << isa<ObjCEncodeExpr>(LiteralStringStripped)
                              << LiteralString->getSourceRange());
  }
}

static ImplicitConversionKind castKindToImplicitConversionKind(CastKind CK) {
  switch (CK) {
  default: {
#ifndef NDEBUG
    llvm::errs() << "unhandled cast kind: " << CastExpr::getCastKindName(CK)
                 << "\n";
#endif
    llvm_unreachable("unhandled cast kind");
  }
  case CK_UserDefinedConversion:
    return ICK_Identity;
  case CK_LValueToRValue:
    return ICK_Lvalue_To_Rvalue;
  case CK_ArrayToPointerDecay:
    return ICK_Array_To_Pointer;
  case CK_FunctionToPointerDecay:
    return ICK_Function_To_Pointer;
  case CK_IntegralCast:
    return ICK_Integral_Conversion;
  case CK_FloatingCast:
    return ICK_Floating_Conversion;
  case CK_IntegralToFloating:
  case CK_FloatingToIntegral:
    return ICK_Floating_Integral;
  case CK_IntegralComplexCast:
  case CK_FloatingComplexCast:
  case CK_FloatingComplexToIntegralComplex:
  case CK_IntegralComplexToFloatingComplex:
    return ICK_Complex_Conversion;
  case CK_FloatingComplexToReal:
  case CK_FloatingRealToComplex:
  case CK_IntegralComplexToReal:
  case CK_IntegralRealToComplex:
    return ICK_Complex_Real;
  case CK_HLSLArrayRValue:
    return ICK_HLSL_Array_RValue;
  }
}

static bool checkThreeWayNarrowingConversion(Sema &S, QualType ToType, Expr *E,
                                             QualType FromType,
                                             SourceLocation Loc) {
  // Check for a narrowing implicit conversion.
  StandardConversionSequence SCS;
  SCS.setAsIdentityConversion();
  SCS.setToType(0, FromType);
  SCS.setToType(1, ToType);
  if (const auto *ICE = dyn_cast<ImplicitCastExpr>(E))
    SCS.Second = castKindToImplicitConversionKind(ICE->getCastKind());

  APValue PreNarrowingValue;
  QualType PreNarrowingType;
  switch (SCS.getNarrowingKind(S.Context, E, PreNarrowingValue,
                               PreNarrowingType,
                               /*IgnoreFloatToIntegralConversion*/ true)) {
  case NK_Dependent_Narrowing:
    // Implicit conversion to a narrower type, but the expression is
    // value-dependent so we can't tell whether it's actually narrowing.
  case NK_Not_Narrowing:
    return false;

  case NK_Constant_Narrowing:
    // Implicit conversion to a narrower type, and the value is not a constant
    // expression.
    S.Diag(E->getBeginLoc(), diag::err_spaceship_argument_narrowing)
        << /*Constant*/ 1
        << PreNarrowingValue.getAsString(S.Context, PreNarrowingType) << ToType;
    return true;

  case NK_Variable_Narrowing:
    // Implicit conversion to a narrower type, and the value is not a constant
    // expression.
  case NK_Type_Narrowing:
    S.Diag(E->getBeginLoc(), diag::err_spaceship_argument_narrowing)
        << /*Constant*/ 0 << FromType << ToType;
    // TODO: It's not a constant expression, but what if the user intended it
    // to be? Can we produce notes to help them figure out why it isn't?
    return true;
  }
  llvm_unreachable("unhandled case in switch");
}

static QualType checkArithmeticOrEnumeralThreeWayCompare(Sema &S,
                                                         ExprResult &LHS,
                                                         ExprResult &RHS,
                                                         SourceLocation Loc) {
  QualType LHSType = LHS.get()->getType();
  QualType RHSType = RHS.get()->getType();
  // Dig out the original argument type and expression before implicit casts
  // were applied. These are the types/expressions we need to check the
  // [expr.spaceship] requirements against.
  ExprResult LHSStripped = LHS.get()->IgnoreParenImpCasts();
  ExprResult RHSStripped = RHS.get()->IgnoreParenImpCasts();
  QualType LHSStrippedType = LHSStripped.get()->getType();
  QualType RHSStrippedType = RHSStripped.get()->getType();

  // C++2a [expr.spaceship]p3: If one of the operands is of type bool and the
  // other is not, the program is ill-formed.
  if (LHSStrippedType->isBooleanType() != RHSStrippedType->isBooleanType()) {
    S.InvalidOperands(Loc, LHSStripped, RHSStripped);
    return QualType();
  }

  // FIXME: Consider combining this with checkEnumArithmeticConversions.
  int NumEnumArgs = (int)LHSStrippedType->isEnumeralType() +
                    RHSStrippedType->isEnumeralType();
  if (NumEnumArgs == 1) {
    bool LHSIsEnum = LHSStrippedType->isEnumeralType();
    QualType OtherTy = LHSIsEnum ? RHSStrippedType : LHSStrippedType;
    if (OtherTy->hasFloatingRepresentation()) {
      S.InvalidOperands(Loc, LHSStripped, RHSStripped);
      return QualType();
    }
  }
  if (NumEnumArgs == 2) {
    // C++2a [expr.spaceship]p5: If both operands have the same enumeration
    // type E, the operator yields the result of converting the operands
    // to the underlying type of E and applying <=> to the converted operands.
    if (!S.Context.hasSameUnqualifiedType(LHSStrippedType, RHSStrippedType)) {
      S.InvalidOperands(Loc, LHS, RHS);
      return QualType();
    }
    QualType IntType =
        LHSStrippedType->castAs<EnumType>()->getDecl()->getIntegerType();
    assert(IntType->isArithmeticType());

    // We can't use `CK_IntegralCast` when the underlying type is 'bool', so we
    // promote the boolean type, and all other promotable integer types, to
    // avoid this.
    if (S.Context.isPromotableIntegerType(IntType))
      IntType = S.Context.getPromotedIntegerType(IntType);

    LHS = S.ImpCastExprToType(LHS.get(), IntType, CK_IntegralCast);
    RHS = S.ImpCastExprToType(RHS.get(), IntType, CK_IntegralCast);
    LHSType = RHSType = IntType;
  }

  // C++2a [expr.spaceship]p4: If both operands have arithmetic types, the
  // usual arithmetic conversions are applied to the operands.
  QualType Type =
      S.UsualArithmeticConversions(LHS, RHS, Loc, Sema::ACK_Comparison);
  if (LHS.isInvalid() || RHS.isInvalid())
    return QualType();
  if (Type.isNull())
    return S.InvalidOperands(Loc, LHS, RHS);

  std::optional<ComparisonCategoryType> CCT =
      getComparisonCategoryForBuiltinCmp(Type);
  if (!CCT)
    return S.InvalidOperands(Loc, LHS, RHS);

  bool HasNarrowing = checkThreeWayNarrowingConversion(
      S, Type, LHS.get(), LHSType, LHS.get()->getBeginLoc());
  HasNarrowing |= checkThreeWayNarrowingConversion(S, Type, RHS.get(), RHSType,
                                                   RHS.get()->getBeginLoc());
  if (HasNarrowing)
    return QualType();

  assert(!Type.isNull() && "composite type for <=> has not been set");

  return S.CheckComparisonCategoryType(
      *CCT, Loc, Sema::ComparisonCategoryUsage::OperatorInExpression);
}

static QualType checkArithmeticOrEnumeralCompare(Sema &S, ExprResult &LHS,
                                                 ExprResult &RHS,
                                                 SourceLocation Loc,
                                                 BinaryOperatorKind Opc) {
  if (Opc == BO_Cmp)
    return checkArithmeticOrEnumeralThreeWayCompare(S, LHS, RHS, Loc);

  // C99 6.5.8p3 / C99 6.5.9p4
  QualType Type =
      S.UsualArithmeticConversions(LHS, RHS, Loc, Sema::ACK_Comparison);
  if (LHS.isInvalid() || RHS.isInvalid())
    return QualType();
  if (Type.isNull())
    return S.InvalidOperands(Loc, LHS, RHS);
  assert(Type->isArithmeticType() || Type->isEnumeralType());

  if (Type->isAnyComplexType() && BinaryOperator::isRelationalOp(Opc))
    return S.InvalidOperands(Loc, LHS, RHS);

  // Check for comparisons of floating point operands using != and ==.
  if (Type->hasFloatingRepresentation())
    S.CheckFloatComparison(Loc, LHS.get(), RHS.get(), Opc);

  // The result of comparisons is 'bool' in C++, 'int' in C.
  return S.Context.getLogicalOperationType();
}

void Sema::CheckPtrComparisonWithNullChar(ExprResult &E, ExprResult &NullE) {
  if (!NullE.get()->getType()->isAnyPointerType())
    return;
  int NullValue = PP.isMacroDefined("NULL") ? 0 : 1;
  if (!E.get()->getType()->isAnyPointerType() &&
      E.get()->isNullPointerConstant(Context,
                                     Expr::NPC_ValueDependentIsNotNull) ==
        Expr::NPCK_ZeroExpression) {
    if (const auto *CL = dyn_cast<CharacterLiteral>(E.get())) {
      if (CL->getValue() == 0)
        Diag(E.get()->getExprLoc(), diag::warn_pointer_compare)
            << NullValue
            << FixItHint::CreateReplacement(E.get()->getExprLoc(),
                                            NullValue ? "NULL" : "(void *)0");
    } else if (const auto *CE = dyn_cast<CStyleCastExpr>(E.get())) {
        TypeSourceInfo *TI = CE->getTypeInfoAsWritten();
        QualType T = Context.getCanonicalType(TI->getType()).getUnqualifiedType();
        if (T == Context.CharTy)
          Diag(E.get()->getExprLoc(), diag::warn_pointer_compare)
              << NullValue
              << FixItHint::CreateReplacement(E.get()->getExprLoc(),
                                              NullValue ? "NULL" : "(void *)0");
      }
  }
}

// C99 6.5.8, C++ [expr.rel]
QualType Sema::CheckCompareOperands(ExprResult &LHS, ExprResult &RHS,
                                    SourceLocation Loc,
                                    BinaryOperatorKind Opc) {
  bool IsRelational = BinaryOperator::isRelationalOp(Opc);
  bool IsThreeWay = Opc == BO_Cmp;
  bool IsOrdered = IsRelational || IsThreeWay;
  auto IsAnyPointerType = [](ExprResult E) {
    QualType Ty = E.get()->getType();
    return Ty->isPointerType() || Ty->isMemberPointerType();
  };

  // C++2a [expr.spaceship]p6: If at least one of the operands is of pointer
  // type, array-to-pointer, ..., conversions are performed on both operands to
  // bring them to their composite type.
  // Otherwise, all comparisons expect an rvalue, so convert to rvalue before
  // any type-related checks.
  if (!IsThreeWay || IsAnyPointerType(LHS) || IsAnyPointerType(RHS)) {
    LHS = DefaultFunctionArrayLvalueConversion(LHS.get());
    if (LHS.isInvalid())
      return QualType();
    RHS = DefaultFunctionArrayLvalueConversion(RHS.get());
    if (RHS.isInvalid())
      return QualType();
  } else {
    LHS = DefaultLvalueConversion(LHS.get());
    if (LHS.isInvalid())
      return QualType();
    RHS = DefaultLvalueConversion(RHS.get());
    if (RHS.isInvalid())
      return QualType();
  }

  checkArithmeticNull(*this, LHS, RHS, Loc, /*IsCompare=*/true);
  if (!getLangOpts().CPlusPlus && BinaryOperator::isEqualityOp(Opc)) {
    CheckPtrComparisonWithNullChar(LHS, RHS);
    CheckPtrComparisonWithNullChar(RHS, LHS);
  }

  // Handle vector comparisons separately.
  if (LHS.get()->getType()->isVectorType() ||
      RHS.get()->getType()->isVectorType())
    return CheckVectorCompareOperands(LHS, RHS, Loc, Opc);

  if (LHS.get()->getType()->isSveVLSBuiltinType() ||
      RHS.get()->getType()->isSveVLSBuiltinType())
    return CheckSizelessVectorCompareOperands(LHS, RHS, Loc, Opc);

  diagnoseLogicalNotOnLHSofCheck(*this, LHS, RHS, Loc, Opc);
  diagnoseTautologicalComparison(*this, Loc, LHS.get(), RHS.get(), Opc);

  QualType LHSType = LHS.get()->getType();
  QualType RHSType = RHS.get()->getType();
  if ((LHSType->isArithmeticType() || LHSType->isEnumeralType()) &&
      (RHSType->isArithmeticType() || RHSType->isEnumeralType()))
    return checkArithmeticOrEnumeralCompare(*this, LHS, RHS, Loc, Opc);

  if ((LHSType->isPointerType() &&
       LHSType->getPointeeType().isWebAssemblyReferenceType()) ||
      (RHSType->isPointerType() &&
       RHSType->getPointeeType().isWebAssemblyReferenceType()))
    return InvalidOperands(Loc, LHS, RHS);

  const Expr::NullPointerConstantKind LHSNullKind =
      LHS.get()->isNullPointerConstant(Context, Expr::NPC_ValueDependentIsNull);
  const Expr::NullPointerConstantKind RHSNullKind =
      RHS.get()->isNullPointerConstant(Context, Expr::NPC_ValueDependentIsNull);
  bool LHSIsNull = LHSNullKind != Expr::NPCK_NotNull;
  bool RHSIsNull = RHSNullKind != Expr::NPCK_NotNull;

  auto computeResultTy = [&]() {
    if (Opc != BO_Cmp)
      return Context.getLogicalOperationType();
    assert(getLangOpts().CPlusPlus);
    assert(Context.hasSameType(LHS.get()->getType(), RHS.get()->getType()));

    QualType CompositeTy = LHS.get()->getType();
    assert(!CompositeTy->isReferenceType());

    std::optional<ComparisonCategoryType> CCT =
        getComparisonCategoryForBuiltinCmp(CompositeTy);
    if (!CCT)
      return InvalidOperands(Loc, LHS, RHS);

    if (CompositeTy->isPointerType() && LHSIsNull != RHSIsNull) {
      // P0946R0: Comparisons between a null pointer constant and an object
      // pointer result in std::strong_equality, which is ill-formed under
      // P1959R0.
      Diag(Loc, diag::err_typecheck_three_way_comparison_of_pointer_and_zero)
          << (LHSIsNull ? LHS.get()->getSourceRange()
                        : RHS.get()->getSourceRange());
      return QualType();
    }

    return CheckComparisonCategoryType(
        *CCT, Loc, ComparisonCategoryUsage::OperatorInExpression);
  };

  if (!IsOrdered && LHSIsNull != RHSIsNull) {
    bool IsEquality = Opc == BO_EQ;
    if (RHSIsNull)
      DiagnoseAlwaysNonNullPointer(LHS.get(), RHSNullKind, IsEquality,
                                   RHS.get()->getSourceRange());
    else
      DiagnoseAlwaysNonNullPointer(RHS.get(), LHSNullKind, IsEquality,
                                   LHS.get()->getSourceRange());
  }

  if (IsOrdered && LHSType->isFunctionPointerType() &&
      RHSType->isFunctionPointerType()) {
    // Valid unless a relational comparison of function pointers
    bool IsError = Opc == BO_Cmp;
    auto DiagID =
        IsError ? diag::err_typecheck_ordered_comparison_of_function_pointers
        : getLangOpts().CPlusPlus
            ? diag::warn_typecheck_ordered_comparison_of_function_pointers
            : diag::ext_typecheck_ordered_comparison_of_function_pointers;
    Diag(Loc, DiagID) << LHSType << RHSType << LHS.get()->getSourceRange()
                      << RHS.get()->getSourceRange();
    if (IsError)
      return QualType();
  }

  if ((LHSType->isIntegerType() && !LHSIsNull) ||
      (RHSType->isIntegerType() && !RHSIsNull)) {
    // Skip normal pointer conversion checks in this case; we have better
    // diagnostics for this below.
  } else if (getLangOpts().CPlusPlus) {
    // Equality comparison of a function pointer to a void pointer is invalid,
    // but we allow it as an extension.
    // FIXME: If we really want to allow this, should it be part of composite
    // pointer type computation so it works in conditionals too?
    if (!IsOrdered &&
        ((LHSType->isFunctionPointerType() && RHSType->isVoidPointerType()) ||
         (RHSType->isFunctionPointerType() && LHSType->isVoidPointerType()))) {
      // This is a gcc extension compatibility comparison.
      // In a SFINAE context, we treat this as a hard error to maintain
      // conformance with the C++ standard.
      diagnoseFunctionPointerToVoidComparison(
          *this, Loc, LHS, RHS, /*isError*/ (bool)isSFINAEContext());

      if (isSFINAEContext())
        return QualType();

      RHS = ImpCastExprToType(RHS.get(), LHSType, CK_BitCast);
      return computeResultTy();
    }

    // C++ [expr.eq]p2:
    //   If at least one operand is a pointer [...] bring them to their
    //   composite pointer type.
    // C++ [expr.spaceship]p6
    //  If at least one of the operands is of pointer type, [...] bring them
    //  to their composite pointer type.
    // C++ [expr.rel]p2:
    //   If both operands are pointers, [...] bring them to their composite
    //   pointer type.
    // For <=>, the only valid non-pointer types are arrays and functions, and
    // we already decayed those, so this is really the same as the relational
    // comparison rule.
    if ((int)LHSType->isPointerType() + (int)RHSType->isPointerType() >=
            (IsOrdered ? 2 : 1) &&
        (!LangOpts.ObjCAutoRefCount || !(LHSType->isObjCObjectPointerType() ||
                                         RHSType->isObjCObjectPointerType()))) {
      if (convertPointersToCompositeType(*this, Loc, LHS, RHS))
        return QualType();
      return computeResultTy();
    }
  } else if (LHSType->isPointerType() &&
             RHSType->isPointerType()) { // C99 6.5.8p2
    // All of the following pointer-related warnings are GCC extensions, except
    // when handling null pointer constants.
    QualType LCanPointeeTy =
      LHSType->castAs<PointerType>()->getPointeeType().getCanonicalType();
    QualType RCanPointeeTy =
      RHSType->castAs<PointerType>()->getPointeeType().getCanonicalType();

    // C99 6.5.9p2 and C99 6.5.8p2
    if (Context.typesAreCompatible(LCanPointeeTy.getUnqualifiedType(),
                                   RCanPointeeTy.getUnqualifiedType())) {
      if (IsRelational) {
        // Pointers both need to point to complete or incomplete types
        if ((LCanPointeeTy->isIncompleteType() !=
             RCanPointeeTy->isIncompleteType()) &&
            !getLangOpts().C11) {
          Diag(Loc, diag::ext_typecheck_compare_complete_incomplete_pointers)
              << LHS.get()->getSourceRange() << RHS.get()->getSourceRange()
              << LHSType << RHSType << LCanPointeeTy->isIncompleteType()
              << RCanPointeeTy->isIncompleteType();
        }
      }
    } else if (!IsRelational &&
               (LCanPointeeTy->isVoidType() || RCanPointeeTy->isVoidType())) {
      // Valid unless comparison between non-null pointer and function pointer
      if ((LCanPointeeTy->isFunctionType() || RCanPointeeTy->isFunctionType())
          && !LHSIsNull && !RHSIsNull)
        diagnoseFunctionPointerToVoidComparison(*this, Loc, LHS, RHS,
                                                /*isError*/false);
    } else {
      // Invalid
      diagnoseDistinctPointerComparison(*this, Loc, LHS, RHS, /*isError*/false);
    }
    if (LCanPointeeTy != RCanPointeeTy) {
      // Treat NULL constant as a special case in OpenCL.
      if (getLangOpts().OpenCL && !LHSIsNull && !RHSIsNull) {
        if (!LCanPointeeTy.isAddressSpaceOverlapping(RCanPointeeTy)) {
          Diag(Loc,
               diag::err_typecheck_op_on_nonoverlapping_address_space_pointers)
              << LHSType << RHSType << 0 /* comparison */
              << LHS.get()->getSourceRange() << RHS.get()->getSourceRange();
        }
      }
      LangAS AddrSpaceL = LCanPointeeTy.getAddressSpace();
      LangAS AddrSpaceR = RCanPointeeTy.getAddressSpace();
      CastKind Kind = AddrSpaceL != AddrSpaceR ? CK_AddressSpaceConversion
                                               : CK_BitCast;
      if (LHSIsNull && !RHSIsNull)
        LHS = ImpCastExprToType(LHS.get(), RHSType, Kind);
      else
        RHS = ImpCastExprToType(RHS.get(), LHSType, Kind);
    }
    return computeResultTy();
  }


  // C++ [expr.eq]p4:
  //   Two operands of type std::nullptr_t or one operand of type
  //   std::nullptr_t and the other a null pointer constant compare
  //   equal.
  // C23 6.5.9p5:
  //   If both operands have type nullptr_t or one operand has type nullptr_t
  //   and the other is a null pointer constant, they compare equal if the
  //   former is a null pointer.
  if (!IsOrdered && LHSIsNull && RHSIsNull) {
    if (LHSType->isNullPtrType()) {
      RHS = ImpCastExprToType(RHS.get(), LHSType, CK_NullToPointer);
      return computeResultTy();
    }
    if (RHSType->isNullPtrType()) {
      LHS = ImpCastExprToType(LHS.get(), RHSType, CK_NullToPointer);
      return computeResultTy();
    }
  }

  if (!getLangOpts().CPlusPlus && !IsOrdered && (LHSIsNull || RHSIsNull)) {
    // C23 6.5.9p6:
    //   Otherwise, at least one operand is a pointer. If one is a pointer and
    //   the other is a null pointer constant or has type nullptr_t, they
    //   compare equal
    if (LHSIsNull && RHSType->isPointerType()) {
      LHS = ImpCastExprToType(LHS.get(), RHSType, CK_NullToPointer);
      return computeResultTy();
    }
    if (RHSIsNull && LHSType->isPointerType()) {
      RHS = ImpCastExprToType(RHS.get(), LHSType, CK_NullToPointer);
      return computeResultTy();
    }
  }

  // Comparison of Objective-C pointers and block pointers against nullptr_t.
  // These aren't covered by the composite pointer type rules.
  if (!IsOrdered && RHSType->isNullPtrType() &&
      (LHSType->isObjCObjectPointerType() || LHSType->isBlockPointerType())) {
    RHS = ImpCastExprToType(RHS.get(), LHSType, CK_NullToPointer);
    return computeResultTy();
  }
  if (!IsOrdered && LHSType->isNullPtrType() &&
      (RHSType->isObjCObjectPointerType() || RHSType->isBlockPointerType())) {
    LHS = ImpCastExprToType(LHS.get(), RHSType, CK_NullToPointer);
    return computeResultTy();
  }

  if (getLangOpts().CPlusPlus) {
    if (IsRelational &&
        ((LHSType->isNullPtrType() && RHSType->isPointerType()) ||
         (RHSType->isNullPtrType() && LHSType->isPointerType()))) {
      // HACK: Relational comparison of nullptr_t against a pointer type is
      // invalid per DR583, but we allow it within std::less<> and friends,
      // since otherwise common uses of it break.
      // FIXME: Consider removing this hack once LWG fixes std::less<> and
      // friends to have std::nullptr_t overload candidates.
      DeclContext *DC = CurContext;
      if (isa<FunctionDecl>(DC))
        DC = DC->getParent();
      if (auto *CTSD = dyn_cast<ClassTemplateSpecializationDecl>(DC)) {
        if (CTSD->isInStdNamespace() &&
            llvm::StringSwitch<bool>(CTSD->getName())
                .Cases("less", "less_equal", "greater", "greater_equal", true)
                .Default(false)) {
          if (RHSType->isNullPtrType())
            RHS = ImpCastExprToType(RHS.get(), LHSType, CK_NullToPointer);
          else
            LHS = ImpCastExprToType(LHS.get(), RHSType, CK_NullToPointer);
          return computeResultTy();
        }
      }
    }

    // C++ [expr.eq]p2:
    //   If at least one operand is a pointer to member, [...] bring them to
    //   their composite pointer type.
    if (!IsOrdered &&
        (LHSType->isMemberPointerType() || RHSType->isMemberPointerType())) {
      if (convertPointersToCompositeType(*this, Loc, LHS, RHS))
        return QualType();
      else
        return computeResultTy();
    }
  }

  // Handle block pointer types.
  if (!IsOrdered && LHSType->isBlockPointerType() &&
      RHSType->isBlockPointerType()) {
    QualType lpointee = LHSType->castAs<BlockPointerType>()->getPointeeType();
    QualType rpointee = RHSType->castAs<BlockPointerType>()->getPointeeType();

    if (!LHSIsNull && !RHSIsNull &&
        !Context.typesAreCompatible(lpointee, rpointee)) {
      Diag(Loc, diag::err_typecheck_comparison_of_distinct_blocks)
        << LHSType << RHSType << LHS.get()->getSourceRange()
        << RHS.get()->getSourceRange();
    }
    RHS = ImpCastExprToType(RHS.get(), LHSType, CK_BitCast);
    return computeResultTy();
  }

  // Allow block pointers to be compared with null pointer constants.
  if (!IsOrdered
      && ((LHSType->isBlockPointerType() && RHSType->isPointerType())
          || (LHSType->isPointerType() && RHSType->isBlockPointerType()))) {
    if (!LHSIsNull && !RHSIsNull) {
      if (!((RHSType->isPointerType() && RHSType->castAs<PointerType>()
             ->getPointeeType()->isVoidType())
            || (LHSType->isPointerType() && LHSType->castAs<PointerType>()
                ->getPointeeType()->isVoidType())))
        Diag(Loc, diag::err_typecheck_comparison_of_distinct_blocks)
          << LHSType << RHSType << LHS.get()->getSourceRange()
          << RHS.get()->getSourceRange();
    }
    if (LHSIsNull && !RHSIsNull)
      LHS = ImpCastExprToType(LHS.get(), RHSType,
                              RHSType->isPointerType() ? CK_BitCast
                                : CK_AnyPointerToBlockPointerCast);
    else
      RHS = ImpCastExprToType(RHS.get(), LHSType,
                              LHSType->isPointerType() ? CK_BitCast
                                : CK_AnyPointerToBlockPointerCast);
    return computeResultTy();
  }

  if (LHSType->isObjCObjectPointerType() ||
      RHSType->isObjCObjectPointerType()) {
    const PointerType *LPT = LHSType->getAs<PointerType>();
    const PointerType *RPT = RHSType->getAs<PointerType>();
    if (LPT || RPT) {
      bool LPtrToVoid = LPT ? LPT->getPointeeType()->isVoidType() : false;
      bool RPtrToVoid = RPT ? RPT->getPointeeType()->isVoidType() : false;

      if (!LPtrToVoid && !RPtrToVoid &&
          !Context.typesAreCompatible(LHSType, RHSType)) {
        diagnoseDistinctPointerComparison(*this, Loc, LHS, RHS,
                                          /*isError*/false);
      }
      // FIXME: If LPtrToVoid, we should presumably convert the LHS rather than
      // the RHS, but we have test coverage for this behavior.
      // FIXME: Consider using convertPointersToCompositeType in C++.
      if (LHSIsNull && !RHSIsNull) {
        Expr *E = LHS.get();
        if (getLangOpts().ObjCAutoRefCount)
          ObjC().CheckObjCConversion(SourceRange(), RHSType, E,
                                     CheckedConversionKind::Implicit);
        LHS = ImpCastExprToType(E, RHSType,
                                RPT ? CK_BitCast :CK_CPointerToObjCPointerCast);
      }
      else {
        Expr *E = RHS.get();
        if (getLangOpts().ObjCAutoRefCount)
          ObjC().CheckObjCConversion(SourceRange(), LHSType, E,
                                     CheckedConversionKind::Implicit,
                                     /*Diagnose=*/true,
                                     /*DiagnoseCFAudited=*/false, Opc);
        RHS = ImpCastExprToType(E, LHSType,
                                LPT ? CK_BitCast :CK_CPointerToObjCPointerCast);
      }
      return computeResultTy();
    }
    if (LHSType->isObjCObjectPointerType() &&
        RHSType->isObjCObjectPointerType()) {
      if (!Context.areComparableObjCPointerTypes(LHSType, RHSType))
        diagnoseDistinctPointerComparison(*this, Loc, LHS, RHS,
                                          /*isError*/false);
      if (isObjCObjectLiteral(LHS) || isObjCObjectLiteral(RHS))
        diagnoseObjCLiteralComparison(*this, Loc, LHS, RHS, Opc);

      if (LHSIsNull && !RHSIsNull)
        LHS = ImpCastExprToType(LHS.get(), RHSType, CK_BitCast);
      else
        RHS = ImpCastExprToType(RHS.get(), LHSType, CK_BitCast);
      return computeResultTy();
    }

    if (!IsOrdered && LHSType->isBlockPointerType() &&
        RHSType->isBlockCompatibleObjCPointerType(Context)) {
      LHS = ImpCastExprToType(LHS.get(), RHSType,
                              CK_BlockPointerToObjCPointerCast);
      return computeResultTy();
    } else if (!IsOrdered &&
               LHSType->isBlockCompatibleObjCPointerType(Context) &&
               RHSType->isBlockPointerType()) {
      RHS = ImpCastExprToType(RHS.get(), LHSType,
                              CK_BlockPointerToObjCPointerCast);
      return computeResultTy();
    }
  }
  if ((LHSType->isAnyPointerType() && RHSType->isIntegerType()) ||
      (LHSType->isIntegerType() && RHSType->isAnyPointerType())) {
    unsigned DiagID = 0;
    bool isError = false;
    if (LangOpts.DebuggerSupport) {
      // Under a debugger, allow the comparison of pointers to integers,
      // since users tend to want to compare addresses.
    } else if ((LHSIsNull && LHSType->isIntegerType()) ||
               (RHSIsNull && RHSType->isIntegerType())) {
      if (IsOrdered) {
        isError = getLangOpts().CPlusPlus;
        DiagID =
          isError ? diag::err_typecheck_ordered_comparison_of_pointer_and_zero
                  : diag::ext_typecheck_ordered_comparison_of_pointer_and_zero;
      }
    } else if (getLangOpts().CPlusPlus) {
      DiagID = diag::err_typecheck_comparison_of_pointer_integer;
      isError = true;
    } else if (IsOrdered)
      DiagID = diag::ext_typecheck_ordered_comparison_of_pointer_integer;
    else
      DiagID = diag::ext_typecheck_comparison_of_pointer_integer;

    if (DiagID) {
      Diag(Loc, DiagID)
        << LHSType << RHSType << LHS.get()->getSourceRange()
        << RHS.get()->getSourceRange();
      if (isError)
        return QualType();
    }

    if (LHSType->isIntegerType())
      LHS = ImpCastExprToType(LHS.get(), RHSType,
                        LHSIsNull ? CK_NullToPointer : CK_IntegralToPointer);
    else
      RHS = ImpCastExprToType(RHS.get(), LHSType,
                        RHSIsNull ? CK_NullToPointer : CK_IntegralToPointer);
    return computeResultTy();
  }

  // Handle block pointers.
  if (!IsOrdered && RHSIsNull
      && LHSType->isBlockPointerType() && RHSType->isIntegerType()) {
    RHS = ImpCastExprToType(RHS.get(), LHSType, CK_NullToPointer);
    return computeResultTy();
  }
  if (!IsOrdered && LHSIsNull
      && LHSType->isIntegerType() && RHSType->isBlockPointerType()) {
    LHS = ImpCastExprToType(LHS.get(), RHSType, CK_NullToPointer);
    return computeResultTy();
  }

  if (getLangOpts().getOpenCLCompatibleVersion() >= 200) {
    if (LHSType->isClkEventT() && RHSType->isClkEventT()) {
      return computeResultTy();
    }

    if (LHSType->isQueueT() && RHSType->isQueueT()) {
      return computeResultTy();
    }

    if (LHSIsNull && RHSType->isQueueT()) {
      LHS = ImpCastExprToType(LHS.get(), RHSType, CK_NullToPointer);
      return computeResultTy();
    }

    if (LHSType->isQueueT() && RHSIsNull) {
      RHS = ImpCastExprToType(RHS.get(), LHSType, CK_NullToPointer);
      return computeResultTy();
    }
  }

  return InvalidOperands(Loc, LHS, RHS);
}

QualType Sema::GetSignedVectorType(QualType V) {
  const VectorType *VTy = V->castAs<VectorType>();
  unsigned TypeSize = Context.getTypeSize(VTy->getElementType());

  if (isa<ExtVectorType>(VTy)) {
    if (VTy->isExtVectorBoolType())
      return Context.getExtVectorType(Context.BoolTy, VTy->getNumElements());
    if (TypeSize == Context.getTypeSize(Context.CharTy))
      return Context.getExtVectorType(Context.CharTy, VTy->getNumElements());
    if (TypeSize == Context.getTypeSize(Context.ShortTy))
      return Context.getExtVectorType(Context.ShortTy, VTy->getNumElements());
    if (TypeSize == Context.getTypeSize(Context.IntTy))
      return Context.getExtVectorType(Context.IntTy, VTy->getNumElements());
    if (TypeSize == Context.getTypeSize(Context.Int128Ty))
      return Context.getExtVectorType(Context.Int128Ty, VTy->getNumElements());
    if (TypeSize == Context.getTypeSize(Context.LongTy))
      return Context.getExtVectorType(Context.LongTy, VTy->getNumElements());
    assert(TypeSize == Context.getTypeSize(Context.LongLongTy) &&
           "Unhandled vector element size in vector compare");
    return Context.getExtVectorType(Context.LongLongTy, VTy->getNumElements());
  }

  if (TypeSize == Context.getTypeSize(Context.Int128Ty))
    return Context.getVectorType(Context.Int128Ty, VTy->getNumElements(),
                                 VectorKind::Generic);
  if (TypeSize == Context.getTypeSize(Context.LongLongTy))
    return Context.getVectorType(Context.LongLongTy, VTy->getNumElements(),
                                 VectorKind::Generic);
  if (TypeSize == Context.getTypeSize(Context.LongTy))
    return Context.getVectorType(Context.LongTy, VTy->getNumElements(),
                                 VectorKind::Generic);
  if (TypeSize == Context.getTypeSize(Context.IntTy))
    return Context.getVectorType(Context.IntTy, VTy->getNumElements(),
                                 VectorKind::Generic);
  if (TypeSize == Context.getTypeSize(Context.ShortTy))
    return Context.getVectorType(Context.ShortTy, VTy->getNumElements(),
                                 VectorKind::Generic);
  assert(TypeSize == Context.getTypeSize(Context.CharTy) &&
         "Unhandled vector element size in vector compare");
  return Context.getVectorType(Context.CharTy, VTy->getNumElements(),
                               VectorKind::Generic);
}

QualType Sema::GetSignedSizelessVectorType(QualType V) {
  const BuiltinType *VTy = V->castAs<BuiltinType>();
  assert(VTy->isSizelessBuiltinType() && "expected sizeless type");

  const QualType ETy = V->getSveEltType(Context);
  const auto TypeSize = Context.getTypeSize(ETy);

  const QualType IntTy = Context.getIntTypeForBitwidth(TypeSize, true);
  const llvm::ElementCount VecSize = Context.getBuiltinVectorTypeInfo(VTy).EC;
  return Context.getScalableVectorType(IntTy, VecSize.getKnownMinValue());
}

QualType Sema::CheckVectorCompareOperands(ExprResult &LHS, ExprResult &RHS,
                                          SourceLocation Loc,
                                          BinaryOperatorKind Opc) {
  if (Opc == BO_Cmp) {
    Diag(Loc, diag::err_three_way_vector_comparison);
    return QualType();
  }

  // Check to make sure we're operating on vectors of the same type and width,
  // Allowing one side to be a scalar of element type.
  QualType vType =
      CheckVectorOperands(LHS, RHS, Loc, /*isCompAssign*/ false,
                          /*AllowBothBool*/ true,
                          /*AllowBoolConversions*/ getLangOpts().ZVector,
                          /*AllowBooleanOperation*/ true,
                          /*ReportInvalid*/ true);
  if (vType.isNull())
    return vType;

  QualType LHSType = LHS.get()->getType();

  // Determine the return type of a vector compare. By default clang will return
  // a scalar for all vector compares except vector bool and vector pixel.
  // With the gcc compiler we will always return a vector type and with the xl
  // compiler we will always return a scalar type. This switch allows choosing
  // which behavior is prefered.
  if (getLangOpts().AltiVec) {
    switch (getLangOpts().getAltivecSrcCompat()) {
    case LangOptions::AltivecSrcCompatKind::Mixed:
      // If AltiVec, the comparison results in a numeric type, i.e.
      // bool for C++, int for C
      if (vType->castAs<VectorType>()->getVectorKind() ==
          VectorKind::AltiVecVector)
        return Context.getLogicalOperationType();
      else
        Diag(Loc, diag::warn_deprecated_altivec_src_compat);
      break;
    case LangOptions::AltivecSrcCompatKind::GCC:
      // For GCC we always return the vector type.
      break;
    case LangOptions::AltivecSrcCompatKind::XL:
      return Context.getLogicalOperationType();
      break;
    }
  }

  // For non-floating point types, check for self-comparisons of the form
  // x == x, x != x, x < x, etc.  These always evaluate to a constant, and
  // often indicate logic errors in the program.
  diagnoseTautologicalComparison(*this, Loc, LHS.get(), RHS.get(), Opc);

  // Check for comparisons of floating point operands using != and ==.
  if (LHSType->hasFloatingRepresentation()) {
    assert(RHS.get()->getType()->hasFloatingRepresentation());
    CheckFloatComparison(Loc, LHS.get(), RHS.get(), Opc);
  }

  // Return a signed type for the vector.
  return GetSignedVectorType(vType);
}

QualType Sema::CheckSizelessVectorCompareOperands(ExprResult &LHS,
                                                  ExprResult &RHS,
                                                  SourceLocation Loc,
                                                  BinaryOperatorKind Opc) {
  if (Opc == BO_Cmp) {
    Diag(Loc, diag::err_three_way_vector_comparison);
    return QualType();
  }

  // Check to make sure we're operating on vectors of the same type and width,
  // Allowing one side to be a scalar of element type.
  QualType vType = CheckSizelessVectorOperands(
      LHS, RHS, Loc, /*isCompAssign*/ false, ACK_Comparison);

  if (vType.isNull())
    return vType;

  QualType LHSType = LHS.get()->getType();

  // For non-floating point types, check for self-comparisons of the form
  // x == x, x != x, x < x, etc.  These always evaluate to a constant, and
  // often indicate logic errors in the program.
  diagnoseTautologicalComparison(*this, Loc, LHS.get(), RHS.get(), Opc);

  // Check for comparisons of floating point operands using != and ==.
  if (LHSType->hasFloatingRepresentation()) {
    assert(RHS.get()->getType()->hasFloatingRepresentation());
    CheckFloatComparison(Loc, LHS.get(), RHS.get(), Opc);
  }

  const BuiltinType *LHSBuiltinTy = LHSType->getAs<BuiltinType>();
  const BuiltinType *RHSBuiltinTy = RHS.get()->getType()->getAs<BuiltinType>();

  if (LHSBuiltinTy && RHSBuiltinTy && LHSBuiltinTy->isSVEBool() &&
      RHSBuiltinTy->isSVEBool())
    return LHSType;

  // Return a signed type for the vector.
  return GetSignedSizelessVectorType(vType);
}

static void diagnoseXorMisusedAsPow(Sema &S, const ExprResult &XorLHS,
                                    const ExprResult &XorRHS,
                                    const SourceLocation Loc) {
  // Do not diagnose macros.
  if (Loc.isMacroID())
    return;

  // Do not diagnose if both LHS and RHS are macros.
  if (XorLHS.get()->getExprLoc().isMacroID() &&
      XorRHS.get()->getExprLoc().isMacroID())
    return;

  bool Negative = false;
  bool ExplicitPlus = false;
  const auto *LHSInt = dyn_cast<IntegerLiteral>(XorLHS.get());
  const auto *RHSInt = dyn_cast<IntegerLiteral>(XorRHS.get());

  if (!LHSInt)
    return;
  if (!RHSInt) {
    // Check negative literals.
    if (const auto *UO = dyn_cast<UnaryOperator>(XorRHS.get())) {
      UnaryOperatorKind Opc = UO->getOpcode();
      if (Opc != UO_Minus && Opc != UO_Plus)
        return;
      RHSInt = dyn_cast<IntegerLiteral>(UO->getSubExpr());
      if (!RHSInt)
        return;
      Negative = (Opc == UO_Minus);
      ExplicitPlus = !Negative;
    } else {
      return;
    }
  }

  const llvm::APInt &LeftSideValue = LHSInt->getValue();
  llvm::APInt RightSideValue = RHSInt->getValue();
  if (LeftSideValue != 2 && LeftSideValue != 10)
    return;

  if (LeftSideValue.getBitWidth() != RightSideValue.getBitWidth())
    return;

  CharSourceRange ExprRange = CharSourceRange::getCharRange(
      LHSInt->getBeginLoc(), S.getLocForEndOfToken(RHSInt->getLocation()));
  llvm::StringRef ExprStr =
      Lexer::getSourceText(ExprRange, S.getSourceManager(), S.getLangOpts());

  CharSourceRange XorRange =
      CharSourceRange::getCharRange(Loc, S.getLocForEndOfToken(Loc));
  llvm::StringRef XorStr =
      Lexer::getSourceText(XorRange, S.getSourceManager(), S.getLangOpts());
  // Do not diagnose if xor keyword/macro is used.
  if (XorStr == "xor")
    return;

  std::string LHSStr = std::string(Lexer::getSourceText(
      CharSourceRange::getTokenRange(LHSInt->getSourceRange()),
      S.getSourceManager(), S.getLangOpts()));
  std::string RHSStr = std::string(Lexer::getSourceText(
      CharSourceRange::getTokenRange(RHSInt->getSourceRange()),
      S.getSourceManager(), S.getLangOpts()));

  if (Negative) {
    RightSideValue = -RightSideValue;
    RHSStr = "-" + RHSStr;
  } else if (ExplicitPlus) {
    RHSStr = "+" + RHSStr;
  }

  StringRef LHSStrRef = LHSStr;
  StringRef RHSStrRef = RHSStr;
  // Do not diagnose literals with digit separators, binary, hexadecimal, octal
  // literals.
  if (LHSStrRef.starts_with("0b") || LHSStrRef.starts_with("0B") ||
      RHSStrRef.starts_with("0b") || RHSStrRef.starts_with("0B") ||
      LHSStrRef.starts_with("0x") || LHSStrRef.starts_with("0X") ||
      RHSStrRef.starts_with("0x") || RHSStrRef.starts_with("0X") ||
      (LHSStrRef.size() > 1 && LHSStrRef.starts_with("0")) ||
      (RHSStrRef.size() > 1 && RHSStrRef.starts_with("0")) ||
      LHSStrRef.contains('\'') || RHSStrRef.contains('\''))
    return;

  bool SuggestXor =
      S.getLangOpts().CPlusPlus || S.getPreprocessor().isMacroDefined("xor");
  const llvm::APInt XorValue = LeftSideValue ^ RightSideValue;
  int64_t RightSideIntValue = RightSideValue.getSExtValue();
  if (LeftSideValue == 2 && RightSideIntValue >= 0) {
    std::string SuggestedExpr = "1 << " + RHSStr;
    bool Overflow = false;
    llvm::APInt One = (LeftSideValue - 1);
    llvm::APInt PowValue = One.sshl_ov(RightSideValue, Overflow);
    if (Overflow) {
      if (RightSideIntValue < 64)
        S.Diag(Loc, diag::warn_xor_used_as_pow_base)
            << ExprStr << toString(XorValue, 10, true) << ("1LL << " + RHSStr)
            << FixItHint::CreateReplacement(ExprRange, "1LL << " + RHSStr);
      else if (RightSideIntValue == 64)
        S.Diag(Loc, diag::warn_xor_used_as_pow)
            << ExprStr << toString(XorValue, 10, true);
      else
        return;
    } else {
      S.Diag(Loc, diag::warn_xor_used_as_pow_base_extra)
          << ExprStr << toString(XorValue, 10, true) << SuggestedExpr
          << toString(PowValue, 10, true)
          << FixItHint::CreateReplacement(
                 ExprRange, (RightSideIntValue == 0) ? "1" : SuggestedExpr);
    }

    S.Diag(Loc, diag::note_xor_used_as_pow_silence)
        << ("0x2 ^ " + RHSStr) << SuggestXor;
  } else if (LeftSideValue == 10) {
    std::string SuggestedValue = "1e" + std::to_string(RightSideIntValue);
    S.Diag(Loc, diag::warn_xor_used_as_pow_base)
        << ExprStr << toString(XorValue, 10, true) << SuggestedValue
        << FixItHint::CreateReplacement(ExprRange, SuggestedValue);
    S.Diag(Loc, diag::note_xor_used_as_pow_silence)
        << ("0xA ^ " + RHSStr) << SuggestXor;
  }
}

QualType Sema::CheckVectorLogicalOperands(ExprResult &LHS, ExprResult &RHS,
                                          SourceLocation Loc) {
  // Ensure that either both operands are of the same vector type, or
  // one operand is of a vector type and the other is of its element type.
  QualType vType = CheckVectorOperands(LHS, RHS, Loc, false,
                                       /*AllowBothBool*/ true,
                                       /*AllowBoolConversions*/ false,
                                       /*AllowBooleanOperation*/ false,
                                       /*ReportInvalid*/ false);
  if (vType.isNull())
    return InvalidOperands(Loc, LHS, RHS);
  if (getLangOpts().OpenCL &&
      getLangOpts().getOpenCLCompatibleVersion() < 120 &&
      vType->hasFloatingRepresentation())
    return InvalidOperands(Loc, LHS, RHS);
  // FIXME: The check for C++ here is for GCC compatibility. GCC rejects the
  //        usage of the logical operators && and || with vectors in C. This
  //        check could be notionally dropped.
  if (!getLangOpts().CPlusPlus &&
      !(isa<ExtVectorType>(vType->getAs<VectorType>())))
    return InvalidLogicalVectorOperands(Loc, LHS, RHS);

  return GetSignedVectorType(LHS.get()->getType());
}

QualType Sema::CheckMatrixElementwiseOperands(ExprResult &LHS, ExprResult &RHS,
                                              SourceLocation Loc,
                                              bool IsCompAssign) {
  if (!IsCompAssign) {
    LHS = DefaultFunctionArrayLvalueConversion(LHS.get());
    if (LHS.isInvalid())
      return QualType();
  }
  RHS = DefaultFunctionArrayLvalueConversion(RHS.get());
  if (RHS.isInvalid())
    return QualType();

  // For conversion purposes, we ignore any qualifiers.
  // For example, "const float" and "float" are equivalent.
  QualType LHSType = LHS.get()->getType().getUnqualifiedType();
  QualType RHSType = RHS.get()->getType().getUnqualifiedType();

  const MatrixType *LHSMatType = LHSType->getAs<MatrixType>();
  const MatrixType *RHSMatType = RHSType->getAs<MatrixType>();
  assert((LHSMatType || RHSMatType) && "At least one operand must be a matrix");

  if (Context.hasSameType(LHSType, RHSType))
    return Context.getCommonSugaredType(LHSType, RHSType);

  // Type conversion may change LHS/RHS. Keep copies to the original results, in
  // case we have to return InvalidOperands.
  ExprResult OriginalLHS = LHS;
  ExprResult OriginalRHS = RHS;
  if (LHSMatType && !RHSMatType) {
    RHS = tryConvertExprToType(RHS.get(), LHSMatType->getElementType());
    if (!RHS.isInvalid())
      return LHSType;

    return InvalidOperands(Loc, OriginalLHS, OriginalRHS);
  }

  if (!LHSMatType && RHSMatType) {
    LHS = tryConvertExprToType(LHS.get(), RHSMatType->getElementType());
    if (!LHS.isInvalid())
      return RHSType;
    return InvalidOperands(Loc, OriginalLHS, OriginalRHS);
  }

  return InvalidOperands(Loc, LHS, RHS);
}

QualType Sema::CheckMatrixMultiplyOperands(ExprResult &LHS, ExprResult &RHS,
                                           SourceLocation Loc,
                                           bool IsCompAssign) {
  if (!IsCompAssign) {
    LHS = DefaultFunctionArrayLvalueConversion(LHS.get());
    if (LHS.isInvalid())
      return QualType();
  }
  RHS = DefaultFunctionArrayLvalueConversion(RHS.get());
  if (RHS.isInvalid())
    return QualType();

  auto *LHSMatType = LHS.get()->getType()->getAs<ConstantMatrixType>();
  auto *RHSMatType = RHS.get()->getType()->getAs<ConstantMatrixType>();
  assert((LHSMatType || RHSMatType) && "At least one operand must be a matrix");

  if (LHSMatType && RHSMatType) {
    if (LHSMatType->getNumColumns() != RHSMatType->getNumRows())
      return InvalidOperands(Loc, LHS, RHS);

    if (Context.hasSameType(LHSMatType, RHSMatType))
      return Context.getCommonSugaredType(
          LHS.get()->getType().getUnqualifiedType(),
          RHS.get()->getType().getUnqualifiedType());

    QualType LHSELTy = LHSMatType->getElementType(),
             RHSELTy = RHSMatType->getElementType();
    if (!Context.hasSameType(LHSELTy, RHSELTy))
      return InvalidOperands(Loc, LHS, RHS);

    return Context.getConstantMatrixType(
        Context.getCommonSugaredType(LHSELTy, RHSELTy),
        LHSMatType->getNumRows(), RHSMatType->getNumColumns());
  }
  return CheckMatrixElementwiseOperands(LHS, RHS, Loc, IsCompAssign);
}

static bool isLegalBoolVectorBinaryOp(BinaryOperatorKind Opc) {
  switch (Opc) {
  default:
    return false;
  case BO_And:
  case BO_AndAssign:
  case BO_Or:
  case BO_OrAssign:
  case BO_Xor:
  case BO_XorAssign:
    return true;
  }
}

inline QualType Sema::CheckBitwiseOperands(ExprResult &LHS, ExprResult &RHS,
                                           SourceLocation Loc,
                                           BinaryOperatorKind Opc) {
  checkArithmeticNull(*this, LHS, RHS, Loc, /*IsCompare=*/false);

  bool IsCompAssign =
      Opc == BO_AndAssign || Opc == BO_OrAssign || Opc == BO_XorAssign;

  bool LegalBoolVecOperator = isLegalBoolVectorBinaryOp(Opc);

  if (LHS.get()->getType()->isVectorType() ||
      RHS.get()->getType()->isVectorType()) {
    if (LHS.get()->getType()->hasIntegerRepresentation() &&
        RHS.get()->getType()->hasIntegerRepresentation())
      return CheckVectorOperands(LHS, RHS, Loc, IsCompAssign,
                                 /*AllowBothBool*/ true,
                                 /*AllowBoolConversions*/ getLangOpts().ZVector,
                                 /*AllowBooleanOperation*/ LegalBoolVecOperator,
                                 /*ReportInvalid*/ true);
    return InvalidOperands(Loc, LHS, RHS);
  }

  if (LHS.get()->getType()->isSveVLSBuiltinType() ||
      RHS.get()->getType()->isSveVLSBuiltinType()) {
    if (LHS.get()->getType()->hasIntegerRepresentation() &&
        RHS.get()->getType()->hasIntegerRepresentation())
      return CheckSizelessVectorOperands(LHS, RHS, Loc, IsCompAssign,
                                         ACK_BitwiseOp);
    return InvalidOperands(Loc, LHS, RHS);
  }

  if (LHS.get()->getType()->isSveVLSBuiltinType() ||
      RHS.get()->getType()->isSveVLSBuiltinType()) {
    if (LHS.get()->getType()->hasIntegerRepresentation() &&
        RHS.get()->getType()->hasIntegerRepresentation())
      return CheckSizelessVectorOperands(LHS, RHS, Loc, IsCompAssign,
                                         ACK_BitwiseOp);
    return InvalidOperands(Loc, LHS, RHS);
  }

  if (Opc == BO_And)
    diagnoseLogicalNotOnLHSofCheck(*this, LHS, RHS, Loc, Opc);

  if (LHS.get()->getType()->hasFloatingRepresentation() ||
      RHS.get()->getType()->hasFloatingRepresentation())
    return InvalidOperands(Loc, LHS, RHS);

  ExprResult LHSResult = LHS, RHSResult = RHS;
  QualType compType = UsualArithmeticConversions(
      LHSResult, RHSResult, Loc, IsCompAssign ? ACK_CompAssign : ACK_BitwiseOp);
  if (LHSResult.isInvalid() || RHSResult.isInvalid())
    return QualType();
  LHS = LHSResult.get();
  RHS = RHSResult.get();

  if (Opc == BO_Xor)
    diagnoseXorMisusedAsPow(*this, LHS, RHS, Loc);

  if (!compType.isNull() && compType->isIntegralOrUnscopedEnumerationType())
    return compType;
  return InvalidOperands(Loc, LHS, RHS);
}

// C99 6.5.[13,14]
inline QualType Sema::CheckLogicalOperands(ExprResult &LHS, ExprResult &RHS,
                                           SourceLocation Loc,
                                           BinaryOperatorKind Opc) {
  // Check vector operands differently.
  if (LHS.get()->getType()->isVectorType() ||
      RHS.get()->getType()->isVectorType())
    return CheckVectorLogicalOperands(LHS, RHS, Loc);

  bool EnumConstantInBoolContext = false;
  for (const ExprResult &HS : {LHS, RHS}) {
    if (const auto *DREHS = dyn_cast<DeclRefExpr>(HS.get())) {
      const auto *ECDHS = dyn_cast<EnumConstantDecl>(DREHS->getDecl());
      if (ECDHS && ECDHS->getInitVal() != 0 && ECDHS->getInitVal() != 1)
        EnumConstantInBoolContext = true;
    }
  }

  if (EnumConstantInBoolContext)
    Diag(Loc, diag::warn_enum_constant_in_bool_context);

  // WebAssembly tables can't be used with logical operators.
  QualType LHSTy = LHS.get()->getType();
  QualType RHSTy = RHS.get()->getType();
  const auto *LHSATy = dyn_cast<ArrayType>(LHSTy);
  const auto *RHSATy = dyn_cast<ArrayType>(RHSTy);
  if ((LHSATy && LHSATy->getElementType().isWebAssemblyReferenceType()) ||
      (RHSATy && RHSATy->getElementType().isWebAssemblyReferenceType())) {
    return InvalidOperands(Loc, LHS, RHS);
  }

  // Diagnose cases where the user write a logical and/or but probably meant a
  // bitwise one.  We do this when the LHS is a non-bool integer and the RHS
  // is a constant.
  if (!EnumConstantInBoolContext && LHS.get()->getType()->isIntegerType() &&
      !LHS.get()->getType()->isBooleanType() &&
      RHS.get()->getType()->isIntegerType() && !RHS.get()->isValueDependent() &&
      // Don't warn in macros or template instantiations.
      !Loc.isMacroID() && !inTemplateInstantiation()) {
    // If the RHS can be constant folded, and if it constant folds to something
    // that isn't 0 or 1 (which indicate a potential logical operation that
    // happened to fold to true/false) then warn.
    // Parens on the RHS are ignored.
    Expr::EvalResult EVResult;
    if (RHS.get()->EvaluateAsInt(EVResult, Context)) {
      llvm::APSInt Result = EVResult.Val.getInt();
      if ((getLangOpts().CPlusPlus && !RHS.get()->getType()->isBooleanType() &&
           !RHS.get()->getExprLoc().isMacroID()) ||
          (Result != 0 && Result != 1)) {
        Diag(Loc, diag::warn_logical_instead_of_bitwise)
            << RHS.get()->getSourceRange() << (Opc == BO_LAnd ? "&&" : "||");
        // Suggest replacing the logical operator with the bitwise version
        Diag(Loc, diag::note_logical_instead_of_bitwise_change_operator)
            << (Opc == BO_LAnd ? "&" : "|")
            << FixItHint::CreateReplacement(
                   SourceRange(Loc, getLocForEndOfToken(Loc)),
                   Opc == BO_LAnd ? "&" : "|");
        if (Opc == BO_LAnd)
          // Suggest replacing "Foo() && kNonZero" with "Foo()"
          Diag(Loc, diag::note_logical_instead_of_bitwise_remove_constant)
              << FixItHint::CreateRemoval(
                     SourceRange(getLocForEndOfToken(LHS.get()->getEndLoc()),
                                 RHS.get()->getEndLoc()));
      }
    }
  }

  if (!Context.getLangOpts().CPlusPlus) {
    // OpenCL v1.1 s6.3.g: The logical operators and (&&), or (||) do
    // not operate on the built-in scalar and vector float types.
    if (Context.getLangOpts().OpenCL &&
        Context.getLangOpts().OpenCLVersion < 120) {
      if (LHS.get()->getType()->isFloatingType() ||
          RHS.get()->getType()->isFloatingType())
        return InvalidOperands(Loc, LHS, RHS);
    }

    LHS = UsualUnaryConversions(LHS.get());
    if (LHS.isInvalid())
      return QualType();

    RHS = UsualUnaryConversions(RHS.get());
    if (RHS.isInvalid())
      return QualType();

    if (!LHS.get()->getType()->isScalarType() ||
        !RHS.get()->getType()->isScalarType())
      return InvalidOperands(Loc, LHS, RHS);

    return Context.IntTy;
  }

  // The following is safe because we only use this method for
  // non-overloadable operands.

  // C++ [expr.log.and]p1
  // C++ [expr.log.or]p1
  // The operands are both contextually converted to type bool.
  ExprResult LHSRes = PerformContextuallyConvertToBool(LHS.get());
  if (LHSRes.isInvalid())
    return InvalidOperands(Loc, LHS, RHS);
  LHS = LHSRes;

  ExprResult RHSRes = PerformContextuallyConvertToBool(RHS.get());
  if (RHSRes.isInvalid())
    return InvalidOperands(Loc, LHS, RHS);
  RHS = RHSRes;

  // C++ [expr.log.and]p2
  // C++ [expr.log.or]p2
  // The result is a bool.
  return Context.BoolTy;
}

static bool IsReadonlyMessage(Expr *E, Sema &S) {
  const MemberExpr *ME = dyn_cast<MemberExpr>(E);
  if (!ME) return false;
  if (!isa<FieldDecl>(ME->getMemberDecl())) return false;
  ObjCMessageExpr *Base = dyn_cast<ObjCMessageExpr>(
      ME->getBase()->IgnoreImplicit()->IgnoreParenImpCasts());
  if (!Base) return false;
  return Base->getMethodDecl() != nullptr;
}

/// Is the given expression (which must be 'const') a reference to a
/// variable which was originally non-const, but which has become
/// 'const' due to being captured within a block?
enum NonConstCaptureKind { NCCK_None, NCCK_Block, NCCK_Lambda };
static NonConstCaptureKind isReferenceToNonConstCapture(Sema &S, Expr *E) {
  assert(E->isLValue() && E->getType().isConstQualified());
  E = E->IgnoreParens();

  // Must be a reference to a declaration from an enclosing scope.
  DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(E);
  if (!DRE) return NCCK_None;
  if (!DRE->refersToEnclosingVariableOrCapture()) return NCCK_None;

  // The declaration must be a variable which is not declared 'const'.
  VarDecl *var = dyn_cast<VarDecl>(DRE->getDecl());
  if (!var) return NCCK_None;
  if (var->getType().isConstQualified()) return NCCK_None;
  assert(var->hasLocalStorage() && "capture added 'const' to non-local?");

  // Decide whether the first capture was for a block or a lambda.
  DeclContext *DC = S.CurContext, *Prev = nullptr;
  // Decide whether the first capture was for a block or a lambda.
  while (DC) {
    // For init-capture, it is possible that the variable belongs to the
    // template pattern of the current context.
    if (auto *FD = dyn_cast<FunctionDecl>(DC))
      if (var->isInitCapture() &&
          FD->getTemplateInstantiationPattern() == var->getDeclContext())
        break;
    if (DC == var->getDeclContext())
      break;
    Prev = DC;
    DC = DC->getParent();
  }
  // Unless we have an init-capture, we've gone one step too far.
  if (!var->isInitCapture())
    DC = Prev;
  return (isa<BlockDecl>(DC) ? NCCK_Block : NCCK_Lambda);
}

static bool IsTypeModifiable(QualType Ty, bool IsDereference) {
  Ty = Ty.getNonReferenceType();
  if (IsDereference && Ty->isPointerType())
    Ty = Ty->getPointeeType();
  return !Ty.isConstQualified();
}

// Update err_typecheck_assign_const and note_typecheck_assign_const
// when this enum is changed.
enum {
  ConstFunction,
  ConstVariable,
  ConstMember,
  ConstMethod,
  NestedConstMember,
  ConstUnknown,  // Keep as last element
};

/// Emit the "read-only variable not assignable" error and print notes to give
/// more information about why the variable is not assignable, such as pointing
/// to the declaration of a const variable, showing that a method is const, or
/// that the function is returning a const reference.
static void DiagnoseConstAssignment(Sema &S, const Expr *E,
                                    SourceLocation Loc) {
  SourceRange ExprRange = E->getSourceRange();

  // Only emit one error on the first const found.  All other consts will emit
  // a note to the error.
  bool DiagnosticEmitted = false;

  // Track if the current expression is the result of a dereference, and if the
  // next checked expression is the result of a dereference.
  bool IsDereference = false;
  bool NextIsDereference = false;

  // Loop to process MemberExpr chains.
  while (true) {
    IsDereference = NextIsDereference;

    E = E->IgnoreImplicit()->IgnoreParenImpCasts();
    if (const MemberExpr *ME = dyn_cast<MemberExpr>(E)) {
      NextIsDereference = ME->isArrow();
      const ValueDecl *VD = ME->getMemberDecl();
      if (const FieldDecl *Field = dyn_cast<FieldDecl>(VD)) {
        // Mutable fields can be modified even if the class is const.
        if (Field->isMutable()) {
          assert(DiagnosticEmitted && "Expected diagnostic not emitted.");
          break;
        }

        if (!IsTypeModifiable(Field->getType(), IsDereference)) {
          if (!DiagnosticEmitted) {
            S.Diag(Loc, diag::err_typecheck_assign_const)
                << ExprRange << ConstMember << false /*static*/ << Field
                << Field->getType();
            DiagnosticEmitted = true;
          }
          S.Diag(VD->getLocation(), diag::note_typecheck_assign_const)
              << ConstMember << false /*static*/ << Field << Field->getType()
              << Field->getSourceRange();
        }
        E = ME->getBase();
        continue;
      } else if (const VarDecl *VDecl = dyn_cast<VarDecl>(VD)) {
        if (VDecl->getType().isConstQualified()) {
          if (!DiagnosticEmitted) {
            S.Diag(Loc, diag::err_typecheck_assign_const)
                << ExprRange << ConstMember << true /*static*/ << VDecl
                << VDecl->getType();
            DiagnosticEmitted = true;
          }
          S.Diag(VD->getLocation(), diag::note_typecheck_assign_const)
              << ConstMember << true /*static*/ << VDecl << VDecl->getType()
              << VDecl->getSourceRange();
        }
        // Static fields do not inherit constness from parents.
        break;
      }
      break; // End MemberExpr
    } else if (const ArraySubscriptExpr *ASE =
                   dyn_cast<ArraySubscriptExpr>(E)) {
      E = ASE->getBase()->IgnoreParenImpCasts();
      continue;
    } else if (const ExtVectorElementExpr *EVE =
                   dyn_cast<ExtVectorElementExpr>(E)) {
      E = EVE->getBase()->IgnoreParenImpCasts();
      continue;
    }
    break;
  }

  if (const CallExpr *CE = dyn_cast<CallExpr>(E)) {
    // Function calls
    const FunctionDecl *FD = CE->getDirectCallee();
    if (FD && !IsTypeModifiable(FD->getReturnType(), IsDereference)) {
      if (!DiagnosticEmitted) {
        S.Diag(Loc, diag::err_typecheck_assign_const) << ExprRange
                                                      << ConstFunction << FD;
        DiagnosticEmitted = true;
      }
      S.Diag(FD->getReturnTypeSourceRange().getBegin(),
             diag::note_typecheck_assign_const)
          << ConstFunction << FD << FD->getReturnType()
          << FD->getReturnTypeSourceRange();
    }
  } else if (const DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(E)) {
    // Point to variable declaration.
    if (const ValueDecl *VD = DRE->getDecl()) {
      if (!IsTypeModifiable(VD->getType(), IsDereference)) {
        if (!DiagnosticEmitted) {
          S.Diag(Loc, diag::err_typecheck_assign_const)
              << ExprRange << ConstVariable << VD << VD->getType();
          DiagnosticEmitted = true;
        }
        S.Diag(VD->getLocation(), diag::note_typecheck_assign_const)
            << ConstVariable << VD << VD->getType() << VD->getSourceRange();
      }
    }
  } else if (isa<CXXThisExpr>(E)) {
    if (const DeclContext *DC = S.getFunctionLevelDeclContext()) {
      if (const CXXMethodDecl *MD = dyn_cast<CXXMethodDecl>(DC)) {
        if (MD->isConst()) {
          if (!DiagnosticEmitted) {
            S.Diag(Loc, diag::err_typecheck_assign_const) << ExprRange
                                                          << ConstMethod << MD;
            DiagnosticEmitted = true;
          }
          S.Diag(MD->getLocation(), diag::note_typecheck_assign_const)
              << ConstMethod << MD << MD->getSourceRange();
        }
      }
    }
  }

  if (DiagnosticEmitted)
    return;

  // Can't determine a more specific message, so display the generic error.
  S.Diag(Loc, diag::err_typecheck_assign_const) << ExprRange << ConstUnknown;
}

enum OriginalExprKind {
  OEK_Variable,
  OEK_Member,
  OEK_LValue
};

static void DiagnoseRecursiveConstFields(Sema &S, const ValueDecl *VD,
                                         const RecordType *Ty,
                                         SourceLocation Loc, SourceRange Range,
                                         OriginalExprKind OEK,
                                         bool &DiagnosticEmitted) {
  std::vector<const RecordType *> RecordTypeList;
  RecordTypeList.push_back(Ty);
  unsigned NextToCheckIndex = 0;
  // We walk the record hierarchy breadth-first to ensure that we print
  // diagnostics in field nesting order.
  while (RecordTypeList.size() > NextToCheckIndex) {
    bool IsNested = NextToCheckIndex > 0;
    for (const FieldDecl *Field :
         RecordTypeList[NextToCheckIndex]->getDecl()->fields()) {
      // First, check every field for constness.
      QualType FieldTy = Field->getType();
      if (FieldTy.isConstQualified()) {
        if (!DiagnosticEmitted) {
          S.Diag(Loc, diag::err_typecheck_assign_const)
              << Range << NestedConstMember << OEK << VD
              << IsNested << Field;
          DiagnosticEmitted = true;
        }
        S.Diag(Field->getLocation(), diag::note_typecheck_assign_const)
            << NestedConstMember << IsNested << Field
            << FieldTy << Field->getSourceRange();
      }

      // Then we append it to the list to check next in order.
      FieldTy = FieldTy.getCanonicalType();
      if (const auto *FieldRecTy = FieldTy->getAs<RecordType>()) {
        if (!llvm::is_contained(RecordTypeList, FieldRecTy))
          RecordTypeList.push_back(FieldRecTy);
      }
    }
    ++NextToCheckIndex;
  }
}

/// Emit an error for the case where a record we are trying to assign to has a
/// const-qualified field somewhere in its hierarchy.
static void DiagnoseRecursiveConstFields(Sema &S, const Expr *E,
                                         SourceLocation Loc) {
  QualType Ty = E->getType();
  assert(Ty->isRecordType() && "lvalue was not record?");
  SourceRange Range = E->getSourceRange();
  const RecordType *RTy = Ty.getCanonicalType()->getAs<RecordType>();
  bool DiagEmitted = false;

  if (const MemberExpr *ME = dyn_cast<MemberExpr>(E))
    DiagnoseRecursiveConstFields(S, ME->getMemberDecl(), RTy, Loc,
            Range, OEK_Member, DiagEmitted);
  else if (const DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(E))
    DiagnoseRecursiveConstFields(S, DRE->getDecl(), RTy, Loc,
            Range, OEK_Variable, DiagEmitted);
  else
    DiagnoseRecursiveConstFields(S, nullptr, RTy, Loc,
            Range, OEK_LValue, DiagEmitted);
  if (!DiagEmitted)
    DiagnoseConstAssignment(S, E, Loc);
}

/// CheckForModifiableLvalue - Verify that E is a modifiable lvalue.  If not,
/// emit an error and return true.  If so, return false.
static bool CheckForModifiableLvalue(Expr *E, SourceLocation Loc, Sema &S) {
  assert(!E->hasPlaceholderType(BuiltinType::PseudoObject));

  S.CheckShadowingDeclModification(E, Loc);

  SourceLocation OrigLoc = Loc;
  Expr::isModifiableLvalueResult IsLV = E->isModifiableLvalue(S.Context,
                                                              &Loc);
  if (IsLV == Expr::MLV_ClassTemporary && IsReadonlyMessage(E, S))
    IsLV = Expr::MLV_InvalidMessageExpression;
  if (IsLV == Expr::MLV_Valid)
    return false;

  unsigned DiagID = 0;
  bool NeedType = false;
  switch (IsLV) { // C99 6.5.16p2
  case Expr::MLV_ConstQualified:
    // Use a specialized diagnostic when we're assigning to an object
    // from an enclosing function or block.
    if (NonConstCaptureKind NCCK = isReferenceToNonConstCapture(S, E)) {
      if (NCCK == NCCK_Block)
        DiagID = diag::err_block_decl_ref_not_modifiable_lvalue;
      else
        DiagID = diag::err_lambda_decl_ref_not_modifiable_lvalue;
      break;
    }

    // In ARC, use some specialized diagnostics for occasions where we
    // infer 'const'.  These are always pseudo-strong variables.
    if (S.getLangOpts().ObjCAutoRefCount) {
      DeclRefExpr *declRef = dyn_cast<DeclRefExpr>(E->IgnoreParenCasts());
      if (declRef && isa<VarDecl>(declRef->getDecl())) {
        VarDecl *var = cast<VarDecl>(declRef->getDecl());

        // Use the normal diagnostic if it's pseudo-__strong but the
        // user actually wrote 'const'.
        if (var->isARCPseudoStrong() &&
            (!var->getTypeSourceInfo() ||
             !var->getTypeSourceInfo()->getType().isConstQualified())) {
          // There are three pseudo-strong cases:
          //  - self
          ObjCMethodDecl *method = S.getCurMethodDecl();
          if (method && var == method->getSelfDecl()) {
            DiagID = method->isClassMethod()
              ? diag::err_typecheck_arc_assign_self_class_method
              : diag::err_typecheck_arc_assign_self;

          //  - Objective-C externally_retained attribute.
          } else if (var->hasAttr<ObjCExternallyRetainedAttr>() ||
                     isa<ParmVarDecl>(var)) {
            DiagID = diag::err_typecheck_arc_assign_externally_retained;

          //  - fast enumeration variables
          } else {
            DiagID = diag::err_typecheck_arr_assign_enumeration;
          }

          SourceRange Assign;
          if (Loc != OrigLoc)
            Assign = SourceRange(OrigLoc, OrigLoc);
          S.Diag(Loc, DiagID) << E->getSourceRange() << Assign;
          // We need to preserve the AST regardless, so migration tool
          // can do its job.
          return false;
        }
      }
    }

    // If none of the special cases above are triggered, then this is a
    // simple const assignment.
    if (DiagID == 0) {
      DiagnoseConstAssignment(S, E, Loc);
      return true;
    }

    break;
  case Expr::MLV_ConstAddrSpace:
    DiagnoseConstAssignment(S, E, Loc);
    return true;
  case Expr::MLV_ConstQualifiedField:
    DiagnoseRecursiveConstFields(S, E, Loc);
    return true;
  case Expr::MLV_ArrayType:
  case Expr::MLV_ArrayTemporary:
    DiagID = diag::err_typecheck_array_not_modifiable_lvalue;
    NeedType = true;
    break;
  case Expr::MLV_NotObjectType:
    DiagID = diag::err_typecheck_non_object_not_modifiable_lvalue;
    NeedType = true;
    break;
  case Expr::MLV_LValueCast:
    DiagID = diag::err_typecheck_lvalue_casts_not_supported;
    break;
  case Expr::MLV_Valid:
    llvm_unreachable("did not take early return for MLV_Valid");
  case Expr::MLV_InvalidExpression:
  case Expr::MLV_MemberFunction:
  case Expr::MLV_ClassTemporary:
    DiagID = diag::err_typecheck_expression_not_modifiable_lvalue;
    break;
  case Expr::MLV_IncompleteType:
  case Expr::MLV_IncompleteVoidType:
    return S.RequireCompleteType(Loc, E->getType(),
             diag::err_typecheck_incomplete_type_not_modifiable_lvalue, E);
  case Expr::MLV_DuplicateVectorComponents:
    DiagID = diag::err_typecheck_duplicate_vector_components_not_mlvalue;
    break;
  case Expr::MLV_NoSetterProperty:
    llvm_unreachable("readonly properties should be processed differently");
  case Expr::MLV_InvalidMessageExpression:
    DiagID = diag::err_readonly_message_assignment;
    break;
  case Expr::MLV_SubObjCPropertySetting:
    DiagID = diag::err_no_subobject_property_setting;
    break;
  }

  SourceRange Assign;
  if (Loc != OrigLoc)
    Assign = SourceRange(OrigLoc, OrigLoc);
  if (NeedType)
    S.Diag(Loc, DiagID) << E->getType() << E->getSourceRange() << Assign;
  else
    S.Diag(Loc, DiagID) << E->getSourceRange() << Assign;
  return true;
}

static void CheckIdentityFieldAssignment(Expr *LHSExpr, Expr *RHSExpr,
                                         SourceLocation Loc,
                                         Sema &Sema) {
  if (Sema.inTemplateInstantiation())
    return;
  if (Sema.isUnevaluatedContext())
    return;
  if (Loc.isInvalid() || Loc.isMacroID())
    return;
  if (LHSExpr->getExprLoc().isMacroID() || RHSExpr->getExprLoc().isMacroID())
    return;

  // C / C++ fields
  MemberExpr *ML = dyn_cast<MemberExpr>(LHSExpr);
  MemberExpr *MR = dyn_cast<MemberExpr>(RHSExpr);
  if (ML && MR) {
    if (!(isa<CXXThisExpr>(ML->getBase()) && isa<CXXThisExpr>(MR->getBase())))
      return;
    const ValueDecl *LHSDecl =
        cast<ValueDecl>(ML->getMemberDecl()->getCanonicalDecl());
    const ValueDecl *RHSDecl =
        cast<ValueDecl>(MR->getMemberDecl()->getCanonicalDecl());
    if (LHSDecl != RHSDecl)
      return;
    if (LHSDecl->getType().isVolatileQualified())
      return;
    if (const ReferenceType *RefTy = LHSDecl->getType()->getAs<ReferenceType>())
      if (RefTy->getPointeeType().isVolatileQualified())
        return;

    Sema.Diag(Loc, diag::warn_identity_field_assign) << 0;
  }

  // Objective-C instance variables
  ObjCIvarRefExpr *OL = dyn_cast<ObjCIvarRefExpr>(LHSExpr);
  ObjCIvarRefExpr *OR = dyn_cast<ObjCIvarRefExpr>(RHSExpr);
  if (OL && OR && OL->getDecl() == OR->getDecl()) {
    DeclRefExpr *RL = dyn_cast<DeclRefExpr>(OL->getBase()->IgnoreImpCasts());
    DeclRefExpr *RR = dyn_cast<DeclRefExpr>(OR->getBase()->IgnoreImpCasts());
    if (RL && RR && RL->getDecl() == RR->getDecl())
      Sema.Diag(Loc, diag::warn_identity_field_assign) << 1;
  }
}

// C99 6.5.16.1
QualType Sema::CheckAssignmentOperands(Expr *LHSExpr, ExprResult &RHS,
                                       SourceLocation Loc,
                                       QualType CompoundType,
                                       BinaryOperatorKind Opc) {
  assert(!LHSExpr->hasPlaceholderType(BuiltinType::PseudoObject));

  // Verify that LHS is a modifiable lvalue, and emit error if not.
  if (CheckForModifiableLvalue(LHSExpr, Loc, *this))
    return QualType();

  QualType LHSType = LHSExpr->getType();
  QualType RHSType = CompoundType.isNull() ? RHS.get()->getType() :
                                             CompoundType;
  // OpenCL v1.2 s6.1.1.1 p2:
  // The half data type can only be used to declare a pointer to a buffer that
  // contains half values
  if (getLangOpts().OpenCL &&
      !getOpenCLOptions().isAvailableOption("cl_khr_fp16", getLangOpts()) &&
      LHSType->isHalfType()) {
    Diag(Loc, diag::err_opencl_half_load_store) << 1
        << LHSType.getUnqualifiedType();
    return QualType();
  }

  // WebAssembly tables can't be used on RHS of an assignment expression.
  if (RHSType->isWebAssemblyTableType()) {
    Diag(Loc, diag::err_wasm_table_art) << 0;
    return QualType();
  }

  AssignConvertType ConvTy;
  if (CompoundType.isNull()) {
    Expr *RHSCheck = RHS.get();

    CheckIdentityFieldAssignment(LHSExpr, RHSCheck, Loc, *this);

    QualType LHSTy(LHSType);
    ConvTy = CheckSingleAssignmentConstraints(LHSTy, RHS);
    if (RHS.isInvalid())
      return QualType();
    // Special case of NSObject attributes on c-style pointer types.
    if (ConvTy == IncompatiblePointer &&
        ((Context.isObjCNSObjectType(LHSType) &&
          RHSType->isObjCObjectPointerType()) ||
         (Context.isObjCNSObjectType(RHSType) &&
          LHSType->isObjCObjectPointerType())))
      ConvTy = Compatible;

    if (ConvTy == Compatible &&
        LHSType->isObjCObjectType())
        Diag(Loc, diag::err_objc_object_assignment)
          << LHSType;

    // If the RHS is a unary plus or minus, check to see if they = and + are
    // right next to each other.  If so, the user may have typo'd "x =+ 4"
    // instead of "x += 4".
    if (ImplicitCastExpr *ICE = dyn_cast<ImplicitCastExpr>(RHSCheck))
      RHSCheck = ICE->getSubExpr();
    if (UnaryOperator *UO = dyn_cast<UnaryOperator>(RHSCheck)) {
      if ((UO->getOpcode() == UO_Plus || UO->getOpcode() == UO_Minus) &&
          Loc.isFileID() && UO->getOperatorLoc().isFileID() &&
          // Only if the two operators are exactly adjacent.
          Loc.getLocWithOffset(1) == UO->getOperatorLoc() &&
          // And there is a space or other character before the subexpr of the
          // unary +/-.  We don't want to warn on "x=-1".
          Loc.getLocWithOffset(2) != UO->getSubExpr()->getBeginLoc() &&
          UO->getSubExpr()->getBeginLoc().isFileID()) {
        Diag(Loc, diag::warn_not_compound_assign)
          << (UO->getOpcode() == UO_Plus ? "+" : "-")
          << SourceRange(UO->getOperatorLoc(), UO->getOperatorLoc());
      }
    }

    if (ConvTy == Compatible) {
      if (LHSType.getObjCLifetime() == Qualifiers::OCL_Strong) {
        // Warn about retain cycles where a block captures the LHS, but
        // not if the LHS is a simple variable into which the block is
        // being stored...unless that variable can be captured by reference!
        const Expr *InnerLHS = LHSExpr->IgnoreParenCasts();
        const DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(InnerLHS);
        if (!DRE || DRE->getDecl()->hasAttr<BlocksAttr>())
          ObjC().checkRetainCycles(LHSExpr, RHS.get());
      }

      if (LHSType.getObjCLifetime() == Qualifiers::OCL_Strong ||
          LHSType.isNonWeakInMRRWithObjCWeak(Context)) {
        // It is safe to assign a weak reference into a strong variable.
        // Although this code can still have problems:
        //   id x = self.weakProp;
        //   id y = self.weakProp;
        // we do not warn to warn spuriously when 'x' and 'y' are on separate
        // paths through the function. This should be revisited if
        // -Wrepeated-use-of-weak is made flow-sensitive.
        // For ObjCWeak only, we do not warn if the assign is to a non-weak
        // variable, which will be valid for the current autorelease scope.
        if (!Diags.isIgnored(diag::warn_arc_repeated_use_of_weak,
                             RHS.get()->getBeginLoc()))
          getCurFunction()->markSafeWeakUse(RHS.get());

      } else if (getLangOpts().ObjCAutoRefCount || getLangOpts().ObjCWeak) {
        checkUnsafeExprAssigns(Loc, LHSExpr, RHS.get());
      }
    }
  } else {
    // Compound assignment "x += y"
    ConvTy = CheckAssignmentConstraints(Loc, LHSType, RHSType);
  }

  if (DiagnoseAssignmentResult(ConvTy, Loc, LHSType, RHSType,
                               RHS.get(), AA_Assigning))
    return QualType();

  CheckForNullPointerDereference(*this, LHSExpr);

  AssignedEntity AE{LHSExpr};
  checkExprLifetime(*this, AE, RHS.get());

  if (getLangOpts().CPlusPlus20 && LHSType.isVolatileQualified()) {
    if (CompoundType.isNull()) {
      // C++2a [expr.ass]p5:
      //   A simple-assignment whose left operand is of a volatile-qualified
      //   type is deprecated unless the assignment is either a discarded-value
      //   expression or an unevaluated operand
      ExprEvalContexts.back().VolatileAssignmentLHSs.push_back(LHSExpr);
    }
  }

  // C11 6.5.16p3: The type of an assignment expression is the type of the
  // left operand would have after lvalue conversion.
  // C11 6.3.2.1p2: ...this is called lvalue conversion. If the lvalue has
  // qualified type, the value has the unqualified version of the type of the
  // lvalue; additionally, if the lvalue has atomic type, the value has the
  // non-atomic version of the type of the lvalue.
  // C++ 5.17p1: the type of the assignment expression is that of its left
  // operand.
  return getLangOpts().CPlusPlus ? LHSType : LHSType.getAtomicUnqualifiedType();
}

// Scenarios to ignore if expression E is:
// 1. an explicit cast expression into void
// 2. a function call expression that returns void
static bool IgnoreCommaOperand(const Expr *E, const ASTContext &Context) {
  E = E->IgnoreParens();

  if (const CastExpr *CE = dyn_cast<CastExpr>(E)) {
    if (CE->getCastKind() == CK_ToVoid) {
      return true;
    }

    // static_cast<void> on a dependent type will not show up as CK_ToVoid.
    if (CE->getCastKind() == CK_Dependent && E->getType()->isVoidType() &&
        CE->getSubExpr()->getType()->isDependentType()) {
      return true;
    }
  }

  if (const auto *CE = dyn_cast<CallExpr>(E))
    return CE->getCallReturnType(Context)->isVoidType();
  return false;
}

void Sema::DiagnoseCommaOperator(const Expr *LHS, SourceLocation Loc) {
  // No warnings in macros
  if (Loc.isMacroID())
    return;

  // Don't warn in template instantiations.
  if (inTemplateInstantiation())
    return;

  // Scope isn't fine-grained enough to explicitly list the specific cases, so
  // instead, skip more than needed, then call back into here with the
  // CommaVisitor in SemaStmt.cpp.
  // The listed locations are the initialization and increment portions
  // of a for loop.  The additional checks are on the condition of
  // if statements, do/while loops, and for loops.
  // Differences in scope flags for C89 mode requires the extra logic.
  const unsigned ForIncrementFlags =
      getLangOpts().C99 || getLangOpts().CPlusPlus
          ? Scope::ControlScope | Scope::ContinueScope | Scope::BreakScope
          : Scope::ContinueScope | Scope::BreakScope;
  const unsigned ForInitFlags = Scope::ControlScope | Scope::DeclScope;
  const unsigned ScopeFlags = getCurScope()->getFlags();
  if ((ScopeFlags & ForIncrementFlags) == ForIncrementFlags ||
      (ScopeFlags & ForInitFlags) == ForInitFlags)
    return;

  // If there are multiple comma operators used together, get the RHS of the
  // of the comma operator as the LHS.
  while (const BinaryOperator *BO = dyn_cast<BinaryOperator>(LHS)) {
    if (BO->getOpcode() != BO_Comma)
      break;
    LHS = BO->getRHS();
  }

  // Only allow some expressions on LHS to not warn.
  if (IgnoreCommaOperand(LHS, Context))
    return;

  Diag(Loc, diag::warn_comma_operator);
  Diag(LHS->getBeginLoc(), diag::note_cast_to_void)
      << LHS->getSourceRange()
      << FixItHint::CreateInsertion(LHS->getBeginLoc(),
                                    LangOpts.CPlusPlus ? "static_cast<void>("
                                                       : "(void)(")
      << FixItHint::CreateInsertion(PP.getLocForEndOfToken(LHS->getEndLoc()),
                                    ")");
}

// C99 6.5.17
static QualType CheckCommaOperands(Sema &S, ExprResult &LHS, ExprResult &RHS,
                                   SourceLocation Loc) {
  LHS = S.CheckPlaceholderExpr(LHS.get());
  RHS = S.CheckPlaceholderExpr(RHS.get());
  if (LHS.isInvalid() || RHS.isInvalid())
    return QualType();

  // C's comma performs lvalue conversion (C99 6.3.2.1) on both its
  // operands, but not unary promotions.
  // C++'s comma does not do any conversions at all (C++ [expr.comma]p1).

  // So we treat the LHS as a ignored value, and in C++ we allow the
  // containing site to determine what should be done with the RHS.
  LHS = S.IgnoredValueConversions(LHS.get());
  if (LHS.isInvalid())
    return QualType();

  S.DiagnoseUnusedExprResult(LHS.get(), diag::warn_unused_comma_left_operand);

  if (!S.getLangOpts().CPlusPlus) {
    RHS = S.DefaultFunctionArrayLvalueConversion(RHS.get());
    if (RHS.isInvalid())
      return QualType();
    if (!RHS.get()->getType()->isVoidType())
      S.RequireCompleteType(Loc, RHS.get()->getType(),
                            diag::err_incomplete_type);
  }

  if (!S.getDiagnostics().isIgnored(diag::warn_comma_operator, Loc))
    S.DiagnoseCommaOperator(LHS.get(), Loc);

  return RHS.get()->getType();
}

/// CheckIncrementDecrementOperand - unlike most "Check" methods, this routine
/// doesn't need to call UsualUnaryConversions or UsualArithmeticConversions.
static QualType CheckIncrementDecrementOperand(Sema &S, Expr *Op,
                                               ExprValueKind &VK,
                                               ExprObjectKind &OK,
                                               SourceLocation OpLoc, bool IsInc,
                                               bool IsPrefix) {
  QualType ResType = Op->getType();
  // Atomic types can be used for increment / decrement where the non-atomic
  // versions can, so ignore the _Atomic() specifier for the purpose of
  // checking.
  if (const AtomicType *ResAtomicType = ResType->getAs<AtomicType>())
    ResType = ResAtomicType->getValueType();

  assert(!ResType.isNull() && "no type for increment/decrement expression");

  if (S.getLangOpts().CPlusPlus && ResType->isBooleanType()) {
    // Decrement of bool is not allowed.
    if (!IsInc) {
      S.Diag(OpLoc, diag::err_decrement_bool) << Op->getSourceRange();
      return QualType();
    }
    // Increment of bool sets it to true, but is deprecated.
    S.Diag(OpLoc, S.getLangOpts().CPlusPlus17 ? diag::ext_increment_bool
                                              : diag::warn_increment_bool)
      << Op->getSourceRange();
  } else if (S.getLangOpts().CPlusPlus && ResType->isEnumeralType()) {
    // Error on enum increments and decrements in C++ mode
    S.Diag(OpLoc, diag::err_increment_decrement_enum) << IsInc << ResType;
    return QualType();
  } else if (ResType->isRealType()) {
    // OK!
  } else if (ResType->isPointerType()) {
    // C99 6.5.2.4p2, 6.5.6p2
    if (!checkArithmeticOpPointerOperand(S, OpLoc, Op))
      return QualType();
  } else if (ResType->isObjCObjectPointerType()) {
    // On modern runtimes, ObjC pointer arithmetic is forbidden.
    // Otherwise, we just need a complete type.
    if (checkArithmeticIncompletePointerType(S, OpLoc, Op) ||
        checkArithmeticOnObjCPointer(S, OpLoc, Op))
      return QualType();
  } else if (ResType->isAnyComplexType()) {
    // C99 does not support ++/-- on complex types, we allow as an extension.
    S.Diag(OpLoc, S.getLangOpts().C2y ? diag::warn_c2y_compat_increment_complex
                                      : diag::ext_c2y_increment_complex)
        << IsInc << Op->getSourceRange();
  } else if (ResType->isPlaceholderType()) {
    ExprResult PR = S.CheckPlaceholderExpr(Op);
    if (PR.isInvalid()) return QualType();
    return CheckIncrementDecrementOperand(S, PR.get(), VK, OK, OpLoc,
                                          IsInc, IsPrefix);
  } else if (S.getLangOpts().AltiVec && ResType->isVectorType()) {
    // OK! ( C/C++ Language Extensions for CBEA(Version 2.6) 10.3 )
  } else if (S.getLangOpts().ZVector && ResType->isVectorType() &&
             (ResType->castAs<VectorType>()->getVectorKind() !=
              VectorKind::AltiVecBool)) {
    // The z vector extensions allow ++ and -- for non-bool vectors.
  } else if (S.getLangOpts().OpenCL && ResType->isVectorType() &&
             ResType->castAs<VectorType>()->getElementType()->isIntegerType()) {
    // OpenCL V1.2 6.3 says dec/inc ops operate on integer vector types.
  } else {
    S.Diag(OpLoc, diag::err_typecheck_illegal_increment_decrement)
      << ResType << int(IsInc) << Op->getSourceRange();
    return QualType();
  }
  // At this point, we know we have a real, complex or pointer type.
  // Now make sure the operand is a modifiable lvalue.
  if (CheckForModifiableLvalue(Op, OpLoc, S))
    return QualType();
  if (S.getLangOpts().CPlusPlus20 && ResType.isVolatileQualified()) {
    // C++2a [expr.pre.inc]p1, [expr.post.inc]p1:
    //   An operand with volatile-qualified type is deprecated
    S.Diag(OpLoc, diag::warn_deprecated_increment_decrement_volatile)
        << IsInc << ResType;
  }
  // In C++, a prefix increment is the same type as the operand. Otherwise
  // (in C or with postfix), the increment is the unqualified type of the
  // operand.
  if (IsPrefix && S.getLangOpts().CPlusPlus) {
    VK = VK_LValue;
    OK = Op->getObjectKind();
    return ResType;
  } else {
    VK = VK_PRValue;
    return ResType.getUnqualifiedType();
  }
}

/// getPrimaryDecl - Helper function for CheckAddressOfOperand().
/// This routine allows us to typecheck complex/recursive expressions
/// where the declaration is needed for type checking. We only need to
/// handle cases when the expression references a function designator
/// or is an lvalue. Here are some examples:
///  - &(x) => x
///  - &*****f => f for f a function designator.
///  - &s.xx => s
///  - &s.zz[1].yy -> s, if zz is an array
///  - *(x + 1) -> x, if x is an array
///  - &"123"[2] -> 0
///  - & __real__ x -> x
///
/// FIXME: We don't recurse to the RHS of a comma, nor handle pointers to
/// members.
static ValueDecl *getPrimaryDecl(Expr *E) {
  switch (E->getStmtClass()) {
  case Stmt::DeclRefExprClass:
    return cast<DeclRefExpr>(E)->getDecl();
  case Stmt::MemberExprClass:
    // If this is an arrow operator, the address is an offset from
    // the base's value, so the object the base refers to is
    // irrelevant.
    if (cast<MemberExpr>(E)->isArrow())
      return nullptr;
    // Otherwise, the expression refers to a part of the base
    return getPrimaryDecl(cast<MemberExpr>(E)->getBase());
  case Stmt::ArraySubscriptExprClass: {
    // FIXME: This code shouldn't be necessary!  We should catch the implicit
    // promotion of register arrays earlier.
    Expr* Base = cast<ArraySubscriptExpr>(E)->getBase();
    if (ImplicitCastExpr* ICE = dyn_cast<ImplicitCastExpr>(Base)) {
      if (ICE->getSubExpr()->getType()->isArrayType())
        return getPrimaryDecl(ICE->getSubExpr());
    }
    return nullptr;
  }
  case Stmt::UnaryOperatorClass: {
    UnaryOperator *UO = cast<UnaryOperator>(E);

    switch(UO->getOpcode()) {
    case UO_Real:
    case UO_Imag:
    case UO_Extension:
      return getPrimaryDecl(UO->getSubExpr());
    default:
      return nullptr;
    }
  }
  case Stmt::ParenExprClass:
    return getPrimaryDecl(cast<ParenExpr>(E)->getSubExpr());
  case Stmt::ImplicitCastExprClass:
    // If the result of an implicit cast is an l-value, we care about
    // the sub-expression; otherwise, the result here doesn't matter.
    return getPrimaryDecl(cast<ImplicitCastExpr>(E)->getSubExpr());
  case Stmt::CXXUuidofExprClass:
    return cast<CXXUuidofExpr>(E)->getGuidDecl();
  default:
    return nullptr;
  }
}

namespace {
enum {
  AO_Bit_Field = 0,
  AO_Vector_Element = 1,
  AO_Property_Expansion = 2,
  AO_Register_Variable = 3,
  AO_Matrix_Element = 4,
  AO_No_Error = 5
};
}
/// Diagnose invalid operand for address of operations.
///
/// \param Type The type of operand which cannot have its address taken.
static void diagnoseAddressOfInvalidType(Sema &S, SourceLocation Loc,
                                         Expr *E, unsigned Type) {
  S.Diag(Loc, diag::err_typecheck_address_of) << Type << E->getSourceRange();
}

bool Sema::CheckUseOfCXXMethodAsAddressOfOperand(SourceLocation OpLoc,
                                                 const Expr *Op,
                                                 const CXXMethodDecl *MD) {
  const auto *DRE = cast<DeclRefExpr>(Op->IgnoreParens());

  if (Op != DRE)
    return Diag(OpLoc, diag::err_parens_pointer_member_function)
           << Op->getSourceRange();

  // Taking the address of a dtor is illegal per C++ [class.dtor]p2.
  if (isa<CXXDestructorDecl>(MD))
    return Diag(OpLoc, diag::err_typecheck_addrof_dtor)
           << DRE->getSourceRange();

  if (DRE->getQualifier())
    return false;

  if (MD->getParent()->getName().empty())
    return Diag(OpLoc, diag::err_unqualified_pointer_member_function)
           << DRE->getSourceRange();

  SmallString<32> Str;
  StringRef Qual = (MD->getParent()->getName() + "::").toStringRef(Str);
  return Diag(OpLoc, diag::err_unqualified_pointer_member_function)
         << DRE->getSourceRange()
         << FixItHint::CreateInsertion(DRE->getSourceRange().getBegin(), Qual);
}

QualType Sema::CheckAddressOfOperand(ExprResult &OrigOp, SourceLocation OpLoc) {
  if (const BuiltinType *PTy = OrigOp.get()->getType()->getAsPlaceholderType()){
    if (PTy->getKind() == BuiltinType::Overload) {
      Expr *E = OrigOp.get()->IgnoreParens();
      if (!isa<OverloadExpr>(E)) {
        assert(cast<UnaryOperator>(E)->getOpcode() == UO_AddrOf);
        Diag(OpLoc, diag::err_typecheck_invalid_lvalue_addrof_addrof_function)
          << OrigOp.get()->getSourceRange();
        return QualType();
      }

      OverloadExpr *Ovl = cast<OverloadExpr>(E);
      if (isa<UnresolvedMemberExpr>(Ovl))
        if (!ResolveSingleFunctionTemplateSpecialization(Ovl)) {
          Diag(OpLoc, diag::err_invalid_form_pointer_member_function)
            << OrigOp.get()->getSourceRange();
          return QualType();
        }

      return Context.OverloadTy;
    }

    if (PTy->getKind() == BuiltinType::UnknownAny)
      return Context.UnknownAnyTy;

    if (PTy->getKind() == BuiltinType::BoundMember) {
      Diag(OpLoc, diag::err_invalid_form_pointer_member_function)
        << OrigOp.get()->getSourceRange();
      return QualType();
    }

    OrigOp = CheckPlaceholderExpr(OrigOp.get());
    if (OrigOp.isInvalid()) return QualType();
  }

  if (OrigOp.get()->isTypeDependent())
    return Context.DependentTy;

  assert(!OrigOp.get()->hasPlaceholderType());

  // Make sure to ignore parentheses in subsequent checks
  Expr *op = OrigOp.get()->IgnoreParens();

  // In OpenCL captures for blocks called as lambda functions
  // are located in the private address space. Blocks used in
  // enqueue_kernel can be located in a different address space
  // depending on a vendor implementation. Thus preventing
  // taking an address of the capture to avoid invalid AS casts.
  if (LangOpts.OpenCL) {
    auto* VarRef = dyn_cast<DeclRefExpr>(op);
    if (VarRef && VarRef->refersToEnclosingVariableOrCapture()) {
      Diag(op->getExprLoc(), diag::err_opencl_taking_address_capture);
      return QualType();
    }
  }

  if (getLangOpts().C99) {
    // Implement C99-only parts of addressof rules.
    if (UnaryOperator* uOp = dyn_cast<UnaryOperator>(op)) {
      if (uOp->getOpcode() == UO_Deref)
        // Per C99 6.5.3.2, the address of a deref always returns a valid result
        // (assuming the deref expression is valid).
        return uOp->getSubExpr()->getType();
    }
    // Technically, there should be a check for array subscript
    // expressions here, but the result of one is always an lvalue anyway.
  }
  ValueDecl *dcl = getPrimaryDecl(op);

  if (auto *FD = dyn_cast_or_null<FunctionDecl>(dcl))
    if (!checkAddressOfFunctionIsAvailable(FD, /*Complain=*/true,
                                           op->getBeginLoc()))
      return QualType();

  Expr::LValueClassification lval = op->ClassifyLValue(Context);
  unsigned AddressOfError = AO_No_Error;

  if (lval == Expr::LV_ClassTemporary || lval == Expr::LV_ArrayTemporary) {
    bool sfinae = (bool)isSFINAEContext();
    Diag(OpLoc, isSFINAEContext() ? diag::err_typecheck_addrof_temporary
                                  : diag::ext_typecheck_addrof_temporary)
      << op->getType() << op->getSourceRange();
    if (sfinae)
      return QualType();
    // Materialize the temporary as an lvalue so that we can take its address.
    OrigOp = op =
        CreateMaterializeTemporaryExpr(op->getType(), OrigOp.get(), true);
  } else if (isa<ObjCSelectorExpr>(op)) {
    return Context.getPointerType(op->getType());
  } else if (lval == Expr::LV_MemberFunction) {
    // If it's an instance method, make a member pointer.
    // The expression must have exactly the form &A::foo.

    // If the underlying expression isn't a decl ref, give up.
    if (!isa<DeclRefExpr>(op)) {
      Diag(OpLoc, diag::err_invalid_form_pointer_member_function)
        << OrigOp.get()->getSourceRange();
      return QualType();
    }
    DeclRefExpr *DRE = cast<DeclRefExpr>(op);
    CXXMethodDecl *MD = cast<CXXMethodDecl>(DRE->getDecl());

    CheckUseOfCXXMethodAsAddressOfOperand(OpLoc, OrigOp.get(), MD);

    QualType MPTy = Context.getMemberPointerType(
        op->getType(), Context.getTypeDeclType(MD->getParent()).getTypePtr());

    if (getLangOpts().PointerAuthCalls && MD->isVirtual() &&
        !isUnevaluatedContext() && !MPTy->isDependentType()) {
      // When pointer authentication is enabled, argument and return types of
      // vitual member functions must be complete. This is because vitrual
      // member function pointers are implemented using virtual dispatch
      // thunks and the thunks cannot be emitted if the argument or return
      // types are incomplete.
      auto ReturnOrParamTypeIsIncomplete = [&](QualType T,
                                               SourceLocation DeclRefLoc,
                                               SourceLocation RetArgTypeLoc) {
        if (RequireCompleteType(DeclRefLoc, T, diag::err_incomplete_type)) {
          Diag(DeclRefLoc,
               diag::note_ptrauth_virtual_function_pointer_incomplete_arg_ret);
          Diag(RetArgTypeLoc,
               diag::note_ptrauth_virtual_function_incomplete_arg_ret_type)
              << T;
          return true;
        }
        return false;
      };
      QualType RetTy = MD->getReturnType();
      bool IsIncomplete =
          !RetTy->isVoidType() &&
          ReturnOrParamTypeIsIncomplete(
              RetTy, OpLoc, MD->getReturnTypeSourceRange().getBegin());
      for (auto *PVD : MD->parameters())
        IsIncomplete |= ReturnOrParamTypeIsIncomplete(PVD->getType(), OpLoc,
                                                      PVD->getBeginLoc());
      if (IsIncomplete)
        return QualType();
    }

    // Under the MS ABI, lock down the inheritance model now.
    if (Context.getTargetInfo().getCXXABI().isMicrosoft())
      (void)isCompleteType(OpLoc, MPTy);
    return MPTy;
  } else if (lval != Expr::LV_Valid && lval != Expr::LV_IncompleteVoidType) {
    // C99 6.5.3.2p1
    // The operand must be either an l-value or a function designator
    if (!op->getType()->isFunctionType()) {
      // Use a special diagnostic for loads from property references.
      if (isa<PseudoObjectExpr>(op)) {
        AddressOfError = AO_Property_Expansion;
      } else {
        Diag(OpLoc, diag::err_typecheck_invalid_lvalue_addrof)
          << op->getType() << op->getSourceRange();
        return QualType();
      }
    } else if (const auto *DRE = dyn_cast<DeclRefExpr>(op)) {
      if (const auto *MD = dyn_cast_or_null<CXXMethodDecl>(DRE->getDecl()))
        CheckUseOfCXXMethodAsAddressOfOperand(OpLoc, OrigOp.get(), MD);
    }

  } else if (op->getObjectKind() == OK_BitField) { // C99 6.5.3.2p1
    // The operand cannot be a bit-field
    AddressOfError = AO_Bit_Field;
  } else if (op->getObjectKind() == OK_VectorComponent) {
    // The operand cannot be an element of a vector
    AddressOfError = AO_Vector_Element;
  } else if (op->getObjectKind() == OK_MatrixComponent) {
    // The operand cannot be an element of a matrix.
    AddressOfError = AO_Matrix_Element;
  } else if (dcl) { // C99 6.5.3.2p1
    // We have an lvalue with a decl. Make sure the decl is not declared
    // with the register storage-class specifier.
    if (const VarDecl *vd = dyn_cast<VarDecl>(dcl)) {
      // in C++ it is not error to take address of a register
      // variable (c++03 7.1.1P3)
      if (vd->getStorageClass() == SC_Register &&
          !getLangOpts().CPlusPlus) {
        AddressOfError = AO_Register_Variable;
      }
    } else if (isa<MSPropertyDecl>(dcl)) {
      AddressOfError = AO_Property_Expansion;
    } else if (isa<FunctionTemplateDecl>(dcl)) {
      return Context.OverloadTy;
    } else if (isa<FieldDecl>(dcl) || isa<IndirectFieldDecl>(dcl)) {
      // Okay: we can take the address of a field.
      // Could be a pointer to member, though, if there is an explicit
      // scope qualifier for the class.

      // [C++26] [expr.prim.id.general]
      // If an id-expression E denotes a non-static non-type member
      // of some class C [...] and if E is a qualified-id, E is
      // not the un-parenthesized operand of the unary & operator [...]
      // the id-expression is transformed into a class member access expression.
      if (isa<DeclRefExpr>(op) && cast<DeclRefExpr>(op)->getQualifier() &&
          !isa<ParenExpr>(OrigOp.get())) {
        DeclContext *Ctx = dcl->getDeclContext();
        if (Ctx && Ctx->isRecord()) {
          if (dcl->getType()->isReferenceType()) {
            Diag(OpLoc,
                 diag::err_cannot_form_pointer_to_member_of_reference_type)
              << dcl->getDeclName() << dcl->getType();
            return QualType();
          }

          while (cast<RecordDecl>(Ctx)->isAnonymousStructOrUnion())
            Ctx = Ctx->getParent();

          QualType MPTy = Context.getMemberPointerType(
              op->getType(),
              Context.getTypeDeclType(cast<RecordDecl>(Ctx)).getTypePtr());
          // Under the MS ABI, lock down the inheritance model now.
          if (Context.getTargetInfo().getCXXABI().isMicrosoft())
            (void)isCompleteType(OpLoc, MPTy);
          return MPTy;
        }
      }
    } else if (!isa<FunctionDecl, NonTypeTemplateParmDecl, BindingDecl,
                    MSGuidDecl, UnnamedGlobalConstantDecl>(dcl))
      llvm_unreachable("Unknown/unexpected decl type");
  }

  if (AddressOfError != AO_No_Error) {
    diagnoseAddressOfInvalidType(*this, OpLoc, op, AddressOfError);
    return QualType();
  }

  if (lval == Expr::LV_IncompleteVoidType) {
    // Taking the address of a void variable is technically illegal, but we
    // allow it in cases which are otherwise valid.
    // Example: "extern void x; void* y = &x;".
    Diag(OpLoc, diag::ext_typecheck_addrof_void) << op->getSourceRange();
  }

  // If the operand has type "type", the result has type "pointer to type".
  if (op->getType()->isObjCObjectType())
    return Context.getObjCObjectPointerType(op->getType());

  // Cannot take the address of WebAssembly references or tables.
  if (Context.getTargetInfo().getTriple().isWasm()) {
    QualType OpTy = op->getType();
    if (OpTy.isWebAssemblyReferenceType()) {
      Diag(OpLoc, diag::err_wasm_ca_reference)
          << 1 << OrigOp.get()->getSourceRange();
      return QualType();
    }
    if (OpTy->isWebAssemblyTableType()) {
      Diag(OpLoc, diag::err_wasm_table_pr)
          << 1 << OrigOp.get()->getSourceRange();
      return QualType();
    }
  }

  CheckAddressOfPackedMember(op);

  return Context.getPointerType(op->getType());
}

static void RecordModifiableNonNullParam(Sema &S, const Expr *Exp) {
  const DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(Exp);
  if (!DRE)
    return;
  const Decl *D = DRE->getDecl();
  if (!D)
    return;
  const ParmVarDecl *Param = dyn_cast<ParmVarDecl>(D);
  if (!Param)
    return;
  if (const FunctionDecl* FD = dyn_cast<FunctionDecl>(Param->getDeclContext()))
    if (!FD->hasAttr<NonNullAttr>() && !Param->hasAttr<NonNullAttr>())
      return;
  if (FunctionScopeInfo *FD = S.getCurFunction())
    FD->ModifiedNonNullParams.insert(Param);
}

/// CheckIndirectionOperand - Type check unary indirection (prefix '*').
static QualType CheckIndirectionOperand(Sema &S, Expr *Op, ExprValueKind &VK,
                                        SourceLocation OpLoc,
                                        bool IsAfterAmp = false) {
  ExprResult ConvResult = S.UsualUnaryConversions(Op);
  if (ConvResult.isInvalid())
    return QualType();
  Op = ConvResult.get();
  QualType OpTy = Op->getType();
  QualType Result;

  if (isa<CXXReinterpretCastExpr>(Op)) {
    QualType OpOrigType = Op->IgnoreParenCasts()->getType();
    S.CheckCompatibleReinterpretCast(OpOrigType, OpTy, /*IsDereference*/true,
                                     Op->getSourceRange());
  }

  if (const PointerType *PT = OpTy->getAs<PointerType>())
  {
    Result = PT->getPointeeType();
  }
  else if (const ObjCObjectPointerType *OPT =
             OpTy->getAs<ObjCObjectPointerType>())
    Result = OPT->getPointeeType();
  else {
    ExprResult PR = S.CheckPlaceholderExpr(Op);
    if (PR.isInvalid()) return QualType();
    if (PR.get() != Op)
      return CheckIndirectionOperand(S, PR.get(), VK, OpLoc);
  }

  if (Result.isNull()) {
    S.Diag(OpLoc, diag::err_typecheck_indirection_requires_pointer)
      << OpTy << Op->getSourceRange();
    return QualType();
  }

  if (Result->isVoidType()) {
    // C++ [expr.unary.op]p1:
    //   [...] the expression to which [the unary * operator] is applied shall
    //   be a pointer to an object type, or a pointer to a function type
    LangOptions LO = S.getLangOpts();
    if (LO.CPlusPlus)
      S.Diag(OpLoc, diag::err_typecheck_indirection_through_void_pointer_cpp)
          << OpTy << Op->getSourceRange();
    else if (!(LO.C99 && IsAfterAmp) && !S.isUnevaluatedContext())
      S.Diag(OpLoc, diag::ext_typecheck_indirection_through_void_pointer)
          << OpTy << Op->getSourceRange();
  }

  // Dereferences are usually l-values...
  VK = VK_LValue;

  // ...except that certain expressions are never l-values in C.
  if (!S.getLangOpts().CPlusPlus && Result.isCForbiddenLValueType())
    VK = VK_PRValue;

  return Result;
}

BinaryOperatorKind Sema::ConvertTokenKindToBinaryOpcode(tok::TokenKind Kind) {
  BinaryOperatorKind Opc;
  switch (Kind) {
  default: llvm_unreachable("Unknown binop!");
  case tok::periodstar:           Opc = BO_PtrMemD; break;
  case tok::arrowstar:            Opc = BO_PtrMemI; break;
  case tok::star:                 Opc = BO_Mul; break;
  case tok::slash:                Opc = BO_Div; break;
  case tok::percent:              Opc = BO_Rem; break;
  case tok::plus:                 Opc = BO_Add; break;
  case tok::minus:                Opc = BO_Sub; break;
  case tok::lessless:             Opc = BO_Shl; break;
  case tok::greatergreater:       Opc = BO_Shr; break;
  case tok::lessequal:            Opc = BO_LE; break;
  case tok::less:                 Opc = BO_LT; break;
  case tok::greaterequal:         Opc = BO_GE; break;
  case tok::greater:              Opc = BO_GT; break;
  case tok::exclaimequal:         Opc = BO_NE; break;
  case tok::equalequal:           Opc = BO_EQ; break;
  case tok::spaceship:            Opc = BO_Cmp; break;
  case tok::amp:                  Opc = BO_And; break;
  case tok::caret:                Opc = BO_Xor; break;
  case tok::pipe:                 Opc = BO_Or; break;
  case tok::ampamp:               Opc = BO_LAnd; break;
  case tok::pipepipe:             Opc = BO_LOr; break;
  case tok::equal:                Opc = BO_Assign; break;
  case tok::starequal:            Opc = BO_MulAssign; break;
  case tok::slashequal:           Opc = BO_DivAssign; break;
  case tok::percentequal:         Opc = BO_RemAssign; break;
  case tok::plusequal:            Opc = BO_AddAssign; break;
  case tok::minusequal:           Opc = BO_SubAssign; break;
  case tok::lesslessequal:        Opc = BO_ShlAssign; break;
  case tok::greatergreaterequal:  Opc = BO_ShrAssign; break;
  case tok::ampequal:             Opc = BO_AndAssign; break;
  case tok::caretequal:           Opc = BO_XorAssign; break;
  case tok::pipeequal:            Opc = BO_OrAssign; break;
  case tok::comma:                Opc = BO_Comma; break;
  }
  return Opc;
}

static inline UnaryOperatorKind ConvertTokenKindToUnaryOpcode(
  tok::TokenKind Kind) {
  UnaryOperatorKind Opc;
  switch (Kind) {
  default: llvm_unreachable("Unknown unary op!");
  case tok::plusplus:     Opc = UO_PreInc; break;
  case tok::minusminus:   Opc = UO_PreDec; break;
  case tok::amp:          Opc = UO_AddrOf; break;
  case tok::star:         Opc = UO_Deref; break;
  case tok::plus:         Opc = UO_Plus; break;
  case tok::minus:        Opc = UO_Minus; break;
  case tok::tilde:        Opc = UO_Not; break;
  case tok::exclaim:      Opc = UO_LNot; break;
  case tok::kw___real:    Opc = UO_Real; break;
  case tok::kw___imag:    Opc = UO_Imag; break;
  case tok::kw___extension__: Opc = UO_Extension; break;
  }
  return Opc;
}

const FieldDecl *
Sema::getSelfAssignmentClassMemberCandidate(const ValueDecl *SelfAssigned) {
  // Explore the case for adding 'this->' to the LHS of a self assignment, very
  // common for setters.
  // struct A {
  // int X;
  // -void setX(int X) { X = X; }
  // +void setX(int X) { this->X = X; }
  // };

  // Only consider parameters for self assignment fixes.
  if (!isa<ParmVarDecl>(SelfAssigned))
    return nullptr;
  const auto *Method =
      dyn_cast_or_null<CXXMethodDecl>(getCurFunctionDecl(true));
  if (!Method)
    return nullptr;

  const CXXRecordDecl *Parent = Method->getParent();
  // In theory this is fixable if the lambda explicitly captures this, but
  // that's added complexity that's rarely going to be used.
  if (Parent->isLambda())
    return nullptr;

  // FIXME: Use an actual Lookup operation instead of just traversing fields
  // in order to get base class fields.
  auto Field =
      llvm::find_if(Parent->fields(),
                    [Name(SelfAssigned->getDeclName())](const FieldDecl *F) {
                      return F->getDeclName() == Name;
                    });
  return (Field != Parent->field_end()) ? *Field : nullptr;
}

/// DiagnoseSelfAssignment - Emits a warning if a value is assigned to itself.
/// This warning suppressed in the event of macro expansions.
static void DiagnoseSelfAssignment(Sema &S, Expr *LHSExpr, Expr *RHSExpr,
                                   SourceLocation OpLoc, bool IsBuiltin) {
  if (S.inTemplateInstantiation())
    return;
  if (S.isUnevaluatedContext())
    return;
  if (OpLoc.isInvalid() || OpLoc.isMacroID())
    return;
  LHSExpr = LHSExpr->IgnoreParenImpCasts();
  RHSExpr = RHSExpr->IgnoreParenImpCasts();
  const DeclRefExpr *LHSDeclRef = dyn_cast<DeclRefExpr>(LHSExpr);
  const DeclRefExpr *RHSDeclRef = dyn_cast<DeclRefExpr>(RHSExpr);
  if (!LHSDeclRef || !RHSDeclRef ||
      LHSDeclRef->getLocation().isMacroID() ||
      RHSDeclRef->getLocation().isMacroID())
    return;
  const ValueDecl *LHSDecl =
    cast<ValueDecl>(LHSDeclRef->getDecl()->getCanonicalDecl());
  const ValueDecl *RHSDecl =
    cast<ValueDecl>(RHSDeclRef->getDecl()->getCanonicalDecl());
  if (LHSDecl != RHSDecl)
    return;
  if (LHSDecl->getType().isVolatileQualified())
    return;
  if (const ReferenceType *RefTy = LHSDecl->getType()->getAs<ReferenceType>())
    if (RefTy->getPointeeType().isVolatileQualified())
      return;

  auto Diag = S.Diag(OpLoc, IsBuiltin ? diag::warn_self_assignment_builtin
                                      : diag::warn_self_assignment_overloaded)
              << LHSDeclRef->getType() << LHSExpr->getSourceRange()
              << RHSExpr->getSourceRange();
  if (const FieldDecl *SelfAssignField =
          S.getSelfAssignmentClassMemberCandidate(RHSDecl))
    Diag << 1 << SelfAssignField
         << FixItHint::CreateInsertion(LHSDeclRef->getBeginLoc(), "this->");
  else
    Diag << 0;
}

/// Check if a bitwise-& is performed on an Objective-C pointer.  This
/// is usually indicative of introspection within the Objective-C pointer.
static void checkObjCPointerIntrospection(Sema &S, ExprResult &L, ExprResult &R,
                                          SourceLocation OpLoc) {
  if (!S.getLangOpts().ObjC)
    return;

  const Expr *ObjCPointerExpr = nullptr, *OtherExpr = nullptr;
  const Expr *LHS = L.get();
  const Expr *RHS = R.get();

  if (LHS->IgnoreParenCasts()->getType()->isObjCObjectPointerType()) {
    ObjCPointerExpr = LHS;
    OtherExpr = RHS;
  }
  else if (RHS->IgnoreParenCasts()->getType()->isObjCObjectPointerType()) {
    ObjCPointerExpr = RHS;
    OtherExpr = LHS;
  }

  // This warning is deliberately made very specific to reduce false
  // positives with logic that uses '&' for hashing.  This logic mainly
  // looks for code trying to introspect into tagged pointers, which
  // code should generally never do.
  if (ObjCPointerExpr && isa<IntegerLiteral>(OtherExpr->IgnoreParenCasts())) {
    unsigned Diag = diag::warn_objc_pointer_masking;
    // Determine if we are introspecting the result of performSelectorXXX.
    const Expr *Ex = ObjCPointerExpr->IgnoreParenCasts();
    // Special case messages to -performSelector and friends, which
    // can return non-pointer values boxed in a pointer value.
    // Some clients may wish to silence warnings in this subcase.
    if (const ObjCMessageExpr *ME = dyn_cast<ObjCMessageExpr>(Ex)) {
      Selector S = ME->getSelector();
      StringRef SelArg0 = S.getNameForSlot(0);
      if (SelArg0.starts_with("performSelector"))
        Diag = diag::warn_objc_pointer_masking_performSelector;
    }

    S.Diag(OpLoc, Diag)
      << ObjCPointerExpr->getSourceRange();
  }
}

static NamedDecl *getDeclFromExpr(Expr *E) {
  if (!E)
    return nullptr;
  if (auto *DRE = dyn_cast<DeclRefExpr>(E))
    return DRE->getDecl();
  if (auto *ME = dyn_cast<MemberExpr>(E))
    return ME->getMemberDecl();
  if (auto *IRE = dyn_cast<ObjCIvarRefExpr>(E))
    return IRE->getDecl();
  return nullptr;
}

// This helper function promotes a binary operator's operands (which are of a
// half vector type) to a vector of floats and then truncates the result to
// a vector of either half or short.
static ExprResult convertHalfVecBinOp(Sema &S, ExprResult LHS, ExprResult RHS,
                                      BinaryOperatorKind Opc, QualType ResultTy,
                                      ExprValueKind VK, ExprObjectKind OK,
                                      bool IsCompAssign, SourceLocation OpLoc,
                                      FPOptionsOverride FPFeatures) {
  auto &Context = S.getASTContext();
  assert((isVector(ResultTy, Context.HalfTy) ||
          isVector(ResultTy, Context.ShortTy)) &&
         "Result must be a vector of half or short");
  assert(isVector(LHS.get()->getType(), Context.HalfTy) &&
         isVector(RHS.get()->getType(), Context.HalfTy) &&
         "both operands expected to be a half vector");

  RHS = convertVector(RHS.get(), Context.FloatTy, S);
  QualType BinOpResTy = RHS.get()->getType();

  // If Opc is a comparison, ResultType is a vector of shorts. In that case,
  // change BinOpResTy to a vector of ints.
  if (isVector(ResultTy, Context.ShortTy))
    BinOpResTy = S.GetSignedVectorType(BinOpResTy);

  if (IsCompAssign)
    return CompoundAssignOperator::Create(Context, LHS.get(), RHS.get(), Opc,
                                          ResultTy, VK, OK, OpLoc, FPFeatures,
                                          BinOpResTy, BinOpResTy);

  LHS = convertVector(LHS.get(), Context.FloatTy, S);
  auto *BO = BinaryOperator::Create(Context, LHS.get(), RHS.get(), Opc,
                                    BinOpResTy, VK, OK, OpLoc, FPFeatures);
  return convertVector(BO, ResultTy->castAs<VectorType>()->getElementType(), S);
}

static std::pair<ExprResult, ExprResult>
CorrectDelayedTyposInBinOp(Sema &S, BinaryOperatorKind Opc, Expr *LHSExpr,
                           Expr *RHSExpr) {
  ExprResult LHS = LHSExpr, RHS = RHSExpr;
  if (!S.Context.isDependenceAllowed()) {
    // C cannot handle TypoExpr nodes on either side of a binop because it
    // doesn't handle dependent types properly, so make sure any TypoExprs have
    // been dealt with before checking the operands.
    LHS = S.CorrectDelayedTyposInExpr(LHS);
    RHS = S.CorrectDelayedTyposInExpr(
        RHS, /*InitDecl=*/nullptr, /*RecoverUncorrectedTypos=*/false,
        [Opc, LHS](Expr *E) {
          if (Opc != BO_Assign)
            return ExprResult(E);
          // Avoid correcting the RHS to the same Expr as the LHS.
          Decl *D = getDeclFromExpr(E);
          return (D && D == getDeclFromExpr(LHS.get())) ? ExprError() : E;
        });
  }
  return std::make_pair(LHS, RHS);
}

/// Returns true if conversion between vectors of halfs and vectors of floats
/// is needed.
static bool needsConversionOfHalfVec(bool OpRequiresConversion, ASTContext &Ctx,
                                     Expr *E0, Expr *E1 = nullptr) {
  if (!OpRequiresConversion || Ctx.getLangOpts().NativeHalfType ||
      Ctx.getTargetInfo().useFP16ConversionIntrinsics())
    return false;

  auto HasVectorOfHalfType = [&Ctx](Expr *E) {
    QualType Ty = E->IgnoreImplicit()->getType();

    // Don't promote half precision neon vectors like float16x4_t in arm_neon.h
    // to vectors of floats. Although the element type of the vectors is __fp16,
    // the vectors shouldn't be treated as storage-only types. See the
    // discussion here: https://reviews.llvm.org/rG825235c140e7
    if (const VectorType *VT = Ty->getAs<VectorType>()) {
      if (VT->getVectorKind() == VectorKind::Neon)
        return false;
      return VT->getElementType().getCanonicalType() == Ctx.HalfTy;
    }
    return false;
  };

  return HasVectorOfHalfType(E0) && (!E1 || HasVectorOfHalfType(E1));
}

ExprResult Sema::CreateBuiltinBinOp(SourceLocation OpLoc,
                                    BinaryOperatorKind Opc,
                                    Expr *LHSExpr, Expr *RHSExpr) {
  if (getLangOpts().CPlusPlus11 && isa<InitListExpr>(RHSExpr)) {
    // The syntax only allows initializer lists on the RHS of assignment,
    // so we don't need to worry about accepting invalid code for
    // non-assignment operators.
    // C++11 5.17p9:
    //   The meaning of x = {v} [...] is that of x = T(v) [...]. The meaning
    //   of x = {} is x = T().
    InitializationKind Kind = InitializationKind::CreateDirectList(
        RHSExpr->getBeginLoc(), RHSExpr->getBeginLoc(), RHSExpr->getEndLoc());
    InitializedEntity Entity =
        InitializedEntity::InitializeTemporary(LHSExpr->getType());
    InitializationSequence InitSeq(*this, Entity, Kind, RHSExpr);
    ExprResult Init = InitSeq.Perform(*this, Entity, Kind, RHSExpr);
    if (Init.isInvalid())
      return Init;
    RHSExpr = Init.get();
  }

  ExprResult LHS = LHSExpr, RHS = RHSExpr;
  QualType ResultTy;     // Result type of the binary operator.
  // The following two variables are used for compound assignment operators
  QualType CompLHSTy;    // Type of LHS after promotions for computation
  QualType CompResultTy; // Type of computation result
  ExprValueKind VK = VK_PRValue;
  ExprObjectKind OK = OK_Ordinary;
  bool ConvertHalfVec = false;

  std::tie(LHS, RHS) = CorrectDelayedTyposInBinOp(*this, Opc, LHSExpr, RHSExpr);
  if (!LHS.isUsable() || !RHS.isUsable())
    return ExprError();

  if (getLangOpts().OpenCL) {
    QualType LHSTy = LHSExpr->getType();
    QualType RHSTy = RHSExpr->getType();
    // OpenCLC v2.0 s6.13.11.1 allows atomic variables to be initialized by
    // the ATOMIC_VAR_INIT macro.
    if (LHSTy->isAtomicType() || RHSTy->isAtomicType()) {
      SourceRange SR(LHSExpr->getBeginLoc(), RHSExpr->getEndLoc());
      if (BO_Assign == Opc)
        Diag(OpLoc, diag::err_opencl_atomic_init) << 0 << SR;
      else
        ResultTy = InvalidOperands(OpLoc, LHS, RHS);
      return ExprError();
    }

    // OpenCL special types - image, sampler, pipe, and blocks are to be used
    // only with a builtin functions and therefore should be disallowed here.
    if (LHSTy->isImageType() || RHSTy->isImageType() ||
        LHSTy->isSamplerT() || RHSTy->isSamplerT() ||
        LHSTy->isPipeType() || RHSTy->isPipeType() ||
        LHSTy->isBlockPointerType() || RHSTy->isBlockPointerType()) {
      ResultTy = InvalidOperands(OpLoc, LHS, RHS);
      return ExprError();
    }
  }

  checkTypeSupport(LHSExpr->getType(), OpLoc, /*ValueDecl*/ nullptr);
  checkTypeSupport(RHSExpr->getType(), OpLoc, /*ValueDecl*/ nullptr);

  switch (Opc) {
  case BO_Assign:
    ResultTy = CheckAssignmentOperands(LHS.get(), RHS, OpLoc, QualType(), Opc);
    if (getLangOpts().CPlusPlus &&
        LHS.get()->getObjectKind() != OK_ObjCProperty) {
      VK = LHS.get()->getValueKind();
      OK = LHS.get()->getObjectKind();
    }
    if (!ResultTy.isNull()) {
      DiagnoseSelfAssignment(*this, LHS.get(), RHS.get(), OpLoc, true);
      DiagnoseSelfMove(LHS.get(), RHS.get(), OpLoc);

      // Avoid copying a block to the heap if the block is assigned to a local
      // auto variable that is declared in the same scope as the block. This
      // optimization is unsafe if the local variable is declared in an outer
      // scope. For example:
      //
      // BlockTy b;
      // {
      //   b = ^{...};
      // }
      // // It is unsafe to invoke the block here if it wasn't copied to the
      // // heap.
      // b();

      if (auto *BE = dyn_cast<BlockExpr>(RHS.get()->IgnoreParens()))
        if (auto *DRE = dyn_cast<DeclRefExpr>(LHS.get()->IgnoreParens()))
          if (auto *VD = dyn_cast<VarDecl>(DRE->getDecl()))
            if (VD->hasLocalStorage() && getCurScope()->isDeclScope(VD))
              BE->getBlockDecl()->setCanAvoidCopyToHeap();

      if (LHS.get()->getType().hasNonTrivialToPrimitiveCopyCUnion())
        checkNonTrivialCUnion(LHS.get()->getType(), LHS.get()->getExprLoc(),
                              NTCUC_Assignment, NTCUK_Copy);
    }
    RecordModifiableNonNullParam(*this, LHS.get());
    break;
  case BO_PtrMemD:
  case BO_PtrMemI:
    ResultTy = CheckPointerToMemberOperands(LHS, RHS, VK, OpLoc,
                                            Opc == BO_PtrMemI);
    break;
  case BO_Mul:
  case BO_Div:
    ConvertHalfVec = true;
    ResultTy = CheckMultiplyDivideOperands(LHS, RHS, OpLoc, false,
                                           Opc == BO_Div);
    break;
  case BO_Rem:
    ResultTy = CheckRemainderOperands(LHS, RHS, OpLoc);
    break;
  case BO_Add:
    ConvertHalfVec = true;
    ResultTy = CheckAdditionOperands(LHS, RHS, OpLoc, Opc);
    break;
  case BO_Sub:
    ConvertHalfVec = true;
    ResultTy = CheckSubtractionOperands(LHS, RHS, OpLoc);
    break;
  case BO_Shl:
  case BO_Shr:
    ResultTy = CheckShiftOperands(LHS, RHS, OpLoc, Opc);
    break;
  case BO_LE:
  case BO_LT:
  case BO_GE:
  case BO_GT:
    ConvertHalfVec = true;
    ResultTy = CheckCompareOperands(LHS, RHS, OpLoc, Opc);

    if (const auto *BI = dyn_cast<BinaryOperator>(LHSExpr);
        BI && BI->isComparisonOp())
      Diag(OpLoc, diag::warn_consecutive_comparison);

    break;
  case BO_EQ:
  case BO_NE:
    ConvertHalfVec = true;
    ResultTy = CheckCompareOperands(LHS, RHS, OpLoc, Opc);
    break;
  case BO_Cmp:
    ConvertHalfVec = true;
    ResultTy = CheckCompareOperands(LHS, RHS, OpLoc, Opc);
    assert(ResultTy.isNull() || ResultTy->getAsCXXRecordDecl());
    break;
  case BO_And:
    checkObjCPointerIntrospection(*this, LHS, RHS, OpLoc);
    [[fallthrough]];
  case BO_Xor:
  case BO_Or:
    ResultTy = CheckBitwiseOperands(LHS, RHS, OpLoc, Opc);
    break;
  case BO_LAnd:
  case BO_LOr:
    ConvertHalfVec = true;
    ResultTy = CheckLogicalOperands(LHS, RHS, OpLoc, Opc);
    break;
  case BO_MulAssign:
  case BO_DivAssign:
    ConvertHalfVec = true;
    CompResultTy = CheckMultiplyDivideOperands(LHS, RHS, OpLoc, true,
                                               Opc == BO_DivAssign);
    CompLHSTy = CompResultTy;
    if (!CompResultTy.isNull() && !LHS.isInvalid() && !RHS.isInvalid())
      ResultTy =
          CheckAssignmentOperands(LHS.get(), RHS, OpLoc, CompResultTy, Opc);
    break;
  case BO_RemAssign:
    CompResultTy = CheckRemainderOperands(LHS, RHS, OpLoc, true);
    CompLHSTy = CompResultTy;
    if (!CompResultTy.isNull() && !LHS.isInvalid() && !RHS.isInvalid())
      ResultTy =
          CheckAssignmentOperands(LHS.get(), RHS, OpLoc, CompResultTy, Opc);
    break;
  case BO_AddAssign:
    ConvertHalfVec = true;
    CompResultTy = CheckAdditionOperands(LHS, RHS, OpLoc, Opc, &CompLHSTy);
    if (!CompResultTy.isNull() && !LHS.isInvalid() && !RHS.isInvalid())
      ResultTy =
          CheckAssignmentOperands(LHS.get(), RHS, OpLoc, CompResultTy, Opc);
    break;
  case BO_SubAssign:
    ConvertHalfVec = true;
    CompResultTy = CheckSubtractionOperands(LHS, RHS, OpLoc, &CompLHSTy);
    if (!CompResultTy.isNull() && !LHS.isInvalid() && !RHS.isInvalid())
      ResultTy =
          CheckAssignmentOperands(LHS.get(), RHS, OpLoc, CompResultTy, Opc);
    break;
  case BO_ShlAssign:
  case BO_ShrAssign:
    CompResultTy = CheckShiftOperands(LHS, RHS, OpLoc, Opc, true);
    CompLHSTy = CompResultTy;
    if (!CompResultTy.isNull() && !LHS.isInvalid() && !RHS.isInvalid())
      ResultTy =
          CheckAssignmentOperands(LHS.get(), RHS, OpLoc, CompResultTy, Opc);
    break;
  case BO_AndAssign:
  case BO_OrAssign: // fallthrough
    DiagnoseSelfAssignment(*this, LHS.get(), RHS.get(), OpLoc, true);
    [[fallthrough]];
  case BO_XorAssign:
    CompResultTy = CheckBitwiseOperands(LHS, RHS, OpLoc, Opc);
    CompLHSTy = CompResultTy;
    if (!CompResultTy.isNull() && !LHS.isInvalid() && !RHS.isInvalid())
      ResultTy =
          CheckAssignmentOperands(LHS.get(), RHS, OpLoc, CompResultTy, Opc);
    break;
  case BO_Comma:
    ResultTy = CheckCommaOperands(*this, LHS, RHS, OpLoc);
    if (getLangOpts().CPlusPlus && !RHS.isInvalid()) {
      VK = RHS.get()->getValueKind();
      OK = RHS.get()->getObjectKind();
    }
    break;
  }
  if (ResultTy.isNull() || LHS.isInvalid() || RHS.isInvalid())
    return ExprError();

  // Some of the binary operations require promoting operands of half vector to
  // float vectors and truncating the result back to half vector. For now, we do
  // this only when HalfArgsAndReturn is set (that is, when the target is arm or
  // arm64).
  assert(
      (Opc == BO_Comma || isVector(RHS.get()->getType(), Context.HalfTy) ==
                              isVector(LHS.get()->getType(), Context.HalfTy)) &&
      "both sides are half vectors or neither sides are");
  ConvertHalfVec =
      needsConversionOfHalfVec(ConvertHalfVec, Context, LHS.get(), RHS.get());

  // Check for array bounds violations for both sides of the BinaryOperator
  CheckArrayAccess(LHS.get());
  CheckArrayAccess(RHS.get());

  if (const ObjCIsaExpr *OISA = dyn_cast<ObjCIsaExpr>(LHS.get()->IgnoreParenCasts())) {
    NamedDecl *ObjectSetClass = LookupSingleName(TUScope,
                                                 &Context.Idents.get("object_setClass"),
                                                 SourceLocation(), LookupOrdinaryName);
    if (ObjectSetClass && isa<ObjCIsaExpr>(LHS.get())) {
      SourceLocation RHSLocEnd = getLocForEndOfToken(RHS.get()->getEndLoc());
      Diag(LHS.get()->getExprLoc(), diag::warn_objc_isa_assign)
          << FixItHint::CreateInsertion(LHS.get()->getBeginLoc(),
                                        "object_setClass(")
          << FixItHint::CreateReplacement(SourceRange(OISA->getOpLoc(), OpLoc),
                                          ",")
          << FixItHint::CreateInsertion(RHSLocEnd, ")");
    }
    else
      Diag(LHS.get()->getExprLoc(), diag::warn_objc_isa_assign);
  }
  else if (const ObjCIvarRefExpr *OIRE =
           dyn_cast<ObjCIvarRefExpr>(LHS.get()->IgnoreParenCasts()))
    DiagnoseDirectIsaAccess(*this, OIRE, OpLoc, RHS.get());

  // Opc is not a compound assignment if CompResultTy is null.
  if (CompResultTy.isNull()) {
    if (ConvertHalfVec)
      return convertHalfVecBinOp(*this, LHS, RHS, Opc, ResultTy, VK, OK, false,
                                 OpLoc, CurFPFeatureOverrides());
    return BinaryOperator::Create(Context, LHS.get(), RHS.get(), Opc, ResultTy,
                                  VK, OK, OpLoc, CurFPFeatureOverrides());
  }

  // Handle compound assignments.
  if (getLangOpts().CPlusPlus && LHS.get()->getObjectKind() !=
      OK_ObjCProperty) {
    VK = VK_LValue;
    OK = LHS.get()->getObjectKind();
  }

  // The LHS is not converted to the result type for fixed-point compound
  // assignment as the common type is computed on demand. Reset the CompLHSTy
  // to the LHS type we would have gotten after unary conversions.
  if (CompResultTy->isFixedPointType())
    CompLHSTy = UsualUnaryConversions(LHS.get()).get()->getType();

  if (ConvertHalfVec)
    return convertHalfVecBinOp(*this, LHS, RHS, Opc, ResultTy, VK, OK, true,
                               OpLoc, CurFPFeatureOverrides());

  return CompoundAssignOperator::Create(
      Context, LHS.get(), RHS.get(), Opc, ResultTy, VK, OK, OpLoc,
      CurFPFeatureOverrides(), CompLHSTy, CompResultTy);
}

/// DiagnoseBitwisePrecedence - Emit a warning when bitwise and comparison
/// operators are mixed in a way that suggests that the programmer forgot that
/// comparison operators have higher precedence. The most typical example of
/// such code is "flags & 0x0020 != 0", which is equivalent to "flags & 1".
static void DiagnoseBitwisePrecedence(Sema &Self, BinaryOperatorKind Opc,
                                      SourceLocation OpLoc, Expr *LHSExpr,
                                      Expr *RHSExpr) {
  BinaryOperator *LHSBO = dyn_cast<BinaryOperator>(LHSExpr);
  BinaryOperator *RHSBO = dyn_cast<BinaryOperator>(RHSExpr);

  // Check that one of the sides is a comparison operator and the other isn't.
  bool isLeftComp = LHSBO && LHSBO->isComparisonOp();
  bool isRightComp = RHSBO && RHSBO->isComparisonOp();
  if (isLeftComp == isRightComp)
    return;

  // Bitwise operations are sometimes used as eager logical ops.
  // Don't diagnose this.
  bool isLeftBitwise = LHSBO && LHSBO->isBitwiseOp();
  bool isRightBitwise = RHSBO && RHSBO->isBitwiseOp();
  if (isLeftBitwise || isRightBitwise)
    return;

  SourceRange DiagRange = isLeftComp
                              ? SourceRange(LHSExpr->getBeginLoc(), OpLoc)
                              : SourceRange(OpLoc, RHSExpr->getEndLoc());
  StringRef OpStr = isLeftComp ? LHSBO->getOpcodeStr() : RHSBO->getOpcodeStr();
  SourceRange ParensRange =
      isLeftComp
          ? SourceRange(LHSBO->getRHS()->getBeginLoc(), RHSExpr->getEndLoc())
          : SourceRange(LHSExpr->getBeginLoc(), RHSBO->getLHS()->getEndLoc());

  Self.Diag(OpLoc, diag::warn_precedence_bitwise_rel)
    << DiagRange << BinaryOperator::getOpcodeStr(Opc) << OpStr;
  SuggestParentheses(Self, OpLoc,
    Self.PDiag(diag::note_precedence_silence) << OpStr,
    (isLeftComp ? LHSExpr : RHSExpr)->getSourceRange());
  SuggestParentheses(Self, OpLoc,
    Self.PDiag(diag::note_precedence_bitwise_first)
      << BinaryOperator::getOpcodeStr(Opc),
    ParensRange);
}

/// It accepts a '&&' expr that is inside a '||' one.
/// Emit a diagnostic together with a fixit hint that wraps the '&&' expression
/// in parentheses.
static void
EmitDiagnosticForLogicalAndInLogicalOr(Sema &Self, SourceLocation OpLoc,
                                       BinaryOperator *Bop) {
  assert(Bop->getOpcode() == BO_LAnd);
  Self.Diag(Bop->getOperatorLoc(), diag::warn_logical_and_in_logical_or)
      << Bop->getSourceRange() << OpLoc;
  SuggestParentheses(Self, Bop->getOperatorLoc(),
    Self.PDiag(diag::note_precedence_silence)
      << Bop->getOpcodeStr(),
    Bop->getSourceRange());
}

/// Look for '&&' in the left hand of a '||' expr.
static void DiagnoseLogicalAndInLogicalOrLHS(Sema &S, SourceLocation OpLoc,
                                             Expr *LHSExpr, Expr *RHSExpr) {
  if (BinaryOperator *Bop = dyn_cast<BinaryOperator>(LHSExpr)) {
    if (Bop->getOpcode() == BO_LAnd) {
      // If it's "string_literal && a || b" don't warn since the precedence
      // doesn't matter.
      if (!isa<StringLiteral>(Bop->getLHS()->IgnoreParenImpCasts()))
        return EmitDiagnosticForLogicalAndInLogicalOr(S, OpLoc, Bop);
    } else if (Bop->getOpcode() == BO_LOr) {
      if (BinaryOperator *RBop = dyn_cast<BinaryOperator>(Bop->getRHS())) {
        // If it's "a || b && string_literal || c" we didn't warn earlier for
        // "a || b && string_literal", but warn now.
        if (RBop->getOpcode() == BO_LAnd &&
            isa<StringLiteral>(RBop->getRHS()->IgnoreParenImpCasts()))
          return EmitDiagnosticForLogicalAndInLogicalOr(S, OpLoc, RBop);
      }
    }
  }
}

/// Look for '&&' in the right hand of a '||' expr.
static void DiagnoseLogicalAndInLogicalOrRHS(Sema &S, SourceLocation OpLoc,
                                             Expr *LHSExpr, Expr *RHSExpr) {
  if (BinaryOperator *Bop = dyn_cast<BinaryOperator>(RHSExpr)) {
    if (Bop->getOpcode() == BO_LAnd) {
      // If it's "a || b && string_literal" don't warn since the precedence
      // doesn't matter.
      if (!isa<StringLiteral>(Bop->getRHS()->IgnoreParenImpCasts()))
        return EmitDiagnosticForLogicalAndInLogicalOr(S, OpLoc, Bop);
    }
  }
}

/// Look for bitwise op in the left or right hand of a bitwise op with
/// lower precedence and emit a diagnostic together with a fixit hint that wraps
/// the '&' expression in parentheses.
static void DiagnoseBitwiseOpInBitwiseOp(Sema &S, BinaryOperatorKind Opc,
                                         SourceLocation OpLoc, Expr *SubExpr) {
  if (BinaryOperator *Bop = dyn_cast<BinaryOperator>(SubExpr)) {
    if (Bop->isBitwiseOp() && Bop->getOpcode() < Opc) {
      S.Diag(Bop->getOperatorLoc(), diag::warn_bitwise_op_in_bitwise_op)
        << Bop->getOpcodeStr() << BinaryOperator::getOpcodeStr(Opc)
        << Bop->getSourceRange() << OpLoc;
      SuggestParentheses(S, Bop->getOperatorLoc(),
        S.PDiag(diag::note_precedence_silence)
          << Bop->getOpcodeStr(),
        Bop->getSourceRange());
    }
  }
}

static void DiagnoseAdditionInShift(Sema &S, SourceLocation OpLoc,
                                    Expr *SubExpr, StringRef Shift) {
  if (BinaryOperator *Bop = dyn_cast<BinaryOperator>(SubExpr)) {
    if (Bop->getOpcode() == BO_Add || Bop->getOpcode() == BO_Sub) {
      StringRef Op = Bop->getOpcodeStr();
      S.Diag(Bop->getOperatorLoc(), diag::warn_addition_in_bitshift)
          << Bop->getSourceRange() << OpLoc << Shift << Op;
      SuggestParentheses(S, Bop->getOperatorLoc(),
          S.PDiag(diag::note_precedence_silence) << Op,
          Bop->getSourceRange());
    }
  }
}

static void DiagnoseShiftCompare(Sema &S, SourceLocation OpLoc,
                                 Expr *LHSExpr, Expr *RHSExpr) {
  CXXOperatorCallExpr *OCE = dyn_cast<CXXOperatorCallExpr>(LHSExpr);
  if (!OCE)
    return;

  FunctionDecl *FD = OCE->getDirectCallee();
  if (!FD || !FD->isOverloadedOperator())
    return;

  OverloadedOperatorKind Kind = FD->getOverloadedOperator();
  if (Kind != OO_LessLess && Kind != OO_GreaterGreater)
    return;

  S.Diag(OpLoc, diag::warn_overloaded_shift_in_comparison)
      << LHSExpr->getSourceRange() << RHSExpr->getSourceRange()
      << (Kind == OO_LessLess);
  SuggestParentheses(S, OCE->getOperatorLoc(),
                     S.PDiag(diag::note_precedence_silence)
                         << (Kind == OO_LessLess ? "<<" : ">>"),
                     OCE->getSourceRange());
  SuggestParentheses(
      S, OpLoc, S.PDiag(diag::note_evaluate_comparison_first),
      SourceRange(OCE->getArg(1)->getBeginLoc(), RHSExpr->getEndLoc()));
}

/// DiagnoseBinOpPrecedence - Emit warnings for expressions with tricky
/// precedence.
static void DiagnoseBinOpPrecedence(Sema &Self, BinaryOperatorKind Opc,
                                    SourceLocation OpLoc, Expr *LHSExpr,
                                    Expr *RHSExpr){
  // Diagnose "arg1 'bitwise' arg2 'eq' arg3".
  if (BinaryOperator::isBitwiseOp(Opc))
    DiagnoseBitwisePrecedence(Self, Opc, OpLoc, LHSExpr, RHSExpr);

  // Diagnose "arg1 & arg2 | arg3"
  if ((Opc == BO_Or || Opc == BO_Xor) &&
      !OpLoc.isMacroID()/* Don't warn in macros. */) {
    DiagnoseBitwiseOpInBitwiseOp(Self, Opc, OpLoc, LHSExpr);
    DiagnoseBitwiseOpInBitwiseOp(Self, Opc, OpLoc, RHSExpr);
  }

  // Warn about arg1 || arg2 && arg3, as GCC 4.3+ does.
  // We don't warn for 'assert(a || b && "bad")' since this is safe.
  if (Opc == BO_LOr && !OpLoc.isMacroID()/* Don't warn in macros. */) {
    DiagnoseLogicalAndInLogicalOrLHS(Self, OpLoc, LHSExpr, RHSExpr);
    DiagnoseLogicalAndInLogicalOrRHS(Self, OpLoc, LHSExpr, RHSExpr);
  }

  if ((Opc == BO_Shl && LHSExpr->getType()->isIntegralType(Self.getASTContext()))
      || Opc == BO_Shr) {
    StringRef Shift = BinaryOperator::getOpcodeStr(Opc);
    DiagnoseAdditionInShift(Self, OpLoc, LHSExpr, Shift);
    DiagnoseAdditionInShift(Self, OpLoc, RHSExpr, Shift);
  }

  // Warn on overloaded shift operators and comparisons, such as:
  // cout << 5 == 4;
  if (BinaryOperator::isComparisonOp(Opc))
    DiagnoseShiftCompare(Self, OpLoc, LHSExpr, RHSExpr);
}

ExprResult Sema::ActOnBinOp(Scope *S, SourceLocation TokLoc,
                            tok::TokenKind Kind,
                            Expr *LHSExpr, Expr *RHSExpr) {
  BinaryOperatorKind Opc = ConvertTokenKindToBinaryOpcode(Kind);
  assert(LHSExpr && "ActOnBinOp(): missing left expression");
  assert(RHSExpr && "ActOnBinOp(): missing right expression");

  // Emit warnings for tricky precedence issues, e.g. "bitfield & 0x4 == 0"
  DiagnoseBinOpPrecedence(*this, Opc, TokLoc, LHSExpr, RHSExpr);

  return BuildBinOp(S, TokLoc, Opc, LHSExpr, RHSExpr);
}

void Sema::LookupBinOp(Scope *S, SourceLocation OpLoc, BinaryOperatorKind Opc,
                       UnresolvedSetImpl &Functions) {
  OverloadedOperatorKind OverOp = BinaryOperator::getOverloadedOperator(Opc);
  if (OverOp != OO_None && OverOp != OO_Equal)
    LookupOverloadedOperatorName(OverOp, S, Functions);

  // In C++20 onwards, we may have a second operator to look up.
  if (getLangOpts().CPlusPlus20) {
    if (OverloadedOperatorKind ExtraOp = getRewrittenOverloadedOperator(OverOp))
      LookupOverloadedOperatorName(ExtraOp, S, Functions);
  }
}

/// Build an overloaded binary operator expression in the given scope.
static ExprResult BuildOverloadedBinOp(Sema &S, Scope *Sc, SourceLocation OpLoc,
                                       BinaryOperatorKind Opc,
                                       Expr *LHS, Expr *RHS) {
  switch (Opc) {
  case BO_Assign:
    // In the non-overloaded case, we warn about self-assignment (x = x) for
    // both simple assignment and certain compound assignments where algebra
    // tells us the operation yields a constant result.  When the operator is
    // overloaded, we can't do the latter because we don't want to assume that
    // those algebraic identities still apply; for example, a path-building
    // library might use operator/= to append paths.  But it's still reasonable
    // to assume that simple assignment is just moving/copying values around
    // and so self-assignment is likely a bug.
    DiagnoseSelfAssignment(S, LHS, RHS, OpLoc, false);
    [[fallthrough]];
  case BO_DivAssign:
  case BO_RemAssign:
  case BO_SubAssign:
  case BO_AndAssign:
  case BO_OrAssign:
  case BO_XorAssign:
    CheckIdentityFieldAssignment(LHS, RHS, OpLoc, S);
    break;
  default:
    break;
  }

  // Find all of the overloaded operators visible from this point.
  UnresolvedSet<16> Functions;
  S.LookupBinOp(Sc, OpLoc, Opc, Functions);

  // Build the (potentially-overloaded, potentially-dependent)
  // binary operation.
  return S.CreateOverloadedBinOp(OpLoc, Opc, Functions, LHS, RHS);
}

ExprResult Sema::BuildBinOp(Scope *S, SourceLocation OpLoc,
                            BinaryOperatorKind Opc,
                            Expr *LHSExpr, Expr *RHSExpr) {
  ExprResult LHS, RHS;
  std::tie(LHS, RHS) = CorrectDelayedTyposInBinOp(*this, Opc, LHSExpr, RHSExpr);
  if (!LHS.isUsable() || !RHS.isUsable())
    return ExprError();
  LHSExpr = LHS.get();
  RHSExpr = RHS.get();

  // We want to end up calling one of SemaPseudoObject::checkAssignment
  // (if the LHS is a pseudo-object), BuildOverloadedBinOp (if
  // both expressions are overloadable or either is type-dependent),
  // or CreateBuiltinBinOp (in any other case).  We also want to get
  // any placeholder types out of the way.

  // Handle pseudo-objects in the LHS.
  if (const BuiltinType *pty = LHSExpr->getType()->getAsPlaceholderType()) {
    // Assignments with a pseudo-object l-value need special analysis.
    if (pty->getKind() == BuiltinType::PseudoObject &&
        BinaryOperator::isAssignmentOp(Opc))
      return PseudoObject().checkAssignment(S, OpLoc, Opc, LHSExpr, RHSExpr);

    // Don't resolve overloads if the other type is overloadable.
    if (getLangOpts().CPlusPlus && pty->getKind() == BuiltinType::Overload) {
      // We can't actually test that if we still have a placeholder,
      // though.  Fortunately, none of the exceptions we see in that
      // code below are valid when the LHS is an overload set.  Note
      // that an overload set can be dependently-typed, but it never
      // instantiates to having an overloadable type.
      ExprResult resolvedRHS = CheckPlaceholderExpr(RHSExpr);
      if (resolvedRHS.isInvalid()) return ExprError();
      RHSExpr = resolvedRHS.get();

      if (RHSExpr->isTypeDependent() ||
          RHSExpr->getType()->isOverloadableType())
        return BuildOverloadedBinOp(*this, S, OpLoc, Opc, LHSExpr, RHSExpr);
    }

    // If we're instantiating "a.x < b" or "A::x < b" and 'x' names a function
    // template, diagnose the missing 'template' keyword instead of diagnosing
    // an invalid use of a bound member function.
    //
    // Note that "A::x < b" might be valid if 'b' has an overloadable type due
    // to C++1z [over.over]/1.4, but we already checked for that case above.
    if (Opc == BO_LT && inTemplateInstantiation() &&
        (pty->getKind() == BuiltinType::BoundMember ||
         pty->getKind() == BuiltinType::Overload)) {
      auto *OE = dyn_cast<OverloadExpr>(LHSExpr);
      if (OE && !OE->hasTemplateKeyword() && !OE->hasExplicitTemplateArgs() &&
          llvm::any_of(OE->decls(), [](NamedDecl *ND) {
            return isa<FunctionTemplateDecl>(ND);
          })) {
        Diag(OE->getQualifier() ? OE->getQualifierLoc().getBeginLoc()
                                : OE->getNameLoc(),
             diag::err_template_kw_missing)
          << OE->getName().getAsString() << "";
        return ExprError();
      }
    }

    ExprResult LHS = CheckPlaceholderExpr(LHSExpr);
    if (LHS.isInvalid()) return ExprError();
    LHSExpr = LHS.get();
  }

  // Handle pseudo-objects in the RHS.
  if (const BuiltinType *pty = RHSExpr->getType()->getAsPlaceholderType()) {
    // An overload in the RHS can potentially be resolved by the type
    // being assigned to.
    if (Opc == BO_Assign && pty->getKind() == BuiltinType::Overload) {
      if (getLangOpts().CPlusPlus &&
          (LHSExpr->isTypeDependent() || RHSExpr->isTypeDependent() ||
           LHSExpr->getType()->isOverloadableType()))
        return BuildOverloadedBinOp(*this, S, OpLoc, Opc, LHSExpr, RHSExpr);

      return CreateBuiltinBinOp(OpLoc, Opc, LHSExpr, RHSExpr);
    }

    // Don't resolve overloads if the other type is overloadable.
    if (getLangOpts().CPlusPlus && pty->getKind() == BuiltinType::Overload &&
        LHSExpr->getType()->isOverloadableType())
      return BuildOverloadedBinOp(*this, S, OpLoc, Opc, LHSExpr, RHSExpr);

    ExprResult resolvedRHS = CheckPlaceholderExpr(RHSExpr);
    if (!resolvedRHS.isUsable()) return ExprError();
    RHSExpr = resolvedRHS.get();
  }

  if (getLangOpts().CPlusPlus) {
    // Otherwise, build an overloaded op if either expression is type-dependent
    // or has an overloadable type.
    if (LHSExpr->isTypeDependent() || RHSExpr->isTypeDependent() ||
        LHSExpr->getType()->isOverloadableType() ||
        RHSExpr->getType()->isOverloadableType())
      return BuildOverloadedBinOp(*this, S, OpLoc, Opc, LHSExpr, RHSExpr);
  }

  if (getLangOpts().RecoveryAST &&
      (LHSExpr->isTypeDependent() || RHSExpr->isTypeDependent())) {
    assert(!getLangOpts().CPlusPlus);
    assert((LHSExpr->containsErrors() || RHSExpr->containsErrors()) &&
           "Should only occur in error-recovery path.");
    if (BinaryOperator::isCompoundAssignmentOp(Opc))
      // C [6.15.16] p3:
      // An assignment expression has the value of the left operand after the
      // assignment, but is not an lvalue.
      return CompoundAssignOperator::Create(
          Context, LHSExpr, RHSExpr, Opc,
          LHSExpr->getType().getUnqualifiedType(), VK_PRValue, OK_Ordinary,
          OpLoc, CurFPFeatureOverrides());
    QualType ResultType;
    switch (Opc) {
    case BO_Assign:
      ResultType = LHSExpr->getType().getUnqualifiedType();
      break;
    case BO_LT:
    case BO_GT:
    case BO_LE:
    case BO_GE:
    case BO_EQ:
    case BO_NE:
    case BO_LAnd:
    case BO_LOr:
      // These operators have a fixed result type regardless of operands.
      ResultType = Context.IntTy;
      break;
    case BO_Comma:
      ResultType = RHSExpr->getType();
      break;
    default:
      ResultType = Context.DependentTy;
      break;
    }
    return BinaryOperator::Create(Context, LHSExpr, RHSExpr, Opc, ResultType,
                                  VK_PRValue, OK_Ordinary, OpLoc,
                                  CurFPFeatureOverrides());
  }

  // Build a built-in binary operation.
  return CreateBuiltinBinOp(OpLoc, Opc, LHSExpr, RHSExpr);
}

static bool isOverflowingIntegerType(ASTContext &Ctx, QualType T) {
  if (T.isNull() || T->isDependentType())
    return false;

  if (!Ctx.isPromotableIntegerType(T))
    return true;

  return Ctx.getIntWidth(T) >= Ctx.getIntWidth(Ctx.IntTy);
}

ExprResult Sema::CreateBuiltinUnaryOp(SourceLocation OpLoc,
                                      UnaryOperatorKind Opc, Expr *InputExpr,
                                      bool IsAfterAmp) {
  ExprResult Input = InputExpr;
  ExprValueKind VK = VK_PRValue;
  ExprObjectKind OK = OK_Ordinary;
  QualType resultType;
  bool CanOverflow = false;

  bool ConvertHalfVec = false;
  if (getLangOpts().OpenCL) {
    QualType Ty = InputExpr->getType();
    // The only legal unary operation for atomics is '&'.
    if ((Opc != UO_AddrOf && Ty->isAtomicType()) ||
    // OpenCL special types - image, sampler, pipe, and blocks are to be used
    // only with a builtin functions and therefore should be disallowed here.
        (Ty->isImageType() || Ty->isSamplerT() || Ty->isPipeType()
        || Ty->isBlockPointerType())) {
      return ExprError(Diag(OpLoc, diag::err_typecheck_unary_expr)
                       << InputExpr->getType()
                       << Input.get()->getSourceRange());
    }
  }

  if (getLangOpts().HLSL && OpLoc.isValid()) {
    if (Opc == UO_AddrOf)
      return ExprError(Diag(OpLoc, diag::err_hlsl_operator_unsupported) << 0);
    if (Opc == UO_Deref)
      return ExprError(Diag(OpLoc, diag::err_hlsl_operator_unsupported) << 1);
  }

  if (InputExpr->isTypeDependent() &&
      InputExpr->getType()->isSpecificBuiltinType(BuiltinType::Dependent)) {
    resultType = Context.DependentTy;
  } else {
    switch (Opc) {
    case UO_PreInc:
    case UO_PreDec:
    case UO_PostInc:
    case UO_PostDec:
      resultType =
          CheckIncrementDecrementOperand(*this, Input.get(), VK, OK, OpLoc,
                                         Opc == UO_PreInc || Opc == UO_PostInc,
                                         Opc == UO_PreInc || Opc == UO_PreDec);
      CanOverflow = isOverflowingIntegerType(Context, resultType);
      break;
    case UO_AddrOf:
      resultType = CheckAddressOfOperand(Input, OpLoc);
      CheckAddressOfNoDeref(InputExpr);
      RecordModifiableNonNullParam(*this, InputExpr);
      break;
    case UO_Deref: {
      Input = DefaultFunctionArrayLvalueConversion(Input.get());
      if (Input.isInvalid())
        return ExprError();
      resultType =
          CheckIndirectionOperand(*this, Input.get(), VK, OpLoc, IsAfterAmp);
      break;
    }
    case UO_Plus:
    case UO_Minus:
      CanOverflow = Opc == UO_Minus &&
                    isOverflowingIntegerType(Context, Input.get()->getType());
      Input = UsualUnaryConversions(Input.get());
      if (Input.isInvalid())
        return ExprError();
      // Unary plus and minus require promoting an operand of half vector to a
      // float vector and truncating the result back to a half vector. For now,
      // we do this only when HalfArgsAndReturns is set (that is, when the
      // target is arm or arm64).
      ConvertHalfVec = needsConversionOfHalfVec(true, Context, Input.get());

      // If the operand is a half vector, promote it to a float vector.
      if (ConvertHalfVec)
        Input = convertVector(Input.get(), Context.FloatTy, *this);
      resultType = Input.get()->getType();
      if (resultType->isArithmeticType()) // C99 6.5.3.3p1
        break;
      else if (resultType->isVectorType() &&
               // The z vector extensions don't allow + or - with bool vectors.
               (!Context.getLangOpts().ZVector ||
                resultType->castAs<VectorType>()->getVectorKind() !=
                    VectorKind::AltiVecBool))
        break;
      else if (resultType->isSveVLSBuiltinType()) // SVE vectors allow + and -
        break;
      else if (getLangOpts().CPlusPlus && // C++ [expr.unary.op]p6
               Opc == UO_Plus && resultType->isPointerType())
        break;

      return ExprError(Diag(OpLoc, diag::err_typecheck_unary_expr)
                       << resultType << Input.get()->getSourceRange());

    case UO_Not: // bitwise complement
      Input = UsualUnaryConversions(Input.get());
      if (Input.isInvalid())
        return ExprError();
      resultType = Input.get()->getType();
      // C99 6.5.3.3p1. We allow complex int and float as a GCC extension.
      if (resultType->isComplexType() || resultType->isComplexIntegerType())
        // C99 does not support '~' for complex conjugation.
        Diag(OpLoc, diag::ext_integer_complement_complex)
            << resultType << Input.get()->getSourceRange();
      else if (resultType->hasIntegerRepresentation())
        break;
      else if (resultType->isExtVectorType() && Context.getLangOpts().OpenCL) {
        // OpenCL v1.1 s6.3.f: The bitwise operator not (~) does not operate
        // on vector float types.
        QualType T = resultType->castAs<ExtVectorType>()->getElementType();
        if (!T->isIntegerType())
          return ExprError(Diag(OpLoc, diag::err_typecheck_unary_expr)
                           << resultType << Input.get()->getSourceRange());
      } else {
        return ExprError(Diag(OpLoc, diag::err_typecheck_unary_expr)
                         << resultType << Input.get()->getSourceRange());
      }
      break;

    case UO_LNot: // logical negation
      // Unlike +/-/~, integer promotions aren't done here (C99 6.5.3.3p5).
      Input = DefaultFunctionArrayLvalueConversion(Input.get());
      if (Input.isInvalid())
        return ExprError();
      resultType = Input.get()->getType();

      // Though we still have to promote half FP to float...
      if (resultType->isHalfType() && !Context.getLangOpts().NativeHalfType) {
        Input = ImpCastExprToType(Input.get(), Context.FloatTy, CK_FloatingCast)
                    .get();
        resultType = Context.FloatTy;
      }

      // WebAsembly tables can't be used in unary expressions.
      if (resultType->isPointerType() &&
          resultType->getPointeeType().isWebAssemblyReferenceType()) {
        return ExprError(Diag(OpLoc, diag::err_typecheck_unary_expr)
                         << resultType << Input.get()->getSourceRange());
      }

      if (resultType->isScalarType() && !isScopedEnumerationType(resultType)) {
        // C99 6.5.3.3p1: ok, fallthrough;
        if (Context.getLangOpts().CPlusPlus) {
          // C++03 [expr.unary.op]p8, C++0x [expr.unary.op]p9:
          // operand contextually converted to bool.
          Input = ImpCastExprToType(Input.get(), Context.BoolTy,
                                    ScalarTypeToBooleanCastKind(resultType));
        } else if (Context.getLangOpts().OpenCL &&
                   Context.getLangOpts().OpenCLVersion < 120) {
          // OpenCL v1.1 6.3.h: The logical operator not (!) does not
          // operate on scalar float types.
          if (!resultType->isIntegerType() && !resultType->isPointerType())
            return ExprError(Diag(OpLoc, diag::err_typecheck_unary_expr)
                             << resultType << Input.get()->getSourceRange());
        }
      } else if (resultType->isExtVectorType()) {
        if (Context.getLangOpts().OpenCL &&
            Context.getLangOpts().getOpenCLCompatibleVersion() < 120) {
          // OpenCL v1.1 6.3.h: The logical operator not (!) does not
          // operate on vector float types.
          QualType T = resultType->castAs<ExtVectorType>()->getElementType();
          if (!T->isIntegerType())
            return ExprError(Diag(OpLoc, diag::err_typecheck_unary_expr)
                             << resultType << Input.get()->getSourceRange());
        }
        // Vector logical not returns the signed variant of the operand type.
        resultType = GetSignedVectorType(resultType);
        break;
      } else if (Context.getLangOpts().CPlusPlus &&
                 resultType->isVectorType()) {
        const VectorType *VTy = resultType->castAs<VectorType>();
        if (VTy->getVectorKind() != VectorKind::Generic)
          return ExprError(Diag(OpLoc, diag::err_typecheck_unary_expr)
                           << resultType << Input.get()->getSourceRange());

        // Vector logical not returns the signed variant of the operand type.
        resultType = GetSignedVectorType(resultType);
        break;
      } else {
        return ExprError(Diag(OpLoc, diag::err_typecheck_unary_expr)
                         << resultType << Input.get()->getSourceRange());
      }

      // LNot always has type int. C99 6.5.3.3p5.
      // In C++, it's bool. C++ 5.3.1p8
      resultType = Context.getLogicalOperationType();
      break;
    case UO_Real:
    case UO_Imag:
      resultType = CheckRealImagOperand(*this, Input, OpLoc, Opc == UO_Real);
      // _Real maps ordinary l-values into ordinary l-values. _Imag maps
      // ordinary complex l-values to ordinary l-values and all other values to
      // r-values.
      if (Input.isInvalid())
        return ExprError();
      if (Opc == UO_Real || Input.get()->getType()->isAnyComplexType()) {
        if (Input.get()->isGLValue() &&
            Input.get()->getObjectKind() == OK_Ordinary)
          VK = Input.get()->getValueKind();
      } else if (!getLangOpts().CPlusPlus) {
        // In C, a volatile scalar is read by __imag. In C++, it is not.
        Input = DefaultLvalueConversion(Input.get());
      }
      break;
    case UO_Extension:
      resultType = Input.get()->getType();
      VK = Input.get()->getValueKind();
      OK = Input.get()->getObjectKind();
      break;
    case UO_Coawait:
      // It's unnecessary to represent the pass-through operator co_await in the
      // AST; just return the input expression instead.
      assert(!Input.get()->getType()->isDependentType() &&
             "the co_await expression must be non-dependant before "
             "building operator co_await");
      return Input;
    }
  }
  if (resultType.isNull() || Input.isInvalid())
    return ExprError();

  // Check for array bounds violations in the operand of the UnaryOperator,
  // except for the '*' and '&' operators that have to be handled specially
  // by CheckArrayAccess (as there are special cases like &array[arraysize]
  // that are explicitly defined as valid by the standard).
  if (Opc != UO_AddrOf && Opc != UO_Deref)
    CheckArrayAccess(Input.get());

  auto *UO =
      UnaryOperator::Create(Context, Input.get(), Opc, resultType, VK, OK,
                            OpLoc, CanOverflow, CurFPFeatureOverrides());

  if (Opc == UO_Deref && UO->getType()->hasAttr(attr::NoDeref) &&
      !isa<ArrayType>(UO->getType().getDesugaredType(Context)) &&
      !isUnevaluatedContext())
    ExprEvalContexts.back().PossibleDerefs.insert(UO);

  // Convert the result back to a half vector.
  if (ConvertHalfVec)
    return convertVector(UO, Context.HalfTy, *this);
  return UO;
}

bool Sema::isQualifiedMemberAccess(Expr *E) {
  if (DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(E)) {
    if (!DRE->getQualifier())
      return false;

    ValueDecl *VD = DRE->getDecl();
    if (!VD->isCXXClassMember())
      return false;

    if (isa<FieldDecl>(VD) || isa<IndirectFieldDecl>(VD))
      return true;
    if (CXXMethodDecl *Method = dyn_cast<CXXMethodDecl>(VD))
      return Method->isImplicitObjectMemberFunction();

    return false;
  }

  if (UnresolvedLookupExpr *ULE = dyn_cast<UnresolvedLookupExpr>(E)) {
    if (!ULE->getQualifier())
      return false;

    for (NamedDecl *D : ULE->decls()) {
      if (CXXMethodDecl *Method = dyn_cast<CXXMethodDecl>(D)) {
        if (Method->isImplicitObjectMemberFunction())
          return true;
      } else {
        // Overload set does not contain methods.
        break;
      }
    }

    return false;
  }

  return false;
}

ExprResult Sema::BuildUnaryOp(Scope *S, SourceLocation OpLoc,
                              UnaryOperatorKind Opc, Expr *Input,
                              bool IsAfterAmp) {
  // First things first: handle placeholders so that the
  // overloaded-operator check considers the right type.
  if (const BuiltinType *pty = Input->getType()->getAsPlaceholderType()) {
    // Increment and decrement of pseudo-object references.
    if (pty->getKind() == BuiltinType::PseudoObject &&
        UnaryOperator::isIncrementDecrementOp(Opc))
      return PseudoObject().checkIncDec(S, OpLoc, Opc, Input);

    // extension is always a builtin operator.
    if (Opc == UO_Extension)
      return CreateBuiltinUnaryOp(OpLoc, Opc, Input);

    // & gets special logic for several kinds of placeholder.
    // The builtin code knows what to do.
    if (Opc == UO_AddrOf &&
        (pty->getKind() == BuiltinType::Overload ||
         pty->getKind() == BuiltinType::UnknownAny ||
         pty->getKind() == BuiltinType::BoundMember))
      return CreateBuiltinUnaryOp(OpLoc, Opc, Input);

    // Anything else needs to be handled now.
    ExprResult Result = CheckPlaceholderExpr(Input);
    if (Result.isInvalid()) return ExprError();
    Input = Result.get();
  }

  if (getLangOpts().CPlusPlus && Input->getType()->isOverloadableType() &&
      UnaryOperator::getOverloadedOperator(Opc) != OO_None &&
      !(Opc == UO_AddrOf && isQualifiedMemberAccess(Input))) {
    // Find all of the overloaded operators visible from this point.
    UnresolvedSet<16> Functions;
    OverloadedOperatorKind OverOp = UnaryOperator::getOverloadedOperator(Opc);
    if (S && OverOp != OO_None)
      LookupOverloadedOperatorName(OverOp, S, Functions);

    return CreateOverloadedUnaryOp(OpLoc, Opc, Functions, Input);
  }

  return CreateBuiltinUnaryOp(OpLoc, Opc, Input, IsAfterAmp);
}

ExprResult Sema::ActOnUnaryOp(Scope *S, SourceLocation OpLoc, tok::TokenKind Op,
                              Expr *Input, bool IsAfterAmp) {
  return BuildUnaryOp(S, OpLoc, ConvertTokenKindToUnaryOpcode(Op), Input,
                      IsAfterAmp);
}

ExprResult Sema::ActOnAddrLabel(SourceLocation OpLoc, SourceLocation LabLoc,
                                LabelDecl *TheDecl) {
  TheDecl->markUsed(Context);
  // Create the AST node.  The address of a label always has type 'void*'.
  auto *Res = new (Context) AddrLabelExpr(
      OpLoc, LabLoc, TheDecl, Context.getPointerType(Context.VoidTy));

  if (getCurFunction())
    getCurFunction()->AddrLabels.push_back(Res);

  return Res;
}

void Sema::ActOnStartStmtExpr() {
  PushExpressionEvaluationContext(ExprEvalContexts.back().Context);
  // Make sure we diagnose jumping into a statement expression.
  setFunctionHasBranchProtectedScope();
}

void Sema::ActOnStmtExprError() {
  // Note that function is also called by TreeTransform when leaving a
  // StmtExpr scope without rebuilding anything.

  DiscardCleanupsInEvaluationContext();
  PopExpressionEvaluationContext();
}

ExprResult Sema::ActOnStmtExpr(Scope *S, SourceLocation LPLoc, Stmt *SubStmt,
                               SourceLocation RPLoc) {
  return BuildStmtExpr(LPLoc, SubStmt, RPLoc, getTemplateDepth(S));
}

ExprResult Sema::BuildStmtExpr(SourceLocation LPLoc, Stmt *SubStmt,
                               SourceLocation RPLoc, unsigned TemplateDepth) {
  assert(SubStmt && isa<CompoundStmt>(SubStmt) && "Invalid action invocation!");
  CompoundStmt *Compound = cast<CompoundStmt>(SubStmt);

  if (hasAnyUnrecoverableErrorsInThisFunction())
    DiscardCleanupsInEvaluationContext();
  assert(!Cleanup.exprNeedsCleanups() &&
         "cleanups within StmtExpr not correctly bound!");
  PopExpressionEvaluationContext();

  // FIXME: there are a variety of strange constraints to enforce here, for
  // example, it is not possible to goto into a stmt expression apparently.
  // More semantic analysis is needed.

  // If there are sub-stmts in the compound stmt, take the type of the last one
  // as the type of the stmtexpr.
  QualType Ty = Context.VoidTy;
  bool StmtExprMayBindToTemp = false;
  if (!Compound->body_empty()) {
    // For GCC compatibility we get the last Stmt excluding trailing NullStmts.
    if (const auto *LastStmt =
            dyn_cast<ValueStmt>(Compound->getStmtExprResult())) {
      if (const Expr *Value = LastStmt->getExprStmt()) {
        StmtExprMayBindToTemp = true;
        Ty = Value->getType();
      }
    }
  }

  // FIXME: Check that expression type is complete/non-abstract; statement
  // expressions are not lvalues.
  Expr *ResStmtExpr =
      new (Context) StmtExpr(Compound, Ty, LPLoc, RPLoc, TemplateDepth);
  if (StmtExprMayBindToTemp)
    return MaybeBindToTemporary(ResStmtExpr);
  return ResStmtExpr;
}

ExprResult Sema::ActOnStmtExprResult(ExprResult ER) {
  if (ER.isInvalid())
    return ExprError();

  // Do function/array conversion on the last expression, but not
  // lvalue-to-rvalue.  However, initialize an unqualified type.
  ER = DefaultFunctionArrayConversion(ER.get());
  if (ER.isInvalid())
    return ExprError();
  Expr *E = ER.get();

  if (E->isTypeDependent())
    return E;

  // In ARC, if the final expression ends in a consume, splice
  // the consume out and bind it later.  In the alternate case
  // (when dealing with a retainable type), the result
  // initialization will create a produce.  In both cases the
  // result will be +1, and we'll need to balance that out with
  // a bind.
  auto *Cast = dyn_cast<ImplicitCastExpr>(E);
  if (Cast && Cast->getCastKind() == CK_ARCConsumeObject)
    return Cast->getSubExpr();

  // FIXME: Provide a better location for the initialization.
  return PerformCopyInitialization(
      InitializedEntity::InitializeStmtExprResult(
          E->getBeginLoc(), E->getType().getUnqualifiedType()),
      SourceLocation(), E);
}

ExprResult Sema::BuildBuiltinOffsetOf(SourceLocation BuiltinLoc,
                                      TypeSourceInfo *TInfo,
                                      ArrayRef<OffsetOfComponent> Components,
                                      SourceLocation RParenLoc) {
  QualType ArgTy = TInfo->getType();
  bool Dependent = ArgTy->isDependentType();
  SourceRange TypeRange = TInfo->getTypeLoc().getLocalSourceRange();

  // We must have at least one component that refers to the type, and the first
  // one is known to be a field designator.  Verify that the ArgTy represents
  // a struct/union/class.
  if (!Dependent && !ArgTy->isRecordType())
    return ExprError(Diag(BuiltinLoc, diag::err_offsetof_record_type)
                       << ArgTy << TypeRange);

  // Type must be complete per C99 7.17p3 because a declaring a variable
  // with an incomplete type would be ill-formed.
  if (!Dependent
      && RequireCompleteType(BuiltinLoc, ArgTy,
                             diag::err_offsetof_incomplete_type, TypeRange))
    return ExprError();

  bool DidWarnAboutNonPOD = false;
  QualType CurrentType = ArgTy;
  SmallVector<OffsetOfNode, 4> Comps;
  SmallVector<Expr*, 4> Exprs;
  for (const OffsetOfComponent &OC : Components) {
    if (OC.isBrackets) {
      // Offset of an array sub-field.  TODO: Should we allow vector elements?
      if (!CurrentType->isDependentType()) {
        const ArrayType *AT = Context.getAsArrayType(CurrentType);
        if(!AT)
          return ExprError(Diag(OC.LocEnd, diag::err_offsetof_array_type)
                           << CurrentType);
        CurrentType = AT->getElementType();
      } else
        CurrentType = Context.DependentTy;

      ExprResult IdxRval = DefaultLvalueConversion(static_cast<Expr*>(OC.U.E));
      if (IdxRval.isInvalid())
        return ExprError();
      Expr *Idx = IdxRval.get();

      // The expression must be an integral expression.
      // FIXME: An integral constant expression?
      if (!Idx->isTypeDependent() && !Idx->isValueDependent() &&
          !Idx->getType()->isIntegerType())
        return ExprError(
            Diag(Idx->getBeginLoc(), diag::err_typecheck_subscript_not_integer)
            << Idx->getSourceRange());

      // Record this array index.
      Comps.push_back(OffsetOfNode(OC.LocStart, Exprs.size(), OC.LocEnd));
      Exprs.push_back(Idx);
      continue;
    }

    // Offset of a field.
    if (CurrentType->isDependentType()) {
      // We have the offset of a field, but we can't look into the dependent
      // type. Just record the identifier of the field.
      Comps.push_back(OffsetOfNode(OC.LocStart, OC.U.IdentInfo, OC.LocEnd));
      CurrentType = Context.DependentTy;
      continue;
    }

    // We need to have a complete type to look into.
    if (RequireCompleteType(OC.LocStart, CurrentType,
                            diag::err_offsetof_incomplete_type))
      return ExprError();

    // Look for the designated field.
    const RecordType *RC = CurrentType->getAs<RecordType>();
    if (!RC)
      return ExprError(Diag(OC.LocEnd, diag::err_offsetof_record_type)
                       << CurrentType);
    RecordDecl *RD = RC->getDecl();

    // C++ [lib.support.types]p5:
    //   The macro offsetof accepts a restricted set of type arguments in this
    //   International Standard. type shall be a POD structure or a POD union
    //   (clause 9).
    // C++11 [support.types]p4:
    //   If type is not a standard-layout class (Clause 9), the results are
    //   undefined.
    if (CXXRecordDecl *CRD = dyn_cast<CXXRecordDecl>(RD)) {
      bool IsSafe = LangOpts.CPlusPlus11? CRD->isStandardLayout() : CRD->isPOD();
      unsigned DiagID =
        LangOpts.CPlusPlus11? diag::ext_offsetof_non_standardlayout_type
                            : diag::ext_offsetof_non_pod_type;

      if (!IsSafe && !DidWarnAboutNonPOD && !isUnevaluatedContext()) {
        Diag(BuiltinLoc, DiagID)
            << SourceRange(Components[0].LocStart, OC.LocEnd) << CurrentType;
        DidWarnAboutNonPOD = true;
      }
    }

    // Look for the field.
    LookupResult R(*this, OC.U.IdentInfo, OC.LocStart, LookupMemberName);
    LookupQualifiedName(R, RD);
    FieldDecl *MemberDecl = R.getAsSingle<FieldDecl>();
    IndirectFieldDecl *IndirectMemberDecl = nullptr;
    if (!MemberDecl) {
      if ((IndirectMemberDecl = R.getAsSingle<IndirectFieldDecl>()))
        MemberDecl = IndirectMemberDecl->getAnonField();
    }

    if (!MemberDecl) {
      // Lookup could be ambiguous when looking up a placeholder variable
      // __builtin_offsetof(S, _).
      // In that case we would already have emitted a diagnostic
      if (!R.isAmbiguous())
        Diag(BuiltinLoc, diag::err_no_member)
            << OC.U.IdentInfo << RD << SourceRange(OC.LocStart, OC.LocEnd);
      return ExprError();
    }

    // C99 7.17p3:
    //   (If the specified member is a bit-field, the behavior is undefined.)
    //
    // We diagnose this as an error.
    if (MemberDecl->isBitField()) {
      Diag(OC.LocEnd, diag::err_offsetof_bitfield)
        << MemberDecl->getDeclName()
        << SourceRange(BuiltinLoc, RParenLoc);
      Diag(MemberDecl->getLocation(), diag::note_bitfield_decl);
      return ExprError();
    }

    RecordDecl *Parent = MemberDecl->getParent();
    if (IndirectMemberDecl)
      Parent = cast<RecordDecl>(IndirectMemberDecl->getDeclContext());

    // If the member was found in a base class, introduce OffsetOfNodes for
    // the base class indirections.
    CXXBasePaths Paths;
    if (IsDerivedFrom(OC.LocStart, CurrentType, Context.getTypeDeclType(Parent),
                      Paths)) {
      if (Paths.getDetectedVirtual()) {
        Diag(OC.LocEnd, diag::err_offsetof_field_of_virtual_base)
          << MemberDecl->getDeclName()
          << SourceRange(BuiltinLoc, RParenLoc);
        return ExprError();
      }

      CXXBasePath &Path = Paths.front();
      for (const CXXBasePathElement &B : Path)
        Comps.push_back(OffsetOfNode(B.Base));
    }

    if (IndirectMemberDecl) {
      for (auto *FI : IndirectMemberDecl->chain()) {
        assert(isa<FieldDecl>(FI));
        Comps.push_back(OffsetOfNode(OC.LocStart,
                                     cast<FieldDecl>(FI), OC.LocEnd));
      }
    } else
      Comps.push_back(OffsetOfNode(OC.LocStart, MemberDecl, OC.LocEnd));

    CurrentType = MemberDecl->getType().getNonReferenceType();
  }

  return OffsetOfExpr::Create(Context, Context.getSizeType(), BuiltinLoc, TInfo,
                              Comps, Exprs, RParenLoc);
}

ExprResult Sema::ActOnBuiltinOffsetOf(Scope *S,
                                      SourceLocation BuiltinLoc,
                                      SourceLocation TypeLoc,
                                      ParsedType ParsedArgTy,
                                      ArrayRef<OffsetOfComponent> Components,
                                      SourceLocation RParenLoc) {

  TypeSourceInfo *ArgTInfo;
  QualType ArgTy = GetTypeFromParser(ParsedArgTy, &ArgTInfo);
  if (ArgTy.isNull())
    return ExprError();

  if (!ArgTInfo)
    ArgTInfo = Context.getTrivialTypeSourceInfo(ArgTy, TypeLoc);

  return BuildBuiltinOffsetOf(BuiltinLoc, ArgTInfo, Components, RParenLoc);
}


ExprResult Sema::ActOnChooseExpr(SourceLocation BuiltinLoc,
                                 Expr *CondExpr,
                                 Expr *LHSExpr, Expr *RHSExpr,
                                 SourceLocation RPLoc) {
  assert((CondExpr && LHSExpr && RHSExpr) && "Missing type argument(s)");

  ExprValueKind VK = VK_PRValue;
  ExprObjectKind OK = OK_Ordinary;
  QualType resType;
  bool CondIsTrue = false;
  if (CondExpr->isTypeDependent() || CondExpr->isValueDependent()) {
    resType = Context.DependentTy;
  } else {
    // The conditional expression is required to be a constant expression.
    llvm::APSInt condEval(32);
    ExprResult CondICE = VerifyIntegerConstantExpression(
        CondExpr, &condEval, diag::err_typecheck_choose_expr_requires_constant);
    if (CondICE.isInvalid())
      return ExprError();
    CondExpr = CondICE.get();
    CondIsTrue = condEval.getZExtValue();

    // If the condition is > zero, then the AST type is the same as the LHSExpr.
    Expr *ActiveExpr = CondIsTrue ? LHSExpr : RHSExpr;

    resType = ActiveExpr->getType();
    VK = ActiveExpr->getValueKind();
    OK = ActiveExpr->getObjectKind();
  }

  return new (Context) ChooseExpr(BuiltinLoc, CondExpr, LHSExpr, RHSExpr,
                                  resType, VK, OK, RPLoc, CondIsTrue);
}

//===----------------------------------------------------------------------===//
// Clang Extensions.
//===----------------------------------------------------------------------===//

void Sema::ActOnBlockStart(SourceLocation CaretLoc, Scope *CurScope) {
  BlockDecl *Block = BlockDecl::Create(Context, CurContext, CaretLoc);

  if (LangOpts.CPlusPlus) {
    MangleNumberingContext *MCtx;
    Decl *ManglingContextDecl;
    std::tie(MCtx, ManglingContextDecl) =
        getCurrentMangleNumberContext(Block->getDeclContext());
    if (MCtx) {
      unsigned ManglingNumber = MCtx->getManglingNumber(Block);
      Block->setBlockMangling(ManglingNumber, ManglingContextDecl);
    }
  }

  PushBlockScope(CurScope, Block);
  CurContext->addDecl(Block);
  if (CurScope)
    PushDeclContext(CurScope, Block);
  else
    CurContext = Block;

  getCurBlock()->HasImplicitReturnType = true;

  // Enter a new evaluation context to insulate the block from any
  // cleanups from the enclosing full-expression.
  PushExpressionEvaluationContext(
      ExpressionEvaluationContext::PotentiallyEvaluated);
}

void Sema::ActOnBlockArguments(SourceLocation CaretLoc, Declarator &ParamInfo,
                               Scope *CurScope) {
  assert(ParamInfo.getIdentifier() == nullptr &&
         "block-id should have no identifier!");
  assert(ParamInfo.getContext() == DeclaratorContext::BlockLiteral);
  BlockScopeInfo *CurBlock = getCurBlock();

  TypeSourceInfo *Sig = GetTypeForDeclarator(ParamInfo);
  QualType T = Sig->getType();

  // FIXME: We should allow unexpanded parameter packs here, but that would,
  // in turn, make the block expression contain unexpanded parameter packs.
  if (DiagnoseUnexpandedParameterPack(CaretLoc, Sig, UPPC_Block)) {
    // Drop the parameters.
    FunctionProtoType::ExtProtoInfo EPI;
    EPI.HasTrailingReturn = false;
    EPI.TypeQuals.addConst();
    T = Context.getFunctionType(Context.DependentTy, std::nullopt, EPI);
    Sig = Context.getTrivialTypeSourceInfo(T);
  }

  // GetTypeForDeclarator always produces a function type for a block
  // literal signature.  Furthermore, it is always a FunctionProtoType
  // unless the function was written with a typedef.
  assert(T->isFunctionType() &&
         "GetTypeForDeclarator made a non-function block signature");

  // Look for an explicit signature in that function type.
  FunctionProtoTypeLoc ExplicitSignature;

  if ((ExplicitSignature = Sig->getTypeLoc()
                               .getAsAdjusted<FunctionProtoTypeLoc>())) {

    // Check whether that explicit signature was synthesized by
    // GetTypeForDeclarator.  If so, don't save that as part of the
    // written signature.
    if (ExplicitSignature.getLocalRangeBegin() ==
        ExplicitSignature.getLocalRangeEnd()) {
      // This would be much cheaper if we stored TypeLocs instead of
      // TypeSourceInfos.
      TypeLoc Result = ExplicitSignature.getReturnLoc();
      unsigned Size = Result.getFullDataSize();
      Sig = Context.CreateTypeSourceInfo(Result.getType(), Size);
      Sig->getTypeLoc().initializeFullCopy(Result, Size);

      ExplicitSignature = FunctionProtoTypeLoc();
    }
  }

  CurBlock->TheDecl->setSignatureAsWritten(Sig);
  CurBlock->FunctionType = T;

  const auto *Fn = T->castAs<FunctionType>();
  QualType RetTy = Fn->getReturnType();
  bool isVariadic =
      (isa<FunctionProtoType>(Fn) && cast<FunctionProtoType>(Fn)->isVariadic());

  CurBlock->TheDecl->setIsVariadic(isVariadic);

  // Context.DependentTy is used as a placeholder for a missing block
  // return type.  TODO:  what should we do with declarators like:
  //   ^ * { ... }
  // If the answer is "apply template argument deduction"....
  if (RetTy != Context.DependentTy) {
    CurBlock->ReturnType = RetTy;
    CurBlock->TheDecl->setBlockMissingReturnType(false);
    CurBlock->HasImplicitReturnType = false;
  }

  // Push block parameters from the declarator if we had them.
  SmallVector<ParmVarDecl*, 8> Params;
  if (ExplicitSignature) {
    for (unsigned I = 0, E = ExplicitSignature.getNumParams(); I != E; ++I) {
      ParmVarDecl *Param = ExplicitSignature.getParam(I);
      if (Param->getIdentifier() == nullptr && !Param->isImplicit() &&
          !Param->isInvalidDecl() && !getLangOpts().CPlusPlus) {
        // Diagnose this as an extension in C17 and earlier.
        if (!getLangOpts().C23)
          Diag(Param->getLocation(), diag::ext_parameter_name_omitted_c23);
      }
      Params.push_back(Param);
    }

  // Fake up parameter variables if we have a typedef, like
  //   ^ fntype { ... }
  } else if (const FunctionProtoType *Fn = T->getAs<FunctionProtoType>()) {
    for (const auto &I : Fn->param_types()) {
      ParmVarDecl *Param = BuildParmVarDeclForTypedef(
          CurBlock->TheDecl, ParamInfo.getBeginLoc(), I);
      Params.push_back(Param);
    }
  }

  // Set the parameters on the block decl.
  if (!Params.empty()) {
    CurBlock->TheDecl->setParams(Params);
    CheckParmsForFunctionDef(CurBlock->TheDecl->parameters(),
                             /*CheckParameterNames=*/false);
  }

  // Finally we can process decl attributes.
  ProcessDeclAttributes(CurScope, CurBlock->TheDecl, ParamInfo);

  // Put the parameter variables in scope.
  for (auto *AI : CurBlock->TheDecl->parameters()) {
    AI->setOwningFunction(CurBlock->TheDecl);

    // If this has an identifier, add it to the scope stack.
    if (AI->getIdentifier()) {
      CheckShadow(CurBlock->TheScope, AI);

      PushOnScopeChains(AI, CurBlock->TheScope);
    }

    if (AI->isInvalidDecl())
      CurBlock->TheDecl->setInvalidDecl();
  }
}

void Sema::ActOnBlockError(SourceLocation CaretLoc, Scope *CurScope) {
  // Leave the expression-evaluation context.
  DiscardCleanupsInEvaluationContext();
  PopExpressionEvaluationContext();

  // Pop off CurBlock, handle nested blocks.
  PopDeclContext();
  PopFunctionScopeInfo();
}

ExprResult Sema::ActOnBlockStmtExpr(SourceLocation CaretLoc,
                                    Stmt *Body, Scope *CurScope) {
  // If blocks are disabled, emit an error.
  if (!LangOpts.Blocks)
    Diag(CaretLoc, diag::err_blocks_disable) << LangOpts.OpenCL;

  // Leave the expression-evaluation context.
  if (hasAnyUnrecoverableErrorsInThisFunction())
    DiscardCleanupsInEvaluationContext();
  assert(!Cleanup.exprNeedsCleanups() &&
         "cleanups within block not correctly bound!");
  PopExpressionEvaluationContext();

  BlockScopeInfo *BSI = cast<BlockScopeInfo>(FunctionScopes.back());
  BlockDecl *BD = BSI->TheDecl;

  if (BSI->HasImplicitReturnType)
    deduceClosureReturnType(*BSI);

  QualType RetTy = Context.VoidTy;
  if (!BSI->ReturnType.isNull())
    RetTy = BSI->ReturnType;

  bool NoReturn = BD->hasAttr<NoReturnAttr>();
  QualType BlockTy;

  // If the user wrote a function type in some form, try to use that.
  if (!BSI->FunctionType.isNull()) {
    const FunctionType *FTy = BSI->FunctionType->castAs<FunctionType>();

    FunctionType::ExtInfo Ext = FTy->getExtInfo();
    if (NoReturn && !Ext.getNoReturn()) Ext = Ext.withNoReturn(true);

    // Turn protoless block types into nullary block types.
    if (isa<FunctionNoProtoType>(FTy)) {
      FunctionProtoType::ExtProtoInfo EPI;
      EPI.ExtInfo = Ext;
      BlockTy = Context.getFunctionType(RetTy, std::nullopt, EPI);

      // Otherwise, if we don't need to change anything about the function type,
      // preserve its sugar structure.
    } else if (FTy->getReturnType() == RetTy &&
               (!NoReturn || FTy->getNoReturnAttr())) {
      BlockTy = BSI->FunctionType;

    // Otherwise, make the minimal modifications to the function type.
    } else {
      const FunctionProtoType *FPT = cast<FunctionProtoType>(FTy);
      FunctionProtoType::ExtProtoInfo EPI = FPT->getExtProtoInfo();
      EPI.TypeQuals = Qualifiers();
      EPI.ExtInfo = Ext;
      BlockTy = Context.getFunctionType(RetTy, FPT->getParamTypes(), EPI);
    }

  // If we don't have a function type, just build one from nothing.
  } else {
    FunctionProtoType::ExtProtoInfo EPI;
    EPI.ExtInfo = FunctionType::ExtInfo().withNoReturn(NoReturn);
    BlockTy = Context.getFunctionType(RetTy, std::nullopt, EPI);
  }

  DiagnoseUnusedParameters(BD->parameters());
  BlockTy = Context.getBlockPointerType(BlockTy);

  // If needed, diagnose invalid gotos and switches in the block.
  if (getCurFunction()->NeedsScopeChecking() &&
      !PP.isCodeCompletionEnabled())
    DiagnoseInvalidJumps(cast<CompoundStmt>(Body));

  BD->setBody(cast<CompoundStmt>(Body));

  if (Body && getCurFunction()->HasPotentialAvailabilityViolations)
    DiagnoseUnguardedAvailabilityViolations(BD);

  // Try to apply the named return value optimization. We have to check again
  // if we can do this, though, because blocks keep return statements around
  // to deduce an implicit return type.
  if (getLangOpts().CPlusPlus && RetTy->isRecordType() &&
      !BD->isDependentContext())
    computeNRVO(Body, BSI);

  if (RetTy.hasNonTrivialToPrimitiveDestructCUnion() ||
      RetTy.hasNonTrivialToPrimitiveCopyCUnion())
    checkNonTrivialCUnion(RetTy, BD->getCaretLocation(), NTCUC_FunctionReturn,
                          NTCUK_Destruct|NTCUK_Copy);

  PopDeclContext();

  // Set the captured variables on the block.
  SmallVector<BlockDecl::Capture, 4> Captures;
  for (Capture &Cap : BSI->Captures) {
    if (Cap.isInvalid() || Cap.isThisCapture())
      continue;
    // Cap.getVariable() is always a VarDecl because
    // blocks cannot capture structured bindings or other ValueDecl kinds.
    auto *Var = cast<VarDecl>(Cap.getVariable());
    Expr *CopyExpr = nullptr;
    if (getLangOpts().CPlusPlus && Cap.isCopyCapture()) {
      if (const RecordType *Record =
              Cap.getCaptureType()->getAs<RecordType>()) {
        // The capture logic needs the destructor, so make sure we mark it.
        // Usually this is unnecessary because most local variables have
        // their destructors marked at declaration time, but parameters are
        // an exception because it's technically only the call site that
        // actually requires the destructor.
        if (isa<ParmVarDecl>(Var))
          FinalizeVarWithDestructor(Var, Record);

        // Enter a separate potentially-evaluated context while building block
        // initializers to isolate their cleanups from those of the block
        // itself.
        // FIXME: Is this appropriate even when the block itself occurs in an
        // unevaluated operand?
        EnterExpressionEvaluationContext EvalContext(
            *this, ExpressionEvaluationContext::PotentiallyEvaluated);

        SourceLocation Loc = Cap.getLocation();

        ExprResult Result = BuildDeclarationNameExpr(
            CXXScopeSpec(), DeclarationNameInfo(Var->getDeclName(), Loc), Var);

        // According to the blocks spec, the capture of a variable from
        // the stack requires a const copy constructor.  This is not true
        // of the copy/move done to move a __block variable to the heap.
        if (!Result.isInvalid() &&
            !Result.get()->getType().isConstQualified()) {
          Result = ImpCastExprToType(Result.get(),
                                     Result.get()->getType().withConst(),
                                     CK_NoOp, VK_LValue);
        }

        if (!Result.isInvalid()) {
          Result = PerformCopyInitialization(
              InitializedEntity::InitializeBlock(Var->getLocation(),
                                                 Cap.getCaptureType()),
              Loc, Result.get());
        }

        // Build a full-expression copy expression if initialization
        // succeeded and used a non-trivial constructor.  Recover from
        // errors by pretending that the copy isn't necessary.
        if (!Result.isInvalid() &&
            !cast<CXXConstructExpr>(Result.get())->getConstructor()
                ->isTrivial()) {
          Result = MaybeCreateExprWithCleanups(Result);
          CopyExpr = Result.get();
        }
      }
    }

    BlockDecl::Capture NewCap(Var, Cap.isBlockCapture(), Cap.isNested(),
                              CopyExpr);
    Captures.push_back(NewCap);
  }
  BD->setCaptures(Context, Captures, BSI->CXXThisCaptureIndex != 0);

  // Pop the block scope now but keep it alive to the end of this function.
  AnalysisBasedWarnings::Policy WP = AnalysisWarnings.getDefaultPolicy();
  PoppedFunctionScopePtr ScopeRAII = PopFunctionScopeInfo(&WP, BD, BlockTy);

  BlockExpr *Result = new (Context) BlockExpr(BD, BlockTy);

  // If the block isn't obviously global, i.e. it captures anything at
  // all, then we need to do a few things in the surrounding context:
  if (Result->getBlockDecl()->hasCaptures()) {
    // First, this expression has a new cleanup object.
    ExprCleanupObjects.push_back(Result->getBlockDecl());
    Cleanup.setExprNeedsCleanups(true);

    // It also gets a branch-protected scope if any of the captured
    // variables needs destruction.
    for (const auto &CI : Result->getBlockDecl()->captures()) {
      const VarDecl *var = CI.getVariable();
      if (var->getType().isDestructedType() != QualType::DK_none) {
        setFunctionHasBranchProtectedScope();
        break;
      }
    }
  }

  if (getCurFunction())
    getCurFunction()->addBlock(BD);

  if (BD->isInvalidDecl())
    return CreateRecoveryExpr(Result->getBeginLoc(), Result->getEndLoc(),
                              {Result}, Result->getType());
  return Result;
}

ExprResult Sema::ActOnVAArg(SourceLocation BuiltinLoc, Expr *E, ParsedType Ty,
                            SourceLocation RPLoc) {
  TypeSourceInfo *TInfo;
  GetTypeFromParser(Ty, &TInfo);
  return BuildVAArgExpr(BuiltinLoc, E, TInfo, RPLoc);
}

ExprResult Sema::BuildVAArgExpr(SourceLocation BuiltinLoc,
                                Expr *E, TypeSourceInfo *TInfo,
                                SourceLocation RPLoc) {
  Expr *OrigExpr = E;
  bool IsMS = false;

  // CUDA device code does not support varargs.
  if (getLangOpts().CUDA && getLangOpts().CUDAIsDevice) {
    if (const FunctionDecl *F = dyn_cast<FunctionDecl>(CurContext)) {
      CUDAFunctionTarget T = CUDA().IdentifyTarget(F);
      if (T == CUDAFunctionTarget::Global || T == CUDAFunctionTarget::Device ||
          T == CUDAFunctionTarget::HostDevice)
        return ExprError(Diag(E->getBeginLoc(), diag::err_va_arg_in_device));
    }
  }

  // NVPTX does not support va_arg expression.
  if (getLangOpts().OpenMP && getLangOpts().OpenMPIsTargetDevice &&
      Context.getTargetInfo().getTriple().isNVPTX())
    targetDiag(E->getBeginLoc(), diag::err_va_arg_in_device);

  // It might be a __builtin_ms_va_list. (But don't ever mark a va_arg()
  // as Microsoft ABI on an actual Microsoft platform, where
  // __builtin_ms_va_list and __builtin_va_list are the same.)
  if (!E->isTypeDependent() && Context.getTargetInfo().hasBuiltinMSVaList() &&
      Context.getTargetInfo().getBuiltinVaListKind() != TargetInfo::CharPtrBuiltinVaList) {
    QualType MSVaListType = Context.getBuiltinMSVaListType();
    if (Context.hasSameType(MSVaListType, E->getType())) {
      if (CheckForModifiableLvalue(E, BuiltinLoc, *this))
        return ExprError();
      IsMS = true;
    }
  }

  // Get the va_list type
  QualType VaListType = Context.getBuiltinVaListType();
  if (!IsMS) {
    if (VaListType->isArrayType()) {
      // Deal with implicit array decay; for example, on x86-64,
      // va_list is an array, but it's supposed to decay to
      // a pointer for va_arg.
      VaListType = Context.getArrayDecayedType(VaListType);
      // Make sure the input expression also decays appropriately.
      ExprResult Result = UsualUnaryConversions(E);
      if (Result.isInvalid())
        return ExprError();
      E = Result.get();
    } else if (VaListType->isRecordType() && getLangOpts().CPlusPlus) {
      // If va_list is a record type and we are compiling in C++ mode,
      // check the argument using reference binding.
      InitializedEntity Entity = InitializedEntity::InitializeParameter(
          Context, Context.getLValueReferenceType(VaListType), false);
      ExprResult Init = PerformCopyInitialization(Entity, SourceLocation(), E);
      if (Init.isInvalid())
        return ExprError();
      E = Init.getAs<Expr>();
    } else {
      // Otherwise, the va_list argument must be an l-value because
      // it is modified by va_arg.
      if (!E->isTypeDependent() &&
          CheckForModifiableLvalue(E, BuiltinLoc, *this))
        return ExprError();
    }
  }

  if (!IsMS && !E->isTypeDependent() &&
      !Context.hasSameType(VaListType, E->getType()))
    return ExprError(
        Diag(E->getBeginLoc(),
             diag::err_first_argument_to_va_arg_not_of_type_va_list)
        << OrigExpr->getType() << E->getSourceRange());

  if (!TInfo->getType()->isDependentType()) {
    if (RequireCompleteType(TInfo->getTypeLoc().getBeginLoc(), TInfo->getType(),
                            diag::err_second_parameter_to_va_arg_incomplete,
                            TInfo->getTypeLoc()))
      return ExprError();

    if (RequireNonAbstractType(TInfo->getTypeLoc().getBeginLoc(),
                               TInfo->getType(),
                               diag::err_second_parameter_to_va_arg_abstract,
                               TInfo->getTypeLoc()))
      return ExprError();

    if (!TInfo->getType().isPODType(Context)) {
      Diag(TInfo->getTypeLoc().getBeginLoc(),
           TInfo->getType()->isObjCLifetimeType()
             ? diag::warn_second_parameter_to_va_arg_ownership_qualified
             : diag::warn_second_parameter_to_va_arg_not_pod)
        << TInfo->getType()
        << TInfo->getTypeLoc().getSourceRange();
    }

    // Check for va_arg where arguments of the given type will be promoted
    // (i.e. this va_arg is guaranteed to have undefined behavior).
    QualType PromoteType;
    if (Context.isPromotableIntegerType(TInfo->getType())) {
      PromoteType = Context.getPromotedIntegerType(TInfo->getType());
      // [cstdarg.syn]p1 defers the C++ behavior to what the C standard says,
      // and C23 7.16.1.1p2 says, in part:
      //   If type is not compatible with the type of the actual next argument
      //   (as promoted according to the default argument promotions), the
      //   behavior is undefined, except for the following cases:
      //     - both types are pointers to qualified or unqualified versions of
      //       compatible types;
      //     - one type is compatible with a signed integer type, the other
      //       type is compatible with the corresponding unsigned integer type,
      //       and the value is representable in both types;
      //     - one type is pointer to qualified or unqualified void and the
      //       other is a pointer to a qualified or unqualified character type;
      //     - or, the type of the next argument is nullptr_t and type is a
      //       pointer type that has the same representation and alignment
      //       requirements as a pointer to a character type.
      // Given that type compatibility is the primary requirement (ignoring
      // qualifications), you would think we could call typesAreCompatible()
      // directly to test this. However, in C++, that checks for *same type*,
      // which causes false positives when passing an enumeration type to
      // va_arg. Instead, get the underlying type of the enumeration and pass
      // that.
      QualType UnderlyingType = TInfo->getType();
      if (const auto *ET = UnderlyingType->getAs<EnumType>())
        UnderlyingType = ET->getDecl()->getIntegerType();
      if (Context.typesAreCompatible(PromoteType, UnderlyingType,
                                     /*CompareUnqualified*/ true))
        PromoteType = QualType();

      // If the types are still not compatible, we need to test whether the
      // promoted type and the underlying type are the same except for
      // signedness. Ask the AST for the correctly corresponding type and see
      // if that's compatible.
      if (!PromoteType.isNull() && !UnderlyingType->isBooleanType() &&
          PromoteType->isUnsignedIntegerType() !=
              UnderlyingType->isUnsignedIntegerType()) {
        UnderlyingType =
            UnderlyingType->isUnsignedIntegerType()
                ? Context.getCorrespondingSignedType(UnderlyingType)
                : Context.getCorrespondingUnsignedType(UnderlyingType);
        if (Context.typesAreCompatible(PromoteType, UnderlyingType,
                                       /*CompareUnqualified*/ true))
          PromoteType = QualType();
      }
    }
    if (TInfo->getType()->isSpecificBuiltinType(BuiltinType::Float))
      PromoteType = Context.DoubleTy;
    if (!PromoteType.isNull())
      DiagRuntimeBehavior(TInfo->getTypeLoc().getBeginLoc(), E,
                  PDiag(diag::warn_second_parameter_to_va_arg_never_compatible)
                          << TInfo->getType()
                          << PromoteType
                          << TInfo->getTypeLoc().getSourceRange());
  }

  QualType T = TInfo->getType().getNonLValueExprType(Context);
  return new (Context) VAArgExpr(BuiltinLoc, E, TInfo, RPLoc, T, IsMS);
}

ExprResult Sema::ActOnGNUNullExpr(SourceLocation TokenLoc) {
  // The type of __null will be int or long, depending on the size of
  // pointers on the target.
  QualType Ty;
  unsigned pw = Context.getTargetInfo().getPointerWidth(LangAS::Default);
  if (pw == Context.getTargetInfo().getIntWidth())
    Ty = Context.IntTy;
  else if (pw == Context.getTargetInfo().getLongWidth())
    Ty = Context.LongTy;
  else if (pw == Context.getTargetInfo().getLongLongWidth())
    Ty = Context.LongLongTy;
  else {
    llvm_unreachable("I don't know size of pointer!");
  }

  return new (Context) GNUNullExpr(Ty, TokenLoc);
}

static CXXRecordDecl *LookupStdSourceLocationImpl(Sema &S, SourceLocation Loc) {
  CXXRecordDecl *ImplDecl = nullptr;

  // Fetch the std::source_location::__impl decl.
  if (NamespaceDecl *Std = S.getStdNamespace()) {
    LookupResult ResultSL(S, &S.PP.getIdentifierTable().get("source_location"),
                          Loc, Sema::LookupOrdinaryName);
    if (S.LookupQualifiedName(ResultSL, Std)) {
      if (auto *SLDecl = ResultSL.getAsSingle<RecordDecl>()) {
        LookupResult ResultImpl(S, &S.PP.getIdentifierTable().get("__impl"),
                                Loc, Sema::LookupOrdinaryName);
        if ((SLDecl->isCompleteDefinition() || SLDecl->isBeingDefined()) &&
            S.LookupQualifiedName(ResultImpl, SLDecl)) {
          ImplDecl = ResultImpl.getAsSingle<CXXRecordDecl>();
        }
      }
    }
  }

  if (!ImplDecl || !ImplDecl->isCompleteDefinition()) {
    S.Diag(Loc, diag::err_std_source_location_impl_not_found);
    return nullptr;
  }

  // Verify that __impl is a trivial struct type, with no base classes, and with
  // only the four expected fields.
  if (ImplDecl->isUnion() || !ImplDecl->isStandardLayout() ||
      ImplDecl->getNumBases() != 0) {
    S.Diag(Loc, diag::err_std_source_location_impl_malformed);
    return nullptr;
  }

  unsigned Count = 0;
  for (FieldDecl *F : ImplDecl->fields()) {
    StringRef Name = F->getName();

    if (Name == "_M_file_name") {
      if (F->getType() !=
          S.Context.getPointerType(S.Context.CharTy.withConst()))
        break;
      Count++;
    } else if (Name == "_M_function_name") {
      if (F->getType() !=
          S.Context.getPointerType(S.Context.CharTy.withConst()))
        break;
      Count++;
    } else if (Name == "_M_line") {
      if (!F->getType()->isIntegerType())
        break;
      Count++;
    } else if (Name == "_M_column") {
      if (!F->getType()->isIntegerType())
        break;
      Count++;
    } else {
      Count = 100; // invalid
      break;
    }
  }
  if (Count != 4) {
    S.Diag(Loc, diag::err_std_source_location_impl_malformed);
    return nullptr;
  }

  return ImplDecl;
}

ExprResult Sema::ActOnSourceLocExpr(SourceLocIdentKind Kind,
                                    SourceLocation BuiltinLoc,
                                    SourceLocation RPLoc) {
  QualType ResultTy;
  switch (Kind) {
  case SourceLocIdentKind::File:
  case SourceLocIdentKind::FileName:
  case SourceLocIdentKind::Function:
  case SourceLocIdentKind::FuncSig: {
    QualType ArrTy = Context.getStringLiteralArrayType(Context.CharTy, 0);
    ResultTy =
        Context.getPointerType(ArrTy->getAsArrayTypeUnsafe()->getElementType());
    break;
  }
  case SourceLocIdentKind::Line:
  case SourceLocIdentKind::Column:
    ResultTy = Context.UnsignedIntTy;
    break;
  case SourceLocIdentKind::SourceLocStruct:
    if (!StdSourceLocationImplDecl) {
      StdSourceLocationImplDecl =
          LookupStdSourceLocationImpl(*this, BuiltinLoc);
      if (!StdSourceLocationImplDecl)
        return ExprError();
    }
    ResultTy = Context.getPointerType(
        Context.getRecordType(StdSourceLocationImplDecl).withConst());
    break;
  }

  return BuildSourceLocExpr(Kind, ResultTy, BuiltinLoc, RPLoc, CurContext);
}

ExprResult Sema::BuildSourceLocExpr(SourceLocIdentKind Kind, QualType ResultTy,
                                    SourceLocation BuiltinLoc,
                                    SourceLocation RPLoc,
                                    DeclContext *ParentContext) {
  return new (Context)
      SourceLocExpr(Context, Kind, ResultTy, BuiltinLoc, RPLoc, ParentContext);
}

ExprResult Sema::ActOnEmbedExpr(SourceLocation EmbedKeywordLoc,
                                StringLiteral *BinaryData) {
  EmbedDataStorage *Data = new (Context) EmbedDataStorage;
  Data->BinaryData = BinaryData;
  return new (Context)
      EmbedExpr(Context, EmbedKeywordLoc, Data, /*NumOfElements=*/0,
                Data->getDataElementCount());
}

static bool maybeDiagnoseAssignmentToFunction(Sema &S, QualType DstType,
                                              const Expr *SrcExpr) {
  if (!DstType->isFunctionPointerType() ||
      !SrcExpr->getType()->isFunctionType())
    return false;

  auto *DRE = dyn_cast<DeclRefExpr>(SrcExpr->IgnoreParenImpCasts());
  if (!DRE)
    return false;

  auto *FD = dyn_cast<FunctionDecl>(DRE->getDecl());
  if (!FD)
    return false;

  return !S.checkAddressOfFunctionIsAvailable(FD,
                                              /*Complain=*/true,
                                              SrcExpr->getBeginLoc());
}

bool Sema::DiagnoseAssignmentResult(AssignConvertType ConvTy,
                                    SourceLocation Loc,
                                    QualType DstType, QualType SrcType,
                                    Expr *SrcExpr, AssignmentAction Action,
                                    bool *Complained) {
  if (Complained)
    *Complained = false;

  // Decode the result (notice that AST's are still created for extensions).
  bool CheckInferredResultType = false;
  bool isInvalid = false;
  unsigned DiagKind = 0;
  ConversionFixItGenerator ConvHints;
  bool MayHaveConvFixit = false;
  bool MayHaveFunctionDiff = false;
  const ObjCInterfaceDecl *IFace = nullptr;
  const ObjCProtocolDecl *PDecl = nullptr;

  switch (ConvTy) {
  case Compatible:
      DiagnoseAssignmentEnum(DstType, SrcType, SrcExpr);
      return false;

  case PointerToInt:
    if (getLangOpts().CPlusPlus) {
      DiagKind = diag::err_typecheck_convert_pointer_int;
      isInvalid = true;
    } else {
      DiagKind = diag::ext_typecheck_convert_pointer_int;
    }
    ConvHints.tryToFixConversion(SrcExpr, SrcType, DstType, *this);
    MayHaveConvFixit = true;
    break;
  case IntToPointer:
    if (getLangOpts().CPlusPlus) {
      DiagKind = diag::err_typecheck_convert_int_pointer;
      isInvalid = true;
    } else {
      DiagKind = diag::ext_typecheck_convert_int_pointer;
    }
    ConvHints.tryToFixConversion(SrcExpr, SrcType, DstType, *this);
    MayHaveConvFixit = true;
    break;
  case IncompatibleFunctionPointerStrict:
    DiagKind =
        diag::warn_typecheck_convert_incompatible_function_pointer_strict;
    ConvHints.tryToFixConversion(SrcExpr, SrcType, DstType, *this);
    MayHaveConvFixit = true;
    break;
  case IncompatibleFunctionPointer:
    if (getLangOpts().CPlusPlus) {
      DiagKind = diag::err_typecheck_convert_incompatible_function_pointer;
      isInvalid = true;
    } else {
      DiagKind = diag::ext_typecheck_convert_incompatible_function_pointer;
    }
    ConvHints.tryToFixConversion(SrcExpr, SrcType, DstType, *this);
    MayHaveConvFixit = true;
    break;
  case IncompatiblePointer:
    if (Action == AA_Passing_CFAudited) {
      DiagKind = diag::err_arc_typecheck_convert_incompatible_pointer;
    } else if (getLangOpts().CPlusPlus) {
      DiagKind = diag::err_typecheck_convert_incompatible_pointer;
      isInvalid = true;
    } else {
      DiagKind = diag::ext_typecheck_convert_incompatible_pointer;
    }
    CheckInferredResultType = DstType->isObjCObjectPointerType() &&
      SrcType->isObjCObjectPointerType();
    if (CheckInferredResultType) {
      SrcType = SrcType.getUnqualifiedType();
      DstType = DstType.getUnqualifiedType();
    } else {
      ConvHints.tryToFixConversion(SrcExpr, SrcType, DstType, *this);
    }
    MayHaveConvFixit = true;
    break;
  case IncompatiblePointerSign:
    if (getLangOpts().CPlusPlus) {
      DiagKind = diag::err_typecheck_convert_incompatible_pointer_sign;
      isInvalid = true;
    } else {
      DiagKind = diag::ext_typecheck_convert_incompatible_pointer_sign;
    }
    break;
  case FunctionVoidPointer:
    if (getLangOpts().CPlusPlus) {
      DiagKind = diag::err_typecheck_convert_pointer_void_func;
      isInvalid = true;
    } else {
      DiagKind = diag::ext_typecheck_convert_pointer_void_func;
    }
    break;
  case IncompatiblePointerDiscardsQualifiers: {
    // Perform array-to-pointer decay if necessary.
    if (SrcType->isArrayType()) SrcType = Context.getArrayDecayedType(SrcType);

    isInvalid = true;

    Qualifiers lhq = SrcType->getPointeeType().getQualifiers();
    Qualifiers rhq = DstType->getPointeeType().getQualifiers();
    if (lhq.getAddressSpace() != rhq.getAddressSpace()) {
      DiagKind = diag::err_typecheck_incompatible_address_space;
      break;
    } else if (lhq.getObjCLifetime() != rhq.getObjCLifetime()) {
      DiagKind = diag::err_typecheck_incompatible_ownership;
      break;
    }

    llvm_unreachable("unknown error case for discarding qualifiers!");
    // fallthrough
  }
  case CompatiblePointerDiscardsQualifiers:
    // If the qualifiers lost were because we were applying the
    // (deprecated) C++ conversion from a string literal to a char*
    // (or wchar_t*), then there was no error (C++ 4.2p2).  FIXME:
    // Ideally, this check would be performed in
    // checkPointerTypesForAssignment. However, that would require a
    // bit of refactoring (so that the second argument is an
    // expression, rather than a type), which should be done as part
    // of a larger effort to fix checkPointerTypesForAssignment for
    // C++ semantics.
    if (getLangOpts().CPlusPlus &&
        IsStringLiteralToNonConstPointerConversion(SrcExpr, DstType))
      return false;
    if (getLangOpts().CPlusPlus) {
      DiagKind =  diag::err_typecheck_convert_discards_qualifiers;
      isInvalid = true;
    } else {
      DiagKind =  diag::ext_typecheck_convert_discards_qualifiers;
    }

    break;
  case IncompatibleNestedPointerQualifiers:
    if (getLangOpts().CPlusPlus) {
      isInvalid = true;
      DiagKind = diag::err_nested_pointer_qualifier_mismatch;
    } else {
      DiagKind = diag::ext_nested_pointer_qualifier_mismatch;
    }
    break;
  case IncompatibleNestedPointerAddressSpaceMismatch:
    DiagKind = diag::err_typecheck_incompatible_nested_address_space;
    isInvalid = true;
    break;
  case IntToBlockPointer:
    DiagKind = diag::err_int_to_block_pointer;
    isInvalid = true;
    break;
  case IncompatibleBlockPointer:
    DiagKind = diag::err_typecheck_convert_incompatible_block_pointer;
    isInvalid = true;
    break;
  case IncompatibleObjCQualifiedId: {
    if (SrcType->isObjCQualifiedIdType()) {
      const ObjCObjectPointerType *srcOPT =
                SrcType->castAs<ObjCObjectPointerType>();
      for (auto *srcProto : srcOPT->quals()) {
        PDecl = srcProto;
        break;
      }
      if (const ObjCInterfaceType *IFaceT =
            DstType->castAs<ObjCObjectPointerType>()->getInterfaceType())
        IFace = IFaceT->getDecl();
    }
    else if (DstType->isObjCQualifiedIdType()) {
      const ObjCObjectPointerType *dstOPT =
        DstType->castAs<ObjCObjectPointerType>();
      for (auto *dstProto : dstOPT->quals()) {
        PDecl = dstProto;
        break;
      }
      if (const ObjCInterfaceType *IFaceT =
            SrcType->castAs<ObjCObjectPointerType>()->getInterfaceType())
        IFace = IFaceT->getDecl();
    }
    if (getLangOpts().CPlusPlus) {
      DiagKind = diag::err_incompatible_qualified_id;
      isInvalid = true;
    } else {
      DiagKind = diag::warn_incompatible_qualified_id;
    }
    break;
  }
  case IncompatibleVectors:
    if (getLangOpts().CPlusPlus) {
      DiagKind = diag::err_incompatible_vectors;
      isInvalid = true;
    } else {
      DiagKind = diag::warn_incompatible_vectors;
    }
    break;
  case IncompatibleObjCWeakRef:
    DiagKind = diag::err_arc_weak_unavailable_assign;
    isInvalid = true;
    break;
  case Incompatible:
    if (maybeDiagnoseAssignmentToFunction(*this, DstType, SrcExpr)) {
      if (Complained)
        *Complained = true;
      return true;
    }

    DiagKind = diag::err_typecheck_convert_incompatible;
    ConvHints.tryToFixConversion(SrcExpr, SrcType, DstType, *this);
    MayHaveConvFixit = true;
    isInvalid = true;
    MayHaveFunctionDiff = true;
    break;
  }

  QualType FirstType, SecondType;
  switch (Action) {
  case AA_Assigning:
  case AA_Initializing:
    // The destination type comes first.
    FirstType = DstType;
    SecondType = SrcType;
    break;

  case AA_Returning:
  case AA_Passing:
  case AA_Passing_CFAudited:
  case AA_Converting:
  case AA_Sending:
  case AA_Casting:
    // The source type comes first.
    FirstType = SrcType;
    SecondType = DstType;
    break;
  }

  PartialDiagnostic FDiag = PDiag(DiagKind);
  AssignmentAction ActionForDiag = Action;
  if (Action == AA_Passing_CFAudited)
    ActionForDiag = AA_Passing;

  FDiag << FirstType << SecondType << ActionForDiag
        << SrcExpr->getSourceRange();

  if (DiagKind == diag::ext_typecheck_convert_incompatible_pointer_sign ||
      DiagKind == diag::err_typecheck_convert_incompatible_pointer_sign) {
    auto isPlainChar = [](const clang::Type *Type) {
      return Type->isSpecificBuiltinType(BuiltinType::Char_S) ||
             Type->isSpecificBuiltinType(BuiltinType::Char_U);
    };
    FDiag << (isPlainChar(FirstType->getPointeeOrArrayElementType()) ||
              isPlainChar(SecondType->getPointeeOrArrayElementType()));
  }

  // If we can fix the conversion, suggest the FixIts.
  if (!ConvHints.isNull()) {
    for (FixItHint &H : ConvHints.Hints)
      FDiag << H;
  }

  if (MayHaveConvFixit) { FDiag << (unsigned) (ConvHints.Kind); }

  if (MayHaveFunctionDiff)
    HandleFunctionTypeMismatch(FDiag, SecondType, FirstType);

  Diag(Loc, FDiag);
  if ((DiagKind == diag::warn_incompatible_qualified_id ||
       DiagKind == diag::err_incompatible_qualified_id) &&
      PDecl && IFace && !IFace->hasDefinition())
    Diag(IFace->getLocation(), diag::note_incomplete_class_and_qualified_id)
        << IFace << PDecl;

  if (SecondType == Context.OverloadTy)
    NoteAllOverloadCandidates(OverloadExpr::find(SrcExpr).Expression,
                              FirstType, /*TakingAddress=*/true);

  if (CheckInferredResultType)
    ObjC().EmitRelatedResultTypeNote(SrcExpr);

  if (Action == AA_Returning && ConvTy == IncompatiblePointer)
    ObjC().EmitRelatedResultTypeNoteForReturn(DstType);

  if (Complained)
    *Complained = true;
  return isInvalid;
}

ExprResult Sema::VerifyIntegerConstantExpression(Expr *E,
                                                 llvm::APSInt *Result,
                                                 AllowFoldKind CanFold) {
  class SimpleICEDiagnoser : public VerifyICEDiagnoser {
  public:
    SemaDiagnosticBuilder diagnoseNotICEType(Sema &S, SourceLocation Loc,
                                             QualType T) override {
      return S.Diag(Loc, diag::err_ice_not_integral)
             << T << S.LangOpts.CPlusPlus;
    }
    SemaDiagnosticBuilder diagnoseNotICE(Sema &S, SourceLocation Loc) override {
      return S.Diag(Loc, diag::err_expr_not_ice) << S.LangOpts.CPlusPlus;
    }
  } Diagnoser;

  return VerifyIntegerConstantExpression(E, Result, Diagnoser, CanFold);
}

ExprResult Sema::VerifyIntegerConstantExpression(Expr *E,
                                                 llvm::APSInt *Result,
                                                 unsigned DiagID,
                                                 AllowFoldKind CanFold) {
  class IDDiagnoser : public VerifyICEDiagnoser {
    unsigned DiagID;

  public:
    IDDiagnoser(unsigned DiagID)
      : VerifyICEDiagnoser(DiagID == 0), DiagID(DiagID) { }

    SemaDiagnosticBuilder diagnoseNotICE(Sema &S, SourceLocation Loc) override {
      return S.Diag(Loc, DiagID);
    }
  } Diagnoser(DiagID);

  return VerifyIntegerConstantExpression(E, Result, Diagnoser, CanFold);
}

Sema::SemaDiagnosticBuilder
Sema::VerifyICEDiagnoser::diagnoseNotICEType(Sema &S, SourceLocation Loc,
                                             QualType T) {
  return diagnoseNotICE(S, Loc);
}

Sema::SemaDiagnosticBuilder
Sema::VerifyICEDiagnoser::diagnoseFold(Sema &S, SourceLocation Loc) {
  return S.Diag(Loc, diag::ext_expr_not_ice) << S.LangOpts.CPlusPlus;
}

ExprResult
Sema::VerifyIntegerConstantExpression(Expr *E, llvm::APSInt *Result,
                                      VerifyICEDiagnoser &Diagnoser,
                                      AllowFoldKind CanFold) {
  SourceLocation DiagLoc = E->getBeginLoc();

  if (getLangOpts().CPlusPlus11) {
    // C++11 [expr.const]p5:
    //   If an expression of literal class type is used in a context where an
    //   integral constant expression is required, then that class type shall
    //   have a single non-explicit conversion function to an integral or
    //   unscoped enumeration type
    ExprResult Converted;
    class CXX11ConvertDiagnoser : public ICEConvertDiagnoser {
      VerifyICEDiagnoser &BaseDiagnoser;
    public:
      CXX11ConvertDiagnoser(VerifyICEDiagnoser &BaseDiagnoser)
          : ICEConvertDiagnoser(/*AllowScopedEnumerations*/ false,
                                BaseDiagnoser.Suppress, true),
            BaseDiagnoser(BaseDiagnoser) {}

      SemaDiagnosticBuilder diagnoseNotInt(Sema &S, SourceLocation Loc,
                                           QualType T) override {
        return BaseDiagnoser.diagnoseNotICEType(S, Loc, T);
      }

      SemaDiagnosticBuilder diagnoseIncomplete(
          Sema &S, SourceLocation Loc, QualType T) override {
        return S.Diag(Loc, diag::err_ice_incomplete_type) << T;
      }

      SemaDiagnosticBuilder diagnoseExplicitConv(
          Sema &S, SourceLocation Loc, QualType T, QualType ConvTy) override {
        return S.Diag(Loc, diag::err_ice_explicit_conversion) << T << ConvTy;
      }

      SemaDiagnosticBuilder noteExplicitConv(
          Sema &S, CXXConversionDecl *Conv, QualType ConvTy) override {
        return S.Diag(Conv->getLocation(), diag::note_ice_conversion_here)
                 << ConvTy->isEnumeralType() << ConvTy;
      }

      SemaDiagnosticBuilder diagnoseAmbiguous(
          Sema &S, SourceLocation Loc, QualType T) override {
        return S.Diag(Loc, diag::err_ice_ambiguous_conversion) << T;
      }

      SemaDiagnosticBuilder noteAmbiguous(
          Sema &S, CXXConversionDecl *Conv, QualType ConvTy) override {
        return S.Diag(Conv->getLocation(), diag::note_ice_conversion_here)
                 << ConvTy->isEnumeralType() << ConvTy;
      }

      SemaDiagnosticBuilder diagnoseConversion(
          Sema &S, SourceLocation Loc, QualType T, QualType ConvTy) override {
        llvm_unreachable("conversion functions are permitted");
      }
    } ConvertDiagnoser(Diagnoser);

    Converted = PerformContextualImplicitConversion(DiagLoc, E,
                                                    ConvertDiagnoser);
    if (Converted.isInvalid())
      return Converted;
    E = Converted.get();
    // The 'explicit' case causes us to get a RecoveryExpr.  Give up here so we
    // don't try to evaluate it later. We also don't want to return the
    // RecoveryExpr here, as it results in this call succeeding, thus callers of
    // this function will attempt to use 'Value'.
    if (isa<RecoveryExpr>(E))
      return ExprError();
    if (!E->getType()->isIntegralOrUnscopedEnumerationType())
      return ExprError();
  } else if (!E->getType()->isIntegralOrUnscopedEnumerationType()) {
    // An ICE must be of integral or unscoped enumeration type.
    if (!Diagnoser.Suppress)
      Diagnoser.diagnoseNotICEType(*this, DiagLoc, E->getType())
          << E->getSourceRange();
    return ExprError();
  }

  ExprResult RValueExpr = DefaultLvalueConversion(E);
  if (RValueExpr.isInvalid())
    return ExprError();

  E = RValueExpr.get();

  // Circumvent ICE checking in C++11 to avoid evaluating the expression twice
  // in the non-ICE case.
  if (!getLangOpts().CPlusPlus11 && E->isIntegerConstantExpr(Context)) {
    SmallVector<PartialDiagnosticAt, 8> Notes;
    if (Result)
      *Result = E->EvaluateKnownConstIntCheckOverflow(Context, &Notes);
    if (!isa<ConstantExpr>(E))
      E = Result ? ConstantExpr::Create(Context, E, APValue(*Result))
                 : ConstantExpr::Create(Context, E);
    
    if (Notes.empty())
      return E;
    
    // If our only note is the usual "invalid subexpression" note, just point
    // the caret at its location rather than producing an essentially
    // redundant note.
    if (Notes.size() == 1 && Notes[0].second.getDiagID() ==
          diag::note_invalid_subexpr_in_const_expr) {
      DiagLoc = Notes[0].first;
      Notes.clear();
    }
    
    if (getLangOpts().CPlusPlus) {
      if (!Diagnoser.Suppress) {
        Diagnoser.diagnoseNotICE(*this, DiagLoc) << E->getSourceRange();
        for (const PartialDiagnosticAt &Note : Notes)
          Diag(Note.first, Note.second);
      }
      return ExprError();
    }

    Diagnoser.diagnoseFold(*this, DiagLoc) << E->getSourceRange();
    for (const PartialDiagnosticAt &Note : Notes)
      Diag(Note.first, Note.second);
    
    return E;
  }

  Expr::EvalResult EvalResult;
  SmallVector<PartialDiagnosticAt, 8> Notes;
  EvalResult.Diag = &Notes;

  // Try to evaluate the expression, and produce diagnostics explaining why it's
  // not a constant expression as a side-effect.
  bool Folded =
      E->EvaluateAsRValue(EvalResult, Context, /*isConstantContext*/ true) &&
      EvalResult.Val.isInt() && !EvalResult.HasSideEffects &&
      (!getLangOpts().CPlusPlus || !EvalResult.HasUndefinedBehavior);

  if (!isa<ConstantExpr>(E))
    E = ConstantExpr::Create(Context, E, EvalResult.Val);

  // In C++11, we can rely on diagnostics being produced for any expression
  // which is not a constant expression. If no diagnostics were produced, then
  // this is a constant expression.
  if (Folded && getLangOpts().CPlusPlus11 && Notes.empty()) {
    if (Result)
      *Result = EvalResult.Val.getInt();
    return E;
  }

  // If our only note is the usual "invalid subexpression" note, just point
  // the caret at its location rather than producing an essentially
  // redundant note.
  if (Notes.size() == 1 && Notes[0].second.getDiagID() ==
        diag::note_invalid_subexpr_in_const_expr) {
    DiagLoc = Notes[0].first;
    Notes.clear();
  }

  if (!Folded || !CanFold) {
    if (!Diagnoser.Suppress) {
      Diagnoser.diagnoseNotICE(*this, DiagLoc) << E->getSourceRange();
      for (const PartialDiagnosticAt &Note : Notes)
        Diag(Note.first, Note.second);
    }

    return ExprError();
  }

  Diagnoser.diagnoseFold(*this, DiagLoc) << E->getSourceRange();
  for (const PartialDiagnosticAt &Note : Notes)
    Diag(Note.first, Note.second);

  if (Result)
    *Result = EvalResult.Val.getInt();
  return E;
}

namespace {
  // Handle the case where we conclude a expression which we speculatively
  // considered to be unevaluated is actually evaluated.
  class TransformToPE : public TreeTransform<TransformToPE> {
    typedef TreeTransform<TransformToPE> BaseTransform;

  public:
    TransformToPE(Sema &SemaRef) : BaseTransform(SemaRef) { }

    // Make sure we redo semantic analysis
    bool AlwaysRebuild() { return true; }
    bool ReplacingOriginal() { return true; }

    // We need to special-case DeclRefExprs referring to FieldDecls which
    // are not part of a member pointer formation; normal TreeTransforming
    // doesn't catch this case because of the way we represent them in the AST.
    // FIXME: This is a bit ugly; is it really the best way to handle this
    // case?
    //
    // Error on DeclRefExprs referring to FieldDecls.
    ExprResult TransformDeclRefExpr(DeclRefExpr *E) {
      if (isa<FieldDecl>(E->getDecl()) &&
          !SemaRef.isUnevaluatedContext())
        return SemaRef.Diag(E->getLocation(),
                            diag::err_invalid_non_static_member_use)
            << E->getDecl() << E->getSourceRange();

      return BaseTransform::TransformDeclRefExpr(E);
    }

    // Exception: filter out member pointer formation
    ExprResult TransformUnaryOperator(UnaryOperator *E) {
      if (E->getOpcode() == UO_AddrOf && E->getType()->isMemberPointerType())
        return E;

      return BaseTransform::TransformUnaryOperator(E);
    }

    // The body of a lambda-expression is in a separate expression evaluation
    // context so never needs to be transformed.
    // FIXME: Ideally we wouldn't transform the closure type either, and would
    // just recreate the capture expressions and lambda expression.
    StmtResult TransformLambdaBody(LambdaExpr *E, Stmt *Body) {
      return SkipLambdaBody(E, Body);
    }
  };
}

ExprResult Sema::TransformToPotentiallyEvaluated(Expr *E) {
  assert(isUnevaluatedContext() &&
         "Should only transform unevaluated expressions");
  ExprEvalContexts.back().Context =
      ExprEvalContexts[ExprEvalContexts.size()-2].Context;
  if (isUnevaluatedContext())
    return E;
  return TransformToPE(*this).TransformExpr(E);
}

TypeSourceInfo *Sema::TransformToPotentiallyEvaluated(TypeSourceInfo *TInfo) {
  assert(isUnevaluatedContext() &&
         "Should only transform unevaluated expressions");
  ExprEvalContexts.back().Context = parentEvaluationContext().Context;
  if (isUnevaluatedContext())
    return TInfo;
  return TransformToPE(*this).TransformType(TInfo);
}

void
Sema::PushExpressionEvaluationContext(
    ExpressionEvaluationContext NewContext, Decl *LambdaContextDecl,
    ExpressionEvaluationContextRecord::ExpressionKind ExprContext) {
  ExprEvalContexts.emplace_back(NewContext, ExprCleanupObjects.size(), Cleanup,
                                LambdaContextDecl, ExprContext);

  // Discarded statements and immediate contexts nested in other
  // discarded statements or immediate context are themselves
  // a discarded statement or an immediate context, respectively.
  ExprEvalContexts.back().InDiscardedStatement =
      parentEvaluationContext().isDiscardedStatementContext();

  // C++23 [expr.const]/p15
  // An expression or conversion is in an immediate function context if [...]
  // it is a subexpression of a manifestly constant-evaluated expression or
  // conversion.
  const auto &Prev = parentEvaluationContext();
  ExprEvalContexts.back().InImmediateFunctionContext =
      Prev.isImmediateFunctionContext() || Prev.isConstantEvaluated();

  ExprEvalContexts.back().InImmediateEscalatingFunctionContext =
      Prev.InImmediateEscalatingFunctionContext;

  Cleanup.reset();
  if (!MaybeODRUseExprs.empty())
    std::swap(MaybeODRUseExprs, ExprEvalContexts.back().SavedMaybeODRUseExprs);
}

void
Sema::PushExpressionEvaluationContext(
    ExpressionEvaluationContext NewContext, ReuseLambdaContextDecl_t,
    ExpressionEvaluationContextRecord::ExpressionKind ExprContext) {
  Decl *ClosureContextDecl = ExprEvalContexts.back().ManglingContextDecl;
  PushExpressionEvaluationContext(NewContext, ClosureContextDecl, ExprContext);
}

namespace {

const DeclRefExpr *CheckPossibleDeref(Sema &S, const Expr *PossibleDeref) {
  PossibleDeref = PossibleDeref->IgnoreParenImpCasts();
  if (const auto *E = dyn_cast<UnaryOperator>(PossibleDeref)) {
    if (E->getOpcode() == UO_Deref)
      return CheckPossibleDeref(S, E->getSubExpr());
  } else if (const auto *E = dyn_cast<ArraySubscriptExpr>(PossibleDeref)) {
    return CheckPossibleDeref(S, E->getBase());
  } else if (const auto *E = dyn_cast<MemberExpr>(PossibleDeref)) {
    return CheckPossibleDeref(S, E->getBase());
  } else if (const auto E = dyn_cast<DeclRefExpr>(PossibleDeref)) {
    QualType Inner;
    QualType Ty = E->getType();
    if (const auto *Ptr = Ty->getAs<PointerType>())
      Inner = Ptr->getPointeeType();
    else if (const auto *Arr = S.Context.getAsArrayType(Ty))
      Inner = Arr->getElementType();
    else
      return nullptr;

    if (Inner->hasAttr(attr::NoDeref))
      return E;
  }
  return nullptr;
}

} // namespace

void Sema::WarnOnPendingNoDerefs(ExpressionEvaluationContextRecord &Rec) {
  for (const Expr *E : Rec.PossibleDerefs) {
    const DeclRefExpr *DeclRef = CheckPossibleDeref(*this, E);
    if (DeclRef) {
      const ValueDecl *Decl = DeclRef->getDecl();
      Diag(E->getExprLoc(), diag::warn_dereference_of_noderef_type)
          << Decl->getName() << E->getSourceRange();
      Diag(Decl->getLocation(), diag::note_previous_decl) << Decl->getName();
    } else {
      Diag(E->getExprLoc(), diag::warn_dereference_of_noderef_type_no_decl)
          << E->getSourceRange();
    }
  }
  Rec.PossibleDerefs.clear();
}

void Sema::CheckUnusedVolatileAssignment(Expr *E) {
  if (!E->getType().isVolatileQualified() || !getLangOpts().CPlusPlus20)
    return;

  // Note: ignoring parens here is not justified by the standard rules, but
  // ignoring parentheses seems like a more reasonable approach, and this only
  // drives a deprecation warning so doesn't affect conformance.
  if (auto *BO = dyn_cast<BinaryOperator>(E->IgnoreParenImpCasts())) {
    if (BO->getOpcode() == BO_Assign) {
      auto &LHSs = ExprEvalContexts.back().VolatileAssignmentLHSs;
      llvm::erase(LHSs, BO->getLHS());
    }
  }
}

void Sema::MarkExpressionAsImmediateEscalating(Expr *E) {
  assert(getLangOpts().CPlusPlus20 &&
         ExprEvalContexts.back().InImmediateEscalatingFunctionContext &&
         "Cannot mark an immediate escalating expression outside of an "
         "immediate escalating context");
  if (auto *Call = dyn_cast<CallExpr>(E->IgnoreImplicit());
      Call && Call->getCallee()) {
    if (auto *DeclRef =
            dyn_cast<DeclRefExpr>(Call->getCallee()->IgnoreImplicit()))
      DeclRef->setIsImmediateEscalating(true);
  } else if (auto *Ctr = dyn_cast<CXXConstructExpr>(E->IgnoreImplicit())) {
    Ctr->setIsImmediateEscalating(true);
  } else if (auto *DeclRef = dyn_cast<DeclRefExpr>(E->IgnoreImplicit())) {
    DeclRef->setIsImmediateEscalating(true);
  } else {
    assert(false && "expected an immediately escalating expression");
  }
  if (FunctionScopeInfo *FI = getCurFunction())
    FI->FoundImmediateEscalatingExpression = true;
}

ExprResult Sema::CheckForImmediateInvocation(ExprResult E, FunctionDecl *Decl) {
  if (isUnevaluatedContext() || !E.isUsable() || !Decl ||
      !Decl->isImmediateFunction() || isAlwaysConstantEvaluatedContext() ||
      isCheckingDefaultArgumentOrInitializer() ||
      RebuildingImmediateInvocation || isImmediateFunctionContext())
    return E;

  /// Opportunistically remove the callee from ReferencesToConsteval if we can.
  /// It's OK if this fails; we'll also remove this in
  /// HandleImmediateInvocations, but catching it here allows us to avoid
  /// walking the AST looking for it in simple cases.
  if (auto *Call = dyn_cast<CallExpr>(E.get()->IgnoreImplicit()))
    if (auto *DeclRef =
            dyn_cast<DeclRefExpr>(Call->getCallee()->IgnoreImplicit()))
      ExprEvalContexts.back().ReferenceToConsteval.erase(DeclRef);

  // C++23 [expr.const]/p16
  // An expression or conversion is immediate-escalating if it is not initially
  // in an immediate function context and it is [...] an immediate invocation
  // that is not a constant expression and is not a subexpression of an
  // immediate invocation.
  APValue Cached;
  auto CheckConstantExpressionAndKeepResult = [&]() {
    llvm::SmallVector<PartialDiagnosticAt, 8> Notes;
    Expr::EvalResult Eval;
    Eval.Diag = &Notes;
    bool Res = E.get()->EvaluateAsConstantExpr(
        Eval, getASTContext(), ConstantExprKind::ImmediateInvocation);
    if (Res && Notes.empty()) {
      Cached = std::move(Eval.Val);
      return true;
    }
    return false;
  };

  if (!E.get()->isValueDependent() &&
      ExprEvalContexts.back().InImmediateEscalatingFunctionContext &&
      !CheckConstantExpressionAndKeepResult()) {
    MarkExpressionAsImmediateEscalating(E.get());
    return E;
  }

  if (Cleanup.exprNeedsCleanups()) {
    // Since an immediate invocation is a full expression itself - it requires
    // an additional ExprWithCleanups node, but it can participate to a bigger
    // full expression which actually requires cleanups to be run after so
    // create ExprWithCleanups without using MaybeCreateExprWithCleanups as it
    // may discard cleanups for outer expression too early.

    // Note that ExprWithCleanups created here must always have empty cleanup
    // objects:
    // - compound literals do not create cleanup objects in C++ and immediate
    // invocations are C++-only.
    // - blocks are not allowed inside constant expressions and compiler will
    // issue an error if they appear there.
    //
    // Hence, in correct code any cleanup objects created inside current
    // evaluation context must be outside the immediate invocation.
    E = ExprWithCleanups::Create(getASTContext(), E.get(),
                                 Cleanup.cleanupsHaveSideEffects(), {});
  }

  ConstantExpr *Res = ConstantExpr::Create(
      getASTContext(), E.get(),
      ConstantExpr::getStorageKind(Decl->getReturnType().getTypePtr(),
                                   getASTContext()),
      /*IsImmediateInvocation*/ true);
  if (Cached.hasValue())
    Res->MoveIntoResult(Cached, getASTContext());
  /// Value-dependent constant expressions should not be immediately
  /// evaluated until they are instantiated.
  if (!Res->isValueDependent())
    ExprEvalContexts.back().ImmediateInvocationCandidates.emplace_back(Res, 0);
  return Res;
}

static void EvaluateAndDiagnoseImmediateInvocation(
    Sema &SemaRef, Sema::ImmediateInvocationCandidate Candidate) {
  llvm::SmallVector<PartialDiagnosticAt, 8> Notes;
  Expr::EvalResult Eval;
  Eval.Diag = &Notes;
  ConstantExpr *CE = Candidate.getPointer();
  bool Result = CE->EvaluateAsConstantExpr(
      Eval, SemaRef.getASTContext(), ConstantExprKind::ImmediateInvocation);
  if (!Result || !Notes.empty()) {
    SemaRef.FailedImmediateInvocations.insert(CE);
    Expr *InnerExpr = CE->getSubExpr()->IgnoreImplicit();
    if (auto *FunctionalCast = dyn_cast<CXXFunctionalCastExpr>(InnerExpr))
      InnerExpr = FunctionalCast->getSubExpr()->IgnoreImplicit();
    FunctionDecl *FD = nullptr;
    if (auto *Call = dyn_cast<CallExpr>(InnerExpr))
      FD = cast<FunctionDecl>(Call->getCalleeDecl());
    else if (auto *Call = dyn_cast<CXXConstructExpr>(InnerExpr))
      FD = Call->getConstructor();
    else if (auto *Cast = dyn_cast<CastExpr>(InnerExpr))
      FD = dyn_cast_or_null<FunctionDecl>(Cast->getConversionFunction());

    assert(FD && FD->isImmediateFunction() &&
           "could not find an immediate function in this expression");
    if (FD->isInvalidDecl())
      return;
    SemaRef.Diag(CE->getBeginLoc(), diag::err_invalid_consteval_call)
        << FD << FD->isConsteval();
    if (auto Context =
            SemaRef.InnermostDeclarationWithDelayedImmediateInvocations()) {
      SemaRef.Diag(Context->Loc, diag::note_invalid_consteval_initializer)
          << Context->Decl;
      SemaRef.Diag(Context->Decl->getBeginLoc(), diag::note_declared_at);
    }
    if (!FD->isConsteval())
      SemaRef.DiagnoseImmediateEscalatingReason(FD);
    for (auto &Note : Notes)
      SemaRef.Diag(Note.first, Note.second);
    return;
  }
  CE->MoveIntoResult(Eval.Val, SemaRef.getASTContext());
}

static void RemoveNestedImmediateInvocation(
    Sema &SemaRef, Sema::ExpressionEvaluationContextRecord &Rec,
    SmallVector<Sema::ImmediateInvocationCandidate, 4>::reverse_iterator It) {
  struct ComplexRemove : TreeTransform<ComplexRemove> {
    using Base = TreeTransform<ComplexRemove>;
    llvm::SmallPtrSetImpl<DeclRefExpr *> &DRSet;
    SmallVector<Sema::ImmediateInvocationCandidate, 4> &IISet;
    SmallVector<Sema::ImmediateInvocationCandidate, 4>::reverse_iterator
        CurrentII;
    ComplexRemove(Sema &SemaRef, llvm::SmallPtrSetImpl<DeclRefExpr *> &DR,
                  SmallVector<Sema::ImmediateInvocationCandidate, 4> &II,
                  SmallVector<Sema::ImmediateInvocationCandidate,
                              4>::reverse_iterator Current)
        : Base(SemaRef), DRSet(DR), IISet(II), CurrentII(Current) {}
    void RemoveImmediateInvocation(ConstantExpr* E) {
      auto It = std::find_if(CurrentII, IISet.rend(),
                             [E](Sema::ImmediateInvocationCandidate Elem) {
                               return Elem.getPointer() == E;
                             });
      // It is possible that some subexpression of the current immediate
      // invocation was handled from another expression evaluation context. Do
      // not handle the current immediate invocation if some of its
      // subexpressions failed before.
      if (It == IISet.rend()) {
        if (SemaRef.FailedImmediateInvocations.contains(E))
          CurrentII->setInt(1);
      } else {
        It->setInt(1); // Mark as deleted
      }
    }
    ExprResult TransformConstantExpr(ConstantExpr *E) {
      if (!E->isImmediateInvocation())
        return Base::TransformConstantExpr(E);
      RemoveImmediateInvocation(E);
      return Base::TransformExpr(E->getSubExpr());
    }
    /// Base::TransfromCXXOperatorCallExpr doesn't traverse the callee so
    /// we need to remove its DeclRefExpr from the DRSet.
    ExprResult TransformCXXOperatorCallExpr(CXXOperatorCallExpr *E) {
      DRSet.erase(cast<DeclRefExpr>(E->getCallee()->IgnoreImplicit()));
      return Base::TransformCXXOperatorCallExpr(E);
    }
    /// Base::TransformUserDefinedLiteral doesn't preserve the
    /// UserDefinedLiteral node.
    ExprResult TransformUserDefinedLiteral(UserDefinedLiteral *E) { return E; }
    /// Base::TransformInitializer skips ConstantExpr so we need to visit them
    /// here.
    ExprResult TransformInitializer(Expr *Init, bool NotCopyInit) {
      if (!Init)
        return Init;
      /// ConstantExpr are the first layer of implicit node to be removed so if
      /// Init isn't a ConstantExpr, no ConstantExpr will be skipped.
      if (auto *CE = dyn_cast<ConstantExpr>(Init))
        if (CE->isImmediateInvocation())
          RemoveImmediateInvocation(CE);
      return Base::TransformInitializer(Init, NotCopyInit);
    }
    ExprResult TransformDeclRefExpr(DeclRefExpr *E) {
      DRSet.erase(E);
      return E;
    }
    ExprResult TransformLambdaExpr(LambdaExpr *E) {
      // Do not rebuild lambdas to avoid creating a new type.
      // Lambdas have already been processed inside their eval context.
      return E;
    }
    bool AlwaysRebuild() { return false; }
    bool ReplacingOriginal() { return true; }
    bool AllowSkippingCXXConstructExpr() {
      bool Res = AllowSkippingFirstCXXConstructExpr;
      AllowSkippingFirstCXXConstructExpr = true;
      return Res;
    }
    bool AllowSkippingFirstCXXConstructExpr = true;
  } Transformer(SemaRef, Rec.ReferenceToConsteval,
                Rec.ImmediateInvocationCandidates, It);

  /// CXXConstructExpr with a single argument are getting skipped by
  /// TreeTransform in some situtation because they could be implicit. This
  /// can only occur for the top-level CXXConstructExpr because it is used
  /// nowhere in the expression being transformed therefore will not be rebuilt.
  /// Setting AllowSkippingFirstCXXConstructExpr to false will prevent from
  /// skipping the first CXXConstructExpr.
  if (isa<CXXConstructExpr>(It->getPointer()->IgnoreImplicit()))
    Transformer.AllowSkippingFirstCXXConstructExpr = false;

  ExprResult Res = Transformer.TransformExpr(It->getPointer()->getSubExpr());
  // The result may not be usable in case of previous compilation errors.
  // In this case evaluation of the expression may result in crash so just
  // don't do anything further with the result.
  if (Res.isUsable()) {
    Res = SemaRef.MaybeCreateExprWithCleanups(Res);
    It->getPointer()->setSubExpr(Res.get());
  }
}

static void
HandleImmediateInvocations(Sema &SemaRef,
                           Sema::ExpressionEvaluationContextRecord &Rec) {
  if ((Rec.ImmediateInvocationCandidates.size() == 0 &&
       Rec.ReferenceToConsteval.size() == 0) ||
      Rec.isImmediateFunctionContext() || SemaRef.RebuildingImmediateInvocation)
    return;

  /// When we have more than 1 ImmediateInvocationCandidates or previously
  /// failed immediate invocations, we need to check for nested
  /// ImmediateInvocationCandidates in order to avoid duplicate diagnostics.
  /// Otherwise we only need to remove ReferenceToConsteval in the immediate
  /// invocation.
  if (Rec.ImmediateInvocationCandidates.size() > 1 ||
      !SemaRef.FailedImmediateInvocations.empty()) {

    /// Prevent sema calls during the tree transform from adding pointers that
    /// are already in the sets.
    llvm::SaveAndRestore DisableIITracking(
        SemaRef.RebuildingImmediateInvocation, true);

    /// Prevent diagnostic during tree transfrom as they are duplicates
    Sema::TentativeAnalysisScope DisableDiag(SemaRef);

    for (auto It = Rec.ImmediateInvocationCandidates.rbegin();
         It != Rec.ImmediateInvocationCandidates.rend(); It++)
      if (!It->getInt())
        RemoveNestedImmediateInvocation(SemaRef, Rec, It);
  } else if (Rec.ImmediateInvocationCandidates.size() == 1 &&
             Rec.ReferenceToConsteval.size()) {
    struct SimpleRemove : RecursiveASTVisitor<SimpleRemove> {
      llvm::SmallPtrSetImpl<DeclRefExpr *> &DRSet;
      SimpleRemove(llvm::SmallPtrSetImpl<DeclRefExpr *> &S) : DRSet(S) {}
      bool VisitDeclRefExpr(DeclRefExpr *E) {
        DRSet.erase(E);
        return DRSet.size();
      }
    } Visitor(Rec.ReferenceToConsteval);
    Visitor.TraverseStmt(
        Rec.ImmediateInvocationCandidates.front().getPointer()->getSubExpr());
  }
  for (auto CE : Rec.ImmediateInvocationCandidates)
    if (!CE.getInt())
      EvaluateAndDiagnoseImmediateInvocation(SemaRef, CE);
  for (auto *DR : Rec.ReferenceToConsteval) {
    // If the expression is immediate escalating, it is not an error;
    // The outer context itself becomes immediate and further errors,
    // if any, will be handled by DiagnoseImmediateEscalatingReason.
    if (DR->isImmediateEscalating())
      continue;
    auto *FD = cast<FunctionDecl>(DR->getDecl());
    const NamedDecl *ND = FD;
    if (const auto *MD = dyn_cast<CXXMethodDecl>(ND);
        MD && (MD->isLambdaStaticInvoker() || isLambdaCallOperator(MD)))
      ND = MD->getParent();

    // C++23 [expr.const]/p16
    // An expression or conversion is immediate-escalating if it is not
    // initially in an immediate function context and it is [...] a
    // potentially-evaluated id-expression that denotes an immediate function
    // that is not a subexpression of an immediate invocation.
    bool ImmediateEscalating = false;
    bool IsPotentiallyEvaluated =
        Rec.Context ==
            Sema::ExpressionEvaluationContext::PotentiallyEvaluated ||
        Rec.Context ==
            Sema::ExpressionEvaluationContext::PotentiallyEvaluatedIfUsed;
    if (SemaRef.inTemplateInstantiation() && IsPotentiallyEvaluated)
      ImmediateEscalating = Rec.InImmediateEscalatingFunctionContext;

    if (!Rec.InImmediateEscalatingFunctionContext ||
        (SemaRef.inTemplateInstantiation() && !ImmediateEscalating)) {
      SemaRef.Diag(DR->getBeginLoc(), diag::err_invalid_consteval_take_address)
          << ND << isa<CXXRecordDecl>(ND) << FD->isConsteval();
      SemaRef.Diag(ND->getLocation(), diag::note_declared_at);
      if (auto Context =
              SemaRef.InnermostDeclarationWithDelayedImmediateInvocations()) {
        SemaRef.Diag(Context->Loc, diag::note_invalid_consteval_initializer)
            << Context->Decl;
        SemaRef.Diag(Context->Decl->getBeginLoc(), diag::note_declared_at);
      }
      if (FD->isImmediateEscalating() && !FD->isConsteval())
        SemaRef.DiagnoseImmediateEscalatingReason(FD);

    } else {
      SemaRef.MarkExpressionAsImmediateEscalating(DR);
    }
  }
}

void Sema::PopExpressionEvaluationContext() {
  ExpressionEvaluationContextRecord& Rec = ExprEvalContexts.back();
  unsigned NumTypos = Rec.NumTypos;

  if (!Rec.Lambdas.empty()) {
    using ExpressionKind = ExpressionEvaluationContextRecord::ExpressionKind;
    if (!getLangOpts().CPlusPlus20 &&
        (Rec.ExprContext == ExpressionKind::EK_TemplateArgument ||
         Rec.isUnevaluated() ||
         (Rec.isConstantEvaluated() && !getLangOpts().CPlusPlus17))) {
      unsigned D;
      if (Rec.isUnevaluated()) {
        // C++11 [expr.prim.lambda]p2:
        //   A lambda-expression shall not appear in an unevaluated operand
        //   (Clause 5).
        D = diag::err_lambda_unevaluated_operand;
      } else if (Rec.isConstantEvaluated() && !getLangOpts().CPlusPlus17) {
        // C++1y [expr.const]p2:
        //   A conditional-expression e is a core constant expression unless the
        //   evaluation of e, following the rules of the abstract machine, would
        //   evaluate [...] a lambda-expression.
        D = diag::err_lambda_in_constant_expression;
      } else if (Rec.ExprContext == ExpressionKind::EK_TemplateArgument) {
        // C++17 [expr.prim.lamda]p2:
        // A lambda-expression shall not appear [...] in a template-argument.
        D = diag::err_lambda_in_invalid_context;
      } else
        llvm_unreachable("Couldn't infer lambda error message.");

      for (const auto *L : Rec.Lambdas)
        Diag(L->getBeginLoc(), D);
    }
  }

  // Append the collected materialized temporaries into previous context before
  // exit if the previous also is a lifetime extending context.
  auto &PrevRecord = parentEvaluationContext();
  if (getLangOpts().CPlusPlus23 && Rec.InLifetimeExtendingContext &&
      PrevRecord.InLifetimeExtendingContext &&
      !Rec.ForRangeLifetimeExtendTemps.empty()) {
    PrevRecord.ForRangeLifetimeExtendTemps.append(
        Rec.ForRangeLifetimeExtendTemps);
  }

  WarnOnPendingNoDerefs(Rec);
  HandleImmediateInvocations(*this, Rec);

  // Warn on any volatile-qualified simple-assignments that are not discarded-
  // value expressions nor unevaluated operands (those cases get removed from
  // this list by CheckUnusedVolatileAssignment).
  for (auto *BO : Rec.VolatileAssignmentLHSs)
    Diag(BO->getBeginLoc(), diag::warn_deprecated_simple_assign_volatile)
        << BO->getType();

  // When are coming out of an unevaluated context, clear out any
  // temporaries that we may have created as part of the evaluation of
  // the expression in that context: they aren't relevant because they
  // will never be constructed.
  if (Rec.isUnevaluated() || Rec.isConstantEvaluated()) {
    ExprCleanupObjects.erase(ExprCleanupObjects.begin() + Rec.NumCleanupObjects,
                             ExprCleanupObjects.end());
    Cleanup = Rec.ParentCleanup;
    CleanupVarDeclMarking();
    std::swap(MaybeODRUseExprs, Rec.SavedMaybeODRUseExprs);
  // Otherwise, merge the contexts together.
  } else {
    Cleanup.mergeFrom(Rec.ParentCleanup);
    MaybeODRUseExprs.insert(Rec.SavedMaybeODRUseExprs.begin(),
                            Rec.SavedMaybeODRUseExprs.end());
  }

  // Pop the current expression evaluation context off the stack.
  ExprEvalContexts.pop_back();

  // The global expression evaluation context record is never popped.
  ExprEvalContexts.back().NumTypos += NumTypos;
}

void Sema::DiscardCleanupsInEvaluationContext() {
  ExprCleanupObjects.erase(
         ExprCleanupObjects.begin() + ExprEvalContexts.back().NumCleanupObjects,
         ExprCleanupObjects.end());
  Cleanup.reset();
  MaybeODRUseExprs.clear();
}

ExprResult Sema::HandleExprEvaluationContextForTypeof(Expr *E) {
  ExprResult Result = CheckPlaceholderExpr(E);
  if (Result.isInvalid())
    return ExprError();
  E = Result.get();
  if (!E->getType()->isVariablyModifiedType())
    return E;
  return TransformToPotentiallyEvaluated(E);
}

/// Are we in a context that is potentially constant evaluated per C++20
/// [expr.const]p12?
static bool isPotentiallyConstantEvaluatedContext(Sema &SemaRef) {
  /// C++2a [expr.const]p12:
  //   An expression or conversion is potentially constant evaluated if it is
  switch (SemaRef.ExprEvalContexts.back().Context) {
    case Sema::ExpressionEvaluationContext::ConstantEvaluated:
    case Sema::ExpressionEvaluationContext::ImmediateFunctionContext:

      // -- a manifestly constant-evaluated expression,
    case Sema::ExpressionEvaluationContext::PotentiallyEvaluated:
    case Sema::ExpressionEvaluationContext::PotentiallyEvaluatedIfUsed:
    case Sema::ExpressionEvaluationContext::DiscardedStatement:
      // -- a potentially-evaluated expression,
    case Sema::ExpressionEvaluationContext::UnevaluatedList:
      // -- an immediate subexpression of a braced-init-list,

      // -- [FIXME] an expression of the form & cast-expression that occurs
      //    within a templated entity
      // -- a subexpression of one of the above that is not a subexpression of
      // a nested unevaluated operand.
      return true;

    case Sema::ExpressionEvaluationContext::Unevaluated:
    case Sema::ExpressionEvaluationContext::UnevaluatedAbstract:
      // Expressions in this context are never evaluated.
      return false;
  }
  llvm_unreachable("Invalid context");
}

/// Return true if this function has a calling convention that requires mangling
/// in the size of the parameter pack.
static bool funcHasParameterSizeMangling(Sema &S, FunctionDecl *FD) {
  // These manglings don't do anything on non-Windows or non-x86 platforms, so
  // we don't need parameter type sizes.
  const llvm::Triple &TT = S.Context.getTargetInfo().getTriple();
  if (!TT.isOSWindows() || !TT.isX86())
    return false;

  // If this is C++ and this isn't an extern "C" function, parameters do not
  // need to be complete. In this case, C++ mangling will apply, which doesn't
  // use the size of the parameters.
  if (S.getLangOpts().CPlusPlus && !FD->isExternC())
    return false;

  // Stdcall, fastcall, and vectorcall need this special treatment.
  CallingConv CC = FD->getType()->castAs<FunctionType>()->getCallConv();
  switch (CC) {
  case CC_X86StdCall:
  case CC_X86FastCall:
  case CC_X86VectorCall:
    return true;
  default:
    break;
  }
  return false;
}

/// Require that all of the parameter types of function be complete. Normally,
/// parameter types are only required to be complete when a function is called
/// or defined, but to mangle functions with certain calling conventions, the
/// mangler needs to know the size of the parameter list. In this situation,
/// MSVC doesn't emit an error or instantiate templates. Instead, MSVC mangles
/// the function as _foo@0, i.e. zero bytes of parameters, which will usually
/// result in a linker error. Clang doesn't implement this behavior, and instead
/// attempts to error at compile time.
static void CheckCompleteParameterTypesForMangler(Sema &S, FunctionDecl *FD,
                                                  SourceLocation Loc) {
  class ParamIncompleteTypeDiagnoser : public Sema::TypeDiagnoser {
    FunctionDecl *FD;
    ParmVarDecl *Param;

  public:
    ParamIncompleteTypeDiagnoser(FunctionDecl *FD, ParmVarDecl *Param)
        : FD(FD), Param(Param) {}

    void diagnose(Sema &S, SourceLocation Loc, QualType T) override {
      CallingConv CC = FD->getType()->castAs<FunctionType>()->getCallConv();
      StringRef CCName;
      switch (CC) {
      case CC_X86StdCall:
        CCName = "stdcall";
        break;
      case CC_X86FastCall:
        CCName = "fastcall";
        break;
      case CC_X86VectorCall:
        CCName = "vectorcall";
        break;
      default:
        llvm_unreachable("CC does not need mangling");
      }

      S.Diag(Loc, diag::err_cconv_incomplete_param_type)
          << Param->getDeclName() << FD->getDeclName() << CCName;
    }
  };

  for (ParmVarDecl *Param : FD->parameters()) {
    ParamIncompleteTypeDiagnoser Diagnoser(FD, Param);
    S.RequireCompleteType(Loc, Param->getType(), Diagnoser);
  }
}

namespace {
enum class OdrUseContext {
  /// Declarations in this context are not odr-used.
  None,
  /// Declarations in this context are formally odr-used, but this is a
  /// dependent context.
  Dependent,
  /// Declarations in this context are odr-used but not actually used (yet).
  FormallyOdrUsed,
  /// Declarations in this context are used.
  Used
};
}

/// Are we within a context in which references to resolved functions or to
/// variables result in odr-use?
static OdrUseContext isOdrUseContext(Sema &SemaRef) {
  OdrUseContext Result;

  switch (SemaRef.ExprEvalContexts.back().Context) {
    case Sema::ExpressionEvaluationContext::Unevaluated:
    case Sema::ExpressionEvaluationContext::UnevaluatedList:
    case Sema::ExpressionEvaluationContext::UnevaluatedAbstract:
      return OdrUseContext::None;

    case Sema::ExpressionEvaluationContext::ConstantEvaluated:
    case Sema::ExpressionEvaluationContext::ImmediateFunctionContext:
    case Sema::ExpressionEvaluationContext::PotentiallyEvaluated:
      Result = OdrUseContext::Used;
      break;

    case Sema::ExpressionEvaluationContext::DiscardedStatement:
      Result = OdrUseContext::FormallyOdrUsed;
      break;

    case Sema::ExpressionEvaluationContext::PotentiallyEvaluatedIfUsed:
      // A default argument formally results in odr-use, but doesn't actually
      // result in a use in any real sense until it itself is used.
      Result = OdrUseContext::FormallyOdrUsed;
      break;
  }

  if (SemaRef.CurContext->isDependentContext())
    return OdrUseContext::Dependent;

  return Result;
}

static bool isImplicitlyDefinableConstexprFunction(FunctionDecl *Func) {
  if (!Func->isConstexpr())
    return false;

  if (Func->isImplicitlyInstantiable() || !Func->isUserProvided())
    return true;
  auto *CCD = dyn_cast<CXXConstructorDecl>(Func);
  return CCD && CCD->getInheritedConstructor();
}

void Sema::MarkFunctionReferenced(SourceLocation Loc, FunctionDecl *Func,
                                  bool MightBeOdrUse) {
  assert(Func && "No function?");

  Func->setReferenced();

  // Recursive functions aren't really used until they're used from some other
  // context.
  bool IsRecursiveCall = CurContext == Func;

  // C++11 [basic.def.odr]p3:
  //   A function whose name appears as a potentially-evaluated expression is
  //   odr-used if it is the unique lookup result or the selected member of a
  //   set of overloaded functions [...].
  //
  // We (incorrectly) mark overload resolution as an unevaluated context, so we
  // can just check that here.
  OdrUseContext OdrUse =
      MightBeOdrUse ? isOdrUseContext(*this) : OdrUseContext::None;
  if (IsRecursiveCall && OdrUse == OdrUseContext::Used)
    OdrUse = OdrUseContext::FormallyOdrUsed;

  // Trivial default constructors and destructors are never actually used.
  // FIXME: What about other special members?
  if (Func->isTrivial() && !Func->hasAttr<DLLExportAttr>() &&
      OdrUse == OdrUseContext::Used) {
    if (auto *Constructor = dyn_cast<CXXConstructorDecl>(Func))
      if (Constructor->isDefaultConstructor())
        OdrUse = OdrUseContext::FormallyOdrUsed;
    if (isa<CXXDestructorDecl>(Func))
      OdrUse = OdrUseContext::FormallyOdrUsed;
  }

  // C++20 [expr.const]p12:
  //   A function [...] is needed for constant evaluation if it is [...] a
  //   constexpr function that is named by an expression that is potentially
  //   constant evaluated
  bool NeededForConstantEvaluation =
      isPotentiallyConstantEvaluatedContext(*this) &&
      isImplicitlyDefinableConstexprFunction(Func);

  // Determine whether we require a function definition to exist, per
  // C++11 [temp.inst]p3:
  //   Unless a function template specialization has been explicitly
  //   instantiated or explicitly specialized, the function template
  //   specialization is implicitly instantiated when the specialization is
  //   referenced in a context that requires a function definition to exist.
  // C++20 [temp.inst]p7:
  //   The existence of a definition of a [...] function is considered to
  //   affect the semantics of the program if the [...] function is needed for
  //   constant evaluation by an expression
  // C++20 [basic.def.odr]p10:
  //   Every program shall contain exactly one definition of every non-inline
  //   function or variable that is odr-used in that program outside of a
  //   discarded statement
  // C++20 [special]p1:
  //   The implementation will implicitly define [defaulted special members]
  //   if they are odr-used or needed for constant evaluation.
  //
  // Note that we skip the implicit instantiation of templates that are only
  // used in unused default arguments or by recursive calls to themselves.
  // This is formally non-conforming, but seems reasonable in practice.
  bool NeedDefinition =
      !IsRecursiveCall &&
      (OdrUse == OdrUseContext::Used ||
       (NeededForConstantEvaluation && !Func->isPureVirtual()));

  // C++14 [temp.expl.spec]p6:
  //   If a template [...] is explicitly specialized then that specialization
  //   shall be declared before the first use of that specialization that would
  //   cause an implicit instantiation to take place, in every translation unit
  //   in which such a use occurs
  if (NeedDefinition &&
      (Func->getTemplateSpecializationKind() != TSK_Undeclared ||
       Func->getMemberSpecializationInfo()))
    checkSpecializationReachability(Loc, Func);

  if (getLangOpts().CUDA)
    CUDA().CheckCall(Loc, Func);

  // If we need a definition, try to create one.
  if (NeedDefinition && !Func->getBody()) {
    runWithSufficientStackSpace(Loc, [&] {
      if (CXXConstructorDecl *Constructor =
              dyn_cast<CXXConstructorDecl>(Func)) {
        Constructor = cast<CXXConstructorDecl>(Constructor->getFirstDecl());
        if (Constructor->isDefaulted() && !Constructor->isDeleted()) {
          if (Constructor->isDefaultConstructor()) {
            if (Constructor->isTrivial() &&
                !Constructor->hasAttr<DLLExportAttr>())
              return;
            DefineImplicitDefaultConstructor(Loc, Constructor);
          } else if (Constructor->isCopyConstructor()) {
            DefineImplicitCopyConstructor(Loc, Constructor);
          } else if (Constructor->isMoveConstructor()) {
            DefineImplicitMoveConstructor(Loc, Constructor);
          }
        } else if (Constructor->getInheritedConstructor()) {
          DefineInheritingConstructor(Loc, Constructor);
        }
      } else if (CXXDestructorDecl *Destructor =
                     dyn_cast<CXXDestructorDecl>(Func)) {
        Destructor = cast<CXXDestructorDecl>(Destructor->getFirstDecl());
        if (Destructor->isDefaulted() && !Destructor->isDeleted()) {
          if (Destructor->isTrivial() && !Destructor->hasAttr<DLLExportAttr>())
            return;
          DefineImplicitDestructor(Loc, Destructor);
        }
        if (Destructor->isVirtual() && getLangOpts().AppleKext)
          MarkVTableUsed(Loc, Destructor->getParent());
      } else if (CXXMethodDecl *MethodDecl = dyn_cast<CXXMethodDecl>(Func)) {
        if (MethodDecl->isOverloadedOperator() &&
            MethodDecl->getOverloadedOperator() == OO_Equal) {
          MethodDecl = cast<CXXMethodDecl>(MethodDecl->getFirstDecl());
          if (MethodDecl->isDefaulted() && !MethodDecl->isDeleted()) {
            if (MethodDecl->isCopyAssignmentOperator())
              DefineImplicitCopyAssignment(Loc, MethodDecl);
            else if (MethodDecl->isMoveAssignmentOperator())
              DefineImplicitMoveAssignment(Loc, MethodDecl);
          }
        } else if (isa<CXXConversionDecl>(MethodDecl) &&
                   MethodDecl->getParent()->isLambda()) {
          CXXConversionDecl *Conversion =
              cast<CXXConversionDecl>(MethodDecl->getFirstDecl());
          if (Conversion->isLambdaToBlockPointerConversion())
            DefineImplicitLambdaToBlockPointerConversion(Loc, Conversion);
          else
            DefineImplicitLambdaToFunctionPointerConversion(Loc, Conversion);
        } else if (MethodDecl->isVirtual() && getLangOpts().AppleKext)
          MarkVTableUsed(Loc, MethodDecl->getParent());
      }

      if (Func->isDefaulted() && !Func->isDeleted()) {
        DefaultedComparisonKind DCK = getDefaultedComparisonKind(Func);
        if (DCK != DefaultedComparisonKind::None)
          DefineDefaultedComparison(Loc, Func, DCK);
      }

      // Implicit instantiation of function templates and member functions of
      // class templates.
      if (Func->isImplicitlyInstantiable()) {
        TemplateSpecializationKind TSK =
            Func->getTemplateSpecializationKindForInstantiation();
        SourceLocation PointOfInstantiation = Func->getPointOfInstantiation();
        bool FirstInstantiation = PointOfInstantiation.isInvalid();
        if (FirstInstantiation) {
          PointOfInstantiation = Loc;
          if (auto *MSI = Func->getMemberSpecializationInfo())
            MSI->setPointOfInstantiation(Loc);
            // FIXME: Notify listener.
          else
            Func->setTemplateSpecializationKind(TSK, PointOfInstantiation);
        } else if (TSK != TSK_ImplicitInstantiation) {
          // Use the point of use as the point of instantiation, instead of the
          // point of explicit instantiation (which we track as the actual point
          // of instantiation). This gives better backtraces in diagnostics.
          PointOfInstantiation = Loc;
        }

        if (FirstInstantiation || TSK != TSK_ImplicitInstantiation ||
            Func->isConstexpr()) {
          if (isa<CXXRecordDecl>(Func->getDeclContext()) &&
              cast<CXXRecordDecl>(Func->getDeclContext())->isLocalClass() &&
              CodeSynthesisContexts.size())
            PendingLocalImplicitInstantiations.push_back(
                std::make_pair(Func, PointOfInstantiation));
          else if (Func->isConstexpr())
            // Do not defer instantiations of constexpr functions, to avoid the
            // expression evaluator needing to call back into Sema if it sees a
            // call to such a function.
            InstantiateFunctionDefinition(PointOfInstantiation, Func);
          else {
            Func->setInstantiationIsPending(true);
            PendingInstantiations.push_back(
                std::make_pair(Func, PointOfInstantiation));
            // Notify the consumer that a function was implicitly instantiated.
            Consumer.HandleCXXImplicitFunctionInstantiation(Func);
          }
        }
      } else {
        // Walk redefinitions, as some of them may be instantiable.
        for (auto *i : Func->redecls()) {
          if (!i->isUsed(false) && i->isImplicitlyInstantiable())
            MarkFunctionReferenced(Loc, i, MightBeOdrUse);
        }
      }
    });
  }

  // If a constructor was defined in the context of a default parameter
  // or of another default member initializer (ie a PotentiallyEvaluatedIfUsed
  // context), its initializers may not be referenced yet.
  if (CXXConstructorDecl *Constructor = dyn_cast<CXXConstructorDecl>(Func)) {
    EnterExpressionEvaluationContext EvalContext(
        *this,
        Constructor->isImmediateFunction()
            ? ExpressionEvaluationContext::ImmediateFunctionContext
            : ExpressionEvaluationContext::PotentiallyEvaluated,
        Constructor);
    for (CXXCtorInitializer *Init : Constructor->inits()) {
      if (Init->isInClassMemberInitializer())
        runWithSufficientStackSpace(Init->getSourceLocation(), [&]() {
          MarkDeclarationsReferencedInExpr(Init->getInit());
        });
    }
  }

  // C++14 [except.spec]p17:
  //   An exception-specification is considered to be needed when:
  //   - the function is odr-used or, if it appears in an unevaluated operand,
  //     would be odr-used if the expression were potentially-evaluated;
  //
  // Note, we do this even if MightBeOdrUse is false. That indicates that the
  // function is a pure virtual function we're calling, and in that case the
  // function was selected by overload resolution and we need to resolve its
  // exception specification for a different reason.
  const FunctionProtoType *FPT = Func->getType()->getAs<FunctionProtoType>();
  if (FPT && isUnresolvedExceptionSpec(FPT->getExceptionSpecType()))
    ResolveExceptionSpec(Loc, FPT);

  // A callee could be called by a host function then by a device function.
  // If we only try recording once, we will miss recording the use on device
  // side. Therefore keep trying until it is recorded.
  if (LangOpts.OffloadImplicitHostDeviceTemplates && LangOpts.CUDAIsDevice &&
      !getASTContext().CUDAImplicitHostDeviceFunUsedByDevice.count(Func))
    CUDA().RecordImplicitHostDeviceFuncUsedByDevice(Func);

  // If this is the first "real" use, act on that.
  if (OdrUse == OdrUseContext::Used && !Func->isUsed(/*CheckUsedAttr=*/false)) {
    // Keep track of used but undefined functions.
    if (!Func->isDefined()) {
      if (mightHaveNonExternalLinkage(Func))
        UndefinedButUsed.insert(std::make_pair(Func->getCanonicalDecl(), Loc));
      else if (Func->getMostRecentDecl()->isInlined() &&
               !LangOpts.GNUInline &&
               !Func->getMostRecentDecl()->hasAttr<GNUInlineAttr>())
        UndefinedButUsed.insert(std::make_pair(Func->getCanonicalDecl(), Loc));
      else if (isExternalWithNoLinkageType(Func))
        UndefinedButUsed.insert(std::make_pair(Func->getCanonicalDecl(), Loc));
    }

    // Some x86 Windows calling conventions mangle the size of the parameter
    // pack into the name. Computing the size of the parameters requires the
    // parameter types to be complete. Check that now.
    if (funcHasParameterSizeMangling(*this, Func))
      CheckCompleteParameterTypesForMangler(*this, Func, Loc);

    // In the MS C++ ABI, the compiler emits destructor variants where they are
    // used. If the destructor is used here but defined elsewhere, mark the
    // virtual base destructors referenced. If those virtual base destructors
    // are inline, this will ensure they are defined when emitting the complete
    // destructor variant. This checking may be redundant if the destructor is
    // provided later in this TU.
    if (Context.getTargetInfo().getCXXABI().isMicrosoft()) {
      if (auto *Dtor = dyn_cast<CXXDestructorDecl>(Func)) {
        CXXRecordDecl *Parent = Dtor->getParent();
        if (Parent->getNumVBases() > 0 && !Dtor->getBody())
          CheckCompleteDestructorVariant(Loc, Dtor);
      }
    }

    Func->markUsed(Context);
  }
}

/// Directly mark a variable odr-used. Given a choice, prefer to use
/// MarkVariableReferenced since it does additional checks and then
/// calls MarkVarDeclODRUsed.
/// If the variable must be captured:
///  - if FunctionScopeIndexToStopAt is null, capture it in the CurContext
///  - else capture it in the DeclContext that maps to the
///    *FunctionScopeIndexToStopAt on the FunctionScopeInfo stack.
static void
MarkVarDeclODRUsed(ValueDecl *V, SourceLocation Loc, Sema &SemaRef,
                   const unsigned *const FunctionScopeIndexToStopAt = nullptr) {
  // Keep track of used but undefined variables.
  // FIXME: We shouldn't suppress this warning for static data members.
  VarDecl *Var = V->getPotentiallyDecomposedVarDecl();
  assert(Var && "expected a capturable variable");

  if (Var->hasDefinition(SemaRef.Context) == VarDecl::DeclarationOnly &&
      (!Var->isExternallyVisible() || Var->isInline() ||
       SemaRef.isExternalWithNoLinkageType(Var)) &&
      !(Var->isStaticDataMember() && Var->hasInit())) {
    SourceLocation &old = SemaRef.UndefinedButUsed[Var->getCanonicalDecl()];
    if (old.isInvalid())
      old = Loc;
  }
  QualType CaptureType, DeclRefType;
  if (SemaRef.LangOpts.OpenMP)
    SemaRef.OpenMP().tryCaptureOpenMPLambdas(V);
  SemaRef.tryCaptureVariable(V, Loc, Sema::TryCapture_Implicit,
                             /*EllipsisLoc*/ SourceLocation(),
                             /*BuildAndDiagnose*/ true, CaptureType,
                             DeclRefType, FunctionScopeIndexToStopAt);

  if (SemaRef.LangOpts.CUDA && Var->hasGlobalStorage()) {
    auto *FD = dyn_cast_or_null<FunctionDecl>(SemaRef.CurContext);
    auto VarTarget = SemaRef.CUDA().IdentifyTarget(Var);
    auto UserTarget = SemaRef.CUDA().IdentifyTarget(FD);
    if (VarTarget == SemaCUDA::CVT_Host &&
        (UserTarget == CUDAFunctionTarget::Device ||
         UserTarget == CUDAFunctionTarget::HostDevice ||
         UserTarget == CUDAFunctionTarget::Global)) {
      // Diagnose ODR-use of host global variables in device functions.
      // Reference of device global variables in host functions is allowed
      // through shadow variables therefore it is not diagnosed.
      if (SemaRef.LangOpts.CUDAIsDevice && !SemaRef.LangOpts.HIPStdPar) {
        SemaRef.targetDiag(Loc, diag::err_ref_bad_target)
            << /*host*/ 2 << /*variable*/ 1 << Var
            << llvm::to_underlying(UserTarget);
        SemaRef.targetDiag(Var->getLocation(),
                           Var->getType().isConstQualified()
                               ? diag::note_cuda_const_var_unpromoted
                               : diag::note_cuda_host_var);
      }
    } else if (VarTarget == SemaCUDA::CVT_Device &&
               !Var->hasAttr<CUDASharedAttr>() &&
               (UserTarget == CUDAFunctionTarget::Host ||
                UserTarget == CUDAFunctionTarget::HostDevice)) {
      // Record a CUDA/HIP device side variable if it is ODR-used
      // by host code. This is done conservatively, when the variable is
      // referenced in any of the following contexts:
      //   - a non-function context
      //   - a host function
      //   - a host device function
      // This makes the ODR-use of the device side variable by host code to
      // be visible in the device compilation for the compiler to be able to
      // emit template variables instantiated by host code only and to
      // externalize the static device side variable ODR-used by host code.
      if (!Var->hasExternalStorage())
        SemaRef.getASTContext().CUDADeviceVarODRUsedByHost.insert(Var);
      else if (SemaRef.LangOpts.GPURelocatableDeviceCode &&
               (!FD || (!FD->getDescribedFunctionTemplate() &&
                        SemaRef.getASTContext().GetGVALinkageForFunction(FD) ==
                            GVA_StrongExternal)))
        SemaRef.getASTContext().CUDAExternalDeviceDeclODRUsedByHost.insert(Var);
    }
  }

  V->markUsed(SemaRef.Context);
}

void Sema::MarkCaptureUsedInEnclosingContext(ValueDecl *Capture,
                                             SourceLocation Loc,
                                             unsigned CapturingScopeIndex) {
  MarkVarDeclODRUsed(Capture, Loc, *this, &CapturingScopeIndex);
}

void diagnoseUncapturableValueReferenceOrBinding(Sema &S, SourceLocation loc,
                                                 ValueDecl *var) {
  DeclContext *VarDC = var->getDeclContext();

  //  If the parameter still belongs to the translation unit, then
  //  we're actually just using one parameter in the declaration of
  //  the next.
  if (isa<ParmVarDecl>(var) &&
      isa<TranslationUnitDecl>(VarDC))
    return;

  // For C code, don't diagnose about capture if we're not actually in code
  // right now; it's impossible to write a non-constant expression outside of
  // function context, so we'll get other (more useful) diagnostics later.
  //
  // For C++, things get a bit more nasty... it would be nice to suppress this
  // diagnostic for certain cases like using a local variable in an array bound
  // for a member of a local class, but the correct predicate is not obvious.
  if (!S.getLangOpts().CPlusPlus && !S.CurContext->isFunctionOrMethod())
    return;

  unsigned ValueKind = isa<BindingDecl>(var) ? 1 : 0;
  unsigned ContextKind = 3; // unknown
  if (isa<CXXMethodDecl>(VarDC) &&
      cast<CXXRecordDecl>(VarDC->getParent())->isLambda()) {
    ContextKind = 2;
  } else if (isa<FunctionDecl>(VarDC)) {
    ContextKind = 0;
  } else if (isa<BlockDecl>(VarDC)) {
    ContextKind = 1;
  }

  S.Diag(loc, diag::err_reference_to_local_in_enclosing_context)
    << var << ValueKind << ContextKind << VarDC;
  S.Diag(var->getLocation(), diag::note_entity_declared_at)
      << var;

  // FIXME: Add additional diagnostic info about class etc. which prevents
  // capture.
}

static bool isVariableAlreadyCapturedInScopeInfo(CapturingScopeInfo *CSI,
                                                 ValueDecl *Var,
                                                 bool &SubCapturesAreNested,
                                                 QualType &CaptureType,
                                                 QualType &DeclRefType) {
  // Check whether we've already captured it.
  if (CSI->CaptureMap.count(Var)) {
    // If we found a capture, any subcaptures are nested.
    SubCapturesAreNested = true;

    // Retrieve the capture type for this variable.
    CaptureType = CSI->getCapture(Var).getCaptureType();

    // Compute the type of an expression that refers to this variable.
    DeclRefType = CaptureType.getNonReferenceType();

    // Similarly to mutable captures in lambda, all the OpenMP captures by copy
    // are mutable in the sense that user can change their value - they are
    // private instances of the captured declarations.
    const Capture &Cap = CSI->getCapture(Var);
    if (Cap.isCopyCapture() &&
        !(isa<LambdaScopeInfo>(CSI) &&
          !cast<LambdaScopeInfo>(CSI)->lambdaCaptureShouldBeConst()) &&
        !(isa<CapturedRegionScopeInfo>(CSI) &&
          cast<CapturedRegionScopeInfo>(CSI)->CapRegionKind == CR_OpenMP))
      DeclRefType.addConst();
    return true;
  }
  return false;
}

// Only block literals, captured statements, and lambda expressions can
// capture; other scopes don't work.
static DeclContext *getParentOfCapturingContextOrNull(DeclContext *DC,
                                                      ValueDecl *Var,
                                                      SourceLocation Loc,
                                                      const bool Diagnose,
                                                      Sema &S) {
  if (isa<BlockDecl>(DC) || isa<CapturedDecl>(DC) || isLambdaCallOperator(DC))
    return getLambdaAwareParentOfDeclContext(DC);

  VarDecl *Underlying = Var->getPotentiallyDecomposedVarDecl();
  if (Underlying) {
    if (Underlying->hasLocalStorage() && Diagnose)
      diagnoseUncapturableValueReferenceOrBinding(S, Loc, Var);
  }
  return nullptr;
}

// Certain capturing entities (lambdas, blocks etc.) are not allowed to capture
// certain types of variables (unnamed, variably modified types etc.)
// so check for eligibility.
static bool isVariableCapturable(CapturingScopeInfo *CSI, ValueDecl *Var,
                                 SourceLocation Loc, const bool Diagnose,
                                 Sema &S) {

  assert((isa<VarDecl, BindingDecl>(Var)) &&
         "Only variables and structured bindings can be captured");

  bool IsBlock = isa<BlockScopeInfo>(CSI);
  bool IsLambda = isa<LambdaScopeInfo>(CSI);

  // Lambdas are not allowed to capture unnamed variables
  // (e.g. anonymous unions).
  // FIXME: The C++11 rule don't actually state this explicitly, but I'm
  // assuming that's the intent.
  if (IsLambda && !Var->getDeclName()) {
    if (Diagnose) {
      S.Diag(Loc, diag::err_lambda_capture_anonymous_var);
      S.Diag(Var->getLocation(), diag::note_declared_at);
    }
    return false;
  }

  // Prohibit variably-modified types in blocks; they're difficult to deal with.
  if (Var->getType()->isVariablyModifiedType() && IsBlock) {
    if (Diagnose) {
      S.Diag(Loc, diag::err_ref_vm_type);
      S.Diag(Var->getLocation(), diag::note_previous_decl) << Var;
    }
    return false;
  }
  // Prohibit structs with flexible array members too.
  // We cannot capture what is in the tail end of the struct.
  if (const RecordType *VTTy = Var->getType()->getAs<RecordType>()) {
    if (VTTy->getDecl()->hasFlexibleArrayMember()) {
      if (Diagnose) {
        if (IsBlock)
          S.Diag(Loc, diag::err_ref_flexarray_type);
        else
          S.Diag(Loc, diag::err_lambda_capture_flexarray_type) << Var;
        S.Diag(Var->getLocation(), diag::note_previous_decl) << Var;
      }
      return false;
    }
  }
  const bool HasBlocksAttr = Var->hasAttr<BlocksAttr>();
  // Lambdas and captured statements are not allowed to capture __block
  // variables; they don't support the expected semantics.
  if (HasBlocksAttr && (IsLambda || isa<CapturedRegionScopeInfo>(CSI))) {
    if (Diagnose) {
      S.Diag(Loc, diag::err_capture_block_variable) << Var << !IsLambda;
      S.Diag(Var->getLocation(), diag::note_previous_decl) << Var;
    }
    return false;
  }
  // OpenCL v2.0 s6.12.5: Blocks cannot reference/capture other blocks
  if (S.getLangOpts().OpenCL && IsBlock &&
      Var->getType()->isBlockPointerType()) {
    if (Diagnose)
      S.Diag(Loc, diag::err_opencl_block_ref_block);
    return false;
  }

  if (isa<BindingDecl>(Var)) {
    if (!IsLambda || !S.getLangOpts().CPlusPlus) {
      if (Diagnose)
        diagnoseUncapturableValueReferenceOrBinding(S, Loc, Var);
      return false;
    } else if (Diagnose && S.getLangOpts().CPlusPlus) {
      S.Diag(Loc, S.LangOpts.CPlusPlus20
                      ? diag::warn_cxx17_compat_capture_binding
                      : diag::ext_capture_binding)
          << Var;
      S.Diag(Var->getLocation(), diag::note_entity_declared_at) << Var;
    }
  }

  return true;
}

// Returns true if the capture by block was successful.
static bool captureInBlock(BlockScopeInfo *BSI, ValueDecl *Var,
                           SourceLocation Loc, const bool BuildAndDiagnose,
                           QualType &CaptureType, QualType &DeclRefType,
                           const bool Nested, Sema &S, bool Invalid) {
  bool ByRef = false;

  // Blocks are not allowed to capture arrays, excepting OpenCL.
  // OpenCL v2.0 s1.12.5 (revision 40): arrays are captured by reference
  // (decayed to pointers).
  if (!Invalid && !S.getLangOpts().OpenCL && CaptureType->isArrayType()) {
    if (BuildAndDiagnose) {
      S.Diag(Loc, diag::err_ref_array_type);
      S.Diag(Var->getLocation(), diag::note_previous_decl) << Var;
      Invalid = true;
    } else {
      return false;
    }
  }

  // Forbid the block-capture of autoreleasing variables.
  if (!Invalid &&
      CaptureType.getObjCLifetime() == Qualifiers::OCL_Autoreleasing) {
    if (BuildAndDiagnose) {
      S.Diag(Loc, diag::err_arc_autoreleasing_capture)
        << /*block*/ 0;
      S.Diag(Var->getLocation(), diag::note_previous_decl) << Var;
      Invalid = true;
    } else {
      return false;
    }
  }

  // Warn about implicitly autoreleasing indirect parameters captured by blocks.
  if (const auto *PT = CaptureType->getAs<PointerType>()) {
    QualType PointeeTy = PT->getPointeeType();

    if (!Invalid && PointeeTy->getAs<ObjCObjectPointerType>() &&
        PointeeTy.getObjCLifetime() == Qualifiers::OCL_Autoreleasing &&
        !S.Context.hasDirectOwnershipQualifier(PointeeTy)) {
      if (BuildAndDiagnose) {
        SourceLocation VarLoc = Var->getLocation();
        S.Diag(Loc, diag::warn_block_capture_autoreleasing);
        S.Diag(VarLoc, diag::note_declare_parameter_strong);
      }
    }
  }

  const bool HasBlocksAttr = Var->hasAttr<BlocksAttr>();
  if (HasBlocksAttr || CaptureType->isReferenceType() ||
      (S.getLangOpts().OpenMP && S.OpenMP().isOpenMPCapturedDecl(Var))) {
    // Block capture by reference does not change the capture or
    // declaration reference types.
    ByRef = true;
  } else {
    // Block capture by copy introduces 'const'.
    CaptureType = CaptureType.getNonReferenceType().withConst();
    DeclRefType = CaptureType;
  }

  // Actually capture the variable.
  if (BuildAndDiagnose)
    BSI->addCapture(Var, HasBlocksAttr, ByRef, Nested, Loc, SourceLocation(),
                    CaptureType, Invalid);

  return !Invalid;
}

/// Capture the given variable in the captured region.
static bool captureInCapturedRegion(
    CapturedRegionScopeInfo *RSI, ValueDecl *Var, SourceLocation Loc,
    const bool BuildAndDiagnose, QualType &CaptureType, QualType &DeclRefType,
    const bool RefersToCapturedVariable, Sema::TryCaptureKind Kind,
    bool IsTopScope, Sema &S, bool Invalid) {
  // By default, capture variables by reference.
  bool ByRef = true;
  if (IsTopScope && Kind != Sema::TryCapture_Implicit) {
    ByRef = (Kind == Sema::TryCapture_ExplicitByRef);
  } else if (S.getLangOpts().OpenMP && RSI->CapRegionKind == CR_OpenMP) {
    // Using an LValue reference type is consistent with Lambdas (see below).
    if (S.OpenMP().isOpenMPCapturedDecl(Var)) {
      bool HasConst = DeclRefType.isConstQualified();
      DeclRefType = DeclRefType.getUnqualifiedType();
      // Don't lose diagnostics about assignments to const.
      if (HasConst)
        DeclRefType.addConst();
    }
    // Do not capture firstprivates in tasks.
    if (S.OpenMP().isOpenMPPrivateDecl(Var, RSI->OpenMPLevel,
                                       RSI->OpenMPCaptureLevel) != OMPC_unknown)
      return true;
    ByRef = S.OpenMP().isOpenMPCapturedByRef(Var, RSI->OpenMPLevel,
                                             RSI->OpenMPCaptureLevel);
  }

  if (ByRef)
    CaptureType = S.Context.getLValueReferenceType(DeclRefType);
  else
    CaptureType = DeclRefType;

  // Actually capture the variable.
  if (BuildAndDiagnose)
    RSI->addCapture(Var, /*isBlock*/ false, ByRef, RefersToCapturedVariable,
                    Loc, SourceLocation(), CaptureType, Invalid);

  return !Invalid;
}

/// Capture the given variable in the lambda.
static bool captureInLambda(LambdaScopeInfo *LSI, ValueDecl *Var,
                            SourceLocation Loc, const bool BuildAndDiagnose,
                            QualType &CaptureType, QualType &DeclRefType,
                            const bool RefersToCapturedVariable,
                            const Sema::TryCaptureKind Kind,
                            SourceLocation EllipsisLoc, const bool IsTopScope,
                            Sema &S, bool Invalid) {
  // Determine whether we are capturing by reference or by value.
  bool ByRef = false;
  if (IsTopScope && Kind != Sema::TryCapture_Implicit) {
    ByRef = (Kind == Sema::TryCapture_ExplicitByRef);
  } else {
    ByRef = (LSI->ImpCaptureStyle == LambdaScopeInfo::ImpCap_LambdaByref);
  }

  if (BuildAndDiagnose && S.Context.getTargetInfo().getTriple().isWasm() &&
      CaptureType.getNonReferenceType().isWebAssemblyReferenceType()) {
    S.Diag(Loc, diag::err_wasm_ca_reference) << 0;
    Invalid = true;
  }

  // Compute the type of the field that will capture this variable.
  if (ByRef) {
    // C++11 [expr.prim.lambda]p15:
    //   An entity is captured by reference if it is implicitly or
    //   explicitly captured but not captured by copy. It is
    //   unspecified whether additional unnamed non-static data
    //   members are declared in the closure type for entities
    //   captured by reference.
    //
    // FIXME: It is not clear whether we want to build an lvalue reference
    // to the DeclRefType or to CaptureType.getNonReferenceType(). GCC appears
    // to do the former, while EDG does the latter. Core issue 1249 will
    // clarify, but for now we follow GCC because it's a more permissive and
    // easily defensible position.
    CaptureType = S.Context.getLValueReferenceType(DeclRefType);
  } else {
    // C++11 [expr.prim.lambda]p14:
    //   For each entity captured by copy, an unnamed non-static
    //   data member is declared in the closure type. The
    //   declaration order of these members is unspecified. The type
    //   of such a data member is the type of the corresponding
    //   captured entity if the entity is not a reference to an
    //   object, or the referenced type otherwise. [Note: If the
    //   captured entity is a reference to a function, the
    //   corresponding data member is also a reference to a
    //   function. - end note ]
    if (const ReferenceType *RefType = CaptureType->getAs<ReferenceType>()){
      if (!RefType->getPointeeType()->isFunctionType())
        CaptureType = RefType->getPointeeType();
    }

    // Forbid the lambda copy-capture of autoreleasing variables.
    if (!Invalid &&
        CaptureType.getObjCLifetime() == Qualifiers::OCL_Autoreleasing) {
      if (BuildAndDiagnose) {
        S.Diag(Loc, diag::err_arc_autoreleasing_capture) << /*lambda*/ 1;
        S.Diag(Var->getLocation(), diag::note_previous_decl)
          << Var->getDeclName();
        Invalid = true;
      } else {
        return false;
      }
    }

    // Make sure that by-copy captures are of a complete and non-abstract type.
    if (!Invalid && BuildAndDiagnose) {
      if (!CaptureType->isDependentType() &&
          S.RequireCompleteSizedType(
              Loc, CaptureType,
              diag::err_capture_of_incomplete_or_sizeless_type,
              Var->getDeclName()))
        Invalid = true;
      else if (S.RequireNonAbstractType(Loc, CaptureType,
                                        diag::err_capture_of_abstract_type))
        Invalid = true;
    }
  }

  // Compute the type of a reference to this captured variable.
  if (ByRef)
    DeclRefType = CaptureType.getNonReferenceType();
  else {
    // C++ [expr.prim.lambda]p5:
    //   The closure type for a lambda-expression has a public inline
    //   function call operator [...]. This function call operator is
    //   declared const (9.3.1) if and only if the lambda-expression's
    //   parameter-declaration-clause is not followed by mutable.
    DeclRefType = CaptureType.getNonReferenceType();
    bool Const = LSI->lambdaCaptureShouldBeConst();
    if (Const && !CaptureType->isReferenceType())
      DeclRefType.addConst();
  }

  // Add the capture.
  if (BuildAndDiagnose)
    LSI->addCapture(Var, /*isBlock=*/false, ByRef, RefersToCapturedVariable,
                    Loc, EllipsisLoc, CaptureType, Invalid);

  return !Invalid;
}

static bool canCaptureVariableByCopy(ValueDecl *Var,
                                     const ASTContext &Context) {
  // Offer a Copy fix even if the type is dependent.
  if (Var->getType()->isDependentType())
    return true;
  QualType T = Var->getType().getNonReferenceType();
  if (T.isTriviallyCopyableType(Context))
    return true;
  if (CXXRecordDecl *RD = T->getAsCXXRecordDecl()) {

    if (!(RD = RD->getDefinition()))
      return false;
    if (RD->hasSimpleCopyConstructor())
      return true;
    if (RD->hasUserDeclaredCopyConstructor())
      for (CXXConstructorDecl *Ctor : RD->ctors())
        if (Ctor->isCopyConstructor())
          return !Ctor->isDeleted();
  }
  return false;
}

/// Create up to 4 fix-its for explicit reference and value capture of \p Var or
/// default capture. Fixes may be omitted if they aren't allowed by the
/// standard, for example we can't emit a default copy capture fix-it if we
/// already explicitly copy capture capture another variable.
static void buildLambdaCaptureFixit(Sema &Sema, LambdaScopeInfo *LSI,
                                    ValueDecl *Var) {
  assert(LSI->ImpCaptureStyle == CapturingScopeInfo::ImpCap_None);
  // Don't offer Capture by copy of default capture by copy fixes if Var is
  // known not to be copy constructible.
  bool ShouldOfferCopyFix = canCaptureVariableByCopy(Var, Sema.getASTContext());

  SmallString<32> FixBuffer;
  StringRef Separator = LSI->NumExplicitCaptures > 0 ? ", " : "";
  if (Var->getDeclName().isIdentifier() && !Var->getName().empty()) {
    SourceLocation VarInsertLoc = LSI->IntroducerRange.getEnd();
    if (ShouldOfferCopyFix) {
      // Offer fixes to insert an explicit capture for the variable.
      // [] -> [VarName]
      // [OtherCapture] -> [OtherCapture, VarName]
      FixBuffer.assign({Separator, Var->getName()});
      Sema.Diag(VarInsertLoc, diag::note_lambda_variable_capture_fixit)
          << Var << /*value*/ 0
          << FixItHint::CreateInsertion(VarInsertLoc, FixBuffer);
    }
    // As above but capture by reference.
    FixBuffer.assign({Separator, "&", Var->getName()});
    Sema.Diag(VarInsertLoc, diag::note_lambda_variable_capture_fixit)
        << Var << /*reference*/ 1
        << FixItHint::CreateInsertion(VarInsertLoc, FixBuffer);
  }

  // Only try to offer default capture if there are no captures excluding this
  // and init captures.
  // [this]: OK.
  // [X = Y]: OK.
  // [&A, &B]: Don't offer.
  // [A, B]: Don't offer.
  if (llvm::any_of(LSI->Captures, [](Capture &C) {
        return !C.isThisCapture() && !C.isInitCapture();
      }))
    return;

  // The default capture specifiers, '=' or '&', must appear first in the
  // capture body.
  SourceLocation DefaultInsertLoc =
      LSI->IntroducerRange.getBegin().getLocWithOffset(1);

  if (ShouldOfferCopyFix) {
    bool CanDefaultCopyCapture = true;
    // [=, *this] OK since c++17
    // [=, this] OK since c++20
    if (LSI->isCXXThisCaptured() && !Sema.getLangOpts().CPlusPlus20)
      CanDefaultCopyCapture = Sema.getLangOpts().CPlusPlus17
                                  ? LSI->getCXXThisCapture().isCopyCapture()
                                  : false;
    // We can't use default capture by copy if any captures already specified
    // capture by copy.
    if (CanDefaultCopyCapture && llvm::none_of(LSI->Captures, [](Capture &C) {
          return !C.isThisCapture() && !C.isInitCapture() && C.isCopyCapture();
        })) {
      FixBuffer.assign({"=", Separator});
      Sema.Diag(DefaultInsertLoc, diag::note_lambda_default_capture_fixit)
          << /*value*/ 0
          << FixItHint::CreateInsertion(DefaultInsertLoc, FixBuffer);
    }
  }

  // We can't use default capture by reference if any captures already specified
  // capture by reference.
  if (llvm::none_of(LSI->Captures, [](Capture &C) {
        return !C.isInitCapture() && C.isReferenceCapture() &&
               !C.isThisCapture();
      })) {
    FixBuffer.assign({"&", Separator});
    Sema.Diag(DefaultInsertLoc, diag::note_lambda_default_capture_fixit)
        << /*reference*/ 1
        << FixItHint::CreateInsertion(DefaultInsertLoc, FixBuffer);
  }
}

bool Sema::tryCaptureVariable(
    ValueDecl *Var, SourceLocation ExprLoc, TryCaptureKind Kind,
    SourceLocation EllipsisLoc, bool BuildAndDiagnose, QualType &CaptureType,
    QualType &DeclRefType, const unsigned *const FunctionScopeIndexToStopAt) {
  // An init-capture is notionally from the context surrounding its
  // declaration, but its parent DC is the lambda class.
  DeclContext *VarDC = Var->getDeclContext();
  DeclContext *DC = CurContext;

  // Skip past RequiresExprBodys because they don't constitute function scopes.
  while (DC->isRequiresExprBody())
    DC = DC->getParent();

  // tryCaptureVariable is called every time a DeclRef is formed,
  // it can therefore have non-negigible impact on performances.
  // For local variables and when there is no capturing scope,
  // we can bailout early.
  if (CapturingFunctionScopes == 0 && (!BuildAndDiagnose || VarDC == DC))
    return true;

  // Exception: Function parameters are not tied to the function's DeclContext
  // until we enter the function definition. Capturing them anyway would result
  // in an out-of-bounds error while traversing DC and its parents.
  if (isa<ParmVarDecl>(Var) && !VarDC->isFunctionOrMethod())
    return true;

  const auto *VD = dyn_cast<VarDecl>(Var);
  if (VD) {
    if (VD->isInitCapture())
      VarDC = VarDC->getParent();
  } else {
    VD = Var->getPotentiallyDecomposedVarDecl();
  }
  assert(VD && "Cannot capture a null variable");

  const unsigned MaxFunctionScopesIndex = FunctionScopeIndexToStopAt
      ? *FunctionScopeIndexToStopAt : FunctionScopes.size() - 1;
  // We need to sync up the Declaration Context with the
  // FunctionScopeIndexToStopAt
  if (FunctionScopeIndexToStopAt) {
    unsigned FSIndex = FunctionScopes.size() - 1;
    while (FSIndex != MaxFunctionScopesIndex) {
      DC = getLambdaAwareParentOfDeclContext(DC);
      --FSIndex;
    }
  }

  // Capture global variables if it is required to use private copy of this
  // variable.
  bool IsGlobal = !VD->hasLocalStorage();
  if (IsGlobal && !(LangOpts.OpenMP &&
                    OpenMP().isOpenMPCapturedDecl(Var, /*CheckScopeInfo=*/true,
                                                  MaxFunctionScopesIndex)))
    return true;

  if (isa<VarDecl>(Var))
    Var = cast<VarDecl>(Var->getCanonicalDecl());

  // Walk up the stack to determine whether we can capture the variable,
  // performing the "simple" checks that don't depend on type. We stop when
  // we've either hit the declared scope of the variable or find an existing
  // capture of that variable.  We start from the innermost capturing-entity
  // (the DC) and ensure that all intervening capturing-entities
  // (blocks/lambdas etc.) between the innermost capturer and the variable`s
  // declcontext can either capture the variable or have already captured
  // the variable.
  CaptureType = Var->getType();
  DeclRefType = CaptureType.getNonReferenceType();
  bool Nested = false;
  bool Explicit = (Kind != TryCapture_Implicit);
  unsigned FunctionScopesIndex = MaxFunctionScopesIndex;
  do {

    LambdaScopeInfo *LSI = nullptr;
    if (!FunctionScopes.empty())
      LSI = dyn_cast_or_null<LambdaScopeInfo>(
          FunctionScopes[FunctionScopesIndex]);

    bool IsInScopeDeclarationContext =
        !LSI || LSI->AfterParameterList || CurContext == LSI->CallOperator;

    if (LSI && !LSI->AfterParameterList) {
      // This allows capturing parameters from a default value which does not
      // seems correct
      if (isa<ParmVarDecl>(Var) && !Var->getDeclContext()->isFunctionOrMethod())
        return true;
    }
    // If the variable is declared in the current context, there is no need to
    // capture it.
    if (IsInScopeDeclarationContext &&
        FunctionScopesIndex == MaxFunctionScopesIndex && VarDC == DC)
      return true;

    // Only block literals, captured statements, and lambda expressions can
    // capture; other scopes don't work.
    DeclContext *ParentDC =
        !IsInScopeDeclarationContext
            ? DC->getParent()
            : getParentOfCapturingContextOrNull(DC, Var, ExprLoc,
                                                BuildAndDiagnose, *this);
    // We need to check for the parent *first* because, if we *have*
    // private-captured a global variable, we need to recursively capture it in
    // intermediate blocks, lambdas, etc.
    if (!ParentDC) {
      if (IsGlobal) {
        FunctionScopesIndex = MaxFunctionScopesIndex - 1;
        break;
      }
      return true;
    }

    FunctionScopeInfo  *FSI = FunctionScopes[FunctionScopesIndex];
    CapturingScopeInfo *CSI = cast<CapturingScopeInfo>(FSI);

    // Check whether we've already captured it.
    if (isVariableAlreadyCapturedInScopeInfo(CSI, Var, Nested, CaptureType,
                                             DeclRefType)) {
      CSI->getCapture(Var).markUsed(BuildAndDiagnose);
      break;
    }

    // When evaluating some attributes (like enable_if) we might refer to a
    // function parameter appertaining to the same declaration as that
    // attribute.
    if (const auto *Parm = dyn_cast<ParmVarDecl>(Var);
        Parm && Parm->getDeclContext() == DC)
      return true;

    // If we are instantiating a generic lambda call operator body,
    // we do not want to capture new variables.  What was captured
    // during either a lambdas transformation or initial parsing
    // should be used.
    if (isGenericLambdaCallOperatorSpecialization(DC)) {
      if (BuildAndDiagnose) {
        LambdaScopeInfo *LSI = cast<LambdaScopeInfo>(CSI);
        if (LSI->ImpCaptureStyle == CapturingScopeInfo::ImpCap_None) {
          Diag(ExprLoc, diag::err_lambda_impcap) << Var;
          Diag(Var->getLocation(), diag::note_previous_decl) << Var;
          Diag(LSI->Lambda->getBeginLoc(), diag::note_lambda_decl);
          buildLambdaCaptureFixit(*this, LSI, Var);
        } else
          diagnoseUncapturableValueReferenceOrBinding(*this, ExprLoc, Var);
      }
      return true;
    }

    // Try to capture variable-length arrays types.
    if (Var->getType()->isVariablyModifiedType()) {
      // We're going to walk down into the type and look for VLA
      // expressions.
      QualType QTy = Var->getType();
      if (ParmVarDecl *PVD = dyn_cast_or_null<ParmVarDecl>(Var))
        QTy = PVD->getOriginalType();
      captureVariablyModifiedType(Context, QTy, CSI);
    }

    if (getLangOpts().OpenMP) {
      if (auto *RSI = dyn_cast<CapturedRegionScopeInfo>(CSI)) {
        // OpenMP private variables should not be captured in outer scope, so
        // just break here. Similarly, global variables that are captured in a
        // target region should not be captured outside the scope of the region.
        if (RSI->CapRegionKind == CR_OpenMP) {
          // FIXME: We should support capturing structured bindings in OpenMP.
          if (isa<BindingDecl>(Var)) {
            if (BuildAndDiagnose) {
              Diag(ExprLoc, diag::err_capture_binding_openmp) << Var;
              Diag(Var->getLocation(), diag::note_entity_declared_at) << Var;
            }
            return true;
          }
          OpenMPClauseKind IsOpenMPPrivateDecl = OpenMP().isOpenMPPrivateDecl(
              Var, RSI->OpenMPLevel, RSI->OpenMPCaptureLevel);
          // If the variable is private (i.e. not captured) and has variably
          // modified type, we still need to capture the type for correct
          // codegen in all regions, associated with the construct. Currently,
          // it is captured in the innermost captured region only.
          if (IsOpenMPPrivateDecl != OMPC_unknown &&
              Var->getType()->isVariablyModifiedType()) {
            QualType QTy = Var->getType();
            if (ParmVarDecl *PVD = dyn_cast_or_null<ParmVarDecl>(Var))
              QTy = PVD->getOriginalType();
            for (int I = 1,
                     E = OpenMP().getNumberOfConstructScopes(RSI->OpenMPLevel);
                 I < E; ++I) {
              auto *OuterRSI = cast<CapturedRegionScopeInfo>(
                  FunctionScopes[FunctionScopesIndex - I]);
              assert(RSI->OpenMPLevel == OuterRSI->OpenMPLevel &&
                     "Wrong number of captured regions associated with the "
                     "OpenMP construct.");
              captureVariablyModifiedType(Context, QTy, OuterRSI);
            }
          }
          bool IsTargetCap =
              IsOpenMPPrivateDecl != OMPC_private &&
              OpenMP().isOpenMPTargetCapturedDecl(Var, RSI->OpenMPLevel,
                                                  RSI->OpenMPCaptureLevel);
          // Do not capture global if it is not privatized in outer regions.
          bool IsGlobalCap =
              IsGlobal && OpenMP().isOpenMPGlobalCapturedDecl(
                              Var, RSI->OpenMPLevel, RSI->OpenMPCaptureLevel);

          // When we detect target captures we are looking from inside the
          // target region, therefore we need to propagate the capture from the
          // enclosing region. Therefore, the capture is not initially nested.
          if (IsTargetCap)
            OpenMP().adjustOpenMPTargetScopeIndex(FunctionScopesIndex,
                                                  RSI->OpenMPLevel);

          if (IsTargetCap || IsOpenMPPrivateDecl == OMPC_private ||
              (IsGlobal && !IsGlobalCap)) {
            Nested = !IsTargetCap;
            bool HasConst = DeclRefType.isConstQualified();
            DeclRefType = DeclRefType.getUnqualifiedType();
            // Don't lose diagnostics about assignments to const.
            if (HasConst)
              DeclRefType.addConst();
            CaptureType = Context.getLValueReferenceType(DeclRefType);
            break;
          }
        }
      }
    }
    if (CSI->ImpCaptureStyle == CapturingScopeInfo::ImpCap_None && !Explicit) {
      // No capture-default, and this is not an explicit capture
      // so cannot capture this variable.
      if (BuildAndDiagnose) {
        Diag(ExprLoc, diag::err_lambda_impcap) << Var;
        Diag(Var->getLocation(), diag::note_previous_decl) << Var;
        auto *LSI = cast<LambdaScopeInfo>(CSI);
        if (LSI->Lambda) {
          Diag(LSI->Lambda->getBeginLoc(), diag::note_lambda_decl);
          buildLambdaCaptureFixit(*this, LSI, Var);
        }
        // FIXME: If we error out because an outer lambda can not implicitly
        // capture a variable that an inner lambda explicitly captures, we
        // should have the inner lambda do the explicit capture - because
        // it makes for cleaner diagnostics later.  This would purely be done
        // so that the diagnostic does not misleadingly claim that a variable
        // can not be captured by a lambda implicitly even though it is captured
        // explicitly.  Suggestion:
        //  - create const bool VariableCaptureWasInitiallyExplicit = Explicit
        //    at the function head
        //  - cache the StartingDeclContext - this must be a lambda
        //  - captureInLambda in the innermost lambda the variable.
      }
      return true;
    }
    Explicit = false;
    FunctionScopesIndex--;
    if (IsInScopeDeclarationContext)
      DC = ParentDC;
  } while (!VarDC->Equals(DC));

  // Walk back down the scope stack, (e.g. from outer lambda to inner lambda)
  // computing the type of the capture at each step, checking type-specific
  // requirements, and adding captures if requested.
  // If the variable had already been captured previously, we start capturing
  // at the lambda nested within that one.
  bool Invalid = false;
  for (unsigned I = ++FunctionScopesIndex, N = MaxFunctionScopesIndex + 1; I != N;
       ++I) {
    CapturingScopeInfo *CSI = cast<CapturingScopeInfo>(FunctionScopes[I]);

    // Certain capturing entities (lambdas, blocks etc.) are not allowed to capture
    // certain types of variables (unnamed, variably modified types etc.)
    // so check for eligibility.
    if (!Invalid)
      Invalid =
          !isVariableCapturable(CSI, Var, ExprLoc, BuildAndDiagnose, *this);

    // After encountering an error, if we're actually supposed to capture, keep
    // capturing in nested contexts to suppress any follow-on diagnostics.
    if (Invalid && !BuildAndDiagnose)
      return true;

    if (BlockScopeInfo *BSI = dyn_cast<BlockScopeInfo>(CSI)) {
      Invalid = !captureInBlock(BSI, Var, ExprLoc, BuildAndDiagnose, CaptureType,
                               DeclRefType, Nested, *this, Invalid);
      Nested = true;
    } else if (CapturedRegionScopeInfo *RSI = dyn_cast<CapturedRegionScopeInfo>(CSI)) {
      Invalid = !captureInCapturedRegion(
          RSI, Var, ExprLoc, BuildAndDiagnose, CaptureType, DeclRefType, Nested,
          Kind, /*IsTopScope*/ I == N - 1, *this, Invalid);
      Nested = true;
    } else {
      LambdaScopeInfo *LSI = cast<LambdaScopeInfo>(CSI);
      Invalid =
          !captureInLambda(LSI, Var, ExprLoc, BuildAndDiagnose, CaptureType,
                           DeclRefType, Nested, Kind, EllipsisLoc,
                           /*IsTopScope*/ I == N - 1, *this, Invalid);
      Nested = true;
    }

    if (Invalid && !BuildAndDiagnose)
      return true;
  }
  return Invalid;
}

bool Sema::tryCaptureVariable(ValueDecl *Var, SourceLocation Loc,
                              TryCaptureKind Kind, SourceLocation EllipsisLoc) {
  QualType CaptureType;
  QualType DeclRefType;
  return tryCaptureVariable(Var, Loc, Kind, EllipsisLoc,
                            /*BuildAndDiagnose=*/true, CaptureType,
                            DeclRefType, nullptr);
}

bool Sema::NeedToCaptureVariable(ValueDecl *Var, SourceLocation Loc) {
  QualType CaptureType;
  QualType DeclRefType;
  return !tryCaptureVariable(Var, Loc, TryCapture_Implicit, SourceLocation(),
                             /*BuildAndDiagnose=*/false, CaptureType,
                             DeclRefType, nullptr);
}

QualType Sema::getCapturedDeclRefType(ValueDecl *Var, SourceLocation Loc) {
  QualType CaptureType;
  QualType DeclRefType;

  // Determine whether we can capture this variable.
  if (tryCaptureVariable(Var, Loc, TryCapture_Implicit, SourceLocation(),
                         /*BuildAndDiagnose=*/false, CaptureType,
                         DeclRefType, nullptr))
    return QualType();

  return DeclRefType;
}

namespace {
// Helper to copy the template arguments from a DeclRefExpr or MemberExpr.
// The produced TemplateArgumentListInfo* points to data stored within this
// object, so should only be used in contexts where the pointer will not be
// used after the CopiedTemplateArgs object is destroyed.
class CopiedTemplateArgs {
  bool HasArgs;
  TemplateArgumentListInfo TemplateArgStorage;
public:
  template<typename RefExpr>
  CopiedTemplateArgs(RefExpr *E) : HasArgs(E->hasExplicitTemplateArgs()) {
    if (HasArgs)
      E->copyTemplateArgumentsInto(TemplateArgStorage);
  }
  operator TemplateArgumentListInfo*()
#ifdef __has_cpp_attribute
#if __has_cpp_attribute(clang::lifetimebound)
  [[clang::lifetimebound]]
#endif
#endif
  {
    return HasArgs ? &TemplateArgStorage : nullptr;
  }
};
}

/// Walk the set of potential results of an expression and mark them all as
/// non-odr-uses if they satisfy the side-conditions of the NonOdrUseReason.
///
/// \return A new expression if we found any potential results, ExprEmpty() if
///         not, and ExprError() if we diagnosed an error.
static ExprResult rebuildPotentialResultsAsNonOdrUsed(Sema &S, Expr *E,
                                                      NonOdrUseReason NOUR) {
  // Per C++11 [basic.def.odr], a variable is odr-used "unless it is
  // an object that satisfies the requirements for appearing in a
  // constant expression (5.19) and the lvalue-to-rvalue conversion (4.1)
  // is immediately applied."  This function handles the lvalue-to-rvalue
  // conversion part.
  //
  // If we encounter a node that claims to be an odr-use but shouldn't be, we
  // transform it into the relevant kind of non-odr-use node and rebuild the
  // tree of nodes leading to it.
  //
  // This is a mini-TreeTransform that only transforms a restricted subset of
  // nodes (and only certain operands of them).

  // Rebuild a subexpression.
  auto Rebuild = [&](Expr *Sub) {
    return rebuildPotentialResultsAsNonOdrUsed(S, Sub, NOUR);
  };

  // Check whether a potential result satisfies the requirements of NOUR.
  auto IsPotentialResultOdrUsed = [&](NamedDecl *D) {
    // Any entity other than a VarDecl is always odr-used whenever it's named
    // in a potentially-evaluated expression.
    auto *VD = dyn_cast<VarDecl>(D);
    if (!VD)
      return true;

    // C++2a [basic.def.odr]p4:
    //   A variable x whose name appears as a potentially-evalauted expression
    //   e is odr-used by e unless
    //   -- x is a reference that is usable in constant expressions, or
    //   -- x is a variable of non-reference type that is usable in constant
    //      expressions and has no mutable subobjects, and e is an element of
    //      the set of potential results of an expression of
    //      non-volatile-qualified non-class type to which the lvalue-to-rvalue
    //      conversion is applied, or
    //   -- x is a variable of non-reference type, and e is an element of the
    //      set of potential results of a discarded-value expression to which
    //      the lvalue-to-rvalue conversion is not applied
    //
    // We check the first bullet and the "potentially-evaluated" condition in
    // BuildDeclRefExpr. We check the type requirements in the second bullet
    // in CheckLValueToRValueConversionOperand below.
    switch (NOUR) {
    case NOUR_None:
    case NOUR_Unevaluated:
      llvm_unreachable("unexpected non-odr-use-reason");

    case NOUR_Constant:
      // Constant references were handled when they were built.
      if (VD->getType()->isReferenceType())
        return true;
      if (auto *RD = VD->getType()->getAsCXXRecordDecl())
        if (RD->hasMutableFields())
          return true;
      if (!VD->isUsableInConstantExpressions(S.Context))
        return true;
      break;

    case NOUR_Discarded:
      if (VD->getType()->isReferenceType())
        return true;
      break;
    }
    return false;
  };

  // Mark that this expression does not constitute an odr-use.
  auto MarkNotOdrUsed = [&] {
    S.MaybeODRUseExprs.remove(E);
    if (LambdaScopeInfo *LSI = S.getCurLambda())
      LSI->markVariableExprAsNonODRUsed(E);
  };

  // C++2a [basic.def.odr]p2:
  //   The set of potential results of an expression e is defined as follows:
  switch (E->getStmtClass()) {
  //   -- If e is an id-expression, ...
  case Expr::DeclRefExprClass: {
    auto *DRE = cast<DeclRefExpr>(E);
    if (DRE->isNonOdrUse() || IsPotentialResultOdrUsed(DRE->getDecl()))
      break;

    // Rebuild as a non-odr-use DeclRefExpr.
    MarkNotOdrUsed();
    return DeclRefExpr::Create(
        S.Context, DRE->getQualifierLoc(), DRE->getTemplateKeywordLoc(),
        DRE->getDecl(), DRE->refersToEnclosingVariableOrCapture(),
        DRE->getNameInfo(), DRE->getType(), DRE->getValueKind(),
        DRE->getFoundDecl(), CopiedTemplateArgs(DRE), NOUR);
  }

  case Expr::FunctionParmPackExprClass: {
    auto *FPPE = cast<FunctionParmPackExpr>(E);
    // If any of the declarations in the pack is odr-used, then the expression
    // as a whole constitutes an odr-use.
    for (VarDecl *D : *FPPE)
      if (IsPotentialResultOdrUsed(D))
        return ExprEmpty();

    // FIXME: Rebuild as a non-odr-use FunctionParmPackExpr? In practice,
    // nothing cares about whether we marked this as an odr-use, but it might
    // be useful for non-compiler tools.
    MarkNotOdrUsed();
    break;
  }

  //   -- If e is a subscripting operation with an array operand...
  case Expr::ArraySubscriptExprClass: {
    auto *ASE = cast<ArraySubscriptExpr>(E);
    Expr *OldBase = ASE->getBase()->IgnoreImplicit();
    if (!OldBase->getType()->isArrayType())
      break;
    ExprResult Base = Rebuild(OldBase);
    if (!Base.isUsable())
      return Base;
    Expr *LHS = ASE->getBase() == ASE->getLHS() ? Base.get() : ASE->getLHS();
    Expr *RHS = ASE->getBase() == ASE->getRHS() ? Base.get() : ASE->getRHS();
    SourceLocation LBracketLoc = ASE->getBeginLoc(); // FIXME: Not stored.
    return S.ActOnArraySubscriptExpr(nullptr, LHS, LBracketLoc, RHS,
                                     ASE->getRBracketLoc());
  }

  case Expr::MemberExprClass: {
    auto *ME = cast<MemberExpr>(E);
    // -- If e is a class member access expression [...] naming a non-static
    //    data member...
    if (isa<FieldDecl>(ME->getMemberDecl())) {
      ExprResult Base = Rebuild(ME->getBase());
      if (!Base.isUsable())
        return Base;
      return MemberExpr::Create(
          S.Context, Base.get(), ME->isArrow(), ME->getOperatorLoc(),
          ME->getQualifierLoc(), ME->getTemplateKeywordLoc(),
          ME->getMemberDecl(), ME->getFoundDecl(), ME->getMemberNameInfo(),
          CopiedTemplateArgs(ME), ME->getType(), ME->getValueKind(),
          ME->getObjectKind(), ME->isNonOdrUse());
    }

    if (ME->getMemberDecl()->isCXXInstanceMember())
      break;

    // -- If e is a class member access expression naming a static data member,
    //    ...
    if (ME->isNonOdrUse() || IsPotentialResultOdrUsed(ME->getMemberDecl()))
      break;

    // Rebuild as a non-odr-use MemberExpr.
    MarkNotOdrUsed();
    return MemberExpr::Create(
        S.Context, ME->getBase(), ME->isArrow(), ME->getOperatorLoc(),
        ME->getQualifierLoc(), ME->getTemplateKeywordLoc(), ME->getMemberDecl(),
        ME->getFoundDecl(), ME->getMemberNameInfo(), CopiedTemplateArgs(ME),
        ME->getType(), ME->getValueKind(), ME->getObjectKind(), NOUR);
  }

  case Expr::BinaryOperatorClass: {
    auto *BO = cast<BinaryOperator>(E);
    Expr *LHS = BO->getLHS();
    Expr *RHS = BO->getRHS();
    // -- If e is a pointer-to-member expression of the form e1 .* e2 ...
    if (BO->getOpcode() == BO_PtrMemD) {
      ExprResult Sub = Rebuild(LHS);
      if (!Sub.isUsable())
        return Sub;
      BO->setLHS(Sub.get());
    //   -- If e is a comma expression, ...
    } else if (BO->getOpcode() == BO_Comma) {
      ExprResult Sub = Rebuild(RHS);
      if (!Sub.isUsable())
        return Sub;
      BO->setRHS(Sub.get());
    } else {
      break;
    }
    return ExprResult(BO);
  }

  //   -- If e has the form (e1)...
  case Expr::ParenExprClass: {
    auto *PE = cast<ParenExpr>(E);
    ExprResult Sub = Rebuild(PE->getSubExpr());
    if (!Sub.isUsable())
      return Sub;
    return S.ActOnParenExpr(PE->getLParen(), PE->getRParen(), Sub.get());
  }

  //   -- If e is a glvalue conditional expression, ...
  // We don't apply this to a binary conditional operator. FIXME: Should we?
  case Expr::ConditionalOperatorClass: {
    auto *CO = cast<ConditionalOperator>(E);
    ExprResult LHS = Rebuild(CO->getLHS());
    if (LHS.isInvalid())
      return ExprError();
    ExprResult RHS = Rebuild(CO->getRHS());
    if (RHS.isInvalid())
      return ExprError();
    if (!LHS.isUsable() && !RHS.isUsable())
      return ExprEmpty();
    if (!LHS.isUsable())
      LHS = CO->getLHS();
    if (!RHS.isUsable())
      RHS = CO->getRHS();
    return S.ActOnConditionalOp(CO->getQuestionLoc(), CO->getColonLoc(),
                                CO->getCond(), LHS.get(), RHS.get());
  }

  // [Clang extension]
  //   -- If e has the form __extension__ e1...
  case Expr::UnaryOperatorClass: {
    auto *UO = cast<UnaryOperator>(E);
    if (UO->getOpcode() != UO_Extension)
      break;
    ExprResult Sub = Rebuild(UO->getSubExpr());
    if (!Sub.isUsable())
      return Sub;
    return S.BuildUnaryOp(nullptr, UO->getOperatorLoc(), UO_Extension,
                          Sub.get());
  }

  // [Clang extension]
  //   -- If e has the form _Generic(...), the set of potential results is the
  //      union of the sets of potential results of the associated expressions.
  case Expr::GenericSelectionExprClass: {
    auto *GSE = cast<GenericSelectionExpr>(E);

    SmallVector<Expr *, 4> AssocExprs;
    bool AnyChanged = false;
    for (Expr *OrigAssocExpr : GSE->getAssocExprs()) {
      ExprResult AssocExpr = Rebuild(OrigAssocExpr);
      if (AssocExpr.isInvalid())
        return ExprError();
      if (AssocExpr.isUsable()) {
        AssocExprs.push_back(AssocExpr.get());
        AnyChanged = true;
      } else {
        AssocExprs.push_back(OrigAssocExpr);
      }
    }

    void *ExOrTy = nullptr;
    bool IsExpr = GSE->isExprPredicate();
    if (IsExpr)
      ExOrTy = GSE->getControllingExpr();
    else
      ExOrTy = GSE->getControllingType();
    return AnyChanged ? S.CreateGenericSelectionExpr(
                            GSE->getGenericLoc(), GSE->getDefaultLoc(),
                            GSE->getRParenLoc(), IsExpr, ExOrTy,
                            GSE->getAssocTypeSourceInfos(), AssocExprs)
                      : ExprEmpty();
  }

  // [Clang extension]
  //   -- If e has the form __builtin_choose_expr(...), the set of potential
  //      results is the union of the sets of potential results of the
  //      second and third subexpressions.
  case Expr::ChooseExprClass: {
    auto *CE = cast<ChooseExpr>(E);

    ExprResult LHS = Rebuild(CE->getLHS());
    if (LHS.isInvalid())
      return ExprError();

    ExprResult RHS = Rebuild(CE->getLHS());
    if (RHS.isInvalid())
      return ExprError();

    if (!LHS.get() && !RHS.get())
      return ExprEmpty();
    if (!LHS.isUsable())
      LHS = CE->getLHS();
    if (!RHS.isUsable())
      RHS = CE->getRHS();

    return S.ActOnChooseExpr(CE->getBuiltinLoc(), CE->getCond(), LHS.get(),
                             RHS.get(), CE->getRParenLoc());
  }

  // Step through non-syntactic nodes.
  case Expr::ConstantExprClass: {
    auto *CE = cast<ConstantExpr>(E);
    ExprResult Sub = Rebuild(CE->getSubExpr());
    if (!Sub.isUsable())
      return Sub;
    return ConstantExpr::Create(S.Context, Sub.get());
  }

  // We could mostly rely on the recursive rebuilding to rebuild implicit
  // casts, but not at the top level, so rebuild them here.
  case Expr::ImplicitCastExprClass: {
    auto *ICE = cast<ImplicitCastExpr>(E);
    // Only step through the narrow set of cast kinds we expect to encounter.
    // Anything else suggests we've left the region in which potential results
    // can be found.
    switch (ICE->getCastKind()) {
    case CK_NoOp:
    case CK_DerivedToBase:
    case CK_UncheckedDerivedToBase: {
      ExprResult Sub = Rebuild(ICE->getSubExpr());
      if (!Sub.isUsable())
        return Sub;
      CXXCastPath Path(ICE->path());
      return S.ImpCastExprToType(Sub.get(), ICE->getType(), ICE->getCastKind(),
                                 ICE->getValueKind(), &Path);
    }

    default:
      break;
    }
    break;
  }

  default:
    break;
  }

  // Can't traverse through this node. Nothing to do.
  return ExprEmpty();
}

ExprResult Sema::CheckLValueToRValueConversionOperand(Expr *E) {
  // Check whether the operand is or contains an object of non-trivial C union
  // type.
  if (E->getType().isVolatileQualified() &&
      (E->getType().hasNonTrivialToPrimitiveDestructCUnion() ||
       E->getType().hasNonTrivialToPrimitiveCopyCUnion()))
    checkNonTrivialCUnion(E->getType(), E->getExprLoc(),
                          Sema::NTCUC_LValueToRValueVolatile,
                          NTCUK_Destruct|NTCUK_Copy);

  // C++2a [basic.def.odr]p4:
  //   [...] an expression of non-volatile-qualified non-class type to which
  //   the lvalue-to-rvalue conversion is applied [...]
  if (E->getType().isVolatileQualified() || E->getType()->getAs<RecordType>())
    return E;

  ExprResult Result =
      rebuildPotentialResultsAsNonOdrUsed(*this, E, NOUR_Constant);
  if (Result.isInvalid())
    return ExprError();
  return Result.get() ? Result : E;
}

ExprResult Sema::ActOnConstantExpression(ExprResult Res) {
  Res = CorrectDelayedTyposInExpr(Res);

  if (!Res.isUsable())
    return Res;

  // If a constant-expression is a reference to a variable where we delay
  // deciding whether it is an odr-use, just assume we will apply the
  // lvalue-to-rvalue conversion.  In the one case where this doesn't happen
  // (a non-type template argument), we have special handling anyway.
  return CheckLValueToRValueConversionOperand(Res.get());
}

void Sema::CleanupVarDeclMarking() {
  // Iterate through a local copy in case MarkVarDeclODRUsed makes a recursive
  // call.
  MaybeODRUseExprSet LocalMaybeODRUseExprs;
  std::swap(LocalMaybeODRUseExprs, MaybeODRUseExprs);

  for (Expr *E : LocalMaybeODRUseExprs) {
    if (auto *DRE = dyn_cast<DeclRefExpr>(E)) {
      MarkVarDeclODRUsed(cast<VarDecl>(DRE->getDecl()),
                         DRE->getLocation(), *this);
    } else if (auto *ME = dyn_cast<MemberExpr>(E)) {
      MarkVarDeclODRUsed(cast<VarDecl>(ME->getMemberDecl()), ME->getMemberLoc(),
                         *this);
    } else if (auto *FP = dyn_cast<FunctionParmPackExpr>(E)) {
      for (VarDecl *VD : *FP)
        MarkVarDeclODRUsed(VD, FP->getParameterPackLocation(), *this);
    } else {
      llvm_unreachable("Unexpected expression");
    }
  }

  assert(MaybeODRUseExprs.empty() &&
         "MarkVarDeclODRUsed failed to cleanup MaybeODRUseExprs?");
}

static void DoMarkPotentialCapture(Sema &SemaRef, SourceLocation Loc,
                                   ValueDecl *Var, Expr *E) {
  VarDecl *VD = Var->getPotentiallyDecomposedVarDecl();
  if (!VD)
    return;

  const bool RefersToEnclosingScope =
      (SemaRef.CurContext != VD->getDeclContext() &&
       VD->getDeclContext()->isFunctionOrMethod() && VD->hasLocalStorage());
  if (RefersToEnclosingScope) {
    LambdaScopeInfo *const LSI =
        SemaRef.getCurLambda(/*IgnoreNonLambdaCapturingScope=*/true);
    if (LSI && (!LSI->CallOperator ||
                !LSI->CallOperator->Encloses(Var->getDeclContext()))) {
      // If a variable could potentially be odr-used, defer marking it so
      // until we finish analyzing the full expression for any
      // lvalue-to-rvalue
      // or discarded value conversions that would obviate odr-use.
      // Add it to the list of potential captures that will be analyzed
      // later (ActOnFinishFullExpr) for eventual capture and odr-use marking
      // unless the variable is a reference that was initialized by a constant
      // expression (this will never need to be captured or odr-used).
      //
      // FIXME: We can simplify this a lot after implementing P0588R1.
      assert(E && "Capture variable should be used in an expression.");
      if (!Var->getType()->isReferenceType() ||
          !VD->isUsableInConstantExpressions(SemaRef.Context))
        LSI->addPotentialCapture(E->IgnoreParens());
    }
  }
}

static void DoMarkVarDeclReferenced(
    Sema &SemaRef, SourceLocation Loc, VarDecl *Var, Expr *E,
    llvm::DenseMap<const VarDecl *, int> &RefsMinusAssignments) {
  assert((!E || isa<DeclRefExpr>(E) || isa<MemberExpr>(E) ||
          isa<FunctionParmPackExpr>(E)) &&
         "Invalid Expr argument to DoMarkVarDeclReferenced");
  Var->setReferenced();

  if (Var->isInvalidDecl())
    return;

  auto *MSI = Var->getMemberSpecializationInfo();
  TemplateSpecializationKind TSK = MSI ? MSI->getTemplateSpecializationKind()
                                       : Var->getTemplateSpecializationKind();

  OdrUseContext OdrUse = isOdrUseContext(SemaRef);
  bool UsableInConstantExpr =
      Var->mightBeUsableInConstantExpressions(SemaRef.Context);

  if (Var->isLocalVarDeclOrParm() && !Var->hasExternalStorage()) {
    RefsMinusAssignments.insert({Var, 0}).first->getSecond()++;
  }

  // C++20 [expr.const]p12:
  //   A variable [...] is needed for constant evaluation if it is [...] a
  //   variable whose name appears as a potentially constant evaluated
  //   expression that is either a contexpr variable or is of non-volatile
  //   const-qualified integral type or of reference type
  bool NeededForConstantEvaluation =
      isPotentiallyConstantEvaluatedContext(SemaRef) && UsableInConstantExpr;

  bool NeedDefinition =
      OdrUse == OdrUseContext::Used || NeededForConstantEvaluation;

  assert(!isa<VarTemplatePartialSpecializationDecl>(Var) &&
         "Can't instantiate a partial template specialization.");

  // If this might be a member specialization of a static data member, check
  // the specialization is visible. We already did the checks for variable
  // template specializations when we created them.
  if (NeedDefinition && TSK != TSK_Undeclared &&
      !isa<VarTemplateSpecializationDecl>(Var))
    SemaRef.checkSpecializationVisibility(Loc, Var);

  // Perform implicit instantiation of static data members, static data member
  // templates of class templates, and variable template specializations. Delay
  // instantiations of variable templates, except for those that could be used
  // in a constant expression.
  if (NeedDefinition && isTemplateInstantiation(TSK)) {
    // Per C++17 [temp.explicit]p10, we may instantiate despite an explicit
    // instantiation declaration if a variable is usable in a constant
    // expression (among other cases).
    bool TryInstantiating =
        TSK == TSK_ImplicitInstantiation ||
        (TSK == TSK_ExplicitInstantiationDeclaration && UsableInConstantExpr);

    if (TryInstantiating) {
      SourceLocation PointOfInstantiation =
          MSI ? MSI->getPointOfInstantiation() : Var->getPointOfInstantiation();
      bool FirstInstantiation = PointOfInstantiation.isInvalid();
      if (FirstInstantiation) {
        PointOfInstantiation = Loc;
        if (MSI)
          MSI->setPointOfInstantiation(PointOfInstantiation);
          // FIXME: Notify listener.
        else
          Var->setTemplateSpecializationKind(TSK, PointOfInstantiation);
      }

      if (UsableInConstantExpr) {
        // Do not defer instantiations of variables that could be used in a
        // constant expression.
        SemaRef.runWithSufficientStackSpace(PointOfInstantiation, [&] {
          SemaRef.InstantiateVariableDefinition(PointOfInstantiation, Var);
        });

        // Re-set the member to trigger a recomputation of the dependence bits
        // for the expression.
        if (auto *DRE = dyn_cast_or_null<DeclRefExpr>(E))
          DRE->setDecl(DRE->getDecl());
        else if (auto *ME = dyn_cast_or_null<MemberExpr>(E))
          ME->setMemberDecl(ME->getMemberDecl());
      } else if (FirstInstantiation) {
        SemaRef.PendingInstantiations
            .push_back(std::make_pair(Var, PointOfInstantiation));
      } else {
        bool Inserted = false;
        for (auto &I : SemaRef.SavedPendingInstantiations) {
          auto Iter = llvm::find_if(
              I, [Var](const Sema::PendingImplicitInstantiation &P) {
                return P.first == Var;
              });
          if (Iter != I.end()) {
            SemaRef.PendingInstantiations.push_back(*Iter);
            I.erase(Iter);
            Inserted = true;
            break;
          }
        }

        // FIXME: For a specialization of a variable template, we don't
        // distinguish between "declaration and type implicitly instantiated"
        // and "implicit instantiation of definition requested", so we have
        // no direct way to avoid enqueueing the pending instantiation
        // multiple times.
        if (isa<VarTemplateSpecializationDecl>(Var) && !Inserted)
          SemaRef.PendingInstantiations
            .push_back(std::make_pair(Var, PointOfInstantiation));
      }
    }
  }

  // C++2a [basic.def.odr]p4:
  //   A variable x whose name appears as a potentially-evaluated expression e
  //   is odr-used by e unless
  //   -- x is a reference that is usable in constant expressions
  //   -- x is a variable of non-reference type that is usable in constant
  //      expressions and has no mutable subobjects [FIXME], and e is an
  //      element of the set of potential results of an expression of
  //      non-volatile-qualified non-class type to which the lvalue-to-rvalue
  //      conversion is applied
  //   -- x is a variable of non-reference type, and e is an element of the set
  //      of potential results of a discarded-value expression to which the
  //      lvalue-to-rvalue conversion is not applied [FIXME]
  //
  // We check the first part of the second bullet here, and
  // Sema::CheckLValueToRValueConversionOperand deals with the second part.
  // FIXME: To get the third bullet right, we need to delay this even for
  // variables that are not usable in constant expressions.

  // If we already know this isn't an odr-use, there's nothing more to do.
  if (DeclRefExpr *DRE = dyn_cast_or_null<DeclRefExpr>(E))
    if (DRE->isNonOdrUse())
      return;
  if (MemberExpr *ME = dyn_cast_or_null<MemberExpr>(E))
    if (ME->isNonOdrUse())
      return;

  switch (OdrUse) {
  case OdrUseContext::None:
    // In some cases, a variable may not have been marked unevaluated, if it
    // appears in a defaukt initializer.
    assert((!E || isa<FunctionParmPackExpr>(E) ||
            SemaRef.isUnevaluatedContext()) &&
           "missing non-odr-use marking for unevaluated decl ref");
    break;

  case OdrUseContext::FormallyOdrUsed:
    // FIXME: Ignoring formal odr-uses results in incorrect lambda capture
    // behavior.
    break;

  case OdrUseContext::Used:
    // If we might later find that this expression isn't actually an odr-use,
    // delay the marking.
    if (E && Var->isUsableInConstantExpressions(SemaRef.Context))
      SemaRef.MaybeODRUseExprs.insert(E);
    else
      MarkVarDeclODRUsed(Var, Loc, SemaRef);
    break;

  case OdrUseContext::Dependent:
    // If this is a dependent context, we don't need to mark variables as
    // odr-used, but we may still need to track them for lambda capture.
    // FIXME: Do we also need to do this inside dependent typeid expressions
    // (which are modeled as unevaluated at this point)?
    DoMarkPotentialCapture(SemaRef, Loc, Var, E);
    break;
  }
}

static void DoMarkBindingDeclReferenced(Sema &SemaRef, SourceLocation Loc,
                                        BindingDecl *BD, Expr *E) {
  BD->setReferenced();

  if (BD->isInvalidDecl())
    return;

  OdrUseContext OdrUse = isOdrUseContext(SemaRef);
  if (OdrUse == OdrUseContext::Used) {
    QualType CaptureType, DeclRefType;
    SemaRef.tryCaptureVariable(BD, Loc, Sema::TryCapture_Implicit,
                               /*EllipsisLoc*/ SourceLocation(),
                               /*BuildAndDiagnose*/ true, CaptureType,
                               DeclRefType,
                               /*FunctionScopeIndexToStopAt*/ nullptr);
  } else if (OdrUse == OdrUseContext::Dependent) {
    DoMarkPotentialCapture(SemaRef, Loc, BD, E);
  }
}

void Sema::MarkVariableReferenced(SourceLocation Loc, VarDecl *Var) {
  DoMarkVarDeclReferenced(*this, Loc, Var, nullptr, RefsMinusAssignments);
}

// C++ [temp.dep.expr]p3:
//   An id-expression is type-dependent if it contains:
//     - an identifier associated by name lookup with an entity captured by copy
//       in a lambda-expression that has an explicit object parameter whose type
//       is dependent ([dcl.fct]),
static void FixDependencyOfIdExpressionsInLambdaWithDependentObjectParameter(
    Sema &SemaRef, ValueDecl *D, Expr *E) {
  auto *ID = dyn_cast<DeclRefExpr>(E);
  if (!ID || ID->isTypeDependent() || !ID->refersToEnclosingVariableOrCapture())
    return;

  // If any enclosing lambda with a dependent explicit object parameter either
  // explicitly captures the variable by value, or has a capture default of '='
  // and does not capture the variable by reference, then the type of the DRE
  // is dependent on the type of that lambda's explicit object parameter.
  auto IsDependent = [&]() {
    for (auto *Scope : llvm::reverse(SemaRef.FunctionScopes)) {
      auto *LSI = dyn_cast<sema::LambdaScopeInfo>(Scope);
      if (!LSI)
        continue;

      if (LSI->Lambda && !LSI->Lambda->Encloses(SemaRef.CurContext) &&
          LSI->AfterParameterList)
        return false;

      const auto *MD = LSI->CallOperator;
      if (MD->getType().isNull())
        continue;

      const auto *Ty = MD->getType()->getAs<FunctionProtoType>();
      if (!Ty || !MD->isExplicitObjectMemberFunction() ||
          !Ty->getParamType(0)->isDependentType())
        continue;

      if (auto *C = LSI->CaptureMap.count(D) ? &LSI->getCapture(D) : nullptr) {
        if (C->isCopyCapture())
          return true;
        continue;
      }

      if (LSI->ImpCaptureStyle == LambdaScopeInfo::ImpCap_LambdaByval)
        return true;
    }
    return false;
  }();

  ID->setCapturedByCopyInLambdaWithExplicitObjectParameter(
      IsDependent, SemaRef.getASTContext());
}

static void
MarkExprReferenced(Sema &SemaRef, SourceLocation Loc, Decl *D, Expr *E,
                   bool MightBeOdrUse,
                   llvm::DenseMap<const VarDecl *, int> &RefsMinusAssignments) {
  if (SemaRef.OpenMP().isInOpenMPDeclareTargetContext())
    SemaRef.OpenMP().checkDeclIsAllowedInOpenMPTarget(E, D);

  if (VarDecl *Var = dyn_cast<VarDecl>(D)) {
    DoMarkVarDeclReferenced(SemaRef, Loc, Var, E, RefsMinusAssignments);
    if (SemaRef.getLangOpts().CPlusPlus)
      FixDependencyOfIdExpressionsInLambdaWithDependentObjectParameter(SemaRef,
                                                                       Var, E);
    return;
  }

  if (BindingDecl *Decl = dyn_cast<BindingDecl>(D)) {
    DoMarkBindingDeclReferenced(SemaRef, Loc, Decl, E);
    if (SemaRef.getLangOpts().CPlusPlus)
      FixDependencyOfIdExpressionsInLambdaWithDependentObjectParameter(SemaRef,
                                                                       Decl, E);
    return;
  }
  SemaRef.MarkAnyDeclReferenced(Loc, D, MightBeOdrUse);

  // If this is a call to a method via a cast, also mark the method in the
  // derived class used in case codegen can devirtualize the call.
  const MemberExpr *ME = dyn_cast<MemberExpr>(E);
  if (!ME)
    return;
  CXXMethodDecl *MD = dyn_cast<CXXMethodDecl>(ME->getMemberDecl());
  if (!MD)
    return;
  // Only attempt to devirtualize if this is truly a virtual call.
  bool IsVirtualCall = MD->isVirtual() &&
                          ME->performsVirtualDispatch(SemaRef.getLangOpts());
  if (!IsVirtualCall)
    return;

  // If it's possible to devirtualize the call, mark the called function
  // referenced.
  CXXMethodDecl *DM = MD->getDevirtualizedMethod(
      ME->getBase(), SemaRef.getLangOpts().AppleKext);
  if (DM)
    SemaRef.MarkAnyDeclReferenced(Loc, DM, MightBeOdrUse);
}

void Sema::MarkDeclRefReferenced(DeclRefExpr *E, const Expr *Base) {
  // TODO: update this with DR# once a defect report is filed.
  // C++11 defect. The address of a pure member should not be an ODR use, even
  // if it's a qualified reference.
  bool OdrUse = true;
  if (const CXXMethodDecl *Method = dyn_cast<CXXMethodDecl>(E->getDecl()))
    if (Method->isVirtual() &&
        !Method->getDevirtualizedMethod(Base, getLangOpts().AppleKext))
      OdrUse = false;

  if (auto *FD = dyn_cast<FunctionDecl>(E->getDecl())) {
    if (!isUnevaluatedContext() && !isConstantEvaluatedContext() &&
        !isImmediateFunctionContext() &&
        !isCheckingDefaultArgumentOrInitializer() &&
        FD->isImmediateFunction() && !RebuildingImmediateInvocation &&
        !FD->isDependentContext())
      ExprEvalContexts.back().ReferenceToConsteval.insert(E);
  }
  MarkExprReferenced(*this, E->getLocation(), E->getDecl(), E, OdrUse,
                     RefsMinusAssignments);
}

void Sema::MarkMemberReferenced(MemberExpr *E) {
  // C++11 [basic.def.odr]p2:
  //   A non-overloaded function whose name appears as a potentially-evaluated
  //   expression or a member of a set of candidate functions, if selected by
  //   overload resolution when referred to from a potentially-evaluated
  //   expression, is odr-used, unless it is a pure virtual function and its
  //   name is not explicitly qualified.
  bool MightBeOdrUse = true;
  if (E->performsVirtualDispatch(getLangOpts())) {
    if (CXXMethodDecl *Method = dyn_cast<CXXMethodDecl>(E->getMemberDecl()))
      if (Method->isPureVirtual())
        MightBeOdrUse = false;
  }
  SourceLocation Loc =
      E->getMemberLoc().isValid() ? E->getMemberLoc() : E->getBeginLoc();
  MarkExprReferenced(*this, Loc, E->getMemberDecl(), E, MightBeOdrUse,
                     RefsMinusAssignments);
}

void Sema::MarkFunctionParmPackReferenced(FunctionParmPackExpr *E) {
  for (VarDecl *VD : *E)
    MarkExprReferenced(*this, E->getParameterPackLocation(), VD, E, true,
                       RefsMinusAssignments);
}

/// Perform marking for a reference to an arbitrary declaration.  It
/// marks the declaration referenced, and performs odr-use checking for
/// functions and variables. This method should not be used when building a
/// normal expression which refers to a variable.
void Sema::MarkAnyDeclReferenced(SourceLocation Loc, Decl *D,
                                 bool MightBeOdrUse) {
  if (MightBeOdrUse) {
    if (auto *VD = dyn_cast<VarDecl>(D)) {
      MarkVariableReferenced(Loc, VD);
      return;
    }
  }
  if (auto *FD = dyn_cast<FunctionDecl>(D)) {
    MarkFunctionReferenced(Loc, FD, MightBeOdrUse);
    return;
  }
  D->setReferenced();
}

namespace {
  // Mark all of the declarations used by a type as referenced.
  // FIXME: Not fully implemented yet! We need to have a better understanding
  // of when we're entering a context we should not recurse into.
  // FIXME: This is and EvaluatedExprMarker are more-or-less equivalent to
  // TreeTransforms rebuilding the type in a new context. Rather than
  // duplicating the TreeTransform logic, we should consider reusing it here.
  // Currently that causes problems when rebuilding LambdaExprs.
  class MarkReferencedDecls : public RecursiveASTVisitor<MarkReferencedDecls> {
    Sema &S;
    SourceLocation Loc;

  public:
    typedef RecursiveASTVisitor<MarkReferencedDecls> Inherited;

    MarkReferencedDecls(Sema &S, SourceLocation Loc) : S(S), Loc(Loc) { }

    bool TraverseTemplateArgument(const TemplateArgument &Arg);
  };
}

bool MarkReferencedDecls::TraverseTemplateArgument(
    const TemplateArgument &Arg) {
  {
    // A non-type template argument is a constant-evaluated context.
    EnterExpressionEvaluationContext Evaluated(
        S, Sema::ExpressionEvaluationContext::ConstantEvaluated);
    if (Arg.getKind() == TemplateArgument::Declaration) {
      if (Decl *D = Arg.getAsDecl())
        S.MarkAnyDeclReferenced(Loc, D, true);
    } else if (Arg.getKind() == TemplateArgument::Expression) {
      S.MarkDeclarationsReferencedInExpr(Arg.getAsExpr(), false);
    }
  }

  return Inherited::TraverseTemplateArgument(Arg);
}

void Sema::MarkDeclarationsReferencedInType(SourceLocation Loc, QualType T) {
  MarkReferencedDecls Marker(*this, Loc);
  Marker.TraverseType(T);
}

namespace {
/// Helper class that marks all of the declarations referenced by
/// potentially-evaluated subexpressions as "referenced".
class EvaluatedExprMarker : public UsedDeclVisitor<EvaluatedExprMarker> {
public:
  typedef UsedDeclVisitor<EvaluatedExprMarker> Inherited;
  bool SkipLocalVariables;
  ArrayRef<const Expr *> StopAt;

  EvaluatedExprMarker(Sema &S, bool SkipLocalVariables,
                      ArrayRef<const Expr *> StopAt)
      : Inherited(S), SkipLocalVariables(SkipLocalVariables), StopAt(StopAt) {}

  void visitUsedDecl(SourceLocation Loc, Decl *D) {
    S.MarkFunctionReferenced(Loc, cast<FunctionDecl>(D));
  }

  void Visit(Expr *E) {
    if (llvm::is_contained(StopAt, E))
      return;
    Inherited::Visit(E);
  }

  void VisitConstantExpr(ConstantExpr *E) {
    // Don't mark declarations within a ConstantExpression, as this expression
    // will be evaluated and folded to a value.
  }

  void VisitDeclRefExpr(DeclRefExpr *E) {
    // If we were asked not to visit local variables, don't.
    if (SkipLocalVariables) {
      if (VarDecl *VD = dyn_cast<VarDecl>(E->getDecl()))
        if (VD->hasLocalStorage())
          return;
    }

    // FIXME: This can trigger the instantiation of the initializer of a
    // variable, which can cause the expression to become value-dependent
    // or error-dependent. Do we need to propagate the new dependence bits?
    S.MarkDeclRefReferenced(E);
  }

  void VisitMemberExpr(MemberExpr *E) {
    S.MarkMemberReferenced(E);
    Visit(E->getBase());
  }
};
} // namespace

void Sema::MarkDeclarationsReferencedInExpr(Expr *E,
                                            bool SkipLocalVariables,
                                            ArrayRef<const Expr*> StopAt) {
  EvaluatedExprMarker(*this, SkipLocalVariables, StopAt).Visit(E);
}

/// Emit a diagnostic when statements are reachable.
/// FIXME: check for reachability even in expressions for which we don't build a
///        CFG (eg, in the initializer of a global or in a constant expression).
///        For example,
///        namespace { auto *p = new double[3][false ? (1, 2) : 3]; }
bool Sema::DiagIfReachable(SourceLocation Loc, ArrayRef<const Stmt *> Stmts,
                           const PartialDiagnostic &PD) {
  if (!Stmts.empty() && getCurFunctionOrMethodDecl()) {
    if (!FunctionScopes.empty())
      FunctionScopes.back()->PossiblyUnreachableDiags.push_back(
          sema::PossiblyUnreachableDiag(PD, Loc, Stmts));
    return true;
  }

  // The initializer of a constexpr variable or of the first declaration of a
  // static data member is not syntactically a constant evaluated constant,
  // but nonetheless is always required to be a constant expression, so we
  // can skip diagnosing.
  // FIXME: Using the mangling context here is a hack.
  if (auto *VD = dyn_cast_or_null<VarDecl>(
          ExprEvalContexts.back().ManglingContextDecl)) {
    if (VD->isConstexpr() ||
        (VD->isStaticDataMember() && VD->isFirstDecl() && !VD->isInline()))
      return false;
    // FIXME: For any other kind of variable, we should build a CFG for its
    // initializer and check whether the context in question is reachable.
  }

  Diag(Loc, PD);
  return true;
}

/// Emit a diagnostic that describes an effect on the run-time behavior
/// of the program being compiled.
///
/// This routine emits the given diagnostic when the code currently being
/// type-checked is "potentially evaluated", meaning that there is a
/// possibility that the code will actually be executable. Code in sizeof()
/// expressions, code used only during overload resolution, etc., are not
/// potentially evaluated. This routine will suppress such diagnostics or,
/// in the absolutely nutty case of potentially potentially evaluated
/// expressions (C++ typeid), queue the diagnostic to potentially emit it
/// later.
///
/// This routine should be used for all diagnostics that describe the run-time
/// behavior of a program, such as passing a non-POD value through an ellipsis.
/// Failure to do so will likely result in spurious diagnostics or failures
/// during overload resolution or within sizeof/alignof/typeof/typeid.
bool Sema::DiagRuntimeBehavior(SourceLocation Loc, ArrayRef<const Stmt*> Stmts,
                               const PartialDiagnostic &PD) {

  if (ExprEvalContexts.back().isDiscardedStatementContext())
    return false;

  switch (ExprEvalContexts.back().Context) {
  case ExpressionEvaluationContext::Unevaluated:
  case ExpressionEvaluationContext::UnevaluatedList:
  case ExpressionEvaluationContext::UnevaluatedAbstract:
  case ExpressionEvaluationContext::DiscardedStatement:
    // The argument will never be evaluated, so don't complain.
    break;

  case ExpressionEvaluationContext::ConstantEvaluated:
  case ExpressionEvaluationContext::ImmediateFunctionContext:
    // Relevant diagnostics should be produced by constant evaluation.
    break;

  case ExpressionEvaluationContext::PotentiallyEvaluated:
  case ExpressionEvaluationContext::PotentiallyEvaluatedIfUsed:
    return DiagIfReachable(Loc, Stmts, PD);
  }

  return false;
}

bool Sema::DiagRuntimeBehavior(SourceLocation Loc, const Stmt *Statement,
                               const PartialDiagnostic &PD) {
  return DiagRuntimeBehavior(
      Loc, Statement ? llvm::ArrayRef(Statement) : std::nullopt, PD);
}

bool Sema::CheckCallReturnType(QualType ReturnType, SourceLocation Loc,
                               CallExpr *CE, FunctionDecl *FD) {
  if (ReturnType->isVoidType() || !ReturnType->isIncompleteType())
    return false;

  // If we're inside a decltype's expression, don't check for a valid return
  // type or construct temporaries until we know whether this is the last call.
  if (ExprEvalContexts.back().ExprContext ==
      ExpressionEvaluationContextRecord::EK_Decltype) {
    ExprEvalContexts.back().DelayedDecltypeCalls.push_back(CE);
    return false;
  }

  class CallReturnIncompleteDiagnoser : public TypeDiagnoser {
    FunctionDecl *FD;
    CallExpr *CE;

  public:
    CallReturnIncompleteDiagnoser(FunctionDecl *FD, CallExpr *CE)
      : FD(FD), CE(CE) { }

    void diagnose(Sema &S, SourceLocation Loc, QualType T) override {
      if (!FD) {
        S.Diag(Loc, diag::err_call_incomplete_return)
          << T << CE->getSourceRange();
        return;
      }

      S.Diag(Loc, diag::err_call_function_incomplete_return)
          << CE->getSourceRange() << FD << T;
      S.Diag(FD->getLocation(), diag::note_entity_declared_at)
          << FD->getDeclName();
    }
  } Diagnoser(FD, CE);

  if (RequireCompleteType(Loc, ReturnType, Diagnoser))
    return true;

  return false;
}

// Diagnose the s/=/==/ and s/\|=/!=/ typos. Note that adding parentheses
// will prevent this condition from triggering, which is what we want.
void Sema::DiagnoseAssignmentAsCondition(Expr *E) {
  SourceLocation Loc;

  unsigned diagnostic = diag::warn_condition_is_assignment;
  bool IsOrAssign = false;

  if (BinaryOperator *Op = dyn_cast<BinaryOperator>(E)) {
    if (Op->getOpcode() != BO_Assign && Op->getOpcode() != BO_OrAssign)
      return;

    IsOrAssign = Op->getOpcode() == BO_OrAssign;

    // Greylist some idioms by putting them into a warning subcategory.
    if (ObjCMessageExpr *ME
          = dyn_cast<ObjCMessageExpr>(Op->getRHS()->IgnoreParenCasts())) {
      Selector Sel = ME->getSelector();

      // self = [<foo> init...]
      if (ObjC().isSelfExpr(Op->getLHS()) && ME->getMethodFamily() == OMF_init)
        diagnostic = diag::warn_condition_is_idiomatic_assignment;

      // <foo> = [<bar> nextObject]
      else if (Sel.isUnarySelector() && Sel.getNameForSlot(0) == "nextObject")
        diagnostic = diag::warn_condition_is_idiomatic_assignment;
    }

    Loc = Op->getOperatorLoc();
  } else if (CXXOperatorCallExpr *Op = dyn_cast<CXXOperatorCallExpr>(E)) {
    if (Op->getOperator() != OO_Equal && Op->getOperator() != OO_PipeEqual)
      return;

    IsOrAssign = Op->getOperator() == OO_PipeEqual;
    Loc = Op->getOperatorLoc();
  } else if (PseudoObjectExpr *POE = dyn_cast<PseudoObjectExpr>(E))
    return DiagnoseAssignmentAsCondition(POE->getSyntacticForm());
  else {
    // Not an assignment.
    return;
  }

  Diag(Loc, diagnostic) << E->getSourceRange();

  SourceLocation Open = E->getBeginLoc();
  SourceLocation Close = getLocForEndOfToken(E->getSourceRange().getEnd());
  Diag(Loc, diag::note_condition_assign_silence)
        << FixItHint::CreateInsertion(Open, "(")
        << FixItHint::CreateInsertion(Close, ")");

  if (IsOrAssign)
    Diag(Loc, diag::note_condition_or_assign_to_comparison)
      << FixItHint::CreateReplacement(Loc, "!=");
  else
    Diag(Loc, diag::note_condition_assign_to_comparison)
      << FixItHint::CreateReplacement(Loc, "==");
}

void Sema::DiagnoseEqualityWithExtraParens(ParenExpr *ParenE) {
  // Don't warn if the parens came from a macro.
  SourceLocation parenLoc = ParenE->getBeginLoc();
  if (parenLoc.isInvalid() || parenLoc.isMacroID())
    return;
  // Don't warn for dependent expressions.
  if (ParenE->isTypeDependent())
    return;

  Expr *E = ParenE->IgnoreParens();

  if (BinaryOperator *opE = dyn_cast<BinaryOperator>(E))
    if (opE->getOpcode() == BO_EQ &&
        opE->getLHS()->IgnoreParenImpCasts()->isModifiableLvalue(Context)
                                                           == Expr::MLV_Valid) {
      SourceLocation Loc = opE->getOperatorLoc();

      Diag(Loc, diag::warn_equality_with_extra_parens) << E->getSourceRange();
      SourceRange ParenERange = ParenE->getSourceRange();
      Diag(Loc, diag::note_equality_comparison_silence)
        << FixItHint::CreateRemoval(ParenERange.getBegin())
        << FixItHint::CreateRemoval(ParenERange.getEnd());
      Diag(Loc, diag::note_equality_comparison_to_assign)
        << FixItHint::CreateReplacement(Loc, "=");
    }
}

ExprResult Sema::CheckBooleanCondition(SourceLocation Loc, Expr *E,
                                       bool IsConstexpr) {
  DiagnoseAssignmentAsCondition(E);
  if (ParenExpr *parenE = dyn_cast<ParenExpr>(E))
    DiagnoseEqualityWithExtraParens(parenE);

  ExprResult result = CheckPlaceholderExpr(E);
  if (result.isInvalid()) return ExprError();
  E = result.get();

  if (!E->isTypeDependent()) {
    if (getLangOpts().CPlusPlus)
      return CheckCXXBooleanCondition(E, IsConstexpr); // C++ 6.4p4

    ExprResult ERes = DefaultFunctionArrayLvalueConversion(E);
    if (ERes.isInvalid())
      return ExprError();
    E = ERes.get();

    QualType T = E->getType();
    if (!T->isScalarType()) { // C99 6.8.4.1p1
      Diag(Loc, diag::err_typecheck_statement_requires_scalar)
        << T << E->getSourceRange();
      return ExprError();
    }
    CheckBoolLikeConversion(E, Loc);
  }

  return E;
}

Sema::ConditionResult Sema::ActOnCondition(Scope *S, SourceLocation Loc,
                                           Expr *SubExpr, ConditionKind CK,
                                           bool MissingOK) {
  // MissingOK indicates whether having no condition expression is valid
  // (for loop) or invalid (e.g. while loop).
  if (!SubExpr)
    return MissingOK ? ConditionResult() : ConditionError();

  ExprResult Cond;
  switch (CK) {
  case ConditionKind::Boolean:
    Cond = CheckBooleanCondition(Loc, SubExpr);
    break;

  case ConditionKind::ConstexprIf:
    Cond = CheckBooleanCondition(Loc, SubExpr, true);
    break;

  case ConditionKind::Switch:
    Cond = CheckSwitchCondition(Loc, SubExpr);
    break;
  }
  if (Cond.isInvalid()) {
    Cond = CreateRecoveryExpr(SubExpr->getBeginLoc(), SubExpr->getEndLoc(),
                              {SubExpr}, PreferredConditionType(CK));
    if (!Cond.get())
      return ConditionError();
  }
  // FIXME: FullExprArg doesn't have an invalid bit, so check nullness instead.
  FullExprArg FullExpr = MakeFullExpr(Cond.get(), Loc);
  if (!FullExpr.get())
    return ConditionError();

  return ConditionResult(*this, nullptr, FullExpr,
                         CK == ConditionKind::ConstexprIf);
}

namespace {
  /// A visitor for rebuilding a call to an __unknown_any expression
  /// to have an appropriate type.
  struct RebuildUnknownAnyFunction
    : StmtVisitor<RebuildUnknownAnyFunction, ExprResult> {

    Sema &S;

    RebuildUnknownAnyFunction(Sema &S) : S(S) {}

    ExprResult VisitStmt(Stmt *S) {
      llvm_unreachable("unexpected statement!");
    }

    ExprResult VisitExpr(Expr *E) {
      S.Diag(E->getExprLoc(), diag::err_unsupported_unknown_any_call)
        << E->getSourceRange();
      return ExprError();
    }

    /// Rebuild an expression which simply semantically wraps another
    /// expression which it shares the type and value kind of.
    template <class T> ExprResult rebuildSugarExpr(T *E) {
      ExprResult SubResult = Visit(E->getSubExpr());
      if (SubResult.isInvalid()) return ExprError();

      Expr *SubExpr = SubResult.get();
      E->setSubExpr(SubExpr);
      E->setType(SubExpr->getType());
      E->setValueKind(SubExpr->getValueKind());
      assert(E->getObjectKind() == OK_Ordinary);
      return E;
    }

    ExprResult VisitParenExpr(ParenExpr *E) {
      return rebuildSugarExpr(E);
    }

    ExprResult VisitUnaryExtension(UnaryOperator *E) {
      return rebuildSugarExpr(E);
    }

    ExprResult VisitUnaryAddrOf(UnaryOperator *E) {
      ExprResult SubResult = Visit(E->getSubExpr());
      if (SubResult.isInvalid()) return ExprError();

      Expr *SubExpr = SubResult.get();
      E->setSubExpr(SubExpr);
      E->setType(S.Context.getPointerType(SubExpr->getType()));
      assert(E->isPRValue());
      assert(E->getObjectKind() == OK_Ordinary);
      return E;
    }

    ExprResult resolveDecl(Expr *E, ValueDecl *VD) {
      if (!isa<FunctionDecl>(VD)) return VisitExpr(E);

      E->setType(VD->getType());

      assert(E->isPRValue());
      if (S.getLangOpts().CPlusPlus &&
          !(isa<CXXMethodDecl>(VD) &&
            cast<CXXMethodDecl>(VD)->isInstance()))
        E->setValueKind(VK_LValue);

      return E;
    }

    ExprResult VisitMemberExpr(MemberExpr *E) {
      return resolveDecl(E, E->getMemberDecl());
    }

    ExprResult VisitDeclRefExpr(DeclRefExpr *E) {
      return resolveDecl(E, E->getDecl());
    }
  };
}

/// Given a function expression of unknown-any type, try to rebuild it
/// to have a function type.
static ExprResult rebuildUnknownAnyFunction(Sema &S, Expr *FunctionExpr) {
  ExprResult Result = RebuildUnknownAnyFunction(S).Visit(FunctionExpr);
  if (Result.isInvalid()) return ExprError();
  return S.DefaultFunctionArrayConversion(Result.get());
}

namespace {
  /// A visitor for rebuilding an expression of type __unknown_anytype
  /// into one which resolves the type directly on the referring
  /// expression.  Strict preservation of the original source
  /// structure is not a goal.
  struct RebuildUnknownAnyExpr
    : StmtVisitor<RebuildUnknownAnyExpr, ExprResult> {

    Sema &S;

    /// The current destination type.
    QualType DestType;

    RebuildUnknownAnyExpr(Sema &S, QualType CastType)
      : S(S), DestType(CastType) {}

    ExprResult VisitStmt(Stmt *S) {
      llvm_unreachable("unexpected statement!");
    }

    ExprResult VisitExpr(Expr *E) {
      S.Diag(E->getExprLoc(), diag::err_unsupported_unknown_any_expr)
        << E->getSourceRange();
      return ExprError();
    }

    ExprResult VisitCallExpr(CallExpr *E);
    ExprResult VisitObjCMessageExpr(ObjCMessageExpr *E);

    /// Rebuild an expression which simply semantically wraps another
    /// expression which it shares the type and value kind of.
    template <class T> ExprResult rebuildSugarExpr(T *E) {
      ExprResult SubResult = Visit(E->getSubExpr());
      if (SubResult.isInvalid()) return ExprError();
      Expr *SubExpr = SubResult.get();
      E->setSubExpr(SubExpr);
      E->setType(SubExpr->getType());
      E->setValueKind(SubExpr->getValueKind());
      assert(E->getObjectKind() == OK_Ordinary);
      return E;
    }

    ExprResult VisitParenExpr(ParenExpr *E) {
      return rebuildSugarExpr(E);
    }

    ExprResult VisitUnaryExtension(UnaryOperator *E) {
      return rebuildSugarExpr(E);
    }

    ExprResult VisitUnaryAddrOf(UnaryOperator *E) {
      const PointerType *Ptr = DestType->getAs<PointerType>();
      if (!Ptr) {
        S.Diag(E->getOperatorLoc(), diag::err_unknown_any_addrof)
          << E->getSourceRange();
        return ExprError();
      }

      if (isa<CallExpr>(E->getSubExpr())) {
        S.Diag(E->getOperatorLoc(), diag::err_unknown_any_addrof_call)
          << E->getSourceRange();
        return ExprError();
      }

      assert(E->isPRValue());
      assert(E->getObjectKind() == OK_Ordinary);
      E->setType(DestType);

      // Build the sub-expression as if it were an object of the pointee type.
      DestType = Ptr->getPointeeType();
      ExprResult SubResult = Visit(E->getSubExpr());
      if (SubResult.isInvalid()) return ExprError();
      E->setSubExpr(SubResult.get());
      return E;
    }

    ExprResult VisitImplicitCastExpr(ImplicitCastExpr *E);

    ExprResult resolveDecl(Expr *E, ValueDecl *VD);

    ExprResult VisitMemberExpr(MemberExpr *E) {
      return resolveDecl(E, E->getMemberDecl());
    }

    ExprResult VisitDeclRefExpr(DeclRefExpr *E) {
      return resolveDecl(E, E->getDecl());
    }
  };
}

/// Rebuilds a call expression which yielded __unknown_anytype.
ExprResult RebuildUnknownAnyExpr::VisitCallExpr(CallExpr *E) {
  Expr *CalleeExpr = E->getCallee();

  enum FnKind {
    FK_MemberFunction,
    FK_FunctionPointer,
    FK_BlockPointer
  };

  FnKind Kind;
  QualType CalleeType = CalleeExpr->getType();
  if (CalleeType == S.Context.BoundMemberTy) {
    assert(isa<CXXMemberCallExpr>(E) || isa<CXXOperatorCallExpr>(E));
    Kind = FK_MemberFunction;
    CalleeType = Expr::findBoundMemberType(CalleeExpr);
  } else if (const PointerType *Ptr = CalleeType->getAs<PointerType>()) {
    CalleeType = Ptr->getPointeeType();
    Kind = FK_FunctionPointer;
  } else {
    CalleeType = CalleeType->castAs<BlockPointerType>()->getPointeeType();
    Kind = FK_BlockPointer;
  }
  const FunctionType *FnType = CalleeType->castAs<FunctionType>();

  // Verify that this is a legal result type of a function.
  if (DestType->isArrayType() || DestType->isFunctionType()) {
    unsigned diagID = diag::err_func_returning_array_function;
    if (Kind == FK_BlockPointer)
      diagID = diag::err_block_returning_array_function;

    S.Diag(E->getExprLoc(), diagID)
      << DestType->isFunctionType() << DestType;
    return ExprError();
  }

  // Otherwise, go ahead and set DestType as the call's result.
  E->setType(DestType.getNonLValueExprType(S.Context));
  E->setValueKind(Expr::getValueKindForType(DestType));
  assert(E->getObjectKind() == OK_Ordinary);

  // Rebuild the function type, replacing the result type with DestType.
  const FunctionProtoType *Proto = dyn_cast<FunctionProtoType>(FnType);
  if (Proto) {
    // __unknown_anytype(...) is a special case used by the debugger when
    // it has no idea what a function's signature is.
    //
    // We want to build this call essentially under the K&R
    // unprototyped rules, but making a FunctionNoProtoType in C++
    // would foul up all sorts of assumptions.  However, we cannot
    // simply pass all arguments as variadic arguments, nor can we
    // portably just call the function under a non-variadic type; see
    // the comment on IR-gen's TargetInfo::isNoProtoCallVariadic.
    // However, it turns out that in practice it is generally safe to
    // call a function declared as "A foo(B,C,D);" under the prototype
    // "A foo(B,C,D,...);".  The only known exception is with the
    // Windows ABI, where any variadic function is implicitly cdecl
    // regardless of its normal CC.  Therefore we change the parameter
    // types to match the types of the arguments.
    //
    // This is a hack, but it is far superior to moving the
    // corresponding target-specific code from IR-gen to Sema/AST.

    ArrayRef<QualType> ParamTypes = Proto->getParamTypes();
    SmallVector<QualType, 8> ArgTypes;
    if (ParamTypes.empty() && Proto->isVariadic()) { // the special case
      ArgTypes.reserve(E->getNumArgs());
      for (unsigned i = 0, e = E->getNumArgs(); i != e; ++i) {
        ArgTypes.push_back(S.Context.getReferenceQualifiedType(E->getArg(i)));
      }
      ParamTypes = ArgTypes;
    }
    DestType = S.Context.getFunctionType(DestType, ParamTypes,
                                         Proto->getExtProtoInfo());
  } else {
    DestType = S.Context.getFunctionNoProtoType(DestType,
                                                FnType->getExtInfo());
  }

  // Rebuild the appropriate pointer-to-function type.
  switch (Kind) {
  case FK_MemberFunction:
    // Nothing to do.
    break;

  case FK_FunctionPointer:
    DestType = S.Context.getPointerType(DestType);
    break;

  case FK_BlockPointer:
    DestType = S.Context.getBlockPointerType(DestType);
    break;
  }

  // Finally, we can recurse.
  ExprResult CalleeResult = Visit(CalleeExpr);
  if (!CalleeResult.isUsable()) return ExprError();
  E->setCallee(CalleeResult.get());

  // Bind a temporary if necessary.
  return S.MaybeBindToTemporary(E);
}

ExprResult RebuildUnknownAnyExpr::VisitObjCMessageExpr(ObjCMessageExpr *E) {
  // Verify that this is a legal result type of a call.
  if (DestType->isArrayType() || DestType->isFunctionType()) {
    S.Diag(E->getExprLoc(), diag::err_func_returning_array_function)
      << DestType->isFunctionType() << DestType;
    return ExprError();
  }

  // Rewrite the method result type if available.
  if (ObjCMethodDecl *Method = E->getMethodDecl()) {
    assert(Method->getReturnType() == S.Context.UnknownAnyTy);
    Method->setReturnType(DestType);
  }

  // Change the type of the message.
  E->setType(DestType.getNonReferenceType());
  E->setValueKind(Expr::getValueKindForType(DestType));

  return S.MaybeBindToTemporary(E);
}

ExprResult RebuildUnknownAnyExpr::VisitImplicitCastExpr(ImplicitCastExpr *E) {
  // The only case we should ever see here is a function-to-pointer decay.
  if (E->getCastKind() == CK_FunctionToPointerDecay) {
    assert(E->isPRValue());
    assert(E->getObjectKind() == OK_Ordinary);

    E->setType(DestType);

    // Rebuild the sub-expression as the pointee (function) type.
    DestType = DestType->castAs<PointerType>()->getPointeeType();

    ExprResult Result = Visit(E->getSubExpr());
    if (!Result.isUsable()) return ExprError();

    E->setSubExpr(Result.get());
    return E;
  } else if (E->getCastKind() == CK_LValueToRValue) {
    assert(E->isPRValue());
    assert(E->getObjectKind() == OK_Ordinary);

    assert(isa<BlockPointerType>(E->getType()));

    E->setType(DestType);

    // The sub-expression has to be a lvalue reference, so rebuild it as such.
    DestType = S.Context.getLValueReferenceType(DestType);

    ExprResult Result = Visit(E->getSubExpr());
    if (!Result.isUsable()) return ExprError();

    E->setSubExpr(Result.get());
    return E;
  } else {
    llvm_unreachable("Unhandled cast type!");
  }
}

ExprResult RebuildUnknownAnyExpr::resolveDecl(Expr *E, ValueDecl *VD) {
  ExprValueKind ValueKind = VK_LValue;
  QualType Type = DestType;

  // We know how to make this work for certain kinds of decls:

  //  - functions
  if (FunctionDecl *FD = dyn_cast<FunctionDecl>(VD)) {
    if (const PointerType *Ptr = Type->getAs<PointerType>()) {
      DestType = Ptr->getPointeeType();
      ExprResult Result = resolveDecl(E, VD);
      if (Result.isInvalid()) return ExprError();
      return S.ImpCastExprToType(Result.get(), Type, CK_FunctionToPointerDecay,
                                 VK_PRValue);
    }

    if (!Type->isFunctionType()) {
      S.Diag(E->getExprLoc(), diag::err_unknown_any_function)
        << VD << E->getSourceRange();
      return ExprError();
    }
    if (const FunctionProtoType *FT = Type->getAs<FunctionProtoType>()) {
      // We must match the FunctionDecl's type to the hack introduced in
      // RebuildUnknownAnyExpr::VisitCallExpr to vararg functions of unknown
      // type. See the lengthy commentary in that routine.
      QualType FDT = FD->getType();
      const FunctionType *FnType = FDT->castAs<FunctionType>();
      const FunctionProtoType *Proto = dyn_cast_or_null<FunctionProtoType>(FnType);
      DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(E);
      if (DRE && Proto && Proto->getParamTypes().empty() && Proto->isVariadic()) {
        SourceLocation Loc = FD->getLocation();
        FunctionDecl *NewFD = FunctionDecl::Create(
            S.Context, FD->getDeclContext(), Loc, Loc,
            FD->getNameInfo().getName(), DestType, FD->getTypeSourceInfo(),
            SC_None, S.getCurFPFeatures().isFPConstrained(),
            false /*isInlineSpecified*/, FD->hasPrototype(),
            /*ConstexprKind*/ ConstexprSpecKind::Unspecified);

        if (FD->getQualifier())
          NewFD->setQualifierInfo(FD->getQualifierLoc());

        SmallVector<ParmVarDecl*, 16> Params;
        for (const auto &AI : FT->param_types()) {
          ParmVarDecl *Param =
            S.BuildParmVarDeclForTypedef(FD, Loc, AI);
          Param->setScopeInfo(0, Params.size());
          Params.push_back(Param);
        }
        NewFD->setParams(Params);
        DRE->setDecl(NewFD);
        VD = DRE->getDecl();
      }
    }

    if (CXXMethodDecl *MD = dyn_cast<CXXMethodDecl>(FD))
      if (MD->isInstance()) {
        ValueKind = VK_PRValue;
        Type = S.Context.BoundMemberTy;
      }

    // Function references aren't l-values in C.
    if (!S.getLangOpts().CPlusPlus)
      ValueKind = VK_PRValue;

  //  - variables
  } else if (isa<VarDecl>(VD)) {
    if (const ReferenceType *RefTy = Type->getAs<ReferenceType>()) {
      Type = RefTy->getPointeeType();
    } else if (Type->isFunctionType()) {
      S.Diag(E->getExprLoc(), diag::err_unknown_any_var_function_type)
        << VD << E->getSourceRange();
      return ExprError();
    }

  //  - nothing else
  } else {
    S.Diag(E->getExprLoc(), diag::err_unsupported_unknown_any_decl)
      << VD << E->getSourceRange();
    return ExprError();
  }

  // Modifying the declaration like this is friendly to IR-gen but
  // also really dangerous.
  VD->setType(DestType);
  E->setType(Type);
  E->setValueKind(ValueKind);
  return E;
}

ExprResult Sema::checkUnknownAnyCast(SourceRange TypeRange, QualType CastType,
                                     Expr *CastExpr, CastKind &CastKind,
                                     ExprValueKind &VK, CXXCastPath &Path) {
  // The type we're casting to must be either void or complete.
  if (!CastType->isVoidType() &&
      RequireCompleteType(TypeRange.getBegin(), CastType,
                          diag::err_typecheck_cast_to_incomplete))
    return ExprError();

  // Rewrite the casted expression from scratch.
  ExprResult result = RebuildUnknownAnyExpr(*this, CastType).Visit(CastExpr);
  if (!result.isUsable()) return ExprError();

  CastExpr = result.get();
  VK = CastExpr->getValueKind();
  CastKind = CK_NoOp;

  return CastExpr;
}

ExprResult Sema::forceUnknownAnyToType(Expr *E, QualType ToType) {
  return RebuildUnknownAnyExpr(*this, ToType).Visit(E);
}

ExprResult Sema::checkUnknownAnyArg(SourceLocation callLoc,
                                    Expr *arg, QualType &paramType) {
  // If the syntactic form of the argument is not an explicit cast of
  // any sort, just do default argument promotion.
  ExplicitCastExpr *castArg = dyn_cast<ExplicitCastExpr>(arg->IgnoreParens());
  if (!castArg) {
    ExprResult result = DefaultArgumentPromotion(arg);
    if (result.isInvalid()) return ExprError();
    paramType = result.get()->getType();
    return result;
  }

  // Otherwise, use the type that was written in the explicit cast.
  assert(!arg->hasPlaceholderType());
  paramType = castArg->getTypeAsWritten();

  // Copy-initialize a parameter of that type.
  InitializedEntity entity =
    InitializedEntity::InitializeParameter(Context, paramType,
                                           /*consumed*/ false);
  return PerformCopyInitialization(entity, callLoc, arg);
}

static ExprResult diagnoseUnknownAnyExpr(Sema &S, Expr *E) {
  Expr *orig = E;
  unsigned diagID = diag::err_uncasted_use_of_unknown_any;
  while (true) {
    E = E->IgnoreParenImpCasts();
    if (CallExpr *call = dyn_cast<CallExpr>(E)) {
      E = call->getCallee();
      diagID = diag::err_uncasted_call_of_unknown_any;
    } else {
      break;
    }
  }

  SourceLocation loc;
  NamedDecl *d;
  if (DeclRefExpr *ref = dyn_cast<DeclRefExpr>(E)) {
    loc = ref->getLocation();
    d = ref->getDecl();
  } else if (MemberExpr *mem = dyn_cast<MemberExpr>(E)) {
    loc = mem->getMemberLoc();
    d = mem->getMemberDecl();
  } else if (ObjCMessageExpr *msg = dyn_cast<ObjCMessageExpr>(E)) {
    diagID = diag::err_uncasted_call_of_unknown_any;
    loc = msg->getSelectorStartLoc();
    d = msg->getMethodDecl();
    if (!d) {
      S.Diag(loc, diag::err_uncasted_send_to_unknown_any_method)
        << static_cast<unsigned>(msg->isClassMessage()) << msg->getSelector()
        << orig->getSourceRange();
      return ExprError();
    }
  } else {
    S.Diag(E->getExprLoc(), diag::err_unsupported_unknown_any_expr)
      << E->getSourceRange();
    return ExprError();
  }

  S.Diag(loc, diagID) << d << orig->getSourceRange();

  // Never recoverable.
  return ExprError();
}

ExprResult Sema::CheckPlaceholderExpr(Expr *E) {
  if (!Context.isDependenceAllowed()) {
    // C cannot handle TypoExpr nodes on either side of a binop because it
    // doesn't handle dependent types properly, so make sure any TypoExprs have
    // been dealt with before checking the operands.
    ExprResult Result = CorrectDelayedTyposInExpr(E);
    if (!Result.isUsable()) return ExprError();
    E = Result.get();
  }

  const BuiltinType *placeholderType = E->getType()->getAsPlaceholderType();
  if (!placeholderType) return E;

  switch (placeholderType->getKind()) {
  case BuiltinType::UnresolvedTemplate: {
    auto *ULE = cast<UnresolvedLookupExpr>(E);
    const DeclarationNameInfo &NameInfo = ULE->getNameInfo();
    // There's only one FoundDecl for UnresolvedTemplate type. See
    // BuildTemplateIdExpr.
    NamedDecl *Temp = *ULE->decls_begin();
    const bool IsTypeAliasTemplateDecl = isa<TypeAliasTemplateDecl>(Temp);

    if (NestedNameSpecifierLoc Loc = ULE->getQualifierLoc(); Loc.hasQualifier())
      Diag(NameInfo.getLoc(), diag::err_template_kw_refers_to_type_template)
          << Loc.getNestedNameSpecifier() << NameInfo.getName().getAsString()
          << Loc.getSourceRange() << IsTypeAliasTemplateDecl;
    else
      Diag(NameInfo.getLoc(), diag::err_template_kw_refers_to_type_template)
          << "" << NameInfo.getName().getAsString() << ULE->getSourceRange()
          << IsTypeAliasTemplateDecl;
    Diag(Temp->getLocation(), diag::note_referenced_type_template)
        << IsTypeAliasTemplateDecl;

    return CreateRecoveryExpr(NameInfo.getBeginLoc(), NameInfo.getEndLoc(), {});
  }

  // Overloaded expressions.
  case BuiltinType::Overload: {
    // Try to resolve a single function template specialization.
    // This is obligatory.
    ExprResult Result = E;
    if (ResolveAndFixSingleFunctionTemplateSpecialization(Result, false))
      return Result;

    // No guarantees that ResolveAndFixSingleFunctionTemplateSpecialization
    // leaves Result unchanged on failure.
    Result = E;
    if (resolveAndFixAddressOfSingleOverloadCandidate(Result))
      return Result;

    // If that failed, try to recover with a call.
    tryToRecoverWithCall(Result, PDiag(diag::err_ovl_unresolvable),
                         /*complain*/ true);
    return Result;
  }

  // Bound member functions.
  case BuiltinType::BoundMember: {
    ExprResult result = E;
    const Expr *BME = E->IgnoreParens();
    PartialDiagnostic PD = PDiag(diag::err_bound_member_function);
    // Try to give a nicer diagnostic if it is a bound member that we recognize.
    if (isa<CXXPseudoDestructorExpr>(BME)) {
      PD = PDiag(diag::err_dtor_expr_without_call) << /*pseudo-destructor*/ 1;
    } else if (const auto *ME = dyn_cast<MemberExpr>(BME)) {
      if (ME->getMemberNameInfo().getName().getNameKind() ==
          DeclarationName::CXXDestructorName)
        PD = PDiag(diag::err_dtor_expr_without_call) << /*destructor*/ 0;
    }
    tryToRecoverWithCall(result, PD,
                         /*complain*/ true);
    return result;
  }

  // ARC unbridged casts.
  case BuiltinType::ARCUnbridgedCast: {
    Expr *realCast = ObjC().stripARCUnbridgedCast(E);
    ObjC().diagnoseARCUnbridgedCast(realCast);
    return realCast;
  }

  // Expressions of unknown type.
  case BuiltinType::UnknownAny:
    return diagnoseUnknownAnyExpr(*this, E);

  // Pseudo-objects.
  case BuiltinType::PseudoObject:
    return PseudoObject().checkRValue(E);

  case BuiltinType::BuiltinFn: {
    // Accept __noop without parens by implicitly converting it to a call expr.
    auto *DRE = dyn_cast<DeclRefExpr>(E->IgnoreParenImpCasts());
    if (DRE) {
      auto *FD = cast<FunctionDecl>(DRE->getDecl());
      unsigned BuiltinID = FD->getBuiltinID();
      if (BuiltinID == Builtin::BI__noop) {
        E = ImpCastExprToType(E, Context.getPointerType(FD->getType()),
                              CK_BuiltinFnToFnPtr)
                .get();
        return CallExpr::Create(Context, E, /*Args=*/{}, Context.IntTy,
                                VK_PRValue, SourceLocation(),
                                FPOptionsOverride());
      }

      if (Context.BuiltinInfo.isInStdNamespace(BuiltinID)) {
        // Any use of these other than a direct call is ill-formed as of C++20,
        // because they are not addressable functions. In earlier language
        // modes, warn and force an instantiation of the real body.
        Diag(E->getBeginLoc(),
             getLangOpts().CPlusPlus20
                 ? diag::err_use_of_unaddressable_function
                 : diag::warn_cxx20_compat_use_of_unaddressable_function);
        if (FD->isImplicitlyInstantiable()) {
          // Require a definition here because a normal attempt at
          // instantiation for a builtin will be ignored, and we won't try
          // again later. We assume that the definition of the template
          // precedes this use.
          InstantiateFunctionDefinition(E->getBeginLoc(), FD,
                                        /*Recursive=*/false,
                                        /*DefinitionRequired=*/true,
                                        /*AtEndOfTU=*/false);
        }
        // Produce a properly-typed reference to the function.
        CXXScopeSpec SS;
        SS.Adopt(DRE->getQualifierLoc());
        TemplateArgumentListInfo TemplateArgs;
        DRE->copyTemplateArgumentsInto(TemplateArgs);
        return BuildDeclRefExpr(
            FD, FD->getType(), VK_LValue, DRE->getNameInfo(),
            DRE->hasQualifier() ? &SS : nullptr, DRE->getFoundDecl(),
            DRE->getTemplateKeywordLoc(),
            DRE->hasExplicitTemplateArgs() ? &TemplateArgs : nullptr);
      }
    }

    Diag(E->getBeginLoc(), diag::err_builtin_fn_use);
    return ExprError();
  }

  case BuiltinType::IncompleteMatrixIdx:
    Diag(cast<MatrixSubscriptExpr>(E->IgnoreParens())
             ->getRowIdx()
             ->getBeginLoc(),
         diag::err_matrix_incomplete_index);
    return ExprError();

  // Expressions of unknown type.
  case BuiltinType::ArraySection:
    Diag(E->getBeginLoc(), diag::err_array_section_use)
        << cast<ArraySectionExpr>(E)->isOMPArraySection();
    return ExprError();

  // Expressions of unknown type.
  case BuiltinType::OMPArrayShaping:
    return ExprError(Diag(E->getBeginLoc(), diag::err_omp_array_shaping_use));

  case BuiltinType::OMPIterator:
    return ExprError(Diag(E->getBeginLoc(), diag::err_omp_iterator_use));

  // Everything else should be impossible.
#define IMAGE_TYPE(ImgType, Id, SingletonId, Access, Suffix) \
  case BuiltinType::Id:
#include "clang/Basic/OpenCLImageTypes.def"
#define EXT_OPAQUE_TYPE(ExtType, Id, Ext) \
  case BuiltinType::Id:
#include "clang/Basic/OpenCLExtensionTypes.def"
#define SVE_TYPE(Name, Id, SingletonId) \
  case BuiltinType::Id:
#include "clang/Basic/AArch64SVEACLETypes.def"
#define PPC_VECTOR_TYPE(Name, Id, Size) \
  case BuiltinType::Id:
#include "clang/Basic/PPCTypes.def"
#define RVV_TYPE(Name, Id, SingletonId) case BuiltinType::Id:
#include "clang/Basic/RISCVVTypes.def"
#define WASM_TYPE(Name, Id, SingletonId) case BuiltinType::Id:
#include "clang/Basic/WebAssemblyReferenceTypes.def"
#define AMDGPU_TYPE(Name, Id, SingletonId) case BuiltinType::Id:
#include "clang/Basic/AMDGPUTypes.def"
#define BUILTIN_TYPE(Id, SingletonId) case BuiltinType::Id:
#define PLACEHOLDER_TYPE(Id, SingletonId)
#include "clang/AST/BuiltinTypes.def"
    break;
  }

  llvm_unreachable("invalid placeholder type!");
}

bool Sema::CheckCaseExpression(Expr *E) {
  if (E->isTypeDependent())
    return true;
  if (E->isValueDependent() || E->isIntegerConstantExpr(Context))
    return E->getType()->isIntegralOrEnumerationType();
  return false;
}

ExprResult Sema::CreateRecoveryExpr(SourceLocation Begin, SourceLocation End,
                                    ArrayRef<Expr *> SubExprs, QualType T) {
  if (!Context.getLangOpts().RecoveryAST)
    return ExprError();

  if (isSFINAEContext())
    return ExprError();

  if (T.isNull() || T->isUndeducedType() ||
      !Context.getLangOpts().RecoveryASTType)
    // We don't know the concrete type, fallback to dependent type.
    T = Context.DependentTy;

  return RecoveryExpr::Create(Context, T, Begin, End, SubExprs);
}
