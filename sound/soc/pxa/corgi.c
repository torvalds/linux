/*
 * corgi.c  --  SoC audio for Corgi
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

static void corgi_ext_control(struct snd_soc_codec *codec)
{
	struct snd_soc_dapm_context *dapm = &codec->dapm;

	/* set up jack connection */
	switch (corgi_jack_func) {
	case CORGI_HP:
		/* set = unmute headphone */
		gpio_set_value(CORGI_GPIO_MUTE_L, 1);
		gpio_set_value(CORGI_GPIO_MUTE_R, 1);
		snd_soc_dapm_disable_pin(dapm, "Mic Jack");
		snd_soc_dapm_disable_pin(dapm, "Line Jack");
		snd_soc_dapm_enable_pin(dapm, "Headphone Jack");
		snd_soc_dapm_disable_pin(dapm, "Headset Jack");
		break;
	case CORGI_MIC:
		/* reset = mute headphone */
		gpio_set_value(CORGI_GPIO_MUTE_L, 0);
		gpio_set_value(CORGI_GPIO_MUTE_R, 0);
		snd_soc_dapm_enable_pin(dapm, "Mic Jack");
		snd_soc_dapm_disable_pin(dapm, "Line Jack");
		snd_soc_dapm_disable_pin(dapm, "Headphone Jack");
		snd_soc_dapm_disable_pin(dapm, "Headset Jack");
		break;
	case CORGI_LINE:
		gpio_set_value(CORGI_GPIO_MUTE_L, 0);
		gpio_set_value(CORGI_GPIO_MUTE_R, 0);
		snd_soc_dapm_disable_pin(dapm, "Mic Jack");
		snd_soc_dapm_enable_pin(dapm, "Line Jack");
		snd_soc_dapm_disable_pin(dapm, "Headphone Jack");
		snd_soc_dapm_disable_pin(dapm, "Headset Jack");
		break;
	case CORGI_HEADSET:
		gpio_set_value(CORGI_GPIO_MUTE_L, 0);
		gpio_set_value(CORGI_GPIO_MUTE_R, 1);
		snd_soc_dapm_enable_pin(dapm, "Mic Jack");
		snd_soc_dapm_disable_pin(dapm, "Line Jack");
		snd_soc_dapm_disable_pin(dapm, "Headphone Jack");
		snd_soc_dapm_enable_pin(dapm, "Headset Jack");
		break;
	}

	if (corgi_spk_func == CORGI_SPK_ON)
		snd_soc_dapm_enable_pin(dapm, "Ext Spk");
	else
		snd_soc_dapm_disable_pin(dapm, "Ext Spk");

	/* signal a DAPM event */
	snd_soc_dapm_sync(dapm);
}

static int corgi_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_codec *codec = rtd->codec;

	mutex_lock(&codec->mutex);

	/* check the jack status at stream startup */
	corgi_ext_control(codec);

	mutex_unlock(&codec->mutex);

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
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
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

	/* set codec DAI configuration */
	ret = snd_soc_dai_set_fmt(codec_dai, SND_SOC_DAIFMT_I2S |
		SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBS_CFS);
	if (ret < 0)
		return ret;

	/* set cpu DAI configuration */
	ret = snd_soc_dai_set_fmt(cpu_dai, SND_SOC_DAIFMT_I2S |
		SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBS_CFS);
	if (ret < 0)
		return ret;

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

static struct snd_soc_ops corgi_ops = {
	.startup = corgi_startup,
	.hw_params = corgi_hw_params,
	.shutdown = corgi_shutdown,
};

static int corgi_get_jack(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = corgi_jack_func;
	return 0;
}

static int corgi_set_jack(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);

	if (corgi_jack_func == ucontrol->value.integer.value[0])
		return 0;

	corgi_jack_func = ucontrol->value.integer.value[0];
	corgi_ext_control(codec);
	return 1;
}

static int corgi_get_spk(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = corgi_spk_func;
	return 0;
}

static int corgi_set_spk(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec =  snd_kcontrol_chip(kcontrol);

	if (corgi_spk_func == ucontrol->value.integer.value[0])
		return 0;

	corgi_spk_func = ucontrol->value.integer.value[0];
	corgi_ext_control(codec);
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
static const struct snd_soc_dapm_route audio_map[] = {

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

static const char *jack_function[] = {"Headphone", "Mic", "Line", "Headset",
	"Off"};
static const char *spk_function[] = {"On", "Off"};
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

/*
 * Logic for a wm8731 as connected on a Sharp SL-C7x0 Device
 */
static int corgi_wm8731_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_soc_dapm_context *dapm = &codec->dapm;
	int err;

	snd_soc_dapm_nc_pin(dapm, "LLINEIN");
	snd_soc_dapm_nc_pin(dapm, "RLINEIN");

	/* Add corgi specific controls */
	err = snd_soc_add_controls(codec, wm8731_corgi_controls,
				ARRAY_SIZE(wm8731_corgi_controls));
	if (err < 0)
		return err;

	/* Add corgi specific widgets */
	snd_soc_dapm_new_controls(dapm, wm8731_dapm_widgets,
				  ARRAY_SIZE(wm8731_dapm_widgets));

	/* Set up corgi specific audio path audio_map */
	snd_soc_dapm_add_routes(dapm, audio_map, ARRAY_SIZE(audio_map));

	snd_soc_dapm_sync(dapm);
	return 0;
}

/* corgi digital audio interface glue - connects codec <--> CPU */
static struct snd_soc_dai_link corgi_dai = {
	.name = "WM8731",
	.stream_name = "WM8731",
	.cpu_dai_name = "pxa2xx-i2s",
	.codec_dai_name = "wm8731-hifi",
	.platform_name = "pxa-pcm-audio",
	.codec_name = "wm8731.0-001b",
	.init = corgi_wm8731_init,
	.ops = &corgi_ops,
};

/* corgi audio machine driver */
static struct snd_soc_card snd_soc_corgi = {
	.name = "Corgi",
	.dai_link = &corgi_dai,
	.num_links = 1,
};

static struct platform_device *corgi_snd_device;

static int __init corgi_init(void)
{
	int ret;

	if (!(machine_is_corgi() || machine_is_shepherd() ||
	      machine_is_husky()))
		return -ENODEV;

	corgi_snd_device = platform_device_alloc("soc-audio", -1);
	if (!corgi_snd_device)
		return -ENOMEM;

	platform_set_drvdata(corgi_snd_device, &snd_soc_corgi);
	ret = platform_device_add(corgi_snd_device);

	if (ret)
		platform_device_put(corgi_snd_device);

	return ret;
}

static void __exit corgi_exit(void)
{
	platform_device_unregister(corgi_snd_device);
}

module_init(corgi_init);
module_exit(corgi_exit);

/* Module information */
MODULE_AUTHOR("Richard Purdie");
MODULE_DESCRIPTION("ALSA SoC Corgi");
MODULE_LICENSE("GPL");
