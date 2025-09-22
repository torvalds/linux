//===-- RegisterInfos_arm64.h -----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifdef DECLARE_REGISTER_INFOS_ARM64_STRUCT

#include <cstddef>

#include "lldb/lldb-defines.h"
#include "lldb/lldb-enumerations.h"
#include "lldb/lldb-private.h"

#include "Utility/ARM64_DWARF_Registers.h"
#include "Utility/ARM64_ehframe_Registers.h"

#ifndef GPR_OFFSET
#error GPR_OFFSET must be defined before including this header file
#endif

#ifndef GPR_OFFSET_NAME
#error GPR_OFFSET_NAME must be defined before including this header file
#endif

#ifndef FPU_OFFSET
#error FPU_OFFSET must be defined before including this header file
#endif

#ifndef FPU_OFFSET_NAME
#error FPU_OFFSET_NAME must be defined before including this header file
#endif

#ifndef EXC_OFFSET_NAME
#error EXC_OFFSET_NAME must be defined before including this header file
#endif

#ifndef DBG_OFFSET_NAME
#error DBG_OFFSET_NAME must be defined before including this header file
#endif

#ifndef DEFINE_DBG
#error DEFINE_DBG must be defined before including this header file
#endif

// Offsets for a little-endian layout of the register context
#define GPR_W_PSEUDO_REG_ENDIAN_OFFSET 0
#define FPU_S_PSEUDO_REG_ENDIAN_OFFSET 0
#define FPU_D_PSEUDO_REG_ENDIAN_OFFSET 0

enum {
  gpr_x0 = 0,
  gpr_x1,
  gpr_x2,
  gpr_x3,
  gpr_x4,
  gpr_x5,
  gpr_x6,
  gpr_x7,
  gpr_x8,
  gpr_x9,
  gpr_x10,
  gpr_x11,
  gpr_x12,
  gpr_x13,
  gpr_x14,
  gpr_x15,
  gpr_x16,
  gpr_x17,
  gpr_x18,
  gpr_x19,
  gpr_x20,
  gpr_x21,
  gpr_x22,
  gpr_x23,
  gpr_x24,
  gpr_x25,
  gpr_x26,
  gpr_x27,
  gpr_x28,
  gpr_x29 = 29,
  gpr_fp = gpr_x29,
  gpr_x30 = 30,
  gpr_lr = gpr_x30,
  gpr_ra = gpr_x30,
  gpr_x31 = 31,
  gpr_sp = gpr_x31,
  gpr_pc = 32,
  gpr_cpsr,

  gpr_w0,
  gpr_w1,
  gpr_w2,
  gpr_w3,
  gpr_w4,
  gpr_w5,
  gpr_w6,
  gpr_w7,
  gpr_w8,
  gpr_w9,
  gpr_w10,
  gpr_w11,
  gpr_w12,
  gpr_w13,
  gpr_w14,
  gpr_w15,
  gpr_w16,
  gpr_w17,
  gpr_w18,
  gpr_w19,
  gpr_w20,
  gpr_w21,
  gpr_w22,
  gpr_w23,
  gpr_w24,
  gpr_w25,
  gpr_w26,
  gpr_w27,
  gpr_w28,

  fpu_v0,
  fpu_v1,
  fpu_v2,
  fpu_v3,
  fpu_v4,
  fpu_v5,
  fpu_v6,
  fpu_v7,
  fpu_v8,
  fpu_v9,
  fpu_v10,
  fpu_v11,
  fpu_v12,
  fpu_v13,
  fpu_v14,
  fpu_v15,
  fpu_v16,
  fpu_v17,
  fpu_v18,
  fpu_v19,
  fpu_v20,
  fpu_v21,
  fpu_v22,
  fpu_v23,
  fpu_v24,
  fpu_v25,
  fpu_v26,
  fpu_v27,
  fpu_v28,
  fpu_v29,
  fpu_v30,
  fpu_v31,

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

  fpu_fpsr,
  fpu_fpcr,

