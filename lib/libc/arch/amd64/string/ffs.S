/*	$OpenBSD: ffs.S,v 1.6 2018/07/03 23:14:05 mortimer Exp $	*/
/*
 * Written by J.T. Conklin <jtc@netbsd.org>.
 * Public domain.
 * Adapted for NetBSD/x86_64 by Frank van der Linden <fvdl@wasabisystems.com>
 */

#include "DEFS.h"

ENTRY(ffs)
	RETGUARD_SETUP(ffs, r11)
	bsfl	%edi,%eax
	jz	L1	 		/* ZF is set if all bits are 0 */
	incl	%eax			/* bits numbered from 1, not 0 */
	jmp L2

	_ALIGN_TEXT
L1:	xorl	%eax,%eax		/* clear result */
L2:	RETGUARD_CHECK(ffs, r11)
	ret
END(ffs)
.protected ffs
