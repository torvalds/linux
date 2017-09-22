/*
 * sst_acpi.c - SST (LPE) driver init file for ACPI enumeration.
 *
 * Copyright (c) 2013, Intel Corporation.
 *
 *  Authors:	Ramesh Babu K V <Ramesh.Babu@intel.com>
 *  Authors:	Omair Mohammed Abdullah <omair.m.abdullah@intel.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 *
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/miscdevice.h>
#include <linux/platform_device.h>
#include <linux/firmware.h>
#include <linux/pm_runtime.h>
#include <linux/pm_qos.h>
#include <linux/dmi.h>
#include <linux/acpi.h>
#include <asm/platform_sst_audio.h>
#include <sound/core.h>
#include <sound/soc.h>
#include <sound/compress_driver.h>
#include <acpi/acbuffer.h>
#include <acpi/platform/acenv.h>
#include <acpi/platform/aclinux.h>
#include <acpi/actypes.h>
#include <acpi/acpi_bus.h>
#include <asm/cpu_device_id.h>
#include <asm/iosf_mbi.h>
#include "../sst-mfld-platform.h"
#include "../../common/sst-dsp.h"
#include "../../common/sst-acpi.h"
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
};

/* Cherryview (Cherrytrail and Braswell) uses same mrfld dpcm fw as Baytrail,
 * so pdata is same as Baytrail.
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
	ctx->iram = devm_ioremap_nocache(ctx->dev, ctx->iram_base,
					 ctx->pdata->res_info->iram_size);
	if (!ctx->iram) {
		dev_err(ctx->dev, "unable to map IRAM\n");
		return -EIO;
	}

	ctx->dram_base = rsrc->start + ctx->pdata->res_info->dram_offset;
	ctx->dram_end = ctx->dram_base + ctx->pdata->res_info->dram_size - 1;
	dev_info(ctx->dev, "DRAM base: %#x", ctx->dram_base);
	ctx->dram = devm_ioremap_nocache(ctx->dev, ctx->dram_base,
					 ctx->pdata->res_info->dram_size);
	if (!ctx->dram) {
		dev_err(ctx->dev, "unable to map DRAM\n");
		return -EIO;
	}

	ctx->shim_phy_add = rsrc->start + ctx->pdata->res_info->shim_offset;
	dev_info(ctx->dev, "SHIM base: %#x", ctx->shim_phy_add);
	ctx->shim = devm_ioremap_nocache(ctx->dev, ctx->shim_phy_add,
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
	ctx->mailbox = devm_ioremap_nocache(ctx->dev, ctx->mailbox_add,
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
	ctx->ddr = devm_ioremap_nocache(ctx->dev, ctx->ddr_base,
					resource_size(rsrc));
	if (!ctx->ddr) {
		dev_err(ctx->dev, "unable to map DDR\n");
		return -EIO;
	}

	/* Find the IRQ */
	ctx->irq_num = platform_get_irq(pdev,
				ctx->pdata->res_info->acpi_ipc_irq_index);
	return 0;
}


static int is_byt_cr(struct device *dev, bool *bytcr)
{
	int status = 0;

	if (IS_ENABLED(CONFIG_IOSF_MBI)) {
		static const struct x86_cpu_id cpu_ids[] = {
			{ X86_VENDOR_INTEL, 6, 55 }, /* Valleyview, Bay Trail */
			{}
		};
		u32 bios_status;

		if (!x86_match_cpu(cpu_ids) || !iosf_mbi_available()) {
			/* bail silently */
			return status;
		}

		status = iosf_mbi_read(BT_MBI_UNIT_PMC, /* 0x04 PUNIT */
				       MBI_REG_READ, /* 0x10 */
				       0x006, /* BIOS_CONFIG */
				       &bios_status);

		if (status) {
			dev_err(dev, "could not read PUNIT BIOS_CONFIG\n");
		} else {
			/* bits 26:27 mirror PMIC options */
			bios_status = (bios_status >> 26) & 3;

			if ((bios_status == 1) || (bios_status == 3))
				*bytcr = true;
			else
				dev_info(dev, "BYT-CR not detected\n");
		}
	} else {
		dev_info(dev, "IOSF_MBI not enabled, no BYT-CR detection\n");
	}
	return status;
}


