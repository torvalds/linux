/*
 * Intel SST loader on ACPI systems
 *
 * Copyright (C) 2013, Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/acpi.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include "sst-dsp.h"

#define SST_LPT_DSP_DMA_ADDR_OFFSET	0x0F0000
#define SST_WPT_DSP_DMA_ADDR_OFFSET	0x0FE000
#define SST_LPT_DSP_DMA_SIZE		(1024 - 1)

/* Descriptor for setting up SST platform data */
struct sst_acpi_desc {
	const char *drv_name;
	/* Platform resource indexes. Must set to -1 if not used */
	int resindex_lpe_base;
	int resindex_pcicfg_base;
	int resindex_fw_base;
	int irqindex_host_ipc;
	int resindex_dma_base;
	/* Unique number identifying the SST core on platform */
	int sst_id;
	/* firmware file name */
	const char *fw_filename;
	/* DMA only valid when resindex_dma_base != -1*/
	int dma_engine;
	int dma_size;
};

/* Descriptor for SST ASoC machine driver */
struct sst_acpi_mach {
	const char *drv_name;
	struct sst_acpi_desc *res_desc;
};

struct sst_acpi_priv {
	struct platform_device *pdev_mach;
	struct platform_device *pdev_pcm;
	struct sst_pdata sst_pdata;
	struct sst_acpi_desc *desc;
};

static int sst_acpi_probe(struct platform_device *pdev)
{
	const struct acpi_device_id *id;
	struct device *dev = &pdev->dev;
	struct sst_acpi_priv *sst_acpi;
	struct sst_pdata *sst_pdata;
	struct sst_acpi_mach *mach;
	struct sst_acpi_desc *desc;
	struct resource *mmio;
	int ret = 0;

	sst_acpi = devm_kzalloc(dev, sizeof(*sst_acpi), GFP_KERNEL);
	if (sst_acpi == NULL)
		return -ENOMEM;

	id = acpi_match_device(dev->driver->acpi_match_table, dev);
	if (!id)
		return -ENODEV;

	mach = (struct sst_acpi_mach *)id->driver_data;
	desc = mach->res_desc;
	sst_pdata = &sst_acpi->sst_pdata;
	sst_pdata->id = desc->sst_id;
	sst_pdata->fw_filename = desc->fw_filename;
	sst_acpi->desc = desc;

	if (desc->resindex_dma_base >= 0) {
		sst_pdata->dma_engine = desc->dma_engine;
		sst_pdata->dma_base = desc->resindex_dma_base;
		sst_pdata->dma_size = desc->dma_size;
	}

	if (desc->irqindex_host_ipc >= 0)
		sst_pdata->irq = platform_get_irq(pdev, desc->irqindex_host_ipc);

	if (desc->resindex_lpe_base >= 0) {
		mmio = platform_get_resource(pdev, IORESOURCE_MEM,
					     desc->resindex_lpe_base);
		if (mmio) {
			sst_pdata->lpe_base = mmio->start;
			sst_pdata->lpe_size = resource_size(mmio);
		}
	}

	if (desc->resindex_pcicfg_base >= 0) {
		mmio = platform_get_resource(pdev, IORESOURCE_MEM,
					     desc->resindex_pcicfg_base);
		if (mmio) {
			sst_pdata->pcicfg_base = mmio->start;
			sst_pdata->pcicfg_size = resource_size(mmio);
		}
	}

	if (desc->resindex_fw_base >= 0) {
		mmio = platform_get_resource(pdev, IORESOURCE_MEM,
					     desc->resindex_fw_base);
		if (mmio) {
			sst_pdata->fw_base = mmio->start;
			sst_pdata->fw_size = resource_size(mmio);
		}
	}

	/* register PCM and DAI driver */
	sst_acpi->pdev_pcm =
		platform_device_register_data(dev, desc->drv_name, -1,
					      sst_pdata, sizeof(*sst_pdata));
	if (IS_ERR(sst_acpi->pdev_pcm))
		return PTR_ERR(sst_acpi->pdev_pcm);

	/* register machine driver */
	platform_set_drvdata(pdev, sst_acpi);

	sst_acpi->pdev_mach =
		platform_device_register_data(dev, mach->drv_name, -1,
					      sst_pdata, sizeof(*sst_pdata));
	if (IS_ERR(sst_acpi->pdev_mach)) {
		ret = PTR_ERR(sst_acpi->pdev_mach);
		goto sst_err;
	}

	return ret;

sst_err:
	platform_device_unregister(sst_acpi->pdev_pcm);
	return ret;
}

static int sst_acpi_remove(struct platform_device *pdev)
{
	struct sst_acpi_priv *sst_acpi = platform_get_drvdata(pdev);

	platform_device_unregister(sst_acpi->pdev_mach);
	platform_device_unregister(sst_acpi->pdev_pcm);

	return 0;
}

static struct sst_acpi_desc sst_acpi_haswell_desc = {
	.drv_name = "haswell-pcm-audio",
	.resindex_lpe_base = 0,
	.resindex_pcicfg_base = 1,
	.resindex_fw_base = -1,
	.irqindex_host_ipc = 0,
	.sst_id = SST_DEV_ID_LYNX_POINT,
	.fw_filename = "intel/IntcSST1.bin",
	.dma_engine = SST_DMA_TYPE_DW,
	.resindex_dma_base = SST_LPT_DSP_DMA_ADDR_OFFSET,
	.dma_size = SST_LPT_DSP_DMA_SIZE,
};

static struct sst_acpi_desc sst_acpi_broadwell_desc = {
	.drv_name = "haswell-pcm-audio",
	.resindex_lpe_base = 0,
	.resindex_pcicfg_base = 1,
	.resindex_fw_base = -1,
	.irqindex_host_ipc = 0,
	.sst_id = SST_DEV_ID_WILDCAT_POINT,
	.fw_filename = "intel/IntcSST2.bin",
	.dma_engine = SST_DMA_TYPE_DW,
	.resindex_dma_base = SST_WPT_DSP_DMA_ADDR_OFFSET,
	.dma_size = SST_LPT_DSP_DMA_SIZE,
};

static struct sst_acpi_mach haswell_mach = {
	.drv_name = "haswell-audio",
	.res_desc = &sst_acpi_haswell_desc,
};

static struct sst_acpi_mach broadwell_mach = {
	.drv_name = "broadwell-audio",
	.res_desc = &sst_acpi_broadwell_desc,
};

static struct acpi_device_id sst_acpi_match[] = {
	{ "INT33C8", (unsigned long)&haswell_mach },
	{ "INT3438", (unsigned long)&broadwell_mach },
	{ }
};
MODULE_DEVICE_TABLE(acpi, sst_acpi_match);

static struct platform_driver sst_acpi_driver = {
	.probe = sst_acpi_probe,
	.remove = sst_acpi_remove,
	.driver = {
		.name = "sst-acpi",
		.owner = THIS_MODULE,
		.acpi_match_table = ACPI_PTR(sst_acpi_match),
	},
};
module_platform_driver(sst_acpi_driver);

MODULE_AUTHOR("Jarkko Nikula <jarkko.nikula@linux.intel.com>");
MODULE_DESCRIPTION("Intel SST loader on ACPI systems");
MODULE_LICENSE("GPL v2");
