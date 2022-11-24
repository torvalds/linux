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

unsigned int msi_get_virq(struct device *dev, unsigned int index);

#endif
