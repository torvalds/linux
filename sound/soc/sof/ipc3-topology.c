// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2021 Intel Corporation. All rights reserved.
//
//

#include <uapi/sound/sof/tokens.h>
#include <sound/pcm_params.h>
#include "sof-priv.h"
#include "sof-audio.h"
#include "ops.h"

/* Buffers */
static const struct sof_topology_token buffer_tokens[] = {
	{SOF_TKN_BUF_SIZE, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc_buffer, size)},
	{SOF_TKN_BUF_CAPS, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc_buffer, caps)},
};

/* scheduling */
static const struct sof_topology_token sched_tokens[] = {
	{SOF_TKN_SCHED_PERIOD, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc_pipe_new, period)},
	{SOF_TKN_SCHED_PRIORITY, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc_pipe_new, priority)},
	{SOF_TKN_SCHED_MIPS, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc_pipe_new, period_mips)},
	{SOF_TKN_SCHED_CORE, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc_pipe_new, core)},
	{SOF_TKN_SCHED_FRAMES, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc_pipe_new, frames_per_sched)},
	{SOF_TKN_SCHED_TIME_DOMAIN, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc_pipe_new, time_domain)},
};

static const struct sof_topology_token pipeline_tokens[] = {
	{SOF_TKN_SCHED_DYNAMIC_PIPELINE, SND_SOC_TPLG_TUPLE_TYPE_BOOL, get_token_u16,
		offsetof(struct snd_sof_widget, dynamic_pipeline_widget)},

};

/* volume */
static const struct sof_topology_token volume_tokens[] = {
	{SOF_TKN_VOLUME_RAMP_STEP_TYPE, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc_comp_volume, ramp)},
	{SOF_TKN_VOLUME_RAMP_STEP_MS, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc_comp_volume, initial_ramp)},
};

/* PCM */
static const struct sof_topology_token pcm_tokens[] = {
	{SOF_TKN_PCM_DMAC_CONFIG, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc_comp_host, dmac_config)},
};

/* Generic components */
static const struct sof_topology_token comp_tokens[] = {
	{SOF_TKN_COMP_PERIOD_SINK_COUNT, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc_comp_config, periods_sink)},
	{SOF_TKN_COMP_PERIOD_SOURCE_COUNT, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc_comp_config, periods_source)},
	{SOF_TKN_COMP_FORMAT,
		SND_SOC_TPLG_TUPLE_TYPE_STRING, get_token_comp_format,
		offsetof(struct sof_ipc_comp_config, frame_fmt)},
};

/* Core tokens */
static const struct sof_topology_token core_tokens[] = {
	{SOF_TKN_COMP_CORE_ID, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc_comp, core)},
};

/* Component extended tokens */
static const struct sof_topology_token comp_ext_tokens[] = {
	{SOF_TKN_COMP_UUID, SND_SOC_TPLG_TUPLE_TYPE_UUID, get_token_uuid,
		offsetof(struct snd_sof_widget, uuid)},
};

static const struct sof_token_info ipc3_token_list[SOF_TOKEN_COUNT] = {
	[SOF_PCM_TOKENS] = {"PCM tokens", pcm_tokens, ARRAY_SIZE(pcm_tokens)},
	[SOF_PIPELINE_TOKENS] = {"Pipeline tokens", pipeline_tokens, ARRAY_SIZE(pipeline_tokens)},
	[SOF_SCHED_TOKENS] = {"Scheduler tokens", sched_tokens, ARRAY_SIZE(sched_tokens)},
	[SOF_COMP_TOKENS] = {"Comp tokens", comp_tokens, ARRAY_SIZE(comp_tokens)},
	[SOF_CORE_TOKENS] = {"Core tokens", core_tokens, ARRAY_SIZE(core_tokens)},
	[SOF_COMP_EXT_TOKENS] = {"AFE tokens", comp_ext_tokens, ARRAY_SIZE(comp_ext_tokens)},
	[SOF_BUFFER_TOKENS] = {"Buffer tokens", buffer_tokens, ARRAY_SIZE(buffer_tokens)},
	[SOF_VOLUME_TOKENS] = {"Volume tokens", volume_tokens, ARRAY_SIZE(volume_tokens)},
};

