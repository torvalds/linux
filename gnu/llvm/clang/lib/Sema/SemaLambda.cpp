//===--- SemaLambda.cpp - Semantic Analysis for C++11 Lambdas -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file implements semantic analysis for C++ lambda expressions.
//
//===----------------------------------------------------------------------===//
#include "clang/Sema/SemaLambda.h"
#include "TypeLocBuilder.h"
#include "clang/AST/ASTLambda.h"
#include "clang/AST/CXXInheritance.h"
#include "clang/AST/ExprCXX.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Sema/DeclSpec.h"
#include "clang/Sema/Initialization.h"
#include "clang/Sema/Lookup.h"
#include "clang/Sema/Scope.h"
#include "clang/Sema/ScopeInfo.h"
#include "clang/Sema/SemaCUDA.h"
#include "clang/Sema/SemaInternal.h"
#include "clang/Sema/SemaOpenMP.h"
#include "clang/Sema/Template.h"
#include "llvm/ADT/STLExtras.h"
#include <optional>
using namespace clang;
using namespace sema;

/// Examines the FunctionScopeInfo stack to determine the nearest
/// enclosing lambda (to the current lambda) that is 'capture-ready' for
/// the variable referenced in the current lambda (i.e. \p VarToCapture).
/// If successful, returns the index into Sema's FunctionScopeInfo stack
/// of the capture-ready lambda's LambdaScopeInfo.
///
/// Climbs down the stack of lambdas (deepest nested lambda - i.e. current
/// lambda - is on top) to determine the index of the nearest enclosing/outer
/// lambda that is ready to capture the \p VarToCapture being referenced in
/// the current lambda.
/// As we climb down the stack, we want the index of the first such lambda -
/// that is the lambda with the highest index that is 'capture-ready'.
///
/// A lambda 'L' is capture-ready for 'V' (var or this) if:
///  - its enclosing context is non-dependent
///  - and if the chain of lambdas between L and the lambda in which
///    V is potentially used (i.e. the lambda at the top of the scope info
///    stack), can all capture or have already captured V.
/// If \p VarToCapture is 'null' then we are trying to capture 'this'.
///
/// Note that a lambda that is deemed 'capture-ready' still needs to be checked
/// for whether it is 'capture-capable' (see
/// getStackIndexOfNearestEnclosingCaptureCapableLambda), before it can truly
/// capture.
///
/// \param FunctionScopes - Sema's stack of nested FunctionScopeInfo's (which a
///  LambdaScopeInfo inherits from).  The current/deepest/innermost lambda
///  is at the top of the stack and has the highest index.
/// \param VarToCapture - the variable to capture.  If NULL, capture 'this'.
///
/// \returns An std::optional<unsigned> Index that if evaluates to 'true'
/// contains the index (into Sema's FunctionScopeInfo stack) of the innermost
/// lambda which is capture-ready.  If the return value evaluates to 'false'
/// then no lambda is capture-ready for \p VarToCapture.

static inline std::optional<unsigned>
getStackIndexOfNearestEnclosingCaptureReadyLambda(
    ArrayRef<const clang::sema::FunctionScopeInfo *> FunctionScopes,
    ValueDecl *VarToCapture) {
  // Label failure to capture.
  const std::optional<unsigned> NoLambdaIsCaptureReady;

  // Ignore all inner captured regions.
  unsigned CurScopeIndex = FunctionScopes.size() - 1;
  while (CurScopeIndex > 0 && isa<clang::sema::CapturedRegionScopeInfo>(
                                  FunctionScopes[CurScopeIndex]))
    --CurScopeIndex;
  assert(
      isa<clang::sema::LambdaScopeInfo>(FunctionScopes[CurScopeIndex]) &&
      "The function on the top of sema's function-info stack must be a lambda");

  // If VarToCapture is null, we are attempting to capture 'this'.
  const bool IsCapturingThis = !VarToCapture;
  const bool IsCapturingVariable = !IsCapturingThis;

  // Start with the current lambda at the top of the stack (highest index).
  DeclContext *EnclosingDC =
      cast<sema::LambdaScopeInfo>(FunctionScopes[CurScopeIndex])->CallOperator;

  do {
    const clang::sema::LambdaScopeInfo *LSI =
        cast<sema::LambdaScopeInfo>(FunctionScopes[CurScopeIndex]);
    // IF we have climbed down to an intervening enclosing lambda that contains
    // the variable declaration - it obviously can/must not capture the
    // variable.
    // Since its enclosing DC is dependent, all the lambdas between it and the
    // innermost nested lambda are dependent (otherwise we wouldn't have
    // arrived here) - so we don't yet have a lambda that can capture the
    // variable.
    if (IsCapturingVariable &&
        VarToCapture->getDeclContext()->Equals(EnclosingDC))
      return NoLambdaIsCaptureReady;

    // For an enclosing lambda to be capture ready for an entity, all
    // intervening lambda's have to be able to capture that entity. If even
    // one of the intervening lambda's is not capable of capturing the entity
    // then no enclosing lambda can ever capture that entity.
    // For e.g.
    // const int x = 10;
    // [=](auto a) {    #1
    //   [](auto b) {   #2 <-- an intervening lambda that can never capture 'x'
    //    [=](auto c) { #3
    //       f(x, c);  <-- can not lead to x's speculative capture by #1 or #2
    //    }; }; };
    // If they do not have a default implicit capture, check to see
    // if the entity has already been explicitly captured.
    // If even a single dependent enclosing lambda lacks the capability
    // to ever capture this variable, there is no further enclosing
    // non-dependent lambda that can capture this variable.
    if (LSI->ImpCaptureStyle == sema::LambdaScopeInfo::ImpCap_None) {
      if (IsCapturingVariable && !LSI->isCaptured(VarToCapture))
        return NoLambdaIsCaptureReady;
      if (IsCapturingThis && !LSI->isCXXThisCaptured())
        return NoLambdaIsCaptureReady;
    }
    EnclosingDC = getLambdaAwareParentOfDeclContext(EnclosingDC);

    assert(CurScopeIndex);
    --CurScopeIndex;
  } while (!EnclosingDC->isTranslationUnit() &&
           EnclosingDC->isDependentContext() &&
           isLambdaCallOperator(EnclosingDC));

  assert(CurScopeIndex < (FunctionScopes.size() - 1));
  // If the enclosingDC is not dependent, then the immediately nested lambda
  // (one index above) is capture-ready.
  if (!EnclosingDC->isDependentContext())
    return CurScopeIndex + 1;
  return NoLambdaIsCaptureReady;
}

/// Examines the FunctionScopeInfo stack to determine the nearest
/// enclosing lambda (to the current lambda) that is 'capture-capable' for
/// the variable referenced in the current lambda (i.e. \p VarToCapture).
/// If successful, returns the index into Sema's FunctionScopeInfo stack
/// of the capture-capable lambda's LambdaScopeInfo.
///
/// Given the current stack of lambdas being processed by Sema and
/// the variable of interest, to identify the nearest enclosing lambda (to the
/// current lambda at the top of the stack) that can truly capture
/// a variable, it has to have the following two properties:
///  a) 'capture-ready' - be the innermost lambda that is 'capture-ready':
///     - climb down the stack (i.e. starting from the innermost and examining
///       each outer lambda step by step) checking if each enclosing
///       lambda can either implicitly or explicitly capture the variable.
///       Record the first such lambda that is enclosed in a non-dependent
///       context. If no such lambda currently exists return failure.
///  b) 'capture-capable' - make sure the 'capture-ready' lambda can truly
///  capture the variable by checking all its enclosing lambdas:
///     - check if all outer lambdas enclosing the 'capture-ready' lambda
///       identified above in 'a' can also capture the variable (this is done
///       via tryCaptureVariable for variables and CheckCXXThisCapture for
///       'this' by passing in the index of the Lambda identified in step 'a')
///
/// \param FunctionScopes - Sema's stack of nested FunctionScopeInfo's (which a
/// LambdaScopeInfo inherits from).  The current/deepest/innermost lambda
/// is at the top of the stack.
///
/// \param VarToCapture - the variable to capture.  If NULL, capture 'this'.
///
///
/// \returns An std::optional<unsigned> Index that if evaluates to 'true'
/// contains the index (into Sema's FunctionScopeInfo stack) of the innermost
/// lambda which is capture-capable.  If the return value evaluates to 'false'
/// then no lambda is capture-capable for \p VarToCapture.

std::optional<unsigned>
clang::getStackIndexOfNearestEnclosingCaptureCapableLambda(
    ArrayRef<const sema::FunctionScopeInfo *> FunctionScopes,
    ValueDecl *VarToCapture, Sema &S) {

  const std::optional<unsigned> NoLambdaIsCaptureCapable;

  const std::optional<unsigned> OptionalStackIndex =
      getStackIndexOfNearestEnclosingCaptureReadyLambda(FunctionScopes,
                                                        VarToCapture);
  if (!OptionalStackIndex)
    return NoLambdaIsCaptureCapable;

  const unsigned IndexOfCaptureReadyLambda = *OptionalStackIndex;
  assert(((IndexOfCaptureReadyLambda != (FunctionScopes.size() - 1)) ||
          S.getCurGenericLambda()) &&
         "The capture ready lambda for a potential capture can only be the "
         "current lambda if it is a generic lambda");

  const sema::LambdaScopeInfo *const CaptureReadyLambdaLSI =
      cast<sema::LambdaScopeInfo>(FunctionScopes[IndexOfCaptureReadyLambda]);

  // If VarToCapture is null, we are attempting to capture 'this'
  const bool IsCapturingThis = !VarToCapture;
  const bool IsCapturingVariable = !IsCapturingThis;

  if (IsCapturingVariable) {
    // Check if the capture-ready lambda can truly capture the variable, by
    // checking whether all enclosing lambdas of the capture-ready lambda allow
    // the capture - i.e. make sure it is capture-capable.
    QualType CaptureType, DeclRefType;
    const bool CanCaptureVariable =
        !S.tryCaptureVariable(VarToCapture,
                              /*ExprVarIsUsedInLoc*/ SourceLocation(),
                              clang::Sema::TryCapture_Implicit,
                              /*EllipsisLoc*/ SourceLocation(),
                              /*BuildAndDiagnose*/ false, CaptureType,
                              DeclRefType, &IndexOfCaptureReadyLambda);
    if (!CanCaptureVariable)
      return NoLambdaIsCaptureCapable;
  } else {
    // Check if the capture-ready lambda can truly capture 'this' by checking
    // whether all enclosing lambdas of the capture-ready lambda can capture
    // 'this'.
    const bool CanCaptureThis =
        !S.CheckCXXThisCapture(
             CaptureReadyLambdaLSI->PotentialThisCaptureLocation,
             /*Explicit*/ false, /*BuildAndDiagnose*/ false,
             &IndexOfCaptureReadyLambda);
    if (!CanCaptureThis)
      return NoLambdaIsCaptureCapable;
  }
  return IndexOfCaptureReadyLambda;
}

static inline TemplateParameterList *
getGenericLambdaTemplateParameterList(LambdaScopeInfo *LSI, Sema &SemaRef) {
  if (!LSI->GLTemplateParameterList && !LSI->TemplateParams.empty()) {
    LSI->GLTemplateParameterList = TemplateParameterList::Create(
        SemaRef.Context,
        /*Template kw loc*/ SourceLocation(),
        /*L angle loc*/ LSI->ExplicitTemplateParamsRange.getBegin(),
        LSI->TemplateParams,
        /*R angle loc*/LSI->ExplicitTemplateParamsRange.getEnd(),
        LSI->RequiresClause.get());
  }
  return LSI->GLTemplateParameterList;
}

CXXRecordDecl *
Sema::createLambdaClosureType(SourceRange IntroducerRange, TypeSourceInfo *Info,
                              unsigned LambdaDependencyKind,
                              LambdaCaptureDefault CaptureDefault) {
  DeclContext *DC = CurContext;
  while (!(DC->isFunctionOrMethod() || DC->isRecord() || DC->isFileContext()))
    DC = DC->getParent();

  bool IsGenericLambda =
      Info && getGenericLambdaTemplateParameterList(getCurLambda(), *this);
  // Start constructing the lambda class.
  CXXRecordDecl *Class = CXXRecordDecl::CreateLambda(
      Context, DC, Info, IntroducerRange.getBegin(), LambdaDependencyKind,
      IsGenericLambda, CaptureDefault);
  DC->addDecl(Class);

  return Class;
}

/// Determine whether the given context is or is enclosed in an inline
/// function.
static bool isInInlineFunction(const DeclContext *DC) {
  while (!DC->isFileContext()) {
    if (const FunctionDecl *FD = dyn_cast<FunctionDecl>(DC))
      if (FD->isInlined())
        return true;

    DC = DC->getLexicalParent();
  }

  return false;
}

