// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2010-2017 Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 *
 * membarrier system call
 */
#include "sched.h"

/*
 * For documentation purposes, here are some membarrier ordering
 * scenarios to keep in mind:
 *
 * A) Userspace thread execution after IPI vs membarrier's memory
 *    barrier before sending the IPI
 *
 * Userspace variables:
 *
 * int x = 0, y = 0;
 *
 * The memory barrier at the start of membarrier() on CPU0 is necessary in
 * order to enforce the guarantee that any writes occurring on CPU0 before
 * the membarrier() is executed will be visible to any code executing on
 * CPU1 after the IPI-induced memory barrier:
 *
 *         CPU0                              CPU1
 *
 *         x = 1
 *         membarrier():
 *           a: smp_mb()
 *           b: send IPI                       IPI-induced mb
 *           c: smp_mb()
 *         r2 = y
 *                                           y = 1
 *                                           barrier()
 *                                           r1 = x
 *
 *                     BUG_ON(r1 == 0 && r2 == 0)
 *
 * The write to y and load from x by CPU1 are unordered by the hardware,
 * so it's possible to have "r1 = x" reordered before "y = 1" at any
 * point after (b).  If the memory barrier at (a) is omitted, then "x = 1"
 * can be reordered after (a) (although not after (c)), so we get r1 == 0
 * and r2 == 0.  This violates the guarantee that membarrier() is
 * supposed by provide.
 *
 * The timing of the memory barrier at (a) has to ensure that it executes
 * before the IPI-induced memory barrier on CPU1.
 *
 * B) Userspace thread execution before IPI vs membarrier's memory
 *    barrier after completing the IPI
 *
 * Userspace variables:
 *
 * int x = 0, y = 0;
 *
 * The memory barrier at the end of membarrier() on CPU0 is necessary in
 * order to enforce the guarantee that any writes occurring on CPU1 before
 * the membarrier() is executed will be visible to any code executing on
 * CPU0 after the membarrier():
 *
 *         CPU0                              CPU1
 *
 *                                           x = 1
 *                                           barrier()
 *                                           y = 1
 *         r2 = y
 *         membarrier():
 *           a: smp_mb()
 *           b: send IPI                       IPI-induced mb
 *           c: smp_mb()
 *         r1 = x
 *         BUG_ON(r1 == 0 && r2 == 1)
 *
 * The writes to x and y are unordered by the hardware, so it's possible to
 * have "r2 = 1" even though the write to x doesn't execute until (b).  If
 * the memory barrier at (c) is omitted then "r1 = x" can be reordered
 * before (b) (although not before (a)), so we get "r1 = 0".  This violates
 * the guarantee that membarrier() is supposed to provide.
 *
 * The timing of the memory barrier at (c) has to ensure that it executes
 * after the IPI-induced memory barrier on CPU1.
 *
 * C) Scheduling userspace thread -> kthread -> userspace thread vs membarrier
 *
 *           CPU0                            CPU1
 *
 *           membarrier():
 *           a: smp_mb()
 *                                           d: switch to kthread (includes mb)
 *           b: read rq->curr->mm == NULL
 *                                           e: switch to user (includes mb)
 *           c: smp_mb()
 *
 * Using the scenario from (A), we can show that (a) needs to be paired
 * with (e). Using the scenario from (B), we can show that (c) needs to
 * be paired with (d).
 *
 * D) exit_mm vs membarrier
 *
 * Two thread groups are created, A and B.  Thread group B is created by
 * issuing clone from group A with flag CLONE_VM set, but not CLONE_THREAD.
 * Let's assume we have a single thread within each thread group (Thread A
 * and Thread B).  Thread A runs on CPU0, Thread B runs on CPU1.
 *
 *           CPU0                            CPU1
 *
 *           membarrier():
 *             a: smp_mb()
 *                                           exit_mm():
 *                                             d: smp_mb()
 *                                             e: current->mm = NULL
 *             b: read rq->curr->mm == NULL
 *             c: smp_mb()
 *
 * Using scenario (B), we can show that (c) needs to be paired with (d).
 *
 * E) kthread_{use,unuse}_mm vs membarrier
 *
 *           CPU0                            CPU1
 *
 *           membarrier():
 *           a: smp_mb()
 *                                           kthread_unuse_mm()
 *                                             d: smp_mb()
 *                                             e: current->mm = NULL
 *           b: read rq->curr->mm == NULL
 *                                           kthread_use_mm()
 *                                             f: current->mm = mm
 *                                             g: smp_mb()
 *           c: smp_mb()
 *
 * Using the scenario from (A), we can show that (a) needs to be paired
 * with (g). Using the scenario from (B), we can show that (c) needs to
 * be paired with (d).
 */

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

