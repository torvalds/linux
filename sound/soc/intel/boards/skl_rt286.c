/*
 * Intel Skylake I2S Machine Driver
 *
 * Copyright (C) 2014-2015, Intel Corporation. All rights reserved.
 *
 * Modified from:
 *   Intel Broadwell Wildcatpoint SST Audio
 *
 *   Copyright (C) 2013, Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/jack.h>
#include <sound/pcm_params.h>
#include "../../codecs/rt286.h"
#include "../../codecs/hdac_hdmi.h"

static struct snd_soc_jack skylake_headset;

struct skl_hdmi_pcm {
	struct list_head head;
	struct snd_soc_dai *codec_dai;
	int device;
};

struct skl_rt286_private {
	struct list_head hdmi_pcm_list;
};

enum {
	SKL_DPCM_AUDIO_PB = 0,
	SKL_DPCM_AUDIO_CP,
	SKL_DPCM_AUDIO_REF_CP,
	SKL_DPCM_AUDIO_DMIC_CP,
	SKL_DPCM_AUDIO_HDMI1_PB,
	SKL_DPCM_AUDIO_HDMI2_PB,
	SKL_DPCM_AUDIO_HDMI3_PB,
};

/* Headset jack detection DAPM pins */
static struct snd_soc_jack_pin skylake_headset_pins[] = {
	{
		.pin = "Mic Jack",
		.mask = SND_JACK_MICROPHONE,
	},
	{
		.pin = "Headphone Jack",
		.mask = SND_JACK_HEADPHONE,
	},
};

static const struct snd_kcontrol_new skylake_controls[] = {
	SOC_DAPM_PIN_SWITCH("Speaker"),
	SOC_DAPM_PIN_SWITCH("Headphone Jack"),
	SOC_DAPM_PIN_SWITCH("Mic Jack"),
};

static const struct snd_soc_dapm_widget skylake_widgets[] = {
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_SPK("Speaker", NULL),
	SND_SOC_DAPM_MIC("Mic Jack", NULL),
	SND_SOC_DAPM_MIC("DMIC2", NULL),
	SND_SOC_DAPM_MIC("SoC DMIC", NULL),
	SND_SOC_DAPM_SPK("HDMI1", NULL),
	SND_SOC_DAPM_SPK("HDMI2", NULL),
	SND_SOC_DAPM_SPK("HDMI3", NULL),
};

static const struct snd_soc_dapm_route skylake_rt286_map[] = {
	/* speaker */
	{"Speaker", NULL, "SPOR"},
	{"Speaker", NULL, "SPOL"},

	/* HP jack connectors - unknown if we have jack deteck */
	{"Headphone Jack", NULL, "HPO Pin"},

	/* other jacks */
	{"MIC1", NULL, "Mic Jack"},

	/* digital mics */
	{"DMIC1 Pin", NULL, "DMIC2"},
	{"DMic", NULL, "SoC DMIC"},

	{"HDMI1", NULL, "hif5 Output"},
	{"HDMI2", NULL, "hif6 Output"},
	{"HDMI3", NULL, "hif7 Output"},

	/* CODEC BE connections */
	{ "AIF1 Playback", NULL, "ssp0 Tx"},
	{ "ssp0 Tx", NULL, "codec0_out"},
	{ "ssp0 Tx", NULL, "codec1_out"},

	{ "codec0_in", NULL, "ssp0 Rx" },
	{ "codec1_in", NULL, "ssp0 Rx" },
	{ "ssp0 Rx", NULL, "AIF1 Capture" },

	{ "dmic01_hifi", NULL, "DMIC01 Rx" },
	{ "DMIC01 Rx", NULL, "DMIC AIF" },

	{ "hifi3", NULL, "iDisp3 Tx"},
	{ "iDisp3 Tx", NULL, "iDisp3_out"},
	{ "hifi2", NULL, "iDisp2 Tx"},
	{ "iDisp2 Tx", NULL, "iDisp2_out"},
	{ "hifi1", NULL, "iDisp1 Tx"},
	{ "iDisp1 Tx", NULL, "iDisp1_out"},

};

