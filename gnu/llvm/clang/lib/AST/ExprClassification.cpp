//===- ExprClassification.cpp - Expression AST Node Implementation --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements Expr::classify.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/Expr.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ExprObjC.h"
#include "llvm/Support/ErrorHandling.h"

using namespace clang;

using Cl = Expr::Classification;

static Cl::Kinds ClassifyInternal(ASTContext &Ctx, const Expr *E);
static Cl::Kinds ClassifyDecl(ASTContext &Ctx, const Decl *D);
static Cl::Kinds ClassifyUnnamed(ASTContext &Ctx, QualType T);
static Cl::Kinds ClassifyMemberExpr(ASTContext &Ctx, const MemberExpr *E);
static Cl::Kinds ClassifyBinaryOp(ASTContext &Ctx, const BinaryOperator *E);
static Cl::Kinds ClassifyConditional(ASTContext &Ctx,
                                     const Expr *trueExpr,
                                     const Expr *falseExpr);
static Cl::ModifiableType IsModifiable(ASTContext &Ctx, const Expr *E,
                                       Cl::Kinds Kind, SourceLocation &Loc);

Cl Expr::ClassifyImpl(ASTContext &Ctx, SourceLocation *Loc) const {
  assert(!TR->isReferenceType() && "Expressions can't have reference type.");

  Cl::Kinds kind = ClassifyInternal(Ctx, this);
  // C99 6.3.2.1: An lvalue is an expression with an object type or an
  //   incomplete type other than void.
  if (!Ctx.getLangOpts().CPlusPlus) {
    // Thus, no functions.
    if (TR->isFunctionType() || TR == Ctx.OverloadTy)
      kind = Cl::CL_Function;
    // No void either, but qualified void is OK because it is "other than void".
    // Void "lvalues" are classified as addressable void values, which are void
    // expressions whose address can be taken.
    else if (TR->isVoidType() && !TR.hasQualifiers())
      kind = (kind == Cl::CL_LValue ? Cl::CL_AddressableVoid : Cl::CL_Void);
  }

  // Enable this assertion for testing.
  switch (kind) {
  case Cl::CL_LValue:
    assert(isLValue());
    break;
  case Cl::CL_XValue:
    assert(isXValue());
    break;
  case Cl::CL_Function:
  case Cl::CL_Void:
  case Cl::CL_AddressableVoid:
  case Cl::CL_DuplicateVectorComponents:
  case Cl::CL_MemberFunction:
  case Cl::CL_SubObjCPropertySetting:
  case Cl::CL_ClassTemporary:
  case Cl::CL_ArrayTemporary:
  case Cl::CL_ObjCMessageRValue:
  case Cl::CL_PRValue:
    assert(isPRValue());
    break;
  }

  Cl::ModifiableType modifiable = Cl::CM_Untested;
  if (Loc)
    modifiable = IsModifiable(Ctx, this, kind, *Loc);
  return Classification(kind, modifiable);
}

/// Classify an expression which creates a temporary, based on its type.
static Cl::Kinds ClassifyTemporary(QualType T) {
  if (T->isRecordType())
    return Cl::CL_ClassTemporary;
  if (T->isArrayType())
    return Cl::CL_ArrayTemporary;

  // No special classification: these don't behave differently from normal
  // prvalues.
  return Cl::CL_PRValue;
}

static Cl::Kinds ClassifyExprValueKind(const LangOptions &Lang,
                                       const Expr *E,
                                       ExprValueKind Kind) {
  switch (Kind) {
  case VK_PRValue:
    return Lang.CPlusPlus ? ClassifyTemporary(E->getType()) : Cl::CL_PRValue;
  case VK_LValue:
    return Cl::CL_LValue;
  case VK_XValue:
    return Cl::CL_XValue;
  }
  llvm_unreachable("Invalid value category of implicit cast.");
}

