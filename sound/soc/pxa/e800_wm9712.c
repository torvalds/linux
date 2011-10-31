/*
 * e800-wm9712.c  --  SoC audio for e800
 *
 * Copyright 2007 (c) Ian Molton <spyro@f2s.com>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation; version 2 ONLY.
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/gpio.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>

#include <asm/mach-types.h>
#include <mach/audio.h>
#include <mach/eseries-gpio.h>

#include "../codecs/wm9712.h"
#include "pxa2xx-ac97.h"

static int e800_spk_amp_event(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *kcontrol, int event)
{
	if (event & SND_SOC_DAPM_PRE_PMU)
		gpio_set_value(GPIO_E800_SPK_AMP_ON, 1);
	else if (event & SND_SOC_DAPM_POST_PMD)
		gpio_set_value(GPIO_E800_SPK_AMP_ON, 0);

	return 0;
}

static int e800_hp_amp_event(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *kcontrol, int event)
{
	if (event & SND_SOC_DAPM_PRE_PMU)
		gpio_set_value(GPIO_E800_HP_AMP_OFF, 0);
	else if (event & SND_SOC_DAPM_POST_PMD)
		gpio_set_value(GPIO_E800_HP_AMP_OFF, 1);

	return 0;
}

static const struct snd_soc_dapm_widget e800_dapm_widgets[] = {
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_MIC("Mic (Internal1)", NULL),
	SND_SOC_DAPM_MIC("Mic (Internal2)", NULL),
	SND_SOC_DAPM_SPK("Speaker", NULL),
	SND_SOC_DAPM_PGA_E("Headphone Amp", SND_SOC_NOPM, 0, 0, NULL, 0,
			e800_hp_amp_event, SND_SOC_DAPM_PRE_PMU |
			SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_PGA_E("Speaker Amp", SND_SOC_NOPM, 0, 0, NULL, 0,
			e800_spk_amp_event, SND_SOC_DAPM_PRE_PMU |
			SND_SOC_DAPM_POST_PMD),
};

static const struct snd_soc_dapm_route audio_map[] = {
	{"Headphone Jack", NULL, "HPOUTL"},
	{"Headphone Jack", NULL, "HPOUTR"},
	{"Headphone Jack", NULL, "Headphone Amp"},

	{"Speaker Amp", NULL, "MONOOUT"},
	{"Speaker", NULL, "Speaker Amp"},

	{"MIC1", NULL, "Mic (Internal1)"},
	{"MIC2", NULL, "Mic (Internal2)"},
};

static int e800_ac97_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_soc_dapm_context *dapm = &codec->dapm;

	snd_soc_dapm_new_controls(dapm, e800_dapm_widgets,
					ARRAY_SIZE(e800_dapm_widgets));

	snd_soc_dapm_add_routes(dapm, audio_map, ARRAY_SIZE(audio_map));

	return 0;
}

static struct snd_soc_dai_link e800_dai[] = {
	{
		.name = "AC97",
		.stream_name = "AC97 HiFi",
		.cpu_dai_name = "pxa2xx-ac97",
		.codec_dai_name = "wm9712-hifi",
		.platform_name = "pxa-pcm-audio",
		.codec_name = "wm9712-codec",
		.init = e800_ac97_init,
	},
	{
		.name = "AC97 Aux",
		.stream_name = "AC97 Aux",
		.cpu_dai_name = "pxa2xx-ac97-aux",
		.codec_dai_name ="wm9712-aux",
		.platform_name = "pxa-pcm-audio",
		.codec_name = "wm9712-codec",
	},
};

static struct snd_soc_card e800 = {
	.name = "Toshiba e800",
	.dai_link = e800_dai,
	.num_links = ARRAY_SIZE(e800_dai),
};

static struct platform_device *e800_snd_device;

static int __init e800_init(void)
{
	int ret;

	if (!machine_is_e800())
		return -ENODEV;

	ret = gpio_request(GPIO_E800_HP_AMP_OFF,  "Headphone amp");
	if (ret)
		return ret;

	ret = gpio_request(GPIO_E800_SPK_AMP_ON, "Speaker amp");
	if (ret)
		goto free_hp_amp_gpio;

	ret = gpio_direction_output(GPIO_E800_HP_AMP_OFF, 1);
	if (ret)
		goto free_spk_amp_gpio;

	ret = gpio_direction_output(GPIO_E800_SPK_AMP_ON, 1);
	if (ret)
		goto free_spk_amp_gpio;

	e800_snd_device = platform_device_alloc("soc-audio", -1);
	if (!e800_snd_device)
		return -ENOMEM;

	platform_set_drvdata(e800_snd_device, &e800);
	ret = platform_device_add(e800_snd_device);

	if (!ret)
		return 0;

/* Fail gracefully */
	platform_device_put(e800_snd_device);
free_spk_amp_gpio:
	gpio_free(GPIO_E800_SPK_AMP_ON);
free_hp_amp_gpio:
	gpio_free(GPIO_E800_HP_AMP_OFF);

	return ret;
}

static void __exit e800_exit(void)
{
	platform_device_unregister(e800_snd_device);
	gpio_free(GPIO_E800_SPK_AMP_ON);
	gpio_free(GPIO_E800_HP_AMP_OFF);
}

module_init(e800_init);
module_exit(e800_exit);

/* Module information */
MODULE_AUTHOR("Ian Molton <spyro@f2s.com>");
MODULE_DESCRIPTION("ALSA SoC driver for e800");
MODULE_LICENSE("GPL v2");
