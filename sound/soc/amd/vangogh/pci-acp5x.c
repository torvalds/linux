// SPDX-License-Identifier: GPL-2.0+
//
// AMD Vangogh ACP PCI Driver
//
// Copyright (C) 2021 Advanced Micro Devices, Inc. All rights reserved.

#include <linux/pci.h>
#include <linux/module.h>
#include <linux/io.h>

#include "acp5x.h"

struct acp5x_dev_data {
	void __iomem *acp5x_base;
};

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

release_regions:
	pci_release_regions(pci);
disable_pci:
	pci_disable_device(pci);

	return ret;
}

static void snd_acp5x_remove(struct pci_dev *pci)
{
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
