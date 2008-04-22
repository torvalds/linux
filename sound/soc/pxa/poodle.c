/*
 * poodle.c  --  SoC audio for Poodle
 *
 * Copyright 2005 Wolfson Microelectronics PLC.
 * Copyright 2005 Openedhand Ltd.
 *
 * Authors: Liam Girdwood <liam.girdwood@wolfsonmicro.com>
 *          Richard Purdie <richard@openedhand.com>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>

#include <asm/mach-types.h>
#include <asm/hardware/locomo.h>
#include <asm/arch/pxa-regs.h>
#include <asm/arch/hardware.h>
#include <asm/arch/poodle.h>
#include <asm/arch/audio.h>

#include "../codecs/wm8731.h"
#include "pxa2xx-pcm.h"
#include "pxa2xx-i2s.h"

#define POODLE_HP        1
#define POODLE_HP_OFF    0
#define POODLE_SPK_ON    1
#define POODLE_SPK_OFF   0

 /* audio clock in Hz - rounded from 12.235MHz */
#define POODLE_AUDIO_CLOCK 12288000

static int poodle_jack_func;
static int poodle_spk_func;

static void poodle_ext_control(struct snd_soc_codec *codec)
{
	int spk = 0;

	/* set up jack connection */
	if (poodle_jack_func == POODLE_HP) {
		/* set = unmute headphone */
		locomo_gpio_write(&poodle_locomo_device.dev,
			POODLE_LOCOMO_GPIO_MUTE_L, 1);
		locomo_gpio_write(&poodle_locomo_device.dev,
			POODLE_LOCOMO_GPIO_MUTE_R, 1);
		snd_soc_dapm_set_endpoint(codec, "Headphone Jack", 1);
	} else {
		locomo_gpio_write(&poodle_locomo_device.dev,
			POODLE_LOCOMO_GPIO_MUTE_L, 0);
		locomo_gpio_write(&poodle_locomo_device.dev,
			POODLE_LOCOMO_GPIO_MUTE_R, 0);
		snd_soc_dapm_set_endpoint(codec, "Headphone Jack", 0);
	}

	if (poodle_spk_func == POODLE_SPK_ON)
		spk = 1;

	/* set the enpoints to their new connetion states */
	snd_soc_dapm_set_endpoint(codec, "Ext Spk", spk);

	/* signal a DAPM event */
	snd_soc_dapm_sync_endpoints(codec);
}

static int poodle_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_codec *codec = rtd->socdev->codec;

	/* check the jack status at stream startup */
	poodle_ext_control(codec);
	return 0;
}

/* we need to unmute the HP at shutdown as the mute burns power on poodle */
static int poodle_shutdown(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_codec *codec = rtd->socdev->codec;

	/* set = unmute headphone */
	locomo_gpio_write(&poodle_locomo_device.dev,
		POODLE_LOCOMO_GPIO_MUTE_L, 1);
	locomo_gpio_write(&poodle_locomo_device.dev,
		POODLE_LOCOMO_GPIO_MUTE_R, 1);
	return 0;
}

static int poodle_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_codec_dai *codec_dai = rtd->dai->codec_dai;
	struct snd_soc_cpu_dai *cpu_dai = rtd->dai->cpu_dai;
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
	ret = codec_dai->dai_ops.set_fmt(codec_dai, SND_SOC_DAIFMT_I2S |
		SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBS_CFS);
	if (ret < 0)
		return ret;

	/* set cpu DAI configuration */
	ret = cpu_dai->dai_ops.set_fmt(cpu_dai, SND_SOC_DAIFMT_I2S |
		SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBS_CFS);
	if (ret < 0)
		return ret;

	/* set the codec system clock for DAC and ADC */
	ret = codec_dai->dai_ops.set_sysclk(codec_dai, WM8731_SYSCLK, clk,
		SND_SOC_CLOCK_IN);
	if (ret < 0)
		return ret;

	/* set the I2S system clock as input (unused) */
	ret = cpu_dai->dai_ops.set_sysclk(cpu_dai, PXA2XX_I2S_SYSCLK, 0,
		SND_SOC_CLOCK_IN);
	if (ret < 0)
		return ret;

	return 0;
}

static struct snd_soc_ops poodle_ops = {
	.startup = poodle_startup,
	.hw_params = poodle_hw_params,
	.shutdown = poodle_shutdown,
};

static int poodle_get_jack(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = poodle_jack_func;
	return 0;
}

static int poodle_set_jack(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec =  snd_kcontrol_chip(kcontrol);

	if (poodle_jack_func == ucontrol->value.integer.value[0])
		return 0;

	poodle_jack_func = ucontrol->value.integer.value[0];
	poodle_ext_control(codec);
	return 1;
}

static int poodle_get_spk(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = poodle_spk_func;
	return 0;
}

static int poodle_set_spk(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec =  snd_kcontrol_chip(kcontrol);

	if (poodle_spk_func == ucontrol->value.integer.value[0])
		return 0;

	poodle_spk_func = ucontrol->value.integer.value[0];
	poodle_ext_control(codec);
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
};

