// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * ALSA SoC Voice Codec Interface for TI DAVINCI processor
 *
 * Copyright (C) 2010 Texas Instruments.
 *
 * Author: Miguel Aguilar <miguel.aguilar@ridgerun.com>
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/mfd/davinci_voicecodec.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/initval.h>
#include <sound/soc.h>
#include <sound/dmaengine_pcm.h>

#include "edma-pcm.h"
#include "davinci-i2s.h"

#define MOD_REG_BIT(val, mask, set) do { \
	if (set) { \
		val |= mask; \
	} else { \
		val &= ~mask; \
	} \
} while (0)

struct davinci_vcif_dev {
	struct davinci_vc *davinci_vc;
	struct snd_dmaengine_dai_dma_data dma_data[2];
	int dma_request[2];
};

static void davinci_vcif_start(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct davinci_vcif_dev *davinci_vcif_dev =
			snd_soc_dai_get_drvdata(asoc_rtd_to_cpu(rtd, 0));
	struct davinci_vc *davinci_vc = davinci_vcif_dev->davinci_vc;
	u32 w;

	/* Start the sample generator and enable transmitter/receiver */
	w = readl(davinci_vc->base + DAVINCI_VC_CTRL);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		MOD_REG_BIT(w, DAVINCI_VC_CTRL_RSTDAC, 0);
	else
		MOD_REG_BIT(w, DAVINCI_VC_CTRL_RSTADC, 0);

	writel(w, davinci_vc->base + DAVINCI_VC_CTRL);
}

static void davinci_vcif_stop(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct davinci_vcif_dev *davinci_vcif_dev =
			snd_soc_dai_get_drvdata(asoc_rtd_to_cpu(rtd, 0));
	struct davinci_vc *davinci_vc = davinci_vcif_dev->davinci_vc;
	u32 w;

	/* Reset transmitter/receiver and sample rate/frame sync generators */
	w = readl(davinci_vc->base + DAVINCI_VC_CTRL);
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		MOD_REG_BIT(w, DAVINCI_VC_CTRL_RSTDAC, 1);
	else
		MOD_REG_BIT(w, DAVINCI_VC_CTRL_RSTADC, 1);

	writel(w, davinci_vc->base + DAVINCI_VC_CTRL);
}

static int davinci_vcif_hw_params(struct snd_pcm_substream *substream,
				  struct snd_pcm_hw_params *params,
				  struct snd_soc_dai *dai)
{
	struct davinci_vcif_dev *davinci_vcif_dev = snd_soc_dai_get_drvdata(dai);
	struct davinci_vc *davinci_vc = davinci_vcif_dev->davinci_vc;
	u32 w;

	/* Restart the codec before setup */
	davinci_vcif_stop(substream);
	davinci_vcif_start(substream);

	/* General line settings */
	writel(DAVINCI_VC_CTRL_MASK, davinci_vc->base + DAVINCI_VC_CTRL);

	writel(DAVINCI_VC_INT_MASK, davinci_vc->base + DAVINCI_VC_INTCLR);

	writel(DAVINCI_VC_INT_MASK, davinci_vc->base + DAVINCI_VC_INTEN);

	w = readl(davinci_vc->base + DAVINCI_VC_CTRL);

	/* Determine xfer data type */
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_U8:
		MOD_REG_BIT(w, DAVINCI_VC_CTRL_RD_BITS_8 |
			    DAVINCI_VC_CTRL_RD_UNSIGNED |
			    DAVINCI_VC_CTRL_WD_BITS_8 |
			    DAVINCI_VC_CTRL_WD_UNSIGNED, 1);
		break;
	case SNDRV_PCM_FORMAT_S8:
		MOD_REG_BIT(w, DAVINCI_VC_CTRL_RD_BITS_8 |
			    DAVINCI_VC_CTRL_WD_BITS_8, 1);

