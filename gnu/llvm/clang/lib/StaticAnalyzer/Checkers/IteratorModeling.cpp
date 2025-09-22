//===-- IteratorModeling.cpp --------------------------------------*- C++ -*--//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Defines a modeling-checker for modeling STL iterator-like iterators.
//
//===----------------------------------------------------------------------===//
//
// In the code, iterator can be represented as a:
// * type-I: typedef-ed pointer. Operations over such iterator, such as
//           comparisons or increments, are modeled straightforwardly by the
//           analyzer.
// * type-II: structure with its method bodies available.  Operations over such
//            iterator are inlined by the analyzer, and results of modeling
//            these operations are exposing implementation details of the
//            iterators, which is not necessarily helping.
// * type-III: completely opaque structure. Operations over such iterator are
//             modeled conservatively, producing conjured symbols everywhere.
//
// To handle all these types in a common way we introduce a structure called
// IteratorPosition which is an abstraction of the position the iterator
// represents using symbolic expressions. The checker handles all the
// operations on this structure.
//
// Additionally, depending on the circumstances, operators of types II and III
// can be represented as:
// * type-IIa, type-IIIa: conjured structure symbols - when returned by value
//                        from conservatively evaluated methods such as
//                        `.begin()`.
// * type-IIb, type-IIIb: memory regions of iterator-typed objects, such as
//                        variables or temporaries, when the iterator object is
//                        currently treated as an lvalue.
// * type-IIc, type-IIIc: compound values of iterator-typed objects, when the
//                        iterator object is treated as an rvalue taken of a
//                        particular lvalue, eg. a copy of "type-a" iterator
//                        object, or an iterator that existed before the
//                        analysis has started.
//
// To handle any of these three different representations stored in an SVal we
// use setter and getters functions which separate the three cases. To store
// them we use a pointer union of symbol and memory region.
//
// The checker works the following way: We record the begin and the
// past-end iterator for all containers whenever their `.begin()` and `.end()`
// are called. Since the Constraint Manager cannot handle such SVals we need
// to take over its role. We post-check equality and non-equality comparisons
// and record that the two sides are equal if we are in the 'equal' branch
// (true-branch for `==` and false-branch for `!=`).
//
// In case of type-I or type-II iterators we get a concrete integer as a result
// of the comparison (1 or 0) but in case of type-III we only get a Symbol. In
// this latter case we record the symbol and reload it in evalAssume() and do
// the propagation there. We also handle (maybe double) negated comparisons
// which are represented in the form of (x == 0 or x != 0) where x is the
// comparison itself.
//
// Since `SimpleConstraintManager` cannot handle complex symbolic expressions
// we only use expressions of the format S, S+n or S-n for iterator positions
// where S is a conjured symbol and n is an unsigned concrete integer. When
// making an assumption e.g. `S1 + n == S2 + m` we store `S1 - S2 == m - n` as
// a constraint which we later retrieve when doing an actual comparison.

#include "clang/AST/DeclTemplate.h"
#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallDescription.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/DynamicType.h"
#include "llvm/ADT/STLExtras.h"

#include "Iterator.h"

#include <utility>

using namespace clang;
using namespace ento;
using namespace iterator;