#ifdef CONFIG_RSEQ
#define MEMBARRIER_PRIVATE_EXPEDITED_RSEQ_BITMASK		\
	(MEMBARRIER_CMD_PRIVATE_EXPEDITED_RSEQ			\
	| MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED_RSEQ)
#else
#define MEMBARRIER_PRIVATE_EXPEDITED_RSEQ_BITMASK	0
#endif

#define MEMBARRIER_CMD_BITMASK						\
	(MEMBARRIER_CMD_GLOBAL | MEMBARRIER_CMD_GLOBAL_EXPEDITED	\
	| MEMBARRIER_CMD_REGISTER_GLOBAL_EXPEDITED			\
	| MEMBARRIER_CMD_PRIVATE_EXPEDITED				\
	| MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED			\
	| MEMBARRIER_PRIVATE_EXPEDITED_SYNC_CORE_BITMASK		\
	| MEMBARRIER_PRIVATE_EXPEDITED_RSEQ_BITMASK)

static void ipi_mb(void *info)
{
	smp_mb();	/* IPIs should be serializing but paranoid. */
}

static void ipi_sync_core(void *info)
{
	/*
	 * The smp_mb() in membarrier after all the IPIs is supposed to
	 * ensure that memory on remote CPUs that occur before the IPI
	 * become visible to membarrier()'s caller -- see scenario B in
	 * the big comment at the top of this file.
	 *
	 * A sync_core() would provide this guarantee, but
	 * sync_core_before_usermode() might end up being deferred until
	 * after membarrier()'s smp_mb().
	 */
	smp_mb();	/* IPIs should be serializing but paranoid. */

	sync_core_before_usermode();
}

