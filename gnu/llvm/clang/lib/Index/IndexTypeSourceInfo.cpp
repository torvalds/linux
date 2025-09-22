//===- IndexTypeSourceInfo.cpp - Indexing types ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "IndexingContext.h"
#include "clang/AST/ASTConcept.h"
#include "clang/AST/PrettyPrinter.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/TypeLoc.h"
#include "llvm/ADT/ScopeExit.h"

using namespace clang;
using namespace index;

namespace {

class TypeIndexer : public RecursiveASTVisitor<TypeIndexer> {
  IndexingContext &IndexCtx;
  const NamedDecl *Parent;
  const DeclContext *ParentDC;
  bool IsBase;
  SmallVector<SymbolRelation, 3> Relations;

  typedef RecursiveASTVisitor<TypeIndexer> base;

public:
  TypeIndexer(IndexingContext &indexCtx, const NamedDecl *parent,
              const DeclContext *DC, bool isBase, bool isIBType)
    : IndexCtx(indexCtx), Parent(parent), ParentDC(DC), IsBase(isBase) {
    if (IsBase) {
      assert(Parent);
      Relations.emplace_back((unsigned)SymbolRole::RelationBaseOf, Parent);
    }
    if (isIBType) {
      assert(Parent);
      Relations.emplace_back((unsigned)SymbolRole::RelationIBTypeOf, Parent);
    }
  }

  bool shouldWalkTypesOfTypeLocs() const { return false; }

#define TRY_TO(CALL_EXPR)                                                      \
  do {                                                                         \
    if (!CALL_EXPR)                                                            \
      return false;                                                            \
  } while (0)

  bool VisitTemplateTypeParmTypeLoc(TemplateTypeParmTypeLoc TTPL) {
    SourceLocation Loc = TTPL.getNameLoc();
    TemplateTypeParmDecl *TTPD = TTPL.getDecl();
    return IndexCtx.handleReference(TTPD, Loc, Parent, ParentDC,
                                    SymbolRoleSet());
  }

  bool VisitTypedefTypeLoc(TypedefTypeLoc TL) {
    SourceLocation Loc = TL.getNameLoc();
    TypedefNameDecl *ND = TL.getTypedefNameDecl();
    if (ND->isTransparentTag()) {
      TagDecl *Underlying = ND->getUnderlyingType()->getAsTagDecl();
      return IndexCtx.handleReference(Underlying, Loc, Parent,
                                      ParentDC, SymbolRoleSet(), Relations);
    }
    if (IsBase) {
      TRY_TO(IndexCtx.handleReference(ND, Loc,
                                      Parent, ParentDC, SymbolRoleSet()));
      if (auto *CD = TL.getType()->getAsCXXRecordDecl()) {
        TRY_TO(IndexCtx.handleReference(CD, Loc, Parent, ParentDC,
                                        (unsigned)SymbolRole::Implicit,
                                        Relations));
      }
    } else {
      TRY_TO(IndexCtx.handleReference(ND, Loc,
                                      Parent, ParentDC, SymbolRoleSet(),
                                      Relations));
    }
    return true;
  }

  bool VisitAutoTypeLoc(AutoTypeLoc TL) {
    if (auto *C = TL.getNamedConcept())
      return IndexCtx.handleReference(C, TL.getConceptNameLoc(), Parent,
                                      ParentDC);
    return true;
  }

  bool traverseParamVarHelper(ParmVarDecl *D) {
    TRY_TO(TraverseNestedNameSpecifierLoc(D->getQualifierLoc()));
    if (D->getTypeSourceInfo())
      TRY_TO(TraverseTypeLoc(D->getTypeSourceInfo()->getTypeLoc()));
    return true;
  }

  bool TraverseParmVarDecl(ParmVarDecl *D) {
    // Avoid visiting default arguments from the definition that were already
    // visited in the declaration.
    // FIXME: A free function definition can have default arguments.
    // Avoiding double visitaiton of default arguments should be handled by the
    // visitor probably with a bit in the AST to indicate if the attached
    // default argument was 'inherited' or written in source.
    if (auto FD = dyn_cast<FunctionDecl>(D->getDeclContext())) {
      if (FD->isThisDeclarationADefinition()) {
        return traverseParamVarHelper(D);
      }
    }

    return base::TraverseParmVarDecl(D);
  }

