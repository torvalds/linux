/* sound/soc/s3c24xx/s3c64xx-i2s.c
 *
 * ALSA SoC Audio Layer - S3C64XX I2S driver
 *
 * Copyright 2008 Openmoko, Inc.
 * Copyright 2008 Simtec Electronics
 *      Ben Dooks <ben@simtec.co.uk>
 *      http://armlinux.simtec.co.uk/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/kernel.h>
#include <linux/gpio.h>
#include <linux/io.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/initval.h>
#include <sound/soc.h>

#include <plat/regs-s3c2412-iis.h>
#include <plat/gpio-bank-d.h>
#include <plat/gpio-bank-e.h>
#include <plat/gpio-cfg.h>
#include <plat/audio.h>

#include <mach/map.h>
#include <mach/dma.h>

#include "s3c24xx-pcm.h"
#include "s3c64xx-i2s.h"

static struct s3c2410_dma_client s3c64xx_dma_client_out = {
	.name		= "I2S PCM Stereo out"
};

static struct s3c2410_dma_client s3c64xx_dma_client_in = {
	.name		= "I2S PCM Stereo in"
};

static struct s3c24xx_pcm_dma_params s3c64xx_i2s_pcm_stereo_out[2] = {
	[0] = {
		.channel	= DMACH_I2S0_OUT,
		.client		= &s3c64xx_dma_client_out,
		.dma_addr	= S3C64XX_PA_IIS0 + S3C2412_IISTXD,
		.dma_size	= 4,
	},
	[1] = {
		.channel	= DMACH_I2S1_OUT,
		.client		= &s3c64xx_dma_client_out,
		.dma_addr	= S3C64XX_PA_IIS1 + S3C2412_IISTXD,
		.dma_size	= 4,
	},
};

static struct s3c24xx_pcm_dma_params s3c64xx_i2s_pcm_stereo_in[2] = {
	[0] = {
		.channel	= DMACH_I2S0_IN,
		.client		= &s3c64xx_dma_client_in,
		.dma_addr	= S3C64XX_PA_IIS0 + S3C2412_IISRXD,
		.dma_size	= 4,
	},
	[1] = {
		.channel	= DMACH_I2S1_IN,
		.client		= &s3c64xx_dma_client_in,
		.dma_addr	= S3C64XX_PA_IIS1 + S3C2412_IISRXD,
		.dma_size	= 4,
	},
};

static struct s3c_i2sv2_info s3c64xx_i2s[2];

static inline struct s3c_i2sv2_info *to_info(struct snd_soc_dai *cpu_dai)
{
	return cpu_dai->private_data;
}

static int s3c64xx_i2s_set_sysclk(struct snd_soc_dai *cpu_dai,
				  int clk_id, unsigned int freq, int dir)
{
	struct s3c_i2sv2_info *i2s = to_info(cpu_dai);
	u32 iismod = readl(i2s->regs + S3C2412_IISMOD);

	switch (clk_id) {
	case S3C64XX_CLKSRC_PCLK:
		iismod &= ~S3C64XX_IISMOD_IMS_SYSMUX;
		break;

	case S3C64XX_CLKSRC_MUX:
		iismod |= S3C64XX_IISMOD_IMS_SYSMUX;
		break;

	default:
		return -EINVAL;
	}

	writel(iismod, i2s->regs + S3C2412_IISMOD);

	return 0;
}

struct clk *s3c64xx_i2s_get_clock(struct snd_soc_dai *dai)
{
	struct s3c_i2sv2_info *i2s = to_info(dai);

	return i2s->iis_cclk;
}
EXPORT_SYMBOL_GPL(s3c64xx_i2s_get_clock);

static int s3c64xx_i2s_probe(struct platform_device *pdev,
			     struct snd_soc_dai *dai)
{
	/* configure GPIO for i2s port */
	switch (dai->id) {
	case 0:
		s3c_gpio_cfgpin(S3C64XX_GPD(0), S3C64XX_GPD0_I2S0_CLK);
		s3c_gpio_cfgpin(S3C64XX_GPD(1), S3C64XX_GPD1_I2S0_CDCLK);
		s3c_gpio_cfgpin(S3C64XX_GPD(2), S3C64XX_GPD2_I2S0_LRCLK);
		s3c_gpio_cfgpin(S3C64XX_GPD(3), S3C64XX_GPD3_I2S0_DI);
		s3c_gpio_cfgpin(S3C64XX_GPD(4), S3C64XX_GPD4_I2S0_D0);
		break;
	case 1:
		s3c_gpio_cfgpin(S3C64XX_GPE(0), S3C64XX_GPE0_I2S1_CLK);
		s3c_gpio_cfgpin(S3C64XX_GPE(1), S3C64XX_GPE1_I2S1_CDCLK);
		s3c_gpio_cfgpin(S3C64XX_GPE(2), S3C64XX_GPE2_I2S1_LRCLK);
		s3c_gpio_cfgpin(S3C64XX_GPE(3), S3C64XX_GPE3_I2S1_DI);
		s3c_gpio_cfgpin(S3C64XX_GPE(4), S3C64XX_GPE4_I2S1_D0);
	}

	return 0;
}


