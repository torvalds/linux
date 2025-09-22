//===-- StopPointSiteList.h -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_BREAKPOINT_STOPPOINTSITELIST_H
#define LLDB_BREAKPOINT_STOPPOINTSITELIST_H

#include <functional>
#include <map>
#include <mutex>

#include <lldb/Breakpoint/BreakpointSite.h>
#include <lldb/Utility/Iterable.h>
#include <lldb/Utility/Stream.h>

namespace lldb_private {

template <typename StopPointSite> class StopPointSiteList {
  // At present Process directly accesses the map of StopPointSites so it can
  // do quick lookups into the map (using GetMap).
  // FIXME: Find a better interface for this.
  friend class Process;

public:
  using StopPointSiteSP = std::shared_ptr<StopPointSite>;

  /// Add a site to the list.
  ///
  /// \param[in] site_sp
  ///    A shared pointer to a site being added to the list.
  ///
  /// \return
  ///    The ID of the site in the list.
  typename StopPointSite::SiteID Add(const StopPointSiteSP &site_sp) {
    lldb::addr_t site_load_addr = site_sp->GetLoadAddress();
    std::lock_guard<std::recursive_mutex> guard(m_mutex);
    typename collection::iterator iter = m_site_list.find(site_load_addr);

    // Add site to the list.  However, if the element already exists in
    // the list, then we don't add it, and return InvalidSiteID.
    if (iter == m_site_list.end()) {
      m_site_list[site_load_addr] = site_sp;
      return site_sp->GetID();
    } else {
      return UINT32_MAX;
    }
  }

  /// Standard Dump routine, doesn't do anything at present.
  /// \param[in] s
  ///     Stream into which to dump the description.
  void Dump(Stream *s) const {
    s->Printf("%p: ", static_cast<const void *>(this));
    s->Printf("StopPointSiteList with %u ConstituentSites:\n",
              (uint32_t)m_site_list.size());
    s->IndentMore();
    typename collection::const_iterator pos;
    typename collection::const_iterator end = m_site_list.end();
    for (pos = m_site_list.begin(); pos != end; ++pos)
      pos->second->Dump(s);
    s->IndentLess();
  }

  /// Returns a shared pointer to the site at address \a addr.
  ///
  /// \param[in] addr
  ///     The address to look for.
  ///
  /// \result
  ///     A shared pointer to the site. Nullptr if no site contains
  ///     the address.
  StopPointSiteSP FindByAddress(lldb::addr_t addr) {
    StopPointSiteSP found_sp;
    std::lock_guard<std::recursive_mutex> guard(m_mutex);
    typename collection::iterator iter = m_site_list.find(addr);
    if (iter != m_site_list.end())
      found_sp = iter->second;
    return found_sp;
  }

  /// Returns a shared pointer to the site with id \a site_id.
  ///
  /// \param[in] site_id
  ///   The site ID to seek for.
  ///
  /// \result
  ///   A shared pointer to the site. Nullptr if no matching site.
  StopPointSiteSP FindByID(typename StopPointSite::SiteID site_id) {
    std::lock_guard<std::recursive_mutex> guard(m_mutex);
    StopPointSiteSP stop_sp;
    typename collection::iterator pos = GetIDIterator(site_id);
    if (pos != m_site_list.end())
      stop_sp = pos->second;

    return stop_sp;
  }

  /// Returns a shared pointer to the site with id \a site_id -
  /// const version.
  ///
  /// \param[in] site_id
  ///   The site ID to seek for.
  ///
  /// \result
  ///   A shared pointer to the site. Nullptr if no matching site.
  const StopPointSiteSP FindByID(typename StopPointSite::SiteID site_id) const {
    std::lock_guard<std::recursive_mutex> guard(m_mutex);
    StopPointSiteSP stop_sp;
    typename collection::const_iterator pos = GetIDConstIterator(site_id);
    if (pos != m_site_list.end())
      stop_sp = pos->second;

    return stop_sp;
  }

  /// Returns the site id to the site at address \a addr.
  ///
  /// \param[in] addr
  ///   The address to match.
  ///
  /// \result
  ///   The ID of the site, or LLDB_INVALID_SITE_ID.
  typename StopPointSite::SiteID FindIDByAddress(lldb::addr_t addr) {
    if (StopPointSiteSP site = FindByAddress(addr))
      return site->GetID();
    return UINT32_MAX;
  }

  /// Returns whether the BreakpointSite \a site_id has a BreakpointLocation
  /// that is part of Breakpoint \a bp_id.
  ///
  /// NB this is only defined when StopPointSiteList is specialized for
  /// BreakpointSite's.
  ///
  /// \param[in] site_id
  ///   The site id to query.
  ///
  /// \param[in] bp_id
  ///   The breakpoint id to look for in \a site_id's BreakpointLocations.
  ///
  /// \result
  ///   True if \a site_id exists in the site list AND \a bp_id
  ///   is the breakpoint for one of the BreakpointLocations.
  bool StopPointSiteContainsBreakpoint(typename StopPointSite::SiteID,
                                       lldb::break_id_t bp_id);

  void ForEach(std::function<void(StopPointSite *)> const &callback) {
    std::lock_guard<std::recursive_mutex> guard(m_mutex);
    for (auto pair : m_site_list)
      callback(pair.second.get());
  }

  /// Removes the site given by \a site_id from this list.
  ///
  /// \param[in] site_id
  ///   The site ID to remove.
  ///
  /// \result
  ///   \b true if the site \a site_id was in the list.
  bool Remove(typename StopPointSite::SiteID site_id) {
    std::lock_guard<std::recursive_mutex> guard(m_mutex);
    typename collection::iterator pos = GetIDIterator(site_id); // Predicate
    if (pos != m_site_list.end()) {
      m_site_list.erase(pos);
      return true;
    }
    return false;
  }

  /// Removes the site at address \a addr from this list.
  ///
  /// \param[in] addr
  ///   The address from which to remove a site.
  ///
  /// \result
  ///   \b true if \a addr had a site to remove from the list.
  bool RemoveByAddress(lldb::addr_t addr) {
    std::lock_guard<std::recursive_mutex> guard(m_mutex);
    typename collection::iterator pos = m_site_list.find(addr);
    if (pos != m_site_list.end()) {
      m_site_list.erase(pos);
      return true;
    }
    return false;
  }

  bool FindInRange(lldb::addr_t lower_bound, lldb::addr_t upper_bound,
                   StopPointSiteList &bp_site_list) const {
    if (lower_bound > upper_bound)
      return false;

    std::lock_guard<std::recursive_mutex> guard(m_mutex);
    typename collection::const_iterator lower, upper, pos;
    lower = m_site_list.lower_bound(lower_bound);
    if (lower == m_site_list.end() || (*lower).first >= upper_bound)
      return false;

    // This is one tricky bit.  The site might overlap the bottom end of
    // the range.  So we grab the site prior to the lower bound, and check
    // that that + its byte size isn't in our range.
    if (lower != m_site_list.begin()) {
      typename collection::const_iterator prev_pos = lower;
      prev_pos--;
      const StopPointSiteSP &prev_site = (*prev_pos).second;
      if (prev_site->GetLoadAddress() + prev_site->GetByteSize() > lower_bound)
        bp_site_list.Add(prev_site);
    }

    upper = m_site_list.upper_bound(upper_bound);

    for (pos = lower; pos != upper; pos++)
      bp_site_list.Add((*pos).second);
    return true;
  }

  typedef void (*StopPointSiteSPMapFunc)(StopPointSite &site, void *baton);

  /// Enquires of the site on in this list with ID \a site_id
  /// whether we should stop for the constituent or not.
  ///
  /// \param[in] context
  ///    This contains the information about this stop.
  ///
  /// \param[in] site_id
  ///    This site ID that we hit.
  ///
  /// \return
  ///    \b true if we should stop, \b false otherwise.
  bool ShouldStop(StoppointCallbackContext *context,
                  typename StopPointSite::SiteID site_id) {
    if (StopPointSiteSP site_sp = FindByID(site_id)) {
      // Let the site decide if it should stop here (could not have
      // reached it's target hit count yet, or it could have a callback that
      // decided it shouldn't stop (shared library loads/unloads).
      return site_sp->ShouldStop(context);
    }
    // We should stop here since this site isn't valid anymore or it
    // doesn't exist.
    return true;
  }

  /// Returns the number of elements in the list.
  ///
  /// \result
  ///   The number of elements.
  size_t GetSize() const {
    std::lock_guard<std::recursive_mutex> guard(m_mutex);
    return m_site_list.size();
  }

  bool IsEmpty() const {
    std::lock_guard<std::recursive_mutex> guard(m_mutex);
    return m_site_list.empty();
  }

  std::vector<StopPointSiteSP> Sites() {
    std::vector<StopPointSiteSP> sites;
    std::lock_guard<std::recursive_mutex> guard(m_mutex);
    typename collection::iterator iter = m_site_list.begin();
    while (iter != m_site_list.end()) {
      sites.push_back(iter->second);
      ++iter;
    }

    return sites;
  }

  void Clear() {
    std::lock_guard<std::recursive_mutex> guard(m_mutex);
    m_site_list.clear();
  }

protected:
  typedef std::map<lldb::addr_t, StopPointSiteSP> collection;

  typename collection::iterator
  GetIDIterator(typename StopPointSite::SiteID site_id) {
    std::lock_guard<std::recursive_mutex> guard(m_mutex);
    auto id_matches =
        [site_id](const std::pair<lldb::addr_t, StopPointSiteSP> s) {
          return site_id == s.second->GetID();
        };
    return std::find_if(m_site_list.begin(),
                        m_site_list.end(), // Search full range
                        id_matches);
  }

  typename collection::const_iterator
  GetIDConstIterator(typename StopPointSite::SiteID site_id) const {
    std::lock_guard<std::recursive_mutex> guard(m_mutex);
    auto id_matches =
        [site_id](const std::pair<lldb::addr_t, StopPointSiteSP> s) {
          return site_id == s.second->GetID();
        };
    return std::find_if(m_site_list.begin(),
                        m_site_list.end(), // Search full range
                        id_matches);
  }

  mutable std::recursive_mutex m_mutex;
  collection m_site_list; // The site list.
};

} // namespace lldb_private

#endif // LLDB_BREAKPOINT_STOPPOINTSITELIST_H
