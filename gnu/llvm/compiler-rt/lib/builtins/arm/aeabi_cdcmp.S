//===-- aeabi_cdcmp.S - EABI cdcmp* implementation ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "../assembly.h"

#define APSR_Z (1 << 30)
#define APSR_C (1 << 29)

// void __aeabi_cdcmpeq(double a, double b) {
//   if (isnan(a) || isnan(b)) {
//     Z = 0; C = 1;
//   } else {
//     __aeabi_cdcmple(a, b);
//   }
// }

        .syntax unified
        .p2align 2
DEFINE_COMPILERRT_FUNCTION(__aeabi_cdcmpeq)
        push {r0-r3, lr}
        bl __aeabi_cdcmpeq_check_nan
        cmp r0, #1
#if defined(USE_THUMB_1)
        beq 1f
        // NaN has been ruled out, so __aeabi_cdcmple can't trap
        mov r0, sp
        ldm r0, {r0-r3}
        bl __aeabi_cdcmple
        pop {r0-r3, pc}
1:
        // Z = 0, C = 1
        movs r0, #0xF
        lsls r0, r0, #31
        pop {r0-r3, pc}
#else
        pop {r0-r3, lr}

        // NaN has been ruled out, so __aeabi_cdcmple can't trap
        // Use "it ne" + unconditional branch to guarantee a supported relocation if
        // __aeabi_cdcmple is in a different section for some builds.
        IT(ne)
        bne __aeabi_cdcmple

#if defined(USE_THUMB_2)
        mov ip, #APSR_C
        msr APSR_nzcvq, ip
#else
        msr APSR_nzcvq, #APSR_C
#endif
        JMP(lr)
#endif
END_COMPILERRT_FUNCTION(__aeabi_cdcmpeq)


// void __aeabi_cdcmple(double a, double b) {
//   if (__aeabi_dcmplt(a, b)) {
//     Z = 0; C = 0;
//   } else if (__aeabi_dcmpeq(a, b)) {
//     Z = 1; C = 1;
//   } else {
//     Z = 0; C = 1;
//   }
// }

        .syntax unified
        .p2align 2
DEFINE_COMPILERRT_FUNCTION(__aeabi_cdcmple)
        // Per the RTABI, this function must preserve r0-r11.
        // Save lr in the same instruction for compactness
        push {r0-r3, lr}

        bl __aeabi_dcmplt
        cmp r0, #1
#if defined(USE_THUMB_1)
        bne 1f
        // Z = 0, C = 0
        movs r0, #1
        lsls r0, r0, #1
        pop {r0-r3, pc}
1:
        mov r0, sp
        ldm r0, {r0-r3}
        bl __aeabi_dcmpeq
        cmp r0, #1
        bne 2f
        // Z = 1, C = 1
        movs r0, #2
        lsls r0, r0, #31
        pop {r0-r3, pc}
2:
        // Z = 0, C = 1
        movs r0, #0xF
        lsls r0, r0, #31
        pop {r0-r3, pc}
#else
        ITT(eq)
        moveq ip, #0
        beq 1f

        ldm sp, {r0-r3}
        bl __aeabi_dcmpeq
        cmp r0, #1
        ITE(eq)
        moveq ip, #(APSR_C | APSR_Z)
        movne ip, #(APSR_C)

1:
        msr APSR_nzcvq, ip
        pop {r0-r3}
        POP_PC()
#endif
END_COMPILERRT_FUNCTION(__aeabi_cdcmple)

// int __aeabi_cdrcmple(double a, double b) {
//   return __aeabi_cdcmple(b, a);
// }

        .syntax unified
        .p2align 2
DEFINE_COMPILERRT_FUNCTION(__aeabi_cdrcmple)
        // Swap r0 and r2
        mov ip, r0
        mov r0, r2
        mov r2, ip

        // Swap r1 and r3
        mov ip, r1
        mov r1, r3
        mov r3, ip

        b __aeabi_cdcmple
END_COMPILERRT_FUNCTION(__aeabi_cdrcmple)

NO_EXEC_STACK_DIRECTIVE

