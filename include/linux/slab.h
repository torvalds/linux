/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Written by Mark Hemment, 1996 (markhe@nextd.demon.co.uk).
 *
 * (C) SGI 2006, Christoph Lameter
 * 	Cleaned up and restructured to ease the addition of alternative
 * 	implementations of SLAB allocators.
 * (C) Linux Foundation 2008-2013
 *      Unified interface for all slab allocators
 */

#ifndef _LINUX_SLAB_H
#define	_LINUX_SLAB_H

#include <linux/cache.h>
#include <linux/gfp.h>
#include <linux/overflow.h>
#include <linux/types.h>
#include <linux/workqueue.h>
#include <linux/percpu-refcount.h>


/*
 * Flags to pass to kmem_cache_create().
 * The ones marked DEBUG are only valid if CONFIG_DEBUG_SLAB is set.
 */
/* DEBUG: Perform (expensive) checks on alloc/free */
#define SLAB_CONSISTENCY_CHECKS	((slab_flags_t __force)0x00000100U)
/* DEBUG: Red zone objs in a cache */
#define SLAB_RED_ZONE		((slab_flags_t __force)0x00000400U)
/* DEBUG: Poison objects */
#define SLAB_POISON		((slab_flags_t __force)0x00000800U)
/* Indicate a kmalloc slab */
#define SLAB_KMALLOC		((slab_flags_t __force)0x00001000U)
/* Align objs on cache lines */
#define SLAB_HWCACHE_ALIGN	((slab_flags_t __force)0x00002000U)
/* Use GFP_DMA memory */
#define SLAB_CACHE_DMA		((slab_flags_t __force)0x00004000U)
/* Use GFP_DMA32 memory */
#define SLAB_CACHE_DMA32	((slab_flags_t __force)0x00008000U)
/* DEBUG: Store the last owner for bug hunting */
#define SLAB_STORE_USER		((slab_flags_t __force)0x00010000U)
/* Panic if kmem_cache_create() fails */
#define SLAB_PANIC		((slab_flags_t __force)0x00040000U)
/*
 * SLAB_TYPESAFE_BY_RCU - **WARNING** READ THIS!
 *
 * This delays freeing the SLAB page by a grace period, it does _NOT_
 * delay object freeing. This means that if you do kmem_cache_free()
 * that memory location is free to be reused at any time. Thus it may
 * be possible to see another object there in the same RCU grace period.
 *
 * This feature only ensures the memory location backing the object
 * stays valid, the trick to using this is relying on an independent
 * object validation pass. Something like:
 *
 *  rcu_read_lock()
 * again:
 *  obj = lockless_lookup(key);
 *  if (obj) {
 *    if (!try_get_ref(obj)) // might fail for free objects
 *      goto again;
 *
 *    if (obj->key != key) { // not the object we expected
 *      put_ref(obj);
 *      goto again;
 *    }
 *  }
 *  rcu_read_unlock();
 *
 * This is useful if we need to approach a kernel structure obliquely,
 * from its address obtained without the usual locking. We can lock
 * the structure to stabilize it and check it's still at the given address,
 * only if we can be sure that the memory has not been meanwhile reused
 * for some other kind of object (which our subsystem's lock might corrupt).
 *
 * rcu_read_lock before reading the address, then rcu_read_unlock after
 * taking the spinlock within the structure expected at that address.
 *
 * Note that it is not possible to acquire a lock within a structure
 * allocated with SLAB_TYPESAFE_BY_RCU without first acquiring a reference
 * as described above.  The reason is that SLAB_TYPESAFE_BY_RCU pages
 * are not zeroed before being given to the slab, which means that any
 * locks must be initialized after each and every kmem_struct_alloc().
 * Alternatively, make the ctor passed to kmem_cache_create() initialize
 * the locks at page-allocation time, as is done in __i915_request_ctor(),
 * sighand_ctor(), and anon_vma_ctor().  Such a ctor permits readers
 * to safely acquire those ctor-initialized locks under rcu_read_lock()
 * protection.
 *
 * Note that SLAB_TYPESAFE_BY_RCU was originally named SLAB_DESTROY_BY_RCU.
 */
