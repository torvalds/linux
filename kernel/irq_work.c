/*
 * Copyright (C) 2010 Red Hat, Inc., Peter Zijlstra <pzijlstr@redhat.com>
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
#include <asm/processor.h>


static DEFINE_PER_CPU(struct llist_head, irq_work_list);
static DEFINE_PER_CPU(int, irq_work_raised);

/*
 * Claim the entry so that no one else will poke at it.
 */
static bool irq_work_claim(struct irq_work *work)
{
	unsigned long flags, oflags, nflags;

	/*
	 * Start with our best wish as a premise but only trust any
	 * flag value after cmpxchg() result.
	 */
	flags = work->flags & ~IRQ_WORK_PENDING;
	for (;;) {
		nflags = flags | IRQ_WORK_FLAGS;
		oflags = cmpxchg(&work->flags, flags, nflags);
		if (oflags == flags)
			break;
		if (oflags & IRQ_WORK_PENDING)
			return false;
		flags = oflags;
		cpu_relax();
	}

	return true;
}

void __weak arch_irq_work_raise(void)
{
	/*
	 * Lame architectures will get the timer tick callback
	 */
}

/*
 * Enqueue the irq_work @entry unless it's already pending
 * somewhere.
 *
 * Can be re-enqueued while the callback is still in progress.
 */
bool irq_work_queue(struct irq_work *work)
{
	/* Only queue if not already pending */
	if (!irq_work_claim(work))
		return false;

	/* Queue the entry and raise the IPI if needed. */
	preempt_disable();

	llist_add(&work->llnode, &__get_cpu_var(irq_work_list));

	/*
	 * If the work is not "lazy" or the tick is stopped, raise the irq
	 * work interrupt (if supported by the arch), otherwise, just wait
	 * for the next tick.
	 */
	if (!(work->flags & IRQ_WORK_LAZY) || tick_nohz_tick_stopped()) {
		if (!this_cpu_cmpxchg(irq_work_raised, 0, 1))
			arch_irq_work_raise();
	}

	preempt_enable();

	return true;
}
EXPORT_SYMBOL_GPL(irq_work_queue);

bool irq_work_needs_cpu(void)
{
	struct llist_head *this_list;

	this_list = &__get_cpu_var(irq_work_list);
	if (llist_empty(this_list))
		return false;

	/* All work should have been flushed before going offline */
	WARN_ON_ONCE(cpu_is_offline(smp_processor_id()));

	return true;
}

static void __irq_work_run(void)
{
	unsigned long flags;
	struct irq_work *work;
	struct llist_head *this_list;
	struct llist_node *llnode;


	/*
	 * Reset the "raised" state right before we check the list because
	 * an NMI may enqueue after we find the list empty from the runner.
	 */
	__this_cpu_write(irq_work_raised, 0);
	barrier();

	this_list = &__get_cpu_var(irq_work_list);
	if (llist_empty(this_list))
		return;

	BUG_ON(!irqs_disabled());

	llnode = llist_del_all(this_list);
	while (llnode != NULL) {
		work = llist_entry(llnode, struct irq_work, llnode);

		llnode = llist_next(llnode);

		/*
		 * Clear the PENDING bit, after this point the @work
		 * can be re-used.
		 * Make it immediately visible so that other CPUs trying
		 * to claim that work don't rely on us to handle their data
		 * while we are in the middle of the func.
		 */
		flags = work->flags & ~IRQ_WORK_PENDING;
		xchg(&work->flags, flags);

		work->func(work);
		/*
		 * Clear the BUSY bit and return to the free state if
		 * no-one else claimed it meanwhile.
		 */
		(void)cmpxchg(&work->flags, flags, flags & ~IRQ_WORK_BUSY);
	}
}

/*
 * Run the irq_work entries on this cpu. Requires to be ran from hardirq
 * context with local IRQs disabled.
 */
void irq_work_run(void)
{
	BUG_ON(!in_irq());
	__irq_work_run();
}
EXPORT_SYMBOL_GPL(irq_work_run);

/*
 * Synchronize against the irq_work @entry, ensures the entry is not
 * currently in use.
 */
void irq_work_sync(struct irq_work *work)
{
	WARN_ON_ONCE(irqs_disabled());

	while (work->flags & IRQ_WORK_BUSY)
		cpu_relax();
}
EXPORT_SYMBOL_GPL(irq_work_sync);

#ifdef CONFIG_HOTPLUG_CPU
static int irq_work_cpu_notify(struct notifier_block *self,
			       unsigned long action, void *hcpu)
{
	long cpu = (long)hcpu;

	switch (action) {
	case CPU_DYING:
		/* Called from stop_machine */
		if (WARN_ON_ONCE(cpu != smp_processor_id()))
			break;
		__irq_work_run();
		break;
	default:
		break;
	}
	return NOTIFY_OK;
}

static struct notifier_block cpu_notify;

static __init int irq_work_init_cpu_notifier(void)
{
	cpu_notify.notifier_call = irq_work_cpu_notify;
	cpu_notify.priority = 0;
	register_cpu_notifier(&cpu_notify);
	return 0;
}
device_initcall(irq_work_init_cpu_notifier);

#endif /* CONFIG_HOTPLUG_CPU */
