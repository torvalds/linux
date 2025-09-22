//===-- NativeRegisterContextLinux_arm.cpp --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#if defined(__arm__) || defined(__arm64__) || defined(__aarch64__)

#include "NativeRegisterContextLinux_arm.h"

#include "Plugins/Process/Linux/NativeProcessLinux.h"
#include "Plugins/Process/Linux/Procfs.h"
#include "Plugins/Process/POSIX/ProcessPOSIXLog.h"
#include "Plugins/Process/Utility/RegisterInfoPOSIX_arm.h"
#include "lldb/Host/HostInfo.h"
#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/RegisterValue.h"
#include "lldb/Utility/Status.h"

#include <elf.h>
#include <sys/uio.h>

#define REG_CONTEXT_SIZE (GetGPRSize() + sizeof(m_fpr))

#ifndef PTRACE_GETVFPREGS
#define PTRACE_GETVFPREGS 27
#define PTRACE_SETVFPREGS 28
#endif
#ifndef PTRACE_GETHBPREGS
#define PTRACE_GETHBPREGS 29
#define PTRACE_SETHBPREGS 30
#endif
#if !defined(PTRACE_TYPE_ARG3)
#define PTRACE_TYPE_ARG3 void *
#endif
#if !defined(PTRACE_TYPE_ARG4)
#define PTRACE_TYPE_ARG4 void *
#endif

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::process_linux;

#if defined(__arm__)

std::unique_ptr<NativeRegisterContextLinux>
NativeRegisterContextLinux::CreateHostNativeRegisterContextLinux(
    const ArchSpec &target_arch, NativeThreadLinux &native_thread) {
  return std::make_unique<NativeRegisterContextLinux_arm>(target_arch,
                                                           native_thread);
}

llvm::Expected<ArchSpec>
NativeRegisterContextLinux::DetermineArchitecture(lldb::tid_t tid) {
  return HostInfo::GetArchitecture();
}

#endif // defined(__arm__)

NativeRegisterContextLinux_arm::NativeRegisterContextLinux_arm(
    const ArchSpec &target_arch, NativeThreadProtocol &native_thread)
    : NativeRegisterContextRegisterInfo(native_thread,
                                        new RegisterInfoPOSIX_arm(target_arch)),
      NativeRegisterContextLinux(native_thread) {
  assert(target_arch.GetMachine() == llvm::Triple::arm);

  ::memset(&m_fpr, 0, sizeof(m_fpr));
  ::memset(&m_gpr_arm, 0, sizeof(m_gpr_arm));
  ::memset(&m_hwp_regs, 0, sizeof(m_hwp_regs));
  ::memset(&m_hbr_regs, 0, sizeof(m_hbr_regs));

  // 16 is just a maximum value, query hardware for actual watchpoint count
  m_max_hwp_supported = 16;
  m_max_hbp_supported = 16;
  m_refresh_hwdebug_info = true;
}

RegisterInfoPOSIX_arm &NativeRegisterContextLinux_arm::GetRegisterInfo() const {
  return static_cast<RegisterInfoPOSIX_arm &>(*m_register_info_interface_up);
}

uint32_t NativeRegisterContextLinux_arm::GetRegisterSetCount() const {
  return GetRegisterInfo().GetRegisterSetCount();
}

uint32_t NativeRegisterContextLinux_arm::GetUserRegisterCount() const {
  uint32_t count = 0;
  for (uint32_t set_index = 0; set_index < GetRegisterSetCount(); ++set_index)
    count += GetRegisterSet(set_index)->num_registers;
  return count;
}

const RegisterSet *
NativeRegisterContextLinux_arm::GetRegisterSet(uint32_t set_index) const {
  return GetRegisterInfo().GetRegisterSet(set_index);
}

