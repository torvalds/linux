// SPDX-License-Identifier: GPL-2.0
//
// Socionext UniPhier AIO DMA driver.
//
// Copyright (c) 2016-2018 Socionext Inc.

#include <linux/dma-mapping.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>

#include "aio.h"

static struct snd_pcm_hardware uniphier_aiodma_hw = {
	.info = SNDRV_PCM_INFO_MMAP |
		SNDRV_PCM_INFO_MMAP_VALID |
		SNDRV_PCM_INFO_INTERLEAVED,
	.period_bytes_min = 256,
	.period_bytes_max = 4096,
	.periods_min      = 4,
	.periods_max      = 1024,
	.buffer_bytes_max = 128 * 1024,
};

static void aiodma_pcm_irq(struct uniphier_aio_sub *sub)
{
	struct snd_pcm_runtime *runtime = sub->substream->runtime;
	int bytes = runtime->period_size *
		runtime->channels * samples_to_bytes(runtime, 1);
	int ret;

	spin_lock(&sub->lock);
	ret = aiodma_rb_set_threshold(sub, runtime->dma_bytes,
				      sub->threshold + bytes);
	if (!ret)
		sub->threshold += bytes;

	aiodma_rb_sync(sub, runtime->dma_addr, runtime->dma_bytes, bytes);
	aiodma_rb_clear_irq(sub);
	spin_unlock(&sub->lock);

	snd_pcm_period_elapsed(sub->substream);
}

static void aiodma_compr_irq(struct uniphier_aio_sub *sub)
{
	struct snd_compr_runtime *runtime = sub->cstream->runtime;
	int bytes = runtime->fragment_size;
	int ret;

	spin_lock(&sub->lock);
	ret = aiodma_rb_set_threshold(sub, sub->compr_bytes,
				      sub->threshold + bytes);
	if (!ret)
		sub->threshold += bytes;

	aiodma_rb_sync(sub, sub->compr_addr, sub->compr_bytes, bytes);
	aiodma_rb_clear_irq(sub);
	spin_unlock(&sub->lock);

	snd_compr_fragment_elapsed(sub->cstream);
}

static irqreturn_t aiodma_irq(int irq, void *p)
{
	struct platform_device *pdev = p;
	struct uniphier_aio_chip *chip = platform_get_drvdata(pdev);
	irqreturn_t ret = IRQ_NONE;
	int i, j;

	for (i = 0; i < chip->num_aios; i++) {
		struct uniphier_aio *aio = &chip->aios[i];

		for (j = 0; j < ARRAY_SIZE(aio->sub); j++) {
			struct uniphier_aio_sub *sub = &aio->sub[j];

			/* Skip channel that does not trigger */
			if (!sub->running || !aiodma_rb_is_irq(sub))
				continue;

			if (sub->substream)
				aiodma_pcm_irq(sub);
			if (sub->cstream)
				aiodma_compr_irq(sub);

			ret = IRQ_HANDLED;
		}
	}

	return ret;
}

static int uniphier_aiodma_open(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;

	snd_soc_set_runtime_hwparams(substream, &uniphier_aiodma_hw);

	return snd_pcm_hw_constraint_step(runtime, 0,
		SNDRV_PCM_HW_PARAM_BUFFER_BYTES, 256);
}

static int uniphier_aiodma_hw_params(struct snd_pcm_substream *substream,
				     struct snd_pcm_hw_params *params)
{
	snd_pcm_set_runtime_buffer(substream, &substream->dma_buffer);
	substream->runtime->dma_bytes = params_buffer_bytes(params);

	return 0;
}

static int uniphier_aiodma_hw_free(struct snd_pcm_substream *substream)
{
	snd_pcm_set_runtime_buffer(substream, NULL);
	substream->runtime->dma_bytes = 0;

	return 0;
}

static int uniphier_aiodma_prepare(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *rtd = snd_pcm_substream_chip(substream);
	struct uniphier_aio *aio = uniphier_priv(rtd->cpu_dai);
	struct uniphier_aio_sub *sub = &aio->sub[substream->stream];
	int bytes = runtime->period_size *
		runtime->channels * samples_to_bytes(runtime, 1);
	unsigned long flags;
	int ret;

	ret = aiodma_ch_set_param(sub);
	if (ret)
		return ret;

	spin_lock_irqsave(&sub->lock, flags);
	ret = aiodma_rb_set_buffer(sub, runtime->dma_addr,
				   runtime->dma_addr + runtime->dma_bytes,
				   bytes);
	spin_unlock_irqrestore(&sub->lock, flags);
	if (ret)
		return ret;

	return 0;
}

