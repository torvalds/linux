/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MM_SLAB_H
#define MM_SLAB_H
/*
 * Internal slab definitions
 */

/* Reuses the bits in struct page */
struct slab {
	unsigned long __page_flags;

#if defined(CONFIG_SLAB)

	union {
		struct list_head slab_list;
		struct rcu_head rcu_head;
	};
	struct kmem_cache *slab_cache;
	void *freelist;	/* array of free object indexes */
	void *s_mem;	/* first object */
	unsigned int active;

#elif defined(CONFIG_SLUB)

	union {
		struct list_head slab_list;
		struct rcu_head rcu_head;
#ifdef CONFIG_SLUB_CPU_PARTIAL
		struct {
			struct slab *next;
			int slabs;	/* Nr of slabs left */
		};
#endif
	};
	struct kmem_cache *slab_cache;
	/* Double-word boundary */
	void *freelist;		/* first free object */
	union {
		unsigned long counters;
		struct {
			unsigned inuse:16;
			unsigned objects:15;
			unsigned frozen:1;
		};
	};
	unsigned int __unused;

#elif defined(CONFIG_SLOB)

	struct list_head slab_list;
	void *__unused_1;
	void *freelist;		/* first free block */
	long units;
	unsigned int __unused_2;

#else
#error "Unexpected slab allocator configured"
#endif

	atomic_t __page_refcount;
#ifdef CONFIG_MEMCG
	unsigned long memcg_data;
#endif
};

#define SLAB_MATCH(pg, sl)						\
	static_assert(offsetof(struct page, pg) == offsetof(struct slab, sl))
SLAB_MATCH(flags, __page_flags);
SLAB_MATCH(compound_head, slab_list);	/* Ensure bit 0 is clear */
#ifndef CONFIG_SLOB
SLAB_MATCH(rcu_head, rcu_head);
#endif
SLAB_MATCH(_refcount, __page_refcount);
#ifdef CONFIG_MEMCG
SLAB_MATCH(memcg_data, memcg_data);
#endif
#undef SLAB_MATCH
static_assert(sizeof(struct slab) <= sizeof(struct page));

/**
 * folio_slab - Converts from folio to slab.
 * @folio: The folio.
 *
 * Currently struct slab is a different representation of a folio where
 * folio_test_slab() is true.
 *
 * Return: The slab which contains this folio.
 */
#define folio_slab(folio)	(_Generic((folio),			\
	const struct folio *:	(const struct slab *)(folio),		\
	struct folio *:		(struct slab *)(folio)))

/**
 * slab_folio - The folio allocated for a slab
 * @slab: The slab.
 *
 * Slabs are allocated as folios that contain the individual objects and are
 * using some fields in the first struct page of the folio - those fields are
 * now accessed by struct slab. It is occasionally necessary to convert back to
 * a folio in order to communicate with the rest of the mm.  Please use this
 * helper function instead of casting yourself, as the implementation may change
 * in the future.
 */
#define slab_folio(s)		(_Generic((s),				\
	const struct slab *:	(const struct folio *)s,		\
	struct slab *:		(struct folio *)s))

/**
 * page_slab - Converts from first struct page to slab.
 * @p: The first (either head of compound or single) page of slab.
 *
 * A temporary wrapper to convert struct page to struct slab in situations where
 * we know the page is the compound head, or single order-0 page.
 *
 * Long-term ideally everything would work with struct slab directly or go
 * through folio to struct slab.
 *
 * Return: The slab which contains this page
 */
#define page_slab(p)		(_Generic((p),				\
	const struct page *:	(const struct slab *)(p),		\
	struct page *:		(struct slab *)(p)))

/**
 * slab_page - The first struct page allocated for a slab
 * @slab: The slab.
 *
 * A convenience wrapper for converting slab to the first struct page of the
 * underlying folio, to communicate with code not yet converted to folio or
 * struct slab.
 */
#define slab_page(s) folio_page(slab_folio(s), 0)

/*
 * If network-based swap is enabled, sl*b must keep track of whether pages
 * were allocated from pfmemalloc reserves.
 */
