//===-- NativeRegisterContextWindows_arm64.cpp ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#if defined(__aarch64__) || defined(_M_ARM64)

#include "NativeRegisterContextWindows_arm64.h"
#include "NativeThreadWindows.h"
#include "Plugins/Process/Utility/RegisterInfoPOSIX_arm64.h"
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
static const uint32_t g_gpr_regnums_arm64[] = {
    gpr_x0_arm64,       gpr_x1_arm64,   gpr_x2_arm64,  gpr_x3_arm64,
    gpr_x4_arm64,       gpr_x5_arm64,   gpr_x6_arm64,  gpr_x7_arm64,
    gpr_x8_arm64,       gpr_x9_arm64,   gpr_x10_arm64, gpr_x11_arm64,
    gpr_x12_arm64,      gpr_x13_arm64,  gpr_x14_arm64, gpr_x15_arm64,
    gpr_x16_arm64,      gpr_x17_arm64,  gpr_x18_arm64, gpr_x19_arm64,
    gpr_x20_arm64,      gpr_x21_arm64,  gpr_x22_arm64, gpr_x23_arm64,
    gpr_x24_arm64,      gpr_x25_arm64,  gpr_x26_arm64, gpr_x27_arm64,
    gpr_x28_arm64,      gpr_fp_arm64,   gpr_lr_arm64,  gpr_sp_arm64,
    gpr_pc_arm64,       gpr_cpsr_arm64, gpr_w0_arm64,  gpr_w1_arm64,
    gpr_w2_arm64,       gpr_w3_arm64,   gpr_w4_arm64,  gpr_w5_arm64,
    gpr_w6_arm64,       gpr_w7_arm64,   gpr_w8_arm64,  gpr_w9_arm64,
    gpr_w10_arm64,      gpr_w11_arm64,  gpr_w12_arm64, gpr_w13_arm64,
    gpr_w14_arm64,      gpr_w15_arm64,  gpr_w16_arm64, gpr_w17_arm64,
    gpr_w18_arm64,      gpr_w19_arm64,  gpr_w20_arm64, gpr_w21_arm64,
    gpr_w22_arm64,      gpr_w23_arm64,  gpr_w24_arm64, gpr_w25_arm64,
    gpr_w26_arm64,      gpr_w27_arm64,  gpr_w28_arm64,
    LLDB_INVALID_REGNUM // Register set must be terminated with this flag
};
static_assert(((sizeof g_gpr_regnums_arm64 / sizeof g_gpr_regnums_arm64[0]) -
               1) == k_num_gpr_registers_arm64,
              "g_gpr_regnums_arm64 has wrong number of register infos");

static const uint32_t g_fpr_regnums_arm64[] = {
    fpu_v0_arm64,       fpu_v1_arm64,   fpu_v2_arm64,  fpu_v3_arm64,
    fpu_v4_arm64,       fpu_v5_arm64,   fpu_v6_arm64,  fpu_v7_arm64,
    fpu_v8_arm64,       fpu_v9_arm64,   fpu_v10_arm64, fpu_v11_arm64,
    fpu_v12_arm64,      fpu_v13_arm64,  fpu_v14_arm64, fpu_v15_arm64,
    fpu_v16_arm64,      fpu_v17_arm64,  fpu_v18_arm64, fpu_v19_arm64,
    fpu_v20_arm64,      fpu_v21_arm64,  fpu_v22_arm64, fpu_v23_arm64,
    fpu_v24_arm64,      fpu_v25_arm64,  fpu_v26_arm64, fpu_v27_arm64,
    fpu_v28_arm64,      fpu_v29_arm64,  fpu_v30_arm64, fpu_v31_arm64,
    fpu_s0_arm64,       fpu_s1_arm64,   fpu_s2_arm64,  fpu_s3_arm64,
    fpu_s4_arm64,       fpu_s5_arm64,   fpu_s6_arm64,  fpu_s7_arm64,
    fpu_s8_arm64,       fpu_s9_arm64,   fpu_s10_arm64, fpu_s11_arm64,
    fpu_s12_arm64,      fpu_s13_arm64,  fpu_s14_arm64, fpu_s15_arm64,
    fpu_s16_arm64,      fpu_s17_arm64,  fpu_s18_arm64, fpu_s19_arm64,
    fpu_s20_arm64,      fpu_s21_arm64,  fpu_s22_arm64, fpu_s23_arm64,
    fpu_s24_arm64,      fpu_s25_arm64,  fpu_s26_arm64, fpu_s27_arm64,
    fpu_s28_arm64,      fpu_s29_arm64,  fpu_s30_arm64, fpu_s31_arm64,

    fpu_d0_arm64,       fpu_d1_arm64,   fpu_d2_arm64,  fpu_d3_arm64,
    fpu_d4_arm64,       fpu_d5_arm64,   fpu_d6_arm64,  fpu_d7_arm64,
    fpu_d8_arm64,       fpu_d9_arm64,   fpu_d10_arm64, fpu_d11_arm64,
    fpu_d12_arm64,      fpu_d13_arm64,  fpu_d14_arm64, fpu_d15_arm64,
    fpu_d16_arm64,      fpu_d17_arm64,  fpu_d18_arm64, fpu_d19_arm64,
    fpu_d20_arm64,      fpu_d21_arm64,  fpu_d22_arm64, fpu_d23_arm64,
    fpu_d24_arm64,      fpu_d25_arm64,  fpu_d26_arm64, fpu_d27_arm64,
    fpu_d28_arm64,      fpu_d29_arm64,  fpu_d30_arm64, fpu_d31_arm64,
    fpu_fpsr_arm64,     fpu_fpcr_arm64,
    LLDB_INVALID_REGNUM // Register set must be terminated with this flag
};
static_assert(((sizeof g_fpr_regnums_arm64 / sizeof g_fpr_regnums_arm64[0]) -
               1) == k_num_fpr_registers_arm64,
              "g_fpu_regnums_arm64 has wrong number of register infos");

