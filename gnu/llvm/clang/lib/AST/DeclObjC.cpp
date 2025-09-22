//===- DeclObjC.cpp - ObjC Declaration AST Node Implementation ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the Objective-C related Decl classes.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/DeclObjC.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTMutationListener.h"
#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/ODRHash.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/Type.h"
#include "clang/AST/TypeLoc.h"
#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/SourceLocation.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <queue>
#include <utility>

using namespace clang;

//===----------------------------------------------------------------------===//
// ObjCListBase
//===----------------------------------------------------------------------===//

void ObjCListBase::set(void *const* InList, unsigned Elts, ASTContext &Ctx) {
  List = nullptr;
  if (Elts == 0) return;  // Setting to an empty list is a noop.

  List = new (Ctx) void*[Elts];
  NumElts = Elts;
  memcpy(List, InList, sizeof(void*)*Elts);
}

void ObjCProtocolList::set(ObjCProtocolDecl* const* InList, unsigned Elts,
                           const SourceLocation *Locs, ASTContext &Ctx) {
  if (Elts == 0)
    return;

  Locations = new (Ctx) SourceLocation[Elts];
  memcpy(Locations, Locs, sizeof(SourceLocation) * Elts);
  set(InList, Elts, Ctx);
}

//===----------------------------------------------------------------------===//
// ObjCInterfaceDecl
//===----------------------------------------------------------------------===//

ObjCContainerDecl::ObjCContainerDecl(Kind DK, DeclContext *DC,
                                     const IdentifierInfo *Id,
                                     SourceLocation nameLoc,
                                     SourceLocation atStartLoc)
    : NamedDecl(DK, DC, nameLoc, Id), DeclContext(DK) {
  setAtStartLoc(atStartLoc);
}

void ObjCContainerDecl::anchor() {}

/// getIvarDecl - This method looks up an ivar in this ContextDecl.
///
ObjCIvarDecl *
ObjCContainerDecl::getIvarDecl(IdentifierInfo *Id) const {
  lookup_result R = lookup(Id);
  for (lookup_iterator Ivar = R.begin(), IvarEnd = R.end();
       Ivar != IvarEnd; ++Ivar) {
    if (auto *ivar = dyn_cast<ObjCIvarDecl>(*Ivar))
      return ivar;
  }
  return nullptr;
}

// Get the local instance/class method declared in this interface.
ObjCMethodDecl *
ObjCContainerDecl::getMethod(Selector Sel, bool isInstance,
                             bool AllowHidden) const {
  // If this context is a hidden protocol definition, don't find any
  // methods there.
  if (const auto *Proto = dyn_cast<ObjCProtocolDecl>(this)) {
    if (const ObjCProtocolDecl *Def = Proto->getDefinition())
      if (!Def->isUnconditionallyVisible() && !AllowHidden)
        return nullptr;
  }

  // Since instance & class methods can have the same name, the loop below
  // ensures we get the correct method.
  //
  // @interface Whatever
  // - (int) class_method;
  // + (float) class_method;
  // @end
  lookup_result R = lookup(Sel);
  for (lookup_iterator Meth = R.begin(), MethEnd = R.end();
       Meth != MethEnd; ++Meth) {
    auto *MD = dyn_cast<ObjCMethodDecl>(*Meth);
    if (MD && MD->isInstanceMethod() == isInstance)
      return MD;
  }
  return nullptr;
}

/// This routine returns 'true' if a user declared setter method was
/// found in the class, its protocols, its super classes or categories.
/// It also returns 'true' if one of its categories has declared a 'readwrite'
/// property.  This is because, user must provide a setter method for the
/// category's 'readwrite' property.
bool ObjCContainerDecl::HasUserDeclaredSetterMethod(
    const ObjCPropertyDecl *Property) const {
  Selector Sel = Property->getSetterName();
  lookup_result R = lookup(Sel);
  for (lookup_iterator Meth = R.begin(), MethEnd = R.end();
       Meth != MethEnd; ++Meth) {
    auto *MD = dyn_cast<ObjCMethodDecl>(*Meth);
    if (MD && MD->isInstanceMethod() && !MD->isImplicit())
      return true;
  }

  if (const auto *ID = dyn_cast<ObjCInterfaceDecl>(this)) {
    // Also look into categories, including class extensions, looking
    // for a user declared instance method.
    for (const auto *Cat : ID->visible_categories()) {
      if (ObjCMethodDecl *MD = Cat->getInstanceMethod(Sel))
        if (!MD->isImplicit())
          return true;
      if (Cat->IsClassExtension())
        continue;
      // Also search through the categories looking for a 'readwrite'
      // declaration of this property. If one found, presumably a setter will
      // be provided (properties declared in categories will not get
      // auto-synthesized).
      for (const auto *P : Cat->properties())
        if (P->getIdentifier() == Property->getIdentifier()) {
          if (P->getPropertyAttributes() &
              ObjCPropertyAttribute::kind_readwrite)
            return true;
          break;
        }
    }

    // Also look into protocols, for a user declared instance method.
    for (const auto *Proto : ID->all_referenced_protocols())
      if (Proto->HasUserDeclaredSetterMethod(Property))
        return true;

    // And in its super class.
    ObjCInterfaceDecl *OSC = ID->getSuperClass();
    while (OSC) {
      if (OSC->HasUserDeclaredSetterMethod(Property))
        return true;
      OSC = OSC->getSuperClass();
    }
  }
  if (const auto *PD = dyn_cast<ObjCProtocolDecl>(this))
    for (const auto *PI : PD->protocols())
      if (PI->HasUserDeclaredSetterMethod(Property))
        return true;
  return false;
}

ObjCPropertyDecl *
ObjCPropertyDecl::findPropertyDecl(const DeclContext *DC,
                                   const IdentifierInfo *propertyID,
                                   ObjCPropertyQueryKind queryKind) {
  // If this context is a hidden protocol definition, don't find any
  // property.
  if (const auto *Proto = dyn_cast<ObjCProtocolDecl>(DC)) {
    if (const ObjCProtocolDecl *Def = Proto->getDefinition())
      if (!Def->isUnconditionallyVisible())
        return nullptr;
  }

  // If context is class, then lookup property in its visible extensions.
  // This comes before property is looked up in primary class.
  if (auto *IDecl = dyn_cast<ObjCInterfaceDecl>(DC)) {
    for (const auto *Ext : IDecl->visible_extensions())
      if (ObjCPropertyDecl *PD = ObjCPropertyDecl::findPropertyDecl(Ext,
                                                       propertyID,
                                                       queryKind))
        return PD;
  }

  DeclContext::lookup_result R = DC->lookup(propertyID);
  ObjCPropertyDecl *classProp = nullptr;
  for (DeclContext::lookup_iterator I = R.begin(), E = R.end(); I != E;
       ++I)
    if (auto *PD = dyn_cast<ObjCPropertyDecl>(*I)) {
      // If queryKind is unknown, we return the instance property if one
      // exists; otherwise we return the class property.
      if ((queryKind == ObjCPropertyQueryKind::OBJC_PR_query_unknown &&
           !PD->isClassProperty()) ||
          (queryKind == ObjCPropertyQueryKind::OBJC_PR_query_class &&
           PD->isClassProperty()) ||
          (queryKind == ObjCPropertyQueryKind::OBJC_PR_query_instance &&
           !PD->isClassProperty()))
        return PD;

      if (PD->isClassProperty())
        classProp = PD;
    }

  if (queryKind == ObjCPropertyQueryKind::OBJC_PR_query_unknown)
    // We can't find the instance property, return the class property.
    return classProp;

  return nullptr;
}

IdentifierInfo *
ObjCPropertyDecl::getDefaultSynthIvarName(ASTContext &Ctx) const {
  SmallString<128> ivarName;
  {
    llvm::raw_svector_ostream os(ivarName);
    os << '_' << getIdentifier()->getName();
  }
  return &Ctx.Idents.get(ivarName.str());
}

ObjCPropertyDecl *ObjCContainerDecl::getProperty(const IdentifierInfo *Id,
                                                 bool IsInstance) const {
  for (auto *LookupResult : lookup(Id)) {
    if (auto *Prop = dyn_cast<ObjCPropertyDecl>(LookupResult)) {
      if (Prop->isInstanceProperty() == IsInstance) {
        return Prop;
      }
    }
  }
  return nullptr;
}

/// FindPropertyDeclaration - Finds declaration of the property given its name
/// in 'PropertyId' and returns it. It returns 0, if not found.
ObjCPropertyDecl *ObjCContainerDecl::FindPropertyDeclaration(
    const IdentifierInfo *PropertyId,
    ObjCPropertyQueryKind QueryKind) const {
  // Don't find properties within hidden protocol definitions.
  if (const auto *Proto = dyn_cast<ObjCProtocolDecl>(this)) {
    if (const ObjCProtocolDecl *Def = Proto->getDefinition())
      if (!Def->isUnconditionallyVisible())
        return nullptr;
  }

  // Search the extensions of a class first; they override what's in
  // the class itself.
  if (const auto *ClassDecl = dyn_cast<ObjCInterfaceDecl>(this)) {
    for (const auto *Ext : ClassDecl->visible_extensions()) {
      if (auto *P = Ext->FindPropertyDeclaration(PropertyId, QueryKind))
        return P;
    }
  }

  if (ObjCPropertyDecl *PD =
        ObjCPropertyDecl::findPropertyDecl(cast<DeclContext>(this), PropertyId,
                                           QueryKind))
    return PD;

  switch (getKind()) {
    default:
      break;
    case Decl::ObjCProtocol: {
      const auto *PID = cast<ObjCProtocolDecl>(this);
      for (const auto *I : PID->protocols())
        if (ObjCPropertyDecl *P = I->FindPropertyDeclaration(PropertyId,
                                                             QueryKind))
          return P;
      break;
    }
    case Decl::ObjCInterface: {
      const auto *OID = cast<ObjCInterfaceDecl>(this);
      // Look through categories (but not extensions; they were handled above).
      for (const auto *Cat : OID->visible_categories()) {
        if (!Cat->IsClassExtension())
          if (ObjCPropertyDecl *P = Cat->FindPropertyDeclaration(
                                             PropertyId, QueryKind))
            return P;
      }

      // Look through protocols.
      for (const auto *I : OID->all_referenced_protocols())
        if (ObjCPropertyDecl *P = I->FindPropertyDeclaration(PropertyId,
                                                             QueryKind))
          return P;

      // Finally, check the super class.
      if (const ObjCInterfaceDecl *superClass = OID->getSuperClass())
        return superClass->FindPropertyDeclaration(PropertyId, QueryKind);
      break;
    }
    case Decl::ObjCCategory: {
      const auto *OCD = cast<ObjCCategoryDecl>(this);
      // Look through protocols.
      if (!OCD->IsClassExtension())
        for (const auto *I : OCD->protocols())
          if (ObjCPropertyDecl *P = I->FindPropertyDeclaration(PropertyId,
                                                               QueryKind))
            return P;
      break;
    }
  }
  return nullptr;
}

void ObjCInterfaceDecl::anchor() {}

