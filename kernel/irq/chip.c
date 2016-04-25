/*
 * linux/kernel/irq/chip.c
 *
 * Copyright (C) 1992, 1998-2006 Linus Torvalds, Ingo Molnar
 * Copyright (C) 2005-2006, Thomas Gleixner, Russell King
 *
 * This file contains the core interrupt handling code, for irq-chip
 * based architectures.
 *
 * Detailed information is available in Documentation/DocBook/genericirq
 */

#include <linux/irq.h>
#include <linux/msi.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>
#include <linux/irqdomain.h>

#include <trace/events/irq.h>

#include "internals.h"

static irqreturn_t bad_chained_irq(int irq, void *dev_id)
{
	WARN_ONCE(1, "Chained irq %d should not call an action\n", irq);
	return IRQ_NONE;
}

/*
 * Chained handlers should never call action on their IRQ. This default
 * action will emit warning if such thing happens.
 */
struct irqaction chained_action = {
	.handler = bad_chained_irq,
};

/**
 *	irq_set_chip - set the irq chip for an irq
 *	@irq:	irq number
 *	@chip:	pointer to irq chip description structure
 */
int irq_set_chip(unsigned int irq, struct irq_chip *chip)
{
	unsigned long flags;
	struct irq_desc *desc = irq_get_desc_lock(irq, &flags, 0);

	if (!desc)
		return -EINVAL;

	if (!chip)
		chip = &no_irq_chip;

	desc->irq_data.chip = chip;
	irq_put_desc_unlock(desc, flags);
	/*
	 * For !CONFIG_SPARSE_IRQ make the irq show up in
	 * allocated_irqs.
	 */
	irq_mark_irq(irq);
	return 0;
}
EXPORT_SYMBOL(irq_set_chip);

/**
 *	irq_set_type - set the irq trigger type for an irq
 *	@irq:	irq number
 *	@type:	IRQ_TYPE_{LEVEL,EDGE}_* value - see include/linux/irq.h
 */
int irq_set_irq_type(unsigned int irq, unsigned int type)
{
	unsigned long flags;
	struct irq_desc *desc = irq_get_desc_buslock(irq, &flags, IRQ_GET_DESC_CHECK_GLOBAL);
	int ret = 0;

	if (!desc)
		return -EINVAL;

	type &= IRQ_TYPE_SENSE_MASK;
	ret = __irq_set_trigger(desc, type);
	irq_put_desc_busunlock(desc, flags);
	return ret;
}
EXPORT_SYMBOL(irq_set_irq_type);

/**
 *	irq_set_handler_data - set irq handler data for an irq
 *	@irq:	Interrupt number
 *	@data:	Pointer to interrupt specific data
 *
 *	Set the hardware irq controller data for an irq
 */
int irq_set_handler_data(unsigned int irq, void *data)
{
	unsigned long flags;
	struct irq_desc *desc = irq_get_desc_lock(irq, &flags, 0);

	if (!desc)
		return -EINVAL;
	desc->irq_common_data.handler_data = data;
	irq_put_desc_unlock(desc, flags);
	return 0;
}
EXPORT_SYMBOL(irq_set_handler_data);

/**
 *	irq_set_msi_desc_off - set MSI descriptor data for an irq at offset
 *	@irq_base:	Interrupt number base
 *	@irq_offset:	Interrupt number offset
 *	@entry:		Pointer to MSI descriptor data
 *
 *	Set the MSI descriptor entry for an irq at offset
 */
int irq_set_msi_desc_off(unsigned int irq_base, unsigned int irq_offset,
			 struct msi_desc *entry)
{
	unsigned long flags;
	struct irq_desc *desc = irq_get_desc_lock(irq_base + irq_offset, &flags, IRQ_GET_DESC_CHECK_GLOBAL);

	if (!desc)
		return -EINVAL;
	desc->irq_common_data.msi_desc = entry;
	if (entry && !irq_offset)
		entry->irq = irq_base;
	irq_put_desc_unlock(desc, flags);
	return 0;
}

/**
 *	irq_set_msi_desc - set MSI descriptor data for an irq
 *	@irq:	Interrupt number
 *	@entry:	Pointer to MSI descriptor data
 *
 *	Set the MSI descriptor entry for an irq
 */
int irq_set_msi_desc(unsigned int irq, struct msi_desc *entry)
{
	return irq_set_msi_desc_off(irq, 0, entry);
}

