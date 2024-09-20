// SPDX-License-Identifier: GPL-2.0-only

#include <linux/slab.h>
#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/dma/pxa-dma.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/pxa2xx-lib.h>
#include <sound/dmaengine_pcm.h>

static const struct snd_pcm_hardware pxa2xx_pcm_hardware = {
	.info			= SNDRV_PCM_INFO_MMAP |
				  SNDRV_PCM_INFO_MMAP_VALID |
				  SNDRV_PCM_INFO_INTERLEAVED |
				  SNDRV_PCM_INFO_PAUSE |
				  SNDRV_PCM_INFO_RESUME,
	.formats		= SNDRV_PCM_FMTBIT_S16_LE |
				  SNDRV_PCM_FMTBIT_S24_LE |
				  SNDRV_PCM_FMTBIT_S32_LE,
	.period_bytes_min	= 32,
	.period_bytes_max	= 8192 - 32,
	.periods_min		= 1,
	.periods_max		= 256,
	.buffer_bytes_max	= 128 * 1024,
	.fifo_size		= 32,
};

int pxa2xx_pcm_hw_params(struct snd_pcm_substream *substream,
			 struct snd_pcm_hw_params *params)
{
	struct dma_chan *chan = snd_dmaengine_pcm_get_chan(substream);
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_dmaengine_dai_dma_data *dma_params;
	struct dma_slave_config config;
	int ret;

	dma_params = snd_soc_dai_get_dma_data(snd_soc_rtd_to_cpu(rtd, 0), substream);
	if (!dma_params)
		return 0;

	ret = snd_hwparams_to_dma_slave_config(substream, params, &config);
	if (ret)
		return ret;

	snd_dmaengine_pcm_set_config_from_dai_data(substream,
			snd_soc_dai_get_dma_data(snd_soc_rtd_to_cpu(rtd, 0), substream),
			&config);

	ret = dmaengine_slave_config(chan, &config);
	if (ret)
		return ret;

	return 0;
}
EXPORT_SYMBOL(pxa2xx_pcm_hw_params);

int pxa2xx_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	return snd_dmaengine_pcm_trigger(substream, cmd);
}
EXPORT_SYMBOL(pxa2xx_pcm_trigger);

snd_pcm_uframes_t
pxa2xx_pcm_pointer(struct snd_pcm_substream *substream)
{
	return snd_dmaengine_pcm_pointer(substream);
}
EXPORT_SYMBOL(pxa2xx_pcm_pointer);

int pxa2xx_pcm_prepare(struct snd_pcm_substream *substream)
{
	return 0;
}
EXPORT_SYMBOL(pxa2xx_pcm_prepare);

int pxa2xx_pcm_open(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_dmaengine_dai_dma_data *dma_params;
	int ret;

	runtime->hw = pxa2xx_pcm_hardware;

	dma_params = snd_soc_dai_get_dma_data(snd_soc_rtd_to_cpu(rtd, 0), substream);
	if (!dma_params)
		return 0;

	/*
	 * For mysterious reasons (and despite what the manual says)
	 * playback samples are lost if the DMA count is not a multiple
	 * of the DMA burst size.  Let's add a rule to enforce that.
	 */
	ret = snd_pcm_hw_constraint_step(runtime, 0,
		SNDRV_PCM_HW_PARAM_PERIOD_BYTES, 32);
	if (ret)
		return ret;

	ret = snd_pcm_hw_constraint_step(runtime, 0,
		SNDRV_PCM_HW_PARAM_BUFFER_BYTES, 32);
	if (ret)
		return ret;

	ret = snd_pcm_hw_constraint_integer(runtime,
					    SNDRV_PCM_HW_PARAM_PERIODS);
	if (ret < 0)
		return ret;

	return snd_dmaengine_pcm_open(
		substream, dma_request_slave_channel(snd_soc_rtd_to_cpu(rtd, 0)->dev,
						     dma_params->chan_name));
}
EXPORT_SYMBOL(pxa2xx_pcm_open);

int pxa2xx_pcm_close(struct snd_pcm_substream *substream)
{
	return snd_dmaengine_pcm_close_release_chan(substream);
}
EXPORT_SYMBOL(pxa2xx_pcm_close);

int pxa2xx_pcm_preallocate_dma_buffer(struct snd_pcm *pcm)
{
	size_t size = pxa2xx_pcm_hardware.buffer_bytes_max;

	return snd_pcm_set_fixed_buffer_all(pcm, SNDRV_DMA_TYPE_DEV_WC,
					    pcm->card->dev, size);
}
EXPORT_SYMBOL(pxa2xx_pcm_preallocate_dma_buffer);

int pxa2xx_soc_pcm_new(struct snd_soc_component *component,
		       struct snd_soc_pcm_runtime *rtd)
{
	struct snd_card *card = rtd->card->snd_card;
	struct snd_pcm *pcm = rtd->pcm;
	int ret;

	ret = dma_coerce_mask_and_coherent(card->dev, DMA_BIT_MASK(32));
	if (ret)
		return ret;

	return pxa2xx_pcm_preallocate_dma_buffer(pcm);
}
EXPORT_SYMBOL(pxa2xx_soc_pcm_new);

int pxa2xx_soc_pcm_open(struct snd_soc_component *component,
			struct snd_pcm_substream *substream)
{
	return pxa2xx_pcm_open(substream);
}
EXPORT_SYMBOL(pxa2xx_soc_pcm_open);

int pxa2xx_soc_pcm_close(struct snd_soc_component *component,
			 struct snd_pcm_substream *substream)
{
	return pxa2xx_pcm_close(substream);
}
EXPORT_SYMBOL(pxa2xx_soc_pcm_close);

int pxa2xx_soc_pcm_hw_params(struct snd_soc_component *component,
			     struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params)
{
	return pxa2xx_pcm_hw_params(substream, params);
}
EXPORT_SYMBOL(pxa2xx_soc_pcm_hw_params);

int pxa2xx_soc_pcm_prepare(struct snd_soc_component *component,
			   struct snd_pcm_substream *substream)
{
	return pxa2xx_pcm_prepare(substream);
}
EXPORT_SYMBOL(pxa2xx_soc_pcm_prepare);

int pxa2xx_soc_pcm_trigger(struct snd_soc_component *component,
			   struct snd_pcm_substream *substream, int cmd)
{
	return pxa2xx_pcm_trigger(substream, cmd);
}
EXPORT_SYMBOL(pxa2xx_soc_pcm_trigger);

snd_pcm_uframes_t
pxa2xx_soc_pcm_pointer(struct snd_soc_component *component,
		       struct snd_pcm_substream *substream)
{
	return pxa2xx_pcm_pointer(substream);
}
EXPORT_SYMBOL(pxa2xx_soc_pcm_pointer);

MODULE_AUTHOR("Nicolas Pitre");
MODULE_DESCRIPTION("Intel PXA2xx sound library");
MODULE_LICENSE("GPL");