  exc_far,
  exc_esr,
  exc_exception,

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

static uint32_t g_contained_x0[] = {gpr_x0, LLDB_INVALID_REGNUM};
static uint32_t g_contained_x1[] = {gpr_x1, LLDB_INVALID_REGNUM};
static uint32_t g_contained_x2[] = {gpr_x2, LLDB_INVALID_REGNUM};
static uint32_t g_contained_x3[] = {gpr_x3, LLDB_INVALID_REGNUM};
static uint32_t g_contained_x4[] = {gpr_x4, LLDB_INVALID_REGNUM};
static uint32_t g_contained_x5[] = {gpr_x5, LLDB_INVALID_REGNUM};
static uint32_t g_contained_x6[] = {gpr_x6, LLDB_INVALID_REGNUM};
static uint32_t g_contained_x7[] = {gpr_x7, LLDB_INVALID_REGNUM};
static uint32_t g_contained_x8[] = {gpr_x8, LLDB_INVALID_REGNUM};
static uint32_t g_contained_x9[] = {gpr_x9, LLDB_INVALID_REGNUM};
static uint32_t g_contained_x10[] = {gpr_x10, LLDB_INVALID_REGNUM};
static uint32_t g_contained_x11[] = {gpr_x11, LLDB_INVALID_REGNUM};
static uint32_t g_contained_x12[] = {gpr_x12, LLDB_INVALID_REGNUM};
static uint32_t g_contained_x13[] = {gpr_x13, LLDB_INVALID_REGNUM};
static uint32_t g_contained_x14[] = {gpr_x14, LLDB_INVALID_REGNUM};
static uint32_t g_contained_x15[] = {gpr_x15, LLDB_INVALID_REGNUM};
static uint32_t g_contained_x16[] = {gpr_x16, LLDB_INVALID_REGNUM};
static uint32_t g_contained_x17[] = {gpr_x17, LLDB_INVALID_REGNUM};
static uint32_t g_contained_x18[] = {gpr_x18, LLDB_INVALID_REGNUM};
static uint32_t g_contained_x19[] = {gpr_x19, LLDB_INVALID_REGNUM};
static uint32_t g_contained_x20[] = {gpr_x20, LLDB_INVALID_REGNUM};
static uint32_t g_contained_x21[] = {gpr_x21, LLDB_INVALID_REGNUM};
static uint32_t g_contained_x22[] = {gpr_x22, LLDB_INVALID_REGNUM};
static uint32_t g_contained_x23[] = {gpr_x23, LLDB_INVALID_REGNUM};
static uint32_t g_contained_x24[] = {gpr_x24, LLDB_INVALID_REGNUM};
static uint32_t g_contained_x25[] = {gpr_x25, LLDB_INVALID_REGNUM};
static uint32_t g_contained_x26[] = {gpr_x26, LLDB_INVALID_REGNUM};
static uint32_t g_contained_x27[] = {gpr_x27, LLDB_INVALID_REGNUM};
static uint32_t g_contained_x28[] = {gpr_x28, LLDB_INVALID_REGNUM};

static uint32_t g_w0_invalidates[] = {gpr_x0, LLDB_INVALID_REGNUM};
static uint32_t g_w1_invalidates[] = {gpr_x1, LLDB_INVALID_REGNUM};
static uint32_t g_w2_invalidates[] = {gpr_x2, LLDB_INVALID_REGNUM};
static uint32_t g_w3_invalidates[] = {gpr_x3, LLDB_INVALID_REGNUM};
static uint32_t g_w4_invalidates[] = {gpr_x4, LLDB_INVALID_REGNUM};
static uint32_t g_w5_invalidates[] = {gpr_x5, LLDB_INVALID_REGNUM};
static uint32_t g_w6_invalidates[] = {gpr_x6, LLDB_INVALID_REGNUM};
static uint32_t g_w7_invalidates[] = {gpr_x7, LLDB_INVALID_REGNUM};
static uint32_t g_w8_invalidates[] = {gpr_x8, LLDB_INVALID_REGNUM};
static uint32_t g_w9_invalidates[] = {gpr_x9, LLDB_INVALID_REGNUM};
static uint32_t g_w10_invalidates[] = {gpr_x10, LLDB_INVALID_REGNUM};
static uint32_t g_w11_invalidates[] = {gpr_x11, LLDB_INVALID_REGNUM};
static uint32_t g_w12_invalidates[] = {gpr_x12, LLDB_INVALID_REGNUM};
static uint32_t g_w13_invalidates[] = {gpr_x13, LLDB_INVALID_REGNUM};
static uint32_t g_w14_invalidates[] = {gpr_x14, LLDB_INVALID_REGNUM};
static uint32_t g_w15_invalidates[] = {gpr_x15, LLDB_INVALID_REGNUM};
static uint32_t g_w16_invalidates[] = {gpr_x16, LLDB_INVALID_REGNUM};
static uint32_t g_w17_invalidates[] = {gpr_x17, LLDB_INVALID_REGNUM};
static uint32_t g_w18_invalidates[] = {gpr_x18, LLDB_INVALID_REGNUM};
static uint32_t g_w19_invalidates[] = {gpr_x19, LLDB_INVALID_REGNUM};
static uint32_t g_w20_invalidates[] = {gpr_x20, LLDB_INVALID_REGNUM};
static uint32_t g_w21_invalidates[] = {gpr_x21, LLDB_INVALID_REGNUM};
static uint32_t g_w22_invalidates[] = {gpr_x22, LLDB_INVALID_REGNUM};
static uint32_t g_w23_invalidates[] = {gpr_x23, LLDB_INVALID_REGNUM};
static uint32_t g_w24_invalidates[] = {gpr_x24, LLDB_INVALID_REGNUM};
static uint32_t g_w25_invalidates[] = {gpr_x25, LLDB_INVALID_REGNUM};
static uint32_t g_w26_invalidates[] = {gpr_x26, LLDB_INVALID_REGNUM};
static uint32_t g_w27_invalidates[] = {gpr_x27, LLDB_INVALID_REGNUM};
static uint32_t g_w28_invalidates[] = {gpr_x28, LLDB_INVALID_REGNUM};

static uint32_t g_contained_v0[] = {fpu_v0, LLDB_INVALID_REGNUM};
static uint32_t g_contained_v1[] = {fpu_v1, LLDB_INVALID_REGNUM};
static uint32_t g_contained_v2[] = {fpu_v2, LLDB_INVALID_REGNUM};
static uint32_t g_contained_v3[] = {fpu_v3, LLDB_INVALID_REGNUM};
static uint32_t g_contained_v4[] = {fpu_v4, LLDB_INVALID_REGNUM};
static uint32_t g_contained_v5[] = {fpu_v5, LLDB_INVALID_REGNUM};
static uint32_t g_contained_v6[] = {fpu_v6, LLDB_INVALID_REGNUM};
static uint32_t g_contained_v7[] = {fpu_v7, LLDB_INVALID_REGNUM};
static uint32_t g_contained_v8[] = {fpu_v8, LLDB_INVALID_REGNUM};
static uint32_t g_contained_v9[] = {fpu_v9, LLDB_INVALID_REGNUM};
static uint32_t g_contained_v10[] = {fpu_v10, LLDB_INVALID_REGNUM};
static uint32_t g_contained_v11[] = {fpu_v11, LLDB_INVALID_REGNUM};
static uint32_t g_contained_v12[] = {fpu_v12, LLDB_INVALID_REGNUM};
static uint32_t g_contained_v13[] = {fpu_v13, LLDB_INVALID_REGNUM};
static uint32_t g_contained_v14[] = {fpu_v14, LLDB_INVALID_REGNUM};
static uint32_t g_contained_v15[] = {fpu_v15, LLDB_INVALID_REGNUM};
static uint32_t g_contained_v16[] = {fpu_v16, LLDB_INVALID_REGNUM};
static uint32_t g_contained_v17[] = {fpu_v17, LLDB_INVALID_REGNUM};
static uint32_t g_contained_v18[] = {fpu_v18, LLDB_INVALID_REGNUM};
static uint32_t g_contained_v19[] = {fpu_v19, LLDB_INVALID_REGNUM};
static uint32_t g_contained_v20[] = {fpu_v20, LLDB_INVALID_REGNUM};
static uint32_t g_contained_v21[] = {fpu_v21, LLDB_INVALID_REGNUM};
static uint32_t g_contained_v22[] = {fpu_v22, LLDB_INVALID_REGNUM};
static uint32_t g_contained_v23[] = {fpu_v23, LLDB_INVALID_REGNUM};
static uint32_t g_contained_v24[] = {fpu_v24, LLDB_INVALID_REGNUM};
static uint32_t g_contained_v25[] = {fpu_v25, LLDB_INVALID_REGNUM};
static uint32_t g_contained_v26[] = {fpu_v26, LLDB_INVALID_REGNUM};
static uint32_t g_contained_v27[] = {fpu_v27, LLDB_INVALID_REGNUM};
static uint32_t g_contained_v28[] = {fpu_v28, LLDB_INVALID_REGNUM};
static uint32_t g_contained_v29[] = {fpu_v29, LLDB_INVALID_REGNUM};
static uint32_t g_contained_v30[] = {fpu_v30, LLDB_INVALID_REGNUM};
static uint32_t g_contained_v31[] = {fpu_v31, LLDB_INVALID_REGNUM};

static uint32_t g_s0_invalidates[] = {fpu_v0, fpu_d0, LLDB_INVALID_REGNUM};
static uint32_t g_s1_invalidates[] = {fpu_v1, fpu_d1, LLDB_INVALID_REGNUM};
static uint32_t g_s2_invalidates[] = {fpu_v2, fpu_d2, LLDB_INVALID_REGNUM};
static uint32_t g_s3_invalidates[] = {fpu_v3, fpu_d3, LLDB_INVALID_REGNUM};
static uint32_t g_s4_invalidates[] = {fpu_v4, fpu_d4, LLDB_INVALID_REGNUM};
static uint32_t g_s5_invalidates[] = {fpu_v5, fpu_d5, LLDB_INVALID_REGNUM};
static uint32_t g_s6_invalidates[] = {fpu_v6, fpu_d6, LLDB_INVALID_REGNUM};
static uint32_t g_s7_invalidates[] = {fpu_v7, fpu_d7, LLDB_INVALID_REGNUM};
static uint32_t g_s8_invalidates[] = {fpu_v8, fpu_d8, LLDB_INVALID_REGNUM};
static uint32_t g_s9_invalidates[] = {fpu_v9, fpu_d9, LLDB_INVALID_REGNUM};
static uint32_t g_s10_invalidates[] = {fpu_v10, fpu_d10, LLDB_INVALID_REGNUM};
static uint32_t g_s11_invalidates[] = {fpu_v11, fpu_d11, LLDB_INVALID_REGNUM};
static uint32_t g_s12_invalidates[] = {fpu_v12, fpu_d12, LLDB_INVALID_REGNUM};
static uint32_t g_s13_invalidates[] = {fpu_v13, fpu_d13, LLDB_INVALID_REGNUM};
static uint32_t g_s14_invalidates[] = {fpu_v14, fpu_d14, LLDB_INVALID_REGNUM};
static uint32_t g_s15_invalidates[] = {fpu_v15, fpu_d15, LLDB_INVALID_REGNUM};
static uint32_t g_s16_invalidates[] = {fpu_v16, fpu_d16, LLDB_INVALID_REGNUM};
static uint32_t g_s17_invalidates[] = {fpu_v17, fpu_d17, LLDB_INVALID_REGNUM};
static uint32_t g_s18_invalidates[] = {fpu_v18, fpu_d18, LLDB_INVALID_REGNUM};
static uint32_t g_s19_invalidates[] = {fpu_v19, fpu_d19, LLDB_INVALID_REGNUM};
static uint32_t g_s20_invalidates[] = {fpu_v20, fpu_d20, LLDB_INVALID_REGNUM};
static uint32_t g_s21_invalidates[] = {fpu_v21, fpu_d21, LLDB_INVALID_REGNUM};
static uint32_t g_s22_invalidates[] = {fpu_v22, fpu_d22, LLDB_INVALID_REGNUM};
static uint32_t g_s23_invalidates[] = {fpu_v23, fpu_d23, LLDB_INVALID_REGNUM};
static uint32_t g_s24_invalidates[] = {fpu_v24, fpu_d24, LLDB_INVALID_REGNUM};
static uint32_t g_s25_invalidates[] = {fpu_v25, fpu_d25, LLDB_INVALID_REGNUM};
static uint32_t g_s26_invalidates[] = {fpu_v26, fpu_d26, LLDB_INVALID_REGNUM};
static uint32_t g_s27_invalidates[] = {fpu_v27, fpu_d27, LLDB_INVALID_REGNUM};
static uint32_t g_s28_invalidates[] = {fpu_v28, fpu_d28, LLDB_INVALID_REGNUM};
static uint32_t g_s29_invalidates[] = {fpu_v29, fpu_d29, LLDB_INVALID_REGNUM};
static uint32_t g_s30_invalidates[] = {fpu_v30, fpu_d30, LLDB_INVALID_REGNUM};
static uint32_t g_s31_invalidates[] = {fpu_v31, fpu_d31, LLDB_INVALID_REGNUM};

static uint32_t g_d0_invalidates[] = {fpu_v0, fpu_s0, LLDB_INVALID_REGNUM};
static uint32_t g_d1_invalidates[] = {fpu_v1, fpu_s1, LLDB_INVALID_REGNUM};
static uint32_t g_d2_invalidates[] = {fpu_v2, fpu_s2, LLDB_INVALID_REGNUM};
static uint32_t g_d3_invalidates[] = {fpu_v3, fpu_s3, LLDB_INVALID_REGNUM};
static uint32_t g_d4_invalidates[] = {fpu_v4, fpu_s4, LLDB_INVALID_REGNUM};
static uint32_t g_d5_invalidates[] = {fpu_v5, fpu_s5, LLDB_INVALID_REGNUM};
static uint32_t g_d6_invalidates[] = {fpu_v6, fpu_s6, LLDB_INVALID_REGNUM};
static uint32_t g_d7_invalidates[] = {fpu_v7, fpu_s7, LLDB_INVALID_REGNUM};
static uint32_t g_d8_invalidates[] = {fpu_v8, fpu_s8, LLDB_INVALID_REGNUM};
static uint32_t g_d9_invalidates[] = {fpu_v9, fpu_s9, LLDB_INVALID_REGNUM};
static uint32_t g_d10_invalidates[] = {fpu_v10, fpu_s10, LLDB_INVALID_REGNUM};
static uint32_t g_d11_invalidates[] = {fpu_v11, fpu_s11, LLDB_INVALID_REGNUM};
static uint32_t g_d12_invalidates[] = {fpu_v12, fpu_s12, LLDB_INVALID_REGNUM};
static uint32_t g_d13_invalidates[] = {fpu_v13, fpu_s13, LLDB_INVALID_REGNUM};
static uint32_t g_d14_invalidates[] = {fpu_v14, fpu_s14, LLDB_INVALID_REGNUM};
static uint32_t g_d15_invalidates[] = {fpu_v15, fpu_s15, LLDB_INVALID_REGNUM};
static uint32_t g_d16_invalidates[] = {fpu_v16, fpu_s16, LLDB_INVALID_REGNUM};
static uint32_t g_d17_invalidates[] = {fpu_v17, fpu_s17, LLDB_INVALID_REGNUM};
static uint32_t g_d18_invalidates[] = {fpu_v18, fpu_s18, LLDB_INVALID_REGNUM};
static uint32_t g_d19_invalidates[] = {fpu_v19, fpu_s19, LLDB_INVALID_REGNUM};
static uint32_t g_d20_invalidates[] = {fpu_v20, fpu_s20, LLDB_INVALID_REGNUM};
static uint32_t g_d21_invalidates[] = {fpu_v21, fpu_s21, LLDB_INVALID_REGNUM};
static uint32_t g_d22_invalidates[] = {fpu_v22, fpu_s22, LLDB_INVALID_REGNUM};
static uint32_t g_d23_invalidates[] = {fpu_v23, fpu_s23, LLDB_INVALID_REGNUM};
static uint32_t g_d24_invalidates[] = {fpu_v24, fpu_s24, LLDB_INVALID_REGNUM};
static uint32_t g_d25_invalidates[] = {fpu_v25, fpu_s25, LLDB_INVALID_REGNUM};
static uint32_t g_d26_invalidates[] = {fpu_v26, fpu_s26, LLDB_INVALID_REGNUM};
static uint32_t g_d27_invalidates[] = {fpu_v27, fpu_s27, LLDB_INVALID_REGNUM};
static uint32_t g_d28_invalidates[] = {fpu_v28, fpu_s28, LLDB_INVALID_REGNUM};
static uint32_t g_d29_invalidates[] = {fpu_v29, fpu_s29, LLDB_INVALID_REGNUM};
static uint32_t g_d30_invalidates[] = {fpu_v30, fpu_s30, LLDB_INVALID_REGNUM};
static uint32_t g_d31_invalidates[] = {fpu_v31, fpu_s31, LLDB_INVALID_REGNUM};

// Generates register kinds array with DWARF, EH frame and generic kind
#define MISC_KIND(reg, type, generic_kind)                                     \
  {                                                                            \
    arm64_ehframe::reg, arm64_dwarf::reg, generic_kind, LLDB_INVALID_REGNUM,   \
        type##_##reg                                                           \
  }

// Generates register kinds array for registers with only lldb kind
#define LLDB_KIND(lldb_kind)                                                   \
  {                                                                            \
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,             \
        LLDB_INVALID_REGNUM, lldb_kind                                         \
  }

// Generates register kinds array for registers with only lldb kind
#define KIND_ALL_INVALID                                                       \
  {                                                                            \
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,             \
        LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM                               \
  }

// Generates register kinds array for vector registers
#define GPR64_KIND(reg, generic_kind) MISC_KIND(reg, gpr, generic_kind)
#define VREG_KIND(reg) MISC_KIND(reg, fpu, LLDB_INVALID_REGNUM)
#define MISC_GPR_KIND(lldb_kind) MISC_KIND(cpsr, gpr, LLDB_REGNUM_GENERIC_FLAGS)
#define MISC_FPU_KIND(lldb_kind) LLDB_KIND(lldb_kind)
#define MISC_EXC_KIND(lldb_kind) LLDB_KIND(lldb_kind)

// clang-format off

// Defines a 64-bit general purpose register
#define DEFINE_GPR64(reg, generic_kind)                                        \
  {                                                                            \
    #reg, nullptr, 8, GPR_OFFSET(gpr_##reg), lldb::eEncodingUint,              \
        lldb::eFormatHex, GPR64_KIND(reg, generic_kind), nullptr, nullptr,     \
        nullptr,                                                               \
  }

// Defines a 64-bit general purpose register
#define DEFINE_GPR64_ALT(reg, alt, generic_kind)                               \
  {                                                                            \
    #reg, #alt, 8, GPR_OFFSET(gpr_##reg), lldb::eEncodingUint,                 \
        lldb::eFormatHex, GPR64_KIND(reg, generic_kind), nullptr, nullptr,     \
        nullptr,                                                               \
  }

// Defines a 32-bit general purpose pseudo register
#define DEFINE_GPR32(wreg, xreg)                                               \
  {                                                                            \
    #wreg, nullptr, 4,                                                         \
        GPR_OFFSET(gpr_##xreg) + GPR_W_PSEUDO_REG_ENDIAN_OFFSET,               \
        lldb::eEncodingUint, lldb::eFormatHex, LLDB_KIND(gpr_##wreg),          \
        g_contained_##xreg, g_##wreg##_invalidates, nullptr,                   \
  }

// Defines a vector register with 16-byte size
#define DEFINE_VREG(reg)                                                       \
  {                                                                            \
    #reg, nullptr, 16, FPU_OFFSET(fpu_##reg - fpu_v0), lldb::eEncodingVector,  \
        lldb::eFormatVectorOfUInt8, VREG_KIND(reg), nullptr, nullptr, nullptr, \
  }

// Defines S and D pseudo registers mapping over corresponding vector register
#define DEFINE_FPU_PSEUDO(reg, size, offset, vreg)                             \
  {                                                                            \
    #reg, nullptr, size, FPU_OFFSET(fpu_##vreg - fpu_v0) + offset,             \
        lldb::eEncodingIEEE754, lldb::eFormatFloat, LLDB_KIND(fpu_##reg),      \
        g_contained_##vreg, g_##reg##_invalidates, nullptr,                    \
  }

// Defines miscellaneous status and control registers like cpsr, fpsr etc
#define DEFINE_MISC_REGS(reg, size, TYPE, lldb_kind)                           \
  {                                                                            \
    #reg, nullptr, size, TYPE##_OFFSET_NAME(reg), lldb::eEncodingUint,         \
        lldb::eFormatHex, MISC_##TYPE##_KIND(lldb_kind), nullptr, nullptr,     \
        nullptr,                                                               \
  }

// Defines pointer authentication mask registers
#define DEFINE_EXTENSION_REG(reg)                                              \
  {                                                                            \
    #reg, nullptr, 8, 0, lldb::eEncodingUint, lldb::eFormatHex,                \
        KIND_ALL_INVALID, nullptr, nullptr, nullptr,                           \
  }

static lldb_private::RegisterInfo g_register_infos_arm64_le[] = {
    // DEFINE_GPR64(name, GENERIC KIND)
    DEFINE_GPR64(x0, LLDB_REGNUM_GENERIC_ARG1),
    DEFINE_GPR64(x1, LLDB_REGNUM_GENERIC_ARG2),
    DEFINE_GPR64(x2, LLDB_REGNUM_GENERIC_ARG3),
    DEFINE_GPR64(x3, LLDB_REGNUM_GENERIC_ARG4),
    DEFINE_GPR64(x4, LLDB_REGNUM_GENERIC_ARG5),
    DEFINE_GPR64(x5, LLDB_REGNUM_GENERIC_ARG6),
    DEFINE_GPR64(x6, LLDB_REGNUM_GENERIC_ARG7),
    DEFINE_GPR64(x7, LLDB_REGNUM_GENERIC_ARG8),
    DEFINE_GPR64(x8, LLDB_INVALID_REGNUM),
    DEFINE_GPR64(x9, LLDB_INVALID_REGNUM),
    DEFINE_GPR64(x10, LLDB_INVALID_REGNUM),
    DEFINE_GPR64(x11, LLDB_INVALID_REGNUM),
    DEFINE_GPR64(x12, LLDB_INVALID_REGNUM),
    DEFINE_GPR64(x13, LLDB_INVALID_REGNUM),
    DEFINE_GPR64(x14, LLDB_INVALID_REGNUM),
    DEFINE_GPR64(x15, LLDB_INVALID_REGNUM),
    DEFINE_GPR64(x16, LLDB_INVALID_REGNUM),
    DEFINE_GPR64(x17, LLDB_INVALID_REGNUM),
    DEFINE_GPR64(x18, LLDB_INVALID_REGNUM),
    DEFINE_GPR64(x19, LLDB_INVALID_REGNUM),
    DEFINE_GPR64(x20, LLDB_INVALID_REGNUM),
    DEFINE_GPR64(x21, LLDB_INVALID_REGNUM),
    DEFINE_GPR64(x22, LLDB_INVALID_REGNUM),
    DEFINE_GPR64(x23, LLDB_INVALID_REGNUM),
    DEFINE_GPR64(x24, LLDB_INVALID_REGNUM),
    DEFINE_GPR64(x25, LLDB_INVALID_REGNUM),
    DEFINE_GPR64(x26, LLDB_INVALID_REGNUM),
    DEFINE_GPR64(x27, LLDB_INVALID_REGNUM),
    DEFINE_GPR64(x28, LLDB_INVALID_REGNUM),
    // DEFINE_GPR64(name, GENERIC KIND)
    DEFINE_GPR64_ALT(fp, x29, LLDB_REGNUM_GENERIC_FP),
    DEFINE_GPR64_ALT(lr, x30, LLDB_REGNUM_GENERIC_RA),
    DEFINE_GPR64_ALT(sp, x31, LLDB_REGNUM_GENERIC_SP),
    DEFINE_GPR64(pc, LLDB_REGNUM_GENERIC_PC),

    // DEFINE_MISC_REGS(name, size, TYPE, lldb kind)
    DEFINE_MISC_REGS(cpsr, 4, GPR, gpr_cpsr),

    // DEFINE_GPR32(name, parent name)
    DEFINE_GPR32(w0, x0),
    DEFINE_GPR32(w1, x1),
    DEFINE_GPR32(w2, x2),
    DEFINE_GPR32(w3, x3),
    DEFINE_GPR32(w4, x4),
    DEFINE_GPR32(w5, x5),
    DEFINE_GPR32(w6, x6),
    DEFINE_GPR32(w7, x7),
    DEFINE_GPR32(w8, x8),
    DEFINE_GPR32(w9, x9),
    DEFINE_GPR32(w10, x10),
    DEFINE_GPR32(w11, x11),
    DEFINE_GPR32(w12, x12),
    DEFINE_GPR32(w13, x13),
    DEFINE_GPR32(w14, x14),
    DEFINE_GPR32(w15, x15),
    DEFINE_GPR32(w16, x16),
    DEFINE_GPR32(w17, x17),
    DEFINE_GPR32(w18, x18),
    DEFINE_GPR32(w19, x19),
    DEFINE_GPR32(w20, x20),
    DEFINE_GPR32(w21, x21),
    DEFINE_GPR32(w22, x22),
    DEFINE_GPR32(w23, x23),
    DEFINE_GPR32(w24, x24),
    DEFINE_GPR32(w25, x25),
    DEFINE_GPR32(w26, x26),
    DEFINE_GPR32(w27, x27),
    DEFINE_GPR32(w28, x28),

    // DEFINE_VREG(name)
    DEFINE_VREG(v0),
    DEFINE_VREG(v1),
    DEFINE_VREG(v2),
    DEFINE_VREG(v3),
    DEFINE_VREG(v4),
    DEFINE_VREG(v5),
    DEFINE_VREG(v6),
    DEFINE_VREG(v7),
    DEFINE_VREG(v8),
    DEFINE_VREG(v9),
    DEFINE_VREG(v10),
    DEFINE_VREG(v11),
    DEFINE_VREG(v12),
    DEFINE_VREG(v13),
    DEFINE_VREG(v14),
    DEFINE_VREG(v15),
    DEFINE_VREG(v16),
    DEFINE_VREG(v17),
    DEFINE_VREG(v18),
    DEFINE_VREG(v19),
    DEFINE_VREG(v20),
    DEFINE_VREG(v21),
    DEFINE_VREG(v22),
    DEFINE_VREG(v23),
    DEFINE_VREG(v24),
    DEFINE_VREG(v25),
    DEFINE_VREG(v26),
    DEFINE_VREG(v27),
    DEFINE_VREG(v28),
    DEFINE_VREG(v29),
    DEFINE_VREG(v30),
    DEFINE_VREG(v31),

    // DEFINE_FPU_PSEUDO(name, size, ENDIAN OFFSET, parent register)
    DEFINE_FPU_PSEUDO(s0, 4, FPU_S_PSEUDO_REG_ENDIAN_OFFSET, v0),
    DEFINE_FPU_PSEUDO(s1, 4, FPU_S_PSEUDO_REG_ENDIAN_OFFSET, v1),
    DEFINE_FPU_PSEUDO(s2, 4, FPU_S_PSEUDO_REG_ENDIAN_OFFSET, v2),
    DEFINE_FPU_PSEUDO(s3, 4, FPU_S_PSEUDO_REG_ENDIAN_OFFSET, v3),
    DEFINE_FPU_PSEUDO(s4, 4, FPU_S_PSEUDO_REG_ENDIAN_OFFSET, v4),
    DEFINE_FPU_PSEUDO(s5, 4, FPU_S_PSEUDO_REG_ENDIAN_OFFSET, v5),
    DEFINE_FPU_PSEUDO(s6, 4, FPU_S_PSEUDO_REG_ENDIAN_OFFSET, v6),
    DEFINE_FPU_PSEUDO(s7, 4, FPU_S_PSEUDO_REG_ENDIAN_OFFSET, v7),
    DEFINE_FPU_PSEUDO(s8, 4, FPU_S_PSEUDO_REG_ENDIAN_OFFSET, v8),
    DEFINE_FPU_PSEUDO(s9, 4, FPU_S_PSEUDO_REG_ENDIAN_OFFSET, v9),
    DEFINE_FPU_PSEUDO(s10, 4, FPU_S_PSEUDO_REG_ENDIAN_OFFSET, v10),
    DEFINE_FPU_PSEUDO(s11, 4, FPU_S_PSEUDO_REG_ENDIAN_OFFSET, v11),
    DEFINE_FPU_PSEUDO(s12, 4, FPU_S_PSEUDO_REG_ENDIAN_OFFSET, v12),
    DEFINE_FPU_PSEUDO(s13, 4, FPU_S_PSEUDO_REG_ENDIAN_OFFSET, v13),
    DEFINE_FPU_PSEUDO(s14, 4, FPU_S_PSEUDO_REG_ENDIAN_OFFSET, v14),
    DEFINE_FPU_PSEUDO(s15, 4, FPU_S_PSEUDO_REG_ENDIAN_OFFSET, v15),
    DEFINE_FPU_PSEUDO(s16, 4, FPU_S_PSEUDO_REG_ENDIAN_OFFSET, v16),
    DEFINE_FPU_PSEUDO(s17, 4, FPU_S_PSEUDO_REG_ENDIAN_OFFSET, v17),
    DEFINE_FPU_PSEUDO(s18, 4, FPU_S_PSEUDO_REG_ENDIAN_OFFSET, v18),
    DEFINE_FPU_PSEUDO(s19, 4, FPU_S_PSEUDO_REG_ENDIAN_OFFSET, v19),
    DEFINE_FPU_PSEUDO(s20, 4, FPU_S_PSEUDO_REG_ENDIAN_OFFSET, v20),
    DEFINE_FPU_PSEUDO(s21, 4, FPU_S_PSEUDO_REG_ENDIAN_OFFSET, v21),
    DEFINE_FPU_PSEUDO(s22, 4, FPU_S_PSEUDO_REG_ENDIAN_OFFSET, v22),
    DEFINE_FPU_PSEUDO(s23, 4, FPU_S_PSEUDO_REG_ENDIAN_OFFSET, v23),
    DEFINE_FPU_PSEUDO(s24, 4, FPU_S_PSEUDO_REG_ENDIAN_OFFSET, v24),
    DEFINE_FPU_PSEUDO(s25, 4, FPU_S_PSEUDO_REG_ENDIAN_OFFSET, v25),
    DEFINE_FPU_PSEUDO(s26, 4, FPU_S_PSEUDO_REG_ENDIAN_OFFSET, v26),
    DEFINE_FPU_PSEUDO(s27, 4, FPU_S_PSEUDO_REG_ENDIAN_OFFSET, v27),
    DEFINE_FPU_PSEUDO(s28, 4, FPU_S_PSEUDO_REG_ENDIAN_OFFSET, v28),
    DEFINE_FPU_PSEUDO(s29, 4, FPU_S_PSEUDO_REG_ENDIAN_OFFSET, v29),
    DEFINE_FPU_PSEUDO(s30, 4, FPU_S_PSEUDO_REG_ENDIAN_OFFSET, v30),
    DEFINE_FPU_PSEUDO(s31, 4, FPU_S_PSEUDO_REG_ENDIAN_OFFSET, v31),

    DEFINE_FPU_PSEUDO(d0, 8, FPU_D_PSEUDO_REG_ENDIAN_OFFSET, v0),
    DEFINE_FPU_PSEUDO(d1, 8, FPU_D_PSEUDO_REG_ENDIAN_OFFSET, v1),
    DEFINE_FPU_PSEUDO(d2, 8, FPU_D_PSEUDO_REG_ENDIAN_OFFSET, v2),
    DEFINE_FPU_PSEUDO(d3, 8, FPU_D_PSEUDO_REG_ENDIAN_OFFSET, v3),
    DEFINE_FPU_PSEUDO(d4, 8, FPU_D_PSEUDO_REG_ENDIAN_OFFSET, v4),
    DEFINE_FPU_PSEUDO(d5, 8, FPU_D_PSEUDO_REG_ENDIAN_OFFSET, v5),
    DEFINE_FPU_PSEUDO(d6, 8, FPU_D_PSEUDO_REG_ENDIAN_OFFSET, v6),
    DEFINE_FPU_PSEUDO(d7, 8, FPU_D_PSEUDO_REG_ENDIAN_OFFSET, v7),
    DEFINE_FPU_PSEUDO(d8, 8, FPU_D_PSEUDO_REG_ENDIAN_OFFSET, v8),
    DEFINE_FPU_PSEUDO(d9, 8, FPU_D_PSEUDO_REG_ENDIAN_OFFSET, v9),
    DEFINE_FPU_PSEUDO(d10, 8, FPU_D_PSEUDO_REG_ENDIAN_OFFSET, v10),
    DEFINE_FPU_PSEUDO(d11, 8, FPU_D_PSEUDO_REG_ENDIAN_OFFSET, v11),
    DEFINE_FPU_PSEUDO(d12, 8, FPU_D_PSEUDO_REG_ENDIAN_OFFSET, v12),
    DEFINE_FPU_PSEUDO(d13, 8, FPU_D_PSEUDO_REG_ENDIAN_OFFSET, v13),
    DEFINE_FPU_PSEUDO(d14, 8, FPU_D_PSEUDO_REG_ENDIAN_OFFSET, v14),
    DEFINE_FPU_PSEUDO(d15, 8, FPU_D_PSEUDO_REG_ENDIAN_OFFSET, v15),
    DEFINE_FPU_PSEUDO(d16, 8, FPU_D_PSEUDO_REG_ENDIAN_OFFSET, v16),
    DEFINE_FPU_PSEUDO(d17, 8, FPU_D_PSEUDO_REG_ENDIAN_OFFSET, v17),
    DEFINE_FPU_PSEUDO(d18, 8, FPU_D_PSEUDO_REG_ENDIAN_OFFSET, v18),
    DEFINE_FPU_PSEUDO(d19, 8, FPU_D_PSEUDO_REG_ENDIAN_OFFSET, v19),
    DEFINE_FPU_PSEUDO(d20, 8, FPU_D_PSEUDO_REG_ENDIAN_OFFSET, v20),
    DEFINE_FPU_PSEUDO(d21, 8, FPU_D_PSEUDO_REG_ENDIAN_OFFSET, v21),
    DEFINE_FPU_PSEUDO(d22, 8, FPU_D_PSEUDO_REG_ENDIAN_OFFSET, v22),
    DEFINE_FPU_PSEUDO(d23, 8, FPU_D_PSEUDO_REG_ENDIAN_OFFSET, v23),
    DEFINE_FPU_PSEUDO(d24, 8, FPU_D_PSEUDO_REG_ENDIAN_OFFSET, v24),
    DEFINE_FPU_PSEUDO(d25, 8, FPU_D_PSEUDO_REG_ENDIAN_OFFSET, v25),
    DEFINE_FPU_PSEUDO(d26, 8, FPU_D_PSEUDO_REG_ENDIAN_OFFSET, v26),
    DEFINE_FPU_PSEUDO(d27, 8, FPU_D_PSEUDO_REG_ENDIAN_OFFSET, v27),
    DEFINE_FPU_PSEUDO(d28, 8, FPU_D_PSEUDO_REG_ENDIAN_OFFSET, v28),
    DEFINE_FPU_PSEUDO(d29, 8, FPU_D_PSEUDO_REG_ENDIAN_OFFSET, v29),
    DEFINE_FPU_PSEUDO(d30, 8, FPU_D_PSEUDO_REG_ENDIAN_OFFSET, v30),
    DEFINE_FPU_PSEUDO(d31, 8, FPU_D_PSEUDO_REG_ENDIAN_OFFSET, v31),

    // DEFINE_MISC_REGS(name, size, TYPE, lldb kind)
    DEFINE_MISC_REGS(fpsr, 4, FPU, fpu_fpsr),
    DEFINE_MISC_REGS(fpcr, 4, FPU, fpu_fpcr),
    DEFINE_MISC_REGS(far, 8, EXC, exc_far),
    DEFINE_MISC_REGS(esr, 4, EXC, exc_esr),
    DEFINE_MISC_REGS(exception, 4, EXC, exc_exception),

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
    {DEFINE_DBG(wcr, 15)}
};
// clang-format on

#endif // DECLARE_REGISTER_INFOS_ARM64_STRUCT
