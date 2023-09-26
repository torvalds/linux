// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
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
#include <trace/events/sof.h>
#include "sof-of-dev.h"
#include "sof-priv.h"
#include "sof-audio.h"
#include "sof-utils.h"
#include "ops.h"

/* Create DMA buffer page table for DSP */
static int create_page_table(struct snd_soc_component *component,
			     struct snd_pcm_substream *substream,
			     unsigned char *dma_area, size_t size)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_sof_pcm *spcm;
	struct snd_dma_buffer *dmab = snd_pcm_get_dma_buf(substream);
	int stream = substream->stream;

	spcm = snd_sof_find_spcm_dai(component, rtd);
	if (!spcm)
		return -EINVAL;

	return snd_sof_create_page_table(component->dev, dmab,
		spcm->stream[stream].page_table.area, size);
}

/*
 * sof pcm period elapse work
 */
static void snd_sof_pcm_period_elapsed_work(struct work_struct *work)
{
	struct snd_sof_pcm_stream *sps =
		container_of(work, struct snd_sof_pcm_stream,
			     period_elapsed_work);

	snd_pcm_period_elapsed(sps->substream);
}

void snd_sof_pcm_init_elapsed_work(struct work_struct *work)
{
	 INIT_WORK(work, snd_sof_pcm_period_elapsed_work);
}

/*
 * sof pcm period elapse, this could be called at irq thread context.
 */
void snd_sof_pcm_period_elapsed(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_component *component =
		snd_soc_rtdcom_lookup(rtd, SOF_AUDIO_PCM_DRV_NAME);
	struct snd_sof_pcm *spcm;

	spcm = snd_sof_find_spcm_dai(component, rtd);
	if (!spcm) {
		dev_err(component->dev,
			"error: period elapsed for unknown stream!\n");
		return;
	}

	/*
	 * snd_pcm_period_elapsed() can be called in interrupt context
	 * before IRQ_HANDLED is returned. Inside snd_pcm_period_elapsed(),
	 * when the PCM is done draining or xrun happened, a STOP IPC will
	 * then be sent and this IPC will hit IPC timeout.
	 * To avoid sending IPC before the previous IPC is handled, we
	 * schedule delayed work here to call the snd_pcm_period_elapsed().
	 */
	schedule_work(&spcm->stream[substream->stream].period_elapsed_work);
}
EXPORT_SYMBOL(snd_sof_pcm_period_elapsed);

static int
sof_pcm_setup_connected_widgets(struct snd_sof_dev *sdev, struct snd_soc_pcm_runtime *rtd,
				struct snd_sof_pcm *spcm, struct snd_pcm_hw_params *params,
				struct snd_sof_platform_stream_params *platform_params, int dir)
{
	struct snd_soc_dai *dai;
	int ret, j;

	/* query DAPM for list of connected widgets and set them up */
	for_each_rtd_cpu_dais(rtd, j, dai) {
		struct snd_soc_dapm_widget_list *list;

		ret = snd_soc_dapm_dai_get_connected_widgets(dai, dir, &list,
							     dpcm_end_walk_at_be);
		if (ret < 0) {
			dev_err(sdev->dev, "error: dai %s has no valid %s path\n", dai->name,
				dir == SNDRV_PCM_STREAM_PLAYBACK ? "playback" : "capture");
			return ret;
		}

		spcm->stream[dir].list = list;

		ret = sof_widget_list_setup(sdev, spcm, params, platform_params, dir);
		if (ret < 0) {
			dev_err(sdev->dev, "error: failed widget list set up for pcm %d dir %d\n",
				spcm->pcm.pcm_id, dir);
			spcm->stream[dir].list = NULL;
			snd_soc_dapm_dai_free_widgets(&list);
			return ret;
		}
	}

	return 0;
}

