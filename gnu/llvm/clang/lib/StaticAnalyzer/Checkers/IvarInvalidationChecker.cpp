//===- IvarInvalidationChecker.cpp ------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This checker implements annotation driven invalidation checking. If a class
//  contains a method annotated with 'objc_instance_variable_invalidator',
//  - (void) foo
//           __attribute__((annotate("objc_instance_variable_invalidator")));
//  all the "ivalidatable" instance variables of this class should be
//  invalidated. We call an instance variable ivalidatable if it is an object of
//  a class which contains an invalidation method. There could be multiple
//  methods annotated with such annotations per class, either one can be used
//  to invalidate the ivar. An ivar or property are considered to be
//  invalidated if they are being assigned 'nil' or an invalidation method has
//  been called on them. An invalidation method should either invalidate all
//  the ivars or call another invalidation method (on self).
//
//  Partial invalidor annotation allows to address cases when ivars are
//  invalidated by other methods, which might or might not be called from
//  the invalidation method. The checker checks that each invalidation
//  method and all the partial methods cumulatively invalidate all ivars.
//    __attribute__((annotate("objc_instance_variable_invalidator_partial")));
//
//===----------------------------------------------------------------------===//

#include "clang/AST/Attr.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/StmtVisitor.h"
#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugReporter.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/AnalysisManager.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallString.h"

using namespace clang;
using namespace ento;

namespace {
struct ChecksFilter {
  /// Check for missing invalidation method declarations.
  bool check_MissingInvalidationMethod = false;
  /// Check that all ivars are invalidated.
  bool check_InstanceVariableInvalidation = false;

  CheckerNameRef checkName_MissingInvalidationMethod;
  CheckerNameRef checkName_InstanceVariableInvalidation;
};

class IvarInvalidationCheckerImpl {
  typedef llvm::SmallSetVector<const ObjCMethodDecl*, 2> MethodSet;
  typedef llvm::DenseMap<const ObjCMethodDecl*,
                         const ObjCIvarDecl*> MethToIvarMapTy;
  typedef llvm::DenseMap<const ObjCPropertyDecl*,
                         const ObjCIvarDecl*> PropToIvarMapTy;
  typedef llvm::DenseMap<const ObjCIvarDecl*,
                         const ObjCPropertyDecl*> IvarToPropMapTy;

  struct InvalidationInfo {
    /// Has the ivar been invalidated?
    bool IsInvalidated = false;

    /// The methods which can be used to invalidate the ivar.
    MethodSet InvalidationMethods;

    InvalidationInfo() = default;
    void addInvalidationMethod(const ObjCMethodDecl *MD) {
      InvalidationMethods.insert(MD);
    }

    bool needsInvalidation() const {
      return !InvalidationMethods.empty();
    }

    bool hasMethod(const ObjCMethodDecl *MD) {
      if (IsInvalidated)
        return true;
      for (const ObjCMethodDecl *Curr : InvalidationMethods) {
        if (Curr == MD) {
          IsInvalidated = true;
          return true;
        }
      }
      return false;
    }
  };

  typedef llvm::DenseMap<const ObjCIvarDecl*, InvalidationInfo> IvarSet;

  /// Statement visitor, which walks the method body and flags the ivars
  /// referenced in it (either directly or via property).
  class MethodCrawler : public ConstStmtVisitor<MethodCrawler> {
    /// The set of Ivars which need to be invalidated.
    IvarSet &IVars;

    /// Flag is set as the result of a message send to another
    /// invalidation method.
    bool &CalledAnotherInvalidationMethod;

    /// Property setter to ivar mapping.
    const MethToIvarMapTy &PropertySetterToIvarMap;

    /// Property getter to ivar mapping.
    const MethToIvarMapTy &PropertyGetterToIvarMap;

    /// Property to ivar mapping.
    const PropToIvarMapTy &PropertyToIvarMap;

    /// The invalidation method being currently processed.
    const ObjCMethodDecl *InvalidationMethod;

    ASTContext &Ctx;

    /// Peel off parens, casts, OpaqueValueExpr, and PseudoObjectExpr.
    const Expr *peel(const Expr *E) const;

    /// Does this expression represent zero: '0'?
    bool isZero(const Expr *E) const;

    /// Mark the given ivar as invalidated.
    void markInvalidated(const ObjCIvarDecl *Iv);

