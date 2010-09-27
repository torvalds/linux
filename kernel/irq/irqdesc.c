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
#include <linux/bitmap.h>

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

#ifdef CONFIG_SMP
static int alloc_masks(struct irq_desc *desc, gfp_t gfp, int node)
{
	if (!zalloc_cpumask_var_node(&desc->irq_data.affinity, gfp, node))
		return -ENOMEM;

#ifdef CONFIG_GENERIC_PENDING_IRQ
	if (!zalloc_cpumask_var_node(&desc->pending_mask, gfp, node)) {
		free_cpumask_var(desc->irq_data.affinity);
		return -ENOMEM;
	}
#endif
	return 0;
}

static void desc_smp_init(struct irq_desc *desc, int node)
{
	desc->irq_data.node = node;
	cpumask_copy(desc->irq_data.affinity, irq_default_affinity);
}

#else
static inline int
alloc_masks(struct irq_desc *desc, gfp_t gfp, int node) { return 0; }
static inline void desc_smp_init(struct irq_desc *desc, int node) { }
#endif

static void desc_set_defaults(unsigned int irq, struct irq_desc *desc, int node)
{
	desc->irq_data.irq = irq;
	desc->irq_data.chip = &no_irq_chip;
	desc->irq_data.chip_data = NULL;
	desc->irq_data.handler_data = NULL;
	desc->irq_data.msi_desc = NULL;
	desc->status = IRQ_DEFAULT_INIT_FLAGS;
	desc->handle_irq = handle_bad_irq;
	desc->depth = 1;
	desc->name = NULL;
	memset(desc->kstat_irqs, 0, nr_cpu_ids * sizeof(*(desc->kstat_irqs)));
	desc_smp_init(desc, node);
}

int nr_irqs = NR_IRQS;
EXPORT_SYMBOL_GPL(nr_irqs);

DEFINE_RAW_SPINLOCK(sparse_irq_lock);
static DECLARE_BITMAP(allocated_irqs, NR_IRQS);

#ifdef CONFIG_SPARSE_IRQ

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

static RADIX_TREE(irq_desc_tree, GFP_ATOMIC);

static void irq_insert_desc(unsigned int irq, struct irq_desc *desc)
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

static void delete_irq_desc(unsigned int irq)
{
	radix_tree_delete(&irq_desc_tree, irq);
}

#ifdef CONFIG_SMP
static void free_masks(struct irq_desc *desc)
{
#ifdef CONFIG_GENERIC_PENDING_IRQ
	free_cpumask_var(desc->pending_mask);
#endif
	free_cpumask_var(desc->affinity);
}
#else
static inline void free_masks(struct irq_desc *desc) { }
#endif

static struct irq_desc *alloc_desc(int irq, int node)
{
	/* Temporary hack until we can switch to GFP_KERNEL */
	gfp_t gfp = gfp_allowed_mask == GFP_BOOT_MASK ? GFP_NOWAIT : GFP_ATOMIC;
	struct irq_desc *desc;

	desc = kzalloc_node(sizeof(*desc), gfp, node);
	if (!desc)
		return NULL;
	/* allocate based on nr_cpu_ids */
	desc->kstat_irqs = kzalloc_node(nr_cpu_ids * sizeof(*desc->kstat_irqs),
					 gfp, node);
	if (!desc->kstat_irqs)
		goto err_desc;

	if (alloc_masks(desc, gfp, node))
		goto err_kstat;

	raw_spin_lock_init(&desc->lock);
	lockdep_set_class(&desc->lock, &irq_desc_lock_class);

	desc_set_defaults(irq, desc, node);

	return desc;

err_kstat:
	kfree(desc->kstat_irqs);
err_desc:
	kfree(desc);
	return NULL;
}

static void free_desc(unsigned int irq)
{
	struct irq_desc *desc = irq_to_desc(irq);
	unsigned long flags;

	unregister_irq_proc(irq, desc);

	raw_spin_lock_irqsave(&sparse_irq_lock, flags);
	delete_irq_desc(irq);
	raw_spin_unlock_irqrestore(&sparse_irq_lock, flags);

	free_masks(desc);
	kfree(desc->kstat_irqs);
	kfree(desc);
}

static int alloc_descs(unsigned int start, unsigned int cnt, int node)
{
	struct irq_desc *desc;
	unsigned long flags;
	int i;

	for (i = 0; i < cnt; i++) {
		desc = alloc_desc(start + i, node);
		if (!desc)
			goto err;
		/* temporary until I fixed x86 madness */
		arch_init_chip_data(desc, node);
		raw_spin_lock_irqsave(&sparse_irq_lock, flags);
		irq_insert_desc(start + i, desc);
		raw_spin_unlock_irqrestore(&sparse_irq_lock, flags);
	}
	return start;

err:
	for (i--; i >= 0; i--)
		free_desc(start + i);

	raw_spin_lock_irqsave(&sparse_irq_lock, flags);
	bitmap_clear(allocated_irqs, start, cnt);
	raw_spin_unlock_irqrestore(&sparse_irq_lock, flags);
	return -ENOMEM;
}

struct irq_desc * __ref irq_to_desc_alloc_node(unsigned int irq, int node)
{
	int res = irq_alloc_descs(irq, irq, 1, node);

	if (res == -EEXIST || res == irq)
		return irq_to_desc(irq);
	return NULL;
}

