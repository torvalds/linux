//===-- lldb-mips-freebsd-register-enums.h ----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_LLDB_MIPS_FREEBSD_REGISTER_ENUMS_H
#define LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_LLDB_MIPS_FREEBSD_REGISTER_ENUMS_H

namespace lldb_private {
// LLDB register codes (e.g. RegisterKind == eRegisterKindLLDB)

// Internal codes for all mips registers.
enum {
  k_first_gpr_mips64,
  gpr_zero_mips64 = k_first_gpr_mips64,
  gpr_r1_mips64,
  gpr_r2_mips64,
  gpr_r3_mips64,
  gpr_r4_mips64,
  gpr_r5_mips64,
  gpr_r6_mips64,
  gpr_r7_mips64,
  gpr_r8_mips64,
  gpr_r9_mips64,
  gpr_r10_mips64,
  gpr_r11_mips64,
  gpr_r12_mips64,
  gpr_r13_mips64,
  gpr_r14_mips64,
  gpr_r15_mips64,
  gpr_r16_mips64,
  gpr_r17_mips64,
  gpr_r18_mips64,
  gpr_r19_mips64,
  gpr_r20_mips64,
  gpr_r21_mips64,
  gpr_r22_mips64,
  gpr_r23_mips64,
  gpr_r24_mips64,
  gpr_r25_mips64,
  gpr_r26_mips64,
  gpr_r27_mips64,
  gpr_gp_mips64,
  gpr_sp_mips64,
  gpr_r30_mips64,
  gpr_ra_mips64,
  gpr_sr_mips64,
  gpr_mullo_mips64,
  gpr_mulhi_mips64,
  gpr_badvaddr_mips64,
  gpr_cause_mips64,
  gpr_pc_mips64,
  gpr_ic_mips64,
  gpr_dummy_mips64,
  k_last_gpr_mips64 = gpr_dummy_mips64,

  k_first_fpr_mips64,
  fpr_f0_mips64 = k_first_fpr_mips64,
  fpr_f1_mips64,
  fpr_f2_mips64,
  fpr_f3_mips64,
  fpr_f4_mips64,
  fpr_f5_mips64,
  fpr_f6_mips64,
  fpr_f7_mips64,
  fpr_f8_mips64,
  fpr_f9_mips64,
  fpr_f10_mips64,
  fpr_f11_mips64,
  fpr_f12_mips64,
  fpr_f13_mips64,
  fpr_f14_mips64,
  fpr_f15_mips64,
  fpr_f16_mips64,
  fpr_f17_mips64,
  fpr_f18_mips64,
  fpr_f19_mips64,
  fpr_f20_mips64,
  fpr_f21_mips64,
  fpr_f22_mips64,
  fpr_f23_mips64,
  fpr_f24_mips64,
  fpr_f25_mips64,
  fpr_f26_mips64,
  fpr_f27_mips64,
  fpr_f28_mips64,
  fpr_f29_mips64,
  fpr_f30_mips64,
  fpr_f31_mips64,
  fpr_fcsr_mips64,
  fpr_fir_mips64,
  k_last_fpr_mips64 = fpr_fir_mips64,

  k_num_registers_mips64,

  k_num_gpr_registers_mips64 = k_last_gpr_mips64 - k_first_gpr_mips64 + 1,
  k_num_fpr_registers_mips64 = k_last_fpr_mips64 - k_first_fpr_mips64 + 1,
};
} // namespace lldb_private
#endif // LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_LLDB_MIPS_FREEBSD_REGISTER_ENUMS_H
