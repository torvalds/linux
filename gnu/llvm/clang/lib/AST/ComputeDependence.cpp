//===- ComputeDependence.cpp ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/AST/ComputeDependence.h"
#include "clang/AST/Attr.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclarationName.h"
#include "clang/AST/DependenceFlags.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ExprConcepts.h"
#include "clang/AST/ExprObjC.h"
#include "clang/AST/ExprOpenMP.h"
#include "clang/Basic/ExceptionSpecificationType.h"
#include "llvm/ADT/ArrayRef.h"

using namespace clang;

ExprDependence clang::computeDependence(FullExpr *E) {
  return E->getSubExpr()->getDependence();
}

ExprDependence clang::computeDependence(OpaqueValueExpr *E) {
  auto D = toExprDependenceForImpliedType(E->getType()->getDependence());
  if (auto *S = E->getSourceExpr())
    D |= S->getDependence();
  assert(!(D & ExprDependence::UnexpandedPack));
  return D;
}

ExprDependence clang::computeDependence(ParenExpr *E) {
  return E->getSubExpr()->getDependence();
}

ExprDependence clang::computeDependence(UnaryOperator *E,
                                        const ASTContext &Ctx) {
  ExprDependence Dep =
      // FIXME: Do we need to look at the type?
      toExprDependenceForImpliedType(E->getType()->getDependence()) |
      E->getSubExpr()->getDependence();

  // C++ [temp.dep.constexpr]p5:
  //   An expression of the form & qualified-id where the qualified-id names a
  //   dependent member of the current instantiation is value-dependent. An
  //   expression of the form & cast-expression is also value-dependent if
  //   evaluating cast-expression as a core constant expression succeeds and
  //   the result of the evaluation refers to a templated entity that is an
  //   object with static or thread storage duration or a member function.
  //
  // What this amounts to is: constant-evaluate the operand and check whether it
  // refers to a templated entity other than a variable with local storage.
  if (Ctx.getLangOpts().CPlusPlus && E->getOpcode() == UO_AddrOf &&
      !(Dep & ExprDependence::Value)) {
    Expr::EvalResult Result;
    SmallVector<PartialDiagnosticAt, 8> Diag;
    Result.Diag = &Diag;
    // FIXME: This doesn't enforce the C++98 constant expression rules.
    if (E->getSubExpr()->EvaluateAsConstantExpr(Result, Ctx) && Diag.empty() &&
        Result.Val.isLValue()) {
      auto *VD = Result.Val.getLValueBase().dyn_cast<const ValueDecl *>();
      if (VD && VD->isTemplated()) {
        auto *VarD = dyn_cast<VarDecl>(VD);
        if (!VarD || !VarD->hasLocalStorage())
          Dep |= ExprDependence::Value;
      }
    }
  }

  return Dep;
}

ExprDependence clang::computeDependence(UnaryExprOrTypeTraitExpr *E) {
  // Never type-dependent (C++ [temp.dep.expr]p3).
  // Value-dependent if the argument is type-dependent.
  if (E->isArgumentType())
    return turnTypeToValueDependence(
        toExprDependenceAsWritten(E->getArgumentType()->getDependence()));

  auto ArgDeps = E->getArgumentExpr()->getDependence();
  auto Deps = ArgDeps & ~ExprDependence::TypeValue;
  // Value-dependent if the argument is type-dependent.
  if (ArgDeps & ExprDependence::Type)
    Deps |= ExprDependence::Value;
  // Check to see if we are in the situation where alignof(decl) should be
  // dependent because decl's alignment is dependent.
  auto ExprKind = E->getKind();
  if (ExprKind != UETT_AlignOf && ExprKind != UETT_PreferredAlignOf)
    return Deps;
  if ((Deps & ExprDependence::Value) && (Deps & ExprDependence::Instantiation))
    return Deps;

  auto *NoParens = E->getArgumentExpr()->IgnoreParens();
  const ValueDecl *D = nullptr;
  if (const auto *DRE = dyn_cast<DeclRefExpr>(NoParens))
    D = DRE->getDecl();
  else if (const auto *ME = dyn_cast<MemberExpr>(NoParens))
    D = ME->getMemberDecl();
  if (!D)
    return Deps;
  for (const auto *I : D->specific_attrs<AlignedAttr>()) {
    if (I->isAlignmentErrorDependent())
      Deps |= ExprDependence::Error;
    if (I->isAlignmentDependent())
      Deps |= ExprDependence::ValueInstantiation;
  }
  return Deps;
}

ExprDependence clang::computeDependence(ArraySubscriptExpr *E) {
  return E->getLHS()->getDependence() | E->getRHS()->getDependence();
}

ExprDependence clang::computeDependence(MatrixSubscriptExpr *E) {
  return E->getBase()->getDependence() | E->getRowIdx()->getDependence() |
         (E->getColumnIdx() ? E->getColumnIdx()->getDependence()
                            : ExprDependence::None);
}

