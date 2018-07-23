// SPDX-License-Identifier: GPL-2.0
/*
 * mtk-afe-platform-driver.c  --  Mediatek afe platform driver
 *
 * Copyright (c) 2016 MediaTek Inc.
 * Author: Garlic Tseng <garlic.tseng@mediatek.com>
 */

#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <sound/soc.h>

#include "mtk-afe-platform-driver.h"
#include "mtk-base-afe.h"

int mtk_afe_combine_sub_dai(struct mtk_base_afe *afe)
{
	struct snd_soc_dai_driver *sub_dai_drivers;
	size_t num_dai_drivers = 0, dai_idx = 0;
	int i;

	if (!afe->sub_dais) {
		dev_err(afe->dev, "%s(), sub_dais == NULL\n", __func__);
		return -EINVAL;
	}

	/* calcualte total dai driver size */
	for (i = 0; i < afe->num_sub_dais; i++) {
		if (afe->sub_dais[i].dai_drivers &&
		    afe->sub_dais[i].num_dai_drivers != 0)
			num_dai_drivers += afe->sub_dais[i].num_dai_drivers;
	}

	dev_info(afe->dev, "%s(), num of dai %zd\n", __func__, num_dai_drivers);

	/* combine sub_dais */
	afe->num_dai_drivers = num_dai_drivers;
	afe->dai_drivers = devm_kcalloc(afe->dev,
					num_dai_drivers,
					sizeof(struct snd_soc_dai_driver),
					GFP_KERNEL);
	if (!afe->dai_drivers)
		return -ENOMEM;

	for (i = 0; i < afe->num_sub_dais; i++) {
		if (afe->sub_dais[i].dai_drivers &&
		    afe->sub_dais[i].num_dai_drivers != 0) {
			sub_dai_drivers = afe->sub_dais[i].dai_drivers;
			/* dai driver */
			memcpy(&afe->dai_drivers[dai_idx],
			       sub_dai_drivers,
			       afe->sub_dais[i].num_dai_drivers *
			       sizeof(struct snd_soc_dai_driver));
			dai_idx += afe->sub_dais[i].num_dai_drivers;
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(mtk_afe_combine_sub_dai);

int mtk_afe_add_sub_dai_control(struct snd_soc_component *component)
{
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(component);
	int i;

	if (!afe->sub_dais) {
		dev_err(afe->dev, "%s(), sub_dais == NULL\n", __func__);
		return -EINVAL;
	}

	for (i = 0; i < afe->num_sub_dais; i++) {
		if (afe->sub_dais[i].controls)
			snd_soc_add_component_controls(component,
				afe->sub_dais[i].controls,
				afe->sub_dais[i].num_controls);

		if (afe->sub_dais[i].dapm_widgets)
			snd_soc_dapm_new_controls(&component->dapm,
				afe->sub_dais[i].dapm_widgets,
				afe->sub_dais[i].num_dapm_widgets);

		if (afe->sub_dais[i].dapm_routes)
			snd_soc_dapm_add_routes(&component->dapm,
				afe->sub_dais[i].dapm_routes,
				afe->sub_dais[i].num_dapm_routes);
	}

	snd_soc_dapm_new_widgets(component->dapm.card);

	return 0;

}
EXPORT_SYMBOL_GPL(mtk_afe_add_sub_dai_control);

static snd_pcm_uframes_t mtk_afe_pcm_pointer
			 (struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_component *component = snd_soc_rtdcom_lookup(rtd, AFE_PCM_NAME);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(component);
	struct mtk_base_afe_memif *memif = &afe->memif[rtd->cpu_dai->id];
	const struct mtk_base_memif_data *memif_data = memif->data;
	struct regmap *regmap = afe->regmap;
	struct device *dev = afe->dev;
	int reg_ofs_base = memif_data->reg_ofs_base;
	int reg_ofs_cur = memif_data->reg_ofs_cur;
	unsigned int hw_ptr = 0, hw_base = 0;
	int ret, pcm_ptr_bytes;

	ret = regmap_read(regmap, reg_ofs_cur, &hw_ptr);
	if (ret || hw_ptr == 0) {
		dev_err(dev, "%s hw_ptr err\n", __func__);
		pcm_ptr_bytes = 0;
		goto POINTER_RETURN_FRAMES;
	}

	ret = regmap_read(regmap, reg_ofs_base, &hw_base);
	if (ret || hw_base == 0) {
		dev_err(dev, "%s hw_ptr err\n", __func__);
		pcm_ptr_bytes = 0;
		goto POINTER_RETURN_FRAMES;
	}

	pcm_ptr_bytes = hw_ptr - hw_base;

POINTER_RETURN_FRAMES:
	return bytes_to_frames(substream->runtime, pcm_ptr_bytes);
}

const struct snd_pcm_ops mtk_afe_pcm_ops = {
	.ioctl = snd_pcm_lib_ioctl,
	.pointer = mtk_afe_pcm_pointer,
};
EXPORT_SYMBOL_GPL(mtk_afe_pcm_ops);

int mtk_afe_pcm_new(struct snd_soc_pcm_runtime *rtd)
{
	size_t size;
	struct snd_pcm *pcm = rtd->pcm;
	struct snd_soc_component *component = snd_soc_rtdcom_lookup(rtd, AFE_PCM_NAME);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(component);

	size = afe->mtk_afe_hardware->buffer_bytes_max;
	return snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_DEV,
						     afe->dev,
						     size, size);
}
EXPORT_SYMBOL_GPL(mtk_afe_pcm_new);

void mtk_afe_pcm_free(struct snd_pcm *pcm)
{
	snd_pcm_lib_preallocate_free_for_all(pcm);
}
EXPORT_SYMBOL_GPL(mtk_afe_pcm_free);

const struct snd_soc_component_driver mtk_afe_pcm_platform = {
	.name = AFE_PCM_NAME,
	.ops = &mtk_afe_pcm_ops,
	.pcm_new = mtk_afe_pcm_new,
	.pcm_free = mtk_afe_pcm_free,
};
EXPORT_SYMBOL_GPL(mtk_afe_pcm_platform);

MODULE_DESCRIPTION("Mediatek simple platform driver");
MODULE_AUTHOR("Garlic Tseng <garlic.tseng@mediatek.com>");
MODULE_LICENSE("GPL v2");

