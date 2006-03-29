#include <linux/irq.h>

#if defined(CONFIG_GENERIC_PENDING_IRQ)

void set_pending_irq(unsigned int irq, cpumask_t mask)
{
	irq_desc_t *desc = irq_desc + irq;
	unsigned long flags;

	spin_lock_irqsave(&desc->lock, flags);
	desc->move_irq = 1;
	pending_irq_cpumask[irq] = mask;
	spin_unlock_irqrestore(&desc->lock, flags);
}

void move_native_irq(int irq)
{
	cpumask_t tmp;
	irq_desc_t *desc = irq_descp(irq);

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

	if (likely(cpus_empty(pending_irq_cpumask[irq])))
		return;

	if (!desc->handler->set_affinity)
		return;

	assert_spin_locked(&desc->lock);

	cpus_and(tmp, pending_irq_cpumask[irq], cpu_online_map);

	/*
	 * If there was a valid mask to work with, please
	 * do the disable, re-program, enable sequence.
	 * This is *not* particularly important for level triggered
	 * but in a edge trigger case, we might be setting rte
	 * when an active trigger is comming in. This could
	 * cause some ioapics to mal-function.
	 * Being paranoid i guess!
	 */
	if (unlikely(!cpus_empty(tmp))) {
		if (likely(!(desc->status & IRQ_DISABLED)))
			desc->handler->disable(irq);

		desc->handler->set_affinity(irq,tmp);

		if (likely(!(desc->status & IRQ_DISABLED)))
			desc->handler->enable(irq);
	}
	cpus_clear(pending_irq_cpumask[irq]);
}

#endif