static Cl::Kinds ClassifyInternal(ASTContext &Ctx, const Expr *E) {
  // This function takes the first stab at classifying expressions.
  const LangOptions &Lang = Ctx.getLangOpts();

  switch (E->getStmtClass()) {
  case Stmt::NoStmtClass:
#define ABSTRACT_STMT(Kind)
#define STMT(Kind, Base) case Expr::Kind##Class:
#define EXPR(Kind, Base)
#include "clang/AST/StmtNodes.inc"
    llvm_unreachable("cannot classify a statement");

    // First come the expressions that are always lvalues, unconditionally.
  case Expr::ObjCIsaExprClass:
    // C++ [expr.prim.general]p1: A string literal is an lvalue.
  case Expr::StringLiteralClass:
    // @encode is equivalent to its string
  case Expr::ObjCEncodeExprClass:
    // __func__ and friends are too.
  case Expr::PredefinedExprClass:
    // Property references are lvalues
  case Expr::ObjCSubscriptRefExprClass:
  case Expr::ObjCPropertyRefExprClass:
    // C++ [expr.typeid]p1: The result of a typeid expression is an lvalue of...
  case Expr::CXXTypeidExprClass:
  case Expr::CXXUuidofExprClass:
    // Unresolved lookups and uncorrected typos get classified as lvalues.
    // FIXME: Is this wise? Should they get their own kind?
  case Expr::UnresolvedLookupExprClass:
  case Expr::UnresolvedMemberExprClass:
  case Expr::TypoExprClass:
  case Expr::DependentCoawaitExprClass:
  case Expr::CXXDependentScopeMemberExprClass:
  case Expr::DependentScopeDeclRefExprClass:
    // ObjC instance variables are lvalues
    // FIXME: ObjC++0x might have different rules
  case Expr::ObjCIvarRefExprClass:
  case Expr::FunctionParmPackExprClass:
  case Expr::MSPropertyRefExprClass:
  case Expr::MSPropertySubscriptExprClass:
  case Expr::ArraySectionExprClass:
  case Expr::OMPArrayShapingExprClass:
  case Expr::OMPIteratorExprClass:
    return Cl::CL_LValue;

    // C99 6.5.2.5p5 says that compound literals are lvalues.
    // In C++, they're prvalue temporaries, except for file-scope arrays.
  case Expr::CompoundLiteralExprClass:
    return !E->isLValue() ? ClassifyTemporary(E->getType()) : Cl::CL_LValue;

    // Expressions that are prvalues.
  case Expr::CXXBoolLiteralExprClass:
  case Expr::CXXPseudoDestructorExprClass:
  case Expr::UnaryExprOrTypeTraitExprClass:
  case Expr::CXXNewExprClass:
  case Expr::CXXNullPtrLiteralExprClass:
  case Expr::ImaginaryLiteralClass:
  case Expr::GNUNullExprClass:
  case Expr::OffsetOfExprClass:
  case Expr::CXXThrowExprClass:
  case Expr::ShuffleVectorExprClass:
  case Expr::ConvertVectorExprClass:
  case Expr::IntegerLiteralClass:
  case Expr::FixedPointLiteralClass:
  case Expr::CharacterLiteralClass:
  case Expr::AddrLabelExprClass:
  case Expr::CXXDeleteExprClass:
  case Expr::ImplicitValueInitExprClass:
  case Expr::BlockExprClass:
  case Expr::FloatingLiteralClass:
  case Expr::CXXNoexceptExprClass:
  case Expr::CXXScalarValueInitExprClass:
  case Expr::TypeTraitExprClass:
  case Expr::ArrayTypeTraitExprClass:
  case Expr::ExpressionTraitExprClass:
  case Expr::ObjCSelectorExprClass:
  case Expr::ObjCProtocolExprClass:
  case Expr::ObjCStringLiteralClass:
  case Expr::ObjCBoxedExprClass:
  case Expr::ObjCArrayLiteralClass:
  case Expr::ObjCDictionaryLiteralClass:
  case Expr::ObjCBoolLiteralExprClass:
  case Expr::ObjCAvailabilityCheckExprClass:
  case Expr::ParenListExprClass:
  case Expr::SizeOfPackExprClass:
  case Expr::SubstNonTypeTemplateParmPackExprClass:
  case Expr::AsTypeExprClass:
  case Expr::ObjCIndirectCopyRestoreExprClass:
  case Expr::AtomicExprClass:
  case Expr::CXXFoldExprClass:
  case Expr::ArrayInitLoopExprClass:
  case Expr::ArrayInitIndexExprClass:
  case Expr::NoInitExprClass:
  case Expr::DesignatedInitUpdateExprClass:
  case Expr::SourceLocExprClass:
  case Expr::ConceptSpecializationExprClass:
  case Expr::RequiresExprClass:
    return Cl::CL_PRValue;

  case Expr::EmbedExprClass:
    // Nominally, this just goes through as a PRValue until we actually expand
    // it and check it.
    return Cl::CL_PRValue;

  // Make HLSL this reference-like
  case Expr::CXXThisExprClass:
    return Lang.HLSL ? Cl::CL_LValue : Cl::CL_PRValue;

  case Expr::ConstantExprClass:
    return ClassifyInternal(Ctx, cast<ConstantExpr>(E)->getSubExpr());

    // Next come the complicated cases.
  case Expr::SubstNonTypeTemplateParmExprClass:
    return ClassifyInternal(Ctx,
                 cast<SubstNonTypeTemplateParmExpr>(E)->getReplacement());

  case Expr::PackIndexingExprClass: {
    // A pack-index-expression always expands to an id-expression.
    // Consider it as an LValue expression.
    if (cast<PackIndexingExpr>(E)->isInstantiationDependent())
      return Cl::CL_LValue;
    return ClassifyInternal(Ctx, cast<PackIndexingExpr>(E)->getSelectedExpr());
  }

    // C, C++98 [expr.sub]p1: The result is an lvalue of type "T".
    // C++11 (DR1213): in the case of an array operand, the result is an lvalue
    //                 if that operand is an lvalue and an xvalue otherwise.
    // Subscripting vector types is more like member access.
  case Expr::ArraySubscriptExprClass:
    if (cast<ArraySubscriptExpr>(E)->getBase()->getType()->isVectorType())
      return ClassifyInternal(Ctx, cast<ArraySubscriptExpr>(E)->getBase());
    if (Lang.CPlusPlus11) {
      // Step over the array-to-pointer decay if present, but not over the
      // temporary materialization.
      auto *Base = cast<ArraySubscriptExpr>(E)->getBase()->IgnoreImpCasts();
      if (Base->getType()->isArrayType())
        return ClassifyInternal(Ctx, Base);
    }
    return Cl::CL_LValue;

  // Subscripting matrix types behaves like member accesses.
  case Expr::MatrixSubscriptExprClass:
    return ClassifyInternal(Ctx, cast<MatrixSubscriptExpr>(E)->getBase());

    // C++ [expr.prim.general]p3: The result is an lvalue if the entity is a
    //   function or variable and a prvalue otherwise.
  case Expr::DeclRefExprClass:
    if (E->getType() == Ctx.UnknownAnyTy)
      return isa<FunctionDecl>(cast<DeclRefExpr>(E)->getDecl())
               ? Cl::CL_PRValue : Cl::CL_LValue;
    return ClassifyDecl(Ctx, cast<DeclRefExpr>(E)->getDecl());

    // Member access is complex.
  case Expr::MemberExprClass:
    return ClassifyMemberExpr(Ctx, cast<MemberExpr>(E));

  case Expr::UnaryOperatorClass:
    switch (cast<UnaryOperator>(E)->getOpcode()) {
      // C++ [expr.unary.op]p1: The unary * operator performs indirection:
      //   [...] the result is an lvalue referring to the object or function
      //   to which the expression points.
    case UO_Deref:
      return Cl::CL_LValue;

      // GNU extensions, simply look through them.
    case UO_Extension:
      return ClassifyInternal(Ctx, cast<UnaryOperator>(E)->getSubExpr());

    // Treat _Real and _Imag basically as if they were member
    // expressions:  l-value only if the operand is a true l-value.
    case UO_Real:
    case UO_Imag: {
      const Expr *Op = cast<UnaryOperator>(E)->getSubExpr()->IgnoreParens();
      Cl::Kinds K = ClassifyInternal(Ctx, Op);
      if (K != Cl::CL_LValue) return K;

      if (isa<ObjCPropertyRefExpr>(Op))
        return Cl::CL_SubObjCPropertySetting;
      return Cl::CL_LValue;
    }

      // C++ [expr.pre.incr]p1: The result is the updated operand; it is an
      //   lvalue, [...]
      // Not so in C.
    case UO_PreInc:
    case UO_PreDec:
      return Lang.CPlusPlus ? Cl::CL_LValue : Cl::CL_PRValue;

    default:
      return Cl::CL_PRValue;
    }

  case Expr::RecoveryExprClass:
  case Expr::OpaqueValueExprClass:
    return ClassifyExprValueKind(Lang, E, E->getValueKind());

    // Pseudo-object expressions can produce l-values with reference magic.
  case Expr::PseudoObjectExprClass:
    return ClassifyExprValueKind(Lang, E,
                                 cast<PseudoObjectExpr>(E)->getValueKind());

    // Implicit casts are lvalues if they're lvalue casts. Other than that, we
    // only specifically record class temporaries.
  case Expr::ImplicitCastExprClass:
    return ClassifyExprValueKind(Lang, E, E->getValueKind());

    // C++ [expr.prim.general]p4: The presence of parentheses does not affect
    //   whether the expression is an lvalue.
  case Expr::ParenExprClass:
    return ClassifyInternal(Ctx, cast<ParenExpr>(E)->getSubExpr());

    // C11 6.5.1.1p4: [A generic selection] is an lvalue, a function designator,
    // or a void expression if its result expression is, respectively, an
    // lvalue, a function designator, or a void expression.
  case Expr::GenericSelectionExprClass:
    if (cast<GenericSelectionExpr>(E)->isResultDependent())
      return Cl::CL_PRValue;
    return ClassifyInternal(Ctx,cast<GenericSelectionExpr>(E)->getResultExpr());

  case Expr::BinaryOperatorClass:
  case Expr::CompoundAssignOperatorClass:
    // C doesn't have any binary expressions that are lvalues.
    if (Lang.CPlusPlus)
      return ClassifyBinaryOp(Ctx, cast<BinaryOperator>(E));
    return Cl::CL_PRValue;

  case Expr::CallExprClass:
  case Expr::CXXOperatorCallExprClass:
  case Expr::CXXMemberCallExprClass:
  case Expr::UserDefinedLiteralClass:
  case Expr::CUDAKernelCallExprClass:
    return ClassifyUnnamed(Ctx, cast<CallExpr>(E)->getCallReturnType(Ctx));

  case Expr::CXXRewrittenBinaryOperatorClass:
    return ClassifyInternal(
        Ctx, cast<CXXRewrittenBinaryOperator>(E)->getSemanticForm());

    // __builtin_choose_expr is equivalent to the chosen expression.
  case Expr::ChooseExprClass:
    return ClassifyInternal(Ctx, cast<ChooseExpr>(E)->getChosenSubExpr());

    // Extended vector element access is an lvalue unless there are duplicates
    // in the shuffle expression.
  case Expr::ExtVectorElementExprClass:
    if (cast<ExtVectorElementExpr>(E)->containsDuplicateElements())
      return Cl::CL_DuplicateVectorComponents;
    if (cast<ExtVectorElementExpr>(E)->isArrow())
      return Cl::CL_LValue;
    return ClassifyInternal(Ctx, cast<ExtVectorElementExpr>(E)->getBase());

    // Simply look at the actual default argument.
  case Expr::CXXDefaultArgExprClass:
    return ClassifyInternal(Ctx, cast<CXXDefaultArgExpr>(E)->getExpr());

    // Same idea for default initializers.
  case Expr::CXXDefaultInitExprClass:
    return ClassifyInternal(Ctx, cast<CXXDefaultInitExpr>(E)->getExpr());

    // Same idea for temporary binding.
  case Expr::CXXBindTemporaryExprClass:
    return ClassifyInternal(Ctx, cast<CXXBindTemporaryExpr>(E)->getSubExpr());

    // And the cleanups guard.
  case Expr::ExprWithCleanupsClass:
    return ClassifyInternal(Ctx, cast<ExprWithCleanups>(E)->getSubExpr());

    // Casts depend completely on the target type. All casts work the same.
  case Expr::CStyleCastExprClass:
  case Expr::CXXFunctionalCastExprClass:
  case Expr::CXXStaticCastExprClass:
  case Expr::CXXDynamicCastExprClass:
  case Expr::CXXReinterpretCastExprClass:
  case Expr::CXXConstCastExprClass:
  case Expr::CXXAddrspaceCastExprClass:
  case Expr::ObjCBridgedCastExprClass:
  case Expr::BuiltinBitCastExprClass:
    // Only in C++ can casts be interesting at all.
    if (!Lang.CPlusPlus) return Cl::CL_PRValue;
    return ClassifyUnnamed(Ctx, cast<ExplicitCastExpr>(E)->getTypeAsWritten());

  case Expr::CXXUnresolvedConstructExprClass:
    return ClassifyUnnamed(Ctx,
                      cast<CXXUnresolvedConstructExpr>(E)->getTypeAsWritten());

  case Expr::BinaryConditionalOperatorClass: {
    if (!Lang.CPlusPlus) return Cl::CL_PRValue;
    const auto *co = cast<BinaryConditionalOperator>(E);
    return ClassifyConditional(Ctx, co->getTrueExpr(), co->getFalseExpr());
  }

  case Expr::ConditionalOperatorClass: {
    // Once again, only C++ is interesting.
    if (!Lang.CPlusPlus) return Cl::CL_PRValue;
    const auto *co = cast<ConditionalOperator>(E);
    return ClassifyConditional(Ctx, co->getTrueExpr(), co->getFalseExpr());
  }

    // ObjC message sends are effectively function calls, if the target function
    // is known.
  case Expr::ObjCMessageExprClass:
    if (const ObjCMethodDecl *Method =
          cast<ObjCMessageExpr>(E)->getMethodDecl()) {
      Cl::Kinds kind = ClassifyUnnamed(Ctx, Method->getReturnType());
      return (kind == Cl::CL_PRValue) ? Cl::CL_ObjCMessageRValue : kind;
    }
    return Cl::CL_PRValue;

    // Some C++ expressions are always class temporaries.
  case Expr::CXXConstructExprClass:
  case Expr::CXXInheritedCtorInitExprClass:
  case Expr::CXXTemporaryObjectExprClass:
  case Expr::LambdaExprClass:
  case Expr::CXXStdInitializerListExprClass:
    return Cl::CL_ClassTemporary;

  case Expr::VAArgExprClass:
    return ClassifyUnnamed(Ctx, E->getType());

  case Expr::DesignatedInitExprClass:
    return ClassifyInternal(Ctx, cast<DesignatedInitExpr>(E)->getInit());

  case Expr::StmtExprClass: {
    const CompoundStmt *S = cast<StmtExpr>(E)->getSubStmt();
    if (const auto *LastExpr = dyn_cast_or_null<Expr>(S->body_back()))
      return ClassifyUnnamed(Ctx, LastExpr->getType());
    return Cl::CL_PRValue;
  }

  case Expr::PackExpansionExprClass:
    return ClassifyInternal(Ctx, cast<PackExpansionExpr>(E)->getPattern());

  case Expr::MaterializeTemporaryExprClass:
    return cast<MaterializeTemporaryExpr>(E)->isBoundToLvalueReference()
              ? Cl::CL_LValue
              : Cl::CL_XValue;

  case Expr::InitListExprClass:
    // An init list can be an lvalue if it is bound to a reference and
    // contains only one element. In that case, we look at that element
    // for an exact classification. Init list creation takes care of the
    // value kind for us, so we only need to fine-tune.
    if (E->isPRValue())
      return ClassifyExprValueKind(Lang, E, E->getValueKind());
    assert(cast<InitListExpr>(E)->getNumInits() == 1 &&
           "Only 1-element init lists can be glvalues.");
    return ClassifyInternal(Ctx, cast<InitListExpr>(E)->getInit(0));

  case Expr::CoawaitExprClass:
  case Expr::CoyieldExprClass:
    return ClassifyInternal(Ctx, cast<CoroutineSuspendExpr>(E)->getResumeExpr());
  case Expr::SYCLUniqueStableNameExprClass:
    return Cl::CL_PRValue;
    break;

  case Expr::CXXParenListInitExprClass:
    if (isa<ArrayType>(E->getType()))
      return Cl::CL_ArrayTemporary;
    return Cl::CL_ClassTemporary;
  }

  llvm_unreachable("unhandled expression kind in classification");
}

