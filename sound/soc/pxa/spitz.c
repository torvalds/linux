/*
 * spitz.c  --  SoC audio for Sharp SL-Cxx00 models Spitz, Borzoi and Akita
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
 *  Revision history
 *    30th Nov 2005   Initial version.
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
#include <asm/hardware/scoop.h>
#include <asm/arch/pxa-regs.h>
#include <asm/arch/hardware.h>
#include <asm/arch/akita.h>
#include <asm/arch/spitz.h>
#include "../codecs/wm8750.h"
#include "pxa2xx-pcm.h"
#include "pxa2xx-i2s.h"

#define SPITZ_HP        0
#define SPITZ_MIC       1
#define SPITZ_LINE      2
#define SPITZ_HEADSET   3
#define SPITZ_HP_OFF    4
#define SPITZ_SPK_ON    0
#define SPITZ_SPK_OFF   1

 /* audio clock in Hz - rounded from 12.235MHz */
#define SPITZ_AUDIO_CLOCK 12288000

static int spitz_jack_func;
static int spitz_spk_func;

static void spitz_ext_control(struct snd_soc_codec *codec)
{
	if (spitz_spk_func == SPITZ_SPK_ON)
		snd_soc_dapm_set_endpoint(codec, "Ext Spk", 1);
	else
		snd_soc_dapm_set_endpoint(codec, "Ext Spk", 0);

	/* set up jack connection */
	switch (spitz_jack_func) {
	case SPITZ_HP:
		/* enable and unmute hp jack, disable mic bias */
		snd_soc_dapm_set_endpoint(codec, "Headset Jack", 0);
		snd_soc_dapm_set_endpoint(codec, "Mic Jack", 0);
		snd_soc_dapm_set_endpoint(codec, "Line Jack", 0);
		snd_soc_dapm_set_endpoint(codec, "Headphone Jack", 1);
		set_scoop_gpio(&spitzscoop_device.dev, SPITZ_SCP_MUTE_L);
		set_scoop_gpio(&spitzscoop_device.dev, SPITZ_SCP_MUTE_R);
		break;
	case SPITZ_MIC:
		/* enable mic jack and bias, mute hp */
		snd_soc_dapm_set_endpoint(codec, "Headphone Jack", 0);
		snd_soc_dapm_set_endpoint(codec, "Headset Jack", 0);
		snd_soc_dapm_set_endpoint(codec, "Line Jack", 0);
		snd_soc_dapm_set_endpoint(codec, "Mic Jack", 1);
		reset_scoop_gpio(&spitzscoop_device.dev, SPITZ_SCP_MUTE_L);
		reset_scoop_gpio(&spitzscoop_device.dev, SPITZ_SCP_MUTE_R);
		break;
	case SPITZ_LINE:
		/* enable line jack, disable mic bias and mute hp */
		snd_soc_dapm_set_endpoint(codec, "Headphone Jack", 0);
		snd_soc_dapm_set_endpoint(codec, "Headset Jack", 0);
		snd_soc_dapm_set_endpoint(codec, "Mic Jack", 0);
		snd_soc_dapm_set_endpoint(codec, "Line Jack", 1);
		reset_scoop_gpio(&spitzscoop_device.dev, SPITZ_SCP_MUTE_L);
		reset_scoop_gpio(&spitzscoop_device.dev, SPITZ_SCP_MUTE_R);
		break;
	case SPITZ_HEADSET:
		/* enable and unmute headset jack enable mic bias, mute L hp */
		snd_soc_dapm_set_endpoint(codec, "Headphone Jack", 0);
		snd_soc_dapm_set_endpoint(codec, "Mic Jack", 1);
		snd_soc_dapm_set_endpoint(codec, "Line Jack", 0);
		snd_soc_dapm_set_endpoint(codec, "Headset Jack", 1);
		reset_scoop_gpio(&spitzscoop_device.dev, SPITZ_SCP_MUTE_L);
		set_scoop_gpio(&spitzscoop_device.dev, SPITZ_SCP_MUTE_R);
		break;
	case SPITZ_HP_OFF:

		/* jack removed, everything off */
		snd_soc_dapm_set_endpoint(codec, "Headphone Jack", 0);
		snd_soc_dapm_set_endpoint(codec, "Headset Jack", 0);
		snd_soc_dapm_set_endpoint(codec, "Mic Jack", 0);
		snd_soc_dapm_set_endpoint(codec, "Line Jack", 0);
		reset_scoop_gpio(&spitzscoop_device.dev, SPITZ_SCP_MUTE_L);
		reset_scoop_gpio(&spitzscoop_device.dev, SPITZ_SCP_MUTE_R);
		break;
	}
	snd_soc_dapm_sync_endpoints(codec);
}

