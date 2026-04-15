/* SPDX-License-Identifier: GPL-2.0 */
/*
 * BPF extensible scheduler class: Documentation/scheduler/sched-ext.rst
 *
 * Copyright (c) 2022 Meta Platforms, Inc. and affiliates.
 * Copyright (c) 2022 Tejun Heo <tj@kernel.org>
 * Copyright (c) 2022 David Vernet <dvernet@meta.com>
 */
#ifndef _LINUX_SCHED_EXT_H
#define _LINUX_SCHED_EXT_H

#ifdef CONFIG_SCHED_CLASS_EXT

#include <linux/llist.h>
#include <linux/rhashtable-types.h>

enum scx_public_consts {
	SCX_OPS_NAME_LEN	= 128,

	/*
	 * %SCX_SLICE_DFL is used to refill slices when the BPF scheduler misses
	 * to set the slice for a task that is selected for execution.
	 * %SCX_EV_REFILL_SLICE_DFL counts the number of times the default slice
	 * refill has been triggered.
	 *
	 * %SCX_SLICE_BYPASS is used as the slice for all tasks in the bypass
	 * mode. As making forward progress for all tasks is the main goal of
	 * the bypass mode, a shorter slice is used.
	 */
	SCX_SLICE_DFL		= 20 * 1000000,	/* 20ms */
	SCX_SLICE_BYPASS	=  5 * 1000000, /*  5ms */
	SCX_SLICE_INF		= U64_MAX,	/* infinite, implies nohz */
};

/*
 * DSQ (dispatch queue) IDs are 64bit of the format:
 *
 *   Bits: [63] [62 ..  0]
 *         [ B] [   ID   ]
 *
 *    B: 1 for IDs for built-in DSQs, 0 for ops-created user DSQs
 *   ID: 63 bit ID
 *
 * Built-in IDs:
 *
 *   Bits: [63] [62] [61..32] [31 ..  0]
 *         [ 1] [ L] [   R  ] [    V   ]
 *
 *    1: 1 for built-in DSQs.
 *    L: 1 for LOCAL_ON DSQ IDs, 0 for others
 *    V: For LOCAL_ON DSQ IDs, a CPU number. For others, a pre-defined value.
 */
enum scx_dsq_id_flags {
	SCX_DSQ_FLAG_BUILTIN	= 1LLU << 63,
	SCX_DSQ_FLAG_LOCAL_ON	= 1LLU << 62,

	SCX_DSQ_INVALID		= SCX_DSQ_FLAG_BUILTIN | 0,
	SCX_DSQ_GLOBAL		= SCX_DSQ_FLAG_BUILTIN | 1,
	SCX_DSQ_LOCAL		= SCX_DSQ_FLAG_BUILTIN | 2,
	SCX_DSQ_BYPASS		= SCX_DSQ_FLAG_BUILTIN | 3,
	SCX_DSQ_LOCAL_ON	= SCX_DSQ_FLAG_BUILTIN | SCX_DSQ_FLAG_LOCAL_ON,
	SCX_DSQ_LOCAL_CPU_MASK	= 0xffffffffLLU,
};

struct scx_deferred_reenq_user {
	struct list_head	node;
	u64			flags;
};

struct scx_dsq_pcpu {
	struct scx_dispatch_q	*dsq;
	struct scx_deferred_reenq_user deferred_reenq_user;
};

/*
 * A dispatch queue (DSQ) can be either a FIFO or p->scx.dsq_vtime ordered
 * queue. A built-in DSQ is always a FIFO. The built-in local DSQs are used to
 * buffer between the scheduler core and the BPF scheduler. See the
 * documentation for more details.
 */
struct scx_dispatch_q {
	raw_spinlock_t		lock;
	struct task_struct __rcu *first_task; /* lockless peek at head */
	struct list_head	list;	/* tasks in dispatch order */
	struct rb_root		priq;	/* used to order by p->scx.dsq_vtime */
	u32			nr;
	u32			seq;	/* used by BPF iter */
	u64			id;
	struct rhash_head	hash_node;
	struct llist_node	free_node;
	struct scx_sched	*sched;
	struct scx_dsq_pcpu __percpu *pcpu;
	struct rcu_head		rcu;
};

