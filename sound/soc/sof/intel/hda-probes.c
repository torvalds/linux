// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2019-2021 Intel Corporation. All rights reserved.
//
// Author: Cezary Rojewski <cezary.rojewski@intel.com>
// Converted to SOF client:
//  Ranjani Sridharan <ranjani.sridharan@linux.intel.com>
//  Peter Ujfalusi <peter.ujfalusi@linux.intel.com>
//

#include <linux/module.h>
#include <sound/hdaudio_ext.h>
#include <sound/soc.h>
#include "../sof-priv.h"
#include "../sof-client-probes.h"
#include "../sof-client.h"
#include "hda.h"

static inline struct hdac_ext_stream *
hda_compr_get_stream(struct snd_compr_stream *cstream)
{
	return cstream->runtime->private_data;
}

static int hda_probes_compr_startup(struct sof_client_dev *cdev,
				    struct snd_compr_stream *cstream,
				    struct snd_soc_dai *dai, u32 *stream_id)
{
	struct snd_sof_dev *sdev = sof_client_dev_to_sof_dev(cdev);
	struct hdac_ext_stream *hext_stream;

	hext_stream = hda_dsp_stream_get(sdev, cstream->direction, 0);
	if (!hext_stream)
		return -EBUSY;

	hdac_stream(hext_stream)->curr_pos = 0;
	hdac_stream(hext_stream)->cstream = cstream;
	cstream->runtime->private_data = hext_stream;

	*stream_id = hdac_stream(hext_stream)->stream_tag;

	return 0;
}

static int hda_probes_compr_shutdown(struct sof_client_dev *cdev,
				     struct snd_compr_stream *cstream,
				     struct snd_soc_dai *dai)
{
	struct hdac_ext_stream *hext_stream = hda_compr_get_stream(cstream);
	struct snd_sof_dev *sdev = sof_client_dev_to_sof_dev(cdev);
	int ret;

	ret = hda_dsp_stream_put(sdev, cstream->direction,
				 hdac_stream(hext_stream)->stream_tag);
	if (ret < 0) {
		dev_dbg(sdev->dev, "stream put failed: %d\n", ret);
		return ret;
	}

	hdac_stream(hext_stream)->cstream = NULL;
	cstream->runtime->private_data = NULL;

	return 0;
}

static int hda_probes_compr_set_params(struct sof_client_dev *cdev,
				       struct snd_compr_stream *cstream,
				       struct snd_compr_params *params,
				       struct snd_soc_dai *dai)
{
	struct hdac_ext_stream *hext_stream = hda_compr_get_stream(cstream);
	struct snd_sof_dev *sdev = sof_client_dev_to_sof_dev(cdev);
	struct hdac_stream *hstream = hdac_stream(hext_stream);
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

	ret = hda_dsp_stream_hw_params(sdev, hext_stream, dmab, NULL);
	if (ret < 0) {
		dev_err(sdev->dev, "error: hdac prepare failed: %d\n", ret);
		return ret;
	}

	return 0;
}

static int hda_probes_compr_trigger(struct sof_client_dev *cdev,
				    struct snd_compr_stream *cstream,
				    int cmd, struct snd_soc_dai *dai)
{
	struct hdac_ext_stream *hext_stream = hda_compr_get_stream(cstream);
	struct snd_sof_dev *sdev = sof_client_dev_to_sof_dev(cdev);

	return hda_dsp_stream_trigger(sdev, hext_stream, cmd);
}

static int hda_probes_compr_pointer(struct sof_client_dev *cdev,
				    struct snd_compr_stream *cstream,
				    struct snd_compr_tstamp *tstamp,
				    struct snd_soc_dai *dai)
{
	struct hdac_ext_stream *hext_stream = hda_compr_get_stream(cstream);
	struct snd_soc_pcm_stream *pstream;

	pstream = &dai->driver->capture;
	tstamp->copied_total = hdac_stream(hext_stream)->curr_pos;
	tstamp->sampling_rate = snd_pcm_rate_bit_to_rate(pstream->rates);

	return 0;
}

/* SOF client implementation */
static const struct sof_probes_host_ops hda_probes_ops = {
	.startup = hda_probes_compr_startup,
	.shutdown = hda_probes_compr_shutdown,
	.set_params = hda_probes_compr_set_params,
	.trigger = hda_probes_compr_trigger,
	.pointer = hda_probes_compr_pointer,
};

int hda_probes_register(struct snd_sof_dev *sdev)
{
	return sof_client_dev_register(sdev, "hda-probes", 0, &hda_probes_ops,
				       sizeof(hda_probes_ops));
}

void hda_probes_unregister(struct snd_sof_dev *sdev)
{
	sof_client_dev_unregister(sdev, "hda-probes", 0);
}

MODULE_IMPORT_NS(SND_SOC_SOF_CLIENT);
