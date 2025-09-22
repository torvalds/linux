//===- DeclTemplate.cpp - Template Declaration AST Node Implementation ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the C++ related Decl classes for templates.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/DeclTemplate.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTMutationListener.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclarationName.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExternalASTSource.h"
#include "clang/AST/TemplateBase.h"
#include "clang/AST/TemplateName.h"
#include "clang/AST/Type.h"
#include "clang/AST/TypeLoc.h"
#include "clang/Basic/Builtins.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/SourceLocation.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

using namespace clang;

//===----------------------------------------------------------------------===//
// TemplateParameterList Implementation
//===----------------------------------------------------------------------===//


TemplateParameterList::TemplateParameterList(const ASTContext& C,
                                             SourceLocation TemplateLoc,
                                             SourceLocation LAngleLoc,
                                             ArrayRef<NamedDecl *> Params,
                                             SourceLocation RAngleLoc,
                                             Expr *RequiresClause)
    : TemplateLoc(TemplateLoc), LAngleLoc(LAngleLoc), RAngleLoc(RAngleLoc),
      NumParams(Params.size()), ContainsUnexpandedParameterPack(false),
      HasRequiresClause(RequiresClause != nullptr),
      HasConstrainedParameters(false) {
  for (unsigned Idx = 0; Idx < NumParams; ++Idx) {
    NamedDecl *P = Params[Idx];
    begin()[Idx] = P;

    bool IsPack = P->isTemplateParameterPack();
    if (const auto *NTTP = dyn_cast<NonTypeTemplateParmDecl>(P)) {
      if (!IsPack && NTTP->getType()->containsUnexpandedParameterPack())
        ContainsUnexpandedParameterPack = true;
      if (NTTP->hasPlaceholderTypeConstraint())
        HasConstrainedParameters = true;
    } else if (const auto *TTP = dyn_cast<TemplateTemplateParmDecl>(P)) {
      if (!IsPack &&
          TTP->getTemplateParameters()->containsUnexpandedParameterPack())
        ContainsUnexpandedParameterPack = true;
    } else if (const auto *TTP = dyn_cast<TemplateTypeParmDecl>(P)) {
      if (const TypeConstraint *TC = TTP->getTypeConstraint()) {
        if (TC->getImmediatelyDeclaredConstraint()
            ->containsUnexpandedParameterPack())
          ContainsUnexpandedParameterPack = true;
      }
      if (TTP->hasTypeConstraint())
        HasConstrainedParameters = true;
    } else {
      llvm_unreachable("unexpected template parameter type");
    }
    // FIXME: If a default argument contains an unexpanded parameter pack, the
    // template parameter list does too.
  }

  if (HasRequiresClause) {
    if (RequiresClause->containsUnexpandedParameterPack())
      ContainsUnexpandedParameterPack = true;
    *getTrailingObjects<Expr *>() = RequiresClause;
  }
}

bool TemplateParameterList::containsUnexpandedParameterPack() const {
  if (ContainsUnexpandedParameterPack)
    return true;
  if (!HasConstrainedParameters)
    return false;

  // An implicit constrained parameter might have had a use of an unexpanded
  // pack added to it after the template parameter list was created. All
  // implicit parameters are at the end of the parameter list.
  for (const NamedDecl *Param : llvm::reverse(asArray())) {
    if (!Param->isImplicit())
      break;

    if (const auto *TTP = dyn_cast<TemplateTypeParmDecl>(Param)) {
      const auto *TC = TTP->getTypeConstraint();
      if (TC && TC->getImmediatelyDeclaredConstraint()
                    ->containsUnexpandedParameterPack())
        return true;
    }
  }

  return false;
}

TemplateParameterList *
TemplateParameterList::Create(const ASTContext &C, SourceLocation TemplateLoc,
                              SourceLocation LAngleLoc,
                              ArrayRef<NamedDecl *> Params,
                              SourceLocation RAngleLoc, Expr *RequiresClause) {
  void *Mem = C.Allocate(totalSizeToAlloc<NamedDecl *, Expr *>(
                             Params.size(), RequiresClause ? 1u : 0u),
                         alignof(TemplateParameterList));
  return new (Mem) TemplateParameterList(C, TemplateLoc, LAngleLoc, Params,
                                         RAngleLoc, RequiresClause);
}

void TemplateParameterList::Profile(llvm::FoldingSetNodeID &ID,
                                    const ASTContext &C) const {
  const Expr *RC = getRequiresClause();
  ID.AddBoolean(RC != nullptr);
  if (RC)
    RC->Profile(ID, C, /*Canonical=*/true);
  ID.AddInteger(size());
  for (NamedDecl *D : *this) {
    if (const auto *NTTP = dyn_cast<NonTypeTemplateParmDecl>(D)) {
      ID.AddInteger(0);
      ID.AddBoolean(NTTP->isParameterPack());
      NTTP->getType().getCanonicalType().Profile(ID);
      ID.AddBoolean(NTTP->hasPlaceholderTypeConstraint());
      if (const Expr *E = NTTP->getPlaceholderTypeConstraint())
        E->Profile(ID, C, /*Canonical=*/true);
      continue;
    }
    if (const auto *TTP = dyn_cast<TemplateTypeParmDecl>(D)) {
      ID.AddInteger(1);
      ID.AddBoolean(TTP->isParameterPack());
      ID.AddBoolean(TTP->hasTypeConstraint());
      if (const TypeConstraint *TC = TTP->getTypeConstraint())
        TC->getImmediatelyDeclaredConstraint()->Profile(ID, C,
                                                        /*Canonical=*/true);
      continue;
    }
    const auto *TTP = cast<TemplateTemplateParmDecl>(D);
    ID.AddInteger(2);
    ID.AddBoolean(TTP->isParameterPack());
    TTP->getTemplateParameters()->Profile(ID, C);
  }
}

unsigned TemplateParameterList::getMinRequiredArguments() const {
  unsigned NumRequiredArgs = 0;
  for (const NamedDecl *P : asArray()) {
    if (P->isTemplateParameterPack()) {
      if (std::optional<unsigned> Expansions = getExpandedPackSize(P)) {
        NumRequiredArgs += *Expansions;
        continue;
      }
      break;
    }

    if (const auto *TTP = dyn_cast<TemplateTypeParmDecl>(P)) {
      if (TTP->hasDefaultArgument())
        break;
    } else if (const auto *NTTP = dyn_cast<NonTypeTemplateParmDecl>(P)) {
      if (NTTP->hasDefaultArgument())
        break;
    } else if (cast<TemplateTemplateParmDecl>(P)->hasDefaultArgument())
      break;

    ++NumRequiredArgs;
  }

  return NumRequiredArgs;
}

unsigned TemplateParameterList::getDepth() const {
  if (size() == 0)
    return 0;

  const NamedDecl *FirstParm = getParam(0);
  if (const auto *TTP = dyn_cast<TemplateTypeParmDecl>(FirstParm))
    return TTP->getDepth();
  else if (const auto *NTTP = dyn_cast<NonTypeTemplateParmDecl>(FirstParm))
    return NTTP->getDepth();
  else
    return cast<TemplateTemplateParmDecl>(FirstParm)->getDepth();
}

static bool AdoptTemplateParameterList(TemplateParameterList *Params,
                                       DeclContext *Owner) {
  bool Invalid = false;
  for (NamedDecl *P : *Params) {
    P->setDeclContext(Owner);

    if (const auto *TTP = dyn_cast<TemplateTemplateParmDecl>(P))
      if (AdoptTemplateParameterList(TTP->getTemplateParameters(), Owner))
        Invalid = true;

    if (P->isInvalidDecl())
      Invalid = true;
  }
  return Invalid;
}

void TemplateParameterList::
getAssociatedConstraints(llvm::SmallVectorImpl<const Expr *> &AC) const {
  if (HasConstrainedParameters)
    for (const NamedDecl *Param : *this) {
      if (const auto *TTP = dyn_cast<TemplateTypeParmDecl>(Param)) {
        if (const auto *TC = TTP->getTypeConstraint())
          AC.push_back(TC->getImmediatelyDeclaredConstraint());
      } else if (const auto *NTTP = dyn_cast<NonTypeTemplateParmDecl>(Param)) {
        if (const Expr *E = NTTP->getPlaceholderTypeConstraint())
          AC.push_back(E);
      }
    }
  if (HasRequiresClause)
    AC.push_back(getRequiresClause());
}

bool TemplateParameterList::hasAssociatedConstraints() const {
  return HasRequiresClause || HasConstrainedParameters;
}

bool TemplateParameterList::shouldIncludeTypeForArgument(
    const PrintingPolicy &Policy, const TemplateParameterList *TPL,
    unsigned Idx) {
  if (!TPL || Idx >= TPL->size() || Policy.AlwaysIncludeTypeForTemplateArgument)
    return true;
  const NamedDecl *TemplParam = TPL->getParam(Idx);
  if (const auto *ParamValueDecl =
          dyn_cast<NonTypeTemplateParmDecl>(TemplParam))
    if (ParamValueDecl->getType()->getContainedDeducedType())
      return true;
  return false;
}

namespace clang {

void *allocateDefaultArgStorageChain(const ASTContext &C) {
  return new (C) char[sizeof(void*) * 2];
}

} // namespace clang

//===----------------------------------------------------------------------===//
// TemplateDecl Implementation
//===----------------------------------------------------------------------===//

