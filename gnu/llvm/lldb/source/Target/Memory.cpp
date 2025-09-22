//===-- Memory.cpp --------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Target/Memory.h"
#include "lldb/Target/Process.h"
#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/RangeMap.h"
#include "lldb/Utility/State.h"

#include <cinttypes>
#include <memory>

using namespace lldb;
using namespace lldb_private;

// MemoryCache constructor
MemoryCache::MemoryCache(Process &process)
    : m_mutex(), m_L1_cache(), m_L2_cache(), m_invalid_ranges(),
      m_process(process),
      m_L2_cache_line_byte_size(process.GetMemoryCacheLineSize()) {}

// Destructor
MemoryCache::~MemoryCache() = default;

void MemoryCache::Clear(bool clear_invalid_ranges) {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  m_L1_cache.clear();
  m_L2_cache.clear();
  if (clear_invalid_ranges)
    m_invalid_ranges.Clear();
  m_L2_cache_line_byte_size = m_process.GetMemoryCacheLineSize();
}

void MemoryCache::AddL1CacheData(lldb::addr_t addr, const void *src,
                                 size_t src_len) {
  AddL1CacheData(
      addr, DataBufferSP(new DataBufferHeap(DataBufferHeap(src, src_len))));
}

void MemoryCache::AddL1CacheData(lldb::addr_t addr,
                                 const DataBufferSP &data_buffer_sp) {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  m_L1_cache[addr] = data_buffer_sp;
}

void MemoryCache::Flush(addr_t addr, size_t size) {
  if (size == 0)
    return;

  std::lock_guard<std::recursive_mutex> guard(m_mutex);

  // Erase any blocks from the L1 cache that intersect with the flush range
  if (!m_L1_cache.empty()) {
    AddrRange flush_range(addr, size);
    BlockMap::iterator pos = m_L1_cache.upper_bound(addr);
    if (pos != m_L1_cache.begin()) {
      --pos;
    }
    while (pos != m_L1_cache.end()) {
      AddrRange chunk_range(pos->first, pos->second->GetByteSize());
      if (!chunk_range.DoesIntersect(flush_range))
        break;
      pos = m_L1_cache.erase(pos);
    }
  }

  if (!m_L2_cache.empty()) {
    const uint32_t cache_line_byte_size = m_L2_cache_line_byte_size;
    const addr_t end_addr = (addr + size - 1);
    const addr_t first_cache_line_addr = addr - (addr % cache_line_byte_size);
    const addr_t last_cache_line_addr =
        end_addr - (end_addr % cache_line_byte_size);
    // Watch for overflow where size will cause us to go off the end of the
    // 64 bit address space
    uint32_t num_cache_lines;
    if (last_cache_line_addr >= first_cache_line_addr)
      num_cache_lines = ((last_cache_line_addr - first_cache_line_addr) /
                         cache_line_byte_size) +
                        1;
    else
      num_cache_lines =
          (UINT64_MAX - first_cache_line_addr + 1) / cache_line_byte_size;

    uint32_t cache_idx = 0;
    for (addr_t curr_addr = first_cache_line_addr; cache_idx < num_cache_lines;
         curr_addr += cache_line_byte_size, ++cache_idx) {
      BlockMap::iterator pos = m_L2_cache.find(curr_addr);
      if (pos != m_L2_cache.end())
        m_L2_cache.erase(pos);
    }
  }
}

void MemoryCache::AddInvalidRange(lldb::addr_t base_addr,
                                  lldb::addr_t byte_size) {
  if (byte_size > 0) {
    std::lock_guard<std::recursive_mutex> guard(m_mutex);
    InvalidRanges::Entry range(base_addr, byte_size);
    m_invalid_ranges.Append(range);
    m_invalid_ranges.Sort();
  }
}

bool MemoryCache::RemoveInvalidRange(lldb::addr_t base_addr,
                                     lldb::addr_t byte_size) {
  if (byte_size > 0) {
    std::lock_guard<std::recursive_mutex> guard(m_mutex);
    const uint32_t idx = m_invalid_ranges.FindEntryIndexThatContains(base_addr);
    if (idx != UINT32_MAX) {
      const InvalidRanges::Entry *entry = m_invalid_ranges.GetEntryAtIndex(idx);
      if (entry->GetRangeBase() == base_addr &&
          entry->GetByteSize() == byte_size)
        return m_invalid_ranges.RemoveEntryAtIndex(idx);
    }
  }
  return false;
}

