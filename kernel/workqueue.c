/*
 * kernel/workqueue.c - generic async execution with shared worker pool
 *
 * Copyright (C) 2002		Ingo Molnar
 *
 *   Derived from the taskqueue/keventd code by:
 *     David Woodhouse <dwmw2@infradead.org>
 *     Andrew Morton
 *     Kai Petzke <wpp@marie.physik.tu-berlin.de>
 *     Theodore Ts'o <tytso@mit.edu>
 *
 * Made to use alloc_percpu by Christoph Lameter.
 *
 * Copyright (C) 2010		SUSE Linux Products GmbH
 * Copyright (C) 2010		Tejun Heo <tj@kernel.org>
 *
 * This is the generic async execution mechanism.  Work items as are
 * executed in process context.  The worker pool is shared and
 * automatically managed.  There is one worker pool for each CPU and
 * one extra for works which are better served by workers which are
 * not bound to any specific CPU.
 *
 * Please read Documentation/workqueue.txt for details.
 */

#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/signal.h>
#include <linux/completion.h>
#include <linux/workqueue.h>
#include <linux/slab.h>
#include <linux/cpu.h>
#include <linux/notifier.h>
#include <linux/kthread.h>
#include <linux/hardirq.h>
#include <linux/mempolicy.h>
#include <linux/freezer.h>
#include <linux/kallsyms.h>
#include <linux/debug_locks.h>
#include <linux/lockdep.h>
#include <linux/idr.h>
#include <linux/jhash.h>
#include <linux/hashtable.h>
#include <linux/rculist.h>

#include "workqueue_internal.h"

enum {
	/*
	 * worker_pool flags
	 *
	 * A bound pool is either associated or disassociated with its CPU.
	 * While associated (!DISASSOCIATED), all workers are bound to the
	 * CPU and none has %WORKER_UNBOUND set and concurrency management
	 * is in effect.
	 *
	 * While DISASSOCIATED, the cpu may be offline and all workers have
	 * %WORKER_UNBOUND set and concurrency management disabled, and may
	 * be executing on any CPU.  The pool behaves as an unbound one.
	 *
	 * Note that DISASSOCIATED can be flipped only while holding
	 * assoc_mutex to avoid changing binding state while
	 * create_worker() is in progress.
	 */
	POOL_MANAGE_WORKERS	= 1 << 0,	/* need to manage workers */
	POOL_DISASSOCIATED	= 1 << 2,	/* cpu can't serve workers */
	POOL_FREEZING		= 1 << 3,	/* freeze in progress */

	/* worker flags */
	WORKER_STARTED		= 1 << 0,	/* started */
	WORKER_DIE		= 1 << 1,	/* die die die */
	WORKER_IDLE		= 1 << 2,	/* is idle */
	WORKER_PREP		= 1 << 3,	/* preparing to run works */
	WORKER_CPU_INTENSIVE	= 1 << 6,	/* cpu intensive */
	WORKER_UNBOUND		= 1 << 7,	/* worker is unbound */

	WORKER_NOT_RUNNING	= WORKER_PREP | WORKER_UNBOUND |
				  WORKER_CPU_INTENSIVE,

	NR_STD_WORKER_POOLS	= 2,		/* # standard pools per cpu */

	UNBOUND_POOL_HASH_ORDER	= 6,		/* hashed by pool->attrs */
	BUSY_WORKER_HASH_ORDER	= 6,		/* 64 pointers */

	MAX_IDLE_WORKERS_RATIO	= 4,		/* 1/4 of busy can be idle */
	IDLE_WORKER_TIMEOUT	= 300 * HZ,	/* keep idle ones for 5 mins */

	MAYDAY_INITIAL_TIMEOUT  = HZ / 100 >= 2 ? HZ / 100 : 2,
						/* call for help after 10ms
						   (min two ticks) */
	MAYDAY_INTERVAL		= HZ / 10,	/* and then every 100ms */
	CREATE_COOLDOWN		= HZ,		/* time to breath after fail */

	/*
	 * Rescue workers are used only on emergencies and shared by
	 * all cpus.  Give -20.
	 */
	RESCUER_NICE_LEVEL	= -20,
	HIGHPRI_NICE_LEVEL	= -20,
};

/*
 * Structure fields follow one of the following exclusion rules.
 *
 * I: Modifiable by initialization/destruction paths and read-only for
 *    everyone else.
 *
 * P: Preemption protected.  Disabling preemption is enough and should
 *    only be modified and accessed from the local cpu.
 *
 * L: pool->lock protected.  Access with pool->lock held.
 *
 * X: During normal operation, modification requires pool->lock and should
 *    be done only from local cpu.  Either disabling preemption on local
 *    cpu or grabbing pool->lock is enough for read access.  If
 *    POOL_DISASSOCIATED is set, it's identical to L.
 *
 * F: wq->flush_mutex protected.
 *
 * W: workqueue_lock protected.
 *
 * R: workqueue_lock protected for writes.  Sched-RCU protected for reads.
 *
 * FR: wq->flush_mutex and workqueue_lock protected for writes.  Sched-RCU
 *     protected for reads.
 */

/* struct worker is defined in workqueue_internal.h */

struct worker_pool {
	spinlock_t		lock;		/* the pool lock */
	int			cpu;		/* I: the associated cpu */
	int			id;		/* I: pool ID */
	unsigned int		flags;		/* X: flags */

	struct list_head	worklist;	/* L: list of pending works */
	int			nr_workers;	/* L: total number of workers */

	/* nr_idle includes the ones off idle_list for rebinding */
	int			nr_idle;	/* L: currently idle ones */

	struct list_head	idle_list;	/* X: list of idle workers */
	struct timer_list	idle_timer;	/* L: worker idle timeout */
	struct timer_list	mayday_timer;	/* L: SOS timer for workers */

	/* workers are chained either in busy_hash or idle_list */
	DECLARE_HASHTABLE(busy_hash, BUSY_WORKER_HASH_ORDER);
						/* L: hash of busy workers */

	struct mutex		manager_arb;	/* manager arbitration */
	struct mutex		assoc_mutex;	/* protect POOL_DISASSOCIATED */
	struct ida		worker_ida;	/* L: for worker IDs */

	struct workqueue_attrs	*attrs;		/* I: worker attributes */
	struct hlist_node	hash_node;	/* R: unbound_pool_hash node */
	int			refcnt;		/* refcnt for unbound pools */

	/*
	 * The current concurrency level.  As it's likely to be accessed
	 * from other CPUs during try_to_wake_up(), put it in a separate
	 * cacheline.
	 */
	atomic_t		nr_running ____cacheline_aligned_in_smp;

	/*
	 * Destruction of pool is sched-RCU protected to allow dereferences
	 * from get_work_pool().
	 */
	struct rcu_head		rcu;
} ____cacheline_aligned_in_smp;

/*
 * The per-pool workqueue.  While queued, the lower WORK_STRUCT_FLAG_BITS
 * of work_struct->data are used for flags and the remaining high bits
 * point to the pwq; thus, pwqs need to be aligned at two's power of the
 * number of flag bits.
 */
struct pool_workqueue {
	struct worker_pool	*pool;		/* I: the associated pool */
	struct workqueue_struct *wq;		/* I: the owning workqueue */
	int			work_color;	/* L: current color */
	int			flush_color;	/* L: flushing color */
	int			refcnt;		/* L: reference count */
	int			nr_in_flight[WORK_NR_COLORS];
						/* L: nr of in_flight works */
	int			nr_active;	/* L: nr of active works */
	int			max_active;	/* L: max active works */
	struct list_head	delayed_works;	/* L: delayed works */
	struct list_head	pwqs_node;	/* FR: node on wq->pwqs */
	struct list_head	mayday_node;	/* W: node on wq->maydays */

	/*
	 * Release of unbound pwq is punted to system_wq.  See put_pwq()
	 * and pwq_unbound_release_workfn() for details.  pool_workqueue
	 * itself is also sched-RCU protected so that the first pwq can be
	 * determined without grabbing workqueue_lock.
	 */
	struct work_struct	unbound_release_work;
	struct rcu_head		rcu;
} __aligned(1 << WORK_STRUCT_FLAG_BITS);

/*
 * Structure used to wait for workqueue flush.
 */
struct wq_flusher {
	struct list_head	list;		/* F: list of flushers */
	int			flush_color;	/* F: flush color waiting for */
	struct completion	done;		/* flush completion */
};

/*
 * The externally visible workqueue abstraction is an array of
 * per-CPU workqueues:
 */
struct workqueue_struct {
	unsigned int		flags;		/* W: WQ_* flags */
	struct pool_workqueue __percpu *cpu_pwqs; /* I: per-cpu pwq's */
	struct list_head	pwqs;		/* FR: all pwqs of this wq */
	struct list_head	list;		/* W: list of all workqueues */

	struct mutex		flush_mutex;	/* protects wq flushing */
	int			work_color;	/* F: current work color */
	int			flush_color;	/* F: current flush color */
	atomic_t		nr_pwqs_to_flush; /* flush in progress */
	struct wq_flusher	*first_flusher;	/* F: first flusher */
	struct list_head	flusher_queue;	/* F: flush waiters */
	struct list_head	flusher_overflow; /* F: flush overflow list */

	struct list_head	maydays;	/* W: pwqs requesting rescue */
	struct worker		*rescuer;	/* I: rescue worker */

	int			nr_drainers;	/* W: drain in progress */
	int			saved_max_active; /* W: saved pwq max_active */
#ifdef CONFIG_LOCKDEP
	struct lockdep_map	lockdep_map;
#endif
	char			name[];		/* I: workqueue name */
};

static struct kmem_cache *pwq_cache;

/* hash of all unbound pools keyed by pool->attrs */
static DEFINE_HASHTABLE(unbound_pool_hash, UNBOUND_POOL_HASH_ORDER);

static struct workqueue_attrs *unbound_std_wq_attrs[NR_STD_WORKER_POOLS];

struct workqueue_struct *system_wq __read_mostly;
EXPORT_SYMBOL_GPL(system_wq);
struct workqueue_struct *system_highpri_wq __read_mostly;
EXPORT_SYMBOL_GPL(system_highpri_wq);
struct workqueue_struct *system_long_wq __read_mostly;
EXPORT_SYMBOL_GPL(system_long_wq);
struct workqueue_struct *system_unbound_wq __read_mostly;
EXPORT_SYMBOL_GPL(system_unbound_wq);
struct workqueue_struct *system_freezable_wq __read_mostly;
EXPORT_SYMBOL_GPL(system_freezable_wq);

#define CREATE_TRACE_POINTS
#include <trace/events/workqueue.h>

#define assert_rcu_or_wq_lock()						\
	rcu_lockdep_assert(rcu_read_lock_sched_held() ||		\
			   lockdep_is_held(&workqueue_lock),		\
			   "sched RCU or workqueue lock should be held")

#define for_each_cpu_worker_pool(pool, cpu)				\
	for ((pool) = &per_cpu(cpu_worker_pools, cpu)[0];		\
	     (pool) < &per_cpu(cpu_worker_pools, cpu)[NR_STD_WORKER_POOLS]; \
	     (pool)++)

#define for_each_busy_worker(worker, i, pool)				\
	hash_for_each(pool->busy_hash, i, worker, hentry)

/**
 * for_each_pool - iterate through all worker_pools in the system
 * @pool: iteration cursor
 * @id: integer used for iteration
 *
 * This must be called either with workqueue_lock held or sched RCU read
 * locked.  If the pool needs to be used beyond the locking in effect, the
 * caller is responsible for guaranteeing that the pool stays online.
 *
 * The if/else clause exists only for the lockdep assertion and can be
 * ignored.
 */
#define for_each_pool(pool, id)						\
	idr_for_each_entry(&worker_pool_idr, pool, id)			\
		if (({ assert_rcu_or_wq_lock(); false; })) { }		\
		else

/**
 * for_each_pwq - iterate through all pool_workqueues of the specified workqueue
 * @pwq: iteration cursor
 * @wq: the target workqueue
 *
 * This must be called either with workqueue_lock held or sched RCU read
 * locked.  If the pwq needs to be used beyond the locking in effect, the
 * caller is responsible for guaranteeing that the pwq stays online.
 *
 * The if/else clause exists only for the lockdep assertion and can be
 * ignored.
 */
#define for_each_pwq(pwq, wq)						\
	list_for_each_entry_rcu((pwq), &(wq)->pwqs, pwqs_node)		\
		if (({ assert_rcu_or_wq_lock(); false; })) { }		\
		else

#ifdef CONFIG_DEBUG_OBJECTS_WORK

static struct debug_obj_descr work_debug_descr;

static void *work_debug_hint(void *addr)
{
	return ((struct work_struct *) addr)->func;
}

/*
 * fixup_init is called when:
 * - an active object is initialized
 */
static int work_fixup_init(void *addr, enum debug_obj_state state)
{
	struct work_struct *work = addr;

	switch (state) {
	case ODEBUG_STATE_ACTIVE:
		cancel_work_sync(work);
		debug_object_init(work, &work_debug_descr);
		return 1;
	default:
		return 0;
	}
}

/*
 * fixup_activate is called when:
 * - an active object is activated
 * - an unknown object is activated (might be a statically initialized object)
 */
static int work_fixup_activate(void *addr, enum debug_obj_state state)
{
	struct work_struct *work = addr;

	switch (state) {

	case ODEBUG_STATE_NOTAVAILABLE:
		/*
		 * This is not really a fixup. The work struct was
		 * statically initialized. We just make sure that it
		 * is tracked in the object tracker.
		 */
		if (test_bit(WORK_STRUCT_STATIC_BIT, work_data_bits(work))) {
			debug_object_init(work, &work_debug_descr);
			debug_object_activate(work, &work_debug_descr);
			return 0;
		}
		WARN_ON_ONCE(1);
		return 0;

	case ODEBUG_STATE_ACTIVE:
		WARN_ON(1);

	default:
		return 0;
	}
}

/*
 * fixup_free is called when:
 * - an active object is freed
 */
static int work_fixup_free(void *addr, enum debug_obj_state state)
{
	struct work_struct *work = addr;

	switch (state) {
	case ODEBUG_STATE_ACTIVE:
		cancel_work_sync(work);
		debug_object_free(work, &work_debug_descr);
		return 1;
	default:
		return 0;
	}
}

static struct debug_obj_descr work_debug_descr = {
	.name		= "work_struct",
	.debug_hint	= work_debug_hint,
	.fixup_init	= work_fixup_init,
	.fixup_activate	= work_fixup_activate,
	.fixup_free	= work_fixup_free,
};

static inline void debug_work_activate(struct work_struct *work)
{
	debug_object_activate(work, &work_debug_descr);
}

static inline void debug_work_deactivate(struct work_struct *work)
{
	debug_object_deactivate(work, &work_debug_descr);
}

void __init_work(struct work_struct *work, int onstack)
{
	if (onstack)
		debug_object_init_on_stack(work, &work_debug_descr);
	else
		debug_object_init(work, &work_debug_descr);
}
EXPORT_SYMBOL_GPL(__init_work);

void destroy_work_on_stack(struct work_struct *work)
{
	debug_object_free(work, &work_debug_descr);
}
EXPORT_SYMBOL_GPL(destroy_work_on_stack);

#else
static inline void debug_work_activate(struct work_struct *work) { }
static inline void debug_work_deactivate(struct work_struct *work) { }
#endif

/* Serializes the accesses to the list of workqueues. */
static DEFINE_SPINLOCK(workqueue_lock);
static LIST_HEAD(workqueues);
static bool workqueue_freezing;		/* W: have wqs started freezing? */

/*
 * The CPU and unbound standard worker pools.  The unbound ones have
 * POOL_DISASSOCIATED set, and their workers have WORKER_UNBOUND set.
 */
static DEFINE_PER_CPU_SHARED_ALIGNED(struct worker_pool [NR_STD_WORKER_POOLS],
				     cpu_worker_pools);

/*
 * idr of all pools.  Modifications are protected by workqueue_lock.  Read
 * accesses are protected by sched-RCU protected.
 */
static DEFINE_IDR(worker_pool_idr);

static int worker_thread(void *__worker);

/* allocate ID and assign it to @pool */
static int worker_pool_assign_id(struct worker_pool *pool)
{
	int ret;

	do {
		if (!idr_pre_get(&worker_pool_idr, GFP_KERNEL))
			return -ENOMEM;

		spin_lock_irq(&workqueue_lock);
		ret = idr_get_new(&worker_pool_idr, pool, &pool->id);
		spin_unlock_irq(&workqueue_lock);
	} while (ret == -EAGAIN);

	return ret;
}

/**
 * first_pwq - return the first pool_workqueue of the specified workqueue
 * @wq: the target workqueue
 *
 * This must be called either with workqueue_lock held or sched RCU read
 * locked.  If the pwq needs to be used beyond the locking in effect, the
 * caller is responsible for guaranteeing that the pwq stays online.
 */
static struct pool_workqueue *first_pwq(struct workqueue_struct *wq)
{
	assert_rcu_or_wq_lock();
	return list_first_or_null_rcu(&wq->pwqs, struct pool_workqueue,
				      pwqs_node);
}

