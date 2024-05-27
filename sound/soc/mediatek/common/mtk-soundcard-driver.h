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
struct snd_pcm_hw_constraint_list;

enum mtk_pcm_constraint_type {
	MTK_CONSTRAINT_PLAYBACK,
	MTK_CONSTRAINT_CAPTURE,
	MTK_CONSTRAINT_HDMIDP,
	MTK_CONSTRAINT_MAX
};

struct mtk_pcm_constraints_data {
	const struct snd_pcm_hw_constraint_list *channels;
	const struct snd_pcm_hw_constraint_list *rates;
};

struct mtk_platform_card_data {
	struct snd_soc_card *card;
	struct snd_soc_jack *jacks;
	const struct mtk_pcm_constraints_data *pcm_constraints;
	u8 num_jacks;
	u8 num_pcm_constraints;
	u8 flags;
};

struct mtk_soundcard_pdata {
	const char *card_name;
	struct mtk_platform_card_data *card_data;
	const struct mtk_sof_priv *sof_priv;

	int (*soc_probe)(struct mtk_soc_card_data *card_data, bool legacy);
};

/* Common playback/capture card startup ops */
extern const struct snd_soc_ops mtk_soundcard_common_playback_ops;
extern const struct snd_soc_ops mtk_soundcard_common_capture_ops;

/* Exported for custom/extended soundcard startup ops */
int mtk_soundcard_startup(struct snd_pcm_substream *substream,
			  enum mtk_pcm_constraint_type ctype);

int parse_dai_link_info(struct snd_soc_card *card);
void clean_card_reference(struct snd_soc_card *card);
int mtk_soundcard_common_probe(struct platform_device *pdev);
#endif
