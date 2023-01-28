// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2022 Intel Corporation. All rights reserved.
//

#include <sound/pcm_params.h>
#include <sound/sof/ipc4/header.h>
#include "sof-audio.h"
#include "sof-priv.h"
#include "ipc4-priv.h"
#include "ipc4-topology.h"

static int sof_ipc4_set_multi_pipeline_state(struct snd_sof_dev *sdev, u32 state,
					     struct ipc4_pipeline_set_state_data *trigger_list)
{
	struct sof_ipc4_msg msg = {{ 0 }};
	u32 primary, ipc_size;

	/* trigger a single pipeline */
	if (trigger_list->count == 1)
		return sof_ipc4_set_pipeline_state(sdev, trigger_list->pipeline_ids[0], state);

	primary = state;
	primary |= SOF_IPC4_MSG_TYPE_SET(SOF_IPC4_GLB_SET_PIPELINE_STATE);
	primary |= SOF_IPC4_MSG_DIR(SOF_IPC4_MSG_REQUEST);
	primary |= SOF_IPC4_MSG_TARGET(SOF_IPC4_FW_GEN_MSG);
	msg.primary = primary;

	/* trigger multiple pipelines with a single IPC */
	msg.extension = SOF_IPC4_GLB_PIPE_STATE_EXT_MULTI;

	/* ipc_size includes the count and the pipeline IDs for the number of pipelines */
	ipc_size = sizeof(u32) * (trigger_list->count + 1);
	msg.data_size = ipc_size;
	msg.data_ptr = trigger_list;

	return sof_ipc_tx_message(sdev->ipc, &msg, ipc_size, NULL, 0);
}

int sof_ipc4_set_pipeline_state(struct snd_sof_dev *sdev, u32 id, u32 state)
{
	struct sof_ipc4_msg msg = {{ 0 }};
	u32 primary;

	dev_dbg(sdev->dev, "ipc4 set pipeline %d state %d", id, state);

	primary = state;
	primary |= SOF_IPC4_GLB_PIPE_STATE_ID(id);
	primary |= SOF_IPC4_MSG_TYPE_SET(SOF_IPC4_GLB_SET_PIPELINE_STATE);
	primary |= SOF_IPC4_MSG_DIR(SOF_IPC4_MSG_REQUEST);
	primary |= SOF_IPC4_MSG_TARGET(SOF_IPC4_FW_GEN_MSG);

	msg.primary = primary;

	return sof_ipc_tx_message(sdev->ipc, &msg, 0, NULL, 0);
}
EXPORT_SYMBOL(sof_ipc4_set_pipeline_state);

static void
sof_ipc4_add_pipeline_to_trigger_list(struct snd_sof_dev *sdev, int state,
				      struct snd_sof_pipeline *spipe,
				      struct ipc4_pipeline_set_state_data *trigger_list)
{
	struct snd_sof_widget *pipe_widget = spipe->pipe_widget;
	struct sof_ipc4_pipeline *pipeline = pipe_widget->private;

	if (pipeline->skip_during_fe_trigger)
		return;

	switch (state) {
	case SOF_IPC4_PIPE_RUNNING:
		/*
		 * Trigger pipeline if all PCMs containing it are paused or if it is RUNNING
		 * for the first time
		 */
		if (spipe->started_count == spipe->paused_count)
			trigger_list->pipeline_ids[trigger_list->count++] =
				pipe_widget->instance_id;
		break;
	case SOF_IPC4_PIPE_RESET:
		/* RESET if the pipeline is neither running nor paused */
		if (!spipe->started_count && !spipe->paused_count)
			trigger_list->pipeline_ids[trigger_list->count++] =
				pipe_widget->instance_id;
		break;
	case SOF_IPC4_PIPE_PAUSED:
		/* Pause the pipeline only when its started_count is 1 more than paused_count */
		if (spipe->paused_count == (spipe->started_count - 1))
			trigger_list->pipeline_ids[trigger_list->count++] =
				pipe_widget->instance_id;
		break;
	default:
		break;
	}
}

