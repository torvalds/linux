/*	$OpenBSD: fp.S,v 1.2 1998/03/16 09:03:31 pefo Exp $	*/
/*-
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Ralph Campbell.
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
 *
 *	from: @(#)fp.s	8.1 (Berkeley) 6/10/93
 *	JNPR: fp.S,v 1.1 2006/08/07 05:38:57 katta
 * $FreeBSD$
 */

/*
 * Standard header stuff.
 */

#include <machine/asm.h>
#include <machine/regnum.h>
#include <machine/cpuregs.h>

#include "assym.inc"

#define	SEXP_INF	0xff
#define	DEXP_INF	0x7ff
#define	SEXP_BIAS	127
#define	DEXP_BIAS	1023
#define	SEXP_MIN	-126
#define	DEXP_MIN	-1022
#define	SEXP_MAX	127
#define	DEXP_MAX	1023
#define	WEXP_MAX	30		/* maximum unbiased exponent for int */
#define	WEXP_MIN	-1		/* minimum unbiased exponent for int */
#define	SFRAC_BITS	23
#define	DFRAC_BITS	52
#define	SIMPL_ONE	0x00800000
#define	DIMPL_ONE	0x00100000
#define	SLEAD_ZEROS	31 - 23
#define	DLEAD_ZEROS	31 - 20
#define	STICKYBIT	1
#define	GUARDBIT	0x80000000
#define	SSIGNAL_NAN	0x00400000
#define	DSIGNAL_NAN	0x00080000
#define	SQUIET_NAN	0x003fffff
#define	DQUIET_NAN0	0x0007ffff
#define	DQUIET_NAN1	0xffffffff
#define	INT_MIN		0x80000000
#define	INT_MAX		0x7fffffff

#define	COND_UNORDERED	0x1
#define	COND_EQUAL	0x2
#define	COND_LESS	0x4
#define	COND_SIGNAL	0x8

.set hardfloat

/*----------------------------------------------------------------------------
 *
 * MipsEmulateFP --
 *
 *	Emulate unimplemented floating point operations.
 *	This routine should only be called by MipsFPInterrupt().
 *
 *	MipsEmulateFP(instr)
 *		unsigned instr;
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Floating point registers are modified according to instruction.
 *
 *----------------------------------------------------------------------------
 */
NESTED(MipsEmulateFP, CALLFRAME_SIZ, ra)
	subu	sp, sp, CALLFRAME_SIZ
	sw	ra, CALLFRAME_RA(sp)
/*
 * Decode the FMT field (bits 24-21) and FUNCTION field (bits 5-0).
 */
	srl	v0, a0, 21 - 2			# get FMT field
	and	v0, v0, 0xF << 2		# mask FMT field
	and	v1, a0, 0x3F			# mask FUNC field
	sll	v1, v1, 5			# align for table lookup
	bgt	v0, 4 << 2, ill			# illegal format

	or	v1, v1, v0
	cfc1	a1, MIPS_FPU_CSR		# get exception register
	lw	a3, func_fmt_tbl(v1)		# switch on FUNC & FMT
	and	a1, a1, ~MIPS_FPU_EXCEPTION_UNIMPL # clear exception
	ctc1	a1, MIPS_FPU_CSR
	j	a3

	.rdata
func_fmt_tbl:
	.word	add_s		# 0
	.word	add_d		# 0
	.word	ill		# 0
	.word	ill		# 0
	.word	ill		# 0
	.word	ill		# 0
	.word	ill		# 0
	.word	ill		# 0
	.word	sub_s		# 1
	.word	sub_d		# 1
	.word	ill		# 1
	.word	ill		# 1
	.word	ill		# 1
	.word	ill		# 1
	.word	ill		# 1
	.word	ill		# 1
	.word	mul_s		# 2
	.word	mul_d		# 2
	.word	ill		# 2
	.word	ill		# 2
	.word	ill		# 2
	.word	ill		# 2
	.word	ill		# 2
	.word	ill		# 2
	.word	div_s		# 3
	.word	div_d		# 3
	.word	ill		# 3
	.word	ill		# 3
	.word	ill		# 3
	.word	ill		# 3
	.word	ill		# 3
	.word	ill		# 3
	.word	ill		# 4
	.word	ill		# 4
	.word	ill		# 4
	.word	ill		# 4
	.word	ill		# 4
	.word	ill		# 4
	.word	ill		# 4
	.word	ill		# 4
	.word	abs_s		# 5
	.word	abs_d		# 5
	.word	ill		# 5
	.word	ill		# 5
	.word	ill		# 5
	.word	ill		# 5
	.word	ill		# 5
	.word	ill		# 5
	.word	mov_s		# 6
	.word	mov_d		# 6
	.word	ill		# 6
	.word	ill		# 6
	.word	ill		# 6
	.word	ill		# 6
	.word	ill		# 6
	.word	ill		# 6
	.word	neg_s		# 7
	.word	neg_d		# 7
	.word	ill		# 7
	.word	ill		# 7
	.word	ill		# 7
	.word	ill		# 7
	.word	ill		# 7
	.word	ill		# 7
	.word	ill		# 8
	.word	ill		# 8
	.word	ill		# 8
	.word	ill		# 8
	.word	ill		# 8
	.word	ill		# 8
	.word	ill		# 8
	.word	ill		# 8
	.word	ill		# 9
	.word	ill		# 9
	.word	ill		# 9
	.word	ill		# 9
	.word	ill		# 9
	.word	ill		# 9
	.word	ill		# 9
	.word	ill		# 9
	.word	ill		# 10
	.word	ill		# 10
	.word	ill		# 10
	.word	ill		# 10
	.word	ill		# 10
	.word	ill		# 10
	.word	ill		# 10
	.word	ill		# 10
	.word	ill		# 11
	.word	ill		# 11
	.word	ill		# 11
	.word	ill		# 11
	.word	ill		# 11
	.word	ill		# 11
	.word	ill		# 11
	.word	ill		# 11
	.word	ill		# 12
	.word	ill		# 12
	.word	ill		# 12
	.word	ill		# 12
	.word	ill		# 12
	.word	ill		# 12
	.word	ill		# 12
	.word	ill		# 12
	.word	ill		# 13
	.word	ill		# 13
	.word	ill		# 13
	.word	ill		# 13
	.word	ill		# 13
	.word	ill		# 13
	.word	ill		# 13
	.word	ill		# 13
	.word	ill		# 14
	.word	ill		# 14
	.word	ill		# 14
	.word	ill		# 14
	.word	ill		# 14
	.word	ill		# 14
	.word	ill		# 14
	.word	ill		# 14
	.word	ill		# 15
	.word	ill		# 15
	.word	ill		# 15
	.word	ill		# 15
	.word	ill		# 15
	.word	ill		# 15
	.word	ill		# 15
	.word	ill		# 15
	.word	ill		# 16
	.word	ill		# 16
	.word	ill		# 16
	.word	ill		# 16
	.word	ill		# 16
	.word	ill		# 16
	.word	ill		# 16
	.word	ill		# 16
	.word	ill		# 17
	.word	ill		# 17
	.word	ill		# 17
	.word	ill		# 17
	.word	ill		# 17
	.word	ill		# 17
	.word	ill		# 17
	.word	ill		# 17
	.word	ill		# 18
	.word	ill		# 18
	.word	ill		# 18
	.word	ill		# 18
	.word	ill		# 18
	.word	ill		# 18
	.word	ill		# 18
	.word	ill		# 18
	.word	ill		# 19
	.word	ill		# 19
	.word	ill		# 19
	.word	ill		# 19
	.word	ill		# 19
	.word	ill		# 19
	.word	ill		# 19
	.word	ill		# 19
	.word	ill		# 20
	.word	ill		# 20
	.word	ill		# 20
	.word	ill		# 20
	.word	ill		# 20
	.word	ill		# 20
	.word	ill		# 20
	.word	ill		# 20
	.word	ill		# 21
	.word	ill		# 21
	.word	ill		# 21
	.word	ill		# 21
	.word	ill		# 21
	.word	ill		# 21
	.word	ill		# 21
	.word	ill		# 21
	.word	ill		# 22
	.word	ill		# 22
	.word	ill		# 22
	.word	ill		# 22
	.word	ill		# 22
	.word	ill		# 22
	.word	ill		# 22
	.word	ill		# 22
	.word	ill		# 23
	.word	ill		# 23
	.word	ill		# 23
	.word	ill		# 23
	.word	ill		# 23
	.word	ill		# 23
	.word	ill		# 23
	.word	ill		# 23
	.word	ill		# 24
	.word	ill		# 24
	.word	ill		# 24
	.word	ill		# 24
	.word	ill		# 24
	.word	ill		# 24
	.word	ill		# 24
	.word	ill		# 24
	.word	ill		# 25
	.word	ill		# 25
	.word	ill		# 25
	.word	ill		# 25
	.word	ill		# 25
	.word	ill		# 25
	.word	ill		# 25
	.word	ill		# 25
	.word	ill		# 26
	.word	ill		# 26
	.word	ill		# 26
	.word	ill		# 26
	.word	ill		# 26
	.word	ill		# 26
	.word	ill		# 26
	.word	ill		# 26
	.word	ill		# 27
	.word	ill		# 27
	.word	ill		# 27
	.word	ill		# 27
	.word	ill		# 27
	.word	ill		# 27
	.word	ill		# 27
	.word	ill		# 27
	.word	ill		# 28
	.word	ill		# 28
	.word	ill		# 28
	.word	ill		# 28
	.word	ill		# 28
	.word	ill		# 28
	.word	ill		# 28
	.word	ill		# 28
	.word	ill		# 29
	.word	ill		# 29
	.word	ill		# 29
	.word	ill		# 29
	.word	ill		# 29
	.word	ill		# 29
	.word	ill		# 29
	.word	ill		# 29
	.word	ill		# 30
	.word	ill		# 30
	.word	ill		# 30
	.word	ill		# 30
	.word	ill		# 30
	.word	ill		# 30
	.word	ill		# 30
	.word	ill		# 30
	.word	ill		# 31
	.word	ill		# 31
	.word	ill		# 31
	.word	ill		# 31
	.word	ill		# 31
	.word	ill		# 31
	.word	ill		# 31
	.word	ill		# 31
	.word	ill		# 32
	.word	cvt_s_d		# 32
	.word	ill		# 32
	.word	ill		# 32
	.word	cvt_s_w		# 32
	.word	ill		# 32
	.word	ill		# 32
	.word	ill		# 32
	.word	cvt_d_s		# 33
	.word	ill		# 33
	.word	ill		# 33
	.word	ill		# 33
	.word	cvt_d_w		# 33
	.word	ill		# 33
	.word	ill		# 33
	.word	ill		# 33
	.word	ill		# 34
	.word	ill		# 34
	.word	ill		# 34
	.word	ill		# 34
	.word	ill		# 34
	.word	ill		# 34
	.word	ill		# 34
	.word	ill		# 34
	.word	ill		# 35
	.word	ill		# 35
	.word	ill		# 35
	.word	ill		# 35
	.word	ill		# 35
	.word	ill		# 35
	.word	ill		# 35
	.word	ill		# 35
	.word	cvt_w_s		# 36
	.word	cvt_w_d		# 36
	.word	ill		# 36
	.word	ill		# 36
	.word	ill		# 36
	.word	ill		# 36
	.word	ill		# 36
	.word	ill		# 36
	.word	ill		# 37
	.word	ill		# 37
	.word	ill		# 37
	.word	ill		# 37
	.word	ill		# 37
	.word	ill		# 37
	.word	ill		# 37
	.word	ill		# 37
	.word	ill		# 38
	.word	ill		# 38
	.word	ill		# 38
	.word	ill		# 38
	.word	ill		# 38
	.word	ill		# 38
	.word	ill		# 38
	.word	ill		# 38
	.word	ill		# 39
	.word	ill		# 39
	.word	ill		# 39
	.word	ill		# 39
	.word	ill		# 39
	.word	ill		# 39
	.word	ill		# 39
	.word	ill		# 39
	.word	ill		# 40
	.word	ill		# 40
	.word	ill		# 40
	.word	ill		# 40
	.word	ill		# 40
	.word	ill		# 40
	.word	ill		# 40
	.word	ill		# 40
	.word	ill		# 41
	.word	ill		# 41
	.word	ill		# 41
	.word	ill		# 41
	.word	ill		# 41
	.word	ill		# 41
	.word	ill		# 41
	.word	ill		# 41
	.word	ill		# 42
	.word	ill		# 42
	.word	ill		# 42
	.word	ill		# 42
	.word	ill		# 42
	.word	ill		# 42
	.word	ill		# 42
	.word	ill		# 42
	.word	ill		# 43
	.word	ill		# 43
	.word	ill		# 43
	.word	ill		# 43
	.word	ill		# 43
	.word	ill		# 43
	.word	ill		# 43
	.word	ill		# 43
	.word	ill		# 44
	.word	ill		# 44
	.word	ill		# 44
	.word	ill		# 44
	.word	ill		# 44
	.word	ill		# 44
	.word	ill		# 44
	.word	ill		# 44
	.word	ill		# 45
	.word	ill		# 45
	.word	ill		# 45
	.word	ill		# 45
	.word	ill		# 45
	.word	ill		# 45
	.word	ill		# 45
	.word	ill		# 45
	.word	ill		# 46
	.word	ill		# 46
	.word	ill		# 46
	.word	ill		# 46
	.word	ill		# 46
	.word	ill		# 46
	.word	ill		# 46
	.word	ill		# 46
	.word	ill		# 47
	.word	ill		# 47
	.word	ill		# 47
	.word	ill		# 47
	.word	ill		# 47
	.word	ill		# 47
	.word	ill		# 47
	.word	ill		# 47
	.word	cmp_s		# 48
	.word	cmp_d		# 48
	.word	ill		# 48
	.word	ill		# 48
	.word	ill		# 48
	.word	ill		# 48
	.word	ill		# 48
	.word	ill		# 48
	.word	cmp_s		# 49
	.word	cmp_d		# 49
	.word	ill		# 49
	.word	ill		# 49
	.word	ill		# 49
	.word	ill		# 49
	.word	ill		# 49
	.word	ill		# 49
	.word	cmp_s		# 50
	.word	cmp_d		# 50
	.word	ill		# 50
	.word	ill		# 50
	.word	ill		# 50
	.word	ill		# 50
	.word	ill		# 50
	.word	ill		# 50
	.word	cmp_s		# 51
	.word	cmp_d		# 51
	.word	ill		# 51
	.word	ill		# 51
	.word	ill		# 51
	.word	ill		# 51
	.word	ill		# 51
	.word	ill		# 51
	.word	cmp_s		# 52
	.word	cmp_d		# 52
	.word	ill		# 52
	.word	ill		# 52
	.word	ill		# 52
	.word	ill		# 52
	.word	ill		# 52
	.word	ill		# 52
	.word	cmp_s		# 53
	.word	cmp_d		# 53
	.word	ill		# 53
	.word	ill		# 53
	.word	ill		# 53
	.word	ill		# 53
	.word	ill		# 53
	.word	ill		# 53
	.word	cmp_s		# 54
	.word	cmp_d		# 54
	.word	ill		# 54
	.word	ill		# 54
	.word	ill		# 54
	.word	ill		# 54
	.word	ill		# 54
	.word	ill		# 54
	.word	cmp_s		# 55
	.word	cmp_d		# 55
	.word	ill		# 55
	.word	ill		# 55
	.word	ill		# 55
	.word	ill		# 55
	.word	ill		# 55
	.word	ill		# 55
	.word	cmp_s		# 56
	.word	cmp_d		# 56
	.word	ill		# 56
	.word	ill		# 56
	.word	ill		# 56
	.word	ill		# 56
	.word	ill		# 56
	.word	ill		# 56
	.word	cmp_s		# 57
	.word	cmp_d		# 57
	.word	ill		# 57
	.word	ill		# 57
	.word	ill		# 57
	.word	ill		# 57
	.word	ill		# 57
	.word	ill		# 57
	.word	cmp_s		# 58
	.word	cmp_d		# 58
	.word	ill		# 58
	.word	ill		# 58
	.word	ill		# 58
	.word	ill		# 58
	.word	ill		# 58
	.word	ill		# 58
	.word	cmp_s		# 59
	.word	cmp_d		# 59
	.word	ill		# 59
	.word	ill		# 59
	.word	ill		# 59
	.word	ill		# 59
	.word	ill		# 59
	.word	ill		# 59
	.word	cmp_s		# 60
	.word	cmp_d		# 60
	.word	ill		# 60
	.word	ill		# 60
	.word	ill		# 60
	.word	ill		# 60
	.word	ill		# 60
	.word	ill		# 60
	.word	cmp_s		# 61
	.word	cmp_d		# 61
	.word	ill		# 61
	.word	ill		# 61
	.word	ill		# 61
	.word	ill		# 61
	.word	ill		# 61
	.word	ill		# 61
	.word	cmp_s		# 62
	.word	cmp_d		# 62
	.word	ill		# 62
	.word	ill		# 62
	.word	ill		# 62
	.word	ill		# 62
	.word	ill		# 62
	.word	ill		# 62
	.word	cmp_s		# 63
	.word	cmp_d		# 63
	.word	ill		# 63
	.word	ill		# 63
	.word	ill		# 63
	.word	ill		# 63
	.word	ill		# 63
	.word	ill		# 63
	.text

