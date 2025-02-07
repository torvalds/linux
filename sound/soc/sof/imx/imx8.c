// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// Copyright 2019-2025 NXP
//
// Author: Daniel Baluta <daniel.baluta@nxp.com>
//
// Hardware interface for audio DSP on i.MX8

#include <linux/firmware.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/pm_domain.h>

#include <linux/module.h>
#include <sound/sof.h>
#include <sound/sof/xtensa.h>
#include <linux/firmware/imx/ipc.h>
#include <linux/firmware/imx/dsp.h>

#include <linux/firmware/imx/svc/misc.h>
#include <dt-bindings/firmware/imx/rsrc.h>
#include "../ops.h"
#include "../sof-of-dev.h"
#include "imx-common.h"

/* DSP memories */
#define IRAM_OFFSET		0x10000
#define IRAM_SIZE		(2 * 1024)
#define DRAM0_OFFSET		0x0
#define DRAM0_SIZE		(32 * 1024)
#define DRAM1_OFFSET		0x8000
#define DRAM1_SIZE		(32 * 1024)
#define SYSRAM_OFFSET		0x18000
#define SYSRAM_SIZE		(256 * 1024)
#define SYSROM_OFFSET		0x58000
#define SYSROM_SIZE		(192 * 1024)

#define RESET_VECTOR_VADDR	0x596f8000

#define MBOX_OFFSET	0x800000
#define MBOX_SIZE	0x1000

/*
 * DSP control.
 */
static int imx8x_run(struct snd_sof_dev *sdev)
{
	int ret;

	ret = imx_sc_misc_set_control(get_chip_pdata(sdev), IMX_SC_R_DSP,
				      IMX_SC_C_OFS_SEL, 1);
	if (ret < 0) {
		dev_err(sdev->dev, "Error system address offset source select\n");
		return ret;
	}

	ret = imx_sc_misc_set_control(get_chip_pdata(sdev), IMX_SC_R_DSP,
				      IMX_SC_C_OFS_AUDIO, 0x80);
	if (ret < 0) {
		dev_err(sdev->dev, "Error system address offset of AUDIO\n");
		return ret;
	}

	ret = imx_sc_misc_set_control(get_chip_pdata(sdev), IMX_SC_R_DSP,
				      IMX_SC_C_OFS_PERIPH, 0x5A);
	if (ret < 0) {
		dev_err(sdev->dev, "Error system address offset of PERIPH %d\n",
			ret);
		return ret;
	}

	ret = imx_sc_misc_set_control(get_chip_pdata(sdev), IMX_SC_R_DSP,
				      IMX_SC_C_OFS_IRQ, 0x51);
	if (ret < 0) {
		dev_err(sdev->dev, "Error system address offset of IRQ\n");
		return ret;
	}

	imx_sc_pm_cpu_start(get_chip_pdata(sdev), IMX_SC_R_DSP, true,
			    RESET_VECTOR_VADDR);

	return 0;
}

static int imx8_run(struct snd_sof_dev *sdev)
{
	int ret;

	ret = imx_sc_misc_set_control(get_chip_pdata(sdev), IMX_SC_R_DSP,
				      IMX_SC_C_OFS_SEL, 0);
	if (ret < 0) {
		dev_err(sdev->dev, "Error system address offset source select\n");
		return ret;
	}

	imx_sc_pm_cpu_start(get_chip_pdata(sdev), IMX_SC_R_DSP, true,
			    RESET_VECTOR_VADDR);

	return 0;
}

static int imx8_probe(struct snd_sof_dev *sdev)
{
	struct imx_sc_ipc *sc_ipc_handle;
	struct imx_common_data *common;
	int ret;

	common = sdev->pdata->hw_pdata;

	ret = imx_scu_get_handle(&sc_ipc_handle);
	if (ret < 0)
		return dev_err_probe(sdev->dev, ret,
				     "failed to fetch SC IPC handle\n");

	common->chip_pdata = sc_ipc_handle;

	return 0;
}

static struct snd_soc_dai_driver imx8_dai[] = {
	IMX_SOF_DAI_DRV_ENTRY_BIDIR("esai0", 1, 8),
	IMX_SOF_DAI_DRV_ENTRY_BIDIR("sai1", 1, 32),
};

static struct snd_sof_dsp_ops sof_imx8_ops;

