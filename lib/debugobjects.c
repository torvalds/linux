// SPDX-License-Identifier: GPL-2.0
/*
 * Generic infrastructure for lifetime debugging of objects.
 *
 * Copyright (C) 2008, Thomas Gleixner <tglx@linutronix.de>
 */

#define pr_fmt(fmt) "ODEBUG: " fmt

#include <linux/cpu.h>
#include <linux/debugobjects.h>
#include <linux/debugfs.h>
#include <linux/hash.h>
#include <linux/kmemleak.h>
#include <linux/sched.h>
#include <linux/sched/loadavg.h>
#include <linux/sched/task_stack.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/static_key.h>

#define ODEBUG_HASH_BITS	14
#define ODEBUG_HASH_SIZE	(1 << ODEBUG_HASH_BITS)

/* Must be power of two */
#define ODEBUG_BATCH_SIZE	16

/* Initial values. Must all be a multiple of batch size */
#define ODEBUG_POOL_SIZE	(64 * ODEBUG_BATCH_SIZE)
#define ODEBUG_POOL_MIN_LEVEL	(ODEBUG_POOL_SIZE / 4)

#define ODEBUG_POOL_PERCPU_SIZE	(8 * ODEBUG_BATCH_SIZE)

#define ODEBUG_CHUNK_SHIFT	PAGE_SHIFT
#define ODEBUG_CHUNK_SIZE	(1 << ODEBUG_CHUNK_SHIFT)
#define ODEBUG_CHUNK_MASK	(~(ODEBUG_CHUNK_SIZE - 1))

/*
 * We limit the freeing of debug objects via workqueue at a maximum
 * frequency of 10Hz and about 1024 objects for each freeing operation.
 * So it is freeing at most 10k debug objects per second.
 */
#define ODEBUG_FREE_WORK_MAX	(1024 / ODEBUG_BATCH_SIZE)
#define ODEBUG_FREE_WORK_DELAY	DIV_ROUND_UP(HZ, 10)

struct debug_bucket {
	struct hlist_head	list;
	raw_spinlock_t		lock;
};

struct pool_stats {
	unsigned int		cur_used;
	unsigned int		max_used;
	unsigned int		min_fill;
};

struct obj_pool {
	struct hlist_head	objects;
	unsigned int		cnt;
	unsigned int		min_cnt;
	unsigned int		max_cnt;
	struct pool_stats	stats;
} ____cacheline_aligned;


static DEFINE_PER_CPU_ALIGNED(struct obj_pool, pool_pcpu)  = {
	.max_cnt	= ODEBUG_POOL_PERCPU_SIZE,
};

static struct debug_bucket	obj_hash[ODEBUG_HASH_SIZE];

static struct debug_obj		obj_static_pool[ODEBUG_POOL_SIZE] __initdata;

static DEFINE_RAW_SPINLOCK(pool_lock);

static struct obj_pool pool_global = {
	.min_cnt		= ODEBUG_POOL_MIN_LEVEL,
	.max_cnt		= ODEBUG_POOL_SIZE,
	.stats			= {
		.min_fill	= ODEBUG_POOL_SIZE,
	},
};

static struct obj_pool pool_to_free = {
	.max_cnt	= UINT_MAX,
};

static HLIST_HEAD(pool_boot);

static unsigned long		avg_usage;
static bool			obj_freeing;

static int __data_racy			debug_objects_maxchain __read_mostly;
static int __data_racy __maybe_unused	debug_objects_maxchecked __read_mostly;
static int __data_racy			debug_objects_fixups __read_mostly;
static int __data_racy			debug_objects_warnings __read_mostly;
static bool __data_racy			debug_objects_enabled __read_mostly
					= CONFIG_DEBUG_OBJECTS_ENABLE_DEFAULT;

static const struct debug_obj_descr	*descr_test  __read_mostly;
static struct kmem_cache		*obj_cache __ro_after_init;

/*
 * Track numbers of kmem_cache_alloc()/free() calls done.
 */
static int __data_racy		debug_objects_allocated;
static int __data_racy		debug_objects_freed;

static void free_obj_work(struct work_struct *work);
static DECLARE_DELAYED_WORK(debug_obj_work, free_obj_work);

static DEFINE_STATIC_KEY_FALSE(obj_cache_enabled);

static int __init enable_object_debug(char *str)
{
	debug_objects_enabled = true;
	return 0;
}
early_param("debug_objects", enable_object_debug);

static int __init disable_object_debug(char *str)
{
	debug_objects_enabled = false;
	return 0;
}
early_param("no_debug_objects", disable_object_debug);

static const char *obj_states[ODEBUG_STATE_MAX] = {
	[ODEBUG_STATE_NONE]		= "none",
	[ODEBUG_STATE_INIT]		= "initialized",
	[ODEBUG_STATE_INACTIVE]		= "inactive",
	[ODEBUG_STATE_ACTIVE]		= "active",
	[ODEBUG_STATE_DESTROYED]	= "destroyed",
	[ODEBUG_STATE_NOTAVAILABLE]	= "not available",
};

static __always_inline unsigned int pool_count(struct obj_pool *pool)
{
	return READ_ONCE(pool->cnt);
}

static __always_inline bool pool_should_refill(struct obj_pool *pool)
{
	return pool_count(pool) < pool->min_cnt;
}

static __always_inline bool pool_must_refill(struct obj_pool *pool)
{
	return pool_count(pool) < pool->min_cnt / 2;
}

static bool pool_move_batch(struct obj_pool *dst, struct obj_pool *src)
{
	struct hlist_node *last, *next_batch, *first_batch;
	struct debug_obj *obj;

	if (dst->cnt >= dst->max_cnt || !src->cnt)
		return false;

	first_batch = src->objects.first;
	obj = hlist_entry(first_batch, typeof(*obj), node);
	last = obj->batch_last;
	next_batch = last->next;

	/* Move the next batch to the front of the source pool */
	src->objects.first = next_batch;
	if (next_batch)
		next_batch->pprev = &src->objects.first;

	/* Add the extracted batch to the destination pool */
	last->next = dst->objects.first;
	if (last->next)
		last->next->pprev = &last->next;
	first_batch->pprev = &dst->objects.first;
	dst->objects.first = first_batch;

	WRITE_ONCE(src->cnt, src->cnt - ODEBUG_BATCH_SIZE);
	WRITE_ONCE(dst->cnt, dst->cnt + ODEBUG_BATCH_SIZE);
	return true;
}

