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

static int acp_dmic_prepare(struct snd_pcm_substream *substream,
			    struct snd_soc_dai *dai)
{
	struct acp_stream *stream = substream->runtime->private_data;
	struct device *dev = dai->component->dev;
	struct acp_chip_info *chip;
	u32 physical_addr, size_dmic, period_bytes;
	unsigned int dmic_ctrl;

	chip = dev_get_platdata(dev);
	/* Enable default DMIC clk */
	writel(PDM_CLK_FREQ_MASK, chip->base + ACP_WOV_CLK_CTRL);
	dmic_ctrl = readl(chip->base + ACP_WOV_MISC_CTRL);
	dmic_ctrl |= PDM_MISC_CTRL_MASK;
	writel(dmic_ctrl, chip->base + ACP_WOV_MISC_CTRL);

	period_bytes = frames_to_bytes(substream->runtime,
			substream->runtime->period_size);
	size_dmic = frames_to_bytes(substream->runtime,
			substream->runtime->buffer_size);

	if (chip->acp_rev >= ACP70_PCI_ID)
		physical_addr = ACP7x_DMIC_MEM_WINDOW_START;
	else
		physical_addr = stream->reg_offset + MEM_WINDOW_START;

	/* Init DMIC Ring buffer */
	writel(physical_addr, chip->base + ACP_WOV_RX_RINGBUFADDR);
	writel(size_dmic, chip->base + ACP_WOV_RX_RINGBUFSIZE);
	writel(period_bytes, chip->base + ACP_WOV_RX_INTR_WATERMARK_SIZE);
	writel(0x01, chip->base + ACPAXI2AXI_ATU_CTRL);

	return 0;
}

static int acp_dmic_dai_trigger(struct snd_pcm_substream *substream,
				int cmd, struct snd_soc_dai *dai)
{
	struct device *dev = dai->component->dev;
	struct acp_chip_info *chip = dev_get_platdata(dev);
	unsigned int dma_enable;
	int ret = 0;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		dma_enable = readl(chip->base + ACP_WOV_PDM_DMA_ENABLE);
		if (!(dma_enable & DMA_EN_MASK)) {
			writel(PDM_ENABLE, chip->base + ACP_WOV_PDM_ENABLE);
			writel(PDM_ENABLE, chip->base + ACP_WOV_PDM_DMA_ENABLE);
		}

		ret = readl_poll_timeout_atomic(chip->base + ACP_WOV_PDM_DMA_ENABLE,
						dma_enable, (dma_enable & DMA_EN_MASK),
						DELAY_US, PDM_TIMEOUT);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		dma_enable = readl(chip->base + ACP_WOV_PDM_DMA_ENABLE);
		if ((dma_enable & DMA_EN_MASK)) {
			writel(PDM_DISABLE, chip->base + ACP_WOV_PDM_ENABLE);
			writel(PDM_DISABLE, chip->base + ACP_WOV_PDM_DMA_ENABLE);

		}

		ret = readl_poll_timeout_atomic(chip->base + ACP_WOV_PDM_DMA_ENABLE,
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
	struct acp_chip_info *chip = dev_get_platdata(dev);
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

	chip->ch_mask = ch_mask;
	if (params_format(hwparams) != SNDRV_PCM_FORMAT_S32_LE) {
		dev_err(dai->dev, "Invalid format:%d\n", params_format(hwparams));
		return -EINVAL;
	}

	writel(ch_mask, chip->base + ACP_WOV_PDM_NO_OF_CHANNELS);
	writel(PDM_DEC_64, chip->base + ACP_WOV_PDM_DECIMATION_FACTOR);

	return 0;
}

static int acp_dmic_dai_startup(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct acp_stream *stream = substream->runtime->private_data;
	struct device *dev = dai->component->dev;
	struct acp_chip_info *chip = dev_get_platdata(dev);
	u32 ext_int_ctrl;

	stream->dai_id = DMIC_INSTANCE;
	stream->irq_bit = BIT(PDM_DMA_STAT);
	stream->pte_offset = ACP_SRAM_PDM_PTE_OFFSET;
	stream->reg_offset = ACP_REGION2_OFFSET;

	/* Enable DMIC Interrupts */
	ext_int_ctrl = readl(ACP_EXTERNAL_INTR_CNTL(chip, 0));
	ext_int_ctrl |= PDM_DMA_INTR_MASK;
	writel(ext_int_ctrl, ACP_EXTERNAL_INTR_CNTL(chip, 0));

	return 0;
}

static void acp_dmic_dai_shutdown(struct snd_pcm_substream *substream,
				  struct snd_soc_dai *dai)
{
	struct device *dev = dai->component->dev;
	struct acp_chip_info *chip = dev_get_platdata(dev);
	u32 ext_int_ctrl;

	/* Disable DMIC interrupts */
	ext_int_ctrl = readl(ACP_EXTERNAL_INTR_CNTL(chip, 0));
	ext_int_ctrl &= ~PDM_DMA_INTR_MASK;
	writel(ext_int_ctrl, ACP_EXTERNAL_INTR_CNTL(chip, 0));
}

const struct snd_soc_dai_ops acp_dmic_dai_ops = {
	.prepare	= acp_dmic_prepare,
	.hw_params	= acp_dmic_hwparams,
	.trigger	= acp_dmic_dai_trigger,
	.startup	= acp_dmic_dai_startup,
	.shutdown	= acp_dmic_dai_shutdown,
};
EXPORT_SYMBOL_NS_GPL(acp_dmic_dai_ops, "SND_SOC_ACP_COMMON");

MODULE_DESCRIPTION("AMD ACP Audio PDM controller");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_ALIAS(DRV_NAME);
