// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2018 Intel Corporation. All rights reserved.
//
// Authors: Liam Girdwood <liam.r.girdwood@linux.intel.com>
//	    Ranjani Sridharan <ranjani.sridharan@linux.intel.com>
//	    Rander Wang <rander.wang@intel.com>
//          Keyon Jie <yang.jie@linux.intel.com>
//

/*
 * Hardware interface for generic Intel audio DSP HDA IP
 */

#include <sound/hda_register.h>
#include <sound/pcm_params.h>
#include "../ops.h"
#include "hda.h"

#define SDnFMT_BASE(x)	((x) << 14)
#define SDnFMT_MULT(x)	(((x) - 1) << 11)
#define SDnFMT_DIV(x)	(((x) - 1) << 8)
#define SDnFMT_BITS(x)	((x) << 4)
#define SDnFMT_CHAN(x)	((x) << 0)

static inline u32 get_mult_div(struct snd_sof_dev *sdev, int rate)
{
	switch (rate) {
	case 8000:
		return SDnFMT_DIV(6);
	case 9600:
		return SDnFMT_DIV(5);
	case 11025:
		return SDnFMT_BASE(1) | SDnFMT_DIV(4);
	case 16000:
		return SDnFMT_DIV(3);
	case 22050:
		return SDnFMT_BASE(1) | SDnFMT_DIV(2);
	case 32000:
		return SDnFMT_DIV(3) | SDnFMT_MULT(2);
	case 44100:
		return SDnFMT_BASE(1);
	case 48000:
		return 0;
	case 88200:
		return SDnFMT_BASE(1) | SDnFMT_MULT(2);
	case 96000:
		return SDnFMT_MULT(2);
	case 176400:
		return SDnFMT_BASE(1) | SDnFMT_MULT(4);
	case 192000:
		return SDnFMT_MULT(4);
	default:
		dev_warn(sdev->dev, "can't find div rate %d using 48kHz\n",
			 rate);
		return 0; /* use 48KHz if not found */
	}
};

static inline u32 get_bits(struct snd_sof_dev *sdev, int sample_bits)
{
	switch (sample_bits) {
	case 8:
		return SDnFMT_BITS(0);
	case 16:
		return SDnFMT_BITS(1);
	case 20:
		return SDnFMT_BITS(2);
	case 24:
		return SDnFMT_BITS(3);
	case 32:
		return SDnFMT_BITS(4);
	default:
		dev_warn(sdev->dev, "can't find %d bits using 16bit\n",
			 sample_bits);
		return SDnFMT_BITS(1); /* use 16bits format if not found */
	}
};

int hda_dsp_pcm_hw_params(struct snd_sof_dev *sdev,
			  struct snd_pcm_substream *substream,
			  struct snd_pcm_hw_params *params,
			  struct sof_ipc_stream_params *ipc_params)
{
	struct hdac_stream *hstream = substream->runtime->private_data;
	struct hdac_ext_stream *stream = stream_to_hdac_ext_stream(hstream);
	struct sof_intel_hda_dev *hda = sdev->pdata->hw_pdata;
	struct snd_dma_buffer *dmab;
	int ret;
	u32 size, rate, bits;

	size = params_buffer_bytes(params);
	rate = get_mult_div(sdev, params_rate(params));
	bits = get_bits(sdev, params_width(params));

	hstream->substream = substream;

	dmab = substream->runtime->dma_buffer_p;

	hstream->format_val = rate | bits | (params_channels(params) - 1);
	hstream->bufsize = size;
	hstream->period_bytes = params_period_bytes(params);
	hstream->no_period_wakeup  =
			(params->info & SNDRV_PCM_INFO_NO_PERIOD_WAKEUP) &&
			(params->flags & SNDRV_PCM_HW_PARAMS_NO_PERIOD_WAKEUP);

	ret = hda_dsp_stream_hw_params(sdev, stream, dmab, params);
	if (ret < 0) {
		dev_err(sdev->dev, "error: hdac prepare failed: %x\n", ret);
		return ret;
	}

	/* disable SPIB, to enable buffer wrap for stream */
	hda_dsp_stream_spib_config(sdev, stream, HDA_DSP_SPIB_DISABLE, 0);

