/*	$OpenBSD: sigsetjmp.S,v 1.4 2023/12/10 16:45:52 deraadt Exp $ */
/*
 * Copyright (c) 2020 Dale Rahn. All rights reserved.
 *
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */  

#include "SYS.h"
#include <machine/asm.h>

#define JMP_sigflag	0x00
#define JMP_r1	0x08
#define JMP_r14	0x10
#define JMP_r15	0x18
#define JMP_r16	0x20
#define JMP_r17	0x28
#define JMP_r18	0x30
#define JMP_r19	0x38
#define JMP_r20	0x40
#define JMP_r21	0x48
#define JMP_r22	0x50
#define JMP_r23	0x58
#define JMP_r24	0x60
#define JMP_r25	0x68
#define JMP_r26	0x70
#define JMP_r27	0x78
#define JMP_r28	0x80
#define JMP_r29	0x88
#define JMP_r30	0x90
#define JMP_r31	0x98
#define JMP_lr  0xa0
#define JMP_cr  0xa8
#define JMP_ctr	0xb0
#define JMP_xer	0xb8
#define JMP_sig	0xc0
#define JMP_sigmask   0xc8


	.extern	__jmpxor

/* int sigsetjmp(sigjmp_buf env, int savemask) */
ENTRY(sigsetjmp)
	RETGUARD_SETUP(sigsetjmp, %r11)
	mr	%r5, %r3		/* save jmpbuf addr in r5 */
	std	%r4, JMP_sigflag(%r5)
	or.	%r4, %r4, %r4
	beq	1f
	li	%r3, 1			/* how = SIG_BLOCK */
	li	%r4, 0			/* oset = empty */
	li	%r0, SYS_sigprocmask
99:	sc
	PINSYSCALL(SYS_sigprocmask, 99b)
	nop
	std	%r3, JMP_sigmask(5)
1:
	addis	%r7, %r2, __jmpxor@toc@ha
	addi	%r7, %r7, __jmpxor@toc@l
	ld	%r8, 0(%r7)		/* xor for r1 */
	ld	%r7, 8(%r7)		/* xor for lr, overwrite addr */

	/*	r1, r14-r31 */
	xor	%r0, %r8, %r1		/* use and overwrite the r1 xor */
	std	%r0, JMP_r1 (%r5)
	std	%r14, JMP_r14(%r5)
	std	%r15, JMP_r15(%r5)
	std	%r16, JMP_r16(%r5)
	std	%r17, JMP_r17(%r5)
	std	%r18, JMP_r18(%r5)
	std	%r19, JMP_r19(%r5)
	std	%r20, JMP_r20(%r5)
	std	%r21, JMP_r21(%r5)
	std	%r22, JMP_r22(%r5)
	std	%r23, JMP_r23(%r5)
	std	%r24, JMP_r24(%r5)
	std	%r25, JMP_r25(%r5)
	std	%r26, JMP_r26(%r5)
	std	%r27, JMP_r27(%r5)
	std	%r28, JMP_r28(%r5)
	std	%r29, JMP_r29(%r5)
	std	%r30, JMP_r30(%r5)
	std	%r31, JMP_r31(%r5)
	/* lr, cr, ctr, xer */
	mflr	%r0
	xor	%r7, %r0, %r7		/* use and overwrite the lr xor */
	std	%r7, JMP_lr(%r5)
	mfcr	%r0
	std	%r0, JMP_cr(%r5)
	mfctr	%r0
	std	%r0, JMP_ctr(%r5)
	mfctr	%r0
	mfxer	%r0
	std	%r0, JMP_xer(%r5)
	/* f14-f31, fpscr */
	li %r3, 0
	RETGUARD_CHECK(sigsetjmp, %r11)
	blr
END(sigsetjmp)


/* int siglongjmp(sigjmp_buf env, int val) */
ENTRY(siglongjmp)
	mr	%r5, %r3		/* save jmpbuf addr in r5 */
	mr	%r6, %r4		/* save val in r6 */
	ld	%r4, JMP_sigflag(%r5)	/* do we need to restore sigmask? */
	or.	%r4, %r4, %r4
	beq	1f

	li	%r3, 3			/* how = SIG_SETMASK */
	ld	%r4, JMP_sigmask(%r5)	/* oset from the jmpbuf */
	li	%r0, SYS_sigprocmask
98:	sc
	PINSYSCALL(SYS_sigprocmask, 98b)
1:
	addis	%r9, %r2, __jmpxor@toc@ha
	addi	%r9, %r9, __jmpxor@toc@l
	ld	%r8, 0(%r9)			/* xor for r1 */
	ld	%r9, 8(%r9)			/* xor for lr, overwrite addr */
	ld	%r0, JMP_lr(%r5)
	xor	%r0, %r0, %r9			/* use the lr xor */
	mtlr	%r0
	RETGUARD_SETUP(siglongjmp, %r11)

	/* r1, r14-r31 */
	ld	%r0, JMP_r1(%r5)
	xor	%r1, %r0, %r8			/* use the r1 xor */
	ld	%r14, JMP_r14(%r5)
	ld	%r15, JMP_r15(%r5)
	ld	%r16, JMP_r16(%r5)
	ld	%r17, JMP_r17(%r5)
	ld	%r18, JMP_r18(%r5)
	ld	%r19, JMP_r19(%r5)
	ld	%r20, JMP_r20(%r5)
	ld	%r21, JMP_r21(%r5)
	ld	%r22, JMP_r22(%r5)
	ld	%r23, JMP_r23(%r5)
	ld	%r24, JMP_r24(%r5)
	ld	%r25, JMP_r25(%r5)
	ld	%r26, JMP_r26(%r5)
	ld	%r27, JMP_r27(%r5)
	ld	%r28, JMP_r28(%r5)
	ld	%r29, JMP_r29(%r5)
	ld	%r30, JMP_r30(%r5)
	ld	%r31, JMP_r31(%r5)
	/* cr, lr, ctr, xer */
	ld	%r8, JMP_cr(%r5)		/* overwrite the r1 xor */
	mtcr	%r8
	ld	%r9, JMP_ctr(%r5)		/* overwrite the lr xor */
	mtctr	%r9
	ld	%r0, JMP_xer(%r5)
	mtxer	%r0
	/* floating point is all caller save */

	/* if return val in r6 == 0, return 1, not 0 */
	mr	%r3, %r6
	cmpwi	%r6, 0
	bne	2f
	li	%r3, 1
2:
	RETGUARD_CHECK(siglongjmp, %r11)
	blr
END(siglongjmp)
