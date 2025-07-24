// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license. When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2021 Advanced Micro Devices, Inc.
//
// Authors: Ajit Kumar Pandey <AjitKumar.Pandey@amd.com>

/*
 * Generic interface for ACP audio blck PCM component
 */

#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/io.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>
#include <linux/dma-mapping.h>

#include "amd.h"
#include "acp-mach.h"

#define DRV_NAME "acp_i2s_dma"

static const struct snd_pcm_hardware acp_pcm_hardware_playback = {
	.info = SNDRV_PCM_INFO_INTERLEAVED |
		SNDRV_PCM_INFO_BLOCK_TRANSFER |
		SNDRV_PCM_INFO_BATCH |
		SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_MMAP_VALID |
		SNDRV_PCM_INFO_PAUSE | SNDRV_PCM_INFO_RESUME,
	.formats = SNDRV_PCM_FMTBIT_S16_LE |  SNDRV_PCM_FMTBIT_S8 |
		   SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S24_LE |
		   SNDRV_PCM_FMTBIT_S32_LE,
	.channels_min = 2,
	.channels_max = 8,
	.rates = SNDRV_PCM_RATE_8000_96000,
	.rate_min = 8000,
	.rate_max = 96000,
	.buffer_bytes_max = PLAYBACK_MAX_NUM_PERIODS * PLAYBACK_MAX_PERIOD_SIZE,
	.period_bytes_min = PLAYBACK_MIN_PERIOD_SIZE,
	.period_bytes_max = PLAYBACK_MAX_PERIOD_SIZE,
	.periods_min = PLAYBACK_MIN_NUM_PERIODS,
	.periods_max = PLAYBACK_MAX_NUM_PERIODS,
};

static const struct snd_pcm_hardware acp_pcm_hardware_capture = {
	.info = SNDRV_PCM_INFO_INTERLEAVED |
		SNDRV_PCM_INFO_BLOCK_TRANSFER |
		SNDRV_PCM_INFO_BATCH |
		SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_MMAP_VALID |
		SNDRV_PCM_INFO_PAUSE | SNDRV_PCM_INFO_RESUME,
	.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S8 |
		   SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S24_LE |
		   SNDRV_PCM_FMTBIT_S32_LE,
	.channels_min = 2,
	.channels_max = 2,
	.rates = SNDRV_PCM_RATE_8000_48000,
	.rate_min = 8000,
	.rate_max = 48000,
	.buffer_bytes_max = CAPTURE_MAX_NUM_PERIODS * CAPTURE_MAX_PERIOD_SIZE,
	.period_bytes_min = CAPTURE_MIN_PERIOD_SIZE,
	.period_bytes_max = CAPTURE_MAX_PERIOD_SIZE,
	.periods_min = CAPTURE_MIN_NUM_PERIODS,
	.periods_max = CAPTURE_MAX_NUM_PERIODS,
};

static const struct snd_pcm_hardware acp6x_pcm_hardware_playback = {
	.info = SNDRV_PCM_INFO_INTERLEAVED |
		SNDRV_PCM_INFO_BLOCK_TRANSFER |
		SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_MMAP_VALID |
		SNDRV_PCM_INFO_PAUSE | SNDRV_PCM_INFO_RESUME,
	.formats = SNDRV_PCM_FMTBIT_S16_LE |  SNDRV_PCM_FMTBIT_S8 |
		   SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S24_LE |
		   SNDRV_PCM_FMTBIT_S32_LE,
	.channels_min = 2,
	.channels_max = 32,
	.rates = SNDRV_PCM_RATE_8000_192000,
	.rate_min = 8000,
	.rate_max = 192000,
	.buffer_bytes_max = PLAYBACK_MAX_NUM_PERIODS * PLAYBACK_MAX_PERIOD_SIZE,
	.period_bytes_min = PLAYBACK_MIN_PERIOD_SIZE,
	.period_bytes_max = PLAYBACK_MAX_PERIOD_SIZE,
	.periods_min = PLAYBACK_MIN_NUM_PERIODS,
	.periods_max = PLAYBACK_MAX_NUM_PERIODS,
};

static const struct snd_pcm_hardware acp6x_pcm_hardware_capture = {
	.info = SNDRV_PCM_INFO_INTERLEAVED |
		SNDRV_PCM_INFO_BLOCK_TRANSFER |
		SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_MMAP_VALID |
		SNDRV_PCM_INFO_PAUSE | SNDRV_PCM_INFO_RESUME,
	.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S8 |
		   SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S24_LE |
		   SNDRV_PCM_FMTBIT_S32_LE,
	.channels_min = 2,
	.channels_max = 32,
	.rates = SNDRV_PCM_RATE_8000_192000,
	.rate_min = 8000,
	.rate_max = 192000,
	.buffer_bytes_max = CAPTURE_MAX_NUM_PERIODS * CAPTURE_MAX_PERIOD_SIZE,
	.period_bytes_min = CAPTURE_MIN_PERIOD_SIZE,
	.period_bytes_max = CAPTURE_MAX_PERIOD_SIZE,
	.periods_min = CAPTURE_MIN_NUM_PERIODS,
	.periods_max = CAPTURE_MAX_NUM_PERIODS,
};

