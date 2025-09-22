//===-- ARM64_DWARF_Registers.h ---------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_UTILITY_ARM64_DWARF_REGISTERS_H
#define LLDB_SOURCE_UTILITY_ARM64_DWARF_REGISTERS_H

#include "lldb/lldb-private.h"

namespace arm64_dwarf {

enum {
  x0 = 0,
  x1,
  x2,
  x3,
  x4,
  x5,
  x6,
  x7,
  x8,
  x9,
  x10,
  x11,
  x12,
  x13,
  x14,
  x15,
  x16,
  x17,
  x18,
  x19,
  x20,
  x21,
  x22,
  x23,
  x24,
  x25,
  x26,
  x27,
  x28,
  x29 = 29,
  fp = x29,
  x30 = 30,
  lr = x30,
  x31 = 31,
  sp = x31,
  pc = 32,
  cpsr = 33,
  // 34-45 reserved

  // 64-bit SVE Vector granule pseudo register
  vg = 46,

  // VG ́8-bit SVE first fault register
  ffr = 47,

  // VG x ́8-bit SVE predicate registers
  p0 = 48,
  p1,
  p2,
  p3,
  p4,
  p5,
  p6,
  p7,
  p8,
  p9,
  p10,
  p11,
  p12,
  p13,
  p14,
  p15,

  // V0-V31 (128 bit vector registers)
  v0 = 64,
  v1,
  v2,
  v3,
  v4,
  v5,
  v6,
  v7,
  v8,
  v9,
  v10,
  v11,
  v12,
  v13,
  v14,
  v15,
  v16,
  v17,
  v18,
  v19,
  v20,
  v21,
  v22,
  v23,
  v24,
  v25,
  v26,
  v27,
  v28,
  v29,
  v30,
  v31,

  // VG ́64-bit SVE vector registers
  z0 = 96,
  z1,
  z2,
  z3,
  z4,
  z5,
  z6,
  z7,
  z8,
  z9,
  z10,
  z11,
  z12,
  z13,
  z14,
  z15,
  z16,
  z17,
  z18,
  z19,
  z20,
  z21,
  z22,
  z23,
  z24,
  z25,
  z26,
  z27,
  z28,
  z29,
  z30,
  z31
};

} // namespace arm64_dwarf

#endif // LLDB_SOURCE_UTILITY_ARM64_DWARF_REGISTERS_H
