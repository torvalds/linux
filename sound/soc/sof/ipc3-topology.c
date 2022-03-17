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

/* Full volume for default values */
#define VOL_ZERO_DB	BIT(VOLUME_FWL)

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

/* DAI */
static const struct sof_topology_token dai_tokens[] = {
	{SOF_TKN_DAI_TYPE, SND_SOC_TPLG_TUPLE_TYPE_STRING, get_token_dai_type,
		offsetof(struct sof_ipc_comp_dai, type)},
	{SOF_TKN_DAI_INDEX, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc_comp_dai, dai_index)},
	{SOF_TKN_DAI_DIRECTION, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc_comp_dai, direction)},
};

/* BE DAI link */
static const struct sof_topology_token dai_link_tokens[] = {
	{SOF_TKN_DAI_TYPE, SND_SOC_TPLG_TUPLE_TYPE_STRING, get_token_dai_type,
		offsetof(struct sof_ipc_dai_config, type)},
	{SOF_TKN_DAI_INDEX, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc_dai_config, dai_index)},
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

/* SSP */
static const struct sof_topology_token ssp_tokens[] = {
	{SOF_TKN_INTEL_SSP_CLKS_CONTROL, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc_dai_ssp_params, clks_control)},
	{SOF_TKN_INTEL_SSP_MCLK_ID, SND_SOC_TPLG_TUPLE_TYPE_SHORT, get_token_u16,
		offsetof(struct sof_ipc_dai_ssp_params, mclk_id)},
	{SOF_TKN_INTEL_SSP_SAMPLE_BITS, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc_dai_ssp_params, sample_valid_bits)},
	{SOF_TKN_INTEL_SSP_FRAME_PULSE_WIDTH, SND_SOC_TPLG_TUPLE_TYPE_SHORT,	get_token_u16,
		offsetof(struct sof_ipc_dai_ssp_params, frame_pulse_width)},
	{SOF_TKN_INTEL_SSP_QUIRKS, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc_dai_ssp_params, quirks)},
	{SOF_TKN_INTEL_SSP_TDM_PADDING_PER_SLOT, SND_SOC_TPLG_TUPLE_TYPE_BOOL, get_token_u16,
		offsetof(struct sof_ipc_dai_ssp_params, tdm_per_slot_padding_flag)},
	{SOF_TKN_INTEL_SSP_BCLK_DELAY, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc_dai_ssp_params, bclk_delay)},
};

/* ALH */
static const struct sof_topology_token alh_tokens[] = {
	{SOF_TKN_INTEL_ALH_RATE, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc_dai_alh_params, rate)},
	{SOF_TKN_INTEL_ALH_CH,	SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc_dai_alh_params, channels)},
};

/* DMIC */
static const struct sof_topology_token dmic_tokens[] = {
	{SOF_TKN_INTEL_DMIC_DRIVER_VERSION, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc_dai_dmic_params, driver_ipc_version)},
	{SOF_TKN_INTEL_DMIC_CLK_MIN, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc_dai_dmic_params, pdmclk_min)},
	{SOF_TKN_INTEL_DMIC_CLK_MAX, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc_dai_dmic_params, pdmclk_max)},
	{SOF_TKN_INTEL_DMIC_SAMPLE_RATE, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc_dai_dmic_params, fifo_fs)},
	{SOF_TKN_INTEL_DMIC_DUTY_MIN, SND_SOC_TPLG_TUPLE_TYPE_SHORT, get_token_u16,
		offsetof(struct sof_ipc_dai_dmic_params, duty_min)},
	{SOF_TKN_INTEL_DMIC_DUTY_MAX, SND_SOC_TPLG_TUPLE_TYPE_SHORT, get_token_u16,
		offsetof(struct sof_ipc_dai_dmic_params, duty_max)},
	{SOF_TKN_INTEL_DMIC_NUM_PDM_ACTIVE, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc_dai_dmic_params, num_pdm_active)},
	{SOF_TKN_INTEL_DMIC_FIFO_WORD_LENGTH, SND_SOC_TPLG_TUPLE_TYPE_SHORT, get_token_u16,
		offsetof(struct sof_ipc_dai_dmic_params, fifo_bits)},
	{SOF_TKN_INTEL_DMIC_UNMUTE_RAMP_TIME_MS, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc_dai_dmic_params, unmute_ramp_time)},
};

/* ESAI */
static const struct sof_topology_token esai_tokens[] = {
	{SOF_TKN_IMX_ESAI_MCLK_ID, SND_SOC_TPLG_TUPLE_TYPE_SHORT, get_token_u16,
		offsetof(struct sof_ipc_dai_esai_params, mclk_id)},
};

/* SAI */
static const struct sof_topology_token sai_tokens[] = {
	{SOF_TKN_IMX_SAI_MCLK_ID, SND_SOC_TPLG_TUPLE_TYPE_SHORT, get_token_u16,
		offsetof(struct sof_ipc_dai_sai_params, mclk_id)},
};

/*
 * DMIC PDM Tokens
 * SOF_TKN_INTEL_DMIC_PDM_CTRL_ID should be the first token
 * as it increments the index while parsing the array of pdm tokens
 * and determines the correct offset
 */
static const struct sof_topology_token dmic_pdm_tokens[] = {
	{SOF_TKN_INTEL_DMIC_PDM_CTRL_ID, SND_SOC_TPLG_TUPLE_TYPE_SHORT, get_token_u16,
		offsetof(struct sof_ipc_dai_dmic_pdm_ctrl, id)},
	{SOF_TKN_INTEL_DMIC_PDM_MIC_A_Enable, SND_SOC_TPLG_TUPLE_TYPE_SHORT, get_token_u16,
		offsetof(struct sof_ipc_dai_dmic_pdm_ctrl, enable_mic_a)},
	{SOF_TKN_INTEL_DMIC_PDM_MIC_B_Enable, SND_SOC_TPLG_TUPLE_TYPE_SHORT, get_token_u16,
		offsetof(struct sof_ipc_dai_dmic_pdm_ctrl, enable_mic_b)},
	{SOF_TKN_INTEL_DMIC_PDM_POLARITY_A, SND_SOC_TPLG_TUPLE_TYPE_SHORT, get_token_u16,
		offsetof(struct sof_ipc_dai_dmic_pdm_ctrl, polarity_mic_a)},
	{SOF_TKN_INTEL_DMIC_PDM_POLARITY_B, SND_SOC_TPLG_TUPLE_TYPE_SHORT, get_token_u16,
		offsetof(struct sof_ipc_dai_dmic_pdm_ctrl, polarity_mic_b)},
	{SOF_TKN_INTEL_DMIC_PDM_CLK_EDGE, SND_SOC_TPLG_TUPLE_TYPE_SHORT, get_token_u16,
		offsetof(struct sof_ipc_dai_dmic_pdm_ctrl, clk_edge)},
	{SOF_TKN_INTEL_DMIC_PDM_SKEW, SND_SOC_TPLG_TUPLE_TYPE_SHORT, get_token_u16,
		offsetof(struct sof_ipc_dai_dmic_pdm_ctrl, skew)},
};

