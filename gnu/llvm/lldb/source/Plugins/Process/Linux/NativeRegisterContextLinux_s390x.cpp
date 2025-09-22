//===-- NativeRegisterContextLinux_s390x.cpp ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#if defined(__s390x__) && defined(__linux__)

#include "NativeRegisterContextLinux_s390x.h"
#include "Plugins/Process/Linux/NativeProcessLinux.h"
#include "Plugins/Process/Utility/RegisterContextLinux_s390x.h"
#include "lldb/Host/HostInfo.h"
#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/RegisterValue.h"
#include "lldb/Utility/Status.h"
#include <sys/ptrace.h>
#include <sys/uio.h>

using namespace lldb_private;
using namespace lldb_private::process_linux;

// Private namespace.

namespace {
// s390x 64-bit general purpose registers.
static const uint32_t g_gpr_regnums_s390x[] = {
    lldb_r0_s390x,      lldb_r1_s390x,    lldb_r2_s390x,    lldb_r3_s390x,
    lldb_r4_s390x,      lldb_r5_s390x,    lldb_r6_s390x,    lldb_r7_s390x,
    lldb_r8_s390x,      lldb_r9_s390x,    lldb_r10_s390x,   lldb_r11_s390x,
    lldb_r12_s390x,     lldb_r13_s390x,   lldb_r14_s390x,   lldb_r15_s390x,
    lldb_acr0_s390x,    lldb_acr1_s390x,  lldb_acr2_s390x,  lldb_acr3_s390x,
    lldb_acr4_s390x,    lldb_acr5_s390x,  lldb_acr6_s390x,  lldb_acr7_s390x,
    lldb_acr8_s390x,    lldb_acr9_s390x,  lldb_acr10_s390x, lldb_acr11_s390x,
    lldb_acr12_s390x,   lldb_acr13_s390x, lldb_acr14_s390x, lldb_acr15_s390x,
    lldb_pswm_s390x,    lldb_pswa_s390x,
    LLDB_INVALID_REGNUM // register sets need to end with this flag
};
static_assert((sizeof(g_gpr_regnums_s390x) / sizeof(g_gpr_regnums_s390x[0])) -
                      1 ==
                  k_num_gpr_registers_s390x,
              "g_gpr_regnums_s390x has wrong number of register infos");

// s390x 64-bit floating point registers.
static const uint32_t g_fpu_regnums_s390x[] = {
    lldb_f0_s390x,      lldb_f1_s390x,  lldb_f2_s390x,  lldb_f3_s390x,
    lldb_f4_s390x,      lldb_f5_s390x,  lldb_f6_s390x,  lldb_f7_s390x,
    lldb_f8_s390x,      lldb_f9_s390x,  lldb_f10_s390x, lldb_f11_s390x,
    lldb_f12_s390x,     lldb_f13_s390x, lldb_f14_s390x, lldb_f15_s390x,
    lldb_fpc_s390x,
    LLDB_INVALID_REGNUM // register sets need to end with this flag
};
static_assert((sizeof(g_fpu_regnums_s390x) / sizeof(g_fpu_regnums_s390x[0])) -
                      1 ==
                  k_num_fpr_registers_s390x,
              "g_fpu_regnums_s390x has wrong number of register infos");

// s390x Linux operating-system information.
static const uint32_t g_linux_regnums_s390x[] = {
    lldb_orig_r2_s390x, lldb_last_break_s390x, lldb_system_call_s390x,
    LLDB_INVALID_REGNUM // register sets need to end with this flag
};
static_assert((sizeof(g_linux_regnums_s390x) /
               sizeof(g_linux_regnums_s390x[0])) -
                      1 ==
                  k_num_linux_registers_s390x,
              "g_linux_regnums_s390x has wrong number of register infos");

// Number of register sets provided by this context.
enum { k_num_register_sets = 3 };

// Register sets for s390x 64-bit.
static const RegisterSet g_reg_sets_s390x[k_num_register_sets] = {
    {"General Purpose Registers", "gpr", k_num_gpr_registers_s390x,
     g_gpr_regnums_s390x},
    {"Floating Point Registers", "fpr", k_num_fpr_registers_s390x,
     g_fpu_regnums_s390x},
    {"Linux Operating System Data", "linux", k_num_linux_registers_s390x,
     g_linux_regnums_s390x},
};
}

