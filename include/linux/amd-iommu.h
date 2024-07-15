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

#ifdef CONFIG_KVM_AMD_SEV
int amd_iommu_snp_disable(void);
#else
static inline int amd_iommu_snp_disable(void) { return 0; }
#endif

#endif /* _ASM_X86_AMD_IOMMU_H */