TemplateDecl::TemplateDecl(Kind DK, DeclContext *DC, SourceLocation L,
                           DeclarationName Name, TemplateParameterList *Params,
                           NamedDecl *Decl)
    : NamedDecl(DK, DC, L, Name), TemplatedDecl(Decl), TemplateParams(Params) {}

void TemplateDecl::anchor() {}

void TemplateDecl::
getAssociatedConstraints(llvm::SmallVectorImpl<const Expr *> &AC) const {
  TemplateParams->getAssociatedConstraints(AC);
  if (auto *FD = dyn_cast_or_null<FunctionDecl>(getTemplatedDecl()))
    if (const Expr *TRC = FD->getTrailingRequiresClause())
      AC.push_back(TRC);
}

bool TemplateDecl::hasAssociatedConstraints() const {
  if (TemplateParams->hasAssociatedConstraints())
    return true;
  if (auto *FD = dyn_cast_or_null<FunctionDecl>(getTemplatedDecl()))
    return FD->getTrailingRequiresClause();
  return false;
}

bool TemplateDecl::isTypeAlias() const {
  switch (getKind()) {
  case TemplateDecl::TypeAliasTemplate:
  case TemplateDecl::BuiltinTemplate:
    return true;
  default:
    return false;
  };
}

//===----------------------------------------------------------------------===//
// RedeclarableTemplateDecl Implementation
//===----------------------------------------------------------------------===//

void RedeclarableTemplateDecl::anchor() {}

RedeclarableTemplateDecl::CommonBase *RedeclarableTemplateDecl::getCommonPtr() const {
  if (Common)
    return Common;

  // Walk the previous-declaration chain until we either find a declaration
  // with a common pointer or we run out of previous declarations.
  SmallVector<const RedeclarableTemplateDecl *, 2> PrevDecls;
  for (const RedeclarableTemplateDecl *Prev = getPreviousDecl(); Prev;
       Prev = Prev->getPreviousDecl()) {
    if (Prev->Common) {
      Common = Prev->Common;
      break;
    }

    PrevDecls.push_back(Prev);
  }

  // If we never found a common pointer, allocate one now.
  if (!Common) {
    // FIXME: If any of the declarations is from an AST file, we probably
    // need an update record to add the common data.

    Common = newCommon(getASTContext());
  }

  // Update any previous declarations we saw with the common pointer.
  for (const RedeclarableTemplateDecl *Prev : PrevDecls)
    Prev->Common = Common;

  return Common;
}

void RedeclarableTemplateDecl::loadLazySpecializationsImpl() const {
  // Grab the most recent declaration to ensure we've loaded any lazy
  // redeclarations of this template.
  CommonBase *CommonBasePtr = getMostRecentDecl()->getCommonPtr();
  if (CommonBasePtr->LazySpecializations) {
    ASTContext &Context = getASTContext();
    GlobalDeclID *Specs = CommonBasePtr->LazySpecializations;
    CommonBasePtr->LazySpecializations = nullptr;
    unsigned SpecSize = (*Specs++).getRawValue();
    for (unsigned I = 0; I != SpecSize; ++I)
      (void)Context.getExternalSource()->GetExternalDecl(Specs[I]);
  }
}

template<class EntryType, typename... ProfileArguments>
typename RedeclarableTemplateDecl::SpecEntryTraits<EntryType>::DeclType *
RedeclarableTemplateDecl::findSpecializationImpl(
    llvm::FoldingSetVector<EntryType> &Specs, void *&InsertPos,
    ProfileArguments&&... ProfileArgs) {
  using SETraits = SpecEntryTraits<EntryType>;

  llvm::FoldingSetNodeID ID;
  EntryType::Profile(ID, std::forward<ProfileArguments>(ProfileArgs)...,
                     getASTContext());
  EntryType *Entry = Specs.FindNodeOrInsertPos(ID, InsertPos);
  return Entry ? SETraits::getDecl(Entry)->getMostRecentDecl() : nullptr;
}

template<class Derived, class EntryType>
void RedeclarableTemplateDecl::addSpecializationImpl(
    llvm::FoldingSetVector<EntryType> &Specializations, EntryType *Entry,
    void *InsertPos) {
  using SETraits = SpecEntryTraits<EntryType>;

  if (InsertPos) {
#ifndef NDEBUG
    void *CorrectInsertPos;
    assert(!findSpecializationImpl(Specializations,
                                   CorrectInsertPos,
                                   SETraits::getTemplateArgs(Entry)) &&
           InsertPos == CorrectInsertPos &&
           "given incorrect InsertPos for specialization");
#endif
    Specializations.InsertNode(Entry, InsertPos);
  } else {
    EntryType *Existing = Specializations.GetOrInsertNode(Entry);
    (void)Existing;
    assert(SETraits::getDecl(Existing)->isCanonicalDecl() &&
           "non-canonical specialization?");
  }

  if (ASTMutationListener *L = getASTMutationListener())
    L->AddedCXXTemplateSpecialization(cast<Derived>(this),
                                      SETraits::getDecl(Entry));
}

ArrayRef<TemplateArgument> RedeclarableTemplateDecl::getInjectedTemplateArgs() {
  TemplateParameterList *Params = getTemplateParameters();
  auto *CommonPtr = getCommonPtr();
  if (!CommonPtr->InjectedArgs) {
    auto &Context = getASTContext();
    SmallVector<TemplateArgument, 16> TemplateArgs;
    Context.getInjectedTemplateArgs(Params, TemplateArgs);
    CommonPtr->InjectedArgs =
        new (Context) TemplateArgument[TemplateArgs.size()];
    std::copy(TemplateArgs.begin(), TemplateArgs.end(),
              CommonPtr->InjectedArgs);
  }

  return llvm::ArrayRef(CommonPtr->InjectedArgs, Params->size());
}

//===----------------------------------------------------------------------===//
// FunctionTemplateDecl Implementation
//===----------------------------------------------------------------------===//

FunctionTemplateDecl *
FunctionTemplateDecl::Create(ASTContext &C, DeclContext *DC, SourceLocation L,
                             DeclarationName Name,
                             TemplateParameterList *Params, NamedDecl *Decl) {
  bool Invalid = AdoptTemplateParameterList(Params, cast<DeclContext>(Decl));
  auto *TD = new (C, DC) FunctionTemplateDecl(C, DC, L, Name, Params, Decl);
  if (Invalid)
    TD->setInvalidDecl();
  return TD;
}

FunctionTemplateDecl *
FunctionTemplateDecl::CreateDeserialized(ASTContext &C, GlobalDeclID ID) {
  return new (C, ID) FunctionTemplateDecl(C, nullptr, SourceLocation(),
                                          DeclarationName(), nullptr, nullptr);
}

RedeclarableTemplateDecl::CommonBase *
FunctionTemplateDecl::newCommon(ASTContext &C) const {
  auto *CommonPtr = new (C) Common;
  C.addDestruction(CommonPtr);
  return CommonPtr;
}

void FunctionTemplateDecl::LoadLazySpecializations() const {
  loadLazySpecializationsImpl();
}

llvm::FoldingSetVector<FunctionTemplateSpecializationInfo> &
FunctionTemplateDecl::getSpecializations() const {
  LoadLazySpecializations();
  return getCommonPtr()->Specializations;
}

FunctionDecl *
FunctionTemplateDecl::findSpecialization(ArrayRef<TemplateArgument> Args,
                                         void *&InsertPos) {
  return findSpecializationImpl(getSpecializations(), InsertPos, Args);
}

void FunctionTemplateDecl::addSpecialization(
      FunctionTemplateSpecializationInfo *Info, void *InsertPos) {
  addSpecializationImpl<FunctionTemplateDecl>(getSpecializations(), Info,
                                              InsertPos);
}

void FunctionTemplateDecl::mergePrevDecl(FunctionTemplateDecl *Prev) {
  using Base = RedeclarableTemplateDecl;

  // If we haven't created a common pointer yet, then it can just be created
  // with the usual method.
  if (!Base::Common)
    return;

  Common *ThisCommon = static_cast<Common *>(Base::Common);
  Common *PrevCommon = nullptr;
  SmallVector<FunctionTemplateDecl *, 8> PreviousDecls;
  for (; Prev; Prev = Prev->getPreviousDecl()) {
    if (Prev->Base::Common) {
      PrevCommon = static_cast<Common *>(Prev->Base::Common);
      break;
    }
    PreviousDecls.push_back(Prev);
  }

  // If the previous redecl chain hasn't created a common pointer yet, then just
  // use this common pointer.
  if (!PrevCommon) {
    for (auto *D : PreviousDecls)
      D->Base::Common = ThisCommon;
    return;
  }

  // Ensure we don't leak any important state.
  assert(ThisCommon->Specializations.size() == 0 &&
         "Can't merge incompatible declarations!");

  Base::Common = PrevCommon;
}

//===----------------------------------------------------------------------===//
// ClassTemplateDecl Implementation
//===----------------------------------------------------------------------===//

ClassTemplateDecl *ClassTemplateDecl::Create(ASTContext &C, DeclContext *DC,
                                             SourceLocation L,
                                             DeclarationName Name,
                                             TemplateParameterList *Params,
                                             NamedDecl *Decl) {
  bool Invalid = AdoptTemplateParameterList(Params, cast<DeclContext>(Decl));
  auto *TD = new (C, DC) ClassTemplateDecl(C, DC, L, Name, Params, Decl);
  if (Invalid)
    TD->setInvalidDecl();
  return TD;
}

