//== ObjCContainersASTChecker.cpp - CoreFoundation containers API *- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// An AST checker that looks for common pitfalls when using 'CFArray',
// 'CFDictionary', 'CFSet' APIs.
//
//===----------------------------------------------------------------------===//
#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/AST/StmtVisitor.h"
#include "clang/Analysis/AnalysisDeclContext.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugReporter.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/AnalysisManager.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang;
using namespace ento;

namespace {
class WalkAST : public StmtVisitor<WalkAST> {
  BugReporter &BR;
  const CheckerBase *Checker;
  AnalysisDeclContext* AC;
  ASTContext &ASTC;
  uint64_t PtrWidth;

  /// Check if the type has pointer size (very conservative).
  inline bool isPointerSize(const Type *T) {
    if (!T)
      return true;
    if (T->isIncompleteType())
      return true;
    return (ASTC.getTypeSize(T) == PtrWidth);
  }

  /// Check if the type is a pointer/array to pointer sized values.
  inline bool hasPointerToPointerSizedType(const Expr *E) {
    QualType T = E->getType();

    // The type could be either a pointer or array.
    const Type *TP = T.getTypePtr();
    QualType PointeeT = TP->getPointeeType();
    if (!PointeeT.isNull()) {
      // If the type is a pointer to an array, check the size of the array
      // elements. To avoid false positives coming from assumption that the
      // values x and &x are equal when x is an array.
      if (const Type *TElem = PointeeT->getArrayElementTypeNoTypeQual())
        if (isPointerSize(TElem))
          return true;

      // Else, check the pointee size.
      return isPointerSize(PointeeT.getTypePtr());
    }

    if (const Type *TElem = TP->getArrayElementTypeNoTypeQual())
      return isPointerSize(TElem);

    // The type must be an array/pointer type.

    // This could be a null constant, which is allowed.
    return static_cast<bool>(
        E->isNullPointerConstant(ASTC, Expr::NPC_ValueDependentIsNull));
  }

public:
  WalkAST(BugReporter &br, const CheckerBase *checker, AnalysisDeclContext *ac)
      : BR(br), Checker(checker), AC(ac), ASTC(AC->getASTContext()),
        PtrWidth(ASTC.getTargetInfo().getPointerWidth(LangAS::Default)) {}

  // Statement visitor methods.
  void VisitChildren(Stmt *S);
  void VisitStmt(Stmt *S) { VisitChildren(S); }
  void VisitCallExpr(CallExpr *CE);
};
} // end anonymous namespace

static StringRef getCalleeName(CallExpr *CE) {
  const FunctionDecl *FD = CE->getDirectCallee();
  if (!FD)
    return StringRef();

  IdentifierInfo *II = FD->getIdentifier();
  if (!II)   // if no identifier, not a simple C function
    return StringRef();

  return II->getName();
}

void WalkAST::VisitCallExpr(CallExpr *CE) {
  StringRef Name = getCalleeName(CE);
  if (Name.empty())
    return;

  const Expr *Arg = nullptr;
  unsigned ArgNum;

  if (Name == "CFArrayCreate" || Name == "CFSetCreate") {
    if (CE->getNumArgs() != 4)
      return;
    ArgNum = 1;
    Arg = CE->getArg(ArgNum)->IgnoreParenCasts();
    if (hasPointerToPointerSizedType(Arg))
        return;
  } else if (Name == "CFDictionaryCreate") {
    if (CE->getNumArgs() != 6)
      return;
    // Check first argument.
    ArgNum = 1;
    Arg = CE->getArg(ArgNum)->IgnoreParenCasts();
    if (hasPointerToPointerSizedType(Arg)) {
      // Check second argument.
      ArgNum = 2;
      Arg = CE->getArg(ArgNum)->IgnoreParenCasts();
      if (hasPointerToPointerSizedType(Arg))
        // Both are good, return.
        return;
    }
  }

  if (Arg) {
    assert(ArgNum == 1 || ArgNum == 2);

    SmallString<64> BufName;
    llvm::raw_svector_ostream OsName(BufName);
    OsName << " Invalid use of '" << Name << "'" ;

    SmallString<256> Buf;
    llvm::raw_svector_ostream Os(Buf);
    // Use "second" and "third" since users will expect 1-based indexing
    // for parameter names when mentioned in prose.
    Os << " The " << ((ArgNum == 1) ? "second" : "third") << " argument to '"
       << Name << "' must be a C array of pointer-sized values, not '"
       << Arg->getType() << "'";

    PathDiagnosticLocation CELoc =
        PathDiagnosticLocation::createBegin(CE, BR.getSourceManager(), AC);
    BR.EmitBasicReport(AC->getDecl(), Checker, OsName.str(),
                       categories::CoreFoundationObjectiveC, Os.str(), CELoc,
                       Arg->getSourceRange());
  }

  // Recurse and check children.
  VisitChildren(CE);
}

void WalkAST::VisitChildren(Stmt *S) {
  for (Stmt *Child : S->children())
    if (Child)
      Visit(Child);
}

namespace {
class ObjCContainersASTChecker : public Checker<check::ASTCodeBody> {
public:

  void checkASTCodeBody(const Decl *D, AnalysisManager& Mgr,
                        BugReporter &BR) const {
    WalkAST walker(BR, this, Mgr.getAnalysisDeclContext(D));
    walker.Visit(D->getBody());
  }
};
}

void ento::registerObjCContainersASTChecker(CheckerManager &mgr) {
  mgr.registerChecker<ObjCContainersASTChecker>();
}

bool ento::shouldRegisterObjCContainersASTChecker(const CheckerManager &mgr) {
  return true;
}
