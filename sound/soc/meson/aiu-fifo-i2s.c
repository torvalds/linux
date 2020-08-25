// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2020 BayLibre, SAS.
// Author: Jerome Brunet <jbrunet@baylibre.com>

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>

#include "aiu.h"
#include "aiu-fifo.h"

#define AIU_I2S_SOURCE_DESC_MODE_8CH	BIT(0)
#define AIU_I2S_SOURCE_DESC_MODE_24BIT	BIT(5)
#define AIU_I2S_SOURCE_DESC_MODE_32BIT	BIT(9)
#define AIU_I2S_SOURCE_DESC_MODE_SPLIT	BIT(11)
#define AIU_MEM_I2S_MASKS_IRQ_BLOCK	GENMASK(31, 16)
#define AIU_MEM_I2S_CONTROL_MODE_16BIT	BIT(6)
#define AIU_MEM_I2S_BUF_CNTL_INIT	BIT(0)
#define AIU_RST_SOFT_I2S_FAST		BIT(0)

#define AIU_FIFO_I2S_BLOCK		256

static struct snd_pcm_hardware fifo_i2s_pcm = {
	.info = (SNDRV_PCM_INFO_INTERLEAVED |
		 SNDRV_PCM_INFO_MMAP |
		 SNDRV_PCM_INFO_MMAP_VALID |
		 SNDRV_PCM_INFO_PAUSE),
	.formats = AIU_FORMATS,
	.rate_min = 5512,
	.rate_max = 192000,
	.channels_min = 2,
	.channels_max = 8,
	.period_bytes_min = AIU_FIFO_I2S_BLOCK,
	.period_bytes_max = AIU_FIFO_I2S_BLOCK * USHRT_MAX,
	.periods_min = 2,
	.periods_max = UINT_MAX,

	/* No real justification for this */
	.buffer_bytes_max = 1 * 1024 * 1024,
};

static int aiu_fifo_i2s_trigger(struct snd_pcm_substream *substream, int cmd,
				struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		snd_soc_component_write(component, AIU_RST_SOFT,
					AIU_RST_SOFT_I2S_FAST);
		snd_soc_component_read(component, AIU_I2S_SYNC);
		break;
	}

	return aiu_fifo_trigger(substream, cmd, dai);
}

static int aiu_fifo_i2s_prepare(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	int ret;

	ret = aiu_fifo_prepare(substream, dai);
	if (ret)
		return ret;

	snd_soc_component_update_bits(component,
				      AIU_MEM_I2S_BUF_CNTL,
				      AIU_MEM_I2S_BUF_CNTL_INIT,
				      AIU_MEM_I2S_BUF_CNTL_INIT);
	snd_soc_component_update_bits(component,
				      AIU_MEM_I2S_BUF_CNTL,
				      AIU_MEM_I2S_BUF_CNTL_INIT, 0);

	return 0;
}

static int aiu_fifo_i2s_hw_params(struct snd_pcm_substream *substream,
				  struct snd_pcm_hw_params *params,
				  struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct aiu_fifo *fifo = dai->playback_dma_data;
	unsigned int val;
	int ret;

	ret = aiu_fifo_hw_params(substream, params, dai);
	if (ret)
		return ret;

	switch (params_physical_width(params)) {
	case 16:
		val = AIU_MEM_I2S_CONTROL_MODE_16BIT;
		break;
	case 32:
		val = 0;
		break;
	default:
		dev_err(dai->dev, "Unsupported physical width %u\n",
			params_physical_width(params));
		return -EINVAL;
	}

	snd_soc_component_update_bits(component, AIU_MEM_I2S_CONTROL,
				      AIU_MEM_I2S_CONTROL_MODE_16BIT,
				      val);

	/* Setup the irq periodicity */
	val = params_period_bytes(params) / fifo->fifo_block;
	val = FIELD_PREP(AIU_MEM_I2S_MASKS_IRQ_BLOCK, val);
	snd_soc_component_update_bits(component, AIU_MEM_I2S_MASKS,
				      AIU_MEM_I2S_MASKS_IRQ_BLOCK, val);

	return 0;
}

const struct snd_soc_dai_ops aiu_fifo_i2s_dai_ops = {
	.trigger	= aiu_fifo_i2s_trigger,
	.prepare	= aiu_fifo_i2s_prepare,
	.hw_params	= aiu_fifo_i2s_hw_params,
	.hw_free	= aiu_fifo_hw_free,
	.startup	= aiu_fifo_startup,
	.shutdown	= aiu_fifo_shutdown,
};

int aiu_fifo_i2s_dai_probe(struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct aiu *aiu = snd_soc_component_get_drvdata(component);
	struct aiu_fifo *fifo;
	int ret;

	ret = aiu_fifo_dai_probe(dai);
	if (ret)
		return ret;

	fifo = dai->playback_dma_data;

	fifo->pcm = &fifo_i2s_pcm;
	fifo->mem_offset = AIU_MEM_I2S_START;
	fifo->fifo_block = AIU_FIFO_I2S_BLOCK;
	fifo->pclk = aiu->i2s.clks[PCLK].clk;
	fifo->irq = aiu->i2s.irq;

	return 0;
}
