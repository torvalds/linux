/*	$OpenBSD: s_scalbnf.S,v 1.6 2018/07/03 22:43:34 mortimer Exp $ */
/*
 * Written by J.T. Conklin <jtc@NetBSD.org>.
 * Public domain.
 */

#include <machine/asm.h>

#include "abi.h"

.global ldexpf
	ldexpf = scalbnf

ENTRY(scalbnf)
	RETGUARD_SETUP(scalbnf, r11)
	movss	%xmm0,-8(%rsp)
	movl	%edi,-4(%rsp)
	fildl	-4(%rsp)
	flds	-8(%rsp)
	fscale
	fstp	%st(1)		/* bug fix for fp stack overflow */
	fstps	-8(%rsp)
	movss	-8(%rsp),%xmm0
	RETGUARD_CHECK(scalbnf, r11)
	ret
END_STD(scalbnf)
