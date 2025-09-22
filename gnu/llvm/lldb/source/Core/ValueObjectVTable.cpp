//===-- ValueObjectVTable.cpp ---------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Core/ValueObjectVTable.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/ValueObjectChild.h"
#include "lldb/Symbol/Function.h"
#include "lldb/Target/Language.h"
#include "lldb/Target/LanguageRuntime.h"
#include "lldb/lldb-defines.h"
#include "lldb/lldb-enumerations.h"
#include "lldb/lldb-forward.h"
#include "lldb/lldb-private-enumerations.h"

using namespace lldb;
using namespace lldb_private;

class ValueObjectVTableChild : public ValueObject {
public:
  ValueObjectVTableChild(ValueObject &parent, uint32_t func_idx,
                         uint64_t addr_size)
      : ValueObject(parent), m_func_idx(func_idx), m_addr_size(addr_size) {
    SetFormat(eFormatPointer);
    SetName(ConstString(llvm::formatv("[{0}]", func_idx).str()));
  }

  ~ValueObjectVTableChild() override = default;

  std::optional<uint64_t> GetByteSize() override { return m_addr_size; };

  llvm::Expected<uint32_t> CalculateNumChildren(uint32_t max) override {
    return 0;
  };

  ValueType GetValueType() const override { return eValueTypeVTableEntry; };

  bool IsInScope() override {
    if (ValueObject *parent = GetParent())
      return parent->IsInScope();
    return false;
  };

protected:
  bool UpdateValue() override {
    SetValueIsValid(false);
    m_value.Clear();
    ValueObject *parent = GetParent();
    if (!parent) {
      m_error.SetErrorString("owning vtable object not valid");
      return false;
    }

    addr_t parent_addr = parent->GetValueAsUnsigned(LLDB_INVALID_ADDRESS);
    if (parent_addr == LLDB_INVALID_ADDRESS) {
      m_error.SetErrorString("invalid vtable address");
      return false;
    }

    ProcessSP process_sp = GetProcessSP();
    if (!process_sp) {
      m_error.SetErrorString("no process");
      return false;
    }

    TargetSP target_sp = GetTargetSP();
    if (!target_sp) {
      m_error.SetErrorString("no target");
      return false;
    }

    // Each `vtable_entry_addr` points to the function pointer.
    addr_t vtable_entry_addr = parent_addr + m_func_idx * m_addr_size;
    addr_t vfunc_ptr =
        process_sp->ReadPointerFromMemory(vtable_entry_addr, m_error);
    if (m_error.Fail()) {
      m_error.SetErrorStringWithFormat(
          "failed to read virtual function entry 0x%16.16" PRIx64,
          vtable_entry_addr);
      return false;
    }


    // Set our value to be the load address of the function pointer in memory
    // and our type to be the function pointer type.
    m_value.SetValueType(Value::ValueType::LoadAddress);
    m_value.GetScalar() = vtable_entry_addr;

    // See if our resolved address points to a function in the debug info. If
    // it does, then we can report the type as a function prototype for this
    // function.
    Function *function = nullptr;
    Address resolved_vfunc_ptr_address;
    target_sp->ResolveLoadAddress(vfunc_ptr, resolved_vfunc_ptr_address);
    if (resolved_vfunc_ptr_address.IsValid())
      function = resolved_vfunc_ptr_address.CalculateSymbolContextFunction();
    if (function) {
      m_value.SetCompilerType(function->GetCompilerType().GetPointerType());
    } else {
      // Set our value's compiler type to a generic function protoype so that
      // it displays as a hex function pointer for the value and the summary
      // will display the address description.

      // Get the original type that this vtable is based off of so we can get
      // the language from it correctly.
      ValueObject *val = parent->GetParent();
      auto type_system = target_sp->GetScratchTypeSystemForLanguage(
            val ? val->GetObjectRuntimeLanguage() : eLanguageTypeC_plus_plus);
      if (type_system) {
        m_value.SetCompilerType(
            (*type_system)->CreateGenericFunctionPrototype().GetPointerType());
      } else {
        consumeError(type_system.takeError());
      }
    }

    // Now read our value into m_data so that our we can use the default
    // summary provider for C++ for function pointers which will get the
    // address description for our function pointer.
    if (m_error.Success()) {
      const bool thread_and_frame_only_if_stopped = true;
      ExecutionContext exe_ctx(
        GetExecutionContextRef().Lock(thread_and_frame_only_if_stopped));
      m_error = m_value.GetValueAsData(&exe_ctx, m_data, GetModule().get());
    }
    SetValueDidChange(true);
    SetValueIsValid(true);
    return true;
  };

  CompilerType GetCompilerTypeImpl() override {
    return m_value.GetCompilerType();
  };

