//===-- ContainerModeling.cpp -------------------------------------*- C++ -*--//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Defines a modeling-checker for modeling STL container-like containers.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/DeclTemplate.h"
#include "clang/Driver/DriverDiagnostic.h"
#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallDescription.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/DynamicType.h"

#include "Iterator.h"

#include <utility>

using namespace clang;
using namespace ento;
using namespace iterator;

namespace {

class ContainerModeling
  : public Checker<check::PostCall, check::LiveSymbols, check::DeadSymbols> {

  void handleBegin(CheckerContext &C, const Expr *CE, SVal RetVal,
                   SVal Cont) const;
  void handleEnd(CheckerContext &C, const Expr *CE, SVal RetVal,
                 SVal Cont) const;
  void handleAssignment(CheckerContext &C, SVal Cont, const Expr *CE = nullptr,
                        SVal OldCont = UndefinedVal()) const;
  void handleAssign(CheckerContext &C, SVal Cont, const Expr *ContE) const;
  void handleClear(CheckerContext &C, SVal Cont, const Expr *ContE) const;
  void handlePushBack(CheckerContext &C, SVal Cont, const Expr *ContE) const;
  void handlePopBack(CheckerContext &C, SVal Cont, const Expr *ContE) const;
  void handlePushFront(CheckerContext &C, SVal Cont, const Expr *ContE) const;
  void handlePopFront(CheckerContext &C, SVal Cont, const Expr *ContE) const;
  void handleInsert(CheckerContext &C, SVal Cont, SVal Iter) const;
  void handleErase(CheckerContext &C, SVal Cont, SVal Iter) const;
  void handleErase(CheckerContext &C, SVal Cont, SVal Iter1, SVal Iter2) const;
  void handleEraseAfter(CheckerContext &C, SVal Cont, SVal Iter) const;
  void handleEraseAfter(CheckerContext &C, SVal Cont, SVal Iter1,
                        SVal Iter2) const;
  const NoteTag *getChangeTag(CheckerContext &C, StringRef Text,
                              const MemRegion *ContReg,
                              const Expr *ContE) const;
  void printState(raw_ostream &Out, ProgramStateRef State, const char *NL,
                  const char *Sep) const override;

public:
  ContainerModeling() = default;

  void checkPostCall(const CallEvent &Call, CheckerContext &C) const;
  void checkLiveSymbols(ProgramStateRef State, SymbolReaper &SR) const;
  void checkDeadSymbols(SymbolReaper &SR, CheckerContext &C) const;

  using NoItParamFn = void (ContainerModeling::*)(CheckerContext &, SVal,
                                                  const Expr *) const;
  using OneItParamFn = void (ContainerModeling::*)(CheckerContext &, SVal,
                                                   SVal) const;
  using TwoItParamFn = void (ContainerModeling::*)(CheckerContext &, SVal, SVal,
                                                   SVal) const;

  CallDescriptionMap<NoItParamFn> NoIterParamFunctions = {
      {{CDM::CXXMethod, {"clear"}, 0}, &ContainerModeling::handleClear},
      {{CDM::CXXMethod, {"assign"}, 2}, &ContainerModeling::handleAssign},
      {{CDM::CXXMethod, {"push_back"}, 1}, &ContainerModeling::handlePushBack},
      {{CDM::CXXMethod, {"emplace_back"}, 1},
       &ContainerModeling::handlePushBack},
      {{CDM::CXXMethod, {"pop_back"}, 0}, &ContainerModeling::handlePopBack},
      {{CDM::CXXMethod, {"push_front"}, 1},
       &ContainerModeling::handlePushFront},
      {{CDM::CXXMethod, {"emplace_front"}, 1},
       &ContainerModeling::handlePushFront},
      {{CDM::CXXMethod, {"pop_front"}, 0}, &ContainerModeling::handlePopFront},
  };

  CallDescriptionMap<OneItParamFn> OneIterParamFunctions = {
      {{CDM::CXXMethod, {"insert"}, 2}, &ContainerModeling::handleInsert},
      {{CDM::CXXMethod, {"emplace"}, 2}, &ContainerModeling::handleInsert},
      {{CDM::CXXMethod, {"erase"}, 1}, &ContainerModeling::handleErase},
      {{CDM::CXXMethod, {"erase_after"}, 1},
       &ContainerModeling::handleEraseAfter},
  };

  CallDescriptionMap<TwoItParamFn> TwoIterParamFunctions = {
      {{CDM::CXXMethod, {"erase"}, 2}, &ContainerModeling::handleErase},
      {{CDM::CXXMethod, {"erase_after"}, 2},
       &ContainerModeling::handleEraseAfter},
  };
};

bool isBeginCall(const FunctionDecl *Func);
bool isEndCall(const FunctionDecl *Func);
bool hasSubscriptOperator(ProgramStateRef State, const MemRegion *Reg);
bool frontModifiable(ProgramStateRef State, const MemRegion *Reg);
bool backModifiable(ProgramStateRef State, const MemRegion *Reg);
SymbolRef getContainerBegin(ProgramStateRef State, const MemRegion *Cont);
SymbolRef getContainerEnd(ProgramStateRef State, const MemRegion *Cont);
ProgramStateRef createContainerBegin(ProgramStateRef State,
                                     const MemRegion *Cont, const Expr *E,
                                     QualType T, const LocationContext *LCtx,
                                     unsigned BlockCount);
ProgramStateRef createContainerEnd(ProgramStateRef State, const MemRegion *Cont,
                                   const Expr *E, QualType T,
                                   const LocationContext *LCtx,
                                   unsigned BlockCount);
ProgramStateRef setContainerData(ProgramStateRef State, const MemRegion *Cont,
                                 const ContainerData &CData);
ProgramStateRef invalidateAllIteratorPositions(ProgramStateRef State,
                                               const MemRegion *Cont);
ProgramStateRef
invalidateAllIteratorPositionsExcept(ProgramStateRef State,
                                     const MemRegion *Cont, SymbolRef Offset,
                                     BinaryOperator::Opcode Opc);
ProgramStateRef invalidateIteratorPositions(ProgramStateRef State,
                                            SymbolRef Offset,
                                            BinaryOperator::Opcode Opc);
ProgramStateRef invalidateIteratorPositions(ProgramStateRef State,
                                            SymbolRef Offset1,
                                            BinaryOperator::Opcode Opc1,
                                            SymbolRef Offset2,
                                            BinaryOperator::Opcode Opc2);
ProgramStateRef reassignAllIteratorPositions(ProgramStateRef State,
                                             const MemRegion *Cont,
                                             const MemRegion *NewCont);
ProgramStateRef reassignAllIteratorPositionsUnless(ProgramStateRef State,
                                                   const MemRegion *Cont,
                                                   const MemRegion *NewCont,
                                                   SymbolRef Offset,
                                                   BinaryOperator::Opcode Opc);
ProgramStateRef rebaseSymbolInIteratorPositionsIf(
    ProgramStateRef State, SValBuilder &SVB, SymbolRef OldSym,
    SymbolRef NewSym, SymbolRef CondSym, BinaryOperator::Opcode Opc);
SymbolRef rebaseSymbol(ProgramStateRef State, SValBuilder &SVB, SymbolRef Expr,
                        SymbolRef OldSym, SymbolRef NewSym);
bool hasLiveIterators(ProgramStateRef State, const MemRegion *Cont);

} // namespace

