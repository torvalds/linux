//===-- MemoryTagManagerAArch64MTE.cpp --------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MemoryTagManagerAArch64MTE.h"
#include "llvm/Support/Error.h"
#include <assert.h>

using namespace lldb_private;

static const unsigned MTE_START_BIT = 56;
static const unsigned MTE_TAG_MAX = 0xf;
static const unsigned MTE_GRANULE_SIZE = 16;

lldb::addr_t
MemoryTagManagerAArch64MTE::GetLogicalTag(lldb::addr_t addr) const {
  return (addr >> MTE_START_BIT) & MTE_TAG_MAX;
}

lldb::addr_t
MemoryTagManagerAArch64MTE::RemoveTagBits(lldb::addr_t addr) const {
  // Here we're ignoring the whole top byte. If you've got MTE
  // you must also have TBI (top byte ignore).
  // The other 4 bits could contain other extension bits or
  // user metadata.
  return addr & ~((lldb::addr_t)0xFF << MTE_START_BIT);
}

ptrdiff_t MemoryTagManagerAArch64MTE::AddressDiff(lldb::addr_t addr1,
                                                  lldb::addr_t addr2) const {
  return RemoveTagBits(addr1) - RemoveTagBits(addr2);
}

lldb::addr_t MemoryTagManagerAArch64MTE::GetGranuleSize() const {
  return MTE_GRANULE_SIZE;
}

int32_t MemoryTagManagerAArch64MTE::GetAllocationTagType() const {
  return eMTE_allocation;
}

size_t MemoryTagManagerAArch64MTE::GetTagSizeInBytes() const { return 1; }

MemoryTagManagerAArch64MTE::TagRange
MemoryTagManagerAArch64MTE::ExpandToGranule(TagRange range) const {
  // Ignore reading a length of 0
  if (!range.IsValid())
    return range;

  const size_t granule = GetGranuleSize();

  // Align start down to granule start
  lldb::addr_t new_start = range.GetRangeBase();
  lldb::addr_t align_down_amount = new_start % granule;
  new_start -= align_down_amount;

  // Account for the distance we moved the start above
  size_t new_len = range.GetByteSize() + align_down_amount;
  // Then align up to the end of the granule
  size_t align_up_amount = granule - (new_len % granule);
  if (align_up_amount != granule)
    new_len += align_up_amount;

  return TagRange(new_start, new_len);
}

static llvm::Error MakeInvalidRangeErr(lldb::addr_t addr,
                                       lldb::addr_t end_addr) {
  return llvm::createStringError(
      llvm::inconvertibleErrorCode(),
      "End address (0x%" PRIx64
      ") must be greater than the start address (0x%" PRIx64 ")",
      end_addr, addr);
}

llvm::Expected<MemoryTagManager::TagRange>
MemoryTagManagerAArch64MTE::MakeTaggedRange(
    lldb::addr_t addr, lldb::addr_t end_addr,
    const lldb_private::MemoryRegionInfos &memory_regions) const {
  // First check that the range is not inverted.
  // We must remove tags here otherwise an address with a higher
  // tag value will always be > the other.
  ptrdiff_t len = AddressDiff(end_addr, addr);
  if (len <= 0)
    return MakeInvalidRangeErr(addr, end_addr);

  // Region addresses will not have memory tags. So when searching
  // we must use an untagged address.
  MemoryRegionInfo::RangeType tag_range(RemoveTagBits(addr), len);
  tag_range = ExpandToGranule(tag_range);

  // Make a copy so we can use the original for errors and the final return.
  MemoryRegionInfo::RangeType remaining_range(tag_range);

  // While there are parts of the range that don't have a matching tagged memory
  // region
  while (remaining_range.IsValid()) {
    // Search for a region that contains the start of the range
    MemoryRegionInfos::const_iterator region = std::find_if(
        memory_regions.cbegin(), memory_regions.cend(),
        [&remaining_range](const MemoryRegionInfo &region) {
          return region.GetRange().Contains(remaining_range.GetRangeBase());
        });

    if (region == memory_regions.cend() ||
        region->GetMemoryTagged() != MemoryRegionInfo::eYes) {
      // Some part of this range is untagged (or unmapped) so error
      return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                     "Address range 0x%" PRIx64 ":0x%" PRIx64
                                     " is not in a memory tagged region",
                                     tag_range.GetRangeBase(),
                                     tag_range.GetRangeEnd());
    }

    // We've found some part of the range so remove that part and continue
    // searching for the rest. Moving the base "slides" the range so we need to
    // save/restore the original end. If old_end is less than the new base, the
    // range will be set to have 0 size and we'll exit the while.
    lldb::addr_t old_end = remaining_range.GetRangeEnd();
    remaining_range.SetRangeBase(region->GetRange().GetRangeEnd());
    remaining_range.SetRangeEnd(old_end);
  }

  // Every part of the range is contained within a tagged memory region.
  return tag_range;
}

