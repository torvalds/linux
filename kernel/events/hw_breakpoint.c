// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2007 Alan Stern
 * Copyright (C) IBM Corporation, 2009
 * Copyright (C) 2009, Frederic Weisbecker <fweisbec@gmail.com>
 *
 * Thanks to Ingo Molnar for his many suggestions.
 *
 * Authors: Alan Stern <stern@rowland.harvard.edu>
 *          K.Prasad <prasad@linux.vnet.ibm.com>
 *          Frederic Weisbecker <fweisbec@gmail.com>
 */

/*
 * HW_breakpoint: a unified kernel/user-space hardware breakpoint facility,
 * using the CPU's debug registers.
 * This file contains the arch-independent routines.
 */

#include <linux/hw_breakpoint.h>

#include <linux/atomic.h>
#include <linux/bug.h>
#include <linux/cpu.h>
#include <linux/export.h>
#include <linux/init.h>
#include <linux/irqflags.h>
#include <linux/kdebug.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/notifier.h>
#include <linux/percpu-rwsem.h>
#include <linux/percpu.h>
#include <linux/rhashtable.h>
#include <linux/sched.h>
#include <linux/slab.h>

/*
 * Datastructure to track the total uses of N slots across tasks or CPUs;
 * bp_slots_histogram::count[N] is the number of assigned N+1 breakpoint slots.
 */
struct bp_slots_histogram {
#ifdef hw_breakpoint_slots
	atomic_t count[hw_breakpoint_slots(0)];
#else
	atomic_t *count;
#endif
};

/*
 * Per-CPU constraints data.
 */
struct bp_cpuinfo {
	/* Number of pinned CPU breakpoints in a CPU. */
	unsigned int			cpu_pinned;
	/* Histogram of pinned task breakpoints in a CPU. */
	struct bp_slots_histogram	tsk_pinned;
};

static DEFINE_PER_CPU(struct bp_cpuinfo, bp_cpuinfo[TYPE_MAX]);

static struct bp_cpuinfo *get_bp_info(int cpu, enum bp_type_idx type)
{
	return per_cpu_ptr(bp_cpuinfo + type, cpu);
}

/* Number of pinned CPU breakpoints globally. */
static struct bp_slots_histogram cpu_pinned[TYPE_MAX];
/* Number of pinned CPU-independent task breakpoints. */
static struct bp_slots_histogram tsk_pinned_all[TYPE_MAX];

/* Keep track of the breakpoints attached to tasks */
static struct rhltable task_bps_ht;
static const struct rhashtable_params task_bps_ht_params = {
	.head_offset = offsetof(struct hw_perf_event, bp_list),
	.key_offset = offsetof(struct hw_perf_event, target),
	.key_len = sizeof_field(struct hw_perf_event, target),
	.automatic_shrinking = true,
};

static bool constraints_initialized __ro_after_init;

/*
 * Synchronizes accesses to the per-CPU constraints; the locking rules are:
 *
 *  1. Atomic updates to bp_cpuinfo::tsk_pinned only require a held read-lock
 *     (due to bp_slots_histogram::count being atomic, no update are lost).
 *
 *  2. Holding a write-lock is required for computations that require a
 *     stable snapshot of all bp_cpuinfo::tsk_pinned.
 *
 *  3. In all other cases, non-atomic accesses require the appropriately held
 *     lock (read-lock for read-only accesses; write-lock for reads/writes).
 */
DEFINE_STATIC_PERCPU_RWSEM(bp_cpuinfo_sem);

/*
 * Return mutex to serialize accesses to per-task lists in task_bps_ht. Since
 * rhltable synchronizes concurrent insertions/deletions, independent tasks may
 * insert/delete concurrently; therefore, a mutex per task is sufficient.
 *
 * Uses task_struct::perf_event_mutex, to avoid extending task_struct with a
 * hw_breakpoint-only mutex, which may be infrequently used. The caveat here is
 * that hw_breakpoint may contend with per-task perf event list management. The
 * assumption is that perf usecases involving hw_breakpoints are very unlikely
 * to result in unnecessary contention.
 */
static inline struct mutex *get_task_bps_mutex(struct perf_event *bp)
{
	struct task_struct *tsk = bp->hw.target;

	return tsk ? &tsk->perf_event_mutex : NULL;
}

