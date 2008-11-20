#ifndef _DMA_REMAPPING_H
#define _DMA_REMAPPING_H

/*
 * VT-d hardware uses 4KiB page size regardless of host page size.
 */
#define VTD_PAGE_SHIFT		(12)
#define VTD_PAGE_SIZE		(1UL << VTD_PAGE_SHIFT)
#define VTD_PAGE_MASK		(((u64)-1) << VTD_PAGE_SHIFT)
#define VTD_PAGE_ALIGN(addr)	(((addr) + VTD_PAGE_SIZE - 1) & VTD_PAGE_MASK)

#define DMA_PTE_READ (1)
#define DMA_PTE_WRITE (2)

struct intel_iommu;
struct dmar_domain;
struct root_entry;

/* PCI domain-device relationship */
struct device_domain_info {
	struct list_head link;	/* link to domain siblings */
	struct list_head global; /* link to global list */
	u8 bus;			/* PCI bus numer */
	u8 devfn;		/* PCI devfn number */
	struct pci_dev *dev; /* it's NULL for PCIE-to-PCI bridge */
	struct dmar_domain *domain; /* pointer to domain */
};

extern void free_dmar_iommu(struct intel_iommu *iommu);

extern int dmar_disabled;

#ifndef CONFIG_DMAR_GFX_WA
static inline void iommu_prepare_gfx_mapping(void)
{
	return;
}
#endif /* !CONFIG_DMAR_GFX_WA */

#endif
