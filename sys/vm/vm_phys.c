/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002-2006 Rice University
 * Copyright (c) 2007 Alan L. Cox <alc@cs.rice.edu>
 * All rights reserved.
 *
 * This software was developed for the FreeBSD Project by Alan L. Cox,
 * Olivier Crameri, Peter Druschel, Sitaram Iyer, and Juan Navarro.
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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY
 * WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 *	Physical memory system implementation
 *
 * Any external functions defined by this module are only to be used by the
 * virtual memory system.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_ddb.h"
#include "opt_vm.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/domainset.h>
#include <sys/lock.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/queue.h>
#include <sys/rwlock.h>
#include <sys/sbuf.h>
#include <sys/sysctl.h>
#include <sys/tree.h>
#include <sys/vmmeter.h>

#include <ddb/ddb.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/vm_kern.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_phys.h>
#include <vm/vm_pagequeue.h>

_Static_assert(sizeof(long) * NBBY >= VM_PHYSSEG_MAX,
    "Too many physsegs.");

#ifdef NUMA
struct mem_affinity __read_mostly *mem_affinity;
int __read_mostly *mem_locality;
#endif

int __read_mostly vm_ndomains = 1;
domainset_t __read_mostly all_domains = DOMAINSET_T_INITIALIZER(0x1);

struct vm_phys_seg __read_mostly vm_phys_segs[VM_PHYSSEG_MAX];
int __read_mostly vm_phys_nsegs;

struct vm_phys_fictitious_seg;
static int vm_phys_fictitious_cmp(struct vm_phys_fictitious_seg *,
    struct vm_phys_fictitious_seg *);

RB_HEAD(fict_tree, vm_phys_fictitious_seg) vm_phys_fictitious_tree =
    RB_INITIALIZER(_vm_phys_fictitious_tree);

struct vm_phys_fictitious_seg {
	RB_ENTRY(vm_phys_fictitious_seg) node;
	/* Memory region data */
	vm_paddr_t	start;
	vm_paddr_t	end;
	vm_page_t	first_page;
};

RB_GENERATE_STATIC(fict_tree, vm_phys_fictitious_seg, node,
    vm_phys_fictitious_cmp);

static struct rwlock_padalign vm_phys_fictitious_reg_lock;
MALLOC_DEFINE(M_FICT_PAGES, "vm_fictitious", "Fictitious VM pages");

static struct vm_freelist __aligned(CACHE_LINE_SIZE)
    vm_phys_free_queues[MAXMEMDOM][VM_NFREELIST][VM_NFREEPOOL]
    [VM_NFREEORDER_MAX];

static int __read_mostly vm_nfreelists;

/*
 * Provides the mapping from VM_FREELIST_* to free list indices (flind).
 */
static int __read_mostly vm_freelist_to_flind[VM_NFREELIST];

CTASSERT(VM_FREELIST_DEFAULT == 0);

#ifdef VM_FREELIST_DMA32
#define	VM_DMA32_BOUNDARY	((vm_paddr_t)1 << 32)
#endif

/*
 * Enforce the assumptions made by vm_phys_add_seg() and vm_phys_init() about
 * the ordering of the free list boundaries.
 */
#if defined(VM_LOWMEM_BOUNDARY) && defined(VM_DMA32_BOUNDARY)
CTASSERT(VM_LOWMEM_BOUNDARY < VM_DMA32_BOUNDARY);
#endif

static int sysctl_vm_phys_free(SYSCTL_HANDLER_ARGS);
SYSCTL_OID(_vm, OID_AUTO, phys_free, CTLTYPE_STRING | CTLFLAG_RD,
    NULL, 0, sysctl_vm_phys_free, "A", "Phys Free Info");

static int sysctl_vm_phys_segs(SYSCTL_HANDLER_ARGS);
SYSCTL_OID(_vm, OID_AUTO, phys_segs, CTLTYPE_STRING | CTLFLAG_RD,
    NULL, 0, sysctl_vm_phys_segs, "A", "Phys Seg Info");

#ifdef NUMA
static int sysctl_vm_phys_locality(SYSCTL_HANDLER_ARGS);
SYSCTL_OID(_vm, OID_AUTO, phys_locality, CTLTYPE_STRING | CTLFLAG_RD,
    NULL, 0, sysctl_vm_phys_locality, "A", "Phys Locality Info");
#endif

SYSCTL_INT(_vm, OID_AUTO, ndomains, CTLFLAG_RD,
    &vm_ndomains, 0, "Number of physical memory domains available.");

static vm_page_t vm_phys_alloc_seg_contig(struct vm_phys_seg *seg,
    u_long npages, vm_paddr_t low, vm_paddr_t high, u_long alignment,
    vm_paddr_t boundary);
static void _vm_phys_create_seg(vm_paddr_t start, vm_paddr_t end, int domain);
static void vm_phys_create_seg(vm_paddr_t start, vm_paddr_t end);
static void vm_phys_split_pages(vm_page_t m, int oind, struct vm_freelist *fl,
    int order, int tail);

/*
 * Red-black tree helpers for vm fictitious range management.
 */
static inline int
vm_phys_fictitious_in_range(struct vm_phys_fictitious_seg *p,
    struct vm_phys_fictitious_seg *range)
{

	KASSERT(range->start != 0 && range->end != 0,
	    ("Invalid range passed on search for vm_fictitious page"));
	if (p->start >= range->end)
		return (1);
	if (p->start < range->start)
		return (-1);

	return (0);
}

static int
vm_phys_fictitious_cmp(struct vm_phys_fictitious_seg *p1,
    struct vm_phys_fictitious_seg *p2)
{

	/* Check if this is a search for a page */
	if (p1->end == 0)
		return (vm_phys_fictitious_in_range(p1, p2));

	KASSERT(p2->end != 0,
    ("Invalid range passed as second parameter to vm fictitious comparison"));

	/* Searching to add a new range */
	if (p1->end <= p2->start)
		return (-1);
	if (p1->start >= p2->end)
		return (1);

	panic("Trying to add overlapping vm fictitious ranges:\n"
	    "[%#jx:%#jx] and [%#jx:%#jx]", (uintmax_t)p1->start,
	    (uintmax_t)p1->end, (uintmax_t)p2->start, (uintmax_t)p2->end);
}

