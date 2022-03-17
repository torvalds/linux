// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2019 Intel Corporation. All rights reserved.
//
// Author: Ranjani Sridharan <ranjani.sridharan@linux.intel.com>
//

#include <linux/bitfield.h>
#include "sof-audio.h"
#include "ops.h"

static int sof_kcontrol_setup(struct snd_sof_dev *sdev, struct snd_sof_control *scontrol)
{
	int ret;

	/* reset readback offset for scontrol */
	scontrol->readback_offset = 0;

	ret = snd_sof_ipc_set_get_comp_data(scontrol, true);
	if (ret < 0)
		dev_err(sdev->dev, "error: failed kcontrol value set for widget: %d\n",
			scontrol->comp_id);

	return ret;
}

static int sof_dai_config_setup(struct snd_sof_dev *sdev, struct snd_sof_dai *dai)
{
	struct sof_dai_private_data *private = dai->private;
	struct sof_ipc_dai_config *config;
	struct sof_ipc_reply reply;
	int ret;

	config = &private->dai_config[dai->current_config];
	if (!config) {
		dev_err(sdev->dev, "error: no config for DAI %s\n", dai->name);
		return -EINVAL;
	}

	/* set NONE flag to clear all previous settings */
	config->flags = SOF_DAI_CONFIG_FLAGS_NONE;

	ret = sof_ipc_tx_message(sdev->ipc, config->hdr.cmd, config, config->hdr.size,
				 &reply, sizeof(reply));

	if (ret < 0)
		dev_err(sdev->dev, "error: failed to set dai config for %s\n", dai->name);

	return ret;
}

static int sof_widget_kcontrol_setup(struct snd_sof_dev *sdev, struct snd_sof_widget *swidget)
{
	struct snd_sof_control *scontrol;
	int ret;

	/* set up all controls for the widget */
	list_for_each_entry(scontrol, &sdev->kcontrol_list, list)
		if (scontrol->comp_id == swidget->comp_id) {
			/* set kcontrol data in DSP */
			ret = sof_kcontrol_setup(sdev, scontrol);
			if (ret < 0) {
				dev_err(sdev->dev, "error: fail to set up kcontrols for widget %s\n",
					swidget->widget->name);
				return ret;
			}

			/*
			 * Read back the data from the DSP for static widgets. This is particularly
			 * useful for binary kcontrols associated with static pipeline widgets to
			 * initialize the data size to match that in the DSP.
			 */
			if (swidget->dynamic_pipeline_widget)
				continue;

			ret = snd_sof_ipc_set_get_comp_data(scontrol, false);
			if (ret < 0)
				dev_warn(sdev->dev, "Failed kcontrol get for control in widget %s\n",
					 swidget->widget->name);
		}

	return 0;
}

static void sof_reset_route_setup_status(struct snd_sof_dev *sdev, struct snd_sof_widget *widget)
{
	struct snd_sof_route *sroute;

	list_for_each_entry(sroute, &sdev->route_list, list)
		if (sroute->src_widget == widget || sroute->sink_widget == widget)
			sroute->setup = false;
}

