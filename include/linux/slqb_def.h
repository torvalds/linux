#ifndef _LINUX_SLQB_DEF_H
#define _LINUX_SLQB_DEF_H

/*
 * SLQB : A slab allocator with object queues.
 *
 * (C) 2008 Nick Piggin <npiggin@suse.de>
 */
#include <linux/types.h>
#include <linux/gfp.h>
#include <linux/workqueue.h>
#include <linux/kobject.h>
#include <linux/rcu_types.h>
#include <linux/mm_types.h>
#include <linux/kernel.h>

#define SLAB_NUMA		0x00000001UL    /* shortcut */

enum stat_item {
	ALLOC,			/* Allocation count */
	ALLOC_SLAB_FILL,	/* Fill freelist from page list */
	ALLOC_SLAB_NEW,		/* New slab acquired from page allocator */
	FREE,			/* Free count */
	FREE_REMOTE,		/* NUMA: freeing to remote list */
	FLUSH_FREE_LIST,	/* Freelist flushed */
	FLUSH_FREE_LIST_OBJECTS, /* Objects flushed from freelist */
	FLUSH_FREE_LIST_REMOTE,	/* Objects flushed from freelist to remote */
	FLUSH_SLAB_PARTIAL,	/* Freeing moves slab to partial list */
	FLUSH_SLAB_FREE,	/* Slab freed to the page allocator */
	FLUSH_RFREE_LIST,	/* Rfree list flushed */
	FLUSH_RFREE_LIST_OBJECTS, /* Rfree objects flushed */
	CLAIM_REMOTE_LIST,	/* Remote freed list claimed */
	CLAIM_REMOTE_LIST_OBJECTS, /* Remote freed objects claimed */
	NR_SLQB_STAT_ITEMS
};

/*
 * Singly-linked list with head, tail, and nr
 */
struct kmlist {
	unsigned long	nr;
	void 		**head;
	void		**tail;
};

/*
 * Every kmem_cache_list has a kmem_cache_remote_free structure, by which
 * objects can be returned to the kmem_cache_list from remote CPUs.
 */
struct kmem_cache_remote_free {
	spinlock_t	lock;
	struct kmlist	list;
} ____cacheline_aligned;

/*
 * A kmem_cache_list manages all the slabs and objects allocated from a given
 * source. Per-cpu kmem_cache_lists allow node-local allocations. Per-node
 * kmem_cache_lists allow off-node allocations (but require locking).
 */
struct kmem_cache_list {
				/* Fastpath LIFO freelist of objects */
	struct kmlist		freelist;
#ifdef CONFIG_SMP
				/* remote_free has reached a watermark */
	int			remote_free_check;
#endif
				/* kmem_cache corresponding to this list */
	struct kmem_cache	*cache;

				/* Number of partial slabs (pages) */
	unsigned long		nr_partial;

				/* Slabs which have some free objects */
	struct list_head	partial;

				/* Total number of slabs allocated */
	unsigned long		nr_slabs;

				/* Protects nr_partial, nr_slabs, and partial */
	spinlock_t		page_lock;

#ifdef CONFIG_SMP
	/*
	 * In the case of per-cpu lists, remote_free is for objects freed by
	 * non-owner CPU back to its home list. For per-node lists, remote_free
	 * is always used to free objects.
	 */
	struct kmem_cache_remote_free remote_free;
#endif

#ifdef CONFIG_SLQB_STATS
	unsigned long		stats[NR_SLQB_STAT_ITEMS];
#endif
} ____cacheline_aligned;

/*
 * Primary per-cpu, per-kmem_cache structure.
 */
struct kmem_cache_cpu {
	struct kmem_cache_list	list;		/* List for node-local slabs */
	unsigned int		colour_next;	/* Next colour offset to use */

#ifdef CONFIG_SMP
	/*
	 * rlist is a list of objects that don't fit on list.freelist (ie.
	 * wrong node). The objects all correspond to a given kmem_cache_list,
	 * remote_cache_list. To free objects to another list, we must first
	 * flush the existing objects, then switch remote_cache_list.
	 *
	 * An NR_CPUS or MAX_NUMNODES array would be nice here, but then we
	 * get to O(NR_CPUS^2) memory consumption situation.
	 */
	struct kmlist		rlist;
	struct kmem_cache_list	*remote_cache_list;
#endif
} ____cacheline_aligned_in_smp;

/*
 * Per-node, per-kmem_cache structure. Used for node-specific allocations.
 */
struct kmem_cache_node {
	struct kmem_cache_list	list;
	spinlock_t		list_lock;	/* protects access to list */
} ____cacheline_aligned;

/*
 * Management object for a slab cache.
 */
struct kmem_cache {
	unsigned long	flags;
	int		hiwater;	/* LIFO list high watermark */
	int		freebatch;	/* LIFO freelist batch flush size */
#ifdef CONFIG_SMP
	struct kmem_cache_cpu	**cpu_slab; /* dynamic per-cpu structures */
#else
	struct kmem_cache_cpu	cpu_slab;
#endif
	int		objsize;	/* Size of object without meta data */
	int		offset;		/* Free pointer offset. */
	int		objects;	/* Number of objects in slab */

#ifdef CONFIG_NUMA
	struct kmem_cache_node	**node_slab; /* dynamic per-node structures */
#endif

	int		size;		/* Size of object including meta data */
	int		order;		/* Allocation order */
	gfp_t		allocflags;	/* gfp flags to use on allocation */
	unsigned int	colour_range;	/* range of colour counter */
	unsigned int	colour_off;	/* offset per colour */
	void		(*ctor)(void *);

