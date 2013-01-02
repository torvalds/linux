/*
 * Speyside audio support
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

#include "../codecs/wm8996.h"
#include "../codecs/wm9081.h"

#define WM8996_HPSEL_GPIO 214
#define MCLK_AUDIO_RATE (512 * 48000)

static int speyside_set_bias_level(struct snd_soc_card *card,
				   struct snd_soc_dapm_context *dapm,
				   enum snd_soc_bias_level level)
{
	struct snd_soc_dai *codec_dai = card->rtd[1].codec_dai;
	int ret;

	if (dapm->dev != codec_dai->dev)
		return 0;

	switch (level) {
	case SND_SOC_BIAS_STANDBY:
		ret = snd_soc_dai_set_sysclk(codec_dai, WM8996_SYSCLK_MCLK2,
					     32768, SND_SOC_CLOCK_IN);
		if (ret < 0)
			return ret;

		ret = snd_soc_dai_set_pll(codec_dai, WM8996_FLL_MCLK2,
					  0, 0, 0);
		if (ret < 0) {
			pr_err("Failed to stop FLL\n");
			return ret;
		}
		break;

	default:
		break;
	}

	return 0;
}

static int speyside_set_bias_level_post(struct snd_soc_card *card,
					struct snd_soc_dapm_context *dapm,
					enum snd_soc_bias_level level)
{
	struct snd_soc_dai *codec_dai = card->rtd[1].codec_dai;
	int ret;

	if (dapm->dev != codec_dai->dev)
		return 0;

	switch (level) {
	case SND_SOC_BIAS_PREPARE:
		if (card->dapm.bias_level == SND_SOC_BIAS_STANDBY) {
			ret = snd_soc_dai_set_pll(codec_dai, 0,
						  WM8996_FLL_MCLK2,
						  32768, MCLK_AUDIO_RATE);
			if (ret < 0) {
				pr_err("Failed to start FLL\n");
				return ret;
			}

			ret = snd_soc_dai_set_sysclk(codec_dai,
						     WM8996_SYSCLK_FLL,
						     MCLK_AUDIO_RATE,
						     SND_SOC_CLOCK_IN);
			if (ret < 0)
				return ret;
		}
		break;

	default:
		break;
	}

	card->dapm.bias_level = level;

	return 0;
}

static struct snd_soc_jack speyside_headset;

/* Headset jack detection DAPM pins */
static struct snd_soc_jack_pin speyside_headset_pins[] = {
	{
		.pin = "Headset Mic",
		.mask = SND_JACK_MICROPHONE,
	},
};

/* Default the headphone selection to active high */
static int speyside_jack_polarity;

static int speyside_get_micbias(struct snd_soc_dapm_widget *source,
				struct snd_soc_dapm_widget *sink)
{
	if (speyside_jack_polarity && (strcmp(source->name, "MICB1") == 0))
		return 1;
	if (!speyside_jack_polarity && (strcmp(source->name, "MICB2") == 0))
		return 1;

	return 0;
}

static void speyside_set_polarity(struct snd_soc_codec *codec,
				  int polarity)
{
	speyside_jack_polarity = !polarity;
	gpio_direction_output(WM8996_HPSEL_GPIO, speyside_jack_polarity);

	/* Re-run DAPM to make sure we're using the correct mic bias */
	snd_soc_dapm_sync(&codec->dapm);
}

static int speyside_wm0010_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_dai *dai = rtd->codec_dai;
	int ret;

	ret = snd_soc_dai_set_sysclk(dai, 0, MCLK_AUDIO_RATE, 0);
	if (ret < 0)
		return ret;

	return 0;
}

static int speyside_wm8996_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_dai *dai = rtd->codec_dai;
	struct snd_soc_codec *codec = rtd->codec;
	int ret;

	ret = snd_soc_dai_set_sysclk(dai, WM8996_SYSCLK_MCLK2, 32768, 0);
	if (ret < 0)
		return ret;

	ret = gpio_request(WM8996_HPSEL_GPIO, "HP_SEL");
	if (ret != 0)
		pr_err("Failed to request HP_SEL GPIO: %d\n", ret);
	gpio_direction_output(WM8996_HPSEL_GPIO, speyside_jack_polarity);

	ret = snd_soc_jack_new(codec, "Headset",
			       SND_JACK_LINEOUT | SND_JACK_HEADSET |
			       SND_JACK_BTN_0,
			       &speyside_headset);
	if (ret)
		return ret;

	ret = snd_soc_jack_add_pins(&speyside_headset,
				    ARRAY_SIZE(speyside_headset_pins),
				    speyside_headset_pins);
	if (ret)
		return ret;

	wm8996_detect(codec, &speyside_headset, speyside_set_polarity);

	return 0;
}

static int speyside_late_probe(struct snd_soc_card *card)
{
	snd_soc_dapm_ignore_suspend(&card->dapm, "Headphone");
	snd_soc_dapm_ignore_suspend(&card->dapm, "Headset Mic");
	snd_soc_dapm_ignore_suspend(&card->dapm, "Main AMIC");
	snd_soc_dapm_ignore_suspend(&card->dapm, "Main DMIC");
	snd_soc_dapm_ignore_suspend(&card->dapm, "Main Speaker");
	snd_soc_dapm_ignore_suspend(&card->dapm, "WM1250 Output");
	snd_soc_dapm_ignore_suspend(&card->dapm, "WM1250 Input");

	return 0;
}

static const struct snd_soc_pcm_stream dsp_codec_params = {
	.formats = SNDRV_PCM_FMTBIT_S32_LE,
	.rate_min = 48000,
	.rate_max = 48000,
	.channels_min = 2,
	.channels_max = 2,
};

