// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2018 Intel Corporation
//
// Authors: Liam Girdwood <liam.r.girdwood@linux.intel.com>
//	    Ranjani Sridharan <ranjani.sridharan@linux.intel.com>
//	    Rander Wang <rander.wang@intel.com>
//          Keyon Jie <yang.jie@linux.intel.com>
//

/*
 * Hardware interface for generic Intel audio DSP HDA IP
 */

#include <linux/moduleparam.h>
#include <sound/hda_register.h>
#include <sound/pcm_params.h>
#include <trace/events/sof_intel.h>
#include "../sof-audio.h"
#include "../ops.h"
#include "hda.h"

#define SDnFMT_BASE(x)	((x) << 14)
#define SDnFMT_MULT(x)	(((x) - 1) << 11)
#define SDnFMT_DIV(x)	(((x) - 1) << 8)
#define SDnFMT_BITS(x)	((x) << 4)
#define SDnFMT_CHAN(x)	((x) << 0)

#define HDA_MAX_PERIOD_TIME_HEADROOM	10

static bool hda_always_enable_dmi_l1;
module_param_named(always_enable_dmi_l1, hda_always_enable_dmi_l1, bool, 0444);
MODULE_PARM_DESC(always_enable_dmi_l1, "SOF HDA always enable DMI l1");

static bool hda_disable_rewinds;
module_param_named(disable_rewinds, hda_disable_rewinds, bool, 0444);
MODULE_PARM_DESC(disable_rewinds, "SOF HDA disable rewinds");

static int hda_force_pause_support = -1;
module_param_named(force_pause_support, hda_force_pause_support, int, 0444);
MODULE_PARM_DESC(force_pause_support,
		 "Pause support: -1: Use default, 0: Disable, 1: Enable (default -1)");

u32 hda_dsp_get_mult_div(struct snd_sof_dev *sdev, int rate)
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

u32 hda_dsp_get_bits(struct snd_sof_dev *sdev, int sample_bits)
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
			  struct snd_sof_platform_stream_params *platform_params)
{
	struct hdac_stream *hstream = substream->runtime->private_data;
	struct hdac_ext_stream *hext_stream = stream_to_hdac_ext_stream(hstream);
	struct sof_intel_hda_dev *hda = sdev->pdata->hw_pdata;
	struct snd_dma_buffer *dmab;
	int ret;

	hstream->substream = substream;

	dmab = substream->runtime->dma_buffer_p;

	/*
	 * Use the codec required format val (which is link_bps adjusted) when
	 * the DSP is not in use
	 */
	if (!sdev->dspless_mode_selected) {
		u32 rate = hda_dsp_get_mult_div(sdev, params_rate(params));
		u32 bits = hda_dsp_get_bits(sdev, params_width(params));

		hstream->format_val = rate | bits | (params_channels(params) - 1);
	}

	hstream->bufsize = params_buffer_bytes(params);
	hstream->period_bytes = params_period_bytes(params);
	hstream->no_period_wakeup  =
			(params->info & SNDRV_PCM_INFO_NO_PERIOD_WAKEUP) &&
			(params->flags & SNDRV_PCM_HW_PARAMS_NO_PERIOD_WAKEUP);

	ret = hda_dsp_stream_hw_params(sdev, hext_stream, dmab, params);
	if (ret < 0) {
		dev_err(sdev->dev, "error: hdac prepare failed: %d\n", ret);
		return ret;
	}

	/* enable SPIB when rewinds are disabled */
	if (hda_disable_rewinds)
		hda_dsp_stream_spib_config(sdev, hext_stream, HDA_DSP_SPIB_ENABLE, 0);
	else
		hda_dsp_stream_spib_config(sdev, hext_stream, HDA_DSP_SPIB_DISABLE, 0);

	if (hda)
		platform_params->no_ipc_position = hda->no_ipc_position;

	platform_params->stream_tag = hstream->stream_tag;

	return 0;
}
EXPORT_SYMBOL_NS(hda_dsp_pcm_hw_params, "SND_SOC_SOF_INTEL_HDA_COMMON");

/* update SPIB register with appl position */
int hda_dsp_pcm_ack(struct snd_sof_dev *sdev, struct snd_pcm_substream *substream)
{
	struct hdac_stream *hstream = substream->runtime->private_data;
	struct snd_pcm_runtime *runtime = substream->runtime;
	ssize_t appl_pos, buf_size;
	u32 spib;

	appl_pos = frames_to_bytes(runtime, runtime->control->appl_ptr);
	buf_size = frames_to_bytes(runtime, runtime->buffer_size);

	spib = appl_pos % buf_size;

	/* Allowable value for SPIB is 1 byte to max buffer size */
	if (!spib)
		spib = buf_size;

	sof_io_write(sdev, hstream->spib_addr, spib);

	return 0;
}
EXPORT_SYMBOL_NS(hda_dsp_pcm_ack, "SND_SOC_SOF_INTEL_HDA_COMMON");

int hda_dsp_pcm_trigger(struct snd_sof_dev *sdev,
			struct snd_pcm_substream *substream, int cmd)
{
	struct hdac_stream *hstream = substream->runtime->private_data;
	struct hdac_ext_stream *hext_stream = stream_to_hdac_ext_stream(hstream);

	return hda_dsp_stream_trigger(sdev, hext_stream, cmd);
}
EXPORT_SYMBOL_NS(hda_dsp_pcm_trigger, "SND_SOC_SOF_INTEL_HDA_COMMON");

