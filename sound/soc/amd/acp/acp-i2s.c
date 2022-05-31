// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license. When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2021 Advanced Micro Devices, Inc.
//
// Authors: Ajit Kumar Pandey <AjitKumar.Pandey@amd.com>
//

/*
 * Generic Hardware interface for ACP Audio I2S controller
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

#define DRV_NAME "acp_i2s_playcap"

static int acp_i2s_hwparams(struct snd_pcm_substream *substream, struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct device *dev = dai->component->dev;
	struct acp_dev_data *adata;
	u32 val;
	u32 xfer_resolution;
	u32 reg_val;

	adata = snd_soc_dai_get_drvdata(dai);

	/* These values are as per Hardware Spec */
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_U8:
	case SNDRV_PCM_FORMAT_S8:
		xfer_resolution = 0x0;
		break;
	case SNDRV_PCM_FORMAT_S16_LE:
		xfer_resolution = 0x02;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		xfer_resolution = 0x04;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		xfer_resolution = 0x05;
		break;
	default:
		return -EINVAL;
	}

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		switch (dai->driver->id) {
		case I2S_BT_INSTANCE:
			reg_val = ACP_BTTDM_ITER;
			break;
		case I2S_SP_INSTANCE:
			reg_val = ACP_I2STDM_ITER;
			break;
		default:
			dev_err(dev, "Invalid dai id %x\n", dai->driver->id);
			return -EINVAL;
		}
	} else {
		switch (dai->driver->id) {
		case I2S_BT_INSTANCE:
			reg_val = ACP_BTTDM_IRER;
			break;
		case I2S_SP_INSTANCE:
			reg_val = ACP_I2STDM_IRER;
			break;
		default:
			dev_err(dev, "Invalid dai id %x\n", dai->driver->id);
			return -EINVAL;
		}
	}

	val = readl(adata->acp_base + reg_val);
	val &= ~ACP3x_ITER_IRER_SAMP_LEN_MASK;
	val = val | (xfer_resolution  << 3);
	writel(val, adata->acp_base + reg_val);

	return 0;
}

static int acp_i2s_trigger(struct snd_pcm_substream *substream, int cmd, struct snd_soc_dai *dai)
{
	struct acp_stream *stream = substream->runtime->private_data;
	struct device *dev = dai->component->dev;
	struct acp_dev_data *adata = dev_get_drvdata(dev);
	u32 val, period_bytes, reg_val, ier_val, water_val, buf_size, buf_reg;

	period_bytes = frames_to_bytes(substream->runtime, substream->runtime->period_size);
	buf_size = frames_to_bytes(substream->runtime, substream->runtime->buffer_size);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		stream->bytescount = acp_get_byte_count(adata, stream->dai_id, substream->stream);
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
			switch (dai->driver->id) {
			case I2S_BT_INSTANCE:
				water_val = ACP_BT_TX_INTR_WATERMARK_SIZE;
				reg_val = ACP_BTTDM_ITER;
				ier_val = ACP_BTTDM_IER;
				buf_reg = ACP_BT_TX_RINGBUFSIZE;
				break;
			case I2S_SP_INSTANCE:
				water_val = ACP_I2S_TX_INTR_WATERMARK_SIZE;
				reg_val = ACP_I2STDM_ITER;
				ier_val = ACP_I2STDM_IER;
				buf_reg = ACP_I2S_TX_RINGBUFSIZE;
				break;
			default:
				dev_err(dev, "Invalid dai id %x\n", dai->driver->id);
				return -EINVAL;
			}
		} else {
			switch (dai->driver->id) {
			case I2S_BT_INSTANCE:
				water_val = ACP_BT_RX_INTR_WATERMARK_SIZE;
				reg_val = ACP_BTTDM_IRER;
				ier_val = ACP_BTTDM_IER;
				buf_reg = ACP_BT_RX_RINGBUFSIZE;
				break;
			case I2S_SP_INSTANCE:
				water_val = ACP_I2S_RX_INTR_WATERMARK_SIZE;
				reg_val = ACP_I2STDM_IRER;
				ier_val = ACP_I2STDM_IER;
				buf_reg = ACP_I2S_RX_RINGBUFSIZE;
				break;
			default:
				dev_err(dev, "Invalid dai id %x\n", dai->driver->id);
				return -EINVAL;
			}
		}
		writel(period_bytes, adata->acp_base + water_val);
		writel(buf_size, adata->acp_base + buf_reg);
		val = readl(adata->acp_base + reg_val);
		val = val | BIT(0);
		writel(val, adata->acp_base + reg_val);
		writel(1, adata->acp_base + ier_val);
		return 0;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
			switch (dai->driver->id) {
			case I2S_BT_INSTANCE:
				reg_val = ACP_BTTDM_ITER;
				break;
			case I2S_SP_INSTANCE:
				reg_val = ACP_I2STDM_ITER;
				break;
			default:
				dev_err(dev, "Invalid dai id %x\n", dai->driver->id);
				return -EINVAL;
			}

		} else {
			switch (dai->driver->id) {
			case I2S_BT_INSTANCE:
				reg_val = ACP_BTTDM_IRER;
				break;
			case I2S_SP_INSTANCE:
				reg_val = ACP_I2STDM_IRER;
				break;
			default:
				dev_err(dev, "Invalid dai id %x\n", dai->driver->id);
				return -EINVAL;
			}
		}
		val = readl(adata->acp_base + reg_val);
		val = val & ~BIT(0);
		writel(val, adata->acp_base + reg_val);

		if (!(readl(adata->acp_base + ACP_BTTDM_ITER) & BIT(0)) &&
		    !(readl(adata->acp_base + ACP_BTTDM_IRER) & BIT(0)))
			writel(0, adata->acp_base + ACP_BTTDM_IER);
		if (!(readl(adata->acp_base + ACP_I2STDM_ITER) & BIT(0)) &&
		    !(readl(adata->acp_base + ACP_I2STDM_IRER) & BIT(0)))
			writel(0, adata->acp_base + ACP_I2STDM_IER);
		return 0;
	default:
		return -EINVAL;
	}

	return 0;
}

