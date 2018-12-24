// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
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
#ifdef CONFIG_X86
#include <asm/iosf_mbi.h>
#endif

#include "ops.h"

/* platform specific devices */
#include "intel/shim.h"

#if IS_ENABLED(CONFIG_SND_SOC_SOF_HASWELL)
static struct sof_dev_desc sof_acpi_haswell_desc = {
	.machines = snd_soc_acpi_intel_haswell_machines,
	.resindex_lpe_base = 0,
	.resindex_pcicfg_base = 1,
	.resindex_imr_base = -1,
	.irqindex_host_ipc = 0,
	.chip_info = &hsw_chip_info,
	.nocodec_fw_filename = "intel/sof-hsw.ri",
	.nocodec_tplg_filename = "intel/sof-hsw-nocodec.tplg",
	.ops = &sof_hsw_ops,
	.arch_ops = &sof_xtensa_arch_ops
};
#endif

#if IS_ENABLED(CONFIG_SND_SOC_SOF_BROADWELL)
static struct sof_dev_desc sof_acpi_broadwell_desc = {
	.machines = snd_soc_acpi_intel_broadwell_machines,
	.resindex_lpe_base = 0,
	.resindex_pcicfg_base = 1,
	.resindex_imr_base = -1,
	.irqindex_host_ipc = 0,
	.chip_info = &bdw_chip_info,
	.nocodec_fw_filename = "intel/sof-bdw.ri",
	.nocodec_tplg_filename = "intel/sof-bdw-nocodec.tplg",
	.ops = &sof_bdw_ops,
	.arch_ops = &sof_xtensa_arch_ops
};
#endif

#if IS_ENABLED(CONFIG_SND_SOC_SOF_BAYTRAIL)

/* BYTCR uses different IRQ index */
static struct sof_dev_desc sof_acpi_baytrailcr_desc = {
	.machines = snd_soc_acpi_intel_baytrail_machines,
	.resindex_lpe_base = 0,
	.resindex_pcicfg_base = 1,
	.resindex_imr_base = 2,
	.irqindex_host_ipc = 0,
	.chip_info = &byt_chip_info,
	.nocodec_fw_filename = "intel/sof-byt.ri",
	.nocodec_tplg_filename = "intel/sof-byt-nocodec.tplg",
	.ops = &sof_byt_ops,
	.arch_ops = &sof_xtensa_arch_ops
};

static struct sof_dev_desc sof_acpi_baytrail_desc = {
	.machines = snd_soc_acpi_intel_baytrail_machines,
	.resindex_lpe_base = 0,
	.resindex_pcicfg_base = 1,
	.resindex_imr_base = 2,
	.irqindex_host_ipc = 5,
	.chip_info = &byt_chip_info,
	.nocodec_fw_filename = "intel/sof-byt.ri",
	.nocodec_tplg_filename = "intel/sof-byt-nocodec.tplg",
	.ops = &sof_byt_ops,
	.arch_ops = &sof_xtensa_arch_ops
};

#ifdef CONFIG_X86 /* TODO: move this to common helper */
static int is_byt_cr(struct device *dev)
{
	u32 bios_status;
	int status;

	if (!iosf_mbi_available()) {
		dev_info(dev, "IOSF_MBI not enabled - can't determine CPU variant\n");
		return -EIO;
	}

	status = iosf_mbi_read(BT_MBI_UNIT_PMC, /* 0x04 PUNIT */
			       MBI_REG_READ, /* 0x10 */
			       0x006, /* BIOS_CONFIG */
			       &bios_status);

	if (status) {
		dev_err(dev, "error: could not read PUNIT BIOS_CONFIG\n");
		return -EIO;
	}

	/* bits 26:27 mirror PMIC options */
	bios_status = (bios_status >> 26) & 3;

	if (bios_status == 1 || bios_status == 3) {
		dev_info(dev, "BYT-CR detected\n");
		return 1;
	}

	dev_info(dev, "BYT-CR not detected\n");
	return 0;
}
#else
static int is_byt_cr(struct device *dev)
{
	return 0;
}
#endif

static struct sof_dev_desc sof_acpi_cherrytrail_desc = {
	.machines = snd_soc_acpi_intel_cherrytrail_machines,
	.resindex_lpe_base = 0,
	.resindex_pcicfg_base = 1,
	.resindex_imr_base = 2,
	.irqindex_host_ipc = 5,
	.chip_info = &cht_chip_info,
	.nocodec_fw_filename = "intel/sof-cht.ri",
	.nocodec_tplg_filename = "intel/sof-cht-nocodec.tplg",
	.ops = &sof_cht_ops,
	.arch_ops = &sof_xtensa_arch_ops
};

