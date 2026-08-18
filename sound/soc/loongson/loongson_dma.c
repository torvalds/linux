// SPDX-License-Identifier: GPL-2.0
//
// Loongson ALSA SoC Platform (DMA) driver
//
// Copyright (C) 2023 Loongson Technology Corporation Limited
// Author: Yingkun Meng <mengyingkun@loongson.cn>
//         Binbin ZHou <zhoubinbin@loongson.cn>
//

#include <linux/module.h>
#include <linux/io-64-nonatomic-lo-hi.h>
#include <linux/delay.h>
#include <linux/pm_runtime.h>
#include <linux/dma-mapping.h>
#include <sound/soc.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include "loongson_i2s.h"

/* Internal DMA dma_order Register */
#define DMA_ORDER_STOP          BIT(4) /* DMA stop */
#define DMA_ORDER_START         BIT(3) /* DMA start */
#define DMA_ORDER_ASK_VALID     BIT(2) /* DMA ask valid flag */
#define DMA_ORDER_AXI_UNCO      BIT(1) /* Uncache access */
#define DMA_ORDER_ADDR_64       BIT(0) /* 64bits address support */

#define DMA_ORDER_ASK_MASK      (~0x1fUL) /* Ask addr mask */
#define DMA_ORDER_CTRL_MASK     (0x0fUL)  /* Control mask  */

/*
 * Internal DMA registers descriptor.
 */
struct loongson_idma_desc {
	u32 order;		/* Next descriptor address register */
	u32 saddr;		/* Source address register */
	u32 daddr;		/* Device address register */
	u32 length;		/* Total length register */
	u32 step_length;	/* Memory stride register */
	u32 step_times;		/* Repeat time register */
	u32 cmd;		/* Command register */
	u32 stats;		/* Status register */
	u32 order_hi;		/* Next descriptor high address register */
	u32 saddr_hi;		/* High source address register */
	u32 res[6];		/* Reserved */
} __packed;

struct loongson_runtime_data {
	struct loongson_idma_data *dma_data;

	struct loongson_idma_desc *dma_desc_arr;
	dma_addr_t dma_desc_arr_phy;
	int dma_desc_arr_size;

	struct loongson_idma_desc *dma_pos_desc;
	dma_addr_t dma_pos_desc_phy;
};

static const struct snd_pcm_hardware loongson_idma_hardware = {
	.info = SNDRV_PCM_INFO_MMAP |
		SNDRV_PCM_INFO_INTERLEAVED |
		SNDRV_PCM_INFO_MMAP_VALID |
		SNDRV_PCM_INFO_RESUME |
		SNDRV_PCM_INFO_PAUSE,
	.formats = (SNDRV_PCM_FMTBIT_S8 |
		SNDRV_PCM_FMTBIT_S16_LE |
		SNDRV_PCM_FMTBIT_S20_3LE |
		SNDRV_PCM_FMTBIT_S24_LE),
	.period_bytes_min = 128,
	.period_bytes_max = 128 * 1024,
	.periods_min = 1,
	.periods_max = PAGE_SIZE / sizeof(struct loongson_idma_desc),
	.buffer_bytes_max = 1024 * 1024,
};

static struct loongson_idma_desc *dma_desc_save(struct loongson_runtime_data *prtd)
{
	void __iomem *order_reg = prtd->dma_data->order_addr;
	u64 val;

	val = (u64)prtd->dma_pos_desc_phy & DMA_ORDER_ASK_MASK;
	val |= (readq(order_reg) & DMA_ORDER_CTRL_MASK);
	val |= DMA_ORDER_ASK_VALID;
	writeq(val, order_reg);

	while (readl(order_reg) & DMA_ORDER_ASK_VALID)
		udelay(2);

	return prtd->dma_pos_desc;
}