void ContainerModeling::checkPostCall(const CallEvent &Call,
                                     CheckerContext &C) const {
  const auto *Func = dyn_cast_or_null<FunctionDecl>(Call.getDecl());
  if (!Func)
    return;

  if (Func->isOverloadedOperator()) {
    const auto Op = Func->getOverloadedOperator();
    if (Op == OO_Equal) {
      // Overloaded 'operator=' must be a non-static member function.
      const auto *InstCall = cast<CXXInstanceCall>(&Call);
      if (cast<CXXMethodDecl>(Func)->isMoveAssignmentOperator()) {
        handleAssignment(C, InstCall->getCXXThisVal(), Call.getOriginExpr(),
                     Call.getArgSVal(0));
        return;
      }

      handleAssignment(C, InstCall->getCXXThisVal());
      return;
    }
  } else {
    if (const auto *InstCall = dyn_cast<CXXInstanceCall>(&Call)) {
      const NoItParamFn *Handler0 = NoIterParamFunctions.lookup(Call);
      if (Handler0) {
        (this->**Handler0)(C, InstCall->getCXXThisVal(),
                           InstCall->getCXXThisExpr());
        return;
      }

      const OneItParamFn *Handler1 = OneIterParamFunctions.lookup(Call);
      if (Handler1) {
        (this->**Handler1)(C, InstCall->getCXXThisVal(), Call.getArgSVal(0));
        return;
      }

      const TwoItParamFn *Handler2 = TwoIterParamFunctions.lookup(Call);
      if (Handler2) {
        (this->**Handler2)(C, InstCall->getCXXThisVal(), Call.getArgSVal(0),
                           Call.getArgSVal(1));
        return;
      }

      const auto *OrigExpr = Call.getOriginExpr();
      if (!OrigExpr)
        return;

      if (isBeginCall(Func)) {
        handleBegin(C, OrigExpr, Call.getReturnValue(),
                    InstCall->getCXXThisVal());
        return;
      }

      if (isEndCall(Func)) {
        handleEnd(C, OrigExpr, Call.getReturnValue(),
                  InstCall->getCXXThisVal());
        return;
      }
    }
  }
}

void ContainerModeling::checkLiveSymbols(ProgramStateRef State,
                                         SymbolReaper &SR) const {
  // Keep symbolic expressions of container begins and ends alive
  auto ContMap = State->get<ContainerMap>();
  for (const auto &Cont : ContMap) {
    const auto CData = Cont.second;
    if (CData.getBegin()) {
      SR.markLive(CData.getBegin());
      if(const auto *SIE = dyn_cast<SymIntExpr>(CData.getBegin()))
        SR.markLive(SIE->getLHS());
    }
    if (CData.getEnd()) {
      SR.markLive(CData.getEnd());
      if(const auto *SIE = dyn_cast<SymIntExpr>(CData.getEnd()))
        SR.markLive(SIE->getLHS());
    }
  }
}

void ContainerModeling::checkDeadSymbols(SymbolReaper &SR,
                                         CheckerContext &C) const {
  // Cleanup
  auto State = C.getState();

  auto ContMap = State->get<ContainerMap>();
  for (const auto &Cont : ContMap) {
    if (!SR.isLiveRegion(Cont.first)) {
      // We must keep the container data while it has live iterators to be able
      // to compare them to the begin and the end of the container.
      if (!hasLiveIterators(State, Cont.first)) {
        State = State->remove<ContainerMap>(Cont.first);
      }
    }
  }

  C.addTransition(State);
}

