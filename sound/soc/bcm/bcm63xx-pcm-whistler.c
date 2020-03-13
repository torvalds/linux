// SPDX-License-Identifier: GPL-2.0-or-later
// linux/sound/bcm/bcm63xx-pcm-whistler.c
// BCM63xx whistler pcm interface
// Copyright (c) 2020 Broadcom Corporation
// Author: Kevin-Ke Li <kevin-ke.li@broadcom.com>

#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/module.h>
#include <sound/pcm_params.h>
#include <linux/regmap.h>
#include <linux/of_device.h>
#include <sound/soc.h>
#include "bcm63xx-i2s.h"


struct i2s_dma_desc {
	unsigned char *dma_area;
	dma_addr_t dma_addr;
	unsigned int dma_len;
};

struct bcm63xx_runtime_data {
	int dma_len;
	dma_addr_t dma_addr;
	dma_addr_t dma_addr_next;
};

static const struct snd_pcm_hardware bcm63xx_pcm_hardware = {
	.info = SNDRV_PCM_INFO_MMAP |
		SNDRV_PCM_INFO_MMAP_VALID |
		SNDRV_PCM_INFO_INTERLEAVED |
		SNDRV_PCM_INFO_PAUSE |
		SNDRV_PCM_INFO_RESUME,
	.formats = SNDRV_PCM_FMTBIT_S32_LE, /* support S32 only */
	.period_bytes_max = 8192 - 32,
	.periods_min = 1,
	.periods_max = PAGE_SIZE/sizeof(struct i2s_dma_desc),
	.buffer_bytes_max = 128 * 1024,
	.fifo_size = 32,
};

static int bcm63xx_pcm_hw_params(struct snd_soc_component *component,
				 struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params)
{
	struct i2s_dma_desc *dma_desc;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_pcm_runtime *runtime = substream->runtime;

	snd_pcm_set_runtime_buffer(substream, &substream->dma_buffer);
	runtime->dma_bytes = params_buffer_bytes(params);

	dma_desc = kzalloc(sizeof(*dma_desc), GFP_NOWAIT);
	if (!dma_desc)
		return -ENOMEM;

	snd_soc_dai_set_dma_data(rtd->cpu_dai, substream, dma_desc);

	return 0;
}

static int bcm63xx_pcm_hw_free(struct snd_soc_component *component,
			struct snd_pcm_substream *substream)
{
	struct i2s_dma_desc	*dma_desc;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;

	dma_desc = snd_soc_dai_get_dma_data(rtd->cpu_dai, substream);
	kfree(dma_desc);
	snd_pcm_set_runtime_buffer(substream, NULL);

	return 0;
}

static int bcm63xx_pcm_trigger(struct snd_soc_component *component,
			       struct snd_pcm_substream *substream, int cmd)
{
	int ret = 0;
	struct snd_soc_pcm_runtime *rtd;
	struct bcm_i2s_priv *i2s_priv;
	struct regmap   *regmap_i2s;

	rtd = substream->private_data;
	i2s_priv = dev_get_drvdata(rtd->cpu_dai->dev);
	regmap_i2s = i2s_priv->regmap_i2s;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		switch (cmd) {
		case SNDRV_PCM_TRIGGER_START:
			regmap_update_bits(regmap_i2s,
					   I2S_TX_IRQ_EN,
					   I2S_TX_DESC_OFF_INTR_EN,
					   I2S_TX_DESC_OFF_INTR_EN);
			regmap_update_bits(regmap_i2s,
					   I2S_TX_CFG,
					   I2S_TX_ENABLE_MASK,
					   I2S_TX_ENABLE);
			break;
		case SNDRV_PCM_TRIGGER_STOP:
		case SNDRV_PCM_TRIGGER_SUSPEND:
		case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
			regmap_write(regmap_i2s,
				     I2S_TX_IRQ_EN,
				     0);
			regmap_update_bits(regmap_i2s,
					   I2S_TX_CFG,
					   I2S_TX_ENABLE_MASK,
					   0);
			break;
		default:
			ret = -EINVAL;
		}
	} else {
		switch (cmd) {
		case SNDRV_PCM_TRIGGER_START:
			regmap_update_bits(regmap_i2s,
					   I2S_RX_IRQ_EN,
					   I2S_RX_DESC_OFF_INTR_EN_MSK,
					   I2S_RX_DESC_OFF_INTR_EN);
			regmap_update_bits(regmap_i2s,
					   I2S_RX_CFG,
					   I2S_RX_ENABLE_MASK,
					   I2S_RX_ENABLE);
			break;
		case SNDRV_PCM_TRIGGER_STOP:
		case SNDRV_PCM_TRIGGER_SUSPEND:
		case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
			regmap_update_bits(regmap_i2s,
					   I2S_RX_IRQ_EN,
					   I2S_RX_DESC_OFF_INTR_EN_MSK,
					   0);
			regmap_update_bits(regmap_i2s,
					   I2S_RX_CFG,
					   I2S_RX_ENABLE_MASK,
					   0);
			break;
		default:
			ret = -EINVAL;
		}
	}
	return ret;
}

