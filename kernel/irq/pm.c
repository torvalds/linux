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
#include <linux/suspend.h>
#include <linux/syscore_ops.h>

#include "internals.h"

bool irq_pm_check_wakeup(struct irq_desc *desc)
{
	if (irqd_is_wakeup_armed(&desc->irq_data)) {
		irqd_clear(&desc->irq_data, IRQD_WAKEUP_ARMED);
		desc->istate |= IRQS_SUSPENDED | IRQS_PENDING;
		desc->depth++;
		irq_disable(desc);
		pm_system_wakeup();
		return true;
	}
	return false;
}

/*
 * Called from __setup_irq() with desc->lock held after @action has
 * been installed in the action chain.
 */
void irq_pm_install_action(struct irq_desc *desc, struct irqaction *action)
{
	desc->nr_actions++;

	if (action->flags & IRQF_FORCE_RESUME)
		desc->force_resume_depth++;

	WARN_ON_ONCE(desc->force_resume_depth &&
		     desc->force_resume_depth != desc->nr_actions);

	if (action->flags & IRQF_NO_SUSPEND)
		desc->no_suspend_depth++;
	else if (action->flags & IRQF_COND_SUSPEND)
		desc->cond_suspend_depth++;

	WARN_ON_ONCE(desc->no_suspend_depth &&
		     (desc->no_suspend_depth +
			desc->cond_suspend_depth) != desc->nr_actions);
}

/*
 * Called from __free_irq() with desc->lock held after @action has
 * been removed from the action chain.
 */
void irq_pm_remove_action(struct irq_desc *desc, struct irqaction *action)
{
	desc->nr_actions--;

	if (action->flags & IRQF_FORCE_RESUME)
		desc->force_resume_depth--;

	if (action->flags & IRQF_NO_SUSPEND)
		desc->no_suspend_depth--;
	else if (action->flags & IRQF_COND_SUSPEND)
		desc->cond_suspend_depth--;
}

static bool suspend_device_irq(struct irq_desc *desc, int irq)
{
	if (!desc->action || desc->no_suspend_depth)
		return false;

	if (irqd_is_wakeup_set(&desc->irq_data)) {
		irqd_set(&desc->irq_data, IRQD_WAKEUP_ARMED);
		/*
		 * We return true here to force the caller to issue
		 * synchronize_irq(). We need to make sure that the
		 * IRQD_WAKEUP_ARMED is visible before we return from
		 * suspend_device_irqs().
		 */
		return true;
	}

	desc->istate |= IRQS_SUSPENDED;
	__disable_irq(desc, irq);

	/*
	 * Hardware which has no wakeup source configuration facility
	 * requires that the non wakeup interrupts are masked at the
	 * chip level. The chip implementation indicates that with
	 * IRQCHIP_MASK_ON_SUSPEND.
	 */
	if (irq_desc_get_chip(desc)->flags & IRQCHIP_MASK_ON_SUSPEND)
		mask_irq(desc);
	return true;
}

/**
 * suspend_device_irqs - disable all currently enabled interrupt lines
 *
 * During system-wide suspend or hibernation device drivers need to be
 * prevented from receiving interrupts and this function is provided
 * for this purpose.
 *
 * So we disable all interrupts and mark them IRQS_SUSPENDED except
 * for those which are unused, those which are marked as not
 * suspendable via an interrupt request with the flag IRQF_NO_SUSPEND
 * set and those which are marked as active wakeup sources.
 *
 * The active wakeup sources are handled by the flow handler entry
 * code which checks for the IRQD_WAKEUP_ARMED flag, suspends the
 * interrupt and notifies the pm core about the wakeup.
 */
void suspend_device_irqs(void)
{
	struct irq_desc *desc;
	int irq;

	for_each_irq_desc(irq, desc) {
		unsigned long flags;
		bool sync;

		if (irq_settings_is_nested_thread(desc))
			continue;
		raw_spin_lock_irqsave(&desc->lock, flags);
		sync = suspend_device_irq(desc, irq);
		raw_spin_unlock_irqrestore(&desc->lock, flags);

		if (sync)
			synchronize_irq(irq);
	}
}
EXPORT_SYMBOL_GPL(suspend_device_irqs);

static void resume_irq(struct irq_desc *desc, int irq)
{
	irqd_clear(&desc->irq_data, IRQD_WAKEUP_ARMED);

	if (desc->istate & IRQS_SUSPENDED)
		goto resume;

	/* Force resume the interrupt? */
	if (!desc->force_resume_depth)
		return;

	/* Pretend that it got disabled ! */
	desc->depth++;
resume:
	desc->istate &= ~IRQS_SUSPENDED;
	__enable_irq(desc, irq);
}

static void resume_irqs(bool want_early)
{
	struct irq_desc *desc;
	int irq;

	for_each_irq_desc(irq, desc) {
		unsigned long flags;
		bool is_early = desc->action &&
			desc->action->flags & IRQF_EARLY_RESUME;

		if (!is_early && want_early)
			continue;
		if (irq_settings_is_nested_thread(desc))
			continue;

		raw_spin_lock_irqsave(&desc->lock, flags);
		resume_irq(desc, irq);
		raw_spin_unlock_irqrestore(&desc->lock, flags);
	}
}

/**
 * irq_pm_syscore_ops - enable interrupt lines early
 *
 * Enable all interrupt lines with %IRQF_EARLY_RESUME set.
 */
static void irq_pm_syscore_resume(void)
{
	resume_irqs(true);
}

static struct syscore_ops irq_pm_syscore_ops = {
	.resume		= irq_pm_syscore_resume,
};

static int __init irq_pm_init_ops(void)
{
	register_syscore_ops(&irq_pm_syscore_ops);
	return 0;
}

device_initcall(irq_pm_init_ops);

/**
 * resume_device_irqs - enable interrupt lines disabled by suspend_device_irqs()
 *
 * Enable all non-%IRQF_EARLY_RESUME interrupt lines previously
 * disabled by suspend_device_irqs() that have the IRQS_SUSPENDED flag
 * set as well as those with %IRQF_FORCE_RESUME.
 */
void resume_device_irqs(void)
{
	resume_irqs(false);
}
EXPORT_SYMBOL_GPL(resume_device_irqs);
