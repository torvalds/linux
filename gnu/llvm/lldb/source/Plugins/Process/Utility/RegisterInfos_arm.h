//===-- RegisterInfos_arm.h -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifdef DECLARE_REGISTER_INFOS_ARM_STRUCT

#include <cstddef>

#include "lldb/lldb-defines.h"
#include "lldb/lldb-enumerations.h"
#include "lldb/lldb-private.h"

#include "Utility/ARM_DWARF_Registers.h"
#include "Utility/ARM_ehframe_Registers.h"

using namespace lldb;
using namespace lldb_private;

#ifndef GPR_OFFSET
#error GPR_OFFSET must be defined before including this header file
#endif

#ifndef FPU_OFFSET
#error FPU_OFFSET must be defined before including this header file
#endif

#ifndef FPSCR_OFFSET
#error FPSCR_OFFSET must be defined before including this header file
#endif

#ifndef EXC_OFFSET
#error EXC_OFFSET_NAME must be defined before including this header file
#endif

#ifndef DEFINE_DBG
#error DEFINE_DBG must be defined before including this header file
#endif

enum {
  gpr_r0 = 0,
  gpr_r1,
  gpr_r2,
  gpr_r3,
  gpr_r4,
  gpr_r5,
  gpr_r6,
  gpr_r7,
  gpr_r8,
  gpr_r9,
  gpr_r10,
  gpr_r11,
  gpr_r12,
  gpr_r13,
  gpr_sp = gpr_r13,
  gpr_r14,
  gpr_lr = gpr_r14,
  gpr_r15,
  gpr_pc = gpr_r15,
  gpr_cpsr,

  fpu_s0,
  fpu_s1,
  fpu_s2,
  fpu_s3,
  fpu_s4,
  fpu_s5,
  fpu_s6,
  fpu_s7,
  fpu_s8,
  fpu_s9,
  fpu_s10,
  fpu_s11,
  fpu_s12,
  fpu_s13,
  fpu_s14,
  fpu_s15,
  fpu_s16,
  fpu_s17,
  fpu_s18,
  fpu_s19,
  fpu_s20,
  fpu_s21,
  fpu_s22,
  fpu_s23,
  fpu_s24,
  fpu_s25,
  fpu_s26,
  fpu_s27,
  fpu_s28,
  fpu_s29,
  fpu_s30,
  fpu_s31,
  fpu_fpscr,

  fpu_d0,
  fpu_d1,
  fpu_d2,
  fpu_d3,
  fpu_d4,
  fpu_d5,
  fpu_d6,
  fpu_d7,
  fpu_d8,
  fpu_d9,
  fpu_d10,
  fpu_d11,
  fpu_d12,
  fpu_d13,
  fpu_d14,
  fpu_d15,
  fpu_d16,
  fpu_d17,
  fpu_d18,
  fpu_d19,
  fpu_d20,
  fpu_d21,
  fpu_d22,
  fpu_d23,
  fpu_d24,
  fpu_d25,
  fpu_d26,
  fpu_d27,
  fpu_d28,
  fpu_d29,
  fpu_d30,
  fpu_d31,

  fpu_q0,
  fpu_q1,
  fpu_q2,
  fpu_q3,
  fpu_q4,
  fpu_q5,
  fpu_q6,
  fpu_q7,
  fpu_q8,
  fpu_q9,
  fpu_q10,
  fpu_q11,
  fpu_q12,
  fpu_q13,
  fpu_q14,
  fpu_q15,

  exc_exception,
  exc_fsr,
  exc_far,

  dbg_bvr0,
  dbg_bvr1,
  dbg_bvr2,
  dbg_bvr3,
  dbg_bvr4,
  dbg_bvr5,
  dbg_bvr6,
  dbg_bvr7,
  dbg_bvr8,
  dbg_bvr9,
  dbg_bvr10,
  dbg_bvr11,
  dbg_bvr12,
  dbg_bvr13,
  dbg_bvr14,
  dbg_bvr15,

