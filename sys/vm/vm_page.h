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
 *	from: @(#)vm_page.h	8.2 (Berkeley) 12/13/93
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
 *
 * $FreeBSD$
 */

/*
 *	Resident memory system definitions.
 */

#ifndef	_VM_PAGE_
#define	_VM_PAGE_

#include <vm/pmap.h>

/*
 *	Management of resident (logical) pages.
 *
 *	A small structure is kept for each resident
 *	page, indexed by page number.  Each structure
 *	is an element of several collections:
 *
 *		A radix tree used to quickly
 *		perform object/offset lookups
 *
 *		A list of all pages for a given object,
 *		so they can be quickly deactivated at
 *		time of deallocation.
 *
 *		An ordered list of pages due for pageout.
 *
 *	In addition, the structure contains the object
 *	and offset to which this page belongs (for pageout),
 *	and sundry status bits.
 *
 *	In general, operations on this structure's mutable fields are
 *	synchronized using either one of or a combination of the lock on the
 *	object that the page belongs to (O), the page lock (P),
 *	the per-domain lock for the free queues (F), or the page's queue
 *	lock (Q).  The physical address of a page is used to select its page
 *	lock from a pool.  The queue lock for a page depends on the value of
 *	its queue field and described in detail below.  If a field is
 *	annotated below with two of these locks, then holding either lock is
 *	sufficient for read access, but both locks are required for write
 *	access.  An annotation of (C) indicates that the field is immutable.
 *
 *	In contrast, the synchronization of accesses to the page's
 *	dirty field is machine dependent (M).  In the
 *	machine-independent layer, the lock on the object that the
 *	page belongs to must be held in order to operate on the field.
 *	However, the pmap layer is permitted to set all bits within
 *	the field without holding that lock.  If the underlying
 *	architecture does not support atomic read-modify-write
 *	operations on the field's type, then the machine-independent
 *	layer uses a 32-bit atomic on the aligned 32-bit word that
 *	contains the dirty field.  In the machine-independent layer,
 *	the implementation of read-modify-write operations on the
 *	field is encapsulated in vm_page_clear_dirty_mask().
 *
 *	The page structure contains two counters which prevent page reuse.
 *	Both counters are protected by the page lock (P).  The hold
 *	counter counts transient references obtained via a pmap lookup, and
 *	is also used to prevent page reclamation in situations where it is
 *	undesirable to block other accesses to the page.  The wire counter
 *	is used to implement mlock(2) and is non-zero for pages containing
 *	kernel memory.  Pages that are wired or held will not be reclaimed
 *	or laundered by the page daemon, but are treated differently during
 *	a page queue scan: held pages remain at their position in the queue,
 *	while wired pages are removed from the queue and must later be
 *	re-enqueued appropriately by the unwiring thread.  It is legal to
 *	call vm_page_free() on a held page; doing so causes it to be removed
 *	from its object and page queue, and the page is released to the
 *	allocator once the last hold reference is dropped.  In contrast,
 *	wired pages may not be freed.
 *
 *	In some pmap implementations, the wire count of a page table page is
 *	used to track the number of populated entries.
 *
 *	The busy lock is an embedded reader-writer lock which protects the
 *	page's contents and identity (i.e., its <object, pindex> tuple) and
 *	interlocks with the object lock (O).  In particular, a page may be
 *	busied or unbusied only with the object write lock held.  To avoid
 *	bloating the page structure, the busy lock lacks some of the
 *	features available to the kernel's general-purpose synchronization
 *	primitives.  As a result, busy lock ordering rules are not verified,
 *	lock recursion is not detected, and an attempt to xbusy a busy page
 *	or sbusy an xbusy page results will trigger a panic rather than
 *	causing the thread to block.  vm_page_sleep_if_busy() can be used to
 *	sleep until the page's busy state changes, after which the caller
 *	must re-lookup the page and re-evaluate its state.
 *
 *	The queue field is the index of the page queue containing the
 *	page, or PQ_NONE if the page is not enqueued.  The queue lock of a
 *	page is the page queue lock corresponding to the page queue index,
 *	or the page lock (P) for the page if it is not enqueued.  To modify
 *	the queue field, the queue lock for the old value of the field must
 *	be held.  It is invalid for a page's queue field to transition
 *	between two distinct page queue indices.  That is, when updating
 *	the queue field, either the new value or the old value must be
 *	PQ_NONE.
 *
 *	To avoid contention on page queue locks, page queue operations
 *	(enqueue, dequeue, requeue) are batched using per-CPU queues.
 *	A deferred operation is requested by inserting an entry into a
 *	batch queue; the entry is simply a pointer to the page, and the
 *	request type is encoded in the page's aflags field using the values
 *	in PGA_QUEUE_STATE_MASK.  The type-stability of struct vm_pages is
 *	crucial to this scheme since the processing of entries in a given
 *	batch queue may be deferred indefinitely.  In particular, a page
 *	may be freed before its pending batch queue entries have been
 *	processed.  The page lock (P) must be held to schedule a batched
 *	queue operation, and the page queue lock must be held in order to
 *	process batch queue entries for the page queue.
 */

#if PAGE_SIZE == 4096
#define VM_PAGE_BITS_ALL 0xffu
typedef uint8_t vm_page_bits_t;
#elif PAGE_SIZE == 8192
#define VM_PAGE_BITS_ALL 0xffffu
typedef uint16_t vm_page_bits_t;
#elif PAGE_SIZE == 16384
#define VM_PAGE_BITS_ALL 0xffffffffu
typedef uint32_t vm_page_bits_t;
#elif PAGE_SIZE == 32768
#define VM_PAGE_BITS_ALL 0xfffffffffffffffflu
typedef uint64_t vm_page_bits_t;
#endif

struct vm_page {
	union {
		TAILQ_ENTRY(vm_page) q; /* page queue or free list (Q) */
		struct {
			SLIST_ENTRY(vm_page) ss; /* private slists */
			void *pv;
		} s;
		struct {
			u_long p;
			u_long v;
		} memguard;
	} plinks;
	TAILQ_ENTRY(vm_page) listq;	/* pages in same object (O) */
	vm_object_t object;		/* which object am I in (O,P) */
	vm_pindex_t pindex;		/* offset into object (O,P) */
	vm_paddr_t phys_addr;		/* physical address of page (C) */
	struct md_page md;		/* machine dependent stuff */
	u_int wire_count;		/* wired down maps refs (P) */
	volatile u_int busy_lock;	/* busy owners lock */
	uint16_t hold_count;		/* page hold count (P) */
	uint16_t flags;			/* page PG_* flags (P) */
	uint8_t aflags;			/* access is atomic */
	uint8_t oflags;			/* page VPO_* flags (O) */
	uint8_t queue;			/* page queue index (Q) */
	int8_t psind;			/* pagesizes[] index (O) */
	int8_t segind;			/* vm_phys segment index (C) */
	uint8_t	order;			/* index of the buddy queue (F) */
	uint8_t pool;			/* vm_phys freepool index (F) */
	u_char	act_count;		/* page usage count (P) */
	/* NOTE that these must support one bit per DEV_BSIZE in a page */
	/* so, on normal X86 kernels, they must be at least 8 bits wide */
	vm_page_bits_t valid;		/* map of valid DEV_BSIZE chunks (O) */
	vm_page_bits_t dirty;		/* map of dirty DEV_BSIZE chunks (M) */
};

/*
 * Page flags stored in oflags:
 *
 * Access to these page flags is synchronized by the lock on the object
 * containing the page (O).
 *
 * Note: VPO_UNMANAGED (used by OBJT_DEVICE, OBJT_PHYS and OBJT_SG)
 * 	 indicates that the page is not under PV management but
 * 	 otherwise should be treated as a normal page.  Pages not
 * 	 under PV management cannot be paged out via the
 * 	 object/vm_page_t because there is no knowledge of their pte
 * 	 mappings, and such pages are also not on any PQ queue.
 *
 */
#define	VPO_KMEM_EXEC	0x01		/* kmem mapping allows execution */
#define	VPO_SWAPSLEEP	0x02		/* waiting for swap to finish */
#define	VPO_UNMANAGED	0x04		/* no PV management for page */
#define	VPO_SWAPINPROG	0x08		/* swap I/O in progress on page */
#define	VPO_NOSYNC	0x10		/* do not collect for syncer */

/*
 * Busy page implementation details.
 * The algorithm is taken mostly by rwlock(9) and sx(9) locks implementation,
 * even if the support for owner identity is removed because of size
 * constraints.  Checks on lock recursion are then not possible, while the
 * lock assertions effectiveness is someway reduced.
 */
#define	VPB_BIT_SHARED		0x01
#define	VPB_BIT_EXCLUSIVE	0x02
#define	VPB_BIT_WAITERS		0x04
#define	VPB_BIT_FLAGMASK						\
	(VPB_BIT_SHARED | VPB_BIT_EXCLUSIVE | VPB_BIT_WAITERS)

#define	VPB_SHARERS_SHIFT	3
#define	VPB_SHARERS(x)							\
	(((x) & ~VPB_BIT_FLAGMASK) >> VPB_SHARERS_SHIFT)
#define	VPB_SHARERS_WORD(x)	((x) << VPB_SHARERS_SHIFT | VPB_BIT_SHARED)
#define	VPB_ONE_SHARER		(1 << VPB_SHARERS_SHIFT)

#define	VPB_SINGLE_EXCLUSIVER	VPB_BIT_EXCLUSIVE

#define	VPB_UNBUSIED		VPB_SHARERS_WORD(0)

#define	PQ_NONE		255
#define	PQ_INACTIVE	0
#define	PQ_ACTIVE	1
#define	PQ_LAUNDRY	2
#define	PQ_UNSWAPPABLE	3
#define	PQ_COUNT	4

#ifndef VM_PAGE_HAVE_PGLIST
TAILQ_HEAD(pglist, vm_page);
#define VM_PAGE_HAVE_PGLIST
#endif
SLIST_HEAD(spglist, vm_page);

#ifdef _KERNEL
extern vm_page_t bogus_page;
#endif	/* _KERNEL */

extern struct mtx_padalign pa_lock[];

#if defined(__arm__)
#define	PDRSHIFT	PDR_SHIFT
#elif !defined(PDRSHIFT)
#define PDRSHIFT	21
#endif

#define	pa_index(pa)	((pa) >> PDRSHIFT)
#define	PA_LOCKPTR(pa)	((struct mtx *)(&pa_lock[pa_index(pa) % PA_LOCK_COUNT]))
#define	PA_LOCKOBJPTR(pa)	((struct lock_object *)PA_LOCKPTR((pa)))
#define	PA_LOCK(pa)	mtx_lock(PA_LOCKPTR(pa))
#define	PA_TRYLOCK(pa)	mtx_trylock(PA_LOCKPTR(pa))
#define	PA_UNLOCK(pa)	mtx_unlock(PA_LOCKPTR(pa))
#define	PA_UNLOCK_COND(pa) 			\
	do {		   			\
		if ((pa) != 0) {		\
			PA_UNLOCK((pa));	\
			(pa) = 0;		\
		}				\
	} while (0)

#define	PA_LOCK_ASSERT(pa, a)	mtx_assert(PA_LOCKPTR(pa), (a))

#if defined(KLD_MODULE) && !defined(KLD_TIED)
#define	vm_page_lock(m)		vm_page_lock_KBI((m), LOCK_FILE, LOCK_LINE)
#define	vm_page_unlock(m)	vm_page_unlock_KBI((m), LOCK_FILE, LOCK_LINE)
#define	vm_page_trylock(m)	vm_page_trylock_KBI((m), LOCK_FILE, LOCK_LINE)
#else	/* !KLD_MODULE */
#define	vm_page_lockptr(m)	(PA_LOCKPTR(VM_PAGE_TO_PHYS((m))))
#define	vm_page_lock(m)		mtx_lock(vm_page_lockptr((m)))
#define	vm_page_unlock(m)	mtx_unlock(vm_page_lockptr((m)))
#define	vm_page_trylock(m)	mtx_trylock(vm_page_lockptr((m)))
#endif
#if defined(INVARIANTS)
#define	vm_page_assert_locked(m)		\
    vm_page_assert_locked_KBI((m), __FILE__, __LINE__)
#define	vm_page_lock_assert(m, a)		\
    vm_page_lock_assert_KBI((m), (a), __FILE__, __LINE__)
#else
#define	vm_page_assert_locked(m)
#define	vm_page_lock_assert(m, a)
#endif

/*
 * The vm_page's aflags are updated using atomic operations.  To set or clear
 * these flags, the functions vm_page_aflag_set() and vm_page_aflag_clear()
 * must be used.  Neither these flags nor these functions are part of the KBI.
 *
 * PGA_REFERENCED may be cleared only if the page is locked.  It is set by
 * both the MI and MD VM layers.  However, kernel loadable modules should not
 * directly set this flag.  They should call vm_page_reference() instead.
 *
 * PGA_WRITEABLE is set exclusively on managed pages by pmap_enter().
 * When it does so, the object must be locked, or the page must be
 * exclusive busied.  The MI VM layer must never access this flag
 * directly.  Instead, it should call pmap_page_is_write_mapped().
 *
 * PGA_EXECUTABLE may be set by pmap routines, and indicates that a page has
 * at least one executable mapping.  It is not consumed by the MI VM layer.
 *
 * PGA_ENQUEUED is set and cleared when a page is inserted into or removed
 * from a page queue, respectively.  It determines whether the plinks.q field
 * of the page is valid.  To set or clear this flag, the queue lock for the
 * page must be held: the page queue lock corresponding to the page's "queue"
 * field if its value is not PQ_NONE, and the page lock otherwise.
 *
 * PGA_DEQUEUE is set when the page is scheduled to be dequeued from a page
 * queue, and cleared when the dequeue request is processed.  A page may
 * have PGA_DEQUEUE set and PGA_ENQUEUED cleared, for instance if a dequeue
 * is requested after the page is scheduled to be enqueued but before it is
 * actually inserted into the page queue.  For allocated pages, the page lock
 * must be held to set this flag, but it may be set by vm_page_free_prep()
 * without the page lock held.  The page queue lock must be held to clear the
 * PGA_DEQUEUE flag.
 *
 * PGA_REQUEUE is set when the page is scheduled to be enqueued or requeued
 * in its page queue.  The page lock must be held to set this flag, and the
 * queue lock for the page must be held to clear it.
 *
 * PGA_REQUEUE_HEAD is a special flag for enqueuing pages near the head of
 * the inactive queue, thus bypassing LRU.  The page lock must be held to
 * set this flag, and the queue lock for the page must be held to clear it.
 */
#define	PGA_WRITEABLE	0x01		/* page may be mapped writeable */
#define	PGA_REFERENCED	0x02		/* page has been referenced */
#define	PGA_EXECUTABLE	0x04		/* page may be mapped executable */
#define	PGA_ENQUEUED	0x08		/* page is enqueued in a page queue */
#define	PGA_DEQUEUE	0x10		/* page is due to be dequeued */
#define	PGA_REQUEUE	0x20		/* page is due to be requeued */
#define	PGA_REQUEUE_HEAD 0x40		/* page requeue should bypass LRU */

#define	PGA_QUEUE_STATE_MASK	(PGA_ENQUEUED | PGA_DEQUEUE | PGA_REQUEUE | \
				PGA_REQUEUE_HEAD)

