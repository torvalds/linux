/*
 * tosa.c  --  SoC audio for Tosa
 *
 * Copyright 2005 Wolfson Microelectronics PLC.
 * Copyright 2005 Openedhand Ltd.
 *
 * Authors: Liam Girdwood <lrg@slimlogic.co.uk>
 *          Richard Purdie <richard@openedhand.com>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 * GPIO's
 *  1 - Jack Insertion
 *  5 - Hookswitch (headset answer/hang up switch)
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/device.h>
#include <linux/gpio.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>

#include <asm/mach-types.h>
#include <mach/tosa.h>
#include <mach/audio.h>

#include "../codecs/wm9712.h"
#include "pxa2xx-ac97.h"

#define TOSA_HP        0
#define TOSA_MIC_INT   1
#define TOSA_HEADSET   2
#define TOSA_HP_OFF    3
#define TOSA_SPK_ON    0
#define TOSA_SPK_OFF   1

static int tosa_jack_func;
static int tosa_spk_func;

static void tosa_ext_control(struct snd_soc_codec *codec)
{
	struct snd_soc_dapm_context *dapm = &codec->dapm;

	/* set up jack connection */
	switch (tosa_jack_func) {
	case TOSA_HP:
		snd_soc_dapm_disable_pin(dapm, "Mic (Internal)");
		snd_soc_dapm_enable_pin(dapm, "Headphone Jack");
		snd_soc_dapm_disable_pin(dapm, "Headset Jack");
		break;
	case TOSA_MIC_INT:
		snd_soc_dapm_enable_pin(dapm, "Mic (Internal)");
		snd_soc_dapm_disable_pin(dapm, "Headphone Jack");
		snd_soc_dapm_disable_pin(dapm, "Headset Jack");
		break;
	case TOSA_HEADSET:
		snd_soc_dapm_disable_pin(dapm, "Mic (Internal)");
		snd_soc_dapm_disable_pin(dapm, "Headphone Jack");
		snd_soc_dapm_enable_pin(dapm, "Headset Jack");
		break;
	}

	if (tosa_spk_func == TOSA_SPK_ON)
		snd_soc_dapm_enable_pin(dapm, "Speaker");
	else
		snd_soc_dapm_disable_pin(dapm, "Speaker");

	snd_soc_dapm_sync(dapm);
}

static int tosa_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_codec *codec = rtd->codec;

	mutex_lock(&codec->mutex);

	/* check the jack status at stream startup */
	tosa_ext_control(codec);

	mutex_unlock(&codec->mutex);

	return 0;
}

static struct snd_soc_ops tosa_ops = {
	.startup = tosa_startup,
};

static int tosa_get_jack(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = tosa_jack_func;
	return 0;
}

static int tosa_set_jack(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec =  snd_kcontrol_chip(kcontrol);

	if (tosa_jack_func == ucontrol->value.integer.value[0])
		return 0;

	tosa_jack_func = ucontrol->value.integer.value[0];
	tosa_ext_control(codec);
	return 1;
}

static int tosa_get_spk(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = tosa_spk_func;
	return 0;
}

static int tosa_set_spk(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec =  snd_kcontrol_chip(kcontrol);

	if (tosa_spk_func == ucontrol->value.integer.value[0])
		return 0;

	tosa_spk_func = ucontrol->value.integer.value[0];
	tosa_ext_control(codec);
	return 1;
}

/* tosa dapm event handlers */
static int tosa_hp_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *k, int event)
{
	gpio_set_value(TOSA_GPIO_L_MUTE, SND_SOC_DAPM_EVENT_ON(event) ? 1 :0);
	return 0;
}

/* tosa machine dapm widgets */
static const struct snd_soc_dapm_widget tosa_dapm_widgets[] = {
SND_SOC_DAPM_HP("Headphone Jack", tosa_hp_event),
SND_SOC_DAPM_HP("Headset Jack", NULL),
SND_SOC_DAPM_MIC("Mic (Internal)", NULL),
SND_SOC_DAPM_SPK("Speaker", NULL),
};

