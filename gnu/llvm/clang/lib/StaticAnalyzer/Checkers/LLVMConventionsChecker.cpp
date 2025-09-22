//=== LLVMConventionsChecker.cpp - Check LLVM codebase conventions ---*- C++ -*-
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This defines LLVMConventionsChecker, a bunch of small little checks
// for checking specific coding conventions in the LLVM/Clang codebase.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/StmtVisitor.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugReporter.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang;
using namespace ento;

//===----------------------------------------------------------------------===//
// Generic type checking routines.
//===----------------------------------------------------------------------===//

static bool IsLLVMStringRef(QualType T) {
  const RecordType *RT = T->getAs<RecordType>();
  if (!RT)
    return false;

  return StringRef(QualType(RT, 0).getAsString()) == "class StringRef";
}

/// Check whether the declaration is semantically inside the top-level
/// namespace named by ns.
static bool InNamespace(const Decl *D, StringRef NS) {
  const NamespaceDecl *ND = dyn_cast<NamespaceDecl>(D->getDeclContext());
  if (!ND)
    return false;
  const IdentifierInfo *II = ND->getIdentifier();
  if (!II || II->getName() != NS)
    return false;
  return isa<TranslationUnitDecl>(ND->getDeclContext());
}

static bool IsStdString(QualType T) {
  if (const ElaboratedType *QT = T->getAs<ElaboratedType>())
    T = QT->getNamedType();

  const TypedefType *TT = T->getAs<TypedefType>();
  if (!TT)
    return false;

  const TypedefNameDecl *TD = TT->getDecl();

  if (!TD->isInStdNamespace())
    return false;

  return TD->getName() == "string";
}

static bool IsClangType(const RecordDecl *RD) {
  return RD->getName() == "Type" && InNamespace(RD, "clang");
}

static bool IsClangDecl(const RecordDecl *RD) {
  return RD->getName() == "Decl" && InNamespace(RD, "clang");
}

static bool IsClangStmt(const RecordDecl *RD) {
  return RD->getName() == "Stmt" && InNamespace(RD, "clang");
}

static bool IsClangAttr(const RecordDecl *RD) {
  return RD->getName() == "Attr" && InNamespace(RD, "clang");
}

static bool IsStdVector(QualType T) {
  const TemplateSpecializationType *TS = T->getAs<TemplateSpecializationType>();
  if (!TS)
    return false;

  TemplateName TM = TS->getTemplateName();
  TemplateDecl *TD = TM.getAsTemplateDecl();

  if (!TD || !InNamespace(TD, "std"))
    return false;

  return TD->getName() == "vector";
}

static bool IsSmallVector(QualType T) {
  const TemplateSpecializationType *TS = T->getAs<TemplateSpecializationType>();
  if (!TS)
    return false;

  TemplateName TM = TS->getTemplateName();
  TemplateDecl *TD = TM.getAsTemplateDecl();

  if (!TD || !InNamespace(TD, "llvm"))
    return false;

  return TD->getName() == "SmallVector";
}

//===----------------------------------------------------------------------===//
// CHECK: a StringRef should not be bound to a temporary std::string whose
// lifetime is shorter than the StringRef's.
//===----------------------------------------------------------------------===//

namespace {
class StringRefCheckerVisitor : public StmtVisitor<StringRefCheckerVisitor> {
  const Decl *DeclWithIssue;
  BugReporter &BR;
  const CheckerBase *Checker;

public:
  StringRefCheckerVisitor(const Decl *declWithIssue, BugReporter &br,
                          const CheckerBase *checker)
      : DeclWithIssue(declWithIssue), BR(br), Checker(checker) {}
  void VisitChildren(Stmt *S) {
    for (Stmt *Child : S->children())
      if (Child)
        Visit(Child);
  }
  void VisitStmt(Stmt *S) { VisitChildren(S); }
  void VisitDeclStmt(DeclStmt *DS);
private:
  void VisitVarDecl(VarDecl *VD);
};
} // end anonymous namespace

static void CheckStringRefAssignedTemporary(const Decl *D, BugReporter &BR,
                                            const CheckerBase *Checker) {
  StringRefCheckerVisitor walker(D, BR, Checker);
  walker.Visit(D->getBody());
}

void StringRefCheckerVisitor::VisitDeclStmt(DeclStmt *S) {
  VisitChildren(S);

  for (auto *I : S->decls())
    if (VarDecl *VD = dyn_cast<VarDecl>(I))
      VisitVarDecl(VD);
}

