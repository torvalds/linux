//===--- LoopUnrolling.cpp - Unroll loops -----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// This file contains functions which are used to decide if a loop worth to be
/// unrolled. Moreover, these functions manages the stack of loop which is
/// tracked by the ProgramState.
///
//===----------------------------------------------------------------------===//

#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/LoopUnrolling.h"
#include <optional>

using namespace clang;
using namespace ento;
using namespace clang::ast_matchers;

static const int MAXIMUM_STEP_UNROLLED = 128;

namespace {
struct LoopState {
private:
  enum Kind { Normal, Unrolled } K;
  const Stmt *LoopStmt;
  const LocationContext *LCtx;
  unsigned maxStep;
  LoopState(Kind InK, const Stmt *S, const LocationContext *L, unsigned N)
      : K(InK), LoopStmt(S), LCtx(L), maxStep(N) {}

public:
  static LoopState getNormal(const Stmt *S, const LocationContext *L,
                             unsigned N) {
    return LoopState(Normal, S, L, N);
  }
  static LoopState getUnrolled(const Stmt *S, const LocationContext *L,
                               unsigned N) {
    return LoopState(Unrolled, S, L, N);
  }
  bool isUnrolled() const { return K == Unrolled; }
  unsigned getMaxStep() const { return maxStep; }
  const Stmt *getLoopStmt() const { return LoopStmt; }
  const LocationContext *getLocationContext() const { return LCtx; }
  bool operator==(const LoopState &X) const {
    return K == X.K && LoopStmt == X.LoopStmt;
  }
  void Profile(llvm::FoldingSetNodeID &ID) const {
    ID.AddInteger(K);
    ID.AddPointer(LoopStmt);
    ID.AddPointer(LCtx);
    ID.AddInteger(maxStep);
  }
};
} // namespace

// The tracked stack of loops. The stack indicates that which loops the
// simulated element contained by. The loops are marked depending if we decided
// to unroll them.
// TODO: The loop stack should not need to be in the program state since it is
// lexical in nature. Instead, the stack of loops should be tracked in the
// LocationContext.
REGISTER_LIST_WITH_PROGRAMSTATE(LoopStack, LoopState)

