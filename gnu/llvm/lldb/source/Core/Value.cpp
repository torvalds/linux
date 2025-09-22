//===-- Value.cpp ---------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Core/Value.h"

#include "lldb/Core/Address.h"
#include "lldb/Core/Module.h"
#include "lldb/Symbol/CompilerType.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Symbol/SymbolContext.h"
#include "lldb/Symbol/Type.h"
#include "lldb/Symbol/Variable.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/SectionLoadList.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/Endian.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/State.h"
#include "lldb/Utility/Stream.h"
#include "lldb/lldb-defines.h"
#include "lldb/lldb-forward.h"
#include "lldb/lldb-types.h"

#include <memory>
#include <optional>
#include <string>

#include <cinttypes>

using namespace lldb;
using namespace lldb_private;

Value::Value() : m_value(), m_compiler_type(), m_data_buffer() {}

Value::Value(const Scalar &scalar)
    : m_value(scalar), m_compiler_type(), m_data_buffer() {}

Value::Value(const void *bytes, int len)
    : m_value(), m_compiler_type(), m_value_type(ValueType::HostAddress),
      m_data_buffer() {
  SetBytes(bytes, len);
}

Value::Value(const Value &v)
    : m_value(v.m_value), m_compiler_type(v.m_compiler_type),
      m_context(v.m_context), m_value_type(v.m_value_type),
      m_context_type(v.m_context_type), m_data_buffer() {
  const uintptr_t rhs_value =
      (uintptr_t)v.m_value.ULongLong(LLDB_INVALID_ADDRESS);
  if ((rhs_value != 0) &&
      (rhs_value == (uintptr_t)v.m_data_buffer.GetBytes())) {
    m_data_buffer.CopyData(v.m_data_buffer.GetBytes(),
                           v.m_data_buffer.GetByteSize());

    m_value = (uintptr_t)m_data_buffer.GetBytes();
  }
}

Value &Value::operator=(const Value &rhs) {
  if (this != &rhs) {
    m_value = rhs.m_value;
    m_compiler_type = rhs.m_compiler_type;
    m_context = rhs.m_context;
    m_value_type = rhs.m_value_type;
    m_context_type = rhs.m_context_type;
    const uintptr_t rhs_value =
        (uintptr_t)rhs.m_value.ULongLong(LLDB_INVALID_ADDRESS);
    if ((rhs_value != 0) &&
        (rhs_value == (uintptr_t)rhs.m_data_buffer.GetBytes())) {
      m_data_buffer.CopyData(rhs.m_data_buffer.GetBytes(),
                             rhs.m_data_buffer.GetByteSize());

      m_value = (uintptr_t)m_data_buffer.GetBytes();
    }
  }
  return *this;
}

void Value::SetBytes(const void *bytes, int len) {
  m_value_type = ValueType::HostAddress;
  m_data_buffer.CopyData(bytes, len);
  m_value = (uintptr_t)m_data_buffer.GetBytes();
}

void Value::AppendBytes(const void *bytes, int len) {
  m_value_type = ValueType::HostAddress;
  m_data_buffer.AppendData(bytes, len);
  m_value = (uintptr_t)m_data_buffer.GetBytes();
}

void Value::Dump(Stream *strm) {
  if (!strm)
    return;
  m_value.GetValue(*strm, true);
  strm->Printf(", value_type = %s, context = %p, context_type = %s",
               Value::GetValueTypeAsCString(m_value_type), m_context,
               Value::GetContextTypeAsCString(m_context_type));
}

Value::ValueType Value::GetValueType() const { return m_value_type; }

AddressType Value::GetValueAddressType() const {
  switch (m_value_type) {
  case ValueType::Invalid:
  case ValueType::Scalar:
    break;
  case ValueType::LoadAddress:
    return eAddressTypeLoad;
  case ValueType::FileAddress:
    return eAddressTypeFile;
  case ValueType::HostAddress:
    return eAddressTypeHost;
  }
  return eAddressTypeInvalid;
}

Value::ValueType Value::GetValueTypeFromAddressType(AddressType address_type) {
  switch (address_type) {
    case eAddressTypeFile:
      return Value::ValueType::FileAddress;
    case eAddressTypeLoad:
      return Value::ValueType::LoadAddress;
    case eAddressTypeHost:
      return Value::ValueType::HostAddress;
    case eAddressTypeInvalid:
      return Value::ValueType::Invalid;
  }
  llvm_unreachable("Unexpected address type!");
}

