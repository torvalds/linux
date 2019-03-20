// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2018 Intel Corporation. All rights reserved.
//
// Author: Liam Girdwood <liam.r.girdwood@linux.intel.com>
//
// PCM Layer, interface between ALSA and IPC.
//

#include <linux/pm_runtime.h>
#include <sound/pcm_params.h>
#include <sound/sof.h>
#include "sof-priv.h"
#include "ops.h"

#define DRV_NAME	"sof-audio-component"

/* Create DMA buffer page table for DSP */
static int create_page_table(struct snd_pcm_substream *substream,
			     unsigned char *dma_area, size_t size)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_component *component =
		snd_soc_rtdcom_lookup(rtd, DRV_NAME);
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(component);
	struct snd_sof_pcm *spcm;
	struct snd_dma_buffer *dmab = snd_pcm_get_dma_buf(substream);
	int stream = substream->stream;

	spcm = snd_sof_find_spcm_dai(sdev, rtd);
	if (!spcm)
		return -EINVAL;

	return snd_sof_create_page_table(sdev, dmab,
		spcm->stream[stream].page_table.area, size);
}

/* this may get called several times by oss emulation */
static int sof_pcm_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_component *component =
		snd_soc_rtdcom_lookup(rtd, DRV_NAME);
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(component);
	struct snd_sof_pcm *spcm;
	struct sof_ipc_pcm_params pcm;
	struct sof_ipc_pcm_params_reply ipc_params_reply;
	int posn_offset;
	int ret;

	/* nothing todo for BE */
	if (rtd->dai_link->no_pcm)
		return 0;

	spcm = snd_sof_find_spcm_dai(sdev, rtd);
	if (!spcm)
		return -EINVAL;

	dev_dbg(sdev->dev, "pcm: hw params stream %d dir %d\n",
		spcm->pcm.pcm_id, substream->stream);

	memset(&pcm, 0, sizeof(pcm));

	/* allocate audio buffer pages */
	ret = snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(params));
	if (ret < 0) {
		dev_err(sdev->dev, "error: could not allocate %d bytes for PCM %d\n",
			params_buffer_bytes(params), spcm->pcm.pcm_id);
		return ret;
	}

	/* create compressed page table for audio firmware */
	ret = create_page_table(substream, runtime->dma_area,
				runtime->dma_bytes);
	if (ret < 0)
		return ret;

	/* number of pages should be rounded up */
	pcm.params.buffer.pages = PFN_UP(runtime->dma_bytes);

	/* set IPC PCM parameters */
	pcm.hdr.size = sizeof(pcm);
	pcm.hdr.cmd = SOF_IPC_GLB_STREAM_MSG | SOF_IPC_STREAM_PCM_PARAMS;
	pcm.comp_id = spcm->stream[substream->stream].comp_id;
	pcm.params.hdr.size = sizeof(pcm.params);
	pcm.params.buffer.phy_addr =
		spcm->stream[substream->stream].page_table.addr;
	pcm.params.buffer.size = runtime->dma_bytes;
	pcm.params.direction = substream->stream;
	pcm.params.sample_valid_bytes = params_width(params) >> 3;
	pcm.params.buffer_fmt = SOF_IPC_BUFFER_INTERLEAVED;
	pcm.params.rate = params_rate(params);
	pcm.params.channels = params_channels(params);
	pcm.params.host_period_bytes = params_period_bytes(params);

	/* container size */
	switch (params_width(params)) {
	case 16:
		pcm.params.sample_container_bytes = 2;
		break;
	case 24:
		pcm.params.sample_container_bytes = 4;
		break;
	case 32:
		pcm.params.sample_container_bytes = 4;
		break;
	default:
		return -EINVAL;
	}

	/* format */
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16:
		pcm.params.frame_fmt = SOF_IPC_FRAME_S16_LE;
		break;
	case SNDRV_PCM_FORMAT_S24:
		pcm.params.frame_fmt = SOF_IPC_FRAME_S24_4LE;
		break;
	case SNDRV_PCM_FORMAT_S32:
		pcm.params.frame_fmt = SOF_IPC_FRAME_S32_LE;
		break;
	case SNDRV_PCM_FORMAT_FLOAT:
		pcm.params.frame_fmt = SOF_IPC_FRAME_FLOAT;
		break;
	default:
		return -EINVAL;
	}

	/* firmware already configured host stream */
	ret = snd_sof_pcm_platform_hw_params(sdev,
					     substream,
					     params,
					     &pcm.params);
	if (ret < 0) {
		dev_err(sdev->dev, "error: platform hw params failed\n");
		return ret;
	}

	dev_dbg(sdev->dev, "stream_tag %d", pcm.params.stream_tag);

	/* send IPC to the DSP */
	ret = sof_ipc_tx_message(sdev->ipc, pcm.hdr.cmd, &pcm, sizeof(pcm),
				 &ipc_params_reply, sizeof(ipc_params_reply));
	if (ret < 0) {
		dev_err(sdev->dev, "error: hw params ipc failed for stream %d\n",
			pcm.params.stream_tag);
		return ret;
	}

	/* validate offset */
	posn_offset = ipc_params_reply.posn_offset;

	/* check if offset is overflow or it is not aligned */
	if (posn_offset > sdev->stream_box.size ||
	    posn_offset % sizeof(struct sof_ipc_stream_posn) != 0) {
		dev_err(sdev->dev, "error: got wrong posn offset 0x%x for PCM %d\n",
			posn_offset, spcm->pcm.pcm_id);
		return -EINVAL;
	}
	spcm->posn_offset[substream->stream] =
		sdev->stream_box.offset + posn_offset;

	/* save pcm hw_params */
	memcpy(&spcm->params[substream->stream], params, sizeof(*params));

	/* unset restore_stream */
	spcm->restore_stream[substream->stream] = 0;

	return ret;
}

