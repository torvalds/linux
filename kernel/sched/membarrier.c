/*
 * Copyright (C) 2010-2017 Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 *
 * membarrier system call
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
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

static int membarrier_global_expedited(void)
{
	int cpu;
	bool fallback = false;
	cpumask_var_t tmpmask;

	if (num_online_cpus() == 1)
		return 0;

	/*
	 * Matches memory barriers around rq->curr modification in
	 * scheduler.
	 */
	smp_mb();	/* system call entry is not a mb. */

	/*
	 * Expedited membarrier commands guarantee that they won't
	 * block, hence the GFP_NOWAIT allocation flag and fallback
	 * implementation.
	 */
	if (!zalloc_cpumask_var(&tmpmask, GFP_NOWAIT)) {
		/* Fallback for OOM. */
		fallback = true;
	}

	cpus_read_lock();
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

		rcu_read_lock();
		p = task_rcu_dereference(&cpu_rq(cpu)->curr);
		if (p && p->mm && (atomic_read(&p->mm->membarrier_state) &
				   MEMBARRIER_STATE_GLOBAL_EXPEDITED)) {
			if (!fallback)
				__cpumask_set_cpu(cpu, tmpmask);
			else
				smp_call_function_single(cpu, ipi_mb, NULL, 1);
		}
		rcu_read_unlock();
	}
	if (!fallback) {
		preempt_disable();
		smp_call_function_many(tmpmask, ipi_mb, NULL, 1);
		preempt_enable();
		free_cpumask_var(tmpmask);
	}
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
	bool fallback = false;
	cpumask_var_t tmpmask;

	if (flags & MEMBARRIER_FLAG_SYNC_CORE) {
		if (!IS_ENABLED(CONFIG_ARCH_HAS_MEMBARRIER_SYNC_CORE))
			return -EINVAL;
		if (!(atomic_read(&current->mm->membarrier_state) &
		      MEMBARRIER_STATE_PRIVATE_EXPEDITED_SYNC_CORE_READY))
			return -EPERM;
	} else {
		if (!(atomic_read(&current->mm->membarrier_state) &
		      MEMBARRIER_STATE_PRIVATE_EXPEDITED_READY))
			return -EPERM;
	}

	if (num_online_cpus() == 1)
		return 0;

	/*
	 * Matches memory barriers around rq->curr modification in
	 * scheduler.
	 */
	smp_mb();	/* system call entry is not a mb. */

	/*
	 * Expedited membarrier commands guarantee that they won't
	 * block, hence the GFP_NOWAIT allocation flag and fallback
	 * implementation.
	 */
	if (!zalloc_cpumask_var(&tmpmask, GFP_NOWAIT)) {
		/* Fallback for OOM. */
		fallback = true;
	}

	cpus_read_lock();
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
		rcu_read_lock();
		p = task_rcu_dereference(&cpu_rq(cpu)->curr);
		if (p && p->mm == current->mm) {
			if (!fallback)
				__cpumask_set_cpu(cpu, tmpmask);
			else
				smp_call_function_single(cpu, ipi_mb, NULL, 1);
		}
		rcu_read_unlock();
	}
	if (!fallback) {
		preempt_disable();
		smp_call_function_many(tmpmask, ipi_mb, NULL, 1);
		preempt_enable();
		free_cpumask_var(tmpmask);
	}
	cpus_read_unlock();

	/*
	 * Memory barrier on the caller thread _after_ we finished
	 * waiting for the last IPI. Matches memory barriers around
	 * rq->curr modification in scheduler.
	 */
	smp_mb();	/* exit from system call is not a mb */

	return 0;
}

static int membarrier_register_global_expedited(void)
{
	struct task_struct *p = current;
	struct mm_struct *mm = p->mm;

	if (atomic_read(&mm->membarrier_state) &
	    MEMBARRIER_STATE_GLOBAL_EXPEDITED_READY)
		return 0;
	atomic_or(MEMBARRIER_STATE_GLOBAL_EXPEDITED, &mm->membarrier_state);
	if (atomic_read(&mm->mm_users) == 1 && get_nr_threads(p) == 1) {
		/*
		 * For single mm user, single threaded process, we can
		 * simply issue a memory barrier after setting
		 * MEMBARRIER_STATE_GLOBAL_EXPEDITED to guarantee that
		 * no memory access following registration is reordered
		 * before registration.
		 */
		smp_mb();
	} else {
		/*
		 * For multi-mm user threads, we need to ensure all
		 * future scheduler executions will observe the new
		 * thread flag state for this mm.
		 */
		synchronize_sched();
	}
	atomic_or(MEMBARRIER_STATE_GLOBAL_EXPEDITED_READY,
		  &mm->membarrier_state);

	return 0;
}

static int membarrier_register_private_expedited(int flags)
{
	struct task_struct *p = current;
	struct mm_struct *mm = p->mm;
	int state = MEMBARRIER_STATE_PRIVATE_EXPEDITED_READY;

	if (flags & MEMBARRIER_FLAG_SYNC_CORE) {
		if (!IS_ENABLED(CONFIG_ARCH_HAS_MEMBARRIER_SYNC_CORE))
			return -EINVAL;
		state = MEMBARRIER_STATE_PRIVATE_EXPEDITED_SYNC_CORE_READY;
	}

	/*
	 * We need to consider threads belonging to different thread
	 * groups, which use the same mm. (CLONE_VM but not
	 * CLONE_THREAD).
	 */
	if ((atomic_read(&mm->membarrier_state) & state) == state)
		return 0;
	atomic_or(MEMBARRIER_STATE_PRIVATE_EXPEDITED, &mm->membarrier_state);
	if (flags & MEMBARRIER_FLAG_SYNC_CORE)
		atomic_or(MEMBARRIER_STATE_PRIVATE_EXPEDITED_SYNC_CORE,
			  &mm->membarrier_state);
	if (!(atomic_read(&mm->mm_users) == 1 && get_nr_threads(p) == 1)) {
		/*
		 * Ensure all future scheduler executions will observe the
		 * new thread flag state for this process.
		 */
		synchronize_sched();
	}
	atomic_or(state, &mm->membarrier_state);

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
 * this system call is guaranteed to always return the same value until
 * reboot.
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
			synchronize_sched();
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
