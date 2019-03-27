/*-
 * Copyright (c) 2016 Justin Hibbits
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
	evstdd	%r1,24+0*8(%r3)
	evstdd	%r2,24+1*8(%r3)
	evstdd	%r11,24+2*8(%r3)
	evstdd	%r12,24+3*8(%r3)
	evstdd	%r13,24+4*8(%r3)
	evstdd	%r14,24+5*8(%r3)
	evstdd	%r15,24+6*8(%r3)
	evstdd	%r16,24+7*8(%r3)
	evstdd	%r17,24+8*8(%r3)
	evstdd	%r18,24+9*8(%r3)
	evstdd	%r19,24+10*8(%r3)
	evstdd	%r20,24+11*8(%r3)
	evstdd	%r21,24+12*8(%r3)
	evstdd	%r22,24+13*8(%r3)
	evstdd	%r23,24+14*8(%r3)
	evstdd	%r24,24+15*8(%r3)
	evstdd	%r25,24+16*8(%r3)
	evstdd	%r26,24+17*8(%r3)
	evstdd	%r27,24+18*8(%r3)
	evstdd	%r28,24+19*8(%r3)
	evstdd	%r29,24+20*8(%r3)
	evstdd	%r30,24+21*8(%r3)
	evstdd	%r31,24+22*8(%r3)

	li	%r3,0
	blr
END(_setjmp)

ENTRY(_longjmp)
	evldd	%r1,24+0*8(%r3)
	evldd	%r2,24+1*8(%r3)
	evldd	%r11,24+2*8(%r3)
	evldd	%r12,24+3*8(%r3)
	evldd	%r13,24+4*8(%r3)
	evldd	%r14,24+5*8(%r3)
	evldd	%r15,24+6*8(%r3)
	evldd	%r16,24+7*8(%r3)
	evldd	%r17,24+8*8(%r3)
	evldd	%r18,24+9*8(%r3)
	evldd	%r19,24+10*8(%r3)
	evldd	%r20,24+11*8(%r3)
	evldd	%r21,24+12*8(%r3)
	evldd	%r22,24+13*8(%r3)
	evldd	%r23,24+14*8(%r3)
	evldd	%r24,24+15*8(%r3)
	evldd	%r25,24+16*8(%r3)
	evldd	%r26,24+17*8(%r3)
	evldd	%r27,24+18*8(%r3)
	evldd	%r28,24+19*8(%r3)
	evldd	%r29,24+20*8(%r3)
	evldd	%r30,24+21*8(%r3)
	evldd	%r31,24+22*8(%r3)

	mtlr	%r11
	mtcr	%r12
	or.	%r3,%r4,%r4
	bnelr
	li	%r3,1
	blr
END(_longjmp)

	.section .note.GNU-stack,"",%progbits
