/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002, 2003, 2004, 2005 Jeffrey Roberson <jeff@FreeBSD.org>
 * Copyright (c) 2004, 2005 Bosko Milekic <bmilekic@FreeBSD.org>
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
 *
 * $FreeBSD$
 *
 */

/*
 * uma.h - External definitions for the Universal Memory Allocator
 *
*/

#ifndef _VM_UMA_H_
#define _VM_UMA_H_

#include <sys/param.h>		/* For NULL */
#include <sys/malloc.h>		/* For M_* */

/* User visible parameters */
#define UMA_SMALLEST_UNIT       (PAGE_SIZE / 256) /* Smallest item allocated */

/* Types and type defs */

struct uma_zone;
/* Opaque type used as a handle to the zone */
typedef struct uma_zone * uma_zone_t;

void zone_drain(uma_zone_t);

/*
 * Item constructor
 *
 * Arguments:
 *	item  A pointer to the memory which has been allocated.
 *	arg   The arg field passed to uma_zalloc_arg
 *	size  The size of the allocated item
 *	flags See zalloc flags
 *
 * Returns:
 *	0      on success
 *      errno  on failure
 *
 * Discussion:
 *	The constructor is called just before the memory is returned
 *	to the user. It may block if necessary.
 */
typedef int (*uma_ctor)(void *mem, int size, void *arg, int flags);

/*
 * Item destructor
 *
 * Arguments:
 *	item  A pointer to the memory which has been allocated.
 *	size  The size of the item being destructed.
 *	arg   Argument passed through uma_zfree_arg
 *
 * Returns:
 *	Nothing
 *
 * Discussion:
 *	The destructor may perform operations that differ from those performed
 *	by the initializer, but it must leave the object in the same state.
 *	This IS type stable storage.  This is called after EVERY zfree call.
 */
typedef void (*uma_dtor)(void *mem, int size, void *arg);

/*
 * Item initializer
 *
 * Arguments:
 *	item  A pointer to the memory which has been allocated.
 *	size  The size of the item being initialized.
 *	flags See zalloc flags
 *
 * Returns:
 *	0      on success
 *      errno  on failure
 *
 * Discussion:
 *	The initializer is called when the memory is cached in the uma zone.
 *	The initializer and the destructor should leave the object in the same
 *	state.
 */
typedef int (*uma_init)(void *mem, int size, int flags);

/*
 * Item discard function
 *
 * Arguments:
 *	item  A pointer to memory which has been 'freed' but has not left the
 *	      zone's cache.
 *	size  The size of the item being discarded.
 *
 * Returns:
 *	Nothing
 *
 * Discussion:
 *	This routine is called when memory leaves a zone and is returned to the
 *	system for other uses.  It is the counter-part to the init function.
 */
typedef void (*uma_fini)(void *mem, int size);

/*
 * Import new memory into a cache zone.
 */
typedef int (*uma_import)(void *arg, void **store, int count, int domain,
    int flags);

/*
 * Free memory from a cache zone.
 */
typedef void (*uma_release)(void *arg, void **store, int count);

/*
 * What's the difference between initializing and constructing?
 *
 * The item is initialized when it is cached, and this is the state that the
 * object should be in when returned to the allocator. The purpose of this is
 * to remove some code which would otherwise be called on each allocation by
 * utilizing a known, stable state.  This differs from the constructor which
 * will be called on EVERY allocation.
 *
 * For example, in the initializer you may want to initialize embedded locks,
 * NULL list pointers, set up initial states, magic numbers, etc.  This way if
 * the object is held in the allocator and re-used it won't be necessary to
 * re-initialize it.
 *
 * The constructor may be used to lock a data structure, link it on to lists,
 * bump reference counts or total counts of outstanding structures, etc.
 *
 */


/* Function proto types */

