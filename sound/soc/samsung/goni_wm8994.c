/*
 * goni_wm8994.c
 *
 * Copyright (C) 2010 Samsung Electronics Co.Ltd
 * Author: Chanwoo Choi <cw00.choi@samsung.com>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <sound/soc.h>
#include <sound/jack.h>

#include <asm/mach-types.h>
#include <mach/gpio.h>

#include "../codecs/wm8994.h"

#define MACHINE_NAME	0
#define CPU_VOICE_DAI	1

static const char *aquila_str[] = {
	[MACHINE_NAME] = "aquila",
	[CPU_VOICE_DAI] = "aquila-voice-dai",
};

static struct snd_soc_card goni;
static struct platform_device *goni_snd_device;

/* 3.5 pie jack */
static struct snd_soc_jack jack;

/* 3.5 pie jack detection DAPM pins */
static struct snd_soc_jack_pin jack_pins[] = {
	{
		.pin = "Headset Mic",
		.mask = SND_JACK_MICROPHONE,
	}, {
		.pin = "Headset Stereophone",
		.mask = SND_JACK_HEADPHONE | SND_JACK_MECHANICAL |
			SND_JACK_AVOUT,
	},
};

/* 3.5 pie jack detection gpios */
static struct snd_soc_jack_gpio jack_gpios[] = {
	{
		.gpio = S5PV210_GPH0(6),
		.name = "DET_3.5",
		.report = SND_JACK_HEADSET | SND_JACK_MECHANICAL |
			SND_JACK_AVOUT,
		.debounce_time = 200,
	},
};

static const struct snd_soc_dapm_widget goni_dapm_widgets[] = {
	SND_SOC_DAPM_SPK("Ext Left Spk", NULL),
	SND_SOC_DAPM_SPK("Ext Right Spk", NULL),
	SND_SOC_DAPM_SPK("Ext Rcv", NULL),
	SND_SOC_DAPM_HP("Headset Stereophone", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
	SND_SOC_DAPM_MIC("Main Mic", NULL),
	SND_SOC_DAPM_MIC("2nd Mic", NULL),
	SND_SOC_DAPM_LINE("Radio In", NULL),
};

static const struct snd_soc_dapm_route goni_dapm_routes[] = {
	{"Ext Left Spk", NULL, "SPKOUTLP"},
	{"Ext Left Spk", NULL, "SPKOUTLN"},

	{"Ext Right Spk", NULL, "SPKOUTRP"},
	{"Ext Right Spk", NULL, "SPKOUTRN"},

	{"Ext Rcv", NULL, "HPOUT2N"},
	{"Ext Rcv", NULL, "HPOUT2P"},

	{"Headset Stereophone", NULL, "HPOUT1L"},
	{"Headset Stereophone", NULL, "HPOUT1R"},

	{"IN1RN", NULL, "Headset Mic"},
	{"IN1RP", NULL, "Headset Mic"},

	{"IN1RN", NULL, "2nd Mic"},
	{"IN1RP", NULL, "2nd Mic"},

	{"IN1LN", NULL, "Main Mic"},
	{"IN1LP", NULL, "Main Mic"},

	{"IN2LN", NULL, "Radio In"},
	{"IN2RN", NULL, "Radio In"},
};

static int goni_wm8994_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_soc_dapm_context *dapm = &codec->dapm;
	int ret;

	/* add goni specific widgets */
	snd_soc_dapm_new_controls(dapm, goni_dapm_widgets,
			ARRAY_SIZE(goni_dapm_widgets));

	/* set up goni specific audio routes */
	snd_soc_dapm_add_routes(dapm, goni_dapm_routes,
			ARRAY_SIZE(goni_dapm_routes));

	/* set endpoints to not connected */
	snd_soc_dapm_nc_pin(dapm, "IN2LP:VXRN");
	snd_soc_dapm_nc_pin(dapm, "IN2RP:VXRP");
	snd_soc_dapm_nc_pin(dapm, "LINEOUT1N");
	snd_soc_dapm_nc_pin(dapm, "LINEOUT1P");
	snd_soc_dapm_nc_pin(dapm, "LINEOUT2N");
	snd_soc_dapm_nc_pin(dapm, "LINEOUT2P");

	if (machine_is_aquila()) {
		snd_soc_dapm_nc_pin(dapm, "SPKOUTRN");
		snd_soc_dapm_nc_pin(dapm, "SPKOUTRP");
	}

	snd_soc_dapm_sync(dapm);

	/* Headset jack detection */
	ret = snd_soc_jack_new(codec, "Headset Jack",
			SND_JACK_HEADSET | SND_JACK_MECHANICAL | SND_JACK_AVOUT,
			&jack);
	if (ret)
		return ret;

	ret = snd_soc_jack_add_pins(&jack, ARRAY_SIZE(jack_pins), jack_pins);
	if (ret)
		return ret;

	ret = snd_soc_jack_add_gpios(&jack, ARRAY_SIZE(jack_gpios), jack_gpios);
	if (ret)
		return ret;

	return 0;
}