static int loongson_idma_pcm_trigger(struct snd_soc_component *component,
				     struct snd_pcm_substream *substream, int cmd)
{
	struct loongson_runtime_data *prtd = substream->runtime->private_data;
	struct device *dev = substream->pcm->card->dev;
	void __iomem *order_reg = prtd->dma_data->order_addr;
	u64 val;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		val = prtd->dma_pos_desc_phy & DMA_ORDER_ASK_MASK;
		if (dev->coherent_dma_mask == DMA_BIT_MASK(64))
			val |= DMA_ORDER_ADDR_64;
		else
			val &= ~DMA_ORDER_ADDR_64;
		val |= (readq(order_reg) & DMA_ORDER_CTRL_MASK);
		val |= DMA_ORDER_START;
		writeq(val, order_reg);

		while ((readl(order_reg) & DMA_ORDER_START))
			udelay(2);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		dma_desc_save(prtd);

		/* dma stop */
		val = readq(order_reg) | DMA_ORDER_STOP;
		writeq(val, order_reg);
		udelay(1000);

		break;
	default:
		dev_err(dev, "Invalid pcm trigger operation\n");
		return -EINVAL;
	}

	return 0;
}

static int loongson_idma_pcm_hw_params(struct snd_soc_component *component,
				       struct snd_pcm_substream *substream,
				       struct snd_pcm_hw_params *params)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct device *dev = substream->pcm->card->dev;
	struct loongson_runtime_data *prtd = runtime->private_data;
	size_t buf_len = params_buffer_bytes(params);
	size_t period_len = params_period_bytes(params);
	dma_addr_t order_addr, mem_addr;
	struct loongson_idma_desc *desc;
	u32 num_periods;
	int i;

	if (buf_len % period_len) {
		dev_err(dev, "buf len not multiply of period len\n");
		return -EINVAL;
	}

	num_periods = buf_len / period_len;
	if (!num_periods || num_periods > prtd->dma_desc_arr_size) {
		dev_err(dev, "dma data too small or too big\n");
		return -EINVAL;
	}

	snd_pcm_set_runtime_buffer(substream, &substream->dma_buffer);
	runtime->dma_bytes = buf_len;

	/* initialize dma descriptor array */
	mem_addr = runtime->dma_addr;
	order_addr = prtd->dma_desc_arr_phy;
	for (i = 0; i < num_periods; i++) {
		desc = &prtd->dma_desc_arr[i];

		/* next descriptor physical address */
		order_addr += sizeof(*desc);
		desc->order = lower_32_bits(order_addr | BIT(0));
		desc->order_hi = upper_32_bits(order_addr);

		desc->saddr = lower_32_bits(mem_addr);
		desc->saddr_hi = upper_32_bits(mem_addr);
		desc->daddr = prtd->dma_data->dev_addr;

		desc->cmd = BIT(0);
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			desc->cmd |= BIT(12);

		desc->length = period_len >> 2;
		desc->step_length = 0;
		desc->step_times = 1;

		mem_addr += period_len;
	}
	desc = &prtd->dma_desc_arr[num_periods - 1];
	desc->order = lower_32_bits(prtd->dma_desc_arr_phy | BIT(0));
	desc->order_hi = upper_32_bits(prtd->dma_desc_arr_phy);

	/* init position descriptor */
	*prtd->dma_pos_desc = *prtd->dma_desc_arr;

	return 0;
}

static snd_pcm_uframes_t
loongson_idma_pcm_pointer(struct snd_soc_component *component,
			  struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct device *dev = substream->pcm->card->dev;
	struct loongson_runtime_data *prtd = runtime->private_data;
	struct loongson_idma_desc *desc;
	snd_pcm_uframes_t x;
	u64 addr;

	desc = dma_desc_save(prtd);
	addr = ((u64)desc->saddr_hi << 32) | desc->saddr;

	if (addr < runtime->dma_addr ||
	    addr > runtime->dma_addr + runtime->dma_bytes) {
		dev_warn(dev, "WARNING! dma_addr:0x%llx\n", addr);
		x = 0;
	} else {
		x = bytes_to_frames(runtime, addr - runtime->dma_addr);
		if (x == runtime->buffer_size)
			x = 0;
	}

	return x;
}

