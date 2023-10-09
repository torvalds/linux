/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Rockchip DLP (Digital Loopback) PCM driver
 *
 * Copyright (C) 2022 Rockchip Electronics Co., Ltd
 * Author: Sugar Zhang <sugar.zhang@rock-chips.com>
 *
 */

#ifndef _ROCKCHIP_DLP_PCM_H
#define _ROCKCHIP_DLP_PCM_H

struct snd_dlp_config {
	int (*get_fifo_count)(struct device *dev, struct snd_pcm_substream *substream);
};

#if IS_REACHABLE(CONFIG_SND_SOC_ROCKCHIP_DLP_PCM)
int devm_snd_dmaengine_dlp_register(struct device *dev,
	const struct snd_dlp_config *config);
#else
static inline int devm_snd_dmaengine_dlp_register(struct device *dev,
	const struct snd_dlp_config *config)
{
	return -ENOSYS;
}
#endif

#endif
