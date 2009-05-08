/*
 * NUMA irq-desc migration code
 *
 * Migrate IRQ data structures (irq_desc, chip_data, etc.) over to
 * the new "home node" of the IRQ.
 */

#include <linux/irq.h>
#include <linux/module.h>
#include <linux/random.h>
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>

#include "internals.h"

static void init_copy_kstat_irqs(struct irq_desc *old_desc,
				 struct irq_desc *desc,
				 int cpu, int nr)
{
	init_kstat_irqs(desc, cpu, nr);

	if (desc->kstat_irqs != old_desc->kstat_irqs)
		memcpy(desc->kstat_irqs, old_desc->kstat_irqs,
			 nr * sizeof(*desc->kstat_irqs));
}

static void free_kstat_irqs(struct irq_desc *old_desc, struct irq_desc *desc)
{
	if (old_desc->kstat_irqs == desc->kstat_irqs)
		return;

	kfree(old_desc->kstat_irqs);
	old_desc->kstat_irqs = NULL;
}

static bool init_copy_one_irq_desc(int irq, struct irq_desc *old_desc,
		 struct irq_desc *desc, int cpu)
{
	memcpy(desc, old_desc, sizeof(struct irq_desc));
	if (!init_alloc_desc_masks(desc, cpu, false)) {
		printk(KERN_ERR "irq %d: can not get new irq_desc cpumask "
				"for migration.\n", irq);
		return false;
	}
	spin_lock_init(&desc->lock);
	desc->cpu = cpu;
	lockdep_set_class(&desc->lock, &irq_desc_lock_class);
	init_copy_kstat_irqs(old_desc, desc, cpu, nr_cpu_ids);
	init_copy_desc_masks(old_desc, desc);
	arch_init_copy_chip_data(old_desc, desc, cpu);
	return true;
}

static void free_one_irq_desc(struct irq_desc *old_desc, struct irq_desc *desc)
{
	free_kstat_irqs(old_desc, desc);
	free_desc_masks(old_desc, desc);
	arch_free_chip_data(old_desc, desc);
}

static struct irq_desc *__real_move_irq_desc(struct irq_desc *old_desc,
						int cpu)
{
	struct irq_desc *desc;
	unsigned int irq;
	unsigned long flags;
	int node;

	irq = old_desc->irq;

	spin_lock_irqsave(&sparse_irq_lock, flags);

	/* We have to check it to avoid races with another CPU */
	desc = irq_desc_ptrs[irq];

	if (desc && old_desc != desc)
		goto out_unlock;

	node = cpu_to_node(cpu);
	desc = kzalloc_node(sizeof(*desc), GFP_ATOMIC, node);
	if (!desc) {
		printk(KERN_ERR "irq %d: can not get new irq_desc "
				"for migration.\n", irq);
		/* still use old one */
		desc = old_desc;
		goto out_unlock;
	}
	if (!init_copy_one_irq_desc(irq, old_desc, desc, cpu)) {
		/* still use old one */
		kfree(desc);
		desc = old_desc;
		goto out_unlock;
	}

	irq_desc_ptrs[irq] = desc;
	spin_unlock_irqrestore(&sparse_irq_lock, flags);

	/* free the old one */
	free_one_irq_desc(old_desc, desc);
	spin_unlock(&old_desc->lock);
	kfree(old_desc);
	spin_lock(&desc->lock);

	return desc;

out_unlock:
	spin_unlock_irqrestore(&sparse_irq_lock, flags);

	return desc;
}

struct irq_desc *move_irq_desc(struct irq_desc *desc, int cpu)
{
	int old_cpu;
	int node, old_node;

	/* those all static, do move them */
	if (desc->irq < NR_IRQS_LEGACY)
		return desc;

	old_cpu = desc->cpu;
	if (old_cpu != cpu) {
		node = cpu_to_node(cpu);
		old_node = cpu_to_node(old_cpu);
		if (old_node != node)
			desc = __real_move_irq_desc(desc, cpu);
		else
			desc->cpu = cpu;
	}

	return desc;
}

