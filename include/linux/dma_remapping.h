#ifndef _DMA_REMAPPING_H
#define _DMA_REMAPPING_H

/*
 * VT-d hardware uses 4KiB page size regardless of host page size.
 */
#define VTD_PAGE_SHIFT		(12)
#define VTD_PAGE_SIZE		(1UL << VTD_PAGE_SHIFT)
#define VTD_PAGE_MASK		(((u64)-1) << VTD_PAGE_SHIFT)
#define VTD_PAGE_ALIGN(addr)	(((addr) + VTD_PAGE_SIZE - 1) & VTD_PAGE_MASK)

#define VTD_STRIDE_SHIFT        (9)
#define VTD_STRIDE_MASK         (((u64)-1) << VTD_STRIDE_SHIFT)

#define DMA_PTE_READ (1)
#define DMA_PTE_WRITE (2)
#define DMA_PTE_LARGE_PAGE (1 << 7)
#define DMA_PTE_SNP (1 << 11)

#define CONTEXT_TT_MULTI_LEVEL	0
#define CONTEXT_TT_DEV_IOTLB	1
#define CONTEXT_TT_PASS_THROUGH 2

struct intel_iommu;
struct dmar_domain;
struct root_entry;


#ifdef CONFIG_INTEL_IOMMU
extern void free_dmar_iommu(struct intel_iommu *iommu);
extern int iommu_calculate_agaw(struct intel_iommu *iommu);
extern int iommu_calculate_max_sagaw(struct intel_iommu *iommu);
extern int dmar_disabled;
extern int intel_iommu_enabled;
#else
static inline int iommu_calculate_agaw(struct intel_iommu *iommu)
{
	return 0;
}
static inline int iommu_calculate_max_sagaw(struct intel_iommu *iommu)
{
	return 0;
}
static inline void free_dmar_iommu(struct intel_iommu *iommu)
{
}
#define dmar_disabled	(1)
#define intel_iommu_enabled (0)
#endif


#endif
