/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002-2005, 2009, 2013 Jeffrey Roberson <jeff@FreeBSD.org>
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

#include <sys/counter.h>
#include <sys/_bitset.h>
#include <sys/_domainset.h>
#include <sys/_task.h>

/* 
 * This file includes definitions, structures, prototypes, and inlines that
 * should not be used outside of the actual implementation of UMA.
 */

/* 
 * The brief summary;  Zones describe unique allocation types.  Zones are
 * organized into per-CPU caches which are filled by buckets.  Buckets are
 * organized according to memory domains.  Buckets are filled from kegs which
 * are also organized according to memory domains.  Kegs describe a unique
 * allocation type, backend memory provider, and layout.  Kegs are associated
 * with one or more zones and zones reference one or more kegs.  Kegs provide
 * slabs which are virtually contiguous collections of pages.  Each slab is
 * broken down int one or more items that will satisfy an individual allocation.
 *
 * Allocation is satisfied in the following order:
 * 1) Per-CPU cache
 * 2) Per-domain cache of buckets
 * 3) Slab from any of N kegs
 * 4) Backend page provider
 *
 * More detail on individual objects is contained below:
 *
 * Kegs contain lists of slabs which are stored in either the full bin, empty
 * bin, or partially allocated bin, to reduce fragmentation.  They also contain
 * the user supplied value for size, which is adjusted for alignment purposes
 * and rsize is the result of that.  The Keg also stores information for
 * managing a hash of page addresses that maps pages to uma_slab_t structures
 * for pages that don't have embedded uma_slab_t's.
 *
 * Keg slab lists are organized by memory domain to support NUMA allocation
 * policies.  By default allocations are spread across domains to reduce the
 * potential for hotspots.  Special keg creation flags may be specified to
 * prefer location allocation.  However there is no strict enforcement as frees
 * may happen on any CPU and these are returned to the CPU-local cache
 * regardless of the originating domain.
 *  
 * The uma_slab_t may be embedded in a UMA_SLAB_SIZE chunk of memory or it may
 * be allocated off the page from a special slab zone.  The free list within a
 * slab is managed with a bitmask.  For item sizes that would yield more than
 * 10% memory waste we potentially allocate a separate uma_slab_t if this will
 * improve the number of items per slab that will fit.  
 *
 * The only really gross cases, with regards to memory waste, are for those
 * items that are just over half the page size.   You can get nearly 50% waste,
 * so you fall back to the memory footprint of the power of two allocator. I
 * have looked at memory allocation sizes on many of the machines available to
 * me, and there does not seem to be an abundance of allocations at this range
 * so at this time it may not make sense to optimize for it.  This can, of 
 * course, be solved with dynamic slab sizes.
 *
 * Kegs may serve multiple Zones but by far most of the time they only serve
 * one.  When a Zone is created, a Keg is allocated and setup for it.  While
 * the backing Keg stores slabs, the Zone caches Buckets of items allocated
 * from the slabs.  Each Zone is equipped with an init/fini and ctor/dtor
 * pair, as well as with its own set of small per-CPU caches, layered above
 * the Zone's general Bucket cache.
 *
 * The PCPU caches are protected by critical sections, and may be accessed
 * safely only from their associated CPU, while the Zones backed by the same
 * Keg all share a common Keg lock (to coalesce contention on the backing
 * slabs).  The backing Keg typically only serves one Zone but in the case of
 * multiple Zones, one of the Zones is considered the Master Zone and all
 * Zone-related stats from the Keg are done in the Master Zone.  For an
 * example of a Multi-Zone setup, refer to the Mbuf allocation code.
 */