/*
 * Page flags.  If changed at any other time than page allocation or
 * freeing, the modification must be protected by the vm_page lock.
 */
#define	PG_FICTITIOUS	0x0004		/* physical page doesn't exist */
#define	PG_ZERO		0x0008		/* page is zeroed */
#define	PG_MARKER	0x0010		/* special queue marker page */
#define	PG_NODUMP	0x0080		/* don't include this page in a dump */
#define	PG_UNHOLDFREE	0x0100		/* delayed free of a held page */

/*
 * Misc constants.
 */
#define ACT_DECLINE		1
#define ACT_ADVANCE		3
#define ACT_INIT		5
#define ACT_MAX			64

#ifdef _KERNEL

#include <sys/systm.h>

#include <machine/atomic.h>

/*
 * Each pageable resident page falls into one of five lists:
 *
 *	free
 *		Available for allocation now.
 *
 *	inactive
 *		Low activity, candidates for reclamation.
 *		This list is approximately LRU ordered.
 *
 *	laundry
 *		This is the list of pages that should be
 *		paged out next.
 *
 *	unswappable
 *		Dirty anonymous pages that cannot be paged
 *		out because no swap device is configured.
 *
 *	active
 *		Pages that are "active", i.e., they have been
 *		recently referenced.
 *
 */

