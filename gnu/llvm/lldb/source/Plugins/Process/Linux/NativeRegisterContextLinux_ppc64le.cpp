//===-- NativeRegisterContextLinux_ppc64le.cpp ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// This implementation is related to the OpenPOWER ABI for Power Architecture
// 64-bit ELF V2 ABI

#if defined(__powerpc64__)

#include "NativeRegisterContextLinux_ppc64le.h"

#include "lldb/Host/HostInfo.h"
#include "lldb/Host/common/NativeProcessProtocol.h"
#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/RegisterValue.h"
#include "lldb/Utility/Status.h"

#include "Plugins/Process/Linux/NativeProcessLinux.h"
#include "Plugins/Process/Linux/Procfs.h"
#include "Plugins/Process/POSIX/ProcessPOSIXLog.h"
#include "Plugins/Process/Utility/RegisterInfoPOSIX_ppc64le.h"

// System includes - They have to be included after framework includes because
// they define some macros which collide with variable names in other modules
#include <sys/socket.h>
#include <elf.h>
#include <asm/ptrace.h>

#define REG_CONTEXT_SIZE                                                       \
  (GetGPRSize() + GetFPRSize() + sizeof(m_vmx_ppc64le) + sizeof(m_vsx_ppc64le))
using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::process_linux;

static const uint32_t g_gpr_regnums_ppc64le[] = {
    gpr_r0_ppc64le,   gpr_r1_ppc64le,  gpr_r2_ppc64le,     gpr_r3_ppc64le,
    gpr_r4_ppc64le,   gpr_r5_ppc64le,  gpr_r6_ppc64le,     gpr_r7_ppc64le,
    gpr_r8_ppc64le,   gpr_r9_ppc64le,  gpr_r10_ppc64le,    gpr_r11_ppc64le,
    gpr_r12_ppc64le,  gpr_r13_ppc64le, gpr_r14_ppc64le,    gpr_r15_ppc64le,
    gpr_r16_ppc64le,  gpr_r17_ppc64le, gpr_r18_ppc64le,    gpr_r19_ppc64le,
    gpr_r20_ppc64le,  gpr_r21_ppc64le, gpr_r22_ppc64le,    gpr_r23_ppc64le,
    gpr_r24_ppc64le,  gpr_r25_ppc64le, gpr_r26_ppc64le,    gpr_r27_ppc64le,
    gpr_r28_ppc64le,  gpr_r29_ppc64le, gpr_r30_ppc64le,    gpr_r31_ppc64le,
    gpr_pc_ppc64le,   gpr_msr_ppc64le, gpr_origr3_ppc64le, gpr_ctr_ppc64le,
    gpr_lr_ppc64le,   gpr_xer_ppc64le, gpr_cr_ppc64le,     gpr_softe_ppc64le,
    gpr_trap_ppc64le,
    LLDB_INVALID_REGNUM // register sets need to end with this flag
};

static const uint32_t g_fpr_regnums_ppc64le[] = {
    fpr_f0_ppc64le,    fpr_f1_ppc64le,  fpr_f2_ppc64le,  fpr_f3_ppc64le,
    fpr_f4_ppc64le,    fpr_f5_ppc64le,  fpr_f6_ppc64le,  fpr_f7_ppc64le,
    fpr_f8_ppc64le,    fpr_f9_ppc64le,  fpr_f10_ppc64le, fpr_f11_ppc64le,
    fpr_f12_ppc64le,   fpr_f13_ppc64le, fpr_f14_ppc64le, fpr_f15_ppc64le,
    fpr_f16_ppc64le,   fpr_f17_ppc64le, fpr_f18_ppc64le, fpr_f19_ppc64le,
    fpr_f20_ppc64le,   fpr_f21_ppc64le, fpr_f22_ppc64le, fpr_f23_ppc64le,
    fpr_f24_ppc64le,   fpr_f25_ppc64le, fpr_f26_ppc64le, fpr_f27_ppc64le,
    fpr_f28_ppc64le,   fpr_f29_ppc64le, fpr_f30_ppc64le, fpr_f31_ppc64le,
    fpr_fpscr_ppc64le,
    LLDB_INVALID_REGNUM // register sets need to end with this flag
};

