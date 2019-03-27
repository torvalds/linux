/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
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
 *	from: @(#)param.h	5.8 (Berkeley) 6/28/91
 * $FreeBSD$
 */

#ifndef _SPARC64_INCLUDE_PARAM_H_
#define	_SPARC64_INCLUDE_PARAM_H_

/*
 * Machine dependent constants for sparc64.
 */

#include <machine/_align.h>

#define __PCI_BAR_ZERO_VALID

#ifndef MACHINE
#define MACHINE		"sparc64"
#endif
#ifndef MACHINE_ARCH
#define	MACHINE_ARCH	"sparc64"
#endif
#define MID_MACHINE	MID_SPARC64

#if defined(SMP) || defined(KLD_MODULE)
#ifndef MAXCPU
#define MAXCPU		64
#endif
#else
#define MAXCPU		1
#endif /* SMP || KLD_MODULE */

#ifndef MAXMEMDOM
#define	MAXMEMDOM	1
#endif

#define	INT_SHIFT	2
#define	PTR_SHIFT	3

#define ALIGNBYTES	_ALIGNBYTES
#define ALIGN(p)	_ALIGN(p)
/*
 * ALIGNED_POINTER is a boolean macro that checks whether an address
 * is valid to fetch data elements of type t from on this architecture.
 * This does not reflect the optimal alignment, just the possibility
 * (within reasonable limits). 
 */
#define	ALIGNED_POINTER(p, t)	((((u_long)(p)) & (sizeof (t) - 1)) == 0)

/*
 * CACHE_LINE_SIZE is the compile-time maximum cache line size for an
 * architecture.  It should be used with appropriate caution.
 */
#define	CACHE_LINE_SHIFT	7
#define	CACHE_LINE_SIZE		(1 << CACHE_LINE_SHIFT)

#define	PAGE_SHIFT_8K	13
#define	PAGE_SIZE_8K	(1L<<PAGE_SHIFT_8K)
#define	PAGE_MASK_8K	(PAGE_SIZE_8K-1)

#define	PAGE_SHIFT_64K	16
#define	PAGE_SIZE_64K	(1L<<PAGE_SHIFT_64K)
#define	PAGE_MASK_64K	(PAGE_SIZE_64K-1)

#define	PAGE_SHIFT_512K	19
#define	PAGE_SIZE_512K	(1L<<PAGE_SHIFT_512K)
#define	PAGE_MASK_512K	(PAGE_SIZE_512K-1)

#define	PAGE_SHIFT_4M	22
#define	PAGE_SIZE_4M	(1L<<PAGE_SHIFT_4M)
#define	PAGE_MASK_4M	(PAGE_SIZE_4M-1)

#define	PAGE_SHIFT_32M	25
#define	PAGE_SIZE_32M	(1L<<PAGE_SHIFT_32M)
#define	PAGE_MASK_32M	(PAGE_SIZE_32M-1)

#define	PAGE_SHIFT_256M	28
#define	PAGE_SIZE_256M	(1L<<PAGE_SHIFT_256M)
#define	PAGE_MASK_256M	(PAGE_SIZE_256M-1)

#define PAGE_SHIFT_MIN	PAGE_SHIFT_8K
#define PAGE_SIZE_MIN	PAGE_SIZE_8K
#define PAGE_MASK_MIN	PAGE_MASK_8K
#define PAGE_SHIFT	PAGE_SHIFT_8K	/* LOG2(PAGE_SIZE) */
#define PAGE_SIZE	PAGE_SIZE_8K	/* bytes/page */
#define PAGE_MASK	PAGE_MASK_8K
#define PAGE_SHIFT_MAX	PAGE_SHIFT_4M
#define PAGE_SIZE_MAX	PAGE_SIZE_4M
#define PAGE_MASK_MAX	PAGE_MASK_4M

#define	MAXPAGESIZES	1		/* maximum number of supported page sizes */

#ifndef KSTACK_PAGES
#define KSTACK_PAGES		4	/* pages of kernel stack (with pcb) */
#endif
#define KSTACK_GUARD_PAGES	1	/* pages of kstack guard; 0 disables */
#define PCPU_PAGES		1

/*
 * Ceiling on size of buffer cache (really only effects write queueing,
 * the VM page cache is not effected), can be changed via
 * the kern.maxbcache /boot/loader.conf variable.
 */
#ifndef VM_BCACHE_SIZE_MAX
#define VM_BCACHE_SIZE_MAX      (400 * 1024 * 1024)
#endif

/*
 * Mach derived conversion macros
 */
#define round_page(x)		(((unsigned long)(x) + PAGE_MASK) & ~PAGE_MASK)
#define trunc_page(x)		((unsigned long)(x) & ~PAGE_MASK)

#define atop(x)			((unsigned long)(x) >> PAGE_SHIFT)
#define ptoa(x)			((unsigned long)(x) << PAGE_SHIFT)

#define sparc64_btop(x)		((unsigned long)(x) >> PAGE_SHIFT)
#define sparc64_ptob(x)		((unsigned long)(x) << PAGE_SHIFT)

#define	pgtok(x)		((unsigned long)(x) * (PAGE_SIZE / 1024))

#ifdef _KERNEL
#define	NO_FUEWORD	1
#endif

#endif /* !_SPARC64_INCLUDE_PARAM_H_ */