static int uniphier_aiodma_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *rtd = snd_pcm_substream_chip(substream);
	struct uniphier_aio *aio = uniphier_priv(rtd->cpu_dai);
	struct uniphier_aio_sub *sub = &aio->sub[substream->stream];
	struct device *dev = &aio->chip->pdev->dev;
	int bytes = runtime->period_size *
		runtime->channels * samples_to_bytes(runtime, 1);
	unsigned long flags;

	spin_lock_irqsave(&sub->lock, flags);
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		aiodma_rb_sync(sub, runtime->dma_addr, runtime->dma_bytes,
			       bytes);
		aiodma_ch_set_enable(sub, 1);
		sub->running = 1;

		break;
	case SNDRV_PCM_TRIGGER_STOP:
		sub->running = 0;
		aiodma_ch_set_enable(sub, 0);

		break;
	default:
		dev_warn(dev, "Unknown trigger(%d) ignored\n", cmd);
		break;
	}
	spin_unlock_irqrestore(&sub->lock, flags);

	return 0;
}

static snd_pcm_uframes_t uniphier_aiodma_pointer(
					struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *rtd = snd_pcm_substream_chip(substream);
	struct uniphier_aio *aio = uniphier_priv(rtd->cpu_dai);
	struct uniphier_aio_sub *sub = &aio->sub[substream->stream];
	int bytes = runtime->period_size *
		runtime->channels * samples_to_bytes(runtime, 1);
	unsigned long flags;
	snd_pcm_uframes_t pos;

	spin_lock_irqsave(&sub->lock, flags);
	aiodma_rb_sync(sub, runtime->dma_addr, runtime->dma_bytes, bytes);

	if (sub->swm->dir == PORT_DIR_OUTPUT)
		pos = bytes_to_frames(runtime, sub->rd_offs);
	else
		pos = bytes_to_frames(runtime, sub->wr_offs);
	spin_unlock_irqrestore(&sub->lock, flags);

	return pos;
}

static int uniphier_aiodma_mmap(struct snd_pcm_substream *substream,
				struct vm_area_struct *vma)
{
	vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);

	return remap_pfn_range(vma, vma->vm_start,
			       substream->dma_buffer.addr >> PAGE_SHIFT,
			       vma->vm_end - vma->vm_start, vma->vm_page_prot);
}

static const struct snd_pcm_ops uniphier_aiodma_ops = {
	.open      = uniphier_aiodma_open,
	.ioctl     = snd_pcm_lib_ioctl,
	.hw_params = uniphier_aiodma_hw_params,
	.hw_free   = uniphier_aiodma_hw_free,
	.prepare   = uniphier_aiodma_prepare,
	.trigger   = uniphier_aiodma_trigger,
	.pointer   = uniphier_aiodma_pointer,
	.mmap      = uniphier_aiodma_mmap,
};

static int uniphier_aiodma_new(struct snd_soc_pcm_runtime *rtd)
{
	struct device *dev = rtd->card->snd_card->dev;
	struct snd_pcm *pcm = rtd->pcm;
	int ret;

	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(33));
	if (ret)
		return ret;

	return snd_pcm_lib_preallocate_pages_for_all(pcm,
		SNDRV_DMA_TYPE_DEV, dev,
		uniphier_aiodma_hw.buffer_bytes_max,
		uniphier_aiodma_hw.buffer_bytes_max);
}

static void uniphier_aiodma_free(struct snd_pcm *pcm)
{
	snd_pcm_lib_preallocate_free_for_all(pcm);
}

static const struct snd_soc_component_driver uniphier_soc_platform = {
	.pcm_new   = uniphier_aiodma_new,
	.pcm_free  = uniphier_aiodma_free,
	.ops       = &uniphier_aiodma_ops,
	.compr_ops = &uniphier_aio_compr_ops,
};

static const struct regmap_config aiodma_regmap_config = {
	.reg_bits      = 32,
	.reg_stride    = 4,
	.val_bits      = 32,
	.max_register  = 0x7fffc,
	.cache_type    = REGCACHE_NONE,
};

/**
 * uniphier_aiodma_soc_register_platform - register the AIO DMA
 * @pdev: the platform device
 *
 * Register and setup the DMA of AIO to transfer the sound data to device.
 * This function need to call once at driver startup and need NOT to call
 * unregister function.
 *
 * Return: Zero if successful, otherwise a negative value on error.
 */
int uniphier_aiodma_soc_register_platform(struct platform_device *pdev)
{
	struct uniphier_aio_chip *chip = platform_get_drvdata(pdev);
	struct device *dev = &pdev->dev;
	struct resource *res;
	void __iomem *preg;
	int irq, ret;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	preg = devm_ioremap_resource(dev, res);
	if (IS_ERR(preg))
		return PTR_ERR(preg);

	chip->regmap = devm_regmap_init_mmio(dev, preg,
					     &aiodma_regmap_config);
	if (IS_ERR(chip->regmap))
		return PTR_ERR(chip->regmap);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(dev, "Could not get irq.\n");
		return irq;
	}

	ret = devm_request_irq(dev, irq, aiodma_irq,
			       IRQF_SHARED, dev_name(dev), pdev);
	if (ret)
		return ret;

	return devm_snd_soc_register_component(dev, &uniphier_soc_platform,
					       NULL, 0);
}
EXPORT_SYMBOL_GPL(uniphier_aiodma_soc_register_platform);
