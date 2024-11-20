/* SPDX-License-Identifier: GPL-2.0 */
/*
 * BPF extensible scheduler class: Documentation/scheduler/sched-ext.rst
 *
 * Copyright (c) 2022 Meta Platforms, Inc. and affiliates.
 * Copyright (c) 2022 Tejun Heo <tj@kernel.org>
 * Copyright (c) 2022 David Vernet <dvernet@meta.com>
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
	SCX_OPS_TASK_ITER_BATCH		= 32,
};

enum scx_exit_kind {
	SCX_EXIT_NONE,
	SCX_EXIT_DONE,

	SCX_EXIT_UNREG = 64,	/* user-space initiated unregistration */
	SCX_EXIT_UNREG_BPF,	/* BPF-initiated unregistration */
	SCX_EXIT_UNREG_KERN,	/* kernel-initiated unregistration */
	SCX_EXIT_SYSRQ,		/* requested by 'S' sysrq */

	SCX_EXIT_ERROR = 1024,	/* runtime error, error msg contains details */
	SCX_EXIT_ERROR_BPF,	/* ERROR but triggered through scx_bpf_error() */
	SCX_EXIT_ERROR_STALL,	/* watchdog detected stalled runnable tasks */
};

/*
 * An exit code can be specified when exiting with scx_bpf_exit() or
 * scx_ops_exit(), corresponding to exit_kind UNREG_BPF and UNREG_KERN
 * respectively. The codes are 64bit of the format:
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

	/* Actions */
	SCX_ECODE_ACT_RESTART	= 1LLU << 48,
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
	SCX_OPS_KEEP_BUILTIN_IDLE = 1LLU << 0,

	/*
	 * By default, if there are no other task to run on the CPU, ext core
	 * keeps running the current task even after its slice expires. If this
	 * flag is specified, such tasks are passed to ops.enqueue() with
	 * %SCX_ENQ_LAST. See the comment above %SCX_ENQ_LAST for more info.
	 */
	SCX_OPS_ENQ_LAST	= 1LLU << 1,

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
	SCX_OPS_ENQ_EXITING	= 1LLU << 2,

	/*
	 * If set, only tasks with policy set to SCHED_EXT are attached to
	 * sched_ext. If clear, SCHED_NORMAL tasks are also included.
	 */
	SCX_OPS_SWITCH_PARTIAL	= 1LLU << 3,

	/*
	 * CPU cgroup support flags
	 */
	SCX_OPS_HAS_CGROUP_WEIGHT = 1LLU << 16,	/* cpu.weight */

	SCX_OPS_ALL_FLAGS	= SCX_OPS_KEEP_BUILTIN_IDLE |
				  SCX_OPS_ENQ_LAST |
				  SCX_OPS_ENQ_EXITING |
				  SCX_OPS_SWITCH_PARTIAL |
				  SCX_OPS_HAS_CGROUP_WEIGHT,
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

/* argument container for ops->cgroup_init() */
struct scx_cgroup_init_args {
	/* the weight of the cgroup [1..10000] */
	u32			weight;
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
 * Argument container for ops->cpu_acquire(). Currently empty, but may be
 * expanded in the future.
 */
struct scx_cpu_acquire_args {};

/* argument container for ops->cpu_release() */
struct scx_cpu_release_args {
	/* the reason the CPU was preempted */
	enum scx_cpu_preempt_reason reason;

	/* the task that's going to be scheduled on the CPU */
	struct task_struct	*task;
};

/*
 * Informational context provided to dump operations.
 */
struct scx_dump_ctx {
	enum scx_exit_kind	kind;
	s64			exit_code;
	const char		*reason;
	u64			at_ns;
	u64			at_jiffies;
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
	 * select_cpu - Pick the target CPU for a task which is being woken up
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
	 * enqueue - Enqueue a task on the BPF scheduler
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
	 * dequeue - Remove a task from the BPF scheduler
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
	 * dispatch - Dispatch tasks from the BPF scheduler and/or user DSQs
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
	 * tick - Periodic tick
	 * @p: task running currently
	 *
	 * This operation is called every 1/HZ seconds on CPUs which are
	 * executing an SCX task. Setting @p->scx.slice to 0 will trigger an
	 * immediate dispatch cycle on the CPU.
	 */
	void (*tick)(struct task_struct *p);

	/**
	 * runnable - A task is becoming runnable on its associated CPU
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
	 * running - A task is starting to run on its associated CPU
	 * @p: task starting to run
	 *
	 * See ->runnable() for explanation on the task state notifiers.
	 */
	void (*running)(struct task_struct *p);

	/**
	 * stopping - A task is stopping execution
	 * @p: task stopping to run
	 * @runnable: is task @p still runnable?
	 *
	 * See ->runnable() for explanation on the task state notifiers. If
	 * !@runnable, ->quiescent() will be invoked after this operation
	 * returns.
	 */
	void (*stopping)(struct task_struct *p, bool runnable);

	/**
	 * quiescent - A task is becoming not runnable on its associated CPU
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
	 * yield - Yield CPU
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
	 * core_sched_before - Task ordering for core-sched
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
	 * set_weight - Set task weight
	 * @p: task to set weight for
	 * @weight: new weight [1..10000]
	 *
	 * Update @p's weight to @weight.
	 */
	void (*set_weight)(struct task_struct *p, u32 weight);

	/**
	 * set_cpumask - Set CPU affinity
	 * @p: task to set CPU affinity for
	 * @cpumask: cpumask of cpus that @p can run on
	 *
	 * Update @p's CPU affinity to @cpumask.
	 */
	void (*set_cpumask)(struct task_struct *p,
			    const struct cpumask *cpumask);

	/**
	 * update_idle - Update the idle state of a CPU
	 * @cpu: CPU to udpate the idle state for
	 * @idle: whether entering or exiting the idle state
	 *
	 * This operation is called when @rq's CPU goes or leaves the idle
	 * state. By default, implementing this operation disables the built-in
	 * idle CPU tracking and the following helpers become unavailable:
	 *
	 * - scx_bpf_select_cpu_dfl()
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
	 * cpu_acquire - A CPU is becoming available to the BPF scheduler
	 * @cpu: The CPU being acquired by the BPF scheduler.
	 * @args: Acquire arguments, see the struct definition.
	 *
	 * A CPU that was previously released from the BPF scheduler is now once
	 * again under its control.
	 */
	void (*cpu_acquire)(s32 cpu, struct scx_cpu_acquire_args *args);

	/**
	 * cpu_release - A CPU is taken away from the BPF scheduler
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
	 * init_task - Initialize a task to run in a BPF scheduler
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
	 * exit_task - Exit a previously-running task from the system
	 * @p: task to exit
	 *
	 * @p is exiting or the BPF scheduler is being unloaded. Perform any
	 * necessary cleanup for @p.
	 */
	void (*exit_task)(struct task_struct *p, struct scx_exit_task_args *args);

	/**
	 * enable - Enable BPF scheduling for a task
	 * @p: task to enable BPF scheduling for
	 *
	 * Enable @p for BPF scheduling. enable() is called on @p any time it
	 * enters SCX, and is always paired with a matching disable().
	 */
	void (*enable)(struct task_struct *p);

	/**
	 * disable - Disable BPF scheduling for a task
	 * @p: task to disable BPF scheduling for
	 *
	 * @p is exiting, leaving SCX or the BPF scheduler is being unloaded.
	 * Disable BPF scheduling for @p. A disable() call is always matched
	 * with a prior enable() call.
	 */
	void (*disable)(struct task_struct *p);

	/**
	 * dump - Dump BPF scheduler state on error
	 * @ctx: debug dump context
	 *
	 * Use scx_bpf_dump() to generate BPF scheduler specific debug dump.
	 */
	void (*dump)(struct scx_dump_ctx *ctx);

	/**
	 * dump_cpu - Dump BPF scheduler state for a CPU on error
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
	 * dump_task - Dump BPF scheduler state for a runnable task on error
	 * @ctx: debug dump context
	 * @p: runnable task to generate debug dump for
	 *
	 * Use scx_bpf_dump() to generate BPF scheduler specific debug dump for
	 * @p.
	 */
	void (*dump_task)(struct scx_dump_ctx *ctx, struct task_struct *p);

#ifdef CONFIG_EXT_GROUP_SCHED
	/**
	 * cgroup_init - Initialize a cgroup
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
	 * cgroup_exit - Exit a cgroup
	 * @cgrp: cgroup being exited
	 *
	 * Either the BPF scheduler is being unloaded or @cgrp destroyed, exit
	 * @cgrp for sched_ext. This operation my block.
	 */
	void (*cgroup_exit)(struct cgroup *cgrp);

	/**
	 * cgroup_prep_move - Prepare a task to be moved to a different cgroup
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
	 * cgroup_move - Commit cgroup move
	 * @p: task being moved
	 * @from: cgroup @p is being moved from
	 * @to: cgroup @p is being moved to
	 *
	 * Commit the move. @p is dequeued during this operation.
	 */
	void (*cgroup_move)(struct task_struct *p,
			    struct cgroup *from, struct cgroup *to);

	/**
	 * cgroup_cancel_move - Cancel cgroup move
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
	 * cgroup_set_weight - A cgroup's weight is being changed
	 * @cgrp: cgroup whose weight is being updated
	 * @weight: new weight [1..10000]
	 *
	 * Update @tg's weight to @weight.
	 */
	void (*cgroup_set_weight)(struct cgroup *cgrp, u32 weight);
#endif	/* CONFIG_EXT_GROUP_SCHED */

	/*
	 * All online ops must come before ops.cpu_online().
	 */

	/**
	 * cpu_online - A CPU became online
	 * @cpu: CPU which just came up
	 *
	 * @cpu just came online. @cpu will not call ops.enqueue() or
	 * ops.dispatch(), nor run tasks associated with other CPUs beforehand.
	 */
	void (*cpu_online)(s32 cpu);

	/**
	 * cpu_offline - A CPU is going offline
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
	 * init - Initialize the BPF scheduler
	 */
	s32 (*init)(void);

	/**
	 * exit - Clean up after the BPF scheduler
	 * @info: Exit info
	 *
	 * ops.exit() is also called on ops.init() failure, which is a bit
	 * unusual. This is to allow rich reporting through @info on how
	 * ops.init() failed.
	 */
	void (*exit)(struct scx_exit_info *info);

	/**
	 * dispatch_max_batch - Max nr of tasks that dispatch() can dispatch
	 */
	u32 dispatch_max_batch;

	/**
	 * flags - %SCX_OPS_* flags
	 */
	u64 flags;

	/**
	 * timeout_ms - The maximum amount of time, in milliseconds, that a
	 * runnable task should be able to wait before being scheduled. The
	 * maximum timeout may not exceed the default timeout of 30 seconds.
	 *
	 * Defaults to the maximum allowed timeout value of 30 seconds.
	 */
	u32 timeout_ms;

	/**
	 * exit_dump_len - scx_exit_info.dump buffer length. If 0, the default
	 * value of 32768 is used.
	 */
	u32 exit_dump_len;

	/**
	 * hotplug_seq - A sequence number that may be set by the scheduler to
	 * detect when a hotplug event has occurred during the loading process.
	 * If 0, no detection occurs. Otherwise, the scheduler will fail to
	 * load if the sequence number does not match @scx_hotplug_seq on the
	 * enable path.
	 */
	u64 hotplug_seq;