ObjCTypeParamList *ObjCInterfaceDecl::getTypeParamList() const {
  // If this particular declaration has a type parameter list, return it.
  if (ObjCTypeParamList *written = getTypeParamListAsWritten())
    return written;

  // If there is a definition, return its type parameter list.
  if (const ObjCInterfaceDecl *def = getDefinition())
    return def->getTypeParamListAsWritten();

  // Otherwise, look at previous declarations to determine whether any
  // of them has a type parameter list, skipping over those
  // declarations that do not.
  for (const ObjCInterfaceDecl *decl = getMostRecentDecl(); decl;
       decl = decl->getPreviousDecl()) {
    if (ObjCTypeParamList *written = decl->getTypeParamListAsWritten())
      return written;
  }

  return nullptr;
}

void ObjCInterfaceDecl::setTypeParamList(ObjCTypeParamList *TPL) {
  TypeParamList = TPL;
  if (!TPL)
    return;
  // Set the declaration context of each of the type parameters.
  for (auto *typeParam : *TypeParamList)
    typeParam->setDeclContext(this);
}

ObjCInterfaceDecl *ObjCInterfaceDecl::getSuperClass() const {
  // FIXME: Should make sure no callers ever do this.
  if (!hasDefinition())
    return nullptr;

  if (data().ExternallyCompleted)
    LoadExternalDefinition();

  if (const ObjCObjectType *superType = getSuperClassType()) {
    if (ObjCInterfaceDecl *superDecl = superType->getInterface()) {
      if (ObjCInterfaceDecl *superDef = superDecl->getDefinition())
        return superDef;

      return superDecl;
    }
  }

  return nullptr;
}

SourceLocation ObjCInterfaceDecl::getSuperClassLoc() const {
  if (TypeSourceInfo *superTInfo = getSuperClassTInfo())
    return superTInfo->getTypeLoc().getBeginLoc();

  return SourceLocation();
}

/// FindPropertyVisibleInPrimaryClass - Finds declaration of the property
/// with name 'PropertyId' in the primary class; including those in protocols
/// (direct or indirect) used by the primary class.
ObjCPropertyDecl *ObjCInterfaceDecl::FindPropertyVisibleInPrimaryClass(
    const IdentifierInfo *PropertyId, ObjCPropertyQueryKind QueryKind) const {
  // FIXME: Should make sure no callers ever do this.
  if (!hasDefinition())
    return nullptr;

  if (data().ExternallyCompleted)
    LoadExternalDefinition();

  if (ObjCPropertyDecl *PD =
      ObjCPropertyDecl::findPropertyDecl(cast<DeclContext>(this), PropertyId,
                                         QueryKind))
    return PD;

  // Look through protocols.
  for (const auto *I : all_referenced_protocols())
    if (ObjCPropertyDecl *P = I->FindPropertyDeclaration(PropertyId,
                                                         QueryKind))
      return P;

  return nullptr;
}

void ObjCInterfaceDecl::collectPropertiesToImplement(PropertyMap &PM) const {
  for (auto *Prop : properties()) {
    PM[std::make_pair(Prop->getIdentifier(), Prop->isClassProperty())] = Prop;
  }
  for (const auto *Ext : known_extensions()) {
    const ObjCCategoryDecl *ClassExt = Ext;
    for (auto *Prop : ClassExt->properties()) {
      PM[std::make_pair(Prop->getIdentifier(), Prop->isClassProperty())] = Prop;
    }
  }
  for (const auto *PI : all_referenced_protocols())
    PI->collectPropertiesToImplement(PM);
  // Note, the properties declared only in class extensions are still copied
  // into the main @interface's property list, and therefore we don't
  // explicitly, have to search class extension properties.
}

bool ObjCInterfaceDecl::isArcWeakrefUnavailable() const {
  const ObjCInterfaceDecl *Class = this;
  while (Class) {
    if (Class->hasAttr<ArcWeakrefUnavailableAttr>())
      return true;
    Class = Class->getSuperClass();
  }
  return false;
}

const ObjCInterfaceDecl *ObjCInterfaceDecl::isObjCRequiresPropertyDefs() const {
  const ObjCInterfaceDecl *Class = this;
  while (Class) {
    if (Class->hasAttr<ObjCRequiresPropertyDefsAttr>())
      return Class;
    Class = Class->getSuperClass();
  }
  return nullptr;
}

void ObjCInterfaceDecl::mergeClassExtensionProtocolList(
                              ObjCProtocolDecl *const* ExtList, unsigned ExtNum,
                              ASTContext &C) {
  if (data().ExternallyCompleted)
    LoadExternalDefinition();

  if (data().AllReferencedProtocols.empty() &&
      data().ReferencedProtocols.empty()) {
    data().AllReferencedProtocols.set(ExtList, ExtNum, C);
    return;
  }

  // Check for duplicate protocol in class's protocol list.
  // This is O(n*m). But it is extremely rare and number of protocols in
  // class or its extension are very few.
  SmallVector<ObjCProtocolDecl *, 8> ProtocolRefs;
  for (unsigned i = 0; i < ExtNum; i++) {
    bool protocolExists = false;
    ObjCProtocolDecl *ProtoInExtension = ExtList[i];
    for (auto *Proto : all_referenced_protocols()) {
      if (C.ProtocolCompatibleWithProtocol(ProtoInExtension, Proto)) {
        protocolExists = true;
        break;
      }
    }
    // Do we want to warn on a protocol in extension class which
    // already exist in the class? Probably not.
    if (!protocolExists)
      ProtocolRefs.push_back(ProtoInExtension);
  }

  if (ProtocolRefs.empty())
    return;

  // Merge ProtocolRefs into class's protocol list;
  ProtocolRefs.append(all_referenced_protocol_begin(),
                      all_referenced_protocol_end());

  data().AllReferencedProtocols.set(ProtocolRefs.data(), ProtocolRefs.size(),C);
}

const ObjCInterfaceDecl *
ObjCInterfaceDecl::findInterfaceWithDesignatedInitializers() const {
  const ObjCInterfaceDecl *IFace = this;
  while (IFace) {
    if (IFace->hasDesignatedInitializers())
      return IFace;
    if (!IFace->inheritsDesignatedInitializers())
      break;
    IFace = IFace->getSuperClass();
  }
  return nullptr;
}

static bool isIntroducingInitializers(const ObjCInterfaceDecl *D) {
  for (const auto *MD : D->instance_methods()) {
    if (MD->getMethodFamily() == OMF_init && !MD->isOverriding())
      return true;
  }
  for (const auto *Ext : D->visible_extensions()) {
    for (const auto *MD : Ext->instance_methods()) {
      if (MD->getMethodFamily() == OMF_init && !MD->isOverriding())
        return true;
    }
  }
  if (const auto *ImplD = D->getImplementation()) {
    for (const auto *MD : ImplD->instance_methods()) {
      if (MD->getMethodFamily() == OMF_init && !MD->isOverriding())
        return true;
    }
  }
  return false;
}

bool ObjCInterfaceDecl::inheritsDesignatedInitializers() const {
  switch (data().InheritedDesignatedInitializers) {
  case DefinitionData::IDI_Inherited:
    return true;
  case DefinitionData::IDI_NotInherited:
    return false;
  case DefinitionData::IDI_Unknown:
    // If the class introduced initializers we conservatively assume that we
    // don't know if any of them is a designated initializer to avoid possible
    // misleading warnings.
    if (isIntroducingInitializers(this)) {
      data().InheritedDesignatedInitializers = DefinitionData::IDI_NotInherited;
    } else {
      if (auto SuperD = getSuperClass()) {
        data().InheritedDesignatedInitializers =
          SuperD->declaresOrInheritsDesignatedInitializers() ?
            DefinitionData::IDI_Inherited :
            DefinitionData::IDI_NotInherited;
      } else {
        data().InheritedDesignatedInitializers =
          DefinitionData::IDI_NotInherited;
      }
    }
    assert(data().InheritedDesignatedInitializers
             != DefinitionData::IDI_Unknown);
    return data().InheritedDesignatedInitializers ==
        DefinitionData::IDI_Inherited;
  }

  llvm_unreachable("unexpected InheritedDesignatedInitializers value");
}

void ObjCInterfaceDecl::getDesignatedInitializers(
    llvm::SmallVectorImpl<const ObjCMethodDecl *> &Methods) const {
  // Check for a complete definition and recover if not so.
  if (!isThisDeclarationADefinition())
    return;
  if (data().ExternallyCompleted)
    LoadExternalDefinition();

  const ObjCInterfaceDecl *IFace= findInterfaceWithDesignatedInitializers();
  if (!IFace)
    return;

  for (const auto *MD : IFace->instance_methods())
    if (MD->isThisDeclarationADesignatedInitializer())
      Methods.push_back(MD);
  for (const auto *Ext : IFace->visible_extensions()) {
    for (const auto *MD : Ext->instance_methods())
      if (MD->isThisDeclarationADesignatedInitializer())
        Methods.push_back(MD);
  }
}

bool ObjCInterfaceDecl::isDesignatedInitializer(Selector Sel,
                                      const ObjCMethodDecl **InitMethod) const {
  bool HasCompleteDef = isThisDeclarationADefinition();
  // During deserialization the data record for the ObjCInterfaceDecl could
  // be made invariant by reusing the canonical decl. Take this into account
  // when checking for the complete definition.
  if (!HasCompleteDef && getCanonicalDecl()->hasDefinition() &&
      getCanonicalDecl()->getDefinition() == getDefinition())
    HasCompleteDef = true;

  // Check for a complete definition and recover if not so.
  if (!HasCompleteDef)
    return false;

  if (data().ExternallyCompleted)
    LoadExternalDefinition();

  const ObjCInterfaceDecl *IFace= findInterfaceWithDesignatedInitializers();
  if (!IFace)
    return false;

  if (const ObjCMethodDecl *MD = IFace->getInstanceMethod(Sel)) {
    if (MD->isThisDeclarationADesignatedInitializer()) {
      if (InitMethod)
        *InitMethod = MD;
      return true;
    }
  }
  for (const auto *Ext : IFace->visible_extensions()) {
    if (const ObjCMethodDecl *MD = Ext->getInstanceMethod(Sel)) {
      if (MD->isThisDeclarationADesignatedInitializer()) {
        if (InitMethod)
          *InitMethod = MD;
        return true;
      }
    }
  }
  return false;
}

void ObjCInterfaceDecl::allocateDefinitionData() {
  assert(!hasDefinition() && "ObjC class already has a definition");
  Data.setPointer(new (getASTContext()) DefinitionData());
  Data.getPointer()->Definition = this;
}

void ObjCInterfaceDecl::startDefinition() {
  allocateDefinitionData();

  // Update all of the declarations with a pointer to the definition.
  for (auto *RD : redecls()) {
    if (RD != this)
      RD->Data = Data;
  }
}

void ObjCInterfaceDecl::startDuplicateDefinitionForComparison() {
  Data.setPointer(nullptr);
  allocateDefinitionData();
  // Don't propagate data to other redeclarations.
}

void ObjCInterfaceDecl::mergeDuplicateDefinitionWithCommon(
    const ObjCInterfaceDecl *Definition) {
  Data = Definition->Data;
}

ObjCIvarDecl *ObjCInterfaceDecl::lookupInstanceVariable(IdentifierInfo *ID,
                                              ObjCInterfaceDecl *&clsDeclared) {
  // FIXME: Should make sure no callers ever do this.
  if (!hasDefinition())
    return nullptr;

  if (data().ExternallyCompleted)
    LoadExternalDefinition();

  ObjCInterfaceDecl* ClassDecl = this;
  while (ClassDecl != nullptr) {
    if (ObjCIvarDecl *I = ClassDecl->getIvarDecl(ID)) {
      clsDeclared = ClassDecl;
      return I;
    }

    for (const auto *Ext : ClassDecl->visible_extensions()) {
      if (ObjCIvarDecl *I = Ext->getIvarDecl(ID)) {
        clsDeclared = ClassDecl;
        return I;
      }
    }

    ClassDecl = ClassDecl->getSuperClass();
  }
  return nullptr;
}

