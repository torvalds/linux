/*	$OpenBSD: s_lrintf.S,v 1.3 2018/07/03 22:43:34 mortimer Exp $	*/

/*
 * Written by Martynas Venckus.  Public domain
 */

#include <machine/asm.h>

ENTRY(lrintf)
	RETGUARD_SETUP(lrintf, r11)
	cvtss2si %xmm0, %rax
	RETGUARD_CHECK(lrintf, r11)
	ret
END(lrintf)
