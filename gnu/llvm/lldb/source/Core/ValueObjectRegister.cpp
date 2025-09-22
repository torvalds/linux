//===-- ValueObjectRegister.cpp -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Core/ValueObjectRegister.h"

#include "lldb/Core/Module.h"
#include "lldb/Core/Value.h"
#include "lldb/Symbol/CompilerType.h"
#include "lldb/Symbol/TypeSystem.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/StackFrame.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/Scalar.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/Stream.h"

#include "llvm/ADT/StringRef.h"

#include <cassert>
#include <memory>
#include <optional>

namespace lldb_private {
class ExecutionContextScope;
}

using namespace lldb;
using namespace lldb_private;

#pragma mark ValueObjectRegisterSet

ValueObjectSP
ValueObjectRegisterSet::Create(ExecutionContextScope *exe_scope,
                               lldb::RegisterContextSP &reg_ctx_sp,
                               uint32_t set_idx) {
  auto manager_sp = ValueObjectManager::Create();
  return (new ValueObjectRegisterSet(exe_scope, *manager_sp, reg_ctx_sp,
                                     set_idx))
      ->GetSP();
}

ValueObjectRegisterSet::ValueObjectRegisterSet(ExecutionContextScope *exe_scope,
                                               ValueObjectManager &manager,
                                               lldb::RegisterContextSP &reg_ctx,
                                               uint32_t reg_set_idx)
    : ValueObject(exe_scope, manager), m_reg_ctx_sp(reg_ctx),
      m_reg_set(nullptr), m_reg_set_idx(reg_set_idx) {
  assert(reg_ctx);
  m_reg_set = reg_ctx->GetRegisterSet(m_reg_set_idx);
  if (m_reg_set) {
    m_name.SetCString(m_reg_set->name);
  }
}

ValueObjectRegisterSet::~ValueObjectRegisterSet() = default;

CompilerType ValueObjectRegisterSet::GetCompilerTypeImpl() {
  return CompilerType();
}

ConstString ValueObjectRegisterSet::GetTypeName() { return ConstString(); }

ConstString ValueObjectRegisterSet::GetQualifiedTypeName() {
  return ConstString();
}

llvm::Expected<uint32_t>
ValueObjectRegisterSet::CalculateNumChildren(uint32_t max) {
  const RegisterSet *reg_set = m_reg_ctx_sp->GetRegisterSet(m_reg_set_idx);
  if (reg_set) {
    auto reg_count = reg_set->num_registers;
    return reg_count <= max ? reg_count : max;
  }
  return 0;
}

std::optional<uint64_t> ValueObjectRegisterSet::GetByteSize() { return 0; }

bool ValueObjectRegisterSet::UpdateValue() {
  m_error.Clear();
  SetValueDidChange(false);
  ExecutionContext exe_ctx(GetExecutionContextRef());
  StackFrame *frame = exe_ctx.GetFramePtr();
  if (frame == nullptr)
    m_reg_ctx_sp.reset();
  else {
    m_reg_ctx_sp = frame->GetRegisterContext();
    if (m_reg_ctx_sp) {
      const RegisterSet *reg_set = m_reg_ctx_sp->GetRegisterSet(m_reg_set_idx);
      if (reg_set == nullptr)
        m_reg_ctx_sp.reset();
      else if (m_reg_set != reg_set) {
        SetValueDidChange(true);
        m_name.SetCString(reg_set->name);
      }
    }
  }
  if (m_reg_ctx_sp) {
    SetValueIsValid(true);
  } else {
    SetValueIsValid(false);
    m_error.SetErrorToGenericError();
    m_children.Clear();
  }
  return m_error.Success();
}

ValueObject *ValueObjectRegisterSet::CreateChildAtIndex(size_t idx) {
  if (m_reg_ctx_sp && m_reg_set) {
    return new ValueObjectRegister(
        *this, m_reg_ctx_sp,
        m_reg_ctx_sp->GetRegisterInfoAtIndex(m_reg_set->registers[idx]));
  }
  return nullptr;
}

lldb::ValueObjectSP
ValueObjectRegisterSet::GetChildMemberWithName(llvm::StringRef name,
                                               bool can_create) {
  ValueObject *valobj = nullptr;
  if (m_reg_ctx_sp && m_reg_set) {
    const RegisterInfo *reg_info = m_reg_ctx_sp->GetRegisterInfoByName(name);
    if (reg_info != nullptr)
      valobj = new ValueObjectRegister(*this, m_reg_ctx_sp, reg_info);
  }
  if (valobj)
    return valobj->GetSP();
  else
    return ValueObjectSP();
}

size_t ValueObjectRegisterSet::GetIndexOfChildWithName(llvm::StringRef name) {
  if (m_reg_ctx_sp && m_reg_set) {
    const RegisterInfo *reg_info = m_reg_ctx_sp->GetRegisterInfoByName(name);
    if (reg_info != nullptr)
      return reg_info->kinds[eRegisterKindLLDB];
  }
  return UINT32_MAX;
}

#pragma mark -
#pragma mark ValueObjectRegister

void ValueObjectRegister::ConstructObject(const RegisterInfo *reg_info) {
  if (reg_info) {
    m_reg_info = *reg_info;
    if (reg_info->name)
      m_name.SetCString(reg_info->name);
    else if (reg_info->alt_name)
      m_name.SetCString(reg_info->alt_name);
  }
}