static inline bool slab_test_pfmemalloc(const struct slab *slab)
{
	return folio_test_active((struct folio *)slab_folio(slab));
}

static inline void slab_set_pfmemalloc(struct slab *slab)
{
	folio_set_active(slab_folio(slab));
}

static inline void slab_clear_pfmemalloc(struct slab *slab)
{
	folio_clear_active(slab_folio(slab));
}

static inline void __slab_clear_pfmemalloc(struct slab *slab)
{
	__folio_clear_active(slab_folio(slab));
}

static inline void *slab_address(const struct slab *slab)
{
	return folio_address(slab_folio(slab));
}

static inline int slab_nid(const struct slab *slab)
{
	return folio_nid(slab_folio(slab));
}

static inline pg_data_t *slab_pgdat(const struct slab *slab)
{
	return folio_pgdat(slab_folio(slab));
}

static inline struct slab *virt_to_slab(const void *addr)
{
	struct folio *folio = virt_to_folio(addr);

	if (!folio_test_slab(folio))
		return NULL;

	return folio_slab(folio);
}

static inline int slab_order(const struct slab *slab)
{
	return folio_order((struct folio *)slab_folio(slab));
}

static inline size_t slab_size(const struct slab *slab)
{
	return PAGE_SIZE << slab_order(slab);
}

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
	slab_flags_t flags;	/* Active flags on the slab */
	unsigned int useroffset;/* Usercopy region offset */
	unsigned int usersize;	/* Usercopy region size */
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
#include <linux/fault-inject.h>
#include <linux/kasan.h>
#include <linux/kmemleak.h>
#include <linux/random.h>
#include <linux/sched/mm.h>
#include <linux/list_lru.h>

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

/* A table of kmalloc cache names and sizes */
extern const struct kmalloc_info_struct {
	const char *name[NR_KMALLOC_TYPES];
	unsigned int size;
} kmalloc_info[];

#ifndef CONFIG_SLOB
/* Kmalloc array related functions */
void setup_kmalloc_cache_index_table(void);
void create_kmalloc_caches(slab_flags_t);

/* Find the kmalloc slab corresponding for a certain size */
struct kmem_cache *kmalloc_slab(size_t, gfp_t);
#endif

gfp_t kmalloc_fix_flags(gfp_t flags);

/* Functions provided by the slab allocators */
int __kmem_cache_create(struct kmem_cache *, slab_flags_t flags);

struct kmem_cache *create_kmalloc_cache(const char *name, unsigned int size,
			slab_flags_t flags, unsigned int useroffset,
			unsigned int usersize);
extern void create_boot_cache(struct kmem_cache *, const char *name,
			unsigned int size, slab_flags_t flags,
			unsigned int useroffset, unsigned int usersize);

int slab_unmergeable(struct kmem_cache *s);
struct kmem_cache *find_mergeable(unsigned size, unsigned align,
		slab_flags_t flags, const char *name, void (*ctor)(void *));
#ifndef CONFIG_SLOB
struct kmem_cache *
__kmem_cache_alias(const char *name, unsigned int size, unsigned int align,
		   slab_flags_t flags, void (*ctor)(void *));

slab_flags_t kmem_cache_flags(unsigned int object_size,
	slab_flags_t flags, const char *name);
#else
static inline struct kmem_cache *
__kmem_cache_alias(const char *name, unsigned int size, unsigned int align,
		   slab_flags_t flags, void (*ctor)(void *))
{ return NULL; }

static inline slab_flags_t kmem_cache_flags(unsigned int object_size,
	slab_flags_t flags, const char *name)
{
	return flags;
}
#endif


/* Legal flag mask for kmem_cache_create(), for various configurations */
#define SLAB_CORE_FLAGS (SLAB_HWCACHE_ALIGN | SLAB_CACHE_DMA | \
			 SLAB_CACHE_DMA32 | SLAB_PANIC | \
			 SLAB_TYPESAFE_BY_RCU | SLAB_DEBUG_OBJECTS )

