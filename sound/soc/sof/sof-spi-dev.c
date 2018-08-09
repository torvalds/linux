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
#include "sof-priv.h"

static const struct dev_pm_ops sof_spi_pm = {
	SET_SYSTEM_SLEEP_PM_OPS(snd_sof_suspend, snd_sof_resume)
	SET_RUNTIME_PM_OPS(snd_sof_runtime_suspend, snd_sof_runtime_resume,
			   NULL)
	.suspend_late = snd_sof_suspend_late,
};

static int sof_spi_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	const struct snd_sof_machine *mach;
	struct snd_sof_machine *m;
	struct snd_sof_pdata *sof_pdata;
	struct sof_platform_priv *priv;
	int ret = 0;

	dev_dbg(&spi->dev, "SPI DSP detected");

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	spi_set_drvdata(spi, priv);

	sof_pdata = devm_kzalloc(dev, sizeof(*sof_pdata), GFP_KERNEL);
	if (!sof_pdata)
		return -ENOMEM;

	/* use nocodec machine atm */
	dev_err(dev, "No matching ASoC machine driver found - using nocodec\n");
	sof_pdata->drv_name = "sof-nocodec";
	m = devm_kzalloc(dev, sizeof(*mach), GFP_KERNEL);
	if (!m)
		return -ENOMEM;

	m->drv_name = "sof-nocodec";
	m->sof_fw_filename = desc->nocodec_fw_filename;
	m->sof_tplg_filename = desc->nocodec_tplg_filename;
	m->ops = desc->machines[0].ops;
	m->asoc_plat_name = "sof-platform";
	mach = m;

	sof_pdata->id = pci_id->device;
	sof_pdata->name = spi_name(spi);
	sof_pdata->machine = mach;
	sof_pdata->desc = (struct sof_dev_desc *)pci_id->driver_data;
	priv->sof_pdata = sof_pdata;
	sof_pdata->dev = dev;
	sof_pdata->type = SOF_DEVICE_SPI;

	/* register machine driver */
	sof_pdata->pdev_mach =
		platform_device_register_data(dev, mach->drv_name, -1,
					      sof_pdata, sizeof(*sof_pdata));
	if (IS_ERR(sof_pdata->pdev_mach))
		return PTR_ERR(sof_pdata->pdev_mach);
	dev_dbg(dev, "created machine %s\n",
		dev_name(&sof_pdata->pdev_mach->dev));

	/* register sof-audio platform driver */
	ret = sof_create_platform_device(priv);
	if (ret) {
		platform_device_unregister(sof_pdata->pdev_mach);
		dev_err(dev, "error: failed to create platform device!\n");
		return ret;
	}

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
}

static struct spi_driver wm8731_spi_driver = {
	.driver = {
		.name	= "sof-spi-dev",
		.of_match_table = sof_of_match,
	},
	.probe		= sof_spi_probe,
	.remove		= sof_spi_remove,
};

static const struct snd_sof_machine sof_spi_machines[] = {
	{ "INT343A", "bxt_alc298s_i2s", "intel/sof-spi.ri",
		"intel/sof-spi.tplg", "0000:00:0e.0", &snd_sof_spi_ops },
};

static const struct sof_dev_desc spi_desc = {
	.machines		= sof_spi_machines,
	.nocodec_fw_filename = "intel/sof-spi.ri",
	.nocodec_tplg_filename = "intel/sof-spi.tplg"
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
