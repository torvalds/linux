// SPDX-License-Identifier: GPL-2.0
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
#include <linux/delay.h>
#include <linux/async.h>

#include "internals.h"

/*
 * Autodetection depends on the fact that any interrupt that
 * comes in on to an unassigned handler will get stuck with
 * "IRQS_WAITING" cleared and the interrupt disabled.
 */
static DEFINE_MUTEX(probing_active);

/**
 *	probe_irq_on	- begin an interrupt autodetect
 *
 *	Commence probing for an interrupt. The interrupts are scanned
 *	and a mask of potential interrupt lines is returned.
 *
 */
unsigned long probe_irq_on(void)
{
	struct irq_desc *desc;
	unsigned long mask = 0;
	int i;

	/*
	 * quiesce the kernel, or at least the asynchronous portion
	 */
	async_synchronize_full();
	mutex_lock(&probing_active);
	/*
	 * something may have generated an irq long ago and we want to
	 * flush such a longstanding irq before considering it as spurious.
	 */
	for_each_irq_desc_reverse(i, desc) {
		raw_spin_lock_irq(&desc->lock);
		if (!desc->action && irq_settings_can_probe(desc)) {
			/*
			 * Some chips need to know about probing in
			 * progress:
			 */
			if (desc->irq_data.chip->irq_set_type)
				desc->irq_data.chip->irq_set_type(&desc->irq_data,
							 IRQ_TYPE_PROBE);
			irq_activate_and_startup(desc, IRQ_NORESEND);
		}
		raw_spin_unlock_irq(&desc->lock);
	}

	/* Wait for longstanding interrupts to trigger. */
	msleep(20);

	/*
	 * enable any unassigned irqs
	 * (we must startup again here because if a longstanding irq
	 * happened in the previous stage, it may have masked itself)
	 */
	for_each_irq_desc_reverse(i, desc) {
		raw_spin_lock_irq(&desc->lock);
		if (!desc->action && irq_settings_can_probe(desc)) {
			desc->istate |= IRQS_AUTODETECT | IRQS_WAITING;
			if (irq_activate_and_startup(desc, IRQ_NORESEND))
				desc->istate |= IRQS_PENDING;
		}
		raw_spin_unlock_irq(&desc->lock);
	}

	/*
	 * Wait for spurious interrupts to trigger
	 */
	msleep(100);

	/*
	 * Now filter out any obviously spurious interrupts
	 */
	for_each_irq_desc(i, desc) {
		raw_spin_lock_irq(&desc->lock);

		if (desc->istate & IRQS_AUTODETECT) {
			/* It triggered already - consider it spurious. */
			if (!(desc->istate & IRQS_WAITING)) {
				desc->istate &= ~IRQS_AUTODETECT;
				irq_shutdown(desc);
			} else
				if (i < 32)
					mask |= 1 << i;
		}
		raw_spin_unlock_irq(&desc->lock);
	}

	return mask;
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
	unsigned int mask = 0;
	struct irq_desc *desc;
	int i;

	for_each_irq_desc(i, desc) {
		raw_spin_lock_irq(&desc->lock);
		if (desc->istate & IRQS_AUTODETECT) {
			if (i < 16 && !(desc->istate & IRQS_WAITING))
				mask |= 1 << i;

			desc->istate &= ~IRQS_AUTODETECT;
			irq_shutdown(desc);
		}
		raw_spin_unlock_irq(&desc->lock);
	}
	mutex_unlock(&probing_active);

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
	int i, irq_found = 0, nr_of_irqs = 0;
	struct irq_desc *desc;

	for_each_irq_desc(i, desc) {
		raw_spin_lock_irq(&desc->lock);

		if (desc->istate & IRQS_AUTODETECT) {
			if (!(desc->istate & IRQS_WAITING)) {
				if (!nr_of_irqs)
					irq_found = i;
				nr_of_irqs++;
			}
			desc->istate &= ~IRQS_AUTODETECT;
			irq_shutdown(desc);
		}
		raw_spin_unlock_irq(&desc->lock);
	}
	mutex_unlock(&probing_active);

	if (nr_of_irqs > 1)
		irq_found = -irq_found;

	return irq_found;
}
EXPORT_SYMBOL(probe_irq_off);

