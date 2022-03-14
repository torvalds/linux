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

struct sof_widget_data {
	int ctrl_type;
	int ipc_cmd;
	struct sof_abi_hdr *pdata;
	struct snd_sof_control *control;
};

struct sof_process_types {
	const char *name;
	enum sof_ipc_process_type type;
	enum sof_comp_type comp_type;
};

static const struct sof_process_types sof_process[] = {
	{"EQFIR", SOF_PROCESS_EQFIR, SOF_COMP_EQ_FIR},
	{"EQIIR", SOF_PROCESS_EQIIR, SOF_COMP_EQ_IIR},
	{"KEYWORD_DETECT", SOF_PROCESS_KEYWORD_DETECT, SOF_COMP_KEYWORD_DETECT},
	{"KPB", SOF_PROCESS_KPB, SOF_COMP_KPB},
	{"CHAN_SELECTOR", SOF_PROCESS_CHAN_SELECTOR, SOF_COMP_SELECTOR},
	{"MUX", SOF_PROCESS_MUX, SOF_COMP_MUX},
	{"DEMUX", SOF_PROCESS_DEMUX, SOF_COMP_DEMUX},
	{"DCBLOCK", SOF_PROCESS_DCBLOCK, SOF_COMP_DCBLOCK},
	{"SMART_AMP", SOF_PROCESS_SMART_AMP, SOF_COMP_SMART_AMP},
};

static enum sof_ipc_process_type find_process(const char *name)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(sof_process); i++) {
		if (strcmp(name, sof_process[i].name) == 0)
			return sof_process[i].type;
	}

	return SOF_PROCESS_NONE;
}

static int get_token_process_type(void *elem, void *object, u32 offset)
{
	u32 *val = (u32 *)((u8 *)object + offset);

	*val = find_process((const char *)elem);
	return 0;
}

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

/* SRC */
static const struct sof_topology_token src_tokens[] = {
	{SOF_TKN_SRC_RATE_IN, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc_comp_src, source_rate)},
	{SOF_TKN_SRC_RATE_OUT, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc_comp_src, sink_rate)},
};

/* ASRC */
static const struct sof_topology_token asrc_tokens[] = {
	{SOF_TKN_ASRC_RATE_IN, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc_comp_asrc, source_rate)},
	{SOF_TKN_ASRC_RATE_OUT, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc_comp_asrc, sink_rate)},
	{SOF_TKN_ASRC_ASYNCHRONOUS_MODE, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc_comp_asrc, asynchronous_mode)},
	{SOF_TKN_ASRC_OPERATION_MODE, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc_comp_asrc, operation_mode)},
};

