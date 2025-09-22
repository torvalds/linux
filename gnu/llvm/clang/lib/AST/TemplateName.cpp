//===- TemplateName.cpp - C++ Template Name Representation ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines the TemplateName interface and subclasses.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/TemplateName.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/DependenceFlags.h"
#include "clang/AST/NestedNameSpecifier.h"
#include "clang/AST/PrettyPrinter.h"
#include "clang/AST/TemplateBase.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/OperatorKinds.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <optional>
#include <string>

using namespace clang;

TemplateArgument
SubstTemplateTemplateParmPackStorage::getArgumentPack() const {
  return TemplateArgument(llvm::ArrayRef(Arguments, Bits.Data));
}

TemplateTemplateParmDecl *
SubstTemplateTemplateParmPackStorage::getParameterPack() const {
  return cast<TemplateTemplateParmDecl>(
      getReplacedTemplateParameterList(getAssociatedDecl())
          ->asArray()[Bits.Index]);
}

TemplateTemplateParmDecl *
SubstTemplateTemplateParmStorage::getParameter() const {
  return cast<TemplateTemplateParmDecl>(
      getReplacedTemplateParameterList(getAssociatedDecl())
          ->asArray()[Bits.Index]);
}

void SubstTemplateTemplateParmStorage::Profile(llvm::FoldingSetNodeID &ID) {
  Profile(ID, Replacement, getAssociatedDecl(), getIndex(), getPackIndex());
}

void SubstTemplateTemplateParmStorage::Profile(
    llvm::FoldingSetNodeID &ID, TemplateName Replacement, Decl *AssociatedDecl,
    unsigned Index, std::optional<unsigned> PackIndex) {
  Replacement.Profile(ID);
  ID.AddPointer(AssociatedDecl);
  ID.AddInteger(Index);
  ID.AddInteger(PackIndex ? *PackIndex + 1 : 0);
}

SubstTemplateTemplateParmPackStorage::SubstTemplateTemplateParmPackStorage(
    ArrayRef<TemplateArgument> ArgPack, Decl *AssociatedDecl, unsigned Index,
    bool Final)
    : UncommonTemplateNameStorage(SubstTemplateTemplateParmPack, Index,
                                  ArgPack.size()),
      Arguments(ArgPack.data()), AssociatedDeclAndFinal(AssociatedDecl, Final) {
  assert(AssociatedDecl != nullptr);
}

void SubstTemplateTemplateParmPackStorage::Profile(llvm::FoldingSetNodeID &ID,
                                                   ASTContext &Context) {
  Profile(ID, Context, getArgumentPack(), getAssociatedDecl(), getIndex(),
          getFinal());
}

Decl *SubstTemplateTemplateParmPackStorage::getAssociatedDecl() const {
  return AssociatedDeclAndFinal.getPointer();
}

bool SubstTemplateTemplateParmPackStorage::getFinal() const {
  return AssociatedDeclAndFinal.getInt();
}

void SubstTemplateTemplateParmPackStorage::Profile(
    llvm::FoldingSetNodeID &ID, ASTContext &Context,
    const TemplateArgument &ArgPack, Decl *AssociatedDecl, unsigned Index,
    bool Final) {
  ArgPack.Profile(ID, Context);
  ID.AddPointer(AssociatedDecl);
  ID.AddInteger(Index);
  ID.AddBoolean(Final);
}

TemplateName::TemplateName(void *Ptr) {
  Storage = StorageType::getFromOpaqueValue(Ptr);
}

TemplateName::TemplateName(TemplateDecl *Template) : Storage(Template) {}
TemplateName::TemplateName(OverloadedTemplateStorage *Storage)
    : Storage(Storage) {}
TemplateName::TemplateName(AssumedTemplateStorage *Storage)
    : Storage(Storage) {}
TemplateName::TemplateName(SubstTemplateTemplateParmStorage *Storage)
    : Storage(Storage) {}
TemplateName::TemplateName(SubstTemplateTemplateParmPackStorage *Storage)
    : Storage(Storage) {}
TemplateName::TemplateName(QualifiedTemplateName *Qual) : Storage(Qual) {}
TemplateName::TemplateName(DependentTemplateName *Dep) : Storage(Dep) {}
TemplateName::TemplateName(UsingShadowDecl *Using) : Storage(Using) {}

bool TemplateName::isNull() const { return Storage.isNull(); }

