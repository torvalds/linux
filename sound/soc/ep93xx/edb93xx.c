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
#include <linux/module.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <asm/mach-types.h>
#include <mach/hardware.h>
#include "ep93xx-pcm.h"

static int edb93xx_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	int err;
	unsigned int mclk_rate;
	unsigned int rate = params_rate(params);

	/*
	 * According to CS4271 datasheet we use MCLK/LRCK=256 for
	 * rates below 50kHz and 128 for higher sample rates
	 */
	if (rate < 50000)
		mclk_rate = rate * 64 * 4;
	else
		mclk_rate = rate * 64 * 2;

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
	.dai_fmt	= SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_IF |
			  SND_SOC_DAIFMT_CBS_CFS,
	.ops		= &edb93xx_ops,
};

static struct snd_soc_card snd_soc_edb93xx = {
	.name		= "EDB93XX",
	.owner		= THIS_MODULE,
	.dai_link	= &edb93xx_dai,
	.num_links	= 1,
};

static int __devinit edb93xx_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = &snd_soc_edb93xx;
	int ret;

	ret = ep93xx_i2s_acquire(EP93XX_SYSCON_DEVCFG_I2SONAC97,
				 EP93XX_SYSCON_I2SCLKDIV_ORIDE |
				 EP93XX_SYSCON_I2SCLKDIV_SPOL);
	if (ret)
		return ret;

	card->dev = &pdev->dev;

	ret = snd_soc_register_card(card);
	if (ret) {
		dev_err(&pdev->dev, "snd_soc_register_card() failed: %d\n",
			ret);
		ep93xx_i2s_release();
	}

	return ret;
}

static int __devexit edb93xx_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);

	snd_soc_unregister_card(card);
	ep93xx_i2s_release();

	return 0;
}

static struct platform_driver edb93xx_driver = {
	.driver		= {
		.name	= "edb93xx-audio",
		.owner	= THIS_MODULE,
	},
	.probe		= edb93xx_probe,
	.remove		= __devexit_p(edb93xx_remove),
};

module_platform_driver(edb93xx_driver);

MODULE_AUTHOR("Alexander Sverdlin <subaparts@yandex.ru>");
MODULE_DESCRIPTION("ALSA SoC EDB93xx");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:edb93xx-audio");
