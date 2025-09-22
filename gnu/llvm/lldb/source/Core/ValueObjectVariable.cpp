//===-- ValueObjectVariable.cpp -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Core/ValueObjectVariable.h"

#include "lldb/Core/Address.h"
#include "lldb/Core/AddressRange.h"
#include "lldb/Core/Declaration.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/Value.h"
#include "lldb/Expression/DWARFExpressionList.h"
#include "lldb/Symbol/Function.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Symbol/SymbolContext.h"
#include "lldb/Symbol/SymbolContextScope.h"
#include "lldb/Symbol/Type.h"
#include "lldb/Symbol/Variable.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/RegisterValue.h"
#include "lldb/Utility/Scalar.h"
#include "lldb/Utility/Status.h"
#include "lldb/lldb-private-enumerations.h"
#include "lldb/lldb-types.h"

#include "llvm/ADT/StringRef.h"

#include <cassert>
#include <memory>
#include <optional>

namespace lldb_private {
class ExecutionContextScope;
}
namespace lldb_private {
class StackFrame;
}
namespace lldb_private {
struct RegisterInfo;
}
using namespace lldb_private;

lldb::ValueObjectSP
ValueObjectVariable::Create(ExecutionContextScope *exe_scope,
                            const lldb::VariableSP &var_sp) {
  auto manager_sp = ValueObjectManager::Create();
  return (new ValueObjectVariable(exe_scope, *manager_sp, var_sp))->GetSP();
}

ValueObjectVariable::ValueObjectVariable(ExecutionContextScope *exe_scope,
                                         ValueObjectManager &manager,
                                         const lldb::VariableSP &var_sp)
    : ValueObject(exe_scope, manager), m_variable_sp(var_sp) {
  // Do not attempt to construct one of these objects with no variable!
  assert(m_variable_sp.get() != nullptr);
  m_name = var_sp->GetName();
}

ValueObjectVariable::~ValueObjectVariable() = default;

CompilerType ValueObjectVariable::GetCompilerTypeImpl() {
  Type *var_type = m_variable_sp->GetType();
  if (var_type)
    return var_type->GetForwardCompilerType();
  return CompilerType();
}

ConstString ValueObjectVariable::GetTypeName() {
  Type *var_type = m_variable_sp->GetType();
  if (var_type)
    return var_type->GetName();
  return ConstString();
}

ConstString ValueObjectVariable::GetDisplayTypeName() {
  Type *var_type = m_variable_sp->GetType();
  if (var_type)
    return var_type->GetForwardCompilerType().GetDisplayTypeName();
  return ConstString();
}

ConstString ValueObjectVariable::GetQualifiedTypeName() {
  Type *var_type = m_variable_sp->GetType();
  if (var_type)
    return var_type->GetQualifiedName();
  return ConstString();
}

llvm::Expected<uint32_t>
ValueObjectVariable::CalculateNumChildren(uint32_t max) {
  CompilerType type(GetCompilerType());

  if (!type.IsValid())
    return llvm::make_error<llvm::StringError>("invalid type",
                                               llvm::inconvertibleErrorCode());

  ExecutionContext exe_ctx(GetExecutionContextRef());
  const bool omit_empty_base_classes = true;
  auto child_count = type.GetNumChildren(omit_empty_base_classes, &exe_ctx);
  if (!child_count)
    return child_count;
  return *child_count <= max ? *child_count : max;
}

std::optional<uint64_t> ValueObjectVariable::GetByteSize() {
  ExecutionContext exe_ctx(GetExecutionContextRef());

  CompilerType type(GetCompilerType());

  if (!type.IsValid())
    return {};

  return type.GetByteSize(exe_ctx.GetBestExecutionContextScope());
}

lldb::ValueType ValueObjectVariable::GetValueType() const {
  if (m_variable_sp)
    return m_variable_sp->GetScope();
  return lldb::eValueTypeInvalid;
}

