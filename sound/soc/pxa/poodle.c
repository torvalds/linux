// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * poodle.c  --  SoC audio for Poodle
 *
 * Copyright 2005 Wolfson Microelectronics PLC.
 * Copyright 2005 Openedhand Ltd.
 *
 * Authors: Liam Girdwood <lrg@slimlogic.co.uk>
 *          Richard Purdie <richard@openedhand.com>
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/timer.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>

#include <asm/mach-types.h>
#include <asm/hardware/locomo.h>
#include <mach/poodle.h>
#include <mach/audio.h>

#include "../codecs/wm8731.h"
#include "pxa2xx-i2s.h"

#define POODLE_HP        1
#define POODLE_HP_OFF    0
#define POODLE_SPK_ON    1
#define POODLE_SPK_OFF   0

 /* audio clock in Hz - rounded from 12.235MHz */
#define POODLE_AUDIO_CLOCK 12288000

static int poodle_jack_func;
static int poodle_spk_func;

static void poodle_ext_control(struct snd_soc_dapm_context *dapm)
{
	/* set up jack connection */
	if (poodle_jack_func == POODLE_HP) {
		/* set = unmute headphone */
		locomo_gpio_write(&poodle_locomo_device.dev,
			POODLE_LOCOMO_GPIO_MUTE_L, 1);
		locomo_gpio_write(&poodle_locomo_device.dev,
			POODLE_LOCOMO_GPIO_MUTE_R, 1);
		snd_soc_dapm_enable_pin(dapm, "Headphone Jack");
	} else {
		locomo_gpio_write(&poodle_locomo_device.dev,
			POODLE_LOCOMO_GPIO_MUTE_L, 0);
		locomo_gpio_write(&poodle_locomo_device.dev,
			POODLE_LOCOMO_GPIO_MUTE_R, 0);
		snd_soc_dapm_disable_pin(dapm, "Headphone Jack");
	}

	/* set the endpoints to their new connection states */
	if (poodle_spk_func == POODLE_SPK_ON)
		snd_soc_dapm_enable_pin(dapm, "Ext Spk");
	else
		snd_soc_dapm_disable_pin(dapm, "Ext Spk");

	/* signal a DAPM event */
	snd_soc_dapm_sync(dapm);
}

static int poodle_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;

	/* check the jack status at stream startup */
	poodle_ext_control(&rtd->card->dapm);

	return 0;
}

/* we need to unmute the HP at shutdown as the mute burns power on poodle */
static void poodle_shutdown(struct snd_pcm_substream *substream)
{
	/* set = unmute headphone */
	locomo_gpio_write(&poodle_locomo_device.dev,
		POODLE_LOCOMO_GPIO_MUTE_L, 1);
	locomo_gpio_write(&poodle_locomo_device.dev,
		POODLE_LOCOMO_GPIO_MUTE_R, 1);
}

static int poodle_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = asoc_rtd_to_codec(rtd, 0);
	struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(rtd, 0);
	unsigned int clk = 0;
	int ret = 0;

	switch (params_rate(params)) {
	case 8000:
	case 16000:
	case 48000:
	case 96000:
		clk = 12288000;
		break;
	case 11025:
	case 22050:
	case 44100:
		clk = 11289600;
		break;
	}

	/* set the codec system clock for DAC and ADC */
	ret = snd_soc_dai_set_sysclk(codec_dai, WM8731_SYSCLK_XTAL, clk,
		SND_SOC_CLOCK_IN);
	if (ret < 0)
		return ret;

	/* set the I2S system clock as input (unused) */
	ret = snd_soc_dai_set_sysclk(cpu_dai, PXA2XX_I2S_SYSCLK, 0,
		SND_SOC_CLOCK_IN);
	if (ret < 0)
		return ret;

	return 0;
}

static const struct snd_soc_ops poodle_ops = {
	.startup = poodle_startup,
	.hw_params = poodle_hw_params,
	.shutdown = poodle_shutdown,
};

static int poodle_get_jack(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.enumerated.item[0] = poodle_jack_func;
	return 0;
}

static int poodle_set_jack(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_card *card =  snd_kcontrol_chip(kcontrol);

	if (poodle_jack_func == ucontrol->value.enumerated.item[0])
		return 0;

	poodle_jack_func = ucontrol->value.enumerated.item[0];
	poodle_ext_control(&card->dapm);
	return 1;
}

static int poodle_get_spk(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.enumerated.item[0] = poodle_spk_func;
	return 0;
}