static struct mutex *bp_constraints_lock(struct perf_event *bp)
{
	struct mutex *tsk_mtx = get_task_bps_mutex(bp);

	if (tsk_mtx) {
		/*
		 * Fully analogous to the perf_try_init_event() nesting
		 * argument in the comment near perf_event_ctx_lock_nested();
		 * this child->perf_event_mutex cannot ever deadlock against
		 * the parent->perf_event_mutex usage from
		 * perf_event_task_{en,dis}able().
		 *
		 * Specifically, inherited events will never occur on
		 * ->perf_event_list.
		 */
		mutex_lock_nested(tsk_mtx, SINGLE_DEPTH_NESTING);
		percpu_down_read(&bp_cpuinfo_sem);
	} else {
		percpu_down_write(&bp_cpuinfo_sem);
	}

	return tsk_mtx;
}

static void bp_constraints_unlock(struct mutex *tsk_mtx)
{
	if (tsk_mtx) {
		percpu_up_read(&bp_cpuinfo_sem);
		mutex_unlock(tsk_mtx);
	} else {
		percpu_up_write(&bp_cpuinfo_sem);
	}
}

static bool bp_constraints_is_locked(struct perf_event *bp)
{
	struct mutex *tsk_mtx = get_task_bps_mutex(bp);

	return percpu_is_write_locked(&bp_cpuinfo_sem) ||
	       (tsk_mtx ? mutex_is_locked(tsk_mtx) :
			  percpu_is_read_locked(&bp_cpuinfo_sem));
}

static inline void assert_bp_constraints_lock_held(struct perf_event *bp)
{
	struct mutex *tsk_mtx = get_task_bps_mutex(bp);

	if (tsk_mtx)
		lockdep_assert_held(tsk_mtx);
	lockdep_assert_held(&bp_cpuinfo_sem);
}

#ifdef hw_breakpoint_slots
/*
 * Number of breakpoint slots is constant, and the same for all types.
 */
static_assert(hw_breakpoint_slots(TYPE_INST) == hw_breakpoint_slots(TYPE_DATA));
static inline int hw_breakpoint_slots_cached(int type)	{ return hw_breakpoint_slots(type); }
static inline int init_breakpoint_slots(void)		{ return 0; }
#else
/*
 * Dynamic number of breakpoint slots.
 */
static int __nr_bp_slots[TYPE_MAX] __ro_after_init;

static inline int hw_breakpoint_slots_cached(int type)
{
	return __nr_bp_slots[type];
}

static __init bool
bp_slots_histogram_alloc(struct bp_slots_histogram *hist, enum bp_type_idx type)
{
	hist->count = kcalloc(hw_breakpoint_slots_cached(type), sizeof(*hist->count), GFP_KERNEL);
	return hist->count;
}

static __init void bp_slots_histogram_free(struct bp_slots_histogram *hist)
{
	kfree(hist->count);
}

static __init int init_breakpoint_slots(void)
{
	int i, cpu, err_cpu;

	for (i = 0; i < TYPE_MAX; i++)
		__nr_bp_slots[i] = hw_breakpoint_slots(i);

	for_each_possible_cpu(cpu) {
		for (i = 0; i < TYPE_MAX; i++) {
			struct bp_cpuinfo *info = get_bp_info(cpu, i);

			if (!bp_slots_histogram_alloc(&info->tsk_pinned, i))
				goto err;
		}
	}
	for (i = 0; i < TYPE_MAX; i++) {
		if (!bp_slots_histogram_alloc(&cpu_pinned[i], i))
			goto err;
		if (!bp_slots_histogram_alloc(&tsk_pinned_all[i], i))
			goto err;
	}

	return 0;
err:
	for_each_possible_cpu(err_cpu) {
		for (i = 0; i < TYPE_MAX; i++)
			bp_slots_histogram_free(&get_bp_info(err_cpu, i)->tsk_pinned);
		if (err_cpu == cpu)
			break;
	}
	for (i = 0; i < TYPE_MAX; i++) {
		bp_slots_histogram_free(&cpu_pinned[i]);
		bp_slots_histogram_free(&tsk_pinned_all[i]);
	}

	return -ENOMEM;
}
#endif