Status
NativeRegisterContextLinux_arm::ReadRegister(const RegisterInfo *reg_info,
                                             RegisterValue &reg_value) {
  Status error;

  if (!reg_info) {
    error.SetErrorString("reg_info NULL");
    return error;
  }

  const uint32_t reg = reg_info->kinds[lldb::eRegisterKindLLDB];

  if (IsFPR(reg)) {
    error = ReadFPR();
    if (error.Fail())
      return error;
  } else {
    uint32_t full_reg = reg;
    bool is_subreg = reg_info->invalidate_regs &&
                     (reg_info->invalidate_regs[0] != LLDB_INVALID_REGNUM);

    if (is_subreg) {
      // Read the full aligned 64-bit register.
      full_reg = reg_info->invalidate_regs[0];
    }

    error = ReadRegisterRaw(full_reg, reg_value);

    if (error.Success()) {
      // If our read was not aligned (for ah,bh,ch,dh), shift our returned
      // value one byte to the right.
      if (is_subreg && (reg_info->byte_offset & 0x1))
        reg_value.SetUInt64(reg_value.GetAsUInt64() >> 8);

      // If our return byte size was greater than the return value reg size,
      // then use the type specified by reg_info rather than the uint64_t
      // default
      if (reg_value.GetByteSize() > reg_info->byte_size)
        reg_value.SetType(*reg_info);
    }
    return error;
  }

  // Get pointer to m_fpr variable and set the data from it.
  uint32_t fpr_offset = CalculateFprOffset(reg_info);
  assert(fpr_offset < sizeof m_fpr);
  uint8_t *src = (uint8_t *)&m_fpr + fpr_offset;
  switch (reg_info->byte_size) {
  case 2:
    reg_value.SetUInt16(*(uint16_t *)src);
    break;
  case 4:
    reg_value.SetUInt32(*(uint32_t *)src);
    break;
  case 8:
    reg_value.SetUInt64(*(uint64_t *)src);
    break;
  case 16:
    reg_value.SetBytes(src, 16, GetByteOrder());
    break;
  default:
    assert(false && "Unhandled data size.");
    error.SetErrorStringWithFormat("unhandled byte size: %" PRIu32,
                                   reg_info->byte_size);
    break;
  }

  return error;
}

Status
NativeRegisterContextLinux_arm::WriteRegister(const RegisterInfo *reg_info,
                                              const RegisterValue &reg_value) {
  if (!reg_info)
    return Status("reg_info NULL");

  const uint32_t reg_index = reg_info->kinds[lldb::eRegisterKindLLDB];
  if (reg_index == LLDB_INVALID_REGNUM)
    return Status("no lldb regnum for %s", reg_info && reg_info->name
                                               ? reg_info->name
                                               : "<unknown register>");

  if (IsGPR(reg_index))
    return WriteRegisterRaw(reg_index, reg_value);

  if (IsFPR(reg_index)) {
    // Get pointer to m_fpr variable and set the data to it.
    uint32_t fpr_offset = CalculateFprOffset(reg_info);
    assert(fpr_offset < sizeof m_fpr);
    uint8_t *dst = (uint8_t *)&m_fpr + fpr_offset;
    ::memcpy(dst, reg_value.GetBytes(), reg_info->byte_size);

    return WriteFPR();
  }

  return Status("failed - register wasn't recognized to be a GPR or an FPR, "
                "write strategy unknown");
}

Status NativeRegisterContextLinux_arm::ReadAllRegisterValues(
    lldb::WritableDataBufferSP &data_sp) {
  Status error;

  data_sp.reset(new DataBufferHeap(REG_CONTEXT_SIZE, 0));
  error = ReadGPR();
  if (error.Fail())
    return error;

  error = ReadFPR();
  if (error.Fail())
    return error;

  uint8_t *dst = data_sp->GetBytes();
  ::memcpy(dst, &m_gpr_arm, GetGPRSize());
  dst += GetGPRSize();
  ::memcpy(dst, &m_fpr, sizeof(m_fpr));

  return error;
}

Status NativeRegisterContextLinux_arm::WriteAllRegisterValues(
    const lldb::DataBufferSP &data_sp) {
  Status error;

  if (!data_sp) {
    error.SetErrorStringWithFormat(
        "NativeRegisterContextLinux_arm::%s invalid data_sp provided",
        __FUNCTION__);
    return error;
  }

  if (data_sp->GetByteSize() != REG_CONTEXT_SIZE) {
    error.SetErrorStringWithFormat(
        "NativeRegisterContextLinux_arm::%s data_sp contained mismatched "
        "data size, expected %" PRIu64 ", actual %" PRIu64,
        __FUNCTION__, (uint64_t)REG_CONTEXT_SIZE, data_sp->GetByteSize());
    return error;
  }

  const uint8_t *src = data_sp->GetBytes();
  if (src == nullptr) {
    error.SetErrorStringWithFormat("NativeRegisterContextLinux_arm::%s "
                                   "DataBuffer::GetBytes() returned a null "
                                   "pointer",
                                   __FUNCTION__);
    return error;
  }
  ::memcpy(&m_gpr_arm, src, GetRegisterInfoInterface().GetGPRSize());

  error = WriteGPR();
  if (error.Fail())
    return error;

  src += GetRegisterInfoInterface().GetGPRSize();
  ::memcpy(&m_fpr, src, sizeof(m_fpr));

  error = WriteFPR();
  if (error.Fail())
    return error;

  return error;
}

