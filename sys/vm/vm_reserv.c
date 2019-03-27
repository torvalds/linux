/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002-2006 Rice University
 * Copyright (c) 2007-2011 Alan L. Cox <alc@cs.rice.edu>
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
 *	Superpage reservation management module
 *
 * Any external functions defined by this module are only to be used by the
 * virtual memory system.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_vm.h"

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/queue.h>
#include <sys/rwlock.h>
#include <sys/sbuf.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <sys/counter.h>
#include <sys/ktr.h>
#include <sys/vmmeter.h>
#include <sys/smp.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_pageout.h>
#include <vm/vm_phys.h>
#include <vm/vm_pagequeue.h>
#include <vm/vm_radix.h>
#include <vm/vm_reserv.h>

/*
 * The reservation system supports the speculative allocation of large physical
 * pages ("superpages").  Speculative allocation enables the fully automatic
 * utilization of superpages by the virtual memory system.  In other words, no
 * programmatic directives are required to use superpages.
 */

#if VM_NRESERVLEVEL > 0

#ifndef VM_LEVEL_0_ORDER_MAX
#define	VM_LEVEL_0_ORDER_MAX	VM_LEVEL_0_ORDER
#endif

/*
 * The number of small pages that are contained in a level 0 reservation
 */
#define	VM_LEVEL_0_NPAGES	(1 << VM_LEVEL_0_ORDER)
#define	VM_LEVEL_0_NPAGES_MAX	(1 << VM_LEVEL_0_ORDER_MAX)

/*
 * The number of bits by which a physical address is shifted to obtain the
 * reservation number
 */
#define	VM_LEVEL_0_SHIFT	(VM_LEVEL_0_ORDER + PAGE_SHIFT)

/*
 * The size of a level 0 reservation in bytes
 */
#define	VM_LEVEL_0_SIZE		(1 << VM_LEVEL_0_SHIFT)

/*
 * Computes the index of the small page underlying the given (object, pindex)
 * within the reservation's array of small pages.
 */
#define	VM_RESERV_INDEX(object, pindex)	\
    (((object)->pg_color + (pindex)) & (VM_LEVEL_0_NPAGES - 1))

/*
 * The size of a population map entry
 */
typedef	u_long		popmap_t;

/*
 * The number of bits in a population map entry
 */
#define	NBPOPMAP	(NBBY * sizeof(popmap_t))

/*
 * The number of population map entries in a reservation
 */
#define	NPOPMAP		howmany(VM_LEVEL_0_NPAGES, NBPOPMAP)
#define	NPOPMAP_MAX	howmany(VM_LEVEL_0_NPAGES_MAX, NBPOPMAP)

/*
 * Number of elapsed ticks before we update the LRU queue position.  Used
 * to reduce contention and churn on the list.
 */
#define	PARTPOPSLOP	1

/*
 * Clear a bit in the population map.
 */
static __inline void
popmap_clear(popmap_t popmap[], int i)
{

	popmap[i / NBPOPMAP] &= ~(1UL << (i % NBPOPMAP));
}

/*
 * Set a bit in the population map.
 */
static __inline void
popmap_set(popmap_t popmap[], int i)
{

	popmap[i / NBPOPMAP] |= 1UL << (i % NBPOPMAP);
}

/*
 * Is a bit in the population map clear?
 */
static __inline boolean_t
popmap_is_clear(popmap_t popmap[], int i)
{

	return ((popmap[i / NBPOPMAP] & (1UL << (i % NBPOPMAP))) == 0);
}

/*
 * Is a bit in the population map set?
 */
static __inline boolean_t
popmap_is_set(popmap_t popmap[], int i)
{

	return ((popmap[i / NBPOPMAP] & (1UL << (i % NBPOPMAP))) != 0);
}

/*
 * The reservation structure
 *
 * A reservation structure is constructed whenever a large physical page is
 * speculatively allocated to an object.  The reservation provides the small
 * physical pages for the range [pindex, pindex + VM_LEVEL_0_NPAGES) of offsets
 * within that object.  The reservation's "popcnt" tracks the number of these
 * small physical pages that are in use at any given time.  When and if the
 * reservation is not fully utilized, it appears in the queue of partially
 * populated reservations.  The reservation always appears on the containing
 * object's list of reservations.
 *
 * A partially populated reservation can be broken and reclaimed at any time.
 *
 * r - vm_reserv_lock
 * d - vm_reserv_domain_lock
 * o - vm_reserv_object_lock
 * c - constant after boot
 */
struct vm_reserv {
	struct mtx	lock;			/* reservation lock. */
	TAILQ_ENTRY(vm_reserv) partpopq;	/* (d) per-domain queue. */
	LIST_ENTRY(vm_reserv) objq;		/* (o, r) object queue */
	vm_object_t	object;			/* (o, r) containing object */
	vm_pindex_t	pindex;			/* (o, r) offset in object */
	vm_page_t	pages;			/* (c) first page  */
	uint16_t	domain;			/* (c) NUMA domain. */
	uint16_t	popcnt;			/* (r) # of pages in use */
	int		lasttick;		/* (r) last pop update tick. */
	char		inpartpopq;		/* (d) */
	popmap_t	popmap[NPOPMAP_MAX];	/* (r) bit vector, used pages */
};

#define	vm_reserv_lockptr(rv)		(&(rv)->lock)
#define	vm_reserv_assert_locked(rv)					\
	    mtx_assert(vm_reserv_lockptr(rv), MA_OWNED)
#define	vm_reserv_lock(rv)		mtx_lock(vm_reserv_lockptr(rv))
#define	vm_reserv_trylock(rv)		mtx_trylock(vm_reserv_lockptr(rv))
#define	vm_reserv_unlock(rv)		mtx_unlock(vm_reserv_lockptr(rv))

static struct mtx_padalign vm_reserv_domain_locks[MAXMEMDOM];

#define	vm_reserv_domain_lockptr(d)	&vm_reserv_domain_locks[(d)]
#define	vm_reserv_domain_lock(d)	mtx_lock(vm_reserv_domain_lockptr(d))
#define	vm_reserv_domain_unlock(d)	mtx_unlock(vm_reserv_domain_lockptr(d))

/*
 * The reservation array
 *
 * This array is analoguous in function to vm_page_array.  It differs in the
 * respect that it may contain a greater number of useful reservation
 * structures than there are (physical) superpages.  These "invalid"
 * reservation structures exist to trade-off space for time in the
 * implementation of vm_reserv_from_page().  Invalid reservation structures are
 * distinguishable from "valid" reservation structures by inspecting the
 * reservation's "pages" field.  Invalid reservation structures have a NULL
 * "pages" field.
 *
 * vm_reserv_from_page() maps a small (physical) page to an element of this
 * array by computing a physical reservation number from the page's physical
 * address.  The physical reservation number is used as the array index.
 *
 * An "active" reservation is a valid reservation structure that has a non-NULL
 * "object" field and a non-zero "popcnt" field.  In other words, every active
 * reservation belongs to a particular object.  Moreover, every active
 * reservation has an entry in the containing object's list of reservations.  
 */
