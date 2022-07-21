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

#include "rpl_acp6x.h"

struct rpl_dev_data {
	void __iomem *acp6x_base;
};

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
	return 0;
release_regions:
	pci_release_regions(pci);
disable_pci:
	pci_disable_device(pci);

	return ret;
}

static void snd_rpl_remove(struct pci_dev *pci)
{
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
};

module_pci_driver(rpl_acp6x_driver);

MODULE_DESCRIPTION("AMD ACP RPL PCI driver");
MODULE_LICENSE("GPL v2");
