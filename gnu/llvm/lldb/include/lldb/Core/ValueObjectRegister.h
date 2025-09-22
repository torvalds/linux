//===-- ValueObjectRegister.h -----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_CORE_VALUEOBJECTREGISTER_H
#define LLDB_CORE_VALUEOBJECTREGISTER_H

#include "lldb/Core/ValueObject.h"
#include "lldb/Symbol/CompilerType.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/RegisterValue.h"
#include "lldb/lldb-defines.h"
#include "lldb/lldb-enumerations.h"
#include "lldb/lldb-forward.h"
#include "lldb/lldb-private-types.h"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace lldb_private {
class DataExtractor;
class Status;
class ExecutionContextScope;
class Scalar;
class Stream;

class ValueObjectRegisterSet : public ValueObject {
public:
  ~ValueObjectRegisterSet() override;

  static lldb::ValueObjectSP Create(ExecutionContextScope *exe_scope,
                                    lldb::RegisterContextSP &reg_ctx_sp,
                                    uint32_t set_idx);

  std::optional<uint64_t> GetByteSize() override;

  lldb::ValueType GetValueType() const override {
    return lldb::eValueTypeRegisterSet;
  }

  ConstString GetTypeName() override;

  ConstString GetQualifiedTypeName() override;

  llvm::Expected<uint32_t> CalculateNumChildren(uint32_t max) override;

  lldb::ValueObjectSP GetChildMemberWithName(llvm::StringRef name,
                                             bool can_create = true) override;

  size_t GetIndexOfChildWithName(llvm::StringRef name) override;

protected:
  bool UpdateValue() override;

  CompilerType GetCompilerTypeImpl() override;

  lldb::RegisterContextSP m_reg_ctx_sp;
  const RegisterSet *m_reg_set;
  uint32_t m_reg_set_idx;

private:
  friend class ValueObjectRegisterContext;

  ValueObjectRegisterSet(ExecutionContextScope *exe_scope,
                         ValueObjectManager &manager,
                         lldb::RegisterContextSP &reg_ctx_sp, uint32_t set_idx);

  ValueObject *CreateChildAtIndex(size_t idx) override;
  ValueObject *CreateSyntheticArrayMember(size_t idx) override {
    return nullptr;
  }

  // For ValueObject only
  ValueObjectRegisterSet(const ValueObjectRegisterSet &) = delete;
  const ValueObjectRegisterSet &
  operator=(const ValueObjectRegisterSet &) = delete;
};

class ValueObjectRegister : public ValueObject {
public:
  ~ValueObjectRegister() override;

  static lldb::ValueObjectSP Create(ExecutionContextScope *exe_scope,
                                    lldb::RegisterContextSP &reg_ctx_sp,
                                    const RegisterInfo *reg_info);

  std::optional<uint64_t> GetByteSize() override;

  lldb::ValueType GetValueType() const override {
    return lldb::eValueTypeRegister;
  }

  ConstString GetTypeName() override;

  llvm::Expected<uint32_t> CalculateNumChildren(uint32_t max) override;

  bool SetValueFromCString(const char *value_str, Status &error) override;

  bool SetData(DataExtractor &data, Status &error) override;

  bool ResolveValue(Scalar &scalar) override;

  void
  GetExpressionPath(Stream &s,
                    GetExpressionPathFormat epformat =
                        eGetExpressionPathFormatDereferencePointers) override;

protected:
  bool UpdateValue() override;

  CompilerType GetCompilerTypeImpl() override;

  lldb::RegisterContextSP m_reg_ctx_sp;
  RegisterInfo m_reg_info;
  RegisterValue m_reg_value;
  ConstString m_type_name;
  CompilerType m_compiler_type;

private:
  void ConstructObject(const RegisterInfo *reg_info);

  friend class ValueObjectRegisterSet;

  ValueObjectRegister(ValueObject &parent, lldb::RegisterContextSP &reg_ctx_sp,
                      const RegisterInfo *reg_info);
  ValueObjectRegister(ExecutionContextScope *exe_scope,
                      ValueObjectManager &manager,
                      lldb::RegisterContextSP &reg_ctx_sp,
                      const RegisterInfo *reg_info);

  // For ValueObject only
  ValueObjectRegister(const ValueObjectRegister &) = delete;
  const ValueObjectRegister &operator=(const ValueObjectRegister &) = delete;
};

} // namespace lldb_private

#endif // LLDB_CORE_VALUEOBJECTREGISTER_H
