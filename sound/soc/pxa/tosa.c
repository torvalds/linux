/*
 * tosa.c  --  SoC audio for Tosa
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
 * GPIO's
 *  1 - Jack Insertion
 *  5 - Hookswitch (headset answer/hang up switch)
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/device.h>

#include <sound/driver.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>

#include <asm/mach-types.h>
#include <asm/hardware/tmio.h>
#include <asm/arch/pxa-regs.h>
#include <asm/arch/hardware.h>
#include <asm/arch/audio.h>
#include <asm/arch/tosa.h>

#include "../codecs/wm9712.h"
#include "pxa2xx-pcm.h"
#include "pxa2xx-ac97.h"

static struct snd_soc_machine tosa;

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
	int spk = 0, mic_int = 0, hp = 0, hs = 0;

	/* set up jack connection */
	switch (tosa_jack_func) {
	case TOSA_HP:
		hp = 1;
		break;
	case TOSA_MIC_INT:
		mic_int = 1;
		break;
	case TOSA_HEADSET:
		hs = 1;
		break;
	}

	if (tosa_spk_func == TOSA_SPK_ON)
		spk = 1;

	snd_soc_dapm_set_endpoint(codec, "Speaker", spk);
	snd_soc_dapm_set_endpoint(codec, "Mic (Internal)", mic_int);
	snd_soc_dapm_set_endpoint(codec, "Headphone Jack", hp);
	snd_soc_dapm_set_endpoint(codec, "Headset Jack", hs);
	snd_soc_dapm_sync_endpoints(codec);
}

static int tosa_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_codec *codec = rtd->socdev->codec;

	/* check the jack status at stream startup */
	tosa_ext_control(codec);
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
static int tosa_hp_event(struct snd_soc_dapm_widget *w, int event)
{
	if (SND_SOC_DAPM_EVENT_ON(event))
		set_tc6393_gpio(&tc6393_device.dev,TOSA_TC6393_L_MUTE);
	else
		reset_tc6393_gpio(&tc6393_device.dev,TOSA_TC6393_L_MUTE);
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
static const char *audio_map[][3] = {

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

	{NULL, NULL, NULL},
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

static int tosa_ac97_init(struct snd_soc_codec *codec)
{
	int i, err;

	snd_soc_dapm_set_endpoint(codec, "OUT3", 0);
	snd_soc_dapm_set_endpoint(codec, "MONOOUT", 0);

	/* add tosa specific controls */
	for (i = 0; i < ARRAY_SIZE(tosa_controls); i++) {
		err = snd_ctl_add(codec->card,
				snd_soc_cnew(&tosa_controls[i],codec, NULL));
		if (err < 0)
			return err;
	}

	/* add tosa specific widgets */
	for (i = 0; i < ARRAY_SIZE(tosa_dapm_widgets); i++) {
		snd_soc_dapm_new_control(codec, &tosa_dapm_widgets[i]);
	}

	/* set up tosa specific audio path audio_map */
	for (i = 0; audio_map[i][0] != NULL; i++) {
		snd_soc_dapm_connect_input(codec, audio_map[i][0],
			audio_map[i][1], audio_map[i][2]);
	}

	snd_soc_dapm_sync_endpoints(codec);
	return 0;
}

static struct snd_soc_dai_link tosa_dai[] = {
{
	.name = "AC97",
	.stream_name = "AC97 HiFi",
	.cpu_dai = &pxa_ac97_dai[PXA2XX_DAI_AC97_HIFI],
	.codec_dai = &wm9712_dai[WM9712_DAI_AC97_HIFI],
	.init = tosa_ac97_init,
	.ops = &tosa_ops,
},
{
	.name = "AC97 Aux",
	.stream_name = "AC97 Aux",
	.cpu_dai = &pxa_ac97_dai[PXA2XX_DAI_AC97_AUX],
	.codec_dai = &wm9712_dai[WM9712_DAI_AC97_AUX],
	.ops = &tosa_ops,
},
};

static struct snd_soc_machine tosa = {
	.name = "Tosa",
	.dai_link = tosa_dai,
	.num_links = ARRAY_SIZE(tosa_dai),
};

static struct snd_soc_device tosa_snd_devdata = {
	.machine = &tosa,
	.platform = &pxa2xx_soc_platform,
	.codec_dev = &soc_codec_dev_wm9712,
};

static struct platform_device *tosa_snd_device;

static int __init tosa_init(void)
{
	int ret;

	if (!machine_is_tosa())
		return -ENODEV;

	tosa_snd_device = platform_device_alloc("soc-audio", -1);
	if (!tosa_snd_device)
		return -ENOMEM;

	platform_set_drvdata(tosa_snd_device, &tosa_snd_devdata);
	tosa_snd_devdata.dev = &tosa_snd_device->dev;
	ret = platform_device_add(tosa_snd_device);

	if (ret)
		platform_device_put(tosa_snd_device);

	return ret;
}

static void __exit tosa_exit(void)
{
	platform_device_unregister(tosa_snd_device);
}

module_init(tosa_init);
module_exit(tosa_exit);

/* Module information */
MODULE_AUTHOR("Richard Purdie");
MODULE_DESCRIPTION("ALSA SoC Tosa");
MODULE_LICENSE("GPL");
