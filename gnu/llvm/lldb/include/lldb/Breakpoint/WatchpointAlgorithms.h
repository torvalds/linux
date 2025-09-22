//===-- WatchpointAlgorithms.h ----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_BREAKPOINT_WATCHPOINTALGORITHMS_H
#define LLDB_BREAKPOINT_WATCHPOINTALGORITHMS_H

#include "lldb/Breakpoint/WatchpointResource.h"
#include "lldb/Utility/ArchSpec.h"
#include "lldb/lldb-private.h"

#include <vector>

namespace lldb_private {

class WatchpointAlgorithms {

public:
  /// Convert a user's watchpoint request into an array of memory
  /// regions, each region watched by one hardware watchpoint register.
  ///
  /// \param[in] addr
  ///     The start address specified by the user.
  ///
  /// \param[in] size
  ///     The number of bytes the user wants to watch.
  ///
  /// \param[in] read
  ///     True if we are watching for read accesses.
  ///
  /// \param[in] write
  ///     True if we are watching for write accesses.
  ///     \a read and \a write may both be true.
  ///     There is no "modify" style for WatchpointResources -
  ///     WatchpointResources are akin to the hardware watchpoint
  ///     registers which are either in terms of read or write.
  ///     "modify" distinction is done at the Watchpoint layer, where
  ///     we check the actual range of bytes the user requested.
  ///
  /// \param[in] supported_features
  ///     The bit flags in this parameter are set depending on which
  ///     WatchpointHardwareFeature enum values the current target supports.
  ///     The eWatchpointHardwareFeatureUnknown bit may be set if we
  ///     don't have specific information about what the remote stub
  ///     can support, and a reasonablec default will be used.
  ///
  /// \param[in] arch
  ///     The ArchSpec of the current Target.
  ///
  /// \return
  ///     A vector of WatchpointResourceSP's, one per hardware watchpoint
  ///     register needed.  We may return more WatchpointResources than the
  ///     target can watch at once; if all resources cannot be set, the
  ///     watchpoint cannot be set.
  static std::vector<lldb::WatchpointResourceSP> AtomizeWatchpointRequest(
      lldb::addr_t addr, size_t size, bool read, bool write,
      WatchpointHardwareFeature supported_features, ArchSpec &arch);

protected:
  struct Region {
    lldb::addr_t addr;
    size_t size;
  };

  /// Convert a user's watchpoint request into an array of Regions,
  /// each of which can be watched by a single hardware watchpoint
  /// that can watch power-of-2 size & aligned memory regions.
  ///
  /// This is the default algorithm if we have no further information;
  /// most watchpoint implementations can be assumed to be able to watch up
  /// to sizeof(void*) regions of memory, in power-of-2 sizes and alignments.
  /// e.g. on a 64-bit target: 1, 2, 4, 8 or bytes with a single hardware
  /// watchpoint register.
  ///
  /// \param[in] user_addr
  ///     The user's start address.
  ///
  /// \param[in] user_size
  ///     The user's specified byte length.
  ///
  /// \param[in] min_byte_size
  ///     The minimum byte size of the range of memory that can be watched
  ///     with one watchpoint register.
  ///     In most cases, this will be 1.  AArch64 MASK watchpoints can
  ///     watch a minimum of 8 bytes (although Byte Address Select watchpoints
  ///     can watch 1 to pointer-size bytes in a pointer-size aligned granule).
  ///
  /// \param[in] max_byte_size
  ///     The maximum byte size supported for one watchpoint on this target.
  ///
  /// \param[in] address_byte_size
  ///     The address byte size on this target.
  static std::vector<Region> PowerOf2Watchpoints(lldb::addr_t user_addr,
                                                 size_t user_size,
                                                 size_t min_byte_size,
                                                 size_t max_byte_size,
                                                 uint32_t address_byte_size);
};

} // namespace lldb_private

#endif // LLDB_BREAKPOINT_WATCHPOINTALGORITHMS_H