static const RegisterSet g_reg_sets_arm64[] = {
    {"General Purpose Registers", "gpr", std::size(g_gpr_regnums_arm64) - 1,
     g_gpr_regnums_arm64},
    {"Floating Point Registers", "fpr", std::size(g_fpr_regnums_arm64) - 1,
     g_fpr_regnums_arm64},
};

enum { k_num_register_sets = 2 };

} // namespace

static RegisterInfoInterface *
CreateRegisterInfoInterface(const ArchSpec &target_arch) {
  assert((HostInfo::GetArchitecture().GetAddressByteSize() == 8) &&
         "Register setting path assumes this is a 64-bit host");
  return new RegisterInfoPOSIX_arm64(
      target_arch, RegisterInfoPOSIX_arm64::eRegsetMaskDefault);
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
  // Register context for a native 64-bit application.
  return std::make_unique<NativeRegisterContextWindows_arm64>(target_arch,
                                                              native_thread);
}

NativeRegisterContextWindows_arm64::NativeRegisterContextWindows_arm64(
    const ArchSpec &target_arch, NativeThreadProtocol &native_thread)
    : NativeRegisterContextWindows(native_thread,
                                   CreateRegisterInfoInterface(target_arch)) {}

bool NativeRegisterContextWindows_arm64::IsGPR(uint32_t reg_index) const {
  return (reg_index >= k_first_gpr_arm64 && reg_index <= k_last_gpr_arm64);
}

bool NativeRegisterContextWindows_arm64::IsFPR(uint32_t reg_index) const {
  return (reg_index >= k_first_fpr_arm64 && reg_index <= k_last_fpr_arm64);
}

uint32_t NativeRegisterContextWindows_arm64::GetRegisterSetCount() const {
  return k_num_register_sets;
}

const RegisterSet *
NativeRegisterContextWindows_arm64::GetRegisterSet(uint32_t set_index) const {
  if (set_index >= k_num_register_sets)
    return nullptr;
  return &g_reg_sets_arm64[set_index];
}

