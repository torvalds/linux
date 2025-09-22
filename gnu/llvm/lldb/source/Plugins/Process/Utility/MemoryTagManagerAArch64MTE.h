//===-- MemoryTagManagerAArch64MTE.h ----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_MEMORYTAGMANAGERAARCH64MTE_H
#define LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_MEMORYTAGMANAGERAARCH64MTE_H

#include "lldb/Target/MemoryTagManager.h"

namespace lldb_private {

class MemoryTagManagerAArch64MTE : public MemoryTagManager {
public:
  // This enum is supposed to be shared for all of AArch64 but until
  // there are more tag types than MTE, it will live here.
  enum MTETagTypes {
    eMTE_logical = 0,
    eMTE_allocation = 1,
  };

  lldb::addr_t GetGranuleSize() const override;
  int32_t GetAllocationTagType() const override;
  size_t GetTagSizeInBytes() const override;

  lldb::addr_t GetLogicalTag(lldb::addr_t addr) const override;
  lldb::addr_t RemoveTagBits(lldb::addr_t addr) const override;
  ptrdiff_t AddressDiff(lldb::addr_t addr1, lldb::addr_t addr2) const override;

  TagRange ExpandToGranule(TagRange range) const override;

  llvm::Expected<TagRange> MakeTaggedRange(
      lldb::addr_t addr, lldb::addr_t end_addr,
      const lldb_private::MemoryRegionInfos &memory_regions) const override;

  llvm::Expected<std::vector<TagRange>> MakeTaggedRanges(
      lldb::addr_t addr, lldb::addr_t end_addr,
      const lldb_private::MemoryRegionInfos &memory_regions) const override;

  llvm::Expected<std::vector<lldb::addr_t>>
  UnpackTagsData(const std::vector<uint8_t> &tags,
                 size_t granules = 0) const override;

  std::vector<lldb::addr_t>
  UnpackTagsFromCoreFileSegment(CoreReaderFn reader,
                                lldb::addr_t tag_segment_virtual_address,
                                lldb::addr_t tag_segment_data_address,
                                lldb::addr_t addr, size_t len) const override;

  llvm::Expected<std::vector<uint8_t>>
  PackTags(const std::vector<lldb::addr_t> &tags) const override;

  llvm::Expected<std::vector<lldb::addr_t>>
  RepeatTagsForRange(const std::vector<lldb::addr_t> &tags,
                     TagRange range) const override;
};

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_MEMORYTAGMANAGERAARCH64MTE_H