static inline void
bp_slots_histogram_add(struct bp_slots_histogram *hist, int old, int val)
{
	const int old_idx = old - 1;
	const int new_idx = old_idx + val;

	if (old_idx >= 0)
		WARN_ON(atomic_dec_return_relaxed(&hist->count[old_idx]) < 0);
	if (new_idx >= 0)
		WARN_ON(atomic_inc_return_relaxed(&hist->count[new_idx]) < 0);
}

static int
bp_slots_histogram_max(struct bp_slots_histogram *hist, enum bp_type_idx type)
{
	for (int i = hw_breakpoint_slots_cached(type) - 1; i >= 0; i--) {
		const int count = atomic_read(&hist->count[i]);

		/* Catch unexpected writers; we want a stable snapshot. */
		ASSERT_EXCLUSIVE_WRITER(hist->count[i]);
		if (count > 0)
			return i + 1;
		WARN(count < 0, "inconsistent breakpoint slots histogram");
	}

	return 0;
}

static int
bp_slots_histogram_max_merge(struct bp_slots_histogram *hist1, struct bp_slots_histogram *hist2,
			     enum bp_type_idx type)
{
	for (int i = hw_breakpoint_slots_cached(type) - 1; i >= 0; i--) {
		const int count1 = atomic_read(&hist1->count[i]);
		const int count2 = atomic_read(&hist2->count[i]);

		/* Catch unexpected writers; we want a stable snapshot. */
		ASSERT_EXCLUSIVE_WRITER(hist1->count[i]);
		ASSERT_EXCLUSIVE_WRITER(hist2->count[i]);
		if (count1 + count2 > 0)
			return i + 1;
		WARN(count1 < 0, "inconsistent breakpoint slots histogram");
		WARN(count2 < 0, "inconsistent breakpoint slots histogram");
	}

	return 0;
}

#ifndef hw_breakpoint_weight
static inline int hw_breakpoint_weight(struct perf_event *bp)
{
	return 1;
}
#endif

static inline enum bp_type_idx find_slot_idx(u64 bp_type)
{
	if (bp_type & HW_BREAKPOINT_RW)
		return TYPE_DATA;

	return TYPE_INST;
}

/*
 * Return the maximum number of pinned breakpoints a task has in this CPU.
 */
static unsigned int max_task_bp_pinned(int cpu, enum bp_type_idx type)
{
	struct bp_slots_histogram *tsk_pinned = &get_bp_info(cpu, type)->tsk_pinned;

	/*
	 * At this point we want to have acquired the bp_cpuinfo_sem as a
	 * writer to ensure that there are no concurrent writers in
	 * toggle_bp_task_slot() to tsk_pinned, and we get a stable snapshot.
	 */
	lockdep_assert_held_write(&bp_cpuinfo_sem);
	return bp_slots_histogram_max_merge(tsk_pinned, &tsk_pinned_all[type], type);
}

/*
 * Count the number of breakpoints of the same type and same task.
 * The given event must be not on the list.
 *
 * If @cpu is -1, but the result of task_bp_pinned() is not CPU-independent,
 * returns a negative value.
 */
static int task_bp_pinned(int cpu, struct perf_event *bp, enum bp_type_idx type)
{
	struct rhlist_head *head, *pos;
	struct perf_event *iter;
	int count = 0;

	/*
	 * We need a stable snapshot of the per-task breakpoint list.
	 */
	assert_bp_constraints_lock_held(bp);

	rcu_read_lock();
	head = rhltable_lookup(&task_bps_ht, &bp->hw.target, task_bps_ht_params);
	if (!head)
		goto out;

	rhl_for_each_entry_rcu(iter, pos, head, hw.bp_list) {
		if (find_slot_idx(iter->attr.bp_type) != type)
			continue;

		if (iter->cpu >= 0) {
			if (cpu == -1) {
				count = -1;
				goto out;
			} else if (cpu != iter->cpu)
				continue;
		}

		count += hw_breakpoint_weight(iter);
	}

out:
	rcu_read_unlock();
	return count;
}

static const struct cpumask *cpumask_of_bp(struct perf_event *bp)
{
	if (bp->cpu >= 0)
		return cpumask_of(bp->cpu);
	return cpu_possible_mask;
}

