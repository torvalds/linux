/*
 * saarb.c -- SoC audio for saarb
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
#include <sound/soc-dapm.h>
#include <sound/jack.h>

#include <asm/mach-types.h>

#include "../codecs/88pm860x-codec.h"
#include "pxa-ssp.h"

static int saarb_pm860x_init(struct snd_soc_pcm_runtime *rtd);

static struct platform_device *saarb_snd_device;

static struct snd_soc_jack hs_jack, mic_jack;

static struct snd_soc_jack_pin hs_jack_pins[] = {
	{ .pin = "Headset Stereophone",	.mask = SND_JACK_HEADPHONE, },
};

static struct snd_soc_jack_pin mic_jack_pins[] = {
	{ .pin = "Headset Mic 2",	.mask = SND_JACK_MICROPHONE, },
};

/* saarb machine dapm widgets */
static const struct snd_soc_dapm_widget saarb_dapm_widgets[] = {
	SND_SOC_DAPM_HP("Headphone Stereophone", NULL),
	SND_SOC_DAPM_LINE("Lineout Out 1", NULL),
	SND_SOC_DAPM_LINE("Lineout Out 2", NULL),
	SND_SOC_DAPM_SPK("Ext Speaker", NULL),
	SND_SOC_DAPM_MIC("Ext Mic 1", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
	SND_SOC_DAPM_MIC("Ext Mic 3", NULL),
};

/* saarb machine audio map */
static const struct snd_soc_dapm_route audio_map[] = {
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

static int saarb_i2s_hw_params(struct snd_pcm_substream *substream,
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

	ret = snd_soc_dai_set_fmt(cpu_dai, SND_SOC_DAIFMT_I2S |
			SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBM_CFM);
	if (ret < 0)
		return ret;
	ret = snd_soc_dai_set_fmt(codec_dai, SND_SOC_DAIFMT_I2S |
			SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBM_CFM);
	if (ret < 0)
		return ret;

	ret = snd_soc_dai_set_tdm_slot(cpu_dai, 3, 3, 2, width);

	return ret;
}

static struct snd_soc_ops saarb_i2s_ops = {
	.hw_params	= saarb_i2s_hw_params,
};

static struct snd_soc_dai_link saarb_dai[] = {
	{
		.name		= "88PM860x I2S",
		.stream_name	= "I2S Audio",
		.cpu_dai_name	= "pxa-ssp-dai.1",
		.codec_dai_name	= "88pm860x-i2s",
		.platform_name	= "pxa-pcm-audio",
		.codec_name	= "88pm860x-codec",
		.init		= saarb_pm860x_init,
		.ops		= &saarb_i2s_ops,
	},
};

static struct snd_soc_card snd_soc_card_saarb = {
	.name = "Saarb",
	.dai_link = saarb_dai,
	.num_links = ARRAY_SIZE(saarb_dai),
};

static int saarb_pm860x_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_codec *codec = rtd->codec;
	int ret;

	snd_soc_dapm_new_controls(codec, saarb_dapm_widgets,
				  ARRAY_SIZE(saarb_dapm_widgets));
	snd_soc_dapm_add_routes(codec, audio_map, ARRAY_SIZE(audio_map));

	/* connected pins */
	snd_soc_dapm_enable_pin(codec, "Ext Speaker");
	snd_soc_dapm_enable_pin(codec, "Ext Mic 1");
	snd_soc_dapm_enable_pin(codec, "Ext Mic 3");
	snd_soc_dapm_disable_pin(codec, "Headset Mic 2");
	snd_soc_dapm_disable_pin(codec, "Headset Stereophone");

	ret = snd_soc_dapm_sync(codec);
	if (ret)
		return ret;

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

static int __init saarb_init(void)
{
	int ret;

	if (!machine_is_saarb())
		return -ENODEV;
	saarb_snd_device = platform_device_alloc("soc-audio", -1);
	if (!saarb_snd_device)
		return -ENOMEM;

	platform_set_drvdata(saarb_snd_device, &snd_soc_card_saarb);

	ret = platform_device_add(saarb_snd_device);
	if (ret)
		platform_device_put(saarb_snd_device);

	return ret;
}

static void __exit saarb_exit(void)
{
	platform_device_unregister(saarb_snd_device);
}

module_init(saarb_init);
module_exit(saarb_exit);

MODULE_AUTHOR("Haojian Zhuang <haojian.zhuang@marvell.com>");
MODULE_DESCRIPTION("ALSA SoC 88PM860x Saarb");
MODULE_LICENSE("GPL");
