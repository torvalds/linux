// SPDX-License-Identifier: GPL-2.0-only
/*
 * AMD ACP PCI driver callback routines for ACP6.3, ACP7.0 & ACP7.1
 * platforms.
 *
 * Copyright 2025 Advanced Micro Devices, Inc.
 * Authors: Vijendar Mukunda <Vijendar.Mukunda@amd.com>
 */

#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/export.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/platform_device.h>

#include "acp63.h"

static int acp63_power_on(void __iomem *acp_base)
{
	u32 val;

	val = readl(acp_base + ACP_PGFSM_STATUS);

	if (!val)
		return val;

	if ((val & ACP63_PGFSM_STATUS_MASK) != ACP63_POWER_ON_IN_PROGRESS)
		writel(ACP63_PGFSM_CNTL_POWER_ON_MASK, acp_base + ACP_PGFSM_CONTROL);

	return readl_poll_timeout(acp_base + ACP_PGFSM_STATUS, val, !val, DELAY_US, ACP63_TIMEOUT);
}

static int acp63_reset(void __iomem *acp_base)
{
	u32 val;
	int ret;

	writel(1, acp_base + ACP_SOFT_RESET);

	ret = readl_poll_timeout(acp_base + ACP_SOFT_RESET, val,
				 val & ACP_SOFT_RESET_SOFTRESET_AUDDONE_MASK,
				 DELAY_US, ACP63_TIMEOUT);
	if (ret)
		return ret;

	writel(0, acp_base + ACP_SOFT_RESET);

	return readl_poll_timeout(acp_base + ACP_SOFT_RESET, val, !val, DELAY_US, ACP63_TIMEOUT);
}

static void acp63_enable_interrupts(void __iomem *acp_base)
{
	writel(1, acp_base + ACP_EXTERNAL_INTR_ENB);
	writel(ACP_ERROR_IRQ, acp_base + ACP_EXTERNAL_INTR_CNTL);
}

static void acp63_disable_interrupts(void __iomem *acp_base)
{
	writel(ACP_EXT_INTR_STAT_CLEAR_MASK, acp_base + ACP_EXTERNAL_INTR_STAT);
	writel(0, acp_base + ACP_EXTERNAL_INTR_CNTL);
	writel(0, acp_base + ACP_EXTERNAL_INTR_ENB);
}

static int acp63_init(void __iomem *acp_base, struct device *dev)
{
	int ret;

	ret = acp63_power_on(acp_base);
	if (ret) {
		dev_err(dev, "ACP power on failed\n");
		return ret;
	}
	writel(0x01, acp_base + ACP_CONTROL);
	ret = acp63_reset(acp_base);
	if (ret) {
		dev_err(dev, "ACP reset failed\n");
		return ret;
	}
	acp63_enable_interrupts(acp_base);
	writel(0, acp_base + ACP_ZSC_DSP_CTRL);
	return 0;
}

static int acp63_deinit(void __iomem *acp_base, struct device *dev)
{
	int ret;

	acp63_disable_interrupts(acp_base);
	ret = acp63_reset(acp_base);
	if (ret) {
		dev_err(dev, "ACP reset failed\n");
		return ret;
	}
	writel(0, acp_base + ACP_CONTROL);
	writel(1, acp_base + ACP_ZSC_DSP_CTRL);
	return 0;
}

void acp63_hw_init_ops(struct acp_hw_ops *hw_ops)
{
	hw_ops->acp_init = acp63_init;
	hw_ops->acp_deinit = acp63_deinit;
}
