// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2014 Intel Corp.
 * Author: Jiang Liu <jiang.liu@linux.intel.com>
 *
 * This file is licensed under GPLv2.
 *
 * This file contains common code to support Message Signalled Interrupt for
 * PCI compatible and non PCI compatible devices.
 */
#include <linux/types.h>
#include <linux/device.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/msi.h>
#include <linux/slab.h>

#include "internals.h"

/**
 * alloc_msi_entry - Allocate an initialize msi_entry
 * @dev:	Pointer to the device for which this is allocated
 * @nvec:	The number of vectors used in this entry
 * @affinity:	Optional pointer to an affinity mask array size of @nvec
 *
 * If @affinity is not NULL then a an affinity array[@nvec] is allocated
 * and the affinity masks from @affinity are copied.
 */
struct msi_desc *
alloc_msi_entry(struct device *dev, int nvec, const struct cpumask *affinity)
{
	struct msi_desc *desc;

	desc = kzalloc(sizeof(*desc), GFP_KERNEL);
	if (!desc)
		return NULL;

	INIT_LIST_HEAD(&desc->list);
	desc->dev = dev;
	desc->nvec_used = nvec;
	if (affinity) {
		desc->affinity = kmemdup(affinity,
			nvec * sizeof(*desc->affinity), GFP_KERNEL);
		if (!desc->affinity) {
			kfree(desc);
			return NULL;
		}
	}

	return desc;
}

void free_msi_entry(struct msi_desc *entry)
{
	kfree(entry->affinity);
	kfree(entry);
}

void __get_cached_msi_msg(struct msi_desc *entry, struct msi_msg *msg)
{
	*msg = entry->msg;
}

void get_cached_msi_msg(unsigned int irq, struct msi_msg *msg)
{
	struct msi_desc *entry = irq_get_msi_desc(irq);

	__get_cached_msi_msg(entry, msg);
}
EXPORT_SYMBOL_GPL(get_cached_msi_msg);

#ifdef CONFIG_GENERIC_MSI_IRQ_DOMAIN
static inline void irq_chip_write_msi_msg(struct irq_data *data,
					  struct msi_msg *msg)
{
	data->chip->irq_write_msi_msg(data, msg);
}

static void msi_check_level(struct irq_domain *domain, struct msi_msg *msg)
{
	struct msi_domain_info *info = domain->host_data;

	/*
	 * If the MSI provider has messed with the second message and
	 * not advertized that it is level-capable, signal the breakage.
	 */
	WARN_ON(!((info->flags & MSI_FLAG_LEVEL_CAPABLE) &&
		  (info->chip->flags & IRQCHIP_SUPPORTS_LEVEL_MSI)) &&
		(msg[1].address_lo || msg[1].address_hi || msg[1].data));
}

/**
 * msi_domain_set_affinity - Generic affinity setter function for MSI domains
 * @irq_data:	The irq data associated to the interrupt
 * @mask:	The affinity mask to set
 * @force:	Flag to enforce setting (disable online checks)
 *
 * Intended to be used by MSI interrupt controllers which are
 * implemented with hierarchical domains.
 */
int msi_domain_set_affinity(struct irq_data *irq_data,
			    const struct cpumask *mask, bool force)
{
	struct irq_data *parent = irq_data->parent_data;
	struct msi_msg msg[2] = { [1] = { }, };
	int ret;

	ret = parent->chip->irq_set_affinity(parent, mask, force);
	if (ret >= 0 && ret != IRQ_SET_MASK_OK_DONE) {
		BUG_ON(irq_chip_compose_msi_msg(irq_data, msg));
		msi_check_level(irq_data->domain, msg);
		irq_chip_write_msi_msg(irq_data, msg);
	}

	return ret;
}

static int msi_domain_activate(struct irq_domain *domain,
			       struct irq_data *irq_data, bool early)
{
	struct msi_msg msg[2] = { [1] = { }, };

	BUG_ON(irq_chip_compose_msi_msg(irq_data, msg));
	msi_check_level(irq_data->domain, msg);
	irq_chip_write_msi_msg(irq_data, msg);
	return 0;
}

static void msi_domain_deactivate(struct irq_domain *domain,
				  struct irq_data *irq_data)
{
	struct msi_msg msg[2];

	memset(msg, 0, sizeof(msg));
	irq_chip_write_msi_msg(irq_data, msg);
}

