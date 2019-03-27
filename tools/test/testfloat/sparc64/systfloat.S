/*-
 * Copyright (c) 2010 by Peter Jeremy <peterjeremy@acm.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
===============================================================================

This GNU assembler source file is part of TestFloat, Release 2a, a package
of programs for testing the correctness of floating-point arithmetic
complying to the IEC/IEEE Standard for Floating-Point.

Written by John R. Hauser.  More information is available through the Web
page `http://HTTP.CS.Berkeley.EDU/~jhauser/arithmetic/TestFloat.html'.

THIS SOFTWARE IS DISTRIBUTED AS IS, FOR FREE.  Although reasonable effort
has been made to avoid it, THIS SOFTWARE MAY CONTAIN FAULTS THAT WILL AT
TIMES RESULT IN INCORRECT BEHAVIOR.  USE OF THIS SOFTWARE IS RESTRICTED TO
PERSONS AND ORGANIZATIONS WHO CAN AND WILL TAKE FULL RESPONSIBILITY FOR ANY
AND ALL LOSSES, COSTS, OR OTHER PROBLEMS ARISING FROM ITS USE.

Derivative works are acceptable, even for commercial purposes, so long as
(1) they include prominent notice that the work is derivative, and (2) they
include prominent notice akin to these four paragraphs for those parts of
this code that are retained.

===============================================================================
*/

#include <machine/asm.h>
__FBSDID("$FreeBSD$");

	.text

/*
	.macro i2f32	src=%i0,dst=%f0
	st	\src,[%sp+2231]
	ld	[%sp+2231],\dst
	.endm

	.macro i2f64	src=%i0,dst=%f0
	stx	\src,[%sp+2231]
	ldd	[%sp+2231],\dst
	.endm

	.macro f2i32	src=%f0,dst=%i0
	st	\src,[%sp+2231]
	ld	[%sp+2231],\dst
	.endm

	.macro f2i64	src=%f0,dst=%i0
	std	\src,[%sp+2231]
	ldx	[%sp+2231],\dst
	.endm
*/
/*
-------------------------------------------------------------------------------
-------------------------------------------------------------------------------
*/
	.align	4
	.global	syst_int32_to_float32
syst_int32_to_float32:
	save	%sp,-192,%sp

	st	%i0,[%sp+2231]
	ld	[%sp+2231],%f0
	fitos	%f0,%f0
	st	%f0,[%sp+2231]
	ld	[%sp+2231],%i0

	ret
	restore

/*
-------------------------------------------------------------------------------
-------------------------------------------------------------------------------
*/
	.align	4
	.global	syst_int32_to_float64
syst_int32_to_float64:
	save	%sp,-192,%sp

	st	%i0,[%sp+2231]
	ld	[%sp+2231],%f0
	fitod	%f0,%f0
	std	%f0,[%sp+2231]
	ldx	[%sp+2231],%i0

	ret
	restore

/*
-------------------------------------------------------------------------------
-------------------------------------------------------------------------------
*/
	.align	4
	.global	syst_int32_to_float128
syst_int32_to_float128:
	save	%sp,-192,%sp

	st	%i0,[%sp+2231]
	ld	[%sp+2231],%f0
	fitoq	%f0,%f0
	std	%f0,[%sp+2231]
	ldx	[%sp+2231],%i0
	std	%f2,[%sp+2231]
	ldx	[%sp+2231],%i1

	ret
	restore

/*
-------------------------------------------------------------------------------
-------------------------------------------------------------------------------
*/
	.align	4
	.global	syst_int64_to_float32
syst_int64_to_float32:
	save	%sp,-192,%sp

	stx	%i0,[%sp+2231]
	ldd	[%sp+2231],%f0
	fxtos	%f0,%f0
	st	%f0,[%sp+2231]
	ld	[%sp+2231],%i0

	ret
	restore

/*
-------------------------------------------------------------------------------
-------------------------------------------------------------------------------
*/
	.align	4
	.global	syst_int64_to_float64
syst_int64_to_float64:
	save	%sp,-192,%sp

	stx	%i0,[%sp+2231]
	ldd	[%sp+2231],%f0
	fxtod	%f0,%f0
	std	%f0,[%sp+2231]
	ldx	[%sp+2231],%i0

	ret
	restore