static int sof_pcm_hw_params(struct snd_soc_component *component,
			     struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(component);
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	const struct sof_ipc_pcm_ops *pcm_ops = sof_ipc_get_ops(sdev, pcm);
	struct snd_sof_platform_stream_params platform_params = { 0 };
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_sof_pcm *spcm;
	int ret;

	/* nothing to do for BE */
	if (rtd->dai_link->no_pcm)
		return 0;

	spcm = snd_sof_find_spcm_dai(component, rtd);
	if (!spcm)
		return -EINVAL;

	/*
	 * Handle repeated calls to hw_params() without free_pcm() in
	 * between. At least ALSA OSS emulation depends on this.
	 */
	if (pcm_ops && pcm_ops->hw_free && spcm->prepared[substream->stream]) {
		ret = pcm_ops->hw_free(component, substream);
		if (ret < 0)
			return ret;

		spcm->prepared[substream->stream] = false;
	}

	dev_dbg(component->dev, "pcm: hw params stream %d dir %d\n",
		spcm->pcm.pcm_id, substream->stream);

	ret = snd_sof_pcm_platform_hw_params(sdev, substream, params, &platform_params);
	if (ret < 0) {
		dev_err(component->dev, "platform hw params failed\n");
		return ret;
	}

	/* if this is a repeated hw_params without hw_free, skip setting up widgets */
	if (!spcm->stream[substream->stream].list) {
		ret = sof_pcm_setup_connected_widgets(sdev, rtd, spcm, params, &platform_params,
						      substream->stream);
		if (ret < 0)
			return ret;
	}

	/* create compressed page table for audio firmware */
	if (runtime->buffer_changed) {
		ret = create_page_table(component, substream, runtime->dma_area,
					runtime->dma_bytes);

		if (ret < 0)
			return ret;
	}

	if (pcm_ops && pcm_ops->hw_params) {
		ret = pcm_ops->hw_params(component, substream, params, &platform_params);
		if (ret < 0)
			return ret;
	}

	spcm->prepared[substream->stream] = true;

	/* save pcm hw_params */
	memcpy(&spcm->params[substream->stream], params, sizeof(*params));

	return 0;
}

static int sof_pcm_hw_free(struct snd_soc_component *component,
			   struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(component);
	const struct sof_ipc_pcm_ops *pcm_ops = sof_ipc_get_ops(sdev, pcm);
	struct snd_sof_pcm *spcm;
	int ret, err = 0;

	/* nothing to do for BE */
	if (rtd->dai_link->no_pcm)
		return 0;

	spcm = snd_sof_find_spcm_dai(component, rtd);
	if (!spcm)
		return -EINVAL;

	dev_dbg(component->dev, "pcm: free stream %d dir %d\n",
		spcm->pcm.pcm_id, substream->stream);

	if (spcm->prepared[substream->stream]) {
		/* stop DMA first if needed */
		if (pcm_ops && pcm_ops->platform_stop_during_hw_free)
			snd_sof_pcm_platform_trigger(sdev, substream, SNDRV_PCM_TRIGGER_STOP);

		/* free PCM in the DSP */
		if (pcm_ops && pcm_ops->hw_free) {
			ret = pcm_ops->hw_free(component, substream);
			if (ret < 0)
				err = ret;
		}

		spcm->prepared[substream->stream] = false;
	}

	/* reset DMA */
	ret = snd_sof_pcm_platform_hw_free(sdev, substream);
	if (ret < 0) {
		dev_err(component->dev, "error: platform hw free failed\n");
		err = ret;
	}

	/* free the DAPM widget list */
	ret = sof_widget_list_free(sdev, spcm, substream->stream);
	if (ret < 0)
		err = ret;

	cancel_work_sync(&spcm->stream[substream->stream].period_elapsed_work);

	return err;
}