static irqreturn_t loongson_idma_pcm_dma_irq(int irq, void *devid)
{
	struct snd_pcm_substream *substream = devid;

	snd_pcm_period_elapsed(substream);
	return IRQ_HANDLED;
}

static int loongson_idma_pcm_open(struct snd_soc_component *component,
				  struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_card *card = substream->pcm->card;
	struct loongson_runtime_data *prtd;
	struct loongson_idma_data *dma_data;

	/*
	 * For mysterious reasons (and despite what the manual says)
	 * playback samples are lost if the DMA count is not a multiple
	 * of the DMA burst size.  Let's add a rule to enforce that.
	 */
	snd_pcm_hw_constraint_step(runtime, 0,
				   SNDRV_PCM_HW_PARAM_PERIOD_BYTES, 128);
	snd_pcm_hw_constraint_step(runtime, 0,
				   SNDRV_PCM_HW_PARAM_BUFFER_BYTES, 128);
	snd_pcm_hw_constraint_integer(substream->runtime,
				      SNDRV_PCM_HW_PARAM_PERIODS);
	snd_soc_set_runtime_hwparams(substream, &loongson_idma_hardware);

	prtd = kzalloc_obj(*prtd);
	if (!prtd)
		return -ENOMEM;

	prtd->dma_desc_arr = dma_alloc_coherent(card->dev, PAGE_SIZE,
						&prtd->dma_desc_arr_phy,
						GFP_KERNEL);
	if (!prtd->dma_desc_arr)
		goto desc_err;

	prtd->dma_desc_arr_size = PAGE_SIZE / sizeof(*prtd->dma_desc_arr);

	prtd->dma_pos_desc = dma_alloc_coherent(card->dev,
						sizeof(*prtd->dma_pos_desc),
						&prtd->dma_pos_desc_phy,
						GFP_KERNEL);
	if (!prtd->dma_pos_desc)
		goto pos_err;

	dma_data = snd_soc_dai_get_dma_data(snd_soc_rtd_to_cpu(rtd, 0), substream);
	prtd->dma_data = dma_data;

	substream->runtime->private_data = prtd;

	return 0;
pos_err:
	dma_free_coherent(card->dev, PAGE_SIZE, prtd->dma_desc_arr,
			  prtd->dma_desc_arr_phy);
desc_err:
	kfree(prtd);

	return -ENOMEM;
}

static int loongson_idma_pcm_close(struct snd_soc_component *component,
				   struct snd_pcm_substream *substream)
{
	struct snd_card *card = substream->pcm->card;
	struct loongson_runtime_data *prtd = substream->runtime->private_data;

	dma_free_coherent(card->dev, PAGE_SIZE, prtd->dma_desc_arr,
			  prtd->dma_desc_arr_phy);

	dma_free_coherent(card->dev, sizeof(*prtd->dma_pos_desc),
			  prtd->dma_pos_desc, prtd->dma_pos_desc_phy);

	kfree(prtd);
	return 0;
}

static int loongson_idma_pcm_mmap(struct snd_soc_component *component,
				  struct snd_pcm_substream *substream,
				  struct vm_area_struct *vma)
{
	return remap_pfn_range(vma, vma->vm_start,
			       substream->dma_buffer.addr >> PAGE_SHIFT,
			       vma->vm_end - vma->vm_start, vma->vm_page_prot);
}

static int loongson_idma_pcm_new(struct snd_soc_component *component,
				 struct snd_soc_pcm_runtime *rtd)
{
	struct snd_card *card = rtd->card->snd_card;
	struct snd_pcm_substream *substream;
	struct loongson_idma_data *dma_data;
	unsigned int i;
	int ret;