static void
sof_ipc4_update_pipeline_state(struct snd_sof_dev *sdev, int state, int cmd,
			       struct snd_sof_pipeline *spipe,
			       struct ipc4_pipeline_set_state_data *trigger_list)
{
	struct snd_sof_widget *pipe_widget = spipe->pipe_widget;
	struct sof_ipc4_pipeline *pipeline = pipe_widget->private;
	int i;

	if (pipeline->skip_during_fe_trigger)
		return;

	/* set state for pipeline if it was just triggered */
	for (i = 0; i < trigger_list->count; i++) {
		if (trigger_list->pipeline_ids[i] == pipe_widget->instance_id) {
			pipeline->state = state;
			break;
		}
	}

	switch (state) {
	case SOF_IPC4_PIPE_PAUSED:
		switch (cmd) {
		case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
			/*
			 * increment paused_count if the PAUSED is the final state during
			 * the PAUSE trigger
			 */
			spipe->paused_count++;
			break;
		case SNDRV_PCM_TRIGGER_STOP:
		case SNDRV_PCM_TRIGGER_SUSPEND:
			/*
			 * decrement started_count if PAUSED is the final state during the
			 * STOP trigger
			 */
			spipe->started_count--;
			break;
		default:
			break;
		}
		break;
	case SOF_IPC4_PIPE_RUNNING:
		switch (cmd) {
		case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
			/* decrement paused_count for RELEASE */
			spipe->paused_count--;
			break;
		case SNDRV_PCM_TRIGGER_START:
		case SNDRV_PCM_TRIGGER_RESUME:
			/* increment started_count for START/RESUME */
			spipe->started_count++;
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}
}

/*
 * The picture below represents the pipeline state machine wrt PCM actions corresponding to the
 * triggers and ioctls
 *				+---------------+
 *				|               |
 *				|    INIT       |
 *				|               |
 *				+-------+-------+
 *					|
 *					|
 *					| START
 *					|
 *					|
 * +----------------+		   +------v-------+		  +-------------+
 * |                |   START     |              |   HW_FREE	  |             |
 * |   RUNNING      <-------------+  PAUSED      +--------------> +   RESET     |
 * |                |   PAUSE     |              |		  |             |
 * +------+---------+   RELEASE   +---------+----+		  +-------------+
 *	  |				     ^
 *	  |				     |
 *	  |				     |
 *	  |				     |
 *	  |		PAUSE		     |
 *	  +---------------------------------+
 *			STOP/SUSPEND
 *
 * Note that during system suspend, the suspend trigger is followed by a hw_free in
 * sof_pcm_trigger(). So, the final state during suspend would be RESET.
 * Also, since the SOF driver doesn't support full resume, streams would be restarted with the
 * prepare ioctl before the START trigger.
 */

static int sof_ipc4_trigger_pipelines(struct snd_soc_component *component,
				      struct snd_pcm_substream *substream, int state, int cmd)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(component);
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct snd_sof_pcm_stream_pipeline_list *pipeline_list;
	struct sof_ipc4_fw_data *ipc4_data = sdev->private;
	struct ipc4_pipeline_set_state_data *trigger_list;
	struct snd_sof_pipeline *spipe;
	struct snd_sof_pcm *spcm;
	int ret;
	int i;

	dev_dbg(sdev->dev, "trigger cmd: %d state: %d\n", cmd, state);

	spcm = snd_sof_find_spcm_dai(component, rtd);
	if (!spcm)
		return -EINVAL;

	pipeline_list = &spcm->stream[substream->stream].pipeline_list;

	/* nothing to trigger if the list is empty */
	if (!pipeline_list->pipelines || !pipeline_list->count)
		return 0;

	/* allocate memory for the pipeline data */
	trigger_list = kzalloc(struct_size(trigger_list, pipeline_ids, pipeline_list->count),
			       GFP_KERNEL);
	if (!trigger_list)
		return -ENOMEM;

	mutex_lock(&ipc4_data->pipeline_state_mutex);

