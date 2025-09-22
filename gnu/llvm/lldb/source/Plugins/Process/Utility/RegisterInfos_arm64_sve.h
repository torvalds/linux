//===-- RegisterInfos_arm64_sve.h -------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifdef DECLARE_REGISTER_INFOS_ARM64_STRUCT

enum {
  sve_vg = exc_far,

  sve_z0,
  sve_z1,
  sve_z2,
  sve_z3,
  sve_z4,
  sve_z5,
  sve_z6,
  sve_z7,
  sve_z8,
  sve_z9,
  sve_z10,
  sve_z11,
  sve_z12,
  sve_z13,
  sve_z14,
  sve_z15,
  sve_z16,
  sve_z17,
  sve_z18,
  sve_z19,
  sve_z20,
  sve_z21,
  sve_z22,
  sve_z23,
  sve_z24,
  sve_z25,
  sve_z26,
  sve_z27,
  sve_z28,
  sve_z29,
  sve_z30,
  sve_z31,

  sve_p0,
  sve_p1,
  sve_p2,
  sve_p3,
  sve_p4,
  sve_p5,
  sve_p6,
  sve_p7,
  sve_p8,
  sve_p9,
  sve_p10,
  sve_p11,
  sve_p12,
  sve_p13,
  sve_p14,
  sve_p15,

  sve_ffr,
};

#ifndef SVE_OFFSET_VG
#error SVE_OFFSET_VG must be defined before including this header file
#endif

static uint32_t g_sve_s0_invalidates[] = {sve_z0, fpu_v0, fpu_d0,
                                          LLDB_INVALID_REGNUM};
static uint32_t g_sve_s1_invalidates[] = {sve_z1, fpu_v1, fpu_d1,
                                          LLDB_INVALID_REGNUM};
static uint32_t g_sve_s2_invalidates[] = {sve_z2, fpu_v2, fpu_d2,
                                          LLDB_INVALID_REGNUM};
static uint32_t g_sve_s3_invalidates[] = {sve_z3, fpu_v3, fpu_d3,
                                          LLDB_INVALID_REGNUM};
static uint32_t g_sve_s4_invalidates[] = {sve_z4, fpu_v4, fpu_d4,
                                          LLDB_INVALID_REGNUM};
static uint32_t g_sve_s5_invalidates[] = {sve_z5, fpu_v5, fpu_d5,
                                          LLDB_INVALID_REGNUM};
static uint32_t g_sve_s6_invalidates[] = {sve_z6, fpu_v6, fpu_d6,
                                          LLDB_INVALID_REGNUM};
static uint32_t g_sve_s7_invalidates[] = {sve_z7, fpu_v7, fpu_d7,
                                          LLDB_INVALID_REGNUM};
static uint32_t g_sve_s8_invalidates[] = {sve_z8, fpu_v8, fpu_d8,
                                          LLDB_INVALID_REGNUM};
static uint32_t g_sve_s9_invalidates[] = {sve_z9, fpu_v9, fpu_d9,
                                          LLDB_INVALID_REGNUM};
static uint32_t g_sve_s10_invalidates[] = {sve_z10, fpu_v10, fpu_d10,
                                           LLDB_INVALID_REGNUM};
static uint32_t g_sve_s11_invalidates[] = {sve_z11, fpu_v11, fpu_d11,
                                           LLDB_INVALID_REGNUM};
static uint32_t g_sve_s12_invalidates[] = {sve_z12, fpu_v12, fpu_d12,
                                           LLDB_INVALID_REGNUM};
static uint32_t g_sve_s13_invalidates[] = {sve_z13, fpu_v13, fpu_d13,
                                           LLDB_INVALID_REGNUM};
static uint32_t g_sve_s14_invalidates[] = {sve_z14, fpu_v14, fpu_d14,
                                           LLDB_INVALID_REGNUM};
static uint32_t g_sve_s15_invalidates[] = {sve_z15, fpu_v15, fpu_d15,
                                           LLDB_INVALID_REGNUM};
