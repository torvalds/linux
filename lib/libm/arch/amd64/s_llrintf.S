/*	$OpenBSD: s_llrintf.S,v 1.3 2018/07/03 22:43:34 mortimer Exp $	*/

/*
 * Written by Martynas Venckus.  Public domain
 */

#include <machine/asm.h>
#include "abi.h"

ENTRY(llrintf)
	RETGUARD_SETUP(llrintf, r11)
	cvtss2si %xmm0, %rax
	RETGUARD_CHECK(llrintf, r11)
	ret
END_STD(llrintf)
