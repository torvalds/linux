// SPDX-License-Identifier: GPL-2.0-only
/*
 * Intel Haswell Lynxpoint SST Audio
 *
 * Copyright (C) 2013, Intel Corporation. All rights reserved.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/soc-acpi.h>
#include <sound/pcm_params.h>

#include "../common/sst-dsp.h"
#include "../haswell/sst-haswell-ipc.h"

#include "../../codecs/rt5640.h"

/* Haswell ULT platforms have a Headphone and Mic jack */
static const struct snd_soc_dapm_widget haswell_widgets[] = {
	SND_SOC_DAPM_HP("Headphones", NULL),
	SND_SOC_DAPM_MIC("Mic", NULL),
};

static const struct snd_soc_dapm_route haswell_rt5640_map[] = {

	{"Headphones", NULL, "HPOR"},
	{"Headphones", NULL, "HPOL"},
	{"IN2P", NULL, "Mic"},

	/* CODEC BE connections */
	{"SSP0 CODEC IN", NULL, "AIF1 Capture"},
	{"AIF1 Playback", NULL, "SSP0 CODEC OUT"},
};

static int haswell_ssp0_fixup(struct snd_soc_pcm_runtime *rtd,
			struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate = hw_param_interval(params,
			SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval *channels = hw_param_interval(params,
						SNDRV_PCM_HW_PARAM_CHANNELS);

	/* The ADSP will covert the FE rate to 48k, stereo */
	rate->min = rate->max = 48000;
	channels->min = channels->max = 2;

	/* set SSP0 to 16 bit */
	params_set_format(params, SNDRV_PCM_FORMAT_S16_LE);
	return 0;
}

static int haswell_rt5640_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	int ret;

	ret = snd_soc_dai_set_sysclk(codec_dai, RT5640_SCLK_S_MCLK, 12288000,
		SND_SOC_CLOCK_IN);

	if (ret < 0) {
		dev_err(rtd->dev, "can't set codec sysclk configuration\n");
		return ret;
	}

	/* set correct codec filter for DAI format and clock config */
	snd_soc_component_update_bits(codec_dai->component, 0x83, 0xffff, 0x8000);

	return ret;
}

static const struct snd_soc_ops haswell_rt5640_ops = {
	.hw_params = haswell_rt5640_hw_params,
};

static int haswell_rtd_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_component *component = snd_soc_rtdcom_lookup(rtd, DRV_NAME);
	struct sst_pdata *pdata = dev_get_platdata(component->dev);
	struct sst_hsw *haswell = pdata->dsp;
	int ret;

	/* Set ADSP SSP port settings */
	ret = sst_hsw_device_set_config(haswell, SST_HSW_DEVICE_SSP_0,
		SST_HSW_DEVICE_MCLK_FREQ_24_MHZ,
		SST_HSW_DEVICE_CLOCK_MASTER, 9);
	if (ret < 0) {
		dev_err(rtd->dev, "failed to set device config\n");
		return ret;
	}

	return 0;
}

SND_SOC_DAILINK_DEF(dummy,
	DAILINK_COMP_ARRAY(COMP_DUMMY()));

SND_SOC_DAILINK_DEF(system,
	DAILINK_COMP_ARRAY(COMP_CPU("System Pin")));

SND_SOC_DAILINK_DEF(offload0,
	DAILINK_COMP_ARRAY(COMP_CPU("Offload0 Pin")));

SND_SOC_DAILINK_DEF(offload1,
	DAILINK_COMP_ARRAY(COMP_CPU("Offload1 Pin")));

SND_SOC_DAILINK_DEF(loopback,
	DAILINK_COMP_ARRAY(COMP_CPU("Loopback Pin")));

SND_SOC_DAILINK_DEF(codec,
	DAILINK_COMP_ARRAY(COMP_CODEC("i2c-INT33CA:00", "rt5640-aif1")));

SND_SOC_DAILINK_DEF(platform,
	DAILINK_COMP_ARRAY(COMP_PLATFORM("haswell-pcm-audio")));

static struct snd_soc_dai_link haswell_rt5640_dais[] = {
	/* Front End DAI links */
	{
		.name = "System",
		.stream_name = "System Playback/Capture",
		.dynamic = 1,
		.init = haswell_rtd_init,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST, SND_SOC_DPCM_TRIGGER_POST},
		.dpcm_playback = 1,
		.dpcm_capture = 1,
		SND_SOC_DAILINK_REG(system, dummy, platform),
	},
	{
		.name = "Offload0",
		.stream_name = "Offload0 Playback",
		.dynamic = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST, SND_SOC_DPCM_TRIGGER_POST},
		.dpcm_playback = 1,
		SND_SOC_DAILINK_REG(offload0, dummy, platform),
	},
	{
		.name = "Offload1",
		.stream_name = "Offload1 Playback",
		.dynamic = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST, SND_SOC_DPCM_TRIGGER_POST},
		.dpcm_playback = 1,
		SND_SOC_DAILINK_REG(offload1, dummy, platform),
	},
	{
		.name = "Loopback",
		.stream_name = "Loopback",
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
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
			SND_SOC_DAIFMT_CBS_CFS,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.be_hw_params_fixup = haswell_ssp0_fixup,
		.ops = &haswell_rt5640_ops,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
		SND_SOC_DAILINK_REG(dummy, codec, dummy),
	},
};

/* audio machine driver for Haswell Lynxpoint DSP + RT5640 */
static struct snd_soc_card haswell_rt5640 = {
	.name = "haswell-rt5640",
	.owner = THIS_MODULE,
	.dai_link = haswell_rt5640_dais,
	.num_links = ARRAY_SIZE(haswell_rt5640_dais),
	.dapm_widgets = haswell_widgets,
	.num_dapm_widgets = ARRAY_SIZE(haswell_widgets),
	.dapm_routes = haswell_rt5640_map,
	.num_dapm_routes = ARRAY_SIZE(haswell_rt5640_map),
	.fully_routed = true,
};

static int haswell_audio_probe(struct platform_device *pdev)
{
	struct snd_soc_acpi_mach *mach;
	const char *platform_name = NULL;
	int ret;

	haswell_rt5640.dev = &pdev->dev;

	/* override plaform name, if required */
	mach = (&pdev->dev)->platform_data;
	if (mach) /* extra check since legacy does not pass parameters */
		platform_name = mach->mach_params.platform;

	ret = snd_soc_fixup_dai_links_platform_name(&haswell_rt5640,
						    platform_name);
	if (ret)
		return ret;

	return devm_snd_soc_register_card(&pdev->dev, &haswell_rt5640);
}

static struct platform_driver haswell_audio = {
	.probe = haswell_audio_probe,
	.driver = {
		.name = "haswell-audio",
	},
};

module_platform_driver(haswell_audio)

/* Module information */
MODULE_AUTHOR("Liam Girdwood, Xingchao Wang");
MODULE_DESCRIPTION("Intel SST Audio for Haswell Lynxpoint");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:haswell-audio");
