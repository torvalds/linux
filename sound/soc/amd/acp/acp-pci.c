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
#include <linux/pm_runtime.h>
#include <linux/module.h>

#include "amd.h"
#include "../mach-config.h"

#define DRV_NAME "acp_pci"

#define ACP3x_REG_START	0x1240000
#define ACP3x_REG_END	0x125C000

static struct platform_device *dmic_dev;
static struct platform_device *pdev;

static const struct resource acp3x_res[] = {
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
	if (flag != FLAG_AMD_LEGACY)
		return -ENODEV;

	chip = devm_kzalloc(&pci->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	if (pci_enable_device(pci)) {
		dev_err(&pci->dev, "pci_enable_device failed\n");
		return -ENODEV;
	}

	ret = pci_request_regions(pci, "AMD ACP3x audio");
	if (ret < 0) {
		dev_err(&pci->dev, "pci_request_regions failed\n");
		return -ENOMEM;
	}

	pci_set_master(pci);

	switch (pci->revision) {
	case 0x01:
		res_acp = acp3x_res;
		num_res = ARRAY_SIZE(acp3x_res);
		chip->name = "acp_asoc_renoir";
		chip->acp_rev = ACP3X_DEV;
		break;
	case 0x6f:
		res_acp = acp3x_res;
		num_res = ARRAY_SIZE(acp3x_res);
		chip->name = "acp_asoc_rembrandt";
		chip->acp_rev = ACP6X_DEV;
		break;
	default:
		dev_err(dev, "Unsupported device revision:0x%x\n", pci->revision);
		return -EINVAL;
	}

	dmic_dev = platform_device_register_data(dev, "dmic-codec", PLATFORM_DEVID_NONE, NULL, 0);
	if (IS_ERR(dmic_dev)) {
		dev_err(dev, "failed to create DMIC device\n");
		return PTR_ERR(dmic_dev);
	}

	addr = pci_resource_start(pci, 0);
	chip->base = devm_ioremap(&pci->dev, addr, pci_resource_len(pci, 0));

	res = devm_kzalloc(&pci->dev, sizeof(struct resource) * num_res, GFP_KERNEL);
	if (!res) {
		platform_device_unregister(dmic_dev);
		return -ENOMEM;
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
		platform_device_unregister(dmic_dev);
		ret = PTR_ERR(pdev);
	}

	return ret;
};

static void acp_pci_remove(struct pci_dev *pci)
{
	if (dmic_dev)
		platform_device_unregister(dmic_dev);
	if (pdev)
		platform_device_unregister(pdev);
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
};
module_pci_driver(snd_amd_acp_pci_driver);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_ALIAS(DRV_NAME);