/**
 * sof_comp_alloc - allocate and initialize buffer for a new component
 * @swidget: pointer to struct snd_sof_widget containing extended data
 * @ipc_size: IPC payload size that will be updated depending on valid
 *  extended data.
 * @index: ID of the pipeline the component belongs to
 *
 * Return: The pointer to the new allocated component, NULL if failed.
 */
static void *sof_comp_alloc(struct snd_sof_widget *swidget, size_t *ipc_size,
			    int index)
{
	struct sof_ipc_comp *comp;
	size_t total_size = *ipc_size;
	size_t ext_size = sizeof(swidget->uuid);

	/* only non-zero UUID is valid */
	if (!guid_is_null(&swidget->uuid))
		total_size += ext_size;

	comp = kzalloc(total_size, GFP_KERNEL);
	if (!comp)
		return NULL;

	/* configure comp new IPC message */
	comp->hdr.size = total_size;
	comp->hdr.cmd = SOF_IPC_GLB_TPLG_MSG | SOF_IPC_TPLG_COMP_NEW;
	comp->id = swidget->comp_id;
	comp->pipeline_id = index;
	comp->core = swidget->core;

	/* handle the extended data if needed */
	if (total_size > *ipc_size) {
		/* append extended data to the end of the component */
		memcpy((u8 *)comp + *ipc_size, &swidget->uuid, ext_size);
		comp->ext_data_length = ext_size;
	}

	/* update ipc_size and return */
	*ipc_size = total_size;
	return comp;
}

static void sof_dbg_comp_config(struct snd_soc_component *scomp, struct sof_ipc_comp_config *config)
{
	dev_dbg(scomp->dev, " config: periods snk %d src %d fmt %d\n",
		config->periods_sink, config->periods_source,
		config->frame_fmt);
}

static int sof_ipc3_widget_setup_comp_host(struct snd_sof_widget *swidget)
{
	struct snd_soc_component *scomp = swidget->scomp;
	struct sof_ipc_comp_host *host;
	size_t ipc_size = sizeof(*host);
	int ret;

	host = sof_comp_alloc(swidget, &ipc_size, swidget->pipeline_id);
	if (!host)
		return -ENOMEM;
	swidget->private = host;

	/* configure host comp IPC message */
	host->comp.type = SOF_COMP_HOST;
	host->config.hdr.size = sizeof(host->config);

	if (swidget->id == snd_soc_dapm_aif_out)
		host->direction = SOF_IPC_STREAM_CAPTURE;
	else
		host->direction = SOF_IPC_STREAM_PLAYBACK;

	/* parse one set of pcm_tokens */
	ret = sof_update_ipc_object(scomp, host, SOF_PCM_TOKENS, swidget->tuples,
				    swidget->num_tuples, sizeof(*host), 1);
	if (ret < 0)
		goto err;

	/* parse one set of comp_tokens */
	ret = sof_update_ipc_object(scomp, &host->config, SOF_COMP_TOKENS, swidget->tuples,
				    swidget->num_tuples, sizeof(host->config), 1);
	if (ret < 0)
		goto err;

	dev_dbg(scomp->dev, "loaded host %s\n", swidget->widget->name);
	sof_dbg_comp_config(scomp, &host->config);

	return 0;
err:
	kfree(swidget->private);
	swidget->private = NULL;

	return ret;
}

static void sof_ipc3_widget_free_comp(struct snd_sof_widget *swidget)
{
	kfree(swidget->private);
}

