#ifndef MM_SLAB_H
#define MM_SLAB_H
/*
 * Internal slab definitions
 */

#ifdef CONFIG_SLOB
/*
 * Common fields provided in kmem_cache by all slab allocators
 * This struct is either used directly by the allocator (SLOB)
 * or the allocator must include definitions for all fields
 * provided in kmem_cache_common in their definition of kmem_cache.
 *
 * Once we can do anonymous structs (C11 standard) we could put a
 * anonymous struct definition in these allocators so that the
 * separate allocations in the kmem_cache structure of SLAB and
 * SLUB is no longer needed.
 */
struct kmem_cache {
	unsigned int object_size;/* The original size of the object */
	unsigned int size;	/* The aligned/padded/added on size  */
	unsigned int align;	/* Alignment as calculated */
	unsigned long flags;	/* Active flags on the slab */
	const char *name;	/* Slab name for sysfs */
	int refcount;		/* Use counter */
	void (*ctor)(void *);	/* Called on object slot creation */
	struct list_head list;	/* List of all slab caches on the system */
};

#endif /* CONFIG_SLOB */

#ifdef CONFIG_SLAB
#include <linux/slab_def.h>
#endif

#ifdef CONFIG_SLUB
#include <linux/slub_def.h>
#endif

#include <linux/memcontrol.h>

/*
 * State of the slab allocator.
 *
 * This is used to describe the states of the allocator during bootup.
 * Allocators use this to gradually bootstrap themselves. Most allocators
 * have the problem that the structures used for managing slab caches are
 * allocated from slab caches themselves.
 */
enum slab_state {
	DOWN,			/* No slab functionality yet */
	PARTIAL,		/* SLUB: kmem_cache_node available */
	PARTIAL_NODE,		/* SLAB: kmalloc size for node struct available */
	UP,			/* Slab caches usable but not all extras yet */
	FULL			/* Everything is working */
};

extern enum slab_state slab_state;

/* The slab cache mutex protects the management structures during changes */
extern struct mutex slab_mutex;

/* The list of all slab caches on the system */
extern struct list_head slab_caches;

/* The slab cache that manages slab cache information */
extern struct kmem_cache *kmem_cache;

unsigned long calculate_alignment(unsigned long flags,
		unsigned long align, unsigned long size);

#ifndef CONFIG_SLOB
/* Kmalloc array related functions */
void create_kmalloc_caches(unsigned long);

/* Find the kmalloc slab corresponding for a certain size */
struct kmem_cache *kmalloc_slab(size_t, gfp_t);
#endif


/* Functions provided by the slab allocators */
extern int __kmem_cache_create(struct kmem_cache *, unsigned long flags);

extern struct kmem_cache *create_kmalloc_cache(const char *name, size_t size,
			unsigned long flags);
extern void create_boot_cache(struct kmem_cache *, const char *name,
			size_t size, unsigned long flags);

struct mem_cgroup;

int slab_unmergeable(struct kmem_cache *s);
struct kmem_cache *find_mergeable(size_t size, size_t align,
		unsigned long flags, const char *name, void (*ctor)(void *));
#ifndef CONFIG_SLOB
struct kmem_cache *
__kmem_cache_alias(const char *name, size_t size, size_t align,
		   unsigned long flags, void (*ctor)(void *));

unsigned long kmem_cache_flags(unsigned long object_size,
	unsigned long flags, const char *name,
	void (*ctor)(void *));
#else
static inline struct kmem_cache *
__kmem_cache_alias(const char *name, size_t size, size_t align,
		   unsigned long flags, void (*ctor)(void *))
{ return NULL; }

static inline unsigned long kmem_cache_flags(unsigned long object_size,
	unsigned long flags, const char *name,
	void (*ctor)(void *))
{
	return flags;
}
#endif


/* Legal flag mask for kmem_cache_create(), for various configurations */
#define SLAB_CORE_FLAGS (SLAB_HWCACHE_ALIGN | SLAB_CACHE_DMA | SLAB_PANIC | \
			 SLAB_DESTROY_BY_RCU | SLAB_DEBUG_OBJECTS )

