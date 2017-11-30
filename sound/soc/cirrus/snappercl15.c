/*
 * snappercl15.c -- SoC audio for Bluewater Systems Snapper CL15 module
 *
 * Copyright (C) 2008 Bluewater Systems Ltd
 * Author: Ryan Mallon
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <linux/platform_device.h>
#include <linux/module.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>

#include <asm/mach-types.h>
#include <mach/hardware.h>

#include "../codecs/tlv320aic23.h"

#define CODEC_CLOCK 5644800

static int snappercl15_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	int err;

	err = snd_soc_dai_set_sysclk(codec_dai, 0, CODEC_CLOCK, 
				     SND_SOC_CLOCK_IN);
	if (err)
		return err;

	err = snd_soc_dai_set_sysclk(cpu_dai, 0, CODEC_CLOCK, 
				     SND_SOC_CLOCK_OUT);
	if (err)
		return err;

	return 0;
}

static const struct snd_soc_ops snappercl15_ops = {
	.hw_params	= snappercl15_hw_params,
};

static const struct snd_soc_dapm_widget tlv320aic23_dapm_widgets[] = {
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_LINE("Line In", NULL),
	SND_SOC_DAPM_MIC("Mic Jack", NULL),
};

static const struct snd_soc_dapm_route audio_map[] = {
	{"Headphone Jack", NULL, "LHPOUT"},
	{"Headphone Jack", NULL, "RHPOUT"},

	{"LLINEIN", NULL, "Line In"},
	{"RLINEIN", NULL, "Line In"},

	{"MICIN", NULL, "Mic Jack"},
};

static struct snd_soc_dai_link snappercl15_dai = {
	.name		= "tlv320aic23",
	.stream_name	= "AIC23",
	.cpu_dai_name	= "ep93xx-i2s",
	.codec_dai_name	= "tlv320aic23-hifi",
	.codec_name	= "tlv320aic23-codec.0-001a",
	.platform_name	= "ep93xx-i2s",
	.dai_fmt	= SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_IF |
			  SND_SOC_DAIFMT_CBS_CFS,
	.ops		= &snappercl15_ops,
};

static struct snd_soc_card snd_soc_snappercl15 = {
	.name		= "Snapper CL15",
	.owner		= THIS_MODULE,
	.dai_link	= &snappercl15_dai,
	.num_links	= 1,

	.dapm_widgets		= tlv320aic23_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(tlv320aic23_dapm_widgets),
	.dapm_routes		= audio_map,
	.num_dapm_routes	= ARRAY_SIZE(audio_map),
};

static int snappercl15_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = &snd_soc_snappercl15;
	int ret;

	ret = ep93xx_i2s_acquire();
	if (ret)
		return ret;

	card->dev = &pdev->dev;

	ret = snd_soc_register_card(card);
	if (ret) {
		dev_err(&pdev->dev, "snd_soc_register_card() failed: %d\n",
			ret);
		ep93xx_i2s_release();
	}

	return ret;
}

static int snappercl15_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);

	snd_soc_unregister_card(card);
	ep93xx_i2s_release();

	return 0;
}

static struct platform_driver snappercl15_driver = {
	.driver		= {
		.name	= "snappercl15-audio",
	},
	.probe		= snappercl15_probe,
	.remove		= snappercl15_remove,
};

module_platform_driver(snappercl15_driver);

MODULE_AUTHOR("Ryan Mallon");
MODULE_DESCRIPTION("ALSA SoC Snapper CL15");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:snappercl15-audio");
