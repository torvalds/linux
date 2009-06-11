/*
 * linux/kernel/irq/manage.c
 *
 * Copyright (C) 1992, 1998-2006 Linus Torvalds, Ingo Molnar
 * Copyright (C) 2005-2006 Thomas Gleixner
 *
 * This file contains driver APIs to the irq subsystem.
 */

#include <linux/irq.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/random.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/sched.h>

#include "internals.h"

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
	struct irq_desc *desc = irq_to_desc(irq);
	unsigned int status;

	if (!desc)
		return;

	do {
		unsigned long flags;

		/*
		 * Wait until we're out of the critical section.  This might
		 * give the wrong answer due to the lack of memory barriers.
		 */
		while (desc->status & IRQ_INPROGRESS)
			cpu_relax();

		/* Ok, that indicated we're done: double-check carefully. */
		spin_lock_irqsave(&desc->lock, flags);
		status = desc->status;
		spin_unlock_irqrestore(&desc->lock, flags);

		/* Oops, that failed? */
	} while (status & IRQ_INPROGRESS);

	/*
	 * We made sure that no hardirq handler is running. Now verify
	 * that no threaded handlers are active.
	 */
	wait_event(desc->wait_for_threads, !atomic_read(&desc->threads_active));
}
EXPORT_SYMBOL(synchronize_irq);

#ifdef CONFIG_SMP
cpumask_var_t irq_default_affinity;

/**
 *	irq_can_set_affinity - Check if the affinity of a given irq can be set
 *	@irq:		Interrupt to check
 *
 */
int irq_can_set_affinity(unsigned int irq)
{
	struct irq_desc *desc = irq_to_desc(irq);

	if (CHECK_IRQ_PER_CPU(desc->status) || !desc->chip ||
	    !desc->chip->set_affinity)
		return 0;

	return 1;
}

void
irq_set_thread_affinity(struct irq_desc *desc, const struct cpumask *cpumask)
{
	struct irqaction *action = desc->action;

	while (action) {
		if (action->thread)
			set_cpus_allowed_ptr(action->thread, cpumask);
		action = action->next;
	}
}

/**
 *	irq_set_affinity - Set the irq affinity of a given irq
 *	@irq:		Interrupt to set affinity
 *	@cpumask:	cpumask
 *
 */
int irq_set_affinity(unsigned int irq, const struct cpumask *cpumask)
{
	struct irq_desc *desc = irq_to_desc(irq);
	unsigned long flags;

	if (!desc->chip->set_affinity)
		return -EINVAL;

	spin_lock_irqsave(&desc->lock, flags);

#ifdef CONFIG_GENERIC_PENDING_IRQ
	if (desc->status & IRQ_MOVE_PCNTXT) {
		if (!desc->chip->set_affinity(irq, cpumask)) {
			cpumask_copy(desc->affinity, cpumask);
			irq_set_thread_affinity(desc, cpumask);
		}
	}
	else {
		desc->status |= IRQ_MOVE_PENDING;
		cpumask_copy(desc->pending_mask, cpumask);
	}
#else
	if (!desc->chip->set_affinity(irq, cpumask)) {
		cpumask_copy(desc->affinity, cpumask);
		irq_set_thread_affinity(desc, cpumask);
	}
#endif
	desc->status |= IRQ_AFFINITY_SET;
	spin_unlock_irqrestore(&desc->lock, flags);
	return 0;
}

#ifndef CONFIG_AUTO_IRQ_AFFINITY
/*
 * Generic version of the affinity autoselector.
 */
static int setup_affinity(unsigned int irq, struct irq_desc *desc)
{
	if (!irq_can_set_affinity(irq))
		return 0;

	/*
	 * Preserve an userspace affinity setup, but make sure that
	 * one of the targets is online.
	 */
	if (desc->status & (IRQ_AFFINITY_SET | IRQ_NO_BALANCING)) {
		if (cpumask_any_and(desc->affinity, cpu_online_mask)
		    < nr_cpu_ids)
			goto set_affinity;
		else
			desc->status &= ~IRQ_AFFINITY_SET;
	}

	cpumask_and(desc->affinity, cpu_online_mask, irq_default_affinity);
set_affinity:
	desc->chip->set_affinity(irq, desc->affinity);

	return 0;
}
#else
static inline int setup_affinity(unsigned int irq, struct irq_desc *d)
{
	return irq_select_affinity(irq);
}
#endif

