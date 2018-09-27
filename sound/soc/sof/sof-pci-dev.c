// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2018 Intel Corporation. All rights reserved.
//
// Author: Liam Girdwood <liam.r.girdwood@linux.intel.com>
//

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/pm_runtime.h>
#include <linux/platform_device.h>
#include <linux/firmware.h>
#include <sound/pcm.h>
#include <sound/soc-acpi.h>
#include <sound/soc-acpi-intel-match.h>
#include <sound/sof.h>
#include <linux/pci.h>
#include <linux/acpi.h>
#include "sof-priv.h"

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
	.nocodec_fw_filename = "intel/sof-glk.ri",
	.nocodec_tplg_filename = "intel/sof-glk-nocodec.tplg"
};
#endif

#if IS_ENABLED(CONFIG_SND_SOC_SOF_BAYTRAIL)
static struct snd_soc_acpi_mach sof_byt_machines[] = {
	{
		.id = "INT343A",
		.drv_name = "edison",
		.sof_fw_filename = "intel/sof-byt.ri",
		.sof_tplg_filename = "intel/sof-byt.tplg",
		.asoc_plat_name = "baytrail-pcm-audio",
	},
	{}
};

static const struct sof_dev_desc byt_desc = {
	.machines		= sof_byt_machines,
	.resindex_lpe_base	= 3,	/* IRAM, but subtract IRAM offset */
	.resindex_pcicfg_base	= -1,
	.resindex_imr_base	= 0,
	.irqindex_host_ipc	= -1,
	.resindex_dma_base	= -1,
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
	.nocodec_fw_filename = "intel/sof-cnl.ri",
	.nocodec_tplg_filename = "intel/sof-cnl.tplg"
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
	.nocodec_fw_filename = "intel/sof-kbl.ri",
	.nocodec_tplg_filename = "intel/sof-kbl-nocodec.tplg"
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
	.nocodec_fw_filename = "intel/sof-icl.ri",
	.nocodec_tplg_filename = "intel/sof-icl-nocodec.tplg"
};
#endif

static const struct dev_pm_ops sof_pci_pm = {
	SET_SYSTEM_SLEEP_PM_OPS(snd_sof_suspend, snd_sof_resume)
	SET_RUNTIME_PM_OPS(snd_sof_runtime_suspend, snd_sof_runtime_resume,
			   NULL)
	.suspend_late = snd_sof_suspend_late,
};

static const struct sof_ops_table mach_ops[] = {
#if IS_ENABLED(CONFIG_SND_SOC_SOF_APOLLOLAKE)
	{&bxt_desc, &sof_apl_ops},
#endif
#if IS_ENABLED(CONFIG_SND_SOC_SOF_GEMINILAKE)
	{&glk_desc, &sof_apl_ops},
#endif
#if IS_ENABLED(CONFIG_SND_SOC_SOF_BAYTRAIL)
	{&byt_desc, &sof_byt_ops},
#endif
#if IS_ENABLED(CONFIG_SND_SOC_SOF_CANNONLAKE)
	{&cnl_desc, &sof_cnl_ops},
#endif
#if IS_ENABLED(CONFIG_SND_SOC_SOF_SKYLAKE)
	{&skl_desc, &sof_skl_ops},
#endif
#if IS_ENABLED(CONFIG_SND_SOC_SOF_KABYLAKE)
	{&kbl_desc, &sof_skl_ops},
#endif
#if IS_ENABLED(CONFIG_SND_SOC_SOF_ICELAKE)
	{&icl_desc, &sof_cnl_ops},
#endif
};

static struct snd_sof_dsp_ops *sof_pci_get_ops(const struct sof_dev_desc *d)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(mach_ops); i++) {
		if (d == mach_ops[i].desc)
			return mach_ops[i].ops;
	}

	/* not found */
	return NULL;
}

