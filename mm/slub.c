// SPDX-License-Identifier: GPL-2.0
/*
 * SLUB: A slab allocator with low overhead percpu array caches and mostly
 * lockless freeing of objects to slabs in the slowpath.
 *
 * The allocator synchronizes using spin_trylock for percpu arrays in the
 * fastpath, and cmpxchg_double (or bit spinlock) for slowpath freeing.
 * Uses a centralized lock to manage a pool of partial slabs.
 *
 * (C) 2007 SGI, Christoph Lameter
 * (C) 2011 Linux Foundation, Christoph Lameter
 * (C) 2025 SUSE, Vlastimil Babka
 */

#include <linux/mm.h>
#include <linux/swap.h> /* mm_account_reclaimed_pages() */
#include <linux/module.h>
#include <linux/bit_spinlock.h>
#include <linux/interrupt.h>
#include <linux/swab.h>
#include <linux/bitops.h>
#include <linux/slab.h>
#include "slab.h"
#include <linux/vmalloc.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/kasan.h>
#include <linux/node.h>
#include <linux/kmsan.h>
#include <linux/cpu.h>
#include <linux/cpuset.h>
#include <linux/mempolicy.h>
#include <linux/ctype.h>
#include <linux/stackdepot.h>
#include <linux/debugobjects.h>
#include <linux/kallsyms.h>
#include <linux/kfence.h>
#include <linux/memory.h>
#include <linux/math64.h>
#include <linux/fault-inject.h>
#include <linux/kmemleak.h>
#include <linux/stacktrace.h>
#include <linux/prefetch.h>
#include <linux/memcontrol.h>
#include <linux/random.h>
#include <kunit/test.h>
#include <kunit/test-bug.h>
#include <linux/sort.h>
#include <linux/irq_work.h>
#include <linux/kprobes.h>
#include <linux/debugfs.h>
#include <trace/events/kmem.h>

#include "internal.h"

/*
 * Lock order:
 *   0.  cpu_hotplug_lock
 *   1.  slab_mutex (Global Mutex)
 *   2a. kmem_cache->cpu_sheaves->lock (Local trylock)
 *   2b. node->barn->lock (Spinlock)
 *   2c. node->list_lock (Spinlock)
 *   3.  slab_lock(slab) (Only on some arches)
 *   4.  object_map_lock (Only for debugging)
 *
 *   slab_mutex
 *
 *   The role of the slab_mutex is to protect the list of all the slabs
 *   and to synchronize major metadata changes to slab cache structures.
 *   Also synchronizes memory hotplug callbacks.
 *
 *   slab_lock
 *
 *   The slab_lock is a wrapper around the page lock, thus it is a bit
 *   spinlock.
 *
 *   The slab_lock is only used on arches that do not have the ability
 *   to do a cmpxchg_double. It only protects:
 *
 *	A. slab->freelist	-> List of free objects in a slab
 *	B. slab->inuse		-> Number of objects in use
 *	C. slab->objects	-> Number of objects in slab
 *	D. slab->frozen		-> frozen state
 *
 *   SL_partial slabs
 *
 *   Slabs on node partial list have at least one free object. A limited number
 *   of slabs on the list can be fully free (slab->inuse == 0), until we start
 *   discarding them. These slabs are marked with SL_partial, and the flag is
 *   cleared while removing them, usually to grab their freelist afterwards.
 *   This clearing also exempts them from list management. Please see
 *   __slab_free() for more details.
 *
 *   Full slabs
 *
 *   For caches without debugging enabled, full slabs (slab->inuse ==
 *   slab->objects and slab->freelist == NULL) are not placed on any list.
 *   The __slab_free() freeing the first object from such a slab will place
 *   it on the partial list. Caches with debugging enabled place such slab
 *   on the full list and use different allocation and freeing paths.
 *
 *   Frozen slabs
 *
 *   If a slab is frozen then it is exempt from list management. It is used to
 *   indicate a slab that has failed consistency checks and thus cannot be
 *   allocated from anymore - it is also marked as full. Any previously
 *   allocated objects will be simply leaked upon freeing instead of attempting
 *   to modify the potentially corrupted freelist and metadata.
 *
 *   To sum up, the current scheme is:
 *   - node partial slab:            SL_partial && !full && !frozen
 *   - taken off partial list:      !SL_partial && !full && !frozen
 *   - full slab, not on any list:  !SL_partial &&  full && !frozen
 *   - frozen due to inconsistency: !SL_partial &&  full &&  frozen
 *
 *   node->list_lock (spinlock)
 *
 *   The list_lock protects the partial and full list on each node and
 *   the partial slab counter. If taken then no new slabs may be added or
 *   removed from the lists nor make the number of partial slabs be modified.
 *   (Note that the total number of slabs is an atomic value that may be
 *   modified without taking the list lock).
 *
 *   The list_lock is a centralized lock and thus we avoid taking it as
 *   much as possible. As long as SLUB does not have to handle partial
 *   slabs, operations can continue without any centralized lock.
 *
 *   For debug caches, all allocations are forced to go through a list_lock
 *   protected region to serialize against concurrent validation.
 *
 *   cpu_sheaves->lock (local_trylock)
 *
 *   This lock protects fastpath operations on the percpu sheaves. On !RT it
 *   only disables preemption and does no atomic operations. As long as the main
 *   or spare sheaf can handle the allocation or free, there is no other
 *   overhead.
 *
 *   node->barn->lock (spinlock)
 *
 *   This lock protects the operations on per-NUMA-node barn. It can quickly
 *   serve an empty or full sheaf if available, and avoid more expensive refill
 *   or flush operation.
 *
 *   Lockless freeing
 *
 *   Objects may have to be freed to their slabs when they are from a remote
 *   node (where we want to avoid filling local sheaves with remote objects)
 *   or when there are too many full sheaves. On architectures supporting
 *   cmpxchg_double this is done by a lockless update of slab's freelist and
 *   counters, otherwise slab_lock is taken. This only needs to take the
 *   list_lock if it's a first free to a full slab, or when a slab becomes empty
 *   after the free.
 *
 *   irq, preemption, migration considerations
 *
 *   Interrupts are disabled as part of list_lock or barn lock operations, or
 *   around the slab_lock operation, in order to make the slab allocator safe
 *   to use in the context of an irq.
 *   Preemption is disabled as part of local_trylock operations.
 *   kmalloc_nolock() and kfree_nolock() are safe in NMI context but see
 *   their limitations.
 *
 * SLUB assigns two object arrays called sheaves for caching allocations and
 * frees on each cpu, with a NUMA node shared barn for balancing between cpus.
 * Allocations and frees are primarily served from these sheaves.
 *
 * Slabs with free elements are kept on a partial list and during regular
 * operations no list for full slabs is used. If an object in a full slab is
 * freed then the slab will show up again on the partial lists.
 * We track full slabs for debugging purposes though because otherwise we
 * cannot scan all objects.
 *
 * Slabs are freed when they become empty. Teardown and setup is minimal so we
 * rely on the page allocators per cpu caches for fast frees and allocs.
 *
 * SLAB_DEBUG_FLAGS	Slab requires special handling due to debug
 * 			options set. This moves	slab handling out of
 * 			the fast path and disables lockless freelists.
 */

/**
 * enum slab_flags - How the slab flags bits are used.
 * @SL_locked: Is locked with slab_lock()
 * @SL_partial: On the per-node partial list
 * @SL_pfmemalloc: Was allocated from PF_MEMALLOC reserves
 *
 * The slab flags share space with the page flags but some bits have
 * different interpretations.  The high bits are used for information
 * like zone/node/section.
 */
enum slab_flags {
	SL_locked = PG_locked,
	SL_partial = PG_workingset,	/* Historical reasons for this bit */
	SL_pfmemalloc = PG_active,	/* Historical reasons for this bit */
};

#ifndef CONFIG_SLUB_TINY
#define __fastpath_inline __always_inline
#else
#define __fastpath_inline
#endif

#ifdef CONFIG_SLUB_DEBUG
#ifdef CONFIG_SLUB_DEBUG_ON
DEFINE_STATIC_KEY_TRUE(slub_debug_enabled);
#else
DEFINE_STATIC_KEY_FALSE(slub_debug_enabled);
#endif
#endif		/* CONFIG_SLUB_DEBUG */

#ifdef CONFIG_NUMA
static DEFINE_STATIC_KEY_FALSE(strict_numa);
#endif

/* Structure holding parameters for get_from_partial() call chain */
struct partial_context {
	gfp_t flags;
	unsigned int orig_size;
};

/* Structure holding parameters for get_partial_node_bulk() */
struct partial_bulk_context {
	gfp_t flags;
	unsigned int min_objects;
	unsigned int max_objects;
	struct list_head slabs;
};

static inline bool kmem_cache_debug(struct kmem_cache *s)
{
	return kmem_cache_debug_flags(s, SLAB_DEBUG_FLAGS);
}

void *fixup_red_left(struct kmem_cache *s, void *p)
{
	if (kmem_cache_debug_flags(s, SLAB_RED_ZONE))
		p += s->red_left_pad;

	return p;
}

/*
 * Issues still to be resolved:
 *
 * - Support PAGE_ALLOC_DEBUG. Should be easy to do.
 *
 * - Variable sizing of the per node arrays
 */

/* Enable to log cmpxchg failures */
#undef SLUB_DEBUG_CMPXCHG

#ifndef CONFIG_SLUB_TINY
/*
 * Minimum number of partial slabs. These will be left on the partial
 * lists even if they are empty. kmem_cache_shrink may reclaim them.
 */
#define MIN_PARTIAL 5

/*
 * Maximum number of desirable partial slabs.
 * The existence of more partial slabs makes kmem_cache_shrink
 * sort the partial list by the number of objects in use.
 */
#define MAX_PARTIAL 10
#else
#define MIN_PARTIAL 0
#define MAX_PARTIAL 0
#endif

#define DEBUG_DEFAULT_FLAGS (SLAB_CONSISTENCY_CHECKS | SLAB_RED_ZONE | \
				SLAB_POISON | SLAB_STORE_USER)

/*
 * These debug flags cannot use CMPXCHG because there might be consistency
 * issues when checking or reading debug information
 */
#define SLAB_NO_CMPXCHG (SLAB_CONSISTENCY_CHECKS | SLAB_STORE_USER | \
				SLAB_TRACE)


/*
 * Debugging flags that require metadata to be stored in the slab.  These get
 * disabled when slab_debug=O is used and a cache's min order increases with
 * metadata.
 */
#define DEBUG_METADATA_FLAGS (SLAB_RED_ZONE | SLAB_POISON | SLAB_STORE_USER)

#define OO_SHIFT	16
#define OO_MASK		((1 << OO_SHIFT) - 1)
#define MAX_OBJS_PER_PAGE	32767 /* since slab.objects is u15 */

/* Internal SLUB flags */
/* Poison object */
#define __OBJECT_POISON		__SLAB_FLAG_BIT(_SLAB_OBJECT_POISON)
/* Use cmpxchg_double */

#ifdef system_has_freelist_aba
#define __CMPXCHG_DOUBLE	__SLAB_FLAG_BIT(_SLAB_CMPXCHG_DOUBLE)
#else
#define __CMPXCHG_DOUBLE	__SLAB_FLAG_UNUSED
#endif

/*
 * Tracking user of a slab.
 */
#define TRACK_ADDRS_COUNT 16
struct track {
	unsigned long addr;	/* Called from address */
#ifdef CONFIG_STACKDEPOT
	depot_stack_handle_t handle;
#endif
	int cpu;		/* Was running on cpu */
	int pid;		/* Pid context */
	unsigned long when;	/* When did the operation occur */
};

enum track_item { TRACK_ALLOC, TRACK_FREE };

#ifdef SLAB_SUPPORTS_SYSFS
static int sysfs_slab_add(struct kmem_cache *);
#else
static inline int sysfs_slab_add(struct kmem_cache *s) { return 0; }
#endif

#if defined(CONFIG_DEBUG_FS) && defined(CONFIG_SLUB_DEBUG)
static void debugfs_slab_add(struct kmem_cache *);
#else
static inline void debugfs_slab_add(struct kmem_cache *s) { }
#endif

enum add_mode {
	ADD_TO_HEAD,
	ADD_TO_TAIL,
};

enum stat_item {
	ALLOC_FASTPATH,		/* Allocation from percpu sheaves */
	ALLOC_SLOWPATH,		/* Allocation from partial or new slab */
	FREE_RCU_SHEAF,		/* Free to rcu_free sheaf */
	FREE_RCU_SHEAF_FAIL,	/* Failed to free to a rcu_free sheaf */
	FREE_FASTPATH,		/* Free to percpu sheaves */
	FREE_SLOWPATH,		/* Free to a slab */
	FREE_ADD_PARTIAL,	/* Freeing moves slab to partial list */
	FREE_REMOVE_PARTIAL,	/* Freeing removes last object */
	ALLOC_SLAB,		/* New slab acquired from page allocator */
	ALLOC_NODE_MISMATCH,	/* Requested node different from cpu sheaf */
	FREE_SLAB,		/* Slab freed to the page allocator */
	ORDER_FALLBACK,		/* Number of times fallback was necessary */
	CMPXCHG_DOUBLE_FAIL,	/* Failures of slab freelist update */
	SHEAF_FLUSH,		/* Objects flushed from a sheaf */
	SHEAF_REFILL,		/* Objects refilled to a sheaf */
	SHEAF_ALLOC,		/* Allocation of an empty sheaf */
	SHEAF_FREE,		/* Freeing of an empty sheaf */
	BARN_GET,		/* Got full sheaf from barn */
	BARN_GET_FAIL,		/* Failed to get full sheaf from barn */
	BARN_PUT,		/* Put full sheaf to barn */
	BARN_PUT_FAIL,		/* Failed to put full sheaf to barn */
	SHEAF_PREFILL_FAST,	/* Sheaf prefill grabbed the spare sheaf */
	SHEAF_PREFILL_SLOW,	/* Sheaf prefill found no spare sheaf */
	SHEAF_PREFILL_OVERSIZE,	/* Allocation of oversize sheaf for prefill */
	SHEAF_RETURN_FAST,	/* Sheaf return reattached spare sheaf */
	SHEAF_RETURN_SLOW,	/* Sheaf return could not reattach spare */
	NR_SLUB_STAT_ITEMS
};

#ifdef CONFIG_SLUB_STATS
struct kmem_cache_stats {
	unsigned int stat[NR_SLUB_STAT_ITEMS];
};
#endif

static inline void stat(const struct kmem_cache *s, enum stat_item si)
{
#ifdef CONFIG_SLUB_STATS
	/*
	 * The rmw is racy on a preemptible kernel but this is acceptable, so
	 * avoid this_cpu_add()'s irq-disable overhead.
	 */
	raw_cpu_inc(s->cpu_stats->stat[si]);
#endif
}

static inline
void stat_add(const struct kmem_cache *s, enum stat_item si, int v)
{
#ifdef CONFIG_SLUB_STATS
	raw_cpu_add(s->cpu_stats->stat[si], v);
#endif
}

#define MAX_FULL_SHEAVES	10
#define MAX_EMPTY_SHEAVES	10

struct node_barn {
	spinlock_t lock;
	struct list_head sheaves_full;
	struct list_head sheaves_empty;
	unsigned int nr_full;
	unsigned int nr_empty;
};

struct slab_sheaf {
	union {
		struct rcu_head rcu_head;
		struct list_head barn_list;
		/* only used for prefilled sheafs */
		struct {
			unsigned int capacity;
			bool pfmemalloc;
		};
	};
	struct kmem_cache *cache;
	unsigned int size;
	int node; /* only used for rcu_sheaf */
	void *objects[];
};

struct slub_percpu_sheaves {
	local_trylock_t lock;
	struct slab_sheaf *main; /* never NULL when unlocked */
	struct slab_sheaf *spare; /* empty or full, may be NULL */
	struct slab_sheaf *rcu_free; /* for batching kfree_rcu() */
};

/*
 * The slab lists for all objects.
 */
struct kmem_cache_node {
	spinlock_t list_lock;
	unsigned long nr_partial;
	struct list_head partial;
#ifdef CONFIG_SLUB_DEBUG
	atomic_long_t nr_slabs;
	atomic_long_t total_objects;
	struct list_head full;
#endif
	struct node_barn *barn;
};

static inline struct kmem_cache_node *get_node(struct kmem_cache *s, int node)
{
	return s->node[node];
}

/*
 * Get the barn of the current cpu's closest memory node. It may not exist on
 * systems with memoryless nodes but without CONFIG_HAVE_MEMORYLESS_NODES
 */
static inline struct node_barn *get_barn(struct kmem_cache *s)
{
	struct kmem_cache_node *n = get_node(s, numa_mem_id());

	if (!n)
		return NULL;

	return n->barn;
}

/*
 * Iterator over all nodes. The body will be executed for each node that has
 * a kmem_cache_node structure allocated (which is true for all online nodes)
 */
#define for_each_kmem_cache_node(__s, __node, __n) \
	for (__node = 0; __node < nr_node_ids; __node++) \
		 if ((__n = get_node(__s, __node)))

/*
 * Tracks for which NUMA nodes we have kmem_cache_nodes allocated.
 * Corresponds to node_state[N_MEMORY], but can temporarily
 * differ during memory hotplug/hotremove operations.
 * Protected by slab_mutex.
 */
static nodemask_t slab_nodes;

/*
 * Workqueue used for flushing cpu and kfree_rcu sheaves.
 */
static struct workqueue_struct *flushwq;

struct slub_flush_work {
	struct work_struct work;
	struct kmem_cache *s;
	bool skip;
};

static DEFINE_MUTEX(flush_lock);
static DEFINE_PER_CPU(struct slub_flush_work, slub_flush);

/********************************************************************
 * 			Core slab cache functions
 *******************************************************************/

/*
 * Returns freelist pointer (ptr). With hardening, this is obfuscated
 * with an XOR of the address where the pointer is held and a per-cache
 * random number.
 */
static inline freeptr_t freelist_ptr_encode(const struct kmem_cache *s,
					    void *ptr, unsigned long ptr_addr)
{
	unsigned long encoded;

#ifdef CONFIG_SLAB_FREELIST_HARDENED
	encoded = (unsigned long)ptr ^ s->random ^ swab(ptr_addr);
#else
	encoded = (unsigned long)ptr;
#endif
	return (freeptr_t){.v = encoded};
}

static inline void *freelist_ptr_decode(const struct kmem_cache *s,
					freeptr_t ptr, unsigned long ptr_addr)
{
	void *decoded;

#ifdef CONFIG_SLAB_FREELIST_HARDENED
	decoded = (void *)(ptr.v ^ s->random ^ swab(ptr_addr));
#else
	decoded = (void *)ptr.v;
#endif
	return decoded;
}

static inline void *get_freepointer(struct kmem_cache *s, void *object)
{
	unsigned long ptr_addr;
	freeptr_t p;

	object = kasan_reset_tag(object);
	ptr_addr = (unsigned long)object + s->offset;
	p = *(freeptr_t *)(ptr_addr);
	return freelist_ptr_decode(s, p, ptr_addr);
}

static inline void set_freepointer(struct kmem_cache *s, void *object, void *fp)
{
	unsigned long freeptr_addr = (unsigned long)object + s->offset;

#ifdef CONFIG_SLAB_FREELIST_HARDENED
	BUG_ON(object == fp); /* naive detection of double free or corruption */
#endif

	freeptr_addr = (unsigned long)kasan_reset_tag((void *)freeptr_addr);
	*(freeptr_t *)freeptr_addr = freelist_ptr_encode(s, fp, freeptr_addr);
}

/*
 * See comment in calculate_sizes().
 */
static inline bool freeptr_outside_object(struct kmem_cache *s)
{
	return s->offset >= s->inuse;
}

/*
 * Return offset of the end of info block which is inuse + free pointer if
 * not overlapping with object.
 */
static inline unsigned int get_info_end(struct kmem_cache *s)
{
	if (freeptr_outside_object(s))
		return s->inuse + sizeof(void *);
	else
		return s->inuse;
}

/* Loop over all objects in a slab */
#define for_each_object(__p, __s, __addr, __objects) \
	for (__p = fixup_red_left(__s, __addr); \
		__p < (__addr) + (__objects) * (__s)->size; \
		__p += (__s)->size)

static inline unsigned int order_objects(unsigned int order, unsigned int size)
{
	return ((unsigned int)PAGE_SIZE << order) / size;
}

static inline struct kmem_cache_order_objects oo_make(unsigned int order,
		unsigned int size)
{
	struct kmem_cache_order_objects x = {
		(order << OO_SHIFT) + order_objects(order, size)
	};

	return x;
}

static inline unsigned int oo_order(struct kmem_cache_order_objects x)
{
	return x.x >> OO_SHIFT;
}

static inline unsigned int oo_objects(struct kmem_cache_order_objects x)
{
	return x.x & OO_MASK;
}

/*
 * If network-based swap is enabled, slub must keep track of whether memory
 * were allocated from pfmemalloc reserves.
 */
static inline bool slab_test_pfmemalloc(const struct slab *slab)
{
	return test_bit(SL_pfmemalloc, &slab->flags.f);
}

static inline void slab_set_pfmemalloc(struct slab *slab)
{
	set_bit(SL_pfmemalloc, &slab->flags.f);
}

static inline void __slab_clear_pfmemalloc(struct slab *slab)
{
	__clear_bit(SL_pfmemalloc, &slab->flags.f);
}

/*
 * Per slab locking using the pagelock
 */
static __always_inline void slab_lock(struct slab *slab)
{
	bit_spin_lock(SL_locked, &slab->flags.f);
}

static __always_inline void slab_unlock(struct slab *slab)
{
	bit_spin_unlock(SL_locked, &slab->flags.f);
}

static inline bool
__update_freelist_fast(struct slab *slab, struct freelist_counters *old,
		       struct freelist_counters *new)
{
#ifdef system_has_freelist_aba
	return try_cmpxchg_freelist(&slab->freelist_counters,
				    &old->freelist_counters,
				    new->freelist_counters);
#else
	return false;
#endif
}

static inline bool
__update_freelist_slow(struct slab *slab, struct freelist_counters *old,
		       struct freelist_counters *new)
{
	bool ret = false;

	slab_lock(slab);
	if (slab->freelist == old->freelist &&
	    slab->counters == old->counters) {
		slab->freelist = new->freelist;
		/* prevent tearing for the read in get_partial_node_bulk() */
		WRITE_ONCE(slab->counters, new->counters);
		ret = true;
	}
	slab_unlock(slab);

	return ret;
}

/*
 * Interrupts must be disabled (for the fallback code to work right), typically
 * by an _irqsave() lock variant. On PREEMPT_RT the preempt_disable(), which is
 * part of bit_spin_lock(), is sufficient because the policy is not to allow any
 * allocation/ free operation in hardirq context. Therefore nothing can
 * interrupt the operation.
 */
static inline bool __slab_update_freelist(struct kmem_cache *s, struct slab *slab,
		struct freelist_counters *old, struct freelist_counters *new, const char *n)
{
	bool ret;

	if (!IS_ENABLED(CONFIG_PREEMPT_RT))
		lockdep_assert_irqs_disabled();

	if (s->flags & __CMPXCHG_DOUBLE)
		ret = __update_freelist_fast(slab, old, new);
	else
		ret = __update_freelist_slow(slab, old, new);

	if (likely(ret))
		return true;

	cpu_relax();
	stat(s, CMPXCHG_DOUBLE_FAIL);

#ifdef SLUB_DEBUG_CMPXCHG
	pr_info("%s %s: cmpxchg double redo ", n, s->name);
#endif

	return false;
}

static inline bool slab_update_freelist(struct kmem_cache *s, struct slab *slab,
		struct freelist_counters *old, struct freelist_counters *new, const char *n)
{
	bool ret;

	if (s->flags & __CMPXCHG_DOUBLE) {
		ret = __update_freelist_fast(slab, old, new);
	} else {
		unsigned long flags;

		local_irq_save(flags);
		ret = __update_freelist_slow(slab, old, new);
		local_irq_restore(flags);
	}
	if (likely(ret))
		return true;

	cpu_relax();
	stat(s, CMPXCHG_DOUBLE_FAIL);

#ifdef SLUB_DEBUG_CMPXCHG
	pr_info("%s %s: cmpxchg double redo ", n, s->name);
#endif

	return false;
}

/*
 * kmalloc caches has fixed sizes (mostly power of 2), and kmalloc() API
 * family will round up the real request size to these fixed ones, so
 * there could be an extra area than what is requested. Save the original
 * request size in the meta data area, for better debug and sanity check.
 */
static inline void set_orig_size(struct kmem_cache *s,
				void *object, unsigned long orig_size)
{
	void *p = kasan_reset_tag(object);

	if (!slub_debug_orig_size(s))
		return;

	p += get_info_end(s);
	p += sizeof(struct track) * 2;

	*(unsigned long *)p = orig_size;
}

static inline unsigned long get_orig_size(struct kmem_cache *s, void *object)
{
	void *p = kasan_reset_tag(object);

	if (is_kfence_address(object))
		return kfence_ksize(object);

	if (!slub_debug_orig_size(s))
		return s->object_size;

	p += get_info_end(s);
	p += sizeof(struct track) * 2;

	return *(unsigned long *)p;
}

#ifdef CONFIG_SLAB_OBJ_EXT

/*
 * Check if memory cgroup or memory allocation profiling is enabled.
 * If enabled, SLUB tries to reduce memory overhead of accounting
 * slab objects. If neither is enabled when this function is called,
 * the optimization is simply skipped to avoid affecting caches that do not
 * need slabobj_ext metadata.
 *
 * However, this may disable optimization when memory cgroup or memory
 * allocation profiling is used, but slabs are created too early
 * even before those subsystems are initialized.
 */
static inline bool need_slab_obj_exts(struct kmem_cache *s)
{
	if (s->flags & SLAB_NO_OBJ_EXT)
		return false;

	if (memcg_kmem_online() && (s->flags & SLAB_ACCOUNT))
		return true;

	if (mem_alloc_profiling_enabled())
		return true;

	return false;
}

static inline unsigned int obj_exts_size_in_slab(struct slab *slab)
{
	return sizeof(struct slabobj_ext) * slab->objects;
}

static inline unsigned long obj_exts_offset_in_slab(struct kmem_cache *s,
						    struct slab *slab)
{
	unsigned long objext_offset;

	objext_offset = s->size * slab->objects;
	objext_offset = ALIGN(objext_offset, sizeof(struct slabobj_ext));
	return objext_offset;
}

static inline bool obj_exts_fit_within_slab_leftover(struct kmem_cache *s,
						     struct slab *slab)
{
	unsigned long objext_offset = obj_exts_offset_in_slab(s, slab);
	unsigned long objext_size = obj_exts_size_in_slab(slab);

	return objext_offset + objext_size <= slab_size(slab);
}

static inline bool obj_exts_in_slab(struct kmem_cache *s, struct slab *slab)
{
	unsigned long obj_exts;
	unsigned long start;
	unsigned long end;

	obj_exts = slab_obj_exts(slab);
	if (!obj_exts)
		return false;

	start = (unsigned long)slab_address(slab);
	end = start + slab_size(slab);
	return (obj_exts >= start) && (obj_exts < end);
}
#else
static inline bool need_slab_obj_exts(struct kmem_cache *s)
{
	return false;
}

static inline unsigned int obj_exts_size_in_slab(struct slab *slab)
{
	return 0;
}

static inline unsigned long obj_exts_offset_in_slab(struct kmem_cache *s,
						    struct slab *slab)
{
	return 0;
}

static inline bool obj_exts_fit_within_slab_leftover(struct kmem_cache *s,
						     struct slab *slab)
{
	return false;
}

static inline bool obj_exts_in_slab(struct kmem_cache *s, struct slab *slab)
{
	return false;
}

#endif

#if defined(CONFIG_SLAB_OBJ_EXT) && defined(CONFIG_64BIT)
static bool obj_exts_in_object(struct kmem_cache *s, struct slab *slab)
{
	/*
	 * Note we cannot rely on the SLAB_OBJ_EXT_IN_OBJ flag here and need to
	 * check the stride. A cache can have SLAB_OBJ_EXT_IN_OBJ set, but
	 * allocations within_slab_leftover are preferred. And those may be
	 * possible or not depending on the particular slab's size.
	 */
	return obj_exts_in_slab(s, slab) &&
	       (slab_get_stride(slab) == s->size);
}

static unsigned int obj_exts_offset_in_object(struct kmem_cache *s)
{
	unsigned int offset = get_info_end(s);

	if (kmem_cache_debug_flags(s, SLAB_STORE_USER))
		offset += sizeof(struct track) * 2;

	if (slub_debug_orig_size(s))
		offset += sizeof(unsigned long);

	offset += kasan_metadata_size(s, false);

	return offset;
}
#else
static inline bool obj_exts_in_object(struct kmem_cache *s, struct slab *slab)
{
	return false;
}

static inline unsigned int obj_exts_offset_in_object(struct kmem_cache *s)
{
	return 0;
}
#endif

#ifdef CONFIG_SLUB_DEBUG

/*
 * For debugging context when we want to check if the struct slab pointer
 * appears to be valid.
 */
static inline bool validate_slab_ptr(struct slab *slab)
{
	return PageSlab(slab_page(slab));
}

static unsigned long object_map[BITS_TO_LONGS(MAX_OBJS_PER_PAGE)];
static DEFINE_SPINLOCK(object_map_lock);

static void __fill_map(unsigned long *obj_map, struct kmem_cache *s,
		       struct slab *slab)
{
	void *addr = slab_address(slab);
	void *p;

	bitmap_zero(obj_map, slab->objects);

	for (p = slab->freelist; p; p = get_freepointer(s, p))
		set_bit(__obj_to_index(s, addr, p), obj_map);
}

#if IS_ENABLED(CONFIG_KUNIT)
static bool slab_add_kunit_errors(void)
{
	struct kunit_resource *resource;

	if (!kunit_get_current_test())
		return false;

	resource = kunit_find_named_resource(current->kunit_test, "slab_errors");
	if (!resource)
		return false;

	(*(int *)resource->data)++;
	kunit_put_resource(resource);
	return true;
}

bool slab_in_kunit_test(void)
{
	struct kunit_resource *resource;

	if (!kunit_get_current_test())
		return false;

	resource = kunit_find_named_resource(current->kunit_test, "slab_errors");
	if (!resource)
		return false;

	kunit_put_resource(resource);
	return true;
}
#else
static inline bool slab_add_kunit_errors(void) { return false; }
#endif

static inline unsigned int size_from_object(struct kmem_cache *s)
{
	if (s->flags & SLAB_RED_ZONE)
		return s->size - s->red_left_pad;

	return s->size;
}

static inline void *restore_red_left(struct kmem_cache *s, void *p)
{
	if (s->flags & SLAB_RED_ZONE)
		p -= s->red_left_pad;

	return p;
}

/*
 * Debug settings:
 */
#if defined(CONFIG_SLUB_DEBUG_ON)
static slab_flags_t slub_debug = DEBUG_DEFAULT_FLAGS;
#else
static slab_flags_t slub_debug;
#endif

static const char *slub_debug_string __ro_after_init;
static int disable_higher_order_debug;

/*
 * Object debugging
 */

/* Verify that a pointer has an address that is valid within a slab page */
static inline int check_valid_pointer(struct kmem_cache *s,
				struct slab *slab, void *object)
{
	void *base;

	if (!object)
		return 1;

	base = slab_address(slab);
	object = kasan_reset_tag(object);
	object = restore_red_left(s, object);
	if (object < base || object >= base + slab->objects * s->size ||
		(object - base) % s->size) {
		return 0;
	}

	return 1;
}

static void print_section(char *level, char *text, u8 *addr,
			  unsigned int length)
{
	metadata_access_enable();
	print_hex_dump(level, text, DUMP_PREFIX_ADDRESS,
			16, 1, kasan_reset_tag((void *)addr), length, 1);
	metadata_access_disable();
}

static struct track *get_track(struct kmem_cache *s, void *object,
	enum track_item alloc)
{
	struct track *p;

	p = object + get_info_end(s);

	return kasan_reset_tag(p + alloc);
}

#ifdef CONFIG_STACKDEPOT
static noinline depot_stack_handle_t set_track_prepare(gfp_t gfp_flags)
{
	depot_stack_handle_t handle;
	unsigned long entries[TRACK_ADDRS_COUNT];
	unsigned int nr_entries;

	nr_entries = stack_trace_save(entries, ARRAY_SIZE(entries), 3);
	handle = stack_depot_save(entries, nr_entries, gfp_flags);

	return handle;
}
#else
static inline depot_stack_handle_t set_track_prepare(gfp_t gfp_flags)
{
	return 0;
}
#endif

static void set_track_update(struct kmem_cache *s, void *object,
			     enum track_item alloc, unsigned long addr,
			     depot_stack_handle_t handle)
{
	struct track *p = get_track(s, object, alloc);

#ifdef CONFIG_STACKDEPOT
	p->handle = handle;
#endif
	p->addr = addr;
	p->cpu = raw_smp_processor_id();
	p->pid = current->pid;
	p->when = jiffies;
}

static __always_inline void set_track(struct kmem_cache *s, void *object,
				      enum track_item alloc, unsigned long addr, gfp_t gfp_flags)
{
	depot_stack_handle_t handle = set_track_prepare(gfp_flags);

	set_track_update(s, object, alloc, addr, handle);
}

static void init_tracking(struct kmem_cache *s, void *object)
{
	struct track *p;

	if (!(s->flags & SLAB_STORE_USER))
		return;

	p = get_track(s, object, TRACK_ALLOC);
	memset(p, 0, 2*sizeof(struct track));
}

static void print_track(const char *s, struct track *t, unsigned long pr_time)
{
	depot_stack_handle_t handle __maybe_unused;

	if (!t->addr)
		return;

	pr_err("%s in %pS age=%lu cpu=%u pid=%d\n",
	       s, (void *)t->addr, pr_time - t->when, t->cpu, t->pid);
#ifdef CONFIG_STACKDEPOT
	handle = READ_ONCE(t->handle);
	if (handle)
		stack_depot_print(handle);
	else
		pr_err("object allocation/free stack trace missing\n");
#endif
}

void print_tracking(struct kmem_cache *s, void *object)
{
	unsigned long pr_time = jiffies;
	if (!(s->flags & SLAB_STORE_USER))
		return;

	print_track("Allocated", get_track(s, object, TRACK_ALLOC), pr_time);
	print_track("Freed", get_track(s, object, TRACK_FREE), pr_time);
}

static void print_slab_info(const struct slab *slab)
{
	pr_err("Slab 0x%p objects=%u used=%u fp=0x%p flags=%pGp\n",
	       slab, slab->objects, slab->inuse, slab->freelist,
	       &slab->flags.f);
}

void skip_orig_size_check(struct kmem_cache *s, const void *object)
{
	set_orig_size(s, (void *)object, s->object_size);
}

static void __slab_bug(struct kmem_cache *s, const char *fmt, va_list argsp)
{
	struct va_format vaf;
	va_list args;

	va_copy(args, argsp);
	vaf.fmt = fmt;
	vaf.va = &args;
	pr_err("=============================================================================\n");
	pr_err("BUG %s (%s): %pV\n", s ? s->name : "<unknown>", print_tainted(), &vaf);
	pr_err("-----------------------------------------------------------------------------\n\n");
	va_end(args);
}

static void slab_bug(struct kmem_cache *s, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	__slab_bug(s, fmt, args);
	va_end(args);
}

__printf(2, 3)
static void slab_fix(struct kmem_cache *s, const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;

	if (slab_add_kunit_errors())
		return;

	va_start(args, fmt);
	vaf.fmt = fmt;
	vaf.va = &args;
	pr_err("FIX %s: %pV\n", s->name, &vaf);
	va_end(args);
}

static void print_trailer(struct kmem_cache *s, struct slab *slab, u8 *p)
{
	unsigned int off;	/* Offset of last byte */
	u8 *addr = slab_address(slab);

	print_tracking(s, p);

	print_slab_info(slab);

	pr_err("Object 0x%p @offset=%tu fp=0x%p\n\n",
	       p, p - addr, get_freepointer(s, p));

	if (s->flags & SLAB_RED_ZONE)
		print_section(KERN_ERR, "Redzone  ", p - s->red_left_pad,
			      s->red_left_pad);
	else if (p > addr + 16)
		print_section(KERN_ERR, "Bytes b4 ", p - 16, 16);

	print_section(KERN_ERR,         "Object   ", p,
		      min_t(unsigned int, s->object_size, PAGE_SIZE));
	if (s->flags & SLAB_RED_ZONE)
		print_section(KERN_ERR, "Redzone  ", p + s->object_size,
			s->inuse - s->object_size);

	off = get_info_end(s);

	if (s->flags & SLAB_STORE_USER)
		off += 2 * sizeof(struct track);

	if (slub_debug_orig_size(s))
		off += sizeof(unsigned long);

	off += kasan_metadata_size(s, false);

	if (obj_exts_in_object(s, slab))
		off += sizeof(struct slabobj_ext);

	if (off != size_from_object(s))
		/* Beginning of the filler is the free pointer */
		print_section(KERN_ERR, "Padding  ", p + off,
			      size_from_object(s) - off);
}

