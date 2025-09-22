/*	$OpenBSD: vmparam.h,v 1.25 2024/11/08 01:44:52 jsg Exp $	*/
/*	$NetBSD: vmparam.h,v 1.1 2003/04/26 18:39:49 fvdl Exp $	*/

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
 * Machine dependent constants for amd64.
 */

/*
 * USRSTACK is the top (end) of the user stack. Immediately above the
 * user stack resides the user structure, which is UPAGES long and contains
 * the kernel stack.
 *
 * Immediately after the user structure is the page table map, and then
 * kernel address space.
 */
#define	USRSTACK	VM_MAXUSER_ADDRESS

/*
 * Virtual memory related constants, all in bytes
 */
#define	MAXTSIZ		((paddr_t)256*1024*1024)	/* max text size */
#ifndef DFLDSIZ
#define	DFLDSIZ		((paddr_t)128*1024*1024)	/* initial data size limit */
#endif
#ifndef MAXDSIZ
#define	MAXDSIZ		((paddr_t)128*1024*1024*1024)	/* max data size */
#endif
#ifndef BRKSIZ
#define	BRKSIZ		((paddr_t)8*1024*1024*1024)	/* heap gap size */
#endif
#ifndef	DFLSSIZ
#define	DFLSSIZ		((paddr_t)2*1024*1024)		/* initial stack size limit */
#endif
#ifndef	MAXSSIZ
#define	MAXSSIZ		((paddr_t)32*1024*1024)		/* max stack size */
#endif

#define STACKGAP_RANDOM	256*1024

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
 * Mach derived constants
 */

/* user/kernel map constants */
#define VM_MIN_ADDRESS		PAGE_SIZE
#define VM_MAXUSER_ADDRESS	0x00007f7fffffc000
#define VM_MAX_ADDRESS		0x00007fbfdfeff000
#ifdef _KERNEL
#define VM_MIN_STACK_ADDRESS	0x0000600000000000
#endif
#define VM_MIN_KERNEL_ADDRESS	0xffff800000000000
#define VM_MAX_KERNEL_ADDRESS	0xffff800100000000

/* map PIE into approximately the first quarter of user va space */
#define VM_PIE_MIN_ADDR		VM_MIN_ADDRESS
#define VM_PIE_MAX_ADDR		0x200000000000

/* virtual sizes (bytes) for various kernel submaps */
#define VM_PHYS_SIZE		(USRIOSIZE*PAGE_SIZE)

#define VM_PHYSSEG_MAX		16	/* actually we could have this many segments */
#define VM_PHYSSEG_STRAT	VM_PSTRAT_BIGFIRST
#define VM_PHYSSEG_NOADD		/* can't add RAM after vm_mem_init */

#endif /* _MACHINE_VMPARAM_H_ */
