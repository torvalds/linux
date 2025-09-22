//=== ConversionChecker.cpp -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Check that there is no loss of sign/precision in assignments, comparisons
// and multiplications.
//
// ConversionChecker uses path sensitive analysis to determine possible values
// of expressions. A warning is reported when:
// * a negative value is implicitly converted to an unsigned value in an
//   assignment, comparison or multiplication.
// * assignment / initialization when the source value is greater than the max
//   value of the target integer type
// * assignment / initialization when the source integer is above the range
//   where the target floating point type can represent all integers
//
// Many compilers and tools have similar checks that are based on semantic
// analysis. Those checks are sound but have poor precision. ConversionChecker
// is an alternative to those checks.
//
//===----------------------------------------------------------------------===//
#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/AST/ParentMap.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "llvm/ADT/APFloat.h"

#include <climits>

using namespace clang;
using namespace ento;

namespace {
class ConversionChecker : public Checker<check::PreStmt<ImplicitCastExpr>> {
public:
  void checkPreStmt(const ImplicitCastExpr *Cast, CheckerContext &C) const;

private:
  const BugType BT{this, "Conversion"};

  bool isLossOfPrecision(const ImplicitCastExpr *Cast, QualType DestType,
                         CheckerContext &C) const;

  bool isLossOfSign(const ImplicitCastExpr *Cast, CheckerContext &C) const;

  void reportBug(ExplodedNode *N, const Expr *E, CheckerContext &C,
                 const char Msg[]) const;
};
}

void ConversionChecker::checkPreStmt(const ImplicitCastExpr *Cast,
                                     CheckerContext &C) const {
  // Don't warn for implicit conversions to bool
  if (Cast->getType()->isBooleanType())
    return;

  // Don't warn for loss of sign/precision in macros.
  if (Cast->getExprLoc().isMacroID())
    return;

  // Get Parent.
  const ParentMap &PM = C.getLocationContext()->getParentMap();
  const Stmt *Parent = PM.getParent(Cast);
  if (!Parent)
    return;
  // Dont warn if this is part of an explicit cast
  if (isa<ExplicitCastExpr>(Parent))
    return;

  bool LossOfSign = false;
  bool LossOfPrecision = false;

  // Loss of sign/precision in binary operation.
  if (const auto *B = dyn_cast<BinaryOperator>(Parent)) {
    BinaryOperator::Opcode Opc = B->getOpcode();
    if (Opc == BO_Assign) {
      if (!Cast->IgnoreParenImpCasts()->isEvaluatable(C.getASTContext())) {
        LossOfSign = isLossOfSign(Cast, C);
        LossOfPrecision = isLossOfPrecision(Cast, Cast->getType(), C);
      }
    } else if (Opc == BO_AddAssign || Opc == BO_SubAssign) {
      // No loss of sign.
      LossOfPrecision = isLossOfPrecision(Cast, B->getLHS()->getType(), C);
    } else if (Opc == BO_MulAssign) {
      LossOfSign = isLossOfSign(Cast, C);
      LossOfPrecision = isLossOfPrecision(Cast, B->getLHS()->getType(), C);
    } else if (Opc == BO_DivAssign || Opc == BO_RemAssign) {
      LossOfSign = isLossOfSign(Cast, C);
      // No loss of precision.
    } else if (Opc == BO_AndAssign) {
      LossOfSign = isLossOfSign(Cast, C);
      // No loss of precision.
    } else if (Opc == BO_OrAssign || Opc == BO_XorAssign) {
      LossOfSign = isLossOfSign(Cast, C);
      LossOfPrecision = isLossOfPrecision(Cast, B->getLHS()->getType(), C);
    } else if (B->isRelationalOp() || B->isMultiplicativeOp()) {
      LossOfSign = isLossOfSign(Cast, C);
    }
  } else if (isa<DeclStmt, ReturnStmt>(Parent)) {
    if (!Cast->IgnoreParenImpCasts()->isEvaluatable(C.getASTContext())) {
      LossOfSign = isLossOfSign(Cast, C);
      LossOfPrecision = isLossOfPrecision(Cast, Cast->getType(), C);
    }
  } else {
    LossOfSign = isLossOfSign(Cast, C);
    LossOfPrecision = isLossOfPrecision(Cast, Cast->getType(), C);
  }

  if (LossOfSign || LossOfPrecision) {
    // Generate an error node.
    ExplodedNode *N = C.generateNonFatalErrorNode(C.getState());
    if (!N)
      return;
    if (LossOfSign)
      reportBug(N, Cast, C, "Loss of sign in implicit conversion");
    if (LossOfPrecision)
      reportBug(N, Cast, C, "Loss of precision in implicit conversion");
  }
}

void ConversionChecker::reportBug(ExplodedNode *N, const Expr *E,
                                  CheckerContext &C, const char Msg[]) const {
  // Generate a report for this bug.
  auto R = std::make_unique<PathSensitiveBugReport>(BT, Msg, N);
  bugreporter::trackExpressionValue(N, E, *R);
  C.emitReport(std::move(R));
}

bool ConversionChecker::isLossOfPrecision(const ImplicitCastExpr *Cast,
                                          QualType DestType,
                                          CheckerContext &C) const {
  // Don't warn about explicit loss of precision.
  if (Cast->isEvaluatable(C.getASTContext()))
    return false;

  QualType SubType = Cast->IgnoreParenImpCasts()->getType();

  if (!DestType->isRealType() || !SubType->isIntegerType())
    return false;

  const bool isFloat = DestType->isFloatingType();

  const auto &AC = C.getASTContext();

  // We will find the largest RepresentsUntilExp value such that the DestType
  // can exactly represent all nonnegative integers below 2^RepresentsUntilExp.
  unsigned RepresentsUntilExp;

  if (isFloat) {
    const llvm::fltSemantics &Sema = AC.getFloatTypeSemantics(DestType);
    RepresentsUntilExp = llvm::APFloat::semanticsPrecision(Sema);
  } else {
    RepresentsUntilExp = AC.getIntWidth(DestType);
    if (RepresentsUntilExp == 1) {
      // This is just casting a number to bool, probably not a bug.
      return false;
    }
    if (DestType->isSignedIntegerType())
      RepresentsUntilExp--;
  }

  if (RepresentsUntilExp >= sizeof(unsigned long long) * CHAR_BIT) {
    // Avoid overflow in our later calculations.
    return false;
  }

  unsigned CorrectedSrcWidth = AC.getIntWidth(SubType);
  if (SubType->isSignedIntegerType())
    CorrectedSrcWidth--;

  if (RepresentsUntilExp >= CorrectedSrcWidth) {
    // Simple case: the destination can store all values of the source type.
    return false;
  }

  unsigned long long MaxVal = 1ULL << RepresentsUntilExp;
  if (isFloat) {
    // If this is a floating point type, it can also represent MaxVal exactly.
    MaxVal++;
  }
  return C.isGreaterOrEqual(Cast->getSubExpr(), MaxVal);
  // TODO: maybe also check negative values with too large magnitude.
}

bool ConversionChecker::isLossOfSign(const ImplicitCastExpr *Cast,
                                     CheckerContext &C) const {
  QualType CastType = Cast->getType();
  QualType SubType = Cast->IgnoreParenImpCasts()->getType();

  if (!CastType->isUnsignedIntegerType() || !SubType->isSignedIntegerType())
    return false;

  return C.isNegative(Cast->getSubExpr());
}

void ento::registerConversionChecker(CheckerManager &mgr) {
  mgr.registerChecker<ConversionChecker>();
}

bool ento::shouldRegisterConversionChecker(const CheckerManager &mgr) {
  return true;
}
