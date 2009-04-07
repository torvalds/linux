/*
 * linux/kernel/irq/pm.c
 *
 * Copyright (C) 2009 Rafael J. Wysocki <rjw@sisk.pl>, Novell Inc.
 *
 * This file contains power management functions related to interrupts.
 */

#include <linux/irq.h>
#include <linux/module.h>
#include <linux/interrupt.h>

#include "internals.h"

/**
 * suspend_device_irqs - disable all currently enabled interrupt lines
 *
 * During system-wide suspend or hibernation device interrupts need to be
 * disabled at the chip level and this function is provided for this purpose.
 * It disables all interrupt lines that are enabled at the moment and sets the
 * IRQ_SUSPENDED flag for them.
 */
void suspend_device_irqs(void)
{
	struct irq_desc *desc;
	int irq;

	for_each_irq_desc(irq, desc) {
		unsigned long flags;

		spin_lock_irqsave(&desc->lock, flags);
		__disable_irq(desc, irq, true);
		spin_unlock_irqrestore(&desc->lock, flags);
	}

	for_each_irq_desc(irq, desc)
		if (desc->status & IRQ_SUSPENDED)
			synchronize_irq(irq);
}
EXPORT_SYMBOL_GPL(suspend_device_irqs);

/**
 * resume_device_irqs - enable interrupt lines disabled by suspend_device_irqs()
 *
 * Enable all interrupt lines previously disabled by suspend_device_irqs() that
 * have the IRQ_SUSPENDED flag set.
 */
void resume_device_irqs(void)
{
	struct irq_desc *desc;
	int irq;

	for_each_irq_desc(irq, desc) {
		unsigned long flags;

		if (!(desc->status & IRQ_SUSPENDED))
			continue;

		spin_lock_irqsave(&desc->lock, flags);
		__enable_irq(desc, irq, true);
		spin_unlock_irqrestore(&desc->lock, flags);
	}
}
EXPORT_SYMBOL_GPL(resume_device_irqs);

/**
 * check_wakeup_irqs - check if any wake-up interrupts are pending
 */
int check_wakeup_irqs(void)
{
	struct irq_desc *desc;
	int irq;

	for_each_irq_desc(irq, desc)
		if ((desc->status & IRQ_WAKEUP) && (desc->status & IRQ_PENDING))
			return -EBUSY;

	return 0;
}