static int sof_pcm_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_component *component =
		snd_soc_rtdcom_lookup(rtd, DRV_NAME);
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(component);
	struct snd_sof_pcm *spcm;
	struct sof_ipc_stream stream;
	struct sof_ipc_reply reply;
	int ret;

	/* nothing todo for BE */
	if (rtd->dai_link->no_pcm)
		return 0;

	spcm = snd_sof_find_spcm_dai(sdev, rtd);
	if (!spcm)
		return -EINVAL;

	dev_dbg(sdev->dev, "pcm: free stream %d dir %d\n", spcm->pcm.pcm_id,
		substream->stream);

	stream.hdr.size = sizeof(stream);
	stream.hdr.cmd = SOF_IPC_GLB_STREAM_MSG | SOF_IPC_STREAM_PCM_FREE;
	stream.comp_id = spcm->stream[substream->stream].comp_id;

	/* send IPC to the DSP */
	ret = sof_ipc_tx_message(sdev->ipc, stream.hdr.cmd, &stream,
				 sizeof(stream), &reply, sizeof(reply));

	snd_pcm_lib_free_pages(substream);
	return ret;
}

static int sof_restore_hw_params(struct snd_pcm_substream *substream,
				 struct snd_sof_pcm *spcm,
				 struct snd_sof_dev *sdev)
{
	snd_pcm_uframes_t host;
	u64 host_posn;
	int ret;

	/* resume stream */
	host_posn = spcm->stream[substream->stream].posn.host_posn;
	host = bytes_to_frames(substream->runtime, host_posn);
	dev_dbg(sdev->dev,
		"PCM: resume stream %d dir %d DMA position %lu\n",
		spcm->pcm.pcm_id, substream->stream, host);

	/* set hw_params */
	ret = sof_pcm_hw_params(substream,
				&spcm->params[substream->stream]);
	if (ret < 0) {
		dev_err(sdev->dev,
			"error: set pcm hw_params after resume\n");
		return ret;
	}

	return 0;
}

/*
 * FE dai link trigger actions are always executed in non-atomic context because
 * they involve IPC's.
 */