static const uint32_t g_vmx_regnums_ppc64le[] = {
    vmx_vr0_ppc64le,  vmx_vr1_ppc64le,    vmx_vr2_ppc64le,  vmx_vr3_ppc64le,
    vmx_vr4_ppc64le,  vmx_vr5_ppc64le,    vmx_vr6_ppc64le,  vmx_vr7_ppc64le,
    vmx_vr8_ppc64le,  vmx_vr9_ppc64le,    vmx_vr10_ppc64le, vmx_vr11_ppc64le,
    vmx_vr12_ppc64le, vmx_vr13_ppc64le,   vmx_vr14_ppc64le, vmx_vr15_ppc64le,
    vmx_vr16_ppc64le, vmx_vr17_ppc64le,   vmx_vr18_ppc64le, vmx_vr19_ppc64le,
    vmx_vr20_ppc64le, vmx_vr21_ppc64le,   vmx_vr22_ppc64le, vmx_vr23_ppc64le,
    vmx_vr24_ppc64le, vmx_vr25_ppc64le,   vmx_vr26_ppc64le, vmx_vr27_ppc64le,
    vmx_vr28_ppc64le, vmx_vr29_ppc64le,   vmx_vr30_ppc64le, vmx_vr31_ppc64le,
    vmx_vscr_ppc64le, vmx_vrsave_ppc64le,
    LLDB_INVALID_REGNUM // register sets need to end with this flag
};

static const uint32_t g_vsx_regnums_ppc64le[] = {
    vsx_vs0_ppc64le,  vsx_vs1_ppc64le,  vsx_vs2_ppc64le,  vsx_vs3_ppc64le,
    vsx_vs4_ppc64le,  vsx_vs5_ppc64le,  vsx_vs6_ppc64le,  vsx_vs7_ppc64le,
    vsx_vs8_ppc64le,  vsx_vs9_ppc64le,  vsx_vs10_ppc64le, vsx_vs11_ppc64le,
    vsx_vs12_ppc64le, vsx_vs13_ppc64le, vsx_vs14_ppc64le, vsx_vs15_ppc64le,
    vsx_vs16_ppc64le, vsx_vs17_ppc64le, vsx_vs18_ppc64le, vsx_vs19_ppc64le,
    vsx_vs20_ppc64le, vsx_vs21_ppc64le, vsx_vs22_ppc64le, vsx_vs23_ppc64le,
    vsx_vs24_ppc64le, vsx_vs25_ppc64le, vsx_vs26_ppc64le, vsx_vs27_ppc64le,
    vsx_vs28_ppc64le, vsx_vs29_ppc64le, vsx_vs30_ppc64le, vsx_vs31_ppc64le,
    vsx_vs32_ppc64le, vsx_vs33_ppc64le, vsx_vs34_ppc64le, vsx_vs35_ppc64le,
    vsx_vs36_ppc64le, vsx_vs37_ppc64le, vsx_vs38_ppc64le, vsx_vs39_ppc64le,
    vsx_vs40_ppc64le, vsx_vs41_ppc64le, vsx_vs42_ppc64le, vsx_vs43_ppc64le,
    vsx_vs44_ppc64le, vsx_vs45_ppc64le, vsx_vs46_ppc64le, vsx_vs47_ppc64le,
    vsx_vs48_ppc64le, vsx_vs49_ppc64le, vsx_vs50_ppc64le, vsx_vs51_ppc64le,
    vsx_vs52_ppc64le, vsx_vs53_ppc64le, vsx_vs54_ppc64le, vsx_vs55_ppc64le,
    vsx_vs56_ppc64le, vsx_vs57_ppc64le, vsx_vs58_ppc64le, vsx_vs59_ppc64le,
    vsx_vs60_ppc64le, vsx_vs61_ppc64le, vsx_vs62_ppc64le, vsx_vs63_ppc64le,
    LLDB_INVALID_REGNUM // register sets need to end with this flag
};

