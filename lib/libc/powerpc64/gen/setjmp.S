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
/*      $NetBSD: setjmp.S,v 1.3 1998/10/03 12:30:38 tsubai Exp $        */

#include <machine/asm.h>
__FBSDID("$FreeBSD$");

#include <sys/syscall.h>
	
/*
 * C library -- setjmp, longjmp
 *
 *      longjmp(a,v)
 * will generate a "return(v?v:1)" from the last call to
 *      setjmp(a)
 * by restoring registers from the stack.
 * The previous signal state is restored.
 *
 * jmpbuf layout:
 *     +------------+
 *     |   unused   |
 *     +------------+
 *     | sig state  |
 *     |            |
 *     | (4 words)  |
 *     |            |
 *     +------------+
 *     | saved regs |
 *     |    ...     |
 */

ENTRY(setjmp)
	mr	%r6,%r3
	li	%r3,1			/* SIG_BLOCK, but doesn't matter */
					/*            since set == NULL  */
	li	%r4,0			/* set = NULL */
	mr	%r5,%r6			/* &oset */
	addi	%r5,%r5,4
	li	%r0, SYS_sigprocmask	/*sigprocmask(SIG_BLOCK, NULL, &oset)*/
	sc				/*assume no error       XXX */
	mflr	%r11			/* r11 <- link reg */
	mfcr	%r12			/* r12 <- condition reg */
	mr	%r10,%r1		/* r10 <- stackptr */
	mr	%r9,%r2			/*  r9 <- global ptr */

	std	%r9,40 + 0*8(%r6)
	stfd	%f14,40 + 23*8(%r6)
	std	%r10,40 + 1*8(%r6)
	stfd	%f15,40 + 24*8(%r6)
	std	%r11,40 + 2*8(%r6)
	stfd	%f16,40 + 25*8(%r6)
	std	%r12,40 + 3*8(%r6)
	stfd	%f17,40 + 26*8(%r6)
	std	%r13,40 + 4*8(%r6)
	stfd	%f18,40 + 27*8(%r6)
	std	%r14,40 + 5*8(%r6)
	stfd	%f19,40 + 28*8(%r6)
	std	%r15,40 + 6*8(%r6)
	stfd	%f20,40 + 29*8(%r6)
	std	%r16,40 + 7*8(%r6)
	stfd	%f21,40 + 30*8(%r6)
	std	%r17,40 + 8*8(%r6)
	stfd	%f22,40 + 31*8(%r6)
	std	%r18,40 + 9*8(%r6)
	stfd	%f23,40 + 32*8(%r6)
	std	%r19,40 + 10*8(%r6)
	stfd	%f24,40 + 33*8(%r6)
	std	%r20,40 + 11*8(%r6)
	stfd	%f25,40 + 34*8(%r6)
	std	%r21,40 + 12*8(%r6)
	stfd	%f26,40 + 35*8(%r6)
	std	%r22,40 + 13*8(%r6)
	stfd	%f27,40 + 36*8(%r6)
	std	%r23,40 + 14*8(%r6)
	stfd	%f28,40 + 37*8(%r6)
	std	%r24,40 + 15*8(%r6)
	stfd	%f29,40 + 38*8(%r6)
	std	%r25,40 + 16*8(%r6)
	stfd	%f30,40 + 39*8(%r6)
	std	%r26,40 + 17*8(%r6)
	stfd	%f31,40 + 40*8(%r6)
	std	%r27,40 + 18*8(%r6)
	std	%r28,40 + 19*8(%r6)
	std	%r29,40 + 20*8(%r6)
	std	%r30,40 + 21*8(%r6)
	std	%r31,40 + 22*8(%r6)

	/* XXX Altivec regs */

	li	%r3,0			/* return (0) */
	blr
END(setjmp)

	WEAK_REFERENCE(__longjmp, longjmp)
ENTRY(__longjmp)
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
	mr	%r6,%r4			/* save val param */
	mtlr	%r11			/* r11 -> link reg */
	mtcr	%r12			/* r12 -> condition reg */
	mr	%r2,%r9			/* r9  -> global ptr */
	mr	%r1,%r10		/* r10 -> stackptr */
	mr	%r4,%r3
	li	%r3,3			/* SIG_SETMASK */
	addi	%r4,%r4,4		/* &set */
	li	%r5,0			/* oset = NULL */
	li	%r0,SYS_sigprocmask	/* sigprocmask(SIG_SET, &set, NULL) */
	sc                              /* assume no error       XXX */
	or.	%r3,%r6,%r6
	bnelr
	li	%r3,1
	blr
END(__longjmp)

	.section .note.GNU-stack,"",%progbits