int sof_widget_free(struct snd_sof_dev *sdev, struct snd_sof_widget *swidget)
{
	struct sof_ipc_free ipc_free = {
		.hdr = {
			.size = sizeof(ipc_free),
			.cmd = SOF_IPC_GLB_TPLG_MSG,
		},
		.id = swidget->comp_id,
	};
	struct sof_ipc_reply reply;
	int ret, err;

	if (!swidget->private)
		return 0;

	/* only free when use_count is 0 */
	if (--swidget->use_count)
		return 0;

	switch (swidget->id) {
	case snd_soc_dapm_scheduler:
	{
		ipc_free.hdr.cmd |= SOF_IPC_TPLG_PIPE_FREE;
		break;
	}
	case snd_soc_dapm_buffer:
		ipc_free.hdr.cmd |= SOF_IPC_TPLG_BUFFER_FREE;
		break;
	case snd_soc_dapm_dai_in:
	case snd_soc_dapm_dai_out:
	{
		struct snd_sof_dai *dai = swidget->private;

		dai->configured = false;
		fallthrough;
	}
	default:
		ipc_free.hdr.cmd |= SOF_IPC_TPLG_COMP_FREE;
		break;
	}

	/* continue to disable core even if IPC fails */
	err = sof_ipc_tx_message(sdev->ipc, ipc_free.hdr.cmd, &ipc_free, sizeof(ipc_free),
				 &reply, sizeof(reply));
	if (err < 0)
		dev_err(sdev->dev, "error: failed to free widget %s\n", swidget->widget->name);

	/*
	 * disable widget core. continue to route setup status and complete flag
	 * even if this fails and return the appropriate error
	 */
	ret = snd_sof_dsp_core_put(sdev, swidget->core);
	if (ret < 0) {
		dev_err(sdev->dev, "error: failed to disable target core: %d for widget %s\n",
			swidget->core, swidget->widget->name);
		if (!err)
			err = ret;
	}

	/* reset route setup status for all routes that contain this widget */
	sof_reset_route_setup_status(sdev, swidget);
	swidget->complete = 0;

	/*
	 * free the scheduler widget (same as pipe_widget) associated with the current swidget.
	 * skip for static pipelines
	 */
	if (swidget->dynamic_pipeline_widget && swidget->id != snd_soc_dapm_scheduler) {
		ret = sof_widget_free(sdev, swidget->pipe_widget);
		if (ret < 0 && !err)
			err = ret;
	}

	if (!err)
		dev_dbg(sdev->dev, "widget %s freed\n", swidget->widget->name);

	return err;
}
EXPORT_SYMBOL(sof_widget_free);

int sof_widget_setup(struct snd_sof_dev *sdev, struct snd_sof_widget *swidget)
{
	struct sof_ipc_pipe_new *pipeline;
	struct sof_ipc_comp_reply r;
	struct sof_ipc_cmd_hdr *hdr;
	struct sof_ipc_comp *comp;
	struct snd_sof_dai *dai;
	int ret;

	/* skip if there is no private data */
	if (!swidget->private)
		return 0;

	/* widget already set up */
	if (++swidget->use_count > 1)
		return 0;

	/*
	 * The scheduler widget for a pipeline is not part of the connected DAPM
	 * widget list and it needs to be set up before the widgets in the pipeline
	 * are set up. The use_count for the scheduler widget is incremented for every
	 * widget in a given pipeline to ensure that it is freed only after the last
	 * widget in the pipeline is freed. Skip setting up scheduler widget for static pipelines.
	 */
	if (swidget->dynamic_pipeline_widget && swidget->id != snd_soc_dapm_scheduler) {
		if (!swidget->pipe_widget) {
			dev_err(sdev->dev, "No scheduler widget set for %s\n",
				swidget->widget->name);
			ret = -EINVAL;
			goto use_count_dec;
		}

		ret = sof_widget_setup(sdev, swidget->pipe_widget);
		if (ret < 0)
			goto use_count_dec;
	}

	/* enable widget core */
	ret = snd_sof_dsp_core_get(sdev, swidget->core);
	if (ret < 0) {
		dev_err(sdev->dev, "error: failed to enable target core for widget %s\n",
			swidget->widget->name);
		goto pipe_widget_free;
	}

	switch (swidget->id) {
	case snd_soc_dapm_dai_in:
	case snd_soc_dapm_dai_out:
	{
		struct sof_dai_private_data *dai_data;

		dai = swidget->private;
		dai_data = dai->private;
		comp = &dai_data->comp_dai->comp;
		dai->configured = false;

		ret = sof_ipc_tx_message(sdev->ipc, comp->hdr.cmd, dai_data->comp_dai,
					 comp->hdr.size, &r, sizeof(r));
		if (ret < 0) {
			dev_err(sdev->dev, "error: failed to load widget %s\n",
				swidget->widget->name);
			goto core_put;
		}

		ret = sof_dai_config_setup(sdev, dai);
		if (ret < 0) {
			dev_err(sdev->dev, "error: failed to load dai config for DAI %s\n",
				swidget->widget->name);

			/*
			 * widget use_count and core ref_count will both be decremented by
			 * sof_widget_free()
			 */
			sof_widget_free(sdev, swidget);
			return ret;
		}
		break;
	}
	case snd_soc_dapm_scheduler:
		pipeline = swidget->private;
		ret = sof_ipc_tx_message(sdev->ipc, pipeline->hdr.cmd, pipeline,
					 sizeof(*pipeline), &r, sizeof(r));
		break;
	default:
		hdr = swidget->private;
		ret = sof_ipc_tx_message(sdev->ipc, hdr->cmd, swidget->private, hdr->size,
					 &r, sizeof(r));
		break;
	}
	if (ret < 0) {
		dev_err(sdev->dev, "error: failed to load widget %s\n", swidget->widget->name);
		goto core_put;
	}

	/* restore kcontrols for widget */
	ret = sof_widget_kcontrol_setup(sdev, swidget);
	if (ret < 0) {
		dev_err(sdev->dev, "error: failed to restore kcontrols for widget %s\n",
			swidget->widget->name);
		/*
		 * widget use_count and core ref_count will both be decremented by
		 * sof_widget_free()
		 */
		sof_widget_free(sdev, swidget);
		return ret;
	}

	dev_dbg(sdev->dev, "widget %s setup complete\n", swidget->widget->name);

	return 0;

core_put:
	snd_sof_dsp_core_put(sdev, swidget->core);
pipe_widget_free:
	if (swidget->id != snd_soc_dapm_scheduler)
		sof_widget_free(sdev, swidget->pipe_widget);
use_count_dec:
	swidget->use_count--;
	return ret;
}
EXPORT_SYMBOL(sof_widget_setup);