int __init early_irq_init(void)
{
	int i, node = first_online_node;
	struct irq_desc *desc;

	init_irq_default_affinity();

	 /* initialize nr_irqs based on nr_cpu_ids */
	arch_probe_nr_irqs();
	printk(KERN_INFO "NR_IRQS:%d nr_irqs:%d\n", NR_IRQS, nr_irqs);

	for (i = 0; i < NR_IRQS_LEGACY; i++) {
		desc = alloc_desc(i, node);
		set_bit(i, allocated_irqs);
		irq_insert_desc(i, desc);
	}
	return arch_early_irq_init();
}

#else /* !CONFIG_SPARSE_IRQ */

struct irq_desc irq_desc[NR_IRQS] __cacheline_aligned_in_smp = {
	[0 ... NR_IRQS-1] = {
		.status		= IRQ_DEFAULT_INIT_FLAGS,
		.handle_irq	= handle_bad_irq,
		.depth		= 1,
		.lock		= __RAW_SPIN_LOCK_UNLOCKED(irq_desc->lock),
	}
};

static unsigned int kstat_irqs_all[NR_IRQS][NR_CPUS];
int __init early_irq_init(void)
{
	int count, i, node = first_online_node;
	struct irq_desc *desc;

	init_irq_default_affinity();

	printk(KERN_INFO "NR_IRQS:%d\n", NR_IRQS);

	desc = irq_desc;
	count = ARRAY_SIZE(irq_desc);

	for (i = 0; i < count; i++) {
		desc[i].irq_data.irq = i;
		desc[i].irq_data.chip = &no_irq_chip;
		desc[i].kstat_irqs = kstat_irqs_all[i];
		alloc_masks(desc + i, GFP_KERNEL, node);
		desc_smp_init(desc + i, node);
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

#ifdef CONFIG_SMP
static inline int desc_node(struct irq_desc *desc)
{
	return desc->irq_data.node;
}
#else
static inline int desc_node(struct irq_desc *desc) { return 0; }
#endif

static void free_desc(unsigned int irq)
{
	struct irq_desc *desc = irq_to_desc(irq);
	unsigned long flags;

	raw_spin_lock_irqsave(&desc->lock, flags);
	desc_set_defaults(irq, desc, desc_node(desc));
	raw_spin_unlock_irqrestore(&desc->lock, flags);
}

static inline int alloc_descs(unsigned int start, unsigned int cnt, int node)
{
	return start;
}
#endif /* !CONFIG_SPARSE_IRQ */

/* Dynamic interrupt handling */

/**
 * irq_free_descs - free irq descriptors
 * @from:	Start of descriptor range
 * @cnt:	Number of consecutive irqs to free
 */
void irq_free_descs(unsigned int from, unsigned int cnt)
{
	unsigned long flags;
	int i;

	if (from >= nr_irqs || (from + cnt) > nr_irqs)
		return;

	for (i = 0; i < cnt; i++)
		free_desc(from + i);

	raw_spin_lock_irqsave(&sparse_irq_lock, flags);
	bitmap_clear(allocated_irqs, from, cnt);
	raw_spin_unlock_irqrestore(&sparse_irq_lock, flags);
}

/**
 * irq_alloc_descs - allocate and initialize a range of irq descriptors
 * @irq:	Allocate for specific irq number if irq >= 0
 * @from:	Start the search from this irq number
 * @cnt:	Number of consecutive irqs to allocate.
 * @node:	Preferred node on which the irq descriptor should be allocated
 *
 * Returns the first irq number or error code
 */
int __ref
irq_alloc_descs(int irq, unsigned int from, unsigned int cnt, int node)
{
	unsigned long flags;
	int start, ret;

	if (!cnt)
		return -EINVAL;

	raw_spin_lock_irqsave(&sparse_irq_lock, flags);

	start = bitmap_find_next_zero_area(allocated_irqs, nr_irqs, from, cnt, 0);
	ret = -EEXIST;
	if (irq >=0 && start != irq)
		goto err;

	ret = -ENOMEM;
	if (start >= nr_irqs)
		goto err;

	bitmap_set(allocated_irqs, start, cnt);
	raw_spin_unlock_irqrestore(&sparse_irq_lock, flags);
	return alloc_descs(start, cnt, node);

err:
	raw_spin_unlock_irqrestore(&sparse_irq_lock, flags);
	return ret;
}

/**
 * irq_reserve_irqs - mark irqs allocated
 * @from:	mark from irq number
 * @cnt:	number of irqs to mark
 *
 * Returns 0 on success or an appropriate error code
 */
int irq_reserve_irqs(unsigned int from, unsigned int cnt)
{
	unsigned long flags;
	unsigned int start;
	int ret = 0;

	if (!cnt || (from + cnt) > nr_irqs)
		return -EINVAL;

	raw_spin_lock_irqsave(&sparse_irq_lock, flags);
	start = bitmap_find_next_zero_area(allocated_irqs, nr_irqs, from, cnt, 0);
	if (start == from)
		bitmap_set(allocated_irqs, start, cnt);
	else
		ret = -EEXIST;
	raw_spin_unlock_irqrestore(&sparse_irq_lock, flags);
	return ret;
}

/**
 * irq_get_next_irq - get next allocated irq number
 * @offset:	where to start the search
 *
 * Returns next irq number after offset or nr_irqs if none is found.
 */
unsigned int irq_get_next_irq(unsigned int offset)
{
	return find_next_bit(allocated_irqs, nr_irqs, offset);
}

/* Statistics access */
void clear_kstat_irqs(struct irq_desc *desc)
{
	memset(desc->kstat_irqs, 0, nr_cpu_ids * sizeof(*(desc->kstat_irqs)));
}

unsigned int kstat_irqs_cpu(unsigned int irq, int cpu)
{
	struct irq_desc *desc = irq_to_desc(irq);
	return desc ? desc->kstat_irqs[cpu] : 0;
}