ExprDependence clang::computeDependence(CompoundLiteralExpr *E) {
  return toExprDependenceAsWritten(
             E->getTypeSourceInfo()->getType()->getDependence()) |
         toExprDependenceForImpliedType(E->getType()->getDependence()) |
         turnTypeToValueDependence(E->getInitializer()->getDependence());
}

ExprDependence clang::computeDependence(ImplicitCastExpr *E) {
  // We model implicit conversions as combining the dependence of their
  // subexpression, apart from its type, with the semantic portion of the
  // target type.
  ExprDependence D =
      toExprDependenceForImpliedType(E->getType()->getDependence());
  if (auto *S = E->getSubExpr())
    D |= S->getDependence() & ~ExprDependence::Type;
  return D;
}

ExprDependence clang::computeDependence(ExplicitCastExpr *E) {
  // Cast expressions are type-dependent if the type is
  // dependent (C++ [temp.dep.expr]p3).
  // Cast expressions are value-dependent if the type is
  // dependent or if the subexpression is value-dependent.
  //
  // Note that we also need to consider the dependence of the actual type here,
  // because when the type as written is a deduced type, that type is not
  // dependent, but it may be deduced as a dependent type.
  ExprDependence D =
      toExprDependenceAsWritten(
          cast<ExplicitCastExpr>(E)->getTypeAsWritten()->getDependence()) |
      toExprDependenceForImpliedType(E->getType()->getDependence());
  if (auto *S = E->getSubExpr())
    D |= S->getDependence() & ~ExprDependence::Type;
  return D;
}

ExprDependence clang::computeDependence(BinaryOperator *E) {
  return E->getLHS()->getDependence() | E->getRHS()->getDependence();
}

ExprDependence clang::computeDependence(ConditionalOperator *E) {
  // The type of the conditional operator depends on the type of the conditional
  // to support the GCC vector conditional extension. Additionally,
  // [temp.dep.expr] does specify state that this should be dependent on ALL sub
  // expressions.
  return E->getCond()->getDependence() | E->getLHS()->getDependence() |
         E->getRHS()->getDependence();
}

ExprDependence clang::computeDependence(BinaryConditionalOperator *E) {
  return E->getCommon()->getDependence() | E->getFalseExpr()->getDependence();
}

ExprDependence clang::computeDependence(StmtExpr *E, unsigned TemplateDepth) {
  auto D = toExprDependenceForImpliedType(E->getType()->getDependence());
  // Propagate dependence of the result.
  if (const auto *CompoundExprResult =
          dyn_cast_or_null<ValueStmt>(E->getSubStmt()->getStmtExprResult()))
    if (const Expr *ResultExpr = CompoundExprResult->getExprStmt())
      D |= ResultExpr->getDependence();
  // Note: we treat a statement-expression in a dependent context as always
  // being value- and instantiation-dependent. This matches the behavior of
  // lambda-expressions and GCC.
  if (TemplateDepth)
    D |= ExprDependence::ValueInstantiation;
  // A param pack cannot be expanded over stmtexpr boundaries.
  return D & ~ExprDependence::UnexpandedPack;
}

ExprDependence clang::computeDependence(ConvertVectorExpr *E) {
  auto D = toExprDependenceAsWritten(
               E->getTypeSourceInfo()->getType()->getDependence()) |
           E->getSrcExpr()->getDependence();
  if (!E->getType()->isDependentType())
    D &= ~ExprDependence::Type;
  return D;
}

ExprDependence clang::computeDependence(ChooseExpr *E) {
  if (E->isConditionDependent())
    return ExprDependence::TypeValueInstantiation |
           E->getCond()->getDependence() | E->getLHS()->getDependence() |
           E->getRHS()->getDependence();

  auto Cond = E->getCond()->getDependence();
  auto Active = E->getLHS()->getDependence();
  auto Inactive = E->getRHS()->getDependence();
  if (!E->isConditionTrue())
    std::swap(Active, Inactive);
  // Take type- and value- dependency from the active branch. Propagate all
  // other flags from all branches.
  return (Active & ExprDependence::TypeValue) |
         ((Cond | Active | Inactive) & ~ExprDependence::TypeValue);
}

ExprDependence clang::computeDependence(ParenListExpr *P) {
  auto D = ExprDependence::None;
  for (auto *E : P->exprs())
    D |= E->getDependence();
  return D;
}

ExprDependence clang::computeDependence(VAArgExpr *E) {
  auto D = toExprDependenceAsWritten(
               E->getWrittenTypeInfo()->getType()->getDependence()) |
           (E->getSubExpr()->getDependence() & ~ExprDependence::Type);
  return D;
}

ExprDependence clang::computeDependence(NoInitExpr *E) {
  return toExprDependenceForImpliedType(E->getType()->getDependence()) &
         (ExprDependence::Instantiation | ExprDependence::Error);
}

