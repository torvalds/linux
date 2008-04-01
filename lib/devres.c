#include <linux/pci.h>
#include <linux/io.h>
#include <linux/module.h>

static void devm_ioremap_release(struct device *dev, void *res)
{
	iounmap(*(void __iomem **)res);
}

static int devm_ioremap_match(struct device *dev, void *res, void *match_data)
{
	return *(void **)res == match_data;
}

/**
 * devm_ioremap - Managed ioremap()
 * @dev: Generic device to remap IO address for
 * @offset: BUS offset to map
 * @size: Size of map
 *
 * Managed ioremap().  Map is automatically unmapped on driver detach.
 */
void __iomem *devm_ioremap(struct device *dev, unsigned long offset,
			   unsigned long size)
{
	void __iomem **ptr, *addr;

	ptr = devres_alloc(devm_ioremap_release, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return NULL;

	addr = ioremap(offset, size);
	if (addr) {
		*ptr = addr;
		devres_add(dev, ptr);
	} else
		devres_free(ptr);

	return addr;
}
EXPORT_SYMBOL(devm_ioremap);

/**
 * devm_ioremap_nocache - Managed ioremap_nocache()
 * @dev: Generic device to remap IO address for
 * @offset: BUS offset to map
 * @size: Size of map
 *
 * Managed ioremap_nocache().  Map is automatically unmapped on driver
 * detach.
 */
void __iomem *devm_ioremap_nocache(struct device *dev, unsigned long offset,
				   unsigned long size)
{
	void __iomem **ptr, *addr;

	ptr = devres_alloc(devm_ioremap_release, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return NULL;

	addr = ioremap_nocache(offset, size);
	if (addr) {
		*ptr = addr;
		devres_add(dev, ptr);
	} else
		devres_free(ptr);

	return addr;
}
EXPORT_SYMBOL(devm_ioremap_nocache);

/**
 * devm_iounmap - Managed iounmap()
 * @dev: Generic device to unmap for
 * @addr: Address to unmap
 *
 * Managed iounmap().  @addr must have been mapped using devm_ioremap*().
 */
void devm_iounmap(struct device *dev, void __iomem *addr)
{
	iounmap(addr);
	WARN_ON(devres_destroy(dev, devm_ioremap_release, devm_ioremap_match,
			       (void *)addr));
}
EXPORT_SYMBOL(devm_iounmap);

#ifdef CONFIG_HAS_IOPORT
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
 */
void __iomem * devm_ioport_map(struct device *dev, unsigned long port,
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
			       devm_ioport_map_match, (void *)addr));
}
EXPORT_SYMBOL(devm_ioport_unmap);

#ifdef CONFIG_PCI
/*
 * PCI iomap devres
 */
#define PCIM_IOMAP_MAX	PCI_ROM_RESOURCE

struct pcim_iomap_devres {
	void __iomem *table[PCIM_IOMAP_MAX];
};

static void pcim_iomap_release(struct device *gendev, void *res)
{
	struct pci_dev *dev = container_of(gendev, struct pci_dev, dev);
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
void __iomem * const * pcim_iomap_table(struct pci_dev *pdev)
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
void __iomem * pcim_iomap(struct pci_dev *pdev, int bar, unsigned long maxlen)
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
int pcim_iomap_regions(struct pci_dev *pdev, u16 mask, const char *name)
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
int pcim_iomap_regions_request_all(struct pci_dev *pdev, u16 mask,
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
 * Unamp and release regions specified by @mask.
 */
void pcim_iounmap_regions(struct pci_dev *pdev, u16 mask)
{
	void __iomem * const *iomap;
	int i;

	iomap = pcim_iomap_table(pdev);
	if (!iomap)
		return;

	for (i = 0; i < DEVICE_COUNT_RESOURCE; i++) {
		if (!(mask & (1 << i)))
			continue;

		pcim_iounmap(pdev, iomap[i]);
		pci_release_region(pdev, i);
	}
}
EXPORT_SYMBOL(pcim_iounmap_regions);
#endif
#endif