static int msi_domain_alloc(struct irq_domain *domain, unsigned int virq,
			    unsigned int nr_irqs, void *arg)
{
	struct msi_domain_info *info = domain->host_data;
	struct msi_domain_ops *ops = info->ops;
	irq_hw_number_t hwirq = ops->get_hwirq(info, arg);
	int i, ret;

	if (irq_find_mapping(domain, hwirq) > 0)
		return -EEXIST;

	if (domain->parent) {
		ret = irq_domain_alloc_irqs_parent(domain, virq, nr_irqs, arg);
		if (ret < 0)
			return ret;
	}

	for (i = 0; i < nr_irqs; i++) {
		ret = ops->msi_init(domain, info, virq + i, hwirq + i, arg);
		if (ret < 0) {
			if (ops->msi_free) {
				for (i--; i > 0; i--)
					ops->msi_free(domain, info, virq + i);
			}
			irq_domain_free_irqs_top(domain, virq, nr_irqs);
			return ret;
		}
	}

	return 0;
}

static void msi_domain_free(struct irq_domain *domain, unsigned int virq,
			    unsigned int nr_irqs)
{
	struct msi_domain_info *info = domain->host_data;
	int i;

	if (info->ops->msi_free) {
		for (i = 0; i < nr_irqs; i++)
			info->ops->msi_free(domain, info, virq + i);
	}
	irq_domain_free_irqs_top(domain, virq, nr_irqs);
}

static const struct irq_domain_ops msi_domain_ops = {
	.alloc		= msi_domain_alloc,
	.free		= msi_domain_free,
	.activate	= msi_domain_activate,
	.deactivate	= msi_domain_deactivate,
};

#ifdef GENERIC_MSI_DOMAIN_OPS
static irq_hw_number_t msi_domain_ops_get_hwirq(struct msi_domain_info *info,
						msi_alloc_info_t *arg)
{
	return arg->hwirq;
}

static int msi_domain_ops_prepare(struct irq_domain *domain, struct device *dev,
				  int nvec, msi_alloc_info_t *arg)
{
	memset(arg, 0, sizeof(*arg));
	return 0;
}

static void msi_domain_ops_set_desc(msi_alloc_info_t *arg,
				    struct msi_desc *desc)
{
	arg->desc = desc;
}
#else
#define msi_domain_ops_get_hwirq	NULL
#define msi_domain_ops_prepare		NULL
#define msi_domain_ops_set_desc		NULL
#endif /* !GENERIC_MSI_DOMAIN_OPS */

static int msi_domain_ops_init(struct irq_domain *domain,
			       struct msi_domain_info *info,
			       unsigned int virq, irq_hw_number_t hwirq,
			       msi_alloc_info_t *arg)
{
	irq_domain_set_hwirq_and_chip(domain, virq, hwirq, info->chip,
				      info->chip_data);
	if (info->handler && info->handler_name) {
		__irq_set_handler(virq, info->handler, 0, info->handler_name);
		if (info->handler_data)
			irq_set_handler_data(virq, info->handler_data);
	}
	return 0;
}

static int msi_domain_ops_check(struct irq_domain *domain,
				struct msi_domain_info *info,
				struct device *dev)
{
	return 0;
}

static struct msi_domain_ops msi_domain_ops_default = {
	.get_hwirq	= msi_domain_ops_get_hwirq,
	.msi_init	= msi_domain_ops_init,
	.msi_check	= msi_domain_ops_check,
	.msi_prepare	= msi_domain_ops_prepare,
	.set_desc	= msi_domain_ops_set_desc,
};

static void msi_domain_update_dom_ops(struct msi_domain_info *info)
{
	struct msi_domain_ops *ops = info->ops;

	if (ops == NULL) {
		info->ops = &msi_domain_ops_default;
		return;
	}

	if (ops->get_hwirq == NULL)
		ops->get_hwirq = msi_domain_ops_default.get_hwirq;
	if (ops->msi_init == NULL)
		ops->msi_init = msi_domain_ops_default.msi_init;
	if (ops->msi_check == NULL)
		ops->msi_check = msi_domain_ops_default.msi_check;
	if (ops->msi_prepare == NULL)
		ops->msi_prepare = msi_domain_ops_default.msi_prepare;
	if (ops->set_desc == NULL)
		ops->set_desc = msi_domain_ops_default.set_desc;
}