static vm_reserv_t vm_reserv_array;

/*
 * The partially populated reservation queue
 *
 * This queue enables the fast recovery of an unused free small page from a
 * partially populated reservation.  The reservation at the head of this queue
 * is the least recently changed, partially populated reservation.
 *
 * Access to this queue is synchronized by the free page queue lock.
 */
static TAILQ_HEAD(, vm_reserv) vm_rvq_partpop[MAXMEMDOM];

static SYSCTL_NODE(_vm, OID_AUTO, reserv, CTLFLAG_RD, 0, "Reservation Info");

static counter_u64_t vm_reserv_broken = EARLY_COUNTER;
SYSCTL_COUNTER_U64(_vm_reserv, OID_AUTO, broken, CTLFLAG_RD,
    &vm_reserv_broken, "Cumulative number of broken reservations");

static counter_u64_t vm_reserv_freed = EARLY_COUNTER;
SYSCTL_COUNTER_U64(_vm_reserv, OID_AUTO, freed, CTLFLAG_RD,
    &vm_reserv_freed, "Cumulative number of freed reservations");

static int sysctl_vm_reserv_fullpop(SYSCTL_HANDLER_ARGS);

SYSCTL_PROC(_vm_reserv, OID_AUTO, fullpop, CTLTYPE_INT | CTLFLAG_RD, NULL, 0,
    sysctl_vm_reserv_fullpop, "I", "Current number of full reservations");

static int sysctl_vm_reserv_partpopq(SYSCTL_HANDLER_ARGS);

SYSCTL_OID(_vm_reserv, OID_AUTO, partpopq, CTLTYPE_STRING | CTLFLAG_RD, NULL, 0,
    sysctl_vm_reserv_partpopq, "A", "Partially populated reservation queues");

static counter_u64_t vm_reserv_reclaimed = EARLY_COUNTER;
SYSCTL_COUNTER_U64(_vm_reserv, OID_AUTO, reclaimed, CTLFLAG_RD,
    &vm_reserv_reclaimed, "Cumulative number of reclaimed reservations");

/*
 * The object lock pool is used to synchronize the rvq.  We can not use a
 * pool mutex because it is required before malloc works.
 *
 * The "hash" function could be made faster without divide and modulo.
 */
#define	VM_RESERV_OBJ_LOCK_COUNT	MAXCPU

struct mtx_padalign vm_reserv_object_mtx[VM_RESERV_OBJ_LOCK_COUNT];

#define	vm_reserv_object_lock_idx(object)			\
	    (((uintptr_t)object / sizeof(*object)) % VM_RESERV_OBJ_LOCK_COUNT)
#define	vm_reserv_object_lock_ptr(object)			\
	    &vm_reserv_object_mtx[vm_reserv_object_lock_idx((object))]
#define	vm_reserv_object_lock(object)				\
	    mtx_lock(vm_reserv_object_lock_ptr((object)))
#define	vm_reserv_object_unlock(object)				\
	    mtx_unlock(vm_reserv_object_lock_ptr((object)))

static void		vm_reserv_break(vm_reserv_t rv);
static void		vm_reserv_depopulate(vm_reserv_t rv, int index);
static vm_reserv_t	vm_reserv_from_page(vm_page_t m);
static boolean_t	vm_reserv_has_pindex(vm_reserv_t rv,
			    vm_pindex_t pindex);
static void		vm_reserv_populate(vm_reserv_t rv, int index);
static void		vm_reserv_reclaim(vm_reserv_t rv);

/*
 * Returns the current number of full reservations.
 *
 * Since the number of full reservations is computed without acquiring the
 * free page queue lock, the returned value may be inexact.
 */
static int
sysctl_vm_reserv_fullpop(SYSCTL_HANDLER_ARGS)
{
	vm_paddr_t paddr;
	struct vm_phys_seg *seg;
	vm_reserv_t rv;
	int fullpop, segind;

	fullpop = 0;
	for (segind = 0; segind < vm_phys_nsegs; segind++) {
		seg = &vm_phys_segs[segind];
		paddr = roundup2(seg->start, VM_LEVEL_0_SIZE);
		while (paddr + VM_LEVEL_0_SIZE <= seg->end) {
			rv = &vm_reserv_array[paddr >> VM_LEVEL_0_SHIFT];
			fullpop += rv->popcnt == VM_LEVEL_0_NPAGES;
			paddr += VM_LEVEL_0_SIZE;
		}
	}
	return (sysctl_handle_int(oidp, &fullpop, 0, req));
}

/*
 * Describes the current state of the partially populated reservation queue.
 */
static int
sysctl_vm_reserv_partpopq(SYSCTL_HANDLER_ARGS)
{
	struct sbuf sbuf;
	vm_reserv_t rv;
	int counter, error, domain, level, unused_pages;

	error = sysctl_wire_old_buffer(req, 0);
	if (error != 0)
		return (error);
	sbuf_new_for_sysctl(&sbuf, NULL, 128, req);
	sbuf_printf(&sbuf, "\nDOMAIN    LEVEL     SIZE  NUMBER\n\n");
	for (domain = 0; domain < vm_ndomains; domain++) {
		for (level = -1; level <= VM_NRESERVLEVEL - 2; level++) {
			counter = 0;
			unused_pages = 0;
			vm_reserv_domain_lock(domain);
			TAILQ_FOREACH(rv, &vm_rvq_partpop[domain], partpopq) {
				counter++;
				unused_pages += VM_LEVEL_0_NPAGES - rv->popcnt;
			}
			vm_reserv_domain_unlock(domain);
			sbuf_printf(&sbuf, "%6d, %7d, %6dK, %6d\n",
			    domain, level,
			    unused_pages * ((int)PAGE_SIZE / 1024), counter);
		}
	}
	error = sbuf_finish(&sbuf);
	sbuf_delete(&sbuf);
	return (error);
}

/*
 * Remove a reservation from the object's objq.
 */
static void
vm_reserv_remove(vm_reserv_t rv)
{
	vm_object_t object;

	vm_reserv_assert_locked(rv);
	CTR5(KTR_VM, "%s: rv %p object %p popcnt %d inpartpop %d",
	    __FUNCTION__, rv, rv->object, rv->popcnt, rv->inpartpopq);
	KASSERT(rv->object != NULL,
	    ("vm_reserv_remove: reserv %p is free", rv));
	KASSERT(!rv->inpartpopq,
	    ("vm_reserv_remove: reserv %p's inpartpopq is TRUE", rv));
	object = rv->object;
	vm_reserv_object_lock(object);
	LIST_REMOVE(rv, objq);
	rv->object = NULL;
	vm_reserv_object_unlock(object);
}