/*
 * Single precision subtract.
 */
sub_s:
	jal	get_ft_fs_s
	xor	ta0, ta0, 1			# negate FT sign bit
	b	add_sub_s
/*
 * Single precision add.
 */
add_s:
	jal	get_ft_fs_s
add_sub_s:
	bne	t1, SEXP_INF, 1f		# is FS an infinity?
	bne	ta1, SEXP_INF, result_fs_s	# if FT is not inf, result=FS
	bne	t2, zero, result_fs_s		# if FS is NAN, result is FS
	bne	ta2, zero, result_ft_s		# if FT is NAN, result is FT
	bne	t0, ta0, invalid_s		# both infinities same sign?
	b	result_fs_s			# result is in FS
1:
	beq	ta1, SEXP_INF, result_ft_s	# if FT is inf, result=FT
	bne	t1, zero, 4f			# is FS a denormalized num?
	beq	t2, zero, 3f			# is FS zero?
	bne	ta1, zero, 2f			# is FT a denormalized num?
	beq	ta2, zero, result_fs_s		# FT is zero, result=FS
	jal	renorm_fs_s
	jal	renorm_ft_s
	b	5f
2:
	jal	renorm_fs_s
	subu	ta1, ta1, SEXP_BIAS		# unbias FT exponent
	or	ta2, ta2, SIMPL_ONE		# set implied one bit
	b	5f
3:
	bne	ta1, zero, result_ft_s		# if FT != 0, result=FT
	bne	ta2, zero, result_ft_s
	and	v0, a1, MIPS_FPU_ROUNDING_BITS	# get rounding mode
	bne	v0, MIPS_FPU_ROUND_RM, 1f	# round to -infinity?
	or	t0, t0, ta0			# compute result sign
	b	result_fs_s
1:
	and	t0, t0, ta0			# compute result sign
	b	result_fs_s
4:
	bne	ta1, zero, 2f			# is FT a denormalized num?
	beq	ta2, zero, result_fs_s		# FT is zero, result=FS
	subu	t1, t1, SEXP_BIAS		# unbias FS exponent
	or	t2, t2, SIMPL_ONE		# set implied one bit
	jal	renorm_ft_s
	b	5f
2:
	subu	t1, t1, SEXP_BIAS		# unbias FS exponent
	or	t2, t2, SIMPL_ONE		# set implied one bit
	subu	ta1, ta1, SEXP_BIAS		# unbias FT exponent
	or	ta2, ta2, SIMPL_ONE		# set implied one bit
/*
 * Perform the addition.
 */
5:
	move	t8, zero			# no shifted bits (sticky reg)
	beq	t1, ta1, 4f			# no shift needed
	subu	v0, t1, ta1			# v0 = difference of exponents
	move	v1, v0				# v1 = abs(difference)
	bge	v0, zero, 1f
	negu	v1
1:
	ble	v1, SFRAC_BITS+2, 2f		# is difference too great?
	li	t8, STICKYBIT			# set the sticky bit
	bge	v0, zero, 1f			# check which exp is larger
	move	t1, ta1				# result exp is FTs
	move	t2, zero			# FSs fraction shifted is zero
	b	4f
1:
	move	ta2, zero			# FTs fraction shifted is zero
	b	4f
2:
	li	t9, 32				# compute 32 - abs(exp diff)
	subu	t9, t9, v1
	bgt	v0, zero, 3f			# if FS > FT, shift FTs frac
	move	t1, ta1				# FT > FS, result exp is FTs
	sll	t8, t2, t9			# save bits shifted out
	srl	t2, t2, v1			# shift FSs fraction
	b	4f
3:
	sll	t8, ta2, t9			# save bits shifted out
	srl	ta2, ta2, v1			# shift FTs fraction
4:
	bne	t0, ta0, 1f			# if signs differ, subtract
	addu	t2, t2, ta2			# add fractions
	b	norm_s
1:
	blt	t2, ta2, 3f			# subtract larger from smaller
	bne	t2, ta2, 2f			# if same, result=0
	move	t1, zero			# result=0
	move	t2, zero
	and	v0, a1, MIPS_FPU_ROUNDING_BITS	# get rounding mode
	bne	v0, MIPS_FPU_ROUND_RM, 1f	# round to -infinity?
	or	t0, t0, ta0			# compute result sign
	b	result_fs_s
1:
	and	t0, t0, ta0			# compute result sign
	b	result_fs_s
2:
	sltu	t9, zero, t8			# compute t2:zero - ta2:t8
	subu	t8, zero, t8
	subu	t2, t2, ta2			# subtract fractions
	subu	t2, t2, t9			# subtract barrow
	b	norm_s
3:
	move	t0, ta0				# sign of result = FTs
	sltu	t9, zero, t8			# compute ta2:zero - t2:t8
	subu	t8, zero, t8
	subu	t2, ta2, t2			# subtract fractions
	subu	t2, t2, t9			# subtract barrow
	b	norm_s

/*
 * Double precision subtract.
 */
sub_d:
	jal	get_ft_fs_d
	xor	ta0, ta0, 1			# negate sign bit
	b	add_sub_d
/*
 * Double precision add.
 */
add_d:
	jal	get_ft_fs_d
add_sub_d:
	bne	t1, DEXP_INF, 1f		# is FS an infinity?
	bne	ta1, DEXP_INF, result_fs_d	# if FT is not inf, result=FS
	bne	t2, zero, result_fs_d		# if FS is NAN, result is FS
	bne	t3, zero, result_fs_d
	bne	ta2, zero, result_ft_d		# if FT is NAN, result is FT
	bne	ta3, zero, result_ft_d
	bne	t0, ta0, invalid_d		# both infinities same sign?
	b	result_fs_d			# result is in FS
1:
	beq	ta1, DEXP_INF, result_ft_d	# if FT is inf, result=FT
	bne	t1, zero, 4f			# is FS a denormalized num?
	bne	t2, zero, 1f			# is FS zero?
	beq	t3, zero, 3f
1:
	bne	ta1, zero, 2f			# is FT a denormalized num?
	bne	ta2, zero, 1f
	beq	ta3, zero, result_fs_d		# FT is zero, result=FS
1:
	jal	renorm_fs_d
	jal	renorm_ft_d
	b	5f
2:
	jal	renorm_fs_d
	subu	ta1, ta1, DEXP_BIAS		# unbias FT exponent
	or	ta2, ta2, DIMPL_ONE		# set implied one bit
	b	5f
3:
	bne	ta1, zero, result_ft_d		# if FT != 0, result=FT
	bne	ta2, zero, result_ft_d
	bne	ta3, zero, result_ft_d
	and	v0, a1, MIPS_FPU_ROUNDING_BITS	# get rounding mode
	bne	v0, MIPS_FPU_ROUND_RM, 1f	# round to -infinity?
	or	t0, t0, ta0			# compute result sign
	b	result_fs_d
1:
	and	t0, t0, ta0			# compute result sign
	b	result_fs_d
4:
	bne	ta1, zero, 2f			# is FT a denormalized num?
	bne	ta2, zero, 1f
	beq	ta3, zero, result_fs_d		# FT is zero, result=FS
1:
	subu	t1, t1, DEXP_BIAS		# unbias FS exponent
	or	t2, t2, DIMPL_ONE		# set implied one bit
	jal	renorm_ft_d
	b	5f
2:
	subu	t1, t1, DEXP_BIAS		# unbias FS exponent
	or	t2, t2, DIMPL_ONE		# set implied one bit
	subu	ta1, ta1, DEXP_BIAS		# unbias FT exponent
	or	ta2, ta2, DIMPL_ONE		# set implied one bit
/*
 * Perform the addition.
 */
5:
	move	t8, zero			# no shifted bits (sticky reg)
	beq	t1, ta1, 4f			# no shift needed
	subu	v0, t1, ta1			# v0 = difference of exponents
	move	v1, v0				# v1 = abs(difference)
	bge	v0, zero, 1f
	negu	v1
1:
	ble	v1, DFRAC_BITS+2, 2f		# is difference too great?
	li	t8, STICKYBIT			# set the sticky bit
	bge	v0, zero, 1f			# check which exp is larger
	move	t1, ta1				# result exp is FTs
	move	t2, zero			# FSs fraction shifted is zero
	move	t3, zero
	b	4f
1:
	move	ta2, zero			# FTs fraction shifted is zero
	move	ta3, zero
	b	4f
2:
	li	t9, 32
	bge	v0, zero, 3f			# if FS > FT, shift FTs frac
	move	t1, ta1				# FT > FS, result exp is FTs
	blt	v1, t9, 1f			# shift right by < 32?
	subu	v1, v1, t9
	subu	t9, t9, v1
	sll	t8, t2, t9			# save bits shifted out
	sltu	t9, zero, t3			# dont lose any one bits
	or	t8, t8, t9			# save sticky bit
	srl	t3, t2, v1			# shift FSs fraction
	move	t2, zero
	b	4f
1:
	subu	t9, t9, v1
	sll	t8, t3, t9			# save bits shifted out
	srl	t3, t3, v1			# shift FSs fraction
	sll	t9, t2, t9			# save bits shifted out of t2
	or	t3, t3, t9			# and put into t3
	srl	t2, t2, v1
	b	4f
3:
	blt	v1, t9, 1f			# shift right by < 32?
	subu	v1, v1, t9
	subu	t9, t9, v1
	sll	t8, ta2, t9			# save bits shifted out
	srl	ta3, ta2, v1			# shift FTs fraction
	move	ta2, zero
	b	4f
1:
	subu	t9, t9, v1
	sll	t8, ta3, t9			# save bits shifted out
	srl	ta3, ta3, v1			# shift FTs fraction
	sll	t9, ta2, t9			# save bits shifted out of t2
	or	ta3, ta3, t9			# and put into t3
	srl	ta2, ta2, v1
4:
	bne	t0, ta0, 1f			# if signs differ, subtract
	addu	t3, t3, ta3			# add fractions
	sltu	t9, t3, ta3			# compute carry
	addu	t2, t2, ta2			# add fractions
	addu	t2, t2, t9			# add carry
	b	norm_d
1:
	blt	t2, ta2, 3f			# subtract larger from smaller
	bne	t2, ta2, 2f
	bltu	t3, ta3, 3f
	bne	t3, ta3, 2f			# if same, result=0
	move	t1, zero			# result=0
	move	t2, zero
	move	t3, zero
	and	v0, a1, MIPS_FPU_ROUNDING_BITS	# get rounding mode
	bne	v0, MIPS_FPU_ROUND_RM, 1f	# round to -infinity?
	or	t0, t0, ta0			# compute result sign
	b	result_fs_d
1:
	and	t0, t0, ta0			# compute result sign
	b	result_fs_d
2:
	beq	t8, zero, 1f			# compute t2:t3:zero - ta2:ta3:t8
	subu	t8, zero, t8
	sltu	v0, t3, 1			# compute barrow out
	subu	t3, t3, 1			# subtract barrow
	subu	t2, t2, v0
1:
	sltu	v0, t3, ta3
	subu	t3, t3, ta3			# subtract fractions
	subu	t2, t2, ta2			# subtract fractions
	subu	t2, t2, v0			# subtract barrow
	b	norm_d
3:
	move	t0, ta0				# sign of result = FTs
	beq	t8, zero, 1f			# compute ta2:ta3:zero - t2:t3:t8
	subu	t8, zero, t8
	sltu	v0, ta3, 1			# compute barrow out
	subu	ta3, ta3, 1			# subtract barrow
	subu	ta2, ta2, v0
1:
	sltu	v0, ta3, t3
	subu	t3, ta3, t3			# subtract fractions
	subu	t2, ta2, t2			# subtract fractions
	subu	t2, t2, v0			# subtract barrow
	b	norm_d

/*
 * Single precision multiply.
 */
mul_s:
	jal	get_ft_fs_s
	xor	t0, t0, ta0			# compute sign of result
	move	ta0, t0
	bne	t1, SEXP_INF, 2f		# is FS an infinity?
	bne	t2, zero, result_fs_s		# if FS is a NAN, result=FS
	bne	ta1, SEXP_INF, 1f		# FS is inf, is FT an infinity?
	bne	ta2, zero, result_ft_s		# if FT is a NAN, result=FT
	b	result_fs_s			# result is infinity
1:
	bne	ta1, zero, result_fs_s		# inf * zero? if no, result=FS
	bne	ta2, zero, result_fs_s
	b	invalid_s			# infinity * zero is invalid
2:
	bne	ta1, SEXP_INF, 1f		# FS != inf, is FT an infinity?
	bne	t1, zero, result_ft_s		# zero * inf? if no, result=FT
	bne	t2, zero, result_ft_s
	bne	ta2, zero, result_ft_s		# if FT is a NAN, result=FT
	b	invalid_s			# zero * infinity is invalid
1:
	bne	t1, zero, 1f			# is FS zero?
	beq	t2, zero, result_fs_s		# result is zero
	jal	renorm_fs_s
	b	2f
1:
	subu	t1, t1, SEXP_BIAS		# unbias FS exponent
	or	t2, t2, SIMPL_ONE		# set implied one bit
2:
	bne	ta1, zero, 1f			# is FT zero?
	beq	ta2, zero, result_ft_s		# result is zero
	jal	renorm_ft_s
	b	2f