TemplateName::NameKind TemplateName::getKind() const {
  if (auto *ND = Storage.dyn_cast<Decl *>()) {
    if (isa<UsingShadowDecl>(ND))
      return UsingTemplate;
    assert(isa<TemplateDecl>(ND));
    return Template;
  }

  if (Storage.is<DependentTemplateName *>())
    return DependentTemplate;
  if (Storage.is<QualifiedTemplateName *>())
    return QualifiedTemplate;

  UncommonTemplateNameStorage *uncommon
    = Storage.get<UncommonTemplateNameStorage*>();
  if (uncommon->getAsOverloadedStorage())
    return OverloadedTemplate;
  if (uncommon->getAsAssumedTemplateName())
    return AssumedTemplate;
  if (uncommon->getAsSubstTemplateTemplateParm())
    return SubstTemplateTemplateParm;
  return SubstTemplateTemplateParmPack;
}

TemplateDecl *TemplateName::getAsTemplateDecl() const {
  if (Decl *TemplateOrUsing = Storage.dyn_cast<Decl *>()) {
    if (UsingShadowDecl *USD = dyn_cast<UsingShadowDecl>(TemplateOrUsing))
      return cast<TemplateDecl>(USD->getTargetDecl());

    assert(isa<TemplateDecl>(TemplateOrUsing));
    return cast<TemplateDecl>(TemplateOrUsing);
  }

  if (QualifiedTemplateName *QTN = getAsQualifiedTemplateName())
    return QTN->getUnderlyingTemplate().getAsTemplateDecl();

  if (SubstTemplateTemplateParmStorage *sub = getAsSubstTemplateTemplateParm())
    return sub->getReplacement().getAsTemplateDecl();

  if (UsingShadowDecl *USD = getAsUsingShadowDecl())
    return cast<TemplateDecl>(USD->getTargetDecl());

  return nullptr;
}

OverloadedTemplateStorage *TemplateName::getAsOverloadedTemplate() const {
  if (UncommonTemplateNameStorage *Uncommon =
          Storage.dyn_cast<UncommonTemplateNameStorage *>())
    return Uncommon->getAsOverloadedStorage();

  return nullptr;
}

AssumedTemplateStorage *TemplateName::getAsAssumedTemplateName() const {
  if (UncommonTemplateNameStorage *Uncommon =
          Storage.dyn_cast<UncommonTemplateNameStorage *>())
    return Uncommon->getAsAssumedTemplateName();

  return nullptr;
}

SubstTemplateTemplateParmStorage *
TemplateName::getAsSubstTemplateTemplateParm() const {
  if (UncommonTemplateNameStorage *uncommon =
          Storage.dyn_cast<UncommonTemplateNameStorage *>())
    return uncommon->getAsSubstTemplateTemplateParm();

  return nullptr;
}

SubstTemplateTemplateParmPackStorage *
TemplateName::getAsSubstTemplateTemplateParmPack() const {
  if (UncommonTemplateNameStorage *Uncommon =
          Storage.dyn_cast<UncommonTemplateNameStorage *>())
    return Uncommon->getAsSubstTemplateTemplateParmPack();

  return nullptr;
}

QualifiedTemplateName *TemplateName::getAsQualifiedTemplateName() const {
  return Storage.dyn_cast<QualifiedTemplateName *>();
}

DependentTemplateName *TemplateName::getAsDependentTemplateName() const {
  return Storage.dyn_cast<DependentTemplateName *>();
}

UsingShadowDecl *TemplateName::getAsUsingShadowDecl() const {
  if (Decl *D = Storage.dyn_cast<Decl *>())
    if (UsingShadowDecl *USD = dyn_cast<UsingShadowDecl>(D))
      return USD;
  if (QualifiedTemplateName *QTN = getAsQualifiedTemplateName())
    return QTN->getUnderlyingTemplate().getAsUsingShadowDecl();
  return nullptr;
}

TemplateNameDependence TemplateName::getDependence() const {
  auto D = TemplateNameDependence::None;
  switch (getKind()) {
  case TemplateName::NameKind::QualifiedTemplate:
    if (NestedNameSpecifier *NNS = getAsQualifiedTemplateName()->getQualifier())
      D |= toTemplateNameDependence(NNS->getDependence());
    break;
  case TemplateName::NameKind::DependentTemplate:
    D |= toTemplateNameDependence(
        getAsDependentTemplateName()->getQualifier()->getDependence());
    break;
  case TemplateName::NameKind::SubstTemplateTemplateParmPack:
    D |= TemplateNameDependence::UnexpandedPack;
    break;
  case TemplateName::NameKind::OverloadedTemplate:
    llvm_unreachable("overloaded templates shouldn't survive to here.");
  default:
    break;
  }
  if (TemplateDecl *Template = getAsTemplateDecl()) {
    if (auto *TTP = dyn_cast<TemplateTemplateParmDecl>(Template)) {
      D |= TemplateNameDependence::DependentInstantiation;
      if (TTP->isParameterPack())
        D |= TemplateNameDependence::UnexpandedPack;
    }
    // FIXME: Hack, getDeclContext() can be null if Template is still
    // initializing due to PCH reading, so we check it before using it.
    // Should probably modify TemplateSpecializationType to allow constructing
    // it without the isDependent() checking.
    if (Template->getDeclContext() &&
        Template->getDeclContext()->isDependentContext())
      D |= TemplateNameDependence::DependentInstantiation;
  } else {
    D |= TemplateNameDependence::DependentInstantiation;
  }
  return D;
}

