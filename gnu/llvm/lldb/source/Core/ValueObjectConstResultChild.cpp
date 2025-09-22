//===-- ValueObjectConstResultChild.cpp -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Core/ValueObjectConstResultChild.h"

#include "lldb/lldb-private-enumerations.h"
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

ValueObjectConstResultChild::ValueObjectConstResultChild(
    ValueObject &parent, const CompilerType &compiler_type,
    ConstString name, uint32_t byte_size, int32_t byte_offset,
    uint32_t bitfield_bit_size, uint32_t bitfield_bit_offset,
    bool is_base_class, bool is_deref_of_parent, lldb::addr_t live_address,
    uint64_t language_flags)
    : ValueObjectChild(parent, compiler_type, name, byte_size, byte_offset,
                       bitfield_bit_size, bitfield_bit_offset, is_base_class,
                       is_deref_of_parent, eAddressTypeLoad, language_flags),
      m_impl(this, live_address) {
  m_name = name;
}

ValueObjectConstResultChild::~ValueObjectConstResultChild() = default;

lldb::ValueObjectSP ValueObjectConstResultChild::Dereference(Status &error) {
  return m_impl.Dereference(error);
}

lldb::ValueObjectSP ValueObjectConstResultChild::GetSyntheticChildAtOffset(
    uint32_t offset, const CompilerType &type, bool can_create,
    ConstString name_const_str) {
  return m_impl.GetSyntheticChildAtOffset(offset, type, can_create,
                                          name_const_str);
}

lldb::ValueObjectSP ValueObjectConstResultChild::AddressOf(Status &error) {
  return m_impl.AddressOf(error);
}

lldb::addr_t ValueObjectConstResultChild::GetAddressOf(
  bool scalar_is_load_address, AddressType* address_type) {
  return m_impl.GetAddressOf(scalar_is_load_address, address_type);
}

size_t ValueObjectConstResultChild::GetPointeeData(DataExtractor &data,
                                                   uint32_t item_idx,
                                                   uint32_t item_count) {
  return m_impl.GetPointeeData(data, item_idx, item_count);
}

lldb::ValueObjectSP
ValueObjectConstResultChild::DoCast(const CompilerType &compiler_type) {
  return m_impl.Cast(compiler_type);
}