/* Defer freeing slabs to RCU */
#define SLAB_TYPESAFE_BY_RCU	((slab_flags_t __force)0x00080000U)
/* Spread some memory over cpuset */
#define SLAB_MEM_SPREAD		((slab_flags_t __force)0x00100000U)
/* Trace allocations and frees */
#define SLAB_TRACE		((slab_flags_t __force)0x00200000U)

/* Flag to prevent checks on free */
#ifdef CONFIG_DEBUG_OBJECTS
# define SLAB_DEBUG_OBJECTS	((slab_flags_t __force)0x00400000U)
#else
# define SLAB_DEBUG_OBJECTS	0
#endif

/* Avoid kmemleak tracing */
#define SLAB_NOLEAKTRACE	((slab_flags_t __force)0x00800000U)

/* Fault injection mark */
#ifdef CONFIG_FAILSLAB
# define SLAB_FAILSLAB		((slab_flags_t __force)0x02000000U)
#else
# define SLAB_FAILSLAB		0
#endif
/* Account to memcg */
#ifdef CONFIG_MEMCG_KMEM
# define SLAB_ACCOUNT		((slab_flags_t __force)0x04000000U)
#else
# define SLAB_ACCOUNT		0
#endif

#ifdef CONFIG_KASAN_GENERIC
#define SLAB_KASAN		((slab_flags_t __force)0x08000000U)
#else
#define SLAB_KASAN		0
#endif

/*
 * Ignore user specified debugging flags.
 * Intended for caches created for self-tests so they have only flags
 * specified in the code and other flags are ignored.
 */
#define SLAB_NO_USER_FLAGS	((slab_flags_t __force)0x10000000U)

#ifdef CONFIG_KFENCE
#define SLAB_SKIP_KFENCE	((slab_flags_t __force)0x20000000U)
#else
#define SLAB_SKIP_KFENCE	0
#endif

/* The following flags affect the page allocator grouping pages by mobility */
/* Objects are reclaimable */
#ifndef CONFIG_SLUB_TINY
#define SLAB_RECLAIM_ACCOUNT	((slab_flags_t __force)0x00020000U)
#else
#define SLAB_RECLAIM_ACCOUNT	((slab_flags_t __force)0)
#endif
#define SLAB_TEMPORARY		SLAB_RECLAIM_ACCOUNT	/* Objects are short-lived */

/*
 * ZERO_SIZE_PTR will be returned for zero sized kmalloc requests.
 *
 * Dereferencing ZERO_SIZE_PTR will lead to a distinct access fault.
 *
 * ZERO_SIZE_PTR can be passed to kfree though in the same way that NULL can.
 * Both make kfree a no-op.
 */
#define ZERO_SIZE_PTR ((void *)16)

#define ZERO_OR_NULL_PTR(x) ((unsigned long)(x) <= \
				(unsigned long)ZERO_SIZE_PTR)

#include <linux/kasan.h>

struct list_lru;
struct mem_cgroup;
/*
 * struct kmem_cache related prototypes
 */
bool slab_is_available(void);

struct kmem_cache *kmem_cache_create(const char *name, unsigned int size,
			unsigned int align, slab_flags_t flags,
			void (*ctor)(void *));
struct kmem_cache *kmem_cache_create_usercopy(const char *name,
			unsigned int size, unsigned int align,
			slab_flags_t flags,
			unsigned int useroffset, unsigned int usersize,
			void (*ctor)(void *));
void kmem_cache_destroy(struct kmem_cache *s);
int kmem_cache_shrink(struct kmem_cache *s);

/*
 * Please use this macro to create slab caches. Simply specify the
 * name of the structure and maybe some flags that are listed above.
 *
 * The alignment of the struct determines object alignment. If you
 * f.e. add ____cacheline_aligned_in_smp to the struct declaration
 * then the objects will be properly aligned in SMP configurations.
 */
#define KMEM_CACHE(__struct, __flags)					\
		kmem_cache_create(#__struct, sizeof(struct __struct),	\
			__alignof__(struct __struct), (__flags), NULL)

/*
 * To whitelist a single field for copying to/from usercopy, use this
 * macro instead for KMEM_CACHE() above.
 */
