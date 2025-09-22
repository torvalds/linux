//===-- sync-ops.h - --===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements outline macros for the __sync_fetch_and_*
// operations. Different instantiations will generate appropriate assembly for
// ARM and Thumb-2 versions of the functions.
//
//===----------------------------------------------------------------------===//

#include "../assembly.h"

#if __ARM_ARCH >= 7
#define DMB dmb
#elif __ARM_ARCH >= 6
#define DMB mcr p15, #0, r0, c7, c10, #5
#else
#error DMB is only supported on ARMv6+
#endif

#define SYNC_OP_4(op)                                                          \
  .p2align 2;                                                                  \
  .syntax unified;                                                             \
  DEFINE_COMPILERRT_FUNCTION(__sync_fetch_and_##op)                            \
  DMB;                                                                         \
  mov r12, r0;                                                                 \
  LOCAL_LABEL(tryatomic_##op) : ldrex r0, [r12];                               \
  op(r2, r0, r1);                                                              \
  strex r3, r2, [r12];                                                         \
  cmp r3, #0;                                                                  \
  bne LOCAL_LABEL(tryatomic_##op);                                             \
  DMB;                                                                         \
  bx lr

#define SYNC_OP_8(op)                                                          \
  .p2align 2;                                                                  \
  .syntax unified;                                                             \
  DEFINE_COMPILERRT_FUNCTION(__sync_fetch_and_##op)                            \
  push {r4, r5, r6, lr};                                                       \
  DMB;                                                                         \
  mov r12, r0;                                                                 \
  LOCAL_LABEL(tryatomic_##op) : ldrexd r0, r1, [r12];                          \
  op(r4, r5, r0, r1, r2, r3);                                                  \
  strexd r6, r4, r5, [r12];                                                    \
  cmp r6, #0;                                                                  \
  bne LOCAL_LABEL(tryatomic_##op);                                             \
  DMB;                                                                         \
  pop { r4, r5, r6, pc }

#define MINMAX_4(rD, rN, rM, cmp_kind)                                         \
  cmp rN, rM;                                                                  \
  mov rD, rM;                                                                  \
  it cmp_kind;                                                                 \
  mov##cmp_kind rD, rN

#define MINMAX_8(rD_LO, rD_HI, rN_LO, rN_HI, rM_LO, rM_HI, cmp_kind)           \
  cmp rN_LO, rM_LO;                                                            \
  sbcs rN_HI, rM_HI;                                                           \
  mov rD_LO, rM_LO;                                                            \
  mov rD_HI, rM_HI;                                                            \
  itt cmp_kind;                                                                \
  mov##cmp_kind rD_LO, rN_LO;                                                  \
  mov##cmp_kind rD_HI, rN_HI
