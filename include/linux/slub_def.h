#ifndef _LINUX_SLUB_DEF_H
#define _LINUX_SLUB_DEF_H

/*
 * SLUB : A Slab allocator without object queues.
 *
 * (C) 2007 SGI, Christoph Lameter
 */
#include <linux/types.h>
#include <linux/gfp.h>
#include <linux/bug.h>
#include <linux/workqueue.h>
#include <linux/kobject.h>

#include <linux/kmemleak.h>

enum stat_item {
	ALLOC_FASTPATH,		/* Allocation from cpu slab */
	ALLOC_SLOWPATH,		/* Allocation by getting a new cpu slab */
	FREE_FASTPATH,		/* Free to cpu slub */
	FREE_SLOWPATH,		/* Freeing not to cpu slab */
	FREE_FROZEN,		/* Freeing to frozen slab */
	FREE_ADD_PARTIAL,	/* Freeing moves slab to partial list */
	FREE_REMOVE_PARTIAL,	/* Freeing removes last object */
	ALLOC_FROM_PARTIAL,	/* Cpu slab acquired from node partial list */
	ALLOC_SLAB,		/* Cpu slab acquired from page allocator */
	ALLOC_REFILL,		/* Refill cpu slab from slab freelist */
	ALLOC_NODE_MISMATCH,	/* Switching cpu slab */
	FREE_SLAB,		/* Slab freed to the page allocator */
	CPUSLAB_FLUSH,		/* Abandoning of the cpu slab */
	DEACTIVATE_FULL,	/* Cpu slab was full when deactivated */
	DEACTIVATE_EMPTY,	/* Cpu slab was empty when deactivated */
	DEACTIVATE_TO_HEAD,	/* Cpu slab was moved to the head of partials */
	DEACTIVATE_TO_TAIL,	/* Cpu slab was moved to the tail of partials */
	DEACTIVATE_REMOTE_FREES,/* Slab contained remotely freed objects */
	DEACTIVATE_BYPASS,	/* Implicit deactivation */
	ORDER_FALLBACK,		/* Number of times fallback was necessary */
	CMPXCHG_DOUBLE_CPU_FAIL,/* Failure of this_cpu_cmpxchg_double */
	CMPXCHG_DOUBLE_FAIL,	/* Number of times that cmpxchg double did not match */
	CPU_PARTIAL_ALLOC,	/* Used cpu partial on alloc */
	CPU_PARTIAL_FREE,	/* Refill cpu partial on free */
	CPU_PARTIAL_NODE,	/* Refill cpu partial from node partial */
	CPU_PARTIAL_DRAIN,	/* Drain cpu partial to node partial */
	NR_SLUB_STAT_ITEMS };

struct kmem_cache_cpu {
	void **freelist;	/* Pointer to next available object */
	unsigned long tid;	/* Globally unique transaction id */
	struct page *page;	/* The slab from which we are allocating */
	struct page *partial;	/* Partially allocated frozen slabs */
#ifdef CONFIG_SLUB_STATS
	unsigned stat[NR_SLUB_STAT_ITEMS];
#endif
};

struct kmem_cache_node {
	spinlock_t list_lock;	/* Protect partial list and nr_partial */
	unsigned long nr_partial;
	struct list_head partial;
#ifdef CONFIG_SLUB_DEBUG
	atomic_long_t nr_slabs;
	atomic_long_t total_objects;
	struct list_head full;
#endif
};

/*
 * Word size structure that can be atomically updated or read and that
 * contains both the order and the number of objects that a slab of the
 * given order would contain.
 */
struct kmem_cache_order_objects {
	unsigned long x;
};

/*
 * Slab cache management.
 */
struct kmem_cache {
	struct kmem_cache_cpu __percpu *cpu_slab;
	/* Used for retriving partial slabs etc */
	unsigned long flags;
	unsigned long min_partial;
	int size;		/* The size of an object including meta data */
	int objsize;		/* The size of an object without meta data */
	int offset;		/* Free pointer offset. */
	int cpu_partial;	/* Number of per cpu partial objects to keep around */
	struct kmem_cache_order_objects oo;

