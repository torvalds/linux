//===-- SemaConcept.cpp - Semantic Analysis for Constraints and Concepts --===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file implements semantic analysis for C++ constraints and concepts.
//
//===----------------------------------------------------------------------===//

#include "clang/Sema/SemaConcept.h"
#include "TreeTransform.h"
#include "clang/AST/ASTLambda.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/ExprConcepts.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Basic/OperatorPrecedence.h"
#include "clang/Sema/EnterExpressionEvaluationContext.h"
#include "clang/Sema/Initialization.h"
#include "clang/Sema/Overload.h"
#include "clang/Sema/ScopeInfo.h"
#include "clang/Sema/Sema.h"
#include "clang/Sema/SemaDiagnostic.h"
#include "clang/Sema/SemaInternal.h"
#include "clang/Sema/Template.h"
#include "clang/Sema/TemplateDeduction.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/StringExtras.h"
#include <optional>

using namespace clang;
using namespace sema;

namespace {
class LogicalBinOp {
  SourceLocation Loc;
  OverloadedOperatorKind Op = OO_None;
  const Expr *LHS = nullptr;
  const Expr *RHS = nullptr;

public:
  LogicalBinOp(const Expr *E) {
    if (auto *BO = dyn_cast<BinaryOperator>(E)) {
      Op = BinaryOperator::getOverloadedOperator(BO->getOpcode());
      LHS = BO->getLHS();
      RHS = BO->getRHS();
      Loc = BO->getExprLoc();
    } else if (auto *OO = dyn_cast<CXXOperatorCallExpr>(E)) {
      // If OO is not || or && it might not have exactly 2 arguments.
      if (OO->getNumArgs() == 2) {
        Op = OO->getOperator();
        LHS = OO->getArg(0);
        RHS = OO->getArg(1);
        Loc = OO->getOperatorLoc();
      }
    }
  }

  bool isAnd() const { return Op == OO_AmpAmp; }
  bool isOr() const { return Op == OO_PipePipe; }
  explicit operator bool() const { return isAnd() || isOr(); }

  const Expr *getLHS() const { return LHS; }
  const Expr *getRHS() const { return RHS; }
  OverloadedOperatorKind getOp() const { return Op; }

  ExprResult recreateBinOp(Sema &SemaRef, ExprResult LHS) const {
    return recreateBinOp(SemaRef, LHS, const_cast<Expr *>(getRHS()));
  }

  ExprResult recreateBinOp(Sema &SemaRef, ExprResult LHS,
                           ExprResult RHS) const {
    assert((isAnd() || isOr()) && "Not the right kind of op?");
    assert((!LHS.isInvalid() && !RHS.isInvalid()) && "not good expressions?");

    if (!LHS.isUsable() || !RHS.isUsable())
      return ExprEmpty();

    // We should just be able to 'normalize' these to the builtin Binary
    // Operator, since that is how they are evaluated in constriant checks.
    return BinaryOperator::Create(SemaRef.Context, LHS.get(), RHS.get(),
                                  BinaryOperator::getOverloadedOpcode(Op),
                                  SemaRef.Context.BoolTy, VK_PRValue,
                                  OK_Ordinary, Loc, FPOptionsOverride{});
  }
};
}

bool Sema::CheckConstraintExpression(const Expr *ConstraintExpression,
                                     Token NextToken, bool *PossibleNonPrimary,
                                     bool IsTrailingRequiresClause) {
  // C++2a [temp.constr.atomic]p1
  // ..E shall be a constant expression of type bool.

  ConstraintExpression = ConstraintExpression->IgnoreParenImpCasts();

  if (LogicalBinOp BO = ConstraintExpression) {
    return CheckConstraintExpression(BO.getLHS(), NextToken,
                                     PossibleNonPrimary) &&
           CheckConstraintExpression(BO.getRHS(), NextToken,
                                     PossibleNonPrimary);
  } else if (auto *C = dyn_cast<ExprWithCleanups>(ConstraintExpression))
    return CheckConstraintExpression(C->getSubExpr(), NextToken,
                                     PossibleNonPrimary);

  QualType Type = ConstraintExpression->getType();

  auto CheckForNonPrimary = [&] {
    if (!PossibleNonPrimary)
      return;

    *PossibleNonPrimary =
        // We have the following case:
        // template<typename> requires func(0) struct S { };
        // The user probably isn't aware of the parentheses required around
        // the function call, and we're only going to parse 'func' as the
        // primary-expression, and complain that it is of non-bool type.
        //
        // However, if we're in a lambda, this might also be:
        // []<typename> requires var () {};
        // Which also looks like a function call due to the lambda parentheses,
        // but unlike the first case, isn't an error, so this check is skipped.
        (NextToken.is(tok::l_paren) &&
         (IsTrailingRequiresClause ||
          (Type->isDependentType() &&
           isa<UnresolvedLookupExpr>(ConstraintExpression) &&
           !dyn_cast_if_present<LambdaScopeInfo>(getCurFunction())) ||
          Type->isFunctionType() ||
          Type->isSpecificBuiltinType(BuiltinType::Overload))) ||
        // We have the following case:
        // template<typename T> requires size_<T> == 0 struct S { };
        // The user probably isn't aware of the parentheses required around
        // the binary operator, and we're only going to parse 'func' as the
        // first operand, and complain that it is of non-bool type.
        getBinOpPrecedence(NextToken.getKind(),
                           /*GreaterThanIsOperator=*/true,
                           getLangOpts().CPlusPlus11) > prec::LogicalAnd;
  };

  // An atomic constraint!
  if (ConstraintExpression->isTypeDependent()) {
    CheckForNonPrimary();
    return true;
  }

  if (!Context.hasSameUnqualifiedType(Type, Context.BoolTy)) {
    Diag(ConstraintExpression->getExprLoc(),
         diag::err_non_bool_atomic_constraint) << Type
        << ConstraintExpression->getSourceRange();
    CheckForNonPrimary();
    return false;
  }

  if (PossibleNonPrimary)
      *PossibleNonPrimary = false;
  return true;
}

namespace {
struct SatisfactionStackRAII {
  Sema &SemaRef;
  bool Inserted = false;
  SatisfactionStackRAII(Sema &SemaRef, const NamedDecl *ND,
                        const llvm::FoldingSetNodeID &FSNID)
      : SemaRef(SemaRef) {
      if (ND) {
      SemaRef.PushSatisfactionStackEntry(ND, FSNID);
      Inserted = true;
      }
  }
  ~SatisfactionStackRAII() {
        if (Inserted)
          SemaRef.PopSatisfactionStackEntry();
  }
};
} // namespace

template <typename ConstraintEvaluator>
static ExprResult
calculateConstraintSatisfaction(Sema &S, const Expr *ConstraintExpr,
                                ConstraintSatisfaction &Satisfaction,
                                const ConstraintEvaluator &Evaluator);

template <typename ConstraintEvaluator>
static ExprResult
calculateConstraintSatisfaction(Sema &S, const Expr *LHS,
                                OverloadedOperatorKind Op, const Expr *RHS,
                                ConstraintSatisfaction &Satisfaction,
                                const ConstraintEvaluator &Evaluator) {
  size_t EffectiveDetailEndIndex = Satisfaction.Details.size();

  ExprResult LHSRes =
      calculateConstraintSatisfaction(S, LHS, Satisfaction, Evaluator);

  if (LHSRes.isInvalid())
    return ExprError();

  bool IsLHSSatisfied = Satisfaction.IsSatisfied;

  if (Op == clang::OO_PipePipe && IsLHSSatisfied)
    // [temp.constr.op] p3
    //    A disjunction is a constraint taking two operands. To determine if
    //    a disjunction is satisfied, the satisfaction of the first operand
    //    is checked. If that is satisfied, the disjunction is satisfied.
    //    Otherwise, the disjunction is satisfied if and only if the second
    //    operand is satisfied.
    // LHS is instantiated while RHS is not. Skip creating invalid BinaryOp.
    return LHSRes;

  if (Op == clang::OO_AmpAmp && !IsLHSSatisfied)
    // [temp.constr.op] p2
    //    A conjunction is a constraint taking two operands. To determine if
    //    a conjunction is satisfied, the satisfaction of the first operand
    //    is checked. If that is not satisfied, the conjunction is not
    //    satisfied. Otherwise, the conjunction is satisfied if and only if
    //    the second operand is satisfied.
    // LHS is instantiated while RHS is not. Skip creating invalid BinaryOp.
    return LHSRes;

  ExprResult RHSRes =
      calculateConstraintSatisfaction(S, RHS, Satisfaction, Evaluator);
  if (RHSRes.isInvalid())
    return ExprError();

  bool IsRHSSatisfied = Satisfaction.IsSatisfied;
  // Current implementation adds diagnostic information about the falsity
  // of each false atomic constraint expression when it evaluates them.
  // When the evaluation results to `false || true`, the information
  // generated during the evaluation of left-hand side is meaningless
  // because the whole expression evaluates to true.
  // The following code removes the irrelevant diagnostic information.
  // FIXME: We should probably delay the addition of diagnostic information
  // until we know the entire expression is false.
  if (Op == clang::OO_PipePipe && IsRHSSatisfied) {
    auto EffectiveDetailEnd = Satisfaction.Details.begin();
    std::advance(EffectiveDetailEnd, EffectiveDetailEndIndex);
    Satisfaction.Details.erase(EffectiveDetailEnd, Satisfaction.Details.end());
  }

  if (!LHSRes.isUsable() || !RHSRes.isUsable())
    return ExprEmpty();

  return BinaryOperator::Create(S.Context, LHSRes.get(), RHSRes.get(),
                                BinaryOperator::getOverloadedOpcode(Op),
                                S.Context.BoolTy, VK_PRValue, OK_Ordinary,
                                LHS->getBeginLoc(), FPOptionsOverride{});
}

