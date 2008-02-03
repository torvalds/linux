#ifndef _ASM_X8664_IOMMU_H
#define _ASM_X8664_IOMMU_H 1

extern void pci_iommu_shutdown(void);
extern void no_iommu_init(void);
extern int force_iommu, no_iommu;
extern int iommu_detected;
#ifdef CONFIG_GART_IOMMU
extern void gart_iommu_init(void);
extern void gart_iommu_shutdown(void);
extern void __init gart_parse_options(char *);
extern void early_gart_iommu_check(void);
extern void gart_iommu_hole_init(void);
extern int fallback_aper_order;
extern int fallback_aper_force;
extern int gart_iommu_aperture;
extern int gart_iommu_aperture_allowed;
extern int gart_iommu_aperture_disabled;
extern int fix_aperture;
#else
#define gart_iommu_aperture 0
#define gart_iommu_aperture_allowed 0

static inline void early_gart_iommu_check(void)
{
}

static inline void gart_iommu_shutdown(void)
{
}

#endif

#endif