	/*
	 * IPC4 requires pipelines to be triggered in order starting at the sink and
	 * walking all the way to the source. So traverse the pipeline_list in the order
	 * sink->source when starting PCM's and in the reverse order to pause/stop PCM's.
	 * Skip the pipelines that have their skip_during_fe_trigger flag set. If there is a fork
	 * in the pipeline, the order of triggering between the left/right paths will be
	 * indeterministic. But the sink->source trigger order sink->source would still be
	 * guaranteed for each fork independently.
	 */
	if (state == SOF_IPC4_PIPE_RUNNING || state == SOF_IPC4_PIPE_RESET)
		for (i = pipeline_list->count - 1; i >= 0; i--) {
			spipe = pipeline_list->pipelines[i];
			sof_ipc4_add_pipeline_to_trigger_list(sdev, state, spipe, trigger_list);
		}
	else
		for (i = 0; i < pipeline_list->count; i++) {
			spipe = pipeline_list->pipelines[i];
			sof_ipc4_add_pipeline_to_trigger_list(sdev, state, spipe, trigger_list);
		}

	/* return if all pipelines are in the requested state already */
	if (!trigger_list->count) {
		ret = 0;
		goto free;
	}

	/* no need to pause before reset or before pause release */
	if (state == SOF_IPC4_PIPE_RESET || cmd == SNDRV_PCM_TRIGGER_PAUSE_RELEASE)
		goto skip_pause_transition;

	/*
	 * set paused state for pipelines if the final state is PAUSED or when the pipeline
	 * is set to RUNNING for the first time after the PCM is started.
	 */
	ret = sof_ipc4_set_multi_pipeline_state(sdev, SOF_IPC4_PIPE_PAUSED, trigger_list);
	if (ret < 0) {
		dev_err(sdev->dev, "failed to pause all pipelines\n");
		goto free;
	}

	/* update PAUSED state for all pipelines just triggered */
	for (i = 0; i < pipeline_list->count ; i++) {
		spipe = pipeline_list->pipelines[i];
		sof_ipc4_update_pipeline_state(sdev, SOF_IPC4_PIPE_PAUSED, cmd, spipe,
					       trigger_list);
	}

	/* return if this is the final state */
	if (state == SOF_IPC4_PIPE_PAUSED)
		goto free;
skip_pause_transition:
	/* else set the RUNNING/RESET state in the DSP */
	ret = sof_ipc4_set_multi_pipeline_state(sdev, state, trigger_list);
	if (ret < 0) {
		dev_err(sdev->dev, "failed to set final state %d for all pipelines\n", state);
		goto free;
	}

	/* update RUNNING/RESET state for all pipelines that were just triggered */
	for (i = 0; i < pipeline_list->count; i++) {
		spipe = pipeline_list->pipelines[i];
		sof_ipc4_update_pipeline_state(sdev, state, cmd, spipe, trigger_list);
	}

free:
	mutex_unlock(&ipc4_data->pipeline_state_mutex);
	kfree(trigger_list);
	return ret;
}

static int sof_ipc4_pcm_trigger(struct snd_soc_component *component,
				struct snd_pcm_substream *substream, int cmd)
{
	int state;

	/* determine the pipeline state */
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		state = SOF_IPC4_PIPE_PAUSED;
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_START:
		state = SOF_IPC4_PIPE_RUNNING;
		break;
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_STOP:
		state = SOF_IPC4_PIPE_PAUSED;
		break;
	default:
		dev_err(component->dev, "%s: unhandled trigger cmd %d\n", __func__, cmd);
		return -EINVAL;
	}

	/* set the pipeline state */
	return sof_ipc4_trigger_pipelines(component, substream, state, cmd);
}

static int sof_ipc4_pcm_hw_free(struct snd_soc_component *component,
				struct snd_pcm_substream *substream)
{
	/* command is not relevant with RESET, so just pass 0 */
	return sof_ipc4_trigger_pipelines(component, substream, SOF_IPC4_PIPE_RESET, 0);
}

static void ipc4_ssp_dai_config_pcm_params_match(struct snd_sof_dev *sdev, const char *link_name,
						 struct snd_pcm_hw_params *params)
{
	struct snd_sof_dai_link *slink;
	struct snd_sof_dai *dai;
	bool dai_link_found = false;
	int i;

