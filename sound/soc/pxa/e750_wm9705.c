/*
 * e750-wm9705.c  --  SoC audio for e750
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

#include <mach/audio.h>
#include <mach/eseries-gpio.h>

#include <asm/mach-types.h>

#include "../codecs/wm9705.h"
#include "pxa2xx-ac97.h"

static int e750_spk_amp_event(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *kcontrol, int event)
{
	if (event & SND_SOC_DAPM_PRE_PMU)
		gpio_set_value(GPIO_E750_SPK_AMP_OFF, 0);
	else if (event & SND_SOC_DAPM_POST_PMD)
		gpio_set_value(GPIO_E750_SPK_AMP_OFF, 1);

	return 0;
}

static int e750_hp_amp_event(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *kcontrol, int event)
{
	if (event & SND_SOC_DAPM_PRE_PMU)
		gpio_set_value(GPIO_E750_HP_AMP_OFF, 0);
	else if (event & SND_SOC_DAPM_POST_PMD)
		gpio_set_value(GPIO_E750_HP_AMP_OFF, 1);

	return 0;
}

static const struct snd_soc_dapm_widget e750_dapm_widgets[] = {
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_SPK("Speaker", NULL),
	SND_SOC_DAPM_MIC("Mic (Internal)", NULL),
	SND_SOC_DAPM_PGA_E("Headphone Amp", SND_SOC_NOPM, 0, 0, NULL, 0,
			e750_hp_amp_event, SND_SOC_DAPM_PRE_PMU |
			SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_PGA_E("Speaker Amp", SND_SOC_NOPM, 0, 0, NULL, 0,
			e750_spk_amp_event, SND_SOC_DAPM_PRE_PMU |
			SND_SOC_DAPM_POST_PMD),
};

static const struct snd_soc_dapm_route audio_map[] = {
	{"Headphone Amp", NULL, "HPOUTL"},
	{"Headphone Amp", NULL, "HPOUTR"},
	{"Headphone Jack", NULL, "Headphone Amp"},

	{"Speaker Amp", NULL, "MONOOUT"},
	{"Speaker", NULL, "Speaker Amp"},

	{"MIC1", NULL, "Mic (Internal)"},
};

static int e750_ac97_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_soc_dapm_context *dapm = &codec->dapm;

	snd_soc_dapm_nc_pin(dapm, "LOUT");
	snd_soc_dapm_nc_pin(dapm, "ROUT");
	snd_soc_dapm_nc_pin(dapm, "PHONE");
	snd_soc_dapm_nc_pin(dapm, "LINEINL");
	snd_soc_dapm_nc_pin(dapm, "LINEINR");
	snd_soc_dapm_nc_pin(dapm, "CDINL");
	snd_soc_dapm_nc_pin(dapm, "CDINR");
	snd_soc_dapm_nc_pin(dapm, "PCBEEP");
	snd_soc_dapm_nc_pin(dapm, "MIC2");

	snd_soc_dapm_new_controls(dapm, e750_dapm_widgets,
					ARRAY_SIZE(e750_dapm_widgets));

	snd_soc_dapm_add_routes(dapm, audio_map, ARRAY_SIZE(audio_map));

	snd_soc_dapm_sync(dapm);

	return 0;
}

static struct snd_soc_dai_link e750_dai[] = {
	{
		.name = "AC97",
		.stream_name = "AC97 HiFi",
		.cpu_dai_name = "pxa-ac97.0",
		.codec_dai_name = "wm9705-hifi",
		.platform_name = "pxa-pcm-audio",
		.codec_name = "wm9705-codec",
		.init = e750_ac97_init,
		/* use ops to check startup state */
	},
	{
		.name = "AC97 Aux",
		.stream_name = "AC97 Aux",
		.cpu_dai_name = "pxa-ac97.1",
		.codec_dai_name ="wm9705-aux",
		.platform_name = "pxa-pcm-audio",
		.codec_name = "wm9705-codec",
	},
};

static struct snd_soc_card e750 = {
	.name = "Toshiba e750",
	.dai_link = e750_dai,
	.num_links = ARRAY_SIZE(e750_dai),
};

static struct platform_device *e750_snd_device;

static int __init e750_init(void)
{
	int ret;

	if (!machine_is_e750())
		return -ENODEV;

	ret = gpio_request(GPIO_E750_HP_AMP_OFF,  "Headphone amp");
	if (ret)
		return ret;

	ret = gpio_request(GPIO_E750_SPK_AMP_OFF, "Speaker amp");
	if (ret)
		goto free_hp_amp_gpio;

	ret = gpio_direction_output(GPIO_E750_HP_AMP_OFF, 1);
	if (ret)
		goto free_spk_amp_gpio;

	ret = gpio_direction_output(GPIO_E750_SPK_AMP_OFF, 1);
	if (ret)
		goto free_spk_amp_gpio;

	e750_snd_device = platform_device_alloc("soc-audio", -1);
	if (!e750_snd_device) {
		ret = -ENOMEM;
		goto free_spk_amp_gpio;
	}

	platform_set_drvdata(e750_snd_device, &e750);
	ret = platform_device_add(e750_snd_device);

	if (!ret)
		return 0;

/* Fail gracefully */
	platform_device_put(e750_snd_device);
free_spk_amp_gpio:
	gpio_free(GPIO_E750_SPK_AMP_OFF);
free_hp_amp_gpio:
	gpio_free(GPIO_E750_HP_AMP_OFF);

	return ret;
}

static void __exit e750_exit(void)
{
	platform_device_unregister(e750_snd_device);
	gpio_free(GPIO_E750_SPK_AMP_OFF);
	gpio_free(GPIO_E750_HP_AMP_OFF);
}

module_init(e750_init);
module_exit(e750_exit);

/* Module information */
MODULE_AUTHOR("Ian Molton <spyro@f2s.com>");
MODULE_DESCRIPTION("ALSA SoC driver for e750");
MODULE_LICENSE("GPL v2");