extern vm_page_t vm_page_array;		/* First resident page in table */
extern long vm_page_array_size;		/* number of vm_page_t's */
extern long first_page;			/* first physical page number */

#define VM_PAGE_TO_PHYS(entry)	((entry)->phys_addr)

/*
 * PHYS_TO_VM_PAGE() returns the vm_page_t object that represents a memory
 * page to which the given physical address belongs. The correct vm_page_t
 * object is returned for addresses that are not page-aligned.
 */
vm_page_t PHYS_TO_VM_PAGE(vm_paddr_t pa);

/*
 * Page allocation parameters for vm_page for the functions
 * vm_page_alloc(), vm_page_grab(), vm_page_alloc_contig() and
 * vm_page_alloc_freelist().  Some functions support only a subset
 * of the flags, and ignore others, see the flags legend.
 *
 * The meaning of VM_ALLOC_ZERO differs slightly between the vm_page_alloc*()
 * and the vm_page_grab*() functions.  See these functions for details.
 *
 * Bits 0 - 1 define class.
 * Bits 2 - 15 dedicated for flags.
 * Legend:
 * (a) - vm_page_alloc() supports the flag.
 * (c) - vm_page_alloc_contig() supports the flag.
 * (f) - vm_page_alloc_freelist() supports the flag.
 * (g) - vm_page_grab() supports the flag.
 * (p) - vm_page_grab_pages() supports the flag.
 * Bits above 15 define the count of additional pages that the caller
 * intends to allocate.
 */