/* HDA */
static const struct sof_topology_token hda_tokens[] = {
	{SOF_TKN_INTEL_HDA_RATE, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc_dai_hda_params, rate)},
	{SOF_TKN_INTEL_HDA_CH, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc_dai_hda_params, channels)},
};

/* AFE */
static const struct sof_topology_token afe_tokens[] = {
	{SOF_TKN_MEDIATEK_AFE_RATE, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc_dai_mtk_afe_params, rate)},
	{SOF_TKN_MEDIATEK_AFE_CH, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc_dai_mtk_afe_params, channels)},
	{SOF_TKN_MEDIATEK_AFE_FORMAT, SND_SOC_TPLG_TUPLE_TYPE_STRING, get_token_comp_format,
		offsetof(struct sof_ipc_dai_mtk_afe_params, format)},
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
	[SOF_DAI_TOKENS] = {"DAI tokens", dai_tokens, ARRAY_SIZE(dai_tokens)},
	[SOF_DAI_LINK_TOKENS] = {"DAI link tokens", dai_link_tokens, ARRAY_SIZE(dai_link_tokens)},
	[SOF_HDA_TOKENS] = {"HDA tokens", hda_tokens, ARRAY_SIZE(hda_tokens)},
	[SOF_SSP_TOKENS] = {"SSP tokens", ssp_tokens, ARRAY_SIZE(ssp_tokens)},
	[SOF_ALH_TOKENS] = {"ALH tokens", alh_tokens, ARRAY_SIZE(alh_tokens)},
	[SOF_DMIC_TOKENS] = {"DMIC tokens", dmic_tokens, ARRAY_SIZE(dmic_tokens)},
	[SOF_DMIC_PDM_TOKENS] = {"DMIC PDM tokens", dmic_pdm_tokens, ARRAY_SIZE(dmic_pdm_tokens)},
	[SOF_ESAI_TOKENS] = {"ESAI tokens", esai_tokens, ARRAY_SIZE(esai_tokens)},
	[SOF_SAI_TOKENS] = {"SAI tokens", sai_tokens, ARRAY_SIZE(sai_tokens)},
	[SOF_AFE_TOKENS] = {"AFE tokens", afe_tokens, ARRAY_SIZE(afe_tokens)},
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
	struct sof_ipc_ctrl_data *cdata;
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

		cdata = wdata[i].control->ipc_control_data;
		wdata[i].pdata = cdata->data;
		if (!wdata[i].pdata)
			return -EINVAL;

		/* make sure data is valid - data can be updated at runtime */
		if (widget->dobj.widget.kcontrol_type[i] == SND_SOC_TPLG_TYPE_BYTES &&
		    wdata[i].pdata->magic != SOF_ABI_MAGIC)
			return -EINVAL;

		*size += wdata[i].pdata->size;

		/* get data type */
		switch (cdata->cmd) {
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

static int sof_link_hda_load(struct snd_soc_component *scomp, struct snd_sof_dai_link *slink,
			     struct sof_ipc_dai_config *config, struct snd_sof_dai *dai)
{
	struct sof_dai_private_data *private = dai->private;
	u32 size = sizeof(*config);
	int ret;

	/* init IPC */
	memset(&config->hda, 0, sizeof(config->hda));
	config->hdr.size = size;

	/* parse one set of HDA tokens */
	ret = sof_update_ipc_object(scomp, &config->hda, SOF_HDA_TOKENS, slink->tuples,
				    slink->num_tuples, size, 1);
	if (ret < 0)
		return ret;

	dev_dbg(scomp->dev, "HDA config rate %d channels %d\n",
		config->hda.rate, config->hda.channels);

	config->hda.link_dma_ch = DMA_CHAN_INVALID;

	dai->number_configs = 1;
	dai->current_config = 0;
	private->dai_config = kmemdup(config, size, GFP_KERNEL);
	if (!private->dai_config)
		return -ENOMEM;

	return 0;
}

static void sof_dai_set_format(struct snd_soc_tplg_hw_config *hw_config,
			       struct sof_ipc_dai_config *config)
{
	/* clock directions wrt codec */
	config->format &= ~SOF_DAI_FMT_CLOCK_PROVIDER_MASK;
	if (hw_config->bclk_provider == SND_SOC_TPLG_BCLK_CP) {
		/* codec is bclk provider */
		if (hw_config->fsync_provider == SND_SOC_TPLG_FSYNC_CP)
			config->format |= SOF_DAI_FMT_CBP_CFP;
		else
			config->format |= SOF_DAI_FMT_CBP_CFC;
	} else {
		/* codec is bclk consumer */
		if (hw_config->fsync_provider == SND_SOC_TPLG_FSYNC_CP)
			config->format |= SOF_DAI_FMT_CBC_CFP;
		else
			config->format |= SOF_DAI_FMT_CBC_CFC;
	}

	/* inverted clocks ? */
	config->format &= ~SOF_DAI_FMT_INV_MASK;
	if (hw_config->invert_bclk) {
		if (hw_config->invert_fsync)
			config->format |= SOF_DAI_FMT_IB_IF;
		else
			config->format |= SOF_DAI_FMT_IB_NF;
	} else {
		if (hw_config->invert_fsync)
			config->format |= SOF_DAI_FMT_NB_IF;
		else
			config->format |= SOF_DAI_FMT_NB_NF;
	}
}

static int sof_link_sai_load(struct snd_soc_component *scomp, struct snd_sof_dai_link *slink,
			     struct sof_ipc_dai_config *config, struct snd_sof_dai *dai)
{
	struct snd_soc_tplg_hw_config *hw_config = slink->hw_configs;
	struct sof_dai_private_data *private = dai->private;
	u32 size = sizeof(*config);
	int ret;

	/* handle master/slave and inverted clocks */
	sof_dai_set_format(hw_config, config);

	/* init IPC */
	memset(&config->sai, 0, sizeof(config->sai));
	config->hdr.size = size;

	/* parse one set of SAI tokens */
	ret = sof_update_ipc_object(scomp, &config->sai, SOF_SAI_TOKENS, slink->tuples,
				    slink->num_tuples, size, 1);
	if (ret < 0)
		return ret;

	config->sai.mclk_rate = le32_to_cpu(hw_config->mclk_rate);
	config->sai.bclk_rate = le32_to_cpu(hw_config->bclk_rate);
	config->sai.fsync_rate = le32_to_cpu(hw_config->fsync_rate);
	config->sai.mclk_direction = hw_config->mclk_direction;

	config->sai.tdm_slots = le32_to_cpu(hw_config->tdm_slots);
	config->sai.tdm_slot_width = le32_to_cpu(hw_config->tdm_slot_width);
	config->sai.rx_slots = le32_to_cpu(hw_config->rx_slots);
	config->sai.tx_slots = le32_to_cpu(hw_config->tx_slots);

	dev_info(scomp->dev,
		 "tplg: config SAI%d fmt 0x%x mclk %d width %d slots %d mclk id %d\n",
		config->dai_index, config->format,
		config->sai.mclk_rate, config->sai.tdm_slot_width,
		config->sai.tdm_slots, config->sai.mclk_id);

	if (config->sai.tdm_slots < 1 || config->sai.tdm_slots > 8) {
		dev_err(scomp->dev, "Invalid channel count for SAI%d\n", config->dai_index);
		return -EINVAL;
	}

	dai->number_configs = 1;
	dai->current_config = 0;
	private->dai_config = kmemdup(config, size, GFP_KERNEL);
	if (!private->dai_config)
		return -ENOMEM;

	return 0;
}

static int sof_link_esai_load(struct snd_soc_component *scomp, struct snd_sof_dai_link *slink,
			      struct sof_ipc_dai_config *config, struct snd_sof_dai *dai)
{
	struct snd_soc_tplg_hw_config *hw_config = slink->hw_configs;
	struct sof_dai_private_data *private = dai->private;
	u32 size = sizeof(*config);
	int ret;

	/* handle master/slave and inverted clocks */
	sof_dai_set_format(hw_config, config);

	/* init IPC */
	memset(&config->esai, 0, sizeof(config->esai));
	config->hdr.size = size;

	/* parse one set of ESAI tokens */
	ret = sof_update_ipc_object(scomp, &config->esai, SOF_ESAI_TOKENS, slink->tuples,
				    slink->num_tuples, size, 1);
	if (ret < 0)
		return ret;

	config->esai.mclk_rate = le32_to_cpu(hw_config->mclk_rate);
	config->esai.bclk_rate = le32_to_cpu(hw_config->bclk_rate);
	config->esai.fsync_rate = le32_to_cpu(hw_config->fsync_rate);
	config->esai.mclk_direction = hw_config->mclk_direction;
	config->esai.tdm_slots = le32_to_cpu(hw_config->tdm_slots);
	config->esai.tdm_slot_width = le32_to_cpu(hw_config->tdm_slot_width);
	config->esai.rx_slots = le32_to_cpu(hw_config->rx_slots);
	config->esai.tx_slots = le32_to_cpu(hw_config->tx_slots);

	dev_info(scomp->dev,
		 "tplg: config ESAI%d fmt 0x%x mclk %d width %d slots %d mclk id %d\n",
		config->dai_index, config->format,
		config->esai.mclk_rate, config->esai.tdm_slot_width,
		config->esai.tdm_slots, config->esai.mclk_id);

	if (config->esai.tdm_slots < 1 || config->esai.tdm_slots > 8) {
		dev_err(scomp->dev, "Invalid channel count for ESAI%d\n", config->dai_index);
		return -EINVAL;
	}

	dai->number_configs = 1;
	dai->current_config = 0;
	private->dai_config = kmemdup(config, size, GFP_KERNEL);
	if (!private->dai_config)
		return -ENOMEM;

	return 0;
}

static int sof_link_acp_dmic_load(struct snd_soc_component *scomp, struct snd_sof_dai_link *slink,
				  struct sof_ipc_dai_config *config, struct snd_sof_dai *dai)
{
	struct snd_soc_tplg_hw_config *hw_config = slink->hw_configs;
	struct sof_dai_private_data *private = dai->private;
	u32 size = sizeof(*config);

       /* handle master/slave and inverted clocks */
	sof_dai_set_format(hw_config, config);

	/* init IPC */
	memset(&config->acpdmic, 0, sizeof(config->acpdmic));
	config->hdr.size = size;

	config->acpdmic.fsync_rate = le32_to_cpu(hw_config->fsync_rate);
	config->acpdmic.tdm_slots = le32_to_cpu(hw_config->tdm_slots);

	dev_info(scomp->dev, "ACP_DMIC config ACP%d channel %d rate %d\n",
		 config->dai_index, config->acpdmic.tdm_slots,
		 config->acpdmic.fsync_rate);

	dai->number_configs = 1;
	dai->current_config = 0;
	private->dai_config = kmemdup(config, size, GFP_KERNEL);
	if (!private->dai_config)
		return -ENOMEM;

	return 0;
}

static int sof_link_acp_bt_load(struct snd_soc_component *scomp, struct snd_sof_dai_link *slink,
				struct sof_ipc_dai_config *config, struct snd_sof_dai *dai)
{
	struct snd_soc_tplg_hw_config *hw_config = slink->hw_configs;
	struct sof_dai_private_data *private = dai->private;
	u32 size = sizeof(*config);

	/* handle master/slave and inverted clocks */
	sof_dai_set_format(hw_config, config);

	/* init IPC */
	memset(&config->acpbt, 0, sizeof(config->acpbt));
	config->hdr.size = size;

	config->acpbt.fsync_rate = le32_to_cpu(hw_config->fsync_rate);
	config->acpbt.tdm_slots = le32_to_cpu(hw_config->tdm_slots);

	dev_info(scomp->dev, "ACP_BT config ACP%d channel %d rate %d\n",
		 config->dai_index, config->acpbt.tdm_slots,
		 config->acpbt.fsync_rate);

	dai->number_configs = 1;
	dai->current_config = 0;
	private->dai_config = kmemdup(config, size, GFP_KERNEL);
	if (!private->dai_config)
		return -ENOMEM;

	return 0;
}

static int sof_link_acp_sp_load(struct snd_soc_component *scomp, struct snd_sof_dai_link *slink,
				struct sof_ipc_dai_config *config, struct snd_sof_dai *dai)
{
	struct snd_soc_tplg_hw_config *hw_config = slink->hw_configs;
	struct sof_dai_private_data *private = dai->private;
	u32 size = sizeof(*config);

	/* handle master/slave and inverted clocks */
	sof_dai_set_format(hw_config, config);

	/* init IPC */
	memset(&config->acpsp, 0, sizeof(config->acpsp));
	config->hdr.size = size;

	config->acpsp.fsync_rate = le32_to_cpu(hw_config->fsync_rate);
	config->acpsp.tdm_slots = le32_to_cpu(hw_config->tdm_slots);

	dev_info(scomp->dev, "ACP_SP config ACP%d channel %d rate %d\n",
		 config->dai_index, config->acpsp.tdm_slots,
		 config->acpsp.fsync_rate);

	dai->number_configs = 1;
	dai->current_config = 0;
	private->dai_config = kmemdup(config, size, GFP_KERNEL);
	if (!private->dai_config)
		return -ENOMEM;

	return 0;
}

static int sof_link_afe_load(struct snd_soc_component *scomp, struct snd_sof_dai_link *slink,
			     struct sof_ipc_dai_config *config, struct snd_sof_dai *dai)
{
	struct sof_dai_private_data *private = dai->private;
	u32 size = sizeof(*config);
	int ret;

	config->hdr.size = size;

	/* parse the required set of AFE tokens based on num_hw_cfgs */
	ret = sof_update_ipc_object(scomp, &config->afe, SOF_AFE_TOKENS, slink->tuples,
				    slink->num_tuples, size, slink->num_hw_configs);
	if (ret < 0)
		return ret;

	dev_dbg(scomp->dev, "AFE config rate %d channels %d format:%d\n",
		config->afe.rate, config->afe.channels, config->afe.format);

	config->afe.stream_id = DMA_CHAN_INVALID;

	dai->number_configs = 1;
	dai->current_config = 0;
	private->dai_config = kmemdup(config, size, GFP_KERNEL);
	if (!private->dai_config)
		return -ENOMEM;

	return 0;
}

static int sof_link_ssp_load(struct snd_soc_component *scomp, struct snd_sof_dai_link *slink,
			     struct sof_ipc_dai_config *config, struct snd_sof_dai *dai)
{
	struct snd_soc_tplg_hw_config *hw_config = slink->hw_configs;
	struct sof_dai_private_data *private = dai->private;
	u32 size = sizeof(*config);
	int current_config = 0;
	int i, ret;

	/*
	 * Parse common data, we should have 1 common data per hw_config.
	 */
	ret = sof_update_ipc_object(scomp, &config->ssp, SOF_SSP_TOKENS, slink->tuples,
				    slink->num_tuples, size, slink->num_hw_configs);
	if (ret < 0)
		return ret;

	/* process all possible hw configs */
	for (i = 0; i < slink->num_hw_configs; i++) {
		if (le32_to_cpu(hw_config[i].id) == slink->default_hw_cfg_id)
			current_config = i;

		/* handle master/slave and inverted clocks */
		sof_dai_set_format(&hw_config[i], &config[i]);

		config[i].hdr.size = size;

		/* copy differentiating hw configs to ipc structs */
		config[i].ssp.mclk_rate = le32_to_cpu(hw_config[i].mclk_rate);
		config[i].ssp.bclk_rate = le32_to_cpu(hw_config[i].bclk_rate);
		config[i].ssp.fsync_rate = le32_to_cpu(hw_config[i].fsync_rate);
		config[i].ssp.tdm_slots = le32_to_cpu(hw_config[i].tdm_slots);
		config[i].ssp.tdm_slot_width = le32_to_cpu(hw_config[i].tdm_slot_width);
		config[i].ssp.mclk_direction = hw_config[i].mclk_direction;
		config[i].ssp.rx_slots = le32_to_cpu(hw_config[i].rx_slots);
		config[i].ssp.tx_slots = le32_to_cpu(hw_config[i].tx_slots);

		dev_dbg(scomp->dev, "tplg: config SSP%d fmt %#x mclk %d bclk %d fclk %d width (%d)%d slots %d mclk id %d quirks %d clks_control %#x\n",
			config[i].dai_index, config[i].format,
			config[i].ssp.mclk_rate, config[i].ssp.bclk_rate,
			config[i].ssp.fsync_rate, config[i].ssp.sample_valid_bits,
			config[i].ssp.tdm_slot_width, config[i].ssp.tdm_slots,
			config[i].ssp.mclk_id, config[i].ssp.quirks, config[i].ssp.clks_control);

		/* validate SSP fsync rate and channel count */
		if (config[i].ssp.fsync_rate < 8000 || config[i].ssp.fsync_rate > 192000) {
			dev_err(scomp->dev, "Invalid fsync rate for SSP%d\n", config[i].dai_index);
			return -EINVAL;
		}

		if (config[i].ssp.tdm_slots < 1 || config[i].ssp.tdm_slots > 8) {
			dev_err(scomp->dev, "Invalid channel count for SSP%d\n",
				config[i].dai_index);
			return -EINVAL;
		}
	}

	dai->number_configs = slink->num_hw_configs;
	dai->current_config = current_config;
	private->dai_config = kmemdup(config, size * slink->num_hw_configs, GFP_KERNEL);
	if (!private->dai_config)
		return -ENOMEM;

	return 0;
}

static int sof_link_dmic_load(struct snd_soc_component *scomp, struct snd_sof_dai_link *slink,
			      struct sof_ipc_dai_config *config, struct snd_sof_dai *dai)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	struct sof_dai_private_data *private = dai->private;
	struct sof_ipc_fw_ready *ready = &sdev->fw_ready;
	struct sof_ipc_fw_version *v = &ready->version;
	size_t size = sizeof(*config);
	int i, ret;

	/* Ensure the entire DMIC config struct is zeros */
	memset(&config->dmic, 0, sizeof(config->dmic));

	/* parse the required set of DMIC tokens based on num_hw_cfgs */
	ret = sof_update_ipc_object(scomp, &config->dmic, SOF_DMIC_TOKENS, slink->tuples,
				    slink->num_tuples, size, slink->num_hw_configs);
	if (ret < 0)
		return ret;

	/* parse the required set of DMIC PDM tokens based on number of active PDM's */
	ret = sof_update_ipc_object(scomp, &config->dmic.pdm[0], SOF_DMIC_PDM_TOKENS,
				    slink->tuples, slink->num_tuples,
				    sizeof(struct sof_ipc_dai_dmic_pdm_ctrl),
				    config->dmic.num_pdm_active);
	if (ret < 0)
		return ret;

	/* set IPC header size */
	config->hdr.size = size;

	/* debug messages */
	dev_dbg(scomp->dev, "tplg: config DMIC%d driver version %d\n",
		config->dai_index, config->dmic.driver_ipc_version);
	dev_dbg(scomp->dev, "pdmclk_min %d pdm_clkmax %d duty_min %d\n",
		config->dmic.pdmclk_min, config->dmic.pdmclk_max,
		config->dmic.duty_min);
	dev_dbg(scomp->dev, "duty_max %d fifo_fs %d num_pdms active %d\n",
		config->dmic.duty_max, config->dmic.fifo_fs,
		config->dmic.num_pdm_active);
	dev_dbg(scomp->dev, "fifo word length %d\n", config->dmic.fifo_bits);

	for (i = 0; i < config->dmic.num_pdm_active; i++) {
		dev_dbg(scomp->dev, "pdm %d mic a %d mic b %d\n",
			config->dmic.pdm[i].id,
			config->dmic.pdm[i].enable_mic_a,
			config->dmic.pdm[i].enable_mic_b);
		dev_dbg(scomp->dev, "pdm %d polarity a %d polarity b %d\n",
			config->dmic.pdm[i].id,
			config->dmic.pdm[i].polarity_mic_a,
			config->dmic.pdm[i].polarity_mic_b);
		dev_dbg(scomp->dev, "pdm %d clk_edge %d skew %d\n",
			config->dmic.pdm[i].id,
			config->dmic.pdm[i].clk_edge,
			config->dmic.pdm[i].skew);
	}

	/*
	 * this takes care of backwards compatible handling of fifo_bits_b.
	 * It is deprecated since firmware ABI version 3.0.1.
	 */
	if (SOF_ABI_VER(v->major, v->minor, v->micro) < SOF_ABI_VER(3, 0, 1))
		config->dmic.fifo_bits_b = config->dmic.fifo_bits;

	dai->number_configs = 1;
	dai->current_config = 0;
	private->dai_config = kmemdup(config, size, GFP_KERNEL);
	if (!private->dai_config)
		return -ENOMEM;

	return 0;
}

