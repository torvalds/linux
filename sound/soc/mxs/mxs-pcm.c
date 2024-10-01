// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2011 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 * Based on sound/soc/imx/imx-pcm-dma-mx2.c
 */

#include <linux/device.h>
#include <linux/init.h>
#include <linux/module.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/dmaengine_pcm.h>

#include "mxs-pcm.h"

static const struct snd_pcm_hardware snd_mxs_hardware = {
	.info			= SNDRV_PCM_INFO_MMAP |
				  SNDRV_PCM_INFO_MMAP_VALID |
				  SNDRV_PCM_INFO_PAUSE |
				  SNDRV_PCM_INFO_RESUME |
				  SNDRV_PCM_INFO_INTERLEAVED |
				  SNDRV_PCM_INFO_HALF_DUPLEX,
	.period_bytes_min	= 32,
	.period_bytes_max	= 8192,
	.periods_min		= 1,
	.periods_max		= 52,
	.buffer_bytes_max	= 64 * 1024,
	.fifo_size		= 32,
};

static const struct snd_dmaengine_pcm_config mxs_dmaengine_pcm_config = {
	.pcm_hardware = &snd_mxs_hardware,
	.prealloc_buffer_size = 64 * 1024,
};

int mxs_pcm_platform_register(struct device *dev)
{
	return devm_snd_dmaengine_pcm_register(dev, &mxs_dmaengine_pcm_config,
		SND_DMAENGINE_PCM_FLAG_HALF_DUPLEX);
}
EXPORT_SYMBOL_GPL(mxs_pcm_platform_register);

MODULE_LICENSE("GPL");
