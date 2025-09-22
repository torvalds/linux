//===-- AddressRangeListImpl.cpp ------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Core/AddressRangeListImpl.h"

using namespace lldb;
using namespace lldb_private;

AddressRangeListImpl::AddressRangeListImpl() : m_ranges() {}

AddressRangeListImpl &
AddressRangeListImpl::operator=(const AddressRangeListImpl &rhs) {
  if (this == &rhs)
    return *this;
  m_ranges = rhs.m_ranges;
  return *this;
}

size_t AddressRangeListImpl::GetSize() const { return m_ranges.size(); }

void AddressRangeListImpl::Reserve(size_t capacity) {
  m_ranges.reserve(capacity);
}

void AddressRangeListImpl::Append(const AddressRange &sb_region) {
  m_ranges.emplace_back(sb_region);
}

void AddressRangeListImpl::Append(const AddressRangeListImpl &list) {
  Reserve(GetSize() + list.GetSize());

  for (const auto &range : list.m_ranges)
    Append(range);
}

void AddressRangeListImpl::Clear() { m_ranges.clear(); }

lldb_private::AddressRange
AddressRangeListImpl::GetAddressRangeAtIndex(size_t index) {
  if (index >= GetSize())
    return AddressRange();
  return m_ranges[index];
}

AddressRanges &AddressRangeListImpl::ref() { return m_ranges; }
