/* SPDX-License-Identifier: GPL-2.0 */
/*
 * mtk-dsp-sof-common.h  --  MediaTek dsp sof common definition
 *
 * Copyright (c) 2022 MediaTek Inc.
 * Author: Chunxu Li <chunxu.li@mediatek.com>
 */

#ifndef _MTK_DSP_SOF_COMMON_H_
#define _MTK_DSP_SOF_COMMON_H_

#include <sound/soc.h>

struct sof_conn_stream {
	const char *normal_link;
	const char *sof_link;
	const char *sof_dma;
	int stream_dir;
};

struct mtk_dai_link {
	const char *name;
	int (*be_hw_params_fixup)(struct snd_soc_pcm_runtime *rtd,
				  struct snd_pcm_hw_params *params);
	struct list_head list;
};

struct mtk_sof_priv {
	const struct sof_conn_stream *conn_streams;
	int num_streams;
	int (*sof_dai_link_fixup)(struct snd_soc_pcm_runtime *rtd,
				  struct snd_pcm_hw_params *params);
};

int mtk_sof_dai_link_fixup(struct snd_soc_pcm_runtime *rtd,
			   struct snd_pcm_hw_params *params);
int mtk_sof_card_probe(struct snd_soc_card *card);
int mtk_sof_card_late_probe(struct snd_soc_card *card);
int mtk_sof_dailink_parse_of(struct snd_soc_card *card, struct device_node *np,
			     const char *propname, struct snd_soc_dai_link *pre_dai_links,
			     int pre_num_links);

#endif