#define KMEM_CACHE_USERCOPY(__struct, __flags, __field)			\
		kmem_cache_create_usercopy(#__struct,			\
			sizeof(struct __struct),			\
			__alignof__(struct __struct), (__flags),	\
			offsetof(struct __struct, __field),		\
			sizeof_field(struct __struct, __field), NULL)

/*
 * Common kmalloc functions provided by all allocators
 */
void * __must_check krealloc(const void *objp, size_t new_size, gfp_t flags) __realloc_size(2);
void kfree(const void *objp);
void kfree_sensitive(const void *objp);
size_t __ksize(const void *objp);

/**
 * ksize - Report actual allocation size of associated object
 *
 * @objp: Pointer returned from a prior kmalloc()-family allocation.
 *
 * This should not be used for writing beyond the originally requested
 * allocation size. Either use krealloc() or round up the allocation size
 * with kmalloc_size_roundup() prior to allocation. If this is used to
 * access beyond the originally requested allocation size, UBSAN_BOUNDS
 * and/or FORTIFY_SOURCE may trip, since they only know about the
 * originally allocated size via the __alloc_size attribute.
 */
size_t ksize(const void *objp);

#ifdef CONFIG_PRINTK
bool kmem_valid_obj(void *object);
void kmem_dump_obj(void *object);
#endif

/*
 * Some archs want to perform DMA into kmalloc caches and need a guaranteed
 * alignment larger than the alignment of a 64-bit integer.
 * Setting ARCH_DMA_MINALIGN in arch headers allows that.
 */
#ifdef ARCH_HAS_DMA_MINALIGN
#if ARCH_DMA_MINALIGN > 8 && !defined(ARCH_KMALLOC_MINALIGN)
#define ARCH_KMALLOC_MINALIGN ARCH_DMA_MINALIGN
#endif
#endif

#ifndef ARCH_KMALLOC_MINALIGN
#define ARCH_KMALLOC_MINALIGN __alignof__(unsigned long long)
#elif ARCH_KMALLOC_MINALIGN > 8
#define KMALLOC_MIN_SIZE ARCH_KMALLOC_MINALIGN
#define KMALLOC_SHIFT_LOW ilog2(KMALLOC_MIN_SIZE)
#endif

/*
 * Setting ARCH_SLAB_MINALIGN in arch headers allows a different alignment.
 * Intended for arches that get misalignment faults even for 64 bit integer
 * aligned buffers.
 */
#ifndef ARCH_SLAB_MINALIGN
#define ARCH_SLAB_MINALIGN __alignof__(unsigned long long)
#endif

/*
 * Arches can define this function if they want to decide the minimum slab
 * alignment at runtime. The value returned by the function must be a power
 * of two and >= ARCH_SLAB_MINALIGN.
 */
#ifndef arch_slab_minalign
static inline unsigned int arch_slab_minalign(void)
{
	return ARCH_SLAB_MINALIGN;
}
#endif

/*
 * kmem_cache_alloc and friends return pointers aligned to ARCH_SLAB_MINALIGN.
 * kmalloc and friends return pointers aligned to both ARCH_KMALLOC_MINALIGN
 * and ARCH_SLAB_MINALIGN, but here we only assume the former alignment.
 */
#define __assume_kmalloc_alignment __assume_aligned(ARCH_KMALLOC_MINALIGN)
#define __assume_slab_alignment __assume_aligned(ARCH_SLAB_MINALIGN)
#define __assume_page_alignment __assume_aligned(PAGE_SIZE)

/*
 * Kmalloc array related definitions
 */

#ifdef CONFIG_SLAB
/*
 * SLAB and SLUB directly allocates requests fitting in to an order-1 page
 * (PAGE_SIZE*2).  Larger requests are passed to the page allocator.
 */
#define KMALLOC_SHIFT_HIGH	(PAGE_SHIFT + 1)
#define KMALLOC_SHIFT_MAX	(MAX_ORDER + PAGE_SHIFT)
#ifndef KMALLOC_SHIFT_LOW
#define KMALLOC_SHIFT_LOW	5
#endif
#endif

#ifdef CONFIG_SLUB
#define KMALLOC_SHIFT_HIGH	(PAGE_SHIFT + 1)
#define KMALLOC_SHIFT_MAX	(MAX_ORDER + PAGE_SHIFT)
#ifndef KMALLOC_SHIFT_LOW
#define KMALLOC_SHIFT_LOW	3
#endif
#endif