bool NativeRegisterContextLinux_arm::IsGPR(unsigned reg) const {
  if (GetRegisterInfo().GetRegisterSetFromRegisterIndex(reg) ==
      RegisterInfoPOSIX_arm::GPRegSet)
    return true;
  return false;
}

bool NativeRegisterContextLinux_arm::IsFPR(unsigned reg) const {
  if (GetRegisterInfo().GetRegisterSetFromRegisterIndex(reg) ==
      RegisterInfoPOSIX_arm::FPRegSet)
    return true;
  return false;
}

uint32_t NativeRegisterContextLinux_arm::NumSupportedHardwareBreakpoints() {
  Log *log = GetLog(POSIXLog::Breakpoints);

  LLDB_LOGF(log, "NativeRegisterContextLinux_arm::%s()", __FUNCTION__);

  Status error;

  // Read hardware breakpoint and watchpoint information.
  error = ReadHardwareDebugInfo();

  if (error.Fail())
    return 0;

  LLDB_LOG(log, "{0}", m_max_hbp_supported);
  return m_max_hbp_supported;
}

uint32_t
NativeRegisterContextLinux_arm::SetHardwareBreakpoint(lldb::addr_t addr,
                                                      size_t size) {
  Log *log = GetLog(POSIXLog::Breakpoints);
  LLDB_LOG(log, "addr: {0:x}, size: {1:x}", addr, size);

  // Read hardware breakpoint and watchpoint information.
  Status error = ReadHardwareDebugInfo();

  if (error.Fail())
    return LLDB_INVALID_INDEX32;

  uint32_t control_value = 0, bp_index = 0;

  // Setup address and control values.
  // Use size to get a hint of arm vs thumb modes.
  switch (size) {
  case 2:
    control_value = (0x3 << 5) | 7;
    addr &= ~1;
    break;
  case 4:
    control_value = (0xfu << 5) | 7;
    addr &= ~3;
    break;
  default:
    return LLDB_INVALID_INDEX32;
  }

  // Iterate over stored breakpoints and find a free bp_index
  bp_index = LLDB_INVALID_INDEX32;
  for (uint32_t i = 0; i < m_max_hbp_supported; i++) {
    if ((m_hbr_regs[i].control & 1) == 0) {
      bp_index = i; // Mark last free slot
    } else if (m_hbr_regs[i].address == addr) {
      return LLDB_INVALID_INDEX32; // We do not support duplicate breakpoints.
    }
  }

  if (bp_index == LLDB_INVALID_INDEX32)
    return LLDB_INVALID_INDEX32;

  // Update breakpoint in local cache
  m_hbr_regs[bp_index].real_addr = addr;
  m_hbr_regs[bp_index].address = addr;
  m_hbr_regs[bp_index].control = control_value;

  // PTRACE call to set corresponding hardware breakpoint register.
  error = WriteHardwareDebugRegs(eDREGTypeBREAK, bp_index);

  if (error.Fail()) {
    m_hbr_regs[bp_index].address = 0;
    m_hbr_regs[bp_index].control &= ~1;

    return LLDB_INVALID_INDEX32;
  }

  return bp_index;
}

bool NativeRegisterContextLinux_arm::ClearHardwareBreakpoint(uint32_t hw_idx) {
  Log *log = GetLog(POSIXLog::Breakpoints);
  LLDB_LOG(log, "hw_idx: {0}", hw_idx);

  // Read hardware breakpoint and watchpoint information.
  Status error = ReadHardwareDebugInfo();

  if (error.Fail())
    return false;

  if (hw_idx >= m_max_hbp_supported)
    return false;

  // Create a backup we can revert to in case of failure.
  lldb::addr_t tempAddr = m_hbr_regs[hw_idx].address;
  uint32_t tempControl = m_hbr_regs[hw_idx].control;

  m_hbr_regs[hw_idx].control &= ~1;
  m_hbr_regs[hw_idx].address = 0;

  // PTRACE call to clear corresponding hardware breakpoint register.
  error = WriteHardwareDebugRegs(eDREGTypeBREAK, hw_idx);

  if (error.Fail()) {
    m_hbr_regs[hw_idx].control = tempControl;
    m_hbr_regs[hw_idx].address = tempAddr;

    return false;
  }

  return true;
}