/*
 * Create a new uma zone
 *
 * Arguments:
 *	name  The text name of the zone for debugging and stats. This memory
 *		should not be freed until the zone has been deallocated.
 *	size  The size of the object that is being created.
 *	ctor  The constructor that is called when the object is allocated.
 *	dtor  The destructor that is called when the object is freed.
 *	init  An initializer that sets up the initial state of the memory.
 *	fini  A discard function that undoes initialization done by init.
 *		ctor/dtor/init/fini may all be null, see notes above.
 *	align A bitmask that corresponds to the requested alignment
 *		eg 4 would be 0x3
 *	flags A set of parameters that control the behavior of the zone.
 *
 * Returns:
 *	A pointer to a structure which is intended to be opaque to users of
 *	the interface.  The value may be null if the wait flag is not set.
 */
uma_zone_t uma_zcreate(const char *name, size_t size, uma_ctor ctor,
		    uma_dtor dtor, uma_init uminit, uma_fini fini,
		    int align, uint32_t flags);

/*
 * Create a secondary uma zone
 *
 * Arguments:
 *	name  The text name of the zone for debugging and stats. This memory
 *		should not be freed until the zone has been deallocated.
 *	ctor  The constructor that is called when the object is allocated.
 *	dtor  The destructor that is called when the object is freed.
 *	zinit  An initializer that sets up the initial state of the memory
 *		as the object passes from the Keg's slab to the Zone's cache.
 *	zfini  A discard function that undoes initialization done by init
 *		as the object passes from the Zone's cache to the Keg's slab.
 *
 *		ctor/dtor/zinit/zfini may all be null, see notes above.
 *		Note that the zinit and zfini specified here are NOT
 *		exactly the same as the init/fini specified to uma_zcreate()
 *		when creating a master zone.  These zinit/zfini are called
 *		on the TRANSITION from keg to zone (and vice-versa). Once
 *		these are set, the primary zone may alter its init/fini
 *		(which are called when the object passes from VM to keg)
 *		using uma_zone_set_init/fini()) as well as its own
 *		zinit/zfini (unset by default for master zone) with
 *		uma_zone_set_zinit/zfini() (note subtle 'z' prefix).
 *
 *	master  A reference to this zone's Master Zone (Primary Zone),
 *		which contains the backing Keg for the Secondary Zone
 *		being added.
 *
 * Returns:
 *	A pointer to a structure which is intended to be opaque to users of
 *	the interface.  The value may be null if the wait flag is not set.
 */
uma_zone_t uma_zsecond_create(char *name, uma_ctor ctor, uma_dtor dtor,
		    uma_init zinit, uma_fini zfini, uma_zone_t master);

/*
 * Create cache-only zones.
 *
 * This allows uma's per-cpu cache facilities to handle arbitrary
 * pointers.  Consumers must specify the import and release functions to
 * fill and destroy caches.  UMA does not allocate any memory for these
 * zones.  The 'arg' parameter is passed to import/release and is caller
 * specific.
 */
uma_zone_t uma_zcache_create(char *name, int size, uma_ctor ctor, uma_dtor dtor,
		    uma_init zinit, uma_fini zfini, uma_import zimport,
		    uma_release zrelease, void *arg, int flags);

/*
 * Definitions for uma_zcreate flags
 *
 * These flags share space with UMA_ZFLAGs in uma_int.h.  Be careful not to
 * overlap when adding new features.  0xff000000 is in use by uma_int.h.
 */
#define UMA_ZONE_PAGEABLE	0x0001	/* Return items not fully backed by
					   physical memory XXX Not yet */
#define UMA_ZONE_ZINIT		0x0002	/* Initialize with zeros */
#define UMA_ZONE_STATIC		0x0004	/* Statically sized zone */
#define UMA_ZONE_OFFPAGE	0x0008	/* Force the slab structure allocation
					   off of the real memory */
#define UMA_ZONE_MALLOC		0x0010	/* For use by malloc(9) only! */
#define UMA_ZONE_NOFREE		0x0020	/* Do not free slabs of this type! */
#define UMA_ZONE_MTXCLASS	0x0040	/* Create a new lock class */
#define	UMA_ZONE_VM		0x0080	/*
					 * Used for internal vm datastructures
					 * only.
					 */