static bool pool_push_batch(struct obj_pool *dst, struct hlist_head *head)
{
	struct hlist_node *last;
	struct debug_obj *obj;

	if (dst->cnt >= dst->max_cnt)
		return false;

	obj = hlist_entry(head->first, typeof(*obj), node);
	last = obj->batch_last;

	hlist_splice_init(head, last, &dst->objects);
	WRITE_ONCE(dst->cnt, dst->cnt + ODEBUG_BATCH_SIZE);
	return true;
}

static bool pool_pop_batch(struct hlist_head *head, struct obj_pool *src)
{
	struct hlist_node *last, *next;
	struct debug_obj *obj;

	if (!src->cnt)
		return false;

	/* Move the complete list to the head */
	hlist_move_list(&src->objects, head);

	obj = hlist_entry(head->first, typeof(*obj), node);
	last = obj->batch_last;
	next = last->next;
	/* Disconnect the batch from the list */
	last->next = NULL;

	/* Move the node after last back to the source pool. */
	src->objects.first = next;
	if (next)
		next->pprev = &src->objects.first;

	WRITE_ONCE(src->cnt, src->cnt - ODEBUG_BATCH_SIZE);
	return true;
}

static struct debug_obj *__alloc_object(struct hlist_head *list)
{
	struct debug_obj *obj;

	if (unlikely(!list->first))
		return NULL;

	obj = hlist_entry(list->first, typeof(*obj), node);
	hlist_del(&obj->node);
	return obj;
}

static void pcpu_refill_stats(void)
{
	struct pool_stats *stats = &pool_global.stats;

	WRITE_ONCE(stats->cur_used, stats->cur_used + ODEBUG_BATCH_SIZE);

	if (stats->cur_used > stats->max_used)
		stats->max_used = stats->cur_used;

	if (pool_global.cnt < stats->min_fill)
		stats->min_fill = pool_global.cnt;
}

static struct debug_obj *pcpu_alloc(void)
{
	struct obj_pool *pcp = this_cpu_ptr(&pool_pcpu);

	lockdep_assert_irqs_disabled();

	for (;;) {
		struct debug_obj *obj = __alloc_object(&pcp->objects);

		if (likely(obj)) {
			pcp->cnt--;
			/*
			 * If this emptied a batch try to refill from the
			 * free pool. Don't do that if this was the top-most
			 * batch as pcpu_free() expects the per CPU pool
			 * to be less than ODEBUG_POOL_PERCPU_SIZE.
			 */
			if (unlikely(pcp->cnt < (ODEBUG_POOL_PERCPU_SIZE - ODEBUG_BATCH_SIZE) &&
				     !(pcp->cnt % ODEBUG_BATCH_SIZE))) {
				/*
				 * Don't try to allocate from the regular pool here
				 * to not exhaust it prematurely.
				 */
				if (pool_count(&pool_to_free)) {
					guard(raw_spinlock)(&pool_lock);
					pool_move_batch(pcp, &pool_to_free);
					pcpu_refill_stats();
				}
			}
			return obj;
		}

		guard(raw_spinlock)(&pool_lock);
		if (!pool_move_batch(pcp, &pool_to_free)) {
			if (!pool_move_batch(pcp, &pool_global))
				return NULL;
		}
		pcpu_refill_stats();
	}
}

static void pcpu_free(struct debug_obj *obj)
{
	struct obj_pool *pcp = this_cpu_ptr(&pool_pcpu);
	struct debug_obj *first;

	lockdep_assert_irqs_disabled();

	if (!(pcp->cnt % ODEBUG_BATCH_SIZE)) {
		obj->batch_last = &obj->node;
	} else {
		first = hlist_entry(pcp->objects.first, typeof(*first), node);
		obj->batch_last = first->batch_last;
	}
	hlist_add_head(&obj->node, &pcp->objects);
	pcp->cnt++;

	/* Pool full ? */
	if (pcp->cnt < ODEBUG_POOL_PERCPU_SIZE)
		return;

	/* Remove a batch from the per CPU pool */
	guard(raw_spinlock)(&pool_lock);
	/* Try to fit the batch into the pool_global first */
	if (!pool_move_batch(&pool_global, pcp))
		pool_move_batch(&pool_to_free, pcp);
	WRITE_ONCE(pool_global.stats.cur_used, pool_global.stats.cur_used - ODEBUG_BATCH_SIZE);
}

static void free_object_list(struct hlist_head *head)
{
	struct hlist_node *tmp;
	struct debug_obj *obj;
	int cnt = 0;

	hlist_for_each_entry_safe(obj, tmp, head, node) {
		hlist_del(&obj->node);
		kmem_cache_free(obj_cache, obj);
		cnt++;
	}
	debug_objects_freed += cnt;
}

static void fill_pool_from_freelist(void)
{
	static unsigned long state;

	/*
	 * Reuse objs from the global obj_to_free list; they will be
	 * reinitialized when allocating.
	 */
	if (!pool_count(&pool_to_free))
		return;

	/*
	 * Prevent the context from being scheduled or interrupted after
	 * setting the state flag;
	 */
	guard(irqsave)();

	/*
	 * Avoid lock contention on &pool_lock and avoid making the cache
	 * line exclusive by testing the bit before attempting to set it.
	 */
	if (test_bit(0, &state) || test_and_set_bit(0, &state))
		return;

	/* Avoid taking the lock when there is no work to do */
	while (pool_should_refill(&pool_global) && pool_count(&pool_to_free)) {
		guard(raw_spinlock)(&pool_lock);
		/* Move a batch if possible */
		pool_move_batch(&pool_global, &pool_to_free);
	}
	clear_bit(0, &state);
}

