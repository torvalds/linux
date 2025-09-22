//===-- ClangExpressionVariable.cpp ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ClangExpressionVariable.h"

#include "lldb/Core/Value.h"
#include "lldb/Core/ValueObjectConstResult.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Target/Process.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/Stream.h"
#include "clang/AST/ASTContext.h"

using namespace lldb_private;
using namespace clang;

char ClangExpressionVariable::ID;

ClangExpressionVariable::ClangExpressionVariable(
    ExecutionContextScope *exe_scope, lldb::ByteOrder byte_order,
    uint32_t addr_byte_size)
    : m_parser_vars(), m_jit_vars() {
  m_flags = EVNone;
  m_frozen_sp =
      ValueObjectConstResult::Create(exe_scope, byte_order, addr_byte_size);
}

ClangExpressionVariable::ClangExpressionVariable(
    ExecutionContextScope *exe_scope, Value &value, ConstString name,
    uint16_t flags)
    : m_parser_vars(), m_jit_vars() {
  m_flags = flags;
  m_frozen_sp = ValueObjectConstResult::Create(exe_scope, value, name);
}

ClangExpressionVariable::ClangExpressionVariable(
    const lldb::ValueObjectSP &valobj_sp)
    : m_parser_vars(), m_jit_vars() {
  m_flags = EVNone;
  m_frozen_sp = valobj_sp;
}

ClangExpressionVariable::ClangExpressionVariable(
    ExecutionContextScope *exe_scope, ConstString name,
    const TypeFromUser &user_type, lldb::ByteOrder byte_order,
    uint32_t addr_byte_size)
    : m_parser_vars(), m_jit_vars() {
  m_flags = EVNone;
  m_frozen_sp =
      ValueObjectConstResult::Create(exe_scope, byte_order, addr_byte_size);
  SetName(name);
  SetCompilerType(user_type);
}

TypeFromUser ClangExpressionVariable::GetTypeFromUser() {
  TypeFromUser tfu(m_frozen_sp->GetCompilerType());
  return tfu;
}