#define REG_CONTEXT_SIZE (sizeof(s390_regs) + sizeof(s390_fp_regs) + 4)

// Required ptrace defines.

#define NT_S390_LAST_BREAK 0x306  /* s390 breaking event address */
#define NT_S390_SYSTEM_CALL 0x307 /* s390 system call restart data */

std::unique_ptr<NativeRegisterContextLinux>
NativeRegisterContextLinux::CreateHostNativeRegisterContextLinux(
    const ArchSpec &target_arch, NativeThreadLinux &native_thread) {
  return std::make_unique<NativeRegisterContextLinux_s390x>(target_arch,
                                                             native_thread);
}

llvm::Expected<ArchSpec>
NativeRegisterContextLinux::DetermineArchitecture(lldb::tid_t tid) {
  return HostInfo::GetArchitecture();
}

// NativeRegisterContextLinux_s390x members.

static RegisterInfoInterface *
CreateRegisterInfoInterface(const ArchSpec &target_arch) {
  assert((HostInfo::GetArchitecture().GetAddressByteSize() == 8) &&
         "Register setting path assumes this is a 64-bit host");
  return new RegisterContextLinux_s390x(target_arch);
}

NativeRegisterContextLinux_s390x::NativeRegisterContextLinux_s390x(
    const ArchSpec &target_arch, NativeThreadProtocol &native_thread)
    : NativeRegisterContextRegisterInfo(
          native_thread, CreateRegisterInfoInterface(target_arch)),
      NativeRegisterContextLinux(native_thread) {
  // Set up data about ranges of valid registers.
  switch (target_arch.GetMachine()) {
  case llvm::Triple::systemz:
    m_reg_info.num_registers = k_num_registers_s390x;
    m_reg_info.num_gpr_registers = k_num_gpr_registers_s390x;
    m_reg_info.num_fpr_registers = k_num_fpr_registers_s390x;
    m_reg_info.last_gpr = k_last_gpr_s390x;
    m_reg_info.first_fpr = k_first_fpr_s390x;
    m_reg_info.last_fpr = k_last_fpr_s390x;
    break;
  default:
    assert(false && "Unhandled target architecture.");
    break;
  }

  // Clear out the watchpoint state.
  m_watchpoint_addr = LLDB_INVALID_ADDRESS;
}

uint32_t NativeRegisterContextLinux_s390x::GetRegisterSetCount() const {
  uint32_t sets = 0;
  for (uint32_t set_index = 0; set_index < k_num_register_sets; ++set_index) {
    if (IsRegisterSetAvailable(set_index))
      ++sets;
  }

  return sets;
}

uint32_t NativeRegisterContextLinux_s390x::GetUserRegisterCount() const {
  uint32_t count = 0;
  for (uint32_t set_index = 0; set_index < k_num_register_sets; ++set_index) {
    const RegisterSet *set = GetRegisterSet(set_index);
    if (set)
      count += set->num_registers;
  }
  return count;
}

const RegisterSet *
NativeRegisterContextLinux_s390x::GetRegisterSet(uint32_t set_index) const {
  if (!IsRegisterSetAvailable(set_index))
    return nullptr;

  switch (GetRegisterInfoInterface().GetTargetArchitecture().GetMachine()) {
  case llvm::Triple::systemz:
    return &g_reg_sets_s390x[set_index];
  default:
    assert(false && "Unhandled target architecture.");
    return nullptr;
  }

  return nullptr;
}

bool NativeRegisterContextLinux_s390x::IsRegisterSetAvailable(
    uint32_t set_index) const {
  return set_index < k_num_register_sets;
}