lldb::DataBufferSP MemoryCache::GetL2CacheLine(lldb::addr_t line_base_addr,
                                               Status &error) {
  // This function assumes that the address given is aligned correctly.
  assert((line_base_addr % m_L2_cache_line_byte_size) == 0);

  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  auto pos = m_L2_cache.find(line_base_addr);
  if (pos != m_L2_cache.end())
    return pos->second;

  auto data_buffer_heap_sp =
      std::make_shared<DataBufferHeap>(m_L2_cache_line_byte_size, 0);
  size_t process_bytes_read = m_process.ReadMemoryFromInferior(
      line_base_addr, data_buffer_heap_sp->GetBytes(),
      data_buffer_heap_sp->GetByteSize(), error);

  // If we failed a read, not much we can do.
  if (process_bytes_read == 0)
    return lldb::DataBufferSP();

  // If we didn't get a complete read, we can still cache what we did get.
  if (process_bytes_read < m_L2_cache_line_byte_size)
    data_buffer_heap_sp->SetByteSize(process_bytes_read);

  m_L2_cache[line_base_addr] = data_buffer_heap_sp;
  return data_buffer_heap_sp;
}

size_t MemoryCache::Read(addr_t addr, void *dst, size_t dst_len,
                         Status &error) {
  if (!dst || dst_len == 0)
    return 0;

  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  // FIXME: We should do a more thorough check to make sure that we're not
  // overlapping with any invalid ranges (e.g. Read 0x100 - 0x200 but there's an
  // invalid range 0x180 - 0x280). `FindEntryThatContains` has an implementation
  // that takes a range, but it only checks to see if the argument is contained
  // by an existing invalid range. It cannot check if the argument contains
  // invalid ranges and cannot check for overlaps.
  if (m_invalid_ranges.FindEntryThatContains(addr)) {
    error.SetErrorStringWithFormat("memory read failed for 0x%" PRIx64, addr);
    return 0;
  }

  // Check the L1 cache for a range that contains the entire memory read.
  // L1 cache contains chunks of memory that are not required to be the size of
  // an L2 cache line. We avoid trying to do partial reads from the L1 cache to
  // simplify the implementation.
  if (!m_L1_cache.empty()) {
    AddrRange read_range(addr, dst_len);
    BlockMap::iterator pos = m_L1_cache.upper_bound(addr);
    if (pos != m_L1_cache.begin()) {
      --pos;
    }
    AddrRange chunk_range(pos->first, pos->second->GetByteSize());
    if (chunk_range.Contains(read_range)) {
      memcpy(dst, pos->second->GetBytes() + (addr - chunk_range.GetRangeBase()),
             dst_len);
      return dst_len;
    }
  }

  // If the size of the read is greater than the size of an L2 cache line, we'll
  // just read from the inferior. If that read is successful, we'll cache what
  // we read in the L1 cache for future use.
  if (dst_len > m_L2_cache_line_byte_size) {
    size_t bytes_read =
        m_process.ReadMemoryFromInferior(addr, dst, dst_len, error);
    if (bytes_read > 0)
      AddL1CacheData(addr, dst, bytes_read);
    return bytes_read;
  }

  // If the size of the read fits inside one L2 cache line, we'll try reading
  // from the L2 cache. Note that if the range of memory we're reading sits
  // between two contiguous cache lines, we'll touch two cache lines instead of
  // just one.

  // We're going to have all of our loads and reads be cache line aligned.
  addr_t cache_line_offset = addr % m_L2_cache_line_byte_size;
  addr_t cache_line_base_addr = addr - cache_line_offset;
  DataBufferSP first_cache_line = GetL2CacheLine(cache_line_base_addr, error);
  // If we get nothing, then the read to the inferior likely failed. Nothing to
  // do here.
  if (!first_cache_line)
    return 0;

  // If the cache line was not filled out completely and the offset is greater
  // than what we have available, we can't do anything further here.
  if (cache_line_offset >= first_cache_line->GetByteSize())
    return 0;

  uint8_t *dst_buf = (uint8_t *)dst;
  size_t bytes_left = dst_len;
  size_t read_size = first_cache_line->GetByteSize() - cache_line_offset;
  if (read_size > bytes_left)
    read_size = bytes_left;

  memcpy(dst_buf + dst_len - bytes_left,
         first_cache_line->GetBytes() + cache_line_offset, read_size);
  bytes_left -= read_size;

  // If the cache line was not filled out completely and we still have data to
  // read, we can't do anything further.
  if (first_cache_line->GetByteSize() < m_L2_cache_line_byte_size &&
      bytes_left > 0)
    return dst_len - bytes_left;

  // We'll hit this scenario if our read straddles two cache lines.
  if (bytes_left > 0) {
    cache_line_base_addr += m_L2_cache_line_byte_size;

    // FIXME: Until we are able to more thoroughly check for invalid ranges, we
    // will have to check the second line to see if it is in an invalid range as
    // well. See the check near the beginning of the function for more details.
    if (m_invalid_ranges.FindEntryThatContains(cache_line_base_addr)) {
      error.SetErrorStringWithFormat("memory read failed for 0x%" PRIx64,
                                     cache_line_base_addr);
      return dst_len - bytes_left;
    }

    DataBufferSP second_cache_line =
        GetL2CacheLine(cache_line_base_addr, error);
    if (!second_cache_line)
      return dst_len - bytes_left;

    read_size = bytes_left;
    if (read_size > second_cache_line->GetByteSize())
      read_size = second_cache_line->GetByteSize();

    memcpy(dst_buf + dst_len - bytes_left, second_cache_line->GetBytes(),
           read_size);
    bytes_left -= read_size;

    return dst_len - bytes_left;
  }

  return dst_len;
}