void ContainerModeling::handleBegin(CheckerContext &C, const Expr *CE,
                                   SVal RetVal, SVal Cont) const {
  const auto *ContReg = Cont.getAsRegion();
  if (!ContReg)
    return;

  ContReg = ContReg->getMostDerivedObjectRegion();

  // If the container already has a begin symbol then use it. Otherwise first
  // create a new one.
  auto State = C.getState();
  auto BeginSym = getContainerBegin(State, ContReg);
  if (!BeginSym) {
    State = createContainerBegin(State, ContReg, CE, C.getASTContext().LongTy,
                                 C.getLocationContext(), C.blockCount());
    BeginSym = getContainerBegin(State, ContReg);
  }
  State = setIteratorPosition(State, RetVal,
                              IteratorPosition::getPosition(ContReg, BeginSym));
  C.addTransition(State);
}

void ContainerModeling::handleEnd(CheckerContext &C, const Expr *CE,
                                 SVal RetVal, SVal Cont) const {
  const auto *ContReg = Cont.getAsRegion();
  if (!ContReg)
    return;

  ContReg = ContReg->getMostDerivedObjectRegion();

  // If the container already has an end symbol then use it. Otherwise first
  // create a new one.
  auto State = C.getState();
  auto EndSym = getContainerEnd(State, ContReg);
  if (!EndSym) {
    State = createContainerEnd(State, ContReg, CE, C.getASTContext().LongTy,
                               C.getLocationContext(), C.blockCount());
    EndSym = getContainerEnd(State, ContReg);
  }
  State = setIteratorPosition(State, RetVal,
                              IteratorPosition::getPosition(ContReg, EndSym));
  C.addTransition(State);
}

void ContainerModeling::handleAssignment(CheckerContext &C, SVal Cont,
                                         const Expr *CE, SVal OldCont) const {
  const auto *ContReg = Cont.getAsRegion();
  if (!ContReg)
    return;

  ContReg = ContReg->getMostDerivedObjectRegion();

  // Assignment of a new value to a container always invalidates all its
  // iterators
  auto State = C.getState();
  const auto CData = getContainerData(State, ContReg);
  if (CData) {
    State = invalidateAllIteratorPositions(State, ContReg);
  }

  // In case of move, iterators of the old container (except the past-end
  // iterators) remain valid but refer to the new container
  if (!OldCont.isUndef()) {
    const auto *OldContReg = OldCont.getAsRegion();
    if (OldContReg) {
      OldContReg = OldContReg->getMostDerivedObjectRegion();
      const auto OldCData = getContainerData(State, OldContReg);
      if (OldCData) {
        if (const auto OldEndSym = OldCData->getEnd()) {
          // If we already assigned an "end" symbol to the old container, then
          // first reassign all iterator positions to the new container which
          // are not past the container (thus not greater or equal to the
          // current "end" symbol).
          State = reassignAllIteratorPositionsUnless(State, OldContReg, ContReg,
                                                     OldEndSym, BO_GE);
          auto &SymMgr = C.getSymbolManager();
          auto &SVB = C.getSValBuilder();
          // Then generate and assign a new "end" symbol for the new container.
          auto NewEndSym =
              SymMgr.conjureSymbol(CE, C.getLocationContext(),
                                   C.getASTContext().LongTy, C.blockCount());
          State = assumeNoOverflow(State, NewEndSym, 4);
          if (CData) {
            State = setContainerData(State, ContReg, CData->newEnd(NewEndSym));
          } else {
            State = setContainerData(State, ContReg,
                                     ContainerData::fromEnd(NewEndSym));
          }
          // Finally, replace the old "end" symbol in the already reassigned
          // iterator positions with the new "end" symbol.
          State = rebaseSymbolInIteratorPositionsIf(
              State, SVB, OldEndSym, NewEndSym, OldEndSym, BO_LT);
        } else {
          // There was no "end" symbol assigned yet to the old container,
          // so reassign all iterator positions to the new container.
          State = reassignAllIteratorPositions(State, OldContReg, ContReg);
        }
        if (const auto OldBeginSym = OldCData->getBegin()) {
          // If we already assigned a "begin" symbol to the old container, then
          // assign it to the new container and remove it from the old one.
          if (CData) {
            State =
                setContainerData(State, ContReg, CData->newBegin(OldBeginSym));
          } else {
            State = setContainerData(State, ContReg,
                                     ContainerData::fromBegin(OldBeginSym));
          }
          State =
              setContainerData(State, OldContReg, OldCData->newBegin(nullptr));
        }
      } else {
        // There was neither "begin" nor "end" symbol assigned yet to the old
        // container, so reassign all iterator positions to the new container.
        State = reassignAllIteratorPositions(State, OldContReg, ContReg);
      }
    }
  }
  C.addTransition(State);
}

void ContainerModeling::handleAssign(CheckerContext &C, SVal Cont,
                                     const Expr *ContE) const {
  const auto *ContReg = Cont.getAsRegion();
  if (!ContReg)
    return;

  ContReg = ContReg->getMostDerivedObjectRegion();

  // The assign() operation invalidates all the iterators
  auto State = C.getState();
  State = invalidateAllIteratorPositions(State, ContReg);
  C.addTransition(State);
}