/* Maximum allocatable size */
#define KMALLOC_MAX_SIZE	(1UL << KMALLOC_SHIFT_MAX)
/* Maximum size for which we actually use a slab cache */
#define KMALLOC_MAX_CACHE_SIZE	(1UL << KMALLOC_SHIFT_HIGH)
/* Maximum order allocatable via the slab allocator */
#define KMALLOC_MAX_ORDER	(KMALLOC_SHIFT_MAX - PAGE_SHIFT)

/*
 * Kmalloc subsystem.
 */
#ifndef KMALLOC_MIN_SIZE
#define KMALLOC_MIN_SIZE (1 << KMALLOC_SHIFT_LOW)
#endif

/*
 * This restriction comes from byte sized index implementation.
 * Page size is normally 2^12 bytes and, in this case, if we want to use
 * byte sized index which can represent 2^8 entries, the size of the object
 * should be equal or greater to 2^12 / 2^8 = 2^4 = 16.
 * If minimum size of kmalloc is less than 16, we use it as minimum object
 * size and give up to use byte sized index.
 */
#define SLAB_OBJ_MIN_SIZE      (KMALLOC_MIN_SIZE < 16 ? \
                               (KMALLOC_MIN_SIZE) : 16)

/*
 * Whenever changing this, take care of that kmalloc_type() and
 * create_kmalloc_caches() still work as intended.
 *
 * KMALLOC_NORMAL can contain only unaccounted objects whereas KMALLOC_CGROUP
 * is for accounted but unreclaimable and non-dma objects. All the other
 * kmem caches can have both accounted and unaccounted objects.
 */
enum kmalloc_cache_type {
	KMALLOC_NORMAL = 0,
#ifndef CONFIG_ZONE_DMA
	KMALLOC_DMA = KMALLOC_NORMAL,
#endif
#ifndef CONFIG_MEMCG_KMEM
	KMALLOC_CGROUP = KMALLOC_NORMAL,
#endif
#ifdef CONFIG_SLUB_TINY
	KMALLOC_RECLAIM = KMALLOC_NORMAL,
#else
	KMALLOC_RECLAIM,
#endif
#ifdef CONFIG_ZONE_DMA
	KMALLOC_DMA,
#endif
#ifdef CONFIG_MEMCG_KMEM
	KMALLOC_CGROUP,
#endif
	NR_KMALLOC_TYPES
};

extern struct kmem_cache *
kmalloc_caches[NR_KMALLOC_TYPES][KMALLOC_SHIFT_HIGH + 1];

/*
 * Define gfp bits that should not be set for KMALLOC_NORMAL.
 */
#define KMALLOC_NOT_NORMAL_BITS					\
	(__GFP_RECLAIMABLE |					\
	(IS_ENABLED(CONFIG_ZONE_DMA)   ? __GFP_DMA : 0) |	\
	(IS_ENABLED(CONFIG_MEMCG_KMEM) ? __GFP_ACCOUNT : 0))

static __always_inline enum kmalloc_cache_type kmalloc_type(gfp_t flags)
{
	/*
	 * The most common case is KMALLOC_NORMAL, so test for it
	 * with a single branch for all the relevant flags.
	 */
	if (likely((flags & KMALLOC_NOT_NORMAL_BITS) == 0))
		return KMALLOC_NORMAL;

	/*
	 * At least one of the flags has to be set. Their priorities in
	 * decreasing order are:
	 *  1) __GFP_DMA
	 *  2) __GFP_RECLAIMABLE
	 *  3) __GFP_ACCOUNT
	 */
	if (IS_ENABLED(CONFIG_ZONE_DMA) && (flags & __GFP_DMA))
		return KMALLOC_DMA;
	if (!IS_ENABLED(CONFIG_MEMCG_KMEM) || (flags & __GFP_RECLAIMABLE))
		return KMALLOC_RECLAIM;
	else
		return KMALLOC_CGROUP;
}

