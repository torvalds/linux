// SPDX-License-Identifier: GPL-2.0
// Copyright 2017 Thomas Gleixner <tglx@linutronix.de>

#include <linux/irqdomain.h>
#include <linux/irq.h>
#include <linux/uaccess.h>

#include "internals.h"

static struct dentry *irq_dir;

void irq_debug_show_bits(struct seq_file *m, int ind, unsigned int state,
			 const struct irq_bit_descr *sd, int size)
{
	int i;

	for (i = 0; i < size; i++, sd++) {
		if (state & sd->mask)
			seq_printf(m, "%*s%s\n", ind + 12, "", sd->name);
	}
}

#ifdef CONFIG_SMP
static void irq_debug_show_masks(struct seq_file *m, struct irq_desc *desc)
{
	struct irq_data *data = irq_desc_get_irq_data(desc);
	const struct cpumask *msk;

	msk = irq_data_get_affinity_mask(data);
	seq_printf(m, "affinity: %*pbl\n", cpumask_pr_args(msk));
#ifdef CONFIG_GENERIC_IRQ_EFFECTIVE_AFF_MASK
	msk = irq_data_get_effective_affinity_mask(data);
	seq_printf(m, "effectiv: %*pbl\n", cpumask_pr_args(msk));
#endif
#ifdef CONFIG_GENERIC_PENDING_IRQ
	msk = desc->pending_mask;
	seq_printf(m, "pending:  %*pbl\n", cpumask_pr_args(msk));
#endif
}
#else
static void irq_debug_show_masks(struct seq_file *m, struct irq_desc *desc) { }
#endif

static const struct irq_bit_descr irqchip_flags[] = {
	BIT_MASK_DESCR(IRQCHIP_SET_TYPE_MASKED),
	BIT_MASK_DESCR(IRQCHIP_EOI_IF_HANDLED),
	BIT_MASK_DESCR(IRQCHIP_MASK_ON_SUSPEND),
	BIT_MASK_DESCR(IRQCHIP_ONOFFLINE_ENABLED),
	BIT_MASK_DESCR(IRQCHIP_SKIP_SET_WAKE),
	BIT_MASK_DESCR(IRQCHIP_ONESHOT_SAFE),
	BIT_MASK_DESCR(IRQCHIP_EOI_THREADED),
	BIT_MASK_DESCR(IRQCHIP_SUPPORTS_LEVEL_MSI),
	BIT_MASK_DESCR(IRQCHIP_SUPPORTS_NMI),
	BIT_MASK_DESCR(IRQCHIP_ENABLE_WAKEUP_ON_SUSPEND),
	BIT_MASK_DESCR(IRQCHIP_IMMUTABLE),
	BIT_MASK_DESCR(IRQCHIP_MOVE_DEFERRED),
};

static void
irq_debug_show_chip(struct seq_file *m, struct irq_data *data, int ind)
{
	struct irq_chip *chip = data->chip;

	if (!chip) {
		seq_printf(m, "chip: None\n");
		return;
	}
	seq_printf(m, "%*schip:    ", ind, "");
	if (chip->irq_print_chip)
		chip->irq_print_chip(data, m);
	else
		seq_printf(m, "%s", chip->name);
	seq_printf(m, "\n%*sflags:   0x%lx\n", ind + 1, "", chip->flags);
	irq_debug_show_bits(m, ind, chip->flags, irqchip_flags,
			    ARRAY_SIZE(irqchip_flags));
}

static void
irq_debug_show_data(struct seq_file *m, struct irq_data *data, int ind)
{
	seq_printf(m, "%*sdomain:  %s\n", ind, "",
		   data->domain ? data->domain->name : "");
	seq_printf(m, "%*shwirq:   0x%lx\n", ind + 1, "", data->hwirq);
	irq_debug_show_chip(m, data, ind + 1);
	if (data->domain && data->domain->ops && data->domain->ops->debug_show)
		data->domain->ops->debug_show(m, NULL, data, ind + 1);
#ifdef	CONFIG_IRQ_DOMAIN_HIERARCHY
	if (!data->parent_data)
		return;
	seq_printf(m, "%*sparent:\n", ind + 1, "");
	irq_debug_show_data(m, data->parent_data, ind + 4);
#endif
}

static const struct irq_bit_descr irqdata_states[] = {
	BIT_MASK_DESCR(IRQ_TYPE_EDGE_RISING),
	BIT_MASK_DESCR(IRQ_TYPE_EDGE_FALLING),
	BIT_MASK_DESCR(IRQ_TYPE_LEVEL_HIGH),
	BIT_MASK_DESCR(IRQ_TYPE_LEVEL_LOW),
	BIT_MASK_DESCR(IRQD_LEVEL),

	BIT_MASK_DESCR(IRQD_ACTIVATED),
	BIT_MASK_DESCR(IRQD_IRQ_STARTED),
	BIT_MASK_DESCR(IRQD_IRQ_DISABLED),
	BIT_MASK_DESCR(IRQD_IRQ_MASKED),
	BIT_MASK_DESCR(IRQD_IRQ_INPROGRESS),

	BIT_MASK_DESCR(IRQD_PER_CPU),
	BIT_MASK_DESCR(IRQD_NO_BALANCING),

	BIT_MASK_DESCR(IRQD_SINGLE_TARGET),
	BIT_MASK_DESCR(IRQD_AFFINITY_SET),
	BIT_MASK_DESCR(IRQD_SETAFFINITY_PENDING),
	BIT_MASK_DESCR(IRQD_AFFINITY_MANAGED),
	BIT_MASK_DESCR(IRQD_AFFINITY_ON_ACTIVATE),
	BIT_MASK_DESCR(IRQD_MANAGED_SHUTDOWN),
	BIT_MASK_DESCR(IRQD_CAN_RESERVE),

	BIT_MASK_DESCR(IRQD_FORWARDED_TO_VCPU),

	BIT_MASK_DESCR(IRQD_WAKEUP_STATE),
	BIT_MASK_DESCR(IRQD_WAKEUP_ARMED),

	BIT_MASK_DESCR(IRQD_DEFAULT_TRIGGER_SET),

	BIT_MASK_DESCR(IRQD_HANDLE_ENFORCE_IRQCTX),

	BIT_MASK_DESCR(IRQD_IRQ_ENABLED_ON_SUSPEND),

	BIT_MASK_DESCR(IRQD_RESEND_WHEN_IN_PROGRESS),
};