snd_pcm_uframes_t hda_dsp_pcm_pointer(struct snd_sof_dev *sdev,
				      struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_component *scomp = sdev->component;
	struct hdac_stream *hstream = substream->runtime->private_data;
	struct sof_intel_hda_dev *hda = sdev->pdata->hw_pdata;
	struct snd_sof_pcm *spcm;
	snd_pcm_uframes_t pos;

	spcm = snd_sof_find_spcm_dai(scomp, rtd);
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

	pos = hda_dsp_stream_get_position(hstream, substream->stream, true);
found:
	pos = bytes_to_frames(substream->runtime, pos);

	trace_sof_intel_hda_dsp_pcm(sdev, hstream, substream, pos);
	return pos;
}
EXPORT_SYMBOL_NS(hda_dsp_pcm_pointer, "SND_SOC_SOF_INTEL_HDA_COMMON");

int hda_dsp_pcm_open(struct snd_sof_dev *sdev,
		     struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_component *scomp = sdev->component;
	struct hdac_ext_stream *dsp_stream;
	struct snd_sof_pcm *spcm;
	int direction = substream->stream;
	u32 flags = 0;

	spcm = snd_sof_find_spcm_dai(scomp, rtd);
	if (!spcm) {
		dev_err(sdev->dev, "error: can't find PCM with DAI ID %d\n", rtd->dai_link->id);
		return -EINVAL;
	}

	/*
	 * if we want the .ack to work, we need to prevent the control from being mapped.
	 * The status can still be mapped.
	 */
	if (hda_disable_rewinds)
		runtime->hw.info |= SNDRV_PCM_INFO_NO_REWINDS | SNDRV_PCM_INFO_SYNC_APPLPTR;

	/*
	 * All playback streams are DMI L1 capable, capture streams need
	 * pause push/release to be disabled
	 */
	if (hda_always_enable_dmi_l1 && direction == SNDRV_PCM_STREAM_CAPTURE)
		runtime->hw.info &= ~SNDRV_PCM_INFO_PAUSE;

	/*
	 * Do not advertise the PAUSE support if it is forced to be disabled via
	 * module parameter or if the pause_supported is false for the PCM
	 * device
	 */
	if (hda_force_pause_support == 0 ||
	    (hda_force_pause_support == -1 &&
	     !spcm->stream[substream->stream].pause_supported))
		runtime->hw.info &= ~SNDRV_PCM_INFO_PAUSE;

	if (hda_always_enable_dmi_l1 ||
	    direction == SNDRV_PCM_STREAM_PLAYBACK ||
	    spcm->stream[substream->stream].d0i3_compatible)
		flags |= SOF_HDA_STREAM_DMI_L1_COMPATIBLE;

	dsp_stream = hda_dsp_stream_get(sdev, direction, flags);
	if (!dsp_stream) {
		dev_err(sdev->dev, "error: no stream available\n");
		return -ENODEV;
	}

	/* minimum as per HDA spec */
	snd_pcm_hw_constraint_step(substream->runtime, 0, SNDRV_PCM_HW_PARAM_PERIOD_BYTES, 4);

	/* avoid circular buffer wrap in middle of period */
	snd_pcm_hw_constraint_integer(substream->runtime,
				      SNDRV_PCM_HW_PARAM_PERIODS);

	/* Limit the maximum number of periods to not exceed the BDL entries count */
	if (runtime->hw.periods_max > HDA_DSP_MAX_BDL_ENTRIES)
		snd_pcm_hw_constraint_minmax(runtime, SNDRV_PCM_HW_PARAM_PERIODS,
					     runtime->hw.periods_min,
					     HDA_DSP_MAX_BDL_ENTRIES);

	/* Only S16 and S32 supported by HDA hardware when used without DSP */
	if (sdev->dspless_mode_selected)
		snd_pcm_hw_constraint_mask64(substream->runtime, SNDRV_PCM_HW_PARAM_FORMAT,
					     SNDRV_PCM_FMTBIT_S16 | SNDRV_PCM_FMTBIT_S32);

	/*
	 * The dsp_max_burst_size_in_ms is the length of the maximum burst size
	 * of the host DMA in the ALSA buffer.
	 *
	 * On playback start the DMA will transfer dsp_max_burst_size_in_ms
	 * amount of data in one initial burst to fill up the host DMA buffer.
	 * Consequent DMA burst sizes are shorter and their length can vary.
	 * To avoid immediate xrun by the initial burst we need to place
	 * constraint on the period size (via PERIOD_TIME) to cover the size of
	 * the host buffer.
	 * We need to add headroom of max 10ms as the firmware needs time to
	 * settle to the 1ms pacing and initially it can run faster for few
	 * internal periods.
	 *
	 * On capture the DMA will transfer 1ms chunks.
	 */
	if (spcm->stream[direction].dsp_max_burst_size_in_ms) {
		unsigned int period_time = spcm->stream[direction].dsp_max_burst_size_in_ms;

		/*
		 * add headroom over the maximum burst size to cover the time
		 * needed for the DMA pace to settle.
		 * Limit the headroom time to HDA_MAX_PERIOD_TIME_HEADROOM
		 */
		period_time += min(period_time, HDA_MAX_PERIOD_TIME_HEADROOM);

		snd_pcm_hw_constraint_minmax(substream->runtime,
			SNDRV_PCM_HW_PARAM_PERIOD_TIME,
			period_time * USEC_PER_MSEC,
			UINT_MAX);
	}

	/* binding pcm substream to hda stream */
	substream->runtime->private_data = &dsp_stream->hstream;

	/*
	 * Reset the llp cache values (they are used for LLP compensation in
	 * case the counter is not reset)
	 */
	dsp_stream->pplcllpl = 0;
	dsp_stream->pplcllpu = 0;

	return 0;
}
EXPORT_SYMBOL_NS(hda_dsp_pcm_open, "SND_SOC_SOF_INTEL_HDA_COMMON");

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
EXPORT_SYMBOL_NS(hda_dsp_pcm_close, "SND_SOC_SOF_INTEL_HDA_COMMON");