/// lookupInheritedClass - This method returns ObjCInterfaceDecl * of the super
/// class whose name is passed as argument. If it is not one of the super classes
/// the it returns NULL.
ObjCInterfaceDecl *ObjCInterfaceDecl::lookupInheritedClass(
                                        const IdentifierInfo*ICName) {
  // FIXME: Should make sure no callers ever do this.
  if (!hasDefinition())
    return nullptr;

  if (data().ExternallyCompleted)
    LoadExternalDefinition();

  ObjCInterfaceDecl* ClassDecl = this;
  while (ClassDecl != nullptr) {
    if (ClassDecl->getIdentifier() == ICName)
      return ClassDecl;
    ClassDecl = ClassDecl->getSuperClass();
  }
  return nullptr;
}

ObjCProtocolDecl *
ObjCInterfaceDecl::lookupNestedProtocol(IdentifierInfo *Name) {
  for (auto *P : all_referenced_protocols())
    if (P->lookupProtocolNamed(Name))
      return P;
  ObjCInterfaceDecl *SuperClass = getSuperClass();
  return SuperClass ? SuperClass->lookupNestedProtocol(Name) : nullptr;
}

/// lookupMethod - This method returns an instance/class method by looking in
/// the class, its categories, and its super classes (using a linear search).
/// When argument category "C" is specified, any implicit method found
/// in this category is ignored.
ObjCMethodDecl *ObjCInterfaceDecl::lookupMethod(Selector Sel,
                                                bool isInstance,
                                                bool shallowCategoryLookup,
                                                bool followSuper,
                                                const ObjCCategoryDecl *C) const
{
  // FIXME: Should make sure no callers ever do this.
  if (!hasDefinition())
    return nullptr;

  const ObjCInterfaceDecl* ClassDecl = this;
  ObjCMethodDecl *MethodDecl = nullptr;

  if (data().ExternallyCompleted)
    LoadExternalDefinition();

  while (ClassDecl) {
    // 1. Look through primary class.
    if ((MethodDecl = ClassDecl->getMethod(Sel, isInstance)))
      return MethodDecl;

    // 2. Didn't find one yet - now look through categories.
    for (const auto *Cat : ClassDecl->visible_categories())
      if ((MethodDecl = Cat->getMethod(Sel, isInstance)))
        if (C != Cat || !MethodDecl->isImplicit())
          return MethodDecl;

    // 3. Didn't find one yet - look through primary class's protocols.
    for (const auto *I : ClassDecl->protocols())
      if ((MethodDecl = I->lookupMethod(Sel, isInstance)))
        return MethodDecl;

    // 4. Didn't find one yet - now look through categories' protocols
    if (!shallowCategoryLookup)
      for (const auto *Cat : ClassDecl->visible_categories()) {
        // Didn't find one yet - look through protocols.
        const ObjCList<ObjCProtocolDecl> &Protocols =
          Cat->getReferencedProtocols();
        for (auto *Protocol : Protocols)
          if ((MethodDecl = Protocol->lookupMethod(Sel, isInstance)))
            if (C != Cat || !MethodDecl->isImplicit())
              return MethodDecl;
      }


    if (!followSuper)
      return nullptr;

    // 5. Get to the super class (if any).
    ClassDecl = ClassDecl->getSuperClass();
  }
  return nullptr;
}

// Will search "local" class/category implementations for a method decl.
// If failed, then we search in class's root for an instance method.
// Returns 0 if no method is found.
ObjCMethodDecl *ObjCInterfaceDecl::lookupPrivateMethod(
                                   const Selector &Sel,
                                   bool Instance) const {
  // FIXME: Should make sure no callers ever do this.
  if (!hasDefinition())
    return nullptr;

  if (data().ExternallyCompleted)
    LoadExternalDefinition();

  ObjCMethodDecl *Method = nullptr;
  if (ObjCImplementationDecl *ImpDecl = getImplementation())
    Method = Instance ? ImpDecl->getInstanceMethod(Sel)
                      : ImpDecl->getClassMethod(Sel);

  // Look through local category implementations associated with the class.
  if (!Method)
    Method = getCategoryMethod(Sel, Instance);

  // Before we give up, check if the selector is an instance method.
  // But only in the root. This matches gcc's behavior and what the
  // runtime expects.
  if (!Instance && !Method && !getSuperClass()) {
    Method = lookupInstanceMethod(Sel);
    // Look through local category implementations associated
    // with the root class.
    if (!Method)
      Method = lookupPrivateMethod(Sel, true);
  }

  if (!Method && getSuperClass())
    return getSuperClass()->lookupPrivateMethod(Sel, Instance);
  return Method;
}

unsigned ObjCInterfaceDecl::getODRHash() {
  assert(hasDefinition() && "ODRHash only for records with definitions");

  // Previously calculated hash is stored in DefinitionData.
  if (hasODRHash())
    return data().ODRHash;

  // Only calculate hash on first call of getODRHash per record.
  ODRHash Hasher;
  Hasher.AddObjCInterfaceDecl(getDefinition());
  data().ODRHash = Hasher.CalculateHash();
  setHasODRHash(true);

  return data().ODRHash;
}

bool ObjCInterfaceDecl::hasODRHash() const {
  if (!hasDefinition())
    return false;
  return data().HasODRHash;
}

void ObjCInterfaceDecl::setHasODRHash(bool HasHash) {
  assert(hasDefinition() && "Cannot set ODRHash without definition");
  data().HasODRHash = HasHash;
}

//===----------------------------------------------------------------------===//
// ObjCMethodDecl
//===----------------------------------------------------------------------===//

ObjCMethodDecl::ObjCMethodDecl(
    SourceLocation beginLoc, SourceLocation endLoc, Selector SelInfo,
    QualType T, TypeSourceInfo *ReturnTInfo, DeclContext *contextDecl,
    bool isInstance, bool isVariadic, bool isPropertyAccessor,
    bool isSynthesizedAccessorStub, bool isImplicitlyDeclared, bool isDefined,
    ObjCImplementationControl impControl, bool HasRelatedResultType)
    : NamedDecl(ObjCMethod, contextDecl, beginLoc, SelInfo),
      DeclContext(ObjCMethod), MethodDeclType(T), ReturnTInfo(ReturnTInfo),
      DeclEndLoc(endLoc) {

  // Initialized the bits stored in DeclContext.
  ObjCMethodDeclBits.Family =
      static_cast<ObjCMethodFamily>(InvalidObjCMethodFamily);
  setInstanceMethod(isInstance);
  setVariadic(isVariadic);
  setPropertyAccessor(isPropertyAccessor);
  setSynthesizedAccessorStub(isSynthesizedAccessorStub);
  setDefined(isDefined);
  setIsRedeclaration(false);
  setHasRedeclaration(false);
  setDeclImplementation(impControl);
  setObjCDeclQualifier(OBJC_TQ_None);
  setRelatedResultType(HasRelatedResultType);
  setSelLocsKind(SelLoc_StandardNoSpace);
  setOverriding(false);
  setHasSkippedBody(false);

  setImplicit(isImplicitlyDeclared);
}

ObjCMethodDecl *ObjCMethodDecl::Create(
    ASTContext &C, SourceLocation beginLoc, SourceLocation endLoc,
    Selector SelInfo, QualType T, TypeSourceInfo *ReturnTInfo,
    DeclContext *contextDecl, bool isInstance, bool isVariadic,
    bool isPropertyAccessor, bool isSynthesizedAccessorStub,
    bool isImplicitlyDeclared, bool isDefined,
    ObjCImplementationControl impControl, bool HasRelatedResultType) {
  return new (C, contextDecl) ObjCMethodDecl(
      beginLoc, endLoc, SelInfo, T, ReturnTInfo, contextDecl, isInstance,
      isVariadic, isPropertyAccessor, isSynthesizedAccessorStub,
      isImplicitlyDeclared, isDefined, impControl, HasRelatedResultType);
}

ObjCMethodDecl *ObjCMethodDecl::CreateDeserialized(ASTContext &C,
                                                   GlobalDeclID ID) {
  return new (C, ID) ObjCMethodDecl(SourceLocation(), SourceLocation(),
                                    Selector(), QualType(), nullptr, nullptr);
}

bool ObjCMethodDecl::isDirectMethod() const {
  return hasAttr<ObjCDirectAttr>() &&
         !getASTContext().getLangOpts().ObjCDisableDirectMethodsForTesting;
}

bool ObjCMethodDecl::isThisDeclarationADesignatedInitializer() const {
  return getMethodFamily() == OMF_init &&
      hasAttr<ObjCDesignatedInitializerAttr>();
}

bool ObjCMethodDecl::definedInNSObject(const ASTContext &Ctx) const {
  if (const auto *PD = dyn_cast<const ObjCProtocolDecl>(getDeclContext()))
    return PD->getIdentifier() == Ctx.getNSObjectName();
  if (const auto *ID = dyn_cast<const ObjCInterfaceDecl>(getDeclContext()))
    return ID->getIdentifier() == Ctx.getNSObjectName();
  return false;
}

bool ObjCMethodDecl::isDesignatedInitializerForTheInterface(
    const ObjCMethodDecl **InitMethod) const {
  if (getMethodFamily() != OMF_init)
    return false;
  const DeclContext *DC = getDeclContext();
  if (isa<ObjCProtocolDecl>(DC))
    return false;
  if (const ObjCInterfaceDecl *ID = getClassInterface())
    return ID->isDesignatedInitializer(getSelector(), InitMethod);
  return false;
}

bool ObjCMethodDecl::hasParamDestroyedInCallee() const {
  for (auto *param : parameters()) {
    if (param->isDestroyedInCallee())
      return true;
  }
  return false;
}

Stmt *ObjCMethodDecl::getBody() const {
  return Body.get(getASTContext().getExternalSource());
}

void ObjCMethodDecl::setAsRedeclaration(const ObjCMethodDecl *PrevMethod) {
  assert(PrevMethod);
  getASTContext().setObjCMethodRedeclaration(PrevMethod, this);
  setIsRedeclaration(true);
  PrevMethod->setHasRedeclaration(true);
}

void ObjCMethodDecl::setParamsAndSelLocs(ASTContext &C,
                                         ArrayRef<ParmVarDecl*> Params,
                                         ArrayRef<SourceLocation> SelLocs) {
  ParamsAndSelLocs = nullptr;
  NumParams = Params.size();
  if (Params.empty() && SelLocs.empty())
    return;

  static_assert(alignof(ParmVarDecl *) >= alignof(SourceLocation),
                "Alignment not sufficient for SourceLocation");

  unsigned Size = sizeof(ParmVarDecl *) * NumParams +
                  sizeof(SourceLocation) * SelLocs.size();
  ParamsAndSelLocs = C.Allocate(Size);
  std::uninitialized_copy(Params.begin(), Params.end(), getParams());
  std::uninitialized_copy(SelLocs.begin(), SelLocs.end(), getStoredSelLocs());
}

void ObjCMethodDecl::getSelectorLocs(
                               SmallVectorImpl<SourceLocation> &SelLocs) const {
  for (unsigned i = 0, e = getNumSelectorLocs(); i != e; ++i)
    SelLocs.push_back(getSelectorLoc(i));
}