#define VM_ALLOC_NORMAL		0
#define VM_ALLOC_INTERRUPT	1
#define VM_ALLOC_SYSTEM		2
#define	VM_ALLOC_CLASS_MASK	3
#define	VM_ALLOC_WAITOK		0x0008	/* (acf) Sleep and retry */
#define	VM_ALLOC_WAITFAIL	0x0010	/* (acf) Sleep and return error */
#define	VM_ALLOC_WIRED		0x0020	/* (acfgp) Allocate a wired page */
#define	VM_ALLOC_ZERO		0x0040	/* (acfgp) Allocate a prezeroed page */
#define	VM_ALLOC_NOOBJ		0x0100	/* (acg) No associated object */
#define	VM_ALLOC_NOBUSY		0x0200	/* (acgp) Do not excl busy the page */
#define	VM_ALLOC_IGN_SBUSY	0x1000	/* (gp) Ignore shared busy flag */
#define	VM_ALLOC_NODUMP		0x2000	/* (ag) don't include in dump */
#define	VM_ALLOC_SBUSY		0x4000	/* (acgp) Shared busy the page */
#define	VM_ALLOC_NOWAIT		0x8000	/* (acfgp) Do not sleep */
#define	VM_ALLOC_COUNT_SHIFT	16
#define	VM_ALLOC_COUNT(count)	((count) << VM_ALLOC_COUNT_SHIFT)

