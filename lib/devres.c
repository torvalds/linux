// SPDX-License-Identifier: GPL-2.0
#include <linux/err.h>
#include <linux/pci.h>
#include <linux/io.h>
#include <linux/gfp.h>
#include <linux/export.h>
#include <linux/of_address.h>

enum devm_ioremap_type {
	DEVM_IOREMAP = 0,
	DEVM_IOREMAP_UC,
	DEVM_IOREMAP_WC,
	DEVM_IOREMAP_NP,
};

void devm_ioremap_release(struct device *dev, void *res)
{
	iounmap(*(void __iomem **)res);
}

static int devm_ioremap_match(struct device *dev, void *res, void *match_data)
{
	return *(void **)res == match_data;
}

static void __iomem *__devm_ioremap(struct device *dev, resource_size_t offset,
				    resource_size_t size,
				    enum devm_ioremap_type type)
{
	void __iomem **ptr, *addr = NULL;

	ptr = devres_alloc(devm_ioremap_release, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return NULL;

	switch (type) {
	case DEVM_IOREMAP:
		addr = ioremap(offset, size);
		break;
	case DEVM_IOREMAP_UC:
		addr = ioremap_uc(offset, size);
		break;
	case DEVM_IOREMAP_WC:
		addr = ioremap_wc(offset, size);
		break;
	case DEVM_IOREMAP_NP:
		addr = ioremap_np(offset, size);
		break;
	}

	if (addr) {
		*ptr = addr;
		devres_add(dev, ptr);
	} else
		devres_free(ptr);

	return addr;
}

/**
 * devm_ioremap - Managed ioremap()
 * @dev: Generic device to remap IO address for
 * @offset: Resource address to map
 * @size: Size of map
 *
 * Managed ioremap().  Map is automatically unmapped on driver detach.
 */
void __iomem *devm_ioremap(struct device *dev, resource_size_t offset,
			   resource_size_t size)
{
	return __devm_ioremap(dev, offset, size, DEVM_IOREMAP);
}
EXPORT_SYMBOL(devm_ioremap);

/**
 * devm_ioremap_uc - Managed ioremap_uc()
 * @dev: Generic device to remap IO address for
 * @offset: Resource address to map
 * @size: Size of map
 *
 * Managed ioremap_uc().  Map is automatically unmapped on driver detach.
 */
void __iomem *devm_ioremap_uc(struct device *dev, resource_size_t offset,
			      resource_size_t size)
{
	return __devm_ioremap(dev, offset, size, DEVM_IOREMAP_UC);
}
EXPORT_SYMBOL_GPL(devm_ioremap_uc);

/**
 * devm_ioremap_wc - Managed ioremap_wc()
 * @dev: Generic device to remap IO address for
 * @offset: Resource address to map
 * @size: Size of map
 *
 * Managed ioremap_wc().  Map is automatically unmapped on driver detach.
 */
void __iomem *devm_ioremap_wc(struct device *dev, resource_size_t offset,
			      resource_size_t size)
{
	return __devm_ioremap(dev, offset, size, DEVM_IOREMAP_WC);
}
EXPORT_SYMBOL(devm_ioremap_wc);

/**
 * devm_ioremap_np - Managed ioremap_np()
 * @dev: Generic device to remap IO address for
 * @offset: Resource address to map
 * @size: Size of map
 *
 * Managed ioremap_np().  Map is automatically unmapped on driver detach.
 */
void __iomem *devm_ioremap_np(struct device *dev, resource_size_t offset,
			      resource_size_t size)
{
	return __devm_ioremap(dev, offset, size, DEVM_IOREMAP_NP);
}
EXPORT_SYMBOL(devm_ioremap_np);

/**
 * devm_iounmap - Managed iounmap()
 * @dev: Generic device to unmap for
 * @addr: Address to unmap
 *
 * Managed iounmap().  @addr must have been mapped using devm_ioremap*().
 */
void devm_iounmap(struct device *dev, void __iomem *addr)
{
	WARN_ON(devres_destroy(dev, devm_ioremap_release, devm_ioremap_match,
			       (__force void *)addr));
	iounmap(addr);
}
EXPORT_SYMBOL(devm_iounmap);

static void __iomem *
__devm_ioremap_resource(struct device *dev, const struct resource *res,
			enum devm_ioremap_type type)
{
	resource_size_t size;
	void __iomem *dest_ptr;
	char *pretty_name;