/* Corgi machine audio_mapnections to the codec pins */
static const char *audio_map[][3] = {

	/* headphone connected to LHPOUT1, RHPOUT1 */
	{"Headphone Jack", NULL, "LHPOUT"},
	{"Headphone Jack", NULL, "RHPOUT"},

	/* speaker connected to LOUT, ROUT */
	{"Ext Spk", NULL, "ROUT"},
	{"Ext Spk", NULL, "LOUT"},

	{NULL, NULL, NULL},
};

static const char *jack_function[] = {"Off", "Headphone"};
static const char *spk_function[] = {"Off", "On"};
static const struct soc_enum poodle_enum[] = {
	SOC_ENUM_SINGLE_EXT(2, jack_function),
	SOC_ENUM_SINGLE_EXT(2, spk_function),
};

static const snd_kcontrol_new_t wm8731_poodle_controls[] = {
	SOC_ENUM_EXT("Jack Function", poodle_enum[0], poodle_get_jack,
		poodle_set_jack),
	SOC_ENUM_EXT("Speaker Function", poodle_enum[1], poodle_get_spk,
		poodle_set_spk),
};

/*
 * Logic for a wm8731 as connected on a Sharp SL-C7x0 Device
 */
static int poodle_wm8731_init(struct snd_soc_codec *codec)
{
	int i, err;

	snd_soc_dapm_set_endpoint(codec, "LLINEIN", 0);
	snd_soc_dapm_set_endpoint(codec, "RLINEIN", 0);
	snd_soc_dapm_set_endpoint(codec, "MICIN", 1);

	/* Add poodle specific controls */
	for (i = 0; i < ARRAY_SIZE(wm8731_poodle_controls); i++) {
		err = snd_ctl_add(codec->card,
			snd_soc_cnew(&wm8731_poodle_controls[i],codec, NULL));
		if (err < 0)
			return err;
	}

	/* Add poodle specific widgets */
	for (i = 0; i < ARRAY_SIZE(wm8731_dapm_widgets); i++) {
		snd_soc_dapm_new_control(codec, &wm8731_dapm_widgets[i]);
	}

	/* Set up poodle specific audio path audio_map */
	for (i = 0; audio_map[i][0] != NULL; i++) {
		snd_soc_dapm_connect_input(codec, audio_map[i][0],
			audio_map[i][1], audio_map[i][2]);
	}

	snd_soc_dapm_sync_endpoints(codec);
	return 0;
}

/* poodle digital audio interface glue - connects codec <--> CPU */
static struct snd_soc_dai_link poodle_dai = {
	.name = "WM8731",
	.stream_name = "WM8731",
	.cpu_dai = &pxa_i2s_dai,
	.codec_dai = &wm8731_dai,
	.init = poodle_wm8731_init,
	.ops = &poodle_ops,
};

/* poodle audio machine driver */
static struct snd_soc_machine snd_soc_machine_poodle = {
	.name = "Poodle",
	.dai_link = &poodle_dai,
	.num_links = 1,
};

/* poodle audio private data */
static struct wm8731_setup_data poodle_wm8731_setup = {
	.i2c_address = 0x1b,
};

/* poodle audio subsystem */
static struct snd_soc_device poodle_snd_devdata = {
	.machine = &snd_soc_machine_poodle,
	.platform = &pxa2xx_soc_platform,
	.codec_dev = &soc_codec_dev_wm8731,
	.codec_data = &poodle_wm8731_setup,
};

static struct platform_device *poodle_snd_device;

static int __init poodle_init(void)
{
	int ret;

	if (!machine_is_poodle())
		return -ENODEV;

	locomo_gpio_set_dir(&poodle_locomo_device.dev,
		POODLE_LOCOMO_GPIO_AMP_ON, 0);
	/* should we mute HP at startup - burning power ?*/
	locomo_gpio_set_dir(&poodle_locomo_device.dev,
		POODLE_LOCOMO_GPIO_MUTE_L, 0);
	locomo_gpio_set_dir(&poodle_locomo_device.dev,
		POODLE_LOCOMO_GPIO_MUTE_R, 0);

	poodle_snd_device = platform_device_alloc("soc-audio", -1);
	if (!poodle_snd_device)
		return -ENOMEM;

	platform_set_drvdata(poodle_snd_device, &poodle_snd_devdata);
	poodle_snd_devdata.dev = &poodle_snd_device->dev;
	ret = platform_device_add(poodle_snd_device);

	if (ret)
		platform_device_put(poodle_snd_device);

	return ret;
}

static void __exit poodle_exit(void)
{
	platform_device_unregister(poodle_snd_device);
}

module_init(poodle_init);
module_exit(poodle_exit);

/* Module information */
MODULE_AUTHOR("Richard Purdie");
MODULE_DESCRIPTION("ALSA SoC Poodle");
MODULE_LICENSE("GPL");
