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
#include <linux/acpi.h>
#include <acpi/acpi_bus.h>
#include <asm/cpu_device_id.h>
#include <asm/iosf_mbi.h>
#include "sof-priv.h"

/* platform specific devices */
#include "intel/shim.h"
#include "intel/hda.h"

#if IS_ENABLED(CONFIG_SND_SOC_SOF_HASWELL)
static struct sof_dev_desc sof_acpi_haswell_desc = {
	.machines = snd_soc_acpi_intel_haswell_machines,
	.resindex_lpe_base = 0,
	.resindex_pcicfg_base = 1,
	.resindex_imr_base = -1,
	.irqindex_host_ipc = 0,
	.nocodec_fw_filename = "intel/sof-hsw.ri",
	.nocodec_tplg_filename = "intel/sof-hsw-nocodec.tplg"
};
#endif

#if IS_ENABLED(CONFIG_SND_SOC_SOF_BROADWELL)
static struct sof_dev_desc sof_acpi_broadwell_desc = {
	.machines = snd_soc_acpi_intel_broadwell_machines,
	.resindex_lpe_base = 0,
	.resindex_pcicfg_base = 1,
	.resindex_imr_base = -1,
	.irqindex_host_ipc = 0,
	.nocodec_fw_filename = "intel/sof-bdw.ri",
	.nocodec_tplg_filename = "intel/sof-bdw-nocodec.tplg"
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
	.nocodec_fw_filename = "intel/sof-byt.ri",
	.nocodec_tplg_filename = "intel/sof-byt-nocodec.tplg"
};

static struct sof_dev_desc sof_acpi_baytrail_desc = {
	.machines = snd_soc_acpi_intel_baytrail_machines,
	.resindex_lpe_base = 0,
	.resindex_pcicfg_base = 1,
	.resindex_imr_base = 2,
	.irqindex_host_ipc = 5,
	.nocodec_fw_filename = "intel/sof-byt.ri",
	.nocodec_tplg_filename = "intel/sof-byt-nocodec.tplg"
};

