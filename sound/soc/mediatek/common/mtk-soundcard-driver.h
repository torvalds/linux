/* SPDX-License-Identifier: GPL-2.0 */
/*
 * mtk-soundcard-driver.h  --  MediaTek soundcard driver common definition
 *
 * Copyright (c) 2022 MediaTek Inc.
 * Author: Trevor Wu <trevor.wu@mediatek.com>
 */

#ifndef _MTK_SOUNDCARD_DRIVER_H_
#define _MTK_SOUNDCARD_DRIVER_H_

struct mtk_sof_priv;
struct mtk_soc_card_data;

struct mtk_platform_card_data {
	struct snd_soc_card *card;
	struct snd_soc_jack *jacks;
	u8 num_jacks;
	u8 flags;
};

struct mtk_soundcard_pdata {
	const char *card_name;
	struct mtk_platform_card_data *card_data;
	struct mtk_sof_priv *sof_priv;
	int (*soc_probe)(struct mtk_soc_card_data *card_data, bool legacy);
};

int parse_dai_link_info(struct snd_soc_card *card);
void clean_card_reference(struct snd_soc_card *card);
int mtk_soundcard_common_probe(struct platform_device *pdev);
#endif
