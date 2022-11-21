// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2010 Red Hat, Inc., Peter Zijlstra
 *
 * Provides a framework for enqueueing and running callbacks from hardirq
 * context. The enqueueing is NMI-safe.
 */

#include <linux/bug.h>
#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/irq_work.h>
#include <linux/percpu.h>
#include <linux/hardirq.h>
#include <linux/irqflags.h>
#include <linux/sched.h>
#include <linux/tick.h>
#include <linux/cpu.h>
#include <linux/notifier.h>
#include <linux/smp.h>
#include <linux/smpboot.h>
#include <asm/processor.h>
#include <linux/kasan.h>

static DEFINE_PER_CPU(struct llist_head, raised_list);
static DEFINE_PER_CPU(struct llist_head, lazy_list);
static DEFINE_PER_CPU(struct task_struct *, irq_workd);

static void wake_irq_workd(void)
{
	struct task_struct *tsk = __this_cpu_read(irq_workd);

	if (!llist_empty(this_cpu_ptr(&lazy_list)) && tsk)
		wake_up_process(tsk);
}

#ifdef CONFIG_SMP
static void irq_work_wake(struct irq_work *entry)
{
	wake_irq_workd();
}

static DEFINE_PER_CPU(struct irq_work, irq_work_wakeup) =
	IRQ_WORK_INIT_HARD(irq_work_wake);
#endif

static int irq_workd_should_run(unsigned int cpu)
{
	return !llist_empty(this_cpu_ptr(&lazy_list));
}

/*
 * Claim the entry so that no one else will poke at it.
 */
static bool irq_work_claim(struct irq_work *work)
{
	int oflags;

	oflags = atomic_fetch_or(IRQ_WORK_CLAIMED | CSD_TYPE_IRQ_WORK, &work->node.a_flags);
	/*
	 * If the work is already pending, no need to raise the IPI.
	 * The pairing smp_mb() in irq_work_single() makes sure
	 * everything we did before is visible.
	 */
	if (oflags & IRQ_WORK_PENDING)
		return false;
	return true;
}

void __weak arch_irq_work_raise(void)
{
	/*
	 * Lame architectures will get the timer tick callback
	 */
}

/* Enqueue on current CPU, work must already be claimed and preempt disabled */
static void __irq_work_queue_local(struct irq_work *work)
{
	struct llist_head *list;
	bool rt_lazy_work = false;
	bool lazy_work = false;
	int work_flags;

	work_flags = atomic_read(&work->node.a_flags);
	if (work_flags & IRQ_WORK_LAZY)
		lazy_work = true;
	else if (IS_ENABLED(CONFIG_PREEMPT_RT) &&
		 !(work_flags & IRQ_WORK_HARD_IRQ))
		rt_lazy_work = true;

	if (lazy_work || rt_lazy_work)
		list = this_cpu_ptr(&lazy_list);
	else
		list = this_cpu_ptr(&raised_list);

	if (!llist_add(&work->node.llist, list))
		return;

	/* If the work is "lazy", handle it from next tick if any */
	if (!lazy_work || tick_nohz_tick_stopped())
		arch_irq_work_raise();
}

/* Enqueue the irq work @work on the current CPU */
bool irq_work_queue(struct irq_work *work)
{
	/* Only queue if not already pending */
	if (!irq_work_claim(work))
		return false;

	/* Queue the entry and raise the IPI if needed. */
	preempt_disable();
	__irq_work_queue_local(work);
	preempt_enable();

	return true;
}
EXPORT_SYMBOL_GPL(irq_work_queue);

/*
 * Enqueue the irq_work @work on @cpu unless it's already pending
 * somewhere.
 *
 * Can be re-enqueued while the callback is still in progress.
 */
bool irq_work_queue_on(struct irq_work *work, int cpu)
{
#ifndef CONFIG_SMP
	return irq_work_queue(work);

#else /* CONFIG_SMP: */
	/* All work should have been flushed before going offline */
	WARN_ON_ONCE(cpu_is_offline(cpu));

	/* Only queue if not already pending */
	if (!irq_work_claim(work))
		return false;

	kasan_record_aux_stack_noalloc(work);

	preempt_disable();
	if (cpu != smp_processor_id()) {
		/* Arch remote IPI send/receive backend aren't NMI safe */
		WARN_ON_ONCE(in_nmi());

		/*
		 * On PREEMPT_RT the items which are not marked as
		 * IRQ_WORK_HARD_IRQ are added to the lazy list and a HARD work
		 * item is used on the remote CPU to wake the thread.
		 */
		if (IS_ENABLED(CONFIG_PREEMPT_RT) &&
		    !(atomic_read(&work->node.a_flags) & IRQ_WORK_HARD_IRQ)) {

			if (!llist_add(&work->node.llist, &per_cpu(lazy_list, cpu)))
				goto out;

			work = &per_cpu(irq_work_wakeup, cpu);
			if (!irq_work_claim(work))
				goto out;
		}

		__smp_call_single_queue(cpu, &work->node.llist);
	} else {
		__irq_work_queue_local(work);
	}
out:
	preempt_enable();

	return true;
#endif /* CONFIG_SMP */
}

