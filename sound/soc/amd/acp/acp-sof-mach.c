// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license. When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2021 Advanced Micro Devices, Inc.
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

static const struct snd_kcontrol_new acp_controls[] = {
	SOC_DAPM_PIN_SWITCH("Headphone Jack"),
	SOC_DAPM_PIN_SWITCH("Headset Mic"),
	SOC_DAPM_PIN_SWITCH("Spk"),
	SOC_DAPM_PIN_SWITCH("Left Spk"),
	SOC_DAPM_PIN_SWITCH("Right Spk"),
};

static const struct snd_soc_dapm_widget acp_widgets[] = {
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
	SND_SOC_DAPM_SPK("Spk", NULL),
	SND_SOC_DAPM_SPK("Left Spk", NULL),
	SND_SOC_DAPM_SPK("Right Spk", NULL),
};

static int acp_sof_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = NULL;
	struct device *dev = &pdev->dev;
	int ret;

	if (!pdev->id_entry)
		return -EINVAL;

	card = devm_kzalloc(dev, sizeof(*card), GFP_KERNEL);
	if (!card)
		return -ENOMEM;

	card->dev = dev;
	card->owner = THIS_MODULE;
	card->name = pdev->id_entry->name;
	card->dapm_widgets = acp_widgets;
	card->num_dapm_widgets = ARRAY_SIZE(acp_widgets);
	card->controls = acp_controls;
	card->num_controls = ARRAY_SIZE(acp_controls);
	card->drvdata = (struct acp_card_drvdata *)pdev->id_entry->driver_data;

	acp_sofdsp_dai_links_create(card);

	ret = devm_snd_soc_register_card(&pdev->dev, card);
	if (ret) {
		dev_err(&pdev->dev,
				"devm_snd_soc_register_card(%s) failed: %d\n",
				card->name, ret);
		return ret;
	}

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
	{ }
};
static struct platform_driver acp_asoc_audio = {
	.driver = {
		.name = "sof_mach",
		.pm = &snd_soc_pm_ops,
	},
	.probe = acp_sof_probe,
	.id_table = board_ids,
};

module_platform_driver(acp_asoc_audio);

MODULE_IMPORT_NS(SND_SOC_AMD_MACH);
MODULE_DESCRIPTION("ACP chrome SOF audio support");
MODULE_ALIAS("platform:rt5682-rt1019");
MODULE_ALIAS("platform:rt5682-max");
MODULE_ALIAS("platform:rt5682s-max");
MODULE_ALIAS("platform:rt5682s-rt1019");
MODULE_ALIAS("platform:nau8825-max");
MODULE_LICENSE("GPL v2");