Status NativeRegisterContextWindows_arm64::GPRRead(const uint32_t reg,
                                                   RegisterValue &reg_value) {
  ::CONTEXT tls_context;
  DWORD context_flag = CONTEXT_CONTROL | CONTEXT_INTEGER;
  Status error =
      GetThreadContextHelper(GetThreadHandle(), &tls_context, context_flag);
  if (error.Fail())
    return error;

  switch (reg) {
  case gpr_x0_arm64:
  case gpr_x1_arm64:
  case gpr_x2_arm64:
  case gpr_x3_arm64:
  case gpr_x4_arm64:
  case gpr_x5_arm64:
  case gpr_x6_arm64:
  case gpr_x7_arm64:
  case gpr_x8_arm64:
  case gpr_x9_arm64:
  case gpr_x10_arm64:
  case gpr_x11_arm64:
  case gpr_x12_arm64:
  case gpr_x13_arm64:
  case gpr_x14_arm64:
  case gpr_x15_arm64:
  case gpr_x16_arm64:
  case gpr_x17_arm64:
  case gpr_x18_arm64:
  case gpr_x19_arm64:
  case gpr_x20_arm64:
  case gpr_x21_arm64:
  case gpr_x22_arm64:
  case gpr_x23_arm64:
  case gpr_x24_arm64:
  case gpr_x25_arm64:
  case gpr_x26_arm64:
  case gpr_x27_arm64:
  case gpr_x28_arm64:
    reg_value.SetUInt64(tls_context.X[reg - gpr_x0_arm64]);
    break;

  case gpr_fp_arm64:
    reg_value.SetUInt64(tls_context.Fp);
    break;
  case gpr_sp_arm64:
    reg_value.SetUInt64(tls_context.Sp);
    break;
  case gpr_lr_arm64:
    reg_value.SetUInt64(tls_context.Lr);
    break;
  case gpr_pc_arm64:
    reg_value.SetUInt64(tls_context.Pc);
    break;
  case gpr_cpsr_arm64:
    reg_value.SetUInt32(tls_context.Cpsr);
    break;

  case gpr_w0_arm64:
  case gpr_w1_arm64:
  case gpr_w2_arm64:
  case gpr_w3_arm64:
  case gpr_w4_arm64:
  case gpr_w5_arm64:
  case gpr_w6_arm64:
  case gpr_w7_arm64:
  case gpr_w8_arm64:
  case gpr_w9_arm64:
  case gpr_w10_arm64:
  case gpr_w11_arm64:
  case gpr_w12_arm64:
  case gpr_w13_arm64:
  case gpr_w14_arm64:
  case gpr_w15_arm64:
  case gpr_w16_arm64:
  case gpr_w17_arm64:
  case gpr_w18_arm64:
  case gpr_w19_arm64:
  case gpr_w20_arm64:
  case gpr_w21_arm64:
  case gpr_w22_arm64:
  case gpr_w23_arm64:
  case gpr_w24_arm64:
  case gpr_w25_arm64:
  case gpr_w26_arm64:
  case gpr_w27_arm64:
  case gpr_w28_arm64:
    reg_value.SetUInt32(
        static_cast<uint32_t>(tls_context.X[reg - gpr_w0_arm64] & 0xffffffff));
    break;
  }

  return error;
}

