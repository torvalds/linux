// SPDX-License-Identifier: GPL-2.0+
//
// AMD ALSA SoC PCM Driver
//
// Copyright (C) 2021 Advanced Micro Devices, Inc. All rights reserved.

#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/pm_runtime.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>

#include "acp5x.h"

#define DRV_NAME "acp5x_i2s_dma"

static const struct snd_pcm_hardware acp5x_pcm_hardware_playback = {
	.info = SNDRV_PCM_INFO_INTERLEAVED |
		SNDRV_PCM_INFO_BLOCK_TRANSFER |
		SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_MMAP_VALID |
		SNDRV_PCM_INFO_PAUSE | SNDRV_PCM_INFO_RESUME,
	.formats = SNDRV_PCM_FMTBIT_S16_LE |  SNDRV_PCM_FMTBIT_S8 |
		   SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S32_LE,
	.channels_min = 2,
	.channels_max = 2,
	.rates = SNDRV_PCM_RATE_8000_96000,
	.rate_min = 8000,
	.rate_max = 96000,
	.buffer_bytes_max = PLAYBACK_MAX_NUM_PERIODS * PLAYBACK_MAX_PERIOD_SIZE,
	.period_bytes_min = PLAYBACK_MIN_PERIOD_SIZE,
	.period_bytes_max = PLAYBACK_MAX_PERIOD_SIZE,
	.periods_min = PLAYBACK_MIN_NUM_PERIODS,
	.periods_max = PLAYBACK_MAX_NUM_PERIODS,
};

static const struct snd_pcm_hardware acp5x_pcm_hardware_capture = {
	.info = SNDRV_PCM_INFO_INTERLEAVED |
		SNDRV_PCM_INFO_BLOCK_TRANSFER |
		SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_MMAP_VALID |
		SNDRV_PCM_INFO_PAUSE | SNDRV_PCM_INFO_RESUME,
	.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S8 |
		   SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S32_LE,
	.channels_min = 2,
	.channels_max = 2,
	.rates = SNDRV_PCM_RATE_8000_96000,
	.rate_min = 8000,
	.rate_max = 96000,
	.buffer_bytes_max = CAPTURE_MAX_NUM_PERIODS * CAPTURE_MAX_PERIOD_SIZE,
	.period_bytes_min = CAPTURE_MIN_PERIOD_SIZE,
	.period_bytes_max = CAPTURE_MAX_PERIOD_SIZE,
	.periods_min = CAPTURE_MIN_NUM_PERIODS,
	.periods_max = CAPTURE_MAX_NUM_PERIODS,
};

static irqreturn_t i2s_irq_handler(int irq, void *dev_id)
{
	struct i2s_dev_data *vg_i2s_data;
	u16 irq_flag;
	u32 val;

	vg_i2s_data = dev_id;
	if (!vg_i2s_data)
		return IRQ_NONE;

	irq_flag = 0;
	val = acp_readl(vg_i2s_data->acp5x_base + ACP_EXTERNAL_INTR_STAT);
	if ((val & BIT(HS_TX_THRESHOLD)) && vg_i2s_data->play_stream) {
		acp_writel(BIT(HS_TX_THRESHOLD), vg_i2s_data->acp5x_base +
			   ACP_EXTERNAL_INTR_STAT);
		snd_pcm_period_elapsed(vg_i2s_data->play_stream);
		irq_flag = 1;
	}
	if ((val & BIT(I2S_TX_THRESHOLD)) && vg_i2s_data->i2ssp_play_stream) {
		acp_writel(BIT(I2S_TX_THRESHOLD),
			   vg_i2s_data->acp5x_base + ACP_EXTERNAL_INTR_STAT);
		snd_pcm_period_elapsed(vg_i2s_data->i2ssp_play_stream);
		irq_flag = 1;
	}

	if ((val & BIT(HS_RX_THRESHOLD)) && vg_i2s_data->capture_stream) {
		acp_writel(BIT(HS_RX_THRESHOLD), vg_i2s_data->acp5x_base +
			   ACP_EXTERNAL_INTR_STAT);
		snd_pcm_period_elapsed(vg_i2s_data->capture_stream);
		irq_flag = 1;
	}
	if ((val & BIT(I2S_RX_THRESHOLD)) && vg_i2s_data->i2ssp_capture_stream) {
		acp_writel(BIT(I2S_RX_THRESHOLD),
			   vg_i2s_data->acp5x_base + ACP_EXTERNAL_INTR_STAT);
		snd_pcm_period_elapsed(vg_i2s_data->i2ssp_capture_stream);
		irq_flag = 1;
	}

	if (irq_flag)
		return IRQ_HANDLED;
	else
		return IRQ_NONE;
}