static int sof_pcm_prepare(struct snd_soc_component *component,
			   struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_sof_pcm *spcm;
	int ret;

	/* nothing to do for BE */
	if (rtd->dai_link->no_pcm)
		return 0;

	spcm = snd_sof_find_spcm_dai(component, rtd);
	if (!spcm)
		return -EINVAL;

	if (spcm->prepared[substream->stream])
		return 0;

	dev_dbg(component->dev, "pcm: prepare stream %d dir %d\n",
		spcm->pcm.pcm_id, substream->stream);

	/* set hw_params */
	ret = sof_pcm_hw_params(component,
				substream, &spcm->params[substream->stream]);
	if (ret < 0) {
		dev_err(component->dev,
			"error: set pcm hw_params after resume\n");
		return ret;
	}

	return 0;
}

/*
 * FE dai link trigger actions are always executed in non-atomic context because
 * they involve IPC's.
 */
static int sof_pcm_trigger(struct snd_soc_component *component,
			   struct snd_pcm_substream *substream, int cmd)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(component);
	const struct sof_ipc_pcm_ops *pcm_ops = sof_ipc_get_ops(sdev, pcm);
	struct snd_sof_pcm *spcm;
	bool reset_hw_params = false;
	bool ipc_first = false;
	int ret = 0;

	/* nothing to do for BE */
	if (rtd->dai_link->no_pcm)
		return 0;

	spcm = snd_sof_find_spcm_dai(component, rtd);
	if (!spcm)
		return -EINVAL;

	dev_dbg(component->dev, "pcm: trigger stream %d dir %d cmd %d\n",
		spcm->pcm.pcm_id, substream->stream, cmd);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		ipc_first = true;
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		if (pcm_ops && pcm_ops->ipc_first_on_start)
			ipc_first = true;
		break;
	case SNDRV_PCM_TRIGGER_START:
		if (spcm->stream[substream->stream].suspend_ignored) {
			/*
			 * This case will be triggered when INFO_RESUME is
			 * not supported, no need to re-start streams that
			 * remained enabled in D0ix.
			 */
			spcm->stream[substream->stream].suspend_ignored = false;
			return 0;
		}

		if (pcm_ops && pcm_ops->ipc_first_on_start)
			ipc_first = true;
		break;
	case SNDRV_PCM_TRIGGER_SUSPEND:
		if (sdev->system_suspend_target == SOF_SUSPEND_S0IX &&
		    spcm->stream[substream->stream].d0i3_compatible) {
			/*
			 * trap the event, not sending trigger stop to
			 * prevent the FW pipelines from being stopped,
			 * and mark the flag to ignore the upcoming DAPM
			 * PM events.
			 */
			spcm->stream[substream->stream].suspend_ignored = true;
			return 0;
		}

		/* On suspend the DMA must be stopped in DSPless mode */
		if (sdev->dspless_mode_selected)
			reset_hw_params = true;

		fallthrough;
	case SNDRV_PCM_TRIGGER_STOP:
		ipc_first = true;
		if (pcm_ops && pcm_ops->reset_hw_params_during_stop)
			reset_hw_params = true;
		break;
	default:
		dev_err(component->dev, "Unhandled trigger cmd %d\n", cmd);
		return -EINVAL;
	}

	if (!ipc_first)
		snd_sof_pcm_platform_trigger(sdev, substream, cmd);

	if (pcm_ops && pcm_ops->trigger)
		ret = pcm_ops->trigger(component, substream, cmd);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
	case SNDRV_PCM_TRIGGER_START:
		/* invoke platform trigger to start DMA only if pcm_ops is successful */
		if (ipc_first && !ret)
			snd_sof_pcm_platform_trigger(sdev, substream, cmd);
		break;
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
	case SNDRV_PCM_TRIGGER_STOP:
		/* invoke platform trigger to stop DMA even if pcm_ops isn't set or if it failed */
		if (!pcm_ops || !pcm_ops->platform_stop_during_hw_free)
			snd_sof_pcm_platform_trigger(sdev, substream, cmd);
		break;
	default:
		break;
	}

	/* free PCM if reset_hw_params is set and the STOP IPC is successful */
	if (!ret && reset_hw_params)
		ret = sof_pcm_stream_free(sdev, substream, spcm, substream->stream, false);

	return ret;
}