namespace {

class IteratorModeling
    : public Checker<check::PostCall, check::PostStmt<UnaryOperator>,
                     check::PostStmt<BinaryOperator>,
                     check::PostStmt<MaterializeTemporaryExpr>,
                     check::Bind, check::LiveSymbols, check::DeadSymbols> {

  using AdvanceFn = void (IteratorModeling::*)(CheckerContext &, const Expr *,
                                               SVal, SVal, SVal) const;

  void handleOverloadedOperator(CheckerContext &C, const CallEvent &Call,
                                OverloadedOperatorKind Op) const;
  void handleAdvanceLikeFunction(CheckerContext &C, const CallEvent &Call,
                                 const Expr *OrigExpr,
                                 const AdvanceFn *Handler) const;

  void handleComparison(CheckerContext &C, const Expr *CE, SVal RetVal,
                        SVal LVal, SVal RVal, OverloadedOperatorKind Op) const;
  void processComparison(CheckerContext &C, ProgramStateRef State,
                         SymbolRef Sym1, SymbolRef Sym2, SVal RetVal,
                         OverloadedOperatorKind Op) const;
  void handleIncrement(CheckerContext &C, SVal RetVal, SVal Iter,
                       bool Postfix) const;
  void handleDecrement(CheckerContext &C, SVal RetVal, SVal Iter,
                       bool Postfix) const;
  void handleRandomIncrOrDecr(CheckerContext &C, const Expr *CE,
                              OverloadedOperatorKind Op, SVal RetVal,
                              SVal Iterator, SVal Amount) const;
  void handlePtrIncrOrDecr(CheckerContext &C, const Expr *Iterator,
                           OverloadedOperatorKind OK, SVal Offset) const;
  void handleAdvance(CheckerContext &C, const Expr *CE, SVal RetVal, SVal Iter,
                     SVal Amount) const;
  void handlePrev(CheckerContext &C, const Expr *CE, SVal RetVal, SVal Iter,
                  SVal Amount) const;
  void handleNext(CheckerContext &C, const Expr *CE, SVal RetVal, SVal Iter,
                  SVal Amount) const;
  void assignToContainer(CheckerContext &C, const Expr *CE, SVal RetVal,
                         const MemRegion *Cont) const;
  bool noChangeInAdvance(CheckerContext &C, SVal Iter, const Expr *CE) const;
  void printState(raw_ostream &Out, ProgramStateRef State, const char *NL,
                  const char *Sep) const override;

  // std::advance, std::prev & std::next
  CallDescriptionMap<AdvanceFn> AdvanceLikeFunctions = {
      // template<class InputIt, class Distance>
      // void advance(InputIt& it, Distance n);
      {{CDM::SimpleFunc, {"std", "advance"}, 2},
       &IteratorModeling::handleAdvance},

      // template<class BidirIt>
      // BidirIt prev(
      //   BidirIt it,
      //   typename std::iterator_traits<BidirIt>::difference_type n = 1);
      {{CDM::SimpleFunc, {"std", "prev"}, 2}, &IteratorModeling::handlePrev},

      // template<class ForwardIt>
      // ForwardIt next(
      //   ForwardIt it,
      //   typename std::iterator_traits<ForwardIt>::difference_type n = 1);
      {{CDM::SimpleFunc, {"std", "next"}, 2}, &IteratorModeling::handleNext},
  };

public:
  IteratorModeling() = default;

  void checkPostCall(const CallEvent &Call, CheckerContext &C) const;
  void checkBind(SVal Loc, SVal Val, const Stmt *S, CheckerContext &C) const;
  void checkPostStmt(const UnaryOperator *UO, CheckerContext &C) const;
  void checkPostStmt(const BinaryOperator *BO, CheckerContext &C) const;
  void checkPostStmt(const MaterializeTemporaryExpr *MTE,
                     CheckerContext &C) const;
  void checkLiveSymbols(ProgramStateRef State, SymbolReaper &SR) const;
  void checkDeadSymbols(SymbolReaper &SR, CheckerContext &C) const;
};

bool isSimpleComparisonOperator(OverloadedOperatorKind OK);
bool isSimpleComparisonOperator(BinaryOperatorKind OK);
ProgramStateRef removeIteratorPosition(ProgramStateRef State, SVal Val);
ProgramStateRef relateSymbols(ProgramStateRef State, SymbolRef Sym1,
                              SymbolRef Sym2, bool Equal);
bool isBoundThroughLazyCompoundVal(const Environment &Env,
                                   const MemRegion *Reg);
const ExplodedNode *findCallEnter(const ExplodedNode *Node, const Expr *Call);

} // namespace

