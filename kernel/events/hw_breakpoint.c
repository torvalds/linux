/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
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

#include <linux/irqflags.h>
#include <linux/kallsyms.h>
#include <linux/notifier.h>
#include <linux/kprobes.h>
#include <linux/kdebug.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/percpu.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/cpu.h>
#include <linux/smp.h>

#include <linux/hw_breakpoint.h>
/*
 * Constraints data
 */
struct bp_cpuinfo {
	/* Number of pinned cpu breakpoints in a cpu */
	unsigned int	cpu_pinned;
	/* tsk_pinned[n] is the number of tasks having n+1 breakpoints */
	unsigned int	*tsk_pinned;
	/* Number of non-pinned cpu/task breakpoints in a cpu */
	unsigned int	flexible; /* XXX: placeholder, see fetch_this_slot() */
};

static DEFINE_PER_CPU(struct bp_cpuinfo, bp_cpuinfo[TYPE_MAX]);
static int nr_slots[TYPE_MAX];

static struct bp_cpuinfo *get_bp_info(int cpu, enum bp_type_idx type)
{
	return per_cpu_ptr(bp_cpuinfo + type, cpu);
}

/* Keep track of the breakpoints attached to tasks */
static LIST_HEAD(bp_task_head);

static int constraints_initialized;

/* Gather the number of total pinned and un-pinned bp in a cpuset */
struct bp_busy_slots {
	unsigned int pinned;
	unsigned int flexible;
};

/* Serialize accesses to the above constraints */
static DEFINE_MUTEX(nr_bp_mutex);

__weak int hw_breakpoint_weight(struct perf_event *bp)
{
	return 1;
}

static inline enum bp_type_idx find_slot_idx(struct perf_event *bp)
{
	if (bp->attr.bp_type & HW_BREAKPOINT_RW)
		return TYPE_DATA;

	return TYPE_INST;
}

/*
 * Report the maximum number of pinned breakpoints a task
 * have in this cpu
 */
static unsigned int max_task_bp_pinned(int cpu, enum bp_type_idx type)
{
	unsigned int *tsk_pinned = get_bp_info(cpu, type)->tsk_pinned;
	int i;

	for (i = nr_slots[type] - 1; i >= 0; i--) {
		if (tsk_pinned[i] > 0)
			return i + 1;
	}

	return 0;
}

/*
 * Count the number of breakpoints of the same type and same task.
 * The given event must be not on the list.
 */
static int task_bp_pinned(int cpu, struct perf_event *bp, enum bp_type_idx type)
{
	struct task_struct *tsk = bp->hw.target;
	struct perf_event *iter;
	int count = 0;

	list_for_each_entry(iter, &bp_task_head, hw.bp_list) {
		if (iter->hw.target == tsk &&
		    find_slot_idx(iter) == type &&
		    (iter->cpu < 0 || cpu == iter->cpu))
			count += hw_breakpoint_weight(iter);
	}

	return count;
}

static const struct cpumask *cpumask_of_bp(struct perf_event *bp)
{
	if (bp->cpu >= 0)
		return cpumask_of(bp->cpu);
	return cpu_possible_mask;
}

/*
 * Report the number of pinned/un-pinned breakpoints we have in
 * a given cpu (cpu > -1) or in all of them (cpu = -1).
 */
static void
fetch_bp_busy_slots(struct bp_busy_slots *slots, struct perf_event *bp,
		    enum bp_type_idx type)
{
	const struct cpumask *cpumask = cpumask_of_bp(bp);
	int cpu;

	for_each_cpu(cpu, cpumask) {
		struct bp_cpuinfo *info = get_bp_info(cpu, type);
		int nr;

		nr = info->cpu_pinned;
		if (!bp->hw.target)
			nr += max_task_bp_pinned(cpu, type);
		else
			nr += task_bp_pinned(cpu, bp, type);

		if (nr > slots->pinned)
			slots->pinned = nr;

		nr = info->flexible;
		if (nr > slots->flexible)
			slots->flexible = nr;
	}
}

