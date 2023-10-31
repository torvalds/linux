// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2019 BayLibre, SAS.
// Author: Jerome Brunet <jbrunet@baylibre.com>

#include <linux/module.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>

#include "meson-codec-glue.h"

static struct snd_soc_dapm_widget *
meson_codec_glue_get_input(struct snd_soc_dapm_widget *w)
{
	struct snd_soc_dapm_path *p;
	struct snd_soc_dapm_widget *in;

	snd_soc_dapm_widget_for_each_source_path(w, p) {
		if (!p->connect)
			continue;

		/* Check that we still are in the same component */
		if (snd_soc_dapm_to_component(w->dapm) !=
		    snd_soc_dapm_to_component(p->source->dapm))
			continue;

		if (p->source->id == snd_soc_dapm_dai_in)
			return p->source;

		in = meson_codec_glue_get_input(p->source);
		if (in)
			return in;
	}

	return NULL;
}

static void meson_codec_glue_input_set_data(struct snd_soc_dai *dai,
					    struct meson_codec_glue_input *data)
{
	snd_soc_dai_dma_data_set_playback(dai, data);
}

struct meson_codec_glue_input *
meson_codec_glue_input_get_data(struct snd_soc_dai *dai)
{
	return snd_soc_dai_dma_data_get_playback(dai);
}
EXPORT_SYMBOL_GPL(meson_codec_glue_input_get_data);

static struct meson_codec_glue_input *
meson_codec_glue_output_get_input_data(struct snd_soc_dapm_widget *w)
{
	struct snd_soc_dapm_widget *in =
		meson_codec_glue_get_input(w);
	struct snd_soc_dai *dai;

	if (WARN_ON(!in))
		return NULL;

	dai = in->priv;

	return meson_codec_glue_input_get_data(dai);
}

int meson_codec_glue_input_hw_params(struct snd_pcm_substream *substream,
				     struct snd_pcm_hw_params *params,
				     struct snd_soc_dai *dai)
{
	struct meson_codec_glue_input *data =
		meson_codec_glue_input_get_data(dai);

	data->params.rates = snd_pcm_rate_to_rate_bit(params_rate(params));
	data->params.rate_min = params_rate(params);
	data->params.rate_max = params_rate(params);
	data->params.formats = 1ULL << (__force int) params_format(params);
	data->params.channels_min = params_channels(params);
	data->params.channels_max = params_channels(params);
	data->params.sig_bits = dai->driver->playback.sig_bits;

	return 0;
}
EXPORT_SYMBOL_GPL(meson_codec_glue_input_hw_params);

int meson_codec_glue_input_set_fmt(struct snd_soc_dai *dai,
				   unsigned int fmt)
{
	struct meson_codec_glue_input *data =
		meson_codec_glue_input_get_data(dai);

	/* Save the source stream format for the downstream link */
	data->fmt = fmt;
	return 0;
}
EXPORT_SYMBOL_GPL(meson_codec_glue_input_set_fmt);

int meson_codec_glue_output_startup(struct snd_pcm_substream *substream,
				    struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dapm_widget *w = snd_soc_dai_get_widget_capture(dai);
	struct meson_codec_glue_input *in_data = meson_codec_glue_output_get_input_data(w);

	if (!in_data)
		return -ENODEV;

	if (WARN_ON(!rtd->dai_link->c2c_params)) {
		dev_warn(dai->dev, "codec2codec link expected\n");
		return -EINVAL;
	}

	/* Replace link params with the input params */
	rtd->dai_link->c2c_params = &in_data->params;
	rtd->dai_link->num_c2c_params = 1;

	return snd_soc_runtime_set_dai_fmt(rtd, in_data->fmt);
}
EXPORT_SYMBOL_GPL(meson_codec_glue_output_startup);

int meson_codec_glue_input_dai_probe(struct snd_soc_dai *dai)
{
	struct meson_codec_glue_input *data;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	meson_codec_glue_input_set_data(dai, data);
	return 0;
}
EXPORT_SYMBOL_GPL(meson_codec_glue_input_dai_probe);

int meson_codec_glue_input_dai_remove(struct snd_soc_dai *dai)
{
	struct meson_codec_glue_input *data =
		meson_codec_glue_input_get_data(dai);

	kfree(data);
	return 0;
}
EXPORT_SYMBOL_GPL(meson_codec_glue_input_dai_remove);

MODULE_AUTHOR("Jerome Brunet <jbrunet@baylibre.com>");
MODULE_DESCRIPTION("Amlogic Codec Glue Helpers");
MODULE_LICENSE("GPL v2");