/// ClassifyDecl - Return the classification of an expression referencing the
/// given declaration.
static Cl::Kinds ClassifyDecl(ASTContext &Ctx, const Decl *D) {
  // C++ [expr.prim.general]p6: The result is an lvalue if the entity is a
  //   function, variable, or data member and a prvalue otherwise.
  // In C, functions are not lvalues.
  // In addition, NonTypeTemplateParmDecl derives from VarDecl but isn't an
  // lvalue unless it's a reference type (C++ [temp.param]p6), so we need to
  // special-case this.

  if (const auto *M = dyn_cast<CXXMethodDecl>(D)) {
    if (M->isImplicitObjectMemberFunction())
      return Cl::CL_MemberFunction;
    if (M->isStatic())
      return Cl::CL_LValue;
    return Cl::CL_PRValue;
  }

  bool islvalue;
  if (const auto *NTTParm = dyn_cast<NonTypeTemplateParmDecl>(D))
    islvalue = NTTParm->getType()->isReferenceType() ||
               NTTParm->getType()->isRecordType();
  else
    islvalue =
        isa<VarDecl, FieldDecl, IndirectFieldDecl, BindingDecl, MSGuidDecl,
            UnnamedGlobalConstantDecl, TemplateParamObjectDecl>(D) ||
        (Ctx.getLangOpts().CPlusPlus &&
         (isa<FunctionDecl, MSPropertyDecl, FunctionTemplateDecl>(D)));

  return islvalue ? Cl::CL_LValue : Cl::CL_PRValue;
}

