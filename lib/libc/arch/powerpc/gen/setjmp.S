/*	$OpenBSD: setjmp.S,v 1.15 2023/12/10 16:45:51 deraadt Exp $ */
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

/* int setjmp(jmp_buf env) */

#define JMP_r1	0x04
#define JMP_r14	0x08
#define JMP_r15	0x0c
#define JMP_r16	0x10
#define JMP_r17	0x14
#define JMP_r18	0x18
#define JMP_r19	0x1c
#define JMP_r20	0x20
#define JMP_r21	0x24
#define JMP_r22	0x28
#define JMP_r23	0x2c
#define JMP_r24	0x30
#define JMP_r25	0x34
#define JMP_r26	0x38
#define JMP_r27	0x3c
#define JMP_r28	0x40
#define JMP_r29	0x44
#define JMP_r30	0x48
#define JMP_r31	0x4c
#define JMP_lr  0x50
#define JMP_cr  0x54
#define JMP_ctr	0x58
#define JMP_xer	0x5c
#define JMP_sig	0x60

	.section	.openbsd.randomdata,"aw",@progbits
	.balign	4
	.globl	__jmpxor
	.hidden	__jmpxor
__jmpxor:
	.zero	4*2		# (r1, lr)
	END(__jmpxor)
	.type	__jmpxor,@object


ENTRY(setjmp)
	mr	5, 3			/* save jmpbuf addr in r5 */
	li	3, 1			/* how = SIG_BLOCK */
	li	4, 0			/* oset = empty */
	li	0, SYS_sigprocmask
99:	sc
	PINSYSCALL(SYS_sigprocmask, 99b)

	stw	3, JMP_sig(5)
	mr	3, 5
ENTRY(_setjmp)
	mflr	6
	bcl	20, 31, 1f
1:	mflr	7
	addis	7, 7, __jmpxor-1b@ha
	addi	7, 7, __jmpxor-1b@l
	mtlr	6
	lwz	0, 0(7)			/* xor for r1 */
	lwz	7, 4(7)			/* xor for lr, overwrite addr */

	/* r1, r14-r31 */
	xor  0, 0, 1			/* use and overwrite the r1 xor */
	stw  0, JMP_r1 (3)
	stw 14, JMP_r14(3)
	stw 15, JMP_r15(3)
	stw 16, JMP_r16(3)
	stw 17, JMP_r17(3)
	stw 18, JMP_r18(3)
	stw 19, JMP_r19(3)
	stw 20, JMP_r20(3)
	stw 21, JMP_r21(3)
	stw 22, JMP_r22(3)
	stw 23, JMP_r23(3)
	stw 24, JMP_r24(3)
	stw 25, JMP_r25(3)
	stw 26, JMP_r26(3)
	stw 27, JMP_r27(3)
	stw 28, JMP_r28(3)
	stw 29, JMP_r29(3)
	stw 30, JMP_r30(3)
	stw 31, JMP_r31(3)
	/* cr, lr, ctr, xer */
	mfcr 0
	stw 0, JMP_cr(3)
	/* "mflr 6" done at start of _setjmp() */
	xor  7, 6, 7			/* use and overwrite the lr xor */
	stw 7, JMP_lr(3)
	mfctr 0
	stw 0, JMP_ctr(3)
	mfxer 0
	stw 0, JMP_xer(3)
	/* f14-f31, fpscr */
	li 3, 0
	blr
END(_setjmp)
END(setjmp)


ENTRY(longjmp)
	mr	5, 3			/* save jmpbuf addr in r5 */
	mr	6, 4			/* save val in r6 */
	li	3, 3			/* how = SIG_SETMASK */
	lwz	4, JMP_sig(5)		/* oset from the jmpbuf */
	li	0, SYS_sigprocmask
98:	sc
	PINSYSCALL(SYS_sigprocmask, 98b)
	mr	3, 5			/* restore jmpbuf and val to r3,r4 */
	mr	4, 6

ENTRY(_longjmp)
	bcl	20, 31, 1f
1:	mflr	9
	addis	9, 9, __jmpxor-1b@ha
	addi	9, 9, __jmpxor-1b@l
	lwz	8, 0(9)			/* xor for r1 */
	lwz	9, 4(9)			/* xor for lr, overwrite addr */

	/* r1, r14-r30 */
	lwz  0, JMP_r1 (3)
	xor  1, 0, 8			/* use the r1 xor */
	lwz 14, JMP_r14(3)
	lwz 15, JMP_r15(3)
	lwz 16, JMP_r16(3)
	lwz 17, JMP_r17(3)
	lwz 18, JMP_r18(3)
	lwz 19, JMP_r19(3)
	lwz 20, JMP_r20(3)
	lwz 21, JMP_r21(3)
	lwz 22, JMP_r22(3)
	lwz 23, JMP_r23(3)
	lwz 24, JMP_r24(3)
	lwz 25, JMP_r25(3)
	lwz 26, JMP_r26(3)
	lwz 27, JMP_r27(3)
	lwz 28, JMP_r28(3)
	lwz 29, JMP_r29(3)
	lwz 30, JMP_r30(3)
	lwz 31, JMP_r31(3)
	/* cr, lr, ctr, xer */
	lwz 8, JMP_cr(3)		/* overwrite the r1 xor */
	mtcr 8
	lwz 0, JMP_lr(3)
	xor  0, 0, 9			/* use the lr xor */
	mtlr 0
	lwz 9, JMP_ctr(3)		/* overwrite the lr xor */
	mtctr 9
	lwz 0, JMP_xer(3)
	mtxer 0
	/* f14-f31, fpscr */

	/* if r4 == 0, return 1, not 0 */
	mr	3, 4
	cmpwi	4, 0
	bnelr
	li	3, 1
	blr
END(_longjmp)
END(longjmp)