void IteratorModeling::checkPostCall(const CallEvent &Call,
                                     CheckerContext &C) const {
  // Record new iterator positions and iterator position changes
  const auto *Func = dyn_cast_or_null<FunctionDecl>(Call.getDecl());
  if (!Func)
    return;

  if (Func->isOverloadedOperator()) {
    const auto Op = Func->getOverloadedOperator();
    handleOverloadedOperator(C, Call, Op);
    return;
  }

  const auto *OrigExpr = Call.getOriginExpr();
  if (!OrigExpr)
    return;

  const AdvanceFn *Handler = AdvanceLikeFunctions.lookup(Call);
  if (Handler) {
    handleAdvanceLikeFunction(C, Call, OrigExpr, Handler);
    return;
  }

  if (!isIteratorType(Call.getResultType()))
    return;

  auto State = C.getState();

  // Already bound to container?
  if (getIteratorPosition(State, Call.getReturnValue()))
    return;

  // Copy-like and move constructors
  if (isa<CXXConstructorCall>(&Call) && Call.getNumArgs() == 1) {
    if (const auto *Pos = getIteratorPosition(State, Call.getArgSVal(0))) {
      State = setIteratorPosition(State, Call.getReturnValue(), *Pos);
      if (cast<CXXConstructorDecl>(Func)->isMoveConstructor()) {
        State = removeIteratorPosition(State, Call.getArgSVal(0));
      }
      C.addTransition(State);
      return;
    }
  }

  // Assumption: if return value is an iterator which is not yet bound to a
  //             container, then look for the first iterator argument of the
  //             same type as the return value and bind the return value to
  //             the same container. This approach works for STL algorithms.
  // FIXME: Add a more conservative mode
  for (unsigned i = 0; i < Call.getNumArgs(); ++i) {
    if (isIteratorType(Call.getArgExpr(i)->getType()) &&
        Call.getArgExpr(i)->getType().getNonReferenceType().getDesugaredType(
            C.getASTContext()).getTypePtr() ==
        Call.getResultType().getDesugaredType(C.getASTContext()).getTypePtr()) {
      if (const auto *Pos = getIteratorPosition(State, Call.getArgSVal(i))) {
        assignToContainer(C, OrigExpr, Call.getReturnValue(),
                          Pos->getContainer());
        return;
      }
    }
  }
}

void IteratorModeling::checkBind(SVal Loc, SVal Val, const Stmt *S,
                                 CheckerContext &C) const {
  auto State = C.getState();
  const auto *Pos = getIteratorPosition(State, Val);
  if (Pos) {
    State = setIteratorPosition(State, Loc, *Pos);
    C.addTransition(State);
  } else {
    const auto *OldPos = getIteratorPosition(State, Loc);
    if (OldPos) {
      State = removeIteratorPosition(State, Loc);
      C.addTransition(State);
    }
  }
}

void IteratorModeling::checkPostStmt(const UnaryOperator *UO,
                                     CheckerContext &C) const {
  UnaryOperatorKind OK = UO->getOpcode();
  if (!isIncrementOperator(OK) && !isDecrementOperator(OK))
    return;

  auto &SVB = C.getSValBuilder();
  handlePtrIncrOrDecr(C, UO->getSubExpr(),
                      isIncrementOperator(OK) ? OO_Plus : OO_Minus,
                      SVB.makeArrayIndex(1));
}

void IteratorModeling::checkPostStmt(const BinaryOperator *BO,
                                     CheckerContext &C) const {
  const ProgramStateRef State = C.getState();
  const BinaryOperatorKind OK = BO->getOpcode();
  const Expr *const LHS = BO->getLHS();
  const Expr *const RHS = BO->getRHS();
  const SVal LVal = State->getSVal(LHS, C.getLocationContext());
  const SVal RVal = State->getSVal(RHS, C.getLocationContext());

  if (isSimpleComparisonOperator(BO->getOpcode())) {
    SVal Result = State->getSVal(BO, C.getLocationContext());
    handleComparison(C, BO, Result, LVal, RVal,
                     BinaryOperator::getOverloadedOperator(OK));
  } else if (isRandomIncrOrDecrOperator(OK)) {
    // In case of operator+ the iterator can be either on the LHS (eg.: it + 1),
    // or on the RHS (eg.: 1 + it). Both cases are modeled.
    const bool IsIterOnLHS = BO->getLHS()->getType()->isPointerType();
    const Expr *const &IterExpr = IsIterOnLHS ? LHS : RHS;
    const Expr *const &AmountExpr = IsIterOnLHS ? RHS : LHS;

    // The non-iterator side must have an integral or enumeration type.
    if (!AmountExpr->getType()->isIntegralOrEnumerationType())
      return;
    SVal AmountVal = IsIterOnLHS ? RVal : LVal;
    handlePtrIncrOrDecr(C, IterExpr, BinaryOperator::getOverloadedOperator(OK),
                        AmountVal);
  }
}

void IteratorModeling::checkPostStmt(const MaterializeTemporaryExpr *MTE,
                                     CheckerContext &C) const {
  /* Transfer iterator state to temporary objects */
  auto State = C.getState();
  const auto *Pos = getIteratorPosition(State, C.getSVal(MTE->getSubExpr()));
  if (!Pos)
    return;
  State = setIteratorPosition(State, C.getSVal(MTE), *Pos);
  C.addTransition(State);
}

