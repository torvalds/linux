//===-- RegisterContext_mips.h --------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_REGISTERCONTEXT_MIPS_H
#define LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_REGISTERCONTEXT_MIPS_H

#include <cstddef>
#include <cstdint>

// eh_frame and DWARF Register numbers (eRegisterKindEHFrame &
// eRegisterKindDWARF)

enum {
  // GP Registers
  dwarf_zero_mips = 0,
  dwarf_r1_mips,
  dwarf_r2_mips,
  dwarf_r3_mips,
  dwarf_r4_mips,
  dwarf_r5_mips,
  dwarf_r6_mips,
  dwarf_r7_mips,
  dwarf_r8_mips,
  dwarf_r9_mips,
  dwarf_r10_mips,
  dwarf_r11_mips,
  dwarf_r12_mips,
  dwarf_r13_mips,
  dwarf_r14_mips,
  dwarf_r15_mips,
  dwarf_r16_mips,
  dwarf_r17_mips,
  dwarf_r18_mips,
  dwarf_r19_mips,
  dwarf_r20_mips,
  dwarf_r21_mips,
  dwarf_r22_mips,
  dwarf_r23_mips,
  dwarf_r24_mips,
  dwarf_r25_mips,
  dwarf_r26_mips,
  dwarf_r27_mips,
  dwarf_gp_mips,
  dwarf_sp_mips,
  dwarf_r30_mips,
  dwarf_ra_mips,
  dwarf_sr_mips,
  dwarf_lo_mips,
  dwarf_hi_mips,
  dwarf_bad_mips,
  dwarf_cause_mips,
  dwarf_pc_mips,
  dwarf_f0_mips,
  dwarf_f1_mips,
  dwarf_f2_mips,
  dwarf_f3_mips,
  dwarf_f4_mips,
  dwarf_f5_mips,
  dwarf_f6_mips,
  dwarf_f7_mips,
  dwarf_f8_mips,
  dwarf_f9_mips,
  dwarf_f10_mips,
  dwarf_f11_mips,
  dwarf_f12_mips,
  dwarf_f13_mips,
  dwarf_f14_mips,
  dwarf_f15_mips,
  dwarf_f16_mips,
  dwarf_f17_mips,
  dwarf_f18_mips,
  dwarf_f19_mips,
  dwarf_f20_mips,
  dwarf_f21_mips,
  dwarf_f22_mips,
  dwarf_f23_mips,
  dwarf_f24_mips,
  dwarf_f25_mips,
  dwarf_f26_mips,
  dwarf_f27_mips,
  dwarf_f28_mips,
  dwarf_f29_mips,
  dwarf_f30_mips,
  dwarf_f31_mips,
  dwarf_fcsr_mips,
  dwarf_fir_mips,
  dwarf_w0_mips,
  dwarf_w1_mips,
  dwarf_w2_mips,
  dwarf_w3_mips,
  dwarf_w4_mips,
  dwarf_w5_mips,
  dwarf_w6_mips,
  dwarf_w7_mips,
  dwarf_w8_mips,
  dwarf_w9_mips,
  dwarf_w10_mips,
  dwarf_w11_mips,
  dwarf_w12_mips,
  dwarf_w13_mips,
  dwarf_w14_mips,
  dwarf_w15_mips,
  dwarf_w16_mips,
  dwarf_w17_mips,
  dwarf_w18_mips,
  dwarf_w19_mips,
  dwarf_w20_mips,
  dwarf_w21_mips,
  dwarf_w22_mips,
  dwarf_w23_mips,
  dwarf_w24_mips,
  dwarf_w25_mips,
  dwarf_w26_mips,
  dwarf_w27_mips,
  dwarf_w28_mips,
  dwarf_w29_mips,
  dwarf_w30_mips,
  dwarf_w31_mips,
  dwarf_mcsr_mips,
  dwarf_mir_mips,
  dwarf_config5_mips,
  dwarf_ic_mips,
  dwarf_dummy_mips
};