/*
-------------------------------------------------------------------------------
-------------------------------------------------------------------------------
*/
	.align	4
	.global	syst_int64_to_float128
syst_int64_to_float128:
	save	%sp,-192,%sp

	stx	%i0,[%sp+2231]
	ldd	[%sp+2231],%f0
	fxtoq	%f0,%f0
	std	%f0,[%sp+2231]
	ldx	[%sp+2231],%i0
	std	%f2,[%sp+2231]
	ldx	[%sp+2231],%i1

	ret
	restore

/*
-------------------------------------------------------------------------------
-------------------------------------------------------------------------------
*/
	.align	4
	.global	syst_float32_to_int32_round_to_zero
syst_float32_to_int32_round_to_zero:
	save	%sp,-192,%sp

	st	%i0,[%sp+2231]
	ld	[%sp+2231],%f0
	fstoi	%f0,%f0
	st	%f0,[%sp+2231]
	ld	[%sp+2231],%i0

	ret
	restore

/*
-------------------------------------------------------------------------------
-------------------------------------------------------------------------------
*/
	.align	4
	.global	syst_float32_to_int64_round_to_zero
syst_float32_to_int64_round_to_zero:
	save	%sp,-192,%sp

	st	%i0,[%sp+2231]
	ld	[%sp+2231],%f0
	fstox	%f0,%f0
	std	%f0,[%sp+2231]
	ldx	[%sp+2231],%i0

	ret
	restore

/*
-------------------------------------------------------------------------------
-------------------------------------------------------------------------------
*/
	.align	4
	.global	syst_float32_to_float64
syst_float32_to_float64:
	save	%sp,-192,%sp

	st	%i0,[%sp+2231]
	ld	[%sp+2231],%f0
	fstod	%f0,%f0
	std	%f0,[%sp+2231]
	ldx	[%sp+2231],%i0

	ret
	restore

/*
-------------------------------------------------------------------------------
-------------------------------------------------------------------------------
*/
	.align	4
	.global	syst_float32_to_float128
syst_float32_to_float128:
	save	%sp,-192,%sp

	st	%i0,[%sp+2231]
	ld	[%sp+2231],%f0
	fstoq	%f0,%f0
	std	%f0,[%sp+2231]
	ldx	[%sp+2231],%i0
	std	%f2,[%sp+2231]
	ldx	[%sp+2231],%i1

	ret
	restore

/*
-------------------------------------------------------------------------------
-------------------------------------------------------------------------------
*/
	.align	4
	.global	syst_float32_add
syst_float32_add:
	save	%sp,-192,%sp

	st	%i0,[%sp+2231]
	ld	[%sp+2231],%f0
	st	%i1,[%sp+2231]
	ld	[%sp+2231],%f1
	fadds	%f0,%f1,%f0
	st	%f0,[%sp+2231]
	ld	[%sp+2231],%i0

	ret
	restore

/*
-------------------------------------------------------------------------------
-------------------------------------------------------------------------------
*/
	.align	4
	.global	syst_float32_sub
syst_float32_sub:
	save	%sp,-192,%sp

	st	%i0,[%sp+2231]
	ld	[%sp+2231],%f0
	st	%i1,[%sp+2231]
	ld	[%sp+2231],%f1
	fsubs	%f0,%f1,%f0
	st	%f0,[%sp+2231]
	ld	[%sp+2231],%i0

	ret
	restore

/*
-------------------------------------------------------------------------------
-------------------------------------------------------------------------------
*/
	.align	4
	.global	syst_float32_mul
syst_float32_mul:
	save	%sp,-192,%sp

	st	%i0,[%sp+2231]
	ld	[%sp+2231],%f0
	st	%i1,[%sp+2231]
	ld	[%sp+2231],%f1
	fmuls	%f0,%f1,%f0
	st	%f0,[%sp+2231]
	ld	[%sp+2231],%i0

	ret
	restore

/*
-------------------------------------------------------------------------------
-------------------------------------------------------------------------------
*/
	.align	4
	.global	syst_float32_div
