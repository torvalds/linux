// SPDX-License-Identifier: GPL-2.0-only
/*
 *  sst_pci.c - SST (LPE) driver init file for pci enumeration.
 *
 *  Copyright (C) 2008-14	Intel Corp
 *  Authors:	Vinod Koul <vinod.koul@intel.com>
 *		Harsha Priya <priya.harsha@intel.com>
 *		Dharageswari R <dharageswari.r@intel.com>
 *		KP Jeeja <jeeja.kp@intel.com>
 *  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/fs.h>
#include <linux/firmware.h>
#include <linux/pm_runtime.h>
#include <sound/core.h>
#include <sound/soc.h>
#include <asm/platform_sst_audio.h>
#include "../sst-mfld-platform.h"
#include "sst.h"

static int sst_platform_get_resources(struct intel_sst_drv *ctx)
{
	int ddr_base, ret = 0;
	struct pci_dev *pci = ctx->pci;

	ret = pci_request_regions(pci, SST_DRV_NAME);
	if (ret)
		return ret;

	/* map registers */
	/* DDR base */
	if (ctx->dev_id == SST_MRFLD_PCI_ID) {
		ctx->ddr_base = pci_resource_start(pci, 0);
		/* check that the relocated IMR base matches with FW Binary */
		ddr_base = relocate_imr_addr_mrfld(ctx->ddr_base);
		if (!ctx->pdata->lib_info) {
			dev_err(ctx->dev, "lib_info pointer NULL\n");
			ret = -EINVAL;
			goto do_release_regions;
		}
		if (ddr_base != ctx->pdata->lib_info->mod_base) {
			dev_err(ctx->dev,
					"FW LSP DDR BASE does not match with IFWI\n");
			ret = -EINVAL;
			goto do_release_regions;
		}
		ctx->ddr_end = pci_resource_end(pci, 0);

		ctx->ddr = pcim_iomap(pci, 0,
					pci_resource_len(pci, 0));
		if (!ctx->ddr) {
			ret = -EINVAL;
			goto do_release_regions;
		}
		dev_dbg(ctx->dev, "sst: DDR Ptr %p\n", ctx->ddr);
	} else {
		ctx->ddr = NULL;
	}
	/* SHIM */
	ctx->shim_phy_add = pci_resource_start(pci, 1);
	ctx->shim = pcim_iomap(pci, 1, pci_resource_len(pci, 1));
	if (!ctx->shim) {
		ret = -EINVAL;
		goto do_release_regions;
	}
	dev_dbg(ctx->dev, "SST Shim Ptr %p\n", ctx->shim);

	/* Shared SRAM */
	ctx->mailbox_add = pci_resource_start(pci, 2);
	ctx->mailbox = pcim_iomap(pci, 2, pci_resource_len(pci, 2));
	if (!ctx->mailbox) {
		ret = -EINVAL;
		goto do_release_regions;
	}
	dev_dbg(ctx->dev, "SRAM Ptr %p\n", ctx->mailbox);

	/* IRAM */
	ctx->iram_end = pci_resource_end(pci, 3);
	ctx->iram_base = pci_resource_start(pci, 3);
	ctx->iram = pcim_iomap(pci, 3, pci_resource_len(pci, 3));
	if (!ctx->iram) {
		ret = -EINVAL;
		goto do_release_regions;
	}
	dev_dbg(ctx->dev, "IRAM Ptr %p\n", ctx->iram);

	/* DRAM */
	ctx->dram_end = pci_resource_end(pci, 4);
	ctx->dram_base = pci_resource_start(pci, 4);
	ctx->dram = pcim_iomap(pci, 4, pci_resource_len(pci, 4));
	if (!ctx->dram) {
		ret = -EINVAL;
		goto do_release_regions;
	}
	dev_dbg(ctx->dev, "DRAM Ptr %p\n", ctx->dram);
do_release_regions:
	pci_release_regions(pci);
	return 0;
}

/*
 * intel_sst_probe - PCI probe function
 *
 * @pci:	PCI device structure
 * @pci_id: PCI device ID structure
 *
 */
static int intel_sst_probe(struct pci_dev *pci,
			const struct pci_device_id *pci_id)
{
	int ret = 0;
	struct intel_sst_drv *sst_drv_ctx;
	struct sst_platform_info *sst_pdata = pci->dev.platform_data;

	dev_dbg(&pci->dev, "Probe for DID %x\n", pci->device);
	ret = sst_alloc_drv_context(&sst_drv_ctx, &pci->dev, pci->device);
	if (ret < 0)
		return ret;

	sst_drv_ctx->pdata = sst_pdata;
	sst_drv_ctx->irq_num = pci->irq;
	snprintf(sst_drv_ctx->firmware_name, sizeof(sst_drv_ctx->firmware_name),
			"%s%04x%s", "fw_sst_",
			sst_drv_ctx->dev_id, ".bin");

	ret = sst_context_init(sst_drv_ctx);
	if (ret < 0)
		return ret;

	/* Init the device */
	ret = pcim_enable_device(pci);
	if (ret) {
		dev_err(sst_drv_ctx->dev,
			"device can't be enabled. Returned err: %d\n", ret);
		goto do_free_drv_ctx;
	}
	sst_drv_ctx->pci = pci_dev_get(pci);
	ret = sst_platform_get_resources(sst_drv_ctx);
	if (ret < 0)
		goto do_free_drv_ctx;

	pci_set_drvdata(pci, sst_drv_ctx);
	sst_configure_runtime_pm(sst_drv_ctx);

	return ret;

do_free_drv_ctx:
	sst_context_cleanup(sst_drv_ctx);
	dev_err(sst_drv_ctx->dev, "Probe failed with %d\n", ret);
	return ret;
}

/**
 * intel_sst_remove - PCI remove function
 *
 * @pci:	PCI device structure
 *
 * This function is called by OS when a device is unloaded
 * This frees the interrupt etc
 */
static void intel_sst_remove(struct pci_dev *pci)
{
	struct intel_sst_drv *sst_drv_ctx = pci_get_drvdata(pci);

	sst_context_cleanup(sst_drv_ctx);
	pci_dev_put(sst_drv_ctx->pci);
	pci_release_regions(pci);
	pci_set_drvdata(pci, NULL);
}

/* PCI Routines */
static const struct pci_device_id intel_sst_ids[] = {
	{ PCI_VDEVICE(INTEL, SST_MRFLD_PCI_ID), 0},
	{ 0, }
};

static struct pci_driver sst_driver = {
	.name = SST_DRV_NAME,
	.id_table = intel_sst_ids,
	.probe = intel_sst_probe,
	.remove = intel_sst_remove,
#ifdef CONFIG_PM
	.driver = {
		.pm = &intel_sst_pm,
	},
#endif
};

module_pci_driver(sst_driver);

MODULE_DESCRIPTION("Intel (R) SST(R) Audio Engine PCI Driver");
MODULE_AUTHOR("Vinod Koul <vinod.koul@intel.com>");
MODULE_AUTHOR("Harsha Priya <priya.harsha@intel.com>");
MODULE_AUTHOR("Dharageswari R <dharageswari.r@intel.com>");
MODULE_AUTHOR("KP Jeeja <jeeja.kp@intel.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("sst");