/*
 * Returns the max pinned breakpoint slots in a given
 * CPU (cpu > -1) or across all of them (cpu = -1).
 */
static int
max_bp_pinned_slots(struct perf_event *bp, enum bp_type_idx type)
{
	const struct cpumask *cpumask = cpumask_of_bp(bp);
	int pinned_slots = 0;
	int cpu;

	if (bp->hw.target && bp->cpu < 0) {
		int max_pinned = task_bp_pinned(-1, bp, type);

		if (max_pinned >= 0) {
			/*
			 * Fast path: task_bp_pinned() is CPU-independent and
			 * returns the same value for any CPU.
			 */
			max_pinned += bp_slots_histogram_max(&cpu_pinned[type], type);
			return max_pinned;
		}
	}

	for_each_cpu(cpu, cpumask) {
		struct bp_cpuinfo *info = get_bp_info(cpu, type);
		int nr;

		nr = info->cpu_pinned;
		if (!bp->hw.target)
			nr += max_task_bp_pinned(cpu, type);
		else
			nr += task_bp_pinned(cpu, bp, type);

		pinned_slots = max(nr, pinned_slots);
	}

	return pinned_slots;
}

/*
 * Add/remove the given breakpoint in our constraint table
 */
static int
toggle_bp_slot(struct perf_event *bp, bool enable, enum bp_type_idx type, int weight)
{
	int cpu, next_tsk_pinned;

	if (!enable)
		weight = -weight;

	if (!bp->hw.target) {
		/*
		 * Update the pinned CPU slots, in per-CPU bp_cpuinfo and in the
		 * global histogram.
		 */
		struct bp_cpuinfo *info = get_bp_info(bp->cpu, type);

		lockdep_assert_held_write(&bp_cpuinfo_sem);
		bp_slots_histogram_add(&cpu_pinned[type], info->cpu_pinned, weight);
		info->cpu_pinned += weight;
		return 0;
	}

	/*
	 * If bp->hw.target, tsk_pinned is only modified, but not used
	 * otherwise. We can permit concurrent updates as long as there are no
	 * other uses: having acquired bp_cpuinfo_sem as a reader allows
	 * concurrent updates here. Uses of tsk_pinned will require acquiring
	 * bp_cpuinfo_sem as a writer to stabilize tsk_pinned's value.
	 */
	lockdep_assert_held_read(&bp_cpuinfo_sem);

	/*
	 * Update the pinned task slots, in per-CPU bp_cpuinfo and in the global
	 * histogram. We need to take care of 4 cases:
	 *
	 *  1. This breakpoint targets all CPUs (cpu < 0), and there may only
	 *     exist other task breakpoints targeting all CPUs. In this case we
	 *     can simply update the global slots histogram.
	 *
	 *  2. This breakpoint targets a specific CPU (cpu >= 0), but there may
	 *     only exist other task breakpoints targeting all CPUs.
	 *
	 *     a. On enable: remove the existing breakpoints from the global
	 *        slots histogram and use the per-CPU histogram.
	 *
	 *     b. On disable: re-insert the existing breakpoints into the global
	 *        slots histogram and remove from per-CPU histogram.
	 *
	 *  3. Some other existing task breakpoints target specific CPUs. Only
	 *     update the per-CPU slots histogram.
	 */

	if (!enable) {
		/*
		 * Remove before updating histograms so we can determine if this
		 * was the last task breakpoint for a specific CPU.
		 */
		int ret = rhltable_remove(&task_bps_ht, &bp->hw.bp_list, task_bps_ht_params);

		if (ret)
			return ret;
	}
	/*
	 * Note: If !enable, next_tsk_pinned will not count the to-be-removed breakpoint.
	 */
	next_tsk_pinned = task_bp_pinned(-1, bp, type);

	if (next_tsk_pinned >= 0) {
		if (bp->cpu < 0) { /* Case 1: fast path */
			if (!enable)
				next_tsk_pinned += hw_breakpoint_weight(bp);
			bp_slots_histogram_add(&tsk_pinned_all[type], next_tsk_pinned, weight);
		} else if (enable) { /* Case 2.a: slow path */
			/* Add existing to per-CPU histograms. */
			for_each_possible_cpu(cpu) {
				bp_slots_histogram_add(&get_bp_info(cpu, type)->tsk_pinned,
						       0, next_tsk_pinned);
			}
			/* Add this first CPU-pinned task breakpoint. */
			bp_slots_histogram_add(&get_bp_info(bp->cpu, type)->tsk_pinned,
					       next_tsk_pinned, weight);
			/* Rebalance global task pinned histogram. */
			bp_slots_histogram_add(&tsk_pinned_all[type], next_tsk_pinned,
					       -next_tsk_pinned);
		} else { /* Case 2.b: slow path */
			/* Remove this last CPU-pinned task breakpoint. */
			bp_slots_histogram_add(&get_bp_info(bp->cpu, type)->tsk_pinned,
					       next_tsk_pinned + hw_breakpoint_weight(bp), weight);
			/* Remove all from per-CPU histograms. */
			for_each_possible_cpu(cpu) {
				bp_slots_histogram_add(&get_bp_info(cpu, type)->tsk_pinned,
						       next_tsk_pinned, -next_tsk_pinned);
			}
			/* Rebalance global task pinned histogram. */
			bp_slots_histogram_add(&tsk_pinned_all[type], 0, next_tsk_pinned);
		}
	} else { /* Case 3: slow path */
		const struct cpumask *cpumask = cpumask_of_bp(bp);

		for_each_cpu(cpu, cpumask) {
			next_tsk_pinned = task_bp_pinned(cpu, bp, type);
			if (!enable)
				next_tsk_pinned += hw_breakpoint_weight(bp);
			bp_slots_histogram_add(&get_bp_info(cpu, type)->tsk_pinned,
					       next_tsk_pinned, weight);
		}
	}

	/*
	 * Readers want a stable snapshot of the per-task breakpoint list.
	 */
	assert_bp_constraints_lock_held(bp);

	if (enable)
		return rhltable_insert(&task_bps_ht, &bp->hw.bp_list, task_bps_ht_params);

	return 0;
}