#define S3C64XX_I2S_RATES \
	(SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_11025 | SNDRV_PCM_RATE_16000 | \
	SNDRV_PCM_RATE_22050 | SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 | \
	SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_88200 | SNDRV_PCM_RATE_96000)

#define S3C64XX_I2S_FMTS \
	(SNDRV_PCM_FMTBIT_S8 | SNDRV_PCM_FMTBIT_S16_LE |\
	 SNDRV_PCM_FMTBIT_S24_LE)

static struct snd_soc_dai_ops s3c64xx_i2s_dai_ops = {
	.set_sysclk	= s3c64xx_i2s_set_sysclk,	
};

struct snd_soc_dai s3c64xx_i2s_dai[] = {
	{
		.name		= "s3c64xx-i2s",
		.id		= 0,
		.probe		= s3c64xx_i2s_probe,
		.playback = {
			.channels_min	= 2,
			.channels_max	= 2,
			.rates		= S3C64XX_I2S_RATES,
			.formats	= S3C64XX_I2S_FMTS,
		},
		.capture = {
			 .channels_min	= 2,
			 .channels_max	= 2,
			 .rates		= S3C64XX_I2S_RATES,
			 .formats	= S3C64XX_I2S_FMTS,
		 },
		.ops = &s3c64xx_i2s_dai_ops,
		.symmetric_rates = 1,
	},
	{
		.name		= "s3c64xx-i2s",
		.id		= 1,
		.probe		= s3c64xx_i2s_probe,
		.playback = {
			.channels_min	= 2,
			.channels_max	= 2,
			.rates		= S3C64XX_I2S_RATES,
			.formats	= S3C64XX_I2S_FMTS,
		},
		.capture = {
			 .channels_min	= 2,
			 .channels_max	= 2,
			 .rates		= S3C64XX_I2S_RATES,
			 .formats	= S3C64XX_I2S_FMTS,
		 },
		.ops = &s3c64xx_i2s_dai_ops,
		.symmetric_rates = 1,
	},
};
EXPORT_SYMBOL_GPL(s3c64xx_i2s_dai);

static __devinit int s3c64xx_iis_dev_probe(struct platform_device *pdev)
{
	struct s3c_i2sv2_info *i2s;
	struct snd_soc_dai *dai;
	int ret;

	if (pdev->id >= ARRAY_SIZE(s3c64xx_i2s)) {
		dev_err(&pdev->dev, "id %d out of range\n", pdev->id);
		return -EINVAL;
	}

	i2s = &s3c64xx_i2s[pdev->id];
	dai = &s3c64xx_i2s_dai[pdev->id];
	dai->dev = &pdev->dev;

	i2s->dma_capture = &s3c64xx_i2s_pcm_stereo_in[pdev->id];
	i2s->dma_playback = &s3c64xx_i2s_pcm_stereo_out[pdev->id];

	i2s->iis_cclk = clk_get(&pdev->dev, "audio-bus");
	if (IS_ERR(i2s->iis_cclk)) {
		dev_err(&pdev->dev, "failed to get audio-bus\n");
		ret = PTR_ERR(i2s->iis_cclk);
		goto err;
	}

	ret = s3c_i2sv2_probe(pdev, dai, i2s, 0);
	if (ret)
		goto err_clk;

	ret = s3c_i2sv2_register_dai(dai);
	if (ret != 0)
		goto err_i2sv2;

	return 0;

err_i2sv2:
	/* Not implemented for I2Sv2 core yet */
err_clk:
	clk_put(i2s->iis_cclk);
err:
	return ret;
}

static __devexit int s3c64xx_iis_dev_remove(struct platform_device *pdev)
{
	dev_err(&pdev->dev, "Device removal not yet supported\n");
	return 0;
}

static struct platform_driver s3c64xx_iis_driver = {
	.probe  = s3c64xx_iis_dev_probe,
	.remove = s3c64xx_iis_dev_remove,
	.driver = {
		.name = "s3c64xx-iis",
		.owner = THIS_MODULE,
	},
};

static int __init s3c64xx_i2s_init(void)
{
	return platform_driver_register(&s3c64xx_iis_driver);
}
module_init(s3c64xx_i2s_init);

static void __exit s3c64xx_i2s_exit(void)
{
	platform_driver_unregister(&s3c64xx_iis_driver);
}
module_exit(s3c64xx_i2s_exit);

/* Module information */
MODULE_AUTHOR("Ben Dooks, <ben@simtec.co.uk>");
MODULE_DESCRIPTION("S3C64XX I2S SoC Interface");
MODULE_LICENSE("GPL");
