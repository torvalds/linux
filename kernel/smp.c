/*
 * Generic helpers for smp ipi calls
 *
 * (C) Jens Axboe <jens.axboe@oracle.com> 2008
 */
#include <linux/rcupdate.h>
#include <linux/rculist.h>
#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/percpu.h>
#include <linux/init.h>
#include <linux/gfp.h>
#include <linux/smp.h>
#include <linux/cpu.h>

#include "smpboot.h"

#ifdef CONFIG_USE_GENERIC_SMP_HELPERS
enum {
	CSD_FLAG_LOCK		= 0x01,
};

struct call_function_data {
	struct call_single_data	__percpu *csd;
	cpumask_var_t		cpumask;
	cpumask_var_t		cpumask_ipi;
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
			return notifier_from_errno(-ENOMEM);
		if (!zalloc_cpumask_var_node(&cfd->cpumask_ipi, GFP_KERNEL,
				cpu_to_node(cpu)))
			return notifier_from_errno(-ENOMEM);
		cfd->csd = alloc_percpu(struct call_single_data);
		if (!cfd->csd) {
			free_cpumask_var(cfd->cpumask);
			return notifier_from_errno(-ENOMEM);
		}
		break;

#ifdef CONFIG_HOTPLUG_CPU
	case CPU_UP_CANCELED:
	case CPU_UP_CANCELED_FROZEN:

	case CPU_DEAD:
	case CPU_DEAD_FROZEN:
		free_cpumask_var(cfd->cpumask);
		free_cpumask_var(cfd->cpumask_ipi);
		free_percpu(cfd->csd);
		break;
#endif
	};

	return NOTIFY_OK;
}

static struct notifier_block hotplug_cfd_notifier = {
	.notifier_call		= hotplug_cfd,
};

void __init call_function_init(void)
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
}

/*
 * csd_lock/csd_unlock used to serialize access to per-cpu csd resources
 *
 * For non-synchronous ipi calls the csd can still be in use by the
 * previous function call. For multi-cpu calls its even more interesting
 * as we'll have to ensure no other cpu is observing our csd.
 */
static void csd_lock_wait(struct call_single_data *csd)
{
	while (csd->flags & CSD_FLAG_LOCK)
		cpu_relax();
}

static void csd_lock(struct call_single_data *csd)
{
	csd_lock_wait(csd);
	csd->flags |= CSD_FLAG_LOCK;

	/*
	 * prevent CPU from reordering the above assignment
	 * to ->flags with any subsequent assignments to other
	 * fields of the specified call_single_data structure:
	 */
	smp_mb();
}

static void csd_unlock(struct call_single_data *csd)
{
	WARN_ON(!(csd->flags & CSD_FLAG_LOCK));

	/*
	 * ensure we're all done before releasing data:
	 */
	smp_mb();

	csd->flags &= ~CSD_FLAG_LOCK;
}

/*
 * Insert a previously allocated call_single_data element
 * for execution on the given CPU. data must already have
 * ->func, ->info, and ->flags set.
 */
static
void generic_exec_single(int cpu, struct call_single_data *csd, int wait)
{
	struct call_single_queue *dst = &per_cpu(call_single_queue, cpu);
	unsigned long flags;
	int ipi;

	raw_spin_lock_irqsave(&dst->lock, flags);
	ipi = list_empty(&dst->list);
	list_add_tail(&csd->list, &dst->list);
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
		csd_lock_wait(csd);
}

/*
 * Invoked by arch to handle an IPI for call function single. Must be
 * called from the arch with interrupts disabled.
 */
