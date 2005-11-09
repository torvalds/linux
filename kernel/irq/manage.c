/*
 * linux/kernel/irq/manage.c
 *
 * Copyright (C) 1992, 1998-2004 Linus Torvalds, Ingo Molnar
 *
 * This file contains driver APIs to the irq subsystem.
 */

#include <linux/config.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/random.h>
#include <linux/interrupt.h>

#include "internals.h"

#ifdef CONFIG_SMP

cpumask_t irq_affinity[NR_IRQS] = { [0 ... NR_IRQS-1] = CPU_MASK_ALL };

#if defined (CONFIG_GENERIC_PENDING_IRQ) || defined (CONFIG_IRQBALANCE)
cpumask_t __cacheline_aligned pending_irq_cpumask[NR_IRQS];
#endif

/**
 *	synchronize_irq - wait for pending IRQ handlers (on other CPUs)
 *	@irq: interrupt number to wait for
 *
 *	This function waits for any pending IRQ handlers for this interrupt
 *	to complete before returning. If you use this function while
 *	holding a resource the IRQ handler may need you will deadlock.
 *
 *	This function may be called - with care - from IRQ context.
 */
void synchronize_irq(unsigned int irq)
{
	struct irq_desc *desc = irq_desc + irq;

	while (desc->status & IRQ_INPROGRESS)
		cpu_relax();
}

EXPORT_SYMBOL(synchronize_irq);

#endif

/**
 *	disable_irq_nosync - disable an irq without waiting
 *	@irq: Interrupt to disable
 *
 *	Disable the selected interrupt line.  Disables and Enables are
 *	nested.
 *	Unlike disable_irq(), this function does not ensure existing
 *	instances of the IRQ handler have completed before returning.
 *
 *	This function may be called from IRQ context.
 */
void disable_irq_nosync(unsigned int irq)
{
	irq_desc_t *desc = irq_desc + irq;
	unsigned long flags;

	spin_lock_irqsave(&desc->lock, flags);
	if (!desc->depth++) {
		desc->status |= IRQ_DISABLED;
		desc->handler->disable(irq);
	}
	spin_unlock_irqrestore(&desc->lock, flags);
}

EXPORT_SYMBOL(disable_irq_nosync);

/**
 *	disable_irq - disable an irq and wait for completion
 *	@irq: Interrupt to disable
 *
 *	Disable the selected interrupt line.  Enables and Disables are
 *	nested.
 *	This function waits for any pending IRQ handlers for this interrupt
 *	to complete before returning. If you use this function while
 *	holding a resource the IRQ handler may need you will deadlock.
 *
 *	This function may be called - with care - from IRQ context.
 */
void disable_irq(unsigned int irq)
{
	irq_desc_t *desc = irq_desc + irq;

	disable_irq_nosync(irq);
	if (desc->action)
		synchronize_irq(irq);
}

EXPORT_SYMBOL(disable_irq);

/**
 *	enable_irq - enable handling of an irq
 *	@irq: Interrupt to enable
 *
 *	Undoes the effect of one call to disable_irq().  If this
 *	matches the last disable, processing of interrupts on this
 *	IRQ line is re-enabled.
 *
 *	This function may be called from IRQ context.
 */
void enable_irq(unsigned int irq)
{
	irq_desc_t *desc = irq_desc + irq;
	unsigned long flags;

	spin_lock_irqsave(&desc->lock, flags);
	switch (desc->depth) {
	case 0:
		WARN_ON(1);
		break;
	case 1: {
		unsigned int status = desc->status & ~IRQ_DISABLED;

		desc->status = status;
		if ((status & (IRQ_PENDING | IRQ_REPLAY)) == IRQ_PENDING) {
			desc->status = status | IRQ_REPLAY;
			hw_resend_irq(desc->handler,irq);
		}
		desc->handler->enable(irq);
		/* fall-through */
	}
	default:
		desc->depth--;
	}
	spin_unlock_irqrestore(&desc->lock, flags);
}

EXPORT_SYMBOL(enable_irq);

/*
 * Internal function that tells the architecture code whether a
 * particular irq has been exclusively allocated or is available
 * for driver use.
 */
int can_request_irq(unsigned int irq, unsigned long irqflags)
{
	struct irqaction *action;

	if (irq >= NR_IRQS)
		return 0;

	action = irq_desc[irq].action;
	if (action)
		if (irqflags & action->flags & SA_SHIRQ)
			action = NULL;

	return !action;
}

/*
 * Internal function to register an irqaction - typically used to
 * allocate special interrupts that are part of the architecture.
 */
