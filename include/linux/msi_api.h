/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_MSI_API_H
#define LINUX_MSI_API_H

/*
 * APIs which are relevant for device driver code for allocating and
 * freeing MSI interrupts and querying the associations between
 * hardware/software MSI indices and the Linux interrupt number.
 */

struct device;

/*
 * Per device interrupt domain related constants.
 */
enum msi_domain_ids {
	MSI_DEFAULT_DOMAIN,
	MSI_MAX_DEVICE_IRQDOMAINS,
};

unsigned int msi_domain_get_virq(struct device *dev, unsigned int domid, unsigned int index);

/**
 * msi_get_virq - Lookup the Linux interrupt number for a MSI index on the default interrupt domain
 * @dev:	Device for which the lookup happens
 * @index:	The MSI index to lookup
 *
 * Return: The Linux interrupt number on success (> 0), 0 if not found
 */
static inline unsigned int msi_get_virq(struct device *dev, unsigned int index)
{
	return msi_domain_get_virq(dev, MSI_DEFAULT_DOMAIN, index);
}

#endif