static int spitz_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_codec *codec = rtd->socdev->codec;

	/* check the jack status at stream startup */
	spitz_ext_control(codec);
	return 0;
}

static int spitz_hw_params(struct snd_pcm_substream *substream,
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
	ret = codec_dai->dai_ops.set_sysclk(codec_dai, WM8750_SYSCLK, clk,
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

static struct snd_soc_ops spitz_ops = {
	.startup = spitz_startup,
	.hw_params = spitz_hw_params,
};

static int spitz_get_jack(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = spitz_jack_func;
	return 0;
}

static int spitz_set_jack(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);

	if (spitz_jack_func == ucontrol->value.integer.value[0])
		return 0;

	spitz_jack_func = ucontrol->value.integer.value[0];
	spitz_ext_control(codec);
	return 1;
}

static int spitz_get_spk(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = spitz_spk_func;
	return 0;
}

static int spitz_set_spk(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec =  snd_kcontrol_chip(kcontrol);

	if (spitz_spk_func == ucontrol->value.integer.value[0])
		return 0;

	spitz_spk_func = ucontrol->value.integer.value[0];
	spitz_ext_control(codec);
	return 1;
}

static int spitz_mic_bias(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *k, int event)
{
	if (machine_is_borzoi() || machine_is_spitz()) {
		if (SND_SOC_DAPM_EVENT_ON(event))
			set_scoop_gpio(&spitzscoop2_device.dev,
				SPITZ_SCP2_MIC_BIAS);
		else
			reset_scoop_gpio(&spitzscoop2_device.dev,
				SPITZ_SCP2_MIC_BIAS);
	}

	if (machine_is_akita()) {
		if (SND_SOC_DAPM_EVENT_ON(event))
			akita_set_ioexp(&akitaioexp_device.dev,
				AKITA_IOEXP_MIC_BIAS);
		else
			akita_reset_ioexp(&akitaioexp_device.dev,
				AKITA_IOEXP_MIC_BIAS);
	}
	return 0;
}

/* spitz machine dapm widgets */
static const struct snd_soc_dapm_widget wm8750_dapm_widgets[] = {
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_MIC("Mic Jack", spitz_mic_bias),
	SND_SOC_DAPM_SPK("Ext Spk", NULL),
	SND_SOC_DAPM_LINE("Line Jack", NULL),

	/* headset is a mic and mono headphone */
	SND_SOC_DAPM_HP("Headset Jack", NULL),
};

/* Spitz machine audio_map */
static const char *audio_map[][3] = {

	/* headphone connected to LOUT1, ROUT1 */
	{"Headphone Jack", NULL, "LOUT1"},
	{"Headphone Jack", NULL, "ROUT1"},

	/* headset connected to ROUT1 and LINPUT1 with bias (def below) */
	{"Headset Jack", NULL, "ROUT1"},

	/* ext speaker connected to LOUT2, ROUT2  */
	{"Ext Spk", NULL , "ROUT2"},
	{"Ext Spk", NULL , "LOUT2"},

	/* mic is connected to input 1 - with bias */
	{"LINPUT1", NULL, "Mic Bias"},
	{"Mic Bias", NULL, "Mic Jack"},

	/* line is connected to input 1 - no bias */
	{"LINPUT1", NULL, "Line Jack"},

	{NULL, NULL, NULL},
};

static const char *jack_function[] = {"Headphone", "Mic", "Line", "Headset",
	"Off"};
static const char *spk_function[] = {"On", "Off"};
static const struct soc_enum spitz_enum[] = {
	SOC_ENUM_SINGLE_EXT(5, jack_function),
	SOC_ENUM_SINGLE_EXT(2, spk_function),
};

static const struct snd_kcontrol_new wm8750_spitz_controls[] = {
	SOC_ENUM_EXT("Jack Function", spitz_enum[0], spitz_get_jack,
		spitz_set_jack),
	SOC_ENUM_EXT("Speaker Function", spitz_enum[1], spitz_get_spk,
		spitz_set_spk),
};

/*
 * Logic for a wm8750 as connected on a Sharp SL-Cxx00 Device
 */
static int spitz_wm8750_init(struct snd_soc_codec *codec)
{
	int i, err;

	/* NC codec pins */
	snd_soc_dapm_set_endpoint(codec, "RINPUT1", 0);
	snd_soc_dapm_set_endpoint(codec, "LINPUT2", 0);
	snd_soc_dapm_set_endpoint(codec, "RINPUT2", 0);
	snd_soc_dapm_set_endpoint(codec, "LINPUT3", 0);
	snd_soc_dapm_set_endpoint(codec, "RINPUT3", 0);
	snd_soc_dapm_set_endpoint(codec, "OUT3", 0);
	snd_soc_dapm_set_endpoint(codec, "MONO", 0);

	/* Add spitz specific controls */
	for (i = 0; i < ARRAY_SIZE(wm8750_spitz_controls); i++) {
		err = snd_ctl_add(codec->card,
			snd_soc_cnew(&wm8750_spitz_controls[i], codec, NULL));
		if (err < 0)
			return err;
	}

	/* Add spitz specific widgets */
	for (i = 0; i < ARRAY_SIZE(wm8750_dapm_widgets); i++) {
		snd_soc_dapm_new_control(codec, &wm8750_dapm_widgets[i]);
	}

	/* Set up spitz specific audio path audio_map */
	for (i = 0; audio_map[i][0] != NULL; i++) {
		snd_soc_dapm_connect_input(codec, audio_map[i][0],
			audio_map[i][1], audio_map[i][2]);
	}

	snd_soc_dapm_sync_endpoints(codec);
	return 0;
}

/* spitz digital audio interface glue - connects codec <--> CPU */
static struct snd_soc_dai_link spitz_dai = {
	.name = "wm8750",
	.stream_name = "WM8750",
	.cpu_dai = &pxa_i2s_dai,
	.codec_dai = &wm8750_dai,
	.init = spitz_wm8750_init,
	.ops = &spitz_ops,
};

/* spitz audio machine driver */
static struct snd_soc_machine snd_soc_machine_spitz = {
	.name = "Spitz",
	.dai_link = &spitz_dai,
	.num_links = 1,
};

/* spitz audio private data */
static struct wm8750_setup_data spitz_wm8750_setup = {
	.i2c_address = 0x1b,
};

/* spitz audio subsystem */
static struct snd_soc_device spitz_snd_devdata = {
	.machine = &snd_soc_machine_spitz,
	.platform = &pxa2xx_soc_platform,
	.codec_dev = &soc_codec_dev_wm8750,
	.codec_data = &spitz_wm8750_setup,
};

static struct platform_device *spitz_snd_device;

static int __init spitz_init(void)
{
	int ret;

	if (!(machine_is_spitz() || machine_is_borzoi() || machine_is_akita()))
		return -ENODEV;

	spitz_snd_device = platform_device_alloc("soc-audio", -1);
	if (!spitz_snd_device)
		return -ENOMEM;

	platform_set_drvdata(spitz_snd_device, &spitz_snd_devdata);
	spitz_snd_devdata.dev = &spitz_snd_device->dev;
	ret = platform_device_add(spitz_snd_device);

	if (ret)
		platform_device_put(spitz_snd_device);

	return ret;
}

static void __exit spitz_exit(void)
{
	platform_device_unregister(spitz_snd_device);
}

module_init(spitz_init);
module_exit(spitz_exit);

MODULE_AUTHOR("Richard Purdie");
MODULE_DESCRIPTION("ALSA SoC Spitz");
MODULE_LICENSE("GPL");