static snd_pcm_uframes_t sof_pcm_pointer(struct snd_soc_component *component,
					 struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(component);
	struct snd_sof_pcm *spcm;
	snd_pcm_uframes_t host, dai;

	/* nothing to do for BE */
	if (rtd->dai_link->no_pcm)
		return 0;

	/* use dsp ops pointer callback directly if set */
	if (sof_ops(sdev)->pcm_pointer)
		return sof_ops(sdev)->pcm_pointer(sdev, substream);

	spcm = snd_sof_find_spcm_dai(component, rtd);
	if (!spcm)
		return -EINVAL;

	/* read position from DSP */
	host = bytes_to_frames(substream->runtime,
			       spcm->stream[substream->stream].posn.host_posn);
	dai = bytes_to_frames(substream->runtime,
			      spcm->stream[substream->stream].posn.dai_posn);

	trace_sof_pcm_pointer_position(sdev, spcm, substream, host, dai);

	return host;
}

static int sof_pcm_open(struct snd_soc_component *component,
			struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(component);
	struct snd_sof_dsp_ops *ops = sof_ops(sdev);
	struct snd_sof_pcm *spcm;
	struct snd_soc_tplg_stream_caps *caps;
	int ret;

	/* nothing to do for BE */
	if (rtd->dai_link->no_pcm)
		return 0;

	spcm = snd_sof_find_spcm_dai(component, rtd);
	if (!spcm)
		return -EINVAL;

	dev_dbg(component->dev, "pcm: open stream %d dir %d\n",
		spcm->pcm.pcm_id, substream->stream);


	caps = &spcm->pcm.caps[substream->stream];

	/* set runtime config */
	runtime->hw.info = ops->hw_info; /* platform-specific */

	/* set any runtime constraints based on topology */
	runtime->hw.formats = le64_to_cpu(caps->formats);
	runtime->hw.period_bytes_min = le32_to_cpu(caps->period_size_min);
	runtime->hw.period_bytes_max = le32_to_cpu(caps->period_size_max);
	runtime->hw.periods_min = le32_to_cpu(caps->periods_min);
	runtime->hw.periods_max = le32_to_cpu(caps->periods_max);

	/*
	 * caps->buffer_size_min is not used since the
	 * snd_pcm_hardware structure only defines buffer_bytes_max
	 */
	runtime->hw.buffer_bytes_max = le32_to_cpu(caps->buffer_size_max);

	dev_dbg(component->dev, "period min %zd max %zd bytes\n",
		runtime->hw.period_bytes_min,
		runtime->hw.period_bytes_max);
	dev_dbg(component->dev, "period count %d max %d\n",
		runtime->hw.periods_min,
		runtime->hw.periods_max);
	dev_dbg(component->dev, "buffer max %zd bytes\n",
		runtime->hw.buffer_bytes_max);

	/* set wait time - TODO: come from topology */
	substream->wait_time = 500;

	spcm->stream[substream->stream].posn.host_posn = 0;
	spcm->stream[substream->stream].posn.dai_posn = 0;
	spcm->stream[substream->stream].substream = substream;
	spcm->prepared[substream->stream] = false;

	ret = snd_sof_pcm_platform_open(sdev, substream);
	if (ret < 0)
		dev_err(component->dev, "error: pcm open failed %d\n", ret);

	return ret;
}

static int sof_pcm_close(struct snd_soc_component *component,
			 struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(component);
	struct snd_sof_pcm *spcm;
	int err;

	/* nothing to do for BE */
	if (rtd->dai_link->no_pcm)
		return 0;

	spcm = snd_sof_find_spcm_dai(component, rtd);
	if (!spcm)
		return -EINVAL;

	dev_dbg(component->dev, "pcm: close stream %d dir %d\n",
		spcm->pcm.pcm_id, substream->stream);

	err = snd_sof_pcm_platform_close(sdev, substream);
	if (err < 0) {
		dev_err(component->dev, "error: pcm close failed %d\n",
			err);
		/*
		 * keep going, no point in preventing the close
		 * from happening
		 */
	}

	return 0;
}

