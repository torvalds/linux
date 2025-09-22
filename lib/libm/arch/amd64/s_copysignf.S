/*	$OpenBSD: s_copysignf.S,v 1.8 2018/07/03 22:43:34 mortimer Exp $ */
/*
 * Written by J.T. Conklin <jtc@NetBSD.org>.
 * Public domain.
 */

#include <machine/asm.h>

#include "abi.h"

	.section .rodata
.Lneg:
	.long 0x7fffffff
.Lpos:
	.long 0x80000000

ENTRY(copysignf)
	RETGUARD_SETUP(copysignf, r11)
	movss    .Lpos(%rip),%xmm2
	movss    .Lneg(%rip),%xmm3
	pand    %xmm2,%xmm1
	pand    %xmm3,%xmm0
	por     %xmm1,%xmm0
	RETGUARD_CHECK(copysignf, r11)
	ret
END_STD(copysignf)
