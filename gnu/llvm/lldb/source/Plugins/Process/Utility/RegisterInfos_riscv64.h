//===-- RegisterInfos_riscv64.h ---------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifdef DECLARE_REGISTER_INFOS_RISCV64_STRUCT

#include <stddef.h>

#include "lldb/lldb-defines.h"
#include "lldb/lldb-enumerations.h"
#include "lldb/lldb-private.h"

#include "Utility/RISCV_DWARF_Registers.h"
#include "lldb-riscv-register-enums.h"

#ifndef GPR_OFFSET
#error GPR_OFFSET must be defined before including this header file
#endif

#ifndef FPR_OFFSET
#error FPR_OFFSET must be defined before including this header file
#endif

using namespace riscv_dwarf;

// clang-format off

// I suppose EHFrame and DWARF are the same.
#define KIND_HELPER(reg, generic_kind)                                         \
  {                                                                            \
    riscv_dwarf::dwarf_##reg, riscv_dwarf::dwarf_##reg, generic_kind,          \
    LLDB_INVALID_REGNUM, reg##_riscv                                           \
  }

// Generates register kinds array for vector registers
#define GPR64_KIND(reg, generic_kind) KIND_HELPER(reg, generic_kind)

// FPR register kinds array for vector registers
#define FPR64_KIND(reg, generic_kind) KIND_HELPER(reg, generic_kind)

// VPR register kinds array for vector registers
#define VPR_KIND(reg, generic_kind) KIND_HELPER(reg, generic_kind)

// Defines a 64-bit general purpose register
#define DEFINE_GPR64(reg, generic_kind) DEFINE_GPR64_ALT(reg, reg, generic_kind)

