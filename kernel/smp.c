/*
 * Generic helpers for smp ipi calls
 *
 * (C) Jens Axboe <jens.axboe@oracle.com> 2008
 */
#include <linux/rcupdate.h>
#include <linux/rculist.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/percpu.h>
#include <linux/init.h>
#include <linux/gfp.h>
#include <linux/smp.h>
#include <linux/cpu.h>

static struct {
	struct list_head	queue;
	raw_spinlock_t		lock;
} call_function __cacheline_aligned_in_smp =
	{
		.queue		= LIST_HEAD_INIT(call_function.queue),
		.lock		= __RAW_SPIN_LOCK_UNLOCKED(call_function.lock),
	};

enum {
	CSD_FLAG_LOCK		= 0x01,
};

struct call_function_data {
	struct call_single_data	csd;
	atomic_t		refs;
	cpumask_var_t		cpumask;
};

static DEFINE_PER_CPU_SHARED_ALIGNED(struct call_function_data, cfd_data);

struct call_single_queue {
	struct list_head	list;
	raw_spinlock_t		lock;
};

static DEFINE_PER_CPU_SHARED_ALIGNED(struct call_single_queue, call_single_queue);

static int
hotplug_cfd(struct notifier_block *nfb, unsigned long action, void *hcpu)
{
	long cpu = (long)hcpu;
	struct call_function_data *cfd = &per_cpu(cfd_data, cpu);

	switch (action) {
	case CPU_UP_PREPARE:
	case CPU_UP_PREPARE_FROZEN:
		if (!zalloc_cpumask_var_node(&cfd->cpumask, GFP_KERNEL,
				cpu_to_node(cpu)))
			return NOTIFY_BAD;
		break;

#ifdef CONFIG_HOTPLUG_CPU
	case CPU_UP_CANCELED:
	case CPU_UP_CANCELED_FROZEN:

	case CPU_DEAD:
	case CPU_DEAD_FROZEN:
		free_cpumask_var(cfd->cpumask);
		break;
#endif
	};

	return NOTIFY_OK;
}

static struct notifier_block __cpuinitdata hotplug_cfd_notifier = {
	.notifier_call		= hotplug_cfd,
};

static int __cpuinit init_call_single_data(void)
{
	void *cpu = (void *)(long)smp_processor_id();
	int i;

	for_each_possible_cpu(i) {
		struct call_single_queue *q = &per_cpu(call_single_queue, i);

		raw_spin_lock_init(&q->lock);
		INIT_LIST_HEAD(&q->list);
	}

	hotplug_cfd(&hotplug_cfd_notifier, CPU_UP_PREPARE, cpu);
	register_cpu_notifier(&hotplug_cfd_notifier);

	return 0;
}
early_initcall(init_call_single_data);

/*
 * csd_lock/csd_unlock used to serialize access to per-cpu csd resources
 *
 * For non-synchronous ipi calls the csd can still be in use by the
 * previous function call. For multi-cpu calls its even more interesting
 * as we'll have to ensure no other cpu is observing our csd.
 */
static void csd_lock_wait(struct call_single_data *data)
{
	while (data->flags & CSD_FLAG_LOCK)
		cpu_relax();
}

static void csd_lock(struct call_single_data *data)
{
	csd_lock_wait(data);
	data->flags = CSD_FLAG_LOCK;

	/*
	 * prevent CPU from reordering the above assignment
	 * to ->flags with any subsequent assignments to other
	 * fields of the specified call_single_data structure:
	 */
	smp_mb();
}

static void csd_unlock(struct call_single_data *data)
{
	WARN_ON(!(data->flags & CSD_FLAG_LOCK));

	/*
	 * ensure we're all done before releasing data:
	 */
	smp_mb();

	data->flags &= ~CSD_FLAG_LOCK;
}

/*
 * Insert a previously allocated call_single_data element
 * for execution on the given CPU. data must already have
 * ->func, ->info, and ->flags set.
 */
