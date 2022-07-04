/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Rockchip DLP (Digital Loopback) driver
 *
 * Copyright (C) 2022 Rockchip Electronics Co., Ltd
 * Author: Sugar Zhang <sugar.zhang@rock-chips.com>
 *
 */

#ifndef _ROCKCHIP_DLP_H
#define _ROCKCHIP_DLP_H

struct snd_dlp_config {
	int (*get_fifo_count)(struct device *dev, int stream);
};

#if IS_REACHABLE(CONFIG_SND_SOC_ROCKCHIP_DLP)
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
