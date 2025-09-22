//===- IndexingContext.cpp - Indexing context data ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "IndexingContext.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Index/IndexDataConsumer.h"

using namespace clang;
using namespace index;

static bool isGeneratedDecl(const Decl *D) {
  if (auto *attr = D->getAttr<ExternalSourceSymbolAttr>()) {
    return attr->getGeneratedDeclaration();
  }
  return false;
}

bool IndexingContext::shouldIndex(const Decl *D) {
  return !isGeneratedDecl(D);
}

const LangOptions &IndexingContext::getLangOpts() const {
  return Ctx->getLangOpts();
}

bool IndexingContext::shouldIndexFunctionLocalSymbols() const {
  return IndexOpts.IndexFunctionLocals;
}

bool IndexingContext::shouldIndexImplicitInstantiation() const {
  return IndexOpts.IndexImplicitInstantiation;
}

bool IndexingContext::shouldIndexParametersInDeclarations() const {
  return IndexOpts.IndexParametersInDeclarations;
}

bool IndexingContext::shouldIndexTemplateParameters() const {
  return IndexOpts.IndexTemplateParameters;
}

bool IndexingContext::handleDecl(const Decl *D,
                                 SymbolRoleSet Roles,
                                 ArrayRef<SymbolRelation> Relations) {
  return handleDecl(D, D->getLocation(), Roles, Relations);
}

bool IndexingContext::handleDecl(const Decl *D, SourceLocation Loc,
                                 SymbolRoleSet Roles,
                                 ArrayRef<SymbolRelation> Relations,
                                 const DeclContext *DC) {
  if (!DC)
    DC = D->getDeclContext();

  const Decl *OrigD = D;
  if (isa<ObjCPropertyImplDecl>(D)) {
    D = cast<ObjCPropertyImplDecl>(D)->getPropertyDecl();
  }
  return handleDeclOccurrence(D, Loc, /*IsRef=*/false, cast<Decl>(DC),
                              Roles, Relations,
                              nullptr, OrigD, DC);
}

bool IndexingContext::handleReference(const NamedDecl *D, SourceLocation Loc,
                                      const NamedDecl *Parent,
                                      const DeclContext *DC,
                                      SymbolRoleSet Roles,
                                      ArrayRef<SymbolRelation> Relations,
                                      const Expr *RefE) {
  if (!shouldIndexFunctionLocalSymbols() && isFunctionLocalSymbol(D))
    return true;

  if (!shouldIndexTemplateParameters() &&
      (isa<NonTypeTemplateParmDecl>(D) || isa<TemplateTypeParmDecl>(D) ||
       isa<TemplateTemplateParmDecl>(D))) {
    return true;
  }
  return handleDeclOccurrence(D, Loc, /*IsRef=*/true, Parent, Roles, Relations,
                              RefE, nullptr, DC);
}

static void reportModuleReferences(const Module *Mod,
                                   ArrayRef<SourceLocation> IdLocs,
                                   const ImportDecl *ImportD,
                                   IndexDataConsumer &DataConsumer) {
  if (!Mod)
    return;
  reportModuleReferences(Mod->Parent, IdLocs.drop_back(), ImportD,
                         DataConsumer);
  DataConsumer.handleModuleOccurrence(
      ImportD, Mod, (SymbolRoleSet)SymbolRole::Reference, IdLocs.back());
}