	/* Allocation and freeing of slabs */
	struct kmem_cache_order_objects max;
	struct kmem_cache_order_objects min;
	gfp_t allocflags;	/* gfp flags to use on each alloc */
	int refcount;		/* Refcount for slab cache destroy */
	void (*ctor)(void *);
	int inuse;		/* Offset to metadata */
	int align;		/* Alignment */
	int reserved;		/* Reserved bytes at the end of slabs */
	const char *name;	/* Name (only for display!) */
	struct list_head list;	/* List of slab caches */
#ifdef CONFIG_SYSFS
	struct kobject kobj;	/* For sysfs */
#endif

#ifdef CONFIG_NUMA
	/*
	 * Defragmentation by allocating from a remote node.
	 */
	int remote_node_defrag_ratio;
#endif
	struct kmem_cache_node *node[MAX_NUMNODES];
};

/*
 * Kmalloc subsystem.
 */
#if defined(ARCH_DMA_MINALIGN) && ARCH_DMA_MINALIGN > 8
#define KMALLOC_MIN_SIZE ARCH_DMA_MINALIGN
#else
#define KMALLOC_MIN_SIZE 8
#endif

#define KMALLOC_SHIFT_LOW ilog2(KMALLOC_MIN_SIZE)

/*
 * Maximum kmalloc object size handled by SLUB. Larger object allocations
 * are passed through to the page allocator. The page allocator "fastpath"
 * is relatively slow so we need this value sufficiently high so that
 * performance critical objects are allocated through the SLUB fastpath.
 *
 * This should be dropped to PAGE_SIZE / 2 once the page allocator
 * "fastpath" becomes competitive with the slab allocator fastpaths.
 */
#define SLUB_MAX_SIZE (2 * PAGE_SIZE)

#define SLUB_PAGE_SHIFT (PAGE_SHIFT + 2)

#ifdef CONFIG_ZONE_DMA
#define SLUB_DMA __GFP_DMA
#else
/* Disable DMA functionality */
#define SLUB_DMA (__force gfp_t)0
#endif

/*
 * We keep the general caches in an array of slab caches that are used for
 * 2^x bytes of allocations.
 */
extern struct kmem_cache *kmalloc_caches[SLUB_PAGE_SHIFT];

/*
 * Sorry that the following has to be that ugly but some versions of GCC
 * have trouble with constant propagation and loops.
 */
static __always_inline int kmalloc_index(size_t size)
{
	if (!size)
		return 0;

	if (size <= KMALLOC_MIN_SIZE)
		return KMALLOC_SHIFT_LOW;

	if (KMALLOC_MIN_SIZE <= 32 && size > 64 && size <= 96)
		return 1;
	if (KMALLOC_MIN_SIZE <= 64 && size > 128 && size <= 192)
		return 2;
	if (size <=          8) return 3;
	if (size <=         16) return 4;
	if (size <=         32) return 5;
	if (size <=         64) return 6;
	if (size <=        128) return 7;
	if (size <=        256) return 8;
	if (size <=        512) return 9;
	if (size <=       1024) return 10;
	if (size <=   2 * 1024) return 11;
	if (size <=   4 * 1024) return 12;
/*
 * The following is only needed to support architectures with a larger page
 * size than 4k. We need to support 2 * PAGE_SIZE here. So for a 64k page
 * size we would have to go up to 128k.
 */
	if (size <=   8 * 1024) return 13;
	if (size <=  16 * 1024) return 14;
	if (size <=  32 * 1024) return 15;
	if (size <=  64 * 1024) return 16;
	if (size <= 128 * 1024) return 17;
	if (size <= 256 * 1024) return 18;
	if (size <= 512 * 1024) return 19;
	if (size <= 1024 * 1024) return 20;
	if (size <=  2 * 1024 * 1024) return 21;
	BUG();
	return -1; /* Will never be reached */

/*
 * What we really wanted to do and cannot do because of compiler issues is:
 *	int i;
 *	for (i = KMALLOC_SHIFT_LOW; i <= KMALLOC_SHIFT_HIGH; i++)
 *		if (size <= (1 << i))
 *			return i;
 */
}

