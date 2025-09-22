/*	$OpenBSD: vmparam.h,v 1.57 2021/03/16 18:04:16 kurt Exp $	*/
/*	$NetBSD: vmparam.h,v 1.15 1994/10/27 04:16:34 cgd Exp $	*/

/*-
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * William Jolitz.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)vmparam.h	5.9 (Berkeley) 5/12/91
 */

#ifndef _MACHINE_VMPARAM_H_
#define _MACHINE_VMPARAM_H_
/*
 * Machine dependent constants for 386.
 */

#define	USRSTACK	VM_MAXUSER_ADDRESS

/*
 * Virtual memory related constants, all in bytes
 */
#define	MAXTSIZ		(256*1024*1024)		/* max text size */
#ifndef DFLDSIZ
#define	DFLDSIZ		(64*1024*1024)		/* initial data size limit */
#endif
#ifndef MAXDSIZ
#define	MAXDSIZ		(3UL*1024*1024*1024)	/* max data size */
#endif
#ifndef BRKSIZ
#define	BRKSIZ		(128*1024*1024)		/* heap gap size */
#endif
#ifndef	DFLSSIZ
#define	DFLSSIZ		(4*1024*1024)		/* initial stack size limit */
#endif
#ifndef	MAXSSIZ
#define	MAXSSIZ		(32*1024*1024)		/* max stack size */
#endif

#define STACKGAP_RANDOM	256*1024

/* I386 has a line where all code is executable: 0 - I386_MAX_EXE_ADDR */
#define I386_MAX_EXE_ADDR 0x20000000		/* exec line */

/* map PIE into 320MB - 448MB address range */
#define VM_PIE_MIN_ADDR 0x14000000
#define VM_PIE_MAX_ADDR 0x1C000000

/*
 * Size of shared memory map
 */
#ifndef SHMMAXPGS
#define SHMMAXPGS	8192
#endif

/*
 * Size of User Raw I/O map
 */
#define	USRIOSIZE 	300

/*
 * Specific addresses being unmapped and used as fillers for free memory.
 */
#define	DEADBEEF0	0xefffeecc	/* malloc's filler */
#define	DEADBEEF1	0xefffaabb	/* pool's filler */

/* user/kernel map constants */
#define VM_MIN_ADDRESS		((vaddr_t)PAGE_SIZE)
#define VM_MAXUSER_ADDRESS	((vaddr_t)((PDSLOT_PTE<<PDSHIFT) - (2 * PAGE_SIZE)))
#define VM_MAX_ADDRESS		((vaddr_t)((PDSLOT_PTE<<PDSHIFT) + \
				    (PDSLOT_PTE<<PAGE_SHIFT)))
#define VM_MIN_KERNEL_ADDRESS	((vaddr_t)KERNBASE)
#define VM_MAX_KERNEL_ADDRESS	((vaddr_t)(PDSLOT_APTE<<PDSHIFT))

/* virtual sizes (bytes) for various kernel submaps */
#define VM_PHYS_SIZE		(USRIOSIZE*PAGE_SIZE)

#define	VM_PHYSSEG_MAX	16	/* actually we could have this many segments */
#define	VM_PHYSSEG_STRAT	VM_PSTRAT_BSEARCH
#define	VM_PHYSSEG_NOADD	/* can't add RAM after vm_mem_init */

#endif /* _MACHINE_VMPARAM_H_ */