static void object_err(struct kmem_cache *s, struct slab *slab,
			u8 *object, const char *reason)
{
	if (slab_add_kunit_errors())
		return;

	slab_bug(s, reason);
	if (!object || !check_valid_pointer(s, slab, object)) {
		print_slab_info(slab);
		pr_err("Invalid pointer 0x%p\n", object);
	} else {
		print_trailer(s, slab, object);
	}
	add_taint(TAINT_BAD_PAGE, LOCKDEP_NOW_UNRELIABLE);

	WARN_ON(1);
}

static void __slab_err(struct slab *slab)
{
	if (slab_in_kunit_test())
		return;

	print_slab_info(slab);
	add_taint(TAINT_BAD_PAGE, LOCKDEP_NOW_UNRELIABLE);

	WARN_ON(1);
}

static __printf(3, 4) void slab_err(struct kmem_cache *s, struct slab *slab,
			const char *fmt, ...)
{
	va_list args;

	if (slab_add_kunit_errors())
		return;

	va_start(args, fmt);
	__slab_bug(s, fmt, args);
	va_end(args);

	__slab_err(slab);
}

static void init_object(struct kmem_cache *s, void *object, u8 val)
{
	u8 *p = kasan_reset_tag(object);
	unsigned int poison_size = s->object_size;

	if (s->flags & SLAB_RED_ZONE) {
		/*
		 * Here and below, avoid overwriting the KMSAN shadow. Keeping
		 * the shadow makes it possible to distinguish uninit-value
		 * from use-after-free.
		 */
		memset_no_sanitize_memory(p - s->red_left_pad, val,
					  s->red_left_pad);

		if (slub_debug_orig_size(s) && val == SLUB_RED_ACTIVE) {
			/*
			 * Redzone the extra allocated space by kmalloc than
			 * requested, and the poison size will be limited to
			 * the original request size accordingly.
			 */
			poison_size = get_orig_size(s, object);
		}
	}

	if (s->flags & __OBJECT_POISON) {
		memset_no_sanitize_memory(p, POISON_FREE, poison_size - 1);
		memset_no_sanitize_memory(p + poison_size - 1, POISON_END, 1);
	}

	if (s->flags & SLAB_RED_ZONE)
		memset_no_sanitize_memory(p + poison_size, val,
					  s->inuse - poison_size);
}

static void restore_bytes(struct kmem_cache *s, const char *message, u8 data,
						void *from, void *to)
{
	slab_fix(s, "Restoring %s 0x%p-0x%p=0x%x", message, from, to - 1, data);
	memset(from, data, to - from);
}

#ifdef CONFIG_KMSAN
#define pad_check_attributes noinline __no_kmsan_checks
#else
#define pad_check_attributes
#endif

static pad_check_attributes int
check_bytes_and_report(struct kmem_cache *s, struct slab *slab,
		       u8 *object, const char *what, u8 *start, unsigned int value,
		       unsigned int bytes, bool slab_obj_print)
{
	u8 *fault;
	u8 *end;
	u8 *addr = slab_address(slab);

	metadata_access_enable();
	fault = memchr_inv(kasan_reset_tag(start), value, bytes);
	metadata_access_disable();
	if (!fault)
		return 1;

	end = start + bytes;
	while (end > fault && end[-1] == value)
		end--;

	if (slab_add_kunit_errors())
		goto skip_bug_print;

	pr_err("[%s overwritten] 0x%p-0x%p @offset=%tu. First byte 0x%x instead of 0x%x\n",
	       what, fault, end - 1, fault - addr, fault[0], value);

	if (slab_obj_print)
		object_err(s, slab, object, "Object corrupt");

skip_bug_print:
	restore_bytes(s, what, value, fault, end);
	return 0;
}

/*
 * Object field layout:
 *
 * [Left redzone padding] (if SLAB_RED_ZONE)
 *   - Field size: s->red_left_pad
 *   - Immediately precedes each object when SLAB_RED_ZONE is set.
 *   - Filled with 0xbb (SLUB_RED_INACTIVE) for inactive objects and
 *     0xcc (SLUB_RED_ACTIVE) for objects in use when SLAB_RED_ZONE.
 *
 * [Object bytes] (object address starts here)
 *   - Field size: s->object_size
 *   - Object payload bytes.
 *   - If the freepointer may overlap the object, it is stored inside
 *     the object (typically near the middle).
 *   - Poisoning uses 0x6b (POISON_FREE) and the last byte is
 *     0xa5 (POISON_END) when __OBJECT_POISON is enabled.
 *
 * [Word-align padding] (right redzone when SLAB_RED_ZONE is set)
 *   - Field size: s->inuse - s->object_size
 *   - If redzoning is enabled and ALIGN(size, sizeof(void *)) adds no
 *     padding, explicitly extend by one word so the right redzone is
 *     non-empty.
 *   - Filled with 0xbb (SLUB_RED_INACTIVE) for inactive objects and
 *     0xcc (SLUB_RED_ACTIVE) for objects in use when SLAB_RED_ZONE.
 *
 * [Metadata starts at object + s->inuse]
 *   - A. freelist pointer (if freeptr_outside_object)
 *   - B. alloc tracking (SLAB_STORE_USER)
 *   - C. free tracking (SLAB_STORE_USER)
 *   - D. original request size (SLAB_KMALLOC && SLAB_STORE_USER)
 *   - E. KASAN metadata (if enabled)
 *
 * [Mandatory padding] (if CONFIG_SLUB_DEBUG && SLAB_RED_ZONE)
 *   - One mandatory debug word to guarantee a minimum poisoned gap
 *     between metadata and the next object, independent of alignment.
 *   - Filled with 0x5a (POISON_INUSE) when SLAB_POISON is set.
 * [Final alignment padding]
 *   - Bytes added by ALIGN(size, s->align) to reach s->size.
 *   - When the padding is large enough, it can be used to store
 *     struct slabobj_ext for accounting metadata (obj_exts_in_object()).
 *   - The remaining bytes (if any) are filled with 0x5a (POISON_INUSE)
 *     when SLAB_POISON is set.
 *
 * Notes:
 * - Redzones are filled by init_object() with SLUB_RED_ACTIVE/INACTIVE.
 * - Object contents are poisoned with POISON_FREE/END when __OBJECT_POISON.
 * - The trailing padding is pre-filled with POISON_INUSE by
 *   setup_slab_debug() when SLAB_POISON is set, and is validated by
 *   check_pad_bytes().
 * - The first object pointer is slab_address(slab) +
 *   (s->red_left_pad if redzoning); subsequent objects are reached by
 *   adding s->size each time.
 *
 * If a slab cache flag relies on specific metadata to exist at a fixed
 * offset, the flag must be included in SLAB_NEVER_MERGE to prevent merging.
 * Otherwise, the cache would misbehave as s->object_size and s->inuse are
 * adjusted during cache merging (see __kmem_cache_alias()).
 */
static int check_pad_bytes(struct kmem_cache *s, struct slab *slab, u8 *p)
{
	unsigned long off = get_info_end(s);	/* The end of info */

	if (s->flags & SLAB_STORE_USER) {
		/* We also have user information there */
		off += 2 * sizeof(struct track);

		if (s->flags & SLAB_KMALLOC)
			off += sizeof(unsigned long);
	}

	off += kasan_metadata_size(s, false);

	if (obj_exts_in_object(s, slab))
		off += sizeof(struct slabobj_ext);

	if (size_from_object(s) == off)
		return 1;

	return check_bytes_and_report(s, slab, p, "Object padding",
			p + off, POISON_INUSE, size_from_object(s) - off, true);
}

/* Check the pad bytes at the end of a slab page */
static pad_check_attributes void
slab_pad_check(struct kmem_cache *s, struct slab *slab)
{
	u8 *start;
	u8 *fault;
	u8 *end;
	u8 *pad;
	int length;
	int remainder;

	if (!(s->flags & SLAB_POISON))
		return;

	start = slab_address(slab);
	length = slab_size(slab);
	end = start + length;

	if (obj_exts_in_slab(s, slab) && !obj_exts_in_object(s, slab)) {
		remainder = length;
		remainder -= obj_exts_offset_in_slab(s, slab);
		remainder -= obj_exts_size_in_slab(slab);
	} else {
		remainder = length % s->size;
	}

	if (!remainder)
		return;

	pad = end - remainder;
	metadata_access_enable();
	fault = memchr_inv(kasan_reset_tag(pad), POISON_INUSE, remainder);
	metadata_access_disable();
	if (!fault)
		return;
	while (end > fault && end[-1] == POISON_INUSE)
		end--;

	slab_bug(s, "Padding overwritten. 0x%p-0x%p @offset=%tu",
		 fault, end - 1, fault - start);
	print_section(KERN_ERR, "Padding ", pad, remainder);
	__slab_err(slab);

	restore_bytes(s, "slab padding", POISON_INUSE, fault, end);
}

static int check_object(struct kmem_cache *s, struct slab *slab,
					void *object, u8 val)
{
	u8 *p = object;
	u8 *endobject = object + s->object_size;
	unsigned int orig_size, kasan_meta_size;
	int ret = 1;

	if (s->flags & SLAB_RED_ZONE) {
		if (!check_bytes_and_report(s, slab, object, "Left Redzone",
			object - s->red_left_pad, val, s->red_left_pad, ret))
			ret = 0;

		if (!check_bytes_and_report(s, slab, object, "Right Redzone",
			endobject, val, s->inuse - s->object_size, ret))
			ret = 0;

		if (slub_debug_orig_size(s) && val == SLUB_RED_ACTIVE) {
			orig_size = get_orig_size(s, object);

			if (s->object_size > orig_size  &&
				!check_bytes_and_report(s, slab, object,
					"kmalloc Redzone", p + orig_size,
					val, s->object_size - orig_size, ret)) {
				ret = 0;
			}
		}
	} else {
		if ((s->flags & SLAB_POISON) && s->object_size < s->inuse) {
			if (!check_bytes_and_report(s, slab, p, "Alignment padding",
				endobject, POISON_INUSE,
				s->inuse - s->object_size, ret))
				ret = 0;
		}
	}

	if (s->flags & SLAB_POISON) {
		if (val != SLUB_RED_ACTIVE && (s->flags & __OBJECT_POISON)) {
			/*
			 * KASAN can save its free meta data inside of the
			 * object at offset 0. Thus, skip checking the part of
			 * the redzone that overlaps with the meta data.
			 */
			kasan_meta_size = kasan_metadata_size(s, true);
			if (kasan_meta_size < s->object_size - 1 &&
			    !check_bytes_and_report(s, slab, p, "Poison",
					p + kasan_meta_size, POISON_FREE,
					s->object_size - kasan_meta_size - 1, ret))
				ret = 0;
			if (kasan_meta_size < s->object_size &&
			    !check_bytes_and_report(s, slab, p, "End Poison",
					p + s->object_size - 1, POISON_END, 1, ret))
				ret = 0;
		}
		/*
		 * check_pad_bytes cleans up on its own.
		 */
		if (!check_pad_bytes(s, slab, p))
			ret = 0;
	}

	/*
	 * Cannot check freepointer while object is allocated if
	 * object and freepointer overlap.
	 */
	if ((freeptr_outside_object(s) || val != SLUB_RED_ACTIVE) &&
	    !check_valid_pointer(s, slab, get_freepointer(s, p))) {
		object_err(s, slab, p, "Freepointer corrupt");
		/*
		 * No choice but to zap it and thus lose the remainder
		 * of the free objects in this slab. May cause
		 * another error because the object count is now wrong.
		 */
		set_freepointer(s, p, NULL);
		ret = 0;
	}

	return ret;
}

/*
 * Checks if the slab state looks sane. Assumes the struct slab pointer
 * was either obtained in a way that ensures it's valid, or validated
 * by validate_slab_ptr()
 */
static int check_slab(struct kmem_cache *s, struct slab *slab)
{
	int maxobj;

	maxobj = order_objects(slab_order(slab), s->size);
	if (slab->objects > maxobj) {
		slab_err(s, slab, "objects %u > max %u",
			slab->objects, maxobj);
		return 0;
	}
	if (slab->inuse > slab->objects) {
		slab_err(s, slab, "inuse %u > max %u",
			slab->inuse, slab->objects);
		return 0;
	}
	if (slab->frozen) {
		slab_err(s, slab, "Slab disabled since SLUB metadata consistency check failed");
		return 0;
	}

	/* Slab_pad_check fixes things up after itself */
	slab_pad_check(s, slab);
	return 1;
}

/*
 * Determine if a certain object in a slab is on the freelist. Must hold the
 * slab lock to guarantee that the chains are in a consistent state.
 */
static bool on_freelist(struct kmem_cache *s, struct slab *slab, void *search)
{
	int nr = 0;
	void *fp;
	void *object = NULL;
	int max_objects;

	fp = slab->freelist;
	while (fp && nr <= slab->objects) {
		if (fp == search)
			return true;
		if (!check_valid_pointer(s, slab, fp)) {
			if (object) {
				object_err(s, slab, object,
					"Freechain corrupt");
				set_freepointer(s, object, NULL);
				break;
			} else {
				slab_err(s, slab, "Freepointer corrupt");
				slab->freelist = NULL;
				slab->inuse = slab->objects;
				slab_fix(s, "Freelist cleared");
				return false;
			}
		}
		object = fp;
		fp = get_freepointer(s, object);
		nr++;
	}

	if (nr > slab->objects) {
		slab_err(s, slab, "Freelist cycle detected");
		slab->freelist = NULL;
		slab->inuse = slab->objects;
		slab_fix(s, "Freelist cleared");
		return false;
	}

	max_objects = order_objects(slab_order(slab), s->size);
	if (max_objects > MAX_OBJS_PER_PAGE)
		max_objects = MAX_OBJS_PER_PAGE;

	if (slab->objects != max_objects) {
		slab_err(s, slab, "Wrong number of objects. Found %d but should be %d",
			 slab->objects, max_objects);
		slab->objects = max_objects;
		slab_fix(s, "Number of objects adjusted");
	}
	if (slab->inuse != slab->objects - nr) {
		slab_err(s, slab, "Wrong object count. Counter is %d but counted were %d",
			 slab->inuse, slab->objects - nr);
		slab->inuse = slab->objects - nr;
		slab_fix(s, "Object count adjusted");
	}
	return search == NULL;
}

static void trace(struct kmem_cache *s, struct slab *slab, void *object,
								int alloc)
{
	if (s->flags & SLAB_TRACE) {
		pr_info("TRACE %s %s 0x%p inuse=%d fp=0x%p\n",
			s->name,
			alloc ? "alloc" : "free",
			object, slab->inuse,
			slab->freelist);

		if (!alloc)
			print_section(KERN_INFO, "Object ", (void *)object,
					s->object_size);

		dump_stack();
	}
}

/*
 * Tracking of fully allocated slabs for debugging purposes.
 */
static void add_full(struct kmem_cache *s,
	struct kmem_cache_node *n, struct slab *slab)
{
	if (!(s->flags & SLAB_STORE_USER))
		return;

	lockdep_assert_held(&n->list_lock);
	list_add(&slab->slab_list, &n->full);
}

static void remove_full(struct kmem_cache *s, struct kmem_cache_node *n, struct slab *slab)
{
	if (!(s->flags & SLAB_STORE_USER))
		return;

	lockdep_assert_held(&n->list_lock);
	list_del(&slab->slab_list);
}

static inline unsigned long node_nr_slabs(struct kmem_cache_node *n)
{
	return atomic_long_read(&n->nr_slabs);
}

static inline void inc_slabs_node(struct kmem_cache *s, int node, int objects)
{
	struct kmem_cache_node *n = get_node(s, node);

	atomic_long_inc(&n->nr_slabs);
	atomic_long_add(objects, &n->total_objects);
}
static inline void dec_slabs_node(struct kmem_cache *s, int node, int objects)
{
	struct kmem_cache_node *n = get_node(s, node);

	atomic_long_dec(&n->nr_slabs);
	atomic_long_sub(objects, &n->total_objects);
}

/* Object debug checks for alloc/free paths */
static void setup_object_debug(struct kmem_cache *s, void *object)
{
	if (!kmem_cache_debug_flags(s, SLAB_STORE_USER|SLAB_RED_ZONE|__OBJECT_POISON))
		return;

	init_object(s, object, SLUB_RED_INACTIVE);
	init_tracking(s, object);
}

static
void setup_slab_debug(struct kmem_cache *s, struct slab *slab, void *addr)
{
	if (!kmem_cache_debug_flags(s, SLAB_POISON))
		return;

	metadata_access_enable();
	memset(kasan_reset_tag(addr), POISON_INUSE, slab_size(slab));
	metadata_access_disable();
}

static inline int alloc_consistency_checks(struct kmem_cache *s,
					struct slab *slab, void *object)
{
	if (!check_slab(s, slab))
		return 0;

	if (!check_valid_pointer(s, slab, object)) {
		object_err(s, slab, object, "Freelist Pointer check fails");
		return 0;
	}

	if (!check_object(s, slab, object, SLUB_RED_INACTIVE))
		return 0;

	return 1;
}

static noinline bool alloc_debug_processing(struct kmem_cache *s,
			struct slab *slab, void *object, int orig_size)
{
	if (s->flags & SLAB_CONSISTENCY_CHECKS) {
		if (!alloc_consistency_checks(s, slab, object))
			goto bad;
	}

	/* Success. Perform special debug activities for allocs */
	trace(s, slab, object, 1);
	set_orig_size(s, object, orig_size);
	init_object(s, object, SLUB_RED_ACTIVE);
	return true;

bad:
	/*
	 * Let's do the best we can to avoid issues in the future. Marking all
	 * objects as used avoids touching the remaining objects.
	 */
	slab_fix(s, "Marking all objects used");
	slab->inuse = slab->objects;
	slab->freelist = NULL;
	slab->frozen = 1; /* mark consistency-failed slab as frozen */

	return false;
}

static inline int free_consistency_checks(struct kmem_cache *s,
		struct slab *slab, void *object, unsigned long addr)
{
	if (!check_valid_pointer(s, slab, object)) {
		slab_err(s, slab, "Invalid object pointer 0x%p", object);
		return 0;
	}

	if (on_freelist(s, slab, object)) {
		object_err(s, slab, object, "Object already free");
		return 0;
	}

	if (!check_object(s, slab, object, SLUB_RED_ACTIVE))
		return 0;

	if (unlikely(s != slab->slab_cache)) {
		if (!slab->slab_cache) {
			slab_err(NULL, slab, "No slab cache for object 0x%p",
				 object);
		} else {
			object_err(s, slab, object,
				   "page slab pointer corrupt.");
		}
		return 0;
	}
	return 1;
}

/*
 * Parse a block of slab_debug options. Blocks are delimited by ';'
 *
 * @str:    start of block
 * @flags:  returns parsed flags, or DEBUG_DEFAULT_FLAGS if none specified
 * @slabs:  return start of list of slabs, or NULL when there's no list
 * @init:   assume this is initial parsing and not per-kmem-create parsing
 *
 * returns the start of next block if there's any, or NULL
 */
static const char *
parse_slub_debug_flags(const char *str, slab_flags_t *flags, const char **slabs, bool init)
{
	bool higher_order_disable = false;

	/* Skip any completely empty blocks */
	while (*str && *str == ';')
		str++;

	if (*str == ',') {
		/*
		 * No options but restriction on slabs. This means full
		 * debugging for slabs matching a pattern.
		 */
		*flags = DEBUG_DEFAULT_FLAGS;
		goto check_slabs;
	}
	*flags = 0;

	/* Determine which debug features should be switched on */
	for (; *str && *str != ',' && *str != ';'; str++) {
		switch (tolower(*str)) {
		case '-':
			*flags = 0;
			break;
		case 'f':
			*flags |= SLAB_CONSISTENCY_CHECKS;
			break;
		case 'z':
			*flags |= SLAB_RED_ZONE;
			break;
		case 'p':
			*flags |= SLAB_POISON;
			break;
		case 'u':
			*flags |= SLAB_STORE_USER;
			break;
		case 't':
			*flags |= SLAB_TRACE;
			break;
		case 'a':
			*flags |= SLAB_FAILSLAB;
			break;
		case 'o':
			/*
			 * Avoid enabling debugging on caches if its minimum
			 * order would increase as a result.
			 */
			higher_order_disable = true;
			break;
		default:
			if (init)
				pr_err("slab_debug option '%c' unknown. skipped\n", *str);
		}
	}
check_slabs:
	if (*str == ',')
		*slabs = ++str;
	else
		*slabs = NULL;

	/* Skip over the slab list */
	while (*str && *str != ';')
		str++;

	/* Skip any completely empty blocks */
	while (*str && *str == ';')
		str++;

	if (init && higher_order_disable)
		disable_higher_order_debug = 1;

	if (*str)
		return str;
	else
		return NULL;
}

static int __init setup_slub_debug(const char *str, const struct kernel_param *kp)
{
	slab_flags_t flags;
	slab_flags_t global_flags;
	const char *saved_str;
	const char *slab_list;
	bool global_slub_debug_changed = false;
	bool slab_list_specified = false;

	global_flags = DEBUG_DEFAULT_FLAGS;
	if (!str || !*str)
		/*
		 * No options specified. Switch on full debugging.
		 */
		goto out;

	saved_str = str;
	while (str) {
		str = parse_slub_debug_flags(str, &flags, &slab_list, true);

		if (!slab_list) {
			global_flags = flags;
			global_slub_debug_changed = true;
		} else {
			slab_list_specified = true;
			if (flags & SLAB_STORE_USER)
				stack_depot_request_early_init();
		}
	}

	/*
	 * For backwards compatibility, a single list of flags with list of
	 * slabs means debugging is only changed for those slabs, so the global
	 * slab_debug should be unchanged (0 or DEBUG_DEFAULT_FLAGS, depending
	 * on CONFIG_SLUB_DEBUG_ON). We can extended that to multiple lists as
	 * long as there is no option specifying flags without a slab list.
	 */
	if (slab_list_specified) {
		if (!global_slub_debug_changed)
			global_flags = slub_debug;
		slub_debug_string = saved_str;
	}
out:
	slub_debug = global_flags;
	if (slub_debug & SLAB_STORE_USER)
		stack_depot_request_early_init();
	if (slub_debug != 0 || slub_debug_string)
		static_branch_enable(&slub_debug_enabled);
	else
		static_branch_disable(&slub_debug_enabled);
	if ((static_branch_unlikely(&init_on_alloc) ||
	     static_branch_unlikely(&init_on_free)) &&
	    (slub_debug & SLAB_POISON))
		pr_info("mem auto-init: SLAB_POISON will take precedence over init_on_alloc/init_on_free\n");
	return 0;
}

static const struct kernel_param_ops param_ops_slab_debug __initconst = {
	.flags = KERNEL_PARAM_OPS_FL_NOARG,
	.set = setup_slub_debug,
};
__core_param_cb(slab_debug, &param_ops_slab_debug, NULL, 0);
__core_param_cb(slub_debug, &param_ops_slab_debug, NULL, 0);

/*
 * kmem_cache_flags - apply debugging options to the cache
 * @flags:		flags to set
 * @name:		name of the cache
 *
 * Debug option(s) are applied to @flags. In addition to the debug
 * option(s), if a slab name (or multiple) is specified i.e.
 * slab_debug=<Debug-Options>,<slab name1>,<slab name2> ...
 * then only the select slabs will receive the debug option(s).
 */
slab_flags_t kmem_cache_flags(slab_flags_t flags, const char *name)
{
	const char *iter;
	size_t len;
	const char *next_block;
	slab_flags_t block_flags;
	slab_flags_t slub_debug_local = slub_debug;

	if (flags & SLAB_NO_USER_FLAGS)
		return flags;

	/*
	 * If the slab cache is for debugging (e.g. kmemleak) then
	 * don't store user (stack trace) information by default,
	 * but let the user enable it via the command line below.
	 */
	if (flags & SLAB_NOLEAKTRACE)
		slub_debug_local &= ~SLAB_STORE_USER;

	len = strlen(name);
	next_block = slub_debug_string;
	/* Go through all blocks of debug options, see if any matches our slab's name */
	while (next_block) {
		next_block = parse_slub_debug_flags(next_block, &block_flags, &iter, false);
		if (!iter)
			continue;
		/* Found a block that has a slab list, search it */
		while (*iter) {
			const char *end, *glob;
			size_t cmplen;

			end = strchrnul(iter, ',');
			if (next_block && next_block < end)
				end = next_block - 1;

			glob = strnchr(iter, end - iter, '*');
			if (glob)
				cmplen = glob - iter;
			else
				cmplen = max_t(size_t, len, (end - iter));

			if (!strncmp(name, iter, cmplen)) {
				flags |= block_flags;
				return flags;
			}

			if (!*end || *end == ';')
				break;
			iter = end + 1;
		}
	}

	return flags | slub_debug_local;
}
#else /* !CONFIG_SLUB_DEBUG */
static inline void setup_object_debug(struct kmem_cache *s, void *object) {}
static inline
void setup_slab_debug(struct kmem_cache *s, struct slab *slab, void *addr) {}

static inline bool alloc_debug_processing(struct kmem_cache *s,
	struct slab *slab, void *object, int orig_size) { return true; }

static inline bool free_debug_processing(struct kmem_cache *s,
	struct slab *slab, void *head, void *tail, int *bulk_cnt,
	unsigned long addr, depot_stack_handle_t handle) { return true; }

static inline void slab_pad_check(struct kmem_cache *s, struct slab *slab) {}
static inline int check_object(struct kmem_cache *s, struct slab *slab,
			void *object, u8 val) { return 1; }
static inline depot_stack_handle_t set_track_prepare(gfp_t gfp_flags) { return 0; }
static inline void set_track(struct kmem_cache *s, void *object,
			     enum track_item alloc, unsigned long addr, gfp_t gfp_flags) {}
static inline void add_full(struct kmem_cache *s, struct kmem_cache_node *n,
					struct slab *slab) {}
static inline void remove_full(struct kmem_cache *s, struct kmem_cache_node *n,
					struct slab *slab) {}
slab_flags_t kmem_cache_flags(slab_flags_t flags, const char *name)
{
	return flags;
}
#define slub_debug 0

#define disable_higher_order_debug 0

static inline unsigned long node_nr_slabs(struct kmem_cache_node *n)
							{ return 0; }
static inline void inc_slabs_node(struct kmem_cache *s, int node,
							int objects) {}
static inline void dec_slabs_node(struct kmem_cache *s, int node,
							int objects) {}
#endif /* CONFIG_SLUB_DEBUG */

/*
 * The allocated objcg pointers array is not accounted directly.
 * Moreover, it should not come from DMA buffer and is not readily
 * reclaimable. So those GFP bits should be masked off.
 */
#define OBJCGS_CLEAR_MASK	(__GFP_DMA | __GFP_RECLAIMABLE | \
				__GFP_ACCOUNT | __GFP_NOFAIL)

#ifdef CONFIG_SLAB_OBJ_EXT

#ifdef CONFIG_MEM_ALLOC_PROFILING_DEBUG

static inline void mark_objexts_empty(struct slabobj_ext *obj_exts)
{
	struct slab *obj_exts_slab;
	unsigned long slab_exts;

	obj_exts_slab = virt_to_slab(obj_exts);
	slab_exts = slab_obj_exts(obj_exts_slab);
	if (slab_exts) {
		get_slab_obj_exts(slab_exts);
		unsigned int offs = obj_to_index(obj_exts_slab->slab_cache,
						 obj_exts_slab, obj_exts);
		struct slabobj_ext *ext = slab_obj_ext(obj_exts_slab,
						       slab_exts, offs);

		if (unlikely(is_codetag_empty(&ext->ref))) {
			put_slab_obj_exts(slab_exts);
			return;
		}

		/* codetag should be NULL here */
		WARN_ON(ext->ref.ct);
		set_codetag_empty(&ext->ref);
		put_slab_obj_exts(slab_exts);
	}
}

static inline bool mark_failed_objexts_alloc(struct slab *slab)
{
	return cmpxchg(&slab->obj_exts, 0, OBJEXTS_ALLOC_FAIL) == 0;
}

static inline void handle_failed_objexts_alloc(unsigned long obj_exts,
			struct slabobj_ext *vec, unsigned int objects)
{
	/*
	 * If vector previously failed to allocate then we have live
	 * objects with no tag reference. Mark all references in this
	 * vector as empty to avoid warnings later on.
	 */
	if (obj_exts == OBJEXTS_ALLOC_FAIL) {
		unsigned int i;

		for (i = 0; i < objects; i++)
			set_codetag_empty(&vec[i].ref);
	}
}

#else /* CONFIG_MEM_ALLOC_PROFILING_DEBUG */

static inline void mark_objexts_empty(struct slabobj_ext *obj_exts) {}
static inline bool mark_failed_objexts_alloc(struct slab *slab) { return false; }
static inline void handle_failed_objexts_alloc(unsigned long obj_exts,
			struct slabobj_ext *vec, unsigned int objects) {}

#endif /* CONFIG_MEM_ALLOC_PROFILING_DEBUG */

static inline void init_slab_obj_exts(struct slab *slab)
{
	slab->obj_exts = 0;
}

/*
 * Calculate the allocation size for slabobj_ext array.
 *
 * When memory allocation profiling is enabled, the obj_exts array
 * could be allocated from the same slab cache it's being allocated for.
 * This would prevent the slab from ever being freed because it would
 * always contain at least one allocated object (its own obj_exts array).
 *
 * To avoid this, increase the allocation size when we detect the array
 * may come from the same cache, forcing it to use a different cache.
 */
static inline size_t obj_exts_alloc_size(struct kmem_cache *s,
					 struct slab *slab, gfp_t gfp)
{
	size_t sz = sizeof(struct slabobj_ext) * slab->objects;
	struct kmem_cache *obj_exts_cache;

	/*
	 * slabobj_ext array for KMALLOC_CGROUP allocations
	 * are served from KMALLOC_NORMAL caches.
	 */
	if (!mem_alloc_profiling_enabled())
		return sz;

	if (sz > KMALLOC_MAX_CACHE_SIZE)
		return sz;

	if (!is_kmalloc_normal(s))
		return sz;

	obj_exts_cache = kmalloc_slab(sz, NULL, gfp, 0);
	/*
	 * We can't simply compare s with obj_exts_cache, because random kmalloc
	 * caches have multiple caches per size, selected by caller address.
	 * Since caller address may differ between kmalloc_slab() and actual
	 * allocation, bump size when sizes are equal.
	 */
	if (s->object_size == obj_exts_cache->object_size)
		return obj_exts_cache->object_size + 1;

	return sz;
}

int alloc_slab_obj_exts(struct slab *slab, struct kmem_cache *s,
		        gfp_t gfp, bool new_slab)
{
	bool allow_spin = gfpflags_allow_spinning(gfp);
	unsigned int objects = objs_per_slab(s, slab);
	unsigned long new_exts;
	unsigned long old_exts;
	struct slabobj_ext *vec;
	size_t sz;

	gfp &= ~OBJCGS_CLEAR_MASK;
	/* Prevent recursive extension vector allocation */
	gfp |= __GFP_NO_OBJ_EXT;

	sz = obj_exts_alloc_size(s, slab, gfp);

	/*
	 * Note that allow_spin may be false during early boot and its
	 * restricted GFP_BOOT_MASK. Due to kmalloc_nolock() only supporting
	 * architectures with cmpxchg16b, early obj_exts will be missing for
	 * very early allocations on those.
	 */
	if (unlikely(!allow_spin))
		vec = kmalloc_nolock(sz, __GFP_ZERO | __GFP_NO_OBJ_EXT,
				     slab_nid(slab));
	else
		vec = kmalloc_node(sz, gfp | __GFP_ZERO, slab_nid(slab));

	if (!vec) {
		/*
		 * Try to mark vectors which failed to allocate.
		 * If this operation fails, there may be a racing process
		 * that has already completed the allocation.
		 */
		if (!mark_failed_objexts_alloc(slab) &&
		    slab_obj_exts(slab))
			return 0;

		return -ENOMEM;
	}

	VM_WARN_ON_ONCE(virt_to_slab(vec) != NULL &&
			virt_to_slab(vec)->slab_cache == s);

	new_exts = (unsigned long)vec;
	if (unlikely(!allow_spin))
		new_exts |= OBJEXTS_NOSPIN_ALLOC;
#ifdef CONFIG_MEMCG
	new_exts |= MEMCG_DATA_OBJEXTS;
#endif
retry:
	old_exts = READ_ONCE(slab->obj_exts);
	handle_failed_objexts_alloc(old_exts, vec, objects);
	slab_set_stride(slab, sizeof(struct slabobj_ext));

	if (new_slab) {
		/*
		 * If the slab is brand new and nobody can yet access its
		 * obj_exts, no synchronization is required and obj_exts can
		 * be simply assigned.
		 */
		slab->obj_exts = new_exts;
	} else if (old_exts & ~OBJEXTS_FLAGS_MASK) {
		/*
		 * If the slab is already in use, somebody can allocate and
		 * assign slabobj_exts in parallel. In this case the existing
		 * objcg vector should be reused.
		 */
		mark_objexts_empty(vec);
		if (unlikely(!allow_spin))
			kfree_nolock(vec);
		else
			kfree(vec);
		return 0;
	} else if (cmpxchg(&slab->obj_exts, old_exts, new_exts) != old_exts) {
		/* Retry if a racing thread changed slab->obj_exts from under us. */
		goto retry;
	}

	if (allow_spin)
		kmemleak_not_leak(vec);
	return 0;
}

static inline void free_slab_obj_exts(struct slab *slab)
{
	struct slabobj_ext *obj_exts;

	obj_exts = (struct slabobj_ext *)slab_obj_exts(slab);
	if (!obj_exts) {
		/*
		 * If obj_exts allocation failed, slab->obj_exts is set to
		 * OBJEXTS_ALLOC_FAIL. In this case, we end up here and should
		 * clear the flag.
		 */
		slab->obj_exts = 0;
		return;
	}

	if (obj_exts_in_slab(slab->slab_cache, slab)) {
		slab->obj_exts = 0;
		return;
	}

	/*
	 * obj_exts was created with __GFP_NO_OBJ_EXT flag, therefore its
	 * corresponding extension will be NULL. alloc_tag_sub() will throw a
	 * warning if slab has extensions but the extension of an object is
	 * NULL, therefore replace NULL with CODETAG_EMPTY to indicate that
	 * the extension for obj_exts is expected to be NULL.
	 */
	mark_objexts_empty(obj_exts);
	if (unlikely(READ_ONCE(slab->obj_exts) & OBJEXTS_NOSPIN_ALLOC))
		kfree_nolock(obj_exts);
	else
		kfree(obj_exts);
	slab->obj_exts = 0;
}

/*
 * Try to allocate slabobj_ext array from unused space.
 * This function must be called on a freshly allocated slab to prevent
 * concurrency problems.
 */
static void alloc_slab_obj_exts_early(struct kmem_cache *s, struct slab *slab)
{
	void *addr;
	unsigned long obj_exts;

	if (!need_slab_obj_exts(s))
		return;

	if (obj_exts_fit_within_slab_leftover(s, slab)) {
		addr = slab_address(slab) + obj_exts_offset_in_slab(s, slab);
		addr = kasan_reset_tag(addr);
		obj_exts = (unsigned long)addr;

		get_slab_obj_exts(obj_exts);
		memset(addr, 0, obj_exts_size_in_slab(slab));
		put_slab_obj_exts(obj_exts);

#ifdef CONFIG_MEMCG
		obj_exts |= MEMCG_DATA_OBJEXTS;
#endif
		slab->obj_exts = obj_exts;
		slab_set_stride(slab, sizeof(struct slabobj_ext));
	} else if (s->flags & SLAB_OBJ_EXT_IN_OBJ) {
		unsigned int offset = obj_exts_offset_in_object(s);

		obj_exts = (unsigned long)slab_address(slab);
		obj_exts += s->red_left_pad;
		obj_exts += offset;

		get_slab_obj_exts(obj_exts);
		for_each_object(addr, s, slab_address(slab), slab->objects)
			memset(kasan_reset_tag(addr) + offset, 0,
			       sizeof(struct slabobj_ext));
		put_slab_obj_exts(obj_exts);

#ifdef CONFIG_MEMCG
		obj_exts |= MEMCG_DATA_OBJEXTS;
#endif
		slab->obj_exts = obj_exts;
		slab_set_stride(slab, s->size);
	}
}

#else /* CONFIG_SLAB_OBJ_EXT */

static inline void init_slab_obj_exts(struct slab *slab)
{
}

static int alloc_slab_obj_exts(struct slab *slab, struct kmem_cache *s,
			       gfp_t gfp, bool new_slab)
{
	return 0;
}

static inline void free_slab_obj_exts(struct slab *slab)
{
}

static inline void alloc_slab_obj_exts_early(struct kmem_cache *s,
						       struct slab *slab)
{
}

#endif /* CONFIG_SLAB_OBJ_EXT */

#ifdef CONFIG_MEM_ALLOC_PROFILING

static inline unsigned long
prepare_slab_obj_exts_hook(struct kmem_cache *s, struct slab *slab,
			   gfp_t flags, void *p)
{
	if (!slab_obj_exts(slab) &&
	    alloc_slab_obj_exts(slab, s, flags, false)) {
		pr_warn_once("%s, %s: Failed to create slab extension vector!\n",
			     __func__, s->name);
		return 0;
	}

	return slab_obj_exts(slab);
}


