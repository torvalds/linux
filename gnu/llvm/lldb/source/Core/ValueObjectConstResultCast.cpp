//===-- ValueObjectConstResultCast.cpp ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Core/ValueObjectConstResultCast.h"

namespace lldb_private {
class DataExtractor;
}
namespace lldb_private {
class Status;
}
namespace lldb_private {
class ValueObject;
}

using namespace lldb_private;

ValueObjectConstResultCast::ValueObjectConstResultCast(
    ValueObject &parent, ConstString name, const CompilerType &cast_type,
    lldb::addr_t live_address)
    : ValueObjectCast(parent, name, cast_type), m_impl(this, live_address) {
  m_name = name;
}

ValueObjectConstResultCast::~ValueObjectConstResultCast() = default;

lldb::ValueObjectSP ValueObjectConstResultCast::Dereference(Status &error) {
  return m_impl.Dereference(error);
}

lldb::ValueObjectSP ValueObjectConstResultCast::GetSyntheticChildAtOffset(
    uint32_t offset, const CompilerType &type, bool can_create,
    ConstString name_const_str) {
  return m_impl.GetSyntheticChildAtOffset(offset, type, can_create,
                                          name_const_str);
}

lldb::ValueObjectSP ValueObjectConstResultCast::AddressOf(Status &error) {
  return m_impl.AddressOf(error);
}

size_t ValueObjectConstResultCast::GetPointeeData(DataExtractor &data,
                                                  uint32_t item_idx,
                                                  uint32_t item_count) {
  return m_impl.GetPointeeData(data, item_idx, item_count);
}

lldb::ValueObjectSP
ValueObjectConstResultCast::DoCast(const CompilerType &compiler_type) {
  return m_impl.Cast(compiler_type);
}
