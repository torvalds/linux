//===-- CompilerDeclContext.h -----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SYMBOL_COMPILERDECLCONTEXT_H
#define LLDB_SYMBOL_COMPILERDECLCONTEXT_H

#include <vector>

#include "lldb/Symbol/Type.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/lldb-private.h"

namespace lldb_private {

/// Represents a generic declaration context in a program. A declaration context
/// is data structure that contains declarations (e.g. namespaces).
///
/// This class serves as an abstraction for a declaration context inside one of
/// the TypeSystems implemented by the language plugins. It does not have any
/// actual logic in it but only stores an opaque pointer and a pointer to the
/// TypeSystem that gives meaning to this opaque pointer. All methods of this
/// class should call their respective method in the TypeSystem interface and
/// pass the opaque pointer along.
///
/// \see lldb_private::TypeSystem
class CompilerDeclContext {
public:
  /// Constructs an invalid CompilerDeclContext.
  CompilerDeclContext() = default;

  /// Constructs a CompilerDeclContext with the given opaque decl context
  /// and its respective TypeSystem instance.
  ///
  /// This constructor should only be called from the respective TypeSystem
  /// implementation.
  ///
  /// \see lldb_private::TypeSystemClang::CreateDeclContext(clang::DeclContext*)
  CompilerDeclContext(TypeSystem *type_system, void *decl_ctx)
      : m_type_system(type_system), m_opaque_decl_ctx(decl_ctx) {}

  // Tests

  explicit operator bool() const { return IsValid(); }

  bool operator<(const CompilerDeclContext &rhs) const {
    if (m_type_system == rhs.m_type_system)
      return m_opaque_decl_ctx < rhs.m_opaque_decl_ctx;
    return m_type_system < rhs.m_type_system;
  }

  bool IsValid() const {
    return m_type_system != nullptr && m_opaque_decl_ctx != nullptr;
  }

  /// Populate a valid compiler context from the current decl context.
  ///
  /// \returns A valid vector of CompilerContext entries that describes
  /// this declaration context. The first entry in the vector is the parent of
  /// the subsequent entry, so the topmost entry is the global namespace.
  std::vector<lldb_private::CompilerContext> GetCompilerContext() const;

  std::vector<CompilerDecl> FindDeclByName(ConstString name,
                                           const bool ignore_using_decls);

  /// Checks if this decl context represents a method of a class.
  ///
  /// \return
  ///     Returns true if this is a decl context that represents a method
  ///     in a struct, union or class.
  bool IsClassMethod();

  /// Determines the original language of the decl context.
  lldb::LanguageType GetLanguage();

  /// Check if the given other decl context is contained in the lookup
  /// of this decl context (for example because the other context is a nested
  /// inline namespace).
  ///
  /// @param[in] other
  ///     The other decl context for which we should check if it is contained
  ///     in the lookoup of this context.
  ///
  /// @return
  ///     Returns true iff the other decl context is contained in the lookup
  ///     of this decl context.
  bool IsContainedInLookup(CompilerDeclContext other) const;

  // Accessors

  TypeSystem *GetTypeSystem() const { return m_type_system; }

  void *GetOpaqueDeclContext() const { return m_opaque_decl_ctx; }

  void SetDeclContext(TypeSystem *type_system, void *decl_ctx) {
    m_type_system = type_system;
    m_opaque_decl_ctx = decl_ctx;
  }

  void Clear() {
    m_type_system = nullptr;
    m_opaque_decl_ctx = nullptr;
  }

  ConstString GetName() const;

  ConstString GetScopeQualifiedName() const;

private:
  TypeSystem *m_type_system = nullptr;
  void *m_opaque_decl_ctx = nullptr;
};

bool operator==(const CompilerDeclContext &lhs, const CompilerDeclContext &rhs);
bool operator!=(const CompilerDeclContext &lhs, const CompilerDeclContext &rhs);

} // namespace lldb_private

#endif // LLDB_SYMBOL_COMPILERDECLCONTEXT_H
