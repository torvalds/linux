/*
 * Written by Mark Hemment, 1996 (markhe@nextd.demon.co.uk).
 *
 * (C) SGI 2006, Christoph Lameter <clameter@sgi.com>
 * 	Cleaned up and restructured to ease the addition of alternative
 * 	implementations of SLAB allocators.
 */

#ifndef _LINUX_SLAB_H
#define	_LINUX_SLAB_H

#ifdef __KERNEL__

#include <linux/gfp.h>
#include <linux/types.h>

typedef struct kmem_cache kmem_cache_t __deprecated;

/*
 * Flags to pass to kmem_cache_create().
 * The ones marked DEBUG are only valid if CONFIG_SLAB_DEBUG is set.
 */
#define SLAB_DEBUG_FREE		0x00000100UL	/* DEBUG: Perform (expensive) checks on free */
#define SLAB_DEBUG_INITIAL	0x00000200UL	/* DEBUG: Call constructor (as verifier) */
#define SLAB_RED_ZONE		0x00000400UL	/* DEBUG: Red zone objs in a cache */
#define SLAB_POISON		0x00000800UL	/* DEBUG: Poison objects */
#define SLAB_HWCACHE_ALIGN	0x00002000UL	/* Align objs on cache lines */
#define SLAB_CACHE_DMA		0x00004000UL	/* Use GFP_DMA memory */
#define SLAB_MUST_HWCACHE_ALIGN	0x00008000UL	/* Force alignment even if debuggin is active */
#define SLAB_STORE_USER		0x00010000UL	/* DEBUG: Store the last owner for bug hunting */
#define SLAB_RECLAIM_ACCOUNT	0x00020000UL	/* Objects are reclaimable */
#define SLAB_PANIC		0x00040000UL	/* Panic if kmem_cache_create() fails */
#define SLAB_DESTROY_BY_RCU	0x00080000UL	/* Defer freeing slabs to RCU */
#define SLAB_MEM_SPREAD		0x00100000UL	/* Spread some memory over cpuset */

/* Flags passed to a constructor functions */
#define SLAB_CTOR_CONSTRUCTOR	0x001UL		/* If not set, then deconstructor */
#define SLAB_CTOR_ATOMIC	0x002UL		/* Tell constructor it can't sleep */
#define SLAB_CTOR_VERIFY	0x004UL		/* Tell constructor it's a verify call */

/*
 * struct kmem_cache related prototypes
 */
void __init kmem_cache_init(void);
extern int slab_is_available(void);

struct kmem_cache *kmem_cache_create(const char *, size_t, size_t,
			unsigned long,
			void (*)(void *, struct kmem_cache *, unsigned long),
			void (*)(void *, struct kmem_cache *, unsigned long));
void kmem_cache_destroy(struct kmem_cache *);
int kmem_cache_shrink(struct kmem_cache *);
void *kmem_cache_alloc(struct kmem_cache *, gfp_t);
void *kmem_cache_zalloc(struct kmem_cache *, gfp_t);
void kmem_cache_free(struct kmem_cache *, void *);
unsigned int kmem_cache_size(struct kmem_cache *);
const char *kmem_cache_name(struct kmem_cache *);
int kmem_ptr_validate(struct kmem_cache *cachep, const void *ptr);

#ifdef CONFIG_NUMA
extern void *kmem_cache_alloc_node(struct kmem_cache *, gfp_t flags, int node);
#else
static inline void *kmem_cache_alloc_node(struct kmem_cache *cachep,
					gfp_t flags, int node)
{
	return kmem_cache_alloc(cachep, flags);
}
#endif

/*
 * Common kmalloc functions provided by all allocators
 */
void *__kmalloc(size_t, gfp_t);
void *__kzalloc(size_t, gfp_t);
void * __must_check krealloc(const void *, size_t, gfp_t);
void kfree(const void *);
size_t ksize(const void *);

/**
 * kcalloc - allocate memory for an array. The memory is set to zero.
 * @n: number of elements.
 * @size: element size.
 * @flags: the type of memory to allocate.
 */
static inline void *kcalloc(size_t n, size_t size, gfp_t flags)
{
	if (n != 0 && size > ULONG_MAX / n)
		return NULL;
	return __kzalloc(n * size, flags);
}

/*
 * Allocator specific definitions. These are mainly used to establish optimized
 * ways to convert kmalloc() calls to kmem_cache_alloc() invocations by selecting
 * the appropriate general cache at compile time.
 */

