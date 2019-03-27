/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 * Copyright (c) 1994 John S. Dyson
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
 *	from: @(#)vmparam.h     5.9 (Berkeley) 5/12/91
 *	from: FreeBSD: src/sys/i386/include/vmparam.h,v 1.33 2000/03/30
 * $FreeBSD$
 */

#ifndef	_MACHINE_VMPARAM_H_
#define	_MACHINE_VMPARAM_H_

/*
 * Virtual memory related constants, all in bytes
 */
#ifndef MAXTSIZ
#define	MAXTSIZ		(1*1024*1024*1024)	/* max text size */
#endif
#ifndef DFLDSIZ
#define	DFLDSIZ		(128*1024*1024)		/* initial data size limit */
#endif
#ifndef MAXDSIZ
#define	MAXDSIZ		(1*1024*1024*1024)	/* max data size */
#endif
#ifndef	DFLSSIZ
#define	DFLSSIZ		(128*1024*1024)		/* initial stack size limit */
#endif
#ifndef	MAXSSIZ
#define	MAXSSIZ		(1*1024*1024*1024)	/* max stack size */
#endif
#ifndef	SGROWSIZ
#define	SGROWSIZ	(128*1024)		/* amount to grow stack */
#endif

/*
 * The physical address space is sparsely populated.
 */
#define	VM_PHYSSEG_SPARSE

/*
 * The number of PHYSSEG entries must be one greater than the number
 * of phys_avail entries because the phys_avail entry that spans the
 * largest physical address that is accessible by ISA DMA is split
 * into two PHYSSEG entries.
 */
#define	VM_PHYSSEG_MAX		64

/*
 * Create two free page pools: VM_FREEPOOL_DEFAULT is the default pool
 * from which physical pages are allocated and VM_FREEPOOL_DIRECT is
 * the pool from which physical pages for small UMA objects are
 * allocated.
 */
#define	VM_NFREEPOOL		2
#define	VM_FREEPOOL_DEFAULT	0
#define	VM_FREEPOOL_DIRECT	1

/*
 * Create one free page list: VM_FREELIST_DEFAULT is for all physical
 * pages.
 */
#define	VM_NFREELIST		1
#define	VM_FREELIST_DEFAULT	0

/*
 * An allocation size of 16MB is supported in order to optimize the
 * use of the direct map by UMA.  Specifically, a cache line contains
 * at most four TTEs, collectively mapping 16MB of physical memory.
 * By reducing the number of distinct 16MB "pages" that are used by UMA,
 * the physical memory allocator reduces the likelihood of both 4MB
 * page TLB misses and cache misses caused by 4MB page TLB misses.
 */
#define	VM_NFREEORDER		12

/*
 * Enable superpage reservations: 1 level.
 */
#ifndef	VM_NRESERVLEVEL
#define	VM_NRESERVLEVEL		1
#endif

/*
 * Level 0 reservations consist of 512 pages.
 */
#ifndef	VM_LEVEL_0_ORDER
#define	VM_LEVEL_0_ORDER	9
#endif

/**
 * Address space layout.
 *
 * UltraSPARC I and II implement a 44 bit virtual address space.  The address
 * space is split into 2 regions at each end of the 64 bit address space, with
 * an out of range "hole" in the middle.  UltraSPARC III implements the full
 * 64 bit virtual address space, but we don't really have any use for it and
 * 43 bits of user address space is considered to be "enough", so we ignore it.
 *
 * Upper region:	0xffffffffffffffff
 *			0xfffff80000000000
 *
 * Hole:		0xfffff7ffffffffff
 *			0x0000080000000000
 *
 * Lower region:	0x000007ffffffffff
 *			0x0000000000000000
 *
 * In general we ignore the upper region, and use the lower region as mappable
 * space.
 *
 * We define some interesting address constants:
 *
 * VM_MIN_ADDRESS and VM_MAX_ADDRESS define the start and end of the entire
 * 64 bit address space, mostly just for convenience.
 *
 * VM_MIN_DIRECT_ADDRESS and VM_MAX_DIRECT_ADDRESS define the start and end
 * of the direct mapped region.  This maps virtual addresses to physical
 * addresses directly using 4mb tlb entries, with the physical address encoded
 * in the lower 43 bits of virtual address.  These mappings are convenient
 * because they do not require page tables, and because they never change they
 * do not require tlb flushes.  However, since these mappings are cacheable,
 * we must ensure that all pages accessed this way are either not double
 * mapped, or that all other mappings have virtual color equal to physical
 * color, in order to avoid creating illegal aliases in the data cache.
 *
 * VM_MIN_KERNEL_ADDRESS and VM_MAX_KERNEL_ADDRESS define the start and end of
 * mappable kernel virtual address space.  VM_MIN_KERNEL_ADDRESS is basically
 * arbitrary, a convenient address is chosen which allows both the kernel text
 * and data and the prom's address space to be mapped with 1 4mb tsb page.
 * VM_MAX_KERNEL_ADDRESS is variable, computed at startup time based on the
 * amount of physical memory available.  Each 4mb tsb page provides 1g of
 * virtual address space, with the only practical limit being available
 * phsyical memory.
 *
 * VM_MIN_PROM_ADDRESS and VM_MAX_PROM_ADDRESS define the start and end of the
 * prom address space.  On startup the prom's mappings are duplicated in the
 * kernel tsb, to allow prom memory to be accessed normally by the kernel.
 *
 * VM_MIN_USER_ADDRESS and VM_MAX_USER_ADDRESS define the start and end of the
 * user address space.  There are some hardware errata about using addresses
 * at the boundary of the va hole, so we allow just under 43 bits of user
 * address space.  Note that the kernel and user address spaces overlap, but
 * this doesn't matter because they use different tlb contexts, and because
 * the kernel address space is not mapped into each process' address space.
 */
#define	VM_MIN_ADDRESS		(0x0000000000000000UL)
#define	VM_MAX_ADDRESS		(0xffffffffffffffffUL)

#define	VM_MIN_DIRECT_ADDRESS	(0xfffff80000000000UL)
#define	VM_MAX_DIRECT_ADDRESS	(VM_MAX_ADDRESS)

#define	VM_MIN_KERNEL_ADDRESS	(0x00000000c0000000UL)
#define	VM_MAX_KERNEL_ADDRESS	(vm_max_kernel_address)

#define	VM_MIN_PROM_ADDRESS	(0x00000000f0000000UL)
#define	VM_MAX_PROM_ADDRESS	(0x00000000ffffffffUL)

#define	VM_MIN_USER_ADDRESS	(0x0000000000000000UL)
#define	VM_MAX_USER_ADDRESS	(0x000007fe00000000UL)

#define	VM_MINUSER_ADDRESS	(VM_MIN_USER_ADDRESS)
#define	VM_MAXUSER_ADDRESS	(VM_MAX_USER_ADDRESS)

#define	KERNBASE		(VM_MIN_KERNEL_ADDRESS)
#define	PROMBASE		(VM_MIN_PROM_ADDRESS)
#define	USRSTACK		(VM_MAX_USER_ADDRESS)

/*
 * How many physical pages per kmem arena virtual page.
 */
#ifndef VM_KMEM_SIZE_SCALE
#define	VM_KMEM_SIZE_SCALE	(tsb_kernel_ldd_phys == 0 ? 3 : 2)
#endif

/*
 * Optional floor (in bytes) on the size of the kmem arena.
 */
#ifndef VM_KMEM_SIZE_MIN
#define	VM_KMEM_SIZE_MIN	(16 * 1024 * 1024)
#endif

/*
 * Optional ceiling (in bytes) on the size of the kmem arena: 60% of the
 * kernel map.
 */
#ifndef VM_KMEM_SIZE_MAX
#define	VM_KMEM_SIZE_MAX	((VM_MAX_KERNEL_ADDRESS - \
    VM_MIN_KERNEL_ADDRESS + 1) * 3 / 5)
#endif

/*
 * Initial pagein size of beginning of executable file.
 */
#ifndef	VM_INITIAL_PAGEIN
#define	VM_INITIAL_PAGEIN	16
#endif

#define	UMA_MD_SMALL_ALLOC

extern u_int tsb_kernel_ldd_phys;
extern vm_offset_t vm_max_kernel_address;

/*
 * Older sparc64 machines have a virtually indexed L1 data cache of 16KB.
 * Consequently, mapping the same physical page multiple times may have
 * caching disabled.
 */
#define	ZERO_REGION_SIZE	PAGE_SIZE

#include <machine/tlb.h>

#define	SFBUF
#define	SFBUF_MAP

#define	PMAP_HAS_DMAP	dcache_color_ignore
#define	PHYS_TO_DMAP(x)	(TLB_PHYS_TO_DIRECT(x))

#endif /* !_MACHINE_VMPARAM_H_ */