/// ClassifyUnnamed - Return the classification of an expression yielding an
/// unnamed value of the given type. This applies in particular to function
/// calls and casts.
static Cl::Kinds ClassifyUnnamed(ASTContext &Ctx, QualType T) {
  // In C, function calls are always rvalues.
  if (!Ctx.getLangOpts().CPlusPlus) return Cl::CL_PRValue;

  // C++ [expr.call]p10: A function call is an lvalue if the result type is an
  //   lvalue reference type or an rvalue reference to function type, an xvalue
  //   if the result type is an rvalue reference to object type, and a prvalue
  //   otherwise.
  if (T->isLValueReferenceType())
    return Cl::CL_LValue;
  const auto *RV = T->getAs<RValueReferenceType>();
  if (!RV) // Could still be a class temporary, though.
    return ClassifyTemporary(T);

  return RV->getPointeeType()->isFunctionType() ? Cl::CL_LValue : Cl::CL_XValue;
}

static Cl::Kinds ClassifyMemberExpr(ASTContext &Ctx, const MemberExpr *E) {
  if (E->getType() == Ctx.UnknownAnyTy)
    return (isa<FunctionDecl>(E->getMemberDecl())
              ? Cl::CL_PRValue : Cl::CL_LValue);

  // Handle C first, it's easier.
  if (!Ctx.getLangOpts().CPlusPlus) {
    // C99 6.5.2.3p3
    // For dot access, the expression is an lvalue if the first part is. For
    // arrow access, it always is an lvalue.
    if (E->isArrow())
      return Cl::CL_LValue;
    // ObjC property accesses are not lvalues, but get special treatment.
    Expr *Base = E->getBase()->IgnoreParens();
    if (isa<ObjCPropertyRefExpr>(Base))
      return Cl::CL_SubObjCPropertySetting;
    return ClassifyInternal(Ctx, Base);
  }

  NamedDecl *Member = E->getMemberDecl();
  // C++ [expr.ref]p3: E1->E2 is converted to the equivalent form (*(E1)).E2.
  // C++ [expr.ref]p4: If E2 is declared to have type "reference to T", then
  //   E1.E2 is an lvalue.
  if (const auto *Value = dyn_cast<ValueDecl>(Member))
    if (Value->getType()->isReferenceType())
      return Cl::CL_LValue;

  //   Otherwise, one of the following rules applies.
  //   -- If E2 is a static member [...] then E1.E2 is an lvalue.
  if (isa<VarDecl>(Member) && Member->getDeclContext()->isRecord())
    return Cl::CL_LValue;

  //   -- If E2 is a non-static data member [...]. If E1 is an lvalue, then
  //      E1.E2 is an lvalue; if E1 is an xvalue, then E1.E2 is an xvalue;
  //      otherwise, it is a prvalue.
  if (isa<FieldDecl>(Member)) {
    // *E1 is an lvalue
    if (E->isArrow())
      return Cl::CL_LValue;
    Expr *Base = E->getBase()->IgnoreParenImpCasts();
    if (isa<ObjCPropertyRefExpr>(Base))
      return Cl::CL_SubObjCPropertySetting;
    return ClassifyInternal(Ctx, E->getBase());
  }

  //   -- If E2 is a [...] member function, [...]
  //      -- If it refers to a static member function [...], then E1.E2 is an
  //         lvalue; [...]
  //      -- Otherwise [...] E1.E2 is a prvalue.
  if (const auto *Method = dyn_cast<CXXMethodDecl>(Member)) {
    if (Method->isStatic())
      return Cl::CL_LValue;
    if (Method->isImplicitObjectMemberFunction())
      return Cl::CL_MemberFunction;
    return Cl::CL_PRValue;
  }

  //   -- If E2 is a member enumerator [...], the expression E1.E2 is a prvalue.
  // So is everything else we haven't handled yet.
  return Cl::CL_PRValue;
}