static int bcm63xx_pcm_prepare(struct snd_soc_component *component,
			struct snd_pcm_substream *substream)
{
	struct i2s_dma_desc	*dma_desc;
	struct regmap		*regmap_i2s;
	struct bcm_i2s_priv	*i2s_priv;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_pcm_runtime *runtime = substream->runtime;
	uint32_t regaddr_desclen, regaddr_descaddr;

	dma_desc = snd_soc_dai_get_dma_data(rtd->cpu_dai, substream);
	dma_desc->dma_len  = snd_pcm_lib_period_bytes(substream);
	dma_desc->dma_addr = runtime->dma_addr;
	dma_desc->dma_area = runtime->dma_area;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		regaddr_desclen = I2S_TX_DESC_IFF_LEN;
		regaddr_descaddr = I2S_TX_DESC_IFF_ADDR;
	} else {
		regaddr_desclen = I2S_RX_DESC_IFF_LEN;
		regaddr_descaddr = I2S_RX_DESC_IFF_ADDR;
	}

	i2s_priv = dev_get_drvdata(rtd->cpu_dai->dev);
	regmap_i2s = i2s_priv->regmap_i2s;

	regmap_write(regmap_i2s, regaddr_desclen, dma_desc->dma_len);
	regmap_write(regmap_i2s, regaddr_descaddr, dma_desc->dma_addr);

	return 0;
}

static snd_pcm_uframes_t
bcm63xx_pcm_pointer(struct snd_soc_component *component,
		struct snd_pcm_substream *substream)
{
	snd_pcm_uframes_t x;
	struct bcm63xx_runtime_data *prtd = substream->runtime->private_data;

	if ((void *)prtd->dma_addr_next == NULL)
		prtd->dma_addr_next = substream->runtime->dma_addr;

	x = bytes_to_frames(substream->runtime,
		prtd->dma_addr_next - substream->runtime->dma_addr);

	return x == substream->runtime->buffer_size ? 0 : x;
}

static int bcm63xx_pcm_mmap(struct snd_soc_component *component,
				struct snd_pcm_substream *substream,
				struct vm_area_struct *vma)
{
	struct snd_pcm_runtime *runtime = substream->runtime;

	return  dma_mmap_wc(substream->pcm->card->dev, vma,
			    runtime->dma_area,
			    runtime->dma_addr,
			    runtime->dma_bytes);

}

static int bcm63xx_pcm_open(struct snd_soc_component *component,
			struct snd_pcm_substream *substream)
{
	int ret = 0;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct bcm63xx_runtime_data *prtd;

	runtime->hw = bcm63xx_pcm_hardware;
	ret = snd_pcm_hw_constraint_step(runtime, 0,
					 SNDRV_PCM_HW_PARAM_PERIOD_BYTES, 32);
	if (ret)
		goto out;

	ret = snd_pcm_hw_constraint_step(runtime, 0,
					 SNDRV_PCM_HW_PARAM_BUFFER_BYTES, 32);
	if (ret)
		goto out;

	ret = snd_pcm_hw_constraint_integer(runtime,
					    SNDRV_PCM_HW_PARAM_PERIODS);
	if (ret < 0)
		goto out;

	ret = -ENOMEM;
	prtd = kzalloc(sizeof(*prtd), GFP_KERNEL);
	if (!prtd)
		goto out;

	runtime->private_data = prtd;
	return 0;
out:
	return ret;
}

static int bcm63xx_pcm_close(struct snd_soc_component *component,
			struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct bcm63xx_runtime_data *prtd = runtime->private_data;

	kfree(prtd);
	return 0;
}