#define	UMA_ZONE_HASH		0x0100	/*
					 * Use a hash table instead of caching
					 * information in the vm_page.
					 */
#define	UMA_ZONE_SECONDARY	0x0200	/* Zone is a Secondary Zone */
#define	UMA_ZONE_NOBUCKET	0x0400	/* Do not use buckets. */
#define	UMA_ZONE_MAXBUCKET	0x0800	/* Use largest buckets. */
#define	UMA_ZONE_CACHESPREAD	0x1000	/*
					 * Spread memory start locations across
					 * all possible cache lines.  May
					 * require many virtually contiguous
					 * backend pages and can fail early.
					 */
#define	UMA_ZONE_VTOSLAB	0x2000	/* Zone uses vtoslab for lookup. */
#define	UMA_ZONE_NODUMP		0x4000	/*
					 * Zone's pages will not be included in
					 * mini-dumps.
					 */
#define	UMA_ZONE_PCPU		0x8000	/*
					 * Allocates mp_maxid + 1 slabs of PAGE_SIZE
					 */
#define	UMA_ZONE_NUMA		0x10000	/*
					 * NUMA aware Zone.  Implements a best
					 * effort first-touch policy.
					 */

/*
 * These flags are shared between the keg and zone.  In zones wishing to add
 * new kegs these flags must be compatible.  Some are determined based on
 * physical parameters of the request and may not be provided by the consumer.
 */
#define	UMA_ZONE_INHERIT						\
    (UMA_ZONE_OFFPAGE | UMA_ZONE_MALLOC | UMA_ZONE_NOFREE |		\
    UMA_ZONE_HASH | UMA_ZONE_VTOSLAB | UMA_ZONE_PCPU)

/* Definitions for align */
#define UMA_ALIGN_PTR	(sizeof(void *) - 1)	/* Alignment fit for ptr */
#define UMA_ALIGN_LONG	(sizeof(long) - 1)	/* "" long */
#define UMA_ALIGN_INT	(sizeof(int) - 1)	/* "" int */
#define UMA_ALIGN_SHORT	(sizeof(short) - 1)	/* "" short */
#define UMA_ALIGN_CHAR	(sizeof(char) - 1)	/* "" char */
#define UMA_ALIGN_CACHE	(0 - 1)			/* Cache line size align */
#define	UMA_ALIGNOF(type) (_Alignof(type) - 1)	/* Alignment fit for 'type' */

/*
 * Destroys an empty uma zone.  If the zone is not empty uma complains loudly.
 *
 * Arguments:
 *	zone  The zone we want to destroy.
 *
 */
void uma_zdestroy(uma_zone_t zone);

/*
 * Allocates an item out of a zone
 *
 * Arguments:
 *	zone  The zone we are allocating from
 *	arg   This data is passed to the ctor function
 *	flags See sys/malloc.h for available flags.
 *
 * Returns:
 *	A non-null pointer to an initialized element from the zone is
 *	guaranteed if the wait flag is M_WAITOK.  Otherwise a null pointer
 *	may be returned if the zone is empty or the ctor failed.
 */

void *uma_zalloc_arg(uma_zone_t zone, void *arg, int flags);
void *uma_zalloc_pcpu_arg(uma_zone_t zone, void *arg, int flags);

/*
 * Allocate an item from a specific NUMA domain.  This uses a slow path in
 * the allocator but is guaranteed to allocate memory from the requested
 * domain if M_WAITOK is set.
 *
 * Arguments:
 *	zone  The zone we are allocating from
 *	arg   This data is passed to the ctor function
 *	domain The domain to allocate from.
 *	flags See sys/malloc.h for available flags.
 */
void *uma_zalloc_domain(uma_zone_t zone, void *arg, int domain, int flags);

