//===-- ClangPersistentVariables.cpp --------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ClangPersistentVariables.h"
#include "ClangASTImporter.h"
#include "ClangModulesDeclVendor.h"

#include "Plugins/TypeSystem/Clang/TypeSystemClang.h"
#include "lldb/Core/Value.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/StreamString.h"

#include "clang/AST/Decl.h"

#include "llvm/ADT/StringMap.h"
#include <optional>
#include <memory>

using namespace lldb;
using namespace lldb_private;

char ClangPersistentVariables::ID;

ClangPersistentVariables::ClangPersistentVariables(
    std::shared_ptr<Target> target_sp)
    : m_target_sp(target_sp) {}

ExpressionVariableSP ClangPersistentVariables::CreatePersistentVariable(
    const lldb::ValueObjectSP &valobj_sp) {
  return AddNewlyConstructedVariable(new ClangExpressionVariable(valobj_sp));
}

ExpressionVariableSP ClangPersistentVariables::CreatePersistentVariable(
    ExecutionContextScope *exe_scope, ConstString name,
    const CompilerType &compiler_type, lldb::ByteOrder byte_order,
    uint32_t addr_byte_size) {
  return AddNewlyConstructedVariable(new ClangExpressionVariable(
      exe_scope, name, compiler_type, byte_order, addr_byte_size));
}

void ClangPersistentVariables::RemovePersistentVariable(
    lldb::ExpressionVariableSP variable) {
  RemoveVariable(variable);

  // Check if the removed variable was the last one that was created. If yes,
  // reuse the variable id for the next variable.

  // Nothing to do if we have not assigned a variable id so far.
  if (m_next_persistent_variable_id == 0)
    return;

  llvm::StringRef name = variable->GetName().GetStringRef();
  // Remove the prefix from the variable that only the indes is left.
  if (!name.consume_front(GetPersistentVariablePrefix(false)))
    return;

  // Check if the variable contained a variable id.
  uint32_t variable_id;
  if (name.getAsInteger(10, variable_id))
    return;
  // If it's the most recent variable id that was assigned, make sure that this
  // variable id will be used for the next persistent variable.
  if (variable_id == m_next_persistent_variable_id - 1)
    m_next_persistent_variable_id--;
}

std::optional<CompilerType>
ClangPersistentVariables::GetCompilerTypeFromPersistentDecl(
    ConstString type_name) {
  PersistentDecl p = m_persistent_decls.lookup(type_name.GetCString());

  if (p.m_decl == nullptr)
    return std::nullopt;

  if (clang::TypeDecl *tdecl = llvm::dyn_cast<clang::TypeDecl>(p.m_decl)) {
    opaque_compiler_type_t t = static_cast<opaque_compiler_type_t>(
        const_cast<clang::Type *>(tdecl->getTypeForDecl()));
    return CompilerType(p.m_context, t);
  }
  return std::nullopt;
}

void ClangPersistentVariables::RegisterPersistentDecl(
    ConstString name, clang::NamedDecl *decl,
    std::shared_ptr<TypeSystemClang> ctx) {
  PersistentDecl p = {decl, ctx};
  m_persistent_decls.insert(std::make_pair(name.GetCString(), p));

  if (clang::EnumDecl *enum_decl = llvm::dyn_cast<clang::EnumDecl>(decl)) {
    for (clang::EnumConstantDecl *enumerator_decl : enum_decl->enumerators()) {
      p = {enumerator_decl, ctx};
      m_persistent_decls.insert(std::make_pair(
          ConstString(enumerator_decl->getNameAsString()).GetCString(), p));
    }
  }
}

clang::NamedDecl *
ClangPersistentVariables::GetPersistentDecl(ConstString name) {
  return m_persistent_decls.lookup(name.GetCString()).m_decl;
}

std::shared_ptr<ClangASTImporter>
ClangPersistentVariables::GetClangASTImporter() {
  if (!m_ast_importer_sp) {
    m_ast_importer_sp = std::make_shared<ClangASTImporter>();
  }
  return m_ast_importer_sp;
}

std::shared_ptr<ClangModulesDeclVendor>
ClangPersistentVariables::GetClangModulesDeclVendor() {
  if (!m_modules_decl_vendor_sp) {
    m_modules_decl_vendor_sp.reset(
        ClangModulesDeclVendor::Create(*m_target_sp));
  }
  return m_modules_decl_vendor_sp;
}

ConstString
ClangPersistentVariables::GetNextPersistentVariableName(bool is_error) {
  llvm::SmallString<64> name;
  {
    llvm::raw_svector_ostream os(name);
    os << GetPersistentVariablePrefix(is_error)
       << m_next_persistent_variable_id++;
  }
  return ConstString(name);
}