static int sof_route_setup(struct snd_sof_dev *sdev, struct snd_soc_dapm_widget *wsource,
			   struct snd_soc_dapm_widget *wsink)
{
	const struct sof_ipc_tplg_ops *ipc_tplg_ops = sdev->ipc->ops->tplg;
	struct snd_sof_widget *src_widget = wsource->dobj.private;
	struct snd_sof_widget *sink_widget = wsink->dobj.private;
	struct snd_sof_route *sroute;
	bool route_found = false;
	int ret;

	/* ignore routes involving virtual widgets in topology */
	switch (src_widget->id) {
	case snd_soc_dapm_out_drv:
	case snd_soc_dapm_output:
	case snd_soc_dapm_input:
		return 0;
	default:
		break;
	}

	switch (sink_widget->id) {
	case snd_soc_dapm_out_drv:
	case snd_soc_dapm_output:
	case snd_soc_dapm_input:
		return 0;
	default:
		break;
	}

	/* find route matching source and sink widgets */
	list_for_each_entry(sroute, &sdev->route_list, list)
		if (sroute->src_widget == src_widget && sroute->sink_widget == sink_widget) {
			route_found = true;
			break;
		}

	if (!route_found) {
		dev_err(sdev->dev, "error: cannot find SOF route for source %s -> %s sink\n",
			wsource->name, wsink->name);
		return -EINVAL;
	}

	/* nothing to do if route is already set up */
	if (sroute->setup)
		return 0;

	ret = ipc_tplg_ops->route_setup(sdev, sroute);
	if (ret < 0)
		return ret;

	sroute->setup = true;
	return 0;
}

static int sof_setup_pipeline_connections(struct snd_sof_dev *sdev,
					  struct snd_soc_dapm_widget_list *list, int dir)
{
	struct snd_soc_dapm_widget *widget;
	struct snd_soc_dapm_path *p;
	int ret;
	int i;

	/*
	 * Set up connections between widgets in the sink/source paths based on direction.
	 * Some non-SOF widgets exist in topology either for compatibility or for the
	 * purpose of connecting a pipeline from a host to a DAI in order to receive the DAPM
	 * events. But they are not handled by the firmware. So ignore them.
	 */
	if (dir == SNDRV_PCM_STREAM_PLAYBACK) {
		for_each_dapm_widgets(list, i, widget) {
			if (!widget->dobj.private)
				continue;

			snd_soc_dapm_widget_for_each_sink_path(widget, p)
				if (p->sink->dobj.private) {
					ret = sof_route_setup(sdev, widget, p->sink);
					if (ret < 0)
						return ret;
				}
		}
	} else {
		for_each_dapm_widgets(list, i, widget) {
			if (!widget->dobj.private)
				continue;

			snd_soc_dapm_widget_for_each_source_path(widget, p)
				if (p->source->dobj.private) {
					ret = sof_route_setup(sdev, p->source, widget);
					if (ret < 0)
						return ret;
				}
		}
	}

