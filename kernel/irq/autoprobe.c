/*
 * linux/kernel/irq/autoprobe.c
 *
 * Copyright (C) 1992, 1998-2004 Linus Torvalds, Ingo Molnar
 *
 * This file contains the interrupt probing code and driver APIs.
 */

#include <linux/irq.h>
#include <linux/module.h>
#include <linux/interrupt.h>

/*
 * Autodetection depends on the fact that any interrupt that
 * comes in on to an unassigned handler will get stuck with
 * "IRQ_WAITING" cleared and the interrupt disabled.
 */
static DECLARE_MUTEX(probe_sem);

/**
 *	probe_irq_on	- begin an interrupt autodetect
 *
 *	Commence probing for an interrupt. The interrupts are scanned
 *	and a mask of potential interrupt lines is returned.
 *
 */
unsigned long probe_irq_on(void)
{
	unsigned long val, delay;
	irq_desc_t *desc;
	unsigned int i;

	down(&probe_sem);
	/*
	 * something may have generated an irq long ago and we want to
	 * flush such a longstanding irq before considering it as spurious.
	 */
	for (i = NR_IRQS-1; i > 0; i--) {
		desc = irq_desc + i;

		spin_lock_irq(&desc->lock);
		if (!irq_desc[i].action)
			irq_desc[i].handler->startup(i);
		spin_unlock_irq(&desc->lock);
	}

	/* Wait for longstanding interrupts to trigger. */
	for (delay = jiffies + HZ/50; time_after(delay, jiffies); )
		/* about 20ms delay */ barrier();

	/*
	 * enable any unassigned irqs
	 * (we must startup again here because if a longstanding irq
	 * happened in the previous stage, it may have masked itself)
	 */
	for (i = NR_IRQS-1; i > 0; i--) {
		desc = irq_desc + i;

		spin_lock_irq(&desc->lock);
		if (!desc->action) {
			desc->status |= IRQ_AUTODETECT | IRQ_WAITING;
			if (desc->handler->startup(i))
				desc->status |= IRQ_PENDING;
		}
		spin_unlock_irq(&desc->lock);
	}

	/*
	 * Wait for spurious interrupts to trigger
	 */
	for (delay = jiffies + HZ/10; time_after(delay, jiffies); )
		/* about 100ms delay */ barrier();

	/*
	 * Now filter out any obviously spurious interrupts
	 */
	val = 0;
	for (i = 0; i < NR_IRQS; i++) {
		irq_desc_t *desc = irq_desc + i;
		unsigned int status;

		spin_lock_irq(&desc->lock);
		status = desc->status;

		if (status & IRQ_AUTODETECT) {
			/* It triggered already - consider it spurious. */
			if (!(status & IRQ_WAITING)) {
				desc->status = status & ~IRQ_AUTODETECT;
				desc->handler->shutdown(i);
			} else
				if (i < 32)
					val |= 1 << i;
		}
		spin_unlock_irq(&desc->lock);
	}

	return val;
}

EXPORT_SYMBOL(probe_irq_on);

/**
 *	probe_irq_mask - scan a bitmap of interrupt lines
 *	@val:	mask of interrupts to consider
 *
 *	Scan the interrupt lines and return a bitmap of active
 *	autodetect interrupts. The interrupt probe logic state
 *	is then returned to its previous value.
 *
 *	Note: we need to scan all the irq's even though we will
 *	only return autodetect irq numbers - just so that we reset
 *	them all to a known state.
 */
unsigned int probe_irq_mask(unsigned long val)
{
	unsigned int mask;
	int i;

	mask = 0;
	for (i = 0; i < NR_IRQS; i++) {
		irq_desc_t *desc = irq_desc + i;
		unsigned int status;

		spin_lock_irq(&desc->lock);
		status = desc->status;

		if (status & IRQ_AUTODETECT) {
			if (i < 16 && !(status & IRQ_WAITING))
				mask |= 1 << i;

			desc->status = status & ~IRQ_AUTODETECT;
			desc->handler->shutdown(i);
		}
		spin_unlock_irq(&desc->lock);
	}
	up(&probe_sem);

	return mask & val;
}
EXPORT_SYMBOL(probe_irq_mask);

/**
 *	probe_irq_off	- end an interrupt autodetect
 *	@val: mask of potential interrupts (unused)
 *
 *	Scans the unused interrupt lines and returns the line which
 *	appears to have triggered the interrupt. If no interrupt was
 *	found then zero is returned. If more than one interrupt is
 *	found then minus the first candidate is returned to indicate
 *	their is doubt.
 *
 *	The interrupt probe logic state is returned to its previous
 *	value.
 *
 *	BUGS: When used in a module (which arguably shouldn't happen)
 *	nothing prevents two IRQ probe callers from overlapping. The
 *	results of this are non-optimal.
 */
int probe_irq_off(unsigned long val)
{
	int i, irq_found = 0, nr_irqs = 0;

	for (i = 0; i < NR_IRQS; i++) {
		irq_desc_t *desc = irq_desc + i;
		unsigned int status;

		spin_lock_irq(&desc->lock);
		status = desc->status;

		if (status & IRQ_AUTODETECT) {
			if (!(status & IRQ_WAITING)) {
				if (!nr_irqs)
					irq_found = i;
				nr_irqs++;
			}
			desc->status = status & ~IRQ_AUTODETECT;
			desc->handler->shutdown(i);
		}
		spin_unlock_irq(&desc->lock);
	}
	up(&probe_sem);

	if (nr_irqs > 1)
		irq_found = -irq_found;
	return irq_found;
}

EXPORT_SYMBOL(probe_irq_off);

