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
#include <sound/intel-dsp-config.h>
#include <sound/soc-acpi.h>
#include <sound/soc-acpi-intel-match.h>
#include <sound/sof.h>
#include "ops.h"

/* platform specific devices */
#include "intel/shim.h"
#include "intel/hda.h"

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
		.ident = "Google Chromebooks",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Google"),
		}
	},
	{},
};

#if IS_ENABLED(CONFIG_SND_SOC_SOF_APOLLOLAKE)
static const struct sof_dev_desc bxt_desc = {
	.machines		= snd_soc_acpi_intel_bxt_machines,
	.use_acpi_target_states	= true,
	.resindex_lpe_base	= 0,
	.resindex_pcicfg_base	= -1,
	.resindex_imr_base	= -1,
	.irqindex_host_ipc	= -1,
	.resindex_dma_base	= -1,
	.chip_info = &apl_chip_info,
	.default_fw_path = "intel/sof",
	.default_tplg_path = "intel/sof-tplg",
	.default_fw_filename = "sof-apl.ri",
	.nocodec_tplg_filename = "sof-apl-nocodec.tplg",
	.ops = &sof_apl_ops,
};
#endif

#if IS_ENABLED(CONFIG_SND_SOC_SOF_GEMINILAKE)
static const struct sof_dev_desc glk_desc = {
	.machines		= snd_soc_acpi_intel_glk_machines,
	.use_acpi_target_states	= true,
	.resindex_lpe_base	= 0,
	.resindex_pcicfg_base	= -1,
	.resindex_imr_base	= -1,
	.irqindex_host_ipc	= -1,
	.resindex_dma_base	= -1,
	.chip_info = &apl_chip_info,
	.default_fw_path = "intel/sof",
	.default_tplg_path = "intel/sof-tplg",
	.default_fw_filename = "sof-glk.ri",
	.nocodec_tplg_filename = "sof-glk-nocodec.tplg",
	.ops = &sof_apl_ops,
};
#endif

#if IS_ENABLED(CONFIG_SND_SOC_SOF_MERRIFIELD)
static struct snd_soc_acpi_mach sof_tng_machines[] = {
	{
		.id = "INT343A",
		.drv_name = "edison",
		.sof_fw_filename = "sof-byt.ri",
		.sof_tplg_filename = "sof-byt.tplg",
	},
	{}
};

static const struct sof_dev_desc tng_desc = {
	.machines		= sof_tng_machines,
	.resindex_lpe_base	= 3,	/* IRAM, but subtract IRAM offset */
	.resindex_pcicfg_base	= -1,
	.resindex_imr_base	= 0,
	.irqindex_host_ipc	= -1,
	.resindex_dma_base	= -1,
	.chip_info = &tng_chip_info,
	.default_fw_path = "intel/sof",
	.default_tplg_path = "intel/sof-tplg",
	.default_fw_filename = "sof-byt.ri",
	.nocodec_tplg_filename = "sof-byt.tplg",
	.ops = &sof_tng_ops,
};
#endif

#if IS_ENABLED(CONFIG_SND_SOC_SOF_CANNONLAKE)
static const struct sof_dev_desc cnl_desc = {
	.machines		= snd_soc_acpi_intel_cnl_machines,
	.alt_machines		= snd_soc_acpi_intel_cnl_sdw_machines,
	.use_acpi_target_states	= true,
	.resindex_lpe_base	= 0,
	.resindex_pcicfg_base	= -1,
	.resindex_imr_base	= -1,
	.irqindex_host_ipc	= -1,
	.resindex_dma_base	= -1,
	.chip_info = &cnl_chip_info,
	.default_fw_path = "intel/sof",
	.default_tplg_path = "intel/sof-tplg",
	.default_fw_filename = "sof-cnl.ri",
	.nocodec_tplg_filename = "sof-cnl-nocodec.tplg",
	.ops = &sof_cnl_ops,
};
#endif

