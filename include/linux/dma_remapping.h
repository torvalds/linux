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

/*
 * 0: readable
 * 1: writable
 * 2-6: reserved
 * 7: super page
 * 8-11: available
 * 12-63: Host physcial address
 */
struct dma_pte {
	u64 val;
};
#define dma_clear_pte(p)	do {(p).val = 0;} while (0)

#define DMA_PTE_READ (1)
#define DMA_PTE_WRITE (2)

#define dma_set_pte_readable(p) do {(p).val |= DMA_PTE_READ;} while (0)
#define dma_set_pte_writable(p) do {(p).val |= DMA_PTE_WRITE;} while (0)
#define dma_set_pte_prot(p, prot) \
		do {(p).val = ((p).val & ~3) | ((prot) & 3); } while (0)
#define dma_pte_addr(p) ((p).val & VTD_PAGE_MASK)
#define dma_set_pte_addr(p, addr) do {\
		(p).val |= ((addr) & VTD_PAGE_MASK); } while (0)
#define dma_pte_present(p) (((p).val & 3) != 0)

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
