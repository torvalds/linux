/*	$OpenBSD: sigsuspend.S,v 1.8 2016/05/07 19:05:21 guenther Exp $	*/
/*	$NetBSD: sigsuspend.S,v 1.2 1996/10/17 03:08:22 cgd Exp $	*/

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

SYSLEAF_HIDDEN(sigsuspend, 1)
	ldl	a0, 0(a0)		/* pass *mask instead of mask */
	CALLSYS_ERROR(sigsuspend)
	mov	zero, v0		/* shouldn't need; just in case... */
	RET
SYSCALL_END_HIDDEN(sigsuspend)