void ObjCMethodDecl::setMethodParams(ASTContext &C,
                                     ArrayRef<ParmVarDecl*> Params,
                                     ArrayRef<SourceLocation> SelLocs) {
  assert((!SelLocs.empty() || isImplicit()) &&
         "No selector locs for non-implicit method");
  if (isImplicit())
    return setParamsAndSelLocs(C, Params, std::nullopt);

  setSelLocsKind(hasStandardSelectorLocs(getSelector(), SelLocs, Params,
                                        DeclEndLoc));
  if (getSelLocsKind() != SelLoc_NonStandard)
    return setParamsAndSelLocs(C, Params, std::nullopt);

  setParamsAndSelLocs(C, Params, SelLocs);
}

/// A definition will return its interface declaration.
/// An interface declaration will return its definition.
/// Otherwise it will return itself.
ObjCMethodDecl *ObjCMethodDecl::getNextRedeclarationImpl() {
  ASTContext &Ctx = getASTContext();
  ObjCMethodDecl *Redecl = nullptr;
  if (hasRedeclaration())
    Redecl = const_cast<ObjCMethodDecl*>(Ctx.getObjCMethodRedeclaration(this));
  if (Redecl)
    return Redecl;

  auto *CtxD = cast<Decl>(getDeclContext());

  if (!CtxD->isInvalidDecl()) {
    if (auto *IFD = dyn_cast<ObjCInterfaceDecl>(CtxD)) {
      if (ObjCImplementationDecl *ImplD = Ctx.getObjCImplementation(IFD))
        if (!ImplD->isInvalidDecl())
          Redecl = ImplD->getMethod(getSelector(), isInstanceMethod());

    } else if (auto *CD = dyn_cast<ObjCCategoryDecl>(CtxD)) {
      if (ObjCCategoryImplDecl *ImplD = Ctx.getObjCImplementation(CD))
        if (!ImplD->isInvalidDecl())
          Redecl = ImplD->getMethod(getSelector(), isInstanceMethod());

    } else if (auto *ImplD = dyn_cast<ObjCImplementationDecl>(CtxD)) {
      if (ObjCInterfaceDecl *IFD = ImplD->getClassInterface())
        if (!IFD->isInvalidDecl())
          Redecl = IFD->getMethod(getSelector(), isInstanceMethod());

    } else if (auto *CImplD = dyn_cast<ObjCCategoryImplDecl>(CtxD)) {
      if (ObjCCategoryDecl *CatD = CImplD->getCategoryDecl())
        if (!CatD->isInvalidDecl())
          Redecl = CatD->getMethod(getSelector(), isInstanceMethod());
    }
  }

  // Ensure that the discovered method redeclaration has a valid declaration
  // context. Used to prevent infinite loops when iterating redeclarations in
  // a partially invalid AST.
  if (Redecl && cast<Decl>(Redecl->getDeclContext())->isInvalidDecl())
    Redecl = nullptr;

  if (!Redecl && isRedeclaration()) {
    // This is the last redeclaration, go back to the first method.
    return cast<ObjCContainerDecl>(CtxD)->getMethod(getSelector(),
                                                    isInstanceMethod(),
                                                    /*AllowHidden=*/true);
  }

  return Redecl ? Redecl : this;
}

ObjCMethodDecl *ObjCMethodDecl::getCanonicalDecl() {
  auto *CtxD = cast<Decl>(getDeclContext());
  const auto &Sel = getSelector();

  if (auto *ImplD = dyn_cast<ObjCImplementationDecl>(CtxD)) {
    if (ObjCInterfaceDecl *IFD = ImplD->getClassInterface()) {
      // When the container is the ObjCImplementationDecl (the primary
      // @implementation), then the canonical Decl is either in
      // the class Interface, or in any of its extension.
      //
      // So when we don't find it in the ObjCInterfaceDecl,
      // sift through extensions too.
      if (ObjCMethodDecl *MD = IFD->getMethod(Sel, isInstanceMethod()))
        return MD;
      for (auto *Ext : IFD->known_extensions())
        if (ObjCMethodDecl *MD = Ext->getMethod(Sel, isInstanceMethod()))
          return MD;
    }
  } else if (auto *CImplD = dyn_cast<ObjCCategoryImplDecl>(CtxD)) {
    if (ObjCCategoryDecl *CatD = CImplD->getCategoryDecl())
      if (ObjCMethodDecl *MD = CatD->getMethod(Sel, isInstanceMethod()))
        return MD;
  }

  if (isRedeclaration()) {
    // It is possible that we have not done deserializing the ObjCMethod yet.
    ObjCMethodDecl *MD =
        cast<ObjCContainerDecl>(CtxD)->getMethod(Sel, isInstanceMethod(),
                                                 /*AllowHidden=*/true);
    return MD ? MD : this;
  }

  return this;
}

SourceLocation ObjCMethodDecl::getEndLoc() const {
  if (Stmt *Body = getBody())
    return Body->getEndLoc();
  return DeclEndLoc;
}

ObjCMethodFamily ObjCMethodDecl::getMethodFamily() const {
  auto family = static_cast<ObjCMethodFamily>(ObjCMethodDeclBits.Family);
  if (family != static_cast<unsigned>(InvalidObjCMethodFamily))
    return family;

  // Check for an explicit attribute.
  if (const ObjCMethodFamilyAttr *attr = getAttr<ObjCMethodFamilyAttr>()) {
    // The unfortunate necessity of mapping between enums here is due
    // to the attributes framework.
    switch (attr->getFamily()) {
    case ObjCMethodFamilyAttr::OMF_None: family = OMF_None; break;
    case ObjCMethodFamilyAttr::OMF_alloc: family = OMF_alloc; break;
    case ObjCMethodFamilyAttr::OMF_copy: family = OMF_copy; break;
    case ObjCMethodFamilyAttr::OMF_init: family = OMF_init; break;
    case ObjCMethodFamilyAttr::OMF_mutableCopy: family = OMF_mutableCopy; break;
    case ObjCMethodFamilyAttr::OMF_new: family = OMF_new; break;
    }
    ObjCMethodDeclBits.Family = family;
    return family;
  }

  family = getSelector().getMethodFamily();
  switch (family) {
  case OMF_None: break;

  // init only has a conventional meaning for an instance method, and
  // it has to return an object.
  case OMF_init:
    if (!isInstanceMethod() || !getReturnType()->isObjCObjectPointerType())
      family = OMF_None;
    break;

  // alloc/copy/new have a conventional meaning for both class and
  // instance methods, but they require an object return.
  case OMF_alloc:
  case OMF_copy:
  case OMF_mutableCopy:
  case OMF_new:
    if (!getReturnType()->isObjCObjectPointerType())
      family = OMF_None;
    break;

  // These selectors have a conventional meaning only for instance methods.
  case OMF_dealloc:
  case OMF_finalize:
  case OMF_retain:
  case OMF_release:
  case OMF_autorelease:
  case OMF_retainCount:
  case OMF_self:
    if (!isInstanceMethod())
      family = OMF_None;
    break;

  case OMF_initialize:
    if (isInstanceMethod() || !getReturnType()->isVoidType())
      family = OMF_None;
    break;

  case OMF_performSelector:
    if (!isInstanceMethod() || !getReturnType()->isObjCIdType())
      family = OMF_None;
    else {
      unsigned noParams = param_size();
      if (noParams < 1 || noParams > 3)
        family = OMF_None;
      else {
        ObjCMethodDecl::param_type_iterator it = param_type_begin();
        QualType ArgT = (*it);
        if (!ArgT->isObjCSelType()) {
          family = OMF_None;
          break;
        }
        while (--noParams) {
          it++;
          ArgT = (*it);
          if (!ArgT->isObjCIdType()) {
            family = OMF_None;
            break;
          }
        }
      }
    }
    break;

  }

  // Cache the result.
  ObjCMethodDeclBits.Family = family;
  return family;
}

QualType ObjCMethodDecl::getSelfType(ASTContext &Context,
                                     const ObjCInterfaceDecl *OID,
                                     bool &selfIsPseudoStrong,
                                     bool &selfIsConsumed) const {
  QualType selfTy;
  selfIsPseudoStrong = false;
  selfIsConsumed = false;
  if (isInstanceMethod()) {
    // There may be no interface context due to error in declaration
    // of the interface (which has been reported). Recover gracefully.
    if (OID) {
      selfTy = Context.getObjCInterfaceType(OID);
      selfTy = Context.getObjCObjectPointerType(selfTy);
    } else {
      selfTy = Context.getObjCIdType();
    }
  } else // we have a factory method.
    selfTy = Context.getObjCClassType();

  if (Context.getLangOpts().ObjCAutoRefCount) {
    if (isInstanceMethod()) {
      selfIsConsumed = hasAttr<NSConsumesSelfAttr>();

      // 'self' is always __strong.  It's actually pseudo-strong except
      // in init methods (or methods labeled ns_consumes_self), though.
      Qualifiers qs;
      qs.setObjCLifetime(Qualifiers::OCL_Strong);
      selfTy = Context.getQualifiedType(selfTy, qs);

      // In addition, 'self' is const unless this is an init method.
      if (getMethodFamily() != OMF_init && !selfIsConsumed) {
        selfTy = selfTy.withConst();
        selfIsPseudoStrong = true;
      }
    }
    else {
      assert(isClassMethod());
      // 'self' is always const in class methods.
      selfTy = selfTy.withConst();
      selfIsPseudoStrong = true;
    }
  }
  return selfTy;
}

void ObjCMethodDecl::createImplicitParams(ASTContext &Context,
                                          const ObjCInterfaceDecl *OID) {
  bool selfIsPseudoStrong, selfIsConsumed;
  QualType selfTy =
    getSelfType(Context, OID, selfIsPseudoStrong, selfIsConsumed);
  auto *Self = ImplicitParamDecl::Create(Context, this, SourceLocation(),
                                         &Context.Idents.get("self"), selfTy,
                                         ImplicitParamKind::ObjCSelf);
  setSelfDecl(Self);

  if (selfIsConsumed)
    Self->addAttr(NSConsumedAttr::CreateImplicit(Context));

  if (selfIsPseudoStrong)
    Self->setARCPseudoStrong(true);

  setCmdDecl(ImplicitParamDecl::Create(
      Context, this, SourceLocation(), &Context.Idents.get("_cmd"),
      Context.getObjCSelType(), ImplicitParamKind::ObjCCmd));
}

ObjCInterfaceDecl *ObjCMethodDecl::getClassInterface() {
  if (auto *ID = dyn_cast<ObjCInterfaceDecl>(getDeclContext()))
    return ID;
  if (auto *CD = dyn_cast<ObjCCategoryDecl>(getDeclContext()))
    return CD->getClassInterface();
  if (auto *IMD = dyn_cast<ObjCImplDecl>(getDeclContext()))
    return IMD->getClassInterface();
  if (isa<ObjCProtocolDecl>(getDeclContext()))
    return nullptr;
  llvm_unreachable("unknown method context");
}

ObjCCategoryDecl *ObjCMethodDecl::getCategory() {
  if (auto *CD = dyn_cast<ObjCCategoryDecl>(getDeclContext()))
    return CD;
  if (auto *IMD = dyn_cast<ObjCCategoryImplDecl>(getDeclContext()))
    return IMD->getCategoryDecl();
  return nullptr;
}

