// SPDX-License-Identifier: GPL-2.0+
/*
 * AMD RPL ACP PCI Driver
 *
 * Copyright 2022 Advanced Micro Devices, Inc.
 */

#include <linux/pci.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#include "rpl_acp6x.h"

struct rpl_dev_data {
	void __iomem *acp6x_base;
};

static int rpl_power_on(void __iomem *acp_base)
{
	u32 val;
	int timeout;

	val = rpl_acp_readl(acp_base + ACP_PGFSM_STATUS);

	if (!val)
		return val;

	if ((val & ACP_PGFSM_STATUS_MASK) != ACP_POWER_ON_IN_PROGRESS)
		rpl_acp_writel(ACP_PGFSM_CNTL_POWER_ON_MASK, acp_base + ACP_PGFSM_CONTROL);
	timeout = 0;
	while (++timeout < 500) {
		val = rpl_acp_readl(acp_base + ACP_PGFSM_STATUS);
		if (!val)
			return 0;
		udelay(1);
	}
	return -ETIMEDOUT;
}

static int rpl_reset(void __iomem *acp_base)
{
	u32 val;
	int timeout;

	rpl_acp_writel(1, acp_base + ACP_SOFT_RESET);
	timeout = 0;
	while (++timeout < 500) {
		val = rpl_acp_readl(acp_base + ACP_SOFT_RESET);
		if (val & ACP_SOFT_RESET_SOFTRESET_AUDDONE_MASK)
			break;
		cpu_relax();
	}
	rpl_acp_writel(0, acp_base + ACP_SOFT_RESET);
	timeout = 0;
	while (++timeout < 500) {
		val = rpl_acp_readl(acp_base + ACP_SOFT_RESET);
		if (!val)
			return 0;
		cpu_relax();
	}
	return -ETIMEDOUT;
}

static int rpl_init(void __iomem *acp_base)
{
	int ret;

	/* power on */
	ret = rpl_power_on(acp_base);
	if (ret) {
		pr_err("ACP power on failed\n");
		return ret;
	}
	rpl_acp_writel(0x01, acp_base + ACP_CONTROL);
	/* Reset */
	ret = rpl_reset(acp_base);
	if (ret) {
		pr_err("ACP reset failed\n");
		return ret;
	}
	rpl_acp_writel(0x03, acp_base + ACP_CLKMUX_SEL);
	return 0;
}

static int rpl_deinit(void __iomem *acp_base)
{
	int ret;

	/* Reset */
	ret = rpl_reset(acp_base);
	if (ret) {
		pr_err("ACP reset failed\n");
		return ret;
	}
	rpl_acp_writel(0x00, acp_base + ACP_CLKMUX_SEL);
	rpl_acp_writel(0x00, acp_base + ACP_CONTROL);
	return 0;
}

static int snd_rpl_probe(struct pci_dev *pci,
			 const struct pci_device_id *pci_id)
{
	struct rpl_dev_data *adata;
	u32 addr;
	int ret;

	/* RPL device check */
	switch (pci->revision) {
	case 0x62:
		break;
	default:
		dev_dbg(&pci->dev, "acp6x pci device not found\n");
		return -ENODEV;
	}
	if (pci_enable_device(pci)) {
		dev_err(&pci->dev, "pci_enable_device failed\n");
		return -ENODEV;
	}

	ret = pci_request_regions(pci, "AMD ACP6x audio");
	if (ret < 0) {
		dev_err(&pci->dev, "pci_request_regions failed\n");
		goto disable_pci;
	}

	adata = devm_kzalloc(&pci->dev, sizeof(struct rpl_dev_data),
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
	ret = rpl_init(adata->acp6x_base);
	if (ret)
		goto release_regions;
	pm_runtime_set_autosuspend_delay(&pci->dev, ACP_SUSPEND_DELAY_MS);
	pm_runtime_use_autosuspend(&pci->dev);
	pm_runtime_put_noidle(&pci->dev);
	pm_runtime_allow(&pci->dev);

	return 0;
release_regions:
	pci_release_regions(pci);
disable_pci:
	pci_disable_device(pci);

	return ret;
}

static int snd_rpl_suspend(struct device *dev)
{
	struct rpl_dev_data *adata;
	int ret;

	adata = dev_get_drvdata(dev);
	ret = rpl_deinit(adata->acp6x_base);
	if (ret)
		dev_err(dev, "ACP de-init failed\n");
	return ret;
}

static int snd_rpl_resume(struct device *dev)
{
	struct rpl_dev_data *adata;
	int ret;

	adata = dev_get_drvdata(dev);
	ret = rpl_init(adata->acp6x_base);
	if (ret)
		dev_err(dev, "ACP init failed\n");
	return ret;
}

static const struct dev_pm_ops rpl_pm = {
	RUNTIME_PM_OPS(snd_rpl_suspend, snd_rpl_resume, NULL)
	SYSTEM_SLEEP_PM_OPS(snd_rpl_suspend, snd_rpl_resume)
};

static void snd_rpl_remove(struct pci_dev *pci)
{
	struct rpl_dev_data *adata;
	int ret;

	adata = pci_get_drvdata(pci);
	ret = rpl_deinit(adata->acp6x_base);
	if (ret)
		dev_err(&pci->dev, "ACP de-init failed\n");
	pm_runtime_forbid(&pci->dev);
	pm_runtime_get_noresume(&pci->dev);
	pci_release_regions(pci);
	pci_disable_device(pci);
}

static const struct pci_device_id snd_rpl_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_AMD, ACP_DEVICE_ID),
	.class = PCI_CLASS_MULTIMEDIA_OTHER << 8,
	.class_mask = 0xffffff },
	{ 0, },
};
MODULE_DEVICE_TABLE(pci, snd_rpl_ids);

static struct pci_driver rpl_acp6x_driver  = {
	.name = KBUILD_MODNAME,
	.id_table = snd_rpl_ids,
	.probe = snd_rpl_probe,
	.remove = snd_rpl_remove,
	.driver = {
		.pm = pm_ptr(&rpl_pm),
	}
};

module_pci_driver(rpl_acp6x_driver);

MODULE_DESCRIPTION("AMD ACP RPL PCI driver");
MODULE_LICENSE("GPL v2");
