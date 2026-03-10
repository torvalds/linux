/* SPDX-License-Identifier: GPL-2.0 */
/*
 * BPF extensible scheduler class: Documentation/scheduler/sched-ext.rst
 *
 * Copyright (c) 2025 Meta Platforms, Inc. and affiliates.
 * Copyright (c) 2025 Tejun Heo <tj@kernel.org>
 */
#define SCX_OP_IDX(op)		(offsetof(struct sched_ext_ops, op) / sizeof(void (*)(void)))

enum scx_consts {
	SCX_DSP_DFL_MAX_BATCH		= 32,
	SCX_DSP_MAX_LOOPS		= 32,
	SCX_WATCHDOG_MAX_TIMEOUT	= 30 * HZ,

	SCX_EXIT_BT_LEN			= 64,
	SCX_EXIT_MSG_LEN		= 1024,
	SCX_EXIT_DUMP_DFL_LEN		= 32768,

	SCX_CPUPERF_ONE			= SCHED_CAPACITY_SCALE,

	/*
	 * Iterating all tasks may take a while. Periodically drop
	 * scx_tasks_lock to avoid causing e.g. CSD and RCU stalls.
	 */
	SCX_TASK_ITER_BATCH		= 32,

	SCX_BYPASS_HOST_NTH		= 2,

	SCX_BYPASS_LB_DFL_INTV_US	= 500 * USEC_PER_MSEC,
	SCX_BYPASS_LB_DONOR_PCT		= 125,
	SCX_BYPASS_LB_MIN_DELTA_DIV	= 4,
	SCX_BYPASS_LB_BATCH		= 256,

	SCX_SUB_MAX_DEPTH		= 4,
};

enum scx_exit_kind {
	SCX_EXIT_NONE,
	SCX_EXIT_DONE,

	SCX_EXIT_UNREG = 64,	/* user-space initiated unregistration */
	SCX_EXIT_UNREG_BPF,	/* BPF-initiated unregistration */
	SCX_EXIT_UNREG_KERN,	/* kernel-initiated unregistration */
	SCX_EXIT_SYSRQ,		/* requested by 'S' sysrq */
	SCX_EXIT_PARENT,	/* parent exiting */

	SCX_EXIT_ERROR = 1024,	/* runtime error, error msg contains details */
	SCX_EXIT_ERROR_BPF,	/* ERROR but triggered through scx_bpf_error() */
	SCX_EXIT_ERROR_STALL,	/* watchdog detected stalled runnable tasks */
};

/*
 * An exit code can be specified when exiting with scx_bpf_exit() or scx_exit(),
 * corresponding to exit_kind UNREG_BPF and UNREG_KERN respectively. The codes
 * are 64bit of the format:
 *
 *   Bits: [63  ..  48 47   ..  32 31 .. 0]
 *         [ SYS ACT ] [ SYS RSN ] [ USR  ]
 *
 *   SYS ACT: System-defined exit actions
 *   SYS RSN: System-defined exit reasons
 *   USR    : User-defined exit codes and reasons
 *
 * Using the above, users may communicate intention and context by ORing system
 * actions and/or system reasons with a user-defined exit code.
 */
enum scx_exit_code {
	/* Reasons */
	SCX_ECODE_RSN_HOTPLUG	= 1LLU << 32,
	SCX_ECODE_RSN_CGROUP_OFFLINE = 2LLU << 32,

	/* Actions */
	SCX_ECODE_ACT_RESTART	= 1LLU << 48,
};

enum scx_exit_flags {
	/*
	 * ops.exit() may be called even if the loading failed before ops.init()
	 * finishes successfully. This is because ops.exit() allows rich exit
	 * info communication. The following flag indicates whether ops.init()
	 * finished successfully.
	 */
	SCX_EFLAG_INITIALIZED   = 1LLU << 0,
};

/*
 * scx_exit_info is passed to ops.exit() to describe why the BPF scheduler is
 * being disabled.
 */
struct scx_exit_info {
	/* %SCX_EXIT_* - broad category of the exit reason */
	enum scx_exit_kind	kind;

	/* exit code if gracefully exiting */
	s64			exit_code;

	/* %SCX_EFLAG_* */
	u64			flags;

	/* textual representation of the above */
	const char		*reason;

	/* backtrace if exiting due to an error */
	unsigned long		*bt;
	u32			bt_len;

	/* informational message */
	char			*msg;

	/* debug dump */
	char			*dump;
};

/* sched_ext_ops.flags */
enum scx_ops_flags {
	/*
	 * Keep built-in idle tracking even if ops.update_idle() is implemented.
	 */
	SCX_OPS_KEEP_BUILTIN_IDLE	= 1LLU << 0,

	/*
	 * By default, if there are no other task to run on the CPU, ext core
	 * keeps running the current task even after its slice expires. If this
	 * flag is specified, such tasks are passed to ops.enqueue() with
	 * %SCX_ENQ_LAST. See the comment above %SCX_ENQ_LAST for more info.
	 */
	SCX_OPS_ENQ_LAST		= 1LLU << 1,

	/*
	 * An exiting task may schedule after PF_EXITING is set. In such cases,
	 * bpf_task_from_pid() may not be able to find the task and if the BPF
	 * scheduler depends on pid lookup for dispatching, the task will be
	 * lost leading to various issues including RCU grace period stalls.
	 *
	 * To mask this problem, by default, unhashed tasks are automatically
	 * dispatched to the local DSQ on enqueue. If the BPF scheduler doesn't
	 * depend on pid lookups and wants to handle these tasks directly, the
	 * following flag can be used.
	 */
	SCX_OPS_ENQ_EXITING		= 1LLU << 2,

	/*
	 * If set, only tasks with policy set to SCHED_EXT are attached to
	 * sched_ext. If clear, SCHED_NORMAL tasks are also included.
	 */
	SCX_OPS_SWITCH_PARTIAL		= 1LLU << 3,

	/*
	 * A migration disabled task can only execute on its current CPU. By
	 * default, such tasks are automatically put on the CPU's local DSQ with
	 * the default slice on enqueue. If this ops flag is set, they also go
	 * through ops.enqueue().
	 *
	 * A migration disabled task never invokes ops.select_cpu() as it can
	 * only select the current CPU. Also, p->cpus_ptr will only contain its
	 * current CPU while p->nr_cpus_allowed keeps tracking p->user_cpus_ptr
	 * and thus may disagree with cpumask_weight(p->cpus_ptr).
	 */
	SCX_OPS_ENQ_MIGRATION_DISABLED	= 1LLU << 4,

