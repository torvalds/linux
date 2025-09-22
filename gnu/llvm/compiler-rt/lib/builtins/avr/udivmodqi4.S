//===------------ udivmodqi4.S - uint8 div & mod --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// As described at
// https://gcc.gnu.org/wiki/avr-gcc#Exceptions_to_the_Calling_Convention, the
// prototype is `struct {uint8, uint8} __udivmodqi4(uint8, uint8)`.
// The uint8 quotient is returned via R24, and the uint8 remainder is returned
// via R25, while R23 is clobbered.
//
//===----------------------------------------------------------------------===//

	.text
	.align 2

	.globl __udivmodqi4
	.type  __udivmodqi4, @function

__udivmodqi4:
	sub     r25, r25           ; Initialize the remainder to zero.
	ldi     r23, 9             ; Only loop 8 rounds for uint8.

__udivmodqi4_loop:
	adc     r24, r24
	dec     r23
	breq    __udivmodqi4_end
	adc     r25, r25
	cp      r25, r22           ; Compare with the divisor.
	brcs    __udivmodqi4_loop
	sub     r25, r22           ; Subtract the divisor.
	rjmp    __udivmodqi4_loop

__udivmodqi4_end:
	com     r24                ; The uint8 quotient is returned via R24.
	ret                        ; The uint8 remainder is returned via R25.