bool irq_work_needs_cpu(void)
{
	struct llist_head *raised, *lazy;

	raised = this_cpu_ptr(&raised_list);
	lazy = this_cpu_ptr(&lazy_list);

	if (llist_empty(raised) || arch_irq_work_has_interrupt())
		if (llist_empty(lazy))
			return false;

	/* All work should have been flushed before going offline */
	WARN_ON_ONCE(cpu_is_offline(smp_processor_id()));

	return true;
}

void irq_work_single(void *arg)
{
	struct irq_work *work = arg;
	int flags;

	/*
	 * Clear the PENDING bit, after this point the @work can be re-used.
	 * The PENDING bit acts as a lock, and we own it, so we can clear it
	 * without atomic ops.
	 */
	flags = atomic_read(&work->node.a_flags);
	flags &= ~IRQ_WORK_PENDING;
	atomic_set(&work->node.a_flags, flags);

	/*
	 * See irq_work_claim().
	 */
	smp_mb();

	lockdep_irq_work_enter(flags);
	work->func(work);
	lockdep_irq_work_exit(flags);

	/*
	 * Clear the BUSY bit, if set, and return to the free state if no-one
	 * else claimed it meanwhile.
	 */
	(void)atomic_cmpxchg(&work->node.a_flags, flags, flags & ~IRQ_WORK_BUSY);

	if ((IS_ENABLED(CONFIG_PREEMPT_RT) && !irq_work_is_hard(work)) ||
	    !arch_irq_work_has_interrupt())
		rcuwait_wake_up(&work->irqwait);
}

static void irq_work_run_list(struct llist_head *list)
{
	struct irq_work *work, *tmp;
	struct llist_node *llnode;

	/*
	 * On PREEMPT_RT IRQ-work which is not marked as HARD will be processed
	 * in a per-CPU thread in preemptible context. Only the items which are
	 * marked as IRQ_WORK_HARD_IRQ will be processed in hardirq context.
	 */
	BUG_ON(!irqs_disabled() && !IS_ENABLED(CONFIG_PREEMPT_RT));

	if (llist_empty(list))
		return;

	llnode = llist_del_all(list);
	llist_for_each_entry_safe(work, tmp, llnode, node.llist)
		irq_work_single(work);
}

/*
 * hotplug calls this through:
 *  hotplug_cfd() -> flush_smp_call_function_queue()
 */
void irq_work_run(void)
{
	irq_work_run_list(this_cpu_ptr(&raised_list));
	if (!IS_ENABLED(CONFIG_PREEMPT_RT))
		irq_work_run_list(this_cpu_ptr(&lazy_list));
	else
		wake_irq_workd();
}
EXPORT_SYMBOL_GPL(irq_work_run);

void irq_work_tick(void)
{
	struct llist_head *raised = this_cpu_ptr(&raised_list);

	if (!llist_empty(raised) && !arch_irq_work_has_interrupt())
		irq_work_run_list(raised);

	if (!IS_ENABLED(CONFIG_PREEMPT_RT))
		irq_work_run_list(this_cpu_ptr(&lazy_list));
	else
		wake_irq_workd();
}

/*
 * Synchronize against the irq_work @entry, ensures the entry is not
 * currently in use.
 */
void irq_work_sync(struct irq_work *work)
{
	lockdep_assert_irqs_enabled();
	might_sleep();

	if ((IS_ENABLED(CONFIG_PREEMPT_RT) && !irq_work_is_hard(work)) ||
	    !arch_irq_work_has_interrupt()) {
		rcuwait_wait_event(&work->irqwait, !irq_work_is_busy(work),
				   TASK_UNINTERRUPTIBLE);
		return;
	}

	while (irq_work_is_busy(work))
		cpu_relax();
}
EXPORT_SYMBOL_GPL(irq_work_sync);

static void run_irq_workd(unsigned int cpu)
{
	irq_work_run_list(this_cpu_ptr(&lazy_list));
}

static void irq_workd_setup(unsigned int cpu)
{
	sched_set_fifo_low(current);
}

static struct smp_hotplug_thread irqwork_threads = {
	.store                  = &irq_workd,
	.setup			= irq_workd_setup,
	.thread_should_run      = irq_workd_should_run,
	.thread_fn              = run_irq_workd,
	.thread_comm            = "irq_work/%u",
};

static __init int irq_work_init_threads(void)
{
	if (IS_ENABLED(CONFIG_PREEMPT_RT))
		BUG_ON(smpboot_register_percpu_thread(&irqwork_threads));
	return 0;
}
early_initcall(irq_work_init_threads);