// Defines a 64-bit general purpose register
#define DEFINE_GPR64_ALT(reg, alt, generic_kind)                               \
  {                                                                            \
    #reg, #alt, 8, GPR_OFFSET(gpr_##reg##_riscv - gpr_first_riscv),            \
    lldb::eEncodingUint, lldb::eFormatHex,                                     \
    GPR64_KIND(gpr_##reg, generic_kind), nullptr, nullptr, nullptr,            \
  }

#define DEFINE_FPR64(reg, generic_kind) DEFINE_FPR64_ALT(reg, reg, generic_kind)

#define DEFINE_FPR64_ALT(reg, alt, generic_kind) DEFINE_FPR_ALT(reg, alt, 8, generic_kind)

#define DEFINE_FPR_ALT(reg, alt, size, generic_kind)                           \
  {                                                                            \
    #reg, #alt, size, FPR_OFFSET(fpr_##reg##_riscv - fpr_first_riscv),         \
    lldb::eEncodingUint, lldb::eFormatHex,                                     \
    FPR64_KIND(fpr_##reg, generic_kind), nullptr, nullptr, nullptr,           \
  }

#define DEFINE_VPR(reg, generic_kind) DEFINE_VPR_ALT(reg, reg, generic_kind)

// Defines a scalable vector register, with default size 128 bits
// The byte offset 0 is a placeholder, which should be corrected at runtime.
#define DEFINE_VPR_ALT(reg, alt, generic_kind)                                 \
  {                                                                            \
    #reg, #alt, 16, 0, lldb::eEncodingVector, lldb::eFormatVectorOfUInt8,      \
    VPR_KIND(vpr_##reg, generic_kind), nullptr, nullptr, nullptr               \
  }

// clang-format on

static lldb_private::RegisterInfo g_register_infos_riscv64_le[] = {
    // DEFINE_GPR64(name, GENERIC KIND)
    DEFINE_GPR64(pc, LLDB_REGNUM_GENERIC_PC),
    DEFINE_GPR64_ALT(ra, x1, LLDB_REGNUM_GENERIC_RA),
    DEFINE_GPR64_ALT(sp, x2, LLDB_REGNUM_GENERIC_SP),
    DEFINE_GPR64_ALT(gp, x3, LLDB_INVALID_REGNUM),
    DEFINE_GPR64_ALT(tp, x4, LLDB_INVALID_REGNUM),
    DEFINE_GPR64_ALT(t0, x5, LLDB_INVALID_REGNUM),
    DEFINE_GPR64_ALT(t1, x6, LLDB_INVALID_REGNUM),
    DEFINE_GPR64_ALT(t2, x7, LLDB_INVALID_REGNUM),
    DEFINE_GPR64_ALT(fp, x8, LLDB_REGNUM_GENERIC_FP),
    DEFINE_GPR64_ALT(s1, x9, LLDB_INVALID_REGNUM),
    DEFINE_GPR64_ALT(a0, x10, LLDB_REGNUM_GENERIC_ARG1),
    DEFINE_GPR64_ALT(a1, x11, LLDB_REGNUM_GENERIC_ARG2),
    DEFINE_GPR64_ALT(a2, x12, LLDB_REGNUM_GENERIC_ARG3),
    DEFINE_GPR64_ALT(a3, x13, LLDB_REGNUM_GENERIC_ARG4),
    DEFINE_GPR64_ALT(a4, x14, LLDB_REGNUM_GENERIC_ARG5),
    DEFINE_GPR64_ALT(a5, x15, LLDB_REGNUM_GENERIC_ARG6),
    DEFINE_GPR64_ALT(a6, x16, LLDB_REGNUM_GENERIC_ARG7),
    DEFINE_GPR64_ALT(a7, x17, LLDB_REGNUM_GENERIC_ARG8),
    DEFINE_GPR64_ALT(s2, x18, LLDB_INVALID_REGNUM),
    DEFINE_GPR64_ALT(s3, x19, LLDB_INVALID_REGNUM),
    DEFINE_GPR64_ALT(s4, x20, LLDB_INVALID_REGNUM),
    DEFINE_GPR64_ALT(s5, x21, LLDB_INVALID_REGNUM),
    DEFINE_GPR64_ALT(s6, x22, LLDB_INVALID_REGNUM),
    DEFINE_GPR64_ALT(s7, x23, LLDB_INVALID_REGNUM),
    DEFINE_GPR64_ALT(s8, x24, LLDB_INVALID_REGNUM),
    DEFINE_GPR64_ALT(s9, x25, LLDB_INVALID_REGNUM),
    DEFINE_GPR64_ALT(s10, x26, LLDB_INVALID_REGNUM),
    DEFINE_GPR64_ALT(s11, x27, LLDB_INVALID_REGNUM),
    DEFINE_GPR64_ALT(t3, x28, LLDB_INVALID_REGNUM),
    DEFINE_GPR64_ALT(t4, x29, LLDB_INVALID_REGNUM),
    DEFINE_GPR64_ALT(t5, x30, LLDB_INVALID_REGNUM),
    DEFINE_GPR64_ALT(t6, x31, LLDB_INVALID_REGNUM),
    DEFINE_GPR64_ALT(zero, x0, LLDB_INVALID_REGNUM),

    DEFINE_FPR64_ALT(ft0, f0, LLDB_INVALID_REGNUM),
    DEFINE_FPR64_ALT(ft1, f1, LLDB_INVALID_REGNUM),
    DEFINE_FPR64_ALT(ft2, f2, LLDB_INVALID_REGNUM),
    DEFINE_FPR64_ALT(ft3, f3, LLDB_INVALID_REGNUM),
    DEFINE_FPR64_ALT(ft4, f4, LLDB_INVALID_REGNUM),
    DEFINE_FPR64_ALT(ft5, f5, LLDB_INVALID_REGNUM),
    DEFINE_FPR64_ALT(ft6, f6, LLDB_INVALID_REGNUM),
    DEFINE_FPR64_ALT(ft7, f7, LLDB_INVALID_REGNUM),
    DEFINE_FPR64_ALT(fs0, f8, LLDB_INVALID_REGNUM),
    DEFINE_FPR64_ALT(fs1, f9, LLDB_INVALID_REGNUM),
    DEFINE_FPR64_ALT(fa0, f10, LLDB_INVALID_REGNUM),
    DEFINE_FPR64_ALT(fa1, f11, LLDB_INVALID_REGNUM),
    DEFINE_FPR64_ALT(fa2, f12, LLDB_INVALID_REGNUM),
    DEFINE_FPR64_ALT(fa3, f13, LLDB_INVALID_REGNUM),
    DEFINE_FPR64_ALT(fa4, f14, LLDB_INVALID_REGNUM),
    DEFINE_FPR64_ALT(fa5, f15, LLDB_INVALID_REGNUM),
    DEFINE_FPR64_ALT(fa6, f16, LLDB_INVALID_REGNUM),
    DEFINE_FPR64_ALT(fa7, f17, LLDB_INVALID_REGNUM),
    DEFINE_FPR64_ALT(fs2, f18, LLDB_INVALID_REGNUM),
    DEFINE_FPR64_ALT(fs3, f19, LLDB_INVALID_REGNUM),
    DEFINE_FPR64_ALT(fs4, f20, LLDB_INVALID_REGNUM),
    DEFINE_FPR64_ALT(fs5, f21, LLDB_INVALID_REGNUM),
    DEFINE_FPR64_ALT(fs6, f22, LLDB_INVALID_REGNUM),
    DEFINE_FPR64_ALT(fs7, f23, LLDB_INVALID_REGNUM),
    DEFINE_FPR64_ALT(fs8, f24, LLDB_INVALID_REGNUM),
    DEFINE_FPR64_ALT(fs9, f25, LLDB_INVALID_REGNUM),
    DEFINE_FPR64_ALT(fs10, f26, LLDB_INVALID_REGNUM),
    DEFINE_FPR64_ALT(fs11, f27, LLDB_INVALID_REGNUM),
    DEFINE_FPR64_ALT(ft8, f28, LLDB_INVALID_REGNUM),
    DEFINE_FPR64_ALT(ft9, f29, LLDB_INVALID_REGNUM),
    DEFINE_FPR64_ALT(ft10, f30, LLDB_INVALID_REGNUM),
    DEFINE_FPR64_ALT(ft11, f31, LLDB_INVALID_REGNUM),
    DEFINE_FPR_ALT(fcsr, nullptr, 4, LLDB_INVALID_REGNUM),

    DEFINE_VPR(v0, LLDB_INVALID_REGNUM),
    DEFINE_VPR(v1, LLDB_INVALID_REGNUM),
    DEFINE_VPR(v2, LLDB_INVALID_REGNUM),
    DEFINE_VPR(v3, LLDB_INVALID_REGNUM),
    DEFINE_VPR(v4, LLDB_INVALID_REGNUM),
    DEFINE_VPR(v5, LLDB_INVALID_REGNUM),
    DEFINE_VPR(v6, LLDB_INVALID_REGNUM),
    DEFINE_VPR(v7, LLDB_INVALID_REGNUM),
    DEFINE_VPR(v8, LLDB_INVALID_REGNUM),
    DEFINE_VPR(v9, LLDB_INVALID_REGNUM),
    DEFINE_VPR(v10, LLDB_INVALID_REGNUM),
    DEFINE_VPR(v11, LLDB_INVALID_REGNUM),
    DEFINE_VPR(v12, LLDB_INVALID_REGNUM),
    DEFINE_VPR(v13, LLDB_INVALID_REGNUM),
    DEFINE_VPR(v14, LLDB_INVALID_REGNUM),
    DEFINE_VPR(v15, LLDB_INVALID_REGNUM),
    DEFINE_VPR(v16, LLDB_INVALID_REGNUM),
    DEFINE_VPR(v17, LLDB_INVALID_REGNUM),
    DEFINE_VPR(v18, LLDB_INVALID_REGNUM),
    DEFINE_VPR(v19, LLDB_INVALID_REGNUM),
    DEFINE_VPR(v20, LLDB_INVALID_REGNUM),
    DEFINE_VPR(v21, LLDB_INVALID_REGNUM),
    DEFINE_VPR(v22, LLDB_INVALID_REGNUM),
    DEFINE_VPR(v23, LLDB_INVALID_REGNUM),
    DEFINE_VPR(v24, LLDB_INVALID_REGNUM),
    DEFINE_VPR(v25, LLDB_INVALID_REGNUM),
    DEFINE_VPR(v26, LLDB_INVALID_REGNUM),
    DEFINE_VPR(v27, LLDB_INVALID_REGNUM),
    DEFINE_VPR(v28, LLDB_INVALID_REGNUM),
    DEFINE_VPR(v29, LLDB_INVALID_REGNUM),
    DEFINE_VPR(v30, LLDB_INVALID_REGNUM),
    DEFINE_VPR(v31, LLDB_INVALID_REGNUM),
};

#endif // DECLARE_REGISTER_INFOS_RISCV64_STRUCT