static void config_acp5x_dma(struct i2s_stream_instance *rtd, int direction)
{
	u16 page_idx;
	u32 low, high, val, acp_fifo_addr, reg_fifo_addr;
	u32 reg_dma_size, reg_fifo_size;
	dma_addr_t addr;

	addr = rtd->dma_addr;
	if (direction == SNDRV_PCM_STREAM_PLAYBACK) {
		switch (rtd->i2s_instance) {
		case I2S_HS_INSTANCE:
			val = ACP_SRAM_HS_PB_PTE_OFFSET;
			break;
		case I2S_SP_INSTANCE:
		default:
			val = ACP_SRAM_SP_PB_PTE_OFFSET;
		}
	} else {
		switch (rtd->i2s_instance) {
		case I2S_HS_INSTANCE:
			val = ACP_SRAM_HS_CP_PTE_OFFSET;
			break;
		case I2S_SP_INSTANCE:
		default:
			val = ACP_SRAM_SP_CP_PTE_OFFSET;
		}
	}
	/* Group Enable */
	acp_writel(ACP_SRAM_PTE_OFFSET | BIT(31), rtd->acp5x_base +
		   ACPAXI2AXI_ATU_BASE_ADDR_GRP_1);
	acp_writel(PAGE_SIZE_4K_ENABLE, rtd->acp5x_base +
		   ACPAXI2AXI_ATU_PAGE_SIZE_GRP_1);

	for (page_idx = 0; page_idx < rtd->num_pages; page_idx++) {
		/* Load the low address of page int ACP SRAM through SRBM */
		low = lower_32_bits(addr);
		high = upper_32_bits(addr);

		acp_writel(low, rtd->acp5x_base + ACP_SCRATCH_REG_0 + val);
		high |= BIT(31);
		acp_writel(high, rtd->acp5x_base + ACP_SCRATCH_REG_0 + val + 4);
		/* Move to next physically contiguous page */
		val += 8;
		addr += PAGE_SIZE;
	}

	if (direction == SNDRV_PCM_STREAM_PLAYBACK) {
		switch (rtd->i2s_instance) {
		case I2S_HS_INSTANCE:
			reg_dma_size = ACP_HS_TX_DMA_SIZE;
			acp_fifo_addr = ACP_SRAM_PTE_OFFSET +
					HS_PB_FIFO_ADDR_OFFSET;
			reg_fifo_addr = ACP_HS_TX_FIFOADDR;
			reg_fifo_size = ACP_HS_TX_FIFOSIZE;
			acp_writel(I2S_HS_TX_MEM_WINDOW_START,
				   rtd->acp5x_base + ACP_HS_TX_RINGBUFADDR);
			break;

		case I2S_SP_INSTANCE:
		default:
			reg_dma_size = ACP_I2S_TX_DMA_SIZE;
			acp_fifo_addr = ACP_SRAM_PTE_OFFSET +
					SP_PB_FIFO_ADDR_OFFSET;
			reg_fifo_addr =	ACP_I2S_TX_FIFOADDR;
			reg_fifo_size = ACP_I2S_TX_FIFOSIZE;
			acp_writel(I2S_SP_TX_MEM_WINDOW_START,
				   rtd->acp5x_base + ACP_I2S_TX_RINGBUFADDR);
		}
	} else {
		switch (rtd->i2s_instance) {
		case I2S_HS_INSTANCE:
			reg_dma_size = ACP_HS_RX_DMA_SIZE;
			acp_fifo_addr = ACP_SRAM_PTE_OFFSET +
					HS_CAPT_FIFO_ADDR_OFFSET;
			reg_fifo_addr = ACP_HS_RX_FIFOADDR;
			reg_fifo_size = ACP_HS_RX_FIFOSIZE;
			acp_writel(I2S_HS_RX_MEM_WINDOW_START,
				   rtd->acp5x_base + ACP_HS_RX_RINGBUFADDR);
			break;

		case I2S_SP_INSTANCE:
		default:
			reg_dma_size = ACP_I2S_RX_DMA_SIZE;
			acp_fifo_addr = ACP_SRAM_PTE_OFFSET +
					SP_CAPT_FIFO_ADDR_OFFSET;
			reg_fifo_addr = ACP_I2S_RX_FIFOADDR;
			reg_fifo_size = ACP_I2S_RX_FIFOSIZE;
			acp_writel(I2S_SP_RX_MEM_WINDOW_START,
				   rtd->acp5x_base + ACP_I2S_RX_RINGBUFADDR);
		}
	}
	acp_writel(DMA_SIZE, rtd->acp5x_base + reg_dma_size);
	acp_writel(acp_fifo_addr, rtd->acp5x_base + reg_fifo_addr);
	acp_writel(FIFO_SIZE, rtd->acp5x_base + reg_fifo_size);
	acp_writel(BIT(I2S_RX_THRESHOLD) | BIT(HS_RX_THRESHOLD)
		   | BIT(I2S_TX_THRESHOLD) | BIT(HS_TX_THRESHOLD),
		   rtd->acp5x_base + ACP_EXTERNAL_INTR_CNTL);
}

