//===-- NameSearchContext.h -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_CLANG_NAME_SEARCH_CONTEXT_H
#define LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_CLANG_NAME_SEARCH_CONTEXT_H

#include "Plugins/ExpressionParser/Clang/ClangASTImporter.h"
#include "Plugins/TypeSystem/Clang/TypeSystemClang.h"
#include "lldb/Symbol/CompilerType.h"
#include "llvm/ADT/SmallSet.h"

namespace lldb_private {

/// \class NameSearchContext ClangASTSource.h
/// "lldb/Expression/ClangASTSource.h" Container for all objects relevant to a
/// single name lookup
///
/// LLDB needs to create Decls for entities it finds.  This class communicates
/// what name is being searched for and provides helper functions to construct
/// Decls given appropriate type information.
struct NameSearchContext {
  /// The type system of the AST from which the lookup originated.
  TypeSystemClang &m_clang_ts;
  /// The list of declarations already constructed.
  llvm::SmallVectorImpl<clang::NamedDecl *> &m_decls;
  /// The mapping of all namespaces found for this request back to their
  /// modules.
  ClangASTImporter::NamespaceMapSP m_namespace_map;
  /// The name being looked for.
  const clang::DeclarationName m_decl_name;
  /// The DeclContext to put declarations into.
  const clang::DeclContext *m_decl_context;
  /// All the types of functions that have been reported, so we don't
  /// report conflicts.
  llvm::SmallSet<CompilerType, 5> m_function_types;

  bool m_found_variable = false;
  bool m_found_function_with_type_info = false;
  bool m_found_local_vars_nsp = false;
  bool m_found_type = false;

  /// Constructor
  ///
  /// Initializes class variables.
  ///
  /// \param[in] clang_ts
  ///     The TypeSystemClang from which the request originates.
  ///
  /// \param[in] decls
  ///     A reference to a list into which new Decls will be placed.  This
  ///     list is typically empty when the function is called.
  ///
  /// \param[in] name
  ///     The name being searched for (always an Identifier).
  ///
  /// \param[in] dc
  ///     The DeclContext to register Decls in.
  NameSearchContext(TypeSystemClang &clang_ts,
                    llvm::SmallVectorImpl<clang::NamedDecl *> &decls,
                    clang::DeclarationName name, const clang::DeclContext *dc)
      : m_clang_ts(clang_ts), m_decls(decls),
        m_namespace_map(std::make_shared<ClangASTImporter::NamespaceMap>()),
        m_decl_name(name), m_decl_context(dc) {
    ;
  }

  /// Create a VarDecl with the name being searched for and the provided type
  /// and register it in the right places.
  ///
  /// \param[in] type
  ///     The opaque QualType for the VarDecl being registered.
  clang::NamedDecl *AddVarDecl(const CompilerType &type);

  /// Create a FunDecl with the name being searched for and the provided type
  /// and register it in the right places.
  ///
  /// \param[in] type
  ///     The opaque QualType for the FunDecl being registered.
  ///
  /// \param[in] extern_c
  ///     If true, build an extern "C" linkage specification for this.
  clang::NamedDecl *AddFunDecl(const CompilerType &type, bool extern_c = false);

  /// Create a FunDecl with the name being searched for and generic type (i.e.
  /// intptr_t NAME_GOES_HERE(...)) and register it in the right places.
  clang::NamedDecl *AddGenericFunDecl();

  /// Create a TypeDecl with the name being searched for and the provided type
  /// and register it in the right places.
  ///
  /// \param[in] compiler_type
  ///     The opaque QualType for the TypeDecl being registered.
  clang::NamedDecl *AddTypeDecl(const CompilerType &compiler_type);

  /// Add Decls from the provided DeclContextLookupResult to the list of
  /// results.
  ///
  /// \param[in] result
  ///     The DeclContextLookupResult, usually returned as the result
  ///     of querying a DeclContext.
  void AddLookupResult(clang::DeclContextLookupResult result);

  /// Add a NamedDecl to the list of results.
  ///
  /// \param[in] decl
  ///     The NamedDecl, usually returned as the result
  ///     of querying a DeclContext.
  void AddNamedDecl(clang::NamedDecl *decl);

private:
  clang::ASTContext &GetASTContext() const {
    return m_clang_ts.getASTContext();
  }
};

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_CLANG_NAME_SEARCH_CONTEXT_H