static int is_byt_cr(struct device *dev)
{
	u32 bios_status;
	int status;

	if (!IS_ENABLED(CONFIG_IOSF_MBI) || !iosf_mbi_available()) {
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

static struct sof_dev_desc sof_acpi_cherrytrail_desc = {
	.machines = snd_soc_acpi_intel_cherrytrail_machines,
	.resindex_lpe_base = 0,
	.resindex_pcicfg_base = 1,
	.resindex_imr_base = 2,
	.irqindex_host_ipc = 5,
	.nocodec_fw_filename = "intel/sof-cht.ri",
	.nocodec_tplg_filename = "intel/sof-cht-nocodec.tplg"
};
#endif

static struct platform_device *
	mfld_new_mach_data(struct snd_sof_pdata *sof_pdata)
{
	struct snd_soc_acpi_mach pmach;
	struct device *dev = sof_pdata->dev;
	const struct snd_soc_acpi_mach *mach = sof_pdata->machine;
	struct platform_device *pdev = NULL;

	memset(&pmach, 0, sizeof(pmach));
	memcpy((void *)pmach.id, mach->id, ACPI_ID_LEN);
	pmach.drv_name = mach->drv_name;

	pdev = platform_device_register_data(dev, mach->drv_name, -1,
					     &pmach, sizeof(pmach));
	return pdev;
}

static const struct dev_pm_ops sof_acpi_pm = {
	SET_SYSTEM_SLEEP_PM_OPS(snd_sof_suspend, snd_sof_resume)
	SET_RUNTIME_PM_OPS(snd_sof_runtime_suspend, snd_sof_runtime_resume,
			   NULL)
	.suspend_late = snd_sof_suspend_late,
};

static const struct sof_ops_table mach_ops[] = {
#if IS_ENABLED(CONFIG_SND_SOC_SOF_HASWELL)
	{&sof_acpi_haswell_desc, &sof_hsw_ops},
#endif
#if IS_ENABLED(CONFIG_SND_SOC_SOF_BROADWELL)
	{&sof_acpi_broadwell_desc, &sof_bdw_ops},
#endif
#if IS_ENABLED(CONFIG_SND_SOC_SOF_BAYTRAIL)
	{&sof_acpi_baytrail_desc, &sof_byt_ops, mfld_new_mach_data},
	{&sof_acpi_baytrailcr_desc, &sof_byt_ops, mfld_new_mach_data},
	{&sof_acpi_cherrytrail_desc, &sof_cht_ops, mfld_new_mach_data},
#endif
};

static struct snd_sof_dsp_ops *
	sof_acpi_get_ops(const struct sof_dev_desc *d,
			 struct platform_device *(**new_mach_data)
			 (struct snd_sof_pdata *))
{
	int i;

	for (i = 0; i < ARRAY_SIZE(mach_ops); i++) {
		if (d == mach_ops[i].desc) {
			*new_mach_data = mach_ops[i].new_data;
			return mach_ops[i].ops;
		}
	}

	/* not found */
	return NULL;
}

static int sof_acpi_probe(struct platform_device *pdev)
{
	const struct acpi_device_id *id;
	struct device *dev = &pdev->dev;
	const struct sof_dev_desc *desc;
	struct snd_soc_acpi_mach *mach;
	struct snd_sof_pdata *sof_pdata;
	struct sof_platform_priv *priv;
	struct snd_sof_dsp_ops *ops;
	struct platform_device *(*new_mach_data)(struct snd_sof_pdata *pdata);
	int ret = 0;

	dev_dbg(&pdev->dev, "ACPI DSP detected");

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	sof_pdata = devm_kzalloc(dev, sizeof(*sof_pdata), GFP_KERNEL);
	if (!sof_pdata)
		return -ENOMEM;

	id = acpi_match_device(dev->driver->acpi_match_table, dev);
	if (!id)
		return -ENODEV;

	desc = (const struct sof_dev_desc *)id->driver_data;
#if IS_ENABLED(CONFIG_SND_SOC_SOF_BAYTRAIL)
	if (desc == &sof_acpi_baytrail_desc && is_byt_cr(dev))
		desc = &sof_acpi_baytrailcr_desc;
#endif

	/* get ops for platform */
	new_mach_data = NULL;
	ops = sof_acpi_get_ops(desc, &new_mach_data);
	if (!ops) {
		dev_err(dev, "error: no matching ACPI descriptor ops\n");
		return -ENODEV;
	}

#if IS_ENABLED(CONFIG_SND_SOC_SOF_FORCE_NOCODEC_MODE)
	/* force nocodec mode */
	dev_warn(dev, "Force to use nocodec mode\n");
	mach = devm_kzalloc(dev, sizeof(*mach), GFP_KERNEL);
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
		dev_warn(dev, "No matching ASoC machine driver found - falling back to HDA codec\n");
		mach = snd_soc_acpi_intel_hda_machines;
		mach->sof_fw_filename = desc->nocodec_fw_filename;
#endif
	}
#endif

	mach->pdata = ops;
	mach->new_mach_data = (struct platform_device *
				(*)(void *pdata)) new_mach_data;

	sof_pdata->machine = mach;
	/*
	 * FIXME, this can't work for baytrail cr:
	 * sof_pdata->desc = (struct sof_dev_desc*) id->driver_data;
	 */
	sof_pdata->desc = desc;
	priv->sof_pdata = sof_pdata;
	sof_pdata->dev = &pdev->dev;
	sof_pdata->type = SOF_DEVICE_APCI;
	sof_pdata->platform = "sof-audio";
	dev_set_drvdata(&pdev->dev, priv);

	/* do we need to generate any machine plat data ? */
	if (mach->new_mach_data) {
		sof_pdata->pdev_mach = mach->new_mach_data(sof_pdata);

		if (IS_ERR(sof_pdata->pdev_mach))
			return PTR_ERR(sof_pdata->pdev_mach);
		dev_dbg(dev, "created machine %s\n",
			dev_name(&sof_pdata->pdev_mach->dev));
	}

	/* register sof-audio platform driver */
	ret = sof_create_platform_device(priv);
	if (ret) {
		platform_device_unregister(sof_pdata->pdev_mach);
		dev_err(dev, "error: failed to create platform device!\n");
		return ret;
	}

	/* allow runtime_pm */
	pm_runtime_set_autosuspend_delay(dev, SND_SOF_SUSPEND_DELAY);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_allow(dev);

	return ret;
}

static void sof_acpi_shutdown(struct platform_device *pdev)
{
	snd_sof_shutdown(&pdev->dev);
}

static int sof_acpi_remove(struct platform_device *pdev)
{
	struct sof_platform_priv *priv = dev_get_drvdata(&pdev->dev);
	struct snd_sof_pdata *sof_pdata = priv->sof_pdata;

	platform_device_unregister(sof_pdata->pdev_mach);
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
	.shutdown = sof_acpi_shutdown,
	.driver = {
		.name = "sof-audio-acpi",
		.pm = &sof_acpi_pm,
		.acpi_match_table = ACPI_PTR(sof_acpi_match),
	},
};
module_platform_driver(snd_sof_acpi_driver);

MODULE_LICENSE("Dual BSD/GPL");