static irqreturn_t i2s_dma_isr(int irq, void *bcm_i2s_priv)
{
	unsigned int availdepth, ifflevel, offlevel, int_status, val_1, val_2;
	struct bcm63xx_runtime_data *prtd;
	struct snd_pcm_substream *substream;
	struct snd_pcm_runtime *runtime;
	struct regmap *regmap_i2s;
	struct i2s_dma_desc *dma_desc;
	struct snd_soc_pcm_runtime *rtd;
	struct bcm_i2s_priv *i2s_priv;

	i2s_priv = (struct bcm_i2s_priv *)bcm_i2s_priv;
	regmap_i2s = i2s_priv->regmap_i2s;

	/* rx */
	regmap_read(regmap_i2s, I2S_RX_IRQ_CTL, &int_status);

	if (int_status & I2S_RX_DESC_OFF_INTR_EN_MSK) {
		substream = i2s_priv->capture_substream;
		runtime = substream->runtime;
		rtd = substream->private_data;
		prtd = runtime->private_data;
		dma_desc = snd_soc_dai_get_dma_data(rtd->cpu_dai, substream);

		offlevel = (int_status & I2S_RX_DESC_OFF_LEVEL_MASK) >>
			   I2S_RX_DESC_OFF_LEVEL_SHIFT;
		while (offlevel) {
			regmap_read(regmap_i2s, I2S_RX_DESC_OFF_ADDR, &val_1);
			regmap_read(regmap_i2s, I2S_RX_DESC_OFF_LEN, &val_2);
			offlevel--;
		}
		prtd->dma_addr_next = val_1 + val_2;
		ifflevel = (int_status & I2S_RX_DESC_IFF_LEVEL_MASK) >>
			   I2S_RX_DESC_IFF_LEVEL_SHIFT;

		availdepth = I2S_DESC_FIFO_DEPTH - ifflevel;
		while (availdepth) {
			dma_desc->dma_addr +=
					snd_pcm_lib_period_bytes(substream);
			dma_desc->dma_area +=
					snd_pcm_lib_period_bytes(substream);
			if (dma_desc->dma_addr - runtime->dma_addr >=
						runtime->dma_bytes) {
				dma_desc->dma_addr = runtime->dma_addr;
				dma_desc->dma_area = runtime->dma_area;
			}

			prtd->dma_addr = dma_desc->dma_addr;
			regmap_write(regmap_i2s, I2S_RX_DESC_IFF_LEN,
				     snd_pcm_lib_period_bytes(substream));
			regmap_write(regmap_i2s, I2S_RX_DESC_IFF_ADDR,
				     dma_desc->dma_addr);
			availdepth--;
		}

		snd_pcm_period_elapsed(substream);

		/* Clear interrupt by writing 0 */
		regmap_update_bits(regmap_i2s, I2S_RX_IRQ_CTL,
				   I2S_RX_INTR_MASK, 0);
	}

	/* tx */
	regmap_read(regmap_i2s, I2S_TX_IRQ_CTL, &int_status);

	if (int_status & I2S_TX_DESC_OFF_INTR_EN_MSK) {
		substream = i2s_priv->play_substream;
		runtime = substream->runtime;
		rtd = substream->private_data;
		prtd = runtime->private_data;
		dma_desc = snd_soc_dai_get_dma_data(rtd->cpu_dai, substream);

		offlevel = (int_status & I2S_TX_DESC_OFF_LEVEL_MASK) >>
			   I2S_TX_DESC_OFF_LEVEL_SHIFT;
		while (offlevel) {
			regmap_read(regmap_i2s, I2S_TX_DESC_OFF_ADDR, &val_1);
			regmap_read(regmap_i2s, I2S_TX_DESC_OFF_LEN,  &val_2);
			prtd->dma_addr_next = val_1 + val_2;
			offlevel--;
		}

		ifflevel = (int_status & I2S_TX_DESC_IFF_LEVEL_MASK) >>
			I2S_TX_DESC_IFF_LEVEL_SHIFT;
		availdepth = I2S_DESC_FIFO_DEPTH - ifflevel;

		while (availdepth) {
			dma_desc->dma_addr +=
					snd_pcm_lib_period_bytes(substream);
			dma_desc->dma_area +=
					snd_pcm_lib_period_bytes(substream);

			if (dma_desc->dma_addr - runtime->dma_addr >=
							runtime->dma_bytes) {
				dma_desc->dma_addr = runtime->dma_addr;
				dma_desc->dma_area = runtime->dma_area;
			}

			prtd->dma_addr = dma_desc->dma_addr;
			regmap_write(regmap_i2s, I2S_TX_DESC_IFF_LEN,
				snd_pcm_lib_period_bytes(substream));
			regmap_write(regmap_i2s, I2S_TX_DESC_IFF_ADDR,
					dma_desc->dma_addr);
			availdepth--;
		}

		snd_pcm_period_elapsed(substream);

		/* Clear interrupt by writing 0 */
		regmap_update_bits(regmap_i2s, I2S_TX_IRQ_CTL,
				   I2S_TX_INTR_MASK, 0);
	}

	return IRQ_HANDLED;
}

