//===--- IndexDataConsumer.h - Abstract index data consumer -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_INDEX_INDEXDATACONSUMER_H
#define LLVM_CLANG_INDEX_INDEXDATACONSUMER_H

#include "clang/Index/IndexSymbol.h"
#include "clang/Lex/Preprocessor.h"

namespace clang {
  class ASTContext;
  class DeclContext;
  class Expr;
  class FileID;
  class IdentifierInfo;
  class ImportDecl;
  class MacroInfo;

namespace index {

class IndexDataConsumer {
public:
  struct ASTNodeInfo {
    const Expr *OrigE;
    const Decl *OrigD;
    const Decl *Parent;
    const DeclContext *ContainerDC;
  };

  virtual ~IndexDataConsumer() = default;

  virtual void initialize(ASTContext &Ctx) {}

  virtual void setPreprocessor(std::shared_ptr<Preprocessor> PP) {}

  /// \returns true to continue indexing, or false to abort.
  virtual bool handleDeclOccurrence(const Decl *D, SymbolRoleSet Roles,
                                    ArrayRef<SymbolRelation> Relations,
                                    SourceLocation Loc, ASTNodeInfo ASTNode) {
    return true;
  }

  /// \returns true to continue indexing, or false to abort.
  virtual bool handleMacroOccurrence(const IdentifierInfo *Name,
                                     const MacroInfo *MI, SymbolRoleSet Roles,
                                     SourceLocation Loc) {
    return true;
  }

  /// \returns true to continue indexing, or false to abort.
  ///
  /// This will be called for each module reference in an import decl.
  /// For "@import MyMod.SubMod", there will be a call for 'MyMod' with the
  /// 'reference' role, and a call for 'SubMod' with the 'declaration' role.
  virtual bool handleModuleOccurrence(const ImportDecl *ImportD,
                                      const Module *Mod, SymbolRoleSet Roles,
                                      SourceLocation Loc) {
    return true;
  }

  virtual void finish() {}
};

} // namespace index
} // namespace clang

#endif
