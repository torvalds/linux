/*	$OpenBSD: e_log10.S,v 1.5 2018/07/03 22:43:34 mortimer Exp $ */
/*
 * Written by J.T. Conklin <jtc@NetBSD.org>.
 * Public domain.
 */

#include <machine/asm.h>

#include "abi.h"

ENTRY(log10)
	RETGUARD_SETUP(log10, r11)
	XMM_ONE_ARG_DOUBLE_PROLOGUE
	fldlg2
	fldl	ARG_DOUBLE_ONE
	fyl2x
	XMM_DOUBLE_EPILOGUE
	RETGUARD_CHECK(log10, r11)
	ret
END(log10)