/*
 * Insert a new reservation into the object's objq.
 */
static void
vm_reserv_insert(vm_reserv_t rv, vm_object_t object, vm_pindex_t pindex)
{
	int i;

	vm_reserv_assert_locked(rv);
	CTR6(KTR_VM,
	    "%s: rv %p(%p) object %p new %p popcnt %d",
	    __FUNCTION__, rv, rv->pages, rv->object, object,
	   rv->popcnt);
	KASSERT(rv->object == NULL,
	    ("vm_reserv_insert: reserv %p isn't free", rv));
	KASSERT(rv->popcnt == 0,
	    ("vm_reserv_insert: reserv %p's popcnt is corrupted", rv));
	KASSERT(!rv->inpartpopq,
	    ("vm_reserv_insert: reserv %p's inpartpopq is TRUE", rv));
	for (i = 0; i < NPOPMAP; i++)
		KASSERT(rv->popmap[i] == 0,
		    ("vm_reserv_insert: reserv %p's popmap is corrupted", rv));
	vm_reserv_object_lock(object);
	rv->pindex = pindex;
	rv->object = object;
	rv->lasttick = ticks;
	LIST_INSERT_HEAD(&object->rvq, rv, objq);
	vm_reserv_object_unlock(object);
}

/*
 * Reduces the given reservation's population count.  If the population count
 * becomes zero, the reservation is destroyed.  Additionally, moves the
 * reservation to the tail of the partially populated reservation queue if the
 * population count is non-zero.
 */
static void
vm_reserv_depopulate(vm_reserv_t rv, int index)
{
	struct vm_domain *vmd;

	vm_reserv_assert_locked(rv);
	CTR5(KTR_VM, "%s: rv %p object %p popcnt %d inpartpop %d",
	    __FUNCTION__, rv, rv->object, rv->popcnt, rv->inpartpopq);
	KASSERT(rv->object != NULL,
	    ("vm_reserv_depopulate: reserv %p is free", rv));
	KASSERT(popmap_is_set(rv->popmap, index),
	    ("vm_reserv_depopulate: reserv %p's popmap[%d] is clear", rv,
	    index));
	KASSERT(rv->popcnt > 0,
	    ("vm_reserv_depopulate: reserv %p's popcnt is corrupted", rv));
	KASSERT(rv->domain < vm_ndomains,
	    ("vm_reserv_depopulate: reserv %p's domain is corrupted %d",
	    rv, rv->domain));
	if (rv->popcnt == VM_LEVEL_0_NPAGES) {
		KASSERT(rv->pages->psind == 1,
		    ("vm_reserv_depopulate: reserv %p is already demoted",
		    rv));
		rv->pages->psind = 0;
	}
	popmap_clear(rv->popmap, index);
	rv->popcnt--;
	if ((unsigned)(ticks - rv->lasttick) >= PARTPOPSLOP ||
	    rv->popcnt == 0) {
		vm_reserv_domain_lock(rv->domain);
		if (rv->inpartpopq) {
			TAILQ_REMOVE(&vm_rvq_partpop[rv->domain], rv, partpopq);
			rv->inpartpopq = FALSE;
		}
		if (rv->popcnt != 0) {
			rv->inpartpopq = TRUE;
			TAILQ_INSERT_TAIL(&vm_rvq_partpop[rv->domain], rv, partpopq);
		}
		vm_reserv_domain_unlock(rv->domain);
		rv->lasttick = ticks;
	}
	vmd = VM_DOMAIN(rv->domain);
	if (rv->popcnt == 0) {
		vm_reserv_remove(rv);
		vm_domain_free_lock(vmd);
		vm_phys_free_pages(rv->pages, VM_LEVEL_0_ORDER);
		vm_domain_free_unlock(vmd);
		counter_u64_add(vm_reserv_freed, 1);
	}
	vm_domain_freecnt_inc(vmd, 1);
}

/*
 * Returns the reservation to which the given page might belong.
 */
static __inline vm_reserv_t
vm_reserv_from_page(vm_page_t m)
{

	return (&vm_reserv_array[VM_PAGE_TO_PHYS(m) >> VM_LEVEL_0_SHIFT]);
}

/*
 * Returns an existing reservation or NULL and initialized successor pointer.
 */
static vm_reserv_t
vm_reserv_from_object(vm_object_t object, vm_pindex_t pindex,
    vm_page_t mpred, vm_page_t *msuccp)
{
	vm_reserv_t rv;
	vm_page_t msucc;

	msucc = NULL;
	if (mpred != NULL) {
		KASSERT(mpred->object == object,
		    ("vm_reserv_from_object: object doesn't contain mpred"));
		KASSERT(mpred->pindex < pindex,
		    ("vm_reserv_from_object: mpred doesn't precede pindex"));
		rv = vm_reserv_from_page(mpred);
		if (rv->object == object && vm_reserv_has_pindex(rv, pindex))
			goto found;
		msucc = TAILQ_NEXT(mpred, listq);
	} else
		msucc = TAILQ_FIRST(&object->memq);
	if (msucc != NULL) {
		KASSERT(msucc->pindex > pindex,
		    ("vm_reserv_from_object: msucc doesn't succeed pindex"));
		rv = vm_reserv_from_page(msucc);
		if (rv->object == object && vm_reserv_has_pindex(rv, pindex))
			goto found;
	}
	rv = NULL;

found:
	*msuccp = msucc;

	return (rv);
}

/*
 * Returns TRUE if the given reservation contains the given page index and
 * FALSE otherwise.
 */
static __inline boolean_t
vm_reserv_has_pindex(vm_reserv_t rv, vm_pindex_t pindex)
{

	return (((pindex - rv->pindex) & ~(VM_LEVEL_0_NPAGES - 1)) == 0);
}

/*
 * Increases the given reservation's population count.  Moves the reservation
 * to the tail of the partially populated reservation queue.
 *
 * The free page queue must be locked.
 */