void IteratorModeling::checkLiveSymbols(ProgramStateRef State,
                                        SymbolReaper &SR) const {
  // Keep symbolic expressions of iterator positions alive
  auto RegionMap = State->get<IteratorRegionMap>();
  for (const IteratorPosition &Pos : llvm::make_second_range(RegionMap)) {
    for (SymbolRef Sym : Pos.getOffset()->symbols())
      if (isa<SymbolData>(Sym))
        SR.markLive(Sym);
  }

  auto SymbolMap = State->get<IteratorSymbolMap>();
  for (const IteratorPosition &Pos : llvm::make_second_range(SymbolMap)) {
    for (SymbolRef Sym : Pos.getOffset()->symbols())
      if (isa<SymbolData>(Sym))
        SR.markLive(Sym);
  }
}

void IteratorModeling::checkDeadSymbols(SymbolReaper &SR,
                                        CheckerContext &C) const {
  // Cleanup
  auto State = C.getState();

  auto RegionMap = State->get<IteratorRegionMap>();
  for (const auto &Reg : RegionMap) {
    if (!SR.isLiveRegion(Reg.first)) {
      // The region behind the `LazyCompoundVal` is often cleaned up before
      // the `LazyCompoundVal` itself. If there are iterator positions keyed
      // by these regions their cleanup must be deferred.
      if (!isBoundThroughLazyCompoundVal(State->getEnvironment(), Reg.first)) {
        State = State->remove<IteratorRegionMap>(Reg.first);
      }
    }
  }

  auto SymbolMap = State->get<IteratorSymbolMap>();
  for (const auto &Sym : SymbolMap) {
    if (!SR.isLive(Sym.first)) {
      State = State->remove<IteratorSymbolMap>(Sym.first);
    }
  }

  C.addTransition(State);
}

void
IteratorModeling::handleOverloadedOperator(CheckerContext &C,
                                           const CallEvent &Call,
                                           OverloadedOperatorKind Op) const {
    if (isSimpleComparisonOperator(Op)) {
      const auto *OrigExpr = Call.getOriginExpr();
      if (!OrigExpr)
        return;

      if (const auto *InstCall = dyn_cast<CXXInstanceCall>(&Call)) {
        handleComparison(C, OrigExpr, Call.getReturnValue(),
                         InstCall->getCXXThisVal(), Call.getArgSVal(0), Op);
        return;
      }

      handleComparison(C, OrigExpr, Call.getReturnValue(), Call.getArgSVal(0),
                         Call.getArgSVal(1), Op);
      return;
    } else if (isRandomIncrOrDecrOperator(Op)) {
      const auto *OrigExpr = Call.getOriginExpr();
      if (!OrigExpr)
        return;

      if (const auto *InstCall = dyn_cast<CXXInstanceCall>(&Call)) {
        if (Call.getNumArgs() >= 1 &&
              Call.getArgExpr(0)->getType()->isIntegralOrEnumerationType()) {
          handleRandomIncrOrDecr(C, OrigExpr, Op, Call.getReturnValue(),
                                 InstCall->getCXXThisVal(), Call.getArgSVal(0));
          return;
        }
      } else if (Call.getNumArgs() >= 2) {
        const Expr *FirstArg = Call.getArgExpr(0);
        const Expr *SecondArg = Call.getArgExpr(1);
        const QualType FirstType = FirstArg->getType();
        const QualType SecondType = SecondArg->getType();

        if (FirstType->isIntegralOrEnumerationType() ||
            SecondType->isIntegralOrEnumerationType()) {
          // In case of operator+ the iterator can be either on the LHS (eg.:
          // it + 1), or on the RHS (eg.: 1 + it). Both cases are modeled.
          const bool IsIterFirst = FirstType->isStructureOrClassType();
          const SVal FirstArg = Call.getArgSVal(0);
          const SVal SecondArg = Call.getArgSVal(1);
          SVal Iterator = IsIterFirst ? FirstArg : SecondArg;
          SVal Amount = IsIterFirst ? SecondArg : FirstArg;

          handleRandomIncrOrDecr(C, OrigExpr, Op, Call.getReturnValue(),
                                 Iterator, Amount);
          return;
        }
      }
    } else if (isIncrementOperator(Op)) {
      if (const auto *InstCall = dyn_cast<CXXInstanceCall>(&Call)) {
        handleIncrement(C, Call.getReturnValue(), InstCall->getCXXThisVal(),
                        Call.getNumArgs());
        return;
      }

      handleIncrement(C, Call.getReturnValue(), Call.getArgSVal(0),
                      Call.getNumArgs());
      return;
    } else if (isDecrementOperator(Op)) {
      if (const auto *InstCall = dyn_cast<CXXInstanceCall>(&Call)) {
        handleDecrement(C, Call.getReturnValue(), InstCall->getCXXThisVal(),
                        Call.getNumArgs());
        return;
      }

      handleDecrement(C, Call.getReturnValue(), Call.getArgSVal(0),
                        Call.getNumArgs());
      return;
    }
}

