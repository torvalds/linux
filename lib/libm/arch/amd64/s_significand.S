/*	$OpenBSD: s_significand.S,v 1.6 2022/08/27 08:30:18 tb Exp $ */
/*
 * Written by J.T. Conklin <jtc@NetBSD.org>.
 * Public domain.
 */

#include <machine/asm.h>

#include "abi.h"

ENTRY_NB(significand)
	RETGUARD_SETUP(significand, r11)
	XMM_ONE_ARG_DOUBLE_PROLOGUE
	fldl	ARG_DOUBLE_ONE
	fxtract
	fstp	%st(1)
	XMM_DOUBLE_EPILOGUE
	RETGUARD_CHECK(significand, r11)
	ret
END(significand)
	.weak	significand