template <typename ConstraintEvaluator>
static ExprResult
calculateConstraintSatisfaction(Sema &S, const CXXFoldExpr *FE,
                                ConstraintSatisfaction &Satisfaction,
                                const ConstraintEvaluator &Evaluator) {
  bool Conjunction = FE->getOperator() == BinaryOperatorKind::BO_LAnd;
  size_t EffectiveDetailEndIndex = Satisfaction.Details.size();

  ExprResult Out;
  if (FE->isLeftFold() && FE->getInit()) {
    Out = calculateConstraintSatisfaction(S, FE->getInit(), Satisfaction,
                                          Evaluator);
    if (Out.isInvalid())
      return ExprError();

    // If the first clause of a conjunction is not satisfied,
    // or if the first clause of a disjection is satisfied,
    // we have established satisfaction of the whole constraint
    // and we should not continue further.
    if (Conjunction != Satisfaction.IsSatisfied)
      return Out;
  }
  std::optional<unsigned> NumExpansions =
      Evaluator.EvaluateFoldExpandedConstraintSize(FE);
  if (!NumExpansions)
    return ExprError();
  for (unsigned I = 0; I < *NumExpansions; I++) {
    Sema::ArgumentPackSubstitutionIndexRAII SubstIndex(S, I);
    ExprResult Res = calculateConstraintSatisfaction(S, FE->getPattern(),
                                                     Satisfaction, Evaluator);
    if (Res.isInvalid())
      return ExprError();
    bool IsRHSSatisfied = Satisfaction.IsSatisfied;
    if (!Conjunction && IsRHSSatisfied) {
      auto EffectiveDetailEnd = Satisfaction.Details.begin();
      std::advance(EffectiveDetailEnd, EffectiveDetailEndIndex);
      Satisfaction.Details.erase(EffectiveDetailEnd,
                                 Satisfaction.Details.end());
    }
    if (Out.isUnset())
      Out = Res;
    else if (!Res.isUnset()) {
      Out = BinaryOperator::Create(
          S.Context, Out.get(), Res.get(), FE->getOperator(), S.Context.BoolTy,
          VK_PRValue, OK_Ordinary, FE->getBeginLoc(), FPOptionsOverride{});
    }
    if (Conjunction != IsRHSSatisfied)
      return Out;
  }

  if (FE->isRightFold() && FE->getInit()) {
    ExprResult Res = calculateConstraintSatisfaction(S, FE->getInit(),
                                                     Satisfaction, Evaluator);
    if (Out.isInvalid())
      return ExprError();

    if (Out.isUnset())
      Out = Res;
    else if (!Res.isUnset()) {
      Out = BinaryOperator::Create(
          S.Context, Out.get(), Res.get(), FE->getOperator(), S.Context.BoolTy,
          VK_PRValue, OK_Ordinary, FE->getBeginLoc(), FPOptionsOverride{});
    }
  }

  if (Out.isUnset()) {
    Satisfaction.IsSatisfied = Conjunction;
    Out = S.BuildEmptyCXXFoldExpr(FE->getBeginLoc(), FE->getOperator());
  }
  return Out;
}

template <typename ConstraintEvaluator>
static ExprResult
calculateConstraintSatisfaction(Sema &S, const Expr *ConstraintExpr,
                                ConstraintSatisfaction &Satisfaction,
                                const ConstraintEvaluator &Evaluator) {
  ConstraintExpr = ConstraintExpr->IgnoreParenImpCasts();

  if (LogicalBinOp BO = ConstraintExpr)
    return calculateConstraintSatisfaction(
        S, BO.getLHS(), BO.getOp(), BO.getRHS(), Satisfaction, Evaluator);

  if (auto *C = dyn_cast<ExprWithCleanups>(ConstraintExpr)) {
    // These aren't evaluated, so we don't care about cleanups, so we can just
    // evaluate these as if the cleanups didn't exist.
    return calculateConstraintSatisfaction(S, C->getSubExpr(), Satisfaction,
                                           Evaluator);
  }

  if (auto *FE = dyn_cast<CXXFoldExpr>(ConstraintExpr);
      FE && S.getLangOpts().CPlusPlus26 &&
      (FE->getOperator() == BinaryOperatorKind::BO_LAnd ||
       FE->getOperator() == BinaryOperatorKind::BO_LOr)) {
    return calculateConstraintSatisfaction(S, FE, Satisfaction, Evaluator);
  }

  // An atomic constraint expression
  ExprResult SubstitutedAtomicExpr =
      Evaluator.EvaluateAtomicConstraint(ConstraintExpr);

  if (SubstitutedAtomicExpr.isInvalid())
    return ExprError();

  if (!SubstitutedAtomicExpr.isUsable())
    // Evaluator has decided satisfaction without yielding an expression.
    return ExprEmpty();

  // We don't have the ability to evaluate this, since it contains a
  // RecoveryExpr, so we want to fail overload resolution.  Otherwise,
  // we'd potentially pick up a different overload, and cause confusing
  // diagnostics. SO, add a failure detail that will cause us to make this
  // overload set not viable.
  if (SubstitutedAtomicExpr.get()->containsErrors()) {
    Satisfaction.IsSatisfied = false;
    Satisfaction.ContainsErrors = true;

    PartialDiagnostic Msg = S.PDiag(diag::note_constraint_references_error);
    SmallString<128> DiagString;
    DiagString = ": ";
    Msg.EmitToString(S.getDiagnostics(), DiagString);
    unsigned MessageSize = DiagString.size();
    char *Mem = new (S.Context) char[MessageSize];
    memcpy(Mem, DiagString.c_str(), MessageSize);
    Satisfaction.Details.emplace_back(
        new (S.Context) ConstraintSatisfaction::SubstitutionDiagnostic{
            SubstitutedAtomicExpr.get()->getBeginLoc(),
            StringRef(Mem, MessageSize)});
    return SubstitutedAtomicExpr;
  }

  EnterExpressionEvaluationContext ConstantEvaluated(
      S, Sema::ExpressionEvaluationContext::ConstantEvaluated);
  SmallVector<PartialDiagnosticAt, 2> EvaluationDiags;
  Expr::EvalResult EvalResult;
  EvalResult.Diag = &EvaluationDiags;
  if (!SubstitutedAtomicExpr.get()->EvaluateAsConstantExpr(EvalResult,
                                                           S.Context) ||
      !EvaluationDiags.empty()) {
    // C++2a [temp.constr.atomic]p1
    //   ...E shall be a constant expression of type bool.
    S.Diag(SubstitutedAtomicExpr.get()->getBeginLoc(),
           diag::err_non_constant_constraint_expression)
        << SubstitutedAtomicExpr.get()->getSourceRange();
    for (const PartialDiagnosticAt &PDiag : EvaluationDiags)
      S.Diag(PDiag.first, PDiag.second);
    return ExprError();
  }

  assert(EvalResult.Val.isInt() &&
         "evaluating bool expression didn't produce int");
  Satisfaction.IsSatisfied = EvalResult.Val.getInt().getBoolValue();
  if (!Satisfaction.IsSatisfied)
    Satisfaction.Details.emplace_back(SubstitutedAtomicExpr.get());

  return SubstitutedAtomicExpr;
}

static bool
DiagRecursiveConstraintEval(Sema &S, llvm::FoldingSetNodeID &ID,
                            const NamedDecl *Templ, const Expr *E,
                            const MultiLevelTemplateArgumentList &MLTAL) {
  E->Profile(ID, S.Context, /*Canonical=*/true);
  for (const auto &List : MLTAL)
    for (const auto &TemplateArg : List.Args)
      TemplateArg.Profile(ID, S.Context);

  // Note that we have to do this with our own collection, because there are
  // times where a constraint-expression check can cause us to need to evaluate
  // other constriants that are unrelated, such as when evaluating a recovery
  // expression, or when trying to determine the constexpr-ness of special
  // members. Otherwise we could just use the
  // Sema::InstantiatingTemplate::isAlreadyBeingInstantiated function.
  if (S.SatisfactionStackContains(Templ, ID)) {
    S.Diag(E->getExprLoc(), diag::err_constraint_depends_on_self)
        << const_cast<Expr *>(E) << E->getSourceRange();
    return true;
  }

  return false;
}

static ExprResult calculateConstraintSatisfaction(
    Sema &S, const NamedDecl *Template, SourceLocation TemplateNameLoc,
    const MultiLevelTemplateArgumentList &MLTAL, const Expr *ConstraintExpr,
    ConstraintSatisfaction &Satisfaction) {

  struct ConstraintEvaluator {
    Sema &S;
    const NamedDecl *Template;
    SourceLocation TemplateNameLoc;
    const MultiLevelTemplateArgumentList &MLTAL;
    ConstraintSatisfaction &Satisfaction;

    ExprResult EvaluateAtomicConstraint(const Expr *AtomicExpr) const {
      EnterExpressionEvaluationContext ConstantEvaluated(
          S, Sema::ExpressionEvaluationContext::ConstantEvaluated,
          Sema::ReuseLambdaContextDecl);

      // Atomic constraint - substitute arguments and check satisfaction.
      ExprResult SubstitutedExpression;
      {
        TemplateDeductionInfo Info(TemplateNameLoc);
        Sema::InstantiatingTemplate Inst(
            S, AtomicExpr->getBeginLoc(),
            Sema::InstantiatingTemplate::ConstraintSubstitution{},
            const_cast<NamedDecl *>(Template), Info,
            AtomicExpr->getSourceRange());
        if (Inst.isInvalid())
          return ExprError();

        llvm::FoldingSetNodeID ID;
        if (Template &&
            DiagRecursiveConstraintEval(S, ID, Template, AtomicExpr, MLTAL)) {
          Satisfaction.IsSatisfied = false;
          Satisfaction.ContainsErrors = true;
          return ExprEmpty();
        }

        SatisfactionStackRAII StackRAII(S, Template, ID);

        // We do not want error diagnostics escaping here.
        Sema::SFINAETrap Trap(S);
        SubstitutedExpression =
            S.SubstConstraintExpr(const_cast<Expr *>(AtomicExpr), MLTAL);

        if (SubstitutedExpression.isInvalid() || Trap.hasErrorOccurred()) {
          // C++2a [temp.constr.atomic]p1
          //   ...If substitution results in an invalid type or expression, the
          //   constraint is not satisfied.
          if (!Trap.hasErrorOccurred())
            // A non-SFINAE error has occurred as a result of this
            // substitution.
            return ExprError();

          PartialDiagnosticAt SubstDiag{SourceLocation(),
                                        PartialDiagnostic::NullDiagnostic()};
          Info.takeSFINAEDiagnostic(SubstDiag);
          // FIXME: Concepts: This is an unfortunate consequence of there
          //  being no serialization code for PartialDiagnostics and the fact
          //  that serializing them would likely take a lot more storage than
          //  just storing them as strings. We would still like, in the
          //  future, to serialize the proper PartialDiagnostic as serializing
          //  it as a string defeats the purpose of the diagnostic mechanism.
          SmallString<128> DiagString;
          DiagString = ": ";
          SubstDiag.second.EmitToString(S.getDiagnostics(), DiagString);
          unsigned MessageSize = DiagString.size();
          char *Mem = new (S.Context) char[MessageSize];
          memcpy(Mem, DiagString.c_str(), MessageSize);
          Satisfaction.Details.emplace_back(
              new (S.Context) ConstraintSatisfaction::SubstitutionDiagnostic{
                  SubstDiag.first, StringRef(Mem, MessageSize)});
          Satisfaction.IsSatisfied = false;
          return ExprEmpty();
        }
      }

      if (!S.CheckConstraintExpression(SubstitutedExpression.get()))
        return ExprError();

      // [temp.constr.atomic]p3: To determine if an atomic constraint is
      // satisfied, the parameter mapping and template arguments are first
      // substituted into its expression.  If substitution results in an
      // invalid type or expression, the constraint is not satisfied.
      // Otherwise, the lvalue-to-rvalue conversion is performed if necessary,
      // and E shall be a constant expression of type bool.
      //
      // Perform the L to R Value conversion if necessary. We do so for all
      // non-PRValue categories, else we fail to extend the lifetime of
      // temporaries, and that fails the constant expression check.
      if (!SubstitutedExpression.get()->isPRValue())
        SubstitutedExpression = ImplicitCastExpr::Create(
            S.Context, SubstitutedExpression.get()->getType(),
            CK_LValueToRValue, SubstitutedExpression.get(),
            /*BasePath=*/nullptr, VK_PRValue, FPOptionsOverride());

      return SubstitutedExpression;
    }

    std::optional<unsigned>
    EvaluateFoldExpandedConstraintSize(const CXXFoldExpr *FE) const {

      // We should ignore errors in the presence of packs of different size.
      Sema::SFINAETrap Trap(S);

      Expr *Pattern = FE->getPattern();

      SmallVector<UnexpandedParameterPack, 2> Unexpanded;
      S.collectUnexpandedParameterPacks(Pattern, Unexpanded);
      assert(!Unexpanded.empty() && "Pack expansion without parameter packs?");
      bool Expand = true;
      bool RetainExpansion = false;
      std::optional<unsigned> OrigNumExpansions = FE->getNumExpansions(),
                              NumExpansions = OrigNumExpansions;
      if (S.CheckParameterPacksForExpansion(
              FE->getEllipsisLoc(), Pattern->getSourceRange(), Unexpanded,
              MLTAL, Expand, RetainExpansion, NumExpansions) ||
          !Expand || RetainExpansion)
        return std::nullopt;

      if (NumExpansions && S.getLangOpts().BracketDepth < NumExpansions) {
        S.Diag(FE->getEllipsisLoc(),
               clang::diag::err_fold_expression_limit_exceeded)
            << *NumExpansions << S.getLangOpts().BracketDepth
            << FE->getSourceRange();
        S.Diag(FE->getEllipsisLoc(), diag::note_bracket_depth);
        return std::nullopt;
      }
      return NumExpansions;
    }
  };

  return calculateConstraintSatisfaction(
      S, ConstraintExpr, Satisfaction,
      ConstraintEvaluator{S, Template, TemplateNameLoc, MLTAL, Satisfaction});
}

