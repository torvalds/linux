// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license. When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2021, 2023 Advanced Micro Devices, Inc.
//
// Authors: Ajit Kumar Pandey <AjitKumar.Pandey@amd.com>
//

/*
 * SOF Machine Driver Support for ACP HW block
 */

#include <sound/core.h>
#include <sound/pcm_params.h>
#include <sound/soc-acpi.h>
#include <sound/soc-dapm.h>
#include <linux/dmi.h>
#include <linux/module.h>

#include "acp-mach.h"

static struct acp_card_drvdata sof_rt5682_rt1019_data = {
	.hs_cpu_id = I2S_SP,
	.amp_cpu_id = I2S_SP,
	.dmic_cpu_id = DMIC,
	.hs_codec_id = RT5682,
	.amp_codec_id = RT1019,
	.dmic_codec_id = DMIC,
};

static struct acp_card_drvdata sof_rt5682_max_data = {
	.hs_cpu_id = I2S_SP,
	.amp_cpu_id = I2S_SP,
	.dmic_cpu_id = DMIC,
	.hs_codec_id = RT5682,
	.amp_codec_id = MAX98360A,
	.dmic_codec_id = DMIC,
};

static struct acp_card_drvdata sof_rt5682s_rt1019_data = {
	.hs_cpu_id = I2S_SP,
	.amp_cpu_id = I2S_SP,
	.dmic_cpu_id = DMIC,
	.hs_codec_id = RT5682S,
	.amp_codec_id = RT1019,
	.dmic_codec_id = DMIC,
};

static struct acp_card_drvdata sof_rt5682s_max_data = {
	.hs_cpu_id = I2S_SP,
	.amp_cpu_id = I2S_SP,
	.dmic_cpu_id = DMIC,
	.hs_codec_id = RT5682S,
	.amp_codec_id = MAX98360A,
	.dmic_codec_id = DMIC,
};

static struct acp_card_drvdata sof_nau8825_data = {
	.hs_cpu_id = I2S_HS,
	.amp_cpu_id = I2S_HS,
	.dmic_cpu_id = DMIC,
	.hs_codec_id = NAU8825,
	.amp_codec_id = MAX98360A,
	.dmic_codec_id = DMIC,
	.soc_mclk = true,
};

static struct acp_card_drvdata sof_rt5682s_hs_rt1019_data = {
	.hs_cpu_id = I2S_HS,
	.amp_cpu_id = I2S_HS,
	.dmic_cpu_id = DMIC,
	.hs_codec_id = RT5682S,
	.amp_codec_id = RT1019,
	.dmic_codec_id = DMIC,
	.soc_mclk = true,
};

static struct acp_card_drvdata sof_nau8821_max98388_data = {
	.hs_cpu_id = I2S_SP,
	.amp_cpu_id = I2S_HS,
	.bt_cpu_id = I2S_BT,
	.hs_codec_id = NAU8821,
	.amp_codec_id = MAX98388,
	.soc_mclk = true,
};

static int acp_sof_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = NULL;
	struct device *dev = &pdev->dev;
	struct snd_soc_acpi_mach *mach = dev_get_platdata(&pdev->dev);
	const struct dmi_system_id *dmi_id;
	struct acp_card_drvdata *acp_card_drvdata;
	int ret;

	if (!pdev->id_entry)
		return -EINVAL;

	card = devm_kzalloc(dev, sizeof(*card), GFP_KERNEL);
	if (!card)
		return -ENOMEM;

	card->dev = dev;
	card->owner = THIS_MODULE;
	card->name = pdev->id_entry->name;
	card->drvdata = (struct acp_card_drvdata *)pdev->id_entry->driver_data;
	/* Widgets and controls added per-codec in acp-mach-common.c */

	acp_card_drvdata = card->drvdata;
	dmi_id = dmi_first_match(acp_quirk_table);
	if (dmi_id && dmi_id->driver_data)
		acp_card_drvdata->tdm_mode = dmi_id->driver_data;

	acp_card_drvdata->acp_rev = mach->mach_params.subsystem_rev;
	ret = acp_sofdsp_dai_links_create(card);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "Failed to create DAI links\n");

	ret = devm_snd_soc_register_card(&pdev->dev, card);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "Failed to register card(%s)\n", card->name);
	return 0;
}

static const struct platform_device_id board_ids[] = {
	{
		.name = "rt5682-rt1019",
		.driver_data = (kernel_ulong_t)&sof_rt5682_rt1019_data
	},
	{
		.name = "rt5682-max",
		.driver_data = (kernel_ulong_t)&sof_rt5682_max_data
	},
	{
		.name = "rt5682s-max",
		.driver_data = (kernel_ulong_t)&sof_rt5682s_max_data
	},
	{
		.name = "rt5682s-rt1019",
		.driver_data = (kernel_ulong_t)&sof_rt5682s_rt1019_data
	},
	{
		.name = "nau8825-max",
		.driver_data = (kernel_ulong_t)&sof_nau8825_data
	},
	{
		.name = "rt5682s-hs-rt1019",
		.driver_data = (kernel_ulong_t)&sof_rt5682s_hs_rt1019_data
	},
	{
		.name = "nau8821-max",
		.driver_data = (kernel_ulong_t)&sof_nau8821_max98388_data
	},
	{ }
};
MODULE_DEVICE_TABLE(platform, board_ids);

static struct platform_driver acp_asoc_audio = {
	.driver = {
		.name = "sof_mach",
		.pm = &snd_soc_pm_ops,
	},
	.probe = acp_sof_probe,
	.id_table = board_ids,
};

module_platform_driver(acp_asoc_audio);

MODULE_IMPORT_NS("SND_SOC_AMD_MACH");
MODULE_DESCRIPTION("ACP SOF Machine Driver");
MODULE_LICENSE("GPL v2");
