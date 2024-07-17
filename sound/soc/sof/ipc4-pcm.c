// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2022 Intel Corporation
//

#include <sound/pcm_params.h>
#include <sound/sof/ipc4/header.h>
#include "sof-audio.h"
#include "sof-priv.h"
#include "ops.h"
#include "ipc4-priv.h"
#include "ipc4-topology.h"
#include "ipc4-fw-reg.h"

/**
 * struct sof_ipc4_timestamp_info - IPC4 timestamp info
 * @host_copier: the host copier of the pcm stream
 * @dai_copier: the dai copier of the pcm stream
 * @stream_start_offset: reported by fw in memory window (converted to frames)
 * @stream_end_offset: reported by fw in memory window (converted to frames)
 * @llp_offset: llp offset in memory window
 * @boundary: wrap boundary should be used for the LLP frame counter
 * @delay: Calculated and stored in pointer callback. The stored value is
 *	   returned in the delay callback.
 */
struct sof_ipc4_timestamp_info {
	struct sof_ipc4_copier *host_copier;
	struct sof_ipc4_copier *dai_copier;
	u64 stream_start_offset;
	u64 stream_end_offset;
	u32 llp_offset;

	u64 boundary;
	snd_pcm_sframes_t delay;
};

/**
 * struct sof_ipc4_pcm_stream_priv - IPC4 specific private data
 * @time_info: pointer to time info struct if it is supported, otherwise NULL
 * @chain_dma_allocated: indicates the ChainDMA allocation state
 */
struct sof_ipc4_pcm_stream_priv {
	struct sof_ipc4_timestamp_info *time_info;

	bool chain_dma_allocated;
};

static inline struct sof_ipc4_timestamp_info *
sof_ipc4_sps_to_time_info(struct snd_sof_pcm_stream *sps)
{
	struct sof_ipc4_pcm_stream_priv *stream_priv = sps->private;

	return stream_priv->time_info;
}

static int sof_ipc4_set_multi_pipeline_state(struct snd_sof_dev *sdev, u32 state,
					     struct ipc4_pipeline_set_state_data *trigger_list)
{
	struct sof_ipc4_msg msg = {{ 0 }};
	u32 primary, ipc_size;

	/* trigger a single pipeline */
	if (trigger_list->count == 1)
		return sof_ipc4_set_pipeline_state(sdev, trigger_list->pipeline_instance_ids[0],
						   state);

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

	return sof_ipc_tx_message_no_reply(sdev->ipc, &msg, ipc_size);
}

int sof_ipc4_set_pipeline_state(struct snd_sof_dev *sdev, u32 instance_id, u32 state)
{
	struct sof_ipc4_msg msg = {{ 0 }};
	u32 primary;

	dev_dbg(sdev->dev, "ipc4 set pipeline instance %d state %d", instance_id, state);

	primary = state;
	primary |= SOF_IPC4_GLB_PIPE_STATE_ID(instance_id);
	primary |= SOF_IPC4_MSG_TYPE_SET(SOF_IPC4_GLB_SET_PIPELINE_STATE);
	primary |= SOF_IPC4_MSG_DIR(SOF_IPC4_MSG_REQUEST);
	primary |= SOF_IPC4_MSG_TARGET(SOF_IPC4_FW_GEN_MSG);

	msg.primary = primary;

	return sof_ipc_tx_message_no_reply(sdev->ipc, &msg, 0);
}
EXPORT_SYMBOL(sof_ipc4_set_pipeline_state);

static void sof_ipc4_add_pipeline_by_priority(struct ipc4_pipeline_set_state_data *trigger_list,
					      struct snd_sof_widget *pipe_widget,
					      s8 *pipe_priority, bool ascend)
{
	struct sof_ipc4_pipeline *pipeline = pipe_widget->private;
	int i, j;

	for (i = 0; i < trigger_list->count; i++) {
		/* add pipeline from low priority to high */
		if (ascend && pipeline->priority < pipe_priority[i])
			break;
		/* add pipeline from high priority to low */
		else if (!ascend && pipeline->priority > pipe_priority[i])
			break;
	}

	for (j = trigger_list->count - 1; j >= i; j--) {
		trigger_list->pipeline_instance_ids[j + 1] = trigger_list->pipeline_instance_ids[j];
		pipe_priority[j + 1] = pipe_priority[j];
	}

	trigger_list->pipeline_instance_ids[i] = pipe_widget->instance_id;
	trigger_list->count++;
	pipe_priority[i] = pipeline->priority;
}

