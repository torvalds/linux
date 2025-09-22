//===-- MemoryTagMap.cpp --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Target/MemoryTagMap.h"
#include <optional>

using namespace lldb_private;

MemoryTagMap::MemoryTagMap(const MemoryTagManager *manager)
    : m_manager(manager) {
  assert(m_manager && "valid tag manager required to construct a MemoryTagMap");
}

void MemoryTagMap::InsertTags(lldb::addr_t addr,
                              const std::vector<lldb::addr_t> tags) {
  // We're assuming that addr has no non address bits and is granule aligned.
  size_t granule_size = m_manager->GetGranuleSize();
  for (auto tag : tags) {
    m_addr_to_tag[addr] = tag;
    addr += granule_size;
  }
}

bool MemoryTagMap::Empty() const { return m_addr_to_tag.empty(); }

std::vector<std::optional<lldb::addr_t>>
MemoryTagMap::GetTags(lldb::addr_t addr, size_t len) const {
  // Addr and len might be unaligned
  addr = m_manager->RemoveTagBits(addr);
  MemoryTagManager::TagRange range(addr, len);
  range = m_manager->ExpandToGranule(range);

  std::vector<std::optional<lldb::addr_t>> tags;
  lldb::addr_t end_addr = range.GetRangeEnd();
  addr = range.GetRangeBase();
  bool got_valid_tags = false;
  size_t granule_size = m_manager->GetGranuleSize();

  for (; addr < end_addr; addr += granule_size) {
    std::optional<lldb::addr_t> tag = GetTag(addr);
    tags.push_back(tag);
    if (tag)
      got_valid_tags = true;
  }

  // To save the caller checking if every item is std::nullopt,
  // we return an empty vector if we got no tags at all.
  if (got_valid_tags)
    return tags;
  return {};
}

std::optional<lldb::addr_t> MemoryTagMap::GetTag(lldb::addr_t addr) const {
  // Here we assume that addr is granule aligned, just like when the tags
  // were inserted.
  auto found = m_addr_to_tag.find(addr);
  if (found == m_addr_to_tag.end())
    return std::nullopt;
  return found->second;
}