AllocatedBlock::AllocatedBlock(lldb::addr_t addr, uint32_t byte_size,
                               uint32_t permissions, uint32_t chunk_size)
    : m_range(addr, byte_size), m_permissions(permissions),
      m_chunk_size(chunk_size)
{
  // The entire address range is free to start with.
  m_free_blocks.Append(m_range);
  assert(byte_size > chunk_size);
}

AllocatedBlock::~AllocatedBlock() = default;

lldb::addr_t AllocatedBlock::ReserveBlock(uint32_t size) {
  // We must return something valid for zero bytes.
  if (size == 0)
    size = 1;
  Log *log = GetLog(LLDBLog::Process);

  const size_t free_count = m_free_blocks.GetSize();
  for (size_t i=0; i<free_count; ++i)
  {
    auto &free_block = m_free_blocks.GetEntryRef(i);
    const lldb::addr_t range_size = free_block.GetByteSize();
    if (range_size >= size)
    {
      // We found a free block that is big enough for our data. Figure out how
      // many chunks we will need and calculate the resulting block size we
      // will reserve.
      addr_t addr = free_block.GetRangeBase();
      size_t num_chunks = CalculateChunksNeededForSize(size);
      lldb::addr_t block_size = num_chunks * m_chunk_size;
      lldb::addr_t bytes_left = range_size - block_size;
      if (bytes_left == 0)
      {
        // The newly allocated block will take all of the bytes in this
        // available block, so we can just add it to the allocated ranges and
        // remove the range from the free ranges.
        m_reserved_blocks.Insert(free_block, false);
        m_free_blocks.RemoveEntryAtIndex(i);
      }
      else
      {
        // Make the new allocated range and add it to the allocated ranges.
        Range<lldb::addr_t, uint32_t> reserved_block(free_block);
        reserved_block.SetByteSize(block_size);
        // Insert the reserved range and don't combine it with other blocks in
        // the reserved blocks list.
        m_reserved_blocks.Insert(reserved_block, false);
        // Adjust the free range in place since we won't change the sorted
        // ordering of the m_free_blocks list.
        free_block.SetRangeBase(reserved_block.GetRangeEnd());
        free_block.SetByteSize(bytes_left);
      }
      LLDB_LOGV(log, "({0}) (size = {1} ({1:x})) => {2:x}", this, size, addr);
      return addr;
    }
  }

  LLDB_LOGV(log, "({0}) (size = {1} ({1:x})) => {2:x}", this, size,
            LLDB_INVALID_ADDRESS);
  return LLDB_INVALID_ADDRESS;
}