/*
 * Called when affinity is set via /proc/irq
 */
int irq_select_affinity_usr(unsigned int irq)
{
	struct irq_desc *desc = irq_to_desc(irq);
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&desc->lock, flags);
	ret = setup_affinity(irq, desc);
	if (!ret)
		irq_set_thread_affinity(desc, desc->affinity);
	spin_unlock_irqrestore(&desc->lock, flags);

	return ret;
}

#else
static inline int setup_affinity(unsigned int irq, struct irq_desc *desc)
{
	return 0;
}
#endif

void __disable_irq(struct irq_desc *desc, unsigned int irq, bool suspend)
{
	if (suspend) {
		if (!desc->action || (desc->action->flags & IRQF_TIMER))
			return;
		desc->status |= IRQ_SUSPENDED;
	}

	if (!desc->depth++) {
		desc->status |= IRQ_DISABLED;
		desc->chip->disable(irq);
	}
}

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
	struct irq_desc *desc = irq_to_desc(irq);
	unsigned long flags;

	if (!desc)
		return;

	spin_lock_irqsave(&desc->lock, flags);
	__disable_irq(desc, irq, false);
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
	struct irq_desc *desc = irq_to_desc(irq);

	if (!desc)
		return;

	disable_irq_nosync(irq);
	if (desc->action)
		synchronize_irq(irq);
}
EXPORT_SYMBOL(disable_irq);

void __enable_irq(struct irq_desc *desc, unsigned int irq, bool resume)
{
	if (resume)
		desc->status &= ~IRQ_SUSPENDED;

	switch (desc->depth) {
	case 0:
 err_out:
		WARN(1, KERN_WARNING "Unbalanced enable for IRQ %d\n", irq);
		break;
	case 1: {
		unsigned int status = desc->status & ~IRQ_DISABLED;

		if (desc->status & IRQ_SUSPENDED)
			goto err_out;
		/* Prevent probing on this irq: */
		desc->status = status | IRQ_NOPROBE;
		check_irq_resend(desc, irq);
		/* fall-through */
	}
	default:
		desc->depth--;
	}
}

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
	struct irq_desc *desc = irq_to_desc(irq);
	unsigned long flags;

	if (!desc)
		return;

	spin_lock_irqsave(&desc->lock, flags);
	__enable_irq(desc, irq, false);
	spin_unlock_irqrestore(&desc->lock, flags);
}
EXPORT_SYMBOL(enable_irq);

static int set_irq_wake_real(unsigned int irq, unsigned int on)
{
	struct irq_desc *desc = irq_to_desc(irq);
	int ret = -ENXIO;

	if (desc->chip->set_wake)
		ret = desc->chip->set_wake(irq, on);

	return ret;
}

/**
 *	set_irq_wake - control irq power management wakeup
 *	@irq:	interrupt to control
 *	@on:	enable/disable power management wakeup
 *
 *	Enable/disable power management wakeup mode, which is
 *	disabled by default.  Enables and disables must match,
 *	just as they match for non-wakeup mode support.
 *
 *	Wakeup mode lets this IRQ wake the system from sleep
 *	states like "suspend to RAM".
 */
int set_irq_wake(unsigned int irq, unsigned int on)
{
	struct irq_desc *desc = irq_to_desc(irq);
	unsigned long flags;
	int ret = 0;

	/* wakeup-capable irqs can be shared between drivers that
	 * don't need to have the same sleep mode behaviors.
	 */
	spin_lock_irqsave(&desc->lock, flags);
	if (on) {
		if (desc->wake_depth++ == 0) {
			ret = set_irq_wake_real(irq, on);
			if (ret)
				desc->wake_depth = 0;
			else
				desc->status |= IRQ_WAKEUP;
		}
	} else {
		if (desc->wake_depth == 0) {
			WARN(1, "Unbalanced IRQ %d wake disable\n", irq);
		} else if (--desc->wake_depth == 0) {
			ret = set_irq_wake_real(irq, on);
			if (ret)
				desc->wake_depth = 1;
			else
				desc->status &= ~IRQ_WAKEUP;
		}
	}

	spin_unlock_irqrestore(&desc->lock, flags);
	return ret;
}
EXPORT_SYMBOL(set_irq_wake);

