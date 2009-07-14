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
#include <linux/interrupt.h>
#include <linux/irq.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>

#include <asm/mach-types.h>
#include <mach/audio.h>
#include <mach/palmasoc.h>

#include "../codecs/wm9712.h"
#include "pxa2xx-pcm.h"
#include "pxa2xx-ac97.h"

static int palm27x_jack_func = 1;
static int palm27x_spk_func = 1;
static int palm27x_ep_gpio = -1;

static void palm27x_ext_control(struct snd_soc_codec *codec)
{
	if (!palm27x_spk_func)
		snd_soc_dapm_enable_pin(codec, "Speaker");
	else
		snd_soc_dapm_disable_pin(codec, "Speaker");

	if (!palm27x_jack_func)
		snd_soc_dapm_enable_pin(codec, "Headphone Jack");
	else
		snd_soc_dapm_disable_pin(codec, "Headphone Jack");

	snd_soc_dapm_sync(codec);
}

static int palm27x_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_codec *codec = rtd->socdev->card->codec;

	/* check the jack status at stream startup */
	palm27x_ext_control(codec);
	return 0;
}

static struct snd_soc_ops palm27x_ops = {
	.startup = palm27x_startup,
};

static irqreturn_t palm27x_interrupt(int irq, void *v)
{
	palm27x_spk_func = gpio_get_value(palm27x_ep_gpio);
	palm27x_jack_func = !palm27x_spk_func;
	return IRQ_HANDLED;
}

static int palm27x_get_jack(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = palm27x_jack_func;
	return 0;
}

static int palm27x_set_jack(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec =  snd_kcontrol_chip(kcontrol);

	if (palm27x_jack_func == ucontrol->value.integer.value[0])
		return 0;

	palm27x_jack_func = ucontrol->value.integer.value[0];
	palm27x_ext_control(codec);
	return 1;
}

static int palm27x_get_spk(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = palm27x_spk_func;
	return 0;
}

static int palm27x_set_spk(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec =  snd_kcontrol_chip(kcontrol);

	if (palm27x_spk_func == ucontrol->value.integer.value[0])
		return 0;

	palm27x_spk_func = ucontrol->value.integer.value[0];
	palm27x_ext_control(codec);
	return 1;
}

/* PalmTX machine dapm widgets */
static const struct snd_soc_dapm_widget palm27x_dapm_widgets[] = {
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_SPK("Speaker", NULL),
};

/* PalmTX audio map */
static const struct snd_soc_dapm_route audio_map[] = {
	/* headphone connected to HPOUTL, HPOUTR */
	{"Headphone Jack", NULL, "HPOUTL"},
	{"Headphone Jack", NULL, "HPOUTR"},

	/* ext speaker connected to ROUT2, LOUT2 */
	{"Speaker", NULL, "LOUT2"},
	{"Speaker", NULL, "ROUT2"},
};

static const char *jack_function[] = {"Headphone", "Off"};
static const char *spk_function[] = {"On", "Off"};
static const struct soc_enum palm27x_enum[] = {
	SOC_ENUM_SINGLE_EXT(2, jack_function),
	SOC_ENUM_SINGLE_EXT(2, spk_function),
};

static const struct snd_kcontrol_new palm27x_controls[] = {
	SOC_ENUM_EXT("Jack Function", palm27x_enum[0], palm27x_get_jack,
		palm27x_set_jack),
	SOC_ENUM_EXT("Speaker Function", palm27x_enum[1], palm27x_get_spk,
		palm27x_set_spk),
};

static int palm27x_ac97_init(struct snd_soc_codec *codec)
{
	int err;

	snd_soc_dapm_nc_pin(codec, "OUT3");
	snd_soc_dapm_nc_pin(codec, "MONOOUT");

	/* add palm27x specific controls */
	err = snd_soc_add_controls(codec, palm27x_controls,
				ARRAY_SIZE(palm27x_controls));
	if (err < 0)
		return err;

	/* add palm27x specific widgets */
	snd_soc_dapm_new_controls(codec, palm27x_dapm_widgets,
				ARRAY_SIZE(palm27x_dapm_widgets));

	/* set up palm27x specific audio path audio_map */
	snd_soc_dapm_add_routes(codec, audio_map, ARRAY_SIZE(audio_map));

	snd_soc_dapm_sync(codec);
	return 0;
}

static struct snd_soc_dai_link palm27x_dai[] = {
{
	.name = "AC97 HiFi",
	.stream_name = "AC97 HiFi",
	.cpu_dai = &pxa_ac97_dai[PXA2XX_DAI_AC97_HIFI],
	.codec_dai = &wm9712_dai[WM9712_DAI_AC97_HIFI],
	.init = palm27x_ac97_init,
	.ops = &palm27x_ops,
},
{
	.name = "AC97 Aux",
	.stream_name = "AC97 Aux",
	.cpu_dai = &pxa_ac97_dai[PXA2XX_DAI_AC97_AUX],
	.codec_dai = &wm9712_dai[WM9712_DAI_AC97_AUX],
	.ops = &palm27x_ops,
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

	if (pdev->dev.platform_data)
		palm27x_ep_gpio = ((struct palm27x_asoc_info *)
			(pdev->dev.platform_data))->jack_gpio;

	ret = gpio_request(palm27x_ep_gpio, "Headphone Jack");
	if (ret)
		return ret;
	ret = gpio_direction_input(palm27x_ep_gpio);
	if (ret)
		goto err_alloc;

	if (request_irq(gpio_to_irq(palm27x_ep_gpio), palm27x_interrupt,
			IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
			"Headphone jack", NULL))
		goto err_alloc;

	palm27x_snd_device = platform_device_alloc("soc-audio", -1);
	if (!palm27x_snd_device) {
		ret = -ENOMEM;
		goto err_dev;
	}

	platform_set_drvdata(palm27x_snd_device, &palm27x_snd_devdata);
	palm27x_snd_devdata.dev = &palm27x_snd_device->dev;
	ret = platform_device_add(palm27x_snd_device);

	if (ret != 0)
		goto put_device;

	return 0;

put_device:
	platform_device_put(palm27x_snd_device);
err_dev:
	free_irq(gpio_to_irq(palm27x_ep_gpio), NULL);
err_alloc:
	gpio_free(palm27x_ep_gpio);

	return ret;
}

static int __devexit palm27x_asoc_remove(struct platform_device *pdev)
{
	free_irq(gpio_to_irq(palm27x_ep_gpio), NULL);
	gpio_free(palm27x_ep_gpio);
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
