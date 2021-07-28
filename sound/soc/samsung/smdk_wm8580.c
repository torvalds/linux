// SPDX-License-Identifier: GPL-2.0+
//
// Copyright (c) 2009 Samsung Electronics Co. Ltd
// Author: Jaswinder Singh <jassisinghbrar@gmail.com>

#include <linux/module.h>
#include <sound/soc.h>
#include <sound/pcm_params.h>

#include "../codecs/wm8580.h"
#include "i2s.h"

/*
 * Default CFG switch settings to use this driver:
 *
 *   SMDK6410: Set CFG1 1-3 Off, CFG2 1-4 On
 */

/* SMDK has a 12MHZ crystal attached to WM8580 */
#define SMDK_WM8580_FREQ 12000000

static int smdk_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct snd_soc_dai *codec_dai = asoc_rtd_to_codec(rtd, 0);
	unsigned int pll_out;
	int rfs, ret;

	switch (params_width(params)) {
	case 8:
	case 16:
		break;
	default:
		return -EINVAL;
	}

	/* The Fvco for WM8580 PLLs must fall within [90,100]MHz.
	 * This criterion can't be met if we request PLL output
	 * as {8000x256, 64000x256, 11025x256}Hz.
	 * As a wayout, we rather change rfs to a minimum value that
	 * results in (params_rate(params) * rfs), and itself, acceptable
	 * to both - the CODEC and the CPU.
	 */
	switch (params_rate(params)) {
	case 16000:
	case 22050:
	case 32000:
	case 44100:
	case 48000:
	case 88200:
	case 96000:
		rfs = 256;
		break;
	case 64000:
		rfs = 384;
		break;
	case 8000:
	case 11025:
		rfs = 512;
		break;
	default:
		return -EINVAL;
	}
	pll_out = params_rate(params) * rfs;

	/* Set WM8580 to drive MCLK from its PLLA */
	ret = snd_soc_dai_set_clkdiv(codec_dai, WM8580_MCLK,
					WM8580_CLKSRC_PLLA);
	if (ret < 0)
		return ret;

	ret = snd_soc_dai_set_pll(codec_dai, WM8580_PLLA, 0,
					SMDK_WM8580_FREQ, pll_out);
	if (ret < 0)
		return ret;

	ret = snd_soc_dai_set_sysclk(codec_dai, WM8580_CLKSRC_PLLA,
				     pll_out, SND_SOC_CLOCK_IN);
	if (ret < 0)
		return ret;

	return 0;
}

/*
 * SMDK WM8580 DAI operations.
 */
static const struct snd_soc_ops smdk_ops = {
	.hw_params = smdk_hw_params,
};

/* SMDK Playback widgets */
static const struct snd_soc_dapm_widget smdk_wm8580_dapm_widgets[] = {
	SND_SOC_DAPM_HP("Front", NULL),
	SND_SOC_DAPM_HP("Center+Sub", NULL),
	SND_SOC_DAPM_HP("Rear", NULL),

	SND_SOC_DAPM_MIC("MicIn", NULL),
	SND_SOC_DAPM_LINE("LineIn", NULL),
};

/* SMDK-PAIFTX connections */
static const struct snd_soc_dapm_route smdk_wm8580_audio_map[] = {
	/* MicIn feeds AINL */
	{"AINL", NULL, "MicIn"},

	/* LineIn feeds AINL/R */
	{"AINL", NULL, "LineIn"},
	{"AINR", NULL, "LineIn"},

	/* Front Left/Right are fed VOUT1L/R */
	{"Front", NULL, "VOUT1L"},
	{"Front", NULL, "VOUT1R"},

	/* Center/Sub are fed VOUT2L/R */
	{"Center+Sub", NULL, "VOUT2L"},
	{"Center+Sub", NULL, "VOUT2R"},

	/* Rear Left/Right are fed VOUT3L/R */
	{"Rear", NULL, "VOUT3L"},
	{"Rear", NULL, "VOUT3R"},
};

static int smdk_wm8580_init_paiftx(struct snd_soc_pcm_runtime *rtd)
{
	/* Enabling the microphone requires the fitting of a 0R
	 * resistor to connect the line from the microphone jack.
	 */
	snd_soc_dapm_disable_pin(&rtd->card->dapm, "MicIn");

	return 0;
}

enum {
	PRI_PLAYBACK = 0,
	PRI_CAPTURE,
};

#define SMDK_DAI_FMT (SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF | \
	SND_SOC_DAIFMT_CBM_CFM)

SND_SOC_DAILINK_DEFS(paif_rx,
	DAILINK_COMP_ARRAY(COMP_CPU("samsung-i2s.2")),
	DAILINK_COMP_ARRAY(COMP_CODEC("wm8580.0-001b", "wm8580-hifi-playback")),
	DAILINK_COMP_ARRAY(COMP_PLATFORM("samsung-i2s.0")));

SND_SOC_DAILINK_DEFS(paif_tx,
	DAILINK_COMP_ARRAY(COMP_CPU("samsung-i2s.2")),
	DAILINK_COMP_ARRAY(COMP_CODEC("wm8580.0-001b", "wm8580-hifi-capture")),
	DAILINK_COMP_ARRAY(COMP_PLATFORM("samsung-i2s.0")));

static struct snd_soc_dai_link smdk_dai[] = {
	[PRI_PLAYBACK] = { /* Primary Playback i/f */
		.name = "WM8580 PAIF RX",
		.stream_name = "Playback",
		.dai_fmt = SMDK_DAI_FMT,
		.ops = &smdk_ops,
		SND_SOC_DAILINK_REG(paif_rx),
	},
	[PRI_CAPTURE] = { /* Primary Capture i/f */
		.name = "WM8580 PAIF TX",
		.stream_name = "Capture",
		.dai_fmt = SMDK_DAI_FMT,
		.init = smdk_wm8580_init_paiftx,
		.ops = &smdk_ops,
		SND_SOC_DAILINK_REG(paif_tx),
	},
};

static struct snd_soc_card smdk = {
	.name = "SMDK-I2S",
	.owner = THIS_MODULE,
	.dai_link = smdk_dai,
	.num_links = ARRAY_SIZE(smdk_dai),

	.dapm_widgets = smdk_wm8580_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(smdk_wm8580_dapm_widgets),
	.dapm_routes = smdk_wm8580_audio_map,
	.num_dapm_routes = ARRAY_SIZE(smdk_wm8580_audio_map),
};

static struct platform_device *smdk_snd_device;

static int __init smdk_audio_init(void)
{
	int ret;

	smdk_snd_device = platform_device_alloc("soc-audio", -1);
	if (!smdk_snd_device)
		return -ENOMEM;

	platform_set_drvdata(smdk_snd_device, &smdk);
	ret = platform_device_add(smdk_snd_device);

	if (ret)
		platform_device_put(smdk_snd_device);

	return ret;
}
module_init(smdk_audio_init);

static void __exit smdk_audio_exit(void)
{
	platform_device_unregister(smdk_snd_device);
}
module_exit(smdk_audio_exit);

MODULE_AUTHOR("Jaswinder Singh, jassisinghbrar@gmail.com");
MODULE_DESCRIPTION("ALSA SoC SMDK WM8580");
MODULE_LICENSE("GPL");