static int sst_acpi_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int ret = 0;
	struct intel_sst_drv *ctx;
	const struct acpi_device_id *id;
	struct sst_acpi_mach *mach;
	struct platform_device *mdev;
	struct platform_device *plat_dev;
	struct sst_platform_info *pdata;
	unsigned int dev_id;
	bool bytcr = false;

	id = acpi_match_device(dev->driver->acpi_match_table, dev);
	if (!id)
		return -ENODEV;
	dev_dbg(dev, "for %s\n", id->id);

	mach = (struct sst_acpi_mach *)id->driver_data;
	mach = sst_acpi_find_machine(mach);
	if (mach == NULL) {
		dev_err(dev, "No matching machine driver found\n");
		return -ENODEV;
	}

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

	ret = is_byt_cr(dev, &bytcr);
	if (!((ret < 0) || (bytcr == false))) {
		dev_info(dev, "Detected Baytrail-CR platform\n");

		/* override resource info */
		byt_rvp_platform_data.res_info = &bytcr_res_info;
	}

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
* intel_sst_remove - remove function
*
* @pdev:	platform device structure
*
* This function is called by OS when a device is unloaded
* This frees the interrupt etc
*/
static int sst_acpi_remove(struct platform_device *pdev)
{
	struct intel_sst_drv *ctx;

	ctx = platform_get_drvdata(pdev);
	sst_context_cleanup(ctx);
	platform_set_drvdata(pdev, NULL);
	return 0;
}

static unsigned long cht_machine_id;

#define CHT_SURFACE_MACH 1
#define BYT_THINKPAD_10  2

static int cht_surface_quirk_cb(const struct dmi_system_id *id)
{
	cht_machine_id = CHT_SURFACE_MACH;
	return 1;
}

static int byt_thinkpad10_quirk_cb(const struct dmi_system_id *id)
{
	cht_machine_id = BYT_THINKPAD_10;
	return 1;
}


static const struct dmi_system_id byt_table[] = {
	{
		.callback = byt_thinkpad10_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_VERSION, "ThinkPad 10"),
		},
	},
	{
		.callback = byt_thinkpad10_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_VERSION, "ThinkPad Tablet B"),
		},
	},
	{
		.callback = byt_thinkpad10_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_VERSION, "Lenovo Miix 2 10"),
		},
	},
	{ }
};

static const struct dmi_system_id cht_table[] = {
	{
		.callback = cht_surface_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Microsoft Corporation"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Surface 3"),
		},
	},
	{ }
};


static struct sst_acpi_mach cht_surface_mach = {
	.id = "10EC5640",
	.drv_name = "cht-bsw-rt5645",
	.fw_filename = "intel/fw_sst_22a8.bin",
	.board = "cht-bsw",
	.pdata = &chv_platform_data,
};

static struct sst_acpi_mach byt_thinkpad_10 = {
	.id = "10EC5640",
	.drv_name = "cht-bsw-rt5672",
	.fw_filename = "intel/fw_sst_0f28.bin",
	.board = "cht-bsw",
	.pdata = &byt_rvp_platform_data,
};

static struct sst_acpi_mach *cht_quirk(void *arg)
{
	struct sst_acpi_mach *mach = arg;

	dmi_check_system(cht_table);

	if (cht_machine_id == CHT_SURFACE_MACH)
		return &cht_surface_mach;
	else
		return mach;
}

static struct sst_acpi_mach *byt_quirk(void *arg)
{
	struct sst_acpi_mach *mach = arg;

	dmi_check_system(byt_table);

	if (cht_machine_id == BYT_THINKPAD_10)
		return &byt_thinkpad_10;
	else
		return mach;
}


static struct sst_acpi_mach sst_acpi_bytcr[] = {
	{
		.id = "10EC5640",
		.drv_name = "bytcr_rt5640",
		.fw_filename = "intel/fw_sst_0f28.bin",
		.board = "bytcr_rt5640",
		.machine_quirk = byt_quirk,
		.pdata = &byt_rvp_platform_data,
	},
	{
		.id = "10EC5642",
		.drv_name = "bytcr_rt5640",
		.fw_filename = "intel/fw_sst_0f28.bin",
		.board = "bytcr_rt5640",
		.pdata = &byt_rvp_platform_data
	},
	{
		.id = "INTCCFFD",
		.drv_name = "bytcr_rt5640",
		.fw_filename = "intel/fw_sst_0f28.bin",
		.board = "bytcr_rt5640",
		.pdata = &byt_rvp_platform_data
	},
	{
		.id = "10EC5651",
		.drv_name = "bytcr_rt5651",
		.fw_filename = "intel/fw_sst_0f28.bin",
		.board = "bytcr_rt5651",
		.pdata = &byt_rvp_platform_data
	},
	{
		.id = "DLGS7212",
		.drv_name = "bytcht_da7213",
		.fw_filename = "intel/fw_sst_0f28.bin",
		.board = "bytcht_da7213",
		.pdata = &byt_rvp_platform_data
	},
	{
		.id = "DLGS7213",
		.drv_name = "bytcht_da7213",
		.fw_filename = "intel/fw_sst_0f28.bin",
		.board = "bytcht_da7213",
		.pdata = &byt_rvp_platform_data
	},
	/* some Baytrail platforms rely on RT5645, use CHT machine driver */
	{
		.id = "10EC5645",
		.drv_name = "cht-bsw-rt5645",
		.fw_filename = "intel/fw_sst_0f28.bin",
		.board = "cht-bsw",
		.pdata = &byt_rvp_platform_data
	},
	{
		.id = "10EC5648",
		.drv_name = "cht-bsw-rt5645",
		.fw_filename = "intel/fw_sst_0f28.bin",
		.board = "cht-bsw",
		.pdata = &byt_rvp_platform_data
	},
#if IS_ENABLED(CONFIG_SND_SOC_INTEL_BYT_CHT_NOCODEC_MACH)
	/*
	 * This is always last in the table so that it is selected only when
	 * enabled explicitly and there is no codec-related information in SSDT
	 */
	{
		.id = "80860F28",
		.drv_name = "bytcht_nocodec",
		.fw_filename = "intel/fw_sst_0f28.bin",
		.board = "bytcht_nocodec",
		.pdata = &byt_rvp_platform_data
	},
#endif
	{},
};