bool NativeRegisterContextLinux_s390x::IsGPR(uint32_t reg_index) const {
  // GPRs come first.  "orig_r2" counts as GPR since it is part of the GPR
  // register area.
  return reg_index <= m_reg_info.last_gpr || reg_index == lldb_orig_r2_s390x;
}

bool NativeRegisterContextLinux_s390x::IsFPR(uint32_t reg_index) const {
  return (m_reg_info.first_fpr <= reg_index &&
          reg_index <= m_reg_info.last_fpr);
}

Status
NativeRegisterContextLinux_s390x::ReadRegister(const RegisterInfo *reg_info,
                                               RegisterValue &reg_value) {
  if (!reg_info)
    return Status("reg_info NULL");

  const uint32_t reg = reg_info->kinds[lldb::eRegisterKindLLDB];
  if (reg == LLDB_INVALID_REGNUM)
    return Status("register \"%s\" is an internal-only lldb register, cannot "
                  "read directly",
                  reg_info->name);

  if (IsGPR(reg)) {
    Status error = ReadGPR();
    if (error.Fail())
      return error;

    uint8_t *src = (uint8_t *)&m_regs + reg_info->byte_offset;
    assert(reg_info->byte_offset + reg_info->byte_size <= sizeof(m_regs));
    switch (reg_info->byte_size) {
    case 4:
      reg_value.SetUInt32(*(uint32_t *)src);
      break;
    case 8:
      reg_value.SetUInt64(*(uint64_t *)src);
      break;
    default:
      assert(false && "Unhandled data size.");
      return Status("unhandled byte size: %" PRIu32, reg_info->byte_size);
    }
    return Status();
  }

  if (IsFPR(reg)) {
    Status error = ReadFPR();
    if (error.Fail())
      return error;

    // byte_offset is just the offset within FPR, not the whole user area.
    uint8_t *src = (uint8_t *)&m_fp_regs + reg_info->byte_offset;
    assert(reg_info->byte_offset + reg_info->byte_size <= sizeof(m_fp_regs));
    switch (reg_info->byte_size) {
    case 4:
      reg_value.SetUInt32(*(uint32_t *)src);
      break;
    case 8:
      reg_value.SetUInt64(*(uint64_t *)src);
      break;
    default:
      assert(false && "Unhandled data size.");
      return Status("unhandled byte size: %" PRIu32, reg_info->byte_size);
    }
    return Status();
  }

  if (reg == lldb_last_break_s390x) {
    uint64_t last_break;
    Status error = DoReadRegisterSet(NT_S390_LAST_BREAK, &last_break, 8);
    if (error.Fail())
      return error;

    reg_value.SetUInt64(last_break);
    return Status();
  }

  if (reg == lldb_system_call_s390x) {
    uint32_t system_call;
    Status error = DoReadRegisterSet(NT_S390_SYSTEM_CALL, &system_call, 4);
    if (error.Fail())
      return error;

    reg_value.SetUInt32(system_call);
    return Status();
  }

  return Status("failed - register wasn't recognized");
}