/**
 *	irq_set_chip_data - set irq chip data for an irq
 *	@irq:	Interrupt number
 *	@data:	Pointer to chip specific data
 *
 *	Set the hardware irq chip data for an irq
 */
int irq_set_chip_data(unsigned int irq, void *data)
{
	unsigned long flags;
	struct irq_desc *desc = irq_get_desc_lock(irq, &flags, 0);

	if (!desc)
		return -EINVAL;
	desc->irq_data.chip_data = data;
	irq_put_desc_unlock(desc, flags);
	return 0;
}
EXPORT_SYMBOL(irq_set_chip_data);

struct irq_data *irq_get_irq_data(unsigned int irq)
{
	struct irq_desc *desc = irq_to_desc(irq);

	return desc ? &desc->irq_data : NULL;
}
EXPORT_SYMBOL_GPL(irq_get_irq_data);

static void irq_state_clr_disabled(struct irq_desc *desc)
{
	irqd_clear(&desc->irq_data, IRQD_IRQ_DISABLED);
}

static void irq_state_set_disabled(struct irq_desc *desc)
{
	irqd_set(&desc->irq_data, IRQD_IRQ_DISABLED);
}

static void irq_state_clr_masked(struct irq_desc *desc)
{
	irqd_clear(&desc->irq_data, IRQD_IRQ_MASKED);
}

static void irq_state_set_masked(struct irq_desc *desc)
{
	irqd_set(&desc->irq_data, IRQD_IRQ_MASKED);
}

int irq_startup(struct irq_desc *desc, bool resend)
{
	int ret = 0;

	irq_state_clr_disabled(desc);
	desc->depth = 0;

	irq_domain_activate_irq(&desc->irq_data);
	if (desc->irq_data.chip->irq_startup) {
		ret = desc->irq_data.chip->irq_startup(&desc->irq_data);
		irq_state_clr_masked(desc);
	} else {
		irq_enable(desc);
	}
	if (resend)
		check_irq_resend(desc);
	return ret;
}

void irq_shutdown(struct irq_desc *desc)
{
	irq_state_set_disabled(desc);
	desc->depth = 1;
	if (desc->irq_data.chip->irq_shutdown)
		desc->irq_data.chip->irq_shutdown(&desc->irq_data);
	else if (desc->irq_data.chip->irq_disable)
		desc->irq_data.chip->irq_disable(&desc->irq_data);
	else
		desc->irq_data.chip->irq_mask(&desc->irq_data);
	irq_domain_deactivate_irq(&desc->irq_data);
	irq_state_set_masked(desc);
}

void irq_enable(struct irq_desc *desc)
{
	irq_state_clr_disabled(desc);
	if (desc->irq_data.chip->irq_enable)
		desc->irq_data.chip->irq_enable(&desc->irq_data);
	else
		desc->irq_data.chip->irq_unmask(&desc->irq_data);
	irq_state_clr_masked(desc);
}

/**
 * irq_disable - Mark interrupt disabled
 * @desc:	irq descriptor which should be disabled
 *
 * If the chip does not implement the irq_disable callback, we
 * use a lazy disable approach. That means we mark the interrupt
 * disabled, but leave the hardware unmasked. That's an
 * optimization because we avoid the hardware access for the
 * common case where no interrupt happens after we marked it
 * disabled. If an interrupt happens, then the interrupt flow
 * handler masks the line at the hardware level and marks it
 * pending.
 *
 * If the interrupt chip does not implement the irq_disable callback,
 * a driver can disable the lazy approach for a particular irq line by
 * calling 'irq_set_status_flags(irq, IRQ_DISABLE_UNLAZY)'. This can
 * be used for devices which cannot disable the interrupt at the
 * device level under certain circumstances and have to use
 * disable_irq[_nosync] instead.
 */
void irq_disable(struct irq_desc *desc)
{
	irq_state_set_disabled(desc);
	if (desc->irq_data.chip->irq_disable) {
		desc->irq_data.chip->irq_disable(&desc->irq_data);
		irq_state_set_masked(desc);
	} else if (irq_settings_disable_unlazy(desc)) {
		mask_irq(desc);
	}
}

void irq_percpu_enable(struct irq_desc *desc, unsigned int cpu)
{
	if (desc->irq_data.chip->irq_enable)
		desc->irq_data.chip->irq_enable(&desc->irq_data);
	else
		desc->irq_data.chip->irq_unmask(&desc->irq_data);
	cpumask_set_cpu(cpu, desc->percpu_enabled);
}