static int sof_link_alh_load(struct snd_soc_component *scomp, struct snd_sof_dai_link *slink,
			     struct sof_ipc_dai_config *config, struct snd_sof_dai *dai)
{
	struct sof_dai_private_data *private = dai->private;
	u32 size = sizeof(*config);
	int ret;

	/* parse the required set of ALH tokens based on num_hw_cfgs */
	ret = sof_update_ipc_object(scomp, &config->alh, SOF_ALH_TOKENS, slink->tuples,
				    slink->num_tuples, size, slink->num_hw_configs);
	if (ret < 0)
		return ret;

	/* init IPC */
	config->hdr.size = size;

	/* set config for all DAI's with name matching the link name */
	dai->number_configs = 1;
	dai->current_config = 0;
	private->dai_config = kmemdup(config, size, GFP_KERNEL);
	if (!private->dai_config)
		return -ENOMEM;

	return 0;
}

static int sof_ipc3_widget_setup_comp_dai(struct snd_sof_widget *swidget)
{
	struct snd_soc_component *scomp = swidget->scomp;
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	struct snd_sof_dai *dai = swidget->private;
	struct sof_dai_private_data *private;
	struct sof_ipc_comp_dai *comp_dai;
	size_t ipc_size = sizeof(*comp_dai);
	struct sof_ipc_dai_config *config;
	struct snd_sof_dai_link *slink;
	int ret;

	private = kzalloc(sizeof(*private), GFP_KERNEL);
	if (!private)
		return -ENOMEM;

	dai->private = private;

	private->comp_dai = sof_comp_alloc(swidget, &ipc_size, swidget->pipeline_id);
	if (!private->comp_dai) {
		ret = -ENOMEM;
		goto free;
	}

	/* configure dai IPC message */
	comp_dai = private->comp_dai;
	comp_dai->comp.type = SOF_COMP_DAI;
	comp_dai->config.hdr.size = sizeof(comp_dai->config);

	/* parse one set of DAI tokens */
	ret = sof_update_ipc_object(scomp, comp_dai, SOF_DAI_TOKENS, swidget->tuples,
				    swidget->num_tuples, sizeof(*comp_dai), 1);
	if (ret < 0)
		goto free;

	/* update comp_tokens */
	ret = sof_update_ipc_object(scomp, &comp_dai->config, SOF_COMP_TOKENS,
				    swidget->tuples, swidget->num_tuples,
				    sizeof(comp_dai->config), 1);
	if (ret < 0)
		goto free;

	dev_dbg(scomp->dev, "%s dai %s: type %d index %d\n",
		__func__, swidget->widget->name, comp_dai->type, comp_dai->dai_index);
	sof_dbg_comp_config(scomp, &comp_dai->config);

	/* now update DAI config */
	list_for_each_entry(slink, &sdev->dai_link_list, list) {
		struct sof_ipc_dai_config common_config;
		int i;

		if (strcmp(slink->link->name, dai->name))
			continue;

		/* Reserve memory for all hw configs, eventually freed by widget */
		config = kcalloc(slink->num_hw_configs, sizeof(*config), GFP_KERNEL);
		if (!config) {
			ret = -ENOMEM;
			goto free_comp;
		}

		/* parse one set of DAI link tokens */
		ret = sof_update_ipc_object(scomp, &common_config, SOF_DAI_LINK_TOKENS,
					    slink->tuples, slink->num_tuples,
					    sizeof(common_config), 1);
		if (ret < 0)
			goto free_config;

		for (i = 0; i < slink->num_hw_configs; i++) {
			config[i].hdr.cmd = SOF_IPC_GLB_DAI_MSG | SOF_IPC_DAI_CONFIG;
			config[i].format = le32_to_cpu(slink->hw_configs[i].fmt);
			config[i].type = common_config.type;
			config[i].dai_index = comp_dai->dai_index;
		}

		switch (common_config.type) {
		case SOF_DAI_INTEL_SSP:
			ret = sof_link_ssp_load(scomp, slink, config, dai);
			break;
		case SOF_DAI_INTEL_DMIC:
			ret = sof_link_dmic_load(scomp, slink, config, dai);
			break;
		case SOF_DAI_INTEL_HDA:
			ret = sof_link_hda_load(scomp, slink, config, dai);
			break;
		case SOF_DAI_INTEL_ALH:
			ret = sof_link_alh_load(scomp, slink, config, dai);
			break;
		case SOF_DAI_IMX_SAI:
			ret = sof_link_sai_load(scomp, slink, config, dai);
			break;
		case SOF_DAI_IMX_ESAI:
			ret = sof_link_esai_load(scomp, slink, config, dai);
			break;
		case SOF_DAI_AMD_BT:
			ret = sof_link_acp_bt_load(scomp, slink, config, dai);
			break;
		case SOF_DAI_AMD_SP:
			ret = sof_link_acp_sp_load(scomp, slink, config, dai);
			break;
		case SOF_DAI_AMD_DMIC:
			ret = sof_link_acp_dmic_load(scomp, slink, config, dai);
			break;
		case SOF_DAI_MEDIATEK_AFE:
			ret = sof_link_afe_load(scomp, slink, config, dai);
			break;
		default:
			break;
		}
		if (ret < 0) {
			dev_err(scomp->dev, "failed to load config for dai %s\n", dai->name);
			goto free_config;
		}

		kfree(config);
	}

	return 0;
free_config:
	kfree(config);
free_comp:
	kfree(comp_dai);
free:
	kfree(private);
	dai->private = NULL;
	return ret;
}

