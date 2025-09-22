/*	$OpenBSD: s_copysign.S,v 1.8 2018/07/03 22:43:34 mortimer Exp $ */
/*
 * Written by J.T. Conklin <jtc@NetBSD.org>.
 * Public domain.
 */

#include <machine/asm.h>

#include "abi.h"

	.section .rodata
.Lpos:
	.quad	0x8000000000000000
.Lneg:
	.quad	0x7fffffffffffffff

ENTRY(copysign)
	RETGUARD_SETUP(copysign, r11)
	movq	.Lpos(%rip),%xmm2
	movq	.Lneg(%rip),%xmm3
	pand	%xmm2,%xmm1
	pand	%xmm3,%xmm0
	por	%xmm1,%xmm0
	RETGUARD_CHECK(copysign, r11)
	ret
END_STD(copysign)