ClassTemplateDecl *ClassTemplateDecl::CreateDeserialized(ASTContext &C,
                                                         GlobalDeclID ID) {
  return new (C, ID) ClassTemplateDecl(C, nullptr, SourceLocation(),
                                       DeclarationName(), nullptr, nullptr);
}

void ClassTemplateDecl::LoadLazySpecializations() const {
  loadLazySpecializationsImpl();
}

llvm::FoldingSetVector<ClassTemplateSpecializationDecl> &
ClassTemplateDecl::getSpecializations() const {
  LoadLazySpecializations();
  return getCommonPtr()->Specializations;
}

llvm::FoldingSetVector<ClassTemplatePartialSpecializationDecl> &
ClassTemplateDecl::getPartialSpecializations() const {
  LoadLazySpecializations();
  return getCommonPtr()->PartialSpecializations;
}

RedeclarableTemplateDecl::CommonBase *
ClassTemplateDecl::newCommon(ASTContext &C) const {
  auto *CommonPtr = new (C) Common;
  C.addDestruction(CommonPtr);
  return CommonPtr;
}

ClassTemplateSpecializationDecl *
ClassTemplateDecl::findSpecialization(ArrayRef<TemplateArgument> Args,
                                      void *&InsertPos) {
  return findSpecializationImpl(getSpecializations(), InsertPos, Args);
}

void ClassTemplateDecl::AddSpecialization(ClassTemplateSpecializationDecl *D,
                                          void *InsertPos) {
  addSpecializationImpl<ClassTemplateDecl>(getSpecializations(), D, InsertPos);
}

ClassTemplatePartialSpecializationDecl *
ClassTemplateDecl::findPartialSpecialization(
    ArrayRef<TemplateArgument> Args,
    TemplateParameterList *TPL, void *&InsertPos) {
  return findSpecializationImpl(getPartialSpecializations(), InsertPos, Args,
                                TPL);
}

void ClassTemplatePartialSpecializationDecl::Profile(
    llvm::FoldingSetNodeID &ID, ArrayRef<TemplateArgument> TemplateArgs,
    TemplateParameterList *TPL, const ASTContext &Context) {
  ID.AddInteger(TemplateArgs.size());
  for (const TemplateArgument &TemplateArg : TemplateArgs)
    TemplateArg.Profile(ID, Context);
  TPL->Profile(ID, Context);
}

void ClassTemplateDecl::AddPartialSpecialization(
                                      ClassTemplatePartialSpecializationDecl *D,
                                      void *InsertPos) {
  if (InsertPos)
    getPartialSpecializations().InsertNode(D, InsertPos);
  else {
    ClassTemplatePartialSpecializationDecl *Existing
      = getPartialSpecializations().GetOrInsertNode(D);
    (void)Existing;
    assert(Existing->isCanonicalDecl() && "Non-canonical specialization?");
  }

  if (ASTMutationListener *L = getASTMutationListener())
    L->AddedCXXTemplateSpecialization(this, D);
}

void ClassTemplateDecl::getPartialSpecializations(
    SmallVectorImpl<ClassTemplatePartialSpecializationDecl *> &PS) const {
  llvm::FoldingSetVector<ClassTemplatePartialSpecializationDecl> &PartialSpecs
    = getPartialSpecializations();
  PS.clear();
  PS.reserve(PartialSpecs.size());
  for (ClassTemplatePartialSpecializationDecl &P : PartialSpecs)
    PS.push_back(P.getMostRecentDecl());
}

ClassTemplatePartialSpecializationDecl *
ClassTemplateDecl::findPartialSpecialization(QualType T) {
  ASTContext &Context = getASTContext();
  for (ClassTemplatePartialSpecializationDecl &P :
       getPartialSpecializations()) {
    if (Context.hasSameType(P.getInjectedSpecializationType(), T))
      return P.getMostRecentDecl();
  }

  return nullptr;
}

ClassTemplatePartialSpecializationDecl *
ClassTemplateDecl::findPartialSpecInstantiatedFromMember(
                                    ClassTemplatePartialSpecializationDecl *D) {
  Decl *DCanon = D->getCanonicalDecl();
  for (ClassTemplatePartialSpecializationDecl &P : getPartialSpecializations()) {
    if (P.getInstantiatedFromMember()->getCanonicalDecl() == DCanon)
      return P.getMostRecentDecl();
  }

  return nullptr;
}

QualType
ClassTemplateDecl::getInjectedClassNameSpecialization() {
  Common *CommonPtr = getCommonPtr();
  if (!CommonPtr->InjectedClassNameType.isNull())
    return CommonPtr->InjectedClassNameType;

  // C++0x [temp.dep.type]p2:
  //  The template argument list of a primary template is a template argument
  //  list in which the nth template argument has the value of the nth template
  //  parameter of the class template. If the nth template parameter is a
  //  template parameter pack (14.5.3), the nth template argument is a pack
  //  expansion (14.5.3) whose pattern is the name of the template parameter
  //  pack.
  ASTContext &Context = getASTContext();
  TemplateParameterList *Params = getTemplateParameters();
  SmallVector<TemplateArgument, 16> TemplateArgs;
  Context.getInjectedTemplateArgs(Params, TemplateArgs);
  TemplateName Name = Context.getQualifiedTemplateName(
      /*NNS=*/nullptr, /*TemplateKeyword=*/false, TemplateName(this));
  CommonPtr->InjectedClassNameType =
      Context.getTemplateSpecializationType(Name, TemplateArgs);
  return CommonPtr->InjectedClassNameType;
}

//===----------------------------------------------------------------------===//
// TemplateTypeParm Allocation/Deallocation Method Implementations
//===----------------------------------------------------------------------===//

TemplateTypeParmDecl *TemplateTypeParmDecl::Create(
    const ASTContext &C, DeclContext *DC, SourceLocation KeyLoc,
    SourceLocation NameLoc, unsigned D, unsigned P, IdentifierInfo *Id,
    bool Typename, bool ParameterPack, bool HasTypeConstraint,
    std::optional<unsigned> NumExpanded) {
  auto *TTPDecl =
      new (C, DC,
           additionalSizeToAlloc<TypeConstraint>(HasTypeConstraint ? 1 : 0))
      TemplateTypeParmDecl(DC, KeyLoc, NameLoc, Id, Typename,
                           HasTypeConstraint, NumExpanded);
  QualType TTPType = C.getTemplateTypeParmType(D, P, ParameterPack, TTPDecl);
  TTPDecl->setTypeForDecl(TTPType.getTypePtr());
  return TTPDecl;
}

TemplateTypeParmDecl *
TemplateTypeParmDecl::CreateDeserialized(const ASTContext &C, GlobalDeclID ID) {
  return new (C, ID)
      TemplateTypeParmDecl(nullptr, SourceLocation(), SourceLocation(), nullptr,
                           false, false, std::nullopt);
}

TemplateTypeParmDecl *
TemplateTypeParmDecl::CreateDeserialized(const ASTContext &C, GlobalDeclID ID,
                                         bool HasTypeConstraint) {
  return new (C, ID,
              additionalSizeToAlloc<TypeConstraint>(HasTypeConstraint ? 1 : 0))
      TemplateTypeParmDecl(nullptr, SourceLocation(), SourceLocation(), nullptr,
                           false, HasTypeConstraint, std::nullopt);
}

SourceLocation TemplateTypeParmDecl::getDefaultArgumentLoc() const {
  return hasDefaultArgument() ? getDefaultArgument().getLocation()
                              : SourceLocation();
}

SourceRange TemplateTypeParmDecl::getSourceRange() const {
  if (hasDefaultArgument() && !defaultArgumentWasInherited())
    return SourceRange(getBeginLoc(),
                       getDefaultArgument().getSourceRange().getEnd());
  // TypeDecl::getSourceRange returns a range containing name location, which is
  // wrong for unnamed template parameters. e.g:
  // it will return <[[typename>]] instead of <[[typename]]>
  if (getDeclName().isEmpty())
    return SourceRange(getBeginLoc());
  return TypeDecl::getSourceRange();
}

void TemplateTypeParmDecl::setDefaultArgument(
    const ASTContext &C, const TemplateArgumentLoc &DefArg) {
  if (DefArg.getArgument().isNull())
    DefaultArgument.set(nullptr);
  else
    DefaultArgument.set(new (C) TemplateArgumentLoc(DefArg));
}

unsigned TemplateTypeParmDecl::getDepth() const {
  return getTypeForDecl()->castAs<TemplateTypeParmType>()->getDepth();
}

unsigned TemplateTypeParmDecl::getIndex() const {
  return getTypeForDecl()->castAs<TemplateTypeParmType>()->getIndex();
}

bool TemplateTypeParmDecl::isParameterPack() const {
  return getTypeForDecl()->castAs<TemplateTypeParmType>()->isParameterPack();
}

void TemplateTypeParmDecl::setTypeConstraint(
    ConceptReference *Loc, Expr *ImmediatelyDeclaredConstraint) {
  assert(HasTypeConstraint &&
         "HasTypeConstraint=true must be passed at construction in order to "
         "call setTypeConstraint");
  assert(!TypeConstraintInitialized &&
         "TypeConstraint was already initialized!");
  new (getTrailingObjects<TypeConstraint>())
      TypeConstraint(Loc, ImmediatelyDeclaredConstraint);
  TypeConstraintInitialized = true;
}

//===----------------------------------------------------------------------===//
// NonTypeTemplateParmDecl Method Implementations
//===----------------------------------------------------------------------===//

