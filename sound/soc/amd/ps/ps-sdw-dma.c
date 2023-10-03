// SPDX-License-Identifier: GPL-2.0+
/*
 * AMD ALSA SoC Pink Sardine SoundWire DMA Driver
 *
 * Copyright 2023 Advanced Micro Devices, Inc.
 */

#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>
#include <linux/pm_runtime.h>
#include <linux/soundwire/sdw_amd.h>
#include "acp63.h"

#define DRV_NAME "amd_ps_sdw_dma"

static struct sdw_dma_ring_buf_reg sdw0_dma_ring_buf_reg[ACP63_SDW0_DMA_MAX_STREAMS] = {
	{ACP_AUDIO0_TX_DMA_SIZE, ACP_AUDIO0_TX_FIFOADDR, ACP_AUDIO0_TX_FIFOSIZE,
	 ACP_AUDIO0_TX_RINGBUFSIZE, ACP_AUDIO0_TX_RINGBUFADDR, ACP_AUDIO0_TX_INTR_WATERMARK_SIZE,
	 ACP_AUDIO0_TX_LINEARPOSITIONCNTR_LOW, ACP_AUDIO0_TX_LINEARPOSITIONCNTR_HIGH},
	{ACP_AUDIO1_TX_DMA_SIZE, ACP_AUDIO1_TX_FIFOADDR, ACP_AUDIO1_TX_FIFOSIZE,
	 ACP_AUDIO1_TX_RINGBUFSIZE, ACP_AUDIO1_TX_RINGBUFADDR, ACP_AUDIO1_TX_INTR_WATERMARK_SIZE,
	 ACP_AUDIO1_TX_LINEARPOSITIONCNTR_LOW, ACP_AUDIO1_TX_LINEARPOSITIONCNTR_HIGH},
	{ACP_AUDIO2_TX_DMA_SIZE, ACP_AUDIO2_TX_FIFOADDR, ACP_AUDIO2_TX_FIFOSIZE,
	 ACP_AUDIO2_TX_RINGBUFSIZE, ACP_AUDIO2_TX_RINGBUFADDR, ACP_AUDIO2_TX_INTR_WATERMARK_SIZE,
	 ACP_AUDIO2_TX_LINEARPOSITIONCNTR_LOW, ACP_AUDIO2_TX_LINEARPOSITIONCNTR_HIGH},
	{ACP_AUDIO0_RX_DMA_SIZE, ACP_AUDIO0_RX_FIFOADDR, ACP_AUDIO0_RX_FIFOSIZE,
	 ACP_AUDIO0_RX_RINGBUFSIZE, ACP_AUDIO0_RX_RINGBUFADDR, ACP_AUDIO0_RX_INTR_WATERMARK_SIZE,
	 ACP_AUDIO0_RX_LINEARPOSITIONCNTR_LOW, ACP_AUDIO0_RX_LINEARPOSITIONCNTR_HIGH},
	{ACP_AUDIO1_RX_DMA_SIZE, ACP_AUDIO1_RX_FIFOADDR, ACP_AUDIO1_RX_FIFOSIZE,
	 ACP_AUDIO1_RX_RINGBUFSIZE, ACP_AUDIO1_RX_RINGBUFADDR, ACP_AUDIO1_RX_INTR_WATERMARK_SIZE,
	 ACP_AUDIO1_RX_LINEARPOSITIONCNTR_LOW, ACP_AUDIO1_RX_LINEARPOSITIONCNTR_HIGH},
	{ACP_AUDIO2_RX_DMA_SIZE, ACP_AUDIO2_RX_FIFOADDR, ACP_AUDIO2_RX_FIFOSIZE,
	 ACP_AUDIO2_RX_RINGBUFSIZE, ACP_AUDIO2_RX_RINGBUFADDR, ACP_AUDIO2_RX_INTR_WATERMARK_SIZE,
	 ACP_AUDIO2_RX_LINEARPOSITIONCNTR_LOW, ACP_AUDIO2_RX_LINEARPOSITIONCNTR_HIGH}
};