static
void generic_exec_single(int cpu, struct call_single_data *data, int wait)
{
	struct call_single_queue *dst = &per_cpu(call_single_queue, cpu);
	unsigned long flags;
	int ipi;

	raw_spin_lock_irqsave(&dst->lock, flags);
	ipi = list_empty(&dst->list);
	list_add_tail(&data->list, &dst->list);
	raw_spin_unlock_irqrestore(&dst->lock, flags);

	/*
	 * The list addition should be visible before sending the IPI
	 * handler locks the list to pull the entry off it because of
	 * normal cache coherency rules implied by spinlocks.
	 *
	 * If IPIs can go out of order to the cache coherency protocol
	 * in an architecture, sufficient synchronisation should be added
	 * to arch code to make it appear to obey cache coherency WRT
	 * locking and barrier primitives. Generic code isn't really
	 * equipped to do the right thing...
	 */
	if (ipi)
		arch_send_call_function_single_ipi(cpu);

	if (wait)
		csd_lock_wait(data);
}

/*
 * Invoked by arch to handle an IPI for call function. Must be called with
 * interrupts disabled.
 */
void generic_smp_call_function_interrupt(void)
{
	struct call_function_data *data;
	int cpu = smp_processor_id();

	/*
	 * Shouldn't receive this interrupt on a cpu that is not yet online.
	 */
	WARN_ON_ONCE(!cpu_online(cpu));

	/*
	 * Ensure entry is visible on call_function_queue after we have
	 * entered the IPI. See comment in smp_call_function_many.
	 * If we don't have this, then we may miss an entry on the list
	 * and never get another IPI to process it.
	 */
	smp_mb();

	/*
	 * It's ok to use list_for_each_rcu() here even though we may
	 * delete 'pos', since list_del_rcu() doesn't clear ->next
	 */
	list_for_each_entry_rcu(data, &call_function.queue, csd.list) {
		int refs;

		if (!cpumask_test_and_clear_cpu(cpu, data->cpumask))
			continue;

		data->csd.func(data->csd.info);

		refs = atomic_dec_return(&data->refs);
		WARN_ON(refs < 0);
		if (!refs) {
			raw_spin_lock(&call_function.lock);
			list_del_rcu(&data->csd.list);
			raw_spin_unlock(&call_function.lock);
		}

		if (refs)
			continue;

		csd_unlock(&data->csd);
	}

}

/*
 * Invoked by arch to handle an IPI for call function single. Must be
 * called from the arch with interrupts disabled.
 */
void generic_smp_call_function_single_interrupt(void)
{
	struct call_single_queue *q = &__get_cpu_var(call_single_queue);
	unsigned int data_flags;
	LIST_HEAD(list);

	/*
	 * Shouldn't receive this interrupt on a cpu that is not yet online.
	 */
	WARN_ON_ONCE(!cpu_online(smp_processor_id()));

	raw_spin_lock(&q->lock);
	list_replace_init(&q->list, &list);
	raw_spin_unlock(&q->lock);

	while (!list_empty(&list)) {
		struct call_single_data *data;

		data = list_entry(list.next, struct call_single_data, list);
		list_del(&data->list);

		/*
		 * 'data' can be invalid after this call if flags == 0
		 * (when called through generic_exec_single()),
		 * so save them away before making the call:
		 */
		data_flags = data->flags;

		data->func(data->info);

		/*
		 * Unlocked CSDs are valid through generic_exec_single():
		 */
		if (data_flags & CSD_FLAG_LOCK)
			csd_unlock(data);
	}
}

static DEFINE_PER_CPU_SHARED_ALIGNED(struct call_single_data, csd_data);

/*
 * smp_call_function_single - Run a function on a specific CPU
 * @func: The function to run. This must be fast and non-blocking.
 * @info: An arbitrary pointer to pass to the function.
 * @wait: If true, wait until function has completed on other CPUs.
 *
 * Returns 0 on success, else a negative status code.
 */