int
vm_phys_domain_match(int prefer, vm_paddr_t low, vm_paddr_t high)
{
#ifdef NUMA
	domainset_t mask;
	int i;

	if (vm_ndomains == 1 || mem_affinity == NULL)
		return (0);

	DOMAINSET_ZERO(&mask);
	/*
	 * Check for any memory that overlaps low, high.
	 */
	for (i = 0; mem_affinity[i].end != 0; i++)
		if (mem_affinity[i].start <= high &&
		    mem_affinity[i].end >= low)
			DOMAINSET_SET(mem_affinity[i].domain, &mask);
	if (prefer != -1 && DOMAINSET_ISSET(prefer, &mask))
		return (prefer);
	if (DOMAINSET_EMPTY(&mask))
		panic("vm_phys_domain_match:  Impossible constraint");
	return (DOMAINSET_FFS(&mask) - 1);
#else
	return (0);
#endif
}

/*
 * Outputs the state of the physical memory allocator, specifically,
 * the amount of physical memory in each free list.
 */
static int
sysctl_vm_phys_free(SYSCTL_HANDLER_ARGS)
{
	struct sbuf sbuf;
	struct vm_freelist *fl;
	int dom, error, flind, oind, pind;

	error = sysctl_wire_old_buffer(req, 0);
	if (error != 0)
		return (error);
	sbuf_new_for_sysctl(&sbuf, NULL, 128 * vm_ndomains, req);
	for (dom = 0; dom < vm_ndomains; dom++) {
		sbuf_printf(&sbuf,"\nDOMAIN %d:\n", dom);
		for (flind = 0; flind < vm_nfreelists; flind++) {
			sbuf_printf(&sbuf, "\nFREE LIST %d:\n"
			    "\n  ORDER (SIZE)  |  NUMBER"
			    "\n              ", flind);
			for (pind = 0; pind < VM_NFREEPOOL; pind++)
				sbuf_printf(&sbuf, "  |  POOL %d", pind);
			sbuf_printf(&sbuf, "\n--            ");
			for (pind = 0; pind < VM_NFREEPOOL; pind++)
				sbuf_printf(&sbuf, "-- --      ");
			sbuf_printf(&sbuf, "--\n");
			for (oind = VM_NFREEORDER - 1; oind >= 0; oind--) {
				sbuf_printf(&sbuf, "  %2d (%6dK)", oind,
				    1 << (PAGE_SHIFT - 10 + oind));
				for (pind = 0; pind < VM_NFREEPOOL; pind++) {
				fl = vm_phys_free_queues[dom][flind][pind];
					sbuf_printf(&sbuf, "  |  %6d",
					    fl[oind].lcnt);
				}
				sbuf_printf(&sbuf, "\n");
			}
		}
	}
	error = sbuf_finish(&sbuf);
	sbuf_delete(&sbuf);
	return (error);
}

/*
 * Outputs the set of physical memory segments.
 */
static int
sysctl_vm_phys_segs(SYSCTL_HANDLER_ARGS)
{
	struct sbuf sbuf;
	struct vm_phys_seg *seg;
	int error, segind;

	error = sysctl_wire_old_buffer(req, 0);
	if (error != 0)
		return (error);
	sbuf_new_for_sysctl(&sbuf, NULL, 128, req);
	for (segind = 0; segind < vm_phys_nsegs; segind++) {
		sbuf_printf(&sbuf, "\nSEGMENT %d:\n\n", segind);
		seg = &vm_phys_segs[segind];
		sbuf_printf(&sbuf, "start:     %#jx\n",
		    (uintmax_t)seg->start);
		sbuf_printf(&sbuf, "end:       %#jx\n",
		    (uintmax_t)seg->end);
		sbuf_printf(&sbuf, "domain:    %d\n", seg->domain);
		sbuf_printf(&sbuf, "free list: %p\n", seg->free_queues);
	}
	error = sbuf_finish(&sbuf);
	sbuf_delete(&sbuf);
	return (error);
}

/*
 * Return affinity, or -1 if there's no affinity information.
 */
int
vm_phys_mem_affinity(int f, int t)
{

#ifdef NUMA
	if (mem_locality == NULL)
		return (-1);
	if (f >= vm_ndomains || t >= vm_ndomains)
		return (-1);
	return (mem_locality[f * vm_ndomains + t]);
#else
	return (-1);
#endif
}

#ifdef NUMA
/*
 * Outputs the VM locality table.
 */
static int
sysctl_vm_phys_locality(SYSCTL_HANDLER_ARGS)
{
	struct sbuf sbuf;
	int error, i, j;

	error = sysctl_wire_old_buffer(req, 0);
	if (error != 0)
		return (error);
	sbuf_new_for_sysctl(&sbuf, NULL, 128, req);

	sbuf_printf(&sbuf, "\n");

	for (i = 0; i < vm_ndomains; i++) {
		sbuf_printf(&sbuf, "%d: ", i);
		for (j = 0; j < vm_ndomains; j++) {
			sbuf_printf(&sbuf, "%d ", vm_phys_mem_affinity(i, j));
		}
		sbuf_printf(&sbuf, "\n");
	}
	error = sbuf_finish(&sbuf);
	sbuf_delete(&sbuf);
	return (error);
}
#endif

static void
vm_freelist_add(struct vm_freelist *fl, vm_page_t m, int order, int tail)
{

	m->order = order;
	if (tail)
		TAILQ_INSERT_TAIL(&fl[order].pl, m, listq);
	else
		TAILQ_INSERT_HEAD(&fl[order].pl, m, listq);
	fl[order].lcnt++;
}

static void
vm_freelist_rem(struct vm_freelist *fl, vm_page_t m, int order)
{

	TAILQ_REMOVE(&fl[order].pl, m, listq);
	fl[order].lcnt--;
	m->order = VM_NFREEORDER;
}

/*
 * Create a physical memory segment.
 */
static void
_vm_phys_create_seg(vm_paddr_t start, vm_paddr_t end, int domain)
{
	struct vm_phys_seg *seg;

	KASSERT(vm_phys_nsegs < VM_PHYSSEG_MAX,
	    ("vm_phys_create_seg: increase VM_PHYSSEG_MAX"));
	KASSERT(domain >= 0 && domain < vm_ndomains,
	    ("vm_phys_create_seg: invalid domain provided"));
	seg = &vm_phys_segs[vm_phys_nsegs++];
	while (seg > vm_phys_segs && (seg - 1)->start >= end) {
		*seg = *(seg - 1);
		seg--;
	}
	seg->start = start;
	seg->end = end;
	seg->domain = domain;
}

