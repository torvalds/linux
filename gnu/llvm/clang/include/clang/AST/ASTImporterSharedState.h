//===- ASTImporterSharedState.h - ASTImporter specific state --*- C++ -*---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines the ASTImporter specific state, which may be shared
//  amongst several ASTImporter objects.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_ASTIMPORTERSHAREDSTATE_H
#define LLVM_CLANG_AST_ASTIMPORTERSHAREDSTATE_H

#include "clang/AST/ASTImportError.h"
#include "clang/AST/ASTImporterLookupTable.h"
#include "clang/AST/Decl.h"
#include "llvm/ADT/DenseMap.h"
#include <optional>

namespace clang {

class TranslationUnitDecl;

/// Importer specific state, which may be shared amongst several ASTImporter
/// objects.
class ASTImporterSharedState {

  /// Pointer to the import specific lookup table.
  std::unique_ptr<ASTImporterLookupTable> LookupTable;

  /// Mapping from the already-imported declarations in the "to"
  /// context to the error status of the import of that declaration.
  /// This map contains only the declarations that were not correctly
  /// imported. The same declaration may or may not be included in
  /// ImportedFromDecls. This map is updated continuously during imports and
  /// never cleared (like ImportedFromDecls).
  llvm::DenseMap<Decl *, ASTImportError> ImportErrors;

  /// Set of the newly created declarations.
  llvm::DenseSet<Decl *> NewDecls;

  // FIXME put ImportedFromDecls here!
  // And from that point we can better encapsulate the lookup table.

public:
  ASTImporterSharedState() = default;

  ASTImporterSharedState(TranslationUnitDecl &ToTU) {
    LookupTable = std::make_unique<ASTImporterLookupTable>(ToTU);
  }

  ASTImporterLookupTable *getLookupTable() { return LookupTable.get(); }

  void addDeclToLookup(Decl *D) {
    if (LookupTable)
      if (auto *ND = dyn_cast<NamedDecl>(D))
        LookupTable->add(ND);
  }

  void removeDeclFromLookup(Decl *D) {
    if (LookupTable)
      if (auto *ND = dyn_cast<NamedDecl>(D))
        LookupTable->remove(ND);
  }

  std::optional<ASTImportError> getImportDeclErrorIfAny(Decl *ToD) const {
    auto Pos = ImportErrors.find(ToD);
    if (Pos != ImportErrors.end())
      return Pos->second;
    else
      return std::nullopt;
  }

  void setImportDeclError(Decl *To, ASTImportError Error) {
    ImportErrors[To] = Error;
  }

  bool isNewDecl(const Decl *ToD) const { return NewDecls.count(ToD); }

  void markAsNewDecl(Decl *ToD) { NewDecls.insert(ToD); }
};

} // namespace clang
#endif // LLVM_CLANG_AST_ASTIMPORTERSHAREDSTATE_H