static bool CheckConstraintSatisfaction(
    Sema &S, const NamedDecl *Template, ArrayRef<const Expr *> ConstraintExprs,
    llvm::SmallVectorImpl<Expr *> &Converted,
    const MultiLevelTemplateArgumentList &TemplateArgsLists,
    SourceRange TemplateIDRange, ConstraintSatisfaction &Satisfaction) {
  if (ConstraintExprs.empty()) {
    Satisfaction.IsSatisfied = true;
    return false;
  }

  if (TemplateArgsLists.isAnyArgInstantiationDependent()) {
    // No need to check satisfaction for dependent constraint expressions.
    Satisfaction.IsSatisfied = true;
    return false;
  }

  ArrayRef<TemplateArgument> TemplateArgs =
      TemplateArgsLists.getNumSubstitutedLevels() > 0
          ? TemplateArgsLists.getOutermost()
          : ArrayRef<TemplateArgument> {};
  Sema::InstantiatingTemplate Inst(S, TemplateIDRange.getBegin(),
      Sema::InstantiatingTemplate::ConstraintsCheck{},
      const_cast<NamedDecl *>(Template), TemplateArgs, TemplateIDRange);
  if (Inst.isInvalid())
    return true;

  for (const Expr *ConstraintExpr : ConstraintExprs) {
    ExprResult Res = calculateConstraintSatisfaction(
        S, Template, TemplateIDRange.getBegin(), TemplateArgsLists,
        ConstraintExpr, Satisfaction);
    if (Res.isInvalid())
      return true;

    Converted.push_back(Res.get());
    if (!Satisfaction.IsSatisfied) {
      // Backfill the 'converted' list with nulls so we can keep the Converted
      // and unconverted lists in sync.
      Converted.append(ConstraintExprs.size() - Converted.size(), nullptr);
      // [temp.constr.op] p2
      // [...] To determine if a conjunction is satisfied, the satisfaction
      // of the first operand is checked. If that is not satisfied, the
      // conjunction is not satisfied. [...]
      return false;
    }
  }
  return false;
}

bool Sema::CheckConstraintSatisfaction(
    const NamedDecl *Template, ArrayRef<const Expr *> ConstraintExprs,
    llvm::SmallVectorImpl<Expr *> &ConvertedConstraints,
    const MultiLevelTemplateArgumentList &TemplateArgsLists,
    SourceRange TemplateIDRange, ConstraintSatisfaction &OutSatisfaction) {
  if (ConstraintExprs.empty()) {
    OutSatisfaction.IsSatisfied = true;
    return false;
  }
  if (!Template) {
    return ::CheckConstraintSatisfaction(
        *this, nullptr, ConstraintExprs, ConvertedConstraints,
        TemplateArgsLists, TemplateIDRange, OutSatisfaction);
  }
  // Invalid templates could make their way here. Substituting them could result
  // in dependent expressions.
  if (Template->isInvalidDecl()) {
    OutSatisfaction.IsSatisfied = false;
    return true;
  }

  // A list of the template argument list flattened in a predictible manner for
  // the purposes of caching. The ConstraintSatisfaction type is in AST so it
  // has no access to the MultiLevelTemplateArgumentList, so this has to happen
  // here.
  llvm::SmallVector<TemplateArgument, 4> FlattenedArgs;
  for (auto List : TemplateArgsLists)
    FlattenedArgs.insert(FlattenedArgs.end(), List.Args.begin(),
                         List.Args.end());

  llvm::FoldingSetNodeID ID;
  ConstraintSatisfaction::Profile(ID, Context, Template, FlattenedArgs);
  void *InsertPos;
  if (auto *Cached = SatisfactionCache.FindNodeOrInsertPos(ID, InsertPos)) {
    OutSatisfaction = *Cached;
    return false;
  }

  auto Satisfaction =
      std::make_unique<ConstraintSatisfaction>(Template, FlattenedArgs);
  if (::CheckConstraintSatisfaction(*this, Template, ConstraintExprs,
                                    ConvertedConstraints, TemplateArgsLists,
                                    TemplateIDRange, *Satisfaction)) {
    OutSatisfaction = *Satisfaction;
    return true;
  }

  if (auto *Cached = SatisfactionCache.FindNodeOrInsertPos(ID, InsertPos)) {
    // The evaluation of this constraint resulted in us trying to re-evaluate it
    // recursively. This isn't really possible, except we try to form a
    // RecoveryExpr as a part of the evaluation.  If this is the case, just
    // return the 'cached' version (which will have the same result), and save
    // ourselves the extra-insert. If it ever becomes possible to legitimately
    // recursively check a constraint, we should skip checking the 'inner' one
    // above, and replace the cached version with this one, as it would be more
    // specific.
    OutSatisfaction = *Cached;
    return false;
  }

  // Else we can simply add this satisfaction to the list.
  OutSatisfaction = *Satisfaction;
  // We cannot use InsertPos here because CheckConstraintSatisfaction might have
  // invalidated it.
  // Note that entries of SatisfactionCache are deleted in Sema's destructor.
  SatisfactionCache.InsertNode(Satisfaction.release());
  return false;
}

bool Sema::CheckConstraintSatisfaction(const Expr *ConstraintExpr,
                                       ConstraintSatisfaction &Satisfaction) {

  struct ConstraintEvaluator {
    Sema &S;
    ExprResult EvaluateAtomicConstraint(const Expr *AtomicExpr) const {
      return S.PerformContextuallyConvertToBool(const_cast<Expr *>(AtomicExpr));
    }

    std::optional<unsigned>
    EvaluateFoldExpandedConstraintSize(const CXXFoldExpr *FE) const {
      return 0;
    }
  };

  return calculateConstraintSatisfaction(*this, ConstraintExpr, Satisfaction,
                                         ConstraintEvaluator{*this})
      .isInvalid();
}

bool Sema::addInstantiatedCapturesToScope(
    FunctionDecl *Function, const FunctionDecl *PatternDecl,
    LocalInstantiationScope &Scope,
    const MultiLevelTemplateArgumentList &TemplateArgs) {
  const auto *LambdaClass = cast<CXXMethodDecl>(Function)->getParent();
  const auto *LambdaPattern = cast<CXXMethodDecl>(PatternDecl)->getParent();

  unsigned Instantiated = 0;

  auto AddSingleCapture = [&](const ValueDecl *CapturedPattern,
                              unsigned Index) {
    ValueDecl *CapturedVar = LambdaClass->getCapture(Index)->getCapturedVar();
    if (CapturedVar->isInitCapture())
      Scope.InstantiatedLocal(CapturedPattern, CapturedVar);
  };

  for (const LambdaCapture &CapturePattern : LambdaPattern->captures()) {
    if (!CapturePattern.capturesVariable()) {
      Instantiated++;
      continue;
    }
    const ValueDecl *CapturedPattern = CapturePattern.getCapturedVar();
    if (!CapturedPattern->isParameterPack()) {
      AddSingleCapture(CapturedPattern, Instantiated++);
    } else {
      Scope.MakeInstantiatedLocalArgPack(CapturedPattern);
      std::optional<unsigned> NumArgumentsInExpansion =
          getNumArgumentsInExpansion(CapturedPattern->getType(), TemplateArgs);
      if (!NumArgumentsInExpansion)
        continue;
      for (unsigned Arg = 0; Arg < *NumArgumentsInExpansion; ++Arg)
        AddSingleCapture(CapturedPattern, Instantiated++);
    }
  }
  return false;
}

