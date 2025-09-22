//== PointerSortingChecker.cpp --------------------------------- -*- C++ -*--=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines PointerSortingChecker which checks for non-determinism
// caused due to sorting containers with pointer-like elements.
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
constexpr llvm::StringLiteral WarnAtNode = "sort";

class PointerSortingChecker : public Checker<check::ASTCodeBody> {
public:
  void checkASTCodeBody(const Decl *D,
                        AnalysisManager &AM,
                        BugReporter &BR) const;
};

static void emitDiagnostics(const BoundNodes &Match, const Decl *D,
                            BugReporter &BR, AnalysisManager &AM,
                            const PointerSortingChecker *Checker) {
  auto *ADC = AM.getAnalysisDeclContext(D);

  const auto *MarkedStmt = Match.getNodeAs<CallExpr>(WarnAtNode);
  assert(MarkedStmt);

  auto Range = MarkedStmt->getSourceRange();
  auto Location = PathDiagnosticLocation::createBegin(MarkedStmt,
                                                      BR.getSourceManager(),
                                                      ADC);
  std::string Diagnostics;
  llvm::raw_string_ostream OS(Diagnostics);
  OS << "Sorting pointer-like elements "
     << "can result in non-deterministic ordering";

  BR.EmitBasicReport(ADC->getDecl(), Checker,
                     "Sorting of pointer-like elements", "Non-determinism",
                     OS.str(), Location, Range);
}

decltype(auto) callsName(const char *FunctionName) {
  return callee(functionDecl(hasName(FunctionName)));
}

// FIXME: Currently we simply check if std::sort is used with pointer-like
// elements. This approach can have a big false positive rate. Using std::sort,
// std::unique and then erase is common technique for deduplicating a container
// (which in some cases might even be quicker than using, let's say std::set).
// In case a container contains arbitrary memory addresses (e.g. multiple
// things give different stuff but might give the same thing multiple times)
// which we don't want to do things with more than once, we might use
// sort-unique-erase and the sort call will emit a report.
auto matchSortWithPointers() -> decltype(decl()) {
  // Match any of these function calls.
  auto SortFuncM = anyOf(
                     callsName("std::is_sorted"),
                     callsName("std::nth_element"),
                     callsName("std::partial_sort"),
                     callsName("std::partition"),
                     callsName("std::sort"),
                     callsName("std::stable_partition"),
                     callsName("std::stable_sort")
                    );

  // Match only if the container has pointer-type elements.
  auto IteratesPointerEltsM = hasArgument(0,
                                hasType(cxxRecordDecl(has(
                                  fieldDecl(hasType(hasCanonicalType(
                                    pointsTo(hasCanonicalType(pointerType()))
                                  )))
                              ))));

  auto PointerSortM = traverse(
      TK_AsIs,
      stmt(callExpr(allOf(SortFuncM, IteratesPointerEltsM))).bind(WarnAtNode));

  return decl(forEachDescendant(PointerSortM));
}

void PointerSortingChecker::checkASTCodeBody(const Decl *D,
                                             AnalysisManager &AM,
                                             BugReporter &BR) const {
  auto MatcherM = matchSortWithPointers();

  auto Matches = match(MatcherM, *D, AM.getASTContext());
  for (const auto &Match : Matches)
    emitDiagnostics(Match, D, BR, AM, this);
}

} // end of anonymous namespace

void ento::registerPointerSortingChecker(CheckerManager &Mgr) {
  Mgr.registerChecker<PointerSortingChecker>();
}

bool ento::shouldRegisterPointerSortingChecker(const CheckerManager &mgr) {
  const LangOptions &LO = mgr.getLangOpts();
  return LO.CPlusPlus;
}
