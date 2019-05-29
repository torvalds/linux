// SPDX-License-Identifier: GPL-2.0-only
/*
 * tegra_pcm.c - Tegra PCM driver
 *
 * Author: Stephen Warren <swarren@nvidia.com>
 * Copyright (C) 2010,2012 - NVIDIA, Inc.
 *
 * Based on code copyright/by:
 *
 * Copyright (c) 2009-2010, NVIDIA Corporation.
 * Scott Peterson <speterson@nvidia.com>
 * Vijay Mali <vmali@nvidia.com>
 *
 * Copyright (C) 2010 Google, Inc.
 * Iliyan Malchev <malchev@google.com>
 */

#include <linux/module.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/dmaengine_pcm.h>

#include "tegra_pcm.h"

static const struct snd_pcm_hardware tegra_pcm_hardware = {
	.info			= SNDRV_PCM_INFO_MMAP |
				  SNDRV_PCM_INFO_MMAP_VALID |
				  SNDRV_PCM_INFO_INTERLEAVED,
	.period_bytes_min	= 1024,
	.period_bytes_max	= PAGE_SIZE,
	.periods_min		= 2,
	.periods_max		= 8,
	.buffer_bytes_max	= PAGE_SIZE * 8,
	.fifo_size		= 4,
};

static const struct snd_dmaengine_pcm_config tegra_dmaengine_pcm_config = {
	.pcm_hardware = &tegra_pcm_hardware,
	.prepare_slave_config = snd_dmaengine_pcm_prepare_slave_config,
	.prealloc_buffer_size = PAGE_SIZE * 8,
};

int tegra_pcm_platform_register(struct device *dev)
{
	return snd_dmaengine_pcm_register(dev, &tegra_dmaengine_pcm_config, 0);
}
EXPORT_SYMBOL_GPL(tegra_pcm_platform_register);

int tegra_pcm_platform_register_with_chan_names(struct device *dev,
				struct snd_dmaengine_pcm_config *config,
				char *txdmachan, char *rxdmachan)
{
	*config = tegra_dmaengine_pcm_config;
	config->dma_dev = dev->parent;
	config->chan_names[0] = txdmachan;
	config->chan_names[1] = rxdmachan;

	return snd_dmaengine_pcm_register(dev, config, 0);
}
EXPORT_SYMBOL_GPL(tegra_pcm_platform_register_with_chan_names);

void tegra_pcm_platform_unregister(struct device *dev)
{
	return snd_dmaengine_pcm_unregister(dev);
}
EXPORT_SYMBOL_GPL(tegra_pcm_platform_unregister);

MODULE_AUTHOR("Stephen Warren <swarren@nvidia.com>");
MODULE_DESCRIPTION("Tegra PCM ASoC driver");
MODULE_LICENSE("GPL");
