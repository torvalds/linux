/* SPDX-License-Identifier: GPL-2.0 */
/*
 * mtk-afe-platform-driver.h  --  Mediatek afe platform driver definition
 *
 * Copyright (c) 2016 MediaTek Inc.
 * Author: Garlic Tseng <garlic.tseng@mediatek.com>
 */

#ifndef _MTK_AFE_PLATFORM_DRIVER_H_
#define _MTK_AFE_PLATFORM_DRIVER_H_

#define AFE_PCM_NAME "mtk-afe-pcm"
extern const struct snd_soc_component_driver mtk_afe_pcm_platform;

struct mtk_base_afe;
struct snd_soc_component;

int mtk_afe_combine_sub_dai(struct mtk_base_afe *afe);
int mtk_afe_add_sub_dai_control(struct snd_soc_component *component);
#endif