		MOD_REG_BIT(w, DAVINCI_VC_CTRL_RD_UNSIGNED |
			    DAVINCI_VC_CTRL_WD_UNSIGNED, 0);
		break;
	case SNDRV_PCM_FORMAT_S16_LE:
		MOD_REG_BIT(w, DAVINCI_VC_CTRL_RD_BITS_8 |
			    DAVINCI_VC_CTRL_RD_UNSIGNED |
			    DAVINCI_VC_CTRL_WD_BITS_8 |
			    DAVINCI_VC_CTRL_WD_UNSIGNED, 0);
		break;
	default:
		printk(KERN_WARNING "davinci-vcif: unsupported PCM format");
		return -EINVAL;
	}

	writel(w, davinci_vc->base + DAVINCI_VC_CTRL);

	return 0;
}

static int davinci_vcif_trigger(struct snd_pcm_substream *substream, int cmd,
				struct snd_soc_dai *dai)
{
	int ret = 0;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		davinci_vcif_start(substream);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		davinci_vcif_stop(substream);
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

#define DAVINCI_VCIF_RATES	SNDRV_PCM_RATE_8000_48000

static const struct snd_soc_dai_ops davinci_vcif_dai_ops = {
	.trigger	= davinci_vcif_trigger,
	.hw_params	= davinci_vcif_hw_params,
};

static int davinci_vcif_dai_probe(struct snd_soc_dai *dai)
{
	struct davinci_vcif_dev *dev = snd_soc_dai_get_drvdata(dai);

	dai->playback_dma_data = &dev->dma_data[SNDRV_PCM_STREAM_PLAYBACK];
	dai->capture_dma_data = &dev->dma_data[SNDRV_PCM_STREAM_CAPTURE];

	return 0;
}

static struct snd_soc_dai_driver davinci_vcif_dai = {
	.probe = davinci_vcif_dai_probe,
	.playback = {
		.channels_min = 1,
		.channels_max = 2,
		.rates = DAVINCI_VCIF_RATES,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,},
	.capture = {
		.channels_min = 1,
		.channels_max = 2,
		.rates = DAVINCI_VCIF_RATES,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,},
	.ops = &davinci_vcif_dai_ops,

};

static const struct snd_soc_component_driver davinci_vcif_component = {
	.name		= "davinci-vcif",
};

static int davinci_vcif_probe(struct platform_device *pdev)
{
	struct davinci_vc *davinci_vc = pdev->dev.platform_data;
	struct davinci_vcif_dev *davinci_vcif_dev;
	int ret;

	davinci_vcif_dev = devm_kzalloc(&pdev->dev,
					sizeof(struct davinci_vcif_dev),
					GFP_KERNEL);
	if (!davinci_vcif_dev)
		return -ENOMEM;

	/* DMA tx params */
	davinci_vcif_dev->davinci_vc = davinci_vc;
	davinci_vcif_dev->dma_data[SNDRV_PCM_STREAM_PLAYBACK].filter_data =
				&davinci_vc->davinci_vcif.dma_tx_channel;
	davinci_vcif_dev->dma_data[SNDRV_PCM_STREAM_PLAYBACK].addr =
				davinci_vc->davinci_vcif.dma_tx_addr;

	/* DMA rx params */
	davinci_vcif_dev->dma_data[SNDRV_PCM_STREAM_CAPTURE].filter_data =
				&davinci_vc->davinci_vcif.dma_rx_channel;
	davinci_vcif_dev->dma_data[SNDRV_PCM_STREAM_CAPTURE].addr =
				davinci_vc->davinci_vcif.dma_rx_addr;

	dev_set_drvdata(&pdev->dev, davinci_vcif_dev);

	ret = devm_snd_soc_register_component(&pdev->dev,
					      &davinci_vcif_component,
					      &davinci_vcif_dai, 1);
	if (ret != 0) {
		dev_err(&pdev->dev, "could not register dai\n");
		return ret;
	}

	ret = edma_pcm_platform_register(&pdev->dev);
	if (ret) {
		dev_err(&pdev->dev, "register PCM failed: %d\n", ret);
		return ret;
	}

	return 0;
}

static struct platform_driver davinci_vcif_driver = {
	.probe		= davinci_vcif_probe,
	.driver		= {
		.name	= "davinci-vcif",
	},
};

module_platform_driver(davinci_vcif_driver);

MODULE_AUTHOR("Miguel Aguilar");
MODULE_DESCRIPTION("Texas Instruments DaVinci ASoC Voice Codec Interface");
MODULE_LICENSE("GPL");