static int skylake_rt286_fe_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_dapm_context *dapm;
	struct snd_soc_component *component = rtd->cpu_dai->component;

	dapm = snd_soc_component_get_dapm(component);
	snd_soc_dapm_ignore_suspend(dapm, "Reference Capture");

	return 0;
}

static int skylake_rt286_codec_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_codec *codec = rtd->codec;
	int ret;

	ret = snd_soc_card_jack_new(rtd->card, "Headset",
		SND_JACK_HEADSET | SND_JACK_BTN_0,
		&skylake_headset,
		skylake_headset_pins, ARRAY_SIZE(skylake_headset_pins));

	if (ret)
		return ret;

	rt286_mic_detect(codec, &skylake_headset);

	snd_soc_dapm_ignore_suspend(&rtd->card->dapm, "SoC DMIC");

	return 0;
}

static int skylake_hdmi_init(struct snd_soc_pcm_runtime *rtd)
{
	struct skl_rt286_private *ctx = snd_soc_card_get_drvdata(rtd->card);
	struct snd_soc_dai *dai = rtd->codec_dai;
	struct skl_hdmi_pcm *pcm;

	pcm = devm_kzalloc(rtd->card->dev, sizeof(*pcm), GFP_KERNEL);
	if (!pcm)
		return -ENOMEM;

	pcm->device = SKL_DPCM_AUDIO_HDMI1_PB + dai->id;
	pcm->codec_dai = dai;

	list_add_tail(&pcm->head, &ctx->hdmi_pcm_list);

	return 0;
}

static unsigned int rates[] = {
	48000,
};

static struct snd_pcm_hw_constraint_list constraints_rates = {
	.count = ARRAY_SIZE(rates),
	.list  = rates,
	.mask = 0,
};

static unsigned int channels[] = {
	2,
};

static struct snd_pcm_hw_constraint_list constraints_channels = {
	.count = ARRAY_SIZE(channels),
	.list = channels,
	.mask = 0,
};

static int skl_fe_startup(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;

	/*
	 * on this platform for PCM device we support,
	 *	48Khz
	 *	stereo
	 *	16 bit audio
	 */

	runtime->hw.channels_max = 2;
	snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_CHANNELS,
					   &constraints_channels);

	runtime->hw.formats = SNDRV_PCM_FMTBIT_S16_LE;
	snd_pcm_hw_constraint_msbits(runtime, 0, 16, 16);

	snd_pcm_hw_constraint_list(runtime, 0,
				SNDRV_PCM_HW_PARAM_RATE, &constraints_rates);

	return 0;
}

static const struct snd_soc_ops skylake_rt286_fe_ops = {
	.startup = skl_fe_startup,
};

static int skylake_ssp0_fixup(struct snd_soc_pcm_runtime *rtd,
			struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate = hw_param_interval(params,
			SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval *channels = hw_param_interval(params,
						SNDRV_PCM_HW_PARAM_CHANNELS);
	struct snd_mask *fmt = hw_param_mask(params, SNDRV_PCM_HW_PARAM_FORMAT);

	/* The output is 48KHz, stereo, 16bits */
	rate->min = rate->max = 48000;
	channels->min = channels->max = 2;

	/* set SSP0 to 24 bit */
	snd_mask_none(fmt);
	snd_mask_set(fmt, SNDRV_PCM_FORMAT_S24_LE);
	return 0;
}

static int skylake_rt286_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	int ret;

	ret = snd_soc_dai_set_sysclk(codec_dai, RT286_SCLK_S_PLL, 24000000,
		SND_SOC_CLOCK_IN);
	if (ret < 0)
		dev_err(rtd->dev, "set codec sysclk failed: %d\n", ret);

	return ret;
}

static struct snd_soc_ops skylake_rt286_ops = {
	.hw_params = skylake_rt286_hw_params,
};

static int skylake_dmic_fixup(struct snd_soc_pcm_runtime *rtd,
				struct snd_pcm_hw_params *params)
{
	struct snd_interval *channels = hw_param_interval(params,
						SNDRV_PCM_HW_PARAM_CHANNELS);
	if (params_channels(params) == 2)
		channels->min = channels->max = 2;
	else
		channels->min = channels->max = 4;

