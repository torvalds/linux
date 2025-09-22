//===-- NativeRegisterContextWindows_arm.cpp ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#if defined(__arm__) || defined(_M_ARM)

#include "NativeRegisterContextWindows_arm.h"
#include "NativeThreadWindows.h"
#include "Plugins/Process/Utility/RegisterInfoPOSIX_arm.h"
#include "ProcessWindowsLog.h"
#include "lldb/Host/HostInfo.h"
#include "lldb/Host/HostThread.h"
#include "lldb/Host/windows/HostThreadWindows.h"
#include "lldb/Host/windows/windows.h"

#include "lldb/Utility/Log.h"
#include "lldb/Utility/RegisterValue.h"
#include "llvm/ADT/STLExtras.h"

using namespace lldb;
using namespace lldb_private;

#define REG_CONTEXT_SIZE sizeof(::CONTEXT)

namespace {
static const uint32_t g_gpr_regnums_arm[] = {
    gpr_r0_arm,         gpr_r1_arm,   gpr_r2_arm,  gpr_r3_arm, gpr_r4_arm,
    gpr_r5_arm,         gpr_r6_arm,   gpr_r7_arm,  gpr_r8_arm, gpr_r9_arm,
    gpr_r10_arm,        gpr_r11_arm,  gpr_r12_arm, gpr_sp_arm, gpr_lr_arm,
    gpr_pc_arm,         gpr_cpsr_arm,
    LLDB_INVALID_REGNUM // Register set must be terminated with this flag
};
static_assert(((sizeof g_gpr_regnums_arm / sizeof g_gpr_regnums_arm[0]) - 1) ==
                  k_num_gpr_registers_arm,
              "g_gpr_regnums_arm has wrong number of register infos");

static const uint32_t g_fpr_regnums_arm[] = {
    fpu_s0_arm,         fpu_s1_arm,  fpu_s2_arm,  fpu_s3_arm,  fpu_s4_arm,
    fpu_s5_arm,         fpu_s6_arm,  fpu_s7_arm,  fpu_s8_arm,  fpu_s9_arm,
    fpu_s10_arm,        fpu_s11_arm, fpu_s12_arm, fpu_s13_arm, fpu_s14_arm,
    fpu_s15_arm,        fpu_s16_arm, fpu_s17_arm, fpu_s18_arm, fpu_s19_arm,
    fpu_s20_arm,        fpu_s21_arm, fpu_s22_arm, fpu_s23_arm, fpu_s24_arm,
    fpu_s25_arm,        fpu_s26_arm, fpu_s27_arm, fpu_s28_arm, fpu_s29_arm,
    fpu_s30_arm,        fpu_s31_arm,

    fpu_d0_arm,         fpu_d1_arm,  fpu_d2_arm,  fpu_d3_arm,  fpu_d4_arm,
    fpu_d5_arm,         fpu_d6_arm,  fpu_d7_arm,  fpu_d8_arm,  fpu_d9_arm,
    fpu_d10_arm,        fpu_d11_arm, fpu_d12_arm, fpu_d13_arm, fpu_d14_arm,
    fpu_d15_arm,        fpu_d16_arm, fpu_d17_arm, fpu_d18_arm, fpu_d19_arm,
    fpu_d20_arm,        fpu_d21_arm, fpu_d22_arm, fpu_d23_arm, fpu_d24_arm,
    fpu_d25_arm,        fpu_d26_arm, fpu_d27_arm, fpu_d28_arm, fpu_d29_arm,
    fpu_d30_arm,        fpu_d31_arm,

    fpu_q0_arm,         fpu_q1_arm,  fpu_q2_arm,  fpu_q3_arm,  fpu_q4_arm,
    fpu_q5_arm,         fpu_q6_arm,  fpu_q7_arm,  fpu_q8_arm,  fpu_q9_arm,
    fpu_q10_arm,        fpu_q11_arm, fpu_q12_arm, fpu_q13_arm, fpu_q14_arm,
    fpu_q15_arm,

    fpu_fpscr_arm,
    LLDB_INVALID_REGNUM // Register set must be terminated with this flag
};
static_assert(((sizeof g_fpr_regnums_arm / sizeof g_fpr_regnums_arm[0]) - 1) ==
                  k_num_fpr_registers_arm,
              "g_fpu_regnums_arm has wrong number of register infos");

static const RegisterSet g_reg_sets_arm[] = {
    {"General Purpose Registers", "gpr", std::size(g_gpr_regnums_arm) - 1,
     g_gpr_regnums_arm},
    {"Floating Point Registers", "fpr", std::size(g_fpr_regnums_arm) - 1,
     g_fpr_regnums_arm},
};

enum { k_num_register_sets = 2 };

} // namespace

