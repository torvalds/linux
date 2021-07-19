/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_PLAT_IOVMM_H
#define __ASM_PLAT_IOVMM_H

#include <linux/list.h>
#include <linux/atomic.h>
#include <linux/spinlock.h>

#define IEP_IOMMU_COMPATIBLE_NAME "rockchip,iep_mmu"
#define VIP_IOMMU_COMPATIBLE_NAME "rockchip,vip_mmu"
#define ISP_IOMMU_COMPATIBLE_NAME "rockchip,isp_mmu"
#define ISP0_IOMMU_COMPATIBLE_NAME "rockchip,isp0_mmu"
#define ISP1_IOMMU_COMPATIBLE_NAME "rockchip,isp1_mmu"
#define VOPB_IOMMU_COMPATIBLE_NAME "rockchip,vopb_mmu"
#define VOPL_IOMMU_COMPATIBLE_NAME "rockchip,vopl_mmu"
#define VOP_IOMMU_COMPATIBLE_NAME	"rockchip,vop_mmu"
#define HEVC_IOMMU_COMPATIBLE_NAME "rockchip,hevc_mmu"
#define VPU_IOMMU_COMPATIBLE_NAME "rockchip,vpu_mmu"
#define VDEC_IOMMU_COMPATIBLE_NAME "rockchip,vdec_mmu"

enum rk_iommu_inttype {
	IOMMU_PAGEFAULT,
	IOMMU_BUSERROR,
	IOMMU_FAULT_UNKNOWN,
	IOMMU_FAULTS_NUM
};

struct iommu_drvdata;

/*
 * @itype: type of fault.
 * @pgtable_base: the physical address of page table base. This is 0 if @itype
 *				  is IOMMU_BUSERROR.
 * @fault_addr: the device (virtual) address that the System MMU tried to
 *			   translated. This is 0 if @itype is IOMMU_BUSERROR.
 */
typedef int (*rockchip_iommu_fault_handler_t)(struct device *dev,
					  enum rk_iommu_inttype itype,
					  unsigned long pgtable_base,
					  unsigned long fault_addr,
					  unsigned int statu
					  );


struct scatterlist;
struct device;

static inline int rockchip_iovmm_activate(struct device *dev)
{
	return -ENOSYS;
}

static inline void rockchip_iovmm_deactivate(struct device *dev)
{
}

static inline dma_addr_t rockchip_iovmm_map(struct device *dev,
			struct scatterlist *sg, off_t offset, size_t size)
{
	return -ENOSYS;
}

static inline void rockchip_iovmm_unmap(struct device *dev, dma_addr_t iova)
{
}

static inline int rockchip_iovmm_map_oto(struct device *dev, phys_addr_t phys,
				size_t size)
{
	return -ENOSYS;
}

static inline void rockchip_iovmm_unmap_oto(struct device *dev, phys_addr_t phys)
{
}

static inline void rockchip_iovmm_set_fault_handler(struct device *dev,
				       rockchip_iommu_fault_handler_t handler)
{
}
static inline int rockchip_iovmm_invalidate_tlb(struct device *dev)
{
	return -ENOSYS;
}

#endif /*__ASM_PLAT_IOVMM_H*/