void
IteratorModeling::handleAdvanceLikeFunction(CheckerContext &C,
                                            const CallEvent &Call,
                                            const Expr *OrigExpr,
                                            const AdvanceFn *Handler) const {
  if (!C.wasInlined) {
    (this->**Handler)(C, OrigExpr, Call.getReturnValue(),
                      Call.getArgSVal(0), Call.getArgSVal(1));
    return;
  }

  // If std::advance() was inlined, but a non-standard function it calls inside
  // was not, then we have to model it explicitly
  const auto *IdInfo = cast<FunctionDecl>(Call.getDecl())->getIdentifier();
  if (IdInfo) {
    if (IdInfo->getName() == "advance") {
      if (noChangeInAdvance(C, Call.getArgSVal(0), OrigExpr)) {
        (this->**Handler)(C, OrigExpr, Call.getReturnValue(),
                          Call.getArgSVal(0), Call.getArgSVal(1));
      }
    }
  }
}

void IteratorModeling::handleComparison(CheckerContext &C, const Expr *CE,
                                        SVal RetVal, SVal LVal, SVal RVal,
                                        OverloadedOperatorKind Op) const {
  // Record the operands and the operator of the comparison for the next
  // evalAssume, if the result is a symbolic expression. If it is a concrete
  // value (only one branch is possible), then transfer the state between
  // the operands according to the operator and the result
  auto State = C.getState();
  const auto *LPos = getIteratorPosition(State, LVal);
  const auto *RPos = getIteratorPosition(State, RVal);
  const MemRegion *Cont = nullptr;
  if (LPos) {
    Cont = LPos->getContainer();
  } else if (RPos) {
    Cont = RPos->getContainer();
  }
  if (!Cont)
    return;

  // At least one of the iterators has recorded positions. If one of them does
  // not then create a new symbol for the offset.
  SymbolRef Sym;
  if (!LPos || !RPos) {
    auto &SymMgr = C.getSymbolManager();
    Sym = SymMgr.conjureSymbol(CE, C.getLocationContext(),
                               C.getASTContext().LongTy, C.blockCount());
    State = assumeNoOverflow(State, Sym, 4);
  }

  if (!LPos) {
    State = setIteratorPosition(State, LVal,
                                IteratorPosition::getPosition(Cont, Sym));
    LPos = getIteratorPosition(State, LVal);
  } else if (!RPos) {
    State = setIteratorPosition(State, RVal,
                                IteratorPosition::getPosition(Cont, Sym));
    RPos = getIteratorPosition(State, RVal);
  }

  // If the value for which we just tried to set a new iterator position is
  // an `SVal`for which no iterator position can be set then the setting was
  // unsuccessful. We cannot handle the comparison in this case.
  if (!LPos || !RPos)
    return;

  // We cannot make assumptions on `UnknownVal`. Let us conjure a symbol
  // instead.
  if (RetVal.isUnknown()) {
    auto &SymMgr = C.getSymbolManager();
    auto *LCtx = C.getLocationContext();
    RetVal = nonloc::SymbolVal(SymMgr.conjureSymbol(
        CE, LCtx, C.getASTContext().BoolTy, C.blockCount()));
    State = State->BindExpr(CE, LCtx, RetVal);
  }

  processComparison(C, State, LPos->getOffset(), RPos->getOffset(), RetVal, Op);
}

