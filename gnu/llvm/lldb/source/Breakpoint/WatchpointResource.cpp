//===-- WatchpointResource.cpp --------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <assert.h>

#include "lldb/Breakpoint/WatchpointResource.h"
#include "lldb/Utility/Stream.h"

#include <algorithm>

using namespace lldb;
using namespace lldb_private;

WatchpointResource::WatchpointResource(lldb::addr_t addr, size_t size,
                                       bool read, bool write)
    : m_id(GetNextID()), m_addr(addr), m_size(size),
      m_watch_read(read), m_watch_write(write) {}

WatchpointResource::~WatchpointResource() {
  std::lock_guard<std::mutex> guard(m_constituents_mutex);
  m_constituents.clear();
}

addr_t WatchpointResource::GetLoadAddress() const { return m_addr; }

size_t WatchpointResource::GetByteSize() const { return m_size; }

bool WatchpointResource::WatchpointResourceRead() const { return m_watch_read; }

bool WatchpointResource::WatchpointResourceWrite() const {
  return m_watch_write;
}

void WatchpointResource::SetType(bool read, bool write) {
  m_watch_read = read;
  m_watch_write = write;
}

wp_resource_id_t WatchpointResource::GetID() const { return m_id; }

bool WatchpointResource::Contains(addr_t addr) {
  if (addr >= m_addr && addr < m_addr + m_size)
    return true;
  return false;
}

void WatchpointResource::AddConstituent(const WatchpointSP &wp_sp) {
  std::lock_guard<std::mutex> guard(m_constituents_mutex);
  m_constituents.push_back(wp_sp);
}

void WatchpointResource::RemoveConstituent(WatchpointSP &wp_sp) {
  std::lock_guard<std::mutex> guard(m_constituents_mutex);
  const auto &it =
      std::find(m_constituents.begin(), m_constituents.end(), wp_sp);
  if (it != m_constituents.end())
    m_constituents.erase(it);
}

size_t WatchpointResource::GetNumberOfConstituents() {
  std::lock_guard<std::mutex> guard(m_constituents_mutex);
  return m_constituents.size();
}

bool WatchpointResource::ConstituentsContains(const WatchpointSP &wp_sp) {
  return ConstituentsContains(wp_sp.get());
}

bool WatchpointResource::ConstituentsContains(const Watchpoint *wp) {
  std::lock_guard<std::mutex> guard(m_constituents_mutex);
  WatchpointCollection::const_iterator match =
      std::find_if(m_constituents.begin(), m_constituents.end(),
                   [&wp](const WatchpointSP &x) { return x.get() == wp; });
  return match != m_constituents.end();
}

WatchpointSP WatchpointResource::GetConstituentAtIndex(size_t idx) {
  std::lock_guard<std::mutex> guard(m_constituents_mutex);
  assert(idx < m_constituents.size());
  if (idx >= m_constituents.size())
    return {};

  return m_constituents[idx];
}

WatchpointResource::WatchpointCollection
WatchpointResource::CopyConstituentsList() {
  std::lock_guard<std::mutex> guard(m_constituents_mutex);
  return m_constituents;
}

bool WatchpointResource::ShouldStop(StoppointCallbackContext *context) {
  // LWP_TODO: Need to poll all Watchpoint constituents and see if
  // we should stop, like BreakpointSites do.
#if 0
  m_hit_counter.Increment();
  // ShouldStop can do a lot of work, and might even come back and hit
  // this breakpoint site again.  So don't hold the m_constituents_mutex the
  // whole while.  Instead make a local copy of the collection and call
  // ShouldStop on the copy.
  WatchpointResourceCollection constituents_copy;
  {
    std::lock_guard<std::recursive_mutex> guard(m_constituents_mutex);
    constituents_copy = m_constituents;
  }
  return constituents_copy.ShouldStop(context);
#endif
  return true;
}

void WatchpointResource::Dump(Stream *s) const {
  s->Printf("addr = 0x%8.8" PRIx64 " size = %zu", m_addr, m_size);
}

wp_resource_id_t WatchpointResource::GetNextID() {
  static wp_resource_id_t g_next_id = 0;
  return ++g_next_id;
}