/*
 * Figure out which kmalloc slab an allocation of a certain size
 * belongs to.
 * 0 = zero alloc
 * 1 =  65 .. 96 bytes
 * 2 = 129 .. 192 bytes
 * n = 2^(n-1)+1 .. 2^n
 *
 * Note: __kmalloc_index() is compile-time optimized, and not runtime optimized;
 * typical usage is via kmalloc_index() and therefore evaluated at compile-time.
 * Callers where !size_is_constant should only be test modules, where runtime
 * overheads of __kmalloc_index() can be tolerated.  Also see kmalloc_slab().
 */
static __always_inline unsigned int __kmalloc_index(size_t size,
						    bool size_is_constant)
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
	if (size <=   8 * 1024) return 13;
	if (size <=  16 * 1024) return 14;
	if (size <=  32 * 1024) return 15;
	if (size <=  64 * 1024) return 16;
	if (size <= 128 * 1024) return 17;
	if (size <= 256 * 1024) return 18;
	if (size <= 512 * 1024) return 19;
	if (size <= 1024 * 1024) return 20;
	if (size <=  2 * 1024 * 1024) return 21;

	if (!IS_ENABLED(CONFIG_PROFILE_ALL_BRANCHES) && size_is_constant)
		BUILD_BUG_ON_MSG(1, "unexpected size in kmalloc_index()");
	else
		BUG();

	/* Will never be reached. Needed because the compiler may complain */
	return -1;
}
static_assert(PAGE_SHIFT <= 20);
#define kmalloc_index(s) __kmalloc_index(s, true)

void *__kmalloc(size_t size, gfp_t flags) __assume_kmalloc_alignment __alloc_size(1);

/**
 * kmem_cache_alloc - Allocate an object
 * @cachep: The cache to allocate from.
 * @flags: See kmalloc().
 *
 * Allocate an object from this cache.
 * See kmem_cache_zalloc() for a shortcut of adding __GFP_ZERO to flags.
 *
 * Return: pointer to the new object or %NULL in case of error
 */
void *kmem_cache_alloc(struct kmem_cache *cachep, gfp_t flags) __assume_slab_alignment __malloc;
void *kmem_cache_alloc_lru(struct kmem_cache *s, struct list_lru *lru,
			   gfp_t gfpflags) __assume_slab_alignment __malloc;
void kmem_cache_free(struct kmem_cache *s, void *objp);

/*
 * Bulk allocation and freeing operations. These are accelerated in an
 * allocator specific way to avoid taking locks repeatedly or building
 * metadata structures unnecessarily.
 *
 * Note that interrupts must be enabled when calling these functions.
 */
void kmem_cache_free_bulk(struct kmem_cache *s, size_t size, void **p);
int kmem_cache_alloc_bulk(struct kmem_cache *s, gfp_t flags, size_t size, void **p);

static __always_inline void kfree_bulk(size_t size, void **p)
{
	kmem_cache_free_bulk(NULL, size, p);
}

void *__kmalloc_node(size_t size, gfp_t flags, int node) __assume_kmalloc_alignment
							 __alloc_size(1);
void *kmem_cache_alloc_node(struct kmem_cache *s, gfp_t flags, int node) __assume_slab_alignment
									 __malloc;

void *kmalloc_trace(struct kmem_cache *s, gfp_t flags, size_t size)
		    __assume_kmalloc_alignment __alloc_size(3);

void *kmalloc_node_trace(struct kmem_cache *s, gfp_t gfpflags,
			 int node, size_t size) __assume_kmalloc_alignment
						__alloc_size(4);
void *kmalloc_large(size_t size, gfp_t flags) __assume_page_alignment
					      __alloc_size(1);

void *kmalloc_large_node(size_t size, gfp_t flags, int node) __assume_page_alignment
							     __alloc_size(1);

