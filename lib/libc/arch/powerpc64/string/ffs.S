/*	$OpenBSD: ffs.S,v 1.2 2020/10/16 23:42:16 deraadt Exp $ */
/*
 * Written by Christian Weisgerber <naddy@openbsd.org>.
 * Public domain.
 */
 
#include "DEFS.h"

ENTRY(ffs)
	RETGUARD_SETUP(ffs, %r11);
	neg	%r4, %r3
	and	%r3, %r3, %r4
	cntlzw	%r3, %r3
	subfic	%r3, %r3, 32
	RETGUARD_CHECK(ffs, %r11);
	blr
END_BUILTIN(ffs)
