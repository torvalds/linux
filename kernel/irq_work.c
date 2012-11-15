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
#include <linux/cpu.h>
#include <linux/notifier.h>
#include <asm/processor.h>

/*
 * An entry can be in one of four states:
 *
 * free	     NULL, 0 -> {claimed}       : free to be used
 * claimed   NULL, 3 -> {pending}       : claimed to be enqueued
 * pending   next, 3 -> {busy}          : queued, pending callback
 * busy      NULL, 2 -> {free, claimed} : callback in progress, can be claimed
 */

#define IRQ_WORK_PENDING	1UL
#define IRQ_WORK_BUSY		2UL
#define IRQ_WORK_FLAGS		3UL

static DEFINE_PER_CPU(struct llist_head, irq_work_list);

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
 * Queue the entry and raise the IPI if needed.
 */
static void __irq_work_queue(struct irq_work *work)
{
	bool empty;

	preempt_disable();

	empty = llist_add(&work->llnode, &__get_cpu_var(irq_work_list));
	/* The list was empty, raise self-interrupt to start processing. */
	if (empty)
		arch_irq_work_raise();

	preempt_enable();
}

/*
 * Enqueue the irq_work @entry, returns true on success, failure when the
 * @entry was already enqueued by someone else.
 *
 * Can be re-enqueued while the callback is still in progress.
 */
bool irq_work_queue(struct irq_work *work)
{
	if (!irq_work_claim(work)) {
		/*
		 * Already enqueued, can't do!
		 */
		return false;
	}

	__irq_work_queue(work);
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
	struct irq_work *work;
	struct llist_head *this_list;
	struct llist_node *llnode;

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
		xchg(&work->flags, IRQ_WORK_BUSY);
		work->func(work);
		/*
		 * Clear the BUSY bit and return to the free state if
		 * no-one else claimed it meanwhile.
		 */
		(void)cmpxchg(&work->flags, IRQ_WORK_BUSY, 0);
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