static int sof_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_component *component =
		snd_soc_rtdcom_lookup(rtd, DRV_NAME);
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(component);
	struct snd_sof_pcm *spcm;
	struct sof_ipc_stream stream;
	struct sof_ipc_reply reply;
	int ret;

	/* nothing todo for BE */
	if (rtd->dai_link->no_pcm)
		return 0;

	spcm = snd_sof_find_spcm_dai(sdev, rtd);
	if (!spcm)
		return -EINVAL;

	dev_dbg(sdev->dev, "pcm: trigger stream %d dir %d cmd %d\n",
		spcm->pcm.pcm_id, substream->stream, cmd);

	stream.hdr.size = sizeof(stream);
	stream.hdr.cmd = SOF_IPC_GLB_STREAM_MSG;
	stream.comp_id = spcm->stream[substream->stream].comp_id;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		stream.hdr.cmd |= SOF_IPC_STREAM_TRIG_PAUSE;
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:

		/* check if the stream hw_params needs to be restored */
		if (spcm->restore_stream[substream->stream]) {

			/* restore hw_params */
			ret = sof_restore_hw_params(substream, spcm, sdev);
			if (ret < 0)
				return ret;

			/* unset restore_stream */
			spcm->restore_stream[substream->stream] = 0;

			/* trigger start */
			stream.hdr.cmd |= SOF_IPC_STREAM_TRIG_START;
		} else {

			/* trigger pause release */
			stream.hdr.cmd |= SOF_IPC_STREAM_TRIG_RELEASE;
		}
		break;
	case SNDRV_PCM_TRIGGER_START:
		/* fall through */
	case SNDRV_PCM_TRIGGER_RESUME:

		/* check if the stream hw_params needs to be restored */
		if (spcm->restore_stream[substream->stream]) {

			/* restore hw_params */
			ret = sof_restore_hw_params(substream, spcm, sdev);
			if (ret < 0)
				return ret;

			/* unset restore_stream */
			spcm->restore_stream[substream->stream] = 0;
		}

		/* trigger stream */
		stream.hdr.cmd |= SOF_IPC_STREAM_TRIG_START;

		break;
	case SNDRV_PCM_TRIGGER_SUSPEND:
		/* fallthrough */
	case SNDRV_PCM_TRIGGER_STOP:

		/* Check if stream was marked for restore before suspend */
		if (spcm->restore_stream[substream->stream]) {

			/* unset restore_stream */
			spcm->restore_stream[substream->stream] = 0;

			/* do not send ipc as the stream hasn't been set up */
			return 0;
		}

		stream.hdr.cmd |= SOF_IPC_STREAM_TRIG_STOP;
		break;
	default:
		dev_err(sdev->dev, "error: unhandled trigger cmd %d\n", cmd);
		return -EINVAL;
	}

	snd_sof_pcm_platform_trigger(sdev, substream, cmd);

	/* send IPC to the DSP */
	return sof_ipc_tx_message(sdev->ipc, stream.hdr.cmd, &stream,
				  sizeof(stream), &reply, sizeof(reply));
}

static snd_pcm_uframes_t sof_pcm_pointer(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_component *component =
		snd_soc_rtdcom_lookup(rtd, DRV_NAME);
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(component);
	struct snd_sof_pcm *spcm;
	snd_pcm_uframes_t host = 0, dai = 0;

	/* nothing todo for BE */
	if (rtd->dai_link->no_pcm)
		return 0;

	/* if have dsp ops pointer callback, use that directly */
	if (sof_ops(sdev)->pcm_pointer)
		return sof_ops(sdev)->pcm_pointer(sdev, substream);

	spcm = snd_sof_find_spcm_dai(sdev, rtd);
	if (!spcm)
		return -EINVAL;

	/* read position from DSP */
	host = bytes_to_frames(substream->runtime,
			       spcm->stream[substream->stream].posn.host_posn);
	dai = bytes_to_frames(substream->runtime,
			      spcm->stream[substream->stream].posn.dai_posn);

	dev_dbg(sdev->dev, "PCM: stream %d dir %d DMA position %lu DAI position %lu\n",
		spcm->pcm.pcm_id, substream->stream, host, dai);

	return host;
}

