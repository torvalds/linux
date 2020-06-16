// SPDX-License-Identifier: GPL-2.0-only
/*
 * Intel Broadwell Wildcatpoint SST Audio
 *
 * Copyright (C) 2013, Intel Corporation. All rights reserved.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/jack.h>
#include <sound/pcm_params.h>
#include <sound/soc-acpi.h>

#include "../common/sst-dsp.h"
#include "../haswell/sst-haswell-ipc.h"

#include "../../codecs/rt286.h"

static struct snd_soc_jack broadwell_headset;
/* Headset jack detection DAPM pins */
static struct snd_soc_jack_pin broadwell_headset_pins[] = {
	{
		.pin = "Mic Jack",
		.mask = SND_JACK_MICROPHONE,
	},
	{
		.pin = "Headphone Jack",
		.mask = SND_JACK_HEADPHONE,
	},
};

static const struct snd_kcontrol_new broadwell_controls[] = {
	SOC_DAPM_PIN_SWITCH("Speaker"),
	SOC_DAPM_PIN_SWITCH("Headphone Jack"),
};

static const struct snd_soc_dapm_widget broadwell_widgets[] = {
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_SPK("Speaker", NULL),
	SND_SOC_DAPM_MIC("Mic Jack", NULL),
	SND_SOC_DAPM_MIC("DMIC1", NULL),
	SND_SOC_DAPM_MIC("DMIC2", NULL),
	SND_SOC_DAPM_LINE("Line Jack", NULL),
};

static const struct snd_soc_dapm_route broadwell_rt286_map[] = {

	/* speaker */
	{"Speaker", NULL, "SPOR"},
	{"Speaker", NULL, "SPOL"},

	/* HP jack connectors - unknown if we have jack deteck */
	{"Headphone Jack", NULL, "HPO Pin"},

	/* other jacks */
	{"MIC1", NULL, "Mic Jack"},
	{"LINE1", NULL, "Line Jack"},

	/* digital mics */
	{"DMIC1 Pin", NULL, "DMIC1"},
	{"DMIC2 Pin", NULL, "DMIC2"},

	/* CODEC BE connections */
	{"SSP0 CODEC IN", NULL, "AIF1 Capture"},
	{"AIF1 Playback", NULL, "SSP0 CODEC OUT"},
};

static int broadwell_rt286_codec_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_component *component = asoc_rtd_to_codec(rtd, 0)->component;
	int ret = 0;
	ret = snd_soc_card_jack_new(rtd->card, "Headset",
		SND_JACK_HEADSET | SND_JACK_BTN_0, &broadwell_headset,
		broadwell_headset_pins, ARRAY_SIZE(broadwell_headset_pins));
	if (ret)
		return ret;

	rt286_mic_detect(component, &broadwell_headset);
	return 0;
}


static int broadwell_ssp0_fixup(struct snd_soc_pcm_runtime *rtd,
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

static int broadwell_rt286_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = asoc_rtd_to_codec(rtd, 0);
	int ret;

	ret = snd_soc_dai_set_sysclk(codec_dai, RT286_SCLK_S_PLL, 24000000,
		SND_SOC_CLOCK_IN);

	if (ret < 0) {
		dev_err(rtd->dev, "can't set codec sysclk configuration\n");
		return ret;
	}

	return ret;
}

static const struct snd_soc_ops broadwell_rt286_ops = {
	.hw_params = broadwell_rt286_hw_params,
};

#if !IS_ENABLED(CONFIG_SND_SOC_SOF_BROADWELL)
static int broadwell_rtd_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_component *component = snd_soc_rtdcom_lookup(rtd, DRV_NAME);
	struct sst_pdata *pdata = dev_get_platdata(component->dev);
	struct sst_hsw *broadwell = pdata->dsp;
	int ret;

	/* Set ADSP SSP port settings */
	ret = sst_hsw_device_set_config(broadwell, SST_HSW_DEVICE_SSP_0,
		SST_HSW_DEVICE_MCLK_FREQ_24_MHZ,
		SST_HSW_DEVICE_CLOCK_MASTER, 9);
	if (ret < 0) {
		dev_err(rtd->dev, "error: failed to set device config\n");
		return ret;
	}

	return 0;
}
#endif

static const unsigned int channels[] = {
	2,
};

static const struct snd_pcm_hw_constraint_list constraints_channels = {
	.count = ARRAY_SIZE(channels),
	.list = channels,
	.mask = 0,
};

static int broadwell_fe_startup(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;

	/* Board supports stereo configuration only */
	runtime->hw.channels_max = 2;
	return snd_pcm_hw_constraint_list(runtime, 0,
					  SNDRV_PCM_HW_PARAM_CHANNELS,
					  &constraints_channels);
}

static const struct snd_soc_ops broadwell_fe_ops = {
	.startup = broadwell_fe_startup,
};

SND_SOC_DAILINK_DEF(system,
	DAILINK_COMP_ARRAY(COMP_CPU("System Pin")));

