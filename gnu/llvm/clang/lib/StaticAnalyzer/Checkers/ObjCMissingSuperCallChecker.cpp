//==- ObjCMissingSuperCallChecker.cpp - Check missing super-calls in ObjC --==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines a ObjCMissingSuperCallChecker, a checker that
//  analyzes a UIViewController implementation to determine if it
//  correctly calls super in the methods where this is mandatory.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/Analysis/PathDiagnostic.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprObjC.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugReporter.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/AnalysisManager.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang;
using namespace ento;

namespace {
struct SelectorDescriptor {
  const char *SelectorName;
  unsigned ArgumentCount;
};

//===----------------------------------------------------------------------===//
// FindSuperCallVisitor - Identify specific calls to the superclass.
//===----------------------------------------------------------------------===//

class FindSuperCallVisitor : public RecursiveASTVisitor<FindSuperCallVisitor> {
public:
  explicit FindSuperCallVisitor(Selector S) : DoesCallSuper(false), Sel(S) {}

  bool VisitObjCMessageExpr(ObjCMessageExpr *E) {
    if (E->getSelector() == Sel)
      if (E->getReceiverKind() == ObjCMessageExpr::SuperInstance)
        DoesCallSuper = true;

    // Recurse if we didn't find the super call yet.
    return !DoesCallSuper;
  }

  bool DoesCallSuper;

private:
  Selector Sel;
};

//===----------------------------------------------------------------------===//
// ObjCSuperCallChecker
//===----------------------------------------------------------------------===//

class ObjCSuperCallChecker : public Checker<
                                      check::ASTDecl<ObjCImplementationDecl> > {
public:
  ObjCSuperCallChecker() = default;

  void checkASTDecl(const ObjCImplementationDecl *D, AnalysisManager &Mgr,
                    BugReporter &BR) const;
private:
  bool isCheckableClass(const ObjCImplementationDecl *D,
                        StringRef &SuperclassName) const;
  void initializeSelectors(ASTContext &Ctx) const;
  void fillSelectors(ASTContext &Ctx, ArrayRef<SelectorDescriptor> Sel,
                     StringRef ClassName) const;
  mutable llvm::StringMap<llvm::SmallPtrSet<Selector, 16>> SelectorsForClass;
  mutable bool IsInitialized = false;
};

}

/// Determine whether the given class has a superclass that we want
/// to check. The name of the found superclass is stored in SuperclassName.
///
/// \param D The declaration to check for superclasses.
/// \param[out] SuperclassName On return, the found superclass name.
bool ObjCSuperCallChecker::isCheckableClass(const ObjCImplementationDecl *D,
                                            StringRef &SuperclassName) const {
  const ObjCInterfaceDecl *ID = D->getClassInterface()->getSuperClass();
  for ( ; ID ; ID = ID->getSuperClass())
  {
    SuperclassName = ID->getIdentifier()->getName();
    if (SelectorsForClass.count(SuperclassName))
      return true;
  }
  return false;
}

void ObjCSuperCallChecker::fillSelectors(ASTContext &Ctx,
                                         ArrayRef<SelectorDescriptor> Sel,
                                         StringRef ClassName) const {
  llvm::SmallPtrSet<Selector, 16> &ClassSelectors =
      SelectorsForClass[ClassName];
  // Fill the Selectors SmallSet with all selectors we want to check.
  for (SelectorDescriptor Descriptor : Sel) {
    assert(Descriptor.ArgumentCount <= 1); // No multi-argument selectors yet.

    // Get the selector.
    const IdentifierInfo *II = &Ctx.Idents.get(Descriptor.SelectorName);

    Selector Sel = Ctx.Selectors.getSelector(Descriptor.ArgumentCount, &II);
    ClassSelectors.insert(Sel);
  }
}

