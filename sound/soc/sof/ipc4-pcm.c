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
#include "ops.h"
#include "ipc4-priv.h"
#include "ipc4-topology.h"
#include "ipc4-fw-reg.h"

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

	return sof_ipc_tx_message_no_reply(sdev->ipc, &msg, ipc_size);
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

	return sof_ipc_tx_message_no_reply(sdev->ipc, &msg, 0);
}
EXPORT_SYMBOL(sof_ipc4_set_pipeline_state);

static void
sof_ipc4_add_pipeline_to_trigger_list(struct snd_sof_dev *sdev, int state,
				      struct snd_sof_pipeline *spipe,
				      struct ipc4_pipeline_set_state_data *trigger_list)
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

	if (pipeline->skip_during_fe_trigger && state != SOF_IPC4_PIPE_RESET)
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

/*
 * Chained DMA is a special case where there is no processing on
 * DSP. The samples are just moved over by host side DMA to a single
 * buffer on DSP and directly from there to link DMA. However, the
 * model on SOF driver has two notional pipelines, one at host DAI,
 * and another at link DAI. They both shall have the use_chain_dma
 * attribute.
 */

static int sof_ipc4_chain_dma_trigger(struct snd_sof_dev *sdev,
				      struct snd_sof_pcm_stream_pipeline_list *pipeline_list,
				      int state, int cmd)
{
	bool allocate, enable, set_fifo_size;
	struct sof_ipc4_msg msg = {{ 0 }};
	int i;

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

	if (allocate)
		msg.primary |= SOF_IPC4_GLB_CHAIN_DMA_ALLOCATE_MASK;

	if (enable)
		msg.primary |= SOF_IPC4_GLB_CHAIN_DMA_ENABLE_MASK;

	return sof_ipc_tx_message_no_reply(sdev->ipc, &msg, 0);
}

static int sof_ipc4_trigger_pipelines(struct snd_soc_component *component,
				      struct snd_pcm_substream *substream, int state, int cmd)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(component);
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct snd_sof_pcm_stream_pipeline_list *pipeline_list;
	struct sof_ipc4_fw_data *ipc4_data = sdev->private;
	struct ipc4_pipeline_set_state_data *trigger_list;
	struct snd_sof_widget *pipe_widget;
	struct sof_ipc4_pipeline *pipeline;
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

	spipe = pipeline_list->pipelines[0];
	pipe_widget = spipe->pipe_widget;
	pipeline = pipe_widget->private;

	/*
	 * If use_chain_dma attribute is set we proceed to chained DMA
	 * trigger function that handles the rest for the substream.
	 */
	if (pipeline->use_chain_dma)
		return sof_ipc4_chain_dma_trigger(sdev, pipeline_list, state, cmd);

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
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(component);
	struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(rtd, 0);
	struct sof_ipc4_copier *ipc4_copier;
	bool use_chain_dma = false;
	int dir;

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
			struct snd_sof_widget *swidget = w->dobj.private;
			struct snd_sof_widget *pipe_widget = swidget->spipe->pipe_widget;
			struct sof_ipc4_pipeline *pipeline = pipe_widget->private;

			if (pipeline->use_chain_dma)
				use_chain_dma = true;
		}
	}

	/* Chain DMA does not use copiers, so no fixup needed */
	if (!use_chain_dma) {
		int ret = sof_ipc4_pcm_dai_link_fixup_rate(sdev, params, ipc4_copier);

		if (ret)
			return ret;
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
	int stream;

	for_each_pcm_streams(stream) {
		pipeline_list = &spcm->stream[stream].pipeline_list;
		kfree(pipeline_list->pipelines);
		pipeline_list->pipelines = NULL;
		kfree(spcm->stream[stream].private);
		spcm->stream[stream].private = NULL;
	}
}