SND_SOC_DAILINK_DEF(offload0,
	DAILINK_COMP_ARRAY(COMP_CPU("Offload0 Pin")));

SND_SOC_DAILINK_DEF(offload1,
	DAILINK_COMP_ARRAY(COMP_CPU("Offload1 Pin")));

SND_SOC_DAILINK_DEF(loopback,
	DAILINK_COMP_ARRAY(COMP_CPU("Loopback Pin")));

SND_SOC_DAILINK_DEF(dummy,
	DAILINK_COMP_ARRAY(COMP_DUMMY()));

SND_SOC_DAILINK_DEF(platform,
	DAILINK_COMP_ARRAY(COMP_PLATFORM("haswell-pcm-audio")));

SND_SOC_DAILINK_DEF(codec,
	DAILINK_COMP_ARRAY(COMP_CODEC("i2c-INT343A:00", "rt286-aif1")));

#if IS_ENABLED(CONFIG_SND_SOC_SOF_BROADWELL)
SND_SOC_DAILINK_DEF(ssp0_port,
	    DAILINK_COMP_ARRAY(COMP_CPU("ssp0-port")));
#endif

/* broadwell digital audio interface glue - connects codec <--> CPU */
static struct snd_soc_dai_link broadwell_rt286_dais[] = {
	/* Front End DAI links */
	{
		.name = "System PCM",
		.stream_name = "System Playback/Capture",
		.dynamic = 1,
#if !IS_ENABLED(CONFIG_SND_SOC_SOF_BROADWELL)
		.init = broadwell_rtd_init,
#endif
		.trigger = {SND_SOC_DPCM_TRIGGER_POST, SND_SOC_DPCM_TRIGGER_POST},
		.ops = &broadwell_fe_ops,
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
		.name = "Loopback PCM",
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
		.init = broadwell_rt286_codec_init,
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
			SND_SOC_DAIFMT_CBS_CFS,
		.ignore_pmdown_time = 1,
		.be_hw_params_fixup = broadwell_ssp0_fixup,
		.ops = &broadwell_rt286_ops,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
#if !IS_ENABLED(CONFIG_SND_SOC_SOF_BROADWELL)
		SND_SOC_DAILINK_REG(dummy, codec, dummy),
#else
		SND_SOC_DAILINK_REG(ssp0_port, codec, platform),
#endif
	},
};

static int broadwell_disable_jack(struct snd_soc_card *card)
{
	struct snd_soc_component *component;

	for_each_card_components(card, component) {
		if (!strcmp(component->name, "i2c-INT343A:00")) {

			dev_dbg(component->dev, "disabling jack detect before going to suspend.\n");
			rt286_mic_detect(component, NULL);
			break;
		}
	}

	return 0;
}

static int broadwell_suspend(struct snd_soc_card *card)
{
	return broadwell_disable_jack(card);
}

static int broadwell_resume(struct snd_soc_card *card){
	struct snd_soc_component *component;

	for_each_card_components(card, component) {
		if (!strcmp(component->name, "i2c-INT343A:00")) {

			dev_dbg(component->dev, "enabling jack detect for resume.\n");
			rt286_mic_detect(component, &broadwell_headset);
			break;
		}
	}
	return 0;
}

/* broadwell audio machine driver for WPT + RT286S */
static struct snd_soc_card broadwell_rt286 = {
	.name = "broadwell-rt286",
	.owner = THIS_MODULE,
	.dai_link = broadwell_rt286_dais,
	.num_links = ARRAY_SIZE(broadwell_rt286_dais),
	.controls = broadwell_controls,
	.num_controls = ARRAY_SIZE(broadwell_controls),
	.dapm_widgets = broadwell_widgets,
	.num_dapm_widgets = ARRAY_SIZE(broadwell_widgets),
	.dapm_routes = broadwell_rt286_map,
	.num_dapm_routes = ARRAY_SIZE(broadwell_rt286_map),
	.fully_routed = true,
	.suspend_pre = broadwell_suspend,
	.resume_post = broadwell_resume,
};

static int broadwell_audio_probe(struct platform_device *pdev)
{
	struct snd_soc_acpi_mach *mach;
	int ret;

	broadwell_rt286.dev = &pdev->dev;

	/* override plaform name, if required */
	mach = pdev->dev.platform_data;
	ret = snd_soc_fixup_dai_links_platform_name(&broadwell_rt286,
						    mach->mach_params.platform);
	if (ret)
		return ret;

	return devm_snd_soc_register_card(&pdev->dev, &broadwell_rt286);
}

static int broadwell_audio_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);

	return broadwell_disable_jack(card);
}

static struct platform_driver broadwell_audio = {
	.probe = broadwell_audio_probe,
	.remove = broadwell_audio_remove,
	.driver = {
		.name = "broadwell-audio",
	},
};

module_platform_driver(broadwell_audio)

/* Module information */
MODULE_AUTHOR("Liam Girdwood, Xingchao Wang");
MODULE_DESCRIPTION("Intel SST Audio for WPT/Broadwell");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:broadwell-audio");