static uint32_t g_sve_s16_invalidates[] = {sve_z16, fpu_v16, fpu_d16,
                                           LLDB_INVALID_REGNUM};
static uint32_t g_sve_s17_invalidates[] = {sve_z17, fpu_v17, fpu_d17,
                                           LLDB_INVALID_REGNUM};
static uint32_t g_sve_s18_invalidates[] = {sve_z18, fpu_v18, fpu_d18,
                                           LLDB_INVALID_REGNUM};
static uint32_t g_sve_s19_invalidates[] = {sve_z19, fpu_v19, fpu_d19,
                                           LLDB_INVALID_REGNUM};
static uint32_t g_sve_s20_invalidates[] = {sve_z20, fpu_v20, fpu_d20,
                                           LLDB_INVALID_REGNUM};
static uint32_t g_sve_s21_invalidates[] = {sve_z21, fpu_v21, fpu_d21,
                                           LLDB_INVALID_REGNUM};
static uint32_t g_sve_s22_invalidates[] = {sve_z22, fpu_v22, fpu_d22,
                                           LLDB_INVALID_REGNUM};
static uint32_t g_sve_s23_invalidates[] = {sve_z23, fpu_v23, fpu_d23,
                                           LLDB_INVALID_REGNUM};
static uint32_t g_sve_s24_invalidates[] = {sve_z24, fpu_v24, fpu_d24,
                                           LLDB_INVALID_REGNUM};
static uint32_t g_sve_s25_invalidates[] = {sve_z25, fpu_v25, fpu_d25,
                                           LLDB_INVALID_REGNUM};
static uint32_t g_sve_s26_invalidates[] = {sve_z26, fpu_v26, fpu_d26,
                                           LLDB_INVALID_REGNUM};
static uint32_t g_sve_s27_invalidates[] = {sve_z27, fpu_v27, fpu_d27,
                                           LLDB_INVALID_REGNUM};
static uint32_t g_sve_s28_invalidates[] = {sve_z28, fpu_v28, fpu_d28,
                                           LLDB_INVALID_REGNUM};
static uint32_t g_sve_s29_invalidates[] = {sve_z29, fpu_v29, fpu_d29,
                                           LLDB_INVALID_REGNUM};
static uint32_t g_sve_s30_invalidates[] = {sve_z30, fpu_v30, fpu_d30,
                                           LLDB_INVALID_REGNUM};
static uint32_t g_sve_s31_invalidates[] = {sve_z31, fpu_v31, fpu_d31,
                                           LLDB_INVALID_REGNUM};

static uint32_t g_sve_d0_invalidates[] = {sve_z0, fpu_v0, fpu_s0,
                                          LLDB_INVALID_REGNUM};
static uint32_t g_sve_d1_invalidates[] = {sve_z1, fpu_v1, fpu_s1,
                                          LLDB_INVALID_REGNUM};
static uint32_t g_sve_d2_invalidates[] = {sve_z2, fpu_v2, fpu_s2,
                                          LLDB_INVALID_REGNUM};
static uint32_t g_sve_d3_invalidates[] = {sve_z3, fpu_v3, fpu_s3,
                                          LLDB_INVALID_REGNUM};
static uint32_t g_sve_d4_invalidates[] = {sve_z4, fpu_v4, fpu_s4,
                                          LLDB_INVALID_REGNUM};
static uint32_t g_sve_d5_invalidates[] = {sve_z5, fpu_v5, fpu_s5,
                                          LLDB_INVALID_REGNUM};
static uint32_t g_sve_d6_invalidates[] = {sve_z6, fpu_v6, fpu_s6,
                                          LLDB_INVALID_REGNUM};
static uint32_t g_sve_d7_invalidates[] = {sve_z7, fpu_v7, fpu_s7,
                                          LLDB_INVALID_REGNUM};
static uint32_t g_sve_d8_invalidates[] = {sve_z8, fpu_v8, fpu_s8,
                                          LLDB_INVALID_REGNUM};