static void msi_domain_update_chip_ops(struct msi_domain_info *info)
{
	struct irq_chip *chip = info->chip;

	BUG_ON(!chip || !chip->irq_mask || !chip->irq_unmask);
	if (!chip->irq_set_affinity)
		chip->irq_set_affinity = msi_domain_set_affinity;
}

/**
 * msi_create_irq_domain - Create a MSI interrupt domain
 * @fwnode:	Optional fwnode of the interrupt controller
 * @info:	MSI domain info
 * @parent:	Parent irq domain
 */
struct irq_domain *msi_create_irq_domain(struct fwnode_handle *fwnode,
					 struct msi_domain_info *info,
					 struct irq_domain *parent)
{
	struct irq_domain *domain;

	if (info->flags & MSI_FLAG_USE_DEF_DOM_OPS)
		msi_domain_update_dom_ops(info);
	if (info->flags & MSI_FLAG_USE_DEF_CHIP_OPS)
		msi_domain_update_chip_ops(info);

	domain = irq_domain_create_hierarchy(parent, IRQ_DOMAIN_FLAG_MSI, 0,
					     fwnode, &msi_domain_ops, info);

	if (domain && !domain->name && info->chip)
		domain->name = info->chip->name;

	return domain;
}

int msi_domain_prepare_irqs(struct irq_domain *domain, struct device *dev,
			    int nvec, msi_alloc_info_t *arg)
{
	struct msi_domain_info *info = domain->host_data;
	struct msi_domain_ops *ops = info->ops;
	int ret;

	ret = ops->msi_check(domain, info, dev);
	if (ret == 0)
		ret = ops->msi_prepare(domain, dev, nvec, arg);

	return ret;
}

int msi_domain_populate_irqs(struct irq_domain *domain, struct device *dev,
			     int virq, int nvec, msi_alloc_info_t *arg)
{
	struct msi_domain_info *info = domain->host_data;
	struct msi_domain_ops *ops = info->ops;
	struct msi_desc *desc;
	int ret = 0;

	for_each_msi_entry(desc, dev) {
		/* Don't even try the multi-MSI brain damage. */
		if (WARN_ON(!desc->irq || desc->nvec_used != 1)) {
			ret = -EINVAL;
			break;
		}

		if (!(desc->irq >= virq && desc->irq < (virq + nvec)))
			continue;

		ops->set_desc(arg, desc);
		/* Assumes the domain mutex is held! */
		ret = irq_domain_alloc_irqs_hierarchy(domain, desc->irq, 1,
						      arg);
		if (ret)
			break;

		irq_set_msi_desc_off(desc->irq, 0, desc);
	}

	if (ret) {
		/* Mop up the damage */
		for_each_msi_entry(desc, dev) {
			if (!(desc->irq >= virq && desc->irq < (virq + nvec)))
				continue;

			irq_domain_free_irqs_common(domain, desc->irq, 1);
		}
	}

	return ret;
}

/*
 * Carefully check whether the device can use reservation mode. If
 * reservation mode is enabled then the early activation will assign a
 * dummy vector to the device. If the PCI/MSI device does not support
 * masking of the entry then this can result in spurious interrupts when
 * the device driver is not absolutely careful. But even then a malfunction
 * of the hardware could result in a spurious interrupt on the dummy vector
 * and render the device unusable. If the entry can be masked then the core
 * logic will prevent the spurious interrupt and reservation mode can be
 * used. For now reservation mode is restricted to PCI/MSI.
 */
static bool msi_check_reservation_mode(struct irq_domain *domain,
				       struct msi_domain_info *info,
				       struct device *dev)
{
	struct msi_desc *desc;

	if (domain->bus_token != DOMAIN_BUS_PCI_MSI)
		return false;

	if (!(info->flags & MSI_FLAG_MUST_REACTIVATE))
		return false;

	if (IS_ENABLED(CONFIG_PCI_MSI) && pci_msi_ignore_mask)
		return false;

	/*
	 * Checking the first MSI descriptor is sufficient. MSIX supports
	 * masking and MSI does so when the maskbit is set.
	 */
	desc = first_msi_entry(dev);
	return desc->msi_attrib.is_msix || desc->msi_attrib.maskbit;
}