bool ValueObjectVariable::UpdateValue() {
  SetValueIsValid(false);
  m_error.Clear();

  Variable *variable = m_variable_sp.get();
  DWARFExpressionList &expr_list = variable->LocationExpressionList();

  if (variable->GetLocationIsConstantValueData()) {
    // expr doesn't contain DWARF bytes, it contains the constant variable
    // value bytes themselves...
    if (expr_list.GetExpressionData(m_data)) {
      if (m_data.GetDataStart() && m_data.GetByteSize())
        m_value.SetBytes(m_data.GetDataStart(), m_data.GetByteSize());
      m_value.SetContext(Value::ContextType::Variable, variable);
    } else
      m_error.SetErrorString("empty constant data");
    // constant bytes can't be edited - sorry
    m_resolved_value.SetContext(Value::ContextType::Invalid, nullptr);
  } else {
    lldb::addr_t loclist_base_load_addr = LLDB_INVALID_ADDRESS;
    ExecutionContext exe_ctx(GetExecutionContextRef());

    Target *target = exe_ctx.GetTargetPtr();
    if (target) {
      m_data.SetByteOrder(target->GetArchitecture().GetByteOrder());
      m_data.SetAddressByteSize(target->GetArchitecture().GetAddressByteSize());
    }

    if (!expr_list.IsAlwaysValidSingleExpr()) {
      SymbolContext sc;
      variable->CalculateSymbolContext(&sc);
      if (sc.function)
        loclist_base_load_addr =
            sc.function->GetAddressRange().GetBaseAddress().GetLoadAddress(
                target);
    }
    Value old_value(m_value);
    llvm::Expected<Value> maybe_value = expr_list.Evaluate(
        &exe_ctx, nullptr, loclist_base_load_addr, nullptr, nullptr);

    if (maybe_value) {
      m_value = *maybe_value;
      m_resolved_value = m_value;
      m_value.SetContext(Value::ContextType::Variable, variable);

      CompilerType compiler_type = GetCompilerType();
      if (compiler_type.IsValid())
        m_value.SetCompilerType(compiler_type);

      Value::ValueType value_type = m_value.GetValueType();

      // The size of the buffer within m_value can be less than the size
      // prescribed by its type. E.g. this can happen when an expression only
      // partially describes an object (say, because it contains DW_OP_piece).
      //
      // In this case, grow m_value to the expected size. An alternative way to
      // handle this is to teach Value::GetValueAsData() and ValueObjectChild
      // not to read past the end of a host buffer, but this gets impractically
      // complicated as a Value's host buffer may be shared with a distant
      // ancestor or sibling in the ValueObject hierarchy.
      //
      // FIXME: When we grow m_value, we should represent the added bits as
      // undefined somehow instead of as 0's.
      if (value_type == Value::ValueType::HostAddress &&
          compiler_type.IsValid()) {
        if (size_t value_buf_size = m_value.GetBuffer().GetByteSize()) {
          size_t value_size = m_value.GetValueByteSize(&m_error, &exe_ctx);
          if (m_error.Success() && value_buf_size < value_size)
            m_value.ResizeData(value_size);
        }
      }

      Process *process = exe_ctx.GetProcessPtr();
      const bool process_is_alive = process && process->IsAlive();

      switch (value_type) {
      case Value::ValueType::Invalid:
        m_error.SetErrorString("invalid value");
        break;
      case Value::ValueType::Scalar:
        // The variable value is in the Scalar value inside the m_value. We can
        // point our m_data right to it.
        m_error =
            m_value.GetValueAsData(&exe_ctx, m_data, GetModule().get());
        break;

      case Value::ValueType::FileAddress:
      case Value::ValueType::LoadAddress:
      case Value::ValueType::HostAddress:
        // The DWARF expression result was an address in the inferior process.
        // If this variable is an aggregate type, we just need the address as
        // the main value as all child variable objects will rely upon this
        // location and add an offset and then read their own values as needed.
        // If this variable is a simple type, we read all data for it into
        // m_data. Make sure this type has a value before we try and read it

        // If we have a file address, convert it to a load address if we can.
        if (value_type == Value::ValueType::FileAddress && process_is_alive)
          m_value.ConvertToLoadAddress(GetModule().get(), target);

        if (!CanProvideValue()) {
          // this value object represents an aggregate type whose children have
          // values, but this object does not. So we say we are changed if our
          // location has changed.
          SetValueDidChange(value_type != old_value.GetValueType() ||
                            m_value.GetScalar() != old_value.GetScalar());
        } else {
          // Copy the Value and set the context to use our Variable so it can
          // extract read its value into m_data appropriately
          Value value(m_value);
          value.SetContext(Value::ContextType::Variable, variable);
          m_error =
              value.GetValueAsData(&exe_ctx, m_data, GetModule().get());

          SetValueDidChange(value_type != old_value.GetValueType() ||
                            m_value.GetScalar() != old_value.GetScalar());
        }
        break;
      }

      SetValueIsValid(m_error.Success());
    } else {
      m_error = maybe_value.takeError();
      // could not find location, won't allow editing
      m_resolved_value.SetContext(Value::ContextType::Invalid, nullptr);
    }
  }

  return m_error.Success();
}

