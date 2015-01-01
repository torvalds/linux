/*
 * ALSA SoC driver for Migo-R
 *
 * Copyright (C) 2009-2010 Guennadi Liakhovetski <g.liakhovetski@gmx.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/clkdev.h>
#include <linux/device.h>
#include <linux/firmware.h>
#include <linux/module.h>

#include <asm/clock.h>

#include <cpu/sh7722.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>

#include "../codecs/wm8978.h"
#include "siu.h"

/* Default 8000Hz sampling frequency */
static unsigned long codec_freq = 8000 * 512;

static unsigned int use_count;

/* External clock, sourced from the codec at the SIUMCKB pin */
static unsigned long siumckb_recalc(struct clk *clk)
{
	return codec_freq;
}

static struct sh_clk_ops siumckb_clk_ops = {
	.recalc = siumckb_recalc,
};

static struct clk siumckb_clk = {
	.ops		= &siumckb_clk_ops,
	.rate		= 0, /* initialised at run-time */
};

static struct clk_lookup *siumckb_lookup;

static int migor_hw_params(struct snd_pcm_substream *substream,
			   struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	int ret;
	unsigned int rate = params_rate(params);

	ret = snd_soc_dai_set_sysclk(codec_dai, WM8978_PLL, 13000000,
				     SND_SOC_CLOCK_IN);
	if (ret < 0)
		return ret;

	ret = snd_soc_dai_set_clkdiv(codec_dai, WM8978_OPCLKRATE, rate * 512);
	if (ret < 0)
		return ret;

	codec_freq = rate * 512;
	/*
	 * This propagates the parent frequency change to children and
	 * recalculates the frequency table
	 */
	clk_set_rate(&siumckb_clk, codec_freq);
	dev_dbg(codec_dai->dev, "%s: configure %luHz\n", __func__, codec_freq);

	ret = snd_soc_dai_set_sysclk(rtd->cpu_dai, SIU_CLKB_EXT,
				     codec_freq / 2, SND_SOC_CLOCK_IN);

	if (!ret)
		use_count++;

	return ret;
}

static int migor_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;

	if (use_count) {
		use_count--;

		if (!use_count)
			snd_soc_dai_set_sysclk(codec_dai, WM8978_PLL, 0,
					       SND_SOC_CLOCK_IN);
	} else {
		dev_dbg(codec_dai->dev, "Unbalanced hw_free!\n");
	}

	return 0;
}

static struct snd_soc_ops migor_dai_ops = {
	.hw_params = migor_hw_params,
	.hw_free = migor_hw_free,
};

static const struct snd_soc_dapm_widget migor_dapm_widgets[] = {
	SND_SOC_DAPM_HP("Headphone", NULL),
	SND_SOC_DAPM_MIC("Onboard Microphone", NULL),
	SND_SOC_DAPM_MIC("External Microphone", NULL),
};

static const struct snd_soc_dapm_route audio_map[] = {
	/* Headphone output connected to LHP/RHP, enable OUT4 for VMID */
	{ "Headphone", NULL,  "OUT4 VMID" },
	{ "OUT4 VMID", NULL,  "LHP" },
	{ "OUT4 VMID", NULL,  "RHP" },

	/* On-board microphone */
	{ "RMICN", NULL, "Mic Bias" },
	{ "RMICP", NULL, "Mic Bias" },
	{ "Mic Bias", NULL, "Onboard Microphone" },

	/* External microphone */
	{ "LMICN", NULL, "Mic Bias" },
	{ "LMICP", NULL, "Mic Bias" },
	{ "Mic Bias", NULL, "External Microphone" },
};

/* migor digital audio interface glue - connects codec <--> CPU */
static struct snd_soc_dai_link migor_dai = {
	.name = "wm8978",
	.stream_name = "WM8978",
	.cpu_dai_name = "siu-pcm-audio",
	.codec_dai_name = "wm8978-hifi",
	.platform_name = "siu-pcm-audio",
	.codec_name = "wm8978.0-001a",
	.dai_fmt = SND_SOC_DAIFMT_NB_IF | SND_SOC_DAIFMT_I2S |
		   SND_SOC_DAIFMT_CBS_CFS,
	.ops = &migor_dai_ops,
};

/* migor audio machine driver */
static struct snd_soc_card snd_soc_migor = {
	.name = "Migo-R",
	.owner = THIS_MODULE,
	.dai_link = &migor_dai,
	.num_links = 1,

	.dapm_widgets = migor_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(migor_dapm_widgets),
	.dapm_routes = audio_map,
	.num_dapm_routes = ARRAY_SIZE(audio_map),
};

static struct platform_device *migor_snd_device;

static int __init migor_init(void)
{
	int ret;

	ret = clk_register(&siumckb_clk);
	if (ret < 0)
		return ret;

	siumckb_lookup = clkdev_alloc(&siumckb_clk, "siumckb_clk", NULL);
	if (!siumckb_lookup) {
		ret = -ENOMEM;
		goto eclkdevalloc;
	}
	clkdev_add(siumckb_lookup);

	/* Port number used on this machine: port B */
	migor_snd_device = platform_device_alloc("soc-audio", 1);
	if (!migor_snd_device) {
		ret = -ENOMEM;
		goto epdevalloc;
	}

	platform_set_drvdata(migor_snd_device, &snd_soc_migor);

	ret = platform_device_add(migor_snd_device);
	if (ret)
		goto epdevadd;

	return 0;

epdevadd:
	platform_device_put(migor_snd_device);
epdevalloc:
	clkdev_drop(siumckb_lookup);
eclkdevalloc:
	clk_unregister(&siumckb_clk);
	return ret;
}

static void __exit migor_exit(void)
{
	clkdev_drop(siumckb_lookup);
	clk_unregister(&siumckb_clk);
	platform_device_unregister(migor_snd_device);
}

module_init(migor_init);
module_exit(migor_exit);

MODULE_AUTHOR("Guennadi Liakhovetski <g.liakhovetski@gmx.de>");
MODULE_DESCRIPTION("ALSA SoC Migor");
MODULE_LICENSE("GPL v2");
