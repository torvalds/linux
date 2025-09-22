//== PointerIterationChecker.cpp ------------------------------- -*- C++ -*--=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines PointerIterationChecker which checks for non-determinism
// caused due to iteration of unordered containers of pointer elements.
//
//===----------------------------------------------------------------------===//

#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"

using namespace clang;
using namespace ento;
using namespace ast_matchers;

namespace {

// ID of a node at which the diagnostic would be emitted.
constexpr llvm::StringLiteral WarnAtNode = "iter";

class PointerIterationChecker : public Checker<check::ASTCodeBody> {
public:
  void checkASTCodeBody(const Decl *D,
                        AnalysisManager &AM,
                        BugReporter &BR) const;
};

static void emitDiagnostics(const BoundNodes &Match, const Decl *D,
                            BugReporter &BR, AnalysisManager &AM,
                            const PointerIterationChecker *Checker) {
  auto *ADC = AM.getAnalysisDeclContext(D);

  const auto *MarkedStmt = Match.getNodeAs<Stmt>(WarnAtNode);
  assert(MarkedStmt);

  auto Range = MarkedStmt->getSourceRange();
  auto Location = PathDiagnosticLocation::createBegin(MarkedStmt,
                                                      BR.getSourceManager(),
                                                      ADC);
  std::string Diagnostics;
  llvm::raw_string_ostream OS(Diagnostics);
  OS << "Iteration of pointer-like elements "
     << "can result in non-deterministic ordering";

  BR.EmitBasicReport(ADC->getDecl(), Checker,
                     "Iteration of pointer-like elements", "Non-determinism",
                     OS.str(), Location, Range);
}

// Assumption: Iteration of ordered containers of pointers is deterministic.

// TODO: Currently, we only check for std::unordered_set. Other unordered
// containers like std::unordered_map also need to be handled.

// TODO: Currently, we do not check what the for loop does with the iterated
// pointer values. Not all iterations may cause non-determinism. For example,
// counting or summing up the elements should not be non-deterministic.

auto matchUnorderedIterWithPointers() -> decltype(decl()) {

  auto UnorderedContainerM = declRefExpr(to(varDecl(hasType(
                               recordDecl(hasName("std::unordered_set")
                             )))));

  auto PointerTypeM = varDecl(hasType(hasCanonicalType(pointerType())));

  auto PointerIterM = stmt(cxxForRangeStmt(
                             hasLoopVariable(PointerTypeM),
                             hasRangeInit(UnorderedContainerM)
                      )).bind(WarnAtNode);

  return decl(forEachDescendant(PointerIterM));
}

void PointerIterationChecker::checkASTCodeBody(const Decl *D,
                                             AnalysisManager &AM,
                                             BugReporter &BR) const {
  auto MatcherM = matchUnorderedIterWithPointers();

  auto Matches = match(MatcherM, *D, AM.getASTContext());
  for (const auto &Match : Matches)
    emitDiagnostics(Match, D, BR, AM, this);
}

} // end of anonymous namespace

void ento::registerPointerIterationChecker(CheckerManager &Mgr) {
  Mgr.registerChecker<PointerIterationChecker>();
}

bool ento::shouldRegisterPointerIterationChecker(const CheckerManager &mgr) {
  const LangOptions &LO = mgr.getLangOpts();
  return LO.CPlusPlus;
}
