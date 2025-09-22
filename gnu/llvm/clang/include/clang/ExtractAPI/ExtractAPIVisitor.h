//===- ExtractAPI/ExtractAPIVisitor.h ---------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines the ExtractAPVisitor AST visitation interface.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_EXTRACTAPI_EXTRACT_API_VISITOR_H
#define LLVM_CLANG_EXTRACTAPI_EXTRACT_API_VISITOR_H

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/Module.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/Specifiers.h"
#include "clang/ExtractAPI/API.h"
#include "clang/ExtractAPI/DeclarationFragments.h"
#include "clang/ExtractAPI/TypedefUnderlyingTypeResolver.h"
#include "clang/Index/USRGeneration.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Casting.h"
#include <type_traits>

namespace clang {
namespace extractapi {
namespace impl {

template <typename Derived>
class ExtractAPIVisitorBase : public RecursiveASTVisitor<Derived> {
protected:
  ExtractAPIVisitorBase(ASTContext &Context, APISet &API)
      : Context(Context), API(API) {}

public:
  const APISet &getAPI() const { return API; }

  bool VisitVarDecl(const VarDecl *Decl);

  bool VisitFunctionDecl(const FunctionDecl *Decl);

  bool VisitEnumDecl(const EnumDecl *Decl);

  bool WalkUpFromFunctionDecl(const FunctionDecl *Decl);

  bool WalkUpFromRecordDecl(const RecordDecl *Decl);

  bool WalkUpFromCXXRecordDecl(const CXXRecordDecl *Decl);

  bool WalkUpFromCXXMethodDecl(const CXXMethodDecl *Decl);

  bool WalkUpFromClassTemplateSpecializationDecl(
      const ClassTemplateSpecializationDecl *Decl);

  bool WalkUpFromClassTemplatePartialSpecializationDecl(
      const ClassTemplatePartialSpecializationDecl *Decl);

  bool WalkUpFromVarTemplateDecl(const VarTemplateDecl *Decl);

  bool WalkUpFromVarTemplateSpecializationDecl(
      const VarTemplateSpecializationDecl *Decl);

  bool WalkUpFromVarTemplatePartialSpecializationDecl(
      const VarTemplatePartialSpecializationDecl *Decl);

  bool WalkUpFromFunctionTemplateDecl(const FunctionTemplateDecl *Decl);

  bool WalkUpFromNamespaceDecl(const NamespaceDecl *Decl);

  bool VisitNamespaceDecl(const NamespaceDecl *Decl);

  bool VisitRecordDecl(const RecordDecl *Decl);

  bool VisitCXXRecordDecl(const CXXRecordDecl *Decl);

  bool VisitCXXMethodDecl(const CXXMethodDecl *Decl);

  bool VisitFieldDecl(const FieldDecl *Decl);

  bool VisitCXXConversionDecl(const CXXConversionDecl *Decl);

  bool VisitCXXConstructorDecl(const CXXConstructorDecl *Decl);

  bool VisitCXXDestructorDecl(const CXXDestructorDecl *Decl);

  bool VisitConceptDecl(const ConceptDecl *Decl);

  bool VisitClassTemplateSpecializationDecl(
      const ClassTemplateSpecializationDecl *Decl);

  bool VisitClassTemplatePartialSpecializationDecl(
      const ClassTemplatePartialSpecializationDecl *Decl);

  bool VisitVarTemplateDecl(const VarTemplateDecl *Decl);

  bool
  VisitVarTemplateSpecializationDecl(const VarTemplateSpecializationDecl *Decl);

  bool VisitVarTemplatePartialSpecializationDecl(
      const VarTemplatePartialSpecializationDecl *Decl);

  bool VisitFunctionTemplateDecl(const FunctionTemplateDecl *Decl);

  bool VisitObjCInterfaceDecl(const ObjCInterfaceDecl *Decl);

  bool VisitObjCProtocolDecl(const ObjCProtocolDecl *Decl);

  bool VisitTypedefNameDecl(const TypedefNameDecl *Decl);

  bool VisitObjCCategoryDecl(const ObjCCategoryDecl *Decl);

  bool shouldDeclBeIncluded(const Decl *Decl) const;

  const RawComment *fetchRawCommentForDecl(const Decl *Decl) const;

protected:
  /// Collect API information for the enum constants and associate with the
  /// parent enum.
  void recordEnumConstants(SymbolReference Container,
                           const EnumDecl::enumerator_range Constants);

  /// Collect API information for the Objective-C methods and associate with the
  /// parent container.
  void recordObjCMethods(ObjCContainerRecord *Container,
                         const ObjCContainerDecl::method_range Methods);

  void recordObjCProperties(ObjCContainerRecord *Container,
                            const ObjCContainerDecl::prop_range Properties);

  void recordObjCInstanceVariables(
      ObjCContainerRecord *Container,
      const llvm::iterator_range<
          DeclContext::specific_decl_iterator<ObjCIvarDecl>>
          Ivars);

  void recordObjCProtocols(ObjCContainerRecord *Container,
                           ObjCInterfaceDecl::protocol_range Protocols);

  ASTContext &Context;
  APISet &API;

  StringRef getTypedefName(const TagDecl *Decl) {
    if (const auto *TypedefDecl = Decl->getTypedefNameForAnonDecl())
      return TypedefDecl->getName();

    return {};
  }

  bool isInSystemHeader(const Decl *D) {
    return Context.getSourceManager().isInSystemHeader(D->getLocation());
  }

private:
  Derived &getDerivedExtractAPIVisitor() {
    return *static_cast<Derived *>(this);
  }

protected:
  SmallVector<SymbolReference> getBases(const CXXRecordDecl *Decl) {
    // FIXME: store AccessSpecifier given by inheritance
    SmallVector<SymbolReference> Bases;
    for (const auto &BaseSpecifier : Decl->bases()) {
      // skip classes not inherited as public
      if (BaseSpecifier.getAccessSpecifier() != AccessSpecifier::AS_public)
        continue;
      if (auto *BaseDecl = BaseSpecifier.getType()->getAsTagDecl()) {
        Bases.emplace_back(createSymbolReferenceForDecl(*BaseDecl));
      } else {
        SymbolReference BaseClass;
        BaseClass.Name = API.copyString(BaseSpecifier.getType().getAsString(
            Decl->getASTContext().getPrintingPolicy()));

        if (BaseSpecifier.getType().getTypePtr()->isTemplateTypeParmType()) {
          if (auto *TTPTD = BaseSpecifier.getType()
                                ->getAs<TemplateTypeParmType>()
                                ->getDecl()) {
            SmallString<128> USR;
            index::generateUSRForDecl(TTPTD, USR);
            BaseClass.USR = API.copyString(USR);
            BaseClass.Source = API.copyString(getOwningModuleName(*TTPTD));
          }
        }
        Bases.emplace_back(BaseClass);
      }
    }
    return Bases;
  }

  APIRecord::RecordKind getKindForDisplay(const CXXRecordDecl *Decl) {
    if (Decl->isUnion())
      return APIRecord::RK_Union;
    if (Decl->isStruct())
      return APIRecord::RK_Struct;

    return APIRecord::RK_CXXClass;
  }

  StringRef getOwningModuleName(const Decl &D) {
    if (auto *OwningModule = D.getImportedOwningModule())
      return OwningModule->Name;

    return {};
  }

  SymbolReference createHierarchyInformationForDecl(const Decl &D) {
    const auto *Context = cast_if_present<Decl>(D.getDeclContext());

    if (!Context || isa<TranslationUnitDecl>(Context))
      return {};

    return createSymbolReferenceForDecl(*Context);
  }

  SymbolReference createSymbolReferenceForDecl(const Decl &D) {
    SmallString<128> USR;
    index::generateUSRForDecl(&D, USR);

    APIRecord *Record = API.findRecordForUSR(USR);
    if (Record)
      return SymbolReference(Record);

    StringRef Name;
    if (auto *ND = dyn_cast<NamedDecl>(&D))
      Name = ND->getName();

    return API.createSymbolReference(Name, USR, getOwningModuleName(D));
  }

