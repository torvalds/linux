//===-- ValueObjectConstResultCast.h ----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_CORE_VALUEOBJECTCONSTRESULTCAST_H
#define LLDB_CORE_VALUEOBJECTCONSTRESULTCAST_H

#include "lldb/Core/ValueObjectCast.h"
#include "lldb/Core/ValueObjectConstResultImpl.h"
#include "lldb/Symbol/CompilerType.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/lldb-defines.h"
#include "lldb/lldb-forward.h"
#include "lldb/lldb-types.h"

#include <cstddef>
#include <cstdint>

namespace lldb_private {
class DataExtractor;
class Status;
class ValueObject;

class ValueObjectConstResultCast : public ValueObjectCast {
public:
  ValueObjectConstResultCast(ValueObject &parent, ConstString name,
                             const CompilerType &cast_type,
                             lldb::addr_t live_address = LLDB_INVALID_ADDRESS);

  ~ValueObjectConstResultCast() override;

  lldb::ValueObjectSP Dereference(Status &error) override;

  virtual CompilerType GetCompilerType() {
    return ValueObjectCast::GetCompilerType();
  }

  lldb::ValueObjectSP GetSyntheticChildAtOffset(
      uint32_t offset, const CompilerType &type, bool can_create,
      ConstString name_const_str = ConstString()) override;

  lldb::ValueObjectSP AddressOf(Status &error) override;

  size_t GetPointeeData(DataExtractor &data, uint32_t item_idx = 0,
                        uint32_t item_count = 1) override;

  lldb::ValueObjectSP DoCast(const CompilerType &compiler_type) override;

protected:
  ValueObjectConstResultImpl m_impl;

private:
  friend class ValueObject;
  friend class ValueObjectConstResult;
  friend class ValueObjectConstResultImpl;

  ValueObject *CreateChildAtIndex(size_t idx) override {
    return m_impl.CreateChildAtIndex(idx);
  }
  ValueObject *CreateSyntheticArrayMember(size_t idx) override {
    return m_impl.CreateSyntheticArrayMember(idx);
  }

  ValueObjectConstResultCast(const ValueObjectConstResultCast &) = delete;
  const ValueObjectConstResultCast &
  operator=(const ValueObjectConstResultCast &) = delete;
};

} // namespace lldb_private

#endif // LLDB_CORE_VALUEOBJECTCONSTRESULTCAST_H
