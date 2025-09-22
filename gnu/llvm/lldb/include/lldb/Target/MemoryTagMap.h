//===-- MemoryTagMap.h ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TARGET_MEMORYTAGMAP_H
#define LLDB_TARGET_MEMORYTAGMAP_H

#include "lldb/Target/MemoryTagManager.h"
#include "lldb/lldb-private.h"
#include <map>
#include <optional>

namespace lldb_private {

/// MemoryTagMap provides a way to give a sparse read result
/// when reading memory tags for a range. This is useful when
/// you want to annotate some large memory dump that might include
/// tagged memory but you don't know that it is all tagged.
class MemoryTagMap {
public:
  /// Init an empty tag map
  ///
  /// \param [in] manager
  ///     Non-null pointer to a memory tag manager.
  MemoryTagMap(const MemoryTagManager *manager);

  /// Insert tags into the map starting from addr.
  ///
  /// \param [in] addr
  ///     Start address of the range to insert tags for.
  ///     This address should be granule aligned and have had
  ///     any non address bits removed.
  ///     (ideally you would use the base of the range you used
  ///     to read the tags in the first place)
  ///
  /// \param [in] tags
  ///     Vector of tags to insert. The first tag will be inserted
  ///     at addr, the next at addr+granule size and so on until
  ///     all tags have been inserted.
  void InsertTags(lldb::addr_t addr, const std::vector<lldb::addr_t> tags);

  bool Empty() const;

  /// Lookup memory tags for a range of memory from addr to addr+len.
  ///
  /// \param [in] addr
  ///    The start of the range. This may include non address bits and
  ///    does not have to be granule aligned.
  ///
  /// \param [in] len
  ///    The length in bytes of the range to read tags for. This does
  ///    not need to be multiple of the granule size.
  ///
  /// \return
  ///    A vector containing the tags found for the granules in the
  ///    range. (which is the result of granule aligning the given range)
  ///
  ///    Each item in the vector is an optional tag. Meaning that if
  ///    it is valid then the granule had a tag and if not, it didn't.
  ///
  ///    If the range had no tags at all, the vector will be empty.
  ///    If some of the range was tagged it will have items and some
  ///    of them may be std::nullopt.
  ///    (this saves the caller checking whether all items are std::nullopt)
  std::vector<std::optional<lldb::addr_t>> GetTags(lldb::addr_t addr,
                                                   size_t len) const;

private:
  /// Lookup the tag for address
  ///
  /// \param [in] address
  ///     The address to lookup a tag for. This should be aligned
  ///     to a granule boundary.
  ///
  /// \return
  ///     The tag for the granule that address refers to, or std::nullopt
  ///     if it has no memory tag.
  std::optional<lldb::addr_t> GetTag(lldb::addr_t addr) const;

  // A map of granule aligned addresses to their memory tag
  std::map<lldb::addr_t, lldb::addr_t> m_addr_to_tag;

  // Memory tag manager used to align addresses and get granule size.
  // Ideally this would be a const& but only certain architectures will
  // have a memory tag manager class to provide here. So for a method
  // returning a MemoryTagMap, std::optional<MemoryTagMap> allows it to handle
  // architectures without memory tagging. Optionals cannot hold references
  // so we go with a pointer that we assume will be not be null.
  const MemoryTagManager *m_manager;
};

} // namespace lldb_private

#endif // LLDB_TARGET_MEMORYTAGMAP_H