void generic_smp_call_function_single_interrupt(void)
{
	struct call_single_queue *q = &__get_cpu_var(call_single_queue);
	LIST_HEAD(list);

	/*
	 * Shouldn't receive this interrupt on a cpu that is not yet online.
	 */
	WARN_ON_ONCE(!cpu_online(smp_processor_id()));

	raw_spin_lock(&q->lock);
	list_replace_init(&q->list, &list);
	raw_spin_unlock(&q->lock);

	while (!list_empty(&list)) {
		struct call_single_data *csd;
		unsigned int csd_flags;

		csd = list_entry(list.next, struct call_single_data, list);
		list_del(&csd->list);

		/*
		 * 'csd' can be invalid after this call if flags == 0
		 * (when called through generic_exec_single()),
		 * so save them away before making the call:
		 */
		csd_flags = csd->flags;

		csd->func(csd->info);

		/*
		 * Unlocked CSDs are valid through generic_exec_single():
		 */
		if (csd_flags & CSD_FLAG_LOCK)
			csd_unlock(csd);
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
int smp_call_function_single(int cpu, smp_call_func_t func, void *info,
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
			struct call_single_data *csd = &d;

			if (!wait)
				csd = &__get_cpu_var(csd_data);

			csd_lock(csd);

			csd->func = func;
			csd->info = info;
			generic_exec_single(cpu, csd, wait);
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
 *
 * Selection preference:
 *	1) current cpu if in @mask
 *	2) any cpu of current node if in @mask
 *	3) any other online cpu in @mask
 */
int smp_call_function_any(const struct cpumask *mask,
			  smp_call_func_t func, void *info, int wait)
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
 * __smp_call_function_single(): Run a function on a specific CPU
 * @cpu: The CPU to run on.
 * @data: Pre-allocated and setup data structure
 * @wait: If true, wait until function has completed on specified CPU.
 *
 * Like smp_call_function_single(), but allow caller to pass in a
 * pre-allocated data structure. Useful for embedding @data inside
 * other structures, for instance.
 */
