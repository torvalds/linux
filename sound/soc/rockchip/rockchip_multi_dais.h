/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Rockchip MULTI DAIS driver
 *
 * Copyright (C) 2018 Fuzhou Rockchip Electronics Co., Ltd
 *
 */

#ifndef _ROCKCHIP_MULTI_DAIS_H
#define _ROCKCHIP_MULTI_DAIS_H

#define MAX_DAIS 4

struct rk_dai {
	struct device *dev;
	struct device_node *of_node;
	struct snd_soc_dai *dai;
	unsigned int fmt;
	unsigned int fmt_msk;
};

struct rk_mdais_dev {
	struct device *dev;
	struct rk_dai *dais;
	unsigned int *playback_channel_maps;
	unsigned int *capture_channel_maps;
	int num_dais;
};

int snd_dmaengine_mpcm_register(struct rk_mdais_dev *mdais);
void snd_dmaengine_mpcm_unregister(struct device *dev);

#endif
