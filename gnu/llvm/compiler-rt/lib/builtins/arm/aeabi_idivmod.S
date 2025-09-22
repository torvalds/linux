//===-- aeabi_idivmod.S - EABI idivmod implementation ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "../assembly.h"

// struct { int quot, int rem} __aeabi_idivmod(int numerator, int denominator) {
//   int rem, quot;
//   quot = __divmodsi4(numerator, denominator, &rem);
//   return {quot, rem};
// }

#if defined(__MINGW32__)
#define __aeabi_idivmod __rt_sdiv
#endif

        .syntax unified
        .text
        DEFINE_CODE_STATE
        .p2align 2
DEFINE_COMPILERRT_FUNCTION(__aeabi_idivmod)
#if defined(USE_THUMB_1)
        push    {r0, r1, lr}
        bl      SYMBOL_NAME(__divsi3)
        pop     {r1, r2, r3} // now r0 = quot, r1 = num, r2 = denom
        muls    r2, r0, r2   // r2 = quot * denom
        subs    r1, r1, r2
        JMP     (r3)
#else  // defined(USE_THUMB_1)
        push    { lr }
        sub     sp, sp, #4
        mov     r2, sp
#if defined(__MINGW32__)
        mov     r3, r0
        mov     r0, r1
        mov     r1, r3
#endif
        bl      SYMBOL_NAME(__divmodsi4)
        ldr     r1, [sp]
        add     sp, sp, #4
        pop     { pc }
#endif //  defined(USE_THUMB_1)
END_COMPILERRT_FUNCTION(__aeabi_idivmod)

NO_EXEC_STACK_DIRECTIVE