	return 0;
}

int sof_widget_list_setup(struct snd_sof_dev *sdev, struct snd_sof_pcm *spcm, int dir)
{
	const struct sof_ipc_tplg_ops *ipc_tplg_ops = sdev->ipc->ops->tplg;
	struct snd_soc_dapm_widget_list *list = spcm->stream[dir].list;
	struct snd_soc_dapm_widget *widget;
	int i, ret, num_widgets;

	/* nothing to set up */
	if (!list)
		return 0;

	/* set up widgets in the list */
	for_each_dapm_widgets(list, num_widgets, widget) {
		struct snd_sof_widget *swidget = widget->dobj.private;

		if (!swidget)
			continue;

		/* set up the widget */
		ret = sof_widget_setup(sdev, swidget);
		if (ret < 0)
			goto widget_free;
	}

	/*
	 * error in setting pipeline connections will result in route status being reset for
	 * routes that were successfully set up when the widgets are freed.
	 */
	ret = sof_setup_pipeline_connections(sdev, list, dir);
	if (ret < 0)
		goto widget_free;

	/* complete pipelines */
	for_each_dapm_widgets(list, i, widget) {
		struct snd_sof_widget *swidget = widget->dobj.private;
		struct snd_sof_widget *pipe_widget;

		if (!swidget)
			continue;

		pipe_widget = swidget->pipe_widget;
		if (!pipe_widget) {
			dev_err(sdev->dev, "error: no pipeline widget found for %s\n",
				swidget->widget->name);
			ret = -EINVAL;
			goto widget_free;
		}

		if (pipe_widget->complete)
			continue;

		if (ipc_tplg_ops->pipeline_complete) {
			pipe_widget->complete = ipc_tplg_ops->pipeline_complete(sdev, pipe_widget);
			if (pipe_widget->complete < 0) {
				ret = pipe_widget->complete;
				goto widget_free;
			}
		}
	}

	return 0;

widget_free:
	/* free all widgets that have been set up successfully */
	for_each_dapm_widgets(list, i, widget) {
		struct snd_sof_widget *swidget = widget->dobj.private;

		if (!swidget)
			continue;

		if (!num_widgets--)
			break;

		sof_widget_free(sdev, swidget);
	}

	return ret;
}

int sof_widget_list_free(struct snd_sof_dev *sdev, struct snd_sof_pcm *spcm, int dir)
{
	struct snd_soc_dapm_widget_list *list = spcm->stream[dir].list;
	struct snd_soc_dapm_widget *widget;
	int i, ret;
	int ret1 = 0;

	/* nothing to free */
	if (!list)
		return 0;

	/*
	 * Free widgets in the list. This can fail but continue freeing other widgets to keep
	 * use_counts balanced.
	 */
	for_each_dapm_widgets(list, i, widget) {
		struct snd_sof_widget *swidget = widget->dobj.private;

		if (!swidget)
			continue;

		/*
		 * free widget and its pipe_widget. Either of these can fail, but free as many as
		 * possible before freeing the list and returning the error.
		 */
		ret = sof_widget_free(sdev, swidget);
		if (ret < 0)
			ret1 = ret;
	}

	snd_soc_dapm_dai_free_widgets(&list);
	spcm->stream[dir].list = NULL;

	return ret1;
}

/*
 * helper to determine if there are only D0i3 compatible
 * streams active
 */