/*
 * Find the slab cache for a given combination of allocation flags and size.
 *
 * This ought to end up with a global pointer to the right cache
 * in kmalloc_caches.
 */
static __always_inline struct kmem_cache *kmalloc_slab(size_t size)
{
	int index = kmalloc_index(size);

	if (index == 0)
		return NULL;

	return kmalloc_caches[index];
}

void *kmem_cache_alloc(struct kmem_cache *, gfp_t);
void *__kmalloc(size_t size, gfp_t flags);

static __always_inline void *
kmalloc_order(size_t size, gfp_t flags, unsigned int order)
{
	void *ret = (void *) __get_free_pages(flags | __GFP_COMP, order);
	kmemleak_alloc(ret, size, 1, flags);
	return ret;
}

/**
 * Calling this on allocated memory will check that the memory
 * is expected to be in use, and print warnings if not.
 */
#ifdef CONFIG_SLUB_DEBUG
extern bool verify_mem_not_deleted(const void *x);
#else
static inline bool verify_mem_not_deleted(const void *x)
{
	return true;
}
#endif

#ifdef CONFIG_TRACING
extern void *
kmem_cache_alloc_trace(struct kmem_cache *s, gfp_t gfpflags, size_t size);
extern void *kmalloc_order_trace(size_t size, gfp_t flags, unsigned int order);
#else
static __always_inline void *
kmem_cache_alloc_trace(struct kmem_cache *s, gfp_t gfpflags, size_t size)
{
	return kmem_cache_alloc(s, gfpflags);
}

static __always_inline void *
kmalloc_order_trace(size_t size, gfp_t flags, unsigned int order)
{
	return kmalloc_order(size, flags, order);
}
#endif

static __always_inline void *kmalloc_large(size_t size, gfp_t flags)
{
	unsigned int order = get_order(size);
	return kmalloc_order_trace(size, flags, order);
}

static __always_inline void *kmalloc(size_t size, gfp_t flags)
{
	if (__builtin_constant_p(size)) {
		if (size > SLUB_MAX_SIZE)
			return kmalloc_large(size, flags);

		if (!(flags & SLUB_DMA)) {
			struct kmem_cache *s = kmalloc_slab(size);

			if (!s)
				return ZERO_SIZE_PTR;

			return kmem_cache_alloc_trace(s, flags, size);
		}
	}
	return __kmalloc(size, flags);
}

#ifdef CONFIG_NUMA
void *__kmalloc_node(size_t size, gfp_t flags, int node);
void *kmem_cache_alloc_node(struct kmem_cache *, gfp_t flags, int node);

#ifdef CONFIG_TRACING
extern void *kmem_cache_alloc_node_trace(struct kmem_cache *s,
					   gfp_t gfpflags,
					   int node, size_t size);
#else
static __always_inline void *
kmem_cache_alloc_node_trace(struct kmem_cache *s,
			      gfp_t gfpflags,
			      int node, size_t size)
{
	return kmem_cache_alloc_node(s, gfpflags, node);
}
#endif

static __always_inline void *kmalloc_node(size_t size, gfp_t flags, int node)
{
	if (__builtin_constant_p(size) &&
		size <= SLUB_MAX_SIZE && !(flags & SLUB_DMA)) {
			struct kmem_cache *s = kmalloc_slab(size);

		if (!s)
			return ZERO_SIZE_PTR;

		return kmem_cache_alloc_node_trace(s, flags, node, size);
	}
	return __kmalloc_node(size, flags, node);
}
#endif

#endif /* _LINUX_SLUB_DEF_H */