static int sof_ipc3_widget_setup_comp_pipeline(struct snd_sof_widget *swidget)
{
	struct snd_soc_component *scomp = swidget->scomp;
	struct sof_ipc_pipe_new *pipeline;
	struct snd_sof_widget *comp_swidget;
	int ret;

	pipeline = kzalloc(sizeof(*pipeline), GFP_KERNEL);
	if (!pipeline)
		return -ENOMEM;

	/* configure pipeline IPC message */
	pipeline->hdr.size = sizeof(*pipeline);
	pipeline->hdr.cmd = SOF_IPC_GLB_TPLG_MSG | SOF_IPC_TPLG_PIPE_NEW;
	pipeline->pipeline_id = swidget->pipeline_id;
	pipeline->comp_id = swidget->comp_id;

	swidget->private = pipeline;

	/* component at start of pipeline is our stream id */
	comp_swidget = snd_sof_find_swidget(scomp, swidget->widget->sname);
	if (!comp_swidget) {
		dev_err(scomp->dev, "scheduler %s refers to non existent widget %s\n",
			swidget->widget->name, swidget->widget->sname);
		ret = -EINVAL;
		goto err;
	}

	pipeline->sched_id = comp_swidget->comp_id;

	/* parse one set of scheduler tokens */
	ret = sof_update_ipc_object(scomp, pipeline, SOF_SCHED_TOKENS, swidget->tuples,
				    swidget->num_tuples, sizeof(*pipeline), 1);
	if (ret < 0)
		goto err;

	/* parse one set of pipeline tokens */
	ret = sof_update_ipc_object(scomp, swidget, SOF_PIPELINE_TOKENS, swidget->tuples,
				    swidget->num_tuples, sizeof(*swidget), 1);
	if (ret < 0)
		goto err;

	if (sof_debug_check_flag(SOF_DBG_DISABLE_MULTICORE))
		pipeline->core = SOF_DSP_PRIMARY_CORE;

	if (sof_debug_check_flag(SOF_DBG_DYNAMIC_PIPELINES_OVERRIDE))
		swidget->dynamic_pipeline_widget =
			sof_debug_check_flag(SOF_DBG_DYNAMIC_PIPELINES_ENABLE);

	dev_dbg(scomp->dev, "pipeline %s: period %d pri %d mips %d core %d frames %d dynamic %d\n",
		swidget->widget->name, pipeline->period, pipeline->priority,
		pipeline->period_mips, pipeline->core, pipeline->frames_per_sched,
		swidget->dynamic_pipeline_widget);

	swidget->core = pipeline->core;

	return 0;

err:
	kfree(swidget->private);
	swidget->private = NULL;

	return ret;
}

