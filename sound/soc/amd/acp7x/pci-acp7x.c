// SPDX-License-Identifier: GPL-2.0-only
/*
 * AMD common ACP PCI driver for ACP7.x variants
 * which includes ACP7.D/7.E/7.F and future variants
 * with same register layout.
 *
 * Copyright 2026 Advanced Micro Devices, Inc.
 */

#include <linux/errno.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/types.h>

#include "acp7x.h"

static int acp_hw_init_ops(struct acp7x_dev_data *adata, struct pci_dev *pci)
{
	adata->hw_ops = devm_kzalloc(&pci->dev, sizeof(struct acp_hw_ops),
				     GFP_KERNEL);
	if (!adata->hw_ops)
		return -ENOMEM;

	switch (adata->acp_rev) {
	case ACP7D_PCI_REV:
	case ACP7E_PCI_REV:
	case ACP7F_PCI_REV:
		acp7x_hw_init_ops(adata->hw_ops);
		break;
	default:
		dev_err(&pci->dev, "ACP device not found\n");
		return -ENODEV;
	}
	return 0;
}

static int snd_acp7x_probe(struct pci_dev *pci,
			   const struct pci_device_id *pci_id)
{
	struct acp7x_dev_data *adata;
	u32 addr;
	u32 flag;
	int ret;

	flag = snd_amd_acp_find_config(pci);
	if (flag)
		return -ENODEV;
	/* ACP PCI revision id check for ACP7.x platforms */
	switch (pci->revision) {
	case ACP7D_PCI_REV:
	case ACP7E_PCI_REV:
	case ACP7F_PCI_REV:
		break;
	default:
		return -ENODEV;
	}
	if (pci_enable_device(pci)) {
		dev_err(&pci->dev, "pci_enable_device failed\n");
		return -ENODEV;
	}

	ret = pci_request_regions(pci, "AMD ACP7.x audio");
	if (ret < 0) {
		dev_err(&pci->dev, "pci_request_regions failed\n");
		goto disable_pci;
	}
	adata = devm_kzalloc(&pci->dev, sizeof(struct acp7x_dev_data),
			     GFP_KERNEL);
	if (!adata) {
		ret = -ENOMEM;
		goto release_regions;
	}
	addr = pci_resource_start(pci, 0);
	adata->acp7x_base = devm_ioremap(&pci->dev, addr,
					 pci_resource_len(pci, 0));
	if (!adata->acp7x_base) {
		ret = -ENOMEM;
		goto release_regions;
	}
	adata->addr = addr;
	adata->reg_range = ACP7X_REG_END - ACP7X_REG_START;
	adata->acp_rev = pci->revision;
	pci_set_master(pci);
	pci_set_drvdata(pci, adata);
	ret = acp_hw_init_ops(adata, pci);
	if (ret) {
		dev_err(&pci->dev, "ACP hw ops init failed\n");
		goto release_regions;
	}
	ret = acp_hw_init(adata, &pci->dev);
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

static int __maybe_unused snd_acp_suspend(struct device *dev)
{
	return acp_hw_suspend(dev);
}

static int __maybe_unused snd_acp_runtime_resume(struct device *dev)
{
	return acp_hw_runtime_resume(dev);
}

static int __maybe_unused snd_acp_resume(struct device *dev)
{
	return acp_hw_resume(dev);
}

static const struct dev_pm_ops acp7x_pm_ops = {
	SET_RUNTIME_PM_OPS(snd_acp_suspend, snd_acp_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(snd_acp_suspend, snd_acp_resume)
};

static void snd_acp7x_remove(struct pci_dev *pci)
{
	struct acp7x_dev_data *adata;
	int ret;

	adata = pci_get_drvdata(pci);
	ret = acp_hw_deinit(adata, &pci->dev);
	if (ret)
		dev_err(&pci->dev, "ACP de-init failed\n");
	pm_runtime_forbid(&pci->dev);
	pm_runtime_get_noresume(&pci->dev);
	pci_release_regions(pci);
	pci_disable_device(pci);
}

static const struct pci_device_id snd_acp7x_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_AMD, ACP_DEVICE_ID),
	.class = PCI_CLASS_MULTIMEDIA_OTHER << 8,
	.class_mask = 0xffffff },
	{ 0, },
};
MODULE_DEVICE_TABLE(pci, snd_acp7x_ids);

static struct pci_driver acp7x_pci_driver  = {
	.name = KBUILD_MODNAME,
	.id_table = snd_acp7x_ids,
	.probe = snd_acp7x_probe,
	.remove = snd_acp7x_remove,
	.driver = {
		.pm = &acp7x_pm_ops,
	}
};

module_pci_driver(acp7x_pci_driver);

MODULE_AUTHOR("Vijendar.Mukunda@amd.com");
MODULE_DESCRIPTION("AMD ACP PCI driver for ACP7.X");
MODULE_LICENSE("GPL");
