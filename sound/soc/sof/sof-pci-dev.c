// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2018 Intel Corporation. All rights reserved.
//
// Author: Liam Girdwood <liam.r.girdwood@linux.intel.com>
//

#include <linux/firmware.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/pm_runtime.h>
#include <sound/soc-acpi.h>
#include <sound/soc-acpi-intel-match.h>
#include <sound/sof.h>
#include "ops.h"

/* platform specific devices */
#include "intel/shim.h"
#include "intel/hda.h"

#if IS_ENABLED(CONFIG_SND_SOC_SOF_APOLLOLAKE)
static struct sof_dev_desc bxt_desc = {
	.machines		= snd_soc_acpi_intel_bxt_machines,
	.resindex_lpe_base	= 0,
	.resindex_pcicfg_base	= -1,
	.resindex_imr_base	= -1,
	.irqindex_host_ipc	= -1,
	.resindex_dma_base	= -1,
	.chip_info = &apl_chip_info,
	.nocodec_fw_filename = "intel/sof-apl.ri",
	.nocodec_tplg_filename = "intel/sof-apl-nocodec.tplg"
};
#endif

#if IS_ENABLED(CONFIG_SND_SOC_SOF_GEMINILAKE)
static struct sof_dev_desc glk_desc = {
	.machines		= snd_soc_acpi_intel_glk_machines,
	.resindex_lpe_base	= 0,
	.resindex_pcicfg_base	= -1,
	.resindex_imr_base	= -1,
	.irqindex_host_ipc	= -1,
	.resindex_dma_base	= -1,
	.chip_info = &apl_chip_info,
	.nocodec_fw_filename = "intel/sof-glk.ri",
	.nocodec_tplg_filename = "intel/sof-glk-nocodec.tplg"
};
#endif