bool Sema::SetupConstraintScope(
    FunctionDecl *FD, std::optional<ArrayRef<TemplateArgument>> TemplateArgs,
    const MultiLevelTemplateArgumentList &MLTAL,
    LocalInstantiationScope &Scope) {
  if (FD->isTemplateInstantiation() && FD->getPrimaryTemplate()) {
    FunctionTemplateDecl *PrimaryTemplate = FD->getPrimaryTemplate();
    InstantiatingTemplate Inst(
        *this, FD->getPointOfInstantiation(),
        Sema::InstantiatingTemplate::ConstraintsCheck{}, PrimaryTemplate,
        TemplateArgs ? *TemplateArgs : ArrayRef<TemplateArgument>{},
        SourceRange());
    if (Inst.isInvalid())
      return true;

    // addInstantiatedParametersToScope creates a map of 'uninstantiated' to
    // 'instantiated' parameters and adds it to the context. For the case where
    // this function is a template being instantiated NOW, we also need to add
    // the list of current template arguments to the list so that they also can
    // be picked out of the map.
    if (auto *SpecArgs = FD->getTemplateSpecializationArgs()) {
      MultiLevelTemplateArgumentList JustTemplArgs(FD, SpecArgs->asArray(),
                                                   /*Final=*/false);
      if (addInstantiatedParametersToScope(
              FD, PrimaryTemplate->getTemplatedDecl(), Scope, JustTemplArgs))
        return true;
    }

    // If this is a member function, make sure we get the parameters that
    // reference the original primary template.
    // We walk up the instantiated template chain so that nested lambdas get
    // handled properly.
    // We should only collect instantiated parameters from the primary template.
    // Otherwise, we may have mismatched template parameter depth!
    if (FunctionTemplateDecl *FromMemTempl =
            PrimaryTemplate->getInstantiatedFromMemberTemplate()) {
      while (FromMemTempl->getInstantiatedFromMemberTemplate())
        FromMemTempl = FromMemTempl->getInstantiatedFromMemberTemplate();
      if (addInstantiatedParametersToScope(FD, FromMemTempl->getTemplatedDecl(),
                                           Scope, MLTAL))
        return true;
    }

    return false;
  }

  if (FD->getTemplatedKind() == FunctionDecl::TK_MemberSpecialization ||
      FD->getTemplatedKind() == FunctionDecl::TK_DependentNonTemplate) {
    FunctionDecl *InstantiatedFrom =
        FD->getTemplatedKind() == FunctionDecl::TK_MemberSpecialization
            ? FD->getInstantiatedFromMemberFunction()
            : FD->getInstantiatedFromDecl();

    InstantiatingTemplate Inst(
        *this, FD->getPointOfInstantiation(),
        Sema::InstantiatingTemplate::ConstraintsCheck{}, InstantiatedFrom,
        TemplateArgs ? *TemplateArgs : ArrayRef<TemplateArgument>{},
        SourceRange());
    if (Inst.isInvalid())
      return true;

    // Case where this was not a template, but instantiated as a
    // child-function.
    if (addInstantiatedParametersToScope(FD, InstantiatedFrom, Scope, MLTAL))
      return true;
  }

  return false;
}

// This function collects all of the template arguments for the purposes of
// constraint-instantiation and checking.
std::optional<MultiLevelTemplateArgumentList>
Sema::SetupConstraintCheckingTemplateArgumentsAndScope(
    FunctionDecl *FD, std::optional<ArrayRef<TemplateArgument>> TemplateArgs,
    LocalInstantiationScope &Scope) {
  MultiLevelTemplateArgumentList MLTAL;

  // Collect the list of template arguments relative to the 'primary' template.
  // We need the entire list, since the constraint is completely uninstantiated
  // at this point.
  MLTAL =
      getTemplateInstantiationArgs(FD, FD->getLexicalDeclContext(),
                                   /*Final=*/false, /*Innermost=*/std::nullopt,
                                   /*RelativeToPrimary=*/true,
                                   /*Pattern=*/nullptr,
                                   /*ForConstraintInstantiation=*/true);
  if (SetupConstraintScope(FD, TemplateArgs, MLTAL, Scope))
    return std::nullopt;

  return MLTAL;
}

bool Sema::CheckFunctionConstraints(const FunctionDecl *FD,
                                    ConstraintSatisfaction &Satisfaction,
                                    SourceLocation UsageLoc,
                                    bool ForOverloadResolution) {
  // Don't check constraints if the function is dependent. Also don't check if
  // this is a function template specialization, as the call to
  // CheckinstantiatedFunctionTemplateConstraints after this will check it
  // better.
  if (FD->isDependentContext() ||
      FD->getTemplatedKind() ==
          FunctionDecl::TK_FunctionTemplateSpecialization) {
    Satisfaction.IsSatisfied = true;
    return false;
  }

  // A lambda conversion operator has the same constraints as the call operator
  // and constraints checking relies on whether we are in a lambda call operator
  // (and may refer to its parameters), so check the call operator instead.
  // Note that the declarations outside of the lambda should also be
  // considered. Turning on the 'ForOverloadResolution' flag results in the
  // LocalInstantiationScope not looking into its parents, but we can still
  // access Decls from the parents while building a lambda RAII scope later.
  if (const auto *MD = dyn_cast<CXXConversionDecl>(FD);
      MD && isLambdaConversionOperator(const_cast<CXXConversionDecl *>(MD)))
    return CheckFunctionConstraints(MD->getParent()->getLambdaCallOperator(),
                                    Satisfaction, UsageLoc,
                                    /*ShouldAddDeclsFromParentScope=*/true);

  DeclContext *CtxToSave = const_cast<FunctionDecl *>(FD);

  while (isLambdaCallOperator(CtxToSave) || FD->isTransparentContext()) {
    if (isLambdaCallOperator(CtxToSave))
      CtxToSave = CtxToSave->getParent()->getParent();
    else
      CtxToSave = CtxToSave->getNonTransparentContext();
  }

  ContextRAII SavedContext{*this, CtxToSave};
  LocalInstantiationScope Scope(*this, !ForOverloadResolution);
  std::optional<MultiLevelTemplateArgumentList> MLTAL =
      SetupConstraintCheckingTemplateArgumentsAndScope(
          const_cast<FunctionDecl *>(FD), {}, Scope);

  if (!MLTAL)
    return true;

  Qualifiers ThisQuals;
  CXXRecordDecl *Record = nullptr;
  if (auto *Method = dyn_cast<CXXMethodDecl>(FD)) {
    ThisQuals = Method->getMethodQualifiers();
    Record = const_cast<CXXRecordDecl *>(Method->getParent());
  }
  CXXThisScopeRAII ThisScope(*this, Record, ThisQuals, Record != nullptr);

  LambdaScopeForCallOperatorInstantiationRAII LambdaScope(
      *this, const_cast<FunctionDecl *>(FD), *MLTAL, Scope,
      ForOverloadResolution);

  return CheckConstraintSatisfaction(
      FD, {FD->getTrailingRequiresClause()}, *MLTAL,
      SourceRange(UsageLoc.isValid() ? UsageLoc : FD->getLocation()),
      Satisfaction);
}


// Figure out the to-translation-unit depth for this function declaration for
// the purpose of seeing if they differ by constraints. This isn't the same as
// getTemplateDepth, because it includes already instantiated parents.
static unsigned
CalculateTemplateDepthForConstraints(Sema &S, const NamedDecl *ND,
                                     bool SkipForSpecialization = false) {
  MultiLevelTemplateArgumentList MLTAL = S.getTemplateInstantiationArgs(
      ND, ND->getLexicalDeclContext(), /*Final=*/false,
      /*Innermost=*/std::nullopt,
      /*RelativeToPrimary=*/true,
      /*Pattern=*/nullptr,
      /*ForConstraintInstantiation=*/true, SkipForSpecialization);
  return MLTAL.getNumLevels();
}

namespace {
  class AdjustConstraintDepth : public TreeTransform<AdjustConstraintDepth> {
  unsigned TemplateDepth = 0;
  public:
  using inherited = TreeTransform<AdjustConstraintDepth>;
  AdjustConstraintDepth(Sema &SemaRef, unsigned TemplateDepth)
      : inherited(SemaRef), TemplateDepth(TemplateDepth) {}

  using inherited::TransformTemplateTypeParmType;
  QualType TransformTemplateTypeParmType(TypeLocBuilder &TLB,
                                         TemplateTypeParmTypeLoc TL, bool) {
    const TemplateTypeParmType *T = TL.getTypePtr();

    TemplateTypeParmDecl *NewTTPDecl = nullptr;
    if (TemplateTypeParmDecl *OldTTPDecl = T->getDecl())
      NewTTPDecl = cast_or_null<TemplateTypeParmDecl>(
          TransformDecl(TL.getNameLoc(), OldTTPDecl));

    QualType Result = getSema().Context.getTemplateTypeParmType(
        T->getDepth() + TemplateDepth, T->getIndex(), T->isParameterPack(),
        NewTTPDecl);
    TemplateTypeParmTypeLoc NewTL = TLB.push<TemplateTypeParmTypeLoc>(Result);
    NewTL.setNameLoc(TL.getNameLoc());
    return Result;
  }
  };
} // namespace

static const Expr *SubstituteConstraintExpressionWithoutSatisfaction(
    Sema &S, const Sema::TemplateCompareNewDeclInfo &DeclInfo,
    const Expr *ConstrExpr) {
  MultiLevelTemplateArgumentList MLTAL = S.getTemplateInstantiationArgs(
      DeclInfo.getDecl(), DeclInfo.getLexicalDeclContext(), /*Final=*/false,
      /*Innermost=*/std::nullopt,
      /*RelativeToPrimary=*/true,
      /*Pattern=*/nullptr, /*ForConstraintInstantiation=*/true,
      /*SkipForSpecialization*/ false);

  if (MLTAL.getNumSubstitutedLevels() == 0)
    return ConstrExpr;

  Sema::SFINAETrap SFINAE(S, /*AccessCheckingSFINAE=*/false);

  Sema::InstantiatingTemplate Inst(
      S, DeclInfo.getLocation(),
      Sema::InstantiatingTemplate::ConstraintNormalization{},
      const_cast<NamedDecl *>(DeclInfo.getDecl()), SourceRange{});
  if (Inst.isInvalid())
    return nullptr;

  // Set up a dummy 'instantiation' scope in the case of reference to function
  // parameters that the surrounding function hasn't been instantiated yet. Note
  // this may happen while we're comparing two templates' constraint
  // equivalence.
  LocalInstantiationScope ScopeForParameters(S, /*CombineWithOuterScope=*/true);
  if (auto *FD = DeclInfo.getDecl()->getAsFunction())
    for (auto *PVD : FD->parameters()) {
      if (!PVD->isParameterPack()) {
        ScopeForParameters.InstantiatedLocal(PVD, PVD);
        continue;
      }
      // This is hacky: we're mapping the parameter pack to a size-of-1 argument
      // to avoid building SubstTemplateTypeParmPackTypes for
      // PackExpansionTypes. The SubstTemplateTypeParmPackType node would
      // otherwise reference the AssociatedDecl of the template arguments, which
      // is, in this case, the template declaration.
      //
      // However, as we are in the process of comparing potential
      // re-declarations, the canonical declaration is the declaration itself at
      // this point. So if we didn't expand these packs, we would end up with an
      // incorrect profile difference because we will be profiling the
      // canonical types!
      //
      // FIXME: Improve the "no-transform" machinery in FindInstantiatedDecl so
      // that we can eliminate the Scope in the cases where the declarations are
      // not necessarily instantiated. It would also benefit the noexcept
      // specifier comparison.
      ScopeForParameters.MakeInstantiatedLocalArgPack(PVD);
      ScopeForParameters.InstantiatedLocalPackArg(PVD, PVD);
    }

  std::optional<Sema::CXXThisScopeRAII> ThisScope;

  // See TreeTransform::RebuildTemplateSpecializationType. A context scope is
  // essential for having an injected class as the canonical type for a template
  // specialization type at the rebuilding stage. This guarantees that, for
  // out-of-line definitions, injected class name types and their equivalent
  // template specializations can be profiled to the same value, which makes it
  // possible that e.g. constraints involving C<Class<T>> and C<Class> are
  // perceived identical.
  std::optional<Sema::ContextRAII> ContextScope;
  if (auto *RD = dyn_cast<CXXRecordDecl>(DeclInfo.getDeclContext())) {
    ThisScope.emplace(S, const_cast<CXXRecordDecl *>(RD), Qualifiers());
    ContextScope.emplace(S, const_cast<DeclContext *>(cast<DeclContext>(RD)),
                         /*NewThisContext=*/false);
  }
  ExprResult SubstConstr = S.SubstConstraintExprWithoutSatisfaction(
      const_cast<clang::Expr *>(ConstrExpr), MLTAL);
  if (SFINAE.hasErrorOccurred() || !SubstConstr.isUsable())
    return nullptr;
  return SubstConstr.get();
}

