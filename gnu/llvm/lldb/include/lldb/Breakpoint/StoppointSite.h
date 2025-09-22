//===-- StoppointSite.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_BREAKPOINT_STOPPOINTSITE_H
#define LLDB_BREAKPOINT_STOPPOINTSITE_H

#include "lldb/Breakpoint/StoppointHitCounter.h"
#include "lldb/Utility/UserID.h"
#include "lldb/lldb-private.h"

namespace lldb_private {

class StoppointSite {
public:
  StoppointSite(lldb::break_id_t bid, lldb::addr_t m_addr, bool hardware);

  StoppointSite(lldb::break_id_t bid, lldb::addr_t m_addr,
                uint32_t byte_size, bool hardware);

  virtual ~StoppointSite() = default;

  virtual lldb::addr_t GetLoadAddress() const { return m_addr; }

  virtual void SetLoadAddress(lldb::addr_t addr) { m_addr = addr; }

  uint32_t GetByteSize() const { return m_byte_size; }

  uint32_t GetHitCount() const { return m_hit_counter.GetValue(); }

  void ResetHitCount() { m_hit_counter.Reset(); }

  bool HardwareRequired() const { return m_is_hardware_required; }

  virtual bool IsHardware() const = 0;

  virtual bool ShouldStop(StoppointCallbackContext* context) = 0;

  virtual void Dump(Stream* stream) const = 0;

  lldb::break_id_t GetID() const { return m_id; }

protected:
  /// Stoppoint site ID.
  lldb::break_id_t m_id;

  /// The load address of this stop point.
  lldb::addr_t m_addr;

  /// True if this point is required to use hardware (which may fail due to
  /// the lack of resources).
  bool m_is_hardware_required;

  /// The size in bytes of stoppoint, e.g. the length of the trap opcode for
  /// software breakpoints, or the optional length in bytes for hardware
  /// breakpoints, or the length of the watchpoint.
  uint32_t m_byte_size;

  /// Number of times this breakpoint/watchpoint has been hit.
  StoppointHitCounter m_hit_counter;

private:
  StoppointSite(const StoppointSite &) = delete;
  const StoppointSite &operator=(const StoppointSite &) = delete;
  StoppointSite() = delete;
};

} // namespace lldb_private

#endif // LLDB_BREAKPOINT_STOPPOINTSITE_H