void IteratorModeling::processComparison(CheckerContext &C,
                                         ProgramStateRef State, SymbolRef Sym1,
                                         SymbolRef Sym2, SVal RetVal,
                                         OverloadedOperatorKind Op) const {
  if (const auto TruthVal = RetVal.getAs<nonloc::ConcreteInt>()) {
    if ((State = relateSymbols(State, Sym1, Sym2,
                              (Op == OO_EqualEqual) ==
                               (TruthVal->getValue() != 0)))) {
      C.addTransition(State);
    } else {
      C.generateSink(State, C.getPredecessor());
    }
    return;
  }

  const auto ConditionVal = RetVal.getAs<DefinedSVal>();
  if (!ConditionVal)
    return;

  if (auto StateTrue = relateSymbols(State, Sym1, Sym2, Op == OO_EqualEqual)) {
    StateTrue = StateTrue->assume(*ConditionVal, true);
    C.addTransition(StateTrue);
  }

  if (auto StateFalse = relateSymbols(State, Sym1, Sym2, Op != OO_EqualEqual)) {
    StateFalse = StateFalse->assume(*ConditionVal, false);
    C.addTransition(StateFalse);
  }
}

void IteratorModeling::handleIncrement(CheckerContext &C, SVal RetVal,
                                       SVal Iter, bool Postfix) const {
  // Increment the symbolic expressions which represents the position of the
  // iterator
  auto State = C.getState();
  auto &BVF = C.getSymbolManager().getBasicVals();

  const auto *Pos = getIteratorPosition(State, Iter);
  if (!Pos)
    return;

  auto NewState =
    advancePosition(State, Iter, OO_Plus,
                    nonloc::ConcreteInt(BVF.getValue(llvm::APSInt::get(1))));
  assert(NewState &&
         "Advancing position by concrete int should always be successful");

  const auto *NewPos = getIteratorPosition(NewState, Iter);
  assert(NewPos &&
         "Iterator should have position after successful advancement");

  State = setIteratorPosition(State, Iter, *NewPos);
  State = setIteratorPosition(State, RetVal, Postfix ? *Pos : *NewPos);
  C.addTransition(State);
}

void IteratorModeling::handleDecrement(CheckerContext &C, SVal RetVal,
                                       SVal Iter, bool Postfix) const {
  // Decrement the symbolic expressions which represents the position of the
  // iterator
  auto State = C.getState();
  auto &BVF = C.getSymbolManager().getBasicVals();

  const auto *Pos = getIteratorPosition(State, Iter);
  if (!Pos)
    return;

  auto NewState =
    advancePosition(State, Iter, OO_Minus,
                    nonloc::ConcreteInt(BVF.getValue(llvm::APSInt::get(1))));
  assert(NewState &&
         "Advancing position by concrete int should always be successful");

  const auto *NewPos = getIteratorPosition(NewState, Iter);
  assert(NewPos &&
         "Iterator should have position after successful advancement");

  State = setIteratorPosition(State, Iter, *NewPos);
  State = setIteratorPosition(State, RetVal, Postfix ? *Pos : *NewPos);
  C.addTransition(State);
}

void IteratorModeling::handleRandomIncrOrDecr(CheckerContext &C, const Expr *CE,
                                              OverloadedOperatorKind Op,
                                              SVal RetVal, SVal Iterator,
                                              SVal Amount) const {
  // Increment or decrement the symbolic expressions which represents the
  // position of the iterator
  auto State = C.getState();

  const auto *Pos = getIteratorPosition(State, Iterator);
  if (!Pos)
    return;

  const auto *Value = &Amount;
  SVal Val;
  if (auto LocAmount = Amount.getAs<Loc>()) {
    Val = State->getRawSVal(*LocAmount);
    Value = &Val;
  }

  const auto &TgtVal =
      (Op == OO_PlusEqual || Op == OO_MinusEqual) ? Iterator : RetVal;

  // `AdvancedState` is a state where the position of `LHS` is advanced. We
  // only need this state to retrieve the new position, but we do not want
  // to change the position of `LHS` (in every case).
  auto AdvancedState = advancePosition(State, Iterator, Op, *Value);
  if (AdvancedState) {
    const auto *NewPos = getIteratorPosition(AdvancedState, Iterator);
    assert(NewPos &&
           "Iterator should have position after successful advancement");

    State = setIteratorPosition(State, TgtVal, *NewPos);
    C.addTransition(State);
  } else {
    assignToContainer(C, CE, TgtVal, Pos->getContainer());
  }
}

