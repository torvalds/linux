/*
 * e740-wm9705.c  --  SoC audio for e740
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
#include <sound/soc-dapm.h>

#include <mach/audio.h>
#include <mach/eseries-gpio.h>

#include <asm/mach-types.h>

#include "../codecs/wm9705.h"
#include "pxa2xx-pcm.h"
#include "pxa2xx-ac97.h"


#define E740_AUDIO_OUT 1
#define E740_AUDIO_IN  2

static int e740_audio_power;

static void e740_sync_audio_power(int status)
{
	gpio_set_value(GPIO_E740_WM9705_nAVDD2, !status);
	gpio_set_value(GPIO_E740_AMP_ON, (status & E740_AUDIO_OUT) ? 1 : 0);
	gpio_set_value(GPIO_E740_MIC_ON, (status & E740_AUDIO_IN) ? 1 : 0);
}

static int e740_mic_amp_event(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *kcontrol, int event)
{
	if (event & SND_SOC_DAPM_PRE_PMU)
		e740_audio_power |= E740_AUDIO_IN;
	else if (event & SND_SOC_DAPM_POST_PMD)
		e740_audio_power &= ~E740_AUDIO_IN;

	e740_sync_audio_power(e740_audio_power);

	return 0;
}

static int e740_output_amp_event(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *kcontrol, int event)
{
	if (event & SND_SOC_DAPM_PRE_PMU)
		e740_audio_power |= E740_AUDIO_OUT;
	else if (event & SND_SOC_DAPM_POST_PMD)
		e740_audio_power &= ~E740_AUDIO_OUT;

	e740_sync_audio_power(e740_audio_power);

	return 0;
}

static const struct snd_soc_dapm_widget e740_dapm_widgets[] = {
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_SPK("Speaker", NULL),
	SND_SOC_DAPM_MIC("Mic (Internal)", NULL),
	SND_SOC_DAPM_PGA_E("Output Amp", SND_SOC_NOPM, 0, 0, NULL, 0,
			e740_output_amp_event, SND_SOC_DAPM_PRE_PMU |
			SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_PGA_E("Mic Amp", SND_SOC_NOPM, 0, 0, NULL, 0,
			e740_mic_amp_event, SND_SOC_DAPM_PRE_PMU |
			SND_SOC_DAPM_POST_PMD),
};

static const struct snd_soc_dapm_route audio_map[] = {
	{"Output Amp", NULL, "LOUT"},
	{"Output Amp", NULL, "ROUT"},
	{"Output Amp", NULL, "MONOOUT"},

	{"Speaker", NULL, "Output Amp"},
	{"Headphone Jack", NULL, "Output Amp"},

	{"MIC1", NULL, "Mic Amp"},
	{"Mic Amp", NULL, "Mic (Internal)"},
};

static int e740_ac97_init(struct snd_soc_codec *codec)
{
	snd_soc_dapm_nc_pin(codec, "HPOUTL");
	snd_soc_dapm_nc_pin(codec, "HPOUTR");
	snd_soc_dapm_nc_pin(codec, "PHONE");
	snd_soc_dapm_nc_pin(codec, "LINEINL");
	snd_soc_dapm_nc_pin(codec, "LINEINR");
	snd_soc_dapm_nc_pin(codec, "CDINL");
	snd_soc_dapm_nc_pin(codec, "CDINR");
	snd_soc_dapm_nc_pin(codec, "PCBEEP");
	snd_soc_dapm_nc_pin(codec, "MIC2");

	snd_soc_dapm_new_controls(codec, e740_dapm_widgets,
					ARRAY_SIZE(e740_dapm_widgets));

	snd_soc_dapm_add_routes(codec, audio_map, ARRAY_SIZE(audio_map));

	snd_soc_dapm_sync(codec);

	return 0;
}

static struct snd_soc_dai_link e740_dai[] = {
	{
		.name = "AC97",
		.stream_name = "AC97 HiFi",
		.cpu_dai = &pxa_ac97_dai[PXA2XX_DAI_AC97_HIFI],
		.codec_dai = &wm9705_dai[WM9705_DAI_AC97_HIFI],
		.init = e740_ac97_init,
	},
	{
		.name = "AC97 Aux",
		.stream_name = "AC97 Aux",
		.cpu_dai = &pxa_ac97_dai[PXA2XX_DAI_AC97_AUX],
		.codec_dai = &wm9705_dai[WM9705_DAI_AC97_AUX],
	},
};

static struct snd_soc_card e740 = {
	.name = "Toshiba e740",
	.platform = &pxa2xx_soc_platform,
	.dai_link = e740_dai,
	.num_links = ARRAY_SIZE(e740_dai),
};

static struct snd_soc_device e740_snd_devdata = {
	.card = &e740,
	.codec_dev = &soc_codec_dev_wm9705,
};

static struct platform_device *e740_snd_device;

static int __init e740_init(void)
{
	int ret;

	if (!machine_is_e740())
		return -ENODEV;

	ret = gpio_request(GPIO_E740_MIC_ON,  "Mic amp");
	if (ret)
		return ret;

	ret = gpio_request(GPIO_E740_AMP_ON, "Output amp");
	if (ret)
		goto free_mic_amp_gpio;

	ret = gpio_request(GPIO_E740_WM9705_nAVDD2, "Audio power");
	if (ret)
		goto free_op_amp_gpio;

	/* Disable audio */
	ret = gpio_direction_output(GPIO_E740_MIC_ON, 0);
	if (ret)
		goto free_apwr_gpio;
	ret = gpio_direction_output(GPIO_E740_AMP_ON, 0);
	if (ret)
		goto free_apwr_gpio;
	ret = gpio_direction_output(GPIO_E740_WM9705_nAVDD2, 1);
	if (ret)
		goto free_apwr_gpio;

	e740_snd_device = platform_device_alloc("soc-audio", -1);
	if (!e740_snd_device) {
		ret = -ENOMEM;
		goto free_apwr_gpio;
	}

	platform_set_drvdata(e740_snd_device, &e740_snd_devdata);
	e740_snd_devdata.dev = &e740_snd_device->dev;
	ret = platform_device_add(e740_snd_device);

	if (!ret)
		return 0;

/* Fail gracefully */
	platform_device_put(e740_snd_device);
free_apwr_gpio:
	gpio_free(GPIO_E740_WM9705_nAVDD2);
free_op_amp_gpio:
	gpio_free(GPIO_E740_AMP_ON);
free_mic_amp_gpio:
	gpio_free(GPIO_E740_MIC_ON);

	return ret;
}

static void __exit e740_exit(void)
{
	platform_device_unregister(e740_snd_device);
}

module_init(e740_init);
module_exit(e740_exit);

/* Module information */
MODULE_AUTHOR("Ian Molton <spyro@f2s.com>");
MODULE_DESCRIPTION("ALSA SoC driver for e740");
MODULE_LICENSE("GPL v2");