static Cl::Kinds ClassifyBinaryOp(ASTContext &Ctx, const BinaryOperator *E) {
  assert(Ctx.getLangOpts().CPlusPlus &&
         "This is only relevant for C++.");
  // C++ [expr.ass]p1: All [...] return an lvalue referring to the left operand.
  // Except we override this for writes to ObjC properties.
  if (E->isAssignmentOp())
    return (E->getLHS()->getObjectKind() == OK_ObjCProperty
              ? Cl::CL_PRValue : Cl::CL_LValue);

  // C++ [expr.comma]p1: the result is of the same value category as its right
  //   operand, [...].
  if (E->getOpcode() == BO_Comma)
    return ClassifyInternal(Ctx, E->getRHS());

  // C++ [expr.mptr.oper]p6: The result of a .* expression whose second operand
  //   is a pointer to a data member is of the same value category as its first
  //   operand.
  if (E->getOpcode() == BO_PtrMemD)
    return (E->getType()->isFunctionType() ||
            E->hasPlaceholderType(BuiltinType::BoundMember))
             ? Cl::CL_MemberFunction
             : ClassifyInternal(Ctx, E->getLHS());

  // C++ [expr.mptr.oper]p6: The result of an ->* expression is an lvalue if its
  //   second operand is a pointer to data member and a prvalue otherwise.
  if (E->getOpcode() == BO_PtrMemI)
    return (E->getType()->isFunctionType() ||
            E->hasPlaceholderType(BuiltinType::BoundMember))
             ? Cl::CL_MemberFunction
             : Cl::CL_LValue;

  // All other binary operations are prvalues.
  return Cl::CL_PRValue;
}