static unsigned int work_color_to_flags(int color)
{
	return color << WORK_STRUCT_COLOR_SHIFT;
}

static int get_work_color(struct work_struct *work)
{
	return (*work_data_bits(work) >> WORK_STRUCT_COLOR_SHIFT) &
		((1 << WORK_STRUCT_COLOR_BITS) - 1);
}

static int work_next_color(int color)
{
	return (color + 1) % WORK_NR_COLORS;
}

/*
 * While queued, %WORK_STRUCT_PWQ is set and non flag bits of a work's data
 * contain the pointer to the queued pwq.  Once execution starts, the flag
 * is cleared and the high bits contain OFFQ flags and pool ID.
 *
 * set_work_pwq(), set_work_pool_and_clear_pending(), mark_work_canceling()
 * and clear_work_data() can be used to set the pwq, pool or clear
 * work->data.  These functions should only be called while the work is
 * owned - ie. while the PENDING bit is set.
 *
 * get_work_pool() and get_work_pwq() can be used to obtain the pool or pwq
 * corresponding to a work.  Pool is available once the work has been
 * queued anywhere after initialization until it is sync canceled.  pwq is
 * available only while the work item is queued.
 *
 * %WORK_OFFQ_CANCELING is used to mark a work item which is being
 * canceled.  While being canceled, a work item may have its PENDING set
 * but stay off timer and worklist for arbitrarily long and nobody should
 * try to steal the PENDING bit.
 */
static inline void set_work_data(struct work_struct *work, unsigned long data,
				 unsigned long flags)
{
	WARN_ON_ONCE(!work_pending(work));
	atomic_long_set(&work->data, data | flags | work_static(work));
}

static void set_work_pwq(struct work_struct *work, struct pool_workqueue *pwq,
			 unsigned long extra_flags)
{
	set_work_data(work, (unsigned long)pwq,
		      WORK_STRUCT_PENDING | WORK_STRUCT_PWQ | extra_flags);
}

static void set_work_pool_and_keep_pending(struct work_struct *work,
					   int pool_id)
{
	set_work_data(work, (unsigned long)pool_id << WORK_OFFQ_POOL_SHIFT,
		      WORK_STRUCT_PENDING);
}

static void set_work_pool_and_clear_pending(struct work_struct *work,
					    int pool_id)
{
	/*
	 * The following wmb is paired with the implied mb in
	 * test_and_set_bit(PENDING) and ensures all updates to @work made
	 * here are visible to and precede any updates by the next PENDING
	 * owner.
	 */
	smp_wmb();
	set_work_data(work, (unsigned long)pool_id << WORK_OFFQ_POOL_SHIFT, 0);
}

static void clear_work_data(struct work_struct *work)
{
	smp_wmb();	/* see set_work_pool_and_clear_pending() */
	set_work_data(work, WORK_STRUCT_NO_POOL, 0);
}

static struct pool_workqueue *get_work_pwq(struct work_struct *work)
{
	unsigned long data = atomic_long_read(&work->data);

	if (data & WORK_STRUCT_PWQ)
		return (void *)(data & WORK_STRUCT_WQ_DATA_MASK);
	else
		return NULL;
}

/**
 * get_work_pool - return the worker_pool a given work was associated with
 * @work: the work item of interest
 *
 * Return the worker_pool @work was last associated with.  %NULL if none.
 *
 * Pools are created and destroyed under workqueue_lock, and allows read
 * access under sched-RCU read lock.  As such, this function should be
 * called under workqueue_lock or with preemption disabled.
 *
 * All fields of the returned pool are accessible as long as the above
 * mentioned locking is in effect.  If the returned pool needs to be used
 * beyond the critical section, the caller is responsible for ensuring the
 * returned pool is and stays online.
 */
static struct worker_pool *get_work_pool(struct work_struct *work)
{
	unsigned long data = atomic_long_read(&work->data);
	int pool_id;

	assert_rcu_or_wq_lock();

	if (data & WORK_STRUCT_PWQ)
		return ((struct pool_workqueue *)
			(data & WORK_STRUCT_WQ_DATA_MASK))->pool;

	pool_id = data >> WORK_OFFQ_POOL_SHIFT;
	if (pool_id == WORK_OFFQ_POOL_NONE)
		return NULL;

	return idr_find(&worker_pool_idr, pool_id);
}

/**
 * get_work_pool_id - return the worker pool ID a given work is associated with
 * @work: the work item of interest
 *
 * Return the worker_pool ID @work was last associated with.
 * %WORK_OFFQ_POOL_NONE if none.
 */
static int get_work_pool_id(struct work_struct *work)
{
	unsigned long data = atomic_long_read(&work->data);

	if (data & WORK_STRUCT_PWQ)
		return ((struct pool_workqueue *)
			(data & WORK_STRUCT_WQ_DATA_MASK))->pool->id;

	return data >> WORK_OFFQ_POOL_SHIFT;
}

static void mark_work_canceling(struct work_struct *work)
{
	unsigned long pool_id = get_work_pool_id(work);

	pool_id <<= WORK_OFFQ_POOL_SHIFT;
	set_work_data(work, pool_id | WORK_OFFQ_CANCELING, WORK_STRUCT_PENDING);
}

static bool work_is_canceling(struct work_struct *work)
{
	unsigned long data = atomic_long_read(&work->data);

	return !(data & WORK_STRUCT_PWQ) && (data & WORK_OFFQ_CANCELING);
}

/*
 * Policy functions.  These define the policies on how the global worker
 * pools are managed.  Unless noted otherwise, these functions assume that
 * they're being called with pool->lock held.
 */

static bool __need_more_worker(struct worker_pool *pool)
{
	return !atomic_read(&pool->nr_running);
}

/*
 * Need to wake up a worker?  Called from anything but currently
 * running workers.
 *
 * Note that, because unbound workers never contribute to nr_running, this
 * function will always return %true for unbound pools as long as the
 * worklist isn't empty.
 */
static bool need_more_worker(struct worker_pool *pool)
{
	return !list_empty(&pool->worklist) && __need_more_worker(pool);
}

/* Can I start working?  Called from busy but !running workers. */
static bool may_start_working(struct worker_pool *pool)
{
	return pool->nr_idle;
}

/* Do I need to keep working?  Called from currently running workers. */
static bool keep_working(struct worker_pool *pool)
{
	return !list_empty(&pool->worklist) &&
		atomic_read(&pool->nr_running) <= 1;
}

/* Do we need a new worker?  Called from manager. */
static bool need_to_create_worker(struct worker_pool *pool)
{
	return need_more_worker(pool) && !may_start_working(pool);
}

/* Do I need to be the manager? */
static bool need_to_manage_workers(struct worker_pool *pool)
{
	return need_to_create_worker(pool) ||
		(pool->flags & POOL_MANAGE_WORKERS);
}

/* Do we have too many workers and should some go away? */
static bool too_many_workers(struct worker_pool *pool)
{
	bool managing = mutex_is_locked(&pool->manager_arb);
	int nr_idle = pool->nr_idle + managing; /* manager is considered idle */
	int nr_busy = pool->nr_workers - nr_idle;

	/*
	 * nr_idle and idle_list may disagree if idle rebinding is in
	 * progress.  Never return %true if idle_list is empty.
	 */
	if (list_empty(&pool->idle_list))
		return false;

	return nr_idle > 2 && (nr_idle - 2) * MAX_IDLE_WORKERS_RATIO >= nr_busy;
}

/*
 * Wake up functions.
 */

/* Return the first worker.  Safe with preemption disabled */
static struct worker *first_worker(struct worker_pool *pool)
{
	if (unlikely(list_empty(&pool->idle_list)))
		return NULL;

	return list_first_entry(&pool->idle_list, struct worker, entry);
}

/**
 * wake_up_worker - wake up an idle worker
 * @pool: worker pool to wake worker from
 *
 * Wake up the first idle worker of @pool.
 *
 * CONTEXT:
 * spin_lock_irq(pool->lock).
 */
static void wake_up_worker(struct worker_pool *pool)
{
	struct worker *worker = first_worker(pool);

	if (likely(worker))
		wake_up_process(worker->task);
}

/**
 * wq_worker_waking_up - a worker is waking up
 * @task: task waking up
 * @cpu: CPU @task is waking up to
 *
 * This function is called during try_to_wake_up() when a worker is
 * being awoken.
 *
 * CONTEXT:
 * spin_lock_irq(rq->lock)
 */
void wq_worker_waking_up(struct task_struct *task, int cpu)
{
	struct worker *worker = kthread_data(task);

	if (!(worker->flags & WORKER_NOT_RUNNING)) {
		WARN_ON_ONCE(worker->pool->cpu != cpu);
		atomic_inc(&worker->pool->nr_running);
	}
}

/**
 * wq_worker_sleeping - a worker is going to sleep
 * @task: task going to sleep
 * @cpu: CPU in question, must be the current CPU number
 *
 * This function is called during schedule() when a busy worker is
 * going to sleep.  Worker on the same cpu can be woken up by
 * returning pointer to its task.
 *
 * CONTEXT:
 * spin_lock_irq(rq->lock)
 *
 * RETURNS:
 * Worker task on @cpu to wake up, %NULL if none.
 */
struct task_struct *wq_worker_sleeping(struct task_struct *task, int cpu)
{
	struct worker *worker = kthread_data(task), *to_wakeup = NULL;
	struct worker_pool *pool;

	/*
	 * Rescuers, which may not have all the fields set up like normal
	 * workers, also reach here, let's not access anything before
	 * checking NOT_RUNNING.
	 */
	if (worker->flags & WORKER_NOT_RUNNING)
		return NULL;

	pool = worker->pool;

	/* this can only happen on the local cpu */
	if (WARN_ON_ONCE(cpu != raw_smp_processor_id()))
		return NULL;

	/*
	 * The counterpart of the following dec_and_test, implied mb,
	 * worklist not empty test sequence is in insert_work().
	 * Please read comment there.
	 *
	 * NOT_RUNNING is clear.  This means that we're bound to and
	 * running on the local cpu w/ rq lock held and preemption
	 * disabled, which in turn means that none else could be
	 * manipulating idle_list, so dereferencing idle_list without pool
	 * lock is safe.
	 */
	if (atomic_dec_and_test(&pool->nr_running) &&
	    !list_empty(&pool->worklist))
		to_wakeup = first_worker(pool);
	return to_wakeup ? to_wakeup->task : NULL;
}

/**
 * worker_set_flags - set worker flags and adjust nr_running accordingly
 * @worker: self
 * @flags: flags to set
 * @wakeup: wakeup an idle worker if necessary
 *
 * Set @flags in @worker->flags and adjust nr_running accordingly.  If
 * nr_running becomes zero and @wakeup is %true, an idle worker is
 * woken up.
 *
 * CONTEXT:
 * spin_lock_irq(pool->lock)
 */
static inline void worker_set_flags(struct worker *worker, unsigned int flags,
				    bool wakeup)
{
	struct worker_pool *pool = worker->pool;

	WARN_ON_ONCE(worker->task != current);

	/*
	 * If transitioning into NOT_RUNNING, adjust nr_running and
	 * wake up an idle worker as necessary if requested by
	 * @wakeup.
	 */
	if ((flags & WORKER_NOT_RUNNING) &&
	    !(worker->flags & WORKER_NOT_RUNNING)) {
		if (wakeup) {
			if (atomic_dec_and_test(&pool->nr_running) &&
			    !list_empty(&pool->worklist))
				wake_up_worker(pool);
		} else
			atomic_dec(&pool->nr_running);
	}

	worker->flags |= flags;
}

/**
 * worker_clr_flags - clear worker flags and adjust nr_running accordingly
 * @worker: self
 * @flags: flags to clear
 *
 * Clear @flags in @worker->flags and adjust nr_running accordingly.
 *
 * CONTEXT:
 * spin_lock_irq(pool->lock)
 */
static inline void worker_clr_flags(struct worker *worker, unsigned int flags)
{
	struct worker_pool *pool = worker->pool;
	unsigned int oflags = worker->flags;

	WARN_ON_ONCE(worker->task != current);

	worker->flags &= ~flags;

	/*
	 * If transitioning out of NOT_RUNNING, increment nr_running.  Note
	 * that the nested NOT_RUNNING is not a noop.  NOT_RUNNING is mask
	 * of multiple flags, not a single flag.
	 */
	if ((flags & WORKER_NOT_RUNNING) && (oflags & WORKER_NOT_RUNNING))
		if (!(worker->flags & WORKER_NOT_RUNNING))
			atomic_inc(&pool->nr_running);
}

/**
 * find_worker_executing_work - find worker which is executing a work
 * @pool: pool of interest
 * @work: work to find worker for
 *
 * Find a worker which is executing @work on @pool by searching
 * @pool->busy_hash which is keyed by the address of @work.  For a worker
 * to match, its current execution should match the address of @work and
 * its work function.  This is to avoid unwanted dependency between
 * unrelated work executions through a work item being recycled while still
 * being executed.
 *
 * This is a bit tricky.  A work item may be freed once its execution
 * starts and nothing prevents the freed area from being recycled for
 * another work item.  If the same work item address ends up being reused
 * before the original execution finishes, workqueue will identify the
 * recycled work item as currently executing and make it wait until the
 * current execution finishes, introducing an unwanted dependency.
 *
 * This function checks the work item address, work function and workqueue
 * to avoid false positives.  Note that this isn't complete as one may
 * construct a work function which can introduce dependency onto itself
 * through a recycled work item.  Well, if somebody wants to shoot oneself
 * in the foot that badly, there's only so much we can do, and if such
 * deadlock actually occurs, it should be easy to locate the culprit work
 * function.
 *
 * CONTEXT:
 * spin_lock_irq(pool->lock).
 *
 * RETURNS:
 * Pointer to worker which is executing @work if found, NULL
 * otherwise.
 */
static struct worker *find_worker_executing_work(struct worker_pool *pool,
						 struct work_struct *work)
{
	struct worker *worker;

	hash_for_each_possible(pool->busy_hash, worker, hentry,
			       (unsigned long)work)
		if (worker->current_work == work &&
		    worker->current_func == work->func)
			return worker;

	return NULL;
}

/**
 * move_linked_works - move linked works to a list
 * @work: start of series of works to be scheduled
 * @head: target list to append @work to
 * @nextp: out paramter for nested worklist walking
 *
 * Schedule linked works starting from @work to @head.  Work series to
 * be scheduled starts at @work and includes any consecutive work with
 * WORK_STRUCT_LINKED set in its predecessor.
 *
 * If @nextp is not NULL, it's updated to point to the next work of
 * the last scheduled work.  This allows move_linked_works() to be
 * nested inside outer list_for_each_entry_safe().
 *
 * CONTEXT:
 * spin_lock_irq(pool->lock).
 */
static void move_linked_works(struct work_struct *work, struct list_head *head,
			      struct work_struct **nextp)
{
	struct work_struct *n;

	/*
	 * Linked worklist will always end before the end of the list,
	 * use NULL for list head.
	 */
	list_for_each_entry_safe_from(work, n, NULL, entry) {
		list_move_tail(&work->entry, head);
		if (!(*work_data_bits(work) & WORK_STRUCT_LINKED))
			break;
	}

	/*
	 * If we're already inside safe list traversal and have moved
	 * multiple works to the scheduled queue, the next position
	 * needs to be updated.
	 */
	if (nextp)
		*nextp = n;
}

/**
 * get_pwq - get an extra reference on the specified pool_workqueue
 * @pwq: pool_workqueue to get
 *
 * Obtain an extra reference on @pwq.  The caller should guarantee that
 * @pwq has positive refcnt and be holding the matching pool->lock.
 */
static void get_pwq(struct pool_workqueue *pwq)
{
	lockdep_assert_held(&pwq->pool->lock);
	WARN_ON_ONCE(pwq->refcnt <= 0);
	pwq->refcnt++;
}

/**
 * put_pwq - put a pool_workqueue reference
 * @pwq: pool_workqueue to put
 *
 * Drop a reference of @pwq.  If its refcnt reaches zero, schedule its
 * destruction.  The caller should be holding the matching pool->lock.
 */
static void put_pwq(struct pool_workqueue *pwq)
{
	lockdep_assert_held(&pwq->pool->lock);
	if (likely(--pwq->refcnt))
		return;
	if (WARN_ON_ONCE(!(pwq->wq->flags & WQ_UNBOUND)))
		return;
	/*
	 * @pwq can't be released under pool->lock, bounce to
	 * pwq_unbound_release_workfn().  This never recurses on the same
	 * pool->lock as this path is taken only for unbound workqueues and
	 * the release work item is scheduled on a per-cpu workqueue.  To
	 * avoid lockdep warning, unbound pool->locks are given lockdep
	 * subclass of 1 in get_unbound_pool().
	 */
	schedule_work(&pwq->unbound_release_work);
}

static void pwq_activate_delayed_work(struct work_struct *work)
{
	struct pool_workqueue *pwq = get_work_pwq(work);

	trace_workqueue_activate_work(work);
	move_linked_works(work, &pwq->pool->worklist, NULL);
	__clear_bit(WORK_STRUCT_DELAYED_BIT, work_data_bits(work));
	pwq->nr_active++;
}