/* Should be called only if mem_alloc_profiling_enabled() */
static noinline void
__alloc_tagging_slab_alloc_hook(struct kmem_cache *s, void *object, gfp_t flags)
{
	unsigned long obj_exts;
	struct slabobj_ext *obj_ext;
	struct slab *slab;

	if (!object)
		return;

	if (s->flags & (SLAB_NO_OBJ_EXT | SLAB_NOLEAKTRACE))
		return;

	if (flags & __GFP_NO_OBJ_EXT)
		return;

	slab = virt_to_slab(object);
	obj_exts = prepare_slab_obj_exts_hook(s, slab, flags, object);
	/*
	 * Currently obj_exts is used only for allocation profiling.
	 * If other users appear then mem_alloc_profiling_enabled()
	 * check should be added before alloc_tag_add().
	 */
	if (obj_exts) {
		unsigned int obj_idx = obj_to_index(s, slab, object);

		get_slab_obj_exts(obj_exts);
		obj_ext = slab_obj_ext(slab, obj_exts, obj_idx);
		alloc_tag_add(&obj_ext->ref, current->alloc_tag, s->size);
		put_slab_obj_exts(obj_exts);
	} else {
		alloc_tag_set_inaccurate(current->alloc_tag);
	}
}

static inline void
alloc_tagging_slab_alloc_hook(struct kmem_cache *s, void *object, gfp_t flags)
{
	if (mem_alloc_profiling_enabled())
		__alloc_tagging_slab_alloc_hook(s, object, flags);
}

/* Should be called only if mem_alloc_profiling_enabled() */
static noinline void
__alloc_tagging_slab_free_hook(struct kmem_cache *s, struct slab *slab, void **p,
			       int objects)
{
	int i;
	unsigned long obj_exts;

	/* slab->obj_exts might not be NULL if it was created for MEMCG accounting. */
	if (s->flags & (SLAB_NO_OBJ_EXT | SLAB_NOLEAKTRACE))
		return;

	obj_exts = slab_obj_exts(slab);
	if (!obj_exts)
		return;

	get_slab_obj_exts(obj_exts);
	for (i = 0; i < objects; i++) {
		unsigned int off = obj_to_index(s, slab, p[i]);

		alloc_tag_sub(&slab_obj_ext(slab, obj_exts, off)->ref, s->size);
	}
	put_slab_obj_exts(obj_exts);
}

static inline void
alloc_tagging_slab_free_hook(struct kmem_cache *s, struct slab *slab, void **p,
			     int objects)
{
	if (mem_alloc_profiling_enabled())
		__alloc_tagging_slab_free_hook(s, slab, p, objects);
}

#else /* CONFIG_MEM_ALLOC_PROFILING */

static inline void
alloc_tagging_slab_alloc_hook(struct kmem_cache *s, void *object, gfp_t flags)
{
}

static inline void
alloc_tagging_slab_free_hook(struct kmem_cache *s, struct slab *slab, void **p,
			     int objects)
{
}

#endif /* CONFIG_MEM_ALLOC_PROFILING */


#ifdef CONFIG_MEMCG

static void memcg_alloc_abort_single(struct kmem_cache *s, void *object);

static __fastpath_inline
bool memcg_slab_post_alloc_hook(struct kmem_cache *s, struct list_lru *lru,
				gfp_t flags, size_t size, void **p)
{
	if (likely(!memcg_kmem_online()))
		return true;

	if (likely(!(flags & __GFP_ACCOUNT) && !(s->flags & SLAB_ACCOUNT)))
		return true;

	if (likely(__memcg_slab_post_alloc_hook(s, lru, flags, size, p)))
		return true;

	if (likely(size == 1)) {
		memcg_alloc_abort_single(s, *p);
		*p = NULL;
	} else {
		kmem_cache_free_bulk(s, size, p);
	}

	return false;
}

static __fastpath_inline
void memcg_slab_free_hook(struct kmem_cache *s, struct slab *slab, void **p,
			  int objects)
{
	unsigned long obj_exts;

	if (!memcg_kmem_online())
		return;

	obj_exts = slab_obj_exts(slab);
	if (likely(!obj_exts))
		return;

	get_slab_obj_exts(obj_exts);
	__memcg_slab_free_hook(s, slab, p, objects, obj_exts);
	put_slab_obj_exts(obj_exts);
}

static __fastpath_inline
bool memcg_slab_post_charge(void *p, gfp_t flags)
{
	unsigned long obj_exts;
	struct slabobj_ext *obj_ext;
	struct kmem_cache *s;
	struct page *page;
	struct slab *slab;
	unsigned long off;

	page = virt_to_page(p);
	if (PageLargeKmalloc(page)) {
		unsigned int order;
		int size;

		if (PageMemcgKmem(page))
			return true;

		order = large_kmalloc_order(page);
		if (__memcg_kmem_charge_page(page, flags, order))
			return false;

		/*
		 * This page has already been accounted in the global stats but
		 * not in the memcg stats. So, subtract from the global and use
		 * the interface which adds to both global and memcg stats.
		 */
		size = PAGE_SIZE << order;
		mod_node_page_state(page_pgdat(page), NR_SLAB_UNRECLAIMABLE_B, -size);
		mod_lruvec_page_state(page, NR_SLAB_UNRECLAIMABLE_B, size);
		return true;
	}

	slab = page_slab(page);
	s = slab->slab_cache;

	/*
	 * Ignore KMALLOC_NORMAL cache to avoid possible circular dependency
	 * of slab_obj_exts being allocated from the same slab and thus the slab
	 * becoming effectively unfreeable.
	 */
	if (is_kmalloc_normal(s))
		return true;

	/* Ignore already charged objects. */
	obj_exts = slab_obj_exts(slab);
	if (obj_exts) {
		get_slab_obj_exts(obj_exts);
		off = obj_to_index(s, slab, p);
		obj_ext = slab_obj_ext(slab, obj_exts, off);
		if (unlikely(obj_ext->objcg)) {
			put_slab_obj_exts(obj_exts);
			return true;
		}
		put_slab_obj_exts(obj_exts);
	}

	return __memcg_slab_post_alloc_hook(s, NULL, flags, 1, &p);
}

#else /* CONFIG_MEMCG */
static inline bool memcg_slab_post_alloc_hook(struct kmem_cache *s,
					      struct list_lru *lru,
					      gfp_t flags, size_t size,
					      void **p)
{
	return true;
}

static inline void memcg_slab_free_hook(struct kmem_cache *s, struct slab *slab,
					void **p, int objects)
{
}

static inline bool memcg_slab_post_charge(void *p, gfp_t flags)
{
	return true;
}
#endif /* CONFIG_MEMCG */

#ifdef CONFIG_SLUB_RCU_DEBUG
static void slab_free_after_rcu_debug(struct rcu_head *rcu_head);

struct rcu_delayed_free {
	struct rcu_head head;
	void *object;
};
#endif

/*
 * Hooks for other subsystems that check memory allocations. In a typical
 * production configuration these hooks all should produce no code at all.
 *
 * Returns true if freeing of the object can proceed, false if its reuse
 * was delayed by CONFIG_SLUB_RCU_DEBUG or KASAN quarantine, or it was returned
 * to KFENCE.
 */
static __always_inline
bool slab_free_hook(struct kmem_cache *s, void *x, bool init,
		    bool after_rcu_delay)
{
	/* Are the object contents still accessible? */
	bool still_accessible = (s->flags & SLAB_TYPESAFE_BY_RCU) && !after_rcu_delay;

	kmemleak_free_recursive(x, s->flags);
	kmsan_slab_free(s, x);

	debug_check_no_locks_freed(x, s->object_size);

	if (!(s->flags & SLAB_DEBUG_OBJECTS))
		debug_check_no_obj_freed(x, s->object_size);

	/* Use KCSAN to help debug racy use-after-free. */
	if (!still_accessible)
		__kcsan_check_access(x, s->object_size,
				     KCSAN_ACCESS_WRITE | KCSAN_ACCESS_ASSERT);

	if (kfence_free(x))
		return false;

	/*
	 * Give KASAN a chance to notice an invalid free operation before we
	 * modify the object.
	 */
	if (kasan_slab_pre_free(s, x))
		return false;

#ifdef CONFIG_SLUB_RCU_DEBUG
	if (still_accessible) {
		struct rcu_delayed_free *delayed_free;

		delayed_free = kmalloc(sizeof(*delayed_free), GFP_NOWAIT);
		if (delayed_free) {
			/*
			 * Let KASAN track our call stack as a "related work
			 * creation", just like if the object had been freed
			 * normally via kfree_rcu().
			 * We have to do this manually because the rcu_head is
			 * not located inside the object.
			 */
			kasan_record_aux_stack(x);

			delayed_free->object = x;
			call_rcu(&delayed_free->head, slab_free_after_rcu_debug);
			return false;
		}
	}
#endif /* CONFIG_SLUB_RCU_DEBUG */

	/*
	 * As memory initialization might be integrated into KASAN,
	 * kasan_slab_free and initialization memset's must be
	 * kept together to avoid discrepancies in behavior.
	 *
	 * The initialization memset's clear the object and the metadata,
	 * but don't touch the SLAB redzone.
	 *
	 * The object's freepointer is also avoided if stored outside the
	 * object.
	 */
	if (unlikely(init)) {
		int rsize;
		unsigned int inuse, orig_size;

		inuse = get_info_end(s);
		orig_size = get_orig_size(s, x);
		if (!kasan_has_integrated_init())
			memset(kasan_reset_tag(x), 0, orig_size);
		rsize = (s->flags & SLAB_RED_ZONE) ? s->red_left_pad : 0;
		memset((char *)kasan_reset_tag(x) + inuse, 0,
		       s->size - inuse - rsize);
		/*
		 * Restore orig_size, otherwise kmalloc redzone overwritten
		 * would be reported
		 */
		set_orig_size(s, x, orig_size);

	}
	/* KASAN might put x into memory quarantine, delaying its reuse. */
	return !kasan_slab_free(s, x, init, still_accessible, false);
}

static __fastpath_inline
bool slab_free_freelist_hook(struct kmem_cache *s, void **head, void **tail,
			     int *cnt)
{

	void *object;
	void *next = *head;
	void *old_tail = *tail;
	bool init;

	if (is_kfence_address(next)) {
		slab_free_hook(s, next, false, false);
		return false;
	}

	/* Head and tail of the reconstructed freelist */
	*head = NULL;
	*tail = NULL;

	init = slab_want_init_on_free(s);

	do {
		object = next;
		next = get_freepointer(s, object);

		/* If object's reuse doesn't have to be delayed */
		if (likely(slab_free_hook(s, object, init, false))) {
			/* Move object to the new freelist */
			set_freepointer(s, object, *head);
			*head = object;
			if (!*tail)
				*tail = object;
		} else {
			/*
			 * Adjust the reconstructed freelist depth
			 * accordingly if object's reuse is delayed.
			 */
			--(*cnt);
		}
	} while (object != old_tail);

	return *head != NULL;
}

static void *setup_object(struct kmem_cache *s, void *object)
{
	setup_object_debug(s, object);
	object = kasan_init_slab_obj(s, object);
	if (unlikely(s->ctor)) {
		kasan_unpoison_new_object(s, object);
		s->ctor(object);
		kasan_poison_new_object(s, object);
	}
	return object;
}

static struct slab_sheaf *__alloc_empty_sheaf(struct kmem_cache *s, gfp_t gfp,
					      unsigned int capacity)
{
	struct slab_sheaf *sheaf;
	size_t sheaf_size;

	if (gfp & __GFP_NO_OBJ_EXT)
		return NULL;

	gfp &= ~OBJCGS_CLEAR_MASK;

	/*
	 * Prevent recursion to the same cache, or a deep stack of kmallocs of
	 * varying sizes (sheaf capacity might differ for each kmalloc size
	 * bucket)
	 */
	if (s->flags & SLAB_KMALLOC)
		gfp |= __GFP_NO_OBJ_EXT;

	sheaf_size = struct_size(sheaf, objects, capacity);
	sheaf = kzalloc(sheaf_size, gfp);

	if (unlikely(!sheaf))
		return NULL;

	sheaf->cache = s;

	stat(s, SHEAF_ALLOC);

	return sheaf;
}

static inline struct slab_sheaf *alloc_empty_sheaf(struct kmem_cache *s,
						   gfp_t gfp)
{
	return __alloc_empty_sheaf(s, gfp, s->sheaf_capacity);
}

static void free_empty_sheaf(struct kmem_cache *s, struct slab_sheaf *sheaf)
{
	kfree(sheaf);

	stat(s, SHEAF_FREE);
}

static unsigned int
refill_objects(struct kmem_cache *s, void **p, gfp_t gfp, unsigned int min,
	       unsigned int max);

static int refill_sheaf(struct kmem_cache *s, struct slab_sheaf *sheaf,
			 gfp_t gfp)
{
	int to_fill = s->sheaf_capacity - sheaf->size;
	int filled;

	if (!to_fill)
		return 0;

	filled = refill_objects(s, &sheaf->objects[sheaf->size], gfp, to_fill,
				to_fill);

	sheaf->size += filled;

	stat_add(s, SHEAF_REFILL, filled);

	if (filled < to_fill)
		return -ENOMEM;

	return 0;
}


static struct slab_sheaf *alloc_full_sheaf(struct kmem_cache *s, gfp_t gfp)
{
	struct slab_sheaf *sheaf = alloc_empty_sheaf(s, gfp);

	if (!sheaf)
		return NULL;

	if (refill_sheaf(s, sheaf, gfp | __GFP_NOMEMALLOC)) {
		free_empty_sheaf(s, sheaf);
		return NULL;
	}

	return sheaf;
}

/*
 * Maximum number of objects freed during a single flush of main pcs sheaf.
 * Translates directly to an on-stack array size.
 */
#define PCS_BATCH_MAX	32U

static void __kmem_cache_free_bulk(struct kmem_cache *s, size_t size, void **p);

/*
 * Free all objects from the main sheaf. In order to perform
 * __kmem_cache_free_bulk() outside of cpu_sheaves->lock, work in batches where
 * object pointers are moved to a on-stack array under the lock. To bound the
 * stack usage, limit each batch to PCS_BATCH_MAX.
 *
 * returns true if at least partially flushed
 */
static bool sheaf_flush_main(struct kmem_cache *s)
{
	struct slub_percpu_sheaves *pcs;
	unsigned int batch, remaining;
	void *objects[PCS_BATCH_MAX];
	struct slab_sheaf *sheaf;
	bool ret = false;

next_batch:
	if (!local_trylock(&s->cpu_sheaves->lock))
		return ret;

	pcs = this_cpu_ptr(s->cpu_sheaves);
	sheaf = pcs->main;

	batch = min(PCS_BATCH_MAX, sheaf->size);

	sheaf->size -= batch;
	memcpy(objects, sheaf->objects + sheaf->size, batch * sizeof(void *));

	remaining = sheaf->size;

	local_unlock(&s->cpu_sheaves->lock);

	__kmem_cache_free_bulk(s, batch, &objects[0]);

	stat_add(s, SHEAF_FLUSH, batch);

	ret = true;

	if (remaining)
		goto next_batch;

	return ret;
}

/*
 * Free all objects from a sheaf that's unused, i.e. not linked to any
 * cpu_sheaves, so we need no locking and batching. The locking is also not
 * necessary when flushing cpu's sheaves (both spare and main) during cpu
 * hotremove as the cpu is not executing anymore.
 */
static void sheaf_flush_unused(struct kmem_cache *s, struct slab_sheaf *sheaf)
{
	if (!sheaf->size)
		return;

	stat_add(s, SHEAF_FLUSH, sheaf->size);

	__kmem_cache_free_bulk(s, sheaf->size, &sheaf->objects[0]);

	sheaf->size = 0;
}

static bool __rcu_free_sheaf_prepare(struct kmem_cache *s,
				     struct slab_sheaf *sheaf)
{
	bool init = slab_want_init_on_free(s);
	void **p = &sheaf->objects[0];
	unsigned int i = 0;
	bool pfmemalloc = false;

	while (i < sheaf->size) {
		struct slab *slab = virt_to_slab(p[i]);

		memcg_slab_free_hook(s, slab, p + i, 1);
		alloc_tagging_slab_free_hook(s, slab, p + i, 1);

		if (unlikely(!slab_free_hook(s, p[i], init, true))) {
			p[i] = p[--sheaf->size];
			continue;
		}

		if (slab_test_pfmemalloc(slab))
			pfmemalloc = true;

		i++;
	}

	return pfmemalloc;
}

static void rcu_free_sheaf_nobarn(struct rcu_head *head)
{
	struct slab_sheaf *sheaf;
	struct kmem_cache *s;

	sheaf = container_of(head, struct slab_sheaf, rcu_head);
	s = sheaf->cache;

	__rcu_free_sheaf_prepare(s, sheaf);

	sheaf_flush_unused(s, sheaf);

	free_empty_sheaf(s, sheaf);
}

/*
 * Caller needs to make sure migration is disabled in order to fully flush
 * single cpu's sheaves
 *
 * must not be called from an irq
 *
 * flushing operations are rare so let's keep it simple and flush to slabs
 * directly, skipping the barn
 */
static void pcs_flush_all(struct kmem_cache *s)
{
	struct slub_percpu_sheaves *pcs;
	struct slab_sheaf *spare, *rcu_free;

	local_lock(&s->cpu_sheaves->lock);
	pcs = this_cpu_ptr(s->cpu_sheaves);

	spare = pcs->spare;
	pcs->spare = NULL;

	rcu_free = pcs->rcu_free;
	pcs->rcu_free = NULL;

	local_unlock(&s->cpu_sheaves->lock);

	if (spare) {
		sheaf_flush_unused(s, spare);
		free_empty_sheaf(s, spare);
	}

	if (rcu_free)
		call_rcu(&rcu_free->rcu_head, rcu_free_sheaf_nobarn);

	sheaf_flush_main(s);
}

static void __pcs_flush_all_cpu(struct kmem_cache *s, unsigned int cpu)
{
	struct slub_percpu_sheaves *pcs;

	pcs = per_cpu_ptr(s->cpu_sheaves, cpu);

	/* The cpu is not executing anymore so we don't need pcs->lock */
	sheaf_flush_unused(s, pcs->main);
	if (pcs->spare) {
		sheaf_flush_unused(s, pcs->spare);
		free_empty_sheaf(s, pcs->spare);
		pcs->spare = NULL;
	}

	if (pcs->rcu_free) {
		call_rcu(&pcs->rcu_free->rcu_head, rcu_free_sheaf_nobarn);
		pcs->rcu_free = NULL;
	}
}

static void pcs_destroy(struct kmem_cache *s)
{
	int cpu;

	/*
	 * We may be unwinding cache creation that failed before or during the
	 * allocation of this.
	 */
	if (!s->cpu_sheaves)
		return;

	/* pcs->main can only point to the bootstrap sheaf, nothing to free */
	if (!cache_has_sheaves(s))
		goto free_pcs;

	for_each_possible_cpu(cpu) {
		struct slub_percpu_sheaves *pcs;

		pcs = per_cpu_ptr(s->cpu_sheaves, cpu);

		/* This can happen when unwinding failed cache creation. */
		if (!pcs->main)
			continue;

		/*
		 * We have already passed __kmem_cache_shutdown() so everything
		 * was flushed and there should be no objects allocated from
		 * slabs, otherwise kmem_cache_destroy() would have aborted.
		 * Therefore something would have to be really wrong if the
		 * warnings here trigger, and we should rather leave objects and
		 * sheaves to leak in that case.
		 */

		WARN_ON(pcs->spare);
		WARN_ON(pcs->rcu_free);

		if (!WARN_ON(pcs->main->size)) {
			free_empty_sheaf(s, pcs->main);
			pcs->main = NULL;
		}
	}

free_pcs:
	free_percpu(s->cpu_sheaves);
	s->cpu_sheaves = NULL;
}

static struct slab_sheaf *barn_get_empty_sheaf(struct node_barn *barn,
					       bool allow_spin)
{
	struct slab_sheaf *empty = NULL;
	unsigned long flags;

	if (!data_race(barn->nr_empty))
		return NULL;

	if (likely(allow_spin))
		spin_lock_irqsave(&barn->lock, flags);
	else if (!spin_trylock_irqsave(&barn->lock, flags))
		return NULL;

	if (likely(barn->nr_empty)) {
		empty = list_first_entry(&barn->sheaves_empty,
					 struct slab_sheaf, barn_list);
		list_del(&empty->barn_list);
		barn->nr_empty--;
	}

	spin_unlock_irqrestore(&barn->lock, flags);

	return empty;
}

/*
 * The following two functions are used mainly in cases where we have to undo an
 * intended action due to a race or cpu migration. Thus they do not check the
 * empty or full sheaf limits for simplicity.
 */

static void barn_put_empty_sheaf(struct node_barn *barn, struct slab_sheaf *sheaf)
{
	unsigned long flags;

	spin_lock_irqsave(&barn->lock, flags);

	list_add(&sheaf->barn_list, &barn->sheaves_empty);
	barn->nr_empty++;

	spin_unlock_irqrestore(&barn->lock, flags);
}

static void barn_put_full_sheaf(struct node_barn *barn, struct slab_sheaf *sheaf)
{
	unsigned long flags;

	spin_lock_irqsave(&barn->lock, flags);

	list_add(&sheaf->barn_list, &barn->sheaves_full);
	barn->nr_full++;

	spin_unlock_irqrestore(&barn->lock, flags);
}

static struct slab_sheaf *barn_get_full_or_empty_sheaf(struct node_barn *barn)
{
	struct slab_sheaf *sheaf = NULL;
	unsigned long flags;

	if (!data_race(barn->nr_full) && !data_race(barn->nr_empty))
		return NULL;

	spin_lock_irqsave(&barn->lock, flags);

	if (barn->nr_full) {
		sheaf = list_first_entry(&barn->sheaves_full, struct slab_sheaf,
					barn_list);
		list_del(&sheaf->barn_list);
		barn->nr_full--;
	} else if (barn->nr_empty) {
		sheaf = list_first_entry(&barn->sheaves_empty,
					 struct slab_sheaf, barn_list);
		list_del(&sheaf->barn_list);
		barn->nr_empty--;
	}

	spin_unlock_irqrestore(&barn->lock, flags);

	return sheaf;
}

/*
 * If a full sheaf is available, return it and put the supplied empty one to
 * barn. We ignore the limit on empty sheaves as the number of sheaves doesn't
 * change.
 */
static struct slab_sheaf *
barn_replace_empty_sheaf(struct node_barn *barn, struct slab_sheaf *empty,
			 bool allow_spin)
{
	struct slab_sheaf *full = NULL;
	unsigned long flags;

	if (!data_race(barn->nr_full))
		return NULL;

	if (likely(allow_spin))
		spin_lock_irqsave(&barn->lock, flags);
	else if (!spin_trylock_irqsave(&barn->lock, flags))
		return NULL;

	if (likely(barn->nr_full)) {
		full = list_first_entry(&barn->sheaves_full, struct slab_sheaf,
					barn_list);
		list_del(&full->barn_list);
		list_add(&empty->barn_list, &barn->sheaves_empty);
		barn->nr_full--;
		barn->nr_empty++;
	}

	spin_unlock_irqrestore(&barn->lock, flags);

	return full;
}

/*
 * If an empty sheaf is available, return it and put the supplied full one to
 * barn. But if there are too many full sheaves, reject this with -E2BIG.
 */
static struct slab_sheaf *
barn_replace_full_sheaf(struct node_barn *barn, struct slab_sheaf *full,
			bool allow_spin)
{
	struct slab_sheaf *empty;
	unsigned long flags;

	/* we don't repeat this check under barn->lock as it's not critical */
	if (data_race(barn->nr_full) >= MAX_FULL_SHEAVES)
		return ERR_PTR(-E2BIG);
	if (!data_race(barn->nr_empty))
		return ERR_PTR(-ENOMEM);

	if (likely(allow_spin))
		spin_lock_irqsave(&barn->lock, flags);
	else if (!spin_trylock_irqsave(&barn->lock, flags))
		return ERR_PTR(-EBUSY);

	if (likely(barn->nr_empty)) {
		empty = list_first_entry(&barn->sheaves_empty, struct slab_sheaf,
					 barn_list);
		list_del(&empty->barn_list);
		list_add(&full->barn_list, &barn->sheaves_full);
		barn->nr_empty--;
		barn->nr_full++;
	} else {
		empty = ERR_PTR(-ENOMEM);
	}

	spin_unlock_irqrestore(&barn->lock, flags);

	return empty;
}

static void barn_init(struct node_barn *barn)
{
	spin_lock_init(&barn->lock);
	INIT_LIST_HEAD(&barn->sheaves_full);
	INIT_LIST_HEAD(&barn->sheaves_empty);
	barn->nr_full = 0;
	barn->nr_empty = 0;
}

static void barn_shrink(struct kmem_cache *s, struct node_barn *barn)
{
	LIST_HEAD(empty_list);
	LIST_HEAD(full_list);
	struct slab_sheaf *sheaf, *sheaf2;
	unsigned long flags;

	spin_lock_irqsave(&barn->lock, flags);

	list_splice_init(&barn->sheaves_full, &full_list);
	barn->nr_full = 0;
	list_splice_init(&barn->sheaves_empty, &empty_list);
	barn->nr_empty = 0;

	spin_unlock_irqrestore(&barn->lock, flags);

	list_for_each_entry_safe(sheaf, sheaf2, &full_list, barn_list) {
		sheaf_flush_unused(s, sheaf);
		free_empty_sheaf(s, sheaf);
	}

	list_for_each_entry_safe(sheaf, sheaf2, &empty_list, barn_list)
		free_empty_sheaf(s, sheaf);
}

/*
 * Slab allocation and freeing
 */
static inline struct slab *alloc_slab_page(gfp_t flags, int node,
					   struct kmem_cache_order_objects oo,
					   bool allow_spin)
{
	struct page *page;
	struct slab *slab;
	unsigned int order = oo_order(oo);

	if (unlikely(!allow_spin))
		page = alloc_frozen_pages_nolock(0/* __GFP_COMP is implied */,
								  node, order);
	else if (node == NUMA_NO_NODE)
		page = alloc_frozen_pages(flags, order);
	else
		page = __alloc_frozen_pages(flags, order, node, NULL);

	if (!page)
		return NULL;

	__SetPageSlab(page);
	slab = page_slab(page);
	if (page_is_pfmemalloc(page))
		slab_set_pfmemalloc(slab);

	return slab;
}

#ifdef CONFIG_SLAB_FREELIST_RANDOM
/* Pre-initialize the random sequence cache */
static int init_cache_random_seq(struct kmem_cache *s)
{
	unsigned int count = oo_objects(s->oo);
	int err;

	/* Bailout if already initialised */
	if (s->random_seq)
		return 0;

	err = cache_random_seq_create(s, count, GFP_KERNEL);
	if (err) {
		pr_err("SLUB: Unable to initialize free list for %s\n",
			s->name);
		return err;
	}

	/* Transform to an offset on the set of pages */
	if (s->random_seq) {
		unsigned int i;

		for (i = 0; i < count; i++)
			s->random_seq[i] *= s->size;
	}
	return 0;
}

/* Initialize each random sequence freelist per cache */
static void __init init_freelist_randomization(void)
{
	struct kmem_cache *s;

	mutex_lock(&slab_mutex);

	list_for_each_entry(s, &slab_caches, list)
		init_cache_random_seq(s);

	mutex_unlock(&slab_mutex);
}

/* Get the next entry on the pre-computed freelist randomized */
static void *next_freelist_entry(struct kmem_cache *s,
				unsigned long *pos, void *start,
				unsigned long page_limit,
				unsigned long freelist_count)
{
	unsigned int idx;

	/*
	 * If the target page allocation failed, the number of objects on the
	 * page might be smaller than the usual size defined by the cache.
	 */
	do {
		idx = s->random_seq[*pos];
		*pos += 1;
		if (*pos >= freelist_count)
			*pos = 0;
	} while (unlikely(idx >= page_limit));

	return (char *)start + idx;
}

/* Shuffle the single linked freelist based on a random pre-computed sequence */
static bool shuffle_freelist(struct kmem_cache *s, struct slab *slab)
{
	void *start;
	void *cur;
	void *next;
	unsigned long idx, pos, page_limit, freelist_count;

	if (slab->objects < 2 || !s->random_seq)
		return false;

	freelist_count = oo_objects(s->oo);
	pos = get_random_u32_below(freelist_count);

	page_limit = slab->objects * s->size;
	start = fixup_red_left(s, slab_address(slab));

	/* First entry is used as the base of the freelist */
	cur = next_freelist_entry(s, &pos, start, page_limit, freelist_count);
	cur = setup_object(s, cur);
	slab->freelist = cur;

	for (idx = 1; idx < slab->objects; idx++) {
		next = next_freelist_entry(s, &pos, start, page_limit,
			freelist_count);
		next = setup_object(s, next);
		set_freepointer(s, cur, next);
		cur = next;
	}
	set_freepointer(s, cur, NULL);

	return true;
}
#else
static inline int init_cache_random_seq(struct kmem_cache *s)
{
	return 0;
}
static inline void init_freelist_randomization(void) { }
static inline bool shuffle_freelist(struct kmem_cache *s, struct slab *slab)
{
	return false;
}
#endif /* CONFIG_SLAB_FREELIST_RANDOM */

static __always_inline void account_slab(struct slab *slab, int order,
					 struct kmem_cache *s, gfp_t gfp)
{
	if (memcg_kmem_online() &&
			(s->flags & SLAB_ACCOUNT) &&
			!slab_obj_exts(slab))
		alloc_slab_obj_exts(slab, s, gfp, true);

	mod_node_page_state(slab_pgdat(slab), cache_vmstat_idx(s),
			    PAGE_SIZE << order);
}

static __always_inline void unaccount_slab(struct slab *slab, int order,
					   struct kmem_cache *s)
{
	/*
	 * The slab object extensions should now be freed regardless of
	 * whether mem_alloc_profiling_enabled() or not because profiling
	 * might have been disabled after slab->obj_exts got allocated.
	 */
	free_slab_obj_exts(slab);

	mod_node_page_state(slab_pgdat(slab), cache_vmstat_idx(s),
			    -(PAGE_SIZE << order));
}

static struct slab *allocate_slab(struct kmem_cache *s, gfp_t flags, int node)
{
	bool allow_spin = gfpflags_allow_spinning(flags);
	struct slab *slab;
	struct kmem_cache_order_objects oo = s->oo;
	gfp_t alloc_gfp;
	void *start, *p, *next;
	int idx;
	bool shuffle;

	flags &= gfp_allowed_mask;

	flags |= s->allocflags;

	/*
	 * Let the initial higher-order allocation fail under memory pressure
	 * so we fall-back to the minimum order allocation.
	 */
	alloc_gfp = (flags | __GFP_NOWARN | __GFP_NORETRY) & ~__GFP_NOFAIL;
	if ((alloc_gfp & __GFP_DIRECT_RECLAIM) && oo_order(oo) > oo_order(s->min))
		alloc_gfp = (alloc_gfp | __GFP_NOMEMALLOC) & ~__GFP_RECLAIM;

	/*
	 * __GFP_RECLAIM could be cleared on the first allocation attempt,
	 * so pass allow_spin flag directly.
	 */
	slab = alloc_slab_page(alloc_gfp, node, oo, allow_spin);
	if (unlikely(!slab)) {
		oo = s->min;
		alloc_gfp = flags;
		/*
		 * Allocation may have failed due to fragmentation.
		 * Try a lower order alloc if possible
		 */
		slab = alloc_slab_page(alloc_gfp, node, oo, allow_spin);
		if (unlikely(!slab))
			return NULL;
		stat(s, ORDER_FALLBACK);
	}

	slab->objects = oo_objects(oo);
	slab->inuse = 0;
	slab->frozen = 0;

	slab->slab_cache = s;

	kasan_poison_slab(slab);

	start = slab_address(slab);

	setup_slab_debug(s, slab, start);
	init_slab_obj_exts(slab);
	/*
	 * Poison the slab before initializing the slabobj_ext array
	 * to prevent the array from being overwritten.
	 */
	alloc_slab_obj_exts_early(s, slab);
	account_slab(slab, oo_order(oo), s, flags);

	shuffle = shuffle_freelist(s, slab);

	if (!shuffle) {
		start = fixup_red_left(s, start);
		start = setup_object(s, start);
		slab->freelist = start;
		for (idx = 0, p = start; idx < slab->objects - 1; idx++) {
			next = p + s->size;
			next = setup_object(s, next);
			set_freepointer(s, p, next);
			p = next;
		}
		set_freepointer(s, p, NULL);
	}

	return slab;
}

static struct slab *new_slab(struct kmem_cache *s, gfp_t flags, int node)
{
	if (unlikely(flags & GFP_SLAB_BUG_MASK))
		flags = kmalloc_fix_flags(flags);

	WARN_ON_ONCE(s->ctor && (flags & __GFP_ZERO));

	return allocate_slab(s,
		flags & (GFP_RECLAIM_MASK | GFP_CONSTRAINT_MASK), node);
}

static void __free_slab(struct kmem_cache *s, struct slab *slab, bool allow_spin)
{
	struct page *page = slab_page(slab);
	int order = compound_order(page);
	int pages = 1 << order;

	__slab_clear_pfmemalloc(slab);
	page->mapping = NULL;
	__ClearPageSlab(page);
	mm_account_reclaimed_pages(pages);
	unaccount_slab(slab, order, s);
	if (allow_spin)
		free_frozen_pages(page, order);
	else
		free_frozen_pages_nolock(page, order);
}

static void free_new_slab_nolock(struct kmem_cache *s, struct slab *slab)
{
	/*
	 * Since it was just allocated, we can skip the actions in
	 * discard_slab() and free_slab().
	 */
	__free_slab(s, slab, false);
}

static void rcu_free_slab(struct rcu_head *h)
{
	struct slab *slab = container_of(h, struct slab, rcu_head);

	__free_slab(slab->slab_cache, slab, true);
}

static void free_slab(struct kmem_cache *s, struct slab *slab)
{
	if (kmem_cache_debug_flags(s, SLAB_CONSISTENCY_CHECKS)) {
		void *p;

		slab_pad_check(s, slab);
		for_each_object(p, s, slab_address(slab), slab->objects)
			check_object(s, slab, p, SLUB_RED_INACTIVE);
	}

	if (unlikely(s->flags & SLAB_TYPESAFE_BY_RCU))
		call_rcu(&slab->rcu_head, rcu_free_slab);
	else
		__free_slab(s, slab, true);
}

static void discard_slab(struct kmem_cache *s, struct slab *slab)
{
	dec_slabs_node(s, slab_nid(slab), slab->objects);
	free_slab(s, slab);
}

static inline bool slab_test_node_partial(const struct slab *slab)
{
	return test_bit(SL_partial, &slab->flags.f);
}

static inline void slab_set_node_partial(struct slab *slab)
{
	set_bit(SL_partial, &slab->flags.f);
}

static inline void slab_clear_node_partial(struct slab *slab)
{
	clear_bit(SL_partial, &slab->flags.f);
}

/*
 * Management of partially allocated slabs.
 */
static inline void
__add_partial(struct kmem_cache_node *n, struct slab *slab, enum add_mode mode)
{
	n->nr_partial++;
	if (mode == ADD_TO_TAIL)
		list_add_tail(&slab->slab_list, &n->partial);
	else
		list_add(&slab->slab_list, &n->partial);
	slab_set_node_partial(slab);
}

static inline void add_partial(struct kmem_cache_node *n,
				struct slab *slab, enum add_mode mode)
{
	lockdep_assert_held(&n->list_lock);
	__add_partial(n, slab, mode);
}

static inline void remove_partial(struct kmem_cache_node *n,
					struct slab *slab)
{
	lockdep_assert_held(&n->list_lock);
	list_del(&slab->slab_list);
	slab_clear_node_partial(slab);
	n->nr_partial--;
}

/*
 * Called only for kmem_cache_debug() caches instead of remove_partial(), with a
 * slab from the n->partial list. Remove only a single object from the slab, do
 * the alloc_debug_processing() checks and leave the slab on the list, or move
 * it to full list if it was the last free object.
 */
static void *alloc_single_from_partial(struct kmem_cache *s,
		struct kmem_cache_node *n, struct slab *slab, int orig_size)
{
	void *object;

	lockdep_assert_held(&n->list_lock);

#ifdef CONFIG_SLUB_DEBUG
	if (s->flags & SLAB_CONSISTENCY_CHECKS) {
		if (!validate_slab_ptr(slab)) {
			slab_err(s, slab, "Not a valid slab page");
			return NULL;
		}
	}
#endif

	object = slab->freelist;
	slab->freelist = get_freepointer(s, object);
	slab->inuse++;

	if (!alloc_debug_processing(s, slab, object, orig_size)) {
		remove_partial(n, slab);
		return NULL;
	}

	if (slab->inuse == slab->objects) {
		remove_partial(n, slab);
		add_full(s, n, slab);
	}

	return object;
}

/*
 * Called only for kmem_cache_debug() caches to allocate from a freshly
 * allocated slab. Allocate a single object instead of whole freelist
 * and put the slab to the partial (or full) list.
 */