void irq_percpu_disable(struct irq_desc *desc, unsigned int cpu)
{
	if (desc->irq_data.chip->irq_disable)
		desc->irq_data.chip->irq_disable(&desc->irq_data);
	else
		desc->irq_data.chip->irq_mask(&desc->irq_data);
	cpumask_clear_cpu(cpu, desc->percpu_enabled);
}

static inline void mask_ack_irq(struct irq_desc *desc)
{
	if (desc->irq_data.chip->irq_mask_ack)
		desc->irq_data.chip->irq_mask_ack(&desc->irq_data);
	else {
		desc->irq_data.chip->irq_mask(&desc->irq_data);
		if (desc->irq_data.chip->irq_ack)
			desc->irq_data.chip->irq_ack(&desc->irq_data);
	}
	irq_state_set_masked(desc);
}

void mask_irq(struct irq_desc *desc)
{
	if (desc->irq_data.chip->irq_mask) {
		desc->irq_data.chip->irq_mask(&desc->irq_data);
		irq_state_set_masked(desc);
	}
}

void unmask_irq(struct irq_desc *desc)
{
	if (desc->irq_data.chip->irq_unmask) {
		desc->irq_data.chip->irq_unmask(&desc->irq_data);
		irq_state_clr_masked(desc);
	}
}

void unmask_threaded_irq(struct irq_desc *desc)
{
	struct irq_chip *chip = desc->irq_data.chip;

	if (chip->flags & IRQCHIP_EOI_THREADED)
		chip->irq_eoi(&desc->irq_data);

	if (chip->irq_unmask) {
		chip->irq_unmask(&desc->irq_data);
		irq_state_clr_masked(desc);
	}
}

/*
 *	handle_nested_irq - Handle a nested irq from a irq thread
 *	@irq:	the interrupt number
 *
 *	Handle interrupts which are nested into a threaded interrupt
 *	handler. The handler function is called inside the calling
 *	threads context.
 */
void handle_nested_irq(unsigned int irq)
{
	struct irq_desc *desc = irq_to_desc(irq);
	struct irqaction *action;
	irqreturn_t action_ret;

	might_sleep();

	raw_spin_lock_irq(&desc->lock);

	desc->istate &= ~(IRQS_REPLAY | IRQS_WAITING);

	action = desc->action;
	if (unlikely(!action || irqd_irq_disabled(&desc->irq_data))) {
		desc->istate |= IRQS_PENDING;
		goto out_unlock;
	}

	kstat_incr_irqs_this_cpu(desc);
	irqd_set(&desc->irq_data, IRQD_IRQ_INPROGRESS);
	raw_spin_unlock_irq(&desc->lock);

	action_ret = action->thread_fn(action->irq, action->dev_id);
	if (!noirqdebug)
		note_interrupt(desc, action_ret);

	raw_spin_lock_irq(&desc->lock);
	irqd_clear(&desc->irq_data, IRQD_IRQ_INPROGRESS);

out_unlock:
	raw_spin_unlock_irq(&desc->lock);
}
EXPORT_SYMBOL_GPL(handle_nested_irq);

static bool irq_check_poll(struct irq_desc *desc)
{
	if (!(desc->istate & IRQS_POLL_INPROGRESS))
		return false;
	return irq_wait_for_poll(desc);
}

static bool irq_may_run(struct irq_desc *desc)
{
	unsigned int mask = IRQD_IRQ_INPROGRESS | IRQD_WAKEUP_ARMED;

	/*
	 * If the interrupt is not in progress and is not an armed
	 * wakeup interrupt, proceed.
	 */
	if (!irqd_has_set(&desc->irq_data, mask))
		return true;

	/*
	 * If the interrupt is an armed wakeup source, mark it pending
	 * and suspended, disable it and notify the pm core about the
	 * event.
	 */
	if (irq_pm_check_wakeup(desc))
		return false;

	/*
	 * Handle a potential concurrent poll on a different core.
	 */
	return irq_check_poll(desc);
}

/**
 *	handle_simple_irq - Simple and software-decoded IRQs.
 *	@desc:	the interrupt description structure for this irq
 *
 *	Simple interrupts are either sent from a demultiplexing interrupt
 *	handler or come from hardware, where no interrupt hardware control
 *	is necessary.
 *
 *	Note: The caller is expected to handle the ack, clear, mask and
 *	unmask issues if necessary.
 */
