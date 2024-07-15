/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MM_SLAB_H
#define MM_SLAB_H

#include <linux/reciprocal_div.h>
#include <linux/list_lru.h>
#include <linux/local_lock.h>
#include <linux/random.h>
#include <linux/kobject.h>
#include <linux/sched/mm.h>
#include <linux/memcontrol.h>
#include <linux/kfence.h>
#include <linux/kasan.h>

/*
 * Internal slab definitions
 */

#ifdef CONFIG_64BIT
# ifdef system_has_cmpxchg128
# define system_has_freelist_aba()	system_has_cmpxchg128()
# define try_cmpxchg_freelist		try_cmpxchg128
# endif
#define this_cpu_try_cmpxchg_freelist	this_cpu_try_cmpxchg128
typedef u128 freelist_full_t;
#else /* CONFIG_64BIT */
# ifdef system_has_cmpxchg64
# define system_has_freelist_aba()	system_has_cmpxchg64()
# define try_cmpxchg_freelist		try_cmpxchg64
# endif
#define this_cpu_try_cmpxchg_freelist	this_cpu_try_cmpxchg64
typedef u64 freelist_full_t;
#endif /* CONFIG_64BIT */

#if defined(system_has_freelist_aba) && !defined(CONFIG_HAVE_ALIGNED_STRUCT_PAGE)
#undef system_has_freelist_aba
#endif

/*
 * Freelist pointer and counter to cmpxchg together, avoids the typical ABA
 * problems with cmpxchg of just a pointer.
 */
typedef union {
	struct {
		void *freelist;
		unsigned long counter;
	};
	freelist_full_t full;
} freelist_aba_t;

/* Reuses the bits in struct page */
struct slab {
	unsigned long __page_flags;

	struct kmem_cache *slab_cache;
	union {
		struct {
			union {
				struct list_head slab_list;
#ifdef CONFIG_SLUB_CPU_PARTIAL
				struct {
					struct slab *next;
					int slabs;	/* Nr of slabs left */
				};
#endif
			};
			/* Double-word boundary */
			union {
				struct {
					void *freelist;		/* first free object */
					union {
						unsigned long counters;
						struct {
							unsigned inuse:16;
							unsigned objects:15;
							unsigned frozen:1;
						};
					};
				};
#ifdef system_has_freelist_aba
				freelist_aba_t freelist_counter;
#endif
			};
		};
		struct rcu_head rcu_head;
	};

	unsigned int __page_type;
	atomic_t __page_refcount;
#ifdef CONFIG_SLAB_OBJ_EXT
	unsigned long obj_exts;
#endif
};

#define SLAB_MATCH(pg, sl)						\
	static_assert(offsetof(struct page, pg) == offsetof(struct slab, sl))
SLAB_MATCH(flags, __page_flags);
SLAB_MATCH(compound_head, slab_cache);	/* Ensure bit 0 is clear */
SLAB_MATCH(_refcount, __page_refcount);
#ifdef CONFIG_SLAB_OBJ_EXT
SLAB_MATCH(memcg_data, obj_exts);
#endif
#undef SLAB_MATCH
static_assert(sizeof(struct slab) <= sizeof(struct page));
#if defined(system_has_freelist_aba)
static_assert(IS_ALIGNED(offsetof(struct slab, freelist), sizeof(freelist_aba_t)));
#endif

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

#ifdef CONFIG_SLUB_CPU_PARTIAL
#define slub_percpu_partial(c)			((c)->partial)

#define slub_set_percpu_partial(c, p)		\
({						\
	slub_percpu_partial(c) = (p)->next;	\
})

#define slub_percpu_partial_read_once(c)	READ_ONCE(slub_percpu_partial(c))
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
	unsigned int size;		/* Object size including metadata */
	unsigned int object_size;	/* Object size without metadata */
	struct reciprocal_value reciprocal_size;
	unsigned int offset;		/* Free pointer offset */
