/*
 * rk_pcm.c  --  ALSA SoC ROCKCHIP PCM Audio Layer Platform driver
 *
 * Driver for rockchip pcm audio
 *
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/dmaengine_pcm.h>

#include "rk_pcm.h"

static const struct snd_pcm_hardware rockchip_pcm_hardware = {
	.info			= SNDRV_PCM_INFO_INTERLEAVED |
				    SNDRV_PCM_INFO_BLOCK_TRANSFER |
				    SNDRV_PCM_INFO_MMAP |
				    SNDRV_PCM_INFO_MMAP_VALID |
				    SNDRV_PCM_INFO_PAUSE |
				    SNDRV_PCM_INFO_RESUME,
	.formats		=   SNDRV_PCM_FMTBIT_S24_LE |
				    SNDRV_PCM_FMTBIT_S20_3LE |
				    SNDRV_PCM_FMTBIT_S16_LE,
	.channels_min		= 2,
	.channels_max		= 8,
	.buffer_bytes_max	= 128*1024,
	.period_bytes_min	= 64,
	.period_bytes_max	= 32*1024,//2048*4,///PAGE_SIZE*2,
	.periods_min		= 3,
	.periods_max		= 128,
	.fifo_size		= 16,
};

static const struct snd_dmaengine_pcm_config rockchip_dmaengine_pcm_config = {
	.pcm_hardware = &rockchip_pcm_hardware,
	.prepare_slave_config = snd_dmaengine_pcm_prepare_slave_config,
	.compat_filter_fn = NULL,
	.prealloc_buffer_size = PAGE_SIZE * 32,
};

int rockchip_pcm_platform_register(struct device *dev)
{
	return snd_dmaengine_pcm_register(dev, &rockchip_dmaengine_pcm_config,
			SND_DMAENGINE_PCM_FLAG_COMPAT|
			SND_DMAENGINE_PCM_FLAG_NO_RESIDUE);
}
EXPORT_SYMBOL_GPL(rockchip_pcm_platform_register);

int rockchip_pcm_platform_unregister(struct device *dev)
{
	snd_dmaengine_pcm_unregister(dev);
	return 0;
}
EXPORT_SYMBOL_GPL(rockchip_pcm_platform_unregister);

/* Module information */
MODULE_AUTHOR("rockchip");
MODULE_DESCRIPTION("ROCKCHIP PCM ASoC Interface");
MODULE_LICENSE("GPL");
