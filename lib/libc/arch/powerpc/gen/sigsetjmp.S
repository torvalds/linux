/*	$OpenBSD: sigsetjmp.S,v 1.9 2023/12/10 16:45:51 deraadt Exp $ */
/*
 * Copyright (c) 1996 Dale Rahn. All rights reserved.
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

#define JMP_sigflag	0x00
#define JMP_r1		0x04
#define JMP_r14		0x08
#define JMP_r15		0x0c
#define JMP_r16		0x10
#define JMP_r17		0x14
#define JMP_r18		0x18
#define JMP_r19		0x1c
#define JMP_r20		0x20
#define JMP_r21		0x24
#define JMP_r22		0x28
#define JMP_r23		0x2c
#define JMP_r24		0x30
#define JMP_r25		0x34
#define JMP_r26		0x38
#define JMP_r27		0x3c
#define JMP_r28		0x40
#define JMP_r29		0x44
#define JMP_r30		0x48
#define JMP_r31		0x4c
#define JMP_lr		0x50
#define JMP_cr		0x54
#define JMP_ctr		0x58
#define JMP_xer		0x5c
#define JMP_sigmask	0x60

	.extern	__jmpxor

/* int sigsetjmp(sigjmp_buf env, int savemask) */
ENTRY(sigsetjmp)
	mr	5, 3			/* save jmpbuf addr in r5 */
	stw	4, JMP_sigflag(5)
	or.	4, 4, 4
	beq	1f
	li	3, 1			/* how = SIG_BLOCK */
	li	4, 0			/* oset = empty */
	li	0, SYS_sigprocmask
99:	sc
	PINSYSCALL(SYS_sigprocmask, 99b)
	stw	3, JMP_sigmask(5)
1:	mflr	6
	bcl	20, 31, 2f
2:	mflr	7
	addis	7, 7, __jmpxor-2b@ha
	addi	7, 7, __jmpxor-2b@l
	mtlr	6
	lwz	0, 0(7)			/* xor for r1 */
	lwz	7, 4(7)			/* xor for lr, overwrite addr */

	/* r1, r14-r31 */
	xor  0, 0, 1			/* use and overwrite the r1 xor */
	/* "mflr 6" done at 1: above */
	xor  7, 6, 7			/* use and overwrite the lr xor */
	stw 7, JMP_lr(5)
	stw  0, JMP_r1 (5)
	stw 14, JMP_r14(5)
	stw 15, JMP_r15(5)
	stw 16, JMP_r16(5)
	stw 17, JMP_r17(5)
	stw 18, JMP_r18(5)
	stw 19, JMP_r19(5)
	stw 20, JMP_r20(5)
	stw 21, JMP_r21(5)
	stw 22, JMP_r22(5)
	stw 23, JMP_r23(5)
	stw 24, JMP_r24(5)
	stw 25, JMP_r25(5)
	stw 26, JMP_r26(5)
	stw 27, JMP_r27(5)
	stw 28, JMP_r28(5)
	stw 29, JMP_r29(5)
	stw 30, JMP_r30(5)
	stw 31, JMP_r31(5)
	/* cr, lr, ctr, xer */
	mfcr 0
	stw 0, JMP_cr(5)
	mfctr 0
	stw 0, JMP_ctr(5)
	mfxer 0
	stw 0, JMP_xer(5)
	/* f14-f31, fpscr */
	li 3, 0
	blr
END(sigsetjmp)


/* int siglongjmp(sigjmp_buf env, int val) */
ENTRY(siglongjmp)
	mr	5, 3			/* save jmpbuf addr in r5 */
	mr	6, 4			/* save val in r6 */
	lwz	4, JMP_sigflag(5)	/* do we need to restore sigmask? */
	or.	4, 4, 4
	beq	1f

	li	3, 3			/* how = SIG_SETMASK */
	lwz	4, JMP_sigmask(5)	/* oset from the jmpbuf */
	li	0, SYS_sigprocmask
98:	sc
	PINSYSCALL(SYS_sigprocmask, 98b)

1:	bcl	20, 31, 2f
2:	mflr	9
	addis	9, 9, __jmpxor-2b@ha
	addi	9, 9, __jmpxor-2b@l
	lwz	8, 0(9)			/* xor for r1 */
	lwz	9, 4(9)			/* xor for lr, overwrite addr */

	/* r1, r14-r31 */
	lwz  0, JMP_r1(5)
	xor  1, 0, 8			/* use the r1 xor */
	lwz 14, JMP_r14(5)
	lwz 15, JMP_r15(5)
	lwz 16, JMP_r16(5)
	lwz 17, JMP_r17(5)
	lwz 18, JMP_r18(5)
	lwz 19, JMP_r19(5)
	lwz 20, JMP_r20(5)
	lwz 21, JMP_r21(5)
	lwz 22, JMP_r22(5)
	lwz 23, JMP_r23(5)
	lwz 24, JMP_r24(5)
	lwz 25, JMP_r25(5)
	lwz 26, JMP_r26(5)
	lwz 27, JMP_r27(5)
	lwz 28, JMP_r28(5)
	lwz 29, JMP_r29(5)
	lwz 30, JMP_r30(5)
	lwz 31, JMP_r31(5)
	/* cr, lr, ctr, xer */
	lwz 8, JMP_cr(5)		/* overwrite the r1 xor */
	mtcr 8
	lwz 0, JMP_lr(5)
	xor  0, 0, 9			/* use the lr xor */
	mtlr 0
	lwz 9, JMP_ctr(5)		/* overwrite the lr xor */
	mtctr 9
	lwz 0, JMP_xer(5)
	mtxer 0
	/* f14-f31, fpscr */

	/* if r6 == 0, return 1, not 0 */
	mr	3, 6
	cmpwi	6, 0
	bnelr
	li	3, 1
	blr
END(siglongjmp)
