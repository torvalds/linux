// SPDX-License-Identifier: GPL-2.0-only
/*
 * sst_acpi.c - SST (LPE) driver init file for ACPI enumeration.
 *
 * Copyright (c) 2013, Intel Corporation.
 *
 *  Authors:	Ramesh Babu K V <Ramesh.Babu@intel.com>
 *  Authors:	Omair Mohammed Abdullah <omair.m.abdullah@intel.com>
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/firmware.h>
#include <linux/pm_runtime.h>
#include <linux/pm_qos.h>
#include <linux/dmi.h>
#include <linux/acpi.h>
#include <asm/platform_sst_audio.h>
#include <sound/core.h>
#include <sound/intel-dsp-config.h>
#include <sound/soc.h>
#include <sound/compress_driver.h>
#include <acpi/acbuffer.h>
#include <acpi/platform/acenv.h>
#include <acpi/platform/aclinux.h>
#include <acpi/actypes.h>
#include <acpi/acpi_bus.h>
#include <sound/soc-acpi.h>
#include <sound/soc-acpi-intel-match.h>
#include "../sst-mfld-platform.h"
#include "../../common/soc-intel-quirks.h"
#include "sst.h"

/* LPE viewpoint addresses */
#define SST_BYT_IRAM_PHY_START	0xff2c0000
#define SST_BYT_IRAM_PHY_END	0xff2d4000
#define SST_BYT_DRAM_PHY_START	0xff300000
#define SST_BYT_DRAM_PHY_END	0xff320000
#define SST_BYT_IMR_VIRT_START	0xc0000000 /* virtual addr in LPE */
#define SST_BYT_IMR_VIRT_END	0xc01fffff
#define SST_BYT_SHIM_PHY_ADDR	0xff340000
#define SST_BYT_MBOX_PHY_ADDR	0xff344000
#define SST_BYT_DMA0_PHY_ADDR	0xff298000
#define SST_BYT_DMA1_PHY_ADDR	0xff29c000
#define SST_BYT_SSP0_PHY_ADDR	0xff2a0000
#define SST_BYT_SSP2_PHY_ADDR	0xff2a2000

#define BYT_FW_MOD_TABLE_OFFSET	0x80000
#define BYT_FW_MOD_TABLE_SIZE	0x100
#define BYT_FW_MOD_OFFSET	(BYT_FW_MOD_TABLE_OFFSET + BYT_FW_MOD_TABLE_SIZE)

static const struct sst_info byt_fwparse_info = {
	.use_elf	= false,
	.max_streams	= 25,
	.iram_start	= SST_BYT_IRAM_PHY_START,
	.iram_end	= SST_BYT_IRAM_PHY_END,
	.iram_use	= true,
	.dram_start	= SST_BYT_DRAM_PHY_START,
	.dram_end	= SST_BYT_DRAM_PHY_END,
	.dram_use	= true,
	.imr_start	= SST_BYT_IMR_VIRT_START,
	.imr_end	= SST_BYT_IMR_VIRT_END,
	.imr_use	= true,
	.mailbox_start	= SST_BYT_MBOX_PHY_ADDR,
	.num_probes	= 0,
	.lpe_viewpt_rqd  = true,
};

static const struct sst_ipc_info byt_ipc_info = {
	.ipc_offset = 0,
	.mbox_recv_off = 0x400,
};

static const struct sst_lib_dnld_info  byt_lib_dnld_info = {
	.mod_base           = SST_BYT_IMR_VIRT_START,
	.mod_end            = SST_BYT_IMR_VIRT_END,
	.mod_table_offset   = BYT_FW_MOD_TABLE_OFFSET,
	.mod_table_size     = BYT_FW_MOD_TABLE_SIZE,
	.mod_ddr_dnld       = false,
};

static const struct sst_res_info byt_rvp_res_info = {
	.shim_offset = 0x140000,
	.shim_size = 0x000100,
	.shim_phy_addr = SST_BYT_SHIM_PHY_ADDR,
	.ssp0_offset = 0xa0000,
	.ssp0_size = 0x1000,
	.dma0_offset = 0x98000,
	.dma0_size = 0x4000,
	.dma1_offset = 0x9c000,
	.dma1_size = 0x4000,
	.iram_offset = 0x0c0000,
	.iram_size = 0x14000,
	.dram_offset = 0x100000,
	.dram_size = 0x28000,
	.mbox_offset = 0x144000,
	.mbox_size = 0x1000,
	.acpi_lpe_res_index = 0,
	.acpi_ddr_index = 2,
	.acpi_ipc_irq_index = 5,
};

