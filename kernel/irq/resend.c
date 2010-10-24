/*
 * linux/kernel/irq/resend.c
 *
 * Copyright (C) 1992, 1998-2006 Linus Torvalds, Ingo Molnar
 * Copyright (C) 2005-2006, Thomas Gleixner
 *
 * This file contains the IRQ-resend code
 *
 * If the interrupt is waiting to be processed, we try to re-run it.
 * We can't directly run it from here since the caller might be in an
 * interrupt-protected region. Not all irq controller chips can
 * retrigger interrupts at the hardware level, so in those cases
 * we allow the resending of IRQs via a tasklet.
 */

#include <linux/irq.h>
#include <linux/module.h>
#include <linux/random.h>
#include <linux/interrupt.h>

#include "internals.h"

#ifdef CONFIG_HARDIRQS_SW_RESEND

/* Bitmap to handle software resend of interrupts: */
static DECLARE_BITMAP(irqs_resend, NR_IRQS);

/*
 * Run software resends of IRQ's
 */
static void resend_irqs(unsigned long arg)
{
	struct irq_desc *desc;
	int irq;

	while (!bitmap_empty(irqs_resend, nr_irqs)) {
		irq = find_first_bit(irqs_resend, nr_irqs);
		clear_bit(irq, irqs_resend);
		desc = irq_to_desc(irq);
		local_irq_disable();
		desc->handle_irq(irq, desc);
		local_irq_enable();
	}
}

/* Tasklet to handle resend: */
static DECLARE_TASKLET(resend_tasklet, resend_irqs, 0);

#endif

/*
 * IRQ resend
 *
 * Is called with interrupts disabled and desc->lock held.
 */
void check_irq_resend(struct irq_desc *desc, unsigned int irq)
{
	unsigned int status = desc->status;

	/*
	 * Make sure the interrupt is enabled, before resending it:
	 */
	desc->irq_data.chip->irq_enable(&desc->irq_data);

	/*
	 * We do not resend level type interrupts. Level type
	 * interrupts are resent by hardware when they are still
	 * active.
	 */
	if ((status & (IRQ_LEVEL | IRQ_PENDING | IRQ_REPLAY)) == IRQ_PENDING) {
		desc->status = (status & ~IRQ_PENDING) | IRQ_REPLAY;

		if (!desc->irq_data.chip->irq_retrigger ||
		    !desc->irq_data.chip->irq_retrigger(&desc->irq_data)) {
#ifdef CONFIG_HARDIRQS_SW_RESEND
			/* Set it pending and activate the softirq: */
			set_bit(irq, irqs_resend);
			tasklet_schedule(&resend_tasklet);
#endif
		}
	}
}
