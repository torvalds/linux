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

static int sof_ipc4_trigger_pipelines(struct snd_soc_component *component,
				      struct snd_pcm_substream *substream, int state)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(component);
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct snd_sof_widget *pipeline_widget;
	struct snd_soc_dapm_widget_list *list;
	struct snd_soc_dapm_widget *widget;
	struct sof_ipc4_pipeline *pipeline;
	struct snd_sof_widget *swidget;
	struct snd_sof_pcm *spcm;
	int ret = 0;
	int num_widgets;

	spcm = snd_sof_find_spcm_dai(component, rtd);
	if (!spcm)
		return -EINVAL;

	list = spcm->stream[substream->stream].list;

	for_each_dapm_widgets(list, num_widgets, widget) {
		swidget = widget->dobj.private;

		if (!swidget)
			continue;

		/*
		 * set pipeline state for both FE and BE pipelines for RUNNING state.
		 * For PAUSE/RESET, set the pipeline state only for the FE pipeline.
		 */
		switch (state) {
		case SOF_IPC4_PIPE_PAUSED:
		case SOF_IPC4_PIPE_RESET:
			if (!WIDGET_IS_AIF(swidget->id))
				continue;
			break;
		default:
			break;
		}

		/* find pipeline widget for the pipeline that this widget belongs to */
		pipeline_widget = swidget->pipe_widget;
		pipeline = (struct sof_ipc4_pipeline *)pipeline_widget->private;

		if (pipeline->state == state)
			continue;

		/* first set the pipeline to PAUSED state */
		if (pipeline->state != SOF_IPC4_PIPE_PAUSED) {
			ret = sof_ipc4_set_pipeline_state(sdev, swidget->pipeline_id,
							  SOF_IPC4_PIPE_PAUSED);
			if (ret < 0) {
				dev_err(sdev->dev, "failed to pause pipeline %d\n",
					swidget->pipeline_id);
				return ret;
			}
		}

		pipeline->state = SOF_IPC4_PIPE_PAUSED;

		if (pipeline->state == state)
			continue;

		/* then set the final state */
		ret = sof_ipc4_set_pipeline_state(sdev, swidget->pipeline_id, state);
		if (ret < 0) {
			dev_err(sdev->dev, "failed to set state %d for pipeline %d\n",
				state, swidget->pipeline_id);
			break;
		}

		pipeline->state = state;
	}

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
	return sof_ipc4_trigger_pipelines(component, substream, state);
}

static int sof_ipc4_pcm_hw_free(struct snd_soc_component *component,
				struct snd_pcm_substream *substream)
{
	return sof_ipc4_trigger_pipelines(component, substream, SOF_IPC4_PIPE_RESET);
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
	struct snd_mask *fmt = hw_param_mask(params, SNDRV_PCM_HW_PARAM_FORMAT);
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(component);
	struct sof_ipc4_copier *ipc4_copier;
	struct snd_soc_dpcm *dpcm;

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

	/*
	 * Set trigger order for capture to SND_SOC_DPCM_TRIGGER_PRE. This is required
	 * to ensure that the BE DAI pipeline gets stopped/suspended before the FE DAI
	 * pipeline gets triggered and the pipeline widgets are freed.
	 */
	for_each_dpcm_fe(rtd, SNDRV_PCM_STREAM_CAPTURE, dpcm) {
		struct snd_soc_pcm_runtime *fe = dpcm->fe;

		fe->dai_link->trigger[SNDRV_PCM_STREAM_CAPTURE] = SND_SOC_DPCM_TRIGGER_PRE;
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

const struct sof_ipc_pcm_ops ipc4_pcm_ops = {
	.trigger = sof_ipc4_pcm_trigger,
	.hw_free = sof_ipc4_pcm_hw_free,
	.dai_link_fixup = sof_ipc4_pcm_dai_link_fixup,
};