/*
 * Allocates an item out of a zone without supplying an argument
 *
 * This is just a wrapper for uma_zalloc_arg for convenience.
 *
 */
static __inline void *uma_zalloc(uma_zone_t zone, int flags);
static __inline void *uma_zalloc_pcpu(uma_zone_t zone, int flags);

static __inline void *
uma_zalloc(uma_zone_t zone, int flags)
{
	return uma_zalloc_arg(zone, NULL, flags);
}

static __inline void *
uma_zalloc_pcpu(uma_zone_t zone, int flags)
{
	return uma_zalloc_pcpu_arg(zone, NULL, flags);
}

/*
 * Frees an item back into the specified zone.
 *
 * Arguments:
 *	zone  The zone the item was originally allocated out of.
 *	item  The memory to be freed.
 *	arg   Argument passed to the destructor
 *
 * Returns:
 *	Nothing.
 */

void uma_zfree_arg(uma_zone_t zone, void *item, void *arg);
void uma_zfree_pcpu_arg(uma_zone_t zone, void *item, void *arg);

/*
 * Frees an item back to the specified zone's domain specific pool.
 *
 * Arguments:
 *	zone  The zone the item was originally allocated out of.
 *	item  The memory to be freed.
 *	arg   Argument passed to the destructor
 */
void uma_zfree_domain(uma_zone_t zone, void *item, void *arg);

/*
 * Frees an item back to a zone without supplying an argument
 *
 * This is just a wrapper for uma_zfree_arg for convenience.
 *
 */
static __inline void uma_zfree(uma_zone_t zone, void *item);
static __inline void uma_zfree_pcpu(uma_zone_t zone, void *item);

static __inline void
uma_zfree(uma_zone_t zone, void *item)
{
	uma_zfree_arg(zone, item, NULL);
}

static __inline void
uma_zfree_pcpu(uma_zone_t zone, void *item)
{
	uma_zfree_pcpu_arg(zone, item, NULL);
}

/*
 * Wait until the specified zone can allocate an item.
 */
void uma_zwait(uma_zone_t zone);

/*
 * Backend page supplier routines
 *
 * Arguments:
 *	zone  The zone that is requesting pages.
 *	size  The number of bytes being requested.
 *	pflag Flags for these memory pages, see below.
 *	domain The NUMA domain that we prefer for this allocation.
 *	wait  Indicates our willingness to block.
 *
 * Returns:
 *	A pointer to the allocated memory or NULL on failure.
 */

typedef void *(*uma_alloc)(uma_zone_t zone, vm_size_t size, int domain,
    uint8_t *pflag, int wait);

/*
 * Backend page free routines
 *
 * Arguments:
 *	item  A pointer to the previously allocated pages.
 *	size  The original size of the allocation.
 *	pflag The flags for the slab.  See UMA_SLAB_* below.
 *
 * Returns:
 *	None
 */
typedef void (*uma_free)(void *item, vm_size_t size, uint8_t pflag);

/*
 * Reclaims unused memory for all zones
 *
 * Arguments:
 *	None
 * Returns:
 *	None
 *
 * This should only be called by the page out daemon.
 */

void uma_reclaim(void);

/*
 * Sets the alignment mask to be used for all zones requesting cache
 * alignment.  Should be called by MD boot code prior to starting VM/UMA.
 *
 * Arguments:
 *	align The alignment mask
 *
 * Returns:
 *	Nothing
 */
void uma_set_align(int align);

/*
 * Set a reserved number of items to hold for M_USE_RESERVE allocations.  All
 * other requests must allocate new backing pages.
 */
void uma_zone_reserve(uma_zone_t zone, int nitems);

/*
 * Reserves the maximum KVA space required by the zone and configures the zone
 * to use a VM_ALLOC_NOOBJ-based backend allocator.
 *
 * Arguments:
 *	zone  The zone to update.
 *	nitems  The upper limit on the number of items that can be allocated.
 *
 * Returns:
 *	0  if KVA space can not be allocated
 *	1  if successful
 *
 * Discussion:
 *	When the machine supports a direct map and the zone's items are smaller
 *	than a page, the zone will use the direct map instead of allocating KVA
 *	space.
 */
