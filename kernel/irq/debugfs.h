/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KERNEL_IRQ_DEBUGFS_H
#define _KERNEL_IRQ_DEBUGFS_H

#ifdef CONFIG_GENERIC_IRQ_DEBUGFS
#include <linux/debugfs.h>

struct irq_bit_descr {
	unsigned int	mask;
	char		*name;
};

#define BIT_MASK_DESCR(m)	{ .mask = m, .name = #m }

void irq_debug_show_bits(struct seq_file *m, int ind, unsigned int state,
			 const struct irq_bit_descr *sd, int size);

void irq_add_debugfs_entry(unsigned int irq, struct irq_desc *desc);
static inline void irq_remove_debugfs_entry(struct irq_desc *desc)
{
	debugfs_remove(desc->debugfs_file);
	kfree(desc->dev_name);
}
void irq_debugfs_copy_devname(int irq, struct device *dev);
# ifdef CONFIG_IRQ_DOMAIN
void irq_domain_debugfs_init(struct dentry *root);
# else
static inline void irq_domain_debugfs_init(struct dentry *root)
{
}
# endif
#else /* CONFIG_GENERIC_IRQ_DEBUGFS */
static inline void irq_add_debugfs_entry(unsigned int irq, struct irq_desc *d)
{
}
static inline void irq_remove_debugfs_entry(struct irq_desc *d)
{
}
static inline void irq_debugfs_copy_devname(int irq, struct device *dev)
{
}
#endif /* CONFIG_GENERIC_IRQ_DEBUGFS */

#endif