static void sof_ipc3_widget_free_comp_dai(struct snd_sof_widget *swidget)
{
	switch (swidget->id) {
	case snd_soc_dapm_dai_in:
	case snd_soc_dapm_dai_out:
	{
		struct snd_sof_dai *dai = swidget->private;
		struct sof_dai_private_data *dai_data;

		if (!dai)
			return;

		dai_data = dai->private;
		if (dai_data) {
			kfree(dai_data->comp_dai);
			kfree(dai_data->dai_config);
			kfree(dai_data);
		}
		kfree(dai);
		break;
	}
	default:
		break;
	}
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

static int sof_ipc3_control_load_bytes(struct snd_sof_dev *sdev, struct snd_sof_control *scontrol)
{
	struct sof_ipc_ctrl_data *cdata;
	int ret;

	scontrol->ipc_control_data = kzalloc(scontrol->max_size, GFP_KERNEL);
	if (!scontrol->ipc_control_data)
		return -ENOMEM;

	if (scontrol->max_size < sizeof(*cdata) ||
	    scontrol->max_size < sizeof(struct sof_abi_hdr)) {
		ret = -EINVAL;
		goto err;
	}

	/* init the get/put bytes data */
	if (scontrol->priv_size > scontrol->max_size - sizeof(*cdata)) {
		dev_err(sdev->dev, "err: bytes data size %zu exceeds max %zu.\n",
			scontrol->priv_size, scontrol->max_size - sizeof(*cdata));
		ret = -EINVAL;
		goto err;
	}

	scontrol->size = sizeof(struct sof_ipc_ctrl_data) + scontrol->priv_size;

	cdata = scontrol->ipc_control_data;
	cdata->cmd = SOF_CTRL_CMD_BINARY;
	cdata->index = scontrol->index;

	if (scontrol->priv_size > 0) {
		memcpy(cdata->data, scontrol->priv, scontrol->priv_size);
		kfree(scontrol->priv);

		if (cdata->data->magic != SOF_ABI_MAGIC) {
			dev_err(sdev->dev, "Wrong ABI magic 0x%08x.\n", cdata->data->magic);
			ret = -EINVAL;
			goto err;
		}

		if (SOF_ABI_VERSION_INCOMPATIBLE(SOF_ABI_VERSION, cdata->data->abi)) {
			dev_err(sdev->dev, "Incompatible ABI version 0x%08x.\n",
				cdata->data->abi);
			ret = -EINVAL;
			goto err;
		}

		if (cdata->data->size + sizeof(struct sof_abi_hdr) != scontrol->priv_size) {
			dev_err(sdev->dev, "Conflict in bytes vs. priv size.\n");
			ret = -EINVAL;
			goto err;
		}
	}

	return 0;
err:
	kfree(scontrol->ipc_control_data);
	return ret;
}

static int sof_ipc3_control_load_volume(struct snd_sof_dev *sdev, struct snd_sof_control *scontrol)
{
	struct sof_ipc_ctrl_data *cdata;
	int i;

	/* init the volume get/put data */
	scontrol->size = struct_size(cdata, chanv, scontrol->num_channels);

	scontrol->ipc_control_data = kzalloc(scontrol->size, GFP_KERNEL);
	if (!scontrol->ipc_control_data)
		return -ENOMEM;

	cdata = scontrol->ipc_control_data;
	cdata->index = scontrol->index;

	/* set cmd for mixer control */
	if (scontrol->max == 1) {
		cdata->cmd = SOF_CTRL_CMD_SWITCH;
		return 0;
	}

	cdata->cmd = SOF_CTRL_CMD_VOLUME;

	/* set default volume values to 0dB in control */
	for (i = 0; i < scontrol->num_channels; i++) {
		cdata->chanv[i].channel = i;
		cdata->chanv[i].value = VOL_ZERO_DB;
	}

	return 0;
}

static int sof_ipc3_control_load_enum(struct snd_sof_dev *sdev, struct snd_sof_control *scontrol)
{
	struct sof_ipc_ctrl_data *cdata;

	/* init the enum get/put data */
	scontrol->size = struct_size(cdata, chanv, scontrol->num_channels);

	scontrol->ipc_control_data = kzalloc(scontrol->size, GFP_KERNEL);
	if (!scontrol->ipc_control_data)
		return -ENOMEM;

	cdata = scontrol->ipc_control_data;
	cdata->index = scontrol->index;
	cdata->cmd = SOF_CTRL_CMD_ENUM;

	return 0;
}

static int sof_ipc3_control_setup(struct snd_sof_dev *sdev, struct snd_sof_control *scontrol)
{
	switch (scontrol->info_type) {
	case SND_SOC_TPLG_CTL_VOLSW:
	case SND_SOC_TPLG_CTL_VOLSW_SX:
	case SND_SOC_TPLG_CTL_VOLSW_XR_SX:
		return sof_ipc3_control_load_volume(sdev, scontrol);
	case SND_SOC_TPLG_CTL_BYTES:
		return sof_ipc3_control_load_bytes(sdev, scontrol);
	case SND_SOC_TPLG_CTL_ENUM:
	case SND_SOC_TPLG_CTL_ENUM_VALUE:
		return sof_ipc3_control_load_enum(sdev, scontrol);
	default:
		break;
	}

	return 0;
}

static int sof_ipc3_control_free(struct snd_sof_dev *sdev, struct snd_sof_control *scontrol)
{
	struct sof_ipc_free fcomp;

	fcomp.hdr.cmd = SOF_IPC_GLB_TPLG_MSG | SOF_IPC_TPLG_COMP_FREE;
	fcomp.hdr.size = sizeof(fcomp);
	fcomp.id = scontrol->comp_id;

	/* send IPC to the DSP */
	return sof_ipc_tx_message(sdev->ipc, fcomp.hdr.cmd, &fcomp, sizeof(fcomp), NULL, 0);
}

/* send pcm params ipc */
static int sof_ipc3_keyword_detect_pcm_params(struct snd_sof_widget *swidget, int dir)
{
	struct snd_soc_component *scomp = swidget->scomp;
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	struct sof_ipc_pcm_params_reply ipc_params_reply;
	struct snd_pcm_hw_params *params;
	struct sof_ipc_pcm_params pcm;
	struct snd_sof_pcm *spcm;
	int ret;

	/* get runtime PCM params using widget's stream name */
	spcm = snd_sof_find_spcm_name(scomp, swidget->widget->sname);
	if (!spcm) {
		dev_err(scomp->dev, "Cannot find PCM for %s\n", swidget->widget->name);
		return -EINVAL;
	}

	params = &spcm->params[dir];

	/* set IPC PCM params */
	memset(&pcm, 0, sizeof(pcm));
	pcm.hdr.size = sizeof(pcm);
	pcm.hdr.cmd = SOF_IPC_GLB_STREAM_MSG | SOF_IPC_STREAM_PCM_PARAMS;
	pcm.comp_id = swidget->comp_id;
	pcm.params.hdr.size = sizeof(pcm.params);
	pcm.params.direction = dir;
	pcm.params.sample_valid_bytes = params_width(params) >> 3;
	pcm.params.buffer_fmt = SOF_IPC_BUFFER_INTERLEAVED;
	pcm.params.rate = params_rate(params);
	pcm.params.channels = params_channels(params);
	pcm.params.host_period_bytes = params_period_bytes(params);

	/* set format */
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
	default:
		return -EINVAL;
	}

	/* send IPC to the DSP */
	ret = sof_ipc_tx_message(sdev->ipc, pcm.hdr.cmd, &pcm, sizeof(pcm),
				 &ipc_params_reply, sizeof(ipc_params_reply));
	if (ret < 0)
		dev_err(scomp->dev, "%s: PCM params failed for %s\n", __func__,
			swidget->widget->name);

	return ret;
}

 /* send stream trigger ipc */