	BUG_ON(!dev);

	if (!res || resource_type(res) != IORESOURCE_MEM) {
		dev_err(dev, "invalid resource\n");
		return IOMEM_ERR_PTR(-EINVAL);
	}

	if (type == DEVM_IOREMAP && res->flags & IORESOURCE_MEM_NONPOSTED)
		type = DEVM_IOREMAP_NP;

	size = resource_size(res);

	if (res->name)
		pretty_name = devm_kasprintf(dev, GFP_KERNEL, "%s %s",
					     dev_name(dev), res->name);
	else
		pretty_name = devm_kstrdup(dev, dev_name(dev), GFP_KERNEL);
	if (!pretty_name) {
		dev_err(dev, "can't generate pretty name for resource %pR\n", res);
		return IOMEM_ERR_PTR(-ENOMEM);
	}

	if (!devm_request_mem_region(dev, res->start, size, pretty_name)) {
		dev_err(dev, "can't request region for resource %pR\n", res);
		return IOMEM_ERR_PTR(-EBUSY);
	}

	dest_ptr = __devm_ioremap(dev, res->start, size, type);
	if (!dest_ptr) {
		dev_err(dev, "ioremap failed for resource %pR\n", res);
		devm_release_mem_region(dev, res->start, size);
		dest_ptr = IOMEM_ERR_PTR(-ENOMEM);
	}