static void *alloc_single_from_new_slab(struct kmem_cache *s, struct slab *slab,
					int orig_size, gfp_t gfpflags)
{
	bool allow_spin = gfpflags_allow_spinning(gfpflags);
	int nid = slab_nid(slab);
	struct kmem_cache_node *n = get_node(s, nid);
	unsigned long flags;
	void *object;

	if (!allow_spin && !spin_trylock_irqsave(&n->list_lock, flags)) {
		/* Unlucky, discard newly allocated slab. */
		free_new_slab_nolock(s, slab);
		return NULL;
	}

	object = slab->freelist;
	slab->freelist = get_freepointer(s, object);
	slab->inuse = 1;

	if (!alloc_debug_processing(s, slab, object, orig_size)) {
		/*
		 * It's not really expected that this would fail on a
		 * freshly allocated slab, but a concurrent memory
		 * corruption in theory could cause that.
		 * Leak memory of allocated slab.
		 */
		if (!allow_spin)
			spin_unlock_irqrestore(&n->list_lock, flags);
		return NULL;
	}

	if (allow_spin)
		spin_lock_irqsave(&n->list_lock, flags);

	if (slab->inuse == slab->objects)
		add_full(s, n, slab);
	else
		add_partial(n, slab, ADD_TO_HEAD);

	inc_slabs_node(s, nid, slab->objects);
	spin_unlock_irqrestore(&n->list_lock, flags);

	return object;
}

static inline bool pfmemalloc_match(struct slab *slab, gfp_t gfpflags);

static bool get_partial_node_bulk(struct kmem_cache *s,
				  struct kmem_cache_node *n,
				  struct partial_bulk_context *pc,
				  bool allow_spin)
{
	struct slab *slab, *slab2;
	unsigned int total_free = 0;
	unsigned long flags;

	/* Racy check to avoid taking the lock unnecessarily. */
	if (!n || data_race(!n->nr_partial))
		return false;

	INIT_LIST_HEAD(&pc->slabs);

	if (allow_spin)
		spin_lock_irqsave(&n->list_lock, flags);
	else if (!spin_trylock_irqsave(&n->list_lock, flags))
		return false;

	list_for_each_entry_safe(slab, slab2, &n->partial, slab_list) {
		struct freelist_counters flc;
		unsigned int slab_free;

		if (!pfmemalloc_match(slab, pc->flags))
			continue;

		/*
		 * determine the number of free objects in the slab racily
		 *
		 * slab_free is a lower bound due to possible subsequent
		 * concurrent freeing, so the caller may get more objects than
		 * requested and must handle that
		 */
		flc.counters = data_race(READ_ONCE(slab->counters));
		slab_free = flc.objects - flc.inuse;

		/* we have already min and this would get us over the max */
		if (total_free >= pc->min_objects
		    && total_free + slab_free > pc->max_objects)
			break;

		remove_partial(n, slab);

		list_add(&slab->slab_list, &pc->slabs);

		total_free += slab_free;
		if (total_free >= pc->max_objects)
			break;
	}

	spin_unlock_irqrestore(&n->list_lock, flags);
	return total_free > 0;
}

/*
 * Try to allocate object from a partial slab on a specific node.
 */
static void *get_from_partial_node(struct kmem_cache *s,
				   struct kmem_cache_node *n,
				   struct partial_context *pc)
{
	struct slab *slab, *slab2;
	unsigned long flags;
	void *object = NULL;

	/*
	 * Racy check. If we mistakenly see no partial slabs then we
	 * just allocate an empty slab. If we mistakenly try to get a
	 * partial slab and there is none available then get_from_partial()
	 * will return NULL.
	 */
	if (!n || !n->nr_partial)
		return NULL;

	if (gfpflags_allow_spinning(pc->flags))
		spin_lock_irqsave(&n->list_lock, flags);
	else if (!spin_trylock_irqsave(&n->list_lock, flags))
		return NULL;
	list_for_each_entry_safe(slab, slab2, &n->partial, slab_list) {

		struct freelist_counters old, new;

		if (!pfmemalloc_match(slab, pc->flags))
			continue;

		if (IS_ENABLED(CONFIG_SLUB_TINY) || kmem_cache_debug(s)) {
			object = alloc_single_from_partial(s, n, slab,
							pc->orig_size);
			if (object)
				break;
			continue;
		}

		/*
		 * get a single object from the slab. This might race against
		 * __slab_free(), which however has to take the list_lock if
		 * it's about to make the slab fully free.
		 */
		do {
			old.freelist = slab->freelist;
			old.counters = slab->counters;

			new.freelist = get_freepointer(s, old.freelist);
			new.counters = old.counters;
			new.inuse++;

		} while (!__slab_update_freelist(s, slab, &old, &new, "get_from_partial_node"));

		object = old.freelist;
		if (!new.freelist)
			remove_partial(n, slab);

		break;
	}
	spin_unlock_irqrestore(&n->list_lock, flags);
	return object;
}

/*
 * Get an object from somewhere. Search in increasing NUMA distances.
 */
static void *get_from_any_partial(struct kmem_cache *s, struct partial_context *pc)
{
#ifdef CONFIG_NUMA
	struct zonelist *zonelist;
	struct zoneref *z;
	struct zone *zone;
	enum zone_type highest_zoneidx = gfp_zone(pc->flags);
	unsigned int cpuset_mems_cookie;

	/*
	 * The defrag ratio allows a configuration of the tradeoffs between
	 * inter node defragmentation and node local allocations. A lower
	 * defrag_ratio increases the tendency to do local allocations
	 * instead of attempting to obtain partial slabs from other nodes.
	 *
	 * If the defrag_ratio is set to 0 then kmalloc() always
	 * returns node local objects. If the ratio is higher then kmalloc()
	 * may return off node objects because partial slabs are obtained
	 * from other nodes and filled up.
	 *
	 * If /sys/kernel/slab/xx/remote_node_defrag_ratio is set to 100
	 * (which makes defrag_ratio = 1000) then every (well almost)
	 * allocation will first attempt to defrag slab caches on other nodes.
	 * This means scanning over all nodes to look for partial slabs which
	 * may be expensive if we do it every time we are trying to find a slab
	 * with available objects.
	 */
	if (!s->remote_node_defrag_ratio ||
			get_cycles() % 1024 > s->remote_node_defrag_ratio)
		return NULL;

	do {
		cpuset_mems_cookie = read_mems_allowed_begin();
		zonelist = node_zonelist(mempolicy_slab_node(), pc->flags);
		for_each_zone_zonelist(zone, z, zonelist, highest_zoneidx) {
			struct kmem_cache_node *n;

			n = get_node(s, zone_to_nid(zone));

			if (n && cpuset_zone_allowed(zone, pc->flags) &&
					n->nr_partial > s->min_partial) {

				void *object = get_from_partial_node(s, n, pc);

				if (object) {
					/*
					 * Don't check read_mems_allowed_retry()
					 * here - if mems_allowed was updated in
					 * parallel, that was a harmless race
					 * between allocation and the cpuset
					 * update
					 */
					return object;
				}
			}
		}
	} while (read_mems_allowed_retry(cpuset_mems_cookie));
#endif	/* CONFIG_NUMA */
	return NULL;
}

/*
 * Get an object from a partial slab
 */
static void *get_from_partial(struct kmem_cache *s, int node,
			      struct partial_context *pc)
{
	int searchnode = node;
	void *object;

	if (node == NUMA_NO_NODE)
		searchnode = numa_mem_id();

	object = get_from_partial_node(s, get_node(s, searchnode), pc);
	if (object || (node != NUMA_NO_NODE && (pc->flags & __GFP_THISNODE)))
		return object;

	return get_from_any_partial(s, pc);
}

static bool has_pcs_used(int cpu, struct kmem_cache *s)
{
	struct slub_percpu_sheaves *pcs;

	if (!cache_has_sheaves(s))
		return false;

	pcs = per_cpu_ptr(s->cpu_sheaves, cpu);

	return (pcs->spare || pcs->rcu_free || pcs->main->size);
}

/*
 * Flush percpu sheaves
 *
 * Called from CPU work handler with migration disabled.
 */
static void flush_cpu_sheaves(struct work_struct *w)
{
	struct kmem_cache *s;
	struct slub_flush_work *sfw;

	sfw = container_of(w, struct slub_flush_work, work);

	s = sfw->s;

	if (cache_has_sheaves(s))
		pcs_flush_all(s);
}

static void flush_all_cpus_locked(struct kmem_cache *s)
{
	struct slub_flush_work *sfw;
	unsigned int cpu;

	lockdep_assert_cpus_held();
	mutex_lock(&flush_lock);

	for_each_online_cpu(cpu) {
		sfw = &per_cpu(slub_flush, cpu);
		if (!has_pcs_used(cpu, s)) {
			sfw->skip = true;
			continue;
		}
		INIT_WORK(&sfw->work, flush_cpu_sheaves);
		sfw->skip = false;
		sfw->s = s;
		queue_work_on(cpu, flushwq, &sfw->work);
	}

	for_each_online_cpu(cpu) {
		sfw = &per_cpu(slub_flush, cpu);
		if (sfw->skip)
			continue;
		flush_work(&sfw->work);
	}

	mutex_unlock(&flush_lock);
}

static void flush_all(struct kmem_cache *s)
{
	cpus_read_lock();
	flush_all_cpus_locked(s);
	cpus_read_unlock();
}

static void flush_rcu_sheaf(struct work_struct *w)
{
	struct slub_percpu_sheaves *pcs;
	struct slab_sheaf *rcu_free;
	struct slub_flush_work *sfw;
	struct kmem_cache *s;

	sfw = container_of(w, struct slub_flush_work, work);
	s = sfw->s;

	local_lock(&s->cpu_sheaves->lock);
	pcs = this_cpu_ptr(s->cpu_sheaves);

	rcu_free = pcs->rcu_free;
	pcs->rcu_free = NULL;

	local_unlock(&s->cpu_sheaves->lock);

	if (rcu_free)
		call_rcu(&rcu_free->rcu_head, rcu_free_sheaf_nobarn);
}


/* needed for kvfree_rcu_barrier() */
void flush_rcu_sheaves_on_cache(struct kmem_cache *s)
{
	struct slub_flush_work *sfw;
	unsigned int cpu;

	mutex_lock(&flush_lock);

	for_each_online_cpu(cpu) {
		sfw = &per_cpu(slub_flush, cpu);

		/*
		 * we don't check if rcu_free sheaf exists - racing
		 * __kfree_rcu_sheaf() might have just removed it.
		 * by executing flush_rcu_sheaf() on the cpu we make
		 * sure the __kfree_rcu_sheaf() finished its call_rcu()
		 */

		INIT_WORK(&sfw->work, flush_rcu_sheaf);
		sfw->s = s;
		queue_work_on(cpu, flushwq, &sfw->work);
	}

	for_each_online_cpu(cpu) {
		sfw = &per_cpu(slub_flush, cpu);
		flush_work(&sfw->work);
	}

	mutex_unlock(&flush_lock);
}

void flush_all_rcu_sheaves(void)
{
	struct kmem_cache *s;

	cpus_read_lock();
	mutex_lock(&slab_mutex);

	list_for_each_entry(s, &slab_caches, list) {
		if (!cache_has_sheaves(s))
			continue;
		flush_rcu_sheaves_on_cache(s);
	}

	mutex_unlock(&slab_mutex);
	cpus_read_unlock();

	rcu_barrier();
}

/*
 * Use the cpu notifier to insure that the cpu slabs are flushed when
 * necessary.
 */
static int slub_cpu_dead(unsigned int cpu)
{
	struct kmem_cache *s;

	mutex_lock(&slab_mutex);
	list_for_each_entry(s, &slab_caches, list) {
		if (cache_has_sheaves(s))
			__pcs_flush_all_cpu(s, cpu);
	}
	mutex_unlock(&slab_mutex);
	return 0;
}

#ifdef CONFIG_SLUB_DEBUG
static int count_free(struct slab *slab)
{
	return slab->objects - slab->inuse;
}

static inline unsigned long node_nr_objs(struct kmem_cache_node *n)
{
	return atomic_long_read(&n->total_objects);
}

/* Supports checking bulk free of a constructed freelist */
static inline bool free_debug_processing(struct kmem_cache *s,
	struct slab *slab, void *head, void *tail, int *bulk_cnt,
	unsigned long addr, depot_stack_handle_t handle)
{
	bool checks_ok = false;
	void *object = head;
	int cnt = 0;

	if (s->flags & SLAB_CONSISTENCY_CHECKS) {
		if (!check_slab(s, slab))
			goto out;
	}

	if (slab->inuse < *bulk_cnt) {
		slab_err(s, slab, "Slab has %d allocated objects but %d are to be freed\n",
			 slab->inuse, *bulk_cnt);
		goto out;
	}

next_object:

	if (++cnt > *bulk_cnt)
		goto out_cnt;

	if (s->flags & SLAB_CONSISTENCY_CHECKS) {
		if (!free_consistency_checks(s, slab, object, addr))
			goto out;
	}

	if (s->flags & SLAB_STORE_USER)
		set_track_update(s, object, TRACK_FREE, addr, handle);
	trace(s, slab, object, 0);
	/* Freepointer not overwritten by init_object(), SLAB_POISON moved it */
	init_object(s, object, SLUB_RED_INACTIVE);

	/* Reached end of constructed freelist yet? */
	if (object != tail) {
		object = get_freepointer(s, object);
		goto next_object;
	}
	checks_ok = true;

out_cnt:
	if (cnt != *bulk_cnt) {
		slab_err(s, slab, "Bulk free expected %d objects but found %d\n",
			 *bulk_cnt, cnt);
		*bulk_cnt = cnt;
	}

out:

	if (!checks_ok)
		slab_fix(s, "Object at 0x%p not freed", object);

	return checks_ok;
}
#endif /* CONFIG_SLUB_DEBUG */

#if defined(CONFIG_SLUB_DEBUG) || defined(SLAB_SUPPORTS_SYSFS)
static unsigned long count_partial(struct kmem_cache_node *n,
					int (*get_count)(struct slab *))
{
	unsigned long flags;
	unsigned long x = 0;
	struct slab *slab;

	spin_lock_irqsave(&n->list_lock, flags);
	list_for_each_entry(slab, &n->partial, slab_list)
		x += get_count(slab);
	spin_unlock_irqrestore(&n->list_lock, flags);
	return x;
}
#endif /* CONFIG_SLUB_DEBUG || SLAB_SUPPORTS_SYSFS */

#ifdef CONFIG_SLUB_DEBUG
#define MAX_PARTIAL_TO_SCAN 10000

static unsigned long count_partial_free_approx(struct kmem_cache_node *n)
{
	unsigned long flags;
	unsigned long x = 0;
	struct slab *slab;

	spin_lock_irqsave(&n->list_lock, flags);
	if (n->nr_partial <= MAX_PARTIAL_TO_SCAN) {
		list_for_each_entry(slab, &n->partial, slab_list)
			x += slab->objects - slab->inuse;
	} else {
		/*
		 * For a long list, approximate the total count of objects in
		 * it to meet the limit on the number of slabs to scan.
		 * Scan from both the list's head and tail for better accuracy.
		 */
		unsigned long scanned = 0;

		list_for_each_entry(slab, &n->partial, slab_list) {
			x += slab->objects - slab->inuse;
			if (++scanned == MAX_PARTIAL_TO_SCAN / 2)
				break;
		}
		list_for_each_entry_reverse(slab, &n->partial, slab_list) {
			x += slab->objects - slab->inuse;
			if (++scanned == MAX_PARTIAL_TO_SCAN)
				break;
		}
		x = mult_frac(x, n->nr_partial, scanned);
		x = min(x, node_nr_objs(n));
	}
	spin_unlock_irqrestore(&n->list_lock, flags);
	return x;
}

static noinline void
slab_out_of_memory(struct kmem_cache *s, gfp_t gfpflags, int nid)
{
	static DEFINE_RATELIMIT_STATE(slub_oom_rs, DEFAULT_RATELIMIT_INTERVAL,
				      DEFAULT_RATELIMIT_BURST);
	int cpu = raw_smp_processor_id();
	int node;
	struct kmem_cache_node *n;

	if ((gfpflags & __GFP_NOWARN) || !__ratelimit(&slub_oom_rs))
		return;

	pr_warn("SLUB: Unable to allocate memory on CPU %u (of node %d) on node %d, gfp=%#x(%pGg)\n",
		cpu, cpu_to_node(cpu), nid, gfpflags, &gfpflags);
	pr_warn("  cache: %s, object size: %u, buffer size: %u, default order: %u, min order: %u\n",
		s->name, s->object_size, s->size, oo_order(s->oo),
		oo_order(s->min));

	if (oo_order(s->min) > get_order(s->object_size))
		pr_warn("  %s debugging increased min order, use slab_debug=O to disable.\n",
			s->name);

	for_each_kmem_cache_node(s, node, n) {
		unsigned long nr_slabs;
		unsigned long nr_objs;
		unsigned long nr_free;

		nr_free  = count_partial_free_approx(n);
		nr_slabs = node_nr_slabs(n);
		nr_objs  = node_nr_objs(n);

		pr_warn("  node %d: slabs: %ld, objs: %ld, free: %ld\n",
			node, nr_slabs, nr_objs, nr_free);
	}
}
#else /* CONFIG_SLUB_DEBUG */
static inline void
slab_out_of_memory(struct kmem_cache *s, gfp_t gfpflags, int nid) { }
#endif

static inline bool pfmemalloc_match(struct slab *slab, gfp_t gfpflags)
{
	if (unlikely(slab_test_pfmemalloc(slab)))
		return gfp_pfmemalloc_allowed(gfpflags);

	return true;
}

/*
 * Get the slab's freelist and do not freeze it.
 *
 * Assumes the slab is isolated from node partial list and not frozen.
 *
 * Assumes this is performed only for caches without debugging so we
 * don't need to worry about adding the slab to the full list.
 */
static inline void *get_freelist_nofreeze(struct kmem_cache *s, struct slab *slab)
{
	struct freelist_counters old, new;

	do {
		old.freelist = slab->freelist;
		old.counters = slab->counters;

		new.freelist = NULL;
		new.counters = old.counters;
		VM_WARN_ON_ONCE(new.frozen);

		new.inuse = old.objects;

	} while (!slab_update_freelist(s, slab, &old, &new, "get_freelist_nofreeze"));

	return old.freelist;
}

/*
 * If the object has been wiped upon free, make sure it's fully initialized by
 * zeroing out freelist pointer.
 *
 * Note that we also wipe custom freelist pointers.
 */
static __always_inline void maybe_wipe_obj_freeptr(struct kmem_cache *s,
						   void *obj)
{
	if (unlikely(slab_want_init_on_free(s)) && obj &&
	    !freeptr_outside_object(s))
		memset((void *)((char *)kasan_reset_tag(obj) + s->offset),
			0, sizeof(void *));
}

static unsigned int alloc_from_new_slab(struct kmem_cache *s, struct slab *slab,
		void **p, unsigned int count, bool allow_spin)
{
	unsigned int allocated = 0;
	struct kmem_cache_node *n;
	bool needs_add_partial;
	unsigned long flags;
	void *object;

	/*
	 * Are we going to put the slab on the partial list?
	 * Note slab->inuse is 0 on a new slab.
	 */
	needs_add_partial = (slab->objects > count);

	if (!allow_spin && needs_add_partial) {

		n = get_node(s, slab_nid(slab));

		if (!spin_trylock_irqsave(&n->list_lock, flags)) {
			/* Unlucky, discard newly allocated slab */
			free_new_slab_nolock(s, slab);
			return 0;
		}
	}

	object = slab->freelist;
	while (object && allocated < count) {
		p[allocated] = object;
		object = get_freepointer(s, object);
		maybe_wipe_obj_freeptr(s, p[allocated]);

		slab->inuse++;
		allocated++;
	}
	slab->freelist = object;

	if (needs_add_partial) {

		if (allow_spin) {
			n = get_node(s, slab_nid(slab));
			spin_lock_irqsave(&n->list_lock, flags);
		}
		add_partial(n, slab, ADD_TO_HEAD);
		spin_unlock_irqrestore(&n->list_lock, flags);
	}

	inc_slabs_node(s, slab_nid(slab), slab->objects);
	return allocated;
}

/*
 * Slow path. We failed to allocate via percpu sheaves or they are not available
 * due to bootstrap or debugging enabled or SLUB_TINY.
 *
 * We try to allocate from partial slab lists and fall back to allocating a new
 * slab.
 */
static void *___slab_alloc(struct kmem_cache *s, gfp_t gfpflags, int node,
			   unsigned long addr, unsigned int orig_size)
{
	bool allow_spin = gfpflags_allow_spinning(gfpflags);
	void *object;
	struct slab *slab;
	struct partial_context pc;
	bool try_thisnode = true;

	stat(s, ALLOC_SLOWPATH);

new_objects:

	pc.flags = gfpflags;
	/*
	 * When a preferred node is indicated but no __GFP_THISNODE
	 *
	 * 1) try to get a partial slab from target node only by having
	 *    __GFP_THISNODE in pc.flags for get_from_partial()
	 * 2) if 1) failed, try to allocate a new slab from target node with
	 *    GPF_NOWAIT | __GFP_THISNODE opportunistically
	 * 3) if 2) failed, retry with original gfpflags which will allow
	 *    get_from_partial() try partial lists of other nodes before
	 *    potentially allocating new page from other nodes
	 */
	if (unlikely(node != NUMA_NO_NODE && !(gfpflags & __GFP_THISNODE)
		     && try_thisnode)) {
		if (unlikely(!allow_spin))
			/* Do not upgrade gfp to NOWAIT from more restrictive mode */
			pc.flags = gfpflags | __GFP_THISNODE;
		else
			pc.flags = GFP_NOWAIT | __GFP_THISNODE;
	}

	pc.orig_size = orig_size;
	object = get_from_partial(s, node, &pc);
	if (object)
		goto success;

	slab = new_slab(s, pc.flags, node);

	if (unlikely(!slab)) {
		if (node != NUMA_NO_NODE && !(gfpflags & __GFP_THISNODE)
		    && try_thisnode) {
			try_thisnode = false;
			goto new_objects;
		}
		slab_out_of_memory(s, gfpflags, node);
		return NULL;
	}

	stat(s, ALLOC_SLAB);

	if (IS_ENABLED(CONFIG_SLUB_TINY) || kmem_cache_debug(s)) {
		object = alloc_single_from_new_slab(s, slab, orig_size, gfpflags);

		if (likely(object))
			goto success;
	} else {
		alloc_from_new_slab(s, slab, &object, 1, allow_spin);

		/* we don't need to check SLAB_STORE_USER here */
		if (likely(object))
			return object;
	}

	if (allow_spin)
		goto new_objects;

	/* This could cause an endless loop. Fail instead. */
	return NULL;

success:
	if (kmem_cache_debug_flags(s, SLAB_STORE_USER))
		set_track(s, object, TRACK_ALLOC, addr, gfpflags);

	return object;
}

static __always_inline void *__slab_alloc_node(struct kmem_cache *s,
		gfp_t gfpflags, int node, unsigned long addr, size_t orig_size)
{
	void *object;

#ifdef CONFIG_NUMA
	if (static_branch_unlikely(&strict_numa) &&
			node == NUMA_NO_NODE) {

		struct mempolicy *mpol = current->mempolicy;

		if (mpol) {
			/*
			 * Special BIND rule support. If the local node
			 * is in permitted set then do not redirect
			 * to a particular node.
			 * Otherwise we apply the memory policy to get
			 * the node we need to allocate on.
			 */
			if (mpol->mode != MPOL_BIND ||
					!node_isset(numa_mem_id(), mpol->nodes))
				node = mempolicy_slab_node();
		}
	}
#endif

	object = ___slab_alloc(s, gfpflags, node, addr, orig_size);

	return object;
}

static __fastpath_inline
struct kmem_cache *slab_pre_alloc_hook(struct kmem_cache *s, gfp_t flags)
{
	flags &= gfp_allowed_mask;

	might_alloc(flags);

	if (unlikely(should_failslab(s, flags)))
		return NULL;

	return s;
}

static __fastpath_inline
bool slab_post_alloc_hook(struct kmem_cache *s, struct list_lru *lru,
			  gfp_t flags, size_t size, void **p, bool init,
			  unsigned int orig_size)
{
	unsigned int zero_size = s->object_size;
	bool kasan_init = init;
	size_t i;
	gfp_t init_flags = flags & gfp_allowed_mask;

	/*
	 * For kmalloc object, the allocated memory size(object_size) is likely
	 * larger than the requested size(orig_size). If redzone check is
	 * enabled for the extra space, don't zero it, as it will be redzoned
	 * soon. The redzone operation for this extra space could be seen as a
	 * replacement of current poisoning under certain debug option, and
	 * won't break other sanity checks.
	 */
	if (kmem_cache_debug_flags(s, SLAB_STORE_USER | SLAB_RED_ZONE) &&
	    (s->flags & SLAB_KMALLOC))
		zero_size = orig_size;

	/*
	 * When slab_debug is enabled, avoid memory initialization integrated
	 * into KASAN and instead zero out the memory via the memset below with
	 * the proper size. Otherwise, KASAN might overwrite SLUB redzones and
	 * cause false-positive reports. This does not lead to a performance
	 * penalty on production builds, as slab_debug is not intended to be
	 * enabled there.
	 */
	if (__slub_debug_enabled())
		kasan_init = false;

	/*
	 * As memory initialization might be integrated into KASAN,
	 * kasan_slab_alloc and initialization memset must be
	 * kept together to avoid discrepancies in behavior.
	 *
	 * As p[i] might get tagged, memset and kmemleak hook come after KASAN.
	 */
	for (i = 0; i < size; i++) {
		p[i] = kasan_slab_alloc(s, p[i], init_flags, kasan_init);
		if (p[i] && init && (!kasan_init ||
				     !kasan_has_integrated_init()))
			memset(p[i], 0, zero_size);
		if (gfpflags_allow_spinning(flags))
			kmemleak_alloc_recursive(p[i], s->object_size, 1,
						 s->flags, init_flags);
		kmsan_slab_alloc(s, p[i], init_flags);
		alloc_tagging_slab_alloc_hook(s, p[i], flags);
	}

	return memcg_slab_post_alloc_hook(s, lru, flags, size, p);
}

/*
 * Replace the empty main sheaf with a (at least partially) full sheaf.
 *
 * Must be called with the cpu_sheaves local lock locked. If successful, returns
 * the pcs pointer and the local lock locked (possibly on a different cpu than
 * initially called). If not successful, returns NULL and the local lock
 * unlocked.
 */
static struct slub_percpu_sheaves *
__pcs_replace_empty_main(struct kmem_cache *s, struct slub_percpu_sheaves *pcs, gfp_t gfp)
{
	struct slab_sheaf *empty = NULL;
	struct slab_sheaf *full;
	struct node_barn *barn;
	bool can_alloc;

	lockdep_assert_held(this_cpu_ptr(&s->cpu_sheaves->lock));

	/* Bootstrap or debug cache, back off */
	if (unlikely(!cache_has_sheaves(s))) {
		local_unlock(&s->cpu_sheaves->lock);
		return NULL;
	}

	if (pcs->spare && pcs->spare->size > 0) {
		swap(pcs->main, pcs->spare);
		return pcs;
	}

	barn = get_barn(s);
	if (!barn) {
		local_unlock(&s->cpu_sheaves->lock);
		return NULL;
	}

	full = barn_replace_empty_sheaf(barn, pcs->main,
					gfpflags_allow_spinning(gfp));

	if (full) {
		stat(s, BARN_GET);
		pcs->main = full;
		return pcs;
	}

	stat(s, BARN_GET_FAIL);

	can_alloc = gfpflags_allow_blocking(gfp);

	if (can_alloc) {
		if (pcs->spare) {
			empty = pcs->spare;
			pcs->spare = NULL;
		} else {
			empty = barn_get_empty_sheaf(barn, true);
		}
	}

	local_unlock(&s->cpu_sheaves->lock);

	if (!can_alloc)
		return NULL;

	if (empty) {
		if (!refill_sheaf(s, empty, gfp | __GFP_NOMEMALLOC)) {
			full = empty;
		} else {
			/*
			 * we must be very low on memory so don't bother
			 * with the barn
			 */
			free_empty_sheaf(s, empty);
		}
	} else {
		full = alloc_full_sheaf(s, gfp);
	}

	if (!full)
		return NULL;

	/*
	 * we can reach here only when gfpflags_allow_blocking
	 * so this must not be an irq
	 */
	local_lock(&s->cpu_sheaves->lock);
	pcs = this_cpu_ptr(s->cpu_sheaves);

	/*
	 * If we are returning empty sheaf, we either got it from the
	 * barn or had to allocate one. If we are returning a full
	 * sheaf, it's due to racing or being migrated to a different
	 * cpu. Breaching the barn's sheaf limits should be thus rare
	 * enough so just ignore them to simplify the recovery.
	 */

	if (pcs->main->size == 0) {
		if (!pcs->spare)
			pcs->spare = pcs->main;
		else
			barn_put_empty_sheaf(barn, pcs->main);
		pcs->main = full;
		return pcs;
	}

	if (!pcs->spare) {
		pcs->spare = full;
		return pcs;
	}

	if (pcs->spare->size == 0) {
		barn_put_empty_sheaf(barn, pcs->spare);
		pcs->spare = full;
		return pcs;
	}

	barn_put_full_sheaf(barn, full);
	stat(s, BARN_PUT);

	return pcs;
}

static __fastpath_inline
void *alloc_from_pcs(struct kmem_cache *s, gfp_t gfp, int node)
{
	struct slub_percpu_sheaves *pcs;
	bool node_requested;
	void *object;

#ifdef CONFIG_NUMA
	if (static_branch_unlikely(&strict_numa) &&
			 node == NUMA_NO_NODE) {

		struct mempolicy *mpol = current->mempolicy;

		if (mpol) {
			/*
			 * Special BIND rule support. If the local node
			 * is in permitted set then do not redirect
			 * to a particular node.
			 * Otherwise we apply the memory policy to get
			 * the node we need to allocate on.
			 */
			if (mpol->mode != MPOL_BIND ||
					!node_isset(numa_mem_id(), mpol->nodes))

				node = mempolicy_slab_node();
		}
	}
#endif

	node_requested = IS_ENABLED(CONFIG_NUMA) && node != NUMA_NO_NODE;

	/*
	 * We assume the percpu sheaves contain only local objects although it's
	 * not completely guaranteed, so we verify later.
	 */
	if (unlikely(node_requested && node != numa_mem_id())) {
		stat(s, ALLOC_NODE_MISMATCH);
		return NULL;
	}

	if (!local_trylock(&s->cpu_sheaves->lock))
		return NULL;

	pcs = this_cpu_ptr(s->cpu_sheaves);

	if (unlikely(pcs->main->size == 0)) {
		pcs = __pcs_replace_empty_main(s, pcs, gfp);
		if (unlikely(!pcs))
			return NULL;
	}

	object = pcs->main->objects[pcs->main->size - 1];

	if (unlikely(node_requested)) {
		/*
		 * Verify that the object was from the node we want. This could
		 * be false because of cpu migration during an unlocked part of
		 * the current allocation or previous freeing process.
		 */
		if (page_to_nid(virt_to_page(object)) != node) {
			local_unlock(&s->cpu_sheaves->lock);
			stat(s, ALLOC_NODE_MISMATCH);
			return NULL;
		}
	}

	pcs->main->size--;

	local_unlock(&s->cpu_sheaves->lock);

	stat(s, ALLOC_FASTPATH);

	return object;
}

static __fastpath_inline
unsigned int alloc_from_pcs_bulk(struct kmem_cache *s, gfp_t gfp, size_t size,
				 void **p)
{
	struct slub_percpu_sheaves *pcs;
	struct slab_sheaf *main;
	unsigned int allocated = 0;
	unsigned int batch;

next_batch:
	if (!local_trylock(&s->cpu_sheaves->lock))
		return allocated;

	pcs = this_cpu_ptr(s->cpu_sheaves);

	if (unlikely(pcs->main->size == 0)) {

		struct slab_sheaf *full;
		struct node_barn *barn;

		if (unlikely(!cache_has_sheaves(s))) {
			local_unlock(&s->cpu_sheaves->lock);
			return allocated;
		}

		if (pcs->spare && pcs->spare->size > 0) {
			swap(pcs->main, pcs->spare);
			goto do_alloc;
		}

		barn = get_barn(s);
		if (!barn) {
			local_unlock(&s->cpu_sheaves->lock);
			return allocated;
		}

		full = barn_replace_empty_sheaf(barn, pcs->main,
						gfpflags_allow_spinning(gfp));

		if (full) {
			stat(s, BARN_GET);
			pcs->main = full;
			goto do_alloc;
		}

		stat(s, BARN_GET_FAIL);

		local_unlock(&s->cpu_sheaves->lock);

		/*
		 * Once full sheaves in barn are depleted, let the bulk
		 * allocation continue from slab pages, otherwise we would just
		 * be copying arrays of pointers twice.
		 */
		return allocated;
	}

do_alloc:

	main = pcs->main;
	batch = min(size, main->size);

	main->size -= batch;
	memcpy(p, main->objects + main->size, batch * sizeof(void *));

	local_unlock(&s->cpu_sheaves->lock);

	stat_add(s, ALLOC_FASTPATH, batch);

	allocated += batch;

	if (batch < size) {
		p += batch;
		size -= batch;
		goto next_batch;
	}

	return allocated;
}


/*
 * Inlined fastpath so that allocation functions (kmalloc, kmem_cache_alloc)
 * have the fastpath folded into their functions. So no function call
 * overhead for requests that can be satisfied on the fastpath.
 *
 * The fastpath works by first checking if the lockless freelist can be used.
 * If not then __slab_alloc is called for slow processing.
 *
 * Otherwise we can simply pick the next object from the lockless free list.
 */
static __fastpath_inline void *slab_alloc_node(struct kmem_cache *s, struct list_lru *lru,
		gfp_t gfpflags, int node, unsigned long addr, size_t orig_size)
{
	void *object;
	bool init = false;

	s = slab_pre_alloc_hook(s, gfpflags);
	if (unlikely(!s))
		return NULL;

	object = kfence_alloc(s, orig_size, gfpflags);
	if (unlikely(object))
		goto out;

	object = alloc_from_pcs(s, gfpflags, node);

	if (!object)
		object = __slab_alloc_node(s, gfpflags, node, addr, orig_size);

	maybe_wipe_obj_freeptr(s, object);
	init = slab_want_init_on_alloc(gfpflags, s);

out:
	/*
	 * When init equals 'true', like for kzalloc() family, only
	 * @orig_size bytes might be zeroed instead of s->object_size
	 * In case this fails due to memcg_slab_post_alloc_hook(),
	 * object is set to NULL
	 */
	slab_post_alloc_hook(s, lru, gfpflags, 1, &object, init, orig_size);

	return object;
}

void *kmem_cache_alloc_noprof(struct kmem_cache *s, gfp_t gfpflags)
{
	void *ret = slab_alloc_node(s, NULL, gfpflags, NUMA_NO_NODE, _RET_IP_,
				    s->object_size);

	trace_kmem_cache_alloc(_RET_IP_, ret, s, gfpflags, NUMA_NO_NODE);

	return ret;
}
EXPORT_SYMBOL(kmem_cache_alloc_noprof);

void *kmem_cache_alloc_lru_noprof(struct kmem_cache *s, struct list_lru *lru,
			   gfp_t gfpflags)
{
	void *ret = slab_alloc_node(s, lru, gfpflags, NUMA_NO_NODE, _RET_IP_,
				    s->object_size);

	trace_kmem_cache_alloc(_RET_IP_, ret, s, gfpflags, NUMA_NO_NODE);

	return ret;
}
EXPORT_SYMBOL(kmem_cache_alloc_lru_noprof);

bool kmem_cache_charge(void *objp, gfp_t gfpflags)
{
	if (!memcg_kmem_online())
		return true;

	return memcg_slab_post_charge(objp, gfpflags);
}
EXPORT_SYMBOL(kmem_cache_charge);

/**
 * kmem_cache_alloc_node - Allocate an object on the specified node
 * @s: The cache to allocate from.
 * @gfpflags: See kmalloc().
 * @node: node number of the target node.
 *
 * Identical to kmem_cache_alloc but it will allocate memory on the given
 * node, which can improve the performance for cpu bound structures.
 *
 * Fallback to other node is possible if __GFP_THISNODE is not set.
 *
 * Return: pointer to the new object or %NULL in case of error
 */
void *kmem_cache_alloc_node_noprof(struct kmem_cache *s, gfp_t gfpflags, int node)
{
	void *ret = slab_alloc_node(s, NULL, gfpflags, node, _RET_IP_, s->object_size);

	trace_kmem_cache_alloc(_RET_IP_, ret, s, gfpflags, node);

	return ret;
}
EXPORT_SYMBOL(kmem_cache_alloc_node_noprof);

static int __prefill_sheaf_pfmemalloc(struct kmem_cache *s,
				      struct slab_sheaf *sheaf, gfp_t gfp)
{
	int ret = 0;

	ret = refill_sheaf(s, sheaf, gfp | __GFP_NOMEMALLOC);

	if (likely(!ret || !gfp_pfmemalloc_allowed(gfp)))
		return ret;

	/*
	 * if we are allowed to, refill sheaf with pfmemalloc but then remember
	 * it for when it's returned
	 */
	ret = refill_sheaf(s, sheaf, gfp);
	sheaf->pfmemalloc = true;

	return ret;
}

