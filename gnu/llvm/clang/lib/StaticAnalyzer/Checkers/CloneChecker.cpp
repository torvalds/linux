//===--- CloneChecker.cpp - Clone detection checker -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// CloneChecker is a checker that reports clones in the current translation
/// unit.
///
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/Analysis/CloneDetection.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/AnalysisManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"

using namespace clang;
using namespace ento;

namespace {
class CloneChecker
    : public Checker<check::ASTCodeBody, check::EndOfTranslationUnit> {
public:
  // Checker options.
  int MinComplexity;
  bool ReportNormalClones = false;
  StringRef IgnoredFilesPattern;

private:
  mutable CloneDetector Detector;
  const BugType BT_Exact{this, "Exact code clone", "Code clone"};
  const BugType BT_Suspicious{this, "Suspicious code clone", "Code clone"};

public:
  void checkASTCodeBody(const Decl *D, AnalysisManager &Mgr,
                        BugReporter &BR) const;

  void checkEndOfTranslationUnit(const TranslationUnitDecl *TU,
                                 AnalysisManager &Mgr, BugReporter &BR) const;

  /// Reports all clones to the user.
  void reportClones(BugReporter &BR, AnalysisManager &Mgr,
                    std::vector<CloneDetector::CloneGroup> &CloneGroups) const;

  /// Reports only suspicious clones to the user along with information
  /// that explain why they are suspicious.
  void reportSuspiciousClones(
      BugReporter &BR, AnalysisManager &Mgr,
      std::vector<CloneDetector::CloneGroup> &CloneGroups) const;
};
} // end anonymous namespace

void CloneChecker::checkASTCodeBody(const Decl *D, AnalysisManager &Mgr,
                                    BugReporter &BR) const {
  // Every statement that should be included in the search for clones needs to
  // be passed to the CloneDetector.
  Detector.analyzeCodeBody(D);
}

void CloneChecker::checkEndOfTranslationUnit(const TranslationUnitDecl *TU,
                                             AnalysisManager &Mgr,
                                             BugReporter &BR) const {
  // At this point, every statement in the translation unit has been analyzed by
  // the CloneDetector. The only thing left to do is to report the found clones.

  // Let the CloneDetector create a list of clones from all the analyzed
  // statements. We don't filter for matching variable patterns at this point
  // because reportSuspiciousClones() wants to search them for errors.
  std::vector<CloneDetector::CloneGroup> AllCloneGroups;

  Detector.findClones(
      AllCloneGroups, FilenamePatternConstraint(IgnoredFilesPattern),
      RecursiveCloneTypeIIHashConstraint(), MinGroupSizeConstraint(2),
      MinComplexityConstraint(MinComplexity),
      RecursiveCloneTypeIIVerifyConstraint(), OnlyLargestCloneConstraint());

  reportSuspiciousClones(BR, Mgr, AllCloneGroups);

  // We are done for this translation unit unless we also need to report normal
  // clones.
  if (!ReportNormalClones)
    return;

  // Now that the suspicious clone detector has checked for pattern errors,
  // we also filter all clones who don't have matching patterns
  CloneDetector::constrainClones(AllCloneGroups,
                                 MatchingVariablePatternConstraint(),
                                 MinGroupSizeConstraint(2));

  reportClones(BR, Mgr, AllCloneGroups);
}

static PathDiagnosticLocation makeLocation(const StmtSequence &S,
                                           AnalysisManager &Mgr) {
  ASTContext &ACtx = Mgr.getASTContext();
  return PathDiagnosticLocation::createBegin(
      S.front(), ACtx.getSourceManager(),
      Mgr.getAnalysisDeclContext(ACtx.getTranslationUnitDecl()));
}

