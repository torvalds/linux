/*	$OpenBSD: s_atanf.S,v 1.4 2018/07/03 22:43:34 mortimer Exp $ */
/*
 * Written by J.T. Conklin <jtc@NetBSD.org>.
 * Public domain.
 */

#include <machine/asm.h>

#include "abi.h"

ENTRY(atanf)
	RETGUARD_SETUP(atanf, r11)
	XMM_ONE_ARG_FLOAT_PROLOGUE
	flds	ARG_FLOAT_ONE
	fld1
	fpatan
	XMM_FLOAT_EPILOGUE
	RETGUARD_CHECK(atanf, r11)
	ret
END_STD(atanf)
