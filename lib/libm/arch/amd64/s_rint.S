/*	$OpenBSD: s_rint.S,v 1.4 2018/07/03 22:43:34 mortimer Exp $ */
/*
 * Written by J.T. Conklin <jtc@NetBSD.org>.
 * Public domain.
 */

#include <machine/asm.h>

#include "abi.h"

ENTRY(rint)
	RETGUARD_SETUP(rint, r11)
	XMM_ONE_ARG_DOUBLE_PROLOGUE
	fldl	ARG_DOUBLE_ONE
	frndint
	XMM_DOUBLE_EPILOGUE
	RETGUARD_CHECK(rint, r11)
	ret
END_STD(rint)