static int sof_ipc3_widget_setup_comp_buffer(struct snd_sof_widget *swidget)
{
	struct snd_soc_component *scomp = swidget->scomp;
	struct sof_ipc_buffer *buffer;
	int ret;

	buffer = kzalloc(sizeof(*buffer), GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	swidget->private = buffer;

	/* configure dai IPC message */
	buffer->comp.hdr.size = sizeof(*buffer);
	buffer->comp.hdr.cmd = SOF_IPC_GLB_TPLG_MSG | SOF_IPC_TPLG_BUFFER_NEW;
	buffer->comp.id = swidget->comp_id;
	buffer->comp.type = SOF_COMP_BUFFER;
	buffer->comp.pipeline_id = swidget->pipeline_id;
	buffer->comp.core = swidget->core;

	/* parse one set of buffer tokens */
	ret = sof_update_ipc_object(scomp, buffer, SOF_BUFFER_TOKENS, swidget->tuples,
				    swidget->num_tuples, sizeof(*buffer), 1);
	if (ret < 0) {
		kfree(swidget->private);
		swidget->private = NULL;
		return ret;
	}

	dev_dbg(scomp->dev, "buffer %s: size %d caps 0x%x\n",
		swidget->widget->name, buffer->size, buffer->caps);

	return 0;
}

/*
 * PGA Topology
 */

static int sof_ipc3_widget_setup_comp_pga(struct snd_sof_widget *swidget)
{
	struct snd_soc_component *scomp = swidget->scomp;
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	struct sof_ipc_comp_volume *volume;
	struct snd_sof_control *scontrol;
	size_t ipc_size = sizeof(*volume);
	int min_step, max_step;
	int ret;

	volume = sof_comp_alloc(swidget, &ipc_size, swidget->pipeline_id);
	if (!volume)
		return -ENOMEM;

	swidget->private = volume;

	/* configure volume IPC message */
	volume->comp.type = SOF_COMP_VOLUME;
	volume->config.hdr.size = sizeof(volume->config);

	/* parse one set of volume tokens */
	ret = sof_update_ipc_object(scomp, volume, SOF_VOLUME_TOKENS, swidget->tuples,
				    swidget->num_tuples, sizeof(*volume), 1);
	if (ret < 0)
		goto err;

	/* parse one set of comp tokens */
	ret = sof_update_ipc_object(scomp, &volume->config, SOF_COMP_TOKENS,
				    swidget->tuples, swidget->num_tuples,
				    sizeof(volume->config), 1);
	if (ret < 0)
		goto err;

	dev_dbg(scomp->dev, "loaded PGA %s\n", swidget->widget->name);
	sof_dbg_comp_config(scomp, &volume->config);

	list_for_each_entry(scontrol, &sdev->kcontrol_list, list) {
		if (scontrol->comp_id == swidget->comp_id &&
		    scontrol->volume_table) {
			min_step = scontrol->min_volume_step;
			max_step = scontrol->max_volume_step;
			volume->min_value = scontrol->volume_table[min_step];
			volume->max_value = scontrol->volume_table[max_step];
			volume->channels = scontrol->num_channels;
			break;
		}
	}

	return 0;
err:
	kfree(swidget->private);
	swidget->private = NULL;

	return ret;
}

/* token list for each topology object */
static enum sof_tokens host_token_list[] = {
	SOF_CORE_TOKENS,
	SOF_COMP_EXT_TOKENS,
	SOF_PCM_TOKENS,
	SOF_COMP_TOKENS,
};

static enum sof_tokens buffer_token_list[] = {
	SOF_BUFFER_TOKENS,
};

static enum sof_tokens pipeline_token_list[] = {
	SOF_CORE_TOKENS,
	SOF_COMP_EXT_TOKENS,
	SOF_PIPELINE_TOKENS,
	SOF_SCHED_TOKENS,
};

static enum sof_tokens pga_token_list[] = {
	SOF_CORE_TOKENS,
	SOF_COMP_EXT_TOKENS,
	SOF_VOLUME_TOKENS,
	SOF_COMP_TOKENS,
};

static const struct sof_ipc_tplg_widget_ops tplg_ipc3_widget_ops[SND_SOC_DAPM_TYPE_COUNT] = {
	[snd_soc_dapm_aif_in] =  {sof_ipc3_widget_setup_comp_host, sof_ipc3_widget_free_comp,
				  host_token_list, ARRAY_SIZE(host_token_list), NULL},
	[snd_soc_dapm_aif_out] = {sof_ipc3_widget_setup_comp_host, sof_ipc3_widget_free_comp,
				  host_token_list, ARRAY_SIZE(host_token_list), NULL},
	[snd_soc_dapm_buffer] = {sof_ipc3_widget_setup_comp_buffer, sof_ipc3_widget_free_comp,
				 buffer_token_list, ARRAY_SIZE(buffer_token_list), NULL},
	[snd_soc_dapm_scheduler] = {sof_ipc3_widget_setup_comp_pipeline, sof_ipc3_widget_free_comp,
				    pipeline_token_list, ARRAY_SIZE(pipeline_token_list), NULL},
	[snd_soc_dapm_pga] = {sof_ipc3_widget_setup_comp_pga, sof_ipc3_widget_free_comp,
			      pga_token_list, ARRAY_SIZE(pga_token_list), NULL},
};

static const struct sof_ipc_tplg_ops ipc3_tplg_ops = {
	.widget = tplg_ipc3_widget_ops,
	.token_list = ipc3_token_list,
};

const struct sof_ipc_ops ipc3_ops = {
	.tplg = &ipc3_tplg_ops,
};