/*
 * SDW1 instance supports one TX stream and one RX stream.
 * For TX/RX streams DMA registers programming for SDW1 instance, it uses ACP_P1_AUDIO1 register
 * set as per hardware register documentation
 */
static struct sdw_dma_ring_buf_reg sdw1_dma_ring_buf_reg[ACP63_SDW1_DMA_MAX_STREAMS] =  {
	{ACP_P1_AUDIO1_TX_DMA_SIZE, ACP_P1_AUDIO1_TX_FIFOADDR, ACP_P1_AUDIO1_TX_FIFOSIZE,
	 ACP_P1_AUDIO1_TX_RINGBUFSIZE, ACP_P1_AUDIO1_TX_RINGBUFADDR,
	 ACP_P1_AUDIO1_TX_INTR_WATERMARK_SIZE,
	 ACP_P1_AUDIO1_TX_LINEARPOSITIONCNTR_LOW, ACP_P1_AUDIO1_TX_LINEARPOSITIONCNTR_HIGH},
	{ACP_P1_AUDIO1_RX_DMA_SIZE, ACP_P1_AUDIO1_RX_FIFOADDR, ACP_P1_AUDIO1_RX_FIFOSIZE,
	 ACP_P1_AUDIO1_RX_RINGBUFSIZE, ACP_P1_AUDIO1_RX_RINGBUFADDR,
	 ACP_P1_AUDIO1_RX_INTR_WATERMARK_SIZE,
	 ACP_P1_AUDIO1_RX_LINEARPOSITIONCNTR_LOW, ACP_P1_AUDIO1_RX_LINEARPOSITIONCNTR_HIGH},
};

static u32 sdw0_dma_enable_reg[ACP63_SDW0_DMA_MAX_STREAMS] = {
	ACP_SW0_AUDIO0_TX_EN,
	ACP_SW0_AUDIO1_TX_EN,
	ACP_SW0_AUDIO2_TX_EN,
	ACP_SW0_AUDIO0_RX_EN,
	ACP_SW0_AUDIO1_RX_EN,
	ACP_SW0_AUDIO2_RX_EN,
};

/*
 * SDW1 instance supports one TX stream and one RX stream.
 * For TX/RX streams DMA enable register programming for SDW1 instance,
 * it uses ACP_SW1_AUDIO1_TX_EN and ACP_SW1_AUDIO1_RX_EN registers
 * as per hardware register documentation.
 */
static u32 sdw1_dma_enable_reg[ACP63_SDW1_DMA_MAX_STREAMS] = {
	ACP_SW1_AUDIO1_TX_EN,
	ACP_SW1_AUDIO1_RX_EN,
};

static const struct snd_pcm_hardware acp63_sdw_hardware_playback = {
	.info = SNDRV_PCM_INFO_INTERLEAVED |
		SNDRV_PCM_INFO_BLOCK_TRANSFER |
		SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_MMAP_VALID |
		SNDRV_PCM_INFO_PAUSE | SNDRV_PCM_INFO_RESUME,
	.formats = SNDRV_PCM_FMTBIT_S16_LE |  SNDRV_PCM_FMTBIT_S8 |
		   SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE,
	.channels_min = 2,
	.channels_max = 2,
	.rates = SNDRV_PCM_RATE_48000,
	.rate_min = 48000,
	.rate_max = 48000,
	.buffer_bytes_max = SDW_PLAYBACK_MAX_NUM_PERIODS * SDW_PLAYBACK_MAX_PERIOD_SIZE,
	.period_bytes_min = SDW_PLAYBACK_MIN_PERIOD_SIZE,
	.period_bytes_max = SDW_PLAYBACK_MAX_PERIOD_SIZE,
	.periods_min = SDW_PLAYBACK_MIN_NUM_PERIODS,
	.periods_max = SDW_PLAYBACK_MAX_NUM_PERIODS,
};

