// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license. When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2022 Advanced Micro Devices, Inc.
//
// Authors: Ajit Kumar Pandey <AjitKumar.Pandey@amd.com>
//	    Vijendar Mukunda <Vijendar.Mukunda@amd.com>
//

/*
 * Generic Hardware interface for ACP Audio PDM controller
 */

#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>

#include "amd.h"

#define DRV_NAME "acp-pdm"

#define PDM_DMA_STAT		0x10
#define PDM_DMA_INTR_MASK	0x10000
#define PDM_DEC_64		0x2
#define PDM_CLK_FREQ_MASK	0x07
#define PDM_MISC_CTRL_MASK	0x10
#define PDM_ENABLE		0x01
#define PDM_DISABLE		0x00
#define DMA_EN_MASK		0x02
#define DELAY_US		5
#define PDM_TIMEOUT		1000
#define ACP_REGION2_OFFSET	0x02000000

static int acp_dmic_prepare(struct snd_pcm_substream *substream,
			    struct snd_soc_dai *dai)
{
	struct acp_stream *stream = substream->runtime->private_data;
	struct device *dev = dai->component->dev;
	struct acp_dev_data *adata = dev_get_drvdata(dev);
	u32 physical_addr, size_dmic, period_bytes;
	unsigned int dmic_ctrl;

	/* Enable default DMIC clk */
	writel(PDM_CLK_FREQ_MASK, adata->acp_base + ACP_WOV_CLK_CTRL);
	dmic_ctrl = readl(adata->acp_base + ACP_WOV_MISC_CTRL);
	dmic_ctrl |= PDM_MISC_CTRL_MASK;
	writel(dmic_ctrl, adata->acp_base + ACP_WOV_MISC_CTRL);

	period_bytes = frames_to_bytes(substream->runtime,
			substream->runtime->period_size);
	size_dmic = frames_to_bytes(substream->runtime,
			substream->runtime->buffer_size);

	physical_addr = stream->reg_offset + MEM_WINDOW_START;

	/* Init DMIC Ring buffer */
	writel(physical_addr, adata->acp_base + ACP_WOV_RX_RINGBUFADDR);
	writel(size_dmic, adata->acp_base + ACP_WOV_RX_RINGBUFSIZE);
	writel(period_bytes, adata->acp_base + ACP_WOV_RX_INTR_WATERMARK_SIZE);
	writel(0x01, adata->acp_base + ACPAXI2AXI_ATU_CTRL);

	return 0;
}

static int acp_dmic_dai_trigger(struct snd_pcm_substream *substream,
				int cmd, struct snd_soc_dai *dai)
{
	struct device *dev = dai->component->dev;
	struct acp_dev_data *adata = dev_get_drvdata(dev);
	unsigned int dma_enable;
	int ret = 0;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		dma_enable = readl(adata->acp_base + ACP_WOV_PDM_DMA_ENABLE);
		if (!(dma_enable & DMA_EN_MASK)) {
			writel(PDM_ENABLE, adata->acp_base + ACP_WOV_PDM_ENABLE);
			writel(PDM_ENABLE, adata->acp_base + ACP_WOV_PDM_DMA_ENABLE);
		}

		ret = readl_poll_timeout_atomic(adata->acp_base + ACP_WOV_PDM_DMA_ENABLE,
						dma_enable, (dma_enable & DMA_EN_MASK),
						DELAY_US, PDM_TIMEOUT);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		dma_enable = readl(adata->acp_base + ACP_WOV_PDM_DMA_ENABLE);
		if ((dma_enable & DMA_EN_MASK)) {
			writel(PDM_DISABLE, adata->acp_base + ACP_WOV_PDM_ENABLE);
			writel(PDM_DISABLE, adata->acp_base + ACP_WOV_PDM_DMA_ENABLE);

		}

		ret = readl_poll_timeout_atomic(adata->acp_base + ACP_WOV_PDM_DMA_ENABLE,
						dma_enable, !(dma_enable & DMA_EN_MASK),
						DELAY_US, PDM_TIMEOUT);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int acp_dmic_hwparams(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *hwparams, struct snd_soc_dai *dai)
{
	struct device *dev = dai->component->dev;
	struct acp_dev_data *adata = dev_get_drvdata(dev);
	unsigned int channels, ch_mask;

	channels = params_channels(hwparams);
	switch (channels) {
	case 2:
		ch_mask = 0;
		break;
	case 4:
		ch_mask = 1;
		break;
	case 6:
		ch_mask = 2;
		break;
	default:
		dev_err(dev, "Invalid channels %d\n", channels);
		return -EINVAL;
	}

	if (params_format(hwparams) != SNDRV_PCM_FORMAT_S32_LE) {
		dev_err(dai->dev, "Invalid format:%d\n", params_format(hwparams));
		return -EINVAL;
	}

	writel(ch_mask, adata->acp_base + ACP_WOV_PDM_NO_OF_CHANNELS);
	writel(PDM_DEC_64, adata->acp_base + ACP_WOV_PDM_DECIMATION_FACTOR);

	return 0;
}

static int acp_dmic_dai_startup(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct acp_stream *stream = substream->runtime->private_data;
	struct device *dev = dai->component->dev;
	struct acp_dev_data *adata = dev_get_drvdata(dev);
	u32 ext_int_ctrl;

	stream->dai_id = DMIC_INSTANCE;
	stream->irq_bit = BIT(PDM_DMA_STAT);
	stream->pte_offset = ACP_SRAM_PDM_PTE_OFFSET;
	stream->reg_offset = ACP_REGION2_OFFSET;

	/* Enable DMIC Interrupts */
	ext_int_ctrl = readl(adata->acp_base + ACP_EXTERNAL_INTR_CNTL);
	ext_int_ctrl |= PDM_DMA_INTR_MASK;
	writel(ext_int_ctrl, adata->acp_base + ACP_EXTERNAL_INTR_CNTL);

	return 0;
}

static void acp_dmic_dai_shutdown(struct snd_pcm_substream *substream,
				  struct snd_soc_dai *dai)
{
	struct device *dev = dai->component->dev;
	struct acp_dev_data *adata = dev_get_drvdata(dev);
	u32 ext_int_ctrl;

	/* Disable DMIC interrrupts */
	ext_int_ctrl = readl(adata->acp_base + ACP_EXTERNAL_INTR_CNTL);
	ext_int_ctrl |= ~PDM_DMA_INTR_MASK;
	writel(ext_int_ctrl, adata->acp_base + ACP_EXTERNAL_INTR_CNTL);
}

const struct snd_soc_dai_ops acp_dmic_dai_ops = {
	.prepare	= acp_dmic_prepare,
	.hw_params	= acp_dmic_hwparams,
	.trigger	= acp_dmic_dai_trigger,
	.startup	= acp_dmic_dai_startup,
	.shutdown	= acp_dmic_dai_shutdown,
};
EXPORT_SYMBOL_NS_GPL(acp_dmic_dai_ops, SND_SOC_ACP_COMMON);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_ALIAS(DRV_NAME);