syst_float32_div:
	save	%sp,-192,%sp

	st	%i0,[%sp+2231]
	ld	[%sp+2231],%f0
	st	%i1,[%sp+2231]
	ld	[%sp+2231],%f1
	fdivs	%f0,%f1,%f0
	st	%f0,[%sp+2231]
	ld	[%sp+2231],%i0

	ret
	restore

/*
-------------------------------------------------------------------------------
-------------------------------------------------------------------------------
*/
	.align	4
	.global	syst_float32_sqrt
syst_float32_sqrt:
	save	%sp,-192,%sp

	st	%i0,[%sp+2231]
	ld	[%sp+2231],%f0
	fsqrts	%f0,%f0
	st	%f0,[%sp+2231]
	ld	[%sp+2231],%i0

	ret
	restore

/*
-------------------------------------------------------------------------------
-------------------------------------------------------------------------------
*/
	.align	4
	.global	syst_float32_eq
syst_float32_eq:
	save	%sp,-192,%sp

	st	%i0,[%sp+2231]
	ld	[%sp+2231],%f0
	st	%i1,[%sp+2231]
	ld	[%sp+2231],%f1
	fcmps	%fcc0,%f0,%f1
	mov	0,%i0
	move	%fcc0,1,%i0

	ret
	restore

/*
-------------------------------------------------------------------------------
-------------------------------------------------------------------------------
*/
	.align	4
	.global	syst_float32_le
syst_float32_le:
	save	%sp,-192,%sp

	st	%i0,[%sp+2231]
	ld	[%sp+2231],%f0
	st	%i1,[%sp+2231]
	ld	[%sp+2231],%f1
	fcmpes	%fcc0,%f0,%f1
	mov	0,%i0
	movle	%fcc0,1,%i0

	ret
	restore

/*
-------------------------------------------------------------------------------
-------------------------------------------------------------------------------
*/
	.align	4
	.global	syst_float32_lt
syst_float32_lt:
	save	%sp,-192,%sp

	st	%i0,[%sp+2231]
	ld	[%sp+2231],%f0
	st	%i1,[%sp+2231]
	ld	[%sp+2231],%f1
	fcmpes	%fcc0,%f0,%f1
	mov	0,%i0
	movl	%fcc0,1,%i0

	ret
	restore

/*
-------------------------------------------------------------------------------
-------------------------------------------------------------------------------
*/
	.align	4
	.global	syst_float32_eq_signaling
syst_float32_eq_signaling:
	save	%sp,-192,%sp

	st	%i0,[%sp+2231]
	ld	[%sp+2231],%f0
	st	%i1,[%sp+2231]
	ld	[%sp+2231],%f1
	fcmpes	%fcc0,%f0,%f1
	mov	0,%i0
	move	%fcc0,1,%i0

	ret
	restore

/*
-------------------------------------------------------------------------------
-------------------------------------------------------------------------------
*/
	.align	4
	.global	syst_float32_le_quiet
syst_float32_le_quiet:
	save	%sp,-192,%sp

	st	%i0,[%sp+2231]
	ld	[%sp+2231],%f0
	st	%i1,[%sp+2231]
	ld	[%sp+2231],%f1
	fcmps	%fcc0,%f0,%f1
	mov	0,%i0
	movle	%fcc0,1,%i0

	ret
	restore

/*
-------------------------------------------------------------------------------
-------------------------------------------------------------------------------
*/
	.align	4
	.global	syst_float32_lt_quiet
syst_float32_lt_quiet:
	save	%sp,-192,%sp

	st	%i0,[%sp+2231]
	ld	[%sp+2231],%f0
	st	%i1,[%sp+2231]
	ld	[%sp+2231],%f1
	fcmps	%fcc0,%f0,%f1
	mov	0,%i0
	movl	%fcc0,1,%i0

	ret
	restore

/*
-------------------------------------------------------------------------------
-------------------------------------------------------------------------------
*/
	.align	4
	.global	syst_float64_to_int32_round_to_zero
syst_float64_to_int32_round_to_zero:
	save	%sp,-192,%sp

	stx	%i0,[%sp+2231]
	ldd	[%sp+2231],%f0
	fdtoi	%f0,%f0
	st	%f0,[%sp+2231]
	ld	[%sp+2231],%i0

	ret
	restore

/*
-------------------------------------------------------------------------------
-------------------------------------------------------------------------------
*/
	.align	4
	.global	syst_float64_to_int64_round_to_zero