void ContainerModeling::handleClear(CheckerContext &C, SVal Cont,
                                    const Expr *ContE) const {
  const auto *ContReg = Cont.getAsRegion();
  if (!ContReg)
    return;

  ContReg = ContReg->getMostDerivedObjectRegion();

  // The clear() operation invalidates all the iterators, except the past-end
  // iterators of list-like containers
  auto State = C.getState();
  if (!hasSubscriptOperator(State, ContReg) ||
      !backModifiable(State, ContReg)) {
    const auto CData = getContainerData(State, ContReg);
    if (CData) {
      if (const auto EndSym = CData->getEnd()) {
        State =
            invalidateAllIteratorPositionsExcept(State, ContReg, EndSym, BO_GE);
        C.addTransition(State);
        return;
      }
    }
  }
  const NoteTag *ChangeTag =
    getChangeTag(C, "became empty", ContReg, ContE);
  State = invalidateAllIteratorPositions(State, ContReg);
  C.addTransition(State, ChangeTag);
}

void ContainerModeling::handlePushBack(CheckerContext &C, SVal Cont,
                                       const Expr *ContE) const {
  const auto *ContReg = Cont.getAsRegion();
  if (!ContReg)
    return;

  ContReg = ContReg->getMostDerivedObjectRegion();

  // For deque-like containers invalidate all iterator positions
  auto State = C.getState();
  if (hasSubscriptOperator(State, ContReg) && frontModifiable(State, ContReg)) {
    State = invalidateAllIteratorPositions(State, ContReg);
    C.addTransition(State);
    return;
  }

  const auto CData = getContainerData(State, ContReg);
  if (!CData)
    return;

  // For vector-like containers invalidate the past-end iterator positions
  if (const auto EndSym = CData->getEnd()) {
    if (hasSubscriptOperator(State, ContReg)) {
      State = invalidateIteratorPositions(State, EndSym, BO_GE);
    }
    auto &SymMgr = C.getSymbolManager();
    auto &BVF = SymMgr.getBasicVals();
    auto &SVB = C.getSValBuilder();
    const auto newEndSym =
      SVB.evalBinOp(State, BO_Add,
                    nonloc::SymbolVal(EndSym),
                    nonloc::ConcreteInt(BVF.getValue(llvm::APSInt::get(1))),
                    SymMgr.getType(EndSym)).getAsSymbol();
    const NoteTag *ChangeTag =
      getChangeTag(C, "extended to the back by 1 position", ContReg, ContE);
    State = setContainerData(State, ContReg, CData->newEnd(newEndSym));
    C.addTransition(State, ChangeTag);
  }
}

void ContainerModeling::handlePopBack(CheckerContext &C, SVal Cont,
                                      const Expr *ContE) const {
  const auto *ContReg = Cont.getAsRegion();
  if (!ContReg)
    return;

  ContReg = ContReg->getMostDerivedObjectRegion();

  auto State = C.getState();
  const auto CData = getContainerData(State, ContReg);
  if (!CData)
    return;

  if (const auto EndSym = CData->getEnd()) {
    auto &SymMgr = C.getSymbolManager();
    auto &BVF = SymMgr.getBasicVals();
    auto &SVB = C.getSValBuilder();
    const auto BackSym =
      SVB.evalBinOp(State, BO_Sub,
                    nonloc::SymbolVal(EndSym),
                    nonloc::ConcreteInt(BVF.getValue(llvm::APSInt::get(1))),
                    SymMgr.getType(EndSym)).getAsSymbol();
    const NoteTag *ChangeTag =
      getChangeTag(C, "shrank from the back by 1 position", ContReg, ContE);
    // For vector-like and deque-like containers invalidate the last and the
    // past-end iterator positions. For list-like containers only invalidate
    // the last position
    if (hasSubscriptOperator(State, ContReg) &&
        backModifiable(State, ContReg)) {
      State = invalidateIteratorPositions(State, BackSym, BO_GE);
      State = setContainerData(State, ContReg, CData->newEnd(nullptr));
    } else {
      State = invalidateIteratorPositions(State, BackSym, BO_EQ);
    }
    auto newEndSym = BackSym;
    State = setContainerData(State, ContReg, CData->newEnd(newEndSym));
    C.addTransition(State, ChangeTag);
  }
}

void ContainerModeling::handlePushFront(CheckerContext &C, SVal Cont,
                                        const Expr *ContE) const {
  const auto *ContReg = Cont.getAsRegion();
  if (!ContReg)
    return;

  ContReg = ContReg->getMostDerivedObjectRegion();

  // For deque-like containers invalidate all iterator positions
  auto State = C.getState();
  if (hasSubscriptOperator(State, ContReg)) {
    State = invalidateAllIteratorPositions(State, ContReg);
    C.addTransition(State);
  } else {
    const auto CData = getContainerData(State, ContReg);
    if (!CData)
      return;

    if (const auto BeginSym = CData->getBegin()) {
      auto &SymMgr = C.getSymbolManager();
      auto &BVF = SymMgr.getBasicVals();
      auto &SVB = C.getSValBuilder();
      const auto newBeginSym =
        SVB.evalBinOp(State, BO_Sub,
                      nonloc::SymbolVal(BeginSym),
                      nonloc::ConcreteInt(BVF.getValue(llvm::APSInt::get(1))),
                      SymMgr.getType(BeginSym)).getAsSymbol();
      const NoteTag *ChangeTag =
        getChangeTag(C, "extended to the front by 1 position", ContReg, ContE);
      State = setContainerData(State, ContReg, CData->newBegin(newBeginSym));
      C.addTransition(State, ChangeTag);
    }
  }
}