static struct snd_soc_dai_link speyside_dai[] = {
	{
		.name = "CPU-DSP",
		.stream_name = "CPU-DSP",
		.cpu_dai_name = "samsung-i2s.0",
		.codec_dai_name = "wm0010-sdi1",
		.platform_name = "samsung-i2s.0",
		.codec_name = "spi0.0",
		.init = speyside_wm0010_init,
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF
				| SND_SOC_DAIFMT_CBM_CFM,
	},
	{
		.name = "DSP-CODEC",
		.stream_name = "DSP-CODEC",
		.cpu_dai_name = "wm0010-sdi2",
		.codec_dai_name = "wm8996-aif1",
		.codec_name = "wm8996.1-001a",
		.init = speyside_wm8996_init,
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF
				| SND_SOC_DAIFMT_CBM_CFM,
		.params = &dsp_codec_params,
		.ignore_suspend = 1,
	},
	{
		.name = "Baseband",
		.stream_name = "Baseband",
		.cpu_dai_name = "wm8996-aif2",
		.codec_dai_name = "wm1250-ev1",
		.codec_name = "wm1250-ev1.1-0027",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF
				| SND_SOC_DAIFMT_CBM_CFM,
		.ignore_suspend = 1,
	},
};

static int speyside_wm9081_init(struct snd_soc_dapm_context *dapm)
{
	/* At any time the WM9081 is active it will have this clock */
	return snd_soc_codec_set_sysclk(dapm->codec, WM9081_SYSCLK_MCLK, 0,
					MCLK_AUDIO_RATE, 0);
}

static struct snd_soc_aux_dev speyside_aux_dev[] = {
	{
		.name = "wm9081",
		.codec_name = "wm9081.1-006c",
		.init = speyside_wm9081_init,
	},
};

static struct snd_soc_codec_conf speyside_codec_conf[] = {
	{
		.dev_name = "wm9081.1-006c",
		.name_prefix = "Sub",
	},
};

static const struct snd_kcontrol_new controls[] = {
	SOC_DAPM_PIN_SWITCH("Main Speaker"),
	SOC_DAPM_PIN_SWITCH("Main DMIC"),
	SOC_DAPM_PIN_SWITCH("Main AMIC"),
	SOC_DAPM_PIN_SWITCH("WM1250 Input"),
	SOC_DAPM_PIN_SWITCH("WM1250 Output"),
	SOC_DAPM_PIN_SWITCH("Headphone"),
};

static struct snd_soc_dapm_widget widgets[] = {
	SND_SOC_DAPM_HP("Headphone", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),

	SND_SOC_DAPM_SPK("Main Speaker", NULL),

	SND_SOC_DAPM_MIC("Main AMIC", NULL),
	SND_SOC_DAPM_MIC("Main DMIC", NULL),
};

static struct snd_soc_dapm_route audio_paths[] = {
	{ "IN1RN", NULL, "MICB1" },
	{ "IN1RP", NULL, "MICB1" },
	{ "IN1RN", NULL, "MICB2" },
	{ "IN1RP", NULL, "MICB2" },
	{ "MICB1", NULL, "Headset Mic", speyside_get_micbias },
	{ "MICB2", NULL, "Headset Mic", speyside_get_micbias },

	{ "IN1LP", NULL, "MICB2" },
	{ "IN1RN", NULL, "MICB1" },
	{ "MICB2", NULL, "Main AMIC" },

	{ "DMIC1DAT", NULL, "MICB1" },
	{ "DMIC2DAT", NULL, "MICB1" },
	{ "MICB1", NULL, "Main DMIC" },

	{ "Headphone", NULL, "HPOUT1L" },
	{ "Headphone", NULL, "HPOUT1R" },

	{ "Sub IN1", NULL, "HPOUT2L" },
	{ "Sub IN2", NULL, "HPOUT2R" },

	{ "Main Speaker", NULL, "Sub SPKN" },
	{ "Main Speaker", NULL, "Sub SPKP" },
	{ "Main Speaker", NULL, "SPKDAT" },
};

static struct snd_soc_card speyside = {
	.name = "Speyside",
	.owner = THIS_MODULE,
	.dai_link = speyside_dai,
	.num_links = ARRAY_SIZE(speyside_dai),
	.aux_dev = speyside_aux_dev,
	.num_aux_devs = ARRAY_SIZE(speyside_aux_dev),
	.codec_conf = speyside_codec_conf,
	.num_configs = ARRAY_SIZE(speyside_codec_conf),

	.set_bias_level = speyside_set_bias_level,
	.set_bias_level_post = speyside_set_bias_level_post,

	.controls = controls,
	.num_controls = ARRAY_SIZE(controls),
	.dapm_widgets = widgets,
	.num_dapm_widgets = ARRAY_SIZE(widgets),
	.dapm_routes = audio_paths,
	.num_dapm_routes = ARRAY_SIZE(audio_paths),
	.fully_routed = true,

	.late_probe = speyside_late_probe,
};

static int speyside_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = &speyside;
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

static int speyside_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);

	snd_soc_unregister_card(card);

	return 0;
}

static struct platform_driver speyside_driver = {
	.driver = {
		.name = "speyside",
		.owner = THIS_MODULE,
		.pm = &snd_soc_pm_ops,
	},
	.probe = speyside_probe,
	.remove = speyside_remove,
};

module_platform_driver(speyside_driver);

MODULE_DESCRIPTION("Speyside audio support");
MODULE_AUTHOR("Mark Brown <broonie@opensource.wolfsonmicro.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:speyside");