static const struct snd_pcm_hardware acp63_sdw_hardware_capture = {
	.info = SNDRV_PCM_INFO_INTERLEAVED |
		SNDRV_PCM_INFO_BLOCK_TRANSFER |
		SNDRV_PCM_INFO_MMAP |
		SNDRV_PCM_INFO_MMAP_VALID |
		SNDRV_PCM_INFO_PAUSE | SNDRV_PCM_INFO_RESUME,
	.formats = SNDRV_PCM_FMTBIT_S16_LE |  SNDRV_PCM_FMTBIT_S8 |
		   SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE,
	.channels_min = 2,
	.channels_max = 2,
	.rates = SNDRV_PCM_RATE_48000,
	.rate_min = 48000,
	.rate_max = 48000,
	.buffer_bytes_max = SDW_CAPTURE_MAX_NUM_PERIODS * SDW_CAPTURE_MAX_PERIOD_SIZE,
	.period_bytes_min = SDW_CAPTURE_MIN_PERIOD_SIZE,
	.period_bytes_max = SDW_CAPTURE_MAX_PERIOD_SIZE,
	.periods_min = SDW_CAPTURE_MIN_NUM_PERIODS,
	.periods_max = SDW_CAPTURE_MAX_NUM_PERIODS,
};

static void acp63_enable_disable_sdw_dma_interrupts(void __iomem *acp_base, bool enable)
{
	u32 ext_intr_cntl, ext_intr_cntl1;
	u32 irq_mask = ACP_SDW_DMA_IRQ_MASK;
	u32 irq_mask1 = ACP_P1_SDW_DMA_IRQ_MASK;

	if (enable) {
		ext_intr_cntl = readl(acp_base + ACP_EXTERNAL_INTR_CNTL);
		ext_intr_cntl |= irq_mask;
		writel(ext_intr_cntl, acp_base + ACP_EXTERNAL_INTR_CNTL);
		ext_intr_cntl1 = readl(acp_base + ACP_EXTERNAL_INTR_CNTL1);
		ext_intr_cntl1 |= irq_mask1;
		writel(ext_intr_cntl1, acp_base + ACP_EXTERNAL_INTR_CNTL1);
	} else {
		ext_intr_cntl = readl(acp_base + ACP_EXTERNAL_INTR_CNTL);
		ext_intr_cntl &= ~irq_mask;
		writel(ext_intr_cntl, acp_base + ACP_EXTERNAL_INTR_CNTL);
		ext_intr_cntl1 = readl(acp_base + ACP_EXTERNAL_INTR_CNTL1);
		ext_intr_cntl1 &= ~irq_mask1;
		writel(ext_intr_cntl1, acp_base + ACP_EXTERNAL_INTR_CNTL1);
	}
}

static void acp63_config_dma(struct acp_sdw_dma_stream *stream, void __iomem *acp_base,
			     u32 stream_id)
{
	u16 page_idx;
	u32 low, high, val;
	u32 sdw_dma_pte_offset;
	dma_addr_t addr;

	addr = stream->dma_addr;
	sdw_dma_pte_offset = SDW_PTE_OFFSET(stream->instance);
	val = sdw_dma_pte_offset + (stream_id * ACP_SDW_PTE_OFFSET);

	/* Group Enable */
	writel(ACP_SDW_SRAM_PTE_OFFSET | BIT(31), acp_base + ACPAXI2AXI_ATU_BASE_ADDR_GRP_2);
	writel(PAGE_SIZE_4K_ENABLE, acp_base + ACPAXI2AXI_ATU_PAGE_SIZE_GRP_2);
	for (page_idx = 0; page_idx < stream->num_pages; page_idx++) {
		/* Load the low address of page int ACP SRAM through SRBM */
		low = lower_32_bits(addr);
		high = upper_32_bits(addr);

		writel(low, acp_base + ACP_SCRATCH_REG_0 + val);
		high |= BIT(31);
		writel(high, acp_base + ACP_SCRATCH_REG_0 + val + 4);
		val += 8;
		addr += PAGE_SIZE;
	}
	writel(0x1, acp_base + ACPAXI2AXI_ATU_CTRL);
}

