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
#include <linux/rculist.h>
#include <linux/hash.h>

#include "internals.h"

/*
 * lockdep: we want to handle all irq_desc locks as a single lock-class:
 */
struct lock_class_key irq_desc_lock_class;

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

#ifdef CONFIG_SPARSE_IRQ
static struct irq_desc irq_desc_init = {
	.irq	    = -1,
	.status	    = IRQ_DISABLED,
	.chip	    = &no_irq_chip,
	.handle_irq = handle_bad_irq,
	.depth      = 1,
	.lock       = __SPIN_LOCK_UNLOCKED(irq_desc_init.lock),
#ifdef CONFIG_SMP
	.affinity   = CPU_MASK_ALL
#endif
};

void init_kstat_irqs(struct irq_desc *desc, int cpu, int nr)
{
	unsigned long bytes;
	char *ptr;
	int node;

	/* Compute how many bytes we need per irq and allocate them */
	bytes = nr * sizeof(unsigned int);

	node = cpu_to_node(cpu);
	ptr = kzalloc_node(bytes, GFP_ATOMIC, node);
	printk(KERN_DEBUG "  alloc kstat_irqs on cpu %d node %d\n", cpu, node);

	if (ptr)
		desc->kstat_irqs = (unsigned int *)ptr;
}

static void init_one_irq_desc(int irq, struct irq_desc *desc, int cpu)
{
	memcpy(desc, &irq_desc_init, sizeof(struct irq_desc));

	spin_lock_init(&desc->lock);
	desc->irq = irq;
#ifdef CONFIG_SMP
	desc->cpu = cpu;
#endif
	lockdep_set_class(&desc->lock, &irq_desc_lock_class);
	init_kstat_irqs(desc, cpu, nr_cpu_ids);
	if (!desc->kstat_irqs) {
		printk(KERN_ERR "can not alloc kstat_irqs\n");
		BUG_ON(1);
	}
	arch_init_chip_data(desc, cpu);
}

/*
 * Protect the sparse_irqs:
 */
DEFINE_SPINLOCK(sparse_irq_lock);

struct irq_desc *irq_desc_ptrs[NR_IRQS] __read_mostly;

static struct irq_desc irq_desc_legacy[NR_IRQS_LEGACY] __cacheline_aligned_in_smp = {
	[0 ... NR_IRQS_LEGACY-1] = {
		.irq	    = -1,
		.status	    = IRQ_DISABLED,
		.chip	    = &no_irq_chip,
		.handle_irq = handle_bad_irq,
		.depth	    = 1,
		.lock	    = __SPIN_LOCK_UNLOCKED(irq_desc_init.lock),
#ifdef CONFIG_SMP
		.affinity   = CPU_MASK_ALL
#endif
	}
};

/* FIXME: use bootmem alloc ...*/
static unsigned int kstat_irqs_legacy[NR_IRQS_LEGACY][NR_CPUS];

int __init early_irq_init(void)
{
	struct irq_desc *desc;
	int legacy_count;
	int i;

	desc = irq_desc_legacy;
	legacy_count = ARRAY_SIZE(irq_desc_legacy);

	for (i = 0; i < legacy_count; i++) {
		desc[i].irq = i;
		desc[i].kstat_irqs = kstat_irqs_legacy[i];
		lockdep_set_class(&desc[i].lock, &irq_desc_lock_class);

		irq_desc_ptrs[i] = desc + i;
	}

	for (i = legacy_count; i < NR_IRQS; i++)
		irq_desc_ptrs[i] = NULL;

	return arch_early_irq_init();
}

struct irq_desc *irq_to_desc(unsigned int irq)
{
	return (irq < NR_IRQS) ? irq_desc_ptrs[irq] : NULL;
}

struct irq_desc *irq_to_desc_alloc_cpu(unsigned int irq, int cpu)
{
	struct irq_desc *desc;
	unsigned long flags;
	int node;

	if (irq >= NR_IRQS) {
		printk(KERN_WARNING "irq >= NR_IRQS in irq_to_desc_alloc: %d %d\n",
				irq, NR_IRQS);
		WARN_ON(1);
		return NULL;
	}

	desc = irq_desc_ptrs[irq];
	if (desc)
		return desc;

	spin_lock_irqsave(&sparse_irq_lock, flags);

	/* We have to check it to avoid races with another CPU */
	desc = irq_desc_ptrs[irq];
	if (desc)
		goto out_unlock;

	node = cpu_to_node(cpu);
	desc = kzalloc_node(sizeof(*desc), GFP_ATOMIC, node);
	printk(KERN_DEBUG "  alloc irq_desc for %d on cpu %d node %d\n",
		 irq, cpu, node);
	if (!desc) {
		printk(KERN_ERR "can not alloc irq_desc\n");
		BUG_ON(1);
	}
	init_one_irq_desc(irq, desc, cpu);

	irq_desc_ptrs[irq] = desc;

out_unlock:
	spin_unlock_irqrestore(&sparse_irq_lock, flags);

	return desc;
}

#else /* !CONFIG_SPARSE_IRQ */

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

int __init early_irq_init(void)
{
	struct irq_desc *desc;
	int count;
	int i;

	desc = irq_desc;
	count = ARRAY_SIZE(irq_desc);

	for (i = 0; i < count; i++)
		desc[i].irq = i;

	return arch_early_irq_init();
}

struct irq_desc *irq_to_desc(unsigned int irq)
{
	return (irq < NR_IRQS) ? irq_desc + irq : NULL;
}

struct irq_desc *irq_to_desc_alloc_cpu(unsigned int irq, int cpu)
{
	return irq_to_desc(irq);
}
#endif /* !CONFIG_SPARSE_IRQ */

/*
 * What should we do if we get a hw irq event on an illegal vector?
 * Each architecture has to answer this themself.
 */
static void ack_bad(unsigned int irq)
{
	struct irq_desc *desc = irq_to_desc(irq);

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

	kstat_incr_irqs_this_cpu(irq, desc);

	if (CHECK_IRQ_PER_CPU(desc->status)) {
		irqreturn_t action_ret;

		/*
		 * No locking required for CPU-local interrupts:
		 */
		if (desc->chip->ack) {
			desc->chip->ack(irq);
			/* get new one */
			desc = irq_remap_to_desc(irq, desc);
		}
		if (likely(!(desc->status & IRQ_DISABLED))) {
			action_ret = handle_IRQ_event(irq, desc->action);
			if (!noirqdebug)
				note_interrupt(irq, desc, action_ret);
		}
		desc->chip->end(irq);
		return 1;
	}

	spin_lock(&desc->lock);
	if (desc->chip->ack) {
		desc->chip->ack(irq);
		desc = irq_remap_to_desc(irq, desc);
	}
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

void early_init_irq_lock_class(void)
{
	struct irq_desc *desc;
	int i;

	for_each_irq_desc(i, desc) {
		lockdep_set_class(&desc->lock, &irq_desc_lock_class);
	}
}

#ifdef CONFIG_SPARSE_IRQ
unsigned int kstat_irqs_cpu(unsigned int irq, int cpu)
{
	struct irq_desc *desc = irq_to_desc(irq);
	return desc ? desc->kstat_irqs[cpu] : 0;
}
#endif
EXPORT_SYMBOL(kstat_irqs_cpu);

