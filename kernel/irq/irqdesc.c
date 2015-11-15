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
#include <linux/export.h>
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>
#include <linux/radix-tree.h>
#include <linux/bitmap.h>
#include <linux/irqdomain.h>

#include "internals.h"

/*
 * lockdep: we want to handle all irq_desc locks as a single lock-class:
 */
static struct lock_class_key irq_desc_lock_class;

#if defined(CONFIG_SMP)
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
	if (!zalloc_cpumask_var_node(&desc->irq_common_data.affinity,
				     gfp, node))
		return -ENOMEM;

#ifdef CONFIG_GENERIC_PENDING_IRQ
	if (!zalloc_cpumask_var_node(&desc->pending_mask, gfp, node)) {
		free_cpumask_var(desc->irq_common_data.affinity);
		return -ENOMEM;
	}
#endif
	return 0;
}

static void desc_smp_init(struct irq_desc *desc, int node)
{
	cpumask_copy(desc->irq_common_data.affinity, irq_default_affinity);
#ifdef CONFIG_GENERIC_PENDING_IRQ
	cpumask_clear(desc->pending_mask);
#endif
#ifdef CONFIG_NUMA
	desc->irq_common_data.node = node;
#endif
}

#else
static inline int
alloc_masks(struct irq_desc *desc, gfp_t gfp, int node) { return 0; }
static inline void desc_smp_init(struct irq_desc *desc, int node) { }
#endif

static void desc_set_defaults(unsigned int irq, struct irq_desc *desc, int node,
		struct module *owner)
{
	int cpu;

	desc->irq_common_data.handler_data = NULL;
	desc->irq_common_data.msi_desc = NULL;

	desc->irq_data.common = &desc->irq_common_data;
	desc->irq_data.irq = irq;
	desc->irq_data.chip = &no_irq_chip;
	desc->irq_data.chip_data = NULL;
	irq_settings_clr_and_set(desc, ~0, _IRQ_DEFAULT_INIT_FLAGS);
	irqd_set(&desc->irq_data, IRQD_IRQ_DISABLED);
	desc->handle_irq = handle_bad_irq;
	desc->depth = 1;
	desc->irq_count = 0;
	desc->irqs_unhandled = 0;
	desc->name = NULL;
	desc->owner = owner;
	for_each_possible_cpu(cpu)
		*per_cpu_ptr(desc->kstat_irqs, cpu) = 0;
	desc_smp_init(desc, node);
}

int nr_irqs = NR_IRQS;
EXPORT_SYMBOL_GPL(nr_irqs);

static DEFINE_MUTEX(sparse_irq_lock);
static DECLARE_BITMAP(allocated_irqs, IRQ_BITMAP_BITS);

#ifdef CONFIG_SPARSE_IRQ

static RADIX_TREE(irq_desc_tree, GFP_KERNEL);

static void irq_insert_desc(unsigned int irq, struct irq_desc *desc)
{
	radix_tree_insert(&irq_desc_tree, irq, desc);
}

struct irq_desc *irq_to_desc(unsigned int irq)
{
	return radix_tree_lookup(&irq_desc_tree, irq);
}
EXPORT_SYMBOL(irq_to_desc);

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
	free_cpumask_var(desc->irq_common_data.affinity);
}
#else
static inline void free_masks(struct irq_desc *desc) { }
#endif

void irq_lock_sparse(void)
{
	mutex_lock(&sparse_irq_lock);
}

void irq_unlock_sparse(void)
{
	mutex_unlock(&sparse_irq_lock);
}

static struct irq_desc *alloc_desc(int irq, int node, struct module *owner)
{
	struct irq_desc *desc;
	gfp_t gfp = GFP_KERNEL;

	desc = kzalloc_node(sizeof(*desc), gfp, node);
	if (!desc)
		return NULL;
	/* allocate based on nr_cpu_ids */
	desc->kstat_irqs = alloc_percpu(unsigned int);
	if (!desc->kstat_irqs)
		goto err_desc;

	if (alloc_masks(desc, gfp, node))
		goto err_kstat;

	raw_spin_lock_init(&desc->lock);
	lockdep_set_class(&desc->lock, &irq_desc_lock_class);

	desc_set_defaults(irq, desc, node, owner);

	return desc;

err_kstat:
	free_percpu(desc->kstat_irqs);
err_desc:
	kfree(desc);
	return NULL;
}

