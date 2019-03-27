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
	stmw	%r9,20(%r3)

	/* FPRs */
	stfd	%f14,112+0*8(%r3)
	stfd	%f15,112+1*8(%r3)
	stfd	%f16,112+2*8(%r3)
	stfd	%f17,112+3*8(%r3)
	stfd	%f18,112+4*8(%r3)
	stfd	%f19,112+5*8(%r3)
	stfd	%f20,112+6*8(%r3)
	stfd	%f21,112+7*8(%r3)
	stfd	%f22,112+8*8(%r3)
	stfd	%f23,112+9*8(%r3)
	stfd	%f24,112+10*8(%r3)
	stfd	%f25,112+11*8(%r3)
	stfd	%f26,112+12*8(%r3)
	stfd	%f27,112+13*8(%r3)
	stfd	%f28,112+14*8(%r3)
	stfd	%f29,112+15*8(%r3)
	stfd	%f30,112+16*8(%r3)
	stfd	%f31,112+17*8(%r3)

	li	%r3,0
	blr
END(_setjmp)

ENTRY(_longjmp)
	lmw	%r9,20(%r3)

	/* FPRs */
	lfd	%f14,112+0*8(%r3)
	lfd	%f15,112+1*8(%r3)
	lfd	%f16,112+2*8(%r3)
	lfd	%f17,112+3*8(%r3)
	lfd	%f18,112+4*8(%r3)
	lfd	%f19,112+5*8(%r3)
	lfd	%f20,112+6*8(%r3)
	lfd	%f21,112+7*8(%r3)
	lfd	%f22,112+8*8(%r3)
	lfd	%f23,112+9*8(%r3)
	lfd	%f24,112+10*8(%r3)
	lfd	%f25,112+11*8(%r3)
	lfd	%f26,112+12*8(%r3)
	lfd	%f27,112+13*8(%r3)
	lfd	%f28,112+14*8(%r3)
	lfd	%f29,112+15*8(%r3)
	lfd	%f30,112+16*8(%r3)
	lfd	%f31,112+17*8(%r3)

	mtlr	%r11
	mtcr	%r12
	mr	%r1,%r10
	or.	%r3,%r4,%r4
	bnelr
	li	%r3,1
	blr
END(_longjmp)

	.section .note.GNU-stack,"",%progbits
