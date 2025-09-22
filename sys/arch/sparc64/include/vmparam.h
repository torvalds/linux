/*	$OpenBSD: vmparam.h,v 1.35 2024/03/29 21:06:14 miod Exp $	*/
/*	$NetBSD: vmparam.h,v 1.18 2001/05/01 02:19:19 thorpej Exp $ */

/*
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This software was developed by the Computer Systems Engineering group
 * at Lawrence Berkeley Laboratory under DARPA contract BG 91-66 and
 * contributed to Berkeley.
 *
 * All advertising materials mentioning features or use of this software
 * must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Lawrence Berkeley Laboratory.
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
 *	@(#)vmparam.h	8.1 (Berkeley) 6/11/93
 */

/*
 * Machine dependent constants for sun4u and sun4v UltraSPARC
 */

#ifndef VMPARAM_H
#define VMPARAM_H

#define USRSTACK	0xffffffffffffe000L

/*
 * Virtual memory related constants, all in bytes
 */
/*
 * Since the compiler generates `call' instructions we can't
 * have more than 4GB in a single text segment.
 *
 * And since we only have a 40-bit address space, allow half
 * of that for data and the other half for stack.
 */
#ifndef MAXTSIZ
#define	MAXTSIZ		(1L*1024*1024*1024)	/* max text size */
#endif
#ifndef DFLDSIZ
#define	DFLDSIZ		(128L*1024*1024)	/* initial data size limit */
#endif
#ifndef MAXDSIZ
#define	MAXDSIZ		(8L*1024*1024*1024)	/* max data size */
#endif
#ifndef BRKSIZ
#define	BRKSIZ		MAXDSIZ			/* heap gap size */
#endif
#ifndef	DFLSSIZ
#define	DFLSSIZ		(2L*1024*1024)		/* initial stack size limit */
#endif
#ifndef	MAXSSIZ
#define	MAXSSIZ		(32L*1024*1024)		/* max stack size */
#endif

#define STACKGAP_RANDOM	256*1024

/*
 * Size of shared memory map
 */
#ifndef SHMMAXPGS
#define SHMMAXPGS	4096			/* 32mb */
#endif

/*
 * Size of User Raw I/O map
 */
#define USRIOSIZE 	300

/*
 * Mach derived constants
 */

/*
 * User/kernel map constants.
 */
#define VM_MIN_ADDRESS		((vaddr_t)PAGE_SIZE)
#define VM_MAX_ADDRESS		((vaddr_t)-1)
#define VM_MAXUSER_ADDRESS	((vaddr_t)-PAGE_SIZE)
#ifdef _KERNEL
#define VM_MIN_STACK_ADDRESS	((vaddr_t)0xfffffe0000000000L)
#endif

/* map PIE into the first quarter of the address space before hole */
#define VM_PIE_MIN_ADDR		PAGE_SIZE
#define VM_PIE_MAX_ADDR		0x10000000000

#define VM_MIN_KERNEL_ADDRESS	((vaddr_t)KERNBASE)
#define VM_MAX_KERNEL_ADDRESS	((vaddr_t)0x000007ffffffffffL)

/* virtual sizes (bytes) for various kernel submaps */
#define VM_PHYS_SIZE		(USRIOSIZE*PAGE_SIZE)

#define VM_PHYSSEG_MAX          32       /* up to 32 segments */
#define VM_PHYSSEG_STRAT        VM_PSTRAT_BSEARCH
#define VM_PHYSSEG_NOADD                /* can't add RAM after vm_mem_init */

#endif