Status NativeRegisterContextLinux_s390x::WriteRegister(
    const RegisterInfo *reg_info, const RegisterValue &reg_value) {
  if (!reg_info)
    return Status("reg_info NULL");

  const uint32_t reg = reg_info->kinds[lldb::eRegisterKindLLDB];
  if (reg == LLDB_INVALID_REGNUM)
    return Status("register \"%s\" is an internal-only lldb register, cannot "
                  "write directly",
                  reg_info->name);

  if (IsGPR(reg)) {
    Status error = ReadGPR();
    if (error.Fail())
      return error;

    uint8_t *dst = (uint8_t *)&m_regs + reg_info->byte_offset;
    assert(reg_info->byte_offset + reg_info->byte_size <= sizeof(m_regs));
    switch (reg_info->byte_size) {
    case 4:
      *(uint32_t *)dst = reg_value.GetAsUInt32();
      break;
    case 8:
      *(uint64_t *)dst = reg_value.GetAsUInt64();
      break;
    default:
      assert(false && "Unhandled data size.");
      return Status("unhandled byte size: %" PRIu32, reg_info->byte_size);
    }
    return WriteGPR();
  }

  if (IsFPR(reg)) {
    Status error = ReadFPR();
    if (error.Fail())
      return error;

    // byte_offset is just the offset within fp_regs, not the whole user area.
    uint8_t *dst = (uint8_t *)&m_fp_regs + reg_info->byte_offset;
    assert(reg_info->byte_offset + reg_info->byte_size <= sizeof(m_fp_regs));
    switch (reg_info->byte_size) {
    case 4:
      *(uint32_t *)dst = reg_value.GetAsUInt32();
      break;
    case 8:
      *(uint64_t *)dst = reg_value.GetAsUInt64();
      break;
    default:
      assert(false && "Unhandled data size.");
      return Status("unhandled byte size: %" PRIu32, reg_info->byte_size);
    }
    return WriteFPR();
  }

  if (reg == lldb_last_break_s390x) {
    return Status("The last break address is read-only");
  }

  if (reg == lldb_system_call_s390x) {
    uint32_t system_call = reg_value.GetAsUInt32();
    return DoWriteRegisterSet(NT_S390_SYSTEM_CALL, &system_call, 4);
  }

  return Status("failed - register wasn't recognized");
}

Status NativeRegisterContextLinux_s390x::ReadAllRegisterValues(
    lldb::WritableDataBufferSP &data_sp) {
  Status error;

  data_sp.reset(new DataBufferHeap(REG_CONTEXT_SIZE, 0));
  uint8_t *dst = data_sp->GetBytes();
  error = ReadGPR();
  if (error.Fail())
    return error;
  memcpy(dst, GetGPRBuffer(), GetGPRSize());
  dst += GetGPRSize();

  error = ReadFPR();
  if (error.Fail())
    return error;
  memcpy(dst, GetFPRBuffer(), GetFPRSize());
  dst += GetFPRSize();

  // Ignore errors if the regset is unsupported (happens on older kernels).
  DoReadRegisterSet(NT_S390_SYSTEM_CALL, dst, 4);
  dst += 4;

  // To enable inferior function calls while the process is stopped in an
  // interrupted system call, we need to clear the system call flag. It will be
  // restored to its original value by WriteAllRegisterValues. Again we ignore
  // error if the regset is unsupported.
  uint32_t system_call = 0;
  DoWriteRegisterSet(NT_S390_SYSTEM_CALL, &system_call, 4);

  return error;
}

Status NativeRegisterContextLinux_s390x::WriteAllRegisterValues(
    const lldb::DataBufferSP &data_sp) {
  Status error;

  if (!data_sp) {
    error.SetErrorStringWithFormat(
        "NativeRegisterContextLinux_s390x::%s invalid data_sp provided",
        __FUNCTION__);
    return error;
  }

  if (data_sp->GetByteSize() != REG_CONTEXT_SIZE) {
    error.SetErrorStringWithFormat(
        "NativeRegisterContextLinux_s390x::%s data_sp contained mismatched "
        "data size, expected %" PRIu64 ", actual %" PRIu64,
        __FUNCTION__, REG_CONTEXT_SIZE, data_sp->GetByteSize());
    return error;
  }

  const uint8_t *src = data_sp->GetBytes();
  if (src == nullptr) {
    error.SetErrorStringWithFormat("NativeRegisterContextLinux_s390x::%s "
                                   "DataBuffer::GetBytes() returned a null "
                                   "pointer",
                                   __FUNCTION__);
    return error;
  }

  memcpy(GetGPRBuffer(), src, GetGPRSize());
  src += GetGPRSize();
  error = WriteGPR();
  if (error.Fail())
    return error;

  memcpy(GetFPRBuffer(), src, GetFPRSize());
  src += GetFPRSize();
  error = WriteFPR();
  if (error.Fail())
    return error;

  // Ignore errors if the regset is unsupported (happens on older kernels).
  DoWriteRegisterSet(NT_S390_SYSTEM_CALL, src, 4);
  src += 4;

  return error;
}