std::tuple<MangleNumberingContext *, Decl *>
Sema::getCurrentMangleNumberContext(const DeclContext *DC) {
  // Compute the context for allocating mangling numbers in the current
  // expression, if the ABI requires them.
  Decl *ManglingContextDecl = ExprEvalContexts.back().ManglingContextDecl;

  enum ContextKind {
    Normal,
    DefaultArgument,
    DataMember,
    InlineVariable,
    TemplatedVariable,
    Concept
  } Kind = Normal;

  bool IsInNonspecializedTemplate =
      inTemplateInstantiation() || CurContext->isDependentContext();

  // Default arguments of member function parameters that appear in a class
  // definition, as well as the initializers of data members, receive special
  // treatment. Identify them.
  if (ManglingContextDecl) {
    if (ParmVarDecl *Param = dyn_cast<ParmVarDecl>(ManglingContextDecl)) {
      if (const DeclContext *LexicalDC
          = Param->getDeclContext()->getLexicalParent())
        if (LexicalDC->isRecord())
          Kind = DefaultArgument;
    } else if (VarDecl *Var = dyn_cast<VarDecl>(ManglingContextDecl)) {
      if (Var->getMostRecentDecl()->isInline())
        Kind = InlineVariable;
      else if (Var->getDeclContext()->isRecord() && IsInNonspecializedTemplate)
        Kind = TemplatedVariable;
      else if (Var->getDescribedVarTemplate())
        Kind = TemplatedVariable;
      else if (auto *VTS = dyn_cast<VarTemplateSpecializationDecl>(Var)) {
        if (!VTS->isExplicitSpecialization())
          Kind = TemplatedVariable;
      }
    } else if (isa<FieldDecl>(ManglingContextDecl)) {
      Kind = DataMember;
    } else if (isa<ImplicitConceptSpecializationDecl>(ManglingContextDecl)) {
      Kind = Concept;
    }
  }

  // Itanium ABI [5.1.7]:
  //   In the following contexts [...] the one-definition rule requires closure
  //   types in different translation units to "correspond":
  switch (Kind) {
  case Normal: {
    //  -- the bodies of inline or templated functions
    if ((IsInNonspecializedTemplate &&
         !(ManglingContextDecl && isa<ParmVarDecl>(ManglingContextDecl))) ||
        isInInlineFunction(CurContext)) {
      while (auto *CD = dyn_cast<CapturedDecl>(DC))
        DC = CD->getParent();
      return std::make_tuple(&Context.getManglingNumberContext(DC), nullptr);
    }

    return std::make_tuple(nullptr, nullptr);
  }

  case Concept:
    // Concept definitions aren't code generated and thus aren't mangled,
    // however the ManglingContextDecl is important for the purposes of
    // re-forming the template argument list of the lambda for constraint
    // evaluation.
  case DataMember:
    //  -- default member initializers
  case DefaultArgument:
    //  -- default arguments appearing in class definitions
  case InlineVariable:
  case TemplatedVariable:
    //  -- the initializers of inline or templated variables
    return std::make_tuple(
        &Context.getManglingNumberContext(ASTContext::NeedExtraManglingDecl,
                                          ManglingContextDecl),
        ManglingContextDecl);
  }

  llvm_unreachable("unexpected context");
}

static QualType
buildTypeForLambdaCallOperator(Sema &S, clang::CXXRecordDecl *Class,
                               TemplateParameterList *TemplateParams,
                               TypeSourceInfo *MethodTypeInfo) {
  assert(MethodTypeInfo && "expected a non null type");

  QualType MethodType = MethodTypeInfo->getType();
  // If a lambda appears in a dependent context or is a generic lambda (has
  // template parameters) and has an 'auto' return type, deduce it to a
  // dependent type.
  if (Class->isDependentContext() || TemplateParams) {
    const FunctionProtoType *FPT = MethodType->castAs<FunctionProtoType>();
    QualType Result = FPT->getReturnType();
    if (Result->isUndeducedType()) {
      Result = S.SubstAutoTypeDependent(Result);
      MethodType = S.Context.getFunctionType(Result, FPT->getParamTypes(),
                                             FPT->getExtProtoInfo());
    }
  }
  return MethodType;
}

// [C++2b] [expr.prim.lambda.closure] p4
//  Given a lambda with a lambda-capture, the type of the explicit object
//  parameter, if any, of the lambda's function call operator (possibly
//  instantiated from a function call operator template) shall be either:
//  - the closure type,
//  - class type publicly and unambiguously derived from the closure type, or
//  - a reference to a possibly cv-qualified such type.
bool Sema::DiagnoseInvalidExplicitObjectParameterInLambda(
    CXXMethodDecl *Method, SourceLocation CallLoc) {
  if (!isLambdaCallWithExplicitObjectParameter(Method))
    return false;
  CXXRecordDecl *RD = Method->getParent();
  if (Method->getType()->isDependentType())
    return false;
  if (RD->isCapturelessLambda())
    return false;

  ParmVarDecl *Param = Method->getParamDecl(0);
  QualType ExplicitObjectParameterType = Param->getType()
                                             .getNonReferenceType()
                                             .getUnqualifiedType()
                                             .getDesugaredType(getASTContext());
  QualType LambdaType = getASTContext().getRecordType(RD);
  if (LambdaType == ExplicitObjectParameterType)
    return false;

  // Don't check the same instantiation twice.
  //
  // If this call operator is ill-formed, there is no point in issuing
  // a diagnostic every time it is called because the problem is in the
  // definition of the derived type, not at the call site.
  //
  // FIXME: Move this check to where we instantiate the method? This should
  // be possible, but the naive approach of just marking the method as invalid
  // leads to us emitting more diagnostics than we should have to for this case
  // (1 error here *and* 1 error about there being no matching overload at the
  // call site). It might be possible to avoid that by also checking if there
  // is an empty cast path for the method stored in the context (signalling that
  // we've already diagnosed it) and then just not building the call, but that
  // doesn't really seem any simpler than diagnosing it at the call site...
  if (auto It = Context.LambdaCastPaths.find(Method);
      It != Context.LambdaCastPaths.end())
    return It->second.empty();

  CXXCastPath &Path = Context.LambdaCastPaths[Method];
  CXXBasePaths Paths(/*FindAmbiguities=*/true, /*RecordPaths=*/true,
                     /*DetectVirtual=*/false);
  if (!IsDerivedFrom(RD->getLocation(), ExplicitObjectParameterType, LambdaType,
                     Paths)) {
    Diag(Param->getLocation(), diag::err_invalid_explicit_object_type_in_lambda)
        << ExplicitObjectParameterType;
    return true;
  }

  if (Paths.isAmbiguous(LambdaType->getCanonicalTypeUnqualified())) {
    std::string PathsDisplay = getAmbiguousPathsDisplayString(Paths);
    Diag(CallLoc, diag::err_explicit_object_lambda_ambiguous_base)
        << LambdaType << PathsDisplay;
    return true;
  }

  if (CheckBaseClassAccess(CallLoc, LambdaType, ExplicitObjectParameterType,
                           Paths.front(),
                           diag::err_explicit_object_lambda_inaccessible_base))
    return true;

  BuildBasePathArray(Paths, Path);
  return false;
}

void Sema::handleLambdaNumbering(
    CXXRecordDecl *Class, CXXMethodDecl *Method,
    std::optional<CXXRecordDecl::LambdaNumbering> NumberingOverride) {
  if (NumberingOverride) {
    Class->setLambdaNumbering(*NumberingOverride);
    return;
  }

  ContextRAII ManglingContext(*this, Class->getDeclContext());

  auto getMangleNumberingContext =
      [this](CXXRecordDecl *Class,
             Decl *ManglingContextDecl) -> MangleNumberingContext * {
    // Get mangle numbering context if there's any extra decl context.
    if (ManglingContextDecl)
      return &Context.getManglingNumberContext(
          ASTContext::NeedExtraManglingDecl, ManglingContextDecl);
    // Otherwise, from that lambda's decl context.
    auto DC = Class->getDeclContext();
    while (auto *CD = dyn_cast<CapturedDecl>(DC))
      DC = CD->getParent();
    return &Context.getManglingNumberContext(DC);
  };

  CXXRecordDecl::LambdaNumbering Numbering;
  MangleNumberingContext *MCtx;
  std::tie(MCtx, Numbering.ContextDecl) =
      getCurrentMangleNumberContext(Class->getDeclContext());
  if (!MCtx && (getLangOpts().CUDA || getLangOpts().SYCLIsDevice ||
                getLangOpts().SYCLIsHost)) {
    // Force lambda numbering in CUDA/HIP as we need to name lambdas following
    // ODR. Both device- and host-compilation need to have a consistent naming
    // on kernel functions. As lambdas are potential part of these `__global__`
    // function names, they needs numbering following ODR.
    // Also force for SYCL, since we need this for the
    // __builtin_sycl_unique_stable_name implementation, which depends on lambda
    // mangling.
    MCtx = getMangleNumberingContext(Class, Numbering.ContextDecl);
    assert(MCtx && "Retrieving mangle numbering context failed!");
    Numbering.HasKnownInternalLinkage = true;
  }
  if (MCtx) {
    Numbering.IndexInContext = MCtx->getNextLambdaIndex();
    Numbering.ManglingNumber = MCtx->getManglingNumber(Method);
    Numbering.DeviceManglingNumber = MCtx->getDeviceManglingNumber(Method);
    Class->setLambdaNumbering(Numbering);

    if (auto *Source =
            dyn_cast_or_null<ExternalSemaSource>(Context.getExternalSource()))
      Source->AssignedLambdaNumbering(Class);
  }
}

static void buildLambdaScopeReturnType(Sema &S, LambdaScopeInfo *LSI,
                                       CXXMethodDecl *CallOperator,
                                       bool ExplicitResultType) {
  if (ExplicitResultType) {
    LSI->HasImplicitReturnType = false;
    LSI->ReturnType = CallOperator->getReturnType();
    if (!LSI->ReturnType->isDependentType() && !LSI->ReturnType->isVoidType())
      S.RequireCompleteType(CallOperator->getBeginLoc(), LSI->ReturnType,
                            diag::err_lambda_incomplete_result);
  } else {
    LSI->HasImplicitReturnType = true;
  }
}

void Sema::buildLambdaScope(LambdaScopeInfo *LSI, CXXMethodDecl *CallOperator,
                            SourceRange IntroducerRange,
                            LambdaCaptureDefault CaptureDefault,
                            SourceLocation CaptureDefaultLoc,
                            bool ExplicitParams, bool Mutable) {
  LSI->CallOperator = CallOperator;
  CXXRecordDecl *LambdaClass = CallOperator->getParent();
  LSI->Lambda = LambdaClass;
  if (CaptureDefault == LCD_ByCopy)
    LSI->ImpCaptureStyle = LambdaScopeInfo::ImpCap_LambdaByval;
  else if (CaptureDefault == LCD_ByRef)
    LSI->ImpCaptureStyle = LambdaScopeInfo::ImpCap_LambdaByref;
  LSI->CaptureDefaultLoc = CaptureDefaultLoc;
  LSI->IntroducerRange = IntroducerRange;
  LSI->ExplicitParams = ExplicitParams;
  LSI->Mutable = Mutable;
}

void Sema::finishLambdaExplicitCaptures(LambdaScopeInfo *LSI) {
  LSI->finishedExplicitCaptures();
}

void Sema::ActOnLambdaExplicitTemplateParameterList(
    LambdaIntroducer &Intro, SourceLocation LAngleLoc,
    ArrayRef<NamedDecl *> TParams, SourceLocation RAngleLoc,
    ExprResult RequiresClause) {
  LambdaScopeInfo *LSI = getCurLambda();
  assert(LSI && "Expected a lambda scope");
  assert(LSI->NumExplicitTemplateParams == 0 &&
         "Already acted on explicit template parameters");
  assert(LSI->TemplateParams.empty() &&
         "Explicit template parameters should come "
         "before invented (auto) ones");
  assert(!TParams.empty() &&
         "No template parameters to act on");
  LSI->TemplateParams.append(TParams.begin(), TParams.end());
  LSI->NumExplicitTemplateParams = TParams.size();
  LSI->ExplicitTemplateParamsRange = {LAngleLoc, RAngleLoc};
  LSI->RequiresClause = RequiresClause;
}

/// If this expression is an enumerator-like expression of some type
/// T, return the type T; otherwise, return null.
///
/// Pointer comparisons on the result here should always work because
/// it's derived from either the parent of an EnumConstantDecl
/// (i.e. the definition) or the declaration returned by
/// EnumType::getDecl() (i.e. the definition).
static EnumDecl *findEnumForBlockReturn(Expr *E) {
  // An expression is an enumerator-like expression of type T if,
  // ignoring parens and parens-like expressions:
  E = E->IgnoreParens();

  //  - it is an enumerator whose enum type is T or
  if (DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(E)) {
    if (EnumConstantDecl *D
          = dyn_cast<EnumConstantDecl>(DRE->getDecl())) {
      return cast<EnumDecl>(D->getDeclContext());
    }
    return nullptr;
  }

  //  - it is a comma expression whose RHS is an enumerator-like
  //    expression of type T or
  if (BinaryOperator *BO = dyn_cast<BinaryOperator>(E)) {
    if (BO->getOpcode() == BO_Comma)
      return findEnumForBlockReturn(BO->getRHS());
    return nullptr;
  }

  //  - it is a statement-expression whose value expression is an
  //    enumerator-like expression of type T or
  if (StmtExpr *SE = dyn_cast<StmtExpr>(E)) {
    if (Expr *last = dyn_cast_or_null<Expr>(SE->getSubStmt()->body_back()))
      return findEnumForBlockReturn(last);
    return nullptr;
  }

  //   - it is a ternary conditional operator (not the GNU ?:
  //     extension) whose second and third operands are
  //     enumerator-like expressions of type T or
  if (ConditionalOperator *CO = dyn_cast<ConditionalOperator>(E)) {
    if (EnumDecl *ED = findEnumForBlockReturn(CO->getTrueExpr()))
      if (ED == findEnumForBlockReturn(CO->getFalseExpr()))
        return ED;
    return nullptr;
  }

  // (implicitly:)
  //   - it is an implicit integral conversion applied to an
  //     enumerator-like expression of type T or
  if (ImplicitCastExpr *ICE = dyn_cast<ImplicitCastExpr>(E)) {
    // We can sometimes see integral conversions in valid
    // enumerator-like expressions.
    if (ICE->getCastKind() == CK_IntegralCast)
      return findEnumForBlockReturn(ICE->getSubExpr());

    // Otherwise, just rely on the type.
  }

  //   - it is an expression of that formal enum type.
  if (const EnumType *ET = E->getType()->getAs<EnumType>()) {
    return ET->getDecl();
  }

  // Otherwise, nope.
  return nullptr;
}

