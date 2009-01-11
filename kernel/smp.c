/*
 * Generic helpers for smp ipi calls
 *
 * (C) Jens Axboe <jens.axboe@oracle.com> 2008
 *
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/percpu.h>
#include <linux/rcupdate.h>
#include <linux/rculist.h>
#include <linux/smp.h>

static DEFINE_PER_CPU(struct call_single_queue, call_single_queue);
static LIST_HEAD(call_function_queue);
__cacheline_aligned_in_smp DEFINE_SPINLOCK(call_function_lock);

enum {
	CSD_FLAG_WAIT		= 0x01,
	CSD_FLAG_ALLOC		= 0x02,
};

struct call_function_data {
	struct call_single_data csd;
	spinlock_t lock;
	unsigned int refs;
	struct rcu_head rcu_head;
	unsigned long cpumask_bits[];
};

struct call_single_queue {
	struct list_head list;
	spinlock_t lock;
};

static int __cpuinit init_call_single_data(void)
{
	int i;

	for_each_possible_cpu(i) {
		struct call_single_queue *q = &per_cpu(call_single_queue, i);

		spin_lock_init(&q->lock);
		INIT_LIST_HEAD(&q->list);
	}
	return 0;
}
early_initcall(init_call_single_data);

static void csd_flag_wait(struct call_single_data *data)
{
	/* Wait for response */
	do {
		if (!(data->flags & CSD_FLAG_WAIT))
			break;
		cpu_relax();
	} while (1);
}

/*
 * Insert a previously allocated call_single_data element for execution
 * on the given CPU. data must already have ->func, ->info, and ->flags set.
 */
static void generic_exec_single(int cpu, struct call_single_data *data)
{
	struct call_single_queue *dst = &per_cpu(call_single_queue, cpu);
	int wait = data->flags & CSD_FLAG_WAIT, ipi;
	unsigned long flags;

	spin_lock_irqsave(&dst->lock, flags);
	ipi = list_empty(&dst->list);
	list_add_tail(&data->list, &dst->list);
	spin_unlock_irqrestore(&dst->lock, flags);

	/*
	 * Make the list addition visible before sending the ipi.
	 */
	smp_mb();

	if (ipi)
		arch_send_call_function_single_ipi(cpu);

	if (wait)
		csd_flag_wait(data);
}

static void rcu_free_call_data(struct rcu_head *head)
{
	struct call_function_data *data;

	data = container_of(head, struct call_function_data, rcu_head);

	kfree(data);
}

/*
 * Invoked by arch to handle an IPI for call function. Must be called with
 * interrupts disabled.
 */
void generic_smp_call_function_interrupt(void)
{
	struct call_function_data *data;
	int cpu = get_cpu();

	/*
	 * It's ok to use list_for_each_rcu() here even though we may delete
	 * 'pos', since list_del_rcu() doesn't clear ->next
	 */
	rcu_read_lock();
	list_for_each_entry_rcu(data, &call_function_queue, csd.list) {
		int refs;

		if (!cpumask_test_cpu(cpu, to_cpumask(data->cpumask_bits)))
			continue;

		data->csd.func(data->csd.info);

		spin_lock(&data->lock);
		cpumask_clear_cpu(cpu, to_cpumask(data->cpumask_bits));
		WARN_ON(data->refs == 0);
		data->refs--;
		refs = data->refs;
		spin_unlock(&data->lock);

		if (refs)
			continue;

		spin_lock(&call_function_lock);
		list_del_rcu(&data->csd.list);
		spin_unlock(&call_function_lock);

		if (data->csd.flags & CSD_FLAG_WAIT) {
			/*
			 * serialize stores to data with the flag clear
			 * and wakeup
			 */
			smp_wmb();
			data->csd.flags &= ~CSD_FLAG_WAIT;
		}
		if (data->csd.flags & CSD_FLAG_ALLOC)
			call_rcu(&data->rcu_head, rcu_free_call_data);
	}
	rcu_read_unlock();

	put_cpu();
}

