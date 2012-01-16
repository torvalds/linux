/*
 * e740-wm9705.c  --  SoC audio for e740
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


#define E740_AUDIO_OUT 1
#define E740_AUDIO_IN  2

static int e740_audio_power;

static void e740_sync_audio_power(int status)
{
	gpio_set_value(GPIO_E740_WM9705_nAVDD2, !status);
	gpio_set_value(GPIO_E740_AMP_ON, (status & E740_AUDIO_OUT) ? 1 : 0);
	gpio_set_value(GPIO_E740_MIC_ON, (status & E740_AUDIO_IN) ? 1 : 0);
}

static int e740_mic_amp_event(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *kcontrol, int event)
{
	if (event & SND_SOC_DAPM_PRE_PMU)
		e740_audio_power |= E740_AUDIO_IN;
	else if (event & SND_SOC_DAPM_POST_PMD)
		e740_audio_power &= ~E740_AUDIO_IN;

	e740_sync_audio_power(e740_audio_power);

	return 0;
}

static int e740_output_amp_event(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *kcontrol, int event)
{
	if (event & SND_SOC_DAPM_PRE_PMU)
		e740_audio_power |= E740_AUDIO_OUT;
	else if (event & SND_SOC_DAPM_POST_PMD)
		e740_audio_power &= ~E740_AUDIO_OUT;

	e740_sync_audio_power(e740_audio_power);

	return 0;
}

static const struct snd_soc_dapm_widget e740_dapm_widgets[] = {
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_SPK("Speaker", NULL),
	SND_SOC_DAPM_MIC("Mic (Internal)", NULL),
	SND_SOC_DAPM_PGA_E("Output Amp", SND_SOC_NOPM, 0, 0, NULL, 0,
			e740_output_amp_event, SND_SOC_DAPM_PRE_PMU |
			SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_PGA_E("Mic Amp", SND_SOC_NOPM, 0, 0, NULL, 0,
			e740_mic_amp_event, SND_SOC_DAPM_PRE_PMU |
			SND_SOC_DAPM_POST_PMD),
};

static const struct snd_soc_dapm_route audio_map[] = {
	{"Output Amp", NULL, "LOUT"},
	{"Output Amp", NULL, "ROUT"},
	{"Output Amp", NULL, "MONOOUT"},

	{"Speaker", NULL, "Output Amp"},
	{"Headphone Jack", NULL, "Output Amp"},

	{"MIC1", NULL, "Mic Amp"},
	{"Mic Amp", NULL, "Mic (Internal)"},
};

static int e740_ac97_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_soc_dapm_context *dapm = &codec->dapm;

	snd_soc_dapm_nc_pin(dapm, "HPOUTL");
	snd_soc_dapm_nc_pin(dapm, "HPOUTR");
	snd_soc_dapm_nc_pin(dapm, "PHONE");
	snd_soc_dapm_nc_pin(dapm, "LINEINL");
	snd_soc_dapm_nc_pin(dapm, "LINEINR");
	snd_soc_dapm_nc_pin(dapm, "CDINL");
	snd_soc_dapm_nc_pin(dapm, "CDINR");
	snd_soc_dapm_nc_pin(dapm, "PCBEEP");
	snd_soc_dapm_nc_pin(dapm, "MIC2");

	snd_soc_dapm_new_controls(dapm, e740_dapm_widgets,
					ARRAY_SIZE(e740_dapm_widgets));

	snd_soc_dapm_add_routes(dapm, audio_map, ARRAY_SIZE(audio_map));

	return 0;
}

static struct snd_soc_dai_link e740_dai[] = {
	{
		.name = "AC97",
		.stream_name = "AC97 HiFi",
		.cpu_dai_name = "pxa2xx-ac97",
		.codec_dai_name = "wm9705-hifi",
		.platform_name = "pxa-pcm-audio",
		.codec_name = "wm9705-codec",
		.init = e740_ac97_init,
	},
	{
		.name = "AC97 Aux",
		.stream_name = "AC97 Aux",
		.cpu_dai_name = "pxa2xx-ac97-aux",
		.codec_dai_name = "wm9705-aux",
		.platform_name = "pxa-pcm-audio",
		.codec_name = "wm9705-codec",
	},
};

static struct snd_soc_card e740 = {
	.name = "Toshiba e740",
	.owner = THIS_MODULE,
	.dai_link = e740_dai,
	.num_links = ARRAY_SIZE(e740_dai),
};

static struct gpio e740_audio_gpios[] = {
	{ GPIO_E740_MIC_ON, GPIOF_OUT_INIT_LOW, "Mic amp" },
	{ GPIO_E740_AMP_ON, GPIOF_OUT_INIT_LOW, "Output amp" },
	{ GPIO_E740_WM9705_nAVDD2, GPIOF_OUT_INIT_HIGH, "Audio power" },
};

static int __devinit e740_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = &e740;
	int ret;

	ret = gpio_request_array(e740_audio_gpios,
				 ARRAY_SIZE(e740_audio_gpios));
	if (ret)
		return ret;

	card->dev = &pdev->dev;

	ret = snd_soc_register_card(card);
	if (ret) {
		dev_err(&pdev->dev, "snd_soc_register_card() failed: %d\n",
			ret);
		gpio_free_array(e740_audio_gpios, ARRAY_SIZE(e740_audio_gpios));
	}
	return ret;
}

static int __devexit e740_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);

	gpio_free_array(e740_audio_gpios, ARRAY_SIZE(e740_audio_gpios));
	snd_soc_unregister_card(card);
	return 0;
}

static struct platform_driver e740_driver = {
	.driver		= {
		.name	= "e740-audio",
		.owner	= THIS_MODULE,
	},
	.probe		= e740_probe,
	.remove		= __devexit_p(e740_remove),
};

module_platform_driver(e740_driver);

/* Module information */
MODULE_AUTHOR("Ian Molton <spyro@f2s.com>");
MODULE_DESCRIPTION("ALSA SoC driver for e740");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:e740-audio");