static void
vm_phys_create_seg(vm_paddr_t start, vm_paddr_t end)
{
#ifdef NUMA
	int i;

	if (mem_affinity == NULL) {
		_vm_phys_create_seg(start, end, 0);
		return;
	}

	for (i = 0;; i++) {
		if (mem_affinity[i].end == 0)
			panic("Reached end of affinity info");
		if (mem_affinity[i].end <= start)
			continue;
		if (mem_affinity[i].start > start)
			panic("No affinity info for start %jx",
			    (uintmax_t)start);
		if (mem_affinity[i].end >= end) {
			_vm_phys_create_seg(start, end,
			    mem_affinity[i].domain);
			break;
		}
		_vm_phys_create_seg(start, mem_affinity[i].end,
		    mem_affinity[i].domain);
		start = mem_affinity[i].end;
	}
#else
	_vm_phys_create_seg(start, end, 0);
#endif
}

/*
 * Add a physical memory segment.
 */
void
vm_phys_add_seg(vm_paddr_t start, vm_paddr_t end)
{
	vm_paddr_t paddr;

	KASSERT((start & PAGE_MASK) == 0,
	    ("vm_phys_define_seg: start is not page aligned"));
	KASSERT((end & PAGE_MASK) == 0,
	    ("vm_phys_define_seg: end is not page aligned"));

	/*
	 * Split the physical memory segment if it spans two or more free
	 * list boundaries.
	 */
	paddr = start;
#ifdef	VM_FREELIST_LOWMEM
	if (paddr < VM_LOWMEM_BOUNDARY && end > VM_LOWMEM_BOUNDARY) {
		vm_phys_create_seg(paddr, VM_LOWMEM_BOUNDARY);
		paddr = VM_LOWMEM_BOUNDARY;
	}
#endif
#ifdef	VM_FREELIST_DMA32
	if (paddr < VM_DMA32_BOUNDARY && end > VM_DMA32_BOUNDARY) {
		vm_phys_create_seg(paddr, VM_DMA32_BOUNDARY);
		paddr = VM_DMA32_BOUNDARY;
	}
#endif
	vm_phys_create_seg(paddr, end);
}

/*
 * Initialize the physical memory allocator.
 *
 * Requires that vm_page_array is initialized!
 */
void
vm_phys_init(void)
{
	struct vm_freelist *fl;
	struct vm_phys_seg *end_seg, *prev_seg, *seg, *tmp_seg;
	u_long npages;
	int dom, flind, freelist, oind, pind, segind;

	/*
	 * Compute the number of free lists, and generate the mapping from the
	 * manifest constants VM_FREELIST_* to the free list indices.
	 *
	 * Initially, the entries of vm_freelist_to_flind[] are set to either
	 * 0 or 1 to indicate which free lists should be created.
	 */
	npages = 0;
	for (segind = vm_phys_nsegs - 1; segind >= 0; segind--) {
		seg = &vm_phys_segs[segind];
#ifdef	VM_FREELIST_LOWMEM
		if (seg->end <= VM_LOWMEM_BOUNDARY)
			vm_freelist_to_flind[VM_FREELIST_LOWMEM] = 1;
		else
#endif
#ifdef	VM_FREELIST_DMA32
		if (
#ifdef	VM_DMA32_NPAGES_THRESHOLD
		    /*
		     * Create the DMA32 free list only if the amount of
		     * physical memory above physical address 4G exceeds the
		     * given threshold.
		     */
		    npages > VM_DMA32_NPAGES_THRESHOLD &&
#endif
		    seg->end <= VM_DMA32_BOUNDARY)
			vm_freelist_to_flind[VM_FREELIST_DMA32] = 1;
		else
#endif
		{
			npages += atop(seg->end - seg->start);
			vm_freelist_to_flind[VM_FREELIST_DEFAULT] = 1;
		}
	}
	/* Change each entry into a running total of the free lists. */
	for (freelist = 1; freelist < VM_NFREELIST; freelist++) {
		vm_freelist_to_flind[freelist] +=
		    vm_freelist_to_flind[freelist - 1];
	}
	vm_nfreelists = vm_freelist_to_flind[VM_NFREELIST - 1];
	KASSERT(vm_nfreelists > 0, ("vm_phys_init: no free lists"));
	/* Change each entry into a free list index. */
	for (freelist = 0; freelist < VM_NFREELIST; freelist++)
		vm_freelist_to_flind[freelist]--;

	/*
	 * Initialize the first_page and free_queues fields of each physical
	 * memory segment.
	 */
#ifdef VM_PHYSSEG_SPARSE
	npages = 0;
#endif
	for (segind = 0; segind < vm_phys_nsegs; segind++) {
		seg = &vm_phys_segs[segind];
#ifdef VM_PHYSSEG_SPARSE
		seg->first_page = &vm_page_array[npages];
		npages += atop(seg->end - seg->start);
#else
		seg->first_page = PHYS_TO_VM_PAGE(seg->start);
#endif
#ifdef	VM_FREELIST_LOWMEM
		if (seg->end <= VM_LOWMEM_BOUNDARY) {
			flind = vm_freelist_to_flind[VM_FREELIST_LOWMEM];
			KASSERT(flind >= 0,
			    ("vm_phys_init: LOWMEM flind < 0"));
		} else
#endif
#ifdef	VM_FREELIST_DMA32
		if (seg->end <= VM_DMA32_BOUNDARY) {
			flind = vm_freelist_to_flind[VM_FREELIST_DMA32];
			KASSERT(flind >= 0,
			    ("vm_phys_init: DMA32 flind < 0"));
		} else
#endif
		{
			flind = vm_freelist_to_flind[VM_FREELIST_DEFAULT];
			KASSERT(flind >= 0,
			    ("vm_phys_init: DEFAULT flind < 0"));
		}
		seg->free_queues = &vm_phys_free_queues[seg->domain][flind];
	}

	/*
	 * Coalesce physical memory segments that are contiguous and share the
	 * same per-domain free queues.
	 */
	prev_seg = vm_phys_segs;
	seg = &vm_phys_segs[1];
	end_seg = &vm_phys_segs[vm_phys_nsegs];
	while (seg < end_seg) {
		if (prev_seg->end == seg->start &&
		    prev_seg->free_queues == seg->free_queues) {
			prev_seg->end = seg->end;
			KASSERT(prev_seg->domain == seg->domain,
			    ("vm_phys_init: free queues cannot span domains"));
			vm_phys_nsegs--;
			end_seg--;
			for (tmp_seg = seg; tmp_seg < end_seg; tmp_seg++)
				*tmp_seg = *(tmp_seg + 1);
		} else {
			prev_seg = seg;
			seg++;
		}
	}

	/*
	 * Initialize the free queues.
	 */
	for (dom = 0; dom < vm_ndomains; dom++) {
		for (flind = 0; flind < vm_nfreelists; flind++) {
			for (pind = 0; pind < VM_NFREEPOOL; pind++) {
				fl = vm_phys_free_queues[dom][flind][pind];
				for (oind = 0; oind < VM_NFREEORDER; oind++)
					TAILQ_INIT(&fl[oind].pl);
			}
		}
	}

	rw_init(&vm_phys_fictitious_reg_lock, "vmfctr");
}

