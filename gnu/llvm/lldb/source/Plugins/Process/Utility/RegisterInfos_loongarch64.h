//===-- RegisterInfos_loongarch64.h -----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifdef DECLARE_REGISTER_INFOS_LOONGARCH64_STRUCT

#include <stddef.h>

#include "lldb/lldb-defines.h"
#include "lldb/lldb-enumerations.h"
#include "lldb/lldb-private.h"

#include "Utility/LoongArch_DWARF_Registers.h"
#include "lldb-loongarch-register-enums.h"

#ifndef GPR_OFFSET
#error GPR_OFFSET must be defined before including this header file
#endif

#ifndef FPR_OFFSET
#error FPR_OFFSET must be defined before including this header file
#endif

using namespace loongarch_dwarf;

// clang-format off

// I suppose EHFrame and DWARF are the same.
#define KIND_HELPER(reg, generic_kind)                                         \
  {                                                                            \
    loongarch_dwarf::dwarf_##reg, loongarch_dwarf::dwarf_##reg, generic_kind,  \
    LLDB_INVALID_REGNUM, reg##_loongarch                                       \
  }

// Generates register kinds array for generic purpose registers
#define GPR64_KIND(reg, generic_kind) KIND_HELPER(reg, generic_kind)

// Generates register kinds array for floating point registers
#define FPR64_KIND(reg, generic_kind) KIND_HELPER(reg, generic_kind)

