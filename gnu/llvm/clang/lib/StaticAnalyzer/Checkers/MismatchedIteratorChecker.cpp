//===-- MismatchedIteratorChecker.cpp -----------------------------*- C++ -*--//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Defines a checker for mistakenly applying a foreign iterator on a container
// and for using iterators of two different containers in a context where
// iterators of the same container should be used.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"


#include "Iterator.h"

using namespace clang;
using namespace ento;
using namespace iterator;

namespace {

class MismatchedIteratorChecker
  : public Checker<check::PreCall, check::PreStmt<BinaryOperator>> {

  const BugType MismatchedBugType{this, "Iterator(s) mismatched",
                                  "Misuse of STL APIs",
                                  /*SuppressOnSink=*/true};

  void verifyMatch(CheckerContext &C, SVal Iter, const MemRegion *Cont) const;
  void verifyMatch(CheckerContext &C, SVal Iter1, SVal Iter2) const;
  void reportBug(StringRef Message, SVal Val1, SVal Val2, CheckerContext &C,
                 ExplodedNode *ErrNode) const;
  void reportBug(StringRef Message, SVal Val, const MemRegion *Reg,
                 CheckerContext &C, ExplodedNode *ErrNode) const;

public:
  void checkPreCall(const CallEvent &Call, CheckerContext &C) const;
  void checkPreStmt(const BinaryOperator *BO, CheckerContext &C) const;

};

} // namespace

void MismatchedIteratorChecker::checkPreCall(const CallEvent &Call,
                                             CheckerContext &C) const {
  // Check for iterator mismatches
  const auto *Func = dyn_cast_or_null<FunctionDecl>(Call.getDecl());
  if (!Func)
    return;

  if (Func->isOverloadedOperator() &&
      isComparisonOperator(Func->getOverloadedOperator())) {
    // Check for comparisons of iterators of different containers
    if (const auto *InstCall = dyn_cast<CXXInstanceCall>(&Call)) {
      if (Call.getNumArgs() < 1)
        return;

      if (!isIteratorType(InstCall->getCXXThisExpr()->getType()) ||
          !isIteratorType(Call.getArgExpr(0)->getType()))
        return;

      verifyMatch(C, InstCall->getCXXThisVal(), Call.getArgSVal(0));
    } else {
      if (Call.getNumArgs() < 2)
        return;

      if (!isIteratorType(Call.getArgExpr(0)->getType()) ||
          !isIteratorType(Call.getArgExpr(1)->getType()))
        return;

      verifyMatch(C, Call.getArgSVal(0), Call.getArgSVal(1));
    }
  } else if (const auto *InstCall = dyn_cast<CXXInstanceCall>(&Call)) {
    const auto *ContReg = InstCall->getCXXThisVal().getAsRegion();
    if (!ContReg)
      return;
    // Check for erase, insert and emplace using iterator of another container
    if (isEraseCall(Func) || isEraseAfterCall(Func)) {
      verifyMatch(C, Call.getArgSVal(0),
                  InstCall->getCXXThisVal().getAsRegion());
      if (Call.getNumArgs() == 2) {
        verifyMatch(C, Call.getArgSVal(1),
                    InstCall->getCXXThisVal().getAsRegion());
      }
    } else if (isInsertCall(Func)) {
      verifyMatch(C, Call.getArgSVal(0),
                  InstCall->getCXXThisVal().getAsRegion());
      if (Call.getNumArgs() == 3 &&
          isIteratorType(Call.getArgExpr(1)->getType()) &&
          isIteratorType(Call.getArgExpr(2)->getType())) {
        verifyMatch(C, Call.getArgSVal(1), Call.getArgSVal(2));
      }
    } else if (isEmplaceCall(Func)) {
      verifyMatch(C, Call.getArgSVal(0),
                  InstCall->getCXXThisVal().getAsRegion());
    }
  } else if (isa<CXXConstructorCall>(&Call)) {
    // Check match of first-last iterator pair in a constructor of a container
    if (Call.getNumArgs() < 2)
      return;

    const auto *Ctr = cast<CXXConstructorDecl>(Call.getDecl());
    if (Ctr->getNumParams() < 2)
      return;

    if (Ctr->getParamDecl(0)->getName() != "first" ||
        Ctr->getParamDecl(1)->getName() != "last")
      return;

    if (!isIteratorType(Call.getArgExpr(0)->getType()) ||
        !isIteratorType(Call.getArgExpr(1)->getType()))
      return;

    verifyMatch(C, Call.getArgSVal(0), Call.getArgSVal(1));
  } else {
    // The main purpose of iterators is to abstract away from different
    // containers and provide a (maybe limited) uniform access to them.
    // This implies that any correctly written template function that
    // works on multiple containers using iterators takes different
    // template parameters for different containers. So we can safely
    // assume that passing iterators of different containers as arguments
    // whose type replaces the same template parameter is a bug.
    //
    // Example:
    // template<typename I1, typename I2>
    // void f(I1 first1, I1 last1, I2 first2, I2 last2);
    //
    // In this case the first two arguments to f() must be iterators must belong
    // to the same container and the last to also to the same container but
    // not necessarily to the same as the first two.

    const auto *Templ = Func->getPrimaryTemplate();
    if (!Templ)
      return;

    const auto *TParams = Templ->getTemplateParameters();
    const auto *TArgs = Func->getTemplateSpecializationArgs();

    // Iterate over all the template parameters
    for (size_t I = 0; I < TParams->size(); ++I) {
      const auto *TPDecl = dyn_cast<TemplateTypeParmDecl>(TParams->getParam(I));
      if (!TPDecl)
        continue;

      if (TPDecl->isParameterPack())
        continue;

      const auto TAType = TArgs->get(I).getAsType();
      if (!isIteratorType(TAType))
        continue;

      SVal LHS = UndefinedVal();

      // For every template parameter which is an iterator type in the
      // instantiation look for all functions' parameters' type by it and
      // check whether they belong to the same container
      for (auto J = 0U; J < Func->getNumParams(); ++J) {
        const auto *Param = Func->getParamDecl(J);
        const auto *ParamType =
            Param->getType()->getAs<SubstTemplateTypeParmType>();
        if (!ParamType)
          continue;
        const TemplateTypeParmDecl *D = ParamType->getReplacedParameter();
        if (D != TPDecl)
          continue;
        if (LHS.isUndef()) {
          LHS = Call.getArgSVal(J);
        } else {
          verifyMatch(C, LHS, Call.getArgSVal(J));
        }
      }
    }
  }
}