static void
vm_reserv_populate(vm_reserv_t rv, int index)
{

	vm_reserv_assert_locked(rv);
	CTR5(KTR_VM, "%s: rv %p object %p popcnt %d inpartpop %d",
	    __FUNCTION__, rv, rv->object, rv->popcnt, rv->inpartpopq);
	KASSERT(rv->object != NULL,
	    ("vm_reserv_populate: reserv %p is free", rv));
	KASSERT(popmap_is_clear(rv->popmap, index),
	    ("vm_reserv_populate: reserv %p's popmap[%d] is set", rv,
	    index));
	KASSERT(rv->popcnt < VM_LEVEL_0_NPAGES,
	    ("vm_reserv_populate: reserv %p is already full", rv));
	KASSERT(rv->pages->psind == 0,
	    ("vm_reserv_populate: reserv %p is already promoted", rv));
	KASSERT(rv->domain < vm_ndomains,
	    ("vm_reserv_populate: reserv %p's domain is corrupted %d",
	    rv, rv->domain));
	popmap_set(rv->popmap, index);
	rv->popcnt++;
	if ((unsigned)(ticks - rv->lasttick) < PARTPOPSLOP &&
	    rv->inpartpopq && rv->popcnt != VM_LEVEL_0_NPAGES)
		return;
	rv->lasttick = ticks;
	vm_reserv_domain_lock(rv->domain);
	if (rv->inpartpopq) {
		TAILQ_REMOVE(&vm_rvq_partpop[rv->domain], rv, partpopq);
		rv->inpartpopq = FALSE;
	}
	if (rv->popcnt < VM_LEVEL_0_NPAGES) {
		rv->inpartpopq = TRUE;
		TAILQ_INSERT_TAIL(&vm_rvq_partpop[rv->domain], rv, partpopq);
	} else {
		KASSERT(rv->pages->psind == 0,
		    ("vm_reserv_populate: reserv %p is already promoted",
		    rv));
		rv->pages->psind = 1;
	}
	vm_reserv_domain_unlock(rv->domain);
}

/*
 * Attempts to allocate a contiguous set of physical pages from existing
 * reservations.  See vm_reserv_alloc_contig() for a description of the
 * function's parameters.
 *
 * The page "mpred" must immediately precede the offset "pindex" within the
 * specified object.
 *
 * The object must be locked.
 */
vm_page_t
vm_reserv_extend_contig(int req, vm_object_t object, vm_pindex_t pindex,
    int domain, u_long npages, vm_paddr_t low, vm_paddr_t high,
    u_long alignment, vm_paddr_t boundary, vm_page_t mpred)
{
	struct vm_domain *vmd;
	vm_paddr_t pa, size;
	vm_page_t m, msucc;
	vm_reserv_t rv;
	int i, index;

	VM_OBJECT_ASSERT_WLOCKED(object);
	KASSERT(npages != 0, ("vm_reserv_alloc_contig: npages is 0"));

	/*
	 * Is a reservation fundamentally impossible?
	 */
	if (pindex < VM_RESERV_INDEX(object, pindex) ||
	    pindex + npages > object->size || object->resident_page_count == 0)
		return (NULL);

	/*
	 * All reservations of a particular size have the same alignment.
	 * Assuming that the first page is allocated from a reservation, the
	 * least significant bits of its physical address can be determined
	 * from its offset from the beginning of the reservation and the size
	 * of the reservation.
	 *
	 * Could the specified index within a reservation of the smallest
	 * possible size satisfy the alignment and boundary requirements?
	 */
	pa = VM_RESERV_INDEX(object, pindex) << PAGE_SHIFT;
	if ((pa & (alignment - 1)) != 0)
		return (NULL);
	size = npages << PAGE_SHIFT;
	if (((pa ^ (pa + size - 1)) & ~(boundary - 1)) != 0)
		return (NULL);

	/*
	 * Look for an existing reservation.
	 */
	rv = vm_reserv_from_object(object, pindex, mpred, &msucc);
	if (rv == NULL)
		return (NULL);
	KASSERT(object != kernel_object || rv->domain == domain,
	    ("vm_reserv_extend_contig: Domain mismatch from reservation."));
	index = VM_RESERV_INDEX(object, pindex);
	/* Does the allocation fit within the reservation? */
	if (index + npages > VM_LEVEL_0_NPAGES)
		return (NULL);
	domain = rv->domain;
	vmd = VM_DOMAIN(domain);
	vm_reserv_lock(rv);
	if (rv->object != object)
		goto out;
	m = &rv->pages[index];
	pa = VM_PAGE_TO_PHYS(m);
	if (pa < low || pa + size > high || (pa & (alignment - 1)) != 0 ||
	    ((pa ^ (pa + size - 1)) & ~(boundary - 1)) != 0)
		goto out;
	/* Handle vm_page_rename(m, new_object, ...). */
	for (i = 0; i < npages; i++) {
		if (popmap_is_set(rv->popmap, index + i))
			goto out;
	}
	if (!vm_domain_allocate(vmd, req, npages))
		goto out;
	for (i = 0; i < npages; i++)
		vm_reserv_populate(rv, index + i);
	vm_reserv_unlock(rv);
	return (m);

out:
	vm_reserv_unlock(rv);
	return (NULL);
}

/*
 * Allocates a contiguous set of physical pages of the given size "npages"
 * from newly created reservations.  All of the physical pages
 * must be at or above the given physical address "low" and below the given
 * physical address "high".  The given value "alignment" determines the
 * alignment of the first physical page in the set.  If the given value
 * "boundary" is non-zero, then the set of physical pages cannot cross any
 * physical address boundary that is a multiple of that value.  Both
 * "alignment" and "boundary" must be a power of two.
 *
 * Callers should first invoke vm_reserv_extend_contig() to attempt an
 * allocation from existing reservations.
 *
 * The page "mpred" must immediately precede the offset "pindex" within the
 * specified object.
 *
 * The object and free page queue must be locked.
 */
