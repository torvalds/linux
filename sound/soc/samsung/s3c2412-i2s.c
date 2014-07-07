/* sound/soc/samsung/s3c2412-i2s.c
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

#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/module.h>

#include <sound/soc.h>
#include <sound/pcm_params.h>

#include <mach/dma.h>
#include <mach/gpio-samsung.h>
#include <plat/gpio-cfg.h>

#include "dma.h"
#include "regs-i2s-v2.h"
#include "s3c2412-i2s.h"

static struct s3c_dma_client s3c2412_dma_client_out = {
	.name		= "I2S PCM Stereo out"
};

static struct s3c_dma_client s3c2412_dma_client_in = {
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
	if (IS_ERR(s3c2412_i2s.iis_cclk)) {
		pr_err("failed to get i2sclk clock\n");
		iounmap(s3c2412_i2s.regs);
		return PTR_ERR(s3c2412_i2s.iis_cclk);
	}

	/* Set MPLL as the source for IIS CLK */

	clk_set_parent(s3c2412_i2s.iis_cclk, clk_get(NULL, "mpll"));
	clk_enable(s3c2412_i2s.iis_cclk);

	s3c2412_i2s.iis_cclk = s3c2412_i2s.iis_pclk;

	/* Configure the I2S pins (GPE0...GPE4) in correct mode */
	s3c_gpio_cfgall_range(S3C2410_GPE(0), 5, S3C_GPIO_SFN(2),
			      S3C_GPIO_PULL_NONE);

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

	switch (params_width(params)) {
	case 8:
		iismod |= S3C2412_IISMOD_8BIT;
		break;
	case 16:
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

static const struct snd_soc_dai_ops s3c2412_i2s_dai_ops = {
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

static const struct snd_soc_component_driver s3c2412_i2s_component = {
	.name		= "s3c2412-i2s",
};

static int s3c2412_iis_dev_probe(struct platform_device *pdev)
{
	int ret = 0;

	ret = s3c_i2sv2_register_component(&pdev->dev, -1,
					   &s3c2412_i2s_component,
					   &s3c2412_i2s_dai);
	if (ret) {
		pr_err("failed to register the dai\n");
		return ret;
	}

	ret = samsung_asoc_dma_platform_register(&pdev->dev);
	if (ret)
		pr_err("failed to register the DMA: %d\n", ret);

	return ret;
}

static struct platform_driver s3c2412_iis_driver = {
	.probe  = s3c2412_iis_dev_probe,
	.driver = {
		.name = "s3c2412-iis",
		.owner = THIS_MODULE,
	},
};

module_platform_driver(s3c2412_iis_driver);

/* Module information */
MODULE_AUTHOR("Ben Dooks, <ben@simtec.co.uk>");
MODULE_DESCRIPTION("S3C2412 I2S SoC Interface");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:s3c2412-iis");