static RegisterInfoInterface *
CreateRegisterInfoInterface(const ArchSpec &target_arch) {
  assert((HostInfo::GetArchitecture().GetAddressByteSize() == 4) &&
         "Register setting path assumes this is a 32-bit host");
  return new RegisterInfoPOSIX_arm(target_arch);
}

static Status GetThreadContextHelper(lldb::thread_t thread_handle,
                                     PCONTEXT context_ptr,
                                     const DWORD control_flag) {
  Log *log = GetLog(WindowsLog::Registers);
  Status error;

  memset(context_ptr, 0, sizeof(::CONTEXT));
  context_ptr->ContextFlags = control_flag;
  if (!::GetThreadContext(thread_handle, context_ptr)) {
    error.SetError(GetLastError(), eErrorTypeWin32);
    LLDB_LOG(log, "{0} GetThreadContext failed with error {1}", __FUNCTION__,
             error);
    return error;
  }
  return Status();
}

static Status SetThreadContextHelper(lldb::thread_t thread_handle,
                                     PCONTEXT context_ptr) {
  Log *log = GetLog(WindowsLog::Registers);
  Status error;
  // It's assumed that the thread has stopped.
  if (!::SetThreadContext(thread_handle, context_ptr)) {
    error.SetError(GetLastError(), eErrorTypeWin32);
    LLDB_LOG(log, "{0} SetThreadContext failed with error {1}", __FUNCTION__,
             error);
    return error;
  }
  return Status();
}

std::unique_ptr<NativeRegisterContextWindows>
NativeRegisterContextWindows::CreateHostNativeRegisterContextWindows(
    const ArchSpec &target_arch, NativeThreadProtocol &native_thread) {
  // TODO: Register context for a WoW64 application?

  // Register context for a native 64-bit application.
  return std::make_unique<NativeRegisterContextWindows_arm>(target_arch,
                                                            native_thread);
}

NativeRegisterContextWindows_arm::NativeRegisterContextWindows_arm(
    const ArchSpec &target_arch, NativeThreadProtocol &native_thread)
    : NativeRegisterContextWindows(native_thread,
                                   CreateRegisterInfoInterface(target_arch)) {}

bool NativeRegisterContextWindows_arm::IsGPR(uint32_t reg_index) const {
  return (reg_index >= k_first_gpr_arm && reg_index <= k_last_gpr_arm);
}

bool NativeRegisterContextWindows_arm::IsFPR(uint32_t reg_index) const {
  return (reg_index >= k_first_fpr_arm && reg_index <= k_last_fpr_arm);
}

uint32_t NativeRegisterContextWindows_arm::GetRegisterSetCount() const {
  return k_num_register_sets;
}

const RegisterSet *
NativeRegisterContextWindows_arm::GetRegisterSet(uint32_t set_index) const {
  if (set_index >= k_num_register_sets)
    return nullptr;
  return &g_reg_sets_arm[set_index];
}