// Number of register sets provided by this context.
static constexpr int k_num_register_sets = 4;

static const RegisterSet g_reg_sets_ppc64le[k_num_register_sets] = {
    {"General Purpose Registers", "gpr", k_num_gpr_registers_ppc64le,
     g_gpr_regnums_ppc64le},
    {"Floating Point Registers", "fpr", k_num_fpr_registers_ppc64le,
     g_fpr_regnums_ppc64le},
    {"AltiVec/VMX Registers", "vmx", k_num_vmx_registers_ppc64le,
     g_vmx_regnums_ppc64le},
    {"VSX Registers", "vsx", k_num_vsx_registers_ppc64le,
     g_vsx_regnums_ppc64le},
};

std::unique_ptr<NativeRegisterContextLinux>
NativeRegisterContextLinux::CreateHostNativeRegisterContextLinux(
    const ArchSpec &target_arch, NativeThreadLinux &native_thread) {
  switch (target_arch.GetMachine()) {
  case llvm::Triple::ppc64le:
    return std::make_unique<NativeRegisterContextLinux_ppc64le>(target_arch,
                                                                 native_thread);
  default:
    llvm_unreachable("have no register context for architecture");
  }
}

llvm::Expected<ArchSpec>
NativeRegisterContextLinux::DetermineArchitecture(lldb::tid_t tid) {
  return HostInfo::GetArchitecture();
}

NativeRegisterContextLinux_ppc64le::NativeRegisterContextLinux_ppc64le(
    const ArchSpec &target_arch, NativeThreadProtocol &native_thread)
    : NativeRegisterContextRegisterInfo(
          native_thread, new RegisterInfoPOSIX_ppc64le(target_arch)),
      NativeRegisterContextLinux(native_thread) {
  if (target_arch.GetMachine() != llvm::Triple::ppc64le) {
    llvm_unreachable("Unhandled target architecture.");
  }

  ::memset(&m_gpr_ppc64le, 0, sizeof(m_gpr_ppc64le));
  ::memset(&m_fpr_ppc64le, 0, sizeof(m_fpr_ppc64le));
  ::memset(&m_vmx_ppc64le, 0, sizeof(m_vmx_ppc64le));
  ::memset(&m_vsx_ppc64le, 0, sizeof(m_vsx_ppc64le));
  ::memset(&m_hwp_regs, 0, sizeof(m_hwp_regs));
}

uint32_t NativeRegisterContextLinux_ppc64le::GetRegisterSetCount() const {
  return k_num_register_sets;
}

const RegisterSet *
NativeRegisterContextLinux_ppc64le::GetRegisterSet(uint32_t set_index) const {
  if (set_index < k_num_register_sets)
    return &g_reg_sets_ppc64le[set_index];

  return nullptr;
}

uint32_t NativeRegisterContextLinux_ppc64le::GetUserRegisterCount() const {
  uint32_t count = 0;
  for (uint32_t set_index = 0; set_index < k_num_register_sets; ++set_index)
    count += g_reg_sets_ppc64le[set_index].num_registers;
  return count;
}