void IteratorModeling::handlePtrIncrOrDecr(CheckerContext &C,
                                           const Expr *Iterator,
                                           OverloadedOperatorKind OK,
                                           SVal Offset) const {
  if (!isa<DefinedSVal>(Offset))
    return;

  QualType PtrType = Iterator->getType();
  if (!PtrType->isPointerType())
    return;
  QualType ElementType = PtrType->getPointeeType();

  ProgramStateRef State = C.getState();
  SVal OldVal = State->getSVal(Iterator, C.getLocationContext());

  const IteratorPosition *OldPos = getIteratorPosition(State, OldVal);
  if (!OldPos)
    return;

  SVal NewVal;
  if (OK == OO_Plus || OK == OO_PlusEqual) {
    NewVal = State->getLValue(ElementType, Offset, OldVal);
  } else {
    auto &SVB = C.getSValBuilder();
    SVal NegatedOffset = SVB.evalMinus(Offset.castAs<NonLoc>());
    NewVal = State->getLValue(ElementType, NegatedOffset, OldVal);
  }

  // `AdvancedState` is a state where the position of `Old` is advanced. We
  // only need this state to retrieve the new position, but we do not want
  // ever to change the position of `OldVal`.
  auto AdvancedState = advancePosition(State, OldVal, OK, Offset);
  if (AdvancedState) {
    const IteratorPosition *NewPos = getIteratorPosition(AdvancedState, OldVal);
    assert(NewPos &&
           "Iterator should have position after successful advancement");

    ProgramStateRef NewState = setIteratorPosition(State, NewVal, *NewPos);
    C.addTransition(NewState);
  } else {
    assignToContainer(C, Iterator, NewVal, OldPos->getContainer());
  }
}

void IteratorModeling::handleAdvance(CheckerContext &C, const Expr *CE,
                                     SVal RetVal, SVal Iter,
                                     SVal Amount) const {
  handleRandomIncrOrDecr(C, CE, OO_PlusEqual, RetVal, Iter, Amount);
}

void IteratorModeling::handlePrev(CheckerContext &C, const Expr *CE,
                                  SVal RetVal, SVal Iter, SVal Amount) const {
  handleRandomIncrOrDecr(C, CE, OO_Minus, RetVal, Iter, Amount);
}

void IteratorModeling::handleNext(CheckerContext &C, const Expr *CE,
                                  SVal RetVal, SVal Iter, SVal Amount) const {
  handleRandomIncrOrDecr(C, CE, OO_Plus, RetVal, Iter, Amount);
}

void IteratorModeling::assignToContainer(CheckerContext &C, const Expr *CE,
                                         SVal RetVal,
                                         const MemRegion *Cont) const {
  Cont = Cont->getMostDerivedObjectRegion();

  auto State = C.getState();
  const auto *LCtx = C.getLocationContext();
  State = createIteratorPosition(State, RetVal, Cont, CE, LCtx, C.blockCount());

  C.addTransition(State);
}

bool IteratorModeling::noChangeInAdvance(CheckerContext &C, SVal Iter,
                                         const Expr *CE) const {
  // Compare the iterator position before and after the call. (To be called
  // from `checkPostCall()`.)
  const auto StateAfter = C.getState();

  const auto *PosAfter = getIteratorPosition(StateAfter, Iter);
  // If we have no position after the call of `std::advance`, then we are not
  // interested. (Modeling of an inlined `std::advance()` should not remove the
  // position in any case.)
  if (!PosAfter)
    return false;

  const ExplodedNode *N = findCallEnter(C.getPredecessor(), CE);
  assert(N && "Any call should have a `CallEnter` node.");

  const auto StateBefore = N->getState();
  const auto *PosBefore = getIteratorPosition(StateBefore, Iter);
  // FIXME: `std::advance()` should not create a new iterator position but
  //        change existing ones. However, in case of iterators implemented as
  //        pointers the handling of parameters in `std::advance()`-like
  //        functions is still incomplete which may result in cases where
  //        the new position is assigned to the wrong pointer. This causes
  //        crash if we use an assertion here.
  if (!PosBefore)
    return false;

  return PosBefore->getOffset() == PosAfter->getOffset();
}