Status NativeRegisterContextWindows_arm::GPRRead(const uint32_t reg,
                                                 RegisterValue &reg_value) {
  ::CONTEXT tls_context;
  DWORD context_flag = CONTEXT_CONTROL | CONTEXT_INTEGER;
  Status error =
      GetThreadContextHelper(GetThreadHandle(), &tls_context, context_flag);
  if (error.Fail())
    return error;

  switch (reg) {
  case gpr_r0_arm:
    reg_value.SetUInt32(tls_context.R0);
    break;
  case gpr_r1_arm:
    reg_value.SetUInt32(tls_context.R1);
    break;
  case gpr_r2_arm:
    reg_value.SetUInt32(tls_context.R2);
    break;
  case gpr_r3_arm:
    reg_value.SetUInt32(tls_context.R3);
    break;
  case gpr_r4_arm:
    reg_value.SetUInt32(tls_context.R4);
    break;
  case gpr_r5_arm:
    reg_value.SetUInt32(tls_context.R5);
    break;
  case gpr_r6_arm:
    reg_value.SetUInt32(tls_context.R6);
    break;
  case gpr_r7_arm:
    reg_value.SetUInt32(tls_context.R7);
    break;
  case gpr_r8_arm:
    reg_value.SetUInt32(tls_context.R8);
    break;
  case gpr_r9_arm:
    reg_value.SetUInt32(tls_context.R9);
    break;
  case gpr_r10_arm:
    reg_value.SetUInt32(tls_context.R10);
    break;
  case gpr_r11_arm:
    reg_value.SetUInt32(tls_context.R11);
    break;
  case gpr_r12_arm:
    reg_value.SetUInt32(tls_context.R12);
    break;
  case gpr_sp_arm:
    reg_value.SetUInt32(tls_context.Sp);
    break;
  case gpr_lr_arm:
    reg_value.SetUInt32(tls_context.Lr);
    break;
  case gpr_pc_arm:
    reg_value.SetUInt32(tls_context.Pc);
    break;
  case gpr_cpsr_arm:
    reg_value.SetUInt32(tls_context.Cpsr);
    break;
  }

  return error;
}

Status
NativeRegisterContextWindows_arm::GPRWrite(const uint32_t reg,
                                           const RegisterValue &reg_value) {
  ::CONTEXT tls_context;
  DWORD context_flag = CONTEXT_CONTROL | CONTEXT_INTEGER;
  auto thread_handle = GetThreadHandle();
  Status error =
      GetThreadContextHelper(thread_handle, &tls_context, context_flag);
  if (error.Fail())
    return error;

  switch (reg) {
  case gpr_r0_arm:
    tls_context.R0 = reg_value.GetAsUInt32();
    break;
  case gpr_r1_arm:
    tls_context.R1 = reg_value.GetAsUInt32();
    break;
  case gpr_r2_arm:
    tls_context.R2 = reg_value.GetAsUInt32();
    break;
  case gpr_r3_arm:
    tls_context.R3 = reg_value.GetAsUInt32();
    break;
  case gpr_r4_arm:
    tls_context.R4 = reg_value.GetAsUInt32();
    break;
  case gpr_r5_arm:
    tls_context.R5 = reg_value.GetAsUInt32();
    break;
  case gpr_r6_arm:
    tls_context.R6 = reg_value.GetAsUInt32();
    break;
  case gpr_r7_arm:
    tls_context.R7 = reg_value.GetAsUInt32();
    break;
  case gpr_r8_arm:
    tls_context.R8 = reg_value.GetAsUInt32();
    break;
  case gpr_r9_arm:
    tls_context.R9 = reg_value.GetAsUInt32();
    break;
  case gpr_r10_arm:
    tls_context.R10 = reg_value.GetAsUInt32();
    break;
  case gpr_r11_arm:
    tls_context.R11 = reg_value.GetAsUInt32();
    break;
  case gpr_r12_arm:
    tls_context.R12 = reg_value.GetAsUInt32();
    break;
  case gpr_sp_arm:
    tls_context.Sp = reg_value.GetAsUInt32();
    break;
  case gpr_lr_arm:
    tls_context.Lr = reg_value.GetAsUInt32();
    break;
  case gpr_pc_arm:
    tls_context.Pc = reg_value.GetAsUInt32();
    break;
  case gpr_cpsr_arm:
    tls_context.Cpsr = reg_value.GetAsUInt32();
    break;
  }

  return SetThreadContextHelper(thread_handle, &tls_context);
}

