// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license. When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2023 Advanced Micro Devices, Inc.
//
// Authors: Syed Saba Kareem <Syed.SabaKareem@amd.com>
//

/*
 * Common file to be used by amd platforms
 */

#include "amd.h"
#include <linux/pci.h>
#include <linux/export.h>

void acp_enable_interrupts(struct acp_dev_data *adata)
{
	struct acp_resource *rsrc = adata->rsrc;
	u32 ext_intr_ctrl;

	writel(0x01, ACP_EXTERNAL_INTR_ENB(adata));
	ext_intr_ctrl = readl(ACP_EXTERNAL_INTR_CNTL(adata, rsrc->irqp_used));
	ext_intr_ctrl |= ACP_ERROR_MASK;
	writel(ext_intr_ctrl, ACP_EXTERNAL_INTR_CNTL(adata, rsrc->irqp_used));
}
EXPORT_SYMBOL_NS_GPL(acp_enable_interrupts, SND_SOC_ACP_COMMON);

void acp_disable_interrupts(struct acp_dev_data *adata)
{
	struct acp_resource *rsrc = adata->rsrc;

	writel(ACP_EXT_INTR_STAT_CLEAR_MASK, ACP_EXTERNAL_INTR_STAT(adata, rsrc->irqp_used));
	writel(0x00, ACP_EXTERNAL_INTR_ENB(adata));
}
EXPORT_SYMBOL_NS_GPL(acp_disable_interrupts, SND_SOC_ACP_COMMON);

static int acp_power_on(struct acp_chip_info *chip)
{
	u32 val, acp_pgfsm_stat_reg, acp_pgfsm_ctrl_reg;
	void __iomem *base;

	base = chip->base;
	switch (chip->acp_rev) {
	case ACP3X_DEV:
		acp_pgfsm_stat_reg = ACP_PGFSM_STATUS;
		acp_pgfsm_ctrl_reg = ACP_PGFSM_CONTROL;
		break;
	case ACP6X_DEV:
		acp_pgfsm_stat_reg = ACP6X_PGFSM_STATUS;
		acp_pgfsm_ctrl_reg = ACP6X_PGFSM_CONTROL;
		break;
	default:
		return -EINVAL;
	}

	val = readl(base + acp_pgfsm_stat_reg);
	if (val == ACP_POWERED_ON)
		return 0;

	if ((val & ACP_PGFSM_STATUS_MASK) != ACP_POWER_ON_IN_PROGRESS)
		writel(ACP_PGFSM_CNTL_POWER_ON_MASK, base + acp_pgfsm_ctrl_reg);

	return readl_poll_timeout(base + acp_pgfsm_stat_reg, val,
				  !val, DELAY_US, ACP_TIMEOUT);
}

static int acp_reset(void __iomem *base)
{
	u32 val;
	int ret;

	writel(1, base + ACP_SOFT_RESET);
	ret = readl_poll_timeout(base + ACP_SOFT_RESET, val, val & ACP_SOFT_RST_DONE_MASK,
				 DELAY_US, ACP_TIMEOUT);
	if (ret)
		return ret;

	writel(0, base + ACP_SOFT_RESET);
	return readl_poll_timeout(base + ACP_SOFT_RESET, val, !val, DELAY_US, ACP_TIMEOUT);
}

int acp_init(struct acp_chip_info *chip)
{
	int ret;

	/* power on */
	ret = acp_power_on(chip);
	if (ret) {
		pr_err("ACP power on failed\n");
		return ret;
	}
	writel(0x01, chip->base + ACP_CONTROL);

	/* Reset */
	ret = acp_reset(chip->base);
	if (ret) {
		pr_err("ACP reset failed\n");
		return ret;
	}
	return 0;
}
EXPORT_SYMBOL_NS_GPL(acp_init, SND_SOC_ACP_COMMON);

int acp_deinit(void __iomem *base)
{
	int ret;

	/* Reset */
	ret = acp_reset(base);
	if (ret)
		return ret;

	writel(0, base + ACP_CONTROL);
	return 0;
}
EXPORT_SYMBOL_NS_GPL(acp_deinit, SND_SOC_ACP_COMMON);

int smn_write(struct pci_dev *dev, u32 smn_addr, u32 data)
{
	pci_write_config_dword(dev, 0x60, smn_addr);
	pci_write_config_dword(dev, 0x64, data);
	return 0;
}
EXPORT_SYMBOL_NS_GPL(smn_write, SND_SOC_ACP_COMMON);

int smn_read(struct pci_dev *dev, u32 smn_addr)
{
	u32 data;

	pci_write_config_dword(dev, 0x60, smn_addr);
	pci_read_config_dword(dev, 0x64, &data);
	return data;
}
EXPORT_SYMBOL_NS_GPL(smn_read, SND_SOC_ACP_COMMON);

MODULE_LICENSE("Dual BSD/GPL");
