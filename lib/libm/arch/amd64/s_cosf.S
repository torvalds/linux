/*	$OpenBSD: s_cosf.S,v 1.4 2018/07/03 22:43:34 mortimer Exp $ */
/*
 * Written by J.T. Conklin <jtc@NetBSD.org>.
 * Public domain.
 */

#include <machine/asm.h>

#include "abi.h"

/* A float's domain isn't large enough to require argument reduction. */
ENTRY(cosf)
	RETGUARD_SETUP(cosf, r11)
	XMM_ONE_ARG_FLOAT_PROLOGUE
	flds	ARG_FLOAT_ONE
	fcos
	XMM_FLOAT_EPILOGUE
	RETGUARD_CHECK(cosf, r11)
	ret	
END_STD(cosf)
