/*
 * Copyright (C) 2010 Red Hat, Inc., Peter Zijlstra <pzijlstr@redhat.com>
 *
 * Provides a framework for enqueueing and running callbacks from hardirq
 * context. The enqueueing is NMI-safe.
 */

#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/irq_work.h>
#include <linux/percpu.h>
#include <linux/hardirq.h>
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
	unsigned long flags, nflags;

	for (;;) {
		flags = work->flags;
		if (flags & IRQ_WORK_PENDING)
			return false;
		nflags = flags | IRQ_WORK_FLAGS;
		if (cmpxchg(&work->flags, flags, nflags) == flags)
			break;
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

/*
 * Run the irq_work entries on this cpu. Requires to be ran from hardirq
 * context with local IRQs disabled.
 */
void irq_work_run(void)
{
	struct irq_work *work;
	struct llist_head *this_list;
	struct llist_node *llnode;

	this_list = &__get_cpu_var(irq_work_list);
	if (llist_empty(this_list))
		return;

	BUG_ON(!in_irq());
	BUG_ON(!irqs_disabled());

	llnode = llist_del_all(this_list);
	while (llnode != NULL) {
		work = llist_entry(llnode, struct irq_work, llnode);

		llnode = llist_next(llnode);

		/*
		 * Clear the PENDING bit, after this point the @work
		 * can be re-used.
		 */
		work->flags = IRQ_WORK_BUSY;
		work->func(work);
		/*
		 * Clear the BUSY bit and return to the free state if
		 * no-one else claimed it meanwhile.
		 */
		(void)cmpxchg(&work->flags, IRQ_WORK_BUSY, 0);
	}
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