bool snd_sof_dsp_only_d0i3_compatible_stream_active(struct snd_sof_dev *sdev)
{
	struct snd_pcm_substream *substream;
	struct snd_sof_pcm *spcm;
	bool d0i3_compatible_active = false;
	int dir;

	list_for_each_entry(spcm, &sdev->pcm_list, list) {
		for_each_pcm_streams(dir) {
			substream = spcm->stream[dir].substream;
			if (!substream || !substream->runtime)
				continue;

			/*
			 * substream->runtime being not NULL indicates
			 * that the stream is open. No need to check the
			 * stream state.
			 */
			if (!spcm->stream[dir].d0i3_compatible)
				return false;

			d0i3_compatible_active = true;
		}
	}

	return d0i3_compatible_active;
}
EXPORT_SYMBOL(snd_sof_dsp_only_d0i3_compatible_stream_active);

bool snd_sof_stream_suspend_ignored(struct snd_sof_dev *sdev)
{
	struct snd_sof_pcm *spcm;

	list_for_each_entry(spcm, &sdev->pcm_list, list) {
		if (spcm->stream[SNDRV_PCM_STREAM_PLAYBACK].suspend_ignored ||
		    spcm->stream[SNDRV_PCM_STREAM_CAPTURE].suspend_ignored)
			return true;
	}

	return false;
}

int sof_set_hw_params_upon_resume(struct device *dev)
{
	struct snd_sof_dev *sdev = dev_get_drvdata(dev);
	struct snd_pcm_substream *substream;
	struct snd_sof_pcm *spcm;
	snd_pcm_state_t state;
	int dir;

	/*
	 * SOF requires hw_params to be set-up internally upon resume.
	 * So, set the flag to indicate this for those streams that
	 * have been suspended.
	 */
	list_for_each_entry(spcm, &sdev->pcm_list, list) {
		for_each_pcm_streams(dir) {
			/*
			 * do not reset hw_params upon resume for streams that
			 * were kept running during suspend
			 */
			if (spcm->stream[dir].suspend_ignored)
				continue;

			substream = spcm->stream[dir].substream;
			if (!substream || !substream->runtime)
				continue;

			state = substream->runtime->status->state;
			if (state == SNDRV_PCM_STATE_SUSPENDED)
				spcm->prepared[dir] = false;
		}
	}

	/* set internal flag for BE */
	return snd_sof_dsp_hw_params_upon_resume(sdev);
}

int sof_set_up_pipelines(struct snd_sof_dev *sdev, bool verify)
{
	const struct sof_ipc_tplg_ops *ipc_tplg_ops = sdev->ipc->ops->tplg;
	struct sof_ipc_fw_version *v = &sdev->fw_ready.version;
	struct snd_sof_widget *swidget;
	struct snd_sof_route *sroute;
	int ret;

	/* restore pipeline components */
	list_for_each_entry(swidget, &sdev->widget_list, list) {
		/* only set up the widgets belonging to static pipelines */
		if (!verify && swidget->dynamic_pipeline_widget)
			continue;

		/*
		 * For older firmware, skip scheduler widgets in this loop,
		 * sof_widget_setup() will be called in the 'complete pipeline' loop
		 */
		if (v->abi_version < SOF_ABI_VER(3, 19, 0) &&
		    swidget->id == snd_soc_dapm_scheduler)
			continue;

		/* update DAI config. The IPC will be sent in sof_widget_setup() */
		if (WIDGET_IS_DAI(swidget->id)) {
			struct snd_sof_dai *dai = swidget->private;
			struct sof_dai_private_data *private = dai->private;
			struct sof_ipc_dai_config *config;

			if (!dai || !private || !private->dai_config)
				continue;

			config = private->dai_config;
			/*
			 * The link DMA channel would be invalidated for running
			 * streams but not for streams that were in the PAUSED
			 * state during suspend. So invalidate it here before setting
			 * the dai config in the DSP.
			 */
			if (config->type == SOF_DAI_INTEL_HDA)
				config->hda.link_dma_ch = DMA_CHAN_INVALID;
		}

		ret = sof_widget_setup(sdev, swidget);
		if (ret < 0)
			return ret;
	}

	/* restore pipeline connections */
	list_for_each_entry(sroute, &sdev->route_list, list) {

		/* only set up routes belonging to static pipelines */
		if (!verify && (sroute->src_widget->dynamic_pipeline_widget ||
				sroute->sink_widget->dynamic_pipeline_widget))
			continue;

		ret = ipc_tplg_ops->route_setup(sdev, sroute);
		if (ret < 0) {
			dev_err(sdev->dev, "%s: restore pipeline connections failed\n", __func__);
			return ret;
		}
	}

	/* complete pipeline */
	list_for_each_entry(swidget, &sdev->widget_list, list) {
		switch (swidget->id) {
		case snd_soc_dapm_scheduler:
			/* only complete static pipelines */
			if (!verify && swidget->dynamic_pipeline_widget)
				continue;

			if (v->abi_version < SOF_ABI_VER(3, 19, 0)) {
				ret = sof_widget_setup(sdev, swidget);
				if (ret < 0)
					return ret;
			}

			if (ipc_tplg_ops->pipeline_complete) {
				swidget->complete = ipc_tplg_ops->pipeline_complete(sdev, swidget);
				if (swidget->complete < 0)
					return swidget->complete;
			}
			break;
		default:
			break;
		}
	}

	return 0;
}