#ifdef CONFIG_SLUB_CPU_PARTIAL
	/* Number of per cpu partial objects to keep around */
	unsigned int cpu_partial;
	/* Number of per cpu partial slabs to keep around */
	unsigned int cpu_partial_slabs;
#endif
	struct kmem_cache_order_objects oo;

	/* Allocation and freeing of slabs */
	struct kmem_cache_order_objects min;
	gfp_t allocflags;		/* gfp flags to use on each alloc */
	int refcount;			/* Refcount for slab cache destroy */
	void (*ctor)(void *object);	/* Object constructor */
	unsigned int inuse;		/* Offset to metadata */
	unsigned int align;		/* Alignment */
	unsigned int red_left_pad;	/* Left redzone padding size */
	const char *name;		/* Name (only for display!) */
	struct list_head list;		/* List of slab caches */
#ifdef CONFIG_SYSFS
	struct kobject kobj;		/* For sysfs */
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
void sysfs_slab_unlink(struct kmem_cache *s);
void sysfs_slab_release(struct kmem_cache *s);
#else
static inline void sysfs_slab_unlink(struct kmem_cache *s) { }
static inline void sysfs_slab_release(struct kmem_cache *s) { }
#endif

void *fixup_red_left(struct kmem_cache *s, void *p);

static inline void *nearest_obj(struct kmem_cache *cache,
				const struct slab *slab, void *x)
{
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

/* Kmalloc array related functions */
void setup_kmalloc_cache_index_table(void);
void create_kmalloc_caches(void);

extern u8 kmalloc_size_index[24];

static inline unsigned int size_index_elem(unsigned int bytes)
{
	return (bytes - 1) / 8;
}

/*
 * Find the kmem_cache structure that serves a given size of
 * allocation
 *
 * This assumes size is larger than zero and not larger than
 * KMALLOC_MAX_CACHE_SIZE and the caller must check that.
 */
static inline struct kmem_cache *
kmalloc_slab(size_t size, gfp_t flags, unsigned long caller)
{
	unsigned int index;

	if (size <= 192)
		index = kmalloc_size_index[size_index_elem(size)];
	else
		index = fls(size - 1);

	return kmalloc_caches[kmalloc_type(flags, caller)][index];
}

gfp_t kmalloc_fix_flags(gfp_t flags);

/* Functions provided by the slab allocators */
int __kmem_cache_create(struct kmem_cache *, slab_flags_t flags);

void __init kmem_cache_init(void);
extern void create_boot_cache(struct kmem_cache *, const char *name,
			unsigned int size, slab_flags_t flags,
			unsigned int useroffset, unsigned int usersize);

int slab_unmergeable(struct kmem_cache *s);
struct kmem_cache *find_mergeable(unsigned size, unsigned align,
		slab_flags_t flags, const char *name, void (*ctor)(void *));
struct kmem_cache *
__kmem_cache_alias(const char *name, unsigned int size, unsigned int align,
		   slab_flags_t flags, void (*ctor)(void *));

slab_flags_t kmem_cache_flags(slab_flags_t flags, const char *name);

static inline bool is_kmalloc_cache(struct kmem_cache *s)
{
	return (s->flags & SLAB_KMALLOC);
}

/* Legal flag mask for kmem_cache_create(), for various configurations */
#define SLAB_CORE_FLAGS (SLAB_HWCACHE_ALIGN | SLAB_CACHE_DMA | \
			 SLAB_CACHE_DMA32 | SLAB_PANIC | \
			 SLAB_TYPESAFE_BY_RCU | SLAB_DEBUG_OBJECTS )

#ifdef CONFIG_SLUB_DEBUG
#define SLAB_DEBUG_FLAGS (SLAB_RED_ZONE | SLAB_POISON | SLAB_STORE_USER | \
			  SLAB_TRACE | SLAB_CONSISTENCY_CHECKS)
#else
#define SLAB_DEBUG_FLAGS (0)
#endif

