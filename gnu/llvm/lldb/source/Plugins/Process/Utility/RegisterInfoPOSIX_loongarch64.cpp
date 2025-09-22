//===-- RegisterInfoPOSIX_loongarch64.cpp --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===---------------------------------------------------------------------===//

#include <cassert>
#include <lldb/Utility/Flags.h>
#include <stddef.h>

#include "lldb/lldb-defines.h"
#include "llvm/Support/Compiler.h"

#include "RegisterInfoPOSIX_loongarch64.h"

#define GPR_OFFSET(idx) ((idx)*8 + 0)
#define FPR_OFFSET(idx) ((idx)*8 + sizeof(RegisterInfoPOSIX_loongarch64::GPR))
#define FCC_OFFSET(idx) ((idx)*1 + 32 * 8 + sizeof(RegisterInfoPOSIX_loongarch64::GPR))
#define FCSR_OFFSET (8 * 1 + 32 * 8 + sizeof(RegisterInfoPOSIX_loongarch64::GPR))

#define REG_CONTEXT_SIZE                                                       \
  (sizeof(RegisterInfoPOSIX_loongarch64::GPR) +                                \
   sizeof(RegisterInfoPOSIX_loongarch64::FPR))

#define DECLARE_REGISTER_INFOS_LOONGARCH64_STRUCT
#include "RegisterInfos_loongarch64.h"
#undef DECLARE_REGISTER_INFOS_LOONGARCH64_STRUCT

const lldb_private::RegisterInfo *
RegisterInfoPOSIX_loongarch64::GetRegisterInfoPtr(
    const lldb_private::ArchSpec &target_arch) {
  switch (target_arch.GetMachine()) {
  case llvm::Triple::loongarch64:
    return g_register_infos_loongarch64;
  default:
    assert(false && "Unhandled target architecture.");
    return nullptr;
  }
}

uint32_t RegisterInfoPOSIX_loongarch64::GetRegisterInfoCount(
    const lldb_private::ArchSpec &target_arch) {
  switch (target_arch.GetMachine()) {
  case llvm::Triple::loongarch64:
    return static_cast<uint32_t>(sizeof(g_register_infos_loongarch64) /
                                 sizeof(g_register_infos_loongarch64[0]));
  default:
    assert(false && "Unhandled target architecture.");
    return 0;
  }
}

// Number of register sets provided by this context.
enum {
  k_num_gpr_registers = gpr_last_loongarch - gpr_first_loongarch + 1,
  k_num_fpr_registers = fpr_last_loongarch - fpr_first_loongarch + 1,
  k_num_register_sets = 2
};

// LoongArch64 general purpose registers.
static const uint32_t g_gpr_regnums_loongarch64[] = {
    gpr_r0_loongarch,        gpr_r1_loongarch,        gpr_r2_loongarch,
    gpr_r3_loongarch,        gpr_r4_loongarch,        gpr_r5_loongarch,
    gpr_r6_loongarch,        gpr_r7_loongarch,        gpr_r8_loongarch,
    gpr_r9_loongarch,        gpr_r10_loongarch,       gpr_r11_loongarch,
    gpr_r12_loongarch,       gpr_r13_loongarch,       gpr_r14_loongarch,
    gpr_r15_loongarch,       gpr_r16_loongarch,       gpr_r17_loongarch,
    gpr_r18_loongarch,       gpr_r19_loongarch,       gpr_r20_loongarch,
    gpr_r21_loongarch,       gpr_r22_loongarch,       gpr_r23_loongarch,
    gpr_r24_loongarch,       gpr_r25_loongarch,       gpr_r26_loongarch,
    gpr_r27_loongarch,       gpr_r28_loongarch,       gpr_r29_loongarch,
    gpr_r30_loongarch,       gpr_r31_loongarch,       gpr_orig_a0_loongarch,
    gpr_pc_loongarch,        gpr_badv_loongarch,      gpr_reserved0_loongarch,
    gpr_reserved1_loongarch, gpr_reserved2_loongarch, gpr_reserved3_loongarch,
    gpr_reserved4_loongarch, gpr_reserved5_loongarch, gpr_reserved6_loongarch,
    gpr_reserved7_loongarch, gpr_reserved8_loongarch, gpr_reserved9_loongarch,
    LLDB_INVALID_REGNUM};

