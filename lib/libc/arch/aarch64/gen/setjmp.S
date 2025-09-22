/*	$OpenBSD: setjmp.S,v 1.6 2022/12/24 10:49:19 kettenis Exp $	*/
/*-
 * Copyright (c) 2014 Andrew Turner
 * Copyright (c) 2014 The FreeBSD Foundation
 * All rights reserved.
 *
 * Portions of this software were developed by Andrew Turner
 * under sponsorship from the FreeBSD Foundation
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
 *
 */

#include "SYS.h"
#include <machine/setjmp.h>

ENTRY(setjmp)

	RETGUARD_SETUP(setjmp, x15)
	mov	x2, x0 				/* save jmpbuf in x2 */
	/* Store the signal mask */
	mov	w1, #0				/* set */
	mov	x0, #1				/* SIG_BLOCK */
	SYSTRAP(sigprocmask)
	str	w0, [x2, #(_JB_SIGMASK * 8)]	/* oset */

	mov	x0, x2
	/* Store the magic value and stack pointer */
	movz	x8, #(_JB_MAGIC_SETJMP & 0xffff)
	movk	x8, #(_JB_MAGIC_SETJMP >> 16), lsl #16
	mov	x9, sp
	stp	x8, x9, [x0], #16

	/* Store the general purpose registers and lr */
	stp	x19, x20, [x0], #16
	stp	x21, x22, [x0], #16
	stp	x23, x24, [x0], #16
	stp	x25, x26, [x0], #16
	stp	x27, x28, [x0], #16
	stp	x29, x30, [x0], #16

	/* Store the vfp registers */
	stp	d8, d9, [x0], #16
	stp	d10, d11, [x0], #16
	stp	d12, d13, [x0], #16
	stp	d14, d15, [x0], #16
	mrs	x1, fpcr
	str	x1, [x0]

	/* Return value */
	mov	x0, #0
	RETGUARD_CHECK(setjmp, x15)
	ret
END_STRONG(setjmp)

ENTRY(longjmp)
	RETGUARD_SYMBOL(longjmp)
	RETGUARD_LOAD_RANDOM(longjmp, x15)
	mov	x2, x0				/* move jmpbuf */
	mov	x3, x1				/* final return value */

	/* Restore the signal mask */
	ldr	w1, [x2, #(_JB_SIGMASK * 8)]	/* set */
	mov	x0, #3				/* SIG_SETMASK */
	SYSTRAP(sigprocmask)

	mov	x0, x2
	/* Check the magic value */
	ldr	x8, [x0], #8
	movz	x9, #(_JB_MAGIC_SETJMP & 0xffff)
	movk	x9, #(_JB_MAGIC_SETJMP >> 16), lsl #16
	cmp	x8, x9
	b.ne	.Lbotch

	/* Restore the stack pointer */
	ldr	x8, [x0], #8
	mov	sp, x8

	/* Restore the general purpose registers and lr */
	ldp	x19, x20, [x0], #16
	ldp	x21, x22, [x0], #16
	ldp	x23, x24, [x0], #16
	ldp	x25, x26, [x0], #16
	ldp	x27, x28, [x0], #16
	ldp	x29, x30, [x0], #16
	RETGUARD_CALC_COOKIE(x15)

	/* Restore the vfp registers */
	ldp	d8, d9, [x0], #16
	ldp	d10, d11, [x0], #16
	ldp	d12, d13, [x0], #16
	ldp	d14, d15, [x0], #16
	ldr	x1, [x0]
	msr	fpcr, x1

	/* Load the return value */
	cmp	w3, #0
	csinc	w0, w3, wzr, ne
	RETGUARD_CHECK(longjmp, x15)
	ret

.Lbotch:
	bl	_HIDDEN(abort)
END_STRONG(longjmp)
