/*	$OpenBSD: e_exp.S,v 1.7 2018/07/03 22:43:34 mortimer Exp $	*/
/*	$NetBSD: e_exp.S,v 1.12 2002/02/27 16:32:46 christos Exp $	*/

/*
 * Copyright (c) 1993,94 Winning Strategies, Inc.
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Winning Strategies, Inc.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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

/*
 * Written by:
 *	J.T. Conklin (jtc@wimsey.com), Winning Strategies, Inc.
 */

#include <machine/asm.h>

#include "abi.h"

/* e^x = 2^(x * log2(e)) */
ENTRY(exp)
	RETGUARD_SETUP(exp, r11)
	XMM_ONE_ARG_DOUBLE_PROLOGUE
	/*
	 * If x is +-Inf, then the subtraction would give Inf-Inf = NaN.
	 * Avoid this.  Also avoid it if x is NaN for convenience.
	 */
	movl	-4(%rsp),%eax
	andl	$0x7fffffff,%eax
	cmpl	$0x7ff00000,%eax
	jae	x_Inf_or_NaN

	fldl	ARG_DOUBLE_ONE

	/*
	 * Ensure that the rounding mode is to nearest (to give the smallest
	 * possible fraction) and that the precision is as high as possible.
	 * We may as well mask interrupts if we switch the mode.
	 */
	fstcw	-8(%rsp)
	movl	-8(%rsp),%eax
	andl	$0x0300,%eax
	cmpl	$0x0300,%eax		/* RC == 0 && PC == 3? */
	je	1f			/* jump if mode is good */
	movl	$0x137f,-4(%rsp)
	fldcw	-4(%rsp)
1:
	fldl2e
	fmulp				/* x * log2(e) */
	fst	%st(1)
	frndint				/* int(x * log2(e)) */
	fst	%st(2)
	fsubrp				/* fract(x * log2(e)) */
	f2xm1				/* 2^(fract(x * log2(e))) - 1 */ 
	fld1
	faddp				/* 2^(fract(x * log2(e))) */
	fscale				/* e^x */
	fstp	%st(1)
	je	1f
	fldcw	-8(%rsp)
1:
	XMM_DOUBLE_EPILOGUE
	RETGUARD_CHECK(exp, r11)
	ret

x_Inf_or_NaN:
	/*
	 * Return 0 if x is -Inf.  Otherwise just return x, although the
	 * C version would return (x + x) (Real Indefinite) if x is a NaN.
	 */
	cmpl	$0xfff00000,-4(%rsp)
	jne	x_not_minus_Inf
	cmpl	$0,-8(%rsp)
	jne	x_not_minus_Inf
	xorpd	%xmm0,%xmm0
	RETGUARD_CHECK(exp, r11)
	ret

x_not_minus_Inf:
	movsd	ARG_DOUBLE_ONE,%xmm0
	RETGUARD_CHECK(exp, r11)
	ret
END_STD(exp)
