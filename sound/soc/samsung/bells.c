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

/* BCLK2 is fixed at this currently */
#define BCLK2_RATE (64 * 8000)

/*
 * Expect a 24.576MHz crystal if one is fitted (the driver will function
 * if this is not fitted).
 */
#define MCLK_RATE 24576000

#define SYS_AUDIO_RATE 44100
#define SYS_MCLK_RATE  (SYS_AUDIO_RATE * 512)

#define DAI_AP_DSP    0
#define DAI_DSP_CODEC 1
#define DAI_CODEC_CP  2
#define DAI_CODEC_SUB 3

struct bells_drvdata {
	int sysclk_rate;
	int asyncclk_rate;
};

static struct bells_drvdata wm2200_drvdata = {
	.sysclk_rate = 22579200,
};

static struct bells_drvdata wm5102_drvdata = {
	.sysclk_rate = 45158400,
	.asyncclk_rate = 49152000,
};

static struct bells_drvdata wm5110_drvdata = {
	.sysclk_rate = 135475200,
	.asyncclk_rate = 147456000,
};

static int bells_set_bias_level(struct snd_soc_card *card,
				struct snd_soc_dapm_context *dapm,
				enum snd_soc_bias_level level)
{
	struct snd_soc_dai *codec_dai = card->rtd[DAI_DSP_CODEC].codec_dai;
	struct snd_soc_codec *codec = codec_dai->codec;
	struct bells_drvdata *bells = card->drvdata;
	int ret;

	if (dapm->dev != codec_dai->dev)
		return 0;

