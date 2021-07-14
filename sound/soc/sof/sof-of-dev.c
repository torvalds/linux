// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// Copyright 2019 NXP
//
// Author: Daniel Baluta <daniel.baluta@nxp.com>
//

#include <linux/firmware.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <sound/sof.h>

#include "ops.h"

extern struct snd_sof_dsp_ops sof_imx8_ops;
extern struct snd_sof_dsp_ops sof_imx8x_ops;
extern struct snd_sof_dsp_ops sof_imx8m_ops;

/* platform specific devices */
#if IS_ENABLED(CONFIG_SND_SOC_SOF_IMX8)
static struct sof_dev_desc sof_of_imx8qxp_desc = {
	.default_fw_path = "imx/sof",
	.default_tplg_path = "imx/sof-tplg",
	.default_fw_filename = "sof-imx8x.ri",
	.nocodec_tplg_filename = "sof-imx8-nocodec.tplg",
	.ops = &sof_imx8x_ops,
};

static struct sof_dev_desc sof_of_imx8qm_desc = {
	.default_fw_path = "imx/sof",
	.default_tplg_path = "imx/sof-tplg",
	.default_fw_filename = "sof-imx8.ri",
	.nocodec_tplg_filename = "sof-imx8-nocodec.tplg",
	.ops = &sof_imx8_ops,
};
#endif

#if IS_ENABLED(CONFIG_SND_SOC_SOF_IMX8M)
static struct sof_dev_desc sof_of_imx8mp_desc = {
	.default_fw_path = "imx/sof",
	.default_tplg_path = "imx/sof-tplg",
	.default_fw_filename = "sof-imx8m.ri",
	.nocodec_tplg_filename = "sof-imx8-nocodec.tplg",
	.ops = &sof_imx8m_ops,
};
#endif

static const struct dev_pm_ops sof_of_pm = {
	.prepare = snd_sof_prepare,
	.complete = snd_sof_complete,
	SET_SYSTEM_SLEEP_PM_OPS(snd_sof_suspend, snd_sof_resume)
	SET_RUNTIME_PM_OPS(snd_sof_runtime_suspend, snd_sof_runtime_resume,
			   NULL)
};

static void sof_of_probe_complete(struct device *dev)
{
	/* allow runtime_pm */
	pm_runtime_set_autosuspend_delay(dev, SND_SOF_SUSPEND_DELAY_MS);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);

	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);
}

static int sof_of_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct sof_dev_desc *desc;
	struct snd_sof_pdata *sof_pdata;
	const struct snd_sof_dsp_ops *ops;

	dev_info(&pdev->dev, "DT DSP detected");

	sof_pdata = devm_kzalloc(dev, sizeof(*sof_pdata), GFP_KERNEL);
	if (!sof_pdata)
		return -ENOMEM;

	desc = device_get_match_data(dev);
	if (!desc)
		return -ENODEV;

	/* get ops for platform */
	ops = desc->ops;
	if (!ops) {
		dev_err(dev, "error: no matching DT descriptor ops\n");
		return -ENODEV;
	}

	sof_pdata->desc = desc;
	sof_pdata->dev = &pdev->dev;
	sof_pdata->fw_filename = desc->default_fw_filename;

	/* TODO: read alternate fw and tplg filenames from DT */
	sof_pdata->fw_filename_prefix = sof_pdata->desc->default_fw_path;
	sof_pdata->tplg_filename_prefix = sof_pdata->desc->default_tplg_path;

	/* set callback to be called on successful device probe to enable runtime_pm */
	sof_pdata->sof_probe_complete = sof_of_probe_complete;

	/* call sof helper for DSP hardware probe */
	return snd_sof_device_probe(dev, sof_pdata);
}

static int sof_of_remove(struct platform_device *pdev)
{
	pm_runtime_disable(&pdev->dev);

	/* call sof helper for DSP hardware remove */
	snd_sof_device_remove(&pdev->dev);

	return 0;
}

static const struct of_device_id sof_of_ids[] = {
#if IS_ENABLED(CONFIG_SND_SOC_SOF_IMX8)
	{ .compatible = "fsl,imx8qxp-dsp", .data = &sof_of_imx8qxp_desc},
	{ .compatible = "fsl,imx8qm-dsp", .data = &sof_of_imx8qm_desc},
#endif
#if IS_ENABLED(CONFIG_SND_SOC_SOF_IMX8M)
	{ .compatible = "fsl,imx8mp-dsp", .data = &sof_of_imx8mp_desc},
#endif
	{ }
};
MODULE_DEVICE_TABLE(of, sof_of_ids);

/* DT driver definition */
static struct platform_driver snd_sof_of_driver = {
	.probe = sof_of_probe,
	.remove = sof_of_remove,
	.driver = {
		.name = "sof-audio-of",
		.pm = &sof_of_pm,
		.of_match_table = sof_of_ids,
	},
};
module_platform_driver(snd_sof_of_driver);

MODULE_LICENSE("Dual BSD/GPL");