/*
 * Constraints to check before allowing this new breakpoint counter.
 *
 * Note: Flexible breakpoints are currently unimplemented, but outlined in the
 * below algorithm for completeness.  The implementation treats flexible as
 * pinned due to no guarantee that we currently always schedule flexible events
 * before a pinned event in a same CPU.
 *
 *  == Non-pinned counter == (Considered as pinned for now)
 *
 *   - If attached to a single cpu, check:
 *
 *       (per_cpu(info->flexible, cpu) || (per_cpu(info->cpu_pinned, cpu)
 *           + max(per_cpu(info->tsk_pinned, cpu)))) < HBP_NUM
 *
 *       -> If there are already non-pinned counters in this cpu, it means
 *          there is already a free slot for them.
 *          Otherwise, we check that the maximum number of per task
 *          breakpoints (for this cpu) plus the number of per cpu breakpoint
 *          (for this cpu) doesn't cover every registers.
 *
 *   - If attached to every cpus, check:
 *
 *       (per_cpu(info->flexible, *) || (max(per_cpu(info->cpu_pinned, *))
 *           + max(per_cpu(info->tsk_pinned, *)))) < HBP_NUM
 *
 *       -> This is roughly the same, except we check the number of per cpu
 *          bp for every cpu and we keep the max one. Same for the per tasks
 *          breakpoints.
 *
 *
 * == Pinned counter ==
 *
 *   - If attached to a single cpu, check:
 *
 *       ((per_cpu(info->flexible, cpu) > 1) + per_cpu(info->cpu_pinned, cpu)
 *            + max(per_cpu(info->tsk_pinned, cpu))) < HBP_NUM
 *
 *       -> Same checks as before. But now the info->flexible, if any, must keep
 *          one register at least (or they will never be fed).
 *
 *   - If attached to every cpus, check:
 *
 *       ((per_cpu(info->flexible, *) > 1) + max(per_cpu(info->cpu_pinned, *))
 *            + max(per_cpu(info->tsk_pinned, *))) < HBP_NUM
 */