int sof_pcm_stream_free(struct snd_sof_dev *sdev, struct snd_pcm_substream *substream,
			struct snd_sof_pcm *spcm, int dir, bool free_widget_list)
{
	int ret;

	/* Send PCM_FREE IPC to reset pipeline */
	ret = sof_pcm_dsp_pcm_free(substream, sdev, spcm);
	if (ret < 0)
		return ret;

	/* stop the DMA */
	ret = snd_sof_pcm_platform_hw_free(sdev, substream);
	if (ret < 0)
		return ret;

	/* free widget list */
	if (free_widget_list) {
		ret = sof_widget_list_free(sdev, spcm, dir);
		if (ret < 0)
			dev_err(sdev->dev, "failed to free widgets during suspend\n");
	}

	return ret;
}

/*
 * Free the PCM, its associated widgets and set the prepared flag to false for all PCMs that
 * did not get suspended(ex: paused streams) so the widgets can be set up again during resume.
 */
static int sof_tear_down_left_over_pipelines(struct snd_sof_dev *sdev)
{
	struct snd_sof_widget *swidget;
	struct snd_sof_pcm *spcm;
	int dir, ret;

	/*
	 * free all PCMs and their associated DAPM widgets if their connected DAPM widget
	 * list is not NULL. This should only be true for paused streams at this point.
	 * This is equivalent to the handling of FE DAI suspend trigger for running streams.
	 */
	list_for_each_entry(spcm, &sdev->pcm_list, list)
		for_each_pcm_streams(dir) {
			struct snd_pcm_substream *substream = spcm->stream[dir].substream;

			if (!substream || !substream->runtime)
				continue;

			if (spcm->stream[dir].list) {
				ret = sof_pcm_stream_free(sdev, substream, spcm, dir, true);
				if (ret < 0)
					return ret;
			}
		}

	/*
	 * free any left over DAI widgets. This is equivalent to the handling of suspend trigger
	 * for the BE DAI for running streams.
	 */
	list_for_each_entry(swidget, &sdev->widget_list, list)
		if (WIDGET_IS_DAI(swidget->id) && swidget->use_count == 1) {
			ret = sof_widget_free(sdev, swidget);
			if (ret < 0)
				return ret;
		}

	return 0;
}

/*
 * For older firmware, this function doesn't free widgets for static pipelines during suspend.
 * It only resets use_count for all widgets.
 */
int sof_tear_down_pipelines(struct snd_sof_dev *sdev, bool verify)
{
	struct sof_ipc_fw_version *v = &sdev->fw_ready.version;
	struct snd_sof_widget *swidget;
	struct snd_sof_route *sroute;
	int ret;

	/*
	 * This function is called during suspend and for one-time topology verification during
	 * first boot. In both cases, there is no need to protect swidget->use_count and
	 * sroute->setup because during suspend all running streams are suspended and during
	 * topology loading the sound card unavailable to open PCMs.
	 */
	list_for_each_entry(swidget, &sdev->widget_list, list) {
		if (swidget->dynamic_pipeline_widget)
			continue;

		/* Do not free widgets for static pipelines with FW ABI older than 3.19 */
		if (!verify && !swidget->dynamic_pipeline_widget &&
		    v->abi_version < SOF_ABI_VER(3, 19, 0)) {
			swidget->use_count = 0;
			swidget->complete = 0;
			continue;
		}

		ret = sof_widget_free(sdev, swidget);
		if (ret < 0)
			return ret;
	}

	/*
	 * Tear down all pipelines associated with PCMs that did not get suspended
	 * and unset the prepare flag so that they can be set up again during resume.
	 * Skip this step for older firmware.
	 */
	if (!verify && v->abi_version >= SOF_ABI_VER(3, 19, 0)) {
		ret = sof_tear_down_left_over_pipelines(sdev);
		if (ret < 0) {
			dev_err(sdev->dev, "failed to tear down paused pipelines\n");
			return ret;
		}
	}

	list_for_each_entry(sroute, &sdev->route_list, list)
		sroute->setup = false;

	return 0;
}