Status NativeRegisterContextLinux_ppc64le::ReadRegister(
    const RegisterInfo *reg_info, RegisterValue &reg_value) {
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

    // Get pointer to m_fpr_ppc64le variable and set the data from it.
    uint32_t fpr_offset = CalculateFprOffset(reg_info);
    assert(fpr_offset < sizeof m_fpr_ppc64le);
    uint8_t *src = (uint8_t *)&m_fpr_ppc64le + fpr_offset;
    reg_value.SetFromMemoryData(*reg_info, src, reg_info->byte_size,
                                eByteOrderLittle, error);
  } else if (IsVSX(reg)) {
    uint32_t vsx_offset = CalculateVsxOffset(reg_info);
    assert(vsx_offset < sizeof(m_vsx_ppc64le));

    if (vsx_offset < sizeof(m_vsx_ppc64le) / 2) {
      error = ReadVSX();
      if (error.Fail())
        return error;

      error = ReadFPR();
      if (error.Fail())
        return error;

      uint64_t value[2];
      uint8_t *dst, *src;
      dst = (uint8_t *)&value;
      src = (uint8_t *)&m_vsx_ppc64le + vsx_offset / 2;
      ::memcpy(dst, src, 8);
      dst += 8;
      src = (uint8_t *)&m_fpr_ppc64le + vsx_offset / 2;
      ::memcpy(dst, src, 8);
      reg_value.SetFromMemoryData(*reg_info, &value, reg_info->byte_size,
                                  eByteOrderLittle, error);
    } else {
      error = ReadVMX();
      if (error.Fail())
        return error;

      // Get pointer to m_vmx_ppc64le variable and set the data from it.
      uint32_t vmx_offset = vsx_offset - sizeof(m_vsx_ppc64le) / 2;
      uint8_t *src = (uint8_t *)&m_vmx_ppc64le + vmx_offset;
      reg_value.SetFromMemoryData(*reg_info, src, reg_info->byte_size,
                                  eByteOrderLittle, error);
    }
  } else if (IsVMX(reg)) {
    error = ReadVMX();
    if (error.Fail())
      return error;

    // Get pointer to m_vmx_ppc64le variable and set the data from it.
    uint32_t vmx_offset = CalculateVmxOffset(reg_info);
    assert(vmx_offset < sizeof m_vmx_ppc64le);
    uint8_t *src = (uint8_t *)&m_vmx_ppc64le + vmx_offset;
    reg_value.SetFromMemoryData(*reg_info, src, reg_info->byte_size,
                                eByteOrderLittle, error);
  } else if (IsGPR(reg)) {
    error = ReadGPR();
    if (error.Fail())
      return error;

    uint8_t *src = (uint8_t *) &m_gpr_ppc64le + reg_info->byte_offset;
    reg_value.SetFromMemoryData(*reg_info, src, reg_info->byte_size,
                                eByteOrderLittle, error);
  } else {
    return Status("failed - register wasn't recognized to be a GPR, FPR, VSX "
                  "or VMX, read strategy unknown");
  }

  return error;
}