static int acp63_configure_sdw_ringbuffer(void __iomem *acp_base, u32 stream_id, u32 size,
					  u32 manager_instance)
{
	u32 reg_dma_size;
	u32 reg_fifo_addr;
	u32 reg_fifo_size;
	u32 reg_ring_buf_size;
	u32 reg_ring_buf_addr;
	u32 sdw_fifo_addr;
	u32 sdw_fifo_offset;
	u32 sdw_ring_buf_addr;
	u32 sdw_ring_buf_size;
	u32 sdw_mem_window_offset;

	switch (manager_instance) {
	case ACP_SDW0:
		reg_dma_size = sdw0_dma_ring_buf_reg[stream_id].reg_dma_size;
		reg_fifo_addr =	sdw0_dma_ring_buf_reg[stream_id].reg_fifo_addr;
		reg_fifo_size = sdw0_dma_ring_buf_reg[stream_id].reg_fifo_size;
		reg_ring_buf_size = sdw0_dma_ring_buf_reg[stream_id].reg_ring_buf_size;
		reg_ring_buf_addr = sdw0_dma_ring_buf_reg[stream_id].reg_ring_buf_addr;
		break;
	case ACP_SDW1:
		reg_dma_size = sdw1_dma_ring_buf_reg[stream_id].reg_dma_size;
		reg_fifo_addr =	sdw1_dma_ring_buf_reg[stream_id].reg_fifo_addr;
		reg_fifo_size = sdw1_dma_ring_buf_reg[stream_id].reg_fifo_size;
		reg_ring_buf_size = sdw1_dma_ring_buf_reg[stream_id].reg_ring_buf_size;
		reg_ring_buf_addr = sdw1_dma_ring_buf_reg[stream_id].reg_ring_buf_addr;
		break;
	default:
		return -EINVAL;
	}
	sdw_fifo_offset = ACP_SDW_FIFO_OFFSET(manager_instance);
	sdw_mem_window_offset = SDW_MEM_WINDOW_START(manager_instance);
	sdw_fifo_addr = sdw_fifo_offset + (stream_id * SDW_FIFO_OFFSET);
	sdw_ring_buf_addr = sdw_mem_window_offset + (stream_id * ACP_SDW_RING_BUFF_ADDR_OFFSET);
	sdw_ring_buf_size = size;
	writel(sdw_ring_buf_size, acp_base + reg_ring_buf_size);
	writel(sdw_ring_buf_addr, acp_base + reg_ring_buf_addr);
	writel(sdw_fifo_addr, acp_base + reg_fifo_addr);
	writel(SDW_DMA_SIZE, acp_base + reg_dma_size);
	writel(SDW_FIFO_SIZE, acp_base + reg_fifo_size);
	return 0;
}

static int acp63_sdw_dma_open(struct snd_soc_component *component,
			      struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime;
	struct acp_sdw_dma_stream *stream;
	struct snd_soc_dai *cpu_dai;
	struct amd_sdw_manager *amd_manager;
	struct snd_soc_pcm_runtime *prtd = substream->private_data;
	int ret;

	runtime = substream->runtime;
	cpu_dai = asoc_rtd_to_cpu(prtd, 0);
	amd_manager = snd_soc_dai_get_drvdata(cpu_dai);
	stream = kzalloc(sizeof(*stream), GFP_KERNEL);
	if (!stream)
		return -ENOMEM;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		runtime->hw = acp63_sdw_hardware_playback;
	else
		runtime->hw = acp63_sdw_hardware_capture;
	ret = snd_pcm_hw_constraint_integer(runtime,
					    SNDRV_PCM_HW_PARAM_PERIODS);
	if (ret < 0) {
		dev_err(component->dev, "set integer constraint failed\n");
		kfree(stream);
		return ret;
	}

	stream->stream_id = cpu_dai->id;
	stream->instance = amd_manager->instance;
	runtime->private_data = stream;
	return ret;
}

