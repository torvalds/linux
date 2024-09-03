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
#include <linux/bitfield.h>

#include "amd.h"

#define DRV_NAME "acp_i2s_playcap"
#define	I2S_MASTER_MODE_ENABLE		1
#define	LRCLK_DIV_FIELD			GENMASK(10, 2)
#define	BCLK_DIV_FIELD			GENMASK(23, 11)
#define	ACP63_LRCLK_DIV_FIELD		GENMASK(12, 2)
#define	ACP63_BCLK_DIV_FIELD		GENMASK(23, 13)

static inline void acp_set_i2s_clk(struct acp_dev_data *adata, int dai_id)
{
	u32 i2s_clk_reg, val;
	struct acp_chip_info *chip;
	struct device *dev;

	dev = adata->dev;
	chip = dev_get_platdata(dev);
	switch (dai_id) {
	case I2S_SP_INSTANCE:
		i2s_clk_reg = ACP_I2STDM0_MSTRCLKGEN;
		break;
	case I2S_BT_INSTANCE:
		i2s_clk_reg = ACP_I2STDM1_MSTRCLKGEN;
		break;
	case I2S_HS_INSTANCE:
		i2s_clk_reg = ACP_I2STDM2_MSTRCLKGEN;
		break;
	default:
		i2s_clk_reg = ACP_I2STDM0_MSTRCLKGEN;
		break;
	}

	val  = I2S_MASTER_MODE_ENABLE;
	if (adata->tdm_mode)
		val |= BIT(1);

	switch (chip->acp_rev) {
	case ACP63_DEV:
	case ACP70_DEV:
	case ACP71_DEV:
		val |= FIELD_PREP(ACP63_LRCLK_DIV_FIELD, adata->lrclk_div);
		val |= FIELD_PREP(ACP63_BCLK_DIV_FIELD, adata->bclk_div);
		break;
	default:
		val |= FIELD_PREP(LRCLK_DIV_FIELD, adata->lrclk_div);
		val |= FIELD_PREP(BCLK_DIV_FIELD, adata->bclk_div);
	}
	writel(val, adata->acp_base + i2s_clk_reg);
}

