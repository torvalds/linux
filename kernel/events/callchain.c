/*
 * Performance events callchain code, extracted from core.c:
 *
 *  Copyright (C) 2008 Thomas Gleixner <tglx@linutronix.de>
 *  Copyright (C) 2008-2011 Red Hat, Inc., Ingo Molnar
 *  Copyright (C) 2008-2011 Red Hat, Inc., Peter Zijlstra
 *  Copyright  ©  2009 Paul Mackerras, IBM Corp. <paulus@au1.ibm.com>
 *
 * For licensing details see kernel-base/COPYING
 */

#include <linux/perf_event.h>
#include <linux/slab.h>
#include "internal.h"

struct callchain_cpus_entries {
	struct rcu_head			rcu_head;
	struct perf_callchain_entry	*cpu_entries[0];
};

static DEFINE_PER_CPU(int, callchain_recursion[PERF_NR_CONTEXTS]);
static atomic_t nr_callchain_events;
static DEFINE_MUTEX(callchain_mutex);
static struct callchain_cpus_entries *callchain_cpus_entries;


__weak void perf_callchain_kernel(struct perf_callchain_entry *entry,
				  struct pt_regs *regs)
{
}

__weak void perf_callchain_user(struct perf_callchain_entry *entry,
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

	size = sizeof(struct perf_callchain_entry) * PERF_NR_CONTEXTS;

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

int get_callchain_buffers(void)
{
	int err = 0;
	int count;

	mutex_lock(&callchain_mutex);

	count = atomic_inc_return(&nr_callchain_events);
	if (WARN_ON_ONCE(count < 1)) {
		err = -EINVAL;
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

	return &entries->cpu_entries[cpu][*rctx];
}

static void
put_callchain_entry(int rctx)
{
	put_recursion_context(this_cpu_ptr(callchain_recursion), rctx);
}

struct perf_callchain_entry *
perf_callchain(struct perf_event *event, struct pt_regs *regs)
{
	int rctx;
	struct perf_callchain_entry *entry;

	int kernel = !event->attr.exclude_callchain_kernel;
	int user   = !event->attr.exclude_callchain_user;

	if (!kernel && !user)
		return NULL;

	entry = get_callchain_entry(&rctx);
	if (rctx == -1)
		return NULL;

	if (!entry)
		goto exit_put;

	entry->nr = 0;

	if (kernel && !user_mode(regs)) {
		perf_callchain_store(entry, PERF_CONTEXT_KERNEL);
		perf_callchain_kernel(entry, regs);
	}

	if (user) {
		if (!user_mode(regs)) {
			if  (current->mm)
				regs = task_pt_regs(current);
			else
				regs = NULL;
		}

		if (regs) {
			/*
			 * Disallow cross-task user callchains.
			 */
			if (event->ctx->task && event->ctx->task != current)
				goto exit_put;

			perf_callchain_store(entry, PERF_CONTEXT_USER);
			perf_callchain_user(entry, regs);
		}
	}

exit_put:
	put_callchain_entry(rctx);

	return entry;
}