bool IndexingContext::importedModule(const ImportDecl *ImportD) {
  if (ImportD->isInvalidDecl())
    return true;

  SourceLocation Loc;
  auto IdLocs = ImportD->getIdentifierLocs();
  if (!IdLocs.empty())
    Loc = IdLocs.back();
  else
    Loc = ImportD->getLocation();

  SourceManager &SM = Ctx->getSourceManager();
  FileID FID = SM.getFileID(SM.getFileLoc(Loc));
  if (FID.isInvalid())
    return true;

  bool Invalid = false;
  const SrcMgr::SLocEntry &SEntry = SM.getSLocEntry(FID, &Invalid);
  if (Invalid || !SEntry.isFile())
    return true;

  if (SEntry.getFile().getFileCharacteristic() != SrcMgr::C_User) {
    switch (IndexOpts.SystemSymbolFilter) {
    case IndexingOptions::SystemSymbolFilterKind::None:
      return true;
    case IndexingOptions::SystemSymbolFilterKind::DeclarationsOnly:
    case IndexingOptions::SystemSymbolFilterKind::All:
      break;
    }
  }

  const Module *Mod = ImportD->getImportedModule();
  if (!ImportD->isImplicit() && Mod->Parent && !IdLocs.empty()) {
    reportModuleReferences(Mod->Parent, IdLocs.drop_back(), ImportD,
                           DataConsumer);
  }

  SymbolRoleSet Roles = (unsigned)SymbolRole::Declaration;
  if (ImportD->isImplicit())
    Roles |= (unsigned)SymbolRole::Implicit;

  return DataConsumer.handleModuleOccurrence(ImportD, Mod, Roles, Loc);
}

bool IndexingContext::isTemplateImplicitInstantiation(const Decl *D) {
  TemplateSpecializationKind TKind = TSK_Undeclared;
  if (const ClassTemplateSpecializationDecl *
      SD = dyn_cast<ClassTemplateSpecializationDecl>(D)) {
    TKind = SD->getSpecializationKind();
  } else if (const FunctionDecl *FD = dyn_cast<FunctionDecl>(D)) {
    TKind = FD->getTemplateSpecializationKind();
  } else if (auto *VD = dyn_cast<VarDecl>(D)) {
    TKind = VD->getTemplateSpecializationKind();
  } else if (const auto *RD = dyn_cast<CXXRecordDecl>(D)) {
    if (RD->getInstantiatedFromMemberClass())
      TKind = RD->getTemplateSpecializationKind();
  } else if (const auto *ED = dyn_cast<EnumDecl>(D)) {
    if (ED->getInstantiatedFromMemberEnum())
      TKind = ED->getTemplateSpecializationKind();
  } else if (isa<FieldDecl>(D) || isa<TypedefNameDecl>(D) ||
             isa<EnumConstantDecl>(D)) {
    if (const auto *Parent = dyn_cast<Decl>(D->getDeclContext()))
      return isTemplateImplicitInstantiation(Parent);
  }
  switch (TKind) {
    case TSK_Undeclared:
      // Instantiation maybe not happen yet when we see a SpecializationDecl,
      // e.g. when the type doesn't need to be complete, we still treat it as an
      // instantiation as we'd like to keep the canonicalized result consistent.
      return isa<ClassTemplateSpecializationDecl>(D);
    case TSK_ExplicitSpecialization:
      return false;
    case TSK_ImplicitInstantiation:
    case TSK_ExplicitInstantiationDeclaration:
    case TSK_ExplicitInstantiationDefinition:
      return true;
  }
  llvm_unreachable("invalid TemplateSpecializationKind");
}

bool IndexingContext::shouldIgnoreIfImplicit(const Decl *D) {
  if (isa<ObjCInterfaceDecl>(D))
    return false;
  if (isa<ObjCCategoryDecl>(D))
    return false;
  if (isa<ObjCIvarDecl>(D))
    return false;
  if (isa<ObjCMethodDecl>(D))
    return false;
  if (isa<ImportDecl>(D))
    return false;
  return true;
}

static const CXXRecordDecl *
getDeclContextForTemplateInstationPattern(const Decl *D) {
  if (const auto *CTSD =
          dyn_cast<ClassTemplateSpecializationDecl>(D->getDeclContext()))
    return CTSD->getTemplateInstantiationPattern();
  else if (const auto *RD = dyn_cast<CXXRecordDecl>(D->getDeclContext()))
    return RD->getInstantiatedFromMemberClass();
  return nullptr;
}

