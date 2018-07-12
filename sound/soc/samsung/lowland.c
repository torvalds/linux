/*
 * Lowland audio support
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

#include "../codecs/wm5100.h"
#include "../codecs/wm9081.h"

#define MCLK1_RATE (44100 * 512)
#define CLKOUT_RATE (44100 * 256)

static struct snd_soc_jack lowland_headset;

/* Headset jack detection DAPM pins */
static struct snd_soc_jack_pin lowland_headset_pins[] = {
	{
		.pin = "Headphone",
		.mask = SND_JACK_HEADPHONE | SND_JACK_LINEOUT,
	},
	{
		.pin = "Headset Mic",
		.mask = SND_JACK_MICROPHONE,
	},
};

static int lowland_wm5100_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_component *component = rtd->codec_dai->component;
	int ret;

	ret = snd_soc_component_set_sysclk(component, WM5100_CLK_SYSCLK,
				       WM5100_CLKSRC_MCLK1, MCLK1_RATE,
				       SND_SOC_CLOCK_IN);
	if (ret < 0) {
		pr_err("Failed to set SYSCLK clock source: %d\n", ret);
		return ret;
	}

	/* Clock OPCLK, used by the other audio components. */
	ret = snd_soc_component_set_sysclk(component, WM5100_CLK_OPCLK, 0,
				       CLKOUT_RATE, 0);
	if (ret < 0) {
		pr_err("Failed to set OPCLK rate: %d\n", ret);
		return ret;
	}

	ret = snd_soc_card_jack_new(rtd->card, "Headset", SND_JACK_LINEOUT |
				    SND_JACK_HEADSET | SND_JACK_BTN_0,
				    &lowland_headset, lowland_headset_pins,
				    ARRAY_SIZE(lowland_headset_pins));
	if (ret)
		return ret;

	wm5100_detect(component, &lowland_headset);

	return 0;
}

static int lowland_wm9081_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_component *component = rtd->codec_dai->component;

	snd_soc_dapm_nc_pin(&rtd->card->dapm, "LINEOUT");

	/* At any time the WM9081 is active it will have this clock */
	return snd_soc_component_set_sysclk(component, WM9081_SYSCLK_MCLK, 0,
					CLKOUT_RATE, 0);
}

static const struct snd_soc_pcm_stream sub_params = {
	.formats = SNDRV_PCM_FMTBIT_S32_LE,
	.rate_min = 44100,
	.rate_max = 44100,
	.channels_min = 2,
	.channels_max = 2,
};

static struct snd_soc_dai_link lowland_dai[] = {
	{
		.name = "CPU",
		.stream_name = "CPU",
		.cpu_dai_name = "samsung-i2s.0",
		.codec_dai_name = "wm5100-aif1",
		.platform_name = "samsung-i2s.0",
		.codec_name = "wm5100.1-001a",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
				SND_SOC_DAIFMT_CBM_CFM,
		.init = lowland_wm5100_init,
	},
	{
		.name = "Baseband",
		.stream_name = "Baseband",
		.cpu_dai_name = "wm5100-aif2",
		.codec_dai_name = "wm1250-ev1",
		.codec_name = "wm1250-ev1.1-0027",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
				SND_SOC_DAIFMT_CBM_CFM,
		.ignore_suspend = 1,
	},
	{
		.name = "Sub Speaker",
		.stream_name = "Sub Speaker",
		.cpu_dai_name = "wm5100-aif3",
		.codec_dai_name = "wm9081-hifi",
		.codec_name = "wm9081.1-006c",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
				SND_SOC_DAIFMT_CBM_CFM,
		.ignore_suspend = 1,
		.params = &sub_params,
		.init = lowland_wm9081_init,
	},
};

static struct snd_soc_codec_conf lowland_codec_conf[] = {
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
	{ "Sub IN1", NULL, "HPOUT2L" },
	{ "Sub IN2", NULL, "HPOUT2R" },

	{ "Main Speaker", NULL, "Sub SPKN" },
	{ "Main Speaker", NULL, "Sub SPKP" },
	{ "Main Speaker", NULL, "SPKDAT1" },
};

static struct snd_soc_card lowland = {
	.name = "Lowland",
	.owner = THIS_MODULE,
	.dai_link = lowland_dai,
	.num_links = ARRAY_SIZE(lowland_dai),
	.codec_conf = lowland_codec_conf,
	.num_configs = ARRAY_SIZE(lowland_codec_conf),

	.controls = controls,
	.num_controls = ARRAY_SIZE(controls),
	.dapm_widgets = widgets,
	.num_dapm_widgets = ARRAY_SIZE(widgets),
	.dapm_routes = audio_paths,
	.num_dapm_routes = ARRAY_SIZE(audio_paths),
};

static int lowland_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = &lowland;
	int ret;

	card->dev = &pdev->dev;

	ret = devm_snd_soc_register_card(&pdev->dev, card);
	if (ret)
		dev_err(&pdev->dev, "snd_soc_register_card() failed: %d\n",
			ret);

	return ret;
}

static struct platform_driver lowland_driver = {
	.driver = {
		.name = "lowland",
		.pm = &snd_soc_pm_ops,
	},
	.probe = lowland_probe,
};

module_platform_driver(lowland_driver);

MODULE_DESCRIPTION("Lowland audio support");
MODULE_AUTHOR("Mark Brown <broonie@opensource.wolfsonmicro.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:lowland");
