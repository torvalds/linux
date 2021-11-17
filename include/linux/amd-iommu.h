/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2007-2010 Advanced Micro Devices, Inc.
 * Author: Joerg Roedel <joerg.roedel@amd.com>
 *         Leo Duran <leo.duran@amd.com>
 */

#ifndef _ASM_X86_AMD_IOMMU_H
#define _ASM_X86_AMD_IOMMU_H

#include <linux/types.h>

struct amd_iommu;

/*
 * This is mainly used to communicate information back-and-forth
 * between SVM and IOMMU for setting up and tearing down posted
 * interrupt
 */
struct amd_iommu_pi_data {
	u32 ga_tag;
	u32 prev_ga_tag;
	u64 base;
	bool is_guest_mode;
	struct vcpu_data *vcpu_data;
	void *ir_data;
};

#ifdef CONFIG_AMD_IOMMU

struct task_struct;
struct pci_dev;

extern int amd_iommu_detect(void);
extern int amd_iommu_init_hardware(void);

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
extern int amd_iommu_bind_pasid(struct pci_dev *pdev, u32 pasid,
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
extern void amd_iommu_unbind_pasid(struct pci_dev *pdev, u32 pasid);

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
					u32 pasid,
					unsigned long address,
					u16);

extern int amd_iommu_set_invalid_ppr_cb(struct pci_dev *pdev,
					amd_iommu_invalid_ppr_cb cb);

#define PPR_FAULT_EXEC	(1 << 1)
#define PPR_FAULT_READ  (1 << 2)
#define PPR_FAULT_WRITE (1 << 5)
#define PPR_FAULT_USER  (1 << 6)
#define PPR_FAULT_RSVD  (1 << 7)
#define PPR_FAULT_GN    (1 << 8)

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

typedef void (*amd_iommu_invalidate_ctx)(struct pci_dev *pdev, u32 pasid);

extern int amd_iommu_set_invalidate_ctx_cb(struct pci_dev *pdev,
					   amd_iommu_invalidate_ctx cb);
#else /* CONFIG_AMD_IOMMU */

static inline int amd_iommu_detect(void) { return -ENODEV; }

#endif /* CONFIG_AMD_IOMMU */

#if defined(CONFIG_AMD_IOMMU) && defined(CONFIG_IRQ_REMAP)

/* IOMMU AVIC Function */
extern int amd_iommu_register_ga_log_notifier(int (*notifier)(u32));

extern int
amd_iommu_update_ga(int cpu, bool is_run, void *data);

extern int amd_iommu_activate_guest_mode(void *data);
extern int amd_iommu_deactivate_guest_mode(void *data);

#else /* defined(CONFIG_AMD_IOMMU) && defined(CONFIG_IRQ_REMAP) */

static inline int
amd_iommu_register_ga_log_notifier(int (*notifier)(u32))
{
	return 0;
}

static inline int
amd_iommu_update_ga(int cpu, bool is_run, void *data)
{
	return 0;
}

static inline int amd_iommu_activate_guest_mode(void *data)
{
	return 0;
}

static inline int amd_iommu_deactivate_guest_mode(void *data)
{
	return 0;
}
#endif /* defined(CONFIG_AMD_IOMMU) && defined(CONFIG_IRQ_REMAP) */

int amd_iommu_get_num_iommus(void);
bool amd_iommu_pc_supported(void);
u8 amd_iommu_pc_get_max_banks(unsigned int idx);
u8 amd_iommu_pc_get_max_counters(unsigned int idx);
int amd_iommu_pc_set_reg(struct amd_iommu *iommu, u8 bank, u8 cntr, u8 fxn,
		u64 *value);
int amd_iommu_pc_get_reg(struct amd_iommu *iommu, u8 bank, u8 cntr, u8 fxn,
		u64 *value);
struct amd_iommu *get_amd_iommu(unsigned int idx);

#endif /* _ASM_X86_AMD_IOMMU_H */