static const Decl *adjustTemplateImplicitInstantiation(const Decl *D) {
  if (const ClassTemplateSpecializationDecl *
      SD = dyn_cast<ClassTemplateSpecializationDecl>(D)) {
    const auto *Template = SD->getTemplateInstantiationPattern();
    if (Template)
      return Template;
    // Fallback to primary template if no instantiation is available yet (e.g.
    // the type doesn't need to be complete).
    return SD->getSpecializedTemplate()->getTemplatedDecl();
  } else if (const FunctionDecl *FD = dyn_cast<FunctionDecl>(D)) {
    return FD->getTemplateInstantiationPattern();
  } else if (auto *VD = dyn_cast<VarDecl>(D)) {
    return VD->getTemplateInstantiationPattern();
  } else if (const auto *RD = dyn_cast<CXXRecordDecl>(D)) {
    return RD->getInstantiatedFromMemberClass();
  } else if (const auto *ED = dyn_cast<EnumDecl>(D)) {
    return ED->getInstantiatedFromMemberEnum();
  } else if (isa<FieldDecl>(D) || isa<TypedefNameDecl>(D)) {
    const auto *ND = cast<NamedDecl>(D);
    if (const CXXRecordDecl *Pattern =
            getDeclContextForTemplateInstationPattern(ND)) {
      for (const NamedDecl *BaseND : Pattern->lookup(ND->getDeclName())) {
        if (BaseND->isImplicit())
          continue;
        if (BaseND->getKind() == ND->getKind())
          return BaseND;
      }
    }
  } else if (const auto *ECD = dyn_cast<EnumConstantDecl>(D)) {
    if (const auto *ED = dyn_cast<EnumDecl>(ECD->getDeclContext())) {
      if (const EnumDecl *Pattern = ED->getInstantiatedFromMemberEnum()) {
        for (const NamedDecl *BaseECD : Pattern->lookup(ECD->getDeclName()))
          return BaseECD;
      }
    }
  }
  return nullptr;
}

static bool isDeclADefinition(const Decl *D, const DeclContext *ContainerDC, ASTContext &Ctx) {
  if (auto VD = dyn_cast<VarDecl>(D))
    return VD->isThisDeclarationADefinition(Ctx);

  if (auto FD = dyn_cast<FunctionDecl>(D))
    return FD->isThisDeclarationADefinition();

  if (auto TD = dyn_cast<TagDecl>(D))
    return TD->isThisDeclarationADefinition();

  if (auto MD = dyn_cast<ObjCMethodDecl>(D))
    return MD->isThisDeclarationADefinition() || isa<ObjCImplDecl>(ContainerDC);

  if (isa<TypedefNameDecl>(D) || isa<EnumConstantDecl>(D) ||
      isa<FieldDecl>(D) || isa<MSPropertyDecl>(D) || isa<ObjCImplDecl>(D) ||
      isa<ObjCPropertyImplDecl>(D) || isa<ConceptDecl>(D))
    return true;

  return false;
}

/// Whether the given NamedDecl should be skipped because it has no name.
static bool shouldSkipNamelessDecl(const NamedDecl *ND) {
  return (ND->getDeclName().isEmpty() && !isa<TagDecl>(ND) &&
          !isa<ObjCCategoryDecl>(ND)) || isa<CXXDeductionGuideDecl>(ND);
}

static const Decl *adjustParent(const Decl *Parent) {
  if (!Parent)
    return nullptr;
  for (;; Parent = cast<Decl>(Parent->getDeclContext())) {
    if (isa<TranslationUnitDecl>(Parent))
      return nullptr;
    if (isa<LinkageSpecDecl>(Parent) || isa<BlockDecl>(Parent))
      continue;
    if (auto NS = dyn_cast<NamespaceDecl>(Parent)) {
      if (NS->isAnonymousNamespace())
        continue;
    } else if (auto RD = dyn_cast<RecordDecl>(Parent)) {
      if (RD->isAnonymousStructOrUnion())
        continue;
    } else if (auto ND = dyn_cast<NamedDecl>(Parent)) {
      if (shouldSkipNamelessDecl(ND))
        continue;
    }
    return Parent;
  }
}

