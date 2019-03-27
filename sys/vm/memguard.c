/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005, Bosko Milekic <bmilekic@FreeBSD.org>.
 * Copyright (c) 2010 Isilon Systems, Inc. (http://www.isilon.com/)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * MemGuard is a simple replacement allocator for debugging only
 * which provides ElectricFence-style memory barrier protection on
 * objects being allocated, and is used to detect tampering-after-free
 * scenarios.
 *
 * See the memguard(9) man page for more information on using MemGuard.
 */

#include "opt_vm.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/types.h>
#include <sys/queue.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/malloc.h>
#include <sys/sysctl.h>
#include <sys/vmem.h>
#include <sys/vmmeter.h>

#include <vm/vm.h>
#include <vm/uma.h>
#include <vm/vm_param.h>
#include <vm/vm_page.h>
#include <vm/vm_map.h>
#include <vm/vm_object.h>
#include <vm/vm_kern.h>
#include <vm/vm_extern.h>
#include <vm/uma_int.h>
#include <vm/memguard.h>

static SYSCTL_NODE(_vm, OID_AUTO, memguard, CTLFLAG_RW, NULL, "MemGuard data");
/*
 * The vm_memguard_divisor variable controls how much of kernel_arena should be
 * reserved for MemGuard.
 */
static u_int vm_memguard_divisor;
SYSCTL_UINT(_vm_memguard, OID_AUTO, divisor, CTLFLAG_RDTUN | CTLFLAG_NOFETCH,
    &vm_memguard_divisor,
    0, "(kmem_size/memguard_divisor) == memguard submap size");

/*
 * Short description (ks_shortdesc) of memory type to monitor.
 */
static char vm_memguard_desc[128] = "";
static struct malloc_type *vm_memguard_mtype = NULL;
TUNABLE_STR("vm.memguard.desc", vm_memguard_desc, sizeof(vm_memguard_desc));
static int
memguard_sysctl_desc(SYSCTL_HANDLER_ARGS)
{
	char desc[sizeof(vm_memguard_desc)];
	int error;

	strlcpy(desc, vm_memguard_desc, sizeof(desc));
	error = sysctl_handle_string(oidp, desc, sizeof(desc), req);
	if (error != 0 || req->newptr == NULL)
		return (error);

	mtx_lock(&malloc_mtx);
	/* If mtp is NULL, it will be initialized in memguard_cmp() */
	vm_memguard_mtype = malloc_desc2type(desc);
	strlcpy(vm_memguard_desc, desc, sizeof(vm_memguard_desc));
	mtx_unlock(&malloc_mtx);
	return (error);
}
SYSCTL_PROC(_vm_memguard, OID_AUTO, desc,
    CTLTYPE_STRING | CTLFLAG_RW | CTLFLAG_MPSAFE, 0, 0,
    memguard_sysctl_desc, "A", "Short description of memory type to monitor");

static vm_offset_t memguard_cursor;
static vm_offset_t memguard_base;
static vm_size_t memguard_mapsize;
static vm_size_t memguard_physlimit;
static u_long memguard_wasted;
static u_long memguard_wrap;
static u_long memguard_succ;
static u_long memguard_fail_kva;
static u_long memguard_fail_pgs;

SYSCTL_ULONG(_vm_memguard, OID_AUTO, cursor, CTLFLAG_RD,
    &memguard_cursor, 0, "MemGuard cursor");
SYSCTL_ULONG(_vm_memguard, OID_AUTO, mapsize, CTLFLAG_RD,
    &memguard_mapsize, 0, "MemGuard private arena size");
SYSCTL_ULONG(_vm_memguard, OID_AUTO, phys_limit, CTLFLAG_RD,
    &memguard_physlimit, 0, "Limit on MemGuard memory consumption");
SYSCTL_ULONG(_vm_memguard, OID_AUTO, wasted, CTLFLAG_RD,
    &memguard_wasted, 0, "Excess memory used through page promotion");
SYSCTL_ULONG(_vm_memguard, OID_AUTO, wrapcnt, CTLFLAG_RD,
    &memguard_wrap, 0, "MemGuard cursor wrap count");
SYSCTL_ULONG(_vm_memguard, OID_AUTO, numalloc, CTLFLAG_RD,
    &memguard_succ, 0, "Count of successful MemGuard allocations");
SYSCTL_ULONG(_vm_memguard, OID_AUTO, fail_kva, CTLFLAG_RD,
    &memguard_fail_kva, 0, "MemGuard failures due to lack of KVA");
SYSCTL_ULONG(_vm_memguard, OID_AUTO, fail_pgs, CTLFLAG_RD,
    &memguard_fail_pgs, 0, "MemGuard failures due to lack of pages");

#define MG_GUARD_AROUND		0x001
#define MG_GUARD_ALLLARGE	0x002
#define MG_GUARD_NOFREE		0x004
static int memguard_options = MG_GUARD_AROUND;
SYSCTL_INT(_vm_memguard, OID_AUTO, options, CTLFLAG_RWTUN,
    &memguard_options, 0,
    "MemGuard options:\n"
    "\t0x001 - add guard pages around each allocation\n"
    "\t0x002 - always use MemGuard for allocations over a page\n"
    "\t0x004 - guard uma(9) zones with UMA_ZONE_NOFREE flag");

static u_int memguard_minsize;
static u_long memguard_minsize_reject;
SYSCTL_UINT(_vm_memguard, OID_AUTO, minsize, CTLFLAG_RW,
    &memguard_minsize, 0, "Minimum size for page promotion");
SYSCTL_ULONG(_vm_memguard, OID_AUTO, minsize_reject, CTLFLAG_RD,
    &memguard_minsize_reject, 0, "# times rejected for size");

static u_int memguard_frequency;
static u_long memguard_frequency_hits;
SYSCTL_UINT(_vm_memguard, OID_AUTO, frequency, CTLFLAG_RWTUN,
    &memguard_frequency, 0, "Times in 100000 that MemGuard will randomly run");
SYSCTL_ULONG(_vm_memguard, OID_AUTO, frequency_hits, CTLFLAG_RD,
    &memguard_frequency_hits, 0, "# times MemGuard randomly chose");


/*
 * Return a fudged value to be used for vm_kmem_size for allocating
 * the kernel_arena.  The memguard memory will be a submap.
 */
unsigned long
memguard_fudge(unsigned long km_size, const struct vm_map *parent_map)
{
	u_long mem_pgs, parent_size;

	vm_memguard_divisor = 10;
	/* CTFLAG_RDTUN doesn't work during the early boot process. */
	TUNABLE_INT_FETCH("vm.memguard.divisor", &vm_memguard_divisor);

	parent_size = vm_map_max(parent_map) - vm_map_min(parent_map) +
	    PAGE_SIZE;
	/* Pick a conservative value if provided value sucks. */
	if ((vm_memguard_divisor <= 0) ||
	    ((parent_size / vm_memguard_divisor) == 0))
		vm_memguard_divisor = 10;
	/*
	 * Limit consumption of physical pages to
	 * 1/vm_memguard_divisor of system memory.  If the KVA is
	 * smaller than this then the KVA limit comes into play first.
	 * This prevents memguard's page promotions from completely
	 * using up memory, since most malloc(9) calls are sub-page.
	 */
	mem_pgs = vm_cnt.v_page_count;
	memguard_physlimit = (mem_pgs / vm_memguard_divisor) * PAGE_SIZE;
	/*
	 * We want as much KVA as we can take safely.  Use at most our
	 * allotted fraction of the parent map's size.  Limit this to
	 * twice the physical memory to avoid using too much memory as
	 * pagetable pages (size must be multiple of PAGE_SIZE).
	 */
	memguard_mapsize = round_page(parent_size / vm_memguard_divisor);
	if (memguard_mapsize / (2 * PAGE_SIZE) > mem_pgs)
		memguard_mapsize = mem_pgs * 2 * PAGE_SIZE;
	if (km_size + memguard_mapsize > parent_size)
		memguard_mapsize = 0;
	return (km_size + memguard_mapsize);
}

/*
 * Initialize the MemGuard mock allocator.  All objects from MemGuard come
 * out of a single VM map (contiguous chunk of address space).
 */
void
memguard_init(vmem_t *parent)
{
	vm_offset_t base;

	vmem_alloc(parent, memguard_mapsize, M_BESTFIT | M_WAITOK, &base);
	vmem_init(memguard_arena, "memguard arena", base, memguard_mapsize,
	    PAGE_SIZE, 0, M_WAITOK);
	memguard_cursor = base;
	memguard_base = base;

	printf("MEMGUARD DEBUGGING ALLOCATOR INITIALIZED:\n");
	printf("\tMEMGUARD map base: 0x%lx\n", (u_long)base);
	printf("\tMEMGUARD map size: %jd KBytes\n",
	    (uintmax_t)memguard_mapsize >> 10);
}

/*
 * Run things that can't be done as early as memguard_init().
 */
static void
memguard_sysinit(void)
{
	struct sysctl_oid_list *parent;

	parent = SYSCTL_STATIC_CHILDREN(_vm_memguard);

	SYSCTL_ADD_UAUTO(NULL, parent, OID_AUTO, "mapstart", CTLFLAG_RD,
	    &memguard_base, "MemGuard KVA base");
	SYSCTL_ADD_UAUTO(NULL, parent, OID_AUTO, "maplimit", CTLFLAG_RD,
	    &memguard_mapsize, "MemGuard KVA size");
#if 0
	SYSCTL_ADD_ULONG(NULL, parent, OID_AUTO, "mapused", CTLFLAG_RD,
	    &memguard_map->size, "MemGuard KVA used");
#endif
}
SYSINIT(memguard, SI_SUB_KLD, SI_ORDER_ANY, memguard_sysinit, NULL);

/*
 * v2sizep() converts a virtual address of the first page allocated for
 * an item to a pointer to u_long recording the size of the original
 * allocation request.
 *
 * This routine is very similar to those defined by UMA in uma_int.h.
 * The difference is that this routine stores the originally allocated
 * size in one of the page's fields that is unused when the page is
 * wired rather than the object field, which is used.
 */
static u_long *
v2sizep(vm_offset_t va)
{
	vm_paddr_t pa;
	struct vm_page *p;

	pa = pmap_kextract(va);
	if (pa == 0)
		panic("MemGuard detected double-free of %p", (void *)va);
	p = PHYS_TO_VM_PAGE(pa);
	KASSERT(p->wire_count != 0 && p->queue == PQ_NONE,
	    ("MEMGUARD: Expected wired page %p in vtomgfifo!", p));
	return (&p->plinks.memguard.p);
}

static u_long *
v2sizev(vm_offset_t va)
{
	vm_paddr_t pa;
	struct vm_page *p;

	pa = pmap_kextract(va);
	if (pa == 0)
		panic("MemGuard detected double-free of %p", (void *)va);
	p = PHYS_TO_VM_PAGE(pa);
	KASSERT(p->wire_count != 0 && p->queue == PQ_NONE,
	    ("MEMGUARD: Expected wired page %p in vtomgfifo!", p));
	return (&p->plinks.memguard.v);
}

/*
 * Allocate a single object of specified size with specified flags
 * (either M_WAITOK or M_NOWAIT).
 */
void *
memguard_alloc(unsigned long req_size, int flags)
{
	vm_offset_t addr, origaddr;
	u_long size_p, size_v;
	int do_guard, rv;

	size_p = round_page(req_size);
	if (size_p == 0)
		return (NULL);
	/*
	 * To ensure there are holes on both sides of the allocation,
	 * request 2 extra pages of KVA.  We will only actually add a
	 * vm_map_entry and get pages for the original request.  Save
	 * the value of memguard_options so we have a consistent
	 * value.
	 */
	size_v = size_p;
	do_guard = (memguard_options & MG_GUARD_AROUND) != 0;
	if (do_guard)
		size_v += 2 * PAGE_SIZE;

	/*
	 * When we pass our memory limit, reject sub-page allocations.
	 * Page-size and larger allocations will use the same amount
	 * of physical memory whether we allocate or hand off to
	 * uma_large_alloc(), so keep those.
	 */
	if (vmem_size(memguard_arena, VMEM_ALLOC) >= memguard_physlimit &&
	    req_size < PAGE_SIZE) {
		addr = (vm_offset_t)NULL;
		memguard_fail_pgs++;
		goto out;
	}
	/*
	 * Keep a moving cursor so we don't recycle KVA as long as
	 * possible.  It's not perfect, since we don't know in what
	 * order previous allocations will be free'd, but it's simple
	 * and fast, and requires O(1) additional storage if guard
	 * pages are not used.
	 *
	 * XXX This scheme will lead to greater fragmentation of the
	 * map, unless vm_map_findspace() is tweaked.
	 */
	for (;;) {
		if (vmem_xalloc(memguard_arena, size_v, 0, 0, 0,
		    memguard_cursor, VMEM_ADDR_MAX,
		    M_BESTFIT | M_NOWAIT, &origaddr) == 0)
			break;
		/*
		 * The map has no space.  This may be due to
		 * fragmentation, or because the cursor is near the
		 * end of the map.
		 */
		if (memguard_cursor == memguard_base) {
			memguard_fail_kva++;
			addr = (vm_offset_t)NULL;
			goto out;
		}
		memguard_wrap++;
		memguard_cursor = memguard_base;
	}
	addr = origaddr;
	if (do_guard)
		addr += PAGE_SIZE;
	rv = kmem_back(kernel_object, addr, size_p, flags);
	if (rv != KERN_SUCCESS) {
		vmem_xfree(memguard_arena, origaddr, size_v);
		memguard_fail_pgs++;
		addr = (vm_offset_t)NULL;
		goto out;
	}
	memguard_cursor = addr + size_v;
	*v2sizep(trunc_page(addr)) = req_size;
	*v2sizev(trunc_page(addr)) = size_v;
	memguard_succ++;
	if (req_size < PAGE_SIZE) {
		memguard_wasted += (PAGE_SIZE - req_size);
		if (do_guard) {
			/*
			 * Align the request to 16 bytes, and return
			 * an address near the end of the page, to
			 * better detect array overrun.
			 */
			req_size = roundup2(req_size, 16);
			addr += (PAGE_SIZE - req_size);
		}
	}
out:
	return ((void *)addr);
}

int
is_memguard_addr(void *addr)
{
	vm_offset_t a = (vm_offset_t)(uintptr_t)addr;

	return (a >= memguard_base && a < memguard_base + memguard_mapsize);
}

/*
 * Free specified single object.
 */
void
memguard_free(void *ptr)
{
	vm_offset_t addr;
	u_long req_size, size, sizev;
	char *temp;
	int i;

	addr = trunc_page((uintptr_t)ptr);
	req_size = *v2sizep(addr);
	sizev = *v2sizev(addr);
	size = round_page(req_size);

	/*
	 * Page should not be guarded right now, so force a write.
	 * The purpose of this is to increase the likelihood of
	 * catching a double-free, but not necessarily a
	 * tamper-after-free (the second thread freeing might not
	 * write before freeing, so this forces it to and,
	 * subsequently, trigger a fault).
	 */
	temp = ptr;
	for (i = 0; i < size; i += PAGE_SIZE)
		temp[i] = 'M';

	/*
	 * This requires carnal knowledge of the implementation of
	 * kmem_free(), but since we've already replaced kmem_malloc()
	 * above, it's not really any worse.  We want to use the
	 * vm_map lock to serialize updates to memguard_wasted, since
	 * we had the lock at increment.
	 */
	kmem_unback(kernel_object, addr, size);
	if (sizev > size)
		addr -= PAGE_SIZE;
	vmem_xfree(memguard_arena, addr, sizev);
	if (req_size < PAGE_SIZE)
		memguard_wasted -= (PAGE_SIZE - req_size);
}

/*
 * Re-allocate an allocation that was originally guarded.
 */
void *
memguard_realloc(void *addr, unsigned long size, struct malloc_type *mtp,
    int flags)
{
	void *newaddr;
	u_long old_size;

	/*
	 * Allocate the new block.  Force the allocation to be guarded
	 * as the original may have been guarded through random
	 * chance, and that should be preserved.
	 */
	if ((newaddr = memguard_alloc(size, flags)) == NULL)
		return (NULL);

	/* Copy over original contents. */
	old_size = *v2sizep(trunc_page((uintptr_t)addr));
	bcopy(addr, newaddr, min(size, old_size));
	memguard_free(addr);
	return (newaddr);
}

static int
memguard_cmp(unsigned long size)
{

	if (size < memguard_minsize) {
		memguard_minsize_reject++;
		return (0);
	}
	if ((memguard_options & MG_GUARD_ALLLARGE) != 0 && size >= PAGE_SIZE)
		return (1);
	if (memguard_frequency > 0 &&
	    (random() % 100000) < memguard_frequency) {
		memguard_frequency_hits++;
		return (1);
	}

	return (0);
}

int
memguard_cmp_mtp(struct malloc_type *mtp, unsigned long size)
{

	if (memguard_cmp(size))
		return(1);

#if 1
	/*
	 * The safest way of comparsion is to always compare short description
	 * string of memory type, but it is also the slowest way.
	 */
	return (strcmp(mtp->ks_shortdesc, vm_memguard_desc) == 0);
#else
	/*
	 * If we compare pointers, there are two possible problems:
	 * 1. Memory type was unloaded and new memory type was allocated at the
	 *    same address.
	 * 2. Memory type was unloaded and loaded again, but allocated at a
	 *    different address.
	 */
	if (vm_memguard_mtype != NULL)
		return (mtp == vm_memguard_mtype);
	if (strcmp(mtp->ks_shortdesc, vm_memguard_desc) == 0) {
		vm_memguard_mtype = mtp;
		return (1);
	}
	return (0);
#endif
}

int
memguard_cmp_zone(uma_zone_t zone)
{

	if ((memguard_options & MG_GUARD_NOFREE) == 0 &&
	    zone->uz_flags & UMA_ZONE_NOFREE)
		return (0);

	if (memguard_cmp(zone->uz_size))
		return (1);

	/*
	 * The safest way of comparsion is to always compare zone name,
	 * but it is also the slowest way.
	 */
	return (strcmp(zone->uz_name, vm_memguard_desc) == 0);
}
