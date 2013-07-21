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

static struct dma_chan *spear_pcm_request_chan(struct snd_soc_pcm_runtime *rtd,
	struct snd_pcm_substream *substream)
{
	struct spear_dma_data *dma_data;

	dma_data = snd_soc_dai_get_dma_data(rtd->cpu_dai, substream);

	return snd_dmaengine_pcm_request_channel(dma_data->filter, dma_data);
}

static const struct snd_dmaengine_pcm_config spear_dmaengine_pcm_config = {
	.pcm_hardware = &spear_pcm_hardware,
	.compat_request_channel = spear_pcm_request_chan,
	.prealloc_buffer_size = 16 * 1024,
};

static int spear_soc_platform_probe(struct platform_device *pdev)
{
	return snd_dmaengine_pcm_register(&pdev->dev,
		&spear_dmaengine_pcm_config,
		SND_DMAENGINE_PCM_FLAG_NO_DT |
		SND_DMAENGINE_PCM_FLAG_COMPAT);
}

static int spear_soc_platform_remove(struct platform_device *pdev)
{
	snd_dmaengine_pcm_unregister(&pdev->dev);
	return 0;
}

static struct platform_driver spear_pcm_driver = {
	.driver = {
		.name = "spear-pcm-audio",
		.owner = THIS_MODULE,
	},

	.probe = spear_soc_platform_probe,
	.remove = spear_soc_platform_remove,
};

module_platform_driver(spear_pcm_driver);

MODULE_AUTHOR("Rajeev Kumar <rajeev-dlh.kumar@st.com>");
MODULE_DESCRIPTION("SPEAr PCM DMA module");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:spear-pcm-audio");