void handle_simple_irq(struct irq_desc *desc)
{
	raw_spin_lock(&desc->lock);

	if (!irq_may_run(desc))
		goto out_unlock;

	desc->istate &= ~(IRQS_REPLAY | IRQS_WAITING);

	if (unlikely(!desc->action || irqd_irq_disabled(&desc->irq_data))) {
		desc->istate |= IRQS_PENDING;
		goto out_unlock;
	}

	kstat_incr_irqs_this_cpu(desc);
	handle_irq_event(desc);

out_unlock:
	raw_spin_unlock(&desc->lock);
}
EXPORT_SYMBOL_GPL(handle_simple_irq);

/*
 * Called unconditionally from handle_level_irq() and only for oneshot
 * interrupts from handle_fasteoi_irq()
 */
static void cond_unmask_irq(struct irq_desc *desc)
{
	/*
	 * We need to unmask in the following cases:
	 * - Standard level irq (IRQF_ONESHOT is not set)
	 * - Oneshot irq which did not wake the thread (caused by a
	 *   spurious interrupt or a primary handler handling it
	 *   completely).
	 */
	if (!irqd_irq_disabled(&desc->irq_data) &&
	    irqd_irq_masked(&desc->irq_data) && !desc->threads_oneshot)
		unmask_irq(desc);
}

/**
 *	handle_level_irq - Level type irq handler
 *	@desc:	the interrupt description structure for this irq
 *
 *	Level type interrupts are active as long as the hardware line has
 *	the active level. This may require to mask the interrupt and unmask
 *	it after the associated handler has acknowledged the device, so the
 *	interrupt line is back to inactive.
 */
void handle_level_irq(struct irq_desc *desc)
{
	raw_spin_lock(&desc->lock);
	mask_ack_irq(desc);

	if (!irq_may_run(desc))
		goto out_unlock;

	desc->istate &= ~(IRQS_REPLAY | IRQS_WAITING);

	/*
	 * If its disabled or no action available
	 * keep it masked and get out of here
	 */
	if (unlikely(!desc->action || irqd_irq_disabled(&desc->irq_data))) {
		desc->istate |= IRQS_PENDING;
		goto out_unlock;
	}

	kstat_incr_irqs_this_cpu(desc);
	handle_irq_event(desc);

	cond_unmask_irq(desc);

out_unlock:
	raw_spin_unlock(&desc->lock);
}
EXPORT_SYMBOL_GPL(handle_level_irq);

#ifdef CONFIG_IRQ_PREFLOW_FASTEOI
static inline void preflow_handler(struct irq_desc *desc)
{
	if (desc->preflow_handler)
		desc->preflow_handler(&desc->irq_data);
}
#else
static inline void preflow_handler(struct irq_desc *desc) { }
#endif

static void cond_unmask_eoi_irq(struct irq_desc *desc, struct irq_chip *chip)
{
	if (!(desc->istate & IRQS_ONESHOT)) {
		chip->irq_eoi(&desc->irq_data);
		return;
	}
	/*
	 * We need to unmask in the following cases:
	 * - Oneshot irq which did not wake the thread (caused by a
	 *   spurious interrupt or a primary handler handling it
	 *   completely).
	 */
	if (!irqd_irq_disabled(&desc->irq_data) &&
	    irqd_irq_masked(&desc->irq_data) && !desc->threads_oneshot) {
		chip->irq_eoi(&desc->irq_data);
		unmask_irq(desc);
	} else if (!(chip->flags & IRQCHIP_EOI_THREADED)) {
		chip->irq_eoi(&desc->irq_data);
	}
}

/**
 *	handle_fasteoi_irq - irq handler for transparent controllers
 *	@desc:	the interrupt description structure for this irq
 *
 *	Only a single callback will be issued to the chip: an ->eoi()
 *	call when the interrupt has been serviced. This enables support
 *	for modern forms of interrupt handlers, which handle the flow
 *	details in hardware, transparently.
 */
void handle_fasteoi_irq(struct irq_desc *desc)
{
	struct irq_chip *chip = desc->irq_data.chip;

	raw_spin_lock(&desc->lock);

	if (!irq_may_run(desc))
		goto out;

	desc->istate &= ~(IRQS_REPLAY | IRQS_WAITING);

	/*
	 * If its disabled or no action available
	 * then mask it and get out of here:
	 */
	if (unlikely(!desc->action || irqd_irq_disabled(&desc->irq_data))) {
		desc->istate |= IRQS_PENDING;
		mask_irq(desc);
		goto out;
	}

	kstat_incr_irqs_this_cpu(desc);
	if (desc->istate & IRQS_ONESHOT)
		mask_irq(desc);

	preflow_handler(desc);
	handle_irq_event(desc);

	cond_unmask_eoi_irq(desc, chip);

	raw_spin_unlock(&desc->lock);
	return;
out:
	if (!(chip->flags & IRQCHIP_EOI_IF_HANDLED))
		chip->irq_eoi(&desc->irq_data);
	raw_spin_unlock(&desc->lock);
}
EXPORT_SYMBOL_GPL(handle_fasteoi_irq);