RegisterInfo *Value::GetRegisterInfo() const {
  if (m_context_type == ContextType::RegisterInfo)
    return static_cast<RegisterInfo *>(m_context);
  return nullptr;
}

Type *Value::GetType() {
  if (m_context_type == ContextType::LLDBType)
    return static_cast<Type *>(m_context);
  return nullptr;
}

size_t Value::AppendDataToHostBuffer(const Value &rhs) {
  if (this == &rhs)
    return 0;

  size_t curr_size = m_data_buffer.GetByteSize();
  Status error;
  switch (rhs.GetValueType()) {
  case ValueType::Invalid:
    return 0;
  case ValueType::Scalar: {
    const size_t scalar_size = rhs.m_value.GetByteSize();
    if (scalar_size > 0) {
      const size_t new_size = curr_size + scalar_size;
      if (ResizeData(new_size) == new_size) {
        rhs.m_value.GetAsMemoryData(m_data_buffer.GetBytes() + curr_size,
                                    scalar_size, endian::InlHostByteOrder(),
                                    error);
        return scalar_size;
      }
    }
  } break;
  case ValueType::FileAddress:
  case ValueType::LoadAddress:
  case ValueType::HostAddress: {
    const uint8_t *src = rhs.GetBuffer().GetBytes();
    const size_t src_len = rhs.GetBuffer().GetByteSize();
    if (src && src_len > 0) {
      const size_t new_size = curr_size + src_len;
      if (ResizeData(new_size) == new_size) {
        ::memcpy(m_data_buffer.GetBytes() + curr_size, src, src_len);
        return src_len;
      }
    }
  } break;
  }
  return 0;
}

size_t Value::ResizeData(size_t len) {
  m_value_type = ValueType::HostAddress;
  m_data_buffer.SetByteSize(len);
  m_value = (uintptr_t)m_data_buffer.GetBytes();
  return m_data_buffer.GetByteSize();
}

bool Value::ValueOf(ExecutionContext *exe_ctx) {
  switch (m_context_type) {
  case ContextType::Invalid:
  case ContextType::RegisterInfo: // RegisterInfo *
  case ContextType::LLDBType:     // Type *
    break;

  case ContextType::Variable: // Variable *
    ResolveValue(exe_ctx);
    return true;
  }
  return false;
}

uint64_t Value::GetValueByteSize(Status *error_ptr, ExecutionContext *exe_ctx) {
  switch (m_context_type) {
  case ContextType::RegisterInfo: // RegisterInfo *
    if (GetRegisterInfo()) {
      if (error_ptr)
        error_ptr->Clear();
      return GetRegisterInfo()->byte_size;
    }
    break;

  case ContextType::Invalid:
  case ContextType::LLDBType: // Type *
  case ContextType::Variable: // Variable *
  {
    auto *scope = exe_ctx ? exe_ctx->GetBestExecutionContextScope() : nullptr;
    if (std::optional<uint64_t> size = GetCompilerType().GetByteSize(scope)) {
      if (error_ptr)
        error_ptr->Clear();
      return *size;
    }
    break;
  }
  }
  if (error_ptr && error_ptr->Success())
    error_ptr->SetErrorString("Unable to determine byte size.");
  return 0;
}

const CompilerType &Value::GetCompilerType() {
  if (!m_compiler_type.IsValid()) {
    switch (m_context_type) {
    case ContextType::Invalid:
      break;

    case ContextType::RegisterInfo:
      break; // TODO: Eventually convert into a compiler type?

    case ContextType::LLDBType: {
      Type *lldb_type = GetType();
      if (lldb_type)
        m_compiler_type = lldb_type->GetForwardCompilerType();
    } break;

    case ContextType::Variable: {
      Variable *variable = GetVariable();
      if (variable) {
        Type *variable_type = variable->GetType();
        if (variable_type)
          m_compiler_type = variable_type->GetForwardCompilerType();
      }
    } break;
    }
  }

  return m_compiler_type;
}

void Value::SetCompilerType(const CompilerType &compiler_type) {
  m_compiler_type = compiler_type;
}

lldb::Format Value::GetValueDefaultFormat() {
  switch (m_context_type) {
  case ContextType::RegisterInfo:
    if (GetRegisterInfo())
      return GetRegisterInfo()->format;
    break;

  case ContextType::Invalid:
  case ContextType::LLDBType:
  case ContextType::Variable: {
    const CompilerType &ast_type = GetCompilerType();
    if (ast_type.IsValid())
      return ast_type.GetFormat();
  } break;
  }

  // Return a good default in case we can't figure anything out
  return eFormatHex;
}