int uma_zone_reserve_kva(uma_zone_t zone, int nitems);

/*
 * Sets a high limit on the number of items allowed in a zone
 *
 * Arguments:
 *	zone  The zone to limit
 *	nitems  The requested upper limit on the number of items allowed
 *
 * Returns:
 *	int  The effective value of nitems after rounding up based on page size
 */
int uma_zone_set_max(uma_zone_t zone, int nitems);

/*
 * Sets a high limit on the number of items allowed in zone's bucket cache
 *
 * Arguments:
 *      zone  The zone to limit
 *      nitems  The requested upper limit on the number of items allowed
 *
 * Returns:
 *      int  The effective value of nitems set
 */
int uma_zone_set_maxcache(uma_zone_t zone, int nitems);

/*
 * Obtains the effective limit on the number of items in a zone
 *
 * Arguments:
 *	zone  The zone to obtain the effective limit from
 *
 * Return:
 *	0  No limit
 *	int  The effective limit of the zone
 */
int uma_zone_get_max(uma_zone_t zone);

/*
 * Sets a warning to be printed when limit is reached
 *
 * Arguments:
 *	zone  The zone we will warn about
 *	warning  Warning content
 *
 * Returns:
 *	Nothing
 */
void uma_zone_set_warning(uma_zone_t zone, const char *warning);

/*
 * Sets a function to run when limit is reached
 *
 * Arguments:
 *	zone  The zone to which this applies
 *	fx  The function ro run
 *
 * Returns:
 *	Nothing
 */
typedef void (*uma_maxaction_t)(uma_zone_t, int);
void uma_zone_set_maxaction(uma_zone_t zone, uma_maxaction_t);

/*
 * Obtains the approximate current number of items allocated from a zone
 *
 * Arguments:
 *	zone  The zone to obtain the current allocation count from
 *
 * Return:
 *	int  The approximate current number of items allocated from the zone
 */
int uma_zone_get_cur(uma_zone_t zone);

/*
 * The following two routines (uma_zone_set_init/fini)
 * are used to set the backend init/fini pair which acts on an
 * object as it becomes allocated and is placed in a slab within
 * the specified zone's backing keg.  These should probably not
 * be changed once allocations have already begun, but only be set
 * immediately upon zone creation.
 */
void uma_zone_set_init(uma_zone_t zone, uma_init uminit);
void uma_zone_set_fini(uma_zone_t zone, uma_fini fini);

/*
 * The following two routines (uma_zone_set_zinit/zfini) are
 * used to set the zinit/zfini pair which acts on an object as
 * it passes from the backing Keg's slab cache to the
 * specified Zone's bucket cache.  These should probably not
 * be changed once allocations have already begun, but only be set
 * immediately upon zone creation.
 */
void uma_zone_set_zinit(uma_zone_t zone, uma_init zinit);
void uma_zone_set_zfini(uma_zone_t zone, uma_fini zfini);

/*
 * Replaces the standard backend allocator for this zone.
 *
 * Arguments:
 *	zone   The zone whose backend allocator is being changed.
 *	allocf A pointer to the allocation function
 *
 * Returns:
 *	Nothing
 *
 * Discussion:
 *	This could be used to implement pageable allocation, or perhaps
 *	even DMA allocators if used in conjunction with the OFFPAGE
 *	zone flag.
 */

void uma_zone_set_allocf(uma_zone_t zone, uma_alloc allocf);

/*
 * Used for freeing memory provided by the allocf above
 *
 * Arguments:
 *	zone  The zone that intends to use this free routine.
 *	freef The page freeing routine.
 *
 * Returns:
 *	Nothing
 */

void uma_zone_set_freef(uma_zone_t zone, uma_free freef);

/*
 * These flags are setable in the allocf and visible in the freef.
 */