Status
NativeRegisterContextWindows_arm64::GPRWrite(const uint32_t reg,
                                             const RegisterValue &reg_value) {
  ::CONTEXT tls_context;
  DWORD context_flag = CONTEXT_CONTROL | CONTEXT_INTEGER;
  auto thread_handle = GetThreadHandle();
  Status error =
      GetThreadContextHelper(thread_handle, &tls_context, context_flag);
  if (error.Fail())
    return error;

  switch (reg) {
  case gpr_x0_arm64:
  case gpr_x1_arm64:
  case gpr_x2_arm64:
  case gpr_x3_arm64:
  case gpr_x4_arm64:
  case gpr_x5_arm64:
  case gpr_x6_arm64:
  case gpr_x7_arm64:
  case gpr_x8_arm64:
  case gpr_x9_arm64:
  case gpr_x10_arm64:
  case gpr_x11_arm64:
  case gpr_x12_arm64:
  case gpr_x13_arm64:
  case gpr_x14_arm64:
  case gpr_x15_arm64:
  case gpr_x16_arm64:
  case gpr_x17_arm64:
  case gpr_x18_arm64:
  case gpr_x19_arm64:
  case gpr_x20_arm64:
  case gpr_x21_arm64:
  case gpr_x22_arm64:
  case gpr_x23_arm64:
  case gpr_x24_arm64:
  case gpr_x25_arm64:
  case gpr_x26_arm64:
  case gpr_x27_arm64:
  case gpr_x28_arm64:
    tls_context.X[reg - gpr_x0_arm64] = reg_value.GetAsUInt64();
    break;

  case gpr_fp_arm64:
    tls_context.Fp = reg_value.GetAsUInt64();
    break;
  case gpr_sp_arm64:
    tls_context.Sp = reg_value.GetAsUInt64();
    break;
  case gpr_lr_arm64:
    tls_context.Lr = reg_value.GetAsUInt64();
    break;
  case gpr_pc_arm64:
    tls_context.Pc = reg_value.GetAsUInt64();
    break;
  case gpr_cpsr_arm64:
    tls_context.Cpsr = reg_value.GetAsUInt32();
    break;

  case gpr_w0_arm64:
  case gpr_w1_arm64:
  case gpr_w2_arm64:
  case gpr_w3_arm64:
  case gpr_w4_arm64:
  case gpr_w5_arm64:
  case gpr_w6_arm64:
  case gpr_w7_arm64:
  case gpr_w8_arm64:
  case gpr_w9_arm64:
  case gpr_w10_arm64:
  case gpr_w11_arm64:
  case gpr_w12_arm64:
  case gpr_w13_arm64:
  case gpr_w14_arm64:
  case gpr_w15_arm64:
  case gpr_w16_arm64:
  case gpr_w17_arm64:
  case gpr_w18_arm64:
  case gpr_w19_arm64:
  case gpr_w20_arm64:
  case gpr_w21_arm64:
  case gpr_w22_arm64:
  case gpr_w23_arm64:
  case gpr_w24_arm64:
  case gpr_w25_arm64:
  case gpr_w26_arm64:
  case gpr_w27_arm64:
  case gpr_w28_arm64:
    tls_context.X[reg - gpr_w0_arm64] = reg_value.GetAsUInt32();
    break;
  }

  return SetThreadContextHelper(thread_handle, &tls_context);
}

Status NativeRegisterContextWindows_arm64::FPRRead(const uint32_t reg,
                                                   RegisterValue &reg_value) {
  ::CONTEXT tls_context;
  DWORD context_flag = CONTEXT_CONTROL | CONTEXT_FLOATING_POINT;
  Status error =
      GetThreadContextHelper(GetThreadHandle(), &tls_context, context_flag);
  if (error.Fail())
    return error;

  switch (reg) {
  case fpu_v0_arm64:
  case fpu_v1_arm64:
  case fpu_v2_arm64:
  case fpu_v3_arm64:
  case fpu_v4_arm64:
  case fpu_v5_arm64:
  case fpu_v6_arm64:
  case fpu_v7_arm64:
  case fpu_v8_arm64:
  case fpu_v9_arm64:
  case fpu_v10_arm64:
  case fpu_v11_arm64:
  case fpu_v12_arm64:
  case fpu_v13_arm64:
  case fpu_v14_arm64:
  case fpu_v15_arm64:
  case fpu_v16_arm64:
  case fpu_v17_arm64:
  case fpu_v18_arm64:
  case fpu_v19_arm64:
  case fpu_v20_arm64:
  case fpu_v21_arm64:
  case fpu_v22_arm64:
  case fpu_v23_arm64:
  case fpu_v24_arm64:
  case fpu_v25_arm64:
  case fpu_v26_arm64:
  case fpu_v27_arm64:
  case fpu_v28_arm64:
  case fpu_v29_arm64:
  case fpu_v30_arm64:
  case fpu_v31_arm64:
    reg_value.SetBytes(tls_context.V[reg - fpu_v0_arm64].B, 16,
                       endian::InlHostByteOrder());
    break;

  case fpu_s0_arm64:
  case fpu_s1_arm64:
  case fpu_s2_arm64:
  case fpu_s3_arm64:
  case fpu_s4_arm64:
  case fpu_s5_arm64:
  case fpu_s6_arm64:
  case fpu_s7_arm64:
  case fpu_s8_arm64:
  case fpu_s9_arm64:
  case fpu_s10_arm64:
  case fpu_s11_arm64:
  case fpu_s12_arm64:
  case fpu_s13_arm64:
  case fpu_s14_arm64:
  case fpu_s15_arm64:
  case fpu_s16_arm64:
  case fpu_s17_arm64:
  case fpu_s18_arm64:
  case fpu_s19_arm64:
  case fpu_s20_arm64:
  case fpu_s21_arm64:
  case fpu_s22_arm64:
  case fpu_s23_arm64:
  case fpu_s24_arm64:
  case fpu_s25_arm64:
  case fpu_s26_arm64:
  case fpu_s27_arm64:
  case fpu_s28_arm64:
  case fpu_s29_arm64:
  case fpu_s30_arm64:
  case fpu_s31_arm64:
    reg_value.SetFloat(tls_context.V[reg - fpu_s0_arm64].S[0]);
    break;

  case fpu_d0_arm64:
  case fpu_d1_arm64:
  case fpu_d2_arm64:
  case fpu_d3_arm64:
  case fpu_d4_arm64:
  case fpu_d5_arm64:
  case fpu_d6_arm64:
  case fpu_d7_arm64:
  case fpu_d8_arm64:
  case fpu_d9_arm64:
  case fpu_d10_arm64:
  case fpu_d11_arm64:
  case fpu_d12_arm64:
  case fpu_d13_arm64:
  case fpu_d14_arm64:
  case fpu_d15_arm64:
  case fpu_d16_arm64:
  case fpu_d17_arm64:
  case fpu_d18_arm64:
  case fpu_d19_arm64:
  case fpu_d20_arm64:
  case fpu_d21_arm64:
  case fpu_d22_arm64:
  case fpu_d23_arm64:
  case fpu_d24_arm64:
  case fpu_d25_arm64:
  case fpu_d26_arm64:
  case fpu_d27_arm64:
  case fpu_d28_arm64:
  case fpu_d29_arm64:
  case fpu_d30_arm64:
  case fpu_d31_arm64:
    reg_value.SetDouble(tls_context.V[reg - fpu_d0_arm64].D[0]);
    break;

  case fpu_fpsr_arm64:
    reg_value.SetUInt32(tls_context.Fpsr);
    break;

  case fpu_fpcr_arm64:
    reg_value.SetUInt32(tls_context.Fpcr);
    break;
  }

  return error;
}