ExprDependence clang::computeDependence(ArrayInitLoopExpr *E) {
  auto D = E->getCommonExpr()->getDependence() |
           E->getSubExpr()->getDependence() | ExprDependence::Instantiation;
  if (!E->getType()->isInstantiationDependentType())
    D &= ~ExprDependence::Instantiation;
  return turnTypeToValueDependence(D);
}

ExprDependence clang::computeDependence(ImplicitValueInitExpr *E) {
  return toExprDependenceForImpliedType(E->getType()->getDependence()) &
         ExprDependence::Instantiation;
}

ExprDependence clang::computeDependence(ExtVectorElementExpr *E) {
  return E->getBase()->getDependence();
}

ExprDependence clang::computeDependence(BlockExpr *E) {
  auto D = toExprDependenceForImpliedType(E->getType()->getDependence());
  if (E->getBlockDecl()->isDependentContext())
    D |= ExprDependence::Instantiation;
  return D;
}

ExprDependence clang::computeDependence(AsTypeExpr *E) {
  // FIXME: AsTypeExpr doesn't store the type as written. Assume the expression
  // type has identical sugar for now, so is a type-as-written.
  auto D = toExprDependenceAsWritten(E->getType()->getDependence()) |
           E->getSrcExpr()->getDependence();
  if (!E->getType()->isDependentType())
    D &= ~ExprDependence::Type;
  return D;
}

ExprDependence clang::computeDependence(CXXRewrittenBinaryOperator *E) {
  return E->getSemanticForm()->getDependence();
}

ExprDependence clang::computeDependence(CXXStdInitializerListExpr *E) {
  auto D = turnTypeToValueDependence(E->getSubExpr()->getDependence());
  D |= toExprDependenceForImpliedType(E->getType()->getDependence());
  return D;
}

ExprDependence clang::computeDependence(CXXTypeidExpr *E) {
  auto D = ExprDependence::None;
  if (E->isTypeOperand())
    D = toExprDependenceAsWritten(
        E->getTypeOperandSourceInfo()->getType()->getDependence());
  else
    D = turnTypeToValueDependence(E->getExprOperand()->getDependence());
  // typeid is never type-dependent (C++ [temp.dep.expr]p4)
  return D & ~ExprDependence::Type;
}

ExprDependence clang::computeDependence(MSPropertyRefExpr *E) {
  return E->getBaseExpr()->getDependence() & ~ExprDependence::Type;
}

ExprDependence clang::computeDependence(MSPropertySubscriptExpr *E) {
  return E->getIdx()->getDependence();
}

ExprDependence clang::computeDependence(CXXUuidofExpr *E) {
  if (E->isTypeOperand())
    return turnTypeToValueDependence(toExprDependenceAsWritten(
        E->getTypeOperandSourceInfo()->getType()->getDependence()));

  return turnTypeToValueDependence(E->getExprOperand()->getDependence());
}

ExprDependence clang::computeDependence(CXXThisExpr *E) {
  // 'this' is type-dependent if the class type of the enclosing
  // member function is dependent (C++ [temp.dep.expr]p2)
  auto D = toExprDependenceForImpliedType(E->getType()->getDependence());

  // If a lambda with an explicit object parameter captures '*this', then
  // 'this' now refers to the captured copy of lambda, and if the lambda
  // is type-dependent, so is the object and thus 'this'.
  //
  // Note: The standard does not mention this case explicitly, but we need
  // to do this so we can mark NSDM accesses as dependent.
  if (E->isCapturedByCopyInLambdaWithExplicitObjectParameter())
    D |= ExprDependence::Type;

  assert(!(D & ExprDependence::UnexpandedPack));
  return D;
}

ExprDependence clang::computeDependence(CXXThrowExpr *E) {
  auto *Op = E->getSubExpr();
  if (!Op)
    return ExprDependence::None;
  return Op->getDependence() & ~ExprDependence::TypeValue;
}

ExprDependence clang::computeDependence(CXXBindTemporaryExpr *E) {
  return E->getSubExpr()->getDependence();
}

ExprDependence clang::computeDependence(CXXScalarValueInitExpr *E) {
  auto D = toExprDependenceForImpliedType(E->getType()->getDependence());
  if (auto *TSI = E->getTypeSourceInfo())
    D |= toExprDependenceAsWritten(TSI->getType()->getDependence());
  return D;
}

ExprDependence clang::computeDependence(CXXDeleteExpr *E) {
  return turnTypeToValueDependence(E->getArgument()->getDependence());
}

ExprDependence clang::computeDependence(ArrayTypeTraitExpr *E) {
  auto D = toExprDependenceAsWritten(E->getQueriedType()->getDependence());
  if (auto *Dim = E->getDimensionExpression())
    D |= Dim->getDependence();
  return turnTypeToValueDependence(D);
}

