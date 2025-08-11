// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 1992, 1998-2004 Linus Torvalds, Ingo Molnar
 *
 * This file contains spurious interrupt handling.
 */

#include <linux/jiffies.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/moduleparam.h>
#include <linux/timer.h>

#include "internals.h"

static int irqfixup __read_mostly;

#define POLL_SPURIOUS_IRQ_INTERVAL (HZ/10)
static void poll_spurious_irqs(struct timer_list *unused);
static DEFINE_TIMER(poll_spurious_irq_timer, poll_spurious_irqs);
int irq_poll_cpu;
static atomic_t irq_poll_active;

/*
 * Recovery handler for misrouted interrupts.
 */
static bool try_one_irq(struct irq_desc *desc, bool force)
{
	struct irqaction *action;
	bool ret = false;

	guard(raw_spinlock)(&desc->lock);

	/*
	 * PER_CPU, nested thread interrupts and interrupts explicitly
	 * marked polled are excluded from polling.
	 */
	if (irq_settings_is_per_cpu(desc) || irq_settings_is_nested_thread(desc) ||
	    irq_settings_is_polled(desc))
		return false;

	/*
	 * Do not poll disabled interrupts unless the spurious
	 * disabled poller asks explicitly.
	 */
	if (irqd_irq_disabled(&desc->irq_data) && !force)
		return false;

	/*
	 * All handlers must agree on IRQF_SHARED, so we test just the
	 * first.
	 */
	action = desc->action;
	if (!action || !(action->flags & IRQF_SHARED) || (action->flags & __IRQF_TIMER))
		return false;

	/* Already running on another processor */
	if (irqd_irq_inprogress(&desc->irq_data)) {
		/*
		 * Already running: If it is shared get the other
		 * CPU to go looking for our mystery interrupt too
		 */
		desc->istate |= IRQS_PENDING;
		return false;
	}

	/* Mark it poll in progress */
	desc->istate |= IRQS_POLL_INPROGRESS;
	do {
		if (handle_irq_event(desc) == IRQ_HANDLED)
			ret = true;
		/* Make sure that there is still a valid action */
		action = desc->action;
	} while ((desc->istate & IRQS_PENDING) && action);
	desc->istate &= ~IRQS_POLL_INPROGRESS;
	return ret;
}

static int misrouted_irq(int irq)
{
	struct irq_desc *desc;
	int i, ok = 0;

	if (atomic_inc_return(&irq_poll_active) != 1)
		goto out;

	irq_poll_cpu = smp_processor_id();

	for_each_irq_desc(i, desc) {
		if (!i)
			 continue;

		if (i == irq)	/* Already tried */
			continue;

		if (try_one_irq(desc, false))
			ok = 1;
	}
out:
	atomic_dec(&irq_poll_active);
	/* So the caller can adjust the irq error counts */
	return ok;
}

static void poll_spurious_irqs(struct timer_list *unused)
{
	struct irq_desc *desc;
	int i;

	if (atomic_inc_return(&irq_poll_active) != 1)
		goto out;
	irq_poll_cpu = smp_processor_id();

	for_each_irq_desc(i, desc) {
		unsigned int state;

		if (!i)
			 continue;

		/* Racy but it doesn't matter */
		state = READ_ONCE(desc->istate);
		if (!(state & IRQS_SPURIOUS_DISABLED))
			continue;

		local_irq_disable();
		try_one_irq(desc, true);
		local_irq_enable();
	}
out:
	atomic_dec(&irq_poll_active);
	mod_timer(&poll_spurious_irq_timer, jiffies + POLL_SPURIOUS_IRQ_INTERVAL);
}

static inline int bad_action_ret(irqreturn_t action_ret)
{
	unsigned int r = action_ret;

	if (likely(r <= (IRQ_HANDLED | IRQ_WAKE_THREAD)))
		return 0;
	return 1;
}

/*
 * If 99,900 of the previous 100,000 interrupts have not been handled
 * then assume that the IRQ is stuck in some manner. Drop a diagnostic
 * and try to turn the IRQ off.
 *
 * (The other 100-of-100,000 interrupts may have been a correctly
 *  functioning device sharing an IRQ with the failing one)
 */
