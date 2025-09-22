//===-- NativeRegisterContextDBReg_arm64.cpp ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "NativeRegisterContextDBReg_arm64.h"

#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/RegisterValue.h"

using namespace lldb_private;

// E (bit 0), used to enable breakpoint/watchpoint
constexpr uint32_t g_enable_bit = 1;
// PAC (bits 2:1): 0b10
constexpr uint32_t g_pac_bits = (2 << 1);

// Returns appropriate control register bits for the specified size
static constexpr inline uint64_t GetSizeBits(int size) {
  // BAS (bits 12:5) hold a bit-mask of addresses to watch
  // e.g. 0b00000001 means 1 byte at address
  //      0b00000011 means 2 bytes (addr..addr+1)
  //      ...
  //      0b11111111 means 8 bytes (addr..addr+7)
  return ((1 << size) - 1) << 5;
}

uint32_t NativeRegisterContextDBReg_arm64::NumSupportedHardwareBreakpoints() {
  Log *log = GetLog(LLDBLog::Breakpoints);
  llvm::Error error = ReadHardwareDebugInfo();
  if (error) {
    LLDB_LOG_ERROR(log, std::move(error),
                   "failed to read debug registers: {0}");
    return 0;
  }

  return m_max_hbp_supported;
}

uint32_t
NativeRegisterContextDBReg_arm64::SetHardwareBreakpoint(lldb::addr_t addr,
                                                        size_t size) {
  Log *log = GetLog(LLDBLog::Breakpoints);
  LLDB_LOG(log, "addr: {0:x}, size: {1:x}", addr, size);

  // Read hardware breakpoint and watchpoint information.
  llvm::Error error = ReadHardwareDebugInfo();
  if (error) {
    LLDB_LOG_ERROR(
        log, std::move(error),
        "unable to set breakpoint: failed to read debug registers: {0}");
    return LLDB_INVALID_INDEX32;
  }

  uint32_t control_value = 0, bp_index = 0;

  // Check if size has a valid hardware breakpoint length.
  if (size != 4)
    return LLDB_INVALID_INDEX32; // Invalid size for a AArch64 hardware
                                 // breakpoint

  // Check 4-byte alignment for hardware breakpoint target address.
  if (addr & 0x03)
    return LLDB_INVALID_INDEX32; // Invalid address, should be 4-byte aligned.

  // Setup control value
  control_value = g_enable_bit | g_pac_bits | GetSizeBits(size);

  // Iterate over stored breakpoints and find a free bp_index
  bp_index = LLDB_INVALID_INDEX32;
  for (uint32_t i = 0; i < m_max_hbp_supported; i++) {
    if (!BreakpointIsEnabled(i))
      bp_index = i; // Mark last free slot
    else if (m_hbp_regs[i].address == addr)
      return LLDB_INVALID_INDEX32; // We do not support duplicate breakpoints.
  }

  if (bp_index == LLDB_INVALID_INDEX32)
    return LLDB_INVALID_INDEX32;

  // Update breakpoint in local cache
  m_hbp_regs[bp_index].real_addr = addr;
  m_hbp_regs[bp_index].address = addr;
  m_hbp_regs[bp_index].control = control_value;

  // PTRACE call to set corresponding hardware breakpoint register.
  error = WriteHardwareDebugRegs(eDREGTypeBREAK);

  if (error) {
    m_hbp_regs[bp_index].address = 0;
    m_hbp_regs[bp_index].control &= ~1;

    LLDB_LOG_ERROR(
        log, std::move(error),
        "unable to set breakpoint: failed to write debug registers: {0}");
    return LLDB_INVALID_INDEX32;
  }

  return bp_index;
}