ExprDependence clang::computeDependence(ExpressionTraitExpr *E) {
  // Never type-dependent.
  auto D = E->getQueriedExpression()->getDependence() & ~ExprDependence::Type;
  // Value-dependent if the argument is type-dependent.
  if (E->getQueriedExpression()->isTypeDependent())
    D |= ExprDependence::Value;
  return D;
}

ExprDependence clang::computeDependence(CXXNoexceptExpr *E, CanThrowResult CT) {
  auto D = E->getOperand()->getDependence() & ~ExprDependence::TypeValue;
  if (CT == CT_Dependent)
    D |= ExprDependence::ValueInstantiation;
  return D;
}

ExprDependence clang::computeDependence(PackExpansionExpr *E) {
  return (E->getPattern()->getDependence() & ~ExprDependence::UnexpandedPack) |
         ExprDependence::TypeValueInstantiation;
}

ExprDependence clang::computeDependence(PackIndexingExpr *E) {

  ExprDependence PatternDep = E->getPackIdExpression()->getDependence() &
                              ~ExprDependence::UnexpandedPack;

  ExprDependence D = E->getIndexExpr()->getDependence();
  if (D & ExprDependence::TypeValueInstantiation)
    D |= E->getIndexExpr()->getDependence() | PatternDep |
         ExprDependence::Instantiation;

  ArrayRef<Expr *> Exprs = E->getExpressions();
  if (Exprs.empty())
    D |= PatternDep | ExprDependence::Instantiation;

  else if (!E->getIndexExpr()->isInstantiationDependent()) {
    std::optional<unsigned> Index = E->getSelectedIndex();
    assert(Index && *Index < Exprs.size() && "pack index out of bound");
    D |= Exprs[*Index]->getDependence();
  }
  return D;
}

ExprDependence clang::computeDependence(SubstNonTypeTemplateParmExpr *E) {
  return E->getReplacement()->getDependence();
}

ExprDependence clang::computeDependence(CoroutineSuspendExpr *E) {
  if (auto *Resume = E->getResumeExpr())
    return (Resume->getDependence() &
            (ExprDependence::TypeValue | ExprDependence::Error)) |
           (E->getCommonExpr()->getDependence() & ~ExprDependence::TypeValue);
  return E->getCommonExpr()->getDependence() |
         ExprDependence::TypeValueInstantiation;
}

ExprDependence clang::computeDependence(DependentCoawaitExpr *E) {
  return E->getOperand()->getDependence() |
         ExprDependence::TypeValueInstantiation;
}

ExprDependence clang::computeDependence(ObjCBoxedExpr *E) {
  return E->getSubExpr()->getDependence();
}

ExprDependence clang::computeDependence(ObjCEncodeExpr *E) {
  return toExprDependenceAsWritten(E->getEncodedType()->getDependence());
}

ExprDependence clang::computeDependence(ObjCIvarRefExpr *E) {
  return turnTypeToValueDependence(E->getBase()->getDependence());
}

ExprDependence clang::computeDependence(ObjCPropertyRefExpr *E) {
  if (E->isObjectReceiver())
    return E->getBase()->getDependence() & ~ExprDependence::Type;
  if (E->isSuperReceiver())
    return toExprDependenceForImpliedType(
               E->getSuperReceiverType()->getDependence()) &
           ~ExprDependence::TypeValue;
  assert(E->isClassReceiver());
  return ExprDependence::None;
}

ExprDependence clang::computeDependence(ObjCSubscriptRefExpr *E) {
  return E->getBaseExpr()->getDependence() | E->getKeyExpr()->getDependence();
}

ExprDependence clang::computeDependence(ObjCIsaExpr *E) {
  return E->getBase()->getDependence() & ~ExprDependence::Type &
         ~ExprDependence::UnexpandedPack;
}

ExprDependence clang::computeDependence(ObjCIndirectCopyRestoreExpr *E) {
  return E->getSubExpr()->getDependence();
}

ExprDependence clang::computeDependence(ArraySectionExpr *E) {
  auto D = E->getBase()->getDependence();
  if (auto *LB = E->getLowerBound())
    D |= LB->getDependence();
  if (auto *Len = E->getLength())
    D |= Len->getDependence();

  if (E->isOMPArraySection()) {
    if (auto *Stride = E->getStride())
      D |= Stride->getDependence();
  }
  return D;
}

ExprDependence clang::computeDependence(OMPArrayShapingExpr *E) {
  auto D = E->getBase()->getDependence();
  for (Expr *Dim: E->getDimensions())
    if (Dim)
      D |= turnValueToTypeDependence(Dim->getDependence());
  return D;
}