#if IS_ENABLED(CONFIG_SND_SOC_SOF_COFFEELAKE)
static const struct sof_dev_desc cfl_desc = {
	.machines		= snd_soc_acpi_intel_cfl_machines,
	.alt_machines		= snd_soc_acpi_intel_cfl_sdw_machines,
	.use_acpi_target_states	= true,
	.resindex_lpe_base	= 0,
	.resindex_pcicfg_base	= -1,
	.resindex_imr_base	= -1,
	.irqindex_host_ipc	= -1,
	.resindex_dma_base	= -1,
	.chip_info = &cnl_chip_info,
	.default_fw_path = "intel/sof",
	.default_tplg_path = "intel/sof-tplg",
	.default_fw_filename = "sof-cfl.ri",
	.nocodec_tplg_filename = "sof-cnl-nocodec.tplg",
	.ops = &sof_cnl_ops,
};
#endif

#if IS_ENABLED(CONFIG_SND_SOC_SOF_COMETLAKE)
static const struct sof_dev_desc cml_desc = {
	.machines		= snd_soc_acpi_intel_cml_machines,
	.alt_machines		= snd_soc_acpi_intel_cml_sdw_machines,
	.use_acpi_target_states	= true,
	.resindex_lpe_base	= 0,
	.resindex_pcicfg_base	= -1,
	.resindex_imr_base	= -1,
	.irqindex_host_ipc	= -1,
	.resindex_dma_base	= -1,
	.chip_info = &cnl_chip_info,
	.default_fw_path = "intel/sof",
	.default_tplg_path = "intel/sof-tplg",
	.default_fw_filename = "sof-cml.ri",
	.nocodec_tplg_filename = "sof-cnl-nocodec.tplg",
	.ops = &sof_cnl_ops,
};
#endif

#if IS_ENABLED(CONFIG_SND_SOC_SOF_ICELAKE)
static const struct sof_dev_desc icl_desc = {
	.machines               = snd_soc_acpi_intel_icl_machines,
	.alt_machines		= snd_soc_acpi_intel_icl_sdw_machines,
	.use_acpi_target_states	= true,
	.resindex_lpe_base      = 0,
	.resindex_pcicfg_base   = -1,
	.resindex_imr_base      = -1,
	.irqindex_host_ipc      = -1,
	.resindex_dma_base      = -1,
	.chip_info = &icl_chip_info,
	.default_fw_path = "intel/sof",
	.default_tplg_path = "intel/sof-tplg",
	.default_fw_filename = "sof-icl.ri",
	.nocodec_tplg_filename = "sof-icl-nocodec.tplg",
	.ops = &sof_cnl_ops,
};
#endif

#if IS_ENABLED(CONFIG_SND_SOC_SOF_TIGERLAKE)
static const struct sof_dev_desc tgl_desc = {
	.machines               = snd_soc_acpi_intel_tgl_machines,
	.alt_machines		= snd_soc_acpi_intel_tgl_sdw_machines,
	.use_acpi_target_states	= true,
	.resindex_lpe_base      = 0,
	.resindex_pcicfg_base   = -1,
	.resindex_imr_base      = -1,
	.irqindex_host_ipc      = -1,
	.resindex_dma_base      = -1,
	.chip_info = &tgl_chip_info,
	.default_fw_path = "intel/sof",
	.default_tplg_path = "intel/sof-tplg",
	.default_fw_filename = "sof-tgl.ri",
	.nocodec_tplg_filename = "sof-tgl-nocodec.tplg",
	.ops = &sof_tgl_ops,
};

static const struct sof_dev_desc tglh_desc = {
	.machines               = snd_soc_acpi_intel_tgl_machines,
	.alt_machines		= snd_soc_acpi_intel_tgl_sdw_machines,
	.resindex_lpe_base      = 0,
	.resindex_pcicfg_base   = -1,
	.resindex_imr_base      = -1,
	.irqindex_host_ipc      = -1,
	.resindex_dma_base      = -1,
	.chip_info = &tglh_chip_info,
	.default_fw_path = "intel/sof",
	.default_tplg_path = "intel/sof-tplg",
	.default_fw_filename = "sof-tgl-h.ri",
	.nocodec_tplg_filename = "sof-tgl-nocodec.tplg",
	.ops = &sof_tgl_ops,
};
#endif