	/*
	 * Queued wakeup (ttwu_queue) is a wakeup optimization that invokes
	 * ops.enqueue() on the ops.select_cpu() selected or the wakee's
	 * previous CPU via IPI (inter-processor interrupt) to reduce cacheline
	 * transfers. When this optimization is enabled, ops.select_cpu() is
	 * skipped in some cases (when racing against the wakee switching out).
	 * As the BPF scheduler may depend on ops.select_cpu() being invoked
	 * during wakeups, queued wakeup is disabled by default.
	 *
	 * If this ops flag is set, queued wakeup optimization is enabled and
	 * the BPF scheduler must be able to handle ops.enqueue() invoked on the
	 * wakee's CPU without preceding ops.select_cpu() even for tasks which
	 * may be executed on multiple CPUs.
	 */
	SCX_OPS_ALLOW_QUEUED_WAKEUP	= 1LLU << 5,

	/*
	 * If set, enable per-node idle cpumasks. If clear, use a single global
	 * flat idle cpumask.
	 */
	SCX_OPS_BUILTIN_IDLE_PER_NODE	= 1LLU << 6,

	SCX_OPS_ALL_FLAGS		= SCX_OPS_KEEP_BUILTIN_IDLE |
					  SCX_OPS_ENQ_LAST |
					  SCX_OPS_ENQ_EXITING |
					  SCX_OPS_ENQ_MIGRATION_DISABLED |
					  SCX_OPS_ALLOW_QUEUED_WAKEUP |
					  SCX_OPS_SWITCH_PARTIAL |
					  SCX_OPS_BUILTIN_IDLE_PER_NODE,

	/* high 8 bits are internal, don't include in SCX_OPS_ALL_FLAGS */
	__SCX_OPS_INTERNAL_MASK		= 0xffLLU << 56,

	SCX_OPS_HAS_CPU_PREEMPT		= 1LLU << 56,
};

/* argument container for ops.init_task() */
struct scx_init_task_args {
	/*
	 * Set if ops.init_task() is being invoked on the fork path, as opposed
	 * to the scheduler transition path.
	 */
	bool			fork;
#ifdef CONFIG_EXT_GROUP_SCHED
	/* the cgroup the task is joining */
	struct cgroup		*cgroup;
#endif
};

/* argument container for ops.exit_task() */
struct scx_exit_task_args {
	/* Whether the task exited before running on sched_ext. */
	bool cancelled;
};

/* argument container for ops.cgroup_init() */
struct scx_cgroup_init_args {
	/* the weight of the cgroup [1..10000] */
	u32			weight;

	/* bandwidth control parameters from cpu.max and cpu.max.burst */
	u64			bw_period_us;
	u64			bw_quota_us;
	u64			bw_burst_us;
};

enum scx_cpu_preempt_reason {
	/* next task is being scheduled by &sched_class_rt */
	SCX_CPU_PREEMPT_RT,
	/* next task is being scheduled by &sched_class_dl */
	SCX_CPU_PREEMPT_DL,
	/* next task is being scheduled by &sched_class_stop */
	SCX_CPU_PREEMPT_STOP,
	/* unknown reason for SCX being preempted */
	SCX_CPU_PREEMPT_UNKNOWN,
};

/*
 * Argument container for ops.cpu_acquire(). Currently empty, but may be
 * expanded in the future.
 */
struct scx_cpu_acquire_args {};

/* argument container for ops.cpu_release() */
struct scx_cpu_release_args {
	/* the reason the CPU was preempted */
	enum scx_cpu_preempt_reason reason;

	/* the task that's going to be scheduled on the CPU */
	struct task_struct	*task;
};

/* informational context provided to dump operations */
struct scx_dump_ctx {
	enum scx_exit_kind	kind;
	s64			exit_code;
	const char		*reason;
	u64			at_ns;
	u64			at_jiffies;
};

/* argument container for ops.sub_attach() */
struct scx_sub_attach_args {
	struct sched_ext_ops	*ops;
	char			*cgroup_path;
};

/* argument container for ops.sub_detach() */
struct scx_sub_detach_args {
	struct sched_ext_ops	*ops;
	char			*cgroup_path;
};

/**
 * struct sched_ext_ops - Operation table for BPF scheduler implementation
 *
 * A BPF scheduler can implement an arbitrary scheduling policy by
 * implementing and loading operations in this table. Note that a userland
 * scheduling policy can also be implemented using the BPF scheduler
 * as a shim layer.
 */
struct sched_ext_ops {
	/**
	 * @select_cpu: Pick the target CPU for a task which is being woken up
	 * @p: task being woken up
	 * @prev_cpu: the cpu @p was on before sleeping
	 * @wake_flags: SCX_WAKE_*
	 *
	 * Decision made here isn't final. @p may be moved to any CPU while it
	 * is getting dispatched for execution later. However, as @p is not on
	 * the rq at this point, getting the eventual execution CPU right here
	 * saves a small bit of overhead down the line.
	 *
	 * If an idle CPU is returned, the CPU is kicked and will try to
	 * dispatch. While an explicit custom mechanism can be added,
	 * select_cpu() serves as the default way to wake up idle CPUs.
	 *
	 * @p may be inserted into a DSQ directly by calling
	 * scx_bpf_dsq_insert(). If so, the ops.enqueue() will be skipped.
	 * Directly inserting into %SCX_DSQ_LOCAL will put @p in the local DSQ
	 * of the CPU returned by this operation.
	 *
	 * Note that select_cpu() is never called for tasks that can only run
	 * on a single CPU or tasks with migration disabled, as they don't have
	 * the option to select a different CPU. See select_task_rq() for
	 * details.
	 */
	s32 (*select_cpu)(struct task_struct *p, s32 prev_cpu, u64 wake_flags);

	/**
	 * @enqueue: Enqueue a task on the BPF scheduler
	 * @p: task being enqueued
	 * @enq_flags: %SCX_ENQ_*
	 *
	 * @p is ready to run. Insert directly into a DSQ by calling
	 * scx_bpf_dsq_insert() or enqueue on the BPF scheduler. If not directly
	 * inserted, the bpf scheduler owns @p and if it fails to dispatch @p,
	 * the task will stall.
	 *
	 * If @p was inserted into a DSQ from ops.select_cpu(), this callback is
	 * skipped.
	 */
	void (*enqueue)(struct task_struct *p, u64 enq_flags);

	/**
	 * @dequeue: Remove a task from the BPF scheduler
	 * @p: task being dequeued
	 * @deq_flags: %SCX_DEQ_*
	 *
	 * Remove @p from the BPF scheduler. This is usually called to isolate
	 * the task while updating its scheduling properties (e.g. priority).
	 *
	 * The ext core keeps track of whether the BPF side owns a given task or
	 * not and can gracefully ignore spurious dispatches from BPF side,
	 * which makes it safe to not implement this method. However, depending
	 * on the scheduling logic, this can lead to confusing behaviors - e.g.
	 * scheduling position not being updated across a priority change.
	 */
	void (*dequeue)(struct task_struct *p, u64 deq_flags);

