//===-- RegisterInfoPOSIX_arm.cpp -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===---------------------------------------------------------------------===//

#include <cassert>
#include <cstddef>
#include <vector>

#include "lldb/lldb-defines.h"
#include "llvm/Support/Compiler.h"

#include "RegisterInfoPOSIX_arm.h"

using namespace lldb;
using namespace lldb_private;

// Based on RegisterContextDarwin_arm.cpp
#define GPR_OFFSET(idx) ((idx)*4)
#define FPU_OFFSET(idx) ((idx)*4 + sizeof(RegisterInfoPOSIX_arm::GPR))
#define FPSCR_OFFSET                                                           \
  (LLVM_EXTENSION offsetof(RegisterInfoPOSIX_arm::FPU, fpscr) +                \
   sizeof(RegisterInfoPOSIX_arm::GPR))
#define EXC_OFFSET(idx)                                                        \
  ((idx)*4 + sizeof(RegisterInfoPOSIX_arm::GPR) +                              \
   sizeof(RegisterInfoPOSIX_arm::FPU))
#define DBG_OFFSET(reg)                                                        \
  ((LLVM_EXTENSION offsetof(RegisterInfoPOSIX_arm::DBG, reg) +                 \
    sizeof(RegisterInfoPOSIX_arm::GPR) + sizeof(RegisterInfoPOSIX_arm::FPU) +  \
    sizeof(RegisterInfoPOSIX_arm::EXC)))

