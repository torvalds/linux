//===-- elfnix_tlv.aarch64.s ---------------------------------------*- ASM -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of the ORC runtime support library.
//
//===----------------------------------------------------------------------===//

// The content of this file is aarch64-only
#if defined(__arm64__) || defined(__aarch64__)

#define REGISTER_SAVE_SPACE_SIZE     32 * 24

        .text

  // returns address of TLV in x0, all other registers preserved
  // TODO: add fast-path for repeat access
  .globl ___orc_rt_elfnix_tlsdesc_resolver
___orc_rt_elfnix_tlsdesc_resolver:
        sub  sp,  sp, #REGISTER_SAVE_SPACE_SIZE
        stp x29, x30, [sp, #16 * 1]
        stp x27, x28, [sp, #16 * 2]
        stp x25, x26, [sp, #16 * 3]
        stp x23, x24, [sp, #16 * 4]
        stp x21, x22, [sp, #16 * 5]
        stp x19, x20, [sp, #16 * 6]
        stp x17, x18, [sp, #16 * 7]
        stp x15, x16, [sp, #16 * 8]
        stp x13, x14, [sp, #16 * 9]
        stp x11, x12, [sp, #16 * 10]
        stp  x9, x10, [sp, #16 * 11]
        stp  x7,  x8, [sp, #16 * 12]
        stp  x5,  x6, [sp, #16 * 13]
        stp  x3,  x4, [sp, #16 * 14]
        stp  x1,  x2, [sp, #16 * 15]
        stp q30, q31, [sp, #32 * 8]
        stp q28, q29, [sp, #32 * 9]
        stp q26, q27, [sp, #32 * 10]
        stp q24, q25, [sp, #32 * 11]
        stp q22, q23, [sp, #32 * 12]
        stp q20, q21, [sp, #32 * 13]
        stp q18, q19, [sp, #32 * 14]
        stp q16, q17, [sp, #32 * 15]
        stp q14, q15, [sp, #32 * 16]
        stp q12, q13, [sp, #32 * 17]
        stp q10, q11, [sp, #32 * 18]
        stp  q8,  q9, [sp, #32 * 19]
        stp  q6,  q7, [sp, #32 * 20]
        stp  q4,  q5, [sp, #32 * 21]
        stp  q2,  q3, [sp, #32 * 22]
        stp  q0,  q1, [sp, #32 * 23]

        mrs x1, TPIDR_EL0 // get thread pointer
        bl ___orc_rt_elfnix_tlsdesc_resolver_impl

        ldp  q0,  q1, [sp, #32 * 23]
        ldp  q2,  q3, [sp, #32 * 22]
        ldp  q4,  q5, [sp, #32 * 21]
        ldp  q6,  q7, [sp, #32 * 20]
        ldp  q8,  q9, [sp, #32 * 19]
        ldp q10, q11, [sp, #32 * 18]
        ldp q12, q13, [sp, #32 * 17]
        ldp q14, q15, [sp, #32 * 16]
        ldp q16, q17, [sp, #32 * 15]
        ldp q18, q19, [sp, #32 * 14]
        ldp q20, q21, [sp, #32 * 13]
        ldp q22, q23, [sp, #32 * 12]
        ldp q24, q25, [sp, #32 * 11]
        ldp q26, q27, [sp, #32 * 10]
        ldp q28, q29, [sp, #32 * 9]
        ldp q30, q31, [sp, #32 * 8]
        ldp  x1,  x2, [sp, #16 * 15]
        ldp  x3,  x4, [sp, #16 * 14]
        ldp  x5,  x6, [sp, #16 * 13]
        ldp  x7,  x8, [sp, #16 * 12]
        ldp  x9, x10, [sp, #16 * 11]
        ldp x11, x12, [sp, #16 * 10]
        ldp x13, x14, [sp, #16 * 9]
        ldp x15, x16, [sp, #16 * 8]
        ldp x17, x18, [sp, #16 * 7]
        ldp x19, x20, [sp, #16 * 6]
        ldp x21, x22, [sp, #16 * 5]
        ldp x23, x24, [sp, #16 * 4]
        ldp x25, x26, [sp, #16 * 3]
        ldp x27, x28, [sp, #16 * 2]
        ldp x29, x30, [sp, #16 * 1]
        add  sp,  sp, #REGISTER_SAVE_SPACE_SIZE
        ret

#endif // defined(__arm64__) || defined(__aarch64__)