void CloneChecker::reportClones(
    BugReporter &BR, AnalysisManager &Mgr,
    std::vector<CloneDetector::CloneGroup> &CloneGroups) const {
  for (const CloneDetector::CloneGroup &Group : CloneGroups) {
    // We group the clones by printing the first as a warning and all others
    // as a note.
    auto R = std::make_unique<BasicBugReport>(
        BT_Exact, "Duplicate code detected", makeLocation(Group.front(), Mgr));
    R->addRange(Group.front().getSourceRange());

    for (unsigned i = 1; i < Group.size(); ++i)
      R->addNote("Similar code here", makeLocation(Group[i], Mgr),
                 Group[i].getSourceRange());
    BR.emitReport(std::move(R));
  }
}

void CloneChecker::reportSuspiciousClones(
    BugReporter &BR, AnalysisManager &Mgr,
    std::vector<CloneDetector::CloneGroup> &CloneGroups) const {
  std::vector<VariablePattern::SuspiciousClonePair> Pairs;

  for (const CloneDetector::CloneGroup &Group : CloneGroups) {
    for (unsigned i = 0; i < Group.size(); ++i) {
      VariablePattern PatternA(Group[i]);

      for (unsigned j = i + 1; j < Group.size(); ++j) {
        VariablePattern PatternB(Group[j]);

        VariablePattern::SuspiciousClonePair ClonePair;
        // For now, we only report clones which break the variable pattern just
        // once because multiple differences in a pattern are an indicator that
        // those differences are maybe intended (e.g. because it's actually a
        // different algorithm).
        // FIXME: In very big clones even multiple variables can be unintended,
        // so replacing this number with a percentage could better handle such
        // cases. On the other hand it could increase the false-positive rate
        // for all clones if the percentage is too high.
        if (PatternA.countPatternDifferences(PatternB, &ClonePair) == 1) {
          Pairs.push_back(ClonePair);
          break;
        }
      }
    }
  }

  ASTContext &ACtx = BR.getContext();
  SourceManager &SM = ACtx.getSourceManager();
  AnalysisDeclContext *ADC =
      Mgr.getAnalysisDeclContext(ACtx.getTranslationUnitDecl());

  for (VariablePattern::SuspiciousClonePair &Pair : Pairs) {
    // FIXME: We are ignoring the suggestions currently, because they are
    // only 50% accurate (even if the second suggestion is unavailable),
    // which may confuse the user.
    // Think how to perform more accurate suggestions?

    auto R = std::make_unique<BasicBugReport>(
        BT_Suspicious,
        "Potential copy-paste error; did you really mean to use '" +
            Pair.FirstCloneInfo.Variable->getNameAsString() + "' here?",
        PathDiagnosticLocation::createBegin(Pair.FirstCloneInfo.Mention, SM,
                                            ADC));
    R->addRange(Pair.FirstCloneInfo.Mention->getSourceRange());

    R->addNote("Similar code using '" +
                   Pair.SecondCloneInfo.Variable->getNameAsString() + "' here",
               PathDiagnosticLocation::createBegin(Pair.SecondCloneInfo.Mention,
                                                   SM, ADC),
               Pair.SecondCloneInfo.Mention->getSourceRange());

    BR.emitReport(std::move(R));
  }
}

//===----------------------------------------------------------------------===//
// Register CloneChecker
//===----------------------------------------------------------------------===//

void ento::registerCloneChecker(CheckerManager &Mgr) {
  auto *Checker = Mgr.registerChecker<CloneChecker>();

  Checker->MinComplexity = Mgr.getAnalyzerOptions().getCheckerIntegerOption(
      Checker, "MinimumCloneComplexity");

  if (Checker->MinComplexity < 0)
    Mgr.reportInvalidCheckerOptionValue(
        Checker, "MinimumCloneComplexity", "a non-negative value");

  Checker->ReportNormalClones = Mgr.getAnalyzerOptions().getCheckerBooleanOption(
      Checker, "ReportNormalClones");

  Checker->IgnoredFilesPattern = Mgr.getAnalyzerOptions()
    .getCheckerStringOption(Checker, "IgnoredFilesPattern");
}

bool ento::shouldRegisterCloneChecker(const CheckerManager &mgr) {
  return true;
}
