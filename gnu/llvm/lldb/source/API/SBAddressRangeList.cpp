//===-- SBAddressRangeList.cpp --------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBAddressRangeList.h"
#include "Utils.h"
#include "lldb/API/SBAddressRange.h"
#include "lldb/API/SBStream.h"
#include "lldb/API/SBTarget.h"
#include "lldb/Core/AddressRangeListImpl.h"
#include "lldb/Utility/Instrumentation.h"
#include "lldb/Utility/Stream.h"

#include <memory>

using namespace lldb;
using namespace lldb_private;

SBAddressRangeList::SBAddressRangeList()
    : m_opaque_up(std::make_unique<AddressRangeListImpl>()) {
  LLDB_INSTRUMENT_VA(this);
}

SBAddressRangeList::SBAddressRangeList(const SBAddressRangeList &rhs)
    : m_opaque_up(std::make_unique<AddressRangeListImpl>(*rhs.m_opaque_up)) {
  LLDB_INSTRUMENT_VA(this, rhs);
}

SBAddressRangeList::~SBAddressRangeList() = default;

const SBAddressRangeList &
SBAddressRangeList::operator=(const SBAddressRangeList &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  if (this != &rhs)
    ref() = rhs.ref();
  return *this;
}

uint32_t SBAddressRangeList::GetSize() const {
  LLDB_INSTRUMENT_VA(this);

  return ref().GetSize();
}

SBAddressRange SBAddressRangeList::GetAddressRangeAtIndex(uint64_t idx) {
  LLDB_INSTRUMENT_VA(this, idx);

  SBAddressRange sb_addr_range;
  (*sb_addr_range.m_opaque_up) = ref().GetAddressRangeAtIndex(idx);
  return sb_addr_range;
}

void SBAddressRangeList::Clear() {
  LLDB_INSTRUMENT_VA(this);

  ref().Clear();
}

void SBAddressRangeList::Append(const SBAddressRange &sb_addr_range) {
  LLDB_INSTRUMENT_VA(this, sb_addr_range);

  ref().Append(*sb_addr_range.m_opaque_up);
}

void SBAddressRangeList::Append(const SBAddressRangeList &sb_addr_range_list) {
  LLDB_INSTRUMENT_VA(this, sb_addr_range_list);

  ref().Append(*sb_addr_range_list.m_opaque_up);
}

bool SBAddressRangeList::GetDescription(SBStream &description,
                                        const SBTarget &target) {
  LLDB_INSTRUMENT_VA(this, description, target);

  const uint32_t num_ranges = GetSize();
  bool is_first = true;
  Stream &stream = description.ref();
  stream << "[";
  for (uint32_t i = 0; i < num_ranges; ++i) {
    if (is_first) {
      is_first = false;
    } else {
      stream.Printf(", ");
    }
    GetAddressRangeAtIndex(i).GetDescription(description, target);
  }
  stream << "]";
  return true;
}

lldb_private::AddressRangeListImpl &SBAddressRangeList::ref() const {
  assert(m_opaque_up && "opaque pointer must always be valid");
  return *m_opaque_up;
}
