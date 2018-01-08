// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2018 Intel Corporation. All rights reserved.
//
// Author: Liam Girdwood <liam.r.girdwood@linux.intel.com>
//

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/pm_runtime.h>
#include <linux/platform_device.h>
#include <linux/firmware.h>
#include <sound/pcm.h>
#include <sound/sof.h>
#include <linux/spi/spi.h>
#include <linux/of_device.h>
#include <linux/gpio/consumer.h>
#include "sof-priv.h"
#include "hw-spi.h"

static const struct dev_pm_ops sof_spi_pm = {
	SET_SYSTEM_SLEEP_PM_OPS(snd_sof_suspend, snd_sof_resume)
	SET_RUNTIME_PM_OPS(snd_sof_runtime_suspend, snd_sof_runtime_resume,
			   NULL)
	.suspend_late = snd_sof_suspend_late,
};

/* FIXME: replace with some meaningful values */
static struct snd_sof_machine sof_spi_machines[] = {
	{
		.id = "INT343A",
		.drv_name = "bxt_alc298s_i2s",
		.sof_fw_filename = "intel/sof-spi.ri",
		.sof_tplg_filename = "intel/sof-spi.tplg",
		.asoc_plat_name = "0000:00:0e.0",
		.ops = &snd_sof_spi_ops,
	},
};

static const struct sof_dev_desc spi_desc = {
	.sof_machines		= sof_spi_machines,
	.nocodec_fw_filename	= "intel/sof-spi.ri",
	.nocodec_tplg_filename	= "intel/sof-spi.tplg",
	.resindex_lpe_base = -1,
	.resindex_pcicfg_base = -1,
	.resindex_imr_base = -1,
	.irqindex_host_ipc = -1,
	.resindex_dma_base = -1,
};

static int sof_spi_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	const struct sof_dev_desc *desc = of_device_get_match_data(dev);
	struct snd_sof_machine *machines;
	struct snd_sof_machine *mach;
	struct snd_sof_pdata *sof_pdata;
	struct sof_platform_priv *priv;
	const char *tplg, *fw;
	struct gpio_desc *gpiod;
	int ret, irq;

	if (!dev->of_node || !desc)
		return -ENODEV;

	machines = desc->sof_machines;
	if (!machines)
		return -ENODEV;

	dev_dbg(&spi->dev, "SPI DSP detected");

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	spi_set_drvdata(spi, priv);

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

	sof_pdata->gpio = desc_to_gpio(gpiod);

	irq = gpiod_to_irq(gpiod);
	if (irq < 0)
		return irq;

	/* TODO: add any required regulators */

	/* use nocodec machine atm */
	dev_err(dev, "No matching ASoC machine driver found - using nocodec\n");
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
	mach->ops = machines[0].ops;
	mach->asoc_plat_name = "sof-platform";

	sof_pdata->id = -1;
	sof_pdata->name = dev_name(&spi->dev);
	sof_pdata->sof_machine = mach;
	sof_pdata->desc = desc;
	priv->sof_pdata = sof_pdata;
	sof_pdata->dev = dev;
	sof_pdata->type = SOF_DEVICE_SPI;

	/* register sof-audio platform driver */
	ret = sof_create_platform_device(priv);
	if (ret) {
		dev_err(dev, "error: failed to create platform device!\n");
		return ret;
	}

	spi->irq = irq;

	/* allow runtime_pm */
	pm_runtime_set_autosuspend_delay(dev, SND_SOF_SUSPEND_DELAY);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_allow(dev);

	return ret;
}

static int sof_spi_remove(struct spi_device *spi)
{
	struct sof_platform_priv *priv = spi_get_drvdata(spi);
	struct snd_sof_pdata *sof_pdata = priv->sof_pdata;

	platform_device_unregister(sof_pdata->pdev_mach);
	if (!IS_ERR_OR_NULL(priv->pdev_pcm))
		platform_device_unregister(priv->pdev_pcm);
	release_firmware(sof_pdata->fw);

	return 0;
}

const struct of_device_id sof_of_match[] = {
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