static void pwq_activate_first_delayed(struct pool_workqueue *pwq)
{
	struct work_struct *work = list_first_entry(&pwq->delayed_works,
						    struct work_struct, entry);

	pwq_activate_delayed_work(work);
}

/**
 * pwq_dec_nr_in_flight - decrement pwq's nr_in_flight
 * @pwq: pwq of interest
 * @color: color of work which left the queue
 *
 * A work either has completed or is removed from pending queue,
 * decrement nr_in_flight of its pwq and handle workqueue flushing.
 *
 * CONTEXT:
 * spin_lock_irq(pool->lock).
 */
static void pwq_dec_nr_in_flight(struct pool_workqueue *pwq, int color)
{
	/* uncolored work items don't participate in flushing or nr_active */
	if (color == WORK_NO_COLOR)
		goto out_put;

	pwq->nr_in_flight[color]--;

	pwq->nr_active--;
	if (!list_empty(&pwq->delayed_works)) {
		/* one down, submit a delayed one */
		if (pwq->nr_active < pwq->max_active)
			pwq_activate_first_delayed(pwq);
	}

	/* is flush in progress and are we at the flushing tip? */
	if (likely(pwq->flush_color != color))
		goto out_put;

	/* are there still in-flight works? */
	if (pwq->nr_in_flight[color])
		goto out_put;

	/* this pwq is done, clear flush_color */
	pwq->flush_color = -1;

	/*
	 * If this was the last pwq, wake up the first flusher.  It
	 * will handle the rest.
	 */
	if (atomic_dec_and_test(&pwq->wq->nr_pwqs_to_flush))
		complete(&pwq->wq->first_flusher->done);
out_put:
	put_pwq(pwq);
}

/**
 * try_to_grab_pending - steal work item from worklist and disable irq
 * @work: work item to steal
 * @is_dwork: @work is a delayed_work
 * @flags: place to store irq state
 *
 * Try to grab PENDING bit of @work.  This function can handle @work in any
 * stable state - idle, on timer or on worklist.  Return values are
 *
 *  1		if @work was pending and we successfully stole PENDING
 *  0		if @work was idle and we claimed PENDING
 *  -EAGAIN	if PENDING couldn't be grabbed at the moment, safe to busy-retry
 *  -ENOENT	if someone else is canceling @work, this state may persist
 *		for arbitrarily long
 *
 * On >= 0 return, the caller owns @work's PENDING bit.  To avoid getting
 * interrupted while holding PENDING and @work off queue, irq must be
 * disabled on entry.  This, combined with delayed_work->timer being
 * irqsafe, ensures that we return -EAGAIN for finite short period of time.
 *
 * On successful return, >= 0, irq is disabled and the caller is
 * responsible for releasing it using local_irq_restore(*@flags).
 *
 * This function is safe to call from any context including IRQ handler.
 */
static int try_to_grab_pending(struct work_struct *work, bool is_dwork,
			       unsigned long *flags)
{
	struct worker_pool *pool;
	struct pool_workqueue *pwq;

	local_irq_save(*flags);

	/* try to steal the timer if it exists */
	if (is_dwork) {
		struct delayed_work *dwork = to_delayed_work(work);

		/*
		 * dwork->timer is irqsafe.  If del_timer() fails, it's
		 * guaranteed that the timer is not queued anywhere and not
		 * running on the local CPU.
		 */
		if (likely(del_timer(&dwork->timer)))
			return 1;
	}

	/* try to claim PENDING the normal way */
	if (!test_and_set_bit(WORK_STRUCT_PENDING_BIT, work_data_bits(work)))
		return 0;

	/*
	 * The queueing is in progress, or it is already queued. Try to
	 * steal it from ->worklist without clearing WORK_STRUCT_PENDING.
	 */
	pool = get_work_pool(work);
	if (!pool)
		goto fail;

	spin_lock(&pool->lock);
	/*
	 * work->data is guaranteed to point to pwq only while the work
	 * item is queued on pwq->wq, and both updating work->data to point
	 * to pwq on queueing and to pool on dequeueing are done under
	 * pwq->pool->lock.  This in turn guarantees that, if work->data
	 * points to pwq which is associated with a locked pool, the work
	 * item is currently queued on that pool.
	 */
	pwq = get_work_pwq(work);
	if (pwq && pwq->pool == pool) {
		debug_work_deactivate(work);

		/*
		 * A delayed work item cannot be grabbed directly because
		 * it might have linked NO_COLOR work items which, if left
		 * on the delayed_list, will confuse pwq->nr_active
		 * management later on and cause stall.  Make sure the work
		 * item is activated before grabbing.
		 */
		if (*work_data_bits(work) & WORK_STRUCT_DELAYED)
			pwq_activate_delayed_work(work);

		list_del_init(&work->entry);
		pwq_dec_nr_in_flight(get_work_pwq(work), get_work_color(work));

		/* work->data points to pwq iff queued, point to pool */
		set_work_pool_and_keep_pending(work, pool->id);

		spin_unlock(&pool->lock);
		return 1;
	}
	spin_unlock(&pool->lock);
fail:
	local_irq_restore(*flags);
	if (work_is_canceling(work))
		return -ENOENT;
	cpu_relax();
	return -EAGAIN;
}

/**
 * insert_work - insert a work into a pool
 * @pwq: pwq @work belongs to
 * @work: work to insert
 * @head: insertion point
 * @extra_flags: extra WORK_STRUCT_* flags to set
 *
 * Insert @work which belongs to @pwq after @head.  @extra_flags is or'd to
 * work_struct flags.
 *
 * CONTEXT:
 * spin_lock_irq(pool->lock).
 */
static void insert_work(struct pool_workqueue *pwq, struct work_struct *work,
			struct list_head *head, unsigned int extra_flags)
{
	struct worker_pool *pool = pwq->pool;

	/* we own @work, set data and link */
	set_work_pwq(work, pwq, extra_flags);
	list_add_tail(&work->entry, head);
	get_pwq(pwq);

	/*
	 * Ensure either worker_sched_deactivated() sees the above
	 * list_add_tail() or we see zero nr_running to avoid workers
	 * lying around lazily while there are works to be processed.
	 */
	smp_mb();

	if (__need_more_worker(pool))
		wake_up_worker(pool);
}

/*
 * Test whether @work is being queued from another work executing on the
 * same workqueue.
 */
static bool is_chained_work(struct workqueue_struct *wq)
{
	struct worker *worker;

	worker = current_wq_worker();
	/*
	 * Return %true iff I'm a worker execuing a work item on @wq.  If
	 * I'm @worker, it's safe to dereference it without locking.
	 */
	return worker && worker->current_pwq->wq == wq;
}

static void __queue_work(int cpu, struct workqueue_struct *wq,
			 struct work_struct *work)
{
	struct pool_workqueue *pwq;
	struct worker_pool *last_pool;
	struct list_head *worklist;
	unsigned int work_flags;
	unsigned int req_cpu = cpu;

	/*
	 * While a work item is PENDING && off queue, a task trying to
	 * steal the PENDING will busy-loop waiting for it to either get
	 * queued or lose PENDING.  Grabbing PENDING and queueing should
	 * happen with IRQ disabled.
	 */
	WARN_ON_ONCE(!irqs_disabled());

	debug_work_activate(work);

	/* if dying, only works from the same workqueue are allowed */
	if (unlikely(wq->flags & WQ_DRAINING) &&
	    WARN_ON_ONCE(!is_chained_work(wq)))
		return;
retry:
	/* pwq which will be used unless @work is executing elsewhere */
	if (!(wq->flags & WQ_UNBOUND)) {
		if (cpu == WORK_CPU_UNBOUND)
			cpu = raw_smp_processor_id();
		pwq = per_cpu_ptr(wq->cpu_pwqs, cpu);
	} else {
		pwq = first_pwq(wq);
	}

	/*
	 * If @work was previously on a different pool, it might still be
	 * running there, in which case the work needs to be queued on that
	 * pool to guarantee non-reentrancy.
	 */
	last_pool = get_work_pool(work);
	if (last_pool && last_pool != pwq->pool) {
		struct worker *worker;

		spin_lock(&last_pool->lock);

		worker = find_worker_executing_work(last_pool, work);

		if (worker && worker->current_pwq->wq == wq) {
			pwq = worker->current_pwq;
		} else {
			/* meh... not running there, queue here */
			spin_unlock(&last_pool->lock);
			spin_lock(&pwq->pool->lock);
		}
	} else {
		spin_lock(&pwq->pool->lock);
	}

	/*
	 * pwq is determined and locked.  For unbound pools, we could have
	 * raced with pwq release and it could already be dead.  If its
	 * refcnt is zero, repeat pwq selection.  Note that pwqs never die
	 * without another pwq replacing it as the first pwq or while a
	 * work item is executing on it, so the retying is guaranteed to
	 * make forward-progress.
	 */
	if (unlikely(!pwq->refcnt)) {
		if (wq->flags & WQ_UNBOUND) {
			spin_unlock(&pwq->pool->lock);
			cpu_relax();
			goto retry;
		}
		/* oops */
		WARN_ONCE(true, "workqueue: per-cpu pwq for %s on cpu%d has 0 refcnt",
			  wq->name, cpu);
	}

	/* pwq determined, queue */
	trace_workqueue_queue_work(req_cpu, pwq, work);

	if (WARN_ON(!list_empty(&work->entry))) {
		spin_unlock(&pwq->pool->lock);
		return;
	}

	pwq->nr_in_flight[pwq->work_color]++;
	work_flags = work_color_to_flags(pwq->work_color);

	if (likely(pwq->nr_active < pwq->max_active)) {
		trace_workqueue_activate_work(work);
		pwq->nr_active++;
		worklist = &pwq->pool->worklist;
	} else {
		work_flags |= WORK_STRUCT_DELAYED;
		worklist = &pwq->delayed_works;
	}

	insert_work(pwq, work, worklist, work_flags);

	spin_unlock(&pwq->pool->lock);
}

/**
 * queue_work_on - queue work on specific cpu
 * @cpu: CPU number to execute work on
 * @wq: workqueue to use
 * @work: work to queue
 *
 * Returns %false if @work was already on a queue, %true otherwise.
 *
 * We queue the work to a specific CPU, the caller must ensure it
 * can't go away.
 */
bool queue_work_on(int cpu, struct workqueue_struct *wq,
		   struct work_struct *work)
{
	bool ret = false;
	unsigned long flags;

	local_irq_save(flags);

	if (!test_and_set_bit(WORK_STRUCT_PENDING_BIT, work_data_bits(work))) {
		__queue_work(cpu, wq, work);
		ret = true;
	}

	local_irq_restore(flags);
	return ret;
}
EXPORT_SYMBOL_GPL(queue_work_on);

/**
 * queue_work - queue work on a workqueue
 * @wq: workqueue to use
 * @work: work to queue
 *
 * Returns %false if @work was already on a queue, %true otherwise.
 *
 * We queue the work to the CPU on which it was submitted, but if the CPU dies
 * it can be processed by another CPU.
 */
bool queue_work(struct workqueue_struct *wq, struct work_struct *work)
{
	return queue_work_on(WORK_CPU_UNBOUND, wq, work);
}
EXPORT_SYMBOL_GPL(queue_work);

void delayed_work_timer_fn(unsigned long __data)
{
	struct delayed_work *dwork = (struct delayed_work *)__data;

	/* should have been called from irqsafe timer with irq already off */
	__queue_work(dwork->cpu, dwork->wq, &dwork->work);
}
EXPORT_SYMBOL(delayed_work_timer_fn);

static void __queue_delayed_work(int cpu, struct workqueue_struct *wq,
				struct delayed_work *dwork, unsigned long delay)
{
	struct timer_list *timer = &dwork->timer;
	struct work_struct *work = &dwork->work;

	WARN_ON_ONCE(timer->function != delayed_work_timer_fn ||
		     timer->data != (unsigned long)dwork);
	WARN_ON_ONCE(timer_pending(timer));
	WARN_ON_ONCE(!list_empty(&work->entry));

	/*
	 * If @delay is 0, queue @dwork->work immediately.  This is for
	 * both optimization and correctness.  The earliest @timer can
	 * expire is on the closest next tick and delayed_work users depend
	 * on that there's no such delay when @delay is 0.
	 */
	if (!delay) {
		__queue_work(cpu, wq, &dwork->work);
		return;
	}

	timer_stats_timer_set_start_info(&dwork->timer);

	dwork->wq = wq;
	dwork->cpu = cpu;
	timer->expires = jiffies + delay;

	if (unlikely(cpu != WORK_CPU_UNBOUND))
		add_timer_on(timer, cpu);
	else
		add_timer(timer);
}

/**
 * queue_delayed_work_on - queue work on specific CPU after delay
 * @cpu: CPU number to execute work on
 * @wq: workqueue to use
 * @dwork: work to queue
 * @delay: number of jiffies to wait before queueing
 *
 * Returns %false if @work was already on a queue, %true otherwise.  If
 * @delay is zero and @dwork is idle, it will be scheduled for immediate
 * execution.
 */
bool queue_delayed_work_on(int cpu, struct workqueue_struct *wq,
			   struct delayed_work *dwork, unsigned long delay)
{
	struct work_struct *work = &dwork->work;
	bool ret = false;
	unsigned long flags;

	/* read the comment in __queue_work() */
	local_irq_save(flags);

	if (!test_and_set_bit(WORK_STRUCT_PENDING_BIT, work_data_bits(work))) {
		__queue_delayed_work(cpu, wq, dwork, delay);
		ret = true;
	}

	local_irq_restore(flags);
	return ret;
}
EXPORT_SYMBOL_GPL(queue_delayed_work_on);

/**
 * queue_delayed_work - queue work on a workqueue after delay
 * @wq: workqueue to use
 * @dwork: delayable work to queue
 * @delay: number of jiffies to wait before queueing
 *
 * Equivalent to queue_delayed_work_on() but tries to use the local CPU.
 */
bool queue_delayed_work(struct workqueue_struct *wq,
			struct delayed_work *dwork, unsigned long delay)
{
	return queue_delayed_work_on(WORK_CPU_UNBOUND, wq, dwork, delay);
}
EXPORT_SYMBOL_GPL(queue_delayed_work);

/**
 * mod_delayed_work_on - modify delay of or queue a delayed work on specific CPU
 * @cpu: CPU number to execute work on
 * @wq: workqueue to use
 * @dwork: work to queue
 * @delay: number of jiffies to wait before queueing
 *
 * If @dwork is idle, equivalent to queue_delayed_work_on(); otherwise,
 * modify @dwork's timer so that it expires after @delay.  If @delay is
 * zero, @work is guaranteed to be scheduled immediately regardless of its
 * current state.
 *
 * Returns %false if @dwork was idle and queued, %true if @dwork was
 * pending and its timer was modified.
 *
 * This function is safe to call from any context including IRQ handler.
 * See try_to_grab_pending() for details.
 */
bool mod_delayed_work_on(int cpu, struct workqueue_struct *wq,
			 struct delayed_work *dwork, unsigned long delay)
{
	unsigned long flags;
	int ret;

	do {
		ret = try_to_grab_pending(&dwork->work, true, &flags);
	} while (unlikely(ret == -EAGAIN));

	if (likely(ret >= 0)) {
		__queue_delayed_work(cpu, wq, dwork, delay);
		local_irq_restore(flags);
	}

	/* -ENOENT from try_to_grab_pending() becomes %true */
	return ret;
}
EXPORT_SYMBOL_GPL(mod_delayed_work_on);

/**
 * mod_delayed_work - modify delay of or queue a delayed work
 * @wq: workqueue to use
 * @dwork: work to queue
 * @delay: number of jiffies to wait before queueing
 *
 * mod_delayed_work_on() on local CPU.
 */
bool mod_delayed_work(struct workqueue_struct *wq, struct delayed_work *dwork,
		      unsigned long delay)
{
	return mod_delayed_work_on(WORK_CPU_UNBOUND, wq, dwork, delay);
}
EXPORT_SYMBOL_GPL(mod_delayed_work);

/**
 * worker_enter_idle - enter idle state
 * @worker: worker which is entering idle state
 *
 * @worker is entering idle state.  Update stats and idle timer if
 * necessary.
 *
 * LOCKING:
 * spin_lock_irq(pool->lock).
 */
static void worker_enter_idle(struct worker *worker)
{
	struct worker_pool *pool = worker->pool;

	if (WARN_ON_ONCE(worker->flags & WORKER_IDLE) ||
	    WARN_ON_ONCE(!list_empty(&worker->entry) &&
			 (worker->hentry.next || worker->hentry.pprev)))
		return;

	/* can't use worker_set_flags(), also called from start_worker() */
	worker->flags |= WORKER_IDLE;
	pool->nr_idle++;
	worker->last_active = jiffies;

	/* idle_list is LIFO */
	list_add(&worker->entry, &pool->idle_list);

	if (too_many_workers(pool) && !timer_pending(&pool->idle_timer))
		mod_timer(&pool->idle_timer, jiffies + IDLE_WORKER_TIMEOUT);

	/*
	 * Sanity check nr_running.  Because wq_unbind_fn() releases
	 * pool->lock between setting %WORKER_UNBOUND and zapping
	 * nr_running, the warning may trigger spuriously.  Check iff
	 * unbind is not in progress.
	 */
	WARN_ON_ONCE(!(pool->flags & POOL_DISASSOCIATED) &&
		     pool->nr_workers == pool->nr_idle &&
		     atomic_read(&pool->nr_running));
}