/// Attempt to find a type T for which the returned expression of the
/// given statement is an enumerator-like expression of that type.
static EnumDecl *findEnumForBlockReturn(ReturnStmt *ret) {
  if (Expr *retValue = ret->getRetValue())
    return findEnumForBlockReturn(retValue);
  return nullptr;
}

/// Attempt to find a common type T for which all of the returned
/// expressions in a block are enumerator-like expressions of that
/// type.
static EnumDecl *findCommonEnumForBlockReturns(ArrayRef<ReturnStmt*> returns) {
  ArrayRef<ReturnStmt*>::iterator i = returns.begin(), e = returns.end();

  // Try to find one for the first return.
  EnumDecl *ED = findEnumForBlockReturn(*i);
  if (!ED) return nullptr;

  // Check that the rest of the returns have the same enum.
  for (++i; i != e; ++i) {
    if (findEnumForBlockReturn(*i) != ED)
      return nullptr;
  }

  // Never infer an anonymous enum type.
  if (!ED->hasNameForLinkage()) return nullptr;

  return ED;
}

/// Adjust the given return statements so that they formally return
/// the given type.  It should require, at most, an IntegralCast.
static void adjustBlockReturnsToEnum(Sema &S, ArrayRef<ReturnStmt*> returns,
                                     QualType returnType) {
  for (ArrayRef<ReturnStmt*>::iterator
         i = returns.begin(), e = returns.end(); i != e; ++i) {
    ReturnStmt *ret = *i;
    Expr *retValue = ret->getRetValue();
    if (S.Context.hasSameType(retValue->getType(), returnType))
      continue;

    // Right now we only support integral fixup casts.
    assert(returnType->isIntegralOrUnscopedEnumerationType());
    assert(retValue->getType()->isIntegralOrUnscopedEnumerationType());

    ExprWithCleanups *cleanups = dyn_cast<ExprWithCleanups>(retValue);

    Expr *E = (cleanups ? cleanups->getSubExpr() : retValue);
    E = ImplicitCastExpr::Create(S.Context, returnType, CK_IntegralCast, E,
                                 /*base path*/ nullptr, VK_PRValue,
                                 FPOptionsOverride());
    if (cleanups) {
      cleanups->setSubExpr(E);
    } else {
      ret->setRetValue(E);
    }
  }
}

void Sema::deduceClosureReturnType(CapturingScopeInfo &CSI) {
  assert(CSI.HasImplicitReturnType);
  // If it was ever a placeholder, it had to been deduced to DependentTy.
  assert(CSI.ReturnType.isNull() || !CSI.ReturnType->isUndeducedType());
  assert((!isa<LambdaScopeInfo>(CSI) || !getLangOpts().CPlusPlus14) &&
         "lambda expressions use auto deduction in C++14 onwards");

  // C++ core issue 975:
  //   If a lambda-expression does not include a trailing-return-type,
  //   it is as if the trailing-return-type denotes the following type:
  //     - if there are no return statements in the compound-statement,
  //       or all return statements return either an expression of type
  //       void or no expression or braced-init-list, the type void;
  //     - otherwise, if all return statements return an expression
  //       and the types of the returned expressions after
  //       lvalue-to-rvalue conversion (4.1 [conv.lval]),
  //       array-to-pointer conversion (4.2 [conv.array]), and
  //       function-to-pointer conversion (4.3 [conv.func]) are the
  //       same, that common type;
  //     - otherwise, the program is ill-formed.
  //
  // C++ core issue 1048 additionally removes top-level cv-qualifiers
  // from the types of returned expressions to match the C++14 auto
  // deduction rules.
  //
  // In addition, in blocks in non-C++ modes, if all of the return
  // statements are enumerator-like expressions of some type T, where
  // T has a name for linkage, then we infer the return type of the
  // block to be that type.

  // First case: no return statements, implicit void return type.
  ASTContext &Ctx = getASTContext();
  if (CSI.Returns.empty()) {
    // It's possible there were simply no /valid/ return statements.
    // In this case, the first one we found may have at least given us a type.
    if (CSI.ReturnType.isNull())
      CSI.ReturnType = Ctx.VoidTy;
    return;
  }

  // Second case: at least one return statement has dependent type.
  // Delay type checking until instantiation.
  assert(!CSI.ReturnType.isNull() && "We should have a tentative return type.");
  if (CSI.ReturnType->isDependentType())
    return;

  // Try to apply the enum-fuzz rule.
  if (!getLangOpts().CPlusPlus) {
    assert(isa<BlockScopeInfo>(CSI));
    const EnumDecl *ED = findCommonEnumForBlockReturns(CSI.Returns);
    if (ED) {
      CSI.ReturnType = Context.getTypeDeclType(ED);
      adjustBlockReturnsToEnum(*this, CSI.Returns, CSI.ReturnType);
      return;
    }
  }

  // Third case: only one return statement. Don't bother doing extra work!
  if (CSI.Returns.size() == 1)
    return;

  // General case: many return statements.
  // Check that they all have compatible return types.

  // We require the return types to strictly match here.
  // Note that we've already done the required promotions as part of
  // processing the return statement.
  for (const ReturnStmt *RS : CSI.Returns) {
    const Expr *RetE = RS->getRetValue();

    QualType ReturnType =
        (RetE ? RetE->getType() : Context.VoidTy).getUnqualifiedType();
    if (Context.getCanonicalFunctionResultType(ReturnType) ==
          Context.getCanonicalFunctionResultType(CSI.ReturnType)) {
      // Use the return type with the strictest possible nullability annotation.
      auto RetTyNullability = ReturnType->getNullability();
      auto BlockNullability = CSI.ReturnType->getNullability();
      if (BlockNullability &&
          (!RetTyNullability ||
           hasWeakerNullability(*RetTyNullability, *BlockNullability)))
        CSI.ReturnType = ReturnType;
      continue;
    }

    // FIXME: This is a poor diagnostic for ReturnStmts without expressions.
    // TODO: It's possible that the *first* return is the divergent one.
    Diag(RS->getBeginLoc(),
         diag::err_typecheck_missing_return_type_incompatible)
        << ReturnType << CSI.ReturnType << isa<LambdaScopeInfo>(CSI);
    // Continue iterating so that we keep emitting diagnostics.
  }
}

QualType Sema::buildLambdaInitCaptureInitialization(
    SourceLocation Loc, bool ByRef, SourceLocation EllipsisLoc,
    std::optional<unsigned> NumExpansions, IdentifierInfo *Id,
    bool IsDirectInit, Expr *&Init) {
  // Create an 'auto' or 'auto&' TypeSourceInfo that we can use to
  // deduce against.
  QualType DeductType = Context.getAutoDeductType();
  TypeLocBuilder TLB;
  AutoTypeLoc TL = TLB.push<AutoTypeLoc>(DeductType);
  TL.setNameLoc(Loc);
  if (ByRef) {
    DeductType = BuildReferenceType(DeductType, true, Loc, Id);
    assert(!DeductType.isNull() && "can't build reference to auto");
    TLB.push<ReferenceTypeLoc>(DeductType).setSigilLoc(Loc);
  }
  if (EllipsisLoc.isValid()) {
    if (Init->containsUnexpandedParameterPack()) {
      Diag(EllipsisLoc, getLangOpts().CPlusPlus20
                            ? diag::warn_cxx17_compat_init_capture_pack
                            : diag::ext_init_capture_pack);
      DeductType = Context.getPackExpansionType(DeductType, NumExpansions,
                                                /*ExpectPackInType=*/false);
      TLB.push<PackExpansionTypeLoc>(DeductType).setEllipsisLoc(EllipsisLoc);
    } else {
      // Just ignore the ellipsis for now and form a non-pack variable. We'll
      // diagnose this later when we try to capture it.
    }
  }
  TypeSourceInfo *TSI = TLB.getTypeSourceInfo(Context, DeductType);

  // Deduce the type of the init capture.
  QualType DeducedType = deduceVarTypeFromInitializer(
      /*VarDecl*/nullptr, DeclarationName(Id), DeductType, TSI,
      SourceRange(Loc, Loc), IsDirectInit, Init);
  if (DeducedType.isNull())
    return QualType();

  // Are we a non-list direct initialization?
  ParenListExpr *CXXDirectInit = dyn_cast<ParenListExpr>(Init);

  // Perform initialization analysis and ensure any implicit conversions
  // (such as lvalue-to-rvalue) are enforced.
  InitializedEntity Entity =
      InitializedEntity::InitializeLambdaCapture(Id, DeducedType, Loc);
  InitializationKind Kind =
      IsDirectInit
          ? (CXXDirectInit ? InitializationKind::CreateDirect(
                                 Loc, Init->getBeginLoc(), Init->getEndLoc())
                           : InitializationKind::CreateDirectList(Loc))
          : InitializationKind::CreateCopy(Loc, Init->getBeginLoc());

  MultiExprArg Args = Init;
  if (CXXDirectInit)
    Args =
        MultiExprArg(CXXDirectInit->getExprs(), CXXDirectInit->getNumExprs());
  QualType DclT;
  InitializationSequence InitSeq(*this, Entity, Kind, Args);
  ExprResult Result = InitSeq.Perform(*this, Entity, Kind, Args, &DclT);

  if (Result.isInvalid())
    return QualType();

  Init = Result.getAs<Expr>();
  return DeducedType;
}

VarDecl *Sema::createLambdaInitCaptureVarDecl(
    SourceLocation Loc, QualType InitCaptureType, SourceLocation EllipsisLoc,
    IdentifierInfo *Id, unsigned InitStyle, Expr *Init, DeclContext *DeclCtx) {
  // FIXME: Retain the TypeSourceInfo from buildLambdaInitCaptureInitialization
  // rather than reconstructing it here.
  TypeSourceInfo *TSI = Context.getTrivialTypeSourceInfo(InitCaptureType, Loc);
  if (auto PETL = TSI->getTypeLoc().getAs<PackExpansionTypeLoc>())
    PETL.setEllipsisLoc(EllipsisLoc);

  // Create a dummy variable representing the init-capture. This is not actually
  // used as a variable, and only exists as a way to name and refer to the
  // init-capture.
  // FIXME: Pass in separate source locations for '&' and identifier.
  VarDecl *NewVD = VarDecl::Create(Context, DeclCtx, Loc, Loc, Id,
                                   InitCaptureType, TSI, SC_Auto);
  NewVD->setInitCapture(true);
  NewVD->setReferenced(true);
  // FIXME: Pass in a VarDecl::InitializationStyle.
  NewVD->setInitStyle(static_cast<VarDecl::InitializationStyle>(InitStyle));
  NewVD->markUsed(Context);
  NewVD->setInit(Init);
  if (NewVD->isParameterPack())
    getCurLambda()->LocalPacks.push_back(NewVD);
  return NewVD;
}

void Sema::addInitCapture(LambdaScopeInfo *LSI, VarDecl *Var, bool ByRef) {
  assert(Var->isInitCapture() && "init capture flag should be set");
  LSI->addCapture(Var, /*isBlock=*/false, ByRef,
                  /*isNested=*/false, Var->getLocation(), SourceLocation(),
                  Var->getType(), /*Invalid=*/false);
}

// Unlike getCurLambda, getCurrentLambdaScopeUnsafe doesn't
// check that the current lambda is in a consistent or fully constructed state.
static LambdaScopeInfo *getCurrentLambdaScopeUnsafe(Sema &S) {
  assert(!S.FunctionScopes.empty());
  return cast<LambdaScopeInfo>(S.FunctionScopes[S.FunctionScopes.size() - 1]);
}

static TypeSourceInfo *
getDummyLambdaType(Sema &S, SourceLocation Loc = SourceLocation()) {
  // C++11 [expr.prim.lambda]p4:
  //   If a lambda-expression does not include a lambda-declarator, it is as
  //   if the lambda-declarator were ().
  FunctionProtoType::ExtProtoInfo EPI(S.Context.getDefaultCallingConvention(
      /*IsVariadic=*/false, /*IsCXXMethod=*/true));
  EPI.HasTrailingReturn = true;
  EPI.TypeQuals.addConst();
  LangAS AS = S.getDefaultCXXMethodAddrSpace();
  if (AS != LangAS::Default)
    EPI.TypeQuals.addAddressSpace(AS);

  // C++1y [expr.prim.lambda]:
  //   The lambda return type is 'auto', which is replaced by the
  //   trailing-return type if provided and/or deduced from 'return'
  //   statements
  // We don't do this before C++1y, because we don't support deduced return
  // types there.
  QualType DefaultTypeForNoTrailingReturn = S.getLangOpts().CPlusPlus14
                                                ? S.Context.getAutoDeductType()
                                                : S.Context.DependentTy;
  QualType MethodTy = S.Context.getFunctionType(DefaultTypeForNoTrailingReturn,
                                                std::nullopt, EPI);
  return S.Context.getTrivialTypeSourceInfo(MethodTy, Loc);
}