bool NativeRegisterContextDBReg_arm64::ClearHardwareBreakpoint(
    uint32_t hw_idx) {
  Log *log = GetLog(LLDBLog::Breakpoints);
  LLDB_LOG(log, "hw_idx: {0}", hw_idx);

  // Read hardware breakpoint and watchpoint information.
  llvm::Error error = ReadHardwareDebugInfo();
  if (error) {
    LLDB_LOG_ERROR(
        log, std::move(error),
        "unable to clear breakpoint: failed to read debug registers: {0}");
    return false;
  }

  if (hw_idx >= m_max_hbp_supported)
    return false;

  // Create a backup we can revert to in case of failure.
  lldb::addr_t tempAddr = m_hbp_regs[hw_idx].address;
  uint32_t tempControl = m_hbp_regs[hw_idx].control;

  m_hbp_regs[hw_idx].control &= ~g_enable_bit;
  m_hbp_regs[hw_idx].address = 0;

  // PTRACE call to clear corresponding hardware breakpoint register.
  error = WriteHardwareDebugRegs(eDREGTypeBREAK);

  if (error) {
    m_hbp_regs[hw_idx].control = tempControl;
    m_hbp_regs[hw_idx].address = tempAddr;

    LLDB_LOG_ERROR(
        log, std::move(error),
        "unable to clear breakpoint: failed to write debug registers: {0}");
    return false;
  }

  return true;
}

Status NativeRegisterContextDBReg_arm64::GetHardwareBreakHitIndex(
    uint32_t &bp_index, lldb::addr_t trap_addr) {
  Log *log = GetLog(LLDBLog::Breakpoints);

  LLDB_LOGF(log, "NativeRegisterContextDBReg_arm64::%s()", __FUNCTION__);

  lldb::addr_t break_addr;

  for (bp_index = 0; bp_index < m_max_hbp_supported; ++bp_index) {
    break_addr = m_hbp_regs[bp_index].address;

    if (BreakpointIsEnabled(bp_index) && trap_addr == break_addr) {
      m_hbp_regs[bp_index].hit_addr = trap_addr;
      return Status();
    }
  }

  bp_index = LLDB_INVALID_INDEX32;
  return Status();
}

Status NativeRegisterContextDBReg_arm64::ClearAllHardwareBreakpoints() {
  Log *log = GetLog(LLDBLog::Breakpoints);

  LLDB_LOGF(log, "NativeRegisterContextDBReg_arm64::%s()", __FUNCTION__);

  // Read hardware breakpoint and watchpoint information.
  llvm::Error error = ReadHardwareDebugInfo();
  if (error)
    return Status(std::move(error));

  for (uint32_t i = 0; i < m_max_hbp_supported; i++) {
    if (BreakpointIsEnabled(i)) {
      // Create a backup we can revert to in case of failure.
      lldb::addr_t tempAddr = m_hbp_regs[i].address;
      uint32_t tempControl = m_hbp_regs[i].control;

      // Clear watchpoints in local cache
      m_hbp_regs[i].control &= ~g_enable_bit;
      m_hbp_regs[i].address = 0;

      // Ptrace call to update hardware debug registers
      error = WriteHardwareDebugRegs(eDREGTypeBREAK);

      if (error) {
        m_hbp_regs[i].control = tempControl;
        m_hbp_regs[i].address = tempAddr;

        return Status(std::move(error));
      }
    }
  }

  return Status();
}

bool NativeRegisterContextDBReg_arm64::BreakpointIsEnabled(uint32_t bp_index) {
  if ((m_hbp_regs[bp_index].control & g_enable_bit) != 0)
    return true;
  else
    return false;
}

uint32_t NativeRegisterContextDBReg_arm64::NumSupportedHardwareWatchpoints() {
  Log *log = GetLog(LLDBLog::Watchpoints);
  llvm::Error error = ReadHardwareDebugInfo();
  if (error) {
    LLDB_LOG_ERROR(log, std::move(error),
                   "failed to read debug registers: {0}");
    return 0;
  }

  return m_max_hwp_supported;
}

