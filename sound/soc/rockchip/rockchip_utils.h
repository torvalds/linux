/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Rockchip Utils API
 *
 * Copyright (c) 2023 Rockchip Electronics Co. Ltd.
 */

#ifndef _ROCKCHIP_UTILS_H
#define _ROCKCHIP_UTILS_H

void rockchip_utils_get_performance(struct snd_pcm_substream *substream,
				    struct snd_pcm_hw_params *params,
				    struct snd_soc_dai *dai,
				    int fifo_word);
void rockchip_utils_put_performance(struct snd_pcm_substream *substream,
				    struct snd_soc_dai *dai);

#endif /* _ROCKCHIP_UTILS_H */
