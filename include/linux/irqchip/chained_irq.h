/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Chained IRQ handlers support.
 *
 * Copyright (C) 2011 ARM Ltd.
 */
#ifndef __IRQCHIP_CHAINED_IRQ_H
#define __IRQCHIP_CHAINED_IRQ_H

#include <linux/irq.h>

/*
 * Entry/exit functions for chained handlers where the primary IRQ chip
 * may implement either fasteoi or level-trigger flow control.
 */
static inline void chained_irq_enter(struct irq_chip *chip,
				     struct irq_desc *desc)
{
	/* FastEOI controllers require no action on entry. */
	if (chip->irq_eoi)
		return;

	if (chip->irq_mask_ack) {
		chip->irq_mask_ack(&desc->irq_data);
	} else {
		chip->irq_mask(&desc->irq_data);
		if (chip->irq_ack)
			chip->irq_ack(&desc->irq_data);
	}
}

static inline void chained_irq_exit(struct irq_chip *chip,
				    struct irq_desc *desc)
{
	if (chip->irq_eoi)
		chip->irq_eoi(&desc->irq_data);
	else
		chip->irq_unmask(&desc->irq_data);
}

#endif /* __IRQCHIP_CHAINED_IRQ_H */
