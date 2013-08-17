/*
 * tavorevb3.c -- SoC audio for Tavor EVB3
 *
 * Copyright (C) 2010 Marvell International Ltd.
 * 	Haojian Zhuang <haojian.zhuang@marvell.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/device.h>
#include <linux/clk.h>
#include <linux/i2c.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/jack.h>

#include <asm/mach-types.h>

#include "../codecs/88pm860x-codec.h"
#include "pxa-ssp.h"

static int evb3_pm860x_init(struct snd_soc_pcm_runtime *rtd);

static struct platform_device *evb3_snd_device;

static struct snd_soc_jack hs_jack, mic_jack;

static struct snd_soc_jack_pin hs_jack_pins[] = {
	{ .pin = "Headset Stereophone",	.mask = SND_JACK_HEADPHONE, },
};

static struct snd_soc_jack_pin mic_jack_pins[] = {
	{ .pin = "Headset Mic 2",	.mask = SND_JACK_MICROPHONE, },
};

/* tavorevb3 machine dapm widgets */
static const struct snd_soc_dapm_widget evb3_dapm_widgets[] = {
	SND_SOC_DAPM_HP("Headset Stereophone", NULL),
	SND_SOC_DAPM_LINE("Lineout Out 1", NULL),
	SND_SOC_DAPM_LINE("Lineout Out 2", NULL),
	SND_SOC_DAPM_SPK("Ext Speaker", NULL),
	SND_SOC_DAPM_MIC("Ext Mic 1", NULL),
	SND_SOC_DAPM_MIC("Headset Mic 2", NULL),
	SND_SOC_DAPM_MIC("Ext Mic 3", NULL),
};

/* tavorevb3 machine audio map */
static const struct snd_soc_dapm_route evb3_audio_map[] = {
	{"Headset Stereophone", NULL, "HS1"},
	{"Headset Stereophone", NULL, "HS2"},

	{"Ext Speaker", NULL, "LSP"},
	{"Ext Speaker", NULL, "LSN"},

	{"Lineout Out 1", NULL, "LINEOUT1"},
	{"Lineout Out 2", NULL, "LINEOUT2"},

	{"MIC1P", NULL, "Mic1 Bias"},
	{"MIC1N", NULL, "Mic1 Bias"},
	{"Mic1 Bias", NULL, "Ext Mic 1"},

	{"MIC2P", NULL, "Mic1 Bias"},
	{"MIC2N", NULL, "Mic1 Bias"},
	{"Mic1 Bias", NULL, "Headset Mic 2"},

	{"MIC3P", NULL, "Mic3 Bias"},
	{"MIC3N", NULL, "Mic3 Bias"},
	{"Mic3 Bias", NULL, "Ext Mic 3"},
};

static int evb3_i2s_hw_params(struct snd_pcm_substream *substream,
			      struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	int width = snd_pcm_format_physical_width(params_format(params));
	int ret;

	ret = snd_soc_dai_set_sysclk(cpu_dai, PXA_SSP_CLK_NET_PLL, 0,
				     PM860X_CLK_DIR_OUT);
	if (ret < 0)
		return ret;

	ret = snd_soc_dai_set_sysclk(codec_dai, 0, 0, PM860X_CLK_DIR_OUT);
	if (ret < 0)
		return ret;

	ret = snd_soc_dai_set_tdm_slot(cpu_dai, 3, 3, 2, width);
	return ret;
}

static struct snd_soc_ops evb3_i2s_ops = {
	.hw_params	= evb3_i2s_hw_params,
};

static struct snd_soc_dai_link evb3_dai[] = {
	{
		.name		= "88PM860x I2S",
		.stream_name	= "I2S Audio",
		.cpu_dai_name	= "pxa-ssp-dai.1",
		.codec_dai_name	= "88pm860x-i2s",
		.platform_name	= "pxa-pcm-audio",
		.codec_name	= "88pm860x-codec",
		.init		= evb3_pm860x_init,
		.dai_fmt	= SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
				  SND_SOC_DAIFMT_CBM_CFM,
		.ops		= &evb3_i2s_ops,
	},
};

static struct snd_soc_card snd_soc_card_evb3 = {
	.name = "Tavor EVB3",
	.owner = THIS_MODULE,
	.dai_link = evb3_dai,
	.num_links = ARRAY_SIZE(evb3_dai),

	.dapm_widgets = evb3_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(evb3_dapm_widgets),
	.dapm_routes = evb3_audio_map,
	.num_dapm_routes = ARRAY_SIZE(evb3_audio_map),
};

static int evb3_pm860x_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_soc_dapm_context *dapm = &codec->dapm;

	/* connected pins */
	snd_soc_dapm_enable_pin(dapm, "Ext Speaker");
	snd_soc_dapm_enable_pin(dapm, "Ext Mic 1");
	snd_soc_dapm_enable_pin(dapm, "Ext Mic 3");
	snd_soc_dapm_disable_pin(dapm, "Headset Mic 2");
	snd_soc_dapm_disable_pin(dapm, "Headset Stereophone");

	/* Headset jack detection */
	snd_soc_jack_new(codec, "Headphone Jack", SND_JACK_HEADPHONE
			| SND_JACK_BTN_0 | SND_JACK_BTN_1 | SND_JACK_BTN_2,
			&hs_jack);
	snd_soc_jack_add_pins(&hs_jack, ARRAY_SIZE(hs_jack_pins),
			      hs_jack_pins);
	snd_soc_jack_new(codec, "Microphone Jack", SND_JACK_MICROPHONE,
			 &mic_jack);
	snd_soc_jack_add_pins(&mic_jack, ARRAY_SIZE(mic_jack_pins),
			      mic_jack_pins);

	/* headphone, microphone detection & headset short detection */
	pm860x_hs_jack_detect(codec, &hs_jack, SND_JACK_HEADPHONE,
			      SND_JACK_BTN_0, SND_JACK_BTN_1, SND_JACK_BTN_2);
	pm860x_mic_jack_detect(codec, &hs_jack, SND_JACK_MICROPHONE);
	return 0;
}

static int __init tavorevb3_init(void)
{
	int ret;

	if (!machine_is_tavorevb3())
		return -ENODEV;
	evb3_snd_device = platform_device_alloc("soc-audio", -1);
	if (!evb3_snd_device)
		return -ENOMEM;

	platform_set_drvdata(evb3_snd_device, &snd_soc_card_evb3);

	ret = platform_device_add(evb3_snd_device);
	if (ret)
		platform_device_put(evb3_snd_device);

	return ret;
}

static void __exit tavorevb3_exit(void)
{
	platform_device_unregister(evb3_snd_device);
}

module_init(tavorevb3_init);
module_exit(tavorevb3_exit);

MODULE_AUTHOR("Haojian Zhuang <haojian.zhuang@marvell.com>");
MODULE_DESCRIPTION("ALSA SoC 88PM860x Tavor EVB3");
MODULE_LICENSE("GPL");