namespace clang {
namespace ento {

static bool isLoopStmt(const Stmt *S) {
  return isa_and_nonnull<ForStmt, WhileStmt, DoStmt>(S);
}

ProgramStateRef processLoopEnd(const Stmt *LoopStmt, ProgramStateRef State) {
  auto LS = State->get<LoopStack>();
  if (!LS.isEmpty() && LS.getHead().getLoopStmt() == LoopStmt)
    State = State->set<LoopStack>(LS.getTail());
  return State;
}

static internal::Matcher<Stmt> simpleCondition(StringRef BindName,
                                               StringRef RefName) {
  return binaryOperator(
             anyOf(hasOperatorName("<"), hasOperatorName(">"),
                   hasOperatorName("<="), hasOperatorName(">="),
                   hasOperatorName("!=")),
             hasEitherOperand(ignoringParenImpCasts(
                 declRefExpr(to(varDecl(hasType(isInteger())).bind(BindName)))
                     .bind(RefName))),
             hasEitherOperand(
                 ignoringParenImpCasts(integerLiteral().bind("boundNum"))))
      .bind("conditionOperator");
}

static internal::Matcher<Stmt>
changeIntBoundNode(internal::Matcher<Decl> VarNodeMatcher) {
  return anyOf(
      unaryOperator(anyOf(hasOperatorName("--"), hasOperatorName("++")),
                    hasUnaryOperand(ignoringParenImpCasts(
                        declRefExpr(to(varDecl(VarNodeMatcher)))))),
      binaryOperator(isAssignmentOperator(),
                     hasLHS(ignoringParenImpCasts(
                         declRefExpr(to(varDecl(VarNodeMatcher)))))));
}

static internal::Matcher<Stmt>
callByRef(internal::Matcher<Decl> VarNodeMatcher) {
  return callExpr(forEachArgumentWithParam(
      declRefExpr(to(varDecl(VarNodeMatcher))),
      parmVarDecl(hasType(references(qualType(unless(isConstQualified())))))));
}

static internal::Matcher<Stmt>
assignedToRef(internal::Matcher<Decl> VarNodeMatcher) {
  return declStmt(hasDescendant(varDecl(
      allOf(hasType(referenceType()),
            hasInitializer(anyOf(
                initListExpr(has(declRefExpr(to(varDecl(VarNodeMatcher))))),
                declRefExpr(to(varDecl(VarNodeMatcher)))))))));
}

static internal::Matcher<Stmt>
getAddrTo(internal::Matcher<Decl> VarNodeMatcher) {
  return unaryOperator(
      hasOperatorName("&"),
      hasUnaryOperand(declRefExpr(hasDeclaration(VarNodeMatcher))));
}

static internal::Matcher<Stmt> hasSuspiciousStmt(StringRef NodeName) {
  return hasDescendant(stmt(
      anyOf(gotoStmt(), switchStmt(), returnStmt(),
            // Escaping and not known mutation of the loop counter is handled
            // by exclusion of assigning and address-of operators and
            // pass-by-ref function calls on the loop counter from the body.
            changeIntBoundNode(equalsBoundNode(std::string(NodeName))),
            callByRef(equalsBoundNode(std::string(NodeName))),
            getAddrTo(equalsBoundNode(std::string(NodeName))),
            assignedToRef(equalsBoundNode(std::string(NodeName))))));
}

static internal::Matcher<Stmt> forLoopMatcher() {
  return forStmt(
             hasCondition(simpleCondition("initVarName", "initVarRef")),
             // Initialization should match the form: 'int i = 6' or 'i = 42'.
             hasLoopInit(
                 anyOf(declStmt(hasSingleDecl(
                           varDecl(allOf(hasInitializer(ignoringParenImpCasts(
                                             integerLiteral().bind("initNum"))),
                                         equalsBoundNode("initVarName"))))),
                       binaryOperator(hasLHS(declRefExpr(to(varDecl(
                                          equalsBoundNode("initVarName"))))),
                                      hasRHS(ignoringParenImpCasts(
                                          integerLiteral().bind("initNum")))))),
             // Incrementation should be a simple increment or decrement
             // operator call.
             hasIncrement(unaryOperator(
                 anyOf(hasOperatorName("++"), hasOperatorName("--")),
                 hasUnaryOperand(declRefExpr(
                     to(varDecl(allOf(equalsBoundNode("initVarName"),
                                      hasType(isInteger())))))))),
             unless(hasBody(hasSuspiciousStmt("initVarName"))))
      .bind("forLoop");
}

static bool isCapturedByReference(ExplodedNode *N, const DeclRefExpr *DR) {

  // Get the lambda CXXRecordDecl
  assert(DR->refersToEnclosingVariableOrCapture());
  const LocationContext *LocCtxt = N->getLocationContext();
  const Decl *D = LocCtxt->getDecl();
  const auto *MD = cast<CXXMethodDecl>(D);
  assert(MD && MD->getParent()->isLambda() &&
         "Captured variable should only be seen while evaluating a lambda");
  const CXXRecordDecl *LambdaCXXRec = MD->getParent();

  // Lookup the fields of the lambda
  llvm::DenseMap<const ValueDecl *, FieldDecl *> LambdaCaptureFields;
  FieldDecl *LambdaThisCaptureField;
  LambdaCXXRec->getCaptureFields(LambdaCaptureFields, LambdaThisCaptureField);

  // Check if the counter is captured by reference
  const VarDecl *VD = cast<VarDecl>(DR->getDecl()->getCanonicalDecl());
  assert(VD);
  const FieldDecl *FD = LambdaCaptureFields[VD];
  assert(FD && "Captured variable without a corresponding field");
  return FD->getType()->isReferenceType();
}

static bool isFoundInStmt(const Stmt *S, const VarDecl *VD) {
  if (const DeclStmt *DS = dyn_cast<DeclStmt>(S)) {
    for (const Decl *D : DS->decls()) {
      // Once we reach the declaration of the VD we can return.
      if (D->getCanonicalDecl() == VD)
        return true;
    }
  }
  return false;
}

// A loop counter is considered escaped if:
// case 1: It is a global variable.
// case 2: It is a reference parameter or a reference capture.
// case 3: It is assigned to a non-const reference variable or parameter.
// case 4: Has its address taken.
static bool isPossiblyEscaped(ExplodedNode *N, const DeclRefExpr *DR) {
  const VarDecl *VD = cast<VarDecl>(DR->getDecl()->getCanonicalDecl());
  assert(VD);
  // Case 1:
  if (VD->hasGlobalStorage())
    return true;

  const bool IsRefParamOrCapture =
      isa<ParmVarDecl>(VD) || DR->refersToEnclosingVariableOrCapture();
  // Case 2:
  if ((DR->refersToEnclosingVariableOrCapture() &&
       isCapturedByReference(N, DR)) ||
      (IsRefParamOrCapture && VD->getType()->isReferenceType()))
    return true;

  while (!N->pred_empty()) {
    // FIXME: getStmtForDiagnostics() does nasty things in order to provide
    // a valid statement for body farms, do we need this behavior here?
    const Stmt *S = N->getStmtForDiagnostics();
    if (!S) {
      N = N->getFirstPred();
      continue;
    }

    if (isFoundInStmt(S, VD)) {
      return false;
    }

    if (const auto *SS = dyn_cast<SwitchStmt>(S)) {
      if (const auto *CST = dyn_cast<CompoundStmt>(SS->getBody())) {
        for (const Stmt *CB : CST->body()) {
          if (isFoundInStmt(CB, VD))
            return false;
        }
      }
    }

    // Check the usage of the pass-by-ref function calls and adress-of operator
    // on VD and reference initialized by VD.
    ASTContext &ASTCtx =
        N->getLocationContext()->getAnalysisDeclContext()->getASTContext();
    // Case 3 and 4:
    auto Match =
        match(stmt(anyOf(callByRef(equalsNode(VD)), getAddrTo(equalsNode(VD)),
                         assignedToRef(equalsNode(VD)))),
              *S, ASTCtx);
    if (!Match.empty())
      return true;

    N = N->getFirstPred();
  }

  // Reference parameter and reference capture will not be found.
  if (IsRefParamOrCapture)
    return false;

  llvm_unreachable("Reached root without finding the declaration of VD");
}

bool shouldCompletelyUnroll(const Stmt *LoopStmt, ASTContext &ASTCtx,
                            ExplodedNode *Pred, unsigned &maxStep) {

  if (!isLoopStmt(LoopStmt))
    return false;

  // TODO: Match the cases where the bound is not a concrete literal but an
  // integer with known value
  auto Matches = match(forLoopMatcher(), *LoopStmt, ASTCtx);
  if (Matches.empty())
    return false;

  const auto *CounterVarRef = Matches[0].getNodeAs<DeclRefExpr>("initVarRef");
  llvm::APInt BoundNum =
      Matches[0].getNodeAs<IntegerLiteral>("boundNum")->getValue();
  llvm::APInt InitNum =
      Matches[0].getNodeAs<IntegerLiteral>("initNum")->getValue();
  auto CondOp = Matches[0].getNodeAs<BinaryOperator>("conditionOperator");
  if (InitNum.getBitWidth() != BoundNum.getBitWidth()) {
    InitNum = InitNum.zext(BoundNum.getBitWidth());
    BoundNum = BoundNum.zext(InitNum.getBitWidth());
  }

  if (CondOp->getOpcode() == BO_GE || CondOp->getOpcode() == BO_LE)
    maxStep = (BoundNum - InitNum + 1).abs().getZExtValue();
  else
    maxStep = (BoundNum - InitNum).abs().getZExtValue();

  // Check if the counter of the loop is not escaped before.
  return !isPossiblyEscaped(Pred, CounterVarRef);
}

bool madeNewBranch(ExplodedNode *N, const Stmt *LoopStmt) {
  const Stmt *S = nullptr;
  while (!N->pred_empty()) {
    if (N->succ_size() > 1)
      return true;

    ProgramPoint P = N->getLocation();
    if (std::optional<BlockEntrance> BE = P.getAs<BlockEntrance>())
      S = BE->getBlock()->getTerminatorStmt();

    if (S == LoopStmt)
      return false;

    N = N->getFirstPred();
  }

  llvm_unreachable("Reached root without encountering the previous step");
}

// updateLoopStack is called on every basic block, therefore it needs to be fast
ProgramStateRef updateLoopStack(const Stmt *LoopStmt, ASTContext &ASTCtx,
                                ExplodedNode *Pred, unsigned maxVisitOnPath) {
  auto State = Pred->getState();
  auto LCtx = Pred->getLocationContext();

  if (!isLoopStmt(LoopStmt))
    return State;

  auto LS = State->get<LoopStack>();
  if (!LS.isEmpty() && LoopStmt == LS.getHead().getLoopStmt() &&
      LCtx == LS.getHead().getLocationContext()) {
    if (LS.getHead().isUnrolled() && madeNewBranch(Pred, LoopStmt)) {
      State = State->set<LoopStack>(LS.getTail());
      State = State->add<LoopStack>(
          LoopState::getNormal(LoopStmt, LCtx, maxVisitOnPath));
    }
    return State;
  }
  unsigned maxStep;
  if (!shouldCompletelyUnroll(LoopStmt, ASTCtx, Pred, maxStep)) {
    State = State->add<LoopStack>(
        LoopState::getNormal(LoopStmt, LCtx, maxVisitOnPath));
    return State;
  }

  unsigned outerStep = (LS.isEmpty() ? 1 : LS.getHead().getMaxStep());

  unsigned innerMaxStep = maxStep * outerStep;
  if (innerMaxStep > MAXIMUM_STEP_UNROLLED)
    State = State->add<LoopStack>(
        LoopState::getNormal(LoopStmt, LCtx, maxVisitOnPath));
  else
    State = State->add<LoopStack>(
        LoopState::getUnrolled(LoopStmt, LCtx, innerMaxStep));
  return State;
}

bool isUnrolledState(ProgramStateRef State) {
  auto LS = State->get<LoopStack>();
  if (LS.isEmpty() || !LS.getHead().isUnrolled())
    return false;
  return true;
}
}
}