static int acp_i2s_set_fmt(struct snd_soc_dai *cpu_dai,
			   unsigned int fmt)
{
	struct acp_dev_data *adata = snd_soc_dai_get_drvdata(cpu_dai);
	int mode;

	mode = fmt & SND_SOC_DAIFMT_FORMAT_MASK;
	switch (mode) {
	case SND_SOC_DAIFMT_I2S:
		adata->tdm_mode = TDM_DISABLE;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		adata->tdm_mode = TDM_ENABLE;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int acp_i2s_set_tdm_slot(struct snd_soc_dai *dai, u32 tx_mask, u32 rx_mask,
				int slots, int slot_width)
{
	struct device *dev = dai->component->dev;
	struct acp_dev_data *adata = snd_soc_dai_get_drvdata(dai);
	struct acp_chip_info *chip;
	struct acp_stream *stream;
	int slot_len, no_of_slots;

	chip = dev_get_platdata(dev);
	switch (slot_width) {
	case SLOT_WIDTH_8:
		slot_len = 8;
		break;
	case SLOT_WIDTH_16:
		slot_len = 16;
		break;
	case SLOT_WIDTH_24:
		slot_len = 24;
		break;
	case SLOT_WIDTH_32:
		slot_len = 0;
		break;
	default:
		dev_err(dev, "Unsupported bitdepth %d\n", slot_width);
		return -EINVAL;
	}

	switch (chip->acp_rev) {
	case ACP3X_DEV:
	case ACP6X_DEV:
		switch (slots) {
		case 1 ... 7:
			no_of_slots = slots;
			break;
		case 8:
			no_of_slots = 0;
			break;
		default:
			dev_err(dev, "Unsupported slots %d\n", slots);
			return -EINVAL;
		}
		break;
	case ACP63_DEV:
	case ACP70_DEV:
	case ACP71_DEV:
		switch (slots) {
		case 1 ... 31:
			no_of_slots = slots;
			break;
		case 32:
			no_of_slots = 0;
			break;
		default:
			dev_err(dev, "Unsupported slots %d\n", slots);
			return -EINVAL;
		}
		break;
	default:
		dev_err(dev, "Unknown chip revision %d\n", chip->acp_rev);
		return -EINVAL;
	}

	slots = no_of_slots;

	spin_lock_irq(&adata->acp_lock);
	list_for_each_entry(stream, &adata->stream_list, list) {
		switch (chip->acp_rev) {
		case ACP3X_DEV:
		case ACP6X_DEV:
			if (tx_mask && stream->dir == SNDRV_PCM_STREAM_PLAYBACK)
				adata->tdm_tx_fmt[stream->dai_id - 1] =
					FRM_LEN | (slots << 15) | (slot_len << 18);
			else if (rx_mask && stream->dir == SNDRV_PCM_STREAM_CAPTURE)
				adata->tdm_rx_fmt[stream->dai_id - 1] =
					FRM_LEN | (slots << 15) | (slot_len << 18);
			break;
		case ACP63_DEV:
		case ACP70_DEV:
		case ACP71_DEV:
			if (tx_mask && stream->dir == SNDRV_PCM_STREAM_PLAYBACK)
				adata->tdm_tx_fmt[stream->dai_id - 1] =
						FRM_LEN | (slots << 13) | (slot_len << 18);
			else if (rx_mask && stream->dir == SNDRV_PCM_STREAM_CAPTURE)
				adata->tdm_rx_fmt[stream->dai_id - 1] =
						FRM_LEN | (slots << 13) | (slot_len << 18);
			break;
		default:
			dev_err(dev, "Unknown chip revision %d\n", chip->acp_rev);
			return -EINVAL;
		}
	}
	spin_unlock_irq(&adata->acp_lock);
	return 0;
}

static int acp_i2s_hwparams(struct snd_pcm_substream *substream, struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct device *dev = dai->component->dev;
	struct acp_dev_data *adata;
	struct acp_resource *rsrc;
	u32 val;
	u32 xfer_resolution;
	u32 reg_val, fmt_reg, tdm_fmt;
	u32 lrclk_div_val, bclk_div_val;

	adata = snd_soc_dai_get_drvdata(dai);
	rsrc = adata->rsrc;

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
			fmt_reg = ACP_BTTDM_TXFRMT;
			break;
		case I2S_SP_INSTANCE:
			reg_val = ACP_I2STDM_ITER;
			fmt_reg = ACP_I2STDM_TXFRMT;
			break;
		case I2S_HS_INSTANCE:
			reg_val = ACP_HSTDM_ITER;
			fmt_reg = ACP_HSTDM_TXFRMT;
			break;
		default:
			dev_err(dev, "Invalid dai id %x\n", dai->driver->id);
			return -EINVAL;
		}
		adata->xfer_tx_resolution[dai->driver->id - 1] = xfer_resolution;
	} else {
		switch (dai->driver->id) {
		case I2S_BT_INSTANCE:
			reg_val = ACP_BTTDM_IRER;
			fmt_reg = ACP_BTTDM_RXFRMT;
			break;
		case I2S_SP_INSTANCE:
			reg_val = ACP_I2STDM_IRER;
			fmt_reg = ACP_I2STDM_RXFRMT;
			break;
		case I2S_HS_INSTANCE:
			reg_val = ACP_HSTDM_IRER;
			fmt_reg = ACP_HSTDM_RXFRMT;
			break;
		default:
			dev_err(dev, "Invalid dai id %x\n", dai->driver->id);
			return -EINVAL;
		}
		adata->xfer_rx_resolution[dai->driver->id - 1] = xfer_resolution;
	}

	val = readl(adata->acp_base + reg_val);
	val &= ~ACP3x_ITER_IRER_SAMP_LEN_MASK;
	val = val | (xfer_resolution  << 3);
	writel(val, adata->acp_base + reg_val);

	if (adata->tdm_mode) {
		val = readl(adata->acp_base + reg_val);
		writel(val | BIT(1), adata->acp_base + reg_val);
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			tdm_fmt = adata->tdm_tx_fmt[dai->driver->id - 1];
		else
			tdm_fmt = adata->tdm_rx_fmt[dai->driver->id - 1];
		writel(tdm_fmt, adata->acp_base + fmt_reg);
	}

	if (rsrc->soc_mclk) {
		switch (params_format(params)) {
		case SNDRV_PCM_FORMAT_S16_LE:
			switch (params_rate(params)) {
			case 8000:
				bclk_div_val = 768;
				break;
			case 16000:
				bclk_div_val = 384;
				break;
			case 24000:
				bclk_div_val = 256;
				break;
			case 32000:
				bclk_div_val = 192;
				break;
			case 44100:
			case 48000:
				bclk_div_val = 128;
				break;
			case 88200:
			case 96000:
				bclk_div_val = 64;
				break;
			case 192000:
				bclk_div_val = 32;
				break;
			default:
				return -EINVAL;
			}
			lrclk_div_val = 32;
			break;
		case SNDRV_PCM_FORMAT_S32_LE:
			switch (params_rate(params)) {
			case 8000:
				bclk_div_val = 384;
				break;
			case 16000:
				bclk_div_val = 192;
				break;
			case 24000:
				bclk_div_val = 128;
				break;
			case 32000:
				bclk_div_val = 96;
				break;
			case 44100:
			case 48000:
				bclk_div_val = 64;
				break;
			case 88200:
			case 96000:
				bclk_div_val = 32;
				break;
			case 192000:
				bclk_div_val = 16;
				break;
			default:
				return -EINVAL;
			}
			lrclk_div_val = 64;
			break;
		default:
			return -EINVAL;
		}

		switch (params_rate(params)) {
		case 8000:
		case 16000:
		case 24000:
		case 48000:
		case 96000:
		case 192000:
			switch (params_channels(params)) {
			case 2:
				break;
			case 4:
				bclk_div_val = bclk_div_val >> 1;
				lrclk_div_val = lrclk_div_val << 1;
				break;
			case 8:
				bclk_div_val = bclk_div_val >> 2;
				lrclk_div_val = lrclk_div_val << 2;
				break;
			case 16:
				bclk_div_val = bclk_div_val >> 3;
				lrclk_div_val = lrclk_div_val << 3;
				break;
			case 32:
				bclk_div_val = bclk_div_val >> 4;
				lrclk_div_val = lrclk_div_val << 4;
				break;
			default:
				dev_err(dev, "Unsupported channels %#x\n",
					params_channels(params));
			}
			break;
		default:
			break;
		}
		adata->lrclk_div = lrclk_div_val;
		adata->bclk_div = bclk_div_val;
	}
	return 0;
}