#if defined(CONFIG_DEBUG_SLAB)
#define SLAB_DEBUG_FLAGS (SLAB_RED_ZONE | SLAB_POISON | SLAB_STORE_USER)
#elif defined(CONFIG_SLUB_DEBUG)
#define SLAB_DEBUG_FLAGS (SLAB_RED_ZONE | SLAB_POISON | SLAB_STORE_USER | \
			  SLAB_TRACE | SLAB_CONSISTENCY_CHECKS)
#else
#define SLAB_DEBUG_FLAGS (0)
#endif

#if defined(CONFIG_SLAB)
#define SLAB_CACHE_FLAGS (SLAB_MEM_SPREAD | SLAB_NOLEAKTRACE | \
			  SLAB_RECLAIM_ACCOUNT | SLAB_TEMPORARY | \
			  SLAB_ACCOUNT)
#elif defined(CONFIG_SLUB)
#define SLAB_CACHE_FLAGS (SLAB_NOLEAKTRACE | SLAB_RECLAIM_ACCOUNT | \
			  SLAB_TEMPORARY | SLAB_ACCOUNT | SLAB_NO_USER_FLAGS)
#else
#define SLAB_CACHE_FLAGS (SLAB_NOLEAKTRACE)
#endif

/* Common flags available with current configuration */
#define CACHE_CREATE_MASK (SLAB_CORE_FLAGS | SLAB_DEBUG_FLAGS | SLAB_CACHE_FLAGS)

/* Common flags permitted for kmem_cache_create */
#define SLAB_FLAGS_PERMITTED (SLAB_CORE_FLAGS | \
			      SLAB_RED_ZONE | \
			      SLAB_POISON | \
			      SLAB_STORE_USER | \
			      SLAB_TRACE | \
			      SLAB_CONSISTENCY_CHECKS | \
			      SLAB_MEM_SPREAD | \
			      SLAB_NOLEAKTRACE | \
			      SLAB_RECLAIM_ACCOUNT | \
			      SLAB_TEMPORARY | \
			      SLAB_ACCOUNT | \
			      SLAB_NO_USER_FLAGS)

bool __kmem_cache_empty(struct kmem_cache *);
int __kmem_cache_shutdown(struct kmem_cache *);
void __kmem_cache_release(struct kmem_cache *);
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

static inline enum node_stat_item cache_vmstat_idx(struct kmem_cache *s)
{
	return (s->flags & SLAB_RECLAIM_ACCOUNT) ?
		NR_SLAB_RECLAIMABLE_B : NR_SLAB_UNRECLAIMABLE_B;
}

#ifdef CONFIG_SLUB_DEBUG
#ifdef CONFIG_SLUB_DEBUG_ON
DECLARE_STATIC_KEY_TRUE(slub_debug_enabled);
#else
DECLARE_STATIC_KEY_FALSE(slub_debug_enabled);
#endif
extern void print_tracking(struct kmem_cache *s, void *object);
long validate_slab_cache(struct kmem_cache *s);
static inline bool __slub_debug_enabled(void)
{
	return static_branch_unlikely(&slub_debug_enabled);
}
#else
static inline void print_tracking(struct kmem_cache *s, void *object)
{
}
static inline bool __slub_debug_enabled(void)
{
	return false;
}
#endif

/*
 * Returns true if any of the specified slub_debug flags is enabled for the
 * cache. Use only for flags parsed by setup_slub_debug() as it also enables
 * the static key.
 */
static inline bool kmem_cache_debug_flags(struct kmem_cache *s, slab_flags_t flags)
{
	if (IS_ENABLED(CONFIG_SLUB_DEBUG))
		VM_WARN_ON_ONCE(!(flags & SLAB_DEBUG_FLAGS));
	if (__slub_debug_enabled())
		return s->flags & flags;
	return false;
}

#ifdef CONFIG_MEMCG_KMEM
/*
 * slab_objcgs - get the object cgroups vector associated with a slab
 * @slab: a pointer to the slab struct
 *
 * Returns a pointer to the object cgroups vector associated with the slab,
 * or NULL if no such vector has been associated yet.
 */
