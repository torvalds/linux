/*-
 * SPDX-License-Identifier: BSD-3-Clause
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
 *	from: @(#)param.h	5.8 (Berkeley) 6/28/91
 * $FreeBSD$
 */


#ifndef _I386_INCLUDE_PARAM_H_
#define	_I386_INCLUDE_PARAM_H_

#include <machine/_align.h>

/*
 * Machine dependent constants for Intel 386.
 */


#define __HAVE_ACPI
#define	__HAVE_PIR
#define __PCI_REROUTE_INTERRUPT

#ifndef MACHINE
#define MACHINE		"i386"
#endif
#ifndef MACHINE_ARCH
#define	MACHINE_ARCH	"i386"
#endif
#define MID_MACHINE	MID_I386

#if defined(SMP) || defined(KLD_MODULE)
#ifndef MAXCPU
#define MAXCPU		32
#endif
#else
#define MAXCPU		1
#endif /* SMP || KLD_MODULE */

#ifndef MAXMEMDOM
#define	MAXMEMDOM	1
#endif

#define ALIGNBYTES	_ALIGNBYTES
#define ALIGN(p)	_ALIGN(p)
/*
 * ALIGNED_POINTER is a boolean macro that checks whether an address
 * is valid to fetch data elements of type t from on this architecture.
 * This does not reflect the optimal alignment, just the possibility
 * (within reasonable limits). 
 */
#define	ALIGNED_POINTER(p, t)	1

/*
 * CACHE_LINE_SIZE is the compile-time maximum cache line size for an
 * architecture.  It should be used with appropriate caution.
 */
#define	CACHE_LINE_SHIFT	6
#define	CACHE_LINE_SIZE		(1 << CACHE_LINE_SHIFT)

#define PAGE_SHIFT	12		/* LOG2(PAGE_SIZE) */
#define PAGE_SIZE	(1 << PAGE_SHIFT)	/* bytes/page */
#define PAGE_MASK	(PAGE_SIZE - 1)
#define NPTEPG		(PAGE_SIZE / sizeof(pt_entry_t))

/* Size in bytes of the page directory */
#define NBPTD		(NPGPTD << PAGE_SHIFT)
/* Number of PDEs in page directory, 2048 for PAE, 1024 for non-PAE */
#define NPDEPTD		(NBPTD / sizeof(pd_entry_t))
/* Number of PDEs in one page of the page directory, 512 vs. 1024 */
#define NPDEPG		(PAGE_SIZE / sizeof(pd_entry_t))
#define PDRMASK		(NBPDR - 1)
#ifndef PDRSHIFT
#define	PDRSHIFT	i386_pmap_PDRSHIFT
#endif
#ifndef NBPDR
#define NBPDR		(1 << PDRSHIFT)	/* bytes/page dir */
#endif

#define	MAXPAGESIZES	2	/* maximum number of supported page sizes */

#define IOPAGES	2		/* pages of i/o permission bitmap */

#ifndef KSTACK_PAGES
#define KSTACK_PAGES 4		/* Includes pcb! */
#endif
#define KSTACK_GUARD_PAGES 1	/* pages of kstack guard; 0 disables */
#if KSTACK_PAGES < 4
#define	TD0_KSTACK_PAGES 4
#else
#define	TD0_KSTACK_PAGES KSTACK_PAGES
#endif

/*
 * Ceiling on amount of swblock kva space, can be changed via
 * the kern.maxswzone /boot/loader.conf variable.
 *
 * 276 is sizeof(struct swblock), but we do not always have a definition
 * in scope for struct swblock, so we have to hardcode it.  Each struct
 * swblock holds metadata for 32 pages, so in theory, this is enough for
 * 16 GB of swap.  In practice, however, the usable amount is considerably
 * lower due to fragmentation.
 */
#ifndef VM_SWZONE_SIZE_MAX
#define VM_SWZONE_SIZE_MAX	(276 * 128 * 1024)
#endif

/*
 * Ceiling on size of buffer cache (really only effects write queueing,
 * the VM page cache is not effected), can be changed via
 * the kern.maxbcache /boot/loader.conf variable.
 *
 * The value is equal to the size of the auto-tuned buffer map for
 * the machine with 4GB of RAM, see vfs_bio.c:kern_vfs_bio_buffer_alloc().
 */
#ifndef VM_BCACHE_SIZE_MAX
#define VM_BCACHE_SIZE_MAX	(7224 * 16 * 1024)
#endif

/*
 * Mach derived conversion macros
 */
#define trunc_page(x)		((x) & ~PAGE_MASK)
#define round_page(x)		(((x) + PAGE_MASK) & ~PAGE_MASK)
#define trunc_4mpage(x)		((x) & ~PDRMASK)
#define round_4mpage(x)		((((x)) + PDRMASK) & ~PDRMASK)

#define atop(x)			((x) >> PAGE_SHIFT)
#define ptoa(x)			((x) << PAGE_SHIFT)

#define i386_btop(x)		((x) >> PAGE_SHIFT)
#define i386_ptob(x)		((x) << PAGE_SHIFT)

#define	pgtok(x)		((x) * (PAGE_SIZE / 1024))

#define INKERNEL(va)		(TRUE)

#endif /* !_I386_INCLUDE_PARAM_H_ */