/* BYTCR has different BIOS from BYT */
static const struct sst_res_info bytcr_res_info = {
	.shim_offset = 0x140000,
	.shim_size = 0x000100,
	.shim_phy_addr = SST_BYT_SHIM_PHY_ADDR,
	.ssp0_offset = 0xa0000,
	.ssp0_size = 0x1000,
	.dma0_offset = 0x98000,
	.dma0_size = 0x4000,
	.dma1_offset = 0x9c000,
	.dma1_size = 0x4000,
	.iram_offset = 0x0c0000,
	.iram_size = 0x14000,
	.dram_offset = 0x100000,
	.dram_size = 0x28000,
	.mbox_offset = 0x144000,
	.mbox_size = 0x1000,
	.acpi_lpe_res_index = 0,
	.acpi_ddr_index = 2,
	.acpi_ipc_irq_index = 0
};

static struct sst_platform_info byt_rvp_platform_data = {
	.probe_data = &byt_fwparse_info,
	.ipc_info = &byt_ipc_info,
	.lib_info = &byt_lib_dnld_info,
	.res_info = &byt_rvp_res_info,
	.platform = "sst-mfld-platform",
	.streams_lost_on_suspend = true,
};

/* Cherryview (Cherrytrail and Braswell) uses same mrfld dpcm fw as Baytrail,
 * so pdata is same as Baytrail, minus the streams_lost_on_suspend quirk.
 */
static struct sst_platform_info chv_platform_data = {
	.probe_data = &byt_fwparse_info,
	.ipc_info = &byt_ipc_info,
	.lib_info = &byt_lib_dnld_info,
	.res_info = &byt_rvp_res_info,
	.platform = "sst-mfld-platform",
};

static int sst_platform_get_resources(struct intel_sst_drv *ctx)
{
	struct resource *rsrc;
	struct platform_device *pdev = to_platform_device(ctx->dev);

	/* All ACPI resource request here */
	/* Get Shim addr */
	rsrc = platform_get_resource(pdev, IORESOURCE_MEM,
					ctx->pdata->res_info->acpi_lpe_res_index);
	if (!rsrc) {
		dev_err(ctx->dev, "Invalid SHIM base from IFWI\n");
		return -EIO;
	}
	dev_info(ctx->dev, "LPE base: %#x size:%#x", (unsigned int) rsrc->start,
					(unsigned int)resource_size(rsrc));

	ctx->iram_base = rsrc->start + ctx->pdata->res_info->iram_offset;
	ctx->iram_end =  ctx->iram_base + ctx->pdata->res_info->iram_size - 1;
	dev_info(ctx->dev, "IRAM base: %#x", ctx->iram_base);
	ctx->iram = devm_ioremap(ctx->dev, ctx->iram_base,
					 ctx->pdata->res_info->iram_size);
	if (!ctx->iram) {
		dev_err(ctx->dev, "unable to map IRAM\n");
		return -EIO;
	}

	ctx->dram_base = rsrc->start + ctx->pdata->res_info->dram_offset;
	ctx->dram_end = ctx->dram_base + ctx->pdata->res_info->dram_size - 1;
	dev_info(ctx->dev, "DRAM base: %#x", ctx->dram_base);
	ctx->dram = devm_ioremap(ctx->dev, ctx->dram_base,
					 ctx->pdata->res_info->dram_size);
	if (!ctx->dram) {
		dev_err(ctx->dev, "unable to map DRAM\n");
		return -EIO;
	}

	ctx->shim_phy_add = rsrc->start + ctx->pdata->res_info->shim_offset;
	dev_info(ctx->dev, "SHIM base: %#x", ctx->shim_phy_add);
	ctx->shim = devm_ioremap(ctx->dev, ctx->shim_phy_add,
					ctx->pdata->res_info->shim_size);
	if (!ctx->shim) {
		dev_err(ctx->dev, "unable to map SHIM\n");
		return -EIO;
	}

	/* reassign physical address to LPE viewpoint address */
	ctx->shim_phy_add = ctx->pdata->res_info->shim_phy_addr;

	/* Get mailbox addr */
	ctx->mailbox_add = rsrc->start + ctx->pdata->res_info->mbox_offset;
	dev_info(ctx->dev, "Mailbox base: %#x", ctx->mailbox_add);
	ctx->mailbox = devm_ioremap(ctx->dev, ctx->mailbox_add,
					    ctx->pdata->res_info->mbox_size);
	if (!ctx->mailbox) {
		dev_err(ctx->dev, "unable to map mailbox\n");
		return -EIO;
	}

	/* reassign physical address to LPE viewpoint address */
	ctx->mailbox_add = ctx->info.mailbox_start;

	rsrc = platform_get_resource(pdev, IORESOURCE_MEM,
					ctx->pdata->res_info->acpi_ddr_index);
	if (!rsrc) {
		dev_err(ctx->dev, "Invalid DDR base from IFWI\n");
		return -EIO;
	}
	ctx->ddr_base = rsrc->start;
	ctx->ddr_end = rsrc->end;
	dev_info(ctx->dev, "DDR base: %#x", ctx->ddr_base);
	ctx->ddr = devm_ioremap(ctx->dev, ctx->ddr_base,
					resource_size(rsrc));
	if (!ctx->ddr) {
		dev_err(ctx->dev, "unable to map DDR\n");
		return -EIO;
	}

	/* Find the IRQ */
	ctx->irq_num = platform_get_irq(pdev,
				ctx->pdata->res_info->acpi_ipc_irq_index);
	if (ctx->irq_num <= 0)
		return ctx->irq_num < 0 ? ctx->irq_num : -EIO;

	return 0;
}

