/*	$OpenBSD: s_ceilf.S,v 1.6 2018/07/03 22:43:34 mortimer Exp $ */
/*
 * Written by J.T. Conklin <jtc@NetBSD.org>.
 * Public domain.
 */

#include <machine/asm.h>

ENTRY(ceilf)
	RETGUARD_SETUP(ceilf, r11)
	fstcw	-8(%rsp)
	movw	-8(%rsp),%dx
	orw	$0x0800,%dx
	andw	$0xfbff,%dx
	movw	%dx,-12(%rsp)
	fldcw	-12(%rsp)
	movss	%xmm0,-4(%rsp)
	flds	-4(%rsp)
	frndint
	fldcw	-8(%rsp)
	fstps	-4(%rsp)
	movss	-4(%rsp),%xmm0
	RETGUARD_CHECK(ceilf, r11)
	ret
END(ceilf)