static uint32_t g_sve_d9_invalidates[] = {sve_z9, fpu_v9, fpu_s9,
                                          LLDB_INVALID_REGNUM};
static uint32_t g_sve_d10_invalidates[] = {sve_z10, fpu_v10, fpu_s10,
                                           LLDB_INVALID_REGNUM};
static uint32_t g_sve_d11_invalidates[] = {sve_z11, fpu_v11, fpu_s11,
                                           LLDB_INVALID_REGNUM};
static uint32_t g_sve_d12_invalidates[] = {sve_z12, fpu_v12, fpu_s12,
                                           LLDB_INVALID_REGNUM};
static uint32_t g_sve_d13_invalidates[] = {sve_z13, fpu_v13, fpu_s13,
                                           LLDB_INVALID_REGNUM};
static uint32_t g_sve_d14_invalidates[] = {sve_z14, fpu_v14, fpu_s14,
                                           LLDB_INVALID_REGNUM};
static uint32_t g_sve_d15_invalidates[] = {sve_z15, fpu_v15, fpu_s15,
                                           LLDB_INVALID_REGNUM};
static uint32_t g_sve_d16_invalidates[] = {sve_z16, fpu_v16, fpu_s16,
                                           LLDB_INVALID_REGNUM};
static uint32_t g_sve_d17_invalidates[] = {sve_z17, fpu_v17, fpu_s17,
                                           LLDB_INVALID_REGNUM};
static uint32_t g_sve_d18_invalidates[] = {sve_z18, fpu_v18, fpu_s18,
                                           LLDB_INVALID_REGNUM};
static uint32_t g_sve_d19_invalidates[] = {sve_z19, fpu_v19, fpu_s19,
                                           LLDB_INVALID_REGNUM};
static uint32_t g_sve_d20_invalidates[] = {sve_z20, fpu_v20, fpu_s20,
                                           LLDB_INVALID_REGNUM};
static uint32_t g_sve_d21_invalidates[] = {sve_z21, fpu_v21, fpu_s21,
                                           LLDB_INVALID_REGNUM};
static uint32_t g_sve_d22_invalidates[] = {sve_z22, fpu_v22, fpu_s22,
                                           LLDB_INVALID_REGNUM};
static uint32_t g_sve_d23_invalidates[] = {sve_z23, fpu_v23, fpu_s23,
                                           LLDB_INVALID_REGNUM};
static uint32_t g_sve_d24_invalidates[] = {sve_z24, fpu_v24, fpu_s24,
                                           LLDB_INVALID_REGNUM};
static uint32_t g_sve_d25_invalidates[] = {sve_z25, fpu_v25, fpu_s25,
                                           LLDB_INVALID_REGNUM};
static uint32_t g_sve_d26_invalidates[] = {sve_z26, fpu_v26, fpu_s26,
                                           LLDB_INVALID_REGNUM};
static uint32_t g_sve_d27_invalidates[] = {sve_z27, fpu_v27, fpu_s27,
                                           LLDB_INVALID_REGNUM};
static uint32_t g_sve_d28_invalidates[] = {sve_z28, fpu_v28, fpu_s28,
                                           LLDB_INVALID_REGNUM};
static uint32_t g_sve_d29_invalidates[] = {sve_z29, fpu_v29, fpu_s29,
                                           LLDB_INVALID_REGNUM};
static uint32_t g_sve_d30_invalidates[] = {sve_z30, fpu_v30, fpu_s30,
                                           LLDB_INVALID_REGNUM};
static uint32_t g_sve_d31_invalidates[] = {sve_z31, fpu_v31, fpu_s31,
                                           LLDB_INVALID_REGNUM};

static uint32_t g_sve_v0_invalidates[] = {sve_z0, fpu_d0, fpu_s0,
                                          LLDB_INVALID_REGNUM};
static uint32_t g_sve_v1_invalidates[] = {sve_z1, fpu_d1, fpu_s1,
                                          LLDB_INVALID_REGNUM};