void ContainerModeling::handlePopFront(CheckerContext &C, SVal Cont,
                                       const Expr *ContE) const {
  const auto *ContReg = Cont.getAsRegion();
  if (!ContReg)
    return;

  ContReg = ContReg->getMostDerivedObjectRegion();

  auto State = C.getState();
  const auto CData = getContainerData(State, ContReg);
  if (!CData)
    return;

  // For deque-like containers invalidate all iterator positions. For list-like
  // iterators only invalidate the first position
  if (const auto BeginSym = CData->getBegin()) {
    if (hasSubscriptOperator(State, ContReg)) {
      State = invalidateIteratorPositions(State, BeginSym, BO_LE);
    } else {
      State = invalidateIteratorPositions(State, BeginSym, BO_EQ);
    }
    auto &SymMgr = C.getSymbolManager();
    auto &BVF = SymMgr.getBasicVals();
    auto &SVB = C.getSValBuilder();
    const auto newBeginSym =
      SVB.evalBinOp(State, BO_Add,
                    nonloc::SymbolVal(BeginSym),
                    nonloc::ConcreteInt(BVF.getValue(llvm::APSInt::get(1))),
                    SymMgr.getType(BeginSym)).getAsSymbol();
    const NoteTag *ChangeTag =
      getChangeTag(C, "shrank from the front by 1 position", ContReg, ContE);
    State = setContainerData(State, ContReg, CData->newBegin(newBeginSym));
    C.addTransition(State, ChangeTag);
  }
}

void ContainerModeling::handleInsert(CheckerContext &C, SVal Cont,
                                     SVal Iter) const {
  const auto *ContReg = Cont.getAsRegion();
  if (!ContReg)
    return;

  ContReg = ContReg->getMostDerivedObjectRegion();

  auto State = C.getState();
  const auto *Pos = getIteratorPosition(State, Iter);
  if (!Pos)
    return;

  // For deque-like containers invalidate all iterator positions. For
  // vector-like containers invalidate iterator positions after the insertion.
  if (hasSubscriptOperator(State, ContReg) && backModifiable(State, ContReg)) {
    if (frontModifiable(State, ContReg)) {
      State = invalidateAllIteratorPositions(State, ContReg);
    } else {
      State = invalidateIteratorPositions(State, Pos->getOffset(), BO_GE);
    }
    if (const auto *CData = getContainerData(State, ContReg)) {
      if (const auto EndSym = CData->getEnd()) {
        State = invalidateIteratorPositions(State, EndSym, BO_GE);
        State = setContainerData(State, ContReg, CData->newEnd(nullptr));
      }
    }
    C.addTransition(State);
  }
}

void ContainerModeling::handleErase(CheckerContext &C, SVal Cont,
                                    SVal Iter) const {
  const auto *ContReg = Cont.getAsRegion();
  if (!ContReg)
    return;

  ContReg = ContReg->getMostDerivedObjectRegion();

  auto State = C.getState();
  const auto *Pos = getIteratorPosition(State, Iter);
  if (!Pos)
    return;

  // For deque-like containers invalidate all iterator positions. For
  // vector-like containers invalidate iterator positions at and after the
  // deletion. For list-like containers only invalidate the deleted position.
  if (hasSubscriptOperator(State, ContReg) && backModifiable(State, ContReg)) {
    if (frontModifiable(State, ContReg)) {
      State = invalidateAllIteratorPositions(State, ContReg);
    } else {
      State = invalidateIteratorPositions(State, Pos->getOffset(), BO_GE);
    }
    if (const auto *CData = getContainerData(State, ContReg)) {
      if (const auto EndSym = CData->getEnd()) {
        State = invalidateIteratorPositions(State, EndSym, BO_GE);
        State = setContainerData(State, ContReg, CData->newEnd(nullptr));
      }
    }
  } else {
    State = invalidateIteratorPositions(State, Pos->getOffset(), BO_EQ);
  }
  C.addTransition(State);
}

void ContainerModeling::handleErase(CheckerContext &C, SVal Cont, SVal Iter1,
                                    SVal Iter2) const {
  const auto *ContReg = Cont.getAsRegion();
  if (!ContReg)
    return;

  ContReg = ContReg->getMostDerivedObjectRegion();
  auto State = C.getState();
  const auto *Pos1 = getIteratorPosition(State, Iter1);
  const auto *Pos2 = getIteratorPosition(State, Iter2);
  if (!Pos1 || !Pos2)
    return;

  // For deque-like containers invalidate all iterator positions. For
  // vector-like containers invalidate iterator positions at and after the
  // deletion range. For list-like containers only invalidate the deleted
  // position range [first..last].
  if (hasSubscriptOperator(State, ContReg) && backModifiable(State, ContReg)) {
    if (frontModifiable(State, ContReg)) {
      State = invalidateAllIteratorPositions(State, ContReg);
    } else {
      State = invalidateIteratorPositions(State, Pos1->getOffset(), BO_GE);
    }
    if (const auto *CData = getContainerData(State, ContReg)) {
      if (const auto EndSym = CData->getEnd()) {
        State = invalidateIteratorPositions(State, EndSym, BO_GE);
        State = setContainerData(State, ContReg, CData->newEnd(nullptr));
      }
    }
  } else {
    State = invalidateIteratorPositions(State, Pos1->getOffset(), BO_GE,
                                        Pos2->getOffset(), BO_LT);
  }
  C.addTransition(State);
}