static int acp63_sdw_dma_hw_params(struct snd_soc_component *component,
				   struct snd_pcm_substream *substream,
				   struct snd_pcm_hw_params *params)
{
	struct acp_sdw_dma_stream *stream;
	struct sdw_dma_dev_data *sdw_data;
	u32 period_bytes;
	u32 water_mark_size_reg;
	u32 irq_mask, ext_intr_ctrl;
	u64 size;
	u32 stream_id;
	u32 acp_ext_intr_cntl_reg;
	int ret;

	sdw_data = dev_get_drvdata(component->dev);
	stream = substream->runtime->private_data;
	if (!stream)
		return -EINVAL;
	stream_id = stream->stream_id;
	switch (stream->instance) {
	case ACP_SDW0:
		sdw_data->sdw0_dma_stream[stream_id] = substream;
		water_mark_size_reg = sdw0_dma_ring_buf_reg[stream_id].water_mark_size_reg;
		acp_ext_intr_cntl_reg = ACP_EXTERNAL_INTR_CNTL;
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			irq_mask = BIT(SDW0_DMA_TX_IRQ_MASK(stream_id));
		else
			irq_mask = BIT(SDW0_DMA_RX_IRQ_MASK(stream_id));
		break;
	case ACP_SDW1:
		sdw_data->sdw1_dma_stream[stream_id] = substream;
		acp_ext_intr_cntl_reg = ACP_EXTERNAL_INTR_CNTL1;
		water_mark_size_reg = sdw1_dma_ring_buf_reg[stream_id].water_mark_size_reg;
		irq_mask = BIT(SDW1_DMA_IRQ_MASK(stream_id));
		break;
	default:
		return -EINVAL;
	}
	size = params_buffer_bytes(params);
	period_bytes = params_period_bytes(params);
	stream->dma_addr = substream->runtime->dma_addr;
	stream->num_pages = (PAGE_ALIGN(size) >> PAGE_SHIFT);
	acp63_config_dma(stream, sdw_data->acp_base, stream_id);
	ret = acp63_configure_sdw_ringbuffer(sdw_data->acp_base, stream_id, size,
					     stream->instance);
	if (ret) {
		dev_err(component->dev, "Invalid DMA channel\n");
		return -EINVAL;
	}
	ext_intr_ctrl = readl(sdw_data->acp_base + acp_ext_intr_cntl_reg);
	ext_intr_ctrl |= irq_mask;
	writel(ext_intr_ctrl, sdw_data->acp_base + acp_ext_intr_cntl_reg);
	writel(period_bytes, sdw_data->acp_base + water_mark_size_reg);
	return 0;
}

static u64 acp63_sdw_get_byte_count(struct acp_sdw_dma_stream *stream, void __iomem *acp_base)
{
	union acp_sdw_dma_count byte_count;
	u32 pos_low_reg, pos_high_reg;

	byte_count.bytescount = 0;
	switch (stream->instance) {
	case ACP_SDW0:
		pos_low_reg = sdw0_dma_ring_buf_reg[stream->stream_id].pos_low_reg;
		pos_high_reg = sdw0_dma_ring_buf_reg[stream->stream_id].pos_high_reg;
		break;
	case ACP_SDW1:
		pos_low_reg = sdw1_dma_ring_buf_reg[stream->stream_id].pos_low_reg;
		pos_high_reg = sdw1_dma_ring_buf_reg[stream->stream_id].pos_high_reg;
		break;
	default:
		goto POINTER_RETURN_BYTES;
	}
	if (pos_low_reg) {
		byte_count.bcount.high = readl(acp_base + pos_high_reg);
		byte_count.bcount.low = readl(acp_base + pos_low_reg);
	}
POINTER_RETURN_BYTES:
	return byte_count.bytescount;
}

static snd_pcm_uframes_t acp63_sdw_dma_pointer(struct snd_soc_component *comp,
					       struct snd_pcm_substream *substream)
{
	struct sdw_dma_dev_data *sdw_data;
	struct acp_sdw_dma_stream *stream;
	u32 pos, buffersize;
	u64 bytescount;