bool Sema::AreConstraintExpressionsEqual(const NamedDecl *Old,
                                         const Expr *OldConstr,
                                         const TemplateCompareNewDeclInfo &New,
                                         const Expr *NewConstr) {
  if (OldConstr == NewConstr)
    return true;
  // C++ [temp.constr.decl]p4
  if (Old && !New.isInvalid() && !New.ContainsDecl(Old) &&
      Old->getLexicalDeclContext() != New.getLexicalDeclContext()) {
    if (const Expr *SubstConstr =
            SubstituteConstraintExpressionWithoutSatisfaction(*this, Old,
                                                              OldConstr))
      OldConstr = SubstConstr;
    else
      return false;
    if (const Expr *SubstConstr =
            SubstituteConstraintExpressionWithoutSatisfaction(*this, New,
                                                              NewConstr))
      NewConstr = SubstConstr;
    else
      return false;
  }

  llvm::FoldingSetNodeID ID1, ID2;
  OldConstr->Profile(ID1, Context, /*Canonical=*/true);
  NewConstr->Profile(ID2, Context, /*Canonical=*/true);
  return ID1 == ID2;
}

bool Sema::FriendConstraintsDependOnEnclosingTemplate(const FunctionDecl *FD) {
  assert(FD->getFriendObjectKind() && "Must be a friend!");

  // The logic for non-templates is handled in ASTContext::isSameEntity, so we
  // don't have to bother checking 'DependsOnEnclosingTemplate' for a
  // non-function-template.
  assert(FD->getDescribedFunctionTemplate() &&
         "Non-function templates don't need to be checked");

  SmallVector<const Expr *, 3> ACs;
  FD->getDescribedFunctionTemplate()->getAssociatedConstraints(ACs);

  unsigned OldTemplateDepth = CalculateTemplateDepthForConstraints(*this, FD);
  for (const Expr *Constraint : ACs)
    if (ConstraintExpressionDependsOnEnclosingTemplate(FD, OldTemplateDepth,
                                                       Constraint))
      return true;

  return false;
}

bool Sema::EnsureTemplateArgumentListConstraints(
    TemplateDecl *TD, const MultiLevelTemplateArgumentList &TemplateArgsLists,
    SourceRange TemplateIDRange) {
  ConstraintSatisfaction Satisfaction;
  llvm::SmallVector<const Expr *, 3> AssociatedConstraints;
  TD->getAssociatedConstraints(AssociatedConstraints);
  if (CheckConstraintSatisfaction(TD, AssociatedConstraints, TemplateArgsLists,
                                  TemplateIDRange, Satisfaction))
    return true;

  if (!Satisfaction.IsSatisfied) {
    SmallString<128> TemplateArgString;
    TemplateArgString = " ";
    TemplateArgString += getTemplateArgumentBindingsText(
        TD->getTemplateParameters(), TemplateArgsLists.getInnermost().data(),
        TemplateArgsLists.getInnermost().size());

    Diag(TemplateIDRange.getBegin(),
         diag::err_template_arg_list_constraints_not_satisfied)
        << (int)getTemplateNameKindForDiagnostics(TemplateName(TD)) << TD
        << TemplateArgString << TemplateIDRange;
    DiagnoseUnsatisfiedConstraint(Satisfaction);
    return true;
  }
  return false;
}

bool Sema::CheckInstantiatedFunctionTemplateConstraints(
    SourceLocation PointOfInstantiation, FunctionDecl *Decl,
    ArrayRef<TemplateArgument> TemplateArgs,
    ConstraintSatisfaction &Satisfaction) {
  // In most cases we're not going to have constraints, so check for that first.
  FunctionTemplateDecl *Template = Decl->getPrimaryTemplate();
  // Note - code synthesis context for the constraints check is created
  // inside CheckConstraintsSatisfaction.
  SmallVector<const Expr *, 3> TemplateAC;
  Template->getAssociatedConstraints(TemplateAC);
  if (TemplateAC.empty()) {
    Satisfaction.IsSatisfied = true;
    return false;
  }

  // Enter the scope of this instantiation. We don't use
  // PushDeclContext because we don't have a scope.
  Sema::ContextRAII savedContext(*this, Decl);
  LocalInstantiationScope Scope(*this);

  std::optional<MultiLevelTemplateArgumentList> MLTAL =
      SetupConstraintCheckingTemplateArgumentsAndScope(Decl, TemplateArgs,
                                                       Scope);

  if (!MLTAL)
    return true;

  Qualifiers ThisQuals;
  CXXRecordDecl *Record = nullptr;
  if (auto *Method = dyn_cast<CXXMethodDecl>(Decl)) {
    ThisQuals = Method->getMethodQualifiers();
    Record = Method->getParent();
  }

  CXXThisScopeRAII ThisScope(*this, Record, ThisQuals, Record != nullptr);
  LambdaScopeForCallOperatorInstantiationRAII LambdaScope(
      *this, const_cast<FunctionDecl *>(Decl), *MLTAL, Scope);

  llvm::SmallVector<Expr *, 1> Converted;
  return CheckConstraintSatisfaction(Template, TemplateAC, Converted, *MLTAL,
                                     PointOfInstantiation, Satisfaction);
}

static void diagnoseUnsatisfiedRequirement(Sema &S,
                                           concepts::ExprRequirement *Req,
                                           bool First) {
  assert(!Req->isSatisfied()
         && "Diagnose() can only be used on an unsatisfied requirement");
  switch (Req->getSatisfactionStatus()) {
    case concepts::ExprRequirement::SS_Dependent:
      llvm_unreachable("Diagnosing a dependent requirement");
      break;
    case concepts::ExprRequirement::SS_ExprSubstitutionFailure: {
      auto *SubstDiag = Req->getExprSubstitutionDiagnostic();
      if (!SubstDiag->DiagMessage.empty())
        S.Diag(SubstDiag->DiagLoc,
               diag::note_expr_requirement_expr_substitution_error)
               << (int)First << SubstDiag->SubstitutedEntity
               << SubstDiag->DiagMessage;
      else
        S.Diag(SubstDiag->DiagLoc,
               diag::note_expr_requirement_expr_unknown_substitution_error)
            << (int)First << SubstDiag->SubstitutedEntity;
      break;
    }
    case concepts::ExprRequirement::SS_NoexceptNotMet:
      S.Diag(Req->getNoexceptLoc(),
             diag::note_expr_requirement_noexcept_not_met)
          << (int)First << Req->getExpr();
      break;
    case concepts::ExprRequirement::SS_TypeRequirementSubstitutionFailure: {
      auto *SubstDiag =
          Req->getReturnTypeRequirement().getSubstitutionDiagnostic();
      if (!SubstDiag->DiagMessage.empty())
        S.Diag(SubstDiag->DiagLoc,
               diag::note_expr_requirement_type_requirement_substitution_error)
            << (int)First << SubstDiag->SubstitutedEntity
            << SubstDiag->DiagMessage;
      else
        S.Diag(SubstDiag->DiagLoc,
               diag::note_expr_requirement_type_requirement_unknown_substitution_error)
            << (int)First << SubstDiag->SubstitutedEntity;
      break;
    }
    case concepts::ExprRequirement::SS_ConstraintsNotSatisfied: {
      ConceptSpecializationExpr *ConstraintExpr =
          Req->getReturnTypeRequirementSubstitutedConstraintExpr();
      if (ConstraintExpr->getTemplateArgsAsWritten()->NumTemplateArgs == 1) {
        // A simple case - expr type is the type being constrained and the concept
        // was not provided arguments.
        Expr *e = Req->getExpr();
        S.Diag(e->getBeginLoc(),
               diag::note_expr_requirement_constraints_not_satisfied_simple)
            << (int)First << S.Context.getReferenceQualifiedType(e)
            << ConstraintExpr->getNamedConcept();
      } else {
        S.Diag(ConstraintExpr->getBeginLoc(),
               diag::note_expr_requirement_constraints_not_satisfied)
            << (int)First << ConstraintExpr;
      }
      S.DiagnoseUnsatisfiedConstraint(ConstraintExpr->getSatisfaction());
      break;
    }
    case concepts::ExprRequirement::SS_Satisfied:
      llvm_unreachable("We checked this above");
  }
}