void ContainerModeling::handleEraseAfter(CheckerContext &C, SVal Cont,
                                        SVal Iter) const {
  auto State = C.getState();
  const auto *Pos = getIteratorPosition(State, Iter);
  if (!Pos)
    return;

  // Invalidate the deleted iterator position, which is the position of the
  // parameter plus one.
  auto &SymMgr = C.getSymbolManager();
  auto &BVF = SymMgr.getBasicVals();
  auto &SVB = C.getSValBuilder();
  const auto NextSym =
    SVB.evalBinOp(State, BO_Add,
                  nonloc::SymbolVal(Pos->getOffset()),
                  nonloc::ConcreteInt(BVF.getValue(llvm::APSInt::get(1))),
                  SymMgr.getType(Pos->getOffset())).getAsSymbol();
  State = invalidateIteratorPositions(State, NextSym, BO_EQ);
  C.addTransition(State);
}

void ContainerModeling::handleEraseAfter(CheckerContext &C, SVal Cont,
                                         SVal Iter1, SVal Iter2) const {
  auto State = C.getState();
  const auto *Pos1 = getIteratorPosition(State, Iter1);
  const auto *Pos2 = getIteratorPosition(State, Iter2);
  if (!Pos1 || !Pos2)
    return;

  // Invalidate the deleted iterator position range (first..last)
  State = invalidateIteratorPositions(State, Pos1->getOffset(), BO_GT,
                                      Pos2->getOffset(), BO_LT);
  C.addTransition(State);
}

const NoteTag *ContainerModeling::getChangeTag(CheckerContext &C,
                                               StringRef Text,
                                               const MemRegion *ContReg,
                                               const Expr *ContE) const {
  StringRef Name;
  // First try to get the name of the variable from the region
  if (const auto *DR = dyn_cast<DeclRegion>(ContReg)) {
    Name = DR->getDecl()->getName();
  // If the region is not a `DeclRegion` then use the expression instead
  } else if (const auto *DRE =
             dyn_cast<DeclRefExpr>(ContE->IgnoreParenCasts())) {
    Name = DRE->getDecl()->getName();
  }

  return C.getNoteTag(
      [Text, Name, ContReg](PathSensitiveBugReport &BR) -> std::string {
        if (!BR.isInteresting(ContReg))
          return "";

        SmallString<256> Msg;
        llvm::raw_svector_ostream Out(Msg);
        Out << "Container " << (!Name.empty() ? ("'" + Name.str() + "' ") : "" )
            << Text;
        return std::string(Out.str());
      });
}

void ContainerModeling::printState(raw_ostream &Out, ProgramStateRef State,
                                  const char *NL, const char *Sep) const {
  auto ContMap = State->get<ContainerMap>();

  if (!ContMap.isEmpty()) {
    Out << Sep << "Container Data :" << NL;
    for (const auto &Cont : ContMap) {
      Cont.first->dumpToStream(Out);
      Out << " : [ ";
      const auto CData = Cont.second;
      if (CData.getBegin())
        CData.getBegin()->dumpToStream(Out);
      else
        Out << "<Unknown>";
      Out << " .. ";
      if (CData.getEnd())
        CData.getEnd()->dumpToStream(Out);
      else
        Out << "<Unknown>";
      Out << " ]";
    }
  }
}