static int bcm63xx_pcm_preallocate_dma_buffer(struct snd_pcm *pcm, int stream)
{
	struct snd_pcm_substream *substream = pcm->streams[stream].substream;
	struct snd_dma_buffer *buf = &substream->dma_buffer;
	size_t size = bcm63xx_pcm_hardware.buffer_bytes_max;

	buf->dev.type = SNDRV_DMA_TYPE_DEV;
	buf->dev.dev = pcm->card->dev;
	buf->private_data = NULL;

	buf->area = dma_alloc_wc(pcm->card->dev,
				 size, &buf->addr,
				 GFP_KERNEL);
	if (!buf->area)
		return -ENOMEM;
	buf->bytes = size;
	return 0;
}

static int bcm63xx_soc_pcm_new(struct snd_soc_component *component,
		struct snd_soc_pcm_runtime *rtd)
{
	struct snd_pcm *pcm = rtd->pcm;
	struct bcm_i2s_priv *i2s_priv;
	int ret;

	i2s_priv = dev_get_drvdata(rtd->cpu_dai->dev);

	of_dma_configure(pcm->card->dev, pcm->card->dev->of_node, 1);

	ret = dma_coerce_mask_and_coherent(pcm->card->dev, DMA_BIT_MASK(32));
	if (ret)
		goto out;

	if (pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream) {
		ret = bcm63xx_pcm_preallocate_dma_buffer(pcm,
						 SNDRV_PCM_STREAM_PLAYBACK);
		if (ret)
			goto out;

		i2s_priv->play_substream =
			pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream;
	}

	if (pcm->streams[SNDRV_PCM_STREAM_CAPTURE].substream) {
		ret = bcm63xx_pcm_preallocate_dma_buffer(pcm,
					SNDRV_PCM_STREAM_CAPTURE);
		if (ret)
			goto out;
		i2s_priv->capture_substream =
			pcm->streams[SNDRV_PCM_STREAM_CAPTURE].substream;
	}

out:
	return ret;
}

static void bcm63xx_pcm_free_dma_buffers(struct snd_soc_component *component,
			 struct snd_pcm *pcm)
{
	int stream;
	struct snd_dma_buffer *buf;
	struct snd_pcm_substream *substream;

	for (stream = 0; stream < 2; stream++) {
		substream = pcm->streams[stream].substream;
		if (!substream)
			continue;
		buf = &substream->dma_buffer;
		if (!buf->area)
			continue;
		dma_free_wc(pcm->card->dev, buf->bytes,
					buf->area, buf->addr);
		buf->area = NULL;
	}
}

static const struct snd_soc_component_driver bcm63xx_soc_platform = {
	.open = bcm63xx_pcm_open,
	.close = bcm63xx_pcm_close,
	.hw_params = bcm63xx_pcm_hw_params,
	.hw_free = bcm63xx_pcm_hw_free,
	.prepare = bcm63xx_pcm_prepare,
	.trigger = bcm63xx_pcm_trigger,
	.pointer = bcm63xx_pcm_pointer,
	.mmap = bcm63xx_pcm_mmap,
	.pcm_construct = bcm63xx_soc_pcm_new,
	.pcm_destruct = bcm63xx_pcm_free_dma_buffers,
};

int bcm63xx_soc_platform_probe(struct platform_device *pdev,
			       struct bcm_i2s_priv *i2s_priv)
{
	int ret;

	i2s_priv->r_irq = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!i2s_priv->r_irq) {
		dev_err(&pdev->dev, "Unable to get register irq resource.\n");
		return -ENODEV;
	}

	ret = devm_request_irq(&pdev->dev, i2s_priv->r_irq->start, i2s_dma_isr,
			i2s_priv->r_irq->flags, "i2s_dma", (void *)i2s_priv);
	if (ret) {
		dev_err(&pdev->dev,
			"i2s_init: failed to request interrupt.ret=%d\n", ret);
		return ret;
	}

	return devm_snd_soc_register_component(&pdev->dev,
					&bcm63xx_soc_platform, NULL, 0);
}

int bcm63xx_soc_platform_remove(struct platform_device *pdev)
{
	return 0;
}

MODULE_AUTHOR("Kevin,Li <kevin-ke.li@broadcom.com>");
MODULE_DESCRIPTION("Broadcom DSL XPON ASOC PCM Interface");
MODULE_LICENSE("GPL v2");
