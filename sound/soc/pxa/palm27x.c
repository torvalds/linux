/*
 * linux/sound/soc/pxa/palm27x.c
 *
 * SoC Audio driver for Palm T|X, T5 and LifeDrive
 *
 * based on tosa.c
 *
 * Copyright (C) 2008 Marek Vasut <marek.vasut@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/device.h>
#include <linux/gpio.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/jack.h>

#include <asm/mach-types.h>
#include <mach/audio.h>
#include <linux/platform_data/asoc-palm27x.h>

#include "pxa2xx-ac97.h"

static struct snd_soc_jack hs_jack;

/* Headphones jack detection DAPM pins */
static struct snd_soc_jack_pin hs_jack_pins[] = {
	{
		.pin    = "Headphone Jack",
		.mask   = SND_JACK_HEADPHONE,
	},
};

/* Headphones jack detection gpios */
static struct snd_soc_jack_gpio hs_jack_gpios[] = {
	[0] = {
		/* gpio is set on per-platform basis */
		.name           = "hp-gpio",
		.report         = SND_JACK_HEADPHONE,
		.debounce_time	= 200,
	},
};

/* Palm27x machine dapm widgets */
static const struct snd_soc_dapm_widget palm27x_dapm_widgets[] = {
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_SPK("Ext. Speaker", NULL),
	SND_SOC_DAPM_MIC("Ext. Microphone", NULL),
};

/* PalmTX audio map */
static const struct snd_soc_dapm_route audio_map[] = {
	/* headphone connected to HPOUTL, HPOUTR */
	{"Headphone Jack", NULL, "HPOUTL"},
	{"Headphone Jack", NULL, "HPOUTR"},

	/* ext speaker connected to ROUT2, LOUT2 */
	{"Ext. Speaker", NULL, "LOUT2"},
	{"Ext. Speaker", NULL, "ROUT2"},

	/* mic connected to MIC1 */
	{"MIC1", NULL, "Ext. Microphone"},
};

static struct snd_soc_card palm27x_asoc;

static int palm27x_ac97_init(struct snd_soc_pcm_runtime *rtd)
{
	int err;

	/* Jack detection API stuff */
	err = snd_soc_card_jack_new(rtd->card, "Headphone Jack",
				    SND_JACK_HEADPHONE, &hs_jack, hs_jack_pins,
				    ARRAY_SIZE(hs_jack_pins));
	if (err)
		return err;

	err = snd_soc_jack_add_gpios(&hs_jack, ARRAY_SIZE(hs_jack_gpios),
				hs_jack_gpios);

	return err;
}

static struct snd_soc_dai_link palm27x_dai[] = {
{
	.name = "AC97 HiFi",
	.stream_name = "AC97 HiFi",
	.cpu_dai_name = "pxa2xx-ac97",
	.codec_dai_name =  "wm9712-hifi",
	.codec_name = "wm9712-codec",
	.platform_name = "pxa-pcm-audio",
	.init = palm27x_ac97_init,
},
{
	.name = "AC97 Aux",
	.stream_name = "AC97 Aux",
	.cpu_dai_name = "pxa2xx-ac97-aux",
	.codec_dai_name = "wm9712-aux",
	.codec_name = "wm9712-codec",
	.platform_name = "pxa-pcm-audio",
},
};

static struct snd_soc_card palm27x_asoc = {
	.name = "Palm/PXA27x",
	.owner = THIS_MODULE,
	.dai_link = palm27x_dai,
	.num_links = ARRAY_SIZE(palm27x_dai),
	.dapm_widgets = palm27x_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(palm27x_dapm_widgets),
	.dapm_routes = audio_map,
	.num_dapm_routes = ARRAY_SIZE(audio_map),
	.fully_routed = true,
};

static int palm27x_asoc_probe(struct platform_device *pdev)
{
	int ret;

	if (!(machine_is_palmtx() || machine_is_palmt5() ||
		machine_is_palmld() || machine_is_palmte2()))
		return -ENODEV;

	if (!pdev->dev.platform_data) {
		dev_err(&pdev->dev, "please supply platform_data\n");
		return -ENODEV;
	}

	hs_jack_gpios[0].gpio = ((struct palm27x_asoc_info *)
			(pdev->dev.platform_data))->jack_gpio;

	palm27x_asoc.dev = &pdev->dev;

	ret = devm_snd_soc_register_card(&pdev->dev, &palm27x_asoc);
	if (ret)
		dev_err(&pdev->dev, "snd_soc_register_card() failed: %d\n",
			ret);
	return ret;
}

static struct platform_driver palm27x_wm9712_driver = {
	.probe		= palm27x_asoc_probe,
	.driver		= {
		.name		= "palm27x-asoc",
		.pm     = &snd_soc_pm_ops,
	},
};

module_platform_driver(palm27x_wm9712_driver);

/* Module information */
MODULE_AUTHOR("Marek Vasut <marek.vasut@gmail.com>");
MODULE_DESCRIPTION("ALSA SoC Palm T|X, T5 and LifeDrive");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:palm27x-asoc");