    /// Checks if IvarRef refers to the tracked IVar, if yes, marks it as
    /// invalidated.
    void checkObjCIvarRefExpr(const ObjCIvarRefExpr *IvarRef);

    /// Checks if ObjCPropertyRefExpr refers to the tracked IVar, if yes, marks
    /// it as invalidated.
    void checkObjCPropertyRefExpr(const ObjCPropertyRefExpr *PA);

    /// Checks if ObjCMessageExpr refers to (is a getter for) the tracked IVar,
    /// if yes, marks it as invalidated.
    void checkObjCMessageExpr(const ObjCMessageExpr *ME);

    /// Checks if the Expr refers to an ivar, if yes, marks it as invalidated.
    void check(const Expr *E);

  public:
    MethodCrawler(IvarSet &InIVars,
                  bool &InCalledAnotherInvalidationMethod,
                  const MethToIvarMapTy &InPropertySetterToIvarMap,
                  const MethToIvarMapTy &InPropertyGetterToIvarMap,
                  const PropToIvarMapTy &InPropertyToIvarMap,
                  ASTContext &InCtx)
    : IVars(InIVars),
      CalledAnotherInvalidationMethod(InCalledAnotherInvalidationMethod),
      PropertySetterToIvarMap(InPropertySetterToIvarMap),
      PropertyGetterToIvarMap(InPropertyGetterToIvarMap),
      PropertyToIvarMap(InPropertyToIvarMap),
      InvalidationMethod(nullptr),
      Ctx(InCtx) {}

    void VisitStmt(const Stmt *S) { VisitChildren(S); }

    void VisitBinaryOperator(const BinaryOperator *BO);

    void VisitObjCMessageExpr(const ObjCMessageExpr *ME);

    void VisitChildren(const Stmt *S) {
      for (const auto *Child : S->children()) {
        if (Child)
          this->Visit(Child);
        if (CalledAnotherInvalidationMethod)
          return;
      }
    }
  };

  /// Check if the any of the methods inside the interface are annotated with
  /// the invalidation annotation, update the IvarInfo accordingly.
  /// \param LookForPartial is set when we are searching for partial
  ///        invalidators.
  static void containsInvalidationMethod(const ObjCContainerDecl *D,
                                         InvalidationInfo &Out,
                                         bool LookForPartial);

  /// Check if ivar should be tracked and add to TrackedIvars if positive.
  /// Returns true if ivar should be tracked.
  static bool trackIvar(const ObjCIvarDecl *Iv, IvarSet &TrackedIvars,
                        const ObjCIvarDecl **FirstIvarDecl);

  /// Given the property declaration, and the list of tracked ivars, finds
  /// the ivar backing the property when possible. Returns '0' when no such
  /// ivar could be found.
  static const ObjCIvarDecl *findPropertyBackingIvar(
      const ObjCPropertyDecl *Prop,
      const ObjCInterfaceDecl *InterfaceD,
      IvarSet &TrackedIvars,
      const ObjCIvarDecl **FirstIvarDecl);

  /// Print ivar name or the property if the given ivar backs a property.
  static void printIvar(llvm::raw_svector_ostream &os,
                        const ObjCIvarDecl *IvarDecl,
                        const IvarToPropMapTy &IvarToPopertyMap);

  void reportNoInvalidationMethod(CheckerNameRef CheckName,
                                  const ObjCIvarDecl *FirstIvarDecl,
                                  const IvarToPropMapTy &IvarToPopertyMap,
                                  const ObjCInterfaceDecl *InterfaceD,
                                  bool MissingDeclaration) const;

  void reportIvarNeedsInvalidation(const ObjCIvarDecl *IvarD,
                                   const IvarToPropMapTy &IvarToPopertyMap,
                                   const ObjCMethodDecl *MethodD) const;

  AnalysisManager& Mgr;
  BugReporter &BR;
  /// Filter on the checks performed.
  const ChecksFilter &Filter;

public:
  IvarInvalidationCheckerImpl(AnalysisManager& InMgr,
                              BugReporter &InBR,
                              const ChecksFilter &InFilter) :
    Mgr (InMgr), BR(InBR), Filter(InFilter) {}