static int __reserve_bp_slot(struct perf_event *bp, u64 bp_type)
{
	enum bp_type_idx type;
	int max_pinned_slots;
	int weight;

	/* We couldn't initialize breakpoint constraints on boot */
	if (!constraints_initialized)
		return -ENOMEM;

	/* Basic checks */
	if (bp_type == HW_BREAKPOINT_EMPTY ||
	    bp_type == HW_BREAKPOINT_INVALID)
		return -EINVAL;

	type = find_slot_idx(bp_type);
	weight = hw_breakpoint_weight(bp);

	/* Check if this new breakpoint can be satisfied across all CPUs. */
	max_pinned_slots = max_bp_pinned_slots(bp, type) + weight;
	if (max_pinned_slots > hw_breakpoint_slots_cached(type))
		return -ENOSPC;

	return toggle_bp_slot(bp, true, type, weight);
}

int reserve_bp_slot(struct perf_event *bp)
{
	struct mutex *mtx = bp_constraints_lock(bp);
	int ret = __reserve_bp_slot(bp, bp->attr.bp_type);

	bp_constraints_unlock(mtx);
	return ret;
}

static void __release_bp_slot(struct perf_event *bp, u64 bp_type)
{
	enum bp_type_idx type;
	int weight;

	type = find_slot_idx(bp_type);
	weight = hw_breakpoint_weight(bp);
	WARN_ON(toggle_bp_slot(bp, false, type, weight));
}

void release_bp_slot(struct perf_event *bp)
{
	struct mutex *mtx = bp_constraints_lock(bp);

	__release_bp_slot(bp, bp->attr.bp_type);
	bp_constraints_unlock(mtx);
}

static int __modify_bp_slot(struct perf_event *bp, u64 old_type, u64 new_type)
{
	int err;

	__release_bp_slot(bp, old_type);

	err = __reserve_bp_slot(bp, new_type);
	if (err) {
		/*
		 * Reserve the old_type slot back in case
		 * there's no space for the new type.
		 *
		 * This must succeed, because we just released
		 * the old_type slot in the __release_bp_slot
		 * call above. If not, something is broken.
		 */
		WARN_ON(__reserve_bp_slot(bp, old_type));
	}

	return err;
}

static int modify_bp_slot(struct perf_event *bp, u64 old_type, u64 new_type)
{
	struct mutex *mtx = bp_constraints_lock(bp);
	int ret = __modify_bp_slot(bp, old_type, new_type);

	bp_constraints_unlock(mtx);
	return ret;
}

/*
 * Allow the kernel debugger to reserve breakpoint slots without
 * taking a lock using the dbg_* variant of for the reserve and
 * release breakpoint slots.
 */
int dbg_reserve_bp_slot(struct perf_event *bp)
{
	int ret;

	if (bp_constraints_is_locked(bp))
		return -1;

	/* Locks aren't held; disable lockdep assert checking. */
	lockdep_off();
	ret = __reserve_bp_slot(bp, bp->attr.bp_type);
	lockdep_on();

	return ret;
}

int dbg_release_bp_slot(struct perf_event *bp)
{
	if (bp_constraints_is_locked(bp))
		return -1;

	/* Locks aren't held; disable lockdep assert checking. */
	lockdep_off();
	__release_bp_slot(bp, bp->attr.bp_type);
	lockdep_on();

	return 0;
}

static int hw_breakpoint_parse(struct perf_event *bp,
			       const struct perf_event_attr *attr,
			       struct arch_hw_breakpoint *hw)
{
	int err;

	err = hw_breakpoint_arch_parse(bp, attr, hw);
	if (err)
		return err;

	if (arch_check_bp_in_kernelspace(hw)) {
		if (attr->exclude_kernel)
			return -EINVAL;
		/*
		 * Don't let unprivileged users set a breakpoint in the trap
		 * path to avoid trap recursion attacks.
		 */
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
	}

	return 0;
}

int register_perf_hw_breakpoint(struct perf_event *bp)
{
	struct arch_hw_breakpoint hw = { };
	int err;

	err = reserve_bp_slot(bp);
	if (err)
		return err;

	err = hw_breakpoint_parse(bp, &bp->attr, &hw);
	if (err) {
		release_bp_slot(bp);
		return err;
	}

	bp->hw.info = hw;

	return 0;
}

/**
 * register_user_hw_breakpoint - register a hardware breakpoint for user space
 * @attr: breakpoint attributes
 * @triggered: callback to trigger when we hit the breakpoint
 * @context: context data could be used in the triggered callback
 * @tsk: pointer to 'task_struct' of the process to which the address belongs
 */
