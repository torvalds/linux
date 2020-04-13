// SPDX-License-Identifier: GPL-2.0
/*
 *  Copyright (C) 2020 Texas Instruments Incorporated - http://www.ti.com
 *  Author: Peter Ujfalusi <peter.ujfalusi@ti.com>
 */

#include <linux/module.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/dmaengine_pcm.h>

#include "udma-pcm.h"

static const struct snd_pcm_hardware udma_pcm_hardware = {
	.info			= SNDRV_PCM_INFO_MMAP |
				  SNDRV_PCM_INFO_MMAP_VALID |
				  SNDRV_PCM_INFO_PAUSE | SNDRV_PCM_INFO_RESUME |
				  SNDRV_PCM_INFO_NO_PERIOD_WAKEUP |
				  SNDRV_PCM_INFO_INTERLEAVED,
	.buffer_bytes_max	= SIZE_MAX,
	.period_bytes_min	= 32,
	.period_bytes_max	= SZ_64K,
	.periods_min		= 2,
	.periods_max		= UINT_MAX,
};

static const struct snd_dmaengine_pcm_config udma_dmaengine_pcm_config = {
	.pcm_hardware = &udma_pcm_hardware,
	.prepare_slave_config = snd_dmaengine_pcm_prepare_slave_config,
};

int udma_pcm_platform_register(struct device *dev)
{
	return devm_snd_dmaengine_pcm_register(dev, &udma_dmaengine_pcm_config,
					       0);
}
EXPORT_SYMBOL_GPL(udma_pcm_platform_register);

MODULE_AUTHOR("Peter Ujfalusi <peter.ujfalusi@ti.com>");
MODULE_DESCRIPTION("UDMA PCM ASoC platform driver");
MODULE_LICENSE("GPL v2");