llvm::Expected<std::vector<MemoryTagManager::TagRange>>
MemoryTagManagerAArch64MTE::MakeTaggedRanges(
    lldb::addr_t addr, lldb::addr_t end_addr,
    const lldb_private::MemoryRegionInfos &memory_regions) const {
  // First check that the range is not inverted.
  // We must remove tags here otherwise an address with a higher
  // tag value will always be > the other.
  ptrdiff_t len = AddressDiff(end_addr, addr);
  if (len <= 0)
    return MakeInvalidRangeErr(addr, end_addr);

  std::vector<MemoryTagManager::TagRange> tagged_ranges;
  // No memory regions means no tagged memory at all
  if (memory_regions.empty())
    return tagged_ranges;

  // For the logic to work regions must be in ascending order
  // which is what you'd have if you used GetMemoryRegions.
  assert(std::is_sorted(
      memory_regions.begin(), memory_regions.end(),
      [](const MemoryRegionInfo &lhs, const MemoryRegionInfo &rhs) {
        return lhs.GetRange().GetRangeBase() < rhs.GetRange().GetRangeBase();
      }));

  // If we're debugging userspace in an OS like Linux that uses an MMU,
  // the only reason we'd get overlapping regions is incorrect data.
  // It is possible that won't hold for embedded with memory protection
  // units (MPUs) that allow overlaps.
  //
  // For now we're going to assume the former, as there is no good way
  // to handle overlaps. For example:
  // < requested range >
  // [--  region 1   --]
  //           [-- region 2--]
  // Where the first region will reduce the requested range to nothing
  // and exit early before it sees the overlap.
  MemoryRegionInfos::const_iterator overlap = std::adjacent_find(
      memory_regions.begin(), memory_regions.end(),
      [](const MemoryRegionInfo &lhs, const MemoryRegionInfo &rhs) {
        return rhs.GetRange().DoesIntersect(lhs.GetRange());
      });
  UNUSED_IF_ASSERT_DISABLED(overlap);
  assert(overlap == memory_regions.end());

  // Region addresses will not have memory tags so when searching
  // we must use an untagged address.
  MemoryRegionInfo::RangeType range(RemoveTagBits(addr), len);
  range = ExpandToGranule(range);

  // While there are regions to check and the range has non zero length
  for (const MemoryRegionInfo &region : memory_regions) {
    // If range we're checking has been reduced to zero length, exit early
    if (!range.IsValid())
      break;

    // If the region doesn't overlap the range at all, ignore it.
    if (!region.GetRange().DoesIntersect(range))
      continue;

    // If it's tagged record this sub-range.
    // (assuming that it's already granule aligned)
    if (region.GetMemoryTagged()) {
      // The region found may extend outside the requested range.
      // For example the first region might start before the range.
      // We must only add what covers the requested range.
      lldb::addr_t start =
          std::max(range.GetRangeBase(), region.GetRange().GetRangeBase());
      lldb::addr_t end =
          std::min(range.GetRangeEnd(), region.GetRange().GetRangeEnd());
      tagged_ranges.push_back(MemoryTagManager::TagRange(start, end - start));
    }

    // Move the range up to start at the end of the region.
    lldb::addr_t old_end = range.GetRangeEnd();
    // This "slides" the range so it moves the end as well.
    range.SetRangeBase(region.GetRange().GetRangeEnd());
    // So we set the end back to the original end address after sliding it up.
    range.SetRangeEnd(old_end);
    // (if the above were to try to set end < begin the range will just be set
    // to 0 size)
  }

  return tagged_ranges;
}

llvm::Expected<std::vector<lldb::addr_t>>
MemoryTagManagerAArch64MTE::UnpackTagsData(const std::vector<uint8_t> &tags,
                                           size_t granules /*=0*/) const {
  // 0 means don't check the number of tags before unpacking
  if (granules) {
    size_t num_tags = tags.size() / GetTagSizeInBytes();
    if (num_tags != granules) {
      return llvm::createStringError(
          llvm::inconvertibleErrorCode(),
          "Packed tag data size does not match expected number of tags. "
          "Expected %zu tag(s) for %zu granule(s), got %zu tag(s).",
          granules, granules, num_tags);
    }
  }

  // (if bytes per tag was not 1, we would reconstruct them here)

  std::vector<lldb::addr_t> unpacked;
  unpacked.reserve(tags.size());
  for (auto it = tags.begin(); it != tags.end(); ++it) {
    // Check all tags are in range
    if (*it > MTE_TAG_MAX) {
      return llvm::createStringError(
          llvm::inconvertibleErrorCode(),
          "Found tag 0x%x which is > max MTE tag value of 0x%x.", *it,
          MTE_TAG_MAX);
    }
    unpacked.push_back(*it);
  }

  return unpacked;
}

