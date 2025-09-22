/*	$OpenBSD: fabs.S,v 1.8 2018/07/03 23:14:05 mortimer Exp $	*/
/*	$NetBSD: fabs.S,v 1.1 2001/06/19 00:25:02 fvdl Exp $	*/

#include <machine/asm.h>

/*
 * Ok, this sucks. Is there really no way to push an xmm register onto
 * the FP stack directly?
 */

ENTRY(fabs)
	RETGUARD_SETUP(fabs, r11)
	movsd	%xmm0, -8(%rsp)
	fldl	-8(%rsp)
	fabs
	fstpl	-8(%rsp)
	movsd	-8(%rsp),%xmm0
	RETGUARD_CHECK(fabs, r11)
	ret
END(fabs)
