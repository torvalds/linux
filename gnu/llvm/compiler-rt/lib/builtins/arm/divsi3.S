//===-- divsi3.S - 32-bit signed integer divide ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the __divsi3 (32-bit signed integer divide) function
// for the ARM architecture as a wrapper around the unsigned routine.
//
//===----------------------------------------------------------------------===//

#include "../assembly.h"

#define ESTABLISH_FRAME \
    push   {r4, r7, lr}    ;\
    add     r7,     sp, #4
#define CLEAR_FRAME_AND_RETURN \
    pop    {r4, r7, pc}

   .syntax unified
   .text
   DEFINE_CODE_STATE

	.p2align 3
// Ok, APCS and AAPCS agree on 32 bit args, so it's safe to use the same routine.
DEFINE_AEABI_FUNCTION_ALIAS(__aeabi_idiv, __divsi3)

@ int __divsi3(int divident, int divisor)
@   Calculate and return the quotient of the (signed) division.

DEFINE_COMPILERRT_FUNCTION(__divsi3)
#if __ARM_ARCH_EXT_IDIV__
   tst     r1,r1
   beq     LOCAL_LABEL(divzero)
   sdiv    r0, r0, r1
   bx      lr
LOCAL_LABEL(divzero):
   // Use movs for compatibility with v8-m.base.
   movs    r0,#0
   bx      lr
#else
ESTABLISH_FRAME
//  Set aside the sign of the quotient.
#  if defined(USE_THUMB_1)
    movs    r4,     r0
    eors    r4,     r1
#  else
    eor     r4,     r0, r1
#  endif
//  Take absolute value of a and b via abs(x) = (x^(x >> 31)) - (x >> 31).
#  if defined(USE_THUMB_1)
    asrs    r2,     r0, #31
    asrs    r3,     r1, #31
    eors    r0,     r2
    eors    r1,     r3
    subs    r0,     r0, r2
    subs    r1,     r1, r3
#  else
    eor     r2,     r0, r0, asr #31
    eor     r3,     r1, r1, asr #31
    sub     r0,     r2, r0, asr #31
    sub     r1,     r3, r1, asr #31
#  endif
//  abs(a) / abs(b)
    bl      SYMBOL_NAME(__udivsi3)
//  Apply sign of quotient to result and return.
#  if defined(USE_THUMB_1)
    asrs    r4,     #31
    eors    r0,     r4
    subs    r0,     r0, r4
#  else
    eor     r0,     r0, r4, asr #31
    sub     r0,     r0, r4, asr #31
#  endif
    CLEAR_FRAME_AND_RETURN
#endif
END_COMPILERRT_FUNCTION(__divsi3)

NO_EXEC_STACK_DIRECTIVE