ValueObjectRegister::ValueObjectRegister(ValueObject &parent,
                                         lldb::RegisterContextSP &reg_ctx_sp,
                                         const RegisterInfo *reg_info)
    : ValueObject(parent), m_reg_ctx_sp(reg_ctx_sp), m_reg_info(),
      m_reg_value(), m_type_name(), m_compiler_type() {
  assert(reg_ctx_sp.get());
  ConstructObject(reg_info);
}

ValueObjectSP ValueObjectRegister::Create(ExecutionContextScope *exe_scope,
                                          lldb::RegisterContextSP &reg_ctx_sp,
                                          const RegisterInfo *reg_info) {
  auto manager_sp = ValueObjectManager::Create();
  return (new ValueObjectRegister(exe_scope, *manager_sp, reg_ctx_sp, reg_info))
      ->GetSP();
}

ValueObjectRegister::ValueObjectRegister(ExecutionContextScope *exe_scope,
                                         ValueObjectManager &manager,
                                         lldb::RegisterContextSP &reg_ctx,
                                         const RegisterInfo *reg_info)
    : ValueObject(exe_scope, manager), m_reg_ctx_sp(reg_ctx), m_reg_info(),
      m_reg_value(), m_type_name(), m_compiler_type() {
  assert(reg_ctx);
  ConstructObject(reg_info);
}

ValueObjectRegister::~ValueObjectRegister() = default;

CompilerType ValueObjectRegister::GetCompilerTypeImpl() {
  if (!m_compiler_type.IsValid()) {
    ExecutionContext exe_ctx(GetExecutionContextRef());
    if (auto *target = exe_ctx.GetTargetPtr()) {
      if (auto *exe_module = target->GetExecutableModulePointer()) {
        auto type_system_or_err =
            exe_module->GetTypeSystemForLanguage(eLanguageTypeC);
        if (auto err = type_system_or_err.takeError()) {
          LLDB_LOG_ERROR(GetLog(LLDBLog::Types), std::move(err),
                         "Unable to get CompilerType from TypeSystem: {0}");
        } else {
          if (auto ts = *type_system_or_err)
            m_compiler_type = ts->GetBuiltinTypeForEncodingAndBitSize(
                m_reg_info.encoding, m_reg_info.byte_size * 8);
        }
      }
    }
  }
  return m_compiler_type;
}

ConstString ValueObjectRegister::GetTypeName() {
  if (m_type_name.IsEmpty())
    m_type_name = GetCompilerType().GetTypeName();
  return m_type_name;
}

llvm::Expected<uint32_t>
ValueObjectRegister::CalculateNumChildren(uint32_t max) {
  ExecutionContext exe_ctx(GetExecutionContextRef());
  auto children_count = GetCompilerType().GetNumChildren(true, &exe_ctx);
  if (!children_count)
    return children_count;
  return *children_count <= max ? *children_count : max;
}

std::optional<uint64_t> ValueObjectRegister::GetByteSize() {
  return m_reg_info.byte_size;
}

bool ValueObjectRegister::UpdateValue() {
  m_error.Clear();
  ExecutionContext exe_ctx(GetExecutionContextRef());
  StackFrame *frame = exe_ctx.GetFramePtr();
  if (frame == nullptr) {
    m_reg_ctx_sp.reset();
    m_reg_value.Clear();
  }

  if (m_reg_ctx_sp) {
    RegisterValue m_old_reg_value(m_reg_value);
    if (m_reg_ctx_sp->ReadRegister(&m_reg_info, m_reg_value)) {
      if (m_reg_value.GetData(m_data)) {
        Process *process = exe_ctx.GetProcessPtr();
        if (process)
          m_data.SetAddressByteSize(process->GetAddressByteSize());
        m_value.SetContext(Value::ContextType::RegisterInfo,
                           (void *)&m_reg_info);
        m_value.SetValueType(Value::ValueType::HostAddress);
        m_value.GetScalar() = (uintptr_t)m_data.GetDataStart();
        SetValueIsValid(true);
        SetValueDidChange(!(m_old_reg_value == m_reg_value));
        return true;
      }
    }
  }

  SetValueIsValid(false);
  m_error.SetErrorToGenericError();
  return false;
}

bool ValueObjectRegister::SetValueFromCString(const char *value_str,
                                              Status &error) {
  // The new value will be in the m_data.  Copy that into our register value.
  error =
      m_reg_value.SetValueFromString(&m_reg_info, llvm::StringRef(value_str));
  if (!error.Success())
    return false;

  if (!m_reg_ctx_sp->WriteRegister(&m_reg_info, m_reg_value)) {
    error.SetErrorString("unable to write back to register");
    return false;
  }

  SetNeedsUpdate();
  return true;
}

bool ValueObjectRegister::SetData(DataExtractor &data, Status &error) {
  error = m_reg_value.SetValueFromData(m_reg_info, data, 0, false);
  if (!error.Success())
    return false;

  if (!m_reg_ctx_sp->WriteRegister(&m_reg_info, m_reg_value)) {
    error.SetErrorString("unable to write back to register");
    return false;
  }

  SetNeedsUpdate();
  return true;
}

bool ValueObjectRegister::ResolveValue(Scalar &scalar) {
  if (UpdateValueIfNeeded(
          false)) // make sure that you are up to date before returning anything
    return m_reg_value.GetScalarValue(scalar);
  return false;
}

void ValueObjectRegister::GetExpressionPath(Stream &s,
                                            GetExpressionPathFormat epformat) {
  s.Printf("$%s", m_reg_info.name);
}