/*
 * Generic object lookup APIs.
 */

struct snd_sof_pcm *snd_sof_find_spcm_name(struct snd_soc_component *scomp,
					   const char *name)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	struct snd_sof_pcm *spcm;

	list_for_each_entry(spcm, &sdev->pcm_list, list) {
		/* match with PCM dai name */
		if (strcmp(spcm->pcm.dai_name, name) == 0)
			return spcm;

		/* match with playback caps name if set */
		if (*spcm->pcm.caps[0].name &&
		    !strcmp(spcm->pcm.caps[0].name, name))
			return spcm;

		/* match with capture caps name if set */
		if (*spcm->pcm.caps[1].name &&
		    !strcmp(spcm->pcm.caps[1].name, name))
			return spcm;
	}

	return NULL;
}

struct snd_sof_pcm *snd_sof_find_spcm_comp(struct snd_soc_component *scomp,
					   unsigned int comp_id,
					   int *direction)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	struct snd_sof_pcm *spcm;
	int dir;

	list_for_each_entry(spcm, &sdev->pcm_list, list) {
		for_each_pcm_streams(dir) {
			if (spcm->stream[dir].comp_id == comp_id) {
				*direction = dir;
				return spcm;
			}
		}
	}

	return NULL;
}

struct snd_sof_widget *snd_sof_find_swidget(struct snd_soc_component *scomp,
					    const char *name)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	struct snd_sof_widget *swidget;

	list_for_each_entry(swidget, &sdev->widget_list, list) {
		if (strcmp(name, swidget->widget->name) == 0)
			return swidget;
	}

	return NULL;
}

/* find widget by stream name and direction */
struct snd_sof_widget *
snd_sof_find_swidget_sname(struct snd_soc_component *scomp,
			   const char *pcm_name, int dir)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	struct snd_sof_widget *swidget;
	enum snd_soc_dapm_type type;

	if (dir == SNDRV_PCM_STREAM_PLAYBACK)
		type = snd_soc_dapm_aif_in;
	else
		type = snd_soc_dapm_aif_out;

	list_for_each_entry(swidget, &sdev->widget_list, list) {
		if (!strcmp(pcm_name, swidget->widget->sname) &&
		    swidget->id == type)
			return swidget;
	}

	return NULL;
}

struct snd_sof_dai *snd_sof_find_dai(struct snd_soc_component *scomp,
				     const char *name)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	struct snd_sof_dai *dai;

	list_for_each_entry(dai, &sdev->dai_list, list) {
		if (dai->name && (strcmp(name, dai->name) == 0))
			return dai;
	}

	return NULL;
}

#define SOF_DAI_CLK_INTEL_SSP_MCLK	0
#define SOF_DAI_CLK_INTEL_SSP_BCLK	1