	/**
	 * @dispatch: Dispatch tasks from the BPF scheduler and/or user DSQs
	 * @cpu: CPU to dispatch tasks for
	 * @prev: previous task being switched out
	 *
	 * Called when a CPU's local dsq is empty. The operation should dispatch
	 * one or more tasks from the BPF scheduler into the DSQs using
	 * scx_bpf_dsq_insert() and/or move from user DSQs into the local DSQ
	 * using scx_bpf_dsq_move_to_local().
	 *
	 * The maximum number of times scx_bpf_dsq_insert() can be called
	 * without an intervening scx_bpf_dsq_move_to_local() is specified by
	 * ops.dispatch_max_batch. See the comments on top of the two functions
	 * for more details.
	 *
	 * When not %NULL, @prev is an SCX task with its slice depleted. If
	 * @prev is still runnable as indicated by set %SCX_TASK_QUEUED in
	 * @prev->scx.flags, it is not enqueued yet and will be enqueued after
	 * ops.dispatch() returns. To keep executing @prev, return without
	 * dispatching or moving any tasks. Also see %SCX_OPS_ENQ_LAST.
	 */
	void (*dispatch)(s32 cpu, struct task_struct *prev);

	/**
	 * @tick: Periodic tick
	 * @p: task running currently
	 *
	 * This operation is called every 1/HZ seconds on CPUs which are
	 * executing an SCX task. Setting @p->scx.slice to 0 will trigger an
	 * immediate dispatch cycle on the CPU.
	 */
	void (*tick)(struct task_struct *p);

	/**
	 * @runnable: A task is becoming runnable on its associated CPU
	 * @p: task becoming runnable
	 * @enq_flags: %SCX_ENQ_*
	 *
	 * This and the following three functions can be used to track a task's
	 * execution state transitions. A task becomes ->runnable() on a CPU,
	 * and then goes through one or more ->running() and ->stopping() pairs
	 * as it runs on the CPU, and eventually becomes ->quiescent() when it's
	 * done running on the CPU.
	 *
	 * @p is becoming runnable on the CPU because it's
	 *
	 * - waking up (%SCX_ENQ_WAKEUP)
	 * - being moved from another CPU
	 * - being restored after temporarily taken off the queue for an
	 *   attribute change.
	 *
	 * This and ->enqueue() are related but not coupled. This operation
	 * notifies @p's state transition and may not be followed by ->enqueue()
	 * e.g. when @p is being dispatched to a remote CPU, or when @p is
	 * being enqueued on a CPU experiencing a hotplug event. Likewise, a
	 * task may be ->enqueue()'d without being preceded by this operation
	 * e.g. after exhausting its slice.
	 */
	void (*runnable)(struct task_struct *p, u64 enq_flags);

	/**
	 * @running: A task is starting to run on its associated CPU
	 * @p: task starting to run
	 *
	 * Note that this callback may be called from a CPU other than the
	 * one the task is going to run on. This can happen when a task
	 * property is changed (i.e., affinity), since scx_next_task_scx(),
	 * which triggers this callback, may run on a CPU different from
	 * the task's assigned CPU.
	 *
	 * Therefore, always use scx_bpf_task_cpu(@p) to determine the
	 * target CPU the task is going to use.
	 *
	 * See ->runnable() for explanation on the task state notifiers.
	 */
	void (*running)(struct task_struct *p);

	/**
	 * @stopping: A task is stopping execution
	 * @p: task stopping to run
	 * @runnable: is task @p still runnable?
	 *
	 * Note that this callback may be called from a CPU other than the
	 * one the task was running on. This can happen when a task
	 * property is changed (i.e., affinity), since dequeue_task_scx(),
	 * which triggers this callback, may run on a CPU different from
	 * the task's assigned CPU.
	 *
	 * Therefore, always use scx_bpf_task_cpu(@p) to retrieve the CPU
	 * the task was running on.
	 *
	 * See ->runnable() for explanation on the task state notifiers. If
	 * !@runnable, ->quiescent() will be invoked after this operation
	 * returns.
	 */
	void (*stopping)(struct task_struct *p, bool runnable);

	/**
	 * @quiescent: A task is becoming not runnable on its associated CPU
	 * @p: task becoming not runnable
	 * @deq_flags: %SCX_DEQ_*
	 *
	 * See ->runnable() for explanation on the task state notifiers.
	 *
	 * @p is becoming quiescent on the CPU because it's
	 *
	 * - sleeping (%SCX_DEQ_SLEEP)
	 * - being moved to another CPU
	 * - being temporarily taken off the queue for an attribute change
	 *   (%SCX_DEQ_SAVE)
	 *
	 * This and ->dequeue() are related but not coupled. This operation
	 * notifies @p's state transition and may not be preceded by ->dequeue()
	 * e.g. when @p is being dispatched to a remote CPU.
	 */
	void (*quiescent)(struct task_struct *p, u64 deq_flags);

	/**
	 * @yield: Yield CPU
	 * @from: yielding task
	 * @to: optional yield target task
	 *
	 * If @to is NULL, @from is yielding the CPU to other runnable tasks.
	 * The BPF scheduler should ensure that other available tasks are
	 * dispatched before the yielding task. Return value is ignored in this
	 * case.
	 *
	 * If @to is not-NULL, @from wants to yield the CPU to @to. If the bpf
	 * scheduler can implement the request, return %true; otherwise, %false.
	 */
	bool (*yield)(struct task_struct *from, struct task_struct *to);

	/**
	 * @core_sched_before: Task ordering for core-sched
	 * @a: task A
	 * @b: task B
	 *
	 * Used by core-sched to determine the ordering between two tasks. See
	 * Documentation/admin-guide/hw-vuln/core-scheduling.rst for details on
	 * core-sched.
	 *
	 * Both @a and @b are runnable and may or may not currently be queued on
	 * the BPF scheduler. Should return %true if @a should run before @b.
	 * %false if there's no required ordering or @b should run before @a.
	 *
	 * If not specified, the default is ordering them according to when they
	 * became runnable.
	 */
	bool (*core_sched_before)(struct task_struct *a, struct task_struct *b);

	/**
	 * @set_weight: Set task weight
	 * @p: task to set weight for
	 * @weight: new weight [1..10000]
	 *
	 * Update @p's weight to @weight.
	 */
	void (*set_weight)(struct task_struct *p, u32 weight);

	/**
	 * @set_cpumask: Set CPU affinity
	 * @p: task to set CPU affinity for
	 * @cpumask: cpumask of cpus that @p can run on
	 *
	 * Update @p's CPU affinity to @cpumask.
	 */
	void (*set_cpumask)(struct task_struct *p,
			    const struct cpumask *cpumask);