#if IS_ENABLED(CONFIG_SND_SOC_SOF_EDISON)
static struct snd_soc_acpi_mach sof_tng_machines[] = {
	{
		.id = "INT343A",
		.drv_name = "edison",
		.sof_fw_filename = "intel/sof-byt.ri",
		.sof_tplg_filename = "intel/sof-byt.tplg",
		.asoc_plat_name = "baytrail-pcm-audio",
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
	.nocodec_fw_filename = "intel/sof-byt.ri",
	.nocodec_tplg_filename = "intel/sof-byt.tplg"
};
#endif

#if IS_ENABLED(CONFIG_SND_SOC_SOF_CANNONLAKE)
static const struct sof_dev_desc cnl_desc = {
	.machines		= snd_soc_acpi_intel_cnl_machines,
	.resindex_lpe_base	= 0,
	.resindex_pcicfg_base	= -1,
	.resindex_imr_base	= -1,
	.irqindex_host_ipc	= -1,
	.resindex_dma_base	= -1,
	.chip_info = &cnl_chip_info,
	.nocodec_fw_filename = "intel/sof-cnl.ri",
	.nocodec_tplg_filename = "intel/sof-cnl.tplg"
};
#endif

#if IS_ENABLED(CONFIG_SND_SOC_SOF_ICELAKE)
static const struct sof_dev_desc icl_desc = {
	.machines               = snd_soc_acpi_intel_icl_machines,
	.resindex_lpe_base      = 0,
	.resindex_pcicfg_base   = -1,
	.resindex_imr_base      = -1,
	.irqindex_host_ipc      = -1,
	.resindex_dma_base      = -1,
	.chip_info = &cnl_chip_info,
	.nocodec_fw_filename = "intel/sof-icl.ri",
	.nocodec_tplg_filename = "intel/sof-icl-nocodec.tplg"
};
#endif

#if IS_ENABLED(CONFIG_SND_SOC_SOF_SKYLAKE)
static struct sof_dev_desc skl_desc = {
	.machines		= snd_soc_acpi_intel_skl_machines,
	.resindex_lpe_base	= 0,
	.resindex_pcicfg_base	= -1,
	.resindex_imr_base	= -1,
	.irqindex_host_ipc	= -1,
	.resindex_dma_base	= -1,
	.chip_info = &skl_chip_info,
	.nocodec_fw_filename = "intel/sof-skl.ri",
	.nocodec_tplg_filename = "intel/sof-skl-nocodec.tplg"
};
#endif

#if IS_ENABLED(CONFIG_SND_SOC_SOF_KABYLAKE)
static struct sof_dev_desc kbl_desc = {
	.machines		= snd_soc_acpi_intel_kbl_machines,
	.resindex_lpe_base	= 0,
	.resindex_pcicfg_base	= -1,
	.resindex_imr_base	= -1,
	.irqindex_host_ipc	= -1,
	.resindex_dma_base	= -1,
	.chip_info = &skl_chip_info,
	.nocodec_fw_filename = "intel/sof-kbl.ri",
	.nocodec_tplg_filename = "intel/sof-kbl-nocodec.tplg"
};
#endif

static const struct dev_pm_ops sof_pci_pm = {
	SET_SYSTEM_SLEEP_PM_OPS(snd_sof_suspend, snd_sof_resume)
	SET_RUNTIME_PM_OPS(snd_sof_runtime_suspend, snd_sof_runtime_resume,
			   NULL)
	.prepare = snd_sof_prepare,

};

static int sof_pci_probe(struct pci_dev *pci,
			 const struct pci_device_id *pci_id)
{
	struct device *dev = &pci->dev;
	const struct sof_dev_desc *desc =
		(const struct sof_dev_desc *)pci_id->driver_data;
	struct snd_soc_acpi_mach *mach;
	struct snd_sof_pdata *sof_pdata;
	struct sof_platform_priv *priv;
	const struct snd_sof_dsp_ops *ops;
	int ret = 0;

	dev_dbg(&pci->dev, "PCI DSP detected");

	/* get ops for platform */
	ops = ((const struct sof_intel_dsp_desc *)desc->chip_info)->ops;
	if (!ops) {
		dev_err(dev, "error: no matching PCI descriptor ops\n");
		return -ENODEV;
	}

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	pci_set_drvdata(pci, priv);

	sof_pdata = devm_kzalloc(dev, sizeof(*sof_pdata), GFP_KERNEL);
	if (!sof_pdata)
		return -ENOMEM;

	ret = pcim_enable_device(pci);
	if (ret < 0)
		return ret;

	ret = pci_request_regions(pci, "Audio DSP");
	if (ret < 0)
		return ret;

#if IS_ENABLED(CONFIG_SND_SOC_SOF_FORCE_NOCODEC_MODE)
	/* force nocodec mode */
	dev_warn(dev, "Force to use nocodec mode\n");
	mach = devm_kzalloc(dev, sizeof(*mach), GFP_KERNEL);
	if (!mach) {
		ret = -ENOMEM;
		goto release_regions;
	}
	ret = sof_nocodec_setup(dev, sof_pdata, mach, desc, ops);
	if (ret < 0)
		goto release_regions;
#else
	/* find machine */
	mach = snd_soc_acpi_find_machine(desc->machines);
#if IS_ENABLED(CONFIG_SND_SOC_SOF_HDA)
	if (!mach) {
		dev_warn(dev, "No matching ASoC machine driver found - falling back to HDA codec\n");
		mach = snd_soc_acpi_intel_hda_machines;
		mach->sof_fw_filename = desc->nocodec_fw_filename;

		/*
		 * TODO: we need to find a way to check if codecs are actually
		 * present
		 */
	}
#endif /* CONFIG_SND_SOC_SOF_HDA */

#if IS_ENABLED(CONFIG_SND_SOC_SOF_NOCODEC)
	if (!mach) {
		/* fallback to nocodec mode */
		dev_warn(dev, "No matching ASoC machine driver found - using nocodec\n");
		mach = devm_kzalloc(dev, sizeof(*mach), GFP_KERNEL);
		if (!mach) {
			ret = -ENOMEM;
			goto release_regions;
		}
		ret = sof_nocodec_setup(dev, sof_pdata, mach, desc, ops);
		if (ret < 0)
			goto release_regions;
	}
#endif /* CONFIG_SND_SOC_SOF_NOCODEC */

#endif /* CONFIG_SND_SOC_SOF_FORCE_NOCODEC_MODE */

	if (!mach) {
		dev_err(dev, "error: no matching ASoC machine driver found - aborting probe\n");
		ret = -ENODEV;
		goto release_regions;
	}

	/*
	 * save ops in pdata.
	 * TODO: the explicit cast removes the const attribute, we'll need
	 * to add a dedicated ops field in the generic soc-acpi structure
	 * to avoid such issues
	 */
	mach->pdata = (void *)ops;

	sof_pdata->id = pci_id->device;
	sof_pdata->name = pci_name(pci);
	sof_pdata->machine = mach;
	sof_pdata->desc = (struct sof_dev_desc *)pci_id->driver_data;
	priv->sof_pdata = sof_pdata;
	sof_pdata->dev = &pci->dev;
	sof_pdata->platform = "sof-audio";

	/* register sof-audio platform driver */
	ret = sof_create_platform_device(priv);
	if (ret) {
		dev_err(dev, "error: failed to create platform device!\n");
		goto release_regions;
	}

	/* allow runtime_pm */
	pm_runtime_set_autosuspend_delay(dev, SND_SOF_SUSPEND_DELAY_MS);
	pm_runtime_use_autosuspend(dev);

	/*
	 * runtime pm for pci device is "forbidden" by default.
	 * so call pm_runtime_allow() to enable it.
	 */
	pm_runtime_allow(dev);

	/* follow recommendation in pci-driver.c to decrement usage counter */
	pm_runtime_put_noidle(dev);

	return ret;

release_regions:
	pci_release_regions(pci);

	return ret;
}

static void sof_pci_remove(struct pci_dev *pci)
{
	struct sof_platform_priv *priv = pci_get_drvdata(pci);
	struct snd_sof_pdata *sof_pdata = priv->sof_pdata;

	/* unregister sof-audio platform driver */
	if (!IS_ERR_OR_NULL(priv->pdev_pcm))
		platform_device_unregister(priv->pdev_pcm);

	/* release firmware */
	release_firmware(sof_pdata->fw);

	/* follow recommendation in pci-driver.c to increment usage counter */
	pm_runtime_get_noresume(&pci->dev);

	/* release pci regions and disable device */
	pci_release_regions(pci);
}

/* PCI IDs */
static const struct pci_device_id sof_pci_ids[] = {
#if IS_ENABLED(CONFIG_SND_SOC_SOF_EDISON)
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
#if IS_ENABLED(CONFIG_SND_SOC_SOF_KABYLAKE)
	{ PCI_DEVICE(0x8086, 0x9d71),
		.driver_data = (unsigned long)&kbl_desc},
#endif
#if IS_ENABLED(CONFIG_SND_SOC_SOF_SKYLAKE)
	{ PCI_DEVICE(0x8086, 0x9d70),
		.driver_data = (unsigned long)&skl_desc},
#endif
#if IS_ENABLED(CONFIG_SND_SOC_SOF_ICELAKE)
	{ PCI_DEVICE(0x8086, 0x34C8),
		.driver_data = (unsigned long)&icl_desc},
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
