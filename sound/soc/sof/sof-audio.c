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
#include <trace/events/sof.h>
#include "sof-audio.h"
#include "sof-of-dev.h"
#include "ops.h"

static void sof_reset_route_setup_status(struct snd_sof_dev *sdev, struct snd_sof_widget *widget)
{
	const struct sof_ipc_tplg_ops *tplg_ops = sdev->ipc->ops->tplg;
	struct snd_sof_route *sroute;

	list_for_each_entry(sroute, &sdev->route_list, list)
		if (sroute->src_widget == widget || sroute->sink_widget == widget) {
			if (sroute->setup && tplg_ops->route_free)
				tplg_ops->route_free(sdev, sroute);

			sroute->setup = false;
		}
}

int sof_widget_free(struct snd_sof_dev *sdev, struct snd_sof_widget *swidget)
{
	const struct sof_ipc_tplg_ops *tplg_ops = sdev->ipc->ops->tplg;
	int err = 0;
	int ret;

	if (!swidget->private)
		return 0;

	trace_sof_widget_free(swidget);

	/* only free when use_count is 0 */
	if (--swidget->use_count)
		return 0;

	/* reset route setup status for all routes that contain this widget */
	sof_reset_route_setup_status(sdev, swidget);

	/* continue to disable core even if IPC fails */
	if (tplg_ops->widget_free)
		err = tplg_ops->widget_free(sdev, swidget);

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

	/*
	 * free the scheduler widget (same as pipe_widget) associated with the current swidget.
	 * skip for static pipelines
	 */
	if (swidget->dynamic_pipeline_widget && swidget->id != snd_soc_dapm_scheduler) {
		ret = sof_widget_free(sdev, swidget->pipe_widget);
		if (ret < 0 && !err)
			err = ret;
		swidget->pipe_widget->complete = 0;
	}

	if (!err)
		dev_dbg(sdev->dev, "widget %s freed\n", swidget->widget->name);

	return err;
}
EXPORT_SYMBOL(sof_widget_free);