void MismatchedIteratorChecker::checkPreStmt(const BinaryOperator *BO,
                                             CheckerContext &C) const {
  if (!BO->isComparisonOp())
    return;

  ProgramStateRef State = C.getState();
  SVal LVal = State->getSVal(BO->getLHS(), C.getLocationContext());
  SVal RVal = State->getSVal(BO->getRHS(), C.getLocationContext());
  verifyMatch(C, LVal, RVal);
}

void MismatchedIteratorChecker::verifyMatch(CheckerContext &C, SVal Iter,
                                            const MemRegion *Cont) const {
  // Verify match between a container and the container of an iterator
  Cont = Cont->getMostDerivedObjectRegion();

  if (const auto *ContSym = Cont->getSymbolicBase()) {
    if (isa<SymbolConjured>(ContSym->getSymbol()))
      return;
  }

  auto State = C.getState();
  const auto *Pos = getIteratorPosition(State, Iter);
  if (!Pos)
    return;

  const auto *IterCont = Pos->getContainer();

  // Skip symbolic regions based on conjured symbols. Two conjured symbols
  // may or may not be the same. For example, the same function can return
  // the same or a different container but we get different conjured symbols
  // for each call. This may cause false positives so omit them from the check.
  if (const auto *ContSym = IterCont->getSymbolicBase()) {
    if (isa<SymbolConjured>(ContSym->getSymbol()))
      return;
  }

  if (IterCont != Cont) {
    auto *N = C.generateNonFatalErrorNode(State);
    if (!N) {
      return;
    }
    reportBug("Container accessed using foreign iterator argument.",
                        Iter, Cont, C, N);
  }
}

void MismatchedIteratorChecker::verifyMatch(CheckerContext &C, SVal Iter1,
                                            SVal Iter2) const {
  // Verify match between the containers of two iterators
  auto State = C.getState();
  const auto *Pos1 = getIteratorPosition(State, Iter1);
  if (!Pos1)
    return;

  const auto *IterCont1 = Pos1->getContainer();

  // Skip symbolic regions based on conjured symbols. Two conjured symbols
  // may or may not be the same. For example, the same function can return
  // the same or a different container but we get different conjured symbols
  // for each call. This may cause false positives so omit them from the check.
  if (const auto *ContSym = IterCont1->getSymbolicBase()) {
    if (isa<SymbolConjured>(ContSym->getSymbol()))
      return;
  }

  const auto *Pos2 = getIteratorPosition(State, Iter2);
  if (!Pos2)
    return;

  const auto *IterCont2 = Pos2->getContainer();
  if (const auto *ContSym = IterCont2->getSymbolicBase()) {
    if (isa<SymbolConjured>(ContSym->getSymbol()))
      return;
  }

  if (IterCont1 != IterCont2) {
    auto *N = C.generateNonFatalErrorNode(State);
    if (!N)
      return;
    reportBug("Iterators of different containers used where the "
                        "same container is expected.", Iter1, Iter2, C, N);
  }
}

void MismatchedIteratorChecker::reportBug(StringRef Message, SVal Val1,
                                          SVal Val2, CheckerContext &C,
                                          ExplodedNode *ErrNode) const {
  auto R = std::make_unique<PathSensitiveBugReport>(MismatchedBugType, Message,
                                                    ErrNode);
  R->markInteresting(Val1);
  R->markInteresting(Val2);
  C.emitReport(std::move(R));
}

void MismatchedIteratorChecker::reportBug(StringRef Message, SVal Val,
                                          const MemRegion *Reg,
                                          CheckerContext &C,
                                          ExplodedNode *ErrNode) const {
  auto R = std::make_unique<PathSensitiveBugReport>(MismatchedBugType, Message,
                                                    ErrNode);
  R->markInteresting(Val);
  R->markInteresting(Reg);
  C.emitReport(std::move(R));
}

void ento::registerMismatchedIteratorChecker(CheckerManager &mgr) {
  mgr.registerChecker<MismatchedIteratorChecker>();
}

bool ento::shouldRegisterMismatchedIteratorChecker(const CheckerManager &mgr) {
  return true;
}