static const struct irq_bit_descr irqdesc_states[] = {
	BIT_MASK_DESCR(_IRQ_NOPROBE),
	BIT_MASK_DESCR(_IRQ_NOREQUEST),
	BIT_MASK_DESCR(_IRQ_NOTHREAD),
	BIT_MASK_DESCR(_IRQ_NOAUTOEN),
	BIT_MASK_DESCR(_IRQ_NESTED_THREAD),
	BIT_MASK_DESCR(_IRQ_PER_CPU_DEVID),
	BIT_MASK_DESCR(_IRQ_IS_POLLED),
	BIT_MASK_DESCR(_IRQ_DISABLE_UNLAZY),
	BIT_MASK_DESCR(_IRQ_HIDDEN),
};

static const struct irq_bit_descr irqdesc_istates[] = {
	BIT_MASK_DESCR(IRQS_AUTODETECT),
	BIT_MASK_DESCR(IRQS_SPURIOUS_DISABLED),
	BIT_MASK_DESCR(IRQS_POLL_INPROGRESS),
	BIT_MASK_DESCR(IRQS_ONESHOT),
	BIT_MASK_DESCR(IRQS_REPLAY),
	BIT_MASK_DESCR(IRQS_WAITING),
	BIT_MASK_DESCR(IRQS_PENDING),
	BIT_MASK_DESCR(IRQS_SUSPENDED),
	BIT_MASK_DESCR(IRQS_NMI),
};


static int irq_debug_show(struct seq_file *m, void *p)
{
	struct irq_desc *desc = m->private;
	struct irq_data *data;

	raw_spin_lock_irq(&desc->lock);
	data = irq_desc_get_irq_data(desc);
	seq_printf(m, "handler:  %ps\n", desc->handle_irq);
	seq_printf(m, "device:   %s\n", desc->dev_name);
	seq_printf(m, "status:   0x%08x\n", desc->status_use_accessors);
	irq_debug_show_bits(m, 0, desc->status_use_accessors, irqdesc_states,
			    ARRAY_SIZE(irqdesc_states));
	seq_printf(m, "istate:   0x%08x\n", desc->istate);
	irq_debug_show_bits(m, 0, desc->istate, irqdesc_istates,
			    ARRAY_SIZE(irqdesc_istates));
	seq_printf(m, "ddepth:   %u\n", desc->depth);
	seq_printf(m, "wdepth:   %u\n", desc->wake_depth);
	seq_printf(m, "dstate:   0x%08x\n", irqd_get(data));
	irq_debug_show_bits(m, 0, irqd_get(data), irqdata_states,
			    ARRAY_SIZE(irqdata_states));
	seq_printf(m, "node:     %d\n", irq_data_get_node(data));
	irq_debug_show_masks(m, desc);
	irq_debug_show_data(m, data, 0);
	raw_spin_unlock_irq(&desc->lock);
	return 0;
}

static int irq_debug_open(struct inode *inode, struct file *file)
{
	return single_open(file, irq_debug_show, inode->i_private);
}

static ssize_t irq_debug_write(struct file *file, const char __user *user_buf,
			       size_t count, loff_t *ppos)
{
	struct irq_desc *desc = file_inode(file)->i_private;
	char buf[8] = { 0, };
	size_t size;

	size = min(sizeof(buf) - 1, count);
	if (copy_from_user(buf, user_buf, size))
		return -EFAULT;

	if (!strncmp(buf, "trigger", size)) {
		int err = irq_inject_interrupt(irq_desc_get_irq(desc));

		return err ? err : count;
	}

	return count;
}

static const struct file_operations dfs_irq_ops = {
	.open		= irq_debug_open,
	.write		= irq_debug_write,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

void irq_debugfs_copy_devname(int irq, struct device *dev)
{
	struct irq_desc *desc = irq_to_desc(irq);
	const char *name = dev_name(dev);

	if (name)
		desc->dev_name = kstrdup(name, GFP_KERNEL);
}

void irq_add_debugfs_entry(unsigned int irq, struct irq_desc *desc)
{
	char name [10];

	if (!irq_dir || !desc || desc->debugfs_file)
		return;

	sprintf(name, "%d", irq);
	desc->debugfs_file = debugfs_create_file(name, 0644, irq_dir, desc,
						 &dfs_irq_ops);
}

static int __init irq_debugfs_init(void)
{
	struct dentry *root_dir;
	int irq;

	root_dir = debugfs_create_dir("irq", NULL);

	irq_domain_debugfs_init(root_dir);

	irq_dir = debugfs_create_dir("irqs", root_dir);

	irq_lock_sparse();
	for_each_active_irq(irq)
		irq_add_debugfs_entry(irq, irq_to_desc(irq));
	irq_unlock_sparse();

	return 0;
}
__initcall(irq_debugfs_init);