static inline struct obj_cgroup **slab_objcgs(struct slab *slab)
{
	unsigned long memcg_data = READ_ONCE(slab->memcg_data);

	VM_BUG_ON_PAGE(memcg_data && !(memcg_data & MEMCG_DATA_OBJCGS),
							slab_page(slab));
	VM_BUG_ON_PAGE(memcg_data & MEMCG_DATA_KMEM, slab_page(slab));

	return (struct obj_cgroup **)(memcg_data & ~MEMCG_DATA_FLAGS_MASK);
}

int memcg_alloc_slab_cgroups(struct slab *slab, struct kmem_cache *s,
				 gfp_t gfp, bool new_slab);
void mod_objcg_state(struct obj_cgroup *objcg, struct pglist_data *pgdat,
		     enum node_stat_item idx, int nr);

static inline void memcg_free_slab_cgroups(struct slab *slab)
{
	kfree(slab_objcgs(slab));
	slab->memcg_data = 0;
}

static inline size_t obj_full_size(struct kmem_cache *s)
{
	/*
	 * For each accounted object there is an extra space which is used
	 * to store obj_cgroup membership. Charge it too.
	 */
	return s->size + sizeof(struct obj_cgroup *);
}

/*
 * Returns false if the allocation should fail.
 */
static inline bool memcg_slab_pre_alloc_hook(struct kmem_cache *s,
					     struct list_lru *lru,
					     struct obj_cgroup **objcgp,
					     size_t objects, gfp_t flags)
{
	struct obj_cgroup *objcg;

	if (!memcg_kmem_enabled())
		return true;

	if (!(flags & __GFP_ACCOUNT) && !(s->flags & SLAB_ACCOUNT))
		return true;

	objcg = get_obj_cgroup_from_current();
	if (!objcg)
		return true;

	if (lru) {
		int ret;
		struct mem_cgroup *memcg;

		memcg = get_mem_cgroup_from_objcg(objcg);
		ret = memcg_list_lru_alloc(memcg, lru, flags);
		css_put(&memcg->css);

		if (ret)
			goto out;
	}

	if (obj_cgroup_charge(objcg, flags, objects * obj_full_size(s)))
		goto out;

	*objcgp = objcg;
	return true;
out:
	obj_cgroup_put(objcg);
	return false;
}

static inline void memcg_slab_post_alloc_hook(struct kmem_cache *s,
					      struct obj_cgroup *objcg,
					      gfp_t flags, size_t size,
					      void **p)
{
	struct slab *slab;
	unsigned long off;
	size_t i;

	if (!memcg_kmem_enabled() || !objcg)
		return;

	for (i = 0; i < size; i++) {
		if (likely(p[i])) {
			slab = virt_to_slab(p[i]);

			if (!slab_objcgs(slab) &&
			    memcg_alloc_slab_cgroups(slab, s, flags,
							 false)) {
				obj_cgroup_uncharge(objcg, obj_full_size(s));
				continue;
			}

			off = obj_to_index(s, slab, p[i]);
			obj_cgroup_get(objcg);
			slab_objcgs(slab)[off] = objcg;
			mod_objcg_state(objcg, slab_pgdat(slab),
					cache_vmstat_idx(s), obj_full_size(s));
		} else {
			obj_cgroup_uncharge(objcg, obj_full_size(s));
		}
	}
	obj_cgroup_put(objcg);
}

static inline void memcg_slab_free_hook(struct kmem_cache *s, struct slab *slab,
					void **p, int objects)
{
	struct obj_cgroup **objcgs;
	int i;

	if (!memcg_kmem_enabled())
		return;

	objcgs = slab_objcgs(slab);
	if (!objcgs)
		return;

	for (i = 0; i < objects; i++) {
		struct obj_cgroup *objcg;
		unsigned int off;

		off = obj_to_index(s, slab, p[i]);
		objcg = objcgs[off];
		if (!objcg)
			continue;

		objcgs[off] = NULL;
		obj_cgroup_uncharge(objcg, obj_full_size(s));
		mod_objcg_state(objcg, slab_pgdat(slab), cache_vmstat_idx(s),
				-obj_full_size(s));
		obj_cgroup_put(objcg);
	}
}

