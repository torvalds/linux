//===- IndexingContext.h - Indexing context data ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_INDEX_INDEXINGCONTEXT_H
#define LLVM_CLANG_LIB_INDEX_INDEXINGCONTEXT_H

#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/LLVM.h"
#include "clang/Index/IndexSymbol.h"
#include "clang/Index/IndexingAction.h"
#include "clang/Lex/MacroInfo.h"
#include "llvm/ADT/ArrayRef.h"

namespace clang {
  class ASTContext;
  class Decl;
  class DeclGroupRef;
  class ImportDecl;
  class TagDecl;
  class TypeSourceInfo;
  class NamedDecl;
  class ObjCMethodDecl;
  class DeclContext;
  class NestedNameSpecifierLoc;
  class Stmt;
  class Expr;
  class TypeLoc;
  class SourceLocation;

namespace index {
  class IndexDataConsumer;

class IndexingContext {
  IndexingOptions IndexOpts;
  IndexDataConsumer &DataConsumer;
  ASTContext *Ctx = nullptr;

public:
  IndexingContext(IndexingOptions IndexOpts, IndexDataConsumer &DataConsumer)
    : IndexOpts(IndexOpts), DataConsumer(DataConsumer) {}

  const IndexingOptions &getIndexOpts() const { return IndexOpts; }
  IndexDataConsumer &getDataConsumer() { return DataConsumer; }

  void setASTContext(ASTContext &ctx) { Ctx = &ctx; }

  bool shouldIndex(const Decl *D);

  const LangOptions &getLangOpts() const;

  bool shouldSuppressRefs() const {
    return false;
  }

  bool shouldIndexFunctionLocalSymbols() const;

  bool shouldIndexImplicitInstantiation() const;

  bool shouldIndexParametersInDeclarations() const;

  bool shouldIndexTemplateParameters() const;

  static bool isTemplateImplicitInstantiation(const Decl *D);

  bool handleDecl(const Decl *D, SymbolRoleSet Roles = SymbolRoleSet(),
                  ArrayRef<SymbolRelation> Relations = std::nullopt);

  bool handleDecl(const Decl *D, SourceLocation Loc,
                  SymbolRoleSet Roles = SymbolRoleSet(),
                  ArrayRef<SymbolRelation> Relations = std::nullopt,
                  const DeclContext *DC = nullptr);

  bool handleReference(const NamedDecl *D, SourceLocation Loc,
                       const NamedDecl *Parent, const DeclContext *DC,
                       SymbolRoleSet Roles = SymbolRoleSet(),
                       ArrayRef<SymbolRelation> Relations = std::nullopt,
                       const Expr *RefE = nullptr);

  void handleMacroDefined(const IdentifierInfo &Name, SourceLocation Loc,
                          const MacroInfo &MI);

  void handleMacroUndefined(const IdentifierInfo &Name, SourceLocation Loc,
                            const MacroInfo &MI);

  void handleMacroReference(const IdentifierInfo &Name, SourceLocation Loc,
                            const MacroInfo &MD);

  bool importedModule(const ImportDecl *ImportD);

  bool indexDecl(const Decl *D);

  void indexTagDecl(const TagDecl *D,
                    ArrayRef<SymbolRelation> Relations = std::nullopt);

  void indexTypeSourceInfo(TypeSourceInfo *TInfo, const NamedDecl *Parent,
                           const DeclContext *DC = nullptr,
                           bool isBase = false,
                           bool isIBType = false);

  void indexTypeLoc(TypeLoc TL, const NamedDecl *Parent,
                    const DeclContext *DC = nullptr,
                    bool isBase = false,
                    bool isIBType = false);

  void indexNestedNameSpecifierLoc(NestedNameSpecifierLoc NNS,
                                   const NamedDecl *Parent,
                                   const DeclContext *DC = nullptr);

  bool indexDeclContext(const DeclContext *DC);

  void indexBody(const Stmt *S, const NamedDecl *Parent,
                 const DeclContext *DC = nullptr);

  bool indexTopLevelDecl(const Decl *D);
  bool indexDeclGroupRef(DeclGroupRef DG);

private:
  bool shouldIgnoreIfImplicit(const Decl *D);

  bool shouldIndexMacroOccurrence(bool IsRef, SourceLocation Loc);

  bool handleDeclOccurrence(const Decl *D, SourceLocation Loc,
                            bool IsRef, const Decl *Parent,
                            SymbolRoleSet Roles,
                            ArrayRef<SymbolRelation> Relations,
                            const Expr *RefE,
                            const Decl *RefD,
                            const DeclContext *ContainerDC);
};

} // end namespace index
} // end namespace clang

#endif
