#ifndef M68KNOMMU_PCI_H
#define	M68KNOMMU_PCI_H

#include <asm-m68k/pci.h>

#ifdef CONFIG_COMEMPCI
/*
 *	These are pretty much arbitary with the CoMEM implementation.
 *	We have the whole address space to ourselves.
 */
#define PCIBIOS_MIN_IO		0x100
#define PCIBIOS_MIN_MEM		0x00010000

#define pcibios_scan_all_fns(a, b)	0

/*
 * Return whether the given PCI device DMA address mask can
 * be supported properly.  For example, if your device can
 * only drive the low 24-bits during PCI bus mastering, then
 * you would pass 0x00ffffff as the mask to this function.
 */
static inline int pci_dma_supported(struct pci_dev *hwdev, u64 mask)
{
	return 1;
}

/*
 * Not supporting more than 32-bit PCI bus addresses now, but
 * must satisfy references to this function.  Change if needed.
 */
#define pci_dac_dma_supported(pci_dev, mask) (0)

static inline void pcibios_add_platform_entries(struct pci_dev *dev)
{
}

#endif /* CONFIG_COMEMPCI */

#endif /* M68KNOMMU_PCI_H */
