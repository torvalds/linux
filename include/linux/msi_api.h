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

/**
 * union msi_instance_cookie - MSI instance cookie
 * @value:	u64 value store
 * @ptr:	Pointer to usage site specific data
 *
 * This cookie is handed to the IMS allocation function and stored in the
 * MSI descriptor for the interrupt chip callbacks.
 *
 * The content of this cookie is MSI domain implementation defined.  For
 * PCI/IMS implementations this could be a PASID or a pointer to queue
 * memory.
 */
union msi_instance_cookie {
	u64	value;
	void	*ptr;
};

/**
 * msi_map - Mapping between MSI index and Linux interrupt number
 * @index:	The MSI index, e.g. slot in the MSI-X table or
 *		a software managed index if >= 0. If negative
 *		the allocation function failed and it contains
 *		the error code.
 * @virq:	The associated Linux interrupt number
 */
struct msi_map {
	int	index;
	int	virq;
};

/*
 * Constant to be used for dynamic allocations when the allocation is any
 * free MSI index, which is either an entry in a hardware table or a
 * software managed index.
 */
#define MSI_ANY_INDEX		UINT_MAX

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