/*
 * Pre-allocate playback/capture audio buffer pages.
 * no need to explicitly release memory preallocated by sof_pcm_new in pcm_free
 * snd_pcm_lib_preallocate_free_for_all() is called by the core.
 */
static int sof_pcm_new(struct snd_soc_component *component,
		       struct snd_soc_pcm_runtime *rtd)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(component);
	struct snd_sof_pcm *spcm;
	struct snd_pcm *pcm = rtd->pcm;
	struct snd_soc_tplg_stream_caps *caps;
	int stream = SNDRV_PCM_STREAM_PLAYBACK;

	/* find SOF PCM for this RTD */
	spcm = snd_sof_find_spcm_dai(component, rtd);
	if (!spcm) {
		dev_warn(component->dev, "warn: can't find PCM with DAI ID %d\n",
			 rtd->dai_link->id);
		return 0;
	}

	dev_dbg(component->dev, "creating new PCM %s\n", spcm->pcm.pcm_name);

	/* do we need to pre-allocate playback audio buffer pages */
	if (!spcm->pcm.playback)
		goto capture;

	caps = &spcm->pcm.caps[stream];

	/* pre-allocate playback audio buffer pages */
	dev_dbg(component->dev,
		"spcm: allocate %s playback DMA buffer size 0x%x max 0x%x\n",
		caps->name, caps->buffer_size_min, caps->buffer_size_max);

	if (!pcm->streams[stream].substream) {
		dev_err(component->dev, "error: NULL playback substream!\n");
		return -EINVAL;
	}

	snd_pcm_set_managed_buffer(pcm->streams[stream].substream,
				   SNDRV_DMA_TYPE_DEV_SG, sdev->dev,
				   0, le32_to_cpu(caps->buffer_size_max));
capture:
	stream = SNDRV_PCM_STREAM_CAPTURE;

	/* do we need to pre-allocate capture audio buffer pages */
	if (!spcm->pcm.capture)
		return 0;

	caps = &spcm->pcm.caps[stream];

	/* pre-allocate capture audio buffer pages */
	dev_dbg(component->dev,
		"spcm: allocate %s capture DMA buffer size 0x%x max 0x%x\n",
		caps->name, caps->buffer_size_min, caps->buffer_size_max);

	if (!pcm->streams[stream].substream) {
		dev_err(component->dev, "error: NULL capture substream!\n");
		return -EINVAL;
	}

	snd_pcm_set_managed_buffer(pcm->streams[stream].substream,
				   SNDRV_DMA_TYPE_DEV_SG, sdev->dev,
				   0, le32_to_cpu(caps->buffer_size_max));

	return 0;
}

