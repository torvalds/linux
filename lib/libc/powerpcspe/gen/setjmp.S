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
	evstdd	%r9,24+0*8(%r6)
	evstdd	%r10,24+1*8(%r6)
	evstdd	%r11,24+2*8(%r6)
	evstdd	%r12,24+3*8(%r6)
	evstdd	%r13,24+4*8(%r6)
	evstdd	%r14,24+5*8(%r6)
	evstdd	%r15,24+6*8(%r6)
	evstdd	%r16,24+7*8(%r6)
	evstdd	%r17,24+8*8(%r6)
	evstdd	%r18,24+9*8(%r6)
	evstdd	%r19,24+10*8(%r6)
	evstdd	%r20,24+11*8(%r6)
	evstdd	%r21,24+12*8(%r6)
	evstdd	%r22,24+13*8(%r6)
	evstdd	%r23,24+14*8(%r6)
	evstdd	%r24,24+15*8(%r6)
	evstdd	%r25,24+16*8(%r6)
	evstdd	%r26,24+17*8(%r6)
	evstdd	%r27,24+18*8(%r6)
	evstdd	%r28,24+19*8(%r6)
	evstdd	%r29,24+20*8(%r6)
	evstdd	%r30,24+21*8(%r6)
	evstdd	%r31,24+22*8(%r6)

	li	%r3,0			/* return (0) */
	blr
END(setjmp)

	WEAK_REFERENCE(CNAME(__longjmp), longjmp)
ENTRY(__longjmp)
	evldd	%r9,24+0*8(%r3)
	evldd	%r10,24+1*8(%r3)
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

	mr	%r6,%r4			/* save val param */
	mtlr	%r11			/* r11 -> link reg */
	mtcr	%r12			/* r12 -> condition reg */
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
