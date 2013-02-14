/* sound/soc/samsung/smartq_wm8987.c
 *
 * Copyright 2010 Maurus Cuelenaere <mcuelenaere@gmail.com>
 *
 * Based on smdk6410_wm8987.c
 *     Copyright 2007 Wolfson Microelectronics PLC. - linux@wolfsonmicro.com
 *     Graeme Gregory - graeme.gregory@wolfsonmicro.com
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <linux/gpio.h>
#include <linux/module.h>

#include <sound/soc.h>
#include <sound/jack.h>

#include <asm/mach-types.h>

#include "i2s.h"
#include "../codecs/wm8750.h"

/*
 * WM8987 is register compatible with WM8750, so using that as base driver.
 */

static struct snd_soc_card snd_soc_smartq;

static int smartq_hifi_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	unsigned int clk = 0;
	int ret;

	switch (params_rate(params)) {
	case 8000:
	case 16000:
	case 32000:
	case 48000:
	case 96000:
		clk = 12288000;
		break;
	case 11025:
	case 22050:
	case 44100:
	case 88200:
		clk = 11289600;
		break;
	}

	/* set codec DAI configuration */
	ret = snd_soc_dai_set_fmt(codec_dai, SND_SOC_DAIFMT_I2S |
					     SND_SOC_DAIFMT_NB_NF |
					     SND_SOC_DAIFMT_CBS_CFS);
	if (ret < 0)
		return ret;

	/* set cpu DAI configuration */
	ret = snd_soc_dai_set_fmt(cpu_dai, SND_SOC_DAIFMT_I2S |
					   SND_SOC_DAIFMT_NB_NF |
					   SND_SOC_DAIFMT_CBS_CFS);
	if (ret < 0)
		return ret;

	/* Use PCLK for I2S signal generation */
	ret = snd_soc_dai_set_sysclk(cpu_dai, SAMSUNG_I2S_RCLKSRC_0,
					0, SND_SOC_CLOCK_IN);
	if (ret < 0)
		return ret;

	/* Gate the RCLK output on PAD */
	ret = snd_soc_dai_set_sysclk(cpu_dai, SAMSUNG_I2S_CDCLK,
					0, SND_SOC_CLOCK_IN);
	if (ret < 0)
		return ret;

	/* set the codec system clock for DAC and ADC */
	ret = snd_soc_dai_set_sysclk(codec_dai, WM8750_SYSCLK, clk,
				     SND_SOC_CLOCK_IN);
	if (ret < 0)
		return ret;

	return 0;
}

/*
 * SmartQ WM8987 HiFi DAI operations.
 */
static struct snd_soc_ops smartq_hifi_ops = {
	.hw_params = smartq_hifi_hw_params,
};

static struct snd_soc_jack smartq_jack;

static struct snd_soc_jack_pin smartq_jack_pins[] = {
	/* Disable speaker when headphone is plugged in */
	{
		.pin	= "Internal Speaker",
		.mask	= SND_JACK_HEADPHONE,
	},
};

static struct snd_soc_jack_gpio smartq_jack_gpios[] = {
	{
		.gpio		= S3C64XX_GPL(12),
		.name		= "headphone detect",
		.report		= SND_JACK_HEADPHONE,
		.debounce_time	= 200,
	},
};

static const struct snd_kcontrol_new wm8987_smartq_controls[] = {
	SOC_DAPM_PIN_SWITCH("Internal Speaker"),
	SOC_DAPM_PIN_SWITCH("Headphone Jack"),
	SOC_DAPM_PIN_SWITCH("Internal Mic"),
};

static int smartq_speaker_event(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *k,
				int event)
{
	gpio_set_value(S3C64XX_GPK(12), SND_SOC_DAPM_EVENT_OFF(event));

	return 0;
}

static const struct snd_soc_dapm_widget wm8987_dapm_widgets[] = {
	SND_SOC_DAPM_SPK("Internal Speaker", smartq_speaker_event),
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_MIC("Internal Mic", NULL),
};

static const struct snd_soc_dapm_route audio_map[] = {
	{"Headphone Jack", NULL, "LOUT2"},
	{"Headphone Jack", NULL, "ROUT2"},

	{"Internal Speaker", NULL, "LOUT2"},
	{"Internal Speaker", NULL, "ROUT2"},

	{"Mic Bias", NULL, "Internal Mic"},
	{"LINPUT2", NULL, "Mic Bias"},
};