/* EFFECT */
static const struct sof_topology_token process_tokens[] = {
	{SOF_TKN_PROCESS_TYPE, SND_SOC_TPLG_TUPLE_TYPE_STRING, get_token_process_type,
		offsetof(struct sof_ipc_comp_process, type)},
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
	[SOF_SRC_TOKENS] = {"SRC tokens", src_tokens, ARRAY_SIZE(src_tokens)},
	[SOF_ASRC_TOKENS] = {"ASRC tokens", asrc_tokens, ARRAY_SIZE(asrc_tokens)},
	[SOF_PROCESS_TOKENS] = {"Process tokens", process_tokens, ARRAY_SIZE(process_tokens)},
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

static int sof_ipc3_widget_setup_comp_tone(struct snd_sof_widget *swidget)
{
	struct snd_soc_component *scomp = swidget->scomp;
	struct sof_ipc_comp_tone *tone;
	size_t ipc_size = sizeof(*tone);
	int ret;

	tone = sof_comp_alloc(swidget, &ipc_size, swidget->pipeline_id);
	if (!tone)
		return -ENOMEM;

	swidget->private = tone;

	/* configure siggen IPC message */
	tone->comp.type = SOF_COMP_TONE;
	tone->config.hdr.size = sizeof(tone->config);

	/* parse one set of comp tokens */
	ret = sof_update_ipc_object(scomp, &tone->config, SOF_COMP_TOKENS, swidget->tuples,
				    swidget->num_tuples, sizeof(tone->config), 1);
	if (ret < 0) {
		kfree(swidget->private);
		swidget->private = NULL;
		return ret;
	}

	dev_dbg(scomp->dev, "tone %s: frequency %d amplitude %d\n",
		swidget->widget->name, tone->frequency, tone->amplitude);
	sof_dbg_comp_config(scomp, &tone->config);

	return 0;
}

static int sof_ipc3_widget_setup_comp_mixer(struct snd_sof_widget *swidget)
{
	struct snd_soc_component *scomp = swidget->scomp;
	struct sof_ipc_comp_mixer *mixer;
	size_t ipc_size = sizeof(*mixer);
	int ret;

	mixer = sof_comp_alloc(swidget, &ipc_size, swidget->pipeline_id);
	if (!mixer)
		return -ENOMEM;

	swidget->private = mixer;

	/* configure mixer IPC message */
	mixer->comp.type = SOF_COMP_MIXER;
	mixer->config.hdr.size = sizeof(mixer->config);

	/* parse one set of comp tokens */
	ret = sof_update_ipc_object(scomp, &mixer->config, SOF_COMP_TOKENS,
				    swidget->tuples, swidget->num_tuples,
				    sizeof(mixer->config), 1);
	if (ret < 0) {
		kfree(swidget->private);
		swidget->private = NULL;

		return ret;
	}

	dev_dbg(scomp->dev, "loaded mixer %s\n", swidget->widget->name);
	sof_dbg_comp_config(scomp, &mixer->config);

	return 0;
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

static int sof_ipc3_widget_setup_comp_src(struct snd_sof_widget *swidget)
{
	struct snd_soc_component *scomp = swidget->scomp;
	struct sof_ipc_comp_src *src;
	size_t ipc_size = sizeof(*src);
	int ret;

	src = sof_comp_alloc(swidget, &ipc_size, swidget->pipeline_id);
	if (!src)
		return -ENOMEM;

	swidget->private = src;

	/* configure src IPC message */
	src->comp.type = SOF_COMP_SRC;
	src->config.hdr.size = sizeof(src->config);

	/* parse one set of src tokens */
	ret = sof_update_ipc_object(scomp, src, SOF_SRC_TOKENS, swidget->tuples,
				    swidget->num_tuples, sizeof(*src), 1);
	if (ret < 0)
		goto err;

	/* parse one set of comp tokens */
	ret = sof_update_ipc_object(scomp, &src->config, SOF_COMP_TOKENS,
				    swidget->tuples, swidget->num_tuples, sizeof(src->config), 1);
	if (ret < 0)
		goto err;

	dev_dbg(scomp->dev, "src %s: source rate %d sink rate %d\n",
		swidget->widget->name, src->source_rate, src->sink_rate);
	sof_dbg_comp_config(scomp, &src->config);

	return 0;
err:
	kfree(swidget->private);
	swidget->private = NULL;

	return ret;
}

static int sof_ipc3_widget_setup_comp_asrc(struct snd_sof_widget *swidget)
{
	struct snd_soc_component *scomp = swidget->scomp;
	struct sof_ipc_comp_asrc *asrc;
	size_t ipc_size = sizeof(*asrc);
	int ret;

	asrc = sof_comp_alloc(swidget, &ipc_size, swidget->pipeline_id);
	if (!asrc)
		return -ENOMEM;

	swidget->private = asrc;

	/* configure ASRC IPC message */
	asrc->comp.type = SOF_COMP_ASRC;
	asrc->config.hdr.size = sizeof(asrc->config);

	/* parse one set of asrc tokens */
	ret = sof_update_ipc_object(scomp, asrc, SOF_ASRC_TOKENS, swidget->tuples,
				    swidget->num_tuples, sizeof(*asrc), 1);
	if (ret < 0)
		goto err;

	/* parse one set of comp tokens */
	ret = sof_update_ipc_object(scomp, &asrc->config, SOF_COMP_TOKENS,
				    swidget->tuples, swidget->num_tuples, sizeof(asrc->config), 1);
	if (ret < 0)
		goto err;

	dev_dbg(scomp->dev, "asrc %s: source rate %d sink rate %d asynch %d operation %d\n",
		swidget->widget->name, asrc->source_rate, asrc->sink_rate,
		asrc->asynchronous_mode, asrc->operation_mode);

	sof_dbg_comp_config(scomp, &asrc->config);

	return 0;
err:
	kfree(swidget->private);
	swidget->private = NULL;

	return ret;
}

/*
 * Mux topology
 */
static int sof_ipc3_widget_setup_comp_mux(struct snd_sof_widget *swidget)
{
	struct snd_soc_component *scomp = swidget->scomp;
	struct sof_ipc_comp_mux *mux;
	size_t ipc_size = sizeof(*mux);
	int ret;

	mux = sof_comp_alloc(swidget, &ipc_size, swidget->pipeline_id);
	if (!mux)
		return -ENOMEM;

	swidget->private = mux;

	/* configure mux IPC message */
	mux->comp.type = SOF_COMP_MUX;
	mux->config.hdr.size = sizeof(mux->config);

	/* parse one set of comp tokens */
	ret = sof_update_ipc_object(scomp, &mux->config, SOF_COMP_TOKENS,
				    swidget->tuples, swidget->num_tuples, sizeof(mux->config), 1);
	if (ret < 0) {
		kfree(swidget->private);
		swidget->private = NULL;
		return ret;
	}

	dev_dbg(scomp->dev, "loaded mux %s\n", swidget->widget->name);
	sof_dbg_comp_config(scomp, &mux->config);

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

static int sof_get_control_data(struct snd_soc_component *scomp,
				struct snd_soc_dapm_widget *widget,
				struct sof_widget_data *wdata, size_t *size)
{
	const struct snd_kcontrol_new *kc;
	struct soc_mixer_control *sm;
	struct soc_bytes_ext *sbe;
	struct soc_enum *se;
	int i;

	*size = 0;

	for (i = 0; i < widget->num_kcontrols; i++) {
		kc = &widget->kcontrol_news[i];

		switch (widget->dobj.widget.kcontrol_type[i]) {
		case SND_SOC_TPLG_TYPE_MIXER:
			sm = (struct soc_mixer_control *)kc->private_value;
			wdata[i].control = sm->dobj.private;
			break;
		case SND_SOC_TPLG_TYPE_BYTES:
			sbe = (struct soc_bytes_ext *)kc->private_value;
			wdata[i].control = sbe->dobj.private;
			break;
		case SND_SOC_TPLG_TYPE_ENUM:
			se = (struct soc_enum *)kc->private_value;
			wdata[i].control = se->dobj.private;
			break;
		default:
			dev_err(scomp->dev, "Unknown kcontrol type %u in widget %s\n",
				widget->dobj.widget.kcontrol_type[i], widget->name);
			return -EINVAL;
		}

		if (!wdata[i].control) {
			dev_err(scomp->dev, "No scontrol for widget %s\n", widget->name);
			return -EINVAL;
		}

		wdata[i].pdata = wdata[i].control->control_data->data;
		if (!wdata[i].pdata)
			return -EINVAL;

		/* make sure data is valid - data can be updated at runtime */
		if (widget->dobj.widget.kcontrol_type[i] == SND_SOC_TPLG_TYPE_BYTES &&
		    wdata[i].pdata->magic != SOF_ABI_MAGIC)
			return -EINVAL;

		*size += wdata[i].pdata->size;

		/* get data type */
		switch (wdata[i].control->control_data->cmd) {
		case SOF_CTRL_CMD_VOLUME:
		case SOF_CTRL_CMD_ENUM:
		case SOF_CTRL_CMD_SWITCH:
			wdata[i].ipc_cmd = SOF_IPC_COMP_SET_VALUE;
			wdata[i].ctrl_type = SOF_CTRL_TYPE_VALUE_CHAN_SET;
			break;
		case SOF_CTRL_CMD_BINARY:
			wdata[i].ipc_cmd = SOF_IPC_COMP_SET_DATA;
			wdata[i].ctrl_type = SOF_CTRL_TYPE_DATA_SET;
			break;
		default:
			break;
		}
	}

	return 0;
}

static int sof_process_load(struct snd_soc_component *scomp,
			    struct snd_sof_widget *swidget, int type)
{
	struct snd_soc_dapm_widget *widget = swidget->widget;
	struct sof_ipc_comp_process *process;
	struct sof_widget_data *wdata = NULL;
	size_t ipc_data_size = 0;
	size_t ipc_size;
	int offset = 0;
	int ret;
	int i;

	/* allocate struct for widget control data sizes and types */
	if (widget->num_kcontrols) {
		wdata = kcalloc(widget->num_kcontrols, sizeof(*wdata), GFP_KERNEL);
		if (!wdata)
			return -ENOMEM;

		/* get possible component controls and get size of all pdata */
		ret = sof_get_control_data(scomp, widget, wdata, &ipc_data_size);
		if (ret < 0)
			goto out;
	}

	ipc_size = sizeof(struct sof_ipc_comp_process) + ipc_data_size;

	/* we are exceeding max ipc size, config needs to be sent separately */
	if (ipc_size > SOF_IPC_MSG_MAX_SIZE) {
		ipc_size -= ipc_data_size;
		ipc_data_size = 0;
	}

	process = sof_comp_alloc(swidget, &ipc_size, swidget->pipeline_id);
	if (!process) {
		ret = -ENOMEM;
		goto out;
	}

	swidget->private = process;

	/* configure iir IPC message */
	process->comp.type = type;
	process->config.hdr.size = sizeof(process->config);

	/* parse one set of comp tokens */
	ret = sof_update_ipc_object(scomp, &process->config, SOF_COMP_TOKENS,
				    swidget->tuples, swidget->num_tuples,
				    sizeof(process->config), 1);
	if (ret < 0)
		goto err;

	dev_dbg(scomp->dev, "loaded process %s\n", swidget->widget->name);
	sof_dbg_comp_config(scomp, &process->config);

	/*
	 * found private data in control, so copy it.
	 * get possible component controls - get size of all pdata,
	 * then memcpy with headers
	 */
	if (ipc_data_size) {
		for (i = 0; i < widget->num_kcontrols; i++) {
			memcpy(&process->data[offset],
			       wdata[i].pdata->data,
			       wdata[i].pdata->size);
			offset += wdata[i].pdata->size;
		}
	}

	process->size = ipc_data_size;

	kfree(wdata);

	return 0;
err:
	kfree(swidget->private);
	swidget->private = NULL;
out:
	kfree(wdata);
	return ret;
}

static enum sof_comp_type find_process_comp_type(enum sof_ipc_process_type type)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(sof_process); i++) {
		if (sof_process[i].type == type)
			return sof_process[i].comp_type;
	}

	return SOF_COMP_NONE;
}

/*
 * Processing Component Topology - can be "effect", "codec", or general
 * "processing".
 */

static int sof_widget_update_ipc_comp_process(struct snd_sof_widget *swidget)
{
	struct snd_soc_component *scomp = swidget->scomp;
	struct sof_ipc_comp_process config;
	int ret;

	memset(&config, 0, sizeof(config));
	config.comp.core = swidget->core;

	/* parse one set of process tokens */
	ret = sof_update_ipc_object(scomp, &config, SOF_PROCESS_TOKENS, swidget->tuples,
				    swidget->num_tuples, sizeof(config), 1);
	if (ret < 0)
		return ret;

	/* now load process specific data and send IPC */
	return sof_process_load(scomp, swidget, find_process_comp_type(config.type));
}

static int sof_ipc3_route_setup(struct snd_sof_dev *sdev, struct snd_sof_route *sroute)
{
	struct sof_ipc_pipe_comp_connect connect;
	struct sof_ipc_reply reply;
	int ret;

	connect.hdr.size = sizeof(connect);
	connect.hdr.cmd = SOF_IPC_GLB_TPLG_MSG | SOF_IPC_TPLG_COMP_CONNECT;
	connect.source_id = sroute->src_widget->comp_id;
	connect.sink_id = sroute->sink_widget->comp_id;

	dev_dbg(sdev->dev, "setting up route %s -> %s\n",
		sroute->src_widget->widget->name,
		sroute->sink_widget->widget->name);

	/* send ipc */
	ret = sof_ipc_tx_message(sdev->ipc, connect.hdr.cmd, &connect, sizeof(connect),
				 &reply, sizeof(reply));
	if (ret < 0)
		dev_err(sdev->dev, "%s: route %s -> %s failed\n", __func__,
			sroute->src_widget->widget->name, sroute->sink_widget->widget->name);

	return ret;
}

/* token list for each topology object */
static enum sof_tokens host_token_list[] = {
	SOF_CORE_TOKENS,
	SOF_COMP_EXT_TOKENS,
	SOF_PCM_TOKENS,
	SOF_COMP_TOKENS,
};

static enum sof_tokens comp_generic_token_list[] = {
	SOF_CORE_TOKENS,
	SOF_COMP_EXT_TOKENS,
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

static enum sof_tokens asrc_token_list[] = {
	SOF_CORE_TOKENS,
	SOF_COMP_EXT_TOKENS,
	SOF_ASRC_TOKENS,
	SOF_COMP_TOKENS,
};

static enum sof_tokens src_token_list[] = {
	SOF_CORE_TOKENS,
	SOF_COMP_EXT_TOKENS,
	SOF_SRC_TOKENS,
	SOF_COMP_TOKENS
};

static enum sof_tokens pga_token_list[] = {
	SOF_CORE_TOKENS,
	SOF_COMP_EXT_TOKENS,
	SOF_VOLUME_TOKENS,
	SOF_COMP_TOKENS,
};

static enum sof_tokens process_token_list[] = {
	SOF_CORE_TOKENS,
	SOF_COMP_EXT_TOKENS,
	SOF_PROCESS_TOKENS,
	SOF_COMP_TOKENS,
};

static const struct sof_ipc_tplg_widget_ops tplg_ipc3_widget_ops[SND_SOC_DAPM_TYPE_COUNT] = {
	[snd_soc_dapm_aif_in] =  {sof_ipc3_widget_setup_comp_host, sof_ipc3_widget_free_comp,
				  host_token_list, ARRAY_SIZE(host_token_list), NULL},
	[snd_soc_dapm_aif_out] = {sof_ipc3_widget_setup_comp_host, sof_ipc3_widget_free_comp,
				  host_token_list, ARRAY_SIZE(host_token_list), NULL},
	[snd_soc_dapm_buffer] = {sof_ipc3_widget_setup_comp_buffer, sof_ipc3_widget_free_comp,
				 buffer_token_list, ARRAY_SIZE(buffer_token_list), NULL},
	[snd_soc_dapm_mixer] = {sof_ipc3_widget_setup_comp_mixer, sof_ipc3_widget_free_comp,
				comp_generic_token_list, ARRAY_SIZE(comp_generic_token_list),
				NULL},
	[snd_soc_dapm_src] = {sof_ipc3_widget_setup_comp_src, sof_ipc3_widget_free_comp,
			      src_token_list, ARRAY_SIZE(src_token_list), NULL},
	[snd_soc_dapm_asrc] = {sof_ipc3_widget_setup_comp_asrc, sof_ipc3_widget_free_comp,
			       asrc_token_list, ARRAY_SIZE(asrc_token_list), NULL},
	[snd_soc_dapm_siggen] = {sof_ipc3_widget_setup_comp_tone, sof_ipc3_widget_free_comp,
				 comp_generic_token_list, ARRAY_SIZE(comp_generic_token_list),
				 NULL},
	[snd_soc_dapm_scheduler] = {sof_ipc3_widget_setup_comp_pipeline, sof_ipc3_widget_free_comp,
				    pipeline_token_list, ARRAY_SIZE(pipeline_token_list), NULL},
	[snd_soc_dapm_pga] = {sof_ipc3_widget_setup_comp_pga, sof_ipc3_widget_free_comp,
			      pga_token_list, ARRAY_SIZE(pga_token_list), NULL},
	[snd_soc_dapm_mux] = {sof_ipc3_widget_setup_comp_mux, sof_ipc3_widget_free_comp,
			      comp_generic_token_list, ARRAY_SIZE(comp_generic_token_list), NULL},
	[snd_soc_dapm_demux] = {sof_ipc3_widget_setup_comp_mux, sof_ipc3_widget_free_comp,
				 comp_generic_token_list, ARRAY_SIZE(comp_generic_token_list),
				 NULL},
	[snd_soc_dapm_effect] = {sof_widget_update_ipc_comp_process, sof_ipc3_widget_free_comp,
				 process_token_list, ARRAY_SIZE(process_token_list), NULL},
};

static const struct sof_ipc_tplg_ops ipc3_tplg_ops = {
	.widget = tplg_ipc3_widget_ops,
	.route_setup = sof_ipc3_route_setup,
	.token_list = ipc3_token_list,
};

const struct sof_ipc_ops ipc3_ops = {
	.tplg = &ipc3_tplg_ops,
};