Status NativeRegisterContextWindows_arm::FPRRead(const uint32_t reg,
                                                 RegisterValue &reg_value) {
  ::CONTEXT tls_context;
  DWORD context_flag = CONTEXT_CONTROL | CONTEXT_FLOATING_POINT;
  Status error =
      GetThreadContextHelper(GetThreadHandle(), &tls_context, context_flag);
  if (error.Fail())
    return error;

  switch (reg) {
  case fpu_s0_arm:
  case fpu_s1_arm:
  case fpu_s2_arm:
  case fpu_s3_arm:
  case fpu_s4_arm:
  case fpu_s5_arm:
  case fpu_s6_arm:
  case fpu_s7_arm:
  case fpu_s8_arm:
  case fpu_s9_arm:
  case fpu_s10_arm:
  case fpu_s11_arm:
  case fpu_s12_arm:
  case fpu_s13_arm:
  case fpu_s14_arm:
  case fpu_s15_arm:
  case fpu_s16_arm:
  case fpu_s17_arm:
  case fpu_s18_arm:
  case fpu_s19_arm:
  case fpu_s20_arm:
  case fpu_s21_arm:
  case fpu_s22_arm:
  case fpu_s23_arm:
  case fpu_s24_arm:
  case fpu_s25_arm:
  case fpu_s26_arm:
  case fpu_s27_arm:
  case fpu_s28_arm:
  case fpu_s29_arm:
  case fpu_s30_arm:
  case fpu_s31_arm:
    reg_value.SetUInt32(tls_context.S[reg - fpu_s0_arm],
                        RegisterValue::eTypeFloat);
    break;

  case fpu_d0_arm:
  case fpu_d1_arm:
  case fpu_d2_arm:
  case fpu_d3_arm:
  case fpu_d4_arm:
  case fpu_d5_arm:
  case fpu_d6_arm:
  case fpu_d7_arm:
  case fpu_d8_arm:
  case fpu_d9_arm:
  case fpu_d10_arm:
  case fpu_d11_arm:
  case fpu_d12_arm:
  case fpu_d13_arm:
  case fpu_d14_arm:
  case fpu_d15_arm:
  case fpu_d16_arm:
  case fpu_d17_arm:
  case fpu_d18_arm:
  case fpu_d19_arm:
  case fpu_d20_arm:
  case fpu_d21_arm:
  case fpu_d22_arm:
  case fpu_d23_arm:
  case fpu_d24_arm:
  case fpu_d25_arm:
  case fpu_d26_arm:
  case fpu_d27_arm:
  case fpu_d28_arm:
  case fpu_d29_arm:
  case fpu_d30_arm:
  case fpu_d31_arm:
    reg_value.SetUInt64(tls_context.D[reg - fpu_d0_arm],
                        RegisterValue::eTypeDouble);
    break;

  case fpu_q0_arm:
  case fpu_q1_arm:
  case fpu_q2_arm:
  case fpu_q3_arm:
  case fpu_q4_arm:
  case fpu_q5_arm:
  case fpu_q6_arm:
  case fpu_q7_arm:
  case fpu_q8_arm:
  case fpu_q9_arm:
  case fpu_q10_arm:
  case fpu_q11_arm:
  case fpu_q12_arm:
  case fpu_q13_arm:
  case fpu_q14_arm:
  case fpu_q15_arm:
    reg_value.SetBytes(&tls_context.Q[reg - fpu_q0_arm], 16,
                       endian::InlHostByteOrder());
    break;

  case fpu_fpscr_arm:
    reg_value.SetUInt32(tls_context.Fpscr);
    break;
  }

  return error;
}