	return 0;
}

static unsigned int channels_dmic[] = {
	2, 4,
};

static struct snd_pcm_hw_constraint_list constraints_dmic_channels = {
	.count = ARRAY_SIZE(channels_dmic),
	.list = channels_dmic,
	.mask = 0,
};

static int skylake_dmic_startup(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;

	runtime->hw.channels_max = 4;
	snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_CHANNELS,
					   &constraints_dmic_channels);

	return snd_pcm_hw_constraint_list(substream->runtime, 0,
			SNDRV_PCM_HW_PARAM_RATE, &constraints_rates);
}

static struct snd_soc_ops skylake_dmic_ops = {
	.startup = skylake_dmic_startup,
};

/* skylake digital audio interface glue - connects codec <--> CPU */
static struct snd_soc_dai_link skylake_rt286_dais[] = {
	/* Front End DAI links */
	[SKL_DPCM_AUDIO_PB] = {
		.name = "Skl Audio Port",
		.stream_name = "Audio",
		.cpu_dai_name = "System Pin",
		.platform_name = "0000:00:1f.3",
		.nonatomic = 1,
		.dynamic = 1,
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.init = skylake_rt286_fe_init,
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST
		},
		.dpcm_playback = 1,
		.ops = &skylake_rt286_fe_ops,
	},
	[SKL_DPCM_AUDIO_CP] = {
		.name = "Skl Audio Capture Port",
		.stream_name = "Audio Record",
		.cpu_dai_name = "System Pin",
		.platform_name = "0000:00:1f.3",
		.nonatomic = 1,
		.dynamic = 1,
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST
		},
		.dpcm_capture = 1,
		.ops = &skylake_rt286_fe_ops,
	},
	[SKL_DPCM_AUDIO_REF_CP] = {
		.name = "Skl Audio Reference cap",
		.stream_name = "refcap",
		.cpu_dai_name = "Reference Pin",
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.platform_name = "0000:00:1f.3",
		.init = NULL,
		.dpcm_capture = 1,
		.nonatomic = 1,
		.dynamic = 1,
	},
	[SKL_DPCM_AUDIO_DMIC_CP] = {
		.name = "Skl Audio DMIC cap",
		.stream_name = "dmiccap",
		.cpu_dai_name = "DMIC Pin",
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.platform_name = "0000:00:1f.3",
		.init = NULL,
		.dpcm_capture = 1,
		.nonatomic = 1,
		.dynamic = 1,
		.ops = &skylake_dmic_ops,
	},
	[SKL_DPCM_AUDIO_HDMI1_PB] = {
		.name = "Skl HDMI Port1",
		.stream_name = "Hdmi1",
		.cpu_dai_name = "HDMI1 Pin",
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.platform_name = "0000:00:1f.3",
		.dpcm_playback = 1,
		.init = NULL,
		.nonatomic = 1,
		.dynamic = 1,
	},
	[SKL_DPCM_AUDIO_HDMI2_PB] = {
		.name = "Skl HDMI Port2",
		.stream_name = "Hdmi2",
		.cpu_dai_name = "HDMI2 Pin",
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.platform_name = "0000:00:1f.3",
		.dpcm_playback = 1,
		.init = NULL,
		.nonatomic = 1,
		.dynamic = 1,
	},
	[SKL_DPCM_AUDIO_HDMI3_PB] = {
		.name = "Skl HDMI Port3",
		.stream_name = "Hdmi3",
		.cpu_dai_name = "HDMI3 Pin",
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.platform_name = "0000:00:1f.3",
		.dpcm_playback = 1,
		.init = NULL,
		.nonatomic = 1,
		.dynamic = 1,
	},

	/* Back End DAI links */
	{
		/* SSP0 - Codec */
		.name = "SSP0-Codec",
		.id = 0,
		.cpu_dai_name = "SSP0 Pin",
		.platform_name = "0000:00:1f.3",
		.no_pcm = 1,
		.codec_name = "i2c-INT343A:00",
		.codec_dai_name = "rt286-aif1",
		.init = skylake_rt286_codec_init,
		.dai_fmt = SND_SOC_DAIFMT_I2S |
			SND_SOC_DAIFMT_NB_NF |
			SND_SOC_DAIFMT_CBS_CFS,
		.ignore_pmdown_time = 1,
		.be_hw_params_fixup = skylake_ssp0_fixup,
		.ops = &skylake_rt286_ops,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
	},
	{
		.name = "dmic01",
		.id = 1,
		.cpu_dai_name = "DMIC01 Pin",
		.codec_name = "dmic-codec",
		.codec_dai_name = "dmic-hifi",
		.platform_name = "0000:00:1f.3",
		.be_hw_params_fixup = skylake_dmic_fixup,
		.ignore_suspend = 1,
		.dpcm_capture = 1,
		.no_pcm = 1,
	},
	{
		.name = "iDisp1",
		.id = 2,
		.cpu_dai_name = "iDisp1 Pin",
		.codec_name = "ehdaudio0D2",
		.codec_dai_name = "intel-hdmi-hifi1",
		.platform_name = "0000:00:1f.3",
		.init = skylake_hdmi_init,
		.dpcm_playback = 1,
		.no_pcm = 1,
	},
	{
		.name = "iDisp2",
		.id = 3,
		.cpu_dai_name = "iDisp2 Pin",
		.codec_name = "ehdaudio0D2",
		.codec_dai_name = "intel-hdmi-hifi2",
		.platform_name = "0000:00:1f.3",
		.init = skylake_hdmi_init,
		.dpcm_playback = 1,
		.no_pcm = 1,
	},
	{
		.name = "iDisp3",
		.id = 4,
		.cpu_dai_name = "iDisp3 Pin",
		.codec_name = "ehdaudio0D2",
		.codec_dai_name = "intel-hdmi-hifi3",
		.platform_name = "0000:00:1f.3",
		.init = skylake_hdmi_init,
		.dpcm_playback = 1,
		.no_pcm = 1,
	},
};