static void diagnoseUnsatisfiedRequirement(Sema &S,
                                           concepts::TypeRequirement *Req,
                                           bool First) {
  assert(!Req->isSatisfied()
         && "Diagnose() can only be used on an unsatisfied requirement");
  switch (Req->getSatisfactionStatus()) {
  case concepts::TypeRequirement::SS_Dependent:
    llvm_unreachable("Diagnosing a dependent requirement");
    return;
  case concepts::TypeRequirement::SS_SubstitutionFailure: {
    auto *SubstDiag = Req->getSubstitutionDiagnostic();
    if (!SubstDiag->DiagMessage.empty())
      S.Diag(SubstDiag->DiagLoc,
             diag::note_type_requirement_substitution_error) << (int)First
          << SubstDiag->SubstitutedEntity << SubstDiag->DiagMessage;
    else
      S.Diag(SubstDiag->DiagLoc,
             diag::note_type_requirement_unknown_substitution_error)
          << (int)First << SubstDiag->SubstitutedEntity;
    return;
  }
  default:
    llvm_unreachable("Unknown satisfaction status");
    return;
  }
}
static void diagnoseWellFormedUnsatisfiedConstraintExpr(Sema &S,
                                                        Expr *SubstExpr,
                                                        bool First = true);

static void diagnoseUnsatisfiedRequirement(Sema &S,
                                           concepts::NestedRequirement *Req,
                                           bool First) {
  using SubstitutionDiagnostic = std::pair<SourceLocation, StringRef>;
  for (auto &Record : Req->getConstraintSatisfaction()) {
    if (auto *SubstDiag = Record.dyn_cast<SubstitutionDiagnostic *>())
      S.Diag(SubstDiag->first, diag::note_nested_requirement_substitution_error)
          << (int)First << Req->getInvalidConstraintEntity()
          << SubstDiag->second;
    else
      diagnoseWellFormedUnsatisfiedConstraintExpr(S, Record.dyn_cast<Expr *>(),
                                                  First);
    First = false;
  }
}

static void diagnoseWellFormedUnsatisfiedConstraintExpr(Sema &S,
                                                        Expr *SubstExpr,
                                                        bool First) {
  SubstExpr = SubstExpr->IgnoreParenImpCasts();
  if (BinaryOperator *BO = dyn_cast<BinaryOperator>(SubstExpr)) {
    switch (BO->getOpcode()) {
    // These two cases will in practice only be reached when using fold
    // expressions with || and &&, since otherwise the || and && will have been
    // broken down into atomic constraints during satisfaction checking.
    case BO_LOr:
      // Or evaluated to false - meaning both RHS and LHS evaluated to false.
      diagnoseWellFormedUnsatisfiedConstraintExpr(S, BO->getLHS(), First);
      diagnoseWellFormedUnsatisfiedConstraintExpr(S, BO->getRHS(),
                                                  /*First=*/false);
      return;
    case BO_LAnd: {
      bool LHSSatisfied =
          BO->getLHS()->EvaluateKnownConstInt(S.Context).getBoolValue();
      if (LHSSatisfied) {
        // LHS is true, so RHS must be false.
        diagnoseWellFormedUnsatisfiedConstraintExpr(S, BO->getRHS(), First);
        return;
      }
      // LHS is false
      diagnoseWellFormedUnsatisfiedConstraintExpr(S, BO->getLHS(), First);

      // RHS might also be false
      bool RHSSatisfied =
          BO->getRHS()->EvaluateKnownConstInt(S.Context).getBoolValue();
      if (!RHSSatisfied)
        diagnoseWellFormedUnsatisfiedConstraintExpr(S, BO->getRHS(),
                                                    /*First=*/false);
      return;
    }
    case BO_GE:
    case BO_LE:
    case BO_GT:
    case BO_LT:
    case BO_EQ:
    case BO_NE:
      if (BO->getLHS()->getType()->isIntegerType() &&
          BO->getRHS()->getType()->isIntegerType()) {
        Expr::EvalResult SimplifiedLHS;
        Expr::EvalResult SimplifiedRHS;
        BO->getLHS()->EvaluateAsInt(SimplifiedLHS, S.Context,
                                    Expr::SE_NoSideEffects,
                                    /*InConstantContext=*/true);
        BO->getRHS()->EvaluateAsInt(SimplifiedRHS, S.Context,
                                    Expr::SE_NoSideEffects,
                                    /*InConstantContext=*/true);
        if (!SimplifiedLHS.Diag && ! SimplifiedRHS.Diag) {
          S.Diag(SubstExpr->getBeginLoc(),
                 diag::note_atomic_constraint_evaluated_to_false_elaborated)
              << (int)First << SubstExpr
              << toString(SimplifiedLHS.Val.getInt(), 10)
              << BinaryOperator::getOpcodeStr(BO->getOpcode())
              << toString(SimplifiedRHS.Val.getInt(), 10);
          return;
        }
      }
      break;

    default:
      break;
    }
  } else if (auto *CSE = dyn_cast<ConceptSpecializationExpr>(SubstExpr)) {
    if (CSE->getTemplateArgsAsWritten()->NumTemplateArgs == 1) {
      S.Diag(
          CSE->getSourceRange().getBegin(),
          diag::
          note_single_arg_concept_specialization_constraint_evaluated_to_false)
          << (int)First
          << CSE->getTemplateArgsAsWritten()->arguments()[0].getArgument()
          << CSE->getNamedConcept();
    } else {
      S.Diag(SubstExpr->getSourceRange().getBegin(),
             diag::note_concept_specialization_constraint_evaluated_to_false)
          << (int)First << CSE;
    }
    S.DiagnoseUnsatisfiedConstraint(CSE->getSatisfaction());
    return;
  } else if (auto *RE = dyn_cast<RequiresExpr>(SubstExpr)) {
    // FIXME: RequiresExpr should store dependent diagnostics.
    for (concepts::Requirement *Req : RE->getRequirements())
      if (!Req->isDependent() && !Req->isSatisfied()) {
        if (auto *E = dyn_cast<concepts::ExprRequirement>(Req))
          diagnoseUnsatisfiedRequirement(S, E, First);
        else if (auto *T = dyn_cast<concepts::TypeRequirement>(Req))
          diagnoseUnsatisfiedRequirement(S, T, First);
        else
          diagnoseUnsatisfiedRequirement(
              S, cast<concepts::NestedRequirement>(Req), First);
        break;
      }
    return;
  } else if (auto *TTE = dyn_cast<TypeTraitExpr>(SubstExpr);
             TTE && TTE->getTrait() == clang::TypeTrait::BTT_IsDeducible) {
    assert(TTE->getNumArgs() == 2);
    S.Diag(SubstExpr->getSourceRange().getBegin(),
           diag::note_is_deducible_constraint_evaluated_to_false)
        << TTE->getArg(0)->getType() << TTE->getArg(1)->getType();
    return;
  }

  S.Diag(SubstExpr->getSourceRange().getBegin(),
         diag::note_atomic_constraint_evaluated_to_false)
      << (int)First << SubstExpr;
}

template <typename SubstitutionDiagnostic>
static void diagnoseUnsatisfiedConstraintExpr(
    Sema &S, const llvm::PointerUnion<Expr *, SubstitutionDiagnostic *> &Record,
    bool First = true) {
  if (auto *Diag = Record.template dyn_cast<SubstitutionDiagnostic *>()) {
    S.Diag(Diag->first, diag::note_substituted_constraint_expr_is_ill_formed)
        << Diag->second;
    return;
  }

  diagnoseWellFormedUnsatisfiedConstraintExpr(S,
      Record.template get<Expr *>(), First);
}

void
Sema::DiagnoseUnsatisfiedConstraint(const ConstraintSatisfaction& Satisfaction,
                                    bool First) {
  assert(!Satisfaction.IsSatisfied &&
         "Attempted to diagnose a satisfied constraint");
  for (auto &Record : Satisfaction.Details) {
    diagnoseUnsatisfiedConstraintExpr(*this, Record, First);
    First = false;
  }
}

void Sema::DiagnoseUnsatisfiedConstraint(
    const ASTConstraintSatisfaction &Satisfaction,
    bool First) {
  assert(!Satisfaction.IsSatisfied &&
         "Attempted to diagnose a satisfied constraint");
  for (auto &Record : Satisfaction) {
    diagnoseUnsatisfiedConstraintExpr(*this, Record, First);
    First = false;
  }
}

const NormalizedConstraint *
Sema::getNormalizedAssociatedConstraints(
    NamedDecl *ConstrainedDecl, ArrayRef<const Expr *> AssociatedConstraints) {
  // In case the ConstrainedDecl comes from modules, it is necessary to use
  // the canonical decl to avoid different atomic constraints with the 'same'
  // declarations.
  ConstrainedDecl = cast<NamedDecl>(ConstrainedDecl->getCanonicalDecl());

  auto CacheEntry = NormalizationCache.find(ConstrainedDecl);
  if (CacheEntry == NormalizationCache.end()) {
    auto Normalized =
        NormalizedConstraint::fromConstraintExprs(*this, ConstrainedDecl,
                                                  AssociatedConstraints);
    CacheEntry =
        NormalizationCache
            .try_emplace(ConstrainedDecl,
                         Normalized
                             ? new (Context) NormalizedConstraint(
                                 std::move(*Normalized))
                             : nullptr)
            .first;
  }
  return CacheEntry->second;
}

const NormalizedConstraint *clang::getNormalizedAssociatedConstraints(
    Sema &S, NamedDecl *ConstrainedDecl,
    ArrayRef<const Expr *> AssociatedConstraints) {
  return S.getNormalizedAssociatedConstraints(ConstrainedDecl,
                                              AssociatedConstraints);
}