static Cl::Kinds ClassifyConditional(ASTContext &Ctx, const Expr *True,
                                     const Expr *False) {
  assert(Ctx.getLangOpts().CPlusPlus &&
         "This is only relevant for C++.");

  // C++ [expr.cond]p2
  //   If either the second or the third operand has type (cv) void,
  //   one of the following shall hold:
  if (True->getType()->isVoidType() || False->getType()->isVoidType()) {
    // The second or the third operand (but not both) is a (possibly
    // parenthesized) throw-expression; the result is of the [...] value
    // category of the other.
    bool TrueIsThrow = isa<CXXThrowExpr>(True->IgnoreParenImpCasts());
    bool FalseIsThrow = isa<CXXThrowExpr>(False->IgnoreParenImpCasts());
    if (const Expr *NonThrow = TrueIsThrow ? (FalseIsThrow ? nullptr : False)
                                           : (FalseIsThrow ? True : nullptr))
      return ClassifyInternal(Ctx, NonThrow);

    //   [Otherwise] the result [...] is a prvalue.
    return Cl::CL_PRValue;
  }

  // Note that at this point, we have already performed all conversions
  // according to [expr.cond]p3.
  // C++ [expr.cond]p4: If the second and third operands are glvalues of the
  //   same value category [...], the result is of that [...] value category.
  // C++ [expr.cond]p5: Otherwise, the result is a prvalue.
  Cl::Kinds LCl = ClassifyInternal(Ctx, True),
            RCl = ClassifyInternal(Ctx, False);
  return LCl == RCl ? LCl : Cl::CL_PRValue;
}

