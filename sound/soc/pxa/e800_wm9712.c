/*
 * e800-wm9712.c  --  SoC audio for e800
 *
 * Copyright 2007 (c) Ian Molton <spyro@f2s.com>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation; version 2 ONLY.
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/gpio.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>

#include <asm/mach-types.h>
#include <mach/audio.h>
#include <mach/eseries-gpio.h>

#include "../codecs/wm9712.h"
#include "pxa2xx-ac97.h"

static int e800_spk_amp_event(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *kcontrol, int event)
{
	if (event & SND_SOC_DAPM_PRE_PMU)
		gpio_set_value(GPIO_E800_SPK_AMP_ON, 1);
	else if (event & SND_SOC_DAPM_POST_PMD)
		gpio_set_value(GPIO_E800_SPK_AMP_ON, 0);

	return 0;
}

static int e800_hp_amp_event(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *kcontrol, int event)
{
	if (event & SND_SOC_DAPM_PRE_PMU)
		gpio_set_value(GPIO_E800_HP_AMP_OFF, 0);
	else if (event & SND_SOC_DAPM_POST_PMD)
		gpio_set_value(GPIO_E800_HP_AMP_OFF, 1);

	return 0;
}

static const struct snd_soc_dapm_widget e800_dapm_widgets[] = {
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_MIC("Mic (Internal1)", NULL),
	SND_SOC_DAPM_MIC("Mic (Internal2)", NULL),
	SND_SOC_DAPM_SPK("Speaker", NULL),
	SND_SOC_DAPM_PGA_E("Headphone Amp", SND_SOC_NOPM, 0, 0, NULL, 0,
			e800_hp_amp_event, SND_SOC_DAPM_PRE_PMU |
			SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_PGA_E("Speaker Amp", SND_SOC_NOPM, 0, 0, NULL, 0,
			e800_spk_amp_event, SND_SOC_DAPM_PRE_PMU |
			SND_SOC_DAPM_POST_PMD),
};

static const struct snd_soc_dapm_route audio_map[] = {
	{"Headphone Jack", NULL, "HPOUTL"},
	{"Headphone Jack", NULL, "HPOUTR"},
	{"Headphone Jack", NULL, "Headphone Amp"},

	{"Speaker Amp", NULL, "MONOOUT"},
	{"Speaker", NULL, "Speaker Amp"},

	{"MIC1", NULL, "Mic (Internal1)"},
	{"MIC2", NULL, "Mic (Internal2)"},
};

static struct snd_soc_dai_link e800_dai[] = {
	{
		.name = "AC97",
		.stream_name = "AC97 HiFi",
		.cpu_dai_name = "pxa2xx-ac97",
		.codec_dai_name = "wm9712-hifi",
		.platform_name = "pxa-pcm-audio",
		.codec_name = "wm9712-codec",
	},
	{
		.name = "AC97 Aux",
		.stream_name = "AC97 Aux",
		.cpu_dai_name = "pxa2xx-ac97-aux",
		.codec_dai_name ="wm9712-aux",
		.platform_name = "pxa-pcm-audio",
		.codec_name = "wm9712-codec",
	},
};

static struct snd_soc_card e800 = {
	.name = "Toshiba e800",
	.owner = THIS_MODULE,
	.dai_link = e800_dai,
	.num_links = ARRAY_SIZE(e800_dai),

	.dapm_widgets = e800_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(e800_dapm_widgets),
	.dapm_routes = audio_map,
	.num_dapm_routes = ARRAY_SIZE(audio_map),
};

static struct gpio e800_audio_gpios[] = {
	{ GPIO_E800_SPK_AMP_ON, GPIOF_OUT_INIT_HIGH, "Headphone amp" },
	{ GPIO_E800_HP_AMP_OFF, GPIOF_OUT_INIT_HIGH, "Speaker amp" },
};

static int e800_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = &e800;
	int ret;

	ret = gpio_request_array(e800_audio_gpios,
				 ARRAY_SIZE(e800_audio_gpios));
	if (ret)
		return ret;

	card->dev = &pdev->dev;

	ret = snd_soc_register_card(card);
	if (ret) {
		dev_err(&pdev->dev, "snd_soc_register_card() failed: %d\n",
			ret);
		gpio_free_array(e800_audio_gpios, ARRAY_SIZE(e800_audio_gpios));
	}
	return ret;
}

static int e800_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);

	gpio_free_array(e800_audio_gpios, ARRAY_SIZE(e800_audio_gpios));
	snd_soc_unregister_card(card);
	return 0;
}

static struct platform_driver e800_driver = {
	.driver		= {
		.name	= "e800-audio",
		.owner	= THIS_MODULE,
		.pm     = &snd_soc_pm_ops,
	},
	.probe		= e800_probe,
	.remove		= e800_remove,
};

module_platform_driver(e800_driver);

/* Module information */
MODULE_AUTHOR("Ian Molton <spyro@f2s.com>");
MODULE_DESCRIPTION("ALSA SoC driver for e800");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:e800-audio");