#if defined(CONFIG_DEBUG_SLAB)
#define SLAB_DEBUG_FLAGS (SLAB_RED_ZONE | SLAB_POISON | SLAB_STORE_USER)
#elif defined(CONFIG_SLUB_DEBUG)
#define SLAB_DEBUG_FLAGS (SLAB_RED_ZONE | SLAB_POISON | SLAB_STORE_USER | \
			  SLAB_TRACE | SLAB_DEBUG_FREE)
#else
#define SLAB_DEBUG_FLAGS (0)
#endif

#if defined(CONFIG_SLAB)
#define SLAB_CACHE_FLAGS (SLAB_MEM_SPREAD | SLAB_NOLEAKTRACE | \
			  SLAB_RECLAIM_ACCOUNT | SLAB_TEMPORARY | SLAB_NOTRACK)
#elif defined(CONFIG_SLUB)
#define SLAB_CACHE_FLAGS (SLAB_NOLEAKTRACE | SLAB_RECLAIM_ACCOUNT | \
			  SLAB_TEMPORARY | SLAB_NOTRACK)
#else
#define SLAB_CACHE_FLAGS (0)
#endif

#define CACHE_CREATE_MASK (SLAB_CORE_FLAGS | SLAB_DEBUG_FLAGS | SLAB_CACHE_FLAGS)

int __kmem_cache_shutdown(struct kmem_cache *);
int __kmem_cache_shrink(struct kmem_cache *);
void slab_kmem_cache_release(struct kmem_cache *);

struct seq_file;
struct file;

struct slabinfo {
	unsigned long active_objs;
	unsigned long num_objs;
	unsigned long active_slabs;
	unsigned long num_slabs;
	unsigned long shared_avail;
	unsigned int limit;
	unsigned int batchcount;
	unsigned int shared;
	unsigned int objects_per_slab;
	unsigned int cache_order;
};

void get_slabinfo(struct kmem_cache *s, struct slabinfo *sinfo);
void slabinfo_show_stats(struct seq_file *m, struct kmem_cache *s);
ssize_t slabinfo_write(struct file *file, const char __user *buffer,
		       size_t count, loff_t *ppos);

#ifdef CONFIG_MEMCG_KMEM
static inline bool is_root_cache(struct kmem_cache *s)
{
	return !s->memcg_params || s->memcg_params->is_root_cache;
}

static inline bool slab_equal_or_root(struct kmem_cache *s,
					struct kmem_cache *p)
{
	return (p == s) ||
		(s->memcg_params && (p == s->memcg_params->root_cache));
}

/*
 * We use suffixes to the name in memcg because we can't have caches
 * created in the system with the same name. But when we print them
 * locally, better refer to them with the base name
 */
static inline const char *cache_name(struct kmem_cache *s)
{
	if (!is_root_cache(s))
		return s->memcg_params->root_cache->name;
	return s->name;
}

/*
 * Note, we protect with RCU only the memcg_caches array, not per-memcg caches.
 * That said the caller must assure the memcg's cache won't go away. Since once
 * created a memcg's cache is destroyed only along with the root cache, it is
 * true if we are going to allocate from the cache or hold a reference to the
 * root cache by other means. Otherwise, we should hold either the slab_mutex
 * or the memcg's slab_caches_mutex while calling this function and accessing
 * the returned value.
 */
static inline struct kmem_cache *
cache_from_memcg_idx(struct kmem_cache *s, int idx)
{
	struct kmem_cache *cachep;
	struct memcg_cache_params *params;

	if (!s->memcg_params)
		return NULL;

	rcu_read_lock();
	params = rcu_dereference(s->memcg_params);
	cachep = params->memcg_caches[idx];
	rcu_read_unlock();

	/*
	 * Make sure we will access the up-to-date value. The code updating
	 * memcg_caches issues a write barrier to match this (see
	 * memcg_register_cache()).
	 */
	smp_read_barrier_depends();
	return cachep;
}

