// SPDX-License-Identifier: GPL-2.0
/*
 * mtk-afe-fe-dais.c  --  Mediatek afe fe dai operator
 *
 * Copyright (c) 2016 MediaTek Inc.
 * Author: Garlic Tseng <garlic.tseng@mediatek.com>
 */

#include <linux/io.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <sound/soc.h>
#include "mtk-afe-platform-driver.h"
#include <sound/pcm_params.h>
#include "mtk-afe-fe-dai.h"
#include "mtk-base-afe.h"

#define AFE_BASE_END_OFFSET 8

static int mtk_regmap_update_bits(struct regmap *map, int reg,
			   unsigned int mask,
			   unsigned int val, int shift)
{
	if (reg < 0 || WARN_ON_ONCE(shift < 0))
		return 0;
	return regmap_update_bits(map, reg, mask << shift, val << shift);
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
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);
	struct snd_pcm_runtime *runtime = substream->runtime;
	int memif_num = asoc_rtd_to_cpu(rtd, 0)->id;
	struct mtk_base_afe_memif *memif = &afe->memif[memif_num];
	const struct snd_pcm_hardware *mtk_afe_hardware = afe->mtk_afe_hardware;
	int ret;

	memif->substream = substream;

	snd_pcm_hw_constraint_step(substream->runtime, 0,
				   SNDRV_PCM_HW_PARAM_BUFFER_BYTES, 16);
	/* enable agent */
	mtk_regmap_update_bits(afe->regmap, memif->data->agent_disable_reg,
			       1, 0, memif->data->agent_disable_shift);

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
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);
	struct mtk_base_afe_memif *memif = &afe->memif[asoc_rtd_to_cpu(rtd, 0)->id];
	int irq_id;

	irq_id = memif->irq_usage;

	mtk_regmap_update_bits(afe->regmap, memif->data->agent_disable_reg,
			       1, 1, memif->data->agent_disable_shift);

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
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);
	int id = asoc_rtd_to_cpu(rtd, 0)->id;
	struct mtk_base_afe_memif *memif = &afe->memif[id];
	int ret;
	unsigned int channels = params_channels(params);
	unsigned int rate = params_rate(params);
	snd_pcm_format_t format = params_format(params);

	if (afe->request_dram_resource)
		afe->request_dram_resource(afe->dev);

	dev_dbg(afe->dev, "%s(), %s, ch %d, rate %d, fmt %d, dma_addr %pad, dma_area %p, dma_bytes 0x%zx\n",
		__func__, memif->data->name,
		channels, rate, format,
		&substream->runtime->dma_addr,
		substream->runtime->dma_area,
		substream->runtime->dma_bytes);

	memset_io((void __force __iomem *)substream->runtime->dma_area, 0,
		  substream->runtime->dma_bytes);

	/* set addr */
	ret = mtk_memif_set_addr(afe, id,
				 substream->runtime->dma_area,
				 substream->runtime->dma_addr,
				 substream->runtime->dma_bytes);
	if (ret) {
		dev_err(afe->dev, "%s(), error, id %d, set addr, ret %d\n",
			__func__, id, ret);
		return ret;
	}

	/* set channel */
	ret = mtk_memif_set_channel(afe, id, channels);
	if (ret) {
		dev_err(afe->dev, "%s(), error, id %d, set channel %d, ret %d\n",
			__func__, id, channels, ret);
		return ret;
	}

	/* set rate */
	ret = mtk_memif_set_rate_substream(substream, id, rate);
	if (ret) {
		dev_err(afe->dev, "%s(), error, id %d, set rate %d, ret %d\n",
			__func__, id, rate, ret);
		return ret;
	}

	/* set format */
	ret = mtk_memif_set_format(afe, id, format);
	if (ret) {
		dev_err(afe->dev, "%s(), error, id %d, set format %d, ret %d\n",
			__func__, id, format, ret);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(mtk_afe_fe_hw_params);

int mtk_afe_fe_hw_free(struct snd_pcm_substream *substream,
		       struct snd_soc_dai *dai)
{
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);

	if (afe->release_dram_resource)
		afe->release_dram_resource(afe->dev);

	return 0;
}
EXPORT_SYMBOL_GPL(mtk_afe_fe_hw_free);

