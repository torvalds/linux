/*
 * rx1950.c  --  ALSA Soc Audio Layer
 *
 * Copyright (c) 2010 Vasily Khoruzhick <anarsoul@gmail.com>
 *
 * Based on smdk2440.c and magician.c
 *
 * Authors: Graeme Gregory graeme.gregory@wolfsonmicro.com
 *          Philipp Zabel <philipp.zabel@gmail.com>
 *          Denis Grigoriev <dgreenday@gmail.com>
 *          Vasily Khoruzhick <anarsoul@gmail.com>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <linux/types.h>
#include <linux/gpio.h>
#include <linux/module.h>

#include <sound/soc.h>
#include <sound/jack.h>

#include <mach/gpio-samsung.h>
#include "regs-iis.h"
#include <asm/mach-types.h>

#include "s3c24xx-i2s.h"

static int rx1950_uda1380_init(struct snd_soc_pcm_runtime *rtd);
static int rx1950_uda1380_card_remove(struct snd_soc_card *card);
static int rx1950_startup(struct snd_pcm_substream *substream);
static int rx1950_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params);
static int rx1950_spk_power(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *kcontrol, int event);

static unsigned int rates[] = {
	16000,
	44100,
	48000,
};

static struct snd_pcm_hw_constraint_list hw_rates = {
	.count = ARRAY_SIZE(rates),
	.list = rates,
	.mask = 0,
};

static struct snd_soc_jack hp_jack;

static struct snd_soc_jack_pin hp_jack_pins[] = {
	{
		.pin	= "Headphone Jack",
		.mask	= SND_JACK_HEADPHONE,
	},
	{
		.pin	= "Speaker",
		.mask	= SND_JACK_HEADPHONE,
		.invert	= 1,
	},
};

static struct snd_soc_jack_gpio hp_jack_gpios[] = {
	[0] = {
		.gpio			= S3C2410_GPG(12),
		.name			= "hp-gpio",
		.report			= SND_JACK_HEADPHONE,
		.invert			= 1,
		.debounce_time		= 200,
	},
};

static struct snd_soc_ops rx1950_ops = {
	.startup	= rx1950_startup,
	.hw_params	= rx1950_hw_params,
};

/* s3c24xx digital audio interface glue - connects codec <--> CPU */
static struct snd_soc_dai_link rx1950_uda1380_dai[] = {
	{
		.name		= "uda1380",
		.stream_name	= "UDA1380 Duplex",
		.cpu_dai_name	= "s3c24xx-iis",
		.codec_dai_name	= "uda1380-hifi",
		.init		= rx1950_uda1380_init,
		.platform_name	= "s3c24xx-iis",
		.codec_name	= "uda1380-codec.0-001a",
		.ops		= &rx1950_ops,
	},
};

/* rx1950 machine dapm widgets */
static const struct snd_soc_dapm_widget uda1380_dapm_widgets[] = {
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_MIC("Mic Jack", NULL),
	SND_SOC_DAPM_SPK("Speaker", rx1950_spk_power),
};

/* rx1950 machine audio_map */
static const struct snd_soc_dapm_route audio_map[] = {
	/* headphone connected to VOUTLHP, VOUTRHP */
	{"Headphone Jack", NULL, "VOUTLHP"},
	{"Headphone Jack", NULL, "VOUTRHP"},

	/* ext speaker connected to VOUTL, VOUTR  */
	{"Speaker", NULL, "VOUTL"},
	{"Speaker", NULL, "VOUTR"},

	/* mic is connected to VINM */
	{"VINM", NULL, "Mic Jack"},
};

static struct snd_soc_card rx1950_asoc = {
	.name = "rx1950",
	.owner = THIS_MODULE,
	.remove = rx1950_uda1380_card_remove,
	.dai_link = rx1950_uda1380_dai,
	.num_links = ARRAY_SIZE(rx1950_uda1380_dai),

	.dapm_widgets = uda1380_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(uda1380_dapm_widgets),
	.dapm_routes = audio_map,
	.num_dapm_routes = ARRAY_SIZE(audio_map),
};

static struct platform_device *s3c24xx_snd_device;

static int rx1950_startup(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;

	return snd_pcm_hw_constraint_list(runtime, 0,
					SNDRV_PCM_HW_PARAM_RATE,
					&hw_rates);
}

