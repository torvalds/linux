#ifndef _DMA_REMAPPING_H
#define _DMA_REMAPPING_H

/*
 * VT-d hardware uses 4KiB page size regardless of host page size.
 */
#define VTD_PAGE_SHIFT		(12)
#define VTD_PAGE_SIZE		(1UL << VTD_PAGE_SHIFT)
#define VTD_PAGE_MASK		(((u64)-1) << VTD_PAGE_SHIFT)
#define VTD_PAGE_ALIGN(addr)	(((addr) + VTD_PAGE_SIZE - 1) & VTD_PAGE_MASK)

struct root_entry;

#define DMA_PTE_READ (1)
#define DMA_PTE_WRITE (2)

struct intel_iommu;

struct dmar_domain {
	int	id;			/* domain id */
	struct intel_iommu *iommu;	/* back pointer to owning iommu */

	struct list_head devices; 	/* all devices' list */
	struct iova_domain iovad;	/* iova's that belong to this domain */

	struct dma_pte	*pgd;		/* virtual address */
	spinlock_t	mapping_lock;	/* page table lock */
	int		gaw;		/* max guest address width */

	/* adjusted guest address width, 0 is level 2 30-bit */
	int		agaw;

#define DOMAIN_FLAG_MULTIPLE_DEVICES 1
	int		flags;
};

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
