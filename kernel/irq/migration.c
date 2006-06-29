
#include <linux/irq.h>

void set_pending_irq(unsigned int irq, cpumask_t mask)
{
	struct irq_desc *desc = irq_desc + irq;
	unsigned long flags;

	spin_lock_irqsave(&desc->lock, flags);
	desc->move_irq = 1;
	irq_desc[irq].pending_mask = mask;
	spin_unlock_irqrestore(&desc->lock, flags);
}

void move_native_irq(int irq)
{
	struct irq_desc *desc = irq_desc + irq;
	cpumask_t tmp;

	if (likely(!desc->move_irq))
		return;

	/*
	 * Paranoia: cpu-local interrupts shouldn't be calling in here anyway.
	 */
	if (CHECK_IRQ_PER_CPU(desc->status)) {
		WARN_ON(1);
		return;
	}

	desc->move_irq = 0;

	if (unlikely(cpus_empty(irq_desc[irq].pending_mask)))
		return;

	if (!desc->chip->set_affinity)
		return;

	assert_spin_locked(&desc->lock);

	cpus_and(tmp, irq_desc[irq].pending_mask, cpu_online_map);

	/*
	 * If there was a valid mask to work with, please
	 * do the disable, re-program, enable sequence.
	 * This is *not* particularly important for level triggered
	 * but in a edge trigger case, we might be setting rte
	 * when an active trigger is comming in. This could
	 * cause some ioapics to mal-function.
	 * Being paranoid i guess!
	 */
	if (likely(!cpus_empty(tmp))) {
		if (likely(!(desc->status & IRQ_DISABLED)))
			desc->chip->disable(irq);

		desc->chip->set_affinity(irq,tmp);

		if (likely(!(desc->status & IRQ_DISABLED)))
			desc->chip->enable(irq);
	}
	cpus_clear(irq_desc[irq].pending_mask);
}
