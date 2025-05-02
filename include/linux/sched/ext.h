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

	SCX_SLICE_DFL		= 20 * 1000000,	/* 20ms */
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
	SCX_DSQ_LOCAL_ON	= SCX_DSQ_FLAG_BUILTIN | SCX_DSQ_FLAG_LOCAL_ON,
	SCX_DSQ_LOCAL_CPU_MASK	= 0xffffffffLLU,
};

/*
 * A dispatch queue (DSQ) can be either a FIFO or p->scx.dsq_vtime ordered
 * queue. A built-in DSQ is always a FIFO. The built-in local DSQs are used to
 * buffer between the scheduler core and the BPF scheduler. See the
 * documentation for more details.
 */
struct scx_dispatch_q {
	raw_spinlock_t		lock;
	struct list_head	list;	/* tasks in dispatch order */
	struct rb_root		priq;	/* used to order by p->scx.dsq_vtime */
	u32			nr;
	u32			seq;	/* used by BPF iter */
	u64			id;
	struct rhash_head	hash_node;
	struct llist_node	free_node;
	struct rcu_head		rcu;
};

/* scx_entity.flags */
enum scx_ent_flags {
	SCX_TASK_QUEUED		= 1 << 0, /* on ext runqueue */
	SCX_TASK_RESET_RUNNABLE_AT = 1 << 2, /* runnable_at should be reset */
	SCX_TASK_DEQD_FOR_SLEEP	= 1 << 3, /* last dequeue was for SLEEP */

	SCX_TASK_STATE_SHIFT	= 8,	  /* bit 8 and 9 are used to carry scx_task_state */
	SCX_TASK_STATE_BITS	= 2,
	SCX_TASK_STATE_MASK	= ((1 << SCX_TASK_STATE_BITS) - 1) << SCX_TASK_STATE_SHIFT,

	SCX_TASK_CURSOR		= 1 << 31, /* iteration cursor, not a task */
};

/* scx_entity.flags & SCX_TASK_STATE_MASK */
enum scx_task_state {
	SCX_TASK_NONE,		/* ops.init_task() not called yet */
	SCX_TASK_INIT,		/* ops.init_task() succeeded, but task can be cancelled */
	SCX_TASK_READY,		/* fully initialized, but not in sched_ext */
	SCX_TASK_ENABLED,	/* fully initialized and in sched_ext */

	SCX_TASK_NR_STATES,
};

/* scx_entity.dsq_flags */
enum scx_ent_dsq_flags {
	SCX_TASK_DSQ_ON_PRIQ	= 1 << 0, /* task is queued on the priority queue of a dsq */
};

/*
 * Mask bits for scx_entity.kf_mask. Not all kfuncs can be called from
 * everywhere and the following bits track which kfunc sets are currently
 * allowed for %current. This simple per-task tracking works because SCX ops
 * nest in a limited way. BPF will likely implement a way to allow and disallow
 * kfuncs depending on the calling context which will replace this manual
 * mechanism. See scx_kf_allow().
 */
enum scx_kf_mask {
	SCX_KF_UNLOCKED		= 0,	  /* sleepable and not rq locked */
	/* ENQUEUE and DISPATCH may be nested inside CPU_RELEASE */
	SCX_KF_CPU_RELEASE	= 1 << 0, /* ops.cpu_release() */
	/* ops.dequeue (in REST) may be nested inside DISPATCH */
	SCX_KF_DISPATCH		= 1 << 1, /* ops.dispatch() */
	SCX_KF_ENQUEUE		= 1 << 2, /* ops.enqueue() and ops.select_cpu() */
	SCX_KF_SELECT_CPU	= 1 << 3, /* ops.select_cpu() */
	SCX_KF_REST		= 1 << 4, /* other rq-locked operations */

	__SCX_KF_RQ_LOCKED	= SCX_KF_CPU_RELEASE | SCX_KF_DISPATCH |
				  SCX_KF_ENQUEUE | SCX_KF_SELECT_CPU | SCX_KF_REST,
	__SCX_KF_TERMINAL	= SCX_KF_ENQUEUE | SCX_KF_SELECT_CPU | SCX_KF_REST,
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

/*
 * The following is embedded in task_struct and contains all fields necessary
 * for a task to be scheduled by SCX.
 */
struct sched_ext_entity {
	struct scx_dispatch_q	*dsq;
	struct scx_dsq_list_node dsq_list;	/* dispatch order */
	struct rb_node		dsq_priq;	/* p->scx.dsq_vtime order */
	u32			dsq_seq;
	u32			dsq_flags;	/* protected by DSQ lock */
	u32			flags;		/* protected by rq lock */
	u32			weight;
	s32			sticky_cpu;
	s32			holding_cpu;
	s32			selected_cpu;
	u32			kf_mask;	/* see scx_kf_mask above */
	struct task_struct	*kf_tasks[2];	/* see SCX_CALL_OP_TASK() */
	atomic_long_t		ops_state;

	struct list_head	runnable_node;	/* rq->scx.runnable_list */
	unsigned long		runnable_at;

#ifdef CONFIG_SCHED_CORE
	u64			core_sched_at;	/* see scx_prio_less() */
#endif
	u64			ddsp_dsq_id;
	u64			ddsp_enq_flags;

	/* BPF scheduler modifiable fields */

	/*
	 * Runtime budget in nsecs. This is usually set through
	 * scx_bpf_dispatch() but can also be modified directly by the BPF
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
	 * queue of a dsq. This is usually set through scx_bpf_dispatch_vtime()
	 * but can also be modified directly by the BPF scheduler. Modifying it
	 * while a task is queued on a dsq may mangle the ordering and is not
	 * recommended.
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

void sched_ext_free(struct task_struct *p);
void print_scx_info(const char *log_lvl, struct task_struct *p);
void scx_softlockup(u32 dur_s);

#else	/* !CONFIG_SCHED_CLASS_EXT */

static inline void sched_ext_free(struct task_struct *p) {}
static inline void print_scx_info(const char *log_lvl, struct task_struct *p) {}
static inline void scx_softlockup(u32 dur_s) {}

#endif	/* CONFIG_SCHED_CLASS_EXT */
#endif	/* _LINUX_SCHED_EXT_H */
