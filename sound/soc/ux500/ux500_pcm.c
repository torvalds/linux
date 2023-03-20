// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) ST-Ericsson SA 2012
 *
 * Author: Ola Lilja <ola.o.lilja@stericsson.com>,
 *         Roger Nilsson <roger.xr.nilsson@stericsson.com>
 *         for ST-Ericsson.
 */

#include <asm/page.h>

#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/slab.h>

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

static int ux500_pcm_prepare_slave_config(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params,
		struct dma_slave_config *slave_config)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct snd_dmaengine_dai_dma_data *snd_dma_params;
	dma_addr_t dma_addr;
	int ret;

	snd_dma_params = snd_soc_dai_get_dma_data(asoc_rtd_to_cpu(rtd, 0), substream);
	dma_addr = snd_dma_params->addr;

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

static const struct snd_dmaengine_pcm_config ux500_dmaengine_of_pcm_config = {
	.prepare_slave_config = ux500_pcm_prepare_slave_config,
};

int ux500_pcm_register_platform(struct platform_device *pdev)
{
	int ret;

	ret = snd_dmaengine_pcm_register(&pdev->dev,
					 &ux500_dmaengine_of_pcm_config, 0);
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
