/*
 * mtk-afe-fe-dais.h  --  Mediatek afe fe dai operator definition
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

#ifndef _MTK_AFE_FE_DAI_H_
#define _MTK_AFE_FE_DAI_H_

struct snd_soc_dai_ops;
struct mtk_base_afe;
struct mtk_base_afe_memif;

int mtk_afe_fe_startup(struct snd_pcm_substream *substream,
		       struct snd_soc_dai *dai);
void mtk_afe_fe_shutdown(struct snd_pcm_substream *substream,
			 struct snd_soc_dai *dai);
int mtk_afe_fe_hw_params(struct snd_pcm_substream *substream,
			 struct snd_pcm_hw_params *params,
			 struct snd_soc_dai *dai);
int mtk_afe_fe_hw_free(struct snd_pcm_substream *substream,
		       struct snd_soc_dai *dai);
int mtk_afe_fe_prepare(struct snd_pcm_substream *substream,
		       struct snd_soc_dai *dai);
int mtk_afe_fe_trigger(struct snd_pcm_substream *substream, int cmd,
		       struct snd_soc_dai *dai);

extern const struct snd_soc_dai_ops mtk_afe_fe_ops;

int mtk_dynamic_irq_acquire(struct mtk_base_afe *afe);
int mtk_dynamic_irq_release(struct mtk_base_afe *afe, int irq_id);
int mtk_afe_dai_suspend(struct snd_soc_dai *dai);
int mtk_afe_dai_resume(struct snd_soc_dai *dai);

#endif
