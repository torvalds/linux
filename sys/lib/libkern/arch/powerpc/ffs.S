/*	$OpenBSD: ffs.S,v 1.3 2020/11/28 19:49:30 gkoehler Exp $ */
/*
 * Written by Christian Weisgerber <naddy@openbsd.org>.
 * Public domain.
 */
 
#include <machine/asm.h>

ENTRY(ffs)
	RETGUARD_SETUP(ffs, %r11, %r12)
	neg	%r4, %r3
	and	%r3, %r3, %r4
	cntlzw	%r3, %r3
	subfic	%r3, %r3, 32
	RETGUARD_CHECK(ffs, %r11, %r12)
	blr
END(ffs)
