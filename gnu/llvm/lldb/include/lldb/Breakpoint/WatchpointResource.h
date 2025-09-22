//===-- WatchpointResource.h ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_BREAKPOINT_WATCHPOINTRESOURCE_H
#define LLDB_BREAKPOINT_WATCHPOINTRESOURCE_H

#include "lldb/Utility/Iterable.h"
#include "lldb/lldb-public.h"

#include <mutex>
#include <vector>

namespace lldb_private {

class WatchpointResource
    : public std::enable_shared_from_this<WatchpointResource> {

public:
  WatchpointResource(lldb::addr_t addr, size_t size, bool read, bool write);

  ~WatchpointResource();

  typedef lldb::wp_resource_id_t SiteID;
  typedef lldb::watch_id_t ConstituentID;

  lldb::addr_t GetLoadAddress() const;

  size_t GetByteSize() const;

  bool WatchpointResourceRead() const;

  bool WatchpointResourceWrite() const;

  void SetType(bool read, bool write);

  typedef std::vector<lldb::WatchpointSP> WatchpointCollection;
  typedef LockingAdaptedIterable<WatchpointCollection, lldb::WatchpointSP,
                                 vector_adapter, std::mutex>
      WatchpointIterable;

  /// Iterate over the watchpoint constituents for this resource
  ///
  /// \return
  ///     An Iterable object which can be used to loop over the watchpoints
  ///     that are constituents of this resource.
  WatchpointIterable Constituents() {
    return WatchpointIterable(m_constituents, m_constituents_mutex);
  }

  /// Enquires of the atchpoints that produced this watchpoint resource
  /// whether we should stop at this location.
  ///
  /// \param[in] context
  ///    This contains the information about this stop.
  ///
  /// \return
  ///    \b true if we should stop, \b false otherwise.
  bool ShouldStop(StoppointCallbackContext *context);

  /// Standard Dump method
  void Dump(Stream *s) const;

  /// The "Constituents" are the watchpoints that share this resource.
  /// The method adds the \a constituent to this resource's constituent list.
  ///
  /// \param[in] constituent
  ///    \a constituent is the Wachpoint to add.
  void AddConstituent(const lldb::WatchpointSP &constituent);

  /// The method removes the constituent at \a constituent from this watchpoint
  /// resource.
  void RemoveConstituent(lldb::WatchpointSP &constituent);

  /// This method returns the number of Watchpoints currently using
  /// watchpoint resource.
  ///
  /// \return
  ///    The number of constituents.
  size_t GetNumberOfConstituents();

  /// This method returns the Watchpoint at index \a index using this
  /// Resource.  The constituents are listed ordinally from 0 to
  /// GetNumberOfConstituents() - 1 so you can use this method to iterate over
  /// the constituents.
  ///
  /// \param[in] idx
  ///     The index in the list of constituents for which you wish the
  ///     constituent location.
  ///
  /// \return
  ///    The Watchpoint at that index.
  lldb::WatchpointSP GetConstituentAtIndex(size_t idx);

  /// Check if the constituents includes a watchpoint.
  ///
  /// \param[in] wp_sp
  ///     The WatchpointSP to search for.
  ///
  /// \result
  ///     true if this resource's constituents includes the watchpoint.
  bool ConstituentsContains(const lldb::WatchpointSP &wp_sp);

  /// Check if the constituents includes a watchpoint.
  ///
  /// \param[in] wp
  ///     The Watchpoint to search for.
  ///
  /// \result
  ///     true if this resource's constituents includes the watchpoint.
  bool ConstituentsContains(const lldb_private::Watchpoint *wp);

  /// This method copies the watchpoint resource's constituents into a new
  /// collection. It does this while the constituents mutex is locked.
  ///
  /// \return
  ///    A copy of the Watchpoints which own this resource.
  WatchpointCollection CopyConstituentsList();

  lldb::wp_resource_id_t GetID() const;

  bool Contains(lldb::addr_t addr);

protected:
  // The StopInfoWatchpoint knows when it is processing a hit for a thread for
  // a site, so let it be the one to manage setting the location hit count once
  // and only once.
  friend class StopInfoWatchpoint;

  void BumpHitCounts();

private:
  static lldb::wp_resource_id_t GetNextID();

  lldb::wp_resource_id_t m_id;

  // Start address & size aligned & expanded to be a valid watchpoint
  // memory granule on this target.
  lldb::addr_t m_addr;
  size_t m_size;

  bool m_watch_read;
  bool m_watch_write;

  /// The Watchpoints which own this resource.
  WatchpointCollection m_constituents;

  /// This mutex protects the constituents collection.
  std::mutex m_constituents_mutex;

  WatchpointResource(const WatchpointResource &) = delete;
  const WatchpointResource &operator=(const WatchpointResource &) = delete;
};

} // namespace lldb_private

#endif // LLDB_BREAKPOINT_WATCHPOINTRESOURCE_H
