// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2010-2017 Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 *
 * membarrier system call
 */
#include "sched.h"

/*
 * Bitmask made from a "or" of all commands within enum membarrier_cmd,
 * except MEMBARRIER_CMD_QUERY.
 */
#ifdef CONFIG_ARCH_HAS_MEMBARRIER_SYNC_CORE
#define MEMBARRIER_PRIVATE_EXPEDITED_SYNC_CORE_BITMASK			\
	(MEMBARRIER_CMD_PRIVATE_EXPEDITED_SYNC_CORE			\
	| MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED_SYNC_CORE)
#else
#define MEMBARRIER_PRIVATE_EXPEDITED_SYNC_CORE_BITMASK	0
#endif

#define MEMBARRIER_CMD_BITMASK						\
	(MEMBARRIER_CMD_GLOBAL | MEMBARRIER_CMD_GLOBAL_EXPEDITED	\
	| MEMBARRIER_CMD_REGISTER_GLOBAL_EXPEDITED			\
	| MEMBARRIER_CMD_PRIVATE_EXPEDITED				\
	| MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED			\
	| MEMBARRIER_PRIVATE_EXPEDITED_SYNC_CORE_BITMASK)

static void ipi_mb(void *info)
{
	smp_mb();	/* IPIs should be serializing but paranoid. */
}

static void ipi_sync_rq_state(void *info)
{
	struct mm_struct *mm = (struct mm_struct *) info;

	if (current->mm != mm)
		return;
	this_cpu_write(runqueues.membarrier_state,
		       atomic_read(&mm->membarrier_state));
	/*
	 * Issue a memory barrier after setting
	 * MEMBARRIER_STATE_GLOBAL_EXPEDITED in the current runqueue to
	 * guarantee that no memory access following registration is reordered
	 * before registration.
	 */
	smp_mb();
}

void membarrier_exec_mmap(struct mm_struct *mm)
{
	/*
	 * Issue a memory barrier before clearing membarrier_state to
	 * guarantee that no memory access prior to exec is reordered after
	 * clearing this state.
	 */
	smp_mb();
	atomic_set(&mm->membarrier_state, 0);
	/*
	 * Keep the runqueue membarrier_state in sync with this mm
	 * membarrier_state.
	 */
	this_cpu_write(runqueues.membarrier_state, 0);
}

static int membarrier_global_expedited(void)
{
	int cpu;
	cpumask_var_t tmpmask;

	if (num_online_cpus() == 1)
		return 0;

	/*
	 * Matches memory barriers around rq->curr modification in
	 * scheduler.
	 */
	smp_mb();	/* system call entry is not a mb. */

	if (!zalloc_cpumask_var(&tmpmask, GFP_KERNEL))
		return -ENOMEM;

	cpus_read_lock();
	rcu_read_lock();
	for_each_online_cpu(cpu) {
		struct task_struct *p;

		/*
		 * Skipping the current CPU is OK even through we can be
		 * migrated at any point. The current CPU, at the point
		 * where we read raw_smp_processor_id(), is ensured to
		 * be in program order with respect to the caller
		 * thread. Therefore, we can skip this CPU from the
		 * iteration.
		 */
		if (cpu == raw_smp_processor_id())
			continue;

		if (!(READ_ONCE(cpu_rq(cpu)->membarrier_state) &
		    MEMBARRIER_STATE_GLOBAL_EXPEDITED))
			continue;

		/*
		 * Skip the CPU if it runs a kernel thread. The scheduler
		 * leaves the prior task mm in place as an optimization when
		 * scheduling a kthread.
		 */
		p = rcu_dereference(cpu_rq(cpu)->curr);
		if (p->flags & PF_KTHREAD)
			continue;

		__cpumask_set_cpu(cpu, tmpmask);
	}
	rcu_read_unlock();

	preempt_disable();
	smp_call_function_many(tmpmask, ipi_mb, NULL, 1);
	preempt_enable();

	free_cpumask_var(tmpmask);
	cpus_read_unlock();

	/*
	 * Memory barrier on the caller thread _after_ we finished
	 * waiting for the last IPI. Matches memory barriers around
	 * rq->curr modification in scheduler.
	 */
	smp_mb();	/* exit from system call is not a mb */
	return 0;
}