/**
 *	handle_edge_irq - edge type IRQ handler
 *	@desc:	the interrupt description structure for this irq
 *
 *	Interrupt occures on the falling and/or rising edge of a hardware
 *	signal. The occurrence is latched into the irq controller hardware
 *	and must be acked in order to be reenabled. After the ack another
 *	interrupt can happen on the same source even before the first one
 *	is handled by the associated event handler. If this happens it
 *	might be necessary to disable (mask) the interrupt depending on the
 *	controller hardware. This requires to reenable the interrupt inside
 *	of the loop which handles the interrupts which have arrived while
 *	the handler was running. If all pending interrupts are handled, the
 *	loop is left.
 */
void handle_edge_irq(struct irq_desc *desc)
{
	raw_spin_lock(&desc->lock);

	desc->istate &= ~(IRQS_REPLAY | IRQS_WAITING);

	if (!irq_may_run(desc)) {
		desc->istate |= IRQS_PENDING;
		mask_ack_irq(desc);
		goto out_unlock;
	}

	/*
	 * If its disabled or no action available then mask it and get
	 * out of here.
	 */
	if (irqd_irq_disabled(&desc->irq_data) || !desc->action) {
		desc->istate |= IRQS_PENDING;
		mask_ack_irq(desc);
		goto out_unlock;
	}

	kstat_incr_irqs_this_cpu(desc);

	/* Start handling the irq */
	desc->irq_data.chip->irq_ack(&desc->irq_data);

	do {
		if (unlikely(!desc->action)) {
			mask_irq(desc);
			goto out_unlock;
		}

		/*
		 * When another irq arrived while we were handling
		 * one, we could have masked the irq.
		 * Renable it, if it was not disabled in meantime.
		 */
		if (unlikely(desc->istate & IRQS_PENDING)) {
			if (!irqd_irq_disabled(&desc->irq_data) &&
			    irqd_irq_masked(&desc->irq_data))
				unmask_irq(desc);
		}

		handle_irq_event(desc);

	} while ((desc->istate & IRQS_PENDING) &&
		 !irqd_irq_disabled(&desc->irq_data));

out_unlock:
	raw_spin_unlock(&desc->lock);
}
EXPORT_SYMBOL(handle_edge_irq);

#ifdef CONFIG_IRQ_EDGE_EOI_HANDLER
/**
 *	handle_edge_eoi_irq - edge eoi type IRQ handler
 *	@desc:	the interrupt description structure for this irq
 *
 * Similar as the above handle_edge_irq, but using eoi and w/o the
 * mask/unmask logic.
 */
void handle_edge_eoi_irq(struct irq_desc *desc)
{
	struct irq_chip *chip = irq_desc_get_chip(desc);

	raw_spin_lock(&desc->lock);

	desc->istate &= ~(IRQS_REPLAY | IRQS_WAITING);

	if (!irq_may_run(desc)) {
		desc->istate |= IRQS_PENDING;
		goto out_eoi;
	}

	/*
	 * If its disabled or no action available then mask it and get
	 * out of here.
	 */
	if (irqd_irq_disabled(&desc->irq_data) || !desc->action) {
		desc->istate |= IRQS_PENDING;
		goto out_eoi;
	}

	kstat_incr_irqs_this_cpu(desc);

	do {
		if (unlikely(!desc->action))
			goto out_eoi;

		handle_irq_event(desc);

	} while ((desc->istate & IRQS_PENDING) &&
		 !irqd_irq_disabled(&desc->irq_data));

out_eoi:
	chip->irq_eoi(&desc->irq_data);
	raw_spin_unlock(&desc->lock);
}
#endif

/**
 *	handle_percpu_irq - Per CPU local irq handler
 *	@desc:	the interrupt description structure for this irq
 *
 *	Per CPU interrupts on SMP machines without locking requirements
 */
