/*	$OpenBSD: e_atan2f.S,v 1.5 2018/07/03 22:43:34 mortimer Exp $ */
/*
 * Written by J.T. Conklin <jtc@NetBSD.org>.
 * Public domain.
 */

#include <machine/asm.h>

#include "abi.h"

ENTRY(atan2f)
	RETGUARD_SETUP(atan2f, r11)
	XMM_TWO_ARG_FLOAT_PROLOGUE
	flds	ARG_FLOAT_ONE
	flds	ARG_FLOAT_TWO
	fpatan
	XMM_FLOAT_EPILOGUE
	RETGUARD_CHECK(atan2f, r11)
	ret
END_STD(atan2f)