/* sched_ext_entity.flags */
enum scx_ent_flags {
	SCX_TASK_QUEUED		= 1 << 0, /* on ext runqueue */
	SCX_TASK_IN_CUSTODY	= 1 << 1, /* in custody, needs ops.dequeue() when leaving */
	SCX_TASK_RESET_RUNNABLE_AT = 1 << 2, /* runnable_at should be reset */
	SCX_TASK_DEQD_FOR_SLEEP	= 1 << 3, /* last dequeue was for SLEEP */
	SCX_TASK_SUB_INIT	= 1 << 4, /* task being initialized for a sub sched */
	SCX_TASK_IMMED		= 1 << 5, /* task is on local DSQ with %SCX_ENQ_IMMED */

	/*
	 * Bits 8 and 9 are used to carry task state:
	 *
	 * NONE		ops.init_task() not called yet
	 * INIT		ops.init_task() succeeded, but task can be cancelled
	 * READY	fully initialized, but not in sched_ext
	 * ENABLED	fully initialized and in sched_ext
	 */
	SCX_TASK_STATE_SHIFT	= 8,	  /* bits 8 and 9 are used to carry task state */
	SCX_TASK_STATE_BITS	= 2,
	SCX_TASK_STATE_MASK	= ((1 << SCX_TASK_STATE_BITS) - 1) << SCX_TASK_STATE_SHIFT,

	SCX_TASK_NONE		= 0 << SCX_TASK_STATE_SHIFT,
	SCX_TASK_INIT		= 1 << SCX_TASK_STATE_SHIFT,
	SCX_TASK_READY		= 2 << SCX_TASK_STATE_SHIFT,
	SCX_TASK_ENABLED	= 3 << SCX_TASK_STATE_SHIFT,

	/*
	 * Bits 12 and 13 are used to carry reenqueue reason. In addition to
	 * %SCX_ENQ_REENQ flag, ops.enqueue() can also test for
	 * %SCX_TASK_REENQ_REASON_NONE to distinguish reenqueues.
	 *
	 * NONE		not being reenqueued
	 * KFUNC	reenqueued by scx_bpf_dsq_reenq() and friends
	 * IMMED	reenqueued due to failed ENQ_IMMED
	 * PREEMPTED	preempted while running
	 */
	SCX_TASK_REENQ_REASON_SHIFT = 12,
	SCX_TASK_REENQ_REASON_BITS = 2,
	SCX_TASK_REENQ_REASON_MASK = ((1 << SCX_TASK_REENQ_REASON_BITS) - 1) << SCX_TASK_REENQ_REASON_SHIFT,

	SCX_TASK_REENQ_NONE	= 0 << SCX_TASK_REENQ_REASON_SHIFT,
	SCX_TASK_REENQ_KFUNC	= 1 << SCX_TASK_REENQ_REASON_SHIFT,
	SCX_TASK_REENQ_IMMED	= 2 << SCX_TASK_REENQ_REASON_SHIFT,
	SCX_TASK_REENQ_PREEMPTED = 3 << SCX_TASK_REENQ_REASON_SHIFT,

	/* iteration cursor, not a task */
	SCX_TASK_CURSOR		= 1 << 31,
};

/* scx_entity.dsq_flags */
enum scx_ent_dsq_flags {
	SCX_TASK_DSQ_ON_PRIQ	= 1 << 0, /* task is queued on the priority queue of a dsq */
};

enum scx_dsq_lnode_flags {
	SCX_DSQ_LNODE_ITER_CURSOR = 1 << 0,

	/* high 16 bits can be for iter cursor flags */
	__SCX_DSQ_LNODE_PRIV_SHIFT = 16,
};

struct scx_dsq_list_node {
	struct list_head	node;
	u32			flags;
	u32			priv;		/* can be used by iter cursor */
};

#define INIT_DSQ_LIST_CURSOR(__cursor, __dsq, __flags)				\
	(struct scx_dsq_list_node) {						\
		.node = LIST_HEAD_INIT((__cursor).node),			\
		.flags = SCX_DSQ_LNODE_ITER_CURSOR | (__flags),			\
		.priv = READ_ONCE((__dsq)->seq),				\
	}

