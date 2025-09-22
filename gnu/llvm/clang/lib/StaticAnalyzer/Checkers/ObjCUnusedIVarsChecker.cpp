//==- ObjCUnusedIVarsChecker.cpp - Check for unused ivars --------*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines a CheckObjCUnusedIvars, a checker that
//  analyzes an Objective-C class's interface/implementation to determine if it
//  has any ivars that are never accessed.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/Attr.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprObjC.h"
#include "clang/Analysis/PathDiagnostic.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/SourceManager.h"
#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugReporter.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "llvm/ADT/STLExtras.h"

using namespace clang;
using namespace ento;

enum IVarState { Unused, Used };
typedef llvm::DenseMap<const ObjCIvarDecl*,IVarState> IvarUsageMap;

static void Scan(IvarUsageMap& M, const Stmt *S) {
  if (!S)
    return;

  if (const ObjCIvarRefExpr *Ex = dyn_cast<ObjCIvarRefExpr>(S)) {
    const ObjCIvarDecl *D = Ex->getDecl();
    IvarUsageMap::iterator I = M.find(D);
    if (I != M.end())
      I->second = Used;
    return;
  }

  // Blocks can reference an instance variable of a class.
  if (const BlockExpr *BE = dyn_cast<BlockExpr>(S)) {
    Scan(M, BE->getBody());
    return;
  }

  if (const PseudoObjectExpr *POE = dyn_cast<PseudoObjectExpr>(S))
    for (const Expr *sub : POE->semantics()) {
      if (const OpaqueValueExpr *OVE = dyn_cast<OpaqueValueExpr>(sub))
        sub = OVE->getSourceExpr();
      Scan(M, sub);
    }

  for (const Stmt *SubStmt : S->children())
    Scan(M, SubStmt);
}

static void Scan(IvarUsageMap& M, const ObjCPropertyImplDecl *D) {
  if (!D)
    return;

  const ObjCIvarDecl *ID = D->getPropertyIvarDecl();

  if (!ID)
    return;

  IvarUsageMap::iterator I = M.find(ID);
  if (I != M.end())
    I->second = Used;
}

static void Scan(IvarUsageMap& M, const ObjCContainerDecl *D) {
  // Scan the methods for accesses.
  for (const auto *I : D->instance_methods())
    Scan(M, I->getBody());

  if (const ObjCImplementationDecl *ID = dyn_cast<ObjCImplementationDecl>(D)) {
    // Scan for @synthesized property methods that act as setters/getters
    // to an ivar.
    for (const auto *I : ID->property_impls())
      Scan(M, I);

    // Scan the associated categories as well.
    for (const auto *Cat : ID->getClassInterface()->visible_categories()) {
      if (const ObjCCategoryImplDecl *CID = Cat->getImplementation())
        Scan(M, CID);
    }
  }
}

static void Scan(IvarUsageMap &M, const DeclContext *C, const FileID FID,
                 const SourceManager &SM) {
  for (const auto *I : C->decls())
    if (const auto *FD = dyn_cast<FunctionDecl>(I)) {
      SourceLocation L = FD->getBeginLoc();
      if (SM.getFileID(L) == FID)
        Scan(M, FD->getBody());
    }
}

static void checkObjCUnusedIvar(const ObjCImplementationDecl *D,
                                BugReporter &BR,
                                const CheckerBase *Checker) {

  const ObjCInterfaceDecl *ID = D->getClassInterface();
  IvarUsageMap M;

  // Iterate over the ivars.
  for (const auto *Ivar : ID->ivars()) {
    // Ignore ivars that...
    // (a) aren't private
    // (b) explicitly marked unused
    // (c) are iboutlets
    // (d) are unnamed bitfields
    if (Ivar->getAccessControl() != ObjCIvarDecl::Private ||
        Ivar->hasAttr<UnusedAttr>() || Ivar->hasAttr<IBOutletAttr>() ||
        Ivar->hasAttr<IBOutletCollectionAttr>() || Ivar->isUnnamedBitField())
      continue;

    M[Ivar] = Unused;
  }

  if (M.empty())
    return;

  // Now scan the implementation declaration.
  Scan(M, D);

  // Any potentially unused ivars?
  bool hasUnused = false;
  for (IVarState State : llvm::make_second_range(M))
    if (State == Unused) {
      hasUnused = true;
      break;
    }

  if (!hasUnused)
    return;

  // We found some potentially unused ivars.  Scan the entire translation unit
  // for functions inside the @implementation that reference these ivars.
  // FIXME: In the future hopefully we can just use the lexical DeclContext
  // to go from the ObjCImplementationDecl to the lexically "nested"
  // C functions.
  const SourceManager &SM = BR.getSourceManager();
  Scan(M, D->getDeclContext(), SM.getFileID(D->getLocation()), SM);

  // Find ivars that are unused.
  for (auto [Ivar, State] : M)
    if (State == Unused) {
      std::string sbuf;
      llvm::raw_string_ostream os(sbuf);
      os << "Instance variable '" << *Ivar << "' in class '" << *ID
         << "' is never used by the methods in its @implementation "
            "(although it may be used by category methods).";

      PathDiagnosticLocation L =
          PathDiagnosticLocation::create(Ivar, BR.getSourceManager());
      BR.EmitBasicReport(ID, Checker, "Unused instance variable",
                         "Optimization", os.str(), L);
    }
}

//===----------------------------------------------------------------------===//
// ObjCUnusedIvarsChecker
//===----------------------------------------------------------------------===//

namespace {
class ObjCUnusedIvarsChecker : public Checker<
                                      check::ASTDecl<ObjCImplementationDecl> > {
public:
  void checkASTDecl(const ObjCImplementationDecl *D, AnalysisManager& mgr,
                    BugReporter &BR) const {
    checkObjCUnusedIvar(D, BR, this);
  }
};
}

void ento::registerObjCUnusedIvarsChecker(CheckerManager &mgr) {
  mgr.registerChecker<ObjCUnusedIvarsChecker>();
}

bool ento::shouldRegisterObjCUnusedIvarsChecker(const CheckerManager &mgr) {
  return true;
}