	return dest_ptr;
}

/**
 * devm_ioremap_resource() - check, request region, and ioremap resource
 * @dev: generic device to handle the resource for
 * @res: resource to be handled
 *
 * Checks that a resource is a valid memory region, requests the memory
 * region and ioremaps it. All operations are managed and will be undone
 * on driver detach.
 *
 * Usage example:
 *
 *	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
 *	base = devm_ioremap_resource(&pdev->dev, res);
 *	if (IS_ERR(base))
 *		return PTR_ERR(base);
 *
 * Return: a pointer to the remapped memory or an ERR_PTR() encoded error code
 * on failure.
 */
void __iomem *devm_ioremap_resource(struct device *dev,
				    const struct resource *res)
{
	return __devm_ioremap_resource(dev, res, DEVM_IOREMAP);
}
EXPORT_SYMBOL(devm_ioremap_resource);

/**
 * devm_ioremap_resource_wc() - write-combined variant of
 *				devm_ioremap_resource()
 * @dev: generic device to handle the resource for
 * @res: resource to be handled
 *
 * Return: a pointer to the remapped memory or an ERR_PTR() encoded error code
 * on failure.
 */
void __iomem *devm_ioremap_resource_wc(struct device *dev,
				       const struct resource *res)
{
	return __devm_ioremap_resource(dev, res, DEVM_IOREMAP_WC);
}

/*
 * devm_of_iomap - Requests a resource and maps the memory mapped IO
 *		   for a given device_node managed by a given device
 *
 * Checks that a resource is a valid memory region, requests the memory
 * region and ioremaps it. All operations are managed and will be undone
 * on driver detach of the device.
 *
 * This is to be used when a device requests/maps resources described
 * by other device tree nodes (children or otherwise).
 *
 * @dev:	The device "managing" the resource
 * @node:       The device-tree node where the resource resides
 * @index:	index of the MMIO range in the "reg" property
 * @size:	Returns the size of the resource (pass NULL if not needed)
 *
 * Usage example:
 *
 *	base = devm_of_iomap(&pdev->dev, node, 0, NULL);
 *	if (IS_ERR(base))
 *		return PTR_ERR(base);
 *
 * Please Note: This is not a one-to-one replacement for of_iomap() because the
 * of_iomap() function does not track whether the region is already mapped.  If
 * two drivers try to map the same memory, the of_iomap() function will succeed
 * but the devm_of_iomap() function will return -EBUSY.
 *
 * Return: a pointer to the requested and mapped memory or an ERR_PTR() encoded
 * error code on failure.
 */
void __iomem *devm_of_iomap(struct device *dev, struct device_node *node, int index,
			    resource_size_t *size)
{
	struct resource res;

	if (of_address_to_resource(node, index, &res))
		return IOMEM_ERR_PTR(-EINVAL);
	if (size)
		*size = resource_size(&res);
	return devm_ioremap_resource(dev, &res);
}
EXPORT_SYMBOL(devm_of_iomap);

#ifdef CONFIG_HAS_IOPORT_MAP
/*
 * Generic iomap devres
 */
static void devm_ioport_map_release(struct device *dev, void *res)
{
	ioport_unmap(*(void __iomem **)res);
}

static int devm_ioport_map_match(struct device *dev, void *res,
				 void *match_data)
{
	return *(void **)res == match_data;
}

/**
 * devm_ioport_map - Managed ioport_map()
 * @dev: Generic device to map ioport for
 * @port: Port to map
 * @nr: Number of ports to map
 *
 * Managed ioport_map().  Map is automatically unmapped on driver
 * detach.
 *
 * Return: a pointer to the remapped memory or NULL on failure.
 */
void __iomem *devm_ioport_map(struct device *dev, unsigned long port,
			       unsigned int nr)
{
	void __iomem **ptr, *addr;

	ptr = devres_alloc(devm_ioport_map_release, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return NULL;

	addr = ioport_map(port, nr);
	if (addr) {
		*ptr = addr;
		devres_add(dev, ptr);
	} else
		devres_free(ptr);

	return addr;
}
EXPORT_SYMBOL(devm_ioport_map);

/**
 * devm_ioport_unmap - Managed ioport_unmap()
 * @dev: Generic device to unmap for
 * @addr: Address to unmap
 *
 * Managed ioport_unmap().  @addr must have been mapped using
 * devm_ioport_map().
 */
void devm_ioport_unmap(struct device *dev, void __iomem *addr)
{
	ioport_unmap(addr);
	WARN_ON(devres_destroy(dev, devm_ioport_map_release,
			       devm_ioport_map_match, (__force void *)addr));
}
EXPORT_SYMBOL(devm_ioport_unmap);
#endif /* CONFIG_HAS_IOPORT_MAP */

#ifdef CONFIG_PCI
/*
 * PCI iomap devres
 */
#define PCIM_IOMAP_MAX	PCI_STD_NUM_BARS

struct pcim_iomap_devres {
	void __iomem *table[PCIM_IOMAP_MAX];
};

static void pcim_iomap_release(struct device *gendev, void *res)
{
	struct pci_dev *dev = to_pci_dev(gendev);
	struct pcim_iomap_devres *this = res;
	int i;

	for (i = 0; i < PCIM_IOMAP_MAX; i++)
		if (this->table[i])
			pci_iounmap(dev, this->table[i]);
}

/**
 * pcim_iomap_table - access iomap allocation table
 * @pdev: PCI device to access iomap table for
 *
 * Access iomap allocation table for @dev.  If iomap table doesn't
 * exist and @pdev is managed, it will be allocated.  All iomaps
 * recorded in the iomap table are automatically unmapped on driver
 * detach.
 *
 * This function might sleep when the table is first allocated but can
 * be safely called without context and guaranteed to succed once
 * allocated.
 */
void __iomem * const *pcim_iomap_table(struct pci_dev *pdev)
{
	struct pcim_iomap_devres *dr, *new_dr;

	dr = devres_find(&pdev->dev, pcim_iomap_release, NULL, NULL);
	if (dr)
		return dr->table;

	new_dr = devres_alloc(pcim_iomap_release, sizeof(*new_dr), GFP_KERNEL);
	if (!new_dr)
		return NULL;
	dr = devres_get(&pdev->dev, new_dr, NULL, NULL);
	return dr->table;
}
EXPORT_SYMBOL(pcim_iomap_table);

/**
 * pcim_iomap - Managed pcim_iomap()
 * @pdev: PCI device to iomap for
 * @bar: BAR to iomap
 * @maxlen: Maximum length of iomap
 *
 * Managed pci_iomap().  Map is automatically unmapped on driver
 * detach.
 */
void __iomem *pcim_iomap(struct pci_dev *pdev, int bar, unsigned long maxlen)
{
	void __iomem **tbl;

	BUG_ON(bar >= PCIM_IOMAP_MAX);

	tbl = (void __iomem **)pcim_iomap_table(pdev);
	if (!tbl || tbl[bar])	/* duplicate mappings not allowed */
		return NULL;

	tbl[bar] = pci_iomap(pdev, bar, maxlen);
	return tbl[bar];
}
EXPORT_SYMBOL(pcim_iomap);

/**
 * pcim_iounmap - Managed pci_iounmap()
 * @pdev: PCI device to iounmap for
 * @addr: Address to unmap
 *
 * Managed pci_iounmap().  @addr must have been mapped using pcim_iomap().
 */
void pcim_iounmap(struct pci_dev *pdev, void __iomem *addr)
{
	void __iomem **tbl;
	int i;

	pci_iounmap(pdev, addr);

	tbl = (void __iomem **)pcim_iomap_table(pdev);
	BUG_ON(!tbl);

	for (i = 0; i < PCIM_IOMAP_MAX; i++)
		if (tbl[i] == addr) {
			tbl[i] = NULL;
			return;
		}
	WARN_ON(1);
}
EXPORT_SYMBOL(pcim_iounmap);

/**
 * pcim_iomap_regions - Request and iomap PCI BARs
 * @pdev: PCI device to map IO resources for
 * @mask: Mask of BARs to request and iomap
 * @name: Name used when requesting regions
 *
 * Request and iomap regions specified by @mask.
 */
int pcim_iomap_regions(struct pci_dev *pdev, int mask, const char *name)
{
	void __iomem * const *iomap;
	int i, rc;

	iomap = pcim_iomap_table(pdev);
	if (!iomap)
		return -ENOMEM;

	for (i = 0; i < DEVICE_COUNT_RESOURCE; i++) {
		unsigned long len;

		if (!(mask & (1 << i)))
			continue;

		rc = -EINVAL;
		len = pci_resource_len(pdev, i);
		if (!len)
			goto err_inval;

		rc = pci_request_region(pdev, i, name);
		if (rc)
			goto err_inval;

		rc = -ENOMEM;
		if (!pcim_iomap(pdev, i, 0))
			goto err_region;
	}

	return 0;

 err_region:
	pci_release_region(pdev, i);
 err_inval:
	while (--i >= 0) {
		if (!(mask & (1 << i)))
			continue;
		pcim_iounmap(pdev, iomap[i]);
		pci_release_region(pdev, i);
	}

	return rc;
}
EXPORT_SYMBOL(pcim_iomap_regions);

/**
 * pcim_iomap_regions_request_all - Request all BARs and iomap specified ones
 * @pdev: PCI device to map IO resources for
 * @mask: Mask of BARs to iomap
 * @name: Name used when requesting regions
 *
 * Request all PCI BARs and iomap regions specified by @mask.
 */
int pcim_iomap_regions_request_all(struct pci_dev *pdev, int mask,
				   const char *name)
{
	int request_mask = ((1 << 6) - 1) & ~mask;
	int rc;

	rc = pci_request_selected_regions(pdev, request_mask, name);
	if (rc)
		return rc;

	rc = pcim_iomap_regions(pdev, mask, name);
	if (rc)
		pci_release_selected_regions(pdev, request_mask);
	return rc;
}
EXPORT_SYMBOL(pcim_iomap_regions_request_all);

/**
 * pcim_iounmap_regions - Unmap and release PCI BARs
 * @pdev: PCI device to map IO resources for
 * @mask: Mask of BARs to unmap and release
 *
 * Unmap and release regions specified by @mask.
 */
void pcim_iounmap_regions(struct pci_dev *pdev, int mask)
{
	void __iomem * const *iomap;
	int i;

	iomap = pcim_iomap_table(pdev);
	if (!iomap)
		return;

	for (i = 0; i < PCIM_IOMAP_MAX; i++) {
		if (!(mask & (1 << i)))
			continue;

		pcim_iounmap(pdev, iomap[i]);
		pci_release_region(pdev, i);
	}
}
EXPORT_SYMBOL(pcim_iounmap_regions);
#endif /* CONFIG_PCI */
