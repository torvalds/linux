//=======- UncountedCallArgsChecker.cpp --------------------------*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ASTUtils.h"
#include "DiagOutputUtils.h"
#include "PtrTypesSemantics.h"
#include "clang/AST/CXXInheritance.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugReporter.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include <optional>

using namespace clang;
using namespace ento;

namespace {

class UncountedCallArgsChecker
    : public Checker<check::ASTDecl<TranslationUnitDecl>> {
  BugType Bug{this,
            "Uncounted call argument for a raw pointer/reference parameter",
            "WebKit coding guidelines"};
  mutable BugReporter *BR;

  TrivialFunctionAnalysis TFA;

public:

  void checkASTDecl(const TranslationUnitDecl *TUD, AnalysisManager &MGR,
                    BugReporter &BRArg) const {
    BR = &BRArg;

    // The calls to checkAST* from AnalysisConsumer don't
    // visit template instantiations or lambda classes. We
    // want to visit those, so we make our own RecursiveASTVisitor.
    struct LocalVisitor : public RecursiveASTVisitor<LocalVisitor> {
      const UncountedCallArgsChecker *Checker;
      explicit LocalVisitor(const UncountedCallArgsChecker *Checker)
          : Checker(Checker) {
        assert(Checker);
      }

      bool shouldVisitTemplateInstantiations() const { return true; }
      bool shouldVisitImplicitCode() const { return false; }

      bool TraverseClassTemplateDecl(ClassTemplateDecl *Decl) {
        if (isRefType(safeGetName(Decl)))
          return true;
        return RecursiveASTVisitor<LocalVisitor>::TraverseClassTemplateDecl(
            Decl);
      }

      bool VisitCallExpr(const CallExpr *CE) {
        Checker->visitCallExpr(CE);
        return true;
      }
    };

    LocalVisitor visitor(this);
    visitor.TraverseDecl(const_cast<TranslationUnitDecl *>(TUD));
  }

  void visitCallExpr(const CallExpr *CE) const {
    if (shouldSkipCall(CE))
      return;

    if (auto *F = CE->getDirectCallee()) {
      // Skip the first argument for overloaded member operators (e. g. lambda
      // or std::function call operator).
      unsigned ArgIdx = isa<CXXOperatorCallExpr>(CE) && isa_and_nonnull<CXXMethodDecl>(F);

      if (auto *MemberCallExpr = dyn_cast<CXXMemberCallExpr>(CE)) {
        if (auto *MD = MemberCallExpr->getMethodDecl()) {
          auto name = safeGetName(MD);
          if (name == "ref" || name == "deref")
            return;
        }
        auto *E = MemberCallExpr->getImplicitObjectArgument();
        QualType ArgType = MemberCallExpr->getObjectType();
        std::optional<bool> IsUncounted =
            isUncounted(ArgType->getAsCXXRecordDecl());
        if (IsUncounted && *IsUncounted && !isPtrOriginSafe(E))
          reportBugOnThis(E);
      }

      for (auto P = F->param_begin();
           // FIXME: Also check variadic function parameters.
           // FIXME: Also check default function arguments. Probably a different
           // checker. In case there are default arguments the call can have
           // fewer arguments than the callee has parameters.
           P < F->param_end() && ArgIdx < CE->getNumArgs(); ++P, ++ArgIdx) {
        // TODO: attributes.
        // if ((*P)->hasAttr<SafeRefCntblRawPtrAttr>())
        //  continue;

        const auto *ArgType = (*P)->getType().getTypePtrOrNull();
        if (!ArgType)
          continue; // FIXME? Should we bail?

        // FIXME: more complex types (arrays, references to raw pointers, etc)
        std::optional<bool> IsUncounted = isUncountedPtr(ArgType);
        if (!IsUncounted || !(*IsUncounted))
          continue;

        const auto *Arg = CE->getArg(ArgIdx);

        if (auto *defaultArg = dyn_cast<CXXDefaultArgExpr>(Arg))
          Arg = defaultArg->getExpr();

        if (isPtrOriginSafe(Arg))
          continue;

        reportBug(Arg, *P);
      }
    }
  }

  bool isPtrOriginSafe(const Expr *Arg) const {
    return tryToFindPtrOrigin(Arg, /*StopAtFirstRefCountedObj=*/true,
                              [](const clang::Expr *ArgOrigin, bool IsSafe) {
                                if (IsSafe)
                                  return true;
                                if (isa<CXXNullPtrLiteralExpr>(ArgOrigin)) {
                                  // foo(nullptr)
                                  return true;
                                }
                                if (isa<IntegerLiteral>(ArgOrigin)) {
                                  // FIXME: Check the value.
                                  // foo(NULL)
                                  return true;
                                }
                                if (isASafeCallArg(ArgOrigin))
                                  return true;
                                return false;
                              });
  }

  bool shouldSkipCall(const CallExpr *CE) const {
    const auto *Callee = CE->getDirectCallee();

    if (BR->getSourceManager().isInSystemHeader(CE->getExprLoc()))
      return true;

    if (Callee && TFA.isTrivial(Callee))
      return true;

    if (CE->getNumArgs() == 0)
      return false;

    // If an assignment is problematic we should warn about the sole existence
    // of object on LHS.
    if (auto *MemberOp = dyn_cast<CXXOperatorCallExpr>(CE)) {
      // Note: assignemnt to built-in type isn't derived from CallExpr.
      if (MemberOp->getOperator() ==
          OO_Equal) { // Ignore assignment to Ref/RefPtr.
        auto *callee = MemberOp->getDirectCallee();
        if (auto *calleeDecl = dyn_cast<CXXMethodDecl>(callee)) {
          if (const CXXRecordDecl *classDecl = calleeDecl->getParent()) {
            if (isRefCounted(classDecl))
              return true;
          }
        }
      }
      if (MemberOp->isAssignmentOp())
        return false;
    }

    if (!Callee)
      return false;

    if (isMethodOnWTFContainerType(Callee))
      return true;

    auto overloadedOperatorType = Callee->getOverloadedOperator();
    if (overloadedOperatorType == OO_EqualEqual ||
        overloadedOperatorType == OO_ExclaimEqual ||
        overloadedOperatorType == OO_LessEqual ||
        overloadedOperatorType == OO_GreaterEqual ||
        overloadedOperatorType == OO_Spaceship ||
        overloadedOperatorType == OO_AmpAmp ||
        overloadedOperatorType == OO_PipePipe)
      return true;

    if (isCtorOfRefCounted(Callee))
      return true;

    auto name = safeGetName(Callee);
    if (name == "adoptRef" || name == "getPtr" || name == "WeakPtr" ||
        name == "dynamicDowncast" || name == "downcast" ||
        name == "checkedDowncast" || name == "uncheckedDowncast" ||
        name == "bitwise_cast" || name == "is" || name == "equal" ||
        name == "hash" || name == "isType" ||
        // FIXME: Most/all of these should be implemented via attributes.
        name == "equalIgnoringASCIICase" ||
        name == "equalIgnoringASCIICaseCommon" ||
        name == "equalIgnoringNullity" || name == "toString")
      return true;

    return false;
  }

  bool isMethodOnWTFContainerType(const FunctionDecl *Decl) const {
    if (!isa<CXXMethodDecl>(Decl))
      return false;
    auto *ClassDecl = Decl->getParent();
    if (!ClassDecl || !isa<CXXRecordDecl>(ClassDecl))
      return false;

    auto *NsDecl = ClassDecl->getParent();
    if (!NsDecl || !isa<NamespaceDecl>(NsDecl))
      return false;

    auto MethodName = safeGetName(Decl);
    auto ClsNameStr = safeGetName(ClassDecl);
    StringRef ClsName = ClsNameStr; // FIXME: Make safeGetName return StringRef.
    auto NamespaceName = safeGetName(NsDecl);
    // FIXME: These should be implemented via attributes.
    return NamespaceName == "WTF" &&
           (MethodName == "find" || MethodName == "findIf" ||
            MethodName == "reverseFind" || MethodName == "reverseFindIf" ||
            MethodName == "findIgnoringASCIICase" || MethodName == "get" ||
            MethodName == "inlineGet" || MethodName == "contains" ||
            MethodName == "containsIf" ||
            MethodName == "containsIgnoringASCIICase" ||
            MethodName == "startsWith" || MethodName == "endsWith" ||
            MethodName == "startsWithIgnoringASCIICase" ||
            MethodName == "endsWithIgnoringASCIICase" ||
            MethodName == "substring") &&
           (ClsName.ends_with("Vector") || ClsName.ends_with("Set") ||
            ClsName.ends_with("Map") || ClsName == "StringImpl" ||
            ClsName.ends_with("String"));
  }

  void reportBug(const Expr *CallArg, const ParmVarDecl *Param) const {
    assert(CallArg);

    SmallString<100> Buf;
    llvm::raw_svector_ostream Os(Buf);

    const std::string paramName = safeGetName(Param);
    Os << "Call argument";
    if (!paramName.empty()) {
      Os << " for parameter ";
      printQuotedQualifiedName(Os, Param);
    }
    Os << " is uncounted and unsafe.";

    const SourceLocation SrcLocToReport =
        isa<CXXDefaultArgExpr>(CallArg) ? Param->getDefaultArg()->getExprLoc()
                                        : CallArg->getSourceRange().getBegin();

    PathDiagnosticLocation BSLoc(SrcLocToReport, BR->getSourceManager());
    auto Report = std::make_unique<BasicBugReport>(Bug, Os.str(), BSLoc);
    Report->addRange(CallArg->getSourceRange());
    BR->emitReport(std::move(Report));
  }

  void reportBugOnThis(const Expr *CallArg) const {
    assert(CallArg);

    const SourceLocation SrcLocToReport = CallArg->getSourceRange().getBegin();

    PathDiagnosticLocation BSLoc(SrcLocToReport, BR->getSourceManager());
    auto Report = std::make_unique<BasicBugReport>(
        Bug, "Call argument for 'this' parameter is uncounted and unsafe.",
        BSLoc);
    Report->addRange(CallArg->getSourceRange());
    BR->emitReport(std::move(Report));
  }
};
} // namespace

void ento::registerUncountedCallArgsChecker(CheckerManager &Mgr) {
  Mgr.registerChecker<UncountedCallArgsChecker>();
}

bool ento::shouldRegisterUncountedCallArgsChecker(const CheckerManager &) {
  return true;
}
