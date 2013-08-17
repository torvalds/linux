/*
 * Littlemill audio support
 *
 * Copyright 2011 Wolfson Microelectronics
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/jack.h>
#include <linux/gpio.h>
#include <linux/module.h>

#include "../codecs/wm8994.h"

static int sample_rate = 44100;

static int littlemill_set_bias_level(struct snd_soc_card *card,
					  struct snd_soc_dapm_context *dapm,
					  enum snd_soc_bias_level level)
{
	struct snd_soc_dai *codec_dai = card->rtd[0].codec_dai;
	int ret;

	if (dapm->dev != codec_dai->dev)
		return 0;

	switch (level) {
	case SND_SOC_BIAS_PREPARE:
		/*
		 * If we've not already clocked things via hw_params()
		 * then do so now, otherwise these are noops.
		 */
		if (dapm->bias_level == SND_SOC_BIAS_STANDBY) {
			ret = snd_soc_dai_set_pll(codec_dai, WM8994_FLL1,
						  WM8994_FLL_SRC_MCLK2, 32768,
						  sample_rate * 512);
			if (ret < 0) {
				pr_err("Failed to start FLL: %d\n", ret);
				return ret;
			}

			ret = snd_soc_dai_set_sysclk(codec_dai,
						     WM8994_SYSCLK_FLL1,
						     sample_rate * 512,
						     SND_SOC_CLOCK_IN);
			if (ret < 0) {
				pr_err("Failed to set SYSCLK: %d\n", ret);
				return ret;
			}
		}
		break;

	default:
		break;
	}

	return 0;
}

static int littlemill_set_bias_level_post(struct snd_soc_card *card,
					       struct snd_soc_dapm_context *dapm,
					       enum snd_soc_bias_level level)
{
	struct snd_soc_dai *codec_dai = card->rtd[0].codec_dai;
	int ret;

	if (dapm->dev != codec_dai->dev)
		return 0;

	switch (level) {
	case SND_SOC_BIAS_STANDBY:
		ret = snd_soc_dai_set_sysclk(codec_dai, WM8994_SYSCLK_MCLK2,
					     32768, SND_SOC_CLOCK_IN);
		if (ret < 0) {
			pr_err("Failed to switch away from FLL: %d\n", ret);
			return ret;
		}

		ret = snd_soc_dai_set_pll(codec_dai, WM8994_FLL1,
					  0, 0, 0);
		if (ret < 0) {
			pr_err("Failed to stop FLL: %d\n", ret);
			return ret;
		}
		break;

	default:
		break;
	}

	dapm->bias_level = level;

	return 0;
}

static int littlemill_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	int ret;

	sample_rate = params_rate(params);

	ret = snd_soc_dai_set_pll(codec_dai, WM8994_FLL1,
				  WM8994_FLL_SRC_MCLK2, 32768,
				  sample_rate * 512);
	if (ret < 0) {
		pr_err("Failed to start FLL: %d\n", ret);
		return ret;
	}

	ret = snd_soc_dai_set_sysclk(codec_dai,
				     WM8994_SYSCLK_FLL1,
				     sample_rate * 512,
				     SND_SOC_CLOCK_IN);
	if (ret < 0) {
		pr_err("Failed to set SYSCLK: %d\n", ret);
		return ret;
	}

	return 0;
}

static struct snd_soc_ops littlemill_ops = {
	.hw_params = littlemill_hw_params,
};

static struct snd_soc_dai_link littlemill_dai[] = {
	{
		.name = "CPU",
		.stream_name = "CPU",
		.cpu_dai_name = "samsung-i2s.0",
		.codec_dai_name = "wm8994-aif1",
		.platform_name = "samsung-audio",
		.codec_name = "wm8994-codec",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF
				| SND_SOC_DAIFMT_CBM_CFM,
		.ops = &littlemill_ops,
	},
};

static struct snd_soc_dapm_widget widgets[] = {
	SND_SOC_DAPM_HP("Headphone", NULL),

	SND_SOC_DAPM_MIC("AMIC", NULL),
	SND_SOC_DAPM_MIC("DMIC", NULL),
};

static struct snd_soc_dapm_route audio_paths[] = {
	{ "Headphone", NULL, "HPOUT1L" },
	{ "Headphone", NULL, "HPOUT1R" },

	{ "AMIC", NULL, "MICBIAS1" },   /* Default for AMICBIAS jumper */
	{ "IN1LN", NULL, "AMIC" },

	{ "DMIC", NULL, "MICBIAS2" },   /* Default for DMICBIAS jumper */
	{ "DMIC1DAT", NULL, "DMIC" },
	{ "DMIC2DAT", NULL, "DMIC" },
};

static struct snd_soc_jack littlemill_headset;

static int littlemill_late_probe(struct snd_soc_card *card)
{
	struct snd_soc_codec *codec = card->rtd[0].codec;
	struct snd_soc_dai *codec_dai = card->rtd[0].codec_dai;
	int ret;

	ret = snd_soc_dai_set_sysclk(codec_dai, WM8994_SYSCLK_MCLK2,
				     32768, SND_SOC_CLOCK_IN);
	if (ret < 0)
		return ret;

	ret = snd_soc_jack_new(codec, "Headset",
			       SND_JACK_HEADSET | SND_JACK_MECHANICAL |
			       SND_JACK_BTN_0 | SND_JACK_BTN_1 |
			       SND_JACK_BTN_2 | SND_JACK_BTN_3 |
			       SND_JACK_BTN_4 | SND_JACK_BTN_5,
			       &littlemill_headset);
	if (ret)
		return ret;

	/* This will check device compatibility itself */
	wm8958_mic_detect(codec, &littlemill_headset, NULL, NULL, NULL, NULL);

	/* As will this */
	wm8994_mic_detect(codec, &littlemill_headset, 1);

	return 0;
}

static struct snd_soc_card littlemill = {
	.name = "Littlemill",
	.owner = THIS_MODULE,
	.dai_link = littlemill_dai,
	.num_links = ARRAY_SIZE(littlemill_dai),

	.set_bias_level = littlemill_set_bias_level,
	.set_bias_level_post = littlemill_set_bias_level_post,

	.dapm_widgets = widgets,
	.num_dapm_widgets = ARRAY_SIZE(widgets),
	.dapm_routes = audio_paths,
	.num_dapm_routes = ARRAY_SIZE(audio_paths),

	.late_probe = littlemill_late_probe,
};

static __devinit int littlemill_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = &littlemill;
	int ret;

	card->dev = &pdev->dev;

	ret = snd_soc_register_card(card);
	if (ret) {
		dev_err(&pdev->dev, "snd_soc_register_card() failed: %d\n",
			ret);
		return ret;
	}

	return 0;
}

static int __devexit littlemill_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);

	snd_soc_unregister_card(card);

	return 0;
}

static struct platform_driver littlemill_driver = {
	.driver = {
		.name = "littlemill",
		.owner = THIS_MODULE,
		.pm = &snd_soc_pm_ops,
	},
	.probe = littlemill_probe,
	.remove = __devexit_p(littlemill_remove),
};

module_platform_driver(littlemill_driver);

MODULE_DESCRIPTION("Littlemill audio support");
MODULE_AUTHOR("Mark Brown <broonie@opensource.wolfsonmicro.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:littlemill");
