/*
 * e750-wm9705.c  --  SoC audio for e750
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

#include <mach/audio.h>
#include <mach/eseries-gpio.h>

#include <asm/mach-types.h>

#include "../codecs/wm9705.h"
#include "pxa2xx-ac97.h"

static int e750_spk_amp_event(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *kcontrol, int event)
{
	if (event & SND_SOC_DAPM_PRE_PMU)
		gpio_set_value(GPIO_E750_SPK_AMP_OFF, 0);
	else if (event & SND_SOC_DAPM_POST_PMD)
		gpio_set_value(GPIO_E750_SPK_AMP_OFF, 1);

	return 0;
}

static int e750_hp_amp_event(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *kcontrol, int event)
{
	if (event & SND_SOC_DAPM_PRE_PMU)
		gpio_set_value(GPIO_E750_HP_AMP_OFF, 0);
	else if (event & SND_SOC_DAPM_POST_PMD)
		gpio_set_value(GPIO_E750_HP_AMP_OFF, 1);

	return 0;
}

static const struct snd_soc_dapm_widget e750_dapm_widgets[] = {
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_SPK("Speaker", NULL),
	SND_SOC_DAPM_MIC("Mic (Internal)", NULL),
	SND_SOC_DAPM_PGA_E("Headphone Amp", SND_SOC_NOPM, 0, 0, NULL, 0,
			e750_hp_amp_event, SND_SOC_DAPM_PRE_PMU |
			SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_PGA_E("Speaker Amp", SND_SOC_NOPM, 0, 0, NULL, 0,
			e750_spk_amp_event, SND_SOC_DAPM_PRE_PMU |
			SND_SOC_DAPM_POST_PMD),
};

static const struct snd_soc_dapm_route audio_map[] = {
	{"Headphone Amp", NULL, "HPOUTL"},
	{"Headphone Amp", NULL, "HPOUTR"},
	{"Headphone Jack", NULL, "Headphone Amp"},

	{"Speaker Amp", NULL, "MONOOUT"},
	{"Speaker", NULL, "Speaker Amp"},

	{"MIC1", NULL, "Mic (Internal)"},
};

static int e750_ac97_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_soc_dapm_context *dapm = &codec->dapm;

	snd_soc_dapm_nc_pin(dapm, "LOUT");
	snd_soc_dapm_nc_pin(dapm, "ROUT");
	snd_soc_dapm_nc_pin(dapm, "PHONE");
	snd_soc_dapm_nc_pin(dapm, "LINEINL");
	snd_soc_dapm_nc_pin(dapm, "LINEINR");
	snd_soc_dapm_nc_pin(dapm, "CDINL");
	snd_soc_dapm_nc_pin(dapm, "CDINR");
	snd_soc_dapm_nc_pin(dapm, "PCBEEP");
	snd_soc_dapm_nc_pin(dapm, "MIC2");

	return 0;
}

static struct snd_soc_dai_link e750_dai[] = {
	{
		.name = "AC97",
		.stream_name = "AC97 HiFi",
		.cpu_dai_name = "pxa2xx-ac97",
		.codec_dai_name = "wm9705-hifi",
		.platform_name = "pxa-pcm-audio",
		.codec_name = "wm9705-codec",
		.init = e750_ac97_init,
		/* use ops to check startup state */
	},
	{
		.name = "AC97 Aux",
		.stream_name = "AC97 Aux",
		.cpu_dai_name = "pxa2xx-ac97-aux",
		.codec_dai_name ="wm9705-aux",
		.platform_name = "pxa-pcm-audio",
		.codec_name = "wm9705-codec",
	},
};

static struct snd_soc_card e750 = {
	.name = "Toshiba e750",
	.owner = THIS_MODULE,
	.dai_link = e750_dai,
	.num_links = ARRAY_SIZE(e750_dai),

	.dapm_widgets = e750_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(e750_dapm_widgets),
	.dapm_routes = audio_map,
	.num_dapm_routes = ARRAY_SIZE(audio_map),
};

static struct gpio e750_audio_gpios[] = {
	{ GPIO_E750_HP_AMP_OFF, GPIOF_OUT_INIT_HIGH, "Headphone amp" },
	{ GPIO_E750_SPK_AMP_OFF, GPIOF_OUT_INIT_HIGH, "Speaker amp" },
};

static int e750_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = &e750;
	int ret;

	ret = gpio_request_array(e750_audio_gpios,
				 ARRAY_SIZE(e750_audio_gpios));
	if (ret)
		return ret;

	card->dev = &pdev->dev;

	ret = snd_soc_register_card(card);
	if (ret) {
		dev_err(&pdev->dev, "snd_soc_register_card() failed: %d\n",
			ret);
		gpio_free_array(e750_audio_gpios, ARRAY_SIZE(e750_audio_gpios));
	}
	return ret;
}

static int e750_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);

	gpio_free_array(e750_audio_gpios, ARRAY_SIZE(e750_audio_gpios));
	snd_soc_unregister_card(card);
	return 0;
}

static struct platform_driver e750_driver = {
	.driver		= {
		.name	= "e750-audio",
		.owner	= THIS_MODULE,
		.pm     = &snd_soc_pm_ops,
	},
	.probe		= e750_probe,
	.remove		= e750_remove,
};

module_platform_driver(e750_driver);

/* Module information */
MODULE_AUTHOR("Ian Molton <spyro@f2s.com>");
MODULE_DESCRIPTION("ALSA SoC driver for e750");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:e750-audio");