void ObjCSuperCallChecker::initializeSelectors(ASTContext &Ctx) const {

  { // Initialize selectors for: UIViewController
    const SelectorDescriptor Selectors[] = {
      { "addChildViewController", 1 },
      { "viewDidAppear", 1 },
      { "viewDidDisappear", 1 },
      { "viewWillAppear", 1 },
      { "viewWillDisappear", 1 },
      { "removeFromParentViewController", 0 },
      { "didReceiveMemoryWarning", 0 },
      { "viewDidUnload", 0 },
      { "viewDidLoad", 0 },
      { "viewWillUnload", 0 },
      { "updateViewConstraints", 0 },
      { "encodeRestorableStateWithCoder", 1 },
      { "restoreStateWithCoder", 1 }};

    fillSelectors(Ctx, Selectors, "UIViewController");
  }

  { // Initialize selectors for: UIResponder
    const SelectorDescriptor Selectors[] = {
      { "resignFirstResponder", 0 }};

    fillSelectors(Ctx, Selectors, "UIResponder");
  }

  { // Initialize selectors for: NSResponder
    const SelectorDescriptor Selectors[] = {
      { "encodeRestorableStateWithCoder", 1 },
      { "restoreStateWithCoder", 1 }};

    fillSelectors(Ctx, Selectors, "NSResponder");
  }

  { // Initialize selectors for: NSDocument
    const SelectorDescriptor Selectors[] = {
      { "encodeRestorableStateWithCoder", 1 },
      { "restoreStateWithCoder", 1 }};

    fillSelectors(Ctx, Selectors, "NSDocument");
  }

  IsInitialized = true;
}

void ObjCSuperCallChecker::checkASTDecl(const ObjCImplementationDecl *D,
                                        AnalysisManager &Mgr,
                                        BugReporter &BR) const {
  ASTContext &Ctx = BR.getContext();

  // We need to initialize the selector table once.
  if (!IsInitialized)
    initializeSelectors(Ctx);

  // Find out whether this class has a superclass that we are supposed to check.
  StringRef SuperclassName;
  if (!isCheckableClass(D, SuperclassName))
    return;


  // Iterate over all instance methods.
  for (auto *MD : D->instance_methods()) {
    Selector S = MD->getSelector();
    // Find out whether this is a selector that we want to check.
    if (!SelectorsForClass[SuperclassName].count(S))
      continue;

    // Check if the method calls its superclass implementation.
    if (MD->getBody())
    {
      FindSuperCallVisitor Visitor(S);
      Visitor.TraverseDecl(MD);

      // It doesn't call super, emit a diagnostic.
      if (!Visitor.DoesCallSuper) {
        PathDiagnosticLocation DLoc =
          PathDiagnosticLocation::createEnd(MD->getBody(),
                                            BR.getSourceManager(),
                                            Mgr.getAnalysisDeclContext(D));

        const char *Name = "Missing call to superclass";
        SmallString<320> Buf;
        llvm::raw_svector_ostream os(Buf);

        os << "The '" << S.getAsString()
           << "' instance method in " << SuperclassName.str() << " subclass '"
           << *D << "' is missing a [super " << S.getAsString() << "] call";

        BR.EmitBasicReport(MD, this, Name, categories::CoreFoundationObjectiveC,
                           os.str(), DLoc);
      }
    }
  }
}


//===----------------------------------------------------------------------===//
// Check registration.
//===----------------------------------------------------------------------===//

void ento::registerObjCSuperCallChecker(CheckerManager &Mgr) {
  Mgr.registerChecker<ObjCSuperCallChecker>();
}

bool ento::shouldRegisterObjCSuperCallChecker(const CheckerManager &mgr) {
  return true;
}

/*
 ToDo list for expanding this check in the future, the list is not exhaustive.
 There are also cases where calling super is suggested but not "mandatory".
 In addition to be able to check the classes and methods below, architectural
 improvements like being able to allow for the super-call to be done in a called
 method would be good too.

UIDocument subclasses
- finishedHandlingError:recovered: (is multi-arg)
- finishedHandlingError:recovered: (is multi-arg)

UIViewController subclasses
- loadView (should *never* call super)
- transitionFromViewController:toViewController:
         duration:options:animations:completion: (is multi-arg)

UICollectionViewController subclasses
- loadView (take care because UIViewController subclasses should NOT call super
            in loadView, but UICollectionViewController subclasses should)

NSObject subclasses
- doesNotRecognizeSelector (it only has to call super if it doesn't throw)

UIPopoverBackgroundView subclasses (some of those are class methods)
- arrowDirection (should *never* call super)
- arrowOffset (should *never* call super)
- arrowBase (should *never* call super)
- arrowHeight (should *never* call super)
- contentViewInsets (should *never* call super)

UITextSelectionRect subclasses (some of those are properties)
- rect (should *never* call super)
- range (should *never* call super)
- writingDirection (should *never* call super)
- isVertical (should *never* call super)
- containsStart (should *never* call super)
- containsEnd (should *never* call super)
*/
