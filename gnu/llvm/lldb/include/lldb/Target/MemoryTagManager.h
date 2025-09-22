//===-- MemoryTagManager.h --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TARGET_MEMORYTAGMANAGER_H
#define LLDB_TARGET_MEMORYTAGMANAGER_H

#include "lldb/Target/MemoryRegionInfo.h"
#include "lldb/Utility/RangeMap.h"
#include "lldb/lldb-private.h"
#include "llvm/Support/Error.h"

namespace lldb_private {

// This interface allows high level commands to handle memory tags
// in a generic way.
//
// Definitions:
//   logical tag    - the tag stored in a pointer
//   allocation tag - the tag stored in hardware
//                    (e.g. special memory, cache line bits)
//   granule        - number of bytes of memory a single tag applies to

class MemoryTagManager {
public:
  typedef Range<lldb::addr_t, lldb::addr_t> TagRange;

  // Extract the logical tag from a pointer
  // The tag is returned as a plain value, with any shifts removed.
  // For example if your tags are stored in bits 56-60 then the logical tag
  // you get will have been shifted down 56 before being returned.
  virtual lldb::addr_t GetLogicalTag(lldb::addr_t addr) const = 0;

  // Remove tag bits from a pointer
  virtual lldb::addr_t RemoveTagBits(lldb::addr_t addr) const = 0;

  // Return the difference between two addresses, ignoring any logical tags they
  // have. If your tags are just part of a larger set of ignored bits, this
  // should ignore all those bits.
  virtual ptrdiff_t AddressDiff(lldb::addr_t addr1,
                                lldb::addr_t addr2) const = 0;

  // Return the number of bytes a single tag covers
  virtual lldb::addr_t GetGranuleSize() const = 0;

  // Align an address range to granule boundaries.
  // So that reading memory tags for the new range returns
  // tags that will cover the original range.
  //
  // Say your granules are 16 bytes and you want
  // tags for 16 bytes of memory starting from address 8.
  // 1 granule isn't enough because it only covers addresses
  // 0-16, we want addresses 8-24. So the range must be
  // expanded to 2 granules.
  virtual TagRange ExpandToGranule(TagRange range) const = 0;

  // Given a range addr to end_addr, check that:
  // * end_addr >= addr (when memory tags are removed)
  // * the granule aligned range is completely covered by tagged memory
  //   (which may include one or more memory regions)
  //
  // If so, return a modified range which will have been expanded
  // to be granule aligned. Otherwise return an error.
  //
  // Tags in the input addresses are ignored and not present
  // in the returned range.
  virtual llvm::Expected<TagRange> MakeTaggedRange(
      lldb::addr_t addr, lldb::addr_t end_addr,
      const lldb_private::MemoryRegionInfos &memory_regions) const = 0;

  // Given a range addr to end_addr, check that end_addr >= addr.
  // If it is not, return an error saying so.
  // Otherwise, granule align it and return a set of ranges representing
  // subsections of the aligned range that have memory tagging enabled.
  //
  // Basically a sparse version of MakeTaggedRange. Use this when you
  // want to know which parts of a larger range have memory tagging.
  //
  // Regions in memory_regions should be sorted in ascending order and
  // not overlap. (use Process GetMemoryRegions)
  //
  // Tags in the input addresses are ignored and not present
  // in the returned ranges.
  virtual llvm::Expected<std::vector<TagRange>> MakeTaggedRanges(
      lldb::addr_t addr, lldb::addr_t end_addr,
      const lldb_private::MemoryRegionInfos &memory_regions) const = 0;

  // Return the type value to use in GDB protocol qMemTags packets to read
  // allocation tags. This is named "Allocation" specifically because the spec
  // allows for logical tags to be read the same way, though we do not use that.
  //
  // This value is unique within a given architecture. Meaning that different
  // tagging schemes within the same architecture should use unique values,
  // but other architectures can overlap those values.
  virtual int32_t GetAllocationTagType() const = 0;

  // Return the number of bytes a single tag will be packed into during
  // transport. For example an MTE tag is 4 bits but occupies 1 byte during
  // transport.
  virtual size_t GetTagSizeInBytes() const = 0;

  // Unpack tags from their stored format (e.g. gdb qMemTags data) into separate
  // tags.
  //
  // Checks that each tag is within the expected value range and if granules is
  // set to non-zero, that the number of tags found matches the number of
  // granules we expected to cover.
  virtual llvm::Expected<std::vector<lldb::addr_t>>
  UnpackTagsData(const std::vector<uint8_t> &tags,
                 size_t granules = 0) const = 0;

  // Unpack tags from a corefile segment containing compressed tags
  // (compression that may be different from the one used for GDB transport).
  //
  // This method asumes that:
  // * addr and len have been granule aligned by a tag manager
  // * addr >= tag_segment_virtual_address
  //
  // 'reader' will always be a wrapper around a CoreFile in real use
  // but allows testing without having to mock a CoreFile.
  typedef std::function<size_t(lldb::offset_t, size_t, void *)> CoreReaderFn;
  std::vector<lldb::addr_t> virtual UnpackTagsFromCoreFileSegment(
      CoreReaderFn reader, lldb::addr_t tag_segment_virtual_address,
      lldb::addr_t tag_segment_data_address, lldb::addr_t addr,
      size_t len) const = 0;

  // Pack uncompressed tags into their storage format (e.g. for gdb QMemTags).
  // Checks that each tag is within the expected value range.
  // We do not check the number of tags or range they apply to because
  // it is up to the remote to repeat them as needed.
  virtual llvm::Expected<std::vector<uint8_t>>
  PackTags(const std::vector<lldb::addr_t> &tags) const = 0;

  // Take a set of tags and repeat them as much as needed to cover the given
  // range. We assume that this range has been previously expanded/aligned to
  // granules. (this method is used by lldb-server to implement QMemTags
  // packet handling)
  //
  // If the range is empty, zero tags are returned.
  // If the range is not empty and...
  //   * there are no tags, an error is returned.
  //   * there are fewer tags than granules, the tags are repeated to fill the
  //     range.
  //   * there are more tags than granules, only the tags required to cover
  //     the range are returned.
  //
  // When repeating tags it will not always return a multiple of the original
  // list. For example if your range is 3 granules and your tags are 1 and 2.
  // You will get tags 1, 2 and 1 returned. Rather than getting 1, 2, 1, 2,
  // which would be one too many tags for the range.
  //
  // A single tag will just be repeated as you'd expected. Tag 1 over 3 granules
  // would return 1, 1, 1.
  virtual llvm::Expected<std::vector<lldb::addr_t>>
  RepeatTagsForRange(const std::vector<lldb::addr_t> &tags,
                     TagRange range) const = 0;

  virtual ~MemoryTagManager() = default;
};

} // namespace lldb_private

#endif // LLDB_TARGET_MEMORYTAGMANAGER_H
