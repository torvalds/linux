//===- ASTImporterLookupTable.h - ASTImporter specific lookup--*- C++ -*---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines the ASTImporterLookupTable class which implements a
//  lookup procedure for the import mechanism.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_ASTIMPORTERLOOKUPTABLE_H
#define LLVM_CLANG_AST_ASTIMPORTERLOOKUPTABLE_H

#include "clang/AST/DeclBase.h" // lookup_result
#include "clang/AST/DeclarationName.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SetVector.h"

namespace clang {

class NamedDecl;
class DeclContext;

// There are certain cases when normal C/C++ lookup (localUncachedLookup)
// does not find AST nodes. E.g.:
// Example 1:
//   template <class T>
//   struct X {
//     friend void foo(); // this is never found in the DC of the TU.
//   };
// Example 2:
//   // The fwd decl to Foo is not found in the lookupPtr of the DC of the
//   // translation unit decl.
//   // Here we could find the node by doing a traverse throught the list of
//   // the Decls in the DC, but that would not scale.
//   struct A { struct Foo *p; };
// This is a severe problem because the importer decides if it has to create a
// new Decl or not based on the lookup results.
// To overcome these cases we need an importer specific lookup table which
// holds every node and we are not interested in any C/C++ specific visibility
// considerations. Simply, we must know if there is an existing Decl in a
// given DC. Once we found it then we can handle any visibility related tasks.
class ASTImporterLookupTable {

  // We store a list of declarations for each name.
  // And we collect these lists for each DeclContext.
  // We could have a flat map with (DeclContext, Name) tuple as key, but a two
  // level map seems easier to handle.
  using DeclList = llvm::SmallSetVector<NamedDecl *, 2>;
  using NameMap = llvm::SmallDenseMap<DeclarationName, DeclList, 4>;
  using DCMap = llvm::DenseMap<DeclContext *, NameMap>;

  void add(DeclContext *DC, NamedDecl *ND);
  void remove(DeclContext *DC, NamedDecl *ND);

  DCMap LookupTable;

public:
  ASTImporterLookupTable(TranslationUnitDecl &TU);
  void add(NamedDecl *ND);
  void remove(NamedDecl *ND);
  // Sometimes a declaration is created first with a temporarily value of decl
  // context (often the translation unit) and later moved to the final context.
  // This happens for declarations that are created before the final declaration
  // context. In such cases the lookup table needs to be updated.
  // (The declaration is in these cases not added to the temporary decl context,
  // only its parent is set.)
  // FIXME: It would be better to not add the declaration to the temporary
  // context at all in the lookup table, but this requires big change in
  // ASTImporter.
  // The function should be called when the old context is definitely different
  // from the new.
  void update(NamedDecl *ND, DeclContext *OldDC);
  // Same as 'update' but allow if 'ND' is not in the table or the old context
  // is the same as the new.
  // FIXME: The old redeclaration context is not handled.
  void updateForced(NamedDecl *ND, DeclContext *OldDC);
  using LookupResult = DeclList;
  LookupResult lookup(DeclContext *DC, DeclarationName Name) const;
  // Check if the `ND` is within the lookup table (with its current name) in
  // context `DC`. This is intended for debug purposes when the DeclContext of a
  // NamedDecl is changed.
  bool contains(DeclContext *DC, NamedDecl *ND) const;
  void dump(DeclContext *DC) const;
  void dump() const;
};

} // namespace clang

#endif // LLVM_CLANG_AST_ASTIMPORTERLOOKUPTABLE_H
