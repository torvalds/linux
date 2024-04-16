// SPDX-License-Identifier: GPL-2.0
//
// mt8186-mt6366-common.c
//	--  MT8186 MT6366 ALSA common driver
//
// Copyright (c) 2022 MediaTek Inc.
// Author: Jiaxin Yu <jiaxin.yu@mediatek.com>
//
#include <sound/soc.h>

#include "../../codecs/mt6358.h"
#include "../common/mtk-afe-platform-driver.h"
#include "mt8186-afe-common.h"
#include "mt8186-mt6366-common.h"

int mt8186_mt6366_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_component *cmpnt_afe =
		snd_soc_rtdcom_lookup(rtd, AFE_PCM_NAME);
	struct snd_soc_component *cmpnt_codec =
		asoc_rtd_to_codec(rtd, 0)->component;
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(cmpnt_afe);
	struct mt8186_afe_private *afe_priv = afe->platform_priv;
	struct snd_soc_dapm_context *dapm = &rtd->card->dapm;
	int ret;

	/* set mtkaif protocol */
	mt6358_set_mtkaif_protocol(cmpnt_codec,
				   MT6358_MTKAIF_PROTOCOL_1);
	afe_priv->mtkaif_protocol = MT6358_MTKAIF_PROTOCOL_1;

	ret = snd_soc_dapm_sync(dapm);
	if (ret) {
		dev_err(rtd->dev, "failed to snd_soc_dapm_sync\n");
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(mt8186_mt6366_init);

int mt8186_mt6366_card_set_be_link(struct snd_soc_card *card,
				   struct snd_soc_dai_link *link,
				   struct device_node *node,
				   char *link_name)
{
	int ret;

	if (node && strcmp(link->name, link_name) == 0) {
		ret = snd_soc_of_get_dai_link_codecs(card->dev, node, link);
		if (ret < 0)
			return dev_err_probe(card->dev, ret, "get dai link codecs fail\n");
	}

	return 0;
}
EXPORT_SYMBOL_GPL(mt8186_mt6366_card_set_be_link);
