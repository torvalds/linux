//===-- CompilerDeclContext.cpp -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Symbol/CompilerDeclContext.h"
#include "lldb/Symbol/CompilerDecl.h"
#include "lldb/Symbol/TypeSystem.h"
#include <vector>

using namespace lldb_private;

std::vector<CompilerDecl>
CompilerDeclContext::FindDeclByName(ConstString name,
                                    const bool ignore_using_decls) {
  if (IsValid())
    return m_type_system->DeclContextFindDeclByName(m_opaque_decl_ctx, name,
                                                    ignore_using_decls);
  return std::vector<CompilerDecl>();
}

ConstString CompilerDeclContext::GetName() const {
  if (IsValid())
    return m_type_system->DeclContextGetName(m_opaque_decl_ctx);
  return ConstString();
}

ConstString CompilerDeclContext::GetScopeQualifiedName() const {
  if (IsValid())
    return m_type_system->DeclContextGetScopeQualifiedName(m_opaque_decl_ctx);
  return ConstString();
}

bool CompilerDeclContext::IsClassMethod() {
  if (IsValid())
    return m_type_system->DeclContextIsClassMethod(m_opaque_decl_ctx);
  return false;
}

lldb::LanguageType CompilerDeclContext::GetLanguage() {
  if (IsValid())
    return m_type_system->DeclContextGetLanguage(m_opaque_decl_ctx);
  return {};
}

bool CompilerDeclContext::IsContainedInLookup(CompilerDeclContext other) const {
  if (!IsValid())
    return false;

  // If the other context is just the current context, we don't need to go
  // over the type system to know that the lookup is identical.
  if (this == &other)
    return true;

  return m_type_system->DeclContextIsContainedInLookup(m_opaque_decl_ctx,
                                                       other.m_opaque_decl_ctx);
}

std::vector<lldb_private::CompilerContext>
CompilerDeclContext::GetCompilerContext() const {
  if (IsValid())
    return m_type_system->DeclContextGetCompilerContext(m_opaque_decl_ctx);
  return {};
}

bool lldb_private::operator==(const lldb_private::CompilerDeclContext &lhs,
                              const lldb_private::CompilerDeclContext &rhs) {
  return lhs.GetTypeSystem() == rhs.GetTypeSystem() &&
         lhs.GetOpaqueDeclContext() == rhs.GetOpaqueDeclContext();
}

bool lldb_private::operator!=(const lldb_private::CompilerDeclContext &lhs,
                              const lldb_private::CompilerDeclContext &rhs) {
  return lhs.GetTypeSystem() != rhs.GetTypeSystem() ||
         lhs.GetOpaqueDeclContext() != rhs.GetOpaqueDeclContext();
}
