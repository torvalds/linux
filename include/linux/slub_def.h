/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SLUB_DEF_H
#define _LINUX_SLUB_DEF_H

/*
 * SLUB : A Slab allocator without object queues.
 *
 * (C) 2007 SGI, Christoph Lameter
 */
#include <linux/kfence.h>
#include <linux/kobject.h>
#include <linux/reciprocal_div.h>
#include <linux/local_lock.h>

#ifdef CONFIG_SLUB_CPU_PARTIAL
#define slub_percpu_partial(c)		((c)->partial)

#define slub_set_percpu_partial(c, p)		\
({						\
	slub_percpu_partial(c) = (p)->next;	\
})

#define slub_percpu_partial_read_once(c)     READ_ONCE(slub_percpu_partial(c))
#else
#define slub_percpu_partial(c)			NULL

#define slub_set_percpu_partial(c, p)

#define slub_percpu_partial_read_once(c)	NULL
#endif // CONFIG_SLUB_CPU_PARTIAL

/*
 * Word size structure that can be atomically updated or read and that
 * contains both the order and the number of objects that a slab of the
 * given order would contain.
 */
struct kmem_cache_order_objects {
	unsigned int x;
};

/*
 * Slab cache management.
 */
struct kmem_cache {
#ifndef CONFIG_SLUB_TINY
	struct kmem_cache_cpu __percpu *cpu_slab;
#endif
	/* Used for retrieving partial slabs, etc. */
	slab_flags_t flags;
	unsigned long min_partial;
	unsigned int size;	/* The size of an object including metadata */
	unsigned int object_size;/* The size of an object without metadata */
	struct reciprocal_value reciprocal_size;
	unsigned int offset;	/* Free pointer offset */
#ifdef CONFIG_SLUB_CPU_PARTIAL
	/* Number of per cpu partial objects to keep around */
	unsigned int cpu_partial;
	/* Number of per cpu partial slabs to keep around */
	unsigned int cpu_partial_slabs;
#endif
	struct kmem_cache_order_objects oo;

	/* Allocation and freeing of slabs */
	struct kmem_cache_order_objects min;
	gfp_t allocflags;	/* gfp flags to use on each alloc */
	int refcount;		/* Refcount for slab cache destroy */
	void (*ctor)(void *);
	unsigned int inuse;		/* Offset to metadata */
	unsigned int align;		/* Alignment */
	unsigned int red_left_pad;	/* Left redzone padding size */
	const char *name;	/* Name (only for display!) */
	struct list_head list;	/* List of slab caches */
#ifdef CONFIG_SYSFS
	struct kobject kobj;	/* For sysfs */
#endif
#ifdef CONFIG_SLAB_FREELIST_HARDENED
	unsigned long random;
#endif

#ifdef CONFIG_NUMA
	/*
	 * Defragmentation by allocating from a remote node.
	 */
	unsigned int remote_node_defrag_ratio;
#endif

#ifdef CONFIG_SLAB_FREELIST_RANDOM
	unsigned int *random_seq;
#endif

#ifdef CONFIG_KASAN_GENERIC
	struct kasan_cache kasan_info;
#endif

#ifdef CONFIG_HARDENED_USERCOPY
	unsigned int useroffset;	/* Usercopy region offset */
	unsigned int usersize;		/* Usercopy region size */
#endif

	struct kmem_cache_node *node[MAX_NUMNODES];
};

#if defined(CONFIG_SYSFS) && !defined(CONFIG_SLUB_TINY)
#define SLAB_SUPPORTS_SYSFS
void sysfs_slab_unlink(struct kmem_cache *);
void sysfs_slab_release(struct kmem_cache *);
#else
static inline void sysfs_slab_unlink(struct kmem_cache *s)
{
}
static inline void sysfs_slab_release(struct kmem_cache *s)
{
}
#endif

void *fixup_red_left(struct kmem_cache *s, void *p);

static inline void *nearest_obj(struct kmem_cache *cache, const struct slab *slab,
				void *x) {
	void *object = x - (x - slab_address(slab)) % cache->size;
	void *last_object = slab_address(slab) +
		(slab->objects - 1) * cache->size;
	void *result = (unlikely(object > last_object)) ? last_object : object;

	result = fixup_red_left(cache, result);
	return result;
}

/* Determine object index from a given position */
static inline unsigned int __obj_to_index(const struct kmem_cache *cache,
					  void *addr, void *obj)
{
	return reciprocal_divide(kasan_reset_tag(obj) - addr,
				 cache->reciprocal_size);
}

static inline unsigned int obj_to_index(const struct kmem_cache *cache,
					const struct slab *slab, void *obj)
{
	if (is_kfence_address(obj))
		return 0;
	return __obj_to_index(cache, slab_address(slab), obj);
}

static inline int objs_per_slab(const struct kmem_cache *cache,
				     const struct slab *slab)
{
	return slab->objects;
}
#endif /* _LINUX_SLUB_DEF_H */