void handle_percpu_irq(struct irq_desc *desc)
{
	struct irq_chip *chip = irq_desc_get_chip(desc);

	kstat_incr_irqs_this_cpu(desc);

	if (chip->irq_ack)
		chip->irq_ack(&desc->irq_data);

	handle_irq_event_percpu(desc);

	if (chip->irq_eoi)
		chip->irq_eoi(&desc->irq_data);
}

/**
 * handle_percpu_devid_irq - Per CPU local irq handler with per cpu dev ids
 * @desc:	the interrupt description structure for this irq
 *
 * Per CPU interrupts on SMP machines without locking requirements. Same as
 * handle_percpu_irq() above but with the following extras:
 *
 * action->percpu_dev_id is a pointer to percpu variables which
 * contain the real device id for the cpu on which this handler is
 * called
 */
void handle_percpu_devid_irq(struct irq_desc *desc)
{
	struct irq_chip *chip = irq_desc_get_chip(desc);
	struct irqaction *action = desc->action;
	void *dev_id = raw_cpu_ptr(action->percpu_dev_id);
	unsigned int irq = irq_desc_get_irq(desc);
	irqreturn_t res;

	kstat_incr_irqs_this_cpu(desc);

	if (chip->irq_ack)
		chip->irq_ack(&desc->irq_data);

	trace_irq_handler_entry(irq, action);
	res = action->handler(irq, dev_id);
	trace_irq_handler_exit(irq, action, res);

	if (chip->irq_eoi)
		chip->irq_eoi(&desc->irq_data);
}

void
__irq_do_set_handler(struct irq_desc *desc, irq_flow_handler_t handle,
		     int is_chained, const char *name)
{
	if (!handle) {
		handle = handle_bad_irq;
	} else {
		struct irq_data *irq_data = &desc->irq_data;
#ifdef CONFIG_IRQ_DOMAIN_HIERARCHY
		/*
		 * With hierarchical domains we might run into a
		 * situation where the outermost chip is not yet set
		 * up, but the inner chips are there.  Instead of
		 * bailing we install the handler, but obviously we
		 * cannot enable/startup the interrupt at this point.
		 */
		while (irq_data) {
			if (irq_data->chip != &no_irq_chip)
				break;
			/*
			 * Bail out if the outer chip is not set up
			 * and the interrrupt supposed to be started
			 * right away.
			 */
			if (WARN_ON(is_chained))
				return;
			/* Try the parent */
			irq_data = irq_data->parent_data;
		}
#endif
		if (WARN_ON(!irq_data || irq_data->chip == &no_irq_chip))
			return;
	}

	/* Uninstall? */
	if (handle == handle_bad_irq) {
		if (desc->irq_data.chip != &no_irq_chip)
			mask_ack_irq(desc);
		irq_state_set_disabled(desc);
		if (is_chained)
			desc->action = NULL;
		desc->depth = 1;
	}
	desc->handle_irq = handle;
	desc->name = name;

	if (handle != handle_bad_irq && is_chained) {
		irq_settings_set_noprobe(desc);
		irq_settings_set_norequest(desc);
		irq_settings_set_nothread(desc);
		desc->action = &chained_action;
		irq_startup(desc, true);
	}
}

void
__irq_set_handler(unsigned int irq, irq_flow_handler_t handle, int is_chained,
		  const char *name)
{
	unsigned long flags;
	struct irq_desc *desc = irq_get_desc_buslock(irq, &flags, 0);

	if (!desc)
		return;

	__irq_do_set_handler(desc, handle, is_chained, name);
	irq_put_desc_busunlock(desc, flags);
}
EXPORT_SYMBOL_GPL(__irq_set_handler);

void
irq_set_chained_handler_and_data(unsigned int irq, irq_flow_handler_t handle,
				 void *data)
{
	unsigned long flags;
	struct irq_desc *desc = irq_get_desc_buslock(irq, &flags, 0);

	if (!desc)
		return;

	__irq_do_set_handler(desc, handle, 1, NULL);
	desc->irq_common_data.handler_data = data;

	irq_put_desc_busunlock(desc, flags);
}
EXPORT_SYMBOL_GPL(irq_set_chained_handler_and_data);

void
irq_set_chip_and_handler_name(unsigned int irq, struct irq_chip *chip,
			      irq_flow_handler_t handle, const char *name)
{
	irq_set_chip(irq, chip);
	__irq_set_handler(irq, handle, 0, name);
}
EXPORT_SYMBOL_GPL(irq_set_chip_and_handler_name);

