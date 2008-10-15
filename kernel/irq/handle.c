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
#include <linux/module.h>
#include <linux/random.h>
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>

#include "internals.h"

/*
 * lockdep: we want to handle all irq_desc locks as a single lock-class:
 */
static struct lock_class_key irq_desc_lock_class;

/**
 * handle_bad_irq - handle spurious and unhandled irqs
 * @irq:       the interrupt number
 * @desc:      description of the interrupt
 *
 * Handles spurious and unhandled IRQ's. It also prints a debugmessage.
 */
void
handle_bad_irq(unsigned int irq, struct irq_desc *desc)
{
	print_irq_desc(irq, desc);
#ifdef CONFIG_HAVE_DYN_ARRAY
	kstat_irqs_this_cpu(desc)++;
#else
	kstat_irqs_this_cpu(irq)++;
#endif
	ack_bad_irq(irq);
}

/*
 * Linux has a controller-independent interrupt architecture.
 * Every controller has a 'controller-template', that is used
 * by the main code to do the right thing. Each driver-visible
 * interrupt source is transparently wired to the appropriate
 * controller. Thus drivers need not be aware of the
 * interrupt-controller.
 *
 * The code is designed to be easily extended with new/different
 * interrupt controllers, without having to do assembly magic or
 * having to touch the generic code.
 *
 * Controller mappings for all interrupt sources:
 */
int nr_irqs = NR_IRQS;
EXPORT_SYMBOL_GPL(nr_irqs);

#ifdef CONFIG_HAVE_DYN_ARRAY
static struct irq_desc irq_desc_init = {
	.irq = -1U,
	.status = IRQ_DISABLED,
	.chip = &no_irq_chip,
	.handle_irq = handle_bad_irq,
	.depth = 1,
	.lock = __SPIN_LOCK_UNLOCKED(irq_desc_init.lock),
#ifdef CONFIG_SMP
	.affinity = CPU_MASK_ALL
#endif
};


static void init_one_irq_desc(struct irq_desc *desc)
{
	memcpy(desc, &irq_desc_init, sizeof(struct irq_desc));
	lockdep_set_class(&desc->lock, &irq_desc_lock_class);
}

extern int after_bootmem;
extern void *__alloc_bootmem_nopanic(unsigned long size,
			     unsigned long align,
			     unsigned long goal);

static void init_kstat_irqs(struct irq_desc *desc, int nr_desc, int nr)
{
	unsigned long bytes, total_bytes;
	char *ptr;
	int i;
	unsigned long phys;

	/* Compute how many bytes we need per irq and allocate them */
	bytes = nr * sizeof(unsigned int);
	total_bytes = bytes * nr_desc;
	if (after_bootmem)
		ptr = kzalloc(total_bytes, GFP_ATOMIC);
	else
		ptr = __alloc_bootmem_nopanic(total_bytes, PAGE_SIZE, 0);

	if (!ptr)
		panic(" can not allocate kstat_irqs\n");

	phys = __pa(ptr);
	printk(KERN_DEBUG "kstat_irqs ==> [%#lx - %#lx]\n", phys, phys + total_bytes);

	for (i = 0; i < nr_desc; i++) {
		desc[i].kstat_irqs = (unsigned int *)ptr;
		ptr += bytes;
	}
}

static void __init init_work(void *data)
{
	struct dyn_array *da = data;
	int i;
	struct  irq_desc *desc;

	desc = *da->name;

	for (i = 0; i < *da->nr; i++) {
		init_one_irq_desc(&desc[i]);
		desc[i].irq = i;
	}

	/* init kstat_irqs, nr_cpu_ids is ready already */
	init_kstat_irqs(desc, *da->nr, nr_cpu_ids);
}

struct irq_desc *irq_desc;
DEFINE_DYN_ARRAY(irq_desc, sizeof(struct irq_desc), nr_irqs, PAGE_SIZE, init_work);

#else

