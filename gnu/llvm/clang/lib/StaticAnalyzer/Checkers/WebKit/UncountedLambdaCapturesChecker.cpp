//=======- UncountedLambdaCapturesChecker.cpp --------------------*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "DiagOutputUtils.h"
#include "PtrTypesSemantics.h"
#include "clang/AST/CXXInheritance.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugReporter.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include <optional>

using namespace clang;
using namespace ento;

namespace {
class UncountedLambdaCapturesChecker
    : public Checker<check::ASTDecl<TranslationUnitDecl>> {
private:
  BugType Bug{this, "Lambda capture of uncounted variable",
              "WebKit coding guidelines"};
  mutable BugReporter *BR = nullptr;

public:
  void checkASTDecl(const TranslationUnitDecl *TUD, AnalysisManager &MGR,
                    BugReporter &BRArg) const {
    BR = &BRArg;

    // The calls to checkAST* from AnalysisConsumer don't
    // visit template instantiations or lambda classes. We
    // want to visit those, so we make our own RecursiveASTVisitor.
    struct LocalVisitor : public RecursiveASTVisitor<LocalVisitor> {
      const UncountedLambdaCapturesChecker *Checker;
      explicit LocalVisitor(const UncountedLambdaCapturesChecker *Checker)
          : Checker(Checker) {
        assert(Checker);
      }

      bool shouldVisitTemplateInstantiations() const { return true; }
      bool shouldVisitImplicitCode() const { return false; }

      bool VisitLambdaExpr(LambdaExpr *L) {
        Checker->visitLambdaExpr(L);
        return true;
      }
    };

    LocalVisitor visitor(this);
    visitor.TraverseDecl(const_cast<TranslationUnitDecl *>(TUD));
  }

  void visitLambdaExpr(LambdaExpr *L) const {
    for (const LambdaCapture &C : L->captures()) {
      if (C.capturesVariable()) {
        ValueDecl *CapturedVar = C.getCapturedVar();
        if (auto *CapturedVarType = CapturedVar->getType().getTypePtrOrNull()) {
            std::optional<bool> IsUncountedPtr = isUncountedPtr(CapturedVarType);
            if (IsUncountedPtr && *IsUncountedPtr) {
                reportBug(C, CapturedVar, CapturedVarType);
            }
        }
      }
    }
  }

  void reportBug(const LambdaCapture &Capture, ValueDecl *CapturedVar,
                 const Type *T) const {
    assert(CapturedVar);

    SmallString<100> Buf;
    llvm::raw_svector_ostream Os(Buf);

    if (Capture.isExplicit()) {
      Os << "Captured ";
    } else {
      Os << "Implicitly captured ";
    }
    if (T->isPointerType()) {
      Os << "raw-pointer ";
    } else {
      assert(T->isReferenceType());
      Os << "reference ";
    }

    printQuotedQualifiedName(Os, Capture.getCapturedVar());
    Os << " to uncounted type is unsafe.";

    PathDiagnosticLocation BSLoc(Capture.getLocation(), BR->getSourceManager());
    auto Report = std::make_unique<BasicBugReport>(Bug, Os.str(), BSLoc);
    BR->emitReport(std::move(Report));
  }
};
} // namespace

void ento::registerUncountedLambdaCapturesChecker(CheckerManager &Mgr) {
  Mgr.registerChecker<UncountedLambdaCapturesChecker>();
}

bool ento::shouldRegisterUncountedLambdaCapturesChecker(
    const CheckerManager &mgr) {
  return true;
}
