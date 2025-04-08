// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright(c) 2024-2025 Intel Corporation
//
// Author: Cezary Rojewski <cezary.rojewski@intel.com>
//

#include <linux/module.h>
#include <linux/platform_device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-acpi.h>
#include "../utils.h"

static const struct snd_soc_dapm_widget card_widgets[] = {
	SND_SOC_DAPM_HP("CPB Stereo HP 1", NULL),
	SND_SOC_DAPM_HP("CPB Stereo HP 2", NULL),
	SND_SOC_DAPM_HP("CPB Stereo HP 3", NULL),
	SND_SOC_DAPM_LINE("CPB Line Out", NULL),
	SND_SOC_DAPM_MIC("CPB Stereo Mic 1", NULL),
	SND_SOC_DAPM_MIC("CPB Stereo Mic 2", NULL),
	SND_SOC_DAPM_LINE("CPB Line In", NULL),
};

static const struct snd_soc_dapm_route card_routes[] = {
	{ "CPB Stereo HP 1", NULL, "AOUT1L" },
	{ "CPB Stereo HP 1", NULL, "AOUT1R" },
	{ "CPB Stereo HP 2", NULL, "AOUT2L" },
	{ "CPB Stereo HP 2", NULL, "AOUT2R" },
	{ "CPB Stereo HP 3", NULL, "AOUT3L" },
	{ "CPB Stereo HP 3", NULL, "AOUT3R" },
	{ "CPB Line Out", NULL, "AOUT4L" },
	{ "CPB Line Out", NULL, "AOUT4R" },

	{ "AIN1L", NULL, "CPB Stereo Mic 1" },
	{ "AIN1R", NULL, "CPB Stereo Mic 1" },
	{ "AIN2L", NULL, "CPB Stereo Mic 2" },
	{ "AIN2R", NULL, "CPB Stereo Mic 2" },
	{ "AIN3L", NULL, "CPB Line In" },
	{ "AIN3R", NULL, "CPB Line In" },
};

static int avs_pcm3168a_be_fixup(struct snd_soc_pcm_runtime *runtime,
				 struct snd_pcm_hw_params *params)
{
	struct snd_mask *fmt = hw_param_mask(params, SNDRV_PCM_HW_PARAM_FORMAT);

	/* Set SSP to 24 bit. */
	snd_mask_none(fmt);
	snd_mask_set_format(fmt, SNDRV_PCM_FORMAT_S24_LE);

	return 0;
}

SND_SOC_DAILINK_DEF(pcm3168a_dac,
		    DAILINK_COMP_ARRAY(COMP_CODEC("i2c-PCM3168A:00", "pcm3168a-dac")));
SND_SOC_DAILINK_DEF(pcm3168a_adc,
		    DAILINK_COMP_ARRAY(COMP_CODEC("i2c-PCM3168A:00", "pcm3168a-adc")));
SND_SOC_DAILINK_DEF(cpu_ssp0, DAILINK_COMP_ARRAY(COMP_CPU("SSP0 Pin")));
SND_SOC_DAILINK_DEF(cpu_ssp2, DAILINK_COMP_ARRAY(COMP_CPU("SSP2 Pin")));

static int avs_create_dai_links(struct device *dev, struct snd_soc_dai_link **links, int *num_links)
{
	struct snd_soc_dai_link_component *platform;
	struct snd_soc_dai_link *dl;
	const int num_dl = 2;

	dl = devm_kcalloc(dev, num_dl, sizeof(*dl), GFP_KERNEL);
	platform = devm_kzalloc(dev, sizeof(*platform), GFP_KERNEL);
	if (!dl || !platform)
		return -ENOMEM;

	platform->name = dev_name(dev);
	dl[0].num_cpus = 1;
	dl[0].num_codecs = 1;
	dl[0].platforms = platform;
	dl[0].num_platforms = 1;
	dl[0].dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBP_CFP;
	dl[0].be_hw_params_fixup = avs_pcm3168a_be_fixup;
	dl[0].nonatomic = 1;
	dl[0].no_pcm = 1;
	memcpy(&dl[1], &dl[0], sizeof(*dl));

	dl[0].name = "SSP0-Codec-dac";
	dl[0].cpus = cpu_ssp0;
	dl[0].codecs = pcm3168a_dac;
	dl[1].name = "SSP2-Codec-adc";
	dl[1].cpus = cpu_ssp2;
	dl[1].codecs = pcm3168a_adc;

	*links = dl;
	*num_links = num_dl;
	return 0;
}

static int avs_pcm3168a_probe(struct platform_device *pdev)
{
	struct snd_soc_acpi_mach *mach;
	struct avs_mach_pdata *pdata;
	struct device *dev = &pdev->dev;
	struct snd_soc_card *card;
	int ret;

	mach = dev_get_platdata(dev);
	pdata = mach->pdata;

	card = devm_kzalloc(dev, sizeof(*card), GFP_KERNEL);
	if (!card)
		return -ENOMEM;

	ret = avs_create_dai_links(dev, &card->dai_link, &card->num_links);
	if (ret)
		return ret;

	if (pdata->obsolete_card_names) {
		card->name = "avs_pcm3168a";
	} else {
		card->driver_name = "avs_pcm3168a";
		card->long_name = card->name = "AVS I2S PCM3168A";
	}
	card->dev = dev;
	card->owner = THIS_MODULE;
	card->dapm_widgets = card_widgets;
	card->num_dapm_widgets = ARRAY_SIZE(card_widgets);
	card->dapm_routes = card_routes;
	card->num_dapm_routes = ARRAY_SIZE(card_routes);
	card->fully_routed = true;

	return devm_snd_soc_register_deferrable_card(dev, card);
}

static const struct platform_device_id avs_pcm3168a_driver_ids[] = {
	{
		.name = "avs_pcm3168a",
	},
	{},
};
MODULE_DEVICE_TABLE(platform, avs_pcm3168a_driver_ids);

static struct platform_driver avs_pcm3168a_driver = {
	.probe = avs_pcm3168a_probe,
	.driver = {
		.name = "avs_pcm3168a",
		.pm = &snd_soc_pm_ops,
	},
	.id_table = avs_pcm3168a_driver_ids,
};

module_platform_driver(avs_pcm3168a_driver);

MODULE_DESCRIPTION("Intel pcm3168a machine driver");
MODULE_AUTHOR("Cezary Rojewski <cezary.rojewski@intel.com>");
MODULE_LICENSE("GPL");
