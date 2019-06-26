// SPDX-License-Identifier: GPL-2.0
//
// Copyright 2007,2008 Simtec Electronics
//
// Based on sound/soc/pxa/spitz.c
//	Copyright 2005 Wolfson Microelectronics PLC.
//	Copyright 2005 Openedhand Ltd.

#include <linux/module.h>
#include <sound/soc.h>

#include <asm/mach-types.h>

#include "s3c2412-i2s.h"
#include "../codecs/wm8750.h"

static const struct snd_soc_dapm_route audio_map[] = {
	{ "Headphone Jack", NULL, "LOUT1" },
	{ "Headphone Jack", NULL, "ROUT1" },
	{ "Internal Speaker", NULL, "LOUT2" },
	{ "Internal Speaker", NULL, "ROUT2" },
	{ "LINPUT1", NULL, "Line Input" },
	{ "RINPUT1", NULL, "Line Input" },
};

static const struct snd_soc_dapm_widget wm8750_dapm_widgets[] = {
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_SPK("Internal Speaker", NULL),
	SND_SOC_DAPM_LINE("Line In", NULL),
};

static int jive_hw_params(struct snd_pcm_substream *substream,
			  struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct s3c_i2sv2_rate_calc div;
	unsigned int clk = 0;
	int ret = 0;

	switch (params_rate(params)) {
	case 8000:
	case 16000:
	case 48000:
	case 96000:
		clk = 12288000;
		break;
	case 11025:
	case 22050:
	case 44100:
		clk = 11289600;
		break;
	}

	s3c_i2sv2_iis_calc_rate(&div, NULL, params_rate(params),
				s3c_i2sv2_get_clock(cpu_dai));

	/* set the codec system clock for DAC and ADC */
	ret = snd_soc_dai_set_sysclk(codec_dai, WM8750_SYSCLK, clk,
				     SND_SOC_CLOCK_IN);
	if (ret < 0)
		return ret;

	ret = snd_soc_dai_set_clkdiv(cpu_dai, S3C2412_DIV_RCLK, div.fs_div);
	if (ret < 0)
		return ret;

	ret = snd_soc_dai_set_clkdiv(cpu_dai, S3C2412_DIV_PRESCALER,
				     div.clk_div - 1);
	if (ret < 0)
		return ret;

	return 0;
}

static const struct snd_soc_ops jive_ops = {
	.hw_params	= jive_hw_params,
};

static struct snd_soc_dai_link jive_dai = {
	.name		= "wm8750",
	.stream_name	= "WM8750",
	.cpu_dai_name	= "s3c2412-i2s",
	.codec_dai_name = "wm8750-hifi",
	.platform_name	= "s3c2412-i2s",
	.codec_name	= "wm8750.0-001a",
	.dai_fmt	= SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
			  SND_SOC_DAIFMT_CBS_CFS,
	.ops		= &jive_ops,
};

/* jive audio machine driver */
static struct snd_soc_card snd_soc_machine_jive = {
	.name		= "Jive",
	.owner		= THIS_MODULE,
	.dai_link	= &jive_dai,
	.num_links	= 1,

	.dapm_widgets	= wm8750_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(wm8750_dapm_widgets),
	.dapm_routes	= audio_map,
	.num_dapm_routes = ARRAY_SIZE(audio_map),
	.fully_routed	= true,
};

static struct platform_device *jive_snd_device;

static int __init jive_init(void)
{
	int ret;

	if (!machine_is_jive())
		return 0;

	printk("JIVE WM8750 Audio support\n");

	jive_snd_device = platform_device_alloc("soc-audio", -1);
	if (!jive_snd_device)
		return -ENOMEM;

	platform_set_drvdata(jive_snd_device, &snd_soc_machine_jive);
	ret = platform_device_add(jive_snd_device);

	if (ret)
		platform_device_put(jive_snd_device);

	return ret;
}

static void __exit jive_exit(void)
{
	platform_device_unregister(jive_snd_device);
}

module_init(jive_init);
module_exit(jive_exit);

MODULE_AUTHOR("Ben Dooks <ben@simtec.co.uk>");
MODULE_DESCRIPTION("ALSA SoC Jive Audio support");
MODULE_LICENSE("GPL");