/**
 * msi_domain_alloc_irqs - Allocate interrupts from a MSI interrupt domain
 * @domain:	The domain to allocate from
 * @dev:	Pointer to device struct of the device for which the interrupts
 *		are allocated
 * @nvec:	The number of interrupts to allocate
 *
 * Returns 0 on success or an error code.
 */
int msi_domain_alloc_irqs(struct irq_domain *domain, struct device *dev,
			  int nvec)
{
	struct msi_domain_info *info = domain->host_data;
	struct msi_domain_ops *ops = info->ops;
	struct irq_data *irq_data;
	struct msi_desc *desc;
	msi_alloc_info_t arg;
	int i, ret, virq;
	bool can_reserve;

	ret = msi_domain_prepare_irqs(domain, dev, nvec, &arg);
	if (ret)
		return ret;

	for_each_msi_entry(desc, dev) {
		ops->set_desc(&arg, desc);

		virq = __irq_domain_alloc_irqs(domain, -1, desc->nvec_used,
					       dev_to_node(dev), &arg, false,
					       desc->affinity);
		if (virq < 0) {
			ret = -ENOSPC;
			if (ops->handle_error)
				ret = ops->handle_error(domain, desc, ret);
			if (ops->msi_finish)
				ops->msi_finish(&arg, ret);
			return ret;
		}

		for (i = 0; i < desc->nvec_used; i++) {
			irq_set_msi_desc_off(virq, i, desc);
			irq_debugfs_copy_devname(virq + i, dev);
		}
	}

	if (ops->msi_finish)
		ops->msi_finish(&arg, 0);

	can_reserve = msi_check_reservation_mode(domain, info, dev);

	for_each_msi_entry(desc, dev) {
		virq = desc->irq;
		if (desc->nvec_used == 1)
			dev_dbg(dev, "irq %d for MSI\n", virq);
		else
			dev_dbg(dev, "irq [%d-%d] for MSI\n",
				virq, virq + desc->nvec_used - 1);
		/*
		 * This flag is set by the PCI layer as we need to activate
		 * the MSI entries before the PCI layer enables MSI in the
		 * card. Otherwise the card latches a random msi message.
		 */
		if (!(info->flags & MSI_FLAG_ACTIVATE_EARLY))
			continue;

		irq_data = irq_domain_get_irq_data(domain, desc->irq);
		if (!can_reserve)
			irqd_clr_can_reserve(irq_data);
		ret = irq_domain_activate_irq(irq_data, can_reserve);
		if (ret)
			goto cleanup;
	}

	/*
	 * If these interrupts use reservation mode, clear the activated bit
	 * so request_irq() will assign the final vector.
	 */
	if (can_reserve) {
		for_each_msi_entry(desc, dev) {
			irq_data = irq_domain_get_irq_data(domain, desc->irq);
			irqd_clr_activated(irq_data);
		}
	}
	return 0;

cleanup:
	for_each_msi_entry(desc, dev) {
		struct irq_data *irqd;

		if (desc->irq == virq)
			break;

		irqd = irq_domain_get_irq_data(domain, desc->irq);
		if (irqd_is_activated(irqd))
			irq_domain_deactivate_irq(irqd);
	}
	msi_domain_free_irqs(domain, dev);
	return ret;
}

/**
 * msi_domain_free_irqs - Free interrupts from a MSI interrupt @domain associated tp @dev
 * @domain:	The domain to managing the interrupts
 * @dev:	Pointer to device struct of the device for which the interrupts
 *		are free
 */
void msi_domain_free_irqs(struct irq_domain *domain, struct device *dev)
{
	struct msi_desc *desc;

	for_each_msi_entry(desc, dev) {
		/*
		 * We might have failed to allocate an MSI early
		 * enough that there is no IRQ associated to this
		 * entry. If that's the case, don't do anything.
		 */
		if (desc->irq) {
			irq_domain_free_irqs(desc->irq, desc->nvec_used);
			desc->irq = 0;
		}
	}
}

/**
 * msi_get_domain_info - Get the MSI interrupt domain info for @domain
 * @domain:	The interrupt domain to retrieve data from
 *
 * Returns the pointer to the msi_domain_info stored in
 * @domain->host_data.
 */
struct msi_domain_info *msi_get_domain_info(struct irq_domain *domain)
{
	return (struct msi_domain_info *)domain->host_data;
}

#endif /* CONFIG_GENERIC_MSI_IRQ_DOMAIN */
