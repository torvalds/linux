/*	$OpenBSD: e_acos.S,v 1.6 2018/07/03 22:43:34 mortimer Exp $ */
/*
 * Written by J.T. Conklin <jtc@NetBSD.org>.
 * Public domain.
 */

#include <machine/asm.h>

#include "abi.h"

/* acos = atan (sqrt(1 - x^2) / x) */
ENTRY(acos)
	RETGUARD_SETUP(acos, r11)
	XMM_ONE_ARG_DOUBLE_PROLOGUE
	fldl	ARG_DOUBLE_ONE		/* x */
	fld	%st(0)
	fmul	%st(0)			/* x^2 */
	fld1				
	fsubp				/* 1 - x^2 */
	fsqrt				/* sqrt (1 - x^2) */
	fxch	%st(1)
	fpatan
	XMM_DOUBLE_EPILOGUE
	RETGUARD_CHECK(acos, r11)
	ret
END(acos)