static int acp_i2s_trigger(struct snd_pcm_substream *substream, int cmd, struct snd_soc_dai *dai)
{
	struct acp_stream *stream = substream->runtime->private_data;
	struct device *dev = dai->component->dev;
	struct acp_dev_data *adata = dev_get_drvdata(dev);
	struct acp_resource *rsrc = adata->rsrc;
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
				water_val = ACP_BT_TX_INTR_WATERMARK_SIZE(adata);
				reg_val = ACP_BTTDM_ITER;
				ier_val = ACP_BTTDM_IER;
				buf_reg = ACP_BT_TX_RINGBUFSIZE(adata);
				break;
			case I2S_SP_INSTANCE:
				water_val = ACP_I2S_TX_INTR_WATERMARK_SIZE(adata);
				reg_val = ACP_I2STDM_ITER;
				ier_val = ACP_I2STDM_IER;
				buf_reg = ACP_I2S_TX_RINGBUFSIZE(adata);
				break;
			case I2S_HS_INSTANCE:
				water_val = ACP_HS_TX_INTR_WATERMARK_SIZE;
				reg_val = ACP_HSTDM_ITER;
				ier_val = ACP_HSTDM_IER;
				buf_reg = ACP_HS_TX_RINGBUFSIZE;
				break;
			default:
				dev_err(dev, "Invalid dai id %x\n", dai->driver->id);
				return -EINVAL;
			}
		} else {
			switch (dai->driver->id) {
			case I2S_BT_INSTANCE:
				water_val = ACP_BT_RX_INTR_WATERMARK_SIZE(adata);
				reg_val = ACP_BTTDM_IRER;
				ier_val = ACP_BTTDM_IER;
				buf_reg = ACP_BT_RX_RINGBUFSIZE(adata);
				break;
			case I2S_SP_INSTANCE:
				water_val = ACP_I2S_RX_INTR_WATERMARK_SIZE(adata);
				reg_val = ACP_I2STDM_IRER;
				ier_val = ACP_I2STDM_IER;
				buf_reg = ACP_I2S_RX_RINGBUFSIZE(adata);
				break;
			case I2S_HS_INSTANCE:
				water_val = ACP_HS_RX_INTR_WATERMARK_SIZE;
				reg_val = ACP_HSTDM_IRER;
				ier_val = ACP_HSTDM_IER;
				buf_reg = ACP_HS_RX_RINGBUFSIZE;
				break;
			default:
				dev_err(dev, "Invalid dai id %x\n", dai->driver->id);
				return -EINVAL;
			}
		}

		writel(period_bytes, adata->acp_base + water_val);
		writel(buf_size, adata->acp_base + buf_reg);
		if (rsrc->soc_mclk)
			acp_set_i2s_clk(adata, dai->driver->id);
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
			case I2S_HS_INSTANCE:
				reg_val = ACP_HSTDM_ITER;
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
			case I2S_HS_INSTANCE:
				reg_val = ACP_HSTDM_IRER;
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
		if (!(readl(adata->acp_base + ACP_HSTDM_ITER) & BIT(0)) &&
		    !(readl(adata->acp_base + ACP_HSTDM_IRER) & BIT(0)))
			writel(0, adata->acp_base + ACP_HSTDM_IER);
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
	struct acp_chip_info *chip;
	struct acp_resource *rsrc = adata->rsrc;
	struct acp_stream *stream = substream->runtime->private_data;
	u32 reg_dma_size = 0, reg_fifo_size = 0, reg_fifo_addr = 0;
	u32 phy_addr = 0, acp_fifo_addr = 0, ext_int_ctrl;
	unsigned int dir = substream->stream;

	chip = dev_get_platdata(dev);
	switch (dai->driver->id) {
	case I2S_SP_INSTANCE:
		if (dir == SNDRV_PCM_STREAM_PLAYBACK) {
			reg_dma_size = ACP_I2S_TX_DMA_SIZE(adata);
			acp_fifo_addr = rsrc->sram_pte_offset +
						SP_PB_FIFO_ADDR_OFFSET;
			reg_fifo_addr =	ACP_I2S_TX_FIFOADDR(adata);
			reg_fifo_size = ACP_I2S_TX_FIFOSIZE(adata);

			if (chip->acp_rev >= ACP70_DEV)
				phy_addr = ACP7x_I2S_SP_TX_MEM_WINDOW_START;
			else
				phy_addr = I2S_SP_TX_MEM_WINDOW_START + stream->reg_offset;
			writel(phy_addr, adata->acp_base + ACP_I2S_TX_RINGBUFADDR(adata));
		} else {
			reg_dma_size = ACP_I2S_RX_DMA_SIZE(adata);
			acp_fifo_addr = rsrc->sram_pte_offset +
						SP_CAPT_FIFO_ADDR_OFFSET;
			reg_fifo_addr = ACP_I2S_RX_FIFOADDR(adata);
			reg_fifo_size = ACP_I2S_RX_FIFOSIZE(adata);

			if (chip->acp_rev >= ACP70_DEV)
				phy_addr = ACP7x_I2S_SP_RX_MEM_WINDOW_START;
			else
				phy_addr = I2S_SP_RX_MEM_WINDOW_START + stream->reg_offset;
			writel(phy_addr, adata->acp_base + ACP_I2S_RX_RINGBUFADDR(adata));
		}
		break;
	case I2S_BT_INSTANCE:
		if (dir == SNDRV_PCM_STREAM_PLAYBACK) {
			reg_dma_size = ACP_BT_TX_DMA_SIZE(adata);
			acp_fifo_addr = rsrc->sram_pte_offset +
						BT_PB_FIFO_ADDR_OFFSET;
			reg_fifo_addr = ACP_BT_TX_FIFOADDR(adata);
			reg_fifo_size = ACP_BT_TX_FIFOSIZE(adata);

			if (chip->acp_rev >= ACP70_DEV)
				phy_addr = ACP7x_I2S_BT_TX_MEM_WINDOW_START;
			else
				phy_addr = I2S_BT_TX_MEM_WINDOW_START + stream->reg_offset;
			writel(phy_addr, adata->acp_base + ACP_BT_TX_RINGBUFADDR(adata));
		} else {
			reg_dma_size = ACP_BT_RX_DMA_SIZE(adata);
			acp_fifo_addr = rsrc->sram_pte_offset +
						BT_CAPT_FIFO_ADDR_OFFSET;
			reg_fifo_addr = ACP_BT_RX_FIFOADDR(adata);
			reg_fifo_size = ACP_BT_RX_FIFOSIZE(adata);

			if (chip->acp_rev >= ACP70_DEV)
				phy_addr = ACP7x_I2S_BT_RX_MEM_WINDOW_START;
			else
				phy_addr = I2S_BT_TX_MEM_WINDOW_START + stream->reg_offset;
			writel(phy_addr, adata->acp_base + ACP_BT_RX_RINGBUFADDR(adata));
		}
		break;
	case I2S_HS_INSTANCE:
		if (dir == SNDRV_PCM_STREAM_PLAYBACK) {
			reg_dma_size = ACP_HS_TX_DMA_SIZE;
			acp_fifo_addr = rsrc->sram_pte_offset +
				HS_PB_FIFO_ADDR_OFFSET;
			reg_fifo_addr = ACP_HS_TX_FIFOADDR;
			reg_fifo_size = ACP_HS_TX_FIFOSIZE;

			if (chip->acp_rev >= ACP70_DEV)
				phy_addr = ACP7x_I2S_HS_TX_MEM_WINDOW_START;
			else
				phy_addr = I2S_HS_TX_MEM_WINDOW_START + stream->reg_offset;
			writel(phy_addr, adata->acp_base + ACP_HS_TX_RINGBUFADDR);
		} else {
			reg_dma_size = ACP_HS_RX_DMA_SIZE;
			acp_fifo_addr = rsrc->sram_pte_offset +
					HS_CAPT_FIFO_ADDR_OFFSET;
			reg_fifo_addr = ACP_HS_RX_FIFOADDR;
			reg_fifo_size = ACP_HS_RX_FIFOSIZE;

			if (chip->acp_rev >= ACP70_DEV)
				phy_addr = ACP7x_I2S_HS_RX_MEM_WINDOW_START;
			else
				phy_addr = I2S_HS_RX_MEM_WINDOW_START + stream->reg_offset;
			writel(phy_addr, adata->acp_base + ACP_HS_RX_RINGBUFADDR);
		}
		break;
	default:
		dev_err(dev, "Invalid dai id %x\n", dai->driver->id);
		return -EINVAL;
	}

	writel(DMA_SIZE, adata->acp_base + reg_dma_size);
	writel(acp_fifo_addr, adata->acp_base + reg_fifo_addr);
	writel(FIFO_SIZE, adata->acp_base + reg_fifo_size);

	ext_int_ctrl = readl(ACP_EXTERNAL_INTR_CNTL(adata, rsrc->irqp_used));
	ext_int_ctrl |= BIT(I2S_RX_THRESHOLD(rsrc->offset)) |
			BIT(BT_RX_THRESHOLD(rsrc->offset)) |
			BIT(I2S_TX_THRESHOLD(rsrc->offset)) |
			BIT(BT_TX_THRESHOLD(rsrc->offset)) |
			BIT(HS_RX_THRESHOLD(rsrc->offset)) |
			BIT(HS_TX_THRESHOLD(rsrc->offset));

	writel(ext_int_ctrl, ACP_EXTERNAL_INTR_CNTL(adata, rsrc->irqp_used));

	return 0;
}