/*
 *	This is the representation for normal (Non OFFPAGE slab)
 *
 *	i == item
 *	s == slab pointer
 *
 *	<----------------  Page (UMA_SLAB_SIZE) ------------------>
 *	___________________________________________________________
 *     | _  _  _  _  _  _  _  _  _  _  _  _  _  _  _   ___________ |
 *     ||i||i||i||i||i||i||i||i||i||i||i||i||i||i||i| |slab header||
 *     ||_||_||_||_||_||_||_||_||_||_||_||_||_||_||_| |___________|| 
 *     |___________________________________________________________|
 *
 *
 *	This is an OFFPAGE slab. These can be larger than UMA_SLAB_SIZE.
 *
 *	___________________________________________________________
 *     | _  _  _  _  _  _  _  _  _  _  _  _  _  _  _  _  _  _  _   |
 *     ||i||i||i||i||i||i||i||i||i||i||i||i||i||i||i||i||i||i||i|  |
 *     ||_||_||_||_||_||_||_||_||_||_||_||_||_||_||_||_||_||_||_|  |
 *     |___________________________________________________________|
 *       ___________    ^
 *	|slab header|   |
 *	|___________|---*
 *
 */

#ifndef VM_UMA_INT_H
#define VM_UMA_INT_H

#define UMA_SLAB_SIZE	PAGE_SIZE	/* How big are our slabs? */
#define UMA_SLAB_MASK	(PAGE_SIZE - 1)	/* Mask to get back to the page */
#define UMA_SLAB_SHIFT	PAGE_SHIFT	/* Number of bits PAGE_MASK */

/* Max waste percentage before going to off page slab management */
#define UMA_MAX_WASTE	10

/*
 * Actual size of uma_slab when it is placed at an end of a page
 * with pointer sized alignment requirement.
 */
#define	SIZEOF_UMA_SLAB	((sizeof(struct uma_slab) & UMA_ALIGN_PTR) ?	  \
			    (sizeof(struct uma_slab) & ~UMA_ALIGN_PTR) +  \
			    (UMA_ALIGN_PTR + 1) : sizeof(struct uma_slab))

/*
 * Size of memory in a not offpage single page slab available for actual items.
 */
#define	UMA_SLAB_SPACE	(PAGE_SIZE - SIZEOF_UMA_SLAB)

/*
 * I doubt there will be many cases where this is exceeded. This is the initial
 * size of the hash table for uma_slabs that are managed off page. This hash
 * does expand by powers of two.  Currently it doesn't get smaller.
 */
#define UMA_HASH_SIZE_INIT	32		

/* 
 * I should investigate other hashing algorithms.  This should yield a low
 * number of collisions if the pages are relatively contiguous.
 */

#define UMA_HASH(h, s) ((((uintptr_t)s) >> UMA_SLAB_SHIFT) & (h)->uh_hashmask)

#define UMA_HASH_INSERT(h, s, mem)					\
		SLIST_INSERT_HEAD(&(h)->uh_slab_hash[UMA_HASH((h),	\
		    (mem))], (s), us_hlink)
#define UMA_HASH_REMOVE(h, s, mem)					\
		SLIST_REMOVE(&(h)->uh_slab_hash[UMA_HASH((h),		\
		    (mem))], (s), uma_slab, us_hlink)

/* Hash table for freed address -> slab translation */

SLIST_HEAD(slabhead, uma_slab);

struct uma_hash {
	struct slabhead	*uh_slab_hash;	/* Hash table for slabs */
	u_int		uh_hashsize;	/* Current size of the hash table */
	u_int		uh_hashmask;	/* Mask used during hashing */
};

/*
 * align field or structure to cache line
 */
#if defined(__amd64__) || defined(__powerpc64__)
#define UMA_ALIGN	__aligned(128)
#else
#define UMA_ALIGN
#endif

/*
 * Structures for per cpu queues.
 */

struct uma_bucket {
	LIST_ENTRY(uma_bucket)	ub_link;	/* Link into the zone */
	int16_t	ub_cnt;				/* Count of items in bucket. */
	int16_t	ub_entries;			/* Max items. */
	void	*ub_bucket[];			/* actual allocation storage */
};

typedef struct uma_bucket * uma_bucket_t;

struct uma_cache {
	uma_bucket_t	uc_freebucket;	/* Bucket we're freeing to */
	uma_bucket_t	uc_allocbucket;	/* Bucket to allocate from */
	uint64_t	uc_allocs;	/* Count of allocations */
	uint64_t	uc_frees;	/* Count of frees */
} UMA_ALIGN;

