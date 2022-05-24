// SPDX-License-Identifier: GPL-2.0+
//
// AMD ALSA SoC PDM Driver
//
//Copyright 2020 Advanced Micro Devices, Inc.

#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/pm_runtime.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>

#include "rn_acp3x.h"

#define DRV_NAME "acp_rn_pdm_dma"

static const struct snd_pcm_hardware acp_pdm_hardware_capture = {
	.info = SNDRV_PCM_INFO_INTERLEAVED |
		SNDRV_PCM_INFO_BLOCK_TRANSFER |
		SNDRV_PCM_INFO_MMAP |
		SNDRV_PCM_INFO_MMAP_VALID |
		SNDRV_PCM_INFO_PAUSE | SNDRV_PCM_INFO_RESUME,
	.formats = SNDRV_PCM_FMTBIT_S32_LE,
	.channels_min = 2,
	.channels_max = 2,
	.rates = SNDRV_PCM_RATE_48000,
	.rate_min = 48000,
	.rate_max = 48000,
	.buffer_bytes_max = CAPTURE_MAX_NUM_PERIODS * CAPTURE_MAX_PERIOD_SIZE,
	.period_bytes_min = CAPTURE_MIN_PERIOD_SIZE,
	.period_bytes_max = CAPTURE_MAX_PERIOD_SIZE,
	.periods_min = CAPTURE_MIN_NUM_PERIODS,
	.periods_max = CAPTURE_MAX_NUM_PERIODS,
};

static irqreturn_t pdm_irq_handler(int irq, void *dev_id)
{
	struct pdm_dev_data *rn_pdm_data;
	u16 cap_flag;
	u32 val;

	rn_pdm_data = dev_id;
	if (!rn_pdm_data)
		return IRQ_NONE;

	cap_flag = 0;
	val = rn_readl(rn_pdm_data->acp_base + ACP_EXTERNAL_INTR_STAT);
	if ((val & BIT(PDM_DMA_STAT)) && rn_pdm_data->capture_stream) {
		rn_writel(BIT(PDM_DMA_STAT), rn_pdm_data->acp_base +
			  ACP_EXTERNAL_INTR_STAT);
		snd_pcm_period_elapsed(rn_pdm_data->capture_stream);
		cap_flag = 1;
	}

	if (cap_flag)
		return IRQ_HANDLED;
	else
		return IRQ_NONE;
}

static void init_pdm_ring_buffer(u32 physical_addr,
				 u32 buffer_size,
				 u32 watermark_size,
				 void __iomem *acp_base)
{
	rn_writel(physical_addr, acp_base + ACP_WOV_RX_RINGBUFADDR);
	rn_writel(buffer_size, acp_base + ACP_WOV_RX_RINGBUFSIZE);
	rn_writel(watermark_size, acp_base + ACP_WOV_RX_INTR_WATERMARK_SIZE);
	rn_writel(0x01, acp_base + ACPAXI2AXI_ATU_CTRL);
}

static void enable_pdm_clock(void __iomem *acp_base)
{
	u32 pdm_clk_enable, pdm_ctrl;

	pdm_clk_enable = ACP_PDM_CLK_FREQ_MASK;

	rn_writel(pdm_clk_enable, acp_base + ACP_WOV_CLK_CTRL);
	pdm_ctrl = rn_readl(acp_base + ACP_WOV_MISC_CTRL);
	pdm_ctrl |= ACP_WOV_MISC_CTRL_MASK;
	rn_writel(pdm_ctrl, acp_base + ACP_WOV_MISC_CTRL);
}

static void enable_pdm_interrupts(void __iomem *acp_base)
{
	u32 ext_int_ctrl;

	ext_int_ctrl = rn_readl(acp_base + ACP_EXTERNAL_INTR_CNTL);
	ext_int_ctrl |= PDM_DMA_INTR_MASK;
	rn_writel(ext_int_ctrl, acp_base + ACP_EXTERNAL_INTR_CNTL);
}

static void disable_pdm_interrupts(void __iomem *acp_base)
{
	u32 ext_int_ctrl;

	ext_int_ctrl = rn_readl(acp_base + ACP_EXTERNAL_INTR_CNTL);
	ext_int_ctrl |= ~PDM_DMA_INTR_MASK;
	rn_writel(ext_int_ctrl, acp_base + ACP_EXTERNAL_INTR_CNTL);
}

