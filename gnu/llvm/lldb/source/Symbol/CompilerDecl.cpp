//===-- CompilerDecl.cpp --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Symbol/CompilerDecl.h"
#include "lldb/Symbol/CompilerDeclContext.h"
#include "lldb/Symbol/TypeSystem.h"
#include "lldb/Utility/Scalar.h"

using namespace lldb_private;

ConstString CompilerDecl::GetName() const {
  return m_type_system->DeclGetName(m_opaque_decl);
}

ConstString CompilerDecl::GetMangledName() const {
  return m_type_system->DeclGetMangledName(m_opaque_decl);
}

CompilerDeclContext CompilerDecl::GetDeclContext() const {
  return m_type_system->DeclGetDeclContext(m_opaque_decl);
}

CompilerType CompilerDecl::GetType() const {
  return m_type_system->GetTypeForDecl(m_opaque_decl);
}

CompilerType CompilerDecl::GetFunctionReturnType() const {
  return m_type_system->DeclGetFunctionReturnType(m_opaque_decl);
}

size_t CompilerDecl::GetNumFunctionArguments() const {
  return m_type_system->DeclGetFunctionNumArguments(m_opaque_decl);
}

CompilerType CompilerDecl::GetFunctionArgumentType(size_t arg_idx) const {
  return m_type_system->DeclGetFunctionArgumentType(m_opaque_decl, arg_idx);
}

bool lldb_private::operator==(const lldb_private::CompilerDecl &lhs,
                              const lldb_private::CompilerDecl &rhs) {
  return lhs.GetTypeSystem() == rhs.GetTypeSystem() &&
         lhs.GetOpaqueDecl() == rhs.GetOpaqueDecl();
}

bool lldb_private::operator!=(const lldb_private::CompilerDecl &lhs,
                              const lldb_private::CompilerDecl &rhs) {
  return lhs.GetTypeSystem() != rhs.GetTypeSystem() ||
         lhs.GetOpaqueDecl() != rhs.GetOpaqueDecl();
}

std::vector<lldb_private::CompilerContext>
CompilerDecl::GetCompilerContext() const {
  return m_type_system->DeclGetCompilerContext(m_opaque_decl);
}

Scalar CompilerDecl::GetConstantValue() const {
  return m_type_system->DeclGetConstantValue(m_opaque_decl);
}
