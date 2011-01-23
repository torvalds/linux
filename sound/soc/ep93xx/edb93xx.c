/*
 * SoC audio for EDB93xx
 *
 * Copyright (c) 2010 Alexander Sverdlin <subaparts@yandex.ru>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * This driver support CS4271 codec being master or slave, working
 * in control port mode, connected either via SPI or I2C.
 * The data format accepted is I2S or left-justified.
 * DAPM support not implemented.
 */

#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <asm/mach-types.h>
#include <mach/hardware.h>
#include "ep93xx-pcm.h"

#define edb93xx_has_audio() (machine_is_edb9301() ||	\
			     machine_is_edb9302() ||	\
			     machine_is_edb9302a() ||	\
			     machine_is_edb9307a() ||	\
			     machine_is_edb9315a())

static int edb93xx_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	int err;
	unsigned int rate = params_rate(params);
	/*
	 * We set LRCLK equal to `rate' and SCLK = LRCLK * 64,
	 * because our sample size is 32 bit * 2 channels.
	 * I2S standard permits us to transmit more bits than
	 * the codec uses.
	 * MCLK = SCLK * 4 is the best recommended value,
	 * but we have to fall back to ratio 2 for higher
	 * sample rates.
	 */
	unsigned int mclk_rate = rate * 64 * ((rate <= 48000) ? 4 : 2);

	err = snd_soc_dai_set_fmt(codec_dai, SND_SOC_DAIFMT_I2S |
				  SND_SOC_DAIFMT_NB_IF |
				  SND_SOC_DAIFMT_CBS_CFS);
	if (err)
		return err;

	err = snd_soc_dai_set_fmt(cpu_dai, SND_SOC_DAIFMT_I2S |
				  SND_SOC_DAIFMT_NB_IF |
				  SND_SOC_DAIFMT_CBS_CFS);
	if (err)
		return err;

	err = snd_soc_dai_set_sysclk(codec_dai, 0, mclk_rate,
				     SND_SOC_CLOCK_IN);
	if (err)
		return err;

	return snd_soc_dai_set_sysclk(cpu_dai, 0, mclk_rate,
				      SND_SOC_CLOCK_OUT);
}

static struct snd_soc_ops edb93xx_ops = {
	.hw_params	= edb93xx_hw_params,
};

static struct snd_soc_dai_link edb93xx_dai = {
	.name		= "CS4271",
	.stream_name	= "CS4271 HiFi",
	.platform_name	= "ep93xx-pcm-audio",
	.cpu_dai_name	= "ep93xx-i2s",
	.codec_name	= "spi0.0",
	.codec_dai_name	= "cs4271-hifi",
	.ops		= &edb93xx_ops,
};

static struct snd_soc_card snd_soc_edb93xx = {
	.name		= "EDB93XX",
	.dai_link	= &edb93xx_dai,
	.num_links	= 1,
};

static struct platform_device *edb93xx_snd_device;

static int __init edb93xx_init(void)
{
	int ret;

	if (!edb93xx_has_audio())
		return -ENODEV;

	ret = ep93xx_i2s_acquire(EP93XX_SYSCON_DEVCFG_I2SONAC97,
				 EP93XX_SYSCON_I2SCLKDIV_ORIDE |
				 EP93XX_SYSCON_I2SCLKDIV_SPOL);
	if (ret)
		return ret;

	edb93xx_snd_device = platform_device_alloc("soc-audio", -1);
	if (!edb93xx_snd_device) {
		ret = -ENOMEM;
		goto free_i2s;
	}

	platform_set_drvdata(edb93xx_snd_device, &snd_soc_edb93xx);
	ret = platform_device_add(edb93xx_snd_device);
	if (ret)
		goto device_put;

	return 0;

device_put:
	platform_device_put(edb93xx_snd_device);
free_i2s:
	ep93xx_i2s_release();
	return ret;
}
module_init(edb93xx_init);

static void __exit edb93xx_exit(void)
{
	platform_device_unregister(edb93xx_snd_device);
	ep93xx_i2s_release();
}
module_exit(edb93xx_exit);

MODULE_AUTHOR("Alexander Sverdlin <subaparts@yandex.ru>");
MODULE_DESCRIPTION("ALSA SoC EDB93xx");
MODULE_LICENSE("GPL");
