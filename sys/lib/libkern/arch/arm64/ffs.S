/*	$OpenBSD: ffs.S,v 1.1 2020/06/10 20:19:29 naddy Exp $ */
/*
 * Written by Christian Weisgerber <naddy@openbsd.org>.
 * Public domain.
 */
 
#include <machine/asm.h>

ENTRY(ffs)
	RETGUARD_SETUP(ffs, x15)
	rbit	w1, w0
	clz	w1, w1
	cmp	w0, wzr
	csinc	w0, wzr, w1, eq
	RETGUARD_CHECK(ffs, x15)
	ret
END(ffs)