syst_float64_to_int64_round_to_zero:
	save	%sp,-192,%sp

	stx	%i0,[%sp+2231]
	ldd	[%sp+2231],%f0
	fdtox	%f0,%f0
	std	%f0,[%sp+2231]
	ldx	[%sp+2231],%i0

	ret
	restore

/*
-------------------------------------------------------------------------------
-------------------------------------------------------------------------------
*/
	.align	4
	.global	syst_float64_to_float32
syst_float64_to_float32:
	save	%sp,-192,%sp

	stx	%i0,[%sp+2231]
	ldd	[%sp+2231],%f0
	fdtos	%f0,%f0
	st	%f0,[%sp+2231]
	ld	[%sp+2231],%i0

	ret
	restore

/*
-------------------------------------------------------------------------------
-------------------------------------------------------------------------------
*/
	.align	4
	.global	syst_float64_to_float128
syst_float64_to_float128:
	save	%sp,-192,%sp

	stx	%i0,[%sp+2231]
	ldd	[%sp+2231],%f0
	fdtoq	%f0,%f0
	std	%f0,[%sp+2231]
	ldx	[%sp+2231],%i0
	std	%f2,[%sp+2231]
	ldx	[%sp+2231],%i1

	ret
	restore

/*
-------------------------------------------------------------------------------
-------------------------------------------------------------------------------
*/
	.align	4
	.global	syst_float64_add
syst_float64_add:
	save	%sp,-192,%sp

	stx	%i0,[%sp+2231]
	ldd	[%sp+2231],%f0
	stx	%i1,[%sp+2231]
	ldd	[%sp+2231],%f2
	faddd	%f0,%f2,%f0
	std	%f0,[%sp+2231]
	ldx	[%sp+2231],%i0

	ret
	restore

/*
-------------------------------------------------------------------------------
-------------------------------------------------------------------------------
*/
	.align	4
	.global	syst_float64_sub
syst_float64_sub:
	save	%sp,-192,%sp

	stx	%i0,[%sp+2231]
	ldd	[%sp+2231],%f0
	stx	%i1,[%sp+2231]
	ldd	[%sp+2231],%f2
	fsubd	%f0,%f2,%f0
	std	%f0,[%sp+2231]
	ldx	[%sp+2231],%i0

	ret
	restore

/*
-------------------------------------------------------------------------------
-------------------------------------------------------------------------------
*/
	.align	4
	.global	syst_float64_mul
syst_float64_mul:
	save	%sp,-192,%sp

	stx	%i0,[%sp+2231]
	ldd	[%sp+2231],%f0
	stx	%i1,[%sp+2231]
	ldd	[%sp+2231],%f2
	fmuld	%f0,%f2,%f0
	std	%f0,[%sp+2231]
	ldx	[%sp+2231],%i0

	ret
	restore

/*
-------------------------------------------------------------------------------
-------------------------------------------------------------------------------
*/
	.align	4
	.global	syst_float64_div
syst_float64_div:
	save	%sp,-192,%sp

	stx	%i0,[%sp+2231]
	ldd	[%sp+2231],%f0
	stx	%i1,[%sp+2231]
	ldd	[%sp+2231],%f2
	fdivd	%f0,%f2,%f0
	std	%f0,[%sp+2231]
	ldx	[%sp+2231],%i0

	ret
	restore

/*
-------------------------------------------------------------------------------
-------------------------------------------------------------------------------
*/
	.align	4
	.global	syst_float64_sqrt
syst_float64_sqrt:
	save	%sp,-192,%sp

	stx	%i0,[%sp+2231]
	ldd	[%sp+2231],%f0
	fsqrtd	%f0,%f0
	std	%f0,[%sp+2231]
	ldx	[%sp+2231],%i0

	ret
	restore

/*
-------------------------------------------------------------------------------
-------------------------------------------------------------------------------
*/
	.align	4
	.global	syst_float64_eq
syst_float64_eq:
	save	%sp,-192,%sp

	stx	%i0,[%sp+2231]
	ldd	[%sp+2231],%f0
	stx	%i1,[%sp+2231]
	ldd	[%sp+2231],%f2
	fcmpd	%fcc0,%f0,%f2
	mov	0,%i0
	move	%fcc0,1,%i0

	ret
	restore

