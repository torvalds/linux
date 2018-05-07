// SPDX-License-Identifier: GPL-2.0
/*
 *  Copyright (C) 2018 Texas Instruments Incorporated - http://www.ti.com
 *  Author: Peter Ujfalusi <peter.ujfalusi@ti.com>
 */

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/dmaengine_pcm.h>
#include <linux/omap-dma.h>

#include "sdma-pcm.h"

static const struct snd_pcm_hardware sdma_pcm_hardware = {
	.info			= SNDRV_PCM_INFO_MMAP |
				  SNDRV_PCM_INFO_MMAP_VALID |
				  SNDRV_PCM_INFO_PAUSE | SNDRV_PCM_INFO_RESUME |
				  SNDRV_PCM_INFO_NO_PERIOD_WAKEUP |
				  SNDRV_PCM_INFO_INTERLEAVED,
	.period_bytes_min	= 32,
	.period_bytes_max	= 64 * 1024,
	.buffer_bytes_max	= 128 * 1024,
	.periods_min		= 2,
	.periods_max		= 255,
};

static const struct snd_dmaengine_pcm_config sdma_dmaengine_pcm_config = {
	.pcm_hardware = &sdma_pcm_hardware,
	.prepare_slave_config = snd_dmaengine_pcm_prepare_slave_config,
	.compat_filter_fn = omap_dma_filter_fn,
	.prealloc_buffer_size = 128 * 1024,
};

int sdma_pcm_platform_register(struct device *dev,
			       char *txdmachan, char *rxdmachan)
{
	struct snd_dmaengine_pcm_config *config;
	unsigned int flags = SND_DMAENGINE_PCM_FLAG_COMPAT;

	/* Standard names for the directions: 'tx' and 'rx' */
	if (!txdmachan && !rxdmachan)
		return devm_snd_dmaengine_pcm_register(dev,
						&sdma_dmaengine_pcm_config,
						flags);

	config = devm_kzalloc(dev, sizeof(*config), GFP_KERNEL);
	if (!config)
		return -ENOMEM;

	*config = sdma_dmaengine_pcm_config;

	if (!txdmachan || !rxdmachan) {
		/* One direction only PCM */
		flags |= SND_DMAENGINE_PCM_FLAG_HALF_DUPLEX;
		if (!txdmachan) {
			txdmachan = rxdmachan;
			rxdmachan = NULL;
		}
	}

	config->chan_names[0] = txdmachan;
	config->chan_names[1] = rxdmachan;

	return devm_snd_dmaengine_pcm_register(dev, config, flags);
}
EXPORT_SYMBOL_GPL(sdma_pcm_platform_register);