vm_page_t
vm_reserv_alloc_contig(int req, vm_object_t object, vm_pindex_t pindex, int domain,
    u_long npages, vm_paddr_t low, vm_paddr_t high, u_long alignment,
    vm_paddr_t boundary, vm_page_t mpred)
{
	struct vm_domain *vmd;
	vm_paddr_t pa, size;
	vm_page_t m, m_ret, msucc;
	vm_pindex_t first, leftcap, rightcap;
	vm_reserv_t rv;
	u_long allocpages, maxpages, minpages;
	int i, index, n;

	VM_OBJECT_ASSERT_WLOCKED(object);
	KASSERT(npages != 0, ("vm_reserv_alloc_contig: npages is 0"));

	/*
	 * Is a reservation fundamentally impossible?
	 */
	if (pindex < VM_RESERV_INDEX(object, pindex) ||
	    pindex + npages > object->size)
		return (NULL);

	/*
	 * All reservations of a particular size have the same alignment.
	 * Assuming that the first page is allocated from a reservation, the
	 * least significant bits of its physical address can be determined
	 * from its offset from the beginning of the reservation and the size
	 * of the reservation.
	 *
	 * Could the specified index within a reservation of the smallest
	 * possible size satisfy the alignment and boundary requirements?
	 */
	pa = VM_RESERV_INDEX(object, pindex) << PAGE_SHIFT;
	if ((pa & (alignment - 1)) != 0)
		return (NULL);
	size = npages << PAGE_SHIFT;
	if (((pa ^ (pa + size - 1)) & ~(boundary - 1)) != 0)
		return (NULL);

	/*
	 * Callers should've extended an existing reservation prior to
	 * calling this function.  If a reservation exists it is
	 * incompatible with the allocation.
	 */
	rv = vm_reserv_from_object(object, pindex, mpred, &msucc);
	if (rv != NULL)
		return (NULL);

	/*
	 * Could at least one reservation fit between the first index to the
	 * left that can be used ("leftcap") and the first index to the right
	 * that cannot be used ("rightcap")?
	 *
	 * We must synchronize with the reserv object lock to protect the
	 * pindex/object of the resulting reservations against rename while
	 * we are inspecting.
	 */
	first = pindex - VM_RESERV_INDEX(object, pindex);
	minpages = VM_RESERV_INDEX(object, pindex) + npages;
	maxpages = roundup2(minpages, VM_LEVEL_0_NPAGES);
	allocpages = maxpages;
	vm_reserv_object_lock(object);
	if (mpred != NULL) {
		if ((rv = vm_reserv_from_page(mpred))->object != object)
			leftcap = mpred->pindex + 1;
		else
			leftcap = rv->pindex + VM_LEVEL_0_NPAGES;
		if (leftcap > first) {
			vm_reserv_object_unlock(object);
			return (NULL);
		}
	}
	if (msucc != NULL) {
		if ((rv = vm_reserv_from_page(msucc))->object != object)
			rightcap = msucc->pindex;
		else
			rightcap = rv->pindex;
		if (first + maxpages > rightcap) {
			if (maxpages == VM_LEVEL_0_NPAGES) {
				vm_reserv_object_unlock(object);
				return (NULL);
			}

			/*
			 * At least one reservation will fit between "leftcap"
			 * and "rightcap".  However, a reservation for the
			 * last of the requested pages will not fit.  Reduce
			 * the size of the upcoming allocation accordingly.
			 */
			allocpages = minpages;
		}
	}
	vm_reserv_object_unlock(object);

	/*
	 * Would the last new reservation extend past the end of the object?
	 */
	if (first + maxpages > object->size) {
		/*
		 * Don't allocate the last new reservation if the object is a
		 * vnode or backed by another object that is a vnode. 
		 */
		if (object->type == OBJT_VNODE ||
		    (object->backing_object != NULL &&
		    object->backing_object->type == OBJT_VNODE)) {
			if (maxpages == VM_LEVEL_0_NPAGES)
				return (NULL);
			allocpages = minpages;
		}
		/* Speculate that the object may grow. */
	}

	/*
	 * Allocate the physical pages.  The alignment and boundary specified
	 * for this allocation may be different from the alignment and
	 * boundary specified for the requested pages.  For instance, the
	 * specified index may not be the first page within the first new
	 * reservation.
	 */
	m = NULL;
	vmd = VM_DOMAIN(domain);
	if (vm_domain_allocate(vmd, req, npages)) {
		vm_domain_free_lock(vmd);
		m = vm_phys_alloc_contig(domain, allocpages, low, high,
		    ulmax(alignment, VM_LEVEL_0_SIZE),
		    boundary > VM_LEVEL_0_SIZE ? boundary : 0);
		vm_domain_free_unlock(vmd);
		if (m == NULL) {
			vm_domain_freecnt_inc(vmd, npages);
			return (NULL);
		}
	} else
		return (NULL);
	KASSERT(vm_phys_domain(m) == domain,
	    ("vm_reserv_alloc_contig: Page domain does not match requested."));

	/*
	 * The allocated physical pages always begin at a reservation
	 * boundary, but they do not always end at a reservation boundary.
	 * Initialize every reservation that is completely covered by the
	 * allocated physical pages.
	 */
	m_ret = NULL;
	index = VM_RESERV_INDEX(object, pindex);
	do {
		rv = vm_reserv_from_page(m);
		KASSERT(rv->pages == m,
		    ("vm_reserv_alloc_contig: reserv %p's pages is corrupted",
		    rv));
		vm_reserv_lock(rv);
		vm_reserv_insert(rv, object, first);
		n = ulmin(VM_LEVEL_0_NPAGES - index, npages);
		for (i = 0; i < n; i++)
			vm_reserv_populate(rv, index + i);
		npages -= n;
		if (m_ret == NULL) {
			m_ret = &rv->pages[index];
			index = 0;
		}
		vm_reserv_unlock(rv);
		m += VM_LEVEL_0_NPAGES;
		first += VM_LEVEL_0_NPAGES;
		allocpages -= VM_LEVEL_0_NPAGES;
	} while (allocpages >= VM_LEVEL_0_NPAGES);
	return (m_ret);
}

/*
 * Attempts to extend an existing reservation and allocate the page to the
 * object.
 *
 * The page "mpred" must immediately precede the offset "pindex" within the
 * specified object.
 *
 * The object must be locked.
 */
vm_page_t
vm_reserv_extend(int req, vm_object_t object, vm_pindex_t pindex, int domain,
    vm_page_t mpred)
{
	struct vm_domain *vmd;
	vm_page_t m, msucc;
	vm_reserv_t rv;
	int index;

	VM_OBJECT_ASSERT_WLOCKED(object);

	/*
	 * Could a reservation currently exist?
	 */
	if (pindex < VM_RESERV_INDEX(object, pindex) ||
	    pindex >= object->size || object->resident_page_count == 0)
		return (NULL);

	/*
	 * Look for an existing reservation.
	 */
	rv = vm_reserv_from_object(object, pindex, mpred, &msucc);
	if (rv == NULL)
		return (NULL);

	KASSERT(object != kernel_object || rv->domain == domain,
	    ("vm_reserv_extend: Domain mismatch from reservation."));
	domain = rv->domain;
	vmd = VM_DOMAIN(domain);
	index = VM_RESERV_INDEX(object, pindex);
	m = &rv->pages[index];
	vm_reserv_lock(rv);
	/* Handle reclaim race. */
	if (rv->object != object ||
	    /* Handle vm_page_rename(m, new_object, ...). */
	    popmap_is_set(rv->popmap, index)) {
		m = NULL;
		goto out;
	}
	if (vm_domain_allocate(vmd, req, 1) == 0)
		m = NULL;
	else
		vm_reserv_populate(rv, index);
out:
	vm_reserv_unlock(rv);

	return (m);
}

/*
 * Attempts to allocate a new reservation for the object, and allocates a
 * page from that reservation.  Callers should first invoke vm_reserv_extend()
 * to attempt an allocation from an existing reservation.
 *
 * The page "mpred" must immediately precede the offset "pindex" within the
 * specified object.
 *
 * The object and free page queue must be locked.
 */