int mtk_afe_fe_trigger(struct snd_pcm_substream *substream, int cmd,
		       struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct snd_pcm_runtime * const runtime = substream->runtime;
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);
	int id = asoc_rtd_to_cpu(rtd, 0)->id;
	struct mtk_base_afe_memif *memif = &afe->memif[id];
	struct mtk_base_afe_irq *irqs = &afe->irqs[memif->irq_usage];
	const struct mtk_base_irq_data *irq_data = irqs->irq_data;
	unsigned int counter = runtime->period_size;
	int fs;
	int ret;

	dev_dbg(afe->dev, "%s %s cmd=%d\n", __func__, memif->data->name, cmd);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
		ret = mtk_memif_set_enable(afe, id);
		if (ret) {
			dev_err(afe->dev, "%s(), error, id %d, memif enable, ret %d\n",
				__func__, id, ret);
			return ret;
		}

		/* set irq counter */
		mtk_regmap_update_bits(afe->regmap, irq_data->irq_cnt_reg,
				       irq_data->irq_cnt_maskbit, counter,
				       irq_data->irq_cnt_shift);

		/* set irq fs */
		fs = afe->irq_fs(substream, runtime->rate);

		if (fs < 0)
			return -EINVAL;

		mtk_regmap_update_bits(afe->regmap, irq_data->irq_fs_reg,
				       irq_data->irq_fs_maskbit, fs,
				       irq_data->irq_fs_shift);

		/* enable interrupt */
		mtk_regmap_update_bits(afe->regmap, irq_data->irq_en_reg,
				       1, 1, irq_data->irq_en_shift);

		return 0;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		ret = mtk_memif_set_disable(afe, id);
		if (ret) {
			dev_err(afe->dev, "%s(), error, id %d, memif enable, ret %d\n",
				__func__, id, ret);
		}

		/* disable interrupt */
		mtk_regmap_update_bits(afe->regmap, irq_data->irq_en_reg,
				       1, 0, irq_data->irq_en_shift);
		/* and clear pending IRQ */
		mtk_regmap_write(afe->regmap, irq_data->irq_clr_reg,
				 1 << irq_data->irq_clr_shift);
		return ret;
	default:
		return -EINVAL;
	}
}
EXPORT_SYMBOL_GPL(mtk_afe_fe_trigger);

int mtk_afe_fe_prepare(struct snd_pcm_substream *substream,
		       struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd  = asoc_substream_to_rtd(substream);
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);
	int id = asoc_rtd_to_cpu(rtd, 0)->id;
	int pbuf_size;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		if (afe->get_memif_pbuf_size) {
			pbuf_size = afe->get_memif_pbuf_size(substream);
			mtk_memif_set_pbuf_size(afe, id, pbuf_size);
		}
	}
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

int mtk_afe_suspend(struct snd_soc_component *component)
{
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(component);
	struct device *dev = afe->dev;
	struct regmap *regmap = afe->regmap;
	int i;

	if (pm_runtime_status_suspended(dev) || afe->suspended)
		return 0;

	if (!afe->reg_back_up)
		afe->reg_back_up =
			devm_kcalloc(dev, afe->reg_back_up_list_num,
				     sizeof(unsigned int), GFP_KERNEL);

	if (afe->reg_back_up) {
		for (i = 0; i < afe->reg_back_up_list_num; i++)
			regmap_read(regmap, afe->reg_back_up_list[i],
				    &afe->reg_back_up[i]);
	}

	afe->suspended = true;
	afe->runtime_suspend(dev);
	return 0;
}
EXPORT_SYMBOL_GPL(mtk_afe_suspend);

int mtk_afe_resume(struct snd_soc_component *component)
{
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(component);
	struct device *dev = afe->dev;
	struct regmap *regmap = afe->regmap;
	int i;

	if (pm_runtime_status_suspended(dev) || !afe->suspended)
		return 0;

	afe->runtime_resume(dev);

	if (!afe->reg_back_up) {
		dev_dbg(dev, "%s no reg_backup\n", __func__);
	} else {
		for (i = 0; i < afe->reg_back_up_list_num; i++)
			mtk_regmap_write(regmap, afe->reg_back_up_list[i],
					 afe->reg_back_up[i]);
	}

	afe->suspended = false;
	return 0;
}
EXPORT_SYMBOL_GPL(mtk_afe_resume);