static int poodle_set_spk(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_card *card =  snd_kcontrol_chip(kcontrol);

	if (poodle_spk_func == ucontrol->value.enumerated.item[0])
		return 0;

	poodle_spk_func = ucontrol->value.enumerated.item[0];
	poodle_ext_control(&card->dapm);
	return 1;
}

static int poodle_amp_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *k, int event)
{
	if (SND_SOC_DAPM_EVENT_ON(event))
		locomo_gpio_write(&poodle_locomo_device.dev,
			POODLE_LOCOMO_GPIO_AMP_ON, 0);
	else
		locomo_gpio_write(&poodle_locomo_device.dev,
			POODLE_LOCOMO_GPIO_AMP_ON, 1);

	return 0;
}

/* poodle machine dapm widgets */
static const struct snd_soc_dapm_widget wm8731_dapm_widgets[] = {
SND_SOC_DAPM_HP("Headphone Jack", NULL),
SND_SOC_DAPM_SPK("Ext Spk", poodle_amp_event),
SND_SOC_DAPM_MIC("Microphone", NULL),
};

/* Corgi machine connections to the codec pins */
static const struct snd_soc_dapm_route poodle_audio_map[] = {

	/* headphone connected to LHPOUT1, RHPOUT1 */
	{"Headphone Jack", NULL, "LHPOUT"},
	{"Headphone Jack", NULL, "RHPOUT"},

	/* speaker connected to LOUT, ROUT */
	{"Ext Spk", NULL, "ROUT"},
	{"Ext Spk", NULL, "LOUT"},

	{"MICIN", NULL, "Microphone"},
};

static const char * const jack_function[] = {"Off", "Headphone"};
static const char * const spk_function[] = {"Off", "On"};
static const struct soc_enum poodle_enum[] = {
	SOC_ENUM_SINGLE_EXT(2, jack_function),
	SOC_ENUM_SINGLE_EXT(2, spk_function),
};

static const struct snd_kcontrol_new wm8731_poodle_controls[] = {
	SOC_ENUM_EXT("Jack Function", poodle_enum[0], poodle_get_jack,
		poodle_set_jack),
	SOC_ENUM_EXT("Speaker Function", poodle_enum[1], poodle_get_spk,
		poodle_set_spk),
};

/* poodle digital audio interface glue - connects codec <--> CPU */
SND_SOC_DAILINK_DEFS(wm8731,
	DAILINK_COMP_ARRAY(COMP_CPU("pxa2xx-i2s")),
	DAILINK_COMP_ARRAY(COMP_CODEC("wm8731.0-001b", "wm8731-hifi")),
	DAILINK_COMP_ARRAY(COMP_PLATFORM("pxa-pcm-audio")));

static struct snd_soc_dai_link poodle_dai = {
	.name = "WM8731",
	.stream_name = "WM8731",
	.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
		   SND_SOC_DAIFMT_CBS_CFS,
	.ops = &poodle_ops,
	SND_SOC_DAILINK_REG(wm8731),
};

/* poodle audio machine driver */
static struct snd_soc_card poodle = {
	.name = "Poodle",
	.dai_link = &poodle_dai,
	.num_links = 1,
	.owner = THIS_MODULE,

	.controls = wm8731_poodle_controls,
	.num_controls = ARRAY_SIZE(wm8731_poodle_controls),
	.dapm_widgets = wm8731_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(wm8731_dapm_widgets),
	.dapm_routes = poodle_audio_map,
	.num_dapm_routes = ARRAY_SIZE(poodle_audio_map),
	.fully_routed = true,
};

static int poodle_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = &poodle;
	int ret;

	locomo_gpio_set_dir(&poodle_locomo_device.dev,
		POODLE_LOCOMO_GPIO_AMP_ON, 0);
	/* should we mute HP at startup - burning power ?*/
	locomo_gpio_set_dir(&poodle_locomo_device.dev,
		POODLE_LOCOMO_GPIO_MUTE_L, 0);
	locomo_gpio_set_dir(&poodle_locomo_device.dev,
		POODLE_LOCOMO_GPIO_MUTE_R, 0);

	card->dev = &pdev->dev;

	ret = devm_snd_soc_register_card(&pdev->dev, card);
	if (ret)
		dev_err(&pdev->dev, "snd_soc_register_card() failed: %d\n",
			ret);
	return ret;
}

static struct platform_driver poodle_driver = {
	.driver		= {
		.name	= "poodle-audio",
		.pm     = &snd_soc_pm_ops,
	},
	.probe		= poodle_probe,
};

module_platform_driver(poodle_driver);

/* Module information */
MODULE_AUTHOR("Richard Purdie");
MODULE_DESCRIPTION("ALSA SoC Poodle");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:poodle-audio");
