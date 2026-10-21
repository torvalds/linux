// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2026 BayLibre, SAS.
// Author: Valerio Setti <vsetti@baylibre.com>

#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>

#include "aiu.h"
#include "gx-formatter.h"

#define AIU_I2S_SOURCE_DESC_MODE_8CH	BIT(0)
#define AIU_I2S_SOURCE_DESC_MODE_24BIT	BIT(5)
#define AIU_I2S_SOURCE_DESC_MODE_32BIT	BIT(9)

#define AIU_I2S_DAC_CFG_MSB_FIRST	BIT(2)

static struct snd_soc_dai *
aiu_formatter_i2s_get_be(struct snd_soc_dapm_widget *w)
{
	struct snd_soc_dapm_path *p;
	struct snd_soc_dai *be;

	snd_soc_dapm_widget_for_each_sink_path(w, p) {
		if (!p->connect)
			continue;

		if (p->sink->id == snd_soc_dapm_dai_in)
			return (struct snd_soc_dai *)p->sink->priv;

		be = aiu_formatter_i2s_get_be(p->sink);
		if (be)
			return be;
	}

	return NULL;
}

static struct gx_stream *
aiu_formatter_i2s_get_stream(struct snd_soc_dapm_widget *w)
{
	struct snd_soc_dai *be = aiu_formatter_i2s_get_be(w);

	if (!be)
		return NULL;

	return snd_soc_dai_dma_data_get_playback(be);
}

static int aiu_formatter_i2s_prepare(struct regmap *map,
				 const struct gx_formatter_hw *quirks,
				 struct gx_stream *ts)
{
	/* Always operate in split (classic interleaved) mode */
	unsigned int desc = 0;

	/*
	 * Pipeline reset is already implemented in aiu_fifo_i2s_trigger() at
	 * trigger time.
	 */

	switch (ts->physical_width) {
	case 16: /* Nothing to do */
		break;

	case 32:
		desc |= (AIU_I2S_SOURCE_DESC_MODE_24BIT |
			 AIU_I2S_SOURCE_DESC_MODE_32BIT);
		break;

	default:
		return -EINVAL;
	}

	switch (ts->channels) {
	case 2: /* Nothing to do */
		break;
	case 8:
		desc |= AIU_I2S_SOURCE_DESC_MODE_8CH;
		break;
	default:
		return -EINVAL;
	}

	regmap_update_bits(map, AIU_I2S_SOURCE_DESC,
				AIU_I2S_SOURCE_DESC_MODE_8CH |
				AIU_I2S_SOURCE_DESC_MODE_24BIT |
				AIU_I2S_SOURCE_DESC_MODE_32BIT,
				desc);

	/* Send data MSB first */
	regmap_update_bits(map, AIU_I2S_DAC_CFG,
				AIU_I2S_DAC_CFG_MSB_FIRST,
				AIU_I2S_DAC_CFG_MSB_FIRST);

	return 0;
}

const struct gx_formatter_ops aiu_formatter_i2s_ops = {
	.get_stream	= aiu_formatter_i2s_get_stream,
	.prepare	= aiu_formatter_i2s_prepare,
};
