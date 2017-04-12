/*
 * mtk-afe-fe-dais.c  --  Mediatek afe fe dai operator
 *
 * Copyright (c) 2016 MediaTek Inc.
 * Author: Garlic Tseng <garlic.tseng@mediatek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <sound/soc.h>
#include "mtk-afe-fe-dai.h"
#include "mtk-base-afe.h"

#define AFE_BASE_END_OFFSET 8

static int mtk_regmap_update_bits(struct regmap *map, int reg,
			   unsigned int mask,
			   unsigned int val)
{
	if (reg < 0)
		return 0;
	return regmap_update_bits(map, reg, mask, val);
}

static int mtk_regmap_write(struct regmap *map, int reg, unsigned int val)
{
	if (reg < 0)
		return 0;
	return regmap_write(map, reg, val);
}

int mtk_afe_fe_startup(struct snd_pcm_substream *substream,
		       struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct mtk_base_afe *afe = snd_soc_platform_get_drvdata(rtd->platform);
	struct snd_pcm_runtime *runtime = substream->runtime;
	int memif_num = rtd->cpu_dai->id;
	struct mtk_base_afe_memif *memif = &afe->memif[memif_num];
	const struct snd_pcm_hardware *mtk_afe_hardware = afe->mtk_afe_hardware;
	int ret;

	memif->substream = substream;

	snd_pcm_hw_constraint_step(substream->runtime, 0,
				   SNDRV_PCM_HW_PARAM_BUFFER_BYTES, 16);
	/* enable agent */
	mtk_regmap_update_bits(afe->regmap, memif->data->agent_disable_reg,
			       1 << memif->data->agent_disable_shift,
			       0 << memif->data->agent_disable_shift);

	snd_soc_set_runtime_hwparams(substream, mtk_afe_hardware);

	/*
	 * Capture cannot use ping-pong buffer since hw_ptr at IRQ may be
	 * smaller than period_size due to AFE's internal buffer.
	 * This easily leads to overrun when avail_min is period_size.
	 * One more period can hold the possible unread buffer.
	 */
	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
		int periods_max = mtk_afe_hardware->periods_max;

		ret = snd_pcm_hw_constraint_minmax(runtime,
						   SNDRV_PCM_HW_PARAM_PERIODS,
						   3, periods_max);
		if (ret < 0) {
			dev_err(afe->dev, "hw_constraint_minmax failed\n");
			return ret;
		}
	}

	ret = snd_pcm_hw_constraint_integer(runtime,
					    SNDRV_PCM_HW_PARAM_PERIODS);
	if (ret < 0)
		dev_err(afe->dev, "snd_pcm_hw_constraint_integer failed\n");

	/* dynamic allocate irq to memif */
	if (memif->irq_usage < 0) {
		int irq_id = mtk_dynamic_irq_acquire(afe);

		if (irq_id != afe->irqs_size) {
			/* link */
			memif->irq_usage = irq_id;
		} else {
			dev_err(afe->dev, "%s() error: no more asys irq\n",
				__func__);
			ret = -EBUSY;
		}
	}
	return ret;
}
EXPORT_SYMBOL_GPL(mtk_afe_fe_startup);

void mtk_afe_fe_shutdown(struct snd_pcm_substream *substream,
			 struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct mtk_base_afe *afe = snd_soc_platform_get_drvdata(rtd->platform);
	struct mtk_base_afe_memif *memif = &afe->memif[rtd->cpu_dai->id];
	int irq_id;

	irq_id = memif->irq_usage;

	mtk_regmap_update_bits(afe->regmap, memif->data->agent_disable_reg,
			       1 << memif->data->agent_disable_shift,
			       1 << memif->data->agent_disable_shift);

	if (!memif->const_irq) {
		mtk_dynamic_irq_release(afe, irq_id);
		memif->irq_usage = -1;
		memif->substream = NULL;
	}
}
EXPORT_SYMBOL_GPL(mtk_afe_fe_shutdown);

