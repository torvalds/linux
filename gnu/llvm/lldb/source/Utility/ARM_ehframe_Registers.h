//===-- ARM_ehframe_Registers.h -------------------------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_UTILITY_ARM_EHFRAME_REGISTERS_H
#define LLDB_SOURCE_UTILITY_ARM_EHFRAME_REGISTERS_H

// The register numbers used in the eh_frame unwind information.
// Should be the same as DWARF register numbers.

enum {
  ehframe_r0 = 0,
  ehframe_r1,
  ehframe_r2,
  ehframe_r3,
  ehframe_r4,
  ehframe_r5,
  ehframe_r6,
  ehframe_r7,
  ehframe_r8,
  ehframe_r9,
  ehframe_r10,
  ehframe_r11,
  ehframe_r12,
  ehframe_sp,
  ehframe_lr,
  ehframe_pc,
  ehframe_cpsr
};

#endif // LLDB_SOURCE_UTILITY_ARM_EHFRAME_REGISTERS_H
