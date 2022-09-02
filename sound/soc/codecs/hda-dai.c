// SPDX-License-Identifier: GPL-2.0
//
// Copyright(c) 2021-2022 Intel Corporation. All rights reserved.
//
// Author: Cezary Rojewski <cezary.rojewski@intel.com>
//

#include <sound/soc.h>
#include <sound/hda_codec.h>
#include "hda.h"

static int hda_codec_dai_startup(struct snd_pcm_substream *substream, struct snd_soc_dai *dai)
{
	struct hda_pcm_stream *stream_info;
	struct hda_codec *codec;
	struct hda_pcm *pcm;
	int ret;

	codec = dev_to_hda_codec(dai->dev);
	stream_info = snd_soc_dai_get_dma_data(dai, substream);
	pcm = container_of(stream_info, struct hda_pcm, stream[substream->stream]);

	dev_dbg(dai->dev, "open stream codec: %08x, info: %p, pcm: %p %s substream: %p\n",
		codec->core.vendor_id, stream_info, pcm, pcm->name, substream);

	snd_hda_codec_pcm_get(pcm);

	ret = stream_info->ops.open(stream_info, codec, substream);
	if (ret < 0) {
		dev_err(dai->dev, "codec open failed: %d\n", ret);
		snd_hda_codec_pcm_put(pcm);
		return ret;
	}

	return 0;
}

static void hda_codec_dai_shutdown(struct snd_pcm_substream *substream, struct snd_soc_dai *dai)
{
	struct hda_pcm_stream *stream_info;
	struct hda_codec *codec;
	struct hda_pcm *pcm;
	int ret;

	codec = dev_to_hda_codec(dai->dev);
	stream_info = snd_soc_dai_get_dma_data(dai, substream);
	pcm = container_of(stream_info, struct hda_pcm, stream[substream->stream]);

	dev_dbg(dai->dev, "close stream codec: %08x, info: %p, pcm: %p %s substream: %p\n",
		codec->core.vendor_id, stream_info, pcm, pcm->name, substream);

	ret = stream_info->ops.close(stream_info, codec, substream);
	if (ret < 0)
		dev_err(dai->dev, "codec close failed: %d\n", ret);

	snd_hda_codec_pcm_put(pcm);
}

static int hda_codec_dai_hw_free(struct snd_pcm_substream *substream, struct snd_soc_dai *dai)
{
	struct hda_pcm_stream *stream_info;
	struct hda_codec *codec;

	codec = dev_to_hda_codec(dai->dev);
	stream_info = snd_soc_dai_get_dma_data(dai, substream);

	snd_hda_codec_cleanup(codec, stream_info, substream);

	return 0;
}

static int hda_codec_dai_prepare(struct snd_pcm_substream *substream, struct snd_soc_dai *dai)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct hda_pcm_stream *stream_info;
	struct hdac_stream *stream;
	struct hda_codec *codec;
	unsigned int format;
	int ret;

	codec = dev_to_hda_codec(dai->dev);
	stream = substream->runtime->private_data;
	stream_info = snd_soc_dai_get_dma_data(dai, substream);
	format = snd_hdac_calc_stream_format(runtime->rate, runtime->channels, runtime->format,
					     runtime->sample_bits, 0);

	ret = snd_hda_codec_prepare(codec, stream_info, stream->stream_tag, format, substream);
	if (ret < 0) {
		dev_err(dai->dev, "codec prepare failed: %d\n", ret);
		return ret;
	}

	return 0;
}

const struct snd_soc_dai_ops snd_soc_hda_codec_dai_ops = {
	.startup = hda_codec_dai_startup,
	.shutdown = hda_codec_dai_shutdown,
	.hw_free = hda_codec_dai_hw_free,
	.prepare = hda_codec_dai_prepare,
};
EXPORT_SYMBOL_GPL(snd_soc_hda_codec_dai_ops);
