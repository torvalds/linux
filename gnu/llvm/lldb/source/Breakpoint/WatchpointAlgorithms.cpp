//===-- WatchpointAlgorithms.cpp ------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Breakpoint/WatchpointAlgorithms.h"
#include "lldb/Breakpoint/WatchpointResource.h"
#include "lldb/Target/Process.h"
#include "lldb/Utility/ArchSpec.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"

#include <algorithm>
#include <utility>
#include <vector>

using namespace lldb;
using namespace lldb_private;

std::vector<WatchpointResourceSP>
WatchpointAlgorithms::AtomizeWatchpointRequest(
    addr_t addr, size_t size, bool read, bool write,
    WatchpointHardwareFeature supported_features, ArchSpec &arch) {

  std::vector<Region> entries;

  if (supported_features & eWatchpointHardwareArmMASK) {
    entries =
        PowerOf2Watchpoints(addr, size,
                            /*min_byte_size*/ 1,
                            /*max_byte_size*/ INT32_MAX,
                            /*address_byte_size*/ arch.GetAddressByteSize());
  } else {
    // As a fallback, assume we can watch any power-of-2
    // number of bytes up through the size of an address in the target.
    entries =
        PowerOf2Watchpoints(addr, size,
                            /*min_byte_size*/ 1,
                            /*max_byte_size*/ arch.GetAddressByteSize(),
                            /*address_byte_size*/ arch.GetAddressByteSize());
  }

  Log *log = GetLog(LLDBLog::Watchpoints);
  LLDB_LOGV(log, "AtomizeWatchpointRequest user request addr {0:x} size {1}",
            addr, size);
  std::vector<WatchpointResourceSP> resources;
  for (Region &ent : entries) {
    LLDB_LOGV(log, "AtomizeWatchpointRequest creating resource {0:x} size {1}",
              ent.addr, ent.size);
    WatchpointResourceSP wp_res_sp =
        std::make_shared<WatchpointResource>(ent.addr, ent.size, read, write);
    resources.push_back(wp_res_sp);
  }

  return resources;
}

// This should be `std::bit_ceil(aligned_size)` but
// that requires C++20.
// Calculates the smallest integral power of two that is not smaller than x.
static uint64_t bit_ceil(uint64_t input) {
  if (input <= 1 || llvm::popcount(input) == 1)
    return input;

  return 1ULL << (64 - llvm::countl_zero(input));
}

/// Convert a user's watchpoint request (\a user_addr and \a user_size)
/// into hardware watchpoints, for a target that can watch a power-of-2
/// region of memory (1, 2, 4, 8, etc), aligned to that same power-of-2
/// memory address.
///
/// If a user asks to watch 4 bytes at address 0x1002 (0x1002-0x1005
/// inclusive) we can implement this with two 2-byte watchpoints
/// (0x1002 and 0x1004) or with an 8-byte watchpoint at 0x1000.
/// A 4-byte watchpoint at 0x1002 would not be properly 4 byte aligned.
///
/// If a user asks to watch 16 bytes at 0x1000, and this target supports
/// 8-byte watchpoints, we can implement this with two 8-byte watchpoints
/// at 0x1000 and 0x1008.
std::vector<WatchpointAlgorithms::Region>
WatchpointAlgorithms::PowerOf2Watchpoints(addr_t user_addr, size_t user_size,
                                          size_t min_byte_size,
                                          size_t max_byte_size,
                                          uint32_t address_byte_size) {

  Log *log = GetLog(LLDBLog::Watchpoints);
  LLDB_LOGV(log,
            "AtomizeWatchpointRequest user request addr {0:x} size {1} "
            "min_byte_size {2}, max_byte_size {3}, address_byte_size {4}",
            user_addr, user_size, min_byte_size, max_byte_size,
            address_byte_size);

  // Can't watch zero bytes.
  if (user_size == 0)
    return {};

  size_t aligned_size = std::max(user_size, min_byte_size);
  /// Round up \a user_size to the next power-of-2 size
  /// user_size == 8   -> aligned_size == 8
  /// user_size == 9   -> aligned_size == 16
  aligned_size = bit_ceil(aligned_size);

  addr_t aligned_start = user_addr & ~(aligned_size - 1);

  // Does this power-of-2 memory range, aligned to power-of-2 that the
  // hardware can watch, completely cover the requested region.
  if (aligned_size <= max_byte_size &&
      aligned_start + aligned_size >= user_addr + user_size)
    return {{aligned_start, aligned_size}};

  // If the maximum region we can watch is larger than the aligned
  // size, try increasing the region size by one power of 2 and see
  // if aligning to that amount can cover the requested region.
  //
  // Increasing the aligned_size repeatedly instead of splitting the
  // watchpoint can result in us watching large regions of memory
  // unintentionally when we could use small two watchpoints.  e.g.
  //    user_addr 0x3ff8 user_size 32
  // can be watched with four 8-byte watchpoints or if it's done with one
  // MASK watchpoint, it would need to be a 32KB watchpoint (a 16KB
  // watchpoint at 0x0 only covers 0x0000-0x4000).  A user request
  // at the end of a power-of-2 region can lead to these undesirably
  // large watchpoints and many false positive hits to ignore.
  if (max_byte_size >= (aligned_size << 1)) {
    aligned_size <<= 1;
    aligned_start = user_addr & ~(aligned_size - 1);
    if (aligned_size <= max_byte_size &&
        aligned_start + aligned_size >= user_addr + user_size)
      return {{aligned_start, aligned_size}};

    // Go back to our original aligned size, to try the multiple
    // watchpoint approach.
    aligned_size >>= 1;
  }

  // We need to split the user's watchpoint into two or more watchpoints
  // that can be monitored by hardware, because of alignment and/or size
  // reasons.
  aligned_size = std::min(aligned_size, max_byte_size);
  aligned_start = user_addr & ~(aligned_size - 1);

  std::vector<Region> result;
  addr_t current_address = aligned_start;
  const addr_t user_end_address = user_addr + user_size;
  while (current_address + aligned_size < user_end_address) {
    result.push_back({current_address, aligned_size});
    current_address += aligned_size;
  }

  if (current_address < user_end_address)
    result.push_back({current_address, aligned_size});

  return result;
}
