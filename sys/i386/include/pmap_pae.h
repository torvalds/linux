/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1991 Regents of the University of California.
 * All rights reserved.
 *
 * Copyright (c) 2018 The FreeBSD Foundation
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * the Systems Programming Group of the University of Utah Computer
 * Science Department and William Jolitz of UUNET Technologies Inc.
 *
 * Portions of this software were developed by
 * Konstantin Belousov <kib@FreeBSD.org> under sponsorship from
 * the FreeBSD Foundation.
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
 * Derived from hp300 version by Mike Hibler, this version by William
 * Jolitz uses a recursive map [a pde points to the page directory] to
 * map the page tables using the pagetables themselves. This is done to
 * reduce the impact on kernel virtual memory for lots of sparse address
 * space, and to reduce the cost of memory to each process.
 *
 *	from: hp300: @(#)pmap.h	7.2 (Berkeley) 12/16/90
 *	from: @(#)pmap.h	7.4 (Berkeley) 5/12/91
 * $FreeBSD$
 */

#ifndef _MACHINE_PMAP_PAE_H
#define	_MACHINE_PMAP_PAE_H

#define	NTRPPTD		2		/* Number of PTDs for trampoline
					   mapping */
#define	LOWPTDI		2		/* low memory map pde */
#define	KERNPTDI	4		/* start of kernel text pde */

#define NPGPTD		4		/* Num of pages for page directory */
#define NPGPTD_SHIFT	9
#undef	PDRSHIFT
#define	PDRSHIFT	PDRSHIFT_PAE
#undef	NBPDR
#define NBPDR		(1 << PDRSHIFT_PAE)	/* bytes/page dir */

#define	PG_FRAME	PG_FRAME_PAE
#define	PG_PS_FRAME	PG_PS_FRAME_PAE

/*
 * Size of Kernel address space.  This is the number of page table pages
 * (4MB each) to use for the kernel.  256 pages == 1 Gigabyte.
 * This **MUST** be a multiple of 4 (eg: 252, 256, 260, etc).
 * For PAE, the page table page unit size is 2MB.  This means that 512 pages
 * is 1 Gigabyte.  Double everything.  It must be a multiple of 8 for PAE.
 */
#define KVA_PAGES	(512*4)

/*
 * The initial number of kernel page table pages that are constructed
 * by pmap_cold() must be sufficient to map vm_page_array.  That number can
 * be calculated as follows:
 *     max_phys / PAGE_SIZE * sizeof(struct vm_page) / NBPDR
 * PAE:      max_phys 16G, sizeof(vm_page) 76, NBPDR 2M, 152 page table pages.
 * PAE_TABLES: max_phys 4G,  sizeof(vm_page) 68, NBPDR 2M, 36 page table pages.
 * Non-PAE:  max_phys 4G,  sizeof(vm_page) 68, NBPDR 4M, 18 page table pages.
 */
#ifndef NKPT
#define	NKPT		240
#endif

typedef uint64_t pdpt_entry_t;
typedef uint64_t pd_entry_t;
typedef uint64_t pt_entry_t;

#define	PTESHIFT	(3)
#define	PDESHIFT	(3)

#define	pde_cmpset(pdep, old, new)	atomic_cmpset_64_i586(pdep, old, new)
#define	pte_load_store(ptep, pte)	atomic_swap_64_i586(ptep, pte)
#define	pte_load_clear(ptep)		atomic_swap_64_i586(ptep, 0)
#define	pte_store(ptep, pte)		atomic_store_rel_64_i586(ptep, pte)
#define	pte_store_zero(ptep, pte)		\
do {						\
	uint32_t *p;				\
						\
	MPASS((*ptep & PG_V) == 0);		\
	p = (void *)ptep;			\
	*(p + 1) = (uint32_t)(pte >> 32);	\
	__compiler_membar();			\
	*p = (uint32_t)pte;			\
} while (0)
#define	pte_load(ptep)			atomic_load_acq_64_i586(ptep)

extern pdpt_entry_t *IdlePDPT;
extern pt_entry_t pg_nx;
extern pd_entry_t *IdlePTD_pae;	/* physical address of "Idle" state directory */

/*
 * KPTmap is a linear mapping of the kernel page table.  It differs from the
 * recursive mapping in two ways: (1) it only provides access to kernel page
 * table pages, and not user page table pages, and (2) it provides access to
 * a kernel page table page after the corresponding virtual addresses have
 * been promoted to a 2/4MB page mapping.
 *
 * KPTmap is first initialized by pmap_cold() to support just NPKT page table
 * pages.  Later, it is reinitialized by pmap_bootstrap() to allow for
 * expansion of the kernel page table.
 */
extern pt_entry_t *KPTmap_pae;

#endif