	/**
	 * @update_idle: Update the idle state of a CPU
	 * @cpu: CPU to update the idle state for
	 * @idle: whether entering or exiting the idle state
	 *
	 * This operation is called when @rq's CPU goes or leaves the idle
	 * state. By default, implementing this operation disables the built-in
	 * idle CPU tracking and the following helpers become unavailable:
	 *
	 * - scx_bpf_select_cpu_dfl()
	 * - scx_bpf_select_cpu_and()
	 * - scx_bpf_test_and_clear_cpu_idle()
	 * - scx_bpf_pick_idle_cpu()
	 *
	 * The user also must implement ops.select_cpu() as the default
	 * implementation relies on scx_bpf_select_cpu_dfl().
	 *
	 * Specify the %SCX_OPS_KEEP_BUILTIN_IDLE flag to keep the built-in idle
	 * tracking.
	 */
	void (*update_idle)(s32 cpu, bool idle);

	/**
	 * @cpu_acquire: A CPU is becoming available to the BPF scheduler
	 * @cpu: The CPU being acquired by the BPF scheduler.
	 * @args: Acquire arguments, see the struct definition.
	 *
	 * A CPU that was previously released from the BPF scheduler is now once
	 * again under its control.
	 */
	void (*cpu_acquire)(s32 cpu, struct scx_cpu_acquire_args *args);

	/**
	 * @cpu_release: A CPU is taken away from the BPF scheduler
	 * @cpu: The CPU being released by the BPF scheduler.
	 * @args: Release arguments, see the struct definition.
	 *
	 * The specified CPU is no longer under the control of the BPF
	 * scheduler. This could be because it was preempted by a higher
	 * priority sched_class, though there may be other reasons as well. The
	 * caller should consult @args->reason to determine the cause.
	 */
	void (*cpu_release)(s32 cpu, struct scx_cpu_release_args *args);

	/**
	 * @init_task: Initialize a task to run in a BPF scheduler
	 * @p: task to initialize for BPF scheduling
	 * @args: init arguments, see the struct definition
	 *
	 * Either we're loading a BPF scheduler or a new task is being forked.
	 * Initialize @p for BPF scheduling. This operation may block and can
	 * be used for allocations, and is called exactly once for a task.
	 *
	 * Return 0 for success, -errno for failure. An error return while
	 * loading will abort loading of the BPF scheduler. During a fork, it
	 * will abort that specific fork.
	 */
	s32 (*init_task)(struct task_struct *p, struct scx_init_task_args *args);

	/**
	 * @exit_task: Exit a previously-running task from the system
	 * @p: task to exit
	 * @args: exit arguments, see the struct definition
	 *
	 * @p is exiting or the BPF scheduler is being unloaded. Perform any
	 * necessary cleanup for @p.
	 */
	void (*exit_task)(struct task_struct *p, struct scx_exit_task_args *args);

	/**
	 * @enable: Enable BPF scheduling for a task
	 * @p: task to enable BPF scheduling for
	 *
	 * Enable @p for BPF scheduling. enable() is called on @p any time it
	 * enters SCX, and is always paired with a matching disable().
	 */
	void (*enable)(struct task_struct *p);

	/**
	 * @disable: Disable BPF scheduling for a task
	 * @p: task to disable BPF scheduling for
	 *
	 * @p is exiting, leaving SCX or the BPF scheduler is being unloaded.
	 * Disable BPF scheduling for @p. A disable() call is always matched
	 * with a prior enable() call.
	 */
	void (*disable)(struct task_struct *p);

	/**
	 * @dump: Dump BPF scheduler state on error
	 * @ctx: debug dump context
	 *
	 * Use scx_bpf_dump() to generate BPF scheduler specific debug dump.
	 */
	void (*dump)(struct scx_dump_ctx *ctx);

	/**
	 * @dump_cpu: Dump BPF scheduler state for a CPU on error
	 * @ctx: debug dump context
	 * @cpu: CPU to generate debug dump for
	 * @idle: @cpu is currently idle without any runnable tasks
	 *
	 * Use scx_bpf_dump() to generate BPF scheduler specific debug dump for
	 * @cpu. If @idle is %true and this operation doesn't produce any
	 * output, @cpu is skipped for dump.
	 */
	void (*dump_cpu)(struct scx_dump_ctx *ctx, s32 cpu, bool idle);

	/**
	 * @dump_task: Dump BPF scheduler state for a runnable task on error
	 * @ctx: debug dump context
	 * @p: runnable task to generate debug dump for
	 *
	 * Use scx_bpf_dump() to generate BPF scheduler specific debug dump for
	 * @p.
	 */
	void (*dump_task)(struct scx_dump_ctx *ctx, struct task_struct *p);

#ifdef CONFIG_EXT_GROUP_SCHED
	/**
	 * @cgroup_init: Initialize a cgroup
	 * @cgrp: cgroup being initialized
	 * @args: init arguments, see the struct definition
	 *
	 * Either the BPF scheduler is being loaded or @cgrp created, initialize
	 * @cgrp for sched_ext. This operation may block.
	 *
	 * Return 0 for success, -errno for failure. An error return while
	 * loading will abort loading of the BPF scheduler. During cgroup
	 * creation, it will abort the specific cgroup creation.
	 */
	s32 (*cgroup_init)(struct cgroup *cgrp,
			   struct scx_cgroup_init_args *args);

	/**
	 * @cgroup_exit: Exit a cgroup
	 * @cgrp: cgroup being exited
	 *
	 * Either the BPF scheduler is being unloaded or @cgrp destroyed, exit
	 * @cgrp for sched_ext. This operation my block.
	 */
	void (*cgroup_exit)(struct cgroup *cgrp);

	/**
	 * @cgroup_prep_move: Prepare a task to be moved to a different cgroup
	 * @p: task being moved
	 * @from: cgroup @p is being moved from
	 * @to: cgroup @p is being moved to
	 *
	 * Prepare @p for move from cgroup @from to @to. This operation may
	 * block and can be used for allocations.
	 *
	 * Return 0 for success, -errno for failure. An error return aborts the
	 * migration.
	 */
	s32 (*cgroup_prep_move)(struct task_struct *p,
				struct cgroup *from, struct cgroup *to);

	/**
	 * @cgroup_move: Commit cgroup move
	 * @p: task being moved
	 * @from: cgroup @p is being moved from
	 * @to: cgroup @p is being moved to
	 *
	 * Commit the move. @p is dequeued during this operation.
	 */
	void (*cgroup_move)(struct task_struct *p,
			    struct cgroup *from, struct cgroup *to);

	/**
	 * @cgroup_cancel_move: Cancel cgroup move
	 * @p: task whose cgroup move is being canceled
	 * @from: cgroup @p was being moved from
	 * @to: cgroup @p was being moved to
	 *
	 * @p was cgroup_prep_move()'d but failed before reaching cgroup_move().
	 * Undo the preparation.
	 */
	void (*cgroup_cancel_move)(struct task_struct *p,
				   struct cgroup *from, struct cgroup *to);