static bool
substituteParameterMappings(Sema &S, NormalizedConstraint &N,
                            ConceptDecl *Concept,
                            const MultiLevelTemplateArgumentList &MLTAL,
                            const ASTTemplateArgumentListInfo *ArgsAsWritten) {

  if (N.isCompound()) {
    if (substituteParameterMappings(S, N.getLHS(), Concept, MLTAL,
                                    ArgsAsWritten))
      return true;
    return substituteParameterMappings(S, N.getRHS(), Concept, MLTAL,
                                       ArgsAsWritten);
  }

  if (N.isFoldExpanded()) {
    Sema::ArgumentPackSubstitutionIndexRAII _(S, -1);
    return substituteParameterMappings(
        S, N.getFoldExpandedConstraint()->Constraint, Concept, MLTAL,
        ArgsAsWritten);
  }

  TemplateParameterList *TemplateParams = Concept->getTemplateParameters();

  AtomicConstraint &Atomic = *N.getAtomicConstraint();
  TemplateArgumentListInfo SubstArgs;
  if (!Atomic.ParameterMapping) {
    llvm::SmallBitVector OccurringIndices(TemplateParams->size());
    S.MarkUsedTemplateParameters(Atomic.ConstraintExpr, /*OnlyDeduced=*/false,
                                 /*Depth=*/0, OccurringIndices);
    TemplateArgumentLoc *TempArgs =
        new (S.Context) TemplateArgumentLoc[OccurringIndices.count()];
    for (unsigned I = 0, J = 0, C = TemplateParams->size(); I != C; ++I)
      if (OccurringIndices[I])
        new (&(TempArgs)[J++])
            TemplateArgumentLoc(S.getIdentityTemplateArgumentLoc(
                TemplateParams->begin()[I],
                // Here we assume we do not support things like
                // template<typename A, typename B>
                // concept C = ...;
                //
                // template<typename... Ts> requires C<Ts...>
                // struct S { };
                // The above currently yields a diagnostic.
                // We still might have default arguments for concept parameters.
                ArgsAsWritten->NumTemplateArgs > I
                    ? ArgsAsWritten->arguments()[I].getLocation()
                    : SourceLocation()));
    Atomic.ParameterMapping.emplace(TempArgs,  OccurringIndices.count());
  }
  SourceLocation InstLocBegin =
      ArgsAsWritten->arguments().empty()
          ? ArgsAsWritten->getLAngleLoc()
          : ArgsAsWritten->arguments().front().getSourceRange().getBegin();
  SourceLocation InstLocEnd =
      ArgsAsWritten->arguments().empty()
          ? ArgsAsWritten->getRAngleLoc()
          : ArgsAsWritten->arguments().front().getSourceRange().getEnd();
  Sema::InstantiatingTemplate Inst(
      S, InstLocBegin,
      Sema::InstantiatingTemplate::ParameterMappingSubstitution{}, Concept,
      {InstLocBegin, InstLocEnd});
  if (Inst.isInvalid())
    return true;
  if (S.SubstTemplateArguments(*Atomic.ParameterMapping, MLTAL, SubstArgs))
    return true;

  TemplateArgumentLoc *TempArgs =
      new (S.Context) TemplateArgumentLoc[SubstArgs.size()];
  std::copy(SubstArgs.arguments().begin(), SubstArgs.arguments().end(),
            TempArgs);
  Atomic.ParameterMapping.emplace(TempArgs, SubstArgs.size());
  return false;
}

static bool substituteParameterMappings(Sema &S, NormalizedConstraint &N,
                                        const ConceptSpecializationExpr *CSE) {
  MultiLevelTemplateArgumentList MLTAL = S.getTemplateInstantiationArgs(
      CSE->getNamedConcept(), CSE->getNamedConcept()->getLexicalDeclContext(),
      /*Final=*/false, CSE->getTemplateArguments(),
      /*RelativeToPrimary=*/true,
      /*Pattern=*/nullptr,
      /*ForConstraintInstantiation=*/true);

  return substituteParameterMappings(S, N, CSE->getNamedConcept(), MLTAL,
                                     CSE->getTemplateArgsAsWritten());
}

NormalizedConstraint::NormalizedConstraint(ASTContext &C,
                                           NormalizedConstraint LHS,
                                           NormalizedConstraint RHS,
                                           CompoundConstraintKind Kind)
    : Constraint{CompoundConstraint{
          new(C) NormalizedConstraintPair{std::move(LHS), std::move(RHS)},
          Kind}} {}

NormalizedConstraint::NormalizedConstraint(ASTContext &C,
                                           const NormalizedConstraint &Other) {
  if (Other.isAtomic()) {
    Constraint = new (C) AtomicConstraint(*Other.getAtomicConstraint());
  } else if (Other.isFoldExpanded()) {
    Constraint = new (C) FoldExpandedConstraint(
        Other.getFoldExpandedConstraint()->Kind,
        NormalizedConstraint(C, Other.getFoldExpandedConstraint()->Constraint),
        Other.getFoldExpandedConstraint()->Pattern);
  } else {
    Constraint = CompoundConstraint(
        new (C)
            NormalizedConstraintPair{NormalizedConstraint(C, Other.getLHS()),
                                     NormalizedConstraint(C, Other.getRHS())},
        Other.getCompoundKind());
  }
}

NormalizedConstraint &NormalizedConstraint::getLHS() const {
  assert(isCompound() && "getLHS called on a non-compound constraint.");
  return Constraint.get<CompoundConstraint>().getPointer()->LHS;
}

NormalizedConstraint &NormalizedConstraint::getRHS() const {
  assert(isCompound() && "getRHS called on a non-compound constraint.");
  return Constraint.get<CompoundConstraint>().getPointer()->RHS;
}

std::optional<NormalizedConstraint>
NormalizedConstraint::fromConstraintExprs(Sema &S, NamedDecl *D,
                                          ArrayRef<const Expr *> E) {
  assert(E.size() != 0);
  auto Conjunction = fromConstraintExpr(S, D, E[0]);
  if (!Conjunction)
    return std::nullopt;
  for (unsigned I = 1; I < E.size(); ++I) {
    auto Next = fromConstraintExpr(S, D, E[I]);
    if (!Next)
      return std::nullopt;
    *Conjunction = NormalizedConstraint(S.Context, std::move(*Conjunction),
                                        std::move(*Next), CCK_Conjunction);
  }
  return Conjunction;
}

std::optional<NormalizedConstraint>
NormalizedConstraint::fromConstraintExpr(Sema &S, NamedDecl *D, const Expr *E) {
  assert(E != nullptr);

  // C++ [temp.constr.normal]p1.1
  // [...]
  // - The normal form of an expression (E) is the normal form of E.
  // [...]
  E = E->IgnoreParenImpCasts();

  // C++2a [temp.param]p4:
  //     [...] If T is not a pack, then E is E', otherwise E is (E' && ...).
  // Fold expression is considered atomic constraints per current wording.
  // See http://cplusplus.github.io/concepts-ts/ts-active.html#28

  if (LogicalBinOp BO = E) {
    auto LHS = fromConstraintExpr(S, D, BO.getLHS());
    if (!LHS)
      return std::nullopt;
    auto RHS = fromConstraintExpr(S, D, BO.getRHS());
    if (!RHS)
      return std::nullopt;

    return NormalizedConstraint(S.Context, std::move(*LHS), std::move(*RHS),
                                BO.isAnd() ? CCK_Conjunction : CCK_Disjunction);
  } else if (auto *CSE = dyn_cast<const ConceptSpecializationExpr>(E)) {
    const NormalizedConstraint *SubNF;
    {
      Sema::InstantiatingTemplate Inst(
          S, CSE->getExprLoc(),
          Sema::InstantiatingTemplate::ConstraintNormalization{}, D,
          CSE->getSourceRange());
      if (Inst.isInvalid())
        return std::nullopt;
      // C++ [temp.constr.normal]p1.1
      // [...]
      // The normal form of an id-expression of the form C<A1, A2, ..., AN>,
      // where C names a concept, is the normal form of the
      // constraint-expression of C, after substituting A1, A2, ..., AN for C’s
      // respective template parameters in the parameter mappings in each atomic
      // constraint. If any such substitution results in an invalid type or
      // expression, the program is ill-formed; no diagnostic is required.
      // [...]
      ConceptDecl *CD = CSE->getNamedConcept();
      SubNF = S.getNormalizedAssociatedConstraints(CD,
                                                   {CD->getConstraintExpr()});
      if (!SubNF)
        return std::nullopt;
    }

    std::optional<NormalizedConstraint> New;
    New.emplace(S.Context, *SubNF);

    if (substituteParameterMappings(S, *New, CSE))
      return std::nullopt;

    return New;
  } else if (auto *FE = dyn_cast<const CXXFoldExpr>(E);
             FE && S.getLangOpts().CPlusPlus26 &&
             (FE->getOperator() == BinaryOperatorKind::BO_LAnd ||
              FE->getOperator() == BinaryOperatorKind::BO_LOr)) {

    // Normalize fold expressions in C++26.

    FoldExpandedConstraint::FoldOperatorKind Kind =
        FE->getOperator() == BinaryOperatorKind::BO_LAnd
            ? FoldExpandedConstraint::FoldOperatorKind::And
            : FoldExpandedConstraint::FoldOperatorKind::Or;

    if (FE->getInit()) {
      auto LHS = fromConstraintExpr(S, D, FE->getLHS());
      auto RHS = fromConstraintExpr(S, D, FE->getRHS());
      if (!LHS || !RHS)
        return std::nullopt;

      if (FE->isRightFold())
        RHS = NormalizedConstraint{new (S.Context) FoldExpandedConstraint{
            Kind, std::move(*RHS), FE->getPattern()}};
      else
        LHS = NormalizedConstraint{new (S.Context) FoldExpandedConstraint{
            Kind, std::move(*LHS), FE->getPattern()}};

      return NormalizedConstraint(
          S.Context, std::move(*LHS), std::move(*RHS),
          FE->getOperator() == BinaryOperatorKind::BO_LAnd ? CCK_Conjunction
                                                           : CCK_Disjunction);
    }
    auto Sub = fromConstraintExpr(S, D, FE->getPattern());
    if (!Sub)
      return std::nullopt;
    return NormalizedConstraint{new (S.Context) FoldExpandedConstraint{
        Kind, std::move(*Sub), FE->getPattern()}};
  }

  return NormalizedConstraint{new (S.Context) AtomicConstraint(S, E)};
}

bool FoldExpandedConstraint::AreCompatibleForSubsumption(
    const FoldExpandedConstraint &A, const FoldExpandedConstraint &B) {

  // [C++26] [temp.constr.fold]
  // Two fold expanded constraints are compatible for subsumption
  // if their respective constraints both contain an equivalent unexpanded pack.

  llvm::SmallVector<UnexpandedParameterPack> APacks, BPacks;
  Sema::collectUnexpandedParameterPacks(const_cast<Expr *>(A.Pattern), APacks);
  Sema::collectUnexpandedParameterPacks(const_cast<Expr *>(B.Pattern), BPacks);

  for (const UnexpandedParameterPack &APack : APacks) {
    std::pair<unsigned, unsigned> DepthAndIndex = getDepthAndIndex(APack);
    auto it = llvm::find_if(BPacks, [&](const UnexpandedParameterPack &BPack) {
      return getDepthAndIndex(BPack) == DepthAndIndex;
    });
    if (it != BPacks.end())
      return true;
  }
  return false;
}

NormalForm clang::makeCNF(const NormalizedConstraint &Normalized) {
  if (Normalized.isAtomic())
    return {{Normalized.getAtomicConstraint()}};

  else if (Normalized.isFoldExpanded())
    return {{Normalized.getFoldExpandedConstraint()}};

  NormalForm LCNF = makeCNF(Normalized.getLHS());
  NormalForm RCNF = makeCNF(Normalized.getRHS());
  if (Normalized.getCompoundKind() == NormalizedConstraint::CCK_Conjunction) {
    LCNF.reserve(LCNF.size() + RCNF.size());
    while (!RCNF.empty())
      LCNF.push_back(RCNF.pop_back_val());
    return LCNF;
  }

  // Disjunction
  NormalForm Res;
  Res.reserve(LCNF.size() * RCNF.size());
  for (auto &LDisjunction : LCNF)
    for (auto &RDisjunction : RCNF) {
      NormalForm::value_type Combined;
      Combined.reserve(LDisjunction.size() + RDisjunction.size());
      std::copy(LDisjunction.begin(), LDisjunction.end(),
                std::back_inserter(Combined));
      std::copy(RDisjunction.begin(), RDisjunction.end(),
                std::back_inserter(Combined));
      Res.emplace_back(Combined);
    }
  return Res;
}