NonTypeTemplateParmDecl::NonTypeTemplateParmDecl(
    DeclContext *DC, SourceLocation StartLoc, SourceLocation IdLoc, unsigned D,
    unsigned P, const IdentifierInfo *Id, QualType T, TypeSourceInfo *TInfo,
    ArrayRef<QualType> ExpandedTypes, ArrayRef<TypeSourceInfo *> ExpandedTInfos)
    : DeclaratorDecl(NonTypeTemplateParm, DC, IdLoc, Id, T, TInfo, StartLoc),
      TemplateParmPosition(D, P), ParameterPack(true),
      ExpandedParameterPack(true), NumExpandedTypes(ExpandedTypes.size()) {
  if (!ExpandedTypes.empty() && !ExpandedTInfos.empty()) {
    auto TypesAndInfos =
        getTrailingObjects<std::pair<QualType, TypeSourceInfo *>>();
    for (unsigned I = 0; I != NumExpandedTypes; ++I) {
      new (&TypesAndInfos[I].first) QualType(ExpandedTypes[I]);
      TypesAndInfos[I].second = ExpandedTInfos[I];
    }
  }
}

NonTypeTemplateParmDecl *NonTypeTemplateParmDecl::Create(
    const ASTContext &C, DeclContext *DC, SourceLocation StartLoc,
    SourceLocation IdLoc, unsigned D, unsigned P, const IdentifierInfo *Id,
    QualType T, bool ParameterPack, TypeSourceInfo *TInfo) {
  AutoType *AT =
      C.getLangOpts().CPlusPlus20 ? T->getContainedAutoType() : nullptr;
  return new (C, DC,
              additionalSizeToAlloc<std::pair<QualType, TypeSourceInfo *>,
                                    Expr *>(0,
                                            AT && AT->isConstrained() ? 1 : 0))
      NonTypeTemplateParmDecl(DC, StartLoc, IdLoc, D, P, Id, T, ParameterPack,
                              TInfo);
}

NonTypeTemplateParmDecl *NonTypeTemplateParmDecl::Create(
    const ASTContext &C, DeclContext *DC, SourceLocation StartLoc,
    SourceLocation IdLoc, unsigned D, unsigned P, const IdentifierInfo *Id,
    QualType T, TypeSourceInfo *TInfo, ArrayRef<QualType> ExpandedTypes,
    ArrayRef<TypeSourceInfo *> ExpandedTInfos) {
  AutoType *AT = TInfo->getType()->getContainedAutoType();
  return new (C, DC,
              additionalSizeToAlloc<std::pair<QualType, TypeSourceInfo *>,
                                    Expr *>(
                  ExpandedTypes.size(), AT && AT->isConstrained() ? 1 : 0))
      NonTypeTemplateParmDecl(DC, StartLoc, IdLoc, D, P, Id, T, TInfo,
                              ExpandedTypes, ExpandedTInfos);
}

NonTypeTemplateParmDecl *
NonTypeTemplateParmDecl::CreateDeserialized(ASTContext &C, GlobalDeclID ID,
                                            bool HasTypeConstraint) {
  return new (C, ID, additionalSizeToAlloc<std::pair<QualType,
                                                     TypeSourceInfo *>,
                                           Expr *>(0,
                                                   HasTypeConstraint ? 1 : 0))
          NonTypeTemplateParmDecl(nullptr, SourceLocation(), SourceLocation(),
                                  0, 0, nullptr, QualType(), false, nullptr);
}

NonTypeTemplateParmDecl *
NonTypeTemplateParmDecl::CreateDeserialized(ASTContext &C, GlobalDeclID ID,
                                            unsigned NumExpandedTypes,
                                            bool HasTypeConstraint) {
  auto *NTTP =
      new (C, ID,
           additionalSizeToAlloc<std::pair<QualType, TypeSourceInfo *>, Expr *>(
               NumExpandedTypes, HasTypeConstraint ? 1 : 0))
          NonTypeTemplateParmDecl(nullptr, SourceLocation(), SourceLocation(),
                                  0, 0, nullptr, QualType(), nullptr,
                                  std::nullopt, std::nullopt);
  NTTP->NumExpandedTypes = NumExpandedTypes;
  return NTTP;
}

SourceRange NonTypeTemplateParmDecl::getSourceRange() const {
  if (hasDefaultArgument() && !defaultArgumentWasInherited())
    return SourceRange(getOuterLocStart(),
                       getDefaultArgument().getSourceRange().getEnd());
  return DeclaratorDecl::getSourceRange();
}

SourceLocation NonTypeTemplateParmDecl::getDefaultArgumentLoc() const {
  return hasDefaultArgument() ? getDefaultArgument().getSourceRange().getBegin()
                              : SourceLocation();
}

void NonTypeTemplateParmDecl::setDefaultArgument(
    const ASTContext &C, const TemplateArgumentLoc &DefArg) {
  if (DefArg.getArgument().isNull())
    DefaultArgument.set(nullptr);
  else
    DefaultArgument.set(new (C) TemplateArgumentLoc(DefArg));
}

//===----------------------------------------------------------------------===//
// TemplateTemplateParmDecl Method Implementations
//===----------------------------------------------------------------------===//

void TemplateTemplateParmDecl::anchor() {}

TemplateTemplateParmDecl::TemplateTemplateParmDecl(
    DeclContext *DC, SourceLocation L, unsigned D, unsigned P,
    IdentifierInfo *Id, bool Typename, TemplateParameterList *Params,
    ArrayRef<TemplateParameterList *> Expansions)
    : TemplateDecl(TemplateTemplateParm, DC, L, Id, Params),
      TemplateParmPosition(D, P), Typename(Typename), ParameterPack(true),
      ExpandedParameterPack(true), NumExpandedParams(Expansions.size()) {
  if (!Expansions.empty())
    std::uninitialized_copy(Expansions.begin(), Expansions.end(),
                            getTrailingObjects<TemplateParameterList *>());
}

TemplateTemplateParmDecl *
TemplateTemplateParmDecl::Create(const ASTContext &C, DeclContext *DC,
                                 SourceLocation L, unsigned D, unsigned P,
                                 bool ParameterPack, IdentifierInfo *Id,
                                 bool Typename, TemplateParameterList *Params) {
  return new (C, DC) TemplateTemplateParmDecl(DC, L, D, P, ParameterPack, Id,
                                              Typename, Params);
}

TemplateTemplateParmDecl *
TemplateTemplateParmDecl::Create(const ASTContext &C, DeclContext *DC,
                                 SourceLocation L, unsigned D, unsigned P,
                                 IdentifierInfo *Id, bool Typename,
                                 TemplateParameterList *Params,
                                 ArrayRef<TemplateParameterList *> Expansions) {
  return new (C, DC,
              additionalSizeToAlloc<TemplateParameterList *>(Expansions.size()))
      TemplateTemplateParmDecl(DC, L, D, P, Id, Typename, Params, Expansions);
}

TemplateTemplateParmDecl *
TemplateTemplateParmDecl::CreateDeserialized(ASTContext &C, GlobalDeclID ID) {
  return new (C, ID) TemplateTemplateParmDecl(nullptr, SourceLocation(), 0, 0,
                                              false, nullptr, false, nullptr);
}

TemplateTemplateParmDecl *
TemplateTemplateParmDecl::CreateDeserialized(ASTContext &C, GlobalDeclID ID,
                                             unsigned NumExpansions) {
  auto *TTP =
      new (C, ID, additionalSizeToAlloc<TemplateParameterList *>(NumExpansions))
          TemplateTemplateParmDecl(nullptr, SourceLocation(), 0, 0, nullptr,
                                   false, nullptr, std::nullopt);
  TTP->NumExpandedParams = NumExpansions;
  return TTP;
}

SourceLocation TemplateTemplateParmDecl::getDefaultArgumentLoc() const {
  return hasDefaultArgument() ? getDefaultArgument().getLocation()
                              : SourceLocation();
}

void TemplateTemplateParmDecl::setDefaultArgument(
    const ASTContext &C, const TemplateArgumentLoc &DefArg) {
  if (DefArg.getArgument().isNull())
    DefaultArgument.set(nullptr);
  else
    DefaultArgument.set(new (C) TemplateArgumentLoc(DefArg));
}

//===----------------------------------------------------------------------===//
// TemplateArgumentList Implementation
//===----------------------------------------------------------------------===//
TemplateArgumentList::TemplateArgumentList(ArrayRef<TemplateArgument> Args)
    : NumArguments(Args.size()) {
  std::uninitialized_copy(Args.begin(), Args.end(),
                          getTrailingObjects<TemplateArgument>());
}

TemplateArgumentList *
TemplateArgumentList::CreateCopy(ASTContext &Context,
                                 ArrayRef<TemplateArgument> Args) {
  void *Mem = Context.Allocate(totalSizeToAlloc<TemplateArgument>(Args.size()));
  return new (Mem) TemplateArgumentList(Args);
}

FunctionTemplateSpecializationInfo *FunctionTemplateSpecializationInfo::Create(
    ASTContext &C, FunctionDecl *FD, FunctionTemplateDecl *Template,
    TemplateSpecializationKind TSK, TemplateArgumentList *TemplateArgs,
    const TemplateArgumentListInfo *TemplateArgsAsWritten, SourceLocation POI,
    MemberSpecializationInfo *MSInfo) {
  const ASTTemplateArgumentListInfo *ArgsAsWritten = nullptr;
  if (TemplateArgsAsWritten)
    ArgsAsWritten = ASTTemplateArgumentListInfo::Create(C,
                                                        *TemplateArgsAsWritten);

  void *Mem =
      C.Allocate(totalSizeToAlloc<MemberSpecializationInfo *>(MSInfo ? 1 : 0));
  return new (Mem) FunctionTemplateSpecializationInfo(
      FD, Template, TSK, TemplateArgs, ArgsAsWritten, POI, MSInfo);
}

