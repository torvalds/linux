//===-- comparesf2.S - Implement single-precision soft-float comparisons --===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the following soft-fp_t comparison routines:
//
//   __eqsf2   __gesf2   __unordsf2
//   __lesf2   __gtsf2
//   __ltsf2
//   __nesf2
//
// The semantics of the routines grouped in each column are identical, so there
// is a single implementation for each, with multiple names.
//
// The routines behave as follows:
//
//   __lesf2(a,b) returns -1 if a < b
//                         0 if a == b
//                         1 if a > b
//                         1 if either a or b is NaN
//
//   __gesf2(a,b) returns -1 if a < b
//                         0 if a == b
//                         1 if a > b
//                        -1 if either a or b is NaN
//
//   __unordsf2(a,b) returns 0 if both a and b are numbers
//                           1 if either a or b is NaN
//
// Note that __lesf2( ) and __gesf2( ) are identical except in their handling of
// NaN values.
//
//===----------------------------------------------------------------------===//

#include "../assembly.h"

    .syntax unified
    .text
    DEFINE_CODE_STATE

    .macro COMPARESF2_FUNCTION_BODY handle_nan:req
#if defined(COMPILER_RT_ARMHF_TARGET)
    vmov r0, s0
    vmov r1, s1
#endif
    // Make copies of a and b with the sign bit shifted off the top.  These will
    // be used to detect zeros and NaNs.
#if defined(USE_THUMB_1)
    push    {r6, lr}
    lsls    r2,         r0, #1
    lsls    r3,         r1, #1
#else
    mov     r2,         r0, lsl #1
    mov     r3,         r1, lsl #1
#endif

    // We do the comparison in three stages (ignoring NaN values for the time
    // being).  First, we orr the absolute values of a and b; this sets the Z
    // flag if both a and b are zero (of either sign).  The shift of r3 doesn't
    // effect this at all, but it *does* make sure that the C flag is clear for
    // the subsequent operations.
#if defined(USE_THUMB_1)
    lsrs    r6,     r3, #1
    orrs    r6,     r2
#else
    orrs    r12,    r2, r3, lsr #1
#endif
    // Next, we check if a and b have the same or different signs.  If they have
    // opposite signs, this eor will set the N flag.
#if defined(USE_THUMB_1)
    beq     1f
    movs    r6,     r0
    eors    r6,     r1
1:
#else
    it ne
    eorsne  r12,    r0, r1
#endif

    // If a and b are equal (either both zeros or bit identical; again, we're
    // ignoring NaNs for now), this subtract will zero out r0.  If they have the
    // same sign, the flags are updated as they would be for a comparison of the
    // absolute values of a and b.
#if defined(USE_THUMB_1)
    bmi     1f
    subs    r0,     r2, r3
1:
#else
    it pl
    subspl  r0,     r2, r3
#endif

    // If a is smaller in magnitude than b and both have the same sign, place
    // the negation of the sign of b in r0.  Thus, if both are negative and
    // a > b, this sets r0 to 0; if both are positive and a < b, this sets
    // r0 to -1.
    //
    // This is also done if a and b have opposite signs and are not both zero,
    // because in that case the subtract was not performed and the C flag is
    // still clear from the shift argument in orrs; if a is positive and b
    // negative, this places 0 in r0; if a is negative and b positive, -1 is
    // placed in r0.
#if defined(USE_THUMB_1)
    bhs     1f
    // Here if a and b have the same sign and absA < absB, the result is thus
    // b < 0 ? 1 : -1. Same if a and b have the opposite sign (ignoring Nan).
    movs    r0,         #1
    lsrs    r1,         #31
    bne     LOCAL_LABEL(CHECK_NAN\@)
    negs    r0,         r0
    b       LOCAL_LABEL(CHECK_NAN\@)
1:
#else
    it lo
    mvnlo   r0,         r1, asr #31
#endif

    // If a is greater in magnitude than b and both have the same sign, place
    // the sign of b in r0.  Thus, if both are negative and a < b, -1 is placed
    // in r0, which is the desired result.  Conversely, if both are positive
    // and a > b, zero is placed in r0.
