//===- NumberObjectConversionChecker.cpp -------------------------*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines NumberObjectConversionChecker, which checks for a
// particular common mistake when dealing with numbers represented as objects
// passed around by pointers. Namely, the language allows to reinterpret the
// pointer as a number directly, often without throwing any warnings,
// but in most cases the result of such conversion is clearly unexpected,
// as pointer value, rather than number value represented by the pointee object,
// becomes the result of such operation.
//
// Currently the checker supports the Objective-C NSNumber class,
// and the OSBoolean class found in macOS low-level code; the latter
// can only hold boolean values.
//
// This checker has an option "Pedantic" (boolean), which enables detection of
// more conversion patterns (which are most likely more harmless, and therefore
// are more likely to produce false positives) - disabled by default,
// enabled with `-analyzer-config osx.NumberObjectConversion:Pedantic=true'.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugReporter.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/AnalysisManager.h"
#include "clang/Lex/Lexer.h"
#include "llvm/ADT/APSInt.h"

using namespace clang;
using namespace ento;
using namespace ast_matchers;

namespace {

class NumberObjectConversionChecker : public Checker<check::ASTCodeBody> {
public:
  bool Pedantic;

  void checkASTCodeBody(const Decl *D, AnalysisManager &AM,
                        BugReporter &BR) const;
};

class Callback : public MatchFinder::MatchCallback {
  const NumberObjectConversionChecker *C;
  BugReporter &BR;
  AnalysisDeclContext *ADC;

public:
  Callback(const NumberObjectConversionChecker *C,
           BugReporter &BR, AnalysisDeclContext *ADC)
      : C(C), BR(BR), ADC(ADC) {}
  void run(const MatchFinder::MatchResult &Result) override;
};
} // end of anonymous namespace

void Callback::run(const MatchFinder::MatchResult &Result) {
  bool IsPedanticMatch =
      (Result.Nodes.getNodeAs<Stmt>("pedantic") != nullptr);
  if (IsPedanticMatch && !C->Pedantic)
    return;

  ASTContext &ACtx = ADC->getASTContext();

  if (const Expr *CheckIfNull =
          Result.Nodes.getNodeAs<Expr>("check_if_null")) {
    // Unless the macro indicates that the intended type is clearly not
    // a pointer type, we should avoid warning on comparing pointers
    // to zero literals in non-pedantic mode.
    // FIXME: Introduce an AST matcher to implement the macro-related logic?
    bool MacroIndicatesWeShouldSkipTheCheck = false;
    SourceLocation Loc = CheckIfNull->getBeginLoc();
    if (Loc.isMacroID()) {
      StringRef MacroName = Lexer::getImmediateMacroName(
          Loc, ACtx.getSourceManager(), ACtx.getLangOpts());
      if (MacroName == "NULL" || MacroName == "nil")
        return;
      if (MacroName == "YES" || MacroName == "NO")
        MacroIndicatesWeShouldSkipTheCheck = true;
    }
    if (!MacroIndicatesWeShouldSkipTheCheck) {
      Expr::EvalResult EVResult;
      if (CheckIfNull->IgnoreParenCasts()->EvaluateAsInt(
              EVResult, ACtx, Expr::SE_AllowSideEffects)) {
        llvm::APSInt Result = EVResult.Val.getInt();
        if (Result == 0) {
          if (!C->Pedantic)
            return;
          IsPedanticMatch = true;
        }
      }
    }
  }

  const Stmt *Conv = Result.Nodes.getNodeAs<Stmt>("conv");
  assert(Conv);

  const Expr *ConvertedCObject = Result.Nodes.getNodeAs<Expr>("c_object");
  const Expr *ConvertedCppObject = Result.Nodes.getNodeAs<Expr>("cpp_object");
  const Expr *ConvertedObjCObject = Result.Nodes.getNodeAs<Expr>("objc_object");
  bool IsCpp = (ConvertedCppObject != nullptr);
  bool IsObjC = (ConvertedObjCObject != nullptr);
  const Expr *Obj = IsObjC ? ConvertedObjCObject
                  : IsCpp ? ConvertedCppObject
                  : ConvertedCObject;
  assert(Obj);

  bool IsComparison =
      (Result.Nodes.getNodeAs<Stmt>("comparison") != nullptr);

  bool IsOSNumber =
      (Result.Nodes.getNodeAs<Decl>("osnumber") != nullptr);

  bool IsInteger =
      (Result.Nodes.getNodeAs<QualType>("int_type") != nullptr);
  bool IsObjCBool =
      (Result.Nodes.getNodeAs<QualType>("objc_bool_type") != nullptr);
  bool IsCppBool =
      (Result.Nodes.getNodeAs<QualType>("cpp_bool_type") != nullptr);

  llvm::SmallString<64> Msg;
  llvm::raw_svector_ostream OS(Msg);

  // Remove ObjC ARC qualifiers.
  QualType ObjT = Obj->getType().getUnqualifiedType();

  // Remove consts from pointers.
  if (IsCpp) {
    assert(ObjT.getCanonicalType()->isPointerType());
    ObjT = ACtx.getPointerType(
        ObjT->getPointeeType().getCanonicalType().getUnqualifiedType());
  }

  if (IsComparison)
    OS << "Comparing ";
  else
    OS << "Converting ";

  OS << "a pointer value of type '" << ObjT << "' to a ";

  std::string EuphemismForPlain = "primitive";
  std::string SuggestedApi = IsObjC ? (IsInteger ? "" : "-boolValue")
                           : IsCpp ? (IsOSNumber ? "" : "getValue()")
                           : "CFNumberGetValue()";
  if (SuggestedApi.empty()) {
    // A generic message if we're not sure what API should be called.
    // FIXME: Pattern-match the integer type to make a better guess?
    SuggestedApi =
        "a method on '" + ObjT.getAsString() + "' to get the scalar value";
    // "scalar" is not quite correct or common, but some documentation uses it
    // when describing object methods we suggest. For consistency, we use
    // "scalar" in the whole sentence when we need to use this word in at least
    // one place, otherwise we use "primitive".
    EuphemismForPlain = "scalar";
  }

  if (IsInteger)
    OS << EuphemismForPlain << " integer value";
  else if (IsObjCBool)
    OS << EuphemismForPlain << " BOOL value";
  else if (IsCppBool)
    OS << EuphemismForPlain << " bool value";
  else // Branch condition?
    OS << EuphemismForPlain << " boolean value";


  if (IsPedanticMatch)
    OS << "; instead, either compare the pointer to "
       << (IsObjC ? "nil" : IsCpp ? "nullptr" : "NULL") << " or ";
  else
    OS << "; did you mean to ";

  if (IsComparison)
    OS << "compare the result of calling " << SuggestedApi;
  else
    OS << "call " << SuggestedApi;

  if (!IsPedanticMatch)
    OS << "?";

  BR.EmitBasicReport(
      ADC->getDecl(), C, "Suspicious number object conversion", "Logic error",
      OS.str(),
      PathDiagnosticLocation::createBegin(Obj, BR.getSourceManager(), ADC),
      Conv->getSourceRange());
}