static uint32_t g_sve_v2_invalidates[] = {sve_z2, fpu_d2, fpu_s2,
                                          LLDB_INVALID_REGNUM};
static uint32_t g_sve_v3_invalidates[] = {sve_z3, fpu_d3, fpu_s3,
                                          LLDB_INVALID_REGNUM};
static uint32_t g_sve_v4_invalidates[] = {sve_z4, fpu_d4, fpu_s4,
                                          LLDB_INVALID_REGNUM};
static uint32_t g_sve_v5_invalidates[] = {sve_z5, fpu_d5, fpu_s5,
                                          LLDB_INVALID_REGNUM};
static uint32_t g_sve_v6_invalidates[] = {sve_z6, fpu_d6, fpu_s6,
                                          LLDB_INVALID_REGNUM};
static uint32_t g_sve_v7_invalidates[] = {sve_z7, fpu_d7, fpu_s7,
                                          LLDB_INVALID_REGNUM};
static uint32_t g_sve_v8_invalidates[] = {sve_z8, fpu_d8, fpu_s8,
                                          LLDB_INVALID_REGNUM};
static uint32_t g_sve_v9_invalidates[] = {sve_z9, fpu_d9, fpu_s9,
                                          LLDB_INVALID_REGNUM};
static uint32_t g_sve_v10_invalidates[] = {sve_z10, fpu_d10, fpu_s10,
                                           LLDB_INVALID_REGNUM};
static uint32_t g_sve_v11_invalidates[] = {sve_z11, fpu_d11, fpu_s11,
                                           LLDB_INVALID_REGNUM};
static uint32_t g_sve_v12_invalidates[] = {sve_z12, fpu_d12, fpu_s12,
                                           LLDB_INVALID_REGNUM};
static uint32_t g_sve_v13_invalidates[] = {sve_z13, fpu_d13, fpu_s13,
                                           LLDB_INVALID_REGNUM};
static uint32_t g_sve_v14_invalidates[] = {sve_z14, fpu_d14, fpu_s14,
                                           LLDB_INVALID_REGNUM};
static uint32_t g_sve_v15_invalidates[] = {sve_z15, fpu_d15, fpu_s15,
                                           LLDB_INVALID_REGNUM};
static uint32_t g_sve_v16_invalidates[] = {sve_z16, fpu_d16, fpu_s16,
                                           LLDB_INVALID_REGNUM};
static uint32_t g_sve_v17_invalidates[] = {sve_z17, fpu_d17, fpu_s17,
                                           LLDB_INVALID_REGNUM};
static uint32_t g_sve_v18_invalidates[] = {sve_z18, fpu_d18, fpu_s18,
                                           LLDB_INVALID_REGNUM};
static uint32_t g_sve_v19_invalidates[] = {sve_z19, fpu_d19, fpu_s19,
                                           LLDB_INVALID_REGNUM};
static uint32_t g_sve_v20_invalidates[] = {sve_z20, fpu_d20, fpu_s20,
                                           LLDB_INVALID_REGNUM};
static uint32_t g_sve_v21_invalidates[] = {sve_z21, fpu_d21, fpu_s21,
                                           LLDB_INVALID_REGNUM};
static uint32_t g_sve_v22_invalidates[] = {sve_z22, fpu_d22, fpu_s22,
                                           LLDB_INVALID_REGNUM};
static uint32_t g_sve_v23_invalidates[] = {sve_z23, fpu_d23, fpu_s23,
                                           LLDB_INVALID_REGNUM};
static uint32_t g_sve_v24_invalidates[] = {sve_z24, fpu_d24, fpu_s24,
                                           LLDB_INVALID_REGNUM};
static uint32_t g_sve_v25_invalidates[] = {sve_z25, fpu_d25, fpu_s25,
                                           LLDB_INVALID_REGNUM};