vm_page_t
vm_reserv_alloc_page(int req, vm_object_t object, vm_pindex_t pindex, int domain,
    vm_page_t mpred)
{
	struct vm_domain *vmd;
	vm_page_t m, msucc;
	vm_pindex_t first, leftcap, rightcap;
	vm_reserv_t rv;
	int index;

	VM_OBJECT_ASSERT_WLOCKED(object);

	/*
	 * Is a reservation fundamentally impossible?
	 */
	if (pindex < VM_RESERV_INDEX(object, pindex) ||
	    pindex >= object->size)
		return (NULL);

	/*
	 * Callers should've extended an existing reservation prior to
	 * calling this function.  If a reservation exists it is
	 * incompatible with the allocation.
	 */
	rv = vm_reserv_from_object(object, pindex, mpred, &msucc);
	if (rv != NULL)
		return (NULL);

	/*
	 * Could a reservation fit between the first index to the left that
	 * can be used and the first index to the right that cannot be used?
	 *
	 * We must synchronize with the reserv object lock to protect the
	 * pindex/object of the resulting reservations against rename while
	 * we are inspecting.
	 */
	first = pindex - VM_RESERV_INDEX(object, pindex);
	vm_reserv_object_lock(object);
	if (mpred != NULL) {
		if ((rv = vm_reserv_from_page(mpred))->object != object)
			leftcap = mpred->pindex + 1;
		else
			leftcap = rv->pindex + VM_LEVEL_0_NPAGES;
		if (leftcap > first) {
			vm_reserv_object_unlock(object);
			return (NULL);
		}
	}
	if (msucc != NULL) {
		if ((rv = vm_reserv_from_page(msucc))->object != object)
			rightcap = msucc->pindex;
		else
			rightcap = rv->pindex;
		if (first + VM_LEVEL_0_NPAGES > rightcap) {
			vm_reserv_object_unlock(object);
			return (NULL);
		}
	}
	vm_reserv_object_unlock(object);

	/*
	 * Would a new reservation extend past the end of the object? 
	 */
	if (first + VM_LEVEL_0_NPAGES > object->size) {
		/*
		 * Don't allocate a new reservation if the object is a vnode or
		 * backed by another object that is a vnode. 
		 */
		if (object->type == OBJT_VNODE ||
		    (object->backing_object != NULL &&
		    object->backing_object->type == OBJT_VNODE))
			return (NULL);
		/* Speculate that the object may grow. */
	}

	/*
	 * Allocate and populate the new reservation.
	 */
	m = NULL;
	vmd = VM_DOMAIN(domain);
	if (vm_domain_allocate(vmd, req, 1)) {
		vm_domain_free_lock(vmd);
		m = vm_phys_alloc_pages(domain, VM_FREEPOOL_DEFAULT,
		    VM_LEVEL_0_ORDER);
		vm_domain_free_unlock(vmd);
		if (m == NULL) {
			vm_domain_freecnt_inc(vmd, 1);
			return (NULL);
		}
	} else
		return (NULL);
	rv = vm_reserv_from_page(m);
	vm_reserv_lock(rv);
	KASSERT(rv->pages == m,
	    ("vm_reserv_alloc_page: reserv %p's pages is corrupted", rv));
	vm_reserv_insert(rv, object, first);
	index = VM_RESERV_INDEX(object, pindex);
	vm_reserv_populate(rv, index);
	vm_reserv_unlock(rv);

	return (&rv->pages[index]);
}

/*
 * Breaks the given reservation.  All free pages in the reservation
 * are returned to the physical memory allocator.  The reservation's
 * population count and map are reset to their initial state.
 *
 * The given reservation must not be in the partially populated reservation
 * queue.  The free page queue lock must be held.
 */
static void
vm_reserv_break(vm_reserv_t rv)
{
	int begin_zeroes, hi, i, lo;

	vm_reserv_assert_locked(rv);
	CTR5(KTR_VM, "%s: rv %p object %p popcnt %d inpartpop %d",
	    __FUNCTION__, rv, rv->object, rv->popcnt, rv->inpartpopq);
	vm_reserv_remove(rv);
	rv->pages->psind = 0;
	i = hi = 0;
	do {
		/* Find the next 0 bit.  Any previous 0 bits are < "hi". */
		lo = ffsl(~(((1UL << hi) - 1) | rv->popmap[i]));
		if (lo == 0) {
			/* Redundantly clears bits < "hi". */
			rv->popmap[i] = 0;
			rv->popcnt -= NBPOPMAP - hi;
			while (++i < NPOPMAP) {
				lo = ffsl(~rv->popmap[i]);
				if (lo == 0) {
					rv->popmap[i] = 0;
					rv->popcnt -= NBPOPMAP;
				} else
					break;
			}
			if (i == NPOPMAP)
				break;
			hi = 0;
		}
		KASSERT(lo > 0, ("vm_reserv_break: lo is %d", lo));
		/* Convert from ffsl() to ordinary bit numbering. */
		lo--;
		if (lo > 0) {
			/* Redundantly clears bits < "hi". */
			rv->popmap[i] &= ~((1UL << lo) - 1);
			rv->popcnt -= lo - hi;
		}
		begin_zeroes = NBPOPMAP * i + lo;
		/* Find the next 1 bit. */
		do
			hi = ffsl(rv->popmap[i]);
		while (hi == 0 && ++i < NPOPMAP);
		if (i != NPOPMAP)
			/* Convert from ffsl() to ordinary bit numbering. */
			hi--;
		vm_domain_free_lock(VM_DOMAIN(rv->domain));
		vm_phys_free_contig(&rv->pages[begin_zeroes], NBPOPMAP * i +
		    hi - begin_zeroes);
		vm_domain_free_unlock(VM_DOMAIN(rv->domain));
	} while (i < NPOPMAP);
	KASSERT(rv->popcnt == 0,
	    ("vm_reserv_break: reserv %p's popcnt is corrupted", rv));
	counter_u64_add(vm_reserv_broken, 1);
}

/*
 * Breaks all reservations belonging to the given object.
 */
void
vm_reserv_break_all(vm_object_t object)
{
	vm_reserv_t rv;

	/*
	 * This access of object->rvq is unsynchronized so that the
	 * object rvq lock can nest after the domain_free lock.  We
	 * must check for races in the results.  However, the object
	 * lock prevents new additions, so we are guaranteed that when
	 * it returns NULL the object is properly empty.
	 */
	while ((rv = LIST_FIRST(&object->rvq)) != NULL) {
		vm_reserv_lock(rv);
		/* Reclaim race. */
		if (rv->object != object) {
			vm_reserv_unlock(rv);
			continue;
		}
		vm_reserv_domain_lock(rv->domain);
		if (rv->inpartpopq) {
			TAILQ_REMOVE(&vm_rvq_partpop[rv->domain], rv, partpopq);
			rv->inpartpopq = FALSE;
		}
		vm_reserv_domain_unlock(rv->domain);
		vm_reserv_break(rv);
		vm_reserv_unlock(rv);
	}
}

