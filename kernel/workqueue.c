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

#include "workqueue_sched.h"

enum {
	/*
	 * global_cwq flags
	 *
	 * A bound gcwq is either associated or disassociated with its CPU.
	 * While associated (!DISASSOCIATED), all workers are bound to the
	 * CPU and none has %WORKER_UNBOUND set and concurrency management
	 * is in effect.
	 *
	 * While DISASSOCIATED, the cpu may be offline and all workers have
	 * %WORKER_UNBOUND set and concurrency management disabled, and may
	 * be executing on any CPU.  The gcwq behaves as an unbound one.
	 *
	 * Note that DISASSOCIATED can be flipped only while holding
	 * managership of all pools on the gcwq to avoid changing binding
	 * state while create_worker() is in progress.
	 */
	GCWQ_DISASSOCIATED	= 1 << 0,	/* cpu can't serve workers */
	GCWQ_FREEZING		= 1 << 1,	/* freeze in progress */

	/* pool flags */
	POOL_MANAGE_WORKERS	= 1 << 0,	/* need to manage workers */
	POOL_MANAGING_WORKERS   = 1 << 1,       /* managing workers */

	/* worker flags */
	WORKER_STARTED		= 1 << 0,	/* started */
	WORKER_DIE		= 1 << 1,	/* die die die */
	WORKER_IDLE		= 1 << 2,	/* is idle */
	WORKER_PREP		= 1 << 3,	/* preparing to run works */
	WORKER_REBIND		= 1 << 5,	/* mom is home, come back */
	WORKER_CPU_INTENSIVE	= 1 << 6,	/* cpu intensive */
	WORKER_UNBOUND		= 1 << 7,	/* worker is unbound */

	WORKER_NOT_RUNNING	= WORKER_PREP | WORKER_REBIND | WORKER_UNBOUND |
				  WORKER_CPU_INTENSIVE,

	NR_WORKER_POOLS		= 2,		/* # worker pools per gcwq */

	BUSY_WORKER_HASH_ORDER	= 6,		/* 64 pointers */
	BUSY_WORKER_HASH_SIZE	= 1 << BUSY_WORKER_HASH_ORDER,
	BUSY_WORKER_HASH_MASK	= BUSY_WORKER_HASH_SIZE - 1,

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
 * L: gcwq->lock protected.  Access with gcwq->lock held.
 *
 * X: During normal operation, modification requires gcwq->lock and
 *    should be done only from local cpu.  Either disabling preemption
 *    on local cpu or grabbing gcwq->lock is enough for read access.
 *    If GCWQ_DISASSOCIATED is set, it's identical to L.
 *
 * F: wq->flush_mutex protected.
 *
 * W: workqueue_lock protected.
 */

struct global_cwq;
struct worker_pool;
struct idle_rebind;

/*
 * The poor guys doing the actual heavy lifting.  All on-duty workers
 * are either serving the manager role, on idle list or on busy hash.
 */
struct worker {
	/* on idle list while idle, on busy hash table while busy */
	union {
		struct list_head	entry;	/* L: while idle */
		struct hlist_node	hentry;	/* L: while busy */
	};

	struct work_struct	*current_work;	/* L: work being processed */
	struct cpu_workqueue_struct *current_cwq; /* L: current_work's cwq */
	struct list_head	scheduled;	/* L: scheduled works */
	struct task_struct	*task;		/* I: worker task */
	struct worker_pool	*pool;		/* I: the associated pool */
	/* 64 bytes boundary on 64bit, 32 on 32bit */
	unsigned long		last_active;	/* L: last active timestamp */
	unsigned int		flags;		/* X: flags */
	int			id;		/* I: worker id */

	/* for rebinding worker to CPU */
	struct idle_rebind	*idle_rebind;	/* L: for idle worker */
	struct work_struct	rebind_work;	/* L: for busy worker */
};

struct worker_pool {
	struct global_cwq	*gcwq;		/* I: the owning gcwq */
	unsigned int		flags;		/* X: flags */

	struct list_head	worklist;	/* L: list of pending works */
	int			nr_workers;	/* L: total number of workers */
	int			nr_idle;	/* L: currently idle ones */

	struct list_head	idle_list;	/* X: list of idle workers */
	struct timer_list	idle_timer;	/* L: worker idle timeout */
	struct timer_list	mayday_timer;	/* L: SOS timer for workers */

	struct mutex		manager_mutex;	/* mutex manager should hold */
	struct ida		worker_ida;	/* L: for worker IDs */
};

/*
 * Global per-cpu workqueue.  There's one and only one for each cpu
 * and all works are queued and processed here regardless of their
 * target workqueues.
 */
struct global_cwq {
	spinlock_t		lock;		/* the gcwq lock */
	unsigned int		cpu;		/* I: the associated cpu */
	unsigned int		flags;		/* L: GCWQ_* flags */

	/* workers are chained either in busy_hash or pool idle_list */
	struct hlist_head	busy_hash[BUSY_WORKER_HASH_SIZE];
						/* L: hash of busy workers */

	struct worker_pool	pools[NR_WORKER_POOLS];
						/* normal and highpri pools */

	wait_queue_head_t	rebind_hold;	/* rebind hold wait */
} ____cacheline_aligned_in_smp;

/*
 * The per-CPU workqueue.  The lower WORK_STRUCT_FLAG_BITS of
 * work_struct->data are used for flags and thus cwqs need to be
 * aligned at two's power of the number of flag bits.
 */
struct cpu_workqueue_struct {
	struct worker_pool	*pool;		/* I: the associated pool */
	struct workqueue_struct *wq;		/* I: the owning workqueue */
	int			work_color;	/* L: current color */
	int			flush_color;	/* L: flushing color */
	int			nr_in_flight[WORK_NR_COLORS];
						/* L: nr of in_flight works */
	int			nr_active;	/* L: nr of active works */
	int			max_active;	/* L: max active works */
	struct list_head	delayed_works;	/* L: delayed works */
};

/*
 * Structure used to wait for workqueue flush.
 */
struct wq_flusher {
	struct list_head	list;		/* F: list of flushers */
	int			flush_color;	/* F: flush color waiting for */
	struct completion	done;		/* flush completion */
};

/*
 * All cpumasks are assumed to be always set on UP and thus can't be
 * used to determine whether there's something to be done.
 */
#ifdef CONFIG_SMP
typedef cpumask_var_t mayday_mask_t;
#define mayday_test_and_set_cpu(cpu, mask)	\
	cpumask_test_and_set_cpu((cpu), (mask))
#define mayday_clear_cpu(cpu, mask)		cpumask_clear_cpu((cpu), (mask))
#define for_each_mayday_cpu(cpu, mask)		for_each_cpu((cpu), (mask))
#define alloc_mayday_mask(maskp, gfp)		zalloc_cpumask_var((maskp), (gfp))
#define free_mayday_mask(mask)			free_cpumask_var((mask))
#else
typedef unsigned long mayday_mask_t;
#define mayday_test_and_set_cpu(cpu, mask)	test_and_set_bit(0, &(mask))
#define mayday_clear_cpu(cpu, mask)		clear_bit(0, &(mask))
#define for_each_mayday_cpu(cpu, mask)		if ((cpu) = 0, (mask))
#define alloc_mayday_mask(maskp, gfp)		true
#define free_mayday_mask(mask)			do { } while (0)
#endif

/*
 * The externally visible workqueue abstraction is an array of
 * per-CPU workqueues:
 */
struct workqueue_struct {
	unsigned int		flags;		/* W: WQ_* flags */
	union {
		struct cpu_workqueue_struct __percpu	*pcpu;
		struct cpu_workqueue_struct		*single;
		unsigned long				v;
	} cpu_wq;				/* I: cwq's */
	struct list_head	list;		/* W: list of all workqueues */

	struct mutex		flush_mutex;	/* protects wq flushing */
	int			work_color;	/* F: current work color */
	int			flush_color;	/* F: current flush color */
	atomic_t		nr_cwqs_to_flush; /* flush in progress */
	struct wq_flusher	*first_flusher;	/* F: first flusher */
	struct list_head	flusher_queue;	/* F: flush waiters */
	struct list_head	flusher_overflow; /* F: flush overflow list */

	mayday_mask_t		mayday_mask;	/* cpus requesting rescue */
	struct worker		*rescuer;	/* I: rescue worker */

