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
 * Machine Driver Legacy Support for ACP HW block
 */

#include <sound/core.h>
#include <sound/pcm_params.h>
#include <sound/soc-acpi.h>
#include <sound/soc-dapm.h>
#include <linux/dmi.h>
#include <linux/module.h>

#include "acp-mach.h"
#include "acp3x-es83xx/acp3x-es83xx.h"

static struct acp_card_drvdata rt5682_rt1019_data = {
	.hs_cpu_id = I2S_SP,
	.amp_cpu_id = I2S_SP,
	.dmic_cpu_id = DMIC,
	.hs_codec_id = RT5682,
	.amp_codec_id = RT1019,
	.dmic_codec_id = DMIC,
	.tdm_mode = false,
};

static struct acp_card_drvdata rt5682s_max_data = {
	.hs_cpu_id = I2S_SP,
	.amp_cpu_id = I2S_SP,
	.dmic_cpu_id = DMIC,
	.hs_codec_id = RT5682S,
	.amp_codec_id = MAX98360A,
	.dmic_codec_id = DMIC,
	.tdm_mode = false,
};

static struct acp_card_drvdata rt5682s_rt1019_data = {
	.hs_cpu_id = I2S_SP,
	.amp_cpu_id = I2S_SP,
	.dmic_cpu_id = DMIC,
	.hs_codec_id = RT5682S,
	.amp_codec_id = RT1019,
	.dmic_codec_id = DMIC,
	.tdm_mode = false,
};

static struct acp_card_drvdata es83xx_rn_data = {
	.hs_cpu_id = I2S_SP,
	.dmic_cpu_id = DMIC,
	.hs_codec_id = ES83XX,
	.dmic_codec_id = DMIC,
	.platform = RENOIR,
};

static struct acp_card_drvdata max_nau8825_data = {
	.hs_cpu_id = I2S_HS,
	.amp_cpu_id = I2S_HS,
	.dmic_cpu_id = DMIC,
	.hs_codec_id = NAU8825,
	.amp_codec_id = MAX98360A,
	.dmic_codec_id = DMIC,
	.soc_mclk = true,
	.platform = REMBRANDT,
	.tdm_mode = false,
};

static struct acp_card_drvdata rt5682s_rt1019_rmb_data = {
	.hs_cpu_id = I2S_HS,
	.amp_cpu_id = I2S_HS,
	.dmic_cpu_id = DMIC,
	.hs_codec_id = RT5682S,
	.amp_codec_id = RT1019,
	.dmic_codec_id = DMIC,
	.soc_mclk = true,
	.platform = REMBRANDT,
	.tdm_mode = false,
};

static struct acp_card_drvdata acp_dmic_data = {
	.dmic_cpu_id = DMIC,
	.dmic_codec_id = DMIC,
};

static bool acp_asoc_init_ops(struct acp_card_drvdata *priv)
{
	bool has_ops = false;

	if (priv->hs_codec_id == ES83XX) {
		has_ops = true;
		acp3x_es83xx_init_ops(&priv->ops);
	}
	return has_ops;
}

static int acp_asoc_suspend_pre(struct snd_soc_card *card)
{
	int ret;

	ret = acp_ops_suspend_pre(card);
	if (ret == 1)
		return 0;
	else
		return ret;
}

static int acp_asoc_resume_post(struct snd_soc_card *card)
{
	int ret;

	ret = acp_ops_resume_post(card);
	if (ret == 1)
		return 0;
	else
		return ret;
}