struct scx_sched;

/*
 * The following is embedded in task_struct and contains all fields necessary
 * for a task to be scheduled by SCX.
 */
struct sched_ext_entity {
#ifdef CONFIG_CGROUPS
	/*
	 * Associated scx_sched. Updated either during fork or while holding
	 * both p->pi_lock and rq lock.
	 */
	struct scx_sched __rcu	*sched;
#endif
	struct scx_dispatch_q	*dsq;
	atomic_long_t		ops_state;
	u64			ddsp_dsq_id;
	u64			ddsp_enq_flags;
	struct scx_dsq_list_node dsq_list;	/* dispatch order */
	struct rb_node		dsq_priq;	/* p->scx.dsq_vtime order */
	u32			dsq_seq;
	u32			dsq_flags;	/* protected by DSQ lock */
	u32			flags;		/* protected by rq lock */
	u32			weight;
	s32			sticky_cpu;
	s32			holding_cpu;
	s32			selected_cpu;
	struct task_struct	*kf_tasks[2];	/* see SCX_CALL_OP_TASK() */

	struct list_head	runnable_node;	/* rq->scx.runnable_list */
	unsigned long		runnable_at;

#ifdef CONFIG_SCHED_CORE
	u64			core_sched_at;	/* see scx_prio_less() */
#endif

	/* BPF scheduler modifiable fields */

	/*
	 * Runtime budget in nsecs. This is usually set through
	 * scx_bpf_dsq_insert() but can also be modified directly by the BPF
	 * scheduler. Automatically decreased by SCX as the task executes. On
	 * depletion, a scheduling event is triggered.
	 *
	 * This value is cleared to zero if the task is preempted by
	 * %SCX_KICK_PREEMPT and shouldn't be used to determine how long the
	 * task ran. Use p->se.sum_exec_runtime instead.
	 */
	u64			slice;

	/*
	 * Used to order tasks when dispatching to the vtime-ordered priority
	 * queue of a dsq. This is usually set through
	 * scx_bpf_dsq_insert_vtime() but can also be modified directly by the
	 * BPF scheduler. Modifying it while a task is queued on a dsq may
	 * mangle the ordering and is not recommended.
	 */
	u64			dsq_vtime;

	/*
	 * If set, reject future sched_setscheduler(2) calls updating the policy
	 * to %SCHED_EXT with -%EACCES.
	 *
	 * Can be set from ops.init_task() while the BPF scheduler is being
	 * loaded (!scx_init_task_args->fork). If set and the task's policy is
	 * already %SCHED_EXT, the task's policy is rejected and forcefully
	 * reverted to %SCHED_NORMAL. The number of such events are reported
	 * through /sys/kernel/debug/sched_ext::nr_rejected. Setting this flag
	 * during fork is not allowed.
	 */
	bool			disallow;	/* reject switching into SCX */

	/* cold fields */
#ifdef CONFIG_EXT_GROUP_SCHED
	struct cgroup		*cgrp_moving_from;
#endif
	struct list_head	tasks_node;
};

void sched_ext_dead(struct task_struct *p);
void print_scx_info(const char *log_lvl, struct task_struct *p);
void scx_softlockup(u32 dur_s);
bool scx_hardlockup(int cpu);
bool scx_rcu_cpu_stall(void);

#else	/* !CONFIG_SCHED_CLASS_EXT */

static inline void sched_ext_dead(struct task_struct *p) {}
static inline void print_scx_info(const char *log_lvl, struct task_struct *p) {}
static inline void scx_softlockup(u32 dur_s) {}
static inline bool scx_hardlockup(int cpu) { return false; }
static inline bool scx_rcu_cpu_stall(void) { return false; }

#endif	/* CONFIG_SCHED_CLASS_EXT */

struct scx_task_group {
#ifdef CONFIG_EXT_GROUP_SCHED
	u32			flags;		/* SCX_TG_* */
	u32			weight;
	u64			bw_period_us;
	u64			bw_quota_us;
	u64			bw_burst_us;
	bool			idle;
#endif
};

#endif	/* _LINUX_SCHED_EXT_H */