#if defined(USE_THUMB_1)
    bls     1f
    // Here both have the same sign and absA > absB.
    movs    r0,         #1
    lsrs    r1,         #31
    beq     LOCAL_LABEL(CHECK_NAN\@)
    negs    r0, r0
1:
#else
    it hi
    movhi   r0,         r1, asr #31
#endif

    // If you've been keeping track, at this point r0 contains -1 if a < b and
    // 0 if a >= b.  All that remains to be done is to set it to 1 if a > b.
    // If a == b, then the Z flag is set, so we can get the correct final value
    // into r0 by simply or'ing with 1 if Z is clear.
    // For Thumb-1, r0 contains -1 if a < b, 0 if a > b and 0 if a == b.
#if !defined(USE_THUMB_1)
    it ne
    orrne   r0,     r0, #1
#endif

    // Finally, we need to deal with NaNs.  If either argument is NaN, replace
    // the value in r0 with 1.
#if defined(USE_THUMB_1)
LOCAL_LABEL(CHECK_NAN\@):
    movs    r6,         #0xff
    lsls    r6,         #24
    cmp     r2,         r6
    bhi     1f
    cmp     r3,         r6
1:
    bls     2f
    \handle_nan
2:
    pop     {r6, pc}
#else
    cmp     r2,         #0xff000000
    ite ls
    cmpls   r3,         #0xff000000
    \handle_nan
    JMP(lr)
#endif
    .endm

@ int __eqsf2(float a, float b)

    .p2align 2
DEFINE_COMPILERRT_FUNCTION(__eqsf2)

    .macro __eqsf2_handle_nan
#if defined(USE_THUMB_1)
    movs    r0,         #1
#else
    movhi   r0,         #1
#endif
    .endm

COMPARESF2_FUNCTION_BODY __eqsf2_handle_nan

END_COMPILERRT_FUNCTION(__eqsf2)

DEFINE_COMPILERRT_FUNCTION_ALIAS(__lesf2, __eqsf2)
DEFINE_COMPILERRT_FUNCTION_ALIAS(__ltsf2, __eqsf2)
DEFINE_COMPILERRT_FUNCTION_ALIAS(__nesf2, __eqsf2)

#if defined(__ELF__)
// Alias for libgcc compatibility
DEFINE_COMPILERRT_FUNCTION_ALIAS(__cmpsf2, __lesf2)
#endif

@ int __gtsf2(float a, float b)

    .p2align 2
DEFINE_COMPILERRT_FUNCTION(__gtsf2)

    .macro __gtsf2_handle_nan
#if defined(USE_THUMB_1)
    movs    r0,         #1
    negs    r0,         r0
#else
    movhi   r0,         #-1
#endif
    .endm

COMPARESF2_FUNCTION_BODY __gtsf2_handle_nan

END_COMPILERRT_FUNCTION(__gtsf2)

DEFINE_COMPILERRT_FUNCTION_ALIAS(__gesf2, __gtsf2)

@ int __unordsf2(float a, float b)

    .p2align 2
DEFINE_COMPILERRT_FUNCTION(__unordsf2)

#if defined(COMPILER_RT_ARMHF_TARGET)
    vmov    r0,         s0
    vmov    r1,         s1
#endif
    // Return 1 for NaN values, 0 otherwise.
    lsls    r2,         r0, #1
    lsls    r3,         r1, #1
    movs    r0,         #0
#if defined(USE_THUMB_1)
    movs    r1,         #0xff
    lsls    r1,         #24
    cmp     r2,         r1
    bhi     1f
    cmp     r3,         r1
1:
    bls     2f
    movs    r0,         #1
2:
#else
    cmp     r2,         #0xff000000
    ite ls
    cmpls   r3,         #0xff000000
    movhi   r0,         #1
#endif
    JMP(lr)
END_COMPILERRT_FUNCTION(__unordsf2)

#if defined(COMPILER_RT_ARMHF_TARGET)
DEFINE_COMPILERRT_FUNCTION(__aeabi_fcmpun)
	vmov s0, r0
	vmov s1, r1
	b SYMBOL_NAME(__unordsf2)
END_COMPILERRT_FUNCTION(__aeabi_fcmpun)
#else
DEFINE_AEABI_FUNCTION_ALIAS(__aeabi_fcmpun, __unordsf2)
#endif

NO_EXEC_STACK_DIRECTIVE

