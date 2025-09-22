/*	$OpenBSD: s_tan.S,v 1.4 2018/07/03 22:43:34 mortimer Exp $ */
/*
 * Written by J.T. Conklin <jtc@NetBSD.org>.
 * Public domain.
 */

#include <machine/asm.h>

#include "abi.h"

ENTRY(tan)
	RETGUARD_SETUP(tan, r11)
	XMM_ONE_ARG_DOUBLE_PROLOGUE
	fldl	ARG_DOUBLE_ONE
	fptan
	fnstsw	%ax
	andw	$0x400,%ax
	jnz	1f
	fstp	%st(0)
	XMM_DOUBLE_EPILOGUE
	RETGUARD_CHECK(tan, r11)
	ret
1:	fldpi
	fadd	%st(0)
	fxch	%st(1)
2:	fprem1
	fstsw	%ax
	andw	$0x400,%ax
	jnz	2b
	fstp	%st(1)
	fptan
	fstp	%st(0)
	XMM_DOUBLE_EPILOGUE
	RETGUARD_CHECK(tan, r11)
	ret
END(tan)
