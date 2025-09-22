/*	$OpenBSD: fixunsdfsi.S,v 1.5 2023/01/13 17:52:08 miod Exp $	*/
/*	$NetBSD: fixunsdfsi.S,v 1.3 2000/07/25 04:26:12 mycroft Exp $	*/
/*
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This software was developed by the Computer Systems Engineering group
 * at Lawrence Berkeley Laboratory under DARPA contract BG 91-66 and
 * contributed to Berkeley.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <machine/asm.h>
#define _LOCORE
#include <machine/frame.h>

/*
 * Convert double to unsigned integer (for gcc).
 *
 * I have made the output for NaN agree with the Sun compiler, not
 * that it really matters, by using `fbul,a'.
 */

	.section .rodata
	.align	8
Lbigd:
	.word	0x43e00000		! .double 2^63
	.word	0			! (who me, not trust the assembler?)
Lbigf:
	.word	0x5f000000		! .float 2^63

/*
 *unsigned long __dtoul(double):
 *	double -> unsigned long
 */
	.text
ENTRY(__dtoul)
	sub	%sp, 16, %sp
	std	%f2, [%sp + 64 + BIAS + 8]
#ifdef __PIC__
	PIC_PROLOGUE(%o5, %o4)
	set	Lbigd, %o4
	ldx	[%o5 + %o4], %g1
	ldd	[%g1], %f2
#else
	sethi	%hi(Lbigd), %g1
	ldd	[%g1 + %lo(Lbigd)], %f2
#endif
	fcmped	%f0, %f2		! d < 2^63, or NaN, or -Inf?
	nop				! (fpop2 delay)
	fbul,a	1f			! if so, use fdtoi to convert to int
	 fdtox	%f0, %f0		!        (this includes negatives!)

	! d does not fit in an int, so subtract 2^63, convert,
	! and add 2^63 again (sigh).  Just hope the intermediate
	! fits (if not, the result is undefined anyway).

	fsubd	%f0, %f2, %f0		! d -= 2^63
	fdtox	%f0, %f0		! convert to int
	std	%f0, [%sp + 64 + BIAS]	! move into return reg
	ldx	[%sp + 64 + BIAS], %o0
	sethi	%hi(0x80000000), %o1
	sllx	%o1, 32, %o1
	add	%o0, %o1, %o0		! add 2^63
	ldd	[%sp + 64 + BIAS + 8], %f2
	retl
	 add	%sp, 16, %sp

1:
	std	%f0, [%sp + 64 + BIAS]	! return result
	ldx	[%sp + 64 + BIAS], %o0
	ldd	[%sp + 64 + BIAS + 8], %f2
	retl
	 add	%sp, 16, %sp

/*
 *unsigned long __ftoul(float):
 *	float -> unsigned long
 */
ENTRY(__ftoul)
	sub	%sp, 16, %sp
	st	%f1, [%sp + 64 + BIAS + 8]
	st	%f2, [%sp + 64 + BIAS + 12]
#ifdef __PIC__
	PIC_PROLOGUE(%o5, %o4)
	set	Lbigf, %o4
	ldx	[%o5 + %o4], %g1
	ld	[%g1], %f2
#else
	sethi	%hi(Lbigf), %g1
	ld	[%g1 + %lo(Lbigf)], %f2
#endif
	fcmpes	%f0, %f2		! d < 2^63, or NaN, or -Inf?
	nop				! (fpop2 delay)
	fbul,a	1f			! if so, use fdtoi to convert to int
	 fstox	%f0, %f0		!        (this includes negatives!)

	! d does not fit in an int, so subtract 2^63, convert,
	! and add 2^63 again (sigh).  Just hope the intermediate
	! fits (if not, the result is undefined anyway).

	fsubs	%f0, %f2, %f0		! d -= 2^63
	fstox	%f0, %f0		! convert to int
	std	%f0, [%sp + 64 + BIAS]	! move into return reg
	ldx	[%sp + 64 + BIAS], %o0
	sethi	%hi(0x80000000), %o1
	sllx	%o1, 32, %o1
	add	%o0, %o1, %o0		! add 2^63
	ld	[%sp + 64 + BIAS + 8], %f1
	ld	[%sp + 64 + BIAS + 12], %f2
	retl
	 add	%sp, 16, %sp

1:
	std	%f0, [%sp + 64 + BIAS]	! return result
	ldx	[%sp + 64 + BIAS], %o0
	ld	[%sp + 64 + BIAS + 8], %f1
	ld	[%sp + 64 + BIAS + 12], %f2
	retl
	 add	%sp, 16, %sp