#else /* CONFIG_MEMCG_KMEM */
static inline struct obj_cgroup **slab_objcgs(struct slab *slab)
{
	return NULL;
}

static inline struct mem_cgroup *memcg_from_slab_obj(void *ptr)
{
	return NULL;
}

static inline int memcg_alloc_slab_cgroups(struct slab *slab,
					       struct kmem_cache *s, gfp_t gfp,
					       bool new_slab)
{
	return 0;
}

static inline void memcg_free_slab_cgroups(struct slab *slab)
{
}

static inline bool memcg_slab_pre_alloc_hook(struct kmem_cache *s,
					     struct list_lru *lru,
					     struct obj_cgroup **objcgp,
					     size_t objects, gfp_t flags)
{
	return true;
}

static inline void memcg_slab_post_alloc_hook(struct kmem_cache *s,
					      struct obj_cgroup *objcg,
					      gfp_t flags, size_t size,
					      void **p)
{
}

static inline void memcg_slab_free_hook(struct kmem_cache *s, struct slab *slab,
					void **p, int objects)
{
}
#endif /* CONFIG_MEMCG_KMEM */

#ifndef CONFIG_SLOB
static inline struct kmem_cache *virt_to_cache(const void *obj)
{
	struct slab *slab;

	slab = virt_to_slab(obj);
	if (WARN_ONCE(!slab, "%s: Object is not a Slab page!\n",
					__func__))
		return NULL;
	return slab->slab_cache;
}

static __always_inline void account_slab(struct slab *slab, int order,
					 struct kmem_cache *s, gfp_t gfp)
{
	if (memcg_kmem_enabled() && (s->flags & SLAB_ACCOUNT))
		memcg_alloc_slab_cgroups(slab, s, gfp, true);

	mod_node_page_state(slab_pgdat(slab), cache_vmstat_idx(s),
			    PAGE_SIZE << order);
}

static __always_inline void unaccount_slab(struct slab *slab, int order,
					   struct kmem_cache *s)
{
	if (memcg_kmem_enabled())
		memcg_free_slab_cgroups(slab);

	mod_node_page_state(slab_pgdat(slab), cache_vmstat_idx(s),
			    -(PAGE_SIZE << order));
}

static inline struct kmem_cache *cache_from_obj(struct kmem_cache *s, void *x)
{
	struct kmem_cache *cachep;

	if (!IS_ENABLED(CONFIG_SLAB_FREELIST_HARDENED) &&
	    !kmem_cache_debug_flags(s, SLAB_CONSISTENCY_CHECKS))
		return s;

	cachep = virt_to_cache(x);
	if (WARN(cachep && cachep != s,
		  "%s: Wrong slab cache. %s but object is from %s\n",
		  __func__, s->name, cachep->name))
		print_tracking(cachep, x);
	return cachep;
}
#endif /* CONFIG_SLOB */

static inline size_t slab_ksize(const struct kmem_cache *s)
{
#ifndef CONFIG_SLUB
	return s->object_size;

#else /* CONFIG_SLUB */
# ifdef CONFIG_SLUB_DEBUG
	/*
	 * Debugging requires use of the padding between object
	 * and whatever may come after it.
	 */
	if (s->flags & (SLAB_RED_ZONE | SLAB_POISON))
		return s->object_size;
# endif
	if (s->flags & SLAB_KASAN)
		return s->object_size;
	/*
	 * If we have the need to store the freelist pointer
	 * back there or track user information then we can
	 * only use the space before that information.
	 */
	if (s->flags & (SLAB_TYPESAFE_BY_RCU | SLAB_STORE_USER))
		return s->inuse;
	/*
	 * Else we can use all the padding etc for the allocation
	 */
	return s->size;
#endif
}

static inline struct kmem_cache *slab_pre_alloc_hook(struct kmem_cache *s,
						     struct list_lru *lru,
						     struct obj_cgroup **objcgp,
						     size_t size, gfp_t flags)
{
	flags &= gfp_allowed_mask;

	might_alloc(flags);

	if (should_failslab(s, flags))
		return NULL;

	if (!memcg_slab_pre_alloc_hook(s, lru, objcgp, size, flags))
		return NULL;

	return s;
}