enum {
  dwarf_zero_mips64 = 0,
  dwarf_r1_mips64,
  dwarf_r2_mips64,
  dwarf_r3_mips64,
  dwarf_r4_mips64,
  dwarf_r5_mips64,
  dwarf_r6_mips64,
  dwarf_r7_mips64,
  dwarf_r8_mips64,
  dwarf_r9_mips64,
  dwarf_r10_mips64,
  dwarf_r11_mips64,
  dwarf_r12_mips64,
  dwarf_r13_mips64,
  dwarf_r14_mips64,
  dwarf_r15_mips64,
  dwarf_r16_mips64,
  dwarf_r17_mips64,
  dwarf_r18_mips64,
  dwarf_r19_mips64,
  dwarf_r20_mips64,
  dwarf_r21_mips64,
  dwarf_r22_mips64,
  dwarf_r23_mips64,
  dwarf_r24_mips64,
  dwarf_r25_mips64,
  dwarf_r26_mips64,
  dwarf_r27_mips64,
  dwarf_gp_mips64,
  dwarf_sp_mips64,
  dwarf_r30_mips64,
  dwarf_ra_mips64,
  dwarf_sr_mips64,
  dwarf_lo_mips64,
  dwarf_hi_mips64,
  dwarf_bad_mips64,
  dwarf_cause_mips64,
  dwarf_pc_mips64,
  dwarf_f0_mips64,
  dwarf_f1_mips64,
  dwarf_f2_mips64,
  dwarf_f3_mips64,
  dwarf_f4_mips64,
  dwarf_f5_mips64,
  dwarf_f6_mips64,
  dwarf_f7_mips64,
  dwarf_f8_mips64,
  dwarf_f9_mips64,
  dwarf_f10_mips64,
  dwarf_f11_mips64,
  dwarf_f12_mips64,
  dwarf_f13_mips64,
  dwarf_f14_mips64,
  dwarf_f15_mips64,
  dwarf_f16_mips64,
  dwarf_f17_mips64,
  dwarf_f18_mips64,
  dwarf_f19_mips64,
  dwarf_f20_mips64,
  dwarf_f21_mips64,
  dwarf_f22_mips64,
  dwarf_f23_mips64,
  dwarf_f24_mips64,
  dwarf_f25_mips64,
  dwarf_f26_mips64,
  dwarf_f27_mips64,
  dwarf_f28_mips64,
  dwarf_f29_mips64,
  dwarf_f30_mips64,
  dwarf_f31_mips64,
  dwarf_fcsr_mips64,
  dwarf_fir_mips64,
  dwarf_ic_mips64,
  dwarf_dummy_mips64,
  dwarf_w0_mips64,
  dwarf_w1_mips64,
  dwarf_w2_mips64,
  dwarf_w3_mips64,
  dwarf_w4_mips64,
  dwarf_w5_mips64,
  dwarf_w6_mips64,
  dwarf_w7_mips64,
  dwarf_w8_mips64,
  dwarf_w9_mips64,
  dwarf_w10_mips64,
  dwarf_w11_mips64,
  dwarf_w12_mips64,
  dwarf_w13_mips64,
  dwarf_w14_mips64,
  dwarf_w15_mips64,
  dwarf_w16_mips64,
  dwarf_w17_mips64,
  dwarf_w18_mips64,
  dwarf_w19_mips64,
  dwarf_w20_mips64,
  dwarf_w21_mips64,
  dwarf_w22_mips64,
  dwarf_w23_mips64,
  dwarf_w24_mips64,
  dwarf_w25_mips64,
  dwarf_w26_mips64,
  dwarf_w27_mips64,
  dwarf_w28_mips64,
  dwarf_w29_mips64,
  dwarf_w30_mips64,
  dwarf_w31_mips64,
  dwarf_mcsr_mips64,
  dwarf_mir_mips64,
  dwarf_config5_mips64,
};

// GP registers
struct GPR_linux_mips {
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
  uint64_t mullo;
  uint64_t mulhi;
  uint64_t pc;
  uint64_t badvaddr;
  uint64_t sr;
  uint64_t cause;
  uint64_t config5;
};

struct FPR_linux_mips {
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
  uint32_t fcsr;
  uint32_t fir;
  uint32_t config5;
};

struct MSAReg {
  uint8_t byte[16];
};

struct MSA_linux_mips {
  MSAReg w0;
  MSAReg w1;
  MSAReg w2;
  MSAReg w3;
  MSAReg w4;
  MSAReg w5;
  MSAReg w6;
  MSAReg w7;
  MSAReg w8;
  MSAReg w9;
  MSAReg w10;
  MSAReg w11;
  MSAReg w12;
  MSAReg w13;
  MSAReg w14;
  MSAReg w15;
  MSAReg w16;
  MSAReg w17;
  MSAReg w18;
  MSAReg w19;
  MSAReg w20;
  MSAReg w21;
  MSAReg w22;
  MSAReg w23;
  MSAReg w24;
  MSAReg w25;
  MSAReg w26;
  MSAReg w27;
  MSAReg w28;
  MSAReg w29;
  MSAReg w30;
  MSAReg w31;
  uint32_t fcsr;    /* FPU control status register */
  uint32_t fir;     /* FPU implementaion revision */
  uint32_t mcsr;    /* MSA control status register */
  uint32_t mir;     /* MSA implementation revision */
  uint32_t config5; /* Config5 register */
};

struct UserArea {
  GPR_linux_mips gpr; // General purpose registers.
  FPR_linux_mips fpr; // Floating point registers.
  MSA_linux_mips msa; // MSA registers.
};

#endif // LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_REGISTERCONTEXT_MIPS_H