1:
	subu	ta1, ta1, SEXP_BIAS		# unbias FT exponent
	or	ta2, ta2, SIMPL_ONE		# set implied one bit
2:
	addu	t1, t1, ta1			# compute result exponent
	addu	t1, t1, 9			# account for binary point
	multu	t2, ta2				# multiply fractions
	mflo	t8
	mfhi	t2
	b	norm_s

/*
 * Double precision multiply.
 */
mul_d:
	jal	get_ft_fs_d
	xor	t0, t0, ta0			# compute sign of result
	move	ta0, t0
	bne	t1, DEXP_INF, 2f		# is FS an infinity?
	bne	t2, zero, result_fs_d		# if FS is a NAN, result=FS
	bne	t3, zero, result_fs_d
	bne	ta1, DEXP_INF, 1f		# FS is inf, is FT an infinity?
	bne	ta2, zero, result_ft_d		# if FT is a NAN, result=FT
	bne	ta3, zero, result_ft_d
	b	result_fs_d			# result is infinity
1:
	bne	ta1, zero, result_fs_d		# inf * zero? if no, result=FS
	bne	ta2, zero, result_fs_d
	bne	ta3, zero, result_fs_d
	b	invalid_d			# infinity * zero is invalid
2:
	bne	ta1, DEXP_INF, 1f		# FS != inf, is FT an infinity?
	bne	t1, zero, result_ft_d		# zero * inf? if no, result=FT
	bne	t2, zero, result_ft_d		# if FS is a NAN, result=FS
	bne	t3, zero, result_ft_d
	bne	ta2, zero, result_ft_d		# if FT is a NAN, result=FT
	bne	ta3, zero, result_ft_d
	b	invalid_d			# zero * infinity is invalid
1:
	bne	t1, zero, 2f			# is FS zero?
	bne	t2, zero, 1f
	beq	t3, zero, result_fs_d		# result is zero
1:
	jal	renorm_fs_d
	b	3f
2:
	subu	t1, t1, DEXP_BIAS		# unbias FS exponent
	or	t2, t2, DIMPL_ONE		# set implied one bit
3:
	bne	ta1, zero, 2f			# is FT zero?
	bne	ta2, zero, 1f
	beq	ta3, zero, result_ft_d		# result is zero
1:
	jal	renorm_ft_d
	b	3f
2:
	subu	ta1, ta1, DEXP_BIAS		# unbias FT exponent
	or	ta2, ta2, DIMPL_ONE		# set implied one bit
3:
	addu	t1, t1, ta1			# compute result exponent
	addu	t1, t1, 12			# ???
	multu	t3, ta3				# multiply fractions (low * low)
	move	ta0, t2				# free up t2,t3 for result
	move	ta1, t3
	mflo	a3				# save low order bits
	mfhi	t8
	not	v0, t8
	multu	ta0, ta3			# multiply FS(high) * FT(low)
	mflo	v1
	mfhi	t3				# init low result
	sltu	v0, v0, v1			# compute carry
	addu	t8, v1
	multu	ta1, ta2			# multiply FS(low) * FT(high)
	addu	t3, t3, v0			# add carry
	not	v0, t8
	mflo	v1
	mfhi	t2
	sltu	v0, v0, v1
	addu	t8, v1
	multu	ta0, ta2			# multiply FS(high) * FT(high)
	addu	t3, v0
	not	v1, t3
	sltu	v1, v1, t2
	addu	t3, t2
	not	v0, t3
	mfhi	t2
	addu	t2, v1
	mflo	v1
	sltu	v0, v0, v1
	addu	t2, v0
	addu	t3, v1
	sltu	a3, zero, a3			# reduce t8,a3 to just t8
	or	t8, a3
	b	norm_d

/*
 * Single precision divide.
 */
div_s:
	jal	get_ft_fs_s
	xor	t0, t0, ta0			# compute sign of result
	move	ta0, t0
	bne	t1, SEXP_INF, 1f		# is FS an infinity?
	bne	t2, zero, result_fs_s		# if FS is NAN, result is FS
	bne	ta1, SEXP_INF, result_fs_s	# is FT an infinity?
	bne	ta2, zero, result_ft_s		# if FT is NAN, result is FT
	b	invalid_s			# infinity/infinity is invalid
1:
	bne	ta1, SEXP_INF, 1f		# is FT an infinity?
	bne	ta2, zero, result_ft_s		# if FT is NAN, result is FT
	move	t1, zero			# x / infinity is zero
	move	t2, zero
	b	result_fs_s
1:
	bne	t1, zero, 2f			# is FS zero?
	bne	t2, zero, 1f
	bne	ta1, zero, result_fs_s		# FS=zero, is FT zero?
	beq	ta2, zero, invalid_s		# 0 / 0
	b	result_fs_s			# result = zero
1:
	jal	renorm_fs_s
	b	3f
2:
	subu	t1, t1, SEXP_BIAS		# unbias FS exponent
	or	t2, t2, SIMPL_ONE		# set implied one bit
3:
	bne	ta1, zero, 2f			# is FT zero?
	bne	ta2, zero, 1f
	or	a1, a1, MIPS_FPU_EXCEPTION_DIV0 | MIPS_FPU_STICKY_DIV0
	and	v0, a1, MIPS_FPU_ENABLE_DIV0 	# trap enabled?
	bne	v0, zero, fpe_trap
	ctc1	a1, MIPS_FPU_CSR		# save exceptions
	li	t1, SEXP_INF			# result is infinity
	move	t2, zero
	b	result_fs_s
1:
	jal	renorm_ft_s
	b	3f
2:
	subu	ta1, ta1, SEXP_BIAS		# unbias FT exponent
	or	ta2, ta2, SIMPL_ONE		# set implied one bit
3:
	subu	t1, t1, ta1			# compute exponent
	subu	t1, t1, 3			# compensate for result position
	li	v0, SFRAC_BITS+3		# number of bits to divide
	move	t8, t2				# init dividend
	move	t2, zero			# init result
1:
	bltu	t8, ta2, 3f			# is dividend >= divisor?
2:
	subu	t8, t8, ta2			# subtract divisor from dividend
	or	t2, t2, 1			# remember that we did
	bne	t8, zero, 3f			# if not done, continue
	sll	t2, t2, v0			# shift result to final position
	b	norm_s
3:
	sll	t8, t8, 1			# shift dividend
	sll	t2, t2, 1			# shift result
	subu	v0, v0, 1			# are we done?
	bne	v0, zero, 1b			# no, continue
	b	norm_s

/*
 * Double precision divide.
 */
div_d:
	jal	get_ft_fs_d
	xor	t0, t0, ta0			# compute sign of result
	move	ta0, t0
	bne	t1, DEXP_INF, 1f		# is FS an infinity?
	bne	t2, zero, result_fs_d		# if FS is NAN, result is FS
	bne	t3, zero, result_fs_d
	bne	ta1, DEXP_INF, result_fs_d	# is FT an infinity?
	bne	ta2, zero, result_ft_d		# if FT is NAN, result is FT
	bne	ta3, zero, result_ft_d
	b	invalid_d			# infinity/infinity is invalid
1:
	bne	ta1, DEXP_INF, 1f		# is FT an infinity?
	bne	ta2, zero, result_ft_d		# if FT is NAN, result is FT
	bne	ta3, zero, result_ft_d
	move	t1, zero			# x / infinity is zero
	move	t2, zero
	move	t3, zero
	b	result_fs_d
1:
	bne	t1, zero, 2f			# is FS zero?
	bne	t2, zero, 1f
	bne	t3, zero, 1f
	bne	ta1, zero, result_fs_d		# FS=zero, is FT zero?
	bne	ta2, zero, result_fs_d
	beq	ta3, zero, invalid_d		# 0 / 0
	b	result_fs_d			# result = zero
1:
	jal	renorm_fs_d
	b	3f
2:
	subu	t1, t1, DEXP_BIAS		# unbias FS exponent
	or	t2, t2, DIMPL_ONE		# set implied one bit
3:
	bne	ta1, zero, 2f			# is FT zero?
	bne	ta2, zero, 1f
	bne	ta3, zero, 1f
	or	a1, a1, MIPS_FPU_EXCEPTION_DIV0 | MIPS_FPU_STICKY_DIV0
	and	v0, a1, MIPS_FPU_ENABLE_DIV0	# trap enabled?
	bne	v0, zero, fpe_trap
	ctc1	a1, MIPS_FPU_CSR		# Save exceptions
	li	t1, DEXP_INF			# result is infinity
	move	t2, zero
	move	t3, zero
	b	result_fs_d
1:
	jal	renorm_ft_d
	b	3f
2:
	subu	ta1, ta1, DEXP_BIAS		# unbias FT exponent
	or	ta2, ta2, DIMPL_ONE		# set implied one bit
3:
	subu	t1, t1, ta1			# compute exponent
	subu	t1, t1, 3			# compensate for result position
	li	v0, DFRAC_BITS+3		# number of bits to divide
	move	t8, t2				# init dividend
	move	t9, t3
	move	t2, zero			# init result
	move	t3, zero
1:
	bltu	t8, ta2, 3f			# is dividend >= divisor?
	bne	t8, ta2, 2f
	bltu	t9, ta3, 3f
2:
	sltu	v1, t9, ta3			# subtract divisor from dividend
	subu	t9, t9, ta3
	subu	t8, t8, ta2
	subu	t8, t8, v1
	or	t3, t3, 1			# remember that we did
	bne	t8, zero, 3f			# if not done, continue
	bne	t9, zero, 3f
	li	v1, 32				# shift result to final position
	blt	v0, v1, 2f			# shift < 32 bits?
	subu	v0, v0, v1			# shift by > 32 bits
	sll	t2, t3, v0			# shift upper part
	move	t3, zero
	b	norm_d
2:
	subu	v1, v1, v0			# shift by < 32 bits
	sll	t2, t2, v0			# shift upper part
	srl	t9, t3, v1			# save bits shifted out
	or	t2, t2, t9			# and put into upper part
	sll	t3, t3, v0
	b	norm_d
3:
	sll	t8, t8, 1			# shift dividend
	srl	v1, t9, 31			# save bit shifted out
	or	t8, t8, v1			# and put into upper part
	sll	t9, t9, 1
	sll	t2, t2, 1			# shift result
	srl	v1, t3, 31			# save bit shifted out
	or	t2, t2, v1			# and put into upper part
	sll	t3, t3, 1
	subu	v0, v0, 1			# are we done?
	bne	v0, zero, 1b			# no, continue
	sltu	v0, zero, t9			# be sure to save any one bits
	or	t8, t8, v0			# from the lower remainder
	b	norm_d

/*
 * Single precision absolute value.
 */
abs_s:
	jal	get_fs_s
	move	t0, zero			# set sign positive
	b	result_fs_s

/*
 * Double precision absolute value.
 */
abs_d:
	jal	get_fs_d
	move	t0, zero			# set sign positive
	b	result_fs_d

/*
 * Single precision move.
 */
mov_s:
	jal	get_fs_s
	b	result_fs_s

/*
 * Double precision move.
 */
mov_d:
	jal	get_fs_d
	b	result_fs_d

/*
 * Single precision negate.
 */
neg_s:
	jal	get_fs_s
	xor	t0, t0, 1			# reverse sign
	b	result_fs_s

/*
 * Double precision negate.
 */
neg_d:
	jal	get_fs_d
	xor	t0, t0, 1			# reverse sign
	b	result_fs_d

/*
 * Convert double to single.
 */
cvt_s_d:
	jal	get_fs_d
	bne	t1, DEXP_INF, 1f		# is FS an infinity?
	li	t1, SEXP_INF			# convert to single
	sll	t2, t2, 3			# convert D fraction to S
	srl	t8, t3, 32 - 3
	or	t2, t2, t8
	b	result_fs_s
1:
	bne	t1, zero, 2f			# is FS zero?
	bne	t2, zero, 1f
	beq	t3, zero, result_fs_s		# result=0
1:
	jal	renorm_fs_d
	subu	t1, t1, 3			# correct exp for shift below
	b	3f
2:
	subu	t1, t1, DEXP_BIAS		# unbias exponent
	or	t2, t2, DIMPL_ONE		# add implied one bit
3:
	sll	t2, t2, 3			# convert D fraction to S
	srl	t8, t3, 32 - 3
	or	t2, t2, t8
	sll	t8, t3, 3
	b	norm_noshift_s

/*
 * Convert integer to single.
 */
cvt_s_w:
	jal	get_fs_int
	bne	t2, zero, 1f			# check for zero
	move	t1, zero
	b	result_fs_s
/*
 * Find out how many leading zero bits are in t2 and put in t9.
 */
1:
	move	v0, t2
	move	t9, zero
	srl	v1, v0, 16
	bne	v1, zero, 1f
	addu	t9, 16
	sll	v0, 16
1:
	srl	v1, v0, 24
	bne	v1, zero, 1f
	addu	t9, 8
	sll	v0, 8
1:
	srl	v1, v0, 28
	bne	v1, zero, 1f
	addu	t9, 4
	sll	v0, 4
1:
	srl	v1, v0, 30
	bne	v1, zero, 1f
	addu	t9, 2
	sll	v0, 2
1:
	srl	v1, v0, 31
	bne	v1, zero, 1f
	addu	t9, 1
/*
 * Now shift t2 the correct number of bits.
 */
1:
	subu	t9, t9, SLEAD_ZEROS		# dont count leading zeros
	li	t1, 23				# init exponent
	subu	t1, t1, t9			# compute exponent
	beq	t9, zero, 1f
	li	v0, 32
	blt	t9, zero, 2f			# if shift < 0, shift right
	subu	v0, v0, t9
	sll	t2, t2, t9			# shift left
1:
	add	t1, t1, SEXP_BIAS		# bias exponent
	and	t2, t2, ~SIMPL_ONE		# clear implied one bit
	b	result_fs_s
2:
	negu	t9				# shift right by t9
	subu	v0, v0, t9
	sll	t8, t2, v0			# save bits shifted out
	srl	t2, t2, t9
	b	norm_noshift_s

/*
 * Convert single to double.
 */
cvt_d_s:
	jal	get_fs_s
	move	t3, zero
	bne	t1, SEXP_INF, 1f		# is FS an infinity?
	li	t1, DEXP_INF			# convert to double
	b	result_fs_d
1:
	bne	t1, zero, 2f			# is FS denormalized or zero?
	beq	t2, zero, result_fs_d		# is FS zero?
	jal	renorm_fs_s
	move	t8, zero
	b	norm_d
2:
	addu	t1, t1, DEXP_BIAS - SEXP_BIAS	# bias exponent correctly
	sll	t3, t2, 32 - 3			# convert S fraction to D
	srl	t2, t2, 3
	b	result_fs_d

/*
 * Convert integer to double.
 */
cvt_d_w:
	jal	get_fs_int
	bne	t2, zero, 1f			# check for zero
	move	t1, zero			# result=0
	move	t3, zero
	b	result_fs_d
/*
 * Find out how many leading zero bits are in t2 and put in t9.
 */
1:
	move	v0, t2
	move	t9, zero
	srl	v1, v0, 16
	bne	v1, zero, 1f
	addu	t9, 16
	sll	v0, 16
1:
	srl	v1, v0, 24
	bne	v1, zero, 1f
	addu	t9, 8
	sll	v0, 8
1:
	srl	v1, v0, 28
	bne	v1, zero, 1f
	addu	t9, 4
	sll	v0, 4