Status NativeRegisterContextLinux_arm::GetHardwareBreakHitIndex(
    uint32_t &bp_index, lldb::addr_t trap_addr) {
  Log *log = GetLog(POSIXLog::Breakpoints);

  LLDB_LOGF(log, "NativeRegisterContextLinux_arm::%s()", __FUNCTION__);

  lldb::addr_t break_addr;

  for (bp_index = 0; bp_index < m_max_hbp_supported; ++bp_index) {
    break_addr = m_hbr_regs[bp_index].address;

    if ((m_hbr_regs[bp_index].control & 0x1) && (trap_addr == break_addr)) {
      m_hbr_regs[bp_index].hit_addr = trap_addr;
      return Status();
    }
  }

  bp_index = LLDB_INVALID_INDEX32;
  return Status();
}

Status NativeRegisterContextLinux_arm::ClearAllHardwareBreakpoints() {
  Log *log = GetLog(POSIXLog::Breakpoints);

  LLDB_LOGF(log, "NativeRegisterContextLinux_arm::%s()", __FUNCTION__);

  Status error;

  // Read hardware breakpoint and watchpoint information.
  error = ReadHardwareDebugInfo();

  if (error.Fail())
    return error;

  lldb::addr_t tempAddr = 0;
  uint32_t tempControl = 0;

  for (uint32_t i = 0; i < m_max_hbp_supported; i++) {
    if (m_hbr_regs[i].control & 0x01) {
      // Create a backup we can revert to in case of failure.
      tempAddr = m_hbr_regs[i].address;
      tempControl = m_hbr_regs[i].control;

      // Clear breakpoints in local cache
      m_hbr_regs[i].control &= ~1;
      m_hbr_regs[i].address = 0;

      // Ptrace call to update hardware debug registers
      error = WriteHardwareDebugRegs(eDREGTypeBREAK, i);

      if (error.Fail()) {
        m_hbr_regs[i].control = tempControl;
        m_hbr_regs[i].address = tempAddr;

        return error;
      }
    }
  }

  return Status();
}

uint32_t NativeRegisterContextLinux_arm::NumSupportedHardwareWatchpoints() {
  Log *log = GetLog(POSIXLog::Watchpoints);

  // Read hardware breakpoint and watchpoint information.
  Status error = ReadHardwareDebugInfo();

  if (error.Fail())
    return 0;

  LLDB_LOG(log, "{0}", m_max_hwp_supported);
  return m_max_hwp_supported;
}