static void free_desc(unsigned int irq)
{
	struct irq_desc *desc = irq_to_desc(irq);

	unregister_irq_proc(irq, desc);

	/*
	 * sparse_irq_lock protects also show_interrupts() and
	 * kstat_irq_usr(). Once we deleted the descriptor from the
	 * sparse tree we can free it. Access in proc will fail to
	 * lookup the descriptor.
	 */
	mutex_lock(&sparse_irq_lock);
	delete_irq_desc(irq);
	mutex_unlock(&sparse_irq_lock);

	free_masks(desc);
	free_percpu(desc->kstat_irqs);
	kfree(desc);
}

static int alloc_descs(unsigned int start, unsigned int cnt, int node,
		       struct module *owner)
{
	struct irq_desc *desc;
	int i;

	for (i = 0; i < cnt; i++) {
		desc = alloc_desc(start + i, node, owner);
		if (!desc)
			goto err;
		mutex_lock(&sparse_irq_lock);
		irq_insert_desc(start + i, desc);
		mutex_unlock(&sparse_irq_lock);
	}
	return start;

err:
	for (i--; i >= 0; i--)
		free_desc(start + i);

	mutex_lock(&sparse_irq_lock);
	bitmap_clear(allocated_irqs, start, cnt);
	mutex_unlock(&sparse_irq_lock);
	return -ENOMEM;
}

static int irq_expand_nr_irqs(unsigned int nr)
{
	if (nr > IRQ_BITMAP_BITS)
		return -ENOMEM;
	nr_irqs = nr;
	return 0;
}

int __init early_irq_init(void)
{
	int i, initcnt, node = first_online_node;
	struct irq_desc *desc;

	init_irq_default_affinity();

	/* Let arch update nr_irqs and return the nr of preallocated irqs */
	initcnt = arch_probe_nr_irqs();
	printk(KERN_INFO "NR_IRQS:%d nr_irqs:%d %d\n", NR_IRQS, nr_irqs, initcnt);

	if (WARN_ON(nr_irqs > IRQ_BITMAP_BITS))
		nr_irqs = IRQ_BITMAP_BITS;

	if (WARN_ON(initcnt > IRQ_BITMAP_BITS))
		initcnt = IRQ_BITMAP_BITS;

	if (initcnt > nr_irqs)
		nr_irqs = initcnt;

	for (i = 0; i < initcnt; i++) {
		desc = alloc_desc(i, node, NULL);
		set_bit(i, allocated_irqs);
		irq_insert_desc(i, desc);
	}
	return arch_early_irq_init();
}

#else /* !CONFIG_SPARSE_IRQ */

struct irq_desc irq_desc[NR_IRQS] __cacheline_aligned_in_smp = {
	[0 ... NR_IRQS-1] = {
		.handle_irq	= handle_bad_irq,
		.depth		= 1,
		.lock		= __RAW_SPIN_LOCK_UNLOCKED(irq_desc->lock),
	}
};

int __init early_irq_init(void)
{
	int count, i, node = first_online_node;
	struct irq_desc *desc;

	init_irq_default_affinity();

	printk(KERN_INFO "NR_IRQS:%d\n", NR_IRQS);

	desc = irq_desc;
	count = ARRAY_SIZE(irq_desc);

	for (i = 0; i < count; i++) {
		desc[i].kstat_irqs = alloc_percpu(unsigned int);
		alloc_masks(&desc[i], GFP_KERNEL, node);
		raw_spin_lock_init(&desc[i].lock);
		lockdep_set_class(&desc[i].lock, &irq_desc_lock_class);
		desc_set_defaults(i, &desc[i], node, NULL);
	}
	return arch_early_irq_init();
}

struct irq_desc *irq_to_desc(unsigned int irq)
{
	return (irq < NR_IRQS) ? irq_desc + irq : NULL;
}
EXPORT_SYMBOL(irq_to_desc);

static void free_desc(unsigned int irq)
{
	struct irq_desc *desc = irq_to_desc(irq);
	unsigned long flags;

	raw_spin_lock_irqsave(&desc->lock, flags);
	desc_set_defaults(irq, desc, irq_desc_get_node(desc), NULL);
	raw_spin_unlock_irqrestore(&desc->lock, flags);
}

static inline int alloc_descs(unsigned int start, unsigned int cnt, int node,
			      struct module *owner)
{
	u32 i;

	for (i = 0; i < cnt; i++) {
		struct irq_desc *desc = irq_to_desc(start + i);

		desc->owner = owner;
	}
	return start;
}

static int irq_expand_nr_irqs(unsigned int nr)
{
	return -ENOMEM;
}

void irq_mark_irq(unsigned int irq)
{
	mutex_lock(&sparse_irq_lock);
	bitmap_set(allocated_irqs, irq, 1);
	mutex_unlock(&sparse_irq_lock);
}

