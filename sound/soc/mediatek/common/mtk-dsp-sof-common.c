// SPDX-License-Identifier: GPL-2.0
/*
 * mtk-dsp-sof-common.c  --  MediaTek dsp sof common ctrl
 *
 * Copyright (c) 2022 MediaTek Inc.
 * Author: Chunxu Li <chunxu.li@mediatek.com>
 */

#include "mtk-dsp-sof-common.h"
#include "mtk-soc-card.h"

/* fixup the BE DAI link to match any values from topology */
int mtk_sof_dai_link_fixup(struct snd_soc_pcm_runtime *rtd,
			   struct snd_pcm_hw_params *params)
{
	struct snd_soc_card *card = rtd->card;
	struct mtk_soc_card_data *soc_card_data = snd_soc_card_get_drvdata(card);
	struct mtk_sof_priv *sof_priv = soc_card_data->sof_priv;
	int i, j, ret = 0;

	for (i = 0; i < sof_priv->num_streams; i++) {
		struct snd_soc_dai *cpu_dai;
		struct snd_soc_pcm_runtime *runtime;
		struct snd_soc_dai_link *sof_dai_link = NULL;
		const struct sof_conn_stream *conn = &sof_priv->conn_streams[i];

		if (conn->normal_link && strcmp(rtd->dai_link->name, conn->normal_link))
			continue;

		for_each_card_rtds(card, runtime) {
			if (strcmp(runtime->dai_link->name, conn->sof_link))
				continue;

			for_each_rtd_cpu_dais(runtime, j, cpu_dai) {
				if (snd_soc_dai_stream_active(cpu_dai, conn->stream_dir) > 0) {
					sof_dai_link = runtime->dai_link;
					break;
				}
			}
			break;
		}

		if (sof_dai_link && sof_dai_link->be_hw_params_fixup)
			ret = sof_dai_link->be_hw_params_fixup(runtime, params);

		break;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(mtk_sof_dai_link_fixup);

int mtk_sof_card_probe(struct snd_soc_card *card)
{
	int i;
	struct snd_soc_dai_link *dai_link;
	struct mtk_soc_card_data *soc_card_data = snd_soc_card_get_drvdata(card);
	struct mtk_sof_priv *sof_priv = soc_card_data->sof_priv;

	/* Set stream_name to help sof bind widgets */
	for_each_card_prelinks(card, i, dai_link) {
		if (dai_link->no_pcm && !dai_link->stream_name && dai_link->name)
			dai_link->stream_name = dai_link->name;
	}

	INIT_LIST_HEAD(&sof_priv->dai_link_list);

	return 0;
}
EXPORT_SYMBOL_GPL(mtk_sof_card_probe);

static struct snd_soc_pcm_runtime *mtk_sof_find_tplg_be(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_card *card = rtd->card;
	struct mtk_soc_card_data *soc_card_data = snd_soc_card_get_drvdata(card);
	struct mtk_sof_priv *sof_priv = soc_card_data->sof_priv;
	struct snd_soc_pcm_runtime *fe;
	struct snd_soc_pcm_runtime *be;
	struct snd_soc_dpcm *dpcm;
	int i, stream;

	for_each_pcm_streams(stream) {
		fe = NULL;
		for_each_dpcm_fe(rtd, stream, dpcm) {
			fe = dpcm->fe;
			if (fe)
				break;
		}

		if (!fe)
			continue;

		for_each_dpcm_be(fe, stream, dpcm) {
			be = dpcm->be;
			if (be == rtd)
				continue;

			for (i = 0; i < sof_priv->num_streams; i++) {
				const struct sof_conn_stream *conn = &sof_priv->conn_streams[i];

				if (!strcmp(be->dai_link->name, conn->sof_link))
					return be;
			}
		}
	}

	return NULL;
}

/* fixup the BE DAI link to match any values from topology */
static int mtk_sof_check_tplg_be_dai_link_fixup(struct snd_soc_pcm_runtime *rtd,
						struct snd_pcm_hw_params *params)
{
	struct snd_soc_card *card = rtd->card;
	struct mtk_soc_card_data *soc_card_data = snd_soc_card_get_drvdata(card);
	struct mtk_sof_priv *sof_priv = soc_card_data->sof_priv;
	struct snd_soc_pcm_runtime *sof_be;
	struct mtk_dai_link *dai_link;
	int ret = 0;

	sof_be = mtk_sof_find_tplg_be(rtd);
	if (sof_be) {
		if (sof_priv->sof_dai_link_fixup)
			ret = sof_priv->sof_dai_link_fixup(rtd, params);
		else if (sof_be->dai_link->be_hw_params_fixup)
			ret = sof_be->dai_link->be_hw_params_fixup(sof_be, params);
	} else {
		list_for_each_entry(dai_link, &sof_priv->dai_link_list, list) {
			if (strcmp(dai_link->name, rtd->dai_link->name) == 0) {
				if (dai_link->be_hw_params_fixup)
					ret = dai_link->be_hw_params_fixup(rtd, params);

				break;
			}
		}
	}

	return ret;
}

int mtk_sof_card_late_probe(struct snd_soc_card *card)
{
	struct snd_soc_pcm_runtime *rtd;
	struct snd_soc_component *sof_comp = NULL;
	struct mtk_soc_card_data *soc_card_data =
		snd_soc_card_get_drvdata(card);
	struct mtk_sof_priv *sof_priv = soc_card_data->sof_priv;
	struct snd_soc_dai_link *dai_link;
	struct mtk_dai_link *mtk_dai_link;
	int i;

	/* 1. find sof component */
	for_each_card_rtds(card, rtd) {
		sof_comp = snd_soc_rtdcom_lookup(rtd, "sof-audio-component");
		if (sof_comp)
			break;
	}

	if (!sof_comp) {
		dev_info(card->dev, "probe without sof-audio-component\n");
		return 0;
	}

	/* 2. overwrite all BE fixups, and backup the existing fixup */
	for_each_card_prelinks(card, i, dai_link) {
		if (dai_link->be_hw_params_fixup) {
			mtk_dai_link = devm_kzalloc(card->dev,
						    sizeof(*mtk_dai_link),
						    GFP_KERNEL);
			if (!mtk_dai_link)
				return -ENOMEM;

			mtk_dai_link->be_hw_params_fixup = dai_link->be_hw_params_fixup;
			mtk_dai_link->name = dai_link->name;

			list_add(&mtk_dai_link->list, &sof_priv->dai_link_list);
		}

		if (dai_link->no_pcm)
			dai_link->be_hw_params_fixup = mtk_sof_check_tplg_be_dai_link_fixup;
	}

	/* 3. add route path and SOF_BE fixup callback */
	for (i = 0; i < sof_priv->num_streams; i++) {
		const struct sof_conn_stream *conn = &sof_priv->conn_streams[i];
		struct snd_soc_pcm_runtime *sof_rtd = NULL;

		for_each_card_rtds(card, rtd) {
			if (!strcmp(rtd->dai_link->name, conn->sof_link)) {
				sof_rtd = rtd;
				break;
			}
		}
		if (sof_rtd) {
			int j;
			struct snd_soc_dai *cpu_dai;

			for_each_rtd_cpu_dais(sof_rtd, j, cpu_dai) {
				struct snd_soc_dapm_route route;
				struct snd_soc_dapm_path *p = NULL;
				struct snd_soc_dapm_widget *widget = snd_soc_dai_get_widget(cpu_dai, conn->stream_dir);

				memset(&route, 0, sizeof(route));
				if (conn->stream_dir == SNDRV_PCM_STREAM_CAPTURE && widget) {
					snd_soc_dapm_widget_for_each_sink_path(widget, p) {
						route.source = conn->sof_dma;
						route.sink = p->sink->name;
						snd_soc_dapm_add_routes(&card->dapm, &route, 1);
					}
				} else if (conn->stream_dir == SNDRV_PCM_STREAM_PLAYBACK && widget) {
					snd_soc_dapm_widget_for_each_source_path(widget, p) {
						route.source = p->source->name;
						route.sink = conn->sof_dma;
						snd_soc_dapm_add_routes(&card->dapm, &route, 1);
					}
				} else {
					dev_err(cpu_dai->dev, "stream dir and widget not pair\n");
				}
			}

			/* overwrite SOF BE fixup */
			sof_rtd->dai_link->be_hw_params_fixup =
				sof_comp->driver->be_hw_params_fixup;
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(mtk_sof_card_late_probe);

int mtk_sof_dailink_parse_of(struct snd_soc_card *card, struct device_node *np,
			     const char *propname, struct snd_soc_dai_link *pre_dai_links,
			     int pre_num_links)
{
	struct device *dev = card->dev;
	struct snd_soc_dai_link *parsed_dai_link;
	const char *dai_name = NULL;
	int i, j, ret, num_links, parsed_num_links = 0;

	num_links = of_property_count_strings(np, "mediatek,dai-link");
	if (num_links < 0 || num_links > card->num_links) {
		dev_dbg(dev, "number of dai-link is invalid\n");
		return -EINVAL;
	}

	parsed_dai_link = devm_kcalloc(dev, num_links, sizeof(*parsed_dai_link), GFP_KERNEL);
	if (!parsed_dai_link)
		return -ENOMEM;

	for (i = 0; i < num_links; i++) {
		ret = of_property_read_string_index(np, propname, i, &dai_name);
		if (ret) {
			dev_dbg(dev, "ASoC: Property '%s' index %d could not be read: %d\n",
				propname, i, ret);
			return ret;
		}
		dev_dbg(dev, "ASoC: Property get dai_name:%s\n", dai_name);
		for (j = 0; j < pre_num_links; j++) {
			if (!strcmp(dai_name, pre_dai_links[j].name)) {
				memcpy(&parsed_dai_link[parsed_num_links++], &pre_dai_links[j],
				       sizeof(struct snd_soc_dai_link));
				break;
			}
		}
	}

	if (parsed_num_links != num_links)
		return -EINVAL;

	card->dai_link = parsed_dai_link;
	card->num_links = parsed_num_links;

	return 0;
}
EXPORT_SYMBOL_GPL(mtk_sof_dailink_parse_of);