/*
 * Register info about the NUMA topology of the system.
 *
 * Invoked by platform-dependent code prior to vm_phys_init().
 */
void
vm_phys_register_domains(int ndomains, struct mem_affinity *affinity,
    int *locality)
{
#ifdef NUMA
	int d, i;

	/*
	 * For now the only override value that we support is 1, which
	 * effectively disables NUMA-awareness in the allocators.
	 */
	d = 0;
	TUNABLE_INT_FETCH("vm.numa.disabled", &d);
	if (d)
		ndomains = 1;

	if (ndomains > 1) {
		vm_ndomains = ndomains;
		mem_affinity = affinity;
		mem_locality = locality;
	}

	for (i = 0; i < vm_ndomains; i++)
		DOMAINSET_SET(i, &all_domains);
#else
	(void)ndomains;
	(void)affinity;
	(void)locality;
#endif
}

/*
 * Split a contiguous, power of two-sized set of physical pages.
 *
 * When this function is called by a page allocation function, the caller
 * should request insertion at the head unless the order [order, oind) queues
 * are known to be empty.  The objective being to reduce the likelihood of
 * long-term fragmentation by promoting contemporaneous allocation and
 * (hopefully) deallocation.
 */
static __inline void
vm_phys_split_pages(vm_page_t m, int oind, struct vm_freelist *fl, int order,
    int tail)
{
	vm_page_t m_buddy;

	while (oind > order) {
		oind--;
		m_buddy = &m[1 << oind];
		KASSERT(m_buddy->order == VM_NFREEORDER,
		    ("vm_phys_split_pages: page %p has unexpected order %d",
		    m_buddy, m_buddy->order));
		vm_freelist_add(fl, m_buddy, oind, tail);
        }
}

/*
 * Add the physical pages [m, m + npages) at the end of a power-of-two aligned
 * and sized set to the specified free list.
 *
 * When this function is called by a page allocation function, the caller
 * should request insertion at the head unless the lower-order queues are
 * known to be empty.  The objective being to reduce the likelihood of long-
 * term fragmentation by promoting contemporaneous allocation and (hopefully)
 * deallocation.
 *
 * The physical page m's buddy must not be free.
 */
static void
vm_phys_enq_range(vm_page_t m, u_int npages, struct vm_freelist *fl, int tail)
{
	u_int n;
	int order;

	KASSERT(npages > 0, ("vm_phys_enq_range: npages is 0"));
	KASSERT(((VM_PAGE_TO_PHYS(m) + npages * PAGE_SIZE) &
	    ((PAGE_SIZE << (fls(npages) - 1)) - 1)) == 0,
	    ("vm_phys_enq_range: page %p and npages %u are misaligned",
	    m, npages));
	do {
		KASSERT(m->order == VM_NFREEORDER,
		    ("vm_phys_enq_range: page %p has unexpected order %d",
		    m, m->order));
		order = ffs(npages) - 1;
		KASSERT(order < VM_NFREEORDER,
		    ("vm_phys_enq_range: order %d is out of range", order));
		vm_freelist_add(fl, m, order, tail);
		n = 1 << order;
		m += n;
		npages -= n;
	} while (npages > 0);
}

/*
 * Tries to allocate the specified number of pages from the specified pool
 * within the specified domain.  Returns the actual number of allocated pages
 * and a pointer to each page through the array ma[].
 *
 * The returned pages may not be physically contiguous.  However, in contrast
 * to performing multiple, back-to-back calls to vm_phys_alloc_pages(..., 0),
 * calling this function once to allocate the desired number of pages will
 * avoid wasted time in vm_phys_split_pages().
 *
 * The free page queues for the specified domain must be locked.
 */
int
vm_phys_alloc_npages(int domain, int pool, int npages, vm_page_t ma[])
{
	struct vm_freelist *alt, *fl;
	vm_page_t m;
	int avail, end, flind, freelist, i, need, oind, pind;

	KASSERT(domain >= 0 && domain < vm_ndomains,
	    ("vm_phys_alloc_npages: domain %d is out of range", domain));
	KASSERT(pool < VM_NFREEPOOL,
	    ("vm_phys_alloc_npages: pool %d is out of range", pool));
	KASSERT(npages <= 1 << (VM_NFREEORDER - 1),
	    ("vm_phys_alloc_npages: npages %d is out of range", npages));
	vm_domain_free_assert_locked(VM_DOMAIN(domain));
	i = 0;
	for (freelist = 0; freelist < VM_NFREELIST; freelist++) {
		flind = vm_freelist_to_flind[freelist];
		if (flind < 0)
			continue;
		fl = vm_phys_free_queues[domain][flind][pool];
		for (oind = 0; oind < VM_NFREEORDER; oind++) {
			while ((m = TAILQ_FIRST(&fl[oind].pl)) != NULL) {
				vm_freelist_rem(fl, m, oind);
				avail = 1 << oind;
				need = imin(npages - i, avail);
				for (end = i + need; i < end;)
					ma[i++] = m++;
				if (need < avail) {
					/*
					 * Return excess pages to fl.  Its
					 * order [0, oind) queues are empty.
					 */
					vm_phys_enq_range(m, avail - need, fl,
					    1);
					return (npages);
				} else if (i == npages)
					return (npages);
			}
		}
		for (oind = VM_NFREEORDER - 1; oind >= 0; oind--) {
			for (pind = 0; pind < VM_NFREEPOOL; pind++) {
				alt = vm_phys_free_queues[domain][flind][pind];
				while ((m = TAILQ_FIRST(&alt[oind].pl)) !=
				    NULL) {
					vm_freelist_rem(alt, m, oind);
					vm_phys_set_pool(pool, m, oind);
					avail = 1 << oind;
					need = imin(npages - i, avail);
					for (end = i + need; i < end;)
						ma[i++] = m++;
					if (need < avail) {
						/*
						 * Return excess pages to fl.
						 * Its order [0, oind) queues
						 * are empty.
						 */
						vm_phys_enq_range(m, avail -
						    need, fl, 1);
						return (npages);
					} else if (i == npages)
						return (npages);
				}
			}
		}
	}
	return (i);
}

