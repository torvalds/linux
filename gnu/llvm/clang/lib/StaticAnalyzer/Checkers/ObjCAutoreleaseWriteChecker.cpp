//===- ObjCAutoreleaseWriteChecker.cpp ---------------------------*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines ObjCAutoreleaseWriteChecker which warns against writes
// into autoreleased out parameters which cause crashes.
// An example of a problematic write is a write to @c error in the example
// below:
//
// - (BOOL) mymethod:(NSError *__autoreleasing *)error list:(NSArray*) list {
//     [list enumerateObjectsUsingBlock:^(id obj, NSUInteger idx, BOOL *stop) {
//       NSString *myString = obj;
//       if ([myString isEqualToString:@"error"] && error)
//         *error = [NSError errorWithDomain:@"MyDomain" code:-1];
//     }];
//     return false;
// }
//
// Such code will crash on read from `*error` due to the autorelease pool
// in `enumerateObjectsUsingBlock` implementation freeing the error object
// on exit from the function.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugReporter.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/BugReporter/CommonBugCategories.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/AnalysisManager.h"
#include "llvm/ADT/Twine.h"

using namespace clang;
using namespace ento;
using namespace ast_matchers;

namespace {

const char *ProblematicWriteBind = "problematicwrite";
const char *CapturedBind = "capturedbind";
const char *ParamBind = "parambind";
const char *IsMethodBind = "ismethodbind";
const char *IsARPBind = "isautoreleasepoolbind";

class ObjCAutoreleaseWriteChecker : public Checker<check::ASTCodeBody> {
public:
  void checkASTCodeBody(const Decl *D,
                        AnalysisManager &AM,
                        BugReporter &BR) const;
private:
  std::vector<std::string> SelectorsWithAutoreleasingPool = {
      // Common to NSArray,  NSSet, NSOrderedSet
      "enumerateObjectsUsingBlock:",
      "enumerateObjectsWithOptions:usingBlock:",

      // Common to NSArray and NSOrderedSet
      "enumerateObjectsAtIndexes:options:usingBlock:",
      "indexOfObjectAtIndexes:options:passingTest:",
      "indexesOfObjectsAtIndexes:options:passingTest:",
      "indexOfObjectPassingTest:",
      "indexOfObjectWithOptions:passingTest:",
      "indexesOfObjectsPassingTest:",
      "indexesOfObjectsWithOptions:passingTest:",

      // NSDictionary
      "enumerateKeysAndObjectsUsingBlock:",
      "enumerateKeysAndObjectsWithOptions:usingBlock:",
      "keysOfEntriesPassingTest:",
      "keysOfEntriesWithOptions:passingTest:",

      // NSSet
      "objectsPassingTest:",
      "objectsWithOptions:passingTest:",
      "enumerateIndexPathsWithOptions:usingBlock:",

      // NSIndexSet
      "enumerateIndexesWithOptions:usingBlock:",
      "enumerateIndexesUsingBlock:",
      "enumerateIndexesInRange:options:usingBlock:",
      "enumerateRangesUsingBlock:",
      "enumerateRangesWithOptions:usingBlock:",
      "enumerateRangesInRange:options:usingBlock:",
      "indexPassingTest:",
      "indexesPassingTest:",
      "indexWithOptions:passingTest:",
      "indexesWithOptions:passingTest:",
      "indexInRange:options:passingTest:",
      "indexesInRange:options:passingTest:"
  };

  std::vector<std::string> FunctionsWithAutoreleasingPool = {
      "dispatch_async", "dispatch_group_async", "dispatch_barrier_async"};
};
}

static inline std::vector<llvm::StringRef>
toRefs(const std::vector<std::string> &V) {
  return std::vector<llvm::StringRef>(V.begin(), V.end());
}

static decltype(auto)
callsNames(const std::vector<std::string> &FunctionNames) {
  return callee(functionDecl(hasAnyName(toRefs(FunctionNames))));
}

