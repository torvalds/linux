// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license. When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2021 Advanced Micro Devices, Inc. All rights reserved.
//
// Authors: Ajit Kumar Pandey <AjitKumar.Pandey@amd.com>

/*
 * PCI interface for Renoir ACP device
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <sound/sof.h>
#include <sound/soc-acpi.h>

#include "../ops.h"
#include "../sof-pci-dev.h"
#include "../../amd/mach-config.h"
#include "acp.h"

#define ACP3x_REG_START		0x1240000
#define ACP3x_REG_END		0x125C000

static struct platform_device *dmic_dev;
static struct platform_device *pdev;

static const struct resource renoir_res[] = {
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

static const struct sof_amd_acp_desc renoir_chip_info = {
	.host_bridge_id = HOST_BRIDGE_CZN,
};

static const struct sof_dev_desc renoir_desc = {
	.machines		= snd_soc_acpi_amd_sof_machines,
	.resindex_lpe_base	= 0,
	.resindex_pcicfg_base	= -1,
	.resindex_imr_base	= -1,
	.irqindex_host_ipc	= -1,
	.chip_info		= &renoir_chip_info,
	.default_fw_path	= "amd/sof",
	.default_tplg_path	= "amd/sof-tplg",
	.default_fw_filename	= "sof-rn.ri",
	.nocodec_tplg_filename	= "sof-acp.tplg",
	.ops			= &sof_renoir_ops,
};

static int acp_pci_rn_probe(struct pci_dev *pci, const struct pci_device_id *pci_id)
{
	struct platform_device_info pdevinfo;
	struct device *dev = &pci->dev;
	const struct resource *res_i2s;
	struct resource *res;
	unsigned int flag, i, addr;
	int ret;

	flag = snd_amd_acp_find_config(pci);
	if (flag != FLAG_AMD_SOF && flag != FLAG_AMD_SOF_ONLY_DMIC)
		return -ENODEV;

	ret = sof_pci_probe(pci, pci_id);
	if (ret != 0)
		return ret;

	dmic_dev = platform_device_register_data(dev, "dmic-codec", PLATFORM_DEVID_NONE, NULL, 0);
	if (IS_ERR(dmic_dev)) {
		dev_err(dev, "failed to create DMIC device\n");
		sof_pci_remove(pci);
		return PTR_ERR(dmic_dev);
	}

	/* Register platform device only if flag set to FLAG_AMD_SOF_ONLY_DMIC */
	if (flag != FLAG_AMD_SOF_ONLY_DMIC)
		return 0;

	addr = pci_resource_start(pci, 0);
	res = devm_kzalloc(&pci->dev, sizeof(struct resource) * ARRAY_SIZE(renoir_res), GFP_KERNEL);
	if (!res) {
		sof_pci_remove(pci);
		return -ENOMEM;
	}

	res_i2s = renoir_res;
	for (i = 0; i < ARRAY_SIZE(renoir_res); i++, res_i2s++) {
		res[i].name = res_i2s->name;
		res[i].flags = res_i2s->flags;
		res[i].start = addr + res_i2s->start;
		res[i].end = addr + res_i2s->end;
		if (res_i2s->flags == IORESOURCE_IRQ) {
			res[i].start = pci->irq;
			res[i].end = res[i].start;
		}
	}

	memset(&pdevinfo, 0, sizeof(pdevinfo));

	/*
	 * We have common PCI driver probe for ACP device but we have to support I2S without SOF
	 * for some distributions. Register platform device that will be used to support non dsp
	 * ACP's audio ends points on some machines.
	 */

	pdevinfo.name = "acp_asoc_renoir";
	pdevinfo.id = 0;
	pdevinfo.parent = &pci->dev;
	pdevinfo.num_res = ARRAY_SIZE(renoir_res);
	pdevinfo.res = &res[0];

	pdev = platform_device_register_full(&pdevinfo);
	if (IS_ERR(pdev)) {
		dev_err(&pci->dev, "cannot register %s device\n", pdevinfo.name);
		sof_pci_remove(pci);
		platform_device_unregister(dmic_dev);
		ret = PTR_ERR(pdev);
	}

	return ret;
};

static void acp_pci_rn_remove(struct pci_dev *pci)
{
	if (dmic_dev)
		platform_device_unregister(dmic_dev);
	if (pdev)
		platform_device_unregister(pdev);

	return sof_pci_remove(pci);
}

/* PCI IDs */
static const struct pci_device_id rn_pci_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_AMD, ACP_PCI_DEV_ID),
	.driver_data = (unsigned long)&renoir_desc},
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, rn_pci_ids);

/* pci_driver definition */
static struct pci_driver snd_sof_pci_amd_rn_driver = {
	.name = KBUILD_MODNAME,
	.id_table = rn_pci_ids,
	.probe = acp_pci_rn_probe,
	.remove = acp_pci_rn_remove,
};
module_pci_driver(snd_sof_pci_amd_rn_driver);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_IMPORT_NS(SND_SOC_SOF_AMD_COMMON);
MODULE_IMPORT_NS(SND_SOC_SOF_PCI_DEV);