// Defines a 64-bit general purpose register
#define DEFINE_GPR64(reg, generic_kind) DEFINE_GPR64_ALT(reg, reg, generic_kind)
#define DEFINE_GPR64_ALT(reg, alt, generic_kind)                               \
  {                                                                            \
    #reg, #alt, 8, GPR_OFFSET(gpr_##reg##_loongarch - gpr_first_loongarch),    \
    lldb::eEncodingUint, lldb::eFormatHex,                                     \
    GPR64_KIND(gpr_##reg, generic_kind), nullptr, nullptr, nullptr,            \
  }

// Defines a 64-bit floating point register
#define DEFINE_FPR64(reg, generic_kind) DEFINE_FPR64_ALT(reg, reg, generic_kind)
#define DEFINE_FPR64_ALT(reg, alt, generic_kind)                               \
  {                                                                            \
    #reg, #alt, 8, FPR_OFFSET(fpr_##reg##_loongarch - fpr_first_loongarch),    \
    lldb::eEncodingUint, lldb::eFormatHex,                                     \
    FPR64_KIND(fpr_##reg, generic_kind), nullptr, nullptr, nullptr,            \
  }

#define DEFINE_FCC(reg, generic_kind)                                          \
  {                                                                            \
    #reg, nullptr, 1, FCC_OFFSET(fpr_##reg##_loongarch - fpr_fcc0_loongarch),  \
    lldb::eEncodingUint, lldb::eFormatHex,                                     \
    FPR64_KIND(fpr_##reg, generic_kind), nullptr, nullptr, nullptr,            \
  }

#define DEFINE_FCSR(reg, generic_kind)                                         \
  {                                                                            \
    #reg, nullptr, 4, FCSR_OFFSET,                                             \
    lldb::eEncodingUint, lldb::eFormatHex,                                     \
    FPR64_KIND(fpr_##reg, generic_kind), nullptr, nullptr, nullptr,            \
  }

// clang-format on

static lldb_private::RegisterInfo g_register_infos_loongarch64[] = {
    DEFINE_GPR64_ALT(r0, zero, LLDB_INVALID_REGNUM),
    DEFINE_GPR64_ALT(r1, ra, LLDB_REGNUM_GENERIC_RA),
    DEFINE_GPR64_ALT(r2, tp, LLDB_INVALID_REGNUM),
    DEFINE_GPR64_ALT(r3, sp, LLDB_REGNUM_GENERIC_SP),
    DEFINE_GPR64_ALT(r4, a0, LLDB_REGNUM_GENERIC_ARG1),
    DEFINE_GPR64_ALT(r5, a1, LLDB_REGNUM_GENERIC_ARG2),
    DEFINE_GPR64_ALT(r6, a2, LLDB_REGNUM_GENERIC_ARG3),
    DEFINE_GPR64_ALT(r7, a3, LLDB_REGNUM_GENERIC_ARG4),
    DEFINE_GPR64_ALT(r8, a4, LLDB_REGNUM_GENERIC_ARG5),
    DEFINE_GPR64_ALT(r9, a5, LLDB_REGNUM_GENERIC_ARG6),
    DEFINE_GPR64_ALT(r10, a6, LLDB_REGNUM_GENERIC_ARG7),
    DEFINE_GPR64_ALT(r11, a7, LLDB_REGNUM_GENERIC_ARG8),
    DEFINE_GPR64_ALT(r12, t0, LLDB_INVALID_REGNUM),
    DEFINE_GPR64_ALT(r13, t1, LLDB_INVALID_REGNUM),
    DEFINE_GPR64_ALT(r14, t2, LLDB_INVALID_REGNUM),
    DEFINE_GPR64_ALT(r15, t3, LLDB_INVALID_REGNUM),
    DEFINE_GPR64_ALT(r16, t4, LLDB_INVALID_REGNUM),
    DEFINE_GPR64_ALT(r17, t5, LLDB_INVALID_REGNUM),
    DEFINE_GPR64_ALT(r18, t6, LLDB_INVALID_REGNUM),
    DEFINE_GPR64_ALT(r19, t7, LLDB_INVALID_REGNUM),
    DEFINE_GPR64_ALT(r20, t8, LLDB_INVALID_REGNUM),
    DEFINE_GPR64(r21, LLDB_INVALID_REGNUM),
    DEFINE_GPR64_ALT(r22, fp, LLDB_REGNUM_GENERIC_FP),
    DEFINE_GPR64_ALT(r23, s0, LLDB_INVALID_REGNUM),
    DEFINE_GPR64_ALT(r24, s1, LLDB_INVALID_REGNUM),
    DEFINE_GPR64_ALT(r25, s2, LLDB_INVALID_REGNUM),
    DEFINE_GPR64_ALT(r26, s3, LLDB_INVALID_REGNUM),
    DEFINE_GPR64_ALT(r27, s4, LLDB_INVALID_REGNUM),
    DEFINE_GPR64_ALT(r28, s5, LLDB_INVALID_REGNUM),
    DEFINE_GPR64_ALT(r29, s6, LLDB_INVALID_REGNUM),
    DEFINE_GPR64_ALT(r30, s7, LLDB_INVALID_REGNUM),
    DEFINE_GPR64_ALT(r31, s8, LLDB_INVALID_REGNUM),

    DEFINE_GPR64(orig_a0, LLDB_INVALID_REGNUM),
    DEFINE_GPR64(pc, LLDB_REGNUM_GENERIC_PC),
    DEFINE_GPR64(badv, LLDB_INVALID_REGNUM),
    DEFINE_GPR64(reserved0, LLDB_INVALID_REGNUM),
    DEFINE_GPR64(reserved1, LLDB_INVALID_REGNUM),
    DEFINE_GPR64(reserved2, LLDB_INVALID_REGNUM),
    DEFINE_GPR64(reserved3, LLDB_INVALID_REGNUM),
    DEFINE_GPR64(reserved4, LLDB_INVALID_REGNUM),
    DEFINE_GPR64(reserved5, LLDB_INVALID_REGNUM),
    DEFINE_GPR64(reserved6, LLDB_INVALID_REGNUM),
    DEFINE_GPR64(reserved7, LLDB_INVALID_REGNUM),
    DEFINE_GPR64(reserved8, LLDB_INVALID_REGNUM),
    DEFINE_GPR64(reserved9, LLDB_INVALID_REGNUM),

    DEFINE_FPR64_ALT(f0, fa0, LLDB_INVALID_REGNUM),
    DEFINE_FPR64_ALT(f1, fa1, LLDB_INVALID_REGNUM),
    DEFINE_FPR64_ALT(f2, fa2, LLDB_INVALID_REGNUM),
    DEFINE_FPR64_ALT(f3, fa3, LLDB_INVALID_REGNUM),
    DEFINE_FPR64_ALT(f4, fa4, LLDB_INVALID_REGNUM),
    DEFINE_FPR64_ALT(f5, fa5, LLDB_INVALID_REGNUM),
    DEFINE_FPR64_ALT(f6, fa6, LLDB_INVALID_REGNUM),
    DEFINE_FPR64_ALT(f7, fa7, LLDB_INVALID_REGNUM),
    DEFINE_FPR64_ALT(f8, ft0, LLDB_INVALID_REGNUM),
    DEFINE_FPR64_ALT(f9, ft1, LLDB_INVALID_REGNUM),
    DEFINE_FPR64_ALT(f10, ft2, LLDB_INVALID_REGNUM),
    DEFINE_FPR64_ALT(f11, ft3, LLDB_INVALID_REGNUM),
    DEFINE_FPR64_ALT(f12, ft4, LLDB_INVALID_REGNUM),
    DEFINE_FPR64_ALT(f13, ft5, LLDB_INVALID_REGNUM),
    DEFINE_FPR64_ALT(f14, ft6, LLDB_INVALID_REGNUM),
    DEFINE_FPR64_ALT(f15, ft7, LLDB_INVALID_REGNUM),
    DEFINE_FPR64_ALT(f16, ft8, LLDB_INVALID_REGNUM),
    DEFINE_FPR64_ALT(f17, ft9, LLDB_INVALID_REGNUM),
    DEFINE_FPR64_ALT(f18, ft10, LLDB_INVALID_REGNUM),
    DEFINE_FPR64_ALT(f19, ft11, LLDB_INVALID_REGNUM),
    DEFINE_FPR64_ALT(f20, ft12, LLDB_INVALID_REGNUM),
    DEFINE_FPR64_ALT(f21, ft13, LLDB_INVALID_REGNUM),
    DEFINE_FPR64_ALT(f22, ft14, LLDB_INVALID_REGNUM),
    DEFINE_FPR64_ALT(f23, ft15, LLDB_INVALID_REGNUM),
    DEFINE_FPR64_ALT(f24, fs0, LLDB_INVALID_REGNUM),
    DEFINE_FPR64_ALT(f25, fs1, LLDB_INVALID_REGNUM),
    DEFINE_FPR64_ALT(f26, fs2, LLDB_INVALID_REGNUM),
    DEFINE_FPR64_ALT(f27, fs3, LLDB_INVALID_REGNUM),
    DEFINE_FPR64_ALT(f28, fs4, LLDB_INVALID_REGNUM),
    DEFINE_FPR64_ALT(f29, fs5, LLDB_INVALID_REGNUM),
    DEFINE_FPR64_ALT(f30, fs6, LLDB_INVALID_REGNUM),
    DEFINE_FPR64_ALT(f31, fs7, LLDB_INVALID_REGNUM),

    DEFINE_FCC(fcc0, LLDB_INVALID_REGNUM),
    DEFINE_FCC(fcc1, LLDB_INVALID_REGNUM),
    DEFINE_FCC(fcc2, LLDB_INVALID_REGNUM),
    DEFINE_FCC(fcc3, LLDB_INVALID_REGNUM),
    DEFINE_FCC(fcc4, LLDB_INVALID_REGNUM),
    DEFINE_FCC(fcc5, LLDB_INVALID_REGNUM),
    DEFINE_FCC(fcc6, LLDB_INVALID_REGNUM),
    DEFINE_FCC(fcc7, LLDB_INVALID_REGNUM),
    DEFINE_FCSR(fcsr, LLDB_INVALID_REGNUM),
};

#endif // DECLARE_REGISTER_INFOS_LOONGARCH64_STRUCT