#define DEFINE_DBG(reg, i)                                                     \
  #reg, NULL, sizeof(((RegisterInfoPOSIX_arm::DBG *) NULL)->reg[i]),           \
                      DBG_OFFSET(reg[i]), eEncodingUint, eFormatHex,           \
                                 {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,    \
                                  LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,    \
                                  dbg_##reg##i },                              \
                                  NULL, NULL, NULL,
#define REG_CONTEXT_SIZE                                                       \
  (sizeof(RegisterInfoPOSIX_arm::GPR) + sizeof(RegisterInfoPOSIX_arm::FPU) +   \
   sizeof(RegisterInfoPOSIX_arm::EXC))

// Include RegisterInfos_arm to declare our g_register_infos_arm structure.
#define DECLARE_REGISTER_INFOS_ARM_STRUCT
#include "RegisterInfos_arm.h"
#undef DECLARE_REGISTER_INFOS_ARM_STRUCT

static const lldb_private::RegisterInfo *
GetRegisterInfoPtr(const lldb_private::ArchSpec &target_arch) {
  switch (target_arch.GetMachine()) {
  case llvm::Triple::arm:
    return g_register_infos_arm;
  default:
    assert(false && "Unhandled target architecture.");
    return nullptr;
  }
}

static uint32_t
GetRegisterInfoCount(const lldb_private::ArchSpec &target_arch) {
  switch (target_arch.GetMachine()) {
  case llvm::Triple::arm:
    return static_cast<uint32_t>(sizeof(g_register_infos_arm) /
                                 sizeof(g_register_infos_arm[0]));
  default:
    assert(false && "Unhandled target architecture.");
    return 0;
  }
}

// Number of register sets provided by this context.
enum {
  k_num_gpr_registers = gpr_cpsr - gpr_r0 + 1,
  k_num_fpr_registers = fpu_q15 - fpu_s0 + 1,
  k_num_register_sets = 2
};

// arm general purpose registers.
static const uint32_t g_gpr_regnums_arm[] = {
    gpr_r0,   gpr_r1,
    gpr_r2,   gpr_r3,
    gpr_r4,   gpr_r5,
    gpr_r6,   gpr_r7,
    gpr_r8,   gpr_r9,
    gpr_r10,  gpr_r11,
    gpr_r12,  gpr_sp,
    gpr_lr,   gpr_pc,
    gpr_cpsr, LLDB_INVALID_REGNUM // register sets need to end with this flag
};
static_assert(((sizeof g_gpr_regnums_arm / sizeof g_gpr_regnums_arm[0]) - 1) ==
                  k_num_gpr_registers,
              "g_gpr_regnums_arm has wrong number of register infos");

// arm floating point registers.
static const uint32_t g_fpu_regnums_arm[] = {
    fpu_s0,    fpu_s1,
    fpu_s2,    fpu_s3,
    fpu_s4,    fpu_s5,
    fpu_s6,    fpu_s7,
    fpu_s8,    fpu_s9,
    fpu_s10,   fpu_s11,
    fpu_s12,   fpu_s13,
    fpu_s14,   fpu_s15,
    fpu_s16,   fpu_s17,
    fpu_s18,   fpu_s19,
    fpu_s20,   fpu_s21,
    fpu_s22,   fpu_s23,
    fpu_s24,   fpu_s25,
    fpu_s26,   fpu_s27,
    fpu_s28,   fpu_s29,
    fpu_s30,   fpu_s31,
    fpu_fpscr, fpu_d0,
    fpu_d1,    fpu_d2,
    fpu_d3,    fpu_d4,
    fpu_d5,    fpu_d6,
    fpu_d7,    fpu_d8,
    fpu_d9,    fpu_d10,
    fpu_d11,   fpu_d12,
    fpu_d13,   fpu_d14,
    fpu_d15,   fpu_d16,
    fpu_d17,   fpu_d18,
    fpu_d19,   fpu_d20,
    fpu_d21,   fpu_d22,
    fpu_d23,   fpu_d24,
    fpu_d25,   fpu_d26,
    fpu_d27,   fpu_d28,
    fpu_d29,   fpu_d30,
    fpu_d31,   fpu_q0,
    fpu_q1,    fpu_q2,
    fpu_q3,    fpu_q4,
    fpu_q5,    fpu_q6,
    fpu_q7,    fpu_q8,
    fpu_q9,    fpu_q10,
    fpu_q11,   fpu_q12,
    fpu_q13,   fpu_q14,
    fpu_q15,   LLDB_INVALID_REGNUM // register sets need to end with this flag
};
static_assert(((sizeof g_fpu_regnums_arm / sizeof g_fpu_regnums_arm[0]) - 1) ==
                  k_num_fpr_registers,
              "g_fpu_regnums_arm has wrong number of register infos");

// Register sets for arm.
static const RegisterSet g_reg_sets_arm[k_num_register_sets] = {
    {"General Purpose Registers", "gpr", k_num_gpr_registers,
     g_gpr_regnums_arm},
    {"Floating Point Registers", "fpu", k_num_fpr_registers,
     g_fpu_regnums_arm}};

RegisterInfoPOSIX_arm::RegisterInfoPOSIX_arm(
    const lldb_private::ArchSpec &target_arch)
    : lldb_private::RegisterInfoAndSetInterface(target_arch),
      m_register_info_p(GetRegisterInfoPtr(target_arch)),
      m_register_info_count(GetRegisterInfoCount(target_arch)) {}

size_t RegisterInfoPOSIX_arm::GetGPRSize() const {
  return sizeof(struct RegisterInfoPOSIX_arm::GPR);
}

size_t RegisterInfoPOSIX_arm::GetFPRSize() const {
  return sizeof(struct RegisterInfoPOSIX_arm::FPU);
}

const lldb_private::RegisterInfo *
RegisterInfoPOSIX_arm::GetRegisterInfo() const {
  return m_register_info_p;
}

size_t RegisterInfoPOSIX_arm::GetRegisterSetCount() const {
  return k_num_register_sets;
}

size_t RegisterInfoPOSIX_arm::GetRegisterSetFromRegisterIndex(
    uint32_t reg_index) const {
  if (reg_index <= gpr_cpsr)
    return GPRegSet;
  if (reg_index <= fpu_q15)
    return FPRegSet;
  return LLDB_INVALID_REGNUM;
}

const lldb_private::RegisterSet *
RegisterInfoPOSIX_arm::GetRegisterSet(size_t set_index) const {
  if (set_index < GetRegisterSetCount())
    return &g_reg_sets_arm[set_index];
  return nullptr;
}

uint32_t RegisterInfoPOSIX_arm::GetRegisterCount() const {
  return m_register_info_count;
}
