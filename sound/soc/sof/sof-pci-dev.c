// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2018 Intel Corporation
//
// Author: Liam Girdwood <liam.r.girdwood@linux.intel.com>
//

#include <linux/firmware.h>
#include <linux/dmi.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/platform_data/x86/soc.h>
#include <linux/pm_runtime.h>
#include <sound/soc-acpi.h>
#include <sound/soc-acpi-intel-match.h>
#include <sound/sof.h>
#include "ops.h"
#include "sof-pci-dev.h"

static char *fw_path;
module_param(fw_path, charp, 0444);
MODULE_PARM_DESC(fw_path, "deprecated - moved to snd-sof module.");

static char *fw_filename;
module_param(fw_filename, charp, 0444);
MODULE_PARM_DESC(fw_filename, "deprecated - moved to snd-sof module.");

static char *lib_path;
module_param(lib_path, charp, 0444);
MODULE_PARM_DESC(lib_path, "deprecated - moved to snd-sof module.");

static char *tplg_path;
module_param(tplg_path, charp, 0444);
MODULE_PARM_DESC(tplg_path, "deprecated - moved to snd-sof module.");

static char *tplg_filename;
module_param(tplg_filename, charp, 0444);
MODULE_PARM_DESC(tplg_filename, "deprecated - moved to snd-sof module.");

static int sof_pci_debug;
module_param_named(sof_pci_debug, sof_pci_debug, int, 0444);
MODULE_PARM_DESC(sof_pci_debug, "SOF PCI debug options (0x0 all off)");

static int sof_pci_ipc_type = -1;
module_param_named(ipc_type, sof_pci_ipc_type, int, 0444);
MODULE_PARM_DESC(ipc_type, "deprecated - moved to snd-sof module.");

static const char *sof_dmi_override_tplg_name;
static bool sof_dmi_use_community_key;

#define SOF_PCI_DISABLE_PM_RUNTIME BIT(0)

static int sof_tplg_cb(const struct dmi_system_id *id)
{
	sof_dmi_override_tplg_name = id->driver_data;
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
	{
		.callback = sof_tplg_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Intel Corporation"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Alder Lake Client Platform"),
			DMI_MATCH(DMI_OEM_STRING, "AUDIO-ADL_MAX98373_ALC5682I_I2S"),
		},
		.driver_data = "sof-adl-rt5682-ssp0-max98373-ssp2.tplg",
	},
	{
		.callback = sof_tplg_cb,
		.matches = {
			DMI_MATCH(DMI_PRODUCT_FAMILY, "Google_Brya"),
			DMI_MATCH(DMI_OEM_STRING, "AUDIO-MAX98390_ALC5682I_I2S"),
		},
		.driver_data = "sof-adl-max98390-ssp2-rt5682-ssp0.tplg",
	},
	{
		.callback = sof_tplg_cb,
		.matches = {
			DMI_MATCH(DMI_PRODUCT_FAMILY, "Google_Brya"),
			DMI_MATCH(DMI_OEM_STRING, "AUDIO_AMP-MAX98360_ALC5682VS_I2S_2WAY"),
		},
		.driver_data = "sof-adl-max98360a-rt5682-2way.tplg",
	},
	{
		.callback = sof_tplg_cb,
		.matches = {
			DMI_MATCH(DMI_PRODUCT_FAMILY, "Google_Brya"),
			DMI_MATCH(DMI_OEM_STRING, "AUDIO-AUDIO_MAX98357_ALC5682I_I2S_2WAY"),
		},
		.driver_data = "sof-adl-max98357a-rt5682-2way.tplg",
	},
	{
		.callback = sof_tplg_cb,
		.matches = {
			DMI_MATCH(DMI_PRODUCT_FAMILY, "Google_Brya"),
			DMI_MATCH(DMI_OEM_STRING, "AUDIO-MAX98360_ALC5682I_I2S_AMP_SSP2"),
		},
		.driver_data = "sof-adl-max98357a-rt5682.tplg",
	},
	{}
};

/* all Up boards use the community key */
static int up_use_community_key(const struct dmi_system_id *id)
{
	sof_dmi_use_community_key = true;
	return 1;
}

/*
 * For ApolloLake Chromebooks we want to force the use of the Intel production key.
 * All newer platforms use the community key
 */
static int chromebook_use_community_key(const struct dmi_system_id *id)
{
	if (!soc_intel_is_apl())
		sof_dmi_use_community_key = true;
	return 1;
}

