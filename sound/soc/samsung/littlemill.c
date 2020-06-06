// SPDX-License-Identifier: GPL-2.0+
//
// Littlemill audio support
//
// Copyright 2011 Wolfson Microelectronics

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
	struct snd_soc_pcm_runtime *rtd;
	struct snd_soc_dai *aif1_dai;
	int ret;

	rtd = snd_soc_get_pcm_runtime(card, &card->dai_link[0]);
	aif1_dai = rtd->codec_dai;

	if (dapm->dev != aif1_dai->dev)
		return 0;

	switch (level) {
	case SND_SOC_BIAS_PREPARE:
		/*
		 * If we've not already clocked things via hw_params()
		 * then do so now, otherwise these are noops.
		 */
		if (dapm->bias_level == SND_SOC_BIAS_STANDBY) {
			ret = snd_soc_dai_set_pll(aif1_dai, WM8994_FLL1,
						  WM8994_FLL_SRC_MCLK2, 32768,
						  sample_rate * 512);
			if (ret < 0) {
				pr_err("Failed to start FLL: %d\n", ret);
				return ret;
			}

			ret = snd_soc_dai_set_sysclk(aif1_dai,
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
	struct snd_soc_pcm_runtime *rtd;
	struct snd_soc_dai *aif1_dai;
	int ret;

	rtd = snd_soc_get_pcm_runtime(card, &card->dai_link[0]);
	aif1_dai = rtd->codec_dai;

	if (dapm->dev != aif1_dai->dev)
		return 0;

	switch (level) {
	case SND_SOC_BIAS_STANDBY:
		ret = snd_soc_dai_set_sysclk(aif1_dai, WM8994_SYSCLK_MCLK2,
					     32768, SND_SOC_CLOCK_IN);
		if (ret < 0) {
			pr_err("Failed to switch away from FLL1: %d\n", ret);
			return ret;
		}

		ret = snd_soc_dai_set_pll(aif1_dai, WM8994_FLL1,
					  0, 0, 0);
		if (ret < 0) {
			pr_err("Failed to stop FLL1: %d\n", ret);
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

static const struct snd_soc_pcm_stream baseband_params = {
	.formats = SNDRV_PCM_FMTBIT_S32_LE,
	.rate_min = 8000,
	.rate_max = 8000,
	.channels_min = 2,
	.channels_max = 2,
};

SND_SOC_DAILINK_DEFS(cpu,
	DAILINK_COMP_ARRAY(COMP_CPU("samsung-i2s.0")),
	DAILINK_COMP_ARRAY(COMP_CODEC("wm8994-codec", "wm8994-aif1")),
	DAILINK_COMP_ARRAY(COMP_PLATFORM("samsung-i2s.0")));

SND_SOC_DAILINK_DEFS(baseband,
	DAILINK_COMP_ARRAY(COMP_CPU("wm8994-aif2")),
	DAILINK_COMP_ARRAY(COMP_CODEC("wm1250-ev1.1-0027",
				      "wm1250-ev1")));

static struct snd_soc_dai_link littlemill_dai[] = {
	{
		.name = "CPU",
		.stream_name = "CPU",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF
				| SND_SOC_DAIFMT_CBM_CFM,
		.ops = &littlemill_ops,
		SND_SOC_DAILINK_REG(cpu),
	},
	{
		.name = "Baseband",
		.stream_name = "Baseband",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF
				| SND_SOC_DAIFMT_CBM_CFM,
		.ignore_suspend = 1,
		.params = &baseband_params,
		SND_SOC_DAILINK_REG(baseband),
	},
};

static int bbclk_ev(struct snd_soc_dapm_widget *w,
		    struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_card *card = w->dapm->card;
	struct snd_soc_pcm_runtime *rtd;
	struct snd_soc_dai *aif2_dai;
	int ret;

	rtd = snd_soc_get_pcm_runtime(card, &card->dai_link[1]);
	aif2_dai = rtd->cpu_dai;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		ret = snd_soc_dai_set_pll(aif2_dai, WM8994_FLL2,
					  WM8994_FLL_SRC_BCLK, 64 * 8000,
					  8000 * 256);
		if (ret < 0) {
			pr_err("Failed to start FLL: %d\n", ret);
			return ret;
		}

		ret = snd_soc_dai_set_sysclk(aif2_dai, WM8994_SYSCLK_FLL2,
					     8000 * 256,
					     SND_SOC_CLOCK_IN);
		if (ret < 0) {
			pr_err("Failed to set SYSCLK: %d\n", ret);
			return ret;
		}
		break;
	case SND_SOC_DAPM_POST_PMD:
		ret = snd_soc_dai_set_sysclk(aif2_dai, WM8994_SYSCLK_MCLK2,
					     32768, SND_SOC_CLOCK_IN);
		if (ret < 0) {
			pr_err("Failed to switch away from FLL2: %d\n", ret);
			return ret;
		}

		ret = snd_soc_dai_set_pll(aif2_dai, WM8994_FLL2,
					  0, 0, 0);
		if (ret < 0) {
			pr_err("Failed to stop FLL2: %d\n", ret);
			return ret;
		}
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct snd_kcontrol_new controls[] = {
	SOC_DAPM_PIN_SWITCH("WM1250 Input"),
	SOC_DAPM_PIN_SWITCH("WM1250 Output"),
};

static struct snd_soc_dapm_widget widgets[] = {
	SND_SOC_DAPM_HP("Headphone", NULL),

	SND_SOC_DAPM_MIC("AMIC", NULL),
	SND_SOC_DAPM_MIC("DMIC", NULL),

	SND_SOC_DAPM_SUPPLY_S("Baseband Clock", -1, SND_SOC_NOPM, 0, 0,
			      bbclk_ev,
			      SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
};

static struct snd_soc_dapm_route audio_paths[] = {
	{ "Headphone", NULL, "HPOUT1L" },
	{ "Headphone", NULL, "HPOUT1R" },

	{ "AMIC", NULL, "MICBIAS1" },   /* Default for AMICBIAS jumper */
	{ "IN1LN", NULL, "AMIC" },

	{ "DMIC", NULL, "MICBIAS2" },   /* Default for DMICBIAS jumper */
	{ "DMIC1DAT", NULL, "DMIC" },
	{ "DMIC2DAT", NULL, "DMIC" },

	{ "AIF2CLK", NULL, "Baseband Clock" },
};

static struct snd_soc_jack littlemill_headset;

static int littlemill_late_probe(struct snd_soc_card *card)
{
	struct snd_soc_pcm_runtime *rtd;
	struct snd_soc_component *component;
	struct snd_soc_dai *aif1_dai;
	struct snd_soc_dai *aif2_dai;
	int ret;

	rtd = snd_soc_get_pcm_runtime(card, &card->dai_link[0]);
	component = rtd->codec_dai->component;
	aif1_dai = rtd->codec_dai;

	rtd = snd_soc_get_pcm_runtime(card, &card->dai_link[1]);
	aif2_dai = rtd->cpu_dai;

	ret = snd_soc_dai_set_sysclk(aif1_dai, WM8994_SYSCLK_MCLK2,
				     32768, SND_SOC_CLOCK_IN);
	if (ret < 0)
		return ret;

	ret = snd_soc_dai_set_sysclk(aif2_dai, WM8994_SYSCLK_MCLK2,
				     32768, SND_SOC_CLOCK_IN);
	if (ret < 0)
		return ret;

	ret = snd_soc_card_jack_new(card, "Headset",
				    SND_JACK_HEADSET | SND_JACK_MECHANICAL |
				    SND_JACK_BTN_0 | SND_JACK_BTN_1 |
				    SND_JACK_BTN_2 | SND_JACK_BTN_3 |
				    SND_JACK_BTN_4 | SND_JACK_BTN_5,
				    &littlemill_headset, NULL, 0);
	if (ret)
		return ret;

	/* This will check device compatibility itself */
	wm8958_mic_detect(component, &littlemill_headset, NULL, NULL, NULL, NULL);

	/* As will this */
	wm8994_mic_detect(component, &littlemill_headset, 1);

	return 0;
}

static struct snd_soc_card littlemill = {
	.name = "Littlemill",
	.owner = THIS_MODULE,
	.dai_link = littlemill_dai,
	.num_links = ARRAY_SIZE(littlemill_dai),

	.set_bias_level = littlemill_set_bias_level,
	.set_bias_level_post = littlemill_set_bias_level_post,

	.controls = controls,
	.num_controls = ARRAY_SIZE(controls),
	.dapm_widgets = widgets,
	.num_dapm_widgets = ARRAY_SIZE(widgets),
	.dapm_routes = audio_paths,
	.num_dapm_routes = ARRAY_SIZE(audio_paths),

	.late_probe = littlemill_late_probe,
};

static int littlemill_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = &littlemill;
	int ret;

	card->dev = &pdev->dev;

	ret = devm_snd_soc_register_card(&pdev->dev, card);
	if (ret)
		dev_err(&pdev->dev, "snd_soc_register_card() failed: %d\n",
			ret);

	return ret;
}

static struct platform_driver littlemill_driver = {
	.driver = {
		.name = "littlemill",
		.pm = &snd_soc_pm_ops,
	},
	.probe = littlemill_probe,
};

module_platform_driver(littlemill_driver);

MODULE_DESCRIPTION("Littlemill audio support");
MODULE_AUTHOR("Mark Brown <broonie@opensource.wolfsonmicro.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:littlemill");
