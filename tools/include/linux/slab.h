/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _TOOLS_SLAB_H
#define _TOOLS_SLAB_H

#include <linux/types.h>
#include <linux/gfp.h>
#include <pthread.h>

#define SLAB_RECLAIM_ACCOUNT    0x00020000UL            /* Objects are reclaimable */

#define kzalloc_node(size, flags, node) kmalloc(size, flags)
enum _slab_flag_bits {
	_SLAB_KMALLOC,
	_SLAB_HWCACHE_ALIGN,
	_SLAB_PANIC,
	_SLAB_TYPESAFE_BY_RCU,
	_SLAB_ACCOUNT,
	_SLAB_FLAGS_LAST_BIT
};

#define __SLAB_FLAG_BIT(nr)	((unsigned int __force)(1U << (nr)))
#define __SLAB_FLAG_UNUSED	((unsigned int __force)(0U))

#define SLAB_HWCACHE_ALIGN	__SLAB_FLAG_BIT(_SLAB_HWCACHE_ALIGN)
#define SLAB_PANIC		__SLAB_FLAG_BIT(_SLAB_PANIC)
#define SLAB_TYPESAFE_BY_RCU	__SLAB_FLAG_BIT(_SLAB_TYPESAFE_BY_RCU)
#ifdef CONFIG_MEMCG
# define SLAB_ACCOUNT		__SLAB_FLAG_BIT(_SLAB_ACCOUNT)
#else
# define SLAB_ACCOUNT		__SLAB_FLAG_UNUSED
#endif

void *kmalloc(size_t size, gfp_t gfp);
void kfree(void *p);
void *kmalloc_array(size_t n, size_t size, gfp_t gfp);

bool slab_is_available(void);

enum slab_state {
	DOWN,
	PARTIAL,
	UP,
	FULL
};

struct kmem_cache {
	pthread_mutex_t lock;
	unsigned int size;
	unsigned int align;
	unsigned int sheaf_capacity;
	int nr_objs;
	void *objs;
	void (*ctor)(void *);
	bool non_kernel_enabled;
	unsigned int non_kernel;
	unsigned long nr_allocated;
	unsigned long nr_tallocated;
	bool exec_callback;
	void (*callback)(void *);
	void *private;
};

struct kmem_cache_args {
	/**
	 * @align: The required alignment for the objects.
	 *
	 * %0 means no specific alignment is requested.
	 */
	unsigned int align;
	/**
	 * @sheaf_capacity: The maximum size of the sheaf.
	 */
	unsigned int sheaf_capacity;
	/**
	 * @useroffset: Usercopy region offset.
	 *
	 * %0 is a valid offset, when @usersize is non-%0
	 */
	unsigned int useroffset;
	/**
	 * @usersize: Usercopy region size.
	 *
	 * %0 means no usercopy region is specified.
	 */
	unsigned int usersize;
	/**
	 * @freeptr_offset: Custom offset for the free pointer
	 * in &SLAB_TYPESAFE_BY_RCU caches
	 *
	 * By default &SLAB_TYPESAFE_BY_RCU caches place the free pointer
	 * outside of the object. This might cause the object to grow in size.
	 * Cache creators that have a reason to avoid this can specify a custom
	 * free pointer offset in their struct where the free pointer will be
	 * placed.
	 *
	 * Note that placing the free pointer inside the object requires the
	 * caller to ensure that no fields are invalidated that are required to
	 * guard against object recycling (See &SLAB_TYPESAFE_BY_RCU for
	 * details).
	 *
	 * Using %0 as a value for @freeptr_offset is valid. If @freeptr_offset
	 * is specified, %use_freeptr_offset must be set %true.
	 *
	 * Note that @ctor currently isn't supported with custom free pointers
	 * as a @ctor requires an external free pointer.
	 */
	unsigned int freeptr_offset;
	/**
	 * @use_freeptr_offset: Whether a @freeptr_offset is used.
	 */
	bool use_freeptr_offset;
	/**
	 * @ctor: A constructor for the objects.
	 *
	 * The constructor is invoked for each object in a newly allocated slab
	 * page. It is the cache user's responsibility to free object in the
	 * same state as after calling the constructor, or deal appropriately
	 * with any differences between a freshly constructed and a reallocated
	 * object.
	 *
	 * %NULL means no constructor.
	 */
	void (*ctor)(void *);
};

struct slab_sheaf {
	union {
		struct list_head barn_list;
		/* only used for prefilled sheafs */
		unsigned int capacity;
	};
	struct kmem_cache *cache;
	unsigned int size;
	int node; /* only used for rcu_sheaf */
	void *objects[];
};

static inline void *kzalloc(size_t size, gfp_t gfp)
{
	return kmalloc(size, gfp | __GFP_ZERO);
}

struct list_lru;

void *kmem_cache_alloc_lru(struct kmem_cache *cachep, struct list_lru *, int flags);
static inline void *kmem_cache_alloc(struct kmem_cache *cachep, int flags)
{
	return kmem_cache_alloc_lru(cachep, NULL, flags);
}
void kmem_cache_free(struct kmem_cache *cachep, void *objp);


struct kmem_cache *
__kmem_cache_create_args(const char *name, unsigned int size,
		struct kmem_cache_args *args, unsigned int flags);

/* If NULL is passed for @args, use this variant with default arguments. */
static inline struct kmem_cache *
__kmem_cache_default_args(const char *name, unsigned int size,
		struct kmem_cache_args *args, unsigned int flags)
{
	struct kmem_cache_args kmem_default_args = {};

	return __kmem_cache_create_args(name, size, &kmem_default_args, flags);
}

static inline struct kmem_cache *
__kmem_cache_create(const char *name, unsigned int size, unsigned int align,
		unsigned int flags, void (*ctor)(void *))
{
	struct kmem_cache_args kmem_args = {
		.align	= align,
		.ctor	= ctor,
	};

	return __kmem_cache_create_args(name, size, &kmem_args, flags);
}

#define kmem_cache_create(__name, __object_size, __args, ...)           \
	_Generic((__args),                                              \
		struct kmem_cache_args *: __kmem_cache_create_args,	\
		void *: __kmem_cache_default_args,			\
		default: __kmem_cache_create)(__name, __object_size, __args, __VA_ARGS__)

void kmem_cache_free_bulk(struct kmem_cache *cachep, size_t size, void **list);
int kmem_cache_alloc_bulk(struct kmem_cache *cachep, gfp_t gfp, size_t size,
			  void **list);
struct slab_sheaf *
kmem_cache_prefill_sheaf(struct kmem_cache *s, gfp_t gfp, unsigned int size);

void *
kmem_cache_alloc_from_sheaf(struct kmem_cache *s, gfp_t gfp,
		struct slab_sheaf *sheaf);

void kmem_cache_return_sheaf(struct kmem_cache *s, gfp_t gfp,
		struct slab_sheaf *sheaf);
int kmem_cache_refill_sheaf(struct kmem_cache *s, gfp_t gfp,
		struct slab_sheaf **sheafp, unsigned int size);

static inline unsigned int kmem_cache_sheaf_size(struct slab_sheaf *sheaf)
{
	return sheaf->size;
}

#endif		/* _TOOLS_SLAB_H */