int mtk_afe_fe_hw_params(struct snd_pcm_substream *substream,
			 struct snd_pcm_hw_params *params,
			 struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct mtk_base_afe *afe = snd_soc_platform_get_drvdata(rtd->platform);
	struct mtk_base_afe_memif *memif = &afe->memif[rtd->cpu_dai->id];
	int msb_at_bit33 = 0;
	int ret, fs = 0;

	ret = snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(params));
	if (ret < 0)
		return ret;

	msb_at_bit33 = upper_32_bits(substream->runtime->dma_addr) ? 1 : 0;
	memif->phys_buf_addr = lower_32_bits(substream->runtime->dma_addr);
	memif->buffer_size = substream->runtime->dma_bytes;

	/* start */
	mtk_regmap_write(afe->regmap, memif->data->reg_ofs_base,
			 memif->phys_buf_addr);
	/* end */
	mtk_regmap_write(afe->regmap,
			 memif->data->reg_ofs_base + AFE_BASE_END_OFFSET,
			 memif->phys_buf_addr + memif->buffer_size - 1);

	/* set MSB to 33-bit */
	mtk_regmap_update_bits(afe->regmap, memif->data->msb_reg,
			       1 << memif->data->msb_shift,
			       msb_at_bit33 << memif->data->msb_shift);

	/* set channel */
	if (memif->data->mono_shift >= 0) {
		unsigned int mono = (params_channels(params) == 1) ? 1 : 0;

		mtk_regmap_update_bits(afe->regmap, memif->data->mono_reg,
				       1 << memif->data->mono_shift,
				       mono << memif->data->mono_shift);
	}

	/* set rate */
	if (memif->data->fs_shift < 0)
		return 0;

	fs = afe->memif_fs(substream, params_rate(params));

	if (fs < 0)
		return -EINVAL;

	mtk_regmap_update_bits(afe->regmap, memif->data->fs_reg,
			       memif->data->fs_maskbit << memif->data->fs_shift,
			       fs << memif->data->fs_shift);

	return 0;
}
EXPORT_SYMBOL_GPL(mtk_afe_fe_hw_params);

int mtk_afe_fe_hw_free(struct snd_pcm_substream *substream,
		       struct snd_soc_dai *dai)
{
	return snd_pcm_lib_free_pages(substream);
}
EXPORT_SYMBOL_GPL(mtk_afe_fe_hw_free);

int mtk_afe_fe_trigger(struct snd_pcm_substream *substream, int cmd,
		       struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_pcm_runtime * const runtime = substream->runtime;
	struct mtk_base_afe *afe = snd_soc_platform_get_drvdata(rtd->platform);
	struct mtk_base_afe_memif *memif = &afe->memif[rtd->cpu_dai->id];
	struct mtk_base_afe_irq *irqs = &afe->irqs[memif->irq_usage];
	const struct mtk_base_irq_data *irq_data = irqs->irq_data;
	unsigned int counter = runtime->period_size;
	int fs;

	dev_dbg(afe->dev, "%s %s cmd=%d\n", __func__, memif->data->name, cmd);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
		if (memif->data->enable_shift >= 0)
			mtk_regmap_update_bits(afe->regmap,
					       memif->data->enable_reg,
					       1 << memif->data->enable_shift,
					       1 << memif->data->enable_shift);

		/* set irq counter */
		mtk_regmap_update_bits(afe->regmap, irq_data->irq_cnt_reg,
				       irq_data->irq_cnt_maskbit
				       << irq_data->irq_cnt_shift,
				       counter << irq_data->irq_cnt_shift);

		/* set irq fs */
		fs = afe->irq_fs(substream, runtime->rate);

		if (fs < 0)
			return -EINVAL;

		mtk_regmap_update_bits(afe->regmap, irq_data->irq_fs_reg,
				       irq_data->irq_fs_maskbit
				       << irq_data->irq_fs_shift,
				       fs << irq_data->irq_fs_shift);

		/* enable interrupt */
		mtk_regmap_update_bits(afe->regmap, irq_data->irq_en_reg,
				       1 << irq_data->irq_en_shift,
				       1 << irq_data->irq_en_shift);

		return 0;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		mtk_regmap_update_bits(afe->regmap, memif->data->enable_reg,
				       1 << memif->data->enable_shift, 0);
		/* disable interrupt */
		mtk_regmap_update_bits(afe->regmap, irq_data->irq_en_reg,
				       1 << irq_data->irq_en_shift,
				       0 << irq_data->irq_en_shift);
		/* and clear pending IRQ */
		mtk_regmap_write(afe->regmap, irq_data->irq_clr_reg,
				 1 << irq_data->irq_clr_shift);
		return 0;
	default:
		return -EINVAL;
	}
}
EXPORT_SYMBOL_GPL(mtk_afe_fe_trigger);