/*
 * Internal function that tells the architecture code whether a
 * particular irq has been exclusively allocated or is available
 * for driver use.
 */
int can_request_irq(unsigned int irq, unsigned long irqflags)
{
	struct irq_desc *desc = irq_to_desc(irq);
	struct irqaction *action;

	if (!desc)
		return 0;

	if (desc->status & IRQ_NOREQUEST)
		return 0;

	action = desc->action;
	if (action)
		if (irqflags & action->flags & IRQF_SHARED)
			action = NULL;

	return !action;
}

void compat_irq_chip_set_default_handler(struct irq_desc *desc)
{
	/*
	 * If the architecture still has not overriden
	 * the flow handler then zap the default. This
	 * should catch incorrect flow-type setting.
	 */
	if (desc->handle_irq == &handle_bad_irq)
		desc->handle_irq = NULL;
}

int __irq_set_trigger(struct irq_desc *desc, unsigned int irq,
		unsigned long flags)
{
	int ret;
	struct irq_chip *chip = desc->chip;

	if (!chip || !chip->set_type) {
		/*
		 * IRQF_TRIGGER_* but the PIC does not support multiple
		 * flow-types?
		 */
		pr_debug("No set_type function for IRQ %d (%s)\n", irq,
				chip ? (chip->name ? : "unknown") : "unknown");
		return 0;
	}

	/* caller masked out all except trigger mode flags */
	ret = chip->set_type(irq, flags);

	if (ret)
		pr_err("setting trigger mode %d for irq %u failed (%pF)\n",
				(int)flags, irq, chip->set_type);
	else {
		if (flags & (IRQ_TYPE_LEVEL_LOW | IRQ_TYPE_LEVEL_HIGH))
			flags |= IRQ_LEVEL;
		/* note that IRQF_TRIGGER_MASK == IRQ_TYPE_SENSE_MASK */
		desc->status &= ~(IRQ_LEVEL | IRQ_TYPE_SENSE_MASK);
		desc->status |= flags;
	}

	return ret;
}

static int irq_wait_for_interrupt(struct irqaction *action)
{
	while (!kthread_should_stop()) {
		set_current_state(TASK_INTERRUPTIBLE);

		if (test_and_clear_bit(IRQTF_RUNTHREAD,
				       &action->thread_flags)) {
			__set_current_state(TASK_RUNNING);
			return 0;
		}
		schedule();
	}
	return -1;
}

/*
 * Interrupt handler thread
 */
static int irq_thread(void *data)
{
	struct sched_param param = { .sched_priority = MAX_USER_RT_PRIO/2, };
	struct irqaction *action = data;
	struct irq_desc *desc = irq_to_desc(action->irq);
	int wake;

	sched_setscheduler(current, SCHED_FIFO, &param);
	current->irqaction = action;

	while (!irq_wait_for_interrupt(action)) {

		atomic_inc(&desc->threads_active);

		spin_lock_irq(&desc->lock);
		if (unlikely(desc->status & IRQ_DISABLED)) {
			/*
			 * CHECKME: We might need a dedicated
			 * IRQ_THREAD_PENDING flag here, which
			 * retriggers the thread in check_irq_resend()
			 * but AFAICT IRQ_PENDING should be fine as it
			 * retriggers the interrupt itself --- tglx
			 */
			desc->status |= IRQ_PENDING;
			spin_unlock_irq(&desc->lock);
		} else {
			spin_unlock_irq(&desc->lock);

			action->thread_fn(action->irq, action->dev_id);
		}

		wake = atomic_dec_and_test(&desc->threads_active);

		if (wake && waitqueue_active(&desc->wait_for_threads))
			wake_up(&desc->wait_for_threads);
	}

	/*
	 * Clear irqaction. Otherwise exit_irq_thread() would make
	 * fuzz about an active irq thread going into nirvana.
	 */
	current->irqaction = NULL;
	return 0;
}

/*
 * Called from do_exit()
 */
void exit_irq_thread(void)
{
	struct task_struct *tsk = current;

	if (!tsk->irqaction)
		return;

	printk(KERN_ERR
	       "exiting task \"%s\" (%d) is an active IRQ thread (irq %d)\n",
	       tsk->comm ? tsk->comm : "", tsk->pid, tsk->irqaction->irq);

	/*
	 * Set the THREAD DIED flag to prevent further wakeups of the
	 * soon to be gone threaded handler.
	 */
	set_bit(IRQTF_DIED, &tsk->irqaction->flags);
}