void __smp_call_function_single(int cpu, struct call_single_data *csd,
				int wait)
{
	unsigned int this_cpu;
	unsigned long flags;

	this_cpu = get_cpu();
	/*
	 * Can deadlock when called with interrupts disabled.
	 * We allow cpu's that are not yet online though, as no one else can
	 * send smp call function interrupt to this cpu and as such deadlocks
	 * can't happen.
	 */
	WARN_ON_ONCE(cpu_online(smp_processor_id()) && wait && irqs_disabled()
		     && !oops_in_progress);

	if (cpu == this_cpu) {
		local_irq_save(flags);
		csd->func(csd->info);
		local_irq_restore(flags);
	} else {
		csd_lock(csd);
		generic_exec_single(cpu, csd, wait);
	}
	put_cpu();
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
			    smp_call_func_t func, void *info, bool wait)
{
	struct call_function_data *cfd;
	int cpu, next_cpu, this_cpu = smp_processor_id();

	/*
	 * Can deadlock when called with interrupts disabled.
	 * We allow cpu's that are not yet online though, as no one else can
	 * send smp call function interrupt to this cpu and as such deadlocks
	 * can't happen.
	 */
	WARN_ON_ONCE(cpu_online(this_cpu) && irqs_disabled()
		     && !oops_in_progress && !early_boot_irqs_disabled);

	/* Try to fastpath.  So, what's a CPU they want? Ignoring this one. */
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

	cfd = &__get_cpu_var(cfd_data);

	cpumask_and(cfd->cpumask, mask, cpu_online_mask);
	cpumask_clear_cpu(this_cpu, cfd->cpumask);

	/* Some callers race with other cpus changing the passed mask */
	if (unlikely(!cpumask_weight(cfd->cpumask)))
		return;

	/*
	 * After we put an entry into the list, cfd->cpumask may be cleared
	 * again when another CPU sends another IPI for a SMP function call, so
	 * cfd->cpumask will be zero.
	 */
	cpumask_copy(cfd->cpumask_ipi, cfd->cpumask);

	for_each_cpu(cpu, cfd->cpumask) {
		struct call_single_data *csd = per_cpu_ptr(cfd->csd, cpu);
		struct call_single_queue *dst =
					&per_cpu(call_single_queue, cpu);
		unsigned long flags;

		csd_lock(csd);
		csd->func = func;
		csd->info = info;

		raw_spin_lock_irqsave(&dst->lock, flags);
		list_add_tail(&csd->list, &dst->list);
		raw_spin_unlock_irqrestore(&dst->lock, flags);
	}

	/* Send a message to all CPUs in the map */
	arch_send_call_function_ipi_mask(cfd->cpumask_ipi);

	if (wait) {
		for_each_cpu(cpu, cfd->cpumask) {
			struct call_single_data *csd;

			csd = per_cpu_ptr(cfd->csd, cpu);
			csd_lock_wait(csd);
		}
	}
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
int smp_call_function(smp_call_func_t func, void *info, int wait)
{
	preempt_disable();
	smp_call_function_many(cpu_online_mask, func, info, wait);
	preempt_enable();

	return 0;
}
EXPORT_SYMBOL(smp_call_function);
#endif /* USE_GENERIC_SMP_HELPERS */

/* Setup configured maximum number of CPUs to activate */
unsigned int setup_max_cpus = NR_CPUS;
EXPORT_SYMBOL(setup_max_cpus);


/*
 * Setup routine for controlling SMP activation
 *
 * Command-line option of "nosmp" or "maxcpus=0" will disable SMP
 * activation entirely (the MPS table probe still happens, though).
 *
 * Command-line option of "maxcpus=<NUM>", where <NUM> is an integer
 * greater than 0, limits the maximum number of CPUs activated in
 * SMP mode to <NUM>.
 */

void __weak arch_disable_smp_support(void) { }

static int __init nosmp(char *str)
{
	setup_max_cpus = 0;
	arch_disable_smp_support();

	return 0;
}

early_param("nosmp", nosmp);

/* this is hard limit */
static int __init nrcpus(char *str)
{
	int nr_cpus;

	get_option(&str, &nr_cpus);
	if (nr_cpus > 0 && nr_cpus < nr_cpu_ids)
		nr_cpu_ids = nr_cpus;

	return 0;
}

early_param("nr_cpus", nrcpus);

static int __init maxcpus(char *str)
{
	get_option(&str, &setup_max_cpus);
	if (setup_max_cpus == 0)
		arch_disable_smp_support();

	return 0;
}

early_param("maxcpus", maxcpus);

/* Setup number of possible processor ids */
int nr_cpu_ids __read_mostly = NR_CPUS;
EXPORT_SYMBOL(nr_cpu_ids);

/* An arch may set nr_cpu_ids earlier if needed, so this would be redundant */
void __init setup_nr_cpu_ids(void)
{
	nr_cpu_ids = find_last_bit(cpumask_bits(cpu_possible_mask),NR_CPUS) + 1;
}

/* Called by boot processor to activate the rest. */
void __init smp_init(void)
{
	unsigned int cpu;

	idle_threads_init();

	/* FIXME: This should be done in userspace --RR */
	for_each_present_cpu(cpu) {
		if (num_online_cpus() >= setup_max_cpus)
			break;
		if (!cpu_online(cpu))
			cpu_up(cpu);
	}

	/* Any cleanup work */
	printk(KERN_INFO "Brought up %ld CPUs\n", (long)num_online_cpus());
	smp_cpus_done(setup_max_cpus);
}

/*
 * Call a function on all processors.  May be used during early boot while
 * early_boot_irqs_disabled is set.  Use local_irq_save/restore() instead
 * of local_irq_disable/enable().
 */
int on_each_cpu(void (*func) (void *info), void *info, int wait)
{
	unsigned long flags;
	int ret = 0;

	preempt_disable();
	ret = smp_call_function(func, info, wait);
	local_irq_save(flags);
	func(info);
	local_irq_restore(flags);
	preempt_enable();
	return ret;
}
EXPORT_SYMBOL(on_each_cpu);

/**
 * on_each_cpu_mask(): Run a function on processors specified by
 * cpumask, which may include the local processor.
 * @mask: The set of cpus to run on (only runs on online subset).
 * @func: The function to run. This must be fast and non-blocking.
 * @info: An arbitrary pointer to pass to the function.
 * @wait: If true, wait (atomically) until function has completed
 *        on other CPUs.
 *
 * If @wait is true, then returns once @func has returned.
 *
 * You must not call this function with disabled interrupts or
 * from a hardware interrupt handler or from a bottom half handler.
 */
void on_each_cpu_mask(const struct cpumask *mask, smp_call_func_t func,
			void *info, bool wait)
{
	int cpu = get_cpu();

	smp_call_function_many(mask, func, info, wait);
	if (cpumask_test_cpu(cpu, mask)) {
		local_irq_disable();
		func(info);
		local_irq_enable();
	}
	put_cpu();
}
EXPORT_SYMBOL(on_each_cpu_mask);

/*
 * on_each_cpu_cond(): Call a function on each processor for which
 * the supplied function cond_func returns true, optionally waiting
 * for all the required CPUs to finish. This may include the local
 * processor.
 * @cond_func:	A callback function that is passed a cpu id and
 *		the the info parameter. The function is called
 *		with preemption disabled. The function should
 *		return a blooean value indicating whether to IPI
 *		the specified CPU.
 * @func:	The function to run on all applicable CPUs.
 *		This must be fast and non-blocking.
 * @info:	An arbitrary pointer to pass to both functions.
 * @wait:	If true, wait (atomically) until function has
 *		completed on other CPUs.
 * @gfp_flags:	GFP flags to use when allocating the cpumask
 *		used internally by the function.
 *
 * The function might sleep if the GFP flags indicates a non
 * atomic allocation is allowed.
 *
 * Preemption is disabled to protect against CPUs going offline but not online.
 * CPUs going online during the call will not be seen or sent an IPI.
 *
 * You must not call this function with disabled interrupts or
 * from a hardware interrupt handler or from a bottom half handler.
 */
void on_each_cpu_cond(bool (*cond_func)(int cpu, void *info),
			smp_call_func_t func, void *info, bool wait,
			gfp_t gfp_flags)
{
	cpumask_var_t cpus;
	int cpu, ret;

	might_sleep_if(gfp_flags & __GFP_WAIT);

	if (likely(zalloc_cpumask_var(&cpus, (gfp_flags|__GFP_NOWARN)))) {
		preempt_disable();
		for_each_online_cpu(cpu)
			if (cond_func(cpu, info))
				cpumask_set_cpu(cpu, cpus);
		on_each_cpu_mask(cpus, func, info, wait);
		preempt_enable();
		free_cpumask_var(cpus);
	} else {
		/*
		 * No free cpumask, bother. No matter, we'll
		 * just have to IPI them one by one.
		 */
		preempt_disable();
		for_each_online_cpu(cpu)
			if (cond_func(cpu, info)) {
				ret = smp_call_function_single(cpu, func,
								info, wait);
				WARN_ON_ONCE(!ret);
			}
		preempt_enable();
	}
}
EXPORT_SYMBOL(on_each_cpu_cond);

static void do_nothing(void *unused)
{
}

/**
 * kick_all_cpus_sync - Force all cpus out of idle
 *
 * Used to synchronize the update of pm_idle function pointer. It's
 * called after the pointer is updated and returns after the dummy
 * callback function has been executed on all cpus. The execution of
 * the function can only happen on the remote cpus after they have
 * left the idle function which had been called via pm_idle function
 * pointer. So it's guaranteed that nothing uses the previous pointer
 * anymore.
 */
void kick_all_cpus_sync(void)
{
	/* Make sure the change is visible before we kick the cpus */
	smp_mb();
	smp_call_function(do_nothing, NULL, 1);
}
EXPORT_SYMBOL_GPL(kick_all_cpus_sync);
