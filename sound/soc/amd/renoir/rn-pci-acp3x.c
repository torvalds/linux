// SPDX-License-Identifier: GPL-2.0+
//
// AMD Renoir ACP PCI Driver
//
//Copyright 2020 Advanced Micro Devices, Inc.

#include <linux/pci.h>
#include <linux/module.h>
#include <linux/io.h>

#include "rn_acp3x.h"

struct acp_dev_data {
	void __iomem *acp_base;
};

static int snd_rn_acp_probe(struct pci_dev *pci,
			    const struct pci_device_id *pci_id)
{
	struct acp_dev_data *adata;
	int ret;
	u32 addr;

	if (pci_enable_device(pci)) {
		dev_err(&pci->dev, "pci_enable_device failed\n");
		return -ENODEV;
	}

	ret = pci_request_regions(pci, "AMD ACP3x audio");
	if (ret < 0) {
		dev_err(&pci->dev, "pci_request_regions failed\n");
		goto disable_pci;
	}

	adata = devm_kzalloc(&pci->dev, sizeof(struct acp_dev_data),
			     GFP_KERNEL);
	if (!adata) {
		ret = -ENOMEM;
		goto release_regions;
	}

	addr = pci_resource_start(pci, 0);
	adata->acp_base = devm_ioremap(&pci->dev, addr,
				       pci_resource_len(pci, 0));
	if (!adata->acp_base) {
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

static void snd_rn_acp_remove(struct pci_dev *pci)
{
	pci_disable_msi(pci);
	pci_release_regions(pci);
	pci_disable_device(pci);
}

static const struct pci_device_id snd_rn_acp_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_AMD, ACP_DEVICE_ID),
	.class = PCI_CLASS_MULTIMEDIA_OTHER << 8,
	.class_mask = 0xffffff },
	{ 0, },
};
MODULE_DEVICE_TABLE(pci, snd_rn_acp_ids);

static struct pci_driver rn_acp_driver  = {
	.name = KBUILD_MODNAME,
	.id_table = snd_rn_acp_ids,
	.probe = snd_rn_acp_probe,
	.remove = snd_rn_acp_remove,
};

module_pci_driver(rn_acp_driver);

MODULE_AUTHOR("Vijendar.Mukunda@amd.com");
MODULE_DESCRIPTION("AMD ACP Renoir PCI driver");
MODULE_LICENSE("GPL v2");
