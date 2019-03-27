/*	$NetBSD: vmparam.h,v 1.26 2003/08/07 16:27:47 agc Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1988 The Regents of the University of California.
 * All rights reserved.
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
 * $FreeBSD$
 */

#ifndef	_MACHINE_VMPARAM_H_
#define	_MACHINE_VMPARAM_H_

/*
 * Machine dependent constants for ARM.
 */

/*
 * Virtual memory related constants, all in bytes
 */
#ifndef	MAXTSIZ
#define	MAXTSIZ		(256UL*1024*1024)	/* max text size */
#endif
#ifndef	DFLDSIZ
#define	DFLDSIZ		(128UL*1024*1024)	/* initial data size limit */
#endif
#ifndef	MAXDSIZ
#define	MAXDSIZ		(512UL*1024*1024)	/* max data size */
#endif
#ifndef	DFLSSIZ
#define	DFLSSIZ		(2UL*1024*1024)		/* initial stack size limit */
#endif
#ifndef	MAXSSIZ
#define	MAXSSIZ		(8UL*1024*1024)		/* max stack size */
#endif
#ifndef	SGROWSIZ
#define	SGROWSIZ	(128UL*1024)		/* amount to grow stack */
#endif

/*
 * Address space constants
 */

/*
 * The line between user space and kernel space
 * Mappings >= KERNEL_BASE are constant across all processes
 */
#ifndef KERNBASE
#define	KERNBASE		0xc0000000
#endif

/*
 * The virtual address the kernel is linked to run at.  For armv4/5 platforms
 * the low-order 30 bits of this must match the low-order bits of the physical
 * address the kernel is loaded at, so the value is most often provided as a
 * kernel config option in the std.platform file. For armv6/7 the kernel can
 * be loaded at any 2MB boundary, and KERNVIRTADDR can also be set to any 2MB
 * boundary.  It is typically overridden in the std.platform file only when
 * KERNBASE is also set to a lower address to provide more KVA.
 */
#ifndef KERNVIRTADDR
#define	KERNVIRTADDR		0xc0000000
#endif

/*
 * max number of non-contig chunks of physical RAM you can have
 */

#define	VM_PHYSSEG_MAX		32

/*
 * The physical address space may be sparsely populated on some ARM systems.
 */
#define	VM_PHYSSEG_SPARSE

/*
 * Create one free page pool.  Since the ARM kernel virtual address
 * space does not include a mapping onto the machine's entire physical
 * memory, VM_FREEPOOL_DIRECT is defined as an alias for the default
 * pool, VM_FREEPOOL_DEFAULT.
 */
#define	VM_NFREEPOOL		1
#define	VM_FREEPOOL_DEFAULT	0
#define	VM_FREEPOOL_DIRECT	0

/*
 * We need just one free list:  DEFAULT.
 */
#define	VM_NFREELIST		1
#define	VM_FREELIST_DEFAULT	0

/*
 * The largest allocation size is 1MB.
 */
#define	VM_NFREEORDER		9

/*
 * Enable superpage reservations: 1 level.
 */
#ifndef	VM_NRESERVLEVEL
#define	VM_NRESERVLEVEL		1
#endif

/*
 * Level 0 reservations consist of 256 pages.
 */
#ifndef	VM_LEVEL_0_ORDER
#define	VM_LEVEL_0_ORDER	8
#endif

#define VM_MIN_ADDRESS          (0x00001000)
#ifndef VM_MAXUSER_ADDRESS
#define VM_MAXUSER_ADDRESS      (KERNBASE - 0x00400000) /* !!! PT2MAP_SIZE */
#endif
#define VM_MAX_ADDRESS          VM_MAXUSER_ADDRESS

#define	SHAREDPAGE		(VM_MAXUSER_ADDRESS - PAGE_SIZE)
#define	USRSTACK		SHAREDPAGE

/* initial pagein size of beginning of executable file */
#ifndef VM_INITIAL_PAGEIN
#define VM_INITIAL_PAGEIN       16
#endif

#ifndef VM_MIN_KERNEL_ADDRESS
#define VM_MIN_KERNEL_ADDRESS KERNBASE
#endif

#define	VM_MAX_KERNEL_ADDRESS	(vm_max_kernel_address)

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
#define	VM_KMEM_SIZE_MIN	(12 * 1024 * 1024)
#endif

/*
 * Optional ceiling (in bytes) on the size of the kmem arena: 40% of the
 * kernel map.
 */
#ifndef VM_KMEM_SIZE_MAX
#define	VM_KMEM_SIZE_MAX	((vm_max_kernel_address - \
    VM_MIN_KERNEL_ADDRESS + 1) * 2 / 5)
#endif

extern vm_offset_t vm_max_kernel_address;

#define	ZERO_REGION_SIZE	(64 * 1024)	/* 64KB */

#ifndef VM_MAX_AUTOTUNE_MAXUSERS
#define	VM_MAX_AUTOTUNE_MAXUSERS	384
#endif

#define	SFBUF
#define	SFBUF_MAP

#define	PMAP_HAS_DMAP	0
#define	PHYS_TO_DMAP(x)	({ panic("No direct map exists"); 0; })
#define	DMAP_TO_PHYS(x)	({ panic("No direct map exists"); 0; })

#define	DEVMAP_MAX_VADDR	ARM_VECTORS_HIGH

#endif	/* _MACHINE_VMPARAM_H_ */