struct perf_event *
register_user_hw_breakpoint(struct perf_event_attr *attr,
			    perf_overflow_handler_t triggered,
			    void *context,
			    struct task_struct *tsk)
{
	return perf_event_create_kernel_counter(attr, -1, tsk, triggered,
						context);
}
EXPORT_SYMBOL_GPL(register_user_hw_breakpoint);

static void hw_breakpoint_copy_attr(struct perf_event_attr *to,
				    struct perf_event_attr *from)
{
	to->bp_addr = from->bp_addr;
	to->bp_type = from->bp_type;
	to->bp_len  = from->bp_len;
	to->disabled = from->disabled;
}

int
modify_user_hw_breakpoint_check(struct perf_event *bp, struct perf_event_attr *attr,
			        bool check)
{
	struct arch_hw_breakpoint hw = { };
	int err;

	err = hw_breakpoint_parse(bp, attr, &hw);
	if (err)
		return err;

	if (check) {
		struct perf_event_attr old_attr;

		old_attr = bp->attr;
		hw_breakpoint_copy_attr(&old_attr, attr);
		if (memcmp(&old_attr, attr, sizeof(*attr)))
			return -EINVAL;
	}

	if (bp->attr.bp_type != attr->bp_type) {
		err = modify_bp_slot(bp, bp->attr.bp_type, attr->bp_type);
		if (err)
			return err;
	}

	hw_breakpoint_copy_attr(&bp->attr, attr);
	bp->hw.info = hw;

	return 0;
}

/**
 * modify_user_hw_breakpoint - modify a user-space hardware breakpoint
 * @bp: the breakpoint structure to modify
 * @attr: new breakpoint attributes
 */
int modify_user_hw_breakpoint(struct perf_event *bp, struct perf_event_attr *attr)
{
	int err;

	/*
	 * modify_user_hw_breakpoint can be invoked with IRQs disabled and hence it
	 * will not be possible to raise IPIs that invoke __perf_event_disable.
	 * So call the function directly after making sure we are targeting the
	 * current task.
	 */
	if (irqs_disabled() && bp->ctx && bp->ctx->task == current)
		perf_event_disable_local(bp);
	else
		perf_event_disable(bp);

	err = modify_user_hw_breakpoint_check(bp, attr, false);

	if (!bp->attr.disabled)
		perf_event_enable(bp);

	return err;
}
EXPORT_SYMBOL_GPL(modify_user_hw_breakpoint);

/**
 * unregister_hw_breakpoint - unregister a user-space hardware breakpoint
 * @bp: the breakpoint structure to unregister
 */
void unregister_hw_breakpoint(struct perf_event *bp)
{
	if (!bp)
		return;
	perf_event_release_kernel(bp);
}
EXPORT_SYMBOL_GPL(unregister_hw_breakpoint);

/**
 * register_wide_hw_breakpoint - register a wide breakpoint in the kernel
 * @attr: breakpoint attributes
 * @triggered: callback to trigger when we hit the breakpoint
 * @context: context data could be used in the triggered callback
 *
 * @return a set of per_cpu pointers to perf events
 */
struct perf_event * __percpu *
register_wide_hw_breakpoint(struct perf_event_attr *attr,
			    perf_overflow_handler_t triggered,
			    void *context)
{
	struct perf_event * __percpu *cpu_events, *bp;
	long err = 0;
	int cpu;

	cpu_events = alloc_percpu(typeof(*cpu_events));
	if (!cpu_events)
		return ERR_PTR_PCPU(-ENOMEM);

	cpus_read_lock();
	for_each_online_cpu(cpu) {
		bp = perf_event_create_kernel_counter(attr, cpu, NULL,
						      triggered, context);
		if (IS_ERR(bp)) {
			err = PTR_ERR(bp);
			break;
		}

		per_cpu(*cpu_events, cpu) = bp;
	}
	cpus_read_unlock();

	if (likely(!err))
		return cpu_events;

	unregister_wide_hw_breakpoint(cpu_events);
	return ERR_PTR_PCPU(err);
}
EXPORT_SYMBOL_GPL(register_wide_hw_breakpoint);

/**
 * unregister_wide_hw_breakpoint - unregister a wide breakpoint in the kernel
 * @cpu_events: the per cpu set of events to unregister
 */