static const struct dmi_system_id community_key_platforms[] = {
	{
		.ident = "Up boards",
		.callback = up_use_community_key,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "AAEON"),
		}
	},
	{
		.ident = "Google Chromebooks",
		.callback = chromebook_use_community_key,
		.matches = {
			DMI_MATCH(DMI_PRODUCT_FAMILY, "Google"),
		}
	},
	{
		.ident = "Google firmware",
		.callback = chromebook_use_community_key,
		.matches = {
			DMI_MATCH(DMI_BIOS_VERSION, "Google"),
		}
	},
	{},
};

EXPORT_NS_DEV_PM_OPS(sof_pci_pm, SND_SOC_SOF_PCI_DEV) = {
	.prepare = snd_sof_prepare,
	.complete = snd_sof_complete,
	SYSTEM_SLEEP_PM_OPS(snd_sof_suspend, snd_sof_resume)
	RUNTIME_PM_OPS(snd_sof_runtime_suspend, snd_sof_runtime_resume,
		       snd_sof_runtime_idle)
};

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
	struct sof_loadable_file_profile *path_override;
	struct device *dev = &pci->dev;
	const struct sof_dev_desc *desc =
		(const struct sof_dev_desc *)pci_id->driver_data;
	struct snd_sof_pdata *sof_pdata;
	int ret;

	dev_dbg(&pci->dev, "PCI DSP detected");

	if (!desc) {
		dev_err(dev, "error: no matching PCI descriptor\n");
		return -ENODEV;
	}

	if (!desc->ops) {
		dev_err(dev, "error: no matching PCI descriptor ops\n");
		return -ENODEV;
	}

	sof_pdata = devm_kzalloc(dev, sizeof(*sof_pdata), GFP_KERNEL);
	if (!sof_pdata)
		return -ENOMEM;

	ret = pcim_enable_device(pci);
	if (ret < 0)
		return ret;

	ret = pcim_request_all_regions(pci, "Audio DSP");
	if (ret < 0)
		return ret;

	sof_pdata->name = pci_name(pci);

	/* PCI defines a vendor ID of 0xFFFF as invalid. */
	if (pci->subsystem_vendor != 0xFFFF) {
		sof_pdata->subsystem_vendor = pci->subsystem_vendor;
		sof_pdata->subsystem_device = pci->subsystem_device;
		sof_pdata->subsystem_id_set = true;
	}

	sof_pdata->desc = desc;
	sof_pdata->dev = dev;

	path_override = &sof_pdata->ipc_file_profile_base;

	if (sof_pci_ipc_type < 0) {
		path_override->ipc_type = desc->ipc_default;
	} else if (sof_pci_ipc_type < SOF_IPC_TYPE_COUNT) {
		path_override->ipc_type = sof_pci_ipc_type;
	} else {
		dev_err(dev, "Invalid IPC type requested: %d\n", sof_pci_ipc_type);
		return -EINVAL;
	}

	path_override->fw_path = fw_path;
	path_override->fw_name = fw_filename;
	path_override->fw_lib_path = lib_path;
	path_override->tplg_path = tplg_path;

	if (dmi_check_system(community_key_platforms) &&
	    sof_dmi_use_community_key) {
		path_override->fw_path_postfix = "community";
		path_override->fw_lib_path_postfix = "community";
	}

	/*
	 * the topology filename will be provided in the machine descriptor, unless
	 * it is overridden by a module parameter or DMI quirk.
	 */
	if (tplg_filename) {
		path_override->tplg_name = tplg_filename;
	} else {
		dmi_check_system(sof_tplg_table);
		if (sof_dmi_override_tplg_name)
			path_override->tplg_name = sof_dmi_override_tplg_name;
	}

	/* set callback to be called on successful device probe to enable runtime_pm */
	sof_pdata->sof_probe_complete = sof_pci_probe_complete;

	/* call sof helper for DSP hardware probe */
	return snd_sof_device_probe(dev, sof_pdata);
}
EXPORT_SYMBOL_NS(sof_pci_probe, "SND_SOC_SOF_PCI_DEV");

void sof_pci_remove(struct pci_dev *pci)
{
	/* call sof helper for DSP hardware remove */
	snd_sof_device_remove(&pci->dev);

	/* follow recommendation in pci-driver.c to increment usage counter */
	if (snd_sof_device_probe_completed(&pci->dev) &&
	    !(sof_pci_debug & SOF_PCI_DISABLE_PM_RUNTIME))
		pm_runtime_get_noresume(&pci->dev);
}
EXPORT_SYMBOL_NS(sof_pci_remove, "SND_SOC_SOF_PCI_DEV");

void sof_pci_shutdown(struct pci_dev *pci)
{
	snd_sof_device_shutdown(&pci->dev);
}
EXPORT_SYMBOL_NS(sof_pci_shutdown, "SND_SOC_SOF_PCI_DEV");

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("SOF support for PCI platforms");