namespace {

bool isBeginCall(const FunctionDecl *Func) {
  const auto *IdInfo = Func->getIdentifier();
  if (!IdInfo)
    return false;
  return IdInfo->getName().ends_with_insensitive("begin");
}

bool isEndCall(const FunctionDecl *Func) {
  const auto *IdInfo = Func->getIdentifier();
  if (!IdInfo)
    return false;
  return IdInfo->getName().ends_with_insensitive("end");
}

const CXXRecordDecl *getCXXRecordDecl(ProgramStateRef State,
                                      const MemRegion *Reg) {
  auto TI = getDynamicTypeInfo(State, Reg);
  if (!TI.isValid())
    return nullptr;

  auto Type = TI.getType();
  if (const auto *RefT = Type->getAs<ReferenceType>()) {
    Type = RefT->getPointeeType();
  }

  if (const auto *PtrT = Type->getAs<PointerType>()) {
    Type = PtrT->getPointeeType();
  }

  return Type->getUnqualifiedDesugaredType()->getAsCXXRecordDecl();
}

bool hasSubscriptOperator(ProgramStateRef State, const MemRegion *Reg) {
  const auto *CRD = getCXXRecordDecl(State, Reg);
  if (!CRD)
    return false;

  for (const auto *Method : CRD->methods()) {
    if (!Method->isOverloadedOperator())
      continue;
    const auto OPK = Method->getOverloadedOperator();
    if (OPK == OO_Subscript) {
      return true;
    }
  }
  return false;
}

bool frontModifiable(ProgramStateRef State, const MemRegion *Reg) {
  const auto *CRD = getCXXRecordDecl(State, Reg);
  if (!CRD)
    return false;

  for (const auto *Method : CRD->methods()) {
    if (!Method->getDeclName().isIdentifier())
      continue;
    if (Method->getName() == "push_front" || Method->getName() == "pop_front") {
      return true;
    }
  }
  return false;
}

bool backModifiable(ProgramStateRef State, const MemRegion *Reg) {
  const auto *CRD = getCXXRecordDecl(State, Reg);
  if (!CRD)
    return false;

  for (const auto *Method : CRD->methods()) {
    if (!Method->getDeclName().isIdentifier())
      continue;
    if (Method->getName() == "push_back" || Method->getName() == "pop_back") {
      return true;
    }
  }
  return false;
}

SymbolRef getContainerBegin(ProgramStateRef State, const MemRegion *Cont) {
  const auto *CDataPtr = getContainerData(State, Cont);
  if (!CDataPtr)
    return nullptr;

  return CDataPtr->getBegin();
}

SymbolRef getContainerEnd(ProgramStateRef State, const MemRegion *Cont) {
  const auto *CDataPtr = getContainerData(State, Cont);
  if (!CDataPtr)
    return nullptr;

  return CDataPtr->getEnd();
}

ProgramStateRef createContainerBegin(ProgramStateRef State,
                                     const MemRegion *Cont, const Expr *E,
                                     QualType T, const LocationContext *LCtx,
                                     unsigned BlockCount) {
  // Only create if it does not exist
  const auto *CDataPtr = getContainerData(State, Cont);
  if (CDataPtr && CDataPtr->getBegin())
    return State;

  auto &SymMgr = State->getSymbolManager();
  const SymbolConjured *Sym = SymMgr.conjureSymbol(E, LCtx, T, BlockCount,
                                                   "begin");
  State = assumeNoOverflow(State, Sym, 4);

  if (CDataPtr) {
    const auto CData = CDataPtr->newBegin(Sym);
    return setContainerData(State, Cont, CData);
  }

  const auto CData = ContainerData::fromBegin(Sym);
  return setContainerData(State, Cont, CData);
}

ProgramStateRef createContainerEnd(ProgramStateRef State, const MemRegion *Cont,
                                   const Expr *E, QualType T,
                                   const LocationContext *LCtx,
                                   unsigned BlockCount) {
  // Only create if it does not exist
  const auto *CDataPtr = getContainerData(State, Cont);
  if (CDataPtr && CDataPtr->getEnd())
    return State;

  auto &SymMgr = State->getSymbolManager();
  const SymbolConjured *Sym = SymMgr.conjureSymbol(E, LCtx, T, BlockCount,
                                                  "end");
  State = assumeNoOverflow(State, Sym, 4);

  if (CDataPtr) {
    const auto CData = CDataPtr->newEnd(Sym);
    return setContainerData(State, Cont, CData);
  }

  const auto CData = ContainerData::fromEnd(Sym);
  return setContainerData(State, Cont, CData);
}

ProgramStateRef setContainerData(ProgramStateRef State, const MemRegion *Cont,
                                 const ContainerData &CData) {
  return State->set<ContainerMap>(Cont, CData);
}

template <typename Condition, typename Process>
ProgramStateRef processIteratorPositions(ProgramStateRef State, Condition Cond,
                                         Process Proc) {
  auto &RegionMapFactory = State->get_context<IteratorRegionMap>();
  auto RegionMap = State->get<IteratorRegionMap>();
  bool Changed = false;
  for (const auto &Reg : RegionMap) {
    if (Cond(Reg.second)) {
      RegionMap = RegionMapFactory.add(RegionMap, Reg.first, Proc(Reg.second));
      Changed = true;
    }
  }

  if (Changed)
    State = State->set<IteratorRegionMap>(RegionMap);

  auto &SymbolMapFactory = State->get_context<IteratorSymbolMap>();
  auto SymbolMap = State->get<IteratorSymbolMap>();
  Changed = false;
  for (const auto &Sym : SymbolMap) {
    if (Cond(Sym.second)) {
      SymbolMap = SymbolMapFactory.add(SymbolMap, Sym.first, Proc(Sym.second));
      Changed = true;
    }
  }

  if (Changed)
    State = State->set<IteratorSymbolMap>(SymbolMap);

  return State;
}

ProgramStateRef invalidateAllIteratorPositions(ProgramStateRef State,
                                               const MemRegion *Cont) {
  auto MatchCont = [&](const IteratorPosition &Pos) {
    return Pos.getContainer() == Cont;
  };
  auto Invalidate = [&](const IteratorPosition &Pos) {
    return Pos.invalidate();
  };
  return processIteratorPositions(State, MatchCont, Invalidate);
}

ProgramStateRef
invalidateAllIteratorPositionsExcept(ProgramStateRef State,
                                     const MemRegion *Cont, SymbolRef Offset,
                                     BinaryOperator::Opcode Opc) {
  auto MatchContAndCompare = [&](const IteratorPosition &Pos) {
    return Pos.getContainer() == Cont &&
           !compare(State, Pos.getOffset(), Offset, Opc);
  };
  auto Invalidate = [&](const IteratorPosition &Pos) {
    return Pos.invalidate();
  };
  return processIteratorPositions(State, MatchContAndCompare, Invalidate);
}

ProgramStateRef invalidateIteratorPositions(ProgramStateRef State,
                                            SymbolRef Offset,
                                            BinaryOperator::Opcode Opc) {
  auto Compare = [&](const IteratorPosition &Pos) {
    return compare(State, Pos.getOffset(), Offset, Opc);
  };
  auto Invalidate = [&](const IteratorPosition &Pos) {
    return Pos.invalidate();
  };
  return processIteratorPositions(State, Compare, Invalidate);
}

ProgramStateRef invalidateIteratorPositions(ProgramStateRef State,
                                            SymbolRef Offset1,
                                            BinaryOperator::Opcode Opc1,
                                            SymbolRef Offset2,
                                            BinaryOperator::Opcode Opc2) {
  auto Compare = [&](const IteratorPosition &Pos) {
    return compare(State, Pos.getOffset(), Offset1, Opc1) &&
           compare(State, Pos.getOffset(), Offset2, Opc2);
  };
  auto Invalidate = [&](const IteratorPosition &Pos) {
    return Pos.invalidate();
  };
  return processIteratorPositions(State, Compare, Invalidate);
}

ProgramStateRef reassignAllIteratorPositions(ProgramStateRef State,
                                             const MemRegion *Cont,
                                             const MemRegion *NewCont) {
  auto MatchCont = [&](const IteratorPosition &Pos) {
    return Pos.getContainer() == Cont;
  };
  auto ReAssign = [&](const IteratorPosition &Pos) {
    return Pos.reAssign(NewCont);
  };
  return processIteratorPositions(State, MatchCont, ReAssign);
}

ProgramStateRef reassignAllIteratorPositionsUnless(ProgramStateRef State,
                                                   const MemRegion *Cont,
                                                   const MemRegion *NewCont,
                                                   SymbolRef Offset,
                                                   BinaryOperator::Opcode Opc) {
  auto MatchContAndCompare = [&](const IteratorPosition &Pos) {
    return Pos.getContainer() == Cont &&
    !compare(State, Pos.getOffset(), Offset, Opc);
  };
  auto ReAssign = [&](const IteratorPosition &Pos) {
    return Pos.reAssign(NewCont);
  };
  return processIteratorPositions(State, MatchContAndCompare, ReAssign);
}

// This function rebases symbolic expression `OldSym + Int` to `NewSym + Int`,
// `OldSym - Int` to `NewSym - Int` and  `OldSym` to `NewSym` in any iterator
// position offsets where `CondSym` is true.
ProgramStateRef rebaseSymbolInIteratorPositionsIf(
    ProgramStateRef State, SValBuilder &SVB, SymbolRef OldSym,
    SymbolRef NewSym, SymbolRef CondSym, BinaryOperator::Opcode Opc) {
  auto LessThanEnd = [&](const IteratorPosition &Pos) {
    return compare(State, Pos.getOffset(), CondSym, Opc);
  };
  auto RebaseSymbol = [&](const IteratorPosition &Pos) {
    return Pos.setTo(rebaseSymbol(State, SVB, Pos.getOffset(), OldSym,
                                   NewSym));
  };
  return processIteratorPositions(State, LessThanEnd, RebaseSymbol);
}

// This function rebases symbolic expression `OldExpr + Int` to `NewExpr + Int`,
// `OldExpr - Int` to `NewExpr - Int` and  `OldExpr` to `NewExpr` in expression
// `OrigExpr`.
SymbolRef rebaseSymbol(ProgramStateRef State, SValBuilder &SVB,
                       SymbolRef OrigExpr, SymbolRef OldExpr,
                       SymbolRef NewSym) {
  auto &SymMgr = SVB.getSymbolManager();
  auto Diff = SVB.evalBinOpNN(State, BO_Sub, nonloc::SymbolVal(OrigExpr),
                              nonloc::SymbolVal(OldExpr),
                              SymMgr.getType(OrigExpr));

  const auto DiffInt = Diff.getAs<nonloc::ConcreteInt>();
  if (!DiffInt)
    return OrigExpr;

  return SVB.evalBinOpNN(State, BO_Add, *DiffInt, nonloc::SymbolVal(NewSym),
                         SymMgr.getType(OrigExpr)).getAsSymbol();
}

bool hasLiveIterators(ProgramStateRef State, const MemRegion *Cont) {
  auto RegionMap = State->get<IteratorRegionMap>();
  for (const auto &Reg : RegionMap) {
    if (Reg.second.getContainer() == Cont)
      return true;
  }

  auto SymbolMap = State->get<IteratorSymbolMap>();
  for (const auto &Sym : SymbolMap) {
    if (Sym.second.getContainer() == Cont)
      return true;
  }

  return false;
}

} // namespace

void ento::registerContainerModeling(CheckerManager &mgr) {
  mgr.registerChecker<ContainerModeling>();
}

bool ento::shouldRegisterContainerModeling(const CheckerManager &mgr) {
  if (!mgr.getLangOpts().CPlusPlus)
    return false;

  if (!mgr.getAnalyzerOptions().ShouldAggressivelySimplifyBinaryOperation) {
    mgr.getASTContext().getDiagnostics().Report(
        diag::err_analyzer_checker_incompatible_analyzer_option)
      << "aggressive-binary-operation-simplification" << "false";
    return false;
  }

  return true;
}
