// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license. When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2022 Advanced Micro Devices, Inc. All rights reserved.
//
// Authors: Ajit Kumar Pandey <AjitKumar.Pandey@amd.com>

/*
 * Generic PCI interface for ACP device
 */

#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>

#include "amd.h"
#include "../mach-config.h"

#define DRV_NAME "acp_pci"

#define ACP3x_REG_START	0x1240000
#define ACP3x_REG_END	0x125C000

static struct platform_device *dmic_dev;
static struct platform_device *pdev;

static const struct resource acp_res[] = {
	{
		.start = 0,
		.end = ACP3x_REG_END - ACP3x_REG_START,
		.name = "acp_mem",
		.flags = IORESOURCE_MEM,
	},
	{
		.start = 0,
		.end = 0,
		.name = "acp_dai_irq",
		.flags = IORESOURCE_IRQ,
	},
};

static int acp_pci_probe(struct pci_dev *pci, const struct pci_device_id *pci_id)
{
	struct platform_device_info pdevinfo;
	struct device *dev = &pci->dev;
	const struct resource *res_acp;
	struct acp_chip_info *chip;
	struct resource *res;
	unsigned int flag, addr, num_res, i;
	int ret;

	flag = snd_amd_acp_find_config(pci);
	if (flag != FLAG_AMD_LEGACY && flag != FLAG_AMD_LEGACY_ONLY_DMIC)
		return -ENODEV;

	chip = devm_kzalloc(&pci->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	if (pci_enable_device(pci))
		return dev_err_probe(&pci->dev, -ENODEV,
				     "pci_enable_device failed\n");

	ret = pci_request_regions(pci, "AMD ACP3x audio");
	if (ret < 0) {
		dev_err(&pci->dev, "pci_request_regions failed\n");
		ret = -ENOMEM;
		goto disable_pci;
	}

	pci_set_master(pci);

	res_acp = acp_res;
	num_res = ARRAY_SIZE(acp_res);

	switch (pci->revision) {
	case 0x01:
		chip->name = "acp_asoc_renoir";
		chip->acp_rev = ACP3X_DEV;
		break;
	case 0x6f:
		chip->name = "acp_asoc_rembrandt";
		chip->acp_rev = ACP6X_DEV;
		break;
	case 0x63:
		chip->name = "acp_asoc_acp63";
		chip->acp_rev = ACP63_DEV;
		break;
	case 0x70:
		chip->name = "acp_asoc_acp70";
		chip->acp_rev = ACP70_DEV;
		break;
	default:
		dev_err(dev, "Unsupported device revision:0x%x\n", pci->revision);
		ret = -EINVAL;
		goto release_regions;
	}
	dmic_dev = platform_device_register_data(dev, "dmic-codec", PLATFORM_DEVID_NONE, NULL, 0);
	if (IS_ERR(dmic_dev)) {
		dev_err(dev, "failed to create DMIC device\n");
		ret = PTR_ERR(dmic_dev);
		goto release_regions;
	}

	addr = pci_resource_start(pci, 0);
	chip->base = devm_ioremap(&pci->dev, addr, pci_resource_len(pci, 0));
	if (!chip->base) {
		ret = -ENOMEM;
		goto unregister_dmic_dev;
	}

	ret = acp_init(chip);
	if (ret)
		goto unregister_dmic_dev;

	check_acp_config(pci, chip);
	if (!chip->is_pdm_dev && !chip->is_i2s_config)
		goto skip_pdev_creation;

	res = devm_kcalloc(&pci->dev, num_res, sizeof(struct resource), GFP_KERNEL);
	if (!res) {
		ret = -ENOMEM;
		goto unregister_dmic_dev;
	}

	for (i = 0; i < num_res; i++, res_acp++) {
		res[i].name = res_acp->name;
		res[i].flags = res_acp->flags;
		res[i].start = addr + res_acp->start;
		res[i].end = addr + res_acp->end;
		if (res_acp->flags == IORESOURCE_IRQ) {
			res[i].start = pci->irq;
			res[i].end = res[i].start;
		}
	}

	chip->flag = flag;
	memset(&pdevinfo, 0, sizeof(pdevinfo));

	pdevinfo.name = chip->name;
	pdevinfo.id = 0;
	pdevinfo.parent = &pci->dev;
	pdevinfo.num_res = num_res;
	pdevinfo.res = &res[0];
	pdevinfo.data = chip;
	pdevinfo.size_data = sizeof(*chip);

	pdev = platform_device_register_full(&pdevinfo);
	if (IS_ERR(pdev)) {
		dev_err(&pci->dev, "cannot register %s device\n", pdevinfo.name);
		ret = PTR_ERR(pdev);
		goto unregister_dmic_dev;
	}

skip_pdev_creation:
	chip->chip_pdev = pdev;
	dev_set_drvdata(&pci->dev, chip);
	pm_runtime_set_autosuspend_delay(&pci->dev, 2000);
	pm_runtime_use_autosuspend(&pci->dev);
	pm_runtime_put_noidle(&pci->dev);
	pm_runtime_allow(&pci->dev);
	return ret;

unregister_dmic_dev:
	platform_device_unregister(dmic_dev);
release_regions:
	pci_release_regions(pci);
disable_pci:
	pci_disable_device(pci);

	return ret;
};

static int __maybe_unused snd_acp_suspend(struct device *dev)
{
	struct acp_chip_info *chip;
	int ret;

	chip = dev_get_drvdata(dev);
	ret = acp_deinit(chip);
	if (ret)
		dev_err(dev, "ACP de-init failed\n");
	return ret;
}

static int __maybe_unused snd_acp_resume(struct device *dev)
{
	struct acp_chip_info *chip;
	struct acp_dev_data *adata;
	struct device child;
	int ret;

	chip = dev_get_drvdata(dev);
	ret = acp_init(chip);
	if (ret)
		dev_err(dev, "ACP init failed\n");
	child = chip->chip_pdev->dev;
	adata = dev_get_drvdata(&child);
	if (adata)
		acp_enable_interrupts(adata);
	return ret;
}

static const struct dev_pm_ops acp_pm_ops = {
	SET_RUNTIME_PM_OPS(snd_acp_suspend, snd_acp_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(snd_acp_suspend, snd_acp_resume)
};

static void acp_pci_remove(struct pci_dev *pci)
{
	struct acp_chip_info *chip;
	int ret;

	chip = pci_get_drvdata(pci);
	pm_runtime_forbid(&pci->dev);
	pm_runtime_get_noresume(&pci->dev);
	if (dmic_dev)
		platform_device_unregister(dmic_dev);
	if (pdev)
		platform_device_unregister(pdev);
	ret = acp_deinit(chip);
	if (ret)
		dev_err(&pci->dev, "ACP de-init failed\n");
}

/* PCI IDs */
static const struct pci_device_id acp_pci_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_AMD, ACP_PCI_DEV_ID)},
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, acp_pci_ids);

/* pci_driver definition */
static struct pci_driver snd_amd_acp_pci_driver = {
	.name = KBUILD_MODNAME,
	.id_table = acp_pci_ids,
	.probe = acp_pci_probe,
	.remove = acp_pci_remove,
	.driver = {
		.pm = &acp_pm_ops,
	},
};
module_pci_driver(snd_amd_acp_pci_driver);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_IMPORT_NS(SND_SOC_ACP_COMMON);
MODULE_ALIAS(DRV_NAME);