//===----------------------------------------------------------------------===//
// ClassTemplateSpecializationDecl Implementation
//===----------------------------------------------------------------------===//

ClassTemplateSpecializationDecl::
ClassTemplateSpecializationDecl(ASTContext &Context, Kind DK, TagKind TK,
                                DeclContext *DC, SourceLocation StartLoc,
                                SourceLocation IdLoc,
                                ClassTemplateDecl *SpecializedTemplate,
                                ArrayRef<TemplateArgument> Args,
                                ClassTemplateSpecializationDecl *PrevDecl)
    : CXXRecordDecl(DK, TK, Context, DC, StartLoc, IdLoc,
                    SpecializedTemplate->getIdentifier(), PrevDecl),
    SpecializedTemplate(SpecializedTemplate),
    TemplateArgs(TemplateArgumentList::CreateCopy(Context, Args)),
    SpecializationKind(TSK_Undeclared) {
}

ClassTemplateSpecializationDecl::ClassTemplateSpecializationDecl(ASTContext &C,
                                                                 Kind DK)
    : CXXRecordDecl(DK, TagTypeKind::Struct, C, nullptr, SourceLocation(),
                    SourceLocation(), nullptr, nullptr),
      SpecializationKind(TSK_Undeclared) {}

ClassTemplateSpecializationDecl *
ClassTemplateSpecializationDecl::Create(ASTContext &Context, TagKind TK,
                                        DeclContext *DC,
                                        SourceLocation StartLoc,
                                        SourceLocation IdLoc,
                                        ClassTemplateDecl *SpecializedTemplate,
                                        ArrayRef<TemplateArgument> Args,
                                   ClassTemplateSpecializationDecl *PrevDecl) {
  auto *Result =
      new (Context, DC) ClassTemplateSpecializationDecl(
          Context, ClassTemplateSpecialization, TK, DC, StartLoc, IdLoc,
          SpecializedTemplate, Args, PrevDecl);
  Result->setMayHaveOutOfDateDef(false);

  // If the template decl is incomplete, copy the external lexical storage from
  // the base template. This allows instantiations of incomplete types to
  // complete using the external AST if the template's declaration came from an
  // external AST.
  if (!SpecializedTemplate->getTemplatedDecl()->isCompleteDefinition())
    Result->setHasExternalLexicalStorage(
      SpecializedTemplate->getTemplatedDecl()->hasExternalLexicalStorage());

  Context.getTypeDeclType(Result, PrevDecl);
  return Result;
}

ClassTemplateSpecializationDecl *
ClassTemplateSpecializationDecl::CreateDeserialized(ASTContext &C,
                                                    GlobalDeclID ID) {
  auto *Result =
    new (C, ID) ClassTemplateSpecializationDecl(C, ClassTemplateSpecialization);
  Result->setMayHaveOutOfDateDef(false);
  return Result;
}

void ClassTemplateSpecializationDecl::getNameForDiagnostic(
    raw_ostream &OS, const PrintingPolicy &Policy, bool Qualified) const {
  NamedDecl::getNameForDiagnostic(OS, Policy, Qualified);

  const auto *PS = dyn_cast<ClassTemplatePartialSpecializationDecl>(this);
  if (const ASTTemplateArgumentListInfo *ArgsAsWritten =
          PS ? PS->getTemplateArgsAsWritten() : nullptr) {
    printTemplateArgumentList(
        OS, ArgsAsWritten->arguments(), Policy,
        getSpecializedTemplate()->getTemplateParameters());
  } else {
    const TemplateArgumentList &TemplateArgs = getTemplateArgs();
    printTemplateArgumentList(
        OS, TemplateArgs.asArray(), Policy,
        getSpecializedTemplate()->getTemplateParameters());
  }
}

ClassTemplateDecl *
ClassTemplateSpecializationDecl::getSpecializedTemplate() const {
  if (const auto *PartialSpec =
          SpecializedTemplate.dyn_cast<SpecializedPartialSpecialization*>())
    return PartialSpec->PartialSpecialization->getSpecializedTemplate();
  return SpecializedTemplate.get<ClassTemplateDecl*>();
}

SourceRange
ClassTemplateSpecializationDecl::getSourceRange() const {
  switch (getSpecializationKind()) {
  case TSK_Undeclared:
  case TSK_ImplicitInstantiation: {
    llvm::PointerUnion<ClassTemplateDecl *,
                       ClassTemplatePartialSpecializationDecl *>
        Pattern = getSpecializedTemplateOrPartial();
    assert(!Pattern.isNull() &&
           "Class template specialization without pattern?");
    if (const auto *CTPSD =
            Pattern.dyn_cast<ClassTemplatePartialSpecializationDecl *>())
      return CTPSD->getSourceRange();
    return Pattern.get<ClassTemplateDecl *>()->getSourceRange();
  }
  case TSK_ExplicitSpecialization: {
    SourceRange Range = CXXRecordDecl::getSourceRange();
    if (const ASTTemplateArgumentListInfo *Args = getTemplateArgsAsWritten();
        !isThisDeclarationADefinition() && Args)
      Range.setEnd(Args->getRAngleLoc());
    return Range;
  }
  case TSK_ExplicitInstantiationDeclaration:
  case TSK_ExplicitInstantiationDefinition: {
    SourceRange Range = CXXRecordDecl::getSourceRange();
    if (SourceLocation ExternKW = getExternKeywordLoc(); ExternKW.isValid())
      Range.setBegin(ExternKW);
    else if (SourceLocation TemplateKW = getTemplateKeywordLoc();
             TemplateKW.isValid())
      Range.setBegin(TemplateKW);
    if (const ASTTemplateArgumentListInfo *Args = getTemplateArgsAsWritten())
      Range.setEnd(Args->getRAngleLoc());
    return Range;
  }
  }
  llvm_unreachable("unhandled template specialization kind");
}

void ClassTemplateSpecializationDecl::setExternKeywordLoc(SourceLocation Loc) {
  auto *Info = ExplicitInfo.dyn_cast<ExplicitInstantiationInfo *>();
  if (!Info) {
    // Don't allocate if the location is invalid.
    if (Loc.isInvalid())
      return;
    Info = new (getASTContext()) ExplicitInstantiationInfo;
    Info->TemplateArgsAsWritten = getTemplateArgsAsWritten();
    ExplicitInfo = Info;
  }
  Info->ExternKeywordLoc = Loc;
}

void ClassTemplateSpecializationDecl::setTemplateKeywordLoc(
    SourceLocation Loc) {
  auto *Info = ExplicitInfo.dyn_cast<ExplicitInstantiationInfo *>();
  if (!Info) {
    // Don't allocate if the location is invalid.
    if (Loc.isInvalid())
      return;
    Info = new (getASTContext()) ExplicitInstantiationInfo;
    Info->TemplateArgsAsWritten = getTemplateArgsAsWritten();
    ExplicitInfo = Info;
  }
  Info->TemplateKeywordLoc = Loc;
}

//===----------------------------------------------------------------------===//
// ConceptDecl Implementation
//===----------------------------------------------------------------------===//
ConceptDecl *ConceptDecl::Create(ASTContext &C, DeclContext *DC,
                                 SourceLocation L, DeclarationName Name,
                                 TemplateParameterList *Params,
                                 Expr *ConstraintExpr) {
  bool Invalid = AdoptTemplateParameterList(Params, DC);
  auto *TD = new (C, DC) ConceptDecl(DC, L, Name, Params, ConstraintExpr);
  if (Invalid)
    TD->setInvalidDecl();
  return TD;
}

ConceptDecl *ConceptDecl::CreateDeserialized(ASTContext &C, GlobalDeclID ID) {
  ConceptDecl *Result = new (C, ID) ConceptDecl(nullptr, SourceLocation(),
                                                DeclarationName(),
                                                nullptr, nullptr);

  return Result;
}

//===----------------------------------------------------------------------===//
// ImplicitConceptSpecializationDecl Implementation
//===----------------------------------------------------------------------===//
ImplicitConceptSpecializationDecl::ImplicitConceptSpecializationDecl(
    DeclContext *DC, SourceLocation SL,
    ArrayRef<TemplateArgument> ConvertedArgs)
    : Decl(ImplicitConceptSpecialization, DC, SL),
      NumTemplateArgs(ConvertedArgs.size()) {
  setTemplateArguments(ConvertedArgs);
}

ImplicitConceptSpecializationDecl::ImplicitConceptSpecializationDecl(
    EmptyShell Empty, unsigned NumTemplateArgs)
    : Decl(ImplicitConceptSpecialization, Empty),
      NumTemplateArgs(NumTemplateArgs) {}

ImplicitConceptSpecializationDecl *ImplicitConceptSpecializationDecl::Create(
    const ASTContext &C, DeclContext *DC, SourceLocation SL,
    ArrayRef<TemplateArgument> ConvertedArgs) {
  return new (C, DC,
              additionalSizeToAlloc<TemplateArgument>(ConvertedArgs.size()))
      ImplicitConceptSpecializationDecl(DC, SL, ConvertedArgs);
}