bool AllocatedBlock::FreeBlock(addr_t addr) {
  bool success = false;
  auto entry_idx = m_reserved_blocks.FindEntryIndexThatContains(addr);
  if (entry_idx != UINT32_MAX)
  {
    m_free_blocks.Insert(m_reserved_blocks.GetEntryRef(entry_idx), true);
    m_reserved_blocks.RemoveEntryAtIndex(entry_idx);
    success = true;
  }
  Log *log = GetLog(LLDBLog::Process);
  LLDB_LOGV(log, "({0}) (addr = {1:x}) => {2}", this, addr, success);
  return success;
}

AllocatedMemoryCache::AllocatedMemoryCache(Process &process)
    : m_process(process), m_mutex(), m_memory_map() {}

AllocatedMemoryCache::~AllocatedMemoryCache() = default;

void AllocatedMemoryCache::Clear(bool deallocate_memory) {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  if (m_process.IsAlive() && deallocate_memory) {
    PermissionsToBlockMap::iterator pos, end = m_memory_map.end();
    for (pos = m_memory_map.begin(); pos != end; ++pos)
      m_process.DoDeallocateMemory(pos->second->GetBaseAddress());
  }
  m_memory_map.clear();
}

AllocatedMemoryCache::AllocatedBlockSP
AllocatedMemoryCache::AllocatePage(uint32_t byte_size, uint32_t permissions,
                                   uint32_t chunk_size, Status &error) {
  AllocatedBlockSP block_sp;
  const size_t page_size = 4096;
  const size_t num_pages = (byte_size + page_size - 1) / page_size;
  const size_t page_byte_size = num_pages * page_size;

  addr_t addr = m_process.DoAllocateMemory(page_byte_size, permissions, error);

  Log *log = GetLog(LLDBLog::Process);
  if (log) {
    LLDB_LOGF(log,
              "Process::DoAllocateMemory (byte_size = 0x%8.8" PRIx32
              ", permissions = %s) => 0x%16.16" PRIx64,
              (uint32_t)page_byte_size, GetPermissionsAsCString(permissions),
              (uint64_t)addr);
  }

  if (addr != LLDB_INVALID_ADDRESS) {
    block_sp = std::make_shared<AllocatedBlock>(addr, page_byte_size,
                                                permissions, chunk_size);
    m_memory_map.insert(std::make_pair(permissions, block_sp));
  }
  return block_sp;
}

lldb::addr_t AllocatedMemoryCache::AllocateMemory(size_t byte_size,
                                                  uint32_t permissions,
                                                  Status &error) {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);

  addr_t addr = LLDB_INVALID_ADDRESS;
  std::pair<PermissionsToBlockMap::iterator, PermissionsToBlockMap::iterator>
      range = m_memory_map.equal_range(permissions);

  for (PermissionsToBlockMap::iterator pos = range.first; pos != range.second;
       ++pos) {
    addr = (*pos).second->ReserveBlock(byte_size);
    if (addr != LLDB_INVALID_ADDRESS)
      break;
  }

  if (addr == LLDB_INVALID_ADDRESS) {
    AllocatedBlockSP block_sp(AllocatePage(byte_size, permissions, 16, error));

    if (block_sp)
      addr = block_sp->ReserveBlock(byte_size);
  }
  Log *log = GetLog(LLDBLog::Process);
  LLDB_LOGF(log,
            "AllocatedMemoryCache::AllocateMemory (byte_size = 0x%8.8" PRIx32
            ", permissions = %s) => 0x%16.16" PRIx64,
            (uint32_t)byte_size, GetPermissionsAsCString(permissions),
            (uint64_t)addr);
  return addr;
}

bool AllocatedMemoryCache::DeallocateMemory(lldb::addr_t addr) {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);

  PermissionsToBlockMap::iterator pos, end = m_memory_map.end();
  bool success = false;
  for (pos = m_memory_map.begin(); pos != end; ++pos) {
    if (pos->second->Contains(addr)) {
      success = pos->second->FreeBlock(addr);
      break;
    }
  }
  Log *log = GetLog(LLDBLog::Process);
  LLDB_LOGF(log,
            "AllocatedMemoryCache::DeallocateMemory (addr = 0x%16.16" PRIx64
            ") => %i",
            (uint64_t)addr, success);
  return success;
}
