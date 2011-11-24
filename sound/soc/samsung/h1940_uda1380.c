/*
 * h1940-uda1380.c  --  ALSA Soc Audio Layer
 *
 * Copyright (c) 2010 Arnaud Patard <arnaud.patard@rtp-net.org>
 * Copyright (c) 2010 Vasily Khoruzhick <anarsoul@gmail.com>
 *
 * Based on version from Arnaud Patard <arnaud.patard@rtp-net.org>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

#include <linux/types.h>
#include <linux/gpio.h>

#include <sound/soc.h>
#include <sound/jack.h>

#include <plat/regs-iis.h>
#include <mach/h1940-latch.h>
#include <asm/mach-types.h>

#include "s3c24xx-i2s.h"

static unsigned int rates[] = {
	11025,
	22050,
	44100,
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
	{
		.gpio			= S3C2410_GPG(4),
		.name			= "hp-gpio",
		.report			= SND_JACK_HEADPHONE,
		.invert			= 1,
		.debounce_time		= 200,
	},
};

static int h1940_startup(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;

	runtime->hw.rate_min = hw_rates.list[0];
	runtime->hw.rate_max = hw_rates.list[hw_rates.count - 1];
	runtime->hw.rates = SNDRV_PCM_RATE_KNOT;

	return snd_pcm_hw_constraint_list(runtime, 0,
					SNDRV_PCM_HW_PARAM_RATE,
					&hw_rates);
}

static int h1940_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	int div;
	int ret;
	unsigned int rate = params_rate(params);

	switch (rate) {
	case 11025:
	case 22050:
	case 44100:
		div = s3c24xx_i2s_get_clockrate() / (384 * rate);
		if (s3c24xx_i2s_get_clockrate() % (384 * rate) > (192 * rate))
			div++;
		break;
	default:
		dev_err(&rtd->dev, "%s: rate %d is not supported\n",
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
	ret = snd_soc_dai_set_sysclk(cpu_dai, S3C24XX_CLKSRC_PCLK, rate,
			SND_SOC_CLOCK_OUT);
	if (ret < 0)
		return ret;

	/* set MCLK division for sample rate */
	ret = snd_soc_dai_set_clkdiv(cpu_dai, S3C24XX_DIV_MCLK,
		S3C2410_IISMOD_384FS);
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

static struct snd_soc_ops h1940_ops = {
	.startup	= h1940_startup,
	.hw_params	= h1940_hw_params,
};

static int h1940_spk_power(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *kcontrol, int event)
{
	if (SND_SOC_DAPM_EVENT_ON(event))
		gpio_set_value(H1940_LATCH_AUDIO_POWER, 1);
	else
		gpio_set_value(H1940_LATCH_AUDIO_POWER, 0);

	return 0;
}

/* h1940 machine dapm widgets */
static const struct snd_soc_dapm_widget uda1380_dapm_widgets[] = {
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_MIC("Mic Jack", NULL),
	SND_SOC_DAPM_SPK("Speaker", h1940_spk_power),
};

/* h1940 machine audio_map */
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

static struct platform_device *s3c24xx_snd_device;

static int h1940_uda1380_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_soc_dapm_context *dapm = &codec->dapm;
	int err;

	/* Add h1940 specific widgets */
	err = snd_soc_dapm_new_controls(dapm, uda1380_dapm_widgets,
				  ARRAY_SIZE(uda1380_dapm_widgets));
	if (err)
		return err;

	/* Set up h1940 specific audio path audio_mapnects */
	err = snd_soc_dapm_add_routes(dapm, audio_map,
				      ARRAY_SIZE(audio_map));
	if (err)
		return err;

	snd_soc_dapm_enable_pin(dapm, "Headphone Jack");
	snd_soc_dapm_enable_pin(dapm, "Speaker");
	snd_soc_dapm_enable_pin(dapm, "Mic Jack");

	snd_soc_dapm_sync(dapm);

	snd_soc_jack_new(codec, "Headphone Jack", SND_JACK_HEADPHONE,
		&hp_jack);

	snd_soc_jack_add_pins(&hp_jack, ARRAY_SIZE(hp_jack_pins),
		hp_jack_pins);

	snd_soc_jack_add_gpios(&hp_jack, ARRAY_SIZE(hp_jack_gpios),
		hp_jack_gpios);

	return 0;
}

/* s3c24xx digital audio interface glue - connects codec <--> CPU */
static struct snd_soc_dai_link h1940_uda1380_dai[] = {
	{
		.name		= "uda1380",
		.stream_name	= "UDA1380 Duplex",
		.cpu_dai_name	= "s3c24xx-iis",
		.codec_dai_name	= "uda1380-hifi",
		.init		= h1940_uda1380_init,
		.platform_name	= "samsung-audio",
		.codec_name	= "uda1380-codec.0-001a",
		.ops		= &h1940_ops,
	},
};

static struct snd_soc_card h1940_asoc = {
	.name = "h1940",
	.dai_link = h1940_uda1380_dai,
	.num_links = ARRAY_SIZE(h1940_uda1380_dai),
};

static int __init h1940_init(void)
{
	int ret;

	if (!machine_is_h1940())
		return -ENODEV;

	/* configure some gpios */
	ret = gpio_request(H1940_LATCH_AUDIO_POWER, "speaker-power");
	if (ret)
		goto err_out;

	ret = gpio_direction_output(H1940_LATCH_AUDIO_POWER, 0);
	if (ret)
		goto err_gpio;

	s3c24xx_snd_device = platform_device_alloc("soc-audio", -1);
	if (!s3c24xx_snd_device) {
		ret = -ENOMEM;
		goto err_gpio;
	}

	platform_set_drvdata(s3c24xx_snd_device, &h1940_asoc);
	ret = platform_device_add(s3c24xx_snd_device);

	if (ret)
		goto err_plat;

	return 0;

err_plat:
	platform_device_put(s3c24xx_snd_device);
err_gpio:
	gpio_free(H1940_LATCH_AUDIO_POWER);

err_out:
	return ret;
}

static void __exit h1940_exit(void)
{
	platform_device_unregister(s3c24xx_snd_device);
	snd_soc_jack_free_gpios(&hp_jack, ARRAY_SIZE(hp_jack_gpios),
		hp_jack_gpios);
	gpio_free(H1940_LATCH_AUDIO_POWER);
}

module_init(h1940_init);
module_exit(h1940_exit);

/* Module information */
MODULE_AUTHOR("Arnaud Patard, Vasily Khoruzhick");
MODULE_DESCRIPTION("ALSA SoC H1940");
MODULE_LICENSE("GPL");