/*
-------------------------------------------------------------------------------
-------------------------------------------------------------------------------
*/
	.align	4
	.global	syst_float64_le
syst_float64_le:
	save	%sp,-192,%sp

	stx	%i0,[%sp+2231]
	ldd	[%sp+2231],%f0
	stx	%i1,[%sp+2231]
	ldd	[%sp+2231],%f2
	fcmped	%fcc0,%f0,%f2
	mov	0,%i0
	movle	%fcc0,1,%i0

	ret
	restore

/*
-------------------------------------------------------------------------------
-------------------------------------------------------------------------------
*/
	.align	4
	.global	syst_float64_lt
syst_float64_lt:
	save	%sp,-192,%sp

	stx	%i0,[%sp+2231]
	ldd	[%sp+2231],%f0
	stx	%i1,[%sp+2231]
	ldd	[%sp+2231],%f2
	fcmped	%fcc0,%f0,%f2
	mov	0,%i0
	movl	%fcc0,1,%i0

	ret
	restore

/*
-------------------------------------------------------------------------------
-------------------------------------------------------------------------------
*/
	.align	4
	.global	syst_float64_eq_signaling
syst_float64_eq_signaling:
	save	%sp,-192,%sp

	stx	%i0,[%sp+2231]
	ldd	[%sp+2231],%f0
	stx	%i1,[%sp+2231]
	ldd	[%sp+2231],%f2
	fcmped	%fcc0,%f0,%f2
	mov	0,%i0
	move	%fcc0,1,%i0

	ret
	restore

/*
-------------------------------------------------------------------------------
-------------------------------------------------------------------------------
*/
	.align	4
	.global	syst_float64_le_quiet
syst_float64_le_quiet:
	save	%sp,-192,%sp

	stx	%i0,[%sp+2231]
	ldd	[%sp+2231],%f0
	stx	%i1,[%sp+2231]
	ldd	[%sp+2231],%f2
	fcmpd	%fcc0,%f0,%f2
	mov	0,%i0
	movle	%fcc0,1,%i0

	ret
	restore

/*
-------------------------------------------------------------------------------
-------------------------------------------------------------------------------
*/
	.align	4
	.global	syst_float64_lt_quiet
syst_float64_lt_quiet:
	save	%sp,-192,%sp

	stx	%i0,[%sp+2231]
	ldd	[%sp+2231],%f0
	stx	%i1,[%sp+2231]
	ldd	[%sp+2231],%f2
	fcmpd	%fcc0,%f0,%f2
	mov	0,%i0
	movl	%fcc0,1,%i0

	ret
	restore

/*
-------------------------------------------------------------------------------
-------------------------------------------------------------------------------
*/
	.align	4
	.global	syst_float128_to_int32_round_to_zero
syst_float128_to_int32_round_to_zero:
	save	%sp,-192,%sp

	stx	%i0,[%sp+2231]
	ldd	[%sp+2231],%f0
	stx	%i1,[%sp+2231]
	ldd	[%sp+2231],%f2
	fqtoi	%f0,%f0
	st	%f0,[%sp+2231]
	ld	[%sp+2231],%i0

	ret
	restore

/*
-------------------------------------------------------------------------------
-------------------------------------------------------------------------------
*/
	.align	4
	.global	syst_float128_to_int64_round_to_zero
syst_float128_to_int64_round_to_zero:
	save	%sp,-192,%sp

	stx	%i0,[%sp+2231]
	ldd	[%sp+2231],%f0
	stx	%i1,[%sp+2231]
	ldd	[%sp+2231],%f2
	fqtox	%f0,%f0
	std	%f0,[%sp+2231]
	ldx	[%sp+2231],%i0

	ret
	restore

/*
-------------------------------------------------------------------------------
-------------------------------------------------------------------------------
*/
	.align	4
	.global	syst_float128_to_float32
syst_float128_to_float32:
	save	%sp,-192,%sp

	stx	%i0,[%sp+2231]
	ldd	[%sp+2231],%f0
	stx	%i1,[%sp+2231]
	ldd	[%sp+2231],%f2
	fqtos	%f0,%f0
	st	%f0,[%sp+2231]
	ld	[%sp+2231],%i0

	ret
	restore

