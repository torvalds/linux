//===------------- divmodhi4.S - sint16 div & mod -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// As described at
// https://gcc.gnu.org/wiki/avr-gcc#Exceptions_to_the_Calling_Convention, the
// prototype is `struct {sint16, sint16}  __divmodhi4(sint16, sint16)`.
// The sint16 quotient is returned via R23:R22, and the sint16 remainder is
// returned via R25:R24, while registers R21/R26/27/Rtmp and bit T in SREG
// are clobbered.
//
//===----------------------------------------------------------------------===//

	.text
	.align 2

#ifdef __AVR_TINY__
	.set __tmp_reg__, 16
#else
	.set __tmp_reg__, 0
#endif

	.globl __divmodhi4
	.type  __divmodhi4, @function

__divmodhi4:
	bst     r25, 7
	mov     __tmp_reg__, r23
	brtc    __divmodhi4_a
	com     __tmp_reg__
	rcall   __divmodhi4_b

__divmodhi4_a:
	sbrc    r23, 7
	rcall   __divmodhi4_c
	rcall   __udivmodhi4 ; Call __udivmodhi4 to do real calculation.
	sbrc    __tmp_reg__, 7
	rcall   __divmodhi4_c
	brtc    __divmodhi4_exit

__divmodhi4_b:
	com     r25
	neg     r24
	sbci    r25, 255
	ret                  ; Return quotient via R23:R22 and remainder via R25:R24.

__divmodhi4_c:
	com     r23
	neg     r22
	sbci    r23, 255

__divmodhi4_exit:
	ret                  ; Return quotient via R23:R22 and remainder via R25:r24.
