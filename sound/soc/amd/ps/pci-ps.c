// SPDX-License-Identifier: GPL-2.0+
/*
 * AMD Pink Sardine ACP PCI Driver
 *
 * Copyright 2022 Advanced Micro Devices, Inc.
 */

#include <linux/pci.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/delay.h>

#include "acp62.h"

struct acp62_dev_data {
	void __iomem *acp62_base;
};

static int acp62_power_on(void __iomem *acp_base)
{
	u32 val;
	int timeout;

	val = acp62_readl(acp_base + ACP_PGFSM_STATUS);

	if (!val)
		return val;

	if ((val & ACP_PGFSM_STATUS_MASK) != ACP_POWER_ON_IN_PROGRESS)
		acp62_writel(ACP_PGFSM_CNTL_POWER_ON_MASK, acp_base + ACP_PGFSM_CONTROL);
	timeout = 0;
	while (++timeout < 500) {
		val = acp62_readl(acp_base + ACP_PGFSM_STATUS);
		if (!val)
			return 0;
		udelay(1);
	}
	return -ETIMEDOUT;
}

static int acp62_reset(void __iomem *acp_base)
{
	u32 val;
	int timeout;

	acp62_writel(1, acp_base + ACP_SOFT_RESET);
	timeout = 0;
	while (++timeout < 500) {
		val = acp62_readl(acp_base + ACP_SOFT_RESET);
		if (val & ACP_SOFT_RESET_SOFTRESET_AUDDONE_MASK)
			break;
		cpu_relax();
	}
	acp62_writel(0, acp_base + ACP_SOFT_RESET);
	timeout = 0;
	while (++timeout < 500) {
		val = acp62_readl(acp_base + ACP_SOFT_RESET);
		if (!val)
			return 0;
		cpu_relax();
	}
	return -ETIMEDOUT;
}

static void acp62_enable_interrupts(void __iomem *acp_base)
{
	acp62_writel(1, acp_base + ACP_EXTERNAL_INTR_ENB);
}

static void acp62_disable_interrupts(void __iomem *acp_base)
{
	acp62_writel(ACP_EXT_INTR_STAT_CLEAR_MASK, acp_base +
		     ACP_EXTERNAL_INTR_STAT);
	acp62_writel(0, acp_base + ACP_EXTERNAL_INTR_CNTL);
	acp62_writel(0, acp_base + ACP_EXTERNAL_INTR_ENB);
}

static int acp62_init(void __iomem *acp_base, struct device *dev)
{
	int ret;

	ret = acp62_power_on(acp_base);
	if (ret) {
		dev_err(dev, "ACP power on failed\n");
		return ret;
	}
	acp62_writel(0x01, acp_base + ACP_CONTROL);
	ret = acp62_reset(acp_base);
	if (ret) {
		dev_err(dev, "ACP reset failed\n");
		return ret;
	}
	acp62_writel(0x03, acp_base + ACP_CLKMUX_SEL);
	acp62_enable_interrupts(acp_base);
	return 0;
}

static int acp62_deinit(void __iomem *acp_base, struct device *dev)
{
	int ret;

	acp62_disable_interrupts(acp_base);
	ret = acp62_reset(acp_base);
	if (ret) {
		dev_err(dev, "ACP reset failed\n");
		return ret;
	}
	acp62_writel(0, acp_base + ACP_CLKMUX_SEL);
	acp62_writel(0, acp_base + ACP_CONTROL);
	return 0;
}

static int snd_acp62_probe(struct pci_dev *pci,
			   const struct pci_device_id *pci_id)
{
	struct acp62_dev_data *adata;
	u32 addr;
	int ret;

	/* Pink Sardine device check */
	switch (pci->revision) {
	case 0x63:
		break;
	default:
		dev_dbg(&pci->dev, "acp62 pci device not found\n");
		return -ENODEV;
	}
	if (pci_enable_device(pci)) {
		dev_err(&pci->dev, "pci_enable_device failed\n");
		return -ENODEV;
	}

	ret = pci_request_regions(pci, "AMD ACP6.2 audio");
	if (ret < 0) {
		dev_err(&pci->dev, "pci_request_regions failed\n");
		goto disable_pci;
	}
	adata = devm_kzalloc(&pci->dev, sizeof(struct acp62_dev_data),
			     GFP_KERNEL);
	if (!adata) {
		ret = -ENOMEM;
		goto release_regions;
	}

	addr = pci_resource_start(pci, 0);
	adata->acp62_base = devm_ioremap(&pci->dev, addr,
					 pci_resource_len(pci, 0));
	if (!adata->acp62_base) {
		ret = -ENOMEM;
		goto release_regions;
	}
	pci_set_master(pci);
	pci_set_drvdata(pci, adata);
	ret = acp62_init(adata->acp62_base, &pci->dev);
	if (ret)
		goto release_regions;

	return 0;
release_regions:
	pci_release_regions(pci);
disable_pci:
	pci_disable_device(pci);

	return ret;
}

static void snd_acp62_remove(struct pci_dev *pci)
{
	struct acp62_dev_data *adata;
	int ret;

	adata = pci_get_drvdata(pci);
	ret = acp62_deinit(adata->acp62_base, &pci->dev);
	if (ret)
		dev_err(&pci->dev, "ACP de-init failed\n");
	pci_release_regions(pci);
	pci_disable_device(pci);
}

static const struct pci_device_id snd_acp62_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_AMD, ACP_DEVICE_ID),
	.class = PCI_CLASS_MULTIMEDIA_OTHER << 8,
	.class_mask = 0xffffff },
	{ 0, },
};
MODULE_DEVICE_TABLE(pci, snd_acp62_ids);

static struct pci_driver ps_acp62_driver  = {
	.name = KBUILD_MODNAME,
	.id_table = snd_acp62_ids,
	.probe = snd_acp62_probe,
	.remove = snd_acp62_remove,
};

module_pci_driver(ps_acp62_driver);

MODULE_AUTHOR("Vijendar.Mukunda@amd.com");
MODULE_AUTHOR("Syed.SabaKareem@amd.com");
MODULE_DESCRIPTION("AMD ACP Pink Sardine PCI driver");
MODULE_LICENSE("GPL v2");
