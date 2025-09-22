//===-- CheckObjCInstMethSignature.cpp - Check ObjC method signatures -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines a CheckObjCInstMethSignature, a flow-insensitive check
//  that determines if an Objective-C class interface incorrectly redefines
//  the method signature in a subclass.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/Analysis/PathDiagnostic.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/Type.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugReporter.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang;
using namespace ento;

static bool AreTypesCompatible(QualType Derived, QualType Ancestor,
                               ASTContext &C) {

  // Right now don't compare the compatibility of pointers.  That involves
  // looking at subtyping relationships.  FIXME: Future patch.
  if (Derived->isAnyPointerType() &&  Ancestor->isAnyPointerType())
    return true;

  return C.typesAreCompatible(Derived, Ancestor);
}

static void CompareReturnTypes(const ObjCMethodDecl *MethDerived,
                               const ObjCMethodDecl *MethAncestor,
                               BugReporter &BR, ASTContext &Ctx,
                               const ObjCImplementationDecl *ID,
                               const CheckerBase *Checker) {

  QualType ResDerived = MethDerived->getReturnType();
  QualType ResAncestor = MethAncestor->getReturnType();

  if (!AreTypesCompatible(ResDerived, ResAncestor, Ctx)) {
    std::string sbuf;
    llvm::raw_string_ostream os(sbuf);

    os << "The Objective-C class '"
       << *MethDerived->getClassInterface()
       << "', which is derived from class '"
       << *MethAncestor->getClassInterface()
       << "', defines the instance method '";
    MethDerived->getSelector().print(os);
    os << "' whose return type is '" << ResDerived
       << "'.  A method with the same name (same selector) is also defined in "
          "class '"
       << *MethAncestor->getClassInterface() << "' and has a return type of '"
       << ResAncestor
       << "'.  These two types are incompatible, and may result in undefined "
          "behavior for clients of these classes.";

    PathDiagnosticLocation MethDLoc =
      PathDiagnosticLocation::createBegin(MethDerived,
                                          BR.getSourceManager());

    BR.EmitBasicReport(
        MethDerived, Checker, "Incompatible instance method return type",
        categories::CoreFoundationObjectiveC, os.str(), MethDLoc);
  }
}

static void CheckObjCInstMethSignature(const ObjCImplementationDecl *ID,
                                       BugReporter &BR,
                                       const CheckerBase *Checker) {

  const ObjCInterfaceDecl *D = ID->getClassInterface();
  const ObjCInterfaceDecl *C = D->getSuperClass();

  if (!C)
    return;

  ASTContext &Ctx = BR.getContext();

  // Build a DenseMap of the methods for quick querying.
  typedef llvm::DenseMap<Selector,ObjCMethodDecl*> MapTy;
  MapTy IMeths;
  unsigned NumMethods = 0;

  for (auto *M : ID->instance_methods()) {
    IMeths[M->getSelector()] = M;
    ++NumMethods;
  }

  // Now recurse the class hierarchy chain looking for methods with the
  // same signatures.
  while (C && NumMethods) {
    for (const auto *M : C->instance_methods()) {
      Selector S = M->getSelector();

      MapTy::iterator MI = IMeths.find(S);

      if (MI == IMeths.end() || MI->second == nullptr)
        continue;

      --NumMethods;
      ObjCMethodDecl *MethDerived = MI->second;
      MI->second = nullptr;

      CompareReturnTypes(MethDerived, M, BR, Ctx, ID, Checker);
    }

    C = C->getSuperClass();
  }
}

//===----------------------------------------------------------------------===//
// ObjCMethSigsChecker
//===----------------------------------------------------------------------===//

namespace {
class ObjCMethSigsChecker : public Checker<
                                      check::ASTDecl<ObjCImplementationDecl> > {
public:
  void checkASTDecl(const ObjCImplementationDecl *D, AnalysisManager& mgr,
                    BugReporter &BR) const {
    CheckObjCInstMethSignature(D, BR, this);
  }
};
}

void ento::registerObjCMethSigsChecker(CheckerManager &mgr) {
  mgr.registerChecker<ObjCMethSigsChecker>();
}

bool ento::shouldRegisterObjCMethSigsChecker(const CheckerManager &mgr) {
  return true;
}