static void ipi_rseq(void *info)
{
	/*
	 * Ensure that all stores done by the calling thread are visible
	 * to the current task before the current task resumes.  We could
	 * probably optimize this away on most architectures, but by the
	 * time we've already sent an IPI, the cost of the extra smp_mb()
	 * is negligible.
	 */
	smp_mb();
	rseq_preempt(current);
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

void membarrier_update_current_mm(struct mm_struct *next_mm)
{
	struct rq *rq = this_rq();
	int membarrier_state = 0;

	if (next_mm)
		membarrier_state = atomic_read(&next_mm->membarrier_state);
	if (READ_ONCE(rq->membarrier_state) == membarrier_state)
		return;
	WRITE_ONCE(rq->membarrier_state, membarrier_state);
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
		 * Skip the CPU if it runs a kernel thread which is not using
		 * a task mm.
		 */
		p = rcu_dereference(cpu_rq(cpu)->curr);
		if (!p->mm)
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

static int membarrier_private_expedited(int flags, int cpu_id)
{
	cpumask_var_t tmpmask;
	struct mm_struct *mm = current->mm;
	smp_call_func_t ipi_func = ipi_mb;

	if (flags == MEMBARRIER_FLAG_SYNC_CORE) {
		if (!IS_ENABLED(CONFIG_ARCH_HAS_MEMBARRIER_SYNC_CORE))
			return -EINVAL;
		if (!(atomic_read(&mm->membarrier_state) &
		      MEMBARRIER_STATE_PRIVATE_EXPEDITED_SYNC_CORE_READY))
			return -EPERM;
		ipi_func = ipi_sync_core;
	} else if (flags == MEMBARRIER_FLAG_RSEQ) {
		if (!IS_ENABLED(CONFIG_RSEQ))
			return -EINVAL;
		if (!(atomic_read(&mm->membarrier_state) &
		      MEMBARRIER_STATE_PRIVATE_EXPEDITED_RSEQ_READY))
			return -EPERM;
		ipi_func = ipi_rseq;
	} else {
		WARN_ON_ONCE(flags);
		if (!(atomic_read(&mm->membarrier_state) &
		      MEMBARRIER_STATE_PRIVATE_EXPEDITED_READY))
			return -EPERM;
	}

	if (flags != MEMBARRIER_FLAG_SYNC_CORE &&
	    (atomic_read(&mm->mm_users) == 1 || num_online_cpus() == 1))
		return 0;

	/*
	 * Matches memory barriers around rq->curr modification in
	 * scheduler.
	 */
	smp_mb();	/* system call entry is not a mb. */

	if (cpu_id < 0 && !zalloc_cpumask_var(&tmpmask, GFP_KERNEL))
		return -ENOMEM;

	cpus_read_lock();

	if (cpu_id >= 0) {
		struct task_struct *p;

		if (cpu_id >= nr_cpu_ids || !cpu_online(cpu_id))
			goto out;
		rcu_read_lock();
		p = rcu_dereference(cpu_rq(cpu_id)->curr);
		if (!p || p->mm != mm) {
			rcu_read_unlock();
			goto out;
		}
		rcu_read_unlock();
	} else {
		int cpu;

		rcu_read_lock();
		for_each_online_cpu(cpu) {
			struct task_struct *p;

			p = rcu_dereference(cpu_rq(cpu)->curr);
			if (p && p->mm == mm)
				__cpumask_set_cpu(cpu, tmpmask);
		}
		rcu_read_unlock();
	}

	if (cpu_id >= 0) {
		/*
		 * smp_call_function_single() will call ipi_func() if cpu_id
		 * is the calling CPU.
		 */
		smp_call_function_single(cpu_id, ipi_func, NULL, 1);
	} else {
		/*
		 * For regular membarrier, we can save a few cycles by
		 * skipping the current cpu -- we're about to do smp_mb()
		 * below, and if we migrate to a different cpu, this cpu
		 * and the new cpu will execute a full barrier in the
		 * scheduler.
		 *
		 * For SYNC_CORE, we do need a barrier on the current cpu --
		 * otherwise, if we are migrated and replaced by a different
		 * task in the same mm just before, during, or after
		 * membarrier, we will end up with some thread in the mm
		 * running without a core sync.
		 *
		 * For RSEQ, don't rseq_preempt() the caller.  User code
		 * is not supposed to issue syscalls at all from inside an
		 * rseq critical section.
		 */
		if (flags != MEMBARRIER_FLAG_SYNC_CORE) {
			preempt_disable();
			smp_call_function_many(tmpmask, ipi_func, NULL, true);
			preempt_enable();
		} else {
			on_each_cpu_mask(tmpmask, ipi_func, NULL, true);
		}
	}

out:
	if (cpu_id < 0)
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
	 * @mm's membarrier state set bits are also set in the runqueue's
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

	on_each_cpu_mask(tmpmask, ipi_sync_rq_state, mm, true);

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

	if (flags == MEMBARRIER_FLAG_SYNC_CORE) {
		if (!IS_ENABLED(CONFIG_ARCH_HAS_MEMBARRIER_SYNC_CORE))
			return -EINVAL;
		ready_state =
			MEMBARRIER_STATE_PRIVATE_EXPEDITED_SYNC_CORE_READY;
	} else if (flags == MEMBARRIER_FLAG_RSEQ) {
		if (!IS_ENABLED(CONFIG_RSEQ))
			return -EINVAL;
		ready_state =
			MEMBARRIER_STATE_PRIVATE_EXPEDITED_RSEQ_READY;
	} else {
		WARN_ON_ONCE(flags);
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
	if (flags & MEMBARRIER_FLAG_RSEQ)
		set_state |= MEMBARRIER_STATE_PRIVATE_EXPEDITED_RSEQ;
	atomic_or(set_state, &mm->membarrier_state);
	ret = sync_runqueues_membarrier_state(mm);
	if (ret)
		return ret;
	atomic_or(ready_state, &mm->membarrier_state);

	return 0;
}

/**
 * sys_membarrier - issue memory barriers on a set of threads
 * @cmd:    Takes command values defined in enum membarrier_cmd.
 * @flags:  Currently needs to be 0 for all commands other than
 *          MEMBARRIER_CMD_PRIVATE_EXPEDITED_RSEQ: in the latter
 *          case it can be MEMBARRIER_CMD_FLAG_CPU, indicating that @cpu_id
 *          contains the CPU on which to interrupt (= restart)
 *          the RSEQ critical section.
 * @cpu_id: if @flags == MEMBARRIER_CMD_FLAG_CPU, indicates the cpu on which
 *          RSEQ CS should be interrupted (@cmd must be
 *          MEMBARRIER_CMD_PRIVATE_EXPEDITED_RSEQ).
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
SYSCALL_DEFINE3(membarrier, int, cmd, unsigned int, flags, int, cpu_id)
{
	switch (cmd) {
	case MEMBARRIER_CMD_PRIVATE_EXPEDITED_RSEQ:
		if (unlikely(flags && flags != MEMBARRIER_CMD_FLAG_CPU))
			return -EINVAL;
		break;
	default:
		if (unlikely(flags))
			return -EINVAL;
	}

	if (!(flags & MEMBARRIER_CMD_FLAG_CPU))
		cpu_id = -1;

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
		return membarrier_private_expedited(0, cpu_id);
	case MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED:
		return membarrier_register_private_expedited(0);
	case MEMBARRIER_CMD_PRIVATE_EXPEDITED_SYNC_CORE:
		return membarrier_private_expedited(MEMBARRIER_FLAG_SYNC_CORE, cpu_id);
	case MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED_SYNC_CORE:
		return membarrier_register_private_expedited(MEMBARRIER_FLAG_SYNC_CORE);
	case MEMBARRIER_CMD_PRIVATE_EXPEDITED_RSEQ:
		return membarrier_private_expedited(MEMBARRIER_FLAG_RSEQ, cpu_id);
	case MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED_RSEQ:
		return membarrier_register_private_expedited(MEMBARRIER_FLAG_RSEQ);
	default:
		return -EINVAL;
	}
}