	switch (level) {
	case SND_SOC_BIAS_PREPARE:
		if (dapm->bias_level != SND_SOC_BIAS_STANDBY)
			break;

		ret = snd_soc_codec_set_pll(codec, WM5102_FLL1,
					    ARIZONA_FLL_SRC_MCLK1,
					    MCLK_RATE,
					    bells->sysclk_rate);
		if (ret < 0)
			pr_err("Failed to start FLL: %d\n", ret);

		if (bells->asyncclk_rate) {
			ret = snd_soc_codec_set_pll(codec, WM5102_FLL2,
						    ARIZONA_FLL_SRC_AIF2BCLK,
						    BCLK2_RATE,
						    bells->asyncclk_rate);
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
	struct snd_soc_dai *codec_dai = card->rtd[DAI_DSP_CODEC].codec_dai;
	struct snd_soc_codec *codec = codec_dai->codec;
	struct bells_drvdata *bells = card->drvdata;
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

		if (bells->asyncclk_rate) {
			ret = snd_soc_codec_set_pll(codec, WM5102_FLL2,
						    0, 0, 0);
			if (ret < 0) {
				pr_err("Failed to stop FLL: %d\n", ret);
				return ret;
			}
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
	struct bells_drvdata *bells = card->drvdata;
	struct snd_soc_codec *wm0010 = card->rtd[DAI_AP_DSP].codec;
	struct snd_soc_codec *codec = card->rtd[DAI_DSP_CODEC].codec;
	struct snd_soc_dai *aif1_dai = card->rtd[DAI_DSP_CODEC].codec_dai;
	struct snd_soc_dai *aif2_dai;
	struct snd_soc_dai *aif3_dai;
	struct snd_soc_dai *wm9081_dai;
	int ret;

	ret = snd_soc_codec_set_sysclk(codec, ARIZONA_CLK_SYSCLK,
				       ARIZONA_CLK_SRC_FLL1,
				       bells->sysclk_rate,
				       SND_SOC_CLOCK_IN);
	if (ret != 0) {
		dev_err(codec->dev, "Failed to set SYSCLK: %d\n", ret);
		return ret;
	}

	ret = snd_soc_codec_set_sysclk(wm0010, 0, 0, SYS_MCLK_RATE, 0);
	if (ret != 0) {
		dev_err(wm0010->dev, "Failed to set WM0010 clock: %d\n", ret);
		return ret;
	}

	ret = snd_soc_dai_set_sysclk(aif1_dai, ARIZONA_CLK_SYSCLK, 0, 0);
	if (ret != 0)
		dev_err(aif1_dai->dev, "Failed to set AIF1 clock: %d\n", ret);

	ret = snd_soc_codec_set_sysclk(codec, ARIZONA_CLK_OPCLK, 0,
				       SYS_MCLK_RATE, SND_SOC_CLOCK_OUT);
	if (ret != 0)
		dev_err(codec->dev, "Failed to set OPCLK: %d\n", ret);

	if (card->num_rtd == DAI_CODEC_CP)
		return 0;

	ret = snd_soc_codec_set_sysclk(codec, ARIZONA_CLK_ASYNCCLK,
				       ARIZONA_CLK_SRC_FLL2,
				       bells->asyncclk_rate,
				       SND_SOC_CLOCK_IN);
	if (ret != 0) {
		dev_err(codec->dev, "Failed to set ASYNCCLK: %d\n", ret);
		return ret;
	}

	aif2_dai = card->rtd[DAI_CODEC_CP].cpu_dai;

	ret = snd_soc_dai_set_sysclk(aif2_dai, ARIZONA_CLK_ASYNCCLK, 0, 0);
	if (ret != 0) {
		dev_err(aif2_dai->dev, "Failed to set AIF2 clock: %d\n", ret);
		return ret;
	}

	if (card->num_rtd == DAI_CODEC_SUB)
		return 0;

	aif3_dai = card->rtd[DAI_CODEC_SUB].cpu_dai;
	wm9081_dai = card->rtd[DAI_CODEC_SUB].codec_dai;

	ret = snd_soc_dai_set_sysclk(aif3_dai, ARIZONA_CLK_SYSCLK, 0, 0);
	if (ret != 0) {
		dev_err(aif1_dai->dev, "Failed to set AIF1 clock: %d\n", ret);
		return ret;
	}

	ret = snd_soc_codec_set_sysclk(wm9081_dai->codec, WM9081_SYSCLK_MCLK,
				       0, SYS_MCLK_RATE, 0);
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
	.rate_min = SYS_AUDIO_RATE,
	.rate_max = SYS_AUDIO_RATE,
	.channels_min = 2,
	.channels_max = 2,
};

static struct snd_soc_dai_link bells_dai_wm2200[] = {
	{
		.name = "CPU-DSP",
		.stream_name = "CPU-DSP",
		.cpu_dai_name = "samsung-i2s.0",
		.codec_dai_name = "wm0010-sdi1",
		.platform_name = "samsung-i2s.0",
		.codec_name = "spi0.0",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF
				| SND_SOC_DAIFMT_CBM_CFM,
	},
	{
		.name = "DSP-CODEC",
		.stream_name = "DSP-CODEC",
		.cpu_dai_name = "wm0010-sdi2",
		.codec_dai_name = "wm2200",
		.codec_name = "wm2200.1-003a",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF
				| SND_SOC_DAIFMT_CBM_CFM,
		.params = &sub_params,
		.ignore_suspend = 1,
	},
};

static struct snd_soc_dai_link bells_dai_wm5102[] = {
	{
		.name = "CPU-DSP",
		.stream_name = "CPU-DSP",
		.cpu_dai_name = "samsung-i2s.0",
		.codec_dai_name = "wm0010-sdi1",
		.platform_name = "samsung-i2s.0",
		.codec_name = "spi0.0",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF
				| SND_SOC_DAIFMT_CBM_CFM,
	},
	{
		.name = "DSP-CODEC",
		.stream_name = "DSP-CODEC",
		.cpu_dai_name = "wm0010-sdi2",
		.codec_dai_name = "wm5102-aif1",
		.codec_name = "wm5102-codec",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF
				| SND_SOC_DAIFMT_CBM_CFM,
		.params = &sub_params,
		.ignore_suspend = 1,
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
		.name = "CPU-DSP",
		.stream_name = "CPU-DSP",
		.cpu_dai_name = "samsung-i2s.0",
		.codec_dai_name = "wm0010-sdi1",
		.platform_name = "samsung-i2s.0",
		.codec_name = "spi0.0",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF
				| SND_SOC_DAIFMT_CBM_CFM,
	},
	{
		.name = "DSP-CODEC",
		.stream_name = "DSP-CODEC",
		.cpu_dai_name = "wm0010-sdi2",
		.codec_dai_name = "wm5110-aif1",
		.codec_name = "wm5110-codec",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF
				| SND_SOC_DAIFMT_CBM_CFM,
		.params = &sub_params,
		.ignore_suspend = 1,
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

static struct snd_soc_dapm_widget bells_widgets[] = {
	SND_SOC_DAPM_MIC("DMIC", NULL),
};

static struct snd_soc_dapm_route bells_routes[] = {
	{ "Sub CLK_SYS", NULL, "OPCLK" },
	{ "CLKIN", NULL, "OPCLK" },

	{ "DMIC", NULL, "MICBIAS2" },
	{ "IN2L", NULL, "DMIC" },
	{ "IN2R", NULL, "DMIC" },
};

static struct snd_soc_card bells_cards[] = {
	{
		.name = "Bells WM2200",
		.owner = THIS_MODULE,
		.dai_link = bells_dai_wm2200,
		.num_links = ARRAY_SIZE(bells_dai_wm2200),
		.codec_conf = bells_codec_conf,
		.num_configs = ARRAY_SIZE(bells_codec_conf),

		.late_probe = bells_late_probe,

		.dapm_widgets = bells_widgets,
		.num_dapm_widgets = ARRAY_SIZE(bells_widgets),
		.dapm_routes = bells_routes,
		.num_dapm_routes = ARRAY_SIZE(bells_routes),

		.set_bias_level = bells_set_bias_level,
		.set_bias_level_post = bells_set_bias_level_post,

		.drvdata = &wm2200_drvdata,
	},
	{
		.name = "Bells WM5102",
		.owner = THIS_MODULE,
		.dai_link = bells_dai_wm5102,
		.num_links = ARRAY_SIZE(bells_dai_wm5102),
		.codec_conf = bells_codec_conf,
		.num_configs = ARRAY_SIZE(bells_codec_conf),

		.late_probe = bells_late_probe,

		.dapm_widgets = bells_widgets,
		.num_dapm_widgets = ARRAY_SIZE(bells_widgets),
		.dapm_routes = bells_routes,
		.num_dapm_routes = ARRAY_SIZE(bells_routes),

		.set_bias_level = bells_set_bias_level,
		.set_bias_level_post = bells_set_bias_level_post,

		.drvdata = &wm5102_drvdata,
	},
	{
		.name = "Bells WM5110",
		.owner = THIS_MODULE,
		.dai_link = bells_dai_wm5110,
		.num_links = ARRAY_SIZE(bells_dai_wm5110),
		.codec_conf = bells_codec_conf,
		.num_configs = ARRAY_SIZE(bells_codec_conf),

		.late_probe = bells_late_probe,

		.dapm_widgets = bells_widgets,
		.num_dapm_widgets = ARRAY_SIZE(bells_widgets),
		.dapm_routes = bells_routes,
		.num_dapm_routes = ARRAY_SIZE(bells_routes),

		.set_bias_level = bells_set_bias_level,
		.set_bias_level_post = bells_set_bias_level_post,

		.drvdata = &wm5110_drvdata,
	},
};


static int bells_probe(struct platform_device *pdev)
{
	int ret;

	bells_cards[pdev->id].dev = &pdev->dev;

	ret = devm_snd_soc_register_card(&pdev->dev, &bells_cards[pdev->id]);
	if (ret)
		dev_err(&pdev->dev,
			"snd_soc_register_card(%s) failed: %d\n",
			bells_cards[pdev->id].name, ret);

	return ret;
}

static struct platform_driver bells_driver = {
	.driver = {
		.name = "bells",
		.owner = THIS_MODULE,
		.pm = &snd_soc_pm_ops,
	},
	.probe = bells_probe,
};

module_platform_driver(bells_driver);

MODULE_DESCRIPTION("Bells audio support");
MODULE_AUTHOR("Mark Brown <broonie@opensource.wolfsonmicro.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:bells");
