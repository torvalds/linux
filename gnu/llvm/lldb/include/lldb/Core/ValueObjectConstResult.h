//===-- ValueObjectConstResult.h --------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_CORE_VALUEOBJECTCONSTRESULT_H
#define LLDB_CORE_VALUEOBJECTCONSTRESULT_H

#include "lldb/Core/Value.h"
#include "lldb/Core/ValueObject.h"
#include "lldb/Core/ValueObjectConstResultImpl.h"
#include "lldb/Symbol/CompilerType.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/Status.h"
#include "lldb/lldb-defines.h"
#include "lldb/lldb-enumerations.h"
#include "lldb/lldb-forward.h"
#include "lldb/lldb-private-enumerations.h"
#include "lldb/lldb-types.h"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace lldb_private {
class DataExtractor;
class ExecutionContextScope;
class Module;

/// A frozen ValueObject copied into host memory.
class ValueObjectConstResult : public ValueObject {
public:
  ~ValueObjectConstResult() override;

  static lldb::ValueObjectSP
  Create(ExecutionContextScope *exe_scope, lldb::ByteOrder byte_order,
         uint32_t addr_byte_size, lldb::addr_t address = LLDB_INVALID_ADDRESS);

  static lldb::ValueObjectSP
  Create(ExecutionContextScope *exe_scope, const CompilerType &compiler_type,
         ConstString name, const DataExtractor &data,
         lldb::addr_t address = LLDB_INVALID_ADDRESS);

  static lldb::ValueObjectSP
  Create(ExecutionContextScope *exe_scope, const CompilerType &compiler_type,
         ConstString name, const lldb::DataBufferSP &result_data_sp,
         lldb::ByteOrder byte_order, uint32_t addr_size,
         lldb::addr_t address = LLDB_INVALID_ADDRESS);

  static lldb::ValueObjectSP
  Create(ExecutionContextScope *exe_scope, const CompilerType &compiler_type,
         ConstString name, lldb::addr_t address,
         AddressType address_type, uint32_t addr_byte_size);

  static lldb::ValueObjectSP Create(ExecutionContextScope *exe_scope,
                                    Value &value, ConstString name,
                                    Module *module = nullptr);

  // When an expression fails to evaluate, we return an error
  static lldb::ValueObjectSP Create(ExecutionContextScope *exe_scope,
                                    const Status &error);

  std::optional<uint64_t> GetByteSize() override;

  lldb::ValueType GetValueType() const override;

  llvm::Expected<uint32_t> CalculateNumChildren(uint32_t max) override;

  ConstString GetTypeName() override;

  ConstString GetDisplayTypeName() override;

  bool IsInScope() override;

  void SetByteSize(size_t size);

  lldb::ValueObjectSP Dereference(Status &error) override;

  lldb::ValueObjectSP GetSyntheticChildAtOffset(
      uint32_t offset, const CompilerType &type, bool can_create,
      ConstString name_const_str = ConstString()) override;

  lldb::ValueObjectSP AddressOf(Status &error) override;

  lldb::addr_t GetAddressOf(bool scalar_is_load_address = true,
                            AddressType *address_type = nullptr) override;

  size_t GetPointeeData(DataExtractor &data, uint32_t item_idx = 0,
                        uint32_t item_count = 1) override;

  lldb::addr_t GetLiveAddress() override { return m_impl.GetLiveAddress(); }

  void SetLiveAddress(lldb::addr_t addr = LLDB_INVALID_ADDRESS,
                      AddressType address_type = eAddressTypeLoad) override {
    m_impl.SetLiveAddress(addr, address_type);
  }

  lldb::ValueObjectSP
  GetDynamicValue(lldb::DynamicValueType valueType) override;

  lldb::LanguageType GetPreferredDisplayLanguage() override;

  lldb::ValueObjectSP DoCast(const CompilerType &compiler_type) override;

protected:
  bool UpdateValue() override;

  CompilerType GetCompilerTypeImpl() override;

  ConstString m_type_name;
  std::optional<uint64_t> m_byte_size;

  ValueObjectConstResultImpl m_impl;

private:
  friend class ValueObjectConstResultImpl;

  ValueObjectConstResult(ExecutionContextScope *exe_scope,
                         ValueObjectManager &manager,
                         lldb::ByteOrder byte_order, uint32_t addr_byte_size,
                         lldb::addr_t address);

  ValueObjectConstResult(ExecutionContextScope *exe_scope,
                         ValueObjectManager &manager,
                         const CompilerType &compiler_type, ConstString name,
                         const DataExtractor &data, lldb::addr_t address);

  ValueObjectConstResult(ExecutionContextScope *exe_scope,
                         ValueObjectManager &manager,
                         const CompilerType &compiler_type, ConstString name,
                         const lldb::DataBufferSP &result_data_sp,
                         lldb::ByteOrder byte_order, uint32_t addr_size,
                         lldb::addr_t address);

  ValueObjectConstResult(ExecutionContextScope *exe_scope,
                         ValueObjectManager &manager,
                         const CompilerType &compiler_type, ConstString name,
                         lldb::addr_t address, AddressType address_type,
                         uint32_t addr_byte_size);

  ValueObjectConstResult(ExecutionContextScope *exe_scope,
                         ValueObjectManager &manager, const Value &value,
                         ConstString name, Module *module = nullptr);

  ValueObjectConstResult(ExecutionContextScope *exe_scope,
                         ValueObjectManager &manager, const Status &error);

  ValueObject *CreateChildAtIndex(size_t idx) override {
    return m_impl.CreateChildAtIndex(idx);
  }
  ValueObject *CreateSyntheticArrayMember(size_t idx) override {
    return m_impl.CreateSyntheticArrayMember(idx);
  }

  ValueObjectConstResult(const ValueObjectConstResult &) = delete;
  const ValueObjectConstResult &
  operator=(const ValueObjectConstResult &) = delete;
};

} // namespace lldb_private

#endif // LLDB_CORE_VALUEOBJECTCONSTRESULT_H