#endif

static const struct dev_pm_ops sof_acpi_pm = {
	SET_SYSTEM_SLEEP_PM_OPS(snd_sof_suspend, snd_sof_resume)
	SET_RUNTIME_PM_OPS(snd_sof_runtime_suspend, snd_sof_runtime_resume,
			   NULL)
};

static int sof_acpi_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct sof_dev_desc *desc;
	struct snd_soc_acpi_mach *mach;
	struct snd_sof_pdata *sof_pdata;
	struct sof_platform_priv *priv;
	const struct snd_sof_dsp_ops *ops;
	int ret = 0;

	dev_dbg(&pdev->dev, "ACPI DSP detected");

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	sof_pdata = devm_kzalloc(dev, sizeof(*sof_pdata), GFP_KERNEL);
	if (!sof_pdata)
		return -ENOMEM;

	desc = (const struct sof_dev_desc *)device_get_match_data(dev);
	if (!desc)
		return -ENODEV;

#if IS_ENABLED(CONFIG_SND_SOC_SOF_BAYTRAIL)
	if (desc == &sof_acpi_baytrail_desc && is_byt_cr(dev))
		desc = &sof_acpi_baytrailcr_desc;
#endif

	/* get ops for platform */
	ops = desc->ops;
	if (!ops) {
		dev_err(dev, "error: no matching ACPI descriptor ops\n");
		return -ENODEV;
	}

#if IS_ENABLED(CONFIG_SND_SOC_SOF_FORCE_NOCODEC_MODE)
	/* force nocodec mode */
	dev_warn(dev, "Force to use nocodec mode\n");
	mach = devm_kzalloc(dev, sizeof(*mach), GFP_KERNEL);
	if (!mach)
		return -ENOMEM;
	ret = sof_nocodec_setup(dev, sof_pdata, mach, desc, ops);
	if (ret < 0)
		return ret;
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
			return ret;
#else
		dev_err(dev, "error: no matching ASoC machine driver found - aborting probe\n");
		return -ENODEV;
#endif
	}
#endif

	sof_pdata->machine = mach;
	sof_pdata->desc = desc;
	priv->sof_pdata = sof_pdata;
	sof_pdata->dev = &pdev->dev;
	sof_pdata->platform = "sof-audio";
	dev_set_drvdata(&pdev->dev, priv);

	/* register sof-audio platform driver */
	ret = sof_create_platform_device(priv);
	if (ret) {
		dev_err(dev, "error: failed to create platform device!\n");
		return ret;
	}

	/* allow runtime_pm */
	pm_runtime_set_autosuspend_delay(dev, SND_SOF_SUSPEND_DELAY_MS);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_allow(dev);

	return ret;
}

static int sof_acpi_remove(struct platform_device *pdev)
{
	struct sof_platform_priv *priv = dev_get_drvdata(&pdev->dev);
	struct snd_sof_pdata *sof_pdata = priv->sof_pdata;

	if (!IS_ERR_OR_NULL(priv->pdev_pcm))
		platform_device_unregister(priv->pdev_pcm);
	release_firmware(sof_pdata->fw);

	return 0;
}

static const struct acpi_device_id sof_acpi_match[] = {
#if IS_ENABLED(CONFIG_SND_SOC_SOF_HASWELL)
	{ "INT33C8", (unsigned long)&sof_acpi_haswell_desc },
#endif
#if IS_ENABLED(CONFIG_SND_SOC_SOF_BROADWELL)
	{ "INT3438", (unsigned long)&sof_acpi_broadwell_desc },
#endif
#if IS_ENABLED(CONFIG_SND_SOC_SOF_BAYTRAIL)
	{ "80860F28", (unsigned long)&sof_acpi_baytrail_desc },
	{ "808622A8", (unsigned long)&sof_acpi_cherrytrail_desc },
#endif
	{ }
};
MODULE_DEVICE_TABLE(acpi, sof_acpi_match);

/* acpi_driver definition */
static struct platform_driver snd_sof_acpi_driver = {
	.probe = sof_acpi_probe,
	.remove = sof_acpi_remove,
	.driver = {
		.name = "sof-audio-acpi",
		.pm = &sof_acpi_pm,
		.acpi_match_table = ACPI_PTR(sof_acpi_match),
	},
};
module_platform_driver(snd_sof_acpi_driver);

MODULE_LICENSE("Dual BSD/GPL");
