/*	$OpenBSD: brk.S,v 1.8 2015/09/10 13:29:09 guenther Exp $	*/
/*	$NetBSD: brk.S,v 1.4 1996/10/17 03:08:15 cgd Exp $	*/

/*
 * Copyright (c) 1994, 1995 Carnegie-Mellon University.
 * All rights reserved.
 *
 * Author: Chris G. Demetriou
 * 
 * Permission to use, copy, modify and distribute this software and
 * its documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 * 
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS" 
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND 
 * FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 * 
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 */

#include "SYS.h"

	.globl	_end
IMPORT(__curbrk, 8)

	.data
__minbrk:
	.quad	_end
	.size	__minbrk, . - __minbrk
	.type	__minbrk,@object

	.text
	.weak	brk
LEAF(brk, 1)
	br	pv, L1				/* XXX profiling */
L1:	LDGP(pv)
	ldq	v0, __minbrk
	cmpult  a0, v0, t0
	cmovne  t0, v0, a0
	CALLSYS_ERROR(break)
	stq	a0, __curbrk
	mov	zero, v0
	RET
END(brk)
