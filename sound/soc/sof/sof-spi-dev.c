// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2018 Intel Corporation. All rights reserved.
//
// Author: Liam Girdwood <liam.r.girdwood@linux.intel.com>
//

#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/firmware.h>
#include <linux/of_device.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/spi/spi.h>
#include <sound/pcm.h>
#include <sound/sof.h>
#include "intel/shim.h"
#include "sof-priv.h"
#include "hw-spi.h"
#include "ops.h"

static char *fw_path;
module_param(fw_path, charp, 0444);
MODULE_PARM_DESC(fw_path, "alternate path for SOF firmware.");

static char *tplg_path;
module_param(tplg_path, charp, 0444);
MODULE_PARM_DESC(tplg_path, "alternate path for SOF topology.");

/* FIXME: replace with some meaningful values */
static struct snd_soc_acpi_mach spi_machines[] = {
	{
		.id = "INT343A",
		.drv_name = "bxt_alc298s_i2s",
		.sof_fw_filename = "sof-spi.ri",
		.sof_tplg_filename = "sof-spi.tplg",
	},
};

static const struct sof_dev_desc spi_desc = {
	.machines		= spi_machines,
	.default_fw_path = "intel/sof",
	.default_tplg_path = "intel/sof-tplg",
	.nocodec_fw_filename	= "sof-spi.ri",
	.nocodec_tplg_filename	= "sof-spi.tplg",
	.resindex_lpe_base = -1,
	.resindex_pcicfg_base = -1,
	.resindex_imr_base = -1,
	.irqindex_host_ipc = -1,
	.resindex_dma_base = -1,
	.chip_info = &spi_chip_info,
	.ops = &snd_sof_spi_ops
};

static int sof_spi_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	const struct sof_dev_desc *desc = of_device_get_match_data(dev);
	struct snd_soc_acpi_mach *machines, *mach;
	struct snd_sof_pdata *sof_pdata;
	struct sof_spi_dev *sof_spi;
	const char *tplg, *fw;
	struct gpio_desc *gpiod;
	int ret, irq;

	if (!dev->of_node || !desc)
		return -ENODEV;

	machines = desc->machines;
	if (!machines)
		return -ENODEV;

	dev_dbg(&spi->dev, "SPI DSP detected");

	sof_pdata = devm_kzalloc(dev, sizeof(*sof_pdata), GFP_KERNEL);
	if (!sof_pdata)
		return -ENOMEM;

	ret = of_property_read_string(dev->of_node, "tplg_filename", &tplg);
	if (ret < 0 || !tplg)
		return -EINVAL;

	ret = of_property_read_string(dev->of_node, "fw_filename", &fw);
	if (ret < 0 || !fw)
		return -EINVAL;

	/*
	 * Get an IRQ GPIO descriptor from an "irq-gpios" property
	 * If the IRQ is optional, use devm_gpiod_get_optional()
	 */
	gpiod = devm_gpiod_get(dev, "irq", GPIOD_IN);
	if (IS_ERR(gpiod))
		return PTR_ERR(gpiod);

	sof_spi = devm_kzalloc(dev, sizeof(*sof_spi), GFP_KERNEL);
	if (!sof_spi)
		return -ENOMEM;

	sof_spi->gpio = desc_to_gpio(gpiod);
	sof_pdata->hw_pdata = sof_spi;

	irq = gpiod_to_irq(gpiod);
	if (irq < 0)
		return irq;

	/* TODO: add any required regulators */

	/* use nocodec machine atm */
	dev_err(dev, "error: no matching ASoC machine driver found - using nocodec\n");
	sof_pdata->drv_name = "sof-nocodec";
	mach = devm_kzalloc(dev, sizeof(*mach), GFP_KERNEL);
	if (!mach)
		return -ENOMEM;

	mach->drv_name = "sof-nocodec";
	/*
	 * desc->nocodec_*_filename are selected as long as nocodec is used.
	 * Later machine->*_filename will have to be used.
	 */
	mach->sof_fw_filename = desc->nocodec_fw_filename;
	mach->sof_tplg_filename = desc->nocodec_tplg_filename;
	mach->mach_params.platform = dev_name(dev);

	sof_pdata->name = dev_name(&spi->dev);
	sof_pdata->machine = mach;
	sof_pdata->desc = desc;
	sof_pdata->dev = dev;

	/* alternate fw and tplg filenames ? */
	if (fw_path)
		sof_pdata->fw_filename_prefix = fw_path;
	else
		sof_pdata->fw_filename_prefix =
			sof_pdata->desc->default_fw_path;

	if (tplg_path)
		sof_pdata->tplg_filename_prefix = tplg_path;
	else
		sof_pdata->tplg_filename_prefix =
			sof_pdata->desc->default_tplg_path;

	sof_pdata->fw_filename = mach->sof_fw_filename;
	sof_pdata->tplg_filename = mach->sof_tplg_filename;

	/* call sof helper for DSP hardware probe */
	ret = snd_sof_device_probe(dev, sof_pdata);
	if (ret) {
		dev_err(dev, "error: failed to probe DSP hardware!\n");
		return ret;
	}

	spi->irq = irq;

	/* allow runtime_pm */
	pm_runtime_set_autosuspend_delay(dev, SND_SOF_SUSPEND_DELAY_MS);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_allow(dev);

	return ret;
}

static int sof_spi_remove(struct spi_device *spi)
{
	/* call sof helper for DSP hardware remove */
	snd_sof_device_remove(&spi->dev);

	return 0;
}

static const struct of_device_id sof_of_match[] = {
	{ .compatible = "sof,spi-sue-creek", .data = &spi_desc },
	{ }
};

static struct spi_driver sof_spi_driver = {
	.driver = {
		.name	= "sof-spi-dev",
		.of_match_table = sof_of_match,
	},
	.probe		= sof_spi_probe,
	.remove		= sof_spi_remove,
};

static int __init sof_spi_modinit(void)
{
	int ret;

	ret = spi_register_driver(&sof_spi_driver);
	if (ret != 0)
		pr_err("Failed to register SOF SPI driver: %d\n", ret);

	return ret;
}
module_init(sof_spi_modinit);

static void __exit sof_spi_modexit(void)
{
	spi_unregister_driver(&sof_spi_driver);
}
module_exit(sof_spi_modexit);

MODULE_LICENSE("Dual BSD/GPL");