	int			nr_drainers;	/* W: drain in progress */
	int			saved_max_active; /* W: saved cwq max_active */
#ifdef CONFIG_LOCKDEP
	struct lockdep_map	lockdep_map;
#endif
	char			name[];		/* I: workqueue name */
};

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

#define for_each_worker_pool(pool, gcwq)				\
	for ((pool) = &(gcwq)->pools[0];				\
	     (pool) < &(gcwq)->pools[NR_WORKER_POOLS]; (pool)++)

#define for_each_busy_worker(worker, i, pos, gcwq)			\
	for (i = 0; i < BUSY_WORKER_HASH_SIZE; i++)			\
		hlist_for_each_entry(worker, pos, &gcwq->busy_hash[i], hentry)

static inline int __next_gcwq_cpu(int cpu, const struct cpumask *mask,
				  unsigned int sw)
{
	if (cpu < nr_cpu_ids) {
		if (sw & 1) {
			cpu = cpumask_next(cpu, mask);
			if (cpu < nr_cpu_ids)
				return cpu;
		}
		if (sw & 2)
			return WORK_CPU_UNBOUND;
	}
	return WORK_CPU_NONE;
}

static inline int __next_wq_cpu(int cpu, const struct cpumask *mask,
				struct workqueue_struct *wq)
{
	return __next_gcwq_cpu(cpu, mask, !(wq->flags & WQ_UNBOUND) ? 1 : 2);
}

/*
 * CPU iterators
 *
 * An extra gcwq is defined for an invalid cpu number
 * (WORK_CPU_UNBOUND) to host workqueues which are not bound to any
 * specific CPU.  The following iterators are similar to
 * for_each_*_cpu() iterators but also considers the unbound gcwq.
 *
 * for_each_gcwq_cpu()		: possible CPUs + WORK_CPU_UNBOUND
 * for_each_online_gcwq_cpu()	: online CPUs + WORK_CPU_UNBOUND
 * for_each_cwq_cpu()		: possible CPUs for bound workqueues,
 *				  WORK_CPU_UNBOUND for unbound workqueues
 */
#define for_each_gcwq_cpu(cpu)						\
	for ((cpu) = __next_gcwq_cpu(-1, cpu_possible_mask, 3);		\
	     (cpu) < WORK_CPU_NONE;					\
	     (cpu) = __next_gcwq_cpu((cpu), cpu_possible_mask, 3))

#define for_each_online_gcwq_cpu(cpu)					\
	for ((cpu) = __next_gcwq_cpu(-1, cpu_online_mask, 3);		\
	     (cpu) < WORK_CPU_NONE;					\
	     (cpu) = __next_gcwq_cpu((cpu), cpu_online_mask, 3))

#define for_each_cwq_cpu(cpu, wq)					\
	for ((cpu) = __next_wq_cpu(-1, cpu_possible_mask, (wq));	\
	     (cpu) < WORK_CPU_NONE;					\
	     (cpu) = __next_wq_cpu((cpu), cpu_possible_mask, (wq)))

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
 * The almighty global cpu workqueues.  nr_running is the only field
 * which is expected to be used frequently by other cpus via
 * try_to_wake_up().  Put it in a separate cacheline.
 */
static DEFINE_PER_CPU(struct global_cwq, global_cwq);
static DEFINE_PER_CPU_SHARED_ALIGNED(atomic_t, pool_nr_running[NR_WORKER_POOLS]);

/*
 * Global cpu workqueue and nr_running counter for unbound gcwq.  The
 * gcwq is always online, has GCWQ_DISASSOCIATED set, and all its
 * workers have WORKER_UNBOUND set.
 */
static struct global_cwq unbound_global_cwq;
static atomic_t unbound_pool_nr_running[NR_WORKER_POOLS] = {
	[0 ... NR_WORKER_POOLS - 1]	= ATOMIC_INIT(0),	/* always 0 */
};

static int worker_thread(void *__worker);

static int worker_pool_pri(struct worker_pool *pool)
{
	return pool - pool->gcwq->pools;
}

static struct global_cwq *get_gcwq(unsigned int cpu)
{
	if (cpu != WORK_CPU_UNBOUND)
		return &per_cpu(global_cwq, cpu);
	else
		return &unbound_global_cwq;
}

static atomic_t *get_pool_nr_running(struct worker_pool *pool)
{
	int cpu = pool->gcwq->cpu;
	int idx = worker_pool_pri(pool);

	if (cpu != WORK_CPU_UNBOUND)
		return &per_cpu(pool_nr_running, cpu)[idx];
	else
		return &unbound_pool_nr_running[idx];
}

static struct cpu_workqueue_struct *get_cwq(unsigned int cpu,
					    struct workqueue_struct *wq)
{
	if (!(wq->flags & WQ_UNBOUND)) {
		if (likely(cpu < nr_cpu_ids))
			return per_cpu_ptr(wq->cpu_wq.pcpu, cpu);
	} else if (likely(cpu == WORK_CPU_UNBOUND))
		return wq->cpu_wq.single;
	return NULL;
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
 * While queued, %WORK_STRUCT_CWQ is set and non flag bits of a work's data
 * contain the pointer to the queued cwq.  Once execution starts, the flag
 * is cleared and the high bits contain OFFQ flags and CPU number.
 *
 * set_work_cwq(), set_work_cpu_and_clear_pending(), mark_work_canceling()
 * and clear_work_data() can be used to set the cwq, cpu or clear
 * work->data.  These functions should only be called while the work is
 * owned - ie. while the PENDING bit is set.
 *
 * get_work_[g]cwq() can be used to obtain the gcwq or cwq corresponding to
 * a work.  gcwq is available once the work has been queued anywhere after
 * initialization until it is sync canceled.  cwq is available only while
 * the work item is queued.
 *
 * %WORK_OFFQ_CANCELING is used to mark a work item which is being
 * canceled.  While being canceled, a work item may have its PENDING set
 * but stay off timer and worklist for arbitrarily long and nobody should
 * try to steal the PENDING bit.
 */
static inline void set_work_data(struct work_struct *work, unsigned long data,
				 unsigned long flags)
{
	BUG_ON(!work_pending(work));
	atomic_long_set(&work->data, data | flags | work_static(work));
}

static void set_work_cwq(struct work_struct *work,
			 struct cpu_workqueue_struct *cwq,
			 unsigned long extra_flags)
{
	set_work_data(work, (unsigned long)cwq,
		      WORK_STRUCT_PENDING | WORK_STRUCT_CWQ | extra_flags);
}

static void set_work_cpu_and_clear_pending(struct work_struct *work,
					   unsigned int cpu)
{
	/*
	 * The following wmb is paired with the implied mb in
	 * test_and_set_bit(PENDING) and ensures all updates to @work made
	 * here are visible to and precede any updates by the next PENDING
	 * owner.
	 */
	smp_wmb();
	set_work_data(work, (unsigned long)cpu << WORK_OFFQ_CPU_SHIFT, 0);
}

static void clear_work_data(struct work_struct *work)
{
	smp_wmb();	/* see set_work_cpu_and_clear_pending() */
	set_work_data(work, WORK_STRUCT_NO_CPU, 0);
}

static struct cpu_workqueue_struct *get_work_cwq(struct work_struct *work)
{
	unsigned long data = atomic_long_read(&work->data);

	if (data & WORK_STRUCT_CWQ)
		return (void *)(data & WORK_STRUCT_WQ_DATA_MASK);
	else
		return NULL;
}

static struct global_cwq *get_work_gcwq(struct work_struct *work)
{
	unsigned long data = atomic_long_read(&work->data);
	unsigned int cpu;

	if (data & WORK_STRUCT_CWQ)
		return ((struct cpu_workqueue_struct *)
			(data & WORK_STRUCT_WQ_DATA_MASK))->pool->gcwq;

	cpu = data >> WORK_OFFQ_CPU_SHIFT;
	if (cpu == WORK_CPU_NONE)
		return NULL;

