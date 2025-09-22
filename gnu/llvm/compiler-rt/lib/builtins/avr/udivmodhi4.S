//===------------ udivmodhi4.S - uint16 div & mod -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// As described at
// https://gcc.gnu.org/wiki/avr-gcc#Exceptions_to_the_Calling_Convention, the
// prototype is `struct {uint16, uint16}  __udivmodhi4(uint16, uint16)`.
// The uint16 quotient is returned via R23:R22, and the uint16 remainder is
// returned via R25:R24, while R21/R26/R27 are clobbered.
//
//===----------------------------------------------------------------------===//

	.text
	.align 2

	.globl __udivmodhi4
	.type  __udivmodhi4, @function

__udivmodhi4:
	sub     r26, r26
	sub     r27, r27           ; Initialize the remainder to zero.
	ldi     r21, 17            ; Only loop 16 rounds for uint16.

__udivmodhi4_loop:
	adc     r24, r24
	adc     r25, r25
	dec     r21
	breq    __udivmodhi4_end
	adc     r26, r26
	adc     r27, r27
	cp      r26, r22
	cpc     r27, r23           ; Compare with the divisor.
	brcs    __udivmodhi4_loop
	sub     r26, r22
	sbc     r27, r23           ; Subtract the divisor.
	rjmp    __udivmodhi4_loop

__udivmodhi4_end:
	com     r24
	com     r25
	mov     r22, r24
	mov     r23, r25           ; The quotient is returned in R23:R22.
	mov     r24, r26
	mov     r25, r27           ; The remainder is returned in in R25:R24.
	ret