/*
 * For now, continue to consider flexible as pinned, until we can
 * ensure no flexible event can ever be scheduled before a pinned event
 * in a same cpu.
 */
static void
fetch_this_slot(struct bp_busy_slots *slots, int weight)
{
	slots->pinned += weight;
}

/*
 * Add a pinned breakpoint for the given task in our constraint table
 */
static void toggle_bp_task_slot(struct perf_event *bp, int cpu,
				enum bp_type_idx type, int weight)
{
	unsigned int *tsk_pinned = get_bp_info(cpu, type)->tsk_pinned;
	int old_idx, new_idx;

	old_idx = task_bp_pinned(cpu, bp, type) - 1;
	new_idx = old_idx + weight;

	if (old_idx >= 0)
		tsk_pinned[old_idx]--;
	if (new_idx >= 0)
		tsk_pinned[new_idx]++;
}

/*
 * Add/remove the given breakpoint in our constraint table
 */
static void
toggle_bp_slot(struct perf_event *bp, bool enable, enum bp_type_idx type,
	       int weight)
{
	const struct cpumask *cpumask = cpumask_of_bp(bp);
	int cpu;

	if (!enable)
		weight = -weight;

	/* Pinned counter cpu profiling */
	if (!bp->hw.target) {
		get_bp_info(bp->cpu, type)->cpu_pinned += weight;
		return;
	}

	/* Pinned counter task profiling */
	for_each_cpu(cpu, cpumask)
		toggle_bp_task_slot(bp, cpu, type, weight);

	if (enable)
		list_add_tail(&bp->hw.bp_list, &bp_task_head);
	else
		list_del(&bp->hw.bp_list);
}

/*
 * Function to perform processor-specific cleanup during unregistration
 */
__weak void arch_unregister_hw_breakpoint(struct perf_event *bp)
{
	/*
	 * A weak stub function here for those archs that don't define
	 * it inside arch/.../kernel/hw_breakpoint.c
	 */
}

/*
 * Contraints to check before allowing this new breakpoint counter:
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
static int __reserve_bp_slot(struct perf_event *bp)
{
	struct bp_busy_slots slots = {0};
	enum bp_type_idx type;
	int weight;

	/* We couldn't initialize breakpoint constraints on boot */
	if (!constraints_initialized)
		return -ENOMEM;

	/* Basic checks */
	if (bp->attr.bp_type == HW_BREAKPOINT_EMPTY ||
	    bp->attr.bp_type == HW_BREAKPOINT_INVALID)
		return -EINVAL;

	type = find_slot_idx(bp);
	weight = hw_breakpoint_weight(bp);

	fetch_bp_busy_slots(&slots, bp, type);
	/*
	 * Simulate the addition of this breakpoint to the constraints
	 * and see the result.
	 */
	fetch_this_slot(&slots, weight);

	/* Flexible counters need to keep at least one slot */
	if (slots.pinned + (!!slots.flexible) > nr_slots[type])
		return -ENOSPC;

	toggle_bp_slot(bp, true, type, weight);

	return 0;
}

int reserve_bp_slot(struct perf_event *bp)
{
	int ret;

	mutex_lock(&nr_bp_mutex);

	ret = __reserve_bp_slot(bp);

	mutex_unlock(&nr_bp_mutex);

	return ret;
}

static void __release_bp_slot(struct perf_event *bp)
{
	enum bp_type_idx type;
	int weight;

	type = find_slot_idx(bp);
	weight = hw_breakpoint_weight(bp);
	toggle_bp_slot(bp, false, type, weight);
}

void release_bp_slot(struct perf_event *bp)
{
	mutex_lock(&nr_bp_mutex);

	arch_unregister_hw_breakpoint(bp);
	__release_bp_slot(bp);

	mutex_unlock(&nr_bp_mutex);
}

/*
 * Allow the kernel debugger to reserve breakpoint slots without
 * taking a lock using the dbg_* variant of for the reserve and
 * release breakpoint slots.
 */
