//===-- Value.h -------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_CORE_VALUE_H
#define LLDB_CORE_VALUE_H

#include "lldb/Symbol/CompilerType.h"
#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/Scalar.h"
#include "lldb/Utility/Status.h"
#include "lldb/lldb-enumerations.h"
#include "lldb/lldb-private-enumerations.h"
#include "lldb/lldb-private-types.h"

#include "llvm/ADT/APInt.h"

#include <vector>

#include <cstdint>
#include <cstring>

namespace lldb_private {
class DataExtractor;
class ExecutionContext;
class Module;
class Stream;
class Type;
class Variable;
}

namespace lldb_private {

class Value {
public:
  /// Type that describes Value::m_value.
  enum class ValueType {
    Invalid = -1,
    // m_value contains:
    /// A raw scalar value.
    Scalar = 0,
    /// A file address value.
    FileAddress,
    /// A load address value.
    LoadAddress,
    /// A host address value (for memory in the process that < A is
    /// using liblldb).
    HostAddress
  };

  /// Type that describes Value::m_context.
  enum class ContextType {
    // m_context contains:
    /// Undefined.
    Invalid = -1,
    /// RegisterInfo * (can be a scalar or a vector register).
    RegisterInfo = 0,
    /// lldb_private::Type *.
    LLDBType,
    /// lldb_private::Variable *.
    Variable
  };

  Value();
  Value(const Scalar &scalar);
  Value(const void *bytes, int len);
  Value(const Value &rhs);

  void SetBytes(const void *bytes, int len);

  void AppendBytes(const void *bytes, int len);

  Value &operator=(const Value &rhs);

  const CompilerType &GetCompilerType();

  void SetCompilerType(const CompilerType &compiler_type);

  ValueType GetValueType() const;

  AddressType GetValueAddressType() const;

  ContextType GetContextType() const { return m_context_type; }

  void SetValueType(ValueType value_type) { m_value_type = value_type; }

  void ClearContext() {
    m_context = nullptr;
    m_context_type = ContextType::Invalid;
  }

  void SetContext(ContextType context_type, void *p) {
    m_context_type = context_type;
    m_context = p;
    if (m_context_type == ContextType::RegisterInfo) {
      RegisterInfo *reg_info = GetRegisterInfo();
      if (reg_info->encoding == lldb::eEncodingVector)
        SetValueType(ValueType::Scalar);
    }
  }

  RegisterInfo *GetRegisterInfo() const;

  Type *GetType();

  Scalar &ResolveValue(ExecutionContext *exe_ctx, Module *module = nullptr);

  const Scalar &GetScalar() const { return m_value; }

  Scalar &GetScalar() { return m_value; }

  size_t ResizeData(size_t len);

  size_t AppendDataToHostBuffer(const Value &rhs);

  DataBufferHeap &GetBuffer() { return m_data_buffer; }

  const DataBufferHeap &GetBuffer() const { return m_data_buffer; }

  bool ValueOf(ExecutionContext *exe_ctx);

  Variable *GetVariable();

  void Dump(Stream *strm);

  lldb::Format GetValueDefaultFormat();

  uint64_t GetValueByteSize(Status *error_ptr, ExecutionContext *exe_ctx);

  Status GetValueAsData(ExecutionContext *exe_ctx, DataExtractor &data,
                        Module *module); // Can be nullptr

  static const char *GetValueTypeAsCString(ValueType context_type);

  static const char *GetContextTypeAsCString(ContextType context_type);

  /// Convert this value's file address to a load address, if possible.
  void ConvertToLoadAddress(Module *module, Target *target);

  bool GetData(DataExtractor &data);

  void Clear();

  static ValueType GetValueTypeFromAddressType(AddressType address_type);

protected:
  Scalar m_value;
  CompilerType m_compiler_type;
  void *m_context = nullptr;
  ValueType m_value_type = ValueType::Scalar;
  ContextType m_context_type = ContextType::Invalid;
  DataBufferHeap m_data_buffer;
};

class ValueList {
public:
  ValueList() = default;
  ~ValueList() = default;

  ValueList(const ValueList &rhs) = default;
  ValueList &operator=(const ValueList &rhs) = default;

  // void InsertValue (Value *value, size_t idx);
  void PushValue(const Value &value);

  size_t GetSize();
  Value *GetValueAtIndex(size_t idx);
  void Clear();

private:
  typedef std::vector<Value> collection;

  collection m_values;
};

} // namespace lldb_private

#endif // LLDB_CORE_VALUE_H
