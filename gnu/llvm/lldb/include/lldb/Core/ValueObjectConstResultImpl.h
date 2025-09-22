//===-- ValueObjectConstResultImpl.h ----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_CORE_VALUEOBJECTCONSTRESULTIMPL_H
#define LLDB_CORE_VALUEOBJECTCONSTRESULTIMPL_H

#include "lldb/Utility/ConstString.h"
#include "lldb/lldb-defines.h"
#include "lldb/lldb-forward.h"
#include "lldb/lldb-private-enumerations.h"
#include "lldb/lldb-types.h"

#include <cstddef>
#include <cstdint>
namespace lldb_private {
class CompilerType;
class DataExtractor;
class Status;
class ValueObject;
}

namespace lldb_private {

/// A class wrapping common implementation details for operations in
/// ValueObjectConstResult ( & Child ) that may need to jump from the host
/// memory space into the target's memory space.
class ValueObjectConstResultImpl {
public:
  ValueObjectConstResultImpl(ValueObject *valobj,
                             lldb::addr_t live_address = LLDB_INVALID_ADDRESS);

  virtual ~ValueObjectConstResultImpl() = default;

  lldb::ValueObjectSP Dereference(Status &error);

  ValueObject *CreateChildAtIndex(size_t idx);
  ValueObject *CreateSyntheticArrayMember(size_t idx);

  lldb::ValueObjectSP
  GetSyntheticChildAtOffset(uint32_t offset, const CompilerType &type,
                            bool can_create,
                            ConstString name_const_str = ConstString());

  lldb::ValueObjectSP AddressOf(Status &error);

  lldb::addr_t GetLiveAddress() { return m_live_address; }

  lldb::ValueObjectSP Cast(const CompilerType &compiler_type);

  void SetLiveAddress(lldb::addr_t addr = LLDB_INVALID_ADDRESS,
                      AddressType address_type = eAddressTypeLoad) {
    m_live_address = addr;
    m_live_address_type = address_type;
  }

  virtual lldb::addr_t GetAddressOf(bool scalar_is_load_address = true,
                                    AddressType *address_type = nullptr);

  virtual size_t GetPointeeData(DataExtractor &data, uint32_t item_idx = 0,
                                uint32_t item_count = 1);

private:
  ValueObject *m_impl_backend;
  lldb::addr_t m_live_address;
  AddressType m_live_address_type;
  lldb::ValueObjectSP m_address_of_backend;

  ValueObjectConstResultImpl(const ValueObjectConstResultImpl &) = delete;
  const ValueObjectConstResultImpl &
  operator=(const ValueObjectConstResultImpl &) = delete;
};

} // namespace lldb_private

#endif // LLDB_CORE_VALUEOBJECTCONSTRESULTIMPL_H
