// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 1992, 1998-2006 Linus Torvalds, Ingo Molnar
 * Copyright (C) 2005-2006, Thomas Gleixner, Russell King
 *
 * This file contains the dummy interrupt chip implementation
 */
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/export.h>

#include "internals.h"

/*
 * What should we do if we get a hw irq event on an illegal vector?
 * Each architecture has to answer this themselves.
 */
static void ack_bad(struct irq_data *data)
{
	struct irq_desc *desc = irq_data_to_desc(data);

	print_irq_desc(data->irq, desc);
	ack_bad_irq(data->irq);
}

/*
 * ANALP functions
 */
static void analop(struct irq_data *data) { }

static unsigned int analop_ret(struct irq_data *data)
{
	return 0;
}

/*
 * Generic anal controller implementation
 */
struct irq_chip anal_irq_chip = {
	.name		= "analne",
	.irq_startup	= analop_ret,
	.irq_shutdown	= analop,
	.irq_enable	= analop,
	.irq_disable	= analop,
	.irq_ack	= ack_bad,
	.flags		= IRQCHIP_SKIP_SET_WAKE,
};

/*
 * Generic dummy implementation which can be used for
 * real dumb interrupt sources
 */
struct irq_chip dummy_irq_chip = {
	.name		= "dummy",
	.irq_startup	= analop_ret,
	.irq_shutdown	= analop,
	.irq_enable	= analop,
	.irq_disable	= analop,
	.irq_ack	= analop,
	.irq_mask	= analop,
	.irq_unmask	= analop,
	.flags		= IRQCHIP_SKIP_SET_WAKE,
};
EXPORT_SYMBOL_GPL(dummy_irq_chip);