  dbg_bcr0,
  dbg_bcr1,
  dbg_bcr2,
  dbg_bcr3,
  dbg_bcr4,
  dbg_bcr5,
  dbg_bcr6,
  dbg_bcr7,
  dbg_bcr8,
  dbg_bcr9,
  dbg_bcr10,
  dbg_bcr11,
  dbg_bcr12,
  dbg_bcr13,
  dbg_bcr14,
  dbg_bcr15,

  dbg_wvr0,
  dbg_wvr1,
  dbg_wvr2,
  dbg_wvr3,
  dbg_wvr4,
  dbg_wvr5,
  dbg_wvr6,
  dbg_wvr7,
  dbg_wvr8,
  dbg_wvr9,
  dbg_wvr10,
  dbg_wvr11,
  dbg_wvr12,
  dbg_wvr13,
  dbg_wvr14,
  dbg_wvr15,

  dbg_wcr0,
  dbg_wcr1,
  dbg_wcr2,
  dbg_wcr3,
  dbg_wcr4,
  dbg_wcr5,
  dbg_wcr6,
  dbg_wcr7,
  dbg_wcr8,
  dbg_wcr9,
  dbg_wcr10,
  dbg_wcr11,
  dbg_wcr12,
  dbg_wcr13,
  dbg_wcr14,
  dbg_wcr15,

  k_num_registers
};

static uint32_t g_s0_invalidates[] = {fpu_d0, fpu_q0, LLDB_INVALID_REGNUM};
static uint32_t g_s1_invalidates[] = {fpu_d0, fpu_q0, LLDB_INVALID_REGNUM};
static uint32_t g_s2_invalidates[] = {fpu_d1, fpu_q0, LLDB_INVALID_REGNUM};
static uint32_t g_s3_invalidates[] = {fpu_d1, fpu_q0, LLDB_INVALID_REGNUM};
static uint32_t g_s4_invalidates[] = {fpu_d2, fpu_q1, LLDB_INVALID_REGNUM};
static uint32_t g_s5_invalidates[] = {fpu_d2, fpu_q1, LLDB_INVALID_REGNUM};
static uint32_t g_s6_invalidates[] = {fpu_d3, fpu_q1, LLDB_INVALID_REGNUM};
static uint32_t g_s7_invalidates[] = {fpu_d3, fpu_q1, LLDB_INVALID_REGNUM};
static uint32_t g_s8_invalidates[] = {fpu_d4, fpu_q2, LLDB_INVALID_REGNUM};
static uint32_t g_s9_invalidates[] = {fpu_d4, fpu_q2, LLDB_INVALID_REGNUM};
static uint32_t g_s10_invalidates[] = {fpu_d5, fpu_q2, LLDB_INVALID_REGNUM};
static uint32_t g_s11_invalidates[] = {fpu_d5, fpu_q2, LLDB_INVALID_REGNUM};
static uint32_t g_s12_invalidates[] = {fpu_d6, fpu_q3, LLDB_INVALID_REGNUM};
static uint32_t g_s13_invalidates[] = {fpu_d6, fpu_q3, LLDB_INVALID_REGNUM};
static uint32_t g_s14_invalidates[] = {fpu_d7, fpu_q3, LLDB_INVALID_REGNUM};
static uint32_t g_s15_invalidates[] = {fpu_d7, fpu_q3, LLDB_INVALID_REGNUM};
static uint32_t g_s16_invalidates[] = {fpu_d8, fpu_q4, LLDB_INVALID_REGNUM};
static uint32_t g_s17_invalidates[] = {fpu_d8, fpu_q4, LLDB_INVALID_REGNUM};
static uint32_t g_s18_invalidates[] = {fpu_d9, fpu_q4, LLDB_INVALID_REGNUM};
static uint32_t g_s19_invalidates[] = {fpu_d9, fpu_q4, LLDB_INVALID_REGNUM};
static uint32_t g_s20_invalidates[] = {fpu_d10, fpu_q5, LLDB_INVALID_REGNUM};
static uint32_t g_s21_invalidates[] = {fpu_d10, fpu_q5, LLDB_INVALID_REGNUM};
static uint32_t g_s22_invalidates[] = {fpu_d11, fpu_q5, LLDB_INVALID_REGNUM};
static uint32_t g_s23_invalidates[] = {fpu_d11, fpu_q5, LLDB_INVALID_REGNUM};
static uint32_t g_s24_invalidates[] = {fpu_d12, fpu_q6, LLDB_INVALID_REGNUM};
static uint32_t g_s25_invalidates[] = {fpu_d12, fpu_q6, LLDB_INVALID_REGNUM};
static uint32_t g_s26_invalidates[] = {fpu_d13, fpu_q6, LLDB_INVALID_REGNUM};
static uint32_t g_s27_invalidates[] = {fpu_d13, fpu_q6, LLDB_INVALID_REGNUM};
static uint32_t g_s28_invalidates[] = {fpu_d14, fpu_q7, LLDB_INVALID_REGNUM};
static uint32_t g_s29_invalidates[] = {fpu_d14, fpu_q7, LLDB_INVALID_REGNUM};
static uint32_t g_s30_invalidates[] = {fpu_d15, fpu_q7, LLDB_INVALID_REGNUM};
static uint32_t g_s31_invalidates[] = {fpu_d15, fpu_q7, LLDB_INVALID_REGNUM};