	BUG_ON(cpu >= nr_cpu_ids && cpu != WORK_CPU_UNBOUND);
	return get_gcwq(cpu);
}

static void mark_work_canceling(struct work_struct *work)
{
	struct global_cwq *gcwq = get_work_gcwq(work);
	unsigned long cpu = gcwq ? gcwq->cpu : WORK_CPU_NONE;

	set_work_data(work, (cpu << WORK_OFFQ_CPU_SHIFT) | WORK_OFFQ_CANCELING,
		      WORK_STRUCT_PENDING);
}

static bool work_is_canceling(struct work_struct *work)
{
	unsigned long data = atomic_long_read(&work->data);

	return !(data & WORK_STRUCT_CWQ) && (data & WORK_OFFQ_CANCELING);
}

/*
 * Policy functions.  These define the policies on how the global worker
 * pools are managed.  Unless noted otherwise, these functions assume that
 * they're being called with gcwq->lock held.
 */

static bool __need_more_worker(struct worker_pool *pool)
{
	return !atomic_read(get_pool_nr_running(pool));
}

/*
 * Need to wake up a worker?  Called from anything but currently
 * running workers.
 *
 * Note that, because unbound workers never contribute to nr_running, this
 * function will always return %true for unbound gcwq as long as the
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
	atomic_t *nr_running = get_pool_nr_running(pool);

	return !list_empty(&pool->worklist) && atomic_read(nr_running) <= 1;
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
	bool managing = pool->flags & POOL_MANAGING_WORKERS;
	int nr_idle = pool->nr_idle + managing; /* manager is considered idle */
	int nr_busy = pool->nr_workers - nr_idle;

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
 * spin_lock_irq(gcwq->lock).
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
void wq_worker_waking_up(struct task_struct *task, unsigned int cpu)
{
	struct worker *worker = kthread_data(task);

	if (!(worker->flags & WORKER_NOT_RUNNING))
		atomic_inc(get_pool_nr_running(worker->pool));
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
struct task_struct *wq_worker_sleeping(struct task_struct *task,
				       unsigned int cpu)
{
	struct worker *worker = kthread_data(task), *to_wakeup = NULL;
	struct worker_pool *pool = worker->pool;
	atomic_t *nr_running = get_pool_nr_running(pool);

	if (worker->flags & WORKER_NOT_RUNNING)
		return NULL;

	/* this can only happen on the local cpu */
	BUG_ON(cpu != raw_smp_processor_id());

	/*
	 * The counterpart of the following dec_and_test, implied mb,
	 * worklist not empty test sequence is in insert_work().
	 * Please read comment there.
	 *
	 * NOT_RUNNING is clear.  This means that we're bound to and
	 * running on the local cpu w/ rq lock held and preemption
	 * disabled, which in turn means that none else could be
	 * manipulating idle_list, so dereferencing idle_list without gcwq
	 * lock is safe.
	 */
	if (atomic_dec_and_test(nr_running) && !list_empty(&pool->worklist))
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
 * spin_lock_irq(gcwq->lock)
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
		atomic_t *nr_running = get_pool_nr_running(pool);

		if (wakeup) {
			if (atomic_dec_and_test(nr_running) &&
			    !list_empty(&pool->worklist))
				wake_up_worker(pool);
		} else
			atomic_dec(nr_running);
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
 * spin_lock_irq(gcwq->lock)
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
			atomic_inc(get_pool_nr_running(pool));
}

/**
 * busy_worker_head - return the busy hash head for a work
 * @gcwq: gcwq of interest
 * @work: work to be hashed
 *
 * Return hash head of @gcwq for @work.
 *
 * CONTEXT:
 * spin_lock_irq(gcwq->lock).
 *
 * RETURNS:
 * Pointer to the hash head.
 */
static struct hlist_head *busy_worker_head(struct global_cwq *gcwq,
					   struct work_struct *work)
{
	const int base_shift = ilog2(sizeof(struct work_struct));
	unsigned long v = (unsigned long)work;

	/* simple shift and fold hash, do we need something better? */
	v >>= base_shift;
	v += v >> BUSY_WORKER_HASH_ORDER;
	v &= BUSY_WORKER_HASH_MASK;

	return &gcwq->busy_hash[v];
}

/**
 * __find_worker_executing_work - find worker which is executing a work
 * @gcwq: gcwq of interest
 * @bwh: hash head as returned by busy_worker_head()
 * @work: work to find worker for
 *
 * Find a worker which is executing @work on @gcwq.  @bwh should be
 * the hash head obtained by calling busy_worker_head() with the same
 * work.
 *
 * CONTEXT:
 * spin_lock_irq(gcwq->lock).
 *
 * RETURNS:
 * Pointer to worker which is executing @work if found, NULL
 * otherwise.
 */
static struct worker *__find_worker_executing_work(struct global_cwq *gcwq,
						   struct hlist_head *bwh,
						   struct work_struct *work)
{
	struct worker *worker;
	struct hlist_node *tmp;

	hlist_for_each_entry(worker, tmp, bwh, hentry)
		if (worker->current_work == work)
			return worker;
	return NULL;
}

/**
 * find_worker_executing_work - find worker which is executing a work
 * @gcwq: gcwq of interest
 * @work: work to find worker for
 *
 * Find a worker which is executing @work on @gcwq.  This function is
 * identical to __find_worker_executing_work() except that this
 * function calculates @bwh itself.
 *
 * CONTEXT:
 * spin_lock_irq(gcwq->lock).
 *
 * RETURNS:
 * Pointer to worker which is executing @work if found, NULL
 * otherwise.
 */
static struct worker *find_worker_executing_work(struct global_cwq *gcwq,
						 struct work_struct *work)
{
	return __find_worker_executing_work(gcwq, busy_worker_head(gcwq, work),
					    work);
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
 * spin_lock_irq(gcwq->lock).
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

static void cwq_activate_first_delayed(struct cpu_workqueue_struct *cwq)
{
	struct work_struct *work = list_first_entry(&cwq->delayed_works,
						    struct work_struct, entry);

	trace_workqueue_activate_work(work);
	move_linked_works(work, &cwq->pool->worklist, NULL);
	__clear_bit(WORK_STRUCT_DELAYED_BIT, work_data_bits(work));
	cwq->nr_active++;
}

/**
 * cwq_dec_nr_in_flight - decrement cwq's nr_in_flight
 * @cwq: cwq of interest
 * @color: color of work which left the queue
 * @delayed: for a delayed work
 *
 * A work either has completed or is removed from pending queue,
 * decrement nr_in_flight of its cwq and handle workqueue flushing.
 *
 * CONTEXT:
 * spin_lock_irq(gcwq->lock).
 */
static void cwq_dec_nr_in_flight(struct cpu_workqueue_struct *cwq, int color,
				 bool delayed)
{
	/* ignore uncolored works */
	if (color == WORK_NO_COLOR)
		return;

	cwq->nr_in_flight[color]--;

	if (!delayed) {
		cwq->nr_active--;
		if (!list_empty(&cwq->delayed_works)) {
			/* one down, submit a delayed one */
			if (cwq->nr_active < cwq->max_active)
				cwq_activate_first_delayed(cwq);
		}
	}

	/* is flush in progress and are we at the flushing tip? */
	if (likely(cwq->flush_color != color))
		return;

	/* are there still in-flight works? */
	if (cwq->nr_in_flight[color])
		return;

	/* this cwq is done, clear flush_color */
	cwq->flush_color = -1;

	/*
	 * If this was the last cwq, wake up the first flusher.  It
	 * will handle the rest.
	 */
	if (atomic_dec_and_test(&cwq->wq->nr_cwqs_to_flush))
		complete(&cwq->wq->first_flusher->done);
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
	struct global_cwq *gcwq;

	WARN_ON_ONCE(in_irq());

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
	gcwq = get_work_gcwq(work);
	if (!gcwq)
		goto fail;

	spin_lock(&gcwq->lock);
	if (!list_empty(&work->entry)) {
		/*
		 * This work is queued, but perhaps we locked the wrong gcwq.
		 * In that case we must see the new value after rmb(), see
		 * insert_work()->wmb().
		 */
		smp_rmb();
		if (gcwq == get_work_gcwq(work)) {
			debug_work_deactivate(work);
			list_del_init(&work->entry);
			cwq_dec_nr_in_flight(get_work_cwq(work),
				get_work_color(work),
				*work_data_bits(work) & WORK_STRUCT_DELAYED);

			spin_unlock(&gcwq->lock);
			return 1;
		}
	}
	spin_unlock(&gcwq->lock);
fail:
	local_irq_restore(*flags);
	if (work_is_canceling(work))
		return -ENOENT;
	cpu_relax();
	return -EAGAIN;
}

/**
 * insert_work - insert a work into gcwq
 * @cwq: cwq @work belongs to
 * @work: work to insert
 * @head: insertion point
 * @extra_flags: extra WORK_STRUCT_* flags to set
 *
 * Insert @work which belongs to @cwq into @gcwq after @head.
 * @extra_flags is or'd to work_struct flags.
 *
 * CONTEXT:
 * spin_lock_irq(gcwq->lock).
 */
static void insert_work(struct cpu_workqueue_struct *cwq,
			struct work_struct *work, struct list_head *head,
			unsigned int extra_flags)
{
	struct worker_pool *pool = cwq->pool;

	/* we own @work, set data and link */
	set_work_cwq(work, cwq, extra_flags);

	/*
	 * Ensure that we get the right work->data if we see the
	 * result of list_add() below, see try_to_grab_pending().
	 */
	smp_wmb();

	list_add_tail(&work->entry, head);

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
 * same workqueue.  This is rather expensive and should only be used from
 * cold paths.
 */
static bool is_chained_work(struct workqueue_struct *wq)
{
	unsigned long flags;
	unsigned int cpu;

	for_each_gcwq_cpu(cpu) {
		struct global_cwq *gcwq = get_gcwq(cpu);
		struct worker *worker;
		struct hlist_node *pos;
		int i;

		spin_lock_irqsave(&gcwq->lock, flags);
		for_each_busy_worker(worker, i, pos, gcwq) {
			if (worker->task != current)
				continue;
			spin_unlock_irqrestore(&gcwq->lock, flags);
			/*
			 * I'm @worker, no locking necessary.  See if @work
			 * is headed to the same workqueue.
			 */
			return worker->current_cwq->wq == wq;
		}
		spin_unlock_irqrestore(&gcwq->lock, flags);
	}
	return false;
}

static void __queue_work(unsigned int cpu, struct workqueue_struct *wq,
			 struct work_struct *work)
{
	struct global_cwq *gcwq;
	struct cpu_workqueue_struct *cwq;
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

	/* determine gcwq to use */
	if (!(wq->flags & WQ_UNBOUND)) {
		struct global_cwq *last_gcwq;

		if (cpu == WORK_CPU_UNBOUND)
			cpu = raw_smp_processor_id();

		/*
		 * It's multi cpu.  If @work was previously on a different
		 * cpu, it might still be running there, in which case the
		 * work needs to be queued on that cpu to guarantee
		 * non-reentrancy.
		 */
		gcwq = get_gcwq(cpu);
		last_gcwq = get_work_gcwq(work);

		if (last_gcwq && last_gcwq != gcwq) {
			struct worker *worker;

			spin_lock(&last_gcwq->lock);

			worker = find_worker_executing_work(last_gcwq, work);

			if (worker && worker->current_cwq->wq == wq)
				gcwq = last_gcwq;
			else {
				/* meh... not running there, queue here */
				spin_unlock(&last_gcwq->lock);
				spin_lock(&gcwq->lock);
			}
		} else {
			spin_lock(&gcwq->lock);
		}
	} else {
		gcwq = get_gcwq(WORK_CPU_UNBOUND);
		spin_lock(&gcwq->lock);
	}

	/* gcwq determined, get cwq and queue */
	cwq = get_cwq(gcwq->cpu, wq);
	trace_workqueue_queue_work(req_cpu, cwq, work);

	if (WARN_ON(!list_empty(&work->entry))) {
		spin_unlock(&gcwq->lock);
		return;
	}

	cwq->nr_in_flight[cwq->work_color]++;
	work_flags = work_color_to_flags(cwq->work_color);

	if (likely(cwq->nr_active < cwq->max_active)) {
		trace_workqueue_activate_work(work);
		cwq->nr_active++;
		worklist = &cwq->pool->worklist;
	} else {
		work_flags |= WORK_STRUCT_DELAYED;
		worklist = &cwq->delayed_works;
	}

	insert_work(cwq, work, worklist, work_flags);

	spin_unlock(&gcwq->lock);
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
	struct cpu_workqueue_struct *cwq = get_work_cwq(&dwork->work);

	/* should have been called from irqsafe timer with irq already off */
	__queue_work(dwork->cpu, cwq->wq, &dwork->work);
}
EXPORT_SYMBOL_GPL(delayed_work_timer_fn);

static void __queue_delayed_work(int cpu, struct workqueue_struct *wq,
				struct delayed_work *dwork, unsigned long delay)
{
	struct timer_list *timer = &dwork->timer;
	struct work_struct *work = &dwork->work;
	unsigned int lcpu;

	WARN_ON_ONCE(timer->function != delayed_work_timer_fn ||
		     timer->data != (unsigned long)dwork);
	BUG_ON(timer_pending(timer));
	BUG_ON(!list_empty(&work->entry));

	timer_stats_timer_set_start_info(&dwork->timer);

	/*
	 * This stores cwq for the moment, for the timer_fn.  Note that the
	 * work's gcwq is preserved to allow reentrance detection for
	 * delayed works.
	 */
	if (!(wq->flags & WQ_UNBOUND)) {
		struct global_cwq *gcwq = get_work_gcwq(work);

		/*
		 * If we cannot get the last gcwq from @work directly,
		 * select the last CPU such that it avoids unnecessarily
		 * triggering non-reentrancy check in __queue_work().
		 */
		lcpu = cpu;
		if (gcwq)
			lcpu = gcwq->cpu;
		if (lcpu == WORK_CPU_UNBOUND)
			lcpu = raw_smp_processor_id();
	} else {
		lcpu = WORK_CPU_UNBOUND;
	}

	set_work_cwq(work, get_cwq(lcpu, wq), 0);

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

	if (!delay)
		return queue_work_on(cpu, wq, &dwork->work);

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
 * spin_lock_irq(gcwq->lock).
 */
static void worker_enter_idle(struct worker *worker)
{
	struct worker_pool *pool = worker->pool;
	struct global_cwq *gcwq = pool->gcwq;

	BUG_ON(worker->flags & WORKER_IDLE);
	BUG_ON(!list_empty(&worker->entry) &&
	       (worker->hentry.next || worker->hentry.pprev));

	/* can't use worker_set_flags(), also called from start_worker() */
	worker->flags |= WORKER_IDLE;
	pool->nr_idle++;
	worker->last_active = jiffies;

	/* idle_list is LIFO */
	list_add(&worker->entry, &pool->idle_list);

	if (too_many_workers(pool) && !timer_pending(&pool->idle_timer))
		mod_timer(&pool->idle_timer, jiffies + IDLE_WORKER_TIMEOUT);

	/*
	 * Sanity check nr_running.  Because gcwq_unbind_fn() releases
	 * gcwq->lock between setting %WORKER_UNBOUND and zapping
	 * nr_running, the warning may trigger spuriously.  Check iff
	 * unbind is not in progress.
	 */
	WARN_ON_ONCE(!(gcwq->flags & GCWQ_DISASSOCIATED) &&
		     pool->nr_workers == pool->nr_idle &&
		     atomic_read(get_pool_nr_running(pool)));
}

/**
 * worker_leave_idle - leave idle state
 * @worker: worker which is leaving idle state
 *
 * @worker is leaving idle state.  Update stats.
 *
 * LOCKING:
 * spin_lock_irq(gcwq->lock).
 */
static void worker_leave_idle(struct worker *worker)
{
	struct worker_pool *pool = worker->pool;

	BUG_ON(!(worker->flags & WORKER_IDLE));
	worker_clr_flags(worker, WORKER_IDLE);
	pool->nr_idle--;
	list_del_init(&worker->entry);
}

/**
 * worker_maybe_bind_and_lock - bind worker to its cpu if possible and lock gcwq
 * @worker: self
 *
 * Works which are scheduled while the cpu is online must at least be
 * scheduled to a worker which is bound to the cpu so that if they are
 * flushed from cpu callbacks while cpu is going down, they are
 * guaranteed to execute on the cpu.
 *
 * This function is to be used by rogue workers and rescuers to bind
 * themselves to the target cpu and may race with cpu going down or
 * coming online.  kthread_bind() can't be used because it may put the
 * worker to already dead cpu and set_cpus_allowed_ptr() can't be used
 * verbatim as it's best effort and blocking and gcwq may be
 * [dis]associated in the meantime.
 *
 * This function tries set_cpus_allowed() and locks gcwq and verifies the
 * binding against %GCWQ_DISASSOCIATED which is set during
 * %CPU_DOWN_PREPARE and cleared during %CPU_ONLINE, so if the worker
 * enters idle state or fetches works without dropping lock, it can
 * guarantee the scheduling requirement described in the first paragraph.
 *
 * CONTEXT:
 * Might sleep.  Called without any lock but returns with gcwq->lock
 * held.
 *
 * RETURNS:
 * %true if the associated gcwq is online (@worker is successfully
 * bound), %false if offline.
 */
static bool worker_maybe_bind_and_lock(struct worker *worker)
__acquires(&gcwq->lock)
{
	struct global_cwq *gcwq = worker->pool->gcwq;
	struct task_struct *task = worker->task;

	while (true) {
		/*
		 * The following call may fail, succeed or succeed
		 * without actually migrating the task to the cpu if
		 * it races with cpu hotunplug operation.  Verify
		 * against GCWQ_DISASSOCIATED.
		 */
		if (!(gcwq->flags & GCWQ_DISASSOCIATED))
			set_cpus_allowed_ptr(task, get_cpu_mask(gcwq->cpu));

		spin_lock_irq(&gcwq->lock);
		if (gcwq->flags & GCWQ_DISASSOCIATED)
			return false;
		if (task_cpu(task) == gcwq->cpu &&
		    cpumask_equal(&current->cpus_allowed,
				  get_cpu_mask(gcwq->cpu)))
			return true;
		spin_unlock_irq(&gcwq->lock);

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

struct idle_rebind {
	int			cnt;		/* # workers to be rebound */
	struct completion	done;		/* all workers rebound */
};

/*
 * Rebind an idle @worker to its CPU.  During CPU onlining, this has to
 * happen synchronously for idle workers.  worker_thread() will test
 * %WORKER_REBIND before leaving idle and call this function.
 */
static void idle_worker_rebind(struct worker *worker)
{
	struct global_cwq *gcwq = worker->pool->gcwq;

	/* CPU must be online at this point */
	WARN_ON(!worker_maybe_bind_and_lock(worker));
	if (!--worker->idle_rebind->cnt)
		complete(&worker->idle_rebind->done);
	spin_unlock_irq(&worker->pool->gcwq->lock);

	/* we did our part, wait for rebind_workers() to finish up */
	wait_event(gcwq->rebind_hold, !(worker->flags & WORKER_REBIND));

	/*
	 * rebind_workers() shouldn't finish until all workers passed the
	 * above WORKER_REBIND wait.  Tell it when done.
	 */
	spin_lock_irq(&worker->pool->gcwq->lock);
	if (!--worker->idle_rebind->cnt)
		complete(&worker->idle_rebind->done);
	spin_unlock_irq(&worker->pool->gcwq->lock);
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
	struct global_cwq *gcwq = worker->pool->gcwq;

	worker_maybe_bind_and_lock(worker);

	/*
	 * %WORKER_REBIND must be cleared even if the above binding failed;
	 * otherwise, we may confuse the next CPU_UP cycle or oops / get
	 * stuck by calling idle_worker_rebind() prematurely.  If CPU went
	 * down again inbetween, %WORKER_UNBOUND would be set, so clearing
	 * %WORKER_REBIND is always safe.
	 */
	worker_clr_flags(worker, WORKER_REBIND);

	spin_unlock_irq(&gcwq->lock);
}

/**
 * rebind_workers - rebind all workers of a gcwq to the associated CPU
 * @gcwq: gcwq of interest
 *
 * @gcwq->cpu is coming online.  Rebind all workers to the CPU.  Rebinding
 * is different for idle and busy ones.
 *
 * The idle ones should be rebound synchronously and idle rebinding should
 * be complete before any worker starts executing work items with
 * concurrency management enabled; otherwise, scheduler may oops trying to
 * wake up non-local idle worker from wq_worker_sleeping().
 *
 * This is achieved by repeatedly requesting rebinding until all idle
 * workers are known to have been rebound under @gcwq->lock and holding all
 * idle workers from becoming busy until idle rebinding is complete.
 *
 * Once idle workers are rebound, busy workers can be rebound as they
 * finish executing their current work items.  Queueing the rebind work at
 * the head of their scheduled lists is enough.  Note that nr_running will
 * be properbly bumped as busy workers rebind.
 *
 * On return, all workers are guaranteed to either be bound or have rebind
 * work item scheduled.
 */
static void rebind_workers(struct global_cwq *gcwq)
	__releases(&gcwq->lock) __acquires(&gcwq->lock)
{
	struct idle_rebind idle_rebind;
	struct worker_pool *pool;
	struct worker *worker;
	struct hlist_node *pos;
	int i;

	lockdep_assert_held(&gcwq->lock);

	for_each_worker_pool(pool, gcwq)
		lockdep_assert_held(&pool->manager_mutex);

	/*
	 * Rebind idle workers.  Interlocked both ways.  We wait for
	 * workers to rebind via @idle_rebind.done.  Workers will wait for
	 * us to finish up by watching %WORKER_REBIND.
	 */
	init_completion(&idle_rebind.done);
retry:
	idle_rebind.cnt = 1;
	INIT_COMPLETION(idle_rebind.done);

	/* set REBIND and kick idle ones, we'll wait for these later */
	for_each_worker_pool(pool, gcwq) {
		list_for_each_entry(worker, &pool->idle_list, entry) {
			unsigned long worker_flags = worker->flags;

			if (worker->flags & WORKER_REBIND)
				continue;

			/* morph UNBOUND to REBIND atomically */
			worker_flags &= ~WORKER_UNBOUND;
			worker_flags |= WORKER_REBIND;
			ACCESS_ONCE(worker->flags) = worker_flags;

			idle_rebind.cnt++;
			worker->idle_rebind = &idle_rebind;

			/* worker_thread() will call idle_worker_rebind() */
			wake_up_process(worker->task);
		}
	}

	if (--idle_rebind.cnt) {
		spin_unlock_irq(&gcwq->lock);
		wait_for_completion(&idle_rebind.done);
		spin_lock_irq(&gcwq->lock);
		/* busy ones might have become idle while waiting, retry */
		goto retry;
	}

	/* all idle workers are rebound, rebind busy workers */
	for_each_busy_worker(worker, i, pos, gcwq) {
		unsigned long worker_flags = worker->flags;
		struct work_struct *rebind_work = &worker->rebind_work;
		struct workqueue_struct *wq;

		/* morph UNBOUND to REBIND atomically */
		worker_flags &= ~WORKER_UNBOUND;
		worker_flags |= WORKER_REBIND;
		ACCESS_ONCE(worker->flags) = worker_flags;

		if (test_and_set_bit(WORK_STRUCT_PENDING_BIT,
				     work_data_bits(rebind_work)))
			continue;

		debug_work_activate(rebind_work);

		/*
		 * wq doesn't really matter but let's keep @worker->pool
		 * and @cwq->pool consistent for sanity.
		 */
		if (worker_pool_pri(worker->pool))
			wq = system_highpri_wq;
		else
			wq = system_wq;

		insert_work(get_cwq(gcwq->cpu, wq), rebind_work,
			worker->scheduled.next,
			work_color_to_flags(WORK_NO_COLOR));
	}

	/*
	 * All idle workers are rebound and waiting for %WORKER_REBIND to
	 * be cleared inside idle_worker_rebind().  Clear and release.
	 * Clearing %WORKER_REBIND from this foreign context is safe
	 * because these workers are still guaranteed to be idle.
	 *
	 * We need to make sure all idle workers passed WORKER_REBIND wait
	 * in idle_worker_rebind() before returning; otherwise, workers can
	 * get stuck at the wait if hotplug cycle repeats.
	 */
	idle_rebind.cnt = 1;
	INIT_COMPLETION(idle_rebind.done);

	for_each_worker_pool(pool, gcwq) {
		list_for_each_entry(worker, &pool->idle_list, entry) {
			worker->flags &= ~WORKER_REBIND;
			idle_rebind.cnt++;
		}
	}

	wake_up_all(&gcwq->rebind_hold);

	if (--idle_rebind.cnt) {
		spin_unlock_irq(&gcwq->lock);
		wait_for_completion(&idle_rebind.done);
		spin_lock_irq(&gcwq->lock);
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
	struct global_cwq *gcwq = pool->gcwq;
	const char *pri = worker_pool_pri(pool) ? "H" : "";
	struct worker *worker = NULL;
	int id = -1;

	spin_lock_irq(&gcwq->lock);
	while (ida_get_new(&pool->worker_ida, &id)) {
		spin_unlock_irq(&gcwq->lock);
		if (!ida_pre_get(&pool->worker_ida, GFP_KERNEL))
			goto fail;
		spin_lock_irq(&gcwq->lock);
	}
	spin_unlock_irq(&gcwq->lock);

	worker = alloc_worker();
	if (!worker)
		goto fail;

	worker->pool = pool;
	worker->id = id;

	if (gcwq->cpu != WORK_CPU_UNBOUND)
		worker->task = kthread_create_on_node(worker_thread,
					worker, cpu_to_node(gcwq->cpu),
					"kworker/%u:%d%s", gcwq->cpu, id, pri);
	else
		worker->task = kthread_create(worker_thread, worker,
					      "kworker/u:%d%s", id, pri);
	if (IS_ERR(worker->task))
		goto fail;

	if (worker_pool_pri(pool))
		set_user_nice(worker->task, HIGHPRI_NICE_LEVEL);

	/*
	 * Determine CPU binding of the new worker depending on
	 * %GCWQ_DISASSOCIATED.  The caller is responsible for ensuring the
	 * flag remains stable across this function.  See the comments
	 * above the flag definition for details.
	 *
	 * As an unbound worker may later become a regular one if CPU comes
	 * online, make sure every worker has %PF_THREAD_BOUND set.
	 */
	if (!(gcwq->flags & GCWQ_DISASSOCIATED)) {
		kthread_bind(worker->task, gcwq->cpu);
	} else {
		worker->task->flags |= PF_THREAD_BOUND;
		worker->flags |= WORKER_UNBOUND;
	}

	return worker;
fail:
	if (id >= 0) {
		spin_lock_irq(&gcwq->lock);
		ida_remove(&pool->worker_ida, id);
		spin_unlock_irq(&gcwq->lock);
	}
	kfree(worker);
	return NULL;
}

/**
 * start_worker - start a newly created worker
 * @worker: worker to start
 *
 * Make the gcwq aware of @worker and start it.
 *
 * CONTEXT:
 * spin_lock_irq(gcwq->lock).
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
 * Destroy @worker and adjust @gcwq stats accordingly.
 *
 * CONTEXT:
 * spin_lock_irq(gcwq->lock) which is released and regrabbed.
 */
static void destroy_worker(struct worker *worker)
{
	struct worker_pool *pool = worker->pool;
	struct global_cwq *gcwq = pool->gcwq;
	int id = worker->id;

	/* sanity check frenzy */
	BUG_ON(worker->current_work);
	BUG_ON(!list_empty(&worker->scheduled));

	if (worker->flags & WORKER_STARTED)
		pool->nr_workers--;
	if (worker->flags & WORKER_IDLE)
		pool->nr_idle--;

	list_del_init(&worker->entry);
	worker->flags |= WORKER_DIE;

	spin_unlock_irq(&gcwq->lock);

	kthread_stop(worker->task);
	kfree(worker);

	spin_lock_irq(&gcwq->lock);
	ida_remove(&pool->worker_ida, id);
}

static void idle_worker_timeout(unsigned long __pool)
{
	struct worker_pool *pool = (void *)__pool;
	struct global_cwq *gcwq = pool->gcwq;

	spin_lock_irq(&gcwq->lock);

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

	spin_unlock_irq(&gcwq->lock);
}

static bool send_mayday(struct work_struct *work)
{
	struct cpu_workqueue_struct *cwq = get_work_cwq(work);
	struct workqueue_struct *wq = cwq->wq;
	unsigned int cpu;

	if (!(wq->flags & WQ_RESCUER))
		return false;

	/* mayday mayday mayday */
	cpu = cwq->pool->gcwq->cpu;
	/* WORK_CPU_UNBOUND can't be set in cpumask, use cpu 0 instead */
	if (cpu == WORK_CPU_UNBOUND)
		cpu = 0;
	if (!mayday_test_and_set_cpu(cpu, wq->mayday_mask))
		wake_up_process(wq->rescuer->task);
	return true;
}

static void gcwq_mayday_timeout(unsigned long __pool)
{
	struct worker_pool *pool = (void *)__pool;
	struct global_cwq *gcwq = pool->gcwq;
	struct work_struct *work;

	spin_lock_irq(&gcwq->lock);

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

	spin_unlock_irq(&gcwq->lock);

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
 * spin_lock_irq(gcwq->lock) which may be released and regrabbed
 * multiple times.  Does GFP_KERNEL allocations.  Called only from
 * manager.
 *
 * RETURNS:
 * false if no action was taken and gcwq->lock stayed locked, true
 * otherwise.
 */
static bool maybe_create_worker(struct worker_pool *pool)
__releases(&gcwq->lock)
__acquires(&gcwq->lock)
{
	struct global_cwq *gcwq = pool->gcwq;

	if (!need_to_create_worker(pool))
		return false;
restart:
	spin_unlock_irq(&gcwq->lock);

	/* if we don't make progress in MAYDAY_INITIAL_TIMEOUT, call for help */
	mod_timer(&pool->mayday_timer, jiffies + MAYDAY_INITIAL_TIMEOUT);

	while (true) {
		struct worker *worker;

		worker = create_worker(pool);
		if (worker) {
			del_timer_sync(&pool->mayday_timer);
			spin_lock_irq(&gcwq->lock);
			start_worker(worker);
			BUG_ON(need_to_create_worker(pool));
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
	spin_lock_irq(&gcwq->lock);
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
 * spin_lock_irq(gcwq->lock) which may be released and regrabbed
 * multiple times.  Called only from manager.
 *
 * RETURNS:
 * false if no action was taken and gcwq->lock stayed locked, true
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
 * Assume the manager role and manage gcwq worker pool @worker belongs
 * to.  At any given time, there can be only zero or one manager per
 * gcwq.  The exclusion is handled automatically by this function.
 *
 * The caller can safely start processing works on false return.  On
 * true return, it's guaranteed that need_to_create_worker() is false
 * and may_start_working() is true.
 *
 * CONTEXT:
 * spin_lock_irq(gcwq->lock) which may be released and regrabbed
 * multiple times.  Does GFP_KERNEL allocations.
 *
 * RETURNS:
 * false if no action was taken and gcwq->lock stayed locked, true if
 * some action was taken.
 */
static bool manage_workers(struct worker *worker)
{
	struct worker_pool *pool = worker->pool;
	bool ret = false;

	if (pool->flags & POOL_MANAGING_WORKERS)
		return ret;

	pool->flags |= POOL_MANAGING_WORKERS;

	/*
	 * To simplify both worker management and CPU hotplug, hold off
	 * management while hotplug is in progress.  CPU hotplug path can't
	 * grab %POOL_MANAGING_WORKERS to achieve this because that can
	 * lead to idle worker depletion (all become busy thinking someone
	 * else is managing) which in turn can result in deadlock under
	 * extreme circumstances.  Use @pool->manager_mutex to synchronize
	 * manager against CPU hotplug.
	 *
	 * manager_mutex would always be free unless CPU hotplug is in
	 * progress.  trylock first without dropping @gcwq->lock.
	 */
	if (unlikely(!mutex_trylock(&pool->manager_mutex))) {
		spin_unlock_irq(&pool->gcwq->lock);
		mutex_lock(&pool->manager_mutex);
		/*
		 * CPU hotplug could have happened while we were waiting
		 * for manager_mutex.  Hotplug itself can't handle us
		 * because manager isn't either on idle or busy list, and
		 * @gcwq's state and ours could have deviated.
		 *
		 * As hotplug is now excluded via manager_mutex, we can
		 * simply try to bind.  It will succeed or fail depending
		 * on @gcwq's current state.  Try it and adjust
		 * %WORKER_UNBOUND accordingly.
		 */
		if (worker_maybe_bind_and_lock(worker))
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

	pool->flags &= ~POOL_MANAGING_WORKERS;
	mutex_unlock(&pool->manager_mutex);
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
 * spin_lock_irq(gcwq->lock) which is released and regrabbed.
 */
static void process_one_work(struct worker *worker, struct work_struct *work)
__releases(&gcwq->lock)
__acquires(&gcwq->lock)
{
	struct cpu_workqueue_struct *cwq = get_work_cwq(work);
	struct worker_pool *pool = worker->pool;
	struct global_cwq *gcwq = pool->gcwq;
	struct hlist_head *bwh = busy_worker_head(gcwq, work);
	bool cpu_intensive = cwq->wq->flags & WQ_CPU_INTENSIVE;
	work_func_t f = work->func;
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
	 * unbound or a disassociated gcwq.
	 */
	WARN_ON_ONCE(!(worker->flags & (WORKER_UNBOUND | WORKER_REBIND)) &&
		     !(gcwq->flags & GCWQ_DISASSOCIATED) &&
		     raw_smp_processor_id() != gcwq->cpu);

	/*
	 * A single work shouldn't be executed concurrently by
	 * multiple workers on a single cpu.  Check whether anyone is
	 * already processing the work.  If so, defer the work to the
	 * currently executing one.
	 */
	collision = __find_worker_executing_work(gcwq, bwh, work);
	if (unlikely(collision)) {
		move_linked_works(work, &collision->scheduled, NULL);
		return;
	}

	/* claim and dequeue */
	debug_work_deactivate(work);
	hlist_add_head(&worker->hentry, bwh);
	worker->current_work = work;
	worker->current_cwq = cwq;
	work_color = get_work_color(work);

	list_del_init(&work->entry);

	/*
	 * CPU intensive works don't participate in concurrency
	 * management.  They're the scheduler's responsibility.
	 */
	if (unlikely(cpu_intensive))
		worker_set_flags(worker, WORKER_CPU_INTENSIVE, true);

	/*
	 * Unbound gcwq isn't concurrency managed and work items should be
	 * executed ASAP.  Wake up another worker if necessary.
	 */
	if ((worker->flags & WORKER_UNBOUND) && need_more_worker(pool))
		wake_up_worker(pool);

	/*
	 * Record the last CPU and clear PENDING which should be the last
	 * update to @work.  Also, do this inside @gcwq->lock so that
	 * PENDING and queued state changes happen together while IRQ is
	 * disabled.
	 */
	set_work_cpu_and_clear_pending(work, gcwq->cpu);

	spin_unlock_irq(&gcwq->lock);

	lock_map_acquire_read(&cwq->wq->lockdep_map);
	lock_map_acquire(&lockdep_map);
	trace_workqueue_execute_start(work);
	f(work);
	/*
	 * While we must be careful to not use "work" after this, the trace
	 * point will only record its address.
	 */
	trace_workqueue_execute_end(work);
	lock_map_release(&lockdep_map);
	lock_map_release(&cwq->wq->lockdep_map);

	if (unlikely(in_atomic() || lockdep_depth(current) > 0)) {
		pr_err("BUG: workqueue leaked lock or atomic: %s/0x%08x/%d\n"
		       "     last function: %pf\n",
		       current->comm, preempt_count(), task_pid_nr(current), f);
		debug_show_held_locks(current);
		dump_stack();
	}

	spin_lock_irq(&gcwq->lock);

	/* clear cpu intensive status */
	if (unlikely(cpu_intensive))
		worker_clr_flags(worker, WORKER_CPU_INTENSIVE);

	/* we're done with it, release */
	hlist_del_init(&worker->hentry);
	worker->current_work = NULL;
	worker->current_cwq = NULL;
	cwq_dec_nr_in_flight(cwq, work_color, false);
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
 * spin_lock_irq(gcwq->lock) which may be released and regrabbed
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
 * The gcwq worker thread function.  There's a single dynamic pool of
 * these per each cpu.  These workers process all works regardless of
 * their specific target workqueue.  The only exception is works which
 * belong to workqueues with a rescuer which will be explained in
 * rescuer_thread().
 */
static int worker_thread(void *__worker)
{
	struct worker *worker = __worker;
	struct worker_pool *pool = worker->pool;
	struct global_cwq *gcwq = pool->gcwq;

	/* tell the scheduler that this is a workqueue worker */
	worker->task->flags |= PF_WQ_WORKER;
woke_up:
	spin_lock_irq(&gcwq->lock);

	/*
	 * DIE can be set only while idle and REBIND set while busy has
	 * @worker->rebind_work scheduled.  Checking here is enough.
	 */
	if (unlikely(worker->flags & (WORKER_REBIND | WORKER_DIE))) {
		spin_unlock_irq(&gcwq->lock);

		if (worker->flags & WORKER_DIE) {
			worker->task->flags &= ~PF_WQ_WORKER;
			return 0;
		}

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
	BUG_ON(!list_empty(&worker->scheduled));

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
	 * gcwq->lock is held and there's no work to process and no
	 * need to manage, sleep.  Workers are woken up only while
	 * holding gcwq->lock or from local cpu, so setting the
	 * current state before releasing gcwq->lock is enough to
	 * prevent losing any event.
	 */
	worker_enter_idle(worker);
	__set_current_state(TASK_INTERRUPTIBLE);
	spin_unlock_irq(&gcwq->lock);
	schedule();
	goto woke_up;
}

/**
 * rescuer_thread - the rescuer thread function
 * @__wq: the associated workqueue
 *
 * Workqueue rescuer thread function.  There's one rescuer for each
 * workqueue which has WQ_RESCUER set.
 *
 * Regular work processing on a gcwq may block trying to create a new
 * worker which uses GFP_KERNEL allocation which has slight chance of
 * developing into deadlock if some works currently on the same queue
 * need to be processed to satisfy the GFP_KERNEL allocation.  This is
 * the problem rescuer solves.
 *
 * When such condition is possible, the gcwq summons rescuers of all
 * workqueues which have works queued on the gcwq and let them process
 * those works so that forward progress can be guaranteed.
 *
 * This should happen rarely.
 */
static int rescuer_thread(void *__wq)
{
	struct workqueue_struct *wq = __wq;
	struct worker *rescuer = wq->rescuer;
	struct list_head *scheduled = &rescuer->scheduled;
	bool is_unbound = wq->flags & WQ_UNBOUND;
	unsigned int cpu;

	set_user_nice(current, RESCUER_NICE_LEVEL);
repeat:
	set_current_state(TASK_INTERRUPTIBLE);

	if (kthread_should_stop())
		return 0;

	/*
	 * See whether any cpu is asking for help.  Unbounded
	 * workqueues use cpu 0 in mayday_mask for CPU_UNBOUND.
	 */
	for_each_mayday_cpu(cpu, wq->mayday_mask) {
		unsigned int tcpu = is_unbound ? WORK_CPU_UNBOUND : cpu;
		struct cpu_workqueue_struct *cwq = get_cwq(tcpu, wq);
		struct worker_pool *pool = cwq->pool;
		struct global_cwq *gcwq = pool->gcwq;
		struct work_struct *work, *n;

		__set_current_state(TASK_RUNNING);
		mayday_clear_cpu(cpu, wq->mayday_mask);

		/* migrate to the target cpu if possible */
		rescuer->pool = pool;
		worker_maybe_bind_and_lock(rescuer);

		/*
		 * Slurp in all works issued via this workqueue and
		 * process'em.
		 */
		BUG_ON(!list_empty(&rescuer->scheduled));
		list_for_each_entry_safe(work, n, &pool->worklist, entry)
			if (get_work_cwq(work) == cwq)
				move_linked_works(work, scheduled, &n);

		process_scheduled_works(rescuer);

		/*
		 * Leave this gcwq.  If keep_working() is %true, notify a
		 * regular worker; otherwise, we end up with 0 concurrency
		 * and stalling the execution.
		 */
		if (keep_working(pool))
			wake_up_worker(pool);

		spin_unlock_irq(&gcwq->lock);
	}

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
 * @cwq: cwq to insert barrier into
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
 * underneath us, so we can't reliably determine cwq from @target.
 *
 * CONTEXT:
 * spin_lock_irq(gcwq->lock).
 */
static void insert_wq_barrier(struct cpu_workqueue_struct *cwq,
			      struct wq_barrier *barr,
			      struct work_struct *target, struct worker *worker)
{
	struct list_head *head;
	unsigned int linked = 0;

	/*
	 * debugobject calls are safe here even with gcwq->lock locked
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
	insert_work(cwq, &barr->work, head,
		    work_color_to_flags(WORK_NO_COLOR) | linked);
}

/**
 * flush_workqueue_prep_cwqs - prepare cwqs for workqueue flushing
 * @wq: workqueue being flushed
 * @flush_color: new flush color, < 0 for no-op
 * @work_color: new work color, < 0 for no-op
 *
 * Prepare cwqs for workqueue flushing.
 *
 * If @flush_color is non-negative, flush_color on all cwqs should be
 * -1.  If no cwq has in-flight commands at the specified color, all
 * cwq->flush_color's stay at -1 and %false is returned.  If any cwq
 * has in flight commands, its cwq->flush_color is set to
 * @flush_color, @wq->nr_cwqs_to_flush is updated accordingly, cwq
 * wakeup logic is armed and %true is returned.
 *
 * The caller should have initialized @wq->first_flusher prior to
 * calling this function with non-negative @flush_color.  If
 * @flush_color is negative, no flush color update is done and %false
 * is returned.
 *
 * If @work_color is non-negative, all cwqs should have the same
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
static bool flush_workqueue_prep_cwqs(struct workqueue_struct *wq,
				      int flush_color, int work_color)
{
	bool wait = false;
	unsigned int cpu;

	if (flush_color >= 0) {
		BUG_ON(atomic_read(&wq->nr_cwqs_to_flush));
		atomic_set(&wq->nr_cwqs_to_flush, 1);
	}

	for_each_cwq_cpu(cpu, wq) {
		struct cpu_workqueue_struct *cwq = get_cwq(cpu, wq);
		struct global_cwq *gcwq = cwq->pool->gcwq;

		spin_lock_irq(&gcwq->lock);

		if (flush_color >= 0) {
			BUG_ON(cwq->flush_color != -1);

			if (cwq->nr_in_flight[flush_color]) {
				cwq->flush_color = flush_color;
				atomic_inc(&wq->nr_cwqs_to_flush);
				wait = true;
			}
		}

		if (work_color >= 0) {
			BUG_ON(work_color != work_next_color(cwq->work_color));
			cwq->work_color = work_color;
		}

		spin_unlock_irq(&gcwq->lock);
	}

	if (flush_color >= 0 && atomic_dec_and_test(&wq->nr_cwqs_to_flush))
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
		BUG_ON(!list_empty(&wq->flusher_overflow));
		this_flusher.flush_color = wq->work_color;
		wq->work_color = next_color;

		if (!wq->first_flusher) {
			/* no flush in progress, become the first flusher */
			BUG_ON(wq->flush_color != this_flusher.flush_color);

			wq->first_flusher = &this_flusher;

			if (!flush_workqueue_prep_cwqs(wq, wq->flush_color,
						       wq->work_color)) {
				/* nothing to flush, done */
				wq->flush_color = next_color;
				wq->first_flusher = NULL;
				goto out_unlock;
			}
		} else {
			/* wait in queue */
			BUG_ON(wq->flush_color == this_flusher.flush_color);
			list_add_tail(&this_flusher.list, &wq->flusher_queue);
			flush_workqueue_prep_cwqs(wq, -1, wq->work_color);
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

	BUG_ON(!list_empty(&this_flusher.list));
	BUG_ON(wq->flush_color != this_flusher.flush_color);

	while (true) {
		struct wq_flusher *next, *tmp;

		/* complete all the flushers sharing the current flush color */
		list_for_each_entry_safe(next, tmp, &wq->flusher_queue, list) {
			if (next->flush_color != wq->flush_color)
				break;
			list_del_init(&next->list);
			complete(&next->done);
		}

		BUG_ON(!list_empty(&wq->flusher_overflow) &&
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
			flush_workqueue_prep_cwqs(wq, -1, wq->work_color);
		}

		if (list_empty(&wq->flusher_queue)) {
			BUG_ON(wq->flush_color != wq->work_color);
			break;
		}

		/*
		 * Need to flush more colors.  Make the next flusher
		 * the new first flusher and arm cwqs.
		 */
		BUG_ON(wq->flush_color == wq->work_color);
		BUG_ON(wq->flush_color != next->flush_color);

		list_del_init(&next->list);
		wq->first_flusher = next;

		if (flush_workqueue_prep_cwqs(wq, wq->flush_color, -1))
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
	unsigned int cpu;

	/*
	 * __queue_work() needs to test whether there are drainers, is much
	 * hotter than drain_workqueue() and already looks at @wq->flags.
	 * Use WQ_DRAINING so that queue doesn't have to check nr_drainers.
	 */
	spin_lock(&workqueue_lock);
	if (!wq->nr_drainers++)
		wq->flags |= WQ_DRAINING;
	spin_unlock(&workqueue_lock);
reflush:
	flush_workqueue(wq);

	for_each_cwq_cpu(cpu, wq) {
		struct cpu_workqueue_struct *cwq = get_cwq(cpu, wq);
		bool drained;

		spin_lock_irq(&cwq->pool->gcwq->lock);
		drained = !cwq->nr_active && list_empty(&cwq->delayed_works);
		spin_unlock_irq(&cwq->pool->gcwq->lock);

		if (drained)
			continue;

		if (++flush_cnt == 10 ||
		    (flush_cnt % 100 == 0 && flush_cnt <= 1000))
			pr_warn("workqueue %s: flush on destruction isn't complete after %u tries\n",
				wq->name, flush_cnt);
		goto reflush;
	}

	spin_lock(&workqueue_lock);
	if (!--wq->nr_drainers)
		wq->flags &= ~WQ_DRAINING;
	spin_unlock(&workqueue_lock);
}
EXPORT_SYMBOL_GPL(drain_workqueue);

static bool start_flush_work(struct work_struct *work, struct wq_barrier *barr)
{
	struct worker *worker = NULL;
	struct global_cwq *gcwq;
	struct cpu_workqueue_struct *cwq;

	might_sleep();
	gcwq = get_work_gcwq(work);
	if (!gcwq)
		return false;

	spin_lock_irq(&gcwq->lock);
	if (!list_empty(&work->entry)) {
		/*
		 * See the comment near try_to_grab_pending()->smp_rmb().
		 * If it was re-queued to a different gcwq under us, we
		 * are not going to wait.
		 */
		smp_rmb();
		cwq = get_work_cwq(work);
		if (unlikely(!cwq || gcwq != cwq->pool->gcwq))
			goto already_gone;
	} else {
		worker = find_worker_executing_work(gcwq, work);
		if (!worker)
			goto already_gone;
		cwq = worker->current_cwq;
	}

	insert_wq_barrier(cwq, barr, work, worker);
	spin_unlock_irq(&gcwq->lock);

	/*
	 * If @max_active is 1 or rescuer is in use, flushing another work
	 * item on the same workqueue may lead to deadlock.  Make sure the
	 * flusher is not running on the same workqueue by verifying write
	 * access.
	 */
	if (cwq->wq->saved_max_active == 1 || cwq->wq->flags & WQ_RESCUER)
		lock_map_acquire(&cwq->wq->lockdep_map);
	else
		lock_map_acquire_read(&cwq->wq->lockdep_map);
	lock_map_release(&cwq->wq->lockdep_map);

	return true;
already_gone:
	spin_unlock_irq(&gcwq->lock);
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
		__queue_work(dwork->cpu,
			     get_work_cwq(&dwork->work)->wq, &dwork->work);
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

	set_work_cpu_and_clear_pending(&dwork->work, work_cpu(&dwork->work));
	local_irq_restore(flags);
	return true;
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

static int alloc_cwqs(struct workqueue_struct *wq)
{
	/*
	 * cwqs are forced aligned according to WORK_STRUCT_FLAG_BITS.
	 * Make sure that the alignment isn't lower than that of
	 * unsigned long long.
	 */
	const size_t size = sizeof(struct cpu_workqueue_struct);
	const size_t align = max_t(size_t, 1 << WORK_STRUCT_FLAG_BITS,
				   __alignof__(unsigned long long));

	if (!(wq->flags & WQ_UNBOUND))
		wq->cpu_wq.pcpu = __alloc_percpu(size, align);
	else {
		void *ptr;

		/*
		 * Allocate enough room to align cwq and put an extra
		 * pointer at the end pointing back to the originally
		 * allocated pointer which will be used for free.
		 */
		ptr = kzalloc(size + align + sizeof(void *), GFP_KERNEL);
		if (ptr) {
			wq->cpu_wq.single = PTR_ALIGN(ptr, align);
			*(void **)(wq->cpu_wq.single + 1) = ptr;
		}
	}

	/* just in case, make sure it's actually aligned */
	BUG_ON(!IS_ALIGNED(wq->cpu_wq.v, align));
	return wq->cpu_wq.v ? 0 : -ENOMEM;
}

static void free_cwqs(struct workqueue_struct *wq)
{
	if (!(wq->flags & WQ_UNBOUND))
		free_percpu(wq->cpu_wq.pcpu);
	else if (wq->cpu_wq.single) {
		/* the pointer to free is stored right after the cwq */
		kfree(*(void **)(wq->cpu_wq.single + 1));
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
	unsigned int cpu;
	size_t namelen;

	/* determine namelen, allocate wq and format name */
	va_start(args, lock_name);
	va_copy(args1, args);
	namelen = vsnprintf(NULL, 0, fmt, args) + 1;

	wq = kzalloc(sizeof(*wq) + namelen, GFP_KERNEL);
	if (!wq)
		goto err;

	vsnprintf(wq->name, namelen, fmt, args1);
	va_end(args);
	va_end(args1);

	/*
	 * Workqueues which may be used during memory reclaim should
	 * have a rescuer to guarantee forward progress.
	 */
	if (flags & WQ_MEM_RECLAIM)
		flags |= WQ_RESCUER;

	max_active = max_active ?: WQ_DFL_ACTIVE;
	max_active = wq_clamp_max_active(max_active, flags, wq->name);

	/* init wq */
	wq->flags = flags;
	wq->saved_max_active = max_active;
	mutex_init(&wq->flush_mutex);
	atomic_set(&wq->nr_cwqs_to_flush, 0);
	INIT_LIST_HEAD(&wq->flusher_queue);
	INIT_LIST_HEAD(&wq->flusher_overflow);

	lockdep_init_map(&wq->lockdep_map, lock_name, key, 0);
	INIT_LIST_HEAD(&wq->list);

	if (alloc_cwqs(wq) < 0)
		goto err;

	for_each_cwq_cpu(cpu, wq) {
		struct cpu_workqueue_struct *cwq = get_cwq(cpu, wq);
		struct global_cwq *gcwq = get_gcwq(cpu);
		int pool_idx = (bool)(flags & WQ_HIGHPRI);

		BUG_ON((unsigned long)cwq & WORK_STRUCT_FLAG_MASK);
		cwq->pool = &gcwq->pools[pool_idx];
		cwq->wq = wq;
		cwq->flush_color = -1;
		cwq->max_active = max_active;
		INIT_LIST_HEAD(&cwq->delayed_works);
	}

	if (flags & WQ_RESCUER) {
		struct worker *rescuer;

		if (!alloc_mayday_mask(&wq->mayday_mask, GFP_KERNEL))
			goto err;

		wq->rescuer = rescuer = alloc_worker();
		if (!rescuer)
			goto err;

		rescuer->task = kthread_create(rescuer_thread, wq, "%s",
					       wq->name);
		if (IS_ERR(rescuer->task))
			goto err;

		rescuer->task->flags |= PF_THREAD_BOUND;
		wake_up_process(rescuer->task);
	}

	/*
	 * workqueue_lock protects global freeze state and workqueues
	 * list.  Grab it, set max_active accordingly and add the new
	 * workqueue to workqueues list.
	 */
	spin_lock(&workqueue_lock);

	if (workqueue_freezing && wq->flags & WQ_FREEZABLE)
		for_each_cwq_cpu(cpu, wq)
			get_cwq(cpu, wq)->max_active = 0;

	list_add(&wq->list, &workqueues);

	spin_unlock(&workqueue_lock);

	return wq;
err:
	if (wq) {
		free_cwqs(wq);
		free_mayday_mask(wq->mayday_mask);
		kfree(wq->rescuer);
		kfree(wq);
	}
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
	unsigned int cpu;

	/* drain it before proceeding with destruction */
	drain_workqueue(wq);

	/*
	 * wq list is used to freeze wq, remove from list after
	 * flushing is complete in case freeze races us.
	 */
	spin_lock(&workqueue_lock);
	list_del(&wq->list);
	spin_unlock(&workqueue_lock);

	/* sanity check */
	for_each_cwq_cpu(cpu, wq) {
		struct cpu_workqueue_struct *cwq = get_cwq(cpu, wq);
		int i;

		for (i = 0; i < WORK_NR_COLORS; i++)
			BUG_ON(cwq->nr_in_flight[i]);
		BUG_ON(cwq->nr_active);
		BUG_ON(!list_empty(&cwq->delayed_works));
	}

	if (wq->flags & WQ_RESCUER) {
		kthread_stop(wq->rescuer->task);
		free_mayday_mask(wq->mayday_mask);
		kfree(wq->rescuer);
	}

	free_cwqs(wq);
	kfree(wq);
}
EXPORT_SYMBOL_GPL(destroy_workqueue);

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
	unsigned int cpu;

	max_active = wq_clamp_max_active(max_active, wq->flags, wq->name);

	spin_lock(&workqueue_lock);

	wq->saved_max_active = max_active;

	for_each_cwq_cpu(cpu, wq) {
		struct global_cwq *gcwq = get_gcwq(cpu);

		spin_lock_irq(&gcwq->lock);

		if (!(wq->flags & WQ_FREEZABLE) ||
		    !(gcwq->flags & GCWQ_FREEZING))
			get_cwq(gcwq->cpu, wq)->max_active = max_active;

		spin_unlock_irq(&gcwq->lock);
	}

	spin_unlock(&workqueue_lock);
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
bool workqueue_congested(unsigned int cpu, struct workqueue_struct *wq)
{
	struct cpu_workqueue_struct *cwq = get_cwq(cpu, wq);

	return !list_empty(&cwq->delayed_works);
}
EXPORT_SYMBOL_GPL(workqueue_congested);

/**
 * work_cpu - return the last known associated cpu for @work
 * @work: the work of interest
 *
 * RETURNS:
 * CPU number if @work was ever queued.  WORK_CPU_NONE otherwise.
 */
unsigned int work_cpu(struct work_struct *work)
{
	struct global_cwq *gcwq = get_work_gcwq(work);

	return gcwq ? gcwq->cpu : WORK_CPU_NONE;
}
EXPORT_SYMBOL_GPL(work_cpu);

/**
 * work_busy - test whether a work is currently pending or running
 * @work: the work to be tested
 *
 * Test whether @work is currently pending or running.  There is no
 * synchronization around this function and the test result is
 * unreliable and only useful as advisory hints or for debugging.
 * Especially for reentrant wqs, the pending state might hide the
 * running state.
 *
 * RETURNS:
 * OR'd bitmask of WORK_BUSY_* bits.
 */
unsigned int work_busy(struct work_struct *work)
{
	struct global_cwq *gcwq = get_work_gcwq(work);
	unsigned long flags;
	unsigned int ret = 0;

	if (!gcwq)
		return false;

	spin_lock_irqsave(&gcwq->lock, flags);

	if (work_pending(work))
		ret |= WORK_BUSY_PENDING;
	if (find_worker_executing_work(gcwq, work))
		ret |= WORK_BUSY_RUNNING;

	spin_unlock_irqrestore(&gcwq->lock, flags);

	return ret;
}
EXPORT_SYMBOL_GPL(work_busy);

/*
 * CPU hotplug.
 *
 * There are two challenges in supporting CPU hotplug.  Firstly, there
 * are a lot of assumptions on strong associations among work, cwq and
 * gcwq which make migrating pending and scheduled works very
 * difficult to implement without impacting hot paths.  Secondly,
 * gcwqs serve mix of short, long and very long running works making
 * blocked draining impractical.
 *
 * This is solved by allowing a gcwq to be disassociated from the CPU
 * running as an unbound one and allowing it to be reattached later if the
 * cpu comes back online.
 */

/* claim manager positions of all pools */
static void gcwq_claim_management_and_lock(struct global_cwq *gcwq)
{
	struct worker_pool *pool;

	for_each_worker_pool(pool, gcwq)
		mutex_lock_nested(&pool->manager_mutex, pool - gcwq->pools);
	spin_lock_irq(&gcwq->lock);
}

/* release manager positions */
static void gcwq_release_management_and_unlock(struct global_cwq *gcwq)
{
	struct worker_pool *pool;

	spin_unlock_irq(&gcwq->lock);
	for_each_worker_pool(pool, gcwq)
		mutex_unlock(&pool->manager_mutex);
}

static void gcwq_unbind_fn(struct work_struct *work)
{
	struct global_cwq *gcwq = get_gcwq(smp_processor_id());
	struct worker_pool *pool;
	struct worker *worker;
	struct hlist_node *pos;
	int i;

	BUG_ON(gcwq->cpu != smp_processor_id());

	gcwq_claim_management_and_lock(gcwq);

	/*
	 * We've claimed all manager positions.  Make all workers unbound
	 * and set DISASSOCIATED.  Before this, all workers except for the
	 * ones which are still executing works from before the last CPU
	 * down must be on the cpu.  After this, they may become diasporas.
	 */
	for_each_worker_pool(pool, gcwq)
		list_for_each_entry(worker, &pool->idle_list, entry)
			worker->flags |= WORKER_UNBOUND;

	for_each_busy_worker(worker, i, pos, gcwq)
		worker->flags |= WORKER_UNBOUND;

	gcwq->flags |= GCWQ_DISASSOCIATED;

	gcwq_release_management_and_unlock(gcwq);

	/*
	 * Call schedule() so that we cross rq->lock and thus can guarantee
	 * sched callbacks see the %WORKER_UNBOUND flag.  This is necessary
	 * as scheduler callbacks may be invoked from other cpus.
	 */
	schedule();

	/*
	 * Sched callbacks are disabled now.  Zap nr_running.  After this,
	 * nr_running stays zero and need_more_worker() and keep_working()
	 * are always true as long as the worklist is not empty.  @gcwq now
	 * behaves as unbound (in terms of concurrency management) gcwq
	 * which is served by workers tied to the CPU.
	 *
	 * On return from this function, the current worker would trigger
	 * unbound chain execution of pending work items if other workers
	 * didn't already.
	 */
	for_each_worker_pool(pool, gcwq)
		atomic_set(get_pool_nr_running(pool), 0);
}

/*
 * Workqueues should be brought up before normal priority CPU notifiers.
 * This will be registered high priority CPU notifier.
 */
static int __devinit workqueue_cpu_up_callback(struct notifier_block *nfb,
					       unsigned long action,
					       void *hcpu)
{
	unsigned int cpu = (unsigned long)hcpu;
	struct global_cwq *gcwq = get_gcwq(cpu);
	struct worker_pool *pool;

	switch (action & ~CPU_TASKS_FROZEN) {
	case CPU_UP_PREPARE:
		for_each_worker_pool(pool, gcwq) {
			struct worker *worker;

			if (pool->nr_workers)
				continue;

			worker = create_worker(pool);
			if (!worker)
				return NOTIFY_BAD;

			spin_lock_irq(&gcwq->lock);
			start_worker(worker);
			spin_unlock_irq(&gcwq->lock);
		}
		break;

	case CPU_DOWN_FAILED:
	case CPU_ONLINE:
		gcwq_claim_management_and_lock(gcwq);
		gcwq->flags &= ~GCWQ_DISASSOCIATED;
		rebind_workers(gcwq);
		gcwq_release_management_and_unlock(gcwq);
		break;
	}
	return NOTIFY_OK;
}

/*
 * Workqueues should be brought down after normal priority CPU notifiers.
 * This will be registered as low priority CPU notifier.
 */
static int __devinit workqueue_cpu_down_callback(struct notifier_block *nfb,
						 unsigned long action,
						 void *hcpu)
{
	unsigned int cpu = (unsigned long)hcpu;
	struct work_struct unbind_work;

	switch (action & ~CPU_TASKS_FROZEN) {
	case CPU_DOWN_PREPARE:
		/* unbinding should happen on the local CPU */
		INIT_WORK_ONSTACK(&unbind_work, gcwq_unbind_fn);
		queue_work_on(cpu, system_highpri_wq, &unbind_work);
		flush_work(&unbind_work);
		break;
	}
	return NOTIFY_OK;
}

#ifdef CONFIG_SMP

struct work_for_cpu {
	struct completion completion;
	long (*fn)(void *);
	void *arg;
	long ret;
};

static int do_work_for_cpu(void *_wfc)
{
	struct work_for_cpu *wfc = _wfc;
	wfc->ret = wfc->fn(wfc->arg);
	complete(&wfc->completion);
	return 0;
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
long work_on_cpu(unsigned int cpu, long (*fn)(void *), void *arg)
{
	struct task_struct *sub_thread;
	struct work_for_cpu wfc = {
		.completion = COMPLETION_INITIALIZER_ONSTACK(wfc.completion),
		.fn = fn,
		.arg = arg,
	};

	sub_thread = kthread_create(do_work_for_cpu, &wfc, "work_for_cpu");
	if (IS_ERR(sub_thread))
		return PTR_ERR(sub_thread);
	kthread_bind(sub_thread, cpu);
	wake_up_process(sub_thread);
	wait_for_completion(&wfc.completion);
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
 * gcwq->worklist.
 *
 * CONTEXT:
 * Grabs and releases workqueue_lock and gcwq->lock's.
 */
void freeze_workqueues_begin(void)
{
	unsigned int cpu;

	spin_lock(&workqueue_lock);

	BUG_ON(workqueue_freezing);
	workqueue_freezing = true;

	for_each_gcwq_cpu(cpu) {
		struct global_cwq *gcwq = get_gcwq(cpu);
		struct workqueue_struct *wq;

		spin_lock_irq(&gcwq->lock);

		BUG_ON(gcwq->flags & GCWQ_FREEZING);
		gcwq->flags |= GCWQ_FREEZING;

		list_for_each_entry(wq, &workqueues, list) {
			struct cpu_workqueue_struct *cwq = get_cwq(cpu, wq);

			if (cwq && wq->flags & WQ_FREEZABLE)
				cwq->max_active = 0;
		}

		spin_unlock_irq(&gcwq->lock);
	}

	spin_unlock(&workqueue_lock);
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
	unsigned int cpu;
	bool busy = false;

	spin_lock(&workqueue_lock);

	BUG_ON(!workqueue_freezing);

	for_each_gcwq_cpu(cpu) {
		struct workqueue_struct *wq;
		/*
		 * nr_active is monotonically decreasing.  It's safe
		 * to peek without lock.
		 */
		list_for_each_entry(wq, &workqueues, list) {
			struct cpu_workqueue_struct *cwq = get_cwq(cpu, wq);

			if (!cwq || !(wq->flags & WQ_FREEZABLE))
				continue;

			BUG_ON(cwq->nr_active < 0);
			if (cwq->nr_active) {
				busy = true;
				goto out_unlock;
			}
		}
	}
out_unlock:
	spin_unlock(&workqueue_lock);
	return busy;
}

/**
 * thaw_workqueues - thaw workqueues
 *
 * Thaw workqueues.  Normal queueing is restored and all collected
 * frozen works are transferred to their respective gcwq worklists.
 *
 * CONTEXT:
 * Grabs and releases workqueue_lock and gcwq->lock's.
 */
void thaw_workqueues(void)
{
	unsigned int cpu;

	spin_lock(&workqueue_lock);

	if (!workqueue_freezing)
		goto out_unlock;

	for_each_gcwq_cpu(cpu) {
		struct global_cwq *gcwq = get_gcwq(cpu);
		struct worker_pool *pool;
		struct workqueue_struct *wq;

		spin_lock_irq(&gcwq->lock);

		BUG_ON(!(gcwq->flags & GCWQ_FREEZING));
		gcwq->flags &= ~GCWQ_FREEZING;

		list_for_each_entry(wq, &workqueues, list) {
			struct cpu_workqueue_struct *cwq = get_cwq(cpu, wq);

			if (!cwq || !(wq->flags & WQ_FREEZABLE))
				continue;

			/* restore max_active and repopulate worklist */
			cwq->max_active = wq->saved_max_active;

			while (!list_empty(&cwq->delayed_works) &&
			       cwq->nr_active < cwq->max_active)
				cwq_activate_first_delayed(cwq);
		}

		for_each_worker_pool(pool, gcwq)
			wake_up_worker(pool);

		spin_unlock_irq(&gcwq->lock);
	}

	workqueue_freezing = false;
out_unlock:
	spin_unlock(&workqueue_lock);
}
#endif /* CONFIG_FREEZER */

static int __init init_workqueues(void)
{
	unsigned int cpu;
	int i;

	/* make sure we have enough bits for OFFQ CPU number */
	BUILD_BUG_ON((1LU << (BITS_PER_LONG - WORK_OFFQ_CPU_SHIFT)) <
		     WORK_CPU_LAST);

	cpu_notifier(workqueue_cpu_up_callback, CPU_PRI_WORKQUEUE_UP);
	cpu_notifier(workqueue_cpu_down_callback, CPU_PRI_WORKQUEUE_DOWN);

	/* initialize gcwqs */
	for_each_gcwq_cpu(cpu) {
		struct global_cwq *gcwq = get_gcwq(cpu);
		struct worker_pool *pool;

		spin_lock_init(&gcwq->lock);
		gcwq->cpu = cpu;
		gcwq->flags |= GCWQ_DISASSOCIATED;

		for (i = 0; i < BUSY_WORKER_HASH_SIZE; i++)
			INIT_HLIST_HEAD(&gcwq->busy_hash[i]);

		for_each_worker_pool(pool, gcwq) {
			pool->gcwq = gcwq;
			INIT_LIST_HEAD(&pool->worklist);
			INIT_LIST_HEAD(&pool->idle_list);

			init_timer_deferrable(&pool->idle_timer);
			pool->idle_timer.function = idle_worker_timeout;
			pool->idle_timer.data = (unsigned long)pool;

			setup_timer(&pool->mayday_timer, gcwq_mayday_timeout,
				    (unsigned long)pool);

			mutex_init(&pool->manager_mutex);
			ida_init(&pool->worker_ida);
		}

		init_waitqueue_head(&gcwq->rebind_hold);
	}

	/* create the initial worker */
	for_each_online_gcwq_cpu(cpu) {
		struct global_cwq *gcwq = get_gcwq(cpu);
		struct worker_pool *pool;

		if (cpu != WORK_CPU_UNBOUND)
			gcwq->flags &= ~GCWQ_DISASSOCIATED;

		for_each_worker_pool(pool, gcwq) {
			struct worker *worker;

			worker = create_worker(pool);
			BUG_ON(!worker);
			spin_lock_irq(&gcwq->lock);
			start_worker(worker);
			spin_unlock_irq(&gcwq->lock);
		}
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