1:
	srl	v1, v0, 30
	bne	v1, zero, 1f
	addu	t9, 2
	sll	v0, 2
1:
	srl	v1, v0, 31
	bne	v1, zero, 1f
	addu	t9, 1
/*
 * Now shift t2 the correct number of bits.
 */
1:
	subu	t9, t9, DLEAD_ZEROS		# dont count leading zeros
	li	t1, DEXP_BIAS + 20		# init exponent
	subu	t1, t1, t9			# compute exponent
	beq	t9, zero, 1f
	li	v0, 32
	blt	t9, zero, 2f			# if shift < 0, shift right
	subu	v0, v0, t9
	sll	t2, t2, t9			# shift left
1:
	and	t2, t2, ~DIMPL_ONE		# clear implied one bit
	move	t3, zero
	b	result_fs_d
2:
	negu	t9				# shift right by t9
	subu	v0, v0, t9
	sll	t3, t2, v0
	srl	t2, t2, t9
	and	t2, t2, ~DIMPL_ONE		# clear implied one bit
	b	result_fs_d

/*
 * Convert single to integer.
 */
cvt_w_s:
	jal	get_fs_s
	bne	t1, SEXP_INF, 1f		# is FS an infinity?
	bne	t2, zero, invalid_w		# invalid conversion
1:
	bne	t1, zero, 1f			# is FS zero?
	beq	t2, zero, result_fs_w		# result is zero
	move	t2, zero			# result is an inexact zero
	b	inexact_w
1:
	subu	t1, t1, SEXP_BIAS		# unbias exponent
	or	t2, t2, SIMPL_ONE		# add implied one bit
	sll	t3, t2, 32 - 3			# convert S fraction to D
	srl	t2, t2, 3
	b	cvt_w

/*
 * Convert double to integer.
 */
cvt_w_d:
	jal	get_fs_d
	bne	t1, DEXP_INF, 1f		# is FS an infinity?
	bne	t2, zero, invalid_w		# invalid conversion
	bne	t3, zero, invalid_w		# invalid conversion
1:
	bne	t1, zero, 2f			# is FS zero?
	bne	t2, zero, 1f
	beq	t3, zero, result_fs_w		# result is zero
1:
	move	t2, zero			# result is an inexact zero
	b	inexact_w
2:
	subu	t1, t1, DEXP_BIAS		# unbias exponent
	or	t2, t2, DIMPL_ONE		# add implied one bit
cvt_w:
	blt	t1, WEXP_MIN, underflow_w	# is exponent too small?
	li	v0, WEXP_MAX+1
	bgt	t1, v0, overflow_w		# is exponent too large?
	bne	t1, v0, 1f			# special check for INT_MIN
	beq	t0, zero, overflow_w		# if positive, overflow
	bne	t2, DIMPL_ONE, overflow_w
	bne	t3, zero, overflow_w
	li	t2, INT_MIN			# result is INT_MIN
	b	result_fs_w
1:
	subu	v0, t1, 20			# compute amount to shift
	beq	v0, zero, 2f			# is shift needed?
	li	v1, 32
	blt	v0, zero, 1f			# if shift < 0, shift right
	subu	v1, v1, v0			# shift left
	sll	t2, t2, v0
	srl	t9, t3, v1			# save bits shifted out of t3
	or	t2, t2, t9			# and put into t2
	sll	t3, t3, v0			# shift FSs fraction
	b	2f
1:
	negu	v0				# shift right by v0
	subu	v1, v1, v0
	sll	t8, t3, v1			# save bits shifted out
	sltu	t8, zero, t8			# dont lose any ones
	srl	t3, t3, v0			# shift FSs fraction
	or	t3, t3, t8
	sll	t9, t2, v1			# save bits shifted out of t2
	or	t3, t3, t9			# and put into t3
	srl	t2, t2, v0
/*
 * round result (t0 is sign, t2 is integer part, t3 is fractional part).
 */
2:
	and	v0, a1, MIPS_FPU_ROUNDING_BITS	# get rounding mode
	beq	v0, MIPS_FPU_ROUND_RN, 3f	# round to nearest
	beq	v0, MIPS_FPU_ROUND_RZ, 5f	# round to zero (truncate)
	beq	v0, MIPS_FPU_ROUND_RP, 1f	# round to +infinity
	beq	t0, zero, 5f			# if sign is positive, truncate
	b	2f
1:
	bne	t0, zero, 5f			# if sign is negative, truncate
2:
	beq	t3, zero, 5f			# if no fraction bits, continue
	addu	t2, t2, 1			# add rounding bit
	blt	t2, zero, overflow_w		# overflow?
	b	5f
3:
	li	v0, GUARDBIT			# load guard bit for rounding
	addu	v0, v0, t3			# add remainder
	sltu	v1, v0, t3			# compute carry out
	beq	v1, zero, 4f			# if no carry, continue
	addu	t2, t2, 1			# add carry to result
	blt	t2, zero, overflow_w		# overflow?
4:
	bne	v0, zero, 5f			# if rounded remainder is zero
	and	t2, t2, ~1			#  clear LSB (round to nearest)
5:
	beq	t0, zero, 1f			# result positive?
	negu	t2				# convert to negative integer
1:
	beq	t3, zero, result_fs_w		# is result exact?
/*
 * Handle inexact exception.
 */
inexact_w:
	or	a1, a1, MIPS_FPU_EXCEPTION_INEXACT | MIPS_FPU_STICKY_INEXACT
	and	v0, a1, MIPS_FPU_ENABLE_INEXACT
	bne	v0, zero, fpe_trap
	ctc1	a1, MIPS_FPU_CSR		# save exceptions
	b	result_fs_w

/*
 * Conversions to integer which overflow will trap (if enabled),
 * or generate an inexact trap (if enabled),
 * or generate an invalid exception.
 */
overflow_w:
	or	a1, a1, MIPS_FPU_EXCEPTION_OVERFLOW | MIPS_FPU_STICKY_OVERFLOW
	and	v0, a1, MIPS_FPU_ENABLE_OVERFLOW
	bne	v0, zero, fpe_trap
	and	v0, a1, MIPS_FPU_ENABLE_INEXACT
	bne	v0, zero, inexact_w		# inexact traps enabled?
	b	invalid_w

/*
 * Conversions to integer which underflow will trap (if enabled),
 * or generate an inexact trap (if enabled),
 * or generate an invalid exception.
 */
underflow_w:
	or	a1, a1, MIPS_FPU_EXCEPTION_UNDERFLOW | MIPS_FPU_STICKY_UNDERFLOW
	and	v0, a1, MIPS_FPU_ENABLE_UNDERFLOW
	bne	v0, zero, fpe_trap
	and	v0, a1, MIPS_FPU_ENABLE_INEXACT
	bne	v0, zero, inexact_w		# inexact traps enabled?
	b	invalid_w

/*
 * Compare single.
 */
cmp_s:
	jal	get_cmp_s
	bne	t1, SEXP_INF, 1f		# is FS an infinity?
	bne	t2, zero, unordered		# FS is a NAN
1:
	bne	ta1, SEXP_INF, 2f		# is FT an infinity?
	bne	ta2, zero, unordered		# FT is a NAN
2:
	sll	t1, t1, 23			# reassemble exp & frac
	or	t1, t1, t2
	sll	ta1, ta1, 23			# reassemble exp & frac
	or	ta1, ta1, ta2
	beq	t0, zero, 1f			# is FS positive?
	negu	t1
1:
	beq	ta0, zero, 1f			# is FT positive?
	negu	ta1
1:
	li	v0, COND_LESS
	blt	t1, ta1, test_cond		# is FS < FT?
	li	v0, COND_EQUAL
	beq	t1, ta1, test_cond		# is FS == FT?
	move	v0, zero			# FS > FT
	b	test_cond

/*
 * Compare double.
 */
cmp_d:
	jal	get_cmp_d
	bne	t1, DEXP_INF, 1f		# is FS an infinity?
	bne	t2, zero, unordered
	bne	t3, zero, unordered		# FS is a NAN
1:
	bne	ta1, DEXP_INF, 2f		# is FT an infinity?
	bne	ta2, zero, unordered
	bne	ta3, zero, unordered		# FT is a NAN
2:
	sll	t1, t1, 20			# reassemble exp & frac
	or	t1, t1, t2
	sll	ta1, ta1, 20			# reassemble exp & frac
	or	ta1, ta1, ta2
	beq	t0, zero, 1f			# is FS positive?
	not	t3				# negate t1,t3
	not	t1
	addu	t3, t3, 1
	seq	v0, t3, zero			# compute carry
	addu	t1, t1, v0
1:
	beq	ta0, zero, 1f			# is FT positive?
	not	ta3				# negate ta1,ta3
	not	ta1
	addu	ta3, ta3, 1
	seq	v0, ta3, zero			# compute carry
	addu	ta1, ta1, v0
1:
	li	v0, COND_LESS
	blt	t1, ta1, test_cond		# is FS(MSW) < FT(MSW)?
	move	v0, zero
	bne	t1, ta1, test_cond		# is FS(MSW) > FT(MSW)?
	li	v0, COND_LESS
	bltu	t3, ta3, test_cond		# is FS(LSW) < FT(LSW)?
	li	v0, COND_EQUAL
	beq	t3, ta3, test_cond		# is FS(LSW) == FT(LSW)?
	move	v0, zero			# FS > FT
test_cond:
	and	v0, v0, a0			# condition match instruction?
set_cond:
	bne	v0, zero, 1f
	and	a1, a1, ~MIPS_FPU_COND_BIT	# clear condition bit
	b	2f
1:
	or	a1, a1, MIPS_FPU_COND_BIT	# set condition bit
2:
	ctc1	a1, MIPS_FPU_CSR		# save condition bit
	b	done

unordered:
	and	v0, a0, COND_UNORDERED		# this cmp match unordered?
	bne	v0, zero, 1f
	and	a1, a1, ~MIPS_FPU_COND_BIT	# clear condition bit
	b	2f
1:
	or	a1, a1, MIPS_FPU_COND_BIT	# set condition bit
2:
	and	v0, a0, COND_SIGNAL
	beq	v0, zero, 1f			# is this a signaling cmp?
	or	a1, a1, MIPS_FPU_EXCEPTION_INVALID | MIPS_FPU_STICKY_INVALID
	and	v0, a1, MIPS_FPU_ENABLE_INVALID
	bne	v0, zero, fpe_trap
1:
	ctc1	a1, MIPS_FPU_CSR		# save condition bit
	b	done

/*
 * Determine the amount to shift the fraction in order to restore the
 * normalized position. After that, round and handle exceptions.
 */
norm_s:
	move	v0, t2
	move	t9, zero			# t9 = num of leading zeros
	bne	t2, zero, 1f
	move	v0, t8
	addu	t9, 32
1:
	srl	v1, v0, 16
	bne	v1, zero, 1f
	addu	t9, 16
	sll	v0, 16
1:
	srl	v1, v0, 24
	bne	v1, zero, 1f
	addu	t9, 8
	sll	v0, 8
1:
	srl	v1, v0, 28
	bne	v1, zero, 1f
	addu	t9, 4
	sll	v0, 4
1:
	srl	v1, v0, 30
	bne	v1, zero, 1f
	addu	t9, 2
	sll	v0, 2
1:
	srl	v1, v0, 31
	bne	v1, zero, 1f
	addu	t9, 1
/*
 * Now shift t2,t8 the correct number of bits.
 */
1:
	subu	t9, t9, SLEAD_ZEROS		# dont count leading zeros
	subu	t1, t1, t9			# adjust the exponent
	beq	t9, zero, norm_noshift_s
	li	v1, 32
	blt	t9, zero, 1f			# if shift < 0, shift right
	subu	v1, v1, t9
	sll	t2, t2, t9			# shift t2,t8 left
	srl	v0, t8, v1			# save bits shifted out
	or	t2, t2, v0
	sll	t8, t8, t9
	b	norm_noshift_s
1:
	negu	t9				# shift t2,t8 right by t9
	subu	v1, v1, t9
	sll	v0, t8, v1			# save bits shifted out
	sltu	v0, zero, v0			# be sure to save any one bits
	srl	t8, t8, t9
	or	t8, t8, v0
	sll	v0, t2, v1			# save bits shifted out
	or	t8, t8, v0
	srl	t2, t2, t9
norm_noshift_s:
	move	ta1, t1				# save unrounded exponent
	move	ta2, t2				# save unrounded fraction
	and	v0, a1, MIPS_FPU_ROUNDING_BITS	# get rounding mode
	beq	v0, MIPS_FPU_ROUND_RN, 3f	# round to nearest
	beq	v0, MIPS_FPU_ROUND_RZ, 5f	# round to zero (truncate)
	beq	v0, MIPS_FPU_ROUND_RP, 1f	# round to +infinity
	beq	t0, zero, 5f			# if sign is positive, truncate
	b	2f
1:
	bne	t0, zero, 5f			# if sign is negative, truncate
2:
	beq	t8, zero, 5f			# if exact, continue
	addu	t2, t2, 1			# add rounding bit
	bne	t2, SIMPL_ONE<<1, 5f		# need to adjust exponent?
	addu	t1, t1, 1			# adjust exponent
	srl	t2, t2, 1			# renormalize fraction
	b	5f
3:
	li	v0, GUARDBIT			# load guard bit for rounding
	addu	v0, v0, t8			# add remainder
	sltu	v1, v0, t8			# compute carry out
	beq	v1, zero, 4f			# if no carry, continue
	addu	t2, t2, 1			# add carry to result
	bne	t2, SIMPL_ONE<<1, 4f		# need to adjust exponent?
	addu	t1, t1, 1			# adjust exponent
	srl	t2, t2, 1			# renormalize fraction
4:
	bne	v0, zero, 5f			# if rounded remainder is zero
	and	t2, t2, ~1			#  clear LSB (round to nearest)
5:
	bgt	t1, SEXP_MAX, overflow_s	# overflow?
	blt	t1, SEXP_MIN, underflow_s	# underflow?
	bne	t8, zero, inexact_s		# is result inexact?
	addu	t1, t1, SEXP_BIAS		# bias exponent
	and	t2, t2, ~SIMPL_ONE		# clear implied one bit
	b	result_fs_s

/*
 * Handle inexact exception.
 */
inexact_s:
	addu	t1, t1, SEXP_BIAS		# bias exponent
	and	t2, t2, ~SIMPL_ONE		# clear implied one bit
inexact_nobias_s:
	jal	set_fd_s			# save result
	or	a1, a1, MIPS_FPU_EXCEPTION_INEXACT | MIPS_FPU_STICKY_INEXACT
	and	v0, a1, MIPS_FPU_ENABLE_INEXACT
	bne	v0, zero, fpe_trap
	ctc1	a1, MIPS_FPU_CSR		# save exceptions
	b	done

/*
 * Overflow will trap (if enabled),
 * or generate an inexact trap (if enabled),
 * or generate an infinity.
 */
overflow_s:
	or	a1, a1, MIPS_FPU_EXCEPTION_OVERFLOW | MIPS_FPU_STICKY_OVERFLOW
	and	v0, a1, MIPS_FPU_ENABLE_OVERFLOW
	beq	v0, zero, 1f
	subu	t1, t1, 192			# bias exponent
	and	t2, t2, ~SIMPL_ONE		# clear implied one bit
	jal	set_fd_s			# save result
	b	fpe_trap
