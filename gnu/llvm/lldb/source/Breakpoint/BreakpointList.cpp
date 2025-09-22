//===-- BreakpointList.cpp ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Breakpoint/BreakpointList.h"

#include "lldb/Target/Target.h"

#include "llvm/Support/Errc.h"

using namespace lldb;
using namespace lldb_private;

static void NotifyChange(const BreakpointSP &bp, BreakpointEventType event) {
  Target &target = bp->GetTarget();
  if (target.EventTypeHasListeners(Target::eBroadcastBitBreakpointChanged)) {
    auto event_data_sp =
        std::make_shared<Breakpoint::BreakpointEventData>(event, bp);
    target.BroadcastEvent(Target::eBroadcastBitBreakpointChanged,
                          event_data_sp);
  }
}

BreakpointList::BreakpointList(bool is_internal)
    : m_next_break_id(0), m_is_internal(is_internal) {}

BreakpointList::~BreakpointList() = default;

break_id_t BreakpointList::Add(BreakpointSP &bp_sp, bool notify) {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);

  // Internal breakpoint IDs are negative, normal ones are positive
  bp_sp->SetID(m_is_internal ? --m_next_break_id : ++m_next_break_id);

  m_breakpoints.push_back(bp_sp);

  if (notify)
    NotifyChange(bp_sp, eBreakpointEventTypeAdded);

  return bp_sp->GetID();
}

bool BreakpointList::Remove(break_id_t break_id, bool notify) {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);

  auto it = std::find_if(
      m_breakpoints.begin(), m_breakpoints.end(),
      [&](const BreakpointSP &bp) { return bp->GetID() == break_id; });

  if (it == m_breakpoints.end())
    return false;

  if (notify)
    NotifyChange(*it, eBreakpointEventTypeRemoved);

  m_breakpoints.erase(it);

  return true;
}

void BreakpointList::RemoveInvalidLocations(const ArchSpec &arch) {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  for (const auto &bp_sp : m_breakpoints)
    bp_sp->RemoveInvalidLocations(arch);
}

void BreakpointList::SetEnabledAll(bool enabled) {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  for (const auto &bp_sp : m_breakpoints)
    bp_sp->SetEnabled(enabled);
}

void BreakpointList::SetEnabledAllowed(bool enabled) {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  for (const auto &bp_sp : m_breakpoints)
    if (bp_sp->AllowDisable())
      bp_sp->SetEnabled(enabled);
}

void BreakpointList::RemoveAll(bool notify) {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  ClearAllBreakpointSites();

  if (notify) {
    for (const auto &bp_sp : m_breakpoints)
      NotifyChange(bp_sp, eBreakpointEventTypeRemoved);
  }

  m_breakpoints.clear();
}

void BreakpointList::RemoveAllowed(bool notify) {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);

  for (const auto &bp_sp : m_breakpoints) {
    if (bp_sp->AllowDelete())
      bp_sp->ClearAllBreakpointSites();
    if (notify)
      NotifyChange(bp_sp, eBreakpointEventTypeRemoved);
  }

  llvm::erase_if(m_breakpoints,
                 [&](const BreakpointSP &bp) { return bp->AllowDelete(); });
}

BreakpointList::bp_collection::iterator
BreakpointList::GetBreakpointIDIterator(break_id_t break_id) {
  return std::find_if(
      m_breakpoints.begin(), m_breakpoints.end(),
      [&](const BreakpointSP &bp) { return bp->GetID() == break_id; });
}

BreakpointList::bp_collection::const_iterator
BreakpointList::GetBreakpointIDConstIterator(break_id_t break_id) const {
  return std::find_if(
      m_breakpoints.begin(), m_breakpoints.end(),
      [&](const BreakpointSP &bp) { return bp->GetID() == break_id; });
}

BreakpointSP BreakpointList::FindBreakpointByID(break_id_t break_id) const {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);

  auto it = GetBreakpointIDConstIterator(break_id);
  if (it != m_breakpoints.end())
    return *it;
  return {};
}

llvm::Expected<std::vector<lldb::BreakpointSP>>
BreakpointList::FindBreakpointsByName(const char *name) {
  if (!name)
    return llvm::createStringError(llvm::errc::invalid_argument,
                                   "FindBreakpointsByName requires a name");

  Status error;
  if (!BreakpointID::StringIsBreakpointName(llvm::StringRef(name), error))
    return error.ToError();

  std::vector<lldb::BreakpointSP> matching_bps;
  for (BreakpointSP bkpt_sp : Breakpoints()) {
    if (bkpt_sp->MatchesName(name)) {
      matching_bps.push_back(bkpt_sp);
    }
  }

  return matching_bps;
}

void BreakpointList::Dump(Stream *s) const {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  s->Printf("%p: ", static_cast<const void *>(this));
  s->Indent();
  s->Printf("BreakpointList with %u Breakpoints:\n",
            (uint32_t)m_breakpoints.size());
  s->IndentMore();
  for (const auto &bp_sp : m_breakpoints)
    bp_sp->Dump(s);
  s->IndentLess();
}

BreakpointSP BreakpointList::GetBreakpointAtIndex(size_t i) const {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  if (i < m_breakpoints.size())
    return m_breakpoints[i];
  return {};
}

void BreakpointList::UpdateBreakpoints(ModuleList &module_list, bool added,
                                       bool delete_locations) {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  for (const auto &bp_sp : m_breakpoints)
    bp_sp->ModulesChanged(module_list, added, delete_locations);
}

void BreakpointList::UpdateBreakpointsWhenModuleIsReplaced(
    ModuleSP old_module_sp, ModuleSP new_module_sp) {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  for (const auto &bp_sp : m_breakpoints)
    bp_sp->ModuleReplaced(old_module_sp, new_module_sp);
}

void BreakpointList::ClearAllBreakpointSites() {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  for (const auto &bp_sp : m_breakpoints)
    bp_sp->ClearAllBreakpointSites();
}

void BreakpointList::ResetHitCounts() {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  for (const auto &bp_sp : m_breakpoints)
    bp_sp->ResetHitCount();
}

void BreakpointList::GetListMutex(
    std::unique_lock<std::recursive_mutex> &lock) {
  lock = std::unique_lock<std::recursive_mutex>(m_mutex);
}
