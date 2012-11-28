/*
 * Bells audio support
 *
 * Copyright 2012 Wolfson Microelectronics
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

#include "../codecs/wm5102.h"
#include "../codecs/wm9081.h"

/*
 * 44.1kHz based clocks for the SYSCLK domain, use a very high clock
 * to allow all the DSP functionality to be enabled if desired.
 */
#define SYSCLK_RATE (44100 * 1024)

/* 48kHz based clocks for the ASYNC domain */
#define ASYNCCLK_RATE (48000 * 512)

/* BCLK2 is fixed at this currently */
#define BCLK2_RATE (64 * 8000)

/*
 * Expect a 24.576MHz crystal if one is fitted (the driver will function
 * if this is not fitted).
 */
#define MCLK_RATE 24576000

#define WM9081_AUDIO_RATE 44100
#define WM9081_MCLK_RATE  (WM9081_AUDIO_RATE * 256)

static int bells_set_bias_level(struct snd_soc_card *card,
				struct snd_soc_dapm_context *dapm,
				enum snd_soc_bias_level level)
{
	struct snd_soc_dai *codec_dai = card->rtd[0].codec_dai;
	struct snd_soc_codec *codec = codec_dai->codec;
	int ret;

	if (dapm->dev != codec_dai->dev)
		return 0;

	switch (level) {
	case SND_SOC_BIAS_PREPARE:
		if (dapm->bias_level == SND_SOC_BIAS_STANDBY) {
			ret = snd_soc_codec_set_pll(codec, WM5102_FLL1,
						    ARIZONA_FLL_SRC_MCLK1,
						    MCLK_RATE,
						    SYSCLK_RATE);
			if (ret < 0)
				pr_err("Failed to start FLL: %d\n", ret);

			ret = snd_soc_codec_set_pll(codec, WM5102_FLL2,
						    ARIZONA_FLL_SRC_AIF2BCLK,
						    BCLK2_RATE,
						    ASYNCCLK_RATE);
			if (ret < 0)
				pr_err("Failed to start FLL: %d\n", ret);
		}
		break;

	default:
		break;
	}

	return 0;
}

static int bells_set_bias_level_post(struct snd_soc_card *card,
				     struct snd_soc_dapm_context *dapm,
				     enum snd_soc_bias_level level)
{
	struct snd_soc_dai *codec_dai = card->rtd[0].codec_dai;
	struct snd_soc_codec *codec = codec_dai->codec;
	int ret;

	if (dapm->dev != codec_dai->dev)
		return 0;