static int goni_hifi_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	unsigned int pll_out = 24000000;
	int ret = 0;

	/* set the cpu DAI configuration */
	ret = snd_soc_dai_set_fmt(cpu_dai, SND_SOC_DAIFMT_I2S |
			SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBM_CFM);
	if (ret < 0)
		return ret;

	/* set codec DAI configuration */
	ret = snd_soc_dai_set_fmt(codec_dai, SND_SOC_DAIFMT_I2S |
			SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBM_CFM);
	if (ret < 0)
		return ret;

	/* set the codec FLL */
	ret = snd_soc_dai_set_pll(codec_dai, WM8994_FLL1, 0, pll_out,
			params_rate(params) * 256);
	if (ret < 0)
		return ret;

	/* set the codec system clock */
	ret = snd_soc_dai_set_sysclk(codec_dai, WM8994_SYSCLK_FLL1,
			params_rate(params) * 256, SND_SOC_CLOCK_IN);
	if (ret < 0)
		return ret;

	return 0;
}

static struct snd_soc_ops goni_hifi_ops = {
	.hw_params = goni_hifi_hw_params,
};

static int goni_voice_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	unsigned int pll_out = 24000000;
	int ret = 0;

	if (params_rate(params) != 8000)
		return -EINVAL;

	/* set codec DAI configuration */
	ret = snd_soc_dai_set_fmt(codec_dai, SND_SOC_DAIFMT_LEFT_J |
			SND_SOC_DAIFMT_IB_IF | SND_SOC_DAIFMT_CBM_CFM);
	if (ret < 0)
		return ret;

	/* set the codec FLL */
	ret = snd_soc_dai_set_pll(codec_dai, WM8994_FLL2, 0, pll_out,
			params_rate(params) * 256);
	if (ret < 0)
		return ret;

	/* set the codec system clock */
	ret = snd_soc_dai_set_sysclk(codec_dai, WM8994_SYSCLK_FLL2,
			params_rate(params) * 256, SND_SOC_CLOCK_IN);
	if (ret < 0)
		return ret;

	return 0;
}

static struct snd_soc_dai_driver voice_dai = {
	.name = "goni-voice-dai",
	.id = 0,
	.playback = {
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,},
	.capture = {
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,},
};

static struct snd_soc_ops goni_voice_ops = {
	.hw_params = goni_voice_hw_params,
};

static struct snd_soc_dai_link goni_dai[] = {
{
	.name = "WM8994",
	.stream_name = "WM8994 HiFi",
	.cpu_dai_name = "samsung-i2s.0",
	.codec_dai_name = "wm8994-aif1",
	.platform_name = "samsung-audio",
	.codec_name = "wm8994-codec.0-001a",
	.init = goni_wm8994_init,
	.ops = &goni_hifi_ops,
}, {
	.name = "WM8994 Voice",
	.stream_name = "Voice",
	.cpu_dai_name = "goni-voice-dai",
	.codec_dai_name = "wm8994-aif2",
	.platform_name = "samsung-audio",
	.codec_name = "wm8994-codec.0-001a",
	.ops = &goni_voice_ops,
},
};

static struct snd_soc_card goni = {
	.name = "goni",
	.dai_link = goni_dai,
	.num_links = ARRAY_SIZE(goni_dai),
};

static int __init goni_init(void)
{
	int ret;

	if (machine_is_aquila()) {
		voice_dai.name = aquila_str[CPU_VOICE_DAI];
		goni_dai[1].cpu_dai_name = aquila_str[CPU_VOICE_DAI];
		goni.name = aquila_str[MACHINE_NAME];
	} else if (!machine_is_goni())
		return -ENODEV;

	goni_snd_device = platform_device_alloc("soc-audio", -1);
	if (!goni_snd_device)
		return -ENOMEM;

	/* register voice DAI here */
	ret = snd_soc_register_dai(&goni_snd_device->dev, &voice_dai);
	if (ret) {
		platform_device_put(goni_snd_device);
		return ret;
	}

	platform_set_drvdata(goni_snd_device, &goni);
	ret = platform_device_add(goni_snd_device);

	if (ret) {
		snd_soc_unregister_dai(&goni_snd_device->dev);
		platform_device_put(goni_snd_device);
	}

	return ret;
}

static void __exit goni_exit(void)
{
	snd_soc_unregister_dai(&goni_snd_device->dev);
	platform_device_unregister(goni_snd_device);
}

module_init(goni_init);
module_exit(goni_exit);

/* Module information */
MODULE_DESCRIPTION("ALSA SoC WM8994 GONI(S5PV210)");
MODULE_AUTHOR("Chanwoo Choi <cw00.choi@samsung.com>");
MODULE_LICENSE("GPL");