static int sof_ipc3_keyword_detect_trigger(struct snd_sof_widget *swidget, int cmd)
{
	struct snd_soc_component *scomp = swidget->scomp;
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	struct sof_ipc_stream stream;
	struct sof_ipc_reply reply;
	int ret;

	/* set IPC stream params */
	stream.hdr.size = sizeof(stream);
	stream.hdr.cmd = SOF_IPC_GLB_STREAM_MSG | cmd;
	stream.comp_id = swidget->comp_id;

	/* send IPC to the DSP */
	ret = sof_ipc_tx_message(sdev->ipc, stream.hdr.cmd, &stream,
				 sizeof(stream), &reply, sizeof(reply));
	if (ret < 0)
		dev_err(scomp->dev, "%s: Failed to trigger %s\n", __func__, swidget->widget->name);

	return ret;
}

static int sof_ipc3_keyword_dapm_event(struct snd_soc_dapm_widget *w,
				       struct snd_kcontrol *k, int event)
{
	struct snd_sof_widget *swidget = w->dobj.private;
	struct snd_soc_component *scomp;
	int stream = SNDRV_PCM_STREAM_CAPTURE;
	struct snd_sof_pcm *spcm;
	int ret = 0;

	if (!swidget)
		return 0;

	scomp = swidget->scomp;