  const uint32_t m_func_idx;
  const uint64_t m_addr_size;

private:
  // For ValueObject only
  ValueObjectVTableChild(const ValueObjectVTableChild &) = delete;
  const ValueObjectVTableChild &
  operator=(const ValueObjectVTableChild &) = delete;
};

ValueObjectSP ValueObjectVTable::Create(ValueObject &parent) {
  return (new ValueObjectVTable(parent))->GetSP();
}

ValueObjectVTable::ValueObjectVTable(ValueObject &parent)
    : ValueObject(parent) {
  SetFormat(eFormatPointer);
}

std::optional<uint64_t> ValueObjectVTable::GetByteSize() {
  if (m_vtable_symbol)
    return m_vtable_symbol->GetByteSize();
  return std::nullopt;
}

llvm::Expected<uint32_t> ValueObjectVTable::CalculateNumChildren(uint32_t max) {
  if (UpdateValueIfNeeded(false))
    return m_num_vtable_entries <= max ? m_num_vtable_entries : max;
  return 0;
}

ValueType ValueObjectVTable::GetValueType() const { return eValueTypeVTable; }

ConstString ValueObjectVTable::GetTypeName() {
  if (m_vtable_symbol)
    return m_vtable_symbol->GetName();
  return ConstString();
}

ConstString ValueObjectVTable::GetQualifiedTypeName() { return GetTypeName(); }

ConstString ValueObjectVTable::GetDisplayTypeName() {
  if (m_vtable_symbol)
    return m_vtable_symbol->GetDisplayName();
  return ConstString();
}

bool ValueObjectVTable::IsInScope() { return GetParent()->IsInScope(); }

ValueObject *ValueObjectVTable::CreateChildAtIndex(size_t idx) {
  return new ValueObjectVTableChild(*this, idx, m_addr_size);
}

bool ValueObjectVTable::UpdateValue() {
  m_error.Clear();
  m_flags.m_children_count_valid = false;
  SetValueIsValid(false);
  m_num_vtable_entries = 0;
  ValueObject *parent = GetParent();
  if (!parent) {
    m_error.SetErrorString("no parent object");
    return false;
  }

  ProcessSP process_sp = GetProcessSP();
  if (!process_sp) {
    m_error.SetErrorString("no process");
    return false;
  }

  const LanguageType language = parent->GetObjectRuntimeLanguage();
  LanguageRuntime *language_runtime = process_sp->GetLanguageRuntime(language);

  if (language_runtime == nullptr) {
    m_error.SetErrorStringWithFormat(
        "no language runtime support for the language \"%s\"",
        Language::GetNameForLanguageType(language));
    return false;
  }

  // Get the vtable information from the language runtime.
  llvm::Expected<LanguageRuntime::VTableInfo> vtable_info_or_err =
      language_runtime->GetVTableInfo(*parent, /*check_type=*/true);
  if (!vtable_info_or_err) {
    m_error = vtable_info_or_err.takeError();
    return false;
  }

  TargetSP target_sp = GetTargetSP();
  const addr_t vtable_start_addr =
      vtable_info_or_err->addr.GetLoadAddress(target_sp.get());

  m_vtable_symbol = vtable_info_or_err->symbol;
  if (!m_vtable_symbol) {
    m_error.SetErrorStringWithFormat(
        "no vtable symbol found containing 0x%" PRIx64, vtable_start_addr);
    return false;
  }

  // Now that we know it's a vtable, we update the object's state.
  SetName(GetTypeName());

  // Calculate the number of entries
  if (!m_vtable_symbol->GetByteSizeIsValid()) {
    m_error.SetErrorStringWithFormat(
        "vtable symbol \"%s\" doesn't have a valid size",
        m_vtable_symbol->GetMangled().GetDemangledName().GetCString());
    return false;
  }

  m_addr_size = process_sp->GetAddressByteSize();
  const addr_t vtable_end_addr =
      m_vtable_symbol->GetLoadAddress(target_sp.get()) +
      m_vtable_symbol->GetByteSize();
  m_num_vtable_entries = (vtable_end_addr - vtable_start_addr) / m_addr_size;

  m_value.SetValueType(Value::ValueType::LoadAddress);
  m_value.GetScalar() = parent->GetAddressOf();
  auto type_system_or_err =
        target_sp->GetScratchTypeSystemForLanguage(eLanguageTypeC_plus_plus);
  if (type_system_or_err) {
    m_value.SetCompilerType(
        (*type_system_or_err)->GetBasicTypeFromAST(eBasicTypeUnsignedLong));
  } else {
    consumeError(type_system_or_err.takeError());
  }
  SetValueDidChange(true);
  SetValueIsValid(true);
  return true;
}

CompilerType ValueObjectVTable::GetCompilerTypeImpl() { return CompilerType(); }

ValueObjectVTable::~ValueObjectVTable() = default;
