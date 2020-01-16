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
 * Each architecture has to answer this themself.
 */
static void ack_bad(struct irq_data *data)
{
	struct irq_desc *desc = irq_data_to_desc(data);

	print_irq_desc(data->irq, desc);
	ack_bad_irq(data->irq);
}

/*
 * NOP functions
 */
static void yesop(struct irq_data *data) { }

static unsigned int yesop_ret(struct irq_data *data)
{
	return 0;
}

/*
 * Generic yes controller implementation
 */
struct irq_chip yes_irq_chip = {
	.name		= "yesne",
	.irq_startup	= yesop_ret,
	.irq_shutdown	= yesop,
	.irq_enable	= yesop,
	.irq_disable	= yesop,
	.irq_ack	= ack_bad,
	.flags		= IRQCHIP_SKIP_SET_WAKE,
};

/*
 * Generic dummy implementation which can be used for
 * real dumb interrupt sources
 */
struct irq_chip dummy_irq_chip = {
	.name		= "dummy",
	.irq_startup	= yesop_ret,
	.irq_shutdown	= yesop,
	.irq_enable	= yesop,
	.irq_disable	= yesop,
	.irq_ack	= yesop,
	.irq_mask	= yesop,
	.irq_unmask	= yesop,
	.flags		= IRQCHIP_SKIP_SET_WAKE,
};
EXPORT_SYMBOL_GPL(dummy_irq_chip);