void ValueObjectVariable::DoUpdateChildrenAddressType(ValueObject &valobj) {
  Value::ValueType value_type = valobj.GetValue().GetValueType();
  ExecutionContext exe_ctx(GetExecutionContextRef());
  Process *process = exe_ctx.GetProcessPtr();
  const bool process_is_alive = process && process->IsAlive();
  const uint32_t type_info = valobj.GetCompilerType().GetTypeInfo();
  const bool is_pointer_or_ref =
      (type_info & (lldb::eTypeIsPointer | lldb::eTypeIsReference)) != 0;

  switch (value_type) {
  case Value::ValueType::Invalid:
    break;
  case Value::ValueType::FileAddress:
    // If this type is a pointer, then its children will be considered load
    // addresses if the pointer or reference is dereferenced, but only if
    // the process is alive.
    //
    // There could be global variables like in the following code:
    // struct LinkedListNode { Foo* foo; LinkedListNode* next; };
    // Foo g_foo1;
    // Foo g_foo2;
    // LinkedListNode g_second_node = { &g_foo2, NULL };
    // LinkedListNode g_first_node = { &g_foo1, &g_second_node };
    //
    // When we aren't running, we should be able to look at these variables
    // using the "target variable" command. Children of the "g_first_node"
    // always will be of the same address type as the parent. But children
    // of the "next" member of LinkedListNode will become load addresses if
    // we have a live process, or remain a file address if it was a file
    // address.
    if (process_is_alive && is_pointer_or_ref)
      valobj.SetAddressTypeOfChildren(eAddressTypeLoad);
    else
      valobj.SetAddressTypeOfChildren(eAddressTypeFile);
    break;
  case Value::ValueType::HostAddress:
    // Same as above for load addresses, except children of pointer or refs
    // are always load addresses. Host addresses are used to store freeze
    // dried variables. If this type is a struct, the entire struct
    // contents will be copied into the heap of the
    // LLDB process, but we do not currently follow any pointers.
    if (is_pointer_or_ref)
      valobj.SetAddressTypeOfChildren(eAddressTypeLoad);
    else
      valobj.SetAddressTypeOfChildren(eAddressTypeHost);
    break;
  case Value::ValueType::LoadAddress:
  case Value::ValueType::Scalar:
    valobj.SetAddressTypeOfChildren(eAddressTypeLoad);
    break;
  }
}



bool ValueObjectVariable::IsInScope() {
  const ExecutionContextRef &exe_ctx_ref = GetExecutionContextRef();
  if (exe_ctx_ref.HasFrameRef()) {
    ExecutionContext exe_ctx(exe_ctx_ref);
    StackFrame *frame = exe_ctx.GetFramePtr();
    if (frame) {
      return m_variable_sp->IsInScope(frame);
    } else {
      // This ValueObject had a frame at one time, but now we can't locate it,
      // so return false since we probably aren't in scope.
      return false;
    }
  }
  // We have a variable that wasn't tied to a frame, which means it is a global
  // and is always in scope.
  return true;
}

lldb::ModuleSP ValueObjectVariable::GetModule() {
  if (m_variable_sp) {
    SymbolContextScope *sc_scope = m_variable_sp->GetSymbolContextScope();
    if (sc_scope) {
      return sc_scope->CalculateSymbolContextModule();
    }
  }
  return lldb::ModuleSP();
}

SymbolContextScope *ValueObjectVariable::GetSymbolContextScope() {
  if (m_variable_sp)
    return m_variable_sp->GetSymbolContextScope();
  return nullptr;
}

bool ValueObjectVariable::GetDeclaration(Declaration &decl) {
  if (m_variable_sp) {
    decl = m_variable_sp->GetDeclaration();
    return true;
  }
  return false;
}

const char *ValueObjectVariable::GetLocationAsCString() {
  if (m_resolved_value.GetContextType() == Value::ContextType::RegisterInfo)
    return GetLocationAsCStringImpl(m_resolved_value, m_data);
  else
    return ValueObject::GetLocationAsCString();
}

bool ValueObjectVariable::SetValueFromCString(const char *value_str,
                                              Status &error) {
  if (!UpdateValueIfNeeded()) {
    error.SetErrorString("unable to update value before writing");
    return false;
  }

  if (m_resolved_value.GetContextType() == Value::ContextType::RegisterInfo) {
    RegisterInfo *reg_info = m_resolved_value.GetRegisterInfo();
    ExecutionContext exe_ctx(GetExecutionContextRef());
    RegisterContext *reg_ctx = exe_ctx.GetRegisterContext();
    RegisterValue reg_value;
    if (!reg_info || !reg_ctx) {
      error.SetErrorString("unable to retrieve register info");
      return false;
    }
    error = reg_value.SetValueFromString(reg_info, llvm::StringRef(value_str));
    if (error.Fail())
      return false;
    if (reg_ctx->WriteRegister(reg_info, reg_value)) {
      SetNeedsUpdate();
      return true;
    } else {
      error.SetErrorString("unable to write back to register");
      return false;
    }
  } else
    return ValueObject::SetValueFromCString(value_str, error);
}

bool ValueObjectVariable::SetData(DataExtractor &data, Status &error) {
  if (!UpdateValueIfNeeded()) {
    error.SetErrorString("unable to update value before writing");
    return false;
  }

  if (m_resolved_value.GetContextType() == Value::ContextType::RegisterInfo) {
    RegisterInfo *reg_info = m_resolved_value.GetRegisterInfo();
    ExecutionContext exe_ctx(GetExecutionContextRef());
    RegisterContext *reg_ctx = exe_ctx.GetRegisterContext();
    RegisterValue reg_value;
    if (!reg_info || !reg_ctx) {
      error.SetErrorString("unable to retrieve register info");
      return false;
    }
    error = reg_value.SetValueFromData(*reg_info, data, 0, true);
    if (error.Fail())
      return false;
    if (reg_ctx->WriteRegister(reg_info, reg_value)) {
      SetNeedsUpdate();
      return true;
    } else {
      error.SetErrorString("unable to write back to register");
      return false;
    }
  } else
    return ValueObject::SetData(data, error);
}