static uint32_t g_sve_v26_invalidates[] = {sve_z26, fpu_d26, fpu_s26,
                                           LLDB_INVALID_REGNUM};
static uint32_t g_sve_v27_invalidates[] = {sve_z27, fpu_d27, fpu_s27,
                                           LLDB_INVALID_REGNUM};
static uint32_t g_sve_v28_invalidates[] = {sve_z28, fpu_d28, fpu_s28,
                                           LLDB_INVALID_REGNUM};
static uint32_t g_sve_v29_invalidates[] = {sve_z29, fpu_d29, fpu_s29,
                                           LLDB_INVALID_REGNUM};
static uint32_t g_sve_v30_invalidates[] = {sve_z30, fpu_d30, fpu_s30,
                                           LLDB_INVALID_REGNUM};
static uint32_t g_sve_v31_invalidates[] = {sve_z31, fpu_d31, fpu_s31,
                                           LLDB_INVALID_REGNUM};

static uint32_t g_contained_z0[] = {sve_z0, LLDB_INVALID_REGNUM};
static uint32_t g_contained_z1[] = {sve_z1, LLDB_INVALID_REGNUM};
static uint32_t g_contained_z2[] = {sve_z2, LLDB_INVALID_REGNUM};
static uint32_t g_contained_z3[] = {sve_z3, LLDB_INVALID_REGNUM};
static uint32_t g_contained_z4[] = {sve_z4, LLDB_INVALID_REGNUM};
static uint32_t g_contained_z5[] = {sve_z5, LLDB_INVALID_REGNUM};
static uint32_t g_contained_z6[] = {sve_z6, LLDB_INVALID_REGNUM};
static uint32_t g_contained_z7[] = {sve_z7, LLDB_INVALID_REGNUM};
static uint32_t g_contained_z8[] = {sve_z8, LLDB_INVALID_REGNUM};
static uint32_t g_contained_z9[] = {sve_z9, LLDB_INVALID_REGNUM};
static uint32_t g_contained_z10[] = {sve_z10, LLDB_INVALID_REGNUM};
static uint32_t g_contained_z11[] = {sve_z11, LLDB_INVALID_REGNUM};
static uint32_t g_contained_z12[] = {sve_z12, LLDB_INVALID_REGNUM};
static uint32_t g_contained_z13[] = {sve_z13, LLDB_INVALID_REGNUM};
static uint32_t g_contained_z14[] = {sve_z14, LLDB_INVALID_REGNUM};
static uint32_t g_contained_z15[] = {sve_z15, LLDB_INVALID_REGNUM};
static uint32_t g_contained_z16[] = {sve_z16, LLDB_INVALID_REGNUM};
static uint32_t g_contained_z17[] = {sve_z17, LLDB_INVALID_REGNUM};
static uint32_t g_contained_z18[] = {sve_z18, LLDB_INVALID_REGNUM};
static uint32_t g_contained_z19[] = {sve_z19, LLDB_INVALID_REGNUM};
static uint32_t g_contained_z20[] = {sve_z20, LLDB_INVALID_REGNUM};
static uint32_t g_contained_z21[] = {sve_z21, LLDB_INVALID_REGNUM};
static uint32_t g_contained_z22[] = {sve_z22, LLDB_INVALID_REGNUM};
static uint32_t g_contained_z23[] = {sve_z23, LLDB_INVALID_REGNUM};
static uint32_t g_contained_z24[] = {sve_z24, LLDB_INVALID_REGNUM};
static uint32_t g_contained_z25[] = {sve_z25, LLDB_INVALID_REGNUM};
static uint32_t g_contained_z26[] = {sve_z26, LLDB_INVALID_REGNUM};
static uint32_t g_contained_z27[] = {sve_z27, LLDB_INVALID_REGNUM};
static uint32_t g_contained_z28[] = {sve_z28, LLDB_INVALID_REGNUM};
static uint32_t g_contained_z29[] = {sve_z29, LLDB_INVALID_REGNUM};
static uint32_t g_contained_z30[] = {sve_z30, LLDB_INVALID_REGNUM};
static uint32_t g_contained_z31[] = {sve_z31, LLDB_INVALID_REGNUM};