/*
 * Internal function to register an irqaction - typically used to
 * allocate special interrupts that are part of the architecture.
 */
static int
__setup_irq(unsigned int irq, struct irq_desc *desc, struct irqaction *new)
{
	struct irqaction *old, **old_ptr;
	const char *old_name = NULL;
	unsigned long flags;
	int shared = 0;
	int ret;

	if (!desc)
		return -EINVAL;

	if (desc->chip == &no_irq_chip)
		return -ENOSYS;
	/*
	 * Some drivers like serial.c use request_irq() heavily,
	 * so we have to be careful not to interfere with a
	 * running system.
	 */
	if (new->flags & IRQF_SAMPLE_RANDOM) {
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
	 * Threaded handler ?
	 */
	if (new->thread_fn) {
		struct task_struct *t;

		t = kthread_create(irq_thread, new, "irq/%d-%s", irq,
				   new->name);
		if (IS_ERR(t))
			return PTR_ERR(t);
		/*
		 * We keep the reference to the task struct even if
		 * the thread dies to avoid that the interrupt code
		 * references an already freed task_struct.
		 */
		get_task_struct(t);
		new->thread = t;
		wake_up_process(t);
	}

	/*
	 * The following block of code has to be executed atomically
	 */
	spin_lock_irqsave(&desc->lock, flags);
	old_ptr = &desc->action;
	old = *old_ptr;
	if (old) {
		/*
		 * Can't share interrupts unless both agree to and are
		 * the same type (level, edge, polarity). So both flag
		 * fields must have IRQF_SHARED set and the bits which
		 * set the trigger type must match.
		 */
		if (!((old->flags & new->flags) & IRQF_SHARED) ||
		    ((old->flags ^ new->flags) & IRQF_TRIGGER_MASK)) {
			old_name = old->name;
			goto mismatch;
		}

#if defined(CONFIG_IRQ_PER_CPU)
		/* All handlers must agree on per-cpuness */
		if ((old->flags & IRQF_PERCPU) !=
		    (new->flags & IRQF_PERCPU))
			goto mismatch;
#endif

		/* add new interrupt at end of irq queue */
		do {
			old_ptr = &old->next;
			old = *old_ptr;
		} while (old);
		shared = 1;
	}

	if (!shared) {
		irq_chip_set_defaults(desc->chip);

		init_waitqueue_head(&desc->wait_for_threads);

		/* Setup the type (level, edge polarity) if configured: */
		if (new->flags & IRQF_TRIGGER_MASK) {
			ret = __irq_set_trigger(desc, irq,
					new->flags & IRQF_TRIGGER_MASK);

			if (ret)
				goto out_thread;
		} else
			compat_irq_chip_set_default_handler(desc);
#if defined(CONFIG_IRQ_PER_CPU)
		if (new->flags & IRQF_PERCPU)
			desc->status |= IRQ_PER_CPU;
#endif

		desc->status &= ~(IRQ_AUTODETECT | IRQ_WAITING |
				  IRQ_INPROGRESS | IRQ_SPURIOUS_DISABLED);

		if (!(desc->status & IRQ_NOAUTOEN)) {
			desc->depth = 0;
			desc->status &= ~IRQ_DISABLED;
			desc->chip->startup(irq);
		} else
			/* Undo nested disables: */
			desc->depth = 1;

		/* Exclude IRQ from balancing if requested */
		if (new->flags & IRQF_NOBALANCING)
			desc->status |= IRQ_NO_BALANCING;

		/* Set default affinity mask once everything is setup */
		setup_affinity(irq, desc);

	} else if ((new->flags & IRQF_TRIGGER_MASK)
			&& (new->flags & IRQF_TRIGGER_MASK)
				!= (desc->status & IRQ_TYPE_SENSE_MASK)) {
		/* hope the handler works with the actual trigger mode... */
		pr_warning("IRQ %d uses trigger mode %d; requested %d\n",
				irq, (int)(desc->status & IRQ_TYPE_SENSE_MASK),
				(int)(new->flags & IRQF_TRIGGER_MASK));
	}

	*old_ptr = new;

	/* Reset broken irq detection when installing new handler */
	desc->irq_count = 0;
	desc->irqs_unhandled = 0;

	/*
	 * Check whether we disabled the irq via the spurious handler
	 * before. Reenable it and give it another chance.
	 */
	if (shared && (desc->status & IRQ_SPURIOUS_DISABLED)) {
		desc->status &= ~IRQ_SPURIOUS_DISABLED;
		__enable_irq(desc, irq, false);
	}

	spin_unlock_irqrestore(&desc->lock, flags);

	new->irq = irq;
	register_irq_proc(irq, desc);
	new->dir = NULL;
	register_handler_proc(irq, new);

	return 0;

mismatch:
#ifdef CONFIG_DEBUG_SHIRQ
	if (!(new->flags & IRQF_PROBE_SHARED)) {
		printk(KERN_ERR "IRQ handler type mismatch for IRQ %d\n", irq);
		if (old_name)
			printk(KERN_ERR "current handler: %s\n", old_name);
		dump_stack();
	}
#endif
	ret = -EBUSY;

out_thread:
	spin_unlock_irqrestore(&desc->lock, flags);
	if (new->thread) {
		struct task_struct *t = new->thread;

		new->thread = NULL;
		if (likely(!test_bit(IRQTF_DIED, &new->thread_flags)))
			kthread_stop(t);
		put_task_struct(t);
	}
	return ret;
}

/**
 *	setup_irq - setup an interrupt
 *	@irq: Interrupt line to setup
 *	@act: irqaction for the interrupt
 *
 * Used to statically setup interrupts in the early boot process.
 */
int setup_irq(unsigned int irq, struct irqaction *act)
{
	struct irq_desc *desc = irq_to_desc(irq);

	return __setup_irq(irq, desc, act);
}
EXPORT_SYMBOL_GPL(setup_irq);

 /*
 * Internal function to unregister an irqaction - used to free
 * regular and special interrupts that are part of the architecture.
 */
static struct irqaction *__free_irq(unsigned int irq, void *dev_id)
{
	struct irq_desc *desc = irq_to_desc(irq);
	struct irqaction *action, **action_ptr;
	struct task_struct *irqthread;
	unsigned long flags;

	WARN(in_interrupt(), "Trying to free IRQ %d from IRQ context!\n", irq);

	if (!desc)
		return NULL;

	spin_lock_irqsave(&desc->lock, flags);

	/*
	 * There can be multiple actions per IRQ descriptor, find the right
	 * one based on the dev_id:
	 */
	action_ptr = &desc->action;
	for (;;) {
		action = *action_ptr;

		if (!action) {
			WARN(1, "Trying to free already-free IRQ %d\n", irq);
			spin_unlock_irqrestore(&desc->lock, flags);

			return NULL;
		}

		if (action->dev_id == dev_id)
			break;
		action_ptr = &action->next;
	}

	/* Found it - now remove it from the list of entries: */
	*action_ptr = action->next;

	/* Currently used only by UML, might disappear one day: */
#ifdef CONFIG_IRQ_RELEASE_METHOD
	if (desc->chip->release)
		desc->chip->release(irq, dev_id);
#endif

	/* If this was the last handler, shut down the IRQ line: */
	if (!desc->action) {
		desc->status |= IRQ_DISABLED;
		if (desc->chip->shutdown)
			desc->chip->shutdown(irq);
		else
			desc->chip->disable(irq);
	}

	irqthread = action->thread;
	action->thread = NULL;

	spin_unlock_irqrestore(&desc->lock, flags);

	unregister_handler_proc(irq, action);

	/* Make sure it's not being used on another CPU: */
	synchronize_irq(irq);

	if (irqthread) {
		if (!test_bit(IRQTF_DIED, &action->thread_flags))
			kthread_stop(irqthread);
		put_task_struct(irqthread);
	}

#ifdef CONFIG_DEBUG_SHIRQ
	/*
	 * It's a shared IRQ -- the driver ought to be prepared for an IRQ
	 * event to happen even now it's being freed, so let's make sure that
	 * is so by doing an extra call to the handler ....
	 *
	 * ( We do this after actually deregistering it, to make sure that a
	 *   'real' IRQ doesn't run in * parallel with our fake. )
	 */
	if (action->flags & IRQF_SHARED) {
		local_irq_save(flags);
		action->handler(irq, dev_id);
		local_irq_restore(flags);
	}
#endif
	return action;
}

/**
 *	remove_irq - free an interrupt
 *	@irq: Interrupt line to free
 *	@act: irqaction for the interrupt
 *
 * Used to remove interrupts statically setup by the early boot process.
 */
void remove_irq(unsigned int irq, struct irqaction *act)
{
	__free_irq(irq, act->dev_id);
}
EXPORT_SYMBOL_GPL(remove_irq);

/**
 *	free_irq - free an interrupt allocated with request_irq
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
	kfree(__free_irq(irq, dev_id));
}
EXPORT_SYMBOL(free_irq);

/**
 *	request_threaded_irq - allocate an interrupt line
 *	@irq: Interrupt line to allocate
 *	@handler: Function to be called when the IRQ occurs.
 *		  Primary handler for threaded interrupts
 *	@thread_fn: Function called from the irq handler thread
 *		    If NULL, no irq thread is created
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
 *	If you want to set up a threaded irq handler for your device
 *	then you need to supply @handler and @thread_fn. @handler ist
 *	still called in hard interrupt context and has to check
 *	whether the interrupt originates from the device. If yes it
 *	needs to disable the interrupt on the device and return
 *	IRQ_THREAD_WAKE which will wake up the handler thread and run
 *	@thread_fn. This split handler design is necessary to support
 *	shared interrupts.
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
 *	IRQF_SHARED		Interrupt is shared
 *	IRQF_DISABLED	Disable local interrupts while processing
 *	IRQF_SAMPLE_RANDOM	The interrupt can be used for entropy
 *	IRQF_TRIGGER_*		Specify active edge(s) or level
 *
 */
int request_threaded_irq(unsigned int irq, irq_handler_t handler,
			 irq_handler_t thread_fn, unsigned long irqflags,
			 const char *devname, void *dev_id)
{
	struct irqaction *action;
	struct irq_desc *desc;
	int retval;

	/*
	 * handle_IRQ_event() always ignores IRQF_DISABLED except for
	 * the _first_ irqaction (sigh).  That can cause oopsing, but
	 * the behavior is classified as "will not fix" so we need to
	 * start nudging drivers away from using that idiom.
	 */
	if ((irqflags & (IRQF_SHARED|IRQF_DISABLED)) ==
					(IRQF_SHARED|IRQF_DISABLED)) {
		pr_warning(
		  "IRQ %d/%s: IRQF_DISABLED is not guaranteed on shared IRQs\n",
			irq, devname);
	}

#ifdef CONFIG_LOCKDEP
	/*
	 * Lockdep wants atomic interrupt handlers:
	 */
	irqflags |= IRQF_DISABLED;
#endif
	/*
	 * Sanity-check: shared interrupts must pass in a real dev-ID,
	 * otherwise we'll have trouble later trying to figure out
	 * which interrupt is which (messes up the interrupt freeing
	 * logic etc).
	 */
	if ((irqflags & IRQF_SHARED) && !dev_id)
		return -EINVAL;

	desc = irq_to_desc(irq);
	if (!desc)
		return -EINVAL;

	if (desc->status & IRQ_NOREQUEST)
		return -EINVAL;
	if (!handler)
		return -EINVAL;

	action = kzalloc(sizeof(struct irqaction), GFP_KERNEL);
	if (!action)
		return -ENOMEM;

	action->handler = handler;
	action->thread_fn = thread_fn;
	action->flags = irqflags;
	action->name = devname;
	action->dev_id = dev_id;

	retval = __setup_irq(irq, desc, action);
	if (retval)
		kfree(action);

#ifdef CONFIG_DEBUG_SHIRQ
	if (irqflags & IRQF_SHARED) {
		/*
		 * It's a shared IRQ -- the driver ought to be prepared for it
		 * to happen immediately, so let's make sure....
		 * We disable the irq to make sure that a 'real' IRQ doesn't
		 * run in parallel with our fake.
		 */
		unsigned long flags;

		disable_irq(irq);
		local_irq_save(flags);

		handler(irq, dev_id);

		local_irq_restore(flags);
		enable_irq(irq);
	}
#endif
	return retval;
}
EXPORT_SYMBOL(request_threaded_irq);