1:
	and	v0, a1, MIPS_FPU_ROUNDING_BITS	# get rounding mode
	beq	v0, MIPS_FPU_ROUND_RN, 3f	# round to nearest
	beq	v0, MIPS_FPU_ROUND_RZ, 1f	# round to zero (truncate)
	beq	v0, MIPS_FPU_ROUND_RP, 2f	# round to +infinity
	bne	t0, zero, 3f
1:
	li	t1, SEXP_MAX			# result is max finite
	li	t2, 0x007fffff
	b	inexact_s
2:
	bne	t0, zero, 1b
3:
	li	t1, SEXP_MAX + 1		# result is infinity
	move	t2, zero
	b	inexact_s

/*
 * In this implementation, "tininess" is detected "after rounding" and
 * "loss of accuracy" is detected as "an inexact result".
 */
underflow_s:
	and	v0, a1, MIPS_FPU_ENABLE_UNDERFLOW
	beq	v0, zero, 1f
/*
 * Underflow is enabled so compute the result and trap.
 */
	addu	t1, t1, 192			# bias exponent
	and	t2, t2, ~SIMPL_ONE		# clear implied one bit
	jal	set_fd_s			# save result
	or	a1, a1, MIPS_FPU_EXCEPTION_UNDERFLOW | MIPS_FPU_STICKY_UNDERFLOW
	b	fpe_trap
/*
 * Underflow is not enabled so compute the result,
 * signal inexact result (if it is) and trap (if enabled).
 */
1:
	move	t1, ta1				# get unrounded exponent
	move	t2, ta2				# get unrounded fraction
	li	t9, SEXP_MIN			# compute shift amount
	subu	t9, t9, t1			# shift t2,t8 right by t9
	blt	t9, SFRAC_BITS+2, 3f		# shift all the bits out?
	move	t1, zero			# result is inexact zero
	move	t2, zero
	or	a1, a1, MIPS_FPU_EXCEPTION_UNDERFLOW | MIPS_FPU_STICKY_UNDERFLOW
/*
 * Now round the zero result.
 * Only need to worry about rounding to +- infinity when the sign matches.
 */
	and	v0, a1, MIPS_FPU_ROUNDING_BITS	# get rounding mode
	beq	v0, MIPS_FPU_ROUND_RN, inexact_nobias_s # round to nearest
	beq	v0, MIPS_FPU_ROUND_RZ, inexact_nobias_s # round to zero
	beq	v0, MIPS_FPU_ROUND_RP, 1f		# round to +infinity
	beq	t0, zero, inexact_nobias_s	# if sign is positive, truncate
	b	2f
1:
	bne	t0, zero, inexact_nobias_s	# if sign is negative, truncate
2:
	addu	t2, t2, 1			# add rounding bit
	b	inexact_nobias_s
3:
	li	v1, 32
	subu	v1, v1, t9
	sltu	v0, zero, t8			# be sure to save any one bits
	sll	t8, t2, v1			# save bits shifted out
	or	t8, t8, v0			# include sticky bits
	srl	t2, t2, t9
/*
 * Now round the denormalized result.
 */
	and	v0, a1, MIPS_FPU_ROUNDING_BITS	# get rounding mode
	beq	v0, MIPS_FPU_ROUND_RN, 3f	# round to nearest
	beq	v0, MIPS_FPU_ROUND_RZ, 5f	# round to zero (truncate)
	beq	v0, MIPS_FPU_ROUND_RP, 1f	# round to +infinity
	beq	t0, zero, 5f			# if sign is positive, truncate
	b	2f
1:
	bne	t0, zero, 5f			# if sign is negative, truncate
2:
	beq	t8, zero, 5f			# if exact, continue
	addu	t2, t2, 1			# add rounding bit
	b	5f
3:
	li	v0, GUARDBIT			# load guard bit for rounding
	addu	v0, v0, t8			# add remainder
	sltu	v1, v0, t8			# compute carry out
	beq	v1, zero, 4f			# if no carry, continue
	addu	t2, t2, 1			# add carry to result
4:
	bne	v0, zero, 5f			# if rounded remainder is zero
	and	t2, t2, ~1			#  clear LSB (round to nearest)
5:
	move	t1, zero			# denorm or zero exponent
	jal	set_fd_s			# save result
	beq	t8, zero, done			# check for exact result
	or	a1, a1, MIPS_FPU_EXCEPTION_UNDERFLOW | MIPS_FPU_STICKY_UNDERFLOW
	or	a1, a1, MIPS_FPU_EXCEPTION_INEXACT | MIPS_FPU_STICKY_INEXACT
	and	v0, a1, MIPS_FPU_ENABLE_INEXACT
	bne	v0, zero, fpe_trap
	ctc1	a1, MIPS_FPU_CSR		# save exceptions
	b	done

/*
 * Determine the amount to shift the fraction in order to restore the
 * normalized position. After that, round and handle exceptions.
 */
norm_d:
	move	v0, t2
	move	t9, zero			# t9 = num of leading zeros
	bne	t2, zero, 1f
	move	v0, t3
	addu	t9, 32
	bne	t3, zero, 1f
	move	v0, t8
	addu	t9, 32
1:
	srl	v1, v0, 16
	bne	v1, zero, 1f
	addu	t9, 16
	sll	v0, 16
1:
	srl	v1, v0, 24
	bne	v1, zero, 1f
	addu	t9, 8
	sll	v0, 8
1:
	srl	v1, v0, 28
	bne	v1, zero, 1f
	addu	t9, 4
	sll	v0, 4
1:
	srl	v1, v0, 30
	bne	v1, zero, 1f
	addu	t9, 2
	sll	v0, 2
1:
	srl	v1, v0, 31
	bne	v1, zero, 1f
	addu	t9, 1
/*
 * Now shift t2,t3,t8 the correct number of bits.
 */
1:
	subu	t9, t9, DLEAD_ZEROS		# dont count leading zeros
	subu	t1, t1, t9			# adjust the exponent
	beq	t9, zero, norm_noshift_d
	li	v1, 32
	blt	t9, zero, 2f			# if shift < 0, shift right
	blt	t9, v1, 1f			# shift by < 32?
	subu	t9, t9, v1			# shift by >= 32
	subu	v1, v1, t9
	sll	t2, t3, t9			# shift left by t9
	srl	v0, t8, v1			# save bits shifted out
	or	t2, t2, v0
	sll	t3, t8, t9
	move	t8, zero
	b	norm_noshift_d
1:
	subu	v1, v1, t9
	sll	t2, t2, t9			# shift left by t9
	srl	v0, t3, v1			# save bits shifted out
	or	t2, t2, v0
	sll	t3, t3, t9
	srl	v0, t8, v1			# save bits shifted out
	or	t3, t3, v0
	sll	t8, t8, t9
	b	norm_noshift_d
2:
	negu	t9				# shift right by t9
	subu	v1, v1, t9			#  (known to be < 32 bits)
	sll	v0, t8, v1			# save bits shifted out
	sltu	v0, zero, v0			# be sure to save any one bits
	srl	t8, t8, t9
	or	t8, t8, v0
	sll	v0, t3, v1			# save bits shifted out
	or	t8, t8, v0
	srl	t3, t3, t9
	sll	v0, t2, v1			# save bits shifted out
	or	t3, t3, v0
	srl	t2, t2, t9
norm_noshift_d:
	move	ta1, t1				# save unrounded exponent
	move	ta2, t2				# save unrounded fraction (MS)
	move	ta3, t3				# save unrounded fraction (LS)
	and	v0, a1, MIPS_FPU_ROUNDING_BITS	# get rounding mode
	beq	v0, MIPS_FPU_ROUND_RN, 3f	# round to nearest
	beq	v0, MIPS_FPU_ROUND_RZ, 5f	# round to zero (truncate)
	beq	v0, MIPS_FPU_ROUND_RP, 1f	# round to +infinity
	beq	t0, zero, 5f			# if sign is positive, truncate
	b	2f
1:
	bne	t0, zero, 5f			# if sign is negative, truncate
2:
	beq	t8, zero, 5f			# if exact, continue
	addu	t3, t3, 1			# add rounding bit
	bne	t3, zero, 5f			# branch if no carry
	addu	t2, t2, 1			# add carry
	bne	t2, DIMPL_ONE<<1, 5f		# need to adjust exponent?
	addu	t1, t1, 1			# adjust exponent
	srl	t2, t2, 1			# renormalize fraction
	b	5f
3:
	li	v0, GUARDBIT			# load guard bit for rounding
	addu	v0, v0, t8			# add remainder
	sltu	v1, v0, t8			# compute carry out
	beq	v1, zero, 4f			# branch if no carry
	addu	t3, t3, 1			# add carry
	bne	t3, zero, 4f			# branch if no carry
	addu	t2, t2, 1			# add carry to result
	bne	t2, DIMPL_ONE<<1, 4f		# need to adjust exponent?
	addu	t1, t1, 1			# adjust exponent
	srl	t2, t2, 1			# renormalize fraction
4:
	bne	v0, zero, 5f			# if rounded remainder is zero
	and	t3, t3, ~1			#  clear LSB (round to nearest)
5:
	bgt	t1, DEXP_MAX, overflow_d	# overflow?
	blt	t1, DEXP_MIN, underflow_d	# underflow?
	bne	t8, zero, inexact_d		# is result inexact?
	addu	t1, t1, DEXP_BIAS		# bias exponent
	and	t2, t2, ~DIMPL_ONE		# clear implied one bit
	b	result_fs_d

/*
 * Handle inexact exception.
 */
inexact_d:
	addu	t1, t1, DEXP_BIAS		# bias exponent
	and	t2, t2, ~DIMPL_ONE		# clear implied one bit
inexact_nobias_d:
	jal	set_fd_d			# save result
	or	a1, a1, MIPS_FPU_EXCEPTION_INEXACT | MIPS_FPU_STICKY_INEXACT
	and	v0, a1, MIPS_FPU_ENABLE_INEXACT
	bne	v0, zero, fpe_trap
	ctc1	a1, MIPS_FPU_CSR		# save exceptions
	b	done

/*
 * Overflow will trap (if enabled),
 * or generate an inexact trap (if enabled),
 * or generate an infinity.
 */
overflow_d:
	or	a1, a1, MIPS_FPU_EXCEPTION_OVERFLOW | MIPS_FPU_STICKY_OVERFLOW
	and	v0, a1, MIPS_FPU_ENABLE_OVERFLOW
	beq	v0, zero, 1f
	subu	t1, t1, 1536			# bias exponent
	and	t2, t2, ~DIMPL_ONE		# clear implied one bit
	jal	set_fd_d			# save result
	b	fpe_trap
1:
	and	v0, a1, MIPS_FPU_ROUNDING_BITS	# get rounding mode
	beq	v0, MIPS_FPU_ROUND_RN, 3f	# round to nearest
	beq	v0, MIPS_FPU_ROUND_RZ, 1f	# round to zero (truncate)
	beq	v0, MIPS_FPU_ROUND_RP, 2f	# round to +infinity
	bne	t0, zero, 3f
1:
	li	t1, DEXP_MAX			# result is max finite
	li	t2, 0x000fffff
	li	t3, 0xffffffff
	b	inexact_d
2:
	bne	t0, zero, 1b
3:
	li	t1, DEXP_MAX + 1		# result is infinity
	move	t2, zero
	move	t3, zero
	b	inexact_d

/*
 * In this implementation, "tininess" is detected "after rounding" and
 * "loss of accuracy" is detected as "an inexact result".
 */
underflow_d:
	and	v0, a1, MIPS_FPU_ENABLE_UNDERFLOW
	beq	v0, zero, 1f
/*
 * Underflow is enabled so compute the result and trap.
 */
	addu	t1, t1, 1536			# bias exponent
	and	t2, t2, ~DIMPL_ONE		# clear implied one bit
	jal	set_fd_d			# save result
	or	a1, a1, MIPS_FPU_EXCEPTION_UNDERFLOW | MIPS_FPU_STICKY_UNDERFLOW
	b	fpe_trap
/*
 * Underflow is not enabled so compute the result,
 * signal inexact result (if it is) and trap (if enabled).
 */
1:
	move	t1, ta1				# get unrounded exponent
	move	t2, ta2				# get unrounded fraction (MS)
	move	t3, ta3				# get unrounded fraction (LS)
	li	t9, DEXP_MIN			# compute shift amount
	subu	t9, t9, t1			# shift t2,t8 right by t9
	blt	t9, DFRAC_BITS+2, 3f		# shift all the bits out?
	move	t1, zero			# result is inexact zero
	move	t2, zero
	move	t3, zero
	or	a1, a1, MIPS_FPU_EXCEPTION_UNDERFLOW | MIPS_FPU_STICKY_UNDERFLOW
/*
 * Now round the zero result.
 * Only need to worry about rounding to +- infinity when the sign matches.
 */
	and	v0, a1, MIPS_FPU_ROUNDING_BITS	# get rounding mode
	beq	v0, MIPS_FPU_ROUND_RN, inexact_nobias_d  # round to nearest
	beq	v0, MIPS_FPU_ROUND_RZ, inexact_nobias_d  # round to zero
	beq	v0, MIPS_FPU_ROUND_RP, 1f		# round to +infinity
	beq	t0, zero, inexact_nobias_d	# if sign is positive, truncate
	b	2f
1:
	bne	t0, zero, inexact_nobias_d	# if sign is negative, truncate
2:
	addu	t3, t3, 1			# add rounding bit
	b	inexact_nobias_d
3:
	li	v1, 32
	blt	t9, v1, 1f			# shift by < 32?
	subu	t9, t9, v1			# shift right by >= 32
	subu	v1, v1, t9
	sltu	v0, zero, t8			# be sure to save any one bits
	sll	t8, t2, v1			# save bits shifted out
	or	t8, t8, v0			# include sticky bits
	srl	t3, t2, t9
	move	t2, zero
	b	2f
1:
	subu	v1, v1, t9			# shift right by t9
	sltu	v0, zero, t8			# be sure to save any one bits
	sll	t8, t3, v1			# save bits shifted out
	or	t8, t8, v0			# include sticky bits
	srl	t3, t3, t9
	sll	v0, t2, v1			# save bits shifted out
	or	t3, t3, v0
	srl	t2, t2, t9
/*
 * Now round the denormalized result.
 */
2:
	and	v0, a1, MIPS_FPU_ROUNDING_BITS	# get rounding mode
	beq	v0, MIPS_FPU_ROUND_RN, 3f	# round to nearest
	beq	v0, MIPS_FPU_ROUND_RZ, 5f	# round to zero (truncate)
	beq	v0, MIPS_FPU_ROUND_RP, 1f	# round to +infinity
	beq	t0, zero, 5f			# if sign is positive, truncate
	b	2f
1:
	bne	t0, zero, 5f			# if sign is negative, truncate
2:
	beq	t8, zero, 5f			# if exact, continue
	addu	t3, t3, 1			# add rounding bit
	bne	t3, zero, 5f			# if no carry, continue
	addu	t2, t2, 1			# add carry
	b	5f
3:
	li	v0, GUARDBIT			# load guard bit for rounding
	addu	v0, v0, t8			# add remainder
	sltu	v1, v0, t8			# compute carry out
	beq	v1, zero, 4f			# if no carry, continue
	addu	t3, t3, 1			# add rounding bit
	bne	t3, zero, 4f			# if no carry, continue
	addu	t2, t2, 1			# add carry
