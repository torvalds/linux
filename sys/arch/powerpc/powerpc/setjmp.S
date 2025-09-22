/*	$OpenBSD: setjmp.S,v 1.11 2022/12/08 01:25:45 guenther Exp $	*/
/* kernel version of this file, does not have signal goop */
/* int setjmp(jmp_buf env) */

/*
 * Copyright (c) 1996 Dale Rahn.
 * All rights reserved.
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

#include <machine/asm.h>
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


ENTRY(setjmp)
	mflr %r12
	RETGUARD_SETUP_LATE(setjmp, %r11, %r12)
	stw %r31, JMP_r31(%r3)
	/* r1, r14-r30 */
	stw %r1,  JMP_r1 (%r3)
	stw %r14, JMP_r14(%r3)
	stw %r15, JMP_r15(%r3)
	stw %r16, JMP_r16(%r3)
	stw %r17, JMP_r17(%r3)
	stw %r18, JMP_r18(%r3)
	stw %r19, JMP_r19(%r3)
	stw %r20, JMP_r20(%r3)
	stw %r21, JMP_r21(%r3)
	stw %r22, JMP_r22(%r3)
	stw %r23, JMP_r23(%r3)
	stw %r24, JMP_r24(%r3)
	stw %r25, JMP_r25(%r3)
	stw %r26, JMP_r26(%r3)
	stw %r27, JMP_r27(%r3)
	stw %r28, JMP_r28(%r3)
	stw %r29, JMP_r29(%r3)
	stw %r30, JMP_r30(%r3)
	/* cr, lr, ctr, xer */
	mfcr %r0
	stw %r0, JMP_cr(%r3)
	stw %r12, JMP_lr(%r3)
	mfctr %r0
	stw %r0, JMP_ctr(%r3)
	mfxer %r0
	stw %r0, JMP_xer(%r3)
	/* f14-f31, fpscr */
	li %r3, 0
	RETGUARD_CHECK(setjmp, %r11, %r12)
	blr


ENTRY(longjmp)
	/* lr, r31 */
	lwz %r12, JMP_lr(%r3)
	RETGUARD_SETUP_LATE(longjmp, %r11, %r12)
	mtlr %r12
	lwz %r31, JMP_r31(%r3)
	/* r1, r14-r30 */
	lwz %r1,  JMP_r1 (%r3)
	lwz %r14, JMP_r14(%r3)
	lwz %r15, JMP_r15(%r3)
	lwz %r16, JMP_r16(%r3)
	lwz %r17, JMP_r17(%r3)
	lwz %r18, JMP_r18(%r3)
	lwz %r19, JMP_r19(%r3)
	lwz %r20, JMP_r20(%r3)
	lwz %r21, JMP_r21(%r3)
	lwz %r22, JMP_r22(%r3)
	lwz %r23, JMP_r23(%r3)
	lwz %r24, JMP_r24(%r3)
	lwz %r25, JMP_r25(%r3)
	lwz %r26, JMP_r26(%r3)
	lwz %r27, JMP_r27(%r3)
	lwz %r28, JMP_r28(%r3)
	lwz %r29, JMP_r29(%r3)
	lwz %r30, JMP_r30(%r3)
	/* cr, ctr, xer */
	lwz %r0, JMP_cr(%r3)
	mtcr %r0
	lwz %r0, JMP_ctr(%r3)
	mtctr %r0
	lwz %r0, JMP_xer(%r3)
	mtxer %r0
	/* f14-f31, fpscr */
	li %r3, 1
	RETGUARD_CHECK(longjmp, %r11, %r12)
	blr
