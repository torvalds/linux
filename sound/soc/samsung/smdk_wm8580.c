/*
 *  smdk_wm8580.c
 *
 *  Copyright (c) 2009 Samsung Electronics Co. Ltd
 *  Author: Jaswinder Singh <jassi.brar@samsung.com>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */

#include <sound/soc.h>
#include <sound/pcm_params.h>

#include <asm/mach-types.h>

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
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	unsigned int pll_out;
	int bfs, rfs, ret;

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_U8:
	case SNDRV_PCM_FORMAT_S8:
		bfs = 16;
		break;
	case SNDRV_PCM_FORMAT_U16_LE:
	case SNDRV_PCM_FORMAT_S16_LE:
		bfs = 32;
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

	/* Set the Codec DAI configuration */
	ret = snd_soc_dai_set_fmt(codec_dai, SND_SOC_DAIFMT_I2S
					 | SND_SOC_DAIFMT_NB_NF
					 | SND_SOC_DAIFMT_CBM_CFM);
	if (ret < 0)
		return ret;

	/* Set the AP DAI configuration */
	ret = snd_soc_dai_set_fmt(cpu_dai, SND_SOC_DAIFMT_I2S
					 | SND_SOC_DAIFMT_NB_NF
					 | SND_SOC_DAIFMT_CBM_CFM);
	if (ret < 0)
		return ret;

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
static struct snd_soc_ops smdk_ops = {
	.hw_params = smdk_hw_params,
};

/* SMDK Playback widgets */
static const struct snd_soc_dapm_widget wm8580_dapm_widgets_pbk[] = {
	SND_SOC_DAPM_HP("Front", NULL),
	SND_SOC_DAPM_HP("Center+Sub", NULL),
	SND_SOC_DAPM_HP("Rear", NULL),
};

/* SMDK Capture widgets */
static const struct snd_soc_dapm_widget wm8580_dapm_widgets_cpt[] = {
	SND_SOC_DAPM_MIC("MicIn", NULL),
	SND_SOC_DAPM_LINE("LineIn", NULL),
};

/* SMDK-PAIFTX connections */
static const struct snd_soc_dapm_route audio_map_tx[] = {
	/* MicIn feeds AINL */
	{"AINL", NULL, "MicIn"},

	/* LineIn feeds AINL/R */
	{"AINL", NULL, "LineIn"},
	{"AINR", NULL, "LineIn"},
};

/* SMDK-PAIFRX connections */
static const struct snd_soc_dapm_route audio_map_rx[] = {
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
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_soc_dapm_context *dapm = &codec->dapm;

	/* Add smdk specific Capture widgets */
	snd_soc_dapm_new_controls(dapm, wm8580_dapm_widgets_cpt,
				  ARRAY_SIZE(wm8580_dapm_widgets_cpt));

	/* Set up PAIFTX audio path */
	snd_soc_dapm_add_routes(dapm, audio_map_tx, ARRAY_SIZE(audio_map_tx));

	/* Enabling the microphone requires the fitting of a 0R
	 * resistor to connect the line from the microphone jack.
	 */
	snd_soc_dapm_disable_pin(dapm, "MicIn");

	/* signal a DAPM event */
	snd_soc_dapm_sync(dapm);

	return 0;
}

static int smdk_wm8580_init_paifrx(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_soc_dapm_context *dapm = &codec->dapm;

	/* Add smdk specific Playback widgets */
	snd_soc_dapm_new_controls(dapm, wm8580_dapm_widgets_pbk,
				  ARRAY_SIZE(wm8580_dapm_widgets_pbk));

	/* Set up PAIFRX audio path */
	snd_soc_dapm_add_routes(dapm, audio_map_rx, ARRAY_SIZE(audio_map_rx));

	/* signal a DAPM event */
	snd_soc_dapm_sync(dapm);

	return 0;
}

enum {
	PRI_PLAYBACK = 0,
	PRI_CAPTURE,
	SEC_PLAYBACK,
};

static struct snd_soc_dai_link smdk_dai[] = {
	[PRI_PLAYBACK] = { /* Primary Playback i/f */
		.name = "WM8580 PAIF RX",
		.stream_name = "Playback",
		.cpu_dai_name = "samsung-i2s.0",
		.codec_dai_name = "wm8580-hifi-playback",
		.platform_name = "samsung-audio",
		.codec_name = "wm8580-codec.0-001b",
		.init = smdk_wm8580_init_paifrx,
		.ops = &smdk_ops,
	},
	[PRI_CAPTURE] = { /* Primary Capture i/f */
		.name = "WM8580 PAIF TX",
		.stream_name = "Capture",
		.cpu_dai_name = "samsung-i2s.0",
		.codec_dai_name = "wm8580-hifi-capture",
		.platform_name = "samsung-audio",
		.codec_name = "wm8580-codec.0-001b",
		.init = smdk_wm8580_init_paiftx,
		.ops = &smdk_ops,
	},
	[SEC_PLAYBACK] = { /* Sec_Fifo Playback i/f */
		.name = "Sec_FIFO TX",
		.stream_name = "Playback",
		.cpu_dai_name = "samsung-i2s.x",
		.codec_dai_name = "wm8580-hifi-playback",
		.platform_name = "samsung-audio",
		.codec_name = "wm8580-codec.0-001b",
		.init = smdk_wm8580_init_paifrx,
		.ops = &smdk_ops,
	},
};

static struct snd_soc_card smdk = {
	.name = "SMDK-I2S",
	.dai_link = smdk_dai,
	.num_links = 2,
};

static struct platform_device *smdk_snd_device;

static int __init smdk_audio_init(void)
{
	int ret;
	char *str;

	if (machine_is_smdkc100()
			|| machine_is_smdkv210() || machine_is_smdkc110()) {
		smdk.num_links = 3;
		/* Secondary is at offset SAMSUNG_I2S_SECOFF from Primary */
		str = (char *)smdk_dai[SEC_PLAYBACK].cpu_dai_name;
		str[strlen(str) - 1] = '0' + SAMSUNG_I2S_SECOFF;
	} else if (machine_is_smdk6410()) {
		str = (char *)smdk_dai[PRI_PLAYBACK].cpu_dai_name;
		str[strlen(str) - 1] = '2';
		str = (char *)smdk_dai[PRI_CAPTURE].cpu_dai_name;
		str[strlen(str) - 1] = '2';
	}

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

MODULE_AUTHOR("Jaswinder Singh, jassi.brar@samsung.com");
MODULE_DESCRIPTION("ALSA SoC SMDK WM8580");
MODULE_LICENSE("GPL");