void unregister_wide_hw_breakpoint(struct perf_event * __percpu *cpu_events)
{
	int cpu;

	for_each_possible_cpu(cpu)
		unregister_hw_breakpoint(per_cpu(*cpu_events, cpu));

	free_percpu(cpu_events);
}
EXPORT_SYMBOL_GPL(unregister_wide_hw_breakpoint);

/**
 * hw_breakpoint_is_used - check if breakpoints are currently used
 *
 * Returns: true if breakpoints are used, false otherwise.
 */
bool hw_breakpoint_is_used(void)
{
	int cpu;

	if (!constraints_initialized)
		return false;

	for_each_possible_cpu(cpu) {
		for (int type = 0; type < TYPE_MAX; ++type) {
			struct bp_cpuinfo *info = get_bp_info(cpu, type);

			if (info->cpu_pinned)
				return true;

			for (int slot = 0; slot < hw_breakpoint_slots_cached(type); ++slot) {
				if (atomic_read(&info->tsk_pinned.count[slot]))
					return true;
			}
		}
	}

	for (int type = 0; type < TYPE_MAX; ++type) {
		for (int slot = 0; slot < hw_breakpoint_slots_cached(type); ++slot) {
			/*
			 * Warn, because if there are CPU pinned counters,
			 * should never get here; bp_cpuinfo::cpu_pinned should
			 * be consistent with the global cpu_pinned histogram.
			 */
			if (WARN_ON(atomic_read(&cpu_pinned[type].count[slot])))
				return true;

			if (atomic_read(&tsk_pinned_all[type].count[slot]))
				return true;
		}
	}

	return false;
}

static struct notifier_block hw_breakpoint_exceptions_nb = {
	.notifier_call = hw_breakpoint_exceptions_notify,
	/* we need to be notified first */
	.priority = 0x7fffffff
};

static void bp_perf_event_destroy(struct perf_event *event)
{
	release_bp_slot(event);
}

static int hw_breakpoint_event_init(struct perf_event *bp)
{
	int err;

	if (bp->attr.type != PERF_TYPE_BREAKPOINT)
		return -ENOENT;

	/*
	 * Check if breakpoint type is supported before proceeding.
	 * Also, no branch sampling for breakpoint events.
	 */
	if (!hw_breakpoint_slots_cached(find_slot_idx(bp->attr.bp_type)) || has_branch_stack(bp))
		return -EOPNOTSUPP;

	err = register_perf_hw_breakpoint(bp);
	if (err)
		return err;

	bp->destroy = bp_perf_event_destroy;

	return 0;
}

static int hw_breakpoint_add(struct perf_event *bp, int flags)
{
	if (!(flags & PERF_EF_START))
		bp->hw.state = PERF_HES_STOPPED;

	if (is_sampling_event(bp)) {
		bp->hw.last_period = bp->hw.sample_period;
		perf_swevent_set_period(bp);
	}

	return arch_install_hw_breakpoint(bp);
}

static void hw_breakpoint_del(struct perf_event *bp, int flags)
{
	arch_uninstall_hw_breakpoint(bp);
}

static void hw_breakpoint_start(struct perf_event *bp, int flags)
{
	bp->hw.state = 0;
}

static void hw_breakpoint_stop(struct perf_event *bp, int flags)
{
	bp->hw.state = PERF_HES_STOPPED;
}

static struct pmu perf_breakpoint = {
	.task_ctx_nr	= perf_sw_context, /* could eventually get its own */

	.event_init	= hw_breakpoint_event_init,
	.add		= hw_breakpoint_add,
	.del		= hw_breakpoint_del,
	.start		= hw_breakpoint_start,
	.stop		= hw_breakpoint_stop,
	.read		= hw_breakpoint_pmu_read,
};

int __init init_hw_breakpoint(void)
{
	int ret;

	ret = rhltable_init(&task_bps_ht, &task_bps_ht_params);
	if (ret)
		return ret;

	ret = init_breakpoint_slots();
	if (ret)
		return ret;

	constraints_initialized = true;

	perf_pmu_register(&perf_breakpoint, "breakpoint", PERF_TYPE_BREAKPOINT);

	return register_die_notifier(&hw_breakpoint_exceptions_nb);
}
