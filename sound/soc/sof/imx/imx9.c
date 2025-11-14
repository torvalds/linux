// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
/*
 * Copyright 2025 NXP
 */

#include <linux/firmware/imx/sm.h>

#include "imx-common.h"

#define IMX95_M7_CPU_ID 0x1
#define IMX95_M7_LM_ID 0x1

static struct snd_soc_dai_driver imx95_dai[] = {
	IMX_SOF_DAI_DRV_ENTRY_BIDIR("sai3", 1, 32),
};

static struct snd_sof_dsp_ops sof_imx9_ops;

static int imx95_ops_init(struct snd_sof_dev *sdev)
{
	/* first copy from template */
	memcpy(&sof_imx9_ops, &sof_imx_ops, sizeof(sof_imx_ops));

	/* ... and finally set DAI driver */
	sof_imx9_ops.drv = get_chip_info(sdev)->drv;
	sof_imx9_ops.num_drv = get_chip_info(sdev)->num_drv;

	return 0;
}

static int imx95_chip_probe(struct snd_sof_dev *sdev)
{
	struct platform_device *pdev;
	struct resource *res;

	pdev = to_platform_device(sdev->dev);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "sram");
	if (!res)
		return dev_err_probe(sdev->dev, -ENODEV,
				     "failed to fetch SRAM region\n");

	return scmi_imx_lmm_reset_vector_set(IMX95_M7_LM_ID, IMX95_M7_CPU_ID,
					     0, res->start);
}

static int imx95_core_kick(struct snd_sof_dev *sdev)
{
	return scmi_imx_lmm_operation(IMX95_M7_LM_ID, SCMI_IMX_LMM_BOOT, 0);
}

static int imx95_core_shutdown(struct snd_sof_dev *sdev)
{
	return scmi_imx_lmm_operation(IMX95_M7_LM_ID,
				      SCMI_IMX_LMM_SHUTDOWN,
				      SCMI_IMX_LMM_OP_FORCEFUL);
}

static const struct imx_chip_ops imx95_chip_ops = {
	.probe = imx95_chip_probe,
	.core_kick = imx95_core_kick,
	.core_shutdown = imx95_core_shutdown,
};

static struct imx_memory_info imx95_memory_regions[] = {
	{ .name = "sram", .reserved = false },
	{ }
};

static const struct imx_chip_info imx95_chip_info = {
	.ipc_info = {
		.boot_mbox_offset = 0x6001000,
		.window_offset = 0x6000000,
	},
	.has_dma_reserved = true,
	.memory = imx95_memory_regions,
	.drv = imx95_dai,
	.num_drv = ARRAY_SIZE(imx95_dai),
	.ops = &imx95_chip_ops,
};

static struct snd_sof_of_mach sof_imx9_machs[] = {
	{
		.compatible = "fsl,imx95-19x19-evk",
		.sof_tplg_filename = "sof-imx95-wm8962.tplg",
		.drv_name = "asoc-audio-graph-card2",
	},
	{
	}
};

IMX_SOF_DEV_DESC(imx95, sof_imx9_machs, &imx95_chip_info, &sof_imx9_ops, imx95_ops_init);

static const struct of_device_id sof_of_imx9_ids[] = {
	{
		.compatible = "fsl,imx95-cm7-sof",
		.data = &IMX_SOF_DEV_DESC_NAME(imx95),
	},
	{
	},
};
MODULE_DEVICE_TABLE(of, sof_of_imx9_ids);

static struct platform_driver snd_sof_of_imx9_driver = {
	.probe = sof_of_probe,
	.remove = sof_of_remove,
	.driver = {
		.name = "sof-audio-of-imx9",
		.pm = pm_ptr(&sof_of_pm),
		.of_match_table = sof_of_imx9_ids,
	},
};
module_platform_driver(snd_sof_of_imx9_driver);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("SOF driver for imx9 platforms");
MODULE_AUTHOR("Laurentiu Mihalcea <laurentiu.mihalcea@nxp.com>");
