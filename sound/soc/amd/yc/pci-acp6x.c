// SPDX-License-Identifier: GPL-2.0+
/*
 * AMD Yellow Carp ACP PCI Driver
 *
 * Copyright 2021 Advanced Micro Devices, Inc.
 */

#include <linux/pci.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/delay.h>

#include "acp6x.h"

struct acp6x_dev_data {
	void __iomem *acp6x_base;
};

static int acp6x_power_on(void __iomem *acp_base)
{
	u32 val;
	int timeout;

	val = acp6x_readl(acp_base + ACP_PGFSM_STATUS);

	if (!val)
		return val;

	if ((val & ACP_PGFSM_STATUS_MASK) != ACP_POWER_ON_IN_PROGRESS)
		acp6x_writel(ACP_PGFSM_CNTL_POWER_ON_MASK, acp_base + ACP_PGFSM_CONTROL);
	timeout = 0;
	while (++timeout < 500) {
		val = acp6x_readl(acp_base + ACP_PGFSM_STATUS);
		if (!val)
			return 0;
		udelay(1);
	}
	return -ETIMEDOUT;
}

static int acp6x_reset(void __iomem *acp_base)
{
	u32 val;
	int timeout;

	acp6x_writel(1, acp_base + ACP_SOFT_RESET);
	timeout = 0;
	while (++timeout < 500) {
		val = acp6x_readl(acp_base + ACP_SOFT_RESET);
		if (val & ACP_SOFT_RESET_SOFTRESET_AUDDONE_MASK)
			break;
		cpu_relax();
	}
	acp6x_writel(0, acp_base + ACP_SOFT_RESET);
	timeout = 0;
	while (++timeout < 500) {
		val = acp6x_readl(acp_base + ACP_SOFT_RESET);
		if (!val)
			return 0;
		cpu_relax();
	}
	return -ETIMEDOUT;
}

static void acp6x_enable_interrupts(void __iomem *acp_base)
{
	acp6x_writel(0x01, acp_base + ACP_EXTERNAL_INTR_ENB);
}

static void acp6x_disable_interrupts(void __iomem *acp_base)
{
	acp6x_writel(ACP_EXT_INTR_STAT_CLEAR_MASK, acp_base +
		     ACP_EXTERNAL_INTR_STAT);
	acp6x_writel(0x00, acp_base + ACP_EXTERNAL_INTR_CNTL);
	acp6x_writel(0x00, acp_base + ACP_EXTERNAL_INTR_ENB);
}

static int acp6x_init(void __iomem *acp_base)
{
	int ret;

	/* power on */
	ret = acp6x_power_on(acp_base);
	if (ret) {
		pr_err("ACP power on failed\n");
		return ret;
	}
	acp6x_writel(0x01, acp_base + ACP_CONTROL);
	/* Reset */
	ret = acp6x_reset(acp_base);
	if (ret) {
		pr_err("ACP reset failed\n");
		return ret;
	}
	acp6x_writel(0x03, acp_base + ACP_CLKMUX_SEL);
	acp6x_enable_interrupts(acp_base);
	return 0;
}

static int acp6x_deinit(void __iomem *acp_base)
{
	int ret;

	acp6x_disable_interrupts(acp_base);
	/* Reset */
	ret = acp6x_reset(acp_base);
	if (ret) {
		pr_err("ACP reset failed\n");
		return ret;
	}
	acp6x_writel(0x00, acp_base + ACP_CLKMUX_SEL);
	acp6x_writel(0x00, acp_base + ACP_CONTROL);
	return 0;
}

static int snd_acp6x_probe(struct pci_dev *pci,
			   const struct pci_device_id *pci_id)
{
	struct acp6x_dev_data *adata;
	int ret;
	u32 addr;

	/* Yellow Carp device check */
	if (pci->revision != 0x60)
		return -ENODEV;

	if (pci_enable_device(pci)) {
		dev_err(&pci->dev, "pci_enable_device failed\n");
		return -ENODEV;
	}

	ret = pci_request_regions(pci, "AMD ACP3x audio");
	if (ret < 0) {
		dev_err(&pci->dev, "pci_request_regions failed\n");
		goto disable_pci;
	}

	adata = devm_kzalloc(&pci->dev, sizeof(struct acp6x_dev_data),
			     GFP_KERNEL);
	if (!adata) {
		ret = -ENOMEM;
		goto release_regions;
	}

	addr = pci_resource_start(pci, 0);
	adata->acp6x_base = devm_ioremap(&pci->dev, addr,
					 pci_resource_len(pci, 0));
	if (!adata->acp6x_base) {
		ret = -ENOMEM;
		goto release_regions;
	}
	pci_set_master(pci);
	pci_set_drvdata(pci, adata);
	ret = acp6x_init(adata->acp6x_base);
	if (ret)
		goto release_regions;

	return 0;
release_regions:
	pci_release_regions(pci);
disable_pci:
	pci_disable_device(pci);

	return ret;
}

static void snd_acp6x_remove(struct pci_dev *pci)
{
	struct acp6x_dev_data *adata;
	int ret;

	adata = pci_get_drvdata(pci);
	ret = acp6x_deinit(adata->acp6x_base);
	if (ret)
		dev_err(&pci->dev, "ACP de-init failed\n");
	pci_release_regions(pci);
	pci_disable_device(pci);
}

static const struct pci_device_id snd_acp6x_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_AMD, ACP_DEVICE_ID),
	.class = PCI_CLASS_MULTIMEDIA_OTHER << 8,
	.class_mask = 0xffffff },
	{ 0, },
};
MODULE_DEVICE_TABLE(pci, snd_acp6x_ids);

static struct pci_driver yc_acp6x_driver  = {
	.name = KBUILD_MODNAME,
	.id_table = snd_acp6x_ids,
	.probe = snd_acp6x_probe,
	.remove = snd_acp6x_remove,
};

module_pci_driver(yc_acp6x_driver);

MODULE_AUTHOR("Vijendar.Mukunda@amd.com");
MODULE_DESCRIPTION("AMD ACP Yellow Carp PCI driver");
MODULE_LICENSE("GPL v2");