static uint32_t g_d0_invalidates[] = {fpu_q0, fpu_s0, fpu_s1,
                                      LLDB_INVALID_REGNUM};
static uint32_t g_d1_invalidates[] = {fpu_q0, fpu_s2, fpu_s3,
                                      LLDB_INVALID_REGNUM};
static uint32_t g_d2_invalidates[] = {fpu_q1, fpu_s4, fpu_s5,
                                      LLDB_INVALID_REGNUM};
static uint32_t g_d3_invalidates[] = {fpu_q1, fpu_s6, fpu_s7,
                                      LLDB_INVALID_REGNUM};
static uint32_t g_d4_invalidates[] = {fpu_q2, fpu_s8, fpu_s9,
                                      LLDB_INVALID_REGNUM};
static uint32_t g_d5_invalidates[] = {fpu_q2, fpu_s10, fpu_s11,
                                      LLDB_INVALID_REGNUM};
static uint32_t g_d6_invalidates[] = {fpu_q3, fpu_s12, fpu_s13,
                                      LLDB_INVALID_REGNUM};
static uint32_t g_d7_invalidates[] = {fpu_q3, fpu_s14, fpu_s15,
                                      LLDB_INVALID_REGNUM};
static uint32_t g_d8_invalidates[] = {fpu_q4, fpu_s16, fpu_s17,
                                      LLDB_INVALID_REGNUM};
static uint32_t g_d9_invalidates[] = {fpu_q4, fpu_s18, fpu_s19,
                                      LLDB_INVALID_REGNUM};
static uint32_t g_d10_invalidates[] = {fpu_q5, fpu_s20, fpu_s21,
                                       LLDB_INVALID_REGNUM};
static uint32_t g_d11_invalidates[] = {fpu_q5, fpu_s22, fpu_s23,
                                       LLDB_INVALID_REGNUM};
static uint32_t g_d12_invalidates[] = {fpu_q6, fpu_s24, fpu_s25,
                                       LLDB_INVALID_REGNUM};
static uint32_t g_d13_invalidates[] = {fpu_q6, fpu_s26, fpu_s27,
                                       LLDB_INVALID_REGNUM};
static uint32_t g_d14_invalidates[] = {fpu_q7, fpu_s28, fpu_s29,
                                       LLDB_INVALID_REGNUM};
static uint32_t g_d15_invalidates[] = {fpu_q7, fpu_s30, fpu_s31,
                                       LLDB_INVALID_REGNUM};
