/* sound/soc/s3c24xx/s3c64xx-i2s-v4.c
 *
 * ALSA SoC Audio Layer - S3C64XX I2Sv4 driver
 * Copyright (c) 2010 Samsung Electronics Co. Ltd
 * 	Author: Jaswinder Singh <jassi.brar@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/clk.h>
#include <linux/gpio.h>
#include <linux/io.h>

#include <sound/soc.h>
#include <sound/pcm_params.h>

#include <mach/gpio-bank-c.h>
#include <mach/gpio-bank-h.h>
#include <plat/gpio-cfg.h>

#include <mach/map.h>
#include <mach/dma.h>

#include "s3c-dma.h"
#include "regs-i2s-v2.h"
#include "s3c64xx-i2s.h"

static struct s3c2410_dma_client s3c64xx_dma_client_out = {
	.name		= "I2Sv4 PCM Stereo out"
};

static struct s3c2410_dma_client s3c64xx_dma_client_in = {
	.name		= "I2Sv4 PCM Stereo in"
};

static struct s3c_dma_params s3c64xx_i2sv4_pcm_stereo_out;
static struct s3c_dma_params s3c64xx_i2sv4_pcm_stereo_in;
static struct s3c_i2sv2_info s3c64xx_i2sv4;

struct snd_soc_dai s3c64xx_i2s_v4_dai;
EXPORT_SYMBOL_GPL(s3c64xx_i2s_v4_dai);

static inline struct s3c_i2sv2_info *to_info(struct snd_soc_dai *cpu_dai)
{
	return cpu_dai->private_data;
}

static int s3c64xx_i2sv4_probe(struct platform_device *pdev,
			     struct snd_soc_dai *dai)
{
	/* configure GPIO for i2s port */
	s3c_gpio_cfgpin(S3C64XX_GPC(4), S3C64XX_GPC4_I2S_V40_DO0);
	s3c_gpio_cfgpin(S3C64XX_GPC(5), S3C64XX_GPC5_I2S_V40_DO1);
	s3c_gpio_cfgpin(S3C64XX_GPC(7), S3C64XX_GPC7_I2S_V40_DO2);
	s3c_gpio_cfgpin(S3C64XX_GPH(6), S3C64XX_GPH6_I2S_V40_BCLK);
	s3c_gpio_cfgpin(S3C64XX_GPH(7), S3C64XX_GPH7_I2S_V40_CDCLK);
	s3c_gpio_cfgpin(S3C64XX_GPH(8), S3C64XX_GPH8_I2S_V40_LRCLK);
	s3c_gpio_cfgpin(S3C64XX_GPH(9), S3C64XX_GPH9_I2S_V40_DI);

	return 0;
}

static int s3c_i2sv4_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params,
				 struct snd_soc_dai *cpu_dai)
{
	struct s3c_i2sv2_info *i2s = to_info(cpu_dai);
	struct s3c_dma_params *dma_data;
	u32 iismod;

	dev_dbg(cpu_dai->dev, "Entered %s\n", __func__);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		dma_data = i2s->dma_playback;
	else
		dma_data = i2s->dma_capture;

	snd_soc_dai_set_dma_data(cpu_dai, substream, dma_data);

	iismod = readl(i2s->regs + S3C2412_IISMOD);
	dev_dbg(cpu_dai->dev, "%s: r: IISMOD: %x\n", __func__, iismod);

	iismod &= ~S3C64XX_IISMOD_BLC_MASK;
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S8:
		iismod |= S3C64XX_IISMOD_BLC_8BIT;
		break;
	case SNDRV_PCM_FORMAT_S16_LE:
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		iismod |= S3C64XX_IISMOD_BLC_24BIT;
		break;
	}

	writel(iismod, i2s->regs + S3C2412_IISMOD);
	dev_dbg(cpu_dai->dev, "%s: w: IISMOD: %x\n", __func__, iismod);

	return 0;
}

static struct snd_soc_dai_ops s3c64xx_i2sv4_dai_ops = {
	.hw_params	= s3c_i2sv4_hw_params,
};

static __devinit int s3c64xx_i2sv4_dev_probe(struct platform_device *pdev)
{
	struct s3c_i2sv2_info *i2s;
	struct snd_soc_dai *dai;
	int ret;

	i2s = &s3c64xx_i2sv4;
	dai = &s3c64xx_i2s_v4_dai;

	if (dai->dev) {
		dev_dbg(dai->dev, "%s: \
			I2Sv4 instance already registered!\n", __func__);
		return -EBUSY;
	}

	dai->dev = &pdev->dev;
	dai->name = "s3c64xx-i2s-v4";
	dai->id = 0;
	dai->symmetric_rates = 1;
	dai->playback.channels_min = 2;
	dai->playback.channels_max = 2;
	dai->playback.rates = S3C64XX_I2S_RATES;
	dai->playback.formats = S3C64XX_I2S_FMTS;
	dai->capture.channels_min = 2;
	dai->capture.channels_max = 2;
	dai->capture.rates = S3C64XX_I2S_RATES;
	dai->capture.formats = S3C64XX_I2S_FMTS;
	dai->probe = s3c64xx_i2sv4_probe;
	dai->ops = &s3c64xx_i2sv4_dai_ops;

	i2s->feature |= S3C_FEATURE_CDCLKCON;

	i2s->dma_capture = &s3c64xx_i2sv4_pcm_stereo_in;
	i2s->dma_playback = &s3c64xx_i2sv4_pcm_stereo_out;

	i2s->dma_capture->channel = DMACH_HSI_I2SV40_RX;
	i2s->dma_capture->dma_addr = S3C64XX_PA_IISV4 + S3C2412_IISRXD;
	i2s->dma_playback->channel = DMACH_HSI_I2SV40_TX;
	i2s->dma_playback->dma_addr = S3C64XX_PA_IISV4 + S3C2412_IISTXD;

	i2s->dma_capture->client = &s3c64xx_dma_client_in;
	i2s->dma_capture->dma_size = 4;
	i2s->dma_playback->client = &s3c64xx_dma_client_out;
	i2s->dma_playback->dma_size = 4;

	i2s->iis_cclk = clk_get(&pdev->dev, "audio-bus");
	if (IS_ERR(i2s->iis_cclk)) {
		dev_err(&pdev->dev, "failed to get audio-bus\n");
		ret = PTR_ERR(i2s->iis_cclk);
		goto err;
	}

	clk_enable(i2s->iis_cclk);

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

static __devexit int s3c64xx_i2sv4_dev_remove(struct platform_device *pdev)
{
	dev_err(&pdev->dev, "Device removal not yet supported\n");
	return 0;
}

static struct platform_driver s3c64xx_i2sv4_driver = {
	.probe  = s3c64xx_i2sv4_dev_probe,
	.remove = s3c64xx_i2sv4_dev_remove,
	.driver = {
		.name = "s3c64xx-iis-v4",
		.owner = THIS_MODULE,
	},
};

static int __init s3c64xx_i2sv4_init(void)
{
	return platform_driver_register(&s3c64xx_i2sv4_driver);
}
module_init(s3c64xx_i2sv4_init);

static void __exit s3c64xx_i2sv4_exit(void)
{
	platform_driver_unregister(&s3c64xx_i2sv4_driver);
}
module_exit(s3c64xx_i2sv4_exit);

/* Module information */
MODULE_AUTHOR("Jaswinder Singh, <jassi.brar@samsung.com>");
MODULE_DESCRIPTION("S3C64XX I2Sv4 SoC Interface");
MODULE_LICENSE("GPL");