static int sof_pcm_open(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_component *component =
		snd_soc_rtdcom_lookup(rtd, DRV_NAME);
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(component);
	struct snd_sof_pcm *spcm;
	struct snd_soc_tplg_stream_caps *caps;
	int ret;
	int err;

	/* nothing todo for BE */
	if (rtd->dai_link->no_pcm)
		return 0;

	spcm = snd_sof_find_spcm_dai(sdev, rtd);
	if (!spcm)
		return -EINVAL;

	dev_dbg(sdev->dev, "pcm: open stream %d dir %d\n", spcm->pcm.pcm_id,
		substream->stream);

	caps = &spcm->pcm.caps[substream->stream];

	ret = pm_runtime_get_sync(sdev->dev);
	if (ret < 0) {
		dev_err(sdev->dev, "error: pcm open failed to resume %d\n",
			ret);
		pm_runtime_put_noidle(sdev->dev);
		return ret;
	}

	/* set any runtime constraints based on topology */
	snd_pcm_hw_constraint_step(substream->runtime, 0,
				   SNDRV_PCM_HW_PARAM_BUFFER_SIZE,
				   le32_to_cpu(caps->period_size_min));
	snd_pcm_hw_constraint_step(substream->runtime, 0,
				   SNDRV_PCM_HW_PARAM_PERIOD_SIZE,
				   le32_to_cpu(caps->period_size_min));

	/* set runtime config */
	runtime->hw.info = SNDRV_PCM_INFO_MMAP |
			  SNDRV_PCM_INFO_MMAP_VALID |
			  SNDRV_PCM_INFO_INTERLEAVED |
			  SNDRV_PCM_INFO_PAUSE |
			  SNDRV_PCM_INFO_RESUME |
			  SNDRV_PCM_INFO_NO_PERIOD_WAKEUP;
	runtime->hw.formats = le64_to_cpu(caps->formats);
	runtime->hw.period_bytes_min = le32_to_cpu(caps->period_size_min);
	runtime->hw.period_bytes_max = le32_to_cpu(caps->period_size_max);
	runtime->hw.periods_min = le32_to_cpu(caps->periods_min);
	runtime->hw.periods_max = le32_to_cpu(caps->periods_max);
	runtime->hw.buffer_bytes_max = le32_to_cpu(caps->buffer_size_max);

	dev_dbg(sdev->dev, "period min %zd max %zd bytes\n",
		runtime->hw.period_bytes_min,
		runtime->hw.period_bytes_max);
	dev_dbg(sdev->dev, "period count %d max %d\n",
		runtime->hw.periods_min,
		runtime->hw.periods_max);
	dev_dbg(sdev->dev, "buffer max %zd bytes\n",
		runtime->hw.buffer_bytes_max);

	/* set wait time - TODO: come from topology */
	substream->wait_time = 500;

	spcm->stream[substream->stream].posn.host_posn = 0;
	spcm->stream[substream->stream].posn.dai_posn = 0;
	spcm->stream[substream->stream].substream = substream;

	ret = snd_sof_pcm_platform_open(sdev, substream);
	if (ret < 0) {
		dev_err(sdev->dev, "error: pcm open failed %d\n",
			ret);

		pm_runtime_mark_last_busy(sdev->dev);

		err = pm_runtime_put_autosuspend(sdev->dev);
		if (err < 0)
			dev_err(sdev->dev, "error: pcm close failed to idle %d\n",
				err);
	}

	return ret;
}

static int sof_pcm_close(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_component *component =
		snd_soc_rtdcom_lookup(rtd, DRV_NAME);
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(component);
	struct snd_sof_pcm *spcm;
	int err;

	/* nothing todo for BE */
	if (rtd->dai_link->no_pcm)
		return 0;

	spcm = snd_sof_find_spcm_dai(sdev, rtd);
	if (!spcm)
		return -EINVAL;

	dev_dbg(sdev->dev, "pcm: close stream %d dir %d\n", spcm->pcm.pcm_id,
		substream->stream);

	err = snd_sof_pcm_platform_close(sdev, substream);
	if (err < 0) {
		dev_err(sdev->dev, "error: pcm close failed %d\n",
			err);
		/*
		 * keep going, no point in preventing the close
		 * from happening
		 */
	}

	pm_runtime_mark_last_busy(sdev->dev);

	err = pm_runtime_put_autosuspend(sdev->dev);
	if (err < 0)
		dev_err(sdev->dev, "error: pcm close failed to idle %d\n",
			err);

	return 0;
}

