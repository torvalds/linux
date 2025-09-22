//=== PointerSubChecker.cpp - Pointer subtraction checker ------*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This files defines PointerSubChecker, a builtin checker that checks for
// pointer subtractions on two pointers pointing to different memory chunks.
// This check corresponds to CWE-469.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/DynamicExtent.h"
#include "llvm/ADT/StringRef.h"

using namespace clang;
using namespace ento;

namespace {
class PointerSubChecker
  : public Checker< check::PreStmt<BinaryOperator> > {
  const BugType BT{this, "Pointer subtraction"};
  const llvm::StringLiteral Msg_MemRegionDifferent =
      "Subtraction of two pointers that do not point into the same array "
      "is undefined behavior.";
  const llvm::StringLiteral Msg_LargeArrayIndex =
      "Using an array index greater than the array size at pointer subtraction "
      "is undefined behavior.";
  const llvm::StringLiteral Msg_NegativeArrayIndex =
      "Using a negative array index at pointer subtraction "
      "is undefined behavior.";
  const llvm::StringLiteral Msg_BadVarIndex =
      "Indexing the address of a variable with other than 1 at this place "
      "is undefined behavior.";

  bool checkArrayBounds(CheckerContext &C, const Expr *E,
                        const ElementRegion *ElemReg,
                        const MemRegion *Reg) const;

public:
  void checkPreStmt(const BinaryOperator *B, CheckerContext &C) const;
};
}

bool PointerSubChecker::checkArrayBounds(CheckerContext &C, const Expr *E,
                                         const ElementRegion *ElemReg,
                                         const MemRegion *Reg) const {
  if (!ElemReg)
    return true;

  auto ReportBug = [&](const llvm::StringLiteral &Msg) {
    if (ExplodedNode *N = C.generateNonFatalErrorNode()) {
      auto R = std::make_unique<PathSensitiveBugReport>(BT, Msg, N);
      R->addRange(E->getSourceRange());
      C.emitReport(std::move(R));
    }
  };

  ProgramStateRef State = C.getState();
  const MemRegion *SuperReg = ElemReg->getSuperRegion();
  SValBuilder &SVB = C.getSValBuilder();

  if (SuperReg == Reg) {
    if (const llvm::APSInt *I = SVB.getKnownValue(State, ElemReg->getIndex());
        I && (!I->isOne() && !I->isZero()))
      ReportBug(Msg_BadVarIndex);
    return false;
  }

  DefinedOrUnknownSVal ElemCount =
      getDynamicElementCount(State, SuperReg, SVB, ElemReg->getElementType());
  auto IndexTooLarge = SVB.evalBinOp(C.getState(), BO_GT, ElemReg->getIndex(),
                                     ElemCount, SVB.getConditionType())
                           .getAs<DefinedOrUnknownSVal>();
  if (IndexTooLarge) {
    ProgramStateRef S1, S2;
    std::tie(S1, S2) = C.getState()->assume(*IndexTooLarge);
    if (S1 && !S2) {
      ReportBug(Msg_LargeArrayIndex);
      return false;
    }
  }
  auto IndexTooSmall = SVB.evalBinOp(State, BO_LT, ElemReg->getIndex(),
                                     SVB.makeZeroVal(SVB.getArrayIndexType()),
                                     SVB.getConditionType())
                           .getAs<DefinedOrUnknownSVal>();
  if (IndexTooSmall) {
    ProgramStateRef S1, S2;
    std::tie(S1, S2) = State->assume(*IndexTooSmall);
    if (S1 && !S2) {
      ReportBug(Msg_NegativeArrayIndex);
      return false;
    }
  }
  return true;
}

void PointerSubChecker::checkPreStmt(const BinaryOperator *B,
                                     CheckerContext &C) const {
  // When doing pointer subtraction, if the two pointers do not point to the
  // same array, emit a warning.
  if (B->getOpcode() != BO_Sub)
    return;

  SVal LV = C.getSVal(B->getLHS());
  SVal RV = C.getSVal(B->getRHS());

  const MemRegion *LR = LV.getAsRegion();
  const MemRegion *RR = RV.getAsRegion();
  if (!LR || !RR)
    return;

  // Allow subtraction of identical pointers.
  if (LR == RR)
    return;

  // No warning if one operand is unknown.
  if (isa<SymbolicRegion>(LR) || isa<SymbolicRegion>(RR))
    return;

  const auto *ElemLR = dyn_cast<ElementRegion>(LR);
  const auto *ElemRR = dyn_cast<ElementRegion>(RR);

  if (!checkArrayBounds(C, B->getLHS(), ElemLR, RR))
    return;
  if (!checkArrayBounds(C, B->getRHS(), ElemRR, LR))
    return;

  const ValueDecl *DiffDeclL = nullptr;
  const ValueDecl *DiffDeclR = nullptr;

  if (ElemLR && ElemRR) {
    const MemRegion *SuperLR = ElemLR->getSuperRegion();
    const MemRegion *SuperRR = ElemRR->getSuperRegion();
    if (SuperLR == SuperRR)
      return;
    // Allow arithmetic on different symbolic regions.
    if (isa<SymbolicRegion>(SuperLR) || isa<SymbolicRegion>(SuperRR))
      return;
    if (const auto *SuperDLR = dyn_cast<DeclRegion>(SuperLR))
      DiffDeclL = SuperDLR->getDecl();
    if (const auto *SuperDRR = dyn_cast<DeclRegion>(SuperRR))
      DiffDeclR = SuperDRR->getDecl();
  }

  if (ExplodedNode *N = C.generateNonFatalErrorNode()) {
    auto R =
        std::make_unique<PathSensitiveBugReport>(BT, Msg_MemRegionDifferent, N);
    R->addRange(B->getSourceRange());
    // The declarations may be identical even if the regions are different:
    //   struct { int array[10]; } a, b;
    //   do_something(&a.array[5] - &b.array[5]);
    // In this case don't emit notes.
    if (DiffDeclL != DiffDeclR) {
      if (DiffDeclL)
        R->addNote("Array at the left-hand side of subtraction",
                   {DiffDeclL, C.getSourceManager()});
      if (DiffDeclR)
        R->addNote("Array at the right-hand side of subtraction",
                   {DiffDeclR, C.getSourceManager()});
    }
    C.emitReport(std::move(R));
  }
}

void ento::registerPointerSubChecker(CheckerManager &mgr) {
  mgr.registerChecker<PointerSubChecker>();
}

bool ento::shouldRegisterPointerSubChecker(const CheckerManager &mgr) {
  return true;
}