uint32_t NativeRegisterContextDBReg_arm64::SetHardwareWatchpoint(
    lldb::addr_t addr, size_t size, uint32_t watch_flags) {
  Log *log = GetLog(LLDBLog::Watchpoints);
  LLDB_LOG(log, "addr: {0:x}, size: {1:x} watch_flags: {2:x}", addr, size,
           watch_flags);

  // Read hardware breakpoint and watchpoint information.
  llvm::Error error = ReadHardwareDebugInfo();
  if (error) {
    LLDB_LOG_ERROR(
        log, std::move(error),
        "unable to set watchpoint: failed to read debug registers: {0}");
    return LLDB_INVALID_INDEX32;
  }

  uint32_t control_value = 0, wp_index = 0;
  lldb::addr_t real_addr = addr;

  // Check if we are setting watchpoint other than read/write/access Also
  // update watchpoint flag to match AArch64 write-read bit configuration.
  switch (watch_flags) {
  case 1:
    watch_flags = 2;
    break;
  case 2:
    watch_flags = 1;
    break;
  case 3:
    break;
  default:
    return LLDB_INVALID_INDEX32;
  }

  // Check if size has a valid hardware watchpoint length.
  if (size != 1 && size != 2 && size != 4 && size != 8)
    return LLDB_INVALID_INDEX32;

  // Check 8-byte alignment for hardware watchpoint target address. Below is a
  // hack to recalculate address and size in order to make sure we can watch
  // non 8-byte aligned addresses as well.
  if (addr & 0x07) {
    uint8_t watch_mask = (addr & 0x07) + size;

    if (watch_mask > 0x08)
      return LLDB_INVALID_INDEX32;
    else if (watch_mask <= 0x02)
      size = 2;
    else if (watch_mask <= 0x04)
      size = 4;
    else
      size = 8;

    addr = addr & (~0x07);
  }

  // Setup control value
  control_value = g_enable_bit | g_pac_bits | GetSizeBits(size);
  control_value |= watch_flags << 3;

  // Iterate over stored watchpoints and find a free wp_index
  wp_index = LLDB_INVALID_INDEX32;
  for (uint32_t i = 0; i < m_max_hwp_supported; i++) {
    if (!WatchpointIsEnabled(i))
      wp_index = i; // Mark last free slot
    else if (m_hwp_regs[i].address == addr) {
      return LLDB_INVALID_INDEX32; // We do not support duplicate watchpoints.
    }
  }

  if (wp_index == LLDB_INVALID_INDEX32)
    return LLDB_INVALID_INDEX32;

  // Update watchpoint in local cache
  m_hwp_regs[wp_index].real_addr = real_addr;
  m_hwp_regs[wp_index].address = addr;
  m_hwp_regs[wp_index].control = control_value;

  // PTRACE call to set corresponding watchpoint register.
  error = WriteHardwareDebugRegs(eDREGTypeWATCH);

  if (error) {
    m_hwp_regs[wp_index].address = 0;
    m_hwp_regs[wp_index].control &= ~g_enable_bit;

    LLDB_LOG_ERROR(
        log, std::move(error),
        "unable to set watchpoint: failed to write debug registers: {0}");
    return LLDB_INVALID_INDEX32;
  }

  return wp_index;
}

bool NativeRegisterContextDBReg_arm64::ClearHardwareWatchpoint(
    uint32_t wp_index) {
  Log *log = GetLog(LLDBLog::Watchpoints);
  LLDB_LOG(log, "wp_index: {0}", wp_index);

  // Read hardware breakpoint and watchpoint information.
  llvm::Error error = ReadHardwareDebugInfo();
  if (error) {
    LLDB_LOG_ERROR(
        log, std::move(error),
        "unable to clear watchpoint: failed to read debug registers: {0}");
    return false;
  }

  if (wp_index >= m_max_hwp_supported)
    return false;

  // Create a backup we can revert to in case of failure.
  lldb::addr_t tempAddr = m_hwp_regs[wp_index].address;
  uint32_t tempControl = m_hwp_regs[wp_index].control;

  // Update watchpoint in local cache
  m_hwp_regs[wp_index].control &= ~g_enable_bit;
  m_hwp_regs[wp_index].address = 0;

  // Ptrace call to update hardware debug registers
  error = WriteHardwareDebugRegs(eDREGTypeWATCH);

  if (error) {
    m_hwp_regs[wp_index].control = tempControl;
    m_hwp_regs[wp_index].address = tempAddr;

    LLDB_LOG_ERROR(
        log, std::move(error),
        "unable to clear watchpoint: failed to write debug registers: {0}");
    return false;
  }

  return true;
}