ExprDependence clang::computeDependence(OMPIteratorExpr *E) {
  auto D = toExprDependenceForImpliedType(E->getType()->getDependence());
  for (unsigned I = 0, End = E->numOfIterators(); I < End; ++I) {
    if (auto *DD = cast_or_null<DeclaratorDecl>(E->getIteratorDecl(I))) {
      // If the type is omitted, it's 'int', and is not dependent in any way.
      if (auto *TSI = DD->getTypeSourceInfo()) {
        D |= toExprDependenceAsWritten(TSI->getType()->getDependence());
      }
    }
    OMPIteratorExpr::IteratorRange IR = E->getIteratorRange(I);
    if (Expr *BE = IR.Begin)
      D |= BE->getDependence();
    if (Expr *EE = IR.End)
      D |= EE->getDependence();
    if (Expr *SE = IR.Step)
      D |= SE->getDependence();
  }
  return D;
}

/// Compute the type-, value-, and instantiation-dependence of a
/// declaration reference
/// based on the declaration being referenced.
ExprDependence clang::computeDependence(DeclRefExpr *E, const ASTContext &Ctx) {
  auto Deps = ExprDependence::None;

  if (auto *NNS = E->getQualifier())
    Deps |= toExprDependence(NNS->getDependence() &
                             ~NestedNameSpecifierDependence::Dependent);

  if (auto *FirstArg = E->getTemplateArgs()) {
    unsigned NumArgs = E->getNumTemplateArgs();
    for (auto *Arg = FirstArg, *End = FirstArg + NumArgs; Arg < End; ++Arg)
      Deps |= toExprDependence(Arg->getArgument().getDependence());
  }

  auto *Decl = E->getDecl();
  auto Type = E->getType();

  if (Decl->isParameterPack())
    Deps |= ExprDependence::UnexpandedPack;
  Deps |= toExprDependenceForImpliedType(Type->getDependence()) &
          ExprDependence::Error;

  // C++ [temp.dep.expr]p3:
  //   An id-expression is type-dependent if it contains:

  //    - an identifier associated by name lookup with one or more declarations
  //      declared with a dependent type
  //    - an identifier associated by name lookup with an entity captured by
  //    copy ([expr.prim.lambda.capture])
  //      in a lambda-expression that has an explicit object parameter whose
  //      type is dependent ([dcl.fct]),
  //
  // [The "or more" case is not modeled as a DeclRefExpr. There are a bunch
  // more bullets here that we handle by treating the declaration as having a
  // dependent type if they involve a placeholder type that can't be deduced.]
  if (Type->isDependentType())
    Deps |= ExprDependence::TypeValueInstantiation;
  else if (Type->isInstantiationDependentType())
    Deps |= ExprDependence::Instantiation;

  //    - an identifier associated by name lookup with an entity captured by
  //    copy ([expr.prim.lambda.capture])
  if (E->isCapturedByCopyInLambdaWithExplicitObjectParameter())
    Deps |= ExprDependence::Type;

  //    - a conversion-function-id that specifies a dependent type
  if (Decl->getDeclName().getNameKind() ==
      DeclarationName::CXXConversionFunctionName) {
    QualType T = Decl->getDeclName().getCXXNameType();
    if (T->isDependentType())
      return Deps | ExprDependence::TypeValueInstantiation;

    if (T->isInstantiationDependentType())
      Deps |= ExprDependence::Instantiation;
  }

  //   - a template-id that is dependent,
  //   - a nested-name-specifier or a qualified-id that names a member of an
  //     unknown specialization
  //   [These are not modeled as DeclRefExprs.]

  //   or if it names a dependent member of the current instantiation that is a
  //   static data member of type "array of unknown bound of T" for some T
  //   [handled below].

  // C++ [temp.dep.constexpr]p2:
  //  An id-expression is value-dependent if:

  //    - it is type-dependent [handled above]

  //    - it is the name of a non-type template parameter,
  if (isa<NonTypeTemplateParmDecl>(Decl))
    return Deps | ExprDependence::ValueInstantiation;

  //   - it names a potentially-constant variable that is initialized with an
  //     expression that is value-dependent
  if (const auto *Var = dyn_cast<VarDecl>(Decl)) {
    if (const Expr *Init = Var->getAnyInitializer()) {
      if (Init->containsErrors())
        Deps |= ExprDependence::Error;

      if (Var->mightBeUsableInConstantExpressions(Ctx) &&
          Init->isValueDependent())
        Deps |= ExprDependence::ValueInstantiation;
    }

    // - it names a static data member that is a dependent member of the
    //   current instantiation and is not initialized in a member-declarator,
    if (Var->isStaticDataMember() &&
        Var->getDeclContext()->isDependentContext() &&
        !Var->getFirstDecl()->hasInit()) {
      const VarDecl *First = Var->getFirstDecl();
      TypeSourceInfo *TInfo = First->getTypeSourceInfo();
      if (TInfo->getType()->isIncompleteArrayType()) {
        Deps |= ExprDependence::TypeValueInstantiation;
      } else if (!First->hasInit()) {
        Deps |= ExprDependence::ValueInstantiation;
      }
    }

    return Deps;
  }

  //   - it names a static member function that is a dependent member of the
  //     current instantiation
  //
  // FIXME: It's unclear that the restriction to static members here has any
  // effect: any use of a non-static member function name requires either
  // forming a pointer-to-member or providing an object parameter, either of
  // which makes the overall expression value-dependent.
  if (auto *MD = dyn_cast<CXXMethodDecl>(Decl)) {
    if (MD->isStatic() && Decl->getDeclContext()->isDependentContext())
      Deps |= ExprDependence::ValueInstantiation;
  }

  return Deps;
}