static void emitDiagnostics(BoundNodes &Match, const Decl *D, BugReporter &BR,
                            AnalysisManager &AM,
                            const ObjCAutoreleaseWriteChecker *Checker) {
  AnalysisDeclContext *ADC = AM.getAnalysisDeclContext(D);

  const auto *PVD = Match.getNodeAs<ParmVarDecl>(ParamBind);
  QualType Ty = PVD->getType();
  if (Ty->getPointeeType().getObjCLifetime() != Qualifiers::OCL_Autoreleasing)
    return;
  const char *ActionMsg = "Write to";
  const auto *MarkedStmt = Match.getNodeAs<Expr>(ProblematicWriteBind);
  bool IsCapture = false;

  // Prefer to warn on write, but if not available, warn on capture.
  if (!MarkedStmt) {
    MarkedStmt = Match.getNodeAs<Expr>(CapturedBind);
    assert(MarkedStmt);
    ActionMsg = "Capture of";
    IsCapture = true;
  }

  SourceRange Range = MarkedStmt->getSourceRange();
  PathDiagnosticLocation Location = PathDiagnosticLocation::createBegin(
      MarkedStmt, BR.getSourceManager(), ADC);

  bool IsMethod = Match.getNodeAs<ObjCMethodDecl>(IsMethodBind) != nullptr;
  const char *FunctionDescription = IsMethod ? "method" : "function";
  bool IsARP = Match.getNodeAs<ObjCAutoreleasePoolStmt>(IsARPBind) != nullptr;

  llvm::SmallString<128> BugNameBuf;
  llvm::raw_svector_ostream BugName(BugNameBuf);
  BugName << ActionMsg
          << " autoreleasing out parameter inside autorelease pool";

  llvm::SmallString<128> BugMessageBuf;
  llvm::raw_svector_ostream BugMessage(BugMessageBuf);
  BugMessage << ActionMsg << " autoreleasing out parameter ";
  if (IsCapture)
    BugMessage << "'" + PVD->getName() + "' ";

  BugMessage << "inside ";
  if (IsARP)
    BugMessage << "locally-scoped autorelease pool;";
  else
    BugMessage << "autorelease pool that may exit before "
               << FunctionDescription << " returns;";

  BugMessage << " consider writing first to a strong local variable"
                " declared outside ";
  if (IsARP)
    BugMessage << "of the autorelease pool";
  else
    BugMessage << "of the block";

  BR.EmitBasicReport(ADC->getDecl(), Checker, BugName.str(),
                     categories::MemoryRefCount, BugMessage.str(), Location,
                     Range);
}

void ObjCAutoreleaseWriteChecker::checkASTCodeBody(const Decl *D,
                                                  AnalysisManager &AM,
                                                  BugReporter &BR) const {

  auto DoublePointerParamM =
      parmVarDecl(hasType(hasCanonicalType(pointerType(
                      pointee(hasCanonicalType(objcObjectPointerType()))))))
          .bind(ParamBind);

  auto ReferencedParamM =
      declRefExpr(to(parmVarDecl(DoublePointerParamM))).bind(CapturedBind);

  // Write into a binded object, e.g. *ParamBind = X.
  auto WritesIntoM = binaryOperator(
    hasLHS(unaryOperator(
        hasOperatorName("*"),
        hasUnaryOperand(
          ignoringParenImpCasts(ReferencedParamM))
    )),
    hasOperatorName("=")
  ).bind(ProblematicWriteBind);

  auto ArgumentCaptureM = hasAnyArgument(
    ignoringParenImpCasts(ReferencedParamM));
  auto CapturedInParamM = stmt(anyOf(
      callExpr(ArgumentCaptureM),
      objcMessageExpr(ArgumentCaptureM)));

  // WritesIntoM happens inside a block passed as an argument.
  auto WritesOrCapturesInBlockM = hasAnyArgument(allOf(
      hasType(hasCanonicalType(blockPointerType())),
      forEachDescendant(
        stmt(anyOf(WritesIntoM, CapturedInParamM))
      )));

  auto BlockPassedToMarkedFuncM = stmt(anyOf(
    callExpr(allOf(
      callsNames(FunctionsWithAutoreleasingPool), WritesOrCapturesInBlockM)),
    objcMessageExpr(allOf(
       hasAnySelector(toRefs(SelectorsWithAutoreleasingPool)),
       WritesOrCapturesInBlockM))
  ));

  // WritesIntoM happens inside an explicit @autoreleasepool.
  auto WritesOrCapturesInPoolM =
      autoreleasePoolStmt(
          forEachDescendant(stmt(anyOf(WritesIntoM, CapturedInParamM))))
          .bind(IsARPBind);

  auto HasParamAndWritesInMarkedFuncM =
      allOf(hasAnyParameter(DoublePointerParamM),
            anyOf(forEachDescendant(BlockPassedToMarkedFuncM),
                  forEachDescendant(WritesOrCapturesInPoolM)));

  auto MatcherM = decl(anyOf(
      objcMethodDecl(HasParamAndWritesInMarkedFuncM).bind(IsMethodBind),
      functionDecl(HasParamAndWritesInMarkedFuncM),
      blockDecl(HasParamAndWritesInMarkedFuncM)));

  auto Matches = match(MatcherM, *D, AM.getASTContext());
  for (BoundNodes Match : Matches)
    emitDiagnostics(Match, D, BR, AM, this);
}

void ento::registerAutoreleaseWriteChecker(CheckerManager &Mgr) {
  Mgr.registerChecker<ObjCAutoreleaseWriteChecker>();
}

bool ento::shouldRegisterAutoreleaseWriteChecker(const CheckerManager &mgr) {
  return true;
}