std::vector<lldb::addr_t>
MemoryTagManagerAArch64MTE::UnpackTagsFromCoreFileSegment(
    CoreReaderFn reader, lldb::addr_t tag_segment_virtual_address,
    lldb::addr_t tag_segment_data_address, lldb::addr_t addr,
    size_t len) const {
  // We can assume by now that addr and len have been granule aligned by a tag
  // manager. However because we have 2 tags per byte we need to round the range
  // up again to align to 2 granule boundaries.
  const size_t granule = GetGranuleSize();
  const size_t two_granules = granule * 2;
  lldb::addr_t aligned_addr = addr;
  size_t aligned_len = len;

  // First align the start address down.
  if (aligned_addr % two_granules) {
    assert(aligned_addr % two_granules == granule);
    aligned_addr -= granule;
    aligned_len += granule;
  }

  // Then align the length up.
  bool aligned_length_up = false;
  if (aligned_len % two_granules) {
    assert(aligned_len % two_granules == granule);
    aligned_len += granule;
    aligned_length_up = true;
  }

  // ProcessElfCore should have validated this when it found the segment.
  assert(aligned_addr >= tag_segment_virtual_address);

  // By now we know that aligned_addr is aligned to a 2 granule boundary.
  const size_t offset_granules =
      (aligned_addr - tag_segment_virtual_address) / granule;
  // 2 tags per byte.
  const size_t file_offset_in_bytes = offset_granules / 2;

  // By now we know that aligned_len is at least 2 granules.
  const size_t tag_bytes_to_read = aligned_len / granule / 2;
  std::vector<uint8_t> tag_data(tag_bytes_to_read);
  const size_t bytes_copied =
      reader(tag_segment_data_address + file_offset_in_bytes, tag_bytes_to_read,
             tag_data.data());
  UNUSED_IF_ASSERT_DISABLED(bytes_copied);
  assert(bytes_copied == tag_bytes_to_read);

  std::vector<lldb::addr_t> tags;
  tags.reserve(2 * tag_data.size());
  // No need to check the range of the tag value here as each occupies only 4
  // bits.
  for (auto tag_byte : tag_data) {
    tags.push_back(tag_byte & 0xf);
    tags.push_back(tag_byte >> 4);
  }

  // If we aligned the address down, don't return the extra first tag.
  if (addr != aligned_addr)
    tags.erase(tags.begin());
  // If we aligned the length up, don't return the extra last tag.
  if (aligned_length_up)
    tags.pop_back();

  return tags;
}

llvm::Expected<std::vector<uint8_t>> MemoryTagManagerAArch64MTE::PackTags(
    const std::vector<lldb::addr_t> &tags) const {
  std::vector<uint8_t> packed;
  packed.reserve(tags.size() * GetTagSizeInBytes());

  for (auto tag : tags) {
    if (tag > MTE_TAG_MAX) {
      return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                     "Found tag 0x%" PRIx64
                                     " which is > max MTE tag value of 0x%x.",
                                     tag, MTE_TAG_MAX);
    }
    packed.push_back(static_cast<uint8_t>(tag));
  }

  return packed;
}

llvm::Expected<std::vector<lldb::addr_t>>
MemoryTagManagerAArch64MTE::RepeatTagsForRange(
    const std::vector<lldb::addr_t> &tags, TagRange range) const {
  std::vector<lldb::addr_t> new_tags;

  // If the range is not empty
  if (range.IsValid()) {
    if (tags.empty()) {
      return llvm::createStringError(
          llvm::inconvertibleErrorCode(),
          "Expected some tags to cover given range, got zero.");
    }

    // We assume that this range has already been expanded/aligned to granules
    size_t granules = range.GetByteSize() / GetGranuleSize();
    new_tags.reserve(granules);
    for (size_t to_copy = 0; granules > 0; granules -= to_copy) {
      to_copy = granules > tags.size() ? tags.size() : granules;
      new_tags.insert(new_tags.end(), tags.begin(), tags.begin() + to_copy);
    }
  }

  return new_tags;
}