static int __kmem_cache_alloc_bulk(struct kmem_cache *s, gfp_t flags,
				   size_t size, void **p);

/*
 * returns a sheaf that has at least the requested size
 * when prefilling is needed, do so with given gfp flags
 *
 * return NULL if sheaf allocation or prefilling failed
 */
struct slab_sheaf *
kmem_cache_prefill_sheaf(struct kmem_cache *s, gfp_t gfp, unsigned int size)
{
	struct slub_percpu_sheaves *pcs;
	struct slab_sheaf *sheaf = NULL;
	struct node_barn *barn;

	if (unlikely(!size))
		return NULL;

	if (unlikely(size > s->sheaf_capacity)) {

		sheaf = kzalloc(struct_size(sheaf, objects, size), gfp);
		if (!sheaf)
			return NULL;

		stat(s, SHEAF_PREFILL_OVERSIZE);
		sheaf->cache = s;
		sheaf->capacity = size;

		/*
		 * we do not need to care about pfmemalloc here because oversize
		 * sheaves area always flushed and freed when returned
		 */
		if (!__kmem_cache_alloc_bulk(s, gfp, size,
					     &sheaf->objects[0])) {
			kfree(sheaf);
			return NULL;
		}

		sheaf->size = size;

		return sheaf;
	}

	local_lock(&s->cpu_sheaves->lock);
	pcs = this_cpu_ptr(s->cpu_sheaves);

	if (pcs->spare) {
		sheaf = pcs->spare;
		pcs->spare = NULL;
		stat(s, SHEAF_PREFILL_FAST);
	} else {
		barn = get_barn(s);

		stat(s, SHEAF_PREFILL_SLOW);
		if (barn)
			sheaf = barn_get_full_or_empty_sheaf(barn);
		if (sheaf && sheaf->size)
			stat(s, BARN_GET);
		else
			stat(s, BARN_GET_FAIL);
	}

	local_unlock(&s->cpu_sheaves->lock);


	if (!sheaf)
		sheaf = alloc_empty_sheaf(s, gfp);

	if (sheaf) {
		sheaf->capacity = s->sheaf_capacity;
		sheaf->pfmemalloc = false;

		if (sheaf->size < size &&
		    __prefill_sheaf_pfmemalloc(s, sheaf, gfp)) {
			sheaf_flush_unused(s, sheaf);
			free_empty_sheaf(s, sheaf);
			sheaf = NULL;
		}
	}

	return sheaf;
}

/*
 * Use this to return a sheaf obtained by kmem_cache_prefill_sheaf()
 *
 * If the sheaf cannot simply become the percpu spare sheaf, but there's space
 * for a full sheaf in the barn, we try to refill the sheaf back to the cache's
 * sheaf_capacity to avoid handling partially full sheaves.
 *
 * If the refill fails because gfp is e.g. GFP_NOWAIT, or the barn is full, the
 * sheaf is instead flushed and freed.
 */
void kmem_cache_return_sheaf(struct kmem_cache *s, gfp_t gfp,
			     struct slab_sheaf *sheaf)
{
	struct slub_percpu_sheaves *pcs;
	struct node_barn *barn;

	if (unlikely((sheaf->capacity != s->sheaf_capacity)
		     || sheaf->pfmemalloc)) {
		sheaf_flush_unused(s, sheaf);
		kfree(sheaf);
		return;
	}

	local_lock(&s->cpu_sheaves->lock);
	pcs = this_cpu_ptr(s->cpu_sheaves);
	barn = get_barn(s);

	if (!pcs->spare) {
		pcs->spare = sheaf;
		sheaf = NULL;
		stat(s, SHEAF_RETURN_FAST);
	}

	local_unlock(&s->cpu_sheaves->lock);

	if (!sheaf)
		return;

	stat(s, SHEAF_RETURN_SLOW);

	/*
	 * If the barn has too many full sheaves or we fail to refill the sheaf,
	 * simply flush and free it.
	 */
	if (!barn || data_race(barn->nr_full) >= MAX_FULL_SHEAVES ||
	    refill_sheaf(s, sheaf, gfp)) {
		sheaf_flush_unused(s, sheaf);
		free_empty_sheaf(s, sheaf);
		return;
	}

	barn_put_full_sheaf(barn, sheaf);
	stat(s, BARN_PUT);
}

/*
 * refill a sheaf previously returned by kmem_cache_prefill_sheaf to at least
 * the given size
 *
 * the sheaf might be replaced by a new one when requesting more than
 * s->sheaf_capacity objects if such replacement is necessary, but the refill
 * fails (returning -ENOMEM), the existing sheaf is left intact
 *
 * In practice we always refill to full sheaf's capacity.
 */
int kmem_cache_refill_sheaf(struct kmem_cache *s, gfp_t gfp,
			    struct slab_sheaf **sheafp, unsigned int size)
{
	struct slab_sheaf *sheaf;

	/*
	 * TODO: do we want to support *sheaf == NULL to be equivalent of
	 * kmem_cache_prefill_sheaf() ?
	 */
	if (!sheafp || !(*sheafp))
		return -EINVAL;

	sheaf = *sheafp;
	if (sheaf->size >= size)
		return 0;

	if (likely(sheaf->capacity >= size)) {
		if (likely(sheaf->capacity == s->sheaf_capacity))
			return __prefill_sheaf_pfmemalloc(s, sheaf, gfp);

		if (!__kmem_cache_alloc_bulk(s, gfp, sheaf->capacity - sheaf->size,
					     &sheaf->objects[sheaf->size])) {
			return -ENOMEM;
		}
		sheaf->size = sheaf->capacity;

		return 0;
	}

	/*
	 * We had a regular sized sheaf and need an oversize one, or we had an
	 * oversize one already but need a larger one now.
	 * This should be a very rare path so let's not complicate it.
	 */
	sheaf = kmem_cache_prefill_sheaf(s, gfp, size);
	if (!sheaf)
		return -ENOMEM;

	kmem_cache_return_sheaf(s, gfp, *sheafp);
	*sheafp = sheaf;
	return 0;
}

/*
 * Allocate from a sheaf obtained by kmem_cache_prefill_sheaf()
 *
 * Guaranteed not to fail as many allocations as was the requested size.
 * After the sheaf is emptied, it fails - no fallback to the slab cache itself.
 *
 * The gfp parameter is meant only to specify __GFP_ZERO or __GFP_ACCOUNT
 * memcg charging is forced over limit if necessary, to avoid failure.
 *
 * It is possible that the allocation comes from kfence and then the sheaf
 * size is not decreased.
 */
void *
kmem_cache_alloc_from_sheaf_noprof(struct kmem_cache *s, gfp_t gfp,
				   struct slab_sheaf *sheaf)
{
	void *ret = NULL;
	bool init;

	if (sheaf->size == 0)
		goto out;

	ret = kfence_alloc(s, s->object_size, gfp);

	if (likely(!ret))
		ret = sheaf->objects[--sheaf->size];

	init = slab_want_init_on_alloc(gfp, s);

	/* add __GFP_NOFAIL to force successful memcg charging */
	slab_post_alloc_hook(s, NULL, gfp | __GFP_NOFAIL, 1, &ret, init, s->object_size);
out:
	trace_kmem_cache_alloc(_RET_IP_, ret, s, gfp, NUMA_NO_NODE);

	return ret;
}

unsigned int kmem_cache_sheaf_size(struct slab_sheaf *sheaf)
{
	return sheaf->size;
}
/*
 * To avoid unnecessary overhead, we pass through large allocation requests
 * directly to the page allocator. We use __GFP_COMP, because we will need to
 * know the allocation order to free the pages properly in kfree.
 */
static void *___kmalloc_large_node(size_t size, gfp_t flags, int node)
{
	struct page *page;
	void *ptr = NULL;
	unsigned int order = get_order(size);

	if (unlikely(flags & GFP_SLAB_BUG_MASK))
		flags = kmalloc_fix_flags(flags);

	flags |= __GFP_COMP;

	if (node == NUMA_NO_NODE)
		page = alloc_frozen_pages_noprof(flags, order);
	else
		page = __alloc_frozen_pages_noprof(flags, order, node, NULL);

	if (page) {
		ptr = page_address(page);
		mod_lruvec_page_state(page, NR_SLAB_UNRECLAIMABLE_B,
				      PAGE_SIZE << order);
		__SetPageLargeKmalloc(page);
	}

	ptr = kasan_kmalloc_large(ptr, size, flags);
	/* As ptr might get tagged, call kmemleak hook after KASAN. */
	kmemleak_alloc(ptr, size, 1, flags);
	kmsan_kmalloc_large(ptr, size, flags);

	return ptr;
}

void *__kmalloc_large_noprof(size_t size, gfp_t flags)
{
	void *ret = ___kmalloc_large_node(size, flags, NUMA_NO_NODE);

	trace_kmalloc(_RET_IP_, ret, size, PAGE_SIZE << get_order(size),
		      flags, NUMA_NO_NODE);
	return ret;
}
EXPORT_SYMBOL(__kmalloc_large_noprof);

void *__kmalloc_large_node_noprof(size_t size, gfp_t flags, int node)
{
	void *ret = ___kmalloc_large_node(size, flags, node);

	trace_kmalloc(_RET_IP_, ret, size, PAGE_SIZE << get_order(size),
		      flags, node);
	return ret;
}
EXPORT_SYMBOL(__kmalloc_large_node_noprof);

static __always_inline
void *__do_kmalloc_node(size_t size, kmem_buckets *b, gfp_t flags, int node,
			unsigned long caller)
{
	struct kmem_cache *s;
	void *ret;

	if (unlikely(size > KMALLOC_MAX_CACHE_SIZE)) {
		ret = __kmalloc_large_node_noprof(size, flags, node);
		trace_kmalloc(caller, ret, size,
			      PAGE_SIZE << get_order(size), flags, node);
		return ret;
	}

	if (unlikely(!size))
		return ZERO_SIZE_PTR;

	s = kmalloc_slab(size, b, flags, caller);

	ret = slab_alloc_node(s, NULL, flags, node, caller, size);
	ret = kasan_kmalloc(s, ret, size, flags);
	trace_kmalloc(caller, ret, size, s->size, flags, node);
	return ret;
}
void *__kmalloc_node_noprof(DECL_BUCKET_PARAMS(size, b), gfp_t flags, int node)
{
	return __do_kmalloc_node(size, PASS_BUCKET_PARAM(b), flags, node, _RET_IP_);
}
EXPORT_SYMBOL(__kmalloc_node_noprof);

void *__kmalloc_noprof(size_t size, gfp_t flags)
{
	return __do_kmalloc_node(size, NULL, flags, NUMA_NO_NODE, _RET_IP_);
}
EXPORT_SYMBOL(__kmalloc_noprof);

/**
 * kmalloc_nolock - Allocate an object of given size from any context.
 * @size: size to allocate
 * @gfp_flags: GFP flags. Only __GFP_ACCOUNT, __GFP_ZERO, __GFP_NO_OBJ_EXT
 * allowed.
 * @node: node number of the target node.
 *
 * Return: pointer to the new object or NULL in case of error.
 * NULL does not mean EBUSY or EAGAIN. It means ENOMEM.
 * There is no reason to call it again and expect !NULL.
 */
void *kmalloc_nolock_noprof(size_t size, gfp_t gfp_flags, int node)
{
	gfp_t alloc_gfp = __GFP_NOWARN | __GFP_NOMEMALLOC | gfp_flags;
	struct kmem_cache *s;
	bool can_retry = true;
	void *ret;

	VM_WARN_ON_ONCE(gfp_flags & ~(__GFP_ACCOUNT | __GFP_ZERO |
				      __GFP_NO_OBJ_EXT));

	if (unlikely(!size))
		return ZERO_SIZE_PTR;

	/*
	 * See the comment for the same check in
	 * alloc_frozen_pages_nolock_noprof()
	 */
	if (IS_ENABLED(CONFIG_PREEMPT_RT) && (in_nmi() || in_hardirq()))
		return NULL;

retry:
	if (unlikely(size > KMALLOC_MAX_CACHE_SIZE))
		return NULL;
	s = kmalloc_slab(size, NULL, alloc_gfp, _RET_IP_);

	if (!(s->flags & __CMPXCHG_DOUBLE) && !kmem_cache_debug(s))
		/*
		 * kmalloc_nolock() is not supported on architectures that
		 * don't implement cmpxchg16b and thus need slab_lock()
		 * which could be preempted by a nmi.
		 * But debug caches don't use that and only rely on
		 * kmem_cache_node->list_lock, so kmalloc_nolock() can attempt
		 * to allocate from debug caches by
		 * spin_trylock_irqsave(&n->list_lock, ...)
		 */
		return NULL;

	ret = alloc_from_pcs(s, alloc_gfp, node);
	if (ret)
		goto success;

	/*
	 * Do not call slab_alloc_node(), since trylock mode isn't
	 * compatible with slab_pre_alloc_hook/should_failslab and
	 * kfence_alloc. Hence call __slab_alloc_node() (at most twice)
	 * and slab_post_alloc_hook() directly.
	 */
	ret = __slab_alloc_node(s, alloc_gfp, node, _RET_IP_, size);

	/*
	 * It's possible we failed due to trylock as we preempted someone with
	 * the sheaves locked, and the list_lock is also held by another cpu.
	 * But it should be rare that multiple kmalloc buckets would have
	 * sheaves locked, so try a larger one.
	 */
	if (!ret && can_retry) {
		/* pick the next kmalloc bucket */
		size = s->object_size + 1;
		/*
		 * Another alternative is to
		 * if (memcg) alloc_gfp &= ~__GFP_ACCOUNT;
		 * else if (!memcg) alloc_gfp |= __GFP_ACCOUNT;
		 * to retry from bucket of the same size.
		 */
		can_retry = false;
		goto retry;
	}

success:
	maybe_wipe_obj_freeptr(s, ret);
	slab_post_alloc_hook(s, NULL, alloc_gfp, 1, &ret,
			     slab_want_init_on_alloc(alloc_gfp, s), size);

	ret = kasan_kmalloc(s, ret, size, alloc_gfp);
	return ret;
}
EXPORT_SYMBOL_GPL(kmalloc_nolock_noprof);

void *__kmalloc_node_track_caller_noprof(DECL_BUCKET_PARAMS(size, b), gfp_t flags,
					 int node, unsigned long caller)
{
	return __do_kmalloc_node(size, PASS_BUCKET_PARAM(b), flags, node, caller);

}
EXPORT_SYMBOL(__kmalloc_node_track_caller_noprof);

void *__kmalloc_cache_noprof(struct kmem_cache *s, gfp_t gfpflags, size_t size)
{
	void *ret = slab_alloc_node(s, NULL, gfpflags, NUMA_NO_NODE,
					    _RET_IP_, size);

	trace_kmalloc(_RET_IP_, ret, size, s->size, gfpflags, NUMA_NO_NODE);

	ret = kasan_kmalloc(s, ret, size, gfpflags);
	return ret;
}
EXPORT_SYMBOL(__kmalloc_cache_noprof);

void *__kmalloc_cache_node_noprof(struct kmem_cache *s, gfp_t gfpflags,
				  int node, size_t size)
{
	void *ret = slab_alloc_node(s, NULL, gfpflags, node, _RET_IP_, size);

	trace_kmalloc(_RET_IP_, ret, size, s->size, gfpflags, node);

	ret = kasan_kmalloc(s, ret, size, gfpflags);
	return ret;
}
EXPORT_SYMBOL(__kmalloc_cache_node_noprof);

static noinline void free_to_partial_list(
	struct kmem_cache *s, struct slab *slab,
	void *head, void *tail, int bulk_cnt,
	unsigned long addr)
{
	struct kmem_cache_node *n = get_node(s, slab_nid(slab));
	struct slab *slab_free = NULL;
	int cnt = bulk_cnt;
	unsigned long flags;
	depot_stack_handle_t handle = 0;

	/*
	 * We cannot use GFP_NOWAIT as there are callsites where waking up
	 * kswapd could deadlock
	 */
	if (s->flags & SLAB_STORE_USER)
		handle = set_track_prepare(__GFP_NOWARN);

	spin_lock_irqsave(&n->list_lock, flags);

	if (free_debug_processing(s, slab, head, tail, &cnt, addr, handle)) {
		void *prior = slab->freelist;

		/* Perform the actual freeing while we still hold the locks */
		slab->inuse -= cnt;
		set_freepointer(s, tail, prior);
		slab->freelist = head;

		/*
		 * If the slab is empty, and node's partial list is full,
		 * it should be discarded anyway no matter it's on full or
		 * partial list.
		 */
		if (slab->inuse == 0 && n->nr_partial >= s->min_partial)
			slab_free = slab;

		if (!prior) {
			/* was on full list */
			remove_full(s, n, slab);
			if (!slab_free) {
				add_partial(n, slab, ADD_TO_TAIL);
				stat(s, FREE_ADD_PARTIAL);
			}
		} else if (slab_free) {
			remove_partial(n, slab);
			stat(s, FREE_REMOVE_PARTIAL);
		}
	}

	if (slab_free) {
		/*
		 * Update the counters while still holding n->list_lock to
		 * prevent spurious validation warnings
		 */
		dec_slabs_node(s, slab_nid(slab_free), slab_free->objects);
	}

	spin_unlock_irqrestore(&n->list_lock, flags);

	if (slab_free) {
		stat(s, FREE_SLAB);
		free_slab(s, slab_free);
	}
}

/*
 * Slow path handling. This may still be called frequently since objects
 * have a longer lifetime than the cpu slabs in most processing loads.
 *
 * So we still attempt to reduce cache line usage. Just take the slab
 * lock and free the item. If there is no additional partial slab
 * handling required then we can return immediately.
 */
static void __slab_free(struct kmem_cache *s, struct slab *slab,
			void *head, void *tail, int cnt,
			unsigned long addr)

{
	bool was_full;
	struct freelist_counters old, new;
	struct kmem_cache_node *n = NULL;
	unsigned long flags;
	bool on_node_partial;

	if (IS_ENABLED(CONFIG_SLUB_TINY) || kmem_cache_debug(s)) {
		free_to_partial_list(s, slab, head, tail, cnt, addr);
		return;
	}

	do {
		if (unlikely(n)) {
			spin_unlock_irqrestore(&n->list_lock, flags);
			n = NULL;
		}

		old.freelist = slab->freelist;
		old.counters = slab->counters;

		was_full = (old.freelist == NULL);

		set_freepointer(s, tail, old.freelist);

		new.freelist = head;
		new.counters = old.counters;
		new.inuse -= cnt;

		/*
		 * Might need to be taken off (due to becoming empty) or added
		 * to (due to not being full anymore) the partial list.
		 * Unless it's frozen.
		 */
		if (!new.inuse || was_full) {

			n = get_node(s, slab_nid(slab));
			/*
			 * Speculatively acquire the list_lock.
			 * If the cmpxchg does not succeed then we may
			 * drop the list_lock without any processing.
			 *
			 * Otherwise the list_lock will synchronize with
			 * other processors updating the list of slabs.
			 */
			spin_lock_irqsave(&n->list_lock, flags);

			on_node_partial = slab_test_node_partial(slab);
		}

	} while (!slab_update_freelist(s, slab, &old, &new, "__slab_free"));

	if (likely(!n)) {
		/*
		 * We didn't take the list_lock because the slab was already on
		 * the partial list and will remain there.
		 */
		return;
	}

	/*
	 * This slab was partially empty but not on the per-node partial list,
	 * in which case we shouldn't manipulate its list, just return.
	 */
	if (!was_full && !on_node_partial) {
		spin_unlock_irqrestore(&n->list_lock, flags);
		return;
	}

	/*
	 * If slab became empty, should we add/keep it on the partial list or we
	 * have enough?
	 */
	if (unlikely(!new.inuse && n->nr_partial >= s->min_partial))
		goto slab_empty;

	/*
	 * Objects left in the slab. If it was not on the partial list before
	 * then add it.
	 */
	if (unlikely(was_full)) {
		add_partial(n, slab, ADD_TO_TAIL);
		stat(s, FREE_ADD_PARTIAL);
	}
	spin_unlock_irqrestore(&n->list_lock, flags);
	return;

slab_empty:
	/*
	 * The slab could have a single object and thus go from full to empty in
	 * a single free, but more likely it was on the partial list. Remove it.
	 */
	if (likely(!was_full)) {
		remove_partial(n, slab);
		stat(s, FREE_REMOVE_PARTIAL);
	}

	spin_unlock_irqrestore(&n->list_lock, flags);
	stat(s, FREE_SLAB);
	discard_slab(s, slab);
}

/*
 * pcs is locked. We should have get rid of the spare sheaf and obtained an
 * empty sheaf, while the main sheaf is full. We want to install the empty sheaf
 * as a main sheaf, and make the current main sheaf a spare sheaf.
 *
 * However due to having relinquished the cpu_sheaves lock when obtaining
 * the empty sheaf, we need to handle some unlikely but possible cases.
 *
 * If we put any sheaf to barn here, it's because we were interrupted or have
 * been migrated to a different cpu, which should be rare enough so just ignore
 * the barn's limits to simplify the handling.
 *
 * An alternative scenario that gets us here is when we fail
 * barn_replace_full_sheaf(), because there's no empty sheaf available in the
 * barn, so we had to allocate it by alloc_empty_sheaf(). But because we saw the
 * limit on full sheaves was not exceeded, we assume it didn't change and just
 * put the full sheaf there.
 */
static void __pcs_install_empty_sheaf(struct kmem_cache *s,
		struct slub_percpu_sheaves *pcs, struct slab_sheaf *empty,
		struct node_barn *barn)
{
	lockdep_assert_held(this_cpu_ptr(&s->cpu_sheaves->lock));

	/* This is what we expect to find if nobody interrupted us. */
	if (likely(!pcs->spare)) {
		pcs->spare = pcs->main;
		pcs->main = empty;
		return;
	}

	/*
	 * Unlikely because if the main sheaf had space, we would have just
	 * freed to it. Get rid of our empty sheaf.
	 */
	if (pcs->main->size < s->sheaf_capacity) {
		barn_put_empty_sheaf(barn, empty);
		return;
	}

	/* Also unlikely for the same reason */
	if (pcs->spare->size < s->sheaf_capacity) {
		swap(pcs->main, pcs->spare);
		barn_put_empty_sheaf(barn, empty);
		return;
	}

	/*
	 * We probably failed barn_replace_full_sheaf() due to no empty sheaf
	 * available there, but we allocated one, so finish the job.
	 */
	barn_put_full_sheaf(barn, pcs->main);
	stat(s, BARN_PUT);
	pcs->main = empty;
}

/*
 * Replace the full main sheaf with a (at least partially) empty sheaf.
 *
 * Must be called with the cpu_sheaves local lock locked. If successful, returns
 * the pcs pointer and the local lock locked (possibly on a different cpu than
 * initially called). If not successful, returns NULL and the local lock
 * unlocked.
 */
static struct slub_percpu_sheaves *
__pcs_replace_full_main(struct kmem_cache *s, struct slub_percpu_sheaves *pcs,
			bool allow_spin)
{
	struct slab_sheaf *empty;
	struct node_barn *barn;
	bool put_fail;

restart:
	lockdep_assert_held(this_cpu_ptr(&s->cpu_sheaves->lock));

	/* Bootstrap or debug cache, back off */
	if (unlikely(!cache_has_sheaves(s))) {
		local_unlock(&s->cpu_sheaves->lock);
		return NULL;
	}

	barn = get_barn(s);
	if (!barn) {
		local_unlock(&s->cpu_sheaves->lock);
		return NULL;
	}

	put_fail = false;

	if (!pcs->spare) {
		empty = barn_get_empty_sheaf(barn, allow_spin);
		if (empty) {
			pcs->spare = pcs->main;
			pcs->main = empty;
			return pcs;
		}
		goto alloc_empty;
	}

	if (pcs->spare->size < s->sheaf_capacity) {
		swap(pcs->main, pcs->spare);
		return pcs;
	}

	empty = barn_replace_full_sheaf(barn, pcs->main, allow_spin);

	if (!IS_ERR(empty)) {
		stat(s, BARN_PUT);
		pcs->main = empty;
		return pcs;
	}

	/* sheaf_flush_unused() doesn't support !allow_spin */
	if (PTR_ERR(empty) == -E2BIG && allow_spin) {
		/* Since we got here, spare exists and is full */
		struct slab_sheaf *to_flush = pcs->spare;

		stat(s, BARN_PUT_FAIL);

		pcs->spare = NULL;
		local_unlock(&s->cpu_sheaves->lock);

		sheaf_flush_unused(s, to_flush);
		empty = to_flush;
		goto got_empty;
	}

	/*
	 * We could not replace full sheaf because barn had no empty
	 * sheaves. We can still allocate it and put the full sheaf in
	 * __pcs_install_empty_sheaf(), but if we fail to allocate it,
	 * make sure to count the fail.
	 */
	put_fail = true;

alloc_empty:
	local_unlock(&s->cpu_sheaves->lock);

	/*
	 * alloc_empty_sheaf() doesn't support !allow_spin and it's
	 * easier to fall back to freeing directly without sheaves
	 * than add the support (and to sheaf_flush_unused() above)
	 */
	if (!allow_spin)
		return NULL;

	empty = alloc_empty_sheaf(s, GFP_NOWAIT);
	if (empty)
		goto got_empty;

	if (put_fail)
		 stat(s, BARN_PUT_FAIL);

	if (!sheaf_flush_main(s))
		return NULL;

	if (!local_trylock(&s->cpu_sheaves->lock))
		return NULL;

	pcs = this_cpu_ptr(s->cpu_sheaves);

	/*
	 * we flushed the main sheaf so it should be empty now,
	 * but in case we got preempted or migrated, we need to
	 * check again
	 */
	if (pcs->main->size == s->sheaf_capacity)
		goto restart;

	return pcs;

got_empty:
	if (!local_trylock(&s->cpu_sheaves->lock)) {
		barn_put_empty_sheaf(barn, empty);
		return NULL;
	}

	pcs = this_cpu_ptr(s->cpu_sheaves);
	__pcs_install_empty_sheaf(s, pcs, empty, barn);

	return pcs;
}

/*
 * Free an object to the percpu sheaves.
 * The object is expected to have passed slab_free_hook() already.
 */
static __fastpath_inline
bool free_to_pcs(struct kmem_cache *s, void *object, bool allow_spin)
{
	struct slub_percpu_sheaves *pcs;

	if (!local_trylock(&s->cpu_sheaves->lock))
		return false;

	pcs = this_cpu_ptr(s->cpu_sheaves);

	if (unlikely(pcs->main->size == s->sheaf_capacity)) {

		pcs = __pcs_replace_full_main(s, pcs, allow_spin);
		if (unlikely(!pcs))
			return false;
	}

	pcs->main->objects[pcs->main->size++] = object;

	local_unlock(&s->cpu_sheaves->lock);

	stat(s, FREE_FASTPATH);

	return true;
}

static void rcu_free_sheaf(struct rcu_head *head)
{
	struct kmem_cache_node *n;
	struct slab_sheaf *sheaf;
	struct node_barn *barn = NULL;
	struct kmem_cache *s;

	sheaf = container_of(head, struct slab_sheaf, rcu_head);

	s = sheaf->cache;

	/*
	 * This may remove some objects due to slab_free_hook() returning false,
	 * so that the sheaf might no longer be completely full. But it's easier
	 * to handle it as full (unless it became completely empty), as the code
	 * handles it fine. The only downside is that sheaf will serve fewer
	 * allocations when reused. It only happens due to debugging, which is a
	 * performance hit anyway.
	 *
	 * If it returns true, there was at least one object from pfmemalloc
	 * slab so simply flush everything.
	 */
	if (__rcu_free_sheaf_prepare(s, sheaf))
		goto flush;

	n = get_node(s, sheaf->node);
	if (!n)
		goto flush;

	barn = n->barn;

	/* due to slab_free_hook() */
	if (unlikely(sheaf->size == 0))
		goto empty;

	/*
	 * Checking nr_full/nr_empty outside lock avoids contention in case the
	 * barn is at the respective limit. Due to the race we might go over the
	 * limit but that should be rare and harmless.
	 */

	if (data_race(barn->nr_full) < MAX_FULL_SHEAVES) {
		stat(s, BARN_PUT);
		barn_put_full_sheaf(barn, sheaf);
		return;
	}

flush:
	stat(s, BARN_PUT_FAIL);
	sheaf_flush_unused(s, sheaf);

empty:
	if (barn && data_race(barn->nr_empty) < MAX_EMPTY_SHEAVES) {
		barn_put_empty_sheaf(barn, sheaf);
		return;
	}

	free_empty_sheaf(s, sheaf);
}

/*
 * kvfree_call_rcu() can be called while holding a raw_spinlock_t. Since
 * __kfree_rcu_sheaf() may acquire a spinlock_t (sleeping lock on PREEMPT_RT),
 * this would violate lock nesting rules. Therefore, kvfree_call_rcu() avoids
 * this problem by bypassing the sheaves layer entirely on PREEMPT_RT.
 *
 * However, lockdep still complains that it is invalid to acquire spinlock_t
 * while holding raw_spinlock_t, even on !PREEMPT_RT where spinlock_t is a
 * spinning lock. Tell lockdep that acquiring spinlock_t is valid here
 * by temporarily raising the wait-type to LD_WAIT_CONFIG.
 */
static DEFINE_WAIT_OVERRIDE_MAP(kfree_rcu_sheaf_map, LD_WAIT_CONFIG);

bool __kfree_rcu_sheaf(struct kmem_cache *s, void *obj)
{
	struct slub_percpu_sheaves *pcs;
	struct slab_sheaf *rcu_sheaf;

	if (WARN_ON_ONCE(IS_ENABLED(CONFIG_PREEMPT_RT)))
		return false;

	lock_map_acquire_try(&kfree_rcu_sheaf_map);

	if (!local_trylock(&s->cpu_sheaves->lock))
		goto fail;

	pcs = this_cpu_ptr(s->cpu_sheaves);

	if (unlikely(!pcs->rcu_free)) {

		struct slab_sheaf *empty;
		struct node_barn *barn;

		/* Bootstrap or debug cache, fall back */
		if (unlikely(!cache_has_sheaves(s))) {
			local_unlock(&s->cpu_sheaves->lock);
			goto fail;
		}

		if (pcs->spare && pcs->spare->size == 0) {
			pcs->rcu_free = pcs->spare;
			pcs->spare = NULL;
			goto do_free;
		}

		barn = get_barn(s);
		if (!barn) {
			local_unlock(&s->cpu_sheaves->lock);
			goto fail;
		}

		empty = barn_get_empty_sheaf(barn, true);

		if (empty) {
			pcs->rcu_free = empty;
			goto do_free;
		}

		local_unlock(&s->cpu_sheaves->lock);

		empty = alloc_empty_sheaf(s, GFP_NOWAIT);

		if (!empty)
			goto fail;

		if (!local_trylock(&s->cpu_sheaves->lock)) {
			barn_put_empty_sheaf(barn, empty);
			goto fail;
		}

		pcs = this_cpu_ptr(s->cpu_sheaves);

		if (unlikely(pcs->rcu_free))
			barn_put_empty_sheaf(barn, empty);
		else
			pcs->rcu_free = empty;
	}

do_free:

	rcu_sheaf = pcs->rcu_free;

	/*
	 * Since we flush immediately when size reaches capacity, we never reach
	 * this with size already at capacity, so no OOB write is possible.
	 */
	rcu_sheaf->objects[rcu_sheaf->size++] = obj;

	if (likely(rcu_sheaf->size < s->sheaf_capacity)) {
		rcu_sheaf = NULL;
	} else {
		pcs->rcu_free = NULL;
		rcu_sheaf->node = numa_mem_id();
	}

	/*
	 * we flush before local_unlock to make sure a racing
	 * flush_all_rcu_sheaves() doesn't miss this sheaf
	 */
	if (rcu_sheaf)
		call_rcu(&rcu_sheaf->rcu_head, rcu_free_sheaf);

	local_unlock(&s->cpu_sheaves->lock);

	stat(s, FREE_RCU_SHEAF);
	lock_map_release(&kfree_rcu_sheaf_map);
	return true;

fail:
	stat(s, FREE_RCU_SHEAF_FAIL);
	lock_map_release(&kfree_rcu_sheaf_map);
	return false;
}

/*
 * Bulk free objects to the percpu sheaves.
 * Unlike free_to_pcs() this includes the calls to all necessary hooks
 * and the fallback to freeing to slab pages.
 */
static void free_to_pcs_bulk(struct kmem_cache *s, size_t size, void **p)
{
	struct slub_percpu_sheaves *pcs;
	struct slab_sheaf *main, *empty;
	bool init = slab_want_init_on_free(s);
	unsigned int batch, i = 0;
	struct node_barn *barn;
	void *remote_objects[PCS_BATCH_MAX];
	unsigned int remote_nr = 0;
	int node = numa_mem_id();

next_remote_batch:
	while (i < size) {
		struct slab *slab = virt_to_slab(p[i]);

		memcg_slab_free_hook(s, slab, p + i, 1);
		alloc_tagging_slab_free_hook(s, slab, p + i, 1);

		if (unlikely(!slab_free_hook(s, p[i], init, false))) {
			p[i] = p[--size];
			continue;
		}

		if (unlikely((IS_ENABLED(CONFIG_NUMA) && slab_nid(slab) != node)
			     || slab_test_pfmemalloc(slab))) {
			remote_objects[remote_nr] = p[i];
			p[i] = p[--size];
			if (++remote_nr >= PCS_BATCH_MAX)
				goto flush_remote;
			continue;
		}

		i++;
	}

	if (!size)
		goto flush_remote;

next_batch:
	if (!local_trylock(&s->cpu_sheaves->lock))
		goto fallback;

	pcs = this_cpu_ptr(s->cpu_sheaves);

	if (likely(pcs->main->size < s->sheaf_capacity))
		goto do_free;

	barn = get_barn(s);
	if (!barn)
		goto no_empty;

	if (!pcs->spare) {
		empty = barn_get_empty_sheaf(barn, true);
		if (!empty)
			goto no_empty;

		pcs->spare = pcs->main;
		pcs->main = empty;
		goto do_free;
	}

	if (pcs->spare->size < s->sheaf_capacity) {
		swap(pcs->main, pcs->spare);
		goto do_free;
	}

	empty = barn_replace_full_sheaf(barn, pcs->main, true);
	if (IS_ERR(empty)) {
		stat(s, BARN_PUT_FAIL);
		goto no_empty;
	}

	stat(s, BARN_PUT);
	pcs->main = empty;

do_free:
	main = pcs->main;
	batch = min(size, s->sheaf_capacity - main->size);

	memcpy(main->objects + main->size, p, batch * sizeof(void *));
	main->size += batch;

	local_unlock(&s->cpu_sheaves->lock);

	stat_add(s, FREE_FASTPATH, batch);

	if (batch < size) {
		p += batch;
		size -= batch;
		goto next_batch;
	}

	if (remote_nr)
		goto flush_remote;

	return;

no_empty:
	local_unlock(&s->cpu_sheaves->lock);

	/*
	 * if we depleted all empty sheaves in the barn or there are too
	 * many full sheaves, free the rest to slab pages
	 */
fallback:
	__kmem_cache_free_bulk(s, size, p);
	stat_add(s, FREE_SLOWPATH, size);

flush_remote:
	if (remote_nr) {
		__kmem_cache_free_bulk(s, remote_nr, &remote_objects[0]);
		stat_add(s, FREE_SLOWPATH, remote_nr);
		if (i < size) {
			remote_nr = 0;
			goto next_remote_batch;
		}
	}
}

struct defer_free {
	struct llist_head objects;
	struct irq_work work;
};

static void free_deferred_objects(struct irq_work *work);

static DEFINE_PER_CPU(struct defer_free, defer_free_objects) = {
	.objects = LLIST_HEAD_INIT(objects),
	.work = IRQ_WORK_INIT(free_deferred_objects),
};

/*
 * In PREEMPT_RT irq_work runs in per-cpu kthread, so it's safe
 * to take sleeping spin_locks from __slab_free().
 * In !PREEMPT_RT irq_work will run after local_unlock_irqrestore().
 */
static void free_deferred_objects(struct irq_work *work)
{
	struct defer_free *df = container_of(work, struct defer_free, work);
	struct llist_head *objs = &df->objects;
	struct llist_node *llnode, *pos, *t;

	if (llist_empty(objs))
		return;

	llnode = llist_del_all(objs);
	llist_for_each_safe(pos, t, llnode) {
		struct kmem_cache *s;
		struct slab *slab;
		void *x = pos;

		slab = virt_to_slab(x);
		s = slab->slab_cache;

		/* Point 'x' back to the beginning of allocated object */
		x -= s->offset;

		/*
		 * We used freepointer in 'x' to link 'x' into df->objects.
		 * Clear it to NULL to avoid false positive detection
		 * of "Freepointer corruption".
		 */
		set_freepointer(s, x, NULL);

		__slab_free(s, slab, x, x, 1, _THIS_IP_);
		stat(s, FREE_SLOWPATH);
	}
}

static void defer_free(struct kmem_cache *s, void *head)
{
	struct defer_free *df;

	guard(preempt)();

	head = kasan_reset_tag(head);

	df = this_cpu_ptr(&defer_free_objects);
	if (llist_add(head + s->offset, &df->objects))
		irq_work_queue(&df->work);
}

