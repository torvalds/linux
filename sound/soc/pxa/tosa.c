// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * tosa.c  --  SoC audio for Tosa
 *
 * Copyright 2005 Wolfson Microelectronics PLC.
 * Copyright 2005 Openedhand Ltd.
 *
 * Authors: Liam Girdwood <lrg@slimlogic.co.uk>
 *          Richard Purdie <richard@openedhand.com>
 *
 * GPIO's
 *  1 - Jack Insertion
 *  5 - Hookswitch (headset answer/hang up switch)
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

#define TOSA_HP        0
#define TOSA_MIC_INT   1
#define TOSA_HEADSET   2
#define TOSA_HP_OFF    3
#define TOSA_SPK_ON    0
#define TOSA_SPK_OFF   1

static int tosa_jack_func;
static int tosa_spk_func;

static void tosa_ext_control(struct snd_soc_dapm_context *dapm)
{

	snd_soc_dapm_mutex_lock(dapm);

	/* set up jack connection */
	switch (tosa_jack_func) {
	case TOSA_HP:
		snd_soc_dapm_disable_pin_unlocked(dapm, "Mic (Internal)");
		snd_soc_dapm_enable_pin_unlocked(dapm, "Headphone Jack");
		snd_soc_dapm_disable_pin_unlocked(dapm, "Headset Jack");
		break;
	case TOSA_MIC_INT:
		snd_soc_dapm_enable_pin_unlocked(dapm, "Mic (Internal)");
		snd_soc_dapm_disable_pin_unlocked(dapm, "Headphone Jack");
		snd_soc_dapm_disable_pin_unlocked(dapm, "Headset Jack");
		break;
	case TOSA_HEADSET:
		snd_soc_dapm_disable_pin_unlocked(dapm, "Mic (Internal)");
		snd_soc_dapm_disable_pin_unlocked(dapm, "Headphone Jack");
		snd_soc_dapm_enable_pin_unlocked(dapm, "Headset Jack");
		break;
	}

	if (tosa_spk_func == TOSA_SPK_ON)
		snd_soc_dapm_enable_pin_unlocked(dapm, "Speaker");
	else
		snd_soc_dapm_disable_pin_unlocked(dapm, "Speaker");

	snd_soc_dapm_sync_unlocked(dapm);

	snd_soc_dapm_mutex_unlock(dapm);
}

static int tosa_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);

	/* check the jack status at stream startup */
	tosa_ext_control(&rtd->card->dapm);

	return 0;
}

static const struct snd_soc_ops tosa_ops = {
	.startup = tosa_startup,
};

static int tosa_get_jack(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.enumerated.item[0] = tosa_jack_func;
	return 0;
}

static int tosa_set_jack(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_card *card = snd_kcontrol_chip(kcontrol);

	if (tosa_jack_func == ucontrol->value.enumerated.item[0])
		return 0;

	tosa_jack_func = ucontrol->value.enumerated.item[0];
	tosa_ext_control(&card->dapm);
	return 1;
}

static int tosa_get_spk(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.enumerated.item[0] = tosa_spk_func;
	return 0;
}

static int tosa_set_spk(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_card *card = snd_kcontrol_chip(kcontrol);

	if (tosa_spk_func == ucontrol->value.enumerated.item[0])
		return 0;

	tosa_spk_func = ucontrol->value.enumerated.item[0];
	tosa_ext_control(&card->dapm);
	return 1;
}

/* tosa dapm event handlers */
static int tosa_hp_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *k, int event)
{
	gpio_set_value(TOSA_GPIO_L_MUTE, SND_SOC_DAPM_EVENT_ON(event) ? 1 : 0);
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

static const char * const jack_function[] = {"Headphone", "Mic", "Line",
	"Headset", "Off"};
static const char * const spk_function[] = {"On", "Off"};
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

SND_SOC_DAILINK_DEFS(ac97,
	DAILINK_COMP_ARRAY(COMP_CPU("pxa2xx-ac97")),
	DAILINK_COMP_ARRAY(COMP_CODEC("wm9712-codec", "wm9712-hifi")),
	DAILINK_COMP_ARRAY(COMP_PLATFORM("pxa-pcm-audio")));

SND_SOC_DAILINK_DEFS(ac97_aux,
	DAILINK_COMP_ARRAY(COMP_CPU("pxa2xx-ac97-aux")),
	DAILINK_COMP_ARRAY(COMP_CODEC("wm9712-codec", "wm9712-aux")),
	DAILINK_COMP_ARRAY(COMP_PLATFORM("pxa-pcm-audio")));

static struct snd_soc_dai_link tosa_dai[] = {
{
	.name = "AC97",
	.stream_name = "AC97 HiFi",
	.ops = &tosa_ops,
	SND_SOC_DAILINK_REG(ac97),
},
{
	.name = "AC97 Aux",
	.stream_name = "AC97 Aux",
	.ops = &tosa_ops,
	SND_SOC_DAILINK_REG(ac97_aux),
},
};

static struct snd_soc_card tosa = {
	.name = "Tosa",
	.owner = THIS_MODULE,
	.dai_link = tosa_dai,
	.num_links = ARRAY_SIZE(tosa_dai),

	.controls = tosa_controls,
	.num_controls = ARRAY_SIZE(tosa_controls),
	.dapm_widgets = tosa_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(tosa_dapm_widgets),
	.dapm_routes = audio_map,
	.num_dapm_routes = ARRAY_SIZE(audio_map),
	.fully_routed = true,
};

static int tosa_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = &tosa;
	int ret;

	ret = gpio_request_one(TOSA_GPIO_L_MUTE, GPIOF_OUT_INIT_LOW,
			       "Headphone Jack");
	if (ret)
		return ret;

	card->dev = &pdev->dev;

	ret = devm_snd_soc_register_card(&pdev->dev, card);
	if (ret) {
		dev_err(&pdev->dev, "snd_soc_register_card() failed: %d\n",
			ret);
		gpio_free(TOSA_GPIO_L_MUTE);
	}
	return ret;
}

static int tosa_remove(struct platform_device *pdev)
{
	gpio_free(TOSA_GPIO_L_MUTE);
	return 0;
}

static struct platform_driver tosa_driver = {
	.driver		= {
		.name	= "tosa-audio",
		.pm     = &snd_soc_pm_ops,
	},
	.probe		= tosa_probe,
	.remove		= tosa_remove,
};

module_platform_driver(tosa_driver);

/* Module information */
MODULE_AUTHOR("Richard Purdie");
MODULE_DESCRIPTION("ALSA SoC Tosa");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:tosa-audio");
