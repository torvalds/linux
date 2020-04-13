// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2020 BayLibre, SAS.
// Author: Jerome Brunet <jbrunet@baylibre.com>

#include <linux/clk.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>

#include "aiu.h"
#include "aiu-fifo.h"

#define AIU_IEC958_DCU_FF_CTRL_EN		BIT(0)
#define AIU_IEC958_DCU_FF_CTRL_AUTO_DISABLE	BIT(1)
#define AIU_IEC958_DCU_FF_CTRL_IRQ_MODE		GENMASK(3, 2)
#define AIU_IEC958_DCU_FF_CTRL_IRQ_OUT_THD	BIT(2)
#define AIU_IEC958_DCU_FF_CTRL_IRQ_FRAME_READ	BIT(3)
#define AIU_IEC958_DCU_FF_CTRL_SYNC_HEAD_EN	BIT(4)
#define AIU_IEC958_DCU_FF_CTRL_BYTE_SEEK	BIT(5)
#define AIU_IEC958_DCU_FF_CTRL_CONTINUE		BIT(6)
#define AIU_MEM_IEC958_CONTROL_ENDIAN		GENMASK(5, 3)
#define AIU_MEM_IEC958_CONTROL_RD_DDR		BIT(6)
#define AIU_MEM_IEC958_CONTROL_MODE_16BIT	BIT(7)
#define AIU_MEM_IEC958_CONTROL_MODE_LINEAR	BIT(8)
#define AIU_MEM_IEC958_BUF_CNTL_INIT		BIT(0)

#define AIU_FIFO_SPDIF_BLOCK			8

static struct snd_pcm_hardware fifo_spdif_pcm = {
	.info = (SNDRV_PCM_INFO_INTERLEAVED |
		 SNDRV_PCM_INFO_MMAP |
		 SNDRV_PCM_INFO_MMAP_VALID |
		 SNDRV_PCM_INFO_PAUSE),
	.formats = AIU_FORMATS,
	.rate_min = 5512,
	.rate_max = 192000,
	.channels_min = 2,
	.channels_max = 2,
	.period_bytes_min = AIU_FIFO_SPDIF_BLOCK,
	.period_bytes_max = AIU_FIFO_SPDIF_BLOCK * USHRT_MAX,
	.periods_min = 2,
	.periods_max = UINT_MAX,

	/* No real justification for this */
	.buffer_bytes_max = 1 * 1024 * 1024,
};

static void fifo_spdif_dcu_enable(struct snd_soc_component *component,
				  bool enable)
{
	snd_soc_component_update_bits(component, AIU_IEC958_DCU_FF_CTRL,
				      AIU_IEC958_DCU_FF_CTRL_EN,
				      enable ? AIU_IEC958_DCU_FF_CTRL_EN : 0);
}

static int fifo_spdif_trigger(struct snd_pcm_substream *substream, int cmd,
			      struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	int ret;

	ret = aiu_fifo_trigger(substream, cmd, dai);
	if (ret)
		return ret;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		fifo_spdif_dcu_enable(component, true);
		break;
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
	case SNDRV_PCM_TRIGGER_STOP:
		fifo_spdif_dcu_enable(component, false);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int fifo_spdif_prepare(struct snd_pcm_substream *substream,
			      struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	int ret;

	ret = aiu_fifo_prepare(substream, dai);
	if (ret)
		return ret;

	snd_soc_component_update_bits(component,
				      AIU_MEM_IEC958_BUF_CNTL,
				      AIU_MEM_IEC958_BUF_CNTL_INIT,
				      AIU_MEM_IEC958_BUF_CNTL_INIT);
	snd_soc_component_update_bits(component,
				      AIU_MEM_IEC958_BUF_CNTL,
				      AIU_MEM_IEC958_BUF_CNTL_INIT, 0);

	return 0;
}

static int fifo_spdif_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	unsigned int val;
	int ret;

	ret = aiu_fifo_hw_params(substream, params, dai);
	if (ret)
		return ret;

	val = AIU_MEM_IEC958_CONTROL_RD_DDR |
	      AIU_MEM_IEC958_CONTROL_MODE_LINEAR;

	switch (params_physical_width(params)) {
	case 16:
		val |= AIU_MEM_IEC958_CONTROL_MODE_16BIT;
		break;
	case 32:
		break;
	default:
		dev_err(dai->dev, "Unsupported physical width %u\n",
			params_physical_width(params));
		return -EINVAL;
	}

	snd_soc_component_update_bits(component, AIU_MEM_IEC958_CONTROL,
				      AIU_MEM_IEC958_CONTROL_ENDIAN |
				      AIU_MEM_IEC958_CONTROL_RD_DDR |
				      AIU_MEM_IEC958_CONTROL_MODE_LINEAR |
				      AIU_MEM_IEC958_CONTROL_MODE_16BIT,
				      val);

	/* Number bytes read by the FIFO between each IRQ */
	snd_soc_component_write(component, AIU_IEC958_BPF,
				params_period_bytes(params));

	/*
	 * AUTO_DISABLE and SYNC_HEAD are enabled by default but
	 * this should be disabled in PCM (uncompressed) mode
	 */
	snd_soc_component_update_bits(component, AIU_IEC958_DCU_FF_CTRL,
				      AIU_IEC958_DCU_FF_CTRL_AUTO_DISABLE |
				      AIU_IEC958_DCU_FF_CTRL_IRQ_MODE |
				      AIU_IEC958_DCU_FF_CTRL_SYNC_HEAD_EN,
				      AIU_IEC958_DCU_FF_CTRL_IRQ_FRAME_READ);

	return 0;
}

const struct snd_soc_dai_ops aiu_fifo_spdif_dai_ops = {
	.trigger	= fifo_spdif_trigger,
	.prepare	= fifo_spdif_prepare,
	.hw_params	= fifo_spdif_hw_params,
	.hw_free	= aiu_fifo_hw_free,
	.startup	= aiu_fifo_startup,
	.shutdown	= aiu_fifo_shutdown,
};

int aiu_fifo_spdif_dai_probe(struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct aiu *aiu = snd_soc_component_get_drvdata(component);
	struct aiu_fifo *fifo;
	int ret;

	ret = aiu_fifo_dai_probe(dai);
	if (ret)
		return ret;

	fifo = dai->playback_dma_data;

	fifo->pcm = &fifo_spdif_pcm;
	fifo->mem_offset = AIU_MEM_IEC958_START;
	fifo->fifo_block = 1;
	fifo->pclk = aiu->spdif.clks[PCLK].clk;
	fifo->irq = aiu->spdif.irq;

	return 0;
}
