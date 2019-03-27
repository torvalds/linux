/*-
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
#ifndef DFLSSIZ
#define	DFLSSIZ		(128*1024*1024)		/* initial stack size limit */
#endif
#ifndef MAXSSIZ
#define	MAXSSIZ		(1*1024*1024*1024)	/* max stack size */
#endif
#ifndef SGROWSIZ
#define	SGROWSIZ	(128*1024)		/* amount to grow stack */
#endif

/*
 * The physical address space is sparsely populated.
 */
#define	VM_PHYSSEG_SPARSE

/*
 * The number of PHYSSEG entries.
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
 * RISC-V implements multiple paging modes with different virtual address space
 * sizes: SV32, SV39 and SV48.  SV39 permits a virtual address space size of
 * 512GB and uses a three-level page table.  Since this is large enough for most
 * purposes, we currently use SV39 for both userland and the kernel, avoiding
 * the extra translation step required by SV48.
 *
 * The address space is split into two regions at each end of the 64-bit address
 * space:
 *
 * 0x0000000000000000 - 0x0000003fffffffff    256GB user map
 * 0x0000004000000000 - 0xffffffbfffffffff    unmappable
 * 0xffffffc000000000 - 0xffffffc7ffffffff    32GB kernel map
 * 0xffffffc800000000 - 0xffffffcfffffffff    32GB unused
 * 0xffffffd000000000 - 0xffffffefffffffff    128GB direct map
 * 0xfffffff000000000 - 0xffffffffffffffff    64GB unused
 *
 * The kernel is loaded at the beginning of the kernel map.
 *
 * We define some interesting address constants:
 *
 * VM_MIN_ADDRESS and VM_MAX_ADDRESS define the start and end of the entire
 * 64 bit address space, mostly just for convenience.
 *
 * VM_MIN_KERNEL_ADDRESS and VM_MAX_KERNEL_ADDRESS define the start and end of
 * mappable kernel virtual address space.
 *
 * VM_MIN_USER_ADDRESS and VM_MAX_USER_ADDRESS define the start and end of the
 * user address space.
 */
#define	VM_MIN_ADDRESS		(0x0000000000000000UL)
#define	VM_MAX_ADDRESS		(0xffffffffffffffffUL)

#define	VM_MIN_KERNEL_ADDRESS	(0xffffffc000000000UL)
#define	VM_MAX_KERNEL_ADDRESS	(0xffffffc800000000UL)

#define	DMAP_MIN_ADDRESS	(0xffffffd000000000UL)
#define	DMAP_MAX_ADDRESS	(0xfffffff000000000UL)

#define	DMAP_MIN_PHYSADDR	(dmap_phys_base)
#define	DMAP_MAX_PHYSADDR	(dmap_phys_max)

/* True if pa is in the dmap range */
#define	PHYS_IN_DMAP(pa)	((pa) >= DMAP_MIN_PHYSADDR && \
    (pa) < DMAP_MAX_PHYSADDR)
/* True if va is in the dmap range */
#define	VIRT_IN_DMAP(va)	((va) >= DMAP_MIN_ADDRESS && \
    (va) < (dmap_max_addr))

#define	PMAP_HAS_DMAP	1
#define	PHYS_TO_DMAP(pa)						\
({									\
	KASSERT(PHYS_IN_DMAP(pa),					\
	    ("%s: PA out of range, PA: 0x%lx", __func__,		\
	    (vm_paddr_t)(pa)));						\
	((pa) - dmap_phys_base) + DMAP_MIN_ADDRESS;			\
})

#define	DMAP_TO_PHYS(va)						\
({									\
	KASSERT(VIRT_IN_DMAP(va),					\
	    ("%s: VA out of range, VA: 0x%lx", __func__,		\
	    (vm_offset_t)(va)));					\
	((va) - DMAP_MIN_ADDRESS) + dmap_phys_base;			\
})

#define	VM_MIN_USER_ADDRESS	(0x0000000000000000UL)
#define	VM_MAX_USER_ADDRESS	(0x0000004000000000UL)

#define	VM_MINUSER_ADDRESS	(VM_MIN_USER_ADDRESS)
#define	VM_MAXUSER_ADDRESS	(VM_MAX_USER_ADDRESS)

#define	KERNBASE		(VM_MIN_KERNEL_ADDRESS)
#define	SHAREDPAGE		(VM_MAXUSER_ADDRESS - PAGE_SIZE)
#define	USRSTACK		SHAREDPAGE

#define	KERNENTRY		(0)

/*
 * How many physical pages per kmem arena virtual page.
 */
#ifndef VM_KMEM_SIZE_SCALE
#define	VM_KMEM_SIZE_SCALE	(3)
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

#ifndef LOCORE
extern vm_paddr_t dmap_phys_base;
extern vm_paddr_t dmap_phys_max;
extern vm_offset_t dmap_max_addr;
extern u_int tsb_kernel_ldd_phys;
extern vm_offset_t vm_max_kernel_address;
extern vm_offset_t init_pt_va;
#endif

#define	ZERO_REGION_SIZE	(64 * 1024)	/* 64KB */

#define	DEVMAP_MAX_VADDR	VM_MAX_KERNEL_ADDRESS

#endif /* !_MACHINE_VMPARAM_H_ */