ExprDependence clang::computeDependence(RecoveryExpr *E) {
  // RecoveryExpr is
  //   - always value-dependent, and therefore instantiation dependent
  //   - contains errors (ExprDependence::Error), by definition
  //   - type-dependent if we don't know the type (fallback to an opaque
  //     dependent type), or the type is known and dependent, or it has
  //     type-dependent subexpressions.
  auto D = toExprDependenceAsWritten(E->getType()->getDependence()) |
           ExprDependence::ErrorDependent;
  // FIXME: remove the type-dependent bit from subexpressions, if the
  // RecoveryExpr has a non-dependent type.
  for (auto *S : E->subExpressions())
    D |= S->getDependence();
  return D;
}

ExprDependence clang::computeDependence(SYCLUniqueStableNameExpr *E) {
  return toExprDependenceAsWritten(
      E->getTypeSourceInfo()->getType()->getDependence());
}

ExprDependence clang::computeDependence(PredefinedExpr *E) {
  return toExprDependenceForImpliedType(E->getType()->getDependence());
}

ExprDependence clang::computeDependence(CallExpr *E,
                                        llvm::ArrayRef<Expr *> PreArgs) {
  auto D = E->getCallee()->getDependence();
  if (E->getType()->isDependentType())
    D |= ExprDependence::Type;
  for (auto *A : llvm::ArrayRef(E->getArgs(), E->getNumArgs())) {
    if (A)
      D |= A->getDependence();
  }
  for (auto *A : PreArgs)
    D |= A->getDependence();
  return D;
}

ExprDependence clang::computeDependence(OffsetOfExpr *E) {
  auto D = turnTypeToValueDependence(toExprDependenceAsWritten(
      E->getTypeSourceInfo()->getType()->getDependence()));
  for (unsigned I = 0, N = E->getNumExpressions(); I < N; ++I)
    D |= turnTypeToValueDependence(E->getIndexExpr(I)->getDependence());
  return D;
}

static inline ExprDependence getDependenceInExpr(DeclarationNameInfo Name) {
  auto D = ExprDependence::None;
  if (Name.isInstantiationDependent())
    D |= ExprDependence::Instantiation;
  if (Name.containsUnexpandedParameterPack())
    D |= ExprDependence::UnexpandedPack;
  return D;
}

ExprDependence clang::computeDependence(MemberExpr *E) {
  auto D = E->getBase()->getDependence();
  D |= getDependenceInExpr(E->getMemberNameInfo());

  if (auto *NNS = E->getQualifier())
    D |= toExprDependence(NNS->getDependence() &
                          ~NestedNameSpecifierDependence::Dependent);

  for (const auto &A : E->template_arguments())
    D |= toExprDependence(A.getArgument().getDependence());

  auto *MemberDecl = E->getMemberDecl();
  if (FieldDecl *FD = dyn_cast<FieldDecl>(MemberDecl)) {
    DeclContext *DC = MemberDecl->getDeclContext();
    // dyn_cast_or_null is used to handle objC variables which do not
    // have a declaration context.
    CXXRecordDecl *RD = dyn_cast_or_null<CXXRecordDecl>(DC);
    if (RD && RD->isDependentContext() && RD->isCurrentInstantiation(DC)) {
      if (!E->getType()->isDependentType())
        D &= ~ExprDependence::Type;
    }

    // Bitfield with value-dependent width is type-dependent.
    if (FD && FD->isBitField() && FD->getBitWidth()->isValueDependent()) {
      D |= ExprDependence::Type;
    }
  }
  return D;
}

ExprDependence clang::computeDependence(InitListExpr *E) {
  auto D = ExprDependence::None;
  for (auto *A : E->inits())
    D |= A->getDependence();
  return D;
}

ExprDependence clang::computeDependence(ShuffleVectorExpr *E) {
  auto D = toExprDependenceForImpliedType(E->getType()->getDependence());
  for (auto *C : llvm::ArrayRef(E->getSubExprs(), E->getNumSubExprs()))
    D |= C->getDependence();
  return D;
}