int sof_widget_setup(struct snd_sof_dev *sdev, struct snd_sof_widget *swidget)
{
	const struct sof_ipc_tplg_ops *tplg_ops = sdev->ipc->ops->tplg;
	int ret;

	/* skip if there is no private data */
	if (!swidget->private)
		return 0;

	trace_sof_widget_setup(swidget);

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

	/* setup widget in the DSP */
	if (tplg_ops->widget_setup) {
		ret = tplg_ops->widget_setup(sdev, swidget);
		if (ret < 0)
			goto core_put;
	}

	/* send config for DAI components */
	if (WIDGET_IS_DAI(swidget->id)) {
		unsigned int flags = SOF_DAI_CONFIG_FLAGS_NONE;

		if (tplg_ops->dai_config) {
			ret = tplg_ops->dai_config(sdev, swidget, flags, NULL);
			if (ret < 0)
				goto widget_free;
		}
	}

	/* restore kcontrols for widget */
	if (tplg_ops->control->widget_kcontrol_setup) {
		ret = tplg_ops->control->widget_kcontrol_setup(sdev, swidget);
		if (ret < 0)
			goto widget_free;
	}

	dev_dbg(sdev->dev, "widget %s setup complete\n", swidget->widget->name);

	return 0;

widget_free:
	/* widget use_count and core ref_count will both be decremented by sof_widget_free() */
	sof_widget_free(sdev, swidget);
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

int sof_route_setup(struct snd_sof_dev *sdev, struct snd_soc_dapm_widget *wsource,
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

static void
sof_unprepare_widgets_in_path(struct snd_sof_dev *sdev, struct snd_soc_dapm_widget *widget)
{
	const struct sof_ipc_tplg_ops *ipc_tplg_ops = sdev->ipc->ops->tplg;
	const struct sof_ipc_tplg_widget_ops *widget_ops = ipc_tplg_ops->widget;
	struct snd_sof_widget *swidget = widget->dobj.private;
	struct snd_soc_dapm_path *p;

	/* skip if the widget is in use or if it is already unprepared */
	if (!swidget || !swidget->prepared || swidget->use_count > 0)
		goto sink_unprepare;

	if (widget_ops[widget->id].ipc_unprepare)
		/* unprepare the source widget */
		widget_ops[widget->id].ipc_unprepare(swidget);

	swidget->prepared = false;

sink_unprepare:
	/* unprepare all widgets in the sink paths */
	snd_soc_dapm_widget_for_each_sink_path(widget, p) {
		if (!p->walking && p->sink->dobj.private) {
			p->walking = true;
			sof_unprepare_widgets_in_path(sdev, p->sink);
			p->walking = false;
		}
	}
}

static int
sof_prepare_widgets_in_path(struct snd_sof_dev *sdev, struct snd_soc_dapm_widget *widget,
			    struct snd_pcm_hw_params *fe_params,
			    struct snd_sof_platform_stream_params *platform_params,
			    struct snd_pcm_hw_params *pipeline_params, int dir)
{
	const struct sof_ipc_tplg_ops *ipc_tplg_ops = sdev->ipc->ops->tplg;
	const struct sof_ipc_tplg_widget_ops *widget_ops = ipc_tplg_ops->widget;
	struct snd_sof_widget *swidget = widget->dobj.private;
	struct snd_soc_dapm_path *p;
	int ret;

	if (!swidget || !widget_ops[widget->id].ipc_prepare || swidget->prepared)
		goto sink_prepare;

	/* prepare the source widget */
	ret = widget_ops[widget->id].ipc_prepare(swidget, fe_params, platform_params,
					     pipeline_params, dir);
	if (ret < 0) {
		dev_err(sdev->dev, "failed to prepare widget %s\n", widget->name);
		return ret;
	}

	swidget->prepared = true;

sink_prepare:
	/* prepare all widgets in the sink paths */
	snd_soc_dapm_widget_for_each_sink_path(widget, p) {
		if (!p->walking && p->sink->dobj.private) {
			p->walking = true;
			ret = sof_prepare_widgets_in_path(sdev, p->sink,  fe_params,
							  platform_params, pipeline_params, dir);
			p->walking = false;
			if (ret < 0) {
				/* unprepare the source widget */
				if (widget_ops[widget->id].ipc_unprepare &&
				    swidget && swidget->prepared && swidget->use_count == 0) {
					widget_ops[widget->id].ipc_unprepare(swidget);
					swidget->prepared = false;
				}
				return ret;
			}
		}
	}

	return 0;
}

/*
 * free all widgets in the sink path starting from the source widget
 * (DAI type for capture, AIF type for playback)
 */
static int sof_free_widgets_in_path(struct snd_sof_dev *sdev, struct snd_soc_dapm_widget *widget,
				    int dir)
{
	struct snd_soc_dapm_path *p;
	int err;
	int ret = 0;

	/* free all widgets even in case of error to keep use counts balanced */
	snd_soc_dapm_widget_for_each_sink_path(widget, p) {
		if (!p->walking && p->sink->dobj.private && widget->dobj.private) {
			p->walking = true;
			if (WIDGET_IS_AIF_OR_DAI(widget->id)) {
				err = sof_widget_free(sdev, widget->dobj.private);
				if (err < 0)
					ret = err;
			}

			err = sof_widget_free(sdev, p->sink->dobj.private);
			if (err < 0)
				ret = err;

			err = sof_free_widgets_in_path(sdev, p->sink, dir);
			if (err < 0)
				ret = err;
			p->walking = false;
		}
	}

	return ret;
}

/*
 * set up all widgets in the sink path starting from the source widget
 * (DAI type for capture, AIF type for playback).
 * The error path in this function ensures that all successfully set up widgets getting freed.
 */
static int sof_set_up_widgets_in_path(struct snd_sof_dev *sdev, struct snd_soc_dapm_widget *widget,
				      int dir)
{
	struct snd_soc_dapm_path *p;
	int ret;

	snd_soc_dapm_widget_for_each_sink_path(widget, p) {
		if (!p->walking && p->sink->dobj.private && widget->dobj.private) {
			p->walking = true;
			if (WIDGET_IS_AIF_OR_DAI(widget->id)) {
				ret = sof_widget_setup(sdev, widget->dobj.private);
				if (ret < 0)
					goto out;
			}

			ret = sof_widget_setup(sdev, p->sink->dobj.private);
			if (ret < 0) {
				if (WIDGET_IS_AIF_OR_DAI(widget->id))
					sof_widget_free(sdev, widget->dobj.private);
				goto out;
			}

			ret = sof_set_up_widgets_in_path(sdev, p->sink, dir);
			if (ret < 0) {
				if (WIDGET_IS_AIF_OR_DAI(widget->id))
					sof_widget_free(sdev, widget->dobj.private);
				sof_widget_free(sdev, p->sink->dobj.private);
			}
out:
			p->walking = false;
			if (ret < 0)
				return ret;
		}
	}

	return 0;
}

static int
sof_walk_widgets_in_order(struct snd_sof_dev *sdev, struct snd_soc_dapm_widget_list *list,
			  struct snd_pcm_hw_params *fe_params,
			  struct snd_sof_platform_stream_params *platform_params, int dir,
			  enum sof_widget_op op)
{
	struct snd_soc_dapm_widget *widget;
	char *str;
	int ret = 0;
	int i;

	for_each_dapm_widgets(list, i, widget) {
		/* starting widget for playback is AIF type */
		if (dir == SNDRV_PCM_STREAM_PLAYBACK && widget->id != snd_soc_dapm_aif_in)
			continue;

		/* starting widget for capture is DAI type */
		if (dir == SNDRV_PCM_STREAM_CAPTURE && widget->id != snd_soc_dapm_dai_out)
			continue;

		switch (op) {
		case SOF_WIDGET_SETUP:
			ret = sof_set_up_widgets_in_path(sdev, widget, dir);
			str = "set up";
			break;
		case SOF_WIDGET_FREE:
			ret = sof_free_widgets_in_path(sdev, widget, dir);
			str = "free";
			break;
		case SOF_WIDGET_PREPARE:
		{
			struct snd_pcm_hw_params pipeline_params;

			str = "prepare";
			/*
			 * When walking the list of connected widgets, the pipeline_params for each
			 * widget is modified by the source widget in the path. Use a local
			 * copy of the runtime params as the pipeline_params so that the runtime
			 * params does not get overwritten.
			 */
			memcpy(&pipeline_params, fe_params, sizeof(*fe_params));

			ret = sof_prepare_widgets_in_path(sdev, widget, fe_params,
							  platform_params, &pipeline_params, dir);
			break;
		}
		case SOF_WIDGET_UNPREPARE:
			sof_unprepare_widgets_in_path(sdev, widget);
			break;
		default:
			dev_err(sdev->dev, "Invalid widget op %d\n", op);
			return -EINVAL;
		}
		if (ret < 0) {
			dev_err(sdev->dev, "Failed to %s connected widgets\n", str);
			return ret;
		}
	}

	return 0;
}

int sof_widget_list_setup(struct snd_sof_dev *sdev, struct snd_sof_pcm *spcm,
			  struct snd_pcm_hw_params *fe_params,
			  struct snd_sof_platform_stream_params *platform_params,
			  int dir)
{
	const struct sof_ipc_tplg_ops *ipc_tplg_ops = sdev->ipc->ops->tplg;
	struct snd_soc_dapm_widget_list *list = spcm->stream[dir].list;
	struct snd_soc_dapm_widget *widget;
	int i, ret;

	/* nothing to set up */
	if (!list)
		return 0;

	/*
	 * Prepare widgets for set up. The prepare step is used to allocate memory, assign
	 * instance ID and pick the widget configuration based on the runtime PCM params.
	 */
	ret = sof_walk_widgets_in_order(sdev, list, fe_params, platform_params,
					dir, SOF_WIDGET_PREPARE);
	if (ret < 0)
		return ret;

	/* Set up is used to send the IPC to the DSP to create the widget */
	ret = sof_walk_widgets_in_order(sdev, list, fe_params, platform_params,
					dir, SOF_WIDGET_SETUP);
	if (ret < 0) {
		ret = sof_walk_widgets_in_order(sdev, list, fe_params, platform_params,
						dir, SOF_WIDGET_UNPREPARE);
		return ret;
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
	sof_walk_widgets_in_order(sdev, list, fe_params, platform_params, dir,
				  SOF_WIDGET_FREE);
	sof_walk_widgets_in_order(sdev, list, NULL, NULL, dir, SOF_WIDGET_UNPREPARE);

	return ret;
}

int sof_widget_list_free(struct snd_sof_dev *sdev, struct snd_sof_pcm *spcm, int dir)
{
	struct snd_soc_dapm_widget_list *list = spcm->stream[dir].list;
	int ret;

	/* nothing to free */
	if (!list)
		return 0;

	/* send IPC to free widget in the DSP */
	ret = sof_walk_widgets_in_order(sdev, list, NULL, NULL, dir, SOF_WIDGET_FREE);

	/* unprepare the widget */
	sof_walk_widgets_in_order(sdev, list, NULL, NULL, dir, SOF_WIDGET_UNPREPARE);

	snd_soc_dapm_dai_free_widgets(&list);
	spcm->stream[dir].list = NULL;

	return ret;
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

int sof_pcm_stream_free(struct snd_sof_dev *sdev, struct snd_pcm_substream *substream,
			struct snd_sof_pcm *spcm, int dir, bool free_widget_list)
{
	const struct sof_ipc_pcm_ops *pcm_ops = sdev->ipc->ops->pcm;
	int ret;

	/* Send PCM_FREE IPC to reset pipeline */
	if (pcm_ops->hw_free && spcm->prepared[substream->stream]) {
		ret = pcm_ops->hw_free(sdev->component, substream);
		if (ret < 0)
			return ret;
	}

	spcm->prepared[substream->stream] = false;

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

static int sof_dai_get_clk(struct snd_soc_pcm_runtime *rtd, int clk_type)
{
	struct snd_soc_component *component =
		snd_soc_rtdcom_lookup(rtd, SOF_AUDIO_PCM_DRV_NAME);
	struct snd_sof_dai *dai =
		snd_sof_find_dai(component, (char *)rtd->dai_link->name);
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(component);
	const struct sof_ipc_tplg_ops *tplg_ops = sdev->ipc->ops->tplg;

	/* use the tplg configured mclk if existed */
	if (!dai)
		return 0;

	if (tplg_ops->dai_get_clk)
		return tplg_ops->dai_get_clk(sdev, dai, clk_type);

	return 0;
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

static struct snd_sof_of_mach *sof_of_machine_select(struct snd_sof_dev *sdev)
{
	struct snd_sof_pdata *sof_pdata = sdev->pdata;
	const struct sof_dev_desc *desc = sof_pdata->desc;
	struct snd_sof_of_mach *mach = desc->of_machines;

	if (!mach)
		return NULL;

	for (; mach->compatible; mach++) {
		if (of_machine_is_compatible(mach->compatible)) {
			sof_pdata->tplg_filename = mach->sof_tplg_filename;
			if (mach->fw_filename)
				sof_pdata->fw_filename = mach->fw_filename;

			return mach;
		}
	}

	return NULL;
}

/*
 * SOF Driver enumeration.
 */
int sof_machine_check(struct snd_sof_dev *sdev)
{
	struct snd_sof_pdata *sof_pdata = sdev->pdata;
	const struct sof_dev_desc *desc = sof_pdata->desc;
	struct snd_soc_acpi_mach *mach;

	if (!IS_ENABLED(CONFIG_SND_SOC_SOF_FORCE_NOCODEC_MODE)) {
		const struct snd_sof_of_mach *of_mach;

		/* find machine */
		mach = snd_sof_machine_select(sdev);
		if (mach) {
			sof_pdata->machine = mach;

			if (sof_pdata->subsystem_id_set) {
				mach->mach_params.subsystem_vendor = sof_pdata->subsystem_vendor;
				mach->mach_params.subsystem_device = sof_pdata->subsystem_device;
				mach->mach_params.subsystem_id_set = true;
			}

			snd_sof_set_mach_params(mach, sdev);
			return 0;
		}

		of_mach = sof_of_machine_select(sdev);
		if (of_mach) {
			sof_pdata->of_machine = of_mach;
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
	if (!sof_pdata->tplg_filename)
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
