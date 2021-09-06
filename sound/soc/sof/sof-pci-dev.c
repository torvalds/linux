// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2018 Intel Corporation. All rights reserved.
//
// Author: Liam Girdwood <liam.r.girdwood@linux.intel.com>
//

#include <linux/firmware.h>
#include <linux/dmi.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/pm_runtime.h>
#include <sound/soc-acpi.h>
#include <sound/soc-acpi-intel-match.h>
#include <sound/sof.h>
#include "ops.h"
#include "sof-pci-dev.h"

static char *fw_path;
module_param(fw_path, charp, 0444);
MODULE_PARM_DESC(fw_path, "alternate path for SOF firmware.");

static char *tplg_path;
module_param(tplg_path, charp, 0444);
MODULE_PARM_DESC(tplg_path, "alternate path for SOF topology.");

static int sof_pci_debug;
module_param_named(sof_pci_debug, sof_pci_debug, int, 0444);
MODULE_PARM_DESC(sof_pci_debug, "SOF PCI debug options (0x0 all off)");

static const char *sof_override_tplg_name;

#define SOF_PCI_DISABLE_PM_RUNTIME BIT(0)

static int sof_tplg_cb(const struct dmi_system_id *id)
{
	sof_override_tplg_name = id->driver_data;
	return 1;
}

static const struct dmi_system_id sof_tplg_table[] = {
	{
		.callback = sof_tplg_cb,
		.matches = {
			DMI_MATCH(DMI_PRODUCT_FAMILY, "Google_Volteer"),
			DMI_MATCH(DMI_OEM_STRING, "AUDIO-MAX98373_ALC5682I_I2S_UP4"),
		},
		.driver_data = "sof-tgl-rt5682-ssp0-max98373-ssp2.tplg",
	},
	{}
};

static const struct dmi_system_id community_key_platforms[] = {
	{
		.ident = "Up Squared",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "AAEON"),
			DMI_MATCH(DMI_BOARD_NAME, "UP-APL01"),
		}
	},
	{
		.ident = "Up Extreme",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "AAEON"),
			DMI_MATCH(DMI_BOARD_NAME, "UP-WHL01"),
		}
	},
	{
		.ident = "Google Chromebooks",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Google"),
		}
	},
	{},
};

const struct dev_pm_ops sof_pci_pm = {
	.prepare = snd_sof_prepare,
	.complete = snd_sof_complete,
	SET_SYSTEM_SLEEP_PM_OPS(snd_sof_suspend, snd_sof_resume)
	SET_RUNTIME_PM_OPS(snd_sof_runtime_suspend, snd_sof_runtime_resume,
			   snd_sof_runtime_idle)
};
EXPORT_SYMBOL_NS(sof_pci_pm, SND_SOC_SOF_PCI_DEV);

static void sof_pci_probe_complete(struct device *dev)
{
	dev_dbg(dev, "Completing SOF PCI probe");

	if (sof_pci_debug & SOF_PCI_DISABLE_PM_RUNTIME)
		return;

	/* allow runtime_pm */
	pm_runtime_set_autosuspend_delay(dev, SND_SOF_SUSPEND_DELAY_MS);
	pm_runtime_use_autosuspend(dev);

	/*
	 * runtime pm for pci device is "forbidden" by default.
	 * so call pm_runtime_allow() to enable it.
	 */
	pm_runtime_allow(dev);

	/* mark last_busy for pm_runtime to make sure not suspend immediately */
	pm_runtime_mark_last_busy(dev);

	/* follow recommendation in pci-driver.c to decrement usage counter */
	pm_runtime_put_noidle(dev);
}

int sof_pci_probe(struct pci_dev *pci, const struct pci_device_id *pci_id)
{
	struct device *dev = &pci->dev;
	const struct sof_dev_desc *desc =
		(const struct sof_dev_desc *)pci_id->driver_data;
	struct snd_sof_pdata *sof_pdata;
	const struct snd_sof_dsp_ops *ops;
	int ret;

	dev_dbg(&pci->dev, "PCI DSP detected");

	/* get ops for platform */
	ops = desc->ops;
	if (!ops) {
		dev_err(dev, "error: no matching PCI descriptor ops\n");
		return -ENODEV;
	}

	sof_pdata = devm_kzalloc(dev, sizeof(*sof_pdata), GFP_KERNEL);
	if (!sof_pdata)
		return -ENOMEM;

	ret = pcim_enable_device(pci);
	if (ret < 0)
		return ret;

	ret = pci_request_regions(pci, "Audio DSP");
	if (ret < 0)
		return ret;

	sof_pdata->name = pci_name(pci);
	sof_pdata->desc = (struct sof_dev_desc *)pci_id->driver_data;
	sof_pdata->dev = dev;
	sof_pdata->fw_filename = desc->default_fw_filename;

	/*
	 * for platforms using the SOF community key, change the
	 * default path automatically to pick the right files from the
	 * linux-firmware tree. This can be overridden with the
	 * fw_path kernel parameter, e.g. for developers.
	 */

	/* alternate fw and tplg filenames ? */
	if (fw_path) {
		sof_pdata->fw_filename_prefix = fw_path;

		dev_dbg(dev,
			"Module parameter used, changed fw path to %s\n",
			sof_pdata->fw_filename_prefix);

	} else if (dmi_check_system(community_key_platforms)) {
		sof_pdata->fw_filename_prefix =
			devm_kasprintf(dev, GFP_KERNEL, "%s/%s",
				       sof_pdata->desc->default_fw_path,
				       "community");

		dev_dbg(dev,
			"Platform uses community key, changed fw path to %s\n",
			sof_pdata->fw_filename_prefix);
	} else {
		sof_pdata->fw_filename_prefix =
			sof_pdata->desc->default_fw_path;
	}

	if (tplg_path)
		sof_pdata->tplg_filename_prefix = tplg_path;
	else
		sof_pdata->tplg_filename_prefix =
			sof_pdata->desc->default_tplg_path;

	dmi_check_system(sof_tplg_table);
	if (sof_override_tplg_name)
		sof_pdata->tplg_filename = sof_override_tplg_name;

	/* set callback to be called on successful device probe to enable runtime_pm */
	sof_pdata->sof_probe_complete = sof_pci_probe_complete;

	/* call sof helper for DSP hardware probe */
	ret = snd_sof_device_probe(dev, sof_pdata);
	if (ret)
		pci_release_regions(pci);

	return ret;
}
EXPORT_SYMBOL_NS(sof_pci_probe, SND_SOC_SOF_PCI_DEV);

void sof_pci_remove(struct pci_dev *pci)
{
	/* call sof helper for DSP hardware remove */
	snd_sof_device_remove(&pci->dev);

	/* follow recommendation in pci-driver.c to increment usage counter */
	if (snd_sof_device_probe_completed(&pci->dev) &&
	    !(sof_pci_debug & SOF_PCI_DISABLE_PM_RUNTIME))
		pm_runtime_get_noresume(&pci->dev);

	/* release pci regions and disable device */
	pci_release_regions(pci);
}
EXPORT_SYMBOL_NS(sof_pci_remove, SND_SOC_SOF_PCI_DEV);

void sof_pci_shutdown(struct pci_dev *pci)
{
	snd_sof_device_shutdown(&pci->dev);
}
EXPORT_SYMBOL_NS(sof_pci_shutdown, SND_SOC_SOF_PCI_DEV);

MODULE_LICENSE("Dual BSD/GPL");
