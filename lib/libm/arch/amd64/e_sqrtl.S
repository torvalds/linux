/*	$OpenBSD: e_sqrtl.S,v 1.3 2018/07/03 22:43:34 mortimer Exp $ */
/*
 * Written by J.T. Conklin <jtc@netbsd.org>.
 * Public domain.
 */

#include <machine/asm.h>
#include "abi.h"

ENTRY(sqrtl)
	RETGUARD_SETUP(sqrtl, r11)
	fldt	8(%rsp)
	fsqrt
	RETGUARD_CHECK(sqrtl, r11)
	ret
END_STD(sqrtl)