SourceRange ObjCMethodDecl::getReturnTypeSourceRange() const {
  const auto *TSI = getReturnTypeSourceInfo();
  if (TSI)
    return TSI->getTypeLoc().getSourceRange();
  return SourceRange();
}

QualType ObjCMethodDecl::getSendResultType() const {
  ASTContext &Ctx = getASTContext();
  return getReturnType().getNonLValueExprType(Ctx)
           .substObjCTypeArgs(Ctx, {}, ObjCSubstitutionContext::Result);
}

QualType ObjCMethodDecl::getSendResultType(QualType receiverType) const {
  // FIXME: Handle related result types here.

  return getReturnType().getNonLValueExprType(getASTContext())
           .substObjCMemberType(receiverType, getDeclContext(),
                                ObjCSubstitutionContext::Result);
}

static void CollectOverriddenMethodsRecurse(const ObjCContainerDecl *Container,
                                            const ObjCMethodDecl *Method,
                               SmallVectorImpl<const ObjCMethodDecl *> &Methods,
                                            bool MovedToSuper) {
  if (!Container)
    return;

  // In categories look for overridden methods from protocols. A method from
  // category is not "overridden" since it is considered as the "same" method
  // (same USR) as the one from the interface.
  if (const auto *Category = dyn_cast<ObjCCategoryDecl>(Container)) {
    // Check whether we have a matching method at this category but only if we
    // are at the super class level.
    if (MovedToSuper)
      if (ObjCMethodDecl *
            Overridden = Container->getMethod(Method->getSelector(),
                                              Method->isInstanceMethod(),
                                              /*AllowHidden=*/true))
        if (Method != Overridden) {
          // We found an override at this category; there is no need to look
          // into its protocols.
          Methods.push_back(Overridden);
          return;
        }

    for (const auto *P : Category->protocols())
      CollectOverriddenMethodsRecurse(P, Method, Methods, MovedToSuper);
    return;
  }

  // Check whether we have a matching method at this level.
  if (const ObjCMethodDecl *
        Overridden = Container->getMethod(Method->getSelector(),
                                          Method->isInstanceMethod(),
                                          /*AllowHidden=*/true))
    if (Method != Overridden) {
      // We found an override at this level; there is no need to look
      // into other protocols or categories.
      Methods.push_back(Overridden);
      return;
    }

  if (const auto *Protocol = dyn_cast<ObjCProtocolDecl>(Container)){
    for (const auto *P : Protocol->protocols())
      CollectOverriddenMethodsRecurse(P, Method, Methods, MovedToSuper);
  }

  if (const auto *Interface = dyn_cast<ObjCInterfaceDecl>(Container)) {
    for (const auto *P : Interface->protocols())
      CollectOverriddenMethodsRecurse(P, Method, Methods, MovedToSuper);

    for (const auto *Cat : Interface->known_categories())
      CollectOverriddenMethodsRecurse(Cat, Method, Methods, MovedToSuper);

    if (const ObjCInterfaceDecl *Super = Interface->getSuperClass())
      return CollectOverriddenMethodsRecurse(Super, Method, Methods,
                                             /*MovedToSuper=*/true);
  }
}

static inline void CollectOverriddenMethods(const ObjCContainerDecl *Container,
                                            const ObjCMethodDecl *Method,
                             SmallVectorImpl<const ObjCMethodDecl *> &Methods) {
  CollectOverriddenMethodsRecurse(Container, Method, Methods,
                                  /*MovedToSuper=*/false);
}

static void collectOverriddenMethodsSlow(const ObjCMethodDecl *Method,
                          SmallVectorImpl<const ObjCMethodDecl *> &overridden) {
  assert(Method->isOverriding());

  if (const auto *ProtD =
          dyn_cast<ObjCProtocolDecl>(Method->getDeclContext())) {
    CollectOverriddenMethods(ProtD, Method, overridden);

  } else if (const auto *IMD =
                 dyn_cast<ObjCImplDecl>(Method->getDeclContext())) {
    const ObjCInterfaceDecl *ID = IMD->getClassInterface();
    if (!ID)
      return;
    // Start searching for overridden methods using the method from the
    // interface as starting point.
    if (const ObjCMethodDecl *IFaceMeth = ID->getMethod(Method->getSelector(),
                                                    Method->isInstanceMethod(),
                                                    /*AllowHidden=*/true))
      Method = IFaceMeth;
    CollectOverriddenMethods(ID, Method, overridden);

  } else if (const auto *CatD =
                 dyn_cast<ObjCCategoryDecl>(Method->getDeclContext())) {
    const ObjCInterfaceDecl *ID = CatD->getClassInterface();
    if (!ID)
      return;
    // Start searching for overridden methods using the method from the
    // interface as starting point.
    if (const ObjCMethodDecl *IFaceMeth = ID->getMethod(Method->getSelector(),
                                                     Method->isInstanceMethod(),
                                                     /*AllowHidden=*/true))
      Method = IFaceMeth;
    CollectOverriddenMethods(ID, Method, overridden);

  } else {
    CollectOverriddenMethods(
                  dyn_cast_or_null<ObjCContainerDecl>(Method->getDeclContext()),
                  Method, overridden);
  }
}

void ObjCMethodDecl::getOverriddenMethods(
                    SmallVectorImpl<const ObjCMethodDecl *> &Overridden) const {
  const ObjCMethodDecl *Method = this;

  if (Method->isRedeclaration()) {
    Method = cast<ObjCContainerDecl>(Method->getDeclContext())
                 ->getMethod(Method->getSelector(), Method->isInstanceMethod(),
                             /*AllowHidden=*/true);
  }

  if (Method->isOverriding()) {
    collectOverriddenMethodsSlow(Method, Overridden);
    assert(!Overridden.empty() &&
           "ObjCMethodDecl's overriding bit is not as expected");
  }
}

const ObjCPropertyDecl *
ObjCMethodDecl::findPropertyDecl(bool CheckOverrides) const {
  Selector Sel = getSelector();
  unsigned NumArgs = Sel.getNumArgs();
  if (NumArgs > 1)
    return nullptr;

  if (isPropertyAccessor()) {
    const auto *Container = cast<ObjCContainerDecl>(getParent());
    // For accessor stubs, go back to the interface.
    if (auto *ImplDecl = dyn_cast<ObjCImplDecl>(Container))
      if (isSynthesizedAccessorStub())
        Container = ImplDecl->getClassInterface();

    bool IsGetter = (NumArgs == 0);
    bool IsInstance = isInstanceMethod();

    /// Local function that attempts to find a matching property within the
    /// given Objective-C container.
    auto findMatchingProperty =
      [&](const ObjCContainerDecl *Container) -> const ObjCPropertyDecl * {
      if (IsInstance) {
        for (const auto *I : Container->instance_properties()) {
          Selector NextSel = IsGetter ? I->getGetterName()
                                      : I->getSetterName();
          if (NextSel == Sel)
            return I;
        }
      } else {
        for (const auto *I : Container->class_properties()) {
          Selector NextSel = IsGetter ? I->getGetterName()
                                      : I->getSetterName();
          if (NextSel == Sel)
            return I;
        }
      }

      return nullptr;
    };

    // Look in the container we were given.
    if (const auto *Found = findMatchingProperty(Container))
      return Found;

    // If we're in a category or extension, look in the main class.
    const ObjCInterfaceDecl *ClassDecl = nullptr;
    if (const auto *Category = dyn_cast<ObjCCategoryDecl>(Container)) {
      ClassDecl = Category->getClassInterface();
      if (const auto *Found = findMatchingProperty(ClassDecl))
        return Found;
    } else {
      // Determine whether the container is a class.
      ClassDecl = cast<ObjCInterfaceDecl>(Container);
    }
    assert(ClassDecl && "Failed to find main class");

    // If we have a class, check its visible extensions.
    for (const auto *Ext : ClassDecl->visible_extensions()) {
      if (Ext == Container)
        continue;
      if (const auto *Found = findMatchingProperty(Ext))
        return Found;
    }

    assert(isSynthesizedAccessorStub() && "expected an accessor stub");

    for (const auto *Cat : ClassDecl->known_categories()) {
      if (Cat == Container)
        continue;
      if (const auto *Found = findMatchingProperty(Cat))
        return Found;
    }

    llvm_unreachable("Marked as a property accessor but no property found!");
  }

  if (!CheckOverrides)
    return nullptr;

  using OverridesTy = SmallVector<const ObjCMethodDecl *, 8>;

  OverridesTy Overrides;
  getOverriddenMethods(Overrides);
  for (const auto *Override : Overrides)
    if (const ObjCPropertyDecl *Prop = Override->findPropertyDecl(false))
      return Prop;

  return nullptr;
}

//===----------------------------------------------------------------------===//
// ObjCTypeParamDecl
//===----------------------------------------------------------------------===//

void ObjCTypeParamDecl::anchor() {}

ObjCTypeParamDecl *ObjCTypeParamDecl::Create(ASTContext &ctx, DeclContext *dc,
                                             ObjCTypeParamVariance variance,
                                             SourceLocation varianceLoc,
                                             unsigned index,
                                             SourceLocation nameLoc,
                                             IdentifierInfo *name,
                                             SourceLocation colonLoc,
                                             TypeSourceInfo *boundInfo) {
  auto *TPDecl =
    new (ctx, dc) ObjCTypeParamDecl(ctx, dc, variance, varianceLoc, index,
                                    nameLoc, name, colonLoc, boundInfo);
  QualType TPType = ctx.getObjCTypeParamType(TPDecl, {});
  TPDecl->setTypeForDecl(TPType.getTypePtr());
  return TPDecl;
}

ObjCTypeParamDecl *ObjCTypeParamDecl::CreateDeserialized(ASTContext &ctx,
                                                         GlobalDeclID ID) {
  return new (ctx, ID) ObjCTypeParamDecl(ctx, nullptr,
                                         ObjCTypeParamVariance::Invariant,
                                         SourceLocation(), 0, SourceLocation(),
                                         nullptr, SourceLocation(), nullptr);
}

SourceRange ObjCTypeParamDecl::getSourceRange() const {
  SourceLocation startLoc = VarianceLoc;
  if (startLoc.isInvalid())
    startLoc = getLocation();

  if (hasExplicitBound()) {
    return SourceRange(startLoc,
                       getTypeSourceInfo()->getTypeLoc().getEndLoc());
  }

  return SourceRange(startLoc);
}

//===----------------------------------------------------------------------===//
// ObjCTypeParamList
//===----------------------------------------------------------------------===//
ObjCTypeParamList::ObjCTypeParamList(SourceLocation lAngleLoc,
                                     ArrayRef<ObjCTypeParamDecl *> typeParams,
                                     SourceLocation rAngleLoc)
    : Brackets(lAngleLoc, rAngleLoc), NumParams(typeParams.size()) {
  std::copy(typeParams.begin(), typeParams.end(), begin());
}

ObjCTypeParamList *ObjCTypeParamList::create(
                     ASTContext &ctx,
                     SourceLocation lAngleLoc,
                     ArrayRef<ObjCTypeParamDecl *> typeParams,
                     SourceLocation rAngleLoc) {
  void *mem =
      ctx.Allocate(totalSizeToAlloc<ObjCTypeParamDecl *>(typeParams.size()),
                   alignof(ObjCTypeParamList));
  return new (mem) ObjCTypeParamList(lAngleLoc, typeParams, rAngleLoc);
}