/*
 * Frees the given page if it belongs to a reservation.  Returns TRUE if the
 * page is freed and FALSE otherwise.
 *
 * The free page queue lock must be held.
 */
boolean_t
vm_reserv_free_page(vm_page_t m)
{
	vm_reserv_t rv;
	boolean_t ret;

	rv = vm_reserv_from_page(m);
	if (rv->object == NULL)
		return (FALSE);
	vm_reserv_lock(rv);
	/* Re-validate after lock. */
	if (rv->object != NULL) {
		vm_reserv_depopulate(rv, m - rv->pages);
		ret = TRUE;
	} else
		ret = FALSE;
	vm_reserv_unlock(rv);

	return (ret);
}

/*
 * Initializes the reservation management system.  Specifically, initializes
 * the reservation array.
 *
 * Requires that vm_page_array and first_page are initialized!
 */
void
vm_reserv_init(void)
{
	vm_paddr_t paddr;
	struct vm_phys_seg *seg;
	struct vm_reserv *rv;
	int i, segind;

	/*
	 * Initialize the reservation array.  Specifically, initialize the
	 * "pages" field for every element that has an underlying superpage.
	 */
	for (segind = 0; segind < vm_phys_nsegs; segind++) {
		seg = &vm_phys_segs[segind];
		paddr = roundup2(seg->start, VM_LEVEL_0_SIZE);
		while (paddr + VM_LEVEL_0_SIZE <= seg->end) {
			rv = &vm_reserv_array[paddr >> VM_LEVEL_0_SHIFT];
			rv->pages = PHYS_TO_VM_PAGE(paddr);
			rv->domain = seg->domain;
			mtx_init(&rv->lock, "vm reserv", NULL, MTX_DEF);
			paddr += VM_LEVEL_0_SIZE;
		}
	}
	for (i = 0; i < MAXMEMDOM; i++) {
		mtx_init(&vm_reserv_domain_locks[i], "VM reserv domain", NULL,
		    MTX_DEF);
		TAILQ_INIT(&vm_rvq_partpop[i]);
	}

	for (i = 0; i < VM_RESERV_OBJ_LOCK_COUNT; i++)
		mtx_init(&vm_reserv_object_mtx[i], "resv obj lock", NULL,
		    MTX_DEF);
}

/*
 * Returns true if the given page belongs to a reservation and that page is
 * free.  Otherwise, returns false.
 */
bool
vm_reserv_is_page_free(vm_page_t m)
{
	vm_reserv_t rv;

	rv = vm_reserv_from_page(m);
	if (rv->object == NULL)
		return (false);
	return (popmap_is_clear(rv->popmap, m - rv->pages));
}

/*
 * If the given page belongs to a reservation, returns the level of that
 * reservation.  Otherwise, returns -1.
 */
int
vm_reserv_level(vm_page_t m)
{
	vm_reserv_t rv;

	rv = vm_reserv_from_page(m);
	return (rv->object != NULL ? 0 : -1);
}

/*
 * Returns a reservation level if the given page belongs to a fully populated
 * reservation and -1 otherwise.
 */
int
vm_reserv_level_iffullpop(vm_page_t m)
{
	vm_reserv_t rv;

	rv = vm_reserv_from_page(m);
	return (rv->popcnt == VM_LEVEL_0_NPAGES ? 0 : -1);
}

/*
 * Breaks the given partially populated reservation, releasing its free pages
 * to the physical memory allocator.
 *
 * The free page queue lock must be held.
 */
static void
vm_reserv_reclaim(vm_reserv_t rv)
{

	vm_reserv_assert_locked(rv);
	CTR5(KTR_VM, "%s: rv %p object %p popcnt %d inpartpop %d",
	    __FUNCTION__, rv, rv->object, rv->popcnt, rv->inpartpopq);
	vm_reserv_domain_lock(rv->domain);
	KASSERT(rv->inpartpopq,
	    ("vm_reserv_reclaim: reserv %p's inpartpopq is FALSE", rv));
	KASSERT(rv->domain < vm_ndomains,
	    ("vm_reserv_reclaim: reserv %p's domain is corrupted %d",
	    rv, rv->domain));
	TAILQ_REMOVE(&vm_rvq_partpop[rv->domain], rv, partpopq);
	rv->inpartpopq = FALSE;
	vm_reserv_domain_unlock(rv->domain);
	vm_reserv_break(rv);
	counter_u64_add(vm_reserv_reclaimed, 1);
}

/*
 * Breaks the reservation at the head of the partially populated reservation
 * queue, releasing its free pages to the physical memory allocator.  Returns
 * TRUE if a reservation is broken and FALSE otherwise.
 *
 * The free page queue lock must be held.
 */
boolean_t
vm_reserv_reclaim_inactive(int domain)
{
	vm_reserv_t rv;

	while ((rv = TAILQ_FIRST(&vm_rvq_partpop[domain])) != NULL) {
		vm_reserv_lock(rv);
		if (rv != TAILQ_FIRST(&vm_rvq_partpop[domain])) {
			vm_reserv_unlock(rv);
			continue;
		}
		vm_reserv_reclaim(rv);
		vm_reserv_unlock(rv);
		return (TRUE);
	}
	return (FALSE);
}

/*
 * Searches the partially populated reservation queue for the least recently
 * changed reservation with free pages that satisfy the given request for
 * contiguous physical memory.  If a satisfactory reservation is found, it is
 * broken.  Returns TRUE if a reservation is broken and FALSE otherwise.
 *
 * The free page queue lock must be held.
 */
