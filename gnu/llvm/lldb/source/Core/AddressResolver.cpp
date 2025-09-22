//===-- AddressResolver.cpp -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Core/AddressResolver.h"

#include "lldb/Core/SearchFilter.h"

namespace lldb_private {
class ModuleList;
}

using namespace lldb_private;

// AddressResolver:
AddressResolver::AddressResolver() = default;

AddressResolver::~AddressResolver() = default;

void AddressResolver::ResolveAddressInModules(SearchFilter &filter,
                                              ModuleList &modules) {
  filter.SearchInModuleList(*this, modules);
}

void AddressResolver::ResolveAddress(SearchFilter &filter) {
  filter.Search(*this);
}

std::vector<AddressRange> &AddressResolver::GetAddressRanges() {
  return m_address_ranges;
}

size_t AddressResolver::GetNumberOfAddresses() {
  return m_address_ranges.size();
}

AddressRange &AddressResolver::GetAddressRangeAtIndex(size_t idx) {
  return m_address_ranges[idx];
}
