/*	$NetBSD: rtld_start.S,v 1.7 2002/09/12 17:18:38 mycroft Exp $	*/

/*-
 * Copyright (c) 1998, 2002 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Matt Thomas and by Charles M. Hannum.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <machine/asm.h>
__FBSDID("$FreeBSD$");

	.text
	.align	0
	.globl	.rtld_start
	.type	.rtld_start,%function
.rtld_start:
	mov	r6, sp			/* save the stack pointer */
	bic	sp, sp, #7		/* align the stack pointer */
	sub	sp, sp, #8		/* make room for obj_main & exit proc */
	mov	r4, r0			/* save ps_strings */
	ldr	sl, .L2
	ldr	r5, .L2+4
	ldr	r0, .L2+8
.L1:
	add	sl, pc, sl
	ldr	r5, [sl, r5]
	ldr	r0, [sl, r0]

	sub	r1, sl, r5		/* relocbase */
	add	r0, r1, r0		/* &_DYNAMIC */
	bl	_rtld_relocate_nonplt_self
	mov	r1, sp
	add	r2, sp, #4
	mov	r0, r6			/* load the sp the kernel gave us */
	bl	_rtld			/* call the shared loader */
	mov	r3, r0			/* save entry point */

	ldr	r2, [sp, #0]		/* r2 = cleanup */
	ldr	r1, [sp, #4]		/* r1 = obj_main */
	mov	sp, r6			/* restore stack */
	mov	r0, r4			/* restore ps_strings */
	mov	pc, r3			/* jump to the entry point */
.L2:
	.word	_GLOBAL_OFFSET_TABLE_ - (.L1+8)
	.word	_GLOBAL_OFFSET_TABLE_(GOT)
	.word	_DYNAMIC(GOT)

	.align	0
	.globl	_rtld_bind_start
	.type	_rtld_bind_start,%function
/*
 *	stack[0] = RA
 *	ip = &GOT[n+3]
 *	lr = &GOT[2]
 */
_rtld_bind_start:
	stmdb	sp!,{r0-r5,sl,fp}

	sub	r1, ip, lr		/* r1 = 4 * (n + 1) */
	sub	r1, r1, #4		/* r1 = 4 * n */
	add	r1, r1, r1		/* r1 = 8 * n */

	ldr	r0, [lr, #-4]		/* get obj ptr from GOT[1] */
	mov	r4, ip			/* save GOT location */

	mov	r5, sp			/* Save the stack pointer */
	bic	sp, sp, #7		/* Align the stack pointer */
	bl	_rtld_bind		/* Call the binder */
	mov	sp, r5			/* Restore the old stack pointer */

	str	r0, [r4]		/* save address in GOT */
	mov	ip, r0			/* save new address */

	ldmia	sp!,{r0-r5,sl,fp,lr}	/* restore the stack */
	mov	pc, ip			/* jump to the new address */

	.section .note.GNU-stack,"",%progbits
