/* SPDX-License-Identifier: GPL-2.0 */
/*
 * BPF extensible scheduler class: Documentation/scheduler/sched-ext.rst
 *
 * Copyright (c) 2022 Meta Platforms, Inc. and affiliates.
 * Copyright (c) 2022 Tejun Heo <tj@kernel.org>
 * Copyright (c) 2022 David Vernet <dvernet@meta.com>
 */
#include <linux/btf_ids.h>
#include "ext_idle.h"

/*
 * NOTE: sched_ext is in the process of growing multiple scheduler support and
 * scx_root usage is in a transitional state. Naked dereferences are safe if the
 * caller is one of the tasks attached to SCX and explicit RCU dereference is
 * necessary otherwise. Naked scx_root dereferences trigger sparse warnings but
 * are used as temporary markers to indicate that the dereferences need to be
 * updated to point to the associated scheduler instances rather than scx_root.
 */
static struct scx_sched __rcu *scx_root;

/*
 * During exit, a task may schedule after losing its PIDs. When disabling the
 * BPF scheduler, we need to be able to iterate tasks in every state to
 * guarantee system safety. Maintain a dedicated task list which contains every
 * task between its fork and eventual free.
 */
static DEFINE_RAW_SPINLOCK(scx_tasks_lock);
static LIST_HEAD(scx_tasks);

/* ops enable/disable */
static DEFINE_MUTEX(scx_enable_mutex);
DEFINE_STATIC_KEY_FALSE(__scx_enabled);
DEFINE_STATIC_PERCPU_RWSEM(scx_fork_rwsem);
static atomic_t scx_enable_state_var = ATOMIC_INIT(SCX_DISABLED);
static unsigned long scx_in_softlockup;
static atomic_t scx_breather_depth = ATOMIC_INIT(0);
static int scx_bypass_depth;
static bool scx_init_task_enabled;
static bool scx_switching_all;
DEFINE_STATIC_KEY_FALSE(__scx_switched_all);

static atomic_long_t scx_nr_rejected = ATOMIC_LONG_INIT(0);
static atomic_long_t scx_hotplug_seq = ATOMIC_LONG_INIT(0);

/*
 * A monotically increasing sequence number that is incremented every time a
 * scheduler is enabled. This can be used by to check if any custom sched_ext
 * scheduler has ever been used in the system.
 */
static atomic_long_t scx_enable_seq = ATOMIC_LONG_INIT(0);

/*
 * The maximum amount of time in jiffies that a task may be runnable without
 * being scheduled on a CPU. If this timeout is exceeded, it will trigger
 * scx_error().
 */
static unsigned long scx_watchdog_timeout;

/*
 * The last time the delayed work was run. This delayed work relies on
 * ksoftirqd being able to run to service timer interrupts, so it's possible
 * that this work itself could get wedged. To account for this, we check that
 * it's not stalled in the timer tick, and trigger an error if it is.
 */
static unsigned long scx_watchdog_timestamp = INITIAL_JIFFIES;

static struct delayed_work scx_watchdog_work;

/*
 * For %SCX_KICK_WAIT: Each CPU has a pointer to an array of pick_task sequence
 * numbers. The arrays are allocated with kvzalloc() as size can exceed percpu
 * allocator limits on large machines. O(nr_cpu_ids^2) allocation, allocated
 * lazily when enabling and freed when disabling to avoid waste when sched_ext
 * isn't active.
 */
struct scx_kick_pseqs {
	struct rcu_head		rcu;
	unsigned long		seqs[];
};

static DEFINE_PER_CPU(struct scx_kick_pseqs __rcu *, scx_kick_pseqs);

/*
 * Direct dispatch marker.
 *
 * Non-NULL values are used for direct dispatch from enqueue path. A valid
 * pointer points to the task currently being enqueued. An ERR_PTR value is used
 * to indicate that direct dispatch has already happened.
 */
static DEFINE_PER_CPU(struct task_struct *, direct_dispatch_task);

static const struct rhashtable_params dsq_hash_params = {
	.key_len		= sizeof_field(struct scx_dispatch_q, id),
	.key_offset		= offsetof(struct scx_dispatch_q, id),
	.head_offset		= offsetof(struct scx_dispatch_q, hash_node),
};

static LLIST_HEAD(dsqs_to_free);

/* dispatch buf */
struct scx_dsp_buf_ent {
	struct task_struct	*task;
	unsigned long		qseq;
	u64			dsq_id;
	u64			enq_flags;
};

static u32 scx_dsp_max_batch;

struct scx_dsp_ctx {
	struct rq		*rq;
	u32			cursor;
	u32			nr_tasks;
	struct scx_dsp_buf_ent	buf[];
};

static struct scx_dsp_ctx __percpu *scx_dsp_ctx;

/* string formatting from BPF */
struct scx_bstr_buf {
	u64			data[MAX_BPRINTF_VARARGS];
	char			line[SCX_EXIT_MSG_LEN];
};

static DEFINE_RAW_SPINLOCK(scx_exit_bstr_buf_lock);
static struct scx_bstr_buf scx_exit_bstr_buf;

/* ops debug dump */
struct scx_dump_data {
	s32			cpu;
	bool			first;
	s32			cursor;
	struct seq_buf		*s;
	const char		*prefix;
	struct scx_bstr_buf	buf;
};

static struct scx_dump_data scx_dump_data = {
	.cpu			= -1,
};

/* /sys/kernel/sched_ext interface */
static struct kset *scx_kset;

#define CREATE_TRACE_POINTS
#include <trace/events/sched_ext.h>

static void process_ddsp_deferred_locals(struct rq *rq);
static void scx_kick_cpu(struct scx_sched *sch, s32 cpu, u64 flags);
static void scx_vexit(struct scx_sched *sch, enum scx_exit_kind kind,
		      s64 exit_code, const char *fmt, va_list args);

static __printf(4, 5) void scx_exit(struct scx_sched *sch,
				    enum scx_exit_kind kind, s64 exit_code,
				    const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	scx_vexit(sch, kind, exit_code, fmt, args);
	va_end(args);
}

#define scx_error(sch, fmt, args...)	scx_exit((sch), SCX_EXIT_ERROR, 0, fmt, ##args)

#define SCX_HAS_OP(sch, op)	test_bit(SCX_OP_IDX(op), (sch)->has_op)

static long jiffies_delta_msecs(unsigned long at, unsigned long now)
{
	if (time_after(at, now))
		return jiffies_to_msecs(at - now);
	else
		return -(long)jiffies_to_msecs(now - at);
}

/* if the highest set bit is N, return a mask with bits [N+1, 31] set */
static u32 higher_bits(u32 flags)
{
	return ~((1 << fls(flags)) - 1);
}

/* return the mask with only the highest bit set */
static u32 highest_bit(u32 flags)
{
	int bit = fls(flags);
	return ((u64)1 << bit) >> 1;
}

static bool u32_before(u32 a, u32 b)
{
	return (s32)(a - b) < 0;
}

static struct scx_dispatch_q *find_global_dsq(struct scx_sched *sch,
					      struct task_struct *p)
{
	return sch->global_dsqs[cpu_to_node(task_cpu(p))];
}

static struct scx_dispatch_q *find_user_dsq(struct scx_sched *sch, u64 dsq_id)
{
	return rhashtable_lookup_fast(&sch->dsq_hash, &dsq_id, dsq_hash_params);
}

/*
 * scx_kf_mask enforcement. Some kfuncs can only be called from specific SCX
 * ops. When invoking SCX ops, SCX_CALL_OP[_RET]() should be used to indicate
 * the allowed kfuncs and those kfuncs should use scx_kf_allowed() to check
 * whether it's running from an allowed context.
 *
 * @mask is constant, always inline to cull the mask calculations.
 */
static __always_inline void scx_kf_allow(u32 mask)
{
	/* nesting is allowed only in increasing scx_kf_mask order */
	WARN_ONCE((mask | higher_bits(mask)) & current->scx.kf_mask,
		  "invalid nesting current->scx.kf_mask=0x%x mask=0x%x\n",
		  current->scx.kf_mask, mask);
	current->scx.kf_mask |= mask;
	barrier();
}

static void scx_kf_disallow(u32 mask)
{
	barrier();
	current->scx.kf_mask &= ~mask;
}

/*
 * Track the rq currently locked.
 *
 * This allows kfuncs to safely operate on rq from any scx ops callback,
 * knowing which rq is already locked.
 */
DEFINE_PER_CPU(struct rq *, scx_locked_rq_state);

static inline void update_locked_rq(struct rq *rq)
{
	/*
	 * Check whether @rq is actually locked. This can help expose bugs
	 * or incorrect assumptions about the context in which a kfunc or
	 * callback is executed.
	 */
	if (rq)
		lockdep_assert_rq_held(rq);
	__this_cpu_write(scx_locked_rq_state, rq);
}

#define SCX_CALL_OP(sch, mask, op, rq, args...)					\
do {										\
	if (rq)									\
		update_locked_rq(rq);						\
	if (mask) {								\
		scx_kf_allow(mask);						\
		(sch)->ops.op(args);						\
		scx_kf_disallow(mask);						\
	} else {								\
		(sch)->ops.op(args);						\
	}									\
	if (rq)									\
		update_locked_rq(NULL);						\
} while (0)

#define SCX_CALL_OP_RET(sch, mask, op, rq, args...)				\
({										\
	__typeof__((sch)->ops.op(args)) __ret;					\
										\
	if (rq)									\
		update_locked_rq(rq);						\
	if (mask) {								\
		scx_kf_allow(mask);						\
		__ret = (sch)->ops.op(args);					\
		scx_kf_disallow(mask);						\
	} else {								\
		__ret = (sch)->ops.op(args);					\
	}									\
	if (rq)									\
		update_locked_rq(NULL);						\
	__ret;									\
})

/*
 * Some kfuncs are allowed only on the tasks that are subjects of the
 * in-progress scx_ops operation for, e.g., locking guarantees. To enforce such
 * restrictions, the following SCX_CALL_OP_*() variants should be used when
 * invoking scx_ops operations that take task arguments. These can only be used
 * for non-nesting operations due to the way the tasks are tracked.
 *
 * kfuncs which can only operate on such tasks can in turn use
 * scx_kf_allowed_on_arg_tasks() to test whether the invocation is allowed on
 * the specific task.
 */