static bool check_pdm_dma_status(void __iomem *acp_base)
{
	bool pdm_dma_status;
	u32 pdm_enable, pdm_dma_enable;

	pdm_dma_status = false;
	pdm_enable = rn_readl(acp_base + ACP_WOV_PDM_ENABLE);
	pdm_dma_enable = rn_readl(acp_base + ACP_WOV_PDM_DMA_ENABLE);
	if ((pdm_enable & ACP_PDM_ENABLE) && (pdm_dma_enable &
	     ACP_PDM_DMA_EN_STATUS))
		pdm_dma_status = true;
	return pdm_dma_status;
}

static int start_pdm_dma(void __iomem *acp_base)
{
	u32 pdm_enable;
	u32 pdm_dma_enable;
	int timeout;

	pdm_enable = 0x01;
	pdm_dma_enable  = 0x01;

	enable_pdm_clock(acp_base);
	rn_writel(pdm_enable, acp_base + ACP_WOV_PDM_ENABLE);
	rn_writel(pdm_dma_enable, acp_base + ACP_WOV_PDM_DMA_ENABLE);
	timeout = 0;
	while (++timeout < ACP_COUNTER) {
		pdm_dma_enable = rn_readl(acp_base + ACP_WOV_PDM_DMA_ENABLE);
		if ((pdm_dma_enable & 0x02) == ACP_PDM_DMA_EN_STATUS)
			return 0;
		udelay(DELAY_US);
	}
	return -ETIMEDOUT;
}

static int stop_pdm_dma(void __iomem *acp_base)
{
	u32 pdm_enable, pdm_dma_enable;
	int timeout;

	pdm_enable = rn_readl(acp_base + ACP_WOV_PDM_ENABLE);
	pdm_dma_enable = rn_readl(acp_base + ACP_WOV_PDM_DMA_ENABLE);
	if (pdm_dma_enable & 0x01) {
		pdm_dma_enable = 0x02;
		rn_writel(pdm_dma_enable, acp_base + ACP_WOV_PDM_DMA_ENABLE);
		timeout = 0;
		while (++timeout < ACP_COUNTER) {
			pdm_dma_enable = rn_readl(acp_base +
						  ACP_WOV_PDM_DMA_ENABLE);
			if ((pdm_dma_enable & 0x02) == 0x00)
				break;
			udelay(DELAY_US);
		}
		if (timeout == ACP_COUNTER)
			return -ETIMEDOUT;
	}
	if (pdm_enable == ACP_PDM_ENABLE) {
		pdm_enable = ACP_PDM_DISABLE;
		rn_writel(pdm_enable, acp_base + ACP_WOV_PDM_ENABLE);
	}
	rn_writel(0x01, acp_base + ACP_WOV_PDM_FIFO_FLUSH);
	return 0;
}

static void config_acp_dma(struct pdm_stream_instance *rtd, int direction)
{
	u16 page_idx;
	u32 low, high, val;
	dma_addr_t addr;

	addr = rtd->dma_addr;
	val = 0;

	/* Group Enable */
	rn_writel(ACP_SRAM_PTE_OFFSET | BIT(31), rtd->acp_base +
		  ACPAXI2AXI_ATU_BASE_ADDR_GRP_1);
	rn_writel(PAGE_SIZE_4K_ENABLE, rtd->acp_base +
		  ACPAXI2AXI_ATU_PAGE_SIZE_GRP_1);

	for (page_idx = 0; page_idx < rtd->num_pages; page_idx++) {
		/* Load the low address of page int ACP SRAM through SRBM */
		low = lower_32_bits(addr);
		high = upper_32_bits(addr);

		rn_writel(low, rtd->acp_base + ACP_SCRATCH_REG_0 + val);
		high |= BIT(31);
		rn_writel(high, rtd->acp_base + ACP_SCRATCH_REG_0 + val + 4);
		val += 8;
		addr += PAGE_SIZE;
	}
}