#ifdef M_NOWAIT
static inline int
malloc2vm_flags(int malloc_flags)
{
	int pflags;

	KASSERT((malloc_flags & M_USE_RESERVE) == 0 ||
	    (malloc_flags & M_NOWAIT) != 0,
	    ("M_USE_RESERVE requires M_NOWAIT"));
	pflags = (malloc_flags & M_USE_RESERVE) != 0 ? VM_ALLOC_INTERRUPT :
	    VM_ALLOC_SYSTEM;
	if ((malloc_flags & M_ZERO) != 0)
		pflags |= VM_ALLOC_ZERO;
	if ((malloc_flags & M_NODUMP) != 0)
		pflags |= VM_ALLOC_NODUMP;
	if ((malloc_flags & M_NOWAIT))
		pflags |= VM_ALLOC_NOWAIT;
	if ((malloc_flags & M_WAITOK))
		pflags |= VM_ALLOC_WAITOK;
	return (pflags);
}
#endif

/*
 * Predicates supported by vm_page_ps_test():
 *
 *	PS_ALL_DIRTY is true only if the entire (super)page is dirty.
 *	However, it can be spuriously false when the (super)page has become
 *	dirty in the pmap but that information has not been propagated to the
 *	machine-independent layer.
 */
#define	PS_ALL_DIRTY	0x1
#define	PS_ALL_VALID	0x2
#define	PS_NONE_BUSY	0x4

void vm_page_busy_downgrade(vm_page_t m);
void vm_page_busy_sleep(vm_page_t m, const char *msg, bool nonshared);
void vm_page_flash(vm_page_t m);
void vm_page_hold(vm_page_t mem);
void vm_page_unhold(vm_page_t mem);
void vm_page_free(vm_page_t m);
void vm_page_free_zero(vm_page_t m);