  void visit(const ObjCImplementationDecl *D) const;
};

static bool isInvalidationMethod(const ObjCMethodDecl *M, bool LookForPartial) {
  for (const auto *Ann : M->specific_attrs<AnnotateAttr>()) {
    if (!LookForPartial &&
        Ann->getAnnotation() == "objc_instance_variable_invalidator")
      return true;
    if (LookForPartial &&
        Ann->getAnnotation() == "objc_instance_variable_invalidator_partial")
      return true;
  }
  return false;
}

void IvarInvalidationCheckerImpl::containsInvalidationMethod(
    const ObjCContainerDecl *D, InvalidationInfo &OutInfo, bool Partial) {

  if (!D)
    return;

  assert(!isa<ObjCImplementationDecl>(D));
  // TODO: Cache the results.

  // Check all methods.
  for (const auto *MDI : D->methods())
    if (isInvalidationMethod(MDI, Partial))
      OutInfo.addInvalidationMethod(
          cast<ObjCMethodDecl>(MDI->getCanonicalDecl()));

  // If interface, check all parent protocols and super.
  if (const ObjCInterfaceDecl *InterfD = dyn_cast<ObjCInterfaceDecl>(D)) {

    // Visit all protocols.
    for (const auto *I : InterfD->protocols())
      containsInvalidationMethod(I->getDefinition(), OutInfo, Partial);

    // Visit all categories in case the invalidation method is declared in
    // a category.
    for (const auto *Ext : InterfD->visible_extensions())
      containsInvalidationMethod(Ext, OutInfo, Partial);

    containsInvalidationMethod(InterfD->getSuperClass(), OutInfo, Partial);
    return;
  }

  // If protocol, check all parent protocols.
  if (const ObjCProtocolDecl *ProtD = dyn_cast<ObjCProtocolDecl>(D)) {
    for (const auto *I : ProtD->protocols()) {
      containsInvalidationMethod(I->getDefinition(), OutInfo, Partial);
    }
    return;
  }
}

bool IvarInvalidationCheckerImpl::trackIvar(const ObjCIvarDecl *Iv,
                                        IvarSet &TrackedIvars,
                                        const ObjCIvarDecl **FirstIvarDecl) {
  QualType IvQTy = Iv->getType();
  const ObjCObjectPointerType *IvTy = IvQTy->getAs<ObjCObjectPointerType>();
  if (!IvTy)
    return false;
  const ObjCInterfaceDecl *IvInterf = IvTy->getInterfaceDecl();

  InvalidationInfo Info;
  containsInvalidationMethod(IvInterf, Info, /*LookForPartial*/ false);
  if (Info.needsInvalidation()) {
    const ObjCIvarDecl *I = cast<ObjCIvarDecl>(Iv->getCanonicalDecl());
    TrackedIvars[I] = Info;
    if (!*FirstIvarDecl)
      *FirstIvarDecl = I;
    return true;
  }
  return false;
}

const ObjCIvarDecl *IvarInvalidationCheckerImpl::findPropertyBackingIvar(
                        const ObjCPropertyDecl *Prop,
                        const ObjCInterfaceDecl *InterfaceD,
                        IvarSet &TrackedIvars,
                        const ObjCIvarDecl **FirstIvarDecl) {
  const ObjCIvarDecl *IvarD = nullptr;

  // Lookup for the synthesized case.
  IvarD = Prop->getPropertyIvarDecl();
  // We only track the ivars/properties that are defined in the current
  // class (not the parent).
  if (IvarD && IvarD->getContainingInterface() == InterfaceD) {
    if (TrackedIvars.count(IvarD)) {
      return IvarD;
    }
    // If the ivar is synthesized we still want to track it.
    if (trackIvar(IvarD, TrackedIvars, FirstIvarDecl))
      return IvarD;
  }

  // Lookup IVars named "_PropName"or "PropName" among the tracked Ivars.
  StringRef PropName = Prop->getIdentifier()->getName();
  for (const ObjCIvarDecl *Iv : llvm::make_first_range(TrackedIvars)) {
    StringRef IvarName = Iv->getName();

    if (IvarName == PropName)
      return Iv;

    SmallString<128> PropNameWithUnderscore;
    {
      llvm::raw_svector_ostream os(PropNameWithUnderscore);
      os << '_' << PropName;
    }
    if (IvarName == PropNameWithUnderscore)
      return Iv;
  }

  // Note, this is a possible source of false positives. We could look at the
  // getter implementation to find the ivar when its name is not derived from
  // the property name.
  return nullptr;
}

void IvarInvalidationCheckerImpl::printIvar(llvm::raw_svector_ostream &os,
                                      const ObjCIvarDecl *IvarDecl,
                                      const IvarToPropMapTy &IvarToPopertyMap) {
  if (IvarDecl->getSynthesize()) {
    const ObjCPropertyDecl *PD = IvarToPopertyMap.lookup(IvarDecl);
    assert(PD &&"Do we synthesize ivars for something other than properties?");
    os << "Property "<< PD->getName() << " ";
  } else {
    os << "Instance variable "<< IvarDecl->getName() << " ";
  }
}

// Check that the invalidatable interfaces with ivars/properties implement the
// invalidation methods.
void IvarInvalidationCheckerImpl::
visit(const ObjCImplementationDecl *ImplD) const {
  // Collect all ivars that need cleanup.
  IvarSet Ivars;
  // Record the first Ivar needing invalidation; used in reporting when only
  // one ivar is sufficient. Cannot grab the first on the Ivars set to ensure
  // deterministic output.
  const ObjCIvarDecl *FirstIvarDecl = nullptr;
  const ObjCInterfaceDecl *InterfaceD = ImplD->getClassInterface();

  // Collect ivars declared in this class, its extensions and its implementation
  ObjCInterfaceDecl *IDecl = const_cast<ObjCInterfaceDecl *>(InterfaceD);
  for (const ObjCIvarDecl *Iv = IDecl->all_declared_ivar_begin(); Iv;
       Iv= Iv->getNextIvar())
    trackIvar(Iv, Ivars, &FirstIvarDecl);

  // Construct Property/Property Accessor to Ivar maps to assist checking if an
  // ivar which is backing a property has been reset.
  MethToIvarMapTy PropSetterToIvarMap;
  MethToIvarMapTy PropGetterToIvarMap;
  PropToIvarMapTy PropertyToIvarMap;
  IvarToPropMapTy IvarToPopertyMap;

  ObjCInterfaceDecl::PropertyMap PropMap;
  InterfaceD->collectPropertiesToImplement(PropMap);

  for (const ObjCPropertyDecl *PD : llvm::make_second_range(PropMap)) {
    if (PD->isClassProperty())
      continue;

    const ObjCIvarDecl *ID = findPropertyBackingIvar(PD, InterfaceD, Ivars,
                                                     &FirstIvarDecl);
    if (!ID)
      continue;

    // Store the mappings.
    PD = cast<ObjCPropertyDecl>(PD->getCanonicalDecl());
    PropertyToIvarMap[PD] = ID;
    IvarToPopertyMap[ID] = PD;

    // Find the setter and the getter.
    const ObjCMethodDecl *SetterD = PD->getSetterMethodDecl();
    if (SetterD) {
      SetterD = SetterD->getCanonicalDecl();
      PropSetterToIvarMap[SetterD] = ID;
    }

    const ObjCMethodDecl *GetterD = PD->getGetterMethodDecl();
    if (GetterD) {
      GetterD = GetterD->getCanonicalDecl();
      PropGetterToIvarMap[GetterD] = ID;
    }
  }

  // If no ivars need invalidation, there is nothing to check here.
  if (Ivars.empty())
    return;

  // Find all partial invalidation methods.
  InvalidationInfo PartialInfo;
  containsInvalidationMethod(InterfaceD, PartialInfo, /*LookForPartial*/ true);

  // Remove ivars invalidated by the partial invalidation methods. They do not
  // need to be invalidated in the regular invalidation methods.
  bool AtImplementationContainsAtLeastOnePartialInvalidationMethod = false;
  for (const ObjCMethodDecl *InterfD : PartialInfo.InvalidationMethods) {
    // Get the corresponding method in the @implementation.
    const ObjCMethodDecl *D = ImplD->getMethod(InterfD->getSelector(),
                                               InterfD->isInstanceMethod());
    if (D && D->hasBody()) {
      AtImplementationContainsAtLeastOnePartialInvalidationMethod = true;

      bool CalledAnotherInvalidationMethod = false;
      // The MethodCrowler is going to remove the invalidated ivars.
      MethodCrawler(Ivars,
                    CalledAnotherInvalidationMethod,
                    PropSetterToIvarMap,
                    PropGetterToIvarMap,
                    PropertyToIvarMap,
                    BR.getContext()).VisitStmt(D->getBody());
      // If another invalidation method was called, trust that full invalidation
      // has occurred.
      if (CalledAnotherInvalidationMethod)
        Ivars.clear();
    }
  }

  // If all ivars have been invalidated by partial invalidators, there is
  // nothing to check here.
  if (Ivars.empty())
    return;

  // Find all invalidation methods in this @interface declaration and parents.
  InvalidationInfo Info;
  containsInvalidationMethod(InterfaceD, Info, /*LookForPartial*/ false);

  // Report an error in case none of the invalidation methods are declared.
  if (!Info.needsInvalidation() && !PartialInfo.needsInvalidation()) {
    if (Filter.check_MissingInvalidationMethod)
      reportNoInvalidationMethod(Filter.checkName_MissingInvalidationMethod,
                                 FirstIvarDecl, IvarToPopertyMap, InterfaceD,
                                 /*MissingDeclaration*/ true);
    // If there are no invalidation methods, there is no ivar validation work
    // to be done.
    return;
  }

  // Only check if Ivars are invalidated when InstanceVariableInvalidation
  // has been requested.
  if (!Filter.check_InstanceVariableInvalidation)
    return;

  // Check that all ivars are invalidated by the invalidation methods.
  bool AtImplementationContainsAtLeastOneInvalidationMethod = false;
  for (const ObjCMethodDecl *InterfD : Info.InvalidationMethods) {
    // Get the corresponding method in the @implementation.
    const ObjCMethodDecl *D = ImplD->getMethod(InterfD->getSelector(),
                                               InterfD->isInstanceMethod());
    if (D && D->hasBody()) {
      AtImplementationContainsAtLeastOneInvalidationMethod = true;

      // Get a copy of ivars needing invalidation.
      IvarSet IvarsI = Ivars;

      bool CalledAnotherInvalidationMethod = false;
      MethodCrawler(IvarsI,
                    CalledAnotherInvalidationMethod,
                    PropSetterToIvarMap,
                    PropGetterToIvarMap,
                    PropertyToIvarMap,
                    BR.getContext()).VisitStmt(D->getBody());
      // If another invalidation method was called, trust that full invalidation
      // has occurred.
      if (CalledAnotherInvalidationMethod)
        continue;

      // Warn on the ivars that were not invalidated by the method.
      for (const ObjCIvarDecl *Ivar : llvm::make_first_range(IvarsI))
        reportIvarNeedsInvalidation(Ivar, IvarToPopertyMap, D);
    }
  }

  // Report an error in case none of the invalidation methods are implemented.
  if (!AtImplementationContainsAtLeastOneInvalidationMethod) {
    if (AtImplementationContainsAtLeastOnePartialInvalidationMethod) {
      // Warn on the ivars that were not invalidated by the prrtial
      // invalidation methods.
      for (const ObjCIvarDecl *Ivar : llvm::make_first_range(Ivars))
        reportIvarNeedsInvalidation(Ivar, IvarToPopertyMap, nullptr);
    } else {
      // Otherwise, no invalidation methods were implemented.
      reportNoInvalidationMethod(Filter.checkName_InstanceVariableInvalidation,
                                 FirstIvarDecl, IvarToPopertyMap, InterfaceD,
                                 /*MissingDeclaration*/ false);
    }
  }
}

void IvarInvalidationCheckerImpl::reportNoInvalidationMethod(
    CheckerNameRef CheckName, const ObjCIvarDecl *FirstIvarDecl,
    const IvarToPropMapTy &IvarToPopertyMap,
    const ObjCInterfaceDecl *InterfaceD, bool MissingDeclaration) const {
  SmallString<128> sbuf;
  llvm::raw_svector_ostream os(sbuf);
  assert(FirstIvarDecl);
  printIvar(os, FirstIvarDecl, IvarToPopertyMap);
  os << "needs to be invalidated; ";
  if (MissingDeclaration)
    os << "no invalidation method is declared for ";
  else
    os << "no invalidation method is defined in the @implementation for ";
  os << InterfaceD->getName();

  PathDiagnosticLocation IvarDecLocation =
    PathDiagnosticLocation::createBegin(FirstIvarDecl, BR.getSourceManager());

  BR.EmitBasicReport(FirstIvarDecl, CheckName, "Incomplete invalidation",
                     categories::CoreFoundationObjectiveC, os.str(),
                     IvarDecLocation);
}

void IvarInvalidationCheckerImpl::
reportIvarNeedsInvalidation(const ObjCIvarDecl *IvarD,
                            const IvarToPropMapTy &IvarToPopertyMap,
                            const ObjCMethodDecl *MethodD) const {
  SmallString<128> sbuf;
  llvm::raw_svector_ostream os(sbuf);
  printIvar(os, IvarD, IvarToPopertyMap);
  os << "needs to be invalidated or set to nil";
  if (MethodD) {
    PathDiagnosticLocation MethodDecLocation =
                           PathDiagnosticLocation::createEnd(MethodD->getBody(),
                           BR.getSourceManager(),
                           Mgr.getAnalysisDeclContext(MethodD));
    BR.EmitBasicReport(MethodD, Filter.checkName_InstanceVariableInvalidation,
                       "Incomplete invalidation",
                       categories::CoreFoundationObjectiveC, os.str(),
                       MethodDecLocation);
  } else {
    BR.EmitBasicReport(
        IvarD, Filter.checkName_InstanceVariableInvalidation,
        "Incomplete invalidation", categories::CoreFoundationObjectiveC,
        os.str(),
        PathDiagnosticLocation::createBegin(IvarD, BR.getSourceManager()));
  }
}

void IvarInvalidationCheckerImpl::MethodCrawler::markInvalidated(
    const ObjCIvarDecl *Iv) {
  IvarSet::iterator I = IVars.find(Iv);
  if (I != IVars.end()) {
    // If InvalidationMethod is present, we are processing the message send and
    // should ensure we are invalidating with the appropriate method,
    // otherwise, we are processing setting to 'nil'.
    if (!InvalidationMethod || I->second.hasMethod(InvalidationMethod))
      IVars.erase(I);
  }
}

const Expr *IvarInvalidationCheckerImpl::MethodCrawler::peel(const Expr *E) const {
  E = E->IgnoreParenCasts();
  if (const PseudoObjectExpr *POE = dyn_cast<PseudoObjectExpr>(E))
    E = POE->getSyntacticForm()->IgnoreParenCasts();
  if (const OpaqueValueExpr *OVE = dyn_cast<OpaqueValueExpr>(E))
    E = OVE->getSourceExpr()->IgnoreParenCasts();
  return E;
}

void IvarInvalidationCheckerImpl::MethodCrawler::checkObjCIvarRefExpr(
    const ObjCIvarRefExpr *IvarRef) {
  if (const Decl *D = IvarRef->getDecl())
    markInvalidated(cast<ObjCIvarDecl>(D->getCanonicalDecl()));
}

void IvarInvalidationCheckerImpl::MethodCrawler::checkObjCMessageExpr(
    const ObjCMessageExpr *ME) {
  const ObjCMethodDecl *MD = ME->getMethodDecl();
  if (MD) {
    MD = MD->getCanonicalDecl();
    MethToIvarMapTy::const_iterator IvI = PropertyGetterToIvarMap.find(MD);
    if (IvI != PropertyGetterToIvarMap.end())
      markInvalidated(IvI->second);
  }
}

void IvarInvalidationCheckerImpl::MethodCrawler::checkObjCPropertyRefExpr(
    const ObjCPropertyRefExpr *PA) {

  if (PA->isExplicitProperty()) {
    const ObjCPropertyDecl *PD = PA->getExplicitProperty();
    if (PD) {
      PD = cast<ObjCPropertyDecl>(PD->getCanonicalDecl());
      PropToIvarMapTy::const_iterator IvI = PropertyToIvarMap.find(PD);
      if (IvI != PropertyToIvarMap.end())
        markInvalidated(IvI->second);
      return;
    }
  }

  if (PA->isImplicitProperty()) {
    const ObjCMethodDecl *MD = PA->getImplicitPropertySetter();
    if (MD) {
      MD = MD->getCanonicalDecl();
      MethToIvarMapTy::const_iterator IvI =PropertyGetterToIvarMap.find(MD);
      if (IvI != PropertyGetterToIvarMap.end())
        markInvalidated(IvI->second);
      return;
    }
  }
}

bool IvarInvalidationCheckerImpl::MethodCrawler::isZero(const Expr *E) const {
  E = peel(E);

  return (E->isNullPointerConstant(Ctx, Expr::NPC_ValueDependentIsNotNull)
           != Expr::NPCK_NotNull);
}

void IvarInvalidationCheckerImpl::MethodCrawler::check(const Expr *E) {
  E = peel(E);

  if (const ObjCIvarRefExpr *IvarRef = dyn_cast<ObjCIvarRefExpr>(E)) {
    checkObjCIvarRefExpr(IvarRef);
    return;
  }

  if (const ObjCPropertyRefExpr *PropRef = dyn_cast<ObjCPropertyRefExpr>(E)) {
    checkObjCPropertyRefExpr(PropRef);
    return;
  }

  if (const ObjCMessageExpr *MsgExpr = dyn_cast<ObjCMessageExpr>(E)) {
    checkObjCMessageExpr(MsgExpr);
    return;
  }
}

void IvarInvalidationCheckerImpl::MethodCrawler::VisitBinaryOperator(
    const BinaryOperator *BO) {
  VisitStmt(BO);

  // Do we assign/compare against zero? If yes, check the variable we are
  // assigning to.
  BinaryOperatorKind Opcode = BO->getOpcode();
  if (Opcode != BO_Assign &&
      Opcode != BO_EQ &&
      Opcode != BO_NE)
    return;

  if (isZero(BO->getRHS())) {
      check(BO->getLHS());
      return;
  }

  if (Opcode != BO_Assign && isZero(BO->getLHS())) {
    check(BO->getRHS());
    return;
  }
}

void IvarInvalidationCheckerImpl::MethodCrawler::VisitObjCMessageExpr(
  const ObjCMessageExpr *ME) {
  const ObjCMethodDecl *MD = ME->getMethodDecl();
  const Expr *Receiver = ME->getInstanceReceiver();

  // Stop if we are calling '[self invalidate]'.
  if (Receiver && isInvalidationMethod(MD, /*LookForPartial*/ false))
    if (Receiver->isObjCSelfExpr()) {
      CalledAnotherInvalidationMethod = true;
      return;
    }

  // Check if we call a setter and set the property to 'nil'.
  if (MD && (ME->getNumArgs() == 1) && isZero(ME->getArg(0))) {
    MD = MD->getCanonicalDecl();
    MethToIvarMapTy::const_iterator IvI = PropertySetterToIvarMap.find(MD);
    if (IvI != PropertySetterToIvarMap.end()) {
      markInvalidated(IvI->second);
      return;
    }
  }

  // Check if we call the 'invalidation' routine on the ivar.
  if (Receiver) {
    InvalidationMethod = MD;
    check(Receiver->IgnoreParenCasts());
    InvalidationMethod = nullptr;
  }

  VisitStmt(ME);
}
} // end anonymous namespace