	const char	*name;		/* Name (only for display!) */
	struct list_head list;		/* List of slab caches */

	int		align;		/* Alignment */
	int		inuse;		/* Offset to metadata */

#ifdef CONFIG_SLQB_SYSFS
	struct kobject	kobj;		/* For sysfs */
#endif
} ____cacheline_aligned;

/*
 * Kmalloc subsystem.
 */
#if defined(ARCH_KMALLOC_MINALIGN) && ARCH_KMALLOC_MINALIGN > 8
#define KMALLOC_MIN_SIZE ARCH_KMALLOC_MINALIGN
#else
#define KMALLOC_MIN_SIZE 8
#endif

#define KMALLOC_SHIFT_LOW ilog2(KMALLOC_MIN_SIZE)
#define KMALLOC_SHIFT_SLQB_HIGH (PAGE_SHIFT + 			\
				 ((9 <= (MAX_ORDER - 1)) ? 9 : (MAX_ORDER - 1)))

extern struct kmem_cache kmalloc_caches[KMALLOC_SHIFT_SLQB_HIGH + 1];
extern struct kmem_cache kmalloc_caches_dma[KMALLOC_SHIFT_SLQB_HIGH + 1];

/*
 * Constant size allocations use this path to find index into kmalloc caches
 * arrays. get_slab() function is used for non-constant sizes.
 */
static __always_inline int kmalloc_index(size_t size)
{
	extern int ____kmalloc_too_large(void);

	if (unlikely(size <= KMALLOC_MIN_SIZE))
		return KMALLOC_SHIFT_LOW;

#if L1_CACHE_BYTES < 64
	if (size > 64 && size <= 96)
		return 1;
#endif
#if L1_CACHE_BYTES < 128
	if (size > 128 && size <= 192)
		return 2;
#endif
	if (size <=	  8) return 3;
	if (size <=	 16) return 4;
	if (size <=	 32) return 5;
	if (size <=	 64) return 6;
	if (size <=	128) return 7;
	if (size <=	256) return 8;
	if (size <=	512) return 9;
	if (size <=       1024) return 10;
	if (size <=   2 * 1024) return 11;
	if (size <=   4 * 1024) return 12;
	if (size <=   8 * 1024) return 13;
	if (size <=  16 * 1024) return 14;
	if (size <=  32 * 1024) return 15;
	if (size <=  64 * 1024) return 16;
	if (size <= 128 * 1024) return 17;
	if (size <= 256 * 1024) return 18;
	if (size <= 512 * 1024) return 19;
	if (size <= 1024 * 1024) return 20;
	if (size <=  2 * 1024 * 1024) return 21;
	if (size <=  4 * 1024 * 1024) return 22;
	if (size <=  8 * 1024 * 1024) return 23;
	if (size <=  16 * 1024 * 1024) return 24;
	if (size <=  32 * 1024 * 1024) return 25;
	return ____kmalloc_too_large();
}

#ifdef CONFIG_ZONE_DMA
#define SLQB_DMA __GFP_DMA
#else
/* Disable "DMA slabs" */
#define SLQB_DMA (__force gfp_t)0
#endif

/*
 * Find the kmalloc slab cache for a given combination of allocation flags and
 * size. Should really only be used for constant 'size' arguments, due to
 * bloat.
 */
static __always_inline struct kmem_cache *kmalloc_slab(size_t size, gfp_t flags)
{
	int index;

	if (unlikely(size > 1UL << KMALLOC_SHIFT_SLQB_HIGH))
		return NULL;
	if (unlikely(!size))
		return ZERO_SIZE_PTR;

	index = kmalloc_index(size);
	if (likely(!(flags & SLQB_DMA)))
		return &kmalloc_caches[index];
	else
		return &kmalloc_caches_dma[index];
}

void *kmem_cache_alloc(struct kmem_cache *, gfp_t);
void *__kmalloc(size_t size, gfp_t flags);

#ifndef ARCH_KMALLOC_MINALIGN
#define ARCH_KMALLOC_MINALIGN __alignof__(unsigned long long)
#endif

#ifndef ARCH_SLAB_MINALIGN
#define ARCH_SLAB_MINALIGN __alignof__(unsigned long long)
#endif

#define KMALLOC_HEADER (ARCH_KMALLOC_MINALIGN < sizeof(void *) ?	\
				sizeof(void *) : ARCH_KMALLOC_MINALIGN)

static __always_inline void *kmalloc(size_t size, gfp_t flags)
{
	if (__builtin_constant_p(size)) {
		struct kmem_cache *s;

		s = kmalloc_slab(size, flags);
		if (unlikely(ZERO_OR_NULL_PTR(s)))
			return s;

		return kmem_cache_alloc(s, flags);
	}
	return __kmalloc(size, flags);
}

#ifdef CONFIG_NUMA
void *__kmalloc_node(size_t size, gfp_t flags, int node);
void *kmem_cache_alloc_node(struct kmem_cache *, gfp_t flags, int node);

static __always_inline void *kmalloc_node(size_t size, gfp_t flags, int node)
{
	if (__builtin_constant_p(size)) {
		struct kmem_cache *s;

		s = kmalloc_slab(size, flags);
		if (unlikely(ZERO_OR_NULL_PTR(s)))
			return s;

		return kmem_cache_alloc_node(s, flags, node);
	}
	return __kmalloc_node(size, flags, node);
}
#endif

#endif /* _LINUX_SLQB_DEF_H */