#define VG_OFFSET_NAME(reg) SVE_OFFSET_VG

#define SVE_REG_KIND(reg) MISC_KIND(reg, sve, LLDB_INVALID_REGNUM)
#define MISC_VG_KIND(lldb_kind) MISC_KIND(vg, sve, LLDB_INVALID_REGNUM)

// Default offset SVE Z registers and all corresponding pseudo registers
// ( S, D and V registers) is zero and will be configured during execution.

// clang-format off

// Defines sve pseudo vector (V) register with 16-byte size
#define DEFINE_VREG_SVE(vreg, zreg)                                            \
  {                                                                            \
    #vreg, nullptr, 16, 0, lldb::eEncodingVector, lldb::eFormatVectorOfUInt8,  \
        VREG_KIND(vreg), g_contained_##zreg, g_sve_##vreg##_invalidates,       \
        nullptr,                                                               \
  }

// Defines S and D pseudo registers mapping over corresponding vector register
#define DEFINE_FPU_PSEUDO_SVE(reg, size, zreg)                                 \
  {                                                                            \
    #reg, nullptr, size, 0, lldb::eEncodingIEEE754, lldb::eFormatFloat,        \
        LLDB_KIND(fpu_##reg), g_contained_##zreg, g_sve_##reg##_invalidates,   \
        nullptr,                                                               \
  }

// Defines a Z vector register with 16-byte default size
#define DEFINE_ZREG(reg)                                                       \
  {                                                                            \
    #reg, nullptr, 16, 0, lldb::eEncodingVector, lldb::eFormatVectorOfUInt8,   \
        SVE_REG_KIND(reg), nullptr, nullptr, nullptr,                          \
  }

// Defines a P vector register with 2-byte default size
#define DEFINE_PREG(reg)                                                       \
  {                                                                            \
    #reg, nullptr, 2, 0, lldb::eEncodingVector, lldb::eFormatVectorOfUInt8,    \
        SVE_REG_KIND(reg), nullptr, nullptr, nullptr,                          \
  }

static lldb_private::RegisterInfo g_register_infos_arm64_sve_le[] = {
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

    // DEFINE_VREG_SVE(v register, z register)
    DEFINE_VREG_SVE(v0, z0),
    DEFINE_VREG_SVE(v1, z1),
    DEFINE_VREG_SVE(v2, z2),
    DEFINE_VREG_SVE(v3, z3),
    DEFINE_VREG_SVE(v4, z4),
    DEFINE_VREG_SVE(v5, z5),
    DEFINE_VREG_SVE(v6, z6),
    DEFINE_VREG_SVE(v7, z7),
    DEFINE_VREG_SVE(v8, z8),
    DEFINE_VREG_SVE(v9, z9),
    DEFINE_VREG_SVE(v10, z10),
    DEFINE_VREG_SVE(v11, z11),
    DEFINE_VREG_SVE(v12, z12),
    DEFINE_VREG_SVE(v13, z13),
    DEFINE_VREG_SVE(v14, z14),
    DEFINE_VREG_SVE(v15, z15),
    DEFINE_VREG_SVE(v16, z16),
    DEFINE_VREG_SVE(v17, z17),
    DEFINE_VREG_SVE(v18, z18),
    DEFINE_VREG_SVE(v19, z19),
    DEFINE_VREG_SVE(v20, z20),
    DEFINE_VREG_SVE(v21, z21),
    DEFINE_VREG_SVE(v22, z22),
    DEFINE_VREG_SVE(v23, z23),
    DEFINE_VREG_SVE(v24, z24),
    DEFINE_VREG_SVE(v25, z25),
    DEFINE_VREG_SVE(v26, z26),
    DEFINE_VREG_SVE(v27, z27),
    DEFINE_VREG_SVE(v28, z28),
    DEFINE_VREG_SVE(v29, z29),
    DEFINE_VREG_SVE(v30, z30),
    DEFINE_VREG_SVE(v31, z31),

    // DEFINE_FPU_PSEUDO(name, size, ENDIAN OFFSET, parent register)
    DEFINE_FPU_PSEUDO_SVE(s0, 4, z0),
    DEFINE_FPU_PSEUDO_SVE(s1, 4, z1),
    DEFINE_FPU_PSEUDO_SVE(s2, 4, z2),
    DEFINE_FPU_PSEUDO_SVE(s3, 4, z3),
    DEFINE_FPU_PSEUDO_SVE(s4, 4, z4),
    DEFINE_FPU_PSEUDO_SVE(s5, 4, z5),
    DEFINE_FPU_PSEUDO_SVE(s6, 4, z6),
    DEFINE_FPU_PSEUDO_SVE(s7, 4, z7),
    DEFINE_FPU_PSEUDO_SVE(s8, 4, z8),
    DEFINE_FPU_PSEUDO_SVE(s9, 4, z9),
    DEFINE_FPU_PSEUDO_SVE(s10, 4, z10),
    DEFINE_FPU_PSEUDO_SVE(s11, 4, z11),
    DEFINE_FPU_PSEUDO_SVE(s12, 4, z12),
    DEFINE_FPU_PSEUDO_SVE(s13, 4, z13),
    DEFINE_FPU_PSEUDO_SVE(s14, 4, z14),
    DEFINE_FPU_PSEUDO_SVE(s15, 4, z15),
    DEFINE_FPU_PSEUDO_SVE(s16, 4, z16),
    DEFINE_FPU_PSEUDO_SVE(s17, 4, z17),
    DEFINE_FPU_PSEUDO_SVE(s18, 4, z18),
    DEFINE_FPU_PSEUDO_SVE(s19, 4, z19),
    DEFINE_FPU_PSEUDO_SVE(s20, 4, z20),
    DEFINE_FPU_PSEUDO_SVE(s21, 4, z21),
    DEFINE_FPU_PSEUDO_SVE(s22, 4, z22),
    DEFINE_FPU_PSEUDO_SVE(s23, 4, z23),
    DEFINE_FPU_PSEUDO_SVE(s24, 4, z24),
    DEFINE_FPU_PSEUDO_SVE(s25, 4, z25),
    DEFINE_FPU_PSEUDO_SVE(s26, 4, z26),
    DEFINE_FPU_PSEUDO_SVE(s27, 4, z27),
    DEFINE_FPU_PSEUDO_SVE(s28, 4, z28),
    DEFINE_FPU_PSEUDO_SVE(s29, 4, z29),
    DEFINE_FPU_PSEUDO_SVE(s30, 4, z30),
    DEFINE_FPU_PSEUDO_SVE(s31, 4, z31),

    DEFINE_FPU_PSEUDO_SVE(d0, 8, z0),
    DEFINE_FPU_PSEUDO_SVE(d1, 8, z1),
    DEFINE_FPU_PSEUDO_SVE(d2, 8, z2),
    DEFINE_FPU_PSEUDO_SVE(d3, 8, z3),
    DEFINE_FPU_PSEUDO_SVE(d4, 8, z4),
    DEFINE_FPU_PSEUDO_SVE(d5, 8, z5),
    DEFINE_FPU_PSEUDO_SVE(d6, 8, z6),
    DEFINE_FPU_PSEUDO_SVE(d7, 8, z7),
    DEFINE_FPU_PSEUDO_SVE(d8, 8, z8),
    DEFINE_FPU_PSEUDO_SVE(d9, 8, z9),
    DEFINE_FPU_PSEUDO_SVE(d10, 8, z10),
    DEFINE_FPU_PSEUDO_SVE(d11, 8, z11),
    DEFINE_FPU_PSEUDO_SVE(d12, 8, z12),
    DEFINE_FPU_PSEUDO_SVE(d13, 8, z13),
    DEFINE_FPU_PSEUDO_SVE(d14, 8, z14),
    DEFINE_FPU_PSEUDO_SVE(d15, 8, z15),
    DEFINE_FPU_PSEUDO_SVE(d16, 8, z16),
    DEFINE_FPU_PSEUDO_SVE(d17, 8, z17),
    DEFINE_FPU_PSEUDO_SVE(d18, 8, z18),
    DEFINE_FPU_PSEUDO_SVE(d19, 8, z19),
    DEFINE_FPU_PSEUDO_SVE(d20, 8, z20),
    DEFINE_FPU_PSEUDO_SVE(d21, 8, z21),
    DEFINE_FPU_PSEUDO_SVE(d22, 8, z22),
    DEFINE_FPU_PSEUDO_SVE(d23, 8, z23),
    DEFINE_FPU_PSEUDO_SVE(d24, 8, z24),
    DEFINE_FPU_PSEUDO_SVE(d25, 8, z25),
    DEFINE_FPU_PSEUDO_SVE(d26, 8, z26),
    DEFINE_FPU_PSEUDO_SVE(d27, 8, z27),
    DEFINE_FPU_PSEUDO_SVE(d28, 8, z28),
    DEFINE_FPU_PSEUDO_SVE(d29, 8, z29),
    DEFINE_FPU_PSEUDO_SVE(d30, 8, z30),
    DEFINE_FPU_PSEUDO_SVE(d31, 8, z31),

    // DEFINE_MISC_REGS(name, size, TYPE, lldb kind)
    DEFINE_MISC_REGS(fpsr, 4, FPU, fpu_fpsr),
    DEFINE_MISC_REGS(fpcr, 4, FPU, fpu_fpcr),

    DEFINE_MISC_REGS(vg, 8, VG, sve_vg),
    // DEFINE_ZREG(name)
    DEFINE_ZREG(z0),
    DEFINE_ZREG(z1),
    DEFINE_ZREG(z2),
    DEFINE_ZREG(z3),
    DEFINE_ZREG(z4),
    DEFINE_ZREG(z5),
    DEFINE_ZREG(z6),
    DEFINE_ZREG(z7),
    DEFINE_ZREG(z8),
    DEFINE_ZREG(z9),
    DEFINE_ZREG(z10),
    DEFINE_ZREG(z11),
    DEFINE_ZREG(z12),
    DEFINE_ZREG(z13),
    DEFINE_ZREG(z14),
    DEFINE_ZREG(z15),
    DEFINE_ZREG(z16),
    DEFINE_ZREG(z17),
    DEFINE_ZREG(z18),
    DEFINE_ZREG(z19),
    DEFINE_ZREG(z20),
    DEFINE_ZREG(z21),
    DEFINE_ZREG(z22),
    DEFINE_ZREG(z23),
    DEFINE_ZREG(z24),
    DEFINE_ZREG(z25),
    DEFINE_ZREG(z26),
    DEFINE_ZREG(z27),
    DEFINE_ZREG(z28),
    DEFINE_ZREG(z29),
    DEFINE_ZREG(z30),
    DEFINE_ZREG(z31),

    // DEFINE_PREG(name)
    DEFINE_PREG(p0),
    DEFINE_PREG(p1),
    DEFINE_PREG(p2),
    DEFINE_PREG(p3),
    DEFINE_PREG(p4),
    DEFINE_PREG(p5),
    DEFINE_PREG(p6),
    DEFINE_PREG(p7),
    DEFINE_PREG(p8),
    DEFINE_PREG(p9),
    DEFINE_PREG(p10),
    DEFINE_PREG(p11),
    DEFINE_PREG(p12),
    DEFINE_PREG(p13),
    DEFINE_PREG(p14),
    DEFINE_PREG(p15),

    // DEFINE FFR
    DEFINE_PREG(ffr)
    // clang-format on
};

#endif // DECLARE_REGISTER_INFOS_ARM64_SVE_STRUCT