static int rx1950_spk_power(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *kcontrol, int event)
{
	if (SND_SOC_DAPM_EVENT_ON(event))
		gpio_set_value(S3C2410_GPA(1), 1);
	else
		gpio_set_value(S3C2410_GPA(1), 0);

	return 0;
}

static int rx1950_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	int div;
	int ret;
	unsigned int rate = params_rate(params);
	int clk_source, fs_mode;

	switch (rate) {
	case 16000:
	case 48000:
		clk_source = S3C24XX_CLKSRC_PCLK;
		fs_mode = S3C2410_IISMOD_256FS;
		div = s3c24xx_i2s_get_clockrate() / (256 * rate);
		if (s3c24xx_i2s_get_clockrate() % (256 * rate) > (128 * rate))
			div++;
		break;
	case 44100:
	case 88200:
		clk_source = S3C24XX_CLKSRC_MPLL;
		fs_mode = S3C2410_IISMOD_384FS;
		div = 1;
		break;
	default:
		printk(KERN_ERR "%s: rate %d is not supported\n",
			__func__, rate);
		return -EINVAL;
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

	/* select clock source */
	ret = snd_soc_dai_set_sysclk(cpu_dai, clk_source, rate,
			SND_SOC_CLOCK_OUT);
	if (ret < 0)
		return ret;

	/* set MCLK division for sample rate */
	ret = snd_soc_dai_set_clkdiv(cpu_dai, S3C24XX_DIV_MCLK,
		fs_mode);
	if (ret < 0)
		return ret;

	/* set BCLK division for sample rate */
	ret = snd_soc_dai_set_clkdiv(cpu_dai, S3C24XX_DIV_BCLK,
		S3C2410_IISMOD_32FS);
	if (ret < 0)
		return ret;

	/* set prescaler division for sample rate */
	ret = snd_soc_dai_set_clkdiv(cpu_dai, S3C24XX_DIV_PRESCALER,
		S3C24XX_PRESCALE(div, div));
	if (ret < 0)
		return ret;

	return 0;
}

static int rx1950_uda1380_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_codec *codec = rtd->codec;

	snd_soc_jack_new(codec, "Headphone Jack", SND_JACK_HEADPHONE,
		&hp_jack);

	snd_soc_jack_add_pins(&hp_jack, ARRAY_SIZE(hp_jack_pins),
		hp_jack_pins);

	snd_soc_jack_add_gpios(&hp_jack, ARRAY_SIZE(hp_jack_gpios),
		hp_jack_gpios);

	return 0;
}

static int rx1950_uda1380_card_remove(struct snd_soc_card *card)
{
	snd_soc_jack_free_gpios(&hp_jack, ARRAY_SIZE(hp_jack_gpios),
		hp_jack_gpios);

	return 0;
}

static int __init rx1950_init(void)
{
	int ret;

	if (!machine_is_rx1950())
		return -ENODEV;

	/* configure some gpios */
	ret = gpio_request(S3C2410_GPA(1), "speaker-power");
	if (ret)
		goto err_gpio;

	ret = gpio_direction_output(S3C2410_GPA(1), 0);
	if (ret)
		goto err_gpio_conf;

	s3c24xx_snd_device = platform_device_alloc("soc-audio", -1);
	if (!s3c24xx_snd_device) {
		ret = -ENOMEM;
		goto err_plat_alloc;
	}

	platform_set_drvdata(s3c24xx_snd_device, &rx1950_asoc);
	ret = platform_device_add(s3c24xx_snd_device);

	if (ret) {
		platform_device_put(s3c24xx_snd_device);
		goto err_plat_add;
	}

	return 0;

err_plat_add:
err_plat_alloc:
err_gpio_conf:
	gpio_free(S3C2410_GPA(1));

err_gpio:
	return ret;
}

static void __exit rx1950_exit(void)
{
	platform_device_unregister(s3c24xx_snd_device);
	gpio_free(S3C2410_GPA(1));
}

module_init(rx1950_init);
module_exit(rx1950_exit);

/* Module information */
MODULE_AUTHOR("Vasily Khoruzhick");
MODULE_DESCRIPTION("ALSA SoC RX1950");
MODULE_LICENSE("GPL");