void config_pte_for_stream(struct acp_chip_info *chip, struct acp_stream *stream)
{
	struct acp_resource *rsrc = chip->rsrc;
	u32 reg_val;

	reg_val = rsrc->sram_pte_offset;
	stream->reg_offset = 0x02000000;

	writel((reg_val + GRP1_OFFSET) | BIT(31), chip->base + ACPAXI2AXI_ATU_BASE_ADDR_GRP_1);
	writel(PAGE_SIZE_4K_ENABLE,  chip->base + ACPAXI2AXI_ATU_PAGE_SIZE_GRP_1);

	writel((reg_val + GRP2_OFFSET) | BIT(31), chip->base + ACPAXI2AXI_ATU_BASE_ADDR_GRP_2);
	writel(PAGE_SIZE_4K_ENABLE,  chip->base + ACPAXI2AXI_ATU_PAGE_SIZE_GRP_2);

	writel(reg_val | BIT(31), chip->base + ACPAXI2AXI_ATU_BASE_ADDR_GRP_5);
	writel(PAGE_SIZE_4K_ENABLE,  chip->base + ACPAXI2AXI_ATU_PAGE_SIZE_GRP_5);

	writel(0x01, chip->base + ACPAXI2AXI_ATU_CTRL);
}
EXPORT_SYMBOL_NS_GPL(config_pte_for_stream, "SND_SOC_ACP_COMMON");

void config_acp_dma(struct acp_chip_info *chip, struct acp_stream *stream, int size)
{
	struct snd_pcm_substream *substream = stream->substream;
	struct acp_resource *rsrc = chip->rsrc;
	dma_addr_t addr = substream->dma_buffer.addr;
	int num_pages = (PAGE_ALIGN(size) >> PAGE_SHIFT);
	u32 low, high, val;
	u16 page_idx;

	switch (chip->acp_rev) {
	case ACP70_PCI_ID:
	case ACP71_PCI_ID:
	case ACP72_PCI_ID:
		switch (stream->dai_id) {
		case I2S_SP_INSTANCE:
			if (stream->dir == SNDRV_PCM_STREAM_PLAYBACK)
				val = 0x0;
			else
				val = 0x1000;
			break;
		case I2S_BT_INSTANCE:
			if (stream->dir == SNDRV_PCM_STREAM_PLAYBACK)
				val = 0x2000;
			else
				val = 0x3000;
			break;
		case I2S_HS_INSTANCE:
			if (stream->dir == SNDRV_PCM_STREAM_PLAYBACK)
				val = 0x4000;
			else
				val = 0x5000;
			break;
		case DMIC_INSTANCE:
			val = 0x6000;
			break;
		default:
			dev_err(chip->dev, "Invalid dai id %x\n", stream->dai_id);
			return;
		}
		break;
	default:
		val = stream->pte_offset;
		break;
	}

	for (page_idx = 0; page_idx < num_pages; page_idx++) {
		/* Load the low address of page int ACP SRAM through SRBM */
		low = lower_32_bits(addr);
		high = upper_32_bits(addr);
		writel(low, chip->base + rsrc->scratch_reg_offset + val);
		high |= BIT(31);
		writel(high, chip->base + rsrc->scratch_reg_offset + val + 4);

		/* Move to next physically contiguous page */
		val += 8;
		addr += PAGE_SIZE;
	}
}
EXPORT_SYMBOL_NS_GPL(config_acp_dma, "SND_SOC_ACP_COMMON");