void vm_page_activate (vm_page_t);
void vm_page_advise(vm_page_t m, int advice);
vm_page_t vm_page_alloc(vm_object_t, vm_pindex_t, int);
vm_page_t vm_page_alloc_domain(vm_object_t, vm_pindex_t, int, int);
vm_page_t vm_page_alloc_after(vm_object_t, vm_pindex_t, int, vm_page_t);
vm_page_t vm_page_alloc_domain_after(vm_object_t, vm_pindex_t, int, int,
    vm_page_t);
vm_page_t vm_page_alloc_contig(vm_object_t object, vm_pindex_t pindex, int req,
    u_long npages, vm_paddr_t low, vm_paddr_t high, u_long alignment,
    vm_paddr_t boundary, vm_memattr_t memattr);
vm_page_t vm_page_alloc_contig_domain(vm_object_t object,
    vm_pindex_t pindex, int domain, int req, u_long npages, vm_paddr_t low,
    vm_paddr_t high, u_long alignment, vm_paddr_t boundary,
    vm_memattr_t memattr);
vm_page_t vm_page_alloc_freelist(int, int);
vm_page_t vm_page_alloc_freelist_domain(int, int, int);
bool vm_page_blacklist_add(vm_paddr_t pa, bool verbose);
void vm_page_change_lock(vm_page_t m, struct mtx **mtx);
vm_page_t vm_page_grab (vm_object_t, vm_pindex_t, int);
int vm_page_grab_pages(vm_object_t object, vm_pindex_t pindex, int allocflags,
    vm_page_t *ma, int count);
void vm_page_deactivate(vm_page_t);
void vm_page_deactivate_noreuse(vm_page_t);
void vm_page_dequeue(vm_page_t m);
void vm_page_dequeue_deferred(vm_page_t m);
void vm_page_drain_pqbatch(void);
vm_page_t vm_page_find_least(vm_object_t, vm_pindex_t);
bool vm_page_free_prep(vm_page_t m);
vm_page_t vm_page_getfake(vm_paddr_t paddr, vm_memattr_t memattr);
void vm_page_initfake(vm_page_t m, vm_paddr_t paddr, vm_memattr_t memattr);
int vm_page_insert (vm_page_t, vm_object_t, vm_pindex_t);
void vm_page_launder(vm_page_t m);
vm_page_t vm_page_lookup (vm_object_t, vm_pindex_t);
vm_page_t vm_page_next(vm_page_t m);
int vm_page_pa_tryrelock(pmap_t, vm_paddr_t, vm_paddr_t *);
struct vm_pagequeue *vm_page_pagequeue(vm_page_t m);
vm_page_t vm_page_prev(vm_page_t m);
bool vm_page_ps_test(vm_page_t m, int flags, vm_page_t skip_m);
void vm_page_putfake(vm_page_t m);
void vm_page_readahead_finish(vm_page_t m);
bool vm_page_reclaim_contig(int req, u_long npages, vm_paddr_t low,
    vm_paddr_t high, u_long alignment, vm_paddr_t boundary);
bool vm_page_reclaim_contig_domain(int domain, int req, u_long npages,
    vm_paddr_t low, vm_paddr_t high, u_long alignment, vm_paddr_t boundary);
void vm_page_reference(vm_page_t m);
void vm_page_remove (vm_page_t);
int vm_page_rename (vm_page_t, vm_object_t, vm_pindex_t);
vm_page_t vm_page_replace(vm_page_t mnew, vm_object_t object,
    vm_pindex_t pindex);
void vm_page_requeue(vm_page_t m);
int vm_page_sbusied(vm_page_t m);
vm_page_t vm_page_scan_contig(u_long npages, vm_page_t m_start,
    vm_page_t m_end, u_long alignment, vm_paddr_t boundary, int options);