	sdw_data = dev_get_drvdata(comp->dev);
	stream = substream->runtime->private_data;
	buffersize = frames_to_bytes(substream->runtime,
				     substream->runtime->buffer_size);
	bytescount = acp63_sdw_get_byte_count(stream, sdw_data->acp_base);
	if (bytescount > stream->bytescount)
		bytescount -= stream->bytescount;
	pos = do_div(bytescount, buffersize);
	return bytes_to_frames(substream->runtime, pos);
}

static int acp63_sdw_dma_new(struct snd_soc_component *component,
			     struct snd_soc_pcm_runtime *rtd)
{
	struct device *parent = component->dev->parent;

	snd_pcm_set_managed_buffer_all(rtd->pcm, SNDRV_DMA_TYPE_DEV,
				       parent, SDW_MIN_BUFFER, SDW_MAX_BUFFER);
	return 0;
}

static int acp63_sdw_dma_close(struct snd_soc_component *component,
			       struct snd_pcm_substream *substream)
{
	struct sdw_dma_dev_data *sdw_data;
	struct acp_sdw_dma_stream *stream;

	sdw_data = dev_get_drvdata(component->dev);
	stream = substream->runtime->private_data;
	if (!stream)
		return -EINVAL;
	switch (stream->instance) {
	case ACP_SDW0:
		sdw_data->sdw0_dma_stream[stream->stream_id] = NULL;
		break;
	case ACP_SDW1:
		sdw_data->sdw1_dma_stream[stream->stream_id] = NULL;
		break;
	default:
		return -EINVAL;
	}
	kfree(stream);
	return 0;
}

static int acp63_sdw_dma_enable(struct snd_pcm_substream *substream,
				void __iomem *acp_base, bool sdw_dma_enable)
{
	struct acp_sdw_dma_stream *stream;
	u32 stream_id;
	u32 sdw_dma_en_reg;
	u32 sdw_dma_en_stat_reg;
	u32 sdw_dma_stat;
	u32 dma_enable;

	stream = substream->runtime->private_data;
	stream_id = stream->stream_id;
	switch (stream->instance) {
	case ACP_SDW0:
		sdw_dma_en_reg = sdw0_dma_enable_reg[stream_id];
		break;
	case ACP_SDW1:
		sdw_dma_en_reg = sdw1_dma_enable_reg[stream_id];
		break;
	default:
		return -EINVAL;
	}
	sdw_dma_en_stat_reg = sdw_dma_en_reg + 4;
	dma_enable = sdw_dma_enable;
	writel(dma_enable, acp_base + sdw_dma_en_reg);
	return readl_poll_timeout(acp_base + sdw_dma_en_stat_reg, sdw_dma_stat,
				  (sdw_dma_stat == dma_enable), ACP_DELAY_US, ACP_COUNTER);
}

static int acp63_sdw_dma_trigger(struct snd_soc_component *comp,
				 struct snd_pcm_substream *substream,
				 int cmd)
{
	struct sdw_dma_dev_data *sdw_data;
	int ret;

	sdw_data = dev_get_drvdata(comp->dev);
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
	case SNDRV_PCM_TRIGGER_RESUME:
		ret = acp63_sdw_dma_enable(substream, sdw_data->acp_base, true);
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_STOP:
		ret = acp63_sdw_dma_enable(substream, sdw_data->acp_base, false);
		break;
	default:
		ret = -EINVAL;
	}
	if (ret)
		dev_err(comp->dev, "trigger %d failed: %d", cmd, ret);
	return ret;
}

static const struct snd_soc_component_driver acp63_sdw_component = {
	.name		= DRV_NAME,
	.open		= acp63_sdw_dma_open,
	.close		= acp63_sdw_dma_close,
	.hw_params	= acp63_sdw_dma_hw_params,
	.trigger	= acp63_sdw_dma_trigger,
	.pointer	= acp63_sdw_dma_pointer,
	.pcm_construct	= acp63_sdw_dma_new,
};