static_assert(((sizeof g_gpr_regnums_loongarch64 /
                sizeof g_gpr_regnums_loongarch64[0]) -
               1) == k_num_gpr_registers,
              "g_gpr_regnums_loongarch64 has wrong number of register infos");

// LoongArch64 floating point registers.
static const uint32_t g_fpr_regnums_loongarch64[] = {
    fpr_f0_loongarch,   fpr_f1_loongarch,   fpr_f2_loongarch,
    fpr_f3_loongarch,   fpr_f4_loongarch,   fpr_f5_loongarch,
    fpr_f6_loongarch,   fpr_f7_loongarch,   fpr_f8_loongarch,
    fpr_f9_loongarch,   fpr_f10_loongarch,  fpr_f11_loongarch,
    fpr_f12_loongarch,  fpr_f13_loongarch,  fpr_f14_loongarch,
    fpr_f15_loongarch,  fpr_f16_loongarch,  fpr_f17_loongarch,
    fpr_f18_loongarch,  fpr_f19_loongarch,  fpr_f20_loongarch,
    fpr_f21_loongarch,  fpr_f22_loongarch,  fpr_f23_loongarch,
    fpr_f24_loongarch,  fpr_f25_loongarch,  fpr_f26_loongarch,
    fpr_f27_loongarch,  fpr_f28_loongarch,  fpr_f29_loongarch,
    fpr_f30_loongarch,  fpr_f31_loongarch,  fpr_fcc0_loongarch,
    fpr_fcc1_loongarch, fpr_fcc2_loongarch, fpr_fcc3_loongarch,
    fpr_fcc4_loongarch, fpr_fcc5_loongarch, fpr_fcc6_loongarch,
    fpr_fcc7_loongarch, fpr_fcsr_loongarch, LLDB_INVALID_REGNUM};

static_assert(((sizeof g_fpr_regnums_loongarch64 /
                sizeof g_fpr_regnums_loongarch64[0]) -
               1) == k_num_fpr_registers,
              "g_fpr_regnums_loongarch64 has wrong number of register infos");

// Register sets for LoongArch64.
static const lldb_private::RegisterSet
    g_reg_sets_loongarch64[k_num_register_sets] = {
        {"General Purpose Registers", "gpr", k_num_gpr_registers,
         g_gpr_regnums_loongarch64},
        {"Floating Point Registers", "fpr", k_num_fpr_registers,
         g_fpr_regnums_loongarch64}};

RegisterInfoPOSIX_loongarch64::RegisterInfoPOSIX_loongarch64(
    const lldb_private::ArchSpec &target_arch, lldb_private::Flags flags)
    : lldb_private::RegisterInfoAndSetInterface(target_arch),
      m_register_info_p(GetRegisterInfoPtr(target_arch)),
      m_register_info_count(GetRegisterInfoCount(target_arch)) {}

uint32_t RegisterInfoPOSIX_loongarch64::GetRegisterCount() const {
  return m_register_info_count;
}

size_t RegisterInfoPOSIX_loongarch64::GetGPRSize() const {
  return sizeof(struct RegisterInfoPOSIX_loongarch64::GPR);
}

size_t RegisterInfoPOSIX_loongarch64::GetFPRSize() const {
  return sizeof(struct RegisterInfoPOSIX_loongarch64::FPR);
}

const lldb_private::RegisterInfo *
RegisterInfoPOSIX_loongarch64::GetRegisterInfo() const {
  return m_register_info_p;
}

size_t RegisterInfoPOSIX_loongarch64::GetRegisterSetCount() const {
  return k_num_register_sets;
}

size_t RegisterInfoPOSIX_loongarch64::GetRegisterSetFromRegisterIndex(
    uint32_t reg_index) const {
  // coverity[unsigned_compare]
  if (reg_index >= gpr_first_loongarch && reg_index <= gpr_last_loongarch)
    return GPRegSet;
  if (reg_index >= fpr_first_loongarch && reg_index <= fpr_last_loongarch)
    return FPRegSet;
  return LLDB_INVALID_REGNUM;
}

const lldb_private::RegisterSet *
RegisterInfoPOSIX_loongarch64::GetRegisterSet(size_t set_index) const {
  if (set_index < GetRegisterSetCount())
    return &g_reg_sets_loongarch64[set_index];
  return nullptr;
}