void vm_page_set_valid_range(vm_page_t m, int base, int size);
int vm_page_sleep_if_busy(vm_page_t m, const char *msg);
vm_offset_t vm_page_startup(vm_offset_t vaddr);
void vm_page_sunbusy(vm_page_t m);
bool vm_page_try_to_free(vm_page_t m);
int vm_page_trysbusy(vm_page_t m);
void vm_page_unhold_pages(vm_page_t *ma, int count);
void vm_page_unswappable(vm_page_t m);
bool vm_page_unwire(vm_page_t m, uint8_t queue);
bool vm_page_unwire_noq(vm_page_t m);
void vm_page_updatefake(vm_page_t m, vm_paddr_t paddr, vm_memattr_t memattr);
void vm_page_wire (vm_page_t);
void vm_page_xunbusy_hard(vm_page_t m);
void vm_page_xunbusy_maybelocked(vm_page_t m);
void vm_page_set_validclean (vm_page_t, int, int);
void vm_page_clear_dirty (vm_page_t, int, int);
void vm_page_set_invalid (vm_page_t, int, int);
int vm_page_is_valid (vm_page_t, int, int);
void vm_page_test_dirty (vm_page_t);
vm_page_bits_t vm_page_bits(int base, int size);
void vm_page_zero_invalid(vm_page_t m, boolean_t setvalid);
void vm_page_free_toq(vm_page_t m);
void vm_page_free_pages_toq(struct spglist *free, bool update_wire_count);

void vm_page_dirty_KBI(vm_page_t m);
void vm_page_lock_KBI(vm_page_t m, const char *file, int line);
void vm_page_unlock_KBI(vm_page_t m, const char *file, int line);
int vm_page_trylock_KBI(vm_page_t m, const char *file, int line);
#if defined(INVARIANTS) || defined(INVARIANT_SUPPORT)
void vm_page_assert_locked_KBI(vm_page_t m, const char *file, int line);
void vm_page_lock_assert_KBI(vm_page_t m, int a, const char *file, int line);
#endif

#define	vm_page_assert_sbusied(m)					\
	KASSERT(vm_page_sbusied(m),					\
	    ("vm_page_assert_sbusied: page %p not shared busy @ %s:%d", \
	    (m), __FILE__, __LINE__))

#define	vm_page_assert_unbusied(m)					\
	KASSERT(!vm_page_busied(m),					\
	    ("vm_page_assert_unbusied: page %p busy @ %s:%d",		\
	    (m), __FILE__, __LINE__))

#define	vm_page_assert_xbusied(m)					\
	KASSERT(vm_page_xbusied(m),					\
	    ("vm_page_assert_xbusied: page %p not exclusive busy @ %s:%d", \
	    (m), __FILE__, __LINE__))

#define	vm_page_busied(m)						\
	((m)->busy_lock != VPB_UNBUSIED)

#define	vm_page_sbusy(m) do {						\
	if (!vm_page_trysbusy(m))					\
		panic("%s: page %p failed shared busying", __func__,	\
		    (m));						\
} while (0)

#define	vm_page_tryxbusy(m)						\
	(atomic_cmpset_acq_int(&(m)->busy_lock, VPB_UNBUSIED,		\
	    VPB_SINGLE_EXCLUSIVER))

#define	vm_page_xbusied(m)						\
	(((m)->busy_lock & VPB_SINGLE_EXCLUSIVER) != 0)

#define	vm_page_xbusy(m) do {						\
	if (!vm_page_tryxbusy(m))					\
		panic("%s: page %p failed exclusive busying", __func__,	\
		    (m));						\
} while (0)

/* Note: page m's lock must not be owned by the caller. */
#define	vm_page_xunbusy(m) do {						\
	if (!atomic_cmpset_rel_int(&(m)->busy_lock,			\
	    VPB_SINGLE_EXCLUSIVER, VPB_UNBUSIED))			\
		vm_page_xunbusy_hard(m);				\
} while (0)

#ifdef INVARIANTS
void vm_page_object_lock_assert(vm_page_t m);
#define	VM_PAGE_OBJECT_LOCK_ASSERT(m)	vm_page_object_lock_assert(m)
void vm_page_assert_pga_writeable(vm_page_t m, uint8_t bits);
#define	VM_PAGE_ASSERT_PGA_WRITEABLE(m, bits)				\
	vm_page_assert_pga_writeable(m, bits)
#else
#define	VM_PAGE_OBJECT_LOCK_ASSERT(m)	(void)0
#define	VM_PAGE_ASSERT_PGA_WRITEABLE(m, bits)	(void)0
#endif

/*
 * We want to use atomic updates for the aflags field, which is 8 bits wide.
 * However, not all architectures support atomic operations on 8-bit
 * destinations.  In order that we can easily use a 32-bit operation, we
 * require that the aflags field be 32-bit aligned.
 */
CTASSERT(offsetof(struct vm_page, aflags) % sizeof(uint32_t) == 0);

