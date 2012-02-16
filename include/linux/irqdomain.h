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
 * Interrupt controller "domain" data structure. This could be defined as a
 * irq domain controller. That is, it handles the mapping between hardware
 * and virtual interrupt numbers for a given interrupt domain. The domain
 * structure is generally created by the PIC code for a given PIC instance
 * (though a domain can cover more than one PIC if they have a flat number
 * model). It's the domain callbacks that are responsible for setting the
 * irq_chip on a given irq_desc after it's been mapped.
 *
 * The host code and data structures are agnostic to whether or not
 * we use an open firmware device-tree. We do have references to struct
 * device_node in two places: in irq_find_host() to find the host matching
 * a given interrupt controller node, and of course as an argument to its
 * counterpart domain->ops->match() callback. However, those are treated as
 * generic pointers by the core and the fact that it's actually a device-node
 * pointer is purely a convention between callers and implementation. This
 * code could thus be used on other architectures by replacing those two
 * by some sort of arch-specific void * "token" used to identify interrupt
 * controllers.
 */

#ifndef _LINUX_IRQDOMAIN_H
#define _LINUX_IRQDOMAIN_H

#include <linux/types.h>
#include <linux/radix-tree.h>

struct device_node;
struct irq_domain;
struct of_device_id;

/* This type is the placeholder for a hardware interrupt number. It has to
 * be big enough to enclose whatever representation is used by a given
 * platform.
 */
typedef unsigned long irq_hw_number_t;

/**
 * struct irq_domain_ops - Methods for irq_domain objects
 * @match: Match an interrupt controller device node to a host, returns
 *         1 on a match
 * @map: Create or update a mapping between a virtual irq number and a hw
 *       irq number. This is called only once for a given mapping.
 * @unmap: Dispose of such a mapping
 * @to_irq: (optional) given a local hardware irq number, return the linux
 *          irq number.  If to_irq is not implemented, then the irq_domain
 *          will use this translation: irq = (domain->irq_base + hwirq)
 * @xlate: Given a device tree node and interrupt specifier, decode
 *         the hardware irq number and linux irq type value.
 *
 * Functions below are provided by the driver and called whenever a new mapping
 * is created or an old mapping is disposed. The driver can then proceed to
 * whatever internal data structures management is required. It also needs
 * to setup the irq_desc when returning from map().
 */
struct irq_domain_ops {
	int (*match)(struct irq_domain *d, struct device_node *node);
	int (*map)(struct irq_domain *d, unsigned int virq, irq_hw_number_t hw);
	void (*unmap)(struct irq_domain *d, unsigned int virq);
	unsigned int (*to_irq)(struct irq_domain *d, unsigned long hwirq);
	int (*xlate)(struct irq_domain *d, struct device_node *node,
		     const u32 *intspec, unsigned int intsize,
		     unsigned long *out_hwirq, unsigned int *out_type);
};

/**
 * struct irq_domain - Hardware interrupt number translation object
 * @link: Element in global irq_domain list.
 * @revmap_type: Method used for reverse mapping hwirq numbers to linux irq. This
 *               will be one of the IRQ_DOMAIN_MAP_* values.
 * @revmap_data: Revmap method specific data.
 * @ops: pointer to irq_domain methods
 * @host_data: private data pointer for use by owner.  Not touched by irq_domain
 *             core code.
 * @irq_base: Start of irq_desc range assigned to the irq_domain.  The creator
 *            of the irq_domain is responsible for allocating the array of
 *            irq_desc structures.
 * @nr_irq: Number of irqs managed by the irq domain
 * @hwirq_base: Starting number for hwirqs managed by the irq domain
 * @of_node: (optional) Pointer to device tree nodes associated with the
 *           irq_domain.  Used when decoding device tree interrupt specifiers.
 */
struct irq_domain {
	struct list_head link;

	/* type of reverse mapping_technique */
	unsigned int revmap_type;
#define IRQ_DOMAIN_MAP_LEGACY 0 /* legacy 8259, gets irqs 1..15 */
#define IRQ_DOMAIN_MAP_NOMAP 1 /* no fast reverse mapping */
#define IRQ_DOMAIN_MAP_LINEAR 2 /* linear map of interrupts */
#define IRQ_DOMAIN_MAP_TREE 3 /* radix tree */
	union {
		struct {
			unsigned int size;
			unsigned int *revmap;
		} linear;
		struct radix_tree_root tree;
	} revmap_data;
	struct irq_domain_ops *ops;
	void *host_data;
	irq_hw_number_t inval_irq;

	unsigned int irq_base;
	unsigned int nr_irq;
	unsigned int hwirq_base;

	/* Optional device node pointer */
	struct device_node *of_node;
};

#ifdef CONFIG_IRQ_DOMAIN
#ifdef CONFIG_PPC
extern struct irq_domain *irq_alloc_host(struct device_node *of_node,
				       unsigned int revmap_type,
				       unsigned int revmap_arg,
				       struct irq_domain_ops *ops,
				       irq_hw_number_t inval_irq);
extern struct irq_domain *irq_find_host(struct device_node *node);
extern void irq_set_default_host(struct irq_domain *host);
extern void irq_set_virq_count(unsigned int count);


extern unsigned int irq_create_mapping(struct irq_domain *host,
				       irq_hw_number_t hwirq);
extern void irq_dispose_mapping(unsigned int virq);
extern unsigned int irq_find_mapping(struct irq_domain *host,
				     irq_hw_number_t hwirq);
extern unsigned int irq_create_direct_mapping(struct irq_domain *host);
extern void irq_radix_revmap_insert(struct irq_domain *host, unsigned int virq,
				    irq_hw_number_t hwirq);
extern unsigned int irq_radix_revmap_lookup(struct irq_domain *host,
					    irq_hw_number_t hwirq);
extern unsigned int irq_linear_revmap(struct irq_domain *host,
				      irq_hw_number_t hwirq);

#else /* CONFIG_PPC */

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

extern struct irq_domain_ops irq_domain_simple_ops;

#if defined(CONFIG_OF_IRQ)
extern void irq_domain_add_simple(struct device_node *controller, int irq_base);
extern void irq_domain_generate_simple(const struct of_device_id *match,
					u64 phys_base, unsigned int irq_start);
#else /* CONFIG_OF_IRQ */
static inline void irq_domain_generate_simple(const struct of_device_id *match,
					u64 phys_base, unsigned int irq_start) { }
#endif /* !CONFIG_OF_IRQ */
#endif /* !CONFIG_PPC */
#endif /* CONFIG_IRQ_DOMAIN */

#endif /* _LINUX_IRQDOMAIN_H */
