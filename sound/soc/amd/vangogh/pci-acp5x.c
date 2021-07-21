// SPDX-License-Identifier: GPL-2.0+
//
// AMD Vangogh ACP PCI Driver
//
// Copyright (C) 2021 Advanced Micro Devices, Inc. All rights reserved.

#include <linux/pci.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/delay.h>

#include "acp5x.h"

struct acp5x_dev_data {
	void __iomem *acp5x_base;
};

static int acp5x_power_on(void __iomem *acp5x_base)
{
	u32 val;
	int timeout;

	val = acp_readl(acp5x_base + ACP_PGFSM_STATUS);

	if (val == 0)
		return val;

	if ((val & ACP_PGFSM_STATUS_MASK) !=
				ACP_POWER_ON_IN_PROGRESS)
		acp_writel(ACP_PGFSM_CNTL_POWER_ON_MASK,
			   acp5x_base + ACP_PGFSM_CONTROL);
	timeout = 0;
	while (++timeout < 500) {
		val = acp_readl(acp5x_base + ACP_PGFSM_STATUS);
		if ((val & ACP_PGFSM_STATUS_MASK) == ACP_POWERED_ON)
			return 0;
		udelay(1);
	}
	return -ETIMEDOUT;
}

static int acp5x_reset(void __iomem *acp5x_base)
{
	u32 val;
	int timeout;

	acp_writel(1, acp5x_base + ACP_SOFT_RESET);
	timeout = 0;
	while (++timeout < 500) {
		val = acp_readl(acp5x_base + ACP_SOFT_RESET);
		if (val & ACP_SOFT_RESET_SOFTRESET_AUDDONE_MASK)
			break;
		cpu_relax();
	}
	acp_writel(0, acp5x_base + ACP_SOFT_RESET);
	timeout = 0;
	while (++timeout < 500) {
		val = acp_readl(acp5x_base + ACP_SOFT_RESET);
		if (!val)
			return 0;
		cpu_relax();
	}
	return -ETIMEDOUT;
}

static void acp5x_enable_interrupts(void __iomem *acp5x_base)
{
	acp_writel(0x01, acp5x_base + ACP_EXTERNAL_INTR_ENB);
}

static void acp5x_disable_interrupts(void __iomem *acp5x_base)
{
	acp_writel(ACP_EXT_INTR_STAT_CLEAR_MASK, acp5x_base +
		   ACP_EXTERNAL_INTR_STAT);
	acp_writel(0x00, acp5x_base + ACP_EXTERNAL_INTR_CNTL);
	acp_writel(0x00, acp5x_base + ACP_EXTERNAL_INTR_ENB);
}

static int acp5x_init(void __iomem *acp5x_base)
{
	int ret;

	/* power on */
	ret = acp5x_power_on(acp5x_base);
	if (ret) {
		pr_err("ACP5x power on failed\n");
		return ret;
	}
	/* Reset */
	ret = acp5x_reset(acp5x_base);
	if (ret) {
		pr_err("ACP5x reset failed\n");
		return ret;
	}
	acp5x_enable_interrupts(acp5x_base);
	return 0;
}

static int acp5x_deinit(void __iomem *acp5x_base)
{
	int ret;

	acp5x_disable_interrupts(acp5x_base);
	/* Reset */
	ret = acp5x_reset(acp5x_base);
	if (ret) {
		pr_err("ACP5x reset failed\n");
		return ret;
	}
	return 0;
}

static int snd_acp5x_probe(struct pci_dev *pci,
			   const struct pci_device_id *pci_id)
{
	struct acp5x_dev_data *adata;
	int ret;
	u32 addr;

	if (pci->revision != 0x50)
		return -ENODEV;

	if (pci_enable_device(pci)) {
		dev_err(&pci->dev, "pci_enable_device failed\n");
		return -ENODEV;
	}

	ret = pci_request_regions(pci, "AMD ACP5x audio");
	if (ret < 0) {
		dev_err(&pci->dev, "pci_request_regions failed\n");
		goto disable_pci;
	}

	adata = devm_kzalloc(&pci->dev, sizeof(struct acp5x_dev_data),
			     GFP_KERNEL);
	if (!adata) {
		ret = -ENOMEM;
		goto release_regions;
	}
	addr = pci_resource_start(pci, 0);
	adata->acp5x_base = devm_ioremap(&pci->dev, addr,
					 pci_resource_len(pci, 0));
	if (!adata->acp5x_base) {
		ret = -ENOMEM;
		goto release_regions;
	}
	pci_set_master(pci);
	pci_set_drvdata(pci, adata);
	ret = acp5x_init(adata->acp5x_base);
	if (ret)
		goto release_regions;

release_regions:
	pci_release_regions(pci);
disable_pci:
	pci_disable_device(pci);

	return ret;
}

static void snd_acp5x_remove(struct pci_dev *pci)
{
	struct acp5x_dev_data *adata;
	int ret;

	adata = pci_get_drvdata(pci);
	ret = acp5x_deinit(adata->acp5x_base);
	if (ret)
		dev_err(&pci->dev, "ACP de-init failed\n");
	pci_release_regions(pci);
	pci_disable_device(pci);
}

static const struct pci_device_id snd_acp5x_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_AMD, ACP_DEVICE_ID),
	.class = PCI_CLASS_MULTIMEDIA_OTHER << 8,
	.class_mask = 0xffffff },
	{ 0, },
};
MODULE_DEVICE_TABLE(pci, snd_acp5x_ids);

static struct pci_driver acp5x_driver  = {
	.name = KBUILD_MODNAME,
	.id_table = snd_acp5x_ids,
	.probe = snd_acp5x_probe,
	.remove = snd_acp5x_remove,
};

module_pci_driver(acp5x_driver);

MODULE_AUTHOR("Vijendar.Mukunda@amd.com");
MODULE_DESCRIPTION("AMD Vangogh ACP PCI driver");
MODULE_LICENSE("GPL v2");