Status NativeRegisterContextLinux_s390x::DoReadRegisterValue(
    uint32_t offset, const char *reg_name, uint32_t size,
    RegisterValue &value) {
  return Status("DoReadRegisterValue unsupported");
}

Status NativeRegisterContextLinux_s390x::DoWriteRegisterValue(
    uint32_t offset, const char *reg_name, const RegisterValue &value) {
  return Status("DoWriteRegisterValue unsupported");
}

Status NativeRegisterContextLinux_s390x::PeekUserArea(uint32_t offset,
                                                      void *buf,
                                                      size_t buf_size) {
  ptrace_area parea;
  parea.len = buf_size;
  parea.process_addr = (addr_t)buf;
  parea.kernel_addr = offset;

  return NativeProcessLinux::PtraceWrapper(PTRACE_PEEKUSR_AREA,
                                           m_thread.GetID(), &parea);
}

Status NativeRegisterContextLinux_s390x::PokeUserArea(uint32_t offset,
                                                      const void *buf,
                                                      size_t buf_size) {
  ptrace_area parea;
  parea.len = buf_size;
  parea.process_addr = (addr_t)buf;
  parea.kernel_addr = offset;

  return NativeProcessLinux::PtraceWrapper(PTRACE_POKEUSR_AREA,
                                           m_thread.GetID(), &parea);
}

Status NativeRegisterContextLinux_s390x::ReadGPR() {
  return PeekUserArea(offsetof(user_regs_struct, psw), GetGPRBuffer(),
                      GetGPRSize());
}

Status NativeRegisterContextLinux_s390x::WriteGPR() {
  return PokeUserArea(offsetof(user_regs_struct, psw), GetGPRBuffer(),
                      GetGPRSize());
}

Status NativeRegisterContextLinux_s390x::ReadFPR() {
  return PeekUserArea(offsetof(user_regs_struct, fp_regs), GetGPRBuffer(),
                      GetGPRSize());
}

Status NativeRegisterContextLinux_s390x::WriteFPR() {
  return PokeUserArea(offsetof(user_regs_struct, fp_regs), GetGPRBuffer(),
                      GetGPRSize());
}

Status NativeRegisterContextLinux_s390x::DoReadRegisterSet(uint32_t regset,
                                                           void *buf,
                                                           size_t buf_size) {
  struct iovec iov;
  iov.iov_base = buf;
  iov.iov_len = buf_size;

  return ReadRegisterSet(&iov, buf_size, regset);
}

Status NativeRegisterContextLinux_s390x::DoWriteRegisterSet(uint32_t regset,
                                                            const void *buf,
                                                            size_t buf_size) {
  struct iovec iov;
  iov.iov_base = const_cast<void *>(buf);
  iov.iov_len = buf_size;

  return WriteRegisterSet(&iov, buf_size, regset);
}

Status NativeRegisterContextLinux_s390x::IsWatchpointHit(uint32_t wp_index,
                                                         bool &is_hit) {
  per_lowcore_bits per_lowcore;

  if (wp_index >= NumSupportedHardwareWatchpoints())
    return Status("Watchpoint index out of range");

  if (m_watchpoint_addr == LLDB_INVALID_ADDRESS) {
    is_hit = false;
    return Status();
  }

  Status error = PeekUserArea(offsetof(user_regs_struct, per_info.lowcore),
                              &per_lowcore, sizeof(per_lowcore));
  if (error.Fail()) {
    is_hit = false;
    return error;
  }

  is_hit = (per_lowcore.perc_storage_alteration == 1 &&
            per_lowcore.perc_store_real_address == 0);

  if (is_hit) {
    // Do not report this watchpoint again.
    memset(&per_lowcore, 0, sizeof(per_lowcore));
    PokeUserArea(offsetof(user_regs_struct, per_info.lowcore), &per_lowcore,
                 sizeof(per_lowcore));
  }

  return Status();
}