/**
 * kmalloc - allocate kernel memory
 * @size: how many bytes of memory are required.
 * @flags: describe the allocation context
 *
 * kmalloc is the normal method of allocating memory
 * for objects smaller than page size in the kernel.
 *
 * The allocated object address is aligned to at least ARCH_KMALLOC_MINALIGN
 * bytes. For @size of power of two bytes, the alignment is also guaranteed
 * to be at least to the size.
 *
 * The @flags argument may be one of the GFP flags defined at
 * include/linux/gfp_types.h and described at
 * :ref:`Documentation/core-api/mm-api.rst <mm-api-gfp-flags>`
 *
 * The recommended usage of the @flags is described at
 * :ref:`Documentation/core-api/memory-allocation.rst <memory_allocation>`
 *
 * Below is a brief outline of the most useful GFP flags
 *
 * %GFP_KERNEL
 *	Allocate normal kernel ram. May sleep.
 *
 * %GFP_NOWAIT
 *	Allocation will not sleep.
 *
 * %GFP_ATOMIC
 *	Allocation will not sleep.  May use emergency pools.
 *
 * Also it is possible to set different flags by OR'ing
 * in one or more of the following additional @flags:
 *
 * %__GFP_ZERO
 *	Zero the allocated memory before returning. Also see kzalloc().
 *
 * %__GFP_HIGH
 *	This allocation has high priority and may use emergency pools.
 *
 * %__GFP_NOFAIL
 *	Indicate that this allocation is in no way allowed to fail
 *	(think twice before using).
 *
 * %__GFP_NORETRY
 *	If memory is not immediately available,
 *	then give up at once.
 *
 * %__GFP_NOWARN
 *	If allocation fails, don't issue any warnings.
 *
 * %__GFP_RETRY_MAYFAIL
 *	Try really hard to succeed the allocation but fail
 *	eventually.
 */
static __always_inline __alloc_size(1) void *kmalloc(size_t size, gfp_t flags)
{
	if (__builtin_constant_p(size) && size) {
		unsigned int index;

		if (size > KMALLOC_MAX_CACHE_SIZE)
			return kmalloc_large(size, flags);

		index = kmalloc_index(size);
		return kmalloc_trace(
				kmalloc_caches[kmalloc_type(flags)][index],
				flags, size);
	}
	return __kmalloc(size, flags);
}

static __always_inline __alloc_size(1) void *kmalloc_node(size_t size, gfp_t flags, int node)
{
	if (__builtin_constant_p(size) && size) {
		unsigned int index;

		if (size > KMALLOC_MAX_CACHE_SIZE)
			return kmalloc_large_node(size, flags, node);

		index = kmalloc_index(size);
		return kmalloc_node_trace(
				kmalloc_caches[kmalloc_type(flags)][index],
				flags, node, size);
	}
	return __kmalloc_node(size, flags, node);
}

/**
 * kmalloc_array - allocate memory for an array.
 * @n: number of elements.
 * @size: element size.
 * @flags: the type of memory to allocate (see kmalloc).
 */
static inline __alloc_size(1, 2) void *kmalloc_array(size_t n, size_t size, gfp_t flags)
{
	size_t bytes;

	if (unlikely(check_mul_overflow(n, size, &bytes)))
		return NULL;
	if (__builtin_constant_p(n) && __builtin_constant_p(size))
		return kmalloc(bytes, flags);
	return __kmalloc(bytes, flags);
}

/**
 * krealloc_array - reallocate memory for an array.
 * @p: pointer to the memory chunk to reallocate
 * @new_n: new number of elements to alloc
 * @new_size: new size of a single member of the array
 * @flags: the type of memory to allocate (see kmalloc)
 */
static inline __realloc_size(2, 3) void * __must_check krealloc_array(void *p,
								      size_t new_n,
								      size_t new_size,
								      gfp_t flags)
{
	size_t bytes;

	if (unlikely(check_mul_overflow(new_n, new_size, &bytes)))
		return NULL;

	return krealloc(p, bytes, flags);
}

/**
 * kcalloc - allocate memory for an array. The memory is set to zero.
 * @n: number of elements.
 * @size: element size.
 * @flags: the type of memory to allocate (see kmalloc).
 */
static inline __alloc_size(1, 2) void *kcalloc(size_t n, size_t size, gfp_t flags)
{
	return kmalloc_array(n, size, flags | __GFP_ZERO);
}

void *__kmalloc_node_track_caller(size_t size, gfp_t flags, int node,
				  unsigned long caller) __alloc_size(1);
#define kmalloc_node_track_caller(size, flags, node) \
	__kmalloc_node_track_caller(size, flags, node, \
				    _RET_IP_)

/*
 * kmalloc_track_caller is a special version of kmalloc that records the
 * calling function of the routine calling it for slab leak tracking instead
 * of just the calling function (confusing, eh?).
 * It's useful when the call to kmalloc comes from a widely-used standard
 * allocator where we care about the real place the memory allocation
 * request comes from.
 */