static uint32_t g_d16_invalidates[] = {fpu_q8, LLDB_INVALID_REGNUM};
static uint32_t g_d17_invalidates[] = {fpu_q8, LLDB_INVALID_REGNUM};
static uint32_t g_d18_invalidates[] = {fpu_q9, LLDB_INVALID_REGNUM};
static uint32_t g_d19_invalidates[] = {fpu_q9, LLDB_INVALID_REGNUM};
static uint32_t g_d20_invalidates[] = {fpu_q10, LLDB_INVALID_REGNUM};
static uint32_t g_d21_invalidates[] = {fpu_q10, LLDB_INVALID_REGNUM};
static uint32_t g_d22_invalidates[] = {fpu_q11, LLDB_INVALID_REGNUM};
static uint32_t g_d23_invalidates[] = {fpu_q11, LLDB_INVALID_REGNUM};
static uint32_t g_d24_invalidates[] = {fpu_q12, LLDB_INVALID_REGNUM};
static uint32_t g_d25_invalidates[] = {fpu_q12, LLDB_INVALID_REGNUM};
static uint32_t g_d26_invalidates[] = {fpu_q13, LLDB_INVALID_REGNUM};
static uint32_t g_d27_invalidates[] = {fpu_q13, LLDB_INVALID_REGNUM};
static uint32_t g_d28_invalidates[] = {fpu_q14, LLDB_INVALID_REGNUM};
static uint32_t g_d29_invalidates[] = {fpu_q14, LLDB_INVALID_REGNUM};
static uint32_t g_d30_invalidates[] = {fpu_q15, LLDB_INVALID_REGNUM};
static uint32_t g_d31_invalidates[] = {fpu_q15, LLDB_INVALID_REGNUM};

static uint32_t g_q0_invalidates[] = {
    fpu_d0, fpu_d1, fpu_s0, fpu_s1, fpu_s2, fpu_s3, LLDB_INVALID_REGNUM};
static uint32_t g_q1_invalidates[] = {
    fpu_d2, fpu_d3, fpu_s4, fpu_s5, fpu_s6, fpu_s7, LLDB_INVALID_REGNUM};
static uint32_t g_q2_invalidates[] = {
    fpu_d4, fpu_d5, fpu_s8, fpu_s9, fpu_s10, fpu_s11, LLDB_INVALID_REGNUM};
static uint32_t g_q3_invalidates[] = {
    fpu_d6, fpu_d7, fpu_s12, fpu_s13, fpu_s14, fpu_s15, LLDB_INVALID_REGNUM};
static uint32_t g_q4_invalidates[] = {
    fpu_d8, fpu_d9, fpu_s16, fpu_s17, fpu_s18, fpu_s19, LLDB_INVALID_REGNUM};
static uint32_t g_q5_invalidates[] = {
    fpu_d10, fpu_d11, fpu_s20, fpu_s21, fpu_s22, fpu_s23, LLDB_INVALID_REGNUM};
static uint32_t g_q6_invalidates[] = {
    fpu_d12, fpu_d13, fpu_s24, fpu_s25, fpu_s26, fpu_s27, LLDB_INVALID_REGNUM};
static uint32_t g_q7_invalidates[] = {
    fpu_d14, fpu_d15, fpu_s28, fpu_s29, fpu_s30, fpu_s31, LLDB_INVALID_REGNUM};
static uint32_t g_q8_invalidates[] = {fpu_d16, fpu_d17, LLDB_INVALID_REGNUM};
static uint32_t g_q9_invalidates[] = {fpu_d18, fpu_d19, LLDB_INVALID_REGNUM};
static uint32_t g_q10_invalidates[] = {fpu_d20, fpu_d21, LLDB_INVALID_REGNUM};
static uint32_t g_q11_invalidates[] = {fpu_d22, fpu_d23, LLDB_INVALID_REGNUM};
static uint32_t g_q12_invalidates[] = {fpu_d24, fpu_d25, LLDB_INVALID_REGNUM};
static uint32_t g_q13_invalidates[] = {fpu_d26, fpu_d27, LLDB_INVALID_REGNUM};
static uint32_t g_q14_invalidates[] = {fpu_d28, fpu_d29, LLDB_INVALID_REGNUM};
static uint32_t g_q15_invalidates[] = {fpu_d30, fpu_d31, LLDB_INVALID_REGNUM};