ImplicitConceptSpecializationDecl *
ImplicitConceptSpecializationDecl::CreateDeserialized(
    const ASTContext &C, GlobalDeclID ID, unsigned NumTemplateArgs) {
  return new (C, ID, additionalSizeToAlloc<TemplateArgument>(NumTemplateArgs))
      ImplicitConceptSpecializationDecl(EmptyShell{}, NumTemplateArgs);
}

void ImplicitConceptSpecializationDecl::setTemplateArguments(
    ArrayRef<TemplateArgument> Converted) {
  assert(Converted.size() == NumTemplateArgs);
  std::uninitialized_copy(Converted.begin(), Converted.end(),
                          getTrailingObjects<TemplateArgument>());
}

//===----------------------------------------------------------------------===//
// ClassTemplatePartialSpecializationDecl Implementation
//===----------------------------------------------------------------------===//
void ClassTemplatePartialSpecializationDecl::anchor() {}

ClassTemplatePartialSpecializationDecl::ClassTemplatePartialSpecializationDecl(
    ASTContext &Context, TagKind TK, DeclContext *DC, SourceLocation StartLoc,
    SourceLocation IdLoc, TemplateParameterList *Params,
    ClassTemplateDecl *SpecializedTemplate, ArrayRef<TemplateArgument> Args,
    ClassTemplatePartialSpecializationDecl *PrevDecl)
    : ClassTemplateSpecializationDecl(
          Context, ClassTemplatePartialSpecialization, TK, DC, StartLoc, IdLoc,
          SpecializedTemplate, Args, PrevDecl),
      TemplateParams(Params), InstantiatedFromMember(nullptr, false) {
  if (AdoptTemplateParameterList(Params, this))
    setInvalidDecl();
}

ClassTemplatePartialSpecializationDecl *
ClassTemplatePartialSpecializationDecl::Create(
    ASTContext &Context, TagKind TK, DeclContext *DC, SourceLocation StartLoc,
    SourceLocation IdLoc, TemplateParameterList *Params,
    ClassTemplateDecl *SpecializedTemplate, ArrayRef<TemplateArgument> Args,
    QualType CanonInjectedType,
    ClassTemplatePartialSpecializationDecl *PrevDecl) {
  auto *Result = new (Context, DC) ClassTemplatePartialSpecializationDecl(
      Context, TK, DC, StartLoc, IdLoc, Params, SpecializedTemplate, Args,
      PrevDecl);
  Result->setSpecializationKind(TSK_ExplicitSpecialization);
  Result->setMayHaveOutOfDateDef(false);

  Context.getInjectedClassNameType(Result, CanonInjectedType);
  return Result;
}

ClassTemplatePartialSpecializationDecl *
ClassTemplatePartialSpecializationDecl::CreateDeserialized(ASTContext &C,
                                                           GlobalDeclID ID) {
  auto *Result = new (C, ID) ClassTemplatePartialSpecializationDecl(C);
  Result->setMayHaveOutOfDateDef(false);
  return Result;
}

SourceRange ClassTemplatePartialSpecializationDecl::getSourceRange() const {
  if (const ClassTemplatePartialSpecializationDecl *MT =
          getInstantiatedFromMember();
      MT && !isMemberSpecialization())
    return MT->getSourceRange();
  SourceRange Range = ClassTemplateSpecializationDecl::getSourceRange();
  if (const TemplateParameterList *TPL = getTemplateParameters();
      TPL && !getNumTemplateParameterLists())
    Range.setBegin(TPL->getTemplateLoc());
  return Range;
}

//===----------------------------------------------------------------------===//
// FriendTemplateDecl Implementation
//===----------------------------------------------------------------------===//

void FriendTemplateDecl::anchor() {}

FriendTemplateDecl *
FriendTemplateDecl::Create(ASTContext &Context, DeclContext *DC,
                           SourceLocation L,
                           MutableArrayRef<TemplateParameterList *> Params,
                           FriendUnion Friend, SourceLocation FLoc) {
  TemplateParameterList **TPL = nullptr;
  if (!Params.empty()) {
    TPL = new (Context) TemplateParameterList *[Params.size()];
    llvm::copy(Params, TPL);
  }
  return new (Context, DC)
      FriendTemplateDecl(DC, L, TPL, Params.size(), Friend, FLoc);
}

FriendTemplateDecl *FriendTemplateDecl::CreateDeserialized(ASTContext &C,
                                                           GlobalDeclID ID) {
  return new (C, ID) FriendTemplateDecl(EmptyShell());
}

//===----------------------------------------------------------------------===//
// TypeAliasTemplateDecl Implementation
//===----------------------------------------------------------------------===//

TypeAliasTemplateDecl *
TypeAliasTemplateDecl::Create(ASTContext &C, DeclContext *DC, SourceLocation L,
                              DeclarationName Name,
                              TemplateParameterList *Params, NamedDecl *Decl) {
  bool Invalid = AdoptTemplateParameterList(Params, DC);
  auto *TD = new (C, DC) TypeAliasTemplateDecl(C, DC, L, Name, Params, Decl);
  if (Invalid)
    TD->setInvalidDecl();
  return TD;
}

TypeAliasTemplateDecl *
TypeAliasTemplateDecl::CreateDeserialized(ASTContext &C, GlobalDeclID ID) {
  return new (C, ID) TypeAliasTemplateDecl(C, nullptr, SourceLocation(),
                                           DeclarationName(), nullptr, nullptr);
}

RedeclarableTemplateDecl::CommonBase *
TypeAliasTemplateDecl::newCommon(ASTContext &C) const {
  auto *CommonPtr = new (C) Common;
  C.addDestruction(CommonPtr);
  return CommonPtr;
}

//===----------------------------------------------------------------------===//
// VarTemplateDecl Implementation
//===----------------------------------------------------------------------===//

VarTemplateDecl *VarTemplateDecl::getDefinition() {
  VarTemplateDecl *CurD = this;
  while (CurD) {
    if (CurD->isThisDeclarationADefinition())
      return CurD;
    CurD = CurD->getPreviousDecl();
  }
  return nullptr;
}

VarTemplateDecl *VarTemplateDecl::Create(ASTContext &C, DeclContext *DC,
                                         SourceLocation L, DeclarationName Name,
                                         TemplateParameterList *Params,
                                         VarDecl *Decl) {
  bool Invalid = AdoptTemplateParameterList(Params, DC);
  auto *TD = new (C, DC) VarTemplateDecl(C, DC, L, Name, Params, Decl);
  if (Invalid)
    TD->setInvalidDecl();
  return TD;
}

VarTemplateDecl *VarTemplateDecl::CreateDeserialized(ASTContext &C,
                                                     GlobalDeclID ID) {
  return new (C, ID) VarTemplateDecl(C, nullptr, SourceLocation(),
                                     DeclarationName(), nullptr, nullptr);
}

void VarTemplateDecl::LoadLazySpecializations() const {
  loadLazySpecializationsImpl();
}

llvm::FoldingSetVector<VarTemplateSpecializationDecl> &
VarTemplateDecl::getSpecializations() const {
  LoadLazySpecializations();
  return getCommonPtr()->Specializations;
}

llvm::FoldingSetVector<VarTemplatePartialSpecializationDecl> &
VarTemplateDecl::getPartialSpecializations() const {
  LoadLazySpecializations();
  return getCommonPtr()->PartialSpecializations;
}

RedeclarableTemplateDecl::CommonBase *
VarTemplateDecl::newCommon(ASTContext &C) const {
  auto *CommonPtr = new (C) Common;
  C.addDestruction(CommonPtr);
  return CommonPtr;
}

VarTemplateSpecializationDecl *
VarTemplateDecl::findSpecialization(ArrayRef<TemplateArgument> Args,
                                    void *&InsertPos) {
  return findSpecializationImpl(getSpecializations(), InsertPos, Args);
}

void VarTemplateDecl::AddSpecialization(VarTemplateSpecializationDecl *D,
                                        void *InsertPos) {
  addSpecializationImpl<VarTemplateDecl>(getSpecializations(), D, InsertPos);
}

VarTemplatePartialSpecializationDecl *
VarTemplateDecl::findPartialSpecialization(ArrayRef<TemplateArgument> Args,
     TemplateParameterList *TPL, void *&InsertPos) {
  return findSpecializationImpl(getPartialSpecializations(), InsertPos, Args,
                                TPL);
}

void VarTemplatePartialSpecializationDecl::Profile(
    llvm::FoldingSetNodeID &ID, ArrayRef<TemplateArgument> TemplateArgs,
    TemplateParameterList *TPL, const ASTContext &Context) {
  ID.AddInteger(TemplateArgs.size());
  for (const TemplateArgument &TemplateArg : TemplateArgs)
    TemplateArg.Profile(ID, Context);
  TPL->Profile(ID, Context);
}

void VarTemplateDecl::AddPartialSpecialization(
    VarTemplatePartialSpecializationDecl *D, void *InsertPos) {
  if (InsertPos)
    getPartialSpecializations().InsertNode(D, InsertPos);
  else {
    VarTemplatePartialSpecializationDecl *Existing =
        getPartialSpecializations().GetOrInsertNode(D);
    (void)Existing;
    assert(Existing->isCanonicalDecl() && "Non-canonical specialization?");
  }

  if (ASTMutationListener *L = getASTMutationListener())
    L->AddedCXXTemplateSpecialization(this, D);
}

