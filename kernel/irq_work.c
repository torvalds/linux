/*
 * Copyright (C) 2010 Red Hat, Inc., Peter Zijlstra <pzijlstr@redhat.com>
 *
 * Provides a framework for enqueueing and running callbacks from hardirq
 * context. The enqueueing is NMI-safe.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/irq_work.h>
#include <linux/hardirq.h>

/*
 * An entry can be in one of four states:
 *
 * free	     NULL, 0 -> {claimed}       : free to be used
 * claimed   NULL, 3 -> {pending}       : claimed to be enqueued
 * pending   next, 3 -> {busy}          : queued, pending callback
 * busy      NULL, 2 -> {free, claimed} : callback in progress, can be claimed
 *
 * We use the lower two bits of the next pointer to keep PENDING and BUSY
 * flags.
 */

#define IRQ_WORK_PENDING	1UL
#define IRQ_WORK_BUSY		2UL
#define IRQ_WORK_FLAGS		3UL

static inline bool irq_work_is_set(struct irq_work *entry, int flags)
{
	return (unsigned long)entry->next & flags;
}

static inline struct irq_work *irq_work_next(struct irq_work *entry)
{
	unsigned long next = (unsigned long)entry->next;
	next &= ~IRQ_WORK_FLAGS;
	return (struct irq_work *)next;
}

static inline struct irq_work *next_flags(struct irq_work *entry, int flags)
{
	unsigned long next = (unsigned long)entry;
	next |= flags;
	return (struct irq_work *)next;
}

static DEFINE_PER_CPU(struct irq_work *, irq_work_list);

/*
 * Claim the entry so that no one else will poke at it.
 */
static bool irq_work_claim(struct irq_work *entry)
{
	struct irq_work *next, *nflags;

	do {
		next = entry->next;
		if ((unsigned long)next & IRQ_WORK_PENDING)
			return false;
		nflags = next_flags(next, IRQ_WORK_FLAGS);
	} while (cmpxchg(&entry->next, next, nflags) != next);

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
static void __irq_work_queue(struct irq_work *entry)
{
	struct irq_work **head, *next;

	head = &get_cpu_var(irq_work_list);

	do {
		next = *head;
		/* Can assign non-atomic because we keep the flags set. */
		entry->next = next_flags(next, IRQ_WORK_FLAGS);
	} while (cmpxchg(head, next, entry) != next);

	/* The list was empty, raise self-interrupt to start processing. */
	if (!irq_work_next(entry))
		arch_irq_work_raise();

	put_cpu_var(irq_work_list);
}

/*
 * Enqueue the irq_work @entry, returns true on success, failure when the
 * @entry was already enqueued by someone else.
 *
 * Can be re-enqueued while the callback is still in progress.
 */
bool irq_work_queue(struct irq_work *entry)
{
	if (!irq_work_claim(entry)) {
		/*
		 * Already enqueued, can't do!
		 */
		return false;
	}

	__irq_work_queue(entry);
	return true;
}
EXPORT_SYMBOL_GPL(irq_work_queue);

/*
 * Run the irq_work entries on this cpu. Requires to be ran from hardirq
 * context with local IRQs disabled.
 */
void irq_work_run(void)
{
	struct irq_work *list, **head;

	head = &__get_cpu_var(irq_work_list);
	if (*head == NULL)
		return;

	BUG_ON(!in_irq());
	BUG_ON(!irqs_disabled());

	list = xchg(head, NULL);
	while (list != NULL) {
		struct irq_work *entry = list;

		list = irq_work_next(list);

		/*
		 * Clear the PENDING bit, after this point the @entry
		 * can be re-used.
		 */
		entry->next = next_flags(NULL, IRQ_WORK_BUSY);
		entry->func(entry);
		/*
		 * Clear the BUSY bit and return to the free state if
		 * no-one else claimed it meanwhile.
		 */
		cmpxchg(&entry->next, next_flags(NULL, IRQ_WORK_BUSY), NULL);
	}
}
EXPORT_SYMBOL_GPL(irq_work_run);

/*
 * Synchronize against the irq_work @entry, ensures the entry is not
 * currently in use.
 */
void irq_work_sync(struct irq_work *entry)
{
	WARN_ON_ONCE(irqs_disabled());

	while (irq_work_is_set(entry, IRQ_WORK_BUSY))
		cpu_relax();
}
EXPORT_SYMBOL_GPL(irq_work_sync);