	switch (level) {
	case SND_SOC_BIAS_STANDBY:
		ret = snd_soc_codec_set_pll(codec, WM5102_FLL1, 0, 0, 0);
		if (ret < 0) {
			pr_err("Failed to stop FLL: %d\n", ret);
			return ret;
		}

		ret = snd_soc_codec_set_pll(codec, WM5102_FLL2, 0, 0, 0);
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

static int bells_late_probe(struct snd_soc_card *card)
{
	struct snd_soc_codec *codec = card->rtd[0].codec;
	struct snd_soc_dai *aif1_dai = card->rtd[0].codec_dai;
	struct snd_soc_dai *aif2_dai = card->rtd[1].cpu_dai;
	struct snd_soc_dai *aif3_dai = card->rtd[2].cpu_dai;
	struct snd_soc_dai *wm9081_dai = card->rtd[2].codec_dai;
	int ret;

	ret = snd_soc_dai_set_sysclk(aif1_dai, ARIZONA_CLK_SYSCLK, 0, 0);
	if (ret != 0) {
		dev_err(aif1_dai->dev, "Failed to set AIF1 clock: %d\n", ret);
		return ret;
	}

	ret = snd_soc_dai_set_sysclk(aif2_dai, ARIZONA_CLK_ASYNCCLK, 0, 0);
	if (ret != 0) {
		dev_err(aif2_dai->dev, "Failed to set AIF2 clock: %d\n", ret);
		return ret;
	}

	ret = snd_soc_dai_set_sysclk(aif3_dai, ARIZONA_CLK_SYSCLK, 0, 0);
	if (ret != 0) {
		dev_err(aif1_dai->dev, "Failed to set AIF1 clock: %d\n", ret);
		return ret;
	}

	ret = snd_soc_codec_set_sysclk(codec, ARIZONA_CLK_SYSCLK,
				       ARIZONA_CLK_SRC_FLL1, SYSCLK_RATE,
				       SND_SOC_CLOCK_IN);
	if (ret != 0) {
		dev_err(codec->dev, "Failed to set SYSCLK: %d\n", ret);
		return ret;
	}

	ret = snd_soc_codec_set_sysclk(codec, ARIZONA_CLK_OPCLK, 0,
				       WM9081_MCLK_RATE, SND_SOC_CLOCK_OUT);
	if (ret != 0) {
		dev_err(codec->dev, "Failed to set OPCLK: %d\n", ret);
		return ret;
	}

	ret = snd_soc_codec_set_sysclk(codec, ARIZONA_CLK_ASYNCCLK,
				       ARIZONA_CLK_SRC_FLL2, ASYNCCLK_RATE,
				       SND_SOC_CLOCK_IN);
	if (ret != 0) {
		dev_err(codec->dev, "Failed to set SYSCLK: %d\n", ret);
		return ret;
	}

	ret = snd_soc_codec_set_sysclk(wm9081_dai->codec, WM9081_SYSCLK_MCLK,
				       0, WM9081_MCLK_RATE, 0);
	if (ret != 0) {
		dev_err(wm9081_dai->dev, "Failed to set MCLK: %d\n", ret);
		return ret;
	}

	return 0;
}

static const struct snd_soc_pcm_stream baseband_params = {
	.formats = SNDRV_PCM_FMTBIT_S32_LE,
	.rate_min = 8000,
	.rate_max = 8000,
	.channels_min = 2,
	.channels_max = 2,
};

static const struct snd_soc_pcm_stream sub_params = {
	.formats = SNDRV_PCM_FMTBIT_S32_LE,
	.rate_min = WM9081_AUDIO_RATE,
	.rate_max = WM9081_AUDIO_RATE,
	.channels_min = 2,
	.channels_max = 2,
};

static struct snd_soc_dai_link bells_dai_wm5102[] = {
	{
		.name = "CPU",
		.stream_name = "CPU",
		.cpu_dai_name = "samsung-i2s.0",
		.codec_dai_name = "wm5102-aif1",
		.platform_name = "samsung-audio",
		.codec_name = "wm5102-codec",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF
				| SND_SOC_DAIFMT_CBM_CFM,
	},
	{
		.name = "Baseband",
		.stream_name = "Baseband",
		.cpu_dai_name = "wm5102-aif2",
		.codec_dai_name = "wm1250-ev1",
		.codec_name = "wm1250-ev1.1-0027",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF
				| SND_SOC_DAIFMT_CBM_CFM,
		.ignore_suspend = 1,
		.params = &baseband_params,
	},
	{
		.name = "Sub",
		.stream_name = "Sub",
		.cpu_dai_name = "wm5102-aif3",
		.codec_dai_name = "wm9081-hifi",
		.codec_name = "wm9081.1-006c",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF
				| SND_SOC_DAIFMT_CBS_CFS,
		.ignore_suspend = 1,
		.params = &sub_params,
	},
};

static struct snd_soc_dai_link bells_dai_wm5110[] = {
	{
		.name = "CPU",
		.stream_name = "CPU",
		.cpu_dai_name = "samsung-i2s.0",
		.codec_dai_name = "wm5110-aif1",
		.platform_name = "samsung-audio",
		.codec_name = "wm5110-codec",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF
				| SND_SOC_DAIFMT_CBM_CFM,
	},
	{
		.name = "Baseband",
		.stream_name = "Baseband",
		.cpu_dai_name = "wm5110-aif2",
		.codec_dai_name = "wm1250-ev1",
		.codec_name = "wm1250-ev1.1-0027",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF
				| SND_SOC_DAIFMT_CBM_CFM,
		.ignore_suspend = 1,
		.params = &baseband_params,
	},
	{
		.name = "Sub",
		.stream_name = "Sub",
		.cpu_dai_name = "wm5110-aif3",
		.codec_dai_name = "wm9081-hifi",
		.codec_name = "wm9081.1-006c",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF
				| SND_SOC_DAIFMT_CBS_CFS,
		.ignore_suspend = 1,
		.params = &sub_params,
	},
};

static struct snd_soc_codec_conf bells_codec_conf[] = {
	{
		.dev_name = "wm9081.1-006c",
		.name_prefix = "Sub",
	},
};

static struct snd_soc_dapm_route bells_routes[] = {
	{ "Sub CLK_SYS", NULL, "OPCLK" },
};

static struct snd_soc_card bells_cards[] = {
	{
		.name = "Bells WM5102",
		.owner = THIS_MODULE,
		.dai_link = bells_dai_wm5102,
		.num_links = ARRAY_SIZE(bells_dai_wm5102),
		.codec_conf = bells_codec_conf,
		.num_configs = ARRAY_SIZE(bells_codec_conf),

		.late_probe = bells_late_probe,

		.dapm_routes = bells_routes,
		.num_dapm_routes = ARRAY_SIZE(bells_routes),

		.set_bias_level = bells_set_bias_level,
		.set_bias_level_post = bells_set_bias_level_post,
	},
	{
		.name = "Bells WM5110",
		.owner = THIS_MODULE,
		.dai_link = bells_dai_wm5110,
		.num_links = ARRAY_SIZE(bells_dai_wm5110),
		.codec_conf = bells_codec_conf,
		.num_configs = ARRAY_SIZE(bells_codec_conf),

		.late_probe = bells_late_probe,

		.dapm_routes = bells_routes,
		.num_dapm_routes = ARRAY_SIZE(bells_routes),

		.set_bias_level = bells_set_bias_level,
		.set_bias_level_post = bells_set_bias_level_post,
	},
};


static __devinit int bells_probe(struct platform_device *pdev)
{
	int ret;

	bells_cards[pdev->id].dev = &pdev->dev;

	ret = snd_soc_register_card(&bells_cards[pdev->id]);
	if (ret) {
		dev_err(&pdev->dev,
			"snd_soc_register_card(%s) failed: %d\n",
			bells_cards[pdev->id].name, ret);
		return ret;
	}

	return 0;
}

static int __devexit bells_remove(struct platform_device *pdev)
{
	snd_soc_unregister_card(&bells_cards[pdev->id]);

	return 0;
}

static struct platform_driver bells_driver = {
	.driver = {
		.name = "bells",
		.owner = THIS_MODULE,
		.pm = &snd_soc_pm_ops,
	},
	.probe = bells_probe,
	.remove = __devexit_p(bells_remove),
};

module_platform_driver(bells_driver);

MODULE_DESCRIPTION("Bells audio support");
MODULE_AUTHOR("Mark Brown <broonie@opensource.wolfsonmicro.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:bells");