/*
-------------------------------------------------------------------------------
-------------------------------------------------------------------------------
*/
	.align	4
	.global	syst_float128_to_float64
syst_float128_to_float64:
	save	%sp,-192,%sp

	stx	%i0,[%sp+2231]
	ldd	[%sp+2231],%f0
	stx	%i1,[%sp+2231]
	ldd	[%sp+2231],%f2
	fqtod	%f0,%f0
	std	%f0,[%sp+2231]
	ldx	[%sp+2231],%i0

	ret
	restore

/*
-------------------------------------------------------------------------------
-------------------------------------------------------------------------------
*/
	.align	4
	.global	syst_float128_add
syst_float128_add:
	save	%sp,-192,%sp

	stx	%i0,[%sp+2231]
	ldd	[%sp+2231],%f0
	stx	%i1,[%sp+2231]
	ldd	[%sp+2231],%f2
	stx	%i2,[%sp+2231]
	ldd	[%sp+2231],%f4
	stx	%i3,[%sp+2231]
	ldd	[%sp+2231],%f6
	faddq	%f0,%f4,%f0
	std	%f0,[%sp+2231]
	ldx	[%sp+2231],%i0
	std	%f2,[%sp+2231]
	ldx	[%sp+2231],%i1

	ret
	restore

/*
-------------------------------------------------------------------------------
-------------------------------------------------------------------------------
*/
	.align	4
	.global	syst_float128_sub
syst_float128_sub:
	save	%sp,-192,%sp

	stx	%i0,[%sp+2231]
	ldd	[%sp+2231],%f0
	stx	%i1,[%sp+2231]
	ldd	[%sp+2231],%f2
	stx	%i2,[%sp+2231]
	ldd	[%sp+2231],%f4
	stx	%i3,[%sp+2231]
	ldd	[%sp+2231],%f6
	fsubq	%f0,%f4,%f0
	std	%f0,[%sp+2231]
	ldx	[%sp+2231],%i0
	std	%f2,[%sp+2231]
	ldx	[%sp+2231],%i1

	ret
	restore

/*
-------------------------------------------------------------------------------
-------------------------------------------------------------------------------
*/
	.align	4
	.global	syst_float128_mul
syst_float128_mul:
	save	%sp,-192,%sp

	stx	%i0,[%sp+2231]
	ldd	[%sp+2231],%f0
	stx	%i1,[%sp+2231]
	ldd	[%sp+2231],%f2
	stx	%i2,[%sp+2231]
	ldd	[%sp+2231],%f4
	stx	%i3,[%sp+2231]
	ldd	[%sp+2231],%f6
	fmulq	%f0,%f4,%f0
	std	%f0,[%sp+2231]
	ldx	[%sp+2231],%i0
	std	%f2,[%sp+2231]
	ldx	[%sp+2231],%i1

	ret
	restore

/*
-------------------------------------------------------------------------------
-------------------------------------------------------------------------------
*/
	.align	4
	.global	syst_float128_div
syst_float128_div:
	save	%sp,-192,%sp

	stx	%i0,[%sp+2231]
	ldd	[%sp+2231],%f0
	stx	%i1,[%sp+2231]
	ldd	[%sp+2231],%f2
	stx	%i2,[%sp+2231]
	ldd	[%sp+2231],%f4
	stx	%i3,[%sp+2231]
	ldd	[%sp+2231],%f6
	fdivq	%f0,%f4,%f0
	std	%f0,[%sp+2231]
	ldx	[%sp+2231],%i0
	std	%f2,[%sp+2231]
	ldx	[%sp+2231],%i1

	ret
	restore

/*
-------------------------------------------------------------------------------
-------------------------------------------------------------------------------
*/
	.align	4
	.global	syst_float128_sqrt
syst_float128_sqrt:
	save	%sp,-192,%sp

	stx	%i0,[%sp+2231]
	ldd	[%sp+2231],%f0
	stx	%i1,[%sp+2231]
	ldd	[%sp+2231],%f2
	fsqrtq	%f0,%f0
	std	%f0,[%sp+2231]
	ldx	[%sp+2231],%i0
	std	%f2,[%sp+2231]
	ldx	[%sp+2231],%i1

	ret
	restore