static bool kmem_alloc_batch(struct hlist_head *head, struct kmem_cache *cache, gfp_t gfp)
{
	struct hlist_node *last = NULL;
	struct debug_obj *obj;

	for (int cnt = 0; cnt < ODEBUG_BATCH_SIZE; cnt++) {
		obj = kmem_cache_zalloc(cache, gfp);
		if (!obj) {
			free_object_list(head);
			return false;
		}
		debug_objects_allocated++;

		if (!last)
			last = &obj->node;
		obj->batch_last = last;

		hlist_add_head(&obj->node, head);
	}
	return true;
}

static void fill_pool(void)
{
	static atomic_t cpus_allocating;

	/*
	 * Avoid allocation and lock contention when:
	 *   - One other CPU is already allocating
	 *   - the global pool has not reached the critical level yet
	 */
	if (!pool_must_refill(&pool_global) && atomic_read(&cpus_allocating))
		return;

	atomic_inc(&cpus_allocating);
	while (pool_should_refill(&pool_global)) {
		HLIST_HEAD(head);

		if (!kmem_alloc_batch(&head, obj_cache, __GFP_HIGH | __GFP_NOWARN))
			break;

		guard(raw_spinlock_irqsave)(&pool_lock);
		if (!pool_push_batch(&pool_global, &head))
			pool_push_batch(&pool_to_free, &head);
	}
	atomic_dec(&cpus_allocating);
}

/*
 * Lookup an object in the hash bucket.
 */
static struct debug_obj *lookup_object(void *addr, struct debug_bucket *b)
{
	struct debug_obj *obj;
	int cnt = 0;

	hlist_for_each_entry(obj, &b->list, node) {
		cnt++;
		if (obj->object == addr)
			return obj;
	}
	if (cnt > debug_objects_maxchain)
		debug_objects_maxchain = cnt;

	return NULL;
}

static void calc_usage(void)
{
	static DEFINE_RAW_SPINLOCK(avg_lock);
	static unsigned long avg_period;
	unsigned long cur, now = jiffies;

	if (!time_after_eq(now, READ_ONCE(avg_period)))
		return;

	if (!raw_spin_trylock(&avg_lock))
		return;

	WRITE_ONCE(avg_period, now + msecs_to_jiffies(10));
	cur = READ_ONCE(pool_global.stats.cur_used) * ODEBUG_FREE_WORK_MAX;
	WRITE_ONCE(avg_usage, calc_load(avg_usage, EXP_5, cur));
	raw_spin_unlock(&avg_lock);
}

static struct debug_obj *alloc_object(void *addr, struct debug_bucket *b,
				      const struct debug_obj_descr *descr)
{
	struct debug_obj *obj;

	calc_usage();

	if (static_branch_likely(&obj_cache_enabled))
		obj = pcpu_alloc();
	else
		obj = __alloc_object(&pool_boot);

	if (likely(obj)) {
		obj->object = addr;
		obj->descr  = descr;
		obj->state  = ODEBUG_STATE_NONE;
		obj->astate = 0;
		hlist_add_head(&obj->node, &b->list);
	}
	return obj;
}

/* workqueue function to free objects. */
static void free_obj_work(struct work_struct *work)
{
	static unsigned long last_use_avg;
	unsigned long cur_used, last_used, delta;
	unsigned int max_free = 0;

	WRITE_ONCE(obj_freeing, false);

	/* Rate limit freeing based on current use average */
	cur_used = READ_ONCE(avg_usage);
	last_used = last_use_avg;
	last_use_avg = cur_used;

	if (!pool_count(&pool_to_free))
		return;

	if (cur_used <= last_used) {
		delta = (last_used - cur_used) / ODEBUG_FREE_WORK_MAX;
		max_free = min(delta, ODEBUG_FREE_WORK_MAX);
	}

	for (int cnt = 0; cnt < ODEBUG_FREE_WORK_MAX; cnt++) {
		HLIST_HEAD(tofree);

		/* Acquire and drop the lock for each batch */
		scoped_guard(raw_spinlock_irqsave, &pool_lock) {
			if (!pool_to_free.cnt)
				return;

			/* Refill the global pool if possible */
			if (pool_move_batch(&pool_global, &pool_to_free)) {
				/* Don't free as there seems to be demand */
				max_free = 0;
			} else if (max_free) {
				pool_pop_batch(&tofree, &pool_to_free);
				max_free--;
			} else {
				return;
			}
		}
		free_object_list(&tofree);
	}
}

static void __free_object(struct debug_obj *obj)
{
	guard(irqsave)();
	if (static_branch_likely(&obj_cache_enabled))
		pcpu_free(obj);
	else
		hlist_add_head(&obj->node, &pool_boot);
}

/*
 * Put the object back into the pool and schedule work to free objects
 * if necessary.
 */
static void free_object(struct debug_obj *obj)
{
	__free_object(obj);
	if (!READ_ONCE(obj_freeing) && pool_count(&pool_to_free)) {
		WRITE_ONCE(obj_freeing, true);
		schedule_delayed_work(&debug_obj_work, ODEBUG_FREE_WORK_DELAY);
	}
}

static void put_objects(struct hlist_head *list)
{
	struct hlist_node *tmp;
	struct debug_obj *obj;

	/*
	 * Using free_object() puts the objects into reuse or schedules
	 * them for freeing and it get's all the accounting correct.
	 */
	hlist_for_each_entry_safe(obj, tmp, list, node) {
		hlist_del(&obj->node);
		free_object(obj);
	}
}

#ifdef CONFIG_HOTPLUG_CPU
static int object_cpu_offline(unsigned int cpu)
{
	/* Remote access is safe as the CPU is dead already */
	struct obj_pool *pcp = per_cpu_ptr(&pool_pcpu, cpu);

	put_objects(&pcp->objects);
	pcp->cnt = 0;
	return 0;
}
#endif