static void __report_bad_irq(struct irq_desc *desc, irqreturn_t action_ret)
{
	unsigned int irq = irq_desc_get_irq(desc);
	struct irqaction *action;

	if (bad_action_ret(action_ret))
		pr_err("irq event %d: bogus return value %x\n", irq, action_ret);
	else
		pr_err("irq %d: nobody cared (try booting with the \"irqpoll\" option)\n", irq);
	dump_stack();
	pr_err("handlers:\n");

	/*
	 * We need to take desc->lock here. note_interrupt() is called
	 * w/o desc->lock held, but IRQ_PROGRESS set. We might race
	 * with something else removing an action. It's ok to take
	 * desc->lock here. See synchronize_irq().
	 */
	guard(raw_spinlock_irqsave)(&desc->lock);
	for_each_action_of_desc(desc, action) {
		pr_err("[<%p>] %ps", action->handler, action->handler);
		if (action->thread_fn)
			pr_cont(" threaded [<%p>] %ps", action->thread_fn, action->thread_fn);
		pr_cont("\n");
	}
}

static void report_bad_irq(struct irq_desc *desc, irqreturn_t action_ret)
{
	static int count = 100;

	if (count > 0) {
		count--;
		__report_bad_irq(desc, action_ret);
	}
}

static inline bool try_misrouted_irq(unsigned int irq, struct irq_desc *desc,
				     irqreturn_t action_ret)
{
	struct irqaction *action;

	if (!irqfixup)
		return false;

	/* We didn't actually handle the IRQ - see if it was misrouted? */
	if (action_ret == IRQ_NONE)
		return true;

	/*
	 * But for 'irqfixup == 2' we also do it for handled interrupts if
	 * they are marked as IRQF_IRQPOLL (or for irq zero, which is the
	 * traditional PC timer interrupt.. Legacy)
	 */
	if (irqfixup < 2)
		return false;

	if (!irq)
		return true;

	/*
	 * Since we don't get the descriptor lock, "action" can
	 * change under us.
	 */
	action = READ_ONCE(desc->action);
	return action && (action->flags & IRQF_IRQPOLL);
}

#define SPURIOUS_DEFERRED	0x80000000