static Cl::ModifiableType IsModifiable(ASTContext &Ctx, const Expr *E,
                                       Cl::Kinds Kind, SourceLocation &Loc) {
  // As a general rule, we only care about lvalues. But there are some rvalues
  // for which we want to generate special results.
  if (Kind == Cl::CL_PRValue) {
    // For the sake of better diagnostics, we want to specifically recognize
    // use of the GCC cast-as-lvalue extension.
    if (const auto *CE = dyn_cast<ExplicitCastExpr>(E->IgnoreParens())) {
      if (CE->getSubExpr()->IgnoreParenImpCasts()->isLValue()) {
        Loc = CE->getExprLoc();
        return Cl::CM_LValueCast;
      }
    }
  }
  if (Kind != Cl::CL_LValue)
    return Cl::CM_RValue;

  // This is the lvalue case.
  // Functions are lvalues in C++, but not modifiable. (C++ [basic.lval]p6)
  if (Ctx.getLangOpts().CPlusPlus && E->getType()->isFunctionType())
    return Cl::CM_Function;

  // Assignment to a property in ObjC is an implicit setter access. But a
  // setter might not exist.
  if (const auto *Expr = dyn_cast<ObjCPropertyRefExpr>(E)) {
    if (Expr->isImplicitProperty() &&
        Expr->getImplicitPropertySetter() == nullptr)
      return Cl::CM_NoSetterProperty;
  }

  CanQualType CT = Ctx.getCanonicalType(E->getType());
  // Const stuff is obviously not modifiable.
  if (CT.isConstQualified())
    return Cl::CM_ConstQualified;
  if (Ctx.getLangOpts().OpenCL &&
      CT.getQualifiers().getAddressSpace() == LangAS::opencl_constant)
    return Cl::CM_ConstAddrSpace;

  // Arrays are not modifiable, only their elements are.
  if (CT->isArrayType())
    return Cl::CM_ArrayType;
  // Incomplete types are not modifiable.
  if (CT->isIncompleteType())
    return Cl::CM_IncompleteType;

  // Records with any const fields (recursively) are not modifiable.
  if (const RecordType *R = CT->getAs<RecordType>())
    if (R->hasConstFields())
      return Cl::CM_ConstQualifiedField;

  return Cl::CM_Modifiable;
}

