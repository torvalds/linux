/*
 * Copyright (C) 2007-2010 Advanced Micro Devices, Inc.
 * Author: Joerg Roedel <joerg.roedel@amd.com>
 *         Leo Duran <leo.duran@amd.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#ifndef _ASM_X86_AMD_IOMMU_H
#define _ASM_X86_AMD_IOMMU_H

#include <linux/types.h>

#ifdef CONFIG_AMD_IOMMU

struct task_struct;
struct pci_dev;

extern int amd_iommu_detect(void);


/**
 * amd_iommu_enable_device_erratum() - Enable erratum workaround for device
 *				       in the IOMMUv2 driver
 * @pdev: The PCI device the workaround is necessary for
 * @erratum: The erratum workaround to enable
 *
 * The function needs to be called before amd_iommu_init_device().
 * Possible values for the erratum number are for now:
 * - AMD_PRI_DEV_ERRATUM_ENABLE_RESET - Reset PRI capability when PRI
 *					is enabled
 * - AMD_PRI_DEV_ERRATUM_LIMIT_REQ_ONE - Limit number of outstanding PRI
 *					 requests to one
 */
#define AMD_PRI_DEV_ERRATUM_ENABLE_RESET		0
#define AMD_PRI_DEV_ERRATUM_LIMIT_REQ_ONE		1

extern void amd_iommu_enable_device_erratum(struct pci_dev *pdev, u32 erratum);

/**
 * amd_iommu_init_device() - Init device for use with IOMMUv2 driver
 * @pdev: The PCI device to initialize
 * @pasids: Number of PASIDs to support for this device
 *
 * This function does all setup for the device pdev so that it can be
 * used with IOMMUv2.
 * Returns 0 on success or negative value on error.
 */
extern int amd_iommu_init_device(struct pci_dev *pdev, int pasids);

/**
 * amd_iommu_free_device() - Free all IOMMUv2 related device resources
 *			     and disable IOMMUv2 usage for this device
 * @pdev: The PCI device to disable IOMMUv2 usage for'
 */
extern void amd_iommu_free_device(struct pci_dev *pdev);

/**
 * amd_iommu_bind_pasid() - Bind a given task to a PASID on a device
 * @pdev: The PCI device to bind the task to
 * @pasid: The PASID on the device the task should be bound to
 * @task: the task to bind
 *
 * The function returns 0 on success or a negative value on error.
 */
extern int amd_iommu_bind_pasid(struct pci_dev *pdev, int pasid,
				struct task_struct *task);

/**
 * amd_iommu_unbind_pasid() - Unbind a PASID from its task on
 *			      a device
 * @pdev: The device of the PASID
 * @pasid: The PASID to unbind
 *
 * When this function returns the device is no longer using the PASID
 * and the PASID is no longer bound to its task.
 */
extern void amd_iommu_unbind_pasid(struct pci_dev *pdev, int pasid);

/**
 * amd_iommu_set_invalid_ppr_cb() - Register a call-back for failed
 *				    PRI requests
 * @pdev: The PCI device the call-back should be registered for
 * @cb: The call-back function
 *
 * The IOMMUv2 driver invokes this call-back when it is unable to
 * successfully handle a PRI request. The device driver can then decide
 * which PRI response the device should see. Possible return values for
 * the call-back are:
 *
 * - AMD_IOMMU_INV_PRI_RSP_SUCCESS - Send SUCCESS back to the device
 * - AMD_IOMMU_INV_PRI_RSP_INVALID - Send INVALID back to the device
 * - AMD_IOMMU_INV_PRI_RSP_FAIL    - Send Failure back to the device,
 *				     the device is required to disable
 *				     PRI when it receives this response
 *
 * The function returns 0 on success or negative value on error.
 */
#define AMD_IOMMU_INV_PRI_RSP_SUCCESS	0
#define AMD_IOMMU_INV_PRI_RSP_INVALID	1
#define AMD_IOMMU_INV_PRI_RSP_FAIL	2

typedef int (*amd_iommu_invalid_ppr_cb)(struct pci_dev *pdev,
					int pasid,
					unsigned long address,
					u16);

extern int amd_iommu_set_invalid_ppr_cb(struct pci_dev *pdev,
					amd_iommu_invalid_ppr_cb cb);

/**
 * amd_iommu_device_info() - Get information about IOMMUv2 support of a
 *			     PCI device
 * @pdev: PCI device to query information from
 * @info: A pointer to an amd_iommu_device_info structure which will contain
 *	  the information about the PCI device
 *
 * Returns 0 on success, negative value on error
 */

#define AMD_IOMMU_DEVICE_FLAG_ATS_SUP     0x1    /* ATS feature supported */
#define AMD_IOMMU_DEVICE_FLAG_PRI_SUP     0x2    /* PRI feature supported */
#define AMD_IOMMU_DEVICE_FLAG_PASID_SUP   0x4    /* PASID context supported */
#define AMD_IOMMU_DEVICE_FLAG_EXEC_SUP    0x8    /* Device may request execution
						    on memory pages */
#define AMD_IOMMU_DEVICE_FLAG_PRIV_SUP   0x10    /* Device may request
						    super-user privileges */

struct amd_iommu_device_info {
	int max_pasids;
	u32 flags;
};

extern int amd_iommu_device_info(struct pci_dev *pdev,
				 struct amd_iommu_device_info *info);

/**
 * amd_iommu_set_invalidate_ctx_cb() - Register a call-back for invalidating
 *				       a pasid context. This call-back is
 *				       invoked when the IOMMUv2 driver needs to
 *				       invalidate a PASID context, for example
 *				       because the task that is bound to that
 *				       context is about to exit.
 *
 * @pdev: The PCI device the call-back should be registered for
 * @cb: The call-back function
 */

typedef void (*amd_iommu_invalidate_ctx)(struct pci_dev *pdev, int pasid);

extern int amd_iommu_set_invalidate_ctx_cb(struct pci_dev *pdev,
					   amd_iommu_invalidate_ctx cb);

#else

static inline int amd_iommu_detect(void) { return -ENODEV; }

#endif

#endif /* _ASM_X86_AMD_IOMMU_H */
