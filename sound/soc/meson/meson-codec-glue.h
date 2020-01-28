/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (c) 2018 Baylibre SAS.
 * Author: Jerome Brunet <jbrunet@baylibre.com>
 */

#ifndef _MESON_CODEC_GLUE_H
#define _MESON_CODEC_GLUE_H

#include <sound/soc.h>

struct meson_codec_glue_input {
	struct snd_soc_pcm_stream params;
	unsigned int fmt;
};

struct snd_soc_dapm_widget *
meson_codec_glue_get_input(struct snd_soc_dapm_widget *w);

/* Input helpers */
struct meson_codec_glue_input *
meson_codec_glue_input_get_data(struct snd_soc_dai *dai);
void meson_codec_glue_input_set_data(struct snd_soc_dai *dai,
				     struct meson_codec_glue_input *data);
int meson_codec_glue_input_hw_params(struct snd_pcm_substream *substream,
				     struct snd_pcm_hw_params *params,
				     struct snd_soc_dai *dai);
int meson_codec_glue_input_set_fmt(struct snd_soc_dai *dai,
				   unsigned int fmt);

/* Output helpers */
struct meson_codec_glue_input *
meson_codec_glue_output_get_input_data(struct snd_soc_dapm_widget *w);
int meson_codec_glue_output_startup(struct snd_pcm_substream *substream,
				    struct snd_soc_dai *dai);

#endif /* _MESON_CODEC_GLUE_H */