	dev_dbg(scomp->dev, "received event %d for widget %s\n",
		event, w->name);

	/* get runtime PCM params using widget's stream name */
	spcm = snd_sof_find_spcm_name(scomp, swidget->widget->sname);
	if (!spcm) {
		dev_err(scomp->dev, "%s: Cannot find PCM for %s\n", __func__,
			swidget->widget->name);
		return -EINVAL;
	}

	/* process events */
	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		if (spcm->stream[stream].suspend_ignored) {
			dev_dbg(scomp->dev, "PRE_PMU event ignored, KWD pipeline is already RUNNING\n");
			return 0;
		}

		/* set pcm params */
		ret = sof_ipc3_keyword_detect_pcm_params(swidget, stream);
		if (ret < 0) {
			dev_err(scomp->dev, "%s: Failed to set pcm params for widget %s\n",
				__func__, swidget->widget->name);
			break;
		}

		/* start trigger */
		ret = sof_ipc3_keyword_detect_trigger(swidget, SOF_IPC_STREAM_TRIG_START);
		if (ret < 0)
			dev_err(scomp->dev, "%s: Failed to trigger widget %s\n", __func__,
				swidget->widget->name);
		break;
	case SND_SOC_DAPM_POST_PMD:
		if (spcm->stream[stream].suspend_ignored) {
			dev_dbg(scomp->dev,
				"POST_PMD event ignored, KWD pipeline will remain RUNNING\n");
			return 0;
		}