static int acp_pdm_dma_open(struct snd_soc_component *component,
			    struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime;
	struct pdm_dev_data *adata;
	struct pdm_stream_instance *pdm_data;
	int ret;

	runtime = substream->runtime;
	adata = dev_get_drvdata(component->dev);
	pdm_data = kzalloc(sizeof(*pdm_data), GFP_KERNEL);
	if (!pdm_data)
		return -EINVAL;

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
		runtime->hw = acp_pdm_hardware_capture;

	ret = snd_pcm_hw_constraint_integer(runtime,
					    SNDRV_PCM_HW_PARAM_PERIODS);
	if (ret < 0) {
		dev_err(component->dev, "set integer constraint failed\n");
		kfree(pdm_data);
		return ret;
	}

	enable_pdm_interrupts(adata->acp_base);

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
		adata->capture_stream = substream;

	pdm_data->acp_base = adata->acp_base;
	runtime->private_data = pdm_data;
	return ret;
}

static int acp_pdm_dma_hw_params(struct snd_soc_component *component,
				 struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params)
{
	struct pdm_stream_instance *rtd;
	size_t size, period_bytes;

	rtd = substream->runtime->private_data;
	if (!rtd)
		return -EINVAL;
	size = params_buffer_bytes(params);
	period_bytes = params_period_bytes(params);
	rtd->dma_addr = substream->runtime->dma_addr;
	rtd->num_pages = (PAGE_ALIGN(size) >> PAGE_SHIFT);
	config_acp_dma(rtd, substream->stream);
	init_pdm_ring_buffer(MEM_WINDOW_START, size, period_bytes,
			     rtd->acp_base);
	return 0;
}

static u64 acp_pdm_get_byte_count(struct pdm_stream_instance *rtd,
				  int direction)
{
	union acp_pdm_dma_count byte_count;

	byte_count.bcount.high =
			rn_readl(rtd->acp_base +
				 ACP_WOV_RX_LINEARPOSITIONCNTR_HIGH);
	byte_count.bcount.low =
			rn_readl(rtd->acp_base +
				 ACP_WOV_RX_LINEARPOSITIONCNTR_LOW);
	return byte_count.bytescount;
}

static snd_pcm_uframes_t acp_pdm_dma_pointer(struct snd_soc_component *comp,
					     struct snd_pcm_substream *stream)
{
	struct pdm_stream_instance *rtd;
	u32 pos, buffersize;
	u64 bytescount;

	rtd = stream->runtime->private_data;
	buffersize = frames_to_bytes(stream->runtime,
				     stream->runtime->buffer_size);
	bytescount = acp_pdm_get_byte_count(rtd, stream->stream);
	if (bytescount > rtd->bytescount)
		bytescount -= rtd->bytescount;
	pos = do_div(bytescount, buffersize);
	return bytes_to_frames(stream->runtime, pos);
}

static int acp_pdm_dma_new(struct snd_soc_component *component,
			   struct snd_soc_pcm_runtime *rtd)
{
	struct device *parent = component->dev->parent;

	snd_pcm_set_managed_buffer_all(rtd->pcm, SNDRV_DMA_TYPE_DEV,
				       parent, MIN_BUFFER, MAX_BUFFER);
	return 0;
}

static int acp_pdm_dma_close(struct snd_soc_component *component,
			     struct snd_pcm_substream *substream)
{
	struct pdm_dev_data *adata = dev_get_drvdata(component->dev);

	disable_pdm_interrupts(adata->acp_base);
	adata->capture_stream = NULL;
	return 0;
}