int smp_call_function_single(int cpu, void (*func) (void *info), void *info,
			     int wait)
{
	struct call_single_data d = {
		.flags = 0,
	};
	unsigned long flags;
	int this_cpu;
	int err = 0;

	/*
	 * prevent preemption and reschedule on another processor,
	 * as well as CPU removal
	 */
	this_cpu = get_cpu();

	/*
	 * Can deadlock when called with interrupts disabled.
	 * We allow cpu's that are not yet online though, as no one else can
	 * send smp call function interrupt to this cpu and as such deadlocks
	 * can't happen.
	 */
	WARN_ON_ONCE(cpu_online(this_cpu) && irqs_disabled()
		     && !oops_in_progress);

	if (cpu == this_cpu) {
		local_irq_save(flags);
		func(info);
		local_irq_restore(flags);
	} else {
		if ((unsigned)cpu < nr_cpu_ids && cpu_online(cpu)) {
			struct call_single_data *data = &d;

			if (!wait)
				data = &__get_cpu_var(csd_data);

			csd_lock(data);

			data->func = func;
			data->info = info;
			generic_exec_single(cpu, data, wait);
		} else {
			err = -ENXIO;	/* CPU not online */
		}
	}

	put_cpu();

	return err;
}
EXPORT_SYMBOL(smp_call_function_single);

/*
 * smp_call_function_any - Run a function on any of the given cpus
 * @mask: The mask of cpus it can run on.
 * @func: The function to run. This must be fast and non-blocking.
 * @info: An arbitrary pointer to pass to the function.
 * @wait: If true, wait until function has completed.
 *
 * Returns 0 on success, else a negative status code (if no cpus were online).
 * Note that @wait will be implicitly turned on in case of allocation failures,
 * since we fall back to on-stack allocation.
 *
 * Selection preference:
 *	1) current cpu if in @mask
 *	2) any cpu of current node if in @mask
 *	3) any other online cpu in @mask
 */
int smp_call_function_any(const struct cpumask *mask,
			  void (*func)(void *info), void *info, int wait)
{
	unsigned int cpu;
	const struct cpumask *nodemask;
	int ret;

	/* Try for same CPU (cheapest) */
	cpu = get_cpu();
	if (cpumask_test_cpu(cpu, mask))
		goto call;

	/* Try for same node. */
	nodemask = cpumask_of_node(cpu_to_node(cpu));
	for (cpu = cpumask_first_and(nodemask, mask); cpu < nr_cpu_ids;
	     cpu = cpumask_next_and(cpu, nodemask, mask)) {
		if (cpu_online(cpu))
			goto call;
	}

	/* Any online will do: smp_call_function_single handles nr_cpu_ids. */
	cpu = cpumask_any_and(mask, cpu_online_mask);
call:
	ret = smp_call_function_single(cpu, func, info, wait);
	put_cpu();
	return ret;
}
EXPORT_SYMBOL_GPL(smp_call_function_any);

/**
 * __smp_call_function_single(): Run a function on another CPU
 * @cpu: The CPU to run on.
 * @data: Pre-allocated and setup data structure
 *
 * Like smp_call_function_single(), but allow caller to pass in a
 * pre-allocated data structure. Useful for embedding @data inside
 * other structures, for instance.
 */
void __smp_call_function_single(int cpu, struct call_single_data *data,
				int wait)
{
	csd_lock(data);

	/*
	 * Can deadlock when called with interrupts disabled.
	 * We allow cpu's that are not yet online though, as no one else can
	 * send smp call function interrupt to this cpu and as such deadlocks
	 * can't happen.
	 */
	WARN_ON_ONCE(cpu_online(smp_processor_id()) && wait && irqs_disabled()
		     && !oops_in_progress);

	generic_exec_single(cpu, data, wait);
}

/**
 * smp_call_function_many(): Run a function on a set of other CPUs.
 * @mask: The set of cpus to run on (only runs on online subset).
 * @func: The function to run. This must be fast and non-blocking.
 * @info: An arbitrary pointer to pass to the function.
 * @wait: If true, wait (atomically) until function has completed
 *        on other CPUs.
 *
 * If @wait is true, then returns once @func has returned.
 *
 * You must not call this function with disabled interrupts or from a
 * hardware interrupt handler or from a bottom half handler. Preemption
 * must be disabled when calling this function.
 */
