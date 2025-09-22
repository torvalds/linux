/*	$OpenBSD: s_ceil.S,v 1.5 2018/07/03 22:43:34 mortimer Exp $ */
/*
 * Written by J.T. Conklin <jtc@NetBSD.org>.
 * Public domain.
 */

#include <machine/asm.h>

#include "abi.h"

ENTRY(ceil)
	RETGUARD_SETUP(ceil, r11)
	fstcw	-12(%rsp)
	movw	-12(%rsp),%dx
	orw	$0x0800,%dx
	andw	$0xfbff,%dx
	movw	%dx,-16(%rsp)
	fldcw	-16(%rsp)
	movsd	%xmm0,-8(%rsp)
	fldl	-8(%rsp)
	frndint
	fldcw	-12(%rsp)
	fstpl	-8(%rsp)
	movsd	-8(%rsp),%xmm0
	RETGUARD_CHECK(ceil, r11)
	ret
END_STD(ceil)