#define SLAB_CACHE_FLAGS (SLAB_NOLEAKTRACE | SLAB_RECLAIM_ACCOUNT | \
			  SLAB_TEMPORARY | SLAB_ACCOUNT | \
			  SLAB_NO_USER_FLAGS | SLAB_KMALLOC | SLAB_NO_MERGE)

/* Common flags available with current configuration */
#define CACHE_CREATE_MASK (SLAB_CORE_FLAGS | SLAB_DEBUG_FLAGS | SLAB_CACHE_FLAGS)

/* Common flags permitted for kmem_cache_create */
#define SLAB_FLAGS_PERMITTED (SLAB_CORE_FLAGS | \
			      SLAB_RED_ZONE | \
			      SLAB_POISON | \
			      SLAB_STORE_USER | \
			      SLAB_TRACE | \
			      SLAB_CONSISTENCY_CHECKS | \
			      SLAB_NOLEAKTRACE | \
			      SLAB_RECLAIM_ACCOUNT | \
			      SLAB_TEMPORARY | \
			      SLAB_ACCOUNT | \
			      SLAB_KMALLOC | \
			      SLAB_NO_MERGE | \
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
 * Returns true if any of the specified slab_debug flags is enabled for the
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

#ifdef CONFIG_SLAB_OBJ_EXT

/*
 * slab_obj_exts - get the pointer to the slab object extension vector
 * associated with a slab.
 * @slab: a pointer to the slab struct
 *
 * Returns a pointer to the object extension vector associated with the slab,
 * or NULL if no such vector has been associated yet.
 */
static inline struct slabobj_ext *slab_obj_exts(struct slab *slab)
{
	unsigned long obj_exts = READ_ONCE(slab->obj_exts);

#ifdef CONFIG_MEMCG
	VM_BUG_ON_PAGE(obj_exts && !(obj_exts & MEMCG_DATA_OBJEXTS),
							slab_page(slab));
	VM_BUG_ON_PAGE(obj_exts & MEMCG_DATA_KMEM, slab_page(slab));
#endif
	return (struct slabobj_ext *)(obj_exts & ~OBJEXTS_FLAGS_MASK);
}

int alloc_slab_obj_exts(struct slab *slab, struct kmem_cache *s,
                        gfp_t gfp, bool new_slab);

#else /* CONFIG_SLAB_OBJ_EXT */

static inline struct slabobj_ext *slab_obj_exts(struct slab *slab)
{
	return NULL;
}

#endif /* CONFIG_SLAB_OBJ_EXT */

static inline enum node_stat_item cache_vmstat_idx(struct kmem_cache *s)
{
	return (s->flags & SLAB_RECLAIM_ACCOUNT) ?
		NR_SLAB_RECLAIMABLE_B : NR_SLAB_UNRECLAIMABLE_B;
}

#ifdef CONFIG_MEMCG_KMEM
bool __memcg_slab_post_alloc_hook(struct kmem_cache *s, struct list_lru *lru,
				  gfp_t flags, size_t size, void **p);
void __memcg_slab_free_hook(struct kmem_cache *s, struct slab *slab,
			    void **p, int objects, struct slabobj_ext *obj_exts);
#endif

size_t __ksize(const void *objp);

static inline size_t slab_ksize(const struct kmem_cache *s)
{
#ifdef CONFIG_SLUB_DEBUG
	/*
	 * Debugging requires use of the padding between object
	 * and whatever may come after it.
	 */
	if (s->flags & (SLAB_RED_ZONE | SLAB_POISON))
		return s->object_size;
#endif
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
}

#ifdef CONFIG_SLUB_DEBUG
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

void __check_heap_object(const void *ptr, unsigned long n,
			 const struct slab *slab, bool to_user);

#ifdef CONFIG_SLUB_DEBUG
void skip_orig_size_check(struct kmem_cache *s, const void *object);
#endif

#endif /* MM_SLAB_H */