#ifdef CONFIG_GENERIC_IRQ_LEGACY
void irq_init_desc(unsigned int irq)
{
	free_desc(irq);
}
#endif

#endif /* !CONFIG_SPARSE_IRQ */

/**
 * generic_handle_irq - Invoke the handler for a particular irq
 * @irq:	The irq number to handle
 *
 */
int generic_handle_irq(unsigned int irq)
{
	struct irq_desc *desc = irq_to_desc(irq);

	if (!desc)
		return -EINVAL;
	generic_handle_irq_desc(desc);
	return 0;
}
EXPORT_SYMBOL_GPL(generic_handle_irq);

#ifdef CONFIG_HANDLE_DOMAIN_IRQ
/**
 * __handle_domain_irq - Invoke the handler for a HW irq belonging to a domain
 * @domain:	The domain where to perform the lookup
 * @hwirq:	The HW irq number to convert to a logical one
 * @lookup:	Whether to perform the domain lookup or not
 * @regs:	Register file coming from the low-level handling code
 *
 * Returns:	0 on success, or -EINVAL if conversion has failed
 */
int __handle_domain_irq(struct irq_domain *domain, unsigned int hwirq,
			bool lookup, struct pt_regs *regs)
{
	struct pt_regs *old_regs = set_irq_regs(regs);
	unsigned int irq = hwirq;
	int ret = 0;

	irq_enter();

#ifdef CONFIG_IRQ_DOMAIN
	if (lookup)
		irq = irq_find_mapping(domain, hwirq);
#endif

	/*
	 * Some hardware gives randomly wrong interrupts.  Rather
	 * than crashing, do something sensible.
	 */
	if (unlikely(!irq || irq >= nr_irqs)) {
		ack_bad_irq(irq);
		ret = -EINVAL;
	} else {
		generic_handle_irq(irq);
	}

	irq_exit();
	set_irq_regs(old_regs);
	return ret;
}
#endif

/* Dynamic interrupt handling */

/**
 * irq_free_descs - free irq descriptors
 * @from:	Start of descriptor range
 * @cnt:	Number of consecutive irqs to free
 */
void irq_free_descs(unsigned int from, unsigned int cnt)
{
	int i;

	if (from >= nr_irqs || (from + cnt) > nr_irqs)
		return;

	for (i = 0; i < cnt; i++)
		free_desc(from + i);

	mutex_lock(&sparse_irq_lock);
	bitmap_clear(allocated_irqs, from, cnt);
	mutex_unlock(&sparse_irq_lock);
}
EXPORT_SYMBOL_GPL(irq_free_descs);

/**
 * irq_alloc_descs - allocate and initialize a range of irq descriptors
 * @irq:	Allocate for specific irq number if irq >= 0
 * @from:	Start the search from this irq number
 * @cnt:	Number of consecutive irqs to allocate.
 * @node:	Preferred node on which the irq descriptor should be allocated
 * @owner:	Owning module (can be NULL)
 *
 * Returns the first irq number or error code
 */
int __ref
__irq_alloc_descs(int irq, unsigned int from, unsigned int cnt, int node,
		  struct module *owner)
{
	int start, ret;

	if (!cnt)
		return -EINVAL;

	if (irq >= 0) {
		if (from > irq)
			return -EINVAL;
		from = irq;
	} else {
		/*
		 * For interrupts which are freely allocated the
		 * architecture can force a lower bound to the @from
		 * argument. x86 uses this to exclude the GSI space.
		 */
		from = arch_dynirq_lower_bound(from);
	}

	mutex_lock(&sparse_irq_lock);

	start = bitmap_find_next_zero_area(allocated_irqs, IRQ_BITMAP_BITS,
					   from, cnt, 0);
	ret = -EEXIST;
	if (irq >=0 && start != irq)
		goto err;

	if (start + cnt > nr_irqs) {
		ret = irq_expand_nr_irqs(start + cnt);
		if (ret)
			goto err;
	}

