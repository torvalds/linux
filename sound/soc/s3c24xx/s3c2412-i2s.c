/* sound/soc/s3c24xx/s3c2412-i2s.c
 *
 * ALSA Soc Audio Layer - S3C2412 I2S driver
 *
 * Copyright (c) 2006 Wolfson Microelectronics PLC.
 *	Graeme Gregory graeme.gregory@wolfsonmicro.com
 *	linux@wolfsonmicro.com
 *
 * Copyright (c) 2007, 2004-2005 Simtec Electronics
 *	http://armlinux.simtec.co.uk/
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/clk.h>
#include <linux/kernel.h>
#include <linux/io.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/initval.h>
#include <sound/soc.h>
#include <mach/hardware.h>

#include <mach/regs-gpio.h>
#include <mach/dma.h>

#include "s3c-dma.h"
#include "regs-i2s-v2.h"
#include "s3c2412-i2s.h"

#define S3C2412_I2S_DEBUG 0

static struct s3c2410_dma_client s3c2412_dma_client_out = {
	.name		= "I2S PCM Stereo out"
};

static struct s3c2410_dma_client s3c2412_dma_client_in = {
	.name		= "I2S PCM Stereo in"
};

static struct s3c_dma_params s3c2412_i2s_pcm_stereo_out = {
	.client		= &s3c2412_dma_client_out,
	.channel	= DMACH_I2S_OUT,
	.dma_addr	= S3C2410_PA_IIS + S3C2412_IISTXD,
	.dma_size	= 4,
};

static struct s3c_dma_params s3c2412_i2s_pcm_stereo_in = {
	.client		= &s3c2412_dma_client_in,
	.channel	= DMACH_I2S_IN,
	.dma_addr	= S3C2410_PA_IIS + S3C2412_IISRXD,
	.dma_size	= 4,
};

static struct s3c_i2sv2_info s3c2412_i2s;

static int s3c2412_i2s_probe(struct snd_soc_dai *dai)
{
	int ret;

	pr_debug("Entered %s\n", __func__);

	ret = s3c_i2sv2_probe(dai, &s3c2412_i2s, S3C2410_PA_IIS);
	if (ret)
		return ret;

	s3c2412_i2s.dma_capture = &s3c2412_i2s_pcm_stereo_in;
	s3c2412_i2s.dma_playback = &s3c2412_i2s_pcm_stereo_out;

	s3c2412_i2s.iis_cclk = clk_get(dai->dev, "i2sclk");
	if (s3c2412_i2s.iis_cclk == NULL) {
		pr_err("failed to get i2sclk clock\n");
		iounmap(s3c2412_i2s.regs);
		return -ENODEV;
	}

	/* Set MPLL as the source for IIS CLK */

	clk_set_parent(s3c2412_i2s.iis_cclk, clk_get(NULL, "mpll"));
	clk_enable(s3c2412_i2s.iis_cclk);

	s3c2412_i2s.iis_cclk = s3c2412_i2s.iis_pclk;

	/* Configure the I2S pins in correct mode */
	s3c2410_gpio_cfgpin(S3C2410_GPE0, S3C2410_GPE0_I2SLRCK);
	s3c2410_gpio_cfgpin(S3C2410_GPE1, S3C2410_GPE1_I2SSCLK);
	s3c2410_gpio_cfgpin(S3C2410_GPE2, S3C2410_GPE2_CDCLK);
	s3c2410_gpio_cfgpin(S3C2410_GPE3, S3C2410_GPE3_I2SSDI);
	s3c2410_gpio_cfgpin(S3C2410_GPE4, S3C2410_GPE4_I2SSDO);

	return 0;
}

static int s3c2412_i2s_remove(struct snd_soc_dai *dai)
{
	clk_disable(s3c2412_i2s.iis_cclk);
	clk_put(s3c2412_i2s.iis_cclk);
	iounmap(s3c2412_i2s.regs);

	return 0;
}

static int s3c2412_i2s_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params,
				 struct snd_soc_dai *cpu_dai)
{
	struct s3c_i2sv2_info *i2s = snd_soc_dai_get_drvdata(cpu_dai);
	struct s3c_dma_params *dma_data;
	u32 iismod;

	pr_debug("Entered %s\n", __func__);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		dma_data = i2s->dma_playback;
	else
		dma_data = i2s->dma_capture;

	snd_soc_dai_set_dma_data(cpu_dai, substream, dma_data);

	iismod = readl(i2s->regs + S3C2412_IISMOD);
	pr_debug("%s: r: IISMOD: %x\n", __func__, iismod);

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S8:
		iismod |= S3C2412_IISMOD_8BIT;
		break;
	case SNDRV_PCM_FORMAT_S16_LE:
		iismod &= ~S3C2412_IISMOD_8BIT;
		break;
	}

	writel(iismod, i2s->regs + S3C2412_IISMOD);
	pr_debug("%s: w: IISMOD: %x\n", __func__, iismod);

	return 0;
}

#define S3C2412_I2S_RATES \
	(SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_11025 | SNDRV_PCM_RATE_16000 | \
	SNDRV_PCM_RATE_22050 | SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 | \
	SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_88200 | SNDRV_PCM_RATE_96000)

static struct snd_soc_dai_ops s3c2412_i2s_dai_ops = {
	.hw_params	= s3c2412_i2s_hw_params,
};

static struct snd_soc_dai_driver s3c2412_i2s_dai = {
	.probe		= s3c2412_i2s_probe,
	.remove	= s3c2412_i2s_remove,
	.playback = {
		.channels_min	= 2,
		.channels_max	= 2,
		.rates		= S3C2412_I2S_RATES,
		.formats	= SNDRV_PCM_FMTBIT_S8 | SNDRV_PCM_FMTBIT_S16_LE,
	},
	.capture = {
		.channels_min	= 2,
		.channels_max	= 2,
		.rates		= S3C2412_I2S_RATES,
		.formats	= SNDRV_PCM_FMTBIT_S8 | SNDRV_PCM_FMTBIT_S16_LE,
	},
	.ops = &s3c2412_i2s_dai_ops,
};

static __devinit int s3c2412_iis_dev_probe(struct platform_device *pdev)
{
	return snd_soc_register_dai(&pdev->dev, &s3c2412_i2s_dai);
}

static __devexit int s3c2412_iis_dev_remove(struct platform_device *pdev)
{
	snd_soc_unregister_dai(&pdev->dev);
	return 0;
}

static struct platform_driver s3c2412_iis_driver = {
	.probe  = s3c2412_iis_dev_probe,
	.remove = s3c2412_iis_dev_remove,
	.driver = {
		.name = "s3c2412-iis",
		.owner = THIS_MODULE,
	},
};

static int __init s3c2412_i2s_init(void)
{
	return platform_driver_register(&s3c2412_iis_driver);
}
module_init(s3c2412_i2s_init);

static void __exit s3c2412_i2s_exit(void)
{
	platform_driver_unregister(&s3c2412_iis_driver);
}
module_exit(s3c2412_i2s_exit);

/* Module information */
MODULE_AUTHOR("Ben Dooks, <ben@simtec.co.uk>");
MODULE_DESCRIPTION("S3C2412 I2S SoC Interface");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:s3c2412-iis");
