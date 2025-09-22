/*	$OpenBSD: s_lrint.S,v 1.3 2018/07/03 22:43:34 mortimer Exp $	*/

/*
 * Written by Martynas Venckus.  Public domain
 */

#include <machine/asm.h>

ENTRY(lrint)
	RETGUARD_SETUP(lrint, r11)
	cvtsd2si %xmm0, %rax
	RETGUARD_CHECK(lrint, r11)
	ret
END(lrint)
