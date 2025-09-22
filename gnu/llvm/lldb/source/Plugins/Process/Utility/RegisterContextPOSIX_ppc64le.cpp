//===-- RegisterContextPOSIX_ppc64le.cpp ----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <cerrno>
#include <cstdint>
#include <cstring>

#include "lldb/Target/Process.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/Endian.h"
#include "lldb/Utility/RegisterValue.h"
#include "lldb/Utility/Scalar.h"
#include "llvm/Support/Compiler.h"

#include "RegisterContextPOSIX_ppc64le.h"

using namespace lldb_private;
using namespace lldb;

static const uint32_t g_gpr_regnums[] = {
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
};

static const uint32_t g_fpr_regnums[] = {
    fpr_f0_ppc64le,    fpr_f1_ppc64le,  fpr_f2_ppc64le,  fpr_f3_ppc64le,
    fpr_f4_ppc64le,    fpr_f5_ppc64le,  fpr_f6_ppc64le,  fpr_f7_ppc64le,
    fpr_f8_ppc64le,    fpr_f9_ppc64le,  fpr_f10_ppc64le, fpr_f11_ppc64le,
    fpr_f12_ppc64le,   fpr_f13_ppc64le, fpr_f14_ppc64le, fpr_f15_ppc64le,
    fpr_f16_ppc64le,   fpr_f17_ppc64le, fpr_f18_ppc64le, fpr_f19_ppc64le,
    fpr_f20_ppc64le,   fpr_f21_ppc64le, fpr_f22_ppc64le, fpr_f23_ppc64le,
    fpr_f24_ppc64le,   fpr_f25_ppc64le, fpr_f26_ppc64le, fpr_f27_ppc64le,
    fpr_f28_ppc64le,   fpr_f29_ppc64le, fpr_f30_ppc64le, fpr_f31_ppc64le,
    fpr_fpscr_ppc64le,
};

static const uint32_t g_vmx_regnums[] = {
    vmx_vr0_ppc64le,  vmx_vr1_ppc64le,    vmx_vr2_ppc64le,  vmx_vr3_ppc64le,
    vmx_vr4_ppc64le,  vmx_vr5_ppc64le,    vmx_vr6_ppc64le,  vmx_vr7_ppc64le,
    vmx_vr8_ppc64le,  vmx_vr9_ppc64le,    vmx_vr10_ppc64le, vmx_vr11_ppc64le,
    vmx_vr12_ppc64le, vmx_vr13_ppc64le,   vmx_vr14_ppc64le, vmx_vr15_ppc64le,
    vmx_vr16_ppc64le, vmx_vr17_ppc64le,   vmx_vr18_ppc64le, vmx_vr19_ppc64le,
    vmx_vr20_ppc64le, vmx_vr21_ppc64le,   vmx_vr22_ppc64le, vmx_vr23_ppc64le,
    vmx_vr24_ppc64le, vmx_vr25_ppc64le,   vmx_vr26_ppc64le, vmx_vr27_ppc64le,
    vmx_vr28_ppc64le, vmx_vr29_ppc64le,   vmx_vr30_ppc64le, vmx_vr31_ppc64le,
    vmx_vscr_ppc64le, vmx_vrsave_ppc64le,
};

static const uint32_t g_vsx_regnums[] = {
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
};

// Number of register sets provided by this context.
enum { k_num_register_sets = 4 };

static const RegisterSet g_reg_sets_ppc64le[k_num_register_sets] = {
    {"General Purpose Registers", "gpr", k_num_gpr_registers_ppc64le,
     g_gpr_regnums},
    {"Floating Point Registers", "fpr", k_num_fpr_registers_ppc64le,
     g_fpr_regnums},
    {"Altivec/VMX Registers", "vmx", k_num_vmx_registers_ppc64le,
     g_vmx_regnums},
    {"VSX Registers", "vsx", k_num_vsx_registers_ppc64le, g_vsx_regnums},
};

bool RegisterContextPOSIX_ppc64le::IsGPR(unsigned reg) {
  return (reg <= k_last_gpr_ppc64le); // GPR's come first.
}

bool RegisterContextPOSIX_ppc64le::IsFPR(unsigned reg) {
  return (reg >= k_first_fpr_ppc64le) && (reg <= k_last_fpr_ppc64le);
}

bool RegisterContextPOSIX_ppc64le::IsVMX(unsigned reg) {
  return (reg >= k_first_vmx_ppc64le) && (reg <= k_last_vmx_ppc64le);
}

bool RegisterContextPOSIX_ppc64le::IsVSX(unsigned reg) {
  return (reg >= k_first_vsx_ppc64le) && (reg <= k_last_vsx_ppc64le);
}

RegisterContextPOSIX_ppc64le::RegisterContextPOSIX_ppc64le(
    Thread &thread, uint32_t concrete_frame_idx,
    RegisterInfoInterface *register_info)
    : RegisterContext(thread, concrete_frame_idx) {
  m_register_info_up.reset(register_info);
}

void RegisterContextPOSIX_ppc64le::InvalidateAllRegisters() {}

unsigned RegisterContextPOSIX_ppc64le::GetRegisterOffset(unsigned reg) {
  assert(reg < k_num_registers_ppc64le && "Invalid register number.");
  return GetRegisterInfo()[reg].byte_offset;
}

unsigned RegisterContextPOSIX_ppc64le::GetRegisterSize(unsigned reg) {
  assert(reg < k_num_registers_ppc64le && "Invalid register number.");
  return GetRegisterInfo()[reg].byte_size;
}

size_t RegisterContextPOSIX_ppc64le::GetRegisterCount() {
  size_t num_registers = k_num_registers_ppc64le;
  return num_registers;
}

size_t RegisterContextPOSIX_ppc64le::GetGPRSize() {
  return m_register_info_up->GetGPRSize();
}

const RegisterInfo *RegisterContextPOSIX_ppc64le::GetRegisterInfo() {
  // Commonly, this method is overridden and g_register_infos is copied and
  // specialized. So, use GetRegisterInfo() rather than g_register_infos in
  // this scope.
  return m_register_info_up->GetRegisterInfo();
}

const RegisterInfo *
RegisterContextPOSIX_ppc64le::GetRegisterInfoAtIndex(size_t reg) {
  if (reg < k_num_registers_ppc64le)
    return &GetRegisterInfo()[reg];
  else
    return nullptr;
}

size_t RegisterContextPOSIX_ppc64le::GetRegisterSetCount() {
  size_t sets = 0;
  for (size_t set = 0; set < k_num_register_sets; ++set) {
    if (IsRegisterSetAvailable(set))
      ++sets;
  }

  return sets;
}

const RegisterSet *RegisterContextPOSIX_ppc64le::GetRegisterSet(size_t set) {
  if (IsRegisterSetAvailable(set))
    return &g_reg_sets_ppc64le[set];
  else
    return nullptr;
}

const char *RegisterContextPOSIX_ppc64le::GetRegisterName(unsigned reg) {
  assert(reg < k_num_registers_ppc64le && "Invalid register offset.");
  return GetRegisterInfo()[reg].name;
}

bool RegisterContextPOSIX_ppc64le::IsRegisterSetAvailable(size_t set_index) {
  size_t num_sets = k_num_register_sets;

  return (set_index < num_sets);
}