	/**
	 * name - BPF scheduler's name
	 *
	 * Must be a non-zero valid BPF object name including only isalnum(),
	 * '_' and '.' chars. Shows up in kernel.sched_ext_ops sysctl while the
	 * BPF scheduler is enabled.
	 */
	char name[SCX_OPS_NAME_LEN];
};

enum scx_opi {
	SCX_OPI_BEGIN			= 0,
	SCX_OPI_NORMAL_BEGIN		= 0,
	SCX_OPI_NORMAL_END		= SCX_OP_IDX(cpu_online),
	SCX_OPI_CPU_HOTPLUG_BEGIN	= SCX_OP_IDX(cpu_online),
	SCX_OPI_CPU_HOTPLUG_END		= SCX_OP_IDX(init),
	SCX_OPI_END			= SCX_OP_IDX(init),
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
	 * The task being enqueued was previously enqueued on the current CPU's
	 * %SCX_DSQ_LOCAL, but was removed from it in a call to the
	 * bpf_scx_reenqueue_local() kfunc. If bpf_scx_reenqueue_local() was
	 * invoked in a ->cpu_release() callback, and the task is again
	 * dispatched back to %SCX_LOCAL_DSQ by this current ->enqueue(), the
	 * task will not be scheduled on the CPU until at least the next invocation
	 * of the ->cpu_acquire() callback.
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
};

enum scx_pick_idle_cpu_flags {
	SCX_PICK_IDLE_CORE	= 1LLU << 0,	/* pick a CPU whose SMT siblings are also idle */
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
	 * Wait for the CPU to be rescheduled. The scx_bpf_kick_cpu() call will
	 * return after the target CPU finishes picking the next task.
	 */
	SCX_KICK_WAIT		= 1LLU << 2,
};

enum scx_tg_flags {
	SCX_TG_ONLINE		= 1U << 0,
	SCX_TG_INITED		= 1U << 1,
};

enum scx_ops_enable_state {
	SCX_OPS_ENABLING,
	SCX_OPS_ENABLED,
	SCX_OPS_DISABLING,
	SCX_OPS_DISABLED,
};

static const char *scx_ops_enable_state_str[] = {
	[SCX_OPS_ENABLING]	= "enabling",
	[SCX_OPS_ENABLED]	= "enabled",
	[SCX_OPS_DISABLING]	= "disabling",
	[SCX_OPS_DISABLED]	= "disabled",
};

/*
 * sched_ext_entity->ops_state
 *
 * Used to track the task ownership between the SCX core and the BPF scheduler.
 * State transitions look as follows:
 *
 * NONE -> QUEUEING -> QUEUED -> DISPATCHING
 *   ^              |                 |
 *   |              v                 v
 *   \-------------------------------/
 *
 * QUEUEING and DISPATCHING states can be waited upon. See wait_ops_state() call
 * sites for explanations on the conditions being waited upon and why they are
 * safe. Transitions out of them into NONE or QUEUED must store_release and the
 * waiters should load_acquire.
 *
 * Tracking scx_ops_state enables sched_ext core to reliably determine whether
 * any given task can be dispatched by the BPF scheduler at all times and thus
 * relaxes the requirements on the BPF scheduler. This allows the BPF scheduler
 * to try to dispatch any task anytime regardless of its state as the SCX core
 * can safely reject invalid dispatches.
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

/*
 * During exit, a task may schedule after losing its PIDs. When disabling the
 * BPF scheduler, we need to be able to iterate tasks in every state to
 * guarantee system safety. Maintain a dedicated task list which contains every
 * task between its fork and eventual free.
 */
static DEFINE_SPINLOCK(scx_tasks_lock);
static LIST_HEAD(scx_tasks);

/* ops enable/disable */
static struct kthread_worker *scx_ops_helper;
static DEFINE_MUTEX(scx_ops_enable_mutex);
DEFINE_STATIC_KEY_FALSE(__scx_ops_enabled);
DEFINE_STATIC_PERCPU_RWSEM(scx_fork_rwsem);
static atomic_t scx_ops_enable_state_var = ATOMIC_INIT(SCX_OPS_DISABLED);
static unsigned long scx_in_softlockup;
static atomic_t scx_ops_breather_depth = ATOMIC_INIT(0);
static int scx_ops_bypass_depth;
static bool scx_ops_init_task_enabled;
static bool scx_switching_all;
DEFINE_STATIC_KEY_FALSE(__scx_switched_all);

static struct sched_ext_ops scx_ops;
static bool scx_warned_zero_slice;

static DEFINE_STATIC_KEY_FALSE(scx_ops_enq_last);
static DEFINE_STATIC_KEY_FALSE(scx_ops_enq_exiting);
static DEFINE_STATIC_KEY_FALSE(scx_ops_cpu_preempt);
static DEFINE_STATIC_KEY_FALSE(scx_builtin_idle_enabled);

#ifdef CONFIG_SMP
static DEFINE_STATIC_KEY_FALSE(scx_selcpu_topo_llc);
static DEFINE_STATIC_KEY_FALSE(scx_selcpu_topo_numa);
#endif

static struct static_key_false scx_has_op[SCX_OPI_END] =
	{ [0 ... SCX_OPI_END-1] = STATIC_KEY_FALSE_INIT };

static atomic_t scx_exit_kind = ATOMIC_INIT(SCX_EXIT_DONE);
static struct scx_exit_info *scx_exit_info;

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
 * scx_ops_error().
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

/* idle tracking */
#ifdef CONFIG_SMP
#ifdef CONFIG_CPUMASK_OFFSTACK
#define CL_ALIGNED_IF_ONSTACK
#else
#define CL_ALIGNED_IF_ONSTACK __cacheline_aligned_in_smp
#endif

static struct {
	cpumask_var_t cpu;
	cpumask_var_t smt;
} idle_masks CL_ALIGNED_IF_ONSTACK;

#endif	/* CONFIG_SMP */

/* for %SCX_KICK_WAIT */
static unsigned long __percpu *scx_kick_cpus_pnt_seqs;

/*
 * Direct dispatch marker.
 *
 * Non-NULL values are used for direct dispatch from enqueue path. A valid
 * pointer points to the task currently being enqueued. An ERR_PTR value is used
 * to indicate that direct dispatch has already happened.
 */
static DEFINE_PER_CPU(struct task_struct *, direct_dispatch_task);

/*
 * Dispatch queues.
 *
 * The global DSQ (%SCX_DSQ_GLOBAL) is split per-node for scalability. This is
 * to avoid live-locking in bypass mode where all tasks are dispatched to
 * %SCX_DSQ_GLOBAL and all CPUs consume from it. If per-node split isn't
 * sufficient, it can be further split.
 */
static struct scx_dispatch_q **global_dsqs;

static const struct rhashtable_params dsq_hash_params = {
	.key_len		= 8,
	.key_offset		= offsetof(struct scx_dispatch_q, id),
	.head_offset		= offsetof(struct scx_dispatch_q, hash_node),
};

static struct rhashtable dsq_hash;
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
static struct kobject *scx_root_kobj;

#define CREATE_TRACE_POINTS
#include <trace/events/sched_ext.h>

static void process_ddsp_deferred_locals(struct rq *rq);
static void scx_bpf_kick_cpu(s32 cpu, u64 flags);
static __printf(3, 4) void scx_ops_exit_kind(enum scx_exit_kind kind,
					     s64 exit_code,
					     const char *fmt, ...);

#define scx_ops_error_kind(err, fmt, args...)					\
	scx_ops_exit_kind((err), 0, fmt, ##args)

#define scx_ops_exit(code, fmt, args...)					\
	scx_ops_exit_kind(SCX_EXIT_UNREG_KERN, (code), fmt, ##args)

#define scx_ops_error(fmt, args...)						\
	scx_ops_error_kind(SCX_EXIT_ERROR, fmt, ##args)

#define SCX_HAS_OP(op)	static_branch_likely(&scx_has_op[SCX_OP_IDX(op)])

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

static struct scx_dispatch_q *find_global_dsq(struct task_struct *p)
{
	return global_dsqs[cpu_to_node(task_cpu(p))];
}

static struct scx_dispatch_q *find_user_dsq(u64 dsq_id)
{
	return rhashtable_lookup_fast(&dsq_hash, &dsq_id, dsq_hash_params);
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

#define SCX_CALL_OP(mask, op, args...)						\
do {										\
	if (mask) {								\
		scx_kf_allow(mask);						\
		scx_ops.op(args);						\
		scx_kf_disallow(mask);						\
	} else {								\
		scx_ops.op(args);						\
	}									\
} while (0)

#define SCX_CALL_OP_RET(mask, op, args...)					\
({										\
	__typeof__(scx_ops.op(args)) __ret;					\
	if (mask) {								\
		scx_kf_allow(mask);						\
		__ret = scx_ops.op(args);					\
		scx_kf_disallow(mask);						\
	} else {								\
		__ret = scx_ops.op(args);					\
	}									\
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
#define SCX_CALL_OP_TASK(mask, op, task, args...)				\
do {										\
	BUILD_BUG_ON((mask) & ~__SCX_KF_TERMINAL);				\
	current->scx.kf_tasks[0] = task;					\
	SCX_CALL_OP(mask, op, task, ##args);					\
	current->scx.kf_tasks[0] = NULL;					\
} while (0)

#define SCX_CALL_OP_TASK_RET(mask, op, task, args...)				\
({										\
	__typeof__(scx_ops.op(task, ##args)) __ret;				\
	BUILD_BUG_ON((mask) & ~__SCX_KF_TERMINAL);				\
	current->scx.kf_tasks[0] = task;					\
	__ret = SCX_CALL_OP_RET(mask, op, task, ##args);			\
	current->scx.kf_tasks[0] = NULL;					\
	__ret;									\
})

#define SCX_CALL_OP_2TASKS_RET(mask, op, task0, task1, args...)			\
({										\
	__typeof__(scx_ops.op(task0, task1, ##args)) __ret;			\
	BUILD_BUG_ON((mask) & ~__SCX_KF_TERMINAL);				\
	current->scx.kf_tasks[0] = task0;					\
	current->scx.kf_tasks[1] = task1;					\
	__ret = SCX_CALL_OP_RET(mask, op, task0, task1, ##args);		\
	current->scx.kf_tasks[0] = NULL;					\
	current->scx.kf_tasks[1] = NULL;					\
	__ret;									\
})

/* @mask is constant, always inline to cull unnecessary branches */
static __always_inline bool scx_kf_allowed(u32 mask)
{
	if (unlikely(!(current->scx.kf_mask & mask))) {
		scx_ops_error("kfunc with mask 0x%x called from an operation only allowing 0x%x",
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
		scx_ops_error("cpu_release kfunc called from a nested operation");
		return false;
	}

	if (unlikely(highest_bit(mask) == SCX_KF_DISPATCH &&
		     (current->scx.kf_mask & higher_bits(SCX_KF_DISPATCH)))) {
		scx_ops_error("dispatch kfunc called from a nested operation");
		return false;
	}

	return true;
}

/* see SCX_CALL_OP_TASK() */
static __always_inline bool scx_kf_allowed_on_arg_tasks(u32 mask,
							struct task_struct *p)
{
	if (!scx_kf_allowed(mask))
		return false;

	if (unlikely((p != current->scx.kf_tasks[0] &&
		      p != current->scx.kf_tasks[1]))) {
		scx_ops_error("called on a task not being operated on");
		return false;
	}

	return true;
}

static bool scx_kf_allowed_if_unlocked(void)
{
	return !current->scx.kf_mask;
}

/**
 * nldsq_next_task - Iterate to the next task in a non-local DSQ
 * @dsq: user dsq being interated
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
	struct task_struct		*locked;
	struct rq			*rq;
	struct rq_flags			rf;
	u32				cnt;
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

	spin_lock_irq(&scx_tasks_lock);

	iter->cursor = (struct sched_ext_entity){ .flags = SCX_TASK_CURSOR };
	list_add(&iter->cursor.tasks_node, &scx_tasks);
	iter->locked = NULL;
	iter->cnt = 0;
}

static void __scx_task_iter_rq_unlock(struct scx_task_iter *iter)
{
	if (iter->locked) {
		task_rq_unlock(iter->rq, iter->locked, &iter->rf);
		iter->locked = NULL;
	}
}

/**
 * scx_task_iter_unlock - Unlock rq and scx_tasks_lock held by a task iterator
 * @iter: iterator to unlock
 *
 * If @iter is in the middle of a locked iteration, it may be locking the rq of
 * the task currently being visited in addition to scx_tasks_lock. Unlock both.
 * This function can be safely called anytime during an iteration.
 */
static void scx_task_iter_unlock(struct scx_task_iter *iter)
{
	__scx_task_iter_rq_unlock(iter);
	spin_unlock_irq(&scx_tasks_lock);
}

/**
 * scx_task_iter_relock - Lock scx_tasks_lock released by scx_task_iter_unlock()
 * @iter: iterator to re-lock
 *
 * Re-lock scx_tasks_lock unlocked by scx_task_iter_unlock(). Note that it
 * doesn't re-lock the rq lock. Must be called before other iterator operations.
 */
static void scx_task_iter_relock(struct scx_task_iter *iter)
{
	spin_lock_irq(&scx_tasks_lock);
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
	list_del_init(&iter->cursor.tasks_node);
	scx_task_iter_unlock(iter);
}

/**
 * scx_task_iter_next - Next task
 * @iter: iterator to walk
 *
 * Visit the next task. See scx_task_iter_start() for details. Locks are dropped
 * and re-acquired every %SCX_OPS_TASK_ITER_BATCH iterations to avoid causing
 * stalls by holding scx_tasks_lock for too long.
 */
static struct task_struct *scx_task_iter_next(struct scx_task_iter *iter)
{
	struct list_head *cursor = &iter->cursor.tasks_node;
	struct sched_ext_entity *pos;

	if (!(++iter->cnt % SCX_OPS_TASK_ITER_BATCH)) {
		scx_task_iter_unlock(iter);
		cond_resched();
		scx_task_iter_relock(iter);
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
 * @include_dead: Whether we should include dead tasks in the iteration
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
	iter->locked = p;

	return p;
}

static enum scx_ops_enable_state scx_ops_enable_state(void)
{
	return atomic_read(&scx_ops_enable_state_var);
}

static enum scx_ops_enable_state
scx_ops_set_enable_state(enum scx_ops_enable_state to)
{
	return atomic_xchg(&scx_ops_enable_state_var, to);
}

static bool scx_ops_tryset_enable_state(enum scx_ops_enable_state to,
					enum scx_ops_enable_state from)
{
	int from_v = from;

	return atomic_try_cmpxchg(&scx_ops_enable_state_var, &from_v, to);
}

static bool scx_rq_bypassing(struct rq *rq)
{
	return unlikely(rq->scx.flags & SCX_RQ_BYPASSING);
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

/**
 * ops_cpu_valid - Verify a cpu number
 * @cpu: cpu number which came from a BPF ops
 * @where: extra information reported on error
 *
 * @cpu is a cpu number which came from the BPF scheduler and can be any value.
 * Verify that it is in range and one of the possible cpus. If invalid, trigger
 * an ops error.
 */
static bool ops_cpu_valid(s32 cpu, const char *where)
{
	if (likely(cpu >= 0 && cpu < nr_cpu_ids && cpu_possible(cpu))) {
		return true;
	} else {
		scx_ops_error("invalid CPU %d%s%s", cpu,
			      where ? " " : "", where ?: "");
		return false;
	}
}

/**
 * ops_sanitize_err - Sanitize a -errno value
 * @ops_name: operation to blame on failure
 * @err: -errno value to sanitize
 *
 * Verify @err is a valid -errno. If not, trigger scx_ops_error() and return
 * -%EPROTO. This is necessary because returning a rogue -errno up the chain can
 * cause misbehaviors. For an example, a large negative return from
 * ops.init_task() triggers an oops when passed up the call chain because the
 * value fails IS_ERR() test after being encoded with ERR_PTR() and then is
 * handled as a pointer.
 */
static int ops_sanitize_err(const char *ops_name, s32 err)
{
	if (err < 0 && err >= -MAX_ERRNO)
		return err;

	scx_ops_error("ops.%s() returned an invalid errno %d", ops_name, err);
	return -EPROTO;
}

static void run_deferred(struct rq *rq)
{
	process_ddsp_deferred_locals(rq);
}

#ifdef CONFIG_SMP
static void deferred_bal_cb_workfn(struct rq *rq)
{
	run_deferred(rq);
}
#endif

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

#ifdef CONFIG_SMP
	/*
	 * If in the middle of waking up a task, task_woken_scx() will be called
	 * afterwards which will then run the deferred actions, no need to
	 * schedule anything.
	 */
	if (rq->scx.flags & SCX_RQ_IN_WAKEUP)
		return;

	/*
	 * If in balance, the balance callbacks will be called before rq lock is
	 * released. Schedule one.
	 */
	if (rq->scx.flags & SCX_RQ_IN_BALANCE) {
		queue_balance_callback(rq, &rq->scx.deferred_bal_cb,
				       deferred_bal_cb_workfn);
		return;
	}
#endif
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
	if (SCX_HAS_OP(core_sched_before))
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

static void dispatch_enqueue(struct scx_dispatch_q *dsq, struct task_struct *p,
			     u64 enq_flags)
{
	bool is_local = dsq->id == SCX_DSQ_LOCAL;

	WARN_ON_ONCE(p->scx.dsq || !list_empty(&p->scx.dsq_list.node));
	WARN_ON_ONCE((p->scx.dsq_flags & SCX_TASK_DSQ_ON_PRIQ) ||
		     !RB_EMPTY_NODE(&p->scx.dsq_priq));

	if (!is_local) {
		raw_spin_lock(&dsq->lock);
		if (unlikely(dsq->id == SCX_DSQ_INVALID)) {
			scx_ops_error("attempting to dispatch to a destroyed dsq");
			/* fall back to the global dsq */
			raw_spin_unlock(&dsq->lock);
			dsq = find_global_dsq(p);
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
		scx_ops_error("cannot use vtime ordering for built-in DSQs");
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
			scx_ops_error("DSQ ID 0x%016llx already had FIFO-enqueued tasks",
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
			scx_ops_error("DSQ ID 0x%016llx already had PRIQ-enqueued tasks",
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

static struct scx_dispatch_q *find_dsq_for_dispatch(struct rq *rq, u64 dsq_id,
						    struct task_struct *p)
{
	struct scx_dispatch_q *dsq;

	if (dsq_id == SCX_DSQ_LOCAL)
		return &rq->scx.local_dsq;

	if ((dsq_id & SCX_DSQ_LOCAL_ON) == SCX_DSQ_LOCAL_ON) {
		s32 cpu = dsq_id & SCX_DSQ_LOCAL_CPU_MASK;

		if (!ops_cpu_valid(cpu, "in SCX_DSQ_LOCAL_ON dispatch verdict"))
			return find_global_dsq(p);

		return &cpu_rq(cpu)->scx.local_dsq;
	}

	if (dsq_id == SCX_DSQ_GLOBAL)
		dsq = find_global_dsq(p);
	else
		dsq = find_user_dsq(dsq_id);

	if (unlikely(!dsq)) {
		scx_ops_error("non-existent DSQ 0x%llx for %s[%d]",
			      dsq_id, p->comm, p->pid);
		return find_global_dsq(p);
	}

	return dsq;
}

static void mark_direct_dispatch(struct task_struct *ddsp_task,
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
			scx_ops_error("%s[%d] already direct-dispatched",
				      p->comm, p->pid);
		else
			scx_ops_error("scheduling for %s[%d] but trying to direct-dispatch %s[%d]",
				      ddsp_task->comm, ddsp_task->pid,
				      p->comm, p->pid);
		return;
	}

	WARN_ON_ONCE(p->scx.ddsp_dsq_id != SCX_DSQ_INVALID);
	WARN_ON_ONCE(p->scx.ddsp_enq_flags);

	p->scx.ddsp_dsq_id = dsq_id;
	p->scx.ddsp_enq_flags = enq_flags;
}

static void direct_dispatch(struct task_struct *p, u64 enq_flags)
{
	struct rq *rq = task_rq(p);
	struct scx_dispatch_q *dsq =
		find_dsq_for_dispatch(rq, p->scx.ddsp_dsq_id, p);

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

	dispatch_enqueue(dsq, p, p->scx.ddsp_enq_flags | SCX_ENQ_CLEAR_OPSS);
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

	if (scx_rq_bypassing(rq))
		goto global;

	if (p->scx.ddsp_dsq_id != SCX_DSQ_INVALID)
		goto direct;

	/* see %SCX_OPS_ENQ_EXITING */
	if (!static_branch_unlikely(&scx_ops_enq_exiting) &&
	    unlikely(p->flags & PF_EXITING))
		goto local;

	if (!SCX_HAS_OP(enqueue))
		goto global;

	/* DSQ bypass didn't trigger, enqueue on the BPF scheduler */
	qseq = rq->scx.ops_qseq++ << SCX_OPSS_QSEQ_SHIFT;

	WARN_ON_ONCE(atomic_long_read(&p->scx.ops_state) != SCX_OPSS_NONE);
	atomic_long_set(&p->scx.ops_state, SCX_OPSS_QUEUEING | qseq);

	ddsp_taskp = this_cpu_ptr(&direct_dispatch_task);
	WARN_ON_ONCE(*ddsp_taskp);
	*ddsp_taskp = p;

	SCX_CALL_OP_TASK(SCX_KF_ENQUEUE, enqueue, p, enq_flags);

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
	direct_dispatch(p, enq_flags);
	return;

local:
	/*
	 * For task-ordering, slice refill must be treated as implying the end
	 * of the current slice. Otherwise, the longer @p stays on the CPU, the
	 * higher priority it becomes from scx_prio_less()'s POV.
	 */
	touch_core_sched(rq, p);
	p->scx.slice = SCX_SLICE_DFL;
local_norefill:
	dispatch_enqueue(&rq->scx.local_dsq, p, enq_flags);
	return;

global:
	touch_core_sched(rq, p);	/* see the comment in local: */
	p->scx.slice = SCX_SLICE_DFL;
	dispatch_enqueue(find_global_dsq(p), p, enq_flags);
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
	 * list_add_tail() must be used. scx_ops_bypass() depends on tasks being
	 * appened to the runnable_list.
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

	if (SCX_HAS_OP(runnable) && !task_on_rq_migrating(p))
		SCX_CALL_OP_TASK(SCX_KF_REST, runnable, p, enq_flags);

	if (enq_flags & SCX_ENQ_WAKEUP)
		touch_core_sched(rq, p);

	do_enqueue_task(rq, p, enq_flags, sticky_cpu);
out:
	rq->scx.flags &= ~SCX_RQ_IN_WAKEUP;
}

static void ops_dequeue(struct task_struct *p, u64 deq_flags)
{
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
		if (SCX_HAS_OP(dequeue))
			SCX_CALL_OP_TASK(SCX_KF_REST, dequeue, p, deq_flags);

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
	if (!(p->scx.flags & SCX_TASK_QUEUED)) {
		WARN_ON_ONCE(task_runnable(p));
		return true;
	}

	ops_dequeue(p, deq_flags);

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
	if (SCX_HAS_OP(stopping) && task_current(rq, p)) {
		update_curr_scx(rq);
		SCX_CALL_OP_TASK(SCX_KF_REST, stopping, p, false);
	}

	if (SCX_HAS_OP(quiescent) && !task_on_rq_migrating(p))
		SCX_CALL_OP_TASK(SCX_KF_REST, quiescent, p, deq_flags);

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
	struct task_struct *p = rq->curr;

	if (SCX_HAS_OP(yield))
		SCX_CALL_OP_2TASKS_RET(SCX_KF_REST, yield, p, NULL);
	else
		p->scx.slice = 0;
}

static bool yield_to_task_scx(struct rq *rq, struct task_struct *to)
{
	struct task_struct *from = rq->curr;

	if (SCX_HAS_OP(yield))
		return SCX_CALL_OP_2TASKS_RET(SCX_KF_REST, yield, from, to);
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

#ifdef CONFIG_SMP
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
 */
static bool task_can_run_on_remote_rq(struct task_struct *p, struct rq *rq,
				      bool trigger_error)
{
	int cpu = cpu_of(rq);

	/*
	 * We don't require the BPF scheduler to avoid dispatching to offline
	 * CPUs mostly for convenience but also because CPUs can go offline
	 * between scx_bpf_dsq_insert() calls and here. Trigger error iff the
	 * picked CPU is outside the allowed mask.
	 */
	if (!task_allowed_on_cpu(p, cpu)) {
		if (trigger_error)
			scx_ops_error("SCX_DSQ_LOCAL[_ON] verdict target cpu %d not allowed for %s[%d]",
				      cpu_of(rq), p->comm, p->pid);
		return false;
	}

	if (unlikely(is_migration_disabled(p)))
		return false;

	if (!scx_rq_online(rq))
		return false;

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
#else	/* CONFIG_SMP */
static inline void move_remote_task_to_local_dsq(struct task_struct *p, u64 enq_flags, struct rq *src_rq, struct rq *dst_rq) { WARN_ON_ONCE(1); }
static inline bool task_can_run_on_remote_rq(struct task_struct *p, struct rq *rq, bool trigger_error) { return false; }
static inline bool consume_remote_task(struct rq *this_rq, struct task_struct *p, struct scx_dispatch_q *dsq, struct rq *task_rq) { return false; }
#endif	/* CONFIG_SMP */

/**
 * move_task_between_dsqs() - Move a task from one DSQ to another
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
static struct rq *move_task_between_dsqs(struct task_struct *p, u64 enq_flags,
					 struct scx_dispatch_q *src_dsq,
					 struct scx_dispatch_q *dst_dsq)
{
	struct rq *src_rq = task_rq(p), *dst_rq;

	BUG_ON(src_dsq->id == SCX_DSQ_LOCAL);
	lockdep_assert_held(&src_dsq->lock);
	lockdep_assert_rq_held(src_rq);

	if (dst_dsq->id == SCX_DSQ_LOCAL) {
		dst_rq = container_of(dst_dsq, struct rq, scx.local_dsq);
		if (!task_can_run_on_remote_rq(p, dst_rq, true)) {
			dst_dsq = find_global_dsq(p);
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

		dispatch_enqueue(dst_dsq, p, enq_flags);
	}

	return dst_rq;
}

/*
 * A poorly behaving BPF scheduler can live-lock the system by e.g. incessantly
 * banging on the same DSQ on a large NUMA system to the point where switching
 * to the bypass mode can take a long time. Inject artifical delays while the
 * bypass mode is switching to guarantee timely completion.
 */
static void scx_ops_breather(struct rq *rq)
{
	u64 until;

	lockdep_assert_rq_held(rq);

	if (likely(!atomic_read(&scx_ops_breather_depth)))
		return;

	raw_spin_rq_unlock(rq);

	until = ktime_get_ns() + NSEC_PER_MSEC;

	do {
		int cnt = 1024;
		while (atomic_read(&scx_ops_breather_depth) && --cnt)
			cpu_relax();
	} while (atomic_read(&scx_ops_breather_depth) &&
		 time_before64(ktime_get_ns(), until));

	raw_spin_rq_lock(rq);
}

static bool consume_dispatch_q(struct rq *rq, struct scx_dispatch_q *dsq)
{
	struct task_struct *p;
retry:
	/*
	 * This retry loop can repeatedly race against scx_ops_bypass()
	 * dequeueing tasks from @dsq trying to put the system into the bypass
	 * mode. On some multi-socket machines (e.g. 2x Intel 8480c), this can
	 * live-lock the machine into soft lockups. Give a breather.
	 */
	scx_ops_breather(rq);

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

		if (task_can_run_on_remote_rq(p, rq, false)) {
			if (likely(consume_remote_task(rq, p, dsq, task_rq)))
				return true;
			goto retry;
		}
	}

	raw_spin_unlock(&dsq->lock);
	return false;
}

static bool consume_global_dsq(struct rq *rq)
{
	int node = cpu_to_node(cpu_of(rq));

	return consume_dispatch_q(rq, global_dsqs[node]);
}

/**
 * dispatch_to_local_dsq - Dispatch a task to a local dsq
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
static void dispatch_to_local_dsq(struct rq *rq, struct scx_dispatch_q *dst_dsq,
				  struct task_struct *p, u64 enq_flags)
{
	struct rq *src_rq = task_rq(p);
	struct rq *dst_rq = container_of(dst_dsq, struct rq, scx.local_dsq);

	/*
	 * We're synchronized against dequeue through DISPATCHING. As @p can't
	 * be dequeued, its task_rq and cpus_allowed are stable too.
	 *
	 * If dispatching to @rq that @p is already on, no lock dancing needed.
	 */
	if (rq == src_rq && rq == dst_rq) {
		dispatch_enqueue(dst_dsq, p, enq_flags | SCX_ENQ_CLEAR_OPSS);
		return;
	}

#ifdef CONFIG_SMP
	if (unlikely(!task_can_run_on_remote_rq(p, dst_rq, true))) {
		dispatch_enqueue(find_global_dsq(p), p,
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
	if (rq != src_rq) {
		raw_spin_rq_unlock(rq);
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
			dispatch_enqueue(&dst_rq->scx.local_dsq, p, enq_flags);
		} else {
			move_remote_task_to_local_dsq(p, enq_flags,
						      src_rq, dst_rq);
		}

		/* if the destination CPU is idle, wake it up */
		if (sched_class_above(p->sched_class, dst_rq->curr->sched_class))
			resched_curr(dst_rq);
	}

	/* switch back to @rq lock */
	if (rq != dst_rq) {
		raw_spin_rq_unlock(dst_rq);
		raw_spin_rq_lock(rq);
	}
#else	/* CONFIG_SMP */
	BUG();	/* control can not reach here on UP */
#endif	/* CONFIG_SMP */
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
static void finish_dispatch(struct rq *rq, struct task_struct *p,
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

	dsq = find_dsq_for_dispatch(this_rq(), dsq_id, p);

	if (dsq->id == SCX_DSQ_LOCAL)
		dispatch_to_local_dsq(rq, dsq, p, enq_flags);
	else
		dispatch_enqueue(dsq, p, enq_flags | SCX_ENQ_CLEAR_OPSS);
}

static void flush_dispatch_buf(struct rq *rq)
{
	struct scx_dsp_ctx *dspc = this_cpu_ptr(scx_dsp_ctx);
	u32 u;

	for (u = 0; u < dspc->cursor; u++) {
		struct scx_dsp_buf_ent *ent = &dspc->buf[u];

		finish_dispatch(rq, ent->task, ent->qseq, ent->dsq_id,
				ent->enq_flags);
	}

	dspc->nr_tasks += dspc->cursor;
	dspc->cursor = 0;
}

static int balance_one(struct rq *rq, struct task_struct *prev)
{
	struct scx_dsp_ctx *dspc = this_cpu_ptr(scx_dsp_ctx);
	bool prev_on_scx = prev->sched_class == &ext_sched_class;
	int nr_loops = SCX_DSP_MAX_LOOPS;

	lockdep_assert_rq_held(rq);
	rq->scx.flags |= SCX_RQ_IN_BALANCE;
	rq->scx.flags &= ~(SCX_RQ_BAL_PENDING | SCX_RQ_BAL_KEEP);

	if (static_branch_unlikely(&scx_ops_cpu_preempt) &&
	    unlikely(rq->scx.cpu_released)) {
		/*
		 * If the previous sched_class for the current CPU was not SCX,
		 * notify the BPF scheduler that it again has control of the
		 * core. This callback complements ->cpu_release(), which is
		 * emitted in switch_class().
		 */
		if (SCX_HAS_OP(cpu_acquire))
			SCX_CALL_OP(SCX_KF_REST, cpu_acquire, cpu_of(rq), NULL);
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
		 * See scx_ops_disable_workfn() for the explanation on the
		 * bypassing test.
		 */
		if ((prev->scx.flags & SCX_TASK_QUEUED) &&
		    prev->scx.slice && !scx_rq_bypassing(rq)) {
			rq->scx.flags |= SCX_RQ_BAL_KEEP;
			goto has_tasks;
		}
	}

	/* if there already are tasks to run, nothing to do */
	if (rq->scx.local_dsq.nr)
		goto has_tasks;

	if (consume_global_dsq(rq))
		goto has_tasks;

	if (!SCX_HAS_OP(dispatch) || scx_rq_bypassing(rq) || !scx_rq_online(rq))
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

		SCX_CALL_OP(SCX_KF_DISPATCH, dispatch, cpu_of(rq),
			    prev_on_scx ? prev : NULL);

		flush_dispatch_buf(rq);

		if (rq->scx.local_dsq.nr)
			goto has_tasks;
		if (consume_global_dsq(rq))
			goto has_tasks;

		/*
		 * ops.dispatch() can trap us in this loop by repeatedly
		 * dispatching ineligible tasks. Break out once in a while to
		 * allow the watchdog to run. As IRQ can't be enabled in
		 * balance(), we want to complete this scheduling cycle and then
		 * start a new one. IOW, we want to call resched_curr() on the
		 * next, most likely idle, task, not the current one. Use
		 * scx_bpf_kick_cpu() for deferred kicking.
		 */
		if (unlikely(!--nr_loops)) {
			scx_bpf_kick_cpu(cpu_of(rq), 0);
			break;
		}
	} while (dspc->nr_tasks);

no_tasks:
	/*
	 * Didn't find another task to run. Keep running @prev unless
	 * %SCX_OPS_ENQ_LAST is in effect.
	 */
	if ((prev->scx.flags & SCX_TASK_QUEUED) &&
	    (!static_branch_unlikely(&scx_ops_enq_last) ||
	     scx_rq_bypassing(rq))) {
		rq->scx.flags |= SCX_RQ_BAL_KEEP;
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
		struct scx_dispatch_q *dsq;

		list_del_init(&p->scx.dsq_list.node);

		dsq = find_dsq_for_dispatch(rq, p->scx.ddsp_dsq_id, p);
		if (!WARN_ON_ONCE(dsq->id != SCX_DSQ_LOCAL))
			dispatch_to_local_dsq(rq, dsq, p, p->scx.ddsp_enq_flags);
	}
}

static void set_next_task_scx(struct rq *rq, struct task_struct *p, bool first)
{
	if (p->scx.flags & SCX_TASK_QUEUED) {
		/*
		 * Core-sched might decide to execute @p before it is
		 * dispatched. Call ops_dequeue() to notify the BPF scheduler.
		 */
		ops_dequeue(p, SCX_DEQ_CORE_SCHED_EXEC);
		dispatch_dequeue(rq, p);
	}

	p->se.exec_start = rq_clock_task(rq);

	/* see dequeue_task_scx() on why we skip when !QUEUED */
	if (SCX_HAS_OP(running) && (p->scx.flags & SCX_TASK_QUEUED))
		SCX_CALL_OP_TASK(SCX_KF_REST, running, p);

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
#ifdef CONFIG_SMP
	if (class == &stop_sched_class)
		return SCX_CPU_PREEMPT_STOP;
#endif
	if (class == &dl_sched_class)
		return SCX_CPU_PREEMPT_DL;
	if (class == &rt_sched_class)
		return SCX_CPU_PREEMPT_RT;
	return SCX_CPU_PREEMPT_UNKNOWN;
}

static void switch_class(struct rq *rq, struct task_struct *next)
{
	const struct sched_class *next_class = next->sched_class;

#ifdef CONFIG_SMP
	/*
	 * Pairs with the smp_load_acquire() issued by a CPU in
	 * kick_cpus_irq_workfn() who is waiting for this CPU to perform a
	 * resched.
	 */
	smp_store_release(&rq->scx.pnt_seq, rq->scx.pnt_seq + 1);
#endif
	if (!static_branch_unlikely(&scx_ops_cpu_preempt))
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
		if (SCX_HAS_OP(cpu_release)) {
			struct scx_cpu_release_args args = {
				.reason = preempt_reason_from_class(next_class),
				.task = next,
			};

			SCX_CALL_OP(SCX_KF_CPU_RELEASE,
				    cpu_release, cpu_of(rq), &args);
		}
		rq->scx.cpu_released = true;
	}
}

static void put_prev_task_scx(struct rq *rq, struct task_struct *p,
			      struct task_struct *next)
{
	update_curr_scx(rq);

	/* see dequeue_task_scx() on why we skip when !QUEUED */
	if (SCX_HAS_OP(stopping) && (p->scx.flags & SCX_TASK_QUEUED))
		SCX_CALL_OP_TASK(SCX_KF_REST, stopping, p, true);

	if (p->scx.flags & SCX_TASK_QUEUED) {
		set_task_runnable(rq, p);

		/*
		 * If @p has slice left and is being put, @p is getting
		 * preempted by a higher priority scheduler class or core-sched
		 * forcing a different task. Leave it at the head of the local
		 * DSQ.
		 */
		if (p->scx.slice && !scx_rq_bypassing(rq)) {
			dispatch_enqueue(&rq->scx.local_dsq, p, SCX_ENQ_HEAD);
			return;
		}

		/*
		 * If @p is runnable but we're about to enter a lower
		 * sched_class, %SCX_OPS_ENQ_LAST must be set. Tell
		 * ops.enqueue() that @p is the only one available for this cpu,
		 * which should trigger an explicit follow-up scheduling event.
		 */
		if (sched_class_above(&ext_sched_class, next->sched_class)) {
			WARN_ON_ONCE(!static_branch_unlikely(&scx_ops_enq_last));
			do_enqueue_task(rq, p, SCX_ENQ_LAST, -1);
		} else {
			do_enqueue_task(rq, p, 0, -1);
		}
	}

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
	bool prev_on_scx = prev->sched_class == &ext_sched_class;
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
		if (prev_on_scx) {
			keep_prev = true;
		} else {
			keep_prev = false;
			kick_idle = true;
		}
	} else if (unlikely(keep_prev && !prev_on_scx)) {
		/* only allowed during transitions */
		WARN_ON_ONCE(scx_ops_enable_state() == SCX_OPS_ENABLED);
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
			p->scx.slice = SCX_SLICE_DFL;
	} else {
		p = first_local_task(rq);
		if (!p) {
			if (kick_idle)
				scx_bpf_kick_cpu(cpu_of(rq), SCX_KICK_IDLE);
			return NULL;
		}

		if (unlikely(!p->scx.slice)) {
			if (!scx_rq_bypassing(rq) && !scx_warned_zero_slice) {
				printk_deferred(KERN_WARNING "sched_ext: %s[%d] has zero slice in %s()\n",
						p->comm, p->pid, __func__);
				scx_warned_zero_slice = true;
			}
			p->scx.slice = SCX_SLICE_DFL;
		}
	}

	return p;
}

#ifdef CONFIG_SCHED_CORE
/**
 * scx_prio_less - Task ordering for core-sched
 * @a: task A
 * @b: task B
 *
 * Core-sched is implemented as an additional scheduling layer on top of the
 * usual sched_class'es and needs to find out the expected task ordering. For
 * SCX, core-sched calls this function to interrogate the task ordering.
 *
 * Unless overridden by ops.core_sched_before(), @p->scx.core_sched_at is used
 * to implement the default task ordering. The older the timestamp, the higher
 * prority the task - the global FIFO ordering matching the default scheduling
 * behavior.
 *
 * When ops.core_sched_before() is enabled, @p->scx.core_sched_at is used to
 * implement FIFO ordering within each local DSQ. See pick_task_scx().
 */
bool scx_prio_less(const struct task_struct *a, const struct task_struct *b,
		   bool in_fi)
{
	/*
	 * The const qualifiers are dropped from task_struct pointers when
	 * calling ops.core_sched_before(). Accesses are controlled by the
	 * verifier.
	 */
	if (SCX_HAS_OP(core_sched_before) && !scx_rq_bypassing(task_rq(a)))
		return SCX_CALL_OP_2TASKS_RET(SCX_KF_REST, core_sched_before,
					      (struct task_struct *)a,
					      (struct task_struct *)b);
	else
		return time_after64(a->scx.core_sched_at, b->scx.core_sched_at);
}
#endif	/* CONFIG_SCHED_CORE */

#ifdef CONFIG_SMP

static bool test_and_clear_cpu_idle(int cpu)
{
#ifdef CONFIG_SCHED_SMT
	/*
	 * SMT mask should be cleared whether we can claim @cpu or not. The SMT
	 * cluster is not wholly idle either way. This also prevents
	 * scx_pick_idle_cpu() from getting caught in an infinite loop.
	 */
	if (sched_smt_active()) {
		const struct cpumask *smt = cpu_smt_mask(cpu);

		/*
		 * If offline, @cpu is not its own sibling and
		 * scx_pick_idle_cpu() can get caught in an infinite loop as
		 * @cpu is never cleared from idle_masks.smt. Ensure that @cpu
		 * is eventually cleared.
		 */
		if (cpumask_intersects(smt, idle_masks.smt))
			cpumask_andnot(idle_masks.smt, idle_masks.smt, smt);
		else if (cpumask_test_cpu(cpu, idle_masks.smt))
			__cpumask_clear_cpu(cpu, idle_masks.smt);
	}
#endif
	return cpumask_test_and_clear_cpu(cpu, idle_masks.cpu);
}

static s32 scx_pick_idle_cpu(const struct cpumask *cpus_allowed, u64 flags)
{
	int cpu;

retry:
	if (sched_smt_active()) {
		cpu = cpumask_any_and_distribute(idle_masks.smt, cpus_allowed);
		if (cpu < nr_cpu_ids)
			goto found;

		if (flags & SCX_PICK_IDLE_CORE)
			return -EBUSY;
	}

	cpu = cpumask_any_and_distribute(idle_masks.cpu, cpus_allowed);
	if (cpu >= nr_cpu_ids)
		return -EBUSY;

found:
	if (test_and_clear_cpu_idle(cpu))
		return cpu;
	else
		goto retry;
}

/*
 * Return the amount of CPUs in the same LLC domain of @cpu (or zero if the LLC
 * domain is not defined).
 */
static unsigned int llc_weight(s32 cpu)
{
	struct sched_domain *sd;

	sd = rcu_dereference(per_cpu(sd_llc, cpu));
	if (!sd)
		return 0;

	return sd->span_weight;
}

/*
 * Return the cpumask representing the LLC domain of @cpu (or NULL if the LLC
 * domain is not defined).
 */
static struct cpumask *llc_span(s32 cpu)
{
	struct sched_domain *sd;

	sd = rcu_dereference(per_cpu(sd_llc, cpu));
	if (!sd)
		return 0;

	return sched_domain_span(sd);
}

/*
 * Return the amount of CPUs in the same NUMA domain of @cpu (or zero if the
 * NUMA domain is not defined).
 */
static unsigned int numa_weight(s32 cpu)
{
	struct sched_domain *sd;
	struct sched_group *sg;

	sd = rcu_dereference(per_cpu(sd_numa, cpu));
	if (!sd)
		return 0;
	sg = sd->groups;
	if (!sg)
		return 0;

	return sg->group_weight;
}

/*
 * Return the cpumask representing the NUMA domain of @cpu (or NULL if the NUMA
 * domain is not defined).
 */
static struct cpumask *numa_span(s32 cpu)
{
	struct sched_domain *sd;
	struct sched_group *sg;

	sd = rcu_dereference(per_cpu(sd_numa, cpu));
	if (!sd)
		return NULL;
	sg = sd->groups;
	if (!sg)
		return NULL;

	return sched_group_span(sg);
}

/*
 * Return true if the LLC domains do not perfectly overlap with the NUMA
 * domains, false otherwise.
 */
static bool llc_numa_mismatch(void)
{
	int cpu;

	/*
	 * We need to scan all online CPUs to verify whether their scheduling
	 * domains overlap.
	 *
	 * While it is rare to encounter architectures with asymmetric NUMA
	 * topologies, CPU hotplugging or virtualized environments can result
	 * in asymmetric configurations.
	 *
	 * For example:
	 *
	 *  NUMA 0:
	 *    - LLC 0: cpu0..cpu7
	 *    - LLC 1: cpu8..cpu15 [offline]
	 *
	 *  NUMA 1:
	 *    - LLC 0: cpu16..cpu23
	 *    - LLC 1: cpu24..cpu31
	 *
	 * In this case, if we only check the first online CPU (cpu0), we might
	 * incorrectly assume that the LLC and NUMA domains are fully
	 * overlapping, which is incorrect (as NUMA 1 has two distinct LLC
	 * domains).
	 */
	for_each_online_cpu(cpu)
		if (llc_weight(cpu) != numa_weight(cpu))
			return true;

	return false;
}

/*
 * Initialize topology-aware scheduling.
 *
 * Detect if the system has multiple LLC or multiple NUMA domains and enable
 * cache-aware / NUMA-aware scheduling optimizations in the default CPU idle
 * selection policy.
 *
 * Assumption: the kernel's internal topology representation assumes that each
 * CPU belongs to a single LLC domain, and that each LLC domain is entirely
 * contained within a single NUMA node.
 */
static void update_selcpu_topology(void)
{
	bool enable_llc = false, enable_numa = false;
	unsigned int nr_cpus;
	s32 cpu = cpumask_first(cpu_online_mask);

	/*
	 * Enable LLC domain optimization only when there are multiple LLC
	 * domains among the online CPUs. If all online CPUs are part of a
	 * single LLC domain, the idle CPU selection logic can choose any
	 * online CPU without bias.
	 *
	 * Note that it is sufficient to check the LLC domain of the first
	 * online CPU to determine whether a single LLC domain includes all
	 * CPUs.
	 */
	rcu_read_lock();
	nr_cpus = llc_weight(cpu);
	if (nr_cpus > 0) {
		if (nr_cpus < num_online_cpus())
			enable_llc = true;
		pr_debug("sched_ext: LLC=%*pb weight=%u\n",
			 cpumask_pr_args(llc_span(cpu)), llc_weight(cpu));
	}

	/*
	 * Enable NUMA optimization only when there are multiple NUMA domains
	 * among the online CPUs and the NUMA domains don't perfectly overlaps
	 * with the LLC domains.
	 *
	 * If all CPUs belong to the same NUMA node and the same LLC domain,
	 * enabling both NUMA and LLC optimizations is unnecessary, as checking
	 * for an idle CPU in the same domain twice is redundant.
	 */
	nr_cpus = numa_weight(cpu);
	if (nr_cpus > 0) {
		if (nr_cpus < num_online_cpus() && llc_numa_mismatch())
			enable_numa = true;
		pr_debug("sched_ext: NUMA=%*pb weight=%u\n",
			 cpumask_pr_args(numa_span(cpu)), numa_weight(cpu));
	}
	rcu_read_unlock();

	pr_debug("sched_ext: LLC idle selection %s\n",
		 enable_llc ? "enabled" : "disabled");
	pr_debug("sched_ext: NUMA idle selection %s\n",
		 enable_numa ? "enabled" : "disabled");

	if (enable_llc)
		static_branch_enable_cpuslocked(&scx_selcpu_topo_llc);
	else
		static_branch_disable_cpuslocked(&scx_selcpu_topo_llc);
	if (enable_numa)
		static_branch_enable_cpuslocked(&scx_selcpu_topo_numa);
	else
		static_branch_disable_cpuslocked(&scx_selcpu_topo_numa);
}

/*
 * Built-in CPU idle selection policy:
 *
 * 1. Prioritize full-idle cores:
 *   - always prioritize CPUs from fully idle cores (both logical CPUs are
 *     idle) to avoid interference caused by SMT.
 *
 * 2. Reuse the same CPU:
 *   - prefer the last used CPU to take advantage of cached data (L1, L2) and
 *     branch prediction optimizations.
 *
 * 3. Pick a CPU within the same LLC (Last-Level Cache):
 *   - if the above conditions aren't met, pick a CPU that shares the same LLC
 *     to maintain cache locality.
 *
 * 4. Pick a CPU within the same NUMA node, if enabled:
 *   - choose a CPU from the same NUMA node to reduce memory access latency.
 *
 * Step 3 and 4 are performed only if the system has, respectively, multiple
 * LLC domains / multiple NUMA nodes (see scx_selcpu_topo_llc and
 * scx_selcpu_topo_numa).
 *
 * NOTE: tasks that can only run on 1 CPU are excluded by this logic, because
 * we never call ops.select_cpu() for them, see select_task_rq().
 */
static s32 scx_select_cpu_dfl(struct task_struct *p, s32 prev_cpu,
			      u64 wake_flags, bool *found)
{
	const struct cpumask *llc_cpus = NULL;
	const struct cpumask *numa_cpus = NULL;
	s32 cpu;

	*found = false;

	/*
	 * This is necessary to protect llc_cpus.
	 */
	rcu_read_lock();

	/*
	 * Determine the scheduling domain only if the task is allowed to run
	 * on all CPUs.
	 *
	 * This is done primarily for efficiency, as it avoids the overhead of
	 * updating a cpumask every time we need to select an idle CPU (which
	 * can be costly in large SMP systems), but it also aligns logically:
	 * if a task's scheduling domain is restricted by user-space (through
	 * CPU affinity), the task will simply use the flat scheduling domain
	 * defined by user-space.
	 */
	if (p->nr_cpus_allowed >= num_possible_cpus()) {
		if (static_branch_maybe(CONFIG_NUMA, &scx_selcpu_topo_numa))
			numa_cpus = numa_span(prev_cpu);

		if (static_branch_maybe(CONFIG_SCHED_MC, &scx_selcpu_topo_llc))
			llc_cpus = llc_span(prev_cpu);
	}

	/*
	 * If WAKE_SYNC, try to migrate the wakee to the waker's CPU.
	 */
	if (wake_flags & SCX_WAKE_SYNC) {
		cpu = smp_processor_id();

		/*
		 * If the waker's CPU is cache affine and prev_cpu is idle,
		 * then avoid a migration.
		 */
		if (cpus_share_cache(cpu, prev_cpu) &&
		    test_and_clear_cpu_idle(prev_cpu)) {
			cpu = prev_cpu;
			goto cpu_found;
		}

		/*
		 * If the waker's local DSQ is empty, and the system is under
		 * utilized, try to wake up @p to the local DSQ of the waker.
		 *
		 * Checking only for an empty local DSQ is insufficient as it
		 * could give the wakee an unfair advantage when the system is
		 * oversaturated.
		 *
		 * Checking only for the presence of idle CPUs is also
		 * insufficient as the local DSQ of the waker could have tasks
		 * piled up on it even if there is an idle core elsewhere on
		 * the system.
		 */
		if (!cpumask_empty(idle_masks.cpu) &&
		    !(current->flags & PF_EXITING) &&
		    cpu_rq(cpu)->scx.local_dsq.nr == 0) {
			if (cpumask_test_cpu(cpu, p->cpus_ptr))
				goto cpu_found;
		}
	}

	/*
	 * If CPU has SMT, any wholly idle CPU is likely a better pick than
	 * partially idle @prev_cpu.
	 */
	if (sched_smt_active()) {
		/*
		 * Keep using @prev_cpu if it's part of a fully idle core.
		 */
		if (cpumask_test_cpu(prev_cpu, idle_masks.smt) &&
		    test_and_clear_cpu_idle(prev_cpu)) {
			cpu = prev_cpu;
			goto cpu_found;
		}

		/*
		 * Search for any fully idle core in the same LLC domain.
		 */
		if (llc_cpus) {
			cpu = scx_pick_idle_cpu(llc_cpus, SCX_PICK_IDLE_CORE);
			if (cpu >= 0)
				goto cpu_found;
		}

		/*
		 * Search for any fully idle core in the same NUMA node.
		 */
		if (numa_cpus) {
			cpu = scx_pick_idle_cpu(numa_cpus, SCX_PICK_IDLE_CORE);
			if (cpu >= 0)
				goto cpu_found;
		}

		/*
		 * Search for any full idle core usable by the task.
		 */
		cpu = scx_pick_idle_cpu(p->cpus_ptr, SCX_PICK_IDLE_CORE);
		if (cpu >= 0)
			goto cpu_found;
	}

	/*
	 * Use @prev_cpu if it's idle.
	 */
	if (test_and_clear_cpu_idle(prev_cpu)) {
		cpu = prev_cpu;
		goto cpu_found;
	}

	/*
	 * Search for any idle CPU in the same LLC domain.
	 */
	if (llc_cpus) {
		cpu = scx_pick_idle_cpu(llc_cpus, 0);
		if (cpu >= 0)
			goto cpu_found;
	}

	/*
	 * Search for any idle CPU in the same NUMA node.
	 */
	if (numa_cpus) {
		cpu = scx_pick_idle_cpu(numa_cpus, 0);
		if (cpu >= 0)
			goto cpu_found;
	}

	/*
	 * Search for any idle CPU usable by the task.
	 */
	cpu = scx_pick_idle_cpu(p->cpus_ptr, 0);
	if (cpu >= 0)
		goto cpu_found;

	rcu_read_unlock();
	return prev_cpu;

cpu_found:
	rcu_read_unlock();

	*found = true;
	return cpu;
}

static int select_task_rq_scx(struct task_struct *p, int prev_cpu, int wake_flags)
{
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

	if (SCX_HAS_OP(select_cpu) && !scx_rq_bypassing(task_rq(p))) {
		s32 cpu;
		struct task_struct **ddsp_taskp;

		ddsp_taskp = this_cpu_ptr(&direct_dispatch_task);
		WARN_ON_ONCE(*ddsp_taskp);
		*ddsp_taskp = p;

		cpu = SCX_CALL_OP_TASK_RET(SCX_KF_ENQUEUE | SCX_KF_SELECT_CPU,
					   select_cpu, p, prev_cpu, wake_flags);
		*ddsp_taskp = NULL;
		if (ops_cpu_valid(cpu, "from ops.select_cpu()"))
			return cpu;
		else
			return prev_cpu;
	} else {
		bool found;
		s32 cpu;

		cpu = scx_select_cpu_dfl(p, prev_cpu, wake_flags, &found);
		if (found) {
			p->scx.slice = SCX_SLICE_DFL;
			p->scx.ddsp_dsq_id = SCX_DSQ_LOCAL;
		}
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
	set_cpus_allowed_common(p, ac);

	/*
	 * The effective cpumask is stored in @p->cpus_ptr which may temporarily
	 * differ from the configured one in @p->cpus_mask. Always tell the bpf
	 * scheduler the effective one.
	 *
	 * Fine-grained memory write control is enforced by BPF making the const
	 * designation pointless. Cast it away when calling the operation.
	 */
	if (SCX_HAS_OP(set_cpumask))
		SCX_CALL_OP_TASK(SCX_KF_REST, set_cpumask, p,
				 (struct cpumask *)p->cpus_ptr);
}

static void reset_idle_masks(void)
{
	/*
	 * Consider all online cpus idle. Should converge to the actual state
	 * quickly.
	 */
	cpumask_copy(idle_masks.cpu, cpu_online_mask);
	cpumask_copy(idle_masks.smt, cpu_online_mask);
}

void __scx_update_idle(struct rq *rq, bool idle)
{
	int cpu = cpu_of(rq);

	if (SCX_HAS_OP(update_idle) && !scx_rq_bypassing(rq)) {
		SCX_CALL_OP(SCX_KF_REST, update_idle, cpu_of(rq), idle);
		if (!static_branch_unlikely(&scx_builtin_idle_enabled))
			return;
	}

	if (idle)
		cpumask_set_cpu(cpu, idle_masks.cpu);
	else
		cpumask_clear_cpu(cpu, idle_masks.cpu);

#ifdef CONFIG_SCHED_SMT
	if (sched_smt_active()) {
		const struct cpumask *smt = cpu_smt_mask(cpu);

		if (idle) {
			/*
			 * idle_masks.smt handling is racy but that's fine as
			 * it's only for optimization and self-correcting.
			 */
			for_each_cpu(cpu, smt) {
				if (!cpumask_test_cpu(cpu, idle_masks.cpu))
					return;
			}
			cpumask_or(idle_masks.smt, idle_masks.smt, smt);
		} else {
			cpumask_andnot(idle_masks.smt, idle_masks.smt, smt);
		}
	}
#endif
}

static void handle_hotplug(struct rq *rq, bool online)
{
	int cpu = cpu_of(rq);

	atomic_long_inc(&scx_hotplug_seq);

	if (scx_enabled())
		update_selcpu_topology();

	if (online && SCX_HAS_OP(cpu_online))
		SCX_CALL_OP(SCX_KF_UNLOCKED, cpu_online, cpu);
	else if (!online && SCX_HAS_OP(cpu_offline))
		SCX_CALL_OP(SCX_KF_UNLOCKED, cpu_offline, cpu);
	else
		scx_ops_exit(SCX_ECODE_ACT_RESTART | SCX_ECODE_RSN_HOTPLUG,
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

#else	/* CONFIG_SMP */

static bool test_and_clear_cpu_idle(int cpu) { return false; }
static s32 scx_pick_idle_cpu(const struct cpumask *cpus_allowed, u64 flags) { return -EBUSY; }
static void reset_idle_masks(void) {}

#endif	/* CONFIG_SMP */

static bool check_rq_for_timeouts(struct rq *rq)
{
	struct task_struct *p;
	struct rq_flags rf;
	bool timed_out = false;

	rq_lock_irqsave(rq, &rf);
	list_for_each_entry(p, &rq->scx.runnable_list, scx.runnable_node) {
		unsigned long last_runnable = p->scx.runnable_at;

		if (unlikely(time_after(jiffies,
					last_runnable + scx_watchdog_timeout))) {
			u32 dur_ms = jiffies_to_msecs(jiffies - last_runnable);

			scx_ops_error_kind(SCX_EXIT_ERROR_STALL,
					   "%s[%d] failed to run for %u.%03us",
					   p->comm, p->pid,
					   dur_ms / 1000, dur_ms % 1000);
			timed_out = true;
			break;
		}
	}
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
	unsigned long last_check;

	if (!scx_enabled())
		return;

	last_check = READ_ONCE(scx_watchdog_timestamp);
	if (unlikely(time_after(jiffies,
				last_check + READ_ONCE(scx_watchdog_timeout)))) {
		u32 dur_ms = jiffies_to_msecs(jiffies - last_check);

		scx_ops_error_kind(SCX_EXIT_ERROR_STALL,
				   "watchdog failed to check in for %u.%03us",
				   dur_ms / 1000, dur_ms % 1000);
	}

	update_other_load_avgs(rq);
}

static void task_tick_scx(struct rq *rq, struct task_struct *curr, int queued)
{
	update_curr_scx(rq);

	/*
	 * While disabling, always resched and refresh core-sched timestamp as
	 * we can't trust the slice management or ops.core_sched_before().
	 */
	if (scx_rq_bypassing(rq)) {
		curr->scx.slice = 0;
		touch_core_sched(rq, curr);
	} else if (SCX_HAS_OP(tick)) {
		SCX_CALL_OP(SCX_KF_REST, tick, curr);
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

static int scx_ops_init_task(struct task_struct *p, struct task_group *tg, bool fork)
{
	int ret;

	p->scx.disallow = false;

	if (SCX_HAS_OP(init_task)) {
		struct scx_init_task_args args = {
			SCX_INIT_TASK_ARGS_CGROUP(tg)
			.fork = fork,
		};

		ret = SCX_CALL_OP_RET(SCX_KF_UNLOCKED, init_task, p, &args);
		if (unlikely(ret)) {
			ret = ops_sanitize_err("init_task", ret);
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
			scx_ops_error("ops.init_task() set task->scx.disallow for %s[%d] during fork",
				      p->comm, p->pid);
		}
	}

	p->scx.flags |= SCX_TASK_RESET_RUNNABLE_AT;
	return 0;
}

static void scx_ops_enable_task(struct task_struct *p)
{
	u32 weight;

	lockdep_assert_rq_held(task_rq(p));

	/*
	 * Set the weight before calling ops.enable() so that the scheduler
	 * doesn't see a stale value if they inspect the task struct.
	 */
	if (task_has_idle_policy(p))
		weight = WEIGHT_IDLEPRIO;
	else
		weight = sched_prio_to_weight[p->static_prio - MAX_RT_PRIO];

	p->scx.weight = sched_weight_to_cgroup(weight);

	if (SCX_HAS_OP(enable))
		SCX_CALL_OP_TASK(SCX_KF_REST, enable, p);
	scx_set_task_state(p, SCX_TASK_ENABLED);

	if (SCX_HAS_OP(set_weight))
		SCX_CALL_OP_TASK(SCX_KF_REST, set_weight, p, p->scx.weight);
}

static void scx_ops_disable_task(struct task_struct *p)
{
	lockdep_assert_rq_held(task_rq(p));
	WARN_ON_ONCE(scx_get_task_state(p) != SCX_TASK_ENABLED);

	if (SCX_HAS_OP(disable))
		SCX_CALL_OP(SCX_KF_REST, disable, p);
	scx_set_task_state(p, SCX_TASK_READY);
}

static void scx_ops_exit_task(struct task_struct *p)
{
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
		scx_ops_disable_task(p);
		break;
	default:
		WARN_ON_ONCE(true);
		return;
	}

	if (SCX_HAS_OP(exit_task))
		SCX_CALL_OP(SCX_KF_REST, exit_task, p, &args);
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

	if (scx_ops_init_task_enabled)
		return scx_ops_init_task(p, task_group(p), true);
	else
		return 0;
}

void scx_post_fork(struct task_struct *p)
{
	if (scx_ops_init_task_enabled) {
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
			scx_ops_enable_task(p);
			task_rq_unlock(rq, p, &rf);
		}
	}

	spin_lock_irq(&scx_tasks_lock);
	list_add_tail(&p->scx.tasks_node, &scx_tasks);
	spin_unlock_irq(&scx_tasks_lock);

	percpu_up_read(&scx_fork_rwsem);
}

void scx_cancel_fork(struct task_struct *p)
{
	if (scx_enabled()) {
		struct rq *rq;
		struct rq_flags rf;

		rq = task_rq_lock(p, &rf);
		WARN_ON_ONCE(scx_get_task_state(p) >= SCX_TASK_READY);
		scx_ops_exit_task(p);
		task_rq_unlock(rq, p, &rf);
	}

	percpu_up_read(&scx_fork_rwsem);
}

void sched_ext_free(struct task_struct *p)
{
	unsigned long flags;

	spin_lock_irqsave(&scx_tasks_lock, flags);
	list_del_init(&p->scx.tasks_node);
	spin_unlock_irqrestore(&scx_tasks_lock, flags);

	/*
	 * @p is off scx_tasks and wholly ours. scx_ops_enable()'s READY ->
	 * ENABLED transitions can't race us. Disable ops for @p.
	 */
	if (scx_get_task_state(p) != SCX_TASK_NONE) {
		struct rq_flags rf;
		struct rq *rq;

		rq = task_rq_lock(p, &rf);
		scx_ops_exit_task(p);
		task_rq_unlock(rq, p, &rf);
	}
}

static void reweight_task_scx(struct rq *rq, struct task_struct *p,
			      const struct load_weight *lw)
{
	lockdep_assert_rq_held(task_rq(p));

	p->scx.weight = sched_weight_to_cgroup(scale_load_down(lw->weight));
	if (SCX_HAS_OP(set_weight))
		SCX_CALL_OP_TASK(SCX_KF_REST, set_weight, p, p->scx.weight);
}

static void prio_changed_scx(struct rq *rq, struct task_struct *p, int oldprio)
{
}

static void switching_to_scx(struct rq *rq, struct task_struct *p)
{
	scx_ops_enable_task(p);

	/*
	 * set_cpus_allowed_scx() is not called while @p is associated with a
	 * different scheduler class. Keep the BPF scheduler up-to-date.
	 */
	if (SCX_HAS_OP(set_cpumask))
		SCX_CALL_OP_TASK(SCX_KF_REST, set_cpumask, p,
				 (struct cpumask *)p->cpus_ptr);
}

static void switched_from_scx(struct rq *rq, struct task_struct *p)
{
	scx_ops_disable_task(p);
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

DEFINE_STATIC_PERCPU_RWSEM(scx_cgroup_rwsem);
static bool scx_cgroup_enabled;
static bool cgroup_warned_missing_weight;
static bool cgroup_warned_missing_idle;

static void scx_cgroup_warn_missing_weight(struct task_group *tg)
{
	if (scx_ops_enable_state() == SCX_OPS_DISABLED ||
	    cgroup_warned_missing_weight)
		return;

	if ((scx_ops.flags & SCX_OPS_HAS_CGROUP_WEIGHT) || !tg->css.parent)
		return;

	pr_warn("sched_ext: \"%s\" does not implement cgroup cpu.weight\n",
		scx_ops.name);
	cgroup_warned_missing_weight = true;
}

static void scx_cgroup_warn_missing_idle(struct task_group *tg)
{
	if (!scx_cgroup_enabled || cgroup_warned_missing_idle)
		return;

	if (!tg->idle)
		return;

	pr_warn("sched_ext: \"%s\" does not implement cgroup cpu.idle\n",
		scx_ops.name);
	cgroup_warned_missing_idle = true;
}

int scx_tg_online(struct task_group *tg)
{
	int ret = 0;

	WARN_ON_ONCE(tg->scx_flags & (SCX_TG_ONLINE | SCX_TG_INITED));

	percpu_down_read(&scx_cgroup_rwsem);

	scx_cgroup_warn_missing_weight(tg);

	if (scx_cgroup_enabled) {
		if (SCX_HAS_OP(cgroup_init)) {
			struct scx_cgroup_init_args args =
				{ .weight = tg->scx_weight };

			ret = SCX_CALL_OP_RET(SCX_KF_UNLOCKED, cgroup_init,
					      tg->css.cgroup, &args);
			if (ret)
				ret = ops_sanitize_err("cgroup_init", ret);
		}
		if (ret == 0)
			tg->scx_flags |= SCX_TG_ONLINE | SCX_TG_INITED;
	} else {
		tg->scx_flags |= SCX_TG_ONLINE;
	}

	percpu_up_read(&scx_cgroup_rwsem);
	return ret;
}

void scx_tg_offline(struct task_group *tg)
{
	WARN_ON_ONCE(!(tg->scx_flags & SCX_TG_ONLINE));

	percpu_down_read(&scx_cgroup_rwsem);

	if (SCX_HAS_OP(cgroup_exit) && (tg->scx_flags & SCX_TG_INITED))
		SCX_CALL_OP(SCX_KF_UNLOCKED, cgroup_exit, tg->css.cgroup);
	tg->scx_flags &= ~(SCX_TG_ONLINE | SCX_TG_INITED);

	percpu_up_read(&scx_cgroup_rwsem);
}

int scx_cgroup_can_attach(struct cgroup_taskset *tset)
{
	struct cgroup_subsys_state *css;
	struct task_struct *p;
	int ret;

	/* released in scx_finish/cancel_attach() */
	percpu_down_read(&scx_cgroup_rwsem);

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

		if (SCX_HAS_OP(cgroup_prep_move)) {
			ret = SCX_CALL_OP_RET(SCX_KF_UNLOCKED, cgroup_prep_move,
					      p, from, css->cgroup);
			if (ret)
				goto err;
		}

		p->scx.cgrp_moving_from = from;
	}

	return 0;

err:
	cgroup_taskset_for_each(p, css, tset) {
		if (SCX_HAS_OP(cgroup_cancel_move) && p->scx.cgrp_moving_from)
			SCX_CALL_OP(SCX_KF_UNLOCKED, cgroup_cancel_move, p,
				    p->scx.cgrp_moving_from, css->cgroup);
		p->scx.cgrp_moving_from = NULL;
	}

	percpu_up_read(&scx_cgroup_rwsem);
	return ops_sanitize_err("cgroup_prep_move", ret);
}

void scx_move_task(struct task_struct *p)
{
	if (!scx_cgroup_enabled)
		return;

	/*
	 * We're called from sched_move_task() which handles both cgroup and
	 * autogroup moves. Ignore the latter.
	 *
	 * Also ignore exiting tasks, because in the exit path tasks transition
	 * from the autogroup to the root group, so task_group_is_autogroup()
	 * alone isn't able to catch exiting autogroup tasks. This is safe for
	 * cgroup_move(), because cgroup migrations never happen for PF_EXITING
	 * tasks.
	 */
	if (task_group_is_autogroup(task_group(p)) || (p->flags & PF_EXITING))
		return;

	/*
	 * @p must have ops.cgroup_prep_move() called on it and thus
	 * cgrp_moving_from set.
	 */
	if (SCX_HAS_OP(cgroup_move) && !WARN_ON_ONCE(!p->scx.cgrp_moving_from))
		SCX_CALL_OP_TASK(SCX_KF_UNLOCKED, cgroup_move, p,
			p->scx.cgrp_moving_from, tg_cgrp(task_group(p)));
	p->scx.cgrp_moving_from = NULL;
}

void scx_cgroup_finish_attach(void)
{
	percpu_up_read(&scx_cgroup_rwsem);
}

void scx_cgroup_cancel_attach(struct cgroup_taskset *tset)
{
	struct cgroup_subsys_state *css;
	struct task_struct *p;

	if (!scx_cgroup_enabled)
		goto out_unlock;

	cgroup_taskset_for_each(p, css, tset) {
		if (SCX_HAS_OP(cgroup_cancel_move) && p->scx.cgrp_moving_from)
			SCX_CALL_OP(SCX_KF_UNLOCKED, cgroup_cancel_move, p,
				    p->scx.cgrp_moving_from, css->cgroup);
		p->scx.cgrp_moving_from = NULL;
	}
out_unlock:
	percpu_up_read(&scx_cgroup_rwsem);
}

void scx_group_set_weight(struct task_group *tg, unsigned long weight)
{
	percpu_down_read(&scx_cgroup_rwsem);

	if (scx_cgroup_enabled && tg->scx_weight != weight) {
		if (SCX_HAS_OP(cgroup_set_weight))
			SCX_CALL_OP(SCX_KF_UNLOCKED, cgroup_set_weight,
				    tg_cgrp(tg), weight);
		tg->scx_weight = weight;
	}

	percpu_up_read(&scx_cgroup_rwsem);
}

void scx_group_set_idle(struct task_group *tg, bool idle)
{
	percpu_down_read(&scx_cgroup_rwsem);
	scx_cgroup_warn_missing_idle(tg);
	percpu_up_read(&scx_cgroup_rwsem);
}

static void scx_cgroup_lock(void)
{
	percpu_down_write(&scx_cgroup_rwsem);
}

static void scx_cgroup_unlock(void)
{
	percpu_up_write(&scx_cgroup_rwsem);
}

#else	/* CONFIG_EXT_GROUP_SCHED */

static inline void scx_cgroup_lock(void) {}
static inline void scx_cgroup_unlock(void) {}

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

#ifdef CONFIG_SMP
	.select_task_rq		= select_task_rq_scx,
	.task_woken		= task_woken_scx,
	.set_cpus_allowed	= set_cpus_allowed_scx,

	.rq_online		= rq_online_scx,
	.rq_offline		= rq_offline_scx,
#endif

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

static struct scx_dispatch_q *create_dsq(u64 dsq_id, int node)
{
	struct scx_dispatch_q *dsq;
	int ret;

	if (dsq_id & SCX_DSQ_FLAG_BUILTIN)
		return ERR_PTR(-EINVAL);

	dsq = kmalloc_node(sizeof(*dsq), GFP_KERNEL, node);
	if (!dsq)
		return ERR_PTR(-ENOMEM);

	init_dsq(dsq, dsq_id);

	ret = rhashtable_insert_fast(&dsq_hash, &dsq->hash_node,
				     dsq_hash_params);
	if (ret) {
		kfree(dsq);
		return ERR_PTR(ret);
	}
	return dsq;
}

static void free_dsq_irq_workfn(struct irq_work *irq_work)
{
	struct llist_node *to_free = llist_del_all(&dsqs_to_free);
	struct scx_dispatch_q *dsq, *tmp_dsq;

	llist_for_each_entry_safe(dsq, tmp_dsq, to_free, free_node)
		kfree_rcu(dsq, rcu);
}

static DEFINE_IRQ_WORK(free_dsq_irq_work, free_dsq_irq_workfn);

static void destroy_dsq(u64 dsq_id)
{
	struct scx_dispatch_q *dsq;
	unsigned long flags;

	rcu_read_lock();

	dsq = find_user_dsq(dsq_id);
	if (!dsq)
		goto out_unlock_rcu;

	raw_spin_lock_irqsave(&dsq->lock, flags);

	if (dsq->nr) {
		scx_ops_error("attempting to destroy in-use dsq 0x%016llx (nr=%u)",
			      dsq->id, dsq->nr);
		goto out_unlock_dsq;
	}

	if (rhashtable_remove_fast(&dsq_hash, &dsq->hash_node, dsq_hash_params))
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
static void scx_cgroup_exit(void)
{
	struct cgroup_subsys_state *css;

	percpu_rwsem_assert_held(&scx_cgroup_rwsem);

	scx_cgroup_enabled = false;

	/*
	 * scx_tg_on/offline() are excluded through scx_cgroup_rwsem. If we walk
	 * cgroups and exit all the inited ones, all online cgroups are exited.
	 */
	rcu_read_lock();
	css_for_each_descendant_post(css, &root_task_group.css) {
		struct task_group *tg = css_tg(css);

		if (!(tg->scx_flags & SCX_TG_INITED))
			continue;
		tg->scx_flags &= ~SCX_TG_INITED;

		if (!scx_ops.cgroup_exit)
			continue;

		if (WARN_ON_ONCE(!css_tryget(css)))
			continue;
		rcu_read_unlock();

		SCX_CALL_OP(SCX_KF_UNLOCKED, cgroup_exit, css->cgroup);

		rcu_read_lock();
		css_put(css);
	}
	rcu_read_unlock();
}

static int scx_cgroup_init(void)
{
	struct cgroup_subsys_state *css;
	int ret;

	percpu_rwsem_assert_held(&scx_cgroup_rwsem);

	cgroup_warned_missing_weight = false;
	cgroup_warned_missing_idle = false;

	/*
	 * scx_tg_on/offline() are excluded thorugh scx_cgroup_rwsem. If we walk
	 * cgroups and init, all online cgroups are initialized.
	 */
	rcu_read_lock();
	css_for_each_descendant_pre(css, &root_task_group.css) {
		struct task_group *tg = css_tg(css);
		struct scx_cgroup_init_args args = { .weight = tg->scx_weight };

		scx_cgroup_warn_missing_weight(tg);
		scx_cgroup_warn_missing_idle(tg);

		if ((tg->scx_flags &
		     (SCX_TG_ONLINE | SCX_TG_INITED)) != SCX_TG_ONLINE)
			continue;

		if (!scx_ops.cgroup_init) {
			tg->scx_flags |= SCX_TG_INITED;
			continue;
		}

		if (WARN_ON_ONCE(!css_tryget(css)))
			continue;
		rcu_read_unlock();

		ret = SCX_CALL_OP_RET(SCX_KF_UNLOCKED, cgroup_init,
				      css->cgroup, &args);
		if (ret) {
			css_put(css);
			scx_ops_error("ops.cgroup_init() failed (%d)", ret);
			return ret;
		}
		tg->scx_flags |= SCX_TG_INITED;

		rcu_read_lock();
		css_put(css);
	}
	rcu_read_unlock();

	WARN_ON_ONCE(scx_cgroup_enabled);
	scx_cgroup_enabled = true;

	return 0;
}

#else
static void scx_cgroup_exit(void) {}
static int scx_cgroup_init(void) { return 0; }
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
	return sysfs_emit(buf, "%s\n",
			  scx_ops_enable_state_str[scx_ops_enable_state()]);
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

static void scx_kobj_release(struct kobject *kobj)
{
	kfree(kobj);
}

static ssize_t scx_attr_ops_show(struct kobject *kobj,
				 struct kobj_attribute *ka, char *buf)
{
	return sysfs_emit(buf, "%s\n", scx_ops.name);
}
SCX_ATTR(ops);

static struct attribute *scx_sched_attrs[] = {
	&scx_attr_ops.attr,
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
	return add_uevent_var(env, "SCXOPS=%s", scx_ops.name);
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
	if (!scx_enabled() ||
	    unlikely(scx_ops_enable_state() == SCX_OPS_DISABLING))
		return false;
	if (READ_ONCE(scx_switching_all))
		return true;
	return policy == SCHED_EXT;
}

/**
 * scx_softlockup - sched_ext softlockup handler
 *
 * On some multi-socket setups (e.g. 2x Intel 8480c), the BPF scheduler can
 * live-lock the system by making many CPUs target the same DSQ to the point
 * where soft-lockup detection triggers. This function is called from
 * soft-lockup watchdog when the triggering point is close and tries to unjam
 * the system by enabling the breather and aborting the BPF scheduler.
 */
void scx_softlockup(u32 dur_s)
{
	switch (scx_ops_enable_state()) {
	case SCX_OPS_ENABLING:
	case SCX_OPS_ENABLED:
		break;
	default:
		return;
	}

	/* allow only one instance, cleared at the end of scx_ops_bypass() */
	if (test_and_set_bit(0, &scx_in_softlockup))
		return;

	printk_deferred(KERN_ERR "sched_ext: Soft lockup - CPU%d stuck for %us, disabling \"%s\"\n",
			smp_processor_id(), dur_s, scx_ops.name);

	/*
	 * Some CPUs may be trapped in the dispatch paths. Enable breather
	 * immediately; otherwise, we might even be able to get to
	 * scx_ops_bypass().
	 */
	atomic_inc(&scx_ops_breather_depth);

	scx_ops_error("soft lockup - CPU#%d stuck for %us",
		      smp_processor_id(), dur_s);
}

static void scx_clear_softlockup(void)
{
	if (test_and_clear_bit(0, &scx_in_softlockup))
		atomic_dec(&scx_ops_breather_depth);
}

/**
 * scx_ops_bypass - [Un]bypass scx_ops and guarantee forward progress
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
 * - scx_bpf_kick_cpu() is disabled to avoid irq_work malfunction during PM
 *   operations.
 *
 * - scx_prio_less() reverts to the default core_sched_at order.
 */
static void scx_ops_bypass(bool bypass)
{
	static DEFINE_RAW_SPINLOCK(bypass_lock);
	int cpu;
	unsigned long flags;

	raw_spin_lock_irqsave(&bypass_lock, flags);
	if (bypass) {
		scx_ops_bypass_depth++;
		WARN_ON_ONCE(scx_ops_bypass_depth <= 0);
		if (scx_ops_bypass_depth != 1)
			goto unlock;
	} else {
		scx_ops_bypass_depth--;
		WARN_ON_ONCE(scx_ops_bypass_depth < 0);
		if (scx_ops_bypass_depth != 0)
			goto unlock;
	}

	atomic_inc(&scx_ops_breather_depth);

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
		struct rq_flags rf;
		struct task_struct *p, *n;

		rq_lock(rq, &rf);

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
			rq_unlock_irqrestore(rq, &rf);
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

		rq_unlock(rq, &rf);

		/* resched to restore ticks and idle state */
		resched_cpu(cpu);
	}

	atomic_dec(&scx_ops_breather_depth);
unlock:
	raw_spin_unlock_irqrestore(&bypass_lock, flags);
	scx_clear_softlockup();
}

static void free_exit_info(struct scx_exit_info *ei)
{
	kfree(ei->dump);
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
	ei->dump = kzalloc(exit_dump_len, GFP_KERNEL);

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

static void scx_ops_disable_workfn(struct kthread_work *work)
{
	struct scx_exit_info *ei = scx_exit_info;
	struct scx_task_iter sti;
	struct task_struct *p;
	struct rhashtable_iter rht_iter;
	struct scx_dispatch_q *dsq;
	int i, kind;

	kind = atomic_read(&scx_exit_kind);
	while (true) {
		/*
		 * NONE indicates that a new scx_ops has been registered since
		 * disable was scheduled - don't kill the new ops. DONE
		 * indicates that the ops has already been disabled.
		 */
		if (kind == SCX_EXIT_NONE || kind == SCX_EXIT_DONE)
			return;
		if (atomic_try_cmpxchg(&scx_exit_kind, &kind, SCX_EXIT_DONE))
			break;
	}
	ei->kind = kind;
	ei->reason = scx_exit_reason(ei->kind);

	/* guarantee forward progress by bypassing scx_ops */
	scx_ops_bypass(true);

	switch (scx_ops_set_enable_state(SCX_OPS_DISABLING)) {
	case SCX_OPS_DISABLING:
		WARN_ONCE(true, "sched_ext: duplicate disabling instance?");
		break;
	case SCX_OPS_DISABLED:
		pr_warn("sched_ext: ops error detected without ops (%s)\n",
			scx_exit_info->msg);
		WARN_ON_ONCE(scx_ops_set_enable_state(SCX_OPS_DISABLED) !=
			     SCX_OPS_DISABLING);
		goto done;
	default:
		break;
	}

	/*
	 * Here, every runnable task is guaranteed to make forward progress and
	 * we can safely use blocking synchronization constructs. Actually
	 * disable ops.
	 */
	mutex_lock(&scx_ops_enable_mutex);

	static_branch_disable(&__scx_switched_all);
	WRITE_ONCE(scx_switching_all, false);

	/*
	 * Shut down cgroup support before tasks so that the cgroup attach path
	 * doesn't race against scx_ops_exit_task().
	 */
	scx_cgroup_lock();
	scx_cgroup_exit();
	scx_cgroup_unlock();

	/*
	 * The BPF scheduler is going away. All tasks including %TASK_DEAD ones
	 * must be switched out and exited synchronously.
	 */
	percpu_down_write(&scx_fork_rwsem);

	scx_ops_init_task_enabled = false;

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
		scx_ops_exit_task(p);
	}
	scx_task_iter_stop(&sti);
	percpu_up_write(&scx_fork_rwsem);

	/* no task is on scx, turn off all the switches and flush in-progress calls */
	static_branch_disable(&__scx_ops_enabled);
	for (i = SCX_OPI_BEGIN; i < SCX_OPI_END; i++)
		static_branch_disable(&scx_has_op[i]);
	static_branch_disable(&scx_ops_enq_last);
	static_branch_disable(&scx_ops_enq_exiting);
	static_branch_disable(&scx_ops_cpu_preempt);
	static_branch_disable(&scx_builtin_idle_enabled);
	synchronize_rcu();

	if (ei->kind >= SCX_EXIT_ERROR) {
		pr_err("sched_ext: BPF scheduler \"%s\" disabled (%s)\n",
		       scx_ops.name, ei->reason);

		if (ei->msg[0] != '\0')
			pr_err("sched_ext: %s: %s\n", scx_ops.name, ei->msg);
#ifdef CONFIG_STACKTRACE
		stack_trace_print(ei->bt, ei->bt_len, 2);
#endif
	} else {
		pr_info("sched_ext: BPF scheduler \"%s\" disabled (%s)\n",
			scx_ops.name, ei->reason);
	}

	if (scx_ops.exit)
		SCX_CALL_OP(SCX_KF_UNLOCKED, exit, ei);

	cancel_delayed_work_sync(&scx_watchdog_work);

	/*
	 * Delete the kobject from the hierarchy eagerly in addition to just
	 * dropping a reference. Otherwise, if the object is deleted
	 * asynchronously, sysfs could observe an object of the same name still
	 * in the hierarchy when another scheduler is loaded.
	 */
	kobject_del(scx_root_kobj);
	kobject_put(scx_root_kobj);
	scx_root_kobj = NULL;

	memset(&scx_ops, 0, sizeof(scx_ops));

	rhashtable_walk_enter(&dsq_hash, &rht_iter);
	do {
		rhashtable_walk_start(&rht_iter);

		while ((dsq = rhashtable_walk_next(&rht_iter)) && !IS_ERR(dsq))
			destroy_dsq(dsq->id);

		rhashtable_walk_stop(&rht_iter);
	} while (dsq == ERR_PTR(-EAGAIN));
	rhashtable_walk_exit(&rht_iter);

	free_percpu(scx_dsp_ctx);
	scx_dsp_ctx = NULL;
	scx_dsp_max_batch = 0;

	free_exit_info(scx_exit_info);
	scx_exit_info = NULL;

	mutex_unlock(&scx_ops_enable_mutex);

	WARN_ON_ONCE(scx_ops_set_enable_state(SCX_OPS_DISABLED) !=
		     SCX_OPS_DISABLING);
done:
	scx_ops_bypass(false);
}

static DEFINE_KTHREAD_WORK(scx_ops_disable_work, scx_ops_disable_workfn);

static void schedule_scx_ops_disable_work(void)
{
	struct kthread_worker *helper = READ_ONCE(scx_ops_helper);

	/*
	 * We may be called spuriously before the first bpf_sched_ext_reg(). If
	 * scx_ops_helper isn't set up yet, there's nothing to do.
	 */
	if (helper)
		kthread_queue_work(helper, &scx_ops_disable_work);
}

static void scx_ops_disable(enum scx_exit_kind kind)
{
	int none = SCX_EXIT_NONE;

	if (WARN_ON_ONCE(kind == SCX_EXIT_NONE || kind == SCX_EXIT_DONE))
		kind = SCX_EXIT_ERROR;

	atomic_try_cmpxchg(&scx_exit_kind, &none, kind);

	schedule_scx_ops_disable_work();
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
	dump_line(s, "      sticky/holding_cpu=%d/%d dsq_id=%s dsq_vtime=%llu",
		  p->scx.sticky_cpu, p->scx.holding_cpu, dsq_id_buf,
		  p->scx.dsq_vtime);
	dump_line(s, "      cpus=%*pb", cpumask_pr_args(p->cpus_ptr));

	if (SCX_HAS_OP(dump_task)) {
		ops_dump_init(s, "    ");
		SCX_CALL_OP(SCX_KF_REST, dump_task, dctx, p);
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
	struct scx_dump_ctx dctx = {
		.kind = ei->kind,
		.exit_code = ei->exit_code,
		.reason = ei->reason,
		.at_ns = ktime_get_ns(),
		.at_jiffies = jiffies,
	};
	struct seq_buf s;
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

	if (SCX_HAS_OP(dump)) {
		ops_dump_init(&s, "");
		SCX_CALL_OP(SCX_KF_UNLOCKED, dump, &dctx);
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

		rq_lock(rq, &rf);

		idle = list_empty(&rq->scx.runnable_list) &&
			rq->curr->sched_class == &idle_sched_class;

		if (idle && !SCX_HAS_OP(dump_cpu))
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
		if (SCX_HAS_OP(dump_cpu)) {
			ops_dump_init(&ns, "  ");
			SCX_CALL_OP(SCX_KF_REST, dump_cpu, &dctx, cpu, idle);
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
		rq_unlock(rq, &rf);
	}

	if (seq_buf_has_overflowed(&s) && dump_len >= sizeof(trunc_marker))
		memcpy(ei->dump + dump_len - sizeof(trunc_marker),
		       trunc_marker, sizeof(trunc_marker));

	spin_unlock_irqrestore(&dump_lock, flags);
}

static void scx_ops_error_irq_workfn(struct irq_work *irq_work)
{
	struct scx_exit_info *ei = scx_exit_info;

	if (ei->kind >= SCX_EXIT_ERROR)
		scx_dump_state(ei, scx_ops.exit_dump_len);

	schedule_scx_ops_disable_work();
}

static DEFINE_IRQ_WORK(scx_ops_error_irq_work, scx_ops_error_irq_workfn);

static __printf(3, 4) void scx_ops_exit_kind(enum scx_exit_kind kind,
					     s64 exit_code,
					     const char *fmt, ...)
{
	struct scx_exit_info *ei = scx_exit_info;
	int none = SCX_EXIT_NONE;
	va_list args;

	if (!atomic_try_cmpxchg(&scx_exit_kind, &none, kind))
		return;

	ei->exit_code = exit_code;
#ifdef CONFIG_STACKTRACE
	if (kind >= SCX_EXIT_ERROR)
		ei->bt_len = stack_trace_save(ei->bt, SCX_EXIT_BT_LEN, 1);
#endif
	va_start(args, fmt);
	vscnprintf(ei->msg, SCX_EXIT_MSG_LEN, fmt, args);
	va_end(args);

	/*
	 * Set ei->kind and ->reason for scx_dump_state(). They'll be set again
	 * in scx_ops_disable_workfn().
	 */
	ei->kind = kind;
	ei->reason = scx_exit_reason(ei->kind);

	irq_work_queue(&scx_ops_error_irq_work);
}

static struct kthread_worker *scx_create_rt_helper(const char *name)
{
	struct kthread_worker *helper;

	helper = kthread_create_worker(0, name);
	if (helper)
		sched_set_fifo(helper->task);
	return helper;
}

static void check_hotplug_seq(const struct sched_ext_ops *ops)
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
			scx_ops_exit(SCX_ECODE_ACT_RESTART | SCX_ECODE_RSN_HOTPLUG,
				     "expected hotplug seq %llu did not match actual %llu",
				     ops->hotplug_seq, global_hotplug_seq);
		}
	}
}

static int validate_ops(const struct sched_ext_ops *ops)
{
	/*
	 * It doesn't make sense to specify the SCX_OPS_ENQ_LAST flag if the
	 * ops.enqueue() callback isn't implemented.
	 */
	if ((ops->flags & SCX_OPS_ENQ_LAST) && !ops->enqueue) {
		scx_ops_error("SCX_OPS_ENQ_LAST requires ops.enqueue() to be implemented");
		return -EINVAL;
	}

	return 0;
}

static int scx_ops_enable(struct sched_ext_ops *ops, struct bpf_link *link)
{
	struct scx_task_iter sti;
	struct task_struct *p;
	unsigned long timeout;
	int i, cpu, node, ret;

	if (!cpumask_equal(housekeeping_cpumask(HK_TYPE_DOMAIN),
			   cpu_possible_mask)) {
		pr_err("sched_ext: Not compatible with \"isolcpus=\" domain isolation\n");
		return -EINVAL;
	}

	mutex_lock(&scx_ops_enable_mutex);

	if (!scx_ops_helper) {
		WRITE_ONCE(scx_ops_helper,
			   scx_create_rt_helper("sched_ext_ops_helper"));
		if (!scx_ops_helper) {
			ret = -ENOMEM;
			goto err_unlock;
		}
	}

	if (!global_dsqs) {
		struct scx_dispatch_q **dsqs;

		dsqs = kcalloc(nr_node_ids, sizeof(dsqs[0]), GFP_KERNEL);
		if (!dsqs) {
			ret = -ENOMEM;
			goto err_unlock;
		}

		for_each_node_state(node, N_POSSIBLE) {
			struct scx_dispatch_q *dsq;

			dsq = kzalloc_node(sizeof(*dsq), GFP_KERNEL, node);
			if (!dsq) {
				for_each_node_state(node, N_POSSIBLE)
					kfree(dsqs[node]);
				kfree(dsqs);
				ret = -ENOMEM;
				goto err_unlock;
			}

			init_dsq(dsq, SCX_DSQ_GLOBAL);
			dsqs[node] = dsq;
		}

		global_dsqs = dsqs;
	}

	if (scx_ops_enable_state() != SCX_OPS_DISABLED) {
		ret = -EBUSY;
		goto err_unlock;
	}

	scx_root_kobj = kzalloc(sizeof(*scx_root_kobj), GFP_KERNEL);
	if (!scx_root_kobj) {
		ret = -ENOMEM;
		goto err_unlock;
	}

	scx_root_kobj->kset = scx_kset;
	ret = kobject_init_and_add(scx_root_kobj, &scx_ktype, NULL, "root");
	if (ret < 0)
		goto err;

	scx_exit_info = alloc_exit_info(ops->exit_dump_len);
	if (!scx_exit_info) {
		ret = -ENOMEM;
		goto err_del;
	}

	/*
	 * Set scx_ops, transition to ENABLING and clear exit info to arm the
	 * disable path. Failure triggers full disabling from here on.
	 */
	scx_ops = *ops;

	WARN_ON_ONCE(scx_ops_set_enable_state(SCX_OPS_ENABLING) !=
		     SCX_OPS_DISABLED);

	atomic_set(&scx_exit_kind, SCX_EXIT_NONE);
	scx_warned_zero_slice = false;

	atomic_long_set(&scx_nr_rejected, 0);

	for_each_possible_cpu(cpu)
		cpu_rq(cpu)->scx.cpuperf_target = SCX_CPUPERF_ONE;

	/*
	 * Keep CPUs stable during enable so that the BPF scheduler can track
	 * online CPUs by watching ->on/offline_cpu() after ->init().
	 */
	cpus_read_lock();

	if (scx_ops.init) {
		ret = SCX_CALL_OP_RET(SCX_KF_UNLOCKED, init);
		if (ret) {
			ret = ops_sanitize_err("init", ret);
			cpus_read_unlock();
			scx_ops_error("ops.init() failed (%d)", ret);
			goto err_disable;
		}
	}

	for (i = SCX_OPI_CPU_HOTPLUG_BEGIN; i < SCX_OPI_CPU_HOTPLUG_END; i++)
		if (((void (**)(void))ops)[i])
			static_branch_enable_cpuslocked(&scx_has_op[i]);

	check_hotplug_seq(ops);
#ifdef CONFIG_SMP
	update_selcpu_topology();
#endif
	cpus_read_unlock();

	ret = validate_ops(ops);
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
	 * Once __scx_ops_enabled is set, %current can be switched to SCX
	 * anytime. This can lead to stalls as some BPF schedulers (e.g.
	 * userspace scheduling) may not function correctly before all tasks are
	 * switched. Init in bypass mode to guarantee forward progress.
	 */
	scx_ops_bypass(true);

	for (i = SCX_OPI_NORMAL_BEGIN; i < SCX_OPI_NORMAL_END; i++)
		if (((void (**)(void))ops)[i])
			static_branch_enable(&scx_has_op[i]);

	if (ops->flags & SCX_OPS_ENQ_LAST)
		static_branch_enable(&scx_ops_enq_last);

	if (ops->flags & SCX_OPS_ENQ_EXITING)
		static_branch_enable(&scx_ops_enq_exiting);
	if (scx_ops.cpu_acquire || scx_ops.cpu_release)
		static_branch_enable(&scx_ops_cpu_preempt);

	if (!ops->update_idle || (ops->flags & SCX_OPS_KEEP_BUILTIN_IDLE)) {
		reset_idle_masks();
		static_branch_enable(&scx_builtin_idle_enabled);
	} else {
		static_branch_disable(&scx_builtin_idle_enabled);
	}

	/*
	 * Lock out forks, cgroup on/offlining and moves before opening the
	 * floodgate so that they don't wander into the operations prematurely.
	 */
	percpu_down_write(&scx_fork_rwsem);

	WARN_ON_ONCE(scx_ops_init_task_enabled);
	scx_ops_init_task_enabled = true;

	/*
	 * Enable ops for every task. Fork is excluded by scx_fork_rwsem
	 * preventing new tasks from being added. No need to exclude tasks
	 * leaving as sched_ext_free() can handle both prepped and enabled
	 * tasks. Prep all tasks first and then enable them with preemption
	 * disabled.
	 *
	 * All cgroups should be initialized before scx_ops_init_task() so that
	 * the BPF scheduler can reliably track each task's cgroup membership
	 * from scx_ops_init_task(). Lock out cgroup on/offlining and task
	 * migrations while tasks are being initialized so that
	 * scx_cgroup_can_attach() never sees uninitialized tasks.
	 */
	scx_cgroup_lock();
	ret = scx_cgroup_init();
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

		ret = scx_ops_init_task(p, task_group(p), false);
		if (ret) {
			put_task_struct(p);
			scx_task_iter_relock(&sti);
			scx_task_iter_stop(&sti);
			scx_ops_error("ops.init_task() failed (%d) for %s[%d]",
				      ret, p->comm, p->pid);
			goto err_disable_unlock_all;
		}

		scx_set_task_state(p, SCX_TASK_READY);

		put_task_struct(p);
		scx_task_iter_relock(&sti);
	}
	scx_task_iter_stop(&sti);
	scx_cgroup_unlock();
	percpu_up_write(&scx_fork_rwsem);

	/*
	 * All tasks are READY. It's safe to turn on scx_enabled() and switch
	 * all eligible tasks.
	 */
	WRITE_ONCE(scx_switching_all, !(ops->flags & SCX_OPS_SWITCH_PARTIAL));
	static_branch_enable(&__scx_ops_enabled);

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

		if (old_class != new_class && p->se.sched_delayed)
			dequeue_task(task_rq(p), p, DEQUEUE_SLEEP | DEQUEUE_DELAYED);

		sched_deq_and_put_task(p, DEQUEUE_SAVE | DEQUEUE_MOVE, &ctx);

		p->scx.slice = SCX_SLICE_DFL;
		p->sched_class = new_class;
		check_class_changing(task_rq(p), p, old_class);

		sched_enq_and_set_task(&ctx);

		check_class_changed(task_rq(p), p, old_class, p->prio);
	}
	scx_task_iter_stop(&sti);
	percpu_up_write(&scx_fork_rwsem);

	scx_ops_bypass(false);

	if (!scx_ops_tryset_enable_state(SCX_OPS_ENABLED, SCX_OPS_ENABLING)) {
		WARN_ON_ONCE(atomic_read(&scx_exit_kind) == SCX_EXIT_NONE);
		goto err_disable;
	}

	if (!(ops->flags & SCX_OPS_SWITCH_PARTIAL))
		static_branch_enable(&__scx_switched_all);

	pr_info("sched_ext: BPF scheduler \"%s\" enabled%s\n",
		scx_ops.name, scx_switched_all() ? "" : " (partial)");
	kobject_uevent(scx_root_kobj, KOBJ_ADD);
	mutex_unlock(&scx_ops_enable_mutex);

	atomic_long_inc(&scx_enable_seq);

	return 0;

err_del:
	kobject_del(scx_root_kobj);
err:
	kobject_put(scx_root_kobj);
	scx_root_kobj = NULL;
	if (scx_exit_info) {
		free_exit_info(scx_exit_info);
		scx_exit_info = NULL;
	}
err_unlock:
	mutex_unlock(&scx_ops_enable_mutex);
	return ret;

err_disable_unlock_all:
	scx_cgroup_unlock();
	percpu_up_write(&scx_fork_rwsem);
	scx_ops_bypass(false);
err_disable:
	mutex_unlock(&scx_ops_enable_mutex);
	/*
	 * Returning an error code here would not pass all the error information
	 * to userspace. Record errno using scx_ops_error() for cases
	 * scx_ops_error() wasn't already invoked and exit indicating success so
	 * that the error is notified through ops.exit() with all the details.
	 *
	 * Flush scx_ops_disable_work to ensure that error is reported before
	 * init completion.
	 */
	scx_ops_error("scx_ops_enable() failed (%d)", ret);
	kthread_flush_work(&scx_ops_disable_work);
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

static const struct bpf_func_proto *
bpf_scx_get_func_proto(enum bpf_func_id func_id, const struct bpf_prog *prog)
{
	switch (func_id) {
	case BPF_FUNC_task_storage_get:
		return &bpf_task_storage_get_proto;
	case BPF_FUNC_task_storage_delete:
		return &bpf_task_storage_delete_proto;
	default:
		return bpf_base_func_proto(func_id, prog);
	}
}

static const struct bpf_verifier_ops bpf_scx_verifier_ops = {
	.get_func_proto = bpf_scx_get_func_proto,
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
	return scx_ops_enable(kdata, link);
}

static void bpf_scx_unreg(void *kdata, struct bpf_link *link)
{
	scx_ops_disable(SCX_EXIT_UNREG);
	kthread_flush_work(&scx_ops_disable_work);
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
	if (scx_ops_helper)
		scx_ops_disable(SCX_EXIT_SYSRQ);
	else
		pr_info("sched_ext: BPF scheduler not yet used\n");
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
	unsigned long *pseqs = this_cpu_ptr(scx_kick_cpus_pnt_seqs);
	bool should_wait = false;
	s32 cpu;

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
	enum scx_ops_enable_state state = scx_ops_enable_state();
	const char *all = READ_ONCE(scx_switching_all) ? "+all" : "";
	char runnable_at_buf[22] = "?";
	struct sched_class *class;
	unsigned long runnable_at;

	if (state == SCX_OPS_DISABLED)
		return;

	/*
	 * Carefully check if the task was running on sched_ext, and then
	 * carefully copy the time it's been runnable, and its state.
	 */
	if (copy_from_kernel_nofault(&class, &p->sched_class, sizeof(class)) ||
	    class != &ext_sched_class) {
		printk("%sSched_ext: %s (%s%s)", log_lvl, scx_ops.name,
		       scx_ops_enable_state_str[state], all);
		return;
	}

	if (!copy_from_kernel_nofault(&runnable_at, &p->scx.runnable_at,
				      sizeof(runnable_at)))
		scnprintf(runnable_at_buf, sizeof(runnable_at_buf), "%+ldms",
			  jiffies_delta_msecs(runnable_at, jiffies));

	/* print everything onto one line to conserve console space */
	printk("%sSched_ext: %s (%s%s), task: runnable_at=%s",
	       log_lvl, scx_ops.name, scx_ops_enable_state_str[state], all,
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
		scx_ops_bypass(true);
		break;
	case PM_POST_HIBERNATION:
	case PM_POST_SUSPEND:
	case PM_POST_RESTORE:
		scx_ops_bypass(false);
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

	BUG_ON(rhashtable_init(&dsq_hash, &dsq_hash_params));
#ifdef CONFIG_SMP
	BUG_ON(!alloc_cpumask_var(&idle_masks.cpu, GFP_KERNEL));
	BUG_ON(!alloc_cpumask_var(&idle_masks.smt, GFP_KERNEL));
#endif
	scx_kick_cpus_pnt_seqs =
		__alloc_percpu(sizeof(scx_kick_cpus_pnt_seqs[0]) * nr_cpu_ids,
			       __alignof__(scx_kick_cpus_pnt_seqs[0]));
	BUG_ON(!scx_kick_cpus_pnt_seqs);

	for_each_possible_cpu(cpu) {
		struct rq *rq = cpu_rq(cpu);

		init_dsq(&rq->scx.local_dsq, SCX_DSQ_LOCAL);
		INIT_LIST_HEAD(&rq->scx.runnable_list);
		INIT_LIST_HEAD(&rq->scx.ddsp_deferred_locals);

		BUG_ON(!zalloc_cpumask_var(&rq->scx.cpus_to_kick, GFP_KERNEL));
		BUG_ON(!zalloc_cpumask_var(&rq->scx.cpus_to_kick_if_idle, GFP_KERNEL));
		BUG_ON(!zalloc_cpumask_var(&rq->scx.cpus_to_preempt, GFP_KERNEL));
		BUG_ON(!zalloc_cpumask_var(&rq->scx.cpus_to_wait, GFP_KERNEL));
		init_irq_work(&rq->scx.deferred_irq_work, deferred_irq_workfn);
		init_irq_work(&rq->scx.kick_cpus_irq_work, kick_cpus_irq_workfn);

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
#include <linux/btf_ids.h>

__bpf_kfunc_start_defs();

/**
 * scx_bpf_select_cpu_dfl - The default implementation of ops.select_cpu()
 * @p: task_struct to select a CPU for
 * @prev_cpu: CPU @p was on previously
 * @wake_flags: %SCX_WAKE_* flags
 * @is_idle: out parameter indicating whether the returned CPU is idle
 *
 * Can only be called from ops.select_cpu() if the built-in CPU selection is
 * enabled - ops.update_idle() is missing or %SCX_OPS_KEEP_BUILTIN_IDLE is set.
 * @p, @prev_cpu and @wake_flags match ops.select_cpu().
 *
 * Returns the picked CPU with *@is_idle indicating whether the picked CPU is
 * currently idle and thus a good candidate for direct dispatching.
 */
__bpf_kfunc s32 scx_bpf_select_cpu_dfl(struct task_struct *p, s32 prev_cpu,
				       u64 wake_flags, bool *is_idle)
{
	if (!static_branch_likely(&scx_builtin_idle_enabled)) {
		scx_ops_error("built-in idle tracking is disabled");
		goto prev_cpu;
	}

	if (!scx_kf_allowed(SCX_KF_SELECT_CPU))
		goto prev_cpu;

#ifdef CONFIG_SMP
	return scx_select_cpu_dfl(p, prev_cpu, wake_flags, is_idle);
#endif

prev_cpu:
	*is_idle = false;
	return prev_cpu;
}

__bpf_kfunc_end_defs();

BTF_KFUNCS_START(scx_kfunc_ids_select_cpu)
BTF_ID_FLAGS(func, scx_bpf_select_cpu_dfl, KF_RCU)
BTF_KFUNCS_END(scx_kfunc_ids_select_cpu)

static const struct btf_kfunc_id_set scx_kfunc_set_select_cpu = {
	.owner			= THIS_MODULE,
	.set			= &scx_kfunc_ids_select_cpu,
};

static bool scx_dsq_insert_preamble(struct task_struct *p, u64 enq_flags)
{
	if (!scx_kf_allowed(SCX_KF_ENQUEUE | SCX_KF_DISPATCH))
		return false;

	lockdep_assert_irqs_disabled();

	if (unlikely(!p)) {
		scx_ops_error("called with NULL task");
		return false;
	}

	if (unlikely(enq_flags & __SCX_ENQ_INTERNAL_MASK)) {
		scx_ops_error("invalid enq_flags 0x%llx", enq_flags);
		return false;
	}

	return true;
}

static void scx_dsq_insert_commit(struct task_struct *p, u64 dsq_id,
				  u64 enq_flags)
{
	struct scx_dsp_ctx *dspc = this_cpu_ptr(scx_dsp_ctx);
	struct task_struct *ddsp_task;

	ddsp_task = __this_cpu_read(direct_dispatch_task);
	if (ddsp_task) {
		mark_direct_dispatch(ddsp_task, p, dsq_id, enq_flags);
		return;
	}

	if (unlikely(dspc->cursor >= scx_dsp_max_batch)) {
		scx_ops_error("dispatch buffer overflow");
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
 * and @p must match the task being enqueued. Also, %SCX_DSQ_LOCAL_ON can't be
 * used to target the local DSQ of a CPU other than the enqueueing one. Use
 * ops.select_cpu() to be on the target CPU in the first place.
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
 * remaining slots. scx_bpf_consume() flushes the batch and resets the counter.
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
	if (!scx_dsq_insert_preamble(p, enq_flags))
		return;

	if (slice)
		p->scx.slice = slice;
	else
		p->scx.slice = p->scx.slice ?: 1;

	scx_dsq_insert_commit(p, dsq_id, enq_flags);
}

/* for backward compatibility, will be removed in v6.15 */
__bpf_kfunc void scx_bpf_dispatch(struct task_struct *p, u64 dsq_id, u64 slice,
				  u64 enq_flags)
{
	printk_deferred_once(KERN_WARNING "sched_ext: scx_bpf_dispatch() renamed to scx_bpf_dsq_insert()");
	scx_bpf_dsq_insert(p, dsq_id, slice, enq_flags);
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
	if (!scx_dsq_insert_preamble(p, enq_flags))
		return;

	if (slice)
		p->scx.slice = slice;
	else
		p->scx.slice = p->scx.slice ?: 1;

	p->scx.dsq_vtime = vtime;

	scx_dsq_insert_commit(p, dsq_id, enq_flags | SCX_ENQ_DSQ_PRIQ);
}

/* for backward compatibility, will be removed in v6.15 */
__bpf_kfunc void scx_bpf_dispatch_vtime(struct task_struct *p, u64 dsq_id,
					u64 slice, u64 vtime, u64 enq_flags)
{
	printk_deferred_once(KERN_WARNING "sched_ext: scx_bpf_dispatch_vtime() renamed to scx_bpf_dsq_insert_vtime()");
	scx_bpf_dsq_insert_vtime(p, dsq_id, slice, vtime, enq_flags);
}

__bpf_kfunc_end_defs();

BTF_KFUNCS_START(scx_kfunc_ids_enqueue_dispatch)
BTF_ID_FLAGS(func, scx_bpf_dsq_insert, KF_RCU)
BTF_ID_FLAGS(func, scx_bpf_dsq_insert_vtime, KF_RCU)
BTF_ID_FLAGS(func, scx_bpf_dispatch, KF_RCU)
BTF_ID_FLAGS(func, scx_bpf_dispatch_vtime, KF_RCU)
BTF_KFUNCS_END(scx_kfunc_ids_enqueue_dispatch)

static const struct btf_kfunc_id_set scx_kfunc_set_enqueue_dispatch = {
	.owner			= THIS_MODULE,
	.set			= &scx_kfunc_ids_enqueue_dispatch,
};

static bool scx_dsq_move(struct bpf_iter_scx_dsq_kern *kit,
			 struct task_struct *p, u64 dsq_id, u64 enq_flags)
{
	struct scx_dispatch_q *src_dsq = kit->dsq, *dst_dsq;
	struct rq *this_rq, *src_rq, *locked_rq;
	bool dispatched = false;
	bool in_balance;
	unsigned long flags;

	if (!scx_kf_allowed_if_unlocked() && !scx_kf_allowed(SCX_KF_DISPATCH))
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
	scx_ops_breather(src_rq);

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
	dst_dsq = find_dsq_for_dispatch(this_rq, dsq_id, p);

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
	locked_rq = move_task_between_dsqs(p, enq_flags, src_dsq, dst_dsq);
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
	if (!scx_kf_allowed(SCX_KF_DISPATCH))
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

	if (!scx_kf_allowed(SCX_KF_DISPATCH))
		return;

	if (dspc->cursor > 0)
		dspc->cursor--;
	else
		scx_ops_error("dispatch buffer underflow");
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

	if (!scx_kf_allowed(SCX_KF_DISPATCH))
		return false;

	flush_dispatch_buf(dspc->rq);

	dsq = find_user_dsq(dsq_id);
	if (unlikely(!dsq)) {
		scx_ops_error("invalid DSQ ID 0x%016llx", dsq_id);
		return false;
	}

	if (consume_dispatch_q(dspc->rq, dsq)) {
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

/* for backward compatibility, will be removed in v6.15 */
__bpf_kfunc bool scx_bpf_consume(u64 dsq_id)
{
	printk_deferred_once(KERN_WARNING "sched_ext: scx_bpf_consume() renamed to scx_bpf_dsq_move_to_local()");
	return scx_bpf_dsq_move_to_local(dsq_id);
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

/* for backward compatibility, will be removed in v6.15 */
__bpf_kfunc void scx_bpf_dispatch_from_dsq_set_slice(
			struct bpf_iter_scx_dsq *it__iter, u64 slice)
{
	printk_deferred_once(KERN_WARNING "sched_ext: scx_bpf_dispatch_from_dsq_set_slice() renamed to scx_bpf_dsq_move_set_slice()");
	scx_bpf_dsq_move_set_slice(it__iter, slice);
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

/* for backward compatibility, will be removed in v6.15 */
__bpf_kfunc void scx_bpf_dispatch_from_dsq_set_vtime(
			struct bpf_iter_scx_dsq *it__iter, u64 vtime)
{
	printk_deferred_once(KERN_WARNING "sched_ext: scx_bpf_dispatch_from_dsq_set_vtime() renamed to scx_bpf_dsq_move_set_vtime()");
	scx_bpf_dsq_move_set_vtime(it__iter, vtime);
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

/* for backward compatibility, will be removed in v6.15 */
__bpf_kfunc bool scx_bpf_dispatch_from_dsq(struct bpf_iter_scx_dsq *it__iter,
					   struct task_struct *p, u64 dsq_id,
					   u64 enq_flags)
{
	printk_deferred_once(KERN_WARNING "sched_ext: scx_bpf_dispatch_from_dsq() renamed to scx_bpf_dsq_move()");
	return scx_bpf_dsq_move(it__iter, p, dsq_id, enq_flags);
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

/* for backward compatibility, will be removed in v6.15 */
__bpf_kfunc bool scx_bpf_dispatch_vtime_from_dsq(struct bpf_iter_scx_dsq *it__iter,
						 struct task_struct *p, u64 dsq_id,
						 u64 enq_flags)
{
	printk_deferred_once(KERN_WARNING "sched_ext: scx_bpf_dispatch_from_dsq_vtime() renamed to scx_bpf_dsq_move_vtime()");
	return scx_bpf_dsq_move_vtime(it__iter, p, dsq_id, enq_flags);
}

__bpf_kfunc_end_defs();

BTF_KFUNCS_START(scx_kfunc_ids_dispatch)
BTF_ID_FLAGS(func, scx_bpf_dispatch_nr_slots)
BTF_ID_FLAGS(func, scx_bpf_dispatch_cancel)
BTF_ID_FLAGS(func, scx_bpf_dsq_move_to_local)
BTF_ID_FLAGS(func, scx_bpf_consume)
BTF_ID_FLAGS(func, scx_bpf_dsq_move_set_slice)
BTF_ID_FLAGS(func, scx_bpf_dsq_move_set_vtime)
BTF_ID_FLAGS(func, scx_bpf_dsq_move, KF_RCU)
BTF_ID_FLAGS(func, scx_bpf_dsq_move_vtime, KF_RCU)
BTF_ID_FLAGS(func, scx_bpf_dispatch_from_dsq_set_slice)
BTF_ID_FLAGS(func, scx_bpf_dispatch_from_dsq_set_vtime)
BTF_ID_FLAGS(func, scx_bpf_dispatch_from_dsq, KF_RCU)
BTF_ID_FLAGS(func, scx_bpf_dispatch_vtime_from_dsq, KF_RCU)
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
	LIST_HEAD(tasks);
	u32 nr_enqueued = 0;
	struct rq *rq;
	struct task_struct *p, *n;

	if (!scx_kf_allowed(SCX_KF_CPU_RELEASE))
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
	if (unlikely(node >= (int)nr_node_ids ||
		     (node < 0 && node != NUMA_NO_NODE)))
		return -EINVAL;
	return PTR_ERR_OR_ZERO(create_dsq(dsq_id, node));
}

__bpf_kfunc_end_defs();

BTF_KFUNCS_START(scx_kfunc_ids_unlocked)
BTF_ID_FLAGS(func, scx_bpf_create_dsq, KF_SLEEPABLE)
BTF_ID_FLAGS(func, scx_bpf_dsq_move_set_slice)
BTF_ID_FLAGS(func, scx_bpf_dsq_move_set_vtime)
BTF_ID_FLAGS(func, scx_bpf_dsq_move, KF_RCU)
BTF_ID_FLAGS(func, scx_bpf_dsq_move_vtime, KF_RCU)
BTF_ID_FLAGS(func, scx_bpf_dispatch_from_dsq_set_slice)
BTF_ID_FLAGS(func, scx_bpf_dispatch_from_dsq_set_vtime)
BTF_ID_FLAGS(func, scx_bpf_dispatch_from_dsq, KF_RCU)
BTF_ID_FLAGS(func, scx_bpf_dispatch_vtime_from_dsq, KF_RCU)
BTF_KFUNCS_END(scx_kfunc_ids_unlocked)

static const struct btf_kfunc_id_set scx_kfunc_set_unlocked = {
	.owner			= THIS_MODULE,
	.set			= &scx_kfunc_ids_unlocked,
};

__bpf_kfunc_start_defs();

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
	struct rq *this_rq;
	unsigned long irq_flags;

	if (!ops_cpu_valid(cpu, NULL))
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
			scx_ops_error("PREEMPT/WAIT cannot be used with SCX_KICK_IDLE");

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
 * scx_bpf_dsq_nr_queued - Return the number of queued tasks
 * @dsq_id: id of the DSQ
 *
 * Return the number of tasks in the DSQ matching @dsq_id. If not found,
 * -%ENOENT is returned.
 */
__bpf_kfunc s32 scx_bpf_dsq_nr_queued(u64 dsq_id)
{
	struct scx_dispatch_q *dsq;
	s32 ret;

	preempt_disable();

	if (dsq_id == SCX_DSQ_LOCAL) {
		ret = READ_ONCE(this_rq()->scx.local_dsq.nr);
		goto out;
	} else if ((dsq_id & SCX_DSQ_LOCAL_ON) == SCX_DSQ_LOCAL_ON) {
		s32 cpu = dsq_id & SCX_DSQ_LOCAL_CPU_MASK;

		if (ops_cpu_valid(cpu, NULL)) {
			ret = READ_ONCE(cpu_rq(cpu)->scx.local_dsq.nr);
			goto out;
		}
	} else {
		dsq = find_user_dsq(dsq_id);
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
	destroy_dsq(dsq_id);
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

	BUILD_BUG_ON(sizeof(struct bpf_iter_scx_dsq_kern) >
		     sizeof(struct bpf_iter_scx_dsq));
	BUILD_BUG_ON(__alignof__(struct bpf_iter_scx_dsq_kern) !=
		     __alignof__(struct bpf_iter_scx_dsq));

	if (flags & ~__SCX_DSQ_ITER_USER_FLAGS)
		return -EINVAL;

	kit->dsq = find_user_dsq(dsq_id);
	if (!kit->dsq)
		return -ENOENT;

	INIT_LIST_HEAD(&kit->cursor.node);
	kit->cursor.flags |= SCX_DSQ_LNODE_ITER_CURSOR | flags;
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

static s32 __bstr_format(u64 *data_buf, char *line_buf, size_t line_size,
			 char *fmt, unsigned long long *data, u32 data__sz)
{
	struct bpf_bprintf_data bprintf_data = { .get_bin_args = true };
	s32 ret;

	if (data__sz % 8 || data__sz > MAX_BPRINTF_VARARGS * 8 ||
	    (data__sz && !data)) {
		scx_ops_error("invalid data=%p and data__sz=%u",
			      (void *)data, data__sz);
		return -EINVAL;
	}

	ret = copy_from_kernel_nofault(data_buf, data, data__sz);
	if (ret < 0) {
		scx_ops_error("failed to read data fields (%d)", ret);
		return ret;
	}

	ret = bpf_bprintf_prepare(fmt, UINT_MAX, data_buf, data__sz / 8,
				  &bprintf_data);
	if (ret < 0) {
		scx_ops_error("format preparation failed (%d)", ret);
		return ret;
	}

	ret = bstr_printf(line_buf, line_size, fmt,
			  bprintf_data.bin_args);
	bpf_bprintf_cleanup(&bprintf_data);
	if (ret < 0) {
		scx_ops_error("(\"%s\", %p, %u) failed to format",
			      fmt, data, data__sz);
		return ret;
	}

	return ret;
}

static s32 bstr_format(struct scx_bstr_buf *buf,
		       char *fmt, unsigned long long *data, u32 data__sz)
{
	return __bstr_format(buf->data, buf->line, sizeof(buf->line),
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
	unsigned long flags;

	raw_spin_lock_irqsave(&scx_exit_bstr_buf_lock, flags);
	if (bstr_format(&scx_exit_bstr_buf, fmt, data, data__sz) >= 0)
		scx_ops_exit_kind(SCX_EXIT_UNREG_BPF, exit_code, "%s",
				  scx_exit_bstr_buf.line);
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
	unsigned long flags;

	raw_spin_lock_irqsave(&scx_exit_bstr_buf_lock, flags);
	if (bstr_format(&scx_exit_bstr_buf, fmt, data, data__sz) >= 0)
		scx_ops_exit_kind(SCX_EXIT_ERROR_BPF, 0, "%s",
				  scx_exit_bstr_buf.line);
	raw_spin_unlock_irqrestore(&scx_exit_bstr_buf_lock, flags);
}

/**
 * scx_bpf_dump - Generate extra debug dump specific to the BPF scheduler
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
	struct scx_dump_data *dd = &scx_dump_data;
	struct scx_bstr_buf *buf = &dd->buf;
	s32 ret;

	if (raw_smp_processor_id() != dd->cpu) {
		scx_ops_error("scx_bpf_dump() must only be called from ops.dump() and friends");
		return;
	}

	/* append the formatted string to the line buf */
	ret = __bstr_format(buf->data, buf->line + dd->cursor,
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
	if (ops_cpu_valid(cpu, NULL))
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
	if (ops_cpu_valid(cpu, NULL))
		return arch_scale_freq_capacity(cpu);
	else
		return SCX_CPUPERF_ONE;
}

/**
 * scx_bpf_cpuperf_set - Set the relative performance target of a CPU
 * @cpu: CPU of interest
 * @perf: target performance level [0, %SCX_CPUPERF_ONE]
 * @flags: %SCX_CPUPERF_* flags
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
	if (unlikely(perf > SCX_CPUPERF_ONE)) {
		scx_ops_error("Invalid cpuperf target %u for CPU %d", perf, cpu);
		return;
	}

	if (ops_cpu_valid(cpu, NULL)) {
		struct rq *rq = cpu_rq(cpu);

		rq->scx.cpuperf_target = perf;

		rcu_read_lock_sched_notrace();
		cpufreq_update_util(cpu_rq(cpu), 0);
		rcu_read_unlock_sched_notrace();
	}
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
 * scx_bpf_get_idle_cpumask - Get a referenced kptr to the idle-tracking
 * per-CPU cpumask.
 *
 * Returns NULL if idle tracking is not enabled, or running on a UP kernel.
 */
__bpf_kfunc const struct cpumask *scx_bpf_get_idle_cpumask(void)
{
	if (!static_branch_likely(&scx_builtin_idle_enabled)) {
		scx_ops_error("built-in idle tracking is disabled");
		return cpu_none_mask;
	}

#ifdef CONFIG_SMP
	return idle_masks.cpu;
#else
	return cpu_none_mask;
#endif
}

/**
 * scx_bpf_get_idle_smtmask - Get a referenced kptr to the idle-tracking,
 * per-physical-core cpumask. Can be used to determine if an entire physical
 * core is free.
 *
 * Returns NULL if idle tracking is not enabled, or running on a UP kernel.
 */
__bpf_kfunc const struct cpumask *scx_bpf_get_idle_smtmask(void)
{
	if (!static_branch_likely(&scx_builtin_idle_enabled)) {
		scx_ops_error("built-in idle tracking is disabled");
		return cpu_none_mask;
	}

#ifdef CONFIG_SMP
	if (sched_smt_active())
		return idle_masks.smt;
	else
		return idle_masks.cpu;
#else
	return cpu_none_mask;
#endif
}

/**
 * scx_bpf_put_idle_cpumask - Release a previously acquired referenced kptr to
 * either the percpu, or SMT idle-tracking cpumask.
 */
__bpf_kfunc void scx_bpf_put_idle_cpumask(const struct cpumask *idle_mask)
{
	/*
	 * Empty function body because we aren't actually acquiring or releasing
	 * a reference to a global idle cpumask, which is read-only in the
	 * caller and is never released. The acquire / release semantics here
	 * are just used to make the cpumask a trusted pointer in the caller.
	 */
}

/**
 * scx_bpf_test_and_clear_cpu_idle - Test and clear @cpu's idle state
 * @cpu: cpu to test and clear idle for
 *
 * Returns %true if @cpu was idle and its idle state was successfully cleared.
 * %false otherwise.
 *
 * Unavailable if ops.update_idle() is implemented and
 * %SCX_OPS_KEEP_BUILTIN_IDLE is not set.
 */
__bpf_kfunc bool scx_bpf_test_and_clear_cpu_idle(s32 cpu)
{
	if (!static_branch_likely(&scx_builtin_idle_enabled)) {
		scx_ops_error("built-in idle tracking is disabled");
		return false;
	}

	if (ops_cpu_valid(cpu, NULL))
		return test_and_clear_cpu_idle(cpu);
	else
		return false;
}

/**
 * scx_bpf_pick_idle_cpu - Pick and claim an idle cpu
 * @cpus_allowed: Allowed cpumask
 * @flags: %SCX_PICK_IDLE_CPU_* flags
 *
 * Pick and claim an idle cpu in @cpus_allowed. Returns the picked idle cpu
 * number on success. -%EBUSY if no matching cpu was found.
 *
 * Idle CPU tracking may race against CPU scheduling state transitions. For
 * example, this function may return -%EBUSY as CPUs are transitioning into the
 * idle state. If the caller then assumes that there will be dispatch events on
 * the CPUs as they were all busy, the scheduler may end up stalling with CPUs
 * idling while there are pending tasks. Use scx_bpf_pick_any_cpu() and
 * scx_bpf_kick_cpu() to guarantee that there will be at least one dispatch
 * event in the near future.
 *
 * Unavailable if ops.update_idle() is implemented and
 * %SCX_OPS_KEEP_BUILTIN_IDLE is not set.
 */
__bpf_kfunc s32 scx_bpf_pick_idle_cpu(const struct cpumask *cpus_allowed,
				      u64 flags)
{
	if (!static_branch_likely(&scx_builtin_idle_enabled)) {
		scx_ops_error("built-in idle tracking is disabled");
		return -EBUSY;
	}

	return scx_pick_idle_cpu(cpus_allowed, flags);
}

/**
 * scx_bpf_pick_any_cpu - Pick and claim an idle cpu if available or pick any CPU
 * @cpus_allowed: Allowed cpumask
 * @flags: %SCX_PICK_IDLE_CPU_* flags
 *
 * Pick and claim an idle cpu in @cpus_allowed. If none is available, pick any
 * CPU in @cpus_allowed. Guaranteed to succeed and returns the picked idle cpu
 * number if @cpus_allowed is not empty. -%EBUSY is returned if @cpus_allowed is
 * empty.
 *
 * If ops.update_idle() is implemented and %SCX_OPS_KEEP_BUILTIN_IDLE is not
 * set, this function can't tell which CPUs are idle and will always pick any
 * CPU.
 */
__bpf_kfunc s32 scx_bpf_pick_any_cpu(const struct cpumask *cpus_allowed,
				     u64 flags)
{
	s32 cpu;

	if (static_branch_likely(&scx_builtin_idle_enabled)) {
		cpu = scx_pick_idle_cpu(cpus_allowed, flags);
		if (cpu >= 0)
			return cpu;
	}

	cpu = cpumask_any_distribute(cpus_allowed);
	if (cpu < nr_cpu_ids)
		return cpu;
	else
		return -EBUSY;
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
	if (!ops_cpu_valid(cpu, NULL))
		return NULL;

	return cpu_rq(cpu);
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

	if (!scx_kf_allowed_on_arg_tasks(__SCX_KF_RQ_LOCKED, p))
		goto out;

	cgrp = tg_cgrp(tg);

out:
	cgroup_get(cgrp);
	return cgrp;
}
#endif

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
BTF_ID_FLAGS(func, scx_bpf_nr_cpu_ids)
BTF_ID_FLAGS(func, scx_bpf_get_possible_cpumask, KF_ACQUIRE)
BTF_ID_FLAGS(func, scx_bpf_get_online_cpumask, KF_ACQUIRE)
BTF_ID_FLAGS(func, scx_bpf_put_cpumask, KF_RELEASE)
BTF_ID_FLAGS(func, scx_bpf_get_idle_cpumask, KF_ACQUIRE)
BTF_ID_FLAGS(func, scx_bpf_get_idle_smtmask, KF_ACQUIRE)
BTF_ID_FLAGS(func, scx_bpf_put_idle_cpumask, KF_RELEASE)
BTF_ID_FLAGS(func, scx_bpf_test_and_clear_cpu_idle)
BTF_ID_FLAGS(func, scx_bpf_pick_idle_cpu, KF_RCU)
BTF_ID_FLAGS(func, scx_bpf_pick_any_cpu, KF_RCU)
BTF_ID_FLAGS(func, scx_bpf_task_running, KF_RCU)
BTF_ID_FLAGS(func, scx_bpf_task_cpu, KF_RCU)
BTF_ID_FLAGS(func, scx_bpf_cpu_rq)
#ifdef CONFIG_CGROUP_SCHED
BTF_ID_FLAGS(func, scx_bpf_task_cgroup, KF_RCU | KF_ACQUIRE)
#endif
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
					     &scx_kfunc_set_select_cpu)) ||
	    (ret = register_btf_kfunc_id_set(BPF_PROG_TYPE_STRUCT_OPS,
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
