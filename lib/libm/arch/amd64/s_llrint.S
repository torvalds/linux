/*	$OpenBSD: s_llrint.S,v 1.3 2018/07/03 22:43:34 mortimer Exp $	*/

/*
 * Written by Martynas Venckus.  Public domain
 */

#include <machine/asm.h>

ENTRY(llrint)
	RETGUARD_SETUP(llrint, r11)
	cvtsd2si %xmm0, %rax
	RETGUARD_CHECK(llrint, r11)
	ret
END(llrint)