/* Out of memory. Free all objects from hash */
static void debug_objects_oom(void)
{
	struct debug_bucket *db = obj_hash;
	HLIST_HEAD(freelist);

	pr_warn("Out of memory. ODEBUG disabled\n");

	for (int i = 0; i < ODEBUG_HASH_SIZE; i++, db++) {
		scoped_guard(raw_spinlock_irqsave, &db->lock)
			hlist_move_list(&db->list, &freelist);

		put_objects(&freelist);
	}
}

/*
 * We use the pfn of the address for the hash. That way we can check
 * for freed objects simply by checking the affected bucket.
 */
static struct debug_bucket *get_bucket(unsigned long addr)
{
	unsigned long hash;

	hash = hash_long((addr >> ODEBUG_CHUNK_SHIFT), ODEBUG_HASH_BITS);
	return &obj_hash[hash];
}

static void debug_print_object(struct debug_obj *obj, char *msg)
{
	const struct debug_obj_descr *descr = obj->descr;
	static int limit;

	/*
	 * Don't report if lookup_object_or_alloc() by the current thread
	 * failed because lookup_object_or_alloc()/debug_objects_oom() by a
	 * concurrent thread turned off debug_objects_enabled and cleared
	 * the hash buckets.
	 */
	if (!debug_objects_enabled)
		return;

	if (limit < 5 && descr != descr_test) {
		void *hint = descr->debug_hint ?
			descr->debug_hint(obj->object) : NULL;
		limit++;
		WARN(1, KERN_ERR "ODEBUG: %s %s (active state %u) "
				 "object: %p object type: %s hint: %pS\n",
			msg, obj_states[obj->state], obj->astate,
			obj->object, descr->name, hint);
	}
	debug_objects_warnings++;
}

/*
 * Try to repair the damage, so we have a better chance to get useful
 * debug output.
 */
static bool
debug_object_fixup(bool (*fixup)(void *addr, enum debug_obj_state state),
		   void * addr, enum debug_obj_state state)
{
	if (fixup && fixup(addr, state)) {
		debug_objects_fixups++;
		return true;
	}
	return false;
}

static void debug_object_is_on_stack(void *addr, int onstack)
{
	int is_on_stack;
	static int limit;

	if (limit > 4)
		return;

	is_on_stack = object_is_on_stack(addr);
	if (is_on_stack == onstack)
		return;

	limit++;
	if (is_on_stack)
		pr_warn("object %p is on stack %p, but NOT annotated.\n", addr,
			 task_stack_page(current));
	else
		pr_warn("object %p is NOT on stack %p, but annotated.\n", addr,
			 task_stack_page(current));

	WARN_ON(1);
}

static struct debug_obj *lookup_object_or_alloc(void *addr, struct debug_bucket *b,
						const struct debug_obj_descr *descr,
						bool onstack, bool alloc_ifstatic)
{
	struct debug_obj *obj = lookup_object(addr, b);
	enum debug_obj_state state = ODEBUG_STATE_NONE;

	if (likely(obj))
		return obj;

	/*
	 * debug_object_init() unconditionally allocates untracked
	 * objects. It does not matter whether it is a static object or
	 * not.
	 *
	 * debug_object_assert_init() and debug_object_activate() allow
	 * allocation only if the descriptor callback confirms that the
	 * object is static and considered initialized. For non-static
	 * objects the allocation needs to be done from the fixup callback.
	 */
	if (unlikely(alloc_ifstatic)) {
		if (!descr->is_static_object || !descr->is_static_object(addr))
			return ERR_PTR(-ENOENT);
		/* Statically allocated objects are considered initialized */
		state = ODEBUG_STATE_INIT;
	}

	obj = alloc_object(addr, b, descr);
	if (likely(obj)) {
		obj->state = state;
		debug_object_is_on_stack(addr, onstack);
		return obj;
	}

	/* Out of memory. Do the cleanup outside of the locked region */
	debug_objects_enabled = false;
	return NULL;
}

static void debug_objects_fill_pool(void)
{
	if (!static_branch_likely(&obj_cache_enabled))
		return;

	if (likely(!pool_should_refill(&pool_global)))
		return;

	/* Try reusing objects from obj_to_free_list */
	fill_pool_from_freelist();

	if (likely(!pool_should_refill(&pool_global)))
		return;

	/*
	 * On RT enabled kernels the pool refill must happen in preemptible
	 * context -- for !RT kernels we rely on the fact that spinlock_t and
	 * raw_spinlock_t are basically the same type and this lock-type
	 * inversion works just fine.
	 */
	if (!IS_ENABLED(CONFIG_PREEMPT_RT) || preemptible()) {
		/*
		 * Annotate away the spinlock_t inside raw_spinlock_t warning
		 * by temporarily raising the wait-type to WAIT_SLEEP, matching
		 * the preemptible() condition above.
		 */
		static DEFINE_WAIT_OVERRIDE_MAP(fill_pool_map, LD_WAIT_SLEEP);
		lock_map_acquire_try(&fill_pool_map);
		fill_pool();
		lock_map_release(&fill_pool_map);
	}
}

static void
__debug_object_init(void *addr, const struct debug_obj_descr *descr, int onstack)
{
	struct debug_obj *obj, o;
	struct debug_bucket *db;
	unsigned long flags;

	debug_objects_fill_pool();

	db = get_bucket((unsigned long) addr);

	raw_spin_lock_irqsave(&db->lock, flags);

	obj = lookup_object_or_alloc(addr, db, descr, onstack, false);
	if (unlikely(!obj)) {
		raw_spin_unlock_irqrestore(&db->lock, flags);
		debug_objects_oom();
		return;
	}

	switch (obj->state) {
	case ODEBUG_STATE_NONE:
	case ODEBUG_STATE_INIT:
	case ODEBUG_STATE_INACTIVE:
		obj->state = ODEBUG_STATE_INIT;
		raw_spin_unlock_irqrestore(&db->lock, flags);
		return;
	default:
		break;
	}

	o = *obj;
	raw_spin_unlock_irqrestore(&db->lock, flags);
	debug_print_object(&o, "init");

	if (o.state == ODEBUG_STATE_ACTIVE)
		debug_object_fixup(descr->fixup_init, addr, o.state);
}

