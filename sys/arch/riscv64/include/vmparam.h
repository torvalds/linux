/*	$OpenBSD: vmparam.h,v 1.9 2023/11/28 09:10:18 jsg Exp $	*/

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
 * Machine dependent constants for riscv64.
 */

#define	USRSTACK	VM_MAXUSER_ADDRESS

/*
 * Virtual memory related constants, all in bytes
 */
#ifndef MAXTSIZ
#define	MAXTSIZ		((paddr_t)1*1024*1024*1024)	/* max text size */
#endif
#ifndef DFLDSIZ
#define	DFLDSIZ		((paddr_t)128*1024*1024)	/* initial data size limit */
#endif
#ifndef MAXDSIZ
#define	MAXDSIZ		((paddr_t)16*1024*1024*1024)	/* max data size */
#endif
#ifndef BRKSIZ
#define	BRKSIZ		((paddr_t)1*1024*1024*1024)	/* heap gap size */
#endif
#ifndef	DFLSSIZ
#define	DFLSSIZ		((paddr_t)128*1024*1024)	/* initial stack size limit */
#endif
#ifndef	MAXSSIZ
#define	MAXSSIZ		((paddr_t)1*1024*1024*1024)	/* max stack size */
#endif

#define	STACKGAP_RANDOM	256*1024

/*
 * Size of shared memory map
 */
#ifndef	SHMMAXPGS
#define	SHMMAXPGS	1024
#endif

/*
 * Size of User Raw I/O map
 */
#define	USRIOSIZE	300

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
 * VM_MIN_ADDRESS and VM_MAXUSER_ADDRESS define the start and end of the
 * user address space.
 */
#define	VM_MIN_ADDRESS		((vaddr_t)PAGE_SIZE)
#define	VM_MAX_ADDRESS		(0xffffffffffffffffUL)

#define	VM_MIN_KERNEL_ADDRESS	(0xffffffc000000000UL)
#define	VM_MAX_KERNEL_ADDRESS	(0xffffffc800000000UL)

/* Kernel L1 Page Table Range */
#define	L1_KERN_BASE		(256)
#define	L1_KERN_ENTRIES		(288 - L1_KERN_BASE)

#define	DMAP_MIN_ADDRESS	(0xffffffd000000000UL)
#define	DMAP_MAX_ADDRESS	(0xfffffff000000000UL)

/* DMAP L1 Page Table Range */
#define	L1_DMAP_BASE		(320)
#define	L1_DMAP_ENTRIES		(448 - L1_DMAP_BASE)

#define	VM_MAXUSER_ADDRESS	(0x0000004000000000UL)  /* 39 bits */

#ifdef _KERNEL
#define	VM_MIN_STACK_ADDRESS	(VM_MAXUSER_ADDRESS * 3 / 4)
#endif

#define	KERNBASE		(VM_MIN_KERNEL_ADDRESS)

#ifndef _LOCORE
extern paddr_t dmap_phys_base;
#endif

/* virtual sizes (bytes) for various kernel submaps */
#define	VM_PHYS_SIZE		(USRIOSIZE*PAGE_SIZE)

#define	VM_PHYSSEG_MAX		32
#define	VM_PHYSSEG_STRAT	VM_PSTRAT_BSEARCH
#define	VM_PHYSSEG_NOADD	/* can't add RAM after vm_mem_init */

#endif /* _MACHINE_VMPARAM_H_ */