	/**
	 * @cgroup_set_weight: A cgroup's weight is being changed
	 * @cgrp: cgroup whose weight is being updated
	 * @weight: new weight [1..10000]
	 *
	 * Update @cgrp's weight to @weight.
	 */
	void (*cgroup_set_weight)(struct cgroup *cgrp, u32 weight);

	/**
	 * @cgroup_set_bandwidth: A cgroup's bandwidth is being changed
	 * @cgrp: cgroup whose bandwidth is being updated
	 * @period_us: bandwidth control period
	 * @quota_us: bandwidth control quota
	 * @burst_us: bandwidth control burst
	 *
	 * Update @cgrp's bandwidth control parameters. This is from the cpu.max
	 * cgroup interface.
	 *
	 * @quota_us / @period_us determines the CPU bandwidth @cgrp is entitled
	 * to. For example, if @period_us is 1_000_000 and @quota_us is
	 * 2_500_000. @cgrp is entitled to 2.5 CPUs. @burst_us can be
	 * interpreted in the same fashion and specifies how much @cgrp can
	 * burst temporarily. The specific control mechanism and thus the
	 * interpretation of @period_us and burstiness is up to the BPF
	 * scheduler.
	 */
	void (*cgroup_set_bandwidth)(struct cgroup *cgrp,
				     u64 period_us, u64 quota_us, u64 burst_us);

	/**
	 * @cgroup_set_idle: A cgroup's idle state is being changed
	 * @cgrp: cgroup whose idle state is being updated
	 * @idle: whether the cgroup is entering or exiting idle state
	 *
	 * Update @cgrp's idle state to @idle. This callback is invoked when
	 * a cgroup transitions between idle and non-idle states, allowing the
	 * BPF scheduler to adjust its behavior accordingly.
	 */
	void (*cgroup_set_idle)(struct cgroup *cgrp, bool idle);

#endif	/* CONFIG_EXT_GROUP_SCHED */

	/**
	 * @sub_attach: Attach a sub-scheduler
	 * @args: argument container, see the struct definition
	 *
	 * Return 0 to accept the sub-scheduler. -errno to reject.
	 */
	s32 (*sub_attach)(struct scx_sub_attach_args *args);

	/**
	 * @sub_detach: Detach a sub-scheduler
	 * @args: argument container, see the struct definition
	 */
	void (*sub_detach)(struct scx_sub_detach_args *args);

	/*
	 * All online ops must come before ops.cpu_online().
	 */

	/**
	 * @cpu_online: A CPU became online
	 * @cpu: CPU which just came up
	 *
	 * @cpu just came online. @cpu will not call ops.enqueue() or
	 * ops.dispatch(), nor run tasks associated with other CPUs beforehand.
	 */
	void (*cpu_online)(s32 cpu);

	/**
	 * @cpu_offline: A CPU is going offline
	 * @cpu: CPU which is going offline
	 *
	 * @cpu is going offline. @cpu will not call ops.enqueue() or
	 * ops.dispatch(), nor run tasks associated with other CPUs afterwards.
	 */
	void (*cpu_offline)(s32 cpu);

	/*
	 * All CPU hotplug ops must come before ops.init().
	 */

	/**
	 * @init: Initialize the BPF scheduler
	 */
	s32 (*init)(void);

	/**
	 * @exit: Clean up after the BPF scheduler
	 * @info: Exit info
	 *
	 * ops.exit() is also called on ops.init() failure, which is a bit
	 * unusual. This is to allow rich reporting through @info on how
	 * ops.init() failed.
	 */
	void (*exit)(struct scx_exit_info *info);

	/*
	 * Data fields must comes after all ops fields.
	 */

	/**
	 * @dispatch_max_batch: Max nr of tasks that dispatch() can dispatch
	 */
	u32 dispatch_max_batch;

	/**
	 * @flags: %SCX_OPS_* flags
	 */
	u64 flags;

	/**
	 * @timeout_ms: The maximum amount of time, in milliseconds, that a
	 * runnable task should be able to wait before being scheduled. The
	 * maximum timeout may not exceed the default timeout of 30 seconds.
	 *
	 * Defaults to the maximum allowed timeout value of 30 seconds.
	 */
	u32 timeout_ms;

	/**
	 * @exit_dump_len: scx_exit_info.dump buffer length. If 0, the default
	 * value of 32768 is used.
	 */
	u32 exit_dump_len;

	/**
	 * @hotplug_seq: A sequence number that may be set by the scheduler to
	 * detect when a hotplug event has occurred during the loading process.
	 * If 0, no detection occurs. Otherwise, the scheduler will fail to
	 * load if the sequence number does not match @scx_hotplug_seq on the
	 * enable path.
	 */
	u64 hotplug_seq;

	/**
	 * @cgroup_id: When >1, attach the scheduler as a sub-scheduler on the
	 * specified cgroup.
	 */
	u64 sub_cgroup_id;

	/**
	 * @name: BPF scheduler's name
	 *
	 * Must be a non-zero valid BPF object name including only isalnum(),
	 * '_' and '.' chars. Shows up in kernel.sched_ext_ops sysctl while the
	 * BPF scheduler is enabled.
	 */
	char name[SCX_OPS_NAME_LEN];

	/* internal use only, must be NULL */
	void __rcu *priv;
};

enum scx_opi {
	SCX_OPI_BEGIN			= 0,
	SCX_OPI_NORMAL_BEGIN		= 0,
	SCX_OPI_NORMAL_END		= SCX_OP_IDX(cpu_online),
	SCX_OPI_CPU_HOTPLUG_BEGIN	= SCX_OP_IDX(cpu_online),
	SCX_OPI_CPU_HOTPLUG_END		= SCX_OP_IDX(init),
	SCX_OPI_END			= SCX_OP_IDX(init),
};

/*
 * Collection of event counters. Event types are placed in descending order.
 */
struct scx_event_stats {
	/*
	 * If ops.select_cpu() returns a CPU which can't be used by the task,
	 * the core scheduler code silently picks a fallback CPU.
	 */
	s64		SCX_EV_SELECT_CPU_FALLBACK;

	/*
	 * When dispatching to a local DSQ, the CPU may have gone offline in
	 * the meantime. In this case, the task is bounced to the global DSQ.
	 */
	s64		SCX_EV_DISPATCH_LOCAL_DSQ_OFFLINE;

	/*
	 * If SCX_OPS_ENQ_LAST is not set, the number of times that a task
	 * continued to run because there were no other tasks on the CPU.
	 */
	s64		SCX_EV_DISPATCH_KEEP_LAST;

