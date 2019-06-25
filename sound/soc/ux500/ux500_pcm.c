// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) ST-Ericsson SA 2012
 *
 * Author: Ola Lilja <ola.o.lilja@stericsson.com>,
 *         Roger Nilsson <roger.xr.nilsson@stericsson.com>
 *         for ST-Ericsson.
 *
 * License terms:
 */

#include <asm/page.h>

#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/slab.h>
#include <linux/platform_data/dma-ste-dma40.h>

#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/dmaengine_pcm.h>

#include "ux500_msp_i2s.h"
#include "ux500_pcm.h"

#define UX500_PLATFORM_PERIODS_BYTES_MIN	128
#define UX500_PLATFORM_PERIODS_BYTES_MAX	(64 * PAGE_SIZE)
#define UX500_PLATFORM_PERIODS_MIN		2
#define UX500_PLATFORM_PERIODS_MAX		48
#define UX500_PLATFORM_BUFFER_BYTES_MAX		(2048 * PAGE_SIZE)

static const struct snd_pcm_hardware ux500_pcm_hw = {
	.info = SNDRV_PCM_INFO_INTERLEAVED |
		SNDRV_PCM_INFO_MMAP |
		SNDRV_PCM_INFO_RESUME |
		SNDRV_PCM_INFO_PAUSE,
	.buffer_bytes_max = UX500_PLATFORM_BUFFER_BYTES_MAX,
	.period_bytes_min = UX500_PLATFORM_PERIODS_BYTES_MIN,
	.period_bytes_max = UX500_PLATFORM_PERIODS_BYTES_MAX,
	.periods_min = UX500_PLATFORM_PERIODS_MIN,
	.periods_max = UX500_PLATFORM_PERIODS_MAX,
};

static struct dma_chan *ux500_pcm_request_chan(struct snd_soc_pcm_runtime *rtd,
	struct snd_pcm_substream *substream)
{
	struct snd_soc_dai *dai = rtd->cpu_dai;
	u16 per_data_width, mem_data_width;
	struct stedma40_chan_cfg *dma_cfg;
	struct ux500_msp_dma_params *dma_params;

	dma_params = snd_soc_dai_get_dma_data(dai, substream);
	dma_cfg = dma_params->dma_cfg;

	mem_data_width = DMA_SLAVE_BUSWIDTH_2_BYTES;

	switch (dma_params->data_size) {
	case 32:
		per_data_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
		break;
	case 16:
		per_data_width = DMA_SLAVE_BUSWIDTH_2_BYTES;
		break;
	case 8:
		per_data_width = DMA_SLAVE_BUSWIDTH_1_BYTE;
		break;
	default:
		per_data_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	}

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		dma_cfg->src_info.data_width = mem_data_width;
		dma_cfg->dst_info.data_width = per_data_width;
	} else {
		dma_cfg->src_info.data_width = per_data_width;
		dma_cfg->dst_info.data_width = mem_data_width;
	}

	return snd_dmaengine_pcm_request_channel(stedma40_filter, dma_cfg);
}

static int ux500_pcm_prepare_slave_config(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params,
		struct dma_slave_config *slave_config)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct msp_i2s_platform_data *pdata = rtd->cpu_dai->dev->platform_data;
	struct snd_dmaengine_dai_dma_data *snd_dma_params;
	struct ux500_msp_dma_params *ste_dma_params;
	dma_addr_t dma_addr;
	int ret;

	if (pdata) {
		ste_dma_params =
			snd_soc_dai_get_dma_data(rtd->cpu_dai, substream);
		dma_addr = ste_dma_params->tx_rx_addr;
	} else {
		snd_dma_params =
			snd_soc_dai_get_dma_data(rtd->cpu_dai, substream);
		dma_addr = snd_dma_params->addr;
	}

	ret = snd_hwparams_to_dma_slave_config(substream, params, slave_config);
	if (ret)
		return ret;

	slave_config->dst_maxburst = 4;
	slave_config->src_maxburst = 4;

	slave_config->src_addr_width = DMA_SLAVE_BUSWIDTH_2_BYTES;
	slave_config->dst_addr_width = DMA_SLAVE_BUSWIDTH_2_BYTES;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		slave_config->dst_addr = dma_addr;
	else
		slave_config->src_addr = dma_addr;

	return 0;
}

static const struct snd_dmaengine_pcm_config ux500_dmaengine_pcm_config = {
	.pcm_hardware = &ux500_pcm_hw,
	.compat_request_channel = ux500_pcm_request_chan,
	.prealloc_buffer_size = 128 * 1024,
	.prepare_slave_config = ux500_pcm_prepare_slave_config,
};

static const struct snd_dmaengine_pcm_config ux500_dmaengine_of_pcm_config = {
	.compat_request_channel = ux500_pcm_request_chan,
	.prepare_slave_config = ux500_pcm_prepare_slave_config,
};

int ux500_pcm_register_platform(struct platform_device *pdev)
{
	const struct snd_dmaengine_pcm_config *pcm_config;
	struct device_node *np = pdev->dev.of_node;
	int ret;

	if (np)
		pcm_config = &ux500_dmaengine_of_pcm_config;
	else
		pcm_config = &ux500_dmaengine_pcm_config;

	ret = snd_dmaengine_pcm_register(&pdev->dev, pcm_config,
					 SND_DMAENGINE_PCM_FLAG_COMPAT);
	if (ret < 0) {
		dev_err(&pdev->dev,
			"%s: ERROR: Failed to register platform '%s' (%d)!\n",
			__func__, pdev->name, ret);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(ux500_pcm_register_platform);

int ux500_pcm_unregister_platform(struct platform_device *pdev)
{
	snd_dmaengine_pcm_unregister(&pdev->dev);
	return 0;
}
EXPORT_SYMBOL_GPL(ux500_pcm_unregister_platform);

MODULE_AUTHOR("Ola Lilja");
MODULE_AUTHOR("Roger Nilsson");
MODULE_DESCRIPTION("ASoC UX500 driver");
MODULE_LICENSE("GPL v2");