/**
 * worker_leave_idle - leave idle state
 * @worker: worker which is leaving idle state
 *
 * @worker is leaving idle state.  Update stats.
 *
 * LOCKING:
 * spin_lock_irq(pool->lock).
 */
static void worker_leave_idle(struct worker *worker)
{
	struct worker_pool *pool = worker->pool;

	if (WARN_ON_ONCE(!(worker->flags & WORKER_IDLE)))
		return;
	worker_clr_flags(worker, WORKER_IDLE);
	pool->nr_idle--;
	list_del_init(&worker->entry);
}

/**
 * worker_maybe_bind_and_lock - try to bind %current to worker_pool and lock it
 * @pool: target worker_pool
 *
 * Bind %current to the cpu of @pool if it is associated and lock @pool.
 *
 * Works which are scheduled while the cpu is online must at least be
 * scheduled to a worker which is bound to the cpu so that if they are
 * flushed from cpu callbacks while cpu is going down, they are
 * guaranteed to execute on the cpu.
 *
 * This function is to be used by unbound workers and rescuers to bind
 * themselves to the target cpu and may race with cpu going down or
 * coming online.  kthread_bind() can't be used because it may put the
 * worker to already dead cpu and set_cpus_allowed_ptr() can't be used
 * verbatim as it's best effort and blocking and pool may be
 * [dis]associated in the meantime.
 *
 * This function tries set_cpus_allowed() and locks pool and verifies the
 * binding against %POOL_DISASSOCIATED which is set during
 * %CPU_DOWN_PREPARE and cleared during %CPU_ONLINE, so if the worker
 * enters idle state or fetches works without dropping lock, it can
 * guarantee the scheduling requirement described in the first paragraph.
 *
 * CONTEXT:
 * Might sleep.  Called without any lock but returns with pool->lock
 * held.
 *
 * RETURNS:
 * %true if the associated pool is online (@worker is successfully
 * bound), %false if offline.
 */
static bool worker_maybe_bind_and_lock(struct worker_pool *pool)
__acquires(&pool->lock)
{
	while (true) {
		/*
		 * The following call may fail, succeed or succeed
		 * without actually migrating the task to the cpu if
		 * it races with cpu hotunplug operation.  Verify
		 * against POOL_DISASSOCIATED.
		 */
		if (!(pool->flags & POOL_DISASSOCIATED))
			set_cpus_allowed_ptr(current, pool->attrs->cpumask);

		spin_lock_irq(&pool->lock);
		if (pool->flags & POOL_DISASSOCIATED)
			return false;
		if (task_cpu(current) == pool->cpu &&
		    cpumask_equal(&current->cpus_allowed, pool->attrs->cpumask))
			return true;
		spin_unlock_irq(&pool->lock);

		/*
		 * We've raced with CPU hot[un]plug.  Give it a breather
		 * and retry migration.  cond_resched() is required here;
		 * otherwise, we might deadlock against cpu_stop trying to
		 * bring down the CPU on non-preemptive kernel.
		 */
		cpu_relax();
		cond_resched();
	}
}

/*
 * Rebind an idle @worker to its CPU.  worker_thread() will test
 * list_empty(@worker->entry) before leaving idle and call this function.
 */
static void idle_worker_rebind(struct worker *worker)
{
	/* CPU may go down again inbetween, clear UNBOUND only on success */
	if (worker_maybe_bind_and_lock(worker->pool))
		worker_clr_flags(worker, WORKER_UNBOUND);

	/* rebind complete, become available again */
	list_add(&worker->entry, &worker->pool->idle_list);
	spin_unlock_irq(&worker->pool->lock);
}

/*
 * Function for @worker->rebind.work used to rebind unbound busy workers to
 * the associated cpu which is coming back online.  This is scheduled by
 * cpu up but can race with other cpu hotplug operations and may be
 * executed twice without intervening cpu down.
 */
static void busy_worker_rebind_fn(struct work_struct *work)
{
	struct worker *worker = container_of(work, struct worker, rebind_work);

	if (worker_maybe_bind_and_lock(worker->pool))
		worker_clr_flags(worker, WORKER_UNBOUND);

	spin_unlock_irq(&worker->pool->lock);
}

/**
 * rebind_workers - rebind all workers of a pool to the associated CPU
 * @pool: pool of interest
 *
 * @pool->cpu is coming online.  Rebind all workers to the CPU.  Rebinding
 * is different for idle and busy ones.
 *
 * Idle ones will be removed from the idle_list and woken up.  They will
 * add themselves back after completing rebind.  This ensures that the
 * idle_list doesn't contain any unbound workers when re-bound busy workers
 * try to perform local wake-ups for concurrency management.
 *
 * Busy workers can rebind after they finish their current work items.
 * Queueing the rebind work item at the head of the scheduled list is
 * enough.  Note that nr_running will be properly bumped as busy workers
 * rebind.
 *
 * On return, all non-manager workers are scheduled for rebind - see
 * manage_workers() for the manager special case.  Any idle worker
 * including the manager will not appear on @idle_list until rebind is
 * complete, making local wake-ups safe.
 */
static void rebind_workers(struct worker_pool *pool)
{
	struct worker *worker, *n;
	int i;

	lockdep_assert_held(&pool->assoc_mutex);
	lockdep_assert_held(&pool->lock);

	/* dequeue and kick idle ones */
	list_for_each_entry_safe(worker, n, &pool->idle_list, entry) {
		/*
		 * idle workers should be off @pool->idle_list until rebind
		 * is complete to avoid receiving premature local wake-ups.
		 */
		list_del_init(&worker->entry);

		/*
		 * worker_thread() will see the above dequeuing and call
		 * idle_worker_rebind().
		 */
		wake_up_process(worker->task);
	}

	/* rebind busy workers */
	for_each_busy_worker(worker, i, pool) {
		struct work_struct *rebind_work = &worker->rebind_work;
		struct workqueue_struct *wq;

		if (test_and_set_bit(WORK_STRUCT_PENDING_BIT,
				     work_data_bits(rebind_work)))
			continue;

		debug_work_activate(rebind_work);

		/*
		 * wq doesn't really matter but let's keep @worker->pool
		 * and @pwq->pool consistent for sanity.
		 */
		if (worker->pool->attrs->nice < 0)
			wq = system_highpri_wq;
		else
			wq = system_wq;

		insert_work(per_cpu_ptr(wq->cpu_pwqs, pool->cpu), rebind_work,
			    worker->scheduled.next,
			    work_color_to_flags(WORK_NO_COLOR));
	}
}

static struct worker *alloc_worker(void)
{
	struct worker *worker;

	worker = kzalloc(sizeof(*worker), GFP_KERNEL);
	if (worker) {
		INIT_LIST_HEAD(&worker->entry);
		INIT_LIST_HEAD(&worker->scheduled);
		INIT_WORK(&worker->rebind_work, busy_worker_rebind_fn);
		/* on creation a worker is in !idle && prep state */
		worker->flags = WORKER_PREP;
	}
	return worker;
}

/**
 * create_worker - create a new workqueue worker
 * @pool: pool the new worker will belong to
 *
 * Create a new worker which is bound to @pool.  The returned worker
 * can be started by calling start_worker() or destroyed using
 * destroy_worker().
 *
 * CONTEXT:
 * Might sleep.  Does GFP_KERNEL allocations.
 *
 * RETURNS:
 * Pointer to the newly created worker.
 */
static struct worker *create_worker(struct worker_pool *pool)
{
	const char *pri = pool->attrs->nice < 0  ? "H" : "";
	struct worker *worker = NULL;
	int id = -1;

	spin_lock_irq(&pool->lock);
	while (ida_get_new(&pool->worker_ida, &id)) {
		spin_unlock_irq(&pool->lock);
		if (!ida_pre_get(&pool->worker_ida, GFP_KERNEL))
			goto fail;
		spin_lock_irq(&pool->lock);
	}
	spin_unlock_irq(&pool->lock);

	worker = alloc_worker();
	if (!worker)
		goto fail;

	worker->pool = pool;
	worker->id = id;

	if (pool->cpu >= 0)
		worker->task = kthread_create_on_node(worker_thread,
					worker, cpu_to_node(pool->cpu),
					"kworker/%d:%d%s", pool->cpu, id, pri);
	else
		worker->task = kthread_create(worker_thread, worker,
					      "kworker/u%d:%d%s",
					      pool->id, id, pri);
	if (IS_ERR(worker->task))
		goto fail;

	set_user_nice(worker->task, pool->attrs->nice);
	set_cpus_allowed_ptr(worker->task, pool->attrs->cpumask);

	/*
	 * %PF_THREAD_BOUND is used to prevent userland from meddling with
	 * cpumask of workqueue workers.  This is an abuse.  We need
	 * %PF_NO_SETAFFINITY.
	 */
	worker->task->flags |= PF_THREAD_BOUND;

	/*
	 * The caller is responsible for ensuring %POOL_DISASSOCIATED
	 * remains stable across this function.  See the comments above the
	 * flag definition for details.
	 */
	if (pool->flags & POOL_DISASSOCIATED)
		worker->flags |= WORKER_UNBOUND;

	return worker;
fail:
	if (id >= 0) {
		spin_lock_irq(&pool->lock);
		ida_remove(&pool->worker_ida, id);
		spin_unlock_irq(&pool->lock);
	}
	kfree(worker);
	return NULL;
}

/**
 * start_worker - start a newly created worker
 * @worker: worker to start
 *
 * Make the pool aware of @worker and start it.
 *
 * CONTEXT:
 * spin_lock_irq(pool->lock).
 */
static void start_worker(struct worker *worker)
{
	worker->flags |= WORKER_STARTED;
	worker->pool->nr_workers++;
	worker_enter_idle(worker);
	wake_up_process(worker->task);
}

/**
 * destroy_worker - destroy a workqueue worker
 * @worker: worker to be destroyed
 *
 * Destroy @worker and adjust @pool stats accordingly.
 *
 * CONTEXT:
 * spin_lock_irq(pool->lock) which is released and regrabbed.
 */
static void destroy_worker(struct worker *worker)
{
	struct worker_pool *pool = worker->pool;
	int id = worker->id;

	/* sanity check frenzy */
	if (WARN_ON(worker->current_work) ||
	    WARN_ON(!list_empty(&worker->scheduled)))
		return;

	if (worker->flags & WORKER_STARTED)
		pool->nr_workers--;
	if (worker->flags & WORKER_IDLE)
		pool->nr_idle--;

	list_del_init(&worker->entry);
	worker->flags |= WORKER_DIE;

	spin_unlock_irq(&pool->lock);

	kthread_stop(worker->task);
	kfree(worker);

	spin_lock_irq(&pool->lock);
	ida_remove(&pool->worker_ida, id);
}

static void idle_worker_timeout(unsigned long __pool)
{
	struct worker_pool *pool = (void *)__pool;

	spin_lock_irq(&pool->lock);

	if (too_many_workers(pool)) {
		struct worker *worker;
		unsigned long expires;

		/* idle_list is kept in LIFO order, check the last one */
		worker = list_entry(pool->idle_list.prev, struct worker, entry);
		expires = worker->last_active + IDLE_WORKER_TIMEOUT;

		if (time_before(jiffies, expires))
			mod_timer(&pool->idle_timer, expires);
		else {
			/* it's been idle for too long, wake up manager */
			pool->flags |= POOL_MANAGE_WORKERS;
			wake_up_worker(pool);
		}
	}

	spin_unlock_irq(&pool->lock);
}

static void send_mayday(struct work_struct *work)
{
	struct pool_workqueue *pwq = get_work_pwq(work);
	struct workqueue_struct *wq = pwq->wq;

	lockdep_assert_held(&workqueue_lock);

	if (!wq->rescuer)
		return;

	/* mayday mayday mayday */
	if (list_empty(&pwq->mayday_node)) {
		list_add_tail(&pwq->mayday_node, &wq->maydays);
		wake_up_process(wq->rescuer->task);
	}
}

static void pool_mayday_timeout(unsigned long __pool)
{
	struct worker_pool *pool = (void *)__pool;
	struct work_struct *work;

	spin_lock_irq(&workqueue_lock);		/* for wq->maydays */
	spin_lock(&pool->lock);

	if (need_to_create_worker(pool)) {
		/*
		 * We've been trying to create a new worker but
		 * haven't been successful.  We might be hitting an
		 * allocation deadlock.  Send distress signals to
		 * rescuers.
		 */
		list_for_each_entry(work, &pool->worklist, entry)
			send_mayday(work);
	}

	spin_unlock(&pool->lock);
	spin_unlock_irq(&workqueue_lock);

	mod_timer(&pool->mayday_timer, jiffies + MAYDAY_INTERVAL);
}

/**
 * maybe_create_worker - create a new worker if necessary
 * @pool: pool to create a new worker for
 *
 * Create a new worker for @pool if necessary.  @pool is guaranteed to
 * have at least one idle worker on return from this function.  If
 * creating a new worker takes longer than MAYDAY_INTERVAL, mayday is
 * sent to all rescuers with works scheduled on @pool to resolve
 * possible allocation deadlock.
 *
 * On return, need_to_create_worker() is guaranteed to be false and
 * may_start_working() true.
 *
 * LOCKING:
 * spin_lock_irq(pool->lock) which may be released and regrabbed
 * multiple times.  Does GFP_KERNEL allocations.  Called only from
 * manager.
 *
 * RETURNS:
 * false if no action was taken and pool->lock stayed locked, true
 * otherwise.
 */
static bool maybe_create_worker(struct worker_pool *pool)
__releases(&pool->lock)
__acquires(&pool->lock)
{
	if (!need_to_create_worker(pool))
		return false;
restart:
	spin_unlock_irq(&pool->lock);

	/* if we don't make progress in MAYDAY_INITIAL_TIMEOUT, call for help */
	mod_timer(&pool->mayday_timer, jiffies + MAYDAY_INITIAL_TIMEOUT);

	while (true) {
		struct worker *worker;

		worker = create_worker(pool);
		if (worker) {
			del_timer_sync(&pool->mayday_timer);
			spin_lock_irq(&pool->lock);
			start_worker(worker);
			if (WARN_ON_ONCE(need_to_create_worker(pool)))
				goto restart;
			return true;
		}

		if (!need_to_create_worker(pool))
			break;

		__set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(CREATE_COOLDOWN);

		if (!need_to_create_worker(pool))
			break;
	}

	del_timer_sync(&pool->mayday_timer);
	spin_lock_irq(&pool->lock);
	if (need_to_create_worker(pool))
		goto restart;
	return true;
}

/**
 * maybe_destroy_worker - destroy workers which have been idle for a while
 * @pool: pool to destroy workers for
 *
 * Destroy @pool workers which have been idle for longer than
 * IDLE_WORKER_TIMEOUT.
 *
 * LOCKING:
 * spin_lock_irq(pool->lock) which may be released and regrabbed
 * multiple times.  Called only from manager.
 *
 * RETURNS:
 * false if no action was taken and pool->lock stayed locked, true
 * otherwise.
 */
static bool maybe_destroy_workers(struct worker_pool *pool)
{
	bool ret = false;

	while (too_many_workers(pool)) {
		struct worker *worker;
		unsigned long expires;

		worker = list_entry(pool->idle_list.prev, struct worker, entry);
		expires = worker->last_active + IDLE_WORKER_TIMEOUT;

		if (time_before(jiffies, expires)) {
			mod_timer(&pool->idle_timer, expires);
			break;
		}

		destroy_worker(worker);
		ret = true;
	}

	return ret;
}

/**
 * manage_workers - manage worker pool
 * @worker: self
 *
 * Assume the manager role and manage the worker pool @worker belongs
 * to.  At any given time, there can be only zero or one manager per
 * pool.  The exclusion is handled automatically by this function.
 *
 * The caller can safely start processing works on false return.  On
 * true return, it's guaranteed that need_to_create_worker() is false
 * and may_start_working() is true.
 *
 * CONTEXT:
 * spin_lock_irq(pool->lock) which may be released and regrabbed
 * multiple times.  Does GFP_KERNEL allocations.
 *
 * RETURNS:
 * spin_lock_irq(pool->lock) which may be released and regrabbed
 * multiple times.  Does GFP_KERNEL allocations.
 */