	/*
	 * If SCX_OPS_ENQ_EXITING is not set, the number of times that a task
	 * is dispatched to a local DSQ when exiting.
	 */
	s64		SCX_EV_ENQ_SKIP_EXITING;

	/*
	 * If SCX_OPS_ENQ_MIGRATION_DISABLED is not set, the number of times a
	 * migration disabled task skips ops.enqueue() and is dispatched to its
	 * local DSQ.
	 */
	s64		SCX_EV_ENQ_SKIP_MIGRATION_DISABLED;

	/*
	 * Total number of times a task's time slice was refilled with the
	 * default value (SCX_SLICE_DFL).
	 */
	s64		SCX_EV_REFILL_SLICE_DFL;

	/*
	 * The total duration of bypass modes in nanoseconds.
	 */
	s64		SCX_EV_BYPASS_DURATION;

	/*
	 * The number of tasks dispatched in the bypassing mode.
	 */
	s64		SCX_EV_BYPASS_DISPATCH;

	/*
	 * The number of times the bypassing mode has been activated.
	 */
	s64		SCX_EV_BYPASS_ACTIVATE;

	/*
	 * The number of times the scheduler attempted to insert a task that it
	 * doesn't own into a DSQ. Such attempts are ignored.
	 *
	 * As BPF schedulers are allowed to ignore dequeues, it's difficult to
	 * tell whether such an attempt is from a scheduler malfunction or an
	 * ignored dequeue around sub-sched enabling. If this count keeps going
	 * up regardless of sub-sched enabling, it likely indicates a bug in the
	 * scheduler.
	 */
	s64		SCX_EV_INSERT_NOT_OWNED;

	/*
	 * The number of times tasks from bypassing descendants are scheduled
	 * from sub_bypass_dsq's.
	 */
	s64		SCX_EV_SUB_BYPASS_DISPATCH;
};

struct scx_sched;

enum scx_sched_pcpu_flags {
	SCX_SCHED_PCPU_BYPASSING	= 1LLU << 0,
};

/* dispatch buf */
struct scx_dsp_buf_ent {
	struct task_struct	*task;
	unsigned long		qseq;
	u64			dsq_id;
	u64			enq_flags;
};

struct scx_dsp_ctx {
	struct rq		*rq;
	u32			cursor;
	u32			nr_tasks;
	struct scx_dsp_buf_ent	buf[];
};

struct scx_deferred_reenq_local {
	struct list_head	node;
	u64			flags;
};

struct scx_sched_pcpu {
	struct scx_sched	*sch;
	u64			flags;	/* protected by rq lock */

	/*
	 * The event counters are in a per-CPU variable to minimize the
	 * accounting overhead. A system-wide view on the event counter is
	 * constructed when requested by scx_bpf_events().
	 */
	struct scx_event_stats	event_stats;

	struct scx_deferred_reenq_local deferred_reenq_local;
	struct scx_dispatch_q	bypass_dsq;
#ifdef CONFIG_EXT_SUB_SCHED
	u32			bypass_host_seq;
#endif

	/* must be the last entry - contains flex array */
	struct scx_dsp_ctx	dsp_ctx;
};

struct scx_sched_pnode {
	struct scx_dispatch_q	global_dsq;
};

struct scx_sched {
	struct sched_ext_ops	ops;
	DECLARE_BITMAP(has_op, SCX_OPI_END);

	/*
	 * Dispatch queues.
	 *
	 * The global DSQ (%SCX_DSQ_GLOBAL) is split per-node for scalability.
	 * This is to avoid live-locking in bypass mode where all tasks are
	 * dispatched to %SCX_DSQ_GLOBAL and all CPUs consume from it. If
	 * per-node split isn't sufficient, it can be further split.
	 */
	struct rhashtable	dsq_hash;
	struct scx_sched_pnode	**pnode;
	struct scx_sched_pcpu __percpu *pcpu;

	u64			slice_dfl;
	u64			bypass_timestamp;
	s32			bypass_depth;

	/* bypass dispatch path enable state, see bypass_dsp_enabled() */
	unsigned long		bypass_dsp_claim;
	atomic_t		bypass_dsp_enable_depth;

	bool			aborting;
	bool			dump_disabled;	/* protected by scx_dump_lock */
	u32			dsp_max_batch;
	s32			level;

	/*
	 * Updates to the following warned bitfields can race causing RMW issues
	 * but it doesn't really matter.
	 */
	bool			warned_zero_slice:1;
	bool			warned_deprecated_rq:1;
	bool			warned_unassoc_progs:1;

	struct list_head	all;

#ifdef CONFIG_EXT_SUB_SCHED
	struct rhash_head	hash_node;

	struct list_head	children;
	struct list_head	sibling;
	struct cgroup		*cgrp;
	char			*cgrp_path;
	struct kset		*sub_kset;

	bool			sub_attached;
#endif	/* CONFIG_EXT_SUB_SCHED */

	/*
	 * The maximum amount of time in jiffies that a task may be runnable
	 * without being scheduled on a CPU. If this timeout is exceeded, it
	 * will trigger scx_error().
	 */
	unsigned long		watchdog_timeout;

	atomic_t		exit_kind;
	struct scx_exit_info	*exit_info;

	struct kobject		kobj;

	struct kthread_worker	*helper;
	struct irq_work		disable_irq_work;
	struct kthread_work	disable_work;
	struct timer_list	bypass_lb_timer;
	struct rcu_work		rcu_work;

	/* all ancestors including self */
	struct scx_sched	*ancestors[];
};

enum scx_wake_flags {
	/* expose select WF_* flags as enums */
	SCX_WAKE_FORK		= WF_FORK,
	SCX_WAKE_TTWU		= WF_TTWU,
	SCX_WAKE_SYNC		= WF_SYNC,
};

enum scx_enq_flags {
	/* expose select ENQUEUE_* flags as enums */
	SCX_ENQ_WAKEUP		= ENQUEUE_WAKEUP,
	SCX_ENQ_HEAD		= ENQUEUE_HEAD,
	SCX_ENQ_CPU_SELECTED	= ENQUEUE_RQ_SELECTED,

	/* high 32bits are SCX specific */

	/*
	 * Set the following to trigger preemption when calling
	 * scx_bpf_dsq_insert() with a local dsq as the target. The slice of the
	 * current task is cleared to zero and the CPU is kicked into the
	 * scheduling path. Implies %SCX_ENQ_HEAD.
	 */
	SCX_ENQ_PREEMPT		= 1LLU << 32,

	/*
	 * The task being enqueued was previously enqueued on a DSQ, but was
	 * removed and is being re-enqueued. See SCX_TASK_REENQ_* flags to find
	 * out why a given task is being reenqueued.
	 */
	SCX_ENQ_REENQ		= 1LLU << 40,