	list_for_each_entry(slink, &sdev->dai_link_list, list) {
		if (!strcmp(slink->link->name, link_name)) {
			dai_link_found = true;
			break;
		}
	}

	if (!dai_link_found)
		return;

	for (i = 0; i < slink->num_hw_configs; i++) {
		struct snd_soc_tplg_hw_config *hw_config = &slink->hw_configs[i];

		if (params_rate(params) == le32_to_cpu(hw_config->fsync_rate)) {
			/* set current config for all DAI's with matching name */
			list_for_each_entry(dai, &sdev->dai_list, list)
				if (!strcmp(slink->link->name, dai->name))
					dai->current_config = le32_to_cpu(hw_config->id);
			break;
		}
	}
}

static int sof_ipc4_pcm_dai_link_fixup(struct snd_soc_pcm_runtime *rtd,
				       struct snd_pcm_hw_params *params)
{
	struct snd_soc_component *component = snd_soc_rtdcom_lookup(rtd, SOF_AUDIO_PCM_DRV_NAME);
	struct snd_sof_dai *dai = snd_sof_find_dai(component, rtd->dai_link->name);
	struct snd_interval *rate = hw_param_interval(params, SNDRV_PCM_HW_PARAM_RATE);
	struct snd_mask *fmt = hw_param_mask(params, SNDRV_PCM_HW_PARAM_FORMAT);
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(component);
	struct sof_ipc4_copier *ipc4_copier;

	if (!dai) {
		dev_err(component->dev, "%s: No DAI found with name %s\n", __func__,
			rtd->dai_link->name);
		return -EINVAL;
	}

	ipc4_copier = dai->private;
	if (!ipc4_copier) {
		dev_err(component->dev, "%s: No private data found for DAI %s\n",
			__func__, rtd->dai_link->name);
		return -EINVAL;
	}

	/* always set BE format to 32-bits for both playback and capture */
	snd_mask_none(fmt);
	snd_mask_set_format(fmt, SNDRV_PCM_FORMAT_S32_LE);

	rate->min = ipc4_copier->available_fmt.base_config->audio_fmt.sampling_frequency;
	rate->max = rate->min;

	switch (ipc4_copier->dai_type) {
	case SOF_DAI_INTEL_SSP:
		ipc4_ssp_dai_config_pcm_params_match(sdev, (char *)rtd->dai_link->name, params);
		break;
	default:
		break;
	}

	return 0;
}

static void sof_ipc4_pcm_free(struct snd_sof_dev *sdev, struct snd_sof_pcm *spcm)
{
	struct snd_sof_pcm_stream_pipeline_list *pipeline_list;
	int stream;

	for_each_pcm_streams(stream) {
		pipeline_list = &spcm->stream[stream].pipeline_list;
		kfree(pipeline_list->pipelines);
		pipeline_list->pipelines = NULL;
	}
}

static int sof_ipc4_pcm_setup(struct snd_sof_dev *sdev, struct snd_sof_pcm *spcm)
{
	struct snd_sof_pcm_stream_pipeline_list *pipeline_list;
	struct sof_ipc4_fw_data *ipc4_data = sdev->private;
	int stream;

	for_each_pcm_streams(stream) {
		pipeline_list = &spcm->stream[stream].pipeline_list;

		/* allocate memory for max number of pipeline IDs */
		pipeline_list->pipelines = kcalloc(ipc4_data->max_num_pipelines,
						   sizeof(struct snd_sof_widget *), GFP_KERNEL);
		if (!pipeline_list->pipelines) {
			sof_ipc4_pcm_free(sdev, spcm);
			return -ENOMEM;
		}
	}

	return 0;
}

const struct sof_ipc_pcm_ops ipc4_pcm_ops = {
	.trigger = sof_ipc4_pcm_trigger,
	.hw_free = sof_ipc4_pcm_hw_free,
	.dai_link_fixup = sof_ipc4_pcm_dai_link_fixup,
	.pcm_setup = sof_ipc4_pcm_setup,
	.pcm_free = sof_ipc4_pcm_free,
};
