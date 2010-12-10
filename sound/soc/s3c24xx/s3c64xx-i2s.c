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

#include <linux/module.h>
#include <linux/clk.h>
#include <linux/gpio.h>
#include <linux/io.h>
#include <linux/slab.h>

#include <sound/soc.h>

#include <plat/audio.h>

#include <mach/map.h>
#include <mach/dma.h>

#include "s3c-dma.h"
#include "regs-i2s-v2.h"
#include "s3c64xx-i2s.h"

/* The value should be set to maximum of the total number
 * of I2Sv3 controllers that any supported SoC has.
 */
#define MAX_I2SV3	2

static struct s3c2410_dma_client s3c64xx_dma_client_out = {
	.name		= "I2S PCM Stereo out"
};

static struct s3c2410_dma_client s3c64xx_dma_client_in = {
	.name		= "I2S PCM Stereo in"
};

static struct s3c_dma_params s3c64xx_i2s_pcm_stereo_out[MAX_I2SV3];
static struct s3c_dma_params s3c64xx_i2s_pcm_stereo_in[MAX_I2SV3];
static struct s3c_i2sv2_info s3c64xx_i2s[MAX_I2SV3];

struct clk *s3c64xx_i2s_get_clock(struct snd_soc_dai *dai)
{
	struct s3c_i2sv2_info *i2s = snd_soc_dai_get_drvdata(dai);
	u32 iismod = readl(i2s->regs + S3C2412_IISMOD);

	if (iismod & S3C2412_IISMOD_IMS_SYSMUX)
		return i2s->iis_cclk;
	else
		return i2s->iis_pclk;
}
EXPORT_SYMBOL_GPL(s3c64xx_i2s_get_clock);

static int s3c64xx_i2s_probe(struct snd_soc_dai *dai)
{
	struct s3c_i2sv2_info *i2s;
	int ret;

	if (dai->id >= MAX_I2SV3) {
		dev_err(dai->dev, "id %d out of range\n", dai->id);
		return -EINVAL;
	}

	i2s = &s3c64xx_i2s[dai->id];
	snd_soc_dai_set_drvdata(dai, i2s);

	i2s->iis_cclk = clk_get(dai->dev, "audio-bus");
	if (IS_ERR(i2s->iis_cclk)) {
		dev_err(dai->dev, "failed to get audio-bus\n");
		ret = PTR_ERR(i2s->iis_cclk);
		goto err;
	}

	clk_enable(i2s->iis_cclk);

	ret = s3c_i2sv2_probe(dai, i2s, i2s->base);
	if (ret)
		goto err_clk;

	return 0;

err_clk:
	clk_disable(i2s->iis_cclk);
	clk_put(i2s->iis_cclk);
err:
	kfree(i2s);
	return ret;
}

static int s3c64xx_i2s_remove(struct snd_soc_dai *dai)
{
	struct s3c_i2sv2_info *i2s = snd_soc_dai_get_drvdata(dai);

	clk_disable(i2s->iis_cclk);
	clk_put(i2s->iis_cclk);
	kfree(i2s);
	return 0;
}

static struct snd_soc_dai_ops s3c64xx_i2s_dai_ops;

static struct snd_soc_dai_driver s3c64xx_i2s_dai[MAX_I2SV3] = {
{
	.name = "s3c64xx-i2s-0",
	.probe = s3c64xx_i2s_probe,
	.remove = s3c64xx_i2s_remove,
	.playback = {
		.channels_min = 2,
		.channels_max = 2,
		.rates = S3C64XX_I2S_RATES,
		.formats = S3C64XX_I2S_FMTS,},
	.capture = {
		.channels_min = 2,
		.channels_max = 2,
		.rates = S3C64XX_I2S_RATES,
		.formats = S3C64XX_I2S_FMTS,},
	.ops = &s3c64xx_i2s_dai_ops,
	.symmetric_rates = 1,
}, {
	.name = "s3c64xx-i2s-1",
	.probe = s3c64xx_i2s_probe,
	.remove = s3c64xx_i2s_remove,
	.playback = {
		.channels_min = 2,
		.channels_max = 2,
		.rates = S3C64XX_I2S_RATES,
		.formats = S3C64XX_I2S_FMTS,},
	.capture = {
		.channels_min = 2,
		.channels_max = 2,
		.rates = S3C64XX_I2S_RATES,
		.formats = S3C64XX_I2S_FMTS,},
	.ops = &s3c64xx_i2s_dai_ops,
	.symmetric_rates = 1,
},};

static __devinit int s3c64xx_iis_dev_probe(struct platform_device *pdev)
{
	struct s3c_audio_pdata *i2s_pdata;
	struct s3c_i2sv2_info *i2s;
	struct resource *res;
	int i, ret;

	if (pdev->id >= MAX_I2SV3) {
		dev_err(&pdev->dev, "id %d out of range\n", pdev->id);
		return -EINVAL;
	}

	i2s = &s3c64xx_i2s[pdev->id];

	i2s->dma_capture = &s3c64xx_i2s_pcm_stereo_in[pdev->id];
	i2s->dma_playback = &s3c64xx_i2s_pcm_stereo_out[pdev->id];

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
				"s3c64xx-i2s")) {
		dev_err(&pdev->dev, "Unable to request SFR region\n");
		return -EBUSY;
	}
	i2s->base = res->start;

	i2s_pdata = pdev->dev.platform_data;
	if (i2s_pdata && i2s_pdata->cfg_gpio && i2s_pdata->cfg_gpio(pdev)) {
		dev_err(&pdev->dev, "Unable to configure gpio\n");
		return -EINVAL;
	}
	i2s->dma_capture->dma_addr = res->start + S3C2412_IISRXD;
	i2s->dma_playback->dma_addr = res->start + S3C2412_IISTXD;

	i2s->dma_capture->client = &s3c64xx_dma_client_in;
	i2s->dma_capture->dma_size = 4;
	i2s->dma_playback->client = &s3c64xx_dma_client_out;
	i2s->dma_playback->dma_size = 4;

	for (i = 0; i < ARRAY_SIZE(s3c64xx_i2s_dai); i++) {
		ret = s3c_i2sv2_register_dai(&pdev->dev, i,
						&s3c64xx_i2s_dai[i]);
		if (ret != 0)
			return ret;
	}

	return 0;
}

static __devexit int s3c64xx_iis_dev_remove(struct platform_device *pdev)
{
	snd_soc_unregister_dais(&pdev->dev, ARRAY_SIZE(s3c64xx_i2s_dai));
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
MODULE_ALIAS("platform:s3c64xx-iis");
