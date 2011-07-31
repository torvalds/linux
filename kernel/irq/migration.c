
#include <linux/irq.h>
#include <linux/interrupt.h>

#include "internals.h"

void move_masked_irq(int irq)
{
	struct irq_desc *desc = irq_to_desc(irq);

	if (likely(!(desc->status & IRQ_MOVE_PENDING)))
		return;

	/*
	 * Paranoia: cpu-local interrupts shouldn't be calling in here anyway.
	 */
	if (CHECK_IRQ_PER_CPU(desc->status)) {
		WARN_ON(1);
		return;
	}

	desc->status &= ~IRQ_MOVE_PENDING;

	if (unlikely(cpumask_empty(desc->pending_mask)))
		return;

	if (!desc->chip->set_affinity)
		return;

	assert_raw_spin_locked(&desc->lock);

	/*
	 * If there was a valid mask to work with, please
	 * do the disable, re-program, enable sequence.
	 * This is *not* particularly important for level triggered
	 * but in a edge trigger case, we might be setting rte
	 * when an active trigger is comming in. This could
	 * cause some ioapics to mal-function.
	 * Being paranoid i guess!
	 *
	 * For correct operation this depends on the caller
	 * masking the irqs.
	 */
	if (likely(cpumask_any_and(desc->pending_mask, cpu_online_mask)
		   < nr_cpu_ids))
		if (!desc->chip->set_affinity(irq, desc->pending_mask)) {
			cpumask_copy(desc->affinity, desc->pending_mask);
			irq_set_thread_affinity(desc);
		}

	cpumask_clear(desc->pending_mask);
}

void move_native_irq(int irq)
{
	struct irq_desc *desc = irq_to_desc(irq);

	if (likely(!(desc->status & IRQ_MOVE_PENDING)))
		return;

	if (unlikely(desc->status & IRQ_DISABLED))
		return;

	desc->chip->mask(irq);
	move_masked_irq(irq);
	desc->chip->unmask(irq);
}