/* fixup the BE DAI link to match any values from topology */
int sof_pcm_dai_link_fixup(struct snd_soc_pcm_runtime *rtd, struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate = hw_param_interval(params,
			SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval *channels = hw_param_interval(params,
						SNDRV_PCM_HW_PARAM_CHANNELS);
	struct snd_mask *fmt = hw_param_mask(params, SNDRV_PCM_HW_PARAM_FORMAT);
	struct snd_soc_component *component =
		snd_soc_rtdcom_lookup(rtd, SOF_AUDIO_PCM_DRV_NAME);
	struct snd_sof_dai *dai =
		snd_sof_find_dai(component, (char *)rtd->dai_link->name);
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(component);
	const struct sof_ipc_pcm_ops *pcm_ops = sof_ipc_get_ops(sdev, pcm);

	/* no topology exists for this BE, try a common configuration */
	if (!dai) {
		dev_warn(component->dev,
			 "warning: no topology found for BE DAI %s config\n",
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

	if (pcm_ops && pcm_ops->dai_link_fixup)
		return pcm_ops->dai_link_fixup(rtd, params);

	return 0;
}
EXPORT_SYMBOL(sof_pcm_dai_link_fixup);

static int sof_pcm_probe(struct snd_soc_component *component)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(component);
	struct snd_sof_pdata *plat_data = sdev->pdata;
	const char *tplg_filename;
	int ret;

	/*
	 * make sure the device is pm_runtime_active before loading the
	 * topology and initiating IPC or bus transactions
	 */
	ret = pm_runtime_resume_and_get(component->dev);
	if (ret < 0 && ret != -EACCES)
		return ret;

	/* load the default topology */
	sdev->component = component;

	tplg_filename = devm_kasprintf(sdev->dev, GFP_KERNEL,
				       "%s/%s",
				       plat_data->tplg_filename_prefix,
				       plat_data->tplg_filename);
	if (!tplg_filename) {
		ret = -ENOMEM;
		goto pm_error;
	}

	ret = snd_sof_load_topology(component, tplg_filename);
	if (ret < 0)
		dev_err(component->dev, "error: failed to load DSP topology %d\n",
			ret);

pm_error:
	pm_runtime_mark_last_busy(component->dev);
	pm_runtime_put_autosuspend(component->dev);

	return ret;
}

static void sof_pcm_remove(struct snd_soc_component *component)
{
	/* remove topology */
	snd_soc_tplg_component_remove(component);
}

static int sof_pcm_ack(struct snd_soc_component *component,
		       struct snd_pcm_substream *substream)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(component);

	return snd_sof_pcm_platform_ack(sdev, substream);
}

static snd_pcm_sframes_t sof_pcm_delay(struct snd_soc_component *component,
				       struct snd_pcm_substream *substream)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(component);
	const struct sof_ipc_pcm_ops *pcm_ops = sof_ipc_get_ops(sdev, pcm);

	if (pcm_ops && pcm_ops->delay)
		return pcm_ops->delay(component, substream);

	return 0;
}

void snd_sof_new_platform_drv(struct snd_sof_dev *sdev)
{
	struct snd_soc_component_driver *pd = &sdev->plat_drv;
	struct snd_sof_pdata *plat_data = sdev->pdata;
	const char *drv_name;

	if (plat_data->machine)
		drv_name = plat_data->machine->drv_name;
	else if (plat_data->of_machine)
		drv_name = plat_data->of_machine->drv_name;
	else
		drv_name = NULL;

	pd->name = "sof-audio-component";
	pd->probe = sof_pcm_probe;
	pd->remove = sof_pcm_remove;
	pd->open = sof_pcm_open;
	pd->close = sof_pcm_close;
	pd->hw_params = sof_pcm_hw_params;
	pd->prepare = sof_pcm_prepare;
	pd->hw_free = sof_pcm_hw_free;
	pd->trigger = sof_pcm_trigger;
	pd->pointer = sof_pcm_pointer;
	pd->ack = sof_pcm_ack;
	pd->delay = sof_pcm_delay;

#if IS_ENABLED(CONFIG_SND_SOC_SOF_COMPRESS)
	pd->compress_ops = &sof_compressed_ops;
#endif

	pd->pcm_construct = sof_pcm_new;
	pd->ignore_machine = drv_name;
	pd->be_pcm_base = SOF_BE_PCM_BASE;
	pd->use_dai_pcm_id = true;
	pd->topology_name_prefix = "sof";

	 /* increment module refcount when a pcm is opened */
	pd->module_get_upon_open = 1;

	pd->legacy_dai_naming = 1;

	/*
	 * The fixup is only needed when the DSP is in use as with the DSPless
	 * mode we are directly using the audio interface
	 */
	if (!sdev->dspless_mode_selected)
		pd->be_hw_params_fixup = sof_pcm_dai_link_fixup;
}