int setup_irq(unsigned int irq, struct irqaction * new)
{
	struct irq_desc *desc = irq_desc + irq;
	struct irqaction *old, **p;
	unsigned long flags;
	int shared = 0;

	if (desc->handler == &no_irq_type)
		return -ENOSYS;
	/*
	 * Some drivers like serial.c use request_irq() heavily,
	 * so we have to be careful not to interfere with a
	 * running system.
	 */
	if (new->flags & SA_SAMPLE_RANDOM) {
		/*
		 * This function might sleep, we want to call it first,
		 * outside of the atomic block.
		 * Yes, this might clear the entropy pool if the wrong
		 * driver is attempted to be loaded, without actually
		 * installing a new handler, but is this really a problem,
		 * only the sysadmin is able to do this.
		 */
		rand_initialize_irq(irq);
	}

	/*
	 * The following block of code has to be executed atomically
	 */
	spin_lock_irqsave(&desc->lock,flags);
	p = &desc->action;
	if ((old = *p) != NULL) {
		/* Can't share interrupts unless both agree to */
		if (!(old->flags & new->flags & SA_SHIRQ)) {
			spin_unlock_irqrestore(&desc->lock,flags);
			return -EBUSY;
		}

		/* add new interrupt at end of irq queue */
		do {
			p = &old->next;
			old = *p;
		} while (old);
		shared = 1;
	}

	*p = new;

	if (!shared) {
		desc->depth = 0;
		desc->status &= ~(IRQ_DISABLED | IRQ_AUTODETECT |
				  IRQ_WAITING | IRQ_INPROGRESS);
		if (desc->handler->startup)
			desc->handler->startup(irq);
		else
			desc->handler->enable(irq);
	}
	spin_unlock_irqrestore(&desc->lock,flags);

	new->irq = irq;
	register_irq_proc(irq);
	new->dir = NULL;
	register_handler_proc(irq, new);

	return 0;
}

/**
 *	free_irq - free an interrupt
 *	@irq: Interrupt line to free
 *	@dev_id: Device identity to free
 *
 *	Remove an interrupt handler. The handler is removed and if the
 *	interrupt line is no longer in use by any driver it is disabled.
 *	On a shared IRQ the caller must ensure the interrupt is disabled
 *	on the card it drives before calling this function. The function
 *	does not return until any executing interrupts for this IRQ
 *	have completed.
 *
 *	This function must not be called from interrupt context.
 */
void free_irq(unsigned int irq, void *dev_id)
{
	struct irq_desc *desc;
	struct irqaction **p;
	unsigned long flags;

	if (irq >= NR_IRQS)
		return;

	desc = irq_desc + irq;
	spin_lock_irqsave(&desc->lock,flags);
	p = &desc->action;
	for (;;) {
		struct irqaction * action = *p;

		if (action) {
			struct irqaction **pp = p;

			p = &action->next;
			if (action->dev_id != dev_id)
				continue;

			/* Found it - now remove it from the list of entries */
			*pp = action->next;

			/* Currently used only by UML, might disappear one day.*/
#ifdef CONFIG_IRQ_RELEASE_METHOD
			if (desc->handler->release)
				desc->handler->release(irq, dev_id);
#endif

			if (!desc->action) {
				desc->status |= IRQ_DISABLED;
				if (desc->handler->shutdown)
					desc->handler->shutdown(irq);
				else
					desc->handler->disable(irq);
			}
			spin_unlock_irqrestore(&desc->lock,flags);
			unregister_handler_proc(irq, action);

			/* Make sure it's not being used on another CPU */
			synchronize_irq(irq);
			kfree(action);
			return;
		}
		printk(KERN_ERR "Trying to free free IRQ%d\n",irq);
		spin_unlock_irqrestore(&desc->lock,flags);
		return;
	}
}

EXPORT_SYMBOL(free_irq);

/**
 *	request_irq - allocate an interrupt line
 *	@irq: Interrupt line to allocate
 *	@handler: Function to be called when the IRQ occurs
 *	@irqflags: Interrupt type flags
 *	@devname: An ascii name for the claiming device
 *	@dev_id: A cookie passed back to the handler function
 *
 *	This call allocates interrupt resources and enables the
 *	interrupt line and IRQ handling. From the point this
 *	call is made your handler function may be invoked. Since
 *	your handler function must clear any interrupt the board
 *	raises, you must take care both to initialise your hardware
 *	and to set up the interrupt handler in the right order.
 *
 *	Dev_id must be globally unique. Normally the address of the
 *	device data structure is used as the cookie. Since the handler
 *	receives this value it makes sense to use it.
 *
 *	If your interrupt is shared you must pass a non NULL dev_id
 *	as this is required when freeing the interrupt.
 *
 *	Flags:
 *
 *	SA_SHIRQ		Interrupt is shared
 *	SA_INTERRUPT		Disable local interrupts while processing
 *	SA_SAMPLE_RANDOM	The interrupt can be used for entropy
 *
 */
int request_irq(unsigned int irq,
		irqreturn_t (*handler)(int, void *, struct pt_regs *),
		unsigned long irqflags, const char * devname, void *dev_id)
{
	struct irqaction * action;
	int retval;

	/*
	 * Sanity-check: shared interrupts must pass in a real dev-ID,
	 * otherwise we'll have trouble later trying to figure out
	 * which interrupt is which (messes up the interrupt freeing
	 * logic etc).
	 */
	if ((irqflags & SA_SHIRQ) && !dev_id)
		return -EINVAL;
	if (irq >= NR_IRQS)
		return -EINVAL;
	if (!handler)
		return -EINVAL;

	action = kmalloc(sizeof(struct irqaction), GFP_ATOMIC);
	if (!action)
		return -ENOMEM;

	action->handler = handler;
	action->flags = irqflags;
	cpus_clear(action->mask);
	action->name = devname;
	action->next = NULL;
	action->dev_id = dev_id;

	retval = setup_irq(irq, action);
	if (retval)
		kfree(action);

	return retval;
}

EXPORT_SYMBOL(request_irq);