static int acp5x_dma_open(struct snd_soc_component *component,
			  struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime;
	struct snd_soc_pcm_runtime *prtd;
	struct i2s_dev_data *adata;
	struct i2s_stream_instance *i2s_data;
	int ret;

	runtime = substream->runtime;
	prtd = asoc_substream_to_rtd(substream);
	component = snd_soc_rtdcom_lookup(prtd, DRV_NAME);
	adata = dev_get_drvdata(component->dev);

	i2s_data = kzalloc(sizeof(*i2s_data), GFP_KERNEL);
	if (!i2s_data)
		return -ENOMEM;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		runtime->hw = acp5x_pcm_hardware_playback;
	else
		runtime->hw = acp5x_pcm_hardware_capture;

	ret = snd_pcm_hw_constraint_integer(runtime,
					    SNDRV_PCM_HW_PARAM_PERIODS);
	if (ret < 0) {
		dev_err(component->dev, "set integer constraint failed\n");
		kfree(i2s_data);
		return ret;
	}
	i2s_data->acp5x_base = adata->acp5x_base;
	runtime->private_data = i2s_data;
	return ret;
}

static int acp5x_dma_hw_params(struct snd_soc_component *component,
			       struct snd_pcm_substream *substream,
			       struct snd_pcm_hw_params *params)
{
	struct i2s_stream_instance *rtd;
	struct snd_soc_pcm_runtime *prtd;
	struct snd_soc_card *card;
	struct acp5x_platform_info *pinfo;
	struct i2s_dev_data *adata;
	u64 size;

	prtd = asoc_substream_to_rtd(substream);
	card = prtd->card;
	pinfo = snd_soc_card_get_drvdata(card);
	adata = dev_get_drvdata(component->dev);
	rtd = substream->runtime->private_data;

	if (!rtd)
		return -EINVAL;

	if (pinfo) {
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
			rtd->i2s_instance = pinfo->play_i2s_instance;
			switch (rtd->i2s_instance) {
			case I2S_HS_INSTANCE:
				adata->play_stream = substream;
				break;
			case I2S_SP_INSTANCE:
			default:
				adata->i2ssp_play_stream = substream;
			}
		} else {
			rtd->i2s_instance = pinfo->cap_i2s_instance;
			switch (rtd->i2s_instance) {
			case I2S_HS_INSTANCE:
				adata->capture_stream = substream;
				break;
			case I2S_SP_INSTANCE:
			default:
				adata->i2ssp_capture_stream = substream;
			}
		}
	} else {
		dev_err(component->dev, "pinfo failed\n");
		return -EINVAL;
	}
	size = params_buffer_bytes(params);
	rtd->dma_addr = substream->runtime->dma_addr;
	rtd->num_pages = (PAGE_ALIGN(size) >> PAGE_SHIFT);
	config_acp5x_dma(rtd, substream->stream);
	return 0;
}

static snd_pcm_uframes_t acp5x_dma_pointer(struct snd_soc_component *component,
					   struct snd_pcm_substream *substream)
{
	struct i2s_stream_instance *rtd;
	u32 pos;
	u32 buffersize;
	u64 bytescount;