static int acp_pdm_dai_trigger(struct snd_pcm_substream *substream,
			       int cmd, struct snd_soc_dai *dai)
{
	struct pdm_stream_instance *rtd;
	int ret;
	bool pdm_status;
	unsigned int ch_mask;

	rtd = substream->runtime->private_data;
	ret = 0;
	switch (substream->runtime->channels) {
	case TWO_CH:
		ch_mask = 0x00;
		break;
	default:
		return -EINVAL;
	}
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		rn_writel(ch_mask, rtd->acp_base + ACP_WOV_PDM_NO_OF_CHANNELS);
		rn_writel(PDM_DECIMATION_FACTOR, rtd->acp_base +
			  ACP_WOV_PDM_DECIMATION_FACTOR);
		rtd->bytescount = acp_pdm_get_byte_count(rtd,
							 substream->stream);
		pdm_status = check_pdm_dma_status(rtd->acp_base);
		if (!pdm_status)
			ret = start_pdm_dma(rtd->acp_base);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		pdm_status = check_pdm_dma_status(rtd->acp_base);
		if (pdm_status)
			ret = stop_pdm_dma(rtd->acp_base);
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

static const struct snd_soc_dai_ops acp_pdm_dai_ops = {
	.trigger   = acp_pdm_dai_trigger,
};

static struct snd_soc_dai_driver acp_pdm_dai_driver = {
	.capture = {
		.rates = SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_S24_LE |
			   SNDRV_PCM_FMTBIT_S32_LE,
		.channels_min = 2,
		.channels_max = 2,
		.rate_min = 48000,
		.rate_max = 48000,
	},
	.ops = &acp_pdm_dai_ops,
};

static const struct snd_soc_component_driver acp_pdm_component = {
	.name		= DRV_NAME,
	.open		= acp_pdm_dma_open,
	.close		= acp_pdm_dma_close,
	.hw_params	= acp_pdm_dma_hw_params,
	.pointer	= acp_pdm_dma_pointer,
	.pcm_construct	= acp_pdm_dma_new,
};

static int acp_pdm_audio_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct pdm_dev_data *adata;
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

	adata->acp_base = devm_ioremap(&pdev->dev, res->start,
				       resource_size(res));
	if (!adata->acp_base)
		return -ENOMEM;

	status = platform_get_irq(pdev, 0);
	if (status < 0)
		return status;
	adata->pdm_irq = status;

	adata->capture_stream = NULL;

	dev_set_drvdata(&pdev->dev, adata);
	status = devm_snd_soc_register_component(&pdev->dev,
						 &acp_pdm_component,
						 &acp_pdm_dai_driver, 1);
	if (status) {
		dev_err(&pdev->dev, "Fail to register acp pdm dai\n");

		return -ENODEV;
	}
	status = devm_request_irq(&pdev->dev, adata->pdm_irq, pdm_irq_handler,
				  irqflags, "ACP_PDM_IRQ", adata);
	if (status) {
		dev_err(&pdev->dev, "ACP PDM IRQ request failed\n");
		return -ENODEV;
	}
	pm_runtime_set_autosuspend_delay(&pdev->dev, ACP_SUSPEND_DELAY_MS);
	pm_runtime_use_autosuspend(&pdev->dev);
	pm_runtime_enable(&pdev->dev);
	pm_runtime_allow(&pdev->dev);
	return 0;
}

static int acp_pdm_audio_remove(struct platform_device *pdev)
{
	pm_runtime_disable(&pdev->dev);
	return 0;
}

static int acp_pdm_resume(struct device *dev)
{
	struct pdm_dev_data *adata;
	struct snd_pcm_runtime *runtime;
	struct pdm_stream_instance *rtd;
	u32 period_bytes, buffer_len;

	adata = dev_get_drvdata(dev);
	if (adata->capture_stream && adata->capture_stream->runtime) {
		runtime = adata->capture_stream->runtime;
		rtd = runtime->private_data;
		period_bytes = frames_to_bytes(runtime, runtime->period_size);
		buffer_len = frames_to_bytes(runtime, runtime->buffer_size);
		config_acp_dma(rtd, SNDRV_PCM_STREAM_CAPTURE);
		init_pdm_ring_buffer(MEM_WINDOW_START, buffer_len, period_bytes,
				     adata->acp_base);
	}
	enable_pdm_interrupts(adata->acp_base);
	return 0;
}

static int acp_pdm_runtime_suspend(struct device *dev)
{
	struct pdm_dev_data *adata;

	adata = dev_get_drvdata(dev);
	disable_pdm_interrupts(adata->acp_base);

	return 0;
}

static int acp_pdm_runtime_resume(struct device *dev)
{
	struct pdm_dev_data *adata;

	adata = dev_get_drvdata(dev);
	enable_pdm_interrupts(adata->acp_base);
	return 0;
}

static const struct dev_pm_ops acp_pdm_pm_ops = {
	.runtime_suspend = acp_pdm_runtime_suspend,
	.runtime_resume = acp_pdm_runtime_resume,
	.resume = acp_pdm_resume,
};

static struct platform_driver acp_pdm_dma_driver = {
	.probe = acp_pdm_audio_probe,
	.remove = acp_pdm_audio_remove,
	.driver = {
		.name = "acp_rn_pdm_dma",
		.pm = &acp_pdm_pm_ops,
	},
};

module_platform_driver(acp_pdm_dma_driver);

MODULE_AUTHOR("Vijendar.Mukunda@amd.com");
MODULE_DESCRIPTION("AMD ACP3x Renior PDM Driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DRV_NAME);