static int acp_dma_open(struct snd_soc_component *component, struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct device *dev = component->dev;
	struct acp_chip_info *chip;
	struct acp_stream *stream;
	int ret;

	stream = kzalloc(sizeof(*stream), GFP_KERNEL);
	if (!stream)
		return -ENOMEM;

	stream->substream = substream;
	chip = dev_get_drvdata(dev->parent);
	switch (chip->acp_rev) {
	case ACP63_PCI_ID:
	case ACP70_PCI_ID:
	case ACP71_PCI_ID:
	case ACP72_PCI_ID:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			runtime->hw = acp6x_pcm_hardware_playback;
		else
			runtime->hw = acp6x_pcm_hardware_capture;
		break;
	default:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			runtime->hw = acp_pcm_hardware_playback;
		else
			runtime->hw = acp_pcm_hardware_capture;
		break;
	}

	ret = snd_pcm_hw_constraint_step(runtime, 0, SNDRV_PCM_HW_PARAM_PERIOD_BYTES, DMA_SIZE);
	if (ret) {
		dev_err(component->dev, "set hw constraint HW_PARAM_PERIOD_BYTES failed\n");
		kfree(stream);
		return ret;
	}

	ret = snd_pcm_hw_constraint_step(runtime, 0, SNDRV_PCM_HW_PARAM_BUFFER_BYTES, DMA_SIZE);
	if (ret) {
		dev_err(component->dev, "set hw constraint HW_PARAM_BUFFER_BYTES failed\n");
		kfree(stream);
		return ret;
	}

	ret = snd_pcm_hw_constraint_integer(runtime, SNDRV_PCM_HW_PARAM_PERIODS);
	if (ret < 0) {
		dev_err(component->dev, "set integer constraint failed\n");
		kfree(stream);
		return ret;
	}
	runtime->private_data = stream;

	writel(1, ACP_EXTERNAL_INTR_ENB(chip));

	spin_lock_irq(&chip->acp_lock);
	list_add_tail(&stream->list, &chip->stream_list);
	spin_unlock_irq(&chip->acp_lock);

	return ret;
}

static int acp_dma_hw_params(struct snd_soc_component *component,
			     struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params)
{
	struct device *dev = component->dev;
	struct acp_chip_info *chip = dev_get_drvdata(dev->parent);
	struct acp_stream *stream = substream->runtime->private_data;
	u64 size = params_buffer_bytes(params);

	/* Configure ACP DMA block with params */
	config_pte_for_stream(chip, stream);
	config_acp_dma(chip, stream, size);

	return 0;
}

static snd_pcm_uframes_t acp_dma_pointer(struct snd_soc_component *component,
					 struct snd_pcm_substream *substream)
{
	struct device *dev = component->dev;
	struct acp_chip_info *chip = dev_get_drvdata(dev->parent);
	struct acp_stream *stream = substream->runtime->private_data;
	u32 pos, buffersize;
	u64 bytescount;

	buffersize = frames_to_bytes(substream->runtime,
				     substream->runtime->buffer_size);

	bytescount = acp_get_byte_count(chip, stream->dai_id, substream->stream);

	if (bytescount > stream->bytescount)
		bytescount -= stream->bytescount;

	pos = do_div(bytescount, buffersize);

	return bytes_to_frames(substream->runtime, pos);
}

static int acp_dma_new(struct snd_soc_component *component,
		       struct snd_soc_pcm_runtime *rtd)
{
	struct device *parent = component->dev->parent;

	snd_pcm_set_managed_buffer_all(rtd->pcm, SNDRV_DMA_TYPE_DEV,
				       parent, MIN_BUFFER, MAX_BUFFER);
	return 0;
}

static int acp_dma_close(struct snd_soc_component *component,
			 struct snd_pcm_substream *substream)
{
	struct device *dev = component->dev;
	struct acp_chip_info *chip = dev_get_drvdata(dev->parent);
	struct acp_stream *stream = substream->runtime->private_data;

	/* Remove entry from list */
	spin_lock_irq(&chip->acp_lock);
	list_del(&stream->list);
	spin_unlock_irq(&chip->acp_lock);
	kfree(stream);

	return 0;
}

static const struct snd_soc_component_driver acp_pcm_component = {
	.name			= DRV_NAME,
	.open			= acp_dma_open,
	.close			= acp_dma_close,
	.hw_params		= acp_dma_hw_params,
	.pointer		= acp_dma_pointer,
	.pcm_construct		= acp_dma_new,
	.legacy_dai_naming	= 1,
};

int acp_platform_register(struct device *dev)
{
	struct acp_chip_info *chip;
	struct snd_soc_dai_driver;
	unsigned int status;

	chip = dev_get_platdata(dev);
	if (!chip || !chip->base) {
		dev_err(dev, "ACP chip data is NULL\n");
		return -ENODEV;
	}

	status = devm_snd_soc_register_component(dev, &acp_pcm_component,
						 chip->dai_driver,
						 chip->num_dai);
	if (status) {
		dev_err(dev, "Fail to register acp i2s component\n");
		return status;
	}

	return 0;
}
EXPORT_SYMBOL_NS_GPL(acp_platform_register, "SND_SOC_ACP_COMMON");

int acp_platform_unregister(struct device *dev)
{
	return 0;
}
EXPORT_SYMBOL_NS_GPL(acp_platform_unregister, "SND_SOC_ACP_COMMON");

MODULE_DESCRIPTION("AMD ACP PCM Driver");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_ALIAS(DRV_NAME);