static TypeSourceInfo *getLambdaType(Sema &S, LambdaIntroducer &Intro,
                                     Declarator &ParamInfo, Scope *CurScope,
                                     SourceLocation Loc,
                                     bool &ExplicitResultType) {

  ExplicitResultType = false;

  assert(
      (ParamInfo.getDeclSpec().getStorageClassSpec() ==
           DeclSpec::SCS_unspecified ||
       ParamInfo.getDeclSpec().getStorageClassSpec() == DeclSpec::SCS_static) &&
      "Unexpected storage specifier");
  bool IsLambdaStatic =
      ParamInfo.getDeclSpec().getStorageClassSpec() == DeclSpec::SCS_static;

  TypeSourceInfo *MethodTyInfo;

  if (ParamInfo.getNumTypeObjects() == 0) {
    MethodTyInfo = getDummyLambdaType(S, Loc);
  } else {
    // Check explicit parameters
    S.CheckExplicitObjectLambda(ParamInfo);

    DeclaratorChunk::FunctionTypeInfo &FTI = ParamInfo.getFunctionTypeInfo();

    bool HasExplicitObjectParameter =
        ParamInfo.isExplicitObjectMemberFunction();

    ExplicitResultType = FTI.hasTrailingReturnType();
    if (!FTI.hasMutableQualifier() && !IsLambdaStatic &&
        !HasExplicitObjectParameter)
      FTI.getOrCreateMethodQualifiers().SetTypeQual(DeclSpec::TQ_const, Loc);

    if (ExplicitResultType && S.getLangOpts().HLSL) {
      QualType RetTy = FTI.getTrailingReturnType().get();
      if (!RetTy.isNull()) {
        // HLSL does not support specifying an address space on a lambda return
        // type.
        LangAS AddressSpace = RetTy.getAddressSpace();
        if (AddressSpace != LangAS::Default)
          S.Diag(FTI.getTrailingReturnTypeLoc(),
                 diag::err_return_value_with_address_space);
      }
    }

    MethodTyInfo = S.GetTypeForDeclarator(ParamInfo);
    assert(MethodTyInfo && "no type from lambda-declarator");

    // Check for unexpanded parameter packs in the method type.
    if (MethodTyInfo->getType()->containsUnexpandedParameterPack())
      S.DiagnoseUnexpandedParameterPack(Intro.Range.getBegin(), MethodTyInfo,
                                        S.UPPC_DeclarationType);
  }
  return MethodTyInfo;
}

CXXMethodDecl *Sema::CreateLambdaCallOperator(SourceRange IntroducerRange,
                                              CXXRecordDecl *Class) {

  // C++20 [expr.prim.lambda.closure]p3:
  // The closure type for a lambda-expression has a public inline function
  // call operator (for a non-generic lambda) or function call operator
  // template (for a generic lambda) whose parameters and return type are
  // described by the lambda-expression's parameter-declaration-clause
  // and trailing-return-type respectively.
  DeclarationName MethodName =
      Context.DeclarationNames.getCXXOperatorName(OO_Call);
  DeclarationNameLoc MethodNameLoc =
      DeclarationNameLoc::makeCXXOperatorNameLoc(IntroducerRange.getBegin());
  CXXMethodDecl *Method = CXXMethodDecl::Create(
      Context, Class, SourceLocation(),
      DeclarationNameInfo(MethodName, IntroducerRange.getBegin(),
                          MethodNameLoc),
      QualType(), /*Tinfo=*/nullptr, SC_None,
      getCurFPFeatures().isFPConstrained(),
      /*isInline=*/true, ConstexprSpecKind::Unspecified, SourceLocation(),
      /*TrailingRequiresClause=*/nullptr);
  Method->setAccess(AS_public);
  return Method;
}

void Sema::AddTemplateParametersToLambdaCallOperator(
    CXXMethodDecl *CallOperator, CXXRecordDecl *Class,
    TemplateParameterList *TemplateParams) {
  assert(TemplateParams && "no template parameters");
  FunctionTemplateDecl *TemplateMethod = FunctionTemplateDecl::Create(
      Context, Class, CallOperator->getLocation(), CallOperator->getDeclName(),
      TemplateParams, CallOperator);
  TemplateMethod->setAccess(AS_public);
  CallOperator->setDescribedFunctionTemplate(TemplateMethod);
}

void Sema::CompleteLambdaCallOperator(
    CXXMethodDecl *Method, SourceLocation LambdaLoc,
    SourceLocation CallOperatorLoc, Expr *TrailingRequiresClause,
    TypeSourceInfo *MethodTyInfo, ConstexprSpecKind ConstexprKind,
    StorageClass SC, ArrayRef<ParmVarDecl *> Params,
    bool HasExplicitResultType) {

  LambdaScopeInfo *LSI = getCurrentLambdaScopeUnsafe(*this);

  if (TrailingRequiresClause)
    Method->setTrailingRequiresClause(TrailingRequiresClause);

  TemplateParameterList *TemplateParams =
      getGenericLambdaTemplateParameterList(LSI, *this);

  DeclContext *DC = Method->getLexicalDeclContext();
  Method->setLexicalDeclContext(LSI->Lambda);
  if (TemplateParams) {
    FunctionTemplateDecl *TemplateMethod =
        Method->getDescribedFunctionTemplate();
    assert(TemplateMethod &&
           "AddTemplateParametersToLambdaCallOperator should have been called");

    LSI->Lambda->addDecl(TemplateMethod);
    TemplateMethod->setLexicalDeclContext(DC);
  } else {
    LSI->Lambda->addDecl(Method);
  }
  LSI->Lambda->setLambdaIsGeneric(TemplateParams);
  LSI->Lambda->setLambdaTypeInfo(MethodTyInfo);

  Method->setLexicalDeclContext(DC);
  Method->setLocation(LambdaLoc);
  Method->setInnerLocStart(CallOperatorLoc);
  Method->setTypeSourceInfo(MethodTyInfo);
  Method->setType(buildTypeForLambdaCallOperator(*this, LSI->Lambda,
                                                 TemplateParams, MethodTyInfo));
  Method->setConstexprKind(ConstexprKind);
  Method->setStorageClass(SC);
  if (!Params.empty()) {
    CheckParmsForFunctionDef(Params, /*CheckParameterNames=*/false);
    Method->setParams(Params);
    for (auto P : Method->parameters()) {
      assert(P && "null in a parameter list");
      P->setOwningFunction(Method);
    }
  }

  buildLambdaScopeReturnType(*this, LSI, Method, HasExplicitResultType);
}

void Sema::ActOnLambdaExpressionAfterIntroducer(LambdaIntroducer &Intro,
                                                Scope *CurrentScope) {

  LambdaScopeInfo *LSI = getCurLambda();
  assert(LSI && "LambdaScopeInfo should be on stack!");

  if (Intro.Default == LCD_ByCopy)
    LSI->ImpCaptureStyle = LambdaScopeInfo::ImpCap_LambdaByval;
  else if (Intro.Default == LCD_ByRef)
    LSI->ImpCaptureStyle = LambdaScopeInfo::ImpCap_LambdaByref;
  LSI->CaptureDefaultLoc = Intro.DefaultLoc;
  LSI->IntroducerRange = Intro.Range;
  LSI->AfterParameterList = false;

  assert(LSI->NumExplicitTemplateParams == 0);

  // Determine if we're within a context where we know that the lambda will
  // be dependent, because there are template parameters in scope.
  CXXRecordDecl::LambdaDependencyKind LambdaDependencyKind =
      CXXRecordDecl::LDK_Unknown;
  if (CurScope->getTemplateParamParent() != nullptr) {
    LambdaDependencyKind = CXXRecordDecl::LDK_AlwaysDependent;
  } else if (Scope *P = CurScope->getParent()) {
    // Given a lambda defined inside a requires expression,
    //
    // struct S {
    //   S(auto var) requires requires { [&] -> decltype(var) { }; }
    //   {}
    // };
    //
    // The parameter var is not injected into the function Decl at the point of
    // parsing lambda. In such scenarios, perceiving it as dependent could
    // result in the constraint being evaluated, which matches what GCC does.
    while (P->getEntity() && P->getEntity()->isRequiresExprBody())
      P = P->getParent();
    if (P->isFunctionDeclarationScope() &&
        llvm::any_of(P->decls(), [](Decl *D) {
          return isa<ParmVarDecl>(D) &&
                 cast<ParmVarDecl>(D)->getType()->isTemplateTypeParmType();
        }))
      LambdaDependencyKind = CXXRecordDecl::LDK_AlwaysDependent;
  }

  CXXRecordDecl *Class = createLambdaClosureType(
      Intro.Range, /*Info=*/nullptr, LambdaDependencyKind, Intro.Default);
  LSI->Lambda = Class;

  CXXMethodDecl *Method = CreateLambdaCallOperator(Intro.Range, Class);
  LSI->CallOperator = Method;
  Method->setLexicalDeclContext(CurContext);

  PushDeclContext(CurScope, Method);

  bool ContainsUnexpandedParameterPack = false;

  // Distinct capture names, for diagnostics.
  llvm::DenseMap<IdentifierInfo *, ValueDecl *> CaptureNames;

  // Handle explicit captures.
  SourceLocation PrevCaptureLoc =
      Intro.Default == LCD_None ? Intro.Range.getBegin() : Intro.DefaultLoc;
  for (auto C = Intro.Captures.begin(), E = Intro.Captures.end(); C != E;
       PrevCaptureLoc = C->Loc, ++C) {
    if (C->Kind == LCK_This || C->Kind == LCK_StarThis) {
      if (C->Kind == LCK_StarThis)
        Diag(C->Loc, !getLangOpts().CPlusPlus17
                         ? diag::ext_star_this_lambda_capture_cxx17
                         : diag::warn_cxx14_compat_star_this_lambda_capture);

      // C++11 [expr.prim.lambda]p8:
      //   An identifier or this shall not appear more than once in a
      //   lambda-capture.
      if (LSI->isCXXThisCaptured()) {
        Diag(C->Loc, diag::err_capture_more_than_once)
            << "'this'" << SourceRange(LSI->getCXXThisCapture().getLocation())
            << FixItHint::CreateRemoval(
                   SourceRange(getLocForEndOfToken(PrevCaptureLoc), C->Loc));
        continue;
      }

      // C++20 [expr.prim.lambda]p8:
      //  If a lambda-capture includes a capture-default that is =,
      //  each simple-capture of that lambda-capture shall be of the form
      //  "&identifier", "this", or "* this". [ Note: The form [&,this] is
      //  redundant but accepted for compatibility with ISO C++14. --end note ]
      if (Intro.Default == LCD_ByCopy && C->Kind != LCK_StarThis)
        Diag(C->Loc, !getLangOpts().CPlusPlus20
                         ? diag::ext_equals_this_lambda_capture_cxx20
                         : diag::warn_cxx17_compat_equals_this_lambda_capture);

      // C++11 [expr.prim.lambda]p12:
      //   If this is captured by a local lambda expression, its nearest
      //   enclosing function shall be a non-static member function.
      QualType ThisCaptureType = getCurrentThisType();
      if (ThisCaptureType.isNull()) {
        Diag(C->Loc, diag::err_this_capture) << true;
        continue;
      }

      CheckCXXThisCapture(C->Loc, /*Explicit=*/true, /*BuildAndDiagnose*/ true,
                          /*FunctionScopeIndexToStopAtPtr*/ nullptr,
                          C->Kind == LCK_StarThis);
      if (!LSI->Captures.empty())
        LSI->ExplicitCaptureRanges[LSI->Captures.size() - 1] = C->ExplicitRange;
      continue;
    }

    assert(C->Id && "missing identifier for capture");

    if (C->Init.isInvalid())
      continue;

    ValueDecl *Var = nullptr;
    if (C->Init.isUsable()) {
      Diag(C->Loc, getLangOpts().CPlusPlus14
                       ? diag::warn_cxx11_compat_init_capture
                       : diag::ext_init_capture);

      // If the initializer expression is usable, but the InitCaptureType
      // is not, then an error has occurred - so ignore the capture for now.
      // for e.g., [n{0}] { }; <-- if no <initializer_list> is included.
      // FIXME: we should create the init capture variable and mark it invalid
      // in this case.
      if (C->InitCaptureType.get().isNull())
        continue;

      if (C->Init.get()->containsUnexpandedParameterPack() &&
          !C->InitCaptureType.get()->getAs<PackExpansionType>())
        DiagnoseUnexpandedParameterPack(C->Init.get(), UPPC_Initializer);

      unsigned InitStyle;
      switch (C->InitKind) {
      case LambdaCaptureInitKind::NoInit:
        llvm_unreachable("not an init-capture?");
      case LambdaCaptureInitKind::CopyInit:
        InitStyle = VarDecl::CInit;
        break;
      case LambdaCaptureInitKind::DirectInit:
        InitStyle = VarDecl::CallInit;
        break;
      case LambdaCaptureInitKind::ListInit:
        InitStyle = VarDecl::ListInit;
        break;
      }
      Var = createLambdaInitCaptureVarDecl(C->Loc, C->InitCaptureType.get(),
                                           C->EllipsisLoc, C->Id, InitStyle,
                                           C->Init.get(), Method);
      assert(Var && "createLambdaInitCaptureVarDecl returned a null VarDecl?");
      if (auto *V = dyn_cast<VarDecl>(Var))
        CheckShadow(CurrentScope, V);
      PushOnScopeChains(Var, CurrentScope, false);
    } else {
      assert(C->InitKind == LambdaCaptureInitKind::NoInit &&
             "init capture has valid but null init?");

      // C++11 [expr.prim.lambda]p8:
      //   If a lambda-capture includes a capture-default that is &, the
      //   identifiers in the lambda-capture shall not be preceded by &.
      //   If a lambda-capture includes a capture-default that is =, [...]
      //   each identifier it contains shall be preceded by &.
      if (C->Kind == LCK_ByRef && Intro.Default == LCD_ByRef) {
        Diag(C->Loc, diag::err_reference_capture_with_reference_default)
            << FixItHint::CreateRemoval(
                SourceRange(getLocForEndOfToken(PrevCaptureLoc), C->Loc));
        continue;
      } else if (C->Kind == LCK_ByCopy && Intro.Default == LCD_ByCopy) {
        Diag(C->Loc, diag::err_copy_capture_with_copy_default)
            << FixItHint::CreateRemoval(
                SourceRange(getLocForEndOfToken(PrevCaptureLoc), C->Loc));
        continue;
      }

      // C++11 [expr.prim.lambda]p10:
      //   The identifiers in a capture-list are looked up using the usual
      //   rules for unqualified name lookup (3.4.1)
      DeclarationNameInfo Name(C->Id, C->Loc);
      LookupResult R(*this, Name, LookupOrdinaryName);
      LookupName(R, CurScope);
      if (R.isAmbiguous())
        continue;
      if (R.empty()) {
        // FIXME: Disable corrections that would add qualification?
        CXXScopeSpec ScopeSpec;
        DeclFilterCCC<VarDecl> Validator{};
        if (DiagnoseEmptyLookup(CurScope, ScopeSpec, R, Validator))
          continue;
      }

      if (auto *BD = R.getAsSingle<BindingDecl>())
        Var = BD;
      else if (R.getAsSingle<FieldDecl>()) {
        Diag(C->Loc, diag::err_capture_class_member_does_not_name_variable)
            << C->Id;
        continue;
      } else
        Var = R.getAsSingle<VarDecl>();
      if (Var && DiagnoseUseOfDecl(Var, C->Loc))
        continue;
    }

    // C++11 [expr.prim.lambda]p10:
    //   [...] each such lookup shall find a variable with automatic storage
    //   duration declared in the reaching scope of the local lambda expression.
    // Note that the 'reaching scope' check happens in tryCaptureVariable().
    if (!Var) {
      Diag(C->Loc, diag::err_capture_does_not_name_variable) << C->Id;
      continue;
    }

    // C++11 [expr.prim.lambda]p8:
    //   An identifier or this shall not appear more than once in a
    //   lambda-capture.
    if (auto [It, Inserted] = CaptureNames.insert(std::pair{C->Id, Var});
        !Inserted) {
      if (C->InitKind == LambdaCaptureInitKind::NoInit &&
          !Var->isInitCapture()) {
        Diag(C->Loc, diag::err_capture_more_than_once)
            << C->Id << It->second->getBeginLoc()
            << FixItHint::CreateRemoval(
                   SourceRange(getLocForEndOfToken(PrevCaptureLoc), C->Loc));
        Var->setInvalidDecl();
      } else if (Var && Var->isPlaceholderVar(getLangOpts())) {
        DiagPlaceholderVariableDefinition(C->Loc);
      } else {
        // Previous capture captured something different (one or both was
        // an init-capture): no fixit.
        Diag(C->Loc, diag::err_capture_more_than_once) << C->Id;
        continue;
      }
    }

    // Ignore invalid decls; they'll just confuse the code later.
    if (Var->isInvalidDecl())
      continue;

    VarDecl *Underlying = Var->getPotentiallyDecomposedVarDecl();

    if (!Underlying->hasLocalStorage()) {
      Diag(C->Loc, diag::err_capture_non_automatic_variable) << C->Id;
      Diag(Var->getLocation(), diag::note_previous_decl) << C->Id;
      continue;
    }

    // C++11 [expr.prim.lambda]p23:
    //   A capture followed by an ellipsis is a pack expansion (14.5.3).
    SourceLocation EllipsisLoc;
    if (C->EllipsisLoc.isValid()) {
      if (Var->isParameterPack()) {
        EllipsisLoc = C->EllipsisLoc;
      } else {
        Diag(C->EllipsisLoc, diag::err_pack_expansion_without_parameter_packs)
            << (C->Init.isUsable() ? C->Init.get()->getSourceRange()
                                   : SourceRange(C->Loc));

        // Just ignore the ellipsis.
      }
    } else if (Var->isParameterPack()) {
      ContainsUnexpandedParameterPack = true;
    }

    if (C->Init.isUsable()) {
      addInitCapture(LSI, cast<VarDecl>(Var), C->Kind == LCK_ByRef);
    } else {
      TryCaptureKind Kind = C->Kind == LCK_ByRef ? TryCapture_ExplicitByRef
                                                 : TryCapture_ExplicitByVal;
      tryCaptureVariable(Var, C->Loc, Kind, EllipsisLoc);
    }
    if (!LSI->Captures.empty())
      LSI->ExplicitCaptureRanges[LSI->Captures.size() - 1] = C->ExplicitRange;
  }
  finishLambdaExplicitCaptures(LSI);
  LSI->ContainsUnexpandedParameterPack |= ContainsUnexpandedParameterPack;
  PopDeclContext();
}

