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

#ifdef CONFIG_RK_IOVMM

int rockchip_iovmm_activate(struct device *dev);
void rockchip_iovmm_deactivate(struct device *dev);

/* rockchip_iovmm_map() - Maps a list of physical memory chunks
 * @dev: the owner of the IO address space where the mapping is created
 * @sg: list of physical memory chunks to map
 * @offset: length in bytes where the mapping starts
 * @size: how much memory to map in bytes. @offset + @size must not exceed
 *        total size of @sg
 *
 * This function returns mapped IO address in the address space of @dev.
 * Returns minus error number if mapping fails.
 * Caller must check its return code with IS_ERROR_VALUE() if the function
 * succeeded.
 *
 * The caller of this function must ensure that iovmm_cleanup() is not called
 * while this function is called.
 *
 */
dma_addr_t rockchip_iovmm_map(struct device *dev, struct scatterlist *sg,
		     off_t offset, size_t size);

/* rockchip_iovmm_unmap() - unmaps the given IO address
 * @dev: the owner of the IO address space where @iova belongs
 * @iova: IO address that needs to be unmapped and freed.
 *
 * The caller of this function must ensure that iovmm_cleanup() is not called
 * while this function is called.
 */
void rockchip_iovmm_unmap(struct device *dev, dma_addr_t iova);

/* rockchip_iovmm_map_oto - create one to one mapping for the given physical address
 * @dev: the owner of the IO address space to map
 * @phys: physical address to map
 * @size: size of the mapping to create
 *
 * This function return 0 if mapping is successful. Otherwise, minus error
 * value.
 */
int rockchip_iovmm_map_oto(struct device *dev, phys_addr_t phys, size_t size);

/* rockchip_iovmm_unmap_oto - remove one to one mapping
 * @dev: the owner ofthe IO address space
 * @phys: physical address to remove mapping
 */
void rockchip_iovmm_unmap_oto(struct device *dev, phys_addr_t phys);

void rockchip_iovmm_set_fault_handler(struct device *dev,
				       rockchip_iommu_fault_handler_t handler);
/** rockchip_iovmm_set_fault_handler() - Fault handler for IOMMUs
 * Called when interrupt occurred by the IOMMUs
 * The device drivers of peripheral devices that has a IOMMU can implement
 * a fault handler to resolve address translation fault by IOMMU.
 * The meanings of return value and parameters are described below.
 *
 * return value: non-zero if the fault is correctly resolved.
 *		   zero if the fault is not handled.
 */

int rockchip_iovmm_invalidate_tlb(struct device *dev);
#else
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

#endif /* CONFIG_RK_IOVMM */

#endif /*__ASM_PLAT_IOVMM_H*/
