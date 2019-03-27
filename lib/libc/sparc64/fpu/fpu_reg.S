/*-
 * Copyright (c) 2002 by Thomas Moestl <tmm@FreeBSD.org>.
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

/*
 * Define arrays of leaf functions to load/store fp registers to memory. See
 * fpu_reg.h for the definitions to use this from C code. The function sizes
 * defines there must be kept in sync with this file!
 */

.macro ld32	reg
	retl
	 ld	[%o0], %f\reg
.endm

.macro st32	reg
	retl
	 st	%f\reg, [%o0]
.endm

.macro ld64	reg
	retl
	 ldd	[%o0], %f\reg
.endm

.macro st64	reg
	retl
	 std	%f\reg, [%o0]
.endm

/* The actual function arrays. */
	.globl	__fpu_ld32
__fpu_ld32:
	ld32	0
	ld32	1
	ld32	2
	ld32	3
	ld32	4
	ld32	5
	ld32	6
	ld32	7
	ld32	8
	ld32	9
	ld32	10
	ld32	11
	ld32	12
	ld32	13
	ld32	14
	ld32	15
	ld32	16
	ld32	17
	ld32	18
	ld32	19
	ld32	20
	ld32	21
	ld32	22
	ld32	23
	ld32	24
	ld32	25
	ld32	26
	ld32	27
	ld32	28
	ld32	29
	ld32	30
	ld32	31

	.globl	__fpu_st32
__fpu_st32:
	st32	0
	st32	1
	st32	2
	st32	3
	st32	4
	st32	5
	st32	6
	st32	7
	st32	8
	st32	9
	st32	10
	st32	11
	st32	12
	st32	13
	st32	14
	st32	15
	st32	16
	st32	17
	st32	18
	st32	19
	st32	20
	st32	21
	st32	22
	st32	23
	st32	24
	st32	25
	st32	26
	st32	27
	st32	28
	st32	29
	st32	30
	st32	31

	.globl	__fpu_ld64
__fpu_ld64:
	ld64	0
	ld64	2
	ld64	4
	ld64	6
	ld64	8
	ld64	10
	ld64	12
	ld64	14
	ld64	16
	ld64	18
	ld64	20
	ld64	22
	ld64	24
	ld64	26
	ld64	28
	ld64	30
	ld64	32
	ld64	34
	ld64	36
	ld64	38
	ld64	40
	ld64	42
	ld64	44
	ld64	46
	ld64	48
	ld64	50
	ld64	52
	ld64	54
	ld64	56
	ld64	58
	ld64	60
	ld64	62

	.globl	__fpu_st64
__fpu_st64:
	st64	0
	st64	2
	st64	4
	st64	6
	st64	8
	st64	10
	st64	12
	st64	14
	st64	16
	st64	18
	st64	20
	st64	22
	st64	24
	st64	26
	st64	28
	st64	30
	st64	32
	st64	34
	st64	36
	st64	38
	st64	40
	st64	42
	st64	44
	st64	46
	st64	48
	st64	50
	st64	52
	st64	54
	st64	56
	st64	58
	st64	60
	st64	62