static int sof_ipc4_pcm_setup(struct snd_sof_dev *sdev, struct snd_sof_pcm *spcm)
{
	struct snd_sof_pcm_stream_pipeline_list *pipeline_list;
	struct sof_ipc4_fw_data *ipc4_data = sdev->private;
	struct sof_ipc4_timestamp_info *stream_info;
	bool support_info = true;
	u32 abi_version;
	u32 abi_offset;
	int stream;

	abi_offset = offsetof(struct sof_ipc4_fw_registers, abi_ver);
	sof_mailbox_read(sdev, sdev->fw_info_box.offset + abi_offset, &abi_version,
			 sizeof(abi_version));

	if (abi_version < SOF_IPC4_FW_REGS_ABI_VER)
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

		if (!support_info)
			continue;

		stream_info = kzalloc(sizeof(*stream_info), GFP_KERNEL);
		if (!stream_info) {
			sof_ipc4_pcm_free(sdev, spcm);
			return -ENOMEM;
		}

		spcm->stream[stream].private = stream_info;
	}

	return 0;
}

static void sof_ipc4_build_time_info(struct snd_sof_dev *sdev, struct snd_sof_pcm_stream *spcm)
{
	struct sof_ipc4_copier *host_copier = NULL;
	struct sof_ipc4_copier *dai_copier = NULL;
	struct sof_ipc4_llp_reading_slot llp_slot;
	struct sof_ipc4_timestamp_info *info;
	struct snd_soc_dapm_widget *widget;
	struct snd_sof_dai *dai;
	int i;

	/* find host & dai to locate info in memory window */
	for_each_dapm_widgets(spcm->list, i, widget) {
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

	info = spcm->private;
	info->host_copier = host_copier;
	info->dai_copier = dai_copier;
	info->llp_offset = offsetof(struct sof_ipc4_fw_registers, llp_gpdma_reading_slots) +
				    sdev->fw_info_box.offset;

	/* find llp slot used by current dai */
	for (i = 0; i < SOF_IPC4_MAX_LLP_GPDMA_READING_SLOTS; i++) {
		sof_mailbox_read(sdev, info->llp_offset, &llp_slot, sizeof(llp_slot));
		if (llp_slot.node_id == dai_copier->data.gtw_cfg.node_id)
			break;

		info->llp_offset += sizeof(llp_slot);
	}

	if (i < SOF_IPC4_MAX_LLP_GPDMA_READING_SLOTS)
		return;

	/* if no llp gpdma slot is used, check aggregated sdw slot */
	info->llp_offset = offsetof(struct sof_ipc4_fw_registers, llp_sndw_reading_slots) +
					sdev->fw_info_box.offset;
	for (i = 0; i < SOF_IPC4_MAX_LLP_SNDW_READING_SLOTS; i++) {
		sof_mailbox_read(sdev, info->llp_offset, &llp_slot, sizeof(llp_slot));
		if (llp_slot.node_id == dai_copier->data.gtw_cfg.node_id)
			break;

		info->llp_offset += sizeof(llp_slot);
	}

	if (i < SOF_IPC4_MAX_LLP_SNDW_READING_SLOTS)
		return;

	/* check EVAD slot */
	info->llp_offset = offsetof(struct sof_ipc4_fw_registers, llp_evad_reading_slot) +
					sdev->fw_info_box.offset;
	sof_mailbox_read(sdev, info->llp_offset, &llp_slot, sizeof(llp_slot));
	if (llp_slot.node_id != dai_copier->data.gtw_cfg.node_id) {
		dev_info(sdev->dev, "no llp found, fall back to default HDA path");
		info->llp_offset = 0;
	}
}

static int sof_ipc4_pcm_hw_params(struct snd_soc_component *component,
				  struct snd_pcm_substream *substream,
				  struct snd_pcm_hw_params *params,
				  struct snd_sof_platform_stream_params *platform_params)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(component);
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct sof_ipc4_timestamp_info *time_info;
	struct snd_sof_pcm *spcm;

	spcm = snd_sof_find_spcm_dai(component, rtd);
	if (!spcm)
		return -EINVAL;

	time_info = spcm->stream[substream->stream].private;
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
					    struct snd_sof_pcm_stream *stream,
					    struct sof_ipc4_timestamp_info *time_info)
{
	struct sof_ipc4_copier *host_copier = time_info->host_copier;
	struct sof_ipc4_copier *dai_copier = time_info->dai_copier;
	struct sof_ipc4_pipeline_registers ppl_reg;
	u64 stream_start_position;
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

