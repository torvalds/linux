/*	$OpenBSD: e_sqrt.S,v 1.6 2018/07/03 22:43:34 mortimer Exp $ */
/*
 * Written by J.T. Conklin <jtc@NetBSD.org>.
 * Public domain.
 */

#include <machine/asm.h>
#include "abi.h"

ENTRY(sqrt)
	RETGUARD_SETUP(sqrt, r11)
	sqrtsd	%xmm0,%xmm0
	RETGUARD_CHECK(sqrt, r11)
	ret
END_STD(sqrt)
