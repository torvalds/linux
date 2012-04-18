/*
 * kirkwood-t5325.c
 *
 * (c) 2010 Arnaud Patard <arnaud.patard@rtp-net.org>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <sound/soc.h>
#include <mach/kirkwood.h>
#include <plat/audio.h>
#include <asm/mach-types.h>
#include "../codecs/alc5623.h"

static int t5325_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	unsigned int freq;

	freq = params_rate(params) * 256;

	return snd_soc_dai_set_sysclk(codec_dai, 0, freq, SND_SOC_CLOCK_IN);

}

static struct snd_soc_ops t5325_ops = {
	.hw_params = t5325_hw_params,
};

static const struct snd_soc_dapm_widget t5325_dapm_widgets[] = {
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_SPK("Speaker", NULL),
	SND_SOC_DAPM_MIC("Mic Jack", NULL),
};

static const struct snd_soc_dapm_route t5325_route[] = {
	{ "Headphone Jack",	NULL,	"HPL" },
	{ "Headphone Jack",	NULL,	"HPR" },

	{"Speaker",		NULL,	"SPKOUT"},
	{"Speaker",		NULL,	"SPKOUTN"},

	{ "MIC1",		NULL,	"Mic Jack" },
	{ "MIC2",		NULL,	"Mic Jack" },
};

static int t5325_dai_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_soc_dapm_context *dapm = &codec->dapm;

	snd_soc_dapm_enable_pin(dapm, "Mic Jack");
	snd_soc_dapm_enable_pin(dapm, "Headphone Jack");
	snd_soc_dapm_enable_pin(dapm, "Speaker");

	return 0;
}

static struct snd_soc_dai_link t5325_dai[] = {
{
	.name = "ALC5621",
	.stream_name = "ALC5621 HiFi",
	.cpu_dai_name = "kirkwood-i2s",
	.platform_name = "kirkwood-pcm-audio",
	.codec_dai_name = "alc5621-hifi",
	.codec_name = "alc562x-codec.0-001a",
	.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_CBS_CFS,
	.ops = &t5325_ops,
	.init = t5325_dai_init,
},
};

static struct snd_soc_card t5325 = {
	.name = "t5325",
	.owner = THIS_MODULE,
	.dai_link = t5325_dai,
	.num_links = ARRAY_SIZE(t5325_dai),

	.dapm_widgets = t5325_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(t5325_dapm_widgets),
	.dapm_routes = t5325_route,
	.num_dapm_routes = ARRAY_SIZE(t5325_route),
};

static int __devinit t5325_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = &t5325;
	int ret;

	card->dev = &pdev->dev;

	ret = snd_soc_register_card(card);
	if (ret)
		dev_err(&pdev->dev, "snd_soc_register_card() failed: %d\n",
			ret);
	return ret;
}

static int __devexit t5325_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);

	snd_soc_unregister_card(card);
	return 0;
}

static struct platform_driver t5325_driver = {
	.driver		= {
		.name	= "t5325-audio",
		.owner	= THIS_MODULE,
	},
	.probe		= t5325_probe,
	.remove		= __devexit_p(t5325_remove),
};

module_platform_driver(t5325_driver);

MODULE_AUTHOR("Arnaud Patard <arnaud.patard@rtp-net.org>");
MODULE_DESCRIPTION("ALSA SoC t5325 audio client");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:t5325-audio");