static struct snd_pcm_ops sof_pcm_ops = {
	.open		= sof_pcm_open,
	.close		= sof_pcm_close,
	.ioctl		= snd_pcm_lib_ioctl,
	.hw_params	= sof_pcm_hw_params,
	.hw_free	= sof_pcm_hw_free,
	.trigger	= sof_pcm_trigger,
	.pointer	= sof_pcm_pointer,
	.page		= snd_pcm_sgbuf_ops_page,
};

/*
 * Pre-allocate playback/capture audio buffer pages.
 * no need to explicitly release memory preallocated by sof_pcm_new in pcm_free
 * snd_pcm_lib_preallocate_free_for_all() is called by the core.
 */
static int sof_pcm_new(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_component *component =
		snd_soc_rtdcom_lookup(rtd, DRV_NAME);
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(component);
	struct snd_sof_pcm *spcm;
	struct snd_pcm *pcm = rtd->pcm;
	struct snd_soc_tplg_stream_caps *caps;
	int ret = 0, stream = SNDRV_PCM_STREAM_PLAYBACK;

	/* find SOF PCM for this RTD */
	spcm = snd_sof_find_spcm_dai(sdev, rtd);
	if (!spcm) {
		dev_warn(sdev->dev, "warn: can't find PCM with DAI ID %d\n",
			 rtd->dai_link->id);
		return 0;
	}

	dev_dbg(sdev->dev, "creating new PCM %s\n", spcm->pcm.pcm_name);

	/* do we need to pre-allocate playback audio buffer pages */
	if (!spcm->pcm.playback)
		goto capture;

	caps = &spcm->pcm.caps[stream];

	/* pre-allocate playback audio buffer pages */
	dev_dbg(sdev->dev, "spcm: allocate %s playback DMA buffer size 0x%x max 0x%x\n",
		caps->name, caps->buffer_size_min, caps->buffer_size_max);

	snd_pcm_lib_preallocate_pages(pcm->streams[stream].substream,
				      SNDRV_DMA_TYPE_DEV_SG, sdev->dev,
				      le32_to_cpu(caps->buffer_size_min),
				      le32_to_cpu(caps->buffer_size_max));
capture:
	stream = SNDRV_PCM_STREAM_CAPTURE;

	/* do we need to pre-allocate capture audio buffer pages */
	if (!spcm->pcm.capture)
		return ret;

	caps = &spcm->pcm.caps[stream];

	/* pre-allocate capture audio buffer pages */
	dev_dbg(sdev->dev, "spcm: allocate %s capture DMA buffer size 0x%x max 0x%x\n",
		caps->name, caps->buffer_size_min, caps->buffer_size_max);

	snd_pcm_lib_preallocate_pages(pcm->streams[stream].substream,
				      SNDRV_DMA_TYPE_DEV_SG, sdev->dev,
				      le32_to_cpu(caps->buffer_size_min),
				      le32_to_cpu(caps->buffer_size_max));

	return ret;
}

