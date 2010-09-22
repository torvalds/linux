/*
 * Copyright (C) 1992, 1998-2006 Linus Torvalds, Ingo Molnar
 * Copyright (C) 2005-2006, Thomas Gleixner, Russell King
 *
 * This file contains the interrupt descriptor management code
 *
 * Detailed information is available in Documentation/DocBook/genericirq
 *
 */
#include <linux/irq.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>
#include <linux/radix-tree.h>

#include "internals.h"

/*
 * lockdep: we want to handle all irq_desc locks as a single lock-class:
 */
struct lock_class_key irq_desc_lock_class;

#if defined(CONFIG_SMP) && defined(CONFIG_GENERIC_HARDIRQS)
static void __init init_irq_default_affinity(void)
{
	alloc_cpumask_var(&irq_default_affinity, GFP_NOWAIT);
	cpumask_setall(irq_default_affinity);
}
#else
static void __init init_irq_default_affinity(void)
{
}
#endif

int nr_irqs = NR_IRQS;
EXPORT_SYMBOL_GPL(nr_irqs);

#ifdef CONFIG_SPARSE_IRQ

static struct irq_desc irq_desc_init = {
	.status		= IRQ_DISABLED,
	.handle_irq	= handle_bad_irq,
	.depth		= 1,
	.lock		= __RAW_SPIN_LOCK_UNLOCKED(irq_desc_init.lock),
};

void __ref init_kstat_irqs(struct irq_desc *desc, int node, int nr)
{
	void *ptr;

	ptr = kzalloc_node(nr * sizeof(*desc->kstat_irqs),
			   GFP_ATOMIC, node);

	/*
	 * don't overwite if can not get new one
	 * init_copy_kstat_irqs() could still use old one
	 */
	if (ptr) {
		printk(KERN_DEBUG "  alloc kstat_irqs on node %d\n", node);
		desc->kstat_irqs = ptr;
	}
}

static void init_one_irq_desc(int irq, struct irq_desc *desc, int node)
{
	memcpy(desc, &irq_desc_init, sizeof(struct irq_desc));

	raw_spin_lock_init(&desc->lock);
	desc->irq_data.irq = irq;
#ifdef CONFIG_SMP
	desc->irq_data.node = node;
#endif
	lockdep_set_class(&desc->lock, &irq_desc_lock_class);
	init_kstat_irqs(desc, node, nr_cpu_ids);
	if (!desc->kstat_irqs) {
		printk(KERN_ERR "can not alloc kstat_irqs\n");
		BUG_ON(1);
	}
	if (!alloc_desc_masks(desc, node, false)) {
		printk(KERN_ERR "can not alloc irq_desc cpumasks\n");
		BUG_ON(1);
	}
	init_desc_masks(desc);
	arch_init_chip_data(desc, node);
}

/*
 * Protect the sparse_irqs:
 */
DEFINE_RAW_SPINLOCK(sparse_irq_lock);

static RADIX_TREE(irq_desc_tree, GFP_ATOMIC);

static void set_irq_desc(unsigned int irq, struct irq_desc *desc)
{
	radix_tree_insert(&irq_desc_tree, irq, desc);
}

struct irq_desc *irq_to_desc(unsigned int irq)
{
	return radix_tree_lookup(&irq_desc_tree, irq);
}

void replace_irq_desc(unsigned int irq, struct irq_desc *desc)
{
	void **ptr;

	ptr = radix_tree_lookup_slot(&irq_desc_tree, irq);
	if (ptr)
		radix_tree_replace_slot(ptr, desc);
}

static struct irq_desc irq_desc_legacy[NR_IRQS_LEGACY] __cacheline_aligned_in_smp = {
	[0 ... NR_IRQS_LEGACY-1] = {
		.status		= IRQ_DISABLED,
		.handle_irq	= handle_bad_irq,
		.depth		= 1,
		.lock		= __RAW_SPIN_LOCK_UNLOCKED(irq_desc_init.lock),
	}
};

static unsigned int *kstat_irqs_legacy;