	rtd = substream->runtime->private_data;
	buffersize = frames_to_bytes(substream->runtime,
				     substream->runtime->buffer_size);
	bytescount = acp_get_byte_count(rtd, substream->stream);
	if (bytescount > rtd->bytescount)
		bytescount -= rtd->bytescount;
	pos = do_div(bytescount, buffersize);
	return bytes_to_frames(substream->runtime, pos);
}

static int acp5x_dma_new(struct snd_soc_component *component,
			 struct snd_soc_pcm_runtime *rtd)
{
	struct device *parent = component->dev->parent;

	snd_pcm_set_managed_buffer_all(rtd->pcm, SNDRV_DMA_TYPE_DEV,
				       parent, MIN_BUFFER, MAX_BUFFER);
	return 0;
}

static int acp5x_dma_close(struct snd_soc_component *component,
			   struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *prtd;
	struct i2s_dev_data *adata;
	struct i2s_stream_instance *ins;

	prtd = asoc_substream_to_rtd(substream);
	component = snd_soc_rtdcom_lookup(prtd, DRV_NAME);
	adata = dev_get_drvdata(component->dev);
	ins = substream->runtime->private_data;
	if (!ins)
		return -EINVAL;
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		switch (ins->i2s_instance) {
		case I2S_HS_INSTANCE:
			adata->play_stream = NULL;
			break;
		case I2S_SP_INSTANCE:
		default:
			adata->i2ssp_play_stream = NULL;
		}
	} else {
		switch (ins->i2s_instance) {
		case I2S_HS_INSTANCE:
			adata->capture_stream = NULL;
			break;
		case I2S_SP_INSTANCE:
		default:
			adata->i2ssp_capture_stream = NULL;
		}
	}
	kfree(ins);
	return 0;
}

static const struct snd_soc_component_driver acp5x_i2s_component = {
	.name		= DRV_NAME,
	.open		= acp5x_dma_open,
	.close		= acp5x_dma_close,
	.hw_params	= acp5x_dma_hw_params,
	.pointer	= acp5x_dma_pointer,
	.pcm_construct	= acp5x_dma_new,
};

static int acp5x_audio_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct i2s_dev_data *adata;
	unsigned int irqflags;
	int status;

	if (!pdev->dev.platform_data) {
		dev_err(&pdev->dev, "platform_data not retrieved\n");
		return -ENODEV;
	}
	irqflags = *((unsigned int *)(pdev->dev.platform_data));

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "IORESOURCE_MEM FAILED\n");
		return -ENODEV;
	}

	adata = devm_kzalloc(&pdev->dev, sizeof(*adata), GFP_KERNEL);
	if (!adata)
		return -ENOMEM;

	adata->acp5x_base = devm_ioremap(&pdev->dev, res->start,
					 resource_size(res));
	if (!adata->acp5x_base)
		return -ENOMEM;

	status = platform_get_irq(pdev, 0);
	if (status < 0)
		return status;
	adata->i2s_irq = status;

	dev_set_drvdata(&pdev->dev, adata);
	status = devm_snd_soc_register_component(&pdev->dev,
						 &acp5x_i2s_component,
						 NULL, 0);
	if (status) {
		dev_err(&pdev->dev, "Fail to register acp i2s component\n");
		return status;
	}
	status = devm_request_irq(&pdev->dev, adata->i2s_irq, i2s_irq_handler,
				  irqflags, "ACP5x_I2S_IRQ", adata);
	if (status) {
		dev_err(&pdev->dev, "ACP5x I2S IRQ request failed\n");
		return status;
	}
	pm_runtime_set_autosuspend_delay(&pdev->dev, 2000);
	pm_runtime_use_autosuspend(&pdev->dev);
	pm_runtime_mark_last_busy(&pdev->dev);
	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);
	return 0;
}

static void acp5x_audio_remove(struct platform_device *pdev)
{
	pm_runtime_disable(&pdev->dev);
}

