//===-- DNBBreakpoint.cpp ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  Created by Greg Clayton on 6/29/07.
//
//===----------------------------------------------------------------------===//

#include "DNBBreakpoint.h"
#include "DNBLog.h"
#include "MachProcess.h"
#include <algorithm>
#include <cassert>
#include <cinttypes>

#pragma mark-- DNBBreakpoint
DNBBreakpoint::DNBBreakpoint(nub_addr_t addr, nub_size_t byte_size,
                             bool hardware)
    : m_retain_count(1), m_byte_size(static_cast<uint32_t>(byte_size)),
      m_opcode(), m_addr(addr), m_enabled(0), m_hw_preferred(hardware),
      m_is_watchpoint(0), m_watch_read(0), m_watch_write(0),
      m_hw_index(INVALID_NUB_HW_INDEX) {}

DNBBreakpoint::~DNBBreakpoint() = default;

void DNBBreakpoint::Dump() const {
  if (IsBreakpoint()) {
    DNBLog("DNBBreakpoint addr = 0x%llx  state = %s  type = %s breakpoint  "
           "hw_index = %i",
           (uint64_t)m_addr, m_enabled ? "enabled " : "disabled",
           IsHardware() ? "hardware" : "software", GetHardwareIndex());
  } else {
    DNBLog("DNBBreakpoint addr = 0x%llx  size = %llu  state = %s  type = %s "
           "watchpoint (%s%s)  hw_index = %i",
           (uint64_t)m_addr, (uint64_t)m_byte_size,
           m_enabled ? "enabled " : "disabled",
           IsHardware() ? "hardware" : "software", m_watch_read ? "r" : "",
           m_watch_write ? "w" : "", GetHardwareIndex());
  }
}

#pragma mark-- DNBBreakpointList

DNBBreakpointList::DNBBreakpointList() = default;

DNBBreakpointList::~DNBBreakpointList() = default;

DNBBreakpoint *DNBBreakpointList::Add(nub_addr_t addr, nub_size_t length,
                                      bool hardware) {
  m_breakpoints.insert(
      std::make_pair(addr, DNBBreakpoint(addr, length, hardware)));
  iterator pos = m_breakpoints.find(addr);
  return &pos->second;
}

bool DNBBreakpointList::Remove(nub_addr_t addr) {
  iterator pos = m_breakpoints.find(addr);
  if (pos != m_breakpoints.end()) {
    m_breakpoints.erase(pos);
    return true;
  }
  return false;
}

DNBBreakpoint *DNBBreakpointList::FindByAddress(nub_addr_t addr) {
  iterator pos = m_breakpoints.find(addr);
  if (pos != m_breakpoints.end())
    return &pos->second;

  return NULL;
}

const DNBBreakpoint *DNBBreakpointList::FindByAddress(nub_addr_t addr) const {
  const_iterator pos = m_breakpoints.find(addr);
  if (pos != m_breakpoints.end())
    return &pos->second;

  return NULL;
}

const DNBBreakpoint *
DNBBreakpointList::FindByHardwareIndex(uint32_t idx) const {
  for (const auto &pos : m_breakpoints)
    if (pos.second.GetHardwareIndex() == idx)
      return &pos.second;

  return nullptr;
}

const DNBBreakpoint *
DNBBreakpointList::FindNearestWatchpoint(nub_addr_t addr) const {
  // Exact match
  for (const auto &pos : m_breakpoints) {
    if (pos.second.IsEnabled()) {
      nub_addr_t start_addr = pos.second.Address();
      nub_addr_t end_addr = start_addr + pos.second.ByteSize();
      if (addr >= start_addr && addr <= end_addr)
        return &pos.second;
    }
  }

  // Find watchpoint nearest to this address
  // before or after the watched region of memory
  const DNBBreakpoint *closest = nullptr;
  uint32_t best_match = UINT32_MAX;
  for (const auto &pos : m_breakpoints) {
    if (pos.second.IsEnabled()) {
      nub_addr_t start_addr = pos.second.Address();
      nub_addr_t end_addr = start_addr + pos.second.ByteSize();
      uint32_t delta = addr < start_addr ? start_addr - addr : addr - end_addr;
      if (delta < best_match) {
        closest = &pos.second;
        best_match = delta;
      }
    }
  }
  return closest;
}

