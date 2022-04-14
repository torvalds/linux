// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2018 Intel Corporation. All rights reserved.
//
// Author: Liam Girdwood <liam.r.girdwood@linux.intel.com>
//

#include <linux/acpi.h>
#include <linux/firmware.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <sound/soc-acpi.h>
#include <sound/soc-acpi-intel-match.h>
#include <sound/sof.h>
#include "../intel/common/soc-intel-quirks.h"
#include "ops.h"
#include "sof-acpi-dev.h"

/* platform specific devices */
#include "intel/shim.h"

static char *fw_path;
module_param(fw_path, charp, 0444);
MODULE_PARM_DESC(fw_path, "alternate path for SOF firmware.");

static char *tplg_path;
module_param(tplg_path, charp, 0444);
MODULE_PARM_DESC(tplg_path, "alternate path for SOF topology.");

static int sof_acpi_debug;
module_param_named(sof_acpi_debug, sof_acpi_debug, int, 0444);
MODULE_PARM_DESC(sof_acpi_debug, "SOF ACPI debug options (0x0 all off)");

#define SOF_ACPI_DISABLE_PM_RUNTIME BIT(0)

const struct dev_pm_ops sof_acpi_pm = {
	SET_SYSTEM_SLEEP_PM_OPS(snd_sof_suspend, snd_sof_resume)
	SET_RUNTIME_PM_OPS(snd_sof_runtime_suspend, snd_sof_runtime_resume,
			   snd_sof_runtime_idle)
};
EXPORT_SYMBOL_NS(sof_acpi_pm, SND_SOC_SOF_ACPI_DEV);

static void sof_acpi_probe_complete(struct device *dev)
{
	dev_dbg(dev, "Completing SOF ACPI probe");

	if (sof_acpi_debug & SOF_ACPI_DISABLE_PM_RUNTIME)
		return;

	/* allow runtime_pm */
	pm_runtime_set_autosuspend_delay(dev, SND_SOF_SUSPEND_DELAY_MS);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_enable(dev);
}

int sof_acpi_probe(struct platform_device *pdev, const struct sof_dev_desc *desc)
{
	struct device *dev = &pdev->dev;
	struct snd_sof_pdata *sof_pdata;

	dev_dbg(dev, "ACPI DSP detected");

	sof_pdata = devm_kzalloc(dev, sizeof(*sof_pdata), GFP_KERNEL);
	if (!sof_pdata)
		return -ENOMEM;

	if (!desc->ops) {
		dev_err(dev, "error: no matching ACPI descriptor ops\n");
		return -ENODEV;
	}

	sof_pdata->desc = desc;
	sof_pdata->dev = &pdev->dev;
	sof_pdata->fw_filename = desc->default_fw_filename;

	/* alternate fw and tplg filenames ? */
	if (fw_path)
		sof_pdata->fw_filename_prefix = fw_path;
	else
		sof_pdata->fw_filename_prefix =
			sof_pdata->desc->default_fw_path[SOF_IPC];

	if (tplg_path)
		sof_pdata->tplg_filename_prefix = tplg_path;
	else
		sof_pdata->tplg_filename_prefix =
			sof_pdata->desc->default_tplg_path[SOF_IPC];

	/* set callback to be called on successful device probe to enable runtime_pm */
	sof_pdata->sof_probe_complete = sof_acpi_probe_complete;

	/* call sof helper for DSP hardware probe */
	return snd_sof_device_probe(dev, sof_pdata);
}
EXPORT_SYMBOL_NS(sof_acpi_probe, SND_SOC_SOF_ACPI_DEV);

int sof_acpi_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	if (!(sof_acpi_debug & SOF_ACPI_DISABLE_PM_RUNTIME))
		pm_runtime_disable(dev);

	/* call sof helper for DSP hardware remove */
	snd_sof_device_remove(dev);

	return 0;
}
EXPORT_SYMBOL_NS(sof_acpi_remove, SND_SOC_SOF_ACPI_DEV);

MODULE_LICENSE("Dual BSD/GPL");