/* fixup the BE DAI link to match any values from topology */
static int sof_pcm_dai_link_fixup(struct snd_soc_pcm_runtime *rtd,
				  struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate = hw_param_interval(params,
			SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval *channels = hw_param_interval(params,
						SNDRV_PCM_HW_PARAM_CHANNELS);
	struct snd_mask *fmt = hw_param_mask(params, SNDRV_PCM_HW_PARAM_FORMAT);
	struct snd_soc_component *component =
		snd_soc_rtdcom_lookup(rtd, DRV_NAME);
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(component);
	struct snd_sof_dai *dai =
		snd_sof_find_dai(sdev, (char *)rtd->dai_link->name);

	/* no topology exists for this BE, try a common configuration */
	if (!dai) {
		dev_warn(sdev->dev, "warning: no topology found for BE DAI %s config\n",
			 rtd->dai_link->name);

		/*  set 48k, stereo, 16bits by default */
		rate->min = 48000;
		rate->max = 48000;

		channels->min = 2;
		channels->max = 2;

		snd_mask_none(fmt);
		snd_mask_set_format(fmt, SNDRV_PCM_FORMAT_S16_LE);

		return 0;
	}

	/* read format from topology */
	snd_mask_none(fmt);

	switch (dai->comp_dai.config.frame_fmt) {
	case SOF_IPC_FRAME_S16_LE:
		snd_mask_set_format(fmt, SNDRV_PCM_FORMAT_S16_LE);
		break;
	case SOF_IPC_FRAME_S24_4LE:
		snd_mask_set_format(fmt, SNDRV_PCM_FORMAT_S24_LE);
		break;
	case SOF_IPC_FRAME_S32_LE:
		snd_mask_set_format(fmt, SNDRV_PCM_FORMAT_S32_LE);
		break;
	default:
		dev_err(sdev->dev, "error: No available DAI format!\n");
		return -EINVAL;
	}

	/* read rate and channels from topology */
	switch (dai->dai_config->type) {
	case SOF_DAI_INTEL_SSP:
		rate->min = dai->dai_config->ssp.fsync_rate;
		rate->max = dai->dai_config->ssp.fsync_rate;
		channels->min = dai->dai_config->ssp.tdm_slots;
		channels->max = dai->dai_config->ssp.tdm_slots;

		dev_dbg(sdev->dev,
			"rate_min: %d rate_max: %d\n", rate->min, rate->max);
		dev_dbg(sdev->dev,
			"channels_min: %d channels_max: %d\n",
			channels->min, channels->max);

		break;
	case SOF_DAI_INTEL_DMIC:
		/* DMIC only supports 16 or 32 bit formats */
		if (dai->comp_dai.config.frame_fmt == SOF_IPC_FRAME_S24_4LE) {
			dev_err(sdev->dev,
				"error: invalid fmt %d for DAI type %d\n",
				dai->comp_dai.config.frame_fmt,
				dai->dai_config->type);
		}
		/* TODO: add any other DMIC specific fixups */
		break;
	case SOF_DAI_INTEL_HDA:
		/* do nothing for HDA dai_link */
		break;
	default:
		dev_err(sdev->dev, "error: invalid DAI type %d\n",
			dai->dai_config->type);
		break;
	}

	return 0;
}

static int sof_pcm_probe(struct snd_soc_component *component)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(component);
	struct snd_sof_pdata *plat_data = sdev->pdata;
	const char *tplg_filename;
	int ret;

	/* load the default topology */
	sdev->component = component;

	tplg_filename = devm_kasprintf(sdev->dev, GFP_KERNEL,
				       "%s/%s",
				       plat_data->tplg_filename_prefix,
				       plat_data->tplg_filename);
	if (!tplg_filename)
		return -ENOMEM;

	ret = snd_sof_load_topology(sdev, tplg_filename);
	if (ret < 0) {
		dev_err(sdev->dev, "error: failed to load DSP topology %d\n",
			ret);
		return ret;
	}

	/*
	 * Some platforms in SOF, ex: BYT, may not have their platform PM
	 * callbacks set. Increment the usage count so as to
	 * prevent the device entering runtime suspend.
	 */
	if (!sof_ops(sdev)->runtime_suspend || !sof_ops(sdev)->runtime_resume)
		pm_runtime_get_noresume(sdev->dev);

	return ret;
}

static void sof_pcm_remove(struct snd_soc_component *component)
{
}

void snd_sof_new_platform_drv(struct snd_sof_dev *sdev)
{
	struct snd_soc_component_driver *pd = &sdev->plat_drv;
	struct snd_sof_pdata *plat_data = sdev->pdata;
	const char *drv_name;

	drv_name = plat_data->machine->drv_name;

	pd->name = "sof-audio-component";
	pd->probe = sof_pcm_probe;
	pd->remove = sof_pcm_remove;
	pd->ops	= &sof_pcm_ops;
#if IS_ENABLED(CONFIG_SND_SOC_SOF_COMPRESS)
	pd->compr_ops = &sof_compressed_ops;
#endif
	pd->pcm_new = sof_pcm_new;
	pd->ignore_machine = drv_name;
	pd->be_hw_params_fixup = sof_pcm_dai_link_fixup;
	pd->be_pcm_base = SOF_BE_PCM_BASE;
	pd->use_dai_pcm_id = true;
	pd->topology_name_prefix = "sof";

	 /* do not increase the refcount in core */
	pd->ignore_module_refcount = 1;
}