4:
	bne	v0, zero, 5f			# if rounded remainder is zero
	and	t3, t3, ~1			#  clear LSB (round to nearest)
5:
	move	t1, zero			# denorm or zero exponent
	jal	set_fd_d			# save result
	beq	t8, zero, done			# check for exact result
	or	a1, a1, MIPS_FPU_EXCEPTION_UNDERFLOW | MIPS_FPU_STICKY_UNDERFLOW
	or	a1, a1, MIPS_FPU_EXCEPTION_INEXACT | MIPS_FPU_STICKY_INEXACT
	and	v0, a1, MIPS_FPU_ENABLE_INEXACT
	bne	v0, zero, fpe_trap
	ctc1	a1, MIPS_FPU_CSR		# save exceptions
	b	done

/*
 * Signal an invalid operation if the trap is enabled; otherwise,
 * the result is a quiet NAN.
 */
invalid_s:					# trap invalid operation
	or	a1, a1, MIPS_FPU_EXCEPTION_INVALID | MIPS_FPU_STICKY_INVALID
	and	v0, a1, MIPS_FPU_ENABLE_INVALID
	bne	v0, zero, fpe_trap
	ctc1	a1, MIPS_FPU_CSR		# save exceptions
	move	t0, zero			# result is a quiet NAN
	li	t1, SEXP_INF
	li	t2, SQUIET_NAN
	jal	set_fd_s			# save result (in t0,t1,t2)
	b	done

/*
 * Signal an invalid operation if the trap is enabled; otherwise,
 * the result is a quiet NAN.
 */
invalid_d:					# trap invalid operation
	or	a1, a1, MIPS_FPU_EXCEPTION_INVALID | MIPS_FPU_STICKY_INVALID
	and	v0, a1, MIPS_FPU_ENABLE_INVALID
	bne	v0, zero, fpe_trap
	ctc1	a1, MIPS_FPU_CSR		# save exceptions
	move	t0, zero			# result is a quiet NAN
	li	t1, DEXP_INF
	li	t2, DQUIET_NAN0
	li	t3, DQUIET_NAN1
	jal	set_fd_d			# save result (in t0,t1,t2,t3)
	b	done

/*
 * Signal an invalid operation if the trap is enabled; otherwise,
 * the result is INT_MAX or INT_MIN.
 */
invalid_w:					# trap invalid operation
	or	a1, a1, MIPS_FPU_EXCEPTION_INVALID | MIPS_FPU_STICKY_INVALID
	and	v0, a1, MIPS_FPU_ENABLE_INVALID
	bne	v0, zero, fpe_trap
	ctc1	a1, MIPS_FPU_CSR		# save exceptions
	bne	t0, zero, 1f
	li	t2, INT_MAX			# result is INT_MAX
	b	result_fs_w
1:
	li	t2, INT_MIN			# result is INT_MIN
	b	result_fs_w

/*
 * Trap if the hardware should have handled this case.
 */
fpe_trap:
	move	a2, a1				# code = FP CSR
	ctc1	a1, MIPS_FPU_CSR		# save exceptions
	break	0

/*
 * Send an illegal instruction signal to the current process.
 */
ill:
	ctc1	a1, MIPS_FPU_CSR		# save exceptions
	move	a2, a0				# code = FP instruction
	break	0

result_ft_s:
	move	t0, ta0				# result is FT
	move	t1, ta1
	move	t2, ta2
result_fs_s:					# result is FS
	jal	set_fd_s			# save result (in t0,t1,t2)
	b	done

result_fs_w:
	jal	set_fd_word			# save result (in t2)
	b	done

result_ft_d:
	move	t0, ta0				# result is FT
	move	t1, ta1
	move	t2, ta2
	move	t3, ta3
result_fs_d:					# result is FS
	jal	set_fd_d			# save result (in t0,t1,t2,t3)

done:
	lw	ra, CALLFRAME_RA(sp)
	addu	sp, sp, CALLFRAME_SIZ
	j	ra
END(MipsEmulateFP)

/*----------------------------------------------------------------------------
 * get_fs_int --
 *
 *	Read (integer) the FS register (bits 15-11).
 *	This is an internal routine used by MipsEmulateFP only.
 *
 * Results:
 *	t0	contains the sign
 *	t2	contains the fraction
 *
 *----------------------------------------------------------------------------
 */
LEAF(get_fs_int)
	srl	a3, a0, 12 - 2			# get FS field (even regs only)
	and	a3, a3, 0xF << 2		# mask FS field
	lw	a3, get_fs_int_tbl(a3)		# switch on register number
	j	a3

	.rdata
get_fs_int_tbl:
	.word	get_fs_int_f0
	.word	get_fs_int_f2
	.word	get_fs_int_f4
	.word	get_fs_int_f6
	.word	get_fs_int_f8
	.word	get_fs_int_f10
	.word	get_fs_int_f12
	.word	get_fs_int_f14
	.word	get_fs_int_f16
	.word	get_fs_int_f18
	.word	get_fs_int_f20
	.word	get_fs_int_f22
	.word	get_fs_int_f24
	.word	get_fs_int_f26
	.word	get_fs_int_f28
	.word	get_fs_int_f30
	.text

get_fs_int_f0:
	mfc1	t2, $f0
	b	get_fs_int_done
get_fs_int_f2:
	mfc1	t2, $f2
	b	get_fs_int_done
get_fs_int_f4:
	mfc1	t2, $f4
	b	get_fs_int_done
get_fs_int_f6:
	mfc1	t2, $f6
	b	get_fs_int_done
get_fs_int_f8:
	mfc1	t2, $f8
	b	get_fs_int_done
get_fs_int_f10:
	mfc1	t2, $f10
	b	get_fs_int_done
get_fs_int_f12:
	mfc1	t2, $f12
	b	get_fs_int_done
get_fs_int_f14:
	mfc1	t2, $f14
	b	get_fs_int_done
get_fs_int_f16:
	mfc1	t2, $f16
	b	get_fs_int_done
get_fs_int_f18:
	mfc1	t2, $f18
	b	get_fs_int_done
get_fs_int_f20:
	mfc1	t2, $f20
	b	get_fs_int_done
get_fs_int_f22:
	mfc1	t2, $f22
	b	get_fs_int_done
get_fs_int_f24:
	mfc1	t2, $f24
	b	get_fs_int_done
get_fs_int_f26:
	mfc1	t2, $f26
	b	get_fs_int_done
get_fs_int_f28:
	mfc1	t2, $f28
	b	get_fs_int_done
get_fs_int_f30:
	mfc1	t2, $f30
get_fs_int_done:
	srl	t0, t2, 31			# init the sign bit
	bge	t2, zero, 1f
	negu	t2
1:
	j	ra
END(get_fs_int)

/*----------------------------------------------------------------------------
 * get_ft_fs_s --
 *
 *	Read (single precision) the FT register (bits 20-16) and
 *	the FS register (bits 15-11) and break up into fields.
 *	This is an internal routine used by MipsEmulateFP only.
 *
 * Results:
 *	t0	contains the FS sign
 *	t1	contains the FS (biased) exponent
 *	t2	contains the FS fraction
 *	ta0	contains the FT sign
 *	ta1	contains the FT (biased) exponent
 *	ta2	contains the FT fraction
 *
 *----------------------------------------------------------------------------
 */
LEAF(get_ft_fs_s)
	srl	a3, a0, 17 - 2			# get FT field (even regs only)
	and	a3, a3, 0xF << 2		# mask FT field
	lw	a3, get_ft_s_tbl(a3)		# switch on register number
	j	a3

	.rdata
get_ft_s_tbl:
	.word	get_ft_s_f0
	.word	get_ft_s_f2
	.word	get_ft_s_f4
	.word	get_ft_s_f6
	.word	get_ft_s_f8
	.word	get_ft_s_f10
	.word	get_ft_s_f12
	.word	get_ft_s_f14
	.word	get_ft_s_f16
	.word	get_ft_s_f18
	.word	get_ft_s_f20
	.word	get_ft_s_f22
	.word	get_ft_s_f24
	.word	get_ft_s_f26
	.word	get_ft_s_f28
	.word	get_ft_s_f30
	.text

get_ft_s_f0:
	mfc1	ta0, $f0
	b	get_ft_s_done
get_ft_s_f2:
	mfc1	ta0, $f2
	b	get_ft_s_done
get_ft_s_f4:
	mfc1	ta0, $f4
	b	get_ft_s_done
get_ft_s_f6:
	mfc1	ta0, $f6
	b	get_ft_s_done
get_ft_s_f8:
	mfc1	ta0, $f8
	b	get_ft_s_done
get_ft_s_f10:
	mfc1	ta0, $f10
	b	get_ft_s_done
get_ft_s_f12:
	mfc1	ta0, $f12
	b	get_ft_s_done
get_ft_s_f14:
	mfc1	ta0, $f14
	b	get_ft_s_done
get_ft_s_f16:
	mfc1	ta0, $f16
	b	get_ft_s_done
get_ft_s_f18:
	mfc1	ta0, $f18
	b	get_ft_s_done
get_ft_s_f20:
	mfc1	ta0, $f20
	b	get_ft_s_done
get_ft_s_f22:
	mfc1	ta0, $f22
	b	get_ft_s_done
get_ft_s_f24:
	mfc1	ta0, $f24
	b	get_ft_s_done
get_ft_s_f26:
	mfc1	ta0, $f26
	b	get_ft_s_done
get_ft_s_f28:
	mfc1	ta0, $f28
	b	get_ft_s_done
get_ft_s_f30:
	mfc1	ta0, $f30
get_ft_s_done:
	srl	ta1, ta0, 23			# get exponent
	and	ta1, ta1, 0xFF
	and	ta2, ta0, 0x7FFFFF		# get fraction
	srl	ta0, ta0, 31			# get sign
	bne	ta1, SEXP_INF, 1f		# is it a signaling NAN?
	and	v0, ta2, SSIGNAL_NAN
	bne	v0, zero, invalid_s
1:
	/* fall through to get FS */

/*----------------------------------------------------------------------------
 * get_fs_s --
 *
 *	Read (single precision) the FS register (bits 15-11) and
 *	break up into fields.
 *	This is an internal routine used by MipsEmulateFP only.
 *
 * Results:
 *	t0	contains the sign
 *	t1	contains the (biased) exponent
 *	t2	contains the fraction
 *
 *----------------------------------------------------------------------------
 */
XLEAF(get_fs_s)
	srl	a3, a0, 12 - 2			# get FS field (even regs only)
	and	a3, a3, 0xF << 2		# mask FS field
	lw	a3, get_fs_s_tbl(a3)		# switch on register number
	j	a3

	.rdata
get_fs_s_tbl:
	.word	get_fs_s_f0
	.word	get_fs_s_f2
	.word	get_fs_s_f4
	.word	get_fs_s_f6
	.word	get_fs_s_f8
	.word	get_fs_s_f10
	.word	get_fs_s_f12
	.word	get_fs_s_f14
	.word	get_fs_s_f16
	.word	get_fs_s_f18
	.word	get_fs_s_f20
	.word	get_fs_s_f22
	.word	get_fs_s_f24
	.word	get_fs_s_f26
	.word	get_fs_s_f28
	.word	get_fs_s_f30
	.text

get_fs_s_f0:
	mfc1	t0, $f0
	b	get_fs_s_done
get_fs_s_f2:
	mfc1	t0, $f2
	b	get_fs_s_done
get_fs_s_f4:
	mfc1	t0, $f4
	b	get_fs_s_done
get_fs_s_f6:
	mfc1	t0, $f6
	b	get_fs_s_done
get_fs_s_f8:
	mfc1	t0, $f8
	b	get_fs_s_done
get_fs_s_f10:
	mfc1	t0, $f10
	b	get_fs_s_done
get_fs_s_f12:
	mfc1	t0, $f12
	b	get_fs_s_done
get_fs_s_f14:
	mfc1	t0, $f14
	b	get_fs_s_done
get_fs_s_f16:
	mfc1	t0, $f16
	b	get_fs_s_done
get_fs_s_f18:
	mfc1	t0, $f18
	b	get_fs_s_done
get_fs_s_f20:
	mfc1	t0, $f20
	b	get_fs_s_done
get_fs_s_f22:
	mfc1	t0, $f22
	b	get_fs_s_done
get_fs_s_f24:
	mfc1	t0, $f24
	b	get_fs_s_done
get_fs_s_f26:
	mfc1	t0, $f26
	b	get_fs_s_done
get_fs_s_f28:
	mfc1	t0, $f28
	b	get_fs_s_done
get_fs_s_f30:
	mfc1	t0, $f30
get_fs_s_done:
	srl	t1, t0, 23			# get exponent
	and	t1, t1, 0xFF
	and	t2, t0, 0x7FFFFF		# get fraction
	srl	t0, t0, 31			# get sign
	bne	t1, SEXP_INF, 1f		# is it a signaling NAN?
	and	v0, t2, SSIGNAL_NAN
	bne	v0, zero, invalid_s
1:
	j	ra
END(get_ft_fs_s)

/*----------------------------------------------------------------------------
 * get_ft_fs_d --
 *
 *	Read (double precision) the FT register (bits 20-16) and
 *	the FS register (bits 15-11) and break up into fields.
 *	This is an internal routine used by MipsEmulateFP only.
 *
 * Results:
 *	t0	contains the FS sign
 *	t1	contains the FS (biased) exponent
 *	t2	contains the FS fraction
 *	t3	contains the FS remaining fraction
 *	ta0	contains the FT sign
 *	ta1	contains the FT (biased) exponent
 *	ta2	contains the FT fraction
 *	ta3	contains the FT remaining fraction
 *
 *----------------------------------------------------------------------------
 */
LEAF(get_ft_fs_d)
	srl	a3, a0, 17 - 2			# get FT field (even regs only)
	and	a3, a3, 0xF << 2		# mask FT field
	lw	a3, get_ft_d_tbl(a3)		# switch on register number
	j	a3

	.rdata
get_ft_d_tbl:
	.word	get_ft_d_f0
	.word	get_ft_d_f2
	.word	get_ft_d_f4
	.word	get_ft_d_f6
	.word	get_ft_d_f8
	.word	get_ft_d_f10
	.word	get_ft_d_f12
	.word	get_ft_d_f14
	.word	get_ft_d_f16
	.word	get_ft_d_f18
	.word	get_ft_d_f20
	.word	get_ft_d_f22
	.word	get_ft_d_f24
	.word	get_ft_d_f26
	.word	get_ft_d_f28
	.word	get_ft_d_f30
	.text

get_ft_d_f0:
	mfc1	ta3, $f0
	mfc1	ta0, $f1
	b	get_ft_d_done
get_ft_d_f2:
	mfc1	ta3, $f2
	mfc1	ta0, $f3
	b	get_ft_d_done
get_ft_d_f4:
	mfc1	ta3, $f4
	mfc1	ta0, $f5
	b	get_ft_d_done
get_ft_d_f6:
	mfc1	ta3, $f6
	mfc1	ta0, $f7
	b	get_ft_d_done
get_ft_d_f8:
	mfc1	ta3, $f8
	mfc1	ta0, $f9
	b	get_ft_d_done
