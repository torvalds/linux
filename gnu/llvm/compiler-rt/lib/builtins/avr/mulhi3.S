//===------------ mulhi3.S - int16 multiplication -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The corresponding C code is something like:
//
// int __mulhi3(int A, int B) {
//   int S = 0;
//   while (A != 0) {
//     if (A & 1)
//       S += B;
//     A = ((unsigned int) A) >> 1;
//     B <<= 1;
//   }
//   return S;
// }
//
// __mulhi3 has special ABI, as the implementation of libgcc, R25:R24 is used
// to return result, while Rtmp/R21/R22/R23 are clobbered.
//
//===----------------------------------------------------------------------===//

	.text
	.align 2

#ifdef __AVR_TINY__
	.set __tmp_reg__, 16
	.set __zero_reg__, 17
#else
	.set __tmp_reg__, 0
	.set __zero_reg__, 1
#endif

	.globl __mulhi3
	.type  __mulhi3, @function

__mulhi3:
	; Use Rzero:Rtmp to store the result.
	clr   __tmp_reg__
	clr   __zero_reg__                 ; S = 0;

__mulhi3_loop:
	clr   r21
	cp    r24, r21
	cpc   r25, r21
	breq  __mulhi3_end                 ; while (A != 0) {

	mov   r21, r24
	andi  r21, 1
	breq  __mulhi3_loop_a              ;   if (A & 1)
	add   __tmp_reg__, r22
	adc   __zero_reg__, r23            ;     S += B;

__mulhi3_loop_a:
	lsr   r25
	ror   r24                          ;   A = ((unsigned int) A) >> 1;
	lsl   r22
	rol   r23                          ;   B <<= 1;
	rjmp  __mulhi3_loop                ; }

__mulhi3_end:
	; Return the result via R25:R24.
	mov   r24, __tmp_reg__
	mov   r25, __zero_reg__
	; Restore __zero_reg__ to 0.
	clr   __zero_reg__
	ret                                ; return S;
