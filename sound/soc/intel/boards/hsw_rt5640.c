// SPDX-License-Identifier: GPL-2.0-only
/*
 * Sound card driver for Intel Haswell Lynx Point with Realtek 5640
 *
 * Copyright (C) 2013, Intel Corporation. All rights reserved.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-acpi.h>
#include "../../codecs/rt5640.h"

static const struct snd_soc_dapm_widget card_widgets[] = {
	SND_SOC_DAPM_HP("Headphones", NULL),
	SND_SOC_DAPM_MIC("Mic", NULL),
};

static const struct snd_soc_dapm_route card_routes[] = {
	{"Headphones", NULL, "HPOR"},
	{"Headphones", NULL, "HPOL"},
	{"IN2P", NULL, "Mic"},

	/* CODEC BE connections */
	{"SSP0 CODEC IN", NULL, "AIF1 Capture"},
	{"AIF1 Playback", NULL, "SSP0 CODEC OUT"},
};

static int codec_link_hw_params_fixup(struct snd_soc_pcm_runtime *rtd,
				      struct snd_pcm_hw_params *params)
{
	struct snd_interval *channels = hw_param_interval(params, SNDRV_PCM_HW_PARAM_CHANNELS);
	struct snd_interval *rate = hw_param_interval(params, SNDRV_PCM_HW_PARAM_RATE);

	/* The ADSP will convert the FE rate to 48k, stereo. */
	rate->min = rate->max = 48000;
	channels->min = channels->max = 2;
	/* Set SSP0 to 16 bit. */
	params_set_format(params, SNDRV_PCM_FORMAT_S16_LE);

	return 0;
}

static int codec_link_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct snd_soc_dai *codec_dai = asoc_rtd_to_codec(rtd, 0);
	int ret;

	ret = snd_soc_dai_set_sysclk(codec_dai, RT5640_SCLK_S_MCLK, 12288000, SND_SOC_CLOCK_IN);
	if (ret < 0) {
		dev_err(rtd->dev, "can't set codec sysclk configuration\n");
		return ret;
	}

	/* Set correct codec filter for DAI format and clock config. */
	snd_soc_component_update_bits(codec_dai->component, 0x83, 0xffff, 0x8000);

	return ret;
}

static const struct snd_soc_ops codec_link_ops = {
	.hw_params = codec_link_hw_params,
};

SND_SOC_DAILINK_DEF(system, DAILINK_COMP_ARRAY(COMP_CPU("System Pin")));
SND_SOC_DAILINK_DEF(offload0, DAILINK_COMP_ARRAY(COMP_CPU("Offload0 Pin")));
SND_SOC_DAILINK_DEF(offload1, DAILINK_COMP_ARRAY(COMP_CPU("Offload1 Pin")));
SND_SOC_DAILINK_DEF(loopback, DAILINK_COMP_ARRAY(COMP_CPU("Loopback Pin")));

SND_SOC_DAILINK_DEF(dummy, DAILINK_COMP_ARRAY(COMP_DUMMY()));
SND_SOC_DAILINK_DEF(codec, DAILINK_COMP_ARRAY(COMP_CODEC("i2c-INT33CA:00", "rt5640-aif1")));
SND_SOC_DAILINK_DEF(platform, DAILINK_COMP_ARRAY(COMP_PLATFORM("haswell-pcm-audio")));
SND_SOC_DAILINK_DEF(ssp0_port, DAILINK_COMP_ARRAY(COMP_CPU("ssp0-port")));

static struct snd_soc_dai_link card_dai_links[] = {
	/* Front End DAI links */
	{
		.name = "System",
		.stream_name = "System Playback/Capture",
		.nonatomic = 1,
		.dynamic = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST, SND_SOC_DPCM_TRIGGER_POST},
		.dpcm_playback = 1,
		.dpcm_capture = 1,
		SND_SOC_DAILINK_REG(system, dummy, platform),
	},
	{
		.name = "Offload0",
		.stream_name = "Offload0 Playback",
		.nonatomic = 1,
		.dynamic = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST, SND_SOC_DPCM_TRIGGER_POST},
		.dpcm_playback = 1,
		SND_SOC_DAILINK_REG(offload0, dummy, platform),
	},
	{
		.name = "Offload1",
		.stream_name = "Offload1 Playback",
		.nonatomic = 1,
		.dynamic = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST, SND_SOC_DPCM_TRIGGER_POST},
		.dpcm_playback = 1,
		SND_SOC_DAILINK_REG(offload1, dummy, platform),
	},
	{
		.name = "Loopback",
		.stream_name = "Loopback",
		.nonatomic = 1,
		.dynamic = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST, SND_SOC_DPCM_TRIGGER_POST},
		.dpcm_capture = 1,
		SND_SOC_DAILINK_REG(loopback, dummy, platform),
	},
	/* Back End DAI links */
	{
		/* SSP0 - Codec */
		.name = "Codec",
		.id = 0,
		.no_pcm = 1,
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBC_CFC,
		.ignore_pmdown_time = 1,
		.be_hw_params_fixup = codec_link_hw_params_fixup,
		.ops = &codec_link_ops,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
		SND_SOC_DAILINK_REG(ssp0_port, codec, platform),
	},
};

static struct snd_soc_card hsw_rt5640_card = {
	.name = "haswell-rt5640",
	.owner = THIS_MODULE,
	.dai_link = card_dai_links,
	.num_links = ARRAY_SIZE(card_dai_links),
	.dapm_widgets = card_widgets,
	.num_dapm_widgets = ARRAY_SIZE(card_widgets),
	.dapm_routes = card_routes,
	.num_dapm_routes = ARRAY_SIZE(card_routes),
	.fully_routed = true,
};

static int hsw_rt5640_probe(struct platform_device *pdev)
{
	struct snd_soc_acpi_mach *mach;
	int ret;

	hsw_rt5640_card.dev = &pdev->dev;
	mach = pdev->dev.platform_data;

	ret = snd_soc_fixup_dai_links_platform_name(&hsw_rt5640_card, mach->mach_params.platform);
	if (ret)
		return ret;

	return devm_snd_soc_register_card(&pdev->dev, &hsw_rt5640_card);
}

static struct platform_driver hsw_rt5640_driver = {
	.probe = hsw_rt5640_probe,
	.driver = {
		.name = "hsw_rt5640",
		.pm = &snd_soc_pm_ops,
	},
};

module_platform_driver(hsw_rt5640_driver)

MODULE_AUTHOR("Liam Girdwood, Xingchao Wang");
MODULE_DESCRIPTION("Sound card driver for Intel Haswell Lynx Point with Realtek 5640");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:hsw_rt5640");