get_ft_d_f10:
	mfc1	ta3, $f10
	mfc1	ta0, $f11
	b	get_ft_d_done
get_ft_d_f12:
	mfc1	ta3, $f12
	mfc1	ta0, $f13
	b	get_ft_d_done
get_ft_d_f14:
	mfc1	ta3, $f14
	mfc1	ta0, $f15
	b	get_ft_d_done
get_ft_d_f16:
	mfc1	ta3, $f16
	mfc1	ta0, $f17
	b	get_ft_d_done
get_ft_d_f18:
	mfc1	ta3, $f18
	mfc1	ta0, $f19
	b	get_ft_d_done
get_ft_d_f20:
	mfc1	ta3, $f20
	mfc1	ta0, $f21
	b	get_ft_d_done
get_ft_d_f22:
	mfc1	ta3, $f22
	mfc1	ta0, $f23
	b	get_ft_d_done
get_ft_d_f24:
	mfc1	ta3, $f24
	mfc1	ta0, $f25
	b	get_ft_d_done
get_ft_d_f26:
	mfc1	ta3, $f26
	mfc1	ta0, $f27
	b	get_ft_d_done
get_ft_d_f28:
	mfc1	ta3, $f28
	mfc1	ta0, $f29
	b	get_ft_d_done
get_ft_d_f30:
	mfc1	ta3, $f30
	mfc1	ta0, $f31
get_ft_d_done:
	srl	ta1, ta0, 20			# get exponent
	and	ta1, ta1, 0x7FF
	and	ta2, ta0, 0xFFFFF		# get fraction
	srl	ta0, ta0, 31			# get sign
	bne	ta1, DEXP_INF, 1f		# is it a signaling NAN?
	and	v0, ta2, DSIGNAL_NAN
	bne	v0, zero, invalid_d
1:
	/* fall through to get FS */

/*----------------------------------------------------------------------------
 * get_fs_d --
 *
 *	Read (double precision) the FS register (bits 15-11) and
 *	break up into fields.
 *	This is an internal routine used by MipsEmulateFP only.
 *
 * Results:
 *	t0	contains the sign
 *	t1	contains the (biased) exponent
 *	t2	contains the fraction
 *	t3	contains the remaining fraction
 *
 *----------------------------------------------------------------------------
 */
XLEAF(get_fs_d)
	srl	a3, a0, 12 - 2			# get FS field (even regs only)
	and	a3, a3, 0xF << 2		# mask FS field
	lw	a3, get_fs_d_tbl(a3)		# switch on register number
	j	a3

	.rdata
get_fs_d_tbl:
	.word	get_fs_d_f0
	.word	get_fs_d_f2
	.word	get_fs_d_f4
	.word	get_fs_d_f6
	.word	get_fs_d_f8
	.word	get_fs_d_f10
	.word	get_fs_d_f12
	.word	get_fs_d_f14
	.word	get_fs_d_f16
	.word	get_fs_d_f18
	.word	get_fs_d_f20
	.word	get_fs_d_f22
	.word	get_fs_d_f24
	.word	get_fs_d_f26
	.word	get_fs_d_f28
	.word	get_fs_d_f30
	.text

get_fs_d_f0:
	mfc1	t3, $f0
	mfc1	t0, $f1
	b	get_fs_d_done
get_fs_d_f2:
	mfc1	t3, $f2
	mfc1	t0, $f3
	b	get_fs_d_done
get_fs_d_f4:
	mfc1	t3, $f4
	mfc1	t0, $f5
	b	get_fs_d_done
get_fs_d_f6:
	mfc1	t3, $f6
	mfc1	t0, $f7
	b	get_fs_d_done
get_fs_d_f8:
	mfc1	t3, $f8
	mfc1	t0, $f9
	b	get_fs_d_done
get_fs_d_f10:
	mfc1	t3, $f10
	mfc1	t0, $f11
	b	get_fs_d_done
get_fs_d_f12:
	mfc1	t3, $f12
	mfc1	t0, $f13
	b	get_fs_d_done
get_fs_d_f14:
	mfc1	t3, $f14
	mfc1	t0, $f15
	b	get_fs_d_done
get_fs_d_f16:
	mfc1	t3, $f16
	mfc1	t0, $f17
	b	get_fs_d_done
get_fs_d_f18:
	mfc1	t3, $f18
	mfc1	t0, $f19
	b	get_fs_d_done
get_fs_d_f20:
	mfc1	t3, $f20
	mfc1	t0, $f21
	b	get_fs_d_done
get_fs_d_f22:
	mfc1	t3, $f22
	mfc1	t0, $f23
	b	get_fs_d_done
get_fs_d_f24:
	mfc1	t3, $f24
	mfc1	t0, $f25
	b	get_fs_d_done
get_fs_d_f26:
	mfc1	t3, $f26
	mfc1	t0, $f27
	b	get_fs_d_done
get_fs_d_f28:
	mfc1	t3, $f28
	mfc1	t0, $f29
	b	get_fs_d_done
get_fs_d_f30:
	mfc1	t3, $f30
	mfc1	t0, $f31
get_fs_d_done:
	srl	t1, t0, 20			# get exponent
	and	t1, t1, 0x7FF
	and	t2, t0, 0xFFFFF			# get fraction
	srl	t0, t0, 31			# get sign
	bne	t1, DEXP_INF, 1f		# is it a signaling NAN?
	and	v0, t2, DSIGNAL_NAN
	bne	v0, zero, invalid_d
1:
	j	ra
END(get_ft_fs_d)

/*----------------------------------------------------------------------------
 * get_cmp_s --
 *
 *	Read (single precision) the FS register (bits 15-11) and
 *	the FT register (bits 20-16) and break up into fields.
 *	This is an internal routine used by MipsEmulateFP only.
 *
 * Results:
 *	t0	contains the sign
 *	t1	contains the (biased) exponent
 *	t2	contains the fraction
 *	ta0	contains the sign
 *	ta1	contains the (biased) exponent
 *	ta2	contains the fraction
 *
 *----------------------------------------------------------------------------
 */
LEAF(get_cmp_s)
	srl	a3, a0, 12 - 2			# get FS field (even regs only)
	and	a3, a3, 0xF << 2		# mask FS field
	lw	a3, cmp_fs_s_tbl(a3)		# switch on register number
	j	a3

	.rdata
cmp_fs_s_tbl:
	.word	cmp_fs_s_f0
	.word	cmp_fs_s_f2
	.word	cmp_fs_s_f4
	.word	cmp_fs_s_f6
	.word	cmp_fs_s_f8
	.word	cmp_fs_s_f10
	.word	cmp_fs_s_f12
	.word	cmp_fs_s_f14
	.word	cmp_fs_s_f16
	.word	cmp_fs_s_f18
	.word	cmp_fs_s_f20
	.word	cmp_fs_s_f22
	.word	cmp_fs_s_f24
	.word	cmp_fs_s_f26
	.word	cmp_fs_s_f28
	.word	cmp_fs_s_f30
	.text

cmp_fs_s_f0:
	mfc1	t0, $f0
	b	cmp_fs_s_done
cmp_fs_s_f2:
	mfc1	t0, $f2
	b	cmp_fs_s_done
cmp_fs_s_f4:
	mfc1	t0, $f4
	b	cmp_fs_s_done
cmp_fs_s_f6:
	mfc1	t0, $f6
	b	cmp_fs_s_done
cmp_fs_s_f8:
	mfc1	t0, $f8
	b	cmp_fs_s_done
cmp_fs_s_f10:
	mfc1	t0, $f10
	b	cmp_fs_s_done
cmp_fs_s_f12:
	mfc1	t0, $f12
	b	cmp_fs_s_done
cmp_fs_s_f14:
	mfc1	t0, $f14
	b	cmp_fs_s_done
cmp_fs_s_f16:
	mfc1	t0, $f16
	b	cmp_fs_s_done
cmp_fs_s_f18:
	mfc1	t0, $f18
	b	cmp_fs_s_done
cmp_fs_s_f20:
	mfc1	t0, $f20
	b	cmp_fs_s_done
cmp_fs_s_f22:
	mfc1	t0, $f22
	b	cmp_fs_s_done
cmp_fs_s_f24:
	mfc1	t0, $f24
	b	cmp_fs_s_done
cmp_fs_s_f26:
	mfc1	t0, $f26
	b	cmp_fs_s_done
cmp_fs_s_f28:
	mfc1	t0, $f28
	b	cmp_fs_s_done
cmp_fs_s_f30:
	mfc1	t0, $f30
cmp_fs_s_done:
	srl	t1, t0, 23			# get exponent
	and	t1, t1, 0xFF
	and	t2, t0, 0x7FFFFF		# get fraction
	srl	t0, t0, 31			# get sign

	srl	a3, a0, 17 - 2			# get FT field (even regs only)
	and	a3, a3, 0xF << 2		# mask FT field
	lw	a3, cmp_ft_s_tbl(a3)		# switch on register number
	j	a3

	.rdata
cmp_ft_s_tbl:
	.word	cmp_ft_s_f0
	.word	cmp_ft_s_f2
	.word	cmp_ft_s_f4
	.word	cmp_ft_s_f6
	.word	cmp_ft_s_f8
	.word	cmp_ft_s_f10
	.word	cmp_ft_s_f12
	.word	cmp_ft_s_f14
	.word	cmp_ft_s_f16
	.word	cmp_ft_s_f18
	.word	cmp_ft_s_f20
	.word	cmp_ft_s_f22
	.word	cmp_ft_s_f24
	.word	cmp_ft_s_f26
	.word	cmp_ft_s_f28
	.word	cmp_ft_s_f30
	.text

cmp_ft_s_f0:
	mfc1	ta0, $f0
	b	cmp_ft_s_done
cmp_ft_s_f2:
	mfc1	ta0, $f2
	b	cmp_ft_s_done
cmp_ft_s_f4:
	mfc1	ta0, $f4
	b	cmp_ft_s_done
cmp_ft_s_f6:
	mfc1	ta0, $f6
	b	cmp_ft_s_done
cmp_ft_s_f8:
	mfc1	ta0, $f8
	b	cmp_ft_s_done
cmp_ft_s_f10:
	mfc1	ta0, $f10
	b	cmp_ft_s_done
cmp_ft_s_f12:
	mfc1	ta0, $f12
	b	cmp_ft_s_done
cmp_ft_s_f14:
	mfc1	ta0, $f14
	b	cmp_ft_s_done
cmp_ft_s_f16:
	mfc1	ta0, $f16
	b	cmp_ft_s_done
cmp_ft_s_f18:
	mfc1	ta0, $f18
	b	cmp_ft_s_done
cmp_ft_s_f20:
	mfc1	ta0, $f20
	b	cmp_ft_s_done
cmp_ft_s_f22:
	mfc1	ta0, $f22
	b	cmp_ft_s_done
cmp_ft_s_f24:
	mfc1	ta0, $f24
	b	cmp_ft_s_done
cmp_ft_s_f26:
	mfc1	ta0, $f26
	b	cmp_ft_s_done
cmp_ft_s_f28:
	mfc1	ta0, $f28
	b	cmp_ft_s_done
cmp_ft_s_f30:
	mfc1	ta0, $f30
cmp_ft_s_done:
	srl	ta1, ta0, 23			# get exponent
	and	ta1, ta1, 0xFF
	and	ta2, ta0, 0x7FFFFF		# get fraction
	srl	ta0, ta0, 31			# get sign
	j	ra
END(get_cmp_s)

/*----------------------------------------------------------------------------
 * get_cmp_d --
 *
 *	Read (double precision) the FS register (bits 15-11) and
 *	the FT register (bits 20-16) and break up into fields.
 *	This is an internal routine used by MipsEmulateFP only.
 *
 * Results:
 *	t0	contains the sign
 *	t1	contains the (biased) exponent
 *	t2	contains the fraction
 *	t3	contains the remaining fraction
 *	ta0	contains the sign
 *	ta1	contains the (biased) exponent
 *	ta2	contains the fraction
 *	ta3	contains the remaining fraction
 *
 *----------------------------------------------------------------------------
 */
LEAF(get_cmp_d)
	srl	a3, a0, 12 - 2			# get FS field (even regs only)
	and	a3, a3, 0xF << 2		# mask FS field
	lw	a3, cmp_fs_d_tbl(a3)		# switch on register number
	j	a3

	.rdata
cmp_fs_d_tbl:
	.word	cmp_fs_d_f0
	.word	cmp_fs_d_f2
	.word	cmp_fs_d_f4
	.word	cmp_fs_d_f6
	.word	cmp_fs_d_f8
	.word	cmp_fs_d_f10
	.word	cmp_fs_d_f12
	.word	cmp_fs_d_f14
	.word	cmp_fs_d_f16
	.word	cmp_fs_d_f18
	.word	cmp_fs_d_f20
	.word	cmp_fs_d_f22
	.word	cmp_fs_d_f24
	.word	cmp_fs_d_f26
	.word	cmp_fs_d_f28
	.word	cmp_fs_d_f30
	.text

cmp_fs_d_f0:
	mfc1	t3, $f0
	mfc1	t0, $f1
	b	cmp_fs_d_done
cmp_fs_d_f2:
	mfc1	t3, $f2
	mfc1	t0, $f3
	b	cmp_fs_d_done
cmp_fs_d_f4:
	mfc1	t3, $f4
	mfc1	t0, $f5
	b	cmp_fs_d_done
cmp_fs_d_f6:
	mfc1	t3, $f6
	mfc1	t0, $f7
	b	cmp_fs_d_done
cmp_fs_d_f8:
	mfc1	t3, $f8
	mfc1	t0, $f9
	b	cmp_fs_d_done
cmp_fs_d_f10:
	mfc1	t3, $f10
	mfc1	t0, $f11
	b	cmp_fs_d_done
cmp_fs_d_f12:
	mfc1	t3, $f12
	mfc1	t0, $f13
	b	cmp_fs_d_done
cmp_fs_d_f14:
	mfc1	t3, $f14
	mfc1	t0, $f15
	b	cmp_fs_d_done
cmp_fs_d_f16:
	mfc1	t3, $f16
	mfc1	t0, $f17
	b	cmp_fs_d_done
cmp_fs_d_f18:
	mfc1	t3, $f18
	mfc1	t0, $f19
	b	cmp_fs_d_done
cmp_fs_d_f20:
	mfc1	t3, $f20
	mfc1	t0, $f21
	b	cmp_fs_d_done
cmp_fs_d_f22:
	mfc1	t3, $f22
	mfc1	t0, $f23
	b	cmp_fs_d_done
cmp_fs_d_f24:
	mfc1	t3, $f24
	mfc1	t0, $f25
	b	cmp_fs_d_done
cmp_fs_d_f26:
	mfc1	t3, $f26
	mfc1	t0, $f27
	b	cmp_fs_d_done
cmp_fs_d_f28:
	mfc1	t3, $f28
	mfc1	t0, $f29
	b	cmp_fs_d_done
cmp_fs_d_f30:
	mfc1	t3, $f30
	mfc1	t0, $f31
cmp_fs_d_done:
	srl	t1, t0, 20			# get exponent
	and	t1, t1, 0x7FF
	and	t2, t0, 0xFFFFF			# get fraction
	srl	t0, t0, 31			# get sign

	srl	a3, a0, 17 - 2			# get FT field (even regs only)
	and	a3, a3, 0xF << 2		# mask FT field
	lw	a3, cmp_ft_d_tbl(a3)		# switch on register number
	j	a3

	.rdata
