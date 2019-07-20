// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/gfp.h>
#include <linux/irq.h>

#include "internals.h"

/*
 * Device resource management aware IRQ request/free implementation.
 */
struct irq_devres {
	unsigned int irq;
	void *dev_id;
};

static void devm_irq_release(struct device *dev, void *res)
{
	struct irq_devres *this = res;

	free_irq(this->irq, this->dev_id);
}

static int devm_irq_match(struct device *dev, void *res, void *data)
{
	struct irq_devres *this = res, *match = data;

	return this->irq == match->irq && this->dev_id == match->dev_id;
}

/**
 *	devm_request_threaded_irq - allocate an interrupt line for a managed device
 *	@dev: device to request interrupt for
 *	@irq: Interrupt line to allocate
 *	@handler: Function to be called when the IRQ occurs
 *	@thread_fn: function to be called in a threaded interrupt context. NULL
 *		    for devices which handle everything in @handler
 *	@irqflags: Interrupt type flags
 *	@devname: An ascii name for the claiming device, dev_name(dev) if NULL
 *	@dev_id: A cookie passed back to the handler function
 *
 *	Except for the extra @dev argument, this function takes the
 *	same arguments and performs the same function as
 *	request_threaded_irq().  IRQs requested with this function will be
 *	automatically freed on driver detach.
 *
 *	If an IRQ allocated with this function needs to be freed
 *	separately, devm_free_irq() must be used.
 */
int devm_request_threaded_irq(struct device *dev, unsigned int irq,
			      irq_handler_t handler, irq_handler_t thread_fn,
			      unsigned long irqflags, const char *devname,
			      void *dev_id)
{
	struct irq_devres *dr;
	int rc;

	dr = devres_alloc(devm_irq_release, sizeof(struct irq_devres),
			  GFP_KERNEL);
	if (!dr)
		return -ENOMEM;

	if (!devname)
		devname = dev_name(dev);

	rc = request_threaded_irq(irq, handler, thread_fn, irqflags, devname,
				  dev_id);
	if (rc) {
		devres_free(dr);
		return rc;
	}

	dr->irq = irq;
	dr->dev_id = dev_id;
	devres_add(dev, dr);

	return 0;
}
EXPORT_SYMBOL(devm_request_threaded_irq);

/**
 *	devm_request_any_context_irq - allocate an interrupt line for a managed device
 *	@dev: device to request interrupt for
 *	@irq: Interrupt line to allocate
 *	@handler: Function to be called when the IRQ occurs
 *	@irqflags: Interrupt type flags
 *	@devname: An ascii name for the claiming device, dev_name(dev) if NULL
 *	@dev_id: A cookie passed back to the handler function
 *
 *	Except for the extra @dev argument, this function takes the
 *	same arguments and performs the same function as
 *	request_any_context_irq().  IRQs requested with this function will be
 *	automatically freed on driver detach.
 *
 *	If an IRQ allocated with this function needs to be freed
 *	separately, devm_free_irq() must be used.
 */
int devm_request_any_context_irq(struct device *dev, unsigned int irq,
			      irq_handler_t handler, unsigned long irqflags,
			      const char *devname, void *dev_id)
{
	struct irq_devres *dr;
	int rc;

	dr = devres_alloc(devm_irq_release, sizeof(struct irq_devres),
			  GFP_KERNEL);
	if (!dr)
		return -ENOMEM;

	if (!devname)
		devname = dev_name(dev);

	rc = request_any_context_irq(irq, handler, irqflags, devname, dev_id);
	if (rc < 0) {
		devres_free(dr);
		return rc;
	}

	dr->irq = irq;
	dr->dev_id = dev_id;
	devres_add(dev, dr);

	return rc;
}
EXPORT_SYMBOL(devm_request_any_context_irq);

/**
 *	devm_free_irq - free an interrupt
 *	@dev: device to free interrupt for
 *	@irq: Interrupt line to free
 *	@dev_id: Device identity to free
 *
 *	Except for the extra @dev argument, this function takes the
 *	same arguments and performs the same function as free_irq().
 *	This function instead of free_irq() should be used to manually
 *	free IRQs allocated with devm_request_irq().
 */
void devm_free_irq(struct device *dev, unsigned int irq, void *dev_id)
{
	struct irq_devres match_data = { irq, dev_id };

	WARN_ON(devres_destroy(dev, devm_irq_release, devm_irq_match,
			       &match_data));
	free_irq(irq, dev_id);
}
EXPORT_SYMBOL(devm_free_irq);

struct irq_desc_devres {
	unsigned int from;
	unsigned int cnt;
};

