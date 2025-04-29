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

static irqreturn_t irq_handler(int irq, void *data)
{
	struct acp_chip_info *chip = data;

	if (chip && chip->acp_hw_ops && chip->acp_hw_ops->irq)
		return chip->acp_hw_ops->irq(irq, chip);

	return IRQ_NONE;
}
static void acp_fill_platform_dev_info(struct platform_device_info *pdevinfo,
				       struct device *parent,
				       struct fwnode_handle *fw_node,
				       char *name, unsigned int id,
				       const struct resource *res,
				       unsigned int num_res,
				       const void *data,
				       size_t size_data)
{
	pdevinfo->name = name;
	pdevinfo->id = id;
	pdevinfo->parent = parent;
	pdevinfo->num_res = num_res;
	pdevinfo->res = res;
	pdevinfo->data = data;
	pdevinfo->size_data = size_data;
	pdevinfo->fwnode = fw_node;
}

static int create_acp_platform_devs(struct pci_dev *pci, struct acp_chip_info *chip, u32 addr)
{
	struct platform_device_info pdevinfo;
	struct device *parent;
	int ret;

	parent = &pci->dev;

	if (chip->is_i2s_config || chip->is_pdm_dev) {
		chip->res = devm_kzalloc(&pci->dev, sizeof(struct resource), GFP_KERNEL);
		if (!chip->res) {
			ret = -ENOMEM;
			goto err;
		}
		chip->res->flags = IORESOURCE_MEM;
		chip->res->start = addr;
		chip->res->end = addr + (ACP3x_REG_END - ACP3x_REG_START);
		memset(&pdevinfo, 0, sizeof(pdevinfo));
	}

	memset(&pdevinfo, 0, sizeof(pdevinfo));
	acp_fill_platform_dev_info(&pdevinfo, parent, NULL, chip->name,
				   0, chip->res, 1, chip, sizeof(*chip));

	chip->acp_plat_dev = platform_device_register_full(&pdevinfo);
	if (IS_ERR(chip->acp_plat_dev)) {
		dev_err(&pci->dev,
			"cannot register %s device\n", pdevinfo.name);
		ret = PTR_ERR(chip->acp_plat_dev);
		goto err;
	}
	if (chip->is_pdm_dev && chip->is_pdm_config) {
		chip->dmic_codec_dev = platform_device_register_data(&pci->dev,
								     "dmic-codec",
								     PLATFORM_DEVID_NONE,
								     NULL, 0);
		if (IS_ERR(chip->dmic_codec_dev)) {
			dev_err(&pci->dev, "failed to create DMIC device\n");
			ret = PTR_ERR(chip->dmic_codec_dev);
			goto unregister_acp_plat_dev;
		}
	}
	return 0;
unregister_acp_plat_dev:
	platform_device_unregister(chip->acp_plat_dev);
err:
	return ret;
}

