//===-- BreakpointSite.cpp ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <cinttypes>

#include "lldb/Breakpoint/BreakpointSite.h"

#include "lldb/Breakpoint/Breakpoint.h"
#include "lldb/Breakpoint/BreakpointLocation.h"
#include "lldb/Utility/Stream.h"

using namespace lldb;
using namespace lldb_private;

BreakpointSite::BreakpointSite(const BreakpointLocationSP &constituent,
                               lldb::addr_t addr, bool use_hardware)
    : StoppointSite(GetNextID(), addr, 0, use_hardware),
      m_type(eSoftware), // Process subclasses need to set this correctly using
                         // SetType()
      m_saved_opcode(), m_trap_opcode(),
      m_enabled(false) // Need to create it disabled, so the first enable turns
                       // it on.
{
  m_constituents.Add(constituent);
}

BreakpointSite::~BreakpointSite() {
  BreakpointLocationSP bp_loc_sp;
  const size_t constituent_count = m_constituents.GetSize();
  for (size_t i = 0; i < constituent_count; i++) {
    m_constituents.GetByIndex(i)->ClearBreakpointSite();
  }
}

break_id_t BreakpointSite::GetNextID() {
  static break_id_t g_next_id = 0;
  return ++g_next_id;
}

// RETURNS - true if we should stop at this breakpoint, false if we
// should continue.

bool BreakpointSite::ShouldStop(StoppointCallbackContext *context) {
  m_hit_counter.Increment();
  // ShouldStop can do a lot of work, and might even come back and hit
  // this breakpoint site again.  So don't hold the m_constituents_mutex the
  // whole while.  Instead make a local copy of the collection and call
  // ShouldStop on the copy.
  BreakpointLocationCollection constituents_copy;
  {
    std::lock_guard<std::recursive_mutex> guard(m_constituents_mutex);
    constituents_copy = m_constituents;
  }
  return constituents_copy.ShouldStop(context);
}

bool BreakpointSite::IsBreakpointAtThisSite(lldb::break_id_t bp_id) {
  std::lock_guard<std::recursive_mutex> guard(m_constituents_mutex);
  const size_t constituent_count = m_constituents.GetSize();
  for (size_t i = 0; i < constituent_count; i++) {
    if (m_constituents.GetByIndex(i)->GetBreakpoint().GetID() == bp_id)
      return true;
  }
  return false;
}

void BreakpointSite::Dump(Stream *s) const {
  if (s == nullptr)
    return;

  s->Printf("BreakpointSite %u: addr = 0x%8.8" PRIx64
            "  type = %s breakpoint  hit_count = %-4u",
            GetID(), (uint64_t)m_addr, IsHardware() ? "hardware" : "software",
            GetHitCount());
}

void BreakpointSite::GetDescription(Stream *s, lldb::DescriptionLevel level) {
  std::lock_guard<std::recursive_mutex> guard(m_constituents_mutex);
  if (level != lldb::eDescriptionLevelBrief)
    s->Printf("breakpoint site: %d at 0x%8.8" PRIx64, GetID(),
              GetLoadAddress());
  m_constituents.GetDescription(s, level);
}

bool BreakpointSite::IsInternal() const { return m_constituents.IsInternal(); }

uint8_t *BreakpointSite::GetTrapOpcodeBytes() { return &m_trap_opcode[0]; }

const uint8_t *BreakpointSite::GetTrapOpcodeBytes() const {
  return &m_trap_opcode[0];
}

size_t BreakpointSite::GetTrapOpcodeMaxByteSize() const {
  return sizeof(m_trap_opcode);
}

bool BreakpointSite::SetTrapOpcode(const uint8_t *trap_opcode,
                                   uint32_t trap_opcode_size) {
  if (trap_opcode_size > 0 && trap_opcode_size <= sizeof(m_trap_opcode)) {
    m_byte_size = trap_opcode_size;
    ::memcpy(m_trap_opcode, trap_opcode, trap_opcode_size);
    return true;
  }
  m_byte_size = 0;
  return false;
}

uint8_t *BreakpointSite::GetSavedOpcodeBytes() { return &m_saved_opcode[0]; }

const uint8_t *BreakpointSite::GetSavedOpcodeBytes() const {
  return &m_saved_opcode[0];
}

bool BreakpointSite::IsEnabled() const { return m_enabled; }

void BreakpointSite::SetEnabled(bool enabled) { m_enabled = enabled; }

void BreakpointSite::AddConstituent(const BreakpointLocationSP &constituent) {
  std::lock_guard<std::recursive_mutex> guard(m_constituents_mutex);
  m_constituents.Add(constituent);
}

size_t BreakpointSite::RemoveConstituent(lldb::break_id_t break_id,
                                         lldb::break_id_t break_loc_id) {
  std::lock_guard<std::recursive_mutex> guard(m_constituents_mutex);
  m_constituents.Remove(break_id, break_loc_id);
  return m_constituents.GetSize();
}

size_t BreakpointSite::GetNumberOfConstituents() {
  std::lock_guard<std::recursive_mutex> guard(m_constituents_mutex);
  return m_constituents.GetSize();
}

BreakpointLocationSP BreakpointSite::GetConstituentAtIndex(size_t index) {
  std::lock_guard<std::recursive_mutex> guard(m_constituents_mutex);
  return m_constituents.GetByIndex(index);
}

bool BreakpointSite::ValidForThisThread(Thread &thread) {
  std::lock_guard<std::recursive_mutex> guard(m_constituents_mutex);
  return m_constituents.ValidForThisThread(thread);
}

void BreakpointSite::BumpHitCounts() {
  std::lock_guard<std::recursive_mutex> guard(m_constituents_mutex);
  for (BreakpointLocationSP loc_sp : m_constituents.BreakpointLocations()) {
    loc_sp->BumpHitCount();
  }
}

bool BreakpointSite::IntersectsRange(lldb::addr_t addr, size_t size,
                                     lldb::addr_t *intersect_addr,
                                     size_t *intersect_size,
                                     size_t *opcode_offset) const {
  // The function should be called only for software breakpoints.
  lldbassert(GetType() == Type::eSoftware);

  if (m_byte_size == 0)
    return false;

  const lldb::addr_t bp_end_addr = m_addr + m_byte_size;
  const lldb::addr_t end_addr = addr + size;
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
            std::min<lldb::addr_t>(bp_end_addr, end_addr) - addr;
      if (opcode_offset)
        *opcode_offset = addr - m_addr;
    } else {
      if (intersect_addr)
        *intersect_addr = m_addr;
      if (intersect_size)
        *intersect_size =
            std::min<lldb::addr_t>(bp_end_addr, end_addr) - m_addr;
      if (opcode_offset)
        *opcode_offset = 0;
    }
  }
  return true;
}

size_t BreakpointSite::CopyConstituentsList(
    BreakpointLocationCollection &out_collection) {
  std::lock_guard<std::recursive_mutex> guard(m_constituents_mutex);
  for (BreakpointLocationSP loc_sp : m_constituents.BreakpointLocations()) {
    out_collection.Add(loc_sp);
  }
  return out_collection.GetSize();
}