bool Value::GetData(DataExtractor &data) {
  switch (m_value_type) {
  case ValueType::Invalid:
    return false;
  case ValueType::Scalar:
    if (m_value.GetData(data))
      return true;
    break;

  case ValueType::LoadAddress:
  case ValueType::FileAddress:
  case ValueType::HostAddress:
    if (m_data_buffer.GetByteSize()) {
      data.SetData(m_data_buffer.GetBytes(), m_data_buffer.GetByteSize(),
                   data.GetByteOrder());
      return true;
    }
    break;
  }

  return false;
}

Status Value::GetValueAsData(ExecutionContext *exe_ctx, DataExtractor &data,
                             Module *module) {
  data.Clear();

  Status error;
  lldb::addr_t address = LLDB_INVALID_ADDRESS;
  AddressType address_type = eAddressTypeFile;
  Address file_so_addr;
  const CompilerType &ast_type = GetCompilerType();
  std::optional<uint64_t> type_size = ast_type.GetByteSize(
      exe_ctx ? exe_ctx->GetBestExecutionContextScope() : nullptr);
  // Nothing to be done for a zero-sized type.
  if (type_size && *type_size == 0)
    return error;

  switch (m_value_type) {
  case ValueType::Invalid:
    error.SetErrorString("invalid value");
    break;
  case ValueType::Scalar: {
    data.SetByteOrder(endian::InlHostByteOrder());
    if (ast_type.IsValid())
      data.SetAddressByteSize(ast_type.GetPointerByteSize());
    else
      data.SetAddressByteSize(sizeof(void *));

    uint32_t limit_byte_size = UINT32_MAX;

    if (type_size)
      limit_byte_size = *type_size;

    if (limit_byte_size <= m_value.GetByteSize()) {
      if (m_value.GetData(data, limit_byte_size))
        return error; // Success;
    }

    error.SetErrorString("extracting data from value failed");
    break;
  }
  case ValueType::LoadAddress:
    if (exe_ctx == nullptr) {
      error.SetErrorString("can't read load address (no execution context)");
    } else {
      Process *process = exe_ctx->GetProcessPtr();
      if (process == nullptr || !process->IsAlive()) {
        Target *target = exe_ctx->GetTargetPtr();
        if (target) {
          // Allow expressions to run and evaluate things when the target has
          // memory sections loaded. This allows you to use "target modules
          // load" to load your executable and any shared libraries, then
          // execute commands where you can look at types in data sections.
          const SectionLoadList &target_sections = target->GetSectionLoadList();
          if (!target_sections.IsEmpty()) {
            address = m_value.ULongLong(LLDB_INVALID_ADDRESS);
            if (target_sections.ResolveLoadAddress(address, file_so_addr)) {
              address_type = eAddressTypeLoad;
              data.SetByteOrder(target->GetArchitecture().GetByteOrder());
              data.SetAddressByteSize(
                  target->GetArchitecture().GetAddressByteSize());
            } else
              address = LLDB_INVALID_ADDRESS;
          }
        } else {
          error.SetErrorString("can't read load address (invalid process)");
        }
      } else {
        address = m_value.ULongLong(LLDB_INVALID_ADDRESS);
        address_type = eAddressTypeLoad;
        data.SetByteOrder(
            process->GetTarget().GetArchitecture().GetByteOrder());
        data.SetAddressByteSize(
            process->GetTarget().GetArchitecture().GetAddressByteSize());
      }
    }
    break;

  case ValueType::FileAddress:
    if (exe_ctx == nullptr) {
      error.SetErrorString("can't read file address (no execution context)");
    } else if (exe_ctx->GetTargetPtr() == nullptr) {
      error.SetErrorString("can't read file address (invalid target)");
    } else {
      address = m_value.ULongLong(LLDB_INVALID_ADDRESS);
      if (address == LLDB_INVALID_ADDRESS) {
        error.SetErrorString("invalid file address");
      } else {
        if (module == nullptr) {
          // The only thing we can currently lock down to a module so that we
          // can resolve a file address, is a variable.
          Variable *variable = GetVariable();
          if (variable) {
            SymbolContext var_sc;
            variable->CalculateSymbolContext(&var_sc);
            module = var_sc.module_sp.get();
          }
        }

        if (module) {
          bool resolved = false;
          ObjectFile *objfile = module->GetObjectFile();
          if (objfile) {
            Address so_addr(address, objfile->GetSectionList());
            addr_t load_address =
                so_addr.GetLoadAddress(exe_ctx->GetTargetPtr());
            bool process_launched_and_stopped =
                exe_ctx->GetProcessPtr()
                    ? StateIsStoppedState(exe_ctx->GetProcessPtr()->GetState(),
                                          true /* must_exist */)
                    : false;
            // Don't use the load address if the process has exited.
            if (load_address != LLDB_INVALID_ADDRESS &&
                process_launched_and_stopped) {
              resolved = true;
              address = load_address;
              address_type = eAddressTypeLoad;
              data.SetByteOrder(
                  exe_ctx->GetTargetRef().GetArchitecture().GetByteOrder());
              data.SetAddressByteSize(exe_ctx->GetTargetRef()
                                          .GetArchitecture()
                                          .GetAddressByteSize());
            } else {
              if (so_addr.IsSectionOffset()) {
                resolved = true;
                file_so_addr = so_addr;
                data.SetByteOrder(objfile->GetByteOrder());
                data.SetAddressByteSize(objfile->GetAddressByteSize());
              }
            }
          }
          if (!resolved) {
            Variable *variable = GetVariable();

            if (module) {
              if (variable)
                error.SetErrorStringWithFormat(
                    "unable to resolve the module for file address 0x%" PRIx64
                    " for variable '%s' in %s",
                    address, variable->GetName().AsCString(""),
                    module->GetFileSpec().GetPath().c_str());
              else
                error.SetErrorStringWithFormat(
                    "unable to resolve the module for file address 0x%" PRIx64
                    " in %s",
                    address, module->GetFileSpec().GetPath().c_str());
            } else {
              if (variable)
                error.SetErrorStringWithFormat(
                    "unable to resolve the module for file address 0x%" PRIx64
                    " for variable '%s'",
                    address, variable->GetName().AsCString(""));
              else
                error.SetErrorStringWithFormat(
                    "unable to resolve the module for file address 0x%" PRIx64,
                    address);
            }
          }
        } else {
          // Can't convert a file address to anything valid without more
          // context (which Module it came from)
          error.SetErrorString(
              "can't read memory from file address without more context");
        }
      }
    }
    break;

  case ValueType::HostAddress:
    address = m_value.ULongLong(LLDB_INVALID_ADDRESS);
    address_type = eAddressTypeHost;
    if (exe_ctx) {
      Target *target = exe_ctx->GetTargetPtr();
      if (target) {
        data.SetByteOrder(target->GetArchitecture().GetByteOrder());
        data.SetAddressByteSize(target->GetArchitecture().GetAddressByteSize());
        break;
      }
    }
    // fallback to host settings
    data.SetByteOrder(endian::InlHostByteOrder());
    data.SetAddressByteSize(sizeof(void *));
    break;
  }

  // Bail if we encountered any errors
  if (error.Fail())
    return error;

  if (address == LLDB_INVALID_ADDRESS) {
    error.SetErrorStringWithFormat("invalid %s address",
                                   address_type == eAddressTypeHost ? "host"
                                                                    : "load");
    return error;
  }

  // If we got here, we need to read the value from memory.
  size_t byte_size = GetValueByteSize(&error, exe_ctx);

  // Bail if we encountered any errors getting the byte size.
  if (error.Fail())
    return error;

  // No memory to read for zero-sized types.
  if (byte_size == 0)
    return error;

  // Make sure we have enough room within "data", and if we don't make
  // something large enough that does
  if (!data.ValidOffsetForDataOfSize(0, byte_size)) {
    auto data_sp = std::make_shared<DataBufferHeap>(byte_size, '\0');
    data.SetData(data_sp);
  }

  uint8_t *dst = const_cast<uint8_t *>(data.PeekData(0, byte_size));
  if (dst != nullptr) {
    if (address_type == eAddressTypeHost) {
      // The address is an address in this process, so just copy it.
      if (address == 0) {
        error.SetErrorString("trying to read from host address of 0.");
        return error;
      }
      memcpy(dst, reinterpret_cast<uint8_t *>(address), byte_size);
    } else if ((address_type == eAddressTypeLoad) ||
               (address_type == eAddressTypeFile)) {
      if (file_so_addr.IsValid()) {
        const bool force_live_memory = true;
        if (exe_ctx->GetTargetRef().ReadMemory(file_so_addr, dst, byte_size,
                                               error, force_live_memory) !=
            byte_size) {
          error.SetErrorStringWithFormat(
              "read memory from 0x%" PRIx64 " failed", (uint64_t)address);
        }
      } else {
        // The execution context might have a NULL process, but it might have a
        // valid process in the exe_ctx->target, so use the
        // ExecutionContext::GetProcess accessor to ensure we get the process
        // if there is one.
        Process *process = exe_ctx->GetProcessPtr();

        if (process) {
          const size_t bytes_read =
              process->ReadMemory(address, dst, byte_size, error);
          if (bytes_read != byte_size)
            error.SetErrorStringWithFormat(
                "read memory from 0x%" PRIx64 " failed (%u of %u bytes read)",
                (uint64_t)address, (uint32_t)bytes_read, (uint32_t)byte_size);
        } else {
          error.SetErrorStringWithFormat("read memory from 0x%" PRIx64
                                         " failed (invalid process)",
                                         (uint64_t)address);
        }
      }
    } else {
      error.SetErrorStringWithFormat("unsupported AddressType value (%i)",
                                     address_type);
    }
  } else {
    error.SetErrorString("out of memory");
  }

  return error;
}

