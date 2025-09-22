/*	$OpenBSD: e_scalb.S,v 1.6 2022/08/27 08:30:18 tb Exp $ */
/*
 * Written by J.T. Conklin <jtc@NetBSD.org>.
 * Public domain.
 */

#include <machine/asm.h>

#include "abi.h"

ENTRY_NB(scalb)
	RETGUARD_SETUP(scalb, r11)
	XMM_TWO_ARG_DOUBLE_PROLOGUE
	fldl	ARG_DOUBLE_TWO
	fldl	ARG_DOUBLE_ONE
	fscale
	fstp	%st(1)			/* bug fix for fp stack overflow */
	XMM_DOUBLE_EPILOGUE
	RETGUARD_CHECK(scalb, r11)
	ret
END_NONSTD(scalb)