static bool manage_workers(struct worker *worker)
{
	struct worker_pool *pool = worker->pool;
	bool ret = false;

	if (!mutex_trylock(&pool->manager_arb))
		return ret;

	/*
	 * To simplify both worker management and CPU hotplug, hold off
	 * management while hotplug is in progress.  CPU hotplug path can't
	 * grab @pool->manager_arb to achieve this because that can lead to
	 * idle worker depletion (all become busy thinking someone else is
	 * managing) which in turn can result in deadlock under extreme
	 * circumstances.  Use @pool->assoc_mutex to synchronize manager
	 * against CPU hotplug.
	 *
	 * assoc_mutex would always be free unless CPU hotplug is in
	 * progress.  trylock first without dropping @pool->lock.
	 */
	if (unlikely(!mutex_trylock(&pool->assoc_mutex))) {
		spin_unlock_irq(&pool->lock);
		mutex_lock(&pool->assoc_mutex);
		/*
		 * CPU hotplug could have happened while we were waiting
		 * for assoc_mutex.  Hotplug itself can't handle us
		 * because manager isn't either on idle or busy list, and
		 * @pool's state and ours could have deviated.
		 *
		 * As hotplug is now excluded via assoc_mutex, we can
		 * simply try to bind.  It will succeed or fail depending
		 * on @pool's current state.  Try it and adjust
		 * %WORKER_UNBOUND accordingly.
		 */
		if (worker_maybe_bind_and_lock(pool))
			worker->flags &= ~WORKER_UNBOUND;
		else
			worker->flags |= WORKER_UNBOUND;

		ret = true;
	}

	pool->flags &= ~POOL_MANAGE_WORKERS;

	/*
	 * Destroy and then create so that may_start_working() is true
	 * on return.
	 */
	ret |= maybe_destroy_workers(pool);
	ret |= maybe_create_worker(pool);

	mutex_unlock(&pool->assoc_mutex);
	mutex_unlock(&pool->manager_arb);
	return ret;
}

/**
 * process_one_work - process single work
 * @worker: self
 * @work: work to process
 *
 * Process @work.  This function contains all the logics necessary to
 * process a single work including synchronization against and
 * interaction with other workers on the same cpu, queueing and
 * flushing.  As long as context requirement is met, any worker can
 * call this function to process a work.
 *
 * CONTEXT:
 * spin_lock_irq(pool->lock) which is released and regrabbed.
 */
static void process_one_work(struct worker *worker, struct work_struct *work)
__releases(&pool->lock)
__acquires(&pool->lock)
{
	struct pool_workqueue *pwq = get_work_pwq(work);
	struct worker_pool *pool = worker->pool;
	bool cpu_intensive = pwq->wq->flags & WQ_CPU_INTENSIVE;
	int work_color;
	struct worker *collision;
#ifdef CONFIG_LOCKDEP
	/*
	 * It is permissible to free the struct work_struct from
	 * inside the function that is called from it, this we need to
	 * take into account for lockdep too.  To avoid bogus "held
	 * lock freed" warnings as well as problems when looking into
	 * work->lockdep_map, make a copy and use that here.
	 */
	struct lockdep_map lockdep_map;

	lockdep_copy_map(&lockdep_map, &work->lockdep_map);
#endif
	/*
	 * Ensure we're on the correct CPU.  DISASSOCIATED test is
	 * necessary to avoid spurious warnings from rescuers servicing the
	 * unbound or a disassociated pool.
	 */
	WARN_ON_ONCE(!(worker->flags & WORKER_UNBOUND) &&
		     !(pool->flags & POOL_DISASSOCIATED) &&
		     raw_smp_processor_id() != pool->cpu);

	/*
	 * A single work shouldn't be executed concurrently by
	 * multiple workers on a single cpu.  Check whether anyone is
	 * already processing the work.  If so, defer the work to the
	 * currently executing one.
	 */
	collision = find_worker_executing_work(pool, work);
	if (unlikely(collision)) {
		move_linked_works(work, &collision->scheduled, NULL);
		return;
	}

	/* claim and dequeue */
	debug_work_deactivate(work);
	hash_add(pool->busy_hash, &worker->hentry, (unsigned long)work);
	worker->current_work = work;
	worker->current_func = work->func;
	worker->current_pwq = pwq;
	work_color = get_work_color(work);

	list_del_init(&work->entry);

	/*
	 * CPU intensive works don't participate in concurrency
	 * management.  They're the scheduler's responsibility.
	 */
	if (unlikely(cpu_intensive))
		worker_set_flags(worker, WORKER_CPU_INTENSIVE, true);

	/*
	 * Unbound pool isn't concurrency managed and work items should be
	 * executed ASAP.  Wake up another worker if necessary.
	 */
	if ((worker->flags & WORKER_UNBOUND) && need_more_worker(pool))
		wake_up_worker(pool);

	/*
	 * Record the last pool and clear PENDING which should be the last
	 * update to @work.  Also, do this inside @pool->lock so that
	 * PENDING and queued state changes happen together while IRQ is
	 * disabled.
	 */
	set_work_pool_and_clear_pending(work, pool->id);

	spin_unlock_irq(&pool->lock);

	lock_map_acquire_read(&pwq->wq->lockdep_map);
	lock_map_acquire(&lockdep_map);
	trace_workqueue_execute_start(work);
	worker->current_func(work);
	/*
	 * While we must be careful to not use "work" after this, the trace
	 * point will only record its address.
	 */
	trace_workqueue_execute_end(work);
	lock_map_release(&lockdep_map);
	lock_map_release(&pwq->wq->lockdep_map);

	if (unlikely(in_atomic() || lockdep_depth(current) > 0)) {
		pr_err("BUG: workqueue leaked lock or atomic: %s/0x%08x/%d\n"
		       "     last function: %pf\n",
		       current->comm, preempt_count(), task_pid_nr(current),
		       worker->current_func);
		debug_show_held_locks(current);
		dump_stack();
	}

	spin_lock_irq(&pool->lock);

	/* clear cpu intensive status */
	if (unlikely(cpu_intensive))
		worker_clr_flags(worker, WORKER_CPU_INTENSIVE);

	/* we're done with it, release */
	hash_del(&worker->hentry);
	worker->current_work = NULL;
	worker->current_func = NULL;
	worker->current_pwq = NULL;
	pwq_dec_nr_in_flight(pwq, work_color);
}

/**
 * process_scheduled_works - process scheduled works
 * @worker: self
 *
 * Process all scheduled works.  Please note that the scheduled list
 * may change while processing a work, so this function repeatedly
 * fetches a work from the top and executes it.
 *
 * CONTEXT:
 * spin_lock_irq(pool->lock) which may be released and regrabbed
 * multiple times.
 */
static void process_scheduled_works(struct worker *worker)
{
	while (!list_empty(&worker->scheduled)) {
		struct work_struct *work = list_first_entry(&worker->scheduled,
						struct work_struct, entry);
		process_one_work(worker, work);
	}
}

/**
 * worker_thread - the worker thread function
 * @__worker: self
 *
 * The worker thread function.  There are NR_CPU_WORKER_POOLS dynamic pools
 * of these per each cpu.  These workers process all works regardless of
 * their specific target workqueue.  The only exception is works which
 * belong to workqueues with a rescuer which will be explained in
 * rescuer_thread().
 */
static int worker_thread(void *__worker)
{
	struct worker *worker = __worker;
	struct worker_pool *pool = worker->pool;

	/* tell the scheduler that this is a workqueue worker */
	worker->task->flags |= PF_WQ_WORKER;
woke_up:
	spin_lock_irq(&pool->lock);

	/* we are off idle list if destruction or rebind is requested */
	if (unlikely(list_empty(&worker->entry))) {
		spin_unlock_irq(&pool->lock);

		/* if DIE is set, destruction is requested */
		if (worker->flags & WORKER_DIE) {
			worker->task->flags &= ~PF_WQ_WORKER;
			return 0;
		}

		/* otherwise, rebind */
		idle_worker_rebind(worker);
		goto woke_up;
	}

	worker_leave_idle(worker);
recheck:
	/* no more worker necessary? */
	if (!need_more_worker(pool))
		goto sleep;

	/* do we need to manage? */
	if (unlikely(!may_start_working(pool)) && manage_workers(worker))
		goto recheck;

	/*
	 * ->scheduled list can only be filled while a worker is
	 * preparing to process a work or actually processing it.
	 * Make sure nobody diddled with it while I was sleeping.
	 */
	WARN_ON_ONCE(!list_empty(&worker->scheduled));

	/*
	 * When control reaches this point, we're guaranteed to have
	 * at least one idle worker or that someone else has already
	 * assumed the manager role.
	 */
	worker_clr_flags(worker, WORKER_PREP);

	do {
		struct work_struct *work =
			list_first_entry(&pool->worklist,
					 struct work_struct, entry);

		if (likely(!(*work_data_bits(work) & WORK_STRUCT_LINKED))) {
			/* optimization path, not strictly necessary */
			process_one_work(worker, work);
			if (unlikely(!list_empty(&worker->scheduled)))
				process_scheduled_works(worker);
		} else {
			move_linked_works(work, &worker->scheduled, NULL);
			process_scheduled_works(worker);
		}
	} while (keep_working(pool));

	worker_set_flags(worker, WORKER_PREP, false);
sleep:
	if (unlikely(need_to_manage_workers(pool)) && manage_workers(worker))
		goto recheck;

	/*
	 * pool->lock is held and there's no work to process and no need to
	 * manage, sleep.  Workers are woken up only while holding
	 * pool->lock or from local cpu, so setting the current state
	 * before releasing pool->lock is enough to prevent losing any
	 * event.
	 */
	worker_enter_idle(worker);
	__set_current_state(TASK_INTERRUPTIBLE);
	spin_unlock_irq(&pool->lock);
	schedule();
	goto woke_up;
}

/**
 * rescuer_thread - the rescuer thread function
 * @__rescuer: self
 *
 * Workqueue rescuer thread function.  There's one rescuer for each
 * workqueue which has WQ_MEM_RECLAIM set.
 *
 * Regular work processing on a pool may block trying to create a new
 * worker which uses GFP_KERNEL allocation which has slight chance of
 * developing into deadlock if some works currently on the same queue
 * need to be processed to satisfy the GFP_KERNEL allocation.  This is
 * the problem rescuer solves.
 *
 * When such condition is possible, the pool summons rescuers of all
 * workqueues which have works queued on the pool and let them process
 * those works so that forward progress can be guaranteed.
 *
 * This should happen rarely.
 */
static int rescuer_thread(void *__rescuer)
{
	struct worker *rescuer = __rescuer;
	struct workqueue_struct *wq = rescuer->rescue_wq;
	struct list_head *scheduled = &rescuer->scheduled;

	set_user_nice(current, RESCUER_NICE_LEVEL);

	/*
	 * Mark rescuer as worker too.  As WORKER_PREP is never cleared, it
	 * doesn't participate in concurrency management.
	 */
	rescuer->task->flags |= PF_WQ_WORKER;
repeat:
	set_current_state(TASK_INTERRUPTIBLE);

	if (kthread_should_stop()) {
		__set_current_state(TASK_RUNNING);
		rescuer->task->flags &= ~PF_WQ_WORKER;
		return 0;
	}

	/* see whether any pwq is asking for help */
	spin_lock_irq(&workqueue_lock);

	while (!list_empty(&wq->maydays)) {
		struct pool_workqueue *pwq = list_first_entry(&wq->maydays,
					struct pool_workqueue, mayday_node);
		struct worker_pool *pool = pwq->pool;
		struct work_struct *work, *n;

		__set_current_state(TASK_RUNNING);
		list_del_init(&pwq->mayday_node);

		spin_unlock_irq(&workqueue_lock);

		/* migrate to the target cpu if possible */
		worker_maybe_bind_and_lock(pool);
		rescuer->pool = pool;

		/*
		 * Slurp in all works issued via this workqueue and
		 * process'em.
		 */
		WARN_ON_ONCE(!list_empty(&rescuer->scheduled));
		list_for_each_entry_safe(work, n, &pool->worklist, entry)
			if (get_work_pwq(work) == pwq)
				move_linked_works(work, scheduled, &n);

		process_scheduled_works(rescuer);

		/*
		 * Leave this pool.  If keep_working() is %true, notify a
		 * regular worker; otherwise, we end up with 0 concurrency
		 * and stalling the execution.
		 */
		if (keep_working(pool))
			wake_up_worker(pool);

		rescuer->pool = NULL;
		spin_unlock(&pool->lock);
		spin_lock(&workqueue_lock);
	}

	spin_unlock_irq(&workqueue_lock);

	/* rescuers should never participate in concurrency management */
	WARN_ON_ONCE(!(rescuer->flags & WORKER_NOT_RUNNING));
	schedule();
	goto repeat;
}

struct wq_barrier {
	struct work_struct	work;
	struct completion	done;
};

static void wq_barrier_func(struct work_struct *work)
{
	struct wq_barrier *barr = container_of(work, struct wq_barrier, work);
	complete(&barr->done);
}

/**
 * insert_wq_barrier - insert a barrier work
 * @pwq: pwq to insert barrier into
 * @barr: wq_barrier to insert
 * @target: target work to attach @barr to
 * @worker: worker currently executing @target, NULL if @target is not executing
 *
 * @barr is linked to @target such that @barr is completed only after
 * @target finishes execution.  Please note that the ordering
 * guarantee is observed only with respect to @target and on the local
 * cpu.
 *
 * Currently, a queued barrier can't be canceled.  This is because
 * try_to_grab_pending() can't determine whether the work to be
 * grabbed is at the head of the queue and thus can't clear LINKED
 * flag of the previous work while there must be a valid next work
 * after a work with LINKED flag set.
 *
 * Note that when @worker is non-NULL, @target may be modified
 * underneath us, so we can't reliably determine pwq from @target.
 *
 * CONTEXT:
 * spin_lock_irq(pool->lock).
 */
static void insert_wq_barrier(struct pool_workqueue *pwq,
			      struct wq_barrier *barr,
			      struct work_struct *target, struct worker *worker)
{
	struct list_head *head;
	unsigned int linked = 0;

	/*
	 * debugobject calls are safe here even with pool->lock locked
	 * as we know for sure that this will not trigger any of the
	 * checks and call back into the fixup functions where we
	 * might deadlock.
	 */
	INIT_WORK_ONSTACK(&barr->work, wq_barrier_func);
	__set_bit(WORK_STRUCT_PENDING_BIT, work_data_bits(&barr->work));
	init_completion(&barr->done);

	/*
	 * If @target is currently being executed, schedule the
	 * barrier to the worker; otherwise, put it after @target.
	 */
	if (worker)
		head = worker->scheduled.next;
	else {
		unsigned long *bits = work_data_bits(target);

		head = target->entry.next;
		/* there can already be other linked works, inherit and set */
		linked = *bits & WORK_STRUCT_LINKED;
		__set_bit(WORK_STRUCT_LINKED_BIT, bits);
	}

	debug_work_activate(&barr->work);
	insert_work(pwq, &barr->work, head,
		    work_color_to_flags(WORK_NO_COLOR) | linked);
}

/**
 * flush_workqueue_prep_pwqs - prepare pwqs for workqueue flushing
 * @wq: workqueue being flushed
 * @flush_color: new flush color, < 0 for no-op
 * @work_color: new work color, < 0 for no-op
 *
 * Prepare pwqs for workqueue flushing.
 *
 * If @flush_color is non-negative, flush_color on all pwqs should be
 * -1.  If no pwq has in-flight commands at the specified color, all
 * pwq->flush_color's stay at -1 and %false is returned.  If any pwq
 * has in flight commands, its pwq->flush_color is set to
 * @flush_color, @wq->nr_pwqs_to_flush is updated accordingly, pwq
 * wakeup logic is armed and %true is returned.
 *
 * The caller should have initialized @wq->first_flusher prior to
 * calling this function with non-negative @flush_color.  If
 * @flush_color is negative, no flush color update is done and %false
 * is returned.
 *
 * If @work_color is non-negative, all pwqs should have the same
 * work_color which is previous to @work_color and all will be
 * advanced to @work_color.
 *
 * CONTEXT:
 * mutex_lock(wq->flush_mutex).
 *
 * RETURNS:
 * %true if @flush_color >= 0 and there's something to flush.  %false
 * otherwise.
 */
static bool flush_workqueue_prep_pwqs(struct workqueue_struct *wq,
				      int flush_color, int work_color)
{
	bool wait = false;
	struct pool_workqueue *pwq;

	if (flush_color >= 0) {
		WARN_ON_ONCE(atomic_read(&wq->nr_pwqs_to_flush));
		atomic_set(&wq->nr_pwqs_to_flush, 1);
	}

	local_irq_disable();

	for_each_pwq(pwq, wq) {
		struct worker_pool *pool = pwq->pool;

		spin_lock(&pool->lock);

		if (flush_color >= 0) {
			WARN_ON_ONCE(pwq->flush_color != -1);

			if (pwq->nr_in_flight[flush_color]) {
				pwq->flush_color = flush_color;
				atomic_inc(&wq->nr_pwqs_to_flush);
				wait = true;
			}
		}

		if (work_color >= 0) {
			WARN_ON_ONCE(work_color != work_next_color(pwq->work_color));
			pwq->work_color = work_color;
		}

		spin_unlock(&pool->lock);
	}

	local_irq_enable();

	if (flush_color >= 0 && atomic_dec_and_test(&wq->nr_pwqs_to_flush))
		complete(&wq->first_flusher->done);

	return wait;
}