	for_each_pcm_streams(i) {
		substream = rtd->pcm->streams[i].substream;
		if (!substream)
			continue;

		dma_data = snd_soc_dai_get_dma_data(snd_soc_rtd_to_cpu(rtd, 0),
						    substream);
		ret = devm_request_irq(card->dev, dma_data->irq,
				       loongson_idma_pcm_dma_irq,
				       IRQF_TRIGGER_HIGH, LS_I2S_DRVNAME,
				       substream);
		if (ret < 0) {
			dev_err(card->dev, "request irq for DMA failed\n");
			return ret;
		}
	}

	return snd_pcm_set_fixed_buffer_all(rtd->pcm, SNDRV_DMA_TYPE_DEV,
					    card->dev,
					    loongson_idma_hardware.buffer_bytes_max);
}

/* Internal DMA component */
const struct snd_soc_component_driver loongson_i2s_idma_component = {
	.name		= LS_I2S_DRVNAME,
	.open		= loongson_idma_pcm_open,
	.close		= loongson_idma_pcm_close,
	.hw_params	= loongson_idma_pcm_hw_params,
	.trigger	= loongson_idma_pcm_trigger,
	.pointer	= loongson_idma_pcm_pointer,
	.mmap		= loongson_idma_pcm_mmap,
	.pcm_new	= loongson_idma_pcm_new,
};
EXPORT_SYMBOL_GPL(loongson_i2s_idma_component);

static const struct snd_pcm_hardware loongson_edma_hardware = {
	.info = SNDRV_PCM_INFO_MMAP |
		SNDRV_PCM_INFO_INTERLEAVED |
		SNDRV_PCM_INFO_MMAP_VALID |
		SNDRV_PCM_INFO_RESUME |
		SNDRV_PCM_INFO_PAUSE,
	.formats = SNDRV_PCM_FMTBIT_S16_LE |
		   SNDRV_PCM_FMTBIT_S20_3LE |
		   SNDRV_PCM_FMTBIT_S24_LE,
	.period_bytes_min = 128,
	.period_bytes_max = 128 * 1024,
	.periods_min = 1,
	.periods_max = 64,
	.buffer_bytes_max = 1024 * 1024,
};

const struct snd_dmaengine_pcm_config loongson_dmaengine_pcm_config = {
	.pcm_hardware = &loongson_edma_hardware,
	.prepare_slave_config = snd_dmaengine_pcm_prepare_slave_config,
	.prealloc_buffer_size = 128 * 1024,
};
EXPORT_SYMBOL_GPL(loongson_dmaengine_pcm_config);

/* External DMA component */
static int loongson_edma_pcm_open(struct snd_soc_component *component,
				  struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;

	if (substream->pcm->device & 1) {
		runtime->hw.info &= ~SNDRV_PCM_INFO_INTERLEAVED;
		runtime->hw.info |= SNDRV_PCM_INFO_NONINTERLEAVED;
	}

	if (substream->pcm->device & 2)
		runtime->hw.info &= ~(SNDRV_PCM_INFO_MMAP |
				      SNDRV_PCM_INFO_MMAP_VALID);
	/*
	 * For mysterious reasons (and despite what the manual says)
	 * playback samples are lost if the DMA count is not a multiple
	 * of the DMA burst size.  Let's add a rule to enforce that.
	 */
	snd_pcm_hw_constraint_step(runtime, 0,
				   SNDRV_PCM_HW_PARAM_PERIOD_BYTES, 128);
	snd_pcm_hw_constraint_step(runtime, 0,
				   SNDRV_PCM_HW_PARAM_BUFFER_BYTES, 128);
	snd_pcm_hw_constraint_integer(substream->runtime,
				      SNDRV_PCM_HW_PARAM_PERIODS);

	return 0;
}

const struct snd_soc_component_driver loongson_i2s_edma_component = {
	.name   = LS_I2S_DRVNAME,
	.open	= loongson_edma_pcm_open,
};
EXPORT_SYMBOL_GPL(loongson_i2s_edma_component);