void VarTemplateDecl::getPartialSpecializations(
    SmallVectorImpl<VarTemplatePartialSpecializationDecl *> &PS) const {
  llvm::FoldingSetVector<VarTemplatePartialSpecializationDecl> &PartialSpecs =
      getPartialSpecializations();
  PS.clear();
  PS.reserve(PartialSpecs.size());
  for (VarTemplatePartialSpecializationDecl &P : PartialSpecs)
    PS.push_back(P.getMostRecentDecl());
}

VarTemplatePartialSpecializationDecl *
VarTemplateDecl::findPartialSpecInstantiatedFromMember(
    VarTemplatePartialSpecializationDecl *D) {
  Decl *DCanon = D->getCanonicalDecl();
  for (VarTemplatePartialSpecializationDecl &P : getPartialSpecializations()) {
    if (P.getInstantiatedFromMember()->getCanonicalDecl() == DCanon)
      return P.getMostRecentDecl();
  }

  return nullptr;
}

//===----------------------------------------------------------------------===//
// VarTemplateSpecializationDecl Implementation
//===----------------------------------------------------------------------===//

VarTemplateSpecializationDecl::VarTemplateSpecializationDecl(
    Kind DK, ASTContext &Context, DeclContext *DC, SourceLocation StartLoc,
    SourceLocation IdLoc, VarTemplateDecl *SpecializedTemplate, QualType T,
    TypeSourceInfo *TInfo, StorageClass S, ArrayRef<TemplateArgument> Args)
    : VarDecl(DK, Context, DC, StartLoc, IdLoc,
              SpecializedTemplate->getIdentifier(), T, TInfo, S),
      SpecializedTemplate(SpecializedTemplate),
      TemplateArgs(TemplateArgumentList::CreateCopy(Context, Args)),
      SpecializationKind(TSK_Undeclared), IsCompleteDefinition(false) {}

VarTemplateSpecializationDecl::VarTemplateSpecializationDecl(Kind DK,
                                                             ASTContext &C)
    : VarDecl(DK, C, nullptr, SourceLocation(), SourceLocation(), nullptr,
              QualType(), nullptr, SC_None),
      SpecializationKind(TSK_Undeclared), IsCompleteDefinition(false) {}

VarTemplateSpecializationDecl *VarTemplateSpecializationDecl::Create(
    ASTContext &Context, DeclContext *DC, SourceLocation StartLoc,
    SourceLocation IdLoc, VarTemplateDecl *SpecializedTemplate, QualType T,
    TypeSourceInfo *TInfo, StorageClass S, ArrayRef<TemplateArgument> Args) {
  return new (Context, DC) VarTemplateSpecializationDecl(
      VarTemplateSpecialization, Context, DC, StartLoc, IdLoc,
      SpecializedTemplate, T, TInfo, S, Args);
}

VarTemplateSpecializationDecl *
VarTemplateSpecializationDecl::CreateDeserialized(ASTContext &C,
                                                  GlobalDeclID ID) {
  return new (C, ID)
      VarTemplateSpecializationDecl(VarTemplateSpecialization, C);
}

void VarTemplateSpecializationDecl::getNameForDiagnostic(
    raw_ostream &OS, const PrintingPolicy &Policy, bool Qualified) const {
  NamedDecl::getNameForDiagnostic(OS, Policy, Qualified);

  const auto *PS = dyn_cast<VarTemplatePartialSpecializationDecl>(this);
  if (const ASTTemplateArgumentListInfo *ArgsAsWritten =
          PS ? PS->getTemplateArgsAsWritten() : nullptr) {
    printTemplateArgumentList(
        OS, ArgsAsWritten->arguments(), Policy,
        getSpecializedTemplate()->getTemplateParameters());
  } else {
    const TemplateArgumentList &TemplateArgs = getTemplateArgs();
    printTemplateArgumentList(
        OS, TemplateArgs.asArray(), Policy,
        getSpecializedTemplate()->getTemplateParameters());
  }
}

VarTemplateDecl *VarTemplateSpecializationDecl::getSpecializedTemplate() const {
  if (const auto *PartialSpec =
          SpecializedTemplate.dyn_cast<SpecializedPartialSpecialization *>())
    return PartialSpec->PartialSpecialization->getSpecializedTemplate();
  return SpecializedTemplate.get<VarTemplateDecl *>();
}

SourceRange VarTemplateSpecializationDecl::getSourceRange() const {
  switch (getSpecializationKind()) {
  case TSK_Undeclared:
  case TSK_ImplicitInstantiation: {
    llvm::PointerUnion<VarTemplateDecl *,
                       VarTemplatePartialSpecializationDecl *>
        Pattern = getSpecializedTemplateOrPartial();
    assert(!Pattern.isNull() &&
           "Variable template specialization without pattern?");
    if (const auto *VTPSD =
            Pattern.dyn_cast<VarTemplatePartialSpecializationDecl *>())
      return VTPSD->getSourceRange();
    VarTemplateDecl *VTD = Pattern.get<VarTemplateDecl *>();
    if (hasInit()) {
      if (VarTemplateDecl *Definition = VTD->getDefinition())
        return Definition->getSourceRange();
    }
    return VTD->getCanonicalDecl()->getSourceRange();
  }
  case TSK_ExplicitSpecialization: {
    SourceRange Range = VarDecl::getSourceRange();
    if (const ASTTemplateArgumentListInfo *Args = getTemplateArgsAsWritten();
        !hasInit() && Args)
      Range.setEnd(Args->getRAngleLoc());
    return Range;
  }
  case TSK_ExplicitInstantiationDeclaration:
  case TSK_ExplicitInstantiationDefinition: {
    SourceRange Range = VarDecl::getSourceRange();
    if (SourceLocation ExternKW = getExternKeywordLoc(); ExternKW.isValid())
      Range.setBegin(ExternKW);
    else if (SourceLocation TemplateKW = getTemplateKeywordLoc();
             TemplateKW.isValid())
      Range.setBegin(TemplateKW);
    if (const ASTTemplateArgumentListInfo *Args = getTemplateArgsAsWritten())
      Range.setEnd(Args->getRAngleLoc());
    return Range;
  }
  }
  llvm_unreachable("unhandled template specialization kind");
}

void VarTemplateSpecializationDecl::setExternKeywordLoc(SourceLocation Loc) {
  auto *Info = ExplicitInfo.dyn_cast<ExplicitInstantiationInfo *>();
  if (!Info) {
    // Don't allocate if the location is invalid.
    if (Loc.isInvalid())
      return;
    Info = new (getASTContext()) ExplicitInstantiationInfo;
    Info->TemplateArgsAsWritten = getTemplateArgsAsWritten();
    ExplicitInfo = Info;
  }
  Info->ExternKeywordLoc = Loc;
}

void VarTemplateSpecializationDecl::setTemplateKeywordLoc(SourceLocation Loc) {
  auto *Info = ExplicitInfo.dyn_cast<ExplicitInstantiationInfo *>();
  if (!Info) {
    // Don't allocate if the location is invalid.
    if (Loc.isInvalid())
      return;
    Info = new (getASTContext()) ExplicitInstantiationInfo;
    Info->TemplateArgsAsWritten = getTemplateArgsAsWritten();
    ExplicitInfo = Info;
  }
  Info->TemplateKeywordLoc = Loc;
}

//===----------------------------------------------------------------------===//
// VarTemplatePartialSpecializationDecl Implementation
//===----------------------------------------------------------------------===//

void VarTemplatePartialSpecializationDecl::anchor() {}

VarTemplatePartialSpecializationDecl::VarTemplatePartialSpecializationDecl(
    ASTContext &Context, DeclContext *DC, SourceLocation StartLoc,
    SourceLocation IdLoc, TemplateParameterList *Params,
    VarTemplateDecl *SpecializedTemplate, QualType T, TypeSourceInfo *TInfo,
    StorageClass S, ArrayRef<TemplateArgument> Args)
    : VarTemplateSpecializationDecl(VarTemplatePartialSpecialization, Context,
                                    DC, StartLoc, IdLoc, SpecializedTemplate, T,
                                    TInfo, S, Args),
      TemplateParams(Params), InstantiatedFromMember(nullptr, false) {
  if (AdoptTemplateParameterList(Params, DC))
    setInvalidDecl();
}

VarTemplatePartialSpecializationDecl *
VarTemplatePartialSpecializationDecl::Create(
    ASTContext &Context, DeclContext *DC, SourceLocation StartLoc,
    SourceLocation IdLoc, TemplateParameterList *Params,
    VarTemplateDecl *SpecializedTemplate, QualType T, TypeSourceInfo *TInfo,
    StorageClass S, ArrayRef<TemplateArgument> Args) {
  auto *Result = new (Context, DC) VarTemplatePartialSpecializationDecl(
      Context, DC, StartLoc, IdLoc, Params, SpecializedTemplate, T, TInfo, S,
      Args);
  Result->setSpecializationKind(TSK_ExplicitSpecialization);
  return Result;
}

VarTemplatePartialSpecializationDecl *
VarTemplatePartialSpecializationDecl::CreateDeserialized(ASTContext &C,
                                                         GlobalDeclID ID) {
  return new (C, ID) VarTemplatePartialSpecializationDecl(C);
}

SourceRange VarTemplatePartialSpecializationDecl::getSourceRange() const {
  if (const VarTemplatePartialSpecializationDecl *MT =
          getInstantiatedFromMember();
      MT && !isMemberSpecialization())
    return MT->getSourceRange();
  SourceRange Range = VarTemplateSpecializationDecl::getSourceRange();
  if (const TemplateParameterList *TPL = getTemplateParameters();
      TPL && !getNumTemplateParameterLists())
    Range.setBegin(TPL->getTemplateLoc());
  return Range;
}

