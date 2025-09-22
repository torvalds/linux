/*	$OpenBSD: sigprocmask.S,v 1.6 2015/10/23 04:39:24 guenther Exp $	*/
/*	$NetBSD: sigprocmask.S,v 1.2 1996/10/17 03:08:21 cgd Exp $	*/

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

SYSLEAF_HIDDEN(sigprocmask, 3)
	mov	a2, a5			/* safe */
	cmoveq	a1, 1, a0		/* if set == NULL, how = SIG_BLOCK */
	beq	a1, Ldoit		/* and set = 0, and do it. */
	ldl	a1, 0(a1)		/* load the set from *set */
Ldoit:	CALLSYS_ERROR(sigprocmask)
	beq	a5, Lret		/* if they don't want old mask, done */	
	stl	v0, 0(a5)		/* otherwise, give it to them. */
Lret:	mov	zero, v0
	RET

SYSCALL_END_HIDDEN(sigprocmask)
