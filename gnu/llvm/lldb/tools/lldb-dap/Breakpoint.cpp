//===-- Breakpoint.cpp ----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Breakpoint.h"
#include "DAP.h"
#include "JSONUtils.h"
#include "llvm/ADT/StringExtras.h"

using namespace lldb_dap;

void Breakpoint::SetCondition() { bp.SetCondition(condition.c_str()); }

void Breakpoint::SetHitCondition() {
  uint64_t hitCount = 0;
  if (llvm::to_integer(hitCondition, hitCount))
    bp.SetIgnoreCount(hitCount - 1);
}

void Breakpoint::CreateJsonObject(llvm::json::Object &object) {
  // Each breakpoint location is treated as a separate breakpoint for VS code.
  // They don't have the notion of a single breakpoint with multiple locations.
  if (!bp.IsValid())
    return;
  object.try_emplace("verified", bp.GetNumResolvedLocations() > 0);
  object.try_emplace("id", bp.GetID());
  // VS Code DAP doesn't currently allow one breakpoint to have multiple
  // locations so we just report the first one. If we report all locations
  // then the IDE starts showing the wrong line numbers and locations for
  // other source file and line breakpoints in the same file.

  // Below we search for the first resolved location in a breakpoint and report
  // this as the breakpoint location since it will have a complete location
  // that is at least loaded in the current process.
  lldb::SBBreakpointLocation bp_loc;
  const auto num_locs = bp.GetNumLocations();
  for (size_t i = 0; i < num_locs; ++i) {
    bp_loc = bp.GetLocationAtIndex(i);
    if (bp_loc.IsResolved())
      break;
  }
  // If not locations are resolved, use the first location.
  if (!bp_loc.IsResolved())
    bp_loc = bp.GetLocationAtIndex(0);
  auto bp_addr = bp_loc.GetAddress();

  if (bp_addr.IsValid()) {
    std::string formatted_addr =
        "0x" + llvm::utohexstr(bp_addr.GetLoadAddress(g_dap.target));
    object.try_emplace("instructionReference", formatted_addr);
    auto line_entry = bp_addr.GetLineEntry();
    const auto line = line_entry.GetLine();
    if (line != UINT32_MAX)
      object.try_emplace("line", line);
    const auto column = line_entry.GetColumn();
    if (column != 0)
      object.try_emplace("column", column);
    object.try_emplace("source", CreateSource(line_entry));
  }
}

bool Breakpoint::MatchesName(const char *name) { return bp.MatchesName(name); }

void Breakpoint::SetBreakpoint() {
  // See comments in BreakpointBase::GetBreakpointLabel() for details of why
  // we add a label to our breakpoints.
  bp.AddName(GetBreakpointLabel());
  if (!condition.empty())
    SetCondition();
  if (!hitCondition.empty())
    SetHitCondition();
}