void irq_modify_status(unsigned int irq, unsigned long clr, unsigned long set)
{
	unsigned long flags;
	struct irq_desc *desc = irq_get_desc_lock(irq, &flags, 0);

	if (!desc)
		return;
	irq_settings_clr_and_set(desc, clr, set);

	irqd_clear(&desc->irq_data, IRQD_NO_BALANCING | IRQD_PER_CPU |
		   IRQD_TRIGGER_MASK | IRQD_LEVEL | IRQD_MOVE_PCNTXT);
	if (irq_settings_has_no_balance_set(desc))
		irqd_set(&desc->irq_data, IRQD_NO_BALANCING);
	if (irq_settings_is_per_cpu(desc))
		irqd_set(&desc->irq_data, IRQD_PER_CPU);
	if (irq_settings_can_move_pcntxt(desc))
		irqd_set(&desc->irq_data, IRQD_MOVE_PCNTXT);
	if (irq_settings_is_level(desc))
		irqd_set(&desc->irq_data, IRQD_LEVEL);

	irqd_set(&desc->irq_data, irq_settings_get_trigger_mask(desc));

	irq_put_desc_unlock(desc, flags);
}
EXPORT_SYMBOL_GPL(irq_modify_status);

/**
 *	irq_cpu_online - Invoke all irq_cpu_online functions.
 *
 *	Iterate through all irqs and invoke the chip.irq_cpu_online()
 *	for each.
 */
void irq_cpu_online(void)
{
	struct irq_desc *desc;
	struct irq_chip *chip;
	unsigned long flags;
	unsigned int irq;

	for_each_active_irq(irq) {
		desc = irq_to_desc(irq);
		if (!desc)
			continue;

		raw_spin_lock_irqsave(&desc->lock, flags);

		chip = irq_data_get_irq_chip(&desc->irq_data);
		if (chip && chip->irq_cpu_online &&
		    (!(chip->flags & IRQCHIP_ONOFFLINE_ENABLED) ||
		     !irqd_irq_disabled(&desc->irq_data)))
			chip->irq_cpu_online(&desc->irq_data);

		raw_spin_unlock_irqrestore(&desc->lock, flags);
	}
}

/**
 *	irq_cpu_offline - Invoke all irq_cpu_offline functions.
 *
 *	Iterate through all irqs and invoke the chip.irq_cpu_offline()
 *	for each.
 */
void irq_cpu_offline(void)
{
	struct irq_desc *desc;
	struct irq_chip *chip;
	unsigned long flags;
	unsigned int irq;

	for_each_active_irq(irq) {
		desc = irq_to_desc(irq);
		if (!desc)
			continue;

		raw_spin_lock_irqsave(&desc->lock, flags);

		chip = irq_data_get_irq_chip(&desc->irq_data);
		if (chip && chip->irq_cpu_offline &&
		    (!(chip->flags & IRQCHIP_ONOFFLINE_ENABLED) ||
		     !irqd_irq_disabled(&desc->irq_data)))
			chip->irq_cpu_offline(&desc->irq_data);

		raw_spin_unlock_irqrestore(&desc->lock, flags);
	}
}

#ifdef	CONFIG_IRQ_DOMAIN_HIERARCHY
/**
 * irq_chip_enable_parent - Enable the parent interrupt (defaults to unmask if
 * NULL)
 * @data:	Pointer to interrupt specific data
 */
void irq_chip_enable_parent(struct irq_data *data)
{
	data = data->parent_data;
	if (data->chip->irq_enable)
		data->chip->irq_enable(data);
	else
		data->chip->irq_unmask(data);
}

/**
 * irq_chip_disable_parent - Disable the parent interrupt (defaults to mask if
 * NULL)
 * @data:	Pointer to interrupt specific data
 */
void irq_chip_disable_parent(struct irq_data *data)
{
	data = data->parent_data;
	if (data->chip->irq_disable)
		data->chip->irq_disable(data);
	else
		data->chip->irq_mask(data);
}

/**
 * irq_chip_ack_parent - Acknowledge the parent interrupt
 * @data:	Pointer to interrupt specific data
 */
void irq_chip_ack_parent(struct irq_data *data)
{
	data = data->parent_data;
	data->chip->irq_ack(data);
}
EXPORT_SYMBOL_GPL(irq_chip_ack_parent);

/**
 * irq_chip_mask_parent - Mask the parent interrupt
 * @data:	Pointer to interrupt specific data
 */