static int sst_acpi_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int ret = 0;
	struct intel_sst_drv *ctx;
	const struct acpi_device_id *id;
	struct snd_soc_acpi_mach *mach;
	struct platform_device *mdev;
	struct platform_device *plat_dev;
	struct sst_platform_info *pdata;
	unsigned int dev_id;

	id = acpi_match_device(dev->driver->acpi_match_table, dev);
	if (!id)
		return -ENODEV;

	ret = snd_intel_acpi_dsp_driver_probe(dev, id->id);
	if (ret != SND_INTEL_DSP_DRIVER_ANY && ret != SND_INTEL_DSP_DRIVER_SST) {
		dev_dbg(dev, "SST ACPI driver not selected, aborting probe\n");
		return -ENODEV;
	}

	dev_dbg(dev, "for %s\n", id->id);

	mach = (struct snd_soc_acpi_mach *)id->driver_data;
	mach = snd_soc_acpi_find_machine(mach);
	if (mach == NULL) {
		dev_err(dev, "No matching machine driver found\n");
		return -ENODEV;
	}

	if (soc_intel_is_byt())
		mach->pdata = &byt_rvp_platform_data;
	else
		mach->pdata = &chv_platform_data;
	pdata = mach->pdata;

	ret = kstrtouint(id->id, 16, &dev_id);
	if (ret < 0) {
		dev_err(dev, "Unique device id conversion error: %d\n", ret);
		return ret;
	}

	dev_dbg(dev, "ACPI device id: %x\n", dev_id);

	ret = sst_alloc_drv_context(&ctx, dev, dev_id);
	if (ret < 0)
		return ret;

	if (soc_intel_is_byt_cr(pdev)) {
		/* override resource info */
		byt_rvp_platform_data.res_info = &bytcr_res_info;
	}

	/* update machine parameters */
	mach->mach_params.acpi_ipc_irq_index =
		pdata->res_info->acpi_ipc_irq_index;

	plat_dev = platform_device_register_data(dev, pdata->platform, -1,
						NULL, 0);
	if (IS_ERR(plat_dev)) {
		dev_err(dev, "Failed to create machine device: %s\n",
			pdata->platform);
		return PTR_ERR(plat_dev);
	}

	/*
	 * Create platform device for sst machine driver,
	 * pass machine info as pdata
	 */
	mdev = platform_device_register_data(dev, mach->drv_name, -1,
					(const void *)mach, sizeof(*mach));
	if (IS_ERR(mdev)) {
		dev_err(dev, "Failed to create machine device: %s\n",
			mach->drv_name);
		return PTR_ERR(mdev);
	}

	/* Fill sst platform data */
	ctx->pdata = pdata;
	strcpy(ctx->firmware_name, mach->fw_filename);

	ret = sst_platform_get_resources(ctx);
	if (ret)
		return ret;

	ret = sst_context_init(ctx);
	if (ret < 0)
		return ret;

	sst_configure_runtime_pm(ctx);
	platform_set_drvdata(pdev, ctx);
	return ret;
}

/**
* sst_acpi_remove - remove function
*
* @pdev:	platform device structure
*
* This function is called by OS when a device is unloaded
* This frees the interrupt etc
*/
static void sst_acpi_remove(struct platform_device *pdev)
{
	struct intel_sst_drv *ctx;

	ctx = platform_get_drvdata(pdev);
	sst_context_cleanup(ctx);
	platform_set_drvdata(pdev, NULL);
}

static const struct acpi_device_id sst_acpi_ids[] = {
	{ "80860F28", (unsigned long)&snd_soc_acpi_intel_baytrail_machines},
	{ "808622A8", (unsigned long)&snd_soc_acpi_intel_cherrytrail_machines},
	{ },
};

MODULE_DEVICE_TABLE(acpi, sst_acpi_ids);

static struct platform_driver sst_acpi_driver = {
	.driver = {
		.name			= "intel_sst_acpi",
		.acpi_match_table	= ACPI_PTR(sst_acpi_ids),
		.pm			= &intel_sst_pm,
	},
	.probe	= sst_acpi_probe,
	.remove_new = sst_acpi_remove,
};

module_platform_driver(sst_acpi_driver);

MODULE_DESCRIPTION("Intel (R) SST(R) Audio Engine ACPI Driver");
MODULE_AUTHOR("Ramesh Babu K V");
MODULE_AUTHOR("Omair Mohammed Abdullah");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("sst");
