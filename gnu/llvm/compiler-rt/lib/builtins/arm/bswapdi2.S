//===------- bswapdi2 - Implement bswapdi2 --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "../assembly.h"

	.syntax unified
	.text
	DEFINE_CODE_STATE

//
// extern uint64_t __bswapdi2(uint64_t);
//
// Reverse all the bytes in a 64-bit integer.
//
	.p2align 2
DEFINE_COMPILERRT_FUNCTION(__bswapdi2)
#if __ARM_ARCH < 6
    // before armv6 does not have "rev" instruction
    // r2 = rev(r0)
    eor r2, r0, r0, ror #16
    bic r2, r2, #0xff0000
    mov r2, r2, lsr #8
    eor r2, r2, r0, ror #8
    // r0 = rev(r1)
    eor r0, r1, r1, ror #16
    bic r0, r0, #0xff0000
    mov r0, r0, lsr #8
    eor r0, r0, r1, ror #8
#else
    rev r2, r0  // r2 = rev(r0)
    rev r0, r1  // r0 = rev(r1)
#endif
    mov r1, r2  // r1 = r2 = rev(r0)
    JMP(lr)
END_COMPILERRT_FUNCTION(__bswapdi2)

NO_EXEC_STACK_DIRECTIVE