Status NativeRegisterContextLinux_ppc64le::WriteRegister(
    const RegisterInfo *reg_info, const RegisterValue &reg_value) {
  Status error;
  if (!reg_info)
    return Status("reg_info NULL");

  const uint32_t reg_index = reg_info->kinds[lldb::eRegisterKindLLDB];
  if (reg_index == LLDB_INVALID_REGNUM)
    return Status("no lldb regnum for %s", reg_info && reg_info->name
                                               ? reg_info->name
                                               : "<unknown register>");

  if (IsGPR(reg_index)) {
    error = ReadGPR();
    if (error.Fail())
      return error;

    uint8_t *dst = (uint8_t *)&m_gpr_ppc64le + reg_info->byte_offset;
    ::memcpy(dst, reg_value.GetBytes(), reg_value.GetByteSize());

    error = WriteGPR();
    if (error.Fail())
      return error;

    return Status();
  }

  if (IsFPR(reg_index)) {
    error = ReadFPR();
    if (error.Fail())
      return error;

    // Get pointer to m_fpr_ppc64le variable and set the data to it.
    uint32_t fpr_offset = CalculateFprOffset(reg_info);
    assert(fpr_offset < GetFPRSize());
    uint8_t *dst = (uint8_t *)&m_fpr_ppc64le + fpr_offset;
    ::memcpy(dst, reg_value.GetBytes(), reg_value.GetByteSize());

    error = WriteFPR();
    if (error.Fail())
      return error;

    return Status();
  }

  if (IsVMX(reg_index)) {
    error = ReadVMX();
    if (error.Fail())
      return error;

    // Get pointer to m_vmx_ppc64le variable and set the data to it.
    uint32_t vmx_offset = CalculateVmxOffset(reg_info);
    assert(vmx_offset < sizeof(m_vmx_ppc64le));
    uint8_t *dst = (uint8_t *)&m_vmx_ppc64le + vmx_offset;
    ::memcpy(dst, reg_value.GetBytes(), reg_value.GetByteSize());

    error = WriteVMX();
    if (error.Fail())
      return error;

    return Status();
  }

  if (IsVSX(reg_index)) {
    uint32_t vsx_offset = CalculateVsxOffset(reg_info);
    assert(vsx_offset < sizeof(m_vsx_ppc64le));

    if (vsx_offset < sizeof(m_vsx_ppc64le) / 2) {
      error = ReadVSX();
      if (error.Fail())
        return error;

      error = ReadFPR();
      if (error.Fail())
        return error;

      uint64_t value[2];
      ::memcpy(value, reg_value.GetBytes(), 16);
      uint8_t *dst, *src;
      src = (uint8_t *)value;
      dst = (uint8_t *)&m_vsx_ppc64le + vsx_offset / 2;
      ::memcpy(dst, src, 8);
      src += 8;
      dst = (uint8_t *)&m_fpr_ppc64le + vsx_offset / 2;
      ::memcpy(dst, src, 8);

      WriteVSX();
      WriteFPR();
    } else {
      error = ReadVMX();
      if (error.Fail())
        return error;

      // Get pointer to m_vmx_ppc64le variable and set the data from it.
      uint32_t vmx_offset = vsx_offset - sizeof(m_vsx_ppc64le) / 2;
      uint8_t *dst = (uint8_t *)&m_vmx_ppc64le + vmx_offset;
      ::memcpy(dst, reg_value.GetBytes(), reg_value.GetByteSize());
      WriteVMX();
    }

    return Status();
  }

  return Status("failed - register wasn't recognized to be a GPR, FPR, VSX "
                "or VMX, write strategy unknown");
}

Status NativeRegisterContextLinux_ppc64le::ReadAllRegisterValues(
    lldb::WritableDataBufferSP &data_sp) {
  Status error;

  data_sp.reset(new DataBufferHeap(REG_CONTEXT_SIZE, 0));
  error = ReadGPR();
  if (error.Fail())
    return error;

  error = ReadFPR();
  if (error.Fail())
    return error;

  error = ReadVMX();
  if (error.Fail())
    return error;

  error = ReadVSX();
  if (error.Fail())
    return error;

  uint8_t *dst = data_sp->GetBytes();
  ::memcpy(dst, &m_gpr_ppc64le, GetGPRSize());
  dst += GetGPRSize();
  ::memcpy(dst, &m_fpr_ppc64le, GetFPRSize());
  dst += GetFPRSize();
  ::memcpy(dst, &m_vmx_ppc64le, sizeof(m_vmx_ppc64le));
  dst += sizeof(m_vmx_ppc64le);
  ::memcpy(dst, &m_vsx_ppc64le, sizeof(m_vsx_ppc64le));

  return error;
}