static int acp63_sdw_platform_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct sdw_dma_dev_data *sdw_data;
	struct acp63_dev_data *acp_data;
	struct device *parent;
	int status;

	parent = pdev->dev.parent;
	acp_data = dev_get_drvdata(parent);
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "IORESOURCE_MEM FAILED\n");
		return -ENODEV;
	}

	sdw_data = devm_kzalloc(&pdev->dev, sizeof(*sdw_data), GFP_KERNEL);
	if (!sdw_data)
		return -ENOMEM;

	sdw_data->acp_base = devm_ioremap(&pdev->dev, res->start, resource_size(res));
	if (!sdw_data->acp_base)
		return -ENOMEM;

	sdw_data->acp_lock = &acp_data->acp_lock;
	dev_set_drvdata(&pdev->dev, sdw_data);
	status = devm_snd_soc_register_component(&pdev->dev,
						 &acp63_sdw_component,
						 NULL, 0);
	if (status) {
		dev_err(&pdev->dev, "Fail to register sdw dma component\n");
		return status;
	}
	pm_runtime_set_autosuspend_delay(&pdev->dev, ACP_SUSPEND_DELAY_MS);
	pm_runtime_use_autosuspend(&pdev->dev);
	pm_runtime_mark_last_busy(&pdev->dev);
	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);
	return 0;
}

static void acp63_sdw_platform_remove(struct platform_device *pdev)
{
	pm_runtime_disable(&pdev->dev);
}

static int acp_restore_sdw_dma_config(struct sdw_dma_dev_data *sdw_data)
{
	struct acp_sdw_dma_stream *stream;
	struct snd_pcm_substream *substream;
	struct snd_pcm_runtime *runtime;
	u32 period_bytes, buf_size, water_mark_size_reg;
	u32 stream_count;
	int index, instance, ret;

	for (instance = 0; instance < AMD_SDW_MAX_MANAGERS; instance++) {
		if (instance == ACP_SDW0)
			stream_count = ACP63_SDW0_DMA_MAX_STREAMS;
		else
			stream_count = ACP63_SDW1_DMA_MAX_STREAMS;

		for (index = 0; index < stream_count; index++) {
			if (instance == ACP_SDW0) {
				substream = sdw_data->sdw0_dma_stream[index];
				water_mark_size_reg =
						sdw0_dma_ring_buf_reg[index].water_mark_size_reg;
			} else {
				substream = sdw_data->sdw1_dma_stream[index];
				water_mark_size_reg =
						sdw1_dma_ring_buf_reg[index].water_mark_size_reg;
			}

			if (substream && substream->runtime) {
				runtime = substream->runtime;
				stream = runtime->private_data;
				period_bytes = frames_to_bytes(runtime, runtime->period_size);
				buf_size = frames_to_bytes(runtime, runtime->buffer_size);
				acp63_config_dma(stream, sdw_data->acp_base, index);
				ret = acp63_configure_sdw_ringbuffer(sdw_data->acp_base, index,
								     buf_size, instance);
				if (ret)
					return ret;
				writel(period_bytes, sdw_data->acp_base + water_mark_size_reg);
			}
		}
	}
	acp63_enable_disable_sdw_dma_interrupts(sdw_data->acp_base, true);
	return 0;
}

static int __maybe_unused acp63_sdw_pcm_resume(struct device *dev)
{
	struct sdw_dma_dev_data *sdw_data;

	sdw_data = dev_get_drvdata(dev);
	return acp_restore_sdw_dma_config(sdw_data);
}

static const struct dev_pm_ops acp63_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(NULL, acp63_sdw_pcm_resume)
};

static struct platform_driver acp63_sdw_dma_driver = {
	.probe = acp63_sdw_platform_probe,
	.remove_new = acp63_sdw_platform_remove,
	.driver = {
		.name = "amd_ps_sdw_dma",
		.pm = &acp63_pm_ops,
	},
};

module_platform_driver(acp63_sdw_dma_driver);

MODULE_AUTHOR("Vijendar.Mukunda@amd.com");
MODULE_DESCRIPTION("AMD ACP6.3 PS SDW DMA Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRV_NAME);