/*
 * Allocate a contiguous, power of two-sized set of physical pages
 * from the free lists.
 *
 * The free page queues must be locked.
 */
vm_page_t
vm_phys_alloc_pages(int domain, int pool, int order)
{
	vm_page_t m;
	int freelist;

	for (freelist = 0; freelist < VM_NFREELIST; freelist++) {
		m = vm_phys_alloc_freelist_pages(domain, freelist, pool, order);
		if (m != NULL)
			return (m);
	}
	return (NULL);
}

/*
 * Allocate a contiguous, power of two-sized set of physical pages from the
 * specified free list.  The free list must be specified using one of the
 * manifest constants VM_FREELIST_*.
 *
 * The free page queues must be locked.
 */
vm_page_t
vm_phys_alloc_freelist_pages(int domain, int freelist, int pool, int order)
{
	struct vm_freelist *alt, *fl;
	vm_page_t m;
	int oind, pind, flind;

	KASSERT(domain >= 0 && domain < vm_ndomains,
	    ("vm_phys_alloc_freelist_pages: domain %d is out of range",
	    domain));
	KASSERT(freelist < VM_NFREELIST,
	    ("vm_phys_alloc_freelist_pages: freelist %d is out of range",
	    freelist));
	KASSERT(pool < VM_NFREEPOOL,
	    ("vm_phys_alloc_freelist_pages: pool %d is out of range", pool));
	KASSERT(order < VM_NFREEORDER,
	    ("vm_phys_alloc_freelist_pages: order %d is out of range", order));

	flind = vm_freelist_to_flind[freelist];
	/* Check if freelist is present */
	if (flind < 0)
		return (NULL);

	vm_domain_free_assert_locked(VM_DOMAIN(domain));
	fl = &vm_phys_free_queues[domain][flind][pool][0];
	for (oind = order; oind < VM_NFREEORDER; oind++) {
		m = TAILQ_FIRST(&fl[oind].pl);
		if (m != NULL) {
			vm_freelist_rem(fl, m, oind);
			/* The order [order, oind) queues are empty. */
			vm_phys_split_pages(m, oind, fl, order, 1);
			return (m);
		}
	}

	/*
	 * The given pool was empty.  Find the largest
	 * contiguous, power-of-two-sized set of pages in any
	 * pool.  Transfer these pages to the given pool, and
	 * use them to satisfy the allocation.
	 */
	for (oind = VM_NFREEORDER - 1; oind >= order; oind--) {
		for (pind = 0; pind < VM_NFREEPOOL; pind++) {
			alt = &vm_phys_free_queues[domain][flind][pind][0];
			m = TAILQ_FIRST(&alt[oind].pl);
			if (m != NULL) {
				vm_freelist_rem(alt, m, oind);
				vm_phys_set_pool(pool, m, oind);
				/* The order [order, oind) queues are empty. */
				vm_phys_split_pages(m, oind, fl, order, 1);
				return (m);
			}
		}
	}
	return (NULL);
}

/*
 * Find the vm_page corresponding to the given physical address.
 */
vm_page_t
vm_phys_paddr_to_vm_page(vm_paddr_t pa)
{
	struct vm_phys_seg *seg;
	int segind;

	for (segind = 0; segind < vm_phys_nsegs; segind++) {
		seg = &vm_phys_segs[segind];
		if (pa >= seg->start && pa < seg->end)
			return (&seg->first_page[atop(pa - seg->start)]);
	}
	return (NULL);
}

vm_page_t
vm_phys_fictitious_to_vm_page(vm_paddr_t pa)
{
	struct vm_phys_fictitious_seg tmp, *seg;
	vm_page_t m;

	m = NULL;
	tmp.start = pa;
	tmp.end = 0;

	rw_rlock(&vm_phys_fictitious_reg_lock);
	seg = RB_FIND(fict_tree, &vm_phys_fictitious_tree, &tmp);
	rw_runlock(&vm_phys_fictitious_reg_lock);
	if (seg == NULL)
		return (NULL);

	m = &seg->first_page[atop(pa - seg->start)];
	KASSERT((m->flags & PG_FICTITIOUS) != 0, ("%p not fictitious", m));

	return (m);
}

static inline void
vm_phys_fictitious_init_range(vm_page_t range, vm_paddr_t start,
    long page_count, vm_memattr_t memattr)
{
	long i;

	bzero(range, page_count * sizeof(*range));
	for (i = 0; i < page_count; i++) {
		vm_page_initfake(&range[i], start + PAGE_SIZE * i, memattr);
		range[i].oflags &= ~VPO_UNMANAGED;
		range[i].busy_lock = VPB_UNBUSIED;
	}
}