  bool TraverseNestedNameSpecifierLoc(NestedNameSpecifierLoc NNS) {
    IndexCtx.indexNestedNameSpecifierLoc(NNS, Parent, ParentDC);
    return true;
  }

  bool VisitTagTypeLoc(TagTypeLoc TL) {
    TagDecl *D = TL.getDecl();
    if (!IndexCtx.shouldIndexFunctionLocalSymbols() &&
        D->getParentFunctionOrMethod())
      return true;

    if (TL.isDefinition()) {
      IndexCtx.indexTagDecl(D);
      return true;
    }

    return IndexCtx.handleReference(D, TL.getNameLoc(),
                                    Parent, ParentDC, SymbolRoleSet(),
                                    Relations);
  }

  bool VisitObjCInterfaceTypeLoc(ObjCInterfaceTypeLoc TL) {
    return IndexCtx.handleReference(TL.getIFaceDecl(), TL.getNameLoc(),
                                    Parent, ParentDC, SymbolRoleSet(), Relations);
  }

  bool VisitObjCObjectTypeLoc(ObjCObjectTypeLoc TL) {
    for (unsigned i = 0, e = TL.getNumProtocols(); i != e; ++i) {
      IndexCtx.handleReference(TL.getProtocol(i), TL.getProtocolLoc(i),
                               Parent, ParentDC, SymbolRoleSet(), Relations);
    }
    return true;
  }

  void HandleTemplateSpecializationTypeLoc(TemplateName TemplName,
                                           SourceLocation TemplNameLoc,
                                           CXXRecordDecl *ResolvedClass,
                                           bool IsTypeAlias) {
    // In presence of type aliases, the resolved class was never written in
    // the code so don't report it.
    if (!IsTypeAlias && ResolvedClass &&
        (!ResolvedClass->isImplicit() ||
         IndexCtx.shouldIndexImplicitInstantiation())) {
      IndexCtx.handleReference(ResolvedClass, TemplNameLoc, Parent, ParentDC,
                               SymbolRoleSet(), Relations);
    } else if (const TemplateDecl *D = TemplName.getAsTemplateDecl()) {
      IndexCtx.handleReference(D, TemplNameLoc, Parent, ParentDC,
                               SymbolRoleSet(), Relations);
    }
  }

  bool VisitTemplateSpecializationTypeLoc(TemplateSpecializationTypeLoc TL) {
    auto *T = TL.getTypePtr();
    if (!T)
      return true;
    HandleTemplateSpecializationTypeLoc(
        T->getTemplateName(), TL.getTemplateNameLoc(), T->getAsCXXRecordDecl(),
        T->isTypeAlias());
    return true;
  }

  bool TraverseTemplateSpecializationTypeLoc(TemplateSpecializationTypeLoc TL) {
    if (!WalkUpFromTemplateSpecializationTypeLoc(TL))
      return false;
    if (!TraverseTemplateName(TL.getTypePtr()->getTemplateName()))
      return false;

    // The relations we have to `Parent` do not apply to our template arguments,
    // so clear them while visiting the args.
    SmallVector<SymbolRelation, 3> SavedRelations = Relations;
    Relations.clear();
    auto ResetSavedRelations =
        llvm::make_scope_exit([&] { this->Relations = SavedRelations; });
    for (unsigned I = 0, E = TL.getNumArgs(); I != E; ++I) {
      if (!TraverseTemplateArgumentLoc(TL.getArgLoc(I)))
        return false;
    }

    return true;
  }

  bool VisitDeducedTemplateSpecializationTypeLoc(DeducedTemplateSpecializationTypeLoc TL) {
    auto *T = TL.getTypePtr();
    if (!T)
      return true;
    HandleTemplateSpecializationTypeLoc(
        T->getTemplateName(), TL.getTemplateNameLoc(), T->getAsCXXRecordDecl(),
        /*IsTypeAlias=*/false);
    return true;
  }

  bool VisitInjectedClassNameTypeLoc(InjectedClassNameTypeLoc TL) {
    return IndexCtx.handleReference(TL.getDecl(), TL.getNameLoc(), Parent,
                                    ParentDC, SymbolRoleSet(), Relations);
  }

