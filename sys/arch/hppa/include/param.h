/*	$OpenBSD: param.h,v 1.49 2023/12/14 13:26:49 claudio Exp $	*/

/*
 * Copyright (c) 1988-1994, The University of Utah and
 * the Computer Systems Laboratory at the University of Utah (CSL).
 * All rights reserved.
 *
 * Permission to use, copy, modify and distribute this software is hereby
 * granted provided that (1) source code retains these copyright, permission,
 * and disclaimer notices, and (2) redistributions including binaries
 * reproduce the notices in supporting documentation, and (3) all advertising
 * materials mentioning features or use of this software display the following
 * acknowledgement: ``This product includes software developed by the
 * Computer Systems Laboratory at the University of Utah.''
 *
 * THE UNIVERSITY OF UTAH AND CSL ALLOW FREE USE OF THIS SOFTWARE IN ITS "AS
 * IS" CONDITION.  THE UNIVERSITY OF UTAH AND CSL DISCLAIM ANY LIABILITY OF
 * ANY KIND FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * CSL requests users of this software to return to csl-dist@cs.utah.edu any
 * improvements that they make and grant CSL redistribution rights.
 */

#ifndef	_MACHINE_PARAM_H_
#define	_MACHINE_PARAM_H_

#ifdef _KERNEL
#include <machine/cpu.h>
#include <machine/intr.h>
#endif

#define	_MACHINE	hppa
#define	MACHINE		"hppa"
#define	_MACHINE_ARCH	hppa
#define	MACHINE_ARCH	"hppa"
#define	MID_MACHINE	MID_HPUX800

#define	PAGE_SHIFT	12
#define	PAGE_SIZE	(1 << PAGE_SHIFT)
#define	PAGE_MASK	(PAGE_SIZE - 1)

#define	KERNBASE	0x00000000		/* start of kernel virtual */

#ifdef _KERNEL

#define	NBPG		PAGE_SIZE		/* bytes/page */
#define	PGSHIFT		PAGE_SHIFT		/* LOG2(PAGE_SIZE) */
#define	PGOFSET		PAGE_MASK		/* byte offset into page */

#define	UPAGES		4			/* pages of u-area */
#define	USPACE		(UPAGES * PAGE_SIZE)	/* total size of u-area */
#define	USPACE_ALIGN	0			/* u-area alignment 0-none */

#define	NMBCLUSTERS	(32 * 1024)		/* max cluster allocation */

#ifndef	MSGBUFSIZE
#define	MSGBUFSIZE	(2 * PAGE_SIZE)		/* default message buffer size */
#endif

#endif /* _KERNEL */

#define	MACHINE_STACK_GROWS_UP	1		/* stack grows to higher addresses */

#endif /* _MACHINE_PARAM_H_ */