NormalForm clang::makeDNF(const NormalizedConstraint &Normalized) {
  if (Normalized.isAtomic())
    return {{Normalized.getAtomicConstraint()}};

  else if (Normalized.isFoldExpanded())
    return {{Normalized.getFoldExpandedConstraint()}};

  NormalForm LDNF = makeDNF(Normalized.getLHS());
  NormalForm RDNF = makeDNF(Normalized.getRHS());
  if (Normalized.getCompoundKind() == NormalizedConstraint::CCK_Disjunction) {
    LDNF.reserve(LDNF.size() + RDNF.size());
    while (!RDNF.empty())
      LDNF.push_back(RDNF.pop_back_val());
    return LDNF;
  }

  // Conjunction
  NormalForm Res;
  Res.reserve(LDNF.size() * RDNF.size());
  for (auto &LConjunction : LDNF) {
    for (auto &RConjunction : RDNF) {
      NormalForm::value_type Combined;
      Combined.reserve(LConjunction.size() + RConjunction.size());
      std::copy(LConjunction.begin(), LConjunction.end(),
                std::back_inserter(Combined));
      std::copy(RConjunction.begin(), RConjunction.end(),
                std::back_inserter(Combined));
      Res.emplace_back(Combined);
    }
  }
  return Res;
}

bool Sema::IsAtLeastAsConstrained(NamedDecl *D1,
                                  MutableArrayRef<const Expr *> AC1,
                                  NamedDecl *D2,
                                  MutableArrayRef<const Expr *> AC2,
                                  bool &Result) {
  if (const auto *FD1 = dyn_cast<FunctionDecl>(D1)) {
    auto IsExpectedEntity = [](const FunctionDecl *FD) {
      FunctionDecl::TemplatedKind Kind = FD->getTemplatedKind();
      return Kind == FunctionDecl::TK_NonTemplate ||
             Kind == FunctionDecl::TK_FunctionTemplate;
    };
    const auto *FD2 = dyn_cast<FunctionDecl>(D2);
    (void)IsExpectedEntity;
    (void)FD1;
    (void)FD2;
    assert(IsExpectedEntity(FD1) && FD2 && IsExpectedEntity(FD2) &&
           "use non-instantiated function declaration for constraints partial "
           "ordering");
  }

  if (AC1.empty()) {
    Result = AC2.empty();
    return false;
  }
  if (AC2.empty()) {
    // TD1 has associated constraints and TD2 does not.
    Result = true;
    return false;
  }

  std::pair<NamedDecl *, NamedDecl *> Key{D1, D2};
  auto CacheEntry = SubsumptionCache.find(Key);
  if (CacheEntry != SubsumptionCache.end()) {
    Result = CacheEntry->second;
    return false;
  }

  unsigned Depth1 = CalculateTemplateDepthForConstraints(*this, D1, true);
  unsigned Depth2 = CalculateTemplateDepthForConstraints(*this, D2, true);

  for (size_t I = 0; I != AC1.size() && I != AC2.size(); ++I) {
    if (Depth2 > Depth1) {
      AC1[I] = AdjustConstraintDepth(*this, Depth2 - Depth1)
                   .TransformExpr(const_cast<Expr *>(AC1[I]))
                   .get();
    } else if (Depth1 > Depth2) {
      AC2[I] = AdjustConstraintDepth(*this, Depth1 - Depth2)
                   .TransformExpr(const_cast<Expr *>(AC2[I]))
                   .get();
    }
  }

  if (clang::subsumes(
          *this, D1, AC1, D2, AC2, Result,
          [this](const AtomicConstraint &A, const AtomicConstraint &B) {
            return A.subsumes(Context, B);
          }))
    return true;
  SubsumptionCache.try_emplace(Key, Result);
  return false;
}

bool Sema::MaybeEmitAmbiguousAtomicConstraintsDiagnostic(NamedDecl *D1,
    ArrayRef<const Expr *> AC1, NamedDecl *D2, ArrayRef<const Expr *> AC2) {
  if (isSFINAEContext())
    // No need to work here because our notes would be discarded.
    return false;

  if (AC1.empty() || AC2.empty())
    return false;

  auto NormalExprEvaluator =
      [this] (const AtomicConstraint &A, const AtomicConstraint &B) {
        return A.subsumes(Context, B);
      };

  const Expr *AmbiguousAtomic1 = nullptr, *AmbiguousAtomic2 = nullptr;
  auto IdenticalExprEvaluator =
      [&] (const AtomicConstraint &A, const AtomicConstraint &B) {
        if (!A.hasMatchingParameterMapping(Context, B))
          return false;
        const Expr *EA = A.ConstraintExpr, *EB = B.ConstraintExpr;
        if (EA == EB)
          return true;

        // Not the same source level expression - are the expressions
        // identical?
        llvm::FoldingSetNodeID IDA, IDB;
        EA->Profile(IDA, Context, /*Canonical=*/true);
        EB->Profile(IDB, Context, /*Canonical=*/true);
        if (IDA != IDB)
          return false;

        AmbiguousAtomic1 = EA;
        AmbiguousAtomic2 = EB;
        return true;
      };

  {
    // The subsumption checks might cause diagnostics
    SFINAETrap Trap(*this);
    auto *Normalized1 = getNormalizedAssociatedConstraints(D1, AC1);
    if (!Normalized1)
      return false;
    const NormalForm DNF1 = makeDNF(*Normalized1);
    const NormalForm CNF1 = makeCNF(*Normalized1);

    auto *Normalized2 = getNormalizedAssociatedConstraints(D2, AC2);
    if (!Normalized2)
      return false;
    const NormalForm DNF2 = makeDNF(*Normalized2);
    const NormalForm CNF2 = makeCNF(*Normalized2);

    bool Is1AtLeastAs2Normally =
        clang::subsumes(DNF1, CNF2, NormalExprEvaluator);
    bool Is2AtLeastAs1Normally =
        clang::subsumes(DNF2, CNF1, NormalExprEvaluator);
    bool Is1AtLeastAs2 = clang::subsumes(DNF1, CNF2, IdenticalExprEvaluator);
    bool Is2AtLeastAs1 = clang::subsumes(DNF2, CNF1, IdenticalExprEvaluator);
    if (Is1AtLeastAs2 == Is1AtLeastAs2Normally &&
        Is2AtLeastAs1 == Is2AtLeastAs1Normally)
      // Same result - no ambiguity was caused by identical atomic expressions.
      return false;
  }

  // A different result! Some ambiguous atomic constraint(s) caused a difference
  assert(AmbiguousAtomic1 && AmbiguousAtomic2);

  Diag(AmbiguousAtomic1->getBeginLoc(), diag::note_ambiguous_atomic_constraints)
      << AmbiguousAtomic1->getSourceRange();
  Diag(AmbiguousAtomic2->getBeginLoc(),
       diag::note_ambiguous_atomic_constraints_similar_expression)
      << AmbiguousAtomic2->getSourceRange();
  return true;
}

concepts::ExprRequirement::ExprRequirement(
    Expr *E, bool IsSimple, SourceLocation NoexceptLoc,
    ReturnTypeRequirement Req, SatisfactionStatus Status,
    ConceptSpecializationExpr *SubstitutedConstraintExpr) :
    Requirement(IsSimple ? RK_Simple : RK_Compound, Status == SS_Dependent,
                Status == SS_Dependent &&
                (E->containsUnexpandedParameterPack() ||
                 Req.containsUnexpandedParameterPack()),
                Status == SS_Satisfied), Value(E), NoexceptLoc(NoexceptLoc),
    TypeReq(Req), SubstitutedConstraintExpr(SubstitutedConstraintExpr),
    Status(Status) {
  assert((!IsSimple || (Req.isEmpty() && NoexceptLoc.isInvalid())) &&
         "Simple requirement must not have a return type requirement or a "
         "noexcept specification");
  assert((Status > SS_TypeRequirementSubstitutionFailure && Req.isTypeConstraint()) ==
         (SubstitutedConstraintExpr != nullptr));
}

concepts::ExprRequirement::ExprRequirement(
    SubstitutionDiagnostic *ExprSubstDiag, bool IsSimple,
    SourceLocation NoexceptLoc, ReturnTypeRequirement Req) :
    Requirement(IsSimple ? RK_Simple : RK_Compound, Req.isDependent(),
                Req.containsUnexpandedParameterPack(), /*IsSatisfied=*/false),
    Value(ExprSubstDiag), NoexceptLoc(NoexceptLoc), TypeReq(Req),
    Status(SS_ExprSubstitutionFailure) {
  assert((!IsSimple || (Req.isEmpty() && NoexceptLoc.isInvalid())) &&
         "Simple requirement must not have a return type requirement or a "
         "noexcept specification");
}

concepts::ExprRequirement::ReturnTypeRequirement::
ReturnTypeRequirement(TemplateParameterList *TPL) :
    TypeConstraintInfo(TPL, false) {
  assert(TPL->size() == 1);
  const TypeConstraint *TC =
      cast<TemplateTypeParmDecl>(TPL->getParam(0))->getTypeConstraint();
  assert(TC &&
         "TPL must have a template type parameter with a type constraint");
  auto *Constraint =
      cast<ConceptSpecializationExpr>(TC->getImmediatelyDeclaredConstraint());
  bool Dependent =
      Constraint->getTemplateArgsAsWritten() &&
      TemplateSpecializationType::anyInstantiationDependentTemplateArguments(
          Constraint->getTemplateArgsAsWritten()->arguments().drop_front(1));
  TypeConstraintInfo.setInt(Dependent ? true : false);
}

concepts::TypeRequirement::TypeRequirement(TypeSourceInfo *T) :
    Requirement(RK_Type, T->getType()->isInstantiationDependentType(),
                T->getType()->containsUnexpandedParameterPack(),
                // We reach this ctor with either dependent types (in which
                // IsSatisfied doesn't matter) or with non-dependent type in
                // which the existence of the type indicates satisfaction.
                /*IsSatisfied=*/true),
    Value(T),
    Status(T->getType()->isInstantiationDependentType() ? SS_Dependent
                                                        : SS_Satisfied) {}