		/* stop trigger */
		ret = sof_ipc3_keyword_detect_trigger(swidget, SOF_IPC_STREAM_TRIG_STOP);
		if (ret < 0)
			dev_err(scomp->dev, "%s: Failed to trigger widget %s\n", __func__,
				swidget->widget->name);

		/* pcm free */
		ret = sof_ipc3_keyword_detect_trigger(swidget, SOF_IPC_STREAM_PCM_FREE);
		if (ret < 0)
			dev_err(scomp->dev, "%s: Failed to free PCM for widget %s\n", __func__,
				swidget->widget->name);
		break;
	default:
		break;
	}

	return ret;
}

/* event handlers for keyword detect component */
static const struct snd_soc_tplg_widget_events sof_kwd_events[] = {
	{SOF_KEYWORD_DETECT_DAPM_EVENT, sof_ipc3_keyword_dapm_event},
};

static int sof_ipc3_widget_bind_event(struct snd_soc_component *scomp,
				      struct snd_sof_widget *swidget, u16 event_type)
{
	struct sof_ipc_comp *ipc_comp;

	/* validate widget event type */
	switch (event_type) {
	case SOF_KEYWORD_DETECT_DAPM_EVENT:
		/* only KEYWORD_DETECT comps should handle this */
		if (swidget->id != snd_soc_dapm_effect)
			break;

		ipc_comp = swidget->private;
		if (ipc_comp && ipc_comp->type != SOF_COMP_KEYWORD_DETECT)
			break;

		/* bind event to keyword detect comp */
		return snd_soc_tplg_widget_bind_event(swidget->widget, sof_kwd_events,
						      ARRAY_SIZE(sof_kwd_events), event_type);
	default:
		break;
	}

	dev_err(scomp->dev, "Invalid event type %d for widget %s\n", event_type,
		swidget->widget->name);

	return -EINVAL;
}