bool TemplateName::isDependent() const {
  return getDependence() & TemplateNameDependence::Dependent;
}

bool TemplateName::isInstantiationDependent() const {
  return getDependence() & TemplateNameDependence::Instantiation;
}

bool TemplateName::containsUnexpandedParameterPack() const {
  return getDependence() & TemplateNameDependence::UnexpandedPack;
}

void TemplateName::Profile(llvm::FoldingSetNodeID &ID) {
  if (const auto* USD = getAsUsingShadowDecl())
    ID.AddPointer(USD->getCanonicalDecl());
  else if (const auto *TD = getAsTemplateDecl())
    ID.AddPointer(TD->getCanonicalDecl());
  else
    ID.AddPointer(Storage.getOpaqueValue());
}

void TemplateName::print(raw_ostream &OS, const PrintingPolicy &Policy,
                         Qualified Qual) const {
  auto handleAnonymousTTP = [](TemplateDecl *TD, raw_ostream &OS) {
    if (TemplateTemplateParmDecl *TTP = dyn_cast<TemplateTemplateParmDecl>(TD);
        TTP && TTP->getIdentifier() == nullptr) {
      OS << "template-parameter-" << TTP->getDepth() << "-" << TTP->getIndex();
      return true;
    }
    return false;
  };
  if (NameKind Kind = getKind();
      Kind == TemplateName::Template || Kind == TemplateName::UsingTemplate) {
    // After `namespace ns { using std::vector }`, what is the fully-qualified
    // name of the UsingTemplateName `vector` within ns?
    //
    // - ns::vector (the qualified name of the using-shadow decl)
    // - std::vector (the qualified name of the underlying template decl)
    //
    // Similar to the UsingType behavior, using declarations are used to import
    // names more often than to export them, thus using the original name is
    // most useful in this case.
    TemplateDecl *Template = getAsTemplateDecl();
    if (handleAnonymousTTP(Template, OS))
      return;
    if (Qual == Qualified::None)
      OS << *Template;
    else
      Template->printQualifiedName(OS, Policy);
  } else if (QualifiedTemplateName *QTN = getAsQualifiedTemplateName()) {
    if (NestedNameSpecifier *NNS = QTN->getQualifier();
        Qual != Qualified::None && NNS)
      NNS->print(OS, Policy);
    if (QTN->hasTemplateKeyword())
      OS << "template ";

    TemplateName Underlying = QTN->getUnderlyingTemplate();
    assert(Underlying.getKind() == TemplateName::Template ||
           Underlying.getKind() == TemplateName::UsingTemplate);

    TemplateDecl *UTD = Underlying.getAsTemplateDecl();

    if (handleAnonymousTTP(UTD, OS))
      return;

    if (IdentifierInfo *II = UTD->getIdentifier();
        Policy.CleanUglifiedParameters && II &&
        isa<TemplateTemplateParmDecl>(UTD))
      OS << II->deuglifiedName();
    else
      OS << *UTD;
  } else if (DependentTemplateName *DTN = getAsDependentTemplateName()) {
    if (NestedNameSpecifier *NNS = DTN->getQualifier())
      NNS->print(OS, Policy);
    OS << "template ";

    if (DTN->isIdentifier())
      OS << DTN->getIdentifier()->getName();
    else
      OS << "operator " << getOperatorSpelling(DTN->getOperator());
  } else if (SubstTemplateTemplateParmStorage *subst =
                 getAsSubstTemplateTemplateParm()) {
    subst->getReplacement().print(OS, Policy, Qual);
  } else if (SubstTemplateTemplateParmPackStorage *SubstPack =
                 getAsSubstTemplateTemplateParmPack())
    OS << *SubstPack->getParameterPack();
  else if (AssumedTemplateStorage *Assumed = getAsAssumedTemplateName()) {
    Assumed->getDeclName().print(OS, Policy);
  } else {
    assert(getKind() == TemplateName::OverloadedTemplate);
    OverloadedTemplateStorage *OTS = getAsOverloadedTemplate();
    (*OTS->begin())->printName(OS, Policy);
  }
}

const StreamingDiagnostic &clang::operator<<(const StreamingDiagnostic &DB,
                                             TemplateName N) {
  std::string NameStr;
  llvm::raw_string_ostream OS(NameStr);
  LangOptions LO;
  LO.CPlusPlus = true;
  LO.Bool = true;
  OS << '\'';
  N.print(OS, PrintingPolicy(LO));
  OS << '\'';
  OS.flush();
  return DB << NameStr;
}