Status NativeRegisterContextLinux_ppc64le::WriteAllRegisterValues(
    const lldb::DataBufferSP &data_sp) {
  Status error;

  if (!data_sp) {
    error.SetErrorStringWithFormat(
        "NativeRegisterContextLinux_ppc64le::%s invalid data_sp provided",
        __FUNCTION__);
    return error;
  }

  if (data_sp->GetByteSize() != REG_CONTEXT_SIZE) {
    error.SetErrorStringWithFormat(
        "NativeRegisterContextLinux_ppc64le::%s data_sp contained mismatched "
        "data size, expected %" PRIu64 ", actual %" PRIu64,
        __FUNCTION__, REG_CONTEXT_SIZE, data_sp->GetByteSize());
    return error;
  }

  const uint8_t *src = data_sp->GetBytes();
  if (src == nullptr) {
    error.SetErrorStringWithFormat("NativeRegisterContextLinux_ppc64le::%s "
                                   "DataBuffer::GetBytes() returned a null "
                                   "pointer",
                                   __FUNCTION__);
    return error;
  }

  ::memcpy(&m_gpr_ppc64le, src, GetGPRSize());
  error = WriteGPR();

  if (error.Fail())
    return error;

  src += GetGPRSize();
  ::memcpy(&m_fpr_ppc64le, src, GetFPRSize());

  error = WriteFPR();
  if (error.Fail())
    return error;

  src += GetFPRSize();
  ::memcpy(&m_vmx_ppc64le, src, sizeof(m_vmx_ppc64le));

  error = WriteVMX();
  if (error.Fail())
    return error;

  src += sizeof(m_vmx_ppc64le);
  ::memcpy(&m_vsx_ppc64le, src, sizeof(m_vsx_ppc64le));
  error = WriteVSX();

  return error;
}

bool NativeRegisterContextLinux_ppc64le::IsGPR(unsigned reg) const {
  return reg <= k_last_gpr_ppc64le; // GPR's come first.
}

bool NativeRegisterContextLinux_ppc64le::IsFPR(unsigned reg) const {
  return (k_first_fpr_ppc64le <= reg && reg <= k_last_fpr_ppc64le);
}

uint32_t NativeRegisterContextLinux_ppc64le::CalculateFprOffset(
    const RegisterInfo *reg_info) const {
  return reg_info->byte_offset -
         GetRegisterInfoAtIndex(k_first_fpr_ppc64le)->byte_offset;
}

uint32_t NativeRegisterContextLinux_ppc64le::CalculateVmxOffset(
    const RegisterInfo *reg_info) const {
  return reg_info->byte_offset -
         GetRegisterInfoAtIndex(k_first_vmx_ppc64le)->byte_offset;
}

uint32_t NativeRegisterContextLinux_ppc64le::CalculateVsxOffset(
    const RegisterInfo *reg_info) const {
  return reg_info->byte_offset -
         GetRegisterInfoAtIndex(k_first_vsx_ppc64le)->byte_offset;
}

Status NativeRegisterContextLinux_ppc64le::ReadVMX() {
  int regset = NT_PPC_VMX;
  return NativeProcessLinux::PtraceWrapper(PTRACE_GETVRREGS, m_thread.GetID(),
                                           &regset, &m_vmx_ppc64le,
                                           sizeof(m_vmx_ppc64le));
}

Status NativeRegisterContextLinux_ppc64le::WriteVMX() {
  int regset = NT_PPC_VMX;
  return NativeProcessLinux::PtraceWrapper(PTRACE_SETVRREGS, m_thread.GetID(),
                                           &regset, &m_vmx_ppc64le,
                                           sizeof(m_vmx_ppc64le));
}

Status NativeRegisterContextLinux_ppc64le::ReadVSX() {
  int regset = NT_PPC_VSX;
  return NativeProcessLinux::PtraceWrapper(PTRACE_GETVSRREGS, m_thread.GetID(),
                                           &regset, &m_vsx_ppc64le,
                                           sizeof(m_vsx_ppc64le));
}

Status NativeRegisterContextLinux_ppc64le::WriteVSX() {
  int regset = NT_PPC_VSX;
  return NativeProcessLinux::PtraceWrapper(PTRACE_SETVSRREGS, m_thread.GetID(),
                                           &regset, &m_vsx_ppc64le,
                                           sizeof(m_vsx_ppc64le));
}

bool NativeRegisterContextLinux_ppc64le::IsVMX(unsigned reg) {
  return (reg >= k_first_vmx_ppc64le) && (reg <= k_last_vmx_ppc64le);
}