static int skylake_card_late_probe(struct snd_soc_card *card)
{
	struct skl_rt286_private *ctx = snd_soc_card_get_drvdata(card);
	struct skl_hdmi_pcm *pcm;
	int err;

	list_for_each_entry(pcm, &ctx->hdmi_pcm_list, head) {
		err = hdac_hdmi_jack_init(pcm->codec_dai, pcm->device);
		if (err < 0)
			return err;
	}

	return 0;
}

/* skylake audio machine driver for SPT + RT286S */
static struct snd_soc_card skylake_rt286 = {
	.name = "skylake-rt286",
	.owner = THIS_MODULE,
	.dai_link = skylake_rt286_dais,
	.num_links = ARRAY_SIZE(skylake_rt286_dais),
	.controls = skylake_controls,
	.num_controls = ARRAY_SIZE(skylake_controls),
	.dapm_widgets = skylake_widgets,
	.num_dapm_widgets = ARRAY_SIZE(skylake_widgets),
	.dapm_routes = skylake_rt286_map,
	.num_dapm_routes = ARRAY_SIZE(skylake_rt286_map),
	.fully_routed = true,
	.late_probe = skylake_card_late_probe,
};

static int skylake_audio_probe(struct platform_device *pdev)
{
	struct skl_rt286_private *ctx;

	ctx = devm_kzalloc(&pdev->dev, sizeof(*ctx), GFP_ATOMIC);
	if (!ctx)
		return -ENOMEM;

	INIT_LIST_HEAD(&ctx->hdmi_pcm_list);

	skylake_rt286.dev = &pdev->dev;
	snd_soc_card_set_drvdata(&skylake_rt286, ctx);

	return devm_snd_soc_register_card(&pdev->dev, &skylake_rt286);
}

static struct platform_driver skylake_audio = {
	.probe = skylake_audio_probe,
	.driver = {
		.name = "skl_alc286s_i2s",
		.pm = &snd_soc_pm_ops,
	},
};

module_platform_driver(skylake_audio)

/* Module information */
MODULE_AUTHOR("Omair Mohammed Abdullah <omair.m.abdullah@intel.com>");
MODULE_DESCRIPTION("Intel SST Audio for Skylake");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:skl_alc286s_i2s");