/**
 * flush_workqueue - ensure that any scheduled work has run to completion.
 * @wq: workqueue to flush
 *
 * Forces execution of the workqueue and blocks until its completion.
 * This is typically used in driver shutdown handlers.
 *
 * We sleep until all works which were queued on entry have been handled,
 * but we are not livelocked by new incoming ones.
 */
void flush_workqueue(struct workqueue_struct *wq)
{
	struct wq_flusher this_flusher = {
		.list = LIST_HEAD_INIT(this_flusher.list),
		.flush_color = -1,
		.done = COMPLETION_INITIALIZER_ONSTACK(this_flusher.done),
	};
	int next_color;

	lock_map_acquire(&wq->lockdep_map);
	lock_map_release(&wq->lockdep_map);

	mutex_lock(&wq->flush_mutex);

	/*
	 * Start-to-wait phase
	 */
	next_color = work_next_color(wq->work_color);

	if (next_color != wq->flush_color) {
		/*
		 * Color space is not full.  The current work_color
		 * becomes our flush_color and work_color is advanced
		 * by one.
		 */
		WARN_ON_ONCE(!list_empty(&wq->flusher_overflow));
		this_flusher.flush_color = wq->work_color;
		wq->work_color = next_color;

		if (!wq->first_flusher) {
			/* no flush in progress, become the first flusher */
			WARN_ON_ONCE(wq->flush_color != this_flusher.flush_color);

			wq->first_flusher = &this_flusher;

			if (!flush_workqueue_prep_pwqs(wq, wq->flush_color,
						       wq->work_color)) {
				/* nothing to flush, done */
				wq->flush_color = next_color;
				wq->first_flusher = NULL;
				goto out_unlock;
			}
		} else {
			/* wait in queue */
			WARN_ON_ONCE(wq->flush_color == this_flusher.flush_color);
			list_add_tail(&this_flusher.list, &wq->flusher_queue);
			flush_workqueue_prep_pwqs(wq, -1, wq->work_color);
		}
	} else {
		/*
		 * Oops, color space is full, wait on overflow queue.
		 * The next flush completion will assign us
		 * flush_color and transfer to flusher_queue.
		 */
		list_add_tail(&this_flusher.list, &wq->flusher_overflow);
	}

	mutex_unlock(&wq->flush_mutex);

	wait_for_completion(&this_flusher.done);

	/*
	 * Wake-up-and-cascade phase
	 *
	 * First flushers are responsible for cascading flushes and
	 * handling overflow.  Non-first flushers can simply return.
	 */
	if (wq->first_flusher != &this_flusher)
		return;

	mutex_lock(&wq->flush_mutex);

	/* we might have raced, check again with mutex held */
	if (wq->first_flusher != &this_flusher)
		goto out_unlock;

	wq->first_flusher = NULL;

	WARN_ON_ONCE(!list_empty(&this_flusher.list));
	WARN_ON_ONCE(wq->flush_color != this_flusher.flush_color);

	while (true) {
		struct wq_flusher *next, *tmp;

		/* complete all the flushers sharing the current flush color */
		list_for_each_entry_safe(next, tmp, &wq->flusher_queue, list) {
			if (next->flush_color != wq->flush_color)
				break;
			list_del_init(&next->list);
			complete(&next->done);
		}

		WARN_ON_ONCE(!list_empty(&wq->flusher_overflow) &&
			     wq->flush_color != work_next_color(wq->work_color));

		/* this flush_color is finished, advance by one */
		wq->flush_color = work_next_color(wq->flush_color);

		/* one color has been freed, handle overflow queue */
		if (!list_empty(&wq->flusher_overflow)) {
			/*
			 * Assign the same color to all overflowed
			 * flushers, advance work_color and append to
			 * flusher_queue.  This is the start-to-wait
			 * phase for these overflowed flushers.
			 */
			list_for_each_entry(tmp, &wq->flusher_overflow, list)
				tmp->flush_color = wq->work_color;

			wq->work_color = work_next_color(wq->work_color);

			list_splice_tail_init(&wq->flusher_overflow,
					      &wq->flusher_queue);
			flush_workqueue_prep_pwqs(wq, -1, wq->work_color);
		}

		if (list_empty(&wq->flusher_queue)) {
			WARN_ON_ONCE(wq->flush_color != wq->work_color);
			break;
		}

		/*
		 * Need to flush more colors.  Make the next flusher
		 * the new first flusher and arm pwqs.
		 */
		WARN_ON_ONCE(wq->flush_color == wq->work_color);
		WARN_ON_ONCE(wq->flush_color != next->flush_color);

		list_del_init(&next->list);
		wq->first_flusher = next;

		if (flush_workqueue_prep_pwqs(wq, wq->flush_color, -1))
			break;

		/*
		 * Meh... this color is already done, clear first
		 * flusher and repeat cascading.
		 */
		wq->first_flusher = NULL;
	}

out_unlock:
	mutex_unlock(&wq->flush_mutex);
}
EXPORT_SYMBOL_GPL(flush_workqueue);

/**
 * drain_workqueue - drain a workqueue
 * @wq: workqueue to drain
 *
 * Wait until the workqueue becomes empty.  While draining is in progress,
 * only chain queueing is allowed.  IOW, only currently pending or running
 * work items on @wq can queue further work items on it.  @wq is flushed
 * repeatedly until it becomes empty.  The number of flushing is detemined
 * by the depth of chaining and should be relatively short.  Whine if it
 * takes too long.
 */
void drain_workqueue(struct workqueue_struct *wq)
{
	unsigned int flush_cnt = 0;
	struct pool_workqueue *pwq;

	/*
	 * __queue_work() needs to test whether there are drainers, is much
	 * hotter than drain_workqueue() and already looks at @wq->flags.
	 * Use WQ_DRAINING so that queue doesn't have to check nr_drainers.
	 */
	spin_lock_irq(&workqueue_lock);
	if (!wq->nr_drainers++)
		wq->flags |= WQ_DRAINING;
	spin_unlock_irq(&workqueue_lock);
reflush:
	flush_workqueue(wq);

	local_irq_disable();

	for_each_pwq(pwq, wq) {
		bool drained;

		spin_lock(&pwq->pool->lock);
		drained = !pwq->nr_active && list_empty(&pwq->delayed_works);
		spin_unlock(&pwq->pool->lock);

		if (drained)
			continue;

		if (++flush_cnt == 10 ||
		    (flush_cnt % 100 == 0 && flush_cnt <= 1000))
			pr_warn("workqueue %s: flush on destruction isn't complete after %u tries\n",
				wq->name, flush_cnt);

		local_irq_enable();
		goto reflush;
	}

	spin_lock(&workqueue_lock);
	if (!--wq->nr_drainers)
		wq->flags &= ~WQ_DRAINING;
	spin_unlock(&workqueue_lock);

	local_irq_enable();
}
EXPORT_SYMBOL_GPL(drain_workqueue);

static bool start_flush_work(struct work_struct *work, struct wq_barrier *barr)
{
	struct worker *worker = NULL;
	struct worker_pool *pool;
	struct pool_workqueue *pwq;

	might_sleep();

	local_irq_disable();
	pool = get_work_pool(work);
	if (!pool) {
		local_irq_enable();
		return false;
	}

	spin_lock(&pool->lock);
	/* see the comment in try_to_grab_pending() with the same code */
	pwq = get_work_pwq(work);
	if (pwq) {
		if (unlikely(pwq->pool != pool))
			goto already_gone;
	} else {
		worker = find_worker_executing_work(pool, work);
		if (!worker)
			goto already_gone;
		pwq = worker->current_pwq;
	}

	insert_wq_barrier(pwq, barr, work, worker);
	spin_unlock_irq(&pool->lock);

	/*
	 * If @max_active is 1 or rescuer is in use, flushing another work
	 * item on the same workqueue may lead to deadlock.  Make sure the
	 * flusher is not running on the same workqueue by verifying write
	 * access.
	 */
	if (pwq->wq->saved_max_active == 1 || pwq->wq->rescuer)
		lock_map_acquire(&pwq->wq->lockdep_map);
	else
		lock_map_acquire_read(&pwq->wq->lockdep_map);
	lock_map_release(&pwq->wq->lockdep_map);

	return true;
already_gone:
	spin_unlock_irq(&pool->lock);
	return false;
}

/**
 * flush_work - wait for a work to finish executing the last queueing instance
 * @work: the work to flush
 *
 * Wait until @work has finished execution.  @work is guaranteed to be idle
 * on return if it hasn't been requeued since flush started.
 *
 * RETURNS:
 * %true if flush_work() waited for the work to finish execution,
 * %false if it was already idle.
 */
bool flush_work(struct work_struct *work)
{
	struct wq_barrier barr;

	lock_map_acquire(&work->lockdep_map);
	lock_map_release(&work->lockdep_map);

	if (start_flush_work(work, &barr)) {
		wait_for_completion(&barr.done);
		destroy_work_on_stack(&barr.work);
		return true;
	} else {
		return false;
	}
}
EXPORT_SYMBOL_GPL(flush_work);

static bool __cancel_work_timer(struct work_struct *work, bool is_dwork)
{
	unsigned long flags;
	int ret;

	do {
		ret = try_to_grab_pending(work, is_dwork, &flags);
		/*
		 * If someone else is canceling, wait for the same event it
		 * would be waiting for before retrying.
		 */
		if (unlikely(ret == -ENOENT))
			flush_work(work);
	} while (unlikely(ret < 0));

	/* tell other tasks trying to grab @work to back off */
	mark_work_canceling(work);
	local_irq_restore(flags);

	flush_work(work);
	clear_work_data(work);
	return ret;
}

/**
 * cancel_work_sync - cancel a work and wait for it to finish
 * @work: the work to cancel
 *
 * Cancel @work and wait for its execution to finish.  This function
 * can be used even if the work re-queues itself or migrates to
 * another workqueue.  On return from this function, @work is
 * guaranteed to be not pending or executing on any CPU.
 *
 * cancel_work_sync(&delayed_work->work) must not be used for
 * delayed_work's.  Use cancel_delayed_work_sync() instead.
 *
 * The caller must ensure that the workqueue on which @work was last
 * queued can't be destroyed before this function returns.
 *
 * RETURNS:
 * %true if @work was pending, %false otherwise.
 */
bool cancel_work_sync(struct work_struct *work)
{
	return __cancel_work_timer(work, false);
}
EXPORT_SYMBOL_GPL(cancel_work_sync);

/**
 * flush_delayed_work - wait for a dwork to finish executing the last queueing
 * @dwork: the delayed work to flush
 *
 * Delayed timer is cancelled and the pending work is queued for
 * immediate execution.  Like flush_work(), this function only
 * considers the last queueing instance of @dwork.
 *
 * RETURNS:
 * %true if flush_work() waited for the work to finish execution,
 * %false if it was already idle.
 */
bool flush_delayed_work(struct delayed_work *dwork)
{
	local_irq_disable();
	if (del_timer_sync(&dwork->timer))
		__queue_work(dwork->cpu, dwork->wq, &dwork->work);
	local_irq_enable();
	return flush_work(&dwork->work);
}
EXPORT_SYMBOL(flush_delayed_work);

/**
 * cancel_delayed_work - cancel a delayed work
 * @dwork: delayed_work to cancel
 *
 * Kill off a pending delayed_work.  Returns %true if @dwork was pending
 * and canceled; %false if wasn't pending.  Note that the work callback
 * function may still be running on return, unless it returns %true and the
 * work doesn't re-arm itself.  Explicitly flush or use
 * cancel_delayed_work_sync() to wait on it.
 *
 * This function is safe to call from any context including IRQ handler.
 */
bool cancel_delayed_work(struct delayed_work *dwork)
{
	unsigned long flags;
	int ret;

	do {
		ret = try_to_grab_pending(&dwork->work, true, &flags);
	} while (unlikely(ret == -EAGAIN));

	if (unlikely(ret < 0))
		return false;

	set_work_pool_and_clear_pending(&dwork->work,
					get_work_pool_id(&dwork->work));
	local_irq_restore(flags);
	return ret;
}
EXPORT_SYMBOL(cancel_delayed_work);

/**
 * cancel_delayed_work_sync - cancel a delayed work and wait for it to finish
 * @dwork: the delayed work cancel
 *
 * This is cancel_work_sync() for delayed works.
 *
 * RETURNS:
 * %true if @dwork was pending, %false otherwise.
 */
bool cancel_delayed_work_sync(struct delayed_work *dwork)
{
	return __cancel_work_timer(&dwork->work, true);
}
EXPORT_SYMBOL(cancel_delayed_work_sync);

/**
 * schedule_work_on - put work task on a specific cpu
 * @cpu: cpu to put the work task on
 * @work: job to be done
 *
 * This puts a job on a specific cpu
 */
bool schedule_work_on(int cpu, struct work_struct *work)
{
	return queue_work_on(cpu, system_wq, work);
}
EXPORT_SYMBOL(schedule_work_on);

/**
 * schedule_work - put work task in global workqueue
 * @work: job to be done
 *
 * Returns %false if @work was already on the kernel-global workqueue and
 * %true otherwise.
 *
 * This puts a job in the kernel-global workqueue if it was not already
 * queued and leaves it in the same position on the kernel-global
 * workqueue otherwise.
 */
bool schedule_work(struct work_struct *work)
{
	return queue_work(system_wq, work);
}
EXPORT_SYMBOL(schedule_work);

/**
 * schedule_delayed_work_on - queue work in global workqueue on CPU after delay
 * @cpu: cpu to use
 * @dwork: job to be done
 * @delay: number of jiffies to wait
 *
 * After waiting for a given time this puts a job in the kernel-global
 * workqueue on the specified CPU.
 */
bool schedule_delayed_work_on(int cpu, struct delayed_work *dwork,
			      unsigned long delay)
{
	return queue_delayed_work_on(cpu, system_wq, dwork, delay);
}
EXPORT_SYMBOL(schedule_delayed_work_on);

/**
 * schedule_delayed_work - put work task in global workqueue after delay
 * @dwork: job to be done
 * @delay: number of jiffies to wait or 0 for immediate execution
 *
 * After waiting for a given time this puts a job in the kernel-global
 * workqueue.
 */
bool schedule_delayed_work(struct delayed_work *dwork, unsigned long delay)
{
	return queue_delayed_work(system_wq, dwork, delay);
}
EXPORT_SYMBOL(schedule_delayed_work);

/**
 * schedule_on_each_cpu - execute a function synchronously on each online CPU
 * @func: the function to call
 *
 * schedule_on_each_cpu() executes @func on each online CPU using the
 * system workqueue and blocks until all CPUs have completed.
 * schedule_on_each_cpu() is very slow.
 *
 * RETURNS:
 * 0 on success, -errno on failure.
 */
int schedule_on_each_cpu(work_func_t func)
{
	int cpu;
	struct work_struct __percpu *works;

	works = alloc_percpu(struct work_struct);
	if (!works)
		return -ENOMEM;

	get_online_cpus();

	for_each_online_cpu(cpu) {
		struct work_struct *work = per_cpu_ptr(works, cpu);

		INIT_WORK(work, func);
		schedule_work_on(cpu, work);
	}

	for_each_online_cpu(cpu)
		flush_work(per_cpu_ptr(works, cpu));

	put_online_cpus();
	free_percpu(works);
	return 0;
}

/**
 * flush_scheduled_work - ensure that any scheduled work has run to completion.
 *
 * Forces execution of the kernel-global workqueue and blocks until its
 * completion.
 *
 * Think twice before calling this function!  It's very easy to get into
 * trouble if you don't take great care.  Either of the following situations
 * will lead to deadlock:
 *
 *	One of the work items currently on the workqueue needs to acquire
 *	a lock held by your code or its caller.
 *
 *	Your code is running in the context of a work routine.
 *
 * They will be detected by lockdep when they occur, but the first might not
 * occur very often.  It depends on what work items are on the workqueue and
 * what locks they need, which you have no control over.
 *
 * In most situations flushing the entire workqueue is overkill; you merely
 * need to know that a particular work item isn't queued and isn't running.
 * In such cases you should use cancel_delayed_work_sync() or
 * cancel_work_sync() instead.
 */
void flush_scheduled_work(void)
{
	flush_workqueue(system_wq);
}
EXPORT_SYMBOL(flush_scheduled_work);

/**
 * execute_in_process_context - reliably execute the routine with user context
 * @fn:		the function to execute
 * @ew:		guaranteed storage for the execute work structure (must
 *		be available when the work executes)
 *
 * Executes the function immediately if process context is available,
 * otherwise schedules the function for delayed execution.
 *
 * Returns:	0 - function was executed
 *		1 - function was scheduled for execution
 */
int execute_in_process_context(work_func_t fn, struct execute_work *ew)
{
	if (!in_interrupt()) {
		fn(&ew->work);
		return 0;
	}

	INIT_WORK(&ew->work, fn);
	schedule_work(&ew->work);

	return 1;
}
EXPORT_SYMBOL_GPL(execute_in_process_context);