void IteratorModeling::printState(raw_ostream &Out, ProgramStateRef State,
                                  const char *NL, const char *Sep) const {
  auto SymbolMap = State->get<IteratorSymbolMap>();
  auto RegionMap = State->get<IteratorRegionMap>();
  // Use a counter to add newlines before every line except the first one.
  unsigned Count = 0;

  if (!SymbolMap.isEmpty() || !RegionMap.isEmpty()) {
    Out << Sep << "Iterator Positions :" << NL;
    for (const auto &Sym : SymbolMap) {
      if (Count++)
        Out << NL;

      Sym.first->dumpToStream(Out);
      Out << " : ";
      const auto Pos = Sym.second;
      Out << (Pos.isValid() ? "Valid" : "Invalid") << " ; Container == ";
      Pos.getContainer()->dumpToStream(Out);
      Out<<" ; Offset == ";
      Pos.getOffset()->dumpToStream(Out);
    }

    for (const auto &Reg : RegionMap) {
      if (Count++)
        Out << NL;

      Reg.first->dumpToStream(Out);
      Out << " : ";
      const auto Pos = Reg.second;
      Out << (Pos.isValid() ? "Valid" : "Invalid") << " ; Container == ";
      Pos.getContainer()->dumpToStream(Out);
      Out<<" ; Offset == ";
      Pos.getOffset()->dumpToStream(Out);
    }
  }
}

namespace {

bool isSimpleComparisonOperator(OverloadedOperatorKind OK) {
  return OK == OO_EqualEqual || OK == OO_ExclaimEqual;
}

bool isSimpleComparisonOperator(BinaryOperatorKind OK) {
  return OK == BO_EQ || OK == BO_NE;
}

ProgramStateRef removeIteratorPosition(ProgramStateRef State, SVal Val) {
  if (auto Reg = Val.getAsRegion()) {
    Reg = Reg->getMostDerivedObjectRegion();
    return State->remove<IteratorRegionMap>(Reg);
  } else if (const auto Sym = Val.getAsSymbol()) {
    return State->remove<IteratorSymbolMap>(Sym);
  } else if (const auto LCVal = Val.getAs<nonloc::LazyCompoundVal>()) {
    return State->remove<IteratorRegionMap>(LCVal->getRegion());
  }
  return nullptr;
}

ProgramStateRef relateSymbols(ProgramStateRef State, SymbolRef Sym1,
                              SymbolRef Sym2, bool Equal) {
  auto &SVB = State->getStateManager().getSValBuilder();

  // FIXME: This code should be reworked as follows:
  // 1. Subtract the operands using evalBinOp().
  // 2. Assume that the result doesn't overflow.
  // 3. Compare the result to 0.
  // 4. Assume the result of the comparison.
  const auto comparison =
    SVB.evalBinOp(State, BO_EQ, nonloc::SymbolVal(Sym1),
                  nonloc::SymbolVal(Sym2), SVB.getConditionType());

  assert(isa<DefinedSVal>(comparison) &&
         "Symbol comparison must be a `DefinedSVal`");

  auto NewState = State->assume(comparison.castAs<DefinedSVal>(), Equal);
  if (!NewState)
    return nullptr;

  if (const auto CompSym = comparison.getAsSymbol()) {
    assert(isa<SymIntExpr>(CompSym) &&
           "Symbol comparison must be a `SymIntExpr`");
    assert(BinaryOperator::isComparisonOp(
               cast<SymIntExpr>(CompSym)->getOpcode()) &&
           "Symbol comparison must be a comparison");
    return assumeNoOverflow(NewState, cast<SymIntExpr>(CompSym)->getLHS(), 2);
  }

  return NewState;
}

bool isBoundThroughLazyCompoundVal(const Environment &Env,
                                   const MemRegion *Reg) {
  for (const auto &Binding : Env) {
    if (const auto LCVal = Binding.second.getAs<nonloc::LazyCompoundVal>()) {
      if (LCVal->getRegion() == Reg)
        return true;
    }
  }

  return false;
}

const ExplodedNode *findCallEnter(const ExplodedNode *Node, const Expr *Call) {
  while (Node) {
    ProgramPoint PP = Node->getLocation();
    if (auto Enter = PP.getAs<CallEnter>()) {
      if (Enter->getCallExpr() == Call)
        break;
    }

    Node = Node->getFirstPred();
  }

  return Node;
}

} // namespace

void ento::registerIteratorModeling(CheckerManager &mgr) {
  mgr.registerChecker<IteratorModeling>();
}

bool ento::shouldRegisterIteratorModeling(const CheckerManager &mgr) {
  return true;
}