static int membarrier_private_expedited(int flags)
{
	int cpu;
	cpumask_var_t tmpmask;
	struct mm_struct *mm = current->mm;

	if (flags & MEMBARRIER_FLAG_SYNC_CORE) {
		if (!IS_ENABLED(CONFIG_ARCH_HAS_MEMBARRIER_SYNC_CORE))
			return -EINVAL;
		if (!(atomic_read(&mm->membarrier_state) &
		      MEMBARRIER_STATE_PRIVATE_EXPEDITED_SYNC_CORE_READY))
			return -EPERM;
	} else {
		if (!(atomic_read(&mm->membarrier_state) &
		      MEMBARRIER_STATE_PRIVATE_EXPEDITED_READY))
			return -EPERM;
	}

	if (atomic_read(&mm->mm_users) == 1 || num_online_cpus() == 1)
		return 0;

	/*
	 * Matches memory barriers around rq->curr modification in
	 * scheduler.
	 */
	smp_mb();	/* system call entry is not a mb. */

	if (!zalloc_cpumask_var(&tmpmask, GFP_KERNEL))
		return -ENOMEM;

	cpus_read_lock();
	rcu_read_lock();
	for_each_online_cpu(cpu) {
		struct task_struct *p;

		/*
		 * Skipping the current CPU is OK even through we can be
		 * migrated at any point. The current CPU, at the point
		 * where we read raw_smp_processor_id(), is ensured to
		 * be in program order with respect to the caller
		 * thread. Therefore, we can skip this CPU from the
		 * iteration.
		 */
		if (cpu == raw_smp_processor_id())
			continue;
		p = rcu_dereference(cpu_rq(cpu)->curr);
		if (p && p->mm == mm)
			__cpumask_set_cpu(cpu, tmpmask);
	}
	rcu_read_unlock();

	preempt_disable();
	smp_call_function_many(tmpmask, ipi_mb, NULL, 1);
	preempt_enable();

	free_cpumask_var(tmpmask);
	cpus_read_unlock();

	/*
	 * Memory barrier on the caller thread _after_ we finished
	 * waiting for the last IPI. Matches memory barriers around
	 * rq->curr modification in scheduler.
	 */
	smp_mb();	/* exit from system call is not a mb */

	return 0;
}

static int sync_runqueues_membarrier_state(struct mm_struct *mm)
{
	int membarrier_state = atomic_read(&mm->membarrier_state);
	cpumask_var_t tmpmask;
	int cpu;

	if (atomic_read(&mm->mm_users) == 1 || num_online_cpus() == 1) {
		this_cpu_write(runqueues.membarrier_state, membarrier_state);

		/*
		 * For single mm user, we can simply issue a memory barrier
		 * after setting MEMBARRIER_STATE_GLOBAL_EXPEDITED in the
		 * mm and in the current runqueue to guarantee that no memory
		 * access following registration is reordered before
		 * registration.
		 */
		smp_mb();
		return 0;
	}

	if (!zalloc_cpumask_var(&tmpmask, GFP_KERNEL))
		return -ENOMEM;

	/*
	 * For mm with multiple users, we need to ensure all future
	 * scheduler executions will observe @mm's new membarrier
	 * state.
	 */
	synchronize_rcu();

	/*
	 * For each cpu runqueue, if the task's mm match @mm, ensure that all
	 * @mm's membarrier state set bits are also set in in the runqueue's
	 * membarrier state. This ensures that a runqueue scheduling
	 * between threads which are users of @mm has its membarrier state
	 * updated.
	 */
	cpus_read_lock();
	rcu_read_lock();
	for_each_online_cpu(cpu) {
		struct rq *rq = cpu_rq(cpu);
		struct task_struct *p;

		p = rcu_dereference(rq->curr);
		if (p && p->mm == mm)
			__cpumask_set_cpu(cpu, tmpmask);
	}
	rcu_read_unlock();

	preempt_disable();
	smp_call_function_many(tmpmask, ipi_sync_rq_state, mm, 1);
	preempt_enable();

	free_cpumask_var(tmpmask);
	cpus_read_unlock();

	return 0;
}

static int membarrier_register_global_expedited(void)
{
	struct task_struct *p = current;
	struct mm_struct *mm = p->mm;
	int ret;

	if (atomic_read(&mm->membarrier_state) &
	    MEMBARRIER_STATE_GLOBAL_EXPEDITED_READY)
		return 0;
	atomic_or(MEMBARRIER_STATE_GLOBAL_EXPEDITED, &mm->membarrier_state);
	ret = sync_runqueues_membarrier_state(mm);
	if (ret)
		return ret;
	atomic_or(MEMBARRIER_STATE_GLOBAL_EXPEDITED_READY,
		  &mm->membarrier_state);

	return 0;
}

