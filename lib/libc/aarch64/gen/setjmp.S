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

#include <machine/asm.h>
__FBSDID("$FreeBSD$");

#include <machine/setjmp.h>

ENTRY(setjmp)
	sub	sp, sp, #16
	stp	x0, lr, [sp]

	/* Store the signal mask */
	add	x2, x0, #(_JB_SIGMASK * 8)	/* oset */
	mov	x1, #0				/* set */
	mov	x0, #1				/* SIG_BLOCK */
	bl	sigprocmask

	ldp	x0, lr, [sp]
	add	sp, sp, #16

	/* Store the magic value and stack pointer */
	ldr	x8, .Lmagic
	mov	x9, sp
	stp	x8, x9, [x0], #16

	/* Store the general purpose registers and lr */
	stp	x19, x20, [x0], #16
	stp	x21, x22, [x0], #16
	stp	x23, x24, [x0], #16
	stp	x25, x26, [x0], #16
	stp	x27, x28, [x0], #16
	stp	x29, lr, [x0], #16

	/* Store the vfp registers */
	stp	d8, d9, [x0], #16
	stp	d10, d11, [x0], #16
	stp	d12, d13, [x0], #16
	stp	d14, d15, [x0]

	/* Return value */
	mov	x0, #0
	ret
	.align	3
.Lmagic:
	.quad	_JB_MAGIC_SETJMP
END(setjmp)

ENTRY(longjmp)
	sub	sp, sp, #32
	stp	x0, lr, [sp]
	str	x1, [sp, #16]

	/* Restore the signal mask */
	mov	x2, #0				/* oset */
	add	x1, x0, #(_JB_SIGMASK * 8)	/* set */
	mov	x0, #3				/* SIG_SETMASK */
	bl	sigprocmask

	ldr	x1, [sp, #16]
	ldp	x0, lr, [sp]
	add	sp, sp, #32

	/* Check the magic value */
	ldr	x8, [x0], #8
	ldr	x9, .Lmagic
	cmp	x8, x9
	b.ne	botch

	/* Restore the stack pointer */
	ldr	x8, [x0], #8
	mov	sp, x8

	/* Restore the general purpose registers and lr */
	ldp	x19, x20, [x0], #16
	ldp	x21, x22, [x0], #16
	ldp	x23, x24, [x0], #16
	ldp	x25, x26, [x0], #16
	ldp	x27, x28, [x0], #16
	ldp	x29, lr, [x0], #16

	/* Restore the vfp registers */
	ldp	d8, d9, [x0], #16
	ldp	d10, d11, [x0], #16
	ldp	d12, d13, [x0], #16
	ldp	d14, d15, [x0]

	/* Load the return value */
	mov	x0, x1
	ret

botch:
	bl	_C_LABEL(longjmperror)
	bl	_C_LABEL(abort)
END(longjmp)