	/*
	 * The task being enqueued is the only task available for the cpu. By
	 * default, ext core keeps executing such tasks but when
	 * %SCX_OPS_ENQ_LAST is specified, they're ops.enqueue()'d with the
	 * %SCX_ENQ_LAST flag set.
	 *
	 * The BPF scheduler is responsible for triggering a follow-up
	 * scheduling event. Otherwise, Execution may stall.
	 */
	SCX_ENQ_LAST		= 1LLU << 41,

	/* high 8 bits are internal */
	__SCX_ENQ_INTERNAL_MASK	= 0xffLLU << 56,

	SCX_ENQ_CLEAR_OPSS	= 1LLU << 56,
	SCX_ENQ_DSQ_PRIQ	= 1LLU << 57,
	SCX_ENQ_NESTED		= 1LLU << 58,
};

enum scx_deq_flags {
	/* expose select DEQUEUE_* flags as enums */
	SCX_DEQ_SLEEP		= DEQUEUE_SLEEP,

	/* high 32bits are SCX specific */

	/*
	 * The generic core-sched layer decided to execute the task even though
	 * it hasn't been dispatched yet. Dequeue from the BPF side.
	 */
	SCX_DEQ_CORE_SCHED_EXEC	= 1LLU << 32,

	/*
	 * The task is being dequeued due to a property change (e.g.,
	 * sched_setaffinity(), sched_setscheduler(), set_user_nice(),
	 * etc.).
	 */
	SCX_DEQ_SCHED_CHANGE	= 1LLU << 33,
};

enum scx_reenq_flags {
	/* low 16bits determine which tasks should be reenqueued */
	SCX_REENQ_ANY		= 1LLU << 0,	/* all tasks */

	__SCX_REENQ_FILTER_MASK	= 0xffffLLU,

	__SCX_REENQ_USER_MASK	= SCX_REENQ_ANY,
};

enum scx_pick_idle_cpu_flags {
	SCX_PICK_IDLE_CORE	= 1LLU << 0,	/* pick a CPU whose SMT siblings are also idle */
	SCX_PICK_IDLE_IN_NODE	= 1LLU << 1,	/* pick a CPU in the same target NUMA node */
};

enum scx_kick_flags {
	/*
	 * Kick the target CPU if idle. Guarantees that the target CPU goes
	 * through at least one full scheduling cycle before going idle. If the
	 * target CPU can be determined to be currently not idle and going to go
	 * through a scheduling cycle before going idle, noop.
	 */
	SCX_KICK_IDLE		= 1LLU << 0,

	/*
	 * Preempt the current task and execute the dispatch path. If the
	 * current task of the target CPU is an SCX task, its ->scx.slice is
	 * cleared to zero before the scheduling path is invoked so that the
	 * task expires and the dispatch path is invoked.
	 */
	SCX_KICK_PREEMPT	= 1LLU << 1,

	/*
	 * The scx_bpf_kick_cpu() call will return after the current SCX task of
	 * the target CPU switches out. This can be used to implement e.g. core
	 * scheduling. This has no effect if the current task on the target CPU
	 * is not on SCX.
	 */
	SCX_KICK_WAIT		= 1LLU << 2,
};

enum scx_tg_flags {
	SCX_TG_ONLINE		= 1U << 0,
	SCX_TG_INITED		= 1U << 1,
};

enum scx_enable_state {
	SCX_ENABLING,
	SCX_ENABLED,
	SCX_DISABLING,
	SCX_DISABLED,
};

static const char *scx_enable_state_str[] = {
	[SCX_ENABLING]		= "enabling",
	[SCX_ENABLED]		= "enabled",
	[SCX_DISABLING]		= "disabling",
	[SCX_DISABLED]		= "disabled",
};

/*
 * Task Ownership State Machine (sched_ext_entity->ops_state)
 *
 * The sched_ext core uses this state machine to track task ownership
 * between the SCX core and the BPF scheduler. This allows the BPF
 * scheduler to dispatch tasks without strict ordering requirements, while
 * the SCX core safely rejects invalid dispatches.
 *
 * State Transitions
 *
 *       .------------> NONE (owned by SCX core)
 *       |               |           ^
 *       |       enqueue |           | direct dispatch
 *       |               v           |
 *       |           QUEUEING -------'
 *       |               |
 *       |       enqueue |
 *       |     completes |
 *       |               v
 *       |            QUEUED (owned by BPF scheduler)
 *       |               |
 *       |      dispatch |
 *       |               |
 *       |               v
 *       |          DISPATCHING
 *       |               |
 *       |      dispatch |
 *       |     completes |
 *       `---------------'
 *
 * State Descriptions
 *
 * - %SCX_OPSS_NONE:
 *     Task is owned by the SCX core. It's either on a run queue, running,
 *     or being manipulated by the core scheduler. The BPF scheduler has no
 *     claim on this task.
 *
 * - %SCX_OPSS_QUEUEING:
 *     Transitional state while transferring a task from the SCX core to
 *     the BPF scheduler. The task's rq lock is held during this state.
 *     Since QUEUEING is both entered and exited under the rq lock, dequeue
 *     can never observe this state (it would be a BUG). When finishing a
 *     dispatch, if the task is still in %SCX_OPSS_QUEUEING the completion
 *     path busy-waits for it to leave this state (via wait_ops_state())
 *     before retrying.
 *
 * - %SCX_OPSS_QUEUED:
 *     Task is owned by the BPF scheduler. It's on a DSQ (dispatch queue)
 *     and the BPF scheduler is responsible for dispatching it. A QSEQ
 *     (queue sequence number) is embedded in this state to detect
 *     dispatch/dequeue races: if a task is dequeued and re-enqueued, the
 *     QSEQ changes and any in-flight dispatch operations targeting the old
 *     QSEQ are safely ignored.
 *
 * - %SCX_OPSS_DISPATCHING:
 *     Transitional state while transferring a task from the BPF scheduler
 *     back to the SCX core. This state indicates the BPF scheduler has
 *     selected the task for execution. When dequeue needs to take the task
 *     off a DSQ and it is still in %SCX_OPSS_DISPATCHING, the dequeue path
 *     busy-waits for it to leave this state (via wait_ops_state()) before
 *     proceeding. Exits to %SCX_OPSS_NONE when dispatch completes.
 *
 * Memory Ordering
 *
 * Transitions out of %SCX_OPSS_QUEUEING and %SCX_OPSS_DISPATCHING into
 * %SCX_OPSS_NONE or %SCX_OPSS_QUEUED must use atomic_long_set_release()
 * and waiters must use atomic_long_read_acquire(). This ensures proper
 * synchronization between concurrent operations.
 *
 * Cross-CPU Task Migration
 *
 * When moving a task in the %SCX_OPSS_DISPATCHING state, we can't simply
 * grab the target CPU's rq lock because a concurrent dequeue might be
 * waiting on %SCX_OPSS_DISPATCHING while holding the source rq lock
 * (deadlock).
 *
 * The sched_ext core uses a "lock dancing" protocol coordinated by
 * p->scx.holding_cpu. When moving a task to a different rq:
 *
 *   1. Verify task can be moved (CPU affinity, migration_disabled, etc.)
 *   2. Set p->scx.holding_cpu to the current CPU
 *   3. Set task state to %SCX_OPSS_NONE; dequeue waits while DISPATCHING
 *      is set, so clearing DISPATCHING first prevents the circular wait
 *      (safe to lock the rq we need)
 *   4. Unlock the current CPU's rq
 *   5. Lock src_rq (where the task currently lives)
 *   6. Verify p->scx.holding_cpu == current CPU, if not, dequeue won the
 *      race (dequeue clears holding_cpu to -1 when it takes the task), in
 *      this case migration is aborted
 *   7. If src_rq == dst_rq: clear holding_cpu and enqueue directly
 *      into dst_rq's local DSQ (no lock swap needed)
 *   8. Otherwise: call move_remote_task_to_local_dsq(), which releases
 *      src_rq, locks dst_rq, and performs the deactivate/activate
 *      migration cycle (dst_rq is held on return)
 *   9. Unlock dst_rq and re-lock the current CPU's rq to restore
 *      the lock state expected by the caller
 *
 * If any verification fails, abort the migration.
 *
 * This state tracking allows the BPF scheduler to try to dispatch any task
 * at any time regardless of its state. The SCX core can safely
 * reject/ignore invalid dispatches, simplifying the BPF scheduler
 * implementation.
 */