#if IS_ENABLED(CONFIG_SND_SOC_SOF_ELKHARTLAKE)
static const struct sof_dev_desc ehl_desc = {
	.machines               = snd_soc_acpi_intel_ehl_machines,
	.use_acpi_target_states	= true,
	.resindex_lpe_base      = 0,
	.resindex_pcicfg_base   = -1,
	.resindex_imr_base      = -1,
	.irqindex_host_ipc      = -1,
	.resindex_dma_base      = -1,
	.chip_info = &ehl_chip_info,
	.default_fw_path = "intel/sof",
	.default_tplg_path = "intel/sof-tplg",
	.default_fw_filename = "sof-ehl.ri",
	.nocodec_tplg_filename = "sof-ehl-nocodec.tplg",
	.ops = &sof_cnl_ops,
};
#endif

#if IS_ENABLED(CONFIG_SND_SOC_SOF_JASPERLAKE)
static const struct sof_dev_desc jsl_desc = {
	.machines               = snd_soc_acpi_intel_jsl_machines,
	.use_acpi_target_states	= true,
	.resindex_lpe_base      = 0,
	.resindex_pcicfg_base   = -1,
	.resindex_imr_base      = -1,
	.irqindex_host_ipc      = -1,
	.resindex_dma_base      = -1,
	.chip_info = &jsl_chip_info,
	.default_fw_path = "intel/sof",
	.default_tplg_path = "intel/sof-tplg",
	.default_fw_filename = "sof-jsl.ri",
	.nocodec_tplg_filename = "sof-jsl-nocodec.tplg",
	.ops = &sof_cnl_ops,
};
#endif

#if IS_ENABLED(CONFIG_SND_SOC_SOF_ALDERLAKE)
static const struct sof_dev_desc adls_desc = {
	.machines		= snd_soc_acpi_intel_hda_machines,
	.resindex_lpe_base      = 0,
	.resindex_pcicfg_base   = -1,
	.resindex_imr_base      = -1,
	.irqindex_host_ipc      = -1,
	.resindex_dma_base      = -1,
	.chip_info = &adls_chip_info,
	.default_fw_path = "intel/sof",
	.default_tplg_path = "intel/sof-tplg",
	.default_fw_filename = "sof-adl-s.ri",
	.nocodec_tplg_filename = "sof-adl-nocodec.tplg",
	.ops = &sof_tgl_ops,
};
#endif