boolean_t
vm_reserv_reclaim_contig(int domain, u_long npages, vm_paddr_t low,
    vm_paddr_t high, u_long alignment, vm_paddr_t boundary)
{
	vm_paddr_t pa, size;
	vm_reserv_t rv, rvn;
	int hi, i, lo, low_index, next_free;

	if (npages > VM_LEVEL_0_NPAGES - 1)
		return (FALSE);
	size = npages << PAGE_SHIFT;
	vm_reserv_domain_lock(domain);
again:
	for (rv = TAILQ_FIRST(&vm_rvq_partpop[domain]); rv != NULL; rv = rvn) {
		rvn = TAILQ_NEXT(rv, partpopq);
		pa = VM_PAGE_TO_PHYS(&rv->pages[VM_LEVEL_0_NPAGES - 1]);
		if (pa + PAGE_SIZE - size < low) {
			/* This entire reservation is too low; go to next. */
			continue;
		}
		pa = VM_PAGE_TO_PHYS(&rv->pages[0]);
		if (pa + size > high) {
			/* This entire reservation is too high; go to next. */
			continue;
		}
		if (vm_reserv_trylock(rv) == 0) {
			vm_reserv_domain_unlock(domain);
			vm_reserv_lock(rv);
			if (!rv->inpartpopq) {
				vm_reserv_domain_lock(domain);
				if (!rvn->inpartpopq)
					goto again;
				continue;
			}
		} else
			vm_reserv_domain_unlock(domain);
		if (pa < low) {
			/* Start the search for free pages at "low". */
			low_index = (low + PAGE_MASK - pa) >> PAGE_SHIFT;
			i = low_index / NBPOPMAP;
			hi = low_index % NBPOPMAP;
		} else
			i = hi = 0;
		do {
			/* Find the next free page. */
			lo = ffsl(~(((1UL << hi) - 1) | rv->popmap[i]));
			while (lo == 0 && ++i < NPOPMAP)
				lo = ffsl(~rv->popmap[i]);
			if (i == NPOPMAP)
				break;
			/* Convert from ffsl() to ordinary bit numbering. */
			lo--;
			next_free = NBPOPMAP * i + lo;
			pa = VM_PAGE_TO_PHYS(&rv->pages[next_free]);
			KASSERT(pa >= low,
			    ("vm_reserv_reclaim_contig: pa is too low"));
			if (pa + size > high) {
				/* The rest of this reservation is too high. */
				break;
			} else if ((pa & (alignment - 1)) != 0 ||
			    ((pa ^ (pa + size - 1)) & ~(boundary - 1)) != 0) {
				/*
				 * The current page doesn't meet the alignment
				 * and/or boundary requirements.  Continue
				 * searching this reservation until the rest
				 * of its free pages are either excluded or
				 * exhausted.
				 */
				hi = lo + 1;
				if (hi >= NBPOPMAP) {
					hi = 0;
					i++;
				}
				continue;
			}
			/* Find the next used page. */
			hi = ffsl(rv->popmap[i] & ~((1UL << lo) - 1));
			while (hi == 0 && ++i < NPOPMAP) {
				if ((NBPOPMAP * i - next_free) * PAGE_SIZE >=
				    size) {
					vm_reserv_reclaim(rv);
					vm_reserv_unlock(rv);
					return (TRUE);
				}
				hi = ffsl(rv->popmap[i]);
			}
			/* Convert from ffsl() to ordinary bit numbering. */
			if (i != NPOPMAP)
				hi--;
			if ((NBPOPMAP * i + hi - next_free) * PAGE_SIZE >=
			    size) {
				vm_reserv_reclaim(rv);
				vm_reserv_unlock(rv);
				return (TRUE);
			}
		} while (i < NPOPMAP);
		vm_reserv_unlock(rv);
		vm_reserv_domain_lock(domain);
		if (rvn != NULL && !rvn->inpartpopq)
			goto again;
	}
	vm_reserv_domain_unlock(domain);
	return (FALSE);
}

/*
 * Transfers the reservation underlying the given page to a new object.
 *
 * The object must be locked.
 */
void
vm_reserv_rename(vm_page_t m, vm_object_t new_object, vm_object_t old_object,
    vm_pindex_t old_object_offset)
{
	vm_reserv_t rv;

	VM_OBJECT_ASSERT_WLOCKED(new_object);
	rv = vm_reserv_from_page(m);
	if (rv->object == old_object) {
		vm_reserv_lock(rv);
		CTR6(KTR_VM,
		    "%s: rv %p object %p new %p popcnt %d inpartpop %d",
		    __FUNCTION__, rv, rv->object, new_object, rv->popcnt,
		    rv->inpartpopq);
		if (rv->object == old_object) {
			vm_reserv_object_lock(old_object);
			rv->object = NULL;
			LIST_REMOVE(rv, objq);
			vm_reserv_object_unlock(old_object);
			vm_reserv_object_lock(new_object);
			rv->object = new_object;
			rv->pindex -= old_object_offset;
			LIST_INSERT_HEAD(&new_object->rvq, rv, objq);
			vm_reserv_object_unlock(new_object);
		}
		vm_reserv_unlock(rv);
	}
}

/*
 * Returns the size (in bytes) of a reservation of the specified level.
 */
int
vm_reserv_size(int level)
{

	switch (level) {
	case 0:
		return (VM_LEVEL_0_SIZE);
	case -1:
		return (PAGE_SIZE);
	default:
		return (0);
	}
}

/*
 * Allocates the virtual and physical memory required by the reservation
 * management system's data structures, in particular, the reservation array.
 */
vm_paddr_t
vm_reserv_startup(vm_offset_t *vaddr, vm_paddr_t end, vm_paddr_t high_water)
{
	vm_paddr_t new_end;
	size_t size;

	/*
	 * Calculate the size (in bytes) of the reservation array.  Round up
	 * from "high_water" because every small page is mapped to an element
	 * in the reservation array based on its physical address.  Thus, the
	 * number of elements in the reservation array can be greater than the
	 * number of superpages. 
	 */
	size = howmany(high_water, VM_LEVEL_0_SIZE) * sizeof(struct vm_reserv);

	/*
	 * Allocate and map the physical memory for the reservation array.  The
	 * next available virtual address is returned by reference.
	 */
	new_end = end - round_page(size);
	vm_reserv_array = (void *)(uintptr_t)pmap_map(vaddr, new_end, end,
	    VM_PROT_READ | VM_PROT_WRITE);
	bzero(vm_reserv_array, size);

	/*
	 * Return the next available physical address.
	 */
	return (new_end);
}

/*
 * Initializes the reservation management system.  Specifically, initializes
 * the reservation counters.
 */
static void
vm_reserv_counter_init(void *unused)
{

	vm_reserv_freed = counter_u64_alloc(M_WAITOK); 
	vm_reserv_broken = counter_u64_alloc(M_WAITOK); 
	vm_reserv_reclaimed = counter_u64_alloc(M_WAITOK); 
}
SYSINIT(vm_reserv_counter_init, SI_SUB_CPU, SI_ORDER_ANY,
    vm_reserv_counter_init, NULL);

/*
 * Returns the superpage containing the given page.
 */
vm_page_t
vm_reserv_to_superpage(vm_page_t m)
{
	vm_reserv_t rv;

	VM_OBJECT_ASSERT_LOCKED(m->object);
	rv = vm_reserv_from_page(m);
	if (rv->object == m->object && rv->popcnt == VM_LEVEL_0_NPAGES)
		m = rv->pages;
	else
		m = NULL;

	return (m);
}

#endif	/* VM_NRESERVLEVEL > 0 */
