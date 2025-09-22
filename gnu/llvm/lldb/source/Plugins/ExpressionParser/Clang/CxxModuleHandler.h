//===-- CxxModuleHandler.h --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_CLANG_CXXMODULEHANDLER_H
#define LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_CLANG_CXXMODULEHANDLER_H

#include "clang/AST/ASTImporter.h"
#include "clang/Sema/Sema.h"
#include "llvm/ADT/StringSet.h"
#include <optional>

namespace lldb_private {

/// Handles importing decls into an ASTContext with an attached C++ module.
///
/// This class searches a C++ module (which must be attached to the target
/// ASTContext) for an equivalent decl to the one that should be imported.
/// If the decl that is found in the module is a suitable replacement
/// for the decl that should be imported, the module decl will be treated as
/// the result of the import process.
///
/// If the Decl that should be imported is a template specialization
/// that doesn't exist yet in the target ASTContext (e.g. `std::vector<int>`),
/// then this class tries to create the template specialization in the target
/// ASTContext. This is only possible if the CxxModuleHandler can determine
/// that instantiating this template is safe to do, e.g. because the target
/// decl is a container class from the STL.
class CxxModuleHandler {
  /// The ASTImporter that should be used to import any Decls which aren't
  /// directly handled by this class itself.
  clang::ASTImporter *m_importer = nullptr;

  /// The Sema instance of the target ASTContext.
  clang::Sema *m_sema = nullptr;

  /// List of template names this class currently supports. These are the
  /// template names inside the 'std' namespace such as 'vector' or 'list'.
  llvm::StringSet<> m_supported_templates;

  /// Tries to manually instantiate the given foreign template in the target
  /// context (designated by m_sema).
  std::optional<clang::Decl *> tryInstantiateStdTemplate(clang::Decl *d);

public:
  CxxModuleHandler() = default;
  CxxModuleHandler(clang::ASTImporter &importer, clang::ASTContext *target);

  /// Attempts to import the given decl into the target ASTContext by
  /// deserializing it from the 'std' module. This function returns a Decl if a
  /// Decl has been deserialized from the 'std' module. Otherwise this function
  /// returns nothing.
  std::optional<clang::Decl *> Import(clang::Decl *d);

  /// Returns true iff this instance is capable of importing any declarations
  /// in the target ASTContext.
  bool isValid() const { return m_sema != nullptr; }
};

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_CLANG_CXXMODULEHANDLER_H