#ifdef CONFIG_SLAB
#include <linux/slab_def.h>
#else
/*
 * Fallback definitions for an allocator not wanting to provide
 * its own optimized kmalloc definitions (like SLOB).
 */

/**
 * kmalloc - allocate memory
 * @size: how many bytes of memory are required.
 * @flags: the type of memory to allocate.
 *
 * kmalloc is the normal method of allocating memory
 * in the kernel.
 *
 * The @flags argument may be one of:
 *
 * %GFP_USER - Allocate memory on behalf of user.  May sleep.
 *
 * %GFP_KERNEL - Allocate normal kernel ram.  May sleep.
 *
 * %GFP_ATOMIC - Allocation will not sleep.
 *   For example, use this inside interrupt handlers.
 *
 * %GFP_HIGHUSER - Allocate pages from high memory.
 *
 * %GFP_NOIO - Do not do any I/O at all while trying to get memory.
 *
 * %GFP_NOFS - Do not make any fs calls while trying to get memory.
 *
 * Also it is possible to set different flags by OR'ing
 * in one or more of the following additional @flags:
 *
 * %__GFP_COLD - Request cache-cold pages instead of
 *   trying to return cache-warm pages.
 *
 * %__GFP_DMA - Request memory from the DMA-capable zone.
 *
 * %__GFP_HIGH - This allocation has high priority and may use emergency pools.
 *
 * %__GFP_HIGHMEM - Allocated memory may be from highmem.
 *
 * %__GFP_NOFAIL - Indicate that this allocation is in no way allowed to fail
 *   (think twice before using).
 *
 * %__GFP_NORETRY - If memory is not immediately available,
 *   then give up at once.
 *
 * %__GFP_NOWARN - If allocation fails, don't issue any warnings.
 *
 * %__GFP_REPEAT - If allocation fails initially, try once more before failing.
 */
static inline void *kmalloc(size_t size, gfp_t flags)
{
	return __kmalloc(size, flags);
}

/**
 * kzalloc - allocate memory. The memory is set to zero.
 * @size: how many bytes of memory are required.
 * @flags: the type of memory to allocate (see kmalloc).
 */
static inline void *kzalloc(size_t size, gfp_t flags)
{
	return __kzalloc(size, flags);
}
#endif

#ifndef CONFIG_NUMA
static inline void *kmalloc_node(size_t size, gfp_t flags, int node)
{
	return kmalloc(size, flags);
}

static inline void *__kmalloc_node(size_t size, gfp_t flags, int node)
{
	return __kmalloc(size, flags);
}
#endif /* !CONFIG_NUMA */

/*
 * kmalloc_track_caller is a special version of kmalloc that records the
 * calling function of the routine calling it for slab leak tracking instead
 * of just the calling function (confusing, eh?).
 * It's useful when the call to kmalloc comes from a widely-used standard
 * allocator where we care about the real place the memory allocation
 * request comes from.
 */
#ifdef CONFIG_DEBUG_SLAB
extern void *__kmalloc_track_caller(size_t, gfp_t, void*);
#define kmalloc_track_caller(size, flags) \
	__kmalloc_track_caller(size, flags, __builtin_return_address(0))
#else
#define kmalloc_track_caller(size, flags) \
	__kmalloc(size, flags)
#endif /* DEBUG_SLAB */

#ifdef CONFIG_NUMA
/*
 * kmalloc_node_track_caller is a special version of kmalloc_node that
 * records the calling function of the routine calling it for slab leak
 * tracking instead of just the calling function (confusing, eh?).
 * It's useful when the call to kmalloc_node comes from a widely-used
 * standard allocator where we care about the real place the memory
 * allocation request comes from.
 */
#ifdef CONFIG_DEBUG_SLAB
extern void *__kmalloc_node_track_caller(size_t, gfp_t, int, void *);
#define kmalloc_node_track_caller(size, flags, node) \
	__kmalloc_node_track_caller(size, flags, node, \
			__builtin_return_address(0))
#else
#define kmalloc_node_track_caller(size, flags, node) \
	__kmalloc_node(size, flags, node)
#endif

#else /* CONFIG_NUMA */

#define kmalloc_node_track_caller(size, flags, node) \
	kmalloc_track_caller(size, flags)

#endif /* DEBUG_SLAB */

#endif	/* __KERNEL__ */
#endif	/* _LINUX_SLAB_H */