Status
NativeRegisterContextWindows_arm64::FPRWrite(const uint32_t reg,
                                             const RegisterValue &reg_value) {
  ::CONTEXT tls_context;
  DWORD context_flag = CONTEXT_CONTROL | CONTEXT_FLOATING_POINT;
  auto thread_handle = GetThreadHandle();
  Status error =
      GetThreadContextHelper(thread_handle, &tls_context, context_flag);
  if (error.Fail())
    return error;

  switch (reg) {
  case fpu_v0_arm64:
  case fpu_v1_arm64:
  case fpu_v2_arm64:
  case fpu_v3_arm64:
  case fpu_v4_arm64:
  case fpu_v5_arm64:
  case fpu_v6_arm64:
  case fpu_v7_arm64:
  case fpu_v8_arm64:
  case fpu_v9_arm64:
  case fpu_v10_arm64:
  case fpu_v11_arm64:
  case fpu_v12_arm64:
  case fpu_v13_arm64:
  case fpu_v14_arm64:
  case fpu_v15_arm64:
  case fpu_v16_arm64:
  case fpu_v17_arm64:
  case fpu_v18_arm64:
  case fpu_v19_arm64:
  case fpu_v20_arm64:
  case fpu_v21_arm64:
  case fpu_v22_arm64:
  case fpu_v23_arm64:
  case fpu_v24_arm64:
  case fpu_v25_arm64:
  case fpu_v26_arm64:
  case fpu_v27_arm64:
  case fpu_v28_arm64:
  case fpu_v29_arm64:
  case fpu_v30_arm64:
  case fpu_v31_arm64:
    memcpy(tls_context.V[reg - fpu_v0_arm64].B, reg_value.GetBytes(), 16);
    break;

  case fpu_s0_arm64:
  case fpu_s1_arm64:
  case fpu_s2_arm64:
  case fpu_s3_arm64:
  case fpu_s4_arm64:
  case fpu_s5_arm64:
  case fpu_s6_arm64:
  case fpu_s7_arm64:
  case fpu_s8_arm64:
  case fpu_s9_arm64:
  case fpu_s10_arm64:
  case fpu_s11_arm64:
  case fpu_s12_arm64:
  case fpu_s13_arm64:
  case fpu_s14_arm64:
  case fpu_s15_arm64:
  case fpu_s16_arm64:
  case fpu_s17_arm64:
  case fpu_s18_arm64:
  case fpu_s19_arm64:
  case fpu_s20_arm64:
  case fpu_s21_arm64:
  case fpu_s22_arm64:
  case fpu_s23_arm64:
  case fpu_s24_arm64:
  case fpu_s25_arm64:
  case fpu_s26_arm64:
  case fpu_s27_arm64:
  case fpu_s28_arm64:
  case fpu_s29_arm64:
  case fpu_s30_arm64:
  case fpu_s31_arm64:
    tls_context.V[reg - fpu_s0_arm64].S[0] = reg_value.GetAsFloat();
    break;

  case fpu_d0_arm64:
  case fpu_d1_arm64:
  case fpu_d2_arm64:
  case fpu_d3_arm64:
  case fpu_d4_arm64:
  case fpu_d5_arm64:
  case fpu_d6_arm64:
  case fpu_d7_arm64:
  case fpu_d8_arm64:
  case fpu_d9_arm64:
  case fpu_d10_arm64:
  case fpu_d11_arm64:
  case fpu_d12_arm64:
  case fpu_d13_arm64:
  case fpu_d14_arm64:
  case fpu_d15_arm64:
  case fpu_d16_arm64:
  case fpu_d17_arm64:
  case fpu_d18_arm64:
  case fpu_d19_arm64:
  case fpu_d20_arm64:
  case fpu_d21_arm64:
  case fpu_d22_arm64:
  case fpu_d23_arm64:
  case fpu_d24_arm64:
  case fpu_d25_arm64:
  case fpu_d26_arm64:
  case fpu_d27_arm64:
  case fpu_d28_arm64:
  case fpu_d29_arm64:
  case fpu_d30_arm64:
  case fpu_d31_arm64:
    tls_context.V[reg - fpu_d0_arm64].D[0] = reg_value.GetAsDouble();
    break;

  case fpu_fpsr_arm64:
    tls_context.Fpsr = reg_value.GetAsUInt32();
    break;

  case fpu_fpcr_arm64:
    tls_context.Fpcr = reg_value.GetAsUInt32();
    break;
  }

  return SetThreadContextHelper(thread_handle, &tls_context);
}