static int sof_dai_get_clk(struct snd_soc_pcm_runtime *rtd, int clk_type)
{
	struct snd_soc_component *component =
		snd_soc_rtdcom_lookup(rtd, SOF_AUDIO_PCM_DRV_NAME);
	struct snd_sof_dai *dai =
		snd_sof_find_dai(component, (char *)rtd->dai_link->name);
	struct sof_dai_private_data *private = dai->private;

	/* use the tplg configured mclk if existed */
	if (!dai || !private || !private->dai_config)
		return 0;

	switch (private->dai_config->type) {
	case SOF_DAI_INTEL_SSP:
		switch (clk_type) {
		case SOF_DAI_CLK_INTEL_SSP_MCLK:
			return private->dai_config->ssp.mclk_rate;
		case SOF_DAI_CLK_INTEL_SSP_BCLK:
			return private->dai_config->ssp.bclk_rate;
		default:
			dev_err(rtd->dev, "fail to get SSP clk %d rate\n",
				clk_type);
			return -EINVAL;
		}
		break;
	default:
		/* not yet implemented for platforms other than the above */
		dev_err(rtd->dev, "DAI type %d not supported yet!\n",
			private->dai_config->type);
		return -EINVAL;
	}
}

/*
 * Helper to get SSP MCLK from a pcm_runtime.
 * Return 0 if not exist.
 */
int sof_dai_get_mclk(struct snd_soc_pcm_runtime *rtd)
{
	return sof_dai_get_clk(rtd, SOF_DAI_CLK_INTEL_SSP_MCLK);
}
EXPORT_SYMBOL(sof_dai_get_mclk);

/*
 * Helper to get SSP BCLK from a pcm_runtime.
 * Return 0 if not exist.
 */
int sof_dai_get_bclk(struct snd_soc_pcm_runtime *rtd)
{
	return sof_dai_get_clk(rtd, SOF_DAI_CLK_INTEL_SSP_BCLK);
}
EXPORT_SYMBOL(sof_dai_get_bclk);

/*
 * SOF Driver enumeration.
 */
int sof_machine_check(struct snd_sof_dev *sdev)
{
	struct snd_sof_pdata *sof_pdata = sdev->pdata;
	const struct sof_dev_desc *desc = sof_pdata->desc;
	struct snd_soc_acpi_mach *mach;

	if (!IS_ENABLED(CONFIG_SND_SOC_SOF_FORCE_NOCODEC_MODE)) {

		/* find machine */
		mach = snd_sof_machine_select(sdev);
		if (mach) {
			sof_pdata->machine = mach;
			snd_sof_set_mach_params(mach, sdev);
			return 0;
		}

		if (!IS_ENABLED(CONFIG_SND_SOC_SOF_NOCODEC)) {
			dev_err(sdev->dev, "error: no matching ASoC machine driver found - aborting probe\n");
			return -ENODEV;
		}
	} else {
		dev_warn(sdev->dev, "Force to use nocodec mode\n");
	}

	/* select nocodec mode */
	dev_warn(sdev->dev, "Using nocodec machine driver\n");
	mach = devm_kzalloc(sdev->dev, sizeof(*mach), GFP_KERNEL);
	if (!mach)
		return -ENOMEM;

	mach->drv_name = "sof-nocodec";
	sof_pdata->tplg_filename = desc->nocodec_tplg_filename;

	sof_pdata->machine = mach;
	snd_sof_set_mach_params(mach, sdev);

	return 0;
}
EXPORT_SYMBOL(sof_machine_check);

int sof_machine_register(struct snd_sof_dev *sdev, void *pdata)
{
	struct snd_sof_pdata *plat_data = pdata;
	const char *drv_name;
	const void *mach;
	int size;

	drv_name = plat_data->machine->drv_name;
	mach = plat_data->machine;
	size = sizeof(*plat_data->machine);

	/* register machine driver, pass machine info as pdata */
	plat_data->pdev_mach =
		platform_device_register_data(sdev->dev, drv_name,
					      PLATFORM_DEVID_NONE, mach, size);
	if (IS_ERR(plat_data->pdev_mach))
		return PTR_ERR(plat_data->pdev_mach);

	dev_dbg(sdev->dev, "created machine %s\n",
		dev_name(&plat_data->pdev_mach->dev));

	return 0;
}
EXPORT_SYMBOL(sof_machine_register);

void sof_machine_unregister(struct snd_sof_dev *sdev, void *pdata)
{
	struct snd_sof_pdata *plat_data = pdata;

	if (!IS_ERR_OR_NULL(plat_data->pdev_mach))
		platform_device_unregister(plat_data->pdev_mach);
}
EXPORT_SYMBOL(sof_machine_unregister);