void note_interrupt(struct irq_desc *desc, irqreturn_t action_ret)
{
	unsigned int irq;

	if (desc->istate & IRQS_POLL_INPROGRESS || irq_settings_is_polled(desc))
		return;

	if (bad_action_ret(action_ret)) {
		report_bad_irq(desc, action_ret);
		return;
	}

	/*
	 * We cannot call note_interrupt from the threaded handler
	 * because we need to look at the compound of all handlers
	 * (primary and threaded). Aside of that in the threaded
	 * shared case we have no serialization against an incoming
	 * hardware interrupt while we are dealing with a threaded
	 * result.
	 *
	 * So in case a thread is woken, we just note the fact and
	 * defer the analysis to the next hardware interrupt.
	 *
	 * The threaded handlers store whether they successfully
	 * handled an interrupt and we check whether that number
	 * changed versus the last invocation.
	 *
	 * We could handle all interrupts with the delayed by one
	 * mechanism, but for the non forced threaded case we'd just
	 * add pointless overhead to the straight hardirq interrupts
	 * for the sake of a few lines less code.
	 */
	if (action_ret & IRQ_WAKE_THREAD) {
		/*
		 * There is a thread woken. Check whether one of the
		 * shared primary handlers returned IRQ_HANDLED. If
		 * not we defer the spurious detection to the next
		 * interrupt.
		 */
		if (action_ret == IRQ_WAKE_THREAD) {
			int handled;
			/*
			 * We use bit 31 of thread_handled_last to
			 * denote the deferred spurious detection
			 * active. No locking necessary as
			 * thread_handled_last is only accessed here
			 * and we have the guarantee that hard
			 * interrupts are not reentrant.
			 */
			if (!(desc->threads_handled_last & SPURIOUS_DEFERRED)) {
				desc->threads_handled_last |= SPURIOUS_DEFERRED;
				return;
			}
			/*
			 * Check whether one of the threaded handlers
			 * returned IRQ_HANDLED since the last
			 * interrupt happened.
			 *
			 * For simplicity we just set bit 31, as it is
			 * set in threads_handled_last as well. So we
			 * avoid extra masking. And we really do not
			 * care about the high bits of the handled
			 * count. We just care about the count being
			 * different than the one we saw before.
			 */
			handled = atomic_read(&desc->threads_handled);
			handled |= SPURIOUS_DEFERRED;
			if (handled != desc->threads_handled_last) {
				action_ret = IRQ_HANDLED;
				/*
				 * Note: We keep the SPURIOUS_DEFERRED
				 * bit set. We are handling the
				 * previous invocation right now.
				 * Keep it for the current one, so the
				 * next hardware interrupt will
				 * account for it.
				 */
				desc->threads_handled_last = handled;
			} else {
				/*
				 * None of the threaded handlers felt
				 * responsible for the last interrupt
				 *
				 * We keep the SPURIOUS_DEFERRED bit
				 * set in threads_handled_last as we
				 * need to account for the current
				 * interrupt as well.
				 */
				action_ret = IRQ_NONE;
			}
		} else {
			/*
			 * One of the primary handlers returned
			 * IRQ_HANDLED. So we don't care about the
			 * threaded handlers on the same line. Clear
			 * the deferred detection bit.
			 *
			 * In theory we could/should check whether the
			 * deferred bit is set and take the result of
			 * the previous run into account here as
			 * well. But it's really not worth the
			 * trouble. If every other interrupt is
			 * handled we never trigger the spurious
			 * detector. And if this is just the one out
			 * of 100k unhandled ones which is handled
			 * then we merily delay the spurious detection
			 * by one hard interrupt. Not a real problem.
			 */
			desc->threads_handled_last &= ~SPURIOUS_DEFERRED;
		}
	}

	if (unlikely(action_ret == IRQ_NONE)) {
		/*
		 * If we are seeing only the odd spurious IRQ caused by
		 * bus asynchronicity then don't eventually trigger an error,
		 * otherwise the counter becomes a doomsday timer for otherwise
		 * working systems
		 */
		if (time_after(jiffies, desc->last_unhandled + HZ/10))
			desc->irqs_unhandled = 1;
		else
			desc->irqs_unhandled++;
		desc->last_unhandled = jiffies;
	}

	irq = irq_desc_get_irq(desc);
	if (unlikely(try_misrouted_irq(irq, desc, action_ret))) {
		int ok = misrouted_irq(irq);
		if (action_ret == IRQ_NONE)
			desc->irqs_unhandled -= ok;
	}

	if (likely(!desc->irqs_unhandled))
		return;

	/* Now getting into unhandled irq detection */
	desc->irq_count++;
	if (likely(desc->irq_count < 100000))
		return;

	desc->irq_count = 0;
	if (unlikely(desc->irqs_unhandled > 99900)) {
		/*
		 * The interrupt is stuck
		 */
		__report_bad_irq(desc, action_ret);
		/*
		 * Now kill the IRQ
		 */
		pr_emerg("Disabling IRQ #%d\n", irq);
		desc->istate |= IRQS_SPURIOUS_DISABLED;
		desc->depth++;
		irq_disable(desc);

		mod_timer(&poll_spurious_irq_timer, jiffies + POLL_SPURIOUS_IRQ_INTERVAL);
	}
	desc->irqs_unhandled = 0;
}

bool noirqdebug __read_mostly;

int noirqdebug_setup(char *str)
{
	noirqdebug = 1;
	pr_info("IRQ lockup detection disabled\n");
	return 1;
}
__setup("noirqdebug", noirqdebug_setup);
module_param(noirqdebug, bool, 0644);
MODULE_PARM_DESC(noirqdebug, "Disable irq lockup detection when true");

static int __init irqfixup_setup(char *str)
{
	if (IS_ENABLED(CONFIG_PREEMPT_RT)) {
		pr_warn("irqfixup boot option not supported with PREEMPT_RT\n");
		return 1;
	}
	irqfixup = 1;
	pr_warn("Misrouted IRQ fixup support enabled.\n");
	pr_warn("This may impact system performance.\n");
	return 1;
}
__setup("irqfixup", irqfixup_setup);
module_param(irqfixup, int, 0644);

static int __init irqpoll_setup(char *str)
{
	if (IS_ENABLED(CONFIG_PREEMPT_RT)) {
		pr_warn("irqpoll boot option not supported with PREEMPT_RT\n");
		return 1;
	}
	irqfixup = 2;
	pr_warn("Misrouted IRQ fixup and polling support enabled\n");
	pr_warn("This may significantly impact system performance\n");
	return 1;
}
__setup("irqpoll", irqpoll_setup);