void Sema::ActOnLambdaClosureQualifiers(LambdaIntroducer &Intro,
                                        SourceLocation MutableLoc) {

  LambdaScopeInfo *LSI = getCurrentLambdaScopeUnsafe(*this);
  LSI->Mutable = MutableLoc.isValid();
  ContextRAII Context(*this, LSI->CallOperator, /*NewThisContext*/ false);

  // C++11 [expr.prim.lambda]p9:
  //   A lambda-expression whose smallest enclosing scope is a block scope is a
  //   local lambda expression; any other lambda expression shall not have a
  //   capture-default or simple-capture in its lambda-introducer.
  //
  // For simple-captures, this is covered by the check below that any named
  // entity is a variable that can be captured.
  //
  // For DR1632, we also allow a capture-default in any context where we can
  // odr-use 'this' (in particular, in a default initializer for a non-static
  // data member).
  if (Intro.Default != LCD_None &&
      !LSI->Lambda->getParent()->isFunctionOrMethod() &&
      (getCurrentThisType().isNull() ||
       CheckCXXThisCapture(SourceLocation(), /*Explicit=*/true,
                           /*BuildAndDiagnose=*/false)))
    Diag(Intro.DefaultLoc, diag::err_capture_default_non_local);
}

void Sema::ActOnLambdaClosureParameters(
    Scope *LambdaScope, MutableArrayRef<DeclaratorChunk::ParamInfo> Params) {
  LambdaScopeInfo *LSI = getCurrentLambdaScopeUnsafe(*this);
  PushDeclContext(LambdaScope, LSI->CallOperator);

  for (const DeclaratorChunk::ParamInfo &P : Params) {
    auto *Param = cast<ParmVarDecl>(P.Param);
    Param->setOwningFunction(LSI->CallOperator);
    if (Param->getIdentifier())
      PushOnScopeChains(Param, LambdaScope, false);
  }

  // After the parameter list, we may parse a noexcept/requires/trailing return
  // type which need to know whether the call operator constiture a dependent
  // context, so we need to setup the FunctionTemplateDecl of generic lambdas
  // now.
  TemplateParameterList *TemplateParams =
      getGenericLambdaTemplateParameterList(LSI, *this);
  if (TemplateParams) {
    AddTemplateParametersToLambdaCallOperator(LSI->CallOperator, LSI->Lambda,
                                              TemplateParams);
    LSI->Lambda->setLambdaIsGeneric(true);
    LSI->ContainsUnexpandedParameterPack |=
        TemplateParams->containsUnexpandedParameterPack();
  }
  LSI->AfterParameterList = true;
}

void Sema::ActOnStartOfLambdaDefinition(LambdaIntroducer &Intro,
                                        Declarator &ParamInfo,
                                        const DeclSpec &DS) {

  LambdaScopeInfo *LSI = getCurrentLambdaScopeUnsafe(*this);
  LSI->CallOperator->setConstexprKind(DS.getConstexprSpecifier());

  SmallVector<ParmVarDecl *, 8> Params;
  bool ExplicitResultType;

  SourceLocation TypeLoc, CallOperatorLoc;
  if (ParamInfo.getNumTypeObjects() == 0) {
    CallOperatorLoc = TypeLoc = Intro.Range.getEnd();
  } else {
    unsigned Index;
    ParamInfo.isFunctionDeclarator(Index);
    const auto &Object = ParamInfo.getTypeObject(Index);
    TypeLoc =
        Object.Loc.isValid() ? Object.Loc : ParamInfo.getSourceRange().getEnd();
    CallOperatorLoc = ParamInfo.getSourceRange().getEnd();
  }

  CXXRecordDecl *Class = LSI->Lambda;
  CXXMethodDecl *Method = LSI->CallOperator;

  TypeSourceInfo *MethodTyInfo = getLambdaType(
      *this, Intro, ParamInfo, getCurScope(), TypeLoc, ExplicitResultType);

  LSI->ExplicitParams = ParamInfo.getNumTypeObjects() != 0;

  if (ParamInfo.isFunctionDeclarator() != 0 &&
      !FTIHasSingleVoidParameter(ParamInfo.getFunctionTypeInfo())) {
    const auto &FTI = ParamInfo.getFunctionTypeInfo();
    Params.reserve(Params.size());
    for (unsigned I = 0; I < FTI.NumParams; ++I) {
      auto *Param = cast<ParmVarDecl>(FTI.Params[I].Param);
      Param->setScopeInfo(0, Params.size());
      Params.push_back(Param);
    }
  }

  bool IsLambdaStatic =
      ParamInfo.getDeclSpec().getStorageClassSpec() == DeclSpec::SCS_static;

  CompleteLambdaCallOperator(
      Method, Intro.Range.getBegin(), CallOperatorLoc,
      ParamInfo.getTrailingRequiresClause(), MethodTyInfo,
      ParamInfo.getDeclSpec().getConstexprSpecifier(),
      IsLambdaStatic ? SC_Static : SC_None, Params, ExplicitResultType);

  CheckCXXDefaultArguments(Method);

  // This represents the function body for the lambda function, check if we
  // have to apply optnone due to a pragma.
  AddRangeBasedOptnone(Method);

  // code_seg attribute on lambda apply to the method.
  if (Attr *A = getImplicitCodeSegOrSectionAttrForFunction(
          Method, /*IsDefinition=*/true))
    Method->addAttr(A);

  // Attributes on the lambda apply to the method.
  ProcessDeclAttributes(CurScope, Method, ParamInfo);

  // CUDA lambdas get implicit host and device attributes.
  if (getLangOpts().CUDA)
    CUDA().SetLambdaAttrs(Method);

  // OpenMP lambdas might get assumumption attributes.
  if (LangOpts.OpenMP)
    OpenMP().ActOnFinishedFunctionDefinitionInOpenMPAssumeScope(Method);

  handleLambdaNumbering(Class, Method);

  for (auto &&C : LSI->Captures) {
    if (!C.isVariableCapture())
      continue;
    ValueDecl *Var = C.getVariable();
    if (Var && Var->isInitCapture()) {
      PushOnScopeChains(Var, CurScope, false);
    }
  }

  auto CheckRedefinition = [&](ParmVarDecl *Param) {
    for (const auto &Capture : Intro.Captures) {
      if (Capture.Id == Param->getIdentifier()) {
        Diag(Param->getLocation(), diag::err_parameter_shadow_capture);
        Diag(Capture.Loc, diag::note_var_explicitly_captured_here)
            << Capture.Id << true;
        return false;
      }
    }
    return true;
  };

  for (ParmVarDecl *P : Params) {
    if (!P->getIdentifier())
      continue;
    if (CheckRedefinition(P))
      CheckShadow(CurScope, P);
    PushOnScopeChains(P, CurScope);
  }

  // C++23 [expr.prim.lambda.capture]p5:
  // If an identifier in a capture appears as the declarator-id of a parameter
  // of the lambda-declarator's parameter-declaration-clause or as the name of a
  // template parameter of the lambda-expression's template-parameter-list, the
  // program is ill-formed.
  TemplateParameterList *TemplateParams =
      getGenericLambdaTemplateParameterList(LSI, *this);
  if (TemplateParams) {
    for (const auto *TP : TemplateParams->asArray()) {
      if (!TP->getIdentifier())
        continue;
      for (const auto &Capture : Intro.Captures) {
        if (Capture.Id == TP->getIdentifier()) {
          Diag(Capture.Loc, diag::err_template_param_shadow) << Capture.Id;
          NoteTemplateParameterLocation(*TP);
        }
      }
    }
  }

  // C++20: dcl.decl.general p4:
  // The optional requires-clause ([temp.pre]) in an init-declarator or
  // member-declarator shall be present only if the declarator declares a
  // templated function ([dcl.fct]).
  if (Expr *TRC = Method->getTrailingRequiresClause()) {
    // [temp.pre]/8:
    // An entity is templated if it is
    // - a template,
    // - an entity defined ([basic.def]) or created ([class.temporary]) in a
    // templated entity,
    // - a member of a templated entity,
    // - an enumerator for an enumeration that is a templated entity, or
    // - the closure type of a lambda-expression ([expr.prim.lambda.closure])
    // appearing in the declaration of a templated entity. [Note 6: A local
    // class, a local or block variable, or a friend function defined in a
    // templated entity is a templated entity.  — end note]
    //
    // A templated function is a function template or a function that is
    // templated. A templated class is a class template or a class that is
    // templated. A templated variable is a variable template or a variable
    // that is templated.

    // Note: we only have to check if this is defined in a template entity, OR
    // if we are a template, since the rest don't apply. The requires clause
    // applies to the call operator, which we already know is a member function,
    // AND defined.
    if (!Method->getDescribedFunctionTemplate() && !Method->isTemplated()) {
      Diag(TRC->getBeginLoc(), diag::err_constrained_non_templated_function);
    }
  }

  // Enter a new evaluation context to insulate the lambda from any
  // cleanups from the enclosing full-expression.
  PushExpressionEvaluationContext(
      LSI->CallOperator->isConsteval()
          ? ExpressionEvaluationContext::ImmediateFunctionContext
          : ExpressionEvaluationContext::PotentiallyEvaluated);
  ExprEvalContexts.back().InImmediateFunctionContext =
      LSI->CallOperator->isConsteval();
  ExprEvalContexts.back().InImmediateEscalatingFunctionContext =
      getLangOpts().CPlusPlus20 && LSI->CallOperator->isImmediateEscalating();
}

