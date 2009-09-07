/*
 * linux/include/asm-generic/pci.h
 *
 *  Copyright (C) 2003 Russell King
 */
#ifndef _ASM_GENERIC_PCI_H
#define _ASM_GENERIC_PCI_H

/**
 * pcibios_resource_to_bus - convert resource to PCI bus address
 * @dev: device which owns this resource
 * @region: converted bus-centric region (start,end)
 * @res: resource to convert
 *
 * Convert a resource to a PCI device bus address or bus window.
 */
static inline void
pcibios_resource_to_bus(struct pci_dev *dev, struct pci_bus_region *region,
			 struct resource *res)
{
	region->start = res->start;
	region->end = res->end;
}

static inline void
pcibios_bus_to_resource(struct pci_dev *dev, struct resource *res,
			struct pci_bus_region *region)
{
	res->start = region->start;
	res->end = region->end;
}

#define pcibios_scan_all_fns(a, b)	0

#ifndef HAVE_ARCH_PCI_GET_LEGACY_IDE_IRQ
static inline int pci_get_legacy_ide_irq(struct pci_dev *dev, int channel)
{
	return channel ? 15 : 14;
}
#endif /* HAVE_ARCH_PCI_GET_LEGACY_IDE_IRQ */

/*
 * By default, assume that no iommu is in use and that the PCI
 * space is mapped to address physical 0.
 */
#ifndef PCI_DMA_BUS_IS_PHYS
#define PCI_DMA_BUS_IS_PHYS	(1)
#endif

#endif /* _ASM_GENERIC_PCI_H */