static const Decl *getCanonicalDecl(const Decl *D) {
  D = D->getCanonicalDecl();
  if (auto TD = dyn_cast<TemplateDecl>(D)) {
    if (auto TTD = TD->getTemplatedDecl()) {
      D = TTD;
      assert(D->isCanonicalDecl());
    }
  }

  return D;
}

static bool shouldReportOccurrenceForSystemDeclOnlyMode(
    bool IsRef, SymbolRoleSet Roles, ArrayRef<SymbolRelation> Relations) {
  if (!IsRef)
    return true;

  auto acceptForRelation = [](SymbolRoleSet roles) -> bool {
    bool accept = false;
    applyForEachSymbolRoleInterruptible(roles, [&accept](SymbolRole r) -> bool {
      switch (r) {
      case SymbolRole::RelationChildOf:
      case SymbolRole::RelationBaseOf:
      case SymbolRole::RelationOverrideOf:
      case SymbolRole::RelationExtendedBy:
      case SymbolRole::RelationAccessorOf:
      case SymbolRole::RelationIBTypeOf:
        accept = true;
        return false;
      case SymbolRole::Declaration:
      case SymbolRole::Definition:
      case SymbolRole::Reference:
      case SymbolRole::Read:
      case SymbolRole::Write:
      case SymbolRole::Call:
      case SymbolRole::Dynamic:
      case SymbolRole::AddressOf:
      case SymbolRole::Implicit:
      case SymbolRole::Undefinition:
      case SymbolRole::RelationReceivedBy:
      case SymbolRole::RelationCalledBy:
      case SymbolRole::RelationContainedBy:
      case SymbolRole::RelationSpecializationOf:
      case SymbolRole::NameReference:
        return true;
      }
      llvm_unreachable("Unsupported SymbolRole value!");
    });
    return accept;
  };

  for (auto &Rel : Relations) {
    if (acceptForRelation(Rel.Roles))
      return true;
  }

  return false;
}

bool IndexingContext::handleDeclOccurrence(const Decl *D, SourceLocation Loc,
                                           bool IsRef, const Decl *Parent,
                                           SymbolRoleSet Roles,
                                           ArrayRef<SymbolRelation> Relations,
                                           const Expr *OrigE,
                                           const Decl *OrigD,
                                           const DeclContext *ContainerDC) {
  if (D->isImplicit() && !isa<ObjCMethodDecl>(D))
    return true;
  if (!isa<NamedDecl>(D) || shouldSkipNamelessDecl(cast<NamedDecl>(D)))
    return true;

  SourceManager &SM = Ctx->getSourceManager();
  FileID FID = SM.getFileID(SM.getFileLoc(Loc));
  if (FID.isInvalid())
    return true;

  bool Invalid = false;
  const SrcMgr::SLocEntry &SEntry = SM.getSLocEntry(FID, &Invalid);
  if (Invalid || !SEntry.isFile())
    return true;

  if (SEntry.getFile().getFileCharacteristic() != SrcMgr::C_User) {
    switch (IndexOpts.SystemSymbolFilter) {
    case IndexingOptions::SystemSymbolFilterKind::None:
      return true;
    case IndexingOptions::SystemSymbolFilterKind::DeclarationsOnly:
      if (!shouldReportOccurrenceForSystemDeclOnlyMode(IsRef, Roles, Relations))
        return true;
      break;
    case IndexingOptions::SystemSymbolFilterKind::All:
      break;
    }
  }

  if (!OrigD)
    OrigD = D;

  if (isTemplateImplicitInstantiation(D)) {
    if (!IsRef)
      return true;
    D = adjustTemplateImplicitInstantiation(D);
    if (!D)
      return true;
    assert(!isTemplateImplicitInstantiation(D));
  }

  if (IsRef)
    Roles |= (unsigned)SymbolRole::Reference;
  else if (isDeclADefinition(OrigD, ContainerDC, *Ctx))
    Roles |= (unsigned)SymbolRole::Definition;
  else
    Roles |= (unsigned)SymbolRole::Declaration;

  D = getCanonicalDecl(D);
  Parent = adjustParent(Parent);
  if (Parent)
    Parent = getCanonicalDecl(Parent);

  SmallVector<SymbolRelation, 6> FinalRelations;
  FinalRelations.reserve(Relations.size()+1);

  auto addRelation = [&](SymbolRelation Rel) {
    auto It = llvm::find_if(FinalRelations, [&](SymbolRelation Elem) -> bool {
      return Elem.RelatedSymbol == Rel.RelatedSymbol;
    });
    if (It != FinalRelations.end()) {
      It->Roles |= Rel.Roles;
    } else {
      FinalRelations.push_back(Rel);
    }
    Roles |= Rel.Roles;
  };

  if (Parent) {
    if (IsRef || (!isa<ParmVarDecl>(D) && isFunctionLocalSymbol(D))) {
      addRelation(SymbolRelation{
        (unsigned)SymbolRole::RelationContainedBy,
        Parent
      });
    } else {
      addRelation(SymbolRelation{
        (unsigned)SymbolRole::RelationChildOf,
        Parent
      });
    }
  }

  for (auto &Rel : Relations) {
    addRelation(SymbolRelation(Rel.Roles,
                               Rel.RelatedSymbol->getCanonicalDecl()));
  }

  IndexDataConsumer::ASTNodeInfo Node{OrigE, OrigD, Parent, ContainerDC};
  return DataConsumer.handleDeclOccurrence(D, Roles, FinalRelations, Loc, Node);
}