  bool isEmbeddedInVarDeclarator(const TagDecl &D) {
    return D.getName().empty() && getTypedefName(&D).empty() &&
           D.isEmbeddedInDeclarator();
  }

  void maybeMergeWithAnonymousTag(const DeclaratorDecl &D,
                                  RecordContext *NewRecordContext) {
    if (!NewRecordContext)
      return;
    auto *Tag = D.getType()->getAsTagDecl();
    SmallString<128> TagUSR;
    clang::index::generateUSRForDecl(Tag, TagUSR);
    if (auto *Record = llvm::dyn_cast_if_present<TagRecord>(
            API.findRecordForUSR(TagUSR))) {
      if (Record->IsEmbeddedInVarDeclarator)
        NewRecordContext->stealRecordChain(*Record);
    }
  }
};

template <typename Derived>
bool ExtractAPIVisitorBase<Derived>::VisitVarDecl(const VarDecl *Decl) {
  // skip function parameters.
  if (isa<ParmVarDecl>(Decl))
    return true;

  // Skip non-global variables in records (struct/union/class) but not static
  // members.
  if (Decl->getDeclContext()->isRecord() && !Decl->isStaticDataMember())
    return true;

  // Skip local variables inside function or method.
  if (!Decl->isDefinedOutsideFunctionOrMethod())
    return true;

  // If this is a template but not specialization or instantiation, skip.
  if (Decl->getASTContext().getTemplateOrSpecializationInfo(Decl) &&
      Decl->getTemplateSpecializationKind() == TSK_Undeclared)
    return true;

  if (!getDerivedExtractAPIVisitor().shouldDeclBeIncluded(Decl))
    return true;

  // Collect symbol information.
  StringRef Name = Decl->getName();
  SmallString<128> USR;
  index::generateUSRForDecl(Decl, USR);
  PresumedLoc Loc =
      Context.getSourceManager().getPresumedLoc(Decl->getLocation());
  LinkageInfo Linkage = Decl->getLinkageAndVisibility();
  DocComment Comment;
  if (auto *RawComment =
          getDerivedExtractAPIVisitor().fetchRawCommentForDecl(Decl))
    Comment = RawComment->getFormattedLines(Context.getSourceManager(),
                                            Context.getDiagnostics());

  // Build declaration fragments and sub-heading for the variable.
  DeclarationFragments Declaration =
      DeclarationFragmentsBuilder::getFragmentsForVar(Decl);
  DeclarationFragments SubHeading =
      DeclarationFragmentsBuilder::getSubHeading(Decl);
  if (Decl->isStaticDataMember()) {
    auto Access = DeclarationFragmentsBuilder::getAccessControl(Decl);
    API.createRecord<StaticFieldRecord>(
        USR, Name, createHierarchyInformationForDecl(*Decl), Loc,
        AvailabilityInfo::createFromDecl(Decl), Linkage, Comment, Declaration,
        SubHeading, Access, isInSystemHeader(Decl));
  } else {
    // Add the global variable record to the API set.
    auto *NewRecord = API.createRecord<GlobalVariableRecord>(
        USR, Name, createHierarchyInformationForDecl(*Decl), Loc,
        AvailabilityInfo::createFromDecl(Decl), Linkage, Comment, Declaration,
        SubHeading, isInSystemHeader(Decl));

    // If this global variable has a non typedef'd anonymous tag type let's
    // pretend the type's child records are under us in the hierarchy.
    maybeMergeWithAnonymousTag(*Decl, NewRecord);
  }

  return true;
}

template <typename Derived>
bool ExtractAPIVisitorBase<Derived>::VisitFunctionDecl(
    const FunctionDecl *Decl) {
  if (const auto *Method = dyn_cast<CXXMethodDecl>(Decl)) {
    // Skip member function in class templates.
    if (Method->getParent()->getDescribedClassTemplate() != nullptr)
      return true;

    // Skip methods in records.
    for (const auto &P : Context.getParents(*Method)) {
      if (P.template get<CXXRecordDecl>())
        return true;
    }

    // Skip ConstructorDecl and DestructorDecl.
    if (isa<CXXConstructorDecl>(Method) || isa<CXXDestructorDecl>(Method))
      return true;
  }

  // Skip templated functions that aren't processed here.
  switch (Decl->getTemplatedKind()) {
  case FunctionDecl::TK_NonTemplate:
  case FunctionDecl::TK_DependentNonTemplate:
  case FunctionDecl::TK_FunctionTemplateSpecialization:
    break;
  case FunctionDecl::TK_FunctionTemplate:
  case FunctionDecl::TK_DependentFunctionTemplateSpecialization:
  case FunctionDecl::TK_MemberSpecialization:
    return true;
  }

  if (!getDerivedExtractAPIVisitor().shouldDeclBeIncluded(Decl))
    return true;

  // Collect symbol information.
  auto Name = Decl->getNameAsString();
  SmallString<128> USR;
  index::generateUSRForDecl(Decl, USR);
  PresumedLoc Loc =
      Context.getSourceManager().getPresumedLoc(Decl->getLocation());
  LinkageInfo Linkage = Decl->getLinkageAndVisibility();
  DocComment Comment;
  if (auto *RawComment =
          getDerivedExtractAPIVisitor().fetchRawCommentForDecl(Decl))
    Comment = RawComment->getFormattedLines(Context.getSourceManager(),
                                            Context.getDiagnostics());

  // Build declaration fragments, sub-heading, and signature of the function.
  DeclarationFragments SubHeading =
      DeclarationFragmentsBuilder::getSubHeading(Decl);
  FunctionSignature Signature =
      DeclarationFragmentsBuilder::getFunctionSignature(Decl);
  if (Decl->getTemplateSpecializationInfo())
    API.createRecord<GlobalFunctionTemplateSpecializationRecord>(
        USR, Name, createHierarchyInformationForDecl(*Decl), Loc,
        AvailabilityInfo::createFromDecl(Decl), Linkage, Comment,
        DeclarationFragmentsBuilder::
            getFragmentsForFunctionTemplateSpecialization(Decl),
        SubHeading, Signature, isInSystemHeader(Decl));
  else
    // Add the function record to the API set.
    API.createRecord<GlobalFunctionRecord>(
        USR, Name, createHierarchyInformationForDecl(*Decl), Loc,
        AvailabilityInfo::createFromDecl(Decl), Linkage, Comment,
        DeclarationFragmentsBuilder::getFragmentsForFunction(Decl), SubHeading,
        Signature, isInSystemHeader(Decl));
  return true;
}

template <typename Derived>
bool ExtractAPIVisitorBase<Derived>::VisitEnumDecl(const EnumDecl *Decl) {
  if (!getDerivedExtractAPIVisitor().shouldDeclBeIncluded(Decl))
    return true;

  SmallString<128> USR;
  index::generateUSRForDecl(Decl, USR);
  PresumedLoc Loc =
      Context.getSourceManager().getPresumedLoc(Decl->getLocation());
  DocComment Comment;
  if (auto *RawComment =
          getDerivedExtractAPIVisitor().fetchRawCommentForDecl(Decl))
    Comment = RawComment->getFormattedLines(Context.getSourceManager(),
                                            Context.getDiagnostics());

  // Build declaration fragments and sub-heading for the enum.
  DeclarationFragments Declaration =
      DeclarationFragmentsBuilder::getFragmentsForEnum(Decl);
  DeclarationFragments SubHeading =
      DeclarationFragmentsBuilder::getSubHeading(Decl);

  // Collect symbol information.
  SymbolReference ParentContainer;

  if (Decl->hasNameForLinkage()) {
    StringRef Name = Decl->getName();
    if (Name.empty())
      Name = getTypedefName(Decl);

    auto *ER = API.createRecord<EnumRecord>(
        USR, Name, createHierarchyInformationForDecl(*Decl), Loc,
        AvailabilityInfo::createFromDecl(Decl), Comment, Declaration,
        SubHeading, isInSystemHeader(Decl), false);
    ParentContainer = SymbolReference(ER);
  } else {
    // If this an anonymous enum then the parent scope of the constants is the
    // top level namespace.
    ParentContainer = {};
  }

  // Now collect information about the enumerators in this enum.
  getDerivedExtractAPIVisitor().recordEnumConstants(ParentContainer,
                                                    Decl->enumerators());

  return true;
}

template <typename Derived>
bool ExtractAPIVisitorBase<Derived>::WalkUpFromFunctionDecl(
    const FunctionDecl *Decl) {
  getDerivedExtractAPIVisitor().VisitFunctionDecl(Decl);
  return true;
}

template <typename Derived>
bool ExtractAPIVisitorBase<Derived>::WalkUpFromRecordDecl(
    const RecordDecl *Decl) {
  getDerivedExtractAPIVisitor().VisitRecordDecl(Decl);
  return true;
}

template <typename Derived>
bool ExtractAPIVisitorBase<Derived>::WalkUpFromCXXRecordDecl(
    const CXXRecordDecl *Decl) {
  getDerivedExtractAPIVisitor().VisitCXXRecordDecl(Decl);
  return true;
}

template <typename Derived>
bool ExtractAPIVisitorBase<Derived>::WalkUpFromCXXMethodDecl(
    const CXXMethodDecl *Decl) {
  getDerivedExtractAPIVisitor().VisitCXXMethodDecl(Decl);
  return true;
}

template <typename Derived>
bool ExtractAPIVisitorBase<Derived>::WalkUpFromClassTemplateSpecializationDecl(
    const ClassTemplateSpecializationDecl *Decl) {
  getDerivedExtractAPIVisitor().VisitClassTemplateSpecializationDecl(Decl);
  return true;
}

template <typename Derived>
bool ExtractAPIVisitorBase<Derived>::
    WalkUpFromClassTemplatePartialSpecializationDecl(
        const ClassTemplatePartialSpecializationDecl *Decl) {
  getDerivedExtractAPIVisitor().VisitClassTemplatePartialSpecializationDecl(
      Decl);
  return true;
}

template <typename Derived>
bool ExtractAPIVisitorBase<Derived>::WalkUpFromVarTemplateDecl(
    const VarTemplateDecl *Decl) {
  getDerivedExtractAPIVisitor().VisitVarTemplateDecl(Decl);
  return true;
}

template <typename Derived>
bool ExtractAPIVisitorBase<Derived>::WalkUpFromVarTemplateSpecializationDecl(
    const VarTemplateSpecializationDecl *Decl) {
  getDerivedExtractAPIVisitor().VisitVarTemplateSpecializationDecl(Decl);
  return true;
}

template <typename Derived>
bool ExtractAPIVisitorBase<Derived>::
    WalkUpFromVarTemplatePartialSpecializationDecl(
        const VarTemplatePartialSpecializationDecl *Decl) {
  getDerivedExtractAPIVisitor().VisitVarTemplatePartialSpecializationDecl(Decl);
  return true;
}

template <typename Derived>
bool ExtractAPIVisitorBase<Derived>::WalkUpFromFunctionTemplateDecl(
    const FunctionTemplateDecl *Decl) {
  getDerivedExtractAPIVisitor().VisitFunctionTemplateDecl(Decl);
  return true;
}

template <typename Derived>
bool ExtractAPIVisitorBase<Derived>::WalkUpFromNamespaceDecl(
    const NamespaceDecl *Decl) {
  getDerivedExtractAPIVisitor().VisitNamespaceDecl(Decl);
  return true;
}

template <typename Derived>
bool ExtractAPIVisitorBase<Derived>::VisitNamespaceDecl(
    const NamespaceDecl *Decl) {
  if (!getDerivedExtractAPIVisitor().shouldDeclBeIncluded(Decl))
    return true;
  if (Decl->isAnonymousNamespace())
    return true;
  StringRef Name = Decl->getName();
  SmallString<128> USR;
  index::generateUSRForDecl(Decl, USR);
  LinkageInfo Linkage = Decl->getLinkageAndVisibility();
  PresumedLoc Loc =
      Context.getSourceManager().getPresumedLoc(Decl->getLocation());
  DocComment Comment;
  if (auto *RawComment =
          getDerivedExtractAPIVisitor().fetchRawCommentForDecl(Decl))
    Comment = RawComment->getFormattedLines(Context.getSourceManager(),
                                            Context.getDiagnostics());

  // Build declaration fragments and sub-heading for the struct.
  DeclarationFragments Declaration =
      DeclarationFragmentsBuilder::getFragmentsForNamespace(Decl);
  DeclarationFragments SubHeading =
      DeclarationFragmentsBuilder::getSubHeading(Decl);
  API.createRecord<NamespaceRecord>(
      USR, Name, createHierarchyInformationForDecl(*Decl), Loc,
      AvailabilityInfo::createFromDecl(Decl), Linkage, Comment, Declaration,
      SubHeading, isInSystemHeader(Decl));

  return true;
}

template <typename Derived>
bool ExtractAPIVisitorBase<Derived>::VisitRecordDecl(const RecordDecl *Decl) {
  if (!getDerivedExtractAPIVisitor().shouldDeclBeIncluded(Decl))
    return true;

  // Collect symbol information.
  StringRef Name = Decl->getName();
  if (Name.empty())
    Name = getTypedefName(Decl);

  SmallString<128> USR;
  index::generateUSRForDecl(Decl, USR);
  PresumedLoc Loc =
      Context.getSourceManager().getPresumedLoc(Decl->getLocation());
  DocComment Comment;
  if (auto *RawComment =
          getDerivedExtractAPIVisitor().fetchRawCommentForDecl(Decl))
    Comment = RawComment->getFormattedLines(Context.getSourceManager(),
                                            Context.getDiagnostics());

  // Build declaration fragments and sub-heading for the struct.
  DeclarationFragments Declaration =
      DeclarationFragmentsBuilder::getFragmentsForRecordDecl(Decl);
  DeclarationFragments SubHeading =
      DeclarationFragmentsBuilder::getSubHeading(Decl);

  if (Decl->isUnion())
    API.createRecord<UnionRecord>(
        USR, Name, createHierarchyInformationForDecl(*Decl), Loc,
        AvailabilityInfo::createFromDecl(Decl), Comment, Declaration,
        SubHeading, isInSystemHeader(Decl), isEmbeddedInVarDeclarator(*Decl));
  else
    API.createRecord<StructRecord>(
        USR, Name, createHierarchyInformationForDecl(*Decl), Loc,
        AvailabilityInfo::createFromDecl(Decl), Comment, Declaration,
        SubHeading, isInSystemHeader(Decl), isEmbeddedInVarDeclarator(*Decl));

  return true;
}

template <typename Derived>
bool ExtractAPIVisitorBase<Derived>::VisitCXXRecordDecl(
    const CXXRecordDecl *Decl) {
  if (!getDerivedExtractAPIVisitor().shouldDeclBeIncluded(Decl) ||
      Decl->isImplicit())
    return true;

  StringRef Name = Decl->getName();
  if (Name.empty())
    Name = getTypedefName(Decl);

  SmallString<128> USR;
  index::generateUSRForDecl(Decl, USR);
  PresumedLoc Loc =
      Context.getSourceManager().getPresumedLoc(Decl->getLocation());
  DocComment Comment;
  if (auto *RawComment =
          getDerivedExtractAPIVisitor().fetchRawCommentForDecl(Decl))
    Comment = RawComment->getFormattedLines(Context.getSourceManager(),
                                            Context.getDiagnostics());
  DeclarationFragments Declaration =
      DeclarationFragmentsBuilder::getFragmentsForCXXClass(Decl);
  DeclarationFragments SubHeading =
      DeclarationFragmentsBuilder::getSubHeading(Decl);

  auto Access = DeclarationFragmentsBuilder::getAccessControl(Decl);

  CXXClassRecord *Record;
  if (Decl->getDescribedClassTemplate()) {
    // Inject template fragments before class fragments.
    Declaration.prepend(
        DeclarationFragmentsBuilder::getFragmentsForRedeclarableTemplate(
            Decl->getDescribedClassTemplate()));
    Record = API.createRecord<ClassTemplateRecord>(
        USR, Name, createHierarchyInformationForDecl(*Decl), Loc,
        AvailabilityInfo::createFromDecl(Decl), Comment, Declaration,
        SubHeading, Template(Decl->getDescribedClassTemplate()), Access,
        isInSystemHeader(Decl));
  } else {
    Record = API.createRecord<CXXClassRecord>(
        USR, Name, createHierarchyInformationForDecl(*Decl), Loc,
        AvailabilityInfo::createFromDecl(Decl), Comment, Declaration,
        SubHeading, APIRecord::RecordKind::RK_CXXClass, Access,
        isInSystemHeader(Decl), isEmbeddedInVarDeclarator(*Decl));
  }

  Record->KindForDisplay = getKindForDisplay(Decl);
  Record->Bases = getBases(Decl);

  return true;
}

template <typename Derived>
bool ExtractAPIVisitorBase<Derived>::VisitCXXMethodDecl(
    const CXXMethodDecl *Decl) {
  if (!getDerivedExtractAPIVisitor().shouldDeclBeIncluded(Decl) ||
      Decl->isImplicit())
    return true;

  if (isa<CXXConversionDecl>(Decl))
    return true;
  if (isa<CXXConstructorDecl>(Decl) || isa<CXXDestructorDecl>(Decl))
    return true;

  SmallString<128> USR;
  index::generateUSRForDecl(Decl, USR);
  PresumedLoc Loc =
      Context.getSourceManager().getPresumedLoc(Decl->getLocation());
  DocComment Comment;
  if (auto *RawComment =
          getDerivedExtractAPIVisitor().fetchRawCommentForDecl(Decl))
    Comment = RawComment->getFormattedLines(Context.getSourceManager(),
                                            Context.getDiagnostics());
  DeclarationFragments SubHeading =
      DeclarationFragmentsBuilder::getSubHeading(Decl);
  auto Access = DeclarationFragmentsBuilder::getAccessControl(Decl);
  auto Signature = DeclarationFragmentsBuilder::getFunctionSignature(Decl);

  if (FunctionTemplateDecl *TemplateDecl =
          Decl->getDescribedFunctionTemplate()) {
    API.createRecord<CXXMethodTemplateRecord>(
        USR, Decl->getNameAsString(), createHierarchyInformationForDecl(*Decl),
        Loc, AvailabilityInfo::createFromDecl(Decl), Comment,
        DeclarationFragmentsBuilder::getFragmentsForFunctionTemplate(
            TemplateDecl),
        SubHeading, DeclarationFragmentsBuilder::getFunctionSignature(Decl),
        DeclarationFragmentsBuilder::getAccessControl(TemplateDecl),
        Template(TemplateDecl), isInSystemHeader(Decl));
  } else if (Decl->getTemplateSpecializationInfo())
    API.createRecord<CXXMethodTemplateSpecializationRecord>(
        USR, Decl->getNameAsString(), createHierarchyInformationForDecl(*Decl),
        Loc, AvailabilityInfo::createFromDecl(Decl), Comment,
        DeclarationFragmentsBuilder::
            getFragmentsForFunctionTemplateSpecialization(Decl),
        SubHeading, Signature, Access, isInSystemHeader(Decl));
  else if (Decl->isOverloadedOperator())
    API.createRecord<CXXInstanceMethodRecord>(
        USR, Decl->getNameAsString(), createHierarchyInformationForDecl(*Decl),
        Loc, AvailabilityInfo::createFromDecl(Decl), Comment,
        DeclarationFragmentsBuilder::getFragmentsForOverloadedOperator(Decl),
        SubHeading, Signature, Access, isInSystemHeader(Decl));
  else if (Decl->isStatic())
    API.createRecord<CXXStaticMethodRecord>(
        USR, Decl->getNameAsString(), createHierarchyInformationForDecl(*Decl),
        Loc, AvailabilityInfo::createFromDecl(Decl), Comment,
        DeclarationFragmentsBuilder::getFragmentsForCXXMethod(Decl), SubHeading,
        Signature, Access, isInSystemHeader(Decl));
  else
    API.createRecord<CXXInstanceMethodRecord>(
        USR, Decl->getNameAsString(), createHierarchyInformationForDecl(*Decl),
        Loc, AvailabilityInfo::createFromDecl(Decl), Comment,
        DeclarationFragmentsBuilder::getFragmentsForCXXMethod(Decl), SubHeading,
        Signature, Access, isInSystemHeader(Decl));

  return true;
}

template <typename Derived>
bool ExtractAPIVisitorBase<Derived>::VisitCXXConstructorDecl(
    const CXXConstructorDecl *Decl) {
  if (!getDerivedExtractAPIVisitor().shouldDeclBeIncluded(Decl) ||
      Decl->isImplicit())
    return true;

  auto Name = Decl->getNameAsString();
  SmallString<128> USR;
  index::generateUSRForDecl(Decl, USR);
  PresumedLoc Loc =
      Context.getSourceManager().getPresumedLoc(Decl->getLocation());
  DocComment Comment;
  if (auto *RawComment =
          getDerivedExtractAPIVisitor().fetchRawCommentForDecl(Decl))
    Comment = RawComment->getFormattedLines(Context.getSourceManager(),
                                            Context.getDiagnostics());

  // Build declaration fragments, sub-heading, and signature for the method.
  DeclarationFragments Declaration =
      DeclarationFragmentsBuilder::getFragmentsForSpecialCXXMethod(Decl);
  DeclarationFragments SubHeading =
      DeclarationFragmentsBuilder::getSubHeading(Decl);
  FunctionSignature Signature =
      DeclarationFragmentsBuilder::getFunctionSignature(Decl);
  AccessControl Access = DeclarationFragmentsBuilder::getAccessControl(Decl);

  API.createRecord<CXXConstructorRecord>(
      USR, Name, createHierarchyInformationForDecl(*Decl), Loc,
      AvailabilityInfo::createFromDecl(Decl), Comment, Declaration, SubHeading,
      Signature, Access, isInSystemHeader(Decl));
  return true;
}

template <typename Derived>
bool ExtractAPIVisitorBase<Derived>::VisitCXXDestructorDecl(
    const CXXDestructorDecl *Decl) {
  if (!getDerivedExtractAPIVisitor().shouldDeclBeIncluded(Decl) ||
      Decl->isImplicit())
    return true;

  auto Name = Decl->getNameAsString();
  SmallString<128> USR;
  index::generateUSRForDecl(Decl, USR);
  PresumedLoc Loc =
      Context.getSourceManager().getPresumedLoc(Decl->getLocation());
  DocComment Comment;
  if (auto *RawComment =
          getDerivedExtractAPIVisitor().fetchRawCommentForDecl(Decl))
    Comment = RawComment->getFormattedLines(Context.getSourceManager(),
                                            Context.getDiagnostics());

  // Build declaration fragments, sub-heading, and signature for the method.
  DeclarationFragments Declaration =
      DeclarationFragmentsBuilder::getFragmentsForSpecialCXXMethod(Decl);
  DeclarationFragments SubHeading =
      DeclarationFragmentsBuilder::getSubHeading(Decl);
  FunctionSignature Signature =
      DeclarationFragmentsBuilder::getFunctionSignature(Decl);
  AccessControl Access = DeclarationFragmentsBuilder::getAccessControl(Decl);
  API.createRecord<CXXDestructorRecord>(
      USR, Name, createHierarchyInformationForDecl(*Decl), Loc,
      AvailabilityInfo::createFromDecl(Decl), Comment, Declaration, SubHeading,
      Signature, Access, isInSystemHeader(Decl));
  return true;
}

template <typename Derived>
bool ExtractAPIVisitorBase<Derived>::VisitConceptDecl(const ConceptDecl *Decl) {
  if (!getDerivedExtractAPIVisitor().shouldDeclBeIncluded(Decl))
    return true;

  StringRef Name = Decl->getName();
  SmallString<128> USR;
  index::generateUSRForDecl(Decl, USR);
  PresumedLoc Loc =
      Context.getSourceManager().getPresumedLoc(Decl->getLocation());
  DocComment Comment;
  if (auto *RawComment =
          getDerivedExtractAPIVisitor().fetchRawCommentForDecl(Decl))
    Comment = RawComment->getFormattedLines(Context.getSourceManager(),
                                            Context.getDiagnostics());
  DeclarationFragments Declaration =
      DeclarationFragmentsBuilder::getFragmentsForConcept(Decl);
  DeclarationFragments SubHeading =
      DeclarationFragmentsBuilder::getSubHeading(Decl);
  API.createRecord<ConceptRecord>(
      USR, Name, createHierarchyInformationForDecl(*Decl), Loc,
      AvailabilityInfo::createFromDecl(Decl), Comment, Declaration, SubHeading,
      Template(Decl), isInSystemHeader(Decl));
  return true;
}

template <typename Derived>
bool ExtractAPIVisitorBase<Derived>::VisitClassTemplateSpecializationDecl(
    const ClassTemplateSpecializationDecl *Decl) {
  if (!getDerivedExtractAPIVisitor().shouldDeclBeIncluded(Decl))
    return true;

  StringRef Name = Decl->getName();
  SmallString<128> USR;
  index::generateUSRForDecl(Decl, USR);
  PresumedLoc Loc =
      Context.getSourceManager().getPresumedLoc(Decl->getLocation());
  DocComment Comment;
  if (auto *RawComment =
          getDerivedExtractAPIVisitor().fetchRawCommentForDecl(Decl))
    Comment = RawComment->getFormattedLines(Context.getSourceManager(),
                                            Context.getDiagnostics());
  DeclarationFragments Declaration =
      DeclarationFragmentsBuilder::getFragmentsForClassTemplateSpecialization(
          Decl);
  DeclarationFragments SubHeading =
      DeclarationFragmentsBuilder::getSubHeading(Decl);

  auto *CTSR = API.createRecord<ClassTemplateSpecializationRecord>(
      USR, Name, createHierarchyInformationForDecl(*Decl), Loc,
      AvailabilityInfo::createFromDecl(Decl), Comment, Declaration, SubHeading,
      DeclarationFragmentsBuilder::getAccessControl(Decl),
      isInSystemHeader(Decl));

  CTSR->Bases = getBases(Decl);

  return true;
}

template <typename Derived>
bool ExtractAPIVisitorBase<Derived>::
    VisitClassTemplatePartialSpecializationDecl(
        const ClassTemplatePartialSpecializationDecl *Decl) {
  if (!getDerivedExtractAPIVisitor().shouldDeclBeIncluded(Decl))
    return true;

  StringRef Name = Decl->getName();
  SmallString<128> USR;
  index::generateUSRForDecl(Decl, USR);
  PresumedLoc Loc =
      Context.getSourceManager().getPresumedLoc(Decl->getLocation());
  DocComment Comment;
  if (auto *RawComment =
          getDerivedExtractAPIVisitor().fetchRawCommentForDecl(Decl))
    Comment = RawComment->getFormattedLines(Context.getSourceManager(),
                                            Context.getDiagnostics());
  DeclarationFragments Declaration = DeclarationFragmentsBuilder::
      getFragmentsForClassTemplatePartialSpecialization(Decl);
  DeclarationFragments SubHeading =
      DeclarationFragmentsBuilder::getSubHeading(Decl);
  auto *CTPSR = API.createRecord<ClassTemplatePartialSpecializationRecord>(
      USR, Name, createHierarchyInformationForDecl(*Decl), Loc,
      AvailabilityInfo::createFromDecl(Decl), Comment, Declaration, SubHeading,
      Template(Decl), DeclarationFragmentsBuilder::getAccessControl(Decl),
      isInSystemHeader(Decl));

  CTPSR->KindForDisplay = getKindForDisplay(Decl);
  CTPSR->Bases = getBases(Decl);

  return true;
}

template <typename Derived>
bool ExtractAPIVisitorBase<Derived>::VisitVarTemplateDecl(
    const VarTemplateDecl *Decl) {
  if (!getDerivedExtractAPIVisitor().shouldDeclBeIncluded(Decl))
    return true;

  // Collect symbol information.
  StringRef Name = Decl->getName();
  SmallString<128> USR;
  index::generateUSRForDecl(Decl, USR);
  PresumedLoc Loc =
      Context.getSourceManager().getPresumedLoc(Decl->getLocation());
  LinkageInfo Linkage = Decl->getLinkageAndVisibility();
  DocComment Comment;
  if (auto *RawComment =
          getDerivedExtractAPIVisitor().fetchRawCommentForDecl(Decl))
    Comment = RawComment->getFormattedLines(Context.getSourceManager(),
                                            Context.getDiagnostics());

  // Build declaration fragments and sub-heading for the variable.
  DeclarationFragments Declaration;
  Declaration
      .append(DeclarationFragmentsBuilder::getFragmentsForRedeclarableTemplate(
          Decl))
      .append(DeclarationFragmentsBuilder::getFragmentsForVarTemplate(
          Decl->getTemplatedDecl()));
  // Inject template fragments before var fragments.
  DeclarationFragments SubHeading =
      DeclarationFragmentsBuilder::getSubHeading(Decl);

  if (Decl->getDeclContext()->getDeclKind() == Decl::CXXRecord)
    API.createRecord<CXXFieldTemplateRecord>(
        USR, Name, createHierarchyInformationForDecl(*Decl), Loc,
        AvailabilityInfo::createFromDecl(Decl), Comment, Declaration,
        SubHeading, DeclarationFragmentsBuilder::getAccessControl(Decl),
        Template(Decl), isInSystemHeader(Decl));
  else
    API.createRecord<GlobalVariableTemplateRecord>(
        USR, Name, createHierarchyInformationForDecl(*Decl), Loc,
        AvailabilityInfo::createFromDecl(Decl), Linkage, Comment, Declaration,
        SubHeading, Template(Decl), isInSystemHeader(Decl));
  return true;
}

template <typename Derived>
bool ExtractAPIVisitorBase<Derived>::VisitVarTemplateSpecializationDecl(
    const VarTemplateSpecializationDecl *Decl) {
  if (!getDerivedExtractAPIVisitor().shouldDeclBeIncluded(Decl))
    return true;

  // Collect symbol information.
  StringRef Name = Decl->getName();
  SmallString<128> USR;
  index::generateUSRForDecl(Decl, USR);
  PresumedLoc Loc =
      Context.getSourceManager().getPresumedLoc(Decl->getLocation());
  LinkageInfo Linkage = Decl->getLinkageAndVisibility();
  DocComment Comment;
  if (auto *RawComment =
          getDerivedExtractAPIVisitor().fetchRawCommentForDecl(Decl))
    Comment = RawComment->getFormattedLines(Context.getSourceManager(),
                                            Context.getDiagnostics());

  // Build declaration fragments and sub-heading for the variable.
  DeclarationFragments Declaration =
      DeclarationFragmentsBuilder::getFragmentsForVarTemplateSpecialization(
          Decl);
  DeclarationFragments SubHeading =
      DeclarationFragmentsBuilder::getSubHeading(Decl);
  API.createRecord<GlobalVariableTemplateSpecializationRecord>(
      USR, Name, createHierarchyInformationForDecl(*Decl), Loc,
      AvailabilityInfo::createFromDecl(Decl), Linkage, Comment, Declaration,
      SubHeading, isInSystemHeader(Decl));
  return true;
}

template <typename Derived>
bool ExtractAPIVisitorBase<Derived>::VisitVarTemplatePartialSpecializationDecl(
    const VarTemplatePartialSpecializationDecl *Decl) {
  if (!getDerivedExtractAPIVisitor().shouldDeclBeIncluded(Decl))
    return true;

  // Collect symbol information.
  StringRef Name = Decl->getName();
  SmallString<128> USR;
  index::generateUSRForDecl(Decl, USR);
  PresumedLoc Loc =
      Context.getSourceManager().getPresumedLoc(Decl->getLocation());
  LinkageInfo Linkage = Decl->getLinkageAndVisibility();
  DocComment Comment;
  if (auto *RawComment =
          getDerivedExtractAPIVisitor().fetchRawCommentForDecl(Decl))
    Comment = RawComment->getFormattedLines(Context.getSourceManager(),
                                            Context.getDiagnostics());

  // Build declaration fragments and sub-heading for the variable.
  DeclarationFragments Declaration = DeclarationFragmentsBuilder::
      getFragmentsForVarTemplatePartialSpecialization(Decl);
  DeclarationFragments SubHeading =
      DeclarationFragmentsBuilder::getSubHeading(Decl);
  API.createRecord<GlobalVariableTemplatePartialSpecializationRecord>(
      USR, Name, createHierarchyInformationForDecl(*Decl), Loc,
      AvailabilityInfo::createFromDecl(Decl), Linkage, Comment, Declaration,
      SubHeading, Template(Decl), isInSystemHeader(Decl));
  return true;
}

template <typename Derived>
bool ExtractAPIVisitorBase<Derived>::VisitFunctionTemplateDecl(
    const FunctionTemplateDecl *Decl) {
  if (isa<CXXMethodDecl>(Decl->getTemplatedDecl()))
    return true;
  if (!getDerivedExtractAPIVisitor().shouldDeclBeIncluded(Decl))
    return true;

  // Collect symbol information.
  auto Name = Decl->getNameAsString();
  SmallString<128> USR;
  index::generateUSRForDecl(Decl, USR);
  PresumedLoc Loc =
      Context.getSourceManager().getPresumedLoc(Decl->getLocation());
  LinkageInfo Linkage = Decl->getLinkageAndVisibility();
  DocComment Comment;
  if (auto *RawComment =
          getDerivedExtractAPIVisitor().fetchRawCommentForDecl(Decl))
    Comment = RawComment->getFormattedLines(Context.getSourceManager(),
                                            Context.getDiagnostics());

  DeclarationFragments SubHeading =
      DeclarationFragmentsBuilder::getSubHeading(Decl);
  FunctionSignature Signature =
      DeclarationFragmentsBuilder::getFunctionSignature(
          Decl->getTemplatedDecl());
  API.createRecord<GlobalFunctionTemplateRecord>(
      USR, Name, createHierarchyInformationForDecl(*Decl), Loc,
      AvailabilityInfo::createFromDecl(Decl), Linkage, Comment,
      DeclarationFragmentsBuilder::getFragmentsForFunctionTemplate(Decl),
      SubHeading, Signature, Template(Decl), isInSystemHeader(Decl));

  return true;
}

template <typename Derived>
bool ExtractAPIVisitorBase<Derived>::VisitObjCInterfaceDecl(
    const ObjCInterfaceDecl *Decl) {
  if (!getDerivedExtractAPIVisitor().shouldDeclBeIncluded(Decl))
    return true;

  // Collect symbol information.
  StringRef Name = Decl->getName();
  SmallString<128> USR;
  index::generateUSRForDecl(Decl, USR);
  PresumedLoc Loc =
      Context.getSourceManager().getPresumedLoc(Decl->getLocation());
  LinkageInfo Linkage = Decl->getLinkageAndVisibility();
  DocComment Comment;
  if (auto *RawComment =
          getDerivedExtractAPIVisitor().fetchRawCommentForDecl(Decl))
    Comment = RawComment->getFormattedLines(Context.getSourceManager(),
                                            Context.getDiagnostics());

  // Build declaration fragments and sub-heading for the interface.
  DeclarationFragments Declaration =
      DeclarationFragmentsBuilder::getFragmentsForObjCInterface(Decl);
  DeclarationFragments SubHeading =
      DeclarationFragmentsBuilder::getSubHeading(Decl);

  // Collect super class information.
  SymbolReference SuperClass;
  if (const auto *SuperClassDecl = Decl->getSuperClass())
    SuperClass = createSymbolReferenceForDecl(*SuperClassDecl);

  auto *InterfaceRecord = API.createRecord<ObjCInterfaceRecord>(
      USR, Name, createHierarchyInformationForDecl(*Decl), Loc,
      AvailabilityInfo::createFromDecl(Decl), Linkage, Comment, Declaration,
      SubHeading, SuperClass, isInSystemHeader(Decl));

  // Record all methods (selectors). This doesn't include automatically
  // synthesized property methods.
  getDerivedExtractAPIVisitor().recordObjCMethods(InterfaceRecord,
                                                  Decl->methods());
  getDerivedExtractAPIVisitor().recordObjCProperties(InterfaceRecord,
                                                     Decl->properties());
  getDerivedExtractAPIVisitor().recordObjCInstanceVariables(InterfaceRecord,
                                                            Decl->ivars());
  getDerivedExtractAPIVisitor().recordObjCProtocols(InterfaceRecord,
                                                    Decl->protocols());

  return true;
}

template <typename Derived>
bool ExtractAPIVisitorBase<Derived>::VisitObjCProtocolDecl(
    const ObjCProtocolDecl *Decl) {
  if (!getDerivedExtractAPIVisitor().shouldDeclBeIncluded(Decl))
    return true;

  // Collect symbol information.
  StringRef Name = Decl->getName();
  SmallString<128> USR;
  index::generateUSRForDecl(Decl, USR);
  PresumedLoc Loc =
      Context.getSourceManager().getPresumedLoc(Decl->getLocation());
  DocComment Comment;
  if (auto *RawComment =
          getDerivedExtractAPIVisitor().fetchRawCommentForDecl(Decl))
    Comment = RawComment->getFormattedLines(Context.getSourceManager(),
                                            Context.getDiagnostics());

  // Build declaration fragments and sub-heading for the protocol.
  DeclarationFragments Declaration =
      DeclarationFragmentsBuilder::getFragmentsForObjCProtocol(Decl);
  DeclarationFragments SubHeading =
      DeclarationFragmentsBuilder::getSubHeading(Decl);

  auto *ProtoRecord = API.createRecord<ObjCProtocolRecord>(
      USR, Name, createHierarchyInformationForDecl(*Decl), Loc,
      AvailabilityInfo::createFromDecl(Decl), Comment, Declaration, SubHeading,
      isInSystemHeader(Decl));

  getDerivedExtractAPIVisitor().recordObjCMethods(ProtoRecord, Decl->methods());
  getDerivedExtractAPIVisitor().recordObjCProperties(ProtoRecord,
                                                     Decl->properties());
  getDerivedExtractAPIVisitor().recordObjCProtocols(ProtoRecord,
                                                    Decl->protocols());

  return true;
}

template <typename Derived>
bool ExtractAPIVisitorBase<Derived>::VisitTypedefNameDecl(
    const TypedefNameDecl *Decl) {
  // Skip ObjC Type Parameter for now.
  if (isa<ObjCTypeParamDecl>(Decl))
    return true;

  if (!Decl->isDefinedOutsideFunctionOrMethod())
    return true;

  if (!getDerivedExtractAPIVisitor().shouldDeclBeIncluded(Decl))
    return true;

  StringRef Name = Decl->getName();

  // If the underlying type was defined as part of the typedef modify it's
  // fragments directly and pretend the typedef doesn't exist.
  if (auto *TagDecl = Decl->getUnderlyingType()->getAsTagDecl()) {
    if (TagDecl->isEmbeddedInDeclarator() && TagDecl->isCompleteDefinition() &&
        Decl->getName() == TagDecl->getName()) {
      SmallString<128> TagUSR;
      index::generateUSRForDecl(TagDecl, TagUSR);
      if (auto *Record = API.findRecordForUSR(TagUSR)) {
        DeclarationFragments LeadingFragments;
        LeadingFragments.append("typedef",
                                DeclarationFragments::FragmentKind::Keyword);
        LeadingFragments.appendSpace();
        Record->Declaration.removeTrailingSemicolon()
            .prepend(std::move(LeadingFragments))
            .append(" { ... } ", DeclarationFragments::FragmentKind::Text)
            .append(Name, DeclarationFragments::FragmentKind::Identifier)
            .appendSemicolon();

        return true;
      }
    }
  }

  PresumedLoc Loc =
      Context.getSourceManager().getPresumedLoc(Decl->getLocation());
  SmallString<128> USR;
  index::generateUSRForDecl(Decl, USR);
  DocComment Comment;
  if (auto *RawComment =
          getDerivedExtractAPIVisitor().fetchRawCommentForDecl(Decl))
    Comment = RawComment->getFormattedLines(Context.getSourceManager(),
                                            Context.getDiagnostics());

  QualType Type = Decl->getUnderlyingType();
  SymbolReference SymRef =
      TypedefUnderlyingTypeResolver(Context).getSymbolReferenceForType(Type,
                                                                       API);

  API.createRecord<TypedefRecord>(
      USR, Name, createHierarchyInformationForDecl(*Decl), Loc,
      AvailabilityInfo::createFromDecl(Decl), Comment,
      DeclarationFragmentsBuilder::getFragmentsForTypedef(Decl),
      DeclarationFragmentsBuilder::getSubHeading(Decl), SymRef,
      isInSystemHeader(Decl));

  return true;
}

template <typename Derived>
bool ExtractAPIVisitorBase<Derived>::VisitObjCCategoryDecl(
    const ObjCCategoryDecl *Decl) {
  if (!getDerivedExtractAPIVisitor().shouldDeclBeIncluded(Decl))
    return true;

  StringRef Name = Decl->getName();
  SmallString<128> USR;
  index::generateUSRForDecl(Decl, USR);
  PresumedLoc Loc =
      Context.getSourceManager().getPresumedLoc(Decl->getLocation());
  DocComment Comment;
  if (auto *RawComment =
          getDerivedExtractAPIVisitor().fetchRawCommentForDecl(Decl))
    Comment = RawComment->getFormattedLines(Context.getSourceManager(),
                                            Context.getDiagnostics());
  // Build declaration fragments and sub-heading for the category.
  DeclarationFragments Declaration =
      DeclarationFragmentsBuilder::getFragmentsForObjCCategory(Decl);
  DeclarationFragments SubHeading =
      DeclarationFragmentsBuilder::getSubHeading(Decl);

  const ObjCInterfaceDecl *InterfaceDecl = Decl->getClassInterface();
  SymbolReference Interface = createSymbolReferenceForDecl(*InterfaceDecl);

  auto *CategoryRecord = API.createRecord<ObjCCategoryRecord>(
      USR, Name, createHierarchyInformationForDecl(*Decl), Loc,
      AvailabilityInfo::createFromDecl(Decl), Comment, Declaration, SubHeading,
      Interface, isInSystemHeader(Decl));

  getDerivedExtractAPIVisitor().recordObjCMethods(CategoryRecord,
                                                  Decl->methods());
  getDerivedExtractAPIVisitor().recordObjCProperties(CategoryRecord,
                                                     Decl->properties());
  getDerivedExtractAPIVisitor().recordObjCInstanceVariables(CategoryRecord,
                                                            Decl->ivars());
  getDerivedExtractAPIVisitor().recordObjCProtocols(CategoryRecord,
                                                    Decl->protocols());

  return true;
}

/// Collect API information for the enum constants and associate with the
/// parent enum.
template <typename Derived>
void ExtractAPIVisitorBase<Derived>::recordEnumConstants(
    SymbolReference Container, const EnumDecl::enumerator_range Constants) {
  for (const auto *Constant : Constants) {
    // Collect symbol information.
    StringRef Name = Constant->getName();
    SmallString<128> USR;
    index::generateUSRForDecl(Constant, USR);
    PresumedLoc Loc =
        Context.getSourceManager().getPresumedLoc(Constant->getLocation());
    DocComment Comment;
    if (auto *RawComment =
            getDerivedExtractAPIVisitor().fetchRawCommentForDecl(Constant))
      Comment = RawComment->getFormattedLines(Context.getSourceManager(),
                                              Context.getDiagnostics());

    // Build declaration fragments and sub-heading for the enum constant.
    DeclarationFragments Declaration =
        DeclarationFragmentsBuilder::getFragmentsForEnumConstant(Constant);
    DeclarationFragments SubHeading =
        DeclarationFragmentsBuilder::getSubHeading(Constant);

    API.createRecord<EnumConstantRecord>(
        USR, Name, Container, Loc, AvailabilityInfo::createFromDecl(Constant),
        Comment, Declaration, SubHeading, isInSystemHeader(Constant));
  }
}

template <typename Derived>
bool ExtractAPIVisitorBase<Derived>::VisitFieldDecl(const FieldDecl *Decl) {
  // ObjCIvars are handled separately
  if (isa<ObjCIvarDecl>(Decl) || isa<ObjCAtDefsFieldDecl>(Decl))
    return true;

  if (!getDerivedExtractAPIVisitor().shouldDeclBeIncluded(Decl))
    return true;

  // Collect symbol information.
  StringRef Name = Decl->getName();
  SmallString<128> USR;
  index::generateUSRForDecl(Decl, USR);
  PresumedLoc Loc =
      Context.getSourceManager().getPresumedLoc(Decl->getLocation());
  DocComment Comment;
  if (auto *RawComment =
          getDerivedExtractAPIVisitor().fetchRawCommentForDecl(Decl))
    Comment = RawComment->getFormattedLines(Context.getSourceManager(),
                                            Context.getDiagnostics());

  // Build declaration fragments and sub-heading for the struct field.
  DeclarationFragments Declaration =
      DeclarationFragmentsBuilder::getFragmentsForField(Decl);
  DeclarationFragments SubHeading =
      DeclarationFragmentsBuilder::getSubHeading(Decl);

  RecordContext *NewRecord = nullptr;
  if (isa<CXXRecordDecl>(Decl->getDeclContext())) {
    AccessControl Access = DeclarationFragmentsBuilder::getAccessControl(Decl);

    NewRecord = API.createRecord<CXXFieldRecord>(
        USR, Name, createHierarchyInformationForDecl(*Decl), Loc,
        AvailabilityInfo::createFromDecl(Decl), Comment, Declaration,
        SubHeading, Access, isInSystemHeader(Decl));
  } else if (auto *RD = dyn_cast<RecordDecl>(Decl->getDeclContext())) {
    if (RD->isUnion())
      NewRecord = API.createRecord<UnionFieldRecord>(
          USR, Name, createHierarchyInformationForDecl(*Decl), Loc,
          AvailabilityInfo::createFromDecl(Decl), Comment, Declaration,
          SubHeading, isInSystemHeader(Decl));
    else
      NewRecord = API.createRecord<StructFieldRecord>(
          USR, Name, createHierarchyInformationForDecl(*Decl), Loc,
          AvailabilityInfo::createFromDecl(Decl), Comment, Declaration,
          SubHeading, isInSystemHeader(Decl));
  }

  // If this field has a non typedef'd anonymous tag type let's pretend the
  // type's child records are under us in the hierarchy.
  maybeMergeWithAnonymousTag(*Decl, NewRecord);

  return true;
}

template <typename Derived>
bool ExtractAPIVisitorBase<Derived>::VisitCXXConversionDecl(
    const CXXConversionDecl *Decl) {
  if (!getDerivedExtractAPIVisitor().shouldDeclBeIncluded(Decl) ||
      Decl->isImplicit())
    return true;

  auto Name = Decl->getNameAsString();
  SmallString<128> USR;
  index::generateUSRForDecl(Decl, USR);
  PresumedLoc Loc =
      Context.getSourceManager().getPresumedLoc(Decl->getLocation());
  DocComment Comment;
  if (auto *RawComment =
          getDerivedExtractAPIVisitor().fetchRawCommentForDecl(Decl))
    Comment = RawComment->getFormattedLines(Context.getSourceManager(),
                                            Context.getDiagnostics());

  // Build declaration fragments, sub-heading, and signature for the method.
  DeclarationFragments Declaration =
      DeclarationFragmentsBuilder::getFragmentsForConversionFunction(Decl);
  DeclarationFragments SubHeading =
      DeclarationFragmentsBuilder::getSubHeading(Decl);
  FunctionSignature Signature =
      DeclarationFragmentsBuilder::getFunctionSignature(Decl);
  AccessControl Access = DeclarationFragmentsBuilder::getAccessControl(Decl);

  if (Decl->isStatic())
    API.createRecord<CXXStaticMethodRecord>(
        USR, Name, createHierarchyInformationForDecl(*Decl), Loc,
        AvailabilityInfo::createFromDecl(Decl), Comment, Declaration,
        SubHeading, Signature, Access, isInSystemHeader(Decl));
  else
    API.createRecord<CXXInstanceMethodRecord>(
        USR, Name, createHierarchyInformationForDecl(*Decl), Loc,
        AvailabilityInfo::createFromDecl(Decl), Comment, Declaration,
        SubHeading, Signature, Access, isInSystemHeader(Decl));

  return true;
}

/// Collect API information for the Objective-C methods and associate with the
/// parent container.
template <typename Derived>
void ExtractAPIVisitorBase<Derived>::recordObjCMethods(
    ObjCContainerRecord *Container,
    const ObjCContainerDecl::method_range Methods) {
  for (const auto *Method : Methods) {
    // Don't record selectors for properties.
    if (Method->isPropertyAccessor())
      continue;

    auto Name = Method->getSelector().getAsString();
    SmallString<128> USR;
    index::generateUSRForDecl(Method, USR);
    PresumedLoc Loc =
        Context.getSourceManager().getPresumedLoc(Method->getLocation());
    DocComment Comment;
    if (auto *RawComment =
            getDerivedExtractAPIVisitor().fetchRawCommentForDecl(Method))
      Comment = RawComment->getFormattedLines(Context.getSourceManager(),
                                              Context.getDiagnostics());

    // Build declaration fragments, sub-heading, and signature for the method.
    DeclarationFragments Declaration =
        DeclarationFragmentsBuilder::getFragmentsForObjCMethod(Method);
    DeclarationFragments SubHeading =
        DeclarationFragmentsBuilder::getSubHeading(Method);
    FunctionSignature Signature =
        DeclarationFragmentsBuilder::getFunctionSignature(Method);

    if (Method->isInstanceMethod())
      API.createRecord<ObjCInstanceMethodRecord>(
          USR, Name, createHierarchyInformationForDecl(*Method), Loc,
          AvailabilityInfo::createFromDecl(Method), Comment, Declaration,
          SubHeading, Signature, isInSystemHeader(Method));
    else
      API.createRecord<ObjCClassMethodRecord>(
          USR, Name, createHierarchyInformationForDecl(*Method), Loc,
          AvailabilityInfo::createFromDecl(Method), Comment, Declaration,
          SubHeading, Signature, isInSystemHeader(Method));
  }
}

template <typename Derived>
void ExtractAPIVisitorBase<Derived>::recordObjCProperties(
    ObjCContainerRecord *Container,
    const ObjCContainerDecl::prop_range Properties) {
  for (const auto *Property : Properties) {
    StringRef Name = Property->getName();
    SmallString<128> USR;
    index::generateUSRForDecl(Property, USR);
    PresumedLoc Loc =
        Context.getSourceManager().getPresumedLoc(Property->getLocation());
    DocComment Comment;
    if (auto *RawComment =
            getDerivedExtractAPIVisitor().fetchRawCommentForDecl(Property))
      Comment = RawComment->getFormattedLines(Context.getSourceManager(),
                                              Context.getDiagnostics());

    // Build declaration fragments and sub-heading for the property.
    DeclarationFragments Declaration =
        DeclarationFragmentsBuilder::getFragmentsForObjCProperty(Property);
    DeclarationFragments SubHeading =
        DeclarationFragmentsBuilder::getSubHeading(Property);

    auto GetterName = Property->getGetterName().getAsString();
    auto SetterName = Property->getSetterName().getAsString();

    // Get the attributes for property.
    unsigned Attributes = ObjCPropertyRecord::NoAttr;
    if (Property->getPropertyAttributes() &
        ObjCPropertyAttribute::kind_readonly)
      Attributes |= ObjCPropertyRecord::ReadOnly;

    if (Property->getPropertyAttributes() & ObjCPropertyAttribute::kind_class)
      API.createRecord<ObjCClassPropertyRecord>(
          USR, Name, createHierarchyInformationForDecl(*Property), Loc,
          AvailabilityInfo::createFromDecl(Property), Comment, Declaration,
          SubHeading,
          static_cast<ObjCPropertyRecord::AttributeKind>(Attributes),
          GetterName, SetterName, Property->isOptional(),
          isInSystemHeader(Property));
    else
      API.createRecord<ObjCInstancePropertyRecord>(
          USR, Name, createHierarchyInformationForDecl(*Property), Loc,
          AvailabilityInfo::createFromDecl(Property), Comment, Declaration,
          SubHeading,
          static_cast<ObjCPropertyRecord::AttributeKind>(Attributes),
          GetterName, SetterName, Property->isOptional(),
          isInSystemHeader(Property));
  }
}

template <typename Derived>
void ExtractAPIVisitorBase<Derived>::recordObjCInstanceVariables(
    ObjCContainerRecord *Container,
    const llvm::iterator_range<
        DeclContext::specific_decl_iterator<ObjCIvarDecl>>
        Ivars) {
  for (const auto *Ivar : Ivars) {
    StringRef Name = Ivar->getName();
    SmallString<128> USR;
    index::generateUSRForDecl(Ivar, USR);

    PresumedLoc Loc =
        Context.getSourceManager().getPresumedLoc(Ivar->getLocation());
    DocComment Comment;
    if (auto *RawComment =
            getDerivedExtractAPIVisitor().fetchRawCommentForDecl(Ivar))
      Comment = RawComment->getFormattedLines(Context.getSourceManager(),
                                              Context.getDiagnostics());

    // Build declaration fragments and sub-heading for the instance variable.
    DeclarationFragments Declaration =
        DeclarationFragmentsBuilder::getFragmentsForField(Ivar);
    DeclarationFragments SubHeading =
        DeclarationFragmentsBuilder::getSubHeading(Ivar);

    API.createRecord<ObjCInstanceVariableRecord>(
        USR, Name, createHierarchyInformationForDecl(*Ivar), Loc,
        AvailabilityInfo::createFromDecl(Ivar), Comment, Declaration,
        SubHeading, isInSystemHeader(Ivar));
  }
}

template <typename Derived>
void ExtractAPIVisitorBase<Derived>::recordObjCProtocols(
    ObjCContainerRecord *Container,
    ObjCInterfaceDecl::protocol_range Protocols) {
  for (const auto *Protocol : Protocols)
    Container->Protocols.emplace_back(createSymbolReferenceForDecl(*Protocol));
}

} // namespace impl

/// The RecursiveASTVisitor to traverse symbol declarations and collect API
/// information.
template <typename Derived = void>
class ExtractAPIVisitor
    : public impl::ExtractAPIVisitorBase<std::conditional_t<
          std::is_same_v<Derived, void>, ExtractAPIVisitor<>, Derived>> {
  using Base = impl::ExtractAPIVisitorBase<std::conditional_t<
      std::is_same_v<Derived, void>, ExtractAPIVisitor<>, Derived>>;

public:
  ExtractAPIVisitor(ASTContext &Context, APISet &API) : Base(Context, API) {}

  bool shouldDeclBeIncluded(const Decl *D) const { return true; }
  const RawComment *fetchRawCommentForDecl(const Decl *D) const {
    if (const auto *Comment = this->Context.getRawCommentForDeclNoCache(D))
      return Comment;

    if (const auto *Declarator = dyn_cast<DeclaratorDecl>(D)) {
      const auto *TagTypeDecl = Declarator->getType()->getAsTagDecl();
      if (TagTypeDecl && TagTypeDecl->isEmbeddedInDeclarator() &&
          TagTypeDecl->isCompleteDefinition())
        return this->Context.getRawCommentForDeclNoCache(TagTypeDecl);
    }

    return nullptr;
  }
};

} // namespace extractapi
} // namespace clang

#endif // LLVM_CLANG_EXTRACTAPI_EXTRACT_API_VISITOR_H