void smp_call_function_many(const struct cpumask *mask,
			    void (*func)(void *), void *info, bool wait)
{
	struct call_function_data *data;
	unsigned long flags;
	int cpu, next_cpu, this_cpu = smp_processor_id();

	/*
	 * Can deadlock when called with interrupts disabled.
	 * We allow cpu's that are not yet online though, as no one else can
	 * send smp call function interrupt to this cpu and as such deadlocks
	 * can't happen.
	 */
	WARN_ON_ONCE(cpu_online(this_cpu) && irqs_disabled()
		     && !oops_in_progress);

	/* So, what's a CPU they want? Ignoring this one. */
	cpu = cpumask_first_and(mask, cpu_online_mask);
	if (cpu == this_cpu)
		cpu = cpumask_next_and(cpu, mask, cpu_online_mask);

	/* No online cpus?  We're done. */
	if (cpu >= nr_cpu_ids)
		return;

	/* Do we have another CPU which isn't us? */
	next_cpu = cpumask_next_and(cpu, mask, cpu_online_mask);
	if (next_cpu == this_cpu)
		next_cpu = cpumask_next_and(next_cpu, mask, cpu_online_mask);

	/* Fastpath: do that cpu by itself. */
	if (next_cpu >= nr_cpu_ids) {
		smp_call_function_single(cpu, func, info, wait);
		return;
	}

	data = &__get_cpu_var(cfd_data);
	csd_lock(&data->csd);

	data->csd.func = func;
	data->csd.info = info;
	cpumask_and(data->cpumask, mask, cpu_online_mask);
	cpumask_clear_cpu(this_cpu, data->cpumask);
	atomic_set(&data->refs, cpumask_weight(data->cpumask));

	raw_spin_lock_irqsave(&call_function.lock, flags);
	/*
	 * Place entry at the _HEAD_ of the list, so that any cpu still
	 * observing the entry in generic_smp_call_function_interrupt()
	 * will not miss any other list entries:
	 */
	list_add_rcu(&data->csd.list, &call_function.queue);
	raw_spin_unlock_irqrestore(&call_function.lock, flags);

	/*
	 * Make the list addition visible before sending the ipi.
	 * (IPIs must obey or appear to obey normal Linux cache
	 * coherency rules -- see comment in generic_exec_single).
	 */
	smp_mb();

	/* Send a message to all CPUs in the map */
	arch_send_call_function_ipi_mask(data->cpumask);

	/* Optionally wait for the CPUs to complete */
	if (wait)
		csd_lock_wait(&data->csd);
}
EXPORT_SYMBOL(smp_call_function_many);

/**
 * smp_call_function(): Run a function on all other CPUs.
 * @func: The function to run. This must be fast and non-blocking.
 * @info: An arbitrary pointer to pass to the function.
 * @wait: If true, wait (atomically) until function has completed
 *        on other CPUs.
 *
 * Returns 0.
 *
 * If @wait is true, then returns once @func has returned; otherwise
 * it returns just before the target cpu calls @func.
 *
 * You must not call this function with disabled interrupts or from a
 * hardware interrupt handler or from a bottom half handler.
 */
int smp_call_function(void (*func)(void *), void *info, int wait)
{
	preempt_disable();
	smp_call_function_many(cpu_online_mask, func, info, wait);
	preempt_enable();

	return 0;
}
EXPORT_SYMBOL(smp_call_function);

void ipi_call_lock(void)
{
	raw_spin_lock(&call_function.lock);
}

void ipi_call_unlock(void)
{
	raw_spin_unlock(&call_function.lock);
}

void ipi_call_lock_irq(void)
{
	raw_spin_lock_irq(&call_function.lock);
}

void ipi_call_unlock_irq(void)
{
	raw_spin_unlock_irq(&call_function.lock);
}