	stream_start_position = ppl_reg.stream_start_offset;
	ch = dai_copier->data.out_format.fmt_cfg;
	ch = SOF_IPC4_AUDIO_FORMAT_CFG_CHANNELS_COUNT(ch);
	dai_sample_size = (dai_copier->data.out_format.bit_depth >> 3) * ch;
	/* convert offset to sample count */
	do_div(stream_start_position, dai_sample_size);
	time_info->stream_start_offset = stream_start_position;

	return 0;
}

static snd_pcm_sframes_t sof_ipc4_pcm_delay(struct snd_soc_component *component,
					    struct snd_pcm_substream *substream)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(component);
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct sof_ipc4_timestamp_info *time_info;
	struct sof_ipc4_llp_reading_slot llp;
	snd_pcm_uframes_t head_ptr, tail_ptr;
	struct snd_sof_pcm_stream *stream;
	struct snd_sof_pcm *spcm;
	u64 tmp_ptr;
	int ret;

	spcm = snd_sof_find_spcm_dai(component, rtd);
	if (!spcm)
		return 0;

	stream = &spcm->stream[substream->stream];
	time_info = stream->private;
	if (!time_info)
		return 0;

	/*
	 * stream_start_offset is updated to memory window by FW based on
	 * pipeline statistics and it may be invalid if host query happens before
	 * the statistics is complete. And it will not change after the first initiailization.
	 */
	if (time_info->stream_start_offset == SOF_IPC4_INVALID_STREAM_POSITION) {
		ret = sof_ipc4_get_stream_start_offset(sdev, substream, stream, time_info);
		if (ret < 0)
			return 0;
	}

	/*
	 * HDaudio links don't support the LLP counter reported by firmware
	 * the link position is read directly from hardware registers.
	 */
	if (!time_info->llp_offset) {
		tmp_ptr = snd_sof_pcm_get_stream_position(sdev, component, substream);
		if (!tmp_ptr)
			return 0;
	} else {
		sof_mailbox_read(sdev, time_info->llp_offset, &llp, sizeof(llp));
		tmp_ptr = ((u64)llp.reading.llp_u << 32) | llp.reading.llp_l;
	}

	/* In two cases dai dma position is not accurate
	 * (1) dai pipeline is started before host pipeline
	 * (2) multiple streams mixed into one. Each stream has the same dai dma position
	 *
	 * Firmware calculates correct stream_start_offset for all cases including above two.
	 * Driver subtracts stream_start_offset from dai dma position to get accurate one
	 */
	tmp_ptr -= time_info->stream_start_offset;

	/* Calculate the delay taking into account that both pointer can wrap */
	div64_u64_rem(tmp_ptr, substream->runtime->boundary, &tmp_ptr);
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		head_ptr = substream->runtime->status->hw_ptr;
		tail_ptr = tmp_ptr;
	} else {
		head_ptr = tmp_ptr;
		tail_ptr = substream->runtime->status->hw_ptr;
	}

	if (head_ptr < tail_ptr)
		return substream->runtime->boundary - tail_ptr + head_ptr;

	return head_ptr - tail_ptr;
}

const struct sof_ipc_pcm_ops ipc4_pcm_ops = {
	.hw_params = sof_ipc4_pcm_hw_params,
	.trigger = sof_ipc4_pcm_trigger,
	.hw_free = sof_ipc4_pcm_hw_free,
	.dai_link_fixup = sof_ipc4_pcm_dai_link_fixup,
	.pcm_setup = sof_ipc4_pcm_setup,
	.pcm_free = sof_ipc4_pcm_free,
	.delay = sof_ipc4_pcm_delay,
	.ipc_first_on_start = true,
	.platform_stop_during_hw_free = true,
};