bool NativeRegisterContextLinux_ppc64le::IsVSX(unsigned reg) {
  return (reg >= k_first_vsx_ppc64le) && (reg <= k_last_vsx_ppc64le);
}

uint32_t NativeRegisterContextLinux_ppc64le::NumSupportedHardwareWatchpoints() {
  Log *log = GetLog(POSIXLog::Watchpoints);

  // Read hardware breakpoint and watchpoint information.
  Status error = ReadHardwareDebugInfo();

  if (error.Fail())
    return 0;

  LLDB_LOG(log, "{0}", m_max_hwp_supported);
  return m_max_hwp_supported;
}

uint32_t NativeRegisterContextLinux_ppc64le::SetHardwareWatchpoint(
    lldb::addr_t addr, size_t size, uint32_t watch_flags) {
  Log *log = GetLog(POSIXLog::Watchpoints);
  LLDB_LOG(log, "addr: {0:x}, size: {1:x} watch_flags: {2:x}", addr, size,
           watch_flags);

  // Read hardware breakpoint and watchpoint information.
  Status error = ReadHardwareDebugInfo();

  if (error.Fail())
    return LLDB_INVALID_INDEX32;

  uint32_t control_value = 0, wp_index = 0;
  lldb::addr_t real_addr = addr;
  uint32_t rw_mode = 0;

  // Check if we are setting watchpoint other than read/write/access Update
  // watchpoint flag to match ppc64le write-read bit configuration.
  switch (watch_flags) {
  case eWatchpointKindWrite:
    rw_mode = PPC_BREAKPOINT_TRIGGER_WRITE;
    watch_flags = 2;
    break;
  case eWatchpointKindRead:
    rw_mode = PPC_BREAKPOINT_TRIGGER_READ;
    watch_flags = 1;
    break;
  case (eWatchpointKindRead | eWatchpointKindWrite):
    rw_mode = PPC_BREAKPOINT_TRIGGER_RW;
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

    addr_t begin = llvm::alignDown(addr, 8);
    addr_t end = llvm::alignTo(addr + size, 8);
    size = llvm::PowerOf2Ceil(end - begin);

    addr = addr & (~0x07);
  }

  // Setup control value
  control_value = watch_flags << 3;
  control_value |= ((1 << size) - 1) << 5;
  control_value |= (2 << 1) | 1;

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
  m_hwp_regs[wp_index].mode = rw_mode;

  // PTRACE call to set corresponding watchpoint register.
  error = WriteHardwareDebugRegs();

  if (error.Fail()) {
    m_hwp_regs[wp_index].address = 0;
    m_hwp_regs[wp_index].control &= llvm::maskTrailingZeros<uint32_t>(1);

    return LLDB_INVALID_INDEX32;
  }

  return wp_index;
}

bool NativeRegisterContextLinux_ppc64le::ClearHardwareWatchpoint(
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
  long *tempSlot = reinterpret_cast<long *>(m_hwp_regs[wp_index].slot);

  // Update watchpoint in local cache
  m_hwp_regs[wp_index].control &= llvm::maskTrailingZeros<uint32_t>(1);
  m_hwp_regs[wp_index].address = 0;
  m_hwp_regs[wp_index].slot = 0;
  m_hwp_regs[wp_index].mode = 0;

  // Ptrace call to update hardware debug registers
  error = NativeProcessLinux::PtraceWrapper(PPC_PTRACE_DELHWDEBUG,
                                            m_thread.GetID(), 0, tempSlot);

  if (error.Fail()) {
    m_hwp_regs[wp_index].control = tempControl;
    m_hwp_regs[wp_index].address = tempAddr;
    m_hwp_regs[wp_index].slot = reinterpret_cast<long>(tempSlot);

    return false;
  }

  return true;
}

