/*-
 * Copyright (c) 2002 Peter Grehan.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
/*      $NetBSD: _setjmp.S,v 1.1 1997/03/29 20:55:53 thorpej Exp $      */

#include <machine/asm.h>
__FBSDID("$FreeBSD$");

/*
 * C library -- _setjmp, _longjmp
 *
 *      _longjmp(a,v)
 * will generate a "return(v?v:1)" from the last call to
 *      _setjmp(a)
 * by restoring registers from the stack.
 * The previous signal state is NOT restored.
 *
 * jmpbuf layout:
 *     +------------+
 *     |   unused   |
 *     +------------+
 *     |   unused   |
 *     |            |
 *     | (4 words)  |
 *     |            |
 *     +------------+
 *     | saved regs |
 *     |    ...     |	
 */

ENTRY(_setjmp)
	mflr	%r11
	mfcr	%r12
	mr	%r10,%r1
	mr	%r9,%r2
	std	%r9,40 + 0*8(%r3)
	stfd	%f14,40 + 23*8(%r3)
	std	%r10,40 + 1*8(%r3)
	stfd	%f15,40 + 24*8(%r3)
	std	%r11,40 + 2*8(%r3)
	stfd	%f16,40 + 25*8(%r3)
	std	%r12,40 + 3*8(%r3)
	stfd	%f17,40 + 26*8(%r3)
	std	%r13,40 + 4*8(%r3)
	stfd	%f18,40 + 27*8(%r3)
	std	%r14,40 + 5*8(%r3)
	stfd	%f19,40 + 28*8(%r3)
	std	%r15,40 + 6*8(%r3)
	stfd	%f20,40 + 29*8(%r3)
	std	%r16,40 + 7*8(%r3)
	stfd	%f21,40 + 30*8(%r3)
	std	%r17,40 + 8*8(%r3)
	stfd	%f22,40 + 31*8(%r3)
	std	%r18,40 + 9*8(%r3)
	stfd	%f23,40 + 32*8(%r3)
	std	%r19,40 + 10*8(%r3)
	stfd	%f24,40 + 33*8(%r3)
	std	%r20,40 + 11*8(%r3)
	stfd	%f25,40 + 34*8(%r3)
	std	%r21,40 + 12*8(%r3)
	stfd	%f26,40 + 35*8(%r3)
	std	%r22,40 + 13*8(%r3)
	stfd	%f27,40 + 36*8(%r3)
	std	%r23,40 + 14*8(%r3)
	stfd	%f28,40 + 37*8(%r3)
	std	%r24,40 + 15*8(%r3)
	stfd	%f29,40 + 38*8(%r3)
	std	%r25,40 + 16*8(%r3)
	stfd	%f30,40 + 39*8(%r3)
	std	%r26,40 + 17*8(%r3)
	stfd	%f31,40 + 40*8(%r3)
	std	%r27,40 + 18*8(%r3)
	std	%r28,40 + 19*8(%r3)
	std	%r29,40 + 20*8(%r3)
	std	%r30,40 + 21*8(%r3)
	std	%r31,40 + 22*8(%r3)
	li	%r3,0
	blr
END(_setjmp)

ENTRY(_longjmp)
	ld	%r9,40 + 0*8(%r3)
	lfd	%f14,40 + 23*8(%r3)
	ld	%r10,40 + 1*8(%r3)
	lfd	%f15,40 + 24*8(%r3)
	ld	%r11,40 + 2*8(%r3)
	lfd	%f16,40 + 25*8(%r3)
	ld	%r12,40 + 3*8(%r3)
	lfd	%f17,40 + 26*8(%r3)
	ld	%r14,40 + 5*8(%r3)
	lfd	%f18,40 + 27*8(%r3)
	ld	%r15,40 + 6*8(%r3)
	lfd	%f19,40 + 28*8(%r3)
	ld	%r16,40 + 7*8(%r3)
	lfd	%f20,40 + 29*8(%r3)
	ld	%r17,40 + 8*8(%r3)
	lfd	%f21,40 + 30*8(%r3)
	ld	%r18,40 + 9*8(%r3)
	lfd	%f22,40 + 31*8(%r3)
	ld	%r19,40 + 10*8(%r3)
	lfd	%f23,40 + 32*8(%r3)
	ld	%r20,40 + 11*8(%r3)
	lfd	%f24,40 + 33*8(%r3)
	ld	%r21,40 + 12*8(%r3)
	lfd	%f25,40 + 34*8(%r3)
	ld	%r22,40 + 13*8(%r3)
	lfd	%f26,40 + 35*8(%r3)
	ld	%r23,40 + 14*8(%r3)
	lfd	%f27,40 + 36*8(%r3)
	ld	%r24,40 + 15*8(%r3)
	lfd	%f28,40 + 37*8(%r3)
	ld	%r25,40 + 16*8(%r3)
	lfd	%f29,40 + 38*8(%r3)
	ld	%r26,40 + 17*8(%r3)
	lfd	%f30,40 + 39*8(%r3)
	ld	%r27,40 + 18*8(%r3)
	lfd	%f31,40 + 40*8(%r3)
	ld	%r28,40 + 19*8(%r3)
	ld	%r29,40 + 20*8(%r3)
	ld	%r30,40 + 21*8(%r3)
	ld	%r31,40 + 22*8(%r3)

	mtlr	%r11
	mtcr	%r12
	mr	%r2,%r9
	mr	%r1,%r10
	or.	%r3,%r4,%r4
	bnelr
	li	%r3,1
	blr
END(_longjmp)

	.section .note.GNU-stack,"",%progbits
