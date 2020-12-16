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
#include <asm/processor.h>


static DEFINE_PER_CPU(struct llist_head, raised_list);
static DEFINE_PER_CPU(struct llist_head, lazy_list);

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
	/* If the work is "lazy", handle it from next tick if any */
	if (atomic_read(&work->node.a_flags) & IRQ_WORK_LAZY) {
		if (llist_add(&work->node.llist, this_cpu_ptr(&lazy_list)) &&
		    tick_nohz_tick_stopped())
			arch_irq_work_raise();
	} else {
		if (llist_add(&work->node.llist, this_cpu_ptr(&raised_list)))
			arch_irq_work_raise();
	}
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

	preempt_disable();
	if (cpu != smp_processor_id()) {
		/* Arch remote IPI send/receive backend aren't NMI safe */
		WARN_ON_ONCE(in_nmi());
		__smp_call_single_queue(cpu, &work->node.llist);
	} else {
		__irq_work_queue_local(work);
	}
	preempt_enable();

	return true;
#endif /* CONFIG_SMP */
}
EXPORT_SYMBOL_GPL(irq_work_queue_on);

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
}

static void irq_work_run_list(struct llist_head *list)
{
	struct irq_work *work, *tmp;
	struct llist_node *llnode;

	BUG_ON(!irqs_disabled());

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
	irq_work_run_list(this_cpu_ptr(&lazy_list));
}
EXPORT_SYMBOL_GPL(irq_work_run);

void irq_work_tick(void)
{
	struct llist_head *raised = this_cpu_ptr(&raised_list);

	if (!llist_empty(raised) && !arch_irq_work_has_interrupt())
		irq_work_run_list(raised);
	irq_work_run_list(this_cpu_ptr(&lazy_list));
}

/*
 * Synchronize against the irq_work @entry, ensures the entry is not
 * currently in use.
 */
void irq_work_sync(struct irq_work *work)
{
	lockdep_assert_irqs_enabled();

	while (irq_work_is_busy(work))
		cpu_relax();
}
EXPORT_SYMBOL_GPL(irq_work_sync);
