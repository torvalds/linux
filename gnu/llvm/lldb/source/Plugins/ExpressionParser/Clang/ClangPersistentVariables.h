//===-- ClangPersistentVariables.h ------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_CLANG_CLANGPERSISTENTVARIABLES_H
#define LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_CLANG_CLANGPERSISTENTVARIABLES_H

#include "llvm/ADT/DenseMap.h"

#include "ClangExpressionVariable.h"
#include "ClangModulesDeclVendor.h"

#include "lldb/Expression/ExpressionVariable.h"
#include <optional>

namespace lldb_private {

class ClangASTImporter;
class ClangModulesDeclVendor;
class Target;
class TypeSystemClang;

/// \class ClangPersistentVariables ClangPersistentVariables.h
/// "lldb/Expression/ClangPersistentVariables.h" Manages persistent values
/// that need to be preserved between expression invocations.
///
/// A list of variables that can be accessed and updated by any expression.  See
/// ClangPersistentVariable for more discussion.  Also provides an increasing,
/// 0-based counter for naming result variables.
class ClangPersistentVariables
    : public llvm::RTTIExtends<ClangPersistentVariables,
                               PersistentExpressionState> {
public:
  // LLVM RTTI support
  static char ID;

  ClangPersistentVariables(std::shared_ptr<Target> target_sp);

  ~ClangPersistentVariables() override = default;

  std::shared_ptr<ClangASTImporter> GetClangASTImporter();
  std::shared_ptr<ClangModulesDeclVendor> GetClangModulesDeclVendor();

  lldb::ExpressionVariableSP
  CreatePersistentVariable(const lldb::ValueObjectSP &valobj_sp) override;

  lldb::ExpressionVariableSP CreatePersistentVariable(
      ExecutionContextScope *exe_scope, ConstString name,
      const CompilerType &compiler_type, lldb::ByteOrder byte_order,
      uint32_t addr_byte_size) override;

  void RemovePersistentVariable(lldb::ExpressionVariableSP variable) override;

  ConstString GetNextPersistentVariableName(bool is_error = false) override;

  /// Returns the next file name that should be used for user expressions.
  std::string GetNextExprFileName() {
    std::string name;
    name.append("<user expression ");
    name.append(std::to_string(m_next_user_file_id++));
    name.append(">");
    return name;
  }

  std::optional<CompilerType>
  GetCompilerTypeFromPersistentDecl(ConstString type_name) override;

  void RegisterPersistentDecl(ConstString name, clang::NamedDecl *decl,
                              std::shared_ptr<TypeSystemClang> ctx);

  clang::NamedDecl *GetPersistentDecl(ConstString name);

  void AddHandLoadedClangModule(ClangModulesDeclVendor::ModuleID module) {
    m_hand_loaded_clang_modules.push_back(module);
  }

  const ClangModulesDeclVendor::ModuleVector &GetHandLoadedClangModules() {
    return m_hand_loaded_clang_modules;
  }

protected:
  llvm::StringRef
  GetPersistentVariablePrefix(bool is_error = false) const override {
    return "$";
  }

private:
  /// The counter used by GetNextExprFileName.
  uint32_t m_next_user_file_id = 0;
  // The counter used by GetNextPersistentVariableName
  uint32_t m_next_persistent_variable_id = 0;

  struct PersistentDecl {
    /// The persistent decl.
    clang::NamedDecl *m_decl = nullptr;
    /// The TypeSystemClang for the ASTContext of m_decl.
    lldb::TypeSystemWP m_context;
  };

  typedef llvm::DenseMap<const char *, PersistentDecl> PersistentDeclMap;
  PersistentDeclMap
      m_persistent_decls; ///< Persistent entities declared by the user.

  ClangModulesDeclVendor::ModuleVector
      m_hand_loaded_clang_modules; ///< These are Clang modules we hand-loaded;
                                   ///these are the highest-
                                   ///< priority source for macros.
  std::shared_ptr<ClangASTImporter> m_ast_importer_sp;
  std::shared_ptr<ClangModulesDeclVendor> m_modules_decl_vendor_sp;
  std::shared_ptr<Target> m_target_sp;
};

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_CLANG_CLANGPERSISTENTVARIABLES_H
