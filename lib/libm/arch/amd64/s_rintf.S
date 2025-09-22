/*	$OpenBSD: s_rintf.S,v 1.4 2018/07/03 22:43:34 mortimer Exp $ */
/*
 * Written by J.T. Conklin <jtc@NetBSD.org>.
 * Public domain.
 */

#include <machine/asm.h>

#include "abi.h"

ENTRY(rintf)
	RETGUARD_SETUP(rintf, r11)
	XMM_ONE_ARG_FLOAT_PROLOGUE
	flds	ARG_FLOAT_ONE
	frndint
	XMM_FLOAT_EPILOGUE
	RETGUARD_CHECK(rintf, r11)
	ret
END_STD(rintf)