// Register the checkers.
namespace {
class IvarInvalidationChecker :
  public Checker<check::ASTDecl<ObjCImplementationDecl> > {
public:
  ChecksFilter Filter;
public:
  void checkASTDecl(const ObjCImplementationDecl *D, AnalysisManager& Mgr,
                    BugReporter &BR) const {
    IvarInvalidationCheckerImpl Walker(Mgr, BR, Filter);
    Walker.visit(D);
  }
};
} // end anonymous namespace

void ento::registerIvarInvalidationModeling(CheckerManager &mgr) {
  mgr.registerChecker<IvarInvalidationChecker>();
}

bool ento::shouldRegisterIvarInvalidationModeling(const CheckerManager &mgr) {
  return true;
}

#define REGISTER_CHECKER(name)                                                 \
  void ento::register##name(CheckerManager &mgr) {                             \
    IvarInvalidationChecker *checker =                                         \
        mgr.getChecker<IvarInvalidationChecker>();                             \
    checker->Filter.check_##name = true;                                       \
    checker->Filter.checkName_##name = mgr.getCurrentCheckerName();            \
  }                                                                            \
                                                                               \
  bool ento::shouldRegister##name(const CheckerManager &mgr) { return true; }

REGISTER_CHECKER(InstanceVariableInvalidation)
REGISTER_CHECKER(MissingInvalidationMethod)