/*
-------------------------------------------------------------------------------
-------------------------------------------------------------------------------
*/
	.align	4
	.global	syst_float128_eq
syst_float128_eq:
	save	%sp,-192,%sp

	stx	%i0,[%sp+2231]
	ldd	[%sp+2231],%f0
	stx	%i1,[%sp+2231]
	ldd	[%sp+2231],%f2
	stx	%i2,[%sp+2231]
	ldd	[%sp+2231],%f4
	stx	%i3,[%sp+2231]
	ldd	[%sp+2231],%f6
	fcmpq	%fcc0,%f0,%f4
	mov	0,%i0
	move	%fcc0,1,%i0

	ret
	restore

/*
-------------------------------------------------------------------------------
-------------------------------------------------------------------------------
*/
	.align	4
	.global	syst_float128_le
syst_float128_le:
	save	%sp,-192,%sp

	stx	%i0,[%sp+2231]
	ldd	[%sp+2231],%f0
	stx	%i1,[%sp+2231]
	ldd	[%sp+2231],%f2
	stx	%i2,[%sp+2231]
	ldd	[%sp+2231],%f4
	stx	%i3,[%sp+2231]
	ldd	[%sp+2231],%f6
	fcmpeq	%fcc0,%f0,%f4
	mov	0,%i0
	movle	%fcc0,1,%i0

	ret
	restore

/*
-------------------------------------------------------------------------------
-------------------------------------------------------------------------------
*/
	.align	4
	.global	syst_float128_lt
syst_float128_lt:
	save	%sp,-192,%sp

	stx	%i0,[%sp+2231]
	ldd	[%sp+2231],%f0
	stx	%i1,[%sp+2231]
	ldd	[%sp+2231],%f2
	stx	%i2,[%sp+2231]
	ldd	[%sp+2231],%f4
	stx	%i3,[%sp+2231]
	ldd	[%sp+2231],%f6
	fcmpeq	%fcc0,%f0,%f4
	mov	0,%i0
	movl	%fcc0,1,%i0

	ret
	restore

/*
-------------------------------------------------------------------------------
-------------------------------------------------------------------------------
*/
	.align	4
	.global	syst_float128_eq_signaling
syst_float128_eq_signaling:
	save	%sp,-192,%sp

	stx	%i0,[%sp+2231]
	ldd	[%sp+2231],%f0
	stx	%i1,[%sp+2231]
	ldd	[%sp+2231],%f2
	stx	%i2,[%sp+2231]
	ldd	[%sp+2231],%f4
	stx	%i3,[%sp+2231]
	ldd	[%sp+2231],%f6
	fcmpeq	%fcc0,%f0,%f4
	mov	0,%i0
	move	%fcc0,1,%i0

	ret
	restore

/*
-------------------------------------------------------------------------------
-------------------------------------------------------------------------------
*/
	.align	4
	.global	syst_float128_le_quiet
syst_float128_le_quiet:
	save	%sp,-192,%sp

	stx	%i0,[%sp+2231]
	ldd	[%sp+2231],%f0
	stx	%i1,[%sp+2231]
	ldd	[%sp+2231],%f2
	stx	%i2,[%sp+2231]
	ldd	[%sp+2231],%f4
	stx	%i3,[%sp+2231]
	ldd	[%sp+2231],%f6
	fcmpq	%fcc0,%f0,%f4
	mov	0,%i0
	movle	%fcc0,1,%i0

	ret
	restore

/*
-------------------------------------------------------------------------------
-------------------------------------------------------------------------------
*/
	.align	4
	.global	syst_float128_lt_quiet
syst_float128_lt_quiet:
	save	%sp,-192,%sp

	stx	%i0,[%sp+2231]
	ldd	[%sp+2231],%f0
	stx	%i1,[%sp+2231]
	ldd	[%sp+2231],%f2
	stx	%i2,[%sp+2231]
	ldd	[%sp+2231],%f4
	stx	%i3,[%sp+2231]
	ldd	[%sp+2231],%f6
	fcmpq	%fcc0,%f0,%f4
	mov	0,%i0
	movl	%fcc0,1,%i0

	ret
	restore