Status NativeRegisterContextLinux_s390x::GetWatchpointHitIndex(
    uint32_t &wp_index, lldb::addr_t trap_addr) {
  uint32_t num_hw_wps = NumSupportedHardwareWatchpoints();
  for (wp_index = 0; wp_index < num_hw_wps; ++wp_index) {
    bool is_hit;
    Status error = IsWatchpointHit(wp_index, is_hit);
    if (error.Fail()) {
      wp_index = LLDB_INVALID_INDEX32;
      return error;
    } else if (is_hit) {
      return error;
    }
  }
  wp_index = LLDB_INVALID_INDEX32;
  return Status();
}

Status NativeRegisterContextLinux_s390x::IsWatchpointVacant(uint32_t wp_index,
                                                            bool &is_vacant) {
  if (wp_index >= NumSupportedHardwareWatchpoints())
    return Status("Watchpoint index out of range");

  is_vacant = m_watchpoint_addr == LLDB_INVALID_ADDRESS;

  return Status();
}

bool NativeRegisterContextLinux_s390x::ClearHardwareWatchpoint(
    uint32_t wp_index) {
  per_struct per_info;

  if (wp_index >= NumSupportedHardwareWatchpoints())
    return false;

  Status error = PeekUserArea(offsetof(user_regs_struct, per_info), &per_info,
                              sizeof(per_info));
  if (error.Fail())
    return false;

  per_info.control_regs.bits.em_storage_alteration = 0;
  per_info.control_regs.bits.storage_alt_space_ctl = 0;
  per_info.starting_addr = 0;
  per_info.ending_addr = 0;

  error = PokeUserArea(offsetof(user_regs_struct, per_info), &per_info,
                       sizeof(per_info));
  if (error.Fail())
    return false;

  m_watchpoint_addr = LLDB_INVALID_ADDRESS;
  return true;
}

Status NativeRegisterContextLinux_s390x::ClearAllHardwareWatchpoints() {
  if (ClearHardwareWatchpoint(0))
    return Status();
  return Status("Clearing all hardware watchpoints failed.");
}

uint32_t NativeRegisterContextLinux_s390x::SetHardwareWatchpoint(
    lldb::addr_t addr, size_t size, uint32_t watch_flags) {
  per_struct per_info;

  if (watch_flags != 0x1)
    return LLDB_INVALID_INDEX32;

  if (m_watchpoint_addr != LLDB_INVALID_ADDRESS)
    return LLDB_INVALID_INDEX32;

  Status error = PeekUserArea(offsetof(user_regs_struct, per_info), &per_info,
                              sizeof(per_info));
  if (error.Fail())
    return LLDB_INVALID_INDEX32;

  per_info.control_regs.bits.em_storage_alteration = 1;
  per_info.control_regs.bits.storage_alt_space_ctl = 1;
  per_info.starting_addr = addr;
  per_info.ending_addr = addr + size - 1;

  error = PokeUserArea(offsetof(user_regs_struct, per_info), &per_info,
                       sizeof(per_info));
  if (error.Fail())
    return LLDB_INVALID_INDEX32;

  m_watchpoint_addr = addr;
  return 0;
}

lldb::addr_t
NativeRegisterContextLinux_s390x::GetWatchpointAddress(uint32_t wp_index) {
  if (wp_index >= NumSupportedHardwareWatchpoints())
    return LLDB_INVALID_ADDRESS;
  return m_watchpoint_addr;
}

uint32_t NativeRegisterContextLinux_s390x::NumSupportedHardwareWatchpoints() {
  return 1;
}

#endif // defined(__s390x__) && defined(__linux__)