typedef struct uma_cache * uma_cache_t;

/*
 * Per-domain memory list.  Embedded in the kegs.
 */
struct uma_domain {
	LIST_HEAD(,uma_slab)	ud_part_slab;	/* partially allocated slabs */
	LIST_HEAD(,uma_slab)	ud_free_slab;	/* empty slab list */
	LIST_HEAD(,uma_slab)	ud_full_slab;	/* full slabs */
};

typedef struct uma_domain * uma_domain_t;

/*
 * Keg management structure
 *
 * TODO: Optimize for cache line size
 *
 */
struct uma_keg {
	struct mtx	uk_lock;	/* Lock for the keg must be first.
					 * See shared uz_keg/uz_lockptr
					 * member of struct uma_zone. */
	struct uma_hash	uk_hash;
	LIST_HEAD(,uma_zone)	uk_zones;	/* Keg's zones */

	struct domainset_ref uk_dr;	/* Domain selection policy. */
	uint32_t	uk_align;	/* Alignment mask */
	uint32_t	uk_pages;	/* Total page count */
	uint32_t	uk_free;	/* Count of items free in slabs */
	uint32_t	uk_reserve;	/* Number of reserved items. */
	uint32_t	uk_size;	/* Requested size of each item */
	uint32_t	uk_rsize;	/* Real size of each item */

	uma_init	uk_init;	/* Keg's init routine */
	uma_fini	uk_fini;	/* Keg's fini routine */
	uma_alloc	uk_allocf;	/* Allocation function */
	uma_free	uk_freef;	/* Free routine */

	u_long		uk_offset;	/* Next free offset from base KVA */
	vm_offset_t	uk_kva;		/* Zone base KVA */
	uma_zone_t	uk_slabzone;	/* Slab zone backing us, if OFFPAGE */

	uint32_t	uk_pgoff;	/* Offset to uma_slab struct */
	uint16_t	uk_ppera;	/* pages per allocation from backend */
	uint16_t	uk_ipers;	/* Items per slab */
	uint32_t	uk_flags;	/* Internal flags */

	/* Least used fields go to the last cache line. */
	const char	*uk_name;		/* Name of creating zone. */
	LIST_ENTRY(uma_keg)	uk_link;	/* List of all kegs */

	/* Must be last, variable sized. */
	struct uma_domain	uk_domain[];	/* Keg's slab lists. */
};
typedef struct uma_keg	* uma_keg_t;

/*
 * Free bits per-slab.
 */
#define	SLAB_SETSIZE	(PAGE_SIZE / UMA_SMALLEST_UNIT)
BITSET_DEFINE(slabbits, SLAB_SETSIZE);

/*
 * The slab structure manages a single contiguous allocation from backing
 * store and subdivides it into individually allocatable items.
 */
struct uma_slab {
	uma_keg_t	us_keg;			/* Keg we live in */
	union {
		LIST_ENTRY(uma_slab)	_us_link;	/* slabs in zone */
		unsigned long	_us_size;	/* Size of allocation */
	} us_type;
	SLIST_ENTRY(uma_slab)	us_hlink;	/* Link for hash table */
	uint8_t		*us_data;		/* First item */
	struct slabbits	us_free;		/* Free bitmask. */
#ifdef INVARIANTS
	struct slabbits	us_debugfree;		/* Debug bitmask. */
#endif
	uint16_t	us_freecount;		/* How many are free? */
	uint8_t		us_flags;		/* Page flags see uma.h */
	uint8_t		us_domain;		/* Backing NUMA domain. */
};

#define	us_link	us_type._us_link
#define	us_size	us_type._us_size

#if MAXMEMDOM >= 255
#error "Slab domain type insufficient"
#endif

typedef struct uma_slab * uma_slab_t;

struct uma_zone_domain {
	LIST_HEAD(,uma_bucket)	uzd_buckets;	/* full buckets */
	long		uzd_nitems;	/* total item count */
	long		uzd_imax;	/* maximum item count this period */
	long		uzd_imin;	/* minimum item count this period */
	long		uzd_wss;	/* working set size estimate */
};