int keventd_up(void)
{
	return system_wq != NULL;
}

/**
 * free_workqueue_attrs - free a workqueue_attrs
 * @attrs: workqueue_attrs to free
 *
 * Undo alloc_workqueue_attrs().
 */
void free_workqueue_attrs(struct workqueue_attrs *attrs)
{
	if (attrs) {
		free_cpumask_var(attrs->cpumask);
		kfree(attrs);
	}
}

/**
 * alloc_workqueue_attrs - allocate a workqueue_attrs
 * @gfp_mask: allocation mask to use
 *
 * Allocate a new workqueue_attrs, initialize with default settings and
 * return it.  Returns NULL on failure.
 */
struct workqueue_attrs *alloc_workqueue_attrs(gfp_t gfp_mask)
{
	struct workqueue_attrs *attrs;

	attrs = kzalloc(sizeof(*attrs), gfp_mask);
	if (!attrs)
		goto fail;
	if (!alloc_cpumask_var(&attrs->cpumask, gfp_mask))
		goto fail;

	cpumask_setall(attrs->cpumask);
	return attrs;
fail:
	free_workqueue_attrs(attrs);
	return NULL;
}

static void copy_workqueue_attrs(struct workqueue_attrs *to,
				 const struct workqueue_attrs *from)
{
	to->nice = from->nice;
	cpumask_copy(to->cpumask, from->cpumask);
}

/*
 * Hacky implementation of jhash of bitmaps which only considers the
 * specified number of bits.  We probably want a proper implementation in
 * include/linux/jhash.h.
 */
static u32 jhash_bitmap(const unsigned long *bitmap, int bits, u32 hash)
{
	int nr_longs = bits / BITS_PER_LONG;
	int nr_leftover = bits % BITS_PER_LONG;
	unsigned long leftover = 0;

	if (nr_longs)
		hash = jhash(bitmap, nr_longs * sizeof(long), hash);
	if (nr_leftover) {
		bitmap_copy(&leftover, bitmap + nr_longs, nr_leftover);
		hash = jhash(&leftover, sizeof(long), hash);
	}
	return hash;
}

/* hash value of the content of @attr */
static u32 wqattrs_hash(const struct workqueue_attrs *attrs)
{
	u32 hash = 0;

	hash = jhash_1word(attrs->nice, hash);
	hash = jhash_bitmap(cpumask_bits(attrs->cpumask), nr_cpu_ids, hash);
	return hash;
}

/* content equality test */
static bool wqattrs_equal(const struct workqueue_attrs *a,
			  const struct workqueue_attrs *b)
{
	if (a->nice != b->nice)
		return false;
	if (!cpumask_equal(a->cpumask, b->cpumask))
		return false;
	return true;
}

/**
 * init_worker_pool - initialize a newly zalloc'd worker_pool
 * @pool: worker_pool to initialize
 *
 * Initiailize a newly zalloc'd @pool.  It also allocates @pool->attrs.
 * Returns 0 on success, -errno on failure.  Even on failure, all fields
 * inside @pool proper are initialized and put_unbound_pool() can be called
 * on @pool safely to release it.
 */
static int init_worker_pool(struct worker_pool *pool)
{
	spin_lock_init(&pool->lock);
	pool->id = -1;
	pool->cpu = -1;
	pool->flags |= POOL_DISASSOCIATED;
	INIT_LIST_HEAD(&pool->worklist);
	INIT_LIST_HEAD(&pool->idle_list);
	hash_init(pool->busy_hash);

	init_timer_deferrable(&pool->idle_timer);
	pool->idle_timer.function = idle_worker_timeout;
	pool->idle_timer.data = (unsigned long)pool;

	setup_timer(&pool->mayday_timer, pool_mayday_timeout,
		    (unsigned long)pool);

	mutex_init(&pool->manager_arb);
	mutex_init(&pool->assoc_mutex);
	ida_init(&pool->worker_ida);

	INIT_HLIST_NODE(&pool->hash_node);
	pool->refcnt = 1;

	/* shouldn't fail above this point */
	pool->attrs = alloc_workqueue_attrs(GFP_KERNEL);
	if (!pool->attrs)
		return -ENOMEM;
	return 0;
}

static void rcu_free_pool(struct rcu_head *rcu)
{
	struct worker_pool *pool = container_of(rcu, struct worker_pool, rcu);

	ida_destroy(&pool->worker_ida);
	free_workqueue_attrs(pool->attrs);
	kfree(pool);
}

/**
 * put_unbound_pool - put a worker_pool
 * @pool: worker_pool to put
 *
 * Put @pool.  If its refcnt reaches zero, it gets destroyed in sched-RCU
 * safe manner.
 */
static void put_unbound_pool(struct worker_pool *pool)
{
	struct worker *worker;

	spin_lock_irq(&workqueue_lock);
	if (--pool->refcnt) {
		spin_unlock_irq(&workqueue_lock);
		return;
	}

	/* sanity checks */
	if (WARN_ON(!(pool->flags & POOL_DISASSOCIATED)) ||
	    WARN_ON(!list_empty(&pool->worklist))) {
		spin_unlock_irq(&workqueue_lock);
		return;
	}

	/* release id and unhash */
	if (pool->id >= 0)
		idr_remove(&worker_pool_idr, pool->id);
	hash_del(&pool->hash_node);

	spin_unlock_irq(&workqueue_lock);

	/* lock out manager and destroy all workers */
	mutex_lock(&pool->manager_arb);
	spin_lock_irq(&pool->lock);

	while ((worker = first_worker(pool)))
		destroy_worker(worker);
	WARN_ON(pool->nr_workers || pool->nr_idle);

	spin_unlock_irq(&pool->lock);
	mutex_unlock(&pool->manager_arb);

	/* shut down the timers */
	del_timer_sync(&pool->idle_timer);
	del_timer_sync(&pool->mayday_timer);

	/* sched-RCU protected to allow dereferences from get_work_pool() */
	call_rcu_sched(&pool->rcu, rcu_free_pool);
}

/**
 * get_unbound_pool - get a worker_pool with the specified attributes
 * @attrs: the attributes of the worker_pool to get
 *
 * Obtain a worker_pool which has the same attributes as @attrs, bump the
 * reference count and return it.  If there already is a matching
 * worker_pool, it will be used; otherwise, this function attempts to
 * create a new one.  On failure, returns NULL.
 */
static struct worker_pool *get_unbound_pool(const struct workqueue_attrs *attrs)
{
	static DEFINE_MUTEX(create_mutex);
	u32 hash = wqattrs_hash(attrs);
	struct worker_pool *pool;
	struct worker *worker;

	mutex_lock(&create_mutex);

	/* do we already have a matching pool? */
	spin_lock_irq(&workqueue_lock);
	hash_for_each_possible(unbound_pool_hash, pool, hash_node, hash) {
		if (wqattrs_equal(pool->attrs, attrs)) {
			pool->refcnt++;
			goto out_unlock;
		}
	}
	spin_unlock_irq(&workqueue_lock);

	/* nope, create a new one */
	pool = kzalloc(sizeof(*pool), GFP_KERNEL);
	if (!pool || init_worker_pool(pool) < 0)
		goto fail;

	lockdep_set_subclass(&pool->lock, 1);	/* see put_pwq() */
	copy_workqueue_attrs(pool->attrs, attrs);

	if (worker_pool_assign_id(pool) < 0)
		goto fail;

	/* create and start the initial worker */
	worker = create_worker(pool);
	if (!worker)
		goto fail;

	spin_lock_irq(&pool->lock);
	start_worker(worker);
	spin_unlock_irq(&pool->lock);

	/* install */
	spin_lock_irq(&workqueue_lock);
	hash_add(unbound_pool_hash, &pool->hash_node, hash);
out_unlock:
	spin_unlock_irq(&workqueue_lock);
	mutex_unlock(&create_mutex);
	return pool;
fail:
	mutex_unlock(&create_mutex);
	if (pool)
		put_unbound_pool(pool);
	return NULL;
}

static void rcu_free_pwq(struct rcu_head *rcu)
{
	kmem_cache_free(pwq_cache,
			container_of(rcu, struct pool_workqueue, rcu));
}

/*
 * Scheduled on system_wq by put_pwq() when an unbound pwq hits zero refcnt
 * and needs to be destroyed.
 */
static void pwq_unbound_release_workfn(struct work_struct *work)
{
	struct pool_workqueue *pwq = container_of(work, struct pool_workqueue,
						  unbound_release_work);
	struct workqueue_struct *wq = pwq->wq;
	struct worker_pool *pool = pwq->pool;

	if (WARN_ON_ONCE(!(wq->flags & WQ_UNBOUND)))
		return;

	/*
	 * Unlink @pwq.  Synchronization against flush_mutex isn't strictly
	 * necessary on release but do it anyway.  It's easier to verify
	 * and consistent with the linking path.
	 */
	mutex_lock(&wq->flush_mutex);
	spin_lock_irq(&workqueue_lock);
	list_del_rcu(&pwq->pwqs_node);
	spin_unlock_irq(&workqueue_lock);
	mutex_unlock(&wq->flush_mutex);

	put_unbound_pool(pool);
	call_rcu_sched(&pwq->rcu, rcu_free_pwq);

	/*
	 * If we're the last pwq going away, @wq is already dead and no one
	 * is gonna access it anymore.  Free it.
	 */
	if (list_empty(&wq->pwqs))
		kfree(wq);
}

static void init_and_link_pwq(struct pool_workqueue *pwq,
			      struct workqueue_struct *wq,
			      struct worker_pool *pool,
			      struct pool_workqueue **p_last_pwq)
{
	BUG_ON((unsigned long)pwq & WORK_STRUCT_FLAG_MASK);

	pwq->pool = pool;
	pwq->wq = wq;
	pwq->flush_color = -1;
	pwq->refcnt = 1;
	pwq->max_active = wq->saved_max_active;
	INIT_LIST_HEAD(&pwq->delayed_works);
	INIT_LIST_HEAD(&pwq->mayday_node);
	INIT_WORK(&pwq->unbound_release_work, pwq_unbound_release_workfn);

	/*
	 * Link @pwq and set the matching work_color.  This is synchronized
	 * with flush_mutex to avoid confusing flush_workqueue().
	 */
	mutex_lock(&wq->flush_mutex);
	spin_lock_irq(&workqueue_lock);

	if (p_last_pwq)
		*p_last_pwq = first_pwq(wq);
	pwq->work_color = wq->work_color;
	list_add_rcu(&pwq->pwqs_node, &wq->pwqs);

	spin_unlock_irq(&workqueue_lock);
	mutex_unlock(&wq->flush_mutex);
}

/**
 * apply_workqueue_attrs - apply new workqueue_attrs to an unbound workqueue
 * @wq: the target workqueue
 * @attrs: the workqueue_attrs to apply, allocated with alloc_workqueue_attrs()
 *
 * Apply @attrs to an unbound workqueue @wq.  If @attrs doesn't match the
 * current attributes, a new pwq is created and made the first pwq which
 * will serve all new work items.  Older pwqs are released as in-flight
 * work items finish.  Note that a work item which repeatedly requeues
 * itself back-to-back will stay on its current pwq.
 *
 * Performs GFP_KERNEL allocations.  Returns 0 on success and -errno on
 * failure.
 */
int apply_workqueue_attrs(struct workqueue_struct *wq,
			  const struct workqueue_attrs *attrs)
{
	struct pool_workqueue *pwq, *last_pwq;
	struct worker_pool *pool;

	if (WARN_ON(!(wq->flags & WQ_UNBOUND)))
		return -EINVAL;

	pwq = kmem_cache_zalloc(pwq_cache, GFP_KERNEL);
	if (!pwq)
		return -ENOMEM;

	pool = get_unbound_pool(attrs);
	if (!pool) {
		kmem_cache_free(pwq_cache, pwq);
		return -ENOMEM;
	}

	init_and_link_pwq(pwq, wq, pool, &last_pwq);
	if (last_pwq) {
		spin_lock_irq(&last_pwq->pool->lock);
		put_pwq(last_pwq);
		spin_unlock_irq(&last_pwq->pool->lock);
	}

	return 0;
}

static int alloc_and_link_pwqs(struct workqueue_struct *wq)
{
	bool highpri = wq->flags & WQ_HIGHPRI;
	int cpu;

	if (!(wq->flags & WQ_UNBOUND)) {
		wq->cpu_pwqs = alloc_percpu(struct pool_workqueue);
		if (!wq->cpu_pwqs)
			return -ENOMEM;

		for_each_possible_cpu(cpu) {
			struct pool_workqueue *pwq =
				per_cpu_ptr(wq->cpu_pwqs, cpu);
			struct worker_pool *cpu_pools =
				per_cpu(cpu_worker_pools, cpu);

			init_and_link_pwq(pwq, wq, &cpu_pools[highpri], NULL);
		}
		return 0;
	} else {
		return apply_workqueue_attrs(wq, unbound_std_wq_attrs[highpri]);
	}
}

static int wq_clamp_max_active(int max_active, unsigned int flags,
			       const char *name)
{
	int lim = flags & WQ_UNBOUND ? WQ_UNBOUND_MAX_ACTIVE : WQ_MAX_ACTIVE;

	if (max_active < 1 || max_active > lim)
		pr_warn("workqueue: max_active %d requested for %s is out of range, clamping between %d and %d\n",
			max_active, name, 1, lim);

	return clamp_val(max_active, 1, lim);
}

struct workqueue_struct *__alloc_workqueue_key(const char *fmt,
					       unsigned int flags,
					       int max_active,
					       struct lock_class_key *key,
					       const char *lock_name, ...)
{
	va_list args, args1;
	struct workqueue_struct *wq;
	struct pool_workqueue *pwq;
	size_t namelen;

	/* determine namelen, allocate wq and format name */
	va_start(args, lock_name);
	va_copy(args1, args);
	namelen = vsnprintf(NULL, 0, fmt, args) + 1;

	wq = kzalloc(sizeof(*wq) + namelen, GFP_KERNEL);
	if (!wq)
		return NULL;

	vsnprintf(wq->name, namelen, fmt, args1);
	va_end(args);
	va_end(args1);

	max_active = max_active ?: WQ_DFL_ACTIVE;
	max_active = wq_clamp_max_active(max_active, flags, wq->name);

	/* init wq */
	wq->flags = flags;
	wq->saved_max_active = max_active;
	mutex_init(&wq->flush_mutex);
	atomic_set(&wq->nr_pwqs_to_flush, 0);
	INIT_LIST_HEAD(&wq->pwqs);
	INIT_LIST_HEAD(&wq->flusher_queue);
	INIT_LIST_HEAD(&wq->flusher_overflow);
	INIT_LIST_HEAD(&wq->maydays);

	lockdep_init_map(&wq->lockdep_map, lock_name, key, 0);
	INIT_LIST_HEAD(&wq->list);

	if (alloc_and_link_pwqs(wq) < 0)
		goto err_free_wq;

	/*
	 * Workqueues which may be used during memory reclaim should
	 * have a rescuer to guarantee forward progress.
	 */
	if (flags & WQ_MEM_RECLAIM) {
		struct worker *rescuer;

		rescuer = alloc_worker();
		if (!rescuer)
			goto err_destroy;

		rescuer->rescue_wq = wq;
		rescuer->task = kthread_create(rescuer_thread, rescuer, "%s",
					       wq->name);
		if (IS_ERR(rescuer->task)) {
			kfree(rescuer);
			goto err_destroy;
		}

		wq->rescuer = rescuer;
		rescuer->task->flags |= PF_THREAD_BOUND;
		wake_up_process(rescuer->task);
	}

	/*
	 * workqueue_lock protects global freeze state and workqueues
	 * list.  Grab it, set max_active accordingly and add the new
	 * workqueue to workqueues list.
	 */
	spin_lock_irq(&workqueue_lock);

	if (workqueue_freezing && wq->flags & WQ_FREEZABLE)
		for_each_pwq(pwq, wq)
			pwq->max_active = 0;

	list_add(&wq->list, &workqueues);

	spin_unlock_irq(&workqueue_lock);

	return wq;

err_free_wq:
	kfree(wq);
	return NULL;
err_destroy:
	destroy_workqueue(wq);
	return NULL;
}
EXPORT_SYMBOL_GPL(__alloc_workqueue_key);

/**
 * destroy_workqueue - safely terminate a workqueue
 * @wq: target workqueue
 *
 * Safely destroy a workqueue. All work currently pending will be done first.
 */