uint32_t NativeRegisterContextLinux_arm::SetHardwareWatchpoint(
    lldb::addr_t addr, size_t size, uint32_t watch_flags) {
  Log *log = GetLog(POSIXLog::Watchpoints);
  LLDB_LOG(log, "addr: {0:x}, size: {1:x} watch_flags: {2:x}", addr, size,
           watch_flags);

  // Read hardware breakpoint and watchpoint information.
  Status error = ReadHardwareDebugInfo();

  if (error.Fail())
    return LLDB_INVALID_INDEX32;

  uint32_t control_value = 0, wp_index = 0, addr_word_offset = 0, byte_mask = 0;
  lldb::addr_t real_addr = addr;

  // Check if we are setting watchpoint other than read/write/access Also
  // update watchpoint flag to match Arm write-read bit configuration.
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

  // Can't watch zero bytes
  // Can't watch more than 4 bytes per WVR/WCR pair

  if (size == 0 || size > 4)
    return LLDB_INVALID_INDEX32;

  // Check 4-byte alignment for hardware watchpoint target address. Below is a
  // hack to recalculate address and size in order to make sure we can watch
  // non 4-byte aligned addresses as well.
  if (addr & 0x03) {
    uint8_t watch_mask = (addr & 0x03) + size;

    if (watch_mask > 0x04)
      return LLDB_INVALID_INDEX32;
    else if (watch_mask <= 0x02)
      size = 2;
    else
      size = 4;

    addr = addr & (~0x03);
  }

  // We can only watch up to four bytes that follow a 4 byte aligned address
  // per watchpoint register pair, so make sure we can properly encode this.
  addr_word_offset = addr % 4;
  byte_mask = ((1u << size) - 1u) << addr_word_offset;

  // Check if we need multiple watchpoint register
  if (byte_mask > 0xfu)
    return LLDB_INVALID_INDEX32;

  // Setup control value
  // Make the byte_mask into a valid Byte Address Select mask
  control_value = byte_mask << 5;

  // Turn on appropriate watchpoint flags read or write
  control_value |= (watch_flags << 3);

  // Enable this watchpoint and make it stop in privileged or user mode;
  control_value |= 7;

  // Make sure bits 1:0 are clear in our address
  addr &= ~((lldb::addr_t)3);

  // Iterate over stored watchpoints and find a free wp_index
  wp_index = LLDB_INVALID_INDEX32;
  for (uint32_t i = 0; i < m_max_hwp_supported; i++) {
    if ((m_hwp_regs[i].control & 1) == 0) {
      wp_index = i; // Mark last free slot
    } else if (m_hwp_regs[i].address == addr) {
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
  error = WriteHardwareDebugRegs(eDREGTypeWATCH, wp_index);

  if (error.Fail()) {
    m_hwp_regs[wp_index].address = 0;
    m_hwp_regs[wp_index].control &= ~1;

    return LLDB_INVALID_INDEX32;
  }

  return wp_index;
}

bool NativeRegisterContextLinux_arm::ClearHardwareWatchpoint(
    uint32_t wp_index) {
  Log *log = GetLog(POSIXLog::Watchpoints);
  LLDB_LOG(log, "wp_index: {0}", wp_index);

  // Read hardware breakpoint and watchpoint information.
  Status error = ReadHardwareDebugInfo();

  if (error.Fail())
    return false;

  if (wp_index >= m_max_hwp_supported)
    return false;

  // Create a backup we can revert to in case of failure.
  lldb::addr_t tempAddr = m_hwp_regs[wp_index].address;
  uint32_t tempControl = m_hwp_regs[wp_index].control;

  // Update watchpoint in local cache
  m_hwp_regs[wp_index].control &= ~1;
  m_hwp_regs[wp_index].address = 0;

  // Ptrace call to update hardware debug registers
  error = WriteHardwareDebugRegs(eDREGTypeWATCH, wp_index);

  if (error.Fail()) {
    m_hwp_regs[wp_index].control = tempControl;
    m_hwp_regs[wp_index].address = tempAddr;

    return false;
  }

  return true;
}

Status NativeRegisterContextLinux_arm::ClearAllHardwareWatchpoints() {
  // Read hardware breakpoint and watchpoint information.
  Status error = ReadHardwareDebugInfo();

  if (error.Fail())
    return error;

  lldb::addr_t tempAddr = 0;
  uint32_t tempControl = 0;

  for (uint32_t i = 0; i < m_max_hwp_supported; i++) {
    if (m_hwp_regs[i].control & 0x01) {
      // Create a backup we can revert to in case of failure.
      tempAddr = m_hwp_regs[i].address;
      tempControl = m_hwp_regs[i].control;

      // Clear watchpoints in local cache
      m_hwp_regs[i].control &= ~1;
      m_hwp_regs[i].address = 0;

      // Ptrace call to update hardware debug registers
      error = WriteHardwareDebugRegs(eDREGTypeWATCH, i);

      if (error.Fail()) {
        m_hwp_regs[i].control = tempControl;
        m_hwp_regs[i].address = tempAddr;

        return error;
      }
    }
  }

  return Status();
}

uint32_t NativeRegisterContextLinux_arm::GetWatchpointSize(uint32_t wp_index) {
  Log *log = GetLog(POSIXLog::Watchpoints);
  LLDB_LOG(log, "wp_index: {0}", wp_index);

  switch ((m_hwp_regs[wp_index].control >> 5) & 0x0f) {
  case 0x01:
    return 1;
  case 0x03:
    return 2;
  case 0x07:
    return 3;
  case 0x0f:
    return 4;
  default:
    return 0;
  }
}
bool NativeRegisterContextLinux_arm::WatchpointIsEnabled(uint32_t wp_index) {
  Log *log = GetLog(POSIXLog::Watchpoints);
  LLDB_LOG(log, "wp_index: {0}", wp_index);

  if ((m_hwp_regs[wp_index].control & 0x1) == 0x1)
    return true;
  else
    return false;
}

Status
NativeRegisterContextLinux_arm::GetWatchpointHitIndex(uint32_t &wp_index,
                                                      lldb::addr_t trap_addr) {
  Log *log = GetLog(POSIXLog::Watchpoints);
  LLDB_LOG(log, "wp_index: {0}, trap_addr: {1:x}", wp_index, trap_addr);

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
NativeRegisterContextLinux_arm::GetWatchpointAddress(uint32_t wp_index) {
  Log *log = GetLog(POSIXLog::Watchpoints);
  LLDB_LOG(log, "wp_index: {0}", wp_index);

  if (wp_index >= m_max_hwp_supported)
    return LLDB_INVALID_ADDRESS;

  if (WatchpointIsEnabled(wp_index))
    return m_hwp_regs[wp_index].real_addr;
  else
    return LLDB_INVALID_ADDRESS;
}

lldb::addr_t
NativeRegisterContextLinux_arm::GetWatchpointHitAddress(uint32_t wp_index) {
  Log *log = GetLog(POSIXLog::Watchpoints);
  LLDB_LOG(log, "wp_index: {0}", wp_index);

  if (wp_index >= m_max_hwp_supported)
    return LLDB_INVALID_ADDRESS;

  if (WatchpointIsEnabled(wp_index))
    return m_hwp_regs[wp_index].hit_addr;
  else
    return LLDB_INVALID_ADDRESS;
}

Status NativeRegisterContextLinux_arm::ReadHardwareDebugInfo() {
  Status error;

  if (!m_refresh_hwdebug_info) {
    return Status();
  }

  unsigned int cap_val;

  error = NativeProcessLinux::PtraceWrapper(PTRACE_GETHBPREGS, m_thread.GetID(),
                                            nullptr, &cap_val,
                                            sizeof(unsigned int));

  if (error.Fail())
    return error;

  m_max_hwp_supported = (cap_val >> 8) & 0xff;
  m_max_hbp_supported = cap_val & 0xff;
  m_refresh_hwdebug_info = false;

  return error;
}

Status NativeRegisterContextLinux_arm::WriteHardwareDebugRegs(int hwbType,
                                                              int hwb_index) {
  Status error;

  lldb::addr_t *addr_buf;
  uint32_t *ctrl_buf;

  if (hwbType == eDREGTypeWATCH) {
    addr_buf = &m_hwp_regs[hwb_index].address;
    ctrl_buf = &m_hwp_regs[hwb_index].control;

    error = NativeProcessLinux::PtraceWrapper(
        PTRACE_SETHBPREGS, m_thread.GetID(),
        (PTRACE_TYPE_ARG3)(intptr_t) - ((hwb_index << 1) + 1), addr_buf,
        sizeof(unsigned int));

    if (error.Fail())
      return error;

    error = NativeProcessLinux::PtraceWrapper(
        PTRACE_SETHBPREGS, m_thread.GetID(),
        (PTRACE_TYPE_ARG3)(intptr_t) - ((hwb_index << 1) + 2), ctrl_buf,
        sizeof(unsigned int));
  } else {
    addr_buf = &m_hbr_regs[hwb_index].address;
    ctrl_buf = &m_hbr_regs[hwb_index].control;

    error = NativeProcessLinux::PtraceWrapper(
        PTRACE_SETHBPREGS, m_thread.GetID(),
        (PTRACE_TYPE_ARG3)(intptr_t)((hwb_index << 1) + 1), addr_buf,
        sizeof(unsigned int));

    if (error.Fail())
      return error;

    error = NativeProcessLinux::PtraceWrapper(
        PTRACE_SETHBPREGS, m_thread.GetID(),
        (PTRACE_TYPE_ARG3)(intptr_t)((hwb_index << 1) + 2), ctrl_buf,
        sizeof(unsigned int));
  }

  return error;
}

uint32_t NativeRegisterContextLinux_arm::CalculateFprOffset(
    const RegisterInfo *reg_info) const {
  return reg_info->byte_offset - GetGPRSize();
}

Status NativeRegisterContextLinux_arm::DoReadRegisterValue(
    uint32_t offset, const char *reg_name, uint32_t size,
    RegisterValue &value) {
  // PTRACE_PEEKUSER don't work in the aarch64 linux kernel used on android
  // devices (always return "Bad address"). To avoid using PTRACE_PEEKUSER we
  // read out the full GPR register set instead. This approach is about 4 times
  // slower but the performance overhead is negligible in comparison to
  // processing time in lldb-server.
  assert(offset % 4 == 0 && "Try to write a register with unaligned offset");
  if (offset + sizeof(uint32_t) > sizeof(m_gpr_arm))
    return Status("Register isn't fit into the size of the GPR area");

  Status error = ReadGPR();
  if (error.Fail())
    return error;

  value.SetUInt32(m_gpr_arm[offset / sizeof(uint32_t)]);
  return Status();
}

Status NativeRegisterContextLinux_arm::DoWriteRegisterValue(
    uint32_t offset, const char *reg_name, const RegisterValue &value) {
  // PTRACE_POKEUSER don't work in the aarch64 linux kernel used on android
  // devices (always return "Bad address"). To avoid using PTRACE_POKEUSER we
  // read out the full GPR register set, modify the requested register and
  // write it back. This approach is about 4 times slower but the performance
  // overhead is negligible in comparison to processing time in lldb-server.
  assert(offset % 4 == 0 && "Try to write a register with unaligned offset");
  if (offset + sizeof(uint32_t) > sizeof(m_gpr_arm))
    return Status("Register isn't fit into the size of the GPR area");

  Status error = ReadGPR();
  if (error.Fail())
    return error;

  uint32_t reg_value = value.GetAsUInt32();
  // As precaution for an undefined behavior encountered while setting PC we
  // will clear thumb bit of new PC if we are already in thumb mode; that is
  // CPSR thumb mode bit is set.
  if (offset / sizeof(uint32_t) == gpr_pc_arm) {
    // Check if we are already in thumb mode and thumb bit of current PC is
    // read out to be zero and thumb bit of next PC is read out to be one.
    if ((m_gpr_arm[gpr_cpsr_arm] & 0x20) && !(m_gpr_arm[gpr_pc_arm] & 0x01) &&
        (value.GetAsUInt32() & 0x01)) {
      reg_value &= (~1ull);
    }
  }

  m_gpr_arm[offset / sizeof(uint32_t)] = reg_value;
  return WriteGPR();
}

Status NativeRegisterContextLinux_arm::ReadGPR() {
#ifdef __arm__
  return NativeRegisterContextLinux::ReadGPR();
#else  // __aarch64__
  struct iovec ioVec;
  ioVec.iov_base = GetGPRBuffer();
  ioVec.iov_len = GetGPRSize();

  return ReadRegisterSet(&ioVec, GetGPRSize(), NT_PRSTATUS);
#endif // __arm__
}

Status NativeRegisterContextLinux_arm::WriteGPR() {
#ifdef __arm__
  return NativeRegisterContextLinux::WriteGPR();
#else  // __aarch64__
  struct iovec ioVec;
  ioVec.iov_base = GetGPRBuffer();
  ioVec.iov_len = GetGPRSize();

  return WriteRegisterSet(&ioVec, GetGPRSize(), NT_PRSTATUS);
#endif // __arm__
}

Status NativeRegisterContextLinux_arm::ReadFPR() {
#ifdef __arm__
  return NativeProcessLinux::PtraceWrapper(PTRACE_GETVFPREGS, m_thread.GetID(),
                                           nullptr, GetFPRBuffer(),
                                           GetFPRSize());
#else  // __aarch64__
  struct iovec ioVec;
  ioVec.iov_base = GetFPRBuffer();
  ioVec.iov_len = GetFPRSize();

  return ReadRegisterSet(&ioVec, GetFPRSize(), NT_ARM_VFP);
#endif // __arm__
}

Status NativeRegisterContextLinux_arm::WriteFPR() {
#ifdef __arm__
  return NativeProcessLinux::PtraceWrapper(PTRACE_SETVFPREGS, m_thread.GetID(),
                                           nullptr, GetFPRBuffer(),
                                           GetFPRSize());
#else  // __aarch64__
  struct iovec ioVec;
  ioVec.iov_base = GetFPRBuffer();
  ioVec.iov_len = GetFPRSize();

  return WriteRegisterSet(&ioVec, GetFPRSize(), NT_ARM_VFP);
#endif // __arm__
}

#endif // defined(__arm__) || defined(__arm64__) || defined(__aarch64__)
