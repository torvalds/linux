// SPDX-License-Identifier: GPL-2.0
/*
 * Performance events callchain code, extracted from core.c:
 *
 *  Copyright (C) 2008 Thomas Gleixner <tglx@linutronix.de>
 *  Copyright (C) 2008-2011 Red Hat, Inc., Ingo Molnar
 *  Copyright (C) 2008-2011 Red Hat, Inc., Peter Zijlstra
 *  Copyright  ©  2009 Paul Mackerras, IBM Corp. <paulus@au1.ibm.com>
 */

#include <linux/perf_event.h>
#include <linux/slab.h>
#include <linux/sched/task_stack.h>

#include "internal.h"

struct callchain_cpus_entries {
	struct rcu_head			rcu_head;
	struct perf_callchain_entry	*cpu_entries[0];
};

int sysctl_perf_event_max_stack __read_mostly = PERF_MAX_STACK_DEPTH;
int sysctl_perf_event_max_contexts_per_stack __read_mostly = PERF_MAX_CONTEXTS_PER_STACK;

static inline size_t perf_callchain_entry__sizeof(void)
{
	return (sizeof(struct perf_callchain_entry) +
		sizeof(__u64) * (sysctl_perf_event_max_stack +
				 sysctl_perf_event_max_contexts_per_stack));
}

static DEFINE_PER_CPU(int, callchain_recursion[PERF_NR_CONTEXTS]);
static atomic_t nr_callchain_events;
static DEFINE_MUTEX(callchain_mutex);
static struct callchain_cpus_entries *callchain_cpus_entries;


__weak void perf_callchain_kernel(struct perf_callchain_entry_ctx *entry,
				  struct pt_regs *regs)
{
}

__weak void perf_callchain_user(struct perf_callchain_entry_ctx *entry,
				struct pt_regs *regs)
{
}

static void release_callchain_buffers_rcu(struct rcu_head *head)
{
	struct callchain_cpus_entries *entries;
	int cpu;

	entries = container_of(head, struct callchain_cpus_entries, rcu_head);

	for_each_possible_cpu(cpu)
		kfree(entries->cpu_entries[cpu]);

	kfree(entries);
}

static void release_callchain_buffers(void)
{
	struct callchain_cpus_entries *entries;

	entries = callchain_cpus_entries;
	RCU_INIT_POINTER(callchain_cpus_entries, NULL);
	call_rcu(&entries->rcu_head, release_callchain_buffers_rcu);
}

static int alloc_callchain_buffers(void)
{
	int cpu;
	int size;
	struct callchain_cpus_entries *entries;

	/*
	 * We can't use the percpu allocation API for data that can be
	 * accessed from NMI. Use a temporary manual per cpu allocation
	 * until that gets sorted out.
	 */
	size = offsetof(struct callchain_cpus_entries, cpu_entries[nr_cpu_ids]);

	entries = kzalloc(size, GFP_KERNEL);
	if (!entries)
		return -ENOMEM;

	size = perf_callchain_entry__sizeof() * PERF_NR_CONTEXTS;

	for_each_possible_cpu(cpu) {
		entries->cpu_entries[cpu] = kmalloc_node(size, GFP_KERNEL,
							 cpu_to_node(cpu));
		if (!entries->cpu_entries[cpu])
			goto fail;
	}

	rcu_assign_pointer(callchain_cpus_entries, entries);

	return 0;

fail:
	for_each_possible_cpu(cpu)
		kfree(entries->cpu_entries[cpu]);
	kfree(entries);

	return -ENOMEM;
}

int get_callchain_buffers(int event_max_stack)
{
	int err = 0;
	int count;

	mutex_lock(&callchain_mutex);

	count = atomic_inc_return(&nr_callchain_events);
	if (WARN_ON_ONCE(count < 1)) {
		err = -EINVAL;
		goto exit;
	}

	/*
	 * If requesting per event more than the global cap,
	 * return a different error to help userspace figure
	 * this out.
	 *
	 * And also do it here so that we have &callchain_mutex held.
	 */
	if (event_max_stack > sysctl_perf_event_max_stack) {
		err = -EOVERFLOW;
		goto exit;
	}

	if (count == 1)
		err = alloc_callchain_buffers();
exit:
	if (err)
		atomic_dec(&nr_callchain_events);

	mutex_unlock(&callchain_mutex);

	return err;
}

void put_callchain_buffers(void)
{
	if (atomic_dec_and_mutex_lock(&nr_callchain_events, &callchain_mutex)) {
		release_callchain_buffers();
		mutex_unlock(&callchain_mutex);
	}
}

static struct perf_callchain_entry *get_callchain_entry(int *rctx)
{
	int cpu;
	struct callchain_cpus_entries *entries;

	*rctx = get_recursion_context(this_cpu_ptr(callchain_recursion));
	if (*rctx == -1)
		return NULL;

	entries = rcu_dereference(callchain_cpus_entries);
	if (!entries)
		return NULL;

	cpu = smp_processor_id();

	return (((void *)entries->cpu_entries[cpu]) +
		(*rctx * perf_callchain_entry__sizeof()));
}

static void
put_callchain_entry(int rctx)
{
	put_recursion_context(this_cpu_ptr(callchain_recursion), rctx);
}

struct perf_callchain_entry *
get_perf_callchain(struct pt_regs *regs, u32 init_nr, bool kernel, bool user,
		   u32 max_stack, bool crosstask, bool add_mark)
{
	struct perf_callchain_entry *entry;
	struct perf_callchain_entry_ctx ctx;
	int rctx;

	entry = get_callchain_entry(&rctx);
	if (rctx == -1)
		return NULL;

	if (!entry)
		goto exit_put;

	ctx.entry     = entry;
	ctx.max_stack = max_stack;
	ctx.nr	      = entry->nr = init_nr;
	ctx.contexts       = 0;
	ctx.contexts_maxed = false;

	if (kernel && !user_mode(regs)) {
		if (add_mark)
			perf_callchain_store_context(&ctx, PERF_CONTEXT_KERNEL);
		perf_callchain_kernel(&ctx, regs);
	}

	if (user) {
		if (!user_mode(regs)) {
			if  (current->mm)
				regs = task_pt_regs(current);
			else
				regs = NULL;
		}

		if (regs) {
			mm_segment_t fs;

			if (crosstask)
				goto exit_put;

			if (add_mark)
				perf_callchain_store_context(&ctx, PERF_CONTEXT_USER);

			fs = get_fs();
			set_fs(USER_DS);
			perf_callchain_user(&ctx, regs);
			set_fs(fs);
		}
	}

exit_put:
	put_callchain_entry(rctx);

	return entry;
}

/*
 * Used for sysctl_perf_event_max_stack and
 * sysctl_perf_event_max_contexts_per_stack.
 */
int perf_event_max_stack_handler(struct ctl_table *table, int write,
				 void __user *buffer, size_t *lenp, loff_t *ppos)
{
	int *value = table->data;
	int new_value = *value, ret;
	struct ctl_table new_table = *table;

	new_table.data = &new_value;
	ret = proc_dointvec_minmax(&new_table, write, buffer, lenp, ppos);
	if (ret || !write)
		return ret;

	mutex_lock(&callchain_mutex);
	if (atomic_read(&nr_callchain_events))
		ret = -EBUSY;
	else
		*value = new_value;

	mutex_unlock(&callchain_mutex);

	return ret;
}
