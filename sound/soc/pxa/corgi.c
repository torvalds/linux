// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * corgi.c  --  SoC audio for Corgi
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
#include <linux/gpio.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>

#include <asm/mach-types.h>
#include <mach/corgi.h>
#include <mach/audio.h>

#include "../codecs/wm8731.h"
#include "pxa2xx-i2s.h"

#define CORGI_HP        0
#define CORGI_MIC       1
#define CORGI_LINE      2
#define CORGI_HEADSET   3
#define CORGI_HP_OFF    4
#define CORGI_SPK_ON    0
#define CORGI_SPK_OFF   1

 /* audio clock in Hz - rounded from 12.235MHz */
#define CORGI_AUDIO_CLOCK 12288000

static int corgi_jack_func;
static int corgi_spk_func;

static void corgi_ext_control(struct snd_soc_dapm_context *dapm)
{
	snd_soc_dapm_mutex_lock(dapm);

	/* set up jack connection */
	switch (corgi_jack_func) {
	case CORGI_HP:
		/* set = unmute headphone */
		gpio_set_value(CORGI_GPIO_MUTE_L, 1);
		gpio_set_value(CORGI_GPIO_MUTE_R, 1);
		snd_soc_dapm_disable_pin_unlocked(dapm, "Mic Jack");
		snd_soc_dapm_disable_pin_unlocked(dapm, "Line Jack");
		snd_soc_dapm_enable_pin_unlocked(dapm, "Headphone Jack");
		snd_soc_dapm_disable_pin_unlocked(dapm, "Headset Jack");
		break;
	case CORGI_MIC:
		/* reset = mute headphone */
		gpio_set_value(CORGI_GPIO_MUTE_L, 0);
		gpio_set_value(CORGI_GPIO_MUTE_R, 0);
		snd_soc_dapm_enable_pin_unlocked(dapm, "Mic Jack");
		snd_soc_dapm_disable_pin_unlocked(dapm, "Line Jack");
		snd_soc_dapm_disable_pin_unlocked(dapm, "Headphone Jack");
		snd_soc_dapm_disable_pin_unlocked(dapm, "Headset Jack");
		break;
	case CORGI_LINE:
		gpio_set_value(CORGI_GPIO_MUTE_L, 0);
		gpio_set_value(CORGI_GPIO_MUTE_R, 0);
		snd_soc_dapm_disable_pin_unlocked(dapm, "Mic Jack");
		snd_soc_dapm_enable_pin_unlocked(dapm, "Line Jack");
		snd_soc_dapm_disable_pin_unlocked(dapm, "Headphone Jack");
		snd_soc_dapm_disable_pin_unlocked(dapm, "Headset Jack");
		break;
	case CORGI_HEADSET:
		gpio_set_value(CORGI_GPIO_MUTE_L, 0);
		gpio_set_value(CORGI_GPIO_MUTE_R, 1);
		snd_soc_dapm_enable_pin_unlocked(dapm, "Mic Jack");
		snd_soc_dapm_disable_pin_unlocked(dapm, "Line Jack");
		snd_soc_dapm_disable_pin_unlocked(dapm, "Headphone Jack");
		snd_soc_dapm_enable_pin_unlocked(dapm, "Headset Jack");
		break;
	}

	if (corgi_spk_func == CORGI_SPK_ON)
		snd_soc_dapm_enable_pin_unlocked(dapm, "Ext Spk");
	else
		snd_soc_dapm_disable_pin_unlocked(dapm, "Ext Spk");

	/* signal a DAPM event */
	snd_soc_dapm_sync_unlocked(dapm);

	snd_soc_dapm_mutex_unlock(dapm);
}

static int corgi_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;

	/* check the jack status at stream startup */
	corgi_ext_control(&rtd->card->dapm);

	return 0;
}

/* we need to unmute the HP at shutdown as the mute burns power on corgi */
static void corgi_shutdown(struct snd_pcm_substream *substream)
{
	/* set = unmute headphone */
	gpio_set_value(CORGI_GPIO_MUTE_L, 1);
	gpio_set_value(CORGI_GPIO_MUTE_R, 1);
}

static int corgi_hw_params(struct snd_pcm_substream *substream,
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

static const struct snd_soc_ops corgi_ops = {
	.startup = corgi_startup,
	.hw_params = corgi_hw_params,
	.shutdown = corgi_shutdown,
};

static int corgi_get_jack(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.enumerated.item[0] = corgi_jack_func;
	return 0;
}

static int corgi_set_jack(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_card *card = snd_kcontrol_chip(kcontrol);

	if (corgi_jack_func == ucontrol->value.enumerated.item[0])
		return 0;

	corgi_jack_func = ucontrol->value.enumerated.item[0];
	corgi_ext_control(&card->dapm);
	return 1;
}

static int corgi_get_spk(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.enumerated.item[0] = corgi_spk_func;
	return 0;
}

static int corgi_set_spk(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_card *card =  snd_kcontrol_chip(kcontrol);

	if (corgi_spk_func == ucontrol->value.enumerated.item[0])
		return 0;

	corgi_spk_func = ucontrol->value.enumerated.item[0];
	corgi_ext_control(&card->dapm);
	return 1;
}

static int corgi_amp_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *k, int event)
{
	gpio_set_value(CORGI_GPIO_APM_ON, SND_SOC_DAPM_EVENT_ON(event));
	return 0;
}