static int __maybe_unused acp5x_pcm_resume(struct device *dev)
{
	struct i2s_dev_data *adata;
	struct i2s_stream_instance *rtd;
	u32 val;

	adata = dev_get_drvdata(dev);

	if (adata->play_stream && adata->play_stream->runtime) {
		rtd = adata->play_stream->runtime->private_data;
		config_acp5x_dma(rtd, SNDRV_PCM_STREAM_PLAYBACK);
		acp_writel((rtd->xfer_resolution  << 3), rtd->acp5x_base + ACP_HSTDM_ITER);
		if (adata->tdm_mode == TDM_ENABLE) {
			acp_writel(adata->tdm_fmt, adata->acp5x_base + ACP_HSTDM_TXFRMT);
			val = acp_readl(adata->acp5x_base + ACP_HSTDM_ITER);
			acp_writel(val | 0x2, adata->acp5x_base + ACP_HSTDM_ITER);
		}
	}
	if (adata->i2ssp_play_stream && adata->i2ssp_play_stream->runtime) {
		rtd = adata->i2ssp_play_stream->runtime->private_data;
		config_acp5x_dma(rtd, SNDRV_PCM_STREAM_PLAYBACK);
		acp_writel((rtd->xfer_resolution  << 3), rtd->acp5x_base + ACP_I2STDM_ITER);
		if (adata->tdm_mode == TDM_ENABLE) {
			acp_writel(adata->tdm_fmt, adata->acp5x_base + ACP_I2STDM_TXFRMT);
			val = acp_readl(adata->acp5x_base + ACP_I2STDM_ITER);
			acp_writel(val | 0x2, adata->acp5x_base + ACP_I2STDM_ITER);
		}
	}

	if (adata->capture_stream && adata->capture_stream->runtime) {
		rtd = adata->capture_stream->runtime->private_data;
		config_acp5x_dma(rtd, SNDRV_PCM_STREAM_CAPTURE);
		acp_writel((rtd->xfer_resolution  << 3), rtd->acp5x_base + ACP_HSTDM_IRER);
		if (adata->tdm_mode == TDM_ENABLE) {
			acp_writel(adata->tdm_fmt, adata->acp5x_base + ACP_HSTDM_RXFRMT);
			val = acp_readl(adata->acp5x_base + ACP_HSTDM_IRER);
			acp_writel(val | 0x2, adata->acp5x_base + ACP_HSTDM_IRER);
		}
	}
	if (adata->i2ssp_capture_stream && adata->i2ssp_capture_stream->runtime) {
		rtd = adata->i2ssp_capture_stream->runtime->private_data;
		config_acp5x_dma(rtd, SNDRV_PCM_STREAM_CAPTURE);
		acp_writel((rtd->xfer_resolution  << 3), rtd->acp5x_base + ACP_I2STDM_IRER);
		if (adata->tdm_mode == TDM_ENABLE) {
			acp_writel(adata->tdm_fmt, adata->acp5x_base + ACP_I2STDM_RXFRMT);
			val = acp_readl(adata->acp5x_base + ACP_I2STDM_IRER);
			acp_writel(val | 0x2, adata->acp5x_base + ACP_I2STDM_IRER);
		}
	}
	acp_writel(1, adata->acp5x_base + ACP_EXTERNAL_INTR_ENB);
	return 0;
}

static int __maybe_unused acp5x_pcm_suspend(struct device *dev)
{
	struct i2s_dev_data *adata;

	adata = dev_get_drvdata(dev);
	acp_writel(0, adata->acp5x_base + ACP_EXTERNAL_INTR_ENB);
	return 0;
}

static int __maybe_unused acp5x_pcm_runtime_resume(struct device *dev)
{
	struct i2s_dev_data *adata;

	adata = dev_get_drvdata(dev);
	acp_writel(1, adata->acp5x_base + ACP_EXTERNAL_INTR_ENB);
	return 0;
}

static const struct dev_pm_ops acp5x_pm_ops = {
	SET_RUNTIME_PM_OPS(acp5x_pcm_suspend,
			   acp5x_pcm_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(acp5x_pcm_suspend, acp5x_pcm_resume)
};

static struct platform_driver acp5x_dma_driver = {
	.probe = acp5x_audio_probe,
	.remove_new = acp5x_audio_remove,
	.driver = {
		.name = "acp5x_i2s_dma",
		.pm = &acp5x_pm_ops,
	},
};

module_platform_driver(acp5x_dma_driver);

MODULE_AUTHOR("Vijendar.Mukunda@amd.com");
MODULE_DESCRIPTION("AMD ACP 5.x PCM Driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DRV_NAME);
