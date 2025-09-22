//===------------- divmodqi4.S - sint8 div & mod --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// As described at
// https://gcc.gnu.org/wiki/avr-gcc#Exceptions_to_the_Calling_Convention, the
// prototype is `struct {sint8, sint8}  __divmodqi4(sint8, sint8)`.
// The sint8 quotient is returned via R24, and the sint8 remainder is returned
// via R25, while registers R23/Rtmp and bit T in SREG are clobbered.
//
//===----------------------------------------------------------------------===//

	.text
	.align 2

#ifdef __AVR_TINY__
	.set __tmp_reg__, 16
#else
	.set __tmp_reg__, 0
#endif

	.globl __divmodqi4
	.type  __divmodqi4, @function

__divmodqi4:
	bst     r24, 7
	mov     __tmp_reg__, r24
	eor     __tmp_reg__, r22
	sbrc    r24, 7
	neg     r24
	sbrc    r22, 7
	neg     r22
	rcall   __udivmodqi4   ; Call __udivmodqi4 to do real calculation.
	brtc    __divmodqi4_1
	neg     r25

__divmodqi4_1:
	sbrc    __tmp_reg__, 7
	neg     r24
	ret                    ; Return quotient via R24 and remainder via R25.