void ObjCTypeParamList::gatherDefaultTypeArgs(
       SmallVectorImpl<QualType> &typeArgs) const {
  typeArgs.reserve(size());
  for (auto *typeParam : *this)
    typeArgs.push_back(typeParam->getUnderlyingType());
}

//===----------------------------------------------------------------------===//
// ObjCInterfaceDecl
//===----------------------------------------------------------------------===//

ObjCInterfaceDecl *ObjCInterfaceDecl::Create(
    const ASTContext &C, DeclContext *DC, SourceLocation atLoc,
    const IdentifierInfo *Id, ObjCTypeParamList *typeParamList,
    ObjCInterfaceDecl *PrevDecl, SourceLocation ClassLoc, bool isInternal) {
  auto *Result = new (C, DC)
      ObjCInterfaceDecl(C, DC, atLoc, Id, typeParamList, ClassLoc, PrevDecl,
                        isInternal);
  Result->Data.setInt(!C.getLangOpts().Modules);
  C.getObjCInterfaceType(Result, PrevDecl);
  return Result;
}

ObjCInterfaceDecl *ObjCInterfaceDecl::CreateDeserialized(const ASTContext &C,
                                                         GlobalDeclID ID) {
  auto *Result = new (C, ID)
      ObjCInterfaceDecl(C, nullptr, SourceLocation(), nullptr, nullptr,
                        SourceLocation(), nullptr, false);
  Result->Data.setInt(!C.getLangOpts().Modules);
  return Result;
}

ObjCInterfaceDecl::ObjCInterfaceDecl(
    const ASTContext &C, DeclContext *DC, SourceLocation AtLoc,
    const IdentifierInfo *Id, ObjCTypeParamList *typeParamList,
    SourceLocation CLoc, ObjCInterfaceDecl *PrevDecl, bool IsInternal)
    : ObjCContainerDecl(ObjCInterface, DC, Id, CLoc, AtLoc),
      redeclarable_base(C) {
  setPreviousDecl(PrevDecl);

  // Copy the 'data' pointer over.
  if (PrevDecl)
    Data = PrevDecl->Data;

  setImplicit(IsInternal);

  setTypeParamList(typeParamList);
}

void ObjCInterfaceDecl::LoadExternalDefinition() const {
  assert(data().ExternallyCompleted && "Class is not externally completed");
  data().ExternallyCompleted = false;
  getASTContext().getExternalSource()->CompleteType(
                                        const_cast<ObjCInterfaceDecl *>(this));
}

void ObjCInterfaceDecl::setExternallyCompleted() {
  assert(getASTContext().getExternalSource() &&
         "Class can't be externally completed without an external source");
  assert(hasDefinition() &&
         "Forward declarations can't be externally completed");
  data().ExternallyCompleted = true;
}

void ObjCInterfaceDecl::setHasDesignatedInitializers() {
  // Check for a complete definition and recover if not so.
  if (!isThisDeclarationADefinition())
    return;
  data().HasDesignatedInitializers = true;
}

bool ObjCInterfaceDecl::hasDesignatedInitializers() const {
  // Check for a complete definition and recover if not so.
  if (!isThisDeclarationADefinition())
    return false;
  if (data().ExternallyCompleted)
    LoadExternalDefinition();

  return data().HasDesignatedInitializers;
}

StringRef
ObjCInterfaceDecl::getObjCRuntimeNameAsString() const {
  if (const auto *ObjCRTName = getAttr<ObjCRuntimeNameAttr>())
    return ObjCRTName->getMetadataName();

  return getName();
}

StringRef
ObjCImplementationDecl::getObjCRuntimeNameAsString() const {
  if (ObjCInterfaceDecl *ID =
      const_cast<ObjCImplementationDecl*>(this)->getClassInterface())
    return ID->getObjCRuntimeNameAsString();

  return getName();
}

ObjCImplementationDecl *ObjCInterfaceDecl::getImplementation() const {
  if (const ObjCInterfaceDecl *Def = getDefinition()) {
    if (data().ExternallyCompleted)
      LoadExternalDefinition();

    return getASTContext().getObjCImplementation(
             const_cast<ObjCInterfaceDecl*>(Def));
  }

  // FIXME: Should make sure no callers ever do this.
  return nullptr;
}

void ObjCInterfaceDecl::setImplementation(ObjCImplementationDecl *ImplD) {
  getASTContext().setObjCImplementation(getDefinition(), ImplD);
}

namespace {

struct SynthesizeIvarChunk {
  uint64_t Size;
  ObjCIvarDecl *Ivar;

  SynthesizeIvarChunk(uint64_t size, ObjCIvarDecl *ivar)
      : Size(size), Ivar(ivar) {}
};

bool operator<(const SynthesizeIvarChunk & LHS,
               const SynthesizeIvarChunk &RHS) {
    return LHS.Size < RHS.Size;
}

} // namespace

/// all_declared_ivar_begin - return first ivar declared in this class,
/// its extensions and its implementation. Lazily build the list on first
/// access.
///
/// Caveat: The list returned by this method reflects the current
/// state of the parser. The cache will be updated for every ivar
/// added by an extension or the implementation when they are
/// encountered.
/// See also ObjCIvarDecl::Create().
ObjCIvarDecl *ObjCInterfaceDecl::all_declared_ivar_begin() {
  // FIXME: Should make sure no callers ever do this.
  if (!hasDefinition())
    return nullptr;

  ObjCIvarDecl *curIvar = nullptr;
  if (!data().IvarList) {
    // Force ivar deserialization upfront, before building IvarList.
    (void)ivar_empty();
    for (const auto *Ext : known_extensions()) {
      (void)Ext->ivar_empty();
    }
    if (!ivar_empty()) {
      ObjCInterfaceDecl::ivar_iterator I = ivar_begin(), E = ivar_end();
      data().IvarList = *I; ++I;
      for (curIvar = data().IvarList; I != E; curIvar = *I, ++I)
        curIvar->setNextIvar(*I);
    }

    for (const auto *Ext : known_extensions()) {
      if (!Ext->ivar_empty()) {
        ObjCCategoryDecl::ivar_iterator
          I = Ext->ivar_begin(),
          E = Ext->ivar_end();
        if (!data().IvarList) {
          data().IvarList = *I; ++I;
          curIvar = data().IvarList;
        }
        for ( ;I != E; curIvar = *I, ++I)
          curIvar->setNextIvar(*I);
      }
    }
    data().IvarListMissingImplementation = true;
  }

  // cached and complete!
  if (!data().IvarListMissingImplementation)
      return data().IvarList;

  if (ObjCImplementationDecl *ImplDecl = getImplementation()) {
    data().IvarListMissingImplementation = false;
    if (!ImplDecl->ivar_empty()) {
      SmallVector<SynthesizeIvarChunk, 16> layout;
      for (auto *IV : ImplDecl->ivars()) {
        if (IV->getSynthesize() && !IV->isInvalidDecl()) {
          layout.push_back(SynthesizeIvarChunk(
                             IV->getASTContext().getTypeSize(IV->getType()), IV));
          continue;
        }
        if (!data().IvarList)
          data().IvarList = IV;
        else
          curIvar->setNextIvar(IV);
        curIvar = IV;
      }

      if (!layout.empty()) {
        // Order synthesized ivars by their size.
        llvm::stable_sort(layout);
        unsigned Ix = 0, EIx = layout.size();
        if (!data().IvarList) {
          data().IvarList = layout[0].Ivar; Ix++;
          curIvar = data().IvarList;
        }
        for ( ; Ix != EIx; curIvar = layout[Ix].Ivar, Ix++)
          curIvar->setNextIvar(layout[Ix].Ivar);
      }
    }
  }
  return data().IvarList;
}

/// FindCategoryDeclaration - Finds category declaration in the list of
/// categories for this class and returns it. Name of the category is passed
/// in 'CategoryId'. If category not found, return 0;
///
ObjCCategoryDecl *ObjCInterfaceDecl::FindCategoryDeclaration(
    const IdentifierInfo *CategoryId) const {
  // FIXME: Should make sure no callers ever do this.
  if (!hasDefinition())
    return nullptr;

  if (data().ExternallyCompleted)
    LoadExternalDefinition();

  for (auto *Cat : visible_categories())
    if (Cat->getIdentifier() == CategoryId)
      return Cat;

  return nullptr;
}

ObjCMethodDecl *
ObjCInterfaceDecl::getCategoryInstanceMethod(Selector Sel) const {
  for (const auto *Cat : visible_categories()) {
    if (ObjCCategoryImplDecl *Impl = Cat->getImplementation())
      if (ObjCMethodDecl *MD = Impl->getInstanceMethod(Sel))
        return MD;
  }

  return nullptr;
}

ObjCMethodDecl *ObjCInterfaceDecl::getCategoryClassMethod(Selector Sel) const {
  for (const auto *Cat : visible_categories()) {
    if (ObjCCategoryImplDecl *Impl = Cat->getImplementation())
      if (ObjCMethodDecl *MD = Impl->getClassMethod(Sel))
        return MD;
  }

  return nullptr;
}

/// ClassImplementsProtocol - Checks that 'lProto' protocol
/// has been implemented in IDecl class, its super class or categories (if
/// lookupCategory is true).
bool ObjCInterfaceDecl::ClassImplementsProtocol(ObjCProtocolDecl *lProto,
                                    bool lookupCategory,
                                    bool RHSIsQualifiedID) {
  if (!hasDefinition())
    return false;

  ObjCInterfaceDecl *IDecl = this;
  // 1st, look up the class.
  for (auto *PI : IDecl->protocols()){
    if (getASTContext().ProtocolCompatibleWithProtocol(lProto, PI))
      return true;
    // This is dubious and is added to be compatible with gcc.  In gcc, it is
    // also allowed assigning a protocol-qualified 'id' type to a LHS object
    // when protocol in qualified LHS is in list of protocols in the rhs 'id'
    // object. This IMO, should be a bug.
    // FIXME: Treat this as an extension, and flag this as an error when GCC
    // extensions are not enabled.
    if (RHSIsQualifiedID &&
        getASTContext().ProtocolCompatibleWithProtocol(PI, lProto))
      return true;
  }

  // 2nd, look up the category.
  if (lookupCategory)
    for (const auto *Cat : visible_categories()) {
      for (auto *PI : Cat->protocols())
        if (getASTContext().ProtocolCompatibleWithProtocol(lProto, PI))
          return true;
    }

  // 3rd, look up the super class(s)
  if (IDecl->getSuperClass())
    return
  IDecl->getSuperClass()->ClassImplementsProtocol(lProto, lookupCategory,
                                                  RHSIsQualifiedID);

  return false;
}

//===----------------------------------------------------------------------===//
// ObjCIvarDecl
//===----------------------------------------------------------------------===//

void ObjCIvarDecl::anchor() {}