enum scx_ops_state {
	SCX_OPSS_NONE,		/* owned by the SCX core */
	SCX_OPSS_QUEUEING,	/* in transit to the BPF scheduler */
	SCX_OPSS_QUEUED,	/* owned by the BPF scheduler */
	SCX_OPSS_DISPATCHING,	/* in transit back to the SCX core */

	/*
	 * QSEQ brands each QUEUED instance so that, when dispatch races
	 * dequeue/requeue, the dispatcher can tell whether it still has a claim
	 * on the task being dispatched.
	 *
	 * As some 32bit archs can't do 64bit store_release/load_acquire,
	 * p->scx.ops_state is atomic_long_t which leaves 30 bits for QSEQ on
	 * 32bit machines. The dispatch race window QSEQ protects is very narrow
	 * and runs with IRQ disabled. 30 bits should be sufficient.
	 */
	SCX_OPSS_QSEQ_SHIFT	= 2,
};

/* Use macros to ensure that the type is unsigned long for the masks */
#define SCX_OPSS_STATE_MASK	((1LU << SCX_OPSS_QSEQ_SHIFT) - 1)
#define SCX_OPSS_QSEQ_MASK	(~SCX_OPSS_STATE_MASK)

extern struct scx_sched __rcu *scx_root;
DECLARE_PER_CPU(struct rq *, scx_locked_rq_state);

/*
 * Return the rq currently locked from an scx callback, or NULL if no rq is
 * locked.
 */
static inline struct rq *scx_locked_rq(void)
{
	return __this_cpu_read(scx_locked_rq_state);
}

static inline bool scx_kf_allowed_if_unlocked(void)
{
	return !current->scx.kf_mask;
}

static inline bool scx_bypassing(struct scx_sched *sch, s32 cpu)
{
	return unlikely(per_cpu_ptr(sch->pcpu, cpu)->flags &
			SCX_SCHED_PCPU_BYPASSING);
}

#ifdef CONFIG_EXT_SUB_SCHED
/**
 * scx_task_sched - Find scx_sched scheduling a task
 * @p: task of interest
 *
 * Return @p's scheduler instance. Must be called with @p's pi_lock or rq lock
 * held.
 */
static inline struct scx_sched *scx_task_sched(const struct task_struct *p)
{
	return rcu_dereference_protected(p->scx.sched,
					 lockdep_is_held(&p->pi_lock) ||
					 lockdep_is_held(__rq_lockp(task_rq(p))));
}

/**
 * scx_task_sched_rcu - Find scx_sched scheduling a task
 * @p: task of interest
 *
 * Return @p's scheduler instance. The returned scx_sched is RCU protected.
 */
static inline struct scx_sched *scx_task_sched_rcu(const struct task_struct *p)
{
	return rcu_dereference_all(p->scx.sched);
}

/**
 * scx_task_on_sched - Is a task on the specified sched?
 * @sch: sched to test against
 * @p: task of interest
 *
 * Returns %true if @p is on @sch, %false otherwise.
 */
static inline bool scx_task_on_sched(struct scx_sched *sch,
				     const struct task_struct *p)
{
	return rcu_access_pointer(p->scx.sched) == sch;
}

/**
 * scx_prog_sched - Find scx_sched associated with a BPF prog
 * @aux: aux passed in from BPF to a kfunc
 *
 * To be called from kfuncs. Return the scheduler instance associated with the
 * BPF program given the implicit kfunc argument aux. The returned scx_sched is
 * RCU protected.
 */
static inline struct scx_sched *scx_prog_sched(const struct bpf_prog_aux *aux)
{
	struct sched_ext_ops *ops;
	struct scx_sched *root;

	ops = bpf_prog_get_assoc_struct_ops(aux);
	if (likely(ops))
		return rcu_dereference_all(ops->priv);

	root = rcu_dereference_all(scx_root);
	if (root) {
		/*
		 * COMPAT-v6.19: Schedulers built before sub-sched support was
		 * introduced may have unassociated non-struct_ops programs.
		 */
		if (!root->ops.sub_attach)
			return root;

		if (!root->warned_unassoc_progs) {
			printk_deferred(KERN_WARNING "sched_ext: Unassociated program %s (id %d)\n",
					aux->name, aux->id);
			root->warned_unassoc_progs = true;
		}
	}

	return NULL;
}
#else	/* CONFIG_EXT_SUB_SCHED */
static inline struct scx_sched *scx_task_sched(const struct task_struct *p)
{
	return rcu_dereference_protected(scx_root,
					 lockdep_is_held(&p->pi_lock) ||
					 lockdep_is_held(__rq_lockp(task_rq(p))));
}

static inline struct scx_sched *scx_task_sched_rcu(const struct task_struct *p)
{
	return rcu_dereference_all(scx_root);
}

static inline bool scx_task_on_sched(struct scx_sched *sch,
				     const struct task_struct *p)
{
	return true;
}

static struct scx_sched *scx_prog_sched(const struct bpf_prog_aux *aux)
{
	return rcu_dereference_all(scx_root);
}
#endif	/* CONFIG_EXT_SUB_SCHED */