static uint32_t g_q0_contained[] = {fpu_q0, LLDB_INVALID_REGNUM};
static uint32_t g_q1_contained[] = {fpu_q1, LLDB_INVALID_REGNUM};
static uint32_t g_q2_contained[] = {fpu_q2, LLDB_INVALID_REGNUM};
static uint32_t g_q3_contained[] = {fpu_q3, LLDB_INVALID_REGNUM};
static uint32_t g_q4_contained[] = {fpu_q4, LLDB_INVALID_REGNUM};
static uint32_t g_q5_contained[] = {fpu_q5, LLDB_INVALID_REGNUM};
static uint32_t g_q6_contained[] = {fpu_q6, LLDB_INVALID_REGNUM};
static uint32_t g_q7_contained[] = {fpu_q7, LLDB_INVALID_REGNUM};
static uint32_t g_q8_contained[] = {fpu_q8, LLDB_INVALID_REGNUM};
static uint32_t g_q9_contained[] = {fpu_q9, LLDB_INVALID_REGNUM};
static uint32_t g_q10_contained[] = {fpu_q10, LLDB_INVALID_REGNUM};
static uint32_t g_q11_contained[] = {fpu_q11, LLDB_INVALID_REGNUM};
static uint32_t g_q12_contained[] = {fpu_q12, LLDB_INVALID_REGNUM};
static uint32_t g_q13_contained[] = {fpu_q13, LLDB_INVALID_REGNUM};
static uint32_t g_q14_contained[] = {fpu_q14, LLDB_INVALID_REGNUM};
static uint32_t g_q15_contained[] = {fpu_q15, LLDB_INVALID_REGNUM};