static inline void slab_post_alloc_hook(struct kmem_cache *s,
					struct obj_cgroup *objcg, gfp_t flags,
					size_t size, void **p, bool init)
{
	size_t i;

	flags &= gfp_allowed_mask;

	/*
	 * As memory initialization might be integrated into KASAN,
	 * kasan_slab_alloc and initialization memset must be
	 * kept together to avoid discrepancies in behavior.
	 *
	 * As p[i] might get tagged, memset and kmemleak hook come after KASAN.
	 */
	for (i = 0; i < size; i++) {
		p[i] = kasan_slab_alloc(s, p[i], flags, init);
		if (p[i] && init && !kasan_has_integrated_init())
			memset(p[i], 0, s->object_size);
		kmemleak_alloc_recursive(p[i], s->object_size, 1,
					 s->flags, flags);
	}

	memcg_slab_post_alloc_hook(s, objcg, flags, size, p);
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
	unsigned long total_slabs;	/* length of all slab lists */
	unsigned long free_slabs;	/* length of free slab list only */
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

#if defined(CONFIG_SLAB) || defined(CONFIG_SLUB_DEBUG)
void dump_unreclaimable_slab(void);
#else
static inline void dump_unreclaimable_slab(void)
{
}
#endif

void ___cache_free(struct kmem_cache *cache, void *x, unsigned long addr);

#ifdef CONFIG_SLAB_FREELIST_RANDOM
int cache_random_seq_create(struct kmem_cache *cachep, unsigned int count,
			gfp_t gfp);
void cache_random_seq_destroy(struct kmem_cache *cachep);
#else
static inline int cache_random_seq_create(struct kmem_cache *cachep,
					unsigned int count, gfp_t gfp)
{
	return 0;
}
static inline void cache_random_seq_destroy(struct kmem_cache *cachep) { }
#endif /* CONFIG_SLAB_FREELIST_RANDOM */

static inline bool slab_want_init_on_alloc(gfp_t flags, struct kmem_cache *c)
{
	if (static_branch_maybe(CONFIG_INIT_ON_ALLOC_DEFAULT_ON,
				&init_on_alloc)) {
		if (c->ctor)
			return false;
		if (c->flags & (SLAB_TYPESAFE_BY_RCU | SLAB_POISON))
			return flags & __GFP_ZERO;
		return true;
	}
	return flags & __GFP_ZERO;
}

static inline bool slab_want_init_on_free(struct kmem_cache *c)
{
	if (static_branch_maybe(CONFIG_INIT_ON_FREE_DEFAULT_ON,
				&init_on_free))
		return !(c->ctor ||
			 (c->flags & (SLAB_TYPESAFE_BY_RCU | SLAB_POISON)));
	return false;
}

#if defined(CONFIG_DEBUG_FS) && defined(CONFIG_SLUB_DEBUG)
void debugfs_slab_release(struct kmem_cache *);
#else
static inline void debugfs_slab_release(struct kmem_cache *s) { }
#endif

#ifdef CONFIG_PRINTK
#define KS_ADDRS_COUNT 16
struct kmem_obj_info {
	void *kp_ptr;
	struct slab *kp_slab;
	void *kp_objp;
	unsigned long kp_data_offset;
	struct kmem_cache *kp_slab_cache;
	void *kp_ret;
	void *kp_stack[KS_ADDRS_COUNT];
	void *kp_free_stack[KS_ADDRS_COUNT];
};
void __kmem_obj_info(struct kmem_obj_info *kpp, void *object, struct slab *slab);
#endif

#ifdef CONFIG_HAVE_HARDENED_USERCOPY_ALLOCATOR
void __check_heap_object(const void *ptr, unsigned long n,
			 const struct slab *slab, bool to_user);
#else
static inline
void __check_heap_object(const void *ptr, unsigned long n,
			 const struct slab *slab, bool to_user)
{
}
#endif

#endif /* MM_SLAB_H */