static int sof_ipc3_complete_pipeline(struct snd_sof_dev *sdev, struct snd_sof_widget *swidget)
{
	struct sof_ipc_pipe_ready ready;
	struct sof_ipc_reply reply;
	int ret;

	dev_dbg(sdev->dev, "tplg: complete pipeline %s id %d\n",
		swidget->widget->name, swidget->comp_id);

	memset(&ready, 0, sizeof(ready));
	ready.hdr.size = sizeof(ready);
	ready.hdr.cmd = SOF_IPC_GLB_TPLG_MSG | SOF_IPC_TPLG_PIPE_COMPLETE;
	ready.comp_id = swidget->comp_id;

	ret = sof_ipc_tx_message(sdev->ipc, ready.hdr.cmd, &ready, sizeof(ready), &reply,
				 sizeof(reply));
	if (ret < 0)
		return ret;

	return 1;
}

static int sof_ipc3_widget_free(struct snd_sof_dev *sdev, struct snd_sof_widget *swidget)
{
	struct sof_ipc_free ipc_free = {
		.hdr = {
			.size = sizeof(ipc_free),
			.cmd = SOF_IPC_GLB_TPLG_MSG,
		},
		.id = swidget->comp_id,
	};
	struct sof_ipc_reply reply;
	int ret;

	if (!swidget->private)
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
	default:
		ipc_free.hdr.cmd |= SOF_IPC_TPLG_COMP_FREE;
		break;
	}

	ret = sof_ipc_tx_message(sdev->ipc, ipc_free.hdr.cmd, &ipc_free, sizeof(ipc_free),
				 &reply, sizeof(reply));
	if (ret < 0)
		dev_err(sdev->dev, "failed to free widget %s\n", swidget->widget->name);

	return ret;
}

static int sof_ipc3_dai_config(struct snd_sof_dev *sdev, struct snd_sof_widget *swidget,
			       unsigned int flags, struct snd_sof_dai_config_data *data)
{
	struct sof_ipc_fw_version *v = &sdev->fw_ready.version;
	struct snd_sof_dai *dai = swidget->private;
	struct sof_dai_private_data *private;
	struct sof_ipc_dai_config *config;
	struct sof_ipc_reply reply;
	int ret = 0;

	if (!dai || !dai->private) {
		dev_err(sdev->dev, "No private data for DAI %s\n", swidget->widget->name);
		return -EINVAL;
	}

	private = dai->private;
	if (!private->dai_config) {
		dev_err(sdev->dev, "No config for DAI %s\n", dai->name);
		return -EINVAL;
	}

	config = &private->dai_config[dai->current_config];
	if (!config) {
		dev_err(sdev->dev, "Invalid current config for DAI %s\n", dai->name);
		return -EINVAL;
	}

	switch (config->type) {
	case SOF_DAI_INTEL_SSP:
		/*
		 * DAI_CONFIG IPC during hw_params/hw_free for SSP DAI's is not supported in older
		 * firmware
		 */
		if (v->abi_version < SOF_ABI_VER(3, 18, 0) &&
		    ((flags & SOF_DAI_CONFIG_FLAGS_HW_PARAMS) ||
		     (flags & SOF_DAI_CONFIG_FLAGS_HW_FREE)))
			return 0;
		break;
	case SOF_DAI_INTEL_HDA:
		if (data)
			config->hda.link_dma_ch = data->dai_data;
		break;
	case SOF_DAI_INTEL_ALH:
		if (data) {
			config->dai_index = data->dai_index;
			config->alh.stream_id = data->dai_data;
		}
		break;
	default:
		break;
	}

	config->flags = flags;

	/* only send the IPC if the widget is set up in the DSP */
	if (swidget->use_count > 0) {
		ret = sof_ipc_tx_message(sdev->ipc, config->hdr.cmd, config, config->hdr.size,
					 &reply, sizeof(reply));
		if (ret < 0)
			dev_err(sdev->dev, "Failed to set dai config for %s\n", dai->name);
	}

	return ret;
}

static int sof_ipc3_widget_setup(struct snd_sof_dev *sdev, struct snd_sof_widget *swidget)
{
	struct sof_ipc_comp_reply reply;
	int ret;

	if (!swidget->private)
		return 0;

	switch (swidget->id) {
	case snd_soc_dapm_dai_in:
	case snd_soc_dapm_dai_out:
	{
		struct snd_sof_dai *dai = swidget->private;
		struct sof_dai_private_data *dai_data = dai->private;
		struct sof_ipc_comp *comp = &dai_data->comp_dai->comp;

		ret = sof_ipc_tx_message(sdev->ipc, comp->hdr.cmd, dai_data->comp_dai,
					 comp->hdr.size, &reply, sizeof(reply));
		break;
	}
	case snd_soc_dapm_scheduler:
	{
		struct sof_ipc_pipe_new *pipeline;

		pipeline = swidget->private;
		ret = sof_ipc_tx_message(sdev->ipc, pipeline->hdr.cmd, pipeline,
					 sizeof(*pipeline), &reply, sizeof(reply));
		break;
	}
	default:
	{
		struct sof_ipc_cmd_hdr *hdr;

		hdr = swidget->private;
		ret = sof_ipc_tx_message(sdev->ipc, hdr->cmd, swidget->private, hdr->size,
					 &reply, sizeof(reply));
		break;
	}
	}
	if (ret < 0)
		dev_err(sdev->dev, "Failed to setup widget %s\n", swidget->widget->name);

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

static enum sof_tokens dai_token_list[] = {
	SOF_CORE_TOKENS,
	SOF_COMP_EXT_TOKENS,
	SOF_DAI_TOKENS,
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

	[snd_soc_dapm_dai_in] = {sof_ipc3_widget_setup_comp_dai, sof_ipc3_widget_free_comp_dai,
				 dai_token_list, ARRAY_SIZE(dai_token_list), NULL},
	[snd_soc_dapm_dai_out] = {sof_ipc3_widget_setup_comp_dai, sof_ipc3_widget_free_comp_dai,
				  dai_token_list, ARRAY_SIZE(dai_token_list), NULL},
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
				 process_token_list, ARRAY_SIZE(process_token_list),
				 sof_ipc3_widget_bind_event},
};

static const struct sof_ipc_tplg_ops ipc3_tplg_ops = {
	.widget = tplg_ipc3_widget_ops,
	.route_setup = sof_ipc3_route_setup,
	.control_setup = sof_ipc3_control_setup,
	.control_free = sof_ipc3_control_free,
	.pipeline_complete = sof_ipc3_complete_pipeline,
	.token_list = ipc3_token_list,
	.widget_free = sof_ipc3_widget_free,
	.widget_setup = sof_ipc3_widget_setup,
	.dai_config = sof_ipc3_dai_config,
};

const struct sof_ipc_ops ipc3_ops = {
	.tplg = &ipc3_tplg_ops,
};
