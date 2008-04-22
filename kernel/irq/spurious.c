/*
 * linux/kernel/irq/spurious.c
 *
 * Copyright (C) 1992, 1998-2004 Linus Torvalds, Ingo Molnar
 *
 * This file contains spurious interrupt handling.
 */

#include <linux/jiffies.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/kallsyms.h>
#include <linux/interrupt.h>
#include <linux/moduleparam.h>

static int irqfixup __read_mostly;

/*
 * Recovery handler for misrouted interrupts.
 */
static int misrouted_irq(int irq)
{
	int i;
	int ok = 0;
	int work = 0;	/* Did we do work for a real IRQ */

	for (i = 1; i < NR_IRQS; i++) {
		struct irq_desc *desc = irq_desc + i;
		struct irqaction *action;

		if (i == irq)	/* Already tried */
			continue;

		spin_lock(&desc->lock);
		/* Already running on another processor */
		if (desc->status & IRQ_INPROGRESS) {
			/*
			 * Already running: If it is shared get the other
			 * CPU to go looking for our mystery interrupt too
			 */
			if (desc->action && (desc->action->flags & IRQF_SHARED))
				desc->status |= IRQ_PENDING;
			spin_unlock(&desc->lock);
			continue;
		}
		/* Honour the normal IRQ locking */
		desc->status |= IRQ_INPROGRESS;
		action = desc->action;
		spin_unlock(&desc->lock);

		while (action) {
			/* Only shared IRQ handlers are safe to call */
			if (action->flags & IRQF_SHARED) {
				if (action->handler(i, action->dev_id) ==
						IRQ_HANDLED)
					ok = 1;
			}
			action = action->next;
		}
		local_irq_disable();
		/* Now clean up the flags */
		spin_lock(&desc->lock);
		action = desc->action;

		/*
		 * While we were looking for a fixup someone queued a real
		 * IRQ clashing with our walk:
		 */
		while ((desc->status & IRQ_PENDING) && action) {
			/*
			 * Perform real IRQ processing for the IRQ we deferred
			 */
			work = 1;
			spin_unlock(&desc->lock);
			handle_IRQ_event(i, action);
			spin_lock(&desc->lock);
			desc->status &= ~IRQ_PENDING;
		}
		desc->status &= ~IRQ_INPROGRESS;
		/*
		 * If we did actual work for the real IRQ line we must let the
		 * IRQ controller clean up too
		 */
		if (work && desc->chip && desc->chip->end)
			desc->chip->end(i);
		spin_unlock(&desc->lock);
	}
	/* So the caller can adjust the irq error counts */
	return ok;
}

/*
 * If 99,900 of the previous 100,000 interrupts have not been handled
 * then assume that the IRQ is stuck in some manner. Drop a diagnostic
 * and try to turn the IRQ off.
 *
 * (The other 100-of-100,000 interrupts may have been a correctly
 *  functioning device sharing an IRQ with the failing one)
 *
 * Called under desc->lock
 */

static void
__report_bad_irq(unsigned int irq, struct irq_desc *desc,
		 irqreturn_t action_ret)
{
	struct irqaction *action;

	if (action_ret != IRQ_HANDLED && action_ret != IRQ_NONE) {
		printk(KERN_ERR "irq event %d: bogus return value %x\n",
				irq, action_ret);
	} else {
		printk(KERN_ERR "irq %d: nobody cared (try booting with "
				"the \"irqpoll\" option)\n", irq);
	}
	dump_stack();
	printk(KERN_ERR "handlers:\n");

	action = desc->action;
	while (action) {
		printk(KERN_ERR "[<%p>]", action->handler);
		print_symbol(" (%s)",
			(unsigned long)action->handler);
		printk("\n");
		action = action->next;
	}
}

static void
report_bad_irq(unsigned int irq, struct irq_desc *desc, irqreturn_t action_ret)
{
	static int count = 100;

	if (count > 0) {
		count--;
		__report_bad_irq(irq, desc, action_ret);
	}
}

static inline int try_misrouted_irq(unsigned int irq, struct irq_desc *desc, irqreturn_t action_ret)
{
	struct irqaction *action;

	if (!irqfixup)
		return 0;

	/* We didn't actually handle the IRQ - see if it was misrouted? */
	if (action_ret == IRQ_NONE)
		return 1;

	/*
	 * But for 'irqfixup == 2' we also do it for handled interrupts if
	 * they are marked as IRQF_IRQPOLL (or for irq zero, which is the
	 * traditional PC timer interrupt.. Legacy)
	 */
	if (irqfixup < 2)
		return 0;

	if (!irq)
		return 1;

	/*
	 * Since we don't get the descriptor lock, "action" can
	 * change under us.  We don't really care, but we don't
	 * want to follow a NULL pointer. So tell the compiler to
	 * just load it once by using a barrier.
	 */
	action = desc->action;
	barrier();
	return action && (action->flags & IRQF_IRQPOLL);
}

void note_interrupt(unsigned int irq, struct irq_desc *desc,
		    irqreturn_t action_ret)
{
	if (unlikely(action_ret != IRQ_HANDLED)) {
		/*
		 * If we are seeing only the odd spurious IRQ caused by
		 * bus asynchronicity then don't eventually trigger an error,
		 * otherwise the couter becomes a doomsday timer for otherwise
		 * working systems
		 */
		if (time_after(jiffies, desc->last_unhandled + HZ/10))
			desc->irqs_unhandled = 1;
		else
			desc->irqs_unhandled++;
		desc->last_unhandled = jiffies;
		if (unlikely(action_ret != IRQ_NONE))
			report_bad_irq(irq, desc, action_ret);
	}

	if (unlikely(try_misrouted_irq(irq, desc, action_ret))) {
		int ok = misrouted_irq(irq);
		if (action_ret == IRQ_NONE)
			desc->irqs_unhandled -= ok;
	}

	desc->irq_count++;
	if (likely(desc->irq_count < 100000))
		return;

	desc->irq_count = 0;
	if (unlikely(desc->irqs_unhandled > 99900)) {
		/*
		 * The interrupt is stuck
		 */
		__report_bad_irq(irq, desc, action_ret);
		/*
		 * Now kill the IRQ
		 */
		printk(KERN_EMERG "Disabling IRQ #%d\n", irq);
		desc->status |= IRQ_DISABLED;
		desc->depth = 1;
		desc->chip->disable(irq);
	}
	desc->irqs_unhandled = 0;
}

int noirqdebug __read_mostly;

int noirqdebug_setup(char *str)
{
	noirqdebug = 1;
	printk(KERN_INFO "IRQ lockup detection disabled\n");

	return 1;
}

__setup("noirqdebug", noirqdebug_setup);
module_param(noirqdebug, bool, 0644);
MODULE_PARM_DESC(noirqdebug, "Disable irq lockup detection when true");

static int __init irqfixup_setup(char *str)
{
	irqfixup = 1;
	printk(KERN_WARNING "Misrouted IRQ fixup support enabled.\n");
	printk(KERN_WARNING "This may impact system performance.\n");

	return 1;
}

__setup("irqfixup", irqfixup_setup);
module_param(irqfixup, int, 0644);
MODULE_PARM_DESC("irqfixup", "0: No fixup, 1: irqfixup mode 2: irqpoll mode");

static int __init irqpoll_setup(char *str)
{
	irqfixup = 2;
	printk(KERN_WARNING "Misrouted IRQ fixup and polling support "
				"enabled\n");
	printk(KERN_WARNING "This may significantly impact system "
				"performance\n");
	return 1;
}

__setup("irqpoll", irqpoll_setup);