static int acp_i2s_prepare(struct snd_pcm_substream *substream, struct snd_soc_dai *dai)
{
	struct device *dev = dai->component->dev;
	struct acp_dev_data *adata = dev_get_drvdata(dev);
	struct acp_stream *stream = substream->runtime->private_data;
	u32 reg_dma_size = 0, reg_fifo_size = 0, reg_fifo_addr = 0;
	u32 phy_addr = 0, acp_fifo_addr = 0, ext_int_ctrl;
	unsigned int dir = substream->stream;

	switch (dai->driver->id) {
	case I2S_SP_INSTANCE:
		if (dir == SNDRV_PCM_STREAM_PLAYBACK) {
			reg_dma_size = ACP_I2S_TX_DMA_SIZE;
			acp_fifo_addr = ACP_SRAM_PTE_OFFSET +
						SP_PB_FIFO_ADDR_OFFSET;
			reg_fifo_addr =	ACP_I2S_TX_FIFOADDR;
			reg_fifo_size = ACP_I2S_TX_FIFOSIZE;

			phy_addr = I2S_SP_TX_MEM_WINDOW_START + stream->reg_offset;
			writel(phy_addr, adata->acp_base + ACP_I2S_TX_RINGBUFADDR);
		} else {
			reg_dma_size = ACP_I2S_RX_DMA_SIZE;
			acp_fifo_addr = ACP_SRAM_PTE_OFFSET +
						SP_CAPT_FIFO_ADDR_OFFSET;
			reg_fifo_addr = ACP_I2S_RX_FIFOADDR;
			reg_fifo_size = ACP_I2S_RX_FIFOSIZE;
			phy_addr = I2S_SP_RX_MEM_WINDOW_START + stream->reg_offset;
			writel(phy_addr, adata->acp_base + ACP_I2S_RX_RINGBUFADDR);
		}
		break;
	case I2S_BT_INSTANCE:
		if (dir == SNDRV_PCM_STREAM_PLAYBACK) {
			reg_dma_size = ACP_BT_TX_DMA_SIZE;
			acp_fifo_addr = ACP_SRAM_PTE_OFFSET +
						BT_PB_FIFO_ADDR_OFFSET;
			reg_fifo_addr = ACP_BT_TX_FIFOADDR;
			reg_fifo_size = ACP_BT_TX_FIFOSIZE;

			phy_addr = I2S_BT_TX_MEM_WINDOW_START + stream->reg_offset;
			writel(phy_addr, adata->acp_base + ACP_BT_TX_RINGBUFADDR);
		} else {
			reg_dma_size = ACP_BT_RX_DMA_SIZE;
			acp_fifo_addr = ACP_SRAM_PTE_OFFSET +
						BT_CAPT_FIFO_ADDR_OFFSET;
			reg_fifo_addr = ACP_BT_RX_FIFOADDR;
			reg_fifo_size = ACP_BT_RX_FIFOSIZE;

			phy_addr = I2S_BT_TX_MEM_WINDOW_START + stream->reg_offset;
			writel(phy_addr, adata->acp_base + ACP_BT_RX_RINGBUFADDR);
		}
		break;
	default:
		dev_err(dev, "Invalid dai id %x\n", dai->driver->id);
		return -EINVAL;
	}

	writel(DMA_SIZE, adata->acp_base + reg_dma_size);
	writel(acp_fifo_addr, adata->acp_base + reg_fifo_addr);
	writel(FIFO_SIZE, adata->acp_base + reg_fifo_size);

	ext_int_ctrl = readl(adata->acp_base + ACP_EXTERNAL_INTR_CNTL);
	ext_int_ctrl |= BIT(I2S_RX_THRESHOLD) | BIT(BT_RX_THRESHOLD)
			| BIT(I2S_TX_THRESHOLD) | BIT(BT_TX_THRESHOLD);

	writel(ext_int_ctrl, adata->acp_base + ACP_EXTERNAL_INTR_CNTL);

	return 0;
}