/**
 * debug_object_init - debug checks when an object is initialized
 * @addr:	address of the object
 * @descr:	pointer to an object specific debug description structure
 */
void debug_object_init(void *addr, const struct debug_obj_descr *descr)
{
	if (!debug_objects_enabled)
		return;

	__debug_object_init(addr, descr, 0);
}
EXPORT_SYMBOL_GPL(debug_object_init);

/**
 * debug_object_init_on_stack - debug checks when an object on stack is
 *				initialized
 * @addr:	address of the object
 * @descr:	pointer to an object specific debug description structure
 */
void debug_object_init_on_stack(void *addr, const struct debug_obj_descr *descr)
{
	if (!debug_objects_enabled)
		return;

	__debug_object_init(addr, descr, 1);
}
EXPORT_SYMBOL_GPL(debug_object_init_on_stack);

/**
 * debug_object_activate - debug checks when an object is activated
 * @addr:	address of the object
 * @descr:	pointer to an object specific debug description structure
 * Returns 0 for success, -EINVAL for check failed.
 */
int debug_object_activate(void *addr, const struct debug_obj_descr *descr)
{
	struct debug_obj o = { .object = addr, .state = ODEBUG_STATE_NOTAVAILABLE, .descr = descr };
	struct debug_bucket *db;
	struct debug_obj *obj;
	unsigned long flags;

	if (!debug_objects_enabled)
		return 0;

	debug_objects_fill_pool();

	db = get_bucket((unsigned long) addr);

	raw_spin_lock_irqsave(&db->lock, flags);

	obj = lookup_object_or_alloc(addr, db, descr, false, true);
	if (unlikely(!obj)) {
		raw_spin_unlock_irqrestore(&db->lock, flags);
		debug_objects_oom();
		return 0;
	} else if (likely(!IS_ERR(obj))) {
		switch (obj->state) {
		case ODEBUG_STATE_ACTIVE:
		case ODEBUG_STATE_DESTROYED:
			o = *obj;
			break;
		case ODEBUG_STATE_INIT:
		case ODEBUG_STATE_INACTIVE:
			obj->state = ODEBUG_STATE_ACTIVE;
			fallthrough;
		default:
			raw_spin_unlock_irqrestore(&db->lock, flags);
			return 0;
		}
	}

	raw_spin_unlock_irqrestore(&db->lock, flags);
	debug_print_object(&o, "activate");

	switch (o.state) {
	case ODEBUG_STATE_ACTIVE:
	case ODEBUG_STATE_NOTAVAILABLE:
		if (debug_object_fixup(descr->fixup_activate, addr, o.state))
			return 0;
		fallthrough;
	default:
		return -EINVAL;
	}
}
EXPORT_SYMBOL_GPL(debug_object_activate);

/**
 * debug_object_deactivate - debug checks when an object is deactivated
 * @addr:	address of the object
 * @descr:	pointer to an object specific debug description structure
 */
void debug_object_deactivate(void *addr, const struct debug_obj_descr *descr)
{
	struct debug_obj o = { .object = addr, .state = ODEBUG_STATE_NOTAVAILABLE, .descr = descr };
	struct debug_bucket *db;
	struct debug_obj *obj;
	unsigned long flags;

	if (!debug_objects_enabled)
		return;

	db = get_bucket((unsigned long) addr);

	raw_spin_lock_irqsave(&db->lock, flags);

	obj = lookup_object(addr, db);
	if (obj) {
		switch (obj->state) {
		case ODEBUG_STATE_DESTROYED:
			break;
		case ODEBUG_STATE_INIT:
		case ODEBUG_STATE_INACTIVE:
		case ODEBUG_STATE_ACTIVE:
			if (obj->astate)
				break;
			obj->state = ODEBUG_STATE_INACTIVE;
			fallthrough;
		default:
			raw_spin_unlock_irqrestore(&db->lock, flags);
			return;
		}
		o = *obj;
	}

	raw_spin_unlock_irqrestore(&db->lock, flags);
	debug_print_object(&o, "deactivate");
}
EXPORT_SYMBOL_GPL(debug_object_deactivate);

/**
 * debug_object_destroy - debug checks when an object is destroyed
 * @addr:	address of the object
 * @descr:	pointer to an object specific debug description structure
 */
void debug_object_destroy(void *addr, const struct debug_obj_descr *descr)
{
	struct debug_obj *obj, o;
	struct debug_bucket *db;
	unsigned long flags;

	if (!debug_objects_enabled)
		return;

	db = get_bucket((unsigned long) addr);

	raw_spin_lock_irqsave(&db->lock, flags);

	obj = lookup_object(addr, db);
	if (!obj) {
		raw_spin_unlock_irqrestore(&db->lock, flags);
		return;
	}

	switch (obj->state) {
	case ODEBUG_STATE_ACTIVE:
	case ODEBUG_STATE_DESTROYED:
		break;
	case ODEBUG_STATE_NONE:
	case ODEBUG_STATE_INIT:
	case ODEBUG_STATE_INACTIVE:
		obj->state = ODEBUG_STATE_DESTROYED;
		fallthrough;
	default:
		raw_spin_unlock_irqrestore(&db->lock, flags);
		return;
	}

	o = *obj;
	raw_spin_unlock_irqrestore(&db->lock, flags);
	debug_print_object(&o, "destroy");

	if (o.state == ODEBUG_STATE_ACTIVE)
		debug_object_fixup(descr->fixup_destroy, addr, o.state);
}
EXPORT_SYMBOL_GPL(debug_object_destroy);

/**
 * debug_object_free - debug checks when an object is freed
 * @addr:	address of the object
 * @descr:	pointer to an object specific debug description structure
 */