void IndexingContext::handleMacroDefined(const IdentifierInfo &Name,
                                         SourceLocation Loc,
                                         const MacroInfo &MI) {
  if (!shouldIndexMacroOccurrence(/*IsRef=*/false, Loc))
    return;
  SymbolRoleSet Roles = (unsigned)SymbolRole::Definition;
  DataConsumer.handleMacroOccurrence(&Name, &MI, Roles, Loc);
}

void IndexingContext::handleMacroUndefined(const IdentifierInfo &Name,
                                           SourceLocation Loc,
                                           const MacroInfo &MI) {
  if (!shouldIndexMacroOccurrence(/*IsRef=*/false, Loc))
    return;
  SymbolRoleSet Roles = (unsigned)SymbolRole::Undefinition;
  DataConsumer.handleMacroOccurrence(&Name, &MI, Roles, Loc);
}

void IndexingContext::handleMacroReference(const IdentifierInfo &Name,
                                           SourceLocation Loc,
                                           const MacroInfo &MI) {
  if (!shouldIndexMacroOccurrence(/*IsRef=*/true, Loc))
    return;
  SymbolRoleSet Roles = (unsigned)SymbolRole::Reference;
  DataConsumer.handleMacroOccurrence(&Name, &MI, Roles, Loc);
}

bool IndexingContext::shouldIndexMacroOccurrence(bool IsRef,
                                                 SourceLocation Loc) {
  if (!IndexOpts.IndexMacros)
    return false;

  switch (IndexOpts.SystemSymbolFilter) {
  case IndexingOptions::SystemSymbolFilterKind::None:
    break;
  case IndexingOptions::SystemSymbolFilterKind::DeclarationsOnly:
    if (!IsRef)
      return true;
    break;
  case IndexingOptions::SystemSymbolFilterKind::All:
    return true;
  }

  SourceManager &SM = Ctx->getSourceManager();
  FileID FID = SM.getFileID(SM.getFileLoc(Loc));
  if (FID.isInvalid())
    return false;

  bool Invalid = false;
  const SrcMgr::SLocEntry &SEntry = SM.getSLocEntry(FID, &Invalid);
  if (Invalid || !SEntry.isFile())
    return false;

  return SEntry.getFile().getFileCharacteristic() == SrcMgr::C_User;
}