/*
 * Invoked by arch to handle an IPI for call function single. Must be called
 * from the arch with interrupts disabled.
 */
void generic_smp_call_function_single_interrupt(void)
{
	struct call_single_queue *q = &__get_cpu_var(call_single_queue);
	LIST_HEAD(list);

	/*
	 * Need to see other stores to list head for checking whether
	 * list is empty without holding q->lock
	 */
	smp_read_barrier_depends();
	while (!list_empty(&q->list)) {
		unsigned int data_flags;

		spin_lock(&q->lock);
		list_replace_init(&q->list, &list);
		spin_unlock(&q->lock);

		while (!list_empty(&list)) {
			struct call_single_data *data;

			data = list_entry(list.next, struct call_single_data,
						list);
			list_del(&data->list);

			/*
			 * 'data' can be invalid after this call if
			 * flags == 0 (when called through
			 * generic_exec_single(), so save them away before
			 * making the call.
			 */
			data_flags = data->flags;

			data->func(data->info);

			if (data_flags & CSD_FLAG_WAIT) {
				smp_wmb();
				data->flags &= ~CSD_FLAG_WAIT;
			} else if (data_flags & CSD_FLAG_ALLOC)
				kfree(data);
		}
		/*
		 * See comment on outer loop
		 */
		smp_read_barrier_depends();
	}
}

/*
 * smp_call_function_single - Run a function on a specific CPU
 * @func: The function to run. This must be fast and non-blocking.
 * @info: An arbitrary pointer to pass to the function.
 * @wait: If true, wait until function has completed on other CPUs.
 *
 * Returns 0 on success, else a negative status code. Note that @wait
 * will be implicitly turned on in case of allocation failures, since
 * we fall back to on-stack allocation.
 */
int smp_call_function_single(int cpu, void (*func) (void *info), void *info,
			     int wait)
{
	struct call_single_data d;
	unsigned long flags;
	/* prevent preemption and reschedule on another processor,
	   as well as CPU removal */
	int me = get_cpu();
	int err = 0;

	/* Can deadlock when called with interrupts disabled */
	WARN_ON(irqs_disabled());

	if (cpu == me) {
		local_irq_save(flags);
		func(info);
		local_irq_restore(flags);
	} else if ((unsigned)cpu < nr_cpu_ids && cpu_online(cpu)) {
		struct call_single_data *data = NULL;

		if (!wait) {
			data = kmalloc(sizeof(*data), GFP_ATOMIC);
			if (data)
				data->flags = CSD_FLAG_ALLOC;
		}
		if (!data) {
			data = &d;
			data->flags = CSD_FLAG_WAIT;
		}

		data->func = func;
		data->info = info;
		generic_exec_single(cpu, data);
	} else {
		err = -ENXIO;	/* CPU not online */
	}

	put_cpu();
	return err;
}
EXPORT_SYMBOL(smp_call_function_single);

/**
 * __smp_call_function_single(): Run a function on another CPU
 * @cpu: The CPU to run on.
 * @data: Pre-allocated and setup data structure
 *
 * Like smp_call_function_single(), but allow caller to pass in a pre-allocated
 * data structure. Useful for embedding @data inside other structures, for
 * instance.
 *
 */
void __smp_call_function_single(int cpu, struct call_single_data *data)
{
	/* Can deadlock when called with interrupts disabled */
	WARN_ON((data->flags & CSD_FLAG_WAIT) && irqs_disabled());

	generic_exec_single(cpu, data);
}

/* FIXME: Shim for archs using old arch_send_call_function_ipi API. */
#ifndef arch_send_call_function_ipi_mask
#define arch_send_call_function_ipi_mask(maskp) \
	arch_send_call_function_ipi(*(maskp))
#endif

