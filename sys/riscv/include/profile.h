/*-
 * SPDX-License-Identifier: MIT-CMU
 *
 * Copyright (c) 1994, 1995, 1996 Carnegie-Mellon University.
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
 *
 *	from: NetBSD: profile.h,v 1.9 1997/04/06 08:47:37 cgd Exp
 *	from: FreeBSD: src/sys/alpha/include/profile.h,v 1.4 1999/12/29
 * $FreeBSD$
 */

#ifndef _MACHINE_PROFILE_H_
#define	_MACHINE_PROFILE_H_

#if !defined(_KERNEL) && !defined(_SYS_CDEFS_H_)
#error this file needs sys/cdefs.h as a prerequisite
#endif

#define	FUNCTION_ALIGNMENT	32

typedef u_long	fptrdiff_t;

#ifdef _KERNEL

#include <machine/cpufunc.h>

#define	_MCOUNT_DECL	void mcount
#define	MCOUNT

#define	MCOUNT_DECL(s)	register_t s;
#define	MCOUNT_ENTER(s)	{s = intr_disable(); }
#define	MCOUNT_EXIT(s)	{intr_restore(s); }

void bintr(void);
void btrap(void);
void eintr(void);
void user(void);

#define	MCOUNT_FROMPC_USER(pc)					\
	((pc < (uintfptr_t)VM_MAXUSER_ADDRESS) ? (uintfptr_t)user : pc)

#define	MCOUNT_FROMPC_INTR(pc)					\
	((pc >= (uintfptr_t)btrap && pc < (uintfptr_t)eintr) ?	\
	    ((pc >= (uintfptr_t)bintr) ? (uintfptr_t)bintr :	\
		(uintfptr_t)btrap) : ~0UL)

void	mcount(uintfptr_t frompc, uintfptr_t selfpc);

#else /* !_KERNEL */

typedef unsigned long long	uintfptr_t;

#define	_MCOUNT_DECL	void mcount
#define	MCOUNT

#endif /* _KERNEL */

#endif /* !_MACHINE_PROFILE_H_ */
