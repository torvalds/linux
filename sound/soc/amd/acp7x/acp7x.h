/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * AMD Common ACP header file for ACP7.X variants(ACP7.D/7.E/7.F)
 *
 * Copyright (C) 2026 Advanced Micro Devices, Inc. All rights reserved.
 */

#ifndef __SOUND_SOC_AMD_ACP7X_H
#define __SOUND_SOC_AMD_ACP7X_H

#include <linux/device.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/pci.h>
#include <linux/types.h>

#include <sound/acp7x_chip_offset_byte.h>

#define ACP_DEVICE_ID		0x15E2
#define ACP7X_REG_START		0x1240000
#define ACP7X_REG_END		0x125C000

#define ACP7D_PCI_REV		0x7D
#define ACP7E_PCI_REV		0x7E
#define ACP7F_PCI_REV		0x7F

/* Common register helper bits used by acp7x-common.c */
#define ACP_SOFT_RESET_SOFTRESET_AUDDONE_MASK	0x00010001

#define DELAY_US				5
#define ACP7X_TIMEOUT				5000

#define ACP7X_PGFSM_CNTL_POWER_ON_MASK		7
#define ACP7X_PGFSM_STATUS_MASK			0x3F

/* time in ms for runtime suspend delay */
#define ACP_SUSPEND_DELAY_MS			2000

struct acp_hw_ops {
	int (*acp_init)(void __iomem *acp_base, struct device *dev);
	int (*acp_deinit)(void __iomem *acp_base, struct device *dev);
	int (*acp_suspend)(struct device *dev);
	int (*acp_resume)(struct device *dev);
	int (*acp_suspend_runtime)(struct device *dev);
	int (*acp_resume_runtime)(struct device *dev);
};

struct acp7x_dev_data {
	void __iomem *acp7x_base;
	struct acp_hw_ops *hw_ops;
	u32 addr;
	u32 reg_range;
	u32 acp_rev;
};

void acp7x_hw_init_ops(struct acp_hw_ops *hw_ops);

static inline int acp_hw_init(struct acp7x_dev_data *adata, struct device *dev)
{
	if (adata && adata->hw_ops && adata->hw_ops->acp_init)
		return adata->hw_ops->acp_init(adata->acp7x_base, dev);
	return -EOPNOTSUPP;
}

static inline int acp_hw_deinit(struct acp7x_dev_data *adata, struct device *dev)
{
	if (adata && adata->hw_ops && adata->hw_ops->acp_deinit)
		return adata->hw_ops->acp_deinit(adata->acp7x_base, dev);
	return -EOPNOTSUPP;
}

static inline int acp_hw_suspend(struct device *dev)
{
	struct acp7x_dev_data *adata = dev_get_drvdata(dev);

	if (adata && adata->hw_ops && adata->hw_ops->acp_suspend)
		return adata->hw_ops->acp_suspend(dev);
	return -EOPNOTSUPP;
}

static inline int acp_hw_resume(struct device *dev)
{
	struct acp7x_dev_data *adata = dev_get_drvdata(dev);

	if (adata && adata->hw_ops && adata->hw_ops->acp_resume)
		return adata->hw_ops->acp_resume(dev);
	return -EOPNOTSUPP;
}

static inline int acp_hw_suspend_runtime(struct device *dev)
{
	struct acp7x_dev_data *adata = dev_get_drvdata(dev);

	if (adata && adata->hw_ops && adata->hw_ops->acp_suspend_runtime)
		return adata->hw_ops->acp_suspend_runtime(dev);
	return -EOPNOTSUPP;
}

static inline int acp_hw_runtime_resume(struct device *dev)
{
	struct acp7x_dev_data *adata = dev_get_drvdata(dev);

	if (adata && adata->hw_ops && adata->hw_ops->acp_resume_runtime)
		return adata->hw_ops->acp_resume_runtime(dev);
	return -EOPNOTSUPP;
}

int snd_amd_acp_find_config(struct pci_dev *pci);

#endif /* __SOUND_SOC_AMD_ACP7X_H */