#define kmalloc_track_caller(size, flags) \
	__kmalloc_node_track_caller(size, flags, \
				    NUMA_NO_NODE, _RET_IP_)

static inline __alloc_size(1, 2) void *kmalloc_array_node(size_t n, size_t size, gfp_t flags,
							  int node)
{
	size_t bytes;

	if (unlikely(check_mul_overflow(n, size, &bytes)))
		return NULL;
	if (__builtin_constant_p(n) && __builtin_constant_p(size))
		return kmalloc_node(bytes, flags, node);
	return __kmalloc_node(bytes, flags, node);
}

static inline __alloc_size(1, 2) void *kcalloc_node(size_t n, size_t size, gfp_t flags, int node)
{
	return kmalloc_array_node(n, size, flags | __GFP_ZERO, node);
}

/*
 * Shortcuts
 */
static inline void *kmem_cache_zalloc(struct kmem_cache *k, gfp_t flags)
{
	return kmem_cache_alloc(k, flags | __GFP_ZERO);
}

/**
 * kzalloc - allocate memory. The memory is set to zero.
 * @size: how many bytes of memory are required.
 * @flags: the type of memory to allocate (see kmalloc).
 */
static inline __alloc_size(1) void *kzalloc(size_t size, gfp_t flags)
{
	return kmalloc(size, flags | __GFP_ZERO);
}

/**
 * kzalloc_node - allocate zeroed memory from a particular memory node.
 * @size: how many bytes of memory are required.
 * @flags: the type of memory to allocate (see kmalloc).
 * @node: memory node from which to allocate
 */
static inline __alloc_size(1) void *kzalloc_node(size_t size, gfp_t flags, int node)
{
	return kmalloc_node(size, flags | __GFP_ZERO, node);
}

extern void *kvmalloc_node(size_t size, gfp_t flags, int node) __alloc_size(1);
static inline __alloc_size(1) void *kvmalloc(size_t size, gfp_t flags)
{
	return kvmalloc_node(size, flags, NUMA_NO_NODE);
}
static inline __alloc_size(1) void *kvzalloc_node(size_t size, gfp_t flags, int node)
{
	return kvmalloc_node(size, flags | __GFP_ZERO, node);
}
static inline __alloc_size(1) void *kvzalloc(size_t size, gfp_t flags)
{
	return kvmalloc(size, flags | __GFP_ZERO);
}

static inline __alloc_size(1, 2) void *kvmalloc_array(size_t n, size_t size, gfp_t flags)
{
	size_t bytes;

	if (unlikely(check_mul_overflow(n, size, &bytes)))
		return NULL;

	return kvmalloc(bytes, flags);
}

static inline __alloc_size(1, 2) void *kvcalloc(size_t n, size_t size, gfp_t flags)
{
	return kvmalloc_array(n, size, flags | __GFP_ZERO);
}

extern void *kvrealloc(const void *p, size_t oldsize, size_t newsize, gfp_t flags)
		      __realloc_size(3);
extern void kvfree(const void *addr);
extern void kvfree_sensitive(const void *addr, size_t len);

unsigned int kmem_cache_size(struct kmem_cache *s);

/**
 * kmalloc_size_roundup - Report allocation bucket size for the given size
 *
 * @size: Number of bytes to round up from.
 *
 * This returns the number of bytes that would be available in a kmalloc()
 * allocation of @size bytes. For example, a 126 byte request would be
 * rounded up to the next sized kmalloc bucket, 128 bytes. (This is strictly
 * for the general-purpose kmalloc()-based allocations, and is not for the
 * pre-sized kmem_cache_alloc()-based allocations.)
 *
 * Use this to kmalloc() the full bucket size ahead of time instead of using
 * ksize() to query the size after an allocation.
 */
size_t kmalloc_size_roundup(size_t size);

void __init kmem_cache_init_late(void);

#if defined(CONFIG_SMP) && defined(CONFIG_SLAB)
int slab_prepare_cpu(unsigned int cpu);
int slab_dead_cpu(unsigned int cpu);
#else
#define slab_prepare_cpu	NULL
#define slab_dead_cpu		NULL
#endif

#endif	/* _LINUX_SLAB_H */