#define FPU_REG(name, size, offset, qreg)                                      \
  {                                                                            \
    #name, nullptr, size, FPU_OFFSET(offset), eEncodingIEEE754, eFormatFloat,  \
        {LLDB_INVALID_REGNUM, dwarf_##name, LLDB_INVALID_REGNUM,               \
         LLDB_INVALID_REGNUM, fpu_##name },                                    \
         g_##qreg##_contained, g_##name##_invalidates, nullptr,                \
  }

#define FPU_QREG(name, offset)                                                 \
  {                                                                            \
    #name, nullptr, 16, FPU_OFFSET(offset), eEncodingVector,                   \
        eFormatVectorOfUInt8,                                                  \
        {LLDB_INVALID_REGNUM, dwarf_##name, LLDB_INVALID_REGNUM,               \
         LLDB_INVALID_REGNUM, fpu_##name },                                    \
         nullptr, g_##name##_invalidates, nullptr,                             \
  }

static RegisterInfo g_register_infos_arm[] = {
    //  NAME         ALT     SZ   OFFSET          ENCODING          FORMAT
    //  EH_FRAME             DWARF                GENERIC
    //  PROCESS PLUGIN       LLDB NATIVE      VALUE REGS      INVALIDATE REGS
    //  ===========  ======= ==   ==============  ================
    //  ====================    ===================  ===================
    //  ==========================  ===================  =============
    //  ==============  =================
    {
        "r0",
        nullptr,
        4,
        GPR_OFFSET(0),
        eEncodingUint,
        eFormatHex,
        {ehframe_r0, dwarf_r0, LLDB_REGNUM_GENERIC_ARG1, LLDB_INVALID_REGNUM,
         gpr_r0},
        nullptr,
        nullptr,
        nullptr,
    },
    {
        "r1",
        nullptr,
        4,
        GPR_OFFSET(1),
        eEncodingUint,
        eFormatHex,
        {ehframe_r1, dwarf_r1, LLDB_REGNUM_GENERIC_ARG2, LLDB_INVALID_REGNUM,
         gpr_r1},
        nullptr,
        nullptr,
        nullptr,
    },
    {
        "r2",
        nullptr,
        4,
        GPR_OFFSET(2),
        eEncodingUint,
        eFormatHex,
        {ehframe_r2, dwarf_r2, LLDB_REGNUM_GENERIC_ARG3, LLDB_INVALID_REGNUM,
         gpr_r2},
        nullptr,
        nullptr,
        nullptr,
    },
    {
        "r3",
        nullptr,
        4,
        GPR_OFFSET(3),
        eEncodingUint,
        eFormatHex,
        {ehframe_r3, dwarf_r3, LLDB_REGNUM_GENERIC_ARG4, LLDB_INVALID_REGNUM,
         gpr_r3},
        nullptr,
        nullptr,
        nullptr,
    },
    {
        "r4",
        nullptr,
        4,
        GPR_OFFSET(4),
        eEncodingUint,
        eFormatHex,
        {ehframe_r4, dwarf_r4, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
         gpr_r4},
        nullptr,
        nullptr,
        nullptr,
    },
    {
        "r5",
        nullptr,
        4,
        GPR_OFFSET(5),
        eEncodingUint,
        eFormatHex,
        {ehframe_r5, dwarf_r5, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
         gpr_r5},
        nullptr,
        nullptr,
        nullptr,
    },
    {
        "r6",
        nullptr,
        4,
        GPR_OFFSET(6),
        eEncodingUint,
        eFormatHex,
        {ehframe_r6, dwarf_r6, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
         gpr_r6},
        nullptr,
        nullptr,
        nullptr,
    },
    {
        "r7",
        nullptr,
        4,
        GPR_OFFSET(7),
        eEncodingUint,
        eFormatHex,
        {ehframe_r7, dwarf_r7, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
         gpr_r7},
        nullptr,
        nullptr,
        nullptr,
    },
    {
        "r8",
        nullptr,
        4,
        GPR_OFFSET(8),
        eEncodingUint,
        eFormatHex,
        {ehframe_r8, dwarf_r8, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
         gpr_r8},
        nullptr,
        nullptr,
        nullptr,
    },
    {
        "r9",
        nullptr,
        4,
        GPR_OFFSET(9),
        eEncodingUint,
        eFormatHex,
        {ehframe_r9, dwarf_r9, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
         gpr_r9},
        nullptr,
        nullptr,
        nullptr,
    },
    {
        "r10",
        nullptr,
        4,
        GPR_OFFSET(10),
        eEncodingUint,
        eFormatHex,
        {ehframe_r10, dwarf_r10, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
         gpr_r10},
        nullptr,
        nullptr,
        nullptr,
    },
    {
        "r11",
        nullptr,
        4,
        GPR_OFFSET(11),
        eEncodingUint,
        eFormatHex,
        {ehframe_r11, dwarf_r11, LLDB_REGNUM_GENERIC_FP, LLDB_INVALID_REGNUM,
         gpr_r11},
        nullptr,
        nullptr,
        nullptr,
    },
    {
        "r12",
        nullptr,
        4,
        GPR_OFFSET(12),
        eEncodingUint,
        eFormatHex,
        {ehframe_r12, dwarf_r12, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
         gpr_r12},
        nullptr,
        nullptr,
        nullptr,
    },
    {
        "sp",
        "r13",
        4,
        GPR_OFFSET(13),
        eEncodingUint,
        eFormatHex,
        {ehframe_sp, dwarf_sp, LLDB_REGNUM_GENERIC_SP, LLDB_INVALID_REGNUM,
         gpr_sp},
        nullptr,
        nullptr,
        nullptr,
    },
    {
        "lr",
        "r14",
        4,
        GPR_OFFSET(14),
        eEncodingUint,
        eFormatHex,
        {ehframe_lr, dwarf_lr, LLDB_REGNUM_GENERIC_RA, LLDB_INVALID_REGNUM,
         gpr_lr},
        nullptr,
        nullptr,
        nullptr,
    },
    {
        "pc",
        "r15",
        4,
        GPR_OFFSET(15),
        eEncodingUint,
        eFormatHex,
        {ehframe_pc, dwarf_pc, LLDB_REGNUM_GENERIC_PC, LLDB_INVALID_REGNUM,
         gpr_pc},
        nullptr,
        nullptr,
        nullptr,
    },
    {
        "cpsr",
        "psr",
        4,
        GPR_OFFSET(16),
        eEncodingUint,
        eFormatHex,
        {ehframe_cpsr, dwarf_cpsr, LLDB_REGNUM_GENERIC_FLAGS,
         LLDB_INVALID_REGNUM, gpr_cpsr},
        nullptr,
        nullptr,
        nullptr,
    },

    FPU_REG(s0, 4, 0, q0),
    FPU_REG(s1, 4, 1, q0),
    FPU_REG(s2, 4, 2, q0),
    FPU_REG(s3, 4, 3, q0),
    FPU_REG(s4, 4, 4, q1),
    FPU_REG(s5, 4, 5, q1),
    FPU_REG(s6, 4, 6, q1),
    FPU_REG(s7, 4, 7, q1),
    FPU_REG(s8, 4, 8, q2),
    FPU_REG(s9, 4, 9, q2),
    FPU_REG(s10, 4, 10, q2),
    FPU_REG(s11, 4, 11, q2),
    FPU_REG(s12, 4, 12, q3),
    FPU_REG(s13, 4, 13, q3),
    FPU_REG(s14, 4, 14, q3),
    FPU_REG(s15, 4, 15, q3),
    FPU_REG(s16, 4, 16, q4),
    FPU_REG(s17, 4, 17, q4),
    FPU_REG(s18, 4, 18, q4),
    FPU_REG(s19, 4, 19, q4),
    FPU_REG(s20, 4, 20, q5),
    FPU_REG(s21, 4, 21, q5),
    FPU_REG(s22, 4, 22, q5),
    FPU_REG(s23, 4, 23, q5),
    FPU_REG(s24, 4, 24, q6),
    FPU_REG(s25, 4, 25, q6),
    FPU_REG(s26, 4, 26, q6),
    FPU_REG(s27, 4, 27, q6),
    FPU_REG(s28, 4, 28, q7),
    FPU_REG(s29, 4, 29, q7),
    FPU_REG(s30, 4, 30, q7),
    FPU_REG(s31, 4, 31, q7),

    {
        "fpscr",
        nullptr,
        4,
        FPSCR_OFFSET,
        eEncodingUint,
        eFormatHex,
        {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
         LLDB_INVALID_REGNUM, fpu_fpscr},
        nullptr,
        nullptr,
        nullptr,
    },

    FPU_REG(d0, 8, 0, q0),
    FPU_REG(d1, 8, 2, q0),
    FPU_REG(d2, 8, 4, q1),
    FPU_REG(d3, 8, 6, q1),
    FPU_REG(d4, 8, 8, q2),
    FPU_REG(d5, 8, 10, q2),
    FPU_REG(d6, 8, 12, q3),
    FPU_REG(d7, 8, 14, q3),
    FPU_REG(d8, 8, 16, q4),
    FPU_REG(d9, 8, 18, q4),
    FPU_REG(d10, 8, 20, q5),
    FPU_REG(d11, 8, 22, q5),
    FPU_REG(d12, 8, 24, q6),
    FPU_REG(d13, 8, 26, q6),
    FPU_REG(d14, 8, 28, q7),
    FPU_REG(d15, 8, 30, q7),
    FPU_REG(d16, 8, 32, q8),
    FPU_REG(d17, 8, 34, q8),
    FPU_REG(d18, 8, 36, q9),
    FPU_REG(d19, 8, 38, q9),
    FPU_REG(d20, 8, 40, q10),
    FPU_REG(d21, 8, 42, q10),
    FPU_REG(d22, 8, 44, q11),
    FPU_REG(d23, 8, 46, q11),
    FPU_REG(d24, 8, 48, q12),
    FPU_REG(d25, 8, 50, q12),
    FPU_REG(d26, 8, 52, q13),
    FPU_REG(d27, 8, 54, q13),
    FPU_REG(d28, 8, 56, q14),
    FPU_REG(d29, 8, 58, q14),
    FPU_REG(d30, 8, 60, q15),
    FPU_REG(d31, 8, 62, q15),

    FPU_QREG(q0, 0),
    FPU_QREG(q1, 4),
    FPU_QREG(q2, 8),
    FPU_QREG(q3, 12),
    FPU_QREG(q4, 16),
    FPU_QREG(q5, 20),
    FPU_QREG(q6, 24),
    FPU_QREG(q7, 28),
    FPU_QREG(q8, 32),
    FPU_QREG(q9, 36),
    FPU_QREG(q10, 40),
    FPU_QREG(q11, 44),
    FPU_QREG(q12, 48),
    FPU_QREG(q13, 52),
    FPU_QREG(q14, 56),
    FPU_QREG(q15, 60),

    {
        "exception",
        nullptr,
        4,
        EXC_OFFSET(0),
        eEncodingUint,
        eFormatHex,
        {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
         LLDB_INVALID_REGNUM, exc_exception},
        nullptr,
        nullptr,
        nullptr,
    },
    {
        "fsr",
        nullptr,
        4,
        EXC_OFFSET(1),
        eEncodingUint,
        eFormatHex,
        {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
         LLDB_INVALID_REGNUM, exc_fsr},
        nullptr,
        nullptr,
        nullptr,
    },
    {
        "far",
        nullptr,
        4,
        EXC_OFFSET(2),
        eEncodingUint,
        eFormatHex,
        {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
         LLDB_INVALID_REGNUM, exc_far},
        nullptr,
        nullptr,
        nullptr,
    },

    {DEFINE_DBG(bvr, 0)},
    {DEFINE_DBG(bvr, 1)},
    {DEFINE_DBG(bvr, 2)},
    {DEFINE_DBG(bvr, 3)},
    {DEFINE_DBG(bvr, 4)},
    {DEFINE_DBG(bvr, 5)},
    {DEFINE_DBG(bvr, 6)},
    {DEFINE_DBG(bvr, 7)},
    {DEFINE_DBG(bvr, 8)},
    {DEFINE_DBG(bvr, 9)},
    {DEFINE_DBG(bvr, 10)},
    {DEFINE_DBG(bvr, 11)},
    {DEFINE_DBG(bvr, 12)},
    {DEFINE_DBG(bvr, 13)},
    {DEFINE_DBG(bvr, 14)},
    {DEFINE_DBG(bvr, 15)},

    {DEFINE_DBG(bcr, 0)},
    {DEFINE_DBG(bcr, 1)},
    {DEFINE_DBG(bcr, 2)},
    {DEFINE_DBG(bcr, 3)},
    {DEFINE_DBG(bcr, 4)},
    {DEFINE_DBG(bcr, 5)},
    {DEFINE_DBG(bcr, 6)},
    {DEFINE_DBG(bcr, 7)},
    {DEFINE_DBG(bcr, 8)},
    {DEFINE_DBG(bcr, 9)},
    {DEFINE_DBG(bcr, 10)},
    {DEFINE_DBG(bcr, 11)},
    {DEFINE_DBG(bcr, 12)},
    {DEFINE_DBG(bcr, 13)},
    {DEFINE_DBG(bcr, 14)},
    {DEFINE_DBG(bcr, 15)},

    {DEFINE_DBG(wvr, 0)},
    {DEFINE_DBG(wvr, 1)},
    {DEFINE_DBG(wvr, 2)},
    {DEFINE_DBG(wvr, 3)},
    {DEFINE_DBG(wvr, 4)},
    {DEFINE_DBG(wvr, 5)},
    {DEFINE_DBG(wvr, 6)},
    {DEFINE_DBG(wvr, 7)},
    {DEFINE_DBG(wvr, 8)},
    {DEFINE_DBG(wvr, 9)},
    {DEFINE_DBG(wvr, 10)},
    {DEFINE_DBG(wvr, 11)},
    {DEFINE_DBG(wvr, 12)},
    {DEFINE_DBG(wvr, 13)},
    {DEFINE_DBG(wvr, 14)},
    {DEFINE_DBG(wvr, 15)},

    {DEFINE_DBG(wcr, 0)},
    {DEFINE_DBG(wcr, 1)},
    {DEFINE_DBG(wcr, 2)},
    {DEFINE_DBG(wcr, 3)},
    {DEFINE_DBG(wcr, 4)},
    {DEFINE_DBG(wcr, 5)},
    {DEFINE_DBG(wcr, 6)},
    {DEFINE_DBG(wcr, 7)},
    {DEFINE_DBG(wcr, 8)},
    {DEFINE_DBG(wcr, 9)},
    {DEFINE_DBG(wcr, 10)},
    {DEFINE_DBG(wcr, 11)},
    {DEFINE_DBG(wcr, 12)},
    {DEFINE_DBG(wcr, 13)},
    {DEFINE_DBG(wcr, 14)},
    {DEFINE_DBG(wcr, 15)}};

#endif // DECLARE_REGISTER_INFOS_ARM_STRUCT