static int acp_asoc_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = NULL;
	struct device *dev = &pdev->dev;
	const struct dmi_system_id *dmi_id;
	struct acp_card_drvdata *acp_card_drvdata;
	int ret;

	if (!pdev->id_entry) {
		ret = -EINVAL;
		goto out;
	}

	card = devm_kzalloc(dev, sizeof(*card), GFP_KERNEL);
	if (!card) {
		ret = -ENOMEM;
		goto out;
	}

	card->drvdata = (struct acp_card_drvdata *)pdev->id_entry->driver_data;
	acp_card_drvdata = card->drvdata;
	acp_card_drvdata->acpi_mach = (struct snd_soc_acpi_mach *)pdev->dev.platform_data;
	card->dev = dev;
	card->owner = THIS_MODULE;
	card->name = pdev->id_entry->name;

	acp_asoc_init_ops(card->drvdata);

	/* If widgets and controls are not set in specific callback,
	 * they will be added per-codec in acp-mach-common.c
	 */
	ret = acp_ops_configure_widgets(card);
	if (ret < 0) {
		dev_err(&pdev->dev,
			"Cannot configure widgets for card (%s): %d\n",
			card->name, ret);
		goto out;
	}
	card->suspend_pre = acp_asoc_suspend_pre;
	card->resume_post = acp_asoc_resume_post;

	ret = acp_ops_probe(card);
	if (ret < 0) {
		dev_err(&pdev->dev,
			"Cannot probe card (%s): %d\n",
			card->name, ret);
		goto out;
	}
	if (!strcmp(pdev->name, "acp-pdm-mach"))
		acp_card_drvdata->platform =  *((int *)dev->platform_data);

	dmi_id = dmi_first_match(acp_quirk_table);
	if (dmi_id && dmi_id->driver_data)
		acp_card_drvdata->tdm_mode = dmi_id->driver_data;

	ret = acp_legacy_dai_links_create(card);
	if (ret) {
		dev_err(&pdev->dev,
			"Cannot create dai links for card (%s): %d\n",
			card->name, ret);
		goto out;
	}

	ret = devm_snd_soc_register_card(&pdev->dev, card);
	if (ret) {
		dev_err(&pdev->dev,
				"devm_snd_soc_register_card(%s) failed: %d\n",
				card->name, ret);
		goto out;
	}
out:
	return ret;
}

static const struct platform_device_id board_ids[] = {
	{
		.name = "acp3xalc56821019",
		.driver_data = (kernel_ulong_t)&rt5682_rt1019_data,
	},
	{
		.name = "acp3xalc5682sm98360",
		.driver_data = (kernel_ulong_t)&rt5682s_max_data,
	},
	{
		.name = "acp3xalc5682s1019",
		.driver_data = (kernel_ulong_t)&rt5682s_rt1019_data,
	},
	{
		.name = "acp3x-es83xx",
		.driver_data = (kernel_ulong_t)&es83xx_rn_data,
	},
	{
		.name = "rmb-nau8825-max",
		.driver_data = (kernel_ulong_t)&max_nau8825_data,
	},
	{
		.name = "rmb-rt5682s-rt1019",
		.driver_data = (kernel_ulong_t)&rt5682s_rt1019_rmb_data,
	},
	{
		.name = "acp-pdm-mach",
		.driver_data = (kernel_ulong_t)&acp_dmic_data,
	},
	{ }
};
static struct platform_driver acp_asoc_audio = {
	.driver = {
		.pm = &snd_soc_pm_ops,
		.name = "acp_mach",
	},
	.probe = acp_asoc_probe,
	.id_table = board_ids,
};

module_platform_driver(acp_asoc_audio);

MODULE_IMPORT_NS(SND_SOC_AMD_MACH);
MODULE_DESCRIPTION("ACP chrome audio support");
MODULE_ALIAS("platform:acp3xalc56821019");
MODULE_ALIAS("platform:acp3xalc5682sm98360");
MODULE_ALIAS("platform:acp3xalc5682s1019");
MODULE_ALIAS("platform:acp3x-es83xx");
MODULE_ALIAS("platform:rmb-nau8825-max");
MODULE_ALIAS("platform:rmb-rt5682s-rt1019");
MODULE_ALIAS("platform:acp-pdm-mach");
MODULE_LICENSE("GPL v2");