static TemplateParameterList *
createMakeIntegerSeqParameterList(const ASTContext &C, DeclContext *DC) {
  // typename T
  auto *T = TemplateTypeParmDecl::Create(
      C, DC, SourceLocation(), SourceLocation(), /*Depth=*/1, /*Position=*/0,
      /*Id=*/nullptr, /*Typename=*/true, /*ParameterPack=*/false,
      /*HasTypeConstraint=*/false);
  T->setImplicit(true);

  // T ...Ints
  TypeSourceInfo *TI =
      C.getTrivialTypeSourceInfo(QualType(T->getTypeForDecl(), 0));
  auto *N = NonTypeTemplateParmDecl::Create(
      C, DC, SourceLocation(), SourceLocation(), /*Depth=*/0, /*Position=*/1,
      /*Id=*/nullptr, TI->getType(), /*ParameterPack=*/true, TI);
  N->setImplicit(true);

  // <typename T, T ...Ints>
  NamedDecl *P[2] = {T, N};
  auto *TPL = TemplateParameterList::Create(
      C, SourceLocation(), SourceLocation(), P, SourceLocation(), nullptr);

  // template <typename T, ...Ints> class IntSeq
  auto *TemplateTemplateParm = TemplateTemplateParmDecl::Create(
      C, DC, SourceLocation(), /*Depth=*/0, /*Position=*/0,
      /*ParameterPack=*/false, /*Id=*/nullptr, /*Typename=*/false, TPL);
  TemplateTemplateParm->setImplicit(true);

  // typename T
  auto *TemplateTypeParm = TemplateTypeParmDecl::Create(
      C, DC, SourceLocation(), SourceLocation(), /*Depth=*/0, /*Position=*/1,
      /*Id=*/nullptr, /*Typename=*/true, /*ParameterPack=*/false,
      /*HasTypeConstraint=*/false);
  TemplateTypeParm->setImplicit(true);

  // T N
  TypeSourceInfo *TInfo = C.getTrivialTypeSourceInfo(
      QualType(TemplateTypeParm->getTypeForDecl(), 0));
  auto *NonTypeTemplateParm = NonTypeTemplateParmDecl::Create(
      C, DC, SourceLocation(), SourceLocation(), /*Depth=*/0, /*Position=*/2,
      /*Id=*/nullptr, TInfo->getType(), /*ParameterPack=*/false, TInfo);
  NamedDecl *Params[] = {TemplateTemplateParm, TemplateTypeParm,
                         NonTypeTemplateParm};

  // template <template <typename T, T ...Ints> class IntSeq, typename T, T N>
  return TemplateParameterList::Create(C, SourceLocation(), SourceLocation(),
                                       Params, SourceLocation(), nullptr);
}

static TemplateParameterList *
createTypePackElementParameterList(const ASTContext &C, DeclContext *DC) {
  // std::size_t Index
  TypeSourceInfo *TInfo = C.getTrivialTypeSourceInfo(C.getSizeType());
  auto *Index = NonTypeTemplateParmDecl::Create(
      C, DC, SourceLocation(), SourceLocation(), /*Depth=*/0, /*Position=*/0,
      /*Id=*/nullptr, TInfo->getType(), /*ParameterPack=*/false, TInfo);

  // typename ...T
  auto *Ts = TemplateTypeParmDecl::Create(
      C, DC, SourceLocation(), SourceLocation(), /*Depth=*/0, /*Position=*/1,
      /*Id=*/nullptr, /*Typename=*/true, /*ParameterPack=*/true,
      /*HasTypeConstraint=*/false);
  Ts->setImplicit(true);

  // template <std::size_t Index, typename ...T>
  NamedDecl *Params[] = {Index, Ts};
  return TemplateParameterList::Create(C, SourceLocation(), SourceLocation(),
                                       llvm::ArrayRef(Params), SourceLocation(),
                                       nullptr);
}

static TemplateParameterList *createBuiltinTemplateParameterList(
    const ASTContext &C, DeclContext *DC, BuiltinTemplateKind BTK) {
  switch (BTK) {
  case BTK__make_integer_seq:
    return createMakeIntegerSeqParameterList(C, DC);
  case BTK__type_pack_element:
    return createTypePackElementParameterList(C, DC);
  }

  llvm_unreachable("unhandled BuiltinTemplateKind!");
}

void BuiltinTemplateDecl::anchor() {}

BuiltinTemplateDecl::BuiltinTemplateDecl(const ASTContext &C, DeclContext *DC,
                                         DeclarationName Name,
                                         BuiltinTemplateKind BTK)
    : TemplateDecl(BuiltinTemplate, DC, SourceLocation(), Name,
                   createBuiltinTemplateParameterList(C, DC, BTK)),
      BTK(BTK) {}

TemplateParamObjectDecl *TemplateParamObjectDecl::Create(const ASTContext &C,
                                                         QualType T,
                                                         const APValue &V) {
  DeclContext *DC = C.getTranslationUnitDecl();
  auto *TPOD = new (C, DC) TemplateParamObjectDecl(DC, T, V);
  C.addDestruction(&TPOD->Value);
  return TPOD;
}

TemplateParamObjectDecl *
TemplateParamObjectDecl::CreateDeserialized(ASTContext &C, GlobalDeclID ID) {
  auto *TPOD = new (C, ID) TemplateParamObjectDecl(nullptr, QualType(), APValue());
  C.addDestruction(&TPOD->Value);
  return TPOD;
}

void TemplateParamObjectDecl::printName(llvm::raw_ostream &OS,
                                        const PrintingPolicy &Policy) const {
  OS << "<template param ";
  printAsExpr(OS, Policy);
  OS << ">";
}

void TemplateParamObjectDecl::printAsExpr(llvm::raw_ostream &OS) const {
  printAsExpr(OS, getASTContext().getPrintingPolicy());
}

void TemplateParamObjectDecl::printAsExpr(llvm::raw_ostream &OS,
                                          const PrintingPolicy &Policy) const {
  getType().getUnqualifiedType().print(OS, Policy);
  printAsInit(OS, Policy);
}

void TemplateParamObjectDecl::printAsInit(llvm::raw_ostream &OS) const {
  printAsInit(OS, getASTContext().getPrintingPolicy());
}

void TemplateParamObjectDecl::printAsInit(llvm::raw_ostream &OS,
                                          const PrintingPolicy &Policy) const {
  getValue().printPretty(OS, Policy, getType(), &getASTContext());
}

TemplateParameterList *clang::getReplacedTemplateParameterList(Decl *D) {
  switch (D->getKind()) {
  case Decl::Kind::CXXRecord:
    return cast<CXXRecordDecl>(D)
        ->getDescribedTemplate()
        ->getTemplateParameters();
  case Decl::Kind::ClassTemplate:
    return cast<ClassTemplateDecl>(D)->getTemplateParameters();
  case Decl::Kind::ClassTemplateSpecialization: {
    const auto *CTSD = cast<ClassTemplateSpecializationDecl>(D);
    auto P = CTSD->getSpecializedTemplateOrPartial();
    if (const auto *CTPSD =
            P.dyn_cast<ClassTemplatePartialSpecializationDecl *>())
      return CTPSD->getTemplateParameters();
    return cast<ClassTemplateDecl *>(P)->getTemplateParameters();
  }
  case Decl::Kind::ClassTemplatePartialSpecialization:
    return cast<ClassTemplatePartialSpecializationDecl>(D)
        ->getTemplateParameters();
  case Decl::Kind::TypeAliasTemplate:
    return cast<TypeAliasTemplateDecl>(D)->getTemplateParameters();
  case Decl::Kind::BuiltinTemplate:
    return cast<BuiltinTemplateDecl>(D)->getTemplateParameters();
  case Decl::Kind::CXXDeductionGuide:
  case Decl::Kind::CXXConversion:
  case Decl::Kind::CXXConstructor:
  case Decl::Kind::CXXDestructor:
  case Decl::Kind::CXXMethod:
  case Decl::Kind::Function:
    return cast<FunctionDecl>(D)
        ->getTemplateSpecializationInfo()
        ->getTemplate()
        ->getTemplateParameters();
  case Decl::Kind::FunctionTemplate:
    return cast<FunctionTemplateDecl>(D)->getTemplateParameters();
  case Decl::Kind::VarTemplate:
    return cast<VarTemplateDecl>(D)->getTemplateParameters();
  case Decl::Kind::VarTemplateSpecialization: {
    const auto *VTSD = cast<VarTemplateSpecializationDecl>(D);
    auto P = VTSD->getSpecializedTemplateOrPartial();
    if (const auto *VTPSD =
            P.dyn_cast<VarTemplatePartialSpecializationDecl *>())
      return VTPSD->getTemplateParameters();
    return cast<VarTemplateDecl *>(P)->getTemplateParameters();
  }
  case Decl::Kind::VarTemplatePartialSpecialization:
    return cast<VarTemplatePartialSpecializationDecl>(D)
        ->getTemplateParameters();
  case Decl::Kind::TemplateTemplateParm:
    return cast<TemplateTemplateParmDecl>(D)->getTemplateParameters();
  case Decl::Kind::Concept:
    return cast<ConceptDecl>(D)->getTemplateParameters();
  default:
    llvm_unreachable("Unhandled templated declaration kind");
  }
}
