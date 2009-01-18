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

extern void free_dmar_iommu(struct intel_iommu *iommu);

#ifdef CONFIG_DMAR
extern int iommu_calculate_agaw(struct intel_iommu *iommu);
#else
static inline int iommu_calculate_agaw(struct intel_iommu *iommu)
{
	return 0;
}
#endif

extern int dmar_disabled;

#endif
