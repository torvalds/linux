/*	$OpenBSD: s_significandf.S,v 1.6 2022/08/27 08:30:18 tb Exp $ */
/*
 * Written by J.T. Conklin <jtc@NetBSD.org>.
 * Public domain.
 */

#include <machine/asm.h>

#include "abi.h"

ENTRY_NB(significandf)
	RETGUARD_SETUP(significandf, r11)
	XMM_ONE_ARG_FLOAT_PROLOGUE
	flds	ARG_FLOAT_ONE
	fxtract
	fstp	%st(1)
	XMM_FLOAT_EPILOGUE
	RETGUARD_CHECK(significandf, r11)
	ret
END(significandf)
	.weak	significandf
