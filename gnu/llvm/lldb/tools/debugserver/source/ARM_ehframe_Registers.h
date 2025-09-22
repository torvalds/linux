//===-- ARM_ehframe_Registers.h -------------------------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef utility_ARM_ehframe_Registers_h_
#define utility_ARM_ehframe_Registers_h_

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

#endif // utility_ARM_ehframe_Registers_h_