/* Cherryview-based platforms: CherryTrail and Braswell */
static struct sst_acpi_mach sst_acpi_chv[] = {
	{
		.id = "10EC5670",
		.drv_name = "cht-bsw-rt5672",
		.fw_filename = "intel/fw_sst_22a8.bin",
		.board = "cht-bsw",
		.pdata = &chv_platform_data
	},
	{
		.id = "10EC5672",
		.drv_name = "cht-bsw-rt5672",
		.fw_filename = "intel/fw_sst_22a8.bin",
		.board = "cht-bsw",
		.pdata = &chv_platform_data
	},
	{
		.id = "10EC5645",
		.drv_name = "cht-bsw-rt5645",
		.fw_filename = "intel/fw_sst_22a8.bin",
		.board = "cht-bsw",
		.pdata = &chv_platform_data
	},
	{
		.id = "10EC5650",
		.drv_name = "cht-bsw-rt5645",
		.fw_filename = "intel/fw_sst_22a8.bin",
		.board = "cht-bsw",
		.pdata = &chv_platform_data
	},
	{
		.id = "10EC3270",
		.drv_name = "cht-bsw-rt5645",
		.fw_filename = "intel/fw_sst_22a8.bin",
		.board = "cht-bsw",
		.pdata = &chv_platform_data
	},

	{
		.id = "193C9890",
		.drv_name = "cht-bsw-max98090",
		.fw_filename = "intel/fw_sst_22a8.bin",
		.board = "cht-bsw",
		.pdata = &chv_platform_data
	},
	{
		.id = "DLGS7212",
		.drv_name = "bytcht_da7213",
		.fw_filename = "intel/fw_sst_22a8.bin",
		.board = "bytcht_da7213",
		.pdata = &chv_platform_data
	},
	{
		.id = "DLGS7213",
		.drv_name = "bytcht_da7213",
		.fw_filename = "intel/fw_sst_22a8.bin",
		.board = "bytcht_da7213",
		.pdata = &chv_platform_data
	},
	{
		.id = "ESSX8316",
		.drv_name = "bytcht_es8316",
		.fw_filename = "intel/fw_sst_22a8.bin",
		.board = "bytcht_es8316",
		.pdata = &chv_platform_data
	},
	/* some CHT-T platforms rely on RT5640, use Baytrail machine driver */
	{
		.id = "10EC5640",
		.drv_name = "bytcr_rt5640",
		.fw_filename = "intel/fw_sst_22a8.bin",
		.board = "bytcr_rt5640",
		.machine_quirk = cht_quirk,
		.pdata = &chv_platform_data
	},
	{
		.id = "10EC3276",
		.drv_name = "bytcr_rt5640",
		.fw_filename = "intel/fw_sst_22a8.bin",
		.board = "bytcr_rt5640",
		.pdata = &chv_platform_data
	},
	/* some CHT-T platforms rely on RT5651, use Baytrail machine driver */
	{
		.id = "10EC5651",
		.drv_name = "bytcr_rt5651",
		.fw_filename = "intel/fw_sst_22a8.bin",
		.board = "bytcr_rt5651",
		.pdata = &chv_platform_data
	},
#if IS_ENABLED(CONFIG_SND_SOC_INTEL_BYT_CHT_NOCODEC_MACH)
	/*
	 * This is always last in the table so that it is selected only when
	 * enabled explicitly and there is no codec-related information in SSDT
	 */
	{
		.id = "808622A8",
		.drv_name = "bytcht_nocodec",
		.fw_filename = "intel/fw_sst_22a8.bin",
		.board = "bytcht_nocodec",
		.pdata = &chv_platform_data
	},
#endif
	{},
};

static const struct acpi_device_id sst_acpi_ids[] = {
	{ "80860F28", (unsigned long)&sst_acpi_bytcr},
	{ "808622A8", (unsigned long) &sst_acpi_chv},
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
	.remove	= sst_acpi_remove,
};

module_platform_driver(sst_acpi_driver);

MODULE_DESCRIPTION("Intel (R) SST(R) Audio Engine ACPI Driver");
MODULE_AUTHOR("Ramesh Babu K V");
MODULE_AUTHOR("Omair Mohammed Abdullah");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("sst");
