//===-- RegisterContextFreeBSD_mips64.cpp ---------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===---------------------------------------------------------------------===//

#include "RegisterContextFreeBSD_mips64.h"
#include "RegisterContextPOSIX_mips64.h"
#include "lldb-mips-freebsd-register-enums.h"
#include <vector>

using namespace lldb_private;
using namespace lldb;

static const uint32_t g_gp_regnums_mips64[] = {
    gpr_zero_mips64,    gpr_r1_mips64,    gpr_r2_mips64,    gpr_r3_mips64,
    gpr_r4_mips64,      gpr_r5_mips64,    gpr_r6_mips64,    gpr_r7_mips64,
    gpr_r8_mips64,      gpr_r9_mips64,    gpr_r10_mips64,   gpr_r11_mips64,
    gpr_r12_mips64,     gpr_r13_mips64,   gpr_r14_mips64,   gpr_r15_mips64,
    gpr_r16_mips64,     gpr_r17_mips64,   gpr_r18_mips64,   gpr_r19_mips64,
    gpr_r20_mips64,     gpr_r21_mips64,   gpr_r22_mips64,   gpr_r23_mips64,
    gpr_r24_mips64,     gpr_r25_mips64,   gpr_r26_mips64,   gpr_r27_mips64,
    gpr_gp_mips64,      gpr_sp_mips64,    gpr_r30_mips64,   gpr_ra_mips64,
    gpr_sr_mips64,      gpr_mullo_mips64, gpr_mulhi_mips64, gpr_badvaddr_mips64,
    gpr_cause_mips64,   gpr_pc_mips64,    gpr_ic_mips64,    gpr_dummy_mips64,
    LLDB_INVALID_REGNUM // register sets need to end with this flag
};

static_assert((sizeof(g_gp_regnums_mips64) / sizeof(g_gp_regnums_mips64[0])) -
                      1 ==
                  k_num_gpr_registers_mips64,
              "g_gp_regnums_mips64 has wrong number of register infos");

const uint32_t g_fp_regnums_mips64[] = {
    fpr_f0_mips64,      fpr_f1_mips64,  fpr_f2_mips64,  fpr_f3_mips64,
    fpr_f4_mips64,      fpr_f5_mips64,  fpr_f6_mips64,  fpr_f7_mips64,
    fpr_f8_mips64,      fpr_f9_mips64,  fpr_f10_mips64, fpr_f11_mips64,
    fpr_f12_mips64,     fpr_f13_mips64, fpr_f14_mips64, fpr_f15_mips64,
    fpr_f16_mips64,     fpr_f17_mips64, fpr_f18_mips64, fpr_f19_mips64,
    fpr_f20_mips64,     fpr_f21_mips64, fpr_f22_mips64, fpr_f23_mips64,
    fpr_f24_mips64,     fpr_f25_mips64, fpr_f26_mips64, fpr_f27_mips64,
    fpr_f28_mips64,     fpr_f29_mips64, fpr_f30_mips64, fpr_f31_mips64,
    fpr_fcsr_mips64,    fpr_fir_mips64,
    LLDB_INVALID_REGNUM // register sets need to end with this flag
};

static_assert((sizeof(g_fp_regnums_mips64) / sizeof(g_fp_regnums_mips64[0])) -
                      1 ==
                  k_num_fpr_registers_mips64,
              "g_fp_regnums_mips64 has wrong number of register infos");

// Number of register sets provided by this context.
constexpr size_t k_num_register_sets = 2;

static const RegisterSet g_reg_sets_mips64[k_num_register_sets] = {
    {"General Purpose Registers", "gpr", k_num_gpr_registers_mips64,
     g_gp_regnums_mips64},
    {"Floating Point Registers", "fpu", k_num_fpr_registers_mips64,
     g_fp_regnums_mips64},
};

// http://svnweb.freebsd.org/base/head/sys/mips/include/regnum.h
typedef struct _GPR {
  uint64_t zero;
  uint64_t r1;
  uint64_t r2;
  uint64_t r3;
  uint64_t r4;
  uint64_t r5;
  uint64_t r6;
  uint64_t r7;
  uint64_t r8;
  uint64_t r9;
  uint64_t r10;
  uint64_t r11;
  uint64_t r12;
  uint64_t r13;
  uint64_t r14;
  uint64_t r15;
  uint64_t r16;
  uint64_t r17;
  uint64_t r18;
  uint64_t r19;
  uint64_t r20;
  uint64_t r21;
  uint64_t r22;
  uint64_t r23;
  uint64_t r24;
  uint64_t r25;
  uint64_t r26;
  uint64_t r27;
  uint64_t gp;
  uint64_t sp;
  uint64_t r30;
  uint64_t ra;
  uint64_t sr;
  uint64_t mullo;
  uint64_t mulhi;
  uint64_t badvaddr;
  uint64_t cause;
  uint64_t pc;
  uint64_t ic;
  uint64_t dummy;
} GPR_freebsd_mips;

typedef struct _FPR {
  uint64_t f0;
  uint64_t f1;
  uint64_t f2;
  uint64_t f3;
  uint64_t f4;
  uint64_t f5;
  uint64_t f6;
  uint64_t f7;
  uint64_t f8;
  uint64_t f9;
  uint64_t f10;
  uint64_t f11;
  uint64_t f12;
  uint64_t f13;
  uint64_t f14;
  uint64_t f15;
  uint64_t f16;
  uint64_t f17;
  uint64_t f18;
  uint64_t f19;
  uint64_t f20;
  uint64_t f21;
  uint64_t f22;
  uint64_t f23;
  uint64_t f24;
  uint64_t f25;
  uint64_t f26;
  uint64_t f27;
  uint64_t f28;
  uint64_t f29;
  uint64_t f30;
  uint64_t f31;
  uint64_t fcsr;
  uint64_t fir;
} FPR_freebsd_mips;

// Include RegisterInfos_mips64 to declare our g_register_infos_mips64
// structure.
#define DECLARE_REGISTER_INFOS_MIPS64_STRUCT
#include "RegisterInfos_mips64.h"
#undef DECLARE_REGISTER_INFOS_MIPS64_STRUCT

RegisterContextFreeBSD_mips64::RegisterContextFreeBSD_mips64(
    const ArchSpec &target_arch)
    : RegisterInfoInterface(target_arch) {}

size_t RegisterContextFreeBSD_mips64::GetGPRSize() const {
  return sizeof(GPR_freebsd_mips);
}

const RegisterSet *
RegisterContextFreeBSD_mips64::GetRegisterSet(size_t set) const {
  // Check if RegisterSet is available
  if (set < k_num_register_sets)
    return &g_reg_sets_mips64[set];
  return nullptr;
}

size_t RegisterContextFreeBSD_mips64::GetRegisterSetCount() const {
  return k_num_register_sets;
}

const RegisterInfo *RegisterContextFreeBSD_mips64::GetRegisterInfo() const {
  assert(GetTargetArchitecture().GetCore() == ArchSpec::eCore_mips64);
  return g_register_infos_mips64;
}

uint32_t RegisterContextFreeBSD_mips64::GetRegisterCount() const {
  return static_cast<uint32_t>(sizeof(g_register_infos_mips64) /
                               sizeof(g_register_infos_mips64[0]));
}
