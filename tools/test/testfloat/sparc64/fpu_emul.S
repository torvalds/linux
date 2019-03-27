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

#include <machine/asm.h>
__FBSDID("$FreeBSD$");

	.section	"rodata1",#alloc
	.align	8

	.global	insn_int32_to_float32
insn_int32_to_float32:
	fitos	%f0,%f0

	.global	insn_int32_to_float64
insn_int32_to_float64:
	fitod	%f0,%f0

	.global	insn_int32_to_float128
insn_int32_to_float128:
	fitoq	%f0,%f0

	.global	insn_int64_to_float32
insn_int64_to_float32:
	fxtos	%f0,%f0

	.global	insn_int64_to_float64
insn_int64_to_float64:
	fxtod	%f0,%f0

	.global	insn_int64_to_float128
insn_int64_to_float128:
	fxtoq	%f0,%f0

	.global	insn_float32_to_int32_round_to_zero
insn_float32_to_int32_round_to_zero:
	fstoi	%f0,%f0

	.global	insn_float32_to_int64_round_to_zero
insn_float32_to_int64_round_to_zero:
	fstox	%f0,%f0

	.global	insn_float32_to_float64
insn_float32_to_float64:
	fstod	%f0,%f0

	.global	insn_float32_to_float128
insn_float32_to_float128:
	fstoq	%f0,%f0

	.global	insn_float32_add
insn_float32_add:
	fadds	%f0,%f1,%f0

	.global	insn_float32_sub
insn_float32_sub:
	fsubs	%f0,%f1,%f0

	.global	insn_float32_mul
insn_float32_mul:
	fmuls	%f0,%f1,%f0

	.global	insn_float32_div
insn_float32_div:
	fdivs	%f0,%f1,%f0

	.global	insn_float32_sqrt
insn_float32_sqrt:
	fsqrts	%f0,%f0

	.global	insn_float32_cmp
insn_float32_cmp:
	fcmps	%fcc0,%f0,%f1

	.global	insn_float32_cmpe
insn_float32_cmpe:
	fcmpes	%fcc0,%f0,%f1

	.global	insn_float64_to_int32_round_to_zero
insn_float64_to_int32_round_to_zero:
	fdtoi	%f0,%f0

	.global	insn_float64_to_int64_round_to_zero
insn_float64_to_int64_round_to_zero:
	fdtox	%f0,%f0

	.global	insn_float64_to_float32
insn_float64_to_float32:
	fdtos	%f0,%f0

	.global	insn_float64_to_float128
insn_float64_to_float128:
	fdtoq	%f0,%f0

	.global	insn_float64_add
insn_float64_add:
	faddd	%f0,%f2,%f0

	.global	insn_float64_sub
insn_float64_sub:
	fsubd	%f0,%f2,%f0

	.global	insn_float64_mul
insn_float64_mul:
	fmuld	%f0,%f2,%f0

	.global	insn_float64_div
insn_float64_div:
	fdivd	%f0,%f2,%f0

	.global	insn_float64_sqrt
insn_float64_sqrt:
	fsqrtd	%f0,%f0

	.global	insn_float64_cmp
insn_float64_cmp:
	fcmpd	%fcc0,%f0,%f2

	.global	insn_float64_cmpe
insn_float64_cmpe:
	fcmped	%fcc0,%f0,%f2

	.global	insn_float128_to_int32_round_to_zero
insn_float128_to_int32_round_to_zero:
	fqtoi	%f0,%f0

	.global	insn_float128_to_int64_round_to_zero
insn_float128_to_int64_round_to_zero:
	fqtox	%f0,%f0

	.global	insn_float128_to_float32
insn_float128_to_float32:
	fqtos	%f0,%f0

	.global	insn_float128_to_float64
insn_float128_to_float64:
	fqtod	%f0,%f0

	.global	insn_float128_add
insn_float128_add:
	faddq	%f0,%f4,%f0

	.global	insn_float128_sub
insn_float128_sub:
	fsubq	%f0,%f4,%f0

	.global	insn_float128_mul
insn_float128_mul:
	fmulq	%f0,%f4,%f0

	.global	insn_float128_div
insn_float128_div:
	fdivq	%f0,%f4,%f0

	.global	insn_float128_sqrt
insn_float128_sqrt:
	fsqrtq	%f0,%f0

	.global	insn_float128_cmp
insn_float128_cmp:
	fcmpq	%fcc0,%f0,%f4

	.global	insn_float128_cmpe
insn_float128_cmpe:
	fcmpeq	%fcc0,%f0,%f4