uint32_t
NativeRegisterContextLinux_ppc64le::GetWatchpointSize(uint32_t wp_index) {
  Log *log = GetLog(POSIXLog::Watchpoints);
  LLDB_LOG(log, "wp_index: {0}", wp_index);

  unsigned control = (m_hwp_regs[wp_index].control >> 5) & 0xff;
  if (llvm::isPowerOf2_32(control + 1)) {
    return llvm::popcount(control);
  }

  return 0;
}

bool NativeRegisterContextLinux_ppc64le::WatchpointIsEnabled(
    uint32_t wp_index) {
  Log *log = GetLog(POSIXLog::Watchpoints);
  LLDB_LOG(log, "wp_index: {0}", wp_index);

  return !!((m_hwp_regs[wp_index].control & 0x1) == 0x1);
}

Status NativeRegisterContextLinux_ppc64le::GetWatchpointHitIndex(
    uint32_t &wp_index, lldb::addr_t trap_addr) {
  Log *log = GetLog(POSIXLog::Watchpoints);
  LLDB_LOG(log, "wp_index: {0}, trap_addr: {1:x}", wp_index, trap_addr);

  uint32_t watch_size;
  lldb::addr_t watch_addr;

  for (wp_index = 0; wp_index < m_max_hwp_supported; ++wp_index) {
    watch_size = GetWatchpointSize(wp_index);
    watch_addr = m_hwp_regs[wp_index].address;

    if (WatchpointIsEnabled(wp_index) && trap_addr >= watch_addr &&
        trap_addr <= watch_addr + watch_size) {
      m_hwp_regs[wp_index].hit_addr = trap_addr;
      return Status();
    }
  }

  wp_index = LLDB_INVALID_INDEX32;
  return Status();
}

lldb::addr_t
NativeRegisterContextLinux_ppc64le::GetWatchpointAddress(uint32_t wp_index) {
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
NativeRegisterContextLinux_ppc64le::GetWatchpointHitAddress(uint32_t wp_index) {
  Log *log = GetLog(POSIXLog::Watchpoints);
  LLDB_LOG(log, "wp_index: {0}", wp_index);

  if (wp_index >= m_max_hwp_supported)
    return LLDB_INVALID_ADDRESS;

  if (WatchpointIsEnabled(wp_index))
    return m_hwp_regs[wp_index].hit_addr;

  return LLDB_INVALID_ADDRESS;
}

Status NativeRegisterContextLinux_ppc64le::ReadHardwareDebugInfo() {
  if (!m_refresh_hwdebug_info) {
    return Status();
  }

  ::pid_t tid = m_thread.GetID();

  struct ppc_debug_info hwdebug_info;
  Status error;

  error = NativeProcessLinux::PtraceWrapper(
      PPC_PTRACE_GETHWDBGINFO, tid, 0, &hwdebug_info, sizeof(hwdebug_info));

  if (error.Fail())
    return error;

  m_max_hwp_supported = hwdebug_info.num_data_bps;
  m_max_hbp_supported = hwdebug_info.num_instruction_bps;
  m_refresh_hwdebug_info = false;

  return error;
}

Status NativeRegisterContextLinux_ppc64le::WriteHardwareDebugRegs() {
  struct ppc_hw_breakpoint reg_state;
  Status error;
  long ret;

  for (uint32_t i = 0; i < m_max_hwp_supported; i++) {
    reg_state.addr = m_hwp_regs[i].address;
    reg_state.trigger_type = m_hwp_regs[i].mode;
    reg_state.version = 1;
    reg_state.addr_mode = PPC_BREAKPOINT_MODE_EXACT;
    reg_state.condition_mode = PPC_BREAKPOINT_CONDITION_NONE;
    reg_state.addr2 = 0;
    reg_state.condition_value = 0;

    error = NativeProcessLinux::PtraceWrapper(PPC_PTRACE_SETHWDEBUG,
                                              m_thread.GetID(), 0, &reg_state,
                                              sizeof(reg_state), &ret);

    if (error.Fail())
      return error;

    m_hwp_regs[i].slot = ret;
  }

  return error;
}

#endif // defined(__powerpc64__)
