//===---------- ExprMutationAnalyzer.cpp ----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "clang/Analysis/Analyses/ExprMutationAnalyzer.h"
#include "clang/AST/Expr.h"
#include "clang/AST/OperationKinds.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "llvm/ADT/STLExtras.h"

namespace clang {
using namespace ast_matchers;

// Check if result of Source expression could be a Target expression.
// Checks:
//  - Implicit Casts
//  - Binary Operators
//  - ConditionalOperator
//  - BinaryConditionalOperator
static bool canExprResolveTo(const Expr *Source, const Expr *Target) {

  const auto IgnoreDerivedToBase = [](const Expr *E, auto Matcher) {
    if (Matcher(E))
      return true;
    if (const auto *Cast = dyn_cast<ImplicitCastExpr>(E)) {
      if ((Cast->getCastKind() == CK_DerivedToBase ||
           Cast->getCastKind() == CK_UncheckedDerivedToBase) &&
          Matcher(Cast->getSubExpr()))
        return true;
    }
    return false;
  };

  const auto EvalCommaExpr = [](const Expr *E, auto Matcher) {
    const Expr *Result = E;
    while (const auto *BOComma =
               dyn_cast_or_null<BinaryOperator>(Result->IgnoreParens())) {
      if (!BOComma->isCommaOp())
        break;
      Result = BOComma->getRHS();
    }

    return Result != E && Matcher(Result);
  };

  // The 'ConditionalOperatorM' matches on `<anything> ? <expr> : <expr>`.
  // This matching must be recursive because `<expr>` can be anything resolving
  // to the `InnerMatcher`, for example another conditional operator.
  // The edge-case `BaseClass &b = <cond> ? DerivedVar1 : DerivedVar2;`
  // is handled, too. The implicit cast happens outside of the conditional.
  // This is matched by `IgnoreDerivedToBase(canResolveToExpr(InnerMatcher))`
  // below.
  const auto ConditionalOperatorM = [Target](const Expr *E) {
    if (const auto *OP = dyn_cast<ConditionalOperator>(E)) {
      if (const auto *TE = OP->getTrueExpr()->IgnoreParens())
        if (canExprResolveTo(TE, Target))
          return true;
      if (const auto *FE = OP->getFalseExpr()->IgnoreParens())
        if (canExprResolveTo(FE, Target))
          return true;
    }
    return false;
  };

  const auto ElvisOperator = [Target](const Expr *E) {
    if (const auto *OP = dyn_cast<BinaryConditionalOperator>(E)) {
      if (const auto *TE = OP->getTrueExpr()->IgnoreParens())
        if (canExprResolveTo(TE, Target))
          return true;
      if (const auto *FE = OP->getFalseExpr()->IgnoreParens())
        if (canExprResolveTo(FE, Target))
          return true;
    }
    return false;
  };

  const Expr *SourceExprP = Source->IgnoreParens();
  return IgnoreDerivedToBase(SourceExprP,
                             [&](const Expr *E) {
                               return E == Target || ConditionalOperatorM(E) ||
                                      ElvisOperator(E);
                             }) ||
         EvalCommaExpr(SourceExprP, [&](const Expr *E) {
           return IgnoreDerivedToBase(
               E->IgnoreParens(), [&](const Expr *EE) { return EE == Target; });
         });
}

namespace {

AST_MATCHER_P(LambdaExpr, hasCaptureInit, const Expr *, E) {
  return llvm::is_contained(Node.capture_inits(), E);
}

AST_MATCHER_P(CXXForRangeStmt, hasRangeStmt,
              ast_matchers::internal::Matcher<DeclStmt>, InnerMatcher) {
  const DeclStmt *const Range = Node.getRangeStmt();
  return InnerMatcher.matches(*Range, Finder, Builder);
}

AST_MATCHER_P(Stmt, canResolveToExpr, const Stmt *, Inner) {
  auto *Exp = dyn_cast<Expr>(&Node);
  if (!Exp)
    return true;
  auto *Target = dyn_cast<Expr>(Inner);
  if (!Target)
    return false;
  return canExprResolveTo(Exp, Target);
}

// Similar to 'hasAnyArgument', but does not work because 'InitListExpr' does
// not have the 'arguments()' method.
AST_MATCHER_P(InitListExpr, hasAnyInit, ast_matchers::internal::Matcher<Expr>,
              InnerMatcher) {
  for (const Expr *Arg : Node.inits()) {
    ast_matchers::internal::BoundNodesTreeBuilder Result(*Builder);
    if (InnerMatcher.matches(*Arg, Finder, &Result)) {
      *Builder = std::move(Result);
      return true;
    }
  }
  return false;
}

const ast_matchers::internal::VariadicDynCastAllOfMatcher<Stmt, CXXTypeidExpr>
    cxxTypeidExpr;

AST_MATCHER(CXXTypeidExpr, isPotentiallyEvaluated) {
  return Node.isPotentiallyEvaluated();
}

AST_MATCHER(CXXMemberCallExpr, isConstCallee) {
  const Decl *CalleeDecl = Node.getCalleeDecl();
  const auto *VD = dyn_cast_or_null<ValueDecl>(CalleeDecl);
  if (!VD)
    return false;
  const QualType T = VD->getType().getCanonicalType();
  const auto *MPT = dyn_cast<MemberPointerType>(T);
  const auto *FPT = MPT ? cast<FunctionProtoType>(MPT->getPointeeType())
                        : dyn_cast<FunctionProtoType>(T);
  if (!FPT)
    return false;
  return FPT->isConst();
}

AST_MATCHER_P(GenericSelectionExpr, hasControllingExpr,
              ast_matchers::internal::Matcher<Expr>, InnerMatcher) {
  if (Node.isTypePredicate())
    return false;
  return InnerMatcher.matches(*Node.getControllingExpr(), Finder, Builder);
}

template <typename T>
ast_matchers::internal::Matcher<T>
findFirst(const ast_matchers::internal::Matcher<T> &Matcher) {
  return anyOf(Matcher, hasDescendant(Matcher));
}

const auto nonConstReferenceType = [] {
  return hasUnqualifiedDesugaredType(
      referenceType(pointee(unless(isConstQualified()))));
};

const auto nonConstPointerType = [] {
  return hasUnqualifiedDesugaredType(
      pointerType(pointee(unless(isConstQualified()))));
};

const auto isMoveOnly = [] {
  return cxxRecordDecl(
      hasMethod(cxxConstructorDecl(isMoveConstructor(), unless(isDeleted()))),
      hasMethod(cxxMethodDecl(isMoveAssignmentOperator(), unless(isDeleted()))),
      unless(anyOf(hasMethod(cxxConstructorDecl(isCopyConstructor(),
                                                unless(isDeleted()))),
                   hasMethod(cxxMethodDecl(isCopyAssignmentOperator(),
                                           unless(isDeleted()))))));
};

template <class T> struct NodeID;
template <> struct NodeID<Expr> { static constexpr StringRef value = "expr"; };
template <> struct NodeID<Decl> { static constexpr StringRef value = "decl"; };
constexpr StringRef NodeID<Expr>::value;
constexpr StringRef NodeID<Decl>::value;

template <class T,
          class F = const Stmt *(ExprMutationAnalyzer::Analyzer::*)(const T *)>
const Stmt *tryEachMatch(ArrayRef<ast_matchers::BoundNodes> Matches,
                         ExprMutationAnalyzer::Analyzer *Analyzer, F Finder) {
  const StringRef ID = NodeID<T>::value;
  for (const auto &Nodes : Matches) {
    if (const Stmt *S = (Analyzer->*Finder)(Nodes.getNodeAs<T>(ID)))
      return S;
  }
  return nullptr;
}

} // namespace

const Stmt *ExprMutationAnalyzer::Analyzer::findMutation(const Expr *Exp) {
  return findMutationMemoized(
      Exp,
      {&ExprMutationAnalyzer::Analyzer::findDirectMutation,
       &ExprMutationAnalyzer::Analyzer::findMemberMutation,
       &ExprMutationAnalyzer::Analyzer::findArrayElementMutation,
       &ExprMutationAnalyzer::Analyzer::findCastMutation,
       &ExprMutationAnalyzer::Analyzer::findRangeLoopMutation,
       &ExprMutationAnalyzer::Analyzer::findReferenceMutation,
       &ExprMutationAnalyzer::Analyzer::findFunctionArgMutation},
      Memorized.Results);
}

const Stmt *ExprMutationAnalyzer::Analyzer::findMutation(const Decl *Dec) {
  return tryEachDeclRef(Dec, &ExprMutationAnalyzer::Analyzer::findMutation);
}

const Stmt *
ExprMutationAnalyzer::Analyzer::findPointeeMutation(const Expr *Exp) {
  return findMutationMemoized(Exp, {/*TODO*/}, Memorized.PointeeResults);
}

const Stmt *
ExprMutationAnalyzer::Analyzer::findPointeeMutation(const Decl *Dec) {
  return tryEachDeclRef(Dec,
                        &ExprMutationAnalyzer::Analyzer::findPointeeMutation);
}

const Stmt *ExprMutationAnalyzer::Analyzer::findMutationMemoized(
    const Expr *Exp, llvm::ArrayRef<MutationFinder> Finders,
    Memoized::ResultMap &MemoizedResults) {
  const auto Memoized = MemoizedResults.find(Exp);
  if (Memoized != MemoizedResults.end())
    return Memoized->second;

  // Assume Exp is not mutated before analyzing Exp.
  MemoizedResults[Exp] = nullptr;
  if (isUnevaluated(Exp))
    return nullptr;

  for (const auto &Finder : Finders) {
    if (const Stmt *S = (this->*Finder)(Exp))
      return MemoizedResults[Exp] = S;
  }

  return nullptr;
}

const Stmt *
ExprMutationAnalyzer::Analyzer::tryEachDeclRef(const Decl *Dec,
                                               MutationFinder Finder) {
  const auto Refs = match(
      findAll(
          declRefExpr(to(
                          // `Dec` or a binding if `Dec` is a decomposition.
                          anyOf(equalsNode(Dec),
                                bindingDecl(forDecomposition(equalsNode(Dec))))
                          //
                          ))
              .bind(NodeID<Expr>::value)),
      Stm, Context);
  for (const auto &RefNodes : Refs) {
    const auto *E = RefNodes.getNodeAs<Expr>(NodeID<Expr>::value);
    if ((this->*Finder)(E))
      return E;
  }
  return nullptr;
}

bool ExprMutationAnalyzer::Analyzer::isUnevaluated(const Stmt *Exp,
                                                   const Stmt &Stm,
                                                   ASTContext &Context) {
  return selectFirst<Stmt>(
             NodeID<Expr>::value,
             match(
                 findFirst(
                     stmt(canResolveToExpr(Exp),
                          anyOf(
                              // `Exp` is part of the underlying expression of
                              // decltype/typeof if it has an ancestor of
                              // typeLoc.
                              hasAncestor(typeLoc(unless(
                                  hasAncestor(unaryExprOrTypeTraitExpr())))),
                              hasAncestor(expr(anyOf(
                                  // `UnaryExprOrTypeTraitExpr` is unevaluated
                                  // unless it's sizeof on VLA.
                                  unaryExprOrTypeTraitExpr(unless(sizeOfExpr(
                                      hasArgumentOfType(variableArrayType())))),
                                  // `CXXTypeidExpr` is unevaluated unless it's
                                  // applied to an expression of glvalue of
                                  // polymorphic class type.
                                  cxxTypeidExpr(
                                      unless(isPotentiallyEvaluated())),
                                  // The controlling expression of
                                  // `GenericSelectionExpr` is unevaluated.
                                  genericSelectionExpr(hasControllingExpr(
                                      hasDescendant(equalsNode(Exp)))),
                                  cxxNoexceptExpr())))))
                         .bind(NodeID<Expr>::value)),
                 Stm, Context)) != nullptr;
}

bool ExprMutationAnalyzer::Analyzer::isUnevaluated(const Expr *Exp) {
  return isUnevaluated(Exp, Stm, Context);
}

const Stmt *
ExprMutationAnalyzer::Analyzer::findExprMutation(ArrayRef<BoundNodes> Matches) {
  return tryEachMatch<Expr>(Matches, this,
                            &ExprMutationAnalyzer::Analyzer::findMutation);
}

const Stmt *
ExprMutationAnalyzer::Analyzer::findDeclMutation(ArrayRef<BoundNodes> Matches) {
  return tryEachMatch<Decl>(Matches, this,
                            &ExprMutationAnalyzer::Analyzer::findMutation);
}

const Stmt *ExprMutationAnalyzer::Analyzer::findExprPointeeMutation(
    ArrayRef<ast_matchers::BoundNodes> Matches) {
  return tryEachMatch<Expr>(
      Matches, this, &ExprMutationAnalyzer::Analyzer::findPointeeMutation);
}

const Stmt *ExprMutationAnalyzer::Analyzer::findDeclPointeeMutation(
    ArrayRef<ast_matchers::BoundNodes> Matches) {
  return tryEachMatch<Decl>(
      Matches, this, &ExprMutationAnalyzer::Analyzer::findPointeeMutation);
}

const Stmt *
ExprMutationAnalyzer::Analyzer::findDirectMutation(const Expr *Exp) {
  // LHS of any assignment operators.
  const auto AsAssignmentLhs =
      binaryOperator(isAssignmentOperator(), hasLHS(canResolveToExpr(Exp)));

  // Operand of increment/decrement operators.
  const auto AsIncDecOperand =
      unaryOperator(anyOf(hasOperatorName("++"), hasOperatorName("--")),
                    hasUnaryOperand(canResolveToExpr(Exp)));

  // Invoking non-const member function.
  // A member function is assumed to be non-const when it is unresolved.
  const auto NonConstMethod = cxxMethodDecl(unless(isConst()));

  const auto AsNonConstThis = expr(anyOf(
      cxxMemberCallExpr(on(canResolveToExpr(Exp)), unless(isConstCallee())),
      cxxOperatorCallExpr(callee(NonConstMethod),
                          hasArgument(0, canResolveToExpr(Exp))),
      // In case of a templated type, calling overloaded operators is not
      // resolved and modelled as `binaryOperator` on a dependent type.
      // Such instances are considered a modification, because they can modify
      // in different instantiations of the template.
      binaryOperator(isTypeDependent(),
                     hasEitherOperand(ignoringImpCasts(canResolveToExpr(Exp)))),
      // A fold expression may contain `Exp` as it's initializer.
      // We don't know if the operator modifies `Exp` because the
      // operator is type dependent due to the parameter pack.
      cxxFoldExpr(hasFoldInit(ignoringImpCasts(canResolveToExpr(Exp)))),
      // Within class templates and member functions the member expression might
      // not be resolved. In that case, the `callExpr` is considered to be a
      // modification.
      callExpr(callee(expr(anyOf(
          unresolvedMemberExpr(hasObjectExpression(canResolveToExpr(Exp))),
          cxxDependentScopeMemberExpr(
              hasObjectExpression(canResolveToExpr(Exp))))))),
      // Match on a call to a known method, but the call itself is type
      // dependent (e.g. `vector<T> v; v.push(T{});` in a templated function).
      callExpr(allOf(
          isTypeDependent(),
          callee(memberExpr(hasDeclaration(NonConstMethod),
                            hasObjectExpression(canResolveToExpr(Exp))))))));

  // Taking address of 'Exp'.
  // We're assuming 'Exp' is mutated as soon as its address is taken, though in
  // theory we can follow the pointer and see whether it escaped `Stm` or is
  // dereferenced and then mutated. This is left for future improvements.
  const auto AsAmpersandOperand =
      unaryOperator(hasOperatorName("&"),
                    // A NoOp implicit cast is adding const.
                    unless(hasParent(implicitCastExpr(hasCastKind(CK_NoOp)))),
                    hasUnaryOperand(canResolveToExpr(Exp)));
  const auto AsPointerFromArrayDecay = castExpr(
      hasCastKind(CK_ArrayToPointerDecay),
      unless(hasParent(arraySubscriptExpr())), has(canResolveToExpr(Exp)));
  // Treat calling `operator->()` of move-only classes as taking address.
  // These are typically smart pointers with unique ownership so we treat
  // mutation of pointee as mutation of the smart pointer itself.
  const auto AsOperatorArrowThis = cxxOperatorCallExpr(
      hasOverloadedOperatorName("->"),
      callee(
          cxxMethodDecl(ofClass(isMoveOnly()), returns(nonConstPointerType()))),
      argumentCountIs(1), hasArgument(0, canResolveToExpr(Exp)));

  // Used as non-const-ref argument when calling a function.
  // An argument is assumed to be non-const-ref when the function is unresolved.
  // Instantiated template functions are not handled here but in
  // findFunctionArgMutation which has additional smarts for handling forwarding
  // references.
  const auto NonConstRefParam = forEachArgumentWithParamType(
      anyOf(canResolveToExpr(Exp),
            memberExpr(hasObjectExpression(canResolveToExpr(Exp)))),
      nonConstReferenceType());
  const auto NotInstantiated = unless(hasDeclaration(isInstantiated()));

  const auto AsNonConstRefArg =
      anyOf(callExpr(NonConstRefParam, NotInstantiated),
            cxxConstructExpr(NonConstRefParam, NotInstantiated),
            // If the call is type-dependent, we can't properly process any
            // argument because required type conversions and implicit casts
            // will be inserted only after specialization.
            callExpr(isTypeDependent(), hasAnyArgument(canResolveToExpr(Exp))),
            cxxUnresolvedConstructExpr(hasAnyArgument(canResolveToExpr(Exp))),
            // Previous False Positive in the following Code:
            // `template <typename T> void f() { int i = 42; new Type<T>(i); }`
            // Where the constructor of `Type` takes its argument as reference.
            // The AST does not resolve in a `cxxConstructExpr` because it is
            // type-dependent.
            parenListExpr(hasDescendant(expr(canResolveToExpr(Exp)))),
            // If the initializer is for a reference type, there is no cast for
            // the variable. Values are cast to RValue first.
            initListExpr(hasAnyInit(expr(canResolveToExpr(Exp)))));

  // Captured by a lambda by reference.
  // If we're initializing a capture with 'Exp' directly then we're initializing
  // a reference capture.
  // For value captures there will be an ImplicitCastExpr <LValueToRValue>.
  const auto AsLambdaRefCaptureInit = lambdaExpr(hasCaptureInit(Exp));

  // Returned as non-const-ref.
  // If we're returning 'Exp' directly then it's returned as non-const-ref.
  // For returning by value there will be an ImplicitCastExpr <LValueToRValue>.
  // For returning by const-ref there will be an ImplicitCastExpr <NoOp> (for
  // adding const.)
  const auto AsNonConstRefReturn =
      returnStmt(hasReturnValue(canResolveToExpr(Exp)));

  // It is used as a non-const-reference for initializing a range-for loop.
  const auto AsNonConstRefRangeInit = cxxForRangeStmt(hasRangeInit(declRefExpr(
      allOf(canResolveToExpr(Exp), hasType(nonConstReferenceType())))));

  const auto Matches = match(
      traverse(
          TK_AsIs,
          findFirst(stmt(anyOf(AsAssignmentLhs, AsIncDecOperand, AsNonConstThis,
                               AsAmpersandOperand, AsPointerFromArrayDecay,
                               AsOperatorArrowThis, AsNonConstRefArg,
                               AsLambdaRefCaptureInit, AsNonConstRefReturn,
                               AsNonConstRefRangeInit))
                        .bind("stmt"))),
      Stm, Context);
  return selectFirst<Stmt>("stmt", Matches);
}

const Stmt *
ExprMutationAnalyzer::Analyzer::findMemberMutation(const Expr *Exp) {
  // Check whether any member of 'Exp' is mutated.
  const auto MemberExprs = match(
      findAll(expr(anyOf(memberExpr(hasObjectExpression(canResolveToExpr(Exp))),
                         cxxDependentScopeMemberExpr(
                             hasObjectExpression(canResolveToExpr(Exp))),
                         binaryOperator(hasOperatorName(".*"),
                                        hasLHS(equalsNode(Exp)))))
                  .bind(NodeID<Expr>::value)),
      Stm, Context);
  return findExprMutation(MemberExprs);
}

const Stmt *
ExprMutationAnalyzer::Analyzer::findArrayElementMutation(const Expr *Exp) {
  // Check whether any element of an array is mutated.
  const auto SubscriptExprs = match(
      findAll(arraySubscriptExpr(
                  anyOf(hasBase(canResolveToExpr(Exp)),
                        hasBase(implicitCastExpr(allOf(
                            hasCastKind(CK_ArrayToPointerDecay),
                            hasSourceExpression(canResolveToExpr(Exp)))))))
                  .bind(NodeID<Expr>::value)),
      Stm, Context);
  return findExprMutation(SubscriptExprs);
}

const Stmt *ExprMutationAnalyzer::Analyzer::findCastMutation(const Expr *Exp) {
  // If the 'Exp' is explicitly casted to a non-const reference type the
  // 'Exp' is considered to be modified.
  const auto ExplicitCast =
      match(findFirst(stmt(castExpr(hasSourceExpression(canResolveToExpr(Exp)),
                                    explicitCastExpr(hasDestinationType(
                                        nonConstReferenceType()))))
                          .bind("stmt")),
            Stm, Context);

  if (const auto *CastStmt = selectFirst<Stmt>("stmt", ExplicitCast))
    return CastStmt;

  // If 'Exp' is casted to any non-const reference type, check the castExpr.
  const auto Casts = match(
      findAll(expr(castExpr(hasSourceExpression(canResolveToExpr(Exp)),
                            anyOf(explicitCastExpr(hasDestinationType(
                                      nonConstReferenceType())),
                                  implicitCastExpr(hasImplicitDestinationType(
                                      nonConstReferenceType())))))
                  .bind(NodeID<Expr>::value)),
      Stm, Context);

  if (const Stmt *S = findExprMutation(Casts))
    return S;
  // Treat std::{move,forward} as cast.
  const auto Calls =
      match(findAll(callExpr(callee(namedDecl(
                                 hasAnyName("::std::move", "::std::forward"))),
                             hasArgument(0, canResolveToExpr(Exp)))
                        .bind("expr")),
            Stm, Context);
  return findExprMutation(Calls);
}

const Stmt *
ExprMutationAnalyzer::Analyzer::findRangeLoopMutation(const Expr *Exp) {
  // Keep the ordering for the specific initialization matches to happen first,
  // because it is cheaper to match all potential modifications of the loop
  // variable.

  // The range variable is a reference to a builtin array. In that case the
  // array is considered modified if the loop-variable is a non-const reference.
  const auto DeclStmtToNonRefToArray = declStmt(hasSingleDecl(varDecl(hasType(
      hasUnqualifiedDesugaredType(referenceType(pointee(arrayType())))))));
  const auto RefToArrayRefToElements = match(
      findFirst(stmt(cxxForRangeStmt(
                         hasLoopVariable(
                             varDecl(anyOf(hasType(nonConstReferenceType()),
                                           hasType(nonConstPointerType())))
                                 .bind(NodeID<Decl>::value)),
                         hasRangeStmt(DeclStmtToNonRefToArray),
                         hasRangeInit(canResolveToExpr(Exp))))
                    .bind("stmt")),
      Stm, Context);

  if (const auto *BadRangeInitFromArray =
          selectFirst<Stmt>("stmt", RefToArrayRefToElements))
    return BadRangeInitFromArray;

  // Small helper to match special cases in range-for loops.
  //
  // It is possible that containers do not provide a const-overload for their
  // iterator accessors. If this is the case, the variable is used non-const
  // no matter what happens in the loop. This requires special detection as it
  // is then faster to find all mutations of the loop variable.
  // It aims at a different modification as well.
  const auto HasAnyNonConstIterator =
      anyOf(allOf(hasMethod(allOf(hasName("begin"), unless(isConst()))),
                  unless(hasMethod(allOf(hasName("begin"), isConst())))),
            allOf(hasMethod(allOf(hasName("end"), unless(isConst()))),
                  unless(hasMethod(allOf(hasName("end"), isConst())))));

  const auto DeclStmtToNonConstIteratorContainer = declStmt(
      hasSingleDecl(varDecl(hasType(hasUnqualifiedDesugaredType(referenceType(
          pointee(hasDeclaration(cxxRecordDecl(HasAnyNonConstIterator)))))))));

  const auto RefToContainerBadIterators = match(
      findFirst(stmt(cxxForRangeStmt(allOf(
                         hasRangeStmt(DeclStmtToNonConstIteratorContainer),
                         hasRangeInit(canResolveToExpr(Exp)))))
                    .bind("stmt")),
      Stm, Context);

  if (const auto *BadIteratorsContainer =
          selectFirst<Stmt>("stmt", RefToContainerBadIterators))
    return BadIteratorsContainer;

  // If range for looping over 'Exp' with a non-const reference loop variable,
  // check all declRefExpr of the loop variable.
  const auto LoopVars =
      match(findAll(cxxForRangeStmt(
                hasLoopVariable(varDecl(hasType(nonConstReferenceType()))
                                    .bind(NodeID<Decl>::value)),
                hasRangeInit(canResolveToExpr(Exp)))),
            Stm, Context);
  return findDeclMutation(LoopVars);
}

const Stmt *
ExprMutationAnalyzer::Analyzer::findReferenceMutation(const Expr *Exp) {
  // Follow non-const reference returned by `operator*()` of move-only classes.
  // These are typically smart pointers with unique ownership so we treat
  // mutation of pointee as mutation of the smart pointer itself.
  const auto Ref = match(
      findAll(cxxOperatorCallExpr(
                  hasOverloadedOperatorName("*"),
                  callee(cxxMethodDecl(ofClass(isMoveOnly()),
                                       returns(nonConstReferenceType()))),
                  argumentCountIs(1), hasArgument(0, canResolveToExpr(Exp)))
                  .bind(NodeID<Expr>::value)),
      Stm, Context);
  if (const Stmt *S = findExprMutation(Ref))
    return S;

  // If 'Exp' is bound to a non-const reference, check all declRefExpr to that.
  const auto Refs = match(
      stmt(forEachDescendant(
          varDecl(hasType(nonConstReferenceType()),
                  hasInitializer(anyOf(
                      canResolveToExpr(Exp),
                      memberExpr(hasObjectExpression(canResolveToExpr(Exp))))),
                  hasParent(declStmt().bind("stmt")),
                  // Don't follow the reference in range statement, we've
                  // handled that separately.
                  unless(hasParent(declStmt(hasParent(cxxForRangeStmt(
                      hasRangeStmt(equalsBoundNode("stmt"))))))))
              .bind(NodeID<Decl>::value))),
      Stm, Context);
  return findDeclMutation(Refs);
}

const Stmt *
ExprMutationAnalyzer::Analyzer::findFunctionArgMutation(const Expr *Exp) {
  const auto NonConstRefParam = forEachArgumentWithParam(
      canResolveToExpr(Exp),
      parmVarDecl(hasType(nonConstReferenceType())).bind("parm"));
  const auto IsInstantiated = hasDeclaration(isInstantiated());
  const auto FuncDecl = hasDeclaration(functionDecl().bind("func"));
  const auto Matches = match(
      traverse(
          TK_AsIs,
          findAll(
              expr(anyOf(callExpr(NonConstRefParam, IsInstantiated, FuncDecl,
                                  unless(callee(namedDecl(hasAnyName(
                                      "::std::move", "::std::forward"))))),
                         cxxConstructExpr(NonConstRefParam, IsInstantiated,
                                          FuncDecl)))
                  .bind(NodeID<Expr>::value))),
      Stm, Context);
  for (const auto &Nodes : Matches) {
    const auto *Exp = Nodes.getNodeAs<Expr>(NodeID<Expr>::value);
    const auto *Func = Nodes.getNodeAs<FunctionDecl>("func");
    if (!Func->getBody() || !Func->getPrimaryTemplate())
      return Exp;

    const auto *Parm = Nodes.getNodeAs<ParmVarDecl>("parm");
    const ArrayRef<ParmVarDecl *> AllParams =
        Func->getPrimaryTemplate()->getTemplatedDecl()->parameters();
    QualType ParmType =
        AllParams[std::min<size_t>(Parm->getFunctionScopeIndex(),
                                   AllParams.size() - 1)]
            ->getType();
    if (const auto *T = ParmType->getAs<PackExpansionType>())
      ParmType = T->getPattern();

    // If param type is forwarding reference, follow into the function
    // definition and see whether the param is mutated inside.
    if (const auto *RefType = ParmType->getAs<RValueReferenceType>()) {
      if (!RefType->getPointeeType().getQualifiers() &&
          RefType->getPointeeType()->getAs<TemplateTypeParmType>()) {
        FunctionParmMutationAnalyzer *Analyzer =
            FunctionParmMutationAnalyzer::getFunctionParmMutationAnalyzer(
                *Func, Context, Memorized);
        if (Analyzer->findMutation(Parm))
          return Exp;
        continue;
      }
    }
    // Not forwarding reference.
    return Exp;
  }
  return nullptr;
}

FunctionParmMutationAnalyzer::FunctionParmMutationAnalyzer(
    const FunctionDecl &Func, ASTContext &Context,
    ExprMutationAnalyzer::Memoized &Memorized)
    : BodyAnalyzer(*Func.getBody(), Context, Memorized) {
  if (const auto *Ctor = dyn_cast<CXXConstructorDecl>(&Func)) {
    // CXXCtorInitializer might also mutate Param but they're not part of
    // function body, check them eagerly here since they're typically trivial.
    for (const CXXCtorInitializer *Init : Ctor->inits()) {
      ExprMutationAnalyzer::Analyzer InitAnalyzer(*Init->getInit(), Context,
                                                  Memorized);
      for (const ParmVarDecl *Parm : Ctor->parameters()) {
        if (Results.contains(Parm))
          continue;
        if (const Stmt *S = InitAnalyzer.findMutation(Parm))
          Results[Parm] = S;
      }
    }
  }
}

const Stmt *
FunctionParmMutationAnalyzer::findMutation(const ParmVarDecl *Parm) {
  const auto Memoized = Results.find(Parm);
  if (Memoized != Results.end())
    return Memoized->second;
  // To handle call A -> call B -> call A. Assume parameters of A is not mutated
  // before analyzing parameters of A. Then when analyzing the second "call A",
  // FunctionParmMutationAnalyzer can use this memoized value to avoid infinite
  // recursion.
  Results[Parm] = nullptr;
  if (const Stmt *S = BodyAnalyzer.findMutation(Parm))
    return Results[Parm] = S;
  return Results[Parm];
}

} // namespace clang
