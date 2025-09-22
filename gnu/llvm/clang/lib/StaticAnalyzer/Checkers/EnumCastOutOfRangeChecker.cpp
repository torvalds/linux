//===- EnumCastOutOfRangeChecker.cpp ---------------------------*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The EnumCastOutOfRangeChecker is responsible for checking integer to
// enumeration casts that could result in undefined values. This could happen
// if the value that we cast from is out of the value range of the enumeration.
// Reference:
// [ISO/IEC 14882-2014] ISO/IEC 14882-2014.
//   Programming Languages — C++, Fourth Edition. 2014.
// C++ Standard, [dcl.enum], in paragraph 8, which defines the range of an enum
// C++ Standard, [expr.static.cast], paragraph 10, which defines the behaviour
//   of casting an integer value that is out of range
// SEI CERT C++ Coding Standard, INT50-CPP. Do not cast to an out-of-range
//   enumeration value
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "llvm/Support/FormatVariadic.h"
#include <optional>

using namespace clang;
using namespace ento;
using llvm::formatv;

namespace {
// This evaluator checks two SVals for equality. The first SVal is provided via
// the constructor, the second is the parameter of the overloaded () operator.
// It uses the in-built ConstraintManager to resolve the equlity to possible or
// not possible ProgramStates.
class ConstraintBasedEQEvaluator {
  const DefinedOrUnknownSVal CompareValue;
  const ProgramStateRef PS;
  SValBuilder &SVB;

public:
  ConstraintBasedEQEvaluator(CheckerContext &C,
                             const DefinedOrUnknownSVal CompareValue)
      : CompareValue(CompareValue), PS(C.getState()), SVB(C.getSValBuilder()) {}

  bool operator()(const llvm::APSInt &EnumDeclInitValue) {
    DefinedOrUnknownSVal EnumDeclValue = SVB.makeIntVal(EnumDeclInitValue);
    DefinedOrUnknownSVal ElemEqualsValueToCast =
        SVB.evalEQ(PS, EnumDeclValue, CompareValue);

    return static_cast<bool>(PS->assume(ElemEqualsValueToCast, true));
  }
};

// This checker checks CastExpr statements.
// If the value provided to the cast is one of the values the enumeration can
// represent, the said value matches the enumeration. If the checker can
// establish the impossibility of matching it gives a warning.
// Being conservative, it does not warn if there is slight possibility the
// value can be matching.
class EnumCastOutOfRangeChecker : public Checker<check::PreStmt<CastExpr>> {
  const BugType EnumValueCastOutOfRange{this, "Enum cast out of range"};
  void reportWarning(CheckerContext &C, const CastExpr *CE,
                     const EnumDecl *E) const;

public:
  void checkPreStmt(const CastExpr *CE, CheckerContext &C) const;
};

using EnumValueVector = llvm::SmallVector<llvm::APSInt, 6>;

// Collects all of the values an enum can represent (as SVals).
EnumValueVector getDeclValuesForEnum(const EnumDecl *ED) {
  EnumValueVector DeclValues(
      std::distance(ED->enumerator_begin(), ED->enumerator_end()));
  llvm::transform(ED->enumerators(), DeclValues.begin(),
                  [](const EnumConstantDecl *D) { return D->getInitVal(); });
  return DeclValues;
}
} // namespace

void EnumCastOutOfRangeChecker::reportWarning(CheckerContext &C,
                                              const CastExpr *CE,
                                              const EnumDecl *E) const {
  assert(E && "valid EnumDecl* is expected");
  if (const ExplodedNode *N = C.generateNonFatalErrorNode()) {
    std::string ValueStr = "", NameStr = "the enum";

    // Try to add details to the message:
    const auto ConcreteValue =
        C.getSVal(CE->getSubExpr()).getAs<nonloc::ConcreteInt>();
    if (ConcreteValue) {
      ValueStr = formatv(" '{0}'", ConcreteValue->getValue());
    }
    if (StringRef EnumName{E->getName()}; !EnumName.empty()) {
      NameStr = formatv("'{0}'", EnumName);
    }

    std::string Msg = formatv("The value{0} provided to the cast expression is "
                              "not in the valid range of values for {1}",
                              ValueStr, NameStr);

    auto BR = std::make_unique<PathSensitiveBugReport>(EnumValueCastOutOfRange,
                                                       Msg, N);
    bugreporter::trackExpressionValue(N, CE->getSubExpr(), *BR);
    BR->addNote("enum declared here",
                PathDiagnosticLocation::create(E, C.getSourceManager()),
                {E->getSourceRange()});
    C.emitReport(std::move(BR));
  }
}

void EnumCastOutOfRangeChecker::checkPreStmt(const CastExpr *CE,
                                             CheckerContext &C) const {

  // Only perform enum range check on casts where such checks are valid.  For
  // all other cast kinds (where enum range checks are unnecessary or invalid),
  // just return immediately.  TODO: The set of casts allowed for enum range
  // checking may be incomplete.  Better to add a missing cast kind to enable a
  // missing check than to generate false negatives and have to remove those
  // later.
  switch (CE->getCastKind()) {
  case CK_IntegralCast:
    break;

  default:
    return;
    break;
  }

  // Get the value of the expression to cast.
  const std::optional<DefinedOrUnknownSVal> ValueToCast =
      C.getSVal(CE->getSubExpr()).getAs<DefinedOrUnknownSVal>();

  // If the value cannot be reasoned about (not even a DefinedOrUnknownSVal),
  // don't analyze further.
  if (!ValueToCast)
    return;

  const QualType T = CE->getType();
  // Check whether the cast type is an enum.
  if (!T->isEnumeralType())
    return;

  // If the cast is an enum, get its declaration.
  // If the isEnumeralType() returned true, then the declaration must exist
  // even if it is a stub declaration. It is up to the getDeclValuesForEnum()
  // function to handle this.
  const EnumDecl *ED = T->castAs<EnumType>()->getDecl();

  EnumValueVector DeclValues = getDeclValuesForEnum(ED);

  // If the declarator list is empty, bail out.
  // Every initialization an enum with a fixed underlying type but without any
  // enumerators would produce a warning if we were to continue at this point.
  // The most notable example is std::byte in the C++17 standard library.
  // TODO: Create heuristics to bail out when the enum type is intended to be
  // used to store combinations of flag values (to mitigate the limitation
  // described in the docs).
  if (DeclValues.size() == 0)
    return;

  // Check if any of the enum values possibly match.
  bool PossibleValueMatch =
      llvm::any_of(DeclValues, ConstraintBasedEQEvaluator(C, *ValueToCast));

  // If there is no value that can possibly match any of the enum values, then
  // warn.
  if (!PossibleValueMatch)
    reportWarning(C, CE, ED);
}

void ento::registerEnumCastOutOfRangeChecker(CheckerManager &mgr) {
  mgr.registerChecker<EnumCastOutOfRangeChecker>();
}

bool ento::shouldRegisterEnumCastOutOfRangeChecker(const CheckerManager &mgr) {
  return true;
}