	/* set host_period_bytes to 0 if no IPC position */
	if (hda && hda->no_ipc_position)
		ipc_params->host_period_bytes = 0;

	ipc_params->stream_tag = hstream->stream_tag;

	return 0;
}

int hda_dsp_pcm_trigger(struct snd_sof_dev *sdev,
			struct snd_pcm_substream *substream, int cmd)
{
	struct hdac_stream *hstream = substream->runtime->private_data;
	struct hdac_ext_stream *stream = stream_to_hdac_ext_stream(hstream);

	return hda_dsp_stream_trigger(sdev, stream, cmd);
}

snd_pcm_uframes_t hda_dsp_pcm_pointer(struct snd_sof_dev *sdev,
				      struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct hdac_stream *hstream = substream->runtime->private_data;
	struct sof_intel_hda_dev *hda = sdev->pdata->hw_pdata;
	struct snd_sof_pcm *spcm;
	snd_pcm_uframes_t pos;

	spcm = snd_sof_find_spcm_dai(sdev, rtd);
	if (!spcm) {
		dev_warn_ratelimited(sdev->dev, "warn: can't find PCM with DAI ID %d\n",
				     rtd->dai_link->id);
		return 0;
	}

	if (hda && !hda->no_ipc_position) {
		/* read position from IPC position */
		pos = spcm->stream[substream->stream].posn.host_posn;
		goto found;
	}

	/*
	 * DPIB/posbuf position mode:
	 * For Playback, Use DPIB register from HDA space which
	 * reflects the actual data transferred.
	 * For Capture, Use the position buffer for pointer, as DPIB
	 * is not accurate enough, its update may be completed
	 * earlier than the data written to DDR.
	 */
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		pos = snd_sof_dsp_read(sdev, HDA_DSP_HDA_BAR,
				       AZX_REG_VS_SDXDPIB_XBASE +
				       (AZX_REG_VS_SDXDPIB_XINTERVAL *
					hstream->index));
	} else {
		/*
		 * For capture stream, we need more workaround to fix the
		 * position incorrect issue:
		 *
		 * 1. Wait at least 20us before reading position buffer after
		 * the interrupt generated(IOC), to make sure position update
		 * happens on frame boundary i.e. 20.833uSec for 48KHz.
		 * 2. Perform a dummy Read to DPIB register to flush DMA
		 * position value.
		 * 3. Read the DMA Position from posbuf. Now the readback
		 * value should be >= period boundary.
		 */
		usleep_range(20, 21);
		snd_sof_dsp_read(sdev, HDA_DSP_HDA_BAR,
				 AZX_REG_VS_SDXDPIB_XBASE +
				 (AZX_REG_VS_SDXDPIB_XINTERVAL *
				  hstream->index));
		pos = snd_hdac_stream_get_pos_posbuf(hstream);
	}

	if (pos >= hstream->bufsize)
		pos = 0;

found:
	pos = bytes_to_frames(substream->runtime, pos);

	dev_vdbg(sdev->dev, "PCM: stream %d dir %d position %lu\n",
		 hstream->index, substream->stream, pos);
	return pos;
}

int hda_dsp_pcm_open(struct snd_sof_dev *sdev,
		     struct snd_pcm_substream *substream)
{
	struct hdac_ext_stream *dsp_stream;
	int direction = substream->stream;

	dsp_stream = hda_dsp_stream_get(sdev, direction);

	if (!dsp_stream) {
		dev_err(sdev->dev, "error: no stream available\n");
		return -ENODEV;
	}

	/* binding pcm substream to hda stream */
	substream->runtime->private_data = &dsp_stream->hstream;
	return 0;
}

int hda_dsp_pcm_close(struct snd_sof_dev *sdev,
		      struct snd_pcm_substream *substream)
{
	struct hdac_stream *hstream = substream->runtime->private_data;
	int direction = substream->stream;
	int ret;

	ret = hda_dsp_stream_put(sdev, direction, hstream->stream_tag);

	if (ret) {
		dev_dbg(sdev->dev, "stream %s not opened!\n", substream->name);
		return -ENODEV;
	}

	/* unbinding pcm substream to hda stream */
	substream->runtime->private_data = NULL;
	return 0;
}