static int corgi_mic_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *k, int event)
{
	gpio_set_value(CORGI_GPIO_MIC_BIAS, SND_SOC_DAPM_EVENT_ON(event));
	return 0;
}

/* corgi machine dapm widgets */
static const struct snd_soc_dapm_widget wm8731_dapm_widgets[] = {
SND_SOC_DAPM_HP("Headphone Jack", NULL),
SND_SOC_DAPM_MIC("Mic Jack", corgi_mic_event),
SND_SOC_DAPM_SPK("Ext Spk", corgi_amp_event),
SND_SOC_DAPM_LINE("Line Jack", NULL),
SND_SOC_DAPM_HP("Headset Jack", NULL),
};

/* Corgi machine audio map (connections to the codec pins) */
static const struct snd_soc_dapm_route corgi_audio_map[] = {

	/* headset Jack  - in = micin, out = LHPOUT*/
	{"Headset Jack", NULL, "LHPOUT"},

	/* headphone connected to LHPOUT1, RHPOUT1 */
	{"Headphone Jack", NULL, "LHPOUT"},
	{"Headphone Jack", NULL, "RHPOUT"},

	/* speaker connected to LOUT, ROUT */
	{"Ext Spk", NULL, "ROUT"},
	{"Ext Spk", NULL, "LOUT"},

	/* mic is connected to MICIN (via right channel of headphone jack) */
	{"MICIN", NULL, "Mic Jack"},

	/* Same as the above but no mic bias for line signals */
	{"MICIN", NULL, "Line Jack"},
};

static const char * const jack_function[] = {"Headphone", "Mic", "Line",
	"Headset", "Off"};
static const char * const spk_function[] = {"On", "Off"};
static const struct soc_enum corgi_enum[] = {
	SOC_ENUM_SINGLE_EXT(5, jack_function),
	SOC_ENUM_SINGLE_EXT(2, spk_function),
};

static const struct snd_kcontrol_new wm8731_corgi_controls[] = {
	SOC_ENUM_EXT("Jack Function", corgi_enum[0], corgi_get_jack,
		corgi_set_jack),
	SOC_ENUM_EXT("Speaker Function", corgi_enum[1], corgi_get_spk,
		corgi_set_spk),
};

/* corgi digital audio interface glue - connects codec <--> CPU */
SND_SOC_DAILINK_DEFS(wm8731,
	DAILINK_COMP_ARRAY(COMP_CPU("pxa2xx-i2s")),
	DAILINK_COMP_ARRAY(COMP_CODEC("wm8731.0-001b", "wm8731-hifi")),
	DAILINK_COMP_ARRAY(COMP_PLATFORM("pxa-pcm-audio")));

static struct snd_soc_dai_link corgi_dai = {
	.name = "WM8731",
	.stream_name = "WM8731",
	.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
		   SND_SOC_DAIFMT_CBS_CFS,
	.ops = &corgi_ops,
	SND_SOC_DAILINK_REG(wm8731),
};

/* corgi audio machine driver */
static struct snd_soc_card corgi = {
	.name = "Corgi",
	.owner = THIS_MODULE,
	.dai_link = &corgi_dai,
	.num_links = 1,

	.controls = wm8731_corgi_controls,
	.num_controls = ARRAY_SIZE(wm8731_corgi_controls),
	.dapm_widgets = wm8731_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(wm8731_dapm_widgets),
	.dapm_routes = corgi_audio_map,
	.num_dapm_routes = ARRAY_SIZE(corgi_audio_map),
	.fully_routed = true,
};

static int corgi_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = &corgi;
	int ret;

	card->dev = &pdev->dev;

	ret = devm_snd_soc_register_card(&pdev->dev, card);
	if (ret)
		dev_err(&pdev->dev, "snd_soc_register_card() failed: %d\n",
			ret);
	return ret;
}

static struct platform_driver corgi_driver = {
	.driver		= {
		.name	= "corgi-audio",
		.pm     = &snd_soc_pm_ops,
	},
	.probe		= corgi_probe,
};

module_platform_driver(corgi_driver);

/* Module information */
MODULE_AUTHOR("Richard Purdie");
MODULE_DESCRIPTION("ALSA SoC Corgi");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:corgi-audio");
