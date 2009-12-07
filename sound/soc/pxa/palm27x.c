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
#include <sound/soc-dapm.h>
#include <sound/jack.h>

#include <asm/mach-types.h>
#include <mach/audio.h>
#include <mach/palmasoc.h>

#include "../codecs/wm9712.h"
#include "pxa2xx-pcm.h"
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
	{"Ext. Microphone", NULL, "MIC1"},
};

static struct snd_soc_card palm27x_asoc;

static int palm27x_ac97_init(struct snd_soc_codec *codec)
{
	int err;

	/* add palm27x specific widgets */
	err = snd_soc_dapm_new_controls(codec, palm27x_dapm_widgets,
				ARRAY_SIZE(palm27x_dapm_widgets));
	if (err)
		return err;

	/* set up palm27x specific audio path audio_map */
	err = snd_soc_dapm_add_routes(codec, audio_map, ARRAY_SIZE(audio_map));
	if (err)
		return err;

	/* connected pins */
	if (machine_is_palmld())
		snd_soc_dapm_enable_pin(codec, "MIC1");
	snd_soc_dapm_enable_pin(codec, "HPOUTL");
	snd_soc_dapm_enable_pin(codec, "HPOUTR");
	snd_soc_dapm_enable_pin(codec, "LOUT2");
	snd_soc_dapm_enable_pin(codec, "ROUT2");

	/* not connected pins */
	snd_soc_dapm_nc_pin(codec, "OUT3");
	snd_soc_dapm_nc_pin(codec, "MONOOUT");
	snd_soc_dapm_nc_pin(codec, "LINEINL");
	snd_soc_dapm_nc_pin(codec, "LINEINR");
	snd_soc_dapm_nc_pin(codec, "PCBEEP");
	snd_soc_dapm_nc_pin(codec, "PHONE");
	snd_soc_dapm_nc_pin(codec, "MIC2");

	err = snd_soc_dapm_sync(codec);
	if (err)
		return err;

	/* Jack detection API stuff */
	err = snd_soc_jack_new(&palm27x_asoc, "Headphone Jack",
				SND_JACK_HEADPHONE, &hs_jack);
	if (err)
		return err;

	err = snd_soc_jack_add_pins(&hs_jack, ARRAY_SIZE(hs_jack_pins),
				hs_jack_pins);
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
	.cpu_dai = &pxa_ac97_dai[PXA2XX_DAI_AC97_HIFI],
	.codec_dai = &wm9712_dai[WM9712_DAI_AC97_HIFI],
	.init = palm27x_ac97_init,
},
{
	.name = "AC97 Aux",
	.stream_name = "AC97 Aux",
	.cpu_dai = &pxa_ac97_dai[PXA2XX_DAI_AC97_AUX],
	.codec_dai = &wm9712_dai[WM9712_DAI_AC97_AUX],
},
};

static struct snd_soc_card palm27x_asoc = {
	.name = "Palm/PXA27x",
	.platform = &pxa2xx_soc_platform,
	.dai_link = palm27x_dai,
	.num_links = ARRAY_SIZE(palm27x_dai),
};

static struct snd_soc_device palm27x_snd_devdata = {
	.card = &palm27x_asoc,
	.codec_dev = &soc_codec_dev_wm9712,
};

static struct platform_device *palm27x_snd_device;

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

	palm27x_snd_device = platform_device_alloc("soc-audio", -1);
	if (!palm27x_snd_device)
		return -ENOMEM;

	platform_set_drvdata(palm27x_snd_device, &palm27x_snd_devdata);
	palm27x_snd_devdata.dev = &palm27x_snd_device->dev;
	ret = platform_device_add(palm27x_snd_device);

	if (ret != 0)
		goto put_device;

	return 0;

put_device:
	platform_device_put(palm27x_snd_device);

	return ret;
}

static int __devexit palm27x_asoc_remove(struct platform_device *pdev)
{
	platform_device_unregister(palm27x_snd_device);
	return 0;
}

static struct platform_driver palm27x_wm9712_driver = {
	.probe		= palm27x_asoc_probe,
	.remove		= __devexit_p(palm27x_asoc_remove),
	.driver		= {
		.name		= "palm27x-asoc",
		.owner		= THIS_MODULE,
	},
};

static int __init palm27x_asoc_init(void)
{
	return platform_driver_register(&palm27x_wm9712_driver);
}

static void __exit palm27x_asoc_exit(void)
{
	platform_driver_unregister(&palm27x_wm9712_driver);
}

module_init(palm27x_asoc_init);
module_exit(palm27x_asoc_exit);

/* Module information */
MODULE_AUTHOR("Marek Vasut <marek.vasut@gmail.com>");
MODULE_DESCRIPTION("ALSA SoC Palm T|X, T5 and LifeDrive");
MODULE_LICENSE("GPL");