void Sema::ActOnLambdaError(SourceLocation StartLoc, Scope *CurScope,
                            bool IsInstantiation) {
  LambdaScopeInfo *LSI = cast<LambdaScopeInfo>(FunctionScopes.back());

  // Leave the expression-evaluation context.
  DiscardCleanupsInEvaluationContext();
  PopExpressionEvaluationContext();

  // Leave the context of the lambda.
  if (!IsInstantiation)
    PopDeclContext();

  // Finalize the lambda.
  CXXRecordDecl *Class = LSI->Lambda;
  Class->setInvalidDecl();
  SmallVector<Decl*, 4> Fields(Class->fields());
  ActOnFields(nullptr, Class->getLocation(), Class, Fields, SourceLocation(),
              SourceLocation(), ParsedAttributesView());
  CheckCompletedCXXClass(nullptr, Class);

  PopFunctionScopeInfo();
}

template <typename Func>
static void repeatForLambdaConversionFunctionCallingConvs(
    Sema &S, const FunctionProtoType &CallOpProto, Func F) {
  CallingConv DefaultFree = S.Context.getDefaultCallingConvention(
      CallOpProto.isVariadic(), /*IsCXXMethod=*/false);
  CallingConv DefaultMember = S.Context.getDefaultCallingConvention(
      CallOpProto.isVariadic(), /*IsCXXMethod=*/true);
  CallingConv CallOpCC = CallOpProto.getCallConv();

  /// Implement emitting a version of the operator for many of the calling
  /// conventions for MSVC, as described here:
  /// https://devblogs.microsoft.com/oldnewthing/20150220-00/?p=44623.
  /// Experimentally, we determined that cdecl, stdcall, fastcall, and
  /// vectorcall are generated by MSVC when it is supported by the target.
  /// Additionally, we are ensuring that the default-free/default-member and
  /// call-operator calling convention are generated as well.
  /// NOTE: We intentionally generate a 'thiscall' on Win32 implicitly from the
  /// 'member default', despite MSVC not doing so. We do this in order to ensure
  /// that someone who intentionally places 'thiscall' on the lambda call
  /// operator will still get that overload, since we don't have the a way of
  /// detecting the attribute by the time we get here.
  if (S.getLangOpts().MSVCCompat) {
    CallingConv Convs[] = {
        CC_C,        CC_X86StdCall, CC_X86FastCall, CC_X86VectorCall,
        DefaultFree, DefaultMember, CallOpCC};
    llvm::sort(Convs);
    llvm::iterator_range<CallingConv *> Range(
        std::begin(Convs), std::unique(std::begin(Convs), std::end(Convs)));
    const TargetInfo &TI = S.getASTContext().getTargetInfo();

    for (CallingConv C : Range) {
      if (TI.checkCallingConvention(C) == TargetInfo::CCCR_OK)
        F(C);
    }
    return;
  }

  if (CallOpCC == DefaultMember && DefaultMember != DefaultFree) {
    F(DefaultFree);
    F(DefaultMember);
  } else {
    F(CallOpCC);
  }
}

// Returns the 'standard' calling convention to be used for the lambda
// conversion function, that is, the 'free' function calling convention unless
// it is overridden by a non-default calling convention attribute.
static CallingConv
getLambdaConversionFunctionCallConv(Sema &S,
                                    const FunctionProtoType *CallOpProto) {
  CallingConv DefaultFree = S.Context.getDefaultCallingConvention(
      CallOpProto->isVariadic(), /*IsCXXMethod=*/false);
  CallingConv DefaultMember = S.Context.getDefaultCallingConvention(
      CallOpProto->isVariadic(), /*IsCXXMethod=*/true);
  CallingConv CallOpCC = CallOpProto->getCallConv();

  // If the call-operator hasn't been changed, return both the 'free' and
  // 'member' function calling convention.
  if (CallOpCC == DefaultMember && DefaultMember != DefaultFree)
    return DefaultFree;
  return CallOpCC;
}

QualType Sema::getLambdaConversionFunctionResultType(
    const FunctionProtoType *CallOpProto, CallingConv CC) {
  const FunctionProtoType::ExtProtoInfo CallOpExtInfo =
      CallOpProto->getExtProtoInfo();
  FunctionProtoType::ExtProtoInfo InvokerExtInfo = CallOpExtInfo;
  InvokerExtInfo.ExtInfo = InvokerExtInfo.ExtInfo.withCallingConv(CC);
  InvokerExtInfo.TypeQuals = Qualifiers();
  assert(InvokerExtInfo.RefQualifier == RQ_None &&
         "Lambda's call operator should not have a reference qualifier");
  return Context.getFunctionType(CallOpProto->getReturnType(),
                                 CallOpProto->getParamTypes(), InvokerExtInfo);
}

/// Add a lambda's conversion to function pointer, as described in
/// C++11 [expr.prim.lambda]p6.
static void addFunctionPointerConversion(Sema &S, SourceRange IntroducerRange,
                                         CXXRecordDecl *Class,
                                         CXXMethodDecl *CallOperator,
                                         QualType InvokerFunctionTy) {
  // This conversion is explicitly disabled if the lambda's function has
  // pass_object_size attributes on any of its parameters.
  auto HasPassObjectSizeAttr = [](const ParmVarDecl *P) {
    return P->hasAttr<PassObjectSizeAttr>();
  };
  if (llvm::any_of(CallOperator->parameters(), HasPassObjectSizeAttr))
    return;

  // Add the conversion to function pointer.
  QualType PtrToFunctionTy = S.Context.getPointerType(InvokerFunctionTy);

  // Create the type of the conversion function.
  FunctionProtoType::ExtProtoInfo ConvExtInfo(
      S.Context.getDefaultCallingConvention(
      /*IsVariadic=*/false, /*IsCXXMethod=*/true));
  // The conversion function is always const and noexcept.
  ConvExtInfo.TypeQuals = Qualifiers();
  ConvExtInfo.TypeQuals.addConst();
  ConvExtInfo.ExceptionSpec.Type = EST_BasicNoexcept;
  QualType ConvTy =
      S.Context.getFunctionType(PtrToFunctionTy, std::nullopt, ConvExtInfo);

  SourceLocation Loc = IntroducerRange.getBegin();
  DeclarationName ConversionName
    = S.Context.DeclarationNames.getCXXConversionFunctionName(
        S.Context.getCanonicalType(PtrToFunctionTy));
  // Construct a TypeSourceInfo for the conversion function, and wire
  // all the parameters appropriately for the FunctionProtoTypeLoc
  // so that everything works during transformation/instantiation of
  // generic lambdas.
  // The main reason for wiring up the parameters of the conversion
  // function with that of the call operator is so that constructs
  // like the following work:
  // auto L = [](auto b) {                <-- 1
  //   return [](auto a) -> decltype(a) { <-- 2
  //      return a;
  //   };
  // };
  // int (*fp)(int) = L(5);
  // Because the trailing return type can contain DeclRefExprs that refer
  // to the original call operator's variables, we hijack the call
  // operators ParmVarDecls below.
  TypeSourceInfo *ConvNamePtrToFunctionTSI =
      S.Context.getTrivialTypeSourceInfo(PtrToFunctionTy, Loc);
  DeclarationNameLoc ConvNameLoc =
      DeclarationNameLoc::makeNamedTypeLoc(ConvNamePtrToFunctionTSI);

  // The conversion function is a conversion to a pointer-to-function.
  TypeSourceInfo *ConvTSI = S.Context.getTrivialTypeSourceInfo(ConvTy, Loc);
  FunctionProtoTypeLoc ConvTL =
      ConvTSI->getTypeLoc().getAs<FunctionProtoTypeLoc>();
  // Get the result of the conversion function which is a pointer-to-function.
  PointerTypeLoc PtrToFunctionTL =
      ConvTL.getReturnLoc().getAs<PointerTypeLoc>();
  // Do the same for the TypeSourceInfo that is used to name the conversion
  // operator.
  PointerTypeLoc ConvNamePtrToFunctionTL =
      ConvNamePtrToFunctionTSI->getTypeLoc().getAs<PointerTypeLoc>();

  // Get the underlying function types that the conversion function will
  // be converting to (should match the type of the call operator).
  FunctionProtoTypeLoc CallOpConvTL =
      PtrToFunctionTL.getPointeeLoc().getAs<FunctionProtoTypeLoc>();
  FunctionProtoTypeLoc CallOpConvNameTL =
    ConvNamePtrToFunctionTL.getPointeeLoc().getAs<FunctionProtoTypeLoc>();

  // Wire up the FunctionProtoTypeLocs with the call operator's parameters.
  // These parameter's are essentially used to transform the name and
  // the type of the conversion operator.  By using the same parameters
  // as the call operator's we don't have to fix any back references that
  // the trailing return type of the call operator's uses (such as
  // decltype(some_type<decltype(a)>::type{} + decltype(a){}) etc.)
  // - we can simply use the return type of the call operator, and
  // everything should work.
  SmallVector<ParmVarDecl *, 4> InvokerParams;
  for (unsigned I = 0, N = CallOperator->getNumParams(); I != N; ++I) {
    ParmVarDecl *From = CallOperator->getParamDecl(I);

    InvokerParams.push_back(ParmVarDecl::Create(
        S.Context,
        // Temporarily add to the TU. This is set to the invoker below.
        S.Context.getTranslationUnitDecl(), From->getBeginLoc(),
        From->getLocation(), From->getIdentifier(), From->getType(),
        From->getTypeSourceInfo(), From->getStorageClass(),
        /*DefArg=*/nullptr));
    CallOpConvTL.setParam(I, From);
    CallOpConvNameTL.setParam(I, From);
  }

  CXXConversionDecl *Conversion = CXXConversionDecl::Create(
      S.Context, Class, Loc,
      DeclarationNameInfo(ConversionName, Loc, ConvNameLoc), ConvTy, ConvTSI,
      S.getCurFPFeatures().isFPConstrained(),
      /*isInline=*/true, ExplicitSpecifier(),
      S.getLangOpts().CPlusPlus17 ? ConstexprSpecKind::Constexpr
                                  : ConstexprSpecKind::Unspecified,
      CallOperator->getBody()->getEndLoc());
  Conversion->setAccess(AS_public);
  Conversion->setImplicit(true);

  // A non-generic lambda may still be a templated entity. We need to preserve
  // constraints when converting the lambda to a function pointer. See GH63181.
  if (Expr *Requires = CallOperator->getTrailingRequiresClause())
    Conversion->setTrailingRequiresClause(Requires);

  if (Class->isGenericLambda()) {
    // Create a template version of the conversion operator, using the template
    // parameter list of the function call operator.
    FunctionTemplateDecl *TemplateCallOperator =
            CallOperator->getDescribedFunctionTemplate();
    FunctionTemplateDecl *ConversionTemplate =
                  FunctionTemplateDecl::Create(S.Context, Class,
                                      Loc, ConversionName,
                                      TemplateCallOperator->getTemplateParameters(),
                                      Conversion);
    ConversionTemplate->setAccess(AS_public);
    ConversionTemplate->setImplicit(true);
    Conversion->setDescribedFunctionTemplate(ConversionTemplate);
    Class->addDecl(ConversionTemplate);
  } else
    Class->addDecl(Conversion);

  // If the lambda is not static, we need to add a static member
  // function that will be the result of the conversion with a
  // certain unique ID.
  // When it is static we just return the static call operator instead.
  if (CallOperator->isImplicitObjectMemberFunction()) {
    DeclarationName InvokerName =
        &S.Context.Idents.get(getLambdaStaticInvokerName());
    // FIXME: Instead of passing in the CallOperator->getTypeSourceInfo()
    // we should get a prebuilt TrivialTypeSourceInfo from Context
    // using FunctionTy & Loc and get its TypeLoc as a FunctionProtoTypeLoc
    // then rewire the parameters accordingly, by hoisting up the InvokeParams
    // loop below and then use its Params to set Invoke->setParams(...) below.
    // This would avoid the 'const' qualifier of the calloperator from
    // contaminating the type of the invoker, which is currently adjusted
    // in SemaTemplateDeduction.cpp:DeduceTemplateArguments.  Fixing the
    // trailing return type of the invoker would require a visitor to rebuild
    // the trailing return type and adjusting all back DeclRefExpr's to refer
    // to the new static invoker parameters - not the call operator's.
    CXXMethodDecl *Invoke = CXXMethodDecl::Create(
        S.Context, Class, Loc, DeclarationNameInfo(InvokerName, Loc),
        InvokerFunctionTy, CallOperator->getTypeSourceInfo(), SC_Static,
        S.getCurFPFeatures().isFPConstrained(),
        /*isInline=*/true, CallOperator->getConstexprKind(),
        CallOperator->getBody()->getEndLoc());
    for (unsigned I = 0, N = CallOperator->getNumParams(); I != N; ++I)
      InvokerParams[I]->setOwningFunction(Invoke);
    Invoke->setParams(InvokerParams);
    Invoke->setAccess(AS_private);
    Invoke->setImplicit(true);
    if (Class->isGenericLambda()) {
      FunctionTemplateDecl *TemplateCallOperator =
          CallOperator->getDescribedFunctionTemplate();
      FunctionTemplateDecl *StaticInvokerTemplate =
          FunctionTemplateDecl::Create(
              S.Context, Class, Loc, InvokerName,
              TemplateCallOperator->getTemplateParameters(), Invoke);
      StaticInvokerTemplate->setAccess(AS_private);
      StaticInvokerTemplate->setImplicit(true);
      Invoke->setDescribedFunctionTemplate(StaticInvokerTemplate);
      Class->addDecl(StaticInvokerTemplate);
    } else
      Class->addDecl(Invoke);
  }
}