void irq_chip_mask_parent(struct irq_data *data)
{
	data = data->parent_data;
	data->chip->irq_mask(data);
}
EXPORT_SYMBOL_GPL(irq_chip_mask_parent);

/**
 * irq_chip_unmask_parent - Unmask the parent interrupt
 * @data:	Pointer to interrupt specific data
 */
void irq_chip_unmask_parent(struct irq_data *data)
{
	data = data->parent_data;
	data->chip->irq_unmask(data);
}
EXPORT_SYMBOL_GPL(irq_chip_unmask_parent);

/**
 * irq_chip_eoi_parent - Invoke EOI on the parent interrupt
 * @data:	Pointer to interrupt specific data
 */
void irq_chip_eoi_parent(struct irq_data *data)
{
	data = data->parent_data;
	data->chip->irq_eoi(data);
}
EXPORT_SYMBOL_GPL(irq_chip_eoi_parent);

/**
 * irq_chip_set_affinity_parent - Set affinity on the parent interrupt
 * @data:	Pointer to interrupt specific data
 * @dest:	The affinity mask to set
 * @force:	Flag to enforce setting (disable online checks)
 *
 * Conditinal, as the underlying parent chip might not implement it.
 */
int irq_chip_set_affinity_parent(struct irq_data *data,
				 const struct cpumask *dest, bool force)
{
	data = data->parent_data;
	if (data->chip->irq_set_affinity)
		return data->chip->irq_set_affinity(data, dest, force);

	return -ENOSYS;
}

/**
 * irq_chip_set_type_parent - Set IRQ type on the parent interrupt
 * @data:	Pointer to interrupt specific data
 * @type:	IRQ_TYPE_{LEVEL,EDGE}_* value - see include/linux/irq.h
 *
 * Conditional, as the underlying parent chip might not implement it.
 */
int irq_chip_set_type_parent(struct irq_data *data, unsigned int type)
{
	data = data->parent_data;

	if (data->chip->irq_set_type)
		return data->chip->irq_set_type(data, type);

	return -ENOSYS;
}
EXPORT_SYMBOL_GPL(irq_chip_set_type_parent);

/**
 * irq_chip_retrigger_hierarchy - Retrigger an interrupt in hardware
 * @data:	Pointer to interrupt specific data
 *
 * Iterate through the domain hierarchy of the interrupt and check
 * whether a hw retrigger function exists. If yes, invoke it.
 */
int irq_chip_retrigger_hierarchy(struct irq_data *data)
{
	for (data = data->parent_data; data; data = data->parent_data)
		if (data->chip && data->chip->irq_retrigger)
			return data->chip->irq_retrigger(data);

	return 0;
}

/**
 * irq_chip_set_vcpu_affinity_parent - Set vcpu affinity on the parent interrupt
 * @data:	Pointer to interrupt specific data
 * @vcpu_info:	The vcpu affinity information
 */
int irq_chip_set_vcpu_affinity_parent(struct irq_data *data, void *vcpu_info)
{
	data = data->parent_data;
	if (data->chip->irq_set_vcpu_affinity)
		return data->chip->irq_set_vcpu_affinity(data, vcpu_info);

	return -ENOSYS;
}

/**
 * irq_chip_set_wake_parent - Set/reset wake-up on the parent interrupt
 * @data:	Pointer to interrupt specific data
 * @on:		Whether to set or reset the wake-up capability of this irq
 *
 * Conditional, as the underlying parent chip might not implement it.
 */
int irq_chip_set_wake_parent(struct irq_data *data, unsigned int on)
{
	data = data->parent_data;
	if (data->chip->irq_set_wake)
		return data->chip->irq_set_wake(data, on);

	return -ENOSYS;
}
#endif

/**
 * irq_chip_compose_msi_msg - Componse msi message for a irq chip
 * @data:	Pointer to interrupt specific data
 * @msg:	Pointer to the MSI message
 *
 * For hierarchical domains we find the first chip in the hierarchy
 * which implements the irq_compose_msi_msg callback. For non
 * hierarchical we use the top level chip.
 */
int irq_chip_compose_msi_msg(struct irq_data *data, struct msi_msg *msg)
{
	struct irq_data *pos = NULL;

#ifdef	CONFIG_IRQ_DOMAIN_HIERARCHY
	for (; data; data = data->parent_data)
#endif
		if (data->chip && data->chip->irq_compose_msi_msg)
			pos = data;
	if (!pos)
		return -ENOSYS;

	pos->chip->irq_compose_msi_msg(pos, msg);

	return 0;
}