static const struct dev_pm_ops sof_pci_pm = {
	.prepare = snd_sof_prepare,
	.complete = snd_sof_complete,
	SET_SYSTEM_SLEEP_PM_OPS(snd_sof_suspend, snd_sof_resume)
	SET_RUNTIME_PM_OPS(snd_sof_runtime_suspend, snd_sof_runtime_resume,
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

static int sof_pci_probe(struct pci_dev *pci,
			 const struct pci_device_id *pci_id)
{
	struct device *dev = &pci->dev;
	const struct sof_dev_desc *desc =
		(const struct sof_dev_desc *)pci_id->driver_data;
	struct snd_sof_pdata *sof_pdata;
	const struct snd_sof_dsp_ops *ops;
	int ret;

	ret = snd_intel_dsp_driver_probe(pci);
	if (ret != SND_INTEL_DSP_DRIVER_ANY && ret != SND_INTEL_DSP_DRIVER_SOF) {
		dev_dbg(&pci->dev, "SOF PCI driver not selected, aborting probe\n");
		return -ENODEV;
	}
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

#if IS_ENABLED(CONFIG_SND_SOC_SOF_PROBE_WORK_QUEUE)
	/* set callback to enable runtime_pm */
	sof_pdata->sof_probe_complete = sof_pci_probe_complete;
#endif
	/* call sof helper for DSP hardware probe */
	ret = snd_sof_device_probe(dev, sof_pdata);
	if (ret) {
		dev_err(dev, "error: failed to probe DSP hardware!\n");
		goto release_regions;
	}

#if !IS_ENABLED(CONFIG_SND_SOC_SOF_PROBE_WORK_QUEUE)
	sof_pci_probe_complete(dev);
#endif

	return ret;

release_regions:
	pci_release_regions(pci);

	return ret;
}

static void sof_pci_remove(struct pci_dev *pci)
{
	/* call sof helper for DSP hardware remove */
	snd_sof_device_remove(&pci->dev);

	/* follow recommendation in pci-driver.c to increment usage counter */
	if (!(sof_pci_debug & SOF_PCI_DISABLE_PM_RUNTIME))
		pm_runtime_get_noresume(&pci->dev);

	/* release pci regions and disable device */
	pci_release_regions(pci);
}

/* PCI IDs */
static const struct pci_device_id sof_pci_ids[] = {
#if IS_ENABLED(CONFIG_SND_SOC_SOF_MERRIFIELD)
	{ PCI_DEVICE(0x8086, 0x119a),
		.driver_data = (unsigned long)&tng_desc},
#endif
#if IS_ENABLED(CONFIG_SND_SOC_SOF_APOLLOLAKE)
	/* BXT-P & Apollolake */
	{ PCI_DEVICE(0x8086, 0x5a98),
		.driver_data = (unsigned long)&bxt_desc},
	{ PCI_DEVICE(0x8086, 0x1a98),
		.driver_data = (unsigned long)&bxt_desc},
#endif
#if IS_ENABLED(CONFIG_SND_SOC_SOF_GEMINILAKE)
	{ PCI_DEVICE(0x8086, 0x3198),
		.driver_data = (unsigned long)&glk_desc},
#endif
#if IS_ENABLED(CONFIG_SND_SOC_SOF_CANNONLAKE)
	{ PCI_DEVICE(0x8086, 0x9dc8),
		.driver_data = (unsigned long)&cnl_desc},
#endif
#if IS_ENABLED(CONFIG_SND_SOC_SOF_COFFEELAKE)
	{ PCI_DEVICE(0x8086, 0xa348),
		.driver_data = (unsigned long)&cfl_desc},
#endif
#if IS_ENABLED(CONFIG_SND_SOC_SOF_ICELAKE)
	{ PCI_DEVICE(0x8086, 0x34C8), /* ICL-LP */
		.driver_data = (unsigned long)&icl_desc},
	{ PCI_DEVICE(0x8086, 0x3dc8), /* ICL-H */
		.driver_data = (unsigned long)&icl_desc},

#endif
#if IS_ENABLED(CONFIG_SND_SOC_SOF_JASPERLAKE)
	{ PCI_DEVICE(0x8086, 0x38c8),
		.driver_data = (unsigned long)&jsl_desc},
	{ PCI_DEVICE(0x8086, 0x4dc8),
		.driver_data = (unsigned long)&jsl_desc},
#endif
#if IS_ENABLED(CONFIG_SND_SOC_SOF_COMETLAKE)
	{ PCI_DEVICE(0x8086, 0x02c8), /* CML-LP */
		.driver_data = (unsigned long)&cml_desc},
	{ PCI_DEVICE(0x8086, 0x06c8), /* CML-H */
		.driver_data = (unsigned long)&cml_desc},
	{ PCI_DEVICE(0x8086, 0xa3f0), /* CML-S */
		.driver_data = (unsigned long)&cml_desc},
#endif
#if IS_ENABLED(CONFIG_SND_SOC_SOF_TIGERLAKE)
	{ PCI_DEVICE(0x8086, 0xa0c8), /* TGL-LP */
		.driver_data = (unsigned long)&tgl_desc},
	{ PCI_DEVICE(0x8086, 0x43c8), /* TGL-H */
		.driver_data = (unsigned long)&tglh_desc},

#endif
#if IS_ENABLED(CONFIG_SND_SOC_SOF_ELKHARTLAKE)
	{ PCI_DEVICE(0x8086, 0x4b55),
		.driver_data = (unsigned long)&ehl_desc},
	{ PCI_DEVICE(0x8086, 0x4b58),
		.driver_data = (unsigned long)&ehl_desc},
#endif
#if IS_ENABLED(CONFIG_SND_SOC_SOF_ALDERLAKE)
	{ PCI_DEVICE(0x8086, 0x7ad0),
		.driver_data = (unsigned long)&adls_desc},
#endif
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, sof_pci_ids);

/* pci_driver definition */
static struct pci_driver snd_sof_pci_driver = {
	.name = "sof-audio-pci",
	.id_table = sof_pci_ids,
	.probe = sof_pci_probe,
	.remove = sof_pci_remove,
	.driver = {
		.pm = &sof_pci_pm,
	},
};
module_pci_driver(snd_sof_pci_driver);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_IMPORT_NS(SND_SOC_SOF_MERRIFIELD);
MODULE_IMPORT_NS(SND_SOC_SOF_INTEL_HDA_COMMON);