void StringRefCheckerVisitor::VisitVarDecl(VarDecl *VD) {
  Expr *Init = VD->getInit();
  if (!Init)
    return;

  // Pattern match for:
  // StringRef x = call() (where call returns std::string)
  if (!IsLLVMStringRef(VD->getType()))
    return;
  ExprWithCleanups *Ex1 = dyn_cast<ExprWithCleanups>(Init);
  if (!Ex1)
    return;
  CXXConstructExpr *Ex2 = dyn_cast<CXXConstructExpr>(Ex1->getSubExpr());
  if (!Ex2 || Ex2->getNumArgs() != 1)
    return;
  ImplicitCastExpr *Ex3 = dyn_cast<ImplicitCastExpr>(Ex2->getArg(0));
  if (!Ex3)
    return;
  CXXConstructExpr *Ex4 = dyn_cast<CXXConstructExpr>(Ex3->getSubExpr());
  if (!Ex4 || Ex4->getNumArgs() != 1)
    return;
  ImplicitCastExpr *Ex5 = dyn_cast<ImplicitCastExpr>(Ex4->getArg(0));
  if (!Ex5)
    return;
  CXXBindTemporaryExpr *Ex6 = dyn_cast<CXXBindTemporaryExpr>(Ex5->getSubExpr());
  if (!Ex6 || !IsStdString(Ex6->getType()))
    return;

  // Okay, badness!  Report an error.
  const char *desc = "StringRef should not be bound to temporary "
                     "std::string that it outlives";
  PathDiagnosticLocation VDLoc =
    PathDiagnosticLocation::createBegin(VD, BR.getSourceManager());
  BR.EmitBasicReport(DeclWithIssue, Checker, desc, "LLVM Conventions", desc,
                     VDLoc, Init->getSourceRange());
}

//===----------------------------------------------------------------------===//
// CHECK: Clang AST nodes should not have fields that can allocate
//   memory.
//===----------------------------------------------------------------------===//

static bool AllocatesMemory(QualType T) {
  return IsStdVector(T) || IsStdString(T) || IsSmallVector(T);
}

// This type checking could be sped up via dynamic programming.
static bool IsPartOfAST(const CXXRecordDecl *R) {
  if (IsClangStmt(R) || IsClangType(R) || IsClangDecl(R) || IsClangAttr(R))
    return true;

  for (const auto &BS : R->bases()) {
    QualType T = BS.getType();
    if (const RecordType *baseT = T->getAs<RecordType>()) {
      CXXRecordDecl *baseD = cast<CXXRecordDecl>(baseT->getDecl());
      if (IsPartOfAST(baseD))
        return true;
    }
  }

  return false;
}

namespace {
class ASTFieldVisitor {
  SmallVector<FieldDecl*, 10> FieldChain;
  const CXXRecordDecl *Root;
  BugReporter &BR;
  const CheckerBase *Checker;

public:
  ASTFieldVisitor(const CXXRecordDecl *root, BugReporter &br,
                  const CheckerBase *checker)
      : Root(root), BR(br), Checker(checker) {}

  void Visit(FieldDecl *D);
  void ReportError(QualType T);
};
} // end anonymous namespace

static void CheckASTMemory(const CXXRecordDecl *R, BugReporter &BR,
                           const CheckerBase *Checker) {
  if (!IsPartOfAST(R))
    return;

  for (auto *I : R->fields()) {
    ASTFieldVisitor walker(R, BR, Checker);
    walker.Visit(I);
  }
}

void ASTFieldVisitor::Visit(FieldDecl *D) {
  FieldChain.push_back(D);

  QualType T = D->getType();

  if (AllocatesMemory(T))
    ReportError(T);

  if (const RecordType *RT = T->getAs<RecordType>()) {
    const RecordDecl *RD = RT->getDecl()->getDefinition();
    for (auto *I : RD->fields())
      Visit(I);
  }

  FieldChain.pop_back();
}

void ASTFieldVisitor::ReportError(QualType T) {
  SmallString<1024> buf;
  llvm::raw_svector_ostream os(buf);

  os << "AST class '" << Root->getName() << "' has a field '"
     << FieldChain.front()->getName() << "' that allocates heap memory";
  if (FieldChain.size() > 1) {
    os << " via the following chain: ";
    bool isFirst = true;
    for (SmallVectorImpl<FieldDecl*>::iterator I=FieldChain.begin(),
         E=FieldChain.end(); I!=E; ++I) {
      if (!isFirst)
        os << '.';
      else
        isFirst = false;
      os << (*I)->getName();
    }
  }
  os << " (type " << FieldChain.back()->getType() << ")";

  // Note that this will fire for every translation unit that uses this
  // class.  This is suboptimal, but at least scan-build will merge
  // duplicate HTML reports.  In the future we need a unified way of merging
  // duplicate reports across translation units.  For C++ classes we cannot
  // just report warnings when we see an out-of-line method definition for a
  // class, as that heuristic doesn't always work (the complete definition of
  // the class may be in the header file, for example).
  PathDiagnosticLocation L = PathDiagnosticLocation::createBegin(
                               FieldChain.front(), BR.getSourceManager());
  BR.EmitBasicReport(Root, Checker, "AST node allocates heap memory",
                     "LLVM Conventions", os.str(), L);
}

//===----------------------------------------------------------------------===//
// LLVMConventionsChecker
//===----------------------------------------------------------------------===//

namespace {
class LLVMConventionsChecker : public Checker<
                                                check::ASTDecl<CXXRecordDecl>,
                                                check::ASTCodeBody > {
public:
  void checkASTDecl(const CXXRecordDecl *R, AnalysisManager& mgr,
                    BugReporter &BR) const {
    if (R->isCompleteDefinition())
      CheckASTMemory(R, BR, this);
  }

  void checkASTCodeBody(const Decl *D, AnalysisManager& mgr,
                        BugReporter &BR) const {
    CheckStringRefAssignedTemporary(D, BR, this);
  }
};
}

void ento::registerLLVMConventionsChecker(CheckerManager &mgr) {
  mgr.registerChecker<LLVMConventionsChecker>();
}

bool ento::shouldRegisterLLVMConventionsChecker(const CheckerManager &mgr) {
  return true;
}
