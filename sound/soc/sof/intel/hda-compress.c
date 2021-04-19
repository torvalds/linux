// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2019-2020 Intel Corporation. All rights reserved.
//
// Author: Cezary Rojewski <cezary.rojewski@intel.com>
//

#include <sound/hdaudio_ext.h>
#include <sound/soc.h>
#include "../sof-priv.h"
#include "hda.h"

static inline struct hdac_ext_stream *
hda_compr_get_stream(struct snd_compr_stream *cstream)
{
	return cstream->runtime->private_data;
}

int hda_probe_compr_assign(struct snd_sof_dev *sdev,
			   struct snd_compr_stream *cstream,
			   struct snd_soc_dai *dai)
{
	struct hdac_ext_stream *stream;

	stream = hda_dsp_stream_get(sdev, cstream->direction, 0);
	if (!stream)
		return -EBUSY;

	hdac_stream(stream)->curr_pos = 0;
	hdac_stream(stream)->cstream = cstream;
	cstream->runtime->private_data = stream;

	return hdac_stream(stream)->stream_tag;
}

int hda_probe_compr_free(struct snd_sof_dev *sdev,
			 struct snd_compr_stream *cstream,
			 struct snd_soc_dai *dai)
{
	struct hdac_ext_stream *stream = hda_compr_get_stream(cstream);
	int ret;

	ret = hda_dsp_stream_put(sdev, cstream->direction,
				 hdac_stream(stream)->stream_tag);
	if (ret < 0) {
		dev_dbg(sdev->dev, "stream put failed: %d\n", ret);
		return ret;
	}

	hdac_stream(stream)->cstream = NULL;
	cstream->runtime->private_data = NULL;

	return 0;
}

int hda_probe_compr_set_params(struct snd_sof_dev *sdev,
			       struct snd_compr_stream *cstream,
			       struct snd_compr_params *params,
			       struct snd_soc_dai *dai)
{
	struct hdac_ext_stream *stream = hda_compr_get_stream(cstream);
	struct hdac_stream *hstream = hdac_stream(stream);
	struct snd_dma_buffer *dmab;
	u32 bits, rate;
	int bps, ret;

	dmab = cstream->runtime->dma_buffer_p;
	/* compr params do not store bit depth, default to S32_LE */
	bps = snd_pcm_format_physical_width(SNDRV_PCM_FORMAT_S32_LE);
	if (bps < 0)
		return bps;
	bits = hda_dsp_get_bits(sdev, bps);
	rate = hda_dsp_get_mult_div(sdev, params->codec.sample_rate);

	hstream->format_val = rate | bits | (params->codec.ch_out - 1);
	hstream->bufsize = cstream->runtime->buffer_size;
	hstream->period_bytes = cstream->runtime->fragment_size;
	hstream->no_period_wakeup = 0;

	ret = hda_dsp_stream_hw_params(sdev, stream, dmab, NULL);
	if (ret < 0) {
		dev_err(sdev->dev, "error: hdac prepare failed: %d\n", ret);
		return ret;
	}

	return 0;
}

int hda_probe_compr_trigger(struct snd_sof_dev *sdev,
			    struct snd_compr_stream *cstream, int cmd,
			    struct snd_soc_dai *dai)
{
	struct hdac_ext_stream *stream = hda_compr_get_stream(cstream);

	return hda_dsp_stream_trigger(sdev, stream, cmd);
}

int hda_probe_compr_pointer(struct snd_sof_dev *sdev,
			    struct snd_compr_stream *cstream,
			    struct snd_compr_tstamp *tstamp,
			    struct snd_soc_dai *dai)
{
	struct hdac_ext_stream *stream = hda_compr_get_stream(cstream);
	struct snd_soc_pcm_stream *pstream;

	pstream = &dai->driver->capture;
	tstamp->copied_total = hdac_stream(stream)->curr_pos;
	tstamp->sampling_rate = snd_pcm_rate_bit_to_rate(pstream->rates);

	return 0;
}
