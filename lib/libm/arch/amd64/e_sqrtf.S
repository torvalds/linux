/*	$OpenBSD: e_sqrtf.S,v 1.6 2018/07/03 22:43:34 mortimer Exp $ */
/*
 * Written by J.T. Conklin <jtc@NetBSD.org>.
 * Public domain.
 */

#include <machine/asm.h>
#include "abi.h"

ENTRY(sqrtf)
	RETGUARD_SETUP(sqrtf, r11)
	sqrtss	%xmm0,%xmm0
	RETGUARD_CHECK(sqrtf, r11)
	ret
END_STD(sqrtf)
