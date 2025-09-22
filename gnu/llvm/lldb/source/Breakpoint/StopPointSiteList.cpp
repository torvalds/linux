//===-- StopPointSiteList.cpp ---------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Breakpoint/StopPointSiteList.h"
#include "lldb/Breakpoint/BreakpointSite.h"
#include "lldb/Breakpoint/WatchpointResource.h"

#include "lldb/Utility/Stream.h"
#include <algorithm>

using namespace lldb;
using namespace lldb_private;

// This method is only defined when we're specializing for
// BreakpointSite / BreakpointLocation / Breakpoint.
// Watchpoints don't have a similar structure, they are
// WatchpointResource / Watchpoint

template <>
bool StopPointSiteList<BreakpointSite>::StopPointSiteContainsBreakpoint(
    typename BreakpointSite::SiteID site_id, lldb::break_id_t bp_id) {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  typename collection::const_iterator pos = GetIDConstIterator(site_id);
  if (pos != m_site_list.end())
    return pos->second->IsBreakpointAtThisSite(bp_id);

  return false;
}

namespace lldb_private {
template class StopPointSiteList<BreakpointSite>;
} // namespace lldb_private