void debug_object_free(void *addr, const struct debug_obj_descr *descr)
{
	struct debug_obj *obj, o;
	struct debug_bucket *db;
	unsigned long flags;

	if (!debug_objects_enabled)
		return;

	db = get_bucket((unsigned long) addr);

	raw_spin_lock_irqsave(&db->lock, flags);

	obj = lookup_object(addr, db);
	if (!obj) {
		raw_spin_unlock_irqrestore(&db->lock, flags);
		return;
	}

	switch (obj->state) {
	case ODEBUG_STATE_ACTIVE:
		break;
	default:
		hlist_del(&obj->node);
		raw_spin_unlock_irqrestore(&db->lock, flags);
		free_object(obj);
		return;
	}

	o = *obj;
	raw_spin_unlock_irqrestore(&db->lock, flags);
	debug_print_object(&o, "free");

	debug_object_fixup(descr->fixup_free, addr, o.state);
}
EXPORT_SYMBOL_GPL(debug_object_free);

/**
 * debug_object_assert_init - debug checks when object should be init-ed
 * @addr:	address of the object
 * @descr:	pointer to an object specific debug description structure
 */
void debug_object_assert_init(void *addr, const struct debug_obj_descr *descr)
{
	struct debug_obj o = { .object = addr, .state = ODEBUG_STATE_NOTAVAILABLE, .descr = descr };
	struct debug_bucket *db;
	struct debug_obj *obj;
	unsigned long flags;

	if (!debug_objects_enabled)
		return;

	debug_objects_fill_pool();

	db = get_bucket((unsigned long) addr);

	raw_spin_lock_irqsave(&db->lock, flags);
	obj = lookup_object_or_alloc(addr, db, descr, false, true);
	raw_spin_unlock_irqrestore(&db->lock, flags);
	if (likely(!IS_ERR_OR_NULL(obj)))
		return;

	/* If NULL the allocation has hit OOM */
	if (!obj) {
		debug_objects_oom();
		return;
	}

	/* Object is neither tracked nor static. It's not initialized. */
	debug_print_object(&o, "assert_init");
	debug_object_fixup(descr->fixup_assert_init, addr, ODEBUG_STATE_NOTAVAILABLE);
}
EXPORT_SYMBOL_GPL(debug_object_assert_init);

/**
 * debug_object_active_state - debug checks object usage state machine
 * @addr:	address of the object
 * @descr:	pointer to an object specific debug description structure
 * @expect:	expected state
 * @next:	state to move to if expected state is found
 */
void
debug_object_active_state(void *addr, const struct debug_obj_descr *descr,
			  unsigned int expect, unsigned int next)
{
	struct debug_obj o = { .object = addr, .state = ODEBUG_STATE_NOTAVAILABLE, .descr = descr };
	struct debug_bucket *db;
	struct debug_obj *obj;
	unsigned long flags;

	if (!debug_objects_enabled)
		return;

	db = get_bucket((unsigned long) addr);

	raw_spin_lock_irqsave(&db->lock, flags);

	obj = lookup_object(addr, db);
	if (obj) {
		switch (obj->state) {
		case ODEBUG_STATE_ACTIVE:
			if (obj->astate != expect)
				break;
			obj->astate = next;
			raw_spin_unlock_irqrestore(&db->lock, flags);
			return;
		default:
			break;
		}
		o = *obj;
	}

	raw_spin_unlock_irqrestore(&db->lock, flags);
	debug_print_object(&o, "active_state");
}
EXPORT_SYMBOL_GPL(debug_object_active_state);

#ifdef CONFIG_DEBUG_OBJECTS_FREE
static void __debug_check_no_obj_freed(const void *address, unsigned long size)
{
	unsigned long flags, oaddr, saddr, eaddr, paddr, chunks;
	int cnt, objs_checked = 0;
	struct debug_obj *obj, o;
	struct debug_bucket *db;
	struct hlist_node *tmp;

	saddr = (unsigned long) address;
	eaddr = saddr + size;
	paddr = saddr & ODEBUG_CHUNK_MASK;
	chunks = ((eaddr - paddr) + (ODEBUG_CHUNK_SIZE - 1));
	chunks >>= ODEBUG_CHUNK_SHIFT;

	for (;chunks > 0; chunks--, paddr += ODEBUG_CHUNK_SIZE) {
		db = get_bucket(paddr);

repeat:
		cnt = 0;
		raw_spin_lock_irqsave(&db->lock, flags);
		hlist_for_each_entry_safe(obj, tmp, &db->list, node) {
			cnt++;
			oaddr = (unsigned long) obj->object;
			if (oaddr < saddr || oaddr >= eaddr)
				continue;

			switch (obj->state) {
			case ODEBUG_STATE_ACTIVE:
				o = *obj;
				raw_spin_unlock_irqrestore(&db->lock, flags);
				debug_print_object(&o, "free");
				debug_object_fixup(o.descr->fixup_free, (void *)oaddr, o.state);
				goto repeat;
			default:
				hlist_del(&obj->node);
				__free_object(obj);
				break;
			}
		}
		raw_spin_unlock_irqrestore(&db->lock, flags);

		if (cnt > debug_objects_maxchain)
			debug_objects_maxchain = cnt;

		objs_checked += cnt;
	}

	if (objs_checked > debug_objects_maxchecked)
		debug_objects_maxchecked = objs_checked;

	/* Schedule work to actually kmem_cache_free() objects */
	if (!READ_ONCE(obj_freeing) && pool_count(&pool_to_free)) {
		WRITE_ONCE(obj_freeing, true);
		schedule_delayed_work(&debug_obj_work, ODEBUG_FREE_WORK_DELAY);
	}
}

void debug_check_no_obj_freed(const void *address, unsigned long size)
{
	if (debug_objects_enabled)
		__debug_check_no_obj_freed(address, size);
}
#endif

#ifdef CONFIG_DEBUG_FS