void defer_free_barrier(void)
{
	int cpu;

	for_each_possible_cpu(cpu)
		irq_work_sync(&per_cpu_ptr(&defer_free_objects, cpu)->work);
}

static __fastpath_inline
void slab_free(struct kmem_cache *s, struct slab *slab, void *object,
	       unsigned long addr)
{
	memcg_slab_free_hook(s, slab, &object, 1);
	alloc_tagging_slab_free_hook(s, slab, &object, 1);

	if (unlikely(!slab_free_hook(s, object, slab_want_init_on_free(s), false)))
		return;

	if (likely(!IS_ENABLED(CONFIG_NUMA) || slab_nid(slab) == numa_mem_id())
	    && likely(!slab_test_pfmemalloc(slab))) {
		if (likely(free_to_pcs(s, object, true)))
			return;
	}

	__slab_free(s, slab, object, object, 1, addr);
	stat(s, FREE_SLOWPATH);
}

#ifdef CONFIG_MEMCG
/* Do not inline the rare memcg charging failed path into the allocation path */
static noinline
void memcg_alloc_abort_single(struct kmem_cache *s, void *object)
{
	struct slab *slab = virt_to_slab(object);

	alloc_tagging_slab_free_hook(s, slab, &object, 1);

	if (likely(slab_free_hook(s, object, slab_want_init_on_free(s), false)))
		__slab_free(s, slab, object, object, 1, _RET_IP_);
}
#endif

static __fastpath_inline
void slab_free_bulk(struct kmem_cache *s, struct slab *slab, void *head,
		    void *tail, void **p, int cnt, unsigned long addr)
{
	memcg_slab_free_hook(s, slab, p, cnt);
	alloc_tagging_slab_free_hook(s, slab, p, cnt);
	/*
	 * With KASAN enabled slab_free_freelist_hook modifies the freelist
	 * to remove objects, whose reuse must be delayed.
	 */
	if (likely(slab_free_freelist_hook(s, &head, &tail, &cnt))) {
		__slab_free(s, slab, head, tail, cnt, addr);
		stat_add(s, FREE_SLOWPATH, cnt);
	}
}

#ifdef CONFIG_SLUB_RCU_DEBUG
static void slab_free_after_rcu_debug(struct rcu_head *rcu_head)
{
	struct rcu_delayed_free *delayed_free =
			container_of(rcu_head, struct rcu_delayed_free, head);
	void *object = delayed_free->object;
	struct slab *slab = virt_to_slab(object);
	struct kmem_cache *s;

	kfree(delayed_free);

	if (WARN_ON(is_kfence_address(object)))
		return;

	/* find the object and the cache again */
	if (WARN_ON(!slab))
		return;
	s = slab->slab_cache;
	if (WARN_ON(!(s->flags & SLAB_TYPESAFE_BY_RCU)))
		return;

	/* resume freeing */
	if (slab_free_hook(s, object, slab_want_init_on_free(s), true)) {
		__slab_free(s, slab, object, object, 1, _THIS_IP_);
		stat(s, FREE_SLOWPATH);
	}
}
#endif /* CONFIG_SLUB_RCU_DEBUG */

#ifdef CONFIG_KASAN_GENERIC
void ___cache_free(struct kmem_cache *cache, void *x, unsigned long addr)
{
	__slab_free(cache, virt_to_slab(x), x, x, 1, addr);
	stat(cache, FREE_SLOWPATH);
}
#endif

static noinline void warn_free_bad_obj(struct kmem_cache *s, void *obj)
{
	struct kmem_cache *cachep;
	struct slab *slab;

	slab = virt_to_slab(obj);
	if (WARN_ONCE(!slab,
			"kmem_cache_free(%s, %p): object is not in a slab page\n",
			s->name, obj))
		return;

	cachep = slab->slab_cache;

	if (WARN_ONCE(cachep != s,
			"kmem_cache_free(%s, %p): object belongs to different cache %s\n",
			s->name, obj, cachep ? cachep->name : "(NULL)")) {
		if (cachep)
			print_tracking(cachep, obj);
		return;
	}
}

/**
 * kmem_cache_free - Deallocate an object
 * @s: The cache the allocation was from.
 * @x: The previously allocated object.
 *
 * Free an object which was previously allocated from this
 * cache.
 */
void kmem_cache_free(struct kmem_cache *s, void *x)
{
	struct slab *slab;

	slab = virt_to_slab(x);

	if (IS_ENABLED(CONFIG_SLAB_FREELIST_HARDENED) ||
	    kmem_cache_debug_flags(s, SLAB_CONSISTENCY_CHECKS)) {

		/*
		 * Intentionally leak the object in these cases, because it
		 * would be too dangerous to continue.
		 */
		if (unlikely(!slab || (slab->slab_cache != s))) {
			warn_free_bad_obj(s, x);
			return;
		}
	}

	trace_kmem_cache_free(_RET_IP_, x, s);
	slab_free(s, slab, x, _RET_IP_);
}
EXPORT_SYMBOL(kmem_cache_free);

static inline size_t slab_ksize(struct slab *slab)
{
	struct kmem_cache *s = slab->slab_cache;

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
	 * or any other metadata back there then we can
	 * only use the space before that information.
	 */
	if (s->flags & (SLAB_TYPESAFE_BY_RCU | SLAB_STORE_USER))
		return s->inuse;
	else if (obj_exts_in_object(s, slab))
		return s->inuse;
	/*
	 * Else we can use all the padding etc for the allocation
	 */
	return s->size;
}

static size_t __ksize(const void *object)
{
	struct page *page;
	struct slab *slab;

	if (unlikely(object == ZERO_SIZE_PTR))
		return 0;

	page = virt_to_page(object);

	if (unlikely(PageLargeKmalloc(page)))
		return large_kmalloc_size(page);

	slab = page_slab(page);
	/* Delete this after we're sure there are no users */
	if (WARN_ON(!slab))
		return page_size(page);

#ifdef CONFIG_SLUB_DEBUG
	skip_orig_size_check(slab->slab_cache, object);
#endif

	return slab_ksize(slab);
}

/**
 * ksize -- Report full size of underlying allocation
 * @objp: pointer to the object
 *
 * This should only be used internally to query the true size of allocations.
 * It is not meant to be a way to discover the usable size of an allocation
 * after the fact. Instead, use kmalloc_size_roundup(). Using memory beyond
 * the originally requested allocation size may trigger KASAN, UBSAN_BOUNDS,
 * and/or FORTIFY_SOURCE.
 *
 * Return: size of the actual memory used by @objp in bytes
 */
size_t ksize(const void *objp)
{
	/*
	 * We need to first check that the pointer to the object is valid.
	 * The KASAN report printed from ksize() is more useful, then when
	 * it's printed later when the behaviour could be undefined due to
	 * a potential use-after-free or double-free.
	 *
	 * We use kasan_check_byte(), which is supported for the hardware
	 * tag-based KASAN mode, unlike kasan_check_read/write().
	 *
	 * If the pointed to memory is invalid, we return 0 to avoid users of
	 * ksize() writing to and potentially corrupting the memory region.
	 *
	 * We want to perform the check before __ksize(), to avoid potentially
	 * crashing in __ksize() due to accessing invalid metadata.
	 */
	if (unlikely(ZERO_OR_NULL_PTR(objp)) || !kasan_check_byte(objp))
		return 0;

	return kfence_ksize(objp) ?: __ksize(objp);
}
EXPORT_SYMBOL(ksize);

static void free_large_kmalloc(struct page *page, void *object)
{
	unsigned int order = compound_order(page);

	if (WARN_ON_ONCE(!PageLargeKmalloc(page))) {
		dump_page(page, "Not a kmalloc allocation");
		return;
	}

	if (WARN_ON_ONCE(order == 0))
		pr_warn_once("object pointer: 0x%p\n", object);

	kmemleak_free(object);
	kasan_kfree_large(object);
	kmsan_kfree_large(object);

	mod_lruvec_page_state(page, NR_SLAB_UNRECLAIMABLE_B,
			      -(PAGE_SIZE << order));
	__ClearPageLargeKmalloc(page);
	free_frozen_pages(page, order);
}

/*
 * Given an rcu_head embedded within an object obtained from kvmalloc at an
 * offset < 4k, free the object in question.
 */
void kvfree_rcu_cb(struct rcu_head *head)
{
	void *obj = head;
	struct page *page;
	struct slab *slab;
	struct kmem_cache *s;
	void *slab_addr;

	if (is_vmalloc_addr(obj)) {
		obj = (void *) PAGE_ALIGN_DOWN((unsigned long)obj);
		vfree(obj);
		return;
	}

	page = virt_to_page(obj);
	slab = page_slab(page);
	if (!slab) {
		/*
		 * rcu_head offset can be only less than page size so no need to
		 * consider allocation order
		 */
		obj = (void *) PAGE_ALIGN_DOWN((unsigned long)obj);
		free_large_kmalloc(page, obj);
		return;
	}

	s = slab->slab_cache;
	slab_addr = slab_address(slab);

	if (is_kfence_address(obj)) {
		obj = kfence_object_start(obj);
	} else {
		unsigned int idx = __obj_to_index(s, slab_addr, obj);

		obj = slab_addr + s->size * idx;
		obj = fixup_red_left(s, obj);
	}

	slab_free(s, slab, obj, _RET_IP_);
}

/**
 * kfree - free previously allocated memory
 * @object: pointer returned by kmalloc() or kmem_cache_alloc()
 *
 * If @object is NULL, no operation is performed.
 */
void kfree(const void *object)
{
	struct page *page;
	struct slab *slab;
	struct kmem_cache *s;
	void *x = (void *)object;

	trace_kfree(_RET_IP_, object);

	if (unlikely(ZERO_OR_NULL_PTR(object)))
		return;

	page = virt_to_page(object);
	slab = page_slab(page);
	if (!slab) {
		free_large_kmalloc(page, (void *)object);
		return;
	}

	s = slab->slab_cache;
	slab_free(s, slab, x, _RET_IP_);
}
EXPORT_SYMBOL(kfree);

/*
 * Can be called while holding raw_spinlock_t or from IRQ and NMI,
 * but ONLY for objects allocated by kmalloc_nolock().
 * Debug checks (like kmemleak and kfence) were skipped on allocation,
 * hence
 * obj = kmalloc(); kfree_nolock(obj);
 * will miss kmemleak/kfence book keeping and will cause false positives.
 * large_kmalloc is not supported either.
 */
void kfree_nolock(const void *object)
{
	struct slab *slab;
	struct kmem_cache *s;
	void *x = (void *)object;

	if (unlikely(ZERO_OR_NULL_PTR(object)))
		return;

	slab = virt_to_slab(object);
	if (unlikely(!slab)) {
		WARN_ONCE(1, "large_kmalloc is not supported by kfree_nolock()");
		return;
	}

	s = slab->slab_cache;

	memcg_slab_free_hook(s, slab, &x, 1);
	alloc_tagging_slab_free_hook(s, slab, &x, 1);
	/*
	 * Unlike slab_free() do NOT call the following:
	 * kmemleak_free_recursive(x, s->flags);
	 * debug_check_no_locks_freed(x, s->object_size);
	 * debug_check_no_obj_freed(x, s->object_size);
	 * __kcsan_check_access(x, s->object_size, ..);
	 * kfence_free(x);
	 * since they take spinlocks or not safe from any context.
	 */
	kmsan_slab_free(s, x);
	/*
	 * If KASAN finds a kernel bug it will do kasan_report_invalid_free()
	 * which will call raw_spin_lock_irqsave() which is technically
	 * unsafe from NMI, but take chance and report kernel bug.
	 * The sequence of
	 * kasan_report_invalid_free() -> raw_spin_lock_irqsave() -> NMI
	 *  -> kfree_nolock() -> kasan_report_invalid_free() on the same CPU
	 * is double buggy and deserves to deadlock.
	 */
	if (kasan_slab_pre_free(s, x))
		return;
	/*
	 * memcg, kasan_slab_pre_free are done for 'x'.
	 * The only thing left is kasan_poison without quarantine,
	 * since kasan quarantine takes locks and not supported from NMI.
	 */
	kasan_slab_free(s, x, false, false, /* skip quarantine */true);

	if (likely(!IS_ENABLED(CONFIG_NUMA) || slab_nid(slab) == numa_mem_id())) {
		if (likely(free_to_pcs(s, x, false)))
			return;
	}

	/*
	 * __slab_free() can locklessly cmpxchg16 into a slab, but then it might
	 * need to take spin_lock for further processing.
	 * Avoid the complexity and simply add to a deferred list.
	 */
	defer_free(s, x);
}
EXPORT_SYMBOL_GPL(kfree_nolock);

static __always_inline __realloc_size(2) void *
__do_krealloc(const void *p, size_t new_size, unsigned long align, gfp_t flags, int nid)
{
	void *ret;
	size_t ks = 0;
	int orig_size = 0;
	struct kmem_cache *s = NULL;

	if (unlikely(ZERO_OR_NULL_PTR(p)))
		goto alloc_new;

	/* Check for double-free. */
	if (!kasan_check_byte(p))
		return NULL;

	/*
	 * If reallocation is not necessary (e. g. the new size is less
	 * than the current allocated size), the current allocation will be
	 * preserved unless __GFP_THISNODE is set. In the latter case a new
	 * allocation on the requested node will be attempted.
	 */
	if (unlikely(flags & __GFP_THISNODE) && nid != NUMA_NO_NODE &&
		     nid != page_to_nid(virt_to_page(p)))
		goto alloc_new;

	if (is_kfence_address(p)) {
		ks = orig_size = kfence_ksize(p);
	} else {
		struct page *page = virt_to_page(p);
		struct slab *slab = page_slab(page);

		if (!slab) {
			/* Big kmalloc object */
			ks = page_size(page);
			WARN_ON(ks <= KMALLOC_MAX_CACHE_SIZE);
			WARN_ON(p != page_address(page));
		} else {
			s = slab->slab_cache;
			orig_size = get_orig_size(s, (void *)p);
			ks = s->object_size;
		}
	}

	/* If the old object doesn't fit, allocate a bigger one */
	if (new_size > ks)
		goto alloc_new;

	/* If the old object doesn't satisfy the new alignment, allocate a new one */
	if (!IS_ALIGNED((unsigned long)p, align))
		goto alloc_new;

	/* Zero out spare memory. */
	if (want_init_on_alloc(flags)) {
		kasan_disable_current();
		if (orig_size && orig_size < new_size)
			memset(kasan_reset_tag(p) + orig_size, 0, new_size - orig_size);
		else
			memset(kasan_reset_tag(p) + new_size, 0, ks - new_size);
		kasan_enable_current();
	}

	/* Setup kmalloc redzone when needed */
	if (s && slub_debug_orig_size(s)) {
		set_orig_size(s, (void *)p, new_size);
		if (s->flags & SLAB_RED_ZONE && new_size < ks)
			memset_no_sanitize_memory(kasan_reset_tag(p) + new_size,
						SLUB_RED_ACTIVE, ks - new_size);
	}

	p = kasan_krealloc(p, new_size, flags);
	return (void *)p;

alloc_new:
	ret = kmalloc_node_track_caller_noprof(new_size, flags, nid, _RET_IP_);
	if (ret && p) {
		/* Disable KASAN checks as the object's redzone is accessed. */
		kasan_disable_current();
		memcpy(ret, kasan_reset_tag(p), orig_size ?: ks);
		kasan_enable_current();
	}

	return ret;
}

/**
 * krealloc_node_align - reallocate memory. The contents will remain unchanged.
 * @p: object to reallocate memory for.
 * @new_size: how many bytes of memory are required.
 * @align: desired alignment.
 * @flags: the type of memory to allocate.
 * @nid: NUMA node or NUMA_NO_NODE
 *
 * If @p is %NULL, krealloc() behaves exactly like kmalloc().  If @new_size
 * is 0 and @p is not a %NULL pointer, the object pointed to is freed.
 *
 * Only alignments up to those guaranteed by kmalloc() will be honored. Please see
 * Documentation/core-api/memory-allocation.rst for more details.
 *
 * If __GFP_ZERO logic is requested, callers must ensure that, starting with the
 * initial memory allocation, every subsequent call to this API for the same
 * memory allocation is flagged with __GFP_ZERO. Otherwise, it is possible that
 * __GFP_ZERO is not fully honored by this API.
 *
 * When slub_debug_orig_size() is off, krealloc() only knows about the bucket
 * size of an allocation (but not the exact size it was allocated with) and
 * hence implements the following semantics for shrinking and growing buffers
 * with __GFP_ZERO::
 *
 *           new             bucket
 *   0       size             size
 *   |--------|----------------|
 *   |  keep  |      zero      |
 *
 * Otherwise, the original allocation size 'orig_size' could be used to
 * precisely clear the requested size, and the new size will also be stored
 * as the new 'orig_size'.
 *
 * In any case, the contents of the object pointed to are preserved up to the
 * lesser of the new and old sizes.
 *
 * Return: pointer to the allocated memory or %NULL in case of error
 */
void *krealloc_node_align_noprof(const void *p, size_t new_size, unsigned long align,
				 gfp_t flags, int nid)
{
	void *ret;

	if (unlikely(!new_size)) {
		kfree(p);
		return ZERO_SIZE_PTR;
	}

	ret = __do_krealloc(p, new_size, align, flags, nid);
	if (ret && kasan_reset_tag(p) != kasan_reset_tag(ret))
		kfree(p);

	return ret;
}
EXPORT_SYMBOL(krealloc_node_align_noprof);

static gfp_t kmalloc_gfp_adjust(gfp_t flags, size_t size)
{
	/*
	 * We want to attempt a large physically contiguous block first because
	 * it is less likely to fragment multiple larger blocks and therefore
	 * contribute to a long term fragmentation less than vmalloc fallback.
	 * However make sure that larger requests are not too disruptive - i.e.
	 * do not direct reclaim unless physically continuous memory is preferred
	 * (__GFP_RETRY_MAYFAIL mode). We still kick in kswapd/kcompactd to
	 * start working in the background
	 */
	if (size > PAGE_SIZE) {
		flags |= __GFP_NOWARN;

		if (!(flags & __GFP_RETRY_MAYFAIL))
			flags &= ~__GFP_DIRECT_RECLAIM;

		/* nofail semantic is implemented by the vmalloc fallback */
		flags &= ~__GFP_NOFAIL;
	}

	return flags;
}

/**
 * __kvmalloc_node - attempt to allocate physically contiguous memory, but upon
 * failure, fall back to non-contiguous (vmalloc) allocation.
 * @size: size of the request.
 * @b: which set of kmalloc buckets to allocate from.
 * @align: desired alignment.
 * @flags: gfp mask for the allocation - must be compatible (superset) with GFP_KERNEL.
 * @node: numa node to allocate from
 *
 * Only alignments up to those guaranteed by kmalloc() will be honored. Please see
 * Documentation/core-api/memory-allocation.rst for more details.
 *
 * Uses kmalloc to get the memory but if the allocation fails then falls back
 * to the vmalloc allocator. Use kvfree for freeing the memory.
 *
 * GFP_NOWAIT and GFP_ATOMIC are supported, the __GFP_NORETRY modifier is not.
 * __GFP_RETRY_MAYFAIL is supported, and it should be used only if kmalloc is
 * preferable to the vmalloc fallback, due to visible performance drawbacks.
 *
 * Return: pointer to the allocated memory of %NULL in case of failure
 */
void *__kvmalloc_node_noprof(DECL_BUCKET_PARAMS(size, b), unsigned long align,
			     gfp_t flags, int node)
{
	bool allow_block;
	void *ret;

	/*
	 * It doesn't really make sense to fallback to vmalloc for sub page
	 * requests
	 */
	ret = __do_kmalloc_node(size, PASS_BUCKET_PARAM(b),
				kmalloc_gfp_adjust(flags, size),
				node, _RET_IP_);
	if (ret || size <= PAGE_SIZE)
		return ret;

	/* Don't even allow crazy sizes */
	if (unlikely(size > INT_MAX)) {
		WARN_ON_ONCE(!(flags & __GFP_NOWARN));
		return NULL;
	}

	/*
	 * For non-blocking the VM_ALLOW_HUGE_VMAP is not used
	 * because the huge-mapping path in vmalloc contains at
	 * least one might_sleep() call.
	 *
	 * TODO: Revise huge-mapping path to support non-blocking
	 * flags.
	 */
	allow_block = gfpflags_allow_blocking(flags);

	/*
	 * kvmalloc() can always use VM_ALLOW_HUGE_VMAP,
	 * since the callers already cannot assume anything
	 * about the resulting pointer, and cannot play
	 * protection games.
	 */
	return __vmalloc_node_range_noprof(size, align, VMALLOC_START, VMALLOC_END,
			flags, PAGE_KERNEL, allow_block ? VM_ALLOW_HUGE_VMAP:0,
			node, __builtin_return_address(0));
}
EXPORT_SYMBOL(__kvmalloc_node_noprof);

/**
 * kvfree() - Free memory.
 * @addr: Pointer to allocated memory.
 *
 * kvfree frees memory allocated by any of vmalloc(), kmalloc() or kvmalloc().
 * It is slightly more efficient to use kfree() or vfree() if you are certain
 * that you know which one to use.
 *
 * Context: Either preemptible task context or not-NMI interrupt.
 */
void kvfree(const void *addr)
{
	if (is_vmalloc_addr(addr))
		vfree(addr);
	else
		kfree(addr);
}
EXPORT_SYMBOL(kvfree);

/**
 * kvfree_sensitive - Free a data object containing sensitive information.
 * @addr: address of the data object to be freed.
 * @len: length of the data object.
 *
 * Use the special memzero_explicit() function to clear the content of a
 * kvmalloc'ed object containing sensitive data to make sure that the
 * compiler won't optimize out the data clearing.
 */
void kvfree_sensitive(const void *addr, size_t len)
{
	if (likely(!ZERO_OR_NULL_PTR(addr))) {
		memzero_explicit((void *)addr, len);
		kvfree(addr);
	}
}
EXPORT_SYMBOL(kvfree_sensitive);

/**
 * kvrealloc_node_align - reallocate memory; contents remain unchanged
 * @p: object to reallocate memory for
 * @size: the size to reallocate
 * @align: desired alignment
 * @flags: the flags for the page level allocator
 * @nid: NUMA node id
 *
 * If @p is %NULL, kvrealloc() behaves exactly like kvmalloc(). If @size is 0
 * and @p is not a %NULL pointer, the object pointed to is freed.
 *
 * Only alignments up to those guaranteed by kmalloc() will be honored. Please see
 * Documentation/core-api/memory-allocation.rst for more details.
 *
 * If __GFP_ZERO logic is requested, callers must ensure that, starting with the
 * initial memory allocation, every subsequent call to this API for the same
 * memory allocation is flagged with __GFP_ZERO. Otherwise, it is possible that
 * __GFP_ZERO is not fully honored by this API.
 *
 * In any case, the contents of the object pointed to are preserved up to the
 * lesser of the new and old sizes.
 *
 * This function must not be called concurrently with itself or kvfree() for the
 * same memory allocation.
 *
 * Return: pointer to the allocated memory or %NULL in case of error
 */
void *kvrealloc_node_align_noprof(const void *p, size_t size, unsigned long align,
				  gfp_t flags, int nid)
{
	void *n;

	if (is_vmalloc_addr(p))
		return vrealloc_node_align_noprof(p, size, align, flags, nid);

	n = krealloc_node_align_noprof(p, size, align, kmalloc_gfp_adjust(flags, size), nid);
	if (!n) {
		/* We failed to krealloc(), fall back to kvmalloc(). */
		n = kvmalloc_node_align_noprof(size, align, flags, nid);
		if (!n)
			return NULL;

		if (p) {
			/* We already know that `p` is not a vmalloc address. */
			kasan_disable_current();
			memcpy(n, kasan_reset_tag(p), ksize(p));
			kasan_enable_current();

			kfree(p);
		}
	}

	return n;
}
EXPORT_SYMBOL(kvrealloc_node_align_noprof);

struct detached_freelist {
	struct slab *slab;
	void *tail;
	void *freelist;
	int cnt;
	struct kmem_cache *s;
};

/*
 * This function progressively scans the array with free objects (with
 * a limited look ahead) and extract objects belonging to the same
 * slab.  It builds a detached freelist directly within the given
 * slab/objects.  This can happen without any need for
 * synchronization, because the objects are owned by running process.
 * The freelist is build up as a single linked list in the objects.
 * The idea is, that this detached freelist can then be bulk
 * transferred to the real freelist(s), but only requiring a single
 * synchronization primitive.  Look ahead in the array is limited due
 * to performance reasons.
 */
static inline
int build_detached_freelist(struct kmem_cache *s, size_t size,
			    void **p, struct detached_freelist *df)
{
	int lookahead = 3;
	void *object;
	struct page *page;
	struct slab *slab;
	size_t same;

	object = p[--size];
	page = virt_to_page(object);
	slab = page_slab(page);
	if (!s) {
		/* Handle kalloc'ed objects */
		if (!slab) {
			free_large_kmalloc(page, object);
			df->slab = NULL;
			return size;
		}
		/* Derive kmem_cache from object */
		df->slab = slab;
		df->s = slab->slab_cache;
	} else {
		df->slab = slab;
		df->s = s;
	}

	/* Start new detached freelist */
	df->tail = object;
	df->freelist = object;
	df->cnt = 1;

	if (is_kfence_address(object))
		return size;

	set_freepointer(df->s, object, NULL);

	same = size;
	while (size) {
		object = p[--size];
		/* df->slab is always set at this point */
		if (df->slab == virt_to_slab(object)) {
			/* Opportunity build freelist */
			set_freepointer(df->s, object, df->freelist);
			df->freelist = object;
			df->cnt++;
			same--;
			if (size != same)
				swap(p[size], p[same]);
			continue;
		}

		/* Limit look ahead search */
		if (!--lookahead)
			break;
	}

	return same;
}

/*
 * Internal bulk free of objects that were not initialised by the post alloc
 * hooks and thus should not be processed by the free hooks
 */
static void __kmem_cache_free_bulk(struct kmem_cache *s, size_t size, void **p)
{
	if (!size)
		return;

	do {
		struct detached_freelist df;

		size = build_detached_freelist(s, size, p, &df);
		if (!df.slab)
			continue;

		if (kfence_free(df.freelist))
			continue;

		__slab_free(df.s, df.slab, df.freelist, df.tail, df.cnt,
			     _RET_IP_);
	} while (likely(size));
}

/* Note that interrupts must be enabled when calling this function. */
void kmem_cache_free_bulk(struct kmem_cache *s, size_t size, void **p)
{
	if (!size)
		return;

	/*
	 * freeing to sheaves is so incompatible with the detached freelist so
	 * once we go that way, we have to do everything differently
	 */
	if (s && cache_has_sheaves(s)) {
		free_to_pcs_bulk(s, size, p);
		return;
	}

	do {
		struct detached_freelist df;

		size = build_detached_freelist(s, size, p, &df);
		if (!df.slab)
			continue;

		slab_free_bulk(df.s, df.slab, df.freelist, df.tail, &p[size],
			       df.cnt, _RET_IP_);
	} while (likely(size));
}
EXPORT_SYMBOL(kmem_cache_free_bulk);

static unsigned int
__refill_objects_node(struct kmem_cache *s, void **p, gfp_t gfp, unsigned int min,
		      unsigned int max, struct kmem_cache_node *n,
		      bool allow_spin)
{
	struct partial_bulk_context pc;
	struct slab *slab, *slab2;
	unsigned int refilled = 0;
	unsigned long flags;
	void *object;

	pc.flags = gfp;
	pc.min_objects = min;
	pc.max_objects = max;

	if (!get_partial_node_bulk(s, n, &pc, allow_spin))
		return 0;

	list_for_each_entry_safe(slab, slab2, &pc.slabs, slab_list) {

		list_del(&slab->slab_list);

		object = get_freelist_nofreeze(s, slab);

		while (object && refilled < max) {
			p[refilled] = object;
			object = get_freepointer(s, object);
			maybe_wipe_obj_freeptr(s, p[refilled]);

			refilled++;
		}

		/*
		 * Freelist had more objects than we can accommodate, we need to
		 * free them back. We can treat it like a detached freelist, just
		 * need to find the tail object.
		 */
		if (unlikely(object)) {
			void *head = object;
			void *tail;
			int cnt = 0;

			do {
				tail = object;
				cnt++;
				object = get_freepointer(s, object);
			} while (object);
			__slab_free(s, slab, head, tail, cnt, _RET_IP_);
		}

		if (refilled >= max)
			break;
	}

	if (unlikely(!list_empty(&pc.slabs))) {
		spin_lock_irqsave(&n->list_lock, flags);

		list_for_each_entry_safe(slab, slab2, &pc.slabs, slab_list) {

			if (unlikely(!slab->inuse && n->nr_partial >= s->min_partial))
				continue;

			list_del(&slab->slab_list);
			add_partial(n, slab, ADD_TO_HEAD);
		}

		spin_unlock_irqrestore(&n->list_lock, flags);

		/* any slabs left are completely free and for discard */
		list_for_each_entry_safe(slab, slab2, &pc.slabs, slab_list) {

			list_del(&slab->slab_list);
			discard_slab(s, slab);
		}
	}

	return refilled;
}

#ifdef CONFIG_NUMA
static unsigned int
__refill_objects_any(struct kmem_cache *s, void **p, gfp_t gfp, unsigned int min,
		     unsigned int max)
{
	struct zonelist *zonelist;
	struct zoneref *z;
	struct zone *zone;
	enum zone_type highest_zoneidx = gfp_zone(gfp);
	unsigned int cpuset_mems_cookie;
	unsigned int refilled = 0;

	/* see get_from_any_partial() for the defrag ratio description */
	if (!s->remote_node_defrag_ratio ||
			get_cycles() % 1024 > s->remote_node_defrag_ratio)
		return 0;

	do {
		cpuset_mems_cookie = read_mems_allowed_begin();
		zonelist = node_zonelist(mempolicy_slab_node(), gfp);
		for_each_zone_zonelist(zone, z, zonelist, highest_zoneidx) {
			struct kmem_cache_node *n;
			unsigned int r;

			n = get_node(s, zone_to_nid(zone));

			if (!n || !cpuset_zone_allowed(zone, gfp) ||
					n->nr_partial <= s->min_partial)
				continue;

			r = __refill_objects_node(s, p, gfp, min, max, n,
						  /* allow_spin = */ false);
			refilled += r;

			if (r >= min) {
				/*
				 * Don't check read_mems_allowed_retry() here -
				 * if mems_allowed was updated in parallel, that
				 * was a harmless race between allocation and
				 * the cpuset update
				 */
				return refilled;
			}
			p += r;
			min -= r;
			max -= r;
		}
	} while (read_mems_allowed_retry(cpuset_mems_cookie));

	return refilled;
}
#else
static inline unsigned int
__refill_objects_any(struct kmem_cache *s, void **p, gfp_t gfp, unsigned int min,
		     unsigned int max)
{
	return 0;
}
#endif

static unsigned int
refill_objects(struct kmem_cache *s, void **p, gfp_t gfp, unsigned int min,
	       unsigned int max)
{
	int local_node = numa_mem_id();
	unsigned int refilled;
	struct slab *slab;

	if (WARN_ON_ONCE(!gfpflags_allow_spinning(gfp)))
		return 0;

	refilled = __refill_objects_node(s, p, gfp, min, max,
					 get_node(s, local_node),
					 /* allow_spin = */ true);
	if (refilled >= min)
		return refilled;

	refilled += __refill_objects_any(s, p + refilled, gfp, min - refilled,
					 max - refilled);
	if (refilled >= min)
		return refilled;

new_slab:

	slab = new_slab(s, gfp, local_node);
	if (!slab)
		goto out;

	stat(s, ALLOC_SLAB);

	/*
	 * TODO: possible optimization - if we know we will consume the whole
	 * slab we might skip creating the freelist?
	 */
	refilled += alloc_from_new_slab(s, slab, p + refilled, max - refilled,
					/* allow_spin = */ true);

	if (refilled < min)
		goto new_slab;

out:
	return refilled;
}

static inline
int __kmem_cache_alloc_bulk(struct kmem_cache *s, gfp_t flags, size_t size,
			    void **p)
{
	int i;

	if (IS_ENABLED(CONFIG_SLUB_TINY) || kmem_cache_debug(s)) {
		for (i = 0; i < size; i++) {

			p[i] = ___slab_alloc(s, flags, NUMA_NO_NODE, _RET_IP_,
					     s->object_size);
			if (unlikely(!p[i]))
				goto error;

			maybe_wipe_obj_freeptr(s, p[i]);
		}
	} else {
		i = refill_objects(s, p, flags, size, size);
		if (i < size)
			goto error;
		stat_add(s, ALLOC_SLOWPATH, i);
	}

	return i;

error:
	__kmem_cache_free_bulk(s, i, p);
	return 0;

}

/*
 * Note that interrupts must be enabled when calling this function and gfp
 * flags must allow spinning.
 */
int kmem_cache_alloc_bulk_noprof(struct kmem_cache *s, gfp_t flags, size_t size,
				 void **p)
{
	unsigned int i = 0;
	void *kfence_obj;

	if (!size)
		return 0;

	s = slab_pre_alloc_hook(s, flags);
	if (unlikely(!s))
		return 0;

	/*
	 * to make things simpler, only assume at most once kfence allocated
	 * object per bulk allocation and choose its index randomly
	 */
	kfence_obj = kfence_alloc(s, s->object_size, flags);

	if (unlikely(kfence_obj)) {
		if (unlikely(size == 1)) {
			p[0] = kfence_obj;
			goto out;
		}
		size--;
	}

	i = alloc_from_pcs_bulk(s, flags, size, p);

	if (i < size) {
		/*
		 * If we ran out of memory, don't bother with freeing back to
		 * the percpu sheaves, we have bigger problems.
		 */
		if (unlikely(__kmem_cache_alloc_bulk(s, flags, size - i, p + i) == 0)) {
			if (i > 0)
				__kmem_cache_free_bulk(s, i, p);
			if (kfence_obj)
				__kfence_free(kfence_obj);
			return 0;
		}
	}

	if (unlikely(kfence_obj)) {
		int idx = get_random_u32_below(size + 1);

		if (idx != size)
			p[size] = p[idx];
		p[idx] = kfence_obj;

		size++;
	}

out:
	/*
	 * memcg and kmem_cache debug support and memory initialization.
	 * Done outside of the IRQ disabled fastpath loop.
	 */
	if (unlikely(!slab_post_alloc_hook(s, NULL, flags, size, p,
		    slab_want_init_on_alloc(flags, s), s->object_size))) {
		return 0;
	}

	return size;
}
EXPORT_SYMBOL(kmem_cache_alloc_bulk_noprof);

/*
 * Object placement in a slab is made very easy because we always start at
 * offset 0. If we tune the size of the object to the alignment then we can
 * get the required alignment by putting one properly sized object after
 * another.
 *
 * Notice that the allocation order determines the sizes of the per cpu
 * caches. Each processor has always one slab available for allocations.
 * Increasing the allocation order reduces the number of times that slabs
 * must be moved on and off the partial lists and is therefore a factor in
 * locking overhead.
 */

/*
 * Minimum / Maximum order of slab pages. This influences locking overhead
 * and slab fragmentation. A higher order reduces the number of partial slabs
 * and increases the number of allocations possible without having to
 * take the list_lock.
 */
static unsigned int slub_min_order;
static unsigned int slub_max_order =
	IS_ENABLED(CONFIG_SLUB_TINY) ? 1 : PAGE_ALLOC_COSTLY_ORDER;
static unsigned int slub_min_objects;

/*
 * Calculate the order of allocation given an slab object size.
 *
 * The order of allocation has significant impact on performance and other
 * system components. Generally order 0 allocations should be preferred since
 * order 0 does not cause fragmentation in the page allocator. Larger objects
 * be problematic to put into order 0 slabs because there may be too much
 * unused space left. We go to a higher order if more than 1/16th of the slab
 * would be wasted.
 *
 * In order to reach satisfactory performance we must ensure that a minimum
 * number of objects is in one slab. Otherwise we may generate too much
 * activity on the partial lists which requires taking the list_lock. This is
 * less a concern for large slabs though which are rarely used.
 *
 * slab_max_order specifies the order where we begin to stop considering the
 * number of objects in a slab as critical. If we reach slab_max_order then
 * we try to keep the page order as low as possible. So we accept more waste
 * of space in favor of a small page order.
 *
 * Higher order allocations also allow the placement of more objects in a
 * slab and thereby reduce object handling overhead. If the user has
 * requested a higher minimum order then we start with that one instead of
 * the smallest order which will fit the object.
 */
static inline unsigned int calc_slab_order(unsigned int size,
		unsigned int min_order, unsigned int max_order,
		unsigned int fract_leftover)
{
	unsigned int order;

	for (order = min_order; order <= max_order; order++) {

		unsigned int slab_size = (unsigned int)PAGE_SIZE << order;
		unsigned int rem;

		rem = slab_size % size;

		if (rem <= slab_size / fract_leftover)
			break;
	}

	return order;
}