/// Add a lambda's conversion to function pointers, as described in
/// C++11 [expr.prim.lambda]p6. Note that in most cases, this should emit only a
/// single pointer conversion. In the event that the default calling convention
/// for free and member functions is different, it will emit both conventions.
static void addFunctionPointerConversions(Sema &S, SourceRange IntroducerRange,
                                          CXXRecordDecl *Class,
                                          CXXMethodDecl *CallOperator) {
  const FunctionProtoType *CallOpProto =
      CallOperator->getType()->castAs<FunctionProtoType>();

  repeatForLambdaConversionFunctionCallingConvs(
      S, *CallOpProto, [&](CallingConv CC) {
        QualType InvokerFunctionTy =
            S.getLambdaConversionFunctionResultType(CallOpProto, CC);
        addFunctionPointerConversion(S, IntroducerRange, Class, CallOperator,
                                     InvokerFunctionTy);
      });
}

/// Add a lambda's conversion to block pointer.
static void addBlockPointerConversion(Sema &S,
                                      SourceRange IntroducerRange,
                                      CXXRecordDecl *Class,
                                      CXXMethodDecl *CallOperator) {
  const FunctionProtoType *CallOpProto =
      CallOperator->getType()->castAs<FunctionProtoType>();
  QualType FunctionTy = S.getLambdaConversionFunctionResultType(
      CallOpProto, getLambdaConversionFunctionCallConv(S, CallOpProto));
  QualType BlockPtrTy = S.Context.getBlockPointerType(FunctionTy);

  FunctionProtoType::ExtProtoInfo ConversionEPI(
      S.Context.getDefaultCallingConvention(
          /*IsVariadic=*/false, /*IsCXXMethod=*/true));
  ConversionEPI.TypeQuals = Qualifiers();
  ConversionEPI.TypeQuals.addConst();
  QualType ConvTy =
      S.Context.getFunctionType(BlockPtrTy, std::nullopt, ConversionEPI);

  SourceLocation Loc = IntroducerRange.getBegin();
  DeclarationName Name
    = S.Context.DeclarationNames.getCXXConversionFunctionName(
        S.Context.getCanonicalType(BlockPtrTy));
  DeclarationNameLoc NameLoc = DeclarationNameLoc::makeNamedTypeLoc(
      S.Context.getTrivialTypeSourceInfo(BlockPtrTy, Loc));
  CXXConversionDecl *Conversion = CXXConversionDecl::Create(
      S.Context, Class, Loc, DeclarationNameInfo(Name, Loc, NameLoc), ConvTy,
      S.Context.getTrivialTypeSourceInfo(ConvTy, Loc),
      S.getCurFPFeatures().isFPConstrained(),
      /*isInline=*/true, ExplicitSpecifier(), ConstexprSpecKind::Unspecified,
      CallOperator->getBody()->getEndLoc());
  Conversion->setAccess(AS_public);
  Conversion->setImplicit(true);
  Class->addDecl(Conversion);
}

ExprResult Sema::BuildCaptureInit(const Capture &Cap,
                                  SourceLocation ImplicitCaptureLoc,
                                  bool IsOpenMPMapping) {
  // VLA captures don't have a stored initialization expression.
  if (Cap.isVLATypeCapture())
    return ExprResult();

  // An init-capture is initialized directly from its stored initializer.
  if (Cap.isInitCapture())
    return cast<VarDecl>(Cap.getVariable())->getInit();

  // For anything else, build an initialization expression. For an implicit
  // capture, the capture notionally happens at the capture-default, so use
  // that location here.
  SourceLocation Loc =
      ImplicitCaptureLoc.isValid() ? ImplicitCaptureLoc : Cap.getLocation();

  // C++11 [expr.prim.lambda]p21:
  //   When the lambda-expression is evaluated, the entities that
  //   are captured by copy are used to direct-initialize each
  //   corresponding non-static data member of the resulting closure
  //   object. (For array members, the array elements are
  //   direct-initialized in increasing subscript order.) These
  //   initializations are performed in the (unspecified) order in
  //   which the non-static data members are declared.

  // C++ [expr.prim.lambda]p12:
  //   An entity captured by a lambda-expression is odr-used (3.2) in
  //   the scope containing the lambda-expression.
  ExprResult Init;
  IdentifierInfo *Name = nullptr;
  if (Cap.isThisCapture()) {
    QualType ThisTy = getCurrentThisType();
    Expr *This = BuildCXXThisExpr(Loc, ThisTy, ImplicitCaptureLoc.isValid());
    if (Cap.isCopyCapture())
      Init = CreateBuiltinUnaryOp(Loc, UO_Deref, This);
    else
      Init = This;
  } else {
    assert(Cap.isVariableCapture() && "unknown kind of capture");
    ValueDecl *Var = Cap.getVariable();
    Name = Var->getIdentifier();
    Init = BuildDeclarationNameExpr(
      CXXScopeSpec(), DeclarationNameInfo(Var->getDeclName(), Loc), Var);
  }

  // In OpenMP, the capture kind doesn't actually describe how to capture:
  // variables are "mapped" onto the device in a process that does not formally
  // make a copy, even for a "copy capture".
  if (IsOpenMPMapping)
    return Init;

  if (Init.isInvalid())
    return ExprError();

  Expr *InitExpr = Init.get();
  InitializedEntity Entity = InitializedEntity::InitializeLambdaCapture(
      Name, Cap.getCaptureType(), Loc);
  InitializationKind InitKind =
      InitializationKind::CreateDirect(Loc, Loc, Loc);
  InitializationSequence InitSeq(*this, Entity, InitKind, InitExpr);
  return InitSeq.Perform(*this, Entity, InitKind, InitExpr);
}

ExprResult Sema::ActOnLambdaExpr(SourceLocation StartLoc, Stmt *Body) {
  LambdaScopeInfo LSI = *cast<LambdaScopeInfo>(FunctionScopes.back());
  ActOnFinishFunctionBody(LSI.CallOperator, Body);
  return BuildLambdaExpr(StartLoc, Body->getEndLoc(), &LSI);
}

static LambdaCaptureDefault
mapImplicitCaptureStyle(CapturingScopeInfo::ImplicitCaptureStyle ICS) {
  switch (ICS) {
  case CapturingScopeInfo::ImpCap_None:
    return LCD_None;
  case CapturingScopeInfo::ImpCap_LambdaByval:
    return LCD_ByCopy;
  case CapturingScopeInfo::ImpCap_CapturedRegion:
  case CapturingScopeInfo::ImpCap_LambdaByref:
    return LCD_ByRef;
  case CapturingScopeInfo::ImpCap_Block:
    llvm_unreachable("block capture in lambda");
  }
  llvm_unreachable("Unknown implicit capture style");
}

bool Sema::CaptureHasSideEffects(const Capture &From) {
  if (From.isInitCapture()) {
    Expr *Init = cast<VarDecl>(From.getVariable())->getInit();
    if (Init && Init->HasSideEffects(Context))
      return true;
  }

  if (!From.isCopyCapture())
    return false;

  const QualType T = From.isThisCapture()
                         ? getCurrentThisType()->getPointeeType()
                         : From.getCaptureType();

  if (T.isVolatileQualified())
    return true;

  const Type *BaseT = T->getBaseElementTypeUnsafe();
  if (const CXXRecordDecl *RD = BaseT->getAsCXXRecordDecl())
    return !RD->isCompleteDefinition() || !RD->hasTrivialCopyConstructor() ||
           !RD->hasTrivialDestructor();

  return false;
}

bool Sema::DiagnoseUnusedLambdaCapture(SourceRange CaptureRange,
                                       const Capture &From) {
  if (CaptureHasSideEffects(From))
    return false;

  if (From.isVLATypeCapture())
    return false;

  // FIXME: maybe we should warn on these if we can find a sensible diagnostic
  // message
  if (From.isInitCapture() &&
      From.getVariable()->isPlaceholderVar(getLangOpts()))
    return false;

  auto diag = Diag(From.getLocation(), diag::warn_unused_lambda_capture);
  if (From.isThisCapture())
    diag << "'this'";
  else
    diag << From.getVariable();
  diag << From.isNonODRUsed();
  diag << FixItHint::CreateRemoval(CaptureRange);
  return true;
}

/// Create a field within the lambda class or captured statement record for the
/// given capture.
FieldDecl *Sema::BuildCaptureField(RecordDecl *RD,
                                   const sema::Capture &Capture) {
  SourceLocation Loc = Capture.getLocation();
  QualType FieldType = Capture.getCaptureType();

  TypeSourceInfo *TSI = nullptr;
  if (Capture.isVariableCapture()) {
    const auto *Var = dyn_cast_or_null<VarDecl>(Capture.getVariable());
    if (Var && Var->isInitCapture())
      TSI = Var->getTypeSourceInfo();
  }

  // FIXME: Should we really be doing this? A null TypeSourceInfo seems more
  // appropriate, at least for an implicit capture.
  if (!TSI)
    TSI = Context.getTrivialTypeSourceInfo(FieldType, Loc);

  // Build the non-static data member.
  FieldDecl *Field =
      FieldDecl::Create(Context, RD, /*StartLoc=*/Loc, /*IdLoc=*/Loc,
                        /*Id=*/nullptr, FieldType, TSI, /*BW=*/nullptr,
                        /*Mutable=*/false, ICIS_NoInit);
  // If the variable being captured has an invalid type, mark the class as
  // invalid as well.
  if (!FieldType->isDependentType()) {
    if (RequireCompleteSizedType(Loc, FieldType,
                                 diag::err_field_incomplete_or_sizeless)) {
      RD->setInvalidDecl();
      Field->setInvalidDecl();
    } else {
      NamedDecl *Def;
      FieldType->isIncompleteType(&Def);
      if (Def && Def->isInvalidDecl()) {
        RD->setInvalidDecl();
        Field->setInvalidDecl();
      }
    }
  }
  Field->setImplicit(true);
  Field->setAccess(AS_private);
  RD->addDecl(Field);

  if (Capture.isVLATypeCapture())
    Field->setCapturedVLAType(Capture.getCapturedVLAType());

  return Field;
}

