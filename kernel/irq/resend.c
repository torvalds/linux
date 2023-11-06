// SPDX-License-Identifier: GPL-2.0
/*
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

/* hlist_head to handle software resend of interrupts: */
static HLIST_HEAD(irq_resend_list);
static DEFINE_RAW_SPINLOCK(irq_resend_lock);

/*
 * Run software resends of IRQ's
 */
static void resend_irqs(struct tasklet_struct *unused)
{
	struct irq_desc *desc;

	raw_spin_lock_irq(&irq_resend_lock);
	while (!hlist_empty(&irq_resend_list)) {
		desc = hlist_entry(irq_resend_list.first, struct irq_desc,
				   resend_node);
		hlist_del_init(&desc->resend_node);
		raw_spin_unlock(&irq_resend_lock);
		desc->handle_irq(desc);
		raw_spin_lock(&irq_resend_lock);
	}
	raw_spin_unlock_irq(&irq_resend_lock);
}

/* Tasklet to handle resend: */
static DECLARE_TASKLET(resend_tasklet, resend_irqs);

static int irq_sw_resend(struct irq_desc *desc)
{
	/*
	 * Validate whether this interrupt can be safely injected from
	 * non interrupt context
	 */
	if (handle_enforce_irqctx(&desc->irq_data))
		return -EINVAL;

	/*
	 * If the interrupt is running in the thread context of the parent
	 * irq we need to be careful, because we cannot trigger it
	 * directly.
	 */
	if (irq_settings_is_nested_thread(desc)) {
		/*
		 * If the parent_irq is valid, we retrigger the parent,
		 * otherwise we do nothing.
		 */
		if (!desc->parent_irq)
			return -EINVAL;

		desc = irq_to_desc(desc->parent_irq);
		if (!desc)
			return -EINVAL;
	}

	/* Add to resend_list and activate the softirq: */
	raw_spin_lock(&irq_resend_lock);
	if (hlist_unhashed(&desc->resend_node))
		hlist_add_head(&desc->resend_node, &irq_resend_list);
	raw_spin_unlock(&irq_resend_lock);
	tasklet_schedule(&resend_tasklet);
	return 0;
}

void clear_irq_resend(struct irq_desc *desc)
{
	raw_spin_lock(&irq_resend_lock);
	hlist_del_init(&desc->resend_node);
	raw_spin_unlock(&irq_resend_lock);
}

void irq_resend_init(struct irq_desc *desc)
{
	INIT_HLIST_NODE(&desc->resend_node);
}
#else
void clear_irq_resend(struct irq_desc *desc) {}
void irq_resend_init(struct irq_desc *desc) {}

static int irq_sw_resend(struct irq_desc *desc)
{
	return -EINVAL;
}
#endif

static int try_retrigger(struct irq_desc *desc)
{
	if (desc->irq_data.chip->irq_retrigger)
		return desc->irq_data.chip->irq_retrigger(&desc->irq_data);

#ifdef CONFIG_IRQ_DOMAIN_HIERARCHY
	return irq_chip_retrigger_hierarchy(&desc->irq_data);
#else
	return 0;
#endif
}

/*
 * IRQ resend
 *
 * Is called with interrupts disabled and desc->lock held.
 */
int check_irq_resend(struct irq_desc *desc, bool inject)
{
	int err = 0;

	/*
	 * We do not resend level type interrupts. Level type interrupts
	 * are resent by hardware when they are still active. Clear the
	 * pending bit so suspend/resume does not get confused.
	 */
	if (irq_settings_is_level(desc)) {
		desc->istate &= ~IRQS_PENDING;
		return -EINVAL;
	}

	if (desc->istate & IRQS_REPLAY)
		return -EBUSY;

	if (!(desc->istate & IRQS_PENDING) && !inject)
		return 0;

	desc->istate &= ~IRQS_PENDING;

	if (!try_retrigger(desc))
		err = irq_sw_resend(desc);

	/* If the retrigger was successful, mark it with the REPLAY bit */
	if (!err)
		desc->istate |= IRQS_REPLAY;
	return err;
}

#ifdef CONFIG_GENERIC_IRQ_INJECTION
/**
 * irq_inject_interrupt - Inject an interrupt for testing/error injection
 * @irq:	The interrupt number
 *
 * This function must only be used for debug and testing purposes!
 *
 * Especially on x86 this can cause a premature completion of an interrupt
 * affinity change causing the interrupt line to become stale. Very
 * unlikely, but possible.
 *
 * The injection can fail for various reasons:
 * - Interrupt is not activated
 * - Interrupt is NMI type or currently replaying
 * - Interrupt is level type
 * - Interrupt does not support hardware retrigger and software resend is
 *   either not enabled or not possible for the interrupt.
 */
int irq_inject_interrupt(unsigned int irq)
{
	struct irq_desc *desc;
	unsigned long flags;
	int err;

	/* Try the state injection hardware interface first */
	if (!irq_set_irqchip_state(irq, IRQCHIP_STATE_PENDING, true))
		return 0;

	/* That failed, try via the resend mechanism */
	desc = irq_get_desc_buslock(irq, &flags, 0);
	if (!desc)
		return -EINVAL;

	/*
	 * Only try to inject when the interrupt is:
	 *  - not NMI type
	 *  - activated
	 */
	if ((desc->istate & IRQS_NMI) || !irqd_is_activated(&desc->irq_data))
		err = -EINVAL;
	else
		err = check_irq_resend(desc, true);

	irq_put_desc_busunlock(desc, flags);
	return err;
}
EXPORT_SYMBOL_GPL(irq_inject_interrupt);
#endif