void destroy_workqueue(struct workqueue_struct *wq)
{
	struct pool_workqueue *pwq;

	/* drain it before proceeding with destruction */
	drain_workqueue(wq);

	spin_lock_irq(&workqueue_lock);

	/* sanity checks */
	for_each_pwq(pwq, wq) {
		int i;

		for (i = 0; i < WORK_NR_COLORS; i++) {
			if (WARN_ON(pwq->nr_in_flight[i])) {
				spin_unlock_irq(&workqueue_lock);
				return;
			}
		}

		if (WARN_ON(pwq->refcnt > 1) ||
		    WARN_ON(pwq->nr_active) ||
		    WARN_ON(!list_empty(&pwq->delayed_works))) {
			spin_unlock_irq(&workqueue_lock);
			return;
		}
	}

	/*
	 * wq list is used to freeze wq, remove from list after
	 * flushing is complete in case freeze races us.
	 */
	list_del_init(&wq->list);

	spin_unlock_irq(&workqueue_lock);

	if (wq->rescuer) {
		kthread_stop(wq->rescuer->task);
		kfree(wq->rescuer);
		wq->rescuer = NULL;
	}

	if (!(wq->flags & WQ_UNBOUND)) {
		/*
		 * The base ref is never dropped on per-cpu pwqs.  Directly
		 * free the pwqs and wq.
		 */
		free_percpu(wq->cpu_pwqs);
		kfree(wq);
	} else {
		/*
		 * We're the sole accessor of @wq at this point.  Directly
		 * access the first pwq and put the base ref.  As both pwqs
		 * and pools are sched-RCU protected, the lock operations
		 * are safe.  @wq will be freed when the last pwq is
		 * released.
		 */
		pwq = list_first_entry(&wq->pwqs, struct pool_workqueue,
				       pwqs_node);
		spin_lock_irq(&pwq->pool->lock);
		put_pwq(pwq);
		spin_unlock_irq(&pwq->pool->lock);
	}
}
EXPORT_SYMBOL_GPL(destroy_workqueue);

/**
 * pwq_set_max_active - adjust max_active of a pwq
 * @pwq: target pool_workqueue
 * @max_active: new max_active value.
 *
 * Set @pwq->max_active to @max_active and activate delayed works if
 * increased.
 *
 * CONTEXT:
 * spin_lock_irq(pool->lock).
 */
static void pwq_set_max_active(struct pool_workqueue *pwq, int max_active)
{
	pwq->max_active = max_active;

	while (!list_empty(&pwq->delayed_works) &&
	       pwq->nr_active < pwq->max_active)
		pwq_activate_first_delayed(pwq);
}

/**
 * workqueue_set_max_active - adjust max_active of a workqueue
 * @wq: target workqueue
 * @max_active: new max_active value.
 *
 * Set max_active of @wq to @max_active.
 *
 * CONTEXT:
 * Don't call from IRQ context.
 */
void workqueue_set_max_active(struct workqueue_struct *wq, int max_active)
{
	struct pool_workqueue *pwq;

	max_active = wq_clamp_max_active(max_active, wq->flags, wq->name);

	spin_lock_irq(&workqueue_lock);

	wq->saved_max_active = max_active;

	for_each_pwq(pwq, wq) {
		struct worker_pool *pool = pwq->pool;

		spin_lock(&pool->lock);

		if (!(wq->flags & WQ_FREEZABLE) ||
		    !(pool->flags & POOL_FREEZING))
			pwq_set_max_active(pwq, max_active);

		spin_unlock(&pool->lock);
	}

	spin_unlock_irq(&workqueue_lock);
}
EXPORT_SYMBOL_GPL(workqueue_set_max_active);

/**
 * workqueue_congested - test whether a workqueue is congested
 * @cpu: CPU in question
 * @wq: target workqueue
 *
 * Test whether @wq's cpu workqueue for @cpu is congested.  There is
 * no synchronization around this function and the test result is
 * unreliable and only useful as advisory hints or for debugging.
 *
 * RETURNS:
 * %true if congested, %false otherwise.
 */
bool workqueue_congested(int cpu, struct workqueue_struct *wq)
{
	struct pool_workqueue *pwq;
	bool ret;

	preempt_disable();

	if (!(wq->flags & WQ_UNBOUND))
		pwq = per_cpu_ptr(wq->cpu_pwqs, cpu);
	else
		pwq = first_pwq(wq);

	ret = !list_empty(&pwq->delayed_works);
	preempt_enable();

	return ret;
}
EXPORT_SYMBOL_GPL(workqueue_congested);

/**
 * work_busy - test whether a work is currently pending or running
 * @work: the work to be tested
 *
 * Test whether @work is currently pending or running.  There is no
 * synchronization around this function and the test result is
 * unreliable and only useful as advisory hints or for debugging.
 *
 * RETURNS:
 * OR'd bitmask of WORK_BUSY_* bits.
 */
unsigned int work_busy(struct work_struct *work)
{
	struct worker_pool *pool;
	unsigned long flags;
	unsigned int ret = 0;

	if (work_pending(work))
		ret |= WORK_BUSY_PENDING;

	local_irq_save(flags);
	pool = get_work_pool(work);
	if (pool) {
		spin_lock(&pool->lock);
		if (find_worker_executing_work(pool, work))
			ret |= WORK_BUSY_RUNNING;
		spin_unlock(&pool->lock);
	}
	local_irq_restore(flags);

	return ret;
}
EXPORT_SYMBOL_GPL(work_busy);

/*
 * CPU hotplug.
 *
 * There are two challenges in supporting CPU hotplug.  Firstly, there
 * are a lot of assumptions on strong associations among work, pwq and
 * pool which make migrating pending and scheduled works very
 * difficult to implement without impacting hot paths.  Secondly,
 * worker pools serve mix of short, long and very long running works making
 * blocked draining impractical.
 *
 * This is solved by allowing the pools to be disassociated from the CPU
 * running as an unbound one and allowing it to be reattached later if the
 * cpu comes back online.
 */

static void wq_unbind_fn(struct work_struct *work)
{
	int cpu = smp_processor_id();
	struct worker_pool *pool;
	struct worker *worker;
	int i;

	for_each_cpu_worker_pool(pool, cpu) {
		WARN_ON_ONCE(cpu != smp_processor_id());

		mutex_lock(&pool->assoc_mutex);
		spin_lock_irq(&pool->lock);

		/*
		 * We've claimed all manager positions.  Make all workers
		 * unbound and set DISASSOCIATED.  Before this, all workers
		 * except for the ones which are still executing works from
		 * before the last CPU down must be on the cpu.  After
		 * this, they may become diasporas.
		 */
		list_for_each_entry(worker, &pool->idle_list, entry)
			worker->flags |= WORKER_UNBOUND;

		for_each_busy_worker(worker, i, pool)
			worker->flags |= WORKER_UNBOUND;

		pool->flags |= POOL_DISASSOCIATED;

		spin_unlock_irq(&pool->lock);
		mutex_unlock(&pool->assoc_mutex);
	}

	/*
	 * Call schedule() so that we cross rq->lock and thus can guarantee
	 * sched callbacks see the %WORKER_UNBOUND flag.  This is necessary
	 * as scheduler callbacks may be invoked from other cpus.
	 */
	schedule();

	/*
	 * Sched callbacks are disabled now.  Zap nr_running.  After this,
	 * nr_running stays zero and need_more_worker() and keep_working()
	 * are always true as long as the worklist is not empty.  Pools on
	 * @cpu now behave as unbound (in terms of concurrency management)
	 * pools which are served by workers tied to the CPU.
	 *
	 * On return from this function, the current worker would trigger
	 * unbound chain execution of pending work items if other workers
	 * didn't already.
	 */
	for_each_cpu_worker_pool(pool, cpu)
		atomic_set(&pool->nr_running, 0);
}

/*
 * Workqueues should be brought up before normal priority CPU notifiers.
 * This will be registered high priority CPU notifier.
 */
static int __cpuinit workqueue_cpu_up_callback(struct notifier_block *nfb,
					       unsigned long action,
					       void *hcpu)
{
	int cpu = (unsigned long)hcpu;
	struct worker_pool *pool;

	switch (action & ~CPU_TASKS_FROZEN) {
	case CPU_UP_PREPARE:
		for_each_cpu_worker_pool(pool, cpu) {
			struct worker *worker;

			if (pool->nr_workers)
				continue;

			worker = create_worker(pool);
			if (!worker)
				return NOTIFY_BAD;

			spin_lock_irq(&pool->lock);
			start_worker(worker);
			spin_unlock_irq(&pool->lock);
		}
		break;

	case CPU_DOWN_FAILED:
	case CPU_ONLINE:
		for_each_cpu_worker_pool(pool, cpu) {
			mutex_lock(&pool->assoc_mutex);
			spin_lock_irq(&pool->lock);

			pool->flags &= ~POOL_DISASSOCIATED;
			rebind_workers(pool);

			spin_unlock_irq(&pool->lock);
			mutex_unlock(&pool->assoc_mutex);
		}
		break;
	}
	return NOTIFY_OK;
}

/*
 * Workqueues should be brought down after normal priority CPU notifiers.
 * This will be registered as low priority CPU notifier.
 */
static int __cpuinit workqueue_cpu_down_callback(struct notifier_block *nfb,
						 unsigned long action,
						 void *hcpu)
{
	int cpu = (unsigned long)hcpu;
	struct work_struct unbind_work;

	switch (action & ~CPU_TASKS_FROZEN) {
	case CPU_DOWN_PREPARE:
		/* unbinding should happen on the local CPU */
		INIT_WORK_ONSTACK(&unbind_work, wq_unbind_fn);
		queue_work_on(cpu, system_highpri_wq, &unbind_work);
		flush_work(&unbind_work);
		break;
	}
	return NOTIFY_OK;
}

#ifdef CONFIG_SMP

struct work_for_cpu {
	struct work_struct work;
	long (*fn)(void *);
	void *arg;
	long ret;
};

static void work_for_cpu_fn(struct work_struct *work)
{
	struct work_for_cpu *wfc = container_of(work, struct work_for_cpu, work);

	wfc->ret = wfc->fn(wfc->arg);
}

/**
 * work_on_cpu - run a function in user context on a particular cpu
 * @cpu: the cpu to run on
 * @fn: the function to run
 * @arg: the function arg
 *
 * This will return the value @fn returns.
 * It is up to the caller to ensure that the cpu doesn't go offline.
 * The caller must not hold any locks which would prevent @fn from completing.
 */
long work_on_cpu(int cpu, long (*fn)(void *), void *arg)
{
	struct work_for_cpu wfc = { .fn = fn, .arg = arg };

	INIT_WORK_ONSTACK(&wfc.work, work_for_cpu_fn);
	schedule_work_on(cpu, &wfc.work);
	flush_work(&wfc.work);
	return wfc.ret;
}
EXPORT_SYMBOL_GPL(work_on_cpu);
#endif /* CONFIG_SMP */

#ifdef CONFIG_FREEZER

/**
 * freeze_workqueues_begin - begin freezing workqueues
 *
 * Start freezing workqueues.  After this function returns, all freezable
 * workqueues will queue new works to their frozen_works list instead of
 * pool->worklist.
 *
 * CONTEXT:
 * Grabs and releases workqueue_lock and pool->lock's.
 */
void freeze_workqueues_begin(void)
{
	struct worker_pool *pool;
	struct workqueue_struct *wq;
	struct pool_workqueue *pwq;
	int id;

	spin_lock_irq(&workqueue_lock);

	WARN_ON_ONCE(workqueue_freezing);
	workqueue_freezing = true;

	/* set FREEZING */
	for_each_pool(pool, id) {
		spin_lock(&pool->lock);
		WARN_ON_ONCE(pool->flags & POOL_FREEZING);
		pool->flags |= POOL_FREEZING;
		spin_unlock(&pool->lock);
	}

	/* suppress further executions by setting max_active to zero */
	list_for_each_entry(wq, &workqueues, list) {
		if (!(wq->flags & WQ_FREEZABLE))
			continue;

		for_each_pwq(pwq, wq) {
			spin_lock(&pwq->pool->lock);
			pwq->max_active = 0;
			spin_unlock(&pwq->pool->lock);
		}
	}

	spin_unlock_irq(&workqueue_lock);
}

/**
 * freeze_workqueues_busy - are freezable workqueues still busy?
 *
 * Check whether freezing is complete.  This function must be called
 * between freeze_workqueues_begin() and thaw_workqueues().
 *
 * CONTEXT:
 * Grabs and releases workqueue_lock.
 *
 * RETURNS:
 * %true if some freezable workqueues are still busy.  %false if freezing
 * is complete.
 */
bool freeze_workqueues_busy(void)
{
	bool busy = false;
	struct workqueue_struct *wq;
	struct pool_workqueue *pwq;

	spin_lock_irq(&workqueue_lock);

	WARN_ON_ONCE(!workqueue_freezing);

	list_for_each_entry(wq, &workqueues, list) {
		if (!(wq->flags & WQ_FREEZABLE))
			continue;
		/*
		 * nr_active is monotonically decreasing.  It's safe
		 * to peek without lock.
		 */
		for_each_pwq(pwq, wq) {
			WARN_ON_ONCE(pwq->nr_active < 0);
			if (pwq->nr_active) {
				busy = true;
				goto out_unlock;
			}
		}
	}
out_unlock:
	spin_unlock_irq(&workqueue_lock);
	return busy;
}

/**
 * thaw_workqueues - thaw workqueues
 *
 * Thaw workqueues.  Normal queueing is restored and all collected
 * frozen works are transferred to their respective pool worklists.
 *
 * CONTEXT:
 * Grabs and releases workqueue_lock and pool->lock's.
 */
void thaw_workqueues(void)
{
	struct workqueue_struct *wq;
	struct pool_workqueue *pwq;
	struct worker_pool *pool;
	int id;

	spin_lock_irq(&workqueue_lock);

	if (!workqueue_freezing)
		goto out_unlock;

	/* clear FREEZING */
	for_each_pool(pool, id) {
		spin_lock(&pool->lock);
		WARN_ON_ONCE(!(pool->flags & POOL_FREEZING));
		pool->flags &= ~POOL_FREEZING;
		spin_unlock(&pool->lock);
	}

	/* restore max_active and repopulate worklist */
	list_for_each_entry(wq, &workqueues, list) {
		if (!(wq->flags & WQ_FREEZABLE))
			continue;

		for_each_pwq(pwq, wq) {
			spin_lock(&pwq->pool->lock);
			pwq_set_max_active(pwq, wq->saved_max_active);
			spin_unlock(&pwq->pool->lock);
		}
	}

	/* kick workers */
	for_each_pool(pool, id) {
		spin_lock(&pool->lock);
		wake_up_worker(pool);
		spin_unlock(&pool->lock);
	}

	workqueue_freezing = false;
out_unlock:
	spin_unlock_irq(&workqueue_lock);
}
#endif /* CONFIG_FREEZER */

static int __init init_workqueues(void)
{
	int std_nice[NR_STD_WORKER_POOLS] = { 0, HIGHPRI_NICE_LEVEL };
	int i, cpu;

	/* make sure we have enough bits for OFFQ pool ID */
	BUILD_BUG_ON((1LU << (BITS_PER_LONG - WORK_OFFQ_POOL_SHIFT)) <
		     WORK_CPU_END * NR_STD_WORKER_POOLS);

	WARN_ON(__alignof__(struct pool_workqueue) < __alignof__(long long));

	pwq_cache = KMEM_CACHE(pool_workqueue, SLAB_PANIC);

	cpu_notifier(workqueue_cpu_up_callback, CPU_PRI_WORKQUEUE_UP);
	hotcpu_notifier(workqueue_cpu_down_callback, CPU_PRI_WORKQUEUE_DOWN);

	/* initialize CPU pools */
	for_each_possible_cpu(cpu) {
		struct worker_pool *pool;

		i = 0;
		for_each_cpu_worker_pool(pool, cpu) {
			BUG_ON(init_worker_pool(pool));
			pool->cpu = cpu;
			cpumask_copy(pool->attrs->cpumask, cpumask_of(cpu));
			pool->attrs->nice = std_nice[i++];

			/* alloc pool ID */
			BUG_ON(worker_pool_assign_id(pool));
		}
	}

	/* create the initial worker */
	for_each_online_cpu(cpu) {
		struct worker_pool *pool;

		for_each_cpu_worker_pool(pool, cpu) {
			struct worker *worker;

			pool->flags &= ~POOL_DISASSOCIATED;

			worker = create_worker(pool);
			BUG_ON(!worker);
			spin_lock_irq(&pool->lock);
			start_worker(worker);
			spin_unlock_irq(&pool->lock);
		}
	}

	/* create default unbound wq attrs */
	for (i = 0; i < NR_STD_WORKER_POOLS; i++) {
		struct workqueue_attrs *attrs;

		BUG_ON(!(attrs = alloc_workqueue_attrs(GFP_KERNEL)));

		attrs->nice = std_nice[i];
		cpumask_setall(attrs->cpumask);

		unbound_std_wq_attrs[i] = attrs;
	}

	system_wq = alloc_workqueue("events", 0, 0);
	system_highpri_wq = alloc_workqueue("events_highpri", WQ_HIGHPRI, 0);
	system_long_wq = alloc_workqueue("events_long", 0, 0);
	system_unbound_wq = alloc_workqueue("events_unbound", WQ_UNBOUND,
					    WQ_UNBOUND_MAX_ACTIVE);
	system_freezable_wq = alloc_workqueue("events_freezable",
					      WQ_FREEZABLE, 0);
	BUG_ON(!system_wq || !system_highpri_wq || !system_long_wq ||
	       !system_unbound_wq || !system_freezable_wq);
	return 0;
}
early_initcall(init_workqueues);
