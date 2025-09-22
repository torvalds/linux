//===------------ mulhi3.S - int8 multiplication --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The corresponding C code is something like:
//
// char __mulqi3(char A, char B) {
//   int S = 0;
//   while (A != 0) {
//     if (A & 1)
//       S += B;
//     B <<= 1;
//     A = ((unsigned char) A) >> 1;
//   }
//   return S;
// }
//
// __mulqi3 has special ABI, as the implementation of libgcc, the result is
// returned via R24, while Rtmp and R22 are clobbered.
//
//===----------------------------------------------------------------------===//

	.text
	.align 2

#ifdef __AVR_TINY__
	.set __tmp_reg__, 16
#else
	.set __tmp_reg__, 0
#endif

	.globl __mulqi3
	.type  __mulqi3, @function

__mulqi3:
	clr   __tmp_reg__              ; S = 0;

__mulqi3_loop:
	cpi   r24, 0
	breq  __mulqi3_end             ; while (A != 0) {
	sbrc  r24, 0                   ;   if (A & 1)
	add   __tmp_reg__, r22         ;     S += B;
	add   r22, r22                 ;   B <<= 1;
	lsr   r24                      ;   A = ((unsigned char) A) >> 1;
	rjmp  __mulqi3_loop            ; }

__mulqi3_end:
	mov   r24, __tmp_reg__
	ret                            ; return S;