static int smartq_wm8987_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_soc_dapm_context *dapm = &codec->dapm;
	int err = 0;

	/* set endpoints to not connected */
	snd_soc_dapm_nc_pin(dapm, "LINPUT1");
	snd_soc_dapm_nc_pin(dapm, "RINPUT1");
	snd_soc_dapm_nc_pin(dapm, "OUT3");
	snd_soc_dapm_nc_pin(dapm, "ROUT1");

	/* set endpoints to default off mode */
	snd_soc_dapm_enable_pin(dapm, "Internal Speaker");
	snd_soc_dapm_enable_pin(dapm, "Internal Mic");
	snd_soc_dapm_disable_pin(dapm, "Headphone Jack");

	/* Headphone jack detection */
	err = snd_soc_jack_new(codec, "Headphone Jack",
			       SND_JACK_HEADPHONE, &smartq_jack);
	if (err)
		return err;

	err = snd_soc_jack_add_pins(&smartq_jack, ARRAY_SIZE(smartq_jack_pins),
				    smartq_jack_pins);
	if (err)
		return err;

	err = snd_soc_jack_add_gpios(&smartq_jack,
				     ARRAY_SIZE(smartq_jack_gpios),
				     smartq_jack_gpios);

	return err;
}

static struct snd_soc_dai_link smartq_dai[] = {
	{
		.name		= "wm8987",
		.stream_name	= "SmartQ Hi-Fi",
		.cpu_dai_name	= "samsung-i2s.0",
		.codec_dai_name	= "wm8750-hifi",
		.platform_name	= "samsung-i2s.0",
		.codec_name	= "wm8750.0-0x1a",
		.init		= smartq_wm8987_init,
		.ops		= &smartq_hifi_ops,
	},
};

static struct snd_soc_card snd_soc_smartq = {
	.name = "SmartQ",
	.owner = THIS_MODULE,
	.dai_link = smartq_dai,
	.num_links = ARRAY_SIZE(smartq_dai),

	.dapm_widgets = wm8987_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(wm8987_dapm_widgets),
	.dapm_routes = audio_map,
	.num_dapm_routes = ARRAY_SIZE(audio_map),
	.controls = wm8987_smartq_controls,
	.num_controls = ARRAY_SIZE(wm8987_smartq_controls),
};

static struct platform_device *smartq_snd_device;

static int __init smartq_init(void)
{
	int ret;

	if (!machine_is_smartq7() && !machine_is_smartq5()) {
		pr_info("Only SmartQ is supported by this ASoC driver\n");
		return -ENODEV;
	}

	smartq_snd_device = platform_device_alloc("soc-audio", -1);
	if (!smartq_snd_device)
		return -ENOMEM;

	platform_set_drvdata(smartq_snd_device, &snd_soc_smartq);

	ret = platform_device_add(smartq_snd_device);
	if (ret) {
		platform_device_put(smartq_snd_device);
		return ret;
	}

	/* Initialise GPIOs used by amplifiers */
	ret = gpio_request(S3C64XX_GPK(12), "amplifiers shutdown");
	if (ret) {
		dev_err(&smartq_snd_device->dev, "Failed to register GPK12\n");
		goto err_unregister_device;
	}

	/* Disable amplifiers */
	ret = gpio_direction_output(S3C64XX_GPK(12), 1);
	if (ret) {
		dev_err(&smartq_snd_device->dev, "Failed to configure GPK12\n");
		goto err_free_gpio_amp_shut;
	}

	return 0;

err_free_gpio_amp_shut:
	gpio_free(S3C64XX_GPK(12));
err_unregister_device:
	platform_device_unregister(smartq_snd_device);

	return ret;
}

static void __exit smartq_exit(void)
{
	gpio_free(S3C64XX_GPK(12));
	snd_soc_jack_free_gpios(&smartq_jack, ARRAY_SIZE(smartq_jack_gpios),
				smartq_jack_gpios);

	platform_device_unregister(smartq_snd_device);
}

module_init(smartq_init);
module_exit(smartq_exit);

/* Module information */
MODULE_AUTHOR("Maurus Cuelenaere <mcuelenaere@gmail.com>");
MODULE_DESCRIPTION("ALSA SoC SmartQ WM8987");
MODULE_LICENSE("GPL");
