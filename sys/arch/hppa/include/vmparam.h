/*	$OpenBSD: vmparam.h,v 1.48 2024/11/14 20:32:13 miod Exp $	*/

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
 *
 * 	Utah $Hdr: vmparam.h 1.16 94/12/16$
 */

#ifndef _MACHINE_VMPARAM_H_
#define _MACHINE_VMPARAM_H_

/*
 * Machine dependent constants for HP PA
 */
#define	USRSTACK	0xB8000000UL	/* start (bottom) of user stack */
#define	SYSCALLGATE	0xC0000000	/* syscall gateway page */

/*
 * Virtual memory related constants, all in bytes
 */
#ifndef MAXTSIZ
#define	MAXTSIZ		(512*1024*1024UL)	/* max text size */
#endif
#ifndef DFLDSIZ
#define	DFLDSIZ		(16*1024*1024)		/* initial data size limit */
#endif
#ifndef MAXDSIZ
#define	MAXDSIZ		(1*1024*1024*1024UL)	/* max data size */
#endif
#ifndef BRKSIZ
#define	BRKSIZ		MAXDSIZ			/* heap gap size */
#endif
#ifndef	DFLSSIZ
#define	DFLSSIZ		(2*1024*1024)		/* initial stack size limit */
#endif
#ifndef	MAXSSIZ
#define	MAXSSIZ		(32*1024*1024UL)	/* max stack size */
#endif

#define	STACKGAP_RANDOM	256*1024

#ifndef USRIOSIZE
#define	USRIOSIZE	((2*HPPA_PGALIAS)/PAGE_SIZE)	/* 8mb */
#endif

/*
 * PTEs for system V style shared memory.
 * This is basically slop for kmempt which we actually allocate (malloc) from.
 */
#ifndef SHMMAXPGS
#define SHMMAXPGS	8192	/* 32mb */
#endif

/* user/kernel map constants */
#define	VM_MIN_ADDRESS		((vaddr_t)PAGE_SIZE)
#define	VM_MAXUSER_ADDRESS	((vaddr_t)0xc0000000)
#define	VM_MAX_ADDRESS		VM_MAXUSER_ADDRESS
#define	VM_MIN_KERNEL_ADDRESS	((vaddr_t)0xc0001000)
#define	VM_MAX_KERNEL_ADDRESS	((vaddr_t)0xef000000)

/* use a small range for PIE to minimize mmap pressure */
#define	VM_PIE_MIN_ADDR		PAGE_SIZE
#define	VM_PIE_MAX_ADDR		0x40000UL

/* virtual sizes (bytes) for various kernel submaps */
#define VM_PHYS_SIZE		(USRIOSIZE*PAGE_SIZE)

#define	VM_PHYSSEG_MAX		1	/* this many physmem segments */
#define	VM_PHYSSEG_STRAT	VM_PSTRAT_RANDOM

#define	VM_PHYSSEG_NOADD	/* XXX until uvm code is fixed */

#endif	/* _MACHINE_VMPARAM_H_ */