typedef struct uma_zone_domain * uma_zone_domain_t;

/*
 * Zone management structure 
 *
 * TODO: Optimize for cache line size
 *
 */
struct uma_zone {
	/* Offset 0, used in alloc/free fast/medium fast path and const. */
	union {
		uma_keg_t	uz_keg;		/* This zone's keg */
		struct mtx 	*uz_lockptr;	/* To keg or to self */
	};
	struct uma_zone_domain	*uz_domain;	/* per-domain buckets */
	uint32_t	uz_flags;	/* Flags inherited from kegs */
	uint32_t	uz_size;	/* Size inherited from kegs */
	uma_ctor	uz_ctor;	/* Constructor for each allocation */
	uma_dtor	uz_dtor;	/* Destructor */
	uint64_t	uz_items;	/* Total items count */
	uint64_t	uz_max_items;	/* Maximum number of items to alloc */
	uint32_t	uz_sleepers;	/* Number of sleepers on memory */
	uint16_t	uz_count;	/* Amount of items in full bucket */
	uint16_t	uz_count_max;	/* Maximum amount of items there */

	/* Offset 64, used in bucket replenish. */
	uma_import	uz_import;	/* Import new memory to cache. */
	uma_release	uz_release;	/* Release memory from cache. */
	void		*uz_arg;	/* Import/release argument. */
	uma_init	uz_init;	/* Initializer for each item */
	uma_fini	uz_fini;	/* Finalizer for each item. */
	void		*uz_spare;
	uint64_t	uz_bkt_count;    /* Items in bucket cache */
	uint64_t	uz_bkt_max;	/* Maximum bucket cache size */

	/* Offset 128 Rare. */
	/*
	 * The lock is placed here to avoid adjacent line prefetcher
	 * in fast paths and to take up space near infrequently accessed
	 * members to reduce alignment overhead.
	 */
	struct mtx	uz_lock;	/* Lock for the zone */
	LIST_ENTRY(uma_zone) uz_link;	/* List of all zones in keg */
	const char	*uz_name;	/* Text name of the zone */
	/* The next two fields are used to print a rate-limited warnings. */
	const char	*uz_warning;	/* Warning to print on failure */
	struct timeval	uz_ratecheck;	/* Warnings rate-limiting */
	struct task	uz_maxaction;	/* Task to run when at limit */
	uint16_t	uz_count_min;	/* Minimal amount of items in bucket */

	/* Offset 256, stats. */
	counter_u64_t	uz_allocs;	/* Total number of allocations */
	counter_u64_t	uz_frees;	/* Total number of frees */
	counter_u64_t	uz_fails;	/* Total number of alloc failures */
	uint64_t	uz_sleeps;	/* Total number of alloc sleeps */

	/*
	 * This HAS to be the last item because we adjust the zone size
	 * based on NCPU and then allocate the space for the zones.
	 */
	struct uma_cache	uz_cpu[]; /* Per cpu caches */

	/* uz_domain follows here. */
};

/*
 * These flags must not overlap with the UMA_ZONE flags specified in uma.h.
 */
#define	UMA_ZFLAG_CACHE		0x04000000	/* uma_zcache_create()d it */
#define	UMA_ZFLAG_DRAINING	0x08000000	/* Running zone_drain. */
#define	UMA_ZFLAG_BUCKET	0x10000000	/* Bucket zone. */
#define UMA_ZFLAG_INTERNAL	0x20000000	/* No offpage no PCPU. */
#define UMA_ZFLAG_CACHEONLY	0x80000000	/* Don't ask VM for buckets. */

#define	UMA_ZFLAG_INHERIT						\
    (UMA_ZFLAG_INTERNAL | UMA_ZFLAG_CACHEONLY | UMA_ZFLAG_BUCKET)

#undef UMA_ALIGN

