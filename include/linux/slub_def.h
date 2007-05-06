#ifndef _LINUX_SLUB_DEF_H
#define _LINUX_SLUB_DEF_H

/*
 * SLUB : A Slab allocator without object queues.
 *
 * (C) 2007 SGI, Christoph Lameter <clameter@sgi.com>
 */
#include <linux/types.h>
#include <linux/gfp.h>
#include <linux/workqueue.h>
#include <linux/kobject.h>

struct kmem_cache_node {
	spinlock_t list_lock;	/* Protect partial list and nr_partial */
	unsigned long nr_partial;
	atomic_long_t nr_slabs;
	struct list_head partial;
};

/*
 * Slab cache management.
 */
struct kmem_cache {
	/* Used for retriving partial slabs etc */
	unsigned long flags;
	int size;		/* The size of an object including meta data */
	int objsize;		/* The size of an object without meta data */
	int offset;		/* Free pointer offset. */
	unsigned int order;

	/*
	 * Avoid an extra cache line for UP, SMP and for the node local to
	 * struct kmem_cache.
	 */
	struct kmem_cache_node local_node;

	/* Allocation and freeing of slabs */
	int objects;		/* Number of objects in slab */
	int refcount;		/* Refcount for slab cache destroy */
	void (*ctor)(void *, struct kmem_cache *, unsigned long);
	void (*dtor)(void *, struct kmem_cache *, unsigned long);
	int inuse;		/* Offset to metadata */
	int align;		/* Alignment */
	const char *name;	/* Name (only for display!) */
	struct list_head list;	/* List of slab caches */
	struct kobject kobj;	/* For sysfs */

#ifdef CONFIG_NUMA
	int defrag_ratio;
	struct kmem_cache_node *node[MAX_NUMNODES];
#endif
	struct page *cpu_slab[NR_CPUS];
};

/*
 * Kmalloc subsystem.
 */
#define KMALLOC_SHIFT_LOW 3

#ifdef CONFIG_LARGE_ALLOCS
#define KMALLOC_SHIFT_HIGH 25
#else
#if !defined(CONFIG_MMU) || NR_CPUS > 512 || MAX_NUMNODES > 256
#define KMALLOC_SHIFT_HIGH 20
#else
#define KMALLOC_SHIFT_HIGH 18
#endif
#endif

/*
 * We keep the general caches in an array of slab caches that are used for
 * 2^x bytes of allocations.
 */
extern struct kmem_cache kmalloc_caches[KMALLOC_SHIFT_HIGH + 1];

/*
 * Sorry that the following has to be that ugly but some versions of GCC
 * have trouble with constant propagation and loops.
 */
static inline int kmalloc_index(int size)
{
	/*
	 * We should return 0 if size == 0 but we use the smallest object
	 * here for SLAB legacy reasons.
	 */
	WARN_ON_ONCE(size == 0);

	if (size > 64 && size <= 96)
		return 1;
	if (size > 128 && size <= 192)
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
	if (size <=   8 * 1024) return 13;
	if (size <=  16 * 1024) return 14;
	if (size <=  32 * 1024) return 15;
	if (size <=  64 * 1024) return 16;
	if (size <= 128 * 1024) return 17;
	if (size <= 256 * 1024) return 18;
#if KMALLOC_SHIFT_HIGH > 18
	if (size <=  512 * 1024) return 19;
	if (size <= 1024 * 1024) return 20;
#endif
#if KMALLOC_SHIFT_HIGH > 20
	if (size <=  2 * 1024 * 1024) return 21;
	if (size <=  4 * 1024 * 1024) return 22;
	if (size <=  8 * 1024 * 1024) return 23;
	if (size <= 16 * 1024 * 1024) return 24;
	if (size <= 32 * 1024 * 1024) return 25;
#endif
	return -1;

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
static inline struct kmem_cache *kmalloc_slab(size_t size)
{
	int index = kmalloc_index(size);

	if (index == 0)
		return NULL;

	if (index < 0) {
		/*
		 * Generate a link failure. Would be great if we could
		 * do something to stop the compile here.
		 */
		extern void __kmalloc_size_too_large(void);
		__kmalloc_size_too_large();
	}
	return &kmalloc_caches[index];
}

#ifdef CONFIG_ZONE_DMA
#define SLUB_DMA __GFP_DMA
#else
/* Disable DMA functionality */
#define SLUB_DMA 0
#endif

static inline void *kmalloc(size_t size, gfp_t flags)
{
	if (__builtin_constant_p(size) && !(flags & SLUB_DMA)) {
		struct kmem_cache *s = kmalloc_slab(size);

		if (!s)
			return NULL;

		return kmem_cache_alloc(s, flags);
	} else
		return __kmalloc(size, flags);
}

static inline void *kzalloc(size_t size, gfp_t flags)
{
	if (__builtin_constant_p(size) && !(flags & SLUB_DMA)) {
		struct kmem_cache *s = kmalloc_slab(size);

		if (!s)
			return NULL;

		return kmem_cache_zalloc(s, flags);
	} else
		return __kzalloc(size, flags);
}

#ifdef CONFIG_NUMA
extern void *__kmalloc_node(size_t size, gfp_t flags, int node);

static inline void *kmalloc_node(size_t size, gfp_t flags, int node)
{
	if (__builtin_constant_p(size) && !(flags & SLUB_DMA)) {
		struct kmem_cache *s = kmalloc_slab(size);

		if (!s)
			return NULL;

		return kmem_cache_alloc_node(s, flags, node);
	} else
		return __kmalloc_node(size, flags, node);
}
#endif

#endif /* _LINUX_SLUB_DEF_H */