static inline struct kmem_cache *memcg_root_cache(struct kmem_cache *s)
{
	if (is_root_cache(s))
		return s;
	return s->memcg_params->root_cache;
}

static __always_inline int memcg_charge_slab(struct kmem_cache *s,
					     gfp_t gfp, int order)
{
	if (!memcg_kmem_enabled())
		return 0;
	if (is_root_cache(s))
		return 0;
	return __memcg_charge_slab(s, gfp, order);
}

static __always_inline void memcg_uncharge_slab(struct kmem_cache *s, int order)
{
	if (!memcg_kmem_enabled())
		return;
	if (is_root_cache(s))
		return;
	__memcg_uncharge_slab(s, order);
}
#else
static inline bool is_root_cache(struct kmem_cache *s)
{
	return true;
}

static inline bool slab_equal_or_root(struct kmem_cache *s,
				      struct kmem_cache *p)
{
	return true;
}

static inline const char *cache_name(struct kmem_cache *s)
{
	return s->name;
}

static inline struct kmem_cache *
cache_from_memcg_idx(struct kmem_cache *s, int idx)
{
	return NULL;
}

static inline struct kmem_cache *memcg_root_cache(struct kmem_cache *s)
{
	return s;
}

static inline int memcg_charge_slab(struct kmem_cache *s, gfp_t gfp, int order)
{
	return 0;
}

static inline void memcg_uncharge_slab(struct kmem_cache *s, int order)
{
}
#endif

static inline struct kmem_cache *cache_from_obj(struct kmem_cache *s, void *x)
{
	struct kmem_cache *cachep;
	struct page *page;

	/*
	 * When kmemcg is not being used, both assignments should return the
	 * same value. but we don't want to pay the assignment price in that
	 * case. If it is not compiled in, the compiler should be smart enough
	 * to not do even the assignment. In that case, slab_equal_or_root
	 * will also be a constant.
	 */
	if (!memcg_kmem_enabled() && !unlikely(s->flags & SLAB_DEBUG_FREE))
		return s;

	page = virt_to_head_page(x);
	cachep = page->slab_cache;
	if (slab_equal_or_root(cachep, s))
		return cachep;

	pr_err("%s: Wrong slab cache. %s but object is from %s\n",
	       __func__, cachep->name, s->name);
	WARN_ON_ONCE(1);
	return s;
}

#ifndef CONFIG_SLOB
/*
 * The slab lists for all objects.
 */
struct kmem_cache_node {
	spinlock_t list_lock;

#ifdef CONFIG_SLAB
	struct list_head slabs_partial;	/* partial list first, better asm code */
	struct list_head slabs_full;
	struct list_head slabs_free;
	unsigned long free_objects;
	unsigned int free_limit;
	unsigned int colour_next;	/* Per-node cache coloring */
	struct array_cache *shared;	/* shared per node */
	struct alien_cache **alien;	/* on other nodes */
	unsigned long next_reap;	/* updated without locking */
	int free_touched;		/* updated without locking */
#endif

#ifdef CONFIG_SLUB
	unsigned long nr_partial;
	struct list_head partial;
#ifdef CONFIG_SLUB_DEBUG
	atomic_long_t nr_slabs;
	atomic_long_t total_objects;
	struct list_head full;
#endif
#endif

};

static inline struct kmem_cache_node *get_node(struct kmem_cache *s, int node)
{
	return s->node[node];
}

/*
 * Iterator over all nodes. The body will be executed for each node that has
 * a kmem_cache_node structure allocated (which is true for all online nodes)
 */
#define for_each_kmem_cache_node(__s, __node, __n) \
	for (__node = 0; __node < nr_node_ids; __node++) \
		 if ((__n = get_node(__s, __node)))

#endif

void *slab_next(struct seq_file *m, void *p, loff_t *pos);
void slab_stop(struct seq_file *m, void *p);

#endif /* MM_SLAB_H */