int
vm_phys_fictitious_reg_range(vm_paddr_t start, vm_paddr_t end,
    vm_memattr_t memattr)
{
	struct vm_phys_fictitious_seg *seg;
	vm_page_t fp;
	long page_count;
#ifdef VM_PHYSSEG_DENSE
	long pi, pe;
	long dpage_count;
#endif

	KASSERT(start < end,
	    ("Start of segment isn't less than end (start: %jx end: %jx)",
	    (uintmax_t)start, (uintmax_t)end));

	page_count = (end - start) / PAGE_SIZE;

#ifdef VM_PHYSSEG_DENSE
	pi = atop(start);
	pe = atop(end);
	if (pi >= first_page && (pi - first_page) < vm_page_array_size) {
		fp = &vm_page_array[pi - first_page];
		if ((pe - first_page) > vm_page_array_size) {
			/*
			 * We have a segment that starts inside
			 * of vm_page_array, but ends outside of it.
			 *
			 * Use vm_page_array pages for those that are
			 * inside of the vm_page_array range, and
			 * allocate the remaining ones.
			 */
			dpage_count = vm_page_array_size - (pi - first_page);
			vm_phys_fictitious_init_range(fp, start, dpage_count,
			    memattr);
			page_count -= dpage_count;
			start += ptoa(dpage_count);
			goto alloc;
		}
		/*
		 * We can allocate the full range from vm_page_array,
		 * so there's no need to register the range in the tree.
		 */
		vm_phys_fictitious_init_range(fp, start, page_count, memattr);
		return (0);
	} else if (pe > first_page && (pe - first_page) < vm_page_array_size) {
		/*
		 * We have a segment that ends inside of vm_page_array,
		 * but starts outside of it.
		 */
		fp = &vm_page_array[0];
		dpage_count = pe - first_page;
		vm_phys_fictitious_init_range(fp, ptoa(first_page), dpage_count,
		    memattr);
		end -= ptoa(dpage_count);
		page_count -= dpage_count;
		goto alloc;
	} else if (pi < first_page && pe > (first_page + vm_page_array_size)) {
		/*
		 * Trying to register a fictitious range that expands before
		 * and after vm_page_array.
		 */
		return (EINVAL);
	} else {
alloc:
#endif
		fp = malloc(page_count * sizeof(struct vm_page), M_FICT_PAGES,
		    M_WAITOK);
#ifdef VM_PHYSSEG_DENSE
	}
#endif
	vm_phys_fictitious_init_range(fp, start, page_count, memattr);

	seg = malloc(sizeof(*seg), M_FICT_PAGES, M_WAITOK | M_ZERO);
	seg->start = start;
	seg->end = end;
	seg->first_page = fp;

	rw_wlock(&vm_phys_fictitious_reg_lock);
	RB_INSERT(fict_tree, &vm_phys_fictitious_tree, seg);
	rw_wunlock(&vm_phys_fictitious_reg_lock);

	return (0);
}

void
vm_phys_fictitious_unreg_range(vm_paddr_t start, vm_paddr_t end)
{
	struct vm_phys_fictitious_seg *seg, tmp;
#ifdef VM_PHYSSEG_DENSE
	long pi, pe;
#endif

	KASSERT(start < end,
	    ("Start of segment isn't less than end (start: %jx end: %jx)",
	    (uintmax_t)start, (uintmax_t)end));

#ifdef VM_PHYSSEG_DENSE
	pi = atop(start);
	pe = atop(end);
	if (pi >= first_page && (pi - first_page) < vm_page_array_size) {
		if ((pe - first_page) <= vm_page_array_size) {
			/*
			 * This segment was allocated using vm_page_array
			 * only, there's nothing to do since those pages
			 * were never added to the tree.
			 */
			return;
		}
		/*
		 * We have a segment that starts inside
		 * of vm_page_array, but ends outside of it.
		 *
		 * Calculate how many pages were added to the
		 * tree and free them.
		 */
		start = ptoa(first_page + vm_page_array_size);
	} else if (pe > first_page && (pe - first_page) < vm_page_array_size) {
		/*
		 * We have a segment that ends inside of vm_page_array,
		 * but starts outside of it.
		 */
		end = ptoa(first_page);
	} else if (pi < first_page && pe > (first_page + vm_page_array_size)) {
		/* Since it's not possible to register such a range, panic. */
		panic(
		    "Unregistering not registered fictitious range [%#jx:%#jx]",
		    (uintmax_t)start, (uintmax_t)end);
	}
#endif
	tmp.start = start;
	tmp.end = 0;

	rw_wlock(&vm_phys_fictitious_reg_lock);
	seg = RB_FIND(fict_tree, &vm_phys_fictitious_tree, &tmp);
	if (seg->start != start || seg->end != end) {
		rw_wunlock(&vm_phys_fictitious_reg_lock);
		panic(
		    "Unregistering not registered fictitious range [%#jx:%#jx]",
		    (uintmax_t)start, (uintmax_t)end);
	}
	RB_REMOVE(fict_tree, &vm_phys_fictitious_tree, seg);
	rw_wunlock(&vm_phys_fictitious_reg_lock);
	free(seg->first_page, M_FICT_PAGES);
	free(seg, M_FICT_PAGES);
}

/*
 * Free a contiguous, power of two-sized set of physical pages.
 *
 * The free page queues must be locked.
 */
void
vm_phys_free_pages(vm_page_t m, int order)
{
	struct vm_freelist *fl;
	struct vm_phys_seg *seg;
	vm_paddr_t pa;
	vm_page_t m_buddy;

	KASSERT(m->order == VM_NFREEORDER,
	    ("vm_phys_free_pages: page %p has unexpected order %d",
	    m, m->order));
	KASSERT(m->pool < VM_NFREEPOOL,
	    ("vm_phys_free_pages: page %p has unexpected pool %d",
	    m, m->pool));
	KASSERT(order < VM_NFREEORDER,
	    ("vm_phys_free_pages: order %d is out of range", order));
	seg = &vm_phys_segs[m->segind];
	vm_domain_free_assert_locked(VM_DOMAIN(seg->domain));
	if (order < VM_NFREEORDER - 1) {
		pa = VM_PAGE_TO_PHYS(m);
		do {
			pa ^= ((vm_paddr_t)1 << (PAGE_SHIFT + order));
			if (pa < seg->start || pa >= seg->end)
				break;
			m_buddy = &seg->first_page[atop(pa - seg->start)];
			if (m_buddy->order != order)
				break;
			fl = (*seg->free_queues)[m_buddy->pool];
			vm_freelist_rem(fl, m_buddy, order);
			if (m_buddy->pool != m->pool)
				vm_phys_set_pool(m->pool, m_buddy, order);
			order++;
			pa &= ~(((vm_paddr_t)1 << (PAGE_SHIFT + order)) - 1);
			m = &seg->first_page[atop(pa - seg->start)];
		} while (order < VM_NFREEORDER - 1);
	}
	fl = (*seg->free_queues)[m->pool];
	vm_freelist_add(fl, m, order, 1);
}