int mtk_memif_set_enable(struct mtk_base_afe *afe, int id)
{
	struct mtk_base_afe_memif *memif = &afe->memif[id];

	if (memif->data->enable_shift < 0) {
		dev_warn(afe->dev, "%s(), error, id %d, enable_shift < 0\n",
			 __func__, id);
		return 0;
	}
	return mtk_regmap_update_bits(afe->regmap, memif->data->enable_reg,
				      1, 1, memif->data->enable_shift);
}
EXPORT_SYMBOL_GPL(mtk_memif_set_enable);

int mtk_memif_set_disable(struct mtk_base_afe *afe, int id)
{
	struct mtk_base_afe_memif *memif = &afe->memif[id];

	if (memif->data->enable_shift < 0) {
		dev_warn(afe->dev, "%s(), error, id %d, enable_shift < 0\n",
			 __func__, id);
		return 0;
	}
	return mtk_regmap_update_bits(afe->regmap, memif->data->enable_reg,
				      1, 0, memif->data->enable_shift);
}
EXPORT_SYMBOL_GPL(mtk_memif_set_disable);

int mtk_memif_set_addr(struct mtk_base_afe *afe, int id,
		       unsigned char *dma_area,
		       dma_addr_t dma_addr,
		       size_t dma_bytes)
{
	struct mtk_base_afe_memif *memif = &afe->memif[id];
	int msb_at_bit33 = upper_32_bits(dma_addr) ? 1 : 0;
	unsigned int phys_buf_addr = lower_32_bits(dma_addr);
	unsigned int phys_buf_addr_upper_32 = upper_32_bits(dma_addr);

	memif->dma_area = dma_area;
	memif->dma_addr = dma_addr;
	memif->dma_bytes = dma_bytes;

	/* start */
	mtk_regmap_write(afe->regmap, memif->data->reg_ofs_base,
			 phys_buf_addr);
	/* end */
	if (memif->data->reg_ofs_end)
		mtk_regmap_write(afe->regmap,
				 memif->data->reg_ofs_end,
				 phys_buf_addr + dma_bytes - 1);
	else
		mtk_regmap_write(afe->regmap,
				 memif->data->reg_ofs_base +
				 AFE_BASE_END_OFFSET,
				 phys_buf_addr + dma_bytes - 1);

	/* set start, end, upper 32 bits */
	if (memif->data->reg_ofs_base_msb) {
		mtk_regmap_write(afe->regmap, memif->data->reg_ofs_base_msb,
				 phys_buf_addr_upper_32);
		mtk_regmap_write(afe->regmap,
				 memif->data->reg_ofs_end_msb,
				 phys_buf_addr_upper_32);
	}

	/*
	 * set MSB to 33-bit, for memif address
	 * only for memif base address, if msb_end_reg exists
	 */
	if (memif->data->msb_reg)
		mtk_regmap_update_bits(afe->regmap, memif->data->msb_reg,
				       1, msb_at_bit33, memif->data->msb_shift);

	/* set MSB to 33-bit, for memif end address */
	if (memif->data->msb_end_reg)
		mtk_regmap_update_bits(afe->regmap, memif->data->msb_end_reg,
				       1, msb_at_bit33,
				       memif->data->msb_end_shift);

	return 0;
}
EXPORT_SYMBOL_GPL(mtk_memif_set_addr);

int mtk_memif_set_channel(struct mtk_base_afe *afe,
			  int id, unsigned int channel)
{
	struct mtk_base_afe_memif *memif = &afe->memif[id];
	unsigned int mono;

	if (memif->data->mono_shift < 0)
		return 0;

	if (memif->data->quad_ch_mask) {
		unsigned int quad_ch = (channel == 4) ? 1 : 0;

		mtk_regmap_update_bits(afe->regmap, memif->data->quad_ch_reg,
				       memif->data->quad_ch_mask,
				       quad_ch, memif->data->quad_ch_shift);
	}

	if (memif->data->mono_invert)
		mono = (channel == 1) ? 0 : 1;
	else
		mono = (channel == 1) ? 1 : 0;

	/* for specific configuration of memif mono mode */
	if (memif->data->int_odd_flag_reg)
		mtk_regmap_update_bits(afe->regmap,
				       memif->data->int_odd_flag_reg,
				       1, mono,
				       memif->data->int_odd_flag_shift);

	return mtk_regmap_update_bits(afe->regmap, memif->data->mono_reg,
				      1, mono, memif->data->mono_shift);
}
EXPORT_SYMBOL_GPL(mtk_memif_set_channel);