  bool VisitDependentNameTypeLoc(DependentNameTypeLoc TL) {
    const DependentNameType *DNT = TL.getTypePtr();
    const NestedNameSpecifier *NNS = DNT->getQualifier();
    const Type *T = NNS->getAsType();
    if (!T)
      return true;
    const TemplateSpecializationType *TST =
        T->getAs<TemplateSpecializationType>();
    if (!TST)
      return true;
    TemplateName TN = TST->getTemplateName();
    const ClassTemplateDecl *TD =
        dyn_cast_or_null<ClassTemplateDecl>(TN.getAsTemplateDecl());
    if (!TD)
      return true;
    CXXRecordDecl *RD = TD->getTemplatedDecl();
    if (!RD->hasDefinition())
      return true;
    RD = RD->getDefinition();
    DeclarationName Name(DNT->getIdentifier());
    std::vector<const NamedDecl *> Symbols = RD->lookupDependentName(
        Name, [](const NamedDecl *ND) { return isa<TypeDecl>(ND); });
    if (Symbols.size() != 1)
      return true;
    return IndexCtx.handleReference(Symbols[0], TL.getNameLoc(), Parent,
                                    ParentDC, SymbolRoleSet(), Relations);
  }

  bool TraverseStmt(Stmt *S) {
    IndexCtx.indexBody(S, Parent, ParentDC);
    return true;
  }
};

} // anonymous namespace

void IndexingContext::indexTypeSourceInfo(TypeSourceInfo *TInfo,
                                          const NamedDecl *Parent,
                                          const DeclContext *DC,
                                          bool isBase,
                                          bool isIBType) {
  if (!TInfo || TInfo->getTypeLoc().isNull())
    return;

  indexTypeLoc(TInfo->getTypeLoc(), Parent, DC, isBase, isIBType);
}

void IndexingContext::indexTypeLoc(TypeLoc TL,
                                   const NamedDecl *Parent,
                                   const DeclContext *DC,
                                   bool isBase,
                                   bool isIBType) {
  if (TL.isNull())
    return;

  if (!DC)
    DC = Parent->getLexicalDeclContext();
  TypeIndexer(*this, Parent, DC, isBase, isIBType).TraverseTypeLoc(TL);
}

void IndexingContext::indexNestedNameSpecifierLoc(NestedNameSpecifierLoc NNS,
                                                  const NamedDecl *Parent,
                                                  const DeclContext *DC) {
  if (!NNS)
    return;

  if (NestedNameSpecifierLoc Prefix = NNS.getPrefix())
    indexNestedNameSpecifierLoc(Prefix, Parent, DC);

  if (!DC)
    DC = Parent->getLexicalDeclContext();
  SourceLocation Loc = NNS.getLocalBeginLoc();

  switch (NNS.getNestedNameSpecifier()->getKind()) {
  case NestedNameSpecifier::Identifier:
  case NestedNameSpecifier::Global:
  case NestedNameSpecifier::Super:
    break;

  case NestedNameSpecifier::Namespace:
    handleReference(NNS.getNestedNameSpecifier()->getAsNamespace(),
                    Loc, Parent, DC, SymbolRoleSet());
    break;
  case NestedNameSpecifier::NamespaceAlias:
    handleReference(NNS.getNestedNameSpecifier()->getAsNamespaceAlias(),
                    Loc, Parent, DC, SymbolRoleSet());
    break;

  case NestedNameSpecifier::TypeSpec:
  case NestedNameSpecifier::TypeSpecWithTemplate:
    indexTypeLoc(NNS.getTypeLoc(), Parent, DC);
    break;
  }
}

void IndexingContext::indexTagDecl(const TagDecl *D,
                                   ArrayRef<SymbolRelation> Relations) {
  if (!shouldIndex(D))
    return;
  if (!shouldIndexFunctionLocalSymbols() && isFunctionLocalSymbol(D))
    return;

  if (handleDecl(D, /*Roles=*/SymbolRoleSet(), Relations)) {
    if (D->isThisDeclarationADefinition()) {
      indexNestedNameSpecifierLoc(D->getQualifierLoc(), D);
      if (auto CXXRD = dyn_cast<CXXRecordDecl>(D)) {
        for (const auto &I : CXXRD->bases()) {
          indexTypeSourceInfo(I.getTypeSourceInfo(), CXXRD, CXXRD, /*isBase=*/true);
        }
      }
      indexDeclContext(D);
    }
  }
}