static void devm_irq_desc_release(struct device *dev, void *res)
{
	struct irq_desc_devres *this = res;

	irq_free_descs(this->from, this->cnt);
}

/**
 * __devm_irq_alloc_descs - Allocate and initialize a range of irq descriptors
 *			    for a managed device
 * @dev:	Device to allocate the descriptors for
 * @irq:	Allocate for specific irq number if irq >= 0
 * @from:	Start the search from this irq number
 * @cnt:	Number of consecutive irqs to allocate
 * @node:	Preferred node on which the irq descriptor should be allocated
 * @owner:	Owning module (can be NULL)
 * @affinity:	Optional pointer to an irq_affinity_desc array of size @cnt
 *		which hints where the irq descriptors should be allocated
 *		and which default affinities to use
 *
 * Returns the first irq number or error code.
 *
 * Note: Use the provided wrappers (devm_irq_alloc_desc*) for simplicity.
 */
int __devm_irq_alloc_descs(struct device *dev, int irq, unsigned int from,
			   unsigned int cnt, int node, struct module *owner,
			   const struct irq_affinity_desc *affinity)
{
	struct irq_desc_devres *dr;
	int base;

	dr = devres_alloc(devm_irq_desc_release, sizeof(*dr), GFP_KERNEL);
	if (!dr)
		return -ENOMEM;

	base = __irq_alloc_descs(irq, from, cnt, node, owner, affinity);
	if (base < 0) {
		devres_free(dr);
		return base;
	}

	dr->from = base;
	dr->cnt = cnt;
	devres_add(dev, dr);

	return base;
}
EXPORT_SYMBOL_GPL(__devm_irq_alloc_descs);

#ifdef CONFIG_GENERIC_IRQ_CHIP
/**
 * devm_irq_alloc_generic_chip - Allocate and initialize a generic chip
 *                               for a managed device
 * @dev:	Device to allocate the generic chip for
 * @name:	Name of the irq chip
 * @num_ct:	Number of irq_chip_type instances associated with this
 * @irq_base:	Interrupt base nr for this chip
 * @reg_base:	Register base address (virtual)
 * @handler:	Default flow handler associated with this chip
 *
 * Returns an initialized irq_chip_generic structure. The chip defaults
 * to the primary (index 0) irq_chip_type and @handler
 */
struct irq_chip_generic *
devm_irq_alloc_generic_chip(struct device *dev, const char *name, int num_ct,
			    unsigned int irq_base, void __iomem *reg_base,
			    irq_flow_handler_t handler)
{
	struct irq_chip_generic *gc;

	gc = devm_kzalloc(dev, struct_size(gc, chip_types, num_ct), GFP_KERNEL);
	if (gc)
		irq_init_generic_chip(gc, name, num_ct,
				      irq_base, reg_base, handler);

	return gc;
}
EXPORT_SYMBOL_GPL(devm_irq_alloc_generic_chip);

struct irq_generic_chip_devres {
	struct irq_chip_generic *gc;
	u32 msk;
	unsigned int clr;
	unsigned int set;
};

static void devm_irq_remove_generic_chip(struct device *dev, void *res)
{
	struct irq_generic_chip_devres *this = res;

	irq_remove_generic_chip(this->gc, this->msk, this->clr, this->set);
}

/**
 * devm_irq_setup_generic_chip - Setup a range of interrupts with a generic
 *                               chip for a managed device
 *
 * @dev:	Device to setup the generic chip for
 * @gc:		Generic irq chip holding all data
 * @msk:	Bitmask holding the irqs to initialize relative to gc->irq_base
 * @flags:	Flags for initialization
 * @clr:	IRQ_* bits to clear
 * @set:	IRQ_* bits to set
 *
 * Set up max. 32 interrupts starting from gc->irq_base. Note, this
 * initializes all interrupts to the primary irq_chip_type and its
 * associated handler.
 */
int devm_irq_setup_generic_chip(struct device *dev, struct irq_chip_generic *gc,
				u32 msk, enum irq_gc_flags flags,
				unsigned int clr, unsigned int set)
{
	struct irq_generic_chip_devres *dr;

	dr = devres_alloc(devm_irq_remove_generic_chip,
			  sizeof(*dr), GFP_KERNEL);
	if (!dr)
		return -ENOMEM;

	irq_setup_generic_chip(gc, msk, flags, clr, set);

	dr->gc = gc;
	dr->msk = msk;
	dr->clr = clr;
	dr->set = set;
	devres_add(dev, dr);

	return 0;
}
EXPORT_SYMBOL_GPL(devm_irq_setup_generic_chip);
#endif /* CONFIG_GENERIC_IRQ_CHIP */
