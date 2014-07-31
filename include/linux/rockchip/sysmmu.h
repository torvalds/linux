/*
 * Rockchip - System MMU support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _ARM_MACH_RK_SYSMMU_H_
#define _ARM_MACH_RK_SYSMMU_H_

#include <linux/list.h>
#include <linux/atomic.h>
#include <linux/spinlock.h>

#define IEP_SYSMMU_COMPATIBLE_NAME "iommu,iep_mmu"
#define VIP_SYSMMU_COMPATIBLE_NAME "iommu,vip_mmu"

#define ISP_SYSMMU_COMPATIBLE_NAME "iommu,isp_mmu"

#define VOPB_SYSMMU_COMPATIBLE_NAME "iommu,vopb_mmu"
#define VOPL_SYSMMU_COMPATIBLE_NAME "iommu,vopl_mmu"

#define HEVC_SYSMMU_COMPATIBLE_NAME "iommu,hevc_mmu"
#define VPU_SYSMMU_COMPATIBLE_NAME "iommu,vpu_mmu"


enum rk_sysmmu_inttype {
	SYSMMU_PAGEFAULT,
	SYSMMU_BUSERROR,
	SYSMMU_FAULT_UNKNOWN,
	SYSMMU_FAULTS_NUM
};
	
struct sysmmu_drvdata;
/*
 * @itype: type of fault.
 * @pgtable_base: the physical address of page table base. This is 0 if @itype
 *				  is SYSMMU_BUSERROR.
 * @fault_addr: the device (virtual) address that the System MMU tried to
 *			   translated. This is 0 if @itype is SYSMMU_BUSERROR.
 */
typedef int (*sysmmu_fault_handler_t)(struct device *dev,
				      enum rk_sysmmu_inttype itype,
				      unsigned long pgtable_base,
				      unsigned long fault_addr,
				      unsigned int statu);
	
#ifdef CONFIG_ROCKCHIP_IOMMU
/**
* rockchip_sysmmu_enable() - enable system mmu
* @owner: The device whose System MMU is about to be enabled.
* @pgd: Base physical address of the 1st level page table
*
* This function enable system mmu to transfer address
* from virtual address to physical address.
* Return non-zero if it fails to enable System MMU.
*/
int rockchip_sysmmu_enable(struct device *owner, unsigned long pgd);

/**
* rockchip_sysmmu_disable() - disable sysmmu mmu of ip
* @owner: The device whose System MMU is about to be disabled.
*
* This function disable system mmu to transfer address
 * from virtual address to physical address
 */
bool rockchip_sysmmu_disable(struct device *owner);

/**
 * rockchip_sysmmu_tlb_invalidate() - flush all TLB entry in system mmu
 * @owner: The device whose System MMU.
 *
 * This function flush all TLB entry in system mmu
 */
void rockchip_sysmmu_tlb_invalidate(struct device *owner);

/** rockchip_sysmmu_set_fault_handler() - Fault handler for System MMUs
 * Called when interrupt occurred by the System MMUs
 * The device drivers of peripheral devices that has a System MMU can implement
 * a fault handler to resolve address translation fault by System MMU.
 * The meanings of return value and parameters are described below.
 *
 * return value: non-zero if the fault is correctly resolved.
 *		   zero if the fault is not handled.
 */
void rockchip_sysmmu_set_fault_handler(struct device *dev,sysmmu_fault_handler_t handler);

/** rockchip_sysmmu_set_prefbuf() - Initialize prefetch buffers of System MMU v3
 *	@owner: The device which need to set the prefetch buffers
 *	@base0: The start virtual address of the area of the @owner device that the
 *			first prefetch buffer loads translation descriptors
 *	@size0: The last virtual address of the area of the @owner device that the
 *			first prefetch buffer loads translation descriptors.
 *	@base1: The start virtual address of the area of the @owner device that the
 *			second prefetch buffer loads translation descriptors. This will be
 *			ignored if @size1 is 0 and this function assigns the 2 prefetch
 *			buffers with each half of the area specified by @base0 and @size0
 *	@size1: The last virtual address of the area of the @owner device that the
 *			prefetch buffer loads translation descriptors. This can be 0. See
 *			the description of @base1 for more information with @size1 = 0
 */
void rockchip_sysmmu_set_prefbuf(struct device *owner,
				unsigned long base0, unsigned long size0,
				unsigned long base1, unsigned long size1);
#else /* CONFIG_ROCKCHIP_IOMMU */
static inline int rockchip_sysmmu_enable(struct device *owner, unsigned long pgd)
{
	return -ENOSYS;
}
static inline bool rockchip_sysmmu_disable(struct device *owner)
{
	return false;
}
static inline void rockchip_sysmmu_tlb_invalidate(struct device *owner)
{
}
static inline void rockchip_sysmmu_set_fault_handler(struct device *dev,sysmmu_fault_handler_t handler)
{
}
static inline void rockchip_sysmmu_set_prefbuf(struct device *owner,
		                               unsigned long base0, unsigned long size0,
                                 	       unsigned long base1, unsigned long size1)
{
}
#endif

#ifdef CONFIG_IOMMU_API
#include <linux/device.h>
static inline void platform_set_sysmmu(struct device *sysmmu, struct device *dev)
{
	dev->archdata.iommu = sysmmu;
}
#else
static inline void platform_set_sysmmu(struct device *sysmmu, struct device *dev)
{
}
#endif

#endif /* _ARM_MACH_RK_SYSMMU_H_ */
