/*-
 * SPDX-License-Identifier: (BSD-3-Clause AND MIT-CMU)
 *
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * The Mach Operating System project at Carnegie-Mellon University.
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
 *	from: @(#)vm_init.c	8.1 (Berkeley) 6/11/93
 *
 *
 * Copyright (c) 1987, 1990 Carnegie-Mellon University.
 * All rights reserved.
 *
 * Authors: Avadis Tevanian, Jr., Michael Wayne Young
 *
 * Permission to use, copy, modify and distribute this software and
 * its documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND
 * FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 */

/*
 *	Initialize the Virtual Memory subsystem.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/domainset.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/proc.h>
#include <sys/rwlock.h>
#include <sys/malloc.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <sys/selinfo.h>
#include <sys/smp.h>
#include <sys/pipe.h>
#include <sys/bio.h>
#include <sys/buf.h>
#include <sys/vmem.h>
#include <sys/vmmeter.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/vm_kern.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_phys.h>
#include <vm/vm_pagequeue.h>
#include <vm/vm_map.h>
#include <vm/vm_pager.h>
#include <vm/vm_extern.h>

extern void	uma_startup1(void);
extern void	uma_startup2(void);
extern void	vm_radix_reserve_kva(void);

long physmem;

/*
 * System initialization
 */
static void vm_mem_init(void *);
SYSINIT(vm_mem, SI_SUB_VM, SI_ORDER_FIRST, vm_mem_init, NULL);

/*
 *	vm_init initializes the virtual memory system.
 *	This is done only by the first cpu up.
 *
 *	The start and end address of physical memory is passed in.
 */
static void
vm_mem_init(void *dummy)
{

	/*
	 * Initialize static domainsets, used by various allocators.
	 */
	domainset_init();

	/*
	 * Initialize resident memory structures.  From here on, all physical
	 * memory is accounted for, and we use only virtual addresses.
	 */
	vm_set_page_size();
	virtual_avail = vm_page_startup(virtual_avail);

	/*
	 * Set an initial domain policy for thread0 so that allocations
	 * can work.
	 */
	domainset_zero();

#ifdef	UMA_MD_SMALL_ALLOC
	/* Announce page availability to UMA. */
	uma_startup1();
#endif
	/*
	 * Initialize other VM packages
	 */
	vmem_startup();
	vm_object_init();
	vm_map_startup();
	kmem_init(virtual_avail, virtual_end);

#ifndef	UMA_MD_SMALL_ALLOC
	/* Set up radix zone to use noobj_alloc. */
	vm_radix_reserve_kva();
#endif
	/* Announce full page availability to UMA. */
	uma_startup2();
	kmem_init_zero_region();
	pmap_init();
	vm_pager_init();
}

void
vm_ksubmap_init(struct kva_md_info *kmi)
{
	vm_offset_t firstaddr;
	caddr_t v;
	vm_size_t size = 0;
	long physmem_est;
	vm_offset_t minaddr;
	vm_offset_t maxaddr;

	/*
	 * Allocate space for system data structures.
	 * The first available kernel virtual address is in "v".
	 * As pages of kernel virtual memory are allocated, "v" is incremented.
	 * As pages of memory are allocated and cleared,
	 * "firstaddr" is incremented.
	 */

	/*
	 * Make two passes.  The first pass calculates how much memory is
	 * needed and allocates it.  The second pass assigns virtual
	 * addresses to the various data structures.
	 */
	firstaddr = 0;
again:
	v = (caddr_t)firstaddr;

	/*
	 * Discount the physical memory larger than the size of kernel_map
	 * to avoid eating up all of KVA space.
	 */
	physmem_est = lmin(physmem, btoc(vm_map_max(kernel_map) -
	    vm_map_min(kernel_map)));

	v = kern_vfs_bio_buffer_alloc(v, physmem_est);

	/*
	 * End of first pass, size has been calculated so allocate memory
	 */
	if (firstaddr == 0) {
		size = (vm_size_t)v;
#ifdef VM_FREELIST_DMA32
		/*
		 * Try to protect 32-bit DMAable memory from the largest
		 * early alloc of wired mem.
		 */
		firstaddr = kmem_alloc_attr(size, M_ZERO | M_NOWAIT,
		    (vm_paddr_t)1 << 32, ~(vm_paddr_t)0, VM_MEMATTR_DEFAULT);
		if (firstaddr == 0)
#endif
			firstaddr = kmem_malloc(size, M_ZERO | M_WAITOK);
		if (firstaddr == 0)
			panic("startup: no room for tables");
		goto again;
	}

	/*
	 * End of second pass, addresses have been assigned
	 */
	if ((vm_size_t)((char *)v - firstaddr) != size)
		panic("startup: table size inconsistency");

	/*
	 * Allocate the clean map to hold all of I/O virtual memory.
	 */
	size = (long)nbuf * BKVASIZE + (long)bio_transient_maxcnt * MAXPHYS;
	kmi->clean_sva = firstaddr = kva_alloc(size);
	kmi->clean_eva = firstaddr + size;

	/*
	 * Allocate the buffer arena.
	 *
	 * Enable the quantum cache if we have more than 4 cpus.  This
	 * avoids lock contention at the expense of some fragmentation.
	 */
	size = (long)nbuf * BKVASIZE;
	kmi->buffer_sva = firstaddr;
	kmi->buffer_eva = kmi->buffer_sva + size;
	vmem_init(buffer_arena, "buffer arena", kmi->buffer_sva, size,
	    PAGE_SIZE, (mp_ncpus > 4) ? BKVASIZE * 8 : 0, 0);
	firstaddr += size;

	/*
	 * And optionally transient bio space.
	 */
	if (bio_transient_maxcnt != 0) {
		size = (long)bio_transient_maxcnt * MAXPHYS;
		vmem_init(transient_arena, "transient arena",
		    firstaddr, size, PAGE_SIZE, 0, 0);
		firstaddr += size;
	}
	if (firstaddr != kmi->clean_eva)
		panic("Clean map calculation incorrect");

	/*
	 * Allocate the pageable submaps.  We may cache an exec map entry per
	 * CPU, so we therefore need to reserve space for at least ncpu+1
	 * entries to avoid deadlock.  The exec map is also used by some image
	 * activators, so we leave a fixed number of pages for their use.
	 */
#ifdef __LP64__
	exec_map_entries = 8 * mp_ncpus;
#else
	exec_map_entries = 2 * mp_ncpus + 4;
#endif
	exec_map_entry_size = round_page(PATH_MAX + ARG_MAX);
	exec_map = kmem_suballoc(kernel_map, &minaddr, &maxaddr,
	    exec_map_entries * exec_map_entry_size + 64 * PAGE_SIZE, FALSE);
	pipe_map = kmem_suballoc(kernel_map, &minaddr, &maxaddr, maxpipekva,
	    FALSE);
}