	bitmap_set(allocated_irqs, start, cnt);
	mutex_unlock(&sparse_irq_lock);
	return alloc_descs(start, cnt, node, owner);

err:
	mutex_unlock(&sparse_irq_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(__irq_alloc_descs);

#ifdef CONFIG_GENERIC_IRQ_LEGACY_ALLOC_HWIRQ
/**
 * irq_alloc_hwirqs - Allocate an irq descriptor and initialize the hardware
 * @cnt:	number of interrupts to allocate
 * @node:	node on which to allocate
 *
 * Returns an interrupt number > 0 or 0, if the allocation fails.
 */
unsigned int irq_alloc_hwirqs(int cnt, int node)
{
	int i, irq = __irq_alloc_descs(-1, 0, cnt, node, NULL);

	if (irq < 0)
		return 0;

	for (i = irq; cnt > 0; i++, cnt--) {
		if (arch_setup_hwirq(i, node))
			goto err;
		irq_clear_status_flags(i, _IRQ_NOREQUEST);
	}
	return irq;

err:
	for (i--; i >= irq; i--) {
		irq_set_status_flags(i, _IRQ_NOREQUEST | _IRQ_NOPROBE);
		arch_teardown_hwirq(i);
	}
	irq_free_descs(irq, cnt);
	return 0;
}
EXPORT_SYMBOL_GPL(irq_alloc_hwirqs);

/**
 * irq_free_hwirqs - Free irq descriptor and cleanup the hardware
 * @from:	Free from irq number
 * @cnt:	number of interrupts to free
 *
 */
void irq_free_hwirqs(unsigned int from, int cnt)
{
	int i, j;

	for (i = from, j = cnt; j > 0; i++, j--) {
		irq_set_status_flags(i, _IRQ_NOREQUEST | _IRQ_NOPROBE);
		arch_teardown_hwirq(i);
	}
	irq_free_descs(from, cnt);
}
EXPORT_SYMBOL_GPL(irq_free_hwirqs);
#endif

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

struct irq_desc *
__irq_get_desc_lock(unsigned int irq, unsigned long *flags, bool bus,
		    unsigned int check)
{
	struct irq_desc *desc = irq_to_desc(irq);

	if (desc) {
		if (check & _IRQ_DESC_CHECK) {
			if ((check & _IRQ_DESC_PERCPU) &&
			    !irq_settings_is_per_cpu_devid(desc))
				return NULL;

			if (!(check & _IRQ_DESC_PERCPU) &&
			    irq_settings_is_per_cpu_devid(desc))
				return NULL;
		}

		if (bus)
			chip_bus_lock(desc);
		raw_spin_lock_irqsave(&desc->lock, *flags);
	}
	return desc;
}

void __irq_put_desc_unlock(struct irq_desc *desc, unsigned long flags, bool bus)
{
	raw_spin_unlock_irqrestore(&desc->lock, flags);
	if (bus)
		chip_bus_sync_unlock(desc);
}

int irq_set_percpu_devid(unsigned int irq)
{
	struct irq_desc *desc = irq_to_desc(irq);

	if (!desc)
		return -EINVAL;

	if (desc->percpu_enabled)
		return -EINVAL;

	desc->percpu_enabled = kzalloc(sizeof(*desc->percpu_enabled), GFP_KERNEL);

	if (!desc->percpu_enabled)
		return -ENOMEM;

	irq_set_percpu_devid_flags(irq);
	return 0;
}

void kstat_incr_irq_this_cpu(unsigned int irq)
{
	kstat_incr_irqs_this_cpu(irq_to_desc(irq));
}

/**
 * kstat_irqs_cpu - Get the statistics for an interrupt on a cpu
 * @irq:	The interrupt number
 * @cpu:	The cpu number
 *
 * Returns the sum of interrupt counts on @cpu since boot for
 * @irq. The caller must ensure that the interrupt is not removed
 * concurrently.
 */
unsigned int kstat_irqs_cpu(unsigned int irq, int cpu)
{
	struct irq_desc *desc = irq_to_desc(irq);

	return desc && desc->kstat_irqs ?
			*per_cpu_ptr(desc->kstat_irqs, cpu) : 0;
}

/**
 * kstat_irqs - Get the statistics for an interrupt
 * @irq:	The interrupt number
 *
 * Returns the sum of interrupt counts on all cpus since boot for
 * @irq. The caller must ensure that the interrupt is not removed
 * concurrently.
 */
unsigned int kstat_irqs(unsigned int irq)
{
	struct irq_desc *desc = irq_to_desc(irq);
	int cpu;
	unsigned int sum = 0;

	if (!desc || !desc->kstat_irqs)
		return 0;
	for_each_possible_cpu(cpu)
		sum += *per_cpu_ptr(desc->kstat_irqs, cpu);
	return sum;
}

/**
 * kstat_irqs_usr - Get the statistics for an interrupt
 * @irq:	The interrupt number
 *
 * Returns the sum of interrupt counts on all cpus since boot for
 * @irq. Contrary to kstat_irqs() this can be called from any
 * preemptible context. It's protected against concurrent removal of
 * an interrupt descriptor when sparse irqs are enabled.
 */
unsigned int kstat_irqs_usr(unsigned int irq)
{
	unsigned int sum;

	irq_lock_sparse();
	sum = kstat_irqs(irq);
	irq_unlock_sparse();
	return sum;
}