static int acp_pci_probe(struct pci_dev *pci, const struct pci_device_id *pci_id)
{
	struct device *dev = &pci->dev;
	struct acp_chip_info *chip;
	unsigned int flag, addr;
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

	chip->acp_rev = pci->revision;
	switch (pci->revision) {
	case 0x01:
		chip->name = "acp_asoc_renoir";
		chip->rsrc = &rn_rsrc;
		chip->acp_hw_ops_init = acp31_hw_ops_init;
		chip->machines = &snd_soc_acpi_amd_acp_machines;
		break;
	case 0x6f:
		chip->name = "acp_asoc_rembrandt";
		chip->rsrc = &rmb_rsrc;
		chip->acp_hw_ops_init = acp6x_hw_ops_init;
		chip->machines = &snd_soc_acpi_amd_rmb_acp_machines;
		break;
	case 0x63:
		chip->name = "acp_asoc_acp63";
		chip->rsrc = &acp63_rsrc;
		chip->acp_hw_ops_init = acp63_hw_ops_init;
		chip->machines = &snd_soc_acpi_amd_acp63_acp_machines;
		break;
	case 0x70:
	case 0x71:
		chip->name = "acp_asoc_acp70";
		chip->rsrc = &acp70_rsrc;
		chip->acp_hw_ops_init = acp70_hw_ops_init;
		chip->machines = &snd_soc_acpi_amd_acp70_acp_machines;
		break;
	default:
		dev_err(dev, "Unsupported device revision:0x%x\n", pci->revision);
		ret = -EINVAL;
		goto release_regions;
	}
	chip->flag = flag;

	addr = pci_resource_start(pci, 0);
	chip->base = devm_ioremap(&pci->dev, addr, pci_resource_len(pci, 0));
	if (!chip->base) {
		ret = -ENOMEM;
		goto release_regions;
	}

	chip->addr = addr;

	chip->acp_hw_ops_init(chip);
	ret = acp_hw_init(chip);
	if (ret)
		goto release_regions;

	ret = devm_request_irq(dev, pci->irq, irq_handler,
			       IRQF_SHARED, "ACP_I2S_IRQ", chip);
	if (ret) {
		dev_err(&pci->dev, "ACP I2S IRQ request failed %d\n", ret);
		goto de_init;
	}

	check_acp_config(pci, chip);
	if (!chip->is_pdm_dev && !chip->is_i2s_config)
		goto skip_pdev_creation;

	ret = create_acp_platform_devs(pci, chip, addr);
	if (ret < 0) {
		dev_err(&pci->dev, "ACP platform devices creation failed\n");
		goto de_init;
	}

	chip->chip_pdev = chip->acp_plat_dev;
	chip->dev = &chip->acp_plat_dev->dev;

	acp_machine_select(chip);

	INIT_LIST_HEAD(&chip->stream_list);
	spin_lock_init(&chip->acp_lock);
skip_pdev_creation:
	dev_set_drvdata(&pci->dev, chip);
	pm_runtime_set_autosuspend_delay(&pci->dev, 2000);
	pm_runtime_use_autosuspend(&pci->dev);
	pm_runtime_put_noidle(&pci->dev);
	pm_runtime_allow(&pci->dev);
	return ret;

de_init:
	acp_hw_deinit(chip);
release_regions:
	pci_release_regions(pci);
disable_pci:
	pci_disable_device(pci);

	return ret;
};

static int snd_acp_suspend(struct device *dev)
{
	struct acp_chip_info *chip;
	int ret;

	chip = dev_get_drvdata(dev);
	ret = acp_hw_deinit(chip);
	if (ret)
		dev_err(dev, "ACP de-init failed\n");
	return ret;
}

static int snd_acp_resume(struct device *dev)
{
	struct acp_chip_info *chip;
	int ret;

	chip = dev_get_drvdata(dev);
	ret = acp_hw_init(chip);
	if (ret)
		dev_err(dev, "ACP init failed\n");

	ret = acp_hw_en_interrupts(chip);
	if (ret)
		dev_err(dev, "ACP en-interrupts failed\n");

	return ret;
}

static const struct dev_pm_ops acp_pm_ops = {
	RUNTIME_PM_OPS(snd_acp_suspend, snd_acp_resume, NULL)
	SYSTEM_SLEEP_PM_OPS(snd_acp_suspend, snd_acp_resume)
};

static void acp_pci_remove(struct pci_dev *pci)
{
	struct acp_chip_info *chip;
	int ret;

	chip = pci_get_drvdata(pci);
	pm_runtime_forbid(&pci->dev);
	pm_runtime_get_noresume(&pci->dev);
	if (chip->dmic_codec_dev)
		platform_device_unregister(chip->dmic_codec_dev);
	if (chip->acp_plat_dev)
		platform_device_unregister(chip->acp_plat_dev);
	if (chip->mach_dev)
		platform_device_unregister(chip->mach_dev);

	ret = acp_hw_deinit(chip);
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
		.pm = pm_ptr(&acp_pm_ops),
	},
};
module_pci_driver(snd_amd_acp_pci_driver);

MODULE_DESCRIPTION("AMD ACP common PCI support");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_IMPORT_NS("SND_SOC_ACP_COMMON");
MODULE_ALIAS(DRV_NAME);