struct irq_desc irq_desc[NR_IRQS] __cacheline_aligned_in_smp = {
	[0 ... NR_IRQS-1] = {
		.status = IRQ_DISABLED,
		.chip = &no_irq_chip,
		.handle_irq = handle_bad_irq,
		.depth = 1,
		.lock = __SPIN_LOCK_UNLOCKED(irq_desc->lock),
#ifdef CONFIG_SMP
		.affinity = CPU_MASK_ALL
#endif
	}
};

#endif

/*
 * What should we do if we get a hw irq event on an illegal vector?
 * Each architecture has to answer this themself.
 */
static void ack_bad(unsigned int irq)
{
	struct irq_desc *desc;

	desc = irq_to_desc(irq);
	print_irq_desc(irq, desc);
	ack_bad_irq(irq);
}

/*
 * NOP functions
 */
static void noop(unsigned int irq)
{
}

static unsigned int noop_ret(unsigned int irq)
{
	return 0;
}

/*
 * Generic no controller implementation
 */
struct irq_chip no_irq_chip = {
	.name		= "none",
	.startup	= noop_ret,
	.shutdown	= noop,
	.enable		= noop,
	.disable	= noop,
	.ack		= ack_bad,
	.end		= noop,
};

/*
 * Generic dummy implementation which can be used for
 * real dumb interrupt sources
 */
struct irq_chip dummy_irq_chip = {
	.name		= "dummy",
	.startup	= noop_ret,
	.shutdown	= noop,
	.enable		= noop,
	.disable	= noop,
	.ack		= noop,
	.mask		= noop,
	.unmask		= noop,
	.end		= noop,
};

/*
 * Special, empty irq handler:
 */
irqreturn_t no_action(int cpl, void *dev_id)
{
	return IRQ_NONE;
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

	if (!(action->flags & IRQF_DISABLED))
		local_irq_enable_in_hardirq();

	do {
		ret = action->handler(irq, action->dev_id);
		if (ret == IRQ_HANDLED)
			status |= action->flags;
		retval |= ret;
		action = action->next;
	} while (action);

	if (status & IRQF_SAMPLE_RANDOM)
		add_interrupt_randomness(irq);
	local_irq_disable();

	return retval;
}

#ifndef CONFIG_GENERIC_HARDIRQS_NO__DO_IRQ
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

#ifdef CONFIG_HAVE_DYN_ARRAY
	kstat_irqs_this_cpu(desc)++;
#else
	kstat_irqs_this_cpu(irq)++;
#endif
	if (CHECK_IRQ_PER_CPU(desc->status)) {
		irqreturn_t action_ret;

		/*
		 * No locking required for CPU-local interrupts:
		 */
		if (desc->chip->ack)
			desc->chip->ack(irq);
		if (likely(!(desc->status & IRQ_DISABLED))) {
			action_ret = handle_IRQ_event(irq, desc->action);
			if (!noirqdebug)
				note_interrupt(irq, desc, action_ret);
		}
		desc->chip->end(irq);
		return 1;
	}

	spin_lock(&desc->lock);
	if (desc->chip->ack)
		desc->chip->ack(irq);
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

		spin_unlock(&desc->lock);

		action_ret = handle_IRQ_event(irq, action);
		if (!noirqdebug)
			note_interrupt(irq, desc, action_ret);

		spin_lock(&desc->lock);
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
	desc->chip->end(irq);
	spin_unlock(&desc->lock);

	return 1;
}
#endif


#ifdef CONFIG_TRACE_IRQFLAGS
void early_init_irq_lock_class(void)
{
#ifndef CONFIG_HAVE_DYN_ARRAY
	int i;

	for (i = 0; i < nr_irqs; i++)
		lockdep_set_class(&irq_desc[i].lock, &irq_desc_lock_class);
#endif
}
#endif

#ifdef CONFIG_HAVE_DYN_ARRAY
unsigned int kstat_irqs_cpu(unsigned int irq, int cpu)
{
	struct irq_desc *desc = irq_to_desc(irq);
	return desc->kstat_irqs[cpu];
}
#endif
EXPORT_SYMBOL(kstat_irqs_cpu);