/*
 * Free a contiguous, arbitrarily sized set of physical pages.
 *
 * The free page queues must be locked.
 */
void
vm_phys_free_contig(vm_page_t m, u_long npages)
{
	u_int n;
	int order;

	/*
	 * Avoid unnecessary coalescing by freeing the pages in the largest
	 * possible power-of-two-sized subsets.
	 */
	vm_domain_free_assert_locked(vm_pagequeue_domain(m));
	for (;; npages -= n) {
		/*
		 * Unsigned "min" is used here so that "order" is assigned
		 * "VM_NFREEORDER - 1" when "m"'s physical address is zero
		 * or the low-order bits of its physical address are zero
		 * because the size of a physical address exceeds the size of
		 * a long.
		 */
		order = min(ffsl(VM_PAGE_TO_PHYS(m) >> PAGE_SHIFT) - 1,
		    VM_NFREEORDER - 1);
		n = 1 << order;
		if (npages < n)
			break;
		vm_phys_free_pages(m, order);
		m += n;
	}
	/* The residual "npages" is less than "1 << (VM_NFREEORDER - 1)". */
	for (; npages > 0; npages -= n) {
		order = flsl(npages) - 1;
		n = 1 << order;
		vm_phys_free_pages(m, order);
		m += n;
	}
}

/*
 * Scan physical memory between the specified addresses "low" and "high" for a
 * run of contiguous physical pages that satisfy the specified conditions, and
 * return the lowest page in the run.  The specified "alignment" determines
 * the alignment of the lowest physical page in the run.  If the specified
 * "boundary" is non-zero, then the run of physical pages cannot span a
 * physical address that is a multiple of "boundary".
 *
 * "npages" must be greater than zero.  Both "alignment" and "boundary" must
 * be a power of two.
 */
vm_page_t
vm_phys_scan_contig(int domain, u_long npages, vm_paddr_t low, vm_paddr_t high,
    u_long alignment, vm_paddr_t boundary, int options)
{
	vm_paddr_t pa_end;
	vm_page_t m_end, m_run, m_start;
	struct vm_phys_seg *seg;
	int segind;

	KASSERT(npages > 0, ("npages is 0"));
	KASSERT(powerof2(alignment), ("alignment is not a power of 2"));
	KASSERT(powerof2(boundary), ("boundary is not a power of 2"));
	if (low >= high)
		return (NULL);
	for (segind = 0; segind < vm_phys_nsegs; segind++) {
		seg = &vm_phys_segs[segind];
		if (seg->domain != domain)
			continue;
		if (seg->start >= high)
			break;
		if (low >= seg->end)
			continue;
		if (low <= seg->start)
			m_start = seg->first_page;
		else
			m_start = &seg->first_page[atop(low - seg->start)];
		if (high < seg->end)
			pa_end = high;
		else
			pa_end = seg->end;
		if (pa_end - VM_PAGE_TO_PHYS(m_start) < ptoa(npages))
			continue;
		m_end = &seg->first_page[atop(pa_end - seg->start)];
		m_run = vm_page_scan_contig(npages, m_start, m_end,
		    alignment, boundary, options);
		if (m_run != NULL)
			return (m_run);
	}
	return (NULL);
}

/*
 * Set the pool for a contiguous, power of two-sized set of physical pages. 
 */
void
vm_phys_set_pool(int pool, vm_page_t m, int order)
{
	vm_page_t m_tmp;

	for (m_tmp = m; m_tmp < &m[1 << order]; m_tmp++)
		m_tmp->pool = pool;
}

/*
 * Search for the given physical page "m" in the free lists.  If the search
 * succeeds, remove "m" from the free lists and return TRUE.  Otherwise, return
 * FALSE, indicating that "m" is not in the free lists.
 *
 * The free page queues must be locked.
 */
boolean_t
vm_phys_unfree_page(vm_page_t m)
{
	struct vm_freelist *fl;
	struct vm_phys_seg *seg;
	vm_paddr_t pa, pa_half;
	vm_page_t m_set, m_tmp;
	int order;

	/*
	 * First, find the contiguous, power of two-sized set of free
	 * physical pages containing the given physical page "m" and
	 * assign it to "m_set".
	 */
	seg = &vm_phys_segs[m->segind];
	vm_domain_free_assert_locked(VM_DOMAIN(seg->domain));
	for (m_set = m, order = 0; m_set->order == VM_NFREEORDER &&
	    order < VM_NFREEORDER - 1; ) {
		order++;
		pa = m->phys_addr & (~(vm_paddr_t)0 << (PAGE_SHIFT + order));
		if (pa >= seg->start)
			m_set = &seg->first_page[atop(pa - seg->start)];
		else
			return (FALSE);
	}
	if (m_set->order < order)
		return (FALSE);
	if (m_set->order == VM_NFREEORDER)
		return (FALSE);
	KASSERT(m_set->order < VM_NFREEORDER,
	    ("vm_phys_unfree_page: page %p has unexpected order %d",
	    m_set, m_set->order));

	/*
	 * Next, remove "m_set" from the free lists.  Finally, extract
	 * "m" from "m_set" using an iterative algorithm: While "m_set"
	 * is larger than a page, shrink "m_set" by returning the half
	 * of "m_set" that does not contain "m" to the free lists.
	 */
	fl = (*seg->free_queues)[m_set->pool];
	order = m_set->order;
	vm_freelist_rem(fl, m_set, order);
	while (order > 0) {
		order--;
		pa_half = m_set->phys_addr ^ (1 << (PAGE_SHIFT + order));
		if (m->phys_addr < pa_half)
			m_tmp = &seg->first_page[atop(pa_half - seg->start)];
		else {
			m_tmp = m_set;
			m_set = &seg->first_page[atop(pa_half - seg->start)];
		}
		vm_freelist_add(fl, m_tmp, order, 0);
	}
	KASSERT(m_set == m, ("vm_phys_unfree_page: fatal inconsistency"));
	return (TRUE);
}

