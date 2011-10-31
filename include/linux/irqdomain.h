/*
 * irq_domain - IRQ translation domains
 *
 * Translation infrastructure between hw and linux irq numbers.  This is
 * helpful for interrupt controllers to implement mapping between hardware
 * irq numbers and the Linux irq number space.
 *
 * irq_domains also have a hook for translating device tree interrupt
 * representation into a hardware irq number that can be mapped back to a
 * Linux irq number without any extra platform support code.
 *
 * irq_domain is expected to be embedded in an interrupt controller's private
 * data structure.
 */
#ifndef _LINUX_IRQDOMAIN_H
#define _LINUX_IRQDOMAIN_H

#include <linux/irq.h>
#include <linux/mod_devicetable.h>

#ifdef CONFIG_IRQ_DOMAIN
struct device_node;
struct irq_domain;

/**
 * struct irq_domain_ops - Methods for irq_domain objects
 * @to_irq: (optional) given a local hardware irq number, return the linux
 *          irq number.  If to_irq is not implemented, then the irq_domain
 *          will use this translation: irq = (domain->irq_base + hwirq)
 * @dt_translate: Given a device tree node and interrupt specifier, decode
 *                the hardware irq number and linux irq type value.
 */
struct irq_domain_ops {
	unsigned int (*to_irq)(struct irq_domain *d, unsigned long hwirq);

#ifdef CONFIG_OF
	int (*dt_translate)(struct irq_domain *d, struct device_node *node,
			    const u32 *intspec, unsigned int intsize,
			    unsigned long *out_hwirq, unsigned int *out_type);
#endif /* CONFIG_OF */
};

/**
 * struct irq_domain - Hardware interrupt number translation object
 * @list: Element in global irq_domain list.
 * @irq_base: Start of irq_desc range assigned to the irq_domain.  The creator
 *            of the irq_domain is responsible for allocating the array of
 *            irq_desc structures.
 * @nr_irq: Number of irqs managed by the irq domain
 * @hwirq_base: Starting number for hwirqs managed by the irq domain
 * @ops: pointer to irq_domain methods
 * @priv: private data pointer for use by owner.  Not touched by irq_domain
 *        core code.
 * @of_node: (optional) Pointer to device tree nodes associated with the
 *           irq_domain.  Used when decoding device tree interrupt specifiers.
 */
struct irq_domain {
	struct list_head list;
	unsigned int irq_base;
	unsigned int nr_irq;
	unsigned int hwirq_base;
	const struct irq_domain_ops *ops;
	void *priv;
	struct device_node *of_node;
};

/**
 * irq_domain_to_irq() - Translate from a hardware irq to a linux irq number
 *
 * Returns the linux irq number associated with a hardware irq.  By default,
 * the mapping is irq == domain->irq_base + hwirq, but this mapping can
 * be overridden if the irq_domain implements a .to_irq() hook.
 */
static inline unsigned int irq_domain_to_irq(struct irq_domain *d,
					     unsigned long hwirq)
{
	if (d->ops->to_irq)
		return d->ops->to_irq(d, hwirq);
	if (WARN_ON(hwirq < d->hwirq_base))
		return 0;
	return d->irq_base + hwirq - d->hwirq_base;
}

#define irq_domain_for_each_hwirq(d, hw) \
	for (hw = d->hwirq_base; hw < d->hwirq_base + d->nr_irq; hw++)

#define irq_domain_for_each_irq(d, hw, irq) \
	for (hw = d->hwirq_base, irq = irq_domain_to_irq(d, hw); \
	     hw < d->hwirq_base + d->nr_irq; \
	     hw++, irq = irq_domain_to_irq(d, hw))

extern void irq_domain_add(struct irq_domain *domain);
extern void irq_domain_del(struct irq_domain *domain);
#endif /* CONFIG_IRQ_DOMAIN */

#if defined(CONFIG_IRQ_DOMAIN) && defined(CONFIG_OF_IRQ)
extern struct irq_domain_ops irq_domain_simple_ops;
extern void irq_domain_add_simple(struct device_node *controller, int irq_base);
extern void irq_domain_generate_simple(const struct of_device_id *match,
					u64 phys_base, unsigned int irq_start);
#else /* CONFIG_IRQ_DOMAIN && CONFIG_OF_IRQ */
static inline void irq_domain_generate_simple(const struct of_device_id *match,
					u64 phys_base, unsigned int irq_start) { }
#endif /* CONFIG_IRQ_DOMAIN && CONFIG_OF_IRQ */

#endif /* _LINUX_IRQDOMAIN_H */