Status NativeRegisterContextDBReg_arm64::ClearAllHardwareWatchpoints() {
  // Read hardware breakpoint and watchpoint information.
  llvm::Error error = ReadHardwareDebugInfo();
  if (error)
    return Status(std::move(error));

  for (uint32_t i = 0; i < m_max_hwp_supported; i++) {
    if (WatchpointIsEnabled(i)) {
      // Create a backup we can revert to in case of failure.
      lldb::addr_t tempAddr = m_hwp_regs[i].address;
      uint32_t tempControl = m_hwp_regs[i].control;

      // Clear watchpoints in local cache
      m_hwp_regs[i].control &= ~g_enable_bit;
      m_hwp_regs[i].address = 0;

      // Ptrace call to update hardware debug registers
      error = WriteHardwareDebugRegs(eDREGTypeWATCH);

      if (error) {
        m_hwp_regs[i].control = tempControl;
        m_hwp_regs[i].address = tempAddr;

        return Status(std::move(error));
      }
    }
  }

  return Status();
}

uint32_t
NativeRegisterContextDBReg_arm64::GetWatchpointSize(uint32_t wp_index) {
  Log *log = GetLog(LLDBLog::Watchpoints);
  LLDB_LOG(log, "wp_index: {0}", wp_index);

  switch ((m_hwp_regs[wp_index].control >> 5) & 0xff) {
  case 0x01:
    return 1;
  case 0x03:
    return 2;
  case 0x0f:
    return 4;
  case 0xff:
    return 8;
  default:
    return 0;
  }
}

bool NativeRegisterContextDBReg_arm64::WatchpointIsEnabled(uint32_t wp_index) {
  Log *log = GetLog(LLDBLog::Watchpoints);
  LLDB_LOG(log, "wp_index: {0}", wp_index);

  if ((m_hwp_regs[wp_index].control & g_enable_bit) != 0)
    return true;
  else
    return false;
}

Status NativeRegisterContextDBReg_arm64::GetWatchpointHitIndex(
    uint32_t &wp_index, lldb::addr_t trap_addr) {
  Log *log = GetLog(LLDBLog::Watchpoints);
  LLDB_LOG(log, "wp_index: {0}, trap_addr: {1:x}", wp_index, trap_addr);

  // Read hardware breakpoint and watchpoint information.
  llvm::Error error = ReadHardwareDebugInfo();
  if (error)
    return Status(std::move(error));

  // Mask off ignored bits from watchpoint trap address.
  trap_addr = FixWatchpointHitAddress(trap_addr);

  uint32_t watch_size;
  lldb::addr_t watch_addr;

  for (wp_index = 0; wp_index < m_max_hwp_supported; ++wp_index) {
    watch_size = GetWatchpointSize(wp_index);
    watch_addr = m_hwp_regs[wp_index].address;

    if (WatchpointIsEnabled(wp_index) && trap_addr >= watch_addr &&
        trap_addr < watch_addr + watch_size) {
      m_hwp_regs[wp_index].hit_addr = trap_addr;
      return Status();
    }
  }

  wp_index = LLDB_INVALID_INDEX32;
  return Status();
}

lldb::addr_t
NativeRegisterContextDBReg_arm64::GetWatchpointAddress(uint32_t wp_index) {
  Log *log = GetLog(LLDBLog::Watchpoints);
  LLDB_LOG(log, "wp_index: {0}", wp_index);

  if (wp_index >= m_max_hwp_supported)
    return LLDB_INVALID_ADDRESS;

  if (WatchpointIsEnabled(wp_index))
    return m_hwp_regs[wp_index].real_addr;
  return LLDB_INVALID_ADDRESS;
}

lldb::addr_t
NativeRegisterContextDBReg_arm64::GetWatchpointHitAddress(uint32_t wp_index) {
  Log *log = GetLog(LLDBLog::Watchpoints);
  LLDB_LOG(log, "wp_index: {0}", wp_index);

  if (wp_index >= m_max_hwp_supported)
    return LLDB_INVALID_ADDRESS;

  if (WatchpointIsEnabled(wp_index))
    return m_hwp_regs[wp_index].hit_addr;
  return LLDB_INVALID_ADDRESS;
}
