/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ALSA ASoC interface for the Loongson platform
 *
 * Copyright (C) 2023 Loongson Technology Corporation Limited
 * Author: Yingkun Meng <mengyingkun@loongson.cn>
 */

#ifndef _LOONGSON_DMA_H
#define _LOONGSON_DMA_H

extern const struct snd_soc_component_driver loongson_i2s_idma_component;
extern const struct snd_soc_component_driver loongson_i2s_edma_component;
extern const struct snd_dmaengine_pcm_config loongson_dmaengine_pcm_config;

#endif