int mtk_afe_fe_prepare(struct snd_pcm_substream *substream,
		       struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd  = substream->private_data;
	struct mtk_base_afe *afe = snd_soc_platform_get_drvdata(rtd->platform);
	struct mtk_base_afe_memif *memif = &afe->memif[rtd->cpu_dai->id];
	int hd_audio = 0;

	/* set hd mode */
	switch (substream->runtime->format) {
	case SNDRV_PCM_FORMAT_S16_LE:
		hd_audio = 0;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		hd_audio = 1;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		hd_audio = 1;
		break;
	default:
		dev_err(afe->dev, "%s() error: unsupported format %d\n",
			__func__, substream->runtime->format);
		break;
	}

	mtk_regmap_update_bits(afe->regmap, memif->data->hd_reg,
			       1 << memif->data->hd_shift,
			       hd_audio << memif->data->hd_shift);

	return 0;
}
EXPORT_SYMBOL_GPL(mtk_afe_fe_prepare);

const struct snd_soc_dai_ops mtk_afe_fe_ops = {
	.startup	= mtk_afe_fe_startup,
	.shutdown	= mtk_afe_fe_shutdown,
	.hw_params	= mtk_afe_fe_hw_params,
	.hw_free	= mtk_afe_fe_hw_free,
	.prepare	= mtk_afe_fe_prepare,
	.trigger	= mtk_afe_fe_trigger,
};
EXPORT_SYMBOL_GPL(mtk_afe_fe_ops);

static DEFINE_MUTEX(irqs_lock);
int mtk_dynamic_irq_acquire(struct mtk_base_afe *afe)
{
	int i;

	mutex_lock(&afe->irq_alloc_lock);
	for (i = 0; i < afe->irqs_size; ++i) {
		if (afe->irqs[i].irq_occupyed == 0) {
			afe->irqs[i].irq_occupyed = 1;
			mutex_unlock(&afe->irq_alloc_lock);
			return i;
		}
	}
	mutex_unlock(&afe->irq_alloc_lock);
	return afe->irqs_size;
}
EXPORT_SYMBOL_GPL(mtk_dynamic_irq_acquire);

int mtk_dynamic_irq_release(struct mtk_base_afe *afe, int irq_id)
{
	mutex_lock(&afe->irq_alloc_lock);
	if (irq_id >= 0 && irq_id < afe->irqs_size) {
		afe->irqs[irq_id].irq_occupyed = 0;
		mutex_unlock(&afe->irq_alloc_lock);
		return 0;
	}
	mutex_unlock(&afe->irq_alloc_lock);
	return -EINVAL;
}
EXPORT_SYMBOL_GPL(mtk_dynamic_irq_release);

int mtk_afe_dai_suspend(struct snd_soc_dai *dai)
{
	struct mtk_base_afe *afe = dev_get_drvdata(dai->dev);
	struct device *dev = afe->dev;
	struct regmap *regmap = afe->regmap;
	int i;

	if (pm_runtime_status_suspended(dev) || afe->suspended)
		return 0;

	if (!afe->reg_back_up)
		afe->reg_back_up =
			devm_kcalloc(dev, afe->reg_back_up_list_num,
				     sizeof(unsigned int), GFP_KERNEL);

	for (i = 0; i < afe->reg_back_up_list_num; i++)
		regmap_read(regmap, afe->reg_back_up_list[i],
			    &afe->reg_back_up[i]);

	afe->suspended = true;
	afe->runtime_suspend(dev);
	return 0;
}
EXPORT_SYMBOL_GPL(mtk_afe_dai_suspend);

int mtk_afe_dai_resume(struct snd_soc_dai *dai)
{
	struct mtk_base_afe *afe = dev_get_drvdata(dai->dev);
	struct device *dev = afe->dev;
	struct regmap *regmap = afe->regmap;
	int i = 0;

	if (pm_runtime_status_suspended(dev) || !afe->suspended)
		return 0;

	afe->runtime_resume(dev);

	if (!afe->reg_back_up)
		dev_dbg(dev, "%s no reg_backup\n", __func__);

	for (i = 0; i < afe->reg_back_up_list_num; i++)
		mtk_regmap_write(regmap, afe->reg_back_up_list[i],
				 afe->reg_back_up[i]);

	afe->suspended = false;
	return 0;
}
EXPORT_SYMBOL_GPL(mtk_afe_dai_resume);

MODULE_DESCRIPTION("Mediatek simple fe dai operator");
MODULE_AUTHOR("Garlic Tseng <garlic.tseng@mediatek.com>");
MODULE_LICENSE("GPL v2");