ExprDependence clang::computeDependence(GenericSelectionExpr *E,
                                        bool ContainsUnexpandedPack) {
  auto D = ContainsUnexpandedPack ? ExprDependence::UnexpandedPack
                                  : ExprDependence::None;
  for (auto *AE : E->getAssocExprs())
    D |= AE->getDependence() & ExprDependence::Error;

  if (E->isExprPredicate())
    D |= E->getControllingExpr()->getDependence() & ExprDependence::Error;
  else
    D |= toExprDependenceAsWritten(
        E->getControllingType()->getType()->getDependence());

  if (E->isResultDependent())
    return D | ExprDependence::TypeValueInstantiation;
  return D | (E->getResultExpr()->getDependence() &
              ~ExprDependence::UnexpandedPack);
}

ExprDependence clang::computeDependence(DesignatedInitExpr *E) {
  auto Deps = E->getInit()->getDependence();
  for (const auto &D : E->designators()) {
    auto DesignatorDeps = ExprDependence::None;
    if (D.isArrayDesignator())
      DesignatorDeps |= E->getArrayIndex(D)->getDependence();
    else if (D.isArrayRangeDesignator())
      DesignatorDeps |= E->getArrayRangeStart(D)->getDependence() |
                        E->getArrayRangeEnd(D)->getDependence();
    Deps |= DesignatorDeps;
    if (DesignatorDeps & ExprDependence::TypeValue)
      Deps |= ExprDependence::TypeValueInstantiation;
  }
  return Deps;
}

ExprDependence clang::computeDependence(PseudoObjectExpr *O) {
  auto D = O->getSyntacticForm()->getDependence();
  for (auto *E : O->semantics())
    D |= E->getDependence();
  return D;
}

ExprDependence clang::computeDependence(AtomicExpr *A) {
  auto D = ExprDependence::None;
  for (auto *E : llvm::ArrayRef(A->getSubExprs(), A->getNumSubExprs()))
    D |= E->getDependence();
  return D;
}

ExprDependence clang::computeDependence(CXXNewExpr *E) {
  auto D = toExprDependenceAsWritten(
      E->getAllocatedTypeSourceInfo()->getType()->getDependence());
  D |= toExprDependenceForImpliedType(E->getAllocatedType()->getDependence());
  auto Size = E->getArraySize();
  if (Size && *Size)
    D |= turnTypeToValueDependence((*Size)->getDependence());
  if (auto *I = E->getInitializer())
    D |= turnTypeToValueDependence(I->getDependence());
  for (auto *A : E->placement_arguments())
    D |= turnTypeToValueDependence(A->getDependence());
  return D;
}

ExprDependence clang::computeDependence(CXXPseudoDestructorExpr *E) {
  auto D = E->getBase()->getDependence();
  if (auto *TSI = E->getDestroyedTypeInfo())
    D |= toExprDependenceAsWritten(TSI->getType()->getDependence());
  if (auto *ST = E->getScopeTypeInfo())
    D |= turnTypeToValueDependence(
        toExprDependenceAsWritten(ST->getType()->getDependence()));
  if (auto *Q = E->getQualifier())
    D |= toExprDependence(Q->getDependence() &
                          ~NestedNameSpecifierDependence::Dependent);
  return D;
}

ExprDependence
clang::computeDependence(OverloadExpr *E, bool KnownDependent,
                         bool KnownInstantiationDependent,
                         bool KnownContainsUnexpandedParameterPack) {
  auto Deps = ExprDependence::None;
  if (KnownDependent)
    Deps |= ExprDependence::TypeValue;
  if (KnownInstantiationDependent)
    Deps |= ExprDependence::Instantiation;
  if (KnownContainsUnexpandedParameterPack)
    Deps |= ExprDependence::UnexpandedPack;
  Deps |= getDependenceInExpr(E->getNameInfo());
  if (auto *Q = E->getQualifier())
    Deps |= toExprDependence(Q->getDependence() &
                             ~NestedNameSpecifierDependence::Dependent);
  for (auto *D : E->decls()) {
    if (D->getDeclContext()->isDependentContext() ||
        isa<UnresolvedUsingValueDecl>(D))
      Deps |= ExprDependence::TypeValueInstantiation;
  }
  // If we have explicit template arguments, check for dependent
  // template arguments and whether they contain any unexpanded pack
  // expansions.
  for (const auto &A : E->template_arguments())
    Deps |= toExprDependence(A.getArgument().getDependence());
  return Deps;
}

ExprDependence clang::computeDependence(DependentScopeDeclRefExpr *E) {
  auto D = ExprDependence::TypeValue;
  D |= getDependenceInExpr(E->getNameInfo());
  if (auto *Q = E->getQualifier())
    D |= toExprDependence(Q->getDependence());
  for (const auto &A : E->template_arguments())
    D |= toExprDependence(A.getArgument().getDependence());
  return D;
}

ExprDependence clang::computeDependence(CXXConstructExpr *E) {
  ExprDependence D =
      toExprDependenceForImpliedType(E->getType()->getDependence());
  for (auto *A : E->arguments())
    D |= A->getDependence() & ~ExprDependence::Type;
  return D;
}

