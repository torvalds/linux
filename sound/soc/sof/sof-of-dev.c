// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// Copyright 2019 NXP
//
// Author: Daniel Baluta <daniel.baluta@nxp.com>
//

#include <linux/firmware.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/pm_runtime.h>
#include <sound/sof.h>

#include "sof-of-dev.h"
#include "ops.h"

static char *fw_path;
module_param(fw_path, charp, 0444);
MODULE_PARM_DESC(fw_path, "alternate path for SOF firmware.");

static char *tplg_path;
module_param(tplg_path, charp, 0444);
MODULE_PARM_DESC(tplg_path, "alternate path for SOF topology.");

const struct dev_pm_ops sof_of_pm = {
	.prepare = snd_sof_prepare,
	.complete = snd_sof_complete,
	SET_SYSTEM_SLEEP_PM_OPS(snd_sof_suspend, snd_sof_resume)
	SET_RUNTIME_PM_OPS(snd_sof_runtime_suspend, snd_sof_runtime_resume,
			   NULL)
};
EXPORT_SYMBOL(sof_of_pm);

static void sof_of_probe_complete(struct device *dev)
{
	/* allow runtime_pm */
	pm_runtime_set_autosuspend_delay(dev, SND_SOF_SUSPEND_DELAY_MS);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_mark_last_busy(dev);
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
}

int sof_of_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct sof_dev_desc *desc;
	struct snd_sof_pdata *sof_pdata;

	dev_info(&pdev->dev, "DT DSP detected");

	sof_pdata = devm_kzalloc(dev, sizeof(*sof_pdata), GFP_KERNEL);
	if (!sof_pdata)
		return -ENOMEM;

	desc = device_get_match_data(dev);
	if (!desc)
		return -ENODEV;

	if (!desc->ops) {
		dev_err(dev, "error: no matching DT descriptor ops\n");
		return -ENODEV;
	}

	sof_pdata->desc = desc;
	sof_pdata->dev = &pdev->dev;
	sof_pdata->fw_filename = desc->default_fw_filename[SOF_IPC];

	if (fw_path)
		sof_pdata->fw_filename_prefix = fw_path;
	else
		sof_pdata->fw_filename_prefix = sof_pdata->desc->default_fw_path[SOF_IPC];

	if (tplg_path)
		sof_pdata->tplg_filename_prefix = tplg_path;
	else
		sof_pdata->tplg_filename_prefix = sof_pdata->desc->default_tplg_path[SOF_IPC];

	/* set callback to be called on successful device probe to enable runtime_pm */
	sof_pdata->sof_probe_complete = sof_of_probe_complete;

	/* call sof helper for DSP hardware probe */
	return snd_sof_device_probe(dev, sof_pdata);
}
EXPORT_SYMBOL(sof_of_probe);

int sof_of_remove(struct platform_device *pdev)
{
	pm_runtime_disable(&pdev->dev);

	/* call sof helper for DSP hardware remove */
	snd_sof_device_remove(&pdev->dev);

	return 0;
}
EXPORT_SYMBOL(sof_of_remove);

void sof_of_shutdown(struct platform_device *pdev)
{
	snd_sof_device_shutdown(&pdev->dev);
}
EXPORT_SYMBOL(sof_of_shutdown);

MODULE_LICENSE("Dual BSD/GPL");