Status
NativeRegisterContextWindows_arm64::ReadRegister(const RegisterInfo *reg_info,
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

Status NativeRegisterContextWindows_arm64::WriteRegister(
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

Status NativeRegisterContextWindows_arm64::ReadAllRegisterValues(
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

Status NativeRegisterContextWindows_arm64::WriteAllRegisterValues(
    const lldb::DataBufferSP &data_sp) {
  Status error;
  const size_t data_size = REG_CONTEXT_SIZE;
  if (!data_sp) {
    error.SetErrorStringWithFormat(
        "NativeRegisterContextWindows_arm64::%s invalid data_sp provided",
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

Status NativeRegisterContextWindows_arm64::IsWatchpointHit(uint32_t wp_index,
                                                           bool &is_hit) {
  return Status("unimplemented");
}

Status NativeRegisterContextWindows_arm64::GetWatchpointHitIndex(
    uint32_t &wp_index, lldb::addr_t trap_addr) {
  return Status("unimplemented");
}

Status NativeRegisterContextWindows_arm64::IsWatchpointVacant(uint32_t wp_index,
                                                              bool &is_vacant) {
  return Status("unimplemented");
}

Status NativeRegisterContextWindows_arm64::SetHardwareWatchpointWithIndex(
    lldb::addr_t addr, size_t size, uint32_t watch_flags, uint32_t wp_index) {
  return Status("unimplemented");
}

bool NativeRegisterContextWindows_arm64::ClearHardwareWatchpoint(
    uint32_t wp_index) {
  return false;
}

Status NativeRegisterContextWindows_arm64::ClearAllHardwareWatchpoints() {
  return Status("unimplemented");
}

uint32_t NativeRegisterContextWindows_arm64::SetHardwareWatchpoint(
    lldb::addr_t addr, size_t size, uint32_t watch_flags) {
  return LLDB_INVALID_INDEX32;
}

lldb::addr_t
NativeRegisterContextWindows_arm64::GetWatchpointAddress(uint32_t wp_index) {
  return LLDB_INVALID_ADDRESS;
}

uint32_t NativeRegisterContextWindows_arm64::NumSupportedHardwareWatchpoints() {
  // Not implemented
  return 0;
}

#endif // defined(__aarch64__) || defined(_M_ARM64)
