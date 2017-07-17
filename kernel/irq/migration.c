
#include <linux/irq.h>
#include <linux/interrupt.h>

#include "internals.h"

/**
 * irq_fixup_move_pending - Cleanup irq move pending from a dying CPU
 * @desc:		Interrupt descpriptor to clean up
 * @force_clear:	If set clear the move pending bit unconditionally.
 *			If not set, clear it only when the dying CPU is the
 *			last one in the pending mask.
 *
 * Returns true if the pending bit was set and the pending mask contains an
 * online CPU other than the dying CPU.
 */
bool irq_fixup_move_pending(struct irq_desc *desc, bool force_clear)
{
	struct irq_data *data = irq_desc_get_irq_data(desc);

	if (!irqd_is_setaffinity_pending(data))
		return false;

	/*
	 * The outgoing CPU might be the last online target in a pending
	 * interrupt move. If that's the case clear the pending move bit.
	 */
	if (cpumask_any_and(desc->pending_mask, cpu_online_mask) >= nr_cpu_ids) {
		irqd_clr_move_pending(data);
		return false;
	}
	if (force_clear)
		irqd_clr_move_pending(data);
	return true;
}

void irq_move_masked_irq(struct irq_data *idata)
{
	struct irq_desc *desc = irq_data_to_desc(idata);
	struct irq_chip *chip = desc->irq_data.chip;

	if (likely(!irqd_is_setaffinity_pending(&desc->irq_data)))
		return;

	irqd_clr_move_pending(&desc->irq_data);

	/*
	 * Paranoia: cpu-local interrupts shouldn't be calling in here anyway.
	 */
	if (irqd_is_per_cpu(&desc->irq_data)) {
		WARN_ON(1);
		return;
	}

	if (unlikely(cpumask_empty(desc->pending_mask)))
		return;

	if (!chip->irq_set_affinity)
		return;

	assert_raw_spin_locked(&desc->lock);

	/*
	 * If there was a valid mask to work with, please
	 * do the disable, re-program, enable sequence.
	 * This is *not* particularly important for level triggered
	 * but in a edge trigger case, we might be setting rte
	 * when an active trigger is coming in. This could
	 * cause some ioapics to mal-function.
	 * Being paranoid i guess!
	 *
	 * For correct operation this depends on the caller
	 * masking the irqs.
	 */
	if (cpumask_any_and(desc->pending_mask, cpu_online_mask) < nr_cpu_ids)
		irq_do_set_affinity(&desc->irq_data, desc->pending_mask, false);

	cpumask_clear(desc->pending_mask);
}

void irq_move_irq(struct irq_data *idata)
{
	bool masked;

	/*
	 * Get top level irq_data when CONFIG_IRQ_DOMAIN_HIERARCHY is enabled,
	 * and it should be optimized away when CONFIG_IRQ_DOMAIN_HIERARCHY is
	 * disabled. So we avoid an "#ifdef CONFIG_IRQ_DOMAIN_HIERARCHY" here.
	 */
	idata = irq_desc_get_irq_data(irq_data_to_desc(idata));

	if (likely(!irqd_is_setaffinity_pending(idata)))
		return;

	if (unlikely(irqd_irq_disabled(idata)))
		return;

	/*
	 * Be careful vs. already masked interrupts. If this is a
	 * threaded interrupt with ONESHOT set, we can end up with an
	 * interrupt storm.
	 */
	masked = irqd_irq_masked(idata);
	if (!masked)
		idata->chip->irq_mask(idata);
	irq_move_masked_irq(idata);
	if (!masked)
		idata->chip->irq_unmask(idata);
}