static int imx8_ops_init(struct snd_sof_dev *sdev)
{
	/* first copy from template */
	memcpy(&sof_imx8_ops, &sof_imx_ops, sizeof(sof_imx_ops));

	/* then set common imx8 ops */
	sof_imx8_ops.dbg_dump = imx8_dump;
	sof_imx8_ops.dsp_arch_ops = &sof_xtensa_arch_ops;
	sof_imx8_ops.debugfs_add_region_item =
		snd_sof_debugfs_add_region_item_iomem;

	/* ... and finally set DAI driver */
	sof_imx8_ops.drv = get_chip_info(sdev)->drv;
	sof_imx8_ops.num_drv = get_chip_info(sdev)->num_drv;

	return 0;
}

static const struct imx_chip_ops imx8_chip_ops = {
	.probe = imx8_probe,
	.core_kick = imx8_run,
};

static const struct imx_chip_ops imx8x_chip_ops = {
	.probe = imx8_probe,
	.core_kick = imx8x_run,
};

static struct imx_memory_info imx8_memory_regions[] = {
	{ .name = "iram", .reserved = false },
	{ .name = "sram", .reserved = true },
	{ }
};

static const struct imx_chip_info imx8_chip_info = {
	.ipc_info = {
		.has_panic_code = true,
		.boot_mbox_offset = 0x800000,
		.window_offset = 0x800000,
	},
	.memory = imx8_memory_regions,
	.drv = imx8_dai,
	.num_drv = ARRAY_SIZE(imx8_dai),
	.ops = &imx8_chip_ops,
};

static const struct imx_chip_info imx8x_chip_info = {
	.ipc_info = {
		.has_panic_code = true,
		.boot_mbox_offset = 0x800000,
		.window_offset = 0x800000,
	},
	.memory = imx8_memory_regions,
	.drv = imx8_dai,
	.num_drv = ARRAY_SIZE(imx8_dai),
	.ops = &imx8x_chip_ops,
};

static struct snd_sof_of_mach sof_imx8_machs[] = {
	{
		.compatible = "fsl,imx8qxp-mek",
		.sof_tplg_filename = "sof-imx8-wm8960.tplg",
		.drv_name = "asoc-audio-graph-card2",
	},
	{
		.compatible = "fsl,imx8qxp-mek-wcpu",
		.sof_tplg_filename = "sof-imx8-wm8962.tplg",
		.drv_name = "asoc-audio-graph-card2",
	},
	{
		.compatible = "fsl,imx8qm-mek",
		.sof_tplg_filename = "sof-imx8-wm8960.tplg",
		.drv_name = "asoc-audio-graph-card2",
	},
	{
		.compatible = "fsl,imx8qm-mek-revd",
		.sof_tplg_filename = "sof-imx8-wm8962.tplg",
		.drv_name = "asoc-audio-graph-card2",
	},
	{
		.compatible = "fsl,imx8qxp-mek-bb",
		.sof_tplg_filename = "sof-imx8-cs42888.tplg",
		.drv_name = "asoc-audio-graph-card2",
	},
	{
		.compatible = "fsl,imx8qm-mek-bb",
		.sof_tplg_filename = "sof-imx8-cs42888.tplg",
		.drv_name = "asoc-audio-graph-card2",
	},

	{}
};

IMX_SOF_DEV_DESC(imx8, sof_imx8_machs, &imx8_chip_info, &sof_imx8_ops, imx8_ops_init);
IMX_SOF_DEV_DESC(imx8x, sof_imx8_machs, &imx8x_chip_info, &sof_imx8_ops, imx8_ops_init);

static const struct of_device_id sof_of_imx8_ids[] = {
	{
		.compatible = "fsl,imx8qxp-dsp",
		.data = &IMX_SOF_DEV_DESC_NAME(imx8x),
	},
	{
		.compatible = "fsl,imx8qm-dsp",
		.data = &IMX_SOF_DEV_DESC_NAME(imx8),
	},
	{ }
};
MODULE_DEVICE_TABLE(of, sof_of_imx8_ids);

/* DT driver definition */
static struct platform_driver snd_sof_of_imx8_driver = {
	.probe = sof_of_probe,
	.remove = sof_of_remove,
	.driver = {
		.name = "sof-audio-of-imx8",
		.pm = &sof_of_pm,
		.of_match_table = sof_of_imx8_ids,
	},
};
module_platform_driver(snd_sof_of_imx8_driver);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("SOF support for IMX8 platforms");
MODULE_IMPORT_NS("SND_SOC_SOF_XTENSA");