int __init early_irq_init(void)
{
	struct irq_desc *desc;
	int legacy_count;
	int node;
	int i;

	init_irq_default_affinity();

	 /* initialize nr_irqs based on nr_cpu_ids */
	arch_probe_nr_irqs();
	printk(KERN_INFO "NR_IRQS:%d nr_irqs:%d\n", NR_IRQS, nr_irqs);

	desc = irq_desc_legacy;
	legacy_count = ARRAY_SIZE(irq_desc_legacy);
	node = first_online_node;

	/* allocate based on nr_cpu_ids */
	kstat_irqs_legacy = kzalloc_node(NR_IRQS_LEGACY * nr_cpu_ids *
					  sizeof(int), GFP_NOWAIT, node);

	irq_desc_init.irq_data.chip = &no_irq_chip;

	for (i = 0; i < legacy_count; i++) {
		desc[i].irq_data.irq = i;
		desc[i].irq_data.chip = &no_irq_chip;
#ifdef CONFIG_SMP
		desc[i].irq_data.node = node;
#endif
		desc[i].kstat_irqs = kstat_irqs_legacy + i * nr_cpu_ids;
		lockdep_set_class(&desc[i].lock, &irq_desc_lock_class);
		alloc_desc_masks(&desc[i], node, true);
		init_desc_masks(&desc[i]);
		set_irq_desc(i, &desc[i]);
	}

	return arch_early_irq_init();
}

struct irq_desc * __ref irq_to_desc_alloc_node(unsigned int irq, int node)
{
	struct irq_desc *desc;
	unsigned long flags;

	if (irq >= nr_irqs) {
		WARN(1, "irq (%d) >= nr_irqs (%d) in irq_to_desc_alloc\n",
			irq, nr_irqs);
		return NULL;
	}

	desc = irq_to_desc(irq);
	if (desc)
		return desc;

	raw_spin_lock_irqsave(&sparse_irq_lock, flags);

	/* We have to check it to avoid races with another CPU */
	desc = irq_to_desc(irq);
	if (desc)
		goto out_unlock;

	desc = kzalloc_node(sizeof(*desc), GFP_ATOMIC, node);

	printk(KERN_DEBUG "  alloc irq_desc for %d on node %d\n", irq, node);
	if (!desc) {
		printk(KERN_ERR "can not alloc irq_desc\n");
		BUG_ON(1);
	}
	init_one_irq_desc(irq, desc, node);

	set_irq_desc(irq, desc);

out_unlock:
	raw_spin_unlock_irqrestore(&sparse_irq_lock, flags);

	return desc;
}

#else /* !CONFIG_SPARSE_IRQ */

struct irq_desc irq_desc[NR_IRQS] __cacheline_aligned_in_smp = {
	[0 ... NR_IRQS-1] = {
		.status		= IRQ_DISABLED,
		.handle_irq	= handle_bad_irq,
		.depth		= 1,
		.lock		= __RAW_SPIN_LOCK_UNLOCKED(irq_desc->lock),
	}
};

static unsigned int kstat_irqs_all[NR_IRQS][NR_CPUS];
int __init early_irq_init(void)
{
	struct irq_desc *desc;
	int count;
	int i;

	init_irq_default_affinity();

	printk(KERN_INFO "NR_IRQS:%d\n", NR_IRQS);

	desc = irq_desc;
	count = ARRAY_SIZE(irq_desc);

	for (i = 0; i < count; i++) {
		desc[i].irq_data.irq = i;
		desc[i].irq_data.chip = &no_irq_chip;
		alloc_desc_masks(&desc[i], 0, true);
		init_desc_masks(&desc[i]);
		desc[i].kstat_irqs = kstat_irqs_all[i];
		lockdep_set_class(&desc[i].lock, &irq_desc_lock_class);
	}
	return arch_early_irq_init();
}

struct irq_desc *irq_to_desc(unsigned int irq)
{
	return (irq < NR_IRQS) ? irq_desc + irq : NULL;
}

struct irq_desc *irq_to_desc_alloc_node(unsigned int irq, int node)
{
	return irq_to_desc(irq);
}
#endif /* !CONFIG_SPARSE_IRQ */

void clear_kstat_irqs(struct irq_desc *desc)
{
	memset(desc->kstat_irqs, 0, nr_cpu_ids * sizeof(*(desc->kstat_irqs)));
}

unsigned int kstat_irqs_cpu(unsigned int irq, int cpu)
{
	struct irq_desc *desc = irq_to_desc(irq);
	return desc ? desc->kstat_irqs[cpu] : 0;
}
EXPORT_SYMBOL(kstat_irqs_cpu);
