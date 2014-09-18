/*
 * ALSA PCM interface for ST SPEAr Processors
 *
 * sound/soc/spear/spear_pcm.c
 *
 * Copyright (C) 2012 ST Microelectronics
 * Rajeev Kumar<rajeev-dlh.kumar@st.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/module.h>
#include <linux/dmaengine.h>
#include <linux/platform_device.h>
#include <sound/dmaengine_pcm.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/spear_dma.h>
#include "spear_pcm.h"

static const struct snd_pcm_hardware spear_pcm_hardware = {
	.info = (SNDRV_PCM_INFO_INTERLEAVED | SNDRV_PCM_INFO_BLOCK_TRANSFER |
		 SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_MMAP_VALID |
		 SNDRV_PCM_INFO_PAUSE | SNDRV_PCM_INFO_RESUME),
	.buffer_bytes_max = 16 * 1024, /* max buffer size */
	.period_bytes_min = 2 * 1024, /* 1 msec data minimum period size */
	.period_bytes_max = 2 * 1024, /* maximum period size */
	.periods_min = 1, /* min # periods */
	.periods_max = 8, /* max # of periods */
	.fifo_size = 0, /* fifo size in bytes */
};

static const struct snd_dmaengine_pcm_config spear_dmaengine_pcm_config = {
	.pcm_hardware = &spear_pcm_hardware,
	.prealloc_buffer_size = 16 * 1024,
};

int devm_spear_pcm_platform_register(struct device *dev,
			struct snd_dmaengine_pcm_config *config,
			bool (*filter)(struct dma_chan *chan, void *slave))
{
	*config = spear_dmaengine_pcm_config;
	config->compat_filter_fn = filter;

	return snd_dmaengine_pcm_register(dev, config,
		SND_DMAENGINE_PCM_FLAG_NO_DT |
		SND_DMAENGINE_PCM_FLAG_COMPAT);
}
EXPORT_SYMBOL_GPL(devm_spear_pcm_platform_register);

MODULE_AUTHOR("Rajeev Kumar <rajeev-dlh.kumar@st.com>");
MODULE_DESCRIPTION("SPEAr PCM DMA module");
MODULE_LICENSE("GPL");
