//===-- DNBBreakpoint.h -----------------------------------------*- C++ -*-===//
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

#ifndef LLDB_TOOLS_DEBUGSERVER_SOURCE_DNBBREAKPOINT_H
#define LLDB_TOOLS_DEBUGSERVER_SOURCE_DNBBREAKPOINT_H

#include <mach/mach.h>

#include <map>
#include <vector>

#include "DNBDefs.h"

class MachProcess;

class DNBBreakpoint {
public:
  DNBBreakpoint(nub_addr_t m_addr, nub_size_t byte_size, bool hardware);
  ~DNBBreakpoint();

  nub_size_t ByteSize() const { return m_byte_size; }
  uint8_t *SavedOpcodeBytes() { return &m_opcode[0]; }
  const uint8_t *SavedOpcodeBytes() const { return &m_opcode[0]; }
  nub_addr_t Address() const { return m_addr; }
  //    nub_thread_t ThreadID() const { return m_tid; }
  bool IsEnabled() const { return m_enabled; }
  bool IntersectsRange(nub_addr_t addr, nub_size_t size,
                       nub_addr_t *intersect_addr, nub_size_t *intersect_size,
                       nub_size_t *opcode_offset) const {
    // We only use software traps for software breakpoints
    if (IsBreakpoint() && IsEnabled() && !IsHardware()) {
      if (m_byte_size > 0) {
        const nub_addr_t bp_end_addr = m_addr + m_byte_size;
        const nub_addr_t end_addr = addr + size;
        // Is the breakpoint end address before the passed in start address?
        if (bp_end_addr <= addr)
          return false;
        // Is the breakpoint start address after passed in end address?
        if (end_addr <= m_addr)
          return false;
        if (intersect_addr || intersect_size || opcode_offset) {
          if (m_addr < addr) {
            if (intersect_addr)
              *intersect_addr = addr;
            if (intersect_size)
              *intersect_size =
                  std::min<nub_addr_t>(bp_end_addr, end_addr) - addr;
            if (opcode_offset)
              *opcode_offset = addr - m_addr;
          } else {
            if (intersect_addr)
              *intersect_addr = m_addr;
            if (intersect_size)
              *intersect_size =
                  std::min<nub_addr_t>(bp_end_addr, end_addr) - m_addr;
            if (opcode_offset)
              *opcode_offset = 0;
          }
        }
        return true;
      }
    }
    return false;
  }
  void SetEnabled(bool enabled) {
    if (!enabled)
      SetHardwareIndex(INVALID_NUB_HW_INDEX);
    m_enabled = enabled;
  }
  void SetIsWatchpoint(uint32_t type) {
    m_is_watchpoint = 1;
    m_watch_read = (type & WATCH_TYPE_READ) != 0;
    m_watch_write = (type & WATCH_TYPE_WRITE) != 0;
  }
  bool IsBreakpoint() const { return m_is_watchpoint == 0; }
  bool IsWatchpoint() const { return m_is_watchpoint == 1; }
  bool WatchpointRead() const { return m_watch_read != 0; }
  bool WatchpointWrite() const { return m_watch_write != 0; }
  bool HardwarePreferred() const { return m_hw_preferred; }
  bool IsHardware() const { return m_hw_index != INVALID_NUB_HW_INDEX; }
  uint32_t GetHardwareIndex() const { return m_hw_index; }
  void SetHardwareIndex(uint32_t hw_index) { m_hw_index = hw_index; }
  void Dump() const;
  uint32_t Retain() { return ++m_retain_count; }
  uint32_t Release() {
    if (m_retain_count == 0)
      return 0;
    return --m_retain_count;
  }

private:
  uint32_t m_retain_count; // Each breakpoint is maintained by address and is
                           // ref counted in case multiple people set a
                           // breakpoint at the same address
  uint32_t m_byte_size;    // Length in bytes of the breakpoint if set in memory
  uint8_t m_opcode[8];     // Saved opcode bytes
  nub_addr_t m_addr;       // Address of this breakpoint
  uint32_t m_enabled : 1,  // Flags for this breakpoint
      m_hw_preferred : 1,  // 1 if this point has been requested to be set using
                           // hardware (which may fail due to lack of resources)
      m_is_watchpoint : 1, // 1 if this is a watchpoint
      m_watch_read : 1,    // 1 if we stop when the watched data is read from
      m_watch_write : 1;   // 1 if we stop when the watched data is written to
  uint32_t
      m_hw_index; // The hardware resource index for this breakpoint/watchpoint
};

class DNBBreakpointList {
public:
  DNBBreakpointList();
  ~DNBBreakpointList();

  DNBBreakpoint *Add(nub_addr_t addr, nub_size_t length, bool hardware);
  bool Remove(nub_addr_t addr);
  DNBBreakpoint *FindByAddress(nub_addr_t addr);
  const DNBBreakpoint *FindNearestWatchpoint(nub_addr_t addr) const;
  const DNBBreakpoint *FindByAddress(nub_addr_t addr) const;
  const DNBBreakpoint *FindByHardwareIndex(uint32_t idx) const;

  size_t FindBreakpointsThatOverlapRange(nub_addr_t addr, nub_addr_t size,
                                         std::vector<DNBBreakpoint *> &bps);

  void Dump() const;

  size_t Size() const { return m_breakpoints.size(); }
  void DisableAll();

  void RemoveTrapsFromBuffer(nub_addr_t addr, nub_size_t size, void *buf) const;

  void DisableAllBreakpoints(MachProcess *process);
  void DisableAllWatchpoints(MachProcess *process);
  void RemoveDisabled();

protected:
  typedef std::map<nub_addr_t, DNBBreakpoint> collection;
  typedef collection::iterator iterator;
  typedef collection::const_iterator const_iterator;
  collection m_breakpoints;
};

#endif
