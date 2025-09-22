/*	$OpenBSD: s_sinf.S,v 1.4 2018/07/03 22:43:34 mortimer Exp $ */
/*
 * Written by J.T. Conklin <jtc@NetBSD.org>.
 * Public domain.
 */

#include <machine/asm.h>

#include "abi.h"

/* A float's domain isn't large enough to require argument reduction. */
ENTRY(sinf)
	RETGUARD_SETUP(sinf, r11)
	XMM_ONE_ARG_FLOAT_PROLOGUE
	flds	ARG_FLOAT_ONE
	fsin
	XMM_FLOAT_EPILOGUE
	RETGUARD_CHECK(sinf, r11)
	ret
END_STD(sinf)
