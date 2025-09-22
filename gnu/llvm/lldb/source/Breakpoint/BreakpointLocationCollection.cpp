//===-- BreakpointLocationCollection.cpp ----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Breakpoint/BreakpointLocationCollection.h"
#include "lldb/Breakpoint/Breakpoint.h"
#include "lldb/Breakpoint/BreakpointLocation.h"
#include "lldb/Core/ModuleList.h"
#include "lldb/Target/Thread.h"
#include "lldb/Target/ThreadSpec.h"

using namespace lldb;
using namespace lldb_private;

// BreakpointLocationCollection constructor
BreakpointLocationCollection::BreakpointLocationCollection() = default;

// Destructor
BreakpointLocationCollection::~BreakpointLocationCollection() = default;

void BreakpointLocationCollection::Add(const BreakpointLocationSP &bp_loc) {
  std::lock_guard<std::mutex> guard(m_collection_mutex);
  BreakpointLocationSP old_bp_loc =
      FindByIDPair(bp_loc->GetBreakpoint().GetID(), bp_loc->GetID());
  if (!old_bp_loc.get())
    m_break_loc_collection.push_back(bp_loc);
}

bool BreakpointLocationCollection::Remove(lldb::break_id_t bp_id,
                                          lldb::break_id_t bp_loc_id) {
  std::lock_guard<std::mutex> guard(m_collection_mutex);
  collection::iterator pos = GetIDPairIterator(bp_id, bp_loc_id); // Predicate
  if (pos != m_break_loc_collection.end()) {
    m_break_loc_collection.erase(pos);
    return true;
  }
  return false;
}

class BreakpointIDPairMatches {
public:
  BreakpointIDPairMatches(lldb::break_id_t break_id,
                          lldb::break_id_t break_loc_id)
      : m_break_id(break_id), m_break_loc_id(break_loc_id) {}

  bool operator()(const BreakpointLocationSP &bp_loc) const {
    return m_break_id == bp_loc->GetBreakpoint().GetID() &&
           m_break_loc_id == bp_loc->GetID();
  }

private:
  const lldb::break_id_t m_break_id;
  const lldb::break_id_t m_break_loc_id;
};

BreakpointLocationCollection::collection::iterator
BreakpointLocationCollection::GetIDPairIterator(lldb::break_id_t break_id,
                                                lldb::break_id_t break_loc_id) {
  return std::find_if(
      m_break_loc_collection.begin(),
      m_break_loc_collection.end(),                     // Search full range
      BreakpointIDPairMatches(break_id, break_loc_id)); // Predicate
}

BreakpointLocationCollection::collection::const_iterator
BreakpointLocationCollection::GetIDPairConstIterator(
    lldb::break_id_t break_id, lldb::break_id_t break_loc_id) const {
  return std::find_if(
      m_break_loc_collection.begin(),
      m_break_loc_collection.end(),                     // Search full range
      BreakpointIDPairMatches(break_id, break_loc_id)); // Predicate
}

BreakpointLocationSP
BreakpointLocationCollection::FindByIDPair(lldb::break_id_t break_id,
                                           lldb::break_id_t break_loc_id) {
  BreakpointLocationSP stop_sp;
  collection::iterator pos = GetIDPairIterator(break_id, break_loc_id);
  if (pos != m_break_loc_collection.end())
    stop_sp = *pos;

  return stop_sp;
}

const BreakpointLocationSP BreakpointLocationCollection::FindByIDPair(
    lldb::break_id_t break_id, lldb::break_id_t break_loc_id) const {
  BreakpointLocationSP stop_sp;
  collection::const_iterator pos =
      GetIDPairConstIterator(break_id, break_loc_id);
  if (pos != m_break_loc_collection.end())
    stop_sp = *pos;

  return stop_sp;
}

BreakpointLocationSP BreakpointLocationCollection::GetByIndex(size_t i) {
  std::lock_guard<std::mutex> guard(m_collection_mutex);
  BreakpointLocationSP stop_sp;
  if (i < m_break_loc_collection.size())
    stop_sp = m_break_loc_collection[i];

  return stop_sp;
}

const BreakpointLocationSP
BreakpointLocationCollection::GetByIndex(size_t i) const {
  std::lock_guard<std::mutex> guard(m_collection_mutex);
  BreakpointLocationSP stop_sp;
  if (i < m_break_loc_collection.size())
    stop_sp = m_break_loc_collection[i];

  return stop_sp;
}

bool BreakpointLocationCollection::ShouldStop(
    StoppointCallbackContext *context) {
  bool shouldStop = false;
  size_t i = 0;
  size_t prev_size = GetSize();
  while (i < prev_size) {
    // ShouldStop can remove the breakpoint from the list, or even delete
    // it, so we should 
    BreakpointLocationSP cur_loc_sp = GetByIndex(i);
    BreakpointSP keep_bkpt_alive_sp = cur_loc_sp->GetBreakpoint().shared_from_this();
    if (cur_loc_sp->ShouldStop(context))
      shouldStop = true;

    if (prev_size == GetSize())
      i++;
    prev_size = GetSize();
  }
  return shouldStop;
}

bool BreakpointLocationCollection::ValidForThisThread(Thread &thread) {
  std::lock_guard<std::mutex> guard(m_collection_mutex);
  collection::iterator pos, begin = m_break_loc_collection.begin(),
                            end = m_break_loc_collection.end();

  for (pos = begin; pos != end; ++pos) {
    if ((*pos)->ValidForThisThread(thread))
      return true;
  }
  return false;
}

bool BreakpointLocationCollection::IsInternal() const {
  std::lock_guard<std::mutex> guard(m_collection_mutex);
  collection::const_iterator pos, begin = m_break_loc_collection.begin(),
                                  end = m_break_loc_collection.end();

  bool is_internal = true;

  for (pos = begin; pos != end; ++pos) {
    if (!(*pos)->GetBreakpoint().IsInternal()) {
      is_internal = false;
      break;
    }
  }
  return is_internal;
}

void BreakpointLocationCollection::GetDescription(
    Stream *s, lldb::DescriptionLevel level) {
  std::lock_guard<std::mutex> guard(m_collection_mutex);
  collection::iterator pos, begin = m_break_loc_collection.begin(),
                            end = m_break_loc_collection.end();

  for (pos = begin; pos != end; ++pos) {
    if (pos != begin)
      s->PutChar(' ');
    (*pos)->GetDescription(s, level);
  }
}

BreakpointLocationCollection &BreakpointLocationCollection::operator=(
    const BreakpointLocationCollection &rhs) {
  if (this != &rhs) {
      std::lock(m_collection_mutex, rhs.m_collection_mutex);
      std::lock_guard<std::mutex> lhs_guard(m_collection_mutex, std::adopt_lock);
      std::lock_guard<std::mutex> rhs_guard(rhs.m_collection_mutex, std::adopt_lock);
      m_break_loc_collection = rhs.m_break_loc_collection;
  }
  return *this;
}