Scalar &Value::ResolveValue(ExecutionContext *exe_ctx, Module *module) {
  const CompilerType &compiler_type = GetCompilerType();
  if (compiler_type.IsValid()) {
    switch (m_value_type) {
    case ValueType::Invalid:
    case ValueType::Scalar: // raw scalar value
      break;

    case ValueType::FileAddress:
    case ValueType::LoadAddress: // load address value
    case ValueType::HostAddress: // host address value (for memory in the process
                                // that is using liblldb)
    {
      DataExtractor data;
      lldb::addr_t addr = m_value.ULongLong(LLDB_INVALID_ADDRESS);
      Status error(GetValueAsData(exe_ctx, data, module));
      if (error.Success()) {
        Scalar scalar;
        if (compiler_type.GetValueAsScalar(
                data, 0, data.GetByteSize(), scalar,
                exe_ctx ? exe_ctx->GetBestExecutionContextScope() : nullptr)) {
          m_value = scalar;
          m_value_type = ValueType::Scalar;
        } else {
          if ((uintptr_t)addr != (uintptr_t)m_data_buffer.GetBytes()) {
            m_value.Clear();
            m_value_type = ValueType::Scalar;
          }
        }
      } else {
        if ((uintptr_t)addr != (uintptr_t)m_data_buffer.GetBytes()) {
          m_value.Clear();
          m_value_type = ValueType::Scalar;
        }
      }
    } break;
    }
  }
  return m_value;
}

