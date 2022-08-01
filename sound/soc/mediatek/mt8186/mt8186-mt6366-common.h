/* SPDX-License-Identifier: GPL-2.0 */
/*
 * mt8186-mt6366-common.h
 *
 * Copyright (c) 2022 MediaTek Inc.
 * Author: Jiaxin Yu <jiaxin.yu@mediatek.com>
 */

#ifndef _MT8186_MT6366_COMMON_H_
#define _MT8186_MT6366_COMMON_H_

int mt8186_mt6366_init(struct snd_soc_pcm_runtime *rtd);
int mt8186_mt6366_card_set_be_link(struct snd_soc_card *card,
				   struct snd_soc_dai_link *link,
				   struct device_node *node,
				   char *link_name);
#endif