ExprResult Sema::BuildLambdaExpr(SourceLocation StartLoc, SourceLocation EndLoc,
                                 LambdaScopeInfo *LSI) {
  // Collect information from the lambda scope.
  SmallVector<LambdaCapture, 4> Captures;
  SmallVector<Expr *, 4> CaptureInits;
  SourceLocation CaptureDefaultLoc = LSI->CaptureDefaultLoc;
  LambdaCaptureDefault CaptureDefault =
      mapImplicitCaptureStyle(LSI->ImpCaptureStyle);
  CXXRecordDecl *Class;
  CXXMethodDecl *CallOperator;
  SourceRange IntroducerRange;
  bool ExplicitParams;
  bool ExplicitResultType;
  CleanupInfo LambdaCleanup;
  bool ContainsUnexpandedParameterPack;
  bool IsGenericLambda;
  {
    CallOperator = LSI->CallOperator;
    Class = LSI->Lambda;
    IntroducerRange = LSI->IntroducerRange;
    ExplicitParams = LSI->ExplicitParams;
    ExplicitResultType = !LSI->HasImplicitReturnType;
    LambdaCleanup = LSI->Cleanup;
    ContainsUnexpandedParameterPack = LSI->ContainsUnexpandedParameterPack;
    IsGenericLambda = Class->isGenericLambda();

    CallOperator->setLexicalDeclContext(Class);
    Decl *TemplateOrNonTemplateCallOperatorDecl =
        CallOperator->getDescribedFunctionTemplate()
        ? CallOperator->getDescribedFunctionTemplate()
        : cast<Decl>(CallOperator);

    // FIXME: Is this really the best choice? Keeping the lexical decl context
    // set as CurContext seems more faithful to the source.
    TemplateOrNonTemplateCallOperatorDecl->setLexicalDeclContext(Class);

    PopExpressionEvaluationContext();

    // True if the current capture has a used capture or default before it.
    bool CurHasPreviousCapture = CaptureDefault != LCD_None;
    SourceLocation PrevCaptureLoc = CurHasPreviousCapture ?
        CaptureDefaultLoc : IntroducerRange.getBegin();

    for (unsigned I = 0, N = LSI->Captures.size(); I != N; ++I) {
      const Capture &From = LSI->Captures[I];

      if (From.isInvalid())
        return ExprError();

      assert(!From.isBlockCapture() && "Cannot capture __block variables");
      bool IsImplicit = I >= LSI->NumExplicitCaptures;
      SourceLocation ImplicitCaptureLoc =
          IsImplicit ? CaptureDefaultLoc : SourceLocation();

      // Use source ranges of explicit captures for fixits where available.
      SourceRange CaptureRange = LSI->ExplicitCaptureRanges[I];

      // Warn about unused explicit captures.
      bool IsCaptureUsed = true;
      if (!CurContext->isDependentContext() && !IsImplicit &&
          !From.isODRUsed()) {
        // Initialized captures that are non-ODR used may not be eliminated.
        // FIXME: Where did the IsGenericLambda here come from?
        bool NonODRUsedInitCapture =
            IsGenericLambda && From.isNonODRUsed() && From.isInitCapture();
        if (!NonODRUsedInitCapture) {
          bool IsLast = (I + 1) == LSI->NumExplicitCaptures;
          SourceRange FixItRange;
          if (CaptureRange.isValid()) {
            if (!CurHasPreviousCapture && !IsLast) {
              // If there are no captures preceding this capture, remove the
              // following comma.
              FixItRange = SourceRange(CaptureRange.getBegin(),
                                       getLocForEndOfToken(CaptureRange.getEnd()));
            } else {
              // Otherwise, remove the comma since the last used capture.
              FixItRange = SourceRange(getLocForEndOfToken(PrevCaptureLoc),
                                       CaptureRange.getEnd());
            }
          }

          IsCaptureUsed = !DiagnoseUnusedLambdaCapture(FixItRange, From);
        }
      }

      if (CaptureRange.isValid()) {
        CurHasPreviousCapture |= IsCaptureUsed;
        PrevCaptureLoc = CaptureRange.getEnd();
      }

      // Map the capture to our AST representation.
      LambdaCapture Capture = [&] {
        if (From.isThisCapture()) {
          // Capturing 'this' implicitly with a default of '[=]' is deprecated,
          // because it results in a reference capture. Don't warn prior to
          // C++2a; there's nothing that can be done about it before then.
          if (getLangOpts().CPlusPlus20 && IsImplicit &&
              CaptureDefault == LCD_ByCopy) {
            Diag(From.getLocation(), diag::warn_deprecated_this_capture);
            Diag(CaptureDefaultLoc, diag::note_deprecated_this_capture)
                << FixItHint::CreateInsertion(
                       getLocForEndOfToken(CaptureDefaultLoc), ", this");
          }
          return LambdaCapture(From.getLocation(), IsImplicit,
                               From.isCopyCapture() ? LCK_StarThis : LCK_This);
        } else if (From.isVLATypeCapture()) {
          return LambdaCapture(From.getLocation(), IsImplicit, LCK_VLAType);
        } else {
          assert(From.isVariableCapture() && "unknown kind of capture");
          ValueDecl *Var = From.getVariable();
          LambdaCaptureKind Kind =
              From.isCopyCapture() ? LCK_ByCopy : LCK_ByRef;
          return LambdaCapture(From.getLocation(), IsImplicit, Kind, Var,
                               From.getEllipsisLoc());
        }
      }();

      // Form the initializer for the capture field.
      ExprResult Init = BuildCaptureInit(From, ImplicitCaptureLoc);

      // FIXME: Skip this capture if the capture is not used, the initializer
      // has no side-effects, the type of the capture is trivial, and the
      // lambda is not externally visible.

      // Add a FieldDecl for the capture and form its initializer.
      BuildCaptureField(Class, From);
      Captures.push_back(Capture);
      CaptureInits.push_back(Init.get());

      if (LangOpts.CUDA)
        CUDA().CheckLambdaCapture(CallOperator, From);
    }

    Class->setCaptures(Context, Captures);

    // C++11 [expr.prim.lambda]p6:
    //   The closure type for a lambda-expression with no lambda-capture
    //   has a public non-virtual non-explicit const conversion function
    //   to pointer to function having the same parameter and return
    //   types as the closure type's function call operator.
    if (Captures.empty() && CaptureDefault == LCD_None)
      addFunctionPointerConversions(*this, IntroducerRange, Class,
                                    CallOperator);

    // Objective-C++:
    //   The closure type for a lambda-expression has a public non-virtual
    //   non-explicit const conversion function to a block pointer having the
    //   same parameter and return types as the closure type's function call
    //   operator.
    // FIXME: Fix generic lambda to block conversions.
    if (getLangOpts().Blocks && getLangOpts().ObjC && !IsGenericLambda)
      addBlockPointerConversion(*this, IntroducerRange, Class, CallOperator);

    // Finalize the lambda class.
    SmallVector<Decl*, 4> Fields(Class->fields());
    ActOnFields(nullptr, Class->getLocation(), Class, Fields, SourceLocation(),
                SourceLocation(), ParsedAttributesView());
    CheckCompletedCXXClass(nullptr, Class);
  }

  Cleanup.mergeFrom(LambdaCleanup);

  LambdaExpr *Lambda = LambdaExpr::Create(Context, Class, IntroducerRange,
                                          CaptureDefault, CaptureDefaultLoc,
                                          ExplicitParams, ExplicitResultType,
                                          CaptureInits, EndLoc,
                                          ContainsUnexpandedParameterPack);
  // If the lambda expression's call operator is not explicitly marked constexpr
  // and we are not in a dependent context, analyze the call operator to infer
  // its constexpr-ness, suppressing diagnostics while doing so.
  if (getLangOpts().CPlusPlus17 && !CallOperator->isInvalidDecl() &&
      !CallOperator->isConstexpr() &&
      !isa<CoroutineBodyStmt>(CallOperator->getBody()) &&
      !Class->getDeclContext()->isDependentContext()) {
    CallOperator->setConstexprKind(
        CheckConstexprFunctionDefinition(CallOperator,
                                         CheckConstexprKind::CheckValid)
            ? ConstexprSpecKind::Constexpr
            : ConstexprSpecKind::Unspecified);
  }

  // Emit delayed shadowing warnings now that the full capture list is known.
  DiagnoseShadowingLambdaDecls(LSI);

  if (!CurContext->isDependentContext()) {
    switch (ExprEvalContexts.back().Context) {
    // C++11 [expr.prim.lambda]p2:
    //   A lambda-expression shall not appear in an unevaluated operand
    //   (Clause 5).
    case ExpressionEvaluationContext::Unevaluated:
    case ExpressionEvaluationContext::UnevaluatedList:
    case ExpressionEvaluationContext::UnevaluatedAbstract:
    // C++1y [expr.const]p2:
    //   A conditional-expression e is a core constant expression unless the
    //   evaluation of e, following the rules of the abstract machine, would
    //   evaluate [...] a lambda-expression.
    //
    // This is technically incorrect, there are some constant evaluated contexts
    // where this should be allowed.  We should probably fix this when DR1607 is
    // ratified, it lays out the exact set of conditions where we shouldn't
    // allow a lambda-expression.
    case ExpressionEvaluationContext::ConstantEvaluated:
    case ExpressionEvaluationContext::ImmediateFunctionContext:
      // We don't actually diagnose this case immediately, because we
      // could be within a context where we might find out later that
      // the expression is potentially evaluated (e.g., for typeid).
      ExprEvalContexts.back().Lambdas.push_back(Lambda);
      break;

    case ExpressionEvaluationContext::DiscardedStatement:
    case ExpressionEvaluationContext::PotentiallyEvaluated:
    case ExpressionEvaluationContext::PotentiallyEvaluatedIfUsed:
      break;
    }
  }

  return MaybeBindToTemporary(Lambda);
}

ExprResult Sema::BuildBlockForLambdaConversion(SourceLocation CurrentLocation,
                                               SourceLocation ConvLocation,
                                               CXXConversionDecl *Conv,
                                               Expr *Src) {
  // Make sure that the lambda call operator is marked used.
  CXXRecordDecl *Lambda = Conv->getParent();
  CXXMethodDecl *CallOperator
    = cast<CXXMethodDecl>(
        Lambda->lookup(
          Context.DeclarationNames.getCXXOperatorName(OO_Call)).front());
  CallOperator->setReferenced();
  CallOperator->markUsed(Context);

  ExprResult Init = PerformCopyInitialization(
      InitializedEntity::InitializeLambdaToBlock(ConvLocation, Src->getType()),
      CurrentLocation, Src);
  if (!Init.isInvalid())
    Init = ActOnFinishFullExpr(Init.get(), /*DiscardedValue*/ false);

  if (Init.isInvalid())
    return ExprError();

  // Create the new block to be returned.
  BlockDecl *Block = BlockDecl::Create(Context, CurContext, ConvLocation);

  // Set the type information.
  Block->setSignatureAsWritten(CallOperator->getTypeSourceInfo());
  Block->setIsVariadic(CallOperator->isVariadic());
  Block->setBlockMissingReturnType(false);

  // Add parameters.
  SmallVector<ParmVarDecl *, 4> BlockParams;
  for (unsigned I = 0, N = CallOperator->getNumParams(); I != N; ++I) {
    ParmVarDecl *From = CallOperator->getParamDecl(I);
    BlockParams.push_back(ParmVarDecl::Create(
        Context, Block, From->getBeginLoc(), From->getLocation(),
        From->getIdentifier(), From->getType(), From->getTypeSourceInfo(),
        From->getStorageClass(),
        /*DefArg=*/nullptr));
  }
  Block->setParams(BlockParams);

  Block->setIsConversionFromLambda(true);

  // Add capture. The capture uses a fake variable, which doesn't correspond
  // to any actual memory location. However, the initializer copy-initializes
  // the lambda object.
  TypeSourceInfo *CapVarTSI =
      Context.getTrivialTypeSourceInfo(Src->getType());
  VarDecl *CapVar = VarDecl::Create(Context, Block, ConvLocation,
                                    ConvLocation, nullptr,
                                    Src->getType(), CapVarTSI,
                                    SC_None);
  BlockDecl::Capture Capture(/*variable=*/CapVar, /*byRef=*/false,
                             /*nested=*/false, /*copy=*/Init.get());
  Block->setCaptures(Context, Capture, /*CapturesCXXThis=*/false);

  // Add a fake function body to the block. IR generation is responsible
  // for filling in the actual body, which cannot be expressed as an AST.
  Block->setBody(new (Context) CompoundStmt(ConvLocation));

  // Create the block literal expression.
  Expr *BuildBlock = new (Context) BlockExpr(Block, Conv->getConversionType());
  ExprCleanupObjects.push_back(Block);
  Cleanup.setExprNeedsCleanups(true);

  return BuildBlock;
}

static FunctionDecl *getPatternFunctionDecl(FunctionDecl *FD) {
  if (FD->getTemplatedKind() == FunctionDecl::TK_MemberSpecialization) {
    while (FD->getInstantiatedFromMemberFunction())
      FD = FD->getInstantiatedFromMemberFunction();
    return FD;
  }

  if (FD->getTemplatedKind() == FunctionDecl::TK_DependentNonTemplate)
    return FD->getInstantiatedFromDecl();

  FunctionTemplateDecl *FTD = FD->getPrimaryTemplate();
  if (!FTD)
    return nullptr;

  while (FTD->getInstantiatedFromMemberTemplate())
    FTD = FTD->getInstantiatedFromMemberTemplate();

  return FTD->getTemplatedDecl();
}

Sema::LambdaScopeForCallOperatorInstantiationRAII::
    LambdaScopeForCallOperatorInstantiationRAII(
        Sema &SemaRef, FunctionDecl *FD, MultiLevelTemplateArgumentList MLTAL,
        LocalInstantiationScope &Scope, bool ShouldAddDeclsFromParentScope)
    : FunctionScopeRAII(SemaRef) {
  if (!isLambdaCallOperator(FD)) {
    FunctionScopeRAII::disable();
    return;
  }

  SemaRef.RebuildLambdaScopeInfo(cast<CXXMethodDecl>(FD));

  FunctionDecl *FDPattern = getPatternFunctionDecl(FD);
  if (!FDPattern)
    return;

  SemaRef.addInstantiatedCapturesToScope(FD, FDPattern, Scope, MLTAL);

  if (!ShouldAddDeclsFromParentScope)
    return;

  llvm::SmallVector<std::pair<FunctionDecl *, FunctionDecl *>, 4>
      ParentInstantiations;
  while (true) {
    FDPattern =
        dyn_cast<FunctionDecl>(getLambdaAwareParentOfDeclContext(FDPattern));
    FD = dyn_cast<FunctionDecl>(getLambdaAwareParentOfDeclContext(FD));

    if (!FDPattern || !FD)
      break;

    ParentInstantiations.emplace_back(FDPattern, FD);
  }

  // Add instantiated parameters and local vars to scopes, starting from the
  // outermost lambda to the innermost lambda. This ordering ensures that
  // parameters in inner lambdas can correctly depend on those defined
  // in outer lambdas, e.g. auto L = [](auto... x) {
  //   return [](decltype(x)... y) { }; // `y` depends on `x`
  // };

  for (const auto &[FDPattern, FD] : llvm::reverse(ParentInstantiations)) {
    SemaRef.addInstantiatedParametersToScope(FD, FDPattern, Scope, MLTAL);
    SemaRef.addInstantiatedLocalVarsToScope(FD, FDPattern, Scope);
  }
}
