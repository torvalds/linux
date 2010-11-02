/*
 * linux/kernel/irq/handle.c
 *
 * Copyright (C) 1992, 1998-2006 Linus Torvalds, Ingo Molnar
 * Copyright (C) 2005-2006, Thomas Gleixner, Russell King
 *
 * This file contains the core interrupt handling code.
 *
 * Detailed information is available in Documentation/DocBook/genericirq
 *
 */

#include <linux/irq.h>
#include <linux/random.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>

#include <trace/events/irq.h>

#include "internals.h"

/**
 * handle_bad_irq - handle spurious and unhandled irqs
 * @irq:       the interrupt number
 * @desc:      description of the interrupt
 *
 * Handles spurious and unhandled IRQ's. It also prints a debugmessage.
 */
void handle_bad_irq(unsigned int irq, struct irq_desc *desc)
{
	print_irq_desc(irq, desc);
	kstat_incr_irqs_this_cpu(irq, desc);
	ack_bad_irq(irq);
}

/*
 * Special, empty irq handler:
 */
irqreturn_t no_action(int cpl, void *dev_id)
{
	return IRQ_NONE;
}

static void warn_no_thread(unsigned int irq, struct irqaction *action)
{
	if (test_and_set_bit(IRQTF_WARNED, &action->thread_flags))
		return;

	printk(KERN_WARNING "IRQ %d device %s returned IRQ_WAKE_THREAD "
	       "but no thread function available.", irq, action->name);
}

/**
 * handle_IRQ_event - irq action chain handler
 * @irq:	the interrupt number
 * @action:	the interrupt action chain for this irq
 *
 * Handles the action chain of an irq event
 */
irqreturn_t handle_IRQ_event(unsigned int irq, struct irqaction *action)
{
	irqreturn_t ret, retval = IRQ_NONE;
	unsigned int status = 0;

	do {
		trace_irq_handler_entry(irq, action);
		ret = action->handler(irq, action->dev_id);
		trace_irq_handler_exit(irq, action, ret);

		switch (ret) {
		case IRQ_WAKE_THREAD:
			/*
			 * Set result to handled so the spurious check
			 * does not trigger.
			 */
			ret = IRQ_HANDLED;

			/*
			 * Catch drivers which return WAKE_THREAD but
			 * did not set up a thread function
			 */
			if (unlikely(!action->thread_fn)) {
				warn_no_thread(irq, action);
				break;
			}

			/*
			 * Wake up the handler thread for this
			 * action. In case the thread crashed and was
			 * killed we just pretend that we handled the
			 * interrupt. The hardirq handler above has
			 * disabled the device interrupt, so no irq
			 * storm is lurking.
			 */
			if (likely(!test_bit(IRQTF_DIED,
					     &action->thread_flags))) {
				set_bit(IRQTF_RUNTHREAD, &action->thread_flags);
				wake_up_process(action->thread);
			}

			/* Fall through to add to randomness */
		case IRQ_HANDLED:
			status |= action->flags;
			break;

		default:
			break;
		}

		retval |= ret;
		action = action->next;
	} while (action);

	if (status & IRQF_SAMPLE_RANDOM)
		add_interrupt_randomness(irq);
	local_irq_disable();

	return retval;
}

#ifndef CONFIG_GENERIC_HARDIRQS_NO__DO_IRQ

#ifdef CONFIG_ENABLE_WARN_DEPRECATED
# warning __do_IRQ is deprecated. Please convert to proper flow handlers
#endif

/**
 * __do_IRQ - original all in one highlevel IRQ handler
 * @irq:	the interrupt number
 *
 * __do_IRQ handles all normal device IRQ's (the special
 * SMP cross-CPU interrupts have their own specific
 * handlers).
 *
 * This is the original x86 implementation which is used for every
 * interrupt type.
 */
unsigned int __do_IRQ(unsigned int irq)
{
	struct irq_desc *desc = irq_to_desc(irq);
	struct irqaction *action;
	unsigned int status;

	kstat_incr_irqs_this_cpu(irq, desc);

	if (CHECK_IRQ_PER_CPU(desc->status)) {
		irqreturn_t action_ret;

		/*
		 * No locking required for CPU-local interrupts:
		 */
		if (desc->irq_data.chip->ack)
			desc->irq_data.chip->ack(irq);
		if (likely(!(desc->status & IRQ_DISABLED))) {
			action_ret = handle_IRQ_event(irq, desc->action);
			if (!noirqdebug)
				note_interrupt(irq, desc, action_ret);
		}
		desc->irq_data.chip->end(irq);
		return 1;
	}

	raw_spin_lock(&desc->lock);
	if (desc->irq_data.chip->ack)
		desc->irq_data.chip->ack(irq);
	/*
	 * REPLAY is when Linux resends an IRQ that was dropped earlier
	 * WAITING is used by probe to mark irqs that are being tested
	 */
	status = desc->status & ~(IRQ_REPLAY | IRQ_WAITING);
	status |= IRQ_PENDING; /* we _want_ to handle it */

	/*
	 * If the IRQ is disabled for whatever reason, we cannot
	 * use the action we have.
	 */
	action = NULL;
	if (likely(!(status & (IRQ_DISABLED | IRQ_INPROGRESS)))) {
		action = desc->action;
		status &= ~IRQ_PENDING; /* we commit to handling */
		status |= IRQ_INPROGRESS; /* we are handling it */
	}
	desc->status = status;

	/*
	 * If there is no IRQ handler or it was disabled, exit early.
	 * Since we set PENDING, if another processor is handling
	 * a different instance of this same irq, the other processor
	 * will take care of it.
	 */
	if (unlikely(!action))
		goto out;

	/*
	 * Edge triggered interrupts need to remember
	 * pending events.
	 * This applies to any hw interrupts that allow a second
	 * instance of the same irq to arrive while we are in do_IRQ
	 * or in the handler. But the code here only handles the _second_
	 * instance of the irq, not the third or fourth. So it is mostly
	 * useful for irq hardware that does not mask cleanly in an
	 * SMP environment.
	 */
	for (;;) {
		irqreturn_t action_ret;

		raw_spin_unlock(&desc->lock);

		action_ret = handle_IRQ_event(irq, action);
		if (!noirqdebug)
			note_interrupt(irq, desc, action_ret);

		raw_spin_lock(&desc->lock);
		if (likely(!(desc->status & IRQ_PENDING)))
			break;
		desc->status &= ~IRQ_PENDING;
	}
	desc->status &= ~IRQ_INPROGRESS;

out:
	/*
	 * The ->end() handler has to deal with interrupts which got
	 * disabled while the handler was running.
	 */
	desc->irq_data.chip->end(irq);
	raw_spin_unlock(&desc->lock);

	return 1;
}
#endif