static int acp_i2s_startup(struct snd_pcm_substream *substream, struct snd_soc_dai *dai)
{
	struct acp_stream *stream = substream->runtime->private_data;
	struct device *dev = dai->component->dev;
	struct acp_dev_data *adata = dev_get_drvdata(dev);
	struct acp_resource *rsrc = adata->rsrc;
	unsigned int dir = substream->stream;
	unsigned int irq_bit = 0;

	switch (dai->driver->id) {
	case I2S_SP_INSTANCE:
		if (dir == SNDRV_PCM_STREAM_PLAYBACK) {
			irq_bit = BIT(I2S_TX_THRESHOLD(rsrc->offset));
			stream->pte_offset = ACP_SRAM_SP_PB_PTE_OFFSET;
			stream->fifo_offset = SP_PB_FIFO_ADDR_OFFSET;
		} else {
			irq_bit = BIT(I2S_RX_THRESHOLD(rsrc->offset));
			stream->pte_offset = ACP_SRAM_SP_CP_PTE_OFFSET;
			stream->fifo_offset = SP_CAPT_FIFO_ADDR_OFFSET;
		}
		break;
	case I2S_BT_INSTANCE:
		if (dir == SNDRV_PCM_STREAM_PLAYBACK) {
			irq_bit = BIT(BT_TX_THRESHOLD(rsrc->offset));
			stream->pte_offset = ACP_SRAM_BT_PB_PTE_OFFSET;
			stream->fifo_offset = BT_PB_FIFO_ADDR_OFFSET;
		} else {
			irq_bit = BIT(BT_RX_THRESHOLD(rsrc->offset));
			stream->pte_offset = ACP_SRAM_BT_CP_PTE_OFFSET;
			stream->fifo_offset = BT_CAPT_FIFO_ADDR_OFFSET;
		}
		break;
	case I2S_HS_INSTANCE:
		if (dir == SNDRV_PCM_STREAM_PLAYBACK) {
			irq_bit = BIT(HS_TX_THRESHOLD(rsrc->offset));
			stream->pte_offset = ACP_SRAM_HS_PB_PTE_OFFSET;
			stream->fifo_offset = HS_PB_FIFO_ADDR_OFFSET;
		} else {
			irq_bit = BIT(HS_RX_THRESHOLD(rsrc->offset));
			stream->pte_offset = ACP_SRAM_HS_CP_PTE_OFFSET;
			stream->fifo_offset = HS_CAPT_FIFO_ADDR_OFFSET;
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
	stream->dir = substream->stream;

	return 0;
}

const struct snd_soc_dai_ops asoc_acp_cpu_dai_ops = {
	.startup	= acp_i2s_startup,
	.hw_params	= acp_i2s_hwparams,
	.prepare	= acp_i2s_prepare,
	.trigger	= acp_i2s_trigger,
	.set_fmt	= acp_i2s_set_fmt,
	.set_tdm_slot	= acp_i2s_set_tdm_slot,
};
EXPORT_SYMBOL_NS_GPL(asoc_acp_cpu_dai_ops, SND_SOC_ACP_COMMON);

MODULE_DESCRIPTION("AMD ACP Audio I2S controller");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_ALIAS(DRV_NAME);
