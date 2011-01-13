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

#include <plat/audio.h>

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

static int s3c64xx_i2sv4_probe(struct snd_soc_dai *dai)
{
	struct s3c_i2sv2_info *i2s = &s3c64xx_i2sv4;
	int ret = 0;

	snd_soc_dai_set_drvdata(dai, i2s);

	ret = s3c_i2sv2_probe(dai, i2s, i2s->base);

	return ret;
}

static int s3c_i2sv4_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params,
				 struct snd_soc_dai *cpu_dai)
{
	struct s3c_i2sv2_info *i2s = snd_soc_dai_get_drvdata(cpu_dai);
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

static struct snd_soc_dai_driver s3c64xx_i2s_v4_dai = {
	.symmetric_rates = 1,
	.playback = {
		.channels_min = 2,
		.channels_max = 2,
		.rates = S3C64XX_I2S_RATES,
		.formats = S3C64XX_I2S_FMTS,
	},
	.capture = {
		.channels_min = 2,
		.channels_max = 2,
		.rates = S3C64XX_I2S_RATES,
		.formats = S3C64XX_I2S_FMTS,
	},
	.probe = s3c64xx_i2sv4_probe,
	.ops = &s3c64xx_i2sv4_dai_ops,
};

static __devinit int s3c64xx_i2sv4_dev_probe(struct platform_device *pdev)
{
	struct s3c_audio_pdata *i2s_pdata;
	struct s3c_i2sv2_info *i2s;
	struct resource *res;
	int ret;

	i2s = &s3c64xx_i2sv4;

	i2s->feature |= S3C_FEATURE_CDCLKCON;

	i2s->dma_capture = &s3c64xx_i2sv4_pcm_stereo_in;
	i2s->dma_playback = &s3c64xx_i2sv4_pcm_stereo_out;

	res = platform_get_resource(pdev, IORESOURCE_DMA, 0);
	if (!res) {
		dev_err(&pdev->dev, "Unable to get I2S-TX dma resource\n");
		return -ENXIO;
	}
	i2s->dma_playback->channel = res->start;

	res = platform_get_resource(pdev, IORESOURCE_DMA, 1);
	if (!res) {
		dev_err(&pdev->dev, "Unable to get I2S-RX dma resource\n");
		return -ENXIO;
	}
	i2s->dma_capture->channel = res->start;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "Unable to get I2S SFR address\n");
		return -ENXIO;
	}

	if (!request_mem_region(res->start, resource_size(res),
				"s3c64xx-i2s-v4")) {
		dev_err(&pdev->dev, "Unable to request SFR region\n");
		return -EBUSY;
	}
	i2s->dma_capture->dma_addr = res->start + S3C2412_IISRXD;
	i2s->dma_playback->dma_addr = res->start + S3C2412_IISTXD;

	i2s->dma_capture->client = &s3c64xx_dma_client_in;
	i2s->dma_capture->dma_size = 4;
	i2s->dma_playback->client = &s3c64xx_dma_client_out;
	i2s->dma_playback->dma_size = 4;

	i2s->base = res->start;

	i2s_pdata = pdev->dev.platform_data;
	if (i2s_pdata && i2s_pdata->cfg_gpio && i2s_pdata->cfg_gpio(pdev)) {
		dev_err(&pdev->dev, "Unable to configure gpio\n");
		return -EINVAL;
	}

	i2s->iis_cclk = clk_get(&pdev->dev, "audio-bus");
	if (IS_ERR(i2s->iis_cclk)) {
		dev_err(&pdev->dev, "failed to get audio-bus\n");
		ret = PTR_ERR(i2s->iis_cclk);
		goto err;
	}

	clk_enable(i2s->iis_cclk);

	ret = s3c_i2sv2_register_dai(&pdev->dev, pdev->id, &s3c64xx_i2s_v4_dai);
	if (ret != 0)
		goto err_i2sv2;

	return 0;

err_i2sv2:
	clk_put(i2s->iis_cclk);
err:
	return ret;
}

static __devexit int s3c64xx_i2sv4_dev_remove(struct platform_device *pdev)
{
	struct s3c_i2sv2_info *i2s = &s3c64xx_i2sv4;
	struct resource *res;

	snd_soc_unregister_dai(&pdev->dev);
	clk_put(i2s->iis_cclk);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res)
		release_mem_region(res->start, resource_size(res));
	else
		dev_warn(&pdev->dev, "Unable to get I2S SFR address\n");
		
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
MODULE_ALIAS("platform:s3c64xx-iis-v4");