Status
NativeRegisterContextWindows_arm::FPRWrite(const uint32_t reg,
                                           const RegisterValue &reg_value) {
  ::CONTEXT tls_context;
  DWORD context_flag = CONTEXT_CONTROL | CONTEXT_FLOATING_POINT;
  auto thread_handle = GetThreadHandle();
  Status error =
      GetThreadContextHelper(thread_handle, &tls_context, context_flag);
  if (error.Fail())
    return error;

  switch (reg) {
  case fpu_s0_arm:
  case fpu_s1_arm:
  case fpu_s2_arm:
  case fpu_s3_arm:
  case fpu_s4_arm:
  case fpu_s5_arm:
  case fpu_s6_arm:
  case fpu_s7_arm:
  case fpu_s8_arm:
  case fpu_s9_arm:
  case fpu_s10_arm:
  case fpu_s11_arm:
  case fpu_s12_arm:
  case fpu_s13_arm:
  case fpu_s14_arm:
  case fpu_s15_arm:
  case fpu_s16_arm:
  case fpu_s17_arm:
  case fpu_s18_arm:
  case fpu_s19_arm:
  case fpu_s20_arm:
  case fpu_s21_arm:
  case fpu_s22_arm:
  case fpu_s23_arm:
  case fpu_s24_arm:
  case fpu_s25_arm:
  case fpu_s26_arm:
  case fpu_s27_arm:
  case fpu_s28_arm:
  case fpu_s29_arm:
  case fpu_s30_arm:
  case fpu_s31_arm:
    tls_context.S[reg - fpu_s0_arm] = reg_value.GetAsUInt32();
    break;

  case fpu_d0_arm:
  case fpu_d1_arm:
  case fpu_d2_arm:
  case fpu_d3_arm:
  case fpu_d4_arm:
  case fpu_d5_arm:
  case fpu_d6_arm:
  case fpu_d7_arm:
  case fpu_d8_arm:
  case fpu_d9_arm:
  case fpu_d10_arm:
  case fpu_d11_arm:
  case fpu_d12_arm:
  case fpu_d13_arm:
  case fpu_d14_arm:
  case fpu_d15_arm:
  case fpu_d16_arm:
  case fpu_d17_arm:
  case fpu_d18_arm:
  case fpu_d19_arm:
  case fpu_d20_arm:
  case fpu_d21_arm:
  case fpu_d22_arm:
  case fpu_d23_arm:
  case fpu_d24_arm:
  case fpu_d25_arm:
  case fpu_d26_arm:
  case fpu_d27_arm:
  case fpu_d28_arm:
  case fpu_d29_arm:
  case fpu_d30_arm:
  case fpu_d31_arm:
    tls_context.D[reg - fpu_d0_arm] = reg_value.GetAsUInt64();
    break;

  case fpu_q0_arm:
  case fpu_q1_arm:
  case fpu_q2_arm:
  case fpu_q3_arm:
  case fpu_q4_arm:
  case fpu_q5_arm:
  case fpu_q6_arm:
  case fpu_q7_arm:
  case fpu_q8_arm:
  case fpu_q9_arm:
  case fpu_q10_arm:
  case fpu_q11_arm:
  case fpu_q12_arm:
  case fpu_q13_arm:
  case fpu_q14_arm:
  case fpu_q15_arm:
    memcpy(&tls_context.Q[reg - fpu_q0_arm], reg_value.GetBytes(), 16);
    break;

  case fpu_fpscr_arm:
    tls_context.Fpscr = reg_value.GetAsUInt32();
    break;
  }

  return SetThreadContextHelper(thread_handle, &tls_context);
}

Status
NativeRegisterContextWindows_arm::ReadRegister(const RegisterInfo *reg_info,
                                               RegisterValue &reg_value) {
  Status error;
  if (!reg_info) {
    error.SetErrorString("reg_info NULL");
    return error;
  }

  const uint32_t reg = reg_info->kinds[lldb::eRegisterKindLLDB];
  if (reg == LLDB_INVALID_REGNUM) {
    // This is likely an internal register for lldb use only and should not be
    // directly queried.
    error.SetErrorStringWithFormat("register \"%s\" is an internal-only lldb "
                                   "register, cannot read directly",
                                   reg_info->name);
    return error;
  }

  if (IsGPR(reg))
    return GPRRead(reg, reg_value);

  if (IsFPR(reg))
    return FPRRead(reg, reg_value);

  return Status("unimplemented");
}

