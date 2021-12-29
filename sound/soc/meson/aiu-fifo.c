// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2020 BayLibre, SAS.
// Author: Jerome Brunet <jbrunet@baylibre.com>

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>

#include "aiu-fifo.h"

#define AIU_MEM_START	0x00
#define AIU_MEM_RD	0x04
#define AIU_MEM_END	0x08
#define AIU_MEM_MASKS	0x0c
#define  AIU_MEM_MASK_CH_RD GENMASK(7, 0)
#define  AIU_MEM_MASK_CH_MEM GENMASK(15, 8)
#define AIU_MEM_CONTROL	0x10
#define  AIU_MEM_CONTROL_INIT BIT(0)
#define  AIU_MEM_CONTROL_FILL_EN BIT(1)
#define  AIU_MEM_CONTROL_EMPTY_EN BIT(2)

static struct snd_soc_dai *aiu_fifo_dai(struct snd_pcm_substream *ss)
{
	struct snd_soc_pcm_runtime *rtd = ss->private_data;

	return asoc_rtd_to_cpu(rtd, 0);
}

snd_pcm_uframes_t aiu_fifo_pointer(struct snd_soc_component *component,
				   struct snd_pcm_substream *substream)
{
	struct snd_soc_dai *dai = aiu_fifo_dai(substream);
	struct aiu_fifo *fifo = dai->playback_dma_data;
	struct snd_pcm_runtime *runtime = substream->runtime;
	unsigned int addr;

	addr = snd_soc_component_read(component, fifo->mem_offset + AIU_MEM_RD);

	return bytes_to_frames(runtime, addr - (unsigned int)runtime->dma_addr);
}

static void aiu_fifo_enable(struct snd_soc_dai *dai, bool enable)
{
	struct snd_soc_component *component = dai->component;
	struct aiu_fifo *fifo = dai->playback_dma_data;
	unsigned int en_mask = (AIU_MEM_CONTROL_FILL_EN |
				AIU_MEM_CONTROL_EMPTY_EN);

	snd_soc_component_update_bits(component,
				      fifo->mem_offset + AIU_MEM_CONTROL,
				      en_mask, enable ? en_mask : 0);
}

int aiu_fifo_trigger(struct snd_pcm_substream *substream, int cmd,
		     struct snd_soc_dai *dai)
{
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		aiu_fifo_enable(dai, true);
		break;
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
	case SNDRV_PCM_TRIGGER_STOP:
		aiu_fifo_enable(dai, false);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

int aiu_fifo_prepare(struct snd_pcm_substream *substream,
		     struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct aiu_fifo *fifo = dai->playback_dma_data;

	snd_soc_component_update_bits(component,
				      fifo->mem_offset + AIU_MEM_CONTROL,
				      AIU_MEM_CONTROL_INIT,
				      AIU_MEM_CONTROL_INIT);
	snd_soc_component_update_bits(component,
				      fifo->mem_offset + AIU_MEM_CONTROL,
				      AIU_MEM_CONTROL_INIT, 0);
	return 0;
}

int aiu_fifo_hw_params(struct snd_pcm_substream *substream,
		       struct snd_pcm_hw_params *params,
		       struct snd_soc_dai *dai)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_component *component = dai->component;
	struct aiu_fifo *fifo = dai->playback_dma_data;
	dma_addr_t end;
	int ret;

	ret = snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(params));
	if (ret < 0)
		return ret;

	/* Setup the fifo boundaries */
	end = runtime->dma_addr + runtime->dma_bytes - fifo->fifo_block;
	snd_soc_component_write(component, fifo->mem_offset + AIU_MEM_START,
				runtime->dma_addr);
	snd_soc_component_write(component, fifo->mem_offset + AIU_MEM_RD,
				runtime->dma_addr);
	snd_soc_component_write(component, fifo->mem_offset + AIU_MEM_END,
				end);

	/* Setup the fifo to read all the memory - no skip */
	snd_soc_component_update_bits(component,
				      fifo->mem_offset + AIU_MEM_MASKS,
				      AIU_MEM_MASK_CH_RD | AIU_MEM_MASK_CH_MEM,
				      FIELD_PREP(AIU_MEM_MASK_CH_RD, 0xff) |
				      FIELD_PREP(AIU_MEM_MASK_CH_MEM, 0xff));

	return 0;
}

int aiu_fifo_hw_free(struct snd_pcm_substream *substream,
		     struct snd_soc_dai *dai)
{
	return snd_pcm_lib_free_pages(substream);
}

static irqreturn_t aiu_fifo_isr(int irq, void *dev_id)
{
	struct snd_pcm_substream *playback = dev_id;

	snd_pcm_period_elapsed(playback);

	return IRQ_HANDLED;
}

int aiu_fifo_startup(struct snd_pcm_substream *substream,
		     struct snd_soc_dai *dai)
{
	struct aiu_fifo *fifo = dai->playback_dma_data;
	int ret;

	snd_soc_set_runtime_hwparams(substream, fifo->pcm);

	/*
	 * Make sure the buffer and period size are multiple of the fifo burst
	 * size
	 */
	ret = snd_pcm_hw_constraint_step(substream->runtime, 0,
					 SNDRV_PCM_HW_PARAM_BUFFER_BYTES,
					 fifo->fifo_block);
	if (ret)
		return ret;

	ret = snd_pcm_hw_constraint_step(substream->runtime, 0,
					 SNDRV_PCM_HW_PARAM_PERIOD_BYTES,
					 fifo->fifo_block);
	if (ret)
		return ret;

	ret = clk_prepare_enable(fifo->pclk);
	if (ret)
		return ret;

	ret = request_irq(fifo->irq, aiu_fifo_isr, 0, dev_name(dai->dev),
			  substream);
	if (ret)
		clk_disable_unprepare(fifo->pclk);

	return ret;
}

void aiu_fifo_shutdown(struct snd_pcm_substream *substream,
		       struct snd_soc_dai *dai)
{
	struct aiu_fifo *fifo = dai->playback_dma_data;

	free_irq(fifo->irq, substream);
	clk_disable_unprepare(fifo->pclk);
}

int aiu_fifo_pcm_new(struct snd_soc_pcm_runtime *rtd,
		     struct snd_soc_dai *dai)
{
	struct snd_pcm_substream *substream =
		rtd->pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream;
	struct snd_card *card = rtd->card->snd_card;
	struct aiu_fifo *fifo = dai->playback_dma_data;
	size_t size = fifo->pcm->buffer_bytes_max;
	int ret;

	ret = dma_coerce_mask_and_coherent(card->dev, DMA_BIT_MASK(32));
	if (ret)
		return ret;

	snd_pcm_lib_preallocate_pages(substream,
				      SNDRV_DMA_TYPE_DEV,
				      card->dev, size, size);

	return 0;
}

int aiu_fifo_dai_probe(struct snd_soc_dai *dai)
{
	struct aiu_fifo *fifo;

	fifo = kzalloc(sizeof(*fifo), GFP_KERNEL);
	if (!fifo)
		return -ENOMEM;

	dai->playback_dma_data = fifo;

	return 0;
}

int aiu_fifo_dai_remove(struct snd_soc_dai *dai)
{
	kfree(dai->playback_dma_data);

	return 0;
}