ExprDependence clang::computeDependence(CXXTemporaryObjectExpr *E) {
  CXXConstructExpr *BaseE = E;
  return toExprDependenceAsWritten(
             E->getTypeSourceInfo()->getType()->getDependence()) |
         computeDependence(BaseE);
}

ExprDependence clang::computeDependence(CXXDefaultInitExpr *E) {
  return E->getExpr()->getDependence();
}

ExprDependence clang::computeDependence(CXXDefaultArgExpr *E) {
  return E->getExpr()->getDependence();
}

ExprDependence clang::computeDependence(LambdaExpr *E,
                                        bool ContainsUnexpandedParameterPack) {
  auto D = toExprDependenceForImpliedType(E->getType()->getDependence());
  if (ContainsUnexpandedParameterPack)
    D |= ExprDependence::UnexpandedPack;
  return D;
}

ExprDependence clang::computeDependence(CXXUnresolvedConstructExpr *E) {
  auto D = ExprDependence::ValueInstantiation;
  D |= toExprDependenceAsWritten(E->getTypeAsWritten()->getDependence());
  D |= toExprDependenceForImpliedType(E->getType()->getDependence());
  for (auto *A : E->arguments())
    D |= A->getDependence() &
         (ExprDependence::UnexpandedPack | ExprDependence::Error);
  return D;
}

ExprDependence clang::computeDependence(CXXDependentScopeMemberExpr *E) {
  auto D = ExprDependence::TypeValueInstantiation;
  if (!E->isImplicitAccess())
    D |= E->getBase()->getDependence();
  if (auto *Q = E->getQualifier())
    D |= toExprDependence(Q->getDependence());
  D |= getDependenceInExpr(E->getMemberNameInfo());
  for (const auto &A : E->template_arguments())
    D |= toExprDependence(A.getArgument().getDependence());
  return D;
}

ExprDependence clang::computeDependence(MaterializeTemporaryExpr *E) {
  return E->getSubExpr()->getDependence();
}

ExprDependence clang::computeDependence(CXXFoldExpr *E) {
  auto D = ExprDependence::TypeValueInstantiation;
  for (const auto *C : {E->getLHS(), E->getRHS()}) {
    if (C)
      D |= C->getDependence() & ~ExprDependence::UnexpandedPack;
  }
  return D;
}

ExprDependence clang::computeDependence(CXXParenListInitExpr *E) {
  auto D = ExprDependence::None;
  for (const auto *A : E->getInitExprs())
    D |= A->getDependence();
  return D;
}

ExprDependence clang::computeDependence(TypeTraitExpr *E) {
  auto D = ExprDependence::None;
  for (const auto *A : E->getArgs())
    D |= toExprDependenceAsWritten(A->getType()->getDependence()) &
         ~ExprDependence::Type;
  return D;
}

ExprDependence clang::computeDependence(ConceptSpecializationExpr *E,
                                        bool ValueDependent) {
  auto TA = TemplateArgumentDependence::None;
  const auto InterestingDeps = TemplateArgumentDependence::Instantiation |
                               TemplateArgumentDependence::UnexpandedPack;
  for (const TemplateArgumentLoc &ArgLoc :
       E->getTemplateArgsAsWritten()->arguments()) {
    TA |= ArgLoc.getArgument().getDependence() & InterestingDeps;
    if (TA == InterestingDeps)
      break;
  }

  ExprDependence D =
      ValueDependent ? ExprDependence::Value : ExprDependence::None;
  auto Res = D | toExprDependence(TA);
  if(!ValueDependent && E->getSatisfaction().ContainsErrors)
    Res |= ExprDependence::Error;
  return Res;
}

ExprDependence clang::computeDependence(ObjCArrayLiteral *E) {
  auto D = ExprDependence::None;
  Expr **Elements = E->getElements();
  for (unsigned I = 0, N = E->getNumElements(); I != N; ++I)
    D |= turnTypeToValueDependence(Elements[I]->getDependence());
  return D;
}

ExprDependence clang::computeDependence(ObjCDictionaryLiteral *E) {
  auto Deps = ExprDependence::None;
  for (unsigned I = 0, N = E->getNumElements(); I < N; ++I) {
    auto KV = E->getKeyValueElement(I);
    auto KVDeps = turnTypeToValueDependence(KV.Key->getDependence() |
                                            KV.Value->getDependence());
    if (KV.EllipsisLoc.isValid())
      KVDeps &= ~ExprDependence::UnexpandedPack;
    Deps |= KVDeps;
  }
  return Deps;
}

ExprDependence clang::computeDependence(ObjCMessageExpr *E) {
  auto D = ExprDependence::None;
  if (auto *R = E->getInstanceReceiver())
    D |= R->getDependence();
  else
    D |= toExprDependenceForImpliedType(E->getType()->getDependence());
  for (auto *A : E->arguments())
    D |= A->getDependence();
  return D;
}