Expr::LValueClassification Expr::ClassifyLValue(ASTContext &Ctx) const {
  Classification VC = Classify(Ctx);
  switch (VC.getKind()) {
  case Cl::CL_LValue: return LV_Valid;
  case Cl::CL_XValue: return LV_InvalidExpression;
  case Cl::CL_Function: return LV_NotObjectType;
  case Cl::CL_Void: return LV_InvalidExpression;
  case Cl::CL_AddressableVoid: return LV_IncompleteVoidType;
  case Cl::CL_DuplicateVectorComponents: return LV_DuplicateVectorComponents;
  case Cl::CL_MemberFunction: return LV_MemberFunction;
  case Cl::CL_SubObjCPropertySetting: return LV_SubObjCPropertySetting;
  case Cl::CL_ClassTemporary: return LV_ClassTemporary;
  case Cl::CL_ArrayTemporary: return LV_ArrayTemporary;
  case Cl::CL_ObjCMessageRValue: return LV_InvalidMessageExpression;
  case Cl::CL_PRValue: return LV_InvalidExpression;
  }
  llvm_unreachable("Unhandled kind");
}

Expr::isModifiableLvalueResult
Expr::isModifiableLvalue(ASTContext &Ctx, SourceLocation *Loc) const {
  SourceLocation dummy;
  Classification VC = ClassifyModifiable(Ctx, Loc ? *Loc : dummy);
  switch (VC.getKind()) {
  case Cl::CL_LValue: break;
  case Cl::CL_XValue: return MLV_InvalidExpression;
  case Cl::CL_Function: return MLV_NotObjectType;
  case Cl::CL_Void: return MLV_InvalidExpression;
  case Cl::CL_AddressableVoid: return MLV_IncompleteVoidType;
  case Cl::CL_DuplicateVectorComponents: return MLV_DuplicateVectorComponents;
  case Cl::CL_MemberFunction: return MLV_MemberFunction;
  case Cl::CL_SubObjCPropertySetting: return MLV_SubObjCPropertySetting;
  case Cl::CL_ClassTemporary: return MLV_ClassTemporary;
  case Cl::CL_ArrayTemporary: return MLV_ArrayTemporary;
  case Cl::CL_ObjCMessageRValue: return MLV_InvalidMessageExpression;
  case Cl::CL_PRValue:
    return VC.getModifiable() == Cl::CM_LValueCast ?
      MLV_LValueCast : MLV_InvalidExpression;
  }
  assert(VC.getKind() == Cl::CL_LValue && "Unhandled kind");
  switch (VC.getModifiable()) {
  case Cl::CM_Untested: llvm_unreachable("Did not test modifiability");
  case Cl::CM_Modifiable: return MLV_Valid;
  case Cl::CM_RValue: llvm_unreachable("CM_RValue and CL_LValue don't match");
  case Cl::CM_Function: return MLV_NotObjectType;
  case Cl::CM_LValueCast:
    llvm_unreachable("CM_LValueCast and CL_LValue don't match");
  case Cl::CM_NoSetterProperty: return MLV_NoSetterProperty;
  case Cl::CM_ConstQualified: return MLV_ConstQualified;
  case Cl::CM_ConstQualifiedField: return MLV_ConstQualifiedField;
  case Cl::CM_ConstAddrSpace: return MLV_ConstAddrSpace;
  case Cl::CM_ArrayType: return MLV_ArrayType;
  case Cl::CM_IncompleteType: return MLV_IncompleteType;
  }
  llvm_unreachable("Unhandled modifiable type");
}