static int membarrier_register_private_expedited(int flags)
{
	struct task_struct *p = current;
	struct mm_struct *mm = p->mm;
	int ready_state = MEMBARRIER_STATE_PRIVATE_EXPEDITED_READY,
	    set_state = MEMBARRIER_STATE_PRIVATE_EXPEDITED,
	    ret;

	if (flags & MEMBARRIER_FLAG_SYNC_CORE) {
		if (!IS_ENABLED(CONFIG_ARCH_HAS_MEMBARRIER_SYNC_CORE))
			return -EINVAL;
		ready_state =
			MEMBARRIER_STATE_PRIVATE_EXPEDITED_SYNC_CORE_READY;
	}

	/*
	 * We need to consider threads belonging to different thread
	 * groups, which use the same mm. (CLONE_VM but not
	 * CLONE_THREAD).
	 */
	if ((atomic_read(&mm->membarrier_state) & ready_state) == ready_state)
		return 0;
	if (flags & MEMBARRIER_FLAG_SYNC_CORE)
		set_state |= MEMBARRIER_STATE_PRIVATE_EXPEDITED_SYNC_CORE;
	atomic_or(set_state, &mm->membarrier_state);
	ret = sync_runqueues_membarrier_state(mm);
	if (ret)
		return ret;
	atomic_or(ready_state, &mm->membarrier_state);

	return 0;
}

/**
 * sys_membarrier - issue memory barriers on a set of threads
 * @cmd:   Takes command values defined in enum membarrier_cmd.
 * @flags: Currently needs to be 0. For future extensions.
 *
 * If this system call is not implemented, -ENOSYS is returned. If the
 * command specified does not exist, not available on the running
 * kernel, or if the command argument is invalid, this system call
 * returns -EINVAL. For a given command, with flags argument set to 0,
 * if this system call returns -ENOSYS or -EINVAL, it is guaranteed to
 * always return the same value until reboot. In addition, it can return
 * -ENOMEM if there is not enough memory available to perform the system
 * call.
 *
 * All memory accesses performed in program order from each targeted thread
 * is guaranteed to be ordered with respect to sys_membarrier(). If we use
 * the semantic "barrier()" to represent a compiler barrier forcing memory
 * accesses to be performed in program order across the barrier, and
 * smp_mb() to represent explicit memory barriers forcing full memory
 * ordering across the barrier, we have the following ordering table for
 * each pair of barrier(), sys_membarrier() and smp_mb():
 *
 * The pair ordering is detailed as (O: ordered, X: not ordered):
 *
 *                        barrier()   smp_mb() sys_membarrier()
 *        barrier()          X           X            O
 *        smp_mb()           X           O            O
 *        sys_membarrier()   O           O            O
 */
SYSCALL_DEFINE2(membarrier, int, cmd, int, flags)
{
	if (unlikely(flags))
		return -EINVAL;
	switch (cmd) {
	case MEMBARRIER_CMD_QUERY:
	{
		int cmd_mask = MEMBARRIER_CMD_BITMASK;

		if (tick_nohz_full_enabled())
			cmd_mask &= ~MEMBARRIER_CMD_GLOBAL;
		return cmd_mask;
	}
	case MEMBARRIER_CMD_GLOBAL:
		/* MEMBARRIER_CMD_GLOBAL is not compatible with nohz_full. */
		if (tick_nohz_full_enabled())
			return -EINVAL;
		if (num_online_cpus() > 1)
			synchronize_rcu();
		return 0;
	case MEMBARRIER_CMD_GLOBAL_EXPEDITED:
		return membarrier_global_expedited();
	case MEMBARRIER_CMD_REGISTER_GLOBAL_EXPEDITED:
		return membarrier_register_global_expedited();
	case MEMBARRIER_CMD_PRIVATE_EXPEDITED:
		return membarrier_private_expedited(0);
	case MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED:
		return membarrier_register_private_expedited(0);
	case MEMBARRIER_CMD_PRIVATE_EXPEDITED_SYNC_CORE:
		return membarrier_private_expedited(MEMBARRIER_FLAG_SYNC_CORE);
	case MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED_SYNC_CORE:
		return membarrier_register_private_expedited(MEMBARRIER_FLAG_SYNC_CORE);
	default:
		return -EINVAL;
	}
}