Variable *Value::GetVariable() {
  if (m_context_type == ContextType::Variable)
    return static_cast<Variable *>(m_context);
  return nullptr;
}

void Value::Clear() {
  m_value.Clear();
  m_compiler_type.Clear();
  m_value_type = ValueType::Scalar;
  m_context = nullptr;
  m_context_type = ContextType::Invalid;
  m_data_buffer.Clear();
}

const char *Value::GetValueTypeAsCString(ValueType value_type) {
  switch (value_type) {
  case ValueType::Invalid:
    return "invalid";
  case ValueType::Scalar:
    return "scalar";
  case ValueType::FileAddress:
    return "file address";
  case ValueType::LoadAddress:
    return "load address";
  case ValueType::HostAddress:
    return "host address";
  };
  llvm_unreachable("enum cases exhausted.");
}

const char *Value::GetContextTypeAsCString(ContextType context_type) {
  switch (context_type) {
  case ContextType::Invalid:
    return "invalid";
  case ContextType::RegisterInfo:
    return "RegisterInfo *";
  case ContextType::LLDBType:
    return "Type *";
  case ContextType::Variable:
    return "Variable *";
  };
  llvm_unreachable("enum cases exhausted.");
}

void Value::ConvertToLoadAddress(Module *module, Target *target) {
  if (!module || !target || (GetValueType() != ValueType::FileAddress))
    return;

  lldb::addr_t file_addr = GetScalar().ULongLong(LLDB_INVALID_ADDRESS);
  if (file_addr == LLDB_INVALID_ADDRESS)
    return;

  Address so_addr;
  if (!module->ResolveFileAddress(file_addr, so_addr))
    return;
  lldb::addr_t load_addr = so_addr.GetLoadAddress(target);
  if (load_addr == LLDB_INVALID_ADDRESS)
    return;

  SetValueType(Value::ValueType::LoadAddress);
  GetScalar() = load_addr;
}

void ValueList::PushValue(const Value &value) { m_values.push_back(value); }

size_t ValueList::GetSize() { return m_values.size(); }

Value *ValueList::GetValueAtIndex(size_t idx) {
  if (idx < GetSize()) {
    return &(m_values[idx]);
  } else
    return nullptr;
}

void ValueList::Clear() { m_values.clear(); }