#define SCX_CALL_OP_TASK(sch, mask, op, rq, task, args...)			\
do {										\
	BUILD_BUG_ON((mask) & ~__SCX_KF_TERMINAL);				\
	current->scx.kf_tasks[0] = task;					\
	SCX_CALL_OP((sch), mask, op, rq, task, ##args);				\
	current->scx.kf_tasks[0] = NULL;					\
} while (0)

#define SCX_CALL_OP_TASK_RET(sch, mask, op, rq, task, args...)			\
({										\
	__typeof__((sch)->ops.op(task, ##args)) __ret;				\
	BUILD_BUG_ON((mask) & ~__SCX_KF_TERMINAL);				\
	current->scx.kf_tasks[0] = task;					\
	__ret = SCX_CALL_OP_RET((sch), mask, op, rq, task, ##args);		\
	current->scx.kf_tasks[0] = NULL;					\
	__ret;									\
})

#define SCX_CALL_OP_2TASKS_RET(sch, mask, op, rq, task0, task1, args...)	\
({										\
	__typeof__((sch)->ops.op(task0, task1, ##args)) __ret;			\
	BUILD_BUG_ON((mask) & ~__SCX_KF_TERMINAL);				\
	current->scx.kf_tasks[0] = task0;					\
	current->scx.kf_tasks[1] = task1;					\
	__ret = SCX_CALL_OP_RET((sch), mask, op, rq, task0, task1, ##args);	\
	current->scx.kf_tasks[0] = NULL;					\
	current->scx.kf_tasks[1] = NULL;					\
	__ret;									\
})

/* @mask is constant, always inline to cull unnecessary branches */
static __always_inline bool scx_kf_allowed(struct scx_sched *sch, u32 mask)
{
	if (unlikely(!(current->scx.kf_mask & mask))) {
		scx_error(sch, "kfunc with mask 0x%x called from an operation only allowing 0x%x",
			  mask, current->scx.kf_mask);
		return false;
	}

	/*
	 * Enforce nesting boundaries. e.g. A kfunc which can be called from
	 * DISPATCH must not be called if we're running DEQUEUE which is nested
	 * inside ops.dispatch(). We don't need to check boundaries for any
	 * blocking kfuncs as the verifier ensures they're only called from
	 * sleepable progs.
	 */
	if (unlikely(highest_bit(mask) == SCX_KF_CPU_RELEASE &&
		     (current->scx.kf_mask & higher_bits(SCX_KF_CPU_RELEASE)))) {
		scx_error(sch, "cpu_release kfunc called from a nested operation");
		return false;
	}

	if (unlikely(highest_bit(mask) == SCX_KF_DISPATCH &&
		     (current->scx.kf_mask & higher_bits(SCX_KF_DISPATCH)))) {
		scx_error(sch, "dispatch kfunc called from a nested operation");
		return false;
	}

	return true;
}

/* see SCX_CALL_OP_TASK() */
static __always_inline bool scx_kf_allowed_on_arg_tasks(struct scx_sched *sch,
							u32 mask,
							struct task_struct *p)
{
	if (!scx_kf_allowed(sch, mask))
		return false;

	if (unlikely((p != current->scx.kf_tasks[0] &&
		      p != current->scx.kf_tasks[1]))) {
		scx_error(sch, "called on a task not being operated on");
		return false;
	}

	return true;
}

/**
 * nldsq_next_task - Iterate to the next task in a non-local DSQ
 * @dsq: user dsq being iterated
 * @cur: current position, %NULL to start iteration
 * @rev: walk backwards
 *
 * Returns %NULL when iteration is finished.
 */
static struct task_struct *nldsq_next_task(struct scx_dispatch_q *dsq,
					   struct task_struct *cur, bool rev)
{
	struct list_head *list_node;
	struct scx_dsq_list_node *dsq_lnode;

	lockdep_assert_held(&dsq->lock);

	if (cur)
		list_node = &cur->scx.dsq_list.node;
	else
		list_node = &dsq->list;

	/* find the next task, need to skip BPF iteration cursors */
	do {
		if (rev)
			list_node = list_node->prev;
		else
			list_node = list_node->next;

		if (list_node == &dsq->list)
			return NULL;

		dsq_lnode = container_of(list_node, struct scx_dsq_list_node,
					 node);
	} while (dsq_lnode->flags & SCX_DSQ_LNODE_ITER_CURSOR);

	return container_of(dsq_lnode, struct task_struct, scx.dsq_list);
}

#define nldsq_for_each_task(p, dsq)						\
	for ((p) = nldsq_next_task((dsq), NULL, false); (p);			\
	     (p) = nldsq_next_task((dsq), (p), false))


/*
 * BPF DSQ iterator. Tasks in a non-local DSQ can be iterated in [reverse]
 * dispatch order. BPF-visible iterator is opaque and larger to allow future
 * changes without breaking backward compatibility. Can be used with
 * bpf_for_each(). See bpf_iter_scx_dsq_*().
 */
enum scx_dsq_iter_flags {
	/* iterate in the reverse dispatch order */
	SCX_DSQ_ITER_REV		= 1U << 16,

	__SCX_DSQ_ITER_HAS_SLICE	= 1U << 30,
	__SCX_DSQ_ITER_HAS_VTIME	= 1U << 31,

	__SCX_DSQ_ITER_USER_FLAGS	= SCX_DSQ_ITER_REV,
	__SCX_DSQ_ITER_ALL_FLAGS	= __SCX_DSQ_ITER_USER_FLAGS |
					  __SCX_DSQ_ITER_HAS_SLICE |
					  __SCX_DSQ_ITER_HAS_VTIME,
};

struct bpf_iter_scx_dsq_kern {
	struct scx_dsq_list_node	cursor;
	struct scx_dispatch_q		*dsq;
	u64				slice;
	u64				vtime;
} __attribute__((aligned(8)));

struct bpf_iter_scx_dsq {
	u64				__opaque[6];
} __attribute__((aligned(8)));


/*
 * SCX task iterator.
 */
struct scx_task_iter {
	struct sched_ext_entity		cursor;
	struct task_struct		*locked_task;
	struct rq			*rq;
	struct rq_flags			rf;
	u32				cnt;
	bool				list_locked;
};

/**
 * scx_task_iter_start - Lock scx_tasks_lock and start a task iteration
 * @iter: iterator to init
 *
 * Initialize @iter and return with scx_tasks_lock held. Once initialized, @iter
 * must eventually be stopped with scx_task_iter_stop().
 *
 * scx_tasks_lock and the rq lock may be released using scx_task_iter_unlock()
 * between this and the first next() call or between any two next() calls. If
 * the locks are released between two next() calls, the caller is responsible
 * for ensuring that the task being iterated remains accessible either through
 * RCU read lock or obtaining a reference count.
 *
 * All tasks which existed when the iteration started are guaranteed to be
 * visited as long as they still exist.
 */
static void scx_task_iter_start(struct scx_task_iter *iter)
{
	BUILD_BUG_ON(__SCX_DSQ_ITER_ALL_FLAGS &
		     ((1U << __SCX_DSQ_LNODE_PRIV_SHIFT) - 1));

	raw_spin_lock_irq(&scx_tasks_lock);

	iter->cursor = (struct sched_ext_entity){ .flags = SCX_TASK_CURSOR };
	list_add(&iter->cursor.tasks_node, &scx_tasks);
	iter->locked_task = NULL;
	iter->cnt = 0;
	iter->list_locked = true;
}

static void __scx_task_iter_rq_unlock(struct scx_task_iter *iter)
{
	if (iter->locked_task) {
		task_rq_unlock(iter->rq, iter->locked_task, &iter->rf);
		iter->locked_task = NULL;
	}
}

/**
 * scx_task_iter_unlock - Unlock rq and scx_tasks_lock held by a task iterator
 * @iter: iterator to unlock
 *
 * If @iter is in the middle of a locked iteration, it may be locking the rq of
 * the task currently being visited in addition to scx_tasks_lock. Unlock both.
 * This function can be safely called anytime during an iteration. The next
 * iterator operation will automatically restore the necessary locking.
 */
static void scx_task_iter_unlock(struct scx_task_iter *iter)
{
	__scx_task_iter_rq_unlock(iter);
	if (iter->list_locked) {
		iter->list_locked = false;
		raw_spin_unlock_irq(&scx_tasks_lock);
	}
}

static void __scx_task_iter_maybe_relock(struct scx_task_iter *iter)
{
	if (!iter->list_locked) {
		raw_spin_lock_irq(&scx_tasks_lock);
		iter->list_locked = true;
	}
}

/**
 * scx_task_iter_stop - Stop a task iteration and unlock scx_tasks_lock
 * @iter: iterator to exit
 *
 * Exit a previously initialized @iter. Must be called with scx_tasks_lock held
 * which is released on return. If the iterator holds a task's rq lock, that rq
 * lock is also released. See scx_task_iter_start() for details.
 */
static void scx_task_iter_stop(struct scx_task_iter *iter)
{
	__scx_task_iter_maybe_relock(iter);
	list_del_init(&iter->cursor.tasks_node);
	scx_task_iter_unlock(iter);
}

/**
 * scx_task_iter_next - Next task
 * @iter: iterator to walk
 *
 * Visit the next task. See scx_task_iter_start() for details. Locks are dropped
 * and re-acquired every %SCX_TASK_ITER_BATCH iterations to avoid causing stalls
 * by holding scx_tasks_lock for too long.
 */
static struct task_struct *scx_task_iter_next(struct scx_task_iter *iter)
{
	struct list_head *cursor = &iter->cursor.tasks_node;
	struct sched_ext_entity *pos;

	__scx_task_iter_maybe_relock(iter);

	if (!(++iter->cnt % SCX_TASK_ITER_BATCH)) {
		scx_task_iter_unlock(iter);
		cond_resched();
		__scx_task_iter_maybe_relock(iter);
	}

	list_for_each_entry(pos, cursor, tasks_node) {
		if (&pos->tasks_node == &scx_tasks)
			return NULL;
		if (!(pos->flags & SCX_TASK_CURSOR)) {
			list_move(cursor, &pos->tasks_node);
			return container_of(pos, struct task_struct, scx);
		}
	}

	/* can't happen, should always terminate at scx_tasks above */
	BUG();
}

/**
 * scx_task_iter_next_locked - Next non-idle task with its rq locked
 * @iter: iterator to walk
 *
 * Visit the non-idle task with its rq lock held. Allows callers to specify
 * whether they would like to filter out dead tasks. See scx_task_iter_start()
 * for details.
 */
static struct task_struct *scx_task_iter_next_locked(struct scx_task_iter *iter)
{
	struct task_struct *p;

	__scx_task_iter_rq_unlock(iter);

	while ((p = scx_task_iter_next(iter))) {
		/*
		 * scx_task_iter is used to prepare and move tasks into SCX
		 * while loading the BPF scheduler and vice-versa while
		 * unloading. The init_tasks ("swappers") should be excluded
		 * from the iteration because:
		 *
		 * - It's unsafe to use __setschduler_prio() on an init_task to
		 *   determine the sched_class to use as it won't preserve its
		 *   idle_sched_class.
		 *
		 * - ops.init/exit_task() can easily be confused if called with
		 *   init_tasks as they, e.g., share PID 0.
		 *
		 * As init_tasks are never scheduled through SCX, they can be
		 * skipped safely. Note that is_idle_task() which tests %PF_IDLE
		 * doesn't work here:
		 *
		 * - %PF_IDLE may not be set for an init_task whose CPU hasn't
		 *   yet been onlined.
		 *
		 * - %PF_IDLE can be set on tasks that are not init_tasks. See
		 *   play_idle_precise() used by CONFIG_IDLE_INJECT.
		 *
		 * Test for idle_sched_class as only init_tasks are on it.
		 */
		if (p->sched_class != &idle_sched_class)
			break;
	}
	if (!p)
		return NULL;

	iter->rq = task_rq_lock(p, &iter->rf);
	iter->locked_task = p;

	return p;
}

/**
 * scx_add_event - Increase an event counter for 'name' by 'cnt'
 * @sch: scx_sched to account events for
 * @name: an event name defined in struct scx_event_stats
 * @cnt: the number of the event occurred
 *
 * This can be used when preemption is not disabled.
 */
#define scx_add_event(sch, name, cnt) do {					\
	this_cpu_add((sch)->pcpu->event_stats.name, (cnt));			\
	trace_sched_ext_event(#name, (cnt));					\
} while(0)

/**
 * __scx_add_event - Increase an event counter for 'name' by 'cnt'
 * @sch: scx_sched to account events for
 * @name: an event name defined in struct scx_event_stats
 * @cnt: the number of the event occurred
 *
 * This should be used only when preemption is disabled.
 */
#define __scx_add_event(sch, name, cnt) do {					\
	__this_cpu_add((sch)->pcpu->event_stats.name, (cnt));			\
	trace_sched_ext_event(#name, cnt);					\
} while(0)

/**
 * scx_agg_event - Aggregate an event counter 'kind' from 'src_e' to 'dst_e'
 * @dst_e: destination event stats
 * @src_e: source event stats
 * @kind: a kind of event to be aggregated
 */
#define scx_agg_event(dst_e, src_e, kind) do {					\
	(dst_e)->kind += READ_ONCE((src_e)->kind);				\
} while(0)

/**
 * scx_dump_event - Dump an event 'kind' in 'events' to 's'
 * @s: output seq_buf
 * @events: event stats
 * @kind: a kind of event to dump
 */
#define scx_dump_event(s, events, kind) do {					\
	dump_line(&(s), "%40s: %16lld", #kind, (events)->kind);			\
} while (0)


static void scx_read_events(struct scx_sched *sch,
			    struct scx_event_stats *events);

static enum scx_enable_state scx_enable_state(void)
{
	return atomic_read(&scx_enable_state_var);
}

static enum scx_enable_state scx_set_enable_state(enum scx_enable_state to)
{
	return atomic_xchg(&scx_enable_state_var, to);
}

static bool scx_tryset_enable_state(enum scx_enable_state to,
				    enum scx_enable_state from)
{
	int from_v = from;

	return atomic_try_cmpxchg(&scx_enable_state_var, &from_v, to);
}

/**
 * wait_ops_state - Busy-wait the specified ops state to end
 * @p: target task
 * @opss: state to wait the end of
 *
 * Busy-wait for @p to transition out of @opss. This can only be used when the
 * state part of @opss is %SCX_QUEUEING or %SCX_DISPATCHING. This function also
 * has load_acquire semantics to ensure that the caller can see the updates made
 * in the enqueueing and dispatching paths.
 */
static void wait_ops_state(struct task_struct *p, unsigned long opss)
{
	do {
		cpu_relax();
	} while (atomic_long_read_acquire(&p->scx.ops_state) == opss);
}

static inline bool __cpu_valid(s32 cpu)
{
	return likely(cpu >= 0 && cpu < nr_cpu_ids && cpu_possible(cpu));
}

/**
 * ops_cpu_valid - Verify a cpu number, to be used on ops input args
 * @sch: scx_sched to abort on error
 * @cpu: cpu number which came from a BPF ops
 * @where: extra information reported on error
 *
 * @cpu is a cpu number which came from the BPF scheduler and can be any value.
 * Verify that it is in range and one of the possible cpus. If invalid, trigger
 * an ops error.
 */
static bool ops_cpu_valid(struct scx_sched *sch, s32 cpu, const char *where)
{
	if (__cpu_valid(cpu)) {
		return true;
	} else {
		scx_error(sch, "invalid CPU %d%s%s", cpu, where ? " " : "", where ?: "");
		return false;
	}
}

/**
 * ops_sanitize_err - Sanitize a -errno value
 * @sch: scx_sched to error out on error
 * @ops_name: operation to blame on failure
 * @err: -errno value to sanitize
 *
 * Verify @err is a valid -errno. If not, trigger scx_error() and return
 * -%EPROTO. This is necessary because returning a rogue -errno up the chain can
 * cause misbehaviors. For an example, a large negative return from
 * ops.init_task() triggers an oops when passed up the call chain because the
 * value fails IS_ERR() test after being encoded with ERR_PTR() and then is
 * handled as a pointer.
 */
static int ops_sanitize_err(struct scx_sched *sch, const char *ops_name, s32 err)
{
	if (err < 0 && err >= -MAX_ERRNO)
		return err;

	scx_error(sch, "ops.%s() returned an invalid errno %d", ops_name, err);
	return -EPROTO;
}

static void run_deferred(struct rq *rq)
{
	process_ddsp_deferred_locals(rq);
}

static void deferred_bal_cb_workfn(struct rq *rq)
{
	run_deferred(rq);
}

static void deferred_irq_workfn(struct irq_work *irq_work)
{
	struct rq *rq = container_of(irq_work, struct rq, scx.deferred_irq_work);

	raw_spin_rq_lock(rq);
	run_deferred(rq);
	raw_spin_rq_unlock(rq);
}

/**
 * schedule_deferred - Schedule execution of deferred actions on an rq
 * @rq: target rq
 *
 * Schedule execution of deferred actions on @rq. Must be called with @rq
 * locked. Deferred actions are executed with @rq locked but unpinned, and thus
 * can unlock @rq to e.g. migrate tasks to other rqs.
 */
static void schedule_deferred(struct rq *rq)
{
	lockdep_assert_rq_held(rq);

	/*
	 * If in the middle of waking up a task, task_woken_scx() will be called
	 * afterwards which will then run the deferred actions, no need to
	 * schedule anything.
	 */
	if (rq->scx.flags & SCX_RQ_IN_WAKEUP)
		return;

	/* Don't do anything if there already is a deferred operation. */
	if (rq->scx.flags & SCX_RQ_BAL_CB_PENDING)
		return;

	/*
	 * If in balance, the balance callbacks will be called before rq lock is
	 * released. Schedule one.
	 *
	 *
	 * We can't directly insert the callback into the
	 * rq's list: The call can drop its lock and make the pending balance
	 * callback visible to unrelated code paths that call rq_pin_lock().
	 *
	 * Just let balance_one() know that it must do it itself.
	 */
	if (rq->scx.flags & SCX_RQ_IN_BALANCE) {
		rq->scx.flags |= SCX_RQ_BAL_CB_PENDING;
		return;
	}

	/*
	 * No scheduler hooks available. Queue an irq work. They are executed on
	 * IRQ re-enable which may take a bit longer than the scheduler hooks.
	 * The above WAKEUP and BALANCE paths should cover most of the cases and
	 * the time to IRQ re-enable shouldn't be long.
	 */
	irq_work_queue(&rq->scx.deferred_irq_work);
}

/**
 * touch_core_sched - Update timestamp used for core-sched task ordering
 * @rq: rq to read clock from, must be locked
 * @p: task to update the timestamp for
 *
 * Update @p->scx.core_sched_at timestamp. This is used by scx_prio_less() to
 * implement global or local-DSQ FIFO ordering for core-sched. Should be called
 * when a task becomes runnable and its turn on the CPU ends (e.g. slice
 * exhaustion).
 */
static void touch_core_sched(struct rq *rq, struct task_struct *p)
{
	lockdep_assert_rq_held(rq);

#ifdef CONFIG_SCHED_CORE
	/*
	 * It's okay to update the timestamp spuriously. Use
	 * sched_core_disabled() which is cheaper than enabled().
	 *
	 * As this is used to determine ordering between tasks of sibling CPUs,
	 * it may be better to use per-core dispatch sequence instead.
	 */
	if (!sched_core_disabled())
		p->scx.core_sched_at = sched_clock_cpu(cpu_of(rq));
#endif
}

/**
 * touch_core_sched_dispatch - Update core-sched timestamp on dispatch
 * @rq: rq to read clock from, must be locked
 * @p: task being dispatched
 *
 * If the BPF scheduler implements custom core-sched ordering via
 * ops.core_sched_before(), @p->scx.core_sched_at is used to implement FIFO
 * ordering within each local DSQ. This function is called from dispatch paths
 * and updates @p->scx.core_sched_at if custom core-sched ordering is in effect.
 */
static void touch_core_sched_dispatch(struct rq *rq, struct task_struct *p)
{
	lockdep_assert_rq_held(rq);

#ifdef CONFIG_SCHED_CORE
	if (unlikely(SCX_HAS_OP(scx_root, core_sched_before)))
		touch_core_sched(rq, p);
#endif
}

static void update_curr_scx(struct rq *rq)
{
	struct task_struct *curr = rq->curr;
	s64 delta_exec;

	delta_exec = update_curr_common(rq);
	if (unlikely(delta_exec <= 0))
		return;

	if (curr->scx.slice != SCX_SLICE_INF) {
		curr->scx.slice -= min_t(u64, curr->scx.slice, delta_exec);
		if (!curr->scx.slice)
			touch_core_sched(rq, curr);
	}
}

static bool scx_dsq_priq_less(struct rb_node *node_a,
			      const struct rb_node *node_b)
{
	const struct task_struct *a =
		container_of(node_a, struct task_struct, scx.dsq_priq);
	const struct task_struct *b =
		container_of(node_b, struct task_struct, scx.dsq_priq);

	return time_before64(a->scx.dsq_vtime, b->scx.dsq_vtime);
}

static void dsq_mod_nr(struct scx_dispatch_q *dsq, s32 delta)
{
	/* scx_bpf_dsq_nr_queued() reads ->nr without locking, use WRITE_ONCE() */
	WRITE_ONCE(dsq->nr, dsq->nr + delta);
}

static void refill_task_slice_dfl(struct scx_sched *sch, struct task_struct *p)
{
	p->scx.slice = SCX_SLICE_DFL;
	__scx_add_event(sch, SCX_EV_REFILL_SLICE_DFL, 1);
}

static void dispatch_enqueue(struct scx_sched *sch, struct scx_dispatch_q *dsq,
			     struct task_struct *p, u64 enq_flags)
{
	bool is_local = dsq->id == SCX_DSQ_LOCAL;

	WARN_ON_ONCE(p->scx.dsq || !list_empty(&p->scx.dsq_list.node));
	WARN_ON_ONCE((p->scx.dsq_flags & SCX_TASK_DSQ_ON_PRIQ) ||
		     !RB_EMPTY_NODE(&p->scx.dsq_priq));

	if (!is_local) {
		raw_spin_lock(&dsq->lock);
		if (unlikely(dsq->id == SCX_DSQ_INVALID)) {
			scx_error(sch, "attempting to dispatch to a destroyed dsq");
			/* fall back to the global dsq */
			raw_spin_unlock(&dsq->lock);
			dsq = find_global_dsq(sch, p);
			raw_spin_lock(&dsq->lock);
		}
	}

	if (unlikely((dsq->id & SCX_DSQ_FLAG_BUILTIN) &&
		     (enq_flags & SCX_ENQ_DSQ_PRIQ))) {
		/*
		 * SCX_DSQ_LOCAL and SCX_DSQ_GLOBAL DSQs always consume from
		 * their FIFO queues. To avoid confusion and accidentally
		 * starving vtime-dispatched tasks by FIFO-dispatched tasks, we
		 * disallow any internal DSQ from doing vtime ordering of
		 * tasks.
		 */
		scx_error(sch, "cannot use vtime ordering for built-in DSQs");
		enq_flags &= ~SCX_ENQ_DSQ_PRIQ;
	}

	if (enq_flags & SCX_ENQ_DSQ_PRIQ) {
		struct rb_node *rbp;

		/*
		 * A PRIQ DSQ shouldn't be using FIFO enqueueing. As tasks are
		 * linked to both the rbtree and list on PRIQs, this can only be
		 * tested easily when adding the first task.
		 */
		if (unlikely(RB_EMPTY_ROOT(&dsq->priq) &&
			     nldsq_next_task(dsq, NULL, false)))
			scx_error(sch, "DSQ ID 0x%016llx already had FIFO-enqueued tasks",
				  dsq->id);

		p->scx.dsq_flags |= SCX_TASK_DSQ_ON_PRIQ;
		rb_add(&p->scx.dsq_priq, &dsq->priq, scx_dsq_priq_less);

		/*
		 * Find the previous task and insert after it on the list so
		 * that @dsq->list is vtime ordered.
		 */
		rbp = rb_prev(&p->scx.dsq_priq);
		if (rbp) {
			struct task_struct *prev =
				container_of(rbp, struct task_struct,
					     scx.dsq_priq);
			list_add(&p->scx.dsq_list.node, &prev->scx.dsq_list.node);
		} else {
			list_add(&p->scx.dsq_list.node, &dsq->list);
		}
	} else {
		/* a FIFO DSQ shouldn't be using PRIQ enqueuing */
		if (unlikely(!RB_EMPTY_ROOT(&dsq->priq)))
			scx_error(sch, "DSQ ID 0x%016llx already had PRIQ-enqueued tasks",
				  dsq->id);

		if (enq_flags & (SCX_ENQ_HEAD | SCX_ENQ_PREEMPT))
			list_add(&p->scx.dsq_list.node, &dsq->list);
		else
			list_add_tail(&p->scx.dsq_list.node, &dsq->list);
	}

	/* seq records the order tasks are queued, used by BPF DSQ iterator */
	dsq->seq++;
	p->scx.dsq_seq = dsq->seq;

	dsq_mod_nr(dsq, 1);
	p->scx.dsq = dsq;

	/*
	 * scx.ddsp_dsq_id and scx.ddsp_enq_flags are only relevant on the
	 * direct dispatch path, but we clear them here because the direct
	 * dispatch verdict may be overridden on the enqueue path during e.g.
	 * bypass.
	 */
	p->scx.ddsp_dsq_id = SCX_DSQ_INVALID;
	p->scx.ddsp_enq_flags = 0;

	/*
	 * We're transitioning out of QUEUEING or DISPATCHING. store_release to
	 * match waiters' load_acquire.
	 */
	if (enq_flags & SCX_ENQ_CLEAR_OPSS)
		atomic_long_set_release(&p->scx.ops_state, SCX_OPSS_NONE);

	if (is_local) {
		struct rq *rq = container_of(dsq, struct rq, scx.local_dsq);
		bool preempt = false;

		if ((enq_flags & SCX_ENQ_PREEMPT) && p != rq->curr &&
		    rq->curr->sched_class == &ext_sched_class) {
			rq->curr->scx.slice = 0;
			preempt = true;
		}

		if (preempt || sched_class_above(&ext_sched_class,
						 rq->curr->sched_class))
			resched_curr(rq);
	} else {
		raw_spin_unlock(&dsq->lock);
	}
}

static void task_unlink_from_dsq(struct task_struct *p,
				 struct scx_dispatch_q *dsq)
{
	WARN_ON_ONCE(list_empty(&p->scx.dsq_list.node));

	if (p->scx.dsq_flags & SCX_TASK_DSQ_ON_PRIQ) {
		rb_erase(&p->scx.dsq_priq, &dsq->priq);
		RB_CLEAR_NODE(&p->scx.dsq_priq);
		p->scx.dsq_flags &= ~SCX_TASK_DSQ_ON_PRIQ;
	}

	list_del_init(&p->scx.dsq_list.node);
	dsq_mod_nr(dsq, -1);
}

static void dispatch_dequeue(struct rq *rq, struct task_struct *p)
{
	struct scx_dispatch_q *dsq = p->scx.dsq;
	bool is_local = dsq == &rq->scx.local_dsq;

	if (!dsq) {
		/*
		 * If !dsq && on-list, @p is on @rq's ddsp_deferred_locals.
		 * Unlinking is all that's needed to cancel.
		 */
		if (unlikely(!list_empty(&p->scx.dsq_list.node)))
			list_del_init(&p->scx.dsq_list.node);

		/*
		 * When dispatching directly from the BPF scheduler to a local
		 * DSQ, the task isn't associated with any DSQ but
		 * @p->scx.holding_cpu may be set under the protection of
		 * %SCX_OPSS_DISPATCHING.
		 */
		if (p->scx.holding_cpu >= 0)
			p->scx.holding_cpu = -1;

		return;
	}

	if (!is_local)
		raw_spin_lock(&dsq->lock);

	/*
	 * Now that we hold @dsq->lock, @p->holding_cpu and @p->scx.dsq_* can't
	 * change underneath us.
	*/
	if (p->scx.holding_cpu < 0) {
		/* @p must still be on @dsq, dequeue */
		task_unlink_from_dsq(p, dsq);
	} else {
		/*
		 * We're racing against dispatch_to_local_dsq() which already
		 * removed @p from @dsq and set @p->scx.holding_cpu. Clear the
		 * holding_cpu which tells dispatch_to_local_dsq() that it lost
		 * the race.
		 */
		WARN_ON_ONCE(!list_empty(&p->scx.dsq_list.node));
		p->scx.holding_cpu = -1;
	}
	p->scx.dsq = NULL;

	if (!is_local)
		raw_spin_unlock(&dsq->lock);
}

static struct scx_dispatch_q *find_dsq_for_dispatch(struct scx_sched *sch,
						    struct rq *rq, u64 dsq_id,
						    struct task_struct *p)
{
	struct scx_dispatch_q *dsq;

	if (dsq_id == SCX_DSQ_LOCAL)
		return &rq->scx.local_dsq;

	if ((dsq_id & SCX_DSQ_LOCAL_ON) == SCX_DSQ_LOCAL_ON) {
		s32 cpu = dsq_id & SCX_DSQ_LOCAL_CPU_MASK;

		if (!ops_cpu_valid(sch, cpu, "in SCX_DSQ_LOCAL_ON dispatch verdict"))
			return find_global_dsq(sch, p);

		return &cpu_rq(cpu)->scx.local_dsq;
	}

	if (dsq_id == SCX_DSQ_GLOBAL)
		dsq = find_global_dsq(sch, p);
	else
		dsq = find_user_dsq(sch, dsq_id);

	if (unlikely(!dsq)) {
		scx_error(sch, "non-existent DSQ 0x%llx for %s[%d]",
			  dsq_id, p->comm, p->pid);
		return find_global_dsq(sch, p);
	}

	return dsq;
}

static void mark_direct_dispatch(struct scx_sched *sch,
				 struct task_struct *ddsp_task,
				 struct task_struct *p, u64 dsq_id,
				 u64 enq_flags)
{
	/*
	 * Mark that dispatch already happened from ops.select_cpu() or
	 * ops.enqueue() by spoiling direct_dispatch_task with a non-NULL value
	 * which can never match a valid task pointer.
	 */
	__this_cpu_write(direct_dispatch_task, ERR_PTR(-ESRCH));

	/* @p must match the task on the enqueue path */
	if (unlikely(p != ddsp_task)) {
		if (IS_ERR(ddsp_task))
			scx_error(sch, "%s[%d] already direct-dispatched",
				  p->comm, p->pid);
		else
			scx_error(sch, "scheduling for %s[%d] but trying to direct-dispatch %s[%d]",
				  ddsp_task->comm, ddsp_task->pid,
				  p->comm, p->pid);
		return;
	}

	WARN_ON_ONCE(p->scx.ddsp_dsq_id != SCX_DSQ_INVALID);
	WARN_ON_ONCE(p->scx.ddsp_enq_flags);

	p->scx.ddsp_dsq_id = dsq_id;
	p->scx.ddsp_enq_flags = enq_flags;
}

static void direct_dispatch(struct scx_sched *sch, struct task_struct *p,
			    u64 enq_flags)
{
	struct rq *rq = task_rq(p);
	struct scx_dispatch_q *dsq =
		find_dsq_for_dispatch(sch, rq, p->scx.ddsp_dsq_id, p);

	touch_core_sched_dispatch(rq, p);

	p->scx.ddsp_enq_flags |= enq_flags;

	/*
	 * We are in the enqueue path with @rq locked and pinned, and thus can't
	 * double lock a remote rq and enqueue to its local DSQ. For
	 * DSQ_LOCAL_ON verdicts targeting the local DSQ of a remote CPU, defer
	 * the enqueue so that it's executed when @rq can be unlocked.
	 */
	if (dsq->id == SCX_DSQ_LOCAL && dsq != &rq->scx.local_dsq) {
		unsigned long opss;

		opss = atomic_long_read(&p->scx.ops_state) & SCX_OPSS_STATE_MASK;

		switch (opss & SCX_OPSS_STATE_MASK) {
		case SCX_OPSS_NONE:
			break;
		case SCX_OPSS_QUEUEING:
			/*
			 * As @p was never passed to the BPF side, _release is
			 * not strictly necessary. Still do it for consistency.
			 */
			atomic_long_set_release(&p->scx.ops_state, SCX_OPSS_NONE);
			break;
		default:
			WARN_ONCE(true, "sched_ext: %s[%d] has invalid ops state 0x%lx in direct_dispatch()",
				  p->comm, p->pid, opss);
			atomic_long_set_release(&p->scx.ops_state, SCX_OPSS_NONE);
			break;
		}

		WARN_ON_ONCE(p->scx.dsq || !list_empty(&p->scx.dsq_list.node));
		list_add_tail(&p->scx.dsq_list.node,
			      &rq->scx.ddsp_deferred_locals);
		schedule_deferred(rq);
		return;
	}

	dispatch_enqueue(sch, dsq, p,
			 p->scx.ddsp_enq_flags | SCX_ENQ_CLEAR_OPSS);
}

static bool scx_rq_online(struct rq *rq)
{
	/*
	 * Test both cpu_active() and %SCX_RQ_ONLINE. %SCX_RQ_ONLINE indicates
	 * the online state as seen from the BPF scheduler. cpu_active() test
	 * guarantees that, if this function returns %true, %SCX_RQ_ONLINE will
	 * stay set until the current scheduling operation is complete even if
	 * we aren't locking @rq.
	 */
	return likely((rq->scx.flags & SCX_RQ_ONLINE) && cpu_active(cpu_of(rq)));
}

static void do_enqueue_task(struct rq *rq, struct task_struct *p, u64 enq_flags,
			    int sticky_cpu)
{
	struct scx_sched *sch = scx_root;
	struct task_struct **ddsp_taskp;
	unsigned long qseq;

	WARN_ON_ONCE(!(p->scx.flags & SCX_TASK_QUEUED));

	/* rq migration */
	if (sticky_cpu == cpu_of(rq))
		goto local_norefill;

	/*
	 * If !scx_rq_online(), we already told the BPF scheduler that the CPU
	 * is offline and are just running the hotplug path. Don't bother the
	 * BPF scheduler.
	 */
	if (!scx_rq_online(rq))
		goto local;

	if (scx_rq_bypassing(rq)) {
		__scx_add_event(sch, SCX_EV_BYPASS_DISPATCH, 1);
		goto global;
	}

	if (p->scx.ddsp_dsq_id != SCX_DSQ_INVALID)
		goto direct;

	/* see %SCX_OPS_ENQ_EXITING */
	if (!(sch->ops.flags & SCX_OPS_ENQ_EXITING) &&
	    unlikely(p->flags & PF_EXITING)) {
		__scx_add_event(sch, SCX_EV_ENQ_SKIP_EXITING, 1);
		goto local;
	}

	/* see %SCX_OPS_ENQ_MIGRATION_DISABLED */
	if (!(sch->ops.flags & SCX_OPS_ENQ_MIGRATION_DISABLED) &&
	    is_migration_disabled(p)) {
		__scx_add_event(sch, SCX_EV_ENQ_SKIP_MIGRATION_DISABLED, 1);
		goto local;
	}

	if (unlikely(!SCX_HAS_OP(sch, enqueue)))
		goto global;

	/* DSQ bypass didn't trigger, enqueue on the BPF scheduler */
	qseq = rq->scx.ops_qseq++ << SCX_OPSS_QSEQ_SHIFT;

	WARN_ON_ONCE(atomic_long_read(&p->scx.ops_state) != SCX_OPSS_NONE);
	atomic_long_set(&p->scx.ops_state, SCX_OPSS_QUEUEING | qseq);

	ddsp_taskp = this_cpu_ptr(&direct_dispatch_task);
	WARN_ON_ONCE(*ddsp_taskp);
	*ddsp_taskp = p;

	SCX_CALL_OP_TASK(sch, SCX_KF_ENQUEUE, enqueue, rq, p, enq_flags);

	*ddsp_taskp = NULL;
	if (p->scx.ddsp_dsq_id != SCX_DSQ_INVALID)
		goto direct;

	/*
	 * If not directly dispatched, QUEUEING isn't clear yet and dispatch or
	 * dequeue may be waiting. The store_release matches their load_acquire.
	 */
	atomic_long_set_release(&p->scx.ops_state, SCX_OPSS_QUEUED | qseq);
	return;

direct:
	direct_dispatch(sch, p, enq_flags);
	return;

local:
	/*
	 * For task-ordering, slice refill must be treated as implying the end
	 * of the current slice. Otherwise, the longer @p stays on the CPU, the
	 * higher priority it becomes from scx_prio_less()'s POV.
	 */
	touch_core_sched(rq, p);
	refill_task_slice_dfl(sch, p);
local_norefill:
	dispatch_enqueue(sch, &rq->scx.local_dsq, p, enq_flags);
	return;

global:
	touch_core_sched(rq, p);	/* see the comment in local: */
	refill_task_slice_dfl(sch, p);
	dispatch_enqueue(sch, find_global_dsq(sch, p), p, enq_flags);
}

static bool task_runnable(const struct task_struct *p)
{
	return !list_empty(&p->scx.runnable_node);
}

static void set_task_runnable(struct rq *rq, struct task_struct *p)
{
	lockdep_assert_rq_held(rq);

	if (p->scx.flags & SCX_TASK_RESET_RUNNABLE_AT) {
		p->scx.runnable_at = jiffies;
		p->scx.flags &= ~SCX_TASK_RESET_RUNNABLE_AT;
	}

	/*
	 * list_add_tail() must be used. scx_bypass() depends on tasks being
	 * appended to the runnable_list.
	 */
	list_add_tail(&p->scx.runnable_node, &rq->scx.runnable_list);
}

static void clr_task_runnable(struct task_struct *p, bool reset_runnable_at)
{
	list_del_init(&p->scx.runnable_node);
	if (reset_runnable_at)
		p->scx.flags |= SCX_TASK_RESET_RUNNABLE_AT;
}

static void enqueue_task_scx(struct rq *rq, struct task_struct *p, int enq_flags)
{
	struct scx_sched *sch = scx_root;
	int sticky_cpu = p->scx.sticky_cpu;

	if (enq_flags & ENQUEUE_WAKEUP)
		rq->scx.flags |= SCX_RQ_IN_WAKEUP;

	enq_flags |= rq->scx.extra_enq_flags;

	if (sticky_cpu >= 0)
		p->scx.sticky_cpu = -1;

	/*
	 * Restoring a running task will be immediately followed by
	 * set_next_task_scx() which expects the task to not be on the BPF
	 * scheduler as tasks can only start running through local DSQs. Force
	 * direct-dispatch into the local DSQ by setting the sticky_cpu.
	 */
	if (unlikely(enq_flags & ENQUEUE_RESTORE) && task_current(rq, p))
		sticky_cpu = cpu_of(rq);

	if (p->scx.flags & SCX_TASK_QUEUED) {
		WARN_ON_ONCE(!task_runnable(p));
		goto out;
	}

	set_task_runnable(rq, p);
	p->scx.flags |= SCX_TASK_QUEUED;
	rq->scx.nr_running++;
	add_nr_running(rq, 1);

	if (SCX_HAS_OP(sch, runnable) && !task_on_rq_migrating(p))
		SCX_CALL_OP_TASK(sch, SCX_KF_REST, runnable, rq, p, enq_flags);

	if (enq_flags & SCX_ENQ_WAKEUP)
		touch_core_sched(rq, p);

	do_enqueue_task(rq, p, enq_flags, sticky_cpu);
out:
	rq->scx.flags &= ~SCX_RQ_IN_WAKEUP;

	if ((enq_flags & SCX_ENQ_CPU_SELECTED) &&
	    unlikely(cpu_of(rq) != p->scx.selected_cpu))
		__scx_add_event(sch, SCX_EV_SELECT_CPU_FALLBACK, 1);
}

static void ops_dequeue(struct rq *rq, struct task_struct *p, u64 deq_flags)
{
	struct scx_sched *sch = scx_root;
	unsigned long opss;

	/* dequeue is always temporary, don't reset runnable_at */
	clr_task_runnable(p, false);

	/* acquire ensures that we see the preceding updates on QUEUED */
	opss = atomic_long_read_acquire(&p->scx.ops_state);

	switch (opss & SCX_OPSS_STATE_MASK) {
	case SCX_OPSS_NONE:
		break;
	case SCX_OPSS_QUEUEING:
		/*
		 * QUEUEING is started and finished while holding @p's rq lock.
		 * As we're holding the rq lock now, we shouldn't see QUEUEING.
		 */
		BUG();
	case SCX_OPSS_QUEUED:
		if (SCX_HAS_OP(sch, dequeue))
			SCX_CALL_OP_TASK(sch, SCX_KF_REST, dequeue, rq,
					 p, deq_flags);

		if (atomic_long_try_cmpxchg(&p->scx.ops_state, &opss,
					    SCX_OPSS_NONE))
			break;
		fallthrough;
	case SCX_OPSS_DISPATCHING:
		/*
		 * If @p is being dispatched from the BPF scheduler to a DSQ,
		 * wait for the transfer to complete so that @p doesn't get
		 * added to its DSQ after dequeueing is complete.
		 *
		 * As we're waiting on DISPATCHING with the rq locked, the
		 * dispatching side shouldn't try to lock the rq while
		 * DISPATCHING is set. See dispatch_to_local_dsq().
		 *
		 * DISPATCHING shouldn't have qseq set and control can reach
		 * here with NONE @opss from the above QUEUED case block.
		 * Explicitly wait on %SCX_OPSS_DISPATCHING instead of @opss.
		 */
		wait_ops_state(p, SCX_OPSS_DISPATCHING);
		BUG_ON(atomic_long_read(&p->scx.ops_state) != SCX_OPSS_NONE);
		break;
	}
}

static bool dequeue_task_scx(struct rq *rq, struct task_struct *p, int deq_flags)
{
	struct scx_sched *sch = scx_root;

	if (!(p->scx.flags & SCX_TASK_QUEUED)) {
		WARN_ON_ONCE(task_runnable(p));
		return true;
	}

	ops_dequeue(rq, p, deq_flags);

	/*
	 * A currently running task which is going off @rq first gets dequeued
	 * and then stops running. As we want running <-> stopping transitions
	 * to be contained within runnable <-> quiescent transitions, trigger
	 * ->stopping() early here instead of in put_prev_task_scx().
	 *
	 * @p may go through multiple stopping <-> running transitions between
	 * here and put_prev_task_scx() if task attribute changes occur while
	 * balance_scx() leaves @rq unlocked. However, they don't contain any
	 * information meaningful to the BPF scheduler and can be suppressed by
	 * skipping the callbacks if the task is !QUEUED.
	 */
	if (SCX_HAS_OP(sch, stopping) && task_current(rq, p)) {
		update_curr_scx(rq);
		SCX_CALL_OP_TASK(sch, SCX_KF_REST, stopping, rq, p, false);
	}

	if (SCX_HAS_OP(sch, quiescent) && !task_on_rq_migrating(p))
		SCX_CALL_OP_TASK(sch, SCX_KF_REST, quiescent, rq, p, deq_flags);

	if (deq_flags & SCX_DEQ_SLEEP)
		p->scx.flags |= SCX_TASK_DEQD_FOR_SLEEP;
	else
		p->scx.flags &= ~SCX_TASK_DEQD_FOR_SLEEP;

	p->scx.flags &= ~SCX_TASK_QUEUED;
	rq->scx.nr_running--;
	sub_nr_running(rq, 1);

	dispatch_dequeue(rq, p);
	return true;
}

static void yield_task_scx(struct rq *rq)
{
	struct scx_sched *sch = scx_root;
	struct task_struct *p = rq->curr;

	if (SCX_HAS_OP(sch, yield))
		SCX_CALL_OP_2TASKS_RET(sch, SCX_KF_REST, yield, rq, p, NULL);
	else
		p->scx.slice = 0;
}

static bool yield_to_task_scx(struct rq *rq, struct task_struct *to)
{
	struct scx_sched *sch = scx_root;
	struct task_struct *from = rq->curr;

	if (SCX_HAS_OP(sch, yield))
		return SCX_CALL_OP_2TASKS_RET(sch, SCX_KF_REST, yield, rq,
					      from, to);
	else
		return false;
}

static void move_local_task_to_local_dsq(struct task_struct *p, u64 enq_flags,
					 struct scx_dispatch_q *src_dsq,
					 struct rq *dst_rq)
{
	struct scx_dispatch_q *dst_dsq = &dst_rq->scx.local_dsq;

	/* @dsq is locked and @p is on @dst_rq */
	lockdep_assert_held(&src_dsq->lock);
	lockdep_assert_rq_held(dst_rq);

	WARN_ON_ONCE(p->scx.holding_cpu >= 0);

	if (enq_flags & (SCX_ENQ_HEAD | SCX_ENQ_PREEMPT))
		list_add(&p->scx.dsq_list.node, &dst_dsq->list);
	else
		list_add_tail(&p->scx.dsq_list.node, &dst_dsq->list);

	dsq_mod_nr(dst_dsq, 1);
	p->scx.dsq = dst_dsq;
}

/**
 * move_remote_task_to_local_dsq - Move a task from a foreign rq to a local DSQ
 * @p: task to move
 * @enq_flags: %SCX_ENQ_*
 * @src_rq: rq to move the task from, locked on entry, released on return
 * @dst_rq: rq to move the task into, locked on return
 *
 * Move @p which is currently on @src_rq to @dst_rq's local DSQ.
 */
static void move_remote_task_to_local_dsq(struct task_struct *p, u64 enq_flags,
					  struct rq *src_rq, struct rq *dst_rq)
{
	lockdep_assert_rq_held(src_rq);

	/* the following marks @p MIGRATING which excludes dequeue */
	deactivate_task(src_rq, p, 0);
	set_task_cpu(p, cpu_of(dst_rq));
	p->scx.sticky_cpu = cpu_of(dst_rq);

	raw_spin_rq_unlock(src_rq);
	raw_spin_rq_lock(dst_rq);

	/*
	 * We want to pass scx-specific enq_flags but activate_task() will
	 * truncate the upper 32 bit. As we own @rq, we can pass them through
	 * @rq->scx.extra_enq_flags instead.
	 */
	WARN_ON_ONCE(!cpumask_test_cpu(cpu_of(dst_rq), p->cpus_ptr));
	WARN_ON_ONCE(dst_rq->scx.extra_enq_flags);
	dst_rq->scx.extra_enq_flags = enq_flags;
	activate_task(dst_rq, p, 0);
	dst_rq->scx.extra_enq_flags = 0;
}

/*
 * Similar to kernel/sched/core.c::is_cpu_allowed(). However, there are two
 * differences:
 *
 * - is_cpu_allowed() asks "Can this task run on this CPU?" while
 *   task_can_run_on_remote_rq() asks "Can the BPF scheduler migrate the task to
 *   this CPU?".
 *
 *   While migration is disabled, is_cpu_allowed() has to say "yes" as the task
 *   must be allowed to finish on the CPU that it's currently on regardless of
 *   the CPU state. However, task_can_run_on_remote_rq() must say "no" as the
 *   BPF scheduler shouldn't attempt to migrate a task which has migration
 *   disabled.
 *
 * - The BPF scheduler is bypassed while the rq is offline and we can always say
 *   no to the BPF scheduler initiated migrations while offline.
 *
 * The caller must ensure that @p and @rq are on different CPUs.
 */
static bool task_can_run_on_remote_rq(struct scx_sched *sch,
				      struct task_struct *p, struct rq *rq,
				      bool enforce)
{
	int cpu = cpu_of(rq);

	WARN_ON_ONCE(task_cpu(p) == cpu);

	/*
	 * If @p has migration disabled, @p->cpus_ptr is updated to contain only
	 * the pinned CPU in migrate_disable_switch() while @p is being switched
	 * out. However, put_prev_task_scx() is called before @p->cpus_ptr is
	 * updated and thus another CPU may see @p on a DSQ inbetween leading to
	 * @p passing the below task_allowed_on_cpu() check while migration is
	 * disabled.
	 *
	 * Test the migration disabled state first as the race window is narrow
	 * and the BPF scheduler failing to check migration disabled state can
	 * easily be masked if task_allowed_on_cpu() is done first.
	 */
	if (unlikely(is_migration_disabled(p))) {
		if (enforce)
			scx_error(sch, "SCX_DSQ_LOCAL[_ON] cannot move migration disabled %s[%d] from CPU %d to %d",
				  p->comm, p->pid, task_cpu(p), cpu);
		return false;
	}

	/*
	 * We don't require the BPF scheduler to avoid dispatching to offline
	 * CPUs mostly for convenience but also because CPUs can go offline
	 * between scx_bpf_dsq_insert() calls and here. Trigger error iff the
	 * picked CPU is outside the allowed mask.
	 */
	if (!task_allowed_on_cpu(p, cpu)) {
		if (enforce)
			scx_error(sch, "SCX_DSQ_LOCAL[_ON] target CPU %d not allowed for %s[%d]",
				  cpu, p->comm, p->pid);
		return false;
	}

	if (!scx_rq_online(rq)) {
		if (enforce)
			__scx_add_event(sch, SCX_EV_DISPATCH_LOCAL_DSQ_OFFLINE, 1);
		return false;
	}

	return true;
}

/**
 * unlink_dsq_and_lock_src_rq() - Unlink task from its DSQ and lock its task_rq
 * @p: target task
 * @dsq: locked DSQ @p is currently on
 * @src_rq: rq @p is currently on, stable with @dsq locked
 *
 * Called with @dsq locked but no rq's locked. We want to move @p to a different
 * DSQ, including any local DSQ, but are not locking @src_rq. Locking @src_rq is
 * required when transferring into a local DSQ. Even when transferring into a
 * non-local DSQ, it's better to use the same mechanism to protect against
 * dequeues and maintain the invariant that @p->scx.dsq can only change while
 * @src_rq is locked, which e.g. scx_dump_task() depends on.
 *
 * We want to grab @src_rq but that can deadlock if we try while locking @dsq,
 * so we want to unlink @p from @dsq, drop its lock and then lock @src_rq. As
 * this may race with dequeue, which can't drop the rq lock or fail, do a little
 * dancing from our side.
 *
 * @p->scx.holding_cpu is set to this CPU before @dsq is unlocked. If @p gets
 * dequeued after we unlock @dsq but before locking @src_rq, the holding_cpu
 * would be cleared to -1. While other cpus may have updated it to different
 * values afterwards, as this operation can't be preempted or recurse, the
 * holding_cpu can never become this CPU again before we're done. Thus, we can
 * tell whether we lost to dequeue by testing whether the holding_cpu still
 * points to this CPU. See dispatch_dequeue() for the counterpart.
 *
 * On return, @dsq is unlocked and @src_rq is locked. Returns %true if @p is
 * still valid. %false if lost to dequeue.
 */
static bool unlink_dsq_and_lock_src_rq(struct task_struct *p,
				       struct scx_dispatch_q *dsq,
				       struct rq *src_rq)
{
	s32 cpu = raw_smp_processor_id();

	lockdep_assert_held(&dsq->lock);

	WARN_ON_ONCE(p->scx.holding_cpu >= 0);
	task_unlink_from_dsq(p, dsq);
	p->scx.holding_cpu = cpu;

	raw_spin_unlock(&dsq->lock);
	raw_spin_rq_lock(src_rq);

	/* task_rq couldn't have changed if we're still the holding cpu */
	return likely(p->scx.holding_cpu == cpu) &&
		!WARN_ON_ONCE(src_rq != task_rq(p));
}

static bool consume_remote_task(struct rq *this_rq, struct task_struct *p,
				struct scx_dispatch_q *dsq, struct rq *src_rq)
{
	raw_spin_rq_unlock(this_rq);

	if (unlink_dsq_and_lock_src_rq(p, dsq, src_rq)) {
		move_remote_task_to_local_dsq(p, 0, src_rq, this_rq);
		return true;
	} else {
		raw_spin_rq_unlock(src_rq);
		raw_spin_rq_lock(this_rq);
		return false;
	}
}

/**
 * move_task_between_dsqs() - Move a task from one DSQ to another
 * @sch: scx_sched being operated on
 * @p: target task
 * @enq_flags: %SCX_ENQ_*
 * @src_dsq: DSQ @p is currently on, must not be a local DSQ
 * @dst_dsq: DSQ @p is being moved to, can be any DSQ
 *
 * Must be called with @p's task_rq and @src_dsq locked. If @dst_dsq is a local
 * DSQ and @p is on a different CPU, @p will be migrated and thus its task_rq
 * will change. As @p's task_rq is locked, this function doesn't need to use the
 * holding_cpu mechanism.
 *
 * On return, @src_dsq is unlocked and only @p's new task_rq, which is the
 * return value, is locked.
 */
static struct rq *move_task_between_dsqs(struct scx_sched *sch,
					 struct task_struct *p, u64 enq_flags,
					 struct scx_dispatch_q *src_dsq,
					 struct scx_dispatch_q *dst_dsq)
{
	struct rq *src_rq = task_rq(p), *dst_rq;

	BUG_ON(src_dsq->id == SCX_DSQ_LOCAL);
	lockdep_assert_held(&src_dsq->lock);
	lockdep_assert_rq_held(src_rq);

	if (dst_dsq->id == SCX_DSQ_LOCAL) {
		dst_rq = container_of(dst_dsq, struct rq, scx.local_dsq);
		if (src_rq != dst_rq &&
		    unlikely(!task_can_run_on_remote_rq(sch, p, dst_rq, true))) {
			dst_dsq = find_global_dsq(sch, p);
			dst_rq = src_rq;
		}
	} else {
		/* no need to migrate if destination is a non-local DSQ */
		dst_rq = src_rq;
	}

	/*
	 * Move @p into $dst_dsq. If $dst_dsq is the local DSQ of a different
	 * CPU, @p will be migrated.
	 */
	if (dst_dsq->id == SCX_DSQ_LOCAL) {
		/* @p is going from a non-local DSQ to a local DSQ */
		if (src_rq == dst_rq) {
			task_unlink_from_dsq(p, src_dsq);
			move_local_task_to_local_dsq(p, enq_flags,
						     src_dsq, dst_rq);
			raw_spin_unlock(&src_dsq->lock);
		} else {
			raw_spin_unlock(&src_dsq->lock);
			move_remote_task_to_local_dsq(p, enq_flags,
						      src_rq, dst_rq);
		}
	} else {
		/*
		 * @p is going from a non-local DSQ to a non-local DSQ. As
		 * $src_dsq is already locked, do an abbreviated dequeue.
		 */
		task_unlink_from_dsq(p, src_dsq);
		p->scx.dsq = NULL;
		raw_spin_unlock(&src_dsq->lock);

		dispatch_enqueue(sch, dst_dsq, p, enq_flags);
	}

	return dst_rq;
}

/*
 * A poorly behaving BPF scheduler can live-lock the system by e.g. incessantly
 * banging on the same DSQ on a large NUMA system to the point where switching
 * to the bypass mode can take a long time. Inject artificial delays while the
 * bypass mode is switching to guarantee timely completion.
 */
static void scx_breather(struct rq *rq)
{
	u64 until;

	lockdep_assert_rq_held(rq);

	if (likely(!atomic_read(&scx_breather_depth)))
		return;

	raw_spin_rq_unlock(rq);

	until = ktime_get_ns() + NSEC_PER_MSEC;

	do {
		int cnt = 1024;
		while (atomic_read(&scx_breather_depth) && --cnt)
			cpu_relax();
	} while (atomic_read(&scx_breather_depth) &&
		 time_before64(ktime_get_ns(), until));

	raw_spin_rq_lock(rq);
}

static bool consume_dispatch_q(struct scx_sched *sch, struct rq *rq,
			       struct scx_dispatch_q *dsq)
{
	struct task_struct *p;
retry:
	/*
	 * This retry loop can repeatedly race against scx_bypass() dequeueing
	 * tasks from @dsq trying to put the system into the bypass mode. On
	 * some multi-socket machines (e.g. 2x Intel 8480c), this can live-lock
	 * the machine into soft lockups. Give a breather.
	 */
	scx_breather(rq);

	/*
	 * The caller can't expect to successfully consume a task if the task's
	 * addition to @dsq isn't guaranteed to be visible somehow. Test
	 * @dsq->list without locking and skip if it seems empty.
	 */
	if (list_empty(&dsq->list))
		return false;

	raw_spin_lock(&dsq->lock);

	nldsq_for_each_task(p, dsq) {
		struct rq *task_rq = task_rq(p);

		if (rq == task_rq) {
			task_unlink_from_dsq(p, dsq);
			move_local_task_to_local_dsq(p, 0, dsq, rq);
			raw_spin_unlock(&dsq->lock);
			return true;
		}

		if (task_can_run_on_remote_rq(sch, p, rq, false)) {
			if (likely(consume_remote_task(rq, p, dsq, task_rq)))
				return true;
			goto retry;
		}
	}

	raw_spin_unlock(&dsq->lock);
	return false;
}

static bool consume_global_dsq(struct scx_sched *sch, struct rq *rq)
{
	int node = cpu_to_node(cpu_of(rq));

	return consume_dispatch_q(sch, rq, sch->global_dsqs[node]);
}

/**
 * dispatch_to_local_dsq - Dispatch a task to a local dsq
 * @sch: scx_sched being operated on
 * @rq: current rq which is locked
 * @dst_dsq: destination DSQ
 * @p: task to dispatch
 * @enq_flags: %SCX_ENQ_*
 *
 * We're holding @rq lock and want to dispatch @p to @dst_dsq which is a local
 * DSQ. This function performs all the synchronization dancing needed because
 * local DSQs are protected with rq locks.
 *
 * The caller must have exclusive ownership of @p (e.g. through
 * %SCX_OPSS_DISPATCHING).
 */
static void dispatch_to_local_dsq(struct scx_sched *sch, struct rq *rq,
				  struct scx_dispatch_q *dst_dsq,
				  struct task_struct *p, u64 enq_flags)
{
	struct rq *src_rq = task_rq(p);
	struct rq *dst_rq = container_of(dst_dsq, struct rq, scx.local_dsq);
	struct rq *locked_rq = rq;

	/*
	 * We're synchronized against dequeue through DISPATCHING. As @p can't
	 * be dequeued, its task_rq and cpus_allowed are stable too.
	 *
	 * If dispatching to @rq that @p is already on, no lock dancing needed.
	 */
	if (rq == src_rq && rq == dst_rq) {
		dispatch_enqueue(sch, dst_dsq, p,
				 enq_flags | SCX_ENQ_CLEAR_OPSS);
		return;
	}

	if (src_rq != dst_rq &&
	    unlikely(!task_can_run_on_remote_rq(sch, p, dst_rq, true))) {
		dispatch_enqueue(sch, find_global_dsq(sch, p), p,
				 enq_flags | SCX_ENQ_CLEAR_OPSS);
		return;
	}

	/*
	 * @p is on a possibly remote @src_rq which we need to lock to move the
	 * task. If dequeue is in progress, it'd be locking @src_rq and waiting
	 * on DISPATCHING, so we can't grab @src_rq lock while holding
	 * DISPATCHING.
	 *
	 * As DISPATCHING guarantees that @p is wholly ours, we can pretend that
	 * we're moving from a DSQ and use the same mechanism - mark the task
	 * under transfer with holding_cpu, release DISPATCHING and then follow
	 * the same protocol. See unlink_dsq_and_lock_src_rq().
	 */
	p->scx.holding_cpu = raw_smp_processor_id();

	/* store_release ensures that dequeue sees the above */
	atomic_long_set_release(&p->scx.ops_state, SCX_OPSS_NONE);

	/* switch to @src_rq lock */
	if (locked_rq != src_rq) {
		raw_spin_rq_unlock(locked_rq);
		locked_rq = src_rq;
		raw_spin_rq_lock(src_rq);
	}

	/* task_rq couldn't have changed if we're still the holding cpu */
	if (likely(p->scx.holding_cpu == raw_smp_processor_id()) &&
	    !WARN_ON_ONCE(src_rq != task_rq(p))) {
		/*
		 * If @p is staying on the same rq, there's no need to go
		 * through the full deactivate/activate cycle. Optimize by
		 * abbreviating move_remote_task_to_local_dsq().
		 */
		if (src_rq == dst_rq) {
			p->scx.holding_cpu = -1;
			dispatch_enqueue(sch, &dst_rq->scx.local_dsq, p,
					 enq_flags);
		} else {
			move_remote_task_to_local_dsq(p, enq_flags,
						      src_rq, dst_rq);
			/* task has been moved to dst_rq, which is now locked */
			locked_rq = dst_rq;
		}

		/* if the destination CPU is idle, wake it up */
		if (sched_class_above(p->sched_class, dst_rq->curr->sched_class))
			resched_curr(dst_rq);
	}

	/* switch back to @rq lock */
	if (locked_rq != rq) {
		raw_spin_rq_unlock(locked_rq);
		raw_spin_rq_lock(rq);
	}
}

/**
 * finish_dispatch - Asynchronously finish dispatching a task
 * @rq: current rq which is locked
 * @p: task to finish dispatching
 * @qseq_at_dispatch: qseq when @p started getting dispatched
 * @dsq_id: destination DSQ ID
 * @enq_flags: %SCX_ENQ_*
 *
 * Dispatching to local DSQs may need to wait for queueing to complete or
 * require rq lock dancing. As we don't wanna do either while inside
 * ops.dispatch() to avoid locking order inversion, we split dispatching into
 * two parts. scx_bpf_dsq_insert() which is called by ops.dispatch() records the
 * task and its qseq. Once ops.dispatch() returns, this function is called to
 * finish up.
 *
 * There is no guarantee that @p is still valid for dispatching or even that it
 * was valid in the first place. Make sure that the task is still owned by the
 * BPF scheduler and claim the ownership before dispatching.
 */
static void finish_dispatch(struct scx_sched *sch, struct rq *rq,
			    struct task_struct *p,
			    unsigned long qseq_at_dispatch,
			    u64 dsq_id, u64 enq_flags)
{
	struct scx_dispatch_q *dsq;
	unsigned long opss;

	touch_core_sched_dispatch(rq, p);
retry:
	/*
	 * No need for _acquire here. @p is accessed only after a successful
	 * try_cmpxchg to DISPATCHING.
	 */
	opss = atomic_long_read(&p->scx.ops_state);

	switch (opss & SCX_OPSS_STATE_MASK) {
	case SCX_OPSS_DISPATCHING:
	case SCX_OPSS_NONE:
		/* someone else already got to it */
		return;
	case SCX_OPSS_QUEUED:
		/*
		 * If qseq doesn't match, @p has gone through at least one
		 * dispatch/dequeue and re-enqueue cycle between
		 * scx_bpf_dsq_insert() and here and we have no claim on it.
		 */
		if ((opss & SCX_OPSS_QSEQ_MASK) != qseq_at_dispatch)
			return;

		/*
		 * While we know @p is accessible, we don't yet have a claim on
		 * it - the BPF scheduler is allowed to dispatch tasks
		 * spuriously and there can be a racing dequeue attempt. Let's
		 * claim @p by atomically transitioning it from QUEUED to
		 * DISPATCHING.
		 */
		if (likely(atomic_long_try_cmpxchg(&p->scx.ops_state, &opss,
						   SCX_OPSS_DISPATCHING)))
			break;
		goto retry;
	case SCX_OPSS_QUEUEING:
		/*
		 * do_enqueue_task() is in the process of transferring the task
		 * to the BPF scheduler while holding @p's rq lock. As we aren't
		 * holding any kernel or BPF resource that the enqueue path may
		 * depend upon, it's safe to wait.
		 */
		wait_ops_state(p, opss);
		goto retry;
	}

	BUG_ON(!(p->scx.flags & SCX_TASK_QUEUED));

	dsq = find_dsq_for_dispatch(sch, this_rq(), dsq_id, p);

	if (dsq->id == SCX_DSQ_LOCAL)
		dispatch_to_local_dsq(sch, rq, dsq, p, enq_flags);
	else
		dispatch_enqueue(sch, dsq, p, enq_flags | SCX_ENQ_CLEAR_OPSS);
}

static void flush_dispatch_buf(struct scx_sched *sch, struct rq *rq)
{
	struct scx_dsp_ctx *dspc = this_cpu_ptr(scx_dsp_ctx);
	u32 u;

	for (u = 0; u < dspc->cursor; u++) {
		struct scx_dsp_buf_ent *ent = &dspc->buf[u];

		finish_dispatch(sch, rq, ent->task, ent->qseq, ent->dsq_id,
				ent->enq_flags);
	}

	dspc->nr_tasks += dspc->cursor;
	dspc->cursor = 0;
}

static inline void maybe_queue_balance_callback(struct rq *rq)
{
	lockdep_assert_rq_held(rq);

	if (!(rq->scx.flags & SCX_RQ_BAL_CB_PENDING))
		return;

	queue_balance_callback(rq, &rq->scx.deferred_bal_cb,
				deferred_bal_cb_workfn);

	rq->scx.flags &= ~SCX_RQ_BAL_CB_PENDING;
}

static int balance_one(struct rq *rq, struct task_struct *prev)
{
	struct scx_sched *sch = scx_root;
	struct scx_dsp_ctx *dspc = this_cpu_ptr(scx_dsp_ctx);
	bool prev_on_scx = prev->sched_class == &ext_sched_class;
	bool prev_on_rq = prev->scx.flags & SCX_TASK_QUEUED;
	int nr_loops = SCX_DSP_MAX_LOOPS;

	lockdep_assert_rq_held(rq);
	rq->scx.flags |= SCX_RQ_IN_BALANCE;
	rq->scx.flags &= ~(SCX_RQ_BAL_PENDING | SCX_RQ_BAL_KEEP);

	if ((sch->ops.flags & SCX_OPS_HAS_CPU_PREEMPT) &&
	    unlikely(rq->scx.cpu_released)) {
		/*
		 * If the previous sched_class for the current CPU was not SCX,
		 * notify the BPF scheduler that it again has control of the
		 * core. This callback complements ->cpu_release(), which is
		 * emitted in switch_class().
		 */
		if (SCX_HAS_OP(sch, cpu_acquire))
			SCX_CALL_OP(sch, SCX_KF_REST, cpu_acquire, rq,
				    cpu_of(rq), NULL);
		rq->scx.cpu_released = false;
	}

	if (prev_on_scx) {
		update_curr_scx(rq);

		/*
		 * If @prev is runnable & has slice left, it has priority and
		 * fetching more just increases latency for the fetched tasks.
		 * Tell pick_task_scx() to keep running @prev. If the BPF
		 * scheduler wants to handle this explicitly, it should
		 * implement ->cpu_release().
		 *
		 * See scx_disable_workfn() for the explanation on the bypassing
		 * test.
		 */
		if (prev_on_rq && prev->scx.slice && !scx_rq_bypassing(rq)) {
			rq->scx.flags |= SCX_RQ_BAL_KEEP;
			goto has_tasks;
		}
	}

	/* if there already are tasks to run, nothing to do */
	if (rq->scx.local_dsq.nr)
		goto has_tasks;

	if (consume_global_dsq(sch, rq))
		goto has_tasks;

	if (unlikely(!SCX_HAS_OP(sch, dispatch)) ||
	    scx_rq_bypassing(rq) || !scx_rq_online(rq))
		goto no_tasks;

	dspc->rq = rq;

	/*
	 * The dispatch loop. Because flush_dispatch_buf() may drop the rq lock,
	 * the local DSQ might still end up empty after a successful
	 * ops.dispatch(). If the local DSQ is empty even after ops.dispatch()
	 * produced some tasks, retry. The BPF scheduler may depend on this
	 * looping behavior to simplify its implementation.
	 */
	do {
		dspc->nr_tasks = 0;

		SCX_CALL_OP(sch, SCX_KF_DISPATCH, dispatch, rq,
			    cpu_of(rq), prev_on_scx ? prev : NULL);

		flush_dispatch_buf(sch, rq);

		if (prev_on_rq && prev->scx.slice) {
			rq->scx.flags |= SCX_RQ_BAL_KEEP;
			goto has_tasks;
		}
		if (rq->scx.local_dsq.nr)
			goto has_tasks;
		if (consume_global_dsq(sch, rq))
			goto has_tasks;

		/*
		 * ops.dispatch() can trap us in this loop by repeatedly
		 * dispatching ineligible tasks. Break out once in a while to
		 * allow the watchdog to run. As IRQ can't be enabled in
		 * balance(), we want to complete this scheduling cycle and then
		 * start a new one. IOW, we want to call resched_curr() on the
		 * next, most likely idle, task, not the current one. Use
		 * scx_kick_cpu() for deferred kicking.
		 */
		if (unlikely(!--nr_loops)) {
			scx_kick_cpu(sch, cpu_of(rq), 0);
			break;
		}
	} while (dspc->nr_tasks);

no_tasks:
	/*
	 * Didn't find another task to run. Keep running @prev unless
	 * %SCX_OPS_ENQ_LAST is in effect.
	 */
	if (prev_on_rq &&
	    (!(sch->ops.flags & SCX_OPS_ENQ_LAST) || scx_rq_bypassing(rq))) {
		rq->scx.flags |= SCX_RQ_BAL_KEEP;
		__scx_add_event(sch, SCX_EV_DISPATCH_KEEP_LAST, 1);
		goto has_tasks;
	}
	rq->scx.flags &= ~SCX_RQ_IN_BALANCE;
	return false;

has_tasks:
	rq->scx.flags &= ~SCX_RQ_IN_BALANCE;
	return true;
}

static int balance_scx(struct rq *rq, struct task_struct *prev,
		       struct rq_flags *rf)
{
	int ret;

	rq_unpin_lock(rq, rf);

	ret = balance_one(rq, prev);

#ifdef CONFIG_SCHED_SMT
	/*
	 * When core-sched is enabled, this ops.balance() call will be followed
	 * by pick_task_scx() on this CPU and the SMT siblings. Balance the
	 * siblings too.
	 */
	if (sched_core_enabled(rq)) {
		const struct cpumask *smt_mask = cpu_smt_mask(cpu_of(rq));
		int scpu;

		for_each_cpu_andnot(scpu, smt_mask, cpumask_of(cpu_of(rq))) {
			struct rq *srq = cpu_rq(scpu);
			struct task_struct *sprev = srq->curr;

			WARN_ON_ONCE(__rq_lockp(rq) != __rq_lockp(srq));
			update_rq_clock(srq);
			balance_one(srq, sprev);
		}
	}
#endif
	rq_repin_lock(rq, rf);

	maybe_queue_balance_callback(rq);

	return ret;
}

static void process_ddsp_deferred_locals(struct rq *rq)
{
	struct task_struct *p;

	lockdep_assert_rq_held(rq);

	/*
	 * Now that @rq can be unlocked, execute the deferred enqueueing of
	 * tasks directly dispatched to the local DSQs of other CPUs. See
	 * direct_dispatch(). Keep popping from the head instead of using
	 * list_for_each_entry_safe() as dispatch_local_dsq() may unlock @rq
	 * temporarily.
	 */
	while ((p = list_first_entry_or_null(&rq->scx.ddsp_deferred_locals,
				struct task_struct, scx.dsq_list.node))) {
		struct scx_sched *sch = scx_root;
		struct scx_dispatch_q *dsq;

		list_del_init(&p->scx.dsq_list.node);

		dsq = find_dsq_for_dispatch(sch, rq, p->scx.ddsp_dsq_id, p);
		if (!WARN_ON_ONCE(dsq->id != SCX_DSQ_LOCAL))
			dispatch_to_local_dsq(sch, rq, dsq, p,
					      p->scx.ddsp_enq_flags);
	}
}

static void set_next_task_scx(struct rq *rq, struct task_struct *p, bool first)
{
	struct scx_sched *sch = scx_root;

	if (p->scx.flags & SCX_TASK_QUEUED) {
		/*
		 * Core-sched might decide to execute @p before it is
		 * dispatched. Call ops_dequeue() to notify the BPF scheduler.
		 */
		ops_dequeue(rq, p, SCX_DEQ_CORE_SCHED_EXEC);
		dispatch_dequeue(rq, p);
	}

	p->se.exec_start = rq_clock_task(rq);

	/* see dequeue_task_scx() on why we skip when !QUEUED */
	if (SCX_HAS_OP(sch, running) && (p->scx.flags & SCX_TASK_QUEUED))
		SCX_CALL_OP_TASK(sch, SCX_KF_REST, running, rq, p);

	clr_task_runnable(p, true);

	/*
	 * @p is getting newly scheduled or got kicked after someone updated its
	 * slice. Refresh whether tick can be stopped. See scx_can_stop_tick().
	 */
	if ((p->scx.slice == SCX_SLICE_INF) !=
	    (bool)(rq->scx.flags & SCX_RQ_CAN_STOP_TICK)) {
		if (p->scx.slice == SCX_SLICE_INF)
			rq->scx.flags |= SCX_RQ_CAN_STOP_TICK;
		else
			rq->scx.flags &= ~SCX_RQ_CAN_STOP_TICK;

		sched_update_tick_dependency(rq);

		/*
		 * For now, let's refresh the load_avgs just when transitioning
		 * in and out of nohz. In the future, we might want to add a
		 * mechanism which calls the following periodically on
		 * tick-stopped CPUs.
		 */
		update_other_load_avgs(rq);
	}
}

static enum scx_cpu_preempt_reason
preempt_reason_from_class(const struct sched_class *class)
{
	if (class == &stop_sched_class)
		return SCX_CPU_PREEMPT_STOP;
	if (class == &dl_sched_class)
		return SCX_CPU_PREEMPT_DL;
	if (class == &rt_sched_class)
		return SCX_CPU_PREEMPT_RT;
	return SCX_CPU_PREEMPT_UNKNOWN;
}

static void switch_class(struct rq *rq, struct task_struct *next)
{
	struct scx_sched *sch = scx_root;
	const struct sched_class *next_class = next->sched_class;

	/*
	 * Pairs with the smp_load_acquire() issued by a CPU in
	 * kick_cpus_irq_workfn() who is waiting for this CPU to perform a
	 * resched.
	 */
	smp_store_release(&rq->scx.pnt_seq, rq->scx.pnt_seq + 1);
	if (!(sch->ops.flags & SCX_OPS_HAS_CPU_PREEMPT))
		return;

	/*
	 * The callback is conceptually meant to convey that the CPU is no
	 * longer under the control of SCX. Therefore, don't invoke the callback
	 * if the next class is below SCX (in which case the BPF scheduler has
	 * actively decided not to schedule any tasks on the CPU).
	 */
	if (sched_class_above(&ext_sched_class, next_class))
		return;

	/*
	 * At this point we know that SCX was preempted by a higher priority
	 * sched_class, so invoke the ->cpu_release() callback if we have not
	 * done so already. We only send the callback once between SCX being
	 * preempted, and it regaining control of the CPU.
	 *
	 * ->cpu_release() complements ->cpu_acquire(), which is emitted the
	 *  next time that balance_scx() is invoked.
	 */
	if (!rq->scx.cpu_released) {
		if (SCX_HAS_OP(sch, cpu_release)) {
			struct scx_cpu_release_args args = {
				.reason = preempt_reason_from_class(next_class),
				.task = next,
			};

			SCX_CALL_OP(sch, SCX_KF_CPU_RELEASE, cpu_release, rq,
				    cpu_of(rq), &args);
		}
		rq->scx.cpu_released = true;
	}
}

static void put_prev_task_scx(struct rq *rq, struct task_struct *p,
			      struct task_struct *next)
{
	struct scx_sched *sch = scx_root;
	update_curr_scx(rq);

	/* see dequeue_task_scx() on why we skip when !QUEUED */
	if (SCX_HAS_OP(sch, stopping) && (p->scx.flags & SCX_TASK_QUEUED))
		SCX_CALL_OP_TASK(sch, SCX_KF_REST, stopping, rq, p, true);

	if (p->scx.flags & SCX_TASK_QUEUED) {
		set_task_runnable(rq, p);

		/*
		 * If @p has slice left and is being put, @p is getting
		 * preempted by a higher priority scheduler class or core-sched
		 * forcing a different task. Leave it at the head of the local
		 * DSQ.
		 */
		if (p->scx.slice && !scx_rq_bypassing(rq)) {
			dispatch_enqueue(sch, &rq->scx.local_dsq, p,
					 SCX_ENQ_HEAD);
			goto switch_class;
		}

		/*
		 * If @p is runnable but we're about to enter a lower
		 * sched_class, %SCX_OPS_ENQ_LAST must be set. Tell
		 * ops.enqueue() that @p is the only one available for this cpu,
		 * which should trigger an explicit follow-up scheduling event.
		 */
		if (sched_class_above(&ext_sched_class, next->sched_class)) {
			WARN_ON_ONCE(!(sch->ops.flags & SCX_OPS_ENQ_LAST));
			do_enqueue_task(rq, p, SCX_ENQ_LAST, -1);
		} else {
			do_enqueue_task(rq, p, 0, -1);
		}
	}

switch_class:
	if (next && next->sched_class != &ext_sched_class)
		switch_class(rq, next);
}

static struct task_struct *first_local_task(struct rq *rq)
{
	return list_first_entry_or_null(&rq->scx.local_dsq.list,
					struct task_struct, scx.dsq_list.node);
}

static struct task_struct *pick_task_scx(struct rq *rq)
{
	struct task_struct *prev = rq->curr;
	struct task_struct *p;
	bool keep_prev = rq->scx.flags & SCX_RQ_BAL_KEEP;
	bool kick_idle = false;

	/*
	 * WORKAROUND:
	 *
	 * %SCX_RQ_BAL_KEEP should be set iff $prev is on SCX as it must just
	 * have gone through balance_scx(). Unfortunately, there currently is a
	 * bug where fair could say yes on balance() but no on pick_task(),
	 * which then ends up calling pick_task_scx() without preceding
	 * balance_scx().
	 *
	 * Keep running @prev if possible and avoid stalling from entering idle
	 * without balancing.
	 *
	 * Once fair is fixed, remove the workaround and trigger WARN_ON_ONCE()
	 * if pick_task_scx() is called without preceding balance_scx().
	 */
	if (unlikely(rq->scx.flags & SCX_RQ_BAL_PENDING)) {
		if (prev->scx.flags & SCX_TASK_QUEUED) {
			keep_prev = true;
		} else {
			keep_prev = false;
			kick_idle = true;
		}
	} else if (unlikely(keep_prev &&
			    prev->sched_class != &ext_sched_class)) {
		/*
		 * Can happen while enabling as SCX_RQ_BAL_PENDING assertion is
		 * conditional on scx_enabled() and may have been skipped.
		 */
		WARN_ON_ONCE(scx_enable_state() == SCX_ENABLED);
		keep_prev = false;
	}

	/*
	 * If balance_scx() is telling us to keep running @prev, replenish slice
	 * if necessary and keep running @prev. Otherwise, pop the first one
	 * from the local DSQ.
	 */
	if (keep_prev) {
		p = prev;
		if (!p->scx.slice)
			refill_task_slice_dfl(rcu_dereference_sched(scx_root), p);
	} else {
		p = first_local_task(rq);
		if (!p) {
			if (kick_idle)
				scx_kick_cpu(rcu_dereference_sched(scx_root),
					     cpu_of(rq), SCX_KICK_IDLE);
			return NULL;
		}

		if (unlikely(!p->scx.slice)) {
			struct scx_sched *sch = rcu_dereference_sched(scx_root);

			if (!scx_rq_bypassing(rq) && !sch->warned_zero_slice) {
				printk_deferred(KERN_WARNING "sched_ext: %s[%d] has zero slice in %s()\n",
						p->comm, p->pid, __func__);
				sch->warned_zero_slice = true;
			}
			refill_task_slice_dfl(sch, p);
		}
	}

	return p;
}

#ifdef CONFIG_SCHED_CORE
/**
 * scx_prio_less - Task ordering for core-sched
 * @a: task A
 * @b: task B
 * @in_fi: in forced idle state
 *
 * Core-sched is implemented as an additional scheduling layer on top of the
 * usual sched_class'es and needs to find out the expected task ordering. For
 * SCX, core-sched calls this function to interrogate the task ordering.
 *
 * Unless overridden by ops.core_sched_before(), @p->scx.core_sched_at is used
 * to implement the default task ordering. The older the timestamp, the higher
 * priority the task - the global FIFO ordering matching the default scheduling
 * behavior.
 *
 * When ops.core_sched_before() is enabled, @p->scx.core_sched_at is used to
 * implement FIFO ordering within each local DSQ. See pick_task_scx().
 */
bool scx_prio_less(const struct task_struct *a, const struct task_struct *b,
		   bool in_fi)
{
	struct scx_sched *sch = scx_root;

	/*
	 * The const qualifiers are dropped from task_struct pointers when
	 * calling ops.core_sched_before(). Accesses are controlled by the
	 * verifier.
	 */
	if (SCX_HAS_OP(sch, core_sched_before) &&
	    !scx_rq_bypassing(task_rq(a)))
		return SCX_CALL_OP_2TASKS_RET(sch, SCX_KF_REST, core_sched_before,
					      NULL,
					      (struct task_struct *)a,
					      (struct task_struct *)b);
	else
		return time_after64(a->scx.core_sched_at, b->scx.core_sched_at);
}
#endif	/* CONFIG_SCHED_CORE */

static int select_task_rq_scx(struct task_struct *p, int prev_cpu, int wake_flags)
{
	struct scx_sched *sch = scx_root;
	bool rq_bypass;

	/*
	 * sched_exec() calls with %WF_EXEC when @p is about to exec(2) as it
	 * can be a good migration opportunity with low cache and memory
	 * footprint. Returning a CPU different than @prev_cpu triggers
	 * immediate rq migration. However, for SCX, as the current rq
	 * association doesn't dictate where the task is going to run, this
	 * doesn't fit well. If necessary, we can later add a dedicated method
	 * which can decide to preempt self to force it through the regular
	 * scheduling path.
	 */
	if (unlikely(wake_flags & WF_EXEC))
		return prev_cpu;

	rq_bypass = scx_rq_bypassing(task_rq(p));
	if (likely(SCX_HAS_OP(sch, select_cpu)) && !rq_bypass) {
		s32 cpu;
		struct task_struct **ddsp_taskp;

		ddsp_taskp = this_cpu_ptr(&direct_dispatch_task);
		WARN_ON_ONCE(*ddsp_taskp);
		*ddsp_taskp = p;

		cpu = SCX_CALL_OP_TASK_RET(sch,
					   SCX_KF_ENQUEUE | SCX_KF_SELECT_CPU,
					   select_cpu, NULL, p, prev_cpu,
					   wake_flags);
		p->scx.selected_cpu = cpu;
		*ddsp_taskp = NULL;
		if (ops_cpu_valid(sch, cpu, "from ops.select_cpu()"))
			return cpu;
		else
			return prev_cpu;
	} else {
		s32 cpu;

		cpu = scx_select_cpu_dfl(p, prev_cpu, wake_flags, NULL, 0);
		if (cpu >= 0) {
			refill_task_slice_dfl(sch, p);
			p->scx.ddsp_dsq_id = SCX_DSQ_LOCAL;
		} else {
			cpu = prev_cpu;
		}
		p->scx.selected_cpu = cpu;

		if (rq_bypass)
			__scx_add_event(sch, SCX_EV_BYPASS_DISPATCH, 1);
		return cpu;
	}
}

static void task_woken_scx(struct rq *rq, struct task_struct *p)
{
	run_deferred(rq);
}

static void set_cpus_allowed_scx(struct task_struct *p,
				 struct affinity_context *ac)
{
	struct scx_sched *sch = scx_root;

	set_cpus_allowed_common(p, ac);

	/*
	 * The effective cpumask is stored in @p->cpus_ptr which may temporarily
	 * differ from the configured one in @p->cpus_mask. Always tell the bpf
	 * scheduler the effective one.
	 *
	 * Fine-grained memory write control is enforced by BPF making the const
	 * designation pointless. Cast it away when calling the operation.
	 */
	if (SCX_HAS_OP(sch, set_cpumask))
		SCX_CALL_OP_TASK(sch, SCX_KF_REST, set_cpumask, NULL,
				 p, (struct cpumask *)p->cpus_ptr);
}

static void handle_hotplug(struct rq *rq, bool online)
{
	struct scx_sched *sch = scx_root;
	int cpu = cpu_of(rq);

	atomic_long_inc(&scx_hotplug_seq);

	/*
	 * scx_root updates are protected by cpus_read_lock() and will stay
	 * stable here. Note that we can't depend on scx_enabled() test as the
	 * hotplug ops need to be enabled before __scx_enabled is set.
	 */
	if (unlikely(!sch))
		return;

	if (scx_enabled())
		scx_idle_update_selcpu_topology(&sch->ops);

	if (online && SCX_HAS_OP(sch, cpu_online))
		SCX_CALL_OP(sch, SCX_KF_UNLOCKED, cpu_online, NULL, cpu);
	else if (!online && SCX_HAS_OP(sch, cpu_offline))
		SCX_CALL_OP(sch, SCX_KF_UNLOCKED, cpu_offline, NULL, cpu);
	else
		scx_exit(sch, SCX_EXIT_UNREG_KERN,
			 SCX_ECODE_ACT_RESTART | SCX_ECODE_RSN_HOTPLUG,
			 "cpu %d going %s, exiting scheduler", cpu,
			 online ? "online" : "offline");
}

void scx_rq_activate(struct rq *rq)
{
	handle_hotplug(rq, true);
}

void scx_rq_deactivate(struct rq *rq)
{
	handle_hotplug(rq, false);
}

static void rq_online_scx(struct rq *rq)
{
	rq->scx.flags |= SCX_RQ_ONLINE;
}

static void rq_offline_scx(struct rq *rq)
{
	rq->scx.flags &= ~SCX_RQ_ONLINE;
}


static bool check_rq_for_timeouts(struct rq *rq)
{
	struct scx_sched *sch;
	struct task_struct *p;
	struct rq_flags rf;
	bool timed_out = false;

	rq_lock_irqsave(rq, &rf);
	sch = rcu_dereference_bh(scx_root);
	if (unlikely(!sch))
		goto out_unlock;

	list_for_each_entry(p, &rq->scx.runnable_list, scx.runnable_node) {
		unsigned long last_runnable = p->scx.runnable_at;

		if (unlikely(time_after(jiffies,
					last_runnable + scx_watchdog_timeout))) {
			u32 dur_ms = jiffies_to_msecs(jiffies - last_runnable);

			scx_exit(sch, SCX_EXIT_ERROR_STALL, 0,
				 "%s[%d] failed to run for %u.%03us",
				 p->comm, p->pid, dur_ms / 1000, dur_ms % 1000);
			timed_out = true;
			break;
		}
	}
out_unlock:
	rq_unlock_irqrestore(rq, &rf);
	return timed_out;
}

static void scx_watchdog_workfn(struct work_struct *work)
{
	int cpu;

	WRITE_ONCE(scx_watchdog_timestamp, jiffies);

	for_each_online_cpu(cpu) {
		if (unlikely(check_rq_for_timeouts(cpu_rq(cpu))))
			break;

		cond_resched();
	}
	queue_delayed_work(system_unbound_wq, to_delayed_work(work),
			   scx_watchdog_timeout / 2);
}

void scx_tick(struct rq *rq)
{
	struct scx_sched *sch;
	unsigned long last_check;

	if (!scx_enabled())
		return;

	sch = rcu_dereference_bh(scx_root);
	if (unlikely(!sch))
		return;

	last_check = READ_ONCE(scx_watchdog_timestamp);
	if (unlikely(time_after(jiffies,
				last_check + READ_ONCE(scx_watchdog_timeout)))) {
		u32 dur_ms = jiffies_to_msecs(jiffies - last_check);

		scx_exit(sch, SCX_EXIT_ERROR_STALL, 0,
			 "watchdog failed to check in for %u.%03us",
			 dur_ms / 1000, dur_ms % 1000);
	}

	update_other_load_avgs(rq);
}

static void task_tick_scx(struct rq *rq, struct task_struct *curr, int queued)
{
	struct scx_sched *sch = scx_root;

	update_curr_scx(rq);

	/*
	 * While disabling, always resched and refresh core-sched timestamp as
	 * we can't trust the slice management or ops.core_sched_before().
	 */
	if (scx_rq_bypassing(rq)) {
		curr->scx.slice = 0;
		touch_core_sched(rq, curr);
	} else if (SCX_HAS_OP(sch, tick)) {
		SCX_CALL_OP_TASK(sch, SCX_KF_REST, tick, rq, curr);
	}

	if (!curr->scx.slice)
		resched_curr(rq);
}

#ifdef CONFIG_EXT_GROUP_SCHED
static struct cgroup *tg_cgrp(struct task_group *tg)
{
	/*
	 * If CGROUP_SCHED is disabled, @tg is NULL. If @tg is an autogroup,
	 * @tg->css.cgroup is NULL. In both cases, @tg can be treated as the
	 * root cgroup.
	 */
	if (tg && tg->css.cgroup)
		return tg->css.cgroup;
	else
		return &cgrp_dfl_root.cgrp;
}

#define SCX_INIT_TASK_ARGS_CGROUP(tg)		.cgroup = tg_cgrp(tg),

#else	/* CONFIG_EXT_GROUP_SCHED */

#define SCX_INIT_TASK_ARGS_CGROUP(tg)

#endif	/* CONFIG_EXT_GROUP_SCHED */

static enum scx_task_state scx_get_task_state(const struct task_struct *p)
{
	return (p->scx.flags & SCX_TASK_STATE_MASK) >> SCX_TASK_STATE_SHIFT;
}

static void scx_set_task_state(struct task_struct *p, enum scx_task_state state)
{
	enum scx_task_state prev_state = scx_get_task_state(p);
	bool warn = false;

	BUILD_BUG_ON(SCX_TASK_NR_STATES > (1 << SCX_TASK_STATE_BITS));

	switch (state) {
	case SCX_TASK_NONE:
		break;
	case SCX_TASK_INIT:
		warn = prev_state != SCX_TASK_NONE;
		break;
	case SCX_TASK_READY:
		warn = prev_state == SCX_TASK_NONE;
		break;
	case SCX_TASK_ENABLED:
		warn = prev_state != SCX_TASK_READY;
		break;
	default:
		warn = true;
		return;
	}

	WARN_ONCE(warn, "sched_ext: Invalid task state transition %d -> %d for %s[%d]",
		  prev_state, state, p->comm, p->pid);

	p->scx.flags &= ~SCX_TASK_STATE_MASK;
	p->scx.flags |= state << SCX_TASK_STATE_SHIFT;
}

static int scx_init_task(struct task_struct *p, struct task_group *tg, bool fork)
{
	struct scx_sched *sch = scx_root;
	int ret;

	p->scx.disallow = false;

	if (SCX_HAS_OP(sch, init_task)) {
		struct scx_init_task_args args = {
			SCX_INIT_TASK_ARGS_CGROUP(tg)
			.fork = fork,
		};

		ret = SCX_CALL_OP_RET(sch, SCX_KF_UNLOCKED, init_task, NULL,
				      p, &args);
		if (unlikely(ret)) {
			ret = ops_sanitize_err(sch, "init_task", ret);
			return ret;
		}
	}

	scx_set_task_state(p, SCX_TASK_INIT);

	if (p->scx.disallow) {
		if (!fork) {
			struct rq *rq;
			struct rq_flags rf;

			rq = task_rq_lock(p, &rf);

			/*
			 * We're in the load path and @p->policy will be applied
			 * right after. Reverting @p->policy here and rejecting
			 * %SCHED_EXT transitions from scx_check_setscheduler()
			 * guarantees that if ops.init_task() sets @p->disallow,
			 * @p can never be in SCX.
			 */
			if (p->policy == SCHED_EXT) {
				p->policy = SCHED_NORMAL;
				atomic_long_inc(&scx_nr_rejected);
			}

			task_rq_unlock(rq, p, &rf);
		} else if (p->policy == SCHED_EXT) {
			scx_error(sch, "ops.init_task() set task->scx.disallow for %s[%d] during fork",
				  p->comm, p->pid);
		}
	}

	p->scx.flags |= SCX_TASK_RESET_RUNNABLE_AT;
	return 0;
}

static void scx_enable_task(struct task_struct *p)
{
	struct scx_sched *sch = scx_root;
	struct rq *rq = task_rq(p);
	u32 weight;

	lockdep_assert_rq_held(rq);

	/*
	 * Set the weight before calling ops.enable() so that the scheduler
	 * doesn't see a stale value if they inspect the task struct.
	 */
	if (task_has_idle_policy(p))
		weight = WEIGHT_IDLEPRIO;
	else
		weight = sched_prio_to_weight[p->static_prio - MAX_RT_PRIO];

	p->scx.weight = sched_weight_to_cgroup(weight);

	if (SCX_HAS_OP(sch, enable))
		SCX_CALL_OP_TASK(sch, SCX_KF_REST, enable, rq, p);
	scx_set_task_state(p, SCX_TASK_ENABLED);

	if (SCX_HAS_OP(sch, set_weight))
		SCX_CALL_OP_TASK(sch, SCX_KF_REST, set_weight, rq,
				 p, p->scx.weight);
}

static void scx_disable_task(struct task_struct *p)
{
	struct scx_sched *sch = scx_root;
	struct rq *rq = task_rq(p);

	lockdep_assert_rq_held(rq);
	WARN_ON_ONCE(scx_get_task_state(p) != SCX_TASK_ENABLED);

	if (SCX_HAS_OP(sch, disable))
		SCX_CALL_OP_TASK(sch, SCX_KF_REST, disable, rq, p);
	scx_set_task_state(p, SCX_TASK_READY);
}

static void scx_exit_task(struct task_struct *p)
{
	struct scx_sched *sch = scx_root;
	struct scx_exit_task_args args = {
		.cancelled = false,
	};

	lockdep_assert_rq_held(task_rq(p));

	switch (scx_get_task_state(p)) {
	case SCX_TASK_NONE:
		return;
	case SCX_TASK_INIT:
		args.cancelled = true;
		break;
	case SCX_TASK_READY:
		break;
	case SCX_TASK_ENABLED:
		scx_disable_task(p);
		break;
	default:
		WARN_ON_ONCE(true);
		return;
	}

	if (SCX_HAS_OP(sch, exit_task))
		SCX_CALL_OP_TASK(sch, SCX_KF_REST, exit_task, task_rq(p),
				 p, &args);
	scx_set_task_state(p, SCX_TASK_NONE);
}

void init_scx_entity(struct sched_ext_entity *scx)
{
	memset(scx, 0, sizeof(*scx));
	INIT_LIST_HEAD(&scx->dsq_list.node);
	RB_CLEAR_NODE(&scx->dsq_priq);
	scx->sticky_cpu = -1;
	scx->holding_cpu = -1;
	INIT_LIST_HEAD(&scx->runnable_node);
	scx->runnable_at = jiffies;
	scx->ddsp_dsq_id = SCX_DSQ_INVALID;
	scx->slice = SCX_SLICE_DFL;
}

void scx_pre_fork(struct task_struct *p)
{
	/*
	 * BPF scheduler enable/disable paths want to be able to iterate and
	 * update all tasks which can become complex when racing forks. As
	 * enable/disable are very cold paths, let's use a percpu_rwsem to
	 * exclude forks.
	 */
	percpu_down_read(&scx_fork_rwsem);
}

int scx_fork(struct task_struct *p)
{
	percpu_rwsem_assert_held(&scx_fork_rwsem);

	if (scx_init_task_enabled)
		return scx_init_task(p, task_group(p), true);
	else
		return 0;
}

void scx_post_fork(struct task_struct *p)
{
	if (scx_init_task_enabled) {
		scx_set_task_state(p, SCX_TASK_READY);

		/*
		 * Enable the task immediately if it's running on sched_ext.
		 * Otherwise, it'll be enabled in switching_to_scx() if and
		 * when it's ever configured to run with a SCHED_EXT policy.
		 */
		if (p->sched_class == &ext_sched_class) {
			struct rq_flags rf;
			struct rq *rq;

			rq = task_rq_lock(p, &rf);
			scx_enable_task(p);
			task_rq_unlock(rq, p, &rf);
		}
	}

	raw_spin_lock_irq(&scx_tasks_lock);
	list_add_tail(&p->scx.tasks_node, &scx_tasks);
	raw_spin_unlock_irq(&scx_tasks_lock);

	percpu_up_read(&scx_fork_rwsem);
}

void scx_cancel_fork(struct task_struct *p)
{
	if (scx_enabled()) {
		struct rq *rq;
		struct rq_flags rf;

		rq = task_rq_lock(p, &rf);
		WARN_ON_ONCE(scx_get_task_state(p) >= SCX_TASK_READY);
		scx_exit_task(p);
		task_rq_unlock(rq, p, &rf);
	}

	percpu_up_read(&scx_fork_rwsem);
}

void sched_ext_free(struct task_struct *p)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&scx_tasks_lock, flags);
	list_del_init(&p->scx.tasks_node);
	raw_spin_unlock_irqrestore(&scx_tasks_lock, flags);

	/*
	 * @p is off scx_tasks and wholly ours. scx_enable()'s READY -> ENABLED
	 * transitions can't race us. Disable ops for @p.
	 */
	if (scx_get_task_state(p) != SCX_TASK_NONE) {
		struct rq_flags rf;
		struct rq *rq;

		rq = task_rq_lock(p, &rf);
		scx_exit_task(p);
		task_rq_unlock(rq, p, &rf);
	}
}

static void reweight_task_scx(struct rq *rq, struct task_struct *p,
			      const struct load_weight *lw)
{
	struct scx_sched *sch = scx_root;

	lockdep_assert_rq_held(task_rq(p));

	p->scx.weight = sched_weight_to_cgroup(scale_load_down(lw->weight));
	if (SCX_HAS_OP(sch, set_weight))
		SCX_CALL_OP_TASK(sch, SCX_KF_REST, set_weight, rq,
				 p, p->scx.weight);
}

static void prio_changed_scx(struct rq *rq, struct task_struct *p, int oldprio)
{
}

static void switching_to_scx(struct rq *rq, struct task_struct *p)
{
	struct scx_sched *sch = scx_root;

	scx_enable_task(p);

	/*
	 * set_cpus_allowed_scx() is not called while @p is associated with a
	 * different scheduler class. Keep the BPF scheduler up-to-date.
	 */
	if (SCX_HAS_OP(sch, set_cpumask))
		SCX_CALL_OP_TASK(sch, SCX_KF_REST, set_cpumask, rq,
				 p, (struct cpumask *)p->cpus_ptr);
}

static void switched_from_scx(struct rq *rq, struct task_struct *p)
{
	scx_disable_task(p);
}

static void wakeup_preempt_scx(struct rq *rq, struct task_struct *p,int wake_flags) {}
static void switched_to_scx(struct rq *rq, struct task_struct *p) {}

int scx_check_setscheduler(struct task_struct *p, int policy)
{
	lockdep_assert_rq_held(task_rq(p));

	/* if disallow, reject transitioning into SCX */
	if (scx_enabled() && READ_ONCE(p->scx.disallow) &&
	    p->policy != policy && policy == SCHED_EXT)
		return -EACCES;

	return 0;
}

#ifdef CONFIG_NO_HZ_FULL
bool scx_can_stop_tick(struct rq *rq)
{
	struct task_struct *p = rq->curr;

	if (scx_rq_bypassing(rq))
		return false;

	if (p->sched_class != &ext_sched_class)
		return true;

	/*
	 * @rq can dispatch from different DSQs, so we can't tell whether it
	 * needs the tick or not by looking at nr_running. Allow stopping ticks
	 * iff the BPF scheduler indicated so. See set_next_task_scx().
	 */
	return rq->scx.flags & SCX_RQ_CAN_STOP_TICK;
}
#endif

#ifdef CONFIG_EXT_GROUP_SCHED

DEFINE_STATIC_PERCPU_RWSEM(scx_cgroup_ops_rwsem);
static bool scx_cgroup_enabled;

void scx_tg_init(struct task_group *tg)
{
	tg->scx.weight = CGROUP_WEIGHT_DFL;
	tg->scx.bw_period_us = default_bw_period_us();
	tg->scx.bw_quota_us = RUNTIME_INF;
}

int scx_tg_online(struct task_group *tg)
{
	struct scx_sched *sch = scx_root;
	int ret = 0;

	WARN_ON_ONCE(tg->scx.flags & (SCX_TG_ONLINE | SCX_TG_INITED));

	if (scx_cgroup_enabled) {
		if (SCX_HAS_OP(sch, cgroup_init)) {
			struct scx_cgroup_init_args args =
				{ .weight = tg->scx.weight,
				  .bw_period_us = tg->scx.bw_period_us,
				  .bw_quota_us = tg->scx.bw_quota_us,
				  .bw_burst_us = tg->scx.bw_burst_us };

			ret = SCX_CALL_OP_RET(sch, SCX_KF_UNLOCKED, cgroup_init,
					      NULL, tg->css.cgroup, &args);
			if (ret)
				ret = ops_sanitize_err(sch, "cgroup_init", ret);
		}
		if (ret == 0)
			tg->scx.flags |= SCX_TG_ONLINE | SCX_TG_INITED;
	} else {
		tg->scx.flags |= SCX_TG_ONLINE;
	}

	return ret;
}

void scx_tg_offline(struct task_group *tg)
{
	struct scx_sched *sch = scx_root;

	WARN_ON_ONCE(!(tg->scx.flags & SCX_TG_ONLINE));

	if (scx_cgroup_enabled && SCX_HAS_OP(sch, cgroup_exit) &&
	    (tg->scx.flags & SCX_TG_INITED))
		SCX_CALL_OP(sch, SCX_KF_UNLOCKED, cgroup_exit, NULL,
			    tg->css.cgroup);
	tg->scx.flags &= ~(SCX_TG_ONLINE | SCX_TG_INITED);
}

int scx_cgroup_can_attach(struct cgroup_taskset *tset)
{
	struct scx_sched *sch = scx_root;
	struct cgroup_subsys_state *css;
	struct task_struct *p;
	int ret;

	if (!scx_cgroup_enabled)
		return 0;

	cgroup_taskset_for_each(p, css, tset) {
		struct cgroup *from = tg_cgrp(task_group(p));
		struct cgroup *to = tg_cgrp(css_tg(css));

		WARN_ON_ONCE(p->scx.cgrp_moving_from);

		/*
		 * sched_move_task() omits identity migrations. Let's match the
		 * behavior so that ops.cgroup_prep_move() and ops.cgroup_move()
		 * always match one-to-one.
		 */
		if (from == to)
			continue;

		if (SCX_HAS_OP(sch, cgroup_prep_move)) {
			ret = SCX_CALL_OP_RET(sch, SCX_KF_UNLOCKED,
					      cgroup_prep_move, NULL,
					      p, from, css->cgroup);
			if (ret)
				goto err;
		}

		p->scx.cgrp_moving_from = from;
	}

	return 0;

err:
	cgroup_taskset_for_each(p, css, tset) {
		if (SCX_HAS_OP(sch, cgroup_cancel_move) &&
		    p->scx.cgrp_moving_from)
			SCX_CALL_OP(sch, SCX_KF_UNLOCKED, cgroup_cancel_move, NULL,
				    p, p->scx.cgrp_moving_from, css->cgroup);
		p->scx.cgrp_moving_from = NULL;
	}

	return ops_sanitize_err(sch, "cgroup_prep_move", ret);
}

void scx_cgroup_move_task(struct task_struct *p)
{
	struct scx_sched *sch = scx_root;

	if (!scx_cgroup_enabled)
		return;

	/*
	 * @p must have ops.cgroup_prep_move() called on it and thus
	 * cgrp_moving_from set.
	 */
	if (SCX_HAS_OP(sch, cgroup_move) &&
	    !WARN_ON_ONCE(!p->scx.cgrp_moving_from))
		SCX_CALL_OP_TASK(sch, SCX_KF_UNLOCKED, cgroup_move, NULL,
				 p, p->scx.cgrp_moving_from,
				 tg_cgrp(task_group(p)));
	p->scx.cgrp_moving_from = NULL;
}

void scx_cgroup_cancel_attach(struct cgroup_taskset *tset)
{
	struct scx_sched *sch = scx_root;
	struct cgroup_subsys_state *css;
	struct task_struct *p;

	if (!scx_cgroup_enabled)
		return;

	cgroup_taskset_for_each(p, css, tset) {
		if (SCX_HAS_OP(sch, cgroup_cancel_move) &&
		    p->scx.cgrp_moving_from)
			SCX_CALL_OP(sch, SCX_KF_UNLOCKED, cgroup_cancel_move, NULL,
				    p, p->scx.cgrp_moving_from, css->cgroup);
		p->scx.cgrp_moving_from = NULL;
	}
}

void scx_group_set_weight(struct task_group *tg, unsigned long weight)
{
	struct scx_sched *sch = scx_root;

	percpu_down_read(&scx_cgroup_ops_rwsem);

	if (scx_cgroup_enabled && SCX_HAS_OP(sch, cgroup_set_weight) &&
	    tg->scx.weight != weight)
		SCX_CALL_OP(sch, SCX_KF_UNLOCKED, cgroup_set_weight, NULL,
			    tg_cgrp(tg), weight);

	tg->scx.weight = weight;

	percpu_up_read(&scx_cgroup_ops_rwsem);
}

void scx_group_set_idle(struct task_group *tg, bool idle)
{
	/* TODO: Implement ops->cgroup_set_idle() */
}

void scx_group_set_bandwidth(struct task_group *tg,
			     u64 period_us, u64 quota_us, u64 burst_us)
{
	struct scx_sched *sch = scx_root;

	percpu_down_read(&scx_cgroup_ops_rwsem);

	if (scx_cgroup_enabled && SCX_HAS_OP(sch, cgroup_set_bandwidth) &&
	    (tg->scx.bw_period_us != period_us ||
	     tg->scx.bw_quota_us != quota_us ||
	     tg->scx.bw_burst_us != burst_us))
		SCX_CALL_OP(sch, SCX_KF_UNLOCKED, cgroup_set_bandwidth, NULL,
			    tg_cgrp(tg), period_us, quota_us, burst_us);

	tg->scx.bw_period_us = period_us;
	tg->scx.bw_quota_us = quota_us;
	tg->scx.bw_burst_us = burst_us;

	percpu_up_read(&scx_cgroup_ops_rwsem);
}

static void scx_cgroup_lock(void)
{
	percpu_down_write(&scx_cgroup_ops_rwsem);
	cgroup_lock();
}

static void scx_cgroup_unlock(void)
{
	cgroup_unlock();
	percpu_up_write(&scx_cgroup_ops_rwsem);
}

#else	/* CONFIG_EXT_GROUP_SCHED */

static void scx_cgroup_lock(void) {}
static void scx_cgroup_unlock(void) {}

#endif	/* CONFIG_EXT_GROUP_SCHED */

/*
 * Omitted operations:
 *
 * - wakeup_preempt: NOOP as it isn't useful in the wakeup path because the task
 *   isn't tied to the CPU at that point. Preemption is implemented by resetting
 *   the victim task's slice to 0 and triggering reschedule on the target CPU.
 *
 * - migrate_task_rq: Unnecessary as task to cpu mapping is transient.
 *
 * - task_fork/dead: We need fork/dead notifications for all tasks regardless of
 *   their current sched_class. Call them directly from sched core instead.
 */
DEFINE_SCHED_CLASS(ext) = {
	.enqueue_task		= enqueue_task_scx,
	.dequeue_task		= dequeue_task_scx,
	.yield_task		= yield_task_scx,
	.yield_to_task		= yield_to_task_scx,

	.wakeup_preempt		= wakeup_preempt_scx,

	.balance		= balance_scx,
	.pick_task		= pick_task_scx,

	.put_prev_task		= put_prev_task_scx,
	.set_next_task		= set_next_task_scx,

	.select_task_rq		= select_task_rq_scx,
	.task_woken		= task_woken_scx,
	.set_cpus_allowed	= set_cpus_allowed_scx,

	.rq_online		= rq_online_scx,
	.rq_offline		= rq_offline_scx,

	.task_tick		= task_tick_scx,

	.switching_to		= switching_to_scx,
	.switched_from		= switched_from_scx,
	.switched_to		= switched_to_scx,
	.reweight_task		= reweight_task_scx,
	.prio_changed		= prio_changed_scx,

	.update_curr		= update_curr_scx,

#ifdef CONFIG_UCLAMP_TASK
	.uclamp_enabled		= 1,
#endif
};

static void init_dsq(struct scx_dispatch_q *dsq, u64 dsq_id)
{
	memset(dsq, 0, sizeof(*dsq));

	raw_spin_lock_init(&dsq->lock);
	INIT_LIST_HEAD(&dsq->list);
	dsq->id = dsq_id;
}

static void free_dsq_irq_workfn(struct irq_work *irq_work)
{
	struct llist_node *to_free = llist_del_all(&dsqs_to_free);
	struct scx_dispatch_q *dsq, *tmp_dsq;

	llist_for_each_entry_safe(dsq, tmp_dsq, to_free, free_node)
		kfree_rcu(dsq, rcu);
}

static DEFINE_IRQ_WORK(free_dsq_irq_work, free_dsq_irq_workfn);

static void destroy_dsq(struct scx_sched *sch, u64 dsq_id)
{
	struct scx_dispatch_q *dsq;
	unsigned long flags;

	rcu_read_lock();

	dsq = find_user_dsq(sch, dsq_id);
	if (!dsq)
		goto out_unlock_rcu;

	raw_spin_lock_irqsave(&dsq->lock, flags);

	if (dsq->nr) {
		scx_error(sch, "attempting to destroy in-use dsq 0x%016llx (nr=%u)",
			  dsq->id, dsq->nr);
		goto out_unlock_dsq;
	}

	if (rhashtable_remove_fast(&sch->dsq_hash, &dsq->hash_node,
				   dsq_hash_params))
		goto out_unlock_dsq;

	/*
	 * Mark dead by invalidating ->id to prevent dispatch_enqueue() from
	 * queueing more tasks. As this function can be called from anywhere,
	 * freeing is bounced through an irq work to avoid nesting RCU
	 * operations inside scheduler locks.
	 */
	dsq->id = SCX_DSQ_INVALID;
	llist_add(&dsq->free_node, &dsqs_to_free);
	irq_work_queue(&free_dsq_irq_work);

out_unlock_dsq:
	raw_spin_unlock_irqrestore(&dsq->lock, flags);
out_unlock_rcu:
	rcu_read_unlock();
}

#ifdef CONFIG_EXT_GROUP_SCHED
static void scx_cgroup_exit(struct scx_sched *sch)
{
	struct cgroup_subsys_state *css;

	scx_cgroup_enabled = false;

	/*
	 * scx_tg_on/offline() are excluded through cgroup_lock(). If we walk
	 * cgroups and exit all the inited ones, all online cgroups are exited.
	 */
	css_for_each_descendant_post(css, &root_task_group.css) {
		struct task_group *tg = css_tg(css);

		if (!(tg->scx.flags & SCX_TG_INITED))
			continue;
		tg->scx.flags &= ~SCX_TG_INITED;

		if (!sch->ops.cgroup_exit)
			continue;

		SCX_CALL_OP(sch, SCX_KF_UNLOCKED, cgroup_exit, NULL,
			    css->cgroup);
	}
}

static int scx_cgroup_init(struct scx_sched *sch)
{
	struct cgroup_subsys_state *css;
	int ret;

	/*
	 * scx_tg_on/offline() are excluded through cgroup_lock(). If we walk
	 * cgroups and init, all online cgroups are initialized.
	 */
	css_for_each_descendant_pre(css, &root_task_group.css) {
		struct task_group *tg = css_tg(css);
		struct scx_cgroup_init_args args = {
			.weight = tg->scx.weight,
			.bw_period_us = tg->scx.bw_period_us,
			.bw_quota_us = tg->scx.bw_quota_us,
			.bw_burst_us = tg->scx.bw_burst_us,
		};

		if ((tg->scx.flags &
		     (SCX_TG_ONLINE | SCX_TG_INITED)) != SCX_TG_ONLINE)
			continue;

		if (!sch->ops.cgroup_init) {
			tg->scx.flags |= SCX_TG_INITED;
			continue;
		}

		ret = SCX_CALL_OP_RET(sch, SCX_KF_UNLOCKED, cgroup_init, NULL,
				      css->cgroup, &args);
		if (ret) {
			css_put(css);
			scx_error(sch, "ops.cgroup_init() failed (%d)", ret);
			return ret;
		}
		tg->scx.flags |= SCX_TG_INITED;
	}

	WARN_ON_ONCE(scx_cgroup_enabled);
	scx_cgroup_enabled = true;

	return 0;
}

#else
static void scx_cgroup_exit(struct scx_sched *sch) {}
static int scx_cgroup_init(struct scx_sched *sch) { return 0; }
#endif


/********************************************************************************
 * Sysfs interface and ops enable/disable.
 */

#define SCX_ATTR(_name)								\
	static struct kobj_attribute scx_attr_##_name = {			\
		.attr = { .name = __stringify(_name), .mode = 0444 },		\
		.show = scx_attr_##_name##_show,				\
	}

static ssize_t scx_attr_state_show(struct kobject *kobj,
				   struct kobj_attribute *ka, char *buf)
{
	return sysfs_emit(buf, "%s\n", scx_enable_state_str[scx_enable_state()]);
}
SCX_ATTR(state);

static ssize_t scx_attr_switch_all_show(struct kobject *kobj,
					struct kobj_attribute *ka, char *buf)
{
	return sysfs_emit(buf, "%d\n", READ_ONCE(scx_switching_all));
}
SCX_ATTR(switch_all);

static ssize_t scx_attr_nr_rejected_show(struct kobject *kobj,
					 struct kobj_attribute *ka, char *buf)
{
	return sysfs_emit(buf, "%ld\n", atomic_long_read(&scx_nr_rejected));
}
SCX_ATTR(nr_rejected);

static ssize_t scx_attr_hotplug_seq_show(struct kobject *kobj,
					 struct kobj_attribute *ka, char *buf)
{
	return sysfs_emit(buf, "%ld\n", atomic_long_read(&scx_hotplug_seq));
}
SCX_ATTR(hotplug_seq);

static ssize_t scx_attr_enable_seq_show(struct kobject *kobj,
					struct kobj_attribute *ka, char *buf)
{
	return sysfs_emit(buf, "%ld\n", atomic_long_read(&scx_enable_seq));
}
SCX_ATTR(enable_seq);

static struct attribute *scx_global_attrs[] = {
	&scx_attr_state.attr,
	&scx_attr_switch_all.attr,
	&scx_attr_nr_rejected.attr,
	&scx_attr_hotplug_seq.attr,
	&scx_attr_enable_seq.attr,
	NULL,
};

static const struct attribute_group scx_global_attr_group = {
	.attrs = scx_global_attrs,
};

static void free_exit_info(struct scx_exit_info *ei);

static void scx_sched_free_rcu_work(struct work_struct *work)
{
	struct rcu_work *rcu_work = to_rcu_work(work);
	struct scx_sched *sch = container_of(rcu_work, struct scx_sched, rcu_work);
	struct rhashtable_iter rht_iter;
	struct scx_dispatch_q *dsq;
	int node;

	irq_work_sync(&sch->error_irq_work);
	kthread_stop(sch->helper->task);

	free_percpu(sch->pcpu);

	for_each_node_state(node, N_POSSIBLE)
		kfree(sch->global_dsqs[node]);
	kfree(sch->global_dsqs);

	rhashtable_walk_enter(&sch->dsq_hash, &rht_iter);
	do {
		rhashtable_walk_start(&rht_iter);

		while ((dsq = rhashtable_walk_next(&rht_iter)) && !IS_ERR(dsq))
			destroy_dsq(sch, dsq->id);

		rhashtable_walk_stop(&rht_iter);
	} while (dsq == ERR_PTR(-EAGAIN));
	rhashtable_walk_exit(&rht_iter);

	rhashtable_free_and_destroy(&sch->dsq_hash, NULL, NULL);
	free_exit_info(sch->exit_info);
	kfree(sch);
}

static void scx_kobj_release(struct kobject *kobj)
{
	struct scx_sched *sch = container_of(kobj, struct scx_sched, kobj);

	INIT_RCU_WORK(&sch->rcu_work, scx_sched_free_rcu_work);
	queue_rcu_work(system_unbound_wq, &sch->rcu_work);
}

static ssize_t scx_attr_ops_show(struct kobject *kobj,
				 struct kobj_attribute *ka, char *buf)
{
	return sysfs_emit(buf, "%s\n", scx_root->ops.name);
}
SCX_ATTR(ops);

#define scx_attr_event_show(buf, at, events, kind) ({				\
	sysfs_emit_at(buf, at, "%s %llu\n", #kind, (events)->kind);		\
})

static ssize_t scx_attr_events_show(struct kobject *kobj,
				    struct kobj_attribute *ka, char *buf)
{
	struct scx_sched *sch = container_of(kobj, struct scx_sched, kobj);
	struct scx_event_stats events;
	int at = 0;

	scx_read_events(sch, &events);
	at += scx_attr_event_show(buf, at, &events, SCX_EV_SELECT_CPU_FALLBACK);
	at += scx_attr_event_show(buf, at, &events, SCX_EV_DISPATCH_LOCAL_DSQ_OFFLINE);
	at += scx_attr_event_show(buf, at, &events, SCX_EV_DISPATCH_KEEP_LAST);
	at += scx_attr_event_show(buf, at, &events, SCX_EV_ENQ_SKIP_EXITING);
	at += scx_attr_event_show(buf, at, &events, SCX_EV_ENQ_SKIP_MIGRATION_DISABLED);
	at += scx_attr_event_show(buf, at, &events, SCX_EV_REFILL_SLICE_DFL);
	at += scx_attr_event_show(buf, at, &events, SCX_EV_BYPASS_DURATION);
	at += scx_attr_event_show(buf, at, &events, SCX_EV_BYPASS_DISPATCH);
	at += scx_attr_event_show(buf, at, &events, SCX_EV_BYPASS_ACTIVATE);
	return at;
}
SCX_ATTR(events);

static struct attribute *scx_sched_attrs[] = {
	&scx_attr_ops.attr,
	&scx_attr_events.attr,
	NULL,
};
ATTRIBUTE_GROUPS(scx_sched);

static const struct kobj_type scx_ktype = {
	.release = scx_kobj_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = scx_sched_groups,
};

static int scx_uevent(const struct kobject *kobj, struct kobj_uevent_env *env)
{
	return add_uevent_var(env, "SCXOPS=%s", scx_root->ops.name);
}

static const struct kset_uevent_ops scx_uevent_ops = {
	.uevent = scx_uevent,
};

/*
 * Used by sched_fork() and __setscheduler_prio() to pick the matching
 * sched_class. dl/rt are already handled.
 */
bool task_should_scx(int policy)
{
	if (!scx_enabled() || unlikely(scx_enable_state() == SCX_DISABLING))
		return false;
	if (READ_ONCE(scx_switching_all))
		return true;
	return policy == SCHED_EXT;
}

bool scx_allow_ttwu_queue(const struct task_struct *p)
{
	struct scx_sched *sch;

	if (!scx_enabled())
		return true;

	sch = rcu_dereference_sched(scx_root);
	if (unlikely(!sch))
		return true;

	if (sch->ops.flags & SCX_OPS_ALLOW_QUEUED_WAKEUP)
		return true;

	if (unlikely(p->sched_class != &ext_sched_class))
		return true;

	return false;
}

/**
 * scx_rcu_cpu_stall - sched_ext RCU CPU stall handler
 *
 * While there are various reasons why RCU CPU stalls can occur on a system
 * that may not be caused by the current BPF scheduler, try kicking out the
 * current scheduler in an attempt to recover the system to a good state before
 * issuing panics.
 */
bool scx_rcu_cpu_stall(void)
{
	struct scx_sched *sch;

	rcu_read_lock();

	sch = rcu_dereference(scx_root);
	if (unlikely(!sch)) {
		rcu_read_unlock();
		return false;
	}

	switch (scx_enable_state()) {
	case SCX_ENABLING:
	case SCX_ENABLED:
		break;
	default:
		rcu_read_unlock();
		return false;
	}

	scx_error(sch, "RCU CPU stall detected!");
	rcu_read_unlock();

	return true;
}

/**
 * scx_softlockup - sched_ext softlockup handler
 * @dur_s: number of seconds of CPU stuck due to soft lockup
 *
 * On some multi-socket setups (e.g. 2x Intel 8480c), the BPF scheduler can
 * live-lock the system by making many CPUs target the same DSQ to the point
 * where soft-lockup detection triggers. This function is called from
 * soft-lockup watchdog when the triggering point is close and tries to unjam
 * the system by enabling the breather and aborting the BPF scheduler.
 */
void scx_softlockup(u32 dur_s)
{
	struct scx_sched *sch;

	rcu_read_lock();

	sch = rcu_dereference(scx_root);
	if (unlikely(!sch))
		goto out_unlock;

	switch (scx_enable_state()) {
	case SCX_ENABLING:
	case SCX_ENABLED:
		break;
	default:
		goto out_unlock;
	}

	/* allow only one instance, cleared at the end of scx_bypass() */
	if (test_and_set_bit(0, &scx_in_softlockup))
		goto out_unlock;

	printk_deferred(KERN_ERR "sched_ext: Soft lockup - CPU%d stuck for %us, disabling \"%s\"\n",
			smp_processor_id(), dur_s, scx_root->ops.name);

	/*
	 * Some CPUs may be trapped in the dispatch paths. Enable breather
	 * immediately; otherwise, we might even be able to get to scx_bypass().
	 */
	atomic_inc(&scx_breather_depth);

	scx_error(sch, "soft lockup - CPU#%d stuck for %us", smp_processor_id(), dur_s);
out_unlock:
	rcu_read_unlock();
}

static void scx_clear_softlockup(void)
{
	if (test_and_clear_bit(0, &scx_in_softlockup))
		atomic_dec(&scx_breather_depth);
}

/**
 * scx_bypass - [Un]bypass scx_ops and guarantee forward progress
 * @bypass: true for bypass, false for unbypass
 *
 * Bypassing guarantees that all runnable tasks make forward progress without
 * trusting the BPF scheduler. We can't grab any mutexes or rwsems as they might
 * be held by tasks that the BPF scheduler is forgetting to run, which
 * unfortunately also excludes toggling the static branches.
 *
 * Let's work around by overriding a couple ops and modifying behaviors based on
 * the DISABLING state and then cycling the queued tasks through dequeue/enqueue
 * to force global FIFO scheduling.
 *
 * - ops.select_cpu() is ignored and the default select_cpu() is used.
 *
 * - ops.enqueue() is ignored and tasks are queued in simple global FIFO order.
 *   %SCX_OPS_ENQ_LAST is also ignored.
 *
 * - ops.dispatch() is ignored.
 *
 * - balance_scx() does not set %SCX_RQ_BAL_KEEP on non-zero slice as slice
 *   can't be trusted. Whenever a tick triggers, the running task is rotated to
 *   the tail of the queue with core_sched_at touched.
 *
 * - pick_next_task() suppresses zero slice warning.
 *
 * - scx_kick_cpu() is disabled to avoid irq_work malfunction during PM
 *   operations.
 *
 * - scx_prio_less() reverts to the default core_sched_at order.
 */
static void scx_bypass(bool bypass)
{
	static DEFINE_RAW_SPINLOCK(bypass_lock);
	static unsigned long bypass_timestamp;
	struct scx_sched *sch;
	unsigned long flags;
	int cpu;

	raw_spin_lock_irqsave(&bypass_lock, flags);
	sch = rcu_dereference_bh(scx_root);

	if (bypass) {
		scx_bypass_depth++;
		WARN_ON_ONCE(scx_bypass_depth <= 0);
		if (scx_bypass_depth != 1)
			goto unlock;
		bypass_timestamp = ktime_get_ns();
		if (sch)
			scx_add_event(sch, SCX_EV_BYPASS_ACTIVATE, 1);
	} else {
		scx_bypass_depth--;
		WARN_ON_ONCE(scx_bypass_depth < 0);
		if (scx_bypass_depth != 0)
			goto unlock;
		if (sch)
			scx_add_event(sch, SCX_EV_BYPASS_DURATION,
				      ktime_get_ns() - bypass_timestamp);
	}

	atomic_inc(&scx_breather_depth);

	/*
	 * No task property is changing. We just need to make sure all currently
	 * queued tasks are re-queued according to the new scx_rq_bypassing()
	 * state. As an optimization, walk each rq's runnable_list instead of
	 * the scx_tasks list.
	 *
	 * This function can't trust the scheduler and thus can't use
	 * cpus_read_lock(). Walk all possible CPUs instead of online.
	 */
	for_each_possible_cpu(cpu) {
		struct rq *rq = cpu_rq(cpu);
		struct task_struct *p, *n;

		raw_spin_rq_lock(rq);

		if (bypass) {
			WARN_ON_ONCE(rq->scx.flags & SCX_RQ_BYPASSING);
			rq->scx.flags |= SCX_RQ_BYPASSING;
		} else {
			WARN_ON_ONCE(!(rq->scx.flags & SCX_RQ_BYPASSING));
			rq->scx.flags &= ~SCX_RQ_BYPASSING;
		}

		/*
		 * We need to guarantee that no tasks are on the BPF scheduler
		 * while bypassing. Either we see enabled or the enable path
		 * sees scx_rq_bypassing() before moving tasks to SCX.
		 */
		if (!scx_enabled()) {
			raw_spin_rq_unlock(rq);
			continue;
		}

		/*
		 * The use of list_for_each_entry_safe_reverse() is required
		 * because each task is going to be removed from and added back
		 * to the runnable_list during iteration. Because they're added
		 * to the tail of the list, safe reverse iteration can still
		 * visit all nodes.
		 */
		list_for_each_entry_safe_reverse(p, n, &rq->scx.runnable_list,
						 scx.runnable_node) {
			struct sched_enq_and_set_ctx ctx;

			/* cycling deq/enq is enough, see the function comment */
			sched_deq_and_put_task(p, DEQUEUE_SAVE | DEQUEUE_MOVE, &ctx);
			sched_enq_and_set_task(&ctx);
		}

		/* resched to restore ticks and idle state */
		if (cpu_online(cpu) || cpu == smp_processor_id())
			resched_curr(rq);

		raw_spin_rq_unlock(rq);
	}

	atomic_dec(&scx_breather_depth);
unlock:
	raw_spin_unlock_irqrestore(&bypass_lock, flags);
	scx_clear_softlockup();
}

static void free_exit_info(struct scx_exit_info *ei)
{
	kvfree(ei->dump);
	kfree(ei->msg);
	kfree(ei->bt);
	kfree(ei);
}

static struct scx_exit_info *alloc_exit_info(size_t exit_dump_len)
{
	struct scx_exit_info *ei;

	ei = kzalloc(sizeof(*ei), GFP_KERNEL);
	if (!ei)
		return NULL;

	ei->bt = kcalloc(SCX_EXIT_BT_LEN, sizeof(ei->bt[0]), GFP_KERNEL);
	ei->msg = kzalloc(SCX_EXIT_MSG_LEN, GFP_KERNEL);
	ei->dump = kvzalloc(exit_dump_len, GFP_KERNEL);

	if (!ei->bt || !ei->msg || !ei->dump) {
		free_exit_info(ei);
		return NULL;
	}

	return ei;
}

static const char *scx_exit_reason(enum scx_exit_kind kind)
{
	switch (kind) {
	case SCX_EXIT_UNREG:
		return "unregistered from user space";
	case SCX_EXIT_UNREG_BPF:
		return "unregistered from BPF";
	case SCX_EXIT_UNREG_KERN:
		return "unregistered from the main kernel";
	case SCX_EXIT_SYSRQ:
		return "disabled by sysrq-S";
	case SCX_EXIT_ERROR:
		return "runtime error";
	case SCX_EXIT_ERROR_BPF:
		return "scx_bpf_error";
	case SCX_EXIT_ERROR_STALL:
		return "runnable task stall";
	default:
		return "<UNKNOWN>";
	}
}

static void free_kick_pseqs_rcu(struct rcu_head *rcu)
{
	struct scx_kick_pseqs *pseqs = container_of(rcu, struct scx_kick_pseqs, rcu);

	kvfree(pseqs);
}

static void free_kick_pseqs(void)
{
	int cpu;

	for_each_possible_cpu(cpu) {
		struct scx_kick_pseqs **pseqs = per_cpu_ptr(&scx_kick_pseqs, cpu);
		struct scx_kick_pseqs *to_free;

		to_free = rcu_replace_pointer(*pseqs, NULL, true);
		if (to_free)
			call_rcu(&to_free->rcu, free_kick_pseqs_rcu);
	}
}

static void scx_disable_workfn(struct kthread_work *work)
{
	struct scx_sched *sch = container_of(work, struct scx_sched, disable_work);
	struct scx_exit_info *ei = sch->exit_info;
	struct scx_task_iter sti;
	struct task_struct *p;
	int kind, cpu;

	kind = atomic_read(&sch->exit_kind);
	while (true) {
		if (kind == SCX_EXIT_DONE)	/* already disabled? */
			return;
		WARN_ON_ONCE(kind == SCX_EXIT_NONE);
		if (atomic_try_cmpxchg(&sch->exit_kind, &kind, SCX_EXIT_DONE))
			break;
	}
	ei->kind = kind;
	ei->reason = scx_exit_reason(ei->kind);

	/* guarantee forward progress by bypassing scx_ops */
	scx_bypass(true);

	switch (scx_set_enable_state(SCX_DISABLING)) {
	case SCX_DISABLING:
		WARN_ONCE(true, "sched_ext: duplicate disabling instance?");
		break;
	case SCX_DISABLED:
		pr_warn("sched_ext: ops error detected without ops (%s)\n",
			sch->exit_info->msg);
		WARN_ON_ONCE(scx_set_enable_state(SCX_DISABLED) != SCX_DISABLING);
		goto done;
	default:
		break;
	}

	/*
	 * Here, every runnable task is guaranteed to make forward progress and
	 * we can safely use blocking synchronization constructs. Actually
	 * disable ops.
	 */
	mutex_lock(&scx_enable_mutex);

	static_branch_disable(&__scx_switched_all);
	WRITE_ONCE(scx_switching_all, false);

	/*
	 * Shut down cgroup support before tasks so that the cgroup attach path
	 * doesn't race against scx_exit_task().
	 */
	scx_cgroup_lock();
	scx_cgroup_exit(sch);
	scx_cgroup_unlock();

	/*
	 * The BPF scheduler is going away. All tasks including %TASK_DEAD ones
	 * must be switched out and exited synchronously.
	 */
	percpu_down_write(&scx_fork_rwsem);

	scx_init_task_enabled = false;

	scx_task_iter_start(&sti);
	while ((p = scx_task_iter_next_locked(&sti))) {
		const struct sched_class *old_class = p->sched_class;
		const struct sched_class *new_class =
			__setscheduler_class(p->policy, p->prio);
		struct sched_enq_and_set_ctx ctx;

		if (old_class != new_class && p->se.sched_delayed)
			dequeue_task(task_rq(p), p, DEQUEUE_SLEEP | DEQUEUE_DELAYED);

		sched_deq_and_put_task(p, DEQUEUE_SAVE | DEQUEUE_MOVE, &ctx);

		p->sched_class = new_class;
		check_class_changing(task_rq(p), p, old_class);

		sched_enq_and_set_task(&ctx);

		check_class_changed(task_rq(p), p, old_class, p->prio);
		scx_exit_task(p);
	}
	scx_task_iter_stop(&sti);
	percpu_up_write(&scx_fork_rwsem);

	/*
	 * Invalidate all the rq clocks to prevent getting outdated
	 * rq clocks from a previous scx scheduler.
	 */
	for_each_possible_cpu(cpu) {
		struct rq *rq = cpu_rq(cpu);
		scx_rq_clock_invalidate(rq);
	}

	/* no task is on scx, turn off all the switches and flush in-progress calls */
	static_branch_disable(&__scx_enabled);
	bitmap_zero(sch->has_op, SCX_OPI_END);
	scx_idle_disable();
	synchronize_rcu();

	if (ei->kind >= SCX_EXIT_ERROR) {
		pr_err("sched_ext: BPF scheduler \"%s\" disabled (%s)\n",
		       sch->ops.name, ei->reason);

		if (ei->msg[0] != '\0')
			pr_err("sched_ext: %s: %s\n", sch->ops.name, ei->msg);
#ifdef CONFIG_STACKTRACE
		stack_trace_print(ei->bt, ei->bt_len, 2);
#endif
	} else {
		pr_info("sched_ext: BPF scheduler \"%s\" disabled (%s)\n",
			sch->ops.name, ei->reason);
	}

	if (sch->ops.exit)
		SCX_CALL_OP(sch, SCX_KF_UNLOCKED, exit, NULL, ei);

	cancel_delayed_work_sync(&scx_watchdog_work);

	/*
	 * scx_root clearing must be inside cpus_read_lock(). See
	 * handle_hotplug().
	 */
	cpus_read_lock();
	RCU_INIT_POINTER(scx_root, NULL);
	cpus_read_unlock();

	/*
	 * Delete the kobject from the hierarchy synchronously. Otherwise, sysfs
	 * could observe an object of the same name still in the hierarchy when
	 * the next scheduler is loaded.
	 */
	kobject_del(&sch->kobj);

	free_percpu(scx_dsp_ctx);
	scx_dsp_ctx = NULL;
	scx_dsp_max_batch = 0;
	free_kick_pseqs();

	mutex_unlock(&scx_enable_mutex);

	WARN_ON_ONCE(scx_set_enable_state(SCX_DISABLED) != SCX_DISABLING);
done:
	scx_bypass(false);
}

static void scx_disable(enum scx_exit_kind kind)
{
	int none = SCX_EXIT_NONE;
	struct scx_sched *sch;

	if (WARN_ON_ONCE(kind == SCX_EXIT_NONE || kind == SCX_EXIT_DONE))
		kind = SCX_EXIT_ERROR;

	rcu_read_lock();
	sch = rcu_dereference(scx_root);
	if (sch) {
		atomic_try_cmpxchg(&sch->exit_kind, &none, kind);
		kthread_queue_work(sch->helper, &sch->disable_work);
	}
	rcu_read_unlock();
}

static void dump_newline(struct seq_buf *s)
{
	trace_sched_ext_dump("");

	/* @s may be zero sized and seq_buf triggers WARN if so */
	if (s->size)
		seq_buf_putc(s, '\n');
}

static __printf(2, 3) void dump_line(struct seq_buf *s, const char *fmt, ...)
{
	va_list args;

#ifdef CONFIG_TRACEPOINTS
	if (trace_sched_ext_dump_enabled()) {
		/* protected by scx_dump_state()::dump_lock */
		static char line_buf[SCX_EXIT_MSG_LEN];

		va_start(args, fmt);
		vscnprintf(line_buf, sizeof(line_buf), fmt, args);
		va_end(args);

		trace_sched_ext_dump(line_buf);
	}
#endif
	/* @s may be zero sized and seq_buf triggers WARN if so */
	if (s->size) {
		va_start(args, fmt);
		seq_buf_vprintf(s, fmt, args);
		va_end(args);

		seq_buf_putc(s, '\n');
	}
}

static void dump_stack_trace(struct seq_buf *s, const char *prefix,
			     const unsigned long *bt, unsigned int len)
{
	unsigned int i;

	for (i = 0; i < len; i++)
		dump_line(s, "%s%pS", prefix, (void *)bt[i]);
}

static void ops_dump_init(struct seq_buf *s, const char *prefix)
{
	struct scx_dump_data *dd = &scx_dump_data;

	lockdep_assert_irqs_disabled();

	dd->cpu = smp_processor_id();		/* allow scx_bpf_dump() */
	dd->first = true;
	dd->cursor = 0;
	dd->s = s;
	dd->prefix = prefix;
}

static void ops_dump_flush(void)
{
	struct scx_dump_data *dd = &scx_dump_data;
	char *line = dd->buf.line;

	if (!dd->cursor)
		return;

	/*
	 * There's something to flush and this is the first line. Insert a blank
	 * line to distinguish ops dump.
	 */
	if (dd->first) {
		dump_newline(dd->s);
		dd->first = false;
	}

	/*
	 * There may be multiple lines in $line. Scan and emit each line
	 * separately.
	 */
	while (true) {
		char *end = line;
		char c;

		while (*end != '\n' && *end != '\0')
			end++;

		/*
		 * If $line overflowed, it may not have newline at the end.
		 * Always emit with a newline.
		 */
		c = *end;
		*end = '\0';
		dump_line(dd->s, "%s%s", dd->prefix, line);
		if (c == '\0')
			break;

		/* move to the next line */
		end++;
		if (*end == '\0')
			break;
		line = end;
	}

	dd->cursor = 0;
}

static void ops_dump_exit(void)
{
	ops_dump_flush();
	scx_dump_data.cpu = -1;
}

static void scx_dump_task(struct seq_buf *s, struct scx_dump_ctx *dctx,
			  struct task_struct *p, char marker)
{
	static unsigned long bt[SCX_EXIT_BT_LEN];
	struct scx_sched *sch = scx_root;
	char dsq_id_buf[19] = "(n/a)";
	unsigned long ops_state = atomic_long_read(&p->scx.ops_state);
	unsigned int bt_len = 0;

	if (p->scx.dsq)
		scnprintf(dsq_id_buf, sizeof(dsq_id_buf), "0x%llx",
			  (unsigned long long)p->scx.dsq->id);

	dump_newline(s);
	dump_line(s, " %c%c %s[%d] %+ldms",
		  marker, task_state_to_char(p), p->comm, p->pid,
		  jiffies_delta_msecs(p->scx.runnable_at, dctx->at_jiffies));
	dump_line(s, "      scx_state/flags=%u/0x%x dsq_flags=0x%x ops_state/qseq=%lu/%lu",
		  scx_get_task_state(p), p->scx.flags & ~SCX_TASK_STATE_MASK,
		  p->scx.dsq_flags, ops_state & SCX_OPSS_STATE_MASK,
		  ops_state >> SCX_OPSS_QSEQ_SHIFT);
	dump_line(s, "      sticky/holding_cpu=%d/%d dsq_id=%s",
		  p->scx.sticky_cpu, p->scx.holding_cpu, dsq_id_buf);
	dump_line(s, "      dsq_vtime=%llu slice=%llu weight=%u",
		  p->scx.dsq_vtime, p->scx.slice, p->scx.weight);
	dump_line(s, "      cpus=%*pb no_mig=%u", cpumask_pr_args(p->cpus_ptr),
		  p->migration_disabled);

	if (SCX_HAS_OP(sch, dump_task)) {
		ops_dump_init(s, "    ");
		SCX_CALL_OP(sch, SCX_KF_REST, dump_task, NULL, dctx, p);
		ops_dump_exit();
	}

#ifdef CONFIG_STACKTRACE
	bt_len = stack_trace_save_tsk(p, bt, SCX_EXIT_BT_LEN, 1);
#endif
	if (bt_len) {
		dump_newline(s);
		dump_stack_trace(s, "    ", bt, bt_len);
	}
}

static void scx_dump_state(struct scx_exit_info *ei, size_t dump_len)
{
	static DEFINE_SPINLOCK(dump_lock);
	static const char trunc_marker[] = "\n\n~~~~ TRUNCATED ~~~~\n";
	struct scx_sched *sch = scx_root;
	struct scx_dump_ctx dctx = {
		.kind = ei->kind,
		.exit_code = ei->exit_code,
		.reason = ei->reason,
		.at_ns = ktime_get_ns(),
		.at_jiffies = jiffies,
	};
	struct seq_buf s;
	struct scx_event_stats events;
	unsigned long flags;
	char *buf;
	int cpu;

	spin_lock_irqsave(&dump_lock, flags);

	seq_buf_init(&s, ei->dump, dump_len);

	if (ei->kind == SCX_EXIT_NONE) {
		dump_line(&s, "Debug dump triggered by %s", ei->reason);
	} else {
		dump_line(&s, "%s[%d] triggered exit kind %d:",
			  current->comm, current->pid, ei->kind);
		dump_line(&s, "  %s (%s)", ei->reason, ei->msg);
		dump_newline(&s);
		dump_line(&s, "Backtrace:");
		dump_stack_trace(&s, "  ", ei->bt, ei->bt_len);
	}

	if (SCX_HAS_OP(sch, dump)) {
		ops_dump_init(&s, "");
		SCX_CALL_OP(sch, SCX_KF_UNLOCKED, dump, NULL, &dctx);
		ops_dump_exit();
	}

	dump_newline(&s);
	dump_line(&s, "CPU states");
	dump_line(&s, "----------");

	for_each_possible_cpu(cpu) {
		struct rq *rq = cpu_rq(cpu);
		struct rq_flags rf;
		struct task_struct *p;
		struct seq_buf ns;
		size_t avail, used;
		bool idle;

		rq_lock_irqsave(rq, &rf);

		idle = list_empty(&rq->scx.runnable_list) &&
			rq->curr->sched_class == &idle_sched_class;

		if (idle && !SCX_HAS_OP(sch, dump_cpu))
			goto next;

		/*
		 * We don't yet know whether ops.dump_cpu() will produce output
		 * and we may want to skip the default CPU dump if it doesn't.
		 * Use a nested seq_buf to generate the standard dump so that we
		 * can decide whether to commit later.
		 */
		avail = seq_buf_get_buf(&s, &buf);
		seq_buf_init(&ns, buf, avail);

		dump_newline(&ns);
		dump_line(&ns, "CPU %-4d: nr_run=%u flags=0x%x cpu_rel=%d ops_qseq=%lu pnt_seq=%lu",
			  cpu, rq->scx.nr_running, rq->scx.flags,
			  rq->scx.cpu_released, rq->scx.ops_qseq,
			  rq->scx.pnt_seq);
		dump_line(&ns, "          curr=%s[%d] class=%ps",
			  rq->curr->comm, rq->curr->pid,
			  rq->curr->sched_class);
		if (!cpumask_empty(rq->scx.cpus_to_kick))
			dump_line(&ns, "  cpus_to_kick   : %*pb",
				  cpumask_pr_args(rq->scx.cpus_to_kick));
		if (!cpumask_empty(rq->scx.cpus_to_kick_if_idle))
			dump_line(&ns, "  idle_to_kick   : %*pb",
				  cpumask_pr_args(rq->scx.cpus_to_kick_if_idle));
		if (!cpumask_empty(rq->scx.cpus_to_preempt))
			dump_line(&ns, "  cpus_to_preempt: %*pb",
				  cpumask_pr_args(rq->scx.cpus_to_preempt));
		if (!cpumask_empty(rq->scx.cpus_to_wait))
			dump_line(&ns, "  cpus_to_wait   : %*pb",
				  cpumask_pr_args(rq->scx.cpus_to_wait));

		used = seq_buf_used(&ns);
		if (SCX_HAS_OP(sch, dump_cpu)) {
			ops_dump_init(&ns, "  ");
			SCX_CALL_OP(sch, SCX_KF_REST, dump_cpu, NULL,
				    &dctx, cpu, idle);
			ops_dump_exit();
		}

		/*
		 * If idle && nothing generated by ops.dump_cpu(), there's
		 * nothing interesting. Skip.
		 */
		if (idle && used == seq_buf_used(&ns))
			goto next;

		/*
		 * $s may already have overflowed when $ns was created. If so,
		 * calling commit on it will trigger BUG.
		 */
		if (avail) {
			seq_buf_commit(&s, seq_buf_used(&ns));
			if (seq_buf_has_overflowed(&ns))
				seq_buf_set_overflow(&s);
		}

		if (rq->curr->sched_class == &ext_sched_class)
			scx_dump_task(&s, &dctx, rq->curr, '*');

		list_for_each_entry(p, &rq->scx.runnable_list, scx.runnable_node)
			scx_dump_task(&s, &dctx, p, ' ');
	next:
		rq_unlock_irqrestore(rq, &rf);
	}

	dump_newline(&s);
	dump_line(&s, "Event counters");
	dump_line(&s, "--------------");

	scx_read_events(sch, &events);
	scx_dump_event(s, &events, SCX_EV_SELECT_CPU_FALLBACK);
	scx_dump_event(s, &events, SCX_EV_DISPATCH_LOCAL_DSQ_OFFLINE);
	scx_dump_event(s, &events, SCX_EV_DISPATCH_KEEP_LAST);
	scx_dump_event(s, &events, SCX_EV_ENQ_SKIP_EXITING);
	scx_dump_event(s, &events, SCX_EV_ENQ_SKIP_MIGRATION_DISABLED);
	scx_dump_event(s, &events, SCX_EV_REFILL_SLICE_DFL);
	scx_dump_event(s, &events, SCX_EV_BYPASS_DURATION);
	scx_dump_event(s, &events, SCX_EV_BYPASS_DISPATCH);
	scx_dump_event(s, &events, SCX_EV_BYPASS_ACTIVATE);

	if (seq_buf_has_overflowed(&s) && dump_len >= sizeof(trunc_marker))
		memcpy(ei->dump + dump_len - sizeof(trunc_marker),
		       trunc_marker, sizeof(trunc_marker));

	spin_unlock_irqrestore(&dump_lock, flags);
}

static void scx_error_irq_workfn(struct irq_work *irq_work)
{
	struct scx_sched *sch = container_of(irq_work, struct scx_sched, error_irq_work);
	struct scx_exit_info *ei = sch->exit_info;

	if (ei->kind >= SCX_EXIT_ERROR)
		scx_dump_state(ei, sch->ops.exit_dump_len);

	kthread_queue_work(sch->helper, &sch->disable_work);
}

static void scx_vexit(struct scx_sched *sch,
		      enum scx_exit_kind kind, s64 exit_code,
		      const char *fmt, va_list args)
{
	struct scx_exit_info *ei = sch->exit_info;
	int none = SCX_EXIT_NONE;

	if (!atomic_try_cmpxchg(&sch->exit_kind, &none, kind))
		return;

	ei->exit_code = exit_code;
#ifdef CONFIG_STACKTRACE
	if (kind >= SCX_EXIT_ERROR)
		ei->bt_len = stack_trace_save(ei->bt, SCX_EXIT_BT_LEN, 1);
#endif
	vscnprintf(ei->msg, SCX_EXIT_MSG_LEN, fmt, args);

	/*
	 * Set ei->kind and ->reason for scx_dump_state(). They'll be set again
	 * in scx_disable_workfn().
	 */
	ei->kind = kind;
	ei->reason = scx_exit_reason(ei->kind);

	irq_work_queue(&sch->error_irq_work);
}

static int alloc_kick_pseqs(void)
{
	int cpu;

	/*
	 * Allocate per-CPU arrays sized by nr_cpu_ids. Use kvzalloc as size
	 * can exceed percpu allocator limits on large machines.
	 */
	for_each_possible_cpu(cpu) {
		struct scx_kick_pseqs **pseqs = per_cpu_ptr(&scx_kick_pseqs, cpu);
		struct scx_kick_pseqs *new_pseqs;

		WARN_ON_ONCE(rcu_access_pointer(*pseqs));

		new_pseqs = kvzalloc_node(struct_size(new_pseqs, seqs, nr_cpu_ids),
					  GFP_KERNEL, cpu_to_node(cpu));
		if (!new_pseqs) {
			free_kick_pseqs();
			return -ENOMEM;
		}

		rcu_assign_pointer(*pseqs, new_pseqs);
	}

	return 0;
}

static struct scx_sched *scx_alloc_and_add_sched(struct sched_ext_ops *ops)
{
	struct scx_sched *sch;
	int node, ret;

	sch = kzalloc(sizeof(*sch), GFP_KERNEL);
	if (!sch)
		return ERR_PTR(-ENOMEM);

	sch->exit_info = alloc_exit_info(ops->exit_dump_len);
	if (!sch->exit_info) {
		ret = -ENOMEM;
		goto err_free_sch;
	}

	ret = rhashtable_init(&sch->dsq_hash, &dsq_hash_params);
	if (ret < 0)
		goto err_free_ei;

	sch->global_dsqs = kcalloc(nr_node_ids, sizeof(sch->global_dsqs[0]),
				   GFP_KERNEL);
	if (!sch->global_dsqs) {
		ret = -ENOMEM;
		goto err_free_hash;
	}

	for_each_node_state(node, N_POSSIBLE) {
		struct scx_dispatch_q *dsq;

		dsq = kzalloc_node(sizeof(*dsq), GFP_KERNEL, node);
		if (!dsq) {
			ret = -ENOMEM;
			goto err_free_gdsqs;
		}

		init_dsq(dsq, SCX_DSQ_GLOBAL);
		sch->global_dsqs[node] = dsq;
	}

	sch->pcpu = alloc_percpu(struct scx_sched_pcpu);
	if (!sch->pcpu)
		goto err_free_gdsqs;

	sch->helper = kthread_run_worker(0, "sched_ext_helper");
	if (IS_ERR(sch->helper)) {
		ret = PTR_ERR(sch->helper);
		goto err_free_pcpu;
	}

	sched_set_fifo(sch->helper->task);

	atomic_set(&sch->exit_kind, SCX_EXIT_NONE);
	init_irq_work(&sch->error_irq_work, scx_error_irq_workfn);
	kthread_init_work(&sch->disable_work, scx_disable_workfn);
	sch->ops = *ops;
	ops->priv = sch;

	sch->kobj.kset = scx_kset;
	ret = kobject_init_and_add(&sch->kobj, &scx_ktype, NULL, "root");
	if (ret < 0)
		goto err_stop_helper;

	return sch;

err_stop_helper:
	kthread_stop(sch->helper->task);
err_free_pcpu:
	free_percpu(sch->pcpu);
err_free_gdsqs:
	for_each_node_state(node, N_POSSIBLE)
		kfree(sch->global_dsqs[node]);
	kfree(sch->global_dsqs);
err_free_hash:
	rhashtable_free_and_destroy(&sch->dsq_hash, NULL, NULL);
err_free_ei:
	free_exit_info(sch->exit_info);
err_free_sch:
	kfree(sch);
	return ERR_PTR(ret);
}

static void check_hotplug_seq(struct scx_sched *sch,
			      const struct sched_ext_ops *ops)
{
	unsigned long long global_hotplug_seq;

	/*
	 * If a hotplug event has occurred between when a scheduler was
	 * initialized, and when we were able to attach, exit and notify user
	 * space about it.
	 */
	if (ops->hotplug_seq) {
		global_hotplug_seq = atomic_long_read(&scx_hotplug_seq);
		if (ops->hotplug_seq != global_hotplug_seq) {
			scx_exit(sch, SCX_EXIT_UNREG_KERN,
				 SCX_ECODE_ACT_RESTART | SCX_ECODE_RSN_HOTPLUG,
				 "expected hotplug seq %llu did not match actual %llu",
				 ops->hotplug_seq, global_hotplug_seq);
		}
	}
}

static int validate_ops(struct scx_sched *sch, const struct sched_ext_ops *ops)
{
	/*
	 * It doesn't make sense to specify the SCX_OPS_ENQ_LAST flag if the
	 * ops.enqueue() callback isn't implemented.
	 */
	if ((ops->flags & SCX_OPS_ENQ_LAST) && !ops->enqueue) {
		scx_error(sch, "SCX_OPS_ENQ_LAST requires ops.enqueue() to be implemented");
		return -EINVAL;
	}

	/*
	 * SCX_OPS_BUILTIN_IDLE_PER_NODE requires built-in CPU idle
	 * selection policy to be enabled.
	 */
	if ((ops->flags & SCX_OPS_BUILTIN_IDLE_PER_NODE) &&
	    (ops->update_idle && !(ops->flags & SCX_OPS_KEEP_BUILTIN_IDLE))) {
		scx_error(sch, "SCX_OPS_BUILTIN_IDLE_PER_NODE requires CPU idle selection enabled");
		return -EINVAL;
	}

	if (ops->flags & SCX_OPS_HAS_CGROUP_WEIGHT)
		pr_warn("SCX_OPS_HAS_CGROUP_WEIGHT is deprecated and a noop\n");

	return 0;
}

static int scx_enable(struct sched_ext_ops *ops, struct bpf_link *link)
{
	struct scx_sched *sch;
	struct scx_task_iter sti;
	struct task_struct *p;
	unsigned long timeout;
	int i, cpu, ret;

	if (!cpumask_equal(housekeeping_cpumask(HK_TYPE_DOMAIN),
			   cpu_possible_mask)) {
		pr_err("sched_ext: Not compatible with \"isolcpus=\" domain isolation\n");
		return -EINVAL;
	}

	mutex_lock(&scx_enable_mutex);

	if (scx_enable_state() != SCX_DISABLED) {
		ret = -EBUSY;
		goto err_unlock;
	}

	ret = alloc_kick_pseqs();
	if (ret)
		goto err_unlock;

	sch = scx_alloc_and_add_sched(ops);
	if (IS_ERR(sch)) {
		ret = PTR_ERR(sch);
		goto err_free_pseqs;
	}

	/*
	 * Transition to ENABLING and clear exit info to arm the disable path.
	 * Failure triggers full disabling from here on.
	 */
	WARN_ON_ONCE(scx_set_enable_state(SCX_ENABLING) != SCX_DISABLED);
	WARN_ON_ONCE(scx_root);

	atomic_long_set(&scx_nr_rejected, 0);

	for_each_possible_cpu(cpu)
		cpu_rq(cpu)->scx.cpuperf_target = SCX_CPUPERF_ONE;

	/*
	 * Keep CPUs stable during enable so that the BPF scheduler can track
	 * online CPUs by watching ->on/offline_cpu() after ->init().
	 */
	cpus_read_lock();

	/*
	 * Make the scheduler instance visible. Must be inside cpus_read_lock().
	 * See handle_hotplug().
	 */
	rcu_assign_pointer(scx_root, sch);

	scx_idle_enable(ops);

	if (sch->ops.init) {
		ret = SCX_CALL_OP_RET(sch, SCX_KF_UNLOCKED, init, NULL);
		if (ret) {
			ret = ops_sanitize_err(sch, "init", ret);
			cpus_read_unlock();
			scx_error(sch, "ops.init() failed (%d)", ret);
			goto err_disable;
		}
		sch->exit_info->flags |= SCX_EFLAG_INITIALIZED;
	}

	for (i = SCX_OPI_CPU_HOTPLUG_BEGIN; i < SCX_OPI_CPU_HOTPLUG_END; i++)
		if (((void (**)(void))ops)[i])
			set_bit(i, sch->has_op);

	check_hotplug_seq(sch, ops);
	scx_idle_update_selcpu_topology(ops);

	cpus_read_unlock();

	ret = validate_ops(sch, ops);
	if (ret)
		goto err_disable;

	WARN_ON_ONCE(scx_dsp_ctx);
	scx_dsp_max_batch = ops->dispatch_max_batch ?: SCX_DSP_DFL_MAX_BATCH;
	scx_dsp_ctx = __alloc_percpu(struct_size_t(struct scx_dsp_ctx, buf,
						   scx_dsp_max_batch),
				     __alignof__(struct scx_dsp_ctx));
	if (!scx_dsp_ctx) {
		ret = -ENOMEM;
		goto err_disable;
	}

	if (ops->timeout_ms)
		timeout = msecs_to_jiffies(ops->timeout_ms);
	else
		timeout = SCX_WATCHDOG_MAX_TIMEOUT;

	WRITE_ONCE(scx_watchdog_timeout, timeout);
	WRITE_ONCE(scx_watchdog_timestamp, jiffies);
	queue_delayed_work(system_unbound_wq, &scx_watchdog_work,
			   scx_watchdog_timeout / 2);

	/*
	 * Once __scx_enabled is set, %current can be switched to SCX anytime.
	 * This can lead to stalls as some BPF schedulers (e.g. userspace
	 * scheduling) may not function correctly before all tasks are switched.
	 * Init in bypass mode to guarantee forward progress.
	 */
	scx_bypass(true);

	for (i = SCX_OPI_NORMAL_BEGIN; i < SCX_OPI_NORMAL_END; i++)
		if (((void (**)(void))ops)[i])
			set_bit(i, sch->has_op);

	if (sch->ops.cpu_acquire || sch->ops.cpu_release)
		sch->ops.flags |= SCX_OPS_HAS_CPU_PREEMPT;

	/*
	 * Lock out forks, cgroup on/offlining and moves before opening the
	 * floodgate so that they don't wander into the operations prematurely.
	 */
	percpu_down_write(&scx_fork_rwsem);

	WARN_ON_ONCE(scx_init_task_enabled);
	scx_init_task_enabled = true;

	/*
	 * Enable ops for every task. Fork is excluded by scx_fork_rwsem
	 * preventing new tasks from being added. No need to exclude tasks
	 * leaving as sched_ext_free() can handle both prepped and enabled
	 * tasks. Prep all tasks first and then enable them with preemption
	 * disabled.
	 *
	 * All cgroups should be initialized before scx_init_task() so that the
	 * BPF scheduler can reliably track each task's cgroup membership from
	 * scx_init_task(). Lock out cgroup on/offlining and task migrations
	 * while tasks are being initialized so that scx_cgroup_can_attach()
	 * never sees uninitialized tasks.
	 */
	scx_cgroup_lock();
	ret = scx_cgroup_init(sch);
	if (ret)
		goto err_disable_unlock_all;

	scx_task_iter_start(&sti);
	while ((p = scx_task_iter_next_locked(&sti))) {
		/*
		 * @p may already be dead, have lost all its usages counts and
		 * be waiting for RCU grace period before being freed. @p can't
		 * be initialized for SCX in such cases and should be ignored.
		 */
		if (!tryget_task_struct(p))
			continue;

		scx_task_iter_unlock(&sti);

		ret = scx_init_task(p, task_group(p), false);
		if (ret) {
			put_task_struct(p);
			scx_task_iter_stop(&sti);
			scx_error(sch, "ops.init_task() failed (%d) for %s[%d]",
				  ret, p->comm, p->pid);
			goto err_disable_unlock_all;
		}

		scx_set_task_state(p, SCX_TASK_READY);

		put_task_struct(p);
	}
	scx_task_iter_stop(&sti);
	scx_cgroup_unlock();
	percpu_up_write(&scx_fork_rwsem);

	/*
	 * All tasks are READY. It's safe to turn on scx_enabled() and switch
	 * all eligible tasks.
	 */
	WRITE_ONCE(scx_switching_all, !(ops->flags & SCX_OPS_SWITCH_PARTIAL));
	static_branch_enable(&__scx_enabled);

	/*
	 * We're fully committed and can't fail. The task READY -> ENABLED
	 * transitions here are synchronized against sched_ext_free() through
	 * scx_tasks_lock.
	 */
	percpu_down_write(&scx_fork_rwsem);
	scx_task_iter_start(&sti);
	while ((p = scx_task_iter_next_locked(&sti))) {
		const struct sched_class *old_class = p->sched_class;
		const struct sched_class *new_class =
			__setscheduler_class(p->policy, p->prio);
		struct sched_enq_and_set_ctx ctx;

		if (!tryget_task_struct(p))
			continue;

		if (old_class != new_class && p->se.sched_delayed)
			dequeue_task(task_rq(p), p, DEQUEUE_SLEEP | DEQUEUE_DELAYED);

		sched_deq_and_put_task(p, DEQUEUE_SAVE | DEQUEUE_MOVE, &ctx);

		p->scx.slice = SCX_SLICE_DFL;
		p->sched_class = new_class;
		check_class_changing(task_rq(p), p, old_class);

		sched_enq_and_set_task(&ctx);

		check_class_changed(task_rq(p), p, old_class, p->prio);
		put_task_struct(p);
	}
	scx_task_iter_stop(&sti);
	percpu_up_write(&scx_fork_rwsem);

	scx_bypass(false);

	if (!scx_tryset_enable_state(SCX_ENABLED, SCX_ENABLING)) {
		WARN_ON_ONCE(atomic_read(&sch->exit_kind) == SCX_EXIT_NONE);
		goto err_disable;
	}

	if (!(ops->flags & SCX_OPS_SWITCH_PARTIAL))
		static_branch_enable(&__scx_switched_all);

	pr_info("sched_ext: BPF scheduler \"%s\" enabled%s\n",
		sch->ops.name, scx_switched_all() ? "" : " (partial)");
	kobject_uevent(&sch->kobj, KOBJ_ADD);
	mutex_unlock(&scx_enable_mutex);

	atomic_long_inc(&scx_enable_seq);

	return 0;

err_free_pseqs:
	free_kick_pseqs();
err_unlock:
	mutex_unlock(&scx_enable_mutex);
	return ret;

err_disable_unlock_all:
	scx_cgroup_unlock();
	percpu_up_write(&scx_fork_rwsem);
	/* we'll soon enter disable path, keep bypass on */
err_disable:
	mutex_unlock(&scx_enable_mutex);
	/*
	 * Returning an error code here would not pass all the error information
	 * to userspace. Record errno using scx_error() for cases scx_error()
	 * wasn't already invoked and exit indicating success so that the error
	 * is notified through ops.exit() with all the details.
	 *
	 * Flush scx_disable_work to ensure that error is reported before init
	 * completion. sch's base reference will be put by bpf_scx_unreg().
	 */
	scx_error(sch, "scx_enable() failed (%d)", ret);
	kthread_flush_work(&sch->disable_work);
	return 0;
}


/********************************************************************************
 * bpf_struct_ops plumbing.
 */
#include <linux/bpf_verifier.h>
#include <linux/bpf.h>
#include <linux/btf.h>

static const struct btf_type *task_struct_type;

static bool bpf_scx_is_valid_access(int off, int size,
				    enum bpf_access_type type,
				    const struct bpf_prog *prog,
				    struct bpf_insn_access_aux *info)
{
	if (type != BPF_READ)
		return false;
	if (off < 0 || off >= sizeof(__u64) * MAX_BPF_FUNC_ARGS)
		return false;
	if (off % size != 0)
		return false;

	return btf_ctx_access(off, size, type, prog, info);
}

static int bpf_scx_btf_struct_access(struct bpf_verifier_log *log,
				     const struct bpf_reg_state *reg, int off,
				     int size)
{
	const struct btf_type *t;

	t = btf_type_by_id(reg->btf, reg->btf_id);
	if (t == task_struct_type) {
		if (off >= offsetof(struct task_struct, scx.slice) &&
		    off + size <= offsetofend(struct task_struct, scx.slice))
			return SCALAR_VALUE;
		if (off >= offsetof(struct task_struct, scx.dsq_vtime) &&
		    off + size <= offsetofend(struct task_struct, scx.dsq_vtime))
			return SCALAR_VALUE;
		if (off >= offsetof(struct task_struct, scx.disallow) &&
		    off + size <= offsetofend(struct task_struct, scx.disallow))
			return SCALAR_VALUE;
	}

	return -EACCES;
}

static const struct bpf_verifier_ops bpf_scx_verifier_ops = {
	.get_func_proto = bpf_base_func_proto,
	.is_valid_access = bpf_scx_is_valid_access,
	.btf_struct_access = bpf_scx_btf_struct_access,
};

static int bpf_scx_init_member(const struct btf_type *t,
			       const struct btf_member *member,
			       void *kdata, const void *udata)
{
	const struct sched_ext_ops *uops = udata;
	struct sched_ext_ops *ops = kdata;
	u32 moff = __btf_member_bit_offset(t, member) / 8;
	int ret;

	switch (moff) {
	case offsetof(struct sched_ext_ops, dispatch_max_batch):
		if (*(u32 *)(udata + moff) > INT_MAX)
			return -E2BIG;
		ops->dispatch_max_batch = *(u32 *)(udata + moff);
		return 1;
	case offsetof(struct sched_ext_ops, flags):
		if (*(u64 *)(udata + moff) & ~SCX_OPS_ALL_FLAGS)
			return -EINVAL;
		ops->flags = *(u64 *)(udata + moff);
		return 1;
	case offsetof(struct sched_ext_ops, name):
		ret = bpf_obj_name_cpy(ops->name, uops->name,
				       sizeof(ops->name));
		if (ret < 0)
			return ret;
		if (ret == 0)
			return -EINVAL;
		return 1;
	case offsetof(struct sched_ext_ops, timeout_ms):
		if (msecs_to_jiffies(*(u32 *)(udata + moff)) >
		    SCX_WATCHDOG_MAX_TIMEOUT)
			return -E2BIG;
		ops->timeout_ms = *(u32 *)(udata + moff);
		return 1;
	case offsetof(struct sched_ext_ops, exit_dump_len):
		ops->exit_dump_len =
			*(u32 *)(udata + moff) ?: SCX_EXIT_DUMP_DFL_LEN;
		return 1;
	case offsetof(struct sched_ext_ops, hotplug_seq):
		ops->hotplug_seq = *(u64 *)(udata + moff);
		return 1;
	}

	return 0;
}

static int bpf_scx_check_member(const struct btf_type *t,
				const struct btf_member *member,
				const struct bpf_prog *prog)
{
	u32 moff = __btf_member_bit_offset(t, member) / 8;

	switch (moff) {
	case offsetof(struct sched_ext_ops, init_task):
#ifdef CONFIG_EXT_GROUP_SCHED
	case offsetof(struct sched_ext_ops, cgroup_init):
	case offsetof(struct sched_ext_ops, cgroup_exit):
	case offsetof(struct sched_ext_ops, cgroup_prep_move):
#endif
	case offsetof(struct sched_ext_ops, cpu_online):
	case offsetof(struct sched_ext_ops, cpu_offline):
	case offsetof(struct sched_ext_ops, init):
	case offsetof(struct sched_ext_ops, exit):
		break;
	default:
		if (prog->sleepable)
			return -EINVAL;
	}

	return 0;
}

static int bpf_scx_reg(void *kdata, struct bpf_link *link)
{
	return scx_enable(kdata, link);
}

static void bpf_scx_unreg(void *kdata, struct bpf_link *link)
{
	struct sched_ext_ops *ops = kdata;
	struct scx_sched *sch = ops->priv;

	scx_disable(SCX_EXIT_UNREG);
	kthread_flush_work(&sch->disable_work);
	kobject_put(&sch->kobj);
}

static int bpf_scx_init(struct btf *btf)
{
	task_struct_type = btf_type_by_id(btf, btf_tracing_ids[BTF_TRACING_TYPE_TASK]);

	return 0;
}

static int bpf_scx_update(void *kdata, void *old_kdata, struct bpf_link *link)
{
	/*
	 * sched_ext does not support updating the actively-loaded BPF
	 * scheduler, as registering a BPF scheduler can always fail if the
	 * scheduler returns an error code for e.g. ops.init(), ops.init_task(),
	 * etc. Similarly, we can always race with unregistration happening
	 * elsewhere, such as with sysrq.
	 */
	return -EOPNOTSUPP;
}

static int bpf_scx_validate(void *kdata)
{
	return 0;
}

static s32 sched_ext_ops__select_cpu(struct task_struct *p, s32 prev_cpu, u64 wake_flags) { return -EINVAL; }
static void sched_ext_ops__enqueue(struct task_struct *p, u64 enq_flags) {}
static void sched_ext_ops__dequeue(struct task_struct *p, u64 enq_flags) {}
static void sched_ext_ops__dispatch(s32 prev_cpu, struct task_struct *prev__nullable) {}
static void sched_ext_ops__tick(struct task_struct *p) {}
static void sched_ext_ops__runnable(struct task_struct *p, u64 enq_flags) {}
static void sched_ext_ops__running(struct task_struct *p) {}
static void sched_ext_ops__stopping(struct task_struct *p, bool runnable) {}
static void sched_ext_ops__quiescent(struct task_struct *p, u64 deq_flags) {}
static bool sched_ext_ops__yield(struct task_struct *from, struct task_struct *to__nullable) { return false; }
static bool sched_ext_ops__core_sched_before(struct task_struct *a, struct task_struct *b) { return false; }
static void sched_ext_ops__set_weight(struct task_struct *p, u32 weight) {}
static void sched_ext_ops__set_cpumask(struct task_struct *p, const struct cpumask *mask) {}
static void sched_ext_ops__update_idle(s32 cpu, bool idle) {}
static void sched_ext_ops__cpu_acquire(s32 cpu, struct scx_cpu_acquire_args *args) {}
static void sched_ext_ops__cpu_release(s32 cpu, struct scx_cpu_release_args *args) {}
static s32 sched_ext_ops__init_task(struct task_struct *p, struct scx_init_task_args *args) { return -EINVAL; }
static void sched_ext_ops__exit_task(struct task_struct *p, struct scx_exit_task_args *args) {}
static void sched_ext_ops__enable(struct task_struct *p) {}
static void sched_ext_ops__disable(struct task_struct *p) {}
#ifdef CONFIG_EXT_GROUP_SCHED
static s32 sched_ext_ops__cgroup_init(struct cgroup *cgrp, struct scx_cgroup_init_args *args) { return -EINVAL; }
static void sched_ext_ops__cgroup_exit(struct cgroup *cgrp) {}
static s32 sched_ext_ops__cgroup_prep_move(struct task_struct *p, struct cgroup *from, struct cgroup *to) { return -EINVAL; }
static void sched_ext_ops__cgroup_move(struct task_struct *p, struct cgroup *from, struct cgroup *to) {}
static void sched_ext_ops__cgroup_cancel_move(struct task_struct *p, struct cgroup *from, struct cgroup *to) {}
static void sched_ext_ops__cgroup_set_weight(struct cgroup *cgrp, u32 weight) {}
static void sched_ext_ops__cgroup_set_bandwidth(struct cgroup *cgrp, u64 period_us, u64 quota_us, u64 burst_us) {}
#endif
static void sched_ext_ops__cpu_online(s32 cpu) {}
static void sched_ext_ops__cpu_offline(s32 cpu) {}
static s32 sched_ext_ops__init(void) { return -EINVAL; }
static void sched_ext_ops__exit(struct scx_exit_info *info) {}
static void sched_ext_ops__dump(struct scx_dump_ctx *ctx) {}
static void sched_ext_ops__dump_cpu(struct scx_dump_ctx *ctx, s32 cpu, bool idle) {}
static void sched_ext_ops__dump_task(struct scx_dump_ctx *ctx, struct task_struct *p) {}

static struct sched_ext_ops __bpf_ops_sched_ext_ops = {
	.select_cpu		= sched_ext_ops__select_cpu,
	.enqueue		= sched_ext_ops__enqueue,
	.dequeue		= sched_ext_ops__dequeue,
	.dispatch		= sched_ext_ops__dispatch,
	.tick			= sched_ext_ops__tick,
	.runnable		= sched_ext_ops__runnable,
	.running		= sched_ext_ops__running,
	.stopping		= sched_ext_ops__stopping,
	.quiescent		= sched_ext_ops__quiescent,
	.yield			= sched_ext_ops__yield,
	.core_sched_before	= sched_ext_ops__core_sched_before,
	.set_weight		= sched_ext_ops__set_weight,
	.set_cpumask		= sched_ext_ops__set_cpumask,
	.update_idle		= sched_ext_ops__update_idle,
	.cpu_acquire		= sched_ext_ops__cpu_acquire,
	.cpu_release		= sched_ext_ops__cpu_release,
	.init_task		= sched_ext_ops__init_task,
	.exit_task		= sched_ext_ops__exit_task,
	.enable			= sched_ext_ops__enable,
	.disable		= sched_ext_ops__disable,
#ifdef CONFIG_EXT_GROUP_SCHED
	.cgroup_init		= sched_ext_ops__cgroup_init,
	.cgroup_exit		= sched_ext_ops__cgroup_exit,
	.cgroup_prep_move	= sched_ext_ops__cgroup_prep_move,
	.cgroup_move		= sched_ext_ops__cgroup_move,
	.cgroup_cancel_move	= sched_ext_ops__cgroup_cancel_move,
	.cgroup_set_weight	= sched_ext_ops__cgroup_set_weight,
	.cgroup_set_bandwidth	= sched_ext_ops__cgroup_set_bandwidth,
#endif
	.cpu_online		= sched_ext_ops__cpu_online,
	.cpu_offline		= sched_ext_ops__cpu_offline,
	.init			= sched_ext_ops__init,
	.exit			= sched_ext_ops__exit,
	.dump			= sched_ext_ops__dump,
	.dump_cpu		= sched_ext_ops__dump_cpu,
	.dump_task		= sched_ext_ops__dump_task,
};

static struct bpf_struct_ops bpf_sched_ext_ops = {
	.verifier_ops = &bpf_scx_verifier_ops,
	.reg = bpf_scx_reg,
	.unreg = bpf_scx_unreg,
	.check_member = bpf_scx_check_member,
	.init_member = bpf_scx_init_member,
	.init = bpf_scx_init,
	.update = bpf_scx_update,
	.validate = bpf_scx_validate,
	.name = "sched_ext_ops",
	.owner = THIS_MODULE,
	.cfi_stubs = &__bpf_ops_sched_ext_ops
};


/********************************************************************************
 * System integration and init.
 */

static void sysrq_handle_sched_ext_reset(u8 key)
{
	scx_disable(SCX_EXIT_SYSRQ);
}

static const struct sysrq_key_op sysrq_sched_ext_reset_op = {
	.handler	= sysrq_handle_sched_ext_reset,
	.help_msg	= "reset-sched-ext(S)",
	.action_msg	= "Disable sched_ext and revert all tasks to CFS",
	.enable_mask	= SYSRQ_ENABLE_RTNICE,
};

static void sysrq_handle_sched_ext_dump(u8 key)
{
	struct scx_exit_info ei = { .kind = SCX_EXIT_NONE, .reason = "SysRq-D" };

	if (scx_enabled())
		scx_dump_state(&ei, 0);
}

static const struct sysrq_key_op sysrq_sched_ext_dump_op = {
	.handler	= sysrq_handle_sched_ext_dump,
	.help_msg	= "dump-sched-ext(D)",
	.action_msg	= "Trigger sched_ext debug dump",
	.enable_mask	= SYSRQ_ENABLE_RTNICE,
};

static bool can_skip_idle_kick(struct rq *rq)
{
	lockdep_assert_rq_held(rq);

	/*
	 * We can skip idle kicking if @rq is going to go through at least one
	 * full SCX scheduling cycle before going idle. Just checking whether
	 * curr is not idle is insufficient because we could be racing
	 * balance_one() trying to pull the next task from a remote rq, which
	 * may fail, and @rq may become idle afterwards.
	 *
	 * The race window is small and we don't and can't guarantee that @rq is
	 * only kicked while idle anyway. Skip only when sure.
	 */
	return !is_idle_task(rq->curr) && !(rq->scx.flags & SCX_RQ_IN_BALANCE);
}

static bool kick_one_cpu(s32 cpu, struct rq *this_rq, unsigned long *pseqs)
{
	struct rq *rq = cpu_rq(cpu);
	struct scx_rq *this_scx = &this_rq->scx;
	bool should_wait = false;
	unsigned long flags;

	raw_spin_rq_lock_irqsave(rq, flags);

	/*
	 * During CPU hotplug, a CPU may depend on kicking itself to make
	 * forward progress. Allow kicking self regardless of online state.
	 */
	if (cpu_online(cpu) || cpu == cpu_of(this_rq)) {
		if (cpumask_test_cpu(cpu, this_scx->cpus_to_preempt)) {
			if (rq->curr->sched_class == &ext_sched_class)
				rq->curr->scx.slice = 0;
			cpumask_clear_cpu(cpu, this_scx->cpus_to_preempt);
		}

		if (cpumask_test_cpu(cpu, this_scx->cpus_to_wait)) {
			pseqs[cpu] = rq->scx.pnt_seq;
			should_wait = true;
		}

		resched_curr(rq);
	} else {
		cpumask_clear_cpu(cpu, this_scx->cpus_to_preempt);
		cpumask_clear_cpu(cpu, this_scx->cpus_to_wait);
	}

	raw_spin_rq_unlock_irqrestore(rq, flags);

	return should_wait;
}

static void kick_one_cpu_if_idle(s32 cpu, struct rq *this_rq)
{
	struct rq *rq = cpu_rq(cpu);
	unsigned long flags;

	raw_spin_rq_lock_irqsave(rq, flags);

	if (!can_skip_idle_kick(rq) &&
	    (cpu_online(cpu) || cpu == cpu_of(this_rq)))
		resched_curr(rq);

	raw_spin_rq_unlock_irqrestore(rq, flags);
}

static void kick_cpus_irq_workfn(struct irq_work *irq_work)
{
	struct rq *this_rq = this_rq();
	struct scx_rq *this_scx = &this_rq->scx;
	struct scx_kick_pseqs __rcu *pseqs_pcpu = __this_cpu_read(scx_kick_pseqs);
	bool should_wait = false;
	unsigned long *pseqs;
	s32 cpu;

	if (unlikely(!pseqs_pcpu)) {
		pr_warn_once("kick_cpus_irq_workfn() called with NULL scx_kick_pseqs");
		return;
	}

	pseqs = rcu_dereference_bh(pseqs_pcpu)->seqs;

	for_each_cpu(cpu, this_scx->cpus_to_kick) {
		should_wait |= kick_one_cpu(cpu, this_rq, pseqs);
		cpumask_clear_cpu(cpu, this_scx->cpus_to_kick);
		cpumask_clear_cpu(cpu, this_scx->cpus_to_kick_if_idle);
	}

	for_each_cpu(cpu, this_scx->cpus_to_kick_if_idle) {
		kick_one_cpu_if_idle(cpu, this_rq);
		cpumask_clear_cpu(cpu, this_scx->cpus_to_kick_if_idle);
	}

	if (!should_wait)
		return;

	for_each_cpu(cpu, this_scx->cpus_to_wait) {
		unsigned long *wait_pnt_seq = &cpu_rq(cpu)->scx.pnt_seq;

		if (cpu != cpu_of(this_rq)) {
			/*
			 * Pairs with smp_store_release() issued by this CPU in
			 * switch_class() on the resched path.
			 *
			 * We busy-wait here to guarantee that no other task can
			 * be scheduled on our core before the target CPU has
			 * entered the resched path.
			 */
			while (smp_load_acquire(wait_pnt_seq) == pseqs[cpu])
				cpu_relax();
		}

		cpumask_clear_cpu(cpu, this_scx->cpus_to_wait);
	}
}

/**
 * print_scx_info - print out sched_ext scheduler state
 * @log_lvl: the log level to use when printing
 * @p: target task
 *
 * If a sched_ext scheduler is enabled, print the name and state of the
 * scheduler. If @p is on sched_ext, print further information about the task.
 *
 * This function can be safely called on any task as long as the task_struct
 * itself is accessible. While safe, this function isn't synchronized and may
 * print out mixups or garbages of limited length.
 */
void print_scx_info(const char *log_lvl, struct task_struct *p)
{
	struct scx_sched *sch = scx_root;
	enum scx_enable_state state = scx_enable_state();
	const char *all = READ_ONCE(scx_switching_all) ? "+all" : "";
	char runnable_at_buf[22] = "?";
	struct sched_class *class;
	unsigned long runnable_at;

	if (state == SCX_DISABLED)
		return;

	/*
	 * Carefully check if the task was running on sched_ext, and then
	 * carefully copy the time it's been runnable, and its state.
	 */
	if (copy_from_kernel_nofault(&class, &p->sched_class, sizeof(class)) ||
	    class != &ext_sched_class) {
		printk("%sSched_ext: %s (%s%s)", log_lvl, sch->ops.name,
		       scx_enable_state_str[state], all);
		return;
	}

	if (!copy_from_kernel_nofault(&runnable_at, &p->scx.runnable_at,
				      sizeof(runnable_at)))
		scnprintf(runnable_at_buf, sizeof(runnable_at_buf), "%+ldms",
			  jiffies_delta_msecs(runnable_at, jiffies));

	/* print everything onto one line to conserve console space */
	printk("%sSched_ext: %s (%s%s), task: runnable_at=%s",
	       log_lvl, sch->ops.name, scx_enable_state_str[state], all,
	       runnable_at_buf);
}

static int scx_pm_handler(struct notifier_block *nb, unsigned long event, void *ptr)
{
	/*
	 * SCX schedulers often have userspace components which are sometimes
	 * involved in critial scheduling paths. PM operations involve freezing
	 * userspace which can lead to scheduling misbehaviors including stalls.
	 * Let's bypass while PM operations are in progress.
	 */
	switch (event) {
	case PM_HIBERNATION_PREPARE:
	case PM_SUSPEND_PREPARE:
	case PM_RESTORE_PREPARE:
		scx_bypass(true);
		break;
	case PM_POST_HIBERNATION:
	case PM_POST_SUSPEND:
	case PM_POST_RESTORE:
		scx_bypass(false);
		break;
	}

	return NOTIFY_OK;
}

static struct notifier_block scx_pm_notifier = {
	.notifier_call = scx_pm_handler,
};

void __init init_sched_ext_class(void)
{
	s32 cpu, v;

	/*
	 * The following is to prevent the compiler from optimizing out the enum
	 * definitions so that BPF scheduler implementations can use them
	 * through the generated vmlinux.h.
	 */
	WRITE_ONCE(v, SCX_ENQ_WAKEUP | SCX_DEQ_SLEEP | SCX_KICK_PREEMPT |
		   SCX_TG_ONLINE);

	scx_idle_init_masks();

	for_each_possible_cpu(cpu) {
		struct rq *rq = cpu_rq(cpu);
		int  n = cpu_to_node(cpu);

		init_dsq(&rq->scx.local_dsq, SCX_DSQ_LOCAL);
		INIT_LIST_HEAD(&rq->scx.runnable_list);
		INIT_LIST_HEAD(&rq->scx.ddsp_deferred_locals);

		BUG_ON(!zalloc_cpumask_var_node(&rq->scx.cpus_to_kick, GFP_KERNEL, n));
		BUG_ON(!zalloc_cpumask_var_node(&rq->scx.cpus_to_kick_if_idle, GFP_KERNEL, n));
		BUG_ON(!zalloc_cpumask_var_node(&rq->scx.cpus_to_preempt, GFP_KERNEL, n));
		BUG_ON(!zalloc_cpumask_var_node(&rq->scx.cpus_to_wait, GFP_KERNEL, n));
		rq->scx.deferred_irq_work = IRQ_WORK_INIT_HARD(deferred_irq_workfn);
		rq->scx.kick_cpus_irq_work = IRQ_WORK_INIT_HARD(kick_cpus_irq_workfn);

		if (cpu_online(cpu))
			cpu_rq(cpu)->scx.flags |= SCX_RQ_ONLINE;
	}

	register_sysrq_key('S', &sysrq_sched_ext_reset_op);
	register_sysrq_key('D', &sysrq_sched_ext_dump_op);
	INIT_DELAYED_WORK(&scx_watchdog_work, scx_watchdog_workfn);
}


/********************************************************************************
 * Helpers that can be called from the BPF scheduler.
 */
static bool scx_dsq_insert_preamble(struct scx_sched *sch, struct task_struct *p,
				    u64 enq_flags)
{
	if (!scx_kf_allowed(sch, SCX_KF_ENQUEUE | SCX_KF_DISPATCH))
		return false;

	lockdep_assert_irqs_disabled();

	if (unlikely(!p)) {
		scx_error(sch, "called with NULL task");
		return false;
	}

	if (unlikely(enq_flags & __SCX_ENQ_INTERNAL_MASK)) {
		scx_error(sch, "invalid enq_flags 0x%llx", enq_flags);
		return false;
	}

	return true;
}

static void scx_dsq_insert_commit(struct scx_sched *sch, struct task_struct *p,
				  u64 dsq_id, u64 enq_flags)
{
	struct scx_dsp_ctx *dspc = this_cpu_ptr(scx_dsp_ctx);
	struct task_struct *ddsp_task;

	ddsp_task = __this_cpu_read(direct_dispatch_task);
	if (ddsp_task) {
		mark_direct_dispatch(sch, ddsp_task, p, dsq_id, enq_flags);
		return;
	}

	if (unlikely(dspc->cursor >= scx_dsp_max_batch)) {
		scx_error(sch, "dispatch buffer overflow");
		return;
	}

	dspc->buf[dspc->cursor++] = (struct scx_dsp_buf_ent){
		.task = p,
		.qseq = atomic_long_read(&p->scx.ops_state) & SCX_OPSS_QSEQ_MASK,
		.dsq_id = dsq_id,
		.enq_flags = enq_flags,
	};
}

__bpf_kfunc_start_defs();

/**
 * scx_bpf_dsq_insert - Insert a task into the FIFO queue of a DSQ
 * @p: task_struct to insert
 * @dsq_id: DSQ to insert into
 * @slice: duration @p can run for in nsecs, 0 to keep the current value
 * @enq_flags: SCX_ENQ_*
 *
 * Insert @p into the FIFO queue of the DSQ identified by @dsq_id. It is safe to
 * call this function spuriously. Can be called from ops.enqueue(),
 * ops.select_cpu(), and ops.dispatch().
 *
 * When called from ops.select_cpu() or ops.enqueue(), it's for direct dispatch
 * and @p must match the task being enqueued.
 *
 * When called from ops.select_cpu(), @enq_flags and @dsp_id are stored, and @p
 * will be directly inserted into the corresponding dispatch queue after
 * ops.select_cpu() returns. If @p is inserted into SCX_DSQ_LOCAL, it will be
 * inserted into the local DSQ of the CPU returned by ops.select_cpu().
 * @enq_flags are OR'd with the enqueue flags on the enqueue path before the
 * task is inserted.
 *
 * When called from ops.dispatch(), there are no restrictions on @p or @dsq_id
 * and this function can be called upto ops.dispatch_max_batch times to insert
 * multiple tasks. scx_bpf_dispatch_nr_slots() returns the number of the
 * remaining slots. scx_bpf_dsq_move_to_local() flushes the batch and resets the
 * counter.
 *
 * This function doesn't have any locking restrictions and may be called under
 * BPF locks (in the future when BPF introduces more flexible locking).
 *
 * @p is allowed to run for @slice. The scheduling path is triggered on slice
 * exhaustion. If zero, the current residual slice is maintained. If
 * %SCX_SLICE_INF, @p never expires and the BPF scheduler must kick the CPU with
 * scx_bpf_kick_cpu() to trigger scheduling.
 */
__bpf_kfunc void scx_bpf_dsq_insert(struct task_struct *p, u64 dsq_id, u64 slice,
				    u64 enq_flags)
{
	struct scx_sched *sch;

	guard(rcu)();
	sch = rcu_dereference(scx_root);
	if (unlikely(!sch))
		return;

	if (!scx_dsq_insert_preamble(sch, p, enq_flags))
		return;

	if (slice)
		p->scx.slice = slice;
	else
		p->scx.slice = p->scx.slice ?: 1;

	scx_dsq_insert_commit(sch, p, dsq_id, enq_flags);
}

/**
 * scx_bpf_dsq_insert_vtime - Insert a task into the vtime priority queue of a DSQ
 * @p: task_struct to insert
 * @dsq_id: DSQ to insert into
 * @slice: duration @p can run for in nsecs, 0 to keep the current value
 * @vtime: @p's ordering inside the vtime-sorted queue of the target DSQ
 * @enq_flags: SCX_ENQ_*
 *
 * Insert @p into the vtime priority queue of the DSQ identified by @dsq_id.
 * Tasks queued into the priority queue are ordered by @vtime. All other aspects
 * are identical to scx_bpf_dsq_insert().
 *
 * @vtime ordering is according to time_before64() which considers wrapping. A
 * numerically larger vtime may indicate an earlier position in the ordering and
 * vice-versa.
 *
 * A DSQ can only be used as a FIFO or priority queue at any given time and this
 * function must not be called on a DSQ which already has one or more FIFO tasks
 * queued and vice-versa. Also, the built-in DSQs (SCX_DSQ_LOCAL and
 * SCX_DSQ_GLOBAL) cannot be used as priority queues.
 */
__bpf_kfunc void scx_bpf_dsq_insert_vtime(struct task_struct *p, u64 dsq_id,
					  u64 slice, u64 vtime, u64 enq_flags)
{
	struct scx_sched *sch;

	guard(rcu)();
	sch = rcu_dereference(scx_root);
	if (unlikely(!sch))
		return;

	if (!scx_dsq_insert_preamble(sch, p, enq_flags))
		return;

	if (slice)
		p->scx.slice = slice;
	else
		p->scx.slice = p->scx.slice ?: 1;

	p->scx.dsq_vtime = vtime;

	scx_dsq_insert_commit(sch, p, dsq_id, enq_flags | SCX_ENQ_DSQ_PRIQ);
}

__bpf_kfunc_end_defs();

BTF_KFUNCS_START(scx_kfunc_ids_enqueue_dispatch)
BTF_ID_FLAGS(func, scx_bpf_dsq_insert, KF_RCU)
BTF_ID_FLAGS(func, scx_bpf_dsq_insert_vtime, KF_RCU)
BTF_KFUNCS_END(scx_kfunc_ids_enqueue_dispatch)

static const struct btf_kfunc_id_set scx_kfunc_set_enqueue_dispatch = {
	.owner			= THIS_MODULE,
	.set			= &scx_kfunc_ids_enqueue_dispatch,
};

static bool scx_dsq_move(struct bpf_iter_scx_dsq_kern *kit,
			 struct task_struct *p, u64 dsq_id, u64 enq_flags)
{
	struct scx_sched *sch = scx_root;
	struct scx_dispatch_q *src_dsq = kit->dsq, *dst_dsq;
	struct rq *this_rq, *src_rq, *locked_rq;
	bool dispatched = false;
	bool in_balance;
	unsigned long flags;

	if (!scx_kf_allowed_if_unlocked() &&
	    !scx_kf_allowed(sch, SCX_KF_DISPATCH))
		return false;

	/*
	 * Can be called from either ops.dispatch() locking this_rq() or any
	 * context where no rq lock is held. If latter, lock @p's task_rq which
	 * we'll likely need anyway.
	 */
	src_rq = task_rq(p);

	local_irq_save(flags);
	this_rq = this_rq();
	in_balance = this_rq->scx.flags & SCX_RQ_IN_BALANCE;

	if (in_balance) {
		if (this_rq != src_rq) {
			raw_spin_rq_unlock(this_rq);
			raw_spin_rq_lock(src_rq);
		}
	} else {
		raw_spin_rq_lock(src_rq);
	}

	/*
	 * If the BPF scheduler keeps calling this function repeatedly, it can
	 * cause similar live-lock conditions as consume_dispatch_q(). Insert a
	 * breather if necessary.
	 */
	scx_breather(src_rq);

	locked_rq = src_rq;
	raw_spin_lock(&src_dsq->lock);

	/*
	 * Did someone else get to it? @p could have already left $src_dsq, got
	 * re-enqueud, or be in the process of being consumed by someone else.
	 */
	if (unlikely(p->scx.dsq != src_dsq ||
		     u32_before(kit->cursor.priv, p->scx.dsq_seq) ||
		     p->scx.holding_cpu >= 0) ||
	    WARN_ON_ONCE(src_rq != task_rq(p))) {
		raw_spin_unlock(&src_dsq->lock);
		goto out;
	}

	/* @p is still on $src_dsq and stable, determine the destination */
	dst_dsq = find_dsq_for_dispatch(sch, this_rq, dsq_id, p);

	/*
	 * Apply vtime and slice updates before moving so that the new time is
	 * visible before inserting into $dst_dsq. @p is still on $src_dsq but
	 * this is safe as we're locking it.
	 */
	if (kit->cursor.flags & __SCX_DSQ_ITER_HAS_VTIME)
		p->scx.dsq_vtime = kit->vtime;
	if (kit->cursor.flags & __SCX_DSQ_ITER_HAS_SLICE)
		p->scx.slice = kit->slice;

	/* execute move */
	locked_rq = move_task_between_dsqs(sch, p, enq_flags, src_dsq, dst_dsq);
	dispatched = true;
out:
	if (in_balance) {
		if (this_rq != locked_rq) {
			raw_spin_rq_unlock(locked_rq);
			raw_spin_rq_lock(this_rq);
		}
	} else {
		raw_spin_rq_unlock_irqrestore(locked_rq, flags);
	}

	kit->cursor.flags &= ~(__SCX_DSQ_ITER_HAS_SLICE |
			       __SCX_DSQ_ITER_HAS_VTIME);
	return dispatched;
}

__bpf_kfunc_start_defs();

/**
 * scx_bpf_dispatch_nr_slots - Return the number of remaining dispatch slots
 *
 * Can only be called from ops.dispatch().
 */
__bpf_kfunc u32 scx_bpf_dispatch_nr_slots(void)
{
	struct scx_sched *sch;

	guard(rcu)();

	sch = rcu_dereference(scx_root);
	if (unlikely(!sch))
		return 0;

	if (!scx_kf_allowed(sch, SCX_KF_DISPATCH))
		return 0;

	return scx_dsp_max_batch - __this_cpu_read(scx_dsp_ctx->cursor);
}

/**
 * scx_bpf_dispatch_cancel - Cancel the latest dispatch
 *
 * Cancel the latest dispatch. Can be called multiple times to cancel further
 * dispatches. Can only be called from ops.dispatch().
 */
__bpf_kfunc void scx_bpf_dispatch_cancel(void)
{
	struct scx_dsp_ctx *dspc = this_cpu_ptr(scx_dsp_ctx);
	struct scx_sched *sch;

	guard(rcu)();

	sch = rcu_dereference(scx_root);
	if (unlikely(!sch))
		return;

	if (!scx_kf_allowed(sch, SCX_KF_DISPATCH))
		return;

	if (dspc->cursor > 0)
		dspc->cursor--;
	else
		scx_error(sch, "dispatch buffer underflow");
}

/**
 * scx_bpf_dsq_move_to_local - move a task from a DSQ to the current CPU's local DSQ
 * @dsq_id: DSQ to move task from
 *
 * Move a task from the non-local DSQ identified by @dsq_id to the current CPU's
 * local DSQ for execution. Can only be called from ops.dispatch().
 *
 * This function flushes the in-flight dispatches from scx_bpf_dsq_insert()
 * before trying to move from the specified DSQ. It may also grab rq locks and
 * thus can't be called under any BPF locks.
 *
 * Returns %true if a task has been moved, %false if there isn't any task to
 * move.
 */
__bpf_kfunc bool scx_bpf_dsq_move_to_local(u64 dsq_id)
{
	struct scx_dsp_ctx *dspc = this_cpu_ptr(scx_dsp_ctx);
	struct scx_dispatch_q *dsq;
	struct scx_sched *sch;

	guard(rcu)();

	sch = rcu_dereference(scx_root);
	if (unlikely(!sch))
		return false;

	if (!scx_kf_allowed(sch, SCX_KF_DISPATCH))
		return false;

	flush_dispatch_buf(sch, dspc->rq);

	dsq = find_user_dsq(sch, dsq_id);
	if (unlikely(!dsq)) {
		scx_error(sch, "invalid DSQ ID 0x%016llx", dsq_id);
		return false;
	}

	if (consume_dispatch_q(sch, dspc->rq, dsq)) {
		/*
		 * A successfully consumed task can be dequeued before it starts
		 * running while the CPU is trying to migrate other dispatched
		 * tasks. Bump nr_tasks to tell balance_scx() to retry on empty
		 * local DSQ.
		 */
		dspc->nr_tasks++;
		return true;
	} else {
		return false;
	}
}

/**
 * scx_bpf_dsq_move_set_slice - Override slice when moving between DSQs
 * @it__iter: DSQ iterator in progress
 * @slice: duration the moved task can run for in nsecs
 *
 * Override the slice of the next task that will be moved from @it__iter using
 * scx_bpf_dsq_move[_vtime](). If this function is not called, the previous
 * slice duration is kept.
 */
__bpf_kfunc void scx_bpf_dsq_move_set_slice(struct bpf_iter_scx_dsq *it__iter,
					    u64 slice)
{
	struct bpf_iter_scx_dsq_kern *kit = (void *)it__iter;

	kit->slice = slice;
	kit->cursor.flags |= __SCX_DSQ_ITER_HAS_SLICE;
}

/**
 * scx_bpf_dsq_move_set_vtime - Override vtime when moving between DSQs
 * @it__iter: DSQ iterator in progress
 * @vtime: task's ordering inside the vtime-sorted queue of the target DSQ
 *
 * Override the vtime of the next task that will be moved from @it__iter using
 * scx_bpf_dsq_move_vtime(). If this function is not called, the previous slice
 * vtime is kept. If scx_bpf_dsq_move() is used to dispatch the next task, the
 * override is ignored and cleared.
 */
__bpf_kfunc void scx_bpf_dsq_move_set_vtime(struct bpf_iter_scx_dsq *it__iter,
					    u64 vtime)
{
	struct bpf_iter_scx_dsq_kern *kit = (void *)it__iter;

	kit->vtime = vtime;
	kit->cursor.flags |= __SCX_DSQ_ITER_HAS_VTIME;
}

/**
 * scx_bpf_dsq_move - Move a task from DSQ iteration to a DSQ
 * @it__iter: DSQ iterator in progress
 * @p: task to transfer
 * @dsq_id: DSQ to move @p to
 * @enq_flags: SCX_ENQ_*
 *
 * Transfer @p which is on the DSQ currently iterated by @it__iter to the DSQ
 * specified by @dsq_id. All DSQs - local DSQs, global DSQ and user DSQs - can
 * be the destination.
 *
 * For the transfer to be successful, @p must still be on the DSQ and have been
 * queued before the DSQ iteration started. This function doesn't care whether
 * @p was obtained from the DSQ iteration. @p just has to be on the DSQ and have
 * been queued before the iteration started.
 *
 * @p's slice is kept by default. Use scx_bpf_dsq_move_set_slice() to update.
 *
 * Can be called from ops.dispatch() or any BPF context which doesn't hold a rq
 * lock (e.g. BPF timers or SYSCALL programs).
 *
 * Returns %true if @p has been consumed, %false if @p had already been consumed
 * or dequeued.
 */
__bpf_kfunc bool scx_bpf_dsq_move(struct bpf_iter_scx_dsq *it__iter,
				  struct task_struct *p, u64 dsq_id,
				  u64 enq_flags)
{
	return scx_dsq_move((struct bpf_iter_scx_dsq_kern *)it__iter,
			    p, dsq_id, enq_flags);
}

/**
 * scx_bpf_dsq_move_vtime - Move a task from DSQ iteration to a PRIQ DSQ
 * @it__iter: DSQ iterator in progress
 * @p: task to transfer
 * @dsq_id: DSQ to move @p to
 * @enq_flags: SCX_ENQ_*
 *
 * Transfer @p which is on the DSQ currently iterated by @it__iter to the
 * priority queue of the DSQ specified by @dsq_id. The destination must be a
 * user DSQ as only user DSQs support priority queue.
 *
 * @p's slice and vtime are kept by default. Use scx_bpf_dsq_move_set_slice()
 * and scx_bpf_dsq_move_set_vtime() to update.
 *
 * All other aspects are identical to scx_bpf_dsq_move(). See
 * scx_bpf_dsq_insert_vtime() for more information on @vtime.
 */
__bpf_kfunc bool scx_bpf_dsq_move_vtime(struct bpf_iter_scx_dsq *it__iter,
					struct task_struct *p, u64 dsq_id,
					u64 enq_flags)
{
	return scx_dsq_move((struct bpf_iter_scx_dsq_kern *)it__iter,
			    p, dsq_id, enq_flags | SCX_ENQ_DSQ_PRIQ);
}

__bpf_kfunc_end_defs();

BTF_KFUNCS_START(scx_kfunc_ids_dispatch)
BTF_ID_FLAGS(func, scx_bpf_dispatch_nr_slots)
BTF_ID_FLAGS(func, scx_bpf_dispatch_cancel)
BTF_ID_FLAGS(func, scx_bpf_dsq_move_to_local)
BTF_ID_FLAGS(func, scx_bpf_dsq_move_set_slice, KF_RCU)
BTF_ID_FLAGS(func, scx_bpf_dsq_move_set_vtime, KF_RCU)
BTF_ID_FLAGS(func, scx_bpf_dsq_move, KF_RCU)
BTF_ID_FLAGS(func, scx_bpf_dsq_move_vtime, KF_RCU)
BTF_KFUNCS_END(scx_kfunc_ids_dispatch)

static const struct btf_kfunc_id_set scx_kfunc_set_dispatch = {
	.owner			= THIS_MODULE,
	.set			= &scx_kfunc_ids_dispatch,
};

__bpf_kfunc_start_defs();

/**
 * scx_bpf_reenqueue_local - Re-enqueue tasks on a local DSQ
 *
 * Iterate over all of the tasks currently enqueued on the local DSQ of the
 * caller's CPU, and re-enqueue them in the BPF scheduler. Returns the number of
 * processed tasks. Can only be called from ops.cpu_release().
 */
__bpf_kfunc u32 scx_bpf_reenqueue_local(void)
{
	struct scx_sched *sch;
	LIST_HEAD(tasks);
	u32 nr_enqueued = 0;
	struct rq *rq;
	struct task_struct *p, *n;

	guard(rcu)();
	sch = rcu_dereference(scx_root);
	if (unlikely(!sch))
		return 0;

	if (!scx_kf_allowed(sch, SCX_KF_CPU_RELEASE))
		return 0;

	rq = cpu_rq(smp_processor_id());
	lockdep_assert_rq_held(rq);

	/*
	 * The BPF scheduler may choose to dispatch tasks back to
	 * @rq->scx.local_dsq. Move all candidate tasks off to a private list
	 * first to avoid processing the same tasks repeatedly.
	 */
	list_for_each_entry_safe(p, n, &rq->scx.local_dsq.list,
				 scx.dsq_list.node) {
		/*
		 * If @p is being migrated, @p's current CPU may not agree with
		 * its allowed CPUs and the migration_cpu_stop is about to
		 * deactivate and re-activate @p anyway. Skip re-enqueueing.
		 *
		 * While racing sched property changes may also dequeue and
		 * re-enqueue a migrating task while its current CPU and allowed
		 * CPUs disagree, they use %ENQUEUE_RESTORE which is bypassed to
		 * the current local DSQ for running tasks and thus are not
		 * visible to the BPF scheduler.
		 */
		if (p->migration_pending)
			continue;

		dispatch_dequeue(rq, p);
		list_add_tail(&p->scx.dsq_list.node, &tasks);
	}

	list_for_each_entry_safe(p, n, &tasks, scx.dsq_list.node) {
		list_del_init(&p->scx.dsq_list.node);
		do_enqueue_task(rq, p, SCX_ENQ_REENQ, -1);
		nr_enqueued++;
	}

	return nr_enqueued;
}

__bpf_kfunc_end_defs();

BTF_KFUNCS_START(scx_kfunc_ids_cpu_release)
BTF_ID_FLAGS(func, scx_bpf_reenqueue_local)
BTF_KFUNCS_END(scx_kfunc_ids_cpu_release)

static const struct btf_kfunc_id_set scx_kfunc_set_cpu_release = {
	.owner			= THIS_MODULE,
	.set			= &scx_kfunc_ids_cpu_release,
};

__bpf_kfunc_start_defs();

/**
 * scx_bpf_create_dsq - Create a custom DSQ
 * @dsq_id: DSQ to create
 * @node: NUMA node to allocate from
 *
 * Create a custom DSQ identified by @dsq_id. Can be called from any sleepable
 * scx callback, and any BPF_PROG_TYPE_SYSCALL prog.
 */
__bpf_kfunc s32 scx_bpf_create_dsq(u64 dsq_id, s32 node)
{
	struct scx_dispatch_q *dsq;
	struct scx_sched *sch;
	s32 ret;

	if (unlikely(node >= (int)nr_node_ids ||
		     (node < 0 && node != NUMA_NO_NODE)))
		return -EINVAL;

	if (unlikely(dsq_id & SCX_DSQ_FLAG_BUILTIN))
		return -EINVAL;

	dsq = kmalloc_node(sizeof(*dsq), GFP_KERNEL, node);
	if (!dsq)
		return -ENOMEM;

	init_dsq(dsq, dsq_id);

	rcu_read_lock();

	sch = rcu_dereference(scx_root);
	if (sch)
		ret = rhashtable_lookup_insert_fast(&sch->dsq_hash, &dsq->hash_node,
						    dsq_hash_params);
	else
		ret = -ENODEV;

	rcu_read_unlock();
	if (ret)
		kfree(dsq);
	return ret;
}

__bpf_kfunc_end_defs();

BTF_KFUNCS_START(scx_kfunc_ids_unlocked)
BTF_ID_FLAGS(func, scx_bpf_create_dsq, KF_SLEEPABLE)
BTF_ID_FLAGS(func, scx_bpf_dsq_move_set_slice, KF_RCU)
BTF_ID_FLAGS(func, scx_bpf_dsq_move_set_vtime, KF_RCU)
BTF_ID_FLAGS(func, scx_bpf_dsq_move, KF_RCU)
BTF_ID_FLAGS(func, scx_bpf_dsq_move_vtime, KF_RCU)
BTF_KFUNCS_END(scx_kfunc_ids_unlocked)

static const struct btf_kfunc_id_set scx_kfunc_set_unlocked = {
	.owner			= THIS_MODULE,
	.set			= &scx_kfunc_ids_unlocked,
};

__bpf_kfunc_start_defs();

static void scx_kick_cpu(struct scx_sched *sch, s32 cpu, u64 flags)
{
	struct rq *this_rq;
	unsigned long irq_flags;

	if (!ops_cpu_valid(sch, cpu, NULL))
		return;

	local_irq_save(irq_flags);

	this_rq = this_rq();

	/*
	 * While bypassing for PM ops, IRQ handling may not be online which can
	 * lead to irq_work_queue() malfunction such as infinite busy wait for
	 * IRQ status update. Suppress kicking.
	 */
	if (scx_rq_bypassing(this_rq))
		goto out;

	/*
	 * Actual kicking is bounced to kick_cpus_irq_workfn() to avoid nesting
	 * rq locks. We can probably be smarter and avoid bouncing if called
	 * from ops which don't hold a rq lock.
	 */
	if (flags & SCX_KICK_IDLE) {
		struct rq *target_rq = cpu_rq(cpu);

		if (unlikely(flags & (SCX_KICK_PREEMPT | SCX_KICK_WAIT)))
			scx_error(sch, "PREEMPT/WAIT cannot be used with SCX_KICK_IDLE");

		if (raw_spin_rq_trylock(target_rq)) {
			if (can_skip_idle_kick(target_rq)) {
				raw_spin_rq_unlock(target_rq);
				goto out;
			}
			raw_spin_rq_unlock(target_rq);
		}
		cpumask_set_cpu(cpu, this_rq->scx.cpus_to_kick_if_idle);
	} else {
		cpumask_set_cpu(cpu, this_rq->scx.cpus_to_kick);

		if (flags & SCX_KICK_PREEMPT)
			cpumask_set_cpu(cpu, this_rq->scx.cpus_to_preempt);
		if (flags & SCX_KICK_WAIT)
			cpumask_set_cpu(cpu, this_rq->scx.cpus_to_wait);
	}

	irq_work_queue(&this_rq->scx.kick_cpus_irq_work);
out:
	local_irq_restore(irq_flags);
}

/**
 * scx_bpf_kick_cpu - Trigger reschedule on a CPU
 * @cpu: cpu to kick
 * @flags: %SCX_KICK_* flags
 *
 * Kick @cpu into rescheduling. This can be used to wake up an idle CPU or
 * trigger rescheduling on a busy CPU. This can be called from any online
 * scx_ops operation and the actual kicking is performed asynchronously through
 * an irq work.
 */
__bpf_kfunc void scx_bpf_kick_cpu(s32 cpu, u64 flags)
{
	struct scx_sched *sch;

	guard(rcu)();
	sch = rcu_dereference(scx_root);
	if (likely(sch))
		scx_kick_cpu(sch, cpu, flags);
}

/**
 * scx_bpf_dsq_nr_queued - Return the number of queued tasks
 * @dsq_id: id of the DSQ
 *
 * Return the number of tasks in the DSQ matching @dsq_id. If not found,
 * -%ENOENT is returned.
 */
__bpf_kfunc s32 scx_bpf_dsq_nr_queued(u64 dsq_id)
{
	struct scx_sched *sch;
	struct scx_dispatch_q *dsq;
	s32 ret;

	preempt_disable();

	sch = rcu_dereference_sched(scx_root);
	if (unlikely(!sch)) {
		ret = -ENODEV;
		goto out;
	}

	if (dsq_id == SCX_DSQ_LOCAL) {
		ret = READ_ONCE(this_rq()->scx.local_dsq.nr);
		goto out;
	} else if ((dsq_id & SCX_DSQ_LOCAL_ON) == SCX_DSQ_LOCAL_ON) {
		s32 cpu = dsq_id & SCX_DSQ_LOCAL_CPU_MASK;

		if (ops_cpu_valid(sch, cpu, NULL)) {
			ret = READ_ONCE(cpu_rq(cpu)->scx.local_dsq.nr);
			goto out;
		}
	} else {
		dsq = find_user_dsq(sch, dsq_id);
		if (dsq) {
			ret = READ_ONCE(dsq->nr);
			goto out;
		}
	}
	ret = -ENOENT;
out:
	preempt_enable();
	return ret;
}

/**
 * scx_bpf_destroy_dsq - Destroy a custom DSQ
 * @dsq_id: DSQ to destroy
 *
 * Destroy the custom DSQ identified by @dsq_id. Only DSQs created with
 * scx_bpf_create_dsq() can be destroyed. The caller must ensure that the DSQ is
 * empty and no further tasks are dispatched to it. Ignored if called on a DSQ
 * which doesn't exist. Can be called from any online scx_ops operations.
 */
__bpf_kfunc void scx_bpf_destroy_dsq(u64 dsq_id)
{
	struct scx_sched *sch;

	rcu_read_lock();
	sch = rcu_dereference(scx_root);
	if (sch)
		destroy_dsq(sch, dsq_id);
	rcu_read_unlock();
}

/**
 * bpf_iter_scx_dsq_new - Create a DSQ iterator
 * @it: iterator to initialize
 * @dsq_id: DSQ to iterate
 * @flags: %SCX_DSQ_ITER_*
 *
 * Initialize BPF iterator @it which can be used with bpf_for_each() to walk
 * tasks in the DSQ specified by @dsq_id. Iteration using @it only includes
 * tasks which are already queued when this function is invoked.
 */
__bpf_kfunc int bpf_iter_scx_dsq_new(struct bpf_iter_scx_dsq *it, u64 dsq_id,
				     u64 flags)
{
	struct bpf_iter_scx_dsq_kern *kit = (void *)it;
	struct scx_sched *sch;

	BUILD_BUG_ON(sizeof(struct bpf_iter_scx_dsq_kern) >
		     sizeof(struct bpf_iter_scx_dsq));
	BUILD_BUG_ON(__alignof__(struct bpf_iter_scx_dsq_kern) !=
		     __alignof__(struct bpf_iter_scx_dsq));

	/*
	 * next() and destroy() will be called regardless of the return value.
	 * Always clear $kit->dsq.
	 */
	kit->dsq = NULL;

	sch = rcu_dereference_check(scx_root, rcu_read_lock_bh_held());
	if (unlikely(!sch))
		return -ENODEV;

	if (flags & ~__SCX_DSQ_ITER_USER_FLAGS)
		return -EINVAL;

	kit->dsq = find_user_dsq(sch, dsq_id);
	if (!kit->dsq)
		return -ENOENT;

	INIT_LIST_HEAD(&kit->cursor.node);
	kit->cursor.flags = SCX_DSQ_LNODE_ITER_CURSOR | flags;
	kit->cursor.priv = READ_ONCE(kit->dsq->seq);

	return 0;
}

/**
 * bpf_iter_scx_dsq_next - Progress a DSQ iterator
 * @it: iterator to progress
 *
 * Return the next task. See bpf_iter_scx_dsq_new().
 */
__bpf_kfunc struct task_struct *bpf_iter_scx_dsq_next(struct bpf_iter_scx_dsq *it)
{
	struct bpf_iter_scx_dsq_kern *kit = (void *)it;
	bool rev = kit->cursor.flags & SCX_DSQ_ITER_REV;
	struct task_struct *p;
	unsigned long flags;

	if (!kit->dsq)
		return NULL;

	raw_spin_lock_irqsave(&kit->dsq->lock, flags);

	if (list_empty(&kit->cursor.node))
		p = NULL;
	else
		p = container_of(&kit->cursor, struct task_struct, scx.dsq_list);

	/*
	 * Only tasks which were queued before the iteration started are
	 * visible. This bounds BPF iterations and guarantees that vtime never
	 * jumps in the other direction while iterating.
	 */
	do {
		p = nldsq_next_task(kit->dsq, p, rev);
	} while (p && unlikely(u32_before(kit->cursor.priv, p->scx.dsq_seq)));

	if (p) {
		if (rev)
			list_move_tail(&kit->cursor.node, &p->scx.dsq_list.node);
		else
			list_move(&kit->cursor.node, &p->scx.dsq_list.node);
	} else {
		list_del_init(&kit->cursor.node);
	}

	raw_spin_unlock_irqrestore(&kit->dsq->lock, flags);

	return p;
}

/**
 * bpf_iter_scx_dsq_destroy - Destroy a DSQ iterator
 * @it: iterator to destroy
 *
 * Undo scx_iter_scx_dsq_new().
 */
__bpf_kfunc void bpf_iter_scx_dsq_destroy(struct bpf_iter_scx_dsq *it)
{
	struct bpf_iter_scx_dsq_kern *kit = (void *)it;

	if (!kit->dsq)
		return;

	if (!list_empty(&kit->cursor.node)) {
		unsigned long flags;

		raw_spin_lock_irqsave(&kit->dsq->lock, flags);
		list_del_init(&kit->cursor.node);
		raw_spin_unlock_irqrestore(&kit->dsq->lock, flags);
	}
	kit->dsq = NULL;
}

__bpf_kfunc_end_defs();

static s32 __bstr_format(struct scx_sched *sch, u64 *data_buf, char *line_buf,
			 size_t line_size, char *fmt, unsigned long long *data,
			 u32 data__sz)
{
	struct bpf_bprintf_data bprintf_data = { .get_bin_args = true };
	s32 ret;

	if (data__sz % 8 || data__sz > MAX_BPRINTF_VARARGS * 8 ||
	    (data__sz && !data)) {
		scx_error(sch, "invalid data=%p and data__sz=%u", (void *)data, data__sz);
		return -EINVAL;
	}

	ret = copy_from_kernel_nofault(data_buf, data, data__sz);
	if (ret < 0) {
		scx_error(sch, "failed to read data fields (%d)", ret);
		return ret;
	}

	ret = bpf_bprintf_prepare(fmt, UINT_MAX, data_buf, data__sz / 8,
				  &bprintf_data);
	if (ret < 0) {
		scx_error(sch, "format preparation failed (%d)", ret);
		return ret;
	}

	ret = bstr_printf(line_buf, line_size, fmt,
			  bprintf_data.bin_args);
	bpf_bprintf_cleanup(&bprintf_data);
	if (ret < 0) {
		scx_error(sch, "(\"%s\", %p, %u) failed to format", fmt, data, data__sz);
		return ret;
	}

	return ret;
}

static s32 bstr_format(struct scx_sched *sch, struct scx_bstr_buf *buf,
		       char *fmt, unsigned long long *data, u32 data__sz)
{
	return __bstr_format(sch, buf->data, buf->line, sizeof(buf->line),
			     fmt, data, data__sz);
}

__bpf_kfunc_start_defs();

/**
 * scx_bpf_exit_bstr - Gracefully exit the BPF scheduler.
 * @exit_code: Exit value to pass to user space via struct scx_exit_info.
 * @fmt: error message format string
 * @data: format string parameters packaged using ___bpf_fill() macro
 * @data__sz: @data len, must end in '__sz' for the verifier
 *
 * Indicate that the BPF scheduler wants to exit gracefully, and initiate ops
 * disabling.
 */
__bpf_kfunc void scx_bpf_exit_bstr(s64 exit_code, char *fmt,
				   unsigned long long *data, u32 data__sz)
{
	struct scx_sched *sch;
	unsigned long flags;

	raw_spin_lock_irqsave(&scx_exit_bstr_buf_lock, flags);
	sch = rcu_dereference_bh(scx_root);
	if (likely(sch) &&
	    bstr_format(sch, &scx_exit_bstr_buf, fmt, data, data__sz) >= 0)
		scx_exit(sch, SCX_EXIT_UNREG_BPF, exit_code, "%s", scx_exit_bstr_buf.line);
	raw_spin_unlock_irqrestore(&scx_exit_bstr_buf_lock, flags);
}

/**
 * scx_bpf_error_bstr - Indicate fatal error
 * @fmt: error message format string
 * @data: format string parameters packaged using ___bpf_fill() macro
 * @data__sz: @data len, must end in '__sz' for the verifier
 *
 * Indicate that the BPF scheduler encountered a fatal error and initiate ops
 * disabling.
 */
__bpf_kfunc void scx_bpf_error_bstr(char *fmt, unsigned long long *data,
				    u32 data__sz)
{
	struct scx_sched *sch;
	unsigned long flags;

	raw_spin_lock_irqsave(&scx_exit_bstr_buf_lock, flags);
	sch = rcu_dereference_bh(scx_root);
	if (likely(sch) &&
	    bstr_format(sch, &scx_exit_bstr_buf, fmt, data, data__sz) >= 0)
		scx_exit(sch, SCX_EXIT_ERROR_BPF, 0, "%s", scx_exit_bstr_buf.line);
	raw_spin_unlock_irqrestore(&scx_exit_bstr_buf_lock, flags);
}

/**
 * scx_bpf_dump_bstr - Generate extra debug dump specific to the BPF scheduler
 * @fmt: format string
 * @data: format string parameters packaged using ___bpf_fill() macro
 * @data__sz: @data len, must end in '__sz' for the verifier
 *
 * To be called through scx_bpf_dump() helper from ops.dump(), dump_cpu() and
 * dump_task() to generate extra debug dump specific to the BPF scheduler.
 *
 * The extra dump may be multiple lines. A single line may be split over
 * multiple calls. The last line is automatically terminated.
 */
__bpf_kfunc void scx_bpf_dump_bstr(char *fmt, unsigned long long *data,
				   u32 data__sz)
{
	struct scx_sched *sch;
	struct scx_dump_data *dd = &scx_dump_data;
	struct scx_bstr_buf *buf = &dd->buf;
	s32 ret;

	guard(rcu)();

	sch = rcu_dereference(scx_root);
	if (unlikely(!sch))
		return;

	if (raw_smp_processor_id() != dd->cpu) {
		scx_error(sch, "scx_bpf_dump() must only be called from ops.dump() and friends");
		return;
	}

	/* append the formatted string to the line buf */
	ret = __bstr_format(sch, buf->data, buf->line + dd->cursor,
			    sizeof(buf->line) - dd->cursor, fmt, data, data__sz);
	if (ret < 0) {
		dump_line(dd->s, "%s[!] (\"%s\", %p, %u) failed to format (%d)",
			  dd->prefix, fmt, data, data__sz, ret);
		return;
	}

	dd->cursor += ret;
	dd->cursor = min_t(s32, dd->cursor, sizeof(buf->line));

	if (!dd->cursor)
		return;

	/*
	 * If the line buf overflowed or ends in a newline, flush it into the
	 * dump. This is to allow the caller to generate a single line over
	 * multiple calls. As ops_dump_flush() can also handle multiple lines in
	 * the line buf, the only case which can lead to an unexpected
	 * truncation is when the caller keeps generating newlines in the middle
	 * instead of the end consecutively. Don't do that.
	 */
	if (dd->cursor >= sizeof(buf->line) || buf->line[dd->cursor - 1] == '\n')
		ops_dump_flush();
}

/**
 * scx_bpf_cpuperf_cap - Query the maximum relative capacity of a CPU
 * @cpu: CPU of interest
 *
 * Return the maximum relative capacity of @cpu in relation to the most
 * performant CPU in the system. The return value is in the range [1,
 * %SCX_CPUPERF_ONE]. See scx_bpf_cpuperf_cur().
 */
__bpf_kfunc u32 scx_bpf_cpuperf_cap(s32 cpu)
{
	struct scx_sched *sch;

	guard(rcu)();

	sch = rcu_dereference(scx_root);
	if (likely(sch) && ops_cpu_valid(sch, cpu, NULL))
		return arch_scale_cpu_capacity(cpu);
	else
		return SCX_CPUPERF_ONE;
}

/**
 * scx_bpf_cpuperf_cur - Query the current relative performance of a CPU
 * @cpu: CPU of interest
 *
 * Return the current relative performance of @cpu in relation to its maximum.
 * The return value is in the range [1, %SCX_CPUPERF_ONE].
 *
 * The current performance level of a CPU in relation to the maximum performance
 * available in the system can be calculated as follows:
 *
 *   scx_bpf_cpuperf_cap() * scx_bpf_cpuperf_cur() / %SCX_CPUPERF_ONE
 *
 * The result is in the range [1, %SCX_CPUPERF_ONE].
 */
__bpf_kfunc u32 scx_bpf_cpuperf_cur(s32 cpu)
{
	struct scx_sched *sch;

	guard(rcu)();

	sch = rcu_dereference(scx_root);
	if (likely(sch) && ops_cpu_valid(sch, cpu, NULL))
		return arch_scale_freq_capacity(cpu);
	else
		return SCX_CPUPERF_ONE;
}

/**
 * scx_bpf_cpuperf_set - Set the relative performance target of a CPU
 * @cpu: CPU of interest
 * @perf: target performance level [0, %SCX_CPUPERF_ONE]
 *
 * Set the target performance level of @cpu to @perf. @perf is in linear
 * relative scale between 0 and %SCX_CPUPERF_ONE. This determines how the
 * schedutil cpufreq governor chooses the target frequency.
 *
 * The actual performance level chosen, CPU grouping, and the overhead and
 * latency of the operations are dependent on the hardware and cpufreq driver in
 * use. Consult hardware and cpufreq documentation for more information. The
 * current performance level can be monitored using scx_bpf_cpuperf_cur().
 */
__bpf_kfunc void scx_bpf_cpuperf_set(s32 cpu, u32 perf)
{
	struct scx_sched *sch;

	guard(rcu)();

	sch = rcu_dereference(scx_root);
	if (unlikely(!sch))
		return;

	if (unlikely(perf > SCX_CPUPERF_ONE)) {
		scx_error(sch, "Invalid cpuperf target %u for CPU %d", perf, cpu);
		return;
	}

	if (ops_cpu_valid(sch, cpu, NULL)) {
		struct rq *rq = cpu_rq(cpu), *locked_rq = scx_locked_rq();
		struct rq_flags rf;

		/*
		 * When called with an rq lock held, restrict the operation
		 * to the corresponding CPU to prevent ABBA deadlocks.
		 */
		if (locked_rq && rq != locked_rq) {
			scx_error(sch, "Invalid target CPU %d", cpu);
			return;
		}

		/*
		 * If no rq lock is held, allow to operate on any CPU by
		 * acquiring the corresponding rq lock.
		 */
		if (!locked_rq) {
			rq_lock_irqsave(rq, &rf);
			update_rq_clock(rq);
		}

		rq->scx.cpuperf_target = perf;
		cpufreq_update_util(rq, 0);

		if (!locked_rq)
			rq_unlock_irqrestore(rq, &rf);
	}
}

/**
 * scx_bpf_nr_node_ids - Return the number of possible node IDs
 *
 * All valid node IDs in the system are smaller than the returned value.
 */
__bpf_kfunc u32 scx_bpf_nr_node_ids(void)
{
	return nr_node_ids;
}

/**
 * scx_bpf_nr_cpu_ids - Return the number of possible CPU IDs
 *
 * All valid CPU IDs in the system are smaller than the returned value.
 */
__bpf_kfunc u32 scx_bpf_nr_cpu_ids(void)
{
	return nr_cpu_ids;
}

/**
 * scx_bpf_get_possible_cpumask - Get a referenced kptr to cpu_possible_mask
 */
__bpf_kfunc const struct cpumask *scx_bpf_get_possible_cpumask(void)
{
	return cpu_possible_mask;
}

/**
 * scx_bpf_get_online_cpumask - Get a referenced kptr to cpu_online_mask
 */
__bpf_kfunc const struct cpumask *scx_bpf_get_online_cpumask(void)
{
	return cpu_online_mask;
}

/**
 * scx_bpf_put_cpumask - Release a possible/online cpumask
 * @cpumask: cpumask to release
 */
__bpf_kfunc void scx_bpf_put_cpumask(const struct cpumask *cpumask)
{
	/*
	 * Empty function body because we aren't actually acquiring or releasing
	 * a reference to a global cpumask, which is read-only in the caller and
	 * is never released. The acquire / release semantics here are just used
	 * to make the cpumask is a trusted pointer in the caller.
	 */
}

/**
 * scx_bpf_task_running - Is task currently running?
 * @p: task of interest
 */
__bpf_kfunc bool scx_bpf_task_running(const struct task_struct *p)
{
	return task_rq(p)->curr == p;
}

/**
 * scx_bpf_task_cpu - CPU a task is currently associated with
 * @p: task of interest
 */
__bpf_kfunc s32 scx_bpf_task_cpu(const struct task_struct *p)
{
	return task_cpu(p);
}

/**
 * scx_bpf_cpu_rq - Fetch the rq of a CPU
 * @cpu: CPU of the rq
 */
__bpf_kfunc struct rq *scx_bpf_cpu_rq(s32 cpu)
{
	struct scx_sched *sch;

	guard(rcu)();

	sch = rcu_dereference(scx_root);
	if (unlikely(!sch))
		return NULL;

	if (!ops_cpu_valid(sch, cpu, NULL))
		return NULL;

	if (!sch->warned_deprecated_rq) {
		printk_deferred(KERN_WARNING "sched_ext: %s() is deprecated; "
				"use scx_bpf_locked_rq() when holding rq lock "
				"or scx_bpf_cpu_curr() to read remote curr safely.\n", __func__);
		sch->warned_deprecated_rq = true;
	}

	return cpu_rq(cpu);
}

/**
 * scx_bpf_locked_rq - Return the rq currently locked by SCX
 *
 * Returns the rq if a rq lock is currently held by SCX.
 * Otherwise emits an error and returns NULL.
 */
__bpf_kfunc struct rq *scx_bpf_locked_rq(void)
{
	struct scx_sched *sch;
	struct rq *rq;

	guard(preempt)();

	sch = rcu_dereference_sched(scx_root);
	if (unlikely(!sch))
		return NULL;

	rq = scx_locked_rq();
	if (!rq) {
		scx_error(sch, "accessing rq without holding rq lock");
		return NULL;
	}

	return rq;
}

/**
 * scx_bpf_cpu_curr - Return remote CPU's curr task
 * @cpu: CPU of interest
 *
 * Callers must hold RCU read lock (KF_RCU).
 */
__bpf_kfunc struct task_struct *scx_bpf_cpu_curr(s32 cpu)
{
	struct scx_sched *sch;

	guard(rcu)();

	sch = rcu_dereference(scx_root);
	if (unlikely(!sch))
		return NULL;

	if (!ops_cpu_valid(sch, cpu, NULL))
		return NULL;

	return rcu_dereference(cpu_rq(cpu)->curr);
}

/**
 * scx_bpf_task_cgroup - Return the sched cgroup of a task
 * @p: task of interest
 *
 * @p->sched_task_group->css.cgroup represents the cgroup @p is associated with
 * from the scheduler's POV. SCX operations should use this function to
 * determine @p's current cgroup as, unlike following @p->cgroups,
 * @p->sched_task_group is protected by @p's rq lock and thus atomic w.r.t. all
 * rq-locked operations. Can be called on the parameter tasks of rq-locked
 * operations. The restriction guarantees that @p's rq is locked by the caller.
 */
#ifdef CONFIG_CGROUP_SCHED
__bpf_kfunc struct cgroup *scx_bpf_task_cgroup(struct task_struct *p)
{
	struct task_group *tg = p->sched_task_group;
	struct cgroup *cgrp = &cgrp_dfl_root.cgrp;
	struct scx_sched *sch;

	guard(rcu)();

	sch = rcu_dereference(scx_root);
	if (unlikely(!sch))
		goto out;

	if (!scx_kf_allowed_on_arg_tasks(sch, __SCX_KF_RQ_LOCKED, p))
		goto out;

	cgrp = tg_cgrp(tg);

out:
	cgroup_get(cgrp);
	return cgrp;
}
#endif

/**
 * scx_bpf_now - Returns a high-performance monotonically non-decreasing
 * clock for the current CPU. The clock returned is in nanoseconds.
 *
 * It provides the following properties:
 *
 * 1) High performance: Many BPF schedulers call bpf_ktime_get_ns() frequently
 *  to account for execution time and track tasks' runtime properties.
 *  Unfortunately, in some hardware platforms, bpf_ktime_get_ns() -- which
 *  eventually reads a hardware timestamp counter -- is neither performant nor
 *  scalable. scx_bpf_now() aims to provide a high-performance clock by
 *  using the rq clock in the scheduler core whenever possible.
 *
 * 2) High enough resolution for the BPF scheduler use cases: In most BPF
 *  scheduler use cases, the required clock resolution is lower than the most
 *  accurate hardware clock (e.g., rdtsc in x86). scx_bpf_now() basically
 *  uses the rq clock in the scheduler core whenever it is valid. It considers
 *  that the rq clock is valid from the time the rq clock is updated
 *  (update_rq_clock) until the rq is unlocked (rq_unpin_lock).
 *
 * 3) Monotonically non-decreasing clock for the same CPU: scx_bpf_now()
 *  guarantees the clock never goes backward when comparing them in the same
 *  CPU. On the other hand, when comparing clocks in different CPUs, there
 *  is no such guarantee -- the clock can go backward. It provides a
 *  monotonically *non-decreasing* clock so that it would provide the same
 *  clock values in two different scx_bpf_now() calls in the same CPU
 *  during the same period of when the rq clock is valid.
 */
__bpf_kfunc u64 scx_bpf_now(void)
{
	struct rq *rq;
	u64 clock;

	preempt_disable();

	rq = this_rq();
	if (smp_load_acquire(&rq->scx.flags) & SCX_RQ_CLK_VALID) {
		/*
		 * If the rq clock is valid, use the cached rq clock.
		 *
		 * Note that scx_bpf_now() is re-entrant between a process
		 * context and an interrupt context (e.g., timer interrupt).
		 * However, we don't need to consider the race between them
		 * because such race is not observable from a caller.
		 */
		clock = READ_ONCE(rq->scx.clock);
	} else {
		/*
		 * Otherwise, return a fresh rq clock.
		 *
		 * The rq clock is updated outside of the rq lock.
		 * In this case, keep the updated rq clock invalid so the next
		 * kfunc call outside the rq lock gets a fresh rq clock.
		 */
		clock = sched_clock_cpu(cpu_of(rq));
	}

	preempt_enable();

	return clock;
}

static void scx_read_events(struct scx_sched *sch, struct scx_event_stats *events)
{
	struct scx_event_stats *e_cpu;
	int cpu;

	/* Aggregate per-CPU event counters into @events. */
	memset(events, 0, sizeof(*events));
	for_each_possible_cpu(cpu) {
		e_cpu = &per_cpu_ptr(sch->pcpu, cpu)->event_stats;
		scx_agg_event(events, e_cpu, SCX_EV_SELECT_CPU_FALLBACK);
		scx_agg_event(events, e_cpu, SCX_EV_DISPATCH_LOCAL_DSQ_OFFLINE);
		scx_agg_event(events, e_cpu, SCX_EV_DISPATCH_KEEP_LAST);
		scx_agg_event(events, e_cpu, SCX_EV_ENQ_SKIP_EXITING);
		scx_agg_event(events, e_cpu, SCX_EV_ENQ_SKIP_MIGRATION_DISABLED);
		scx_agg_event(events, e_cpu, SCX_EV_REFILL_SLICE_DFL);
		scx_agg_event(events, e_cpu, SCX_EV_BYPASS_DURATION);
		scx_agg_event(events, e_cpu, SCX_EV_BYPASS_DISPATCH);
		scx_agg_event(events, e_cpu, SCX_EV_BYPASS_ACTIVATE);
	}
}

/*
 * scx_bpf_events - Get a system-wide event counter to
 * @events: output buffer from a BPF program
 * @events__sz: @events len, must end in '__sz'' for the verifier
 */
__bpf_kfunc void scx_bpf_events(struct scx_event_stats *events,
				size_t events__sz)
{
	struct scx_sched *sch;
	struct scx_event_stats e_sys;

	rcu_read_lock();
	sch = rcu_dereference(scx_root);
	if (sch)
		scx_read_events(sch, &e_sys);
	else
		memset(&e_sys, 0, sizeof(e_sys));
	rcu_read_unlock();

	/*
	 * We cannot entirely trust a BPF-provided size since a BPF program
	 * might be compiled against a different vmlinux.h, of which
	 * scx_event_stats would be larger (a newer vmlinux.h) or smaller
	 * (an older vmlinux.h). Hence, we use the smaller size to avoid
	 * memory corruption.
	 */
	events__sz = min(events__sz, sizeof(*events));
	memcpy(events, &e_sys, events__sz);
}

__bpf_kfunc_end_defs();

BTF_KFUNCS_START(scx_kfunc_ids_any)
BTF_ID_FLAGS(func, scx_bpf_kick_cpu)
BTF_ID_FLAGS(func, scx_bpf_dsq_nr_queued)
BTF_ID_FLAGS(func, scx_bpf_destroy_dsq)
BTF_ID_FLAGS(func, bpf_iter_scx_dsq_new, KF_ITER_NEW | KF_RCU_PROTECTED)
BTF_ID_FLAGS(func, bpf_iter_scx_dsq_next, KF_ITER_NEXT | KF_RET_NULL)
BTF_ID_FLAGS(func, bpf_iter_scx_dsq_destroy, KF_ITER_DESTROY)
BTF_ID_FLAGS(func, scx_bpf_exit_bstr, KF_TRUSTED_ARGS)
BTF_ID_FLAGS(func, scx_bpf_error_bstr, KF_TRUSTED_ARGS)
BTF_ID_FLAGS(func, scx_bpf_dump_bstr, KF_TRUSTED_ARGS)
BTF_ID_FLAGS(func, scx_bpf_cpuperf_cap)
BTF_ID_FLAGS(func, scx_bpf_cpuperf_cur)
BTF_ID_FLAGS(func, scx_bpf_cpuperf_set)
BTF_ID_FLAGS(func, scx_bpf_nr_node_ids)
BTF_ID_FLAGS(func, scx_bpf_nr_cpu_ids)
BTF_ID_FLAGS(func, scx_bpf_get_possible_cpumask, KF_ACQUIRE)
BTF_ID_FLAGS(func, scx_bpf_get_online_cpumask, KF_ACQUIRE)
BTF_ID_FLAGS(func, scx_bpf_put_cpumask, KF_RELEASE)
BTF_ID_FLAGS(func, scx_bpf_task_running, KF_RCU)
BTF_ID_FLAGS(func, scx_bpf_task_cpu, KF_RCU)
BTF_ID_FLAGS(func, scx_bpf_cpu_rq)
BTF_ID_FLAGS(func, scx_bpf_locked_rq, KF_RET_NULL)
BTF_ID_FLAGS(func, scx_bpf_cpu_curr, KF_RET_NULL | KF_RCU_PROTECTED)
#ifdef CONFIG_CGROUP_SCHED
BTF_ID_FLAGS(func, scx_bpf_task_cgroup, KF_RCU | KF_ACQUIRE)
#endif
BTF_ID_FLAGS(func, scx_bpf_now)
BTF_ID_FLAGS(func, scx_bpf_events, KF_TRUSTED_ARGS)
BTF_KFUNCS_END(scx_kfunc_ids_any)

static const struct btf_kfunc_id_set scx_kfunc_set_any = {
	.owner			= THIS_MODULE,
	.set			= &scx_kfunc_ids_any,
};

static int __init scx_init(void)
{
	int ret;

	/*
	 * kfunc registration can't be done from init_sched_ext_class() as
	 * register_btf_kfunc_id_set() needs most of the system to be up.
	 *
	 * Some kfuncs are context-sensitive and can only be called from
	 * specific SCX ops. They are grouped into BTF sets accordingly.
	 * Unfortunately, BPF currently doesn't have a way of enforcing such
	 * restrictions. Eventually, the verifier should be able to enforce
	 * them. For now, register them the same and make each kfunc explicitly
	 * check using scx_kf_allowed().
	 */
	if ((ret = register_btf_kfunc_id_set(BPF_PROG_TYPE_STRUCT_OPS,
					     &scx_kfunc_set_enqueue_dispatch)) ||
	    (ret = register_btf_kfunc_id_set(BPF_PROG_TYPE_STRUCT_OPS,
					     &scx_kfunc_set_dispatch)) ||
	    (ret = register_btf_kfunc_id_set(BPF_PROG_TYPE_STRUCT_OPS,
					     &scx_kfunc_set_cpu_release)) ||
	    (ret = register_btf_kfunc_id_set(BPF_PROG_TYPE_STRUCT_OPS,
					     &scx_kfunc_set_unlocked)) ||
	    (ret = register_btf_kfunc_id_set(BPF_PROG_TYPE_SYSCALL,
					     &scx_kfunc_set_unlocked)) ||
	    (ret = register_btf_kfunc_id_set(BPF_PROG_TYPE_STRUCT_OPS,
					     &scx_kfunc_set_any)) ||
	    (ret = register_btf_kfunc_id_set(BPF_PROG_TYPE_TRACING,
					     &scx_kfunc_set_any)) ||
	    (ret = register_btf_kfunc_id_set(BPF_PROG_TYPE_SYSCALL,
					     &scx_kfunc_set_any))) {
		pr_err("sched_ext: Failed to register kfunc sets (%d)\n", ret);
		return ret;
	}

	ret = scx_idle_init();
	if (ret) {
		pr_err("sched_ext: Failed to initialize idle tracking (%d)\n", ret);
		return ret;
	}

	ret = register_bpf_struct_ops(&bpf_sched_ext_ops, sched_ext_ops);
	if (ret) {
		pr_err("sched_ext: Failed to register struct_ops (%d)\n", ret);
		return ret;
	}

	ret = register_pm_notifier(&scx_pm_notifier);
	if (ret) {
		pr_err("sched_ext: Failed to register PM notifier (%d)\n", ret);
		return ret;
	}

	scx_kset = kset_create_and_add("sched_ext", &scx_uevent_ops, kernel_kobj);
	if (!scx_kset) {
		pr_err("sched_ext: Failed to create /sys/kernel/sched_ext\n");
		return -ENOMEM;
	}

	ret = sysfs_create_group(&scx_kset->kobj, &scx_global_attr_group);
	if (ret < 0) {
		pr_err("sched_ext: Failed to add global attributes\n");
		return ret;
	}

	return 0;
}
__initcall(scx_init);