static void
sof_ipc4_add_pipeline_to_trigger_list(struct snd_sof_dev *sdev, int state,
				      struct snd_sof_pipeline *spipe,
				      struct ipc4_pipeline_set_state_data *trigger_list,
				      s8 *pipe_priority)
{
	struct snd_sof_widget *pipe_widget = spipe->pipe_widget;
	struct sof_ipc4_pipeline *pipeline = pipe_widget->private;

	if (pipeline->skip_during_fe_trigger && state != SOF_IPC4_PIPE_RESET)
		return;

	switch (state) {
	case SOF_IPC4_PIPE_RUNNING:
		/*
		 * Trigger pipeline if all PCMs containing it are paused or if it is RUNNING
		 * for the first time
		 */
		if (spipe->started_count == spipe->paused_count)
			sof_ipc4_add_pipeline_by_priority(trigger_list, pipe_widget, pipe_priority,
							  false);
		break;
	case SOF_IPC4_PIPE_RESET:
		/* RESET if the pipeline is neither running nor paused */
		if (!spipe->started_count && !spipe->paused_count)
			sof_ipc4_add_pipeline_by_priority(trigger_list, pipe_widget, pipe_priority,
							  true);
		break;
	case SOF_IPC4_PIPE_PAUSED:
		/* Pause the pipeline only when its started_count is 1 more than paused_count */
		if (spipe->paused_count == (spipe->started_count - 1))
			sof_ipc4_add_pipeline_by_priority(trigger_list, pipe_widget, pipe_priority,
							  true);
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

	if (pipeline->skip_during_fe_trigger && state != SOF_IPC4_PIPE_RESET)
		return;

	/* set state for pipeline if it was just triggered */
	for (i = 0; i < trigger_list->count; i++) {
		if (trigger_list->pipeline_instance_ids[i] == pipe_widget->instance_id) {
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

/*
 * Chained DMA is a special case where there is no processing on
 * DSP. The samples are just moved over by host side DMA to a single
 * buffer on DSP and directly from there to link DMA. However, the
 * model on SOF driver has two notional pipelines, one at host DAI,
 * and another at link DAI. They both shall have the use_chain_dma
 * attribute.
 */

static int sof_ipc4_chain_dma_trigger(struct snd_sof_dev *sdev,
				      struct snd_sof_pcm *spcm, int direction,
				      struct snd_sof_pcm_stream_pipeline_list *pipeline_list,
				      int state, int cmd)
{
	struct sof_ipc4_fw_data *ipc4_data = sdev->private;
	struct sof_ipc4_pcm_stream_priv *stream_priv;
	bool allocate, enable, set_fifo_size;
	struct sof_ipc4_msg msg = {{ 0 }};
	int ret, i;

	stream_priv = spcm->stream[direction].private;

	switch (state) {
	case SOF_IPC4_PIPE_RUNNING: /* Allocate and start chained dma */
		allocate = true;
		enable = true;
		/*
		 * SOF assumes creation of a new stream from the presence of fifo_size
		 * in the message, so we must leave it out in pause release case.
		 */
		if (cmd == SNDRV_PCM_TRIGGER_PAUSE_RELEASE)
			set_fifo_size = false;
		else
			set_fifo_size = true;
		break;
	case SOF_IPC4_PIPE_PAUSED: /* Disable chained DMA. */
		allocate = true;
		enable = false;
		set_fifo_size = false;
		break;
	case SOF_IPC4_PIPE_RESET: /* Disable and free chained DMA. */

		/* ChainDMA can only be reset if it has been allocated */
		if (!stream_priv->chain_dma_allocated)
			return 0;

		allocate = false;
		enable = false;
		set_fifo_size = false;
		break;
	default:
		dev_err(sdev->dev, "Unexpected state %d", state);
		return -EINVAL;
	}

	msg.primary = SOF_IPC4_MSG_TYPE_SET(SOF_IPC4_GLB_CHAIN_DMA);
	msg.primary |= SOF_IPC4_MSG_DIR(SOF_IPC4_MSG_REQUEST);
	msg.primary |= SOF_IPC4_MSG_TARGET(SOF_IPC4_FW_GEN_MSG);

	/*
	 * To set-up the DMA chain, the host DMA ID and SCS setting
	 * are retrieved from the host pipeline configuration. Likewise
	 * the link DMA ID and fifo_size are retrieved from the link
	 * pipeline configuration.
	 */
	for (i = 0; i < pipeline_list->count; i++) {
		struct snd_sof_pipeline *spipe = pipeline_list->pipelines[i];
		struct snd_sof_widget *pipe_widget = spipe->pipe_widget;
		struct sof_ipc4_pipeline *pipeline = pipe_widget->private;

		if (!pipeline->use_chain_dma) {
			dev_err(sdev->dev,
				"All pipelines in chained DMA stream should have use_chain_dma attribute set.");
			return -EINVAL;
		}

		msg.primary |= pipeline->msg.primary;

		/* Add fifo_size (actually DMA buffer size) field to the message */
		if (set_fifo_size)
			msg.extension |= pipeline->msg.extension;
	}

	if (direction == SNDRV_PCM_STREAM_CAPTURE) {
		/*
		 * For ChainDMA the DMA ids are unique with the following mapping:
		 * playback:  0 - (num_playback_streams - 1)
		 * capture:   num_playback_streams - (num_playback_streams +
		 *				      num_capture_streams - 1)
		 *
		 * Add the num_playback_streams offset to the DMA ids stored in
		 * msg.primary in case capture
		 */
		msg.primary +=  SOF_IPC4_GLB_CHAIN_DMA_HOST_ID(ipc4_data->num_playback_streams);
		msg.primary +=  SOF_IPC4_GLB_CHAIN_DMA_LINK_ID(ipc4_data->num_playback_streams);
	}

	if (allocate)
		msg.primary |= SOF_IPC4_GLB_CHAIN_DMA_ALLOCATE_MASK;

	if (enable)
		msg.primary |= SOF_IPC4_GLB_CHAIN_DMA_ENABLE_MASK;

	ret = sof_ipc_tx_message_no_reply(sdev->ipc, &msg, 0);
	/* Update the ChainDMA allocation state */
	if (!ret)
		stream_priv->chain_dma_allocated = allocate;

	return ret;
}

static int sof_ipc4_trigger_pipelines(struct snd_soc_component *component,
				      struct snd_pcm_substream *substream, int state, int cmd)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(component);
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_sof_pcm_stream_pipeline_list *pipeline_list;
	struct sof_ipc4_fw_data *ipc4_data = sdev->private;
	struct ipc4_pipeline_set_state_data *trigger_list;
	struct snd_sof_widget *pipe_widget;
	struct sof_ipc4_pipeline *pipeline;
	struct snd_sof_pipeline *spipe;
	struct snd_sof_pcm *spcm;
	u8 *pipe_priority;
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

	spipe = pipeline_list->pipelines[0];
	pipe_widget = spipe->pipe_widget;
	pipeline = pipe_widget->private;

	/*
	 * If use_chain_dma attribute is set we proceed to chained DMA
	 * trigger function that handles the rest for the substream.
	 */
	if (pipeline->use_chain_dma)
		return sof_ipc4_chain_dma_trigger(sdev, spcm, substream->stream,
						  pipeline_list, state, cmd);

	/* allocate memory for the pipeline data */
	trigger_list = kzalloc(struct_size(trigger_list, pipeline_instance_ids,
					   pipeline_list->count), GFP_KERNEL);
	if (!trigger_list)
		return -ENOMEM;

	pipe_priority = kzalloc(pipeline_list->count, GFP_KERNEL);
	if (!pipe_priority) {
		kfree(trigger_list);
		return -ENOMEM;
	}

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
			sof_ipc4_add_pipeline_to_trigger_list(sdev, state, spipe, trigger_list,
							      pipe_priority);
		}
	else
		for (i = 0; i < pipeline_list->count; i++) {
			spipe = pipeline_list->pipelines[i];
			sof_ipc4_add_pipeline_to_trigger_list(sdev, state, spipe, trigger_list,
							      pipe_priority);
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
	if (state == SOF_IPC4_PIPE_PAUSED) {
		struct sof_ipc4_timestamp_info *time_info;

		/*
		 * Invalidate the stream_start_offset to make sure that it is
		 * going to be updated if the stream resumes
		 */
		time_info = sof_ipc4_sps_to_time_info(&spcm->stream[substream->stream]);
		if (time_info)
			time_info->stream_start_offset = SOF_IPC4_INVALID_STREAM_POSITION;

		goto free;
	}
skip_pause_transition:
	/* else set the RUNNING/RESET state in the DSP */
	ret = sof_ipc4_set_multi_pipeline_state(sdev, state, trigger_list);
	if (ret < 0) {
		dev_err(sdev->dev, "failed to set final state %d for all pipelines\n", state);
		/*
		 * workaround: if the firmware is crashed while setting the
		 * pipelines to reset state we must ignore the error code and
		 * reset it to 0.
		 * Since the firmware is crashed we will not send IPC messages
		 * and we are going to see errors printed, but the state of the
		 * widgets will be correct for the next boot.
		 */
		if (sdev->fw_state != SOF_FW_CRASHED || state != SOF_IPC4_PIPE_RESET)
			goto free;

		ret = 0;
	}

	/* update RUNNING/RESET state for all pipelines that were just triggered */
	for (i = 0; i < pipeline_list->count; i++) {
		spipe = pipeline_list->pipelines[i];
		sof_ipc4_update_pipeline_state(sdev, state, cmd, spipe, trigger_list);
	}

free:
	mutex_unlock(&ipc4_data->pipeline_state_mutex);
	kfree(trigger_list);
	kfree(pipe_priority);
	return ret;
}

static int sof_ipc4_pcm_trigger(struct snd_soc_component *component,
				struct snd_pcm_substream *substream, int cmd)
{
	int state;

	/* determine the pipeline state */
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_START:
		state = SOF_IPC4_PIPE_RUNNING;
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
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

/*
 * Fixup DAI link parameters for sampling rate based on
 * DAI copier configuration.
 */
static int sof_ipc4_pcm_dai_link_fixup_rate(struct snd_sof_dev *sdev,
					    struct snd_pcm_hw_params *params,
					    struct sof_ipc4_copier *ipc4_copier)
{
	struct sof_ipc4_pin_format *pin_fmts = ipc4_copier->available_fmt.input_pin_fmts;
	struct snd_interval *rate = hw_param_interval(params, SNDRV_PCM_HW_PARAM_RATE);
	int num_input_formats = ipc4_copier->available_fmt.num_input_formats;
	unsigned int fe_rate = params_rate(params);
	bool fe_be_rate_match = false;
	bool single_be_rate = true;
	unsigned int be_rate;
	int i;

	/*
	 * Copier does not change sampling rate, so we
	 * need to only consider the input pin information.
	 */
	for (i = 0; i < num_input_formats; i++) {
		unsigned int val = pin_fmts[i].audio_fmt.sampling_frequency;

		if (i == 0)
			be_rate = val;
		else if (val != be_rate)
			single_be_rate = false;

		if (val == fe_rate) {
			fe_be_rate_match = true;
			break;
		}
	}

	/*
	 * If rate is different than FE rate, topology must
	 * contain an SRC. But we do require topology to
	 * define a single rate in the DAI copier config in
	 * this case (FE rate may be variable).
	 */
	if (!fe_be_rate_match) {
		if (!single_be_rate) {
			dev_err(sdev->dev, "Unable to select sampling rate for DAI link\n");
			return -EINVAL;
		}

		rate->min = be_rate;
		rate->max = rate->min;
	}

	return 0;
}

static int sof_ipc4_pcm_dai_link_fixup(struct snd_soc_pcm_runtime *rtd,
				       struct snd_pcm_hw_params *params)
{
	struct snd_soc_component *component = snd_soc_rtdcom_lookup(rtd, SOF_AUDIO_PCM_DRV_NAME);
	struct snd_sof_dai *dai = snd_sof_find_dai(component, rtd->dai_link->name);
	struct snd_mask *fmt = hw_param_mask(params, SNDRV_PCM_HW_PARAM_FORMAT);
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(component);
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(rtd, 0);
	struct sof_ipc4_audio_format *ipc4_fmt;
	struct sof_ipc4_copier *ipc4_copier;
	bool single_bitdepth = false;
	u32 valid_bits = 0;
	int dir, ret;

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

	for_each_pcm_streams(dir) {
		struct snd_soc_dapm_widget *w = snd_soc_dai_get_widget(cpu_dai, dir);

		if (w) {
			struct sof_ipc4_available_audio_format *available_fmt =
				&ipc4_copier->available_fmt;
			struct snd_sof_widget *swidget = w->dobj.private;
			struct snd_sof_widget *pipe_widget = swidget->spipe->pipe_widget;
			struct sof_ipc4_pipeline *pipeline = pipe_widget->private;

			/* Chain DMA does not use copiers, so no fixup needed */
			if (pipeline->use_chain_dma)
				return 0;

			if (dir == SNDRV_PCM_STREAM_PLAYBACK) {
				if (sof_ipc4_copier_is_single_bitdepth(sdev,
					available_fmt->output_pin_fmts,
					available_fmt->num_output_formats)) {
					ipc4_fmt = &available_fmt->output_pin_fmts->audio_fmt;
					single_bitdepth = true;
				}
			} else {
				if (sof_ipc4_copier_is_single_bitdepth(sdev,
					available_fmt->input_pin_fmts,
					available_fmt->num_input_formats)) {
					ipc4_fmt = &available_fmt->input_pin_fmts->audio_fmt;
					single_bitdepth = true;
				}
			}
		}
	}

	ret = sof_ipc4_pcm_dai_link_fixup_rate(sdev, params, ipc4_copier);
	if (ret)
		return ret;

	if (single_bitdepth) {
		snd_mask_none(fmt);
		valid_bits = SOF_IPC4_AUDIO_FORMAT_CFG_V_BIT_DEPTH(ipc4_fmt->fmt_cfg);
		dev_dbg(component->dev, "Set %s to %d bit format\n", dai->name, valid_bits);
	}

	/* Set format if it is specified */
	switch (valid_bits) {
	case 16:
		snd_mask_set_format(fmt, SNDRV_PCM_FORMAT_S16_LE);
		break;
	case 24:
		snd_mask_set_format(fmt, SNDRV_PCM_FORMAT_S24_LE);
		break;
	case 32:
		snd_mask_set_format(fmt, SNDRV_PCM_FORMAT_S32_LE);
		break;
	default:
		break;
	}

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
	struct sof_ipc4_pcm_stream_priv *stream_priv;
	int stream;

	for_each_pcm_streams(stream) {
		pipeline_list = &spcm->stream[stream].pipeline_list;
		kfree(pipeline_list->pipelines);
		pipeline_list->pipelines = NULL;

		stream_priv = spcm->stream[stream].private;
		kfree(stream_priv->time_info);
		kfree(spcm->stream[stream].private);
		spcm->stream[stream].private = NULL;
	}
}

static int sof_ipc4_pcm_setup(struct snd_sof_dev *sdev, struct snd_sof_pcm *spcm)
{
	struct snd_sof_pcm_stream_pipeline_list *pipeline_list;
	struct sof_ipc4_fw_data *ipc4_data = sdev->private;
	struct sof_ipc4_pcm_stream_priv *stream_priv;
	struct sof_ipc4_timestamp_info *time_info;
	bool support_info = true;
	u32 abi_version;
	u32 abi_offset;
	int stream;

	abi_offset = offsetof(struct sof_ipc4_fw_registers, abi_ver);
	sof_mailbox_read(sdev, sdev->fw_info_box.offset + abi_offset, &abi_version,
			 sizeof(abi_version));

	if (abi_version < SOF_IPC4_FW_REGS_ABI_VER)
		support_info = false;

	/* For delay reporting the get_host_byte_counter callback is needed */
	if (!sof_ops(sdev) || !sof_ops(sdev)->get_host_byte_counter)
		support_info = false;

	for_each_pcm_streams(stream) {
		pipeline_list = &spcm->stream[stream].pipeline_list;

		/* allocate memory for max number of pipeline IDs */
		pipeline_list->pipelines = kcalloc(ipc4_data->max_num_pipelines,
						   sizeof(struct snd_sof_widget *), GFP_KERNEL);
		if (!pipeline_list->pipelines) {
			sof_ipc4_pcm_free(sdev, spcm);
			return -ENOMEM;
		}

		stream_priv = kzalloc(sizeof(*stream_priv), GFP_KERNEL);
		if (!stream_priv) {
			sof_ipc4_pcm_free(sdev, spcm);
			return -ENOMEM;
		}

		spcm->stream[stream].private = stream_priv;

		if (!support_info)
			continue;

		time_info = kzalloc(sizeof(*time_info), GFP_KERNEL);
		if (!time_info) {
			sof_ipc4_pcm_free(sdev, spcm);
			return -ENOMEM;
		}

		stream_priv->time_info = time_info;
	}

	return 0;
}

static void sof_ipc4_build_time_info(struct snd_sof_dev *sdev, struct snd_sof_pcm_stream *sps)
{
	struct sof_ipc4_copier *host_copier = NULL;
	struct sof_ipc4_copier *dai_copier = NULL;
	struct sof_ipc4_llp_reading_slot llp_slot;
	struct sof_ipc4_timestamp_info *time_info;
	struct snd_soc_dapm_widget *widget;
	struct snd_sof_dai *dai;
	int i;

	/* find host & dai to locate info in memory window */
	for_each_dapm_widgets(sps->list, i, widget) {
		struct snd_sof_widget *swidget = widget->dobj.private;

		if (!swidget)
			continue;

		if (WIDGET_IS_AIF(swidget->widget->id)) {
			host_copier = swidget->private;
		} else if (WIDGET_IS_DAI(swidget->widget->id)) {
			dai = swidget->private;
			dai_copier = dai->private;
		}
	}

	/* both host and dai copier must be valid for time_info */
	if (!host_copier || !dai_copier) {
		dev_err(sdev->dev, "host or dai copier are not found\n");
		return;
	}

	time_info = sof_ipc4_sps_to_time_info(sps);
	time_info->host_copier = host_copier;
	time_info->dai_copier = dai_copier;
	time_info->llp_offset = offsetof(struct sof_ipc4_fw_registers,
					 llp_gpdma_reading_slots) + sdev->fw_info_box.offset;

	/* find llp slot used by current dai */
	for (i = 0; i < SOF_IPC4_MAX_LLP_GPDMA_READING_SLOTS; i++) {
		sof_mailbox_read(sdev, time_info->llp_offset, &llp_slot, sizeof(llp_slot));
		if (llp_slot.node_id == dai_copier->data.gtw_cfg.node_id)
			break;

		time_info->llp_offset += sizeof(llp_slot);
	}

	if (i < SOF_IPC4_MAX_LLP_GPDMA_READING_SLOTS)
		return;

	/* if no llp gpdma slot is used, check aggregated sdw slot */
	time_info->llp_offset = offsetof(struct sof_ipc4_fw_registers,
					 llp_sndw_reading_slots) + sdev->fw_info_box.offset;
	for (i = 0; i < SOF_IPC4_MAX_LLP_SNDW_READING_SLOTS; i++) {
		sof_mailbox_read(sdev, time_info->llp_offset, &llp_slot, sizeof(llp_slot));
		if (llp_slot.node_id == dai_copier->data.gtw_cfg.node_id)
			break;

		time_info->llp_offset += sizeof(llp_slot);
	}

	if (i < SOF_IPC4_MAX_LLP_SNDW_READING_SLOTS)
		return;

	/* check EVAD slot */
	time_info->llp_offset = offsetof(struct sof_ipc4_fw_registers,
					 llp_evad_reading_slot) + sdev->fw_info_box.offset;
	sof_mailbox_read(sdev, time_info->llp_offset, &llp_slot, sizeof(llp_slot));
	if (llp_slot.node_id != dai_copier->data.gtw_cfg.node_id)
		time_info->llp_offset = 0;
}

static int sof_ipc4_pcm_hw_params(struct snd_soc_component *component,
				  struct snd_pcm_substream *substream,
				  struct snd_pcm_hw_params *params,
				  struct snd_sof_platform_stream_params *platform_params)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(component);
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct sof_ipc4_timestamp_info *time_info;
	struct snd_sof_pcm *spcm;

	spcm = snd_sof_find_spcm_dai(component, rtd);
	if (!spcm)
		return -EINVAL;

	time_info = sof_ipc4_sps_to_time_info(&spcm->stream[substream->stream]);
	/* delay calculation is not supported by current fw_reg ABI */
	if (!time_info)
		return 0;

	time_info->stream_start_offset = SOF_IPC4_INVALID_STREAM_POSITION;
	time_info->llp_offset = 0;

	sof_ipc4_build_time_info(sdev, &spcm->stream[substream->stream]);

	return 0;
}

static int sof_ipc4_get_stream_start_offset(struct snd_sof_dev *sdev,
					    struct snd_pcm_substream *substream,
					    struct snd_sof_pcm_stream *sps,
					    struct sof_ipc4_timestamp_info *time_info)
{
	struct sof_ipc4_copier *host_copier = time_info->host_copier;
	struct sof_ipc4_copier *dai_copier = time_info->dai_copier;
	struct sof_ipc4_pipeline_registers ppl_reg;
	u32 dai_sample_size;
	u32 ch, node_index;
	u32 offset;

	if (!host_copier || !dai_copier)
		return -EINVAL;

	if (host_copier->data.gtw_cfg.node_id == SOF_IPC4_INVALID_NODE_ID)
		return -EINVAL;

	node_index = SOF_IPC4_NODE_INDEX(host_copier->data.gtw_cfg.node_id);
	offset = offsetof(struct sof_ipc4_fw_registers, pipeline_regs) + node_index * sizeof(ppl_reg);
	sof_mailbox_read(sdev, sdev->fw_info_box.offset + offset, &ppl_reg, sizeof(ppl_reg));
	if (ppl_reg.stream_start_offset == SOF_IPC4_INVALID_STREAM_POSITION)
		return -EINVAL;

	ch = dai_copier->data.out_format.fmt_cfg;
	ch = SOF_IPC4_AUDIO_FORMAT_CFG_CHANNELS_COUNT(ch);
	dai_sample_size = (dai_copier->data.out_format.bit_depth >> 3) * ch;

	/* convert offsets to frame count */
	time_info->stream_start_offset = ppl_reg.stream_start_offset;
	do_div(time_info->stream_start_offset, dai_sample_size);
	time_info->stream_end_offset = ppl_reg.stream_end_offset;
	do_div(time_info->stream_end_offset, dai_sample_size);

	/*
	 * Calculate the wrap boundary need to be used for delay calculation
	 * The host counter is in bytes, it will wrap earlier than the frames
	 * based link counter.
	 */
	time_info->boundary = div64_u64(~((u64)0),
					frames_to_bytes(substream->runtime, 1));
	/* Initialize the delay value to 0 (no delay) */
	time_info->delay = 0;

	return 0;
}

static int sof_ipc4_pcm_pointer(struct snd_soc_component *component,
				struct snd_pcm_substream *substream,
				snd_pcm_uframes_t *pointer)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(component);
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct sof_ipc4_timestamp_info *time_info;
	struct sof_ipc4_llp_reading_slot llp;
	snd_pcm_uframes_t head_cnt, tail_cnt;
	struct snd_sof_pcm_stream *sps;
	u64 dai_cnt, host_cnt, host_ptr;
	struct snd_sof_pcm *spcm;
	int ret;

	spcm = snd_sof_find_spcm_dai(component, rtd);
	if (!spcm)
		return -EOPNOTSUPP;

	sps = &spcm->stream[substream->stream];
	time_info = sof_ipc4_sps_to_time_info(sps);
	if (!time_info)
		return -EOPNOTSUPP;

	/*
	 * stream_start_offset is updated to memory window by FW based on
	 * pipeline statistics and it may be invalid if host query happens before
	 * the statistics is complete. And it will not change after the first initiailization.
	 */
	if (time_info->stream_start_offset == SOF_IPC4_INVALID_STREAM_POSITION) {
		ret = sof_ipc4_get_stream_start_offset(sdev, substream, sps, time_info);
		if (ret < 0)
			return -EOPNOTSUPP;
	}

	/* For delay calculation we need the host counter */
	host_cnt = snd_sof_pcm_get_host_byte_counter(sdev, component, substream);
	host_ptr = host_cnt;

	/* convert the host_cnt to frames */
	host_cnt = div64_u64(host_cnt, frames_to_bytes(substream->runtime, 1));

	/*
	 * If the LLP counter is not reported by firmware in the SRAM window
	 * then read the dai (link) counter via host accessible means if
	 * available.
	 */
	if (!time_info->llp_offset) {
		dai_cnt = snd_sof_pcm_get_dai_frame_counter(sdev, component, substream);
		if (!dai_cnt)
			return -EOPNOTSUPP;
	} else {
		sof_mailbox_read(sdev, time_info->llp_offset, &llp, sizeof(llp));
		dai_cnt = ((u64)llp.reading.llp_u << 32) | llp.reading.llp_l;
	}
	dai_cnt += time_info->stream_end_offset;

	/* In two cases dai dma counter is not accurate
	 * (1) dai pipeline is started before host pipeline
	 * (2) multiple streams mixed into one. Each stream has the same dai dma
	 *     counter
	 *
	 * Firmware calculates correct stream_start_offset for all cases
	 * including above two.
	 * Driver subtracts stream_start_offset from dai dma counter to get
	 * accurate one
	 */

	/*
	 * On stream start the dai counter might not yet have reached the
	 * stream_start_offset value which means that no frames have left the
	 * DSP yet from the audio stream (on playback, capture streams have
	 * offset of 0 as we start capturing right away).
	 * In this case we need to adjust the distance between the counters by
	 * increasing the host counter by (offset - dai_counter).
	 * Otherwise the dai_counter needs to be adjusted to reflect the number
	 * of valid frames passed on the DAI side.
	 *
	 * The delay is the difference between the counters on the two
	 * sides of the DSP.
	 */
	if (dai_cnt < time_info->stream_start_offset) {
		host_cnt += time_info->stream_start_offset - dai_cnt;
		dai_cnt = 0;
	} else {
		dai_cnt -= time_info->stream_start_offset;
	}

	/* Wrap the dai counter at the boundary where the host counter wraps */
	div64_u64_rem(dai_cnt, time_info->boundary, &dai_cnt);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		head_cnt = host_cnt;
		tail_cnt = dai_cnt;
	} else {
		head_cnt = dai_cnt;
		tail_cnt = host_cnt;
	}

	if (head_cnt < tail_cnt) {
		time_info->delay = time_info->boundary - tail_cnt + head_cnt;
		goto out;
	}

	time_info->delay =  head_cnt - tail_cnt;

out:
	/*
	 * Convert the host byte counter to PCM pointer which wraps in buffer
	 * and it is in frames
	 */
	div64_u64_rem(host_ptr, snd_pcm_lib_buffer_bytes(substream), &host_ptr);
	*pointer = bytes_to_frames(substream->runtime, host_ptr);

	return 0;
}

static snd_pcm_sframes_t sof_ipc4_pcm_delay(struct snd_soc_component *component,
					    struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct sof_ipc4_timestamp_info *time_info;
	struct snd_sof_pcm *spcm;

	spcm = snd_sof_find_spcm_dai(component, rtd);
	if (!spcm)
		return 0;

	time_info = sof_ipc4_sps_to_time_info(&spcm->stream[substream->stream]);
	/*
	 * Report the stored delay value calculated in the pointer callback.
	 * In the unlikely event that the calculation was skipped/aborted, the
	 * default 0 delay returned.
	 */
	if (time_info)
		return time_info->delay;

	/* No delay information available, report 0 as delay */
	return 0;

}

const struct sof_ipc_pcm_ops ipc4_pcm_ops = {
	.hw_params = sof_ipc4_pcm_hw_params,
	.trigger = sof_ipc4_pcm_trigger,
	.hw_free = sof_ipc4_pcm_hw_free,
	.dai_link_fixup = sof_ipc4_pcm_dai_link_fixup,
	.pcm_setup = sof_ipc4_pcm_setup,
	.pcm_free = sof_ipc4_pcm_free,
	.pointer = sof_ipc4_pcm_pointer,
	.delay = sof_ipc4_pcm_delay,
	.ipc_first_on_start = true,
	.platform_stop_during_hw_free = true,
};