/*
 *	Clear the given bits in the specified page.
 */
static inline void
vm_page_aflag_clear(vm_page_t m, uint8_t bits)
{
	uint32_t *addr, val;

	/*
	 * The PGA_REFERENCED flag can only be cleared if the page is locked.
	 */
	if ((bits & PGA_REFERENCED) != 0)
		vm_page_assert_locked(m);

	/*
	 * Access the whole 32-bit word containing the aflags field with an
	 * atomic update.  Parallel non-atomic updates to the other fields
	 * within this word are handled properly by the atomic update.
	 */
	addr = (void *)&m->aflags;
	KASSERT(((uintptr_t)addr & (sizeof(uint32_t) - 1)) == 0,
	    ("vm_page_aflag_clear: aflags is misaligned"));
	val = bits;
#if BYTE_ORDER == BIG_ENDIAN
	val <<= 24;
#endif
	atomic_clear_32(addr, val);
}

/*
 *	Set the given bits in the specified page.
 */
static inline void
vm_page_aflag_set(vm_page_t m, uint8_t bits)
{
	uint32_t *addr, val;

	VM_PAGE_ASSERT_PGA_WRITEABLE(m, bits);

	/*
	 * Access the whole 32-bit word containing the aflags field with an
	 * atomic update.  Parallel non-atomic updates to the other fields
	 * within this word are handled properly by the atomic update.
	 */
	addr = (void *)&m->aflags;
	KASSERT(((uintptr_t)addr & (sizeof(uint32_t) - 1)) == 0,
	    ("vm_page_aflag_set: aflags is misaligned"));
	val = bits;
#if BYTE_ORDER == BIG_ENDIAN
	val <<= 24;
#endif
	atomic_set_32(addr, val);
} 

/*
 *	vm_page_dirty:
 *
 *	Set all bits in the page's dirty field.
 *
 *	The object containing the specified page must be locked if the
 *	call is made from the machine-independent layer.
 *
 *	See vm_page_clear_dirty_mask().
 */
static __inline void
vm_page_dirty(vm_page_t m)
{

	/* Use vm_page_dirty_KBI() under INVARIANTS to save memory. */
#if (defined(KLD_MODULE) && !defined(KLD_TIED)) || defined(INVARIANTS)
	vm_page_dirty_KBI(m);
#else
	m->dirty = VM_PAGE_BITS_ALL;
#endif
}

/*
 *	vm_page_undirty:
 *
 *	Set page to not be dirty.  Note: does not clear pmap modify bits
 */
static __inline void
vm_page_undirty(vm_page_t m)
{

	VM_PAGE_OBJECT_LOCK_ASSERT(m);
	m->dirty = 0;
}

static inline void
vm_page_replace_checked(vm_page_t mnew, vm_object_t object, vm_pindex_t pindex,
    vm_page_t mold)
{
	vm_page_t mret;

	mret = vm_page_replace(mnew, object, pindex);
	KASSERT(mret == mold,
	    ("invalid page replacement, mold=%p, mret=%p", mold, mret));

	/* Unused if !INVARIANTS. */
	(void)mold;
	(void)mret;
}

/*
 *	vm_page_queue:
 *
 *	Return the index of the queue containing m.  This index is guaranteed
 *	not to change while the page lock is held.
 */
static inline uint8_t
vm_page_queue(vm_page_t m)
{

	vm_page_assert_locked(m);

	if ((m->aflags & PGA_DEQUEUE) != 0)
		return (PQ_NONE);
	atomic_thread_fence_acq();
	return (m->queue);
}

static inline bool
vm_page_active(vm_page_t m)
{

	return (vm_page_queue(m) == PQ_ACTIVE);
}

static inline bool
vm_page_inactive(vm_page_t m)
{

	return (vm_page_queue(m) == PQ_INACTIVE);
}

static inline bool
vm_page_in_laundry(vm_page_t m)
{
	uint8_t queue;

	queue = vm_page_queue(m);
	return (queue == PQ_LAUNDRY || queue == PQ_UNSWAPPABLE);
}

/*
 *	vm_page_held:
 *
 *	Return true if a reference prevents the page from being reclaimable.
 */
static inline bool
vm_page_held(vm_page_t m)
{

	return (m->hold_count > 0 || m->wire_count > 0);
}

#endif				/* _KERNEL */
#endif				/* !_VM_PAGE_ */
