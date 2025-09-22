//===-- SBMemoryRegionInfoList.cpp ----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBMemoryRegionInfoList.h"
#include "lldb/API/SBMemoryRegionInfo.h"
#include "lldb/API/SBStream.h"
#include "lldb/Target/MemoryRegionInfo.h"
#include "lldb/Utility/Instrumentation.h"

#include <vector>

using namespace lldb;
using namespace lldb_private;

class MemoryRegionInfoListImpl {
public:
  MemoryRegionInfoListImpl() : m_regions() {}

  MemoryRegionInfoListImpl(const MemoryRegionInfoListImpl &rhs) = default;

  MemoryRegionInfoListImpl &operator=(const MemoryRegionInfoListImpl &rhs) {
    if (this == &rhs)
      return *this;
    m_regions = rhs.m_regions;
    return *this;
  }

  size_t GetSize() const { return m_regions.size(); }

  void Reserve(size_t capacity) { return m_regions.reserve(capacity); }

  void Append(const MemoryRegionInfo &sb_region) {
    m_regions.push_back(sb_region);
  }

  void Append(const MemoryRegionInfoListImpl &list) {
    Reserve(GetSize() + list.GetSize());

    for (const auto &val : list.m_regions)
      Append(val);
  }

  void Clear() { m_regions.clear(); }

  bool GetMemoryRegionContainingAddress(lldb::addr_t addr,
                                        MemoryRegionInfo &region_info) {
    for (auto &region : m_regions) {
      if (region.GetRange().Contains(addr)) {
        region_info = region;
        return true;
      }
    }
    return false;
  }

  bool GetMemoryRegionInfoAtIndex(size_t index,
                                  MemoryRegionInfo &region_info) {
    if (index >= GetSize())
      return false;
    region_info = m_regions[index];
    return true;
  }

  MemoryRegionInfos &Ref() { return m_regions; }

  const MemoryRegionInfos &Ref() const { return m_regions; }

private:
  MemoryRegionInfos m_regions;
};

MemoryRegionInfos &SBMemoryRegionInfoList::ref() { return m_opaque_up->Ref(); }

const MemoryRegionInfos &SBMemoryRegionInfoList::ref() const {
  return m_opaque_up->Ref();
}

SBMemoryRegionInfoList::SBMemoryRegionInfoList()
    : m_opaque_up(new MemoryRegionInfoListImpl()) {
  LLDB_INSTRUMENT_VA(this);
}

SBMemoryRegionInfoList::SBMemoryRegionInfoList(
    const SBMemoryRegionInfoList &rhs)
    : m_opaque_up(new MemoryRegionInfoListImpl(*rhs.m_opaque_up)) {
  LLDB_INSTRUMENT_VA(this, rhs);
}

SBMemoryRegionInfoList::~SBMemoryRegionInfoList() = default;

const SBMemoryRegionInfoList &SBMemoryRegionInfoList::
operator=(const SBMemoryRegionInfoList &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  if (this != &rhs) {
    *m_opaque_up = *rhs.m_opaque_up;
  }
  return *this;
}

uint32_t SBMemoryRegionInfoList::GetSize() const {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_up->GetSize();
}

bool SBMemoryRegionInfoList::GetMemoryRegionContainingAddress(
    lldb::addr_t addr, SBMemoryRegionInfo &region_info) {
  LLDB_INSTRUMENT_VA(this, addr, region_info);

  return m_opaque_up->GetMemoryRegionContainingAddress(addr, region_info.ref());
}

bool SBMemoryRegionInfoList::GetMemoryRegionAtIndex(
    uint32_t idx, SBMemoryRegionInfo &region_info) {
  LLDB_INSTRUMENT_VA(this, idx, region_info);

  return m_opaque_up->GetMemoryRegionInfoAtIndex(idx, region_info.ref());
}

void SBMemoryRegionInfoList::Clear() {
  LLDB_INSTRUMENT_VA(this);

  m_opaque_up->Clear();
}

void SBMemoryRegionInfoList::Append(SBMemoryRegionInfo &sb_region) {
  LLDB_INSTRUMENT_VA(this, sb_region);

  m_opaque_up->Append(sb_region.ref());
}

void SBMemoryRegionInfoList::Append(SBMemoryRegionInfoList &sb_region_list) {
  LLDB_INSTRUMENT_VA(this, sb_region_list);

  m_opaque_up->Append(*sb_region_list);
}

const MemoryRegionInfoListImpl *SBMemoryRegionInfoList::operator->() const {
  return m_opaque_up.get();
}

const MemoryRegionInfoListImpl &SBMemoryRegionInfoList::operator*() const {
  assert(m_opaque_up.get());
  return *m_opaque_up;
}