/*
 * Allocate a contiguous set of physical pages of the given size
 * "npages" from the free lists.  All of the physical pages must be at
 * or above the given physical address "low" and below the given
 * physical address "high".  The given value "alignment" determines the
 * alignment of the first physical page in the set.  If the given value
 * "boundary" is non-zero, then the set of physical pages cannot cross
 * any physical address boundary that is a multiple of that value.  Both
 * "alignment" and "boundary" must be a power of two.
 */
vm_page_t
vm_phys_alloc_contig(int domain, u_long npages, vm_paddr_t low, vm_paddr_t high,
    u_long alignment, vm_paddr_t boundary)
{
	vm_paddr_t pa_end, pa_start;
	vm_page_t m_run;
	struct vm_phys_seg *seg;
	int segind;

	KASSERT(npages > 0, ("npages is 0"));
	KASSERT(powerof2(alignment), ("alignment is not a power of 2"));
	KASSERT(powerof2(boundary), ("boundary is not a power of 2"));
	vm_domain_free_assert_locked(VM_DOMAIN(domain));
	if (low >= high)
		return (NULL);
	m_run = NULL;
	for (segind = vm_phys_nsegs - 1; segind >= 0; segind--) {
		seg = &vm_phys_segs[segind];
		if (seg->start >= high || seg->domain != domain)
			continue;
		if (low >= seg->end)
			break;
		if (low <= seg->start)
			pa_start = seg->start;
		else
			pa_start = low;
		if (high < seg->end)
			pa_end = high;
		else
			pa_end = seg->end;
		if (pa_end - pa_start < ptoa(npages))
			continue;
		m_run = vm_phys_alloc_seg_contig(seg, npages, low, high,
		    alignment, boundary);
		if (m_run != NULL)
			break;
	}
	return (m_run);
}

/*
 * Allocate a run of contiguous physical pages from the free list for the
 * specified segment.
 */
static vm_page_t
vm_phys_alloc_seg_contig(struct vm_phys_seg *seg, u_long npages,
    vm_paddr_t low, vm_paddr_t high, u_long alignment, vm_paddr_t boundary)
{
	struct vm_freelist *fl;
	vm_paddr_t pa, pa_end, size;
	vm_page_t m, m_ret;
	u_long npages_end;
	int oind, order, pind;

	KASSERT(npages > 0, ("npages is 0"));
	KASSERT(powerof2(alignment), ("alignment is not a power of 2"));
	KASSERT(powerof2(boundary), ("boundary is not a power of 2"));
	vm_domain_free_assert_locked(VM_DOMAIN(seg->domain));
	/* Compute the queue that is the best fit for npages. */
	order = flsl(npages - 1);
	/* Search for a run satisfying the specified conditions. */
	size = npages << PAGE_SHIFT;
	for (oind = min(order, VM_NFREEORDER - 1); oind < VM_NFREEORDER;
	    oind++) {
		for (pind = 0; pind < VM_NFREEPOOL; pind++) {
			fl = (*seg->free_queues)[pind];
			TAILQ_FOREACH(m_ret, &fl[oind].pl, listq) {
				/*
				 * Is the size of this allocation request
				 * larger than the largest block size?
				 */
				if (order >= VM_NFREEORDER) {
					/*
					 * Determine if a sufficient number of
					 * subsequent blocks to satisfy the
					 * allocation request are free.
					 */
					pa = VM_PAGE_TO_PHYS(m_ret);
					pa_end = pa + size;
					if (pa_end < pa)
						continue;
					for (;;) {
						pa += 1 << (PAGE_SHIFT +
						    VM_NFREEORDER - 1);
						if (pa >= pa_end ||
						    pa < seg->start ||
						    pa >= seg->end)
							break;
						m = &seg->first_page[atop(pa -
						    seg->start)];
						if (m->order != VM_NFREEORDER -
						    1)
							break;
					}
					/* If not, go to the next block. */
					if (pa < pa_end)
						continue;
				}

				/*
				 * Determine if the blocks are within the
				 * given range, satisfy the given alignment,
				 * and do not cross the given boundary.
				 */
				pa = VM_PAGE_TO_PHYS(m_ret);
				pa_end = pa + size;
				if (pa >= low && pa_end <= high &&
				    (pa & (alignment - 1)) == 0 &&
				    rounddown2(pa ^ (pa_end - 1), boundary) == 0)
					goto done;
			}
		}
	}
	return (NULL);
done:
	for (m = m_ret; m < &m_ret[npages]; m = &m[1 << oind]) {
		fl = (*seg->free_queues)[m->pool];
		vm_freelist_rem(fl, m, oind);
		if (m->pool != VM_FREEPOOL_DEFAULT)
			vm_phys_set_pool(VM_FREEPOOL_DEFAULT, m, oind);
	}
	/* Return excess pages to the free lists. */
	npages_end = roundup2(npages, 1 << oind);
	if (npages < npages_end) {
		fl = (*seg->free_queues)[VM_FREEPOOL_DEFAULT];
		vm_phys_enq_range(&m_ret[npages], npages_end - npages, fl, 0);
	}
	return (m_ret);
}

#ifdef DDB
/*
 * Show the number of physical pages in each of the free lists.
 */
DB_SHOW_COMMAND(freepages, db_show_freepages)
{
	struct vm_freelist *fl;
	int flind, oind, pind, dom;

	for (dom = 0; dom < vm_ndomains; dom++) {
		db_printf("DOMAIN: %d\n", dom);
		for (flind = 0; flind < vm_nfreelists; flind++) {
			db_printf("FREE LIST %d:\n"
			    "\n  ORDER (SIZE)  |  NUMBER"
			    "\n              ", flind);
			for (pind = 0; pind < VM_NFREEPOOL; pind++)
				db_printf("  |  POOL %d", pind);
			db_printf("\n--            ");
			for (pind = 0; pind < VM_NFREEPOOL; pind++)
				db_printf("-- --      ");
			db_printf("--\n");
			for (oind = VM_NFREEORDER - 1; oind >= 0; oind--) {
				db_printf("  %2.2d (%6.6dK)", oind,
				    1 << (PAGE_SHIFT - 10 + oind));
				for (pind = 0; pind < VM_NFREEPOOL; pind++) {
				fl = vm_phys_free_queues[dom][flind][pind];
					db_printf("  |  %6.6d", fl[oind].lcnt);
				}
				db_printf("\n");
			}
			db_printf("\n");
		}
		db_printf("\n");
	}
}
#endif
