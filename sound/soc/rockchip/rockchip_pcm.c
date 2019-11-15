/*
 * Copyright (c) 2018 Rockchip Electronics Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/device.h>
#include <linux/init.h>
#include <linux/module.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/dmaengine_pcm.h>

#include "rockchip_pcm.h"

static const struct snd_pcm_hardware snd_rockchip_hardware = {
	.info			= SNDRV_PCM_INFO_MMAP |
				  SNDRV_PCM_INFO_MMAP_VALID |
				  SNDRV_PCM_INFO_PAUSE |
				  SNDRV_PCM_INFO_RESUME |
				  SNDRV_PCM_INFO_INTERLEAVED,
	.period_bytes_min	= 32,
	.period_bytes_max	= SIZE_MAX,
	.periods_min		= 2,
	.periods_max		= 4096,
	.buffer_bytes_max	= SIZE_MAX,
	.fifo_size		= 32,
};

static const struct snd_dmaengine_pcm_config rk_dmaengine_pcm_config = {
	.pcm_hardware = &snd_rockchip_hardware,
	.prepare_slave_config = snd_dmaengine_pcm_prepare_slave_config,
	.prealloc_buffer_size = CONFIG_SND_SOC_ROCKCHIP_PREALLOC_BUFFER_SIZE * 1024,
};

int rockchip_pcm_platform_register(struct device *dev)
{
	return devm_snd_dmaengine_pcm_register(dev, &rk_dmaengine_pcm_config,
		SND_DMAENGINE_PCM_FLAG_COMPAT);
}
EXPORT_SYMBOL_GPL(rockchip_pcm_platform_register);

MODULE_LICENSE("GPL v2");