static int mtk_memif_set_rate_fs(struct mtk_base_afe *afe,
				 int id, int fs)
{
	struct mtk_base_afe_memif *memif = &afe->memif[id];

	if (memif->data->fs_shift >= 0)
		mtk_regmap_update_bits(afe->regmap, memif->data->fs_reg,
				       memif->data->fs_maskbit,
				       fs, memif->data->fs_shift);

	return 0;
}

int mtk_memif_set_rate(struct mtk_base_afe *afe,
		       int id, unsigned int rate)
{
	int fs = 0;

	if (!afe->get_dai_fs) {
		dev_err(afe->dev, "%s(), error, afe->get_dai_fs == NULL\n",
			__func__);
		return -EINVAL;
	}

	fs = afe->get_dai_fs(afe, id, rate);

	if (fs < 0)
		return -EINVAL;

	return mtk_memif_set_rate_fs(afe, id, fs);
}
EXPORT_SYMBOL_GPL(mtk_memif_set_rate);

int mtk_memif_set_rate_substream(struct snd_pcm_substream *substream,
				 int id, unsigned int rate)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct snd_soc_component *component =
		snd_soc_rtdcom_lookup(rtd, AFE_PCM_NAME);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(component);

	int fs = 0;

	if (!afe->memif_fs) {
		dev_err(afe->dev, "%s(), error, afe->memif_fs == NULL\n",
			__func__);
		return -EINVAL;
	}

	fs = afe->memif_fs(substream, rate);

	if (fs < 0)
		return -EINVAL;

	return mtk_memif_set_rate_fs(afe, id, fs);
}
EXPORT_SYMBOL_GPL(mtk_memif_set_rate_substream);

int mtk_memif_set_format(struct mtk_base_afe *afe,
			 int id, snd_pcm_format_t format)
{
	struct mtk_base_afe_memif *memif = &afe->memif[id];
	int hd_audio = 0;
	int hd_align = 0;

	/* set hd mode */
	switch (format) {
	case SNDRV_PCM_FORMAT_S16_LE:
	case SNDRV_PCM_FORMAT_U16_LE:
		hd_audio = 0;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
	case SNDRV_PCM_FORMAT_U32_LE:
		if (afe->memif_32bit_supported) {
			hd_audio = 2;
			hd_align = 0;
		} else {
			hd_audio = 1;
			hd_align = 1;
		}
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
	case SNDRV_PCM_FORMAT_U24_LE:
		hd_audio = 1;
		break;
	default:
		dev_err(afe->dev, "%s() error: unsupported format %d\n",
			__func__, format);
		break;
	}

	mtk_regmap_update_bits(afe->regmap, memif->data->hd_reg,
			       0x3, hd_audio, memif->data->hd_shift);

	mtk_regmap_update_bits(afe->regmap, memif->data->hd_align_reg,
			       0x1, hd_align, memif->data->hd_align_mshift);

	return 0;
}
EXPORT_SYMBOL_GPL(mtk_memif_set_format);

int mtk_memif_set_pbuf_size(struct mtk_base_afe *afe,
			    int id, int pbuf_size)
{
	const struct mtk_base_memif_data *memif_data = afe->memif[id].data;

	if (memif_data->pbuf_mask == 0 || memif_data->minlen_mask == 0)
		return 0;

	mtk_regmap_update_bits(afe->regmap, memif_data->pbuf_reg,
			       memif_data->pbuf_mask,
			       pbuf_size, memif_data->pbuf_shift);

	mtk_regmap_update_bits(afe->regmap, memif_data->minlen_reg,
			       memif_data->minlen_mask,
			       pbuf_size, memif_data->minlen_shift);
	return 0;
}
EXPORT_SYMBOL_GPL(mtk_memif_set_pbuf_size);

MODULE_DESCRIPTION("Mediatek simple fe dai operator");
MODULE_AUTHOR("Garlic Tseng <garlic.tseng@mediatek.com>");
MODULE_LICENSE("GPL v2");