Status NativeRegisterContextWindows_arm::WriteRegister(
    const RegisterInfo *reg_info, const RegisterValue &reg_value) {
  Status error;

  if (!reg_info) {
    error.SetErrorString("reg_info NULL");
    return error;
  }

  const uint32_t reg = reg_info->kinds[lldb::eRegisterKindLLDB];
  if (reg == LLDB_INVALID_REGNUM) {
    // This is likely an internal register for lldb use only and should not be
    // directly written.
    error.SetErrorStringWithFormat("register \"%s\" is an internal-only lldb "
                                   "register, cannot write directly",
                                   reg_info->name);
    return error;
  }

  if (IsGPR(reg))
    return GPRWrite(reg, reg_value);

  if (IsFPR(reg))
    return FPRWrite(reg, reg_value);

  return Status("unimplemented");
}

Status NativeRegisterContextWindows_arm::ReadAllRegisterValues(
    lldb::WritableDataBufferSP &data_sp) {
  const size_t data_size = REG_CONTEXT_SIZE;
  data_sp = std::make_shared<DataBufferHeap>(data_size, 0);
  ::CONTEXT tls_context;
  Status error =
      GetThreadContextHelper(GetThreadHandle(), &tls_context, CONTEXT_ALL);
  if (error.Fail())
    return error;

  uint8_t *dst = data_sp->GetBytes();
  ::memcpy(dst, &tls_context, data_size);
  return error;
}

Status NativeRegisterContextWindows_arm::WriteAllRegisterValues(
    const lldb::DataBufferSP &data_sp) {
  Status error;
  const size_t data_size = REG_CONTEXT_SIZE;
  if (!data_sp) {
    error.SetErrorStringWithFormat(
        "NativeRegisterContextWindows_arm::%s invalid data_sp provided",
        __FUNCTION__);
    return error;
  }

  if (data_sp->GetByteSize() != data_size) {
    error.SetErrorStringWithFormatv(
        "data_sp contained mismatched data size, expected {0}, actual {1}",
        data_size, data_sp->GetByteSize());
    return error;
  }

  ::CONTEXT tls_context;
  memcpy(&tls_context, data_sp->GetBytes(), data_size);
  return SetThreadContextHelper(GetThreadHandle(), &tls_context);
}

Status NativeRegisterContextWindows_arm::IsWatchpointHit(uint32_t wp_index,
                                                         bool &is_hit) {
  return Status("unimplemented");
}

Status NativeRegisterContextWindows_arm::GetWatchpointHitIndex(
    uint32_t &wp_index, lldb::addr_t trap_addr) {
  return Status("unimplemented");
}

Status NativeRegisterContextWindows_arm::IsWatchpointVacant(uint32_t wp_index,
                                                            bool &is_vacant) {
  return Status("unimplemented");
}

Status NativeRegisterContextWindows_arm::SetHardwareWatchpointWithIndex(
    lldb::addr_t addr, size_t size, uint32_t watch_flags, uint32_t wp_index) {
  return Status("unimplemented");
}

bool NativeRegisterContextWindows_arm::ClearHardwareWatchpoint(
    uint32_t wp_index) {
  return false;
}

Status NativeRegisterContextWindows_arm::ClearAllHardwareWatchpoints() {
  return Status("unimplemented");
}

uint32_t NativeRegisterContextWindows_arm::SetHardwareWatchpoint(
    lldb::addr_t addr, size_t size, uint32_t watch_flags) {
  return LLDB_INVALID_INDEX32;
}

lldb::addr_t
NativeRegisterContextWindows_arm::GetWatchpointAddress(uint32_t wp_index) {
  return LLDB_INVALID_ADDRESS;
}

uint32_t NativeRegisterContextWindows_arm::NumSupportedHardwareWatchpoints() {
  // Not implemented
  return 0;
}

#endif // defined(__arm__) || defined(_M_ARM)