#ifdef _KERNEL
/* Internal prototypes */
static __inline uma_slab_t hash_sfind(struct uma_hash *hash, uint8_t *data);
void *uma_large_malloc(vm_size_t size, int wait);
void *uma_large_malloc_domain(vm_size_t size, int domain, int wait);
void uma_large_free(uma_slab_t slab);

/* Lock Macros */

#define	KEG_LOCK_INIT(k, lc)					\
	do {							\
		if ((lc))					\
			mtx_init(&(k)->uk_lock, (k)->uk_name,	\
			    (k)->uk_name, MTX_DEF | MTX_DUPOK);	\
		else						\
			mtx_init(&(k)->uk_lock, (k)->uk_name,	\
			    "UMA zone", MTX_DEF | MTX_DUPOK);	\
	} while (0)

#define	KEG_LOCK_FINI(k)	mtx_destroy(&(k)->uk_lock)
#define	KEG_LOCK(k)	mtx_lock(&(k)->uk_lock)
#define	KEG_UNLOCK(k)	mtx_unlock(&(k)->uk_lock)
#define	KEG_LOCK_ASSERT(k)	mtx_assert(&(k)->uk_lock, MA_OWNED)

#define	KEG_GET(zone, keg) do {					\
	(keg) = (zone)->uz_keg;					\
	KASSERT((void *)(keg) != (void *)&(zone)->uz_lock,	\
	    ("%s: Invalid zone %p type", __func__, (zone)));	\
	} while (0)

#define	ZONE_LOCK_INIT(z, lc)					\
	do {							\
		if ((lc))					\
			mtx_init(&(z)->uz_lock, (z)->uz_name,	\
			    (z)->uz_name, MTX_DEF | MTX_DUPOK);	\
		else						\
			mtx_init(&(z)->uz_lock, (z)->uz_name,	\
			    "UMA zone", MTX_DEF | MTX_DUPOK);	\
	} while (0)

#define	ZONE_LOCK(z)	mtx_lock((z)->uz_lockptr)
#define	ZONE_TRYLOCK(z)	mtx_trylock((z)->uz_lockptr)
#define	ZONE_UNLOCK(z)	mtx_unlock((z)->uz_lockptr)
#define	ZONE_LOCK_FINI(z)	mtx_destroy(&(z)->uz_lock)
#define	ZONE_LOCK_ASSERT(z)	mtx_assert((z)->uz_lockptr, MA_OWNED)

/*
 * Find a slab within a hash table.  This is used for OFFPAGE zones to lookup
 * the slab structure.
 *
 * Arguments:
 *	hash  The hash table to search.
 *	data  The base page of the item.
 *
 * Returns:
 *	A pointer to a slab if successful, else NULL.
 */
static __inline uma_slab_t
hash_sfind(struct uma_hash *hash, uint8_t *data)
{
        uma_slab_t slab;
        u_int hval;

        hval = UMA_HASH(hash, data);

        SLIST_FOREACH(slab, &hash->uh_slab_hash[hval], us_hlink) {
                if ((uint8_t *)slab->us_data == data)
                        return (slab);
        }
        return (NULL);
}

static __inline uma_slab_t
vtoslab(vm_offset_t va)
{
	vm_page_t p;

	p = PHYS_TO_VM_PAGE(pmap_kextract(va));
	return ((uma_slab_t)p->plinks.s.pv);
}

static __inline void
vsetslab(vm_offset_t va, uma_slab_t slab)
{
	vm_page_t p;

	p = PHYS_TO_VM_PAGE(pmap_kextract(va));
	p->plinks.s.pv = slab;
}

/*
 * The following two functions may be defined by architecture specific code
 * if they can provide more efficient allocation functions.  This is useful
 * for using direct mapped addresses.
 */
void *uma_small_alloc(uma_zone_t zone, vm_size_t bytes, int domain,
    uint8_t *pflag, int wait);
void uma_small_free(void *mem, vm_size_t size, uint8_t flags);

/* Set a global soft limit on UMA managed memory. */
void uma_set_limit(unsigned long limit);
#endif /* _KERNEL */

#endif /* VM_UMA_INT_H */