// Finds the next breakpoint at an address greater than or equal to "addr"
size_t DNBBreakpointList::FindBreakpointsThatOverlapRange(
    nub_addr_t addr, nub_addr_t size, std::vector<DNBBreakpoint *> &bps) {
  bps.clear();
  iterator end = m_breakpoints.end();
  // Find the first breakpoint with an address >= to "addr"
  iterator pos = m_breakpoints.lower_bound(addr);
  if (pos != end) {
    if (pos != m_breakpoints.begin()) {
      // Watch out for a breakpoint at an address less than "addr" that might
      // still overlap
      iterator prev_pos = pos;
      --prev_pos;
      if (prev_pos->second.IntersectsRange(addr, size, NULL, NULL, NULL))
        bps.push_back(&pos->second);
    }

    while (pos != end) {
      // When we hit a breakpoint whose start address is greater than "addr +
      // size" we are done.
      // Do the math in a way that doesn't risk unsigned overflow with bad
      // input.
      if ((pos->second.Address() - addr) >= size)
        break;

      // Check if this breakpoint overlaps, and if it does, add it to the list
      if (pos->second.IntersectsRange(addr, size, NULL, NULL, NULL)) {
        bps.push_back(&pos->second);
        ++pos;
      }
    }
  }
  return bps.size();
}

void DNBBreakpointList::Dump() const {
  const_iterator pos;
  const_iterator end = m_breakpoints.end();
  for (pos = m_breakpoints.begin(); pos != end; ++pos)
    pos->second.Dump();
}

void DNBBreakpointList::DisableAll() {
  iterator pos, end = m_breakpoints.end();
  for (pos = m_breakpoints.begin(); pos != end; ++pos)
    pos->second.SetEnabled(false);
}

void DNBBreakpointList::RemoveTrapsFromBuffer(nub_addr_t addr, nub_size_t size,
                                              void *p) const {
  uint8_t *buf = (uint8_t *)p;
  const_iterator end = m_breakpoints.end();
  const_iterator pos = m_breakpoints.lower_bound(addr);
  while (pos != end && (pos->first < (addr + size))) {
    nub_addr_t intersect_addr;
    nub_size_t intersect_size;
    nub_size_t opcode_offset;
    const DNBBreakpoint &bp = pos->second;
    if (bp.IntersectsRange(addr, size, &intersect_addr, &intersect_size,
                           &opcode_offset)) {
      assert(addr <= intersect_addr && intersect_addr < addr + size);
      assert(addr < intersect_addr + intersect_size &&
             intersect_addr + intersect_size <= addr + size);
      assert(opcode_offset + intersect_size <= bp.ByteSize());
      nub_size_t buf_offset = intersect_addr - addr;
      ::memcpy(buf + buf_offset, bp.SavedOpcodeBytes() + opcode_offset,
               intersect_size);
    }
    ++pos;
  }
}

void DNBBreakpointList::DisableAllBreakpoints(MachProcess *process) {
  iterator pos, end = m_breakpoints.end();
  for (pos = m_breakpoints.begin(); pos != end; ++pos)
    process->DisableBreakpoint(pos->second.Address(), false);
}

void DNBBreakpointList::DisableAllWatchpoints(MachProcess *process) {
  iterator pos, end = m_breakpoints.end();
  for (pos = m_breakpoints.begin(); pos != end; ++pos)
    process->DisableWatchpoint(pos->second.Address(), false);
}

void DNBBreakpointList::RemoveDisabled() {
  iterator pos = m_breakpoints.begin();
  while (pos != m_breakpoints.end()) {
    if (!pos->second.IsEnabled())
      pos = m_breakpoints.erase(pos);
    else
      ++pos;
  }
}