static int sof_pci_probe(struct pci_dev *pci,
			 const struct pci_device_id *pci_id)
{
	struct device *dev = &pci->dev;
	const struct sof_dev_desc *desc =
		(const struct sof_dev_desc *)pci_id->driver_data;
	struct snd_soc_acpi_mach *mach;
	struct snd_sof_pdata *sof_pdata;
	struct sof_platform_priv *priv;
	struct snd_sof_dsp_ops *ops;
	int ret = 0;

	dev_dbg(&pci->dev, "PCI DSP detected");

	/* get ops for platform */
	ops = sof_pci_get_ops(desc);
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

	ret = pci_enable_device(pci);
	if (ret < 0)
		return ret;

	ret = pci_request_regions(pci, "Audio DSP");
	if (ret < 0)
		goto disable_dev;

#if IS_ENABLED(CONFIG_SND_SOC_SOF_FORCE_NOCODEC_MODE)
	/* force nocodec mode */
	dev_warn(dev, "Force to use nocodec mode\n");
	mach = devm_kzalloc(dev, sizeof(*mach), GFP_KERNEL);
	ret = sof_nocodec_setup(dev, sof_pdata, mach, desc, ops);
	if (ret < 0)
		goto release_regions;
#else
	/* find machine */
	mach = snd_soc_acpi_find_machine(desc->machines);
	if (!mach) {
#if IS_ENABLED(CONFIG_SND_SOC_SOF_NOCODEC)
		/* fallback to nocodec mode */
		dev_warn(dev, "No matching ASoC machine driver found - using nocodec\n");
		mach = devm_kzalloc(dev, sizeof(*mach), GFP_KERNEL);
		ret = sof_nocodec_setup(dev, sof_pdata, mach, desc, ops);
		if (ret < 0)
			goto release_regions;
#else
		dev_err(dev, "No matching ASoC machine driver found - aborting probe\n");
		ret = -ENODEV;
		goto release_regions;
#endif
	}
#endif

	mach->pdata = ops;

	sof_pdata->id = pci_id->device;
	sof_pdata->name = pci_name(pci);
	sof_pdata->machine = mach;
	sof_pdata->desc = (struct sof_dev_desc *)pci_id->driver_data;
	priv->sof_pdata = sof_pdata;
	sof_pdata->dev = &pci->dev;
	sof_pdata->type = SOF_DEVICE_PCI;

	/* register sof-audio platform driver */
	ret = sof_create_platform_device(priv);
	if (ret) {
		dev_err(dev, "error: failed to create platform device!\n");
		goto release_regions;
	}

	/* allow runtime_pm */
	pm_runtime_set_autosuspend_delay(dev, SND_SOF_SUSPEND_DELAY);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_allow(dev);

	return ret;

release_regions:
	pci_release_regions(pci);
disable_dev:
	pci_disable_device(pci);

	return ret;
}

static void sof_pci_shutdown(struct pci_dev *pci)
{
	snd_sof_shutdown(&pci->dev);
}

static void sof_pci_remove(struct pci_dev *pci)
{
	struct sof_platform_priv *priv = pci_get_drvdata(pci);
	struct snd_sof_pdata *sof_pdata = priv->sof_pdata;

	/* unregister machine driver */
	platform_device_unregister(sof_pdata->pdev_mach);

	/* unregister sof-audio platform driver */
	if (!IS_ERR_OR_NULL(priv->pdev_pcm))
		platform_device_unregister(priv->pdev_pcm);

	/* release firmware */
	release_firmware(sof_pdata->fw);

	/* release pci regions and disable device */
	pci_release_regions(pci);
	pci_disable_device(pci);
}

/* PCI IDs */
static const struct pci_device_id sof_pci_ids[] = {
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

#if IS_ENABLED(CONFIG_SND_SOC_SOF_BAYTRAIL)
	{ PCI_DEVICE(0x8086, 0x119a),
		.driver_data = (unsigned long)&byt_desc},
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
	.shutdown = sof_pci_shutdown,
	.driver = {
		.pm = &sof_pci_pm,
	},
};
module_pci_driver(snd_sof_pci_driver);

MODULE_LICENSE("Dual BSD/GPL");