cmp_ft_d_tbl:
	.word	cmp_ft_d_f0
	.word	cmp_ft_d_f2
	.word	cmp_ft_d_f4
	.word	cmp_ft_d_f6
	.word	cmp_ft_d_f8
	.word	cmp_ft_d_f10
	.word	cmp_ft_d_f12
	.word	cmp_ft_d_f14
	.word	cmp_ft_d_f16
	.word	cmp_ft_d_f18
	.word	cmp_ft_d_f20
	.word	cmp_ft_d_f22
	.word	cmp_ft_d_f24
	.word	cmp_ft_d_f26
	.word	cmp_ft_d_f28
	.word	cmp_ft_d_f30
	.text

cmp_ft_d_f0:
	mfc1	ta3, $f0
	mfc1	ta0, $f1
	b	cmp_ft_d_done
cmp_ft_d_f2:
	mfc1	ta3, $f2
	mfc1	ta0, $f3
	b	cmp_ft_d_done
cmp_ft_d_f4:
	mfc1	ta3, $f4
	mfc1	ta0, $f5
	b	cmp_ft_d_done
cmp_ft_d_f6:
	mfc1	ta3, $f6
	mfc1	ta0, $f7
	b	cmp_ft_d_done
cmp_ft_d_f8:
	mfc1	ta3, $f8
	mfc1	ta0, $f9
	b	cmp_ft_d_done
cmp_ft_d_f10:
	mfc1	ta3, $f10
	mfc1	ta0, $f11
	b	cmp_ft_d_done
cmp_ft_d_f12:
	mfc1	ta3, $f12
	mfc1	ta0, $f13
	b	cmp_ft_d_done
cmp_ft_d_f14:
	mfc1	ta3, $f14
	mfc1	ta0, $f15
	b	cmp_ft_d_done
cmp_ft_d_f16:
	mfc1	ta3, $f16
	mfc1	ta0, $f17
	b	cmp_ft_d_done
cmp_ft_d_f18:
	mfc1	ta3, $f18
	mfc1	ta0, $f19
	b	cmp_ft_d_done
cmp_ft_d_f20:
	mfc1	ta3, $f20
	mfc1	ta0, $f21
	b	cmp_ft_d_done
cmp_ft_d_f22:
	mfc1	ta3, $f22
	mfc1	ta0, $f23
	b	cmp_ft_d_done
cmp_ft_d_f24:
	mfc1	ta3, $f24
	mfc1	ta0, $f25
	b	cmp_ft_d_done
cmp_ft_d_f26:
	mfc1	ta3, $f26
	mfc1	ta0, $f27
	b	cmp_ft_d_done
cmp_ft_d_f28:
	mfc1	ta3, $f28
	mfc1	ta0, $f29
	b	cmp_ft_d_done
cmp_ft_d_f30:
	mfc1	ta3, $f30
	mfc1	ta0, $f31
cmp_ft_d_done:
	srl	ta1, ta0, 20			# get exponent
	and	ta1, ta1, 0x7FF
	and	ta2, ta0, 0xFFFFF		# get fraction
	srl	ta0, ta0, 31			# get sign
	j	ra
END(get_cmp_d)

/*----------------------------------------------------------------------------
 * set_fd_s --
 *
 *	Write (single precision) the FD register (bits 10-6).
 *	This is an internal routine used by MipsEmulateFP only.
 *
 * Arguments:
 *	a0	contains the FP instruction
 *	t0	contains the sign
 *	t1	contains the (biased) exponent
 *	t2	contains the fraction
 *
 * set_fd_word --
 *
 *	Write (integer) the FD register (bits 10-6).
 *	This is an internal routine used by MipsEmulateFP only.
 *
 * Arguments:
 *	a0	contains the FP instruction
 *	t2	contains the integer
 *
 *----------------------------------------------------------------------------
 */
LEAF(set_fd_s)
	sll	t0, t0, 31			# position sign
	sll	t1, t1, 23			# position exponent
	or	t2, t2, t0
	or	t2, t2, t1
XLEAF(set_fd_word)
	srl	a3, a0, 7 - 2			# get FD field (even regs only)
	and	a3, a3, 0xF << 2		# mask FT field
	lw	a3, set_fd_s_tbl(a3)		# switch on register number
	j	a3

	.rdata
set_fd_s_tbl:
	.word	set_fd_s_f0
	.word	set_fd_s_f2
	.word	set_fd_s_f4
	.word	set_fd_s_f6
	.word	set_fd_s_f8
	.word	set_fd_s_f10
	.word	set_fd_s_f12
	.word	set_fd_s_f14
	.word	set_fd_s_f16
	.word	set_fd_s_f18
	.word	set_fd_s_f20
	.word	set_fd_s_f22
	.word	set_fd_s_f24
	.word	set_fd_s_f26
	.word	set_fd_s_f28
	.word	set_fd_s_f30
	.text

set_fd_s_f0:
	mtc1	t2, $f0
	j	ra
set_fd_s_f2:
	mtc1	t2, $f2
	j	ra
set_fd_s_f4:
	mtc1	t2, $f4
	j	ra
set_fd_s_f6:
	mtc1	t2, $f6
	j	ra
set_fd_s_f8:
	mtc1	t2, $f8
	j	ra
set_fd_s_f10:
	mtc1	t2, $f10
	j	ra
set_fd_s_f12:
	mtc1	t2, $f12
	j	ra
set_fd_s_f14:
	mtc1	t2, $f14
	j	ra
set_fd_s_f16:
	mtc1	t2, $f16
	j	ra
set_fd_s_f18:
	mtc1	t2, $f18
	j	ra
set_fd_s_f20:
	mtc1	t2, $f20
	j	ra
set_fd_s_f22:
	mtc1	t2, $f22
	j	ra
set_fd_s_f24:
	mtc1	t2, $f24
	j	ra
set_fd_s_f26:
	mtc1	t2, $f26
	j	ra
set_fd_s_f28:
	mtc1	t2, $f28
	j	ra
set_fd_s_f30:
	mtc1	t2, $f30
	j	ra
END(set_fd_s)

/*----------------------------------------------------------------------------
 * set_fd_d --
 *
 *	Write (double precision) the FT register (bits 10-6).
 *	This is an internal routine used by MipsEmulateFP only.
 *
 * Arguments:
 *	a0	contains the FP instruction
 *	t0	contains the sign
 *	t1	contains the (biased) exponent
 *	t2	contains the fraction
 *	t3	contains the remaining fraction
 *
 *----------------------------------------------------------------------------
 */
LEAF(set_fd_d)
	sll	t0, t0, 31			# set sign
	sll	t1, t1, 20			# set exponent
	or	t0, t0, t1
	or	t0, t0, t2			# set fraction
	srl	a3, a0, 7 - 2			# get FD field (even regs only)
	and	a3, a3, 0xF << 2		# mask FD field
	lw	a3, set_fd_d_tbl(a3)		# switch on register number
	j	a3

	.rdata
set_fd_d_tbl:
	.word	set_fd_d_f0
	.word	set_fd_d_f2
	.word	set_fd_d_f4
	.word	set_fd_d_f6
	.word	set_fd_d_f8
	.word	set_fd_d_f10
	.word	set_fd_d_f12
	.word	set_fd_d_f14
	.word	set_fd_d_f16
	.word	set_fd_d_f18
	.word	set_fd_d_f20
	.word	set_fd_d_f22
	.word	set_fd_d_f24
	.word	set_fd_d_f26
	.word	set_fd_d_f28
	.word	set_fd_d_f30
	.text

set_fd_d_f0:
	mtc1	t3, $f0
	mtc1	t0, $f1
	j	ra
set_fd_d_f2:
	mtc1	t3, $f2
	mtc1	t0, $f3
	j	ra
set_fd_d_f4:
	mtc1	t3, $f4
	mtc1	t0, $f5
	j	ra
set_fd_d_f6:
	mtc1	t3, $f6
	mtc1	t0, $f7
	j	ra
set_fd_d_f8:
	mtc1	t3, $f8
	mtc1	t0, $f9
	j	ra
set_fd_d_f10:
	mtc1	t3, $f10
	mtc1	t0, $f11
	j	ra
set_fd_d_f12:
	mtc1	t3, $f12
	mtc1	t0, $f13
	j	ra
set_fd_d_f14:
	mtc1	t3, $f14
	mtc1	t0, $f15
	j	ra
set_fd_d_f16:
	mtc1	t3, $f16
	mtc1	t0, $f17
	j	ra
set_fd_d_f18:
	mtc1	t3, $f18
	mtc1	t0, $f19
	j	ra
set_fd_d_f20:
	mtc1	t3, $f20
	mtc1	t0, $f21
	j	ra
set_fd_d_f22:
	mtc1	t3, $f22
	mtc1	t0, $f23
	j	ra
set_fd_d_f24:
	mtc1	t3, $f24
	mtc1	t0, $f25
	j	ra
set_fd_d_f26:
	mtc1	t3, $f26
	mtc1	t0, $f27
	j	ra
set_fd_d_f28:
	mtc1	t3, $f28
	mtc1	t0, $f29
	j	ra
set_fd_d_f30:
	mtc1	t3, $f30
	mtc1	t0, $f31
	j	ra
END(set_fd_d)

/*----------------------------------------------------------------------------
 * renorm_fs_s --
 *
 * Results:
 *	t1	unbiased exponent
 *	t2	normalized fraction
 *
 *----------------------------------------------------------------------------
 */
LEAF(renorm_fs_s)
/*
 * Find out how many leading zero bits are in t2 and put in t9.
 */
	move	v0, t2
	move	t9, zero
	srl	v1, v0, 16
	bne	v1, zero, 1f
	addu	t9, 16
	sll	v0, 16
1:
	srl	v1, v0, 24
	bne	v1, zero, 1f
	addu	t9, 8
	sll	v0, 8
1:
	srl	v1, v0, 28
	bne	v1, zero, 1f
	addu	t9, 4
	sll	v0, 4
1:
	srl	v1, v0, 30
	bne	v1, zero, 1f
	addu	t9, 2
	sll	v0, 2
1:
	srl	v1, v0, 31
	bne	v1, zero, 1f
	addu	t9, 1
/*
 * Now shift t2 the correct number of bits.
 */
1:
	subu	t9, t9, SLEAD_ZEROS	# dont count normal leading zeros
	li	t1, SEXP_MIN
	subu	t1, t1, t9		# adjust exponent
	sll	t2, t2, t9
	j	ra
END(renorm_fs_s)

/*----------------------------------------------------------------------------
 * renorm_fs_d --
 *
 * Results:
 *	t1	unbiased exponent
 *	t2,t3	normalized fraction
 *
 *----------------------------------------------------------------------------
 */
LEAF(renorm_fs_d)
/*
 * Find out how many leading zero bits are in t2,t3 and put in t9.
 */
	move	v0, t2
	move	t9, zero
	bne	t2, zero, 1f
	move	v0, t3
	addu	t9, 32
1:
	srl	v1, v0, 16
	bne	v1, zero, 1f
	addu	t9, 16
	sll	v0, 16
1:
	srl	v1, v0, 24
	bne	v1, zero, 1f
	addu	t9, 8
	sll	v0, 8
1:
	srl	v1, v0, 28
	bne	v1, zero, 1f
	addu	t9, 4
	sll	v0, 4
1:
	srl	v1, v0, 30
	bne	v1, zero, 1f
	addu	t9, 2
	sll	v0, 2
1:
	srl	v1, v0, 31
	bne	v1, zero, 1f
	addu	t9, 1
/*
 * Now shift t2,t3 the correct number of bits.
 */
1:
	subu	t9, t9, DLEAD_ZEROS	# dont count normal leading zeros
	li	t1, DEXP_MIN
	subu	t1, t1, t9		# adjust exponent
	li	v0, 32
	blt	t9, v0, 1f
	subu	t9, t9, v0		# shift fraction left >= 32 bits
	sll	t2, t3, t9
	move	t3, zero
	j	ra
1:
	subu	v0, v0, t9		# shift fraction left < 32 bits
	sll	t2, t2, t9
	srl	v1, t3, v0
	or	t2, t2, v1
	sll	t3, t3, t9
	j	ra
END(renorm_fs_d)

/*----------------------------------------------------------------------------
 * renorm_ft_s --
 *
 * Results:
 *	ta1	unbiased exponent
 *	ta2	normalized fraction
 *
 *----------------------------------------------------------------------------
 */
LEAF(renorm_ft_s)
/*
 * Find out how many leading zero bits are in ta2 and put in t9.
 */
	move	v0, ta2
	move	t9, zero
	srl	v1, v0, 16
	bne	v1, zero, 1f
	addu	t9, 16
	sll	v0, 16
1:
	srl	v1, v0, 24
	bne	v1, zero, 1f
	addu	t9, 8
	sll	v0, 8
1:
	srl	v1, v0, 28
	bne	v1, zero, 1f
	addu	t9, 4
	sll	v0, 4
1:
	srl	v1, v0, 30
	bne	v1, zero, 1f
	addu	t9, 2
	sll	v0, 2
1:
	srl	v1, v0, 31
	bne	v1, zero, 1f
	addu	t9, 1
/*
 * Now shift ta2 the correct number of bits.
 */
1:
	subu	t9, t9, SLEAD_ZEROS	# dont count normal leading zeros
	li	ta1, SEXP_MIN
	subu	ta1, ta1, t9		# adjust exponent
	sll	ta2, ta2, t9
	j	ra
END(renorm_ft_s)

/*----------------------------------------------------------------------------
 * renorm_ft_d --
 *
 * Results:
 *	ta1	unbiased exponent
 *	ta2,ta3	normalized fraction
 *
 *----------------------------------------------------------------------------
 */
LEAF(renorm_ft_d)
/*
 * Find out how many leading zero bits are in ta2,ta3 and put in t9.
 */
	move	v0, ta2
	move	t9, zero
	bne	ta2, zero, 1f
	move	v0, ta3
	addu	t9, 32
1:
	srl	v1, v0, 16
	bne	v1, zero, 1f
	addu	t9, 16
	sll	v0, 16
1:
	srl	v1, v0, 24
	bne	v1, zero, 1f
	addu	t9, 8
	sll	v0, 8
1:
	srl	v1, v0, 28
	bne	v1, zero, 1f
	addu	t9, 4
	sll	v0, 4
1:
	srl	v1, v0, 30
	bne	v1, zero, 1f
	addu	t9, 2
	sll	v0, 2
1:
	srl	v1, v0, 31
	bne	v1, zero, 1f
	addu	t9, 1
/*
 * Now shift ta2,ta3 the correct number of bits.
 */
1:
	subu	t9, t9, DLEAD_ZEROS	# dont count normal leading zeros
	li	ta1, DEXP_MIN
	subu	ta1, ta1, t9		# adjust exponent
	li	v0, 32
	blt	t9, v0, 1f
	subu	t9, t9, v0		# shift fraction left >= 32 bits
	sll	ta2, ta3, t9
	move	ta3, zero
	j	ra
1:
	subu	v0, v0, t9		# shift fraction left < 32 bits
	sll	ta2, ta2, t9
	srl	v1, ta3, v0
	or	ta2, ta2, v1
	sll	ta3, ta3, t9
	j	ra
END(renorm_ft_d)