/* tosa audio map */
static const struct snd_soc_dapm_route audio_map[] = {

	/* headphone connected to HPOUTL, HPOUTR */
	{"Headphone Jack", NULL, "HPOUTL"},
	{"Headphone Jack", NULL, "HPOUTR"},

	/* ext speaker connected to LOUT2, ROUT2 */
	{"Speaker", NULL, "LOUT2"},
	{"Speaker", NULL, "ROUT2"},

	/* internal mic is connected to mic1, mic2 differential - with bias */
	{"MIC1", NULL, "Mic Bias"},
	{"MIC2", NULL, "Mic Bias"},
	{"Mic Bias", NULL, "Mic (Internal)"},

	/* headset is connected to HPOUTR, and LINEINR with bias */
	{"Headset Jack", NULL, "HPOUTR"},
	{"LINEINR", NULL, "Mic Bias"},
	{"Mic Bias", NULL, "Headset Jack"},
};

static const char *jack_function[] = {"Headphone", "Mic", "Line", "Headset",
	"Off"};
static const char *spk_function[] = {"On", "Off"};
static const struct soc_enum tosa_enum[] = {
	SOC_ENUM_SINGLE_EXT(5, jack_function),
	SOC_ENUM_SINGLE_EXT(2, spk_function),
};

static const struct snd_kcontrol_new tosa_controls[] = {
	SOC_ENUM_EXT("Jack Function", tosa_enum[0], tosa_get_jack,
		tosa_set_jack),
	SOC_ENUM_EXT("Speaker Function", tosa_enum[1], tosa_get_spk,
		tosa_set_spk),
};

static int tosa_ac97_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_soc_dapm_context *dapm = &codec->dapm;
	int err;

	snd_soc_dapm_nc_pin(dapm, "OUT3");
	snd_soc_dapm_nc_pin(dapm, "MONOOUT");

	/* add tosa specific controls */
	err = snd_soc_add_codec_controls(codec, tosa_controls,
				ARRAY_SIZE(tosa_controls));
	if (err < 0)
		return err;

	/* add tosa specific widgets */
	snd_soc_dapm_new_controls(dapm, tosa_dapm_widgets,
				  ARRAY_SIZE(tosa_dapm_widgets));

	/* set up tosa specific audio path audio_map */
	snd_soc_dapm_add_routes(dapm, audio_map, ARRAY_SIZE(audio_map));

	return 0;
}

static struct snd_soc_dai_link tosa_dai[] = {
{
	.name = "AC97",
	.stream_name = "AC97 HiFi",
	.cpu_dai_name = "pxa2xx-ac97",
	.codec_dai_name = "wm9712-hifi",
	.platform_name = "pxa-pcm-audio",
	.codec_name = "wm9712-codec",
	.init = tosa_ac97_init,
	.ops = &tosa_ops,
},
{
	.name = "AC97 Aux",
	.stream_name = "AC97 Aux",
	.cpu_dai_name = "pxa2xx-ac97-aux",
	.codec_dai_name = "wm9712-aux",
	.platform_name = "pxa-pcm-audio",
	.codec_name = "wm9712-codec",
	.ops = &tosa_ops,
},
};

static struct snd_soc_card tosa = {
	.name = "Tosa",
	.owner = THIS_MODULE,
	.dai_link = tosa_dai,
	.num_links = ARRAY_SIZE(tosa_dai),
};

static int __devinit tosa_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = &tosa;
	int ret;

	ret = gpio_request_one(TOSA_GPIO_L_MUTE, GPIOF_OUT_INIT_LOW,
			       "Headphone Jack");
	if (ret)
		return ret;

	card->dev = &pdev->dev;

	ret = snd_soc_register_card(card);
	if (ret) {
		dev_err(&pdev->dev, "snd_soc_register_card() failed: %d\n",
			ret);
		gpio_free(TOSA_GPIO_L_MUTE);
	}
	return ret;
}

static int __devexit tosa_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);

	gpio_free(TOSA_GPIO_L_MUTE);
	snd_soc_unregister_card(card);
	return 0;
}

static struct platform_driver tosa_driver = {
	.driver		= {
		.name	= "tosa-audio",
		.owner	= THIS_MODULE,
	},
	.probe		= tosa_probe,
	.remove		= __devexit_p(tosa_remove),
};

module_platform_driver(tosa_driver);

/* Module information */
MODULE_AUTHOR("Richard Purdie");
MODULE_DESCRIPTION("ALSA SoC Tosa");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:tosa-audio");