/**
 * smp_call_function_many(): Run a function on a set of other CPUs.
 * @mask: The set of cpus to run on (only runs on online subset).
 * @func: The function to run. This must be fast and non-blocking.
 * @info: An arbitrary pointer to pass to the function.
 * @wait: If true, wait (atomically) until function has completed on other CPUs.
 *
 * If @wait is true, then returns once @func has returned. Note that @wait
 * will be implicitly turned on in case of allocation failures, since
 * we fall back to on-stack allocation.
 *
 * You must not call this function with disabled interrupts or from a
 * hardware interrupt handler or from a bottom half handler. Preemption
 * must be disabled when calling this function.
 */
void smp_call_function_many(const struct cpumask *mask,
			    void (*func)(void *), void *info,
			    bool wait)
{
	struct call_function_data *data;
	unsigned long flags;
	int cpu, next_cpu;

	/* Can deadlock when called with interrupts disabled */
	WARN_ON(irqs_disabled());

	/* So, what's a CPU they want?  Ignoring this one. */
	cpu = cpumask_first_and(mask, cpu_online_mask);
	if (cpu == smp_processor_id())
		cpu = cpumask_next_and(cpu, mask, cpu_online_mask);
	/* No online cpus?  We're done. */
	if (cpu >= nr_cpu_ids)
		return;

	/* Do we have another CPU which isn't us? */
	next_cpu = cpumask_next_and(cpu, mask, cpu_online_mask);
	if (next_cpu == smp_processor_id())
		next_cpu = cpumask_next_and(next_cpu, mask, cpu_online_mask);

	/* Fastpath: do that cpu by itself. */
	if (next_cpu >= nr_cpu_ids) {
		smp_call_function_single(cpu, func, info, wait);
		return;
	}

	data = kmalloc(sizeof(*data) + cpumask_size(), GFP_ATOMIC);
	if (unlikely(!data)) {
		/* Slow path. */
		for_each_online_cpu(cpu) {
			if (cpu == smp_processor_id())
				continue;
			if (cpumask_test_cpu(cpu, mask))
				smp_call_function_single(cpu, func, info, wait);
		}
		return;
	}

	spin_lock_init(&data->lock);
	data->csd.flags = CSD_FLAG_ALLOC;
	if (wait)
		data->csd.flags |= CSD_FLAG_WAIT;
	data->csd.func = func;
	data->csd.info = info;
	cpumask_and(to_cpumask(data->cpumask_bits), mask, cpu_online_mask);
	cpumask_clear_cpu(smp_processor_id(), to_cpumask(data->cpumask_bits));
	data->refs = cpumask_weight(to_cpumask(data->cpumask_bits));

	spin_lock_irqsave(&call_function_lock, flags);
	list_add_tail_rcu(&data->csd.list, &call_function_queue);
	spin_unlock_irqrestore(&call_function_lock, flags);

	/*
	 * Make the list addition visible before sending the ipi.
	 */
	smp_mb();

	/* Send a message to all CPUs in the map */
	arch_send_call_function_ipi_mask(to_cpumask(data->cpumask_bits));

	/* optionally wait for the CPUs to complete */
	if (wait)
		csd_flag_wait(&data->csd);
}
EXPORT_SYMBOL(smp_call_function_many);

/**
 * smp_call_function(): Run a function on all other CPUs.
 * @func: The function to run. This must be fast and non-blocking.
 * @info: An arbitrary pointer to pass to the function.
 * @wait: If true, wait (atomically) until function has completed on other CPUs.
 *
 * Returns 0.
 *
 * If @wait is true, then returns once @func has returned; otherwise
 * it returns just before the target cpu calls @func. In case of allocation
 * failure, @wait will be implicitly turned on.
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
	spin_lock(&call_function_lock);
}

void ipi_call_unlock(void)
{
	spin_unlock(&call_function_lock);
}

void ipi_call_lock_irq(void)
{
	spin_lock_irq(&call_function_lock);
}

void ipi_call_unlock_irq(void)
{
	spin_unlock_irq(&call_function_lock);
}