#define UMA_SLAB_BOOT	0x01		/* Slab alloced from boot pages */
#define UMA_SLAB_KERNEL	0x04		/* Slab alloced from kmem */
#define UMA_SLAB_PRIV	0x08		/* Slab alloced from priv allocator */
#define UMA_SLAB_OFFP	0x10		/* Slab is managed separately  */
#define UMA_SLAB_MALLOC	0x20		/* Slab is a large malloc slab */
/* 0x02, 0x40, and 0x80 are available */

/*
 * Used to pre-fill a zone with some number of items
 *
 * Arguments:
 *	zone    The zone to fill
 *	itemcnt The number of items to reserve
 *
 * Returns:
 *	Nothing
 *
 * NOTE: This is blocking and should only be done at startup
 */
void uma_prealloc(uma_zone_t zone, int itemcnt);

/*
 * Used to determine if a fixed-size zone is exhausted.
 *
 * Arguments:
 *	zone    The zone to check
 *
 * Returns:
 *	Non-zero if zone is exhausted.
 */
int uma_zone_exhausted(uma_zone_t zone);
int uma_zone_exhausted_nolock(uma_zone_t zone);

/*
 * Common UMA_ZONE_PCPU zones.
 */
extern uma_zone_t pcpu_zone_64;
extern uma_zone_t pcpu_zone_ptr;

/*
 * Exported statistics structures to be used by user space monitoring tools.
 * Statistics stream consists of a uma_stream_header, followed by a series of
 * alternative uma_type_header and uma_type_stat structures.
 */
#define	UMA_STREAM_VERSION	0x00000001
struct uma_stream_header {
	uint32_t	ush_version;	/* Stream format version. */
	uint32_t	ush_maxcpus;	/* Value of MAXCPU for stream. */
	uint32_t	ush_count;	/* Number of records. */
	uint32_t	_ush_pad;	/* Pad/reserved field. */
};

#define	UTH_MAX_NAME	32
#define	UTH_ZONE_SECONDARY	0x00000001
struct uma_type_header {
	/*
	 * Static per-zone data, some extracted from the supporting keg.
	 */
	char		uth_name[UTH_MAX_NAME];
	uint32_t	uth_align;	/* Keg: alignment. */
	uint32_t	uth_size;	/* Keg: requested size of item. */
	uint32_t	uth_rsize;	/* Keg: real size of item. */
	uint32_t	uth_maxpages;	/* Keg: maximum number of pages. */
	uint32_t	uth_limit;	/* Keg: max items to allocate. */

	/*
	 * Current dynamic zone/keg-derived statistics.
	 */
	uint32_t	uth_pages;	/* Keg: pages allocated. */
	uint32_t	uth_keg_free;	/* Keg: items free. */
	uint32_t	uth_zone_free;	/* Zone: items free. */
	uint32_t	uth_bucketsize;	/* Zone: desired bucket size. */
	uint32_t	uth_zone_flags;	/* Zone: flags. */
	uint64_t	uth_allocs;	/* Zone: number of allocations. */
	uint64_t	uth_frees;	/* Zone: number of frees. */
	uint64_t	uth_fails;	/* Zone: number of alloc failures. */
	uint64_t	uth_sleeps;	/* Zone: number of alloc sleeps. */
	uint64_t	_uth_reserved1[2];	/* Reserved. */
};

struct uma_percpu_stat {
	uint64_t	ups_allocs;	/* Cache: number of allocations. */
	uint64_t	ups_frees;	/* Cache: number of frees. */
	uint64_t	ups_cache_free;	/* Cache: free items in cache. */
	uint64_t	_ups_reserved[5];	/* Reserved. */
};

void uma_reclaim_wakeup(void);
void uma_reclaim_worker(void *);

unsigned long uma_limit(void);

/* Return the amount of memory managed by UMA. */
unsigned long uma_size(void);

/* Return the amount of memory remaining.  May be negative. */
long uma_avail(void);

#endif	/* _VM_UMA_H_ */
