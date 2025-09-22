/*	$OpenBSD: uvm_init.c,v 1.43 2025/04/16 09:16:48 mpi Exp $	*/
/*	$NetBSD: uvm_init.c,v 1.14 2000/06/27 17:29:23 mrg Exp $	*/

/*
 * Copyright (c) 1997 Charles D. Cranor and Washington University.
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * from: Id: uvm_init.c,v 1.1.2.3 1998/02/06 05:15:27 chs Exp
 */

/*
 * uvm_init.c: init the vm system.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/filedesc.h>
#include <sys/percpu.h>
#include <sys/resourcevar.h>
#include <sys/mman.h>
#include <sys/malloc.h>
#include <sys/vnode.h>
#include <sys/pool.h>

#include <uvm/uvm.h>
#include <uvm/uvm_addr.h>

/*
 * struct uvm: we store all global vars in this structure to make them
 * easier to spot...
 */

struct uvm uvm;		/* decl */
struct uvmexp uvmexp;	/* decl */

COUNTERS_BOOT_MEMORY(uvmexp_countersboot, exp_ncounters);
struct cpumem *uvmexp_counters = COUNTERS_BOOT_INITIALIZER(uvmexp_countersboot);

#if defined(VM_MIN_KERNEL_ADDRESS)
vaddr_t vm_min_kernel_address = VM_MIN_KERNEL_ADDRESS;
#else
vaddr_t vm_min_kernel_address;
#endif

/*
 * local prototypes
 */

/*
 * uvm_init: init the VM system.   called from kern/init_main.c.
 */

void
uvm_init(void)
{
	vaddr_t kvm_start, kvm_end;

	/*
	 * Ensure that the hardware set the page size.
	 */
	if (uvmexp.pagesize == 0) {
		panic("uvm_init: page size not set");
	}
	averunnable.fscale = FSCALE;

	/*
	 * Init the page sub-system.  This includes allocating the vm_page
	 * structures, and setting up all the page queues (and locks).
	 * Available memory will be put in the "free" queue, kvm_start and
	 * kvm_end will be set to the area of kernel virtual memory which
	 * is available for general use.
	 */
	uvm_page_init(&kvm_start, &kvm_end);

	/*
	 * Init the map sub-system.
	 *
	 * Allocates the static pool of vm_map_entry structures that are
	 * used for "special" kernel maps (e.g. kernel_map, kmem_map, etc...).
	 */
	uvm_map_init();

	/*
	 * Setup the kernel's virtual memory data structures.  This includes
	 * setting up the kernel_map/kernel_object.
	 */
	uvm_km_init(vm_min_kernel_address, kvm_start, kvm_end);

	/*
	 * step 4.5: init (tune) the fault recovery code.
	 */
	uvmfault_init();

	/*
	 * Init the pmap module.  The pmap module is free to allocate
	 * memory for its private use (e.g. pvlists).
	 */
	pmap_init();

	/*
	 * step 6: init uvm_km_page allocator memory.
	 */
	uvm_km_page_init();

	/*
	 * Make kernel memory allocators ready for use.
	 * After this call the malloc memory allocator can be used.
	 */
	kmeminit();

	/*
	 * step 7.5: init the dma allocator, which is backed by pools.
	 */
	dma_alloc_init();

	/*
	 * Init all pagers and the pager_map.
	 */
	uvm_pager_init();

	/*
	 * step 9: init anonymous memory system
	 */
	amap_init();

	/*
	 * step 10: start uvm_km_page allocator thread.
	 */
	uvm_km_page_lateinit();

	/*
	 * the VM system is now up!  now that malloc is up we can
	 * enable paging of kernel objects.
	 */
	uao_create(VM_KERNEL_SPACE_SIZE, UAO_FLAG_KERNSWAP);

	/*
	 * reserve some unmapped space for malloc/pool use after free usage
	 */
#ifdef DEADBEEF0
	kvm_start = trunc_page(DEADBEEF0) - PAGE_SIZE;
	if (uvm_map(kernel_map, &kvm_start, 3 * PAGE_SIZE,
	    NULL, UVM_UNKNOWN_OFFSET, 0, UVM_MAPFLAG(PROT_NONE,
	    PROT_NONE, MAP_INHERIT_NONE, MADV_RANDOM, UVM_FLAG_FIXED)))
		panic("uvm_init: cannot reserve dead beef @0x%x", DEADBEEF0);
#endif
#ifdef DEADBEEF1
	kvm_start = trunc_page(DEADBEEF1) - PAGE_SIZE;
	if (uvm_map(kernel_map, &kvm_start, 3 * PAGE_SIZE,
	    NULL, UVM_UNKNOWN_OFFSET, 0, UVM_MAPFLAG(PROT_NONE,
	    PROT_NONE, MAP_INHERIT_NONE, MADV_RANDOM, UVM_FLAG_FIXED)))
		panic("uvm_init: cannot reserve dead beef @0x%x", DEADBEEF1);
#endif
	/*
	 * Init anonymous memory systems.
	 */
	uvm_anon_init();

#ifndef SMALL_KERNEL
	/*
	 * Switch kernel and kmem_map over to a best-fit allocator,
	 * instead of walking the tree.
	 */
	uvm_map_set_uaddr(kernel_map, &kernel_map->uaddr_any[3],
	    uaddr_bestfit_create(vm_map_min(kernel_map),
	    vm_map_max(kernel_map)));
	uvm_map_set_uaddr(kmem_map, &kmem_map->uaddr_any[3],
	    uaddr_bestfit_create(vm_map_min(kmem_map),
	    vm_map_max(kmem_map)));
#endif /* !SMALL_KERNEL */
}

void
uvm_init_percpu(void)
{
	uvmexp_counters = counters_alloc_ncpus(uvmexp_counters, exp_ncounters);

	uvm_anon_init_percpu();
}