int dbg_reserve_bp_slot(struct perf_event *bp)
{
	if (mutex_is_locked(&nr_bp_mutex))
		return -1;

	return __reserve_bp_slot(bp);
}

int dbg_release_bp_slot(struct perf_event *bp)
{
	if (mutex_is_locked(&nr_bp_mutex))
		return -1;

	__release_bp_slot(bp);

	return 0;
}

static int validate_hw_breakpoint(struct perf_event *bp)
{
	int ret;

	ret = arch_validate_hwbkpt_settings(bp);
	if (ret)
		return ret;

	if (arch_check_bp_in_kernelspace(bp)) {
		if (bp->attr.exclude_kernel)
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
	int ret;

	ret = reserve_bp_slot(bp);
	if (ret)
		return ret;

	ret = validate_hw_breakpoint(bp);

	/* if arch_validate_hwbkpt_settings() fails then release bp slot */
	if (ret)
		release_bp_slot(bp);

	return ret;
}

/**
 * register_user_hw_breakpoint - register a hardware breakpoint for user space
 * @attr: breakpoint attributes
 * @triggered: callback to trigger when we hit the breakpoint
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

/**
 * modify_user_hw_breakpoint - modify a user-space hardware breakpoint
 * @bp: the breakpoint structure to modify
 * @attr: new breakpoint attributes
 */
int modify_user_hw_breakpoint(struct perf_event *bp, struct perf_event_attr *attr)
{
	/*
	 * modify_user_hw_breakpoint can be invoked with IRQs disabled and hence it
	 * will not be possible to raise IPIs that invoke __perf_event_disable.
	 * So call the function directly after making sure we are targeting the
	 * current task.
	 */
	if (irqs_disabled() && bp->ctx && bp->ctx->task == current)
		__perf_event_disable(bp);
	else
		perf_event_disable(bp);

	bp->attr.bp_addr = attr->bp_addr;
	bp->attr.bp_type = attr->bp_type;
	bp->attr.bp_len = attr->bp_len;
	bp->attr.disabled = 1;

	if (!attr->disabled) {
		int err = validate_hw_breakpoint(bp);

		if (err)
			return err;

		perf_event_enable(bp);
		bp->attr.disabled = 0;
	}

	return 0;
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
		return (void __percpu __force *)ERR_PTR(-ENOMEM);

	get_online_cpus();
	for_each_online_cpu(cpu) {
		bp = perf_event_create_kernel_counter(attr, cpu, NULL,
						      triggered, context);
		if (IS_ERR(bp)) {
			err = PTR_ERR(bp);
			break;
		}

		per_cpu(*cpu_events, cpu) = bp;
	}
	put_online_cpus();

	if (likely(!err))
		return cpu_events;

	unregister_wide_hw_breakpoint(cpu_events);
	return (void __percpu __force *)ERR_PTR(err);
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
	 * no branch sampling for breakpoint events
	 */
	if (has_branch_stack(bp))
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
	int cpu, err_cpu;
	int i;

	for (i = 0; i < TYPE_MAX; i++)
		nr_slots[i] = hw_breakpoint_slots(i);

	for_each_possible_cpu(cpu) {
		for (i = 0; i < TYPE_MAX; i++) {
			struct bp_cpuinfo *info = get_bp_info(cpu, i);

			info->tsk_pinned = kcalloc(nr_slots[i], sizeof(int),
							GFP_KERNEL);
			if (!info->tsk_pinned)
				goto err_alloc;
		}
	}

	constraints_initialized = 1;

	perf_pmu_register(&perf_breakpoint, "breakpoint", PERF_TYPE_BREAKPOINT);

	return register_die_notifier(&hw_breakpoint_exceptions_nb);

 err_alloc:
	for_each_possible_cpu(err_cpu) {
		for (i = 0; i < TYPE_MAX; i++)
			kfree(get_bp_info(err_cpu, i)->tsk_pinned);
		if (err_cpu == cpu)
			break;
	}

	return -ENOMEM;
}