static int debug_stats_show(struct seq_file *m, void *v)
{
	unsigned int cpu, pool_used, pcp_free = 0;

	/*
	 * pool_global.stats.cur_used is the number of batches currently
	 * handed out to per CPU pools. Convert it to number of objects
	 * and subtract the number of free objects in the per CPU pools.
	 * As this is lockless the number is an estimate.
	 */
	for_each_possible_cpu(cpu)
		pcp_free += per_cpu(pool_pcpu.cnt, cpu);

	pool_used = READ_ONCE(pool_global.stats.cur_used);
	pcp_free = min(pool_used, pcp_free);
	pool_used -= pcp_free;

	seq_printf(m, "max_chain     : %d\n", debug_objects_maxchain);
	seq_printf(m, "max_checked   : %d\n", debug_objects_maxchecked);
	seq_printf(m, "warnings      : %d\n", debug_objects_warnings);
	seq_printf(m, "fixups        : %d\n", debug_objects_fixups);
	seq_printf(m, "pool_free     : %u\n", pool_count(&pool_global) + pcp_free);
	seq_printf(m, "pool_pcp_free : %u\n", pcp_free);
	seq_printf(m, "pool_min_free : %u\n", data_race(pool_global.stats.min_fill));
	seq_printf(m, "pool_used     : %u\n", pool_used);
	seq_printf(m, "pool_max_used : %u\n", data_race(pool_global.stats.max_used));
	seq_printf(m, "on_free_list  : %u\n", pool_count(&pool_to_free));
	seq_printf(m, "objs_allocated: %d\n", debug_objects_allocated);
	seq_printf(m, "objs_freed    : %d\n", debug_objects_freed);
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(debug_stats);

static int __init debug_objects_init_debugfs(void)
{
	struct dentry *dbgdir;

	if (!debug_objects_enabled)
		return 0;

	dbgdir = debugfs_create_dir("debug_objects", NULL);

	debugfs_create_file("stats", 0444, dbgdir, NULL, &debug_stats_fops);

	return 0;
}
__initcall(debug_objects_init_debugfs);

#else
static inline void debug_objects_init_debugfs(void) { }
#endif

#ifdef CONFIG_DEBUG_OBJECTS_SELFTEST

/* Random data structure for the self test */
struct self_test {
	unsigned long	dummy1[6];
	int		static_init;
	unsigned long	dummy2[3];
};

static __initconst const struct debug_obj_descr descr_type_test;

static bool __init is_static_object(void *addr)
{
	struct self_test *obj = addr;

	return obj->static_init;
}

/*
 * fixup_init is called when:
 * - an active object is initialized
 */
static bool __init fixup_init(void *addr, enum debug_obj_state state)
{
	struct self_test *obj = addr;

	switch (state) {
	case ODEBUG_STATE_ACTIVE:
		debug_object_deactivate(obj, &descr_type_test);
		debug_object_init(obj, &descr_type_test);
		return true;
	default:
		return false;
	}
}

/*
 * fixup_activate is called when:
 * - an active object is activated
 * - an unknown non-static object is activated
 */
static bool __init fixup_activate(void *addr, enum debug_obj_state state)
{
	struct self_test *obj = addr;

	switch (state) {
	case ODEBUG_STATE_NOTAVAILABLE:
		return true;
	case ODEBUG_STATE_ACTIVE:
		debug_object_deactivate(obj, &descr_type_test);
		debug_object_activate(obj, &descr_type_test);
		return true;

	default:
		return false;
	}
}

/*
 * fixup_destroy is called when:
 * - an active object is destroyed
 */
static bool __init fixup_destroy(void *addr, enum debug_obj_state state)
{
	struct self_test *obj = addr;

	switch (state) {
	case ODEBUG_STATE_ACTIVE:
		debug_object_deactivate(obj, &descr_type_test);
		debug_object_destroy(obj, &descr_type_test);
		return true;
	default:
		return false;
	}
}

/*
 * fixup_free is called when:
 * - an active object is freed
 */
static bool __init fixup_free(void *addr, enum debug_obj_state state)
{
	struct self_test *obj = addr;

	switch (state) {
	case ODEBUG_STATE_ACTIVE:
		debug_object_deactivate(obj, &descr_type_test);
		debug_object_free(obj, &descr_type_test);
		return true;
	default:
		return false;
	}
}

static int __init
check_results(void *addr, enum debug_obj_state state, int fixups, int warnings)
{
	struct debug_bucket *db;
	struct debug_obj *obj;
	unsigned long flags;
	int res = -EINVAL;

	db = get_bucket((unsigned long) addr);

	raw_spin_lock_irqsave(&db->lock, flags);

	obj = lookup_object(addr, db);
	if (!obj && state != ODEBUG_STATE_NONE) {
		WARN(1, KERN_ERR "ODEBUG: selftest object not found\n");
		goto out;
	}
	if (obj && obj->state != state) {
		WARN(1, KERN_ERR "ODEBUG: selftest wrong state: %d != %d\n",
		       obj->state, state);
		goto out;
	}
	if (fixups != debug_objects_fixups) {
		WARN(1, KERN_ERR "ODEBUG: selftest fixups failed %d != %d\n",
		       fixups, debug_objects_fixups);
		goto out;
	}
	if (warnings != debug_objects_warnings) {
		WARN(1, KERN_ERR "ODEBUG: selftest warnings failed %d != %d\n",
		       warnings, debug_objects_warnings);
		goto out;
	}
	res = 0;
out:
	raw_spin_unlock_irqrestore(&db->lock, flags);
	if (res)
		debug_objects_enabled = false;
	return res;
}

static __initconst const struct debug_obj_descr descr_type_test = {
	.name			= "selftest",
	.is_static_object	= is_static_object,
	.fixup_init		= fixup_init,
	.fixup_activate		= fixup_activate,
	.fixup_destroy		= fixup_destroy,
	.fixup_free		= fixup_free,
};

static __initdata struct self_test obj = { .static_init = 0 };

static bool __init debug_objects_selftest(void)
{
	int fixups, oldfixups, warnings, oldwarnings;
	unsigned long flags;

	local_irq_save(flags);

	fixups = oldfixups = debug_objects_fixups;
	warnings = oldwarnings = debug_objects_warnings;
	descr_test = &descr_type_test;

	debug_object_init(&obj, &descr_type_test);
	if (check_results(&obj, ODEBUG_STATE_INIT, fixups, warnings))
		goto out;
	debug_object_activate(&obj, &descr_type_test);
	if (check_results(&obj, ODEBUG_STATE_ACTIVE, fixups, warnings))
		goto out;
	debug_object_activate(&obj, &descr_type_test);
	if (check_results(&obj, ODEBUG_STATE_ACTIVE, ++fixups, ++warnings))
		goto out;
	debug_object_deactivate(&obj, &descr_type_test);
	if (check_results(&obj, ODEBUG_STATE_INACTIVE, fixups, warnings))
		goto out;
	debug_object_destroy(&obj, &descr_type_test);
	if (check_results(&obj, ODEBUG_STATE_DESTROYED, fixups, warnings))
		goto out;
	debug_object_init(&obj, &descr_type_test);
	if (check_results(&obj, ODEBUG_STATE_DESTROYED, fixups, ++warnings))
		goto out;
	debug_object_activate(&obj, &descr_type_test);
	if (check_results(&obj, ODEBUG_STATE_DESTROYED, fixups, ++warnings))
		goto out;
	debug_object_deactivate(&obj, &descr_type_test);
	if (check_results(&obj, ODEBUG_STATE_DESTROYED, fixups, ++warnings))
		goto out;
	debug_object_free(&obj, &descr_type_test);
	if (check_results(&obj, ODEBUG_STATE_NONE, fixups, warnings))
		goto out;

	obj.static_init = 1;
	debug_object_activate(&obj, &descr_type_test);
	if (check_results(&obj, ODEBUG_STATE_ACTIVE, fixups, warnings))
		goto out;
	debug_object_init(&obj, &descr_type_test);
	if (check_results(&obj, ODEBUG_STATE_INIT, ++fixups, ++warnings))
		goto out;
	debug_object_free(&obj, &descr_type_test);
	if (check_results(&obj, ODEBUG_STATE_NONE, fixups, warnings))
		goto out;

#ifdef CONFIG_DEBUG_OBJECTS_FREE
	debug_object_init(&obj, &descr_type_test);
	if (check_results(&obj, ODEBUG_STATE_INIT, fixups, warnings))
		goto out;
	debug_object_activate(&obj, &descr_type_test);
	if (check_results(&obj, ODEBUG_STATE_ACTIVE, fixups, warnings))
		goto out;
	__debug_check_no_obj_freed(&obj, sizeof(obj));
	if (check_results(&obj, ODEBUG_STATE_NONE, ++fixups, ++warnings))
		goto out;
#endif
	pr_info("selftest passed\n");

out:
	debug_objects_fixups = oldfixups;
	debug_objects_warnings = oldwarnings;
	descr_test = NULL;

	local_irq_restore(flags);
	return debug_objects_enabled;
}
#else
static inline bool debug_objects_selftest(void) { return true; }
#endif

/*
 * Called during early boot to initialize the hash buckets and link
 * the static object pool objects into the poll list. After this call
 * the object tracker is fully operational.
 */
void __init debug_objects_early_init(void)
{
	int i;

	for (i = 0; i < ODEBUG_HASH_SIZE; i++)
		raw_spin_lock_init(&obj_hash[i].lock);

	/* Keep early boot simple and add everything to the boot list */
	for (i = 0; i < ODEBUG_POOL_SIZE; i++)
		hlist_add_head(&obj_static_pool[i].node, &pool_boot);
}

/*
 * Convert the statically allocated objects to dynamic ones.
 * debug_objects_mem_init() is called early so only one CPU is up and
 * interrupts are disabled, which means it is safe to replace the active
 * object references.
 */
static bool __init debug_objects_replace_static_objects(struct kmem_cache *cache)
{
	struct debug_bucket *db = obj_hash;
	struct hlist_node *tmp;
	struct debug_obj *obj;
	HLIST_HEAD(objects);
	int i;

	for (i = 0; i < ODEBUG_POOL_SIZE; i += ODEBUG_BATCH_SIZE) {
		if (!kmem_alloc_batch(&objects, cache, GFP_KERNEL))
			goto free;
		pool_push_batch(&pool_global, &objects);
	}

	/* Disconnect the boot pool. */
	pool_boot.first = NULL;

	/* Replace the active object references */
	for (i = 0; i < ODEBUG_HASH_SIZE; i++, db++) {
		hlist_move_list(&db->list, &objects);

		hlist_for_each_entry(obj, &objects, node) {
			struct debug_obj *new = pcpu_alloc();

			/* copy object data */
			*new = *obj;
			hlist_add_head(&new->node, &db->list);
		}
	}
	return true;
free:
	/* Can't use free_object_list() as the cache is not populated yet */
	hlist_for_each_entry_safe(obj, tmp, &pool_global.objects, node) {
		hlist_del(&obj->node);
		kmem_cache_free(cache, obj);
	}
	return false;
}

/*
 * Called after the kmem_caches are functional to setup a dedicated
 * cache pool, which has the SLAB_DEBUG_OBJECTS flag set. This flag
 * prevents that the debug code is called on kmem_cache_free() for the
 * debug tracker objects to avoid recursive calls.
 */
void __init debug_objects_mem_init(void)
{
	struct kmem_cache *cache;
	int extras;

	if (!debug_objects_enabled)
		return;

	if (!debug_objects_selftest())
		return;

	cache = kmem_cache_create("debug_objects_cache", sizeof (struct debug_obj), 0,
				  SLAB_DEBUG_OBJECTS | SLAB_NOLEAKTRACE, NULL);

	if (!cache || !debug_objects_replace_static_objects(cache)) {
		debug_objects_enabled = false;
		pr_warn("Out of memory.\n");
		return;
	}

	/*
	 * Adjust the thresholds for allocating and freeing objects
	 * according to the number of possible CPUs available in the
	 * system.
	 */
	extras = num_possible_cpus() * ODEBUG_BATCH_SIZE;
	pool_global.max_cnt += extras;
	pool_global.min_cnt += extras;

	/* Everything worked. Expose the cache */
	obj_cache = cache;
	static_branch_enable(&obj_cache_enabled);

#ifdef CONFIG_HOTPLUG_CPU
	cpuhp_setup_state_nocalls(CPUHP_DEBUG_OBJ_DEAD, "object:offline", NULL,
				  object_cpu_offline);
#endif
	return;
}