ObjCIvarDecl *ObjCIvarDecl::Create(ASTContext &C, ObjCContainerDecl *DC,
                                   SourceLocation StartLoc,
                                   SourceLocation IdLoc,
                                   const IdentifierInfo *Id, QualType T,
                                   TypeSourceInfo *TInfo, AccessControl ac,
                                   Expr *BW, bool synthesized) {
  if (DC) {
    // Ivar's can only appear in interfaces, implementations (via synthesized
    // properties), and class extensions (via direct declaration, or synthesized
    // properties).
    //
    // FIXME: This should really be asserting this:
    //   (isa<ObjCCategoryDecl>(DC) &&
    //    cast<ObjCCategoryDecl>(DC)->IsClassExtension()))
    // but unfortunately we sometimes place ivars into non-class extension
    // categories on error. This breaks an AST invariant, and should not be
    // fixed.
    assert((isa<ObjCInterfaceDecl>(DC) || isa<ObjCImplementationDecl>(DC) ||
            isa<ObjCCategoryDecl>(DC)) &&
           "Invalid ivar decl context!");
    // Once a new ivar is created in any of class/class-extension/implementation
    // decl contexts, the previously built IvarList must be rebuilt.
    auto *ID = dyn_cast<ObjCInterfaceDecl>(DC);
    if (!ID) {
      if (auto *IM = dyn_cast<ObjCImplementationDecl>(DC))
        ID = IM->getClassInterface();
      else
        ID = cast<ObjCCategoryDecl>(DC)->getClassInterface();
    }
    ID->setIvarList(nullptr);
  }

  return new (C, DC) ObjCIvarDecl(DC, StartLoc, IdLoc, Id, T, TInfo, ac, BW,
                                  synthesized);
}

ObjCIvarDecl *ObjCIvarDecl::CreateDeserialized(ASTContext &C, GlobalDeclID ID) {
  return new (C, ID) ObjCIvarDecl(nullptr, SourceLocation(), SourceLocation(),
                                  nullptr, QualType(), nullptr,
                                  ObjCIvarDecl::None, nullptr, false);
}

ObjCInterfaceDecl *ObjCIvarDecl::getContainingInterface() {
  auto *DC = cast<ObjCContainerDecl>(getDeclContext());

  switch (DC->getKind()) {
  default:
  case ObjCCategoryImpl:
  case ObjCProtocol:
    llvm_unreachable("invalid ivar container!");

    // Ivars can only appear in class extension categories.
  case ObjCCategory: {
    auto *CD = cast<ObjCCategoryDecl>(DC);
    assert(CD->IsClassExtension() && "invalid container for ivar!");
    return CD->getClassInterface();
  }

  case ObjCImplementation:
    return cast<ObjCImplementationDecl>(DC)->getClassInterface();

  case ObjCInterface:
    return cast<ObjCInterfaceDecl>(DC);
  }
}

QualType ObjCIvarDecl::getUsageType(QualType objectType) const {
  return getType().substObjCMemberType(objectType, getDeclContext(),
                                       ObjCSubstitutionContext::Property);
}

//===----------------------------------------------------------------------===//
// ObjCAtDefsFieldDecl
//===----------------------------------------------------------------------===//

void ObjCAtDefsFieldDecl::anchor() {}

ObjCAtDefsFieldDecl
*ObjCAtDefsFieldDecl::Create(ASTContext &C, DeclContext *DC,
                             SourceLocation StartLoc,  SourceLocation IdLoc,
                             IdentifierInfo *Id, QualType T, Expr *BW) {
  return new (C, DC) ObjCAtDefsFieldDecl(DC, StartLoc, IdLoc, Id, T, BW);
}

ObjCAtDefsFieldDecl *ObjCAtDefsFieldDecl::CreateDeserialized(ASTContext &C,
                                                             GlobalDeclID ID) {
  return new (C, ID) ObjCAtDefsFieldDecl(nullptr, SourceLocation(),
                                         SourceLocation(), nullptr, QualType(),
                                         nullptr);
}

//===----------------------------------------------------------------------===//
// ObjCProtocolDecl
//===----------------------------------------------------------------------===//

void ObjCProtocolDecl::anchor() {}

ObjCProtocolDecl::ObjCProtocolDecl(ASTContext &C, DeclContext *DC,
                                   IdentifierInfo *Id, SourceLocation nameLoc,
                                   SourceLocation atStartLoc,
                                   ObjCProtocolDecl *PrevDecl)
    : ObjCContainerDecl(ObjCProtocol, DC, Id, nameLoc, atStartLoc),
      redeclarable_base(C) {
  setPreviousDecl(PrevDecl);
  if (PrevDecl)
    Data = PrevDecl->Data;
}

ObjCProtocolDecl *ObjCProtocolDecl::Create(ASTContext &C, DeclContext *DC,
                                           IdentifierInfo *Id,
                                           SourceLocation nameLoc,
                                           SourceLocation atStartLoc,
                                           ObjCProtocolDecl *PrevDecl) {
  auto *Result =
      new (C, DC) ObjCProtocolDecl(C, DC, Id, nameLoc, atStartLoc, PrevDecl);
  Result->Data.setInt(!C.getLangOpts().Modules);
  return Result;
}

ObjCProtocolDecl *ObjCProtocolDecl::CreateDeserialized(ASTContext &C,
                                                       GlobalDeclID ID) {
  ObjCProtocolDecl *Result =
      new (C, ID) ObjCProtocolDecl(C, nullptr, nullptr, SourceLocation(),
                                   SourceLocation(), nullptr);
  Result->Data.setInt(!C.getLangOpts().Modules);
  return Result;
}

bool ObjCProtocolDecl::isNonRuntimeProtocol() const {
  return hasAttr<ObjCNonRuntimeProtocolAttr>();
}

void ObjCProtocolDecl::getImpliedProtocols(
    llvm::DenseSet<const ObjCProtocolDecl *> &IPs) const {
  std::queue<const ObjCProtocolDecl *> WorkQueue;
  WorkQueue.push(this);

  while (!WorkQueue.empty()) {
    const auto *PD = WorkQueue.front();
    WorkQueue.pop();
    for (const auto *Parent : PD->protocols()) {
      const auto *Can = Parent->getCanonicalDecl();
      auto Result = IPs.insert(Can);
      if (Result.second)
        WorkQueue.push(Parent);
    }
  }
}

ObjCProtocolDecl *ObjCProtocolDecl::lookupProtocolNamed(IdentifierInfo *Name) {
  ObjCProtocolDecl *PDecl = this;

  if (Name == getIdentifier())
    return PDecl;

  for (auto *I : protocols())
    if ((PDecl = I->lookupProtocolNamed(Name)))
      return PDecl;

  return nullptr;
}

// lookupMethod - Lookup a instance/class method in the protocol and protocols
// it inherited.
ObjCMethodDecl *ObjCProtocolDecl::lookupMethod(Selector Sel,
                                               bool isInstance) const {
  ObjCMethodDecl *MethodDecl = nullptr;

  // If there is no definition or the definition is hidden, we don't find
  // anything.
  const ObjCProtocolDecl *Def = getDefinition();
  if (!Def || !Def->isUnconditionallyVisible())
    return nullptr;

  if ((MethodDecl = getMethod(Sel, isInstance)))
    return MethodDecl;

  for (const auto *I : protocols())
    if ((MethodDecl = I->lookupMethod(Sel, isInstance)))
      return MethodDecl;
  return nullptr;
}

void ObjCProtocolDecl::allocateDefinitionData() {
  assert(!Data.getPointer() && "Protocol already has a definition!");
  Data.setPointer(new (getASTContext()) DefinitionData);
  Data.getPointer()->Definition = this;
  Data.getPointer()->HasODRHash = false;
}

void ObjCProtocolDecl::startDefinition() {
  allocateDefinitionData();

  // Update all of the declarations with a pointer to the definition.
  for (auto *RD : redecls())
    RD->Data = this->Data;
}

void ObjCProtocolDecl::startDuplicateDefinitionForComparison() {
  Data.setPointer(nullptr);
  allocateDefinitionData();
  // Don't propagate data to other redeclarations.
}

void ObjCProtocolDecl::mergeDuplicateDefinitionWithCommon(
    const ObjCProtocolDecl *Definition) {
  Data = Definition->Data;
}

void ObjCProtocolDecl::collectPropertiesToImplement(PropertyMap &PM) const {
  if (const ObjCProtocolDecl *PDecl = getDefinition()) {
    for (auto *Prop : PDecl->properties()) {
      // Insert into PM if not there already.
      PM.insert(std::make_pair(
          std::make_pair(Prop->getIdentifier(), Prop->isClassProperty()),
          Prop));
    }
    // Scan through protocol's protocols.
    for (const auto *PI : PDecl->protocols())
      PI->collectPropertiesToImplement(PM);
  }
}

void ObjCProtocolDecl::collectInheritedProtocolProperties(
    const ObjCPropertyDecl *Property, ProtocolPropertySet &PS,
    PropertyDeclOrder &PO) const {
  if (const ObjCProtocolDecl *PDecl = getDefinition()) {
    if (!PS.insert(PDecl).second)
      return;
    for (auto *Prop : PDecl->properties()) {
      if (Prop == Property)
        continue;
      if (Prop->getIdentifier() == Property->getIdentifier()) {
        PO.push_back(Prop);
        return;
      }
    }
    // Scan through protocol's protocols which did not have a matching property.
    for (const auto *PI : PDecl->protocols())
      PI->collectInheritedProtocolProperties(Property, PS, PO);
  }
}

StringRef
ObjCProtocolDecl::getObjCRuntimeNameAsString() const {
  if (const auto *ObjCRTName = getAttr<ObjCRuntimeNameAttr>())
    return ObjCRTName->getMetadataName();

  return getName();
}

unsigned ObjCProtocolDecl::getODRHash() {
  assert(hasDefinition() && "ODRHash only for records with definitions");

  // Previously calculated hash is stored in DefinitionData.
  if (hasODRHash())
    return data().ODRHash;

  // Only calculate hash on first call of getODRHash per record.
  ODRHash Hasher;
  Hasher.AddObjCProtocolDecl(getDefinition());
  data().ODRHash = Hasher.CalculateHash();
  setHasODRHash(true);

  return data().ODRHash;
}

bool ObjCProtocolDecl::hasODRHash() const {
  if (!hasDefinition())
    return false;
  return data().HasODRHash;
}

void ObjCProtocolDecl::setHasODRHash(bool HasHash) {
  assert(hasDefinition() && "Cannot set ODRHash without definition");
  data().HasODRHash = HasHash;
}

//===----------------------------------------------------------------------===//
// ObjCCategoryDecl
//===----------------------------------------------------------------------===//

void ObjCCategoryDecl::anchor() {}

ObjCCategoryDecl::ObjCCategoryDecl(
    DeclContext *DC, SourceLocation AtLoc, SourceLocation ClassNameLoc,
    SourceLocation CategoryNameLoc, const IdentifierInfo *Id,
    ObjCInterfaceDecl *IDecl, ObjCTypeParamList *typeParamList,
    SourceLocation IvarLBraceLoc, SourceLocation IvarRBraceLoc)
    : ObjCContainerDecl(ObjCCategory, DC, Id, ClassNameLoc, AtLoc),
      ClassInterface(IDecl), CategoryNameLoc(CategoryNameLoc),
      IvarLBraceLoc(IvarLBraceLoc), IvarRBraceLoc(IvarRBraceLoc) {
  setTypeParamList(typeParamList);
}

ObjCCategoryDecl *ObjCCategoryDecl::Create(
    ASTContext &C, DeclContext *DC, SourceLocation AtLoc,
    SourceLocation ClassNameLoc, SourceLocation CategoryNameLoc,
    const IdentifierInfo *Id, ObjCInterfaceDecl *IDecl,
    ObjCTypeParamList *typeParamList, SourceLocation IvarLBraceLoc,
    SourceLocation IvarRBraceLoc) {
  auto *CatDecl =
      new (C, DC) ObjCCategoryDecl(DC, AtLoc, ClassNameLoc, CategoryNameLoc, Id,
                                   IDecl, typeParamList, IvarLBraceLoc,
                                   IvarRBraceLoc);
  if (IDecl) {
    // Link this category into its class's category list.
    CatDecl->NextClassCategory = IDecl->getCategoryListRaw();
    if (IDecl->hasDefinition()) {
      IDecl->setCategoryListRaw(CatDecl);
      if (ASTMutationListener *L = C.getASTMutationListener())
        L->AddedObjCCategoryToInterface(CatDecl, IDecl);
    }
  }

  return CatDecl;
}