void NumberObjectConversionChecker::checkASTCodeBody(const Decl *D,
                                                     AnalysisManager &AM,
                                                     BugReporter &BR) const {
  // Currently this matches CoreFoundation opaque pointer typedefs.
  auto CSuspiciousNumberObjectExprM = expr(ignoringParenImpCasts(
      expr(hasType(elaboratedType(namesType(typedefType(
               hasDeclaration(anyOf(typedefDecl(hasName("CFNumberRef")),
                                    typedefDecl(hasName("CFBooleanRef")))))))))
          .bind("c_object")));

  // Currently this matches XNU kernel number-object pointers.
  auto CppSuspiciousNumberObjectExprM =
      expr(ignoringParenImpCasts(
          expr(hasType(hasCanonicalType(
              pointerType(pointee(hasCanonicalType(
                  recordType(hasDeclaration(
                      anyOf(
                        cxxRecordDecl(hasName("OSBoolean")),
                        cxxRecordDecl(hasName("OSNumber"))
                            .bind("osnumber"))))))))))
          .bind("cpp_object")));

  // Currently this matches NeXTSTEP number objects.
  auto ObjCSuspiciousNumberObjectExprM =
      expr(ignoringParenImpCasts(
          expr(hasType(hasCanonicalType(
              objcObjectPointerType(pointee(
                  qualType(hasCanonicalType(
                      qualType(hasDeclaration(
                          objcInterfaceDecl(hasName("NSNumber")))))))))))
          .bind("objc_object")));

  auto SuspiciousNumberObjectExprM = anyOf(
      CSuspiciousNumberObjectExprM,
      CppSuspiciousNumberObjectExprM,
      ObjCSuspiciousNumberObjectExprM);

  // Useful for predicates like "Unless we've seen the same object elsewhere".
  auto AnotherSuspiciousNumberObjectExprM =
      expr(anyOf(
          equalsBoundNode("c_object"),
          equalsBoundNode("objc_object"),
          equalsBoundNode("cpp_object")));

  // The .bind here is in order to compose the error message more accurately.
  auto ObjCSuspiciousScalarBooleanTypeM =
      qualType(elaboratedType(namesType(
                   typedefType(hasDeclaration(typedefDecl(hasName("BOOL")))))))
          .bind("objc_bool_type");

  // The .bind here is in order to compose the error message more accurately.
  auto SuspiciousScalarBooleanTypeM =
      qualType(anyOf(qualType(booleanType()).bind("cpp_bool_type"),
                     ObjCSuspiciousScalarBooleanTypeM));

  // The .bind here is in order to compose the error message more accurately.
  // Also avoid intptr_t and uintptr_t because they were specifically created
  // for storing pointers.
  auto SuspiciousScalarNumberTypeM =
      qualType(hasCanonicalType(isInteger()),
               unless(elaboratedType(namesType(typedefType(hasDeclaration(
                   typedefDecl(matchesName("^::u?intptr_t$"))))))))
          .bind("int_type");

  auto SuspiciousScalarTypeM =
      qualType(anyOf(SuspiciousScalarBooleanTypeM,
                     SuspiciousScalarNumberTypeM));

  auto SuspiciousScalarExprM =
      expr(ignoringParenImpCasts(expr(hasType(SuspiciousScalarTypeM))));

  auto ConversionThroughAssignmentM =
      binaryOperator(allOf(hasOperatorName("="),
                           hasLHS(SuspiciousScalarExprM),
                           hasRHS(SuspiciousNumberObjectExprM)));

  auto ConversionThroughBranchingM =
      ifStmt(allOf(
          hasCondition(SuspiciousNumberObjectExprM),
          unless(hasConditionVariableStatement(declStmt())
      ))).bind("pedantic");

  auto ConversionThroughCallM =
      callExpr(hasAnyArgument(allOf(hasType(SuspiciousScalarTypeM),
                                    ignoringParenImpCasts(
                                        SuspiciousNumberObjectExprM))));

  // We bind "check_if_null" to modify the warning message
  // in case it was intended to compare a pointer to 0 with a relatively-ok
  // construct "x == 0" or "x != 0".
  auto ConversionThroughEquivalenceM =
      binaryOperator(allOf(anyOf(hasOperatorName("=="), hasOperatorName("!=")),
                           hasEitherOperand(SuspiciousNumberObjectExprM),
                           hasEitherOperand(SuspiciousScalarExprM
                                            .bind("check_if_null"))))
      .bind("comparison");

  auto ConversionThroughComparisonM =
      binaryOperator(allOf(anyOf(hasOperatorName(">="), hasOperatorName(">"),
                                 hasOperatorName("<="), hasOperatorName("<")),
                           hasEitherOperand(SuspiciousNumberObjectExprM),
                           hasEitherOperand(SuspiciousScalarExprM)))
      .bind("comparison");

  auto ConversionThroughConditionalOperatorM =
      conditionalOperator(allOf(
          hasCondition(SuspiciousNumberObjectExprM),
          unless(hasTrueExpression(
              hasDescendant(AnotherSuspiciousNumberObjectExprM))),
          unless(hasFalseExpression(
              hasDescendant(AnotherSuspiciousNumberObjectExprM)))))
      .bind("pedantic");

  auto ConversionThroughExclamationMarkM =
      unaryOperator(allOf(hasOperatorName("!"),
                          has(expr(SuspiciousNumberObjectExprM))))
      .bind("pedantic");

  auto ConversionThroughExplicitBooleanCastM =
      explicitCastExpr(allOf(hasType(SuspiciousScalarBooleanTypeM),
                             has(expr(SuspiciousNumberObjectExprM))));

  auto ConversionThroughExplicitNumberCastM =
      explicitCastExpr(allOf(hasType(SuspiciousScalarNumberTypeM),
                             has(expr(SuspiciousNumberObjectExprM))));

  auto ConversionThroughInitializerM =
      declStmt(hasSingleDecl(
          varDecl(hasType(SuspiciousScalarTypeM),
                  hasInitializer(SuspiciousNumberObjectExprM))));

  auto FinalM = stmt(anyOf(ConversionThroughAssignmentM,
                           ConversionThroughBranchingM,
                           ConversionThroughCallM,
                           ConversionThroughComparisonM,
                           ConversionThroughConditionalOperatorM,
                           ConversionThroughEquivalenceM,
                           ConversionThroughExclamationMarkM,
                           ConversionThroughExplicitBooleanCastM,
                           ConversionThroughExplicitNumberCastM,
                           ConversionThroughInitializerM)).bind("conv");

  MatchFinder F;
  Callback CB(this, BR, AM.getAnalysisDeclContext(D));

  F.addMatcher(traverse(TK_AsIs, stmt(forEachDescendant(FinalM))), &CB);
  F.match(*D->getBody(), AM.getASTContext());
}

void ento::registerNumberObjectConversionChecker(CheckerManager &Mgr) {
  NumberObjectConversionChecker *Chk =
      Mgr.registerChecker<NumberObjectConversionChecker>();
  Chk->Pedantic =
      Mgr.getAnalyzerOptions().getCheckerBooleanOption(Chk, "Pedantic");
}

bool ento::shouldRegisterNumberObjectConversionChecker(const CheckerManager &mgr) {
  return true;
}
