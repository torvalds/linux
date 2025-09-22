/*	$OpenBSD: s_sin.S,v 1.4 2018/07/03 22:43:34 mortimer Exp $ */
/*
 * Written by J.T. Conklin <jtc@NetBSD.org>.
 * Public domain.
 */

#include <machine/asm.h>

#include "abi.h"

ENTRY(sin)
	RETGUARD_SETUP(sin, r11)
	XMM_ONE_ARG_DOUBLE_PROLOGUE
	fldl	ARG_DOUBLE_ONE
	fsin
	fnstsw	%ax
	andw	$0x400,%ax
	jnz	1f
	XMM_DOUBLE_EPILOGUE
	RETGUARD_CHECK(sin, r11)
	ret
1:	fldpi
	fadd	%st(0)
	fxch	%st(1)
2:	fprem1
	fnstsw	%ax
	andw	$0x400,%ax
	jnz	2b
	fstp	%st(1)
	fsin
	XMM_DOUBLE_EPILOGUE
	RETGUARD_CHECK(sin, r11)
	ret
END_STD(sin)