static inline int calculate_order(unsigned int size)
{
	unsigned int order;
	unsigned int min_objects;
	unsigned int max_objects;
	unsigned int min_order;

	min_objects = slub_min_objects;
	if (!min_objects) {
		/*
		 * Some architectures will only update present cpus when
		 * onlining them, so don't trust the number if it's just 1. But
		 * we also don't want to use nr_cpu_ids always, as on some other
		 * architectures, there can be many possible cpus, but never
		 * onlined. Here we compromise between trying to avoid too high
		 * order on systems that appear larger than they are, and too
		 * low order on systems that appear smaller than they are.
		 */
		unsigned int nr_cpus = num_present_cpus();
		if (nr_cpus <= 1)
			nr_cpus = nr_cpu_ids;
		min_objects = 4 * (fls(nr_cpus) + 1);
	}
	/* min_objects can't be 0 because get_order(0) is undefined */
	max_objects = max(order_objects(slub_max_order, size), 1U);
	min_objects = min(min_objects, max_objects);

	min_order = max_t(unsigned int, slub_min_order,
			  get_order(min_objects * size));
	if (order_objects(min_order, size) > MAX_OBJS_PER_PAGE)
		return get_order(size * MAX_OBJS_PER_PAGE) - 1;

	/*
	 * Attempt to find best configuration for a slab. This works by first
	 * attempting to generate a layout with the best possible configuration
	 * and backing off gradually.
	 *
	 * We start with accepting at most 1/16 waste and try to find the
	 * smallest order from min_objects-derived/slab_min_order up to
	 * slab_max_order that will satisfy the constraint. Note that increasing
	 * the order can only result in same or less fractional waste, not more.
	 *
	 * If that fails, we increase the acceptable fraction of waste and try
	 * again. The last iteration with fraction of 1/2 would effectively
	 * accept any waste and give us the order determined by min_objects, as
	 * long as at least single object fits within slab_max_order.
	 */
	for (unsigned int fraction = 16; fraction > 1; fraction /= 2) {
		order = calc_slab_order(size, min_order, slub_max_order,
					fraction);
		if (order <= slub_max_order)
			return order;
	}

	/*
	 * Doh this slab cannot be placed using slab_max_order.
	 */
	order = get_order(size);
	if (order <= MAX_PAGE_ORDER)
		return order;
	return -ENOSYS;
}

static void
init_kmem_cache_node(struct kmem_cache_node *n, struct node_barn *barn)
{
	n->nr_partial = 0;
	spin_lock_init(&n->list_lock);
	INIT_LIST_HEAD(&n->partial);
#ifdef CONFIG_SLUB_DEBUG
	atomic_long_set(&n->nr_slabs, 0);
	atomic_long_set(&n->total_objects, 0);
	INIT_LIST_HEAD(&n->full);
#endif
	n->barn = barn;
	if (barn)
		barn_init(barn);
}

#ifdef CONFIG_SLUB_STATS
static inline int alloc_kmem_cache_stats(struct kmem_cache *s)
{
	BUILD_BUG_ON(PERCPU_DYNAMIC_EARLY_SIZE <
			NR_KMALLOC_TYPES * KMALLOC_SHIFT_HIGH *
			sizeof(struct kmem_cache_stats));

	s->cpu_stats = alloc_percpu(struct kmem_cache_stats);

	if (!s->cpu_stats)
		return 0;

	return 1;
}
#endif

static int init_percpu_sheaves(struct kmem_cache *s)
{
	static struct slab_sheaf bootstrap_sheaf = {};
	int cpu;

	for_each_possible_cpu(cpu) {
		struct slub_percpu_sheaves *pcs;

		pcs = per_cpu_ptr(s->cpu_sheaves, cpu);

		local_trylock_init(&pcs->lock);

		/*
		 * Bootstrap sheaf has zero size so fast-path allocation fails.
		 * It has also size == s->sheaf_capacity, so fast-path free
		 * fails. In the slow paths we recognize the situation by
		 * checking s->sheaf_capacity. This allows fast paths to assume
		 * s->cpu_sheaves and pcs->main always exists and are valid.
		 * It's also safe to share the single static bootstrap_sheaf
		 * with zero-sized objects array as it's never modified.
		 *
		 * Bootstrap_sheaf also has NULL pointer to kmem_cache so we
		 * recognize it and not attempt to free it when destroying the
		 * cache.
		 *
		 * We keep bootstrap_sheaf for kmem_cache and kmem_cache_node,
		 * caches with debug enabled, and all caches with SLUB_TINY.
		 * For kmalloc caches it's used temporarily during the initial
		 * bootstrap.
		 */
		if (!s->sheaf_capacity)
			pcs->main = &bootstrap_sheaf;
		else
			pcs->main = alloc_empty_sheaf(s, GFP_KERNEL);

		if (!pcs->main)
			return -ENOMEM;
	}

	return 0;
}

static struct kmem_cache *kmem_cache_node;

/*
 * No kmalloc_node yet so do it by hand. We know that this is the first
 * slab on the node for this slabcache. There are no concurrent accesses
 * possible.
 *
 * Note that this function only works on the kmem_cache_node
 * when allocating for the kmem_cache_node. This is used for bootstrapping
 * memory on a fresh node that has no slab structures yet.
 */
static void early_kmem_cache_node_alloc(int node)
{
	struct slab *slab;
	struct kmem_cache_node *n;

	BUG_ON(kmem_cache_node->size < sizeof(struct kmem_cache_node));

	slab = new_slab(kmem_cache_node, GFP_NOWAIT, node);

	BUG_ON(!slab);
	if (slab_nid(slab) != node) {
		pr_err("SLUB: Unable to allocate memory from node %d\n", node);
		pr_err("SLUB: Allocating a useless per node structure in order to be able to continue\n");
	}

	n = slab->freelist;
	BUG_ON(!n);
#ifdef CONFIG_SLUB_DEBUG
	init_object(kmem_cache_node, n, SLUB_RED_ACTIVE);
#endif
	n = kasan_slab_alloc(kmem_cache_node, n, GFP_KERNEL, false);
	slab->freelist = get_freepointer(kmem_cache_node, n);
	slab->inuse = 1;
	kmem_cache_node->node[node] = n;
	init_kmem_cache_node(n, NULL);
	inc_slabs_node(kmem_cache_node, node, slab->objects);

	/*
	 * No locks need to be taken here as it has just been
	 * initialized and there is no concurrent access.
	 */
	__add_partial(n, slab, ADD_TO_HEAD);
}

static void free_kmem_cache_nodes(struct kmem_cache *s)
{
	int node;
	struct kmem_cache_node *n;

	for_each_kmem_cache_node(s, node, n) {
		if (n->barn) {
			WARN_ON(n->barn->nr_full);
			WARN_ON(n->barn->nr_empty);
			kfree(n->barn);
			n->barn = NULL;
		}

		s->node[node] = NULL;
		kmem_cache_free(kmem_cache_node, n);
	}
}

void __kmem_cache_release(struct kmem_cache *s)
{
	cache_random_seq_destroy(s);
	pcs_destroy(s);
#ifdef CONFIG_SLUB_STATS
	free_percpu(s->cpu_stats);
#endif
	free_kmem_cache_nodes(s);
}

static int init_kmem_cache_nodes(struct kmem_cache *s)
{
	int node;

	for_each_node_mask(node, slab_nodes) {
		struct kmem_cache_node *n;
		struct node_barn *barn = NULL;

		if (slab_state == DOWN) {
			early_kmem_cache_node_alloc(node);
			continue;
		}

		if (cache_has_sheaves(s)) {
			barn = kmalloc_node(sizeof(*barn), GFP_KERNEL, node);

			if (!barn)
				return 0;
		}

		n = kmem_cache_alloc_node(kmem_cache_node,
						GFP_KERNEL, node);
		if (!n) {
			kfree(barn);
			return 0;
		}

		init_kmem_cache_node(n, barn);

		s->node[node] = n;
	}
	return 1;
}

static unsigned int calculate_sheaf_capacity(struct kmem_cache *s,
					     struct kmem_cache_args *args)

{
	unsigned int capacity;
	size_t size;


	if (IS_ENABLED(CONFIG_SLUB_TINY) || s->flags & SLAB_DEBUG_FLAGS)
		return 0;

	/*
	 * Bootstrap caches can't have sheaves for now (SLAB_NO_OBJ_EXT).
	 * SLAB_NOLEAKTRACE caches (e.g., kmemleak's object_cache) must not
	 * have sheaves to avoid recursion when sheaf allocation triggers
	 * kmemleak tracking.
	 */
	if (s->flags & (SLAB_NO_OBJ_EXT | SLAB_NOLEAKTRACE))
		return 0;

	/*
	 * For now we use roughly similar formula (divided by two as there are
	 * two percpu sheaves) as what was used for percpu partial slabs, which
	 * should result in similar lock contention (barn or list_lock)
	 */
	if (s->size >= PAGE_SIZE)
		capacity = 4;
	else if (s->size >= 1024)
		capacity = 12;
	else if (s->size >= 256)
		capacity = 26;
	else
		capacity = 60;

	/* Increment capacity to make sheaf exactly a kmalloc size bucket */
	size = struct_size_t(struct slab_sheaf, objects, capacity);
	size = kmalloc_size_roundup(size);
	capacity = (size - struct_size_t(struct slab_sheaf, objects, 0)) / sizeof(void *);

	/*
	 * Respect an explicit request for capacity that's typically motivated by
	 * expected maximum size of kmem_cache_prefill_sheaf() to not end up
	 * using low-performance oversize sheaves
	 */
	return max(capacity, args->sheaf_capacity);
}

/*
 * calculate_sizes() determines the order and the distribution of data within
 * a slab object.
 */
static int calculate_sizes(struct kmem_cache_args *args, struct kmem_cache *s)
{
	slab_flags_t flags = s->flags;
	unsigned int size = s->object_size;
	unsigned int aligned_size;
	unsigned int order;

	/*
	 * Round up object size to the next word boundary. We can only
	 * place the free pointer at word boundaries and this determines
	 * the possible location of the free pointer.
	 */
	size = ALIGN(size, sizeof(void *));

#ifdef CONFIG_SLUB_DEBUG
	/*
	 * Determine if we can poison the object itself. If the user of
	 * the slab may touch the object after free or before allocation
	 * then we should never poison the object itself.
	 */
	if ((flags & SLAB_POISON) && !(flags & SLAB_TYPESAFE_BY_RCU) &&
			!s->ctor)
		s->flags |= __OBJECT_POISON;
	else
		s->flags &= ~__OBJECT_POISON;


	/*
	 * If we are Redzoning and there is no space between the end of the
	 * object and the following fields, add one word so the right Redzone
	 * is non-empty.
	 */
	if ((flags & SLAB_RED_ZONE) && size == s->object_size)
		size += sizeof(void *);
#endif

	/*
	 * With that we have determined the number of bytes in actual use
	 * by the object and redzoning.
	 */
	s->inuse = size;

	if (((flags & SLAB_TYPESAFE_BY_RCU) && !args->use_freeptr_offset) ||
	    (flags & SLAB_POISON) ||
	    (s->ctor && !args->use_freeptr_offset) ||
	    ((flags & SLAB_RED_ZONE) &&
	     (s->object_size < sizeof(void *) || slub_debug_orig_size(s)))) {
		/*
		 * Relocate free pointer after the object if it is not
		 * permitted to overwrite the first word of the object on
		 * kmem_cache_free.
		 *
		 * This is the case if we do RCU, have a constructor, are
		 * poisoning the objects, or are redzoning an object smaller
		 * than sizeof(void *) or are redzoning an object with
		 * slub_debug_orig_size() enabled, in which case the right
		 * redzone may be extended.
		 *
		 * The assumption that s->offset >= s->inuse means free
		 * pointer is outside of the object is used in the
		 * freeptr_outside_object() function. If that is no
		 * longer true, the function needs to be modified.
		 */
		s->offset = size;
		size += sizeof(void *);
	} else if (((flags & SLAB_TYPESAFE_BY_RCU) || s->ctor) &&
			args->use_freeptr_offset) {
		s->offset = args->freeptr_offset;
	} else {
		/*
		 * Store freelist pointer near middle of object to keep
		 * it away from the edges of the object to avoid small
		 * sized over/underflows from neighboring allocations.
		 */
		s->offset = ALIGN_DOWN(s->object_size / 2, sizeof(void *));
	}

#ifdef CONFIG_SLUB_DEBUG
	if (flags & SLAB_STORE_USER) {
		/*
		 * Need to store information about allocs and frees after
		 * the object.
		 */
		size += 2 * sizeof(struct track);

		/* Save the original kmalloc request size */
		if (flags & SLAB_KMALLOC)
			size += sizeof(unsigned long);
	}
#endif

	kasan_cache_create(s, &size, &s->flags);
#ifdef CONFIG_SLUB_DEBUG
	if (flags & SLAB_RED_ZONE) {
		/*
		 * Add some empty padding so that we can catch
		 * overwrites from earlier objects rather than let
		 * tracking information or the free pointer be
		 * corrupted if a user writes before the start
		 * of the object.
		 */
		size += sizeof(void *);

		s->red_left_pad = sizeof(void *);
		s->red_left_pad = ALIGN(s->red_left_pad, s->align);
		size += s->red_left_pad;
	}
#endif

	/*
	 * SLUB stores one object immediately after another beginning from
	 * offset 0. In order to align the objects we have to simply size
	 * each object to conform to the alignment.
	 */
	aligned_size = ALIGN(size, s->align);
#if defined(CONFIG_SLAB_OBJ_EXT) && defined(CONFIG_64BIT)
	if (slab_args_unmergeable(args, s->flags) &&
			(aligned_size - size >= sizeof(struct slabobj_ext)))
		s->flags |= SLAB_OBJ_EXT_IN_OBJ;
#endif
	size = aligned_size;

	s->size = size;
	s->reciprocal_size = reciprocal_value(size);
	order = calculate_order(size);

	if ((int)order < 0)
		return 0;

	s->allocflags = __GFP_COMP;

	if (s->flags & SLAB_CACHE_DMA)
		s->allocflags |= GFP_DMA;

	if (s->flags & SLAB_CACHE_DMA32)
		s->allocflags |= GFP_DMA32;

	if (s->flags & SLAB_RECLAIM_ACCOUNT)
		s->allocflags |= __GFP_RECLAIMABLE;

	/*
	 * For KMALLOC_NORMAL caches we enable sheaves later by
	 * bootstrap_kmalloc_sheaves() to avoid recursion
	 */
	if (!is_kmalloc_normal(s))
		s->sheaf_capacity = calculate_sheaf_capacity(s, args);

	/*
	 * Determine the number of objects per slab
	 */
	s->oo = oo_make(order, size);
	s->min = oo_make(get_order(size), size);

	return !!oo_objects(s->oo);
}

static void list_slab_objects(struct kmem_cache *s, struct slab *slab)
{
#ifdef CONFIG_SLUB_DEBUG
	void *addr = slab_address(slab);
	void *p;

	if (!slab_add_kunit_errors())
		slab_bug(s, "Objects remaining on __kmem_cache_shutdown()");

	spin_lock(&object_map_lock);
	__fill_map(object_map, s, slab);

	for_each_object(p, s, addr, slab->objects) {

		if (!test_bit(__obj_to_index(s, addr, p), object_map)) {
			if (slab_add_kunit_errors())
				continue;
			pr_err("Object 0x%p @offset=%tu\n", p, p - addr);
			print_tracking(s, p);
		}
	}
	spin_unlock(&object_map_lock);

	__slab_err(slab);
#endif
}

/*
 * Attempt to free all partial slabs on a node.
 * This is called from __kmem_cache_shutdown(). We must take list_lock
 * because sysfs file might still access partial list after the shutdowning.
 */
static void free_partial(struct kmem_cache *s, struct kmem_cache_node *n)
{
	LIST_HEAD(discard);
	struct slab *slab, *h;

	BUG_ON(irqs_disabled());
	spin_lock_irq(&n->list_lock);
	list_for_each_entry_safe(slab, h, &n->partial, slab_list) {
		if (!slab->inuse) {
			remove_partial(n, slab);
			list_add(&slab->slab_list, &discard);
		} else {
			list_slab_objects(s, slab);
		}
	}
	spin_unlock_irq(&n->list_lock);

	list_for_each_entry_safe(slab, h, &discard, slab_list)
		discard_slab(s, slab);
}

bool __kmem_cache_empty(struct kmem_cache *s)
{
	int node;
	struct kmem_cache_node *n;

	for_each_kmem_cache_node(s, node, n)
		if (n->nr_partial || node_nr_slabs(n))
			return false;
	return true;
}

/*
 * Release all resources used by a slab cache.
 */
int __kmem_cache_shutdown(struct kmem_cache *s)
{
	int node;
	struct kmem_cache_node *n;

	flush_all_cpus_locked(s);

	/* we might have rcu sheaves in flight */
	if (cache_has_sheaves(s))
		rcu_barrier();

	/* Attempt to free all objects */
	for_each_kmem_cache_node(s, node, n) {
		if (n->barn)
			barn_shrink(s, n->barn);
		free_partial(s, n);
		if (n->nr_partial || node_nr_slabs(n))
			return 1;
	}
	return 0;
}

#ifdef CONFIG_PRINTK
void __kmem_obj_info(struct kmem_obj_info *kpp, void *object, struct slab *slab)
{
	void *base;
	int __maybe_unused i;
	unsigned int objnr;
	void *objp;
	void *objp0;
	struct kmem_cache *s = slab->slab_cache;
	struct track __maybe_unused *trackp;

	kpp->kp_ptr = object;
	kpp->kp_slab = slab;
	kpp->kp_slab_cache = s;
	base = slab_address(slab);
	objp0 = kasan_reset_tag(object);
#ifdef CONFIG_SLUB_DEBUG
	objp = restore_red_left(s, objp0);
#else
	objp = objp0;
#endif
	objnr = obj_to_index(s, slab, objp);
	kpp->kp_data_offset = (unsigned long)((char *)objp0 - (char *)objp);
	objp = base + s->size * objnr;
	kpp->kp_objp = objp;
	if (WARN_ON_ONCE(objp < base || objp >= base + slab->objects * s->size
			 || (objp - base) % s->size) ||
	    !(s->flags & SLAB_STORE_USER))
		return;
#ifdef CONFIG_SLUB_DEBUG
	objp = fixup_red_left(s, objp);
	trackp = get_track(s, objp, TRACK_ALLOC);
	kpp->kp_ret = (void *)trackp->addr;
#ifdef CONFIG_STACKDEPOT
	{
		depot_stack_handle_t handle;
		unsigned long *entries;
		unsigned int nr_entries;

		handle = READ_ONCE(trackp->handle);
		if (handle) {
			nr_entries = stack_depot_fetch(handle, &entries);
			for (i = 0; i < KS_ADDRS_COUNT && i < nr_entries; i++)
				kpp->kp_stack[i] = (void *)entries[i];
		}

		trackp = get_track(s, objp, TRACK_FREE);
		handle = READ_ONCE(trackp->handle);
		if (handle) {
			nr_entries = stack_depot_fetch(handle, &entries);
			for (i = 0; i < KS_ADDRS_COUNT && i < nr_entries; i++)
				kpp->kp_free_stack[i] = (void *)entries[i];
		}
	}
#endif
#endif
}
#endif

/********************************************************************
 *		Kmalloc subsystem
 *******************************************************************/

static int __init setup_slub_min_order(const char *str, const struct kernel_param *kp)
{
	int ret;

	ret = kstrtouint(str, 0, &slub_min_order);
	if (ret)
		return ret;

	if (slub_min_order > slub_max_order)
		slub_max_order = slub_min_order;

	return 0;
}

static const struct kernel_param_ops param_ops_slab_min_order __initconst = {
	.set = setup_slub_min_order,
};
__core_param_cb(slab_min_order, &param_ops_slab_min_order, &slub_min_order, 0);
__core_param_cb(slub_min_order, &param_ops_slab_min_order, &slub_min_order, 0);

static int __init setup_slub_max_order(const char *str, const struct kernel_param *kp)
{
	int ret;

	ret = kstrtouint(str, 0, &slub_max_order);
	if (ret)
		return ret;

	slub_max_order = min_t(unsigned int, slub_max_order, MAX_PAGE_ORDER);

	if (slub_min_order > slub_max_order)
		slub_min_order = slub_max_order;

	return 0;
}

static const struct kernel_param_ops param_ops_slab_max_order __initconst = {
	.set = setup_slub_max_order,
};
__core_param_cb(slab_max_order, &param_ops_slab_max_order, &slub_max_order, 0);
__core_param_cb(slub_max_order, &param_ops_slab_max_order, &slub_max_order, 0);

core_param(slab_min_objects, slub_min_objects, uint, 0);
core_param(slub_min_objects, slub_min_objects, uint, 0);

#ifdef CONFIG_NUMA
static int __init setup_slab_strict_numa(const char *str, const struct kernel_param *kp)
{
	if (nr_node_ids > 1) {
		static_branch_enable(&strict_numa);
		pr_info("SLUB: Strict NUMA enabled.\n");
	} else {
		pr_warn("slab_strict_numa parameter set on non NUMA system.\n");
	}

	return 0;
}

static const struct kernel_param_ops param_ops_slab_strict_numa __initconst = {
	.flags = KERNEL_PARAM_OPS_FL_NOARG,
	.set = setup_slab_strict_numa,
};
__core_param_cb(slab_strict_numa, &param_ops_slab_strict_numa, NULL, 0);
#endif


#ifdef CONFIG_HARDENED_USERCOPY
/*
 * Rejects incorrectly sized objects and objects that are to be copied
 * to/from userspace but do not fall entirely within the containing slab
 * cache's usercopy region.
 *
 * Returns NULL if check passes, otherwise const char * to name of cache
 * to indicate an error.
 */
void __check_heap_object(const void *ptr, unsigned long n,
			 const struct slab *slab, bool to_user)
{
	struct kmem_cache *s;
	unsigned int offset;
	bool is_kfence = is_kfence_address(ptr);

	ptr = kasan_reset_tag(ptr);

	/* Find object and usable object size. */
	s = slab->slab_cache;

	/* Reject impossible pointers. */
	if (ptr < slab_address(slab))
		usercopy_abort("SLUB object not in SLUB page?!", NULL,
			       to_user, 0, n);

	/* Find offset within object. */
	if (is_kfence)
		offset = ptr - kfence_object_start(ptr);
	else
		offset = (ptr - slab_address(slab)) % s->size;

	/* Adjust for redzone and reject if within the redzone. */
	if (!is_kfence && kmem_cache_debug_flags(s, SLAB_RED_ZONE)) {
		if (offset < s->red_left_pad)
			usercopy_abort("SLUB object in left red zone",
				       s->name, to_user, offset, n);
		offset -= s->red_left_pad;
	}

	/* Allow address range falling entirely within usercopy region. */
	if (offset >= s->useroffset &&
	    offset - s->useroffset <= s->usersize &&
	    n <= s->useroffset - offset + s->usersize)
		return;

	usercopy_abort("SLUB object", s->name, to_user, offset, n);
}
#endif /* CONFIG_HARDENED_USERCOPY */

#define SHRINK_PROMOTE_MAX 32

/*
 * kmem_cache_shrink discards empty slabs and promotes the slabs filled
 * up most to the head of the partial lists. New allocations will then
 * fill those up and thus they can be removed from the partial lists.
 *
 * The slabs with the least items are placed last. This results in them
 * being allocated from last increasing the chance that the last objects
 * are freed in them.
 */
static int __kmem_cache_do_shrink(struct kmem_cache *s)
{
	int node;
	int i;
	struct kmem_cache_node *n;
	struct slab *slab;
	struct slab *t;
	struct list_head discard;
	struct list_head promote[SHRINK_PROMOTE_MAX];
	unsigned long flags;
	int ret = 0;

	for_each_kmem_cache_node(s, node, n) {
		INIT_LIST_HEAD(&discard);
		for (i = 0; i < SHRINK_PROMOTE_MAX; i++)
			INIT_LIST_HEAD(promote + i);

		if (n->barn)
			barn_shrink(s, n->barn);

		spin_lock_irqsave(&n->list_lock, flags);

		/*
		 * Build lists of slabs to discard or promote.
		 *
		 * Note that concurrent frees may occur while we hold the
		 * list_lock. slab->inuse here is the upper limit.
		 */
		list_for_each_entry_safe(slab, t, &n->partial, slab_list) {
			int free = slab->objects - slab->inuse;

			/* Do not reread slab->inuse */
			barrier();

			/* We do not keep full slabs on the list */
			BUG_ON(free <= 0);

			if (free == slab->objects) {
				list_move(&slab->slab_list, &discard);
				slab_clear_node_partial(slab);
				n->nr_partial--;
				dec_slabs_node(s, node, slab->objects);
			} else if (free <= SHRINK_PROMOTE_MAX)
				list_move(&slab->slab_list, promote + free - 1);
		}

		/*
		 * Promote the slabs filled up most to the head of the
		 * partial list.
		 */
		for (i = SHRINK_PROMOTE_MAX - 1; i >= 0; i--)
			list_splice(promote + i, &n->partial);

		spin_unlock_irqrestore(&n->list_lock, flags);

		/* Release empty slabs */
		list_for_each_entry_safe(slab, t, &discard, slab_list)
			free_slab(s, slab);

		if (node_nr_slabs(n))
			ret = 1;
	}

	return ret;
}

int __kmem_cache_shrink(struct kmem_cache *s)
{
	flush_all(s);
	return __kmem_cache_do_shrink(s);
}

static int slab_mem_going_offline_callback(void)
{
	struct kmem_cache *s;

	mutex_lock(&slab_mutex);
	list_for_each_entry(s, &slab_caches, list) {
		flush_all_cpus_locked(s);
		__kmem_cache_do_shrink(s);
	}
	mutex_unlock(&slab_mutex);

	return 0;
}

static int slab_mem_going_online_callback(int nid)
{
	struct kmem_cache_node *n;
	struct kmem_cache *s;
	int ret = 0;

	/*
	 * We are bringing a node online. No memory is available yet. We must
	 * allocate a kmem_cache_node structure in order to bring the node
	 * online.
	 */
	mutex_lock(&slab_mutex);
	list_for_each_entry(s, &slab_caches, list) {
		struct node_barn *barn = NULL;

		/*
		 * The structure may already exist if the node was previously
		 * onlined and offlined.
		 */
		if (get_node(s, nid))
			continue;

		if (cache_has_sheaves(s)) {
			barn = kmalloc_node(sizeof(*barn), GFP_KERNEL, nid);

			if (!barn) {
				ret = -ENOMEM;
				goto out;
			}
		}

		/*
		 * XXX: kmem_cache_alloc_node will fallback to other nodes
		 *      since memory is not yet available from the node that
		 *      is brought up.
		 */
		n = kmem_cache_alloc(kmem_cache_node, GFP_KERNEL);
		if (!n) {
			kfree(barn);
			ret = -ENOMEM;
			goto out;
		}

		init_kmem_cache_node(n, barn);

		s->node[nid] = n;
	}
	/*
	 * Any cache created after this point will also have kmem_cache_node
	 * initialized for the new node.
	 */
	node_set(nid, slab_nodes);
out:
	mutex_unlock(&slab_mutex);
	return ret;
}

static int slab_memory_callback(struct notifier_block *self,
				unsigned long action, void *arg)
{
	struct node_notify *nn = arg;
	int nid = nn->nid;
	int ret = 0;

	switch (action) {
	case NODE_ADDING_FIRST_MEMORY:
		ret = slab_mem_going_online_callback(nid);
		break;
	case NODE_REMOVING_LAST_MEMORY:
		ret = slab_mem_going_offline_callback();
		break;
	}
	if (ret)
		ret = notifier_from_errno(ret);
	else
		ret = NOTIFY_OK;
	return ret;
}

/********************************************************************
 *			Basic setup of slabs
 *******************************************************************/

/*
 * Used for early kmem_cache structures that were allocated using
 * the page allocator. Allocate them properly then fix up the pointers
 * that may be pointing to the wrong kmem_cache structure.
 */

static struct kmem_cache * __init bootstrap(struct kmem_cache *static_cache)
{
	int node;
	struct kmem_cache *s = kmem_cache_zalloc(kmem_cache, GFP_NOWAIT);
	struct kmem_cache_node *n;

	memcpy(s, static_cache, kmem_cache->object_size);

	for_each_kmem_cache_node(s, node, n) {
		struct slab *p;

		list_for_each_entry(p, &n->partial, slab_list)
			p->slab_cache = s;

#ifdef CONFIG_SLUB_DEBUG
		list_for_each_entry(p, &n->full, slab_list)
			p->slab_cache = s;
#endif
	}
	list_add(&s->list, &slab_caches);
	return s;
}

/*
 * Finish the sheaves initialization done normally by init_percpu_sheaves() and
 * init_kmem_cache_nodes(). For normal kmalloc caches we have to bootstrap it
 * since sheaves and barns are allocated by kmalloc.
 */
static void __init bootstrap_cache_sheaves(struct kmem_cache *s)
{
	struct kmem_cache_args empty_args = {};
	unsigned int capacity;
	bool failed = false;
	int node, cpu;

	capacity = calculate_sheaf_capacity(s, &empty_args);

	/* capacity can be 0 due to debugging or SLUB_TINY */
	if (!capacity)
		return;

	for_each_node_mask(node, slab_nodes) {
		struct node_barn *barn;

		barn = kmalloc_node(sizeof(*barn), GFP_KERNEL, node);

		if (!barn) {
			failed = true;
			goto out;
		}

		barn_init(barn);
		get_node(s, node)->barn = barn;
	}

	for_each_possible_cpu(cpu) {
		struct slub_percpu_sheaves *pcs;

		pcs = per_cpu_ptr(s->cpu_sheaves, cpu);

		pcs->main = __alloc_empty_sheaf(s, GFP_KERNEL, capacity);

		if (!pcs->main) {
			failed = true;
			break;
		}
	}

out:
	/*
	 * It's still early in boot so treat this like same as a failure to
	 * create the kmalloc cache in the first place
	 */
	if (failed)
		panic("Out of memory when creating kmem_cache %s\n", s->name);

	s->sheaf_capacity = capacity;
}

static void __init bootstrap_kmalloc_sheaves(void)
{
	enum kmalloc_cache_type type;

	for (type = KMALLOC_NORMAL; type <= KMALLOC_RANDOM_END; type++) {
		for (int idx = 0; idx < KMALLOC_SHIFT_HIGH + 1; idx++) {
			if (kmalloc_caches[type][idx])
				bootstrap_cache_sheaves(kmalloc_caches[type][idx]);
		}
	}
}

void __init kmem_cache_init(void)
{
	static __initdata struct kmem_cache boot_kmem_cache,
		boot_kmem_cache_node;
	int node;

	if (debug_guardpage_minorder())
		slub_max_order = 0;

	/* Inform pointer hashing choice about slub debugging state. */
	hash_pointers_finalize(__slub_debug_enabled());

	kmem_cache_node = &boot_kmem_cache_node;
	kmem_cache = &boot_kmem_cache;

	/*
	 * Initialize the nodemask for which we will allocate per node
	 * structures. Here we don't need taking slab_mutex yet.
	 */
	for_each_node_state(node, N_MEMORY)
		node_set(node, slab_nodes);

	create_boot_cache(kmem_cache_node, "kmem_cache_node",
			sizeof(struct kmem_cache_node),
			SLAB_HWCACHE_ALIGN | SLAB_NO_OBJ_EXT, 0, 0);

	hotplug_node_notifier(slab_memory_callback, SLAB_CALLBACK_PRI);

	/* Able to allocate the per node structures */
	slab_state = PARTIAL;

	create_boot_cache(kmem_cache, "kmem_cache",
			offsetof(struct kmem_cache, node) +
				nr_node_ids * sizeof(struct kmem_cache_node *),
			SLAB_HWCACHE_ALIGN | SLAB_NO_OBJ_EXT, 0, 0);

	kmem_cache = bootstrap(&boot_kmem_cache);
	kmem_cache_node = bootstrap(&boot_kmem_cache_node);

	/* Now we can use the kmem_cache to allocate kmalloc slabs */
	setup_kmalloc_cache_index_table();
	create_kmalloc_caches();

	bootstrap_kmalloc_sheaves();

	/* Setup random freelists for each cache */
	init_freelist_randomization();

	cpuhp_setup_state_nocalls(CPUHP_SLUB_DEAD, "slub:dead", NULL,
				  slub_cpu_dead);

	pr_info("SLUB: HWalign=%d, Order=%u-%u, MinObjects=%u, CPUs=%u, Nodes=%u\n",
		cache_line_size(),
		slub_min_order, slub_max_order, slub_min_objects,
		nr_cpu_ids, nr_node_ids);
}

void __init kmem_cache_init_late(void)
{
	flushwq = alloc_workqueue("slub_flushwq", WQ_MEM_RECLAIM | WQ_PERCPU,
				  0);
	WARN_ON(!flushwq);
}

int do_kmem_cache_create(struct kmem_cache *s, const char *name,
			 unsigned int size, struct kmem_cache_args *args,
			 slab_flags_t flags)
{
	int err = -EINVAL;

	s->name = name;
	s->size = s->object_size = size;

	s->flags = kmem_cache_flags(flags, s->name);
#ifdef CONFIG_SLAB_FREELIST_HARDENED
	s->random = get_random_long();
#endif
	s->align = args->align;
	s->ctor = args->ctor;
#ifdef CONFIG_HARDENED_USERCOPY
	s->useroffset = args->useroffset;
	s->usersize = args->usersize;
#endif

	if (!calculate_sizes(args, s))
		goto out;
	if (disable_higher_order_debug) {
		/*
		 * Disable debugging flags that store metadata if the min slab
		 * order increased.
		 */
		if (get_order(s->size) > get_order(s->object_size)) {
			s->flags &= ~DEBUG_METADATA_FLAGS;
			s->offset = 0;
			if (!calculate_sizes(args, s))
				goto out;
		}
	}

#ifdef system_has_freelist_aba
	if (system_has_freelist_aba() && !(s->flags & SLAB_NO_CMPXCHG)) {
		/* Enable fast mode */
		s->flags |= __CMPXCHG_DOUBLE;
	}
#endif

	/*
	 * The larger the object size is, the more slabs we want on the partial
	 * list to avoid pounding the page allocator excessively.
	 */
	s->min_partial = min_t(unsigned long, MAX_PARTIAL, ilog2(s->size) / 2);
	s->min_partial = max_t(unsigned long, MIN_PARTIAL, s->min_partial);

	s->cpu_sheaves = alloc_percpu(struct slub_percpu_sheaves);
	if (!s->cpu_sheaves) {
		err = -ENOMEM;
		goto out;
	}

#ifdef CONFIG_NUMA
	s->remote_node_defrag_ratio = 1000;
#endif

	/* Initialize the pre-computed randomized freelist if slab is up */
	if (slab_state >= UP) {
		if (init_cache_random_seq(s))
			goto out;
	}

	if (!init_kmem_cache_nodes(s))
		goto out;

#ifdef CONFIG_SLUB_STATS
	if (!alloc_kmem_cache_stats(s))
		goto out;
#endif

	err = init_percpu_sheaves(s);
	if (err)
		goto out;

	err = 0;

	/* Mutex is not taken during early boot */
	if (slab_state <= UP)
		goto out;

	/*
	 * Failing to create sysfs files is not critical to SLUB functionality.
	 * If it fails, proceed with cache creation without these files.
	 */
	if (sysfs_slab_add(s))
		pr_err("SLUB: Unable to add cache %s to sysfs\n", s->name);

	if (s->flags & SLAB_STORE_USER)
		debugfs_slab_add(s);

out:
	if (err)
		__kmem_cache_release(s);
	return err;
}

#ifdef SLAB_SUPPORTS_SYSFS
static int count_inuse(struct slab *slab)
{
	return slab->inuse;
}

static int count_total(struct slab *slab)
{
	return slab->objects;
}
#endif

#ifdef CONFIG_SLUB_DEBUG
static void validate_slab(struct kmem_cache *s, struct slab *slab,
			  unsigned long *obj_map)
{
	void *p;
	void *addr = slab_address(slab);

	if (!validate_slab_ptr(slab)) {
		slab_err(s, slab, "Not a valid slab page");
		return;
	}

	if (!check_slab(s, slab) || !on_freelist(s, slab, NULL))
		return;

	/* Now we know that a valid freelist exists */
	__fill_map(obj_map, s, slab);
	for_each_object(p, s, addr, slab->objects) {
		u8 val = test_bit(__obj_to_index(s, addr, p), obj_map) ?
			 SLUB_RED_INACTIVE : SLUB_RED_ACTIVE;

		if (!check_object(s, slab, p, val))
			break;
	}
}

static int validate_slab_node(struct kmem_cache *s,
		struct kmem_cache_node *n, unsigned long *obj_map)
{
	unsigned long count = 0;
	struct slab *slab;
	unsigned long flags;

	spin_lock_irqsave(&n->list_lock, flags);

	list_for_each_entry(slab, &n->partial, slab_list) {
		validate_slab(s, slab, obj_map);
		count++;
	}
	if (count != n->nr_partial) {
		pr_err("SLUB %s: %ld partial slabs counted but counter=%ld\n",
		       s->name, count, n->nr_partial);
		slab_add_kunit_errors();
	}

	if (!(s->flags & SLAB_STORE_USER))
		goto out;

	list_for_each_entry(slab, &n->full, slab_list) {
		validate_slab(s, slab, obj_map);
		count++;
	}
	if (count != node_nr_slabs(n)) {
		pr_err("SLUB: %s %ld slabs counted but counter=%ld\n",
		       s->name, count, node_nr_slabs(n));
		slab_add_kunit_errors();
	}

out:
	spin_unlock_irqrestore(&n->list_lock, flags);
	return count;
}