static int acp_i2s_startup(struct snd_pcm_substream *substream, struct snd_soc_dai *dai)
{
	struct acp_stream *stream = substream->runtime->private_data;
	struct device *dev = dai->component->dev;
	unsigned int dir = substream->stream;
	unsigned int irq_bit = 0;

	switch (dai->driver->id) {
	case I2S_SP_INSTANCE:
		if (dir == SNDRV_PCM_STREAM_PLAYBACK) {
			irq_bit = BIT(I2S_TX_THRESHOLD);
			stream->pte_offset = ACP_SRAM_SP_PB_PTE_OFFSET;
			stream->fifo_offset = SP_PB_FIFO_ADDR_OFFSET;
		} else {
			irq_bit = BIT(I2S_RX_THRESHOLD);
			stream->pte_offset = ACP_SRAM_SP_CP_PTE_OFFSET;
			stream->fifo_offset = SP_CAPT_FIFO_ADDR_OFFSET;
		}
		break;
	case I2S_BT_INSTANCE:
		if (dir == SNDRV_PCM_STREAM_PLAYBACK) {
			irq_bit = BIT(BT_TX_THRESHOLD);
			stream->pte_offset = ACP_SRAM_BT_PB_PTE_OFFSET;
			stream->fifo_offset = BT_PB_FIFO_ADDR_OFFSET;
		} else {
			irq_bit = BIT(BT_RX_THRESHOLD);
			stream->pte_offset = ACP_SRAM_BT_CP_PTE_OFFSET;
			stream->fifo_offset = BT_CAPT_FIFO_ADDR_OFFSET;
		}
		break;
	default:
		dev_err(dev, "Invalid dai id %x\n", dai->driver->id);
		return -EINVAL;
	}

	/* Save runtime dai configuration in stream */
	stream->id = dai->driver->id + dir;
	stream->dai_id = dai->driver->id;
	stream->irq_bit = irq_bit;

	return 0;
}

const struct snd_soc_dai_ops asoc_acp_cpu_dai_ops = {
	.startup = acp_i2s_startup,
	.hw_params = acp_i2s_hwparams,
	.prepare = acp_i2s_prepare,
	.trigger = acp_i2s_trigger,
};
EXPORT_SYMBOL_NS_GPL(asoc_acp_cpu_dai_ops, SND_SOC_ACP_COMMON);

int asoc_acp_i2s_probe(struct snd_soc_dai *dai)
{
	struct device *dev = dai->component->dev;
	struct acp_dev_data *adata = dev_get_drvdata(dev);
	unsigned int val;

	if (!adata->acp_base) {
		dev_err(dev, "I2S base is NULL\n");
		return -EINVAL;
	}

	val = readl(adata->acp_base + ACP_I2S_PIN_CONFIG);
	if (val != I2S_MODE) {
		dev_err(dev, "I2S Mode not supported val %x\n", val);
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL_NS_GPL(asoc_acp_i2s_probe, SND_SOC_ACP_COMMON);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_ALIAS(DRV_NAME);