ObjCCategoryDecl *ObjCCategoryDecl::CreateDeserialized(ASTContext &C,
                                                       GlobalDeclID ID) {
  return new (C, ID) ObjCCategoryDecl(nullptr, SourceLocation(),
                                      SourceLocation(), SourceLocation(),
                                      nullptr, nullptr, nullptr);
}

ObjCCategoryImplDecl *ObjCCategoryDecl::getImplementation() const {
  return getASTContext().getObjCImplementation(
                                           const_cast<ObjCCategoryDecl*>(this));
}

void ObjCCategoryDecl::setImplementation(ObjCCategoryImplDecl *ImplD) {
  getASTContext().setObjCImplementation(this, ImplD);
}

void ObjCCategoryDecl::setTypeParamList(ObjCTypeParamList *TPL) {
  TypeParamList = TPL;
  if (!TPL)
    return;
  // Set the declaration context of each of the type parameters.
  for (auto *typeParam : *TypeParamList)
    typeParam->setDeclContext(this);
}

//===----------------------------------------------------------------------===//
// ObjCCategoryImplDecl
//===----------------------------------------------------------------------===//

void ObjCCategoryImplDecl::anchor() {}

ObjCCategoryImplDecl *ObjCCategoryImplDecl::Create(
    ASTContext &C, DeclContext *DC, const IdentifierInfo *Id,
    ObjCInterfaceDecl *ClassInterface, SourceLocation nameLoc,
    SourceLocation atStartLoc, SourceLocation CategoryNameLoc) {
  if (ClassInterface && ClassInterface->hasDefinition())
    ClassInterface = ClassInterface->getDefinition();
  return new (C, DC) ObjCCategoryImplDecl(DC, Id, ClassInterface, nameLoc,
                                          atStartLoc, CategoryNameLoc);
}

ObjCCategoryImplDecl *
ObjCCategoryImplDecl::CreateDeserialized(ASTContext &C, GlobalDeclID ID) {
  return new (C, ID) ObjCCategoryImplDecl(nullptr, nullptr, nullptr,
                                          SourceLocation(), SourceLocation(),
                                          SourceLocation());
}

ObjCCategoryDecl *ObjCCategoryImplDecl::getCategoryDecl() const {
  // The class interface might be NULL if we are working with invalid code.
  if (const ObjCInterfaceDecl *ID = getClassInterface())
    return ID->FindCategoryDeclaration(getIdentifier());
  return nullptr;
}

void ObjCImplDecl::anchor() {}

void ObjCImplDecl::addPropertyImplementation(ObjCPropertyImplDecl *property) {
  // FIXME: The context should be correct before we get here.
  property->setLexicalDeclContext(this);
  addDecl(property);
}

void ObjCImplDecl::setClassInterface(ObjCInterfaceDecl *IFace) {
  ASTContext &Ctx = getASTContext();

  if (auto *ImplD = dyn_cast_or_null<ObjCImplementationDecl>(this)) {
    if (IFace)
      Ctx.setObjCImplementation(IFace, ImplD);

  } else if (auto *ImplD = dyn_cast_or_null<ObjCCategoryImplDecl>(this)) {
    if (ObjCCategoryDecl *CD = IFace->FindCategoryDeclaration(getIdentifier()))
      Ctx.setObjCImplementation(CD, ImplD);
  }

  ClassInterface = IFace;
}

/// FindPropertyImplIvarDecl - This method lookup the ivar in the list of
/// properties implemented in this \@implementation block and returns
/// the implemented property that uses it.
ObjCPropertyImplDecl *ObjCImplDecl::
FindPropertyImplIvarDecl(IdentifierInfo *ivarId) const {
  for (auto *PID : property_impls())
    if (PID->getPropertyIvarDecl() &&
        PID->getPropertyIvarDecl()->getIdentifier() == ivarId)
      return PID;
  return nullptr;
}

/// FindPropertyImplDecl - This method looks up a previous ObjCPropertyImplDecl
/// added to the list of those properties \@synthesized/\@dynamic in this
/// category \@implementation block.
ObjCPropertyImplDecl *ObjCImplDecl::
FindPropertyImplDecl(IdentifierInfo *Id,
                     ObjCPropertyQueryKind QueryKind) const {
  ObjCPropertyImplDecl *ClassPropImpl = nullptr;
  for (auto *PID : property_impls())
    // If queryKind is unknown, we return the instance property if one
    // exists; otherwise we return the class property.
    if (PID->getPropertyDecl()->getIdentifier() == Id) {
      if ((QueryKind == ObjCPropertyQueryKind::OBJC_PR_query_unknown &&
           !PID->getPropertyDecl()->isClassProperty()) ||
          (QueryKind == ObjCPropertyQueryKind::OBJC_PR_query_class &&
           PID->getPropertyDecl()->isClassProperty()) ||
          (QueryKind == ObjCPropertyQueryKind::OBJC_PR_query_instance &&
           !PID->getPropertyDecl()->isClassProperty()))
        return PID;

      if (PID->getPropertyDecl()->isClassProperty())
        ClassPropImpl = PID;
    }

  if (QueryKind == ObjCPropertyQueryKind::OBJC_PR_query_unknown)
    // We can't find the instance property, return the class property.
    return ClassPropImpl;

  return nullptr;
}

raw_ostream &clang::operator<<(raw_ostream &OS,
                               const ObjCCategoryImplDecl &CID) {
  OS << CID.getName();
  return OS;
}

//===----------------------------------------------------------------------===//
// ObjCImplementationDecl
//===----------------------------------------------------------------------===//

void ObjCImplementationDecl::anchor() {}

ObjCImplementationDecl *
ObjCImplementationDecl::Create(ASTContext &C, DeclContext *DC,
                               ObjCInterfaceDecl *ClassInterface,
                               ObjCInterfaceDecl *SuperDecl,
                               SourceLocation nameLoc,
                               SourceLocation atStartLoc,
                               SourceLocation superLoc,
                               SourceLocation IvarLBraceLoc,
                               SourceLocation IvarRBraceLoc) {
  if (ClassInterface && ClassInterface->hasDefinition())
    ClassInterface = ClassInterface->getDefinition();
  return new (C, DC) ObjCImplementationDecl(DC, ClassInterface, SuperDecl,
                                            nameLoc, atStartLoc, superLoc,
                                            IvarLBraceLoc, IvarRBraceLoc);
}

ObjCImplementationDecl *
ObjCImplementationDecl::CreateDeserialized(ASTContext &C, GlobalDeclID ID) {
  return new (C, ID) ObjCImplementationDecl(nullptr, nullptr, nullptr,
                                            SourceLocation(), SourceLocation());
}

void ObjCImplementationDecl::setIvarInitializers(ASTContext &C,
                                             CXXCtorInitializer ** initializers,
                                                 unsigned numInitializers) {
  if (numInitializers > 0) {
    NumIvarInitializers = numInitializers;
    auto **ivarInitializers = new (C) CXXCtorInitializer*[NumIvarInitializers];
    memcpy(ivarInitializers, initializers,
           numInitializers * sizeof(CXXCtorInitializer*));
    IvarInitializers = ivarInitializers;
  }
}

ObjCImplementationDecl::init_const_iterator
ObjCImplementationDecl::init_begin() const {
  return IvarInitializers.get(getASTContext().getExternalSource());
}

raw_ostream &clang::operator<<(raw_ostream &OS,
                               const ObjCImplementationDecl &ID) {
  OS << ID.getName();
  return OS;
}

//===----------------------------------------------------------------------===//
// ObjCCompatibleAliasDecl
//===----------------------------------------------------------------------===//

void ObjCCompatibleAliasDecl::anchor() {}

ObjCCompatibleAliasDecl *
ObjCCompatibleAliasDecl::Create(ASTContext &C, DeclContext *DC,
                                SourceLocation L,
                                IdentifierInfo *Id,
                                ObjCInterfaceDecl* AliasedClass) {
  return new (C, DC) ObjCCompatibleAliasDecl(DC, L, Id, AliasedClass);
}

ObjCCompatibleAliasDecl *
ObjCCompatibleAliasDecl::CreateDeserialized(ASTContext &C, GlobalDeclID ID) {
  return new (C, ID) ObjCCompatibleAliasDecl(nullptr, SourceLocation(),
                                             nullptr, nullptr);
}

//===----------------------------------------------------------------------===//
// ObjCPropertyDecl
//===----------------------------------------------------------------------===//

void ObjCPropertyDecl::anchor() {}

ObjCPropertyDecl *
ObjCPropertyDecl::Create(ASTContext &C, DeclContext *DC, SourceLocation L,
                         const IdentifierInfo *Id, SourceLocation AtLoc,
                         SourceLocation LParenLoc, QualType T,
                         TypeSourceInfo *TSI, PropertyControl propControl) {
  return new (C, DC) ObjCPropertyDecl(DC, L, Id, AtLoc, LParenLoc, T, TSI,
                                      propControl);
}

ObjCPropertyDecl *ObjCPropertyDecl::CreateDeserialized(ASTContext &C,
                                                       GlobalDeclID ID) {
  return new (C, ID) ObjCPropertyDecl(nullptr, SourceLocation(), nullptr,
                                      SourceLocation(), SourceLocation(),
                                      QualType(), nullptr, None);
}

QualType ObjCPropertyDecl::getUsageType(QualType objectType) const {
  return DeclType.substObjCMemberType(objectType, getDeclContext(),
                                      ObjCSubstitutionContext::Property);
}

bool ObjCPropertyDecl::isDirectProperty() const {
  return (PropertyAttributes & ObjCPropertyAttribute::kind_direct) &&
         !getASTContext().getLangOpts().ObjCDisableDirectMethodsForTesting;
}

//===----------------------------------------------------------------------===//
// ObjCPropertyImplDecl
//===----------------------------------------------------------------------===//

ObjCPropertyImplDecl *ObjCPropertyImplDecl::Create(ASTContext &C,
                                                   DeclContext *DC,
                                                   SourceLocation atLoc,
                                                   SourceLocation L,
                                                   ObjCPropertyDecl *property,
                                                   Kind PK,
                                                   ObjCIvarDecl *ivar,
                                                   SourceLocation ivarLoc) {
  return new (C, DC) ObjCPropertyImplDecl(DC, atLoc, L, property, PK, ivar,
                                          ivarLoc);
}

ObjCPropertyImplDecl *
ObjCPropertyImplDecl::CreateDeserialized(ASTContext &C, GlobalDeclID ID) {
  return new (C, ID) ObjCPropertyImplDecl(nullptr, SourceLocation(),
                                          SourceLocation(), nullptr, Dynamic,
                                          nullptr, SourceLocation());
}

SourceRange ObjCPropertyImplDecl::getSourceRange() const {
  SourceLocation EndLoc = getLocation();
  if (IvarLoc.isValid())
    EndLoc = IvarLoc;

  return SourceRange(AtLoc, EndLoc);
}