long validate_slab_cache(struct kmem_cache *s)
{
	int node;
	unsigned long count = 0;
	struct kmem_cache_node *n;
	unsigned long *obj_map;

	obj_map = bitmap_alloc(oo_objects(s->oo), GFP_KERNEL);
	if (!obj_map)
		return -ENOMEM;

	flush_all(s);
	for_each_kmem_cache_node(s, node, n)
		count += validate_slab_node(s, n, obj_map);

	bitmap_free(obj_map);

	return count;
}
EXPORT_SYMBOL(validate_slab_cache);

#ifdef CONFIG_DEBUG_FS
/*
 * Generate lists of code addresses where slabcache objects are allocated
 * and freed.
 */

struct location {
	depot_stack_handle_t handle;
	unsigned long count;
	unsigned long addr;
	unsigned long waste;
	long long sum_time;
	long min_time;
	long max_time;
	long min_pid;
	long max_pid;
	DECLARE_BITMAP(cpus, NR_CPUS);
	nodemask_t nodes;
};

struct loc_track {
	unsigned long max;
	unsigned long count;
	struct location *loc;
	loff_t idx;
};

static struct dentry *slab_debugfs_root;

static void free_loc_track(struct loc_track *t)
{
	if (t->max)
		free_pages((unsigned long)t->loc,
			get_order(sizeof(struct location) * t->max));
}

static int alloc_loc_track(struct loc_track *t, unsigned long max, gfp_t flags)
{
	struct location *l;
	int order;

	order = get_order(sizeof(struct location) * max);

	l = (void *)__get_free_pages(flags, order);
	if (!l)
		return 0;

	if (t->count) {
		memcpy(l, t->loc, sizeof(struct location) * t->count);
		free_loc_track(t);
	}
	t->max = max;
	t->loc = l;
	return 1;
}

static int add_location(struct loc_track *t, struct kmem_cache *s,
				const struct track *track,
				unsigned int orig_size)
{
	long start, end, pos;
	struct location *l;
	unsigned long caddr, chandle, cwaste;
	unsigned long age = jiffies - track->when;
	depot_stack_handle_t handle = 0;
	unsigned int waste = s->object_size - orig_size;

#ifdef CONFIG_STACKDEPOT
	handle = READ_ONCE(track->handle);
#endif
	start = -1;
	end = t->count;

	for ( ; ; ) {
		pos = start + (end - start + 1) / 2;

		/*
		 * There is nothing at "end". If we end up there
		 * we need to add something to before end.
		 */
		if (pos == end)
			break;

		l = &t->loc[pos];
		caddr = l->addr;
		chandle = l->handle;
		cwaste = l->waste;
		if ((track->addr == caddr) && (handle == chandle) &&
			(waste == cwaste)) {

			l->count++;
			if (track->when) {
				l->sum_time += age;
				if (age < l->min_time)
					l->min_time = age;
				if (age > l->max_time)
					l->max_time = age;

				if (track->pid < l->min_pid)
					l->min_pid = track->pid;
				if (track->pid > l->max_pid)
					l->max_pid = track->pid;

				cpumask_set_cpu(track->cpu,
						to_cpumask(l->cpus));
			}
			node_set(page_to_nid(virt_to_page(track)), l->nodes);
			return 1;
		}

		if (track->addr < caddr)
			end = pos;
		else if (track->addr == caddr && handle < chandle)
			end = pos;
		else if (track->addr == caddr && handle == chandle &&
				waste < cwaste)
			end = pos;
		else
			start = pos;
	}

	/*
	 * Not found. Insert new tracking element.
	 */
	if (t->count >= t->max && !alloc_loc_track(t, 2 * t->max, GFP_ATOMIC))
		return 0;

	l = t->loc + pos;
	if (pos < t->count)
		memmove(l + 1, l,
			(t->count - pos) * sizeof(struct location));
	t->count++;
	l->count = 1;
	l->addr = track->addr;
	l->sum_time = age;
	l->min_time = age;
	l->max_time = age;
	l->min_pid = track->pid;
	l->max_pid = track->pid;
	l->handle = handle;
	l->waste = waste;
	cpumask_clear(to_cpumask(l->cpus));
	cpumask_set_cpu(track->cpu, to_cpumask(l->cpus));
	nodes_clear(l->nodes);
	node_set(page_to_nid(virt_to_page(track)), l->nodes);
	return 1;
}

static void process_slab(struct loc_track *t, struct kmem_cache *s,
		struct slab *slab, enum track_item alloc,
		unsigned long *obj_map)
{
	void *addr = slab_address(slab);
	bool is_alloc = (alloc == TRACK_ALLOC);
	void *p;

	__fill_map(obj_map, s, slab);

	for_each_object(p, s, addr, slab->objects)
		if (!test_bit(__obj_to_index(s, addr, p), obj_map))
			add_location(t, s, get_track(s, p, alloc),
				     is_alloc ? get_orig_size(s, p) :
						s->object_size);
}
#endif  /* CONFIG_DEBUG_FS   */
#endif	/* CONFIG_SLUB_DEBUG */

#ifdef SLAB_SUPPORTS_SYSFS
enum slab_stat_type {
	SL_ALL,			/* All slabs */
	SL_PARTIAL,		/* Only partially allocated slabs */
	SL_CPU,			/* Only slabs used for cpu caches */
	SL_OBJECTS,		/* Determine allocated objects not slabs */
	SL_TOTAL		/* Determine object capacity not slabs */
};

#define SO_ALL		(1 << SL_ALL)
#define SO_PARTIAL	(1 << SL_PARTIAL)
#define SO_CPU		(1 << SL_CPU)
#define SO_OBJECTS	(1 << SL_OBJECTS)
#define SO_TOTAL	(1 << SL_TOTAL)

static ssize_t show_slab_objects(struct kmem_cache *s,
				 char *buf, unsigned long flags)
{
	unsigned long total = 0;
	int node;
	int x;
	unsigned long *nodes;
	int len = 0;

	nodes = kcalloc(nr_node_ids, sizeof(unsigned long), GFP_KERNEL);
	if (!nodes)
		return -ENOMEM;

	/*
	 * It is impossible to take "mem_hotplug_lock" here with "kernfs_mutex"
	 * already held which will conflict with an existing lock order:
	 *
	 * mem_hotplug_lock->slab_mutex->kernfs_mutex
	 *
	 * We don't really need mem_hotplug_lock (to hold off
	 * slab_mem_going_offline_callback) here because slab's memory hot
	 * unplug code doesn't destroy the kmem_cache->node[] data.
	 */

#ifdef CONFIG_SLUB_DEBUG
	if (flags & SO_ALL) {
		struct kmem_cache_node *n;

		for_each_kmem_cache_node(s, node, n) {

			if (flags & SO_TOTAL)
				x = node_nr_objs(n);
			else if (flags & SO_OBJECTS)
				x = node_nr_objs(n) - count_partial(n, count_free);
			else
				x = node_nr_slabs(n);
			total += x;
			nodes[node] += x;
		}

	} else
#endif
	if (flags & SO_PARTIAL) {
		struct kmem_cache_node *n;

		for_each_kmem_cache_node(s, node, n) {
			if (flags & SO_TOTAL)
				x = count_partial(n, count_total);
			else if (flags & SO_OBJECTS)
				x = count_partial(n, count_inuse);
			else
				x = n->nr_partial;
			total += x;
			nodes[node] += x;
		}
	}

	len += sysfs_emit_at(buf, len, "%lu", total);
#ifdef CONFIG_NUMA
	for (node = 0; node < nr_node_ids; node++) {
		if (nodes[node])
			len += sysfs_emit_at(buf, len, " N%d=%lu",
					     node, nodes[node]);
	}
#endif
	len += sysfs_emit_at(buf, len, "\n");
	kfree(nodes);

	return len;
}

#define to_slab_attr(n) container_of(n, struct slab_attribute, attr)
#define to_slab(n) container_of(n, struct kmem_cache, kobj)

struct slab_attribute {
	struct attribute attr;
	ssize_t (*show)(struct kmem_cache *s, char *buf);
	ssize_t (*store)(struct kmem_cache *s, const char *x, size_t count);
};

#define SLAB_ATTR_RO(_name) \
	static struct slab_attribute _name##_attr = __ATTR_RO_MODE(_name, 0400)

#define SLAB_ATTR(_name) \
	static struct slab_attribute _name##_attr = __ATTR_RW_MODE(_name, 0600)

static ssize_t slab_size_show(struct kmem_cache *s, char *buf)
{
	return sysfs_emit(buf, "%u\n", s->size);
}
SLAB_ATTR_RO(slab_size);

static ssize_t align_show(struct kmem_cache *s, char *buf)
{
	return sysfs_emit(buf, "%u\n", s->align);
}
SLAB_ATTR_RO(align);

static ssize_t object_size_show(struct kmem_cache *s, char *buf)
{
	return sysfs_emit(buf, "%u\n", s->object_size);
}
SLAB_ATTR_RO(object_size);

static ssize_t objs_per_slab_show(struct kmem_cache *s, char *buf)
{
	return sysfs_emit(buf, "%u\n", oo_objects(s->oo));
}
SLAB_ATTR_RO(objs_per_slab);

static ssize_t order_show(struct kmem_cache *s, char *buf)
{
	return sysfs_emit(buf, "%u\n", oo_order(s->oo));
}
SLAB_ATTR_RO(order);

static ssize_t sheaf_capacity_show(struct kmem_cache *s, char *buf)
{
	return sysfs_emit(buf, "%u\n", s->sheaf_capacity);
}
SLAB_ATTR_RO(sheaf_capacity);

static ssize_t min_partial_show(struct kmem_cache *s, char *buf)
{
	return sysfs_emit(buf, "%lu\n", s->min_partial);
}

static ssize_t min_partial_store(struct kmem_cache *s, const char *buf,
				 size_t length)
{
	unsigned long min;
	int err;

	err = kstrtoul(buf, 10, &min);
	if (err)
		return err;

	s->min_partial = min;
	return length;
}
SLAB_ATTR(min_partial);

static ssize_t cpu_partial_show(struct kmem_cache *s, char *buf)
{
	return sysfs_emit(buf, "0\n");
}

static ssize_t cpu_partial_store(struct kmem_cache *s, const char *buf,
				 size_t length)
{
	unsigned int objects;
	int err;

	err = kstrtouint(buf, 10, &objects);
	if (err)
		return err;
	if (objects)
		return -EINVAL;

	return length;
}
SLAB_ATTR(cpu_partial);

static ssize_t ctor_show(struct kmem_cache *s, char *buf)
{
	if (!s->ctor)
		return 0;
	return sysfs_emit(buf, "%pS\n", s->ctor);
}
SLAB_ATTR_RO(ctor);

static ssize_t aliases_show(struct kmem_cache *s, char *buf)
{
	return sysfs_emit(buf, "%d\n", s->refcount < 0 ? 0 : s->refcount - 1);
}
SLAB_ATTR_RO(aliases);

static ssize_t partial_show(struct kmem_cache *s, char *buf)
{
	return show_slab_objects(s, buf, SO_PARTIAL);
}
SLAB_ATTR_RO(partial);

static ssize_t cpu_slabs_show(struct kmem_cache *s, char *buf)
{
	return show_slab_objects(s, buf, SO_CPU);
}
SLAB_ATTR_RO(cpu_slabs);

static ssize_t objects_partial_show(struct kmem_cache *s, char *buf)
{
	return show_slab_objects(s, buf, SO_PARTIAL|SO_OBJECTS);
}
SLAB_ATTR_RO(objects_partial);

static ssize_t slabs_cpu_partial_show(struct kmem_cache *s, char *buf)
{
	return sysfs_emit(buf, "0(0)\n");
}
SLAB_ATTR_RO(slabs_cpu_partial);

static ssize_t reclaim_account_show(struct kmem_cache *s, char *buf)
{
	return sysfs_emit(buf, "%d\n", !!(s->flags & SLAB_RECLAIM_ACCOUNT));
}
SLAB_ATTR_RO(reclaim_account);

static ssize_t hwcache_align_show(struct kmem_cache *s, char *buf)
{
	return sysfs_emit(buf, "%d\n", !!(s->flags & SLAB_HWCACHE_ALIGN));
}
SLAB_ATTR_RO(hwcache_align);

#ifdef CONFIG_ZONE_DMA
static ssize_t cache_dma_show(struct kmem_cache *s, char *buf)
{
	return sysfs_emit(buf, "%d\n", !!(s->flags & SLAB_CACHE_DMA));
}
SLAB_ATTR_RO(cache_dma);
#endif

#ifdef CONFIG_HARDENED_USERCOPY
static ssize_t usersize_show(struct kmem_cache *s, char *buf)
{
	return sysfs_emit(buf, "%u\n", s->usersize);
}
SLAB_ATTR_RO(usersize);
#endif

static ssize_t destroy_by_rcu_show(struct kmem_cache *s, char *buf)
{
	return sysfs_emit(buf, "%d\n", !!(s->flags & SLAB_TYPESAFE_BY_RCU));
}
SLAB_ATTR_RO(destroy_by_rcu);

#ifdef CONFIG_SLUB_DEBUG
static ssize_t slabs_show(struct kmem_cache *s, char *buf)
{
	return show_slab_objects(s, buf, SO_ALL);
}
SLAB_ATTR_RO(slabs);

static ssize_t total_objects_show(struct kmem_cache *s, char *buf)
{
	return show_slab_objects(s, buf, SO_ALL|SO_TOTAL);
}
SLAB_ATTR_RO(total_objects);

static ssize_t objects_show(struct kmem_cache *s, char *buf)
{
	return show_slab_objects(s, buf, SO_ALL|SO_OBJECTS);
}
SLAB_ATTR_RO(objects);

static ssize_t sanity_checks_show(struct kmem_cache *s, char *buf)
{
	return sysfs_emit(buf, "%d\n", !!(s->flags & SLAB_CONSISTENCY_CHECKS));
}
SLAB_ATTR_RO(sanity_checks);

static ssize_t trace_show(struct kmem_cache *s, char *buf)
{
	return sysfs_emit(buf, "%d\n", !!(s->flags & SLAB_TRACE));
}
SLAB_ATTR_RO(trace);

static ssize_t red_zone_show(struct kmem_cache *s, char *buf)
{
	return sysfs_emit(buf, "%d\n", !!(s->flags & SLAB_RED_ZONE));
}

SLAB_ATTR_RO(red_zone);

static ssize_t poison_show(struct kmem_cache *s, char *buf)
{
	return sysfs_emit(buf, "%d\n", !!(s->flags & SLAB_POISON));
}

SLAB_ATTR_RO(poison);

static ssize_t store_user_show(struct kmem_cache *s, char *buf)
{
	return sysfs_emit(buf, "%d\n", !!(s->flags & SLAB_STORE_USER));
}

SLAB_ATTR_RO(store_user);

static ssize_t validate_show(struct kmem_cache *s, char *buf)
{
	return 0;
}

static ssize_t validate_store(struct kmem_cache *s,
			const char *buf, size_t length)
{
	int ret = -EINVAL;

	if (buf[0] == '1' && kmem_cache_debug(s)) {
		ret = validate_slab_cache(s);
		if (ret >= 0)
			ret = length;
	}
	return ret;
}
SLAB_ATTR(validate);

#endif /* CONFIG_SLUB_DEBUG */

#ifdef CONFIG_FAILSLAB
static ssize_t failslab_show(struct kmem_cache *s, char *buf)
{
	return sysfs_emit(buf, "%d\n", !!(s->flags & SLAB_FAILSLAB));
}

static ssize_t failslab_store(struct kmem_cache *s, const char *buf,
				size_t length)
{
	if (s->refcount > 1)
		return -EINVAL;

	if (buf[0] == '1')
		WRITE_ONCE(s->flags, s->flags | SLAB_FAILSLAB);
	else
		WRITE_ONCE(s->flags, s->flags & ~SLAB_FAILSLAB);

	return length;
}
SLAB_ATTR(failslab);
#endif

static ssize_t shrink_show(struct kmem_cache *s, char *buf)
{
	return 0;
}

static ssize_t shrink_store(struct kmem_cache *s,
			const char *buf, size_t length)
{
	if (buf[0] == '1')
		kmem_cache_shrink(s);
	else
		return -EINVAL;
	return length;
}
SLAB_ATTR(shrink);

#ifdef CONFIG_NUMA
static ssize_t remote_node_defrag_ratio_show(struct kmem_cache *s, char *buf)
{
	return sysfs_emit(buf, "%u\n", s->remote_node_defrag_ratio / 10);
}

static ssize_t remote_node_defrag_ratio_store(struct kmem_cache *s,
				const char *buf, size_t length)
{
	unsigned int ratio;
	int err;

	err = kstrtouint(buf, 10, &ratio);
	if (err)
		return err;
	if (ratio > 100)
		return -ERANGE;

	s->remote_node_defrag_ratio = ratio * 10;

	return length;
}
SLAB_ATTR(remote_node_defrag_ratio);
#endif

#ifdef CONFIG_SLUB_STATS
static int show_stat(struct kmem_cache *s, char *buf, enum stat_item si)
{
	unsigned long sum  = 0;
	int cpu;
	int len = 0;
	int *data = kmalloc_array(nr_cpu_ids, sizeof(int), GFP_KERNEL);

	if (!data)
		return -ENOMEM;

	for_each_online_cpu(cpu) {
		unsigned int x = per_cpu_ptr(s->cpu_stats, cpu)->stat[si];

		data[cpu] = x;
		sum += x;
	}

	len += sysfs_emit_at(buf, len, "%lu", sum);

#ifdef CONFIG_SMP
	for_each_online_cpu(cpu) {
		if (data[cpu])
			len += sysfs_emit_at(buf, len, " C%d=%u",
					     cpu, data[cpu]);
	}
#endif
	kfree(data);
	len += sysfs_emit_at(buf, len, "\n");

	return len;
}

static void clear_stat(struct kmem_cache *s, enum stat_item si)
{
	int cpu;

	for_each_online_cpu(cpu)
		per_cpu_ptr(s->cpu_stats, cpu)->stat[si] = 0;
}

#define STAT_ATTR(si, text) 					\
static ssize_t text##_show(struct kmem_cache *s, char *buf)	\
{								\
	return show_stat(s, buf, si);				\
}								\
static ssize_t text##_store(struct kmem_cache *s,		\
				const char *buf, size_t length)	\
{								\
	if (buf[0] != '0')					\
		return -EINVAL;					\
	clear_stat(s, si);					\
	return length;						\
}								\
SLAB_ATTR(text);						\

STAT_ATTR(ALLOC_FASTPATH, alloc_fastpath);
STAT_ATTR(ALLOC_SLOWPATH, alloc_slowpath);
STAT_ATTR(FREE_RCU_SHEAF, free_rcu_sheaf);
STAT_ATTR(FREE_RCU_SHEAF_FAIL, free_rcu_sheaf_fail);
STAT_ATTR(FREE_FASTPATH, free_fastpath);
STAT_ATTR(FREE_SLOWPATH, free_slowpath);
STAT_ATTR(FREE_ADD_PARTIAL, free_add_partial);
STAT_ATTR(FREE_REMOVE_PARTIAL, free_remove_partial);
STAT_ATTR(ALLOC_SLAB, alloc_slab);
STAT_ATTR(ALLOC_NODE_MISMATCH, alloc_node_mismatch);
STAT_ATTR(FREE_SLAB, free_slab);
STAT_ATTR(ORDER_FALLBACK, order_fallback);
STAT_ATTR(CMPXCHG_DOUBLE_FAIL, cmpxchg_double_fail);
STAT_ATTR(SHEAF_FLUSH, sheaf_flush);
STAT_ATTR(SHEAF_REFILL, sheaf_refill);
STAT_ATTR(SHEAF_ALLOC, sheaf_alloc);
STAT_ATTR(SHEAF_FREE, sheaf_free);
STAT_ATTR(BARN_GET, barn_get);
STAT_ATTR(BARN_GET_FAIL, barn_get_fail);
STAT_ATTR(BARN_PUT, barn_put);
STAT_ATTR(BARN_PUT_FAIL, barn_put_fail);
STAT_ATTR(SHEAF_PREFILL_FAST, sheaf_prefill_fast);
STAT_ATTR(SHEAF_PREFILL_SLOW, sheaf_prefill_slow);
STAT_ATTR(SHEAF_PREFILL_OVERSIZE, sheaf_prefill_oversize);
STAT_ATTR(SHEAF_RETURN_FAST, sheaf_return_fast);
STAT_ATTR(SHEAF_RETURN_SLOW, sheaf_return_slow);
#endif	/* CONFIG_SLUB_STATS */

#ifdef CONFIG_KFENCE
static ssize_t skip_kfence_show(struct kmem_cache *s, char *buf)
{
	return sysfs_emit(buf, "%d\n", !!(s->flags & SLAB_SKIP_KFENCE));
}

static ssize_t skip_kfence_store(struct kmem_cache *s,
			const char *buf, size_t length)
{
	int ret = length;

	if (buf[0] == '0')
		s->flags &= ~SLAB_SKIP_KFENCE;
	else if (buf[0] == '1')
		s->flags |= SLAB_SKIP_KFENCE;
	else
		ret = -EINVAL;

	return ret;
}
SLAB_ATTR(skip_kfence);
#endif

static struct attribute *slab_attrs[] = {
	&slab_size_attr.attr,
	&object_size_attr.attr,
	&objs_per_slab_attr.attr,
	&order_attr.attr,
	&sheaf_capacity_attr.attr,
	&min_partial_attr.attr,
	&cpu_partial_attr.attr,
	&objects_partial_attr.attr,
	&partial_attr.attr,
	&cpu_slabs_attr.attr,
	&ctor_attr.attr,
	&aliases_attr.attr,
	&align_attr.attr,
	&hwcache_align_attr.attr,
	&reclaim_account_attr.attr,
	&destroy_by_rcu_attr.attr,
	&shrink_attr.attr,
	&slabs_cpu_partial_attr.attr,
#ifdef CONFIG_SLUB_DEBUG
	&total_objects_attr.attr,
	&objects_attr.attr,
	&slabs_attr.attr,
	&sanity_checks_attr.attr,
	&trace_attr.attr,
	&red_zone_attr.attr,
	&poison_attr.attr,
	&store_user_attr.attr,
	&validate_attr.attr,
#endif
#ifdef CONFIG_ZONE_DMA
	&cache_dma_attr.attr,
#endif
#ifdef CONFIG_NUMA
	&remote_node_defrag_ratio_attr.attr,
#endif
#ifdef CONFIG_SLUB_STATS
	&alloc_fastpath_attr.attr,
	&alloc_slowpath_attr.attr,
	&free_rcu_sheaf_attr.attr,
	&free_rcu_sheaf_fail_attr.attr,
	&free_fastpath_attr.attr,
	&free_slowpath_attr.attr,
	&free_add_partial_attr.attr,
	&free_remove_partial_attr.attr,
	&alloc_slab_attr.attr,
	&alloc_node_mismatch_attr.attr,
	&free_slab_attr.attr,
	&order_fallback_attr.attr,
	&cmpxchg_double_fail_attr.attr,
	&sheaf_flush_attr.attr,
	&sheaf_refill_attr.attr,
	&sheaf_alloc_attr.attr,
	&sheaf_free_attr.attr,
	&barn_get_attr.attr,
	&barn_get_fail_attr.attr,
	&barn_put_attr.attr,
	&barn_put_fail_attr.attr,
	&sheaf_prefill_fast_attr.attr,
	&sheaf_prefill_slow_attr.attr,
	&sheaf_prefill_oversize_attr.attr,
	&sheaf_return_fast_attr.attr,
	&sheaf_return_slow_attr.attr,
#endif
#ifdef CONFIG_FAILSLAB
	&failslab_attr.attr,
#endif
#ifdef CONFIG_HARDENED_USERCOPY
	&usersize_attr.attr,
#endif
#ifdef CONFIG_KFENCE
	&skip_kfence_attr.attr,
#endif

	NULL
};

static const struct attribute_group slab_attr_group = {
	.attrs = slab_attrs,
};

static ssize_t slab_attr_show(struct kobject *kobj,
				struct attribute *attr,
				char *buf)
{
	struct slab_attribute *attribute;
	struct kmem_cache *s;

	attribute = to_slab_attr(attr);
	s = to_slab(kobj);

	if (!attribute->show)
		return -EIO;

	return attribute->show(s, buf);
}

static ssize_t slab_attr_store(struct kobject *kobj,
				struct attribute *attr,
				const char *buf, size_t len)
{
	struct slab_attribute *attribute;
	struct kmem_cache *s;

	attribute = to_slab_attr(attr);
	s = to_slab(kobj);

	if (!attribute->store)
		return -EIO;

	return attribute->store(s, buf, len);
}

static void kmem_cache_release(struct kobject *k)
{
	slab_kmem_cache_release(to_slab(k));
}

static const struct sysfs_ops slab_sysfs_ops = {
	.show = slab_attr_show,
	.store = slab_attr_store,
};

static const struct kobj_type slab_ktype = {
	.sysfs_ops = &slab_sysfs_ops,
	.release = kmem_cache_release,
};

static struct kset *slab_kset;

static inline struct kset *cache_kset(struct kmem_cache *s)
{
	return slab_kset;
}

#define ID_STR_LENGTH 32

/* Create a unique string id for a slab cache:
 *
 * Format	:[flags-]size
 */
static char *create_unique_id(struct kmem_cache *s)
{
	char *name = kmalloc(ID_STR_LENGTH, GFP_KERNEL);
	char *p = name;

	if (!name)
		return ERR_PTR(-ENOMEM);

	*p++ = ':';
	/*
	 * First flags affecting slabcache operations. We will only
	 * get here for aliasable slabs so we do not need to support
	 * too many flags. The flags here must cover all flags that
	 * are matched during merging to guarantee that the id is
	 * unique.
	 */
	if (s->flags & SLAB_CACHE_DMA)
		*p++ = 'd';
	if (s->flags & SLAB_CACHE_DMA32)
		*p++ = 'D';
	if (s->flags & SLAB_RECLAIM_ACCOUNT)
		*p++ = 'a';
	if (s->flags & SLAB_CONSISTENCY_CHECKS)
		*p++ = 'F';
	if (s->flags & SLAB_ACCOUNT)
		*p++ = 'A';
	if (p != name + 1)
		*p++ = '-';
	p += snprintf(p, ID_STR_LENGTH - (p - name), "%07u", s->size);

	if (WARN_ON(p > name + ID_STR_LENGTH - 1)) {
		kfree(name);
		return ERR_PTR(-EINVAL);
	}
	kmsan_unpoison_memory(name, p - name);
	return name;
}

static int sysfs_slab_add(struct kmem_cache *s)
{
	int err;
	const char *name;
	struct kset *kset = cache_kset(s);
	int unmergeable = slab_unmergeable(s);

	if (!unmergeable && disable_higher_order_debug &&
			(slub_debug & DEBUG_METADATA_FLAGS))
		unmergeable = 1;

	if (unmergeable) {
		/*
		 * Slabcache can never be merged so we can use the name proper.
		 * This is typically the case for debug situations. In that
		 * case we can catch duplicate names easily.
		 */
		sysfs_remove_link(&slab_kset->kobj, s->name);
		name = s->name;
	} else {
		/*
		 * Create a unique name for the slab as a target
		 * for the symlinks.
		 */
		name = create_unique_id(s);
		if (IS_ERR(name))
			return PTR_ERR(name);
	}

	s->kobj.kset = kset;
	err = kobject_init_and_add(&s->kobj, &slab_ktype, NULL, "%s", name);
	if (err)
		goto out;

	err = sysfs_create_group(&s->kobj, &slab_attr_group);
	if (err)
		goto out_del_kobj;

	if (!unmergeable) {
		/* Setup first alias */
		sysfs_slab_alias(s, s->name);
	}
out:
	if (!unmergeable)
		kfree(name);
	return err;
out_del_kobj:
	kobject_del(&s->kobj);
	goto out;
}

void sysfs_slab_unlink(struct kmem_cache *s)
{
	if (s->kobj.state_in_sysfs)
		kobject_del(&s->kobj);
}

void sysfs_slab_release(struct kmem_cache *s)
{
	kobject_put(&s->kobj);
}

/*
 * Need to buffer aliases during bootup until sysfs becomes
 * available lest we lose that information.
 */
struct saved_alias {
	struct kmem_cache *s;
	const char *name;
	struct saved_alias *next;
};

static struct saved_alias *alias_list;

int sysfs_slab_alias(struct kmem_cache *s, const char *name)
{
	struct saved_alias *al;

	if (slab_state == FULL) {
		/*
		 * If we have a leftover link then remove it.
		 */
		sysfs_remove_link(&slab_kset->kobj, name);
		/*
		 * The original cache may have failed to generate sysfs file.
		 * In that case, sysfs_create_link() returns -ENOENT and
		 * symbolic link creation is skipped.
		 */
		return sysfs_create_link(&slab_kset->kobj, &s->kobj, name);
	}

	al = kmalloc(sizeof(struct saved_alias), GFP_KERNEL);
	if (!al)
		return -ENOMEM;

	al->s = s;
	al->name = name;
	al->next = alias_list;
	alias_list = al;
	kmsan_unpoison_memory(al, sizeof(*al));
	return 0;
}

static int __init slab_sysfs_init(void)
{
	struct kmem_cache *s;
	int err;

	mutex_lock(&slab_mutex);

	slab_kset = kset_create_and_add("slab", NULL, kernel_kobj);
	if (!slab_kset) {
		mutex_unlock(&slab_mutex);
		pr_err("Cannot register slab subsystem.\n");
		return -ENOMEM;
	}

	slab_state = FULL;

	list_for_each_entry(s, &slab_caches, list) {
		err = sysfs_slab_add(s);
		if (err)
			pr_err("SLUB: Unable to add boot slab %s to sysfs\n",
			       s->name);
	}

	while (alias_list) {
		struct saved_alias *al = alias_list;

		alias_list = alias_list->next;
		err = sysfs_slab_alias(al->s, al->name);
		if (err)
			pr_err("SLUB: Unable to add boot slab alias %s to sysfs\n",
			       al->name);
		kfree(al);
	}

	mutex_unlock(&slab_mutex);
	return 0;
}
late_initcall(slab_sysfs_init);
#endif /* SLAB_SUPPORTS_SYSFS */

#if defined(CONFIG_SLUB_DEBUG) && defined(CONFIG_DEBUG_FS)
static int slab_debugfs_show(struct seq_file *seq, void *v)
{
	struct loc_track *t = seq->private;
	struct location *l;
	unsigned long idx;

	idx = (unsigned long) t->idx;
	if (idx < t->count) {
		l = &t->loc[idx];

		seq_printf(seq, "%7ld ", l->count);

		if (l->addr)
			seq_printf(seq, "%pS", (void *)l->addr);
		else
			seq_puts(seq, "<not-available>");

		if (l->waste)
			seq_printf(seq, " waste=%lu/%lu",
				l->count * l->waste, l->waste);

		if (l->sum_time != l->min_time) {
			seq_printf(seq, " age=%ld/%llu/%ld",
				l->min_time, div_u64(l->sum_time, l->count),
				l->max_time);
		} else
			seq_printf(seq, " age=%ld", l->min_time);

		if (l->min_pid != l->max_pid)
			seq_printf(seq, " pid=%ld-%ld", l->min_pid, l->max_pid);
		else
			seq_printf(seq, " pid=%ld",
				l->min_pid);

		if (num_online_cpus() > 1 && !cpumask_empty(to_cpumask(l->cpus)))
			seq_printf(seq, " cpus=%*pbl",
				 cpumask_pr_args(to_cpumask(l->cpus)));

		if (nr_online_nodes > 1 && !nodes_empty(l->nodes))
			seq_printf(seq, " nodes=%*pbl",
				 nodemask_pr_args(&l->nodes));

#ifdef CONFIG_STACKDEPOT
		{
			depot_stack_handle_t handle;
			unsigned long *entries;
			unsigned int nr_entries, j;

			handle = READ_ONCE(l->handle);
			if (handle) {
				nr_entries = stack_depot_fetch(handle, &entries);
				seq_puts(seq, "\n");
				for (j = 0; j < nr_entries; j++)
					seq_printf(seq, "        %pS\n", (void *)entries[j]);
			}
		}
#endif
		seq_puts(seq, "\n");
	}

	if (!idx && !t->count)
		seq_puts(seq, "No data\n");

	return 0;
}

static void slab_debugfs_stop(struct seq_file *seq, void *v)
{
}

static void *slab_debugfs_next(struct seq_file *seq, void *v, loff_t *ppos)
{
	struct loc_track *t = seq->private;

	t->idx = ++(*ppos);
	if (*ppos <= t->count)
		return ppos;

	return NULL;
}

static int cmp_loc_by_count(const void *a, const void *b)
{
	struct location *loc1 = (struct location *)a;
	struct location *loc2 = (struct location *)b;

	return cmp_int(loc2->count, loc1->count);
}

static void *slab_debugfs_start(struct seq_file *seq, loff_t *ppos)
{
	struct loc_track *t = seq->private;

	t->idx = *ppos;
	return ppos;
}

static const struct seq_operations slab_debugfs_sops = {
	.start  = slab_debugfs_start,
	.next   = slab_debugfs_next,
	.stop   = slab_debugfs_stop,
	.show   = slab_debugfs_show,
};

static int slab_debug_trace_open(struct inode *inode, struct file *filep)
{

	struct kmem_cache_node *n;
	enum track_item alloc;
	int node;
	struct loc_track *t = __seq_open_private(filep, &slab_debugfs_sops,
						sizeof(struct loc_track));
	struct kmem_cache *s = file_inode(filep)->i_private;
	unsigned long *obj_map;

	if (!t)
		return -ENOMEM;

	obj_map = bitmap_alloc(oo_objects(s->oo), GFP_KERNEL);
	if (!obj_map) {
		seq_release_private(inode, filep);
		return -ENOMEM;
	}

	alloc = debugfs_get_aux_num(filep);

	if (!alloc_loc_track(t, PAGE_SIZE / sizeof(struct location), GFP_KERNEL)) {
		bitmap_free(obj_map);
		seq_release_private(inode, filep);
		return -ENOMEM;
	}

	for_each_kmem_cache_node(s, node, n) {
		unsigned long flags;
		struct slab *slab;

		if (!node_nr_slabs(n))
			continue;

		spin_lock_irqsave(&n->list_lock, flags);
		list_for_each_entry(slab, &n->partial, slab_list)
			process_slab(t, s, slab, alloc, obj_map);
		list_for_each_entry(slab, &n->full, slab_list)
			process_slab(t, s, slab, alloc, obj_map);
		spin_unlock_irqrestore(&n->list_lock, flags);
	}

	/* Sort locations by count */
	sort(t->loc, t->count, sizeof(struct location),
	     cmp_loc_by_count, NULL);

	bitmap_free(obj_map);
	return 0;
}

static int slab_debug_trace_release(struct inode *inode, struct file *file)
{
	struct seq_file *seq = file->private_data;
	struct loc_track *t = seq->private;

	free_loc_track(t);
	return seq_release_private(inode, file);
}

static const struct file_operations slab_debugfs_fops = {
	.open    = slab_debug_trace_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = slab_debug_trace_release,
};

static void debugfs_slab_add(struct kmem_cache *s)
{
	struct dentry *slab_cache_dir;

	if (unlikely(!slab_debugfs_root))
		return;

	slab_cache_dir = debugfs_create_dir(s->name, slab_debugfs_root);

	debugfs_create_file_aux_num("alloc_traces", 0400, slab_cache_dir, s,
					TRACK_ALLOC, &slab_debugfs_fops);

	debugfs_create_file_aux_num("free_traces", 0400, slab_cache_dir, s,
					TRACK_FREE, &slab_debugfs_fops);
}

void debugfs_slab_release(struct kmem_cache *s)
{
	debugfs_lookup_and_remove(s->name, slab_debugfs_root);
}

static int __init slab_debugfs_init(void)
{
	struct kmem_cache *s;

	slab_debugfs_root = debugfs_create_dir("slab", NULL);

	list_for_each_entry(s, &slab_caches, list)
		if (s->flags & SLAB_STORE_USER)
			debugfs_slab_add(s);

	return 0;

}
__initcall(slab_debugfs_init);
#endif
/*
 * The /proc/slabinfo ABI
 */
#ifdef CONFIG_SLUB_DEBUG
void get_slabinfo(struct kmem_cache *s, struct slabinfo *sinfo)
{
	unsigned long nr_slabs = 0;
	unsigned long nr_objs = 0;
	unsigned long nr_free = 0;
	int node;
	struct kmem_cache_node *n;

	for_each_kmem_cache_node(s, node, n) {
		nr_slabs += node_nr_slabs(n);
		nr_objs += node_nr_objs(n);
		nr_free += count_partial_free_approx(n);
	}

	sinfo->active_objs = nr_objs - nr_free;
	sinfo->num_objs = nr_objs;
	sinfo->active_slabs = nr_slabs;
	sinfo->num_slabs = nr_slabs;
	sinfo->objects_per_slab = oo_objects(s->oo);
	sinfo->cache_order = oo_order(s->oo);
}
#endif /* CONFIG_SLUB_DEBUG */
