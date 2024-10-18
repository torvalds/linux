// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2022 Intel Corporation
//
//
#include <linux/bitfield.h>
#include <uapi/sound/sof/tokens.h>
#include <sound/pcm_params.h>
#include <sound/sof/ext_manifest4.h>
#include <sound/intel-nhlt.h>
#include "sof-priv.h"
#include "sof-audio.h"
#include "ipc4-priv.h"
#include "ipc4-topology.h"
#include "ops.h"

/*
 * The ignore_cpc flag can be used to ignore the CPC value for all modules by
 * using 0 instead.
 * The CPC is sent to the firmware along with the SOF_IPC4_MOD_INIT_INSTANCE
 * message and it is used for clock scaling.
 * 0 as CPC value will instruct the firmware to use maximum frequency, thus
 * deactivating the clock scaling.
 */
static bool ignore_cpc;
module_param_named(ipc4_ignore_cpc, ignore_cpc, bool, 0444);
MODULE_PARM_DESC(ipc4_ignore_cpc,
		 "Ignore CPC values. This option will disable clock scaling in firmware.");

#define SOF_IPC4_GAIN_PARAM_ID  0
#define SOF_IPC4_TPLG_ABI_SIZE 6
#define SOF_IPC4_CHAIN_DMA_BUF_SIZE_MS 2

static DEFINE_IDA(alh_group_ida);
static DEFINE_IDA(pipeline_ida);

static const struct sof_topology_token ipc4_sched_tokens[] = {
	{SOF_TKN_SCHED_LP_MODE, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc4_pipeline, lp_mode)},
	{SOF_TKN_SCHED_USE_CHAIN_DMA, SND_SOC_TPLG_TUPLE_TYPE_BOOL, get_token_u16,
		offsetof(struct sof_ipc4_pipeline, use_chain_dma)},
	{SOF_TKN_SCHED_CORE, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc4_pipeline, core_id)},
	{SOF_TKN_SCHED_PRIORITY, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc4_pipeline, priority)},
};

static const struct sof_topology_token pipeline_tokens[] = {
	{SOF_TKN_SCHED_DYNAMIC_PIPELINE, SND_SOC_TPLG_TUPLE_TYPE_BOOL, get_token_u16,
		offsetof(struct snd_sof_widget, dynamic_pipeline_widget)},
};

static const struct sof_topology_token ipc4_comp_tokens[] = {
	{SOF_TKN_COMP_IS_PAGES, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc4_base_module_cfg, is_pages)},
};

static const struct sof_topology_token ipc4_in_audio_format_tokens[] = {
	{SOF_TKN_CAVS_AUDIO_FORMAT_IN_RATE, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc4_pin_format, audio_fmt.sampling_frequency)},
	{SOF_TKN_CAVS_AUDIO_FORMAT_IN_BIT_DEPTH, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc4_pin_format, audio_fmt.bit_depth)},
	{SOF_TKN_CAVS_AUDIO_FORMAT_IN_CH_MAP, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc4_pin_format, audio_fmt.ch_map)},
	{SOF_TKN_CAVS_AUDIO_FORMAT_IN_CH_CFG, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc4_pin_format, audio_fmt.ch_cfg)},
	{SOF_TKN_CAVS_AUDIO_FORMAT_IN_INTERLEAVING_STYLE, SND_SOC_TPLG_TUPLE_TYPE_WORD,
		get_token_u32, offsetof(struct sof_ipc4_pin_format,
		audio_fmt.interleaving_style)},
	{SOF_TKN_CAVS_AUDIO_FORMAT_IN_FMT_CFG, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc4_pin_format, audio_fmt.fmt_cfg)},
	{SOF_TKN_CAVS_AUDIO_FORMAT_INPUT_PIN_INDEX, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc4_pin_format, pin_index)},
	{SOF_TKN_CAVS_AUDIO_FORMAT_IBS, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc4_pin_format, buffer_size)},
};

static const struct sof_topology_token ipc4_out_audio_format_tokens[] = {
	{SOF_TKN_CAVS_AUDIO_FORMAT_OUT_RATE, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc4_pin_format, audio_fmt.sampling_frequency)},
	{SOF_TKN_CAVS_AUDIO_FORMAT_OUT_BIT_DEPTH, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc4_pin_format, audio_fmt.bit_depth)},
	{SOF_TKN_CAVS_AUDIO_FORMAT_OUT_CH_MAP, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc4_pin_format, audio_fmt.ch_map)},
	{SOF_TKN_CAVS_AUDIO_FORMAT_OUT_CH_CFG, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc4_pin_format, audio_fmt.ch_cfg)},
	{SOF_TKN_CAVS_AUDIO_FORMAT_OUT_INTERLEAVING_STYLE, SND_SOC_TPLG_TUPLE_TYPE_WORD,
		get_token_u32, offsetof(struct sof_ipc4_pin_format,
		audio_fmt.interleaving_style)},
	{SOF_TKN_CAVS_AUDIO_FORMAT_OUT_FMT_CFG, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc4_pin_format, audio_fmt.fmt_cfg)},
	{SOF_TKN_CAVS_AUDIO_FORMAT_OUTPUT_PIN_INDEX, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc4_pin_format, pin_index)},
	{SOF_TKN_CAVS_AUDIO_FORMAT_OBS, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc4_pin_format, buffer_size)},
};

static const struct sof_topology_token ipc4_copier_deep_buffer_tokens[] = {
	{SOF_TKN_INTEL_COPIER_DEEP_BUFFER_DMA_MS, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32, 0},
};

static const struct sof_topology_token ipc4_copier_tokens[] = {
	{SOF_TKN_INTEL_COPIER_NODE_TYPE, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32, 0},
};

static const struct sof_topology_token ipc4_audio_fmt_num_tokens[] = {
	{SOF_TKN_COMP_NUM_INPUT_AUDIO_FORMATS, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc4_available_audio_format, num_input_formats)},
	{SOF_TKN_COMP_NUM_OUTPUT_AUDIO_FORMATS, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc4_available_audio_format, num_output_formats)},
};

static const struct sof_topology_token dai_tokens[] = {
	{SOF_TKN_DAI_TYPE, SND_SOC_TPLG_TUPLE_TYPE_STRING, get_token_dai_type,
		offsetof(struct sof_ipc4_copier, dai_type)},
	{SOF_TKN_DAI_INDEX, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc4_copier, dai_index)},
};

/* Component extended tokens */
static const struct sof_topology_token comp_ext_tokens[] = {
	{SOF_TKN_COMP_UUID, SND_SOC_TPLG_TUPLE_TYPE_UUID, get_token_uuid,
		offsetof(struct snd_sof_widget, uuid)},
	{SOF_TKN_COMP_CORE_ID, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct snd_sof_widget, core)},
};

static const struct sof_topology_token gain_tokens[] = {
	{SOF_TKN_GAIN_RAMP_TYPE, SND_SOC_TPLG_TUPLE_TYPE_WORD,
		get_token_u32, offsetof(struct sof_ipc4_gain_params, curve_type)},
	{SOF_TKN_GAIN_RAMP_DURATION,
		SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc4_gain_params, curve_duration_l)},
	{SOF_TKN_GAIN_VAL, SND_SOC_TPLG_TUPLE_TYPE_WORD,
		get_token_u32, offsetof(struct sof_ipc4_gain_params, init_val)},
};

/* SRC */
static const struct sof_topology_token src_tokens[] = {
	{SOF_TKN_SRC_RATE_OUT, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc4_src_data, sink_rate)},
};

static const struct sof_token_info ipc4_token_list[SOF_TOKEN_COUNT] = {
	[SOF_DAI_TOKENS] = {"DAI tokens", dai_tokens, ARRAY_SIZE(dai_tokens)},
	[SOF_PIPELINE_TOKENS] = {"Pipeline tokens", pipeline_tokens, ARRAY_SIZE(pipeline_tokens)},
	[SOF_SCHED_TOKENS] = {"Scheduler tokens", ipc4_sched_tokens,
		ARRAY_SIZE(ipc4_sched_tokens)},
	[SOF_COMP_EXT_TOKENS] = {"Comp extended tokens", comp_ext_tokens,
		ARRAY_SIZE(comp_ext_tokens)},
	[SOF_COMP_TOKENS] = {"IPC4 Component tokens",
		ipc4_comp_tokens, ARRAY_SIZE(ipc4_comp_tokens)},
	[SOF_IN_AUDIO_FORMAT_TOKENS] = {"IPC4 Input Audio format tokens",
		ipc4_in_audio_format_tokens, ARRAY_SIZE(ipc4_in_audio_format_tokens)},
	[SOF_OUT_AUDIO_FORMAT_TOKENS] = {"IPC4 Output Audio format tokens",
		ipc4_out_audio_format_tokens, ARRAY_SIZE(ipc4_out_audio_format_tokens)},
	[SOF_COPIER_DEEP_BUFFER_TOKENS] = {"IPC4 Copier deep buffer tokens",
		ipc4_copier_deep_buffer_tokens, ARRAY_SIZE(ipc4_copier_deep_buffer_tokens)},
	[SOF_COPIER_TOKENS] = {"IPC4 Copier tokens", ipc4_copier_tokens,
		ARRAY_SIZE(ipc4_copier_tokens)},
	[SOF_AUDIO_FMT_NUM_TOKENS] = {"IPC4 Audio format number tokens",
		ipc4_audio_fmt_num_tokens, ARRAY_SIZE(ipc4_audio_fmt_num_tokens)},
	[SOF_GAIN_TOKENS] = {"Gain tokens", gain_tokens, ARRAY_SIZE(gain_tokens)},
	[SOF_SRC_TOKENS] = {"SRC tokens", src_tokens, ARRAY_SIZE(src_tokens)},
};

struct snd_sof_widget *sof_ipc4_find_swidget_by_ids(struct snd_sof_dev *sdev,
						    u32 module_id, int instance_id)
{
	struct snd_sof_widget *swidget;

	list_for_each_entry(swidget, &sdev->widget_list, list) {
		struct sof_ipc4_fw_module *fw_module = swidget->module_info;

		/* Only active module instances have valid instance_id */
		if (!swidget->use_count)
			continue;

		if (fw_module && fw_module->man4_module_entry.id == module_id &&
		    swidget->instance_id == instance_id)
			return swidget;
	}

	return NULL;
}

static void sof_ipc4_dbg_audio_format(struct device *dev, struct sof_ipc4_pin_format *pin_fmt,
				      int num_formats)
{
	int i;

	for (i = 0; i < num_formats; i++) {
		struct sof_ipc4_audio_format *fmt = &pin_fmt[i].audio_fmt;
		dev_dbg(dev,
			"Pin index #%d: %uHz, %ubit, %luch (ch_map %#x ch_cfg %u interleaving_style %u fmt_cfg %#x) buffer size %d\n",
			pin_fmt[i].pin_index, fmt->sampling_frequency, fmt->bit_depth,
			SOF_IPC4_AUDIO_FORMAT_CFG_CHANNELS_COUNT(fmt->fmt_cfg),
			fmt->ch_map, fmt->ch_cfg, fmt->interleaving_style, fmt->fmt_cfg,
			pin_fmt[i].buffer_size);
	}
}

static const struct sof_ipc4_audio_format *
sof_ipc4_get_input_pin_audio_fmt(struct snd_sof_widget *swidget, int pin_index)
{
	struct sof_ipc4_base_module_cfg_ext *base_cfg_ext;
	struct sof_ipc4_process *process;
	int i;

	if (swidget->id != snd_soc_dapm_effect) {
		struct sof_ipc4_base_module_cfg *base = swidget->private;

		/* For non-process modules, base module config format is used for all input pins */
		return &base->audio_fmt;
	}

	process = swidget->private;

	/*
	 * For process modules without base config extension, base module config
	 * format is used for all input pins
	 */
	if (process->init_config != SOF_IPC4_MODULE_INIT_CONFIG_TYPE_BASE_CFG_WITH_EXT)
		return &process->base_config.audio_fmt;

	base_cfg_ext = process->base_config_ext;

	/*
	 * If there are multiple input formats available for a pin, the first available format
	 * is chosen.
	 */
	for (i = 0; i < base_cfg_ext->num_input_pin_fmts; i++) {
		struct sof_ipc4_pin_format *pin_format = &base_cfg_ext->pin_formats[i];

		if (pin_format->pin_index == pin_index)
			return &pin_format->audio_fmt;
	}

	return NULL;
}

/**
 * sof_ipc4_get_audio_fmt - get available audio formats from swidget->tuples
 * @scomp: pointer to pointer to SOC component
 * @swidget: pointer to struct snd_sof_widget containing tuples
 * @available_fmt: pointer to struct sof_ipc4_available_audio_format being filling in
 * @module_base_cfg: Pointer to the base_config in the module init IPC payload
 *
 * Return: 0 if successful
 */
static int sof_ipc4_get_audio_fmt(struct snd_soc_component *scomp,
				  struct snd_sof_widget *swidget,
				  struct sof_ipc4_available_audio_format *available_fmt,
				  struct sof_ipc4_base_module_cfg *module_base_cfg)
{
	struct sof_ipc4_pin_format *in_format = NULL;
	struct sof_ipc4_pin_format *out_format;
	int ret;

	ret = sof_update_ipc_object(scomp, available_fmt,
				    SOF_AUDIO_FMT_NUM_TOKENS, swidget->tuples,
				    swidget->num_tuples, sizeof(*available_fmt), 1);
	if (ret) {
		dev_err(scomp->dev, "Failed to parse audio format token count\n");
		return ret;
	}

	if (!available_fmt->num_input_formats && !available_fmt->num_output_formats) {
		dev_err(scomp->dev, "No input/output pin formats set in topology\n");
		return -EINVAL;
	}

	dev_dbg(scomp->dev,
		"Number of input audio formats: %d. Number of output audio formats: %d\n",
		available_fmt->num_input_formats, available_fmt->num_output_formats);

	/* set is_pages in the module's base_config */
	ret = sof_update_ipc_object(scomp, module_base_cfg, SOF_COMP_TOKENS, swidget->tuples,
				    swidget->num_tuples, sizeof(*module_base_cfg), 1);
	if (ret) {
		dev_err(scomp->dev, "parse comp tokens for %s failed, error: %d\n",
			swidget->widget->name, ret);
		return ret;
	}

	dev_dbg(scomp->dev, "widget %s: is_pages: %d\n", swidget->widget->name,
		module_base_cfg->is_pages);

	if (available_fmt->num_input_formats) {
		in_format = kcalloc(available_fmt->num_input_formats,
				    sizeof(*in_format), GFP_KERNEL);
		if (!in_format)
			return -ENOMEM;
		available_fmt->input_pin_fmts = in_format;

		ret = sof_update_ipc_object(scomp, in_format,
					    SOF_IN_AUDIO_FORMAT_TOKENS, swidget->tuples,
					    swidget->num_tuples, sizeof(*in_format),
					    available_fmt->num_input_formats);
		if (ret) {
			dev_err(scomp->dev, "parse input audio fmt tokens failed %d\n", ret);
			goto err_in;
		}

		dev_dbg(scomp->dev, "Input audio formats for %s\n", swidget->widget->name);
		sof_ipc4_dbg_audio_format(scomp->dev, in_format,
					  available_fmt->num_input_formats);
	}

	if (available_fmt->num_output_formats) {
		out_format = kcalloc(available_fmt->num_output_formats, sizeof(*out_format),
				     GFP_KERNEL);
		if (!out_format) {
			ret = -ENOMEM;
			goto err_in;
		}

		ret = sof_update_ipc_object(scomp, out_format,
					    SOF_OUT_AUDIO_FORMAT_TOKENS, swidget->tuples,
					    swidget->num_tuples, sizeof(*out_format),
					    available_fmt->num_output_formats);
		if (ret) {
			dev_err(scomp->dev, "parse output audio fmt tokens failed\n");
			goto err_out;
		}

		available_fmt->output_pin_fmts = out_format;
		dev_dbg(scomp->dev, "Output audio formats for %s\n", swidget->widget->name);
		sof_ipc4_dbg_audio_format(scomp->dev, out_format,
					  available_fmt->num_output_formats);
	}

	return 0;

err_out:
	kfree(out_format);
err_in:
	kfree(in_format);
	available_fmt->input_pin_fmts = NULL;
	return ret;
}

/* release the memory allocated in sof_ipc4_get_audio_fmt */
static void sof_ipc4_free_audio_fmt(struct sof_ipc4_available_audio_format *available_fmt)

{
	kfree(available_fmt->output_pin_fmts);
	available_fmt->output_pin_fmts = NULL;
	kfree(available_fmt->input_pin_fmts);
	available_fmt->input_pin_fmts = NULL;
}

static void sof_ipc4_widget_free_comp_pipeline(struct snd_sof_widget *swidget)
{
	kfree(swidget->private);
}

static int sof_ipc4_widget_set_module_info(struct snd_sof_widget *swidget)
{
	struct snd_soc_component *scomp = swidget->scomp;
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);

	swidget->module_info = sof_ipc4_find_module_by_uuid(sdev, &swidget->uuid);

	if (swidget->module_info)
		return 0;

	dev_err(sdev->dev, "failed to find module info for widget %s with UUID %pUL\n",
		swidget->widget->name, &swidget->uuid);
	return -EINVAL;
}

static int sof_ipc4_widget_setup_msg(struct snd_sof_widget *swidget, struct sof_ipc4_msg *msg)
{
	struct sof_ipc4_fw_module *fw_module;
	uint32_t type;
	int ret;

	ret = sof_ipc4_widget_set_module_info(swidget);
	if (ret)
		return ret;

	fw_module = swidget->module_info;

	msg->primary = fw_module->man4_module_entry.id;
	msg->primary |= SOF_IPC4_MSG_TYPE_SET(SOF_IPC4_MOD_INIT_INSTANCE);
	msg->primary |= SOF_IPC4_MSG_DIR(SOF_IPC4_MSG_REQUEST);
	msg->primary |= SOF_IPC4_MSG_TARGET(SOF_IPC4_MODULE_MSG);

	msg->extension = SOF_IPC4_MOD_EXT_CORE_ID(swidget->core);

	type = (fw_module->man4_module_entry.type & SOF_IPC4_MODULE_DP) ? 1 : 0;
	msg->extension |= SOF_IPC4_MOD_EXT_DOMAIN(type);

	return 0;
}

static void sof_ipc4_widget_update_kcontrol_module_id(struct snd_sof_widget *swidget)
{
	struct snd_soc_component *scomp = swidget->scomp;
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	struct sof_ipc4_fw_module *fw_module = swidget->module_info;
	struct snd_sof_control *scontrol;

	/* update module ID for all kcontrols for this widget */
	list_for_each_entry(scontrol, &sdev->kcontrol_list, list) {
		if (scontrol->comp_id == swidget->comp_id) {
			struct sof_ipc4_control_data *cdata = scontrol->ipc_control_data;
			struct sof_ipc4_msg *msg = &cdata->msg;

			msg->primary |= fw_module->man4_module_entry.id;
		}
	}
}

static int
sof_ipc4_update_card_components_string(struct snd_sof_widget *swidget,
				       struct snd_sof_pcm *spcm, int dir)
{
	struct snd_sof_widget *pipe_widget = swidget->spipe->pipe_widget;
	struct sof_ipc4_pipeline *pipeline = pipe_widget->private;
	struct snd_soc_component *scomp = spcm->scomp;
	struct snd_soc_card *card = scomp->card;
	const char *pt_marker = "iec61937-pcm";

	/*
	 * Update the card's components list with iec61937-pcm and a list of PCM
	 * ids where ChainDMA is enabled.
	 * These PCMs can be used for bytestream passthrough.
	 */
	if (!pipeline->use_chain_dma)
		return 0;

	if (card->components) {
		const char *tmp = card->components;

		if (strstr(card->components, pt_marker))
			card->components = devm_kasprintf(card->dev, GFP_KERNEL,
							  "%s,%d",
							  card->components,
							  spcm->pcm.pcm_id);
		else
			card->components = devm_kasprintf(card->dev, GFP_KERNEL,
							  "%s %s:%d",
							  card->components,
							  pt_marker,
							  spcm->pcm.pcm_id);

		devm_kfree(card->dev, tmp);
	} else {
		card->components = devm_kasprintf(card->dev, GFP_KERNEL,
						  "%s:%d", pt_marker,
						  spcm->pcm.pcm_id);
	}

	if (!card->components)
		return -ENOMEM;

	return 0;
}

static int sof_ipc4_widget_setup_pcm(struct snd_sof_widget *swidget)
{
	struct sof_ipc4_available_audio_format *available_fmt;
	struct snd_soc_component *scomp = swidget->scomp;
	struct sof_ipc4_copier *ipc4_copier;
	struct snd_sof_pcm *spcm;
	int node_type = 0;
	int ret, dir;

	ipc4_copier = kzalloc(sizeof(*ipc4_copier), GFP_KERNEL);
	if (!ipc4_copier)
		return -ENOMEM;

	swidget->private = ipc4_copier;
	available_fmt = &ipc4_copier->available_fmt;

	dev_dbg(scomp->dev, "Updating IPC structure for %s\n", swidget->widget->name);

	ret = sof_ipc4_get_audio_fmt(scomp, swidget, available_fmt,
				     &ipc4_copier->data.base_config);
	if (ret)
		goto free_copier;

	/*
	 * This callback is used by host copier and module-to-module copier,
	 * and only host copier needs to set gtw_cfg.
	 */
	if (!WIDGET_IS_AIF(swidget->id))
		goto skip_gtw_cfg;

	ret = sof_update_ipc_object(scomp, &node_type,
				    SOF_COPIER_TOKENS, swidget->tuples,
				    swidget->num_tuples, sizeof(node_type), 1);

	if (ret) {
		dev_err(scomp->dev, "parse host copier node type token failed %d\n",
			ret);
		goto free_available_fmt;
	}
	dev_dbg(scomp->dev, "host copier '%s' node_type %u\n", swidget->widget->name, node_type);

	spcm = snd_sof_find_spcm_comp(scomp, swidget->comp_id, &dir);
	if (!spcm)
		goto skip_gtw_cfg;

	ret = sof_ipc4_update_card_components_string(swidget, spcm, dir);
	if (ret)
		goto free_available_fmt;

	if (dir == SNDRV_PCM_STREAM_PLAYBACK) {
		struct snd_sof_pcm_stream *sps = &spcm->stream[dir];

		sof_update_ipc_object(scomp, &sps->dsp_max_burst_size_in_ms,
				      SOF_COPIER_DEEP_BUFFER_TOKENS,
				      swidget->tuples,
				      swidget->num_tuples, sizeof(u32), 1);
		/* Set default DMA buffer size if it is not specified in topology */
		if (!sps->dsp_max_burst_size_in_ms)
			sps->dsp_max_burst_size_in_ms = SOF_IPC4_MIN_DMA_BUFFER_SIZE;
	} else {
		/* Capture data is copied from DSP to host in 1ms bursts */
		spcm->stream[dir].dsp_max_burst_size_in_ms = 1;
	}

skip_gtw_cfg:
	ipc4_copier->gtw_attr = kzalloc(sizeof(*ipc4_copier->gtw_attr), GFP_KERNEL);
	if (!ipc4_copier->gtw_attr) {
		ret = -ENOMEM;
		goto free_available_fmt;
	}

	ipc4_copier->copier_config = (uint32_t *)ipc4_copier->gtw_attr;
	ipc4_copier->data.gtw_cfg.config_length =
		sizeof(struct sof_ipc4_gtw_attributes) >> 2;

	switch (swidget->id) {
	case snd_soc_dapm_aif_in:
	case snd_soc_dapm_aif_out:
		ipc4_copier->data.gtw_cfg.node_id = SOF_IPC4_NODE_TYPE(node_type);
		break;
	case snd_soc_dapm_buffer:
		ipc4_copier->data.gtw_cfg.node_id = SOF_IPC4_INVALID_NODE_ID;
		ipc4_copier->ipc_config_size = 0;
		break;
	default:
		dev_err(scomp->dev, "invalid widget type %d\n", swidget->id);
		ret = -EINVAL;
		goto free_gtw_attr;
	}

	/* set up module info and message header */
	ret = sof_ipc4_widget_setup_msg(swidget, &ipc4_copier->msg);
	if (ret)
		goto free_gtw_attr;

	return 0;

free_gtw_attr:
	kfree(ipc4_copier->gtw_attr);
free_available_fmt:
	sof_ipc4_free_audio_fmt(available_fmt);
free_copier:
	kfree(ipc4_copier);
	swidget->private = NULL;
	return ret;
}

static void sof_ipc4_widget_free_comp_pcm(struct snd_sof_widget *swidget)
{
	struct sof_ipc4_copier *ipc4_copier = swidget->private;
	struct sof_ipc4_available_audio_format *available_fmt;

	if (!ipc4_copier)
		return;

	available_fmt = &ipc4_copier->available_fmt;
	kfree(available_fmt->output_pin_fmts);
	kfree(ipc4_copier->gtw_attr);
	kfree(ipc4_copier);
	swidget->private = NULL;
}

static int sof_ipc4_widget_setup_comp_dai(struct snd_sof_widget *swidget)
{
	struct sof_ipc4_available_audio_format *available_fmt;
	struct snd_soc_component *scomp = swidget->scomp;
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	struct snd_sof_dai *dai = swidget->private;
	struct sof_ipc4_copier *ipc4_copier;
	struct snd_sof_widget *pipe_widget;
	struct sof_ipc4_pipeline *pipeline;
	int node_type = 0;
	int ret;

	ipc4_copier = kzalloc(sizeof(*ipc4_copier), GFP_KERNEL);
	if (!ipc4_copier)
		return -ENOMEM;

	available_fmt = &ipc4_copier->available_fmt;

	dev_dbg(scomp->dev, "Updating IPC structure for %s\n", swidget->widget->name);

	ret = sof_ipc4_get_audio_fmt(scomp, swidget, available_fmt,
				     &ipc4_copier->data.base_config);
	if (ret)
		goto free_copier;

	ret = sof_update_ipc_object(scomp, &node_type,
				    SOF_COPIER_TOKENS, swidget->tuples,
				    swidget->num_tuples, sizeof(node_type), 1);
	if (ret) {
		dev_err(scomp->dev, "parse dai node type failed %d\n", ret);
		goto free_available_fmt;
	}

	ret = sof_update_ipc_object(scomp, ipc4_copier,
				    SOF_DAI_TOKENS, swidget->tuples,
				    swidget->num_tuples, sizeof(u32), 1);
	if (ret) {
		dev_err(scomp->dev, "parse dai copier node token failed %d\n", ret);
		goto free_available_fmt;
	}

	dev_dbg(scomp->dev, "dai %s node_type %u dai_type %u dai_index %d\n", swidget->widget->name,
		node_type, ipc4_copier->dai_type, ipc4_copier->dai_index);

	dai->type = ipc4_copier->dai_type;
	ipc4_copier->data.gtw_cfg.node_id = SOF_IPC4_NODE_TYPE(node_type);

	pipe_widget = swidget->spipe->pipe_widget;
	pipeline = pipe_widget->private;

	if (pipeline->use_chain_dma &&
	    !snd_sof_is_chain_dma_supported(sdev, ipc4_copier->dai_type)) {
		dev_err(scomp->dev, "Bad DAI type '%d', Chain DMA is not supported\n",
			ipc4_copier->dai_type);
		ret = -ENODEV;
		goto free_available_fmt;
	}

	switch (ipc4_copier->dai_type) {
	case SOF_DAI_INTEL_ALH:
	{
		struct sof_ipc4_alh_configuration_blob *blob;
		struct snd_soc_dapm_path *p;
		struct snd_sof_widget *w;
		int src_num = 0;

		snd_soc_dapm_widget_for_each_source_path(swidget->widget, p)
			src_num++;

		if (swidget->id == snd_soc_dapm_dai_in && src_num == 0) {
			/*
			 * The blob will not be used if the ALH copier is playback direction
			 * and doesn't connect to any source.
			 * It is fine to call kfree(ipc4_copier->copier_config) since
			 * ipc4_copier->copier_config is null.
			 */
			ret = 0;
			break;
		}

		blob = kzalloc(sizeof(*blob), GFP_KERNEL);
		if (!blob) {
			ret = -ENOMEM;
			goto free_available_fmt;
		}

		list_for_each_entry(w, &sdev->widget_list, list) {
			if (w->widget->sname &&
			    strcmp(w->widget->sname, swidget->widget->sname))
				continue;

			blob->alh_cfg.device_count++;
		}

		ipc4_copier->copier_config = (uint32_t *)blob;
		/* set data.gtw_cfg.config_length based on device_count */
		ipc4_copier->data.gtw_cfg.config_length = (sizeof(blob->gw_attr) +
							   sizeof(blob->alh_cfg.device_count) +
							   sizeof(*blob->alh_cfg.mapping) *
							   blob->alh_cfg.device_count) >> 2;
		break;
	}
	case SOF_DAI_INTEL_SSP:
		/* set SSP DAI index as the node_id */
		ipc4_copier->data.gtw_cfg.node_id |=
			SOF_IPC4_NODE_INDEX_INTEL_SSP(ipc4_copier->dai_index);
		break;
	case SOF_DAI_INTEL_DMIC:
		/* set DMIC DAI index as the node_id */
		ipc4_copier->data.gtw_cfg.node_id |=
			SOF_IPC4_NODE_INDEX_INTEL_DMIC(ipc4_copier->dai_index);
		break;
	default:
		ipc4_copier->gtw_attr = kzalloc(sizeof(*ipc4_copier->gtw_attr), GFP_KERNEL);
		if (!ipc4_copier->gtw_attr) {
			ret = -ENOMEM;
			goto free_available_fmt;
		}

		ipc4_copier->copier_config = (uint32_t *)ipc4_copier->gtw_attr;
		ipc4_copier->data.gtw_cfg.config_length =
			sizeof(struct sof_ipc4_gtw_attributes) >> 2;
		break;
	}

	dai->scomp = scomp;
	dai->private = ipc4_copier;

	/* set up module info and message header */
	ret = sof_ipc4_widget_setup_msg(swidget, &ipc4_copier->msg);
	if (ret)
		goto free_copier_config;

	return 0;

free_copier_config:
	kfree(ipc4_copier->copier_config);
free_available_fmt:
	sof_ipc4_free_audio_fmt(available_fmt);
free_copier:
	kfree(ipc4_copier);
	dai->private = NULL;
	dai->scomp = NULL;
	return ret;
}

static void sof_ipc4_widget_free_comp_dai(struct snd_sof_widget *swidget)
{
	struct sof_ipc4_available_audio_format *available_fmt;
	struct snd_sof_dai *dai = swidget->private;
	struct sof_ipc4_copier *ipc4_copier;

	if (!dai)
		return;

	if (!dai->private) {
		kfree(dai);
		swidget->private = NULL;
		return;
	}

	ipc4_copier = dai->private;
	available_fmt = &ipc4_copier->available_fmt;

	kfree(available_fmt->output_pin_fmts);
	if (ipc4_copier->dai_type != SOF_DAI_INTEL_SSP &&
	    ipc4_copier->dai_type != SOF_DAI_INTEL_DMIC)
		kfree(ipc4_copier->copier_config);
	kfree(dai->private);
	kfree(dai);
	swidget->private = NULL;
}

static int sof_ipc4_widget_setup_comp_pipeline(struct snd_sof_widget *swidget)
{
	struct snd_soc_component *scomp = swidget->scomp;
	struct sof_ipc4_pipeline *pipeline;
	struct snd_sof_pipeline *spipe = swidget->spipe;
	int ret;

	pipeline = kzalloc(sizeof(*pipeline), GFP_KERNEL);
	if (!pipeline)
		return -ENOMEM;

	ret = sof_update_ipc_object(scomp, pipeline, SOF_SCHED_TOKENS, swidget->tuples,
				    swidget->num_tuples, sizeof(*pipeline), 1);
	if (ret) {
		dev_err(scomp->dev, "parsing scheduler tokens failed\n");
		goto err;
	}

	swidget->core = pipeline->core_id;
	spipe->core_mask |= BIT(pipeline->core_id);

	if (pipeline->use_chain_dma) {
		dev_dbg(scomp->dev, "Set up chain DMA for %s\n", swidget->widget->name);
		swidget->private = pipeline;
		return 0;
	}

	/* parse one set of pipeline tokens */
	ret = sof_update_ipc_object(scomp, swidget, SOF_PIPELINE_TOKENS, swidget->tuples,
				    swidget->num_tuples, sizeof(*swidget), 1);
	if (ret) {
		dev_err(scomp->dev, "parsing pipeline tokens failed\n");
		goto err;
	}

	dev_dbg(scomp->dev, "pipeline '%s': id %d, pri %d, core_id %u, lp mode %d\n",
		swidget->widget->name, swidget->pipeline_id,
		pipeline->priority, pipeline->core_id, pipeline->lp_mode);

	swidget->private = pipeline;

	pipeline->msg.primary = SOF_IPC4_GLB_PIPE_PRIORITY(pipeline->priority);
	pipeline->msg.primary |= SOF_IPC4_MSG_TYPE_SET(SOF_IPC4_GLB_CREATE_PIPELINE);
	pipeline->msg.primary |= SOF_IPC4_MSG_DIR(SOF_IPC4_MSG_REQUEST);
	pipeline->msg.primary |= SOF_IPC4_MSG_TARGET(SOF_IPC4_FW_GEN_MSG);

	pipeline->msg.extension = pipeline->lp_mode;
	pipeline->msg.extension |= SOF_IPC4_GLB_PIPE_EXT_CORE_ID(pipeline->core_id);
	pipeline->state = SOF_IPC4_PIPE_UNINITIALIZED;

	return 0;
err:
	kfree(pipeline);
	return ret;
}

static int sof_ipc4_widget_setup_comp_pga(struct snd_sof_widget *swidget)
{
	struct snd_soc_component *scomp = swidget->scomp;
	struct sof_ipc4_gain *gain;
	int ret;

	gain = kzalloc(sizeof(*gain), GFP_KERNEL);
	if (!gain)
		return -ENOMEM;

	swidget->private = gain;

	gain->data.params.channels = SOF_IPC4_GAIN_ALL_CHANNELS_MASK;
	gain->data.params.init_val = SOF_IPC4_VOL_ZERO_DB;

	ret = sof_ipc4_get_audio_fmt(scomp, swidget, &gain->available_fmt, &gain->data.base_config);
	if (ret)
		goto err;

	ret = sof_update_ipc_object(scomp, &gain->data.params, SOF_GAIN_TOKENS,
				    swidget->tuples, swidget->num_tuples, sizeof(gain->data), 1);
	if (ret) {
		dev_err(scomp->dev, "Parsing gain tokens failed\n");
		goto err;
	}

	dev_dbg(scomp->dev,
		"pga widget %s: ramp type: %d, ramp duration %d, initial gain value: %#x\n",
		swidget->widget->name, gain->data.params.curve_type,
		gain->data.params.curve_duration_l, gain->data.params.init_val);

	ret = sof_ipc4_widget_setup_msg(swidget, &gain->msg);
	if (ret)
		goto err;

	sof_ipc4_widget_update_kcontrol_module_id(swidget);

	return 0;
err:
	sof_ipc4_free_audio_fmt(&gain->available_fmt);
	kfree(gain);
	swidget->private = NULL;
	return ret;
}

static void sof_ipc4_widget_free_comp_pga(struct snd_sof_widget *swidget)
{
	struct sof_ipc4_gain *gain = swidget->private;

	if (!gain)
		return;

	sof_ipc4_free_audio_fmt(&gain->available_fmt);
	kfree(swidget->private);
	swidget->private = NULL;
}

static int sof_ipc4_widget_setup_comp_mixer(struct snd_sof_widget *swidget)
{
	struct snd_soc_component *scomp = swidget->scomp;
	struct sof_ipc4_mixer *mixer;
	int ret;

	dev_dbg(scomp->dev, "Updating IPC structure for %s\n", swidget->widget->name);

	mixer = kzalloc(sizeof(*mixer), GFP_KERNEL);
	if (!mixer)
		return -ENOMEM;

	swidget->private = mixer;

	ret = sof_ipc4_get_audio_fmt(scomp, swidget, &mixer->available_fmt,
				     &mixer->base_config);
	if (ret)
		goto err;

	ret = sof_ipc4_widget_setup_msg(swidget, &mixer->msg);
	if (ret)
		goto err;

	return 0;
err:
	sof_ipc4_free_audio_fmt(&mixer->available_fmt);
	kfree(mixer);
	swidget->private = NULL;
	return ret;
}

static int sof_ipc4_widget_setup_comp_src(struct snd_sof_widget *swidget)
{
	struct snd_soc_component *scomp = swidget->scomp;
	struct snd_sof_pipeline *spipe = swidget->spipe;
	struct sof_ipc4_src *src;
	int ret;

	dev_dbg(scomp->dev, "Updating IPC structure for %s\n", swidget->widget->name);

	src = kzalloc(sizeof(*src), GFP_KERNEL);
	if (!src)
		return -ENOMEM;

	swidget->private = src;

	ret = sof_ipc4_get_audio_fmt(scomp, swidget, &src->available_fmt,
				     &src->data.base_config);
	if (ret)
		goto err;

	ret = sof_update_ipc_object(scomp, &src->data, SOF_SRC_TOKENS, swidget->tuples,
				    swidget->num_tuples, sizeof(*src), 1);
	if (ret) {
		dev_err(scomp->dev, "Parsing SRC tokens failed\n");
		goto err;
	}

	spipe->core_mask |= BIT(swidget->core);

	dev_dbg(scomp->dev, "SRC sink rate %d\n", src->data.sink_rate);

	ret = sof_ipc4_widget_setup_msg(swidget, &src->msg);
	if (ret)
		goto err;

	return 0;
err:
	sof_ipc4_free_audio_fmt(&src->available_fmt);
	kfree(src);
	swidget->private = NULL;
	return ret;
}

static void sof_ipc4_widget_free_comp_src(struct snd_sof_widget *swidget)
{
	struct sof_ipc4_src *src = swidget->private;

	if (!src)
		return;

	sof_ipc4_free_audio_fmt(&src->available_fmt);
	kfree(swidget->private);
	swidget->private = NULL;
}

static void sof_ipc4_widget_free_comp_mixer(struct snd_sof_widget *swidget)
{
	struct sof_ipc4_mixer *mixer = swidget->private;

	if (!mixer)
		return;

	sof_ipc4_free_audio_fmt(&mixer->available_fmt);
	kfree(swidget->private);
	swidget->private = NULL;
}

/*
 * Add the process modules support. The process modules are defined as snd_soc_dapm_effect modules.
 */
static int sof_ipc4_widget_setup_comp_process(struct snd_sof_widget *swidget)
{
	struct snd_soc_component *scomp = swidget->scomp;
	struct sof_ipc4_fw_module *fw_module;
	struct snd_sof_pipeline *spipe = swidget->spipe;
	struct sof_ipc4_process *process;
	void *cfg;
	int ret;

	process = kzalloc(sizeof(*process), GFP_KERNEL);
	if (!process)
		return -ENOMEM;

	swidget->private = process;

	ret = sof_ipc4_get_audio_fmt(scomp, swidget, &process->available_fmt,
				     &process->base_config);
	if (ret)
		goto err;

	ret = sof_ipc4_widget_setup_msg(swidget, &process->msg);
	if (ret)
		goto err;

	/* parse process init module payload config type from module info */
	fw_module = swidget->module_info;
	process->init_config = FIELD_GET(SOF_IPC4_MODULE_INIT_CONFIG_MASK,
					 fw_module->man4_module_entry.type);

	process->ipc_config_size = sizeof(struct sof_ipc4_base_module_cfg);

	/* allocate memory for base config extension if needed */
	if (process->init_config == SOF_IPC4_MODULE_INIT_CONFIG_TYPE_BASE_CFG_WITH_EXT) {
		struct sof_ipc4_base_module_cfg_ext *base_cfg_ext;
		u32 ext_size = struct_size(base_cfg_ext, pin_formats,
					   size_add(swidget->num_input_pins,
						    swidget->num_output_pins));

		base_cfg_ext = kzalloc(ext_size, GFP_KERNEL);
		if (!base_cfg_ext) {
			ret = -ENOMEM;
			goto free_available_fmt;
		}

		base_cfg_ext->num_input_pin_fmts = swidget->num_input_pins;
		base_cfg_ext->num_output_pin_fmts = swidget->num_output_pins;
		process->base_config_ext = base_cfg_ext;
		process->base_config_ext_size = ext_size;
		process->ipc_config_size += ext_size;
	}

	cfg = kzalloc(process->ipc_config_size, GFP_KERNEL);
	if (!cfg) {
		ret = -ENOMEM;
		goto free_base_cfg_ext;
	}

	process->ipc_config_data = cfg;

	sof_ipc4_widget_update_kcontrol_module_id(swidget);

	/* set pipeline core mask to keep track of the core the module is scheduled to run on */
	spipe->core_mask |= BIT(swidget->core);

	return 0;
free_base_cfg_ext:
	kfree(process->base_config_ext);
	process->base_config_ext = NULL;
free_available_fmt:
	sof_ipc4_free_audio_fmt(&process->available_fmt);
err:
	kfree(process);
	swidget->private = NULL;
	return ret;
}

static void sof_ipc4_widget_free_comp_process(struct snd_sof_widget *swidget)
{
	struct sof_ipc4_process *process = swidget->private;

	if (!process)
		return;

	kfree(process->ipc_config_data);
	kfree(process->base_config_ext);
	sof_ipc4_free_audio_fmt(&process->available_fmt);
	kfree(swidget->private);
	swidget->private = NULL;
}

static void
sof_ipc4_update_resource_usage(struct snd_sof_dev *sdev, struct snd_sof_widget *swidget,
			       struct sof_ipc4_base_module_cfg *base_config)
{
	struct sof_ipc4_fw_module *fw_module = swidget->module_info;
	struct snd_sof_widget *pipe_widget;
	struct sof_ipc4_pipeline *pipeline;
	int task_mem, queue_mem;
	int ibs, bss, total;

	ibs = base_config->ibs;
	bss = base_config->is_pages;

	task_mem = SOF_IPC4_PIPELINE_OBJECT_SIZE;
	task_mem += SOF_IPC4_MODULE_INSTANCE_LIST_ITEM_SIZE + bss;

	if (fw_module->man4_module_entry.type & SOF_IPC4_MODULE_LL) {
		task_mem += SOF_IPC4_FW_ROUNDUP(SOF_IPC4_LL_TASK_OBJECT_SIZE);
		task_mem += SOF_IPC4_FW_MAX_QUEUE_COUNT * SOF_IPC4_MODULE_INSTANCE_LIST_ITEM_SIZE;
		task_mem += SOF_IPC4_LL_TASK_LIST_ITEM_SIZE;
	} else {
		task_mem += SOF_IPC4_FW_ROUNDUP(SOF_IPC4_DP_TASK_OBJECT_SIZE);
		task_mem += SOF_IPC4_DP_TASK_LIST_SIZE;
	}

	ibs = SOF_IPC4_FW_ROUNDUP(ibs);
	queue_mem = SOF_IPC4_FW_MAX_QUEUE_COUNT * (SOF_IPC4_DATA_QUEUE_OBJECT_SIZE +  ibs);

	total = SOF_IPC4_FW_PAGE(task_mem + queue_mem);

	pipe_widget = swidget->spipe->pipe_widget;
	pipeline = pipe_widget->private;
	pipeline->mem_usage += total;

	/* Update base_config->cpc from the module manifest */
	sof_ipc4_update_cpc_from_manifest(sdev, fw_module, base_config);

	if (ignore_cpc) {
		dev_dbg(sdev->dev, "%s: ibs / obs: %u / %u, forcing cpc to 0 from %u\n",
			swidget->widget->name, base_config->ibs, base_config->obs,
			base_config->cpc);
		base_config->cpc = 0;
	} else {
		dev_dbg(sdev->dev, "%s: ibs / obs / cpc: %u / %u / %u\n",
			swidget->widget->name, base_config->ibs, base_config->obs,
			base_config->cpc);
	}
}

static int sof_ipc4_widget_assign_instance_id(struct snd_sof_dev *sdev,
					      struct snd_sof_widget *swidget)
{
	struct sof_ipc4_fw_module *fw_module = swidget->module_info;
	int max_instances = fw_module->man4_module_entry.instance_max_count;

	swidget->instance_id = ida_alloc_max(&fw_module->m_ida, max_instances, GFP_KERNEL);
	if (swidget->instance_id < 0) {
		dev_err(sdev->dev, "failed to assign instance id for widget %s",
			swidget->widget->name);
		return swidget->instance_id;
	}

	return 0;
}

/* update hw_params based on the audio stream format */
static int sof_ipc4_update_hw_params(struct snd_sof_dev *sdev, struct snd_pcm_hw_params *params,
				     struct sof_ipc4_audio_format *fmt, u32 param_to_update)
{
	struct snd_interval *i;

	if (param_to_update & BIT(SNDRV_PCM_HW_PARAM_FORMAT)) {
		int valid_bits = SOF_IPC4_AUDIO_FORMAT_CFG_V_BIT_DEPTH(fmt->fmt_cfg);
		snd_pcm_format_t snd_fmt;
		struct snd_mask *m;

		switch (valid_bits) {
		case 16:
			snd_fmt = SNDRV_PCM_FORMAT_S16_LE;
			break;
		case 24:
			snd_fmt = SNDRV_PCM_FORMAT_S24_LE;
			break;
		case 32:
			snd_fmt = SNDRV_PCM_FORMAT_S32_LE;
			break;
		default:
			dev_err(sdev->dev, "invalid PCM valid_bits %d\n", valid_bits);
			return -EINVAL;
		}

		m = hw_param_mask(params, SNDRV_PCM_HW_PARAM_FORMAT);
		snd_mask_none(m);
		snd_mask_set_format(m, snd_fmt);
	}

	if (param_to_update & BIT(SNDRV_PCM_HW_PARAM_RATE)) {
		unsigned int rate = fmt->sampling_frequency;

		i = hw_param_interval(params, SNDRV_PCM_HW_PARAM_RATE);
		i->min = rate;
		i->max = rate;
	}

	if (param_to_update & BIT(SNDRV_PCM_HW_PARAM_CHANNELS)) {
		unsigned int channels = SOF_IPC4_AUDIO_FORMAT_CFG_CHANNELS_COUNT(fmt->fmt_cfg);

		i = hw_param_interval(params, SNDRV_PCM_HW_PARAM_CHANNELS);
		i->min = channels;
		i->max = channels;
	}

	return 0;
}

static bool sof_ipc4_is_single_format(struct snd_sof_dev *sdev,
				      struct sof_ipc4_pin_format *pin_fmts, u32 pin_fmts_size)
{
	struct sof_ipc4_audio_format *fmt;
	u32 rate, channels, valid_bits;
	int i;

	fmt = &pin_fmts[0].audio_fmt;
	rate = fmt->sampling_frequency;
	channels = SOF_IPC4_AUDIO_FORMAT_CFG_CHANNELS_COUNT(fmt->fmt_cfg);
	valid_bits = SOF_IPC4_AUDIO_FORMAT_CFG_V_BIT_DEPTH(fmt->fmt_cfg);

	/* check if all output formats in topology are the same */
	for (i = 1; i < pin_fmts_size; i++) {
		u32 _rate, _channels, _valid_bits;

		fmt = &pin_fmts[i].audio_fmt;
		_rate = fmt->sampling_frequency;
		_channels = SOF_IPC4_AUDIO_FORMAT_CFG_CHANNELS_COUNT(fmt->fmt_cfg);
		_valid_bits = SOF_IPC4_AUDIO_FORMAT_CFG_V_BIT_DEPTH(fmt->fmt_cfg);

		if (_rate != rate || _channels != channels || _valid_bits != valid_bits)
			return false;
	}

	return true;
}

static int sof_ipc4_init_output_audio_fmt(struct snd_sof_dev *sdev,
					  struct sof_ipc4_base_module_cfg *base_config,
					  struct sof_ipc4_available_audio_format *available_fmt,
					  u32 out_ref_rate, u32 out_ref_channels,
					  u32 out_ref_valid_bits)
{
	struct sof_ipc4_audio_format *out_fmt;
	bool single_format;
	int i;

	if (!available_fmt->num_output_formats)
		return -EINVAL;

	single_format = sof_ipc4_is_single_format(sdev, available_fmt->output_pin_fmts,
						  available_fmt->num_output_formats);

	/* pick the first format if there's only one available or if all formats are the same */
	if (single_format) {
		base_config->obs = available_fmt->output_pin_fmts[0].buffer_size;
		return 0;
	}

	/*
	 * if there are multiple output formats, then choose the output format that matches
	 * the reference params
	 */
	for (i = 0; i < available_fmt->num_output_formats; i++) {
		u32 _out_rate, _out_channels, _out_valid_bits;

		out_fmt = &available_fmt->output_pin_fmts[i].audio_fmt;
		_out_rate = out_fmt->sampling_frequency;
		_out_channels = SOF_IPC4_AUDIO_FORMAT_CFG_CHANNELS_COUNT(out_fmt->fmt_cfg);
		_out_valid_bits = SOF_IPC4_AUDIO_FORMAT_CFG_V_BIT_DEPTH(out_fmt->fmt_cfg);

		if (_out_rate == out_ref_rate && _out_channels == out_ref_channels &&
		    _out_valid_bits == out_ref_valid_bits) {
			base_config->obs = available_fmt->output_pin_fmts[i].buffer_size;
			return i;
		}
	}

	return -EINVAL;
}

static int sof_ipc4_get_valid_bits(struct snd_sof_dev *sdev, struct snd_pcm_hw_params *params)
{
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		return 16;
	case SNDRV_PCM_FORMAT_S24_LE:
		return 24;
	case SNDRV_PCM_FORMAT_S32_LE:
		return 32;
	default:
		dev_err(sdev->dev, "invalid pcm frame format %d\n", params_format(params));
		return -EINVAL;
	}
}

static int sof_ipc4_init_input_audio_fmt(struct snd_sof_dev *sdev,
					 struct snd_sof_widget *swidget,
					 struct sof_ipc4_base_module_cfg *base_config,
					 struct snd_pcm_hw_params *params,
					 struct sof_ipc4_available_audio_format *available_fmt)
{
	struct sof_ipc4_pin_format *pin_fmts = available_fmt->input_pin_fmts;
	u32 pin_fmts_size = available_fmt->num_input_formats;
	u32 valid_bits;
	u32 channels;
	u32 rate;
	bool single_format;
	int sample_valid_bits;
	int i = 0;

	if (!available_fmt->num_input_formats) {
		dev_err(sdev->dev, "no input formats for %s\n", swidget->widget->name);
		return -EINVAL;
	}

	single_format = sof_ipc4_is_single_format(sdev, available_fmt->input_pin_fmts,
						  available_fmt->num_input_formats);
	if (single_format)
		goto in_fmt;

	sample_valid_bits = sof_ipc4_get_valid_bits(sdev, params);
	if (sample_valid_bits < 0)
		return sample_valid_bits;

	/*
	 * Search supported input audio formats with pin index 0 to match rate, channels and
	 * sample_valid_bits from reference params
	 */
	for (i = 0; i < pin_fmts_size; i++) {
		struct sof_ipc4_audio_format *fmt = &pin_fmts[i].audio_fmt;

		if (pin_fmts[i].pin_index)
			continue;

		rate = fmt->sampling_frequency;
		channels = SOF_IPC4_AUDIO_FORMAT_CFG_CHANNELS_COUNT(fmt->fmt_cfg);
		valid_bits = SOF_IPC4_AUDIO_FORMAT_CFG_V_BIT_DEPTH(fmt->fmt_cfg);
		if (params_rate(params) == rate && params_channels(params) == channels &&
		    sample_valid_bits == valid_bits) {
			dev_dbg(sdev->dev, "matched audio format index for %uHz, %ubit, %u channels: %d\n",
				rate, valid_bits, channels, i);
			break;
		}
	}

	if (i == pin_fmts_size) {
		dev_err(sdev->dev, "%s: Unsupported audio format: %uHz, %ubit, %u channels\n",
			__func__, params_rate(params), sample_valid_bits, params_channels(params));
		return -EINVAL;
	}

in_fmt:
	/* copy input format */
	if (available_fmt->num_input_formats && i < available_fmt->num_input_formats) {
		memcpy(&base_config->audio_fmt, &available_fmt->input_pin_fmts[i].audio_fmt,
		       sizeof(struct sof_ipc4_audio_format));

		/* set base_cfg ibs/obs */
		base_config->ibs = available_fmt->input_pin_fmts[i].buffer_size;

		dev_dbg(sdev->dev, "Init input audio formats for %s\n", swidget->widget->name);
		sof_ipc4_dbg_audio_format(sdev->dev, &available_fmt->input_pin_fmts[i], 1);
	}

	return i;
}

static void sof_ipc4_unprepare_copier_module(struct snd_sof_widget *swidget)
{
	struct sof_ipc4_copier *ipc4_copier = NULL;
	struct snd_sof_widget *pipe_widget;
	struct sof_ipc4_pipeline *pipeline;

	/* reset pipeline memory usage */
	pipe_widget = swidget->spipe->pipe_widget;
	pipeline = pipe_widget->private;
	pipeline->mem_usage = 0;

	if (WIDGET_IS_AIF(swidget->id) || swidget->id == snd_soc_dapm_buffer) {
		if (pipeline->use_chain_dma) {
			pipeline->msg.primary = 0;
			pipeline->msg.extension = 0;
		}
		ipc4_copier = swidget->private;
	} else if (WIDGET_IS_DAI(swidget->id)) {
		struct snd_sof_dai *dai = swidget->private;

		ipc4_copier = dai->private;

		if (pipeline->use_chain_dma) {
			/*
			 * Preserve the DMA Link ID and clear other bits since
			 * the DMA Link ID is only configured once during
			 * dai_config, other fields are expected to be 0 for
			 * re-configuration
			 */
			pipeline->msg.primary &= SOF_IPC4_GLB_CHAIN_DMA_LINK_ID_MASK;
			pipeline->msg.extension = 0;
		}

		if (ipc4_copier->dai_type == SOF_DAI_INTEL_ALH) {
			struct sof_ipc4_alh_configuration_blob *blob;
			unsigned int group_id;

			blob = (struct sof_ipc4_alh_configuration_blob *)ipc4_copier->copier_config;
			if (blob->alh_cfg.device_count > 1) {
				group_id = SOF_IPC4_NODE_INDEX(ipc4_copier->data.gtw_cfg.node_id) -
					   ALH_MULTI_GTW_BASE;
				ida_free(&alh_group_ida, group_id);
			}
		}
	}

	if (ipc4_copier) {
		kfree(ipc4_copier->ipc_config_data);
		ipc4_copier->ipc_config_data = NULL;
		ipc4_copier->ipc_config_size = 0;
	}
}

#if IS_ENABLED(CONFIG_ACPI) && IS_ENABLED(CONFIG_SND_INTEL_NHLT)
static int snd_sof_get_hw_config_params(struct snd_sof_dev *sdev, struct snd_sof_dai *dai,
					int *sample_rate, int *channel_count, int *bit_depth)
{
	struct snd_soc_tplg_hw_config *hw_config;
	struct snd_sof_dai_link *slink;
	bool dai_link_found = false;
	bool hw_cfg_found = false;
	int i;

	/* get current hw_config from link */
	list_for_each_entry(slink, &sdev->dai_link_list, list) {
		if (!strcmp(slink->link->name, dai->name)) {
			dai_link_found = true;
			break;
		}
	}

	if (!dai_link_found) {
		dev_err(sdev->dev, "%s: no DAI link found for DAI %s\n", __func__, dai->name);
		return -EINVAL;
	}

	for (i = 0; i < slink->num_hw_configs; i++) {
		hw_config = &slink->hw_configs[i];
		if (dai->current_config == le32_to_cpu(hw_config->id)) {
			hw_cfg_found = true;
			break;
		}
	}

	if (!hw_cfg_found) {
		dev_err(sdev->dev, "%s: no matching hw_config found for DAI %s\n", __func__,
			dai->name);
		return -EINVAL;
	}

	*bit_depth = le32_to_cpu(hw_config->tdm_slot_width);
	*channel_count = le32_to_cpu(hw_config->tdm_slots);
	*sample_rate = le32_to_cpu(hw_config->fsync_rate);

	dev_dbg(sdev->dev, "sample rate: %d sample width: %d channels: %d\n",
		*sample_rate, *bit_depth, *channel_count);

	return 0;
}

static int
snd_sof_get_nhlt_endpoint_data(struct snd_sof_dev *sdev, struct snd_sof_dai *dai,
			       bool single_bitdepth,
			       struct snd_pcm_hw_params *params, u32 dai_index,
			       u32 linktype, u8 dir, u32 **dst, u32 *len)
{
	struct sof_ipc4_fw_data *ipc4_data = sdev->private;
	struct nhlt_specific_cfg *cfg;
	int sample_rate, channel_count;
	bool format_change = false;
	int bit_depth, ret;
	u32 nhlt_type;
	int dev_type = 0;

	/* convert to NHLT type */
	switch (linktype) {
	case SOF_DAI_INTEL_DMIC:
		nhlt_type = NHLT_LINK_DMIC;
		channel_count = params_channels(params);
		sample_rate = params_rate(params);
		bit_depth = params_width(params);
		/*
		 * Look for 32-bit blob first instead of 16-bit if copier
		 * supports multiple formats
		 */
		if (bit_depth == 16 && !single_bitdepth) {
			dev_dbg(sdev->dev, "Looking for 32-bit blob first for DMIC\n");
			format_change = true;
			bit_depth = 32;
		}
		break;
	case SOF_DAI_INTEL_SSP:
		nhlt_type = NHLT_LINK_SSP;
		ret = snd_sof_get_hw_config_params(sdev, dai, &sample_rate, &channel_count,
						   &bit_depth);
		if (ret < 0)
			return ret;

		/*
		 * We need to know the type of the external device attached to a SSP
		 * port to retrieve the blob from NHLT. However, device type is not
		 * specified in topology.
		 * Query the type for the port and then pass that information back
		 * to the blob lookup function.
		 */
		dev_type = intel_nhlt_ssp_device_type(sdev->dev, ipc4_data->nhlt,
						      dai_index);
		if (dev_type < 0)
			return dev_type;
		break;
	default:
		return 0;
	}

	dev_dbg(sdev->dev, "dai index %d nhlt type %d direction %d dev type %d\n",
		dai_index, nhlt_type, dir, dev_type);

	/* find NHLT blob with matching params */
	cfg = intel_nhlt_get_endpoint_blob(sdev->dev, ipc4_data->nhlt, dai_index, nhlt_type,
					   bit_depth, bit_depth, channel_count, sample_rate,
					   dir, dev_type);

	if (!cfg) {
		bool get_new_blob = false;

		if (format_change) {
			/*
			 * The 32-bit blob was not found in NHLT table, try to
			 * look for one based on the params
			 */
			bit_depth = params_width(params);
			format_change = false;
			get_new_blob = true;
		} else if (linktype == SOF_DAI_INTEL_DMIC && !single_bitdepth) {
			/*
			 * The requested 32-bit blob (no format change for the
			 * blob request) was not found in NHLT table, try to
			 * look for 16-bit blob if the copier supports multiple
			 * formats
			 */
			bit_depth = 16;
			format_change = true;
			get_new_blob = true;
		}

		if (get_new_blob) {
			cfg = intel_nhlt_get_endpoint_blob(sdev->dev, ipc4_data->nhlt,
							   dai_index, nhlt_type,
							   bit_depth, bit_depth,
							   channel_count, sample_rate,
							   dir, dev_type);
			if (cfg)
				goto out;
		}

		dev_err(sdev->dev,
			"no matching blob for sample rate: %d sample width: %d channels: %d\n",
			sample_rate, bit_depth, channel_count);
		return -EINVAL;
	}

out:
	/* config length should be in dwords */
	*len = cfg->size >> 2;
	*dst = (u32 *)cfg->caps;

	if (format_change) {
		/*
		 * Update the params to reflect that different blob was loaded
		 * instead of the requested bit depth (16 -> 32 or 32 -> 16).
		 * This information is going to be used by the caller to find
		 * matching copier format on the dai side.
		 */
		struct snd_mask *m;

		m = hw_param_mask(params, SNDRV_PCM_HW_PARAM_FORMAT);
		snd_mask_none(m);
		if (bit_depth == 16)
			snd_mask_set_format(m, SNDRV_PCM_FORMAT_S16_LE);
		else
			snd_mask_set_format(m, SNDRV_PCM_FORMAT_S32_LE);

	}

	return 0;
}
#else
static int
snd_sof_get_nhlt_endpoint_data(struct snd_sof_dev *sdev, struct snd_sof_dai *dai,
			       bool single_bitdepth,
			       struct snd_pcm_hw_params *params, u32 dai_index,
			       u32 linktype, u8 dir, u32 **dst, u32 *len)
{
	return 0;
}
#endif

bool sof_ipc4_copier_is_single_bitdepth(struct snd_sof_dev *sdev,
					struct sof_ipc4_pin_format *pin_fmts,
					u32 pin_fmts_size)
{
	struct sof_ipc4_audio_format *fmt;
	u32 valid_bits;
	int i;

	fmt = &pin_fmts[0].audio_fmt;
	valid_bits = SOF_IPC4_AUDIO_FORMAT_CFG_V_BIT_DEPTH(fmt->fmt_cfg);

	/* check if all formats in topology are the same */
	for (i = 1; i < pin_fmts_size; i++) {
		u32 _valid_bits;

		fmt = &pin_fmts[i].audio_fmt;
		_valid_bits = SOF_IPC4_AUDIO_FORMAT_CFG_V_BIT_DEPTH(fmt->fmt_cfg);

		if (_valid_bits != valid_bits)
			return false;
	}

	return true;
}

static int
sof_ipc4_adjust_params_to_dai_format(struct snd_sof_dev *sdev,
				     struct snd_pcm_hw_params *params,
				     struct sof_ipc4_pin_format *pin_fmts,
				     u32 pin_fmts_size)
{
	u32 params_mask = BIT(SNDRV_PCM_HW_PARAM_RATE) |
			  BIT(SNDRV_PCM_HW_PARAM_CHANNELS) |
			  BIT(SNDRV_PCM_HW_PARAM_FORMAT);
	struct sof_ipc4_audio_format *fmt;
	u32 rate, channels, valid_bits;
	int i;

	fmt = &pin_fmts[0].audio_fmt;
	rate = fmt->sampling_frequency;
	channels = SOF_IPC4_AUDIO_FORMAT_CFG_CHANNELS_COUNT(fmt->fmt_cfg);
	valid_bits = SOF_IPC4_AUDIO_FORMAT_CFG_V_BIT_DEPTH(fmt->fmt_cfg);

	/* check if parameters in topology defined formats are the same */
	for (i = 1; i < pin_fmts_size; i++) {
		u32 val;

		fmt = &pin_fmts[i].audio_fmt;

		if (params_mask & BIT(SNDRV_PCM_HW_PARAM_RATE)) {
			val = fmt->sampling_frequency;
			if (val != rate)
				params_mask &= ~BIT(SNDRV_PCM_HW_PARAM_RATE);
		}
		if (params_mask & BIT(SNDRV_PCM_HW_PARAM_CHANNELS)) {
			val = SOF_IPC4_AUDIO_FORMAT_CFG_CHANNELS_COUNT(fmt->fmt_cfg);
			if (val != channels)
				params_mask &= ~BIT(SNDRV_PCM_HW_PARAM_CHANNELS);
		}
		if (params_mask & BIT(SNDRV_PCM_HW_PARAM_FORMAT)) {
			val = SOF_IPC4_AUDIO_FORMAT_CFG_V_BIT_DEPTH(fmt->fmt_cfg);
			if (val != valid_bits)
				params_mask &= ~BIT(SNDRV_PCM_HW_PARAM_FORMAT);
		}
	}

	if (params_mask)
		return sof_ipc4_update_hw_params(sdev, params,
						 &pin_fmts[0].audio_fmt,
						 params_mask);

	return 0;
}

static int
sof_ipc4_prepare_dai_copier(struct snd_sof_dev *sdev, struct snd_sof_dai *dai,
			    struct snd_pcm_hw_params *params, int dir)
{
	struct sof_ipc4_available_audio_format *available_fmt;
	struct snd_pcm_hw_params dai_params = *params;
	struct sof_ipc4_copier_data *copier_data;
	struct sof_ipc4_pin_format *pin_fmts;
	struct sof_ipc4_copier *ipc4_copier;
	bool single_bitdepth;
	u32 num_pin_fmts;
	int ret;

	ipc4_copier = dai->private;
	copier_data = &ipc4_copier->data;
	available_fmt = &ipc4_copier->available_fmt;

	/*
	 * Fixup the params based on the format parameters of the DAI. If any
	 * of the RATE, CHANNELS, bit depth is static among the formats then
	 * narrow the params to only allow that specific parameter value.
	 */
	if (dir == SNDRV_PCM_STREAM_PLAYBACK) {
		pin_fmts = available_fmt->output_pin_fmts;
		num_pin_fmts = available_fmt->num_output_formats;
	} else {
		pin_fmts = available_fmt->input_pin_fmts;
		num_pin_fmts = available_fmt->num_input_formats;
	}

	ret = sof_ipc4_adjust_params_to_dai_format(sdev, &dai_params, pin_fmts,
						   num_pin_fmts);
	if (ret)
		return ret;

	single_bitdepth = sof_ipc4_copier_is_single_bitdepth(sdev, pin_fmts,
							     num_pin_fmts);
	ret = snd_sof_get_nhlt_endpoint_data(sdev, dai, single_bitdepth,
					     &dai_params,
					     ipc4_copier->dai_index,
					     ipc4_copier->dai_type, dir,
					     &ipc4_copier->copier_config,
					     &copier_data->gtw_cfg.config_length);
	/* Update the params to reflect the changes made in this function */
	if (!ret)
		*params = dai_params;

	return ret;
}

static int
sof_ipc4_prepare_copier_module(struct snd_sof_widget *swidget,
			       struct snd_pcm_hw_params *fe_params,
			       struct snd_sof_platform_stream_params *platform_params,
			       struct snd_pcm_hw_params *pipeline_params, int dir)
{
	struct sof_ipc4_available_audio_format *available_fmt;
	struct snd_soc_component *scomp = swidget->scomp;
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	struct sof_ipc4_copier_data *copier_data;
	struct snd_pcm_hw_params ref_params;
	struct sof_ipc4_copier *ipc4_copier;
	struct snd_sof_dai *dai;
	u32 gtw_cfg_config_length;
	u32 dma_config_tlv_size = 0;
	void **ipc_config_data;
	int *ipc_config_size;
	u32 **data;
	int ipc_size, ret, out_ref_valid_bits;
	u32 out_ref_rate, out_ref_channels;
	u32 deep_buffer_dma_ms = 0;
	int output_fmt_index;
	bool single_output_bitdepth;
	int i;

	dev_dbg(sdev->dev, "copier %s, type %d", swidget->widget->name, swidget->id);

	switch (swidget->id) {
	case snd_soc_dapm_aif_in:
	case snd_soc_dapm_aif_out:
	{
		struct sof_ipc4_gtw_attributes *gtw_attr;
		struct snd_sof_widget *pipe_widget;
		struct sof_ipc4_pipeline *pipeline;

		/* parse the deep buffer dma size */
		ret = sof_update_ipc_object(scomp, &deep_buffer_dma_ms,
					    SOF_COPIER_DEEP_BUFFER_TOKENS, swidget->tuples,
					    swidget->num_tuples, sizeof(u32), 1);
		if (ret) {
			dev_err(scomp->dev, "Failed to parse deep buffer dma size for %s\n",
				swidget->widget->name);
			return ret;
		}

		ipc4_copier = (struct sof_ipc4_copier *)swidget->private;
		gtw_attr = ipc4_copier->gtw_attr;
		copier_data = &ipc4_copier->data;
		available_fmt = &ipc4_copier->available_fmt;

		pipe_widget = swidget->spipe->pipe_widget;
		pipeline = pipe_widget->private;

		if (pipeline->use_chain_dma) {
			u32 host_dma_id;
			u32 fifo_size;

			host_dma_id = platform_params->stream_tag - 1;
			pipeline->msg.primary |= SOF_IPC4_GLB_CHAIN_DMA_HOST_ID(host_dma_id);

			/* Set SCS bit for S16_LE format only */
			if (params_format(fe_params) == SNDRV_PCM_FORMAT_S16_LE)
				pipeline->msg.primary |= SOF_IPC4_GLB_CHAIN_DMA_SCS_MASK;

			/*
			 * Despite its name the bitfield 'fifo_size' is used to define DMA buffer
			 * size. The expression calculates 2ms buffer size.
			 */
			fifo_size = DIV_ROUND_UP((SOF_IPC4_CHAIN_DMA_BUF_SIZE_MS *
						  params_rate(fe_params) *
						  params_channels(fe_params) *
						  params_physical_width(fe_params)), 8000);
			pipeline->msg.extension |= SOF_IPC4_GLB_EXT_CHAIN_DMA_FIFO_SIZE(fifo_size);

			/*
			 * Chain DMA does not support stream timestamping, set node_id to invalid
			 * to skip the code in sof_ipc4_get_stream_start_offset().
			 */
			copier_data->gtw_cfg.node_id = SOF_IPC4_INVALID_NODE_ID;

			return 0;
		}

		/*
		 * Use the input_pin_fmts to match pcm params for playback and the output_pin_fmts
		 * for capture.
		 */
		if (dir == SNDRV_PCM_STREAM_PLAYBACK)
			ref_params = *fe_params;
		else
			ref_params = *pipeline_params;

		copier_data->gtw_cfg.node_id &= ~SOF_IPC4_NODE_INDEX_MASK;
		copier_data->gtw_cfg.node_id |=
			SOF_IPC4_NODE_INDEX(platform_params->stream_tag - 1);

		/* set gateway attributes */
		gtw_attr->lp_buffer_alloc = pipeline->lp_mode;
		break;
	}
	case snd_soc_dapm_dai_in:
	case snd_soc_dapm_dai_out:
	{
		struct snd_sof_widget *pipe_widget = swidget->spipe->pipe_widget;
		struct sof_ipc4_pipeline *pipeline = pipe_widget->private;

		if (pipeline->use_chain_dma)
			return 0;

		dai = swidget->private;

		ipc4_copier = (struct sof_ipc4_copier *)dai->private;
		copier_data = &ipc4_copier->data;
		available_fmt = &ipc4_copier->available_fmt;

		/*
		 * Use the fe_params as a base for the copier configuration.
		 * The ref_params might get updated to reflect what format is
		 * supported by the copier on the DAI side.
		 *
		 * In case of capture the ref_params returned will be used to
		 * find the input configuration of the copier.
		 */
		ref_params = *fe_params;
		ret = sof_ipc4_prepare_dai_copier(sdev, dai, &ref_params, dir);
		if (ret < 0)
			return ret;

		/*
		 * For playback the pipeline_params needs to be used to find the
		 * input configuration of the copier.
		 */
		if (dir == SNDRV_PCM_STREAM_PLAYBACK)
			ref_params = *pipeline_params;

		break;
	}
	case snd_soc_dapm_buffer:
	{
		ipc4_copier = (struct sof_ipc4_copier *)swidget->private;
		copier_data = &ipc4_copier->data;
		available_fmt = &ipc4_copier->available_fmt;
		ref_params = *pipeline_params;

		break;
	}
	default:
		dev_err(sdev->dev, "unsupported type %d for copier %s",
			swidget->id, swidget->widget->name);
		return -EINVAL;
	}

	/* set input and output audio formats */
	ret = sof_ipc4_init_input_audio_fmt(sdev, swidget, &copier_data->base_config,
					    &ref_params, available_fmt);
	if (ret < 0)
		return ret;

	/* set the reference params for output format selection */
	single_output_bitdepth = sof_ipc4_copier_is_single_bitdepth(sdev,
					available_fmt->output_pin_fmts,
					available_fmt->num_output_formats);
	switch (swidget->id) {
	case snd_soc_dapm_aif_in:
	case snd_soc_dapm_dai_out:
	case snd_soc_dapm_buffer:
	{
		struct sof_ipc4_audio_format *in_fmt;

		in_fmt = &available_fmt->input_pin_fmts[ret].audio_fmt;
		out_ref_rate = in_fmt->sampling_frequency;
		out_ref_channels = SOF_IPC4_AUDIO_FORMAT_CFG_CHANNELS_COUNT(in_fmt->fmt_cfg);

		if (!single_output_bitdepth)
			out_ref_valid_bits =
				SOF_IPC4_AUDIO_FORMAT_CFG_V_BIT_DEPTH(in_fmt->fmt_cfg);
		break;
	}
	case snd_soc_dapm_aif_out:
	case snd_soc_dapm_dai_in:
		out_ref_rate = params_rate(fe_params);
		out_ref_channels = params_channels(fe_params);
		if (!single_output_bitdepth) {
			out_ref_valid_bits = sof_ipc4_get_valid_bits(sdev, fe_params);
			if (out_ref_valid_bits < 0)
				return out_ref_valid_bits;
		}
		break;
	default:
		/*
		 * Unsupported type should be caught by the former switch default
		 * case, this should never happen in reality.
		 */
		return -EINVAL;
	}

	/*
	 * if the output format is the same across all available output formats, choose
	 * that as the reference.
	 */
	if (single_output_bitdepth) {
		struct sof_ipc4_audio_format *out_fmt;

		out_fmt = &available_fmt->output_pin_fmts[0].audio_fmt;
		out_ref_valid_bits =
			SOF_IPC4_AUDIO_FORMAT_CFG_V_BIT_DEPTH(out_fmt->fmt_cfg);
	}

	dev_dbg(sdev->dev, "copier %s: reference output rate %d, channels %d valid_bits %d\n",
		swidget->widget->name, out_ref_rate, out_ref_channels, out_ref_valid_bits);

	output_fmt_index = sof_ipc4_init_output_audio_fmt(sdev, &copier_data->base_config,
							  available_fmt, out_ref_rate,
							  out_ref_channels, out_ref_valid_bits);
	if (output_fmt_index < 0) {
		dev_err(sdev->dev, "Failed to initialize output format for %s",
			swidget->widget->name);
		return output_fmt_index;
	}

	/*
	 * Set the output format. Current topology defines pin 0 input and output formats in pairs.
	 * This assumes that the pin 0 formats are defined before all other pins.
	 * So pick the output audio format with the same index as the chosen
	 * input format. This logic will need to be updated when the format definitions
	 * in topology change.
	 */
	memcpy(&copier_data->out_format,
	       &available_fmt->output_pin_fmts[output_fmt_index].audio_fmt,
	       sizeof(struct sof_ipc4_audio_format));
	dev_dbg(sdev->dev, "Output audio format for %s\n", swidget->widget->name);
	sof_ipc4_dbg_audio_format(sdev->dev, &available_fmt->output_pin_fmts[output_fmt_index], 1);

	switch (swidget->id) {
	case snd_soc_dapm_dai_in:
	case snd_soc_dapm_dai_out:
	{
		/*
		 * Only SOF_DAI_INTEL_ALH needs copier_data to set blob.
		 * That's why only ALH dai's blob is set after sof_ipc4_init_input_audio_fmt
		 */
		if (ipc4_copier->dai_type == SOF_DAI_INTEL_ALH) {
			struct sof_ipc4_alh_configuration_blob *blob;
			struct sof_ipc4_dma_config *dma_config;
			struct sof_ipc4_copier_data *alh_data;
			struct sof_ipc4_copier *alh_copier;
			struct snd_sof_widget *w;
			u32 ch_count = 0;
			u32 ch_mask = 0;
			u32 ch_map;
			u32 step;
			u32 mask;

			blob = (struct sof_ipc4_alh_configuration_blob *)ipc4_copier->copier_config;

			blob->gw_attr.lp_buffer_alloc = 0;

			/* Get channel_mask from ch_map */
			ch_map = copier_data->base_config.audio_fmt.ch_map;
			for (i = 0; ch_map; i++) {
				if ((ch_map & 0xf) != 0xf) {
					ch_mask |= BIT(i);
					ch_count++;
				}
				ch_map >>= 4;
			}

			step = ch_count / blob->alh_cfg.device_count;
			mask =  GENMASK(step - 1, 0);
			/*
			 * Set each gtw_cfg.node_id to blob->alh_cfg.mapping[]
			 * for all widgets with the same stream name
			 */
			i = 0;
			list_for_each_entry(w, &sdev->widget_list, list) {
				u32 node_type;

				if (w->widget->sname &&
				    strcmp(w->widget->sname, swidget->widget->sname))
					continue;

				dai = w->private;
				alh_copier = (struct sof_ipc4_copier *)dai->private;
				alh_data = &alh_copier->data;
				node_type = SOF_IPC4_GET_NODE_TYPE(alh_data->gtw_cfg.node_id);
				blob->alh_cfg.mapping[i].device = SOF_IPC4_NODE_TYPE(node_type);
				blob->alh_cfg.mapping[i].device |=
					SOF_IPC4_NODE_INDEX(alh_copier->dai_index);

				/*
				 * The mapping[i] device in ALH blob should be the same as the
				 * dma_config_tlv[i] mapping device if a dma_config_tlv is present.
				 * The device id will be used for DMA tlv mapping purposes.
				 */
				if (ipc4_copier->dma_config_tlv[i].length) {
					dma_config = &ipc4_copier->dma_config_tlv[i].dma_config;
					blob->alh_cfg.mapping[i].device =
						dma_config->dma_stream_channel_map.mapping[0].device;
				}

				/*
				 * Set the same channel mask for playback as the audio data is
				 * duplicated for all speakers. For capture, split the channels
				 * among the aggregated DAIs. For example, with 4 channels on 2
				 * aggregated DAIs, the channel_mask should be 0x3 and 0xc for the
				 * two DAI's.
				 * The channel masks used depend on the cpu_dais used in the
				 * dailink at the machine driver level, which actually comes from
				 * the tables in soc_acpi files depending on the _ADR and devID
				 * registers for each codec.
				 */
				if (w->id == snd_soc_dapm_dai_in)
					blob->alh_cfg.mapping[i].channel_mask = ch_mask;
				else
					blob->alh_cfg.mapping[i].channel_mask = mask << (step * i);

				i++;
			}
			if (blob->alh_cfg.device_count > 1) {
				int group_id;

				group_id = ida_alloc_max(&alh_group_ida, ALH_MULTI_GTW_COUNT - 1,
							 GFP_KERNEL);

				if (group_id < 0)
					return group_id;

				/* add multi-gateway base */
				group_id += ALH_MULTI_GTW_BASE;
				copier_data->gtw_cfg.node_id &= ~SOF_IPC4_NODE_INDEX_MASK;
				copier_data->gtw_cfg.node_id |= SOF_IPC4_NODE_INDEX(group_id);
			}
		}
	}
	}

	/* modify the input params for the next widget */
	ret = sof_ipc4_update_hw_params(sdev, pipeline_params,
					&copier_data->out_format,
					BIT(SNDRV_PCM_HW_PARAM_FORMAT) |
					BIT(SNDRV_PCM_HW_PARAM_CHANNELS) |
					BIT(SNDRV_PCM_HW_PARAM_RATE));
	if (ret)
		return ret;

	/*
	 * Set the gateway dma_buffer_size to 2ms buffer size to meet the FW expectation. In the
	 * deep buffer case, set the dma_buffer_size depending on the deep_buffer_dma_ms set
	 * in topology.
	 */
	switch (swidget->id) {
	case snd_soc_dapm_dai_in:
		copier_data->gtw_cfg.dma_buffer_size =
			SOF_IPC4_MIN_DMA_BUFFER_SIZE * copier_data->base_config.ibs;
		break;
	case snd_soc_dapm_aif_in:
		copier_data->gtw_cfg.dma_buffer_size =
			max((u32)SOF_IPC4_MIN_DMA_BUFFER_SIZE, deep_buffer_dma_ms) *
				copier_data->base_config.ibs;
		dev_dbg(sdev->dev, "copier %s, dma buffer%s: %u ms (%u bytes)",
			swidget->widget->name,
			deep_buffer_dma_ms ? " (using Deep Buffer)" : "",
			max((u32)SOF_IPC4_MIN_DMA_BUFFER_SIZE, deep_buffer_dma_ms),
			copier_data->gtw_cfg.dma_buffer_size);
		break;
	case snd_soc_dapm_dai_out:
	case snd_soc_dapm_aif_out:
		copier_data->gtw_cfg.dma_buffer_size =
			SOF_IPC4_MIN_DMA_BUFFER_SIZE * copier_data->base_config.obs;
		break;
	default:
		break;
	}

	data = &ipc4_copier->copier_config;
	ipc_config_size = &ipc4_copier->ipc_config_size;
	ipc_config_data = &ipc4_copier->ipc_config_data;

	/* config_length is DWORD based */
	gtw_cfg_config_length = copier_data->gtw_cfg.config_length * 4;
	ipc_size = sizeof(*copier_data) + gtw_cfg_config_length;

	dma_config_tlv_size = 0;
	for (i = 0; i < SOF_IPC4_DMA_DEVICE_MAX_COUNT; i++) {
		if (ipc4_copier->dma_config_tlv[i].type != SOF_IPC4_GTW_DMA_CONFIG_ID)
			continue;
		dma_config_tlv_size += ipc4_copier->dma_config_tlv[i].length;
		dma_config_tlv_size +=
			ipc4_copier->dma_config_tlv[i].dma_config.dma_priv_config_size;
		dma_config_tlv_size += (sizeof(ipc4_copier->dma_config_tlv[i]) -
			sizeof(ipc4_copier->dma_config_tlv[i].dma_config));
	}

	if (dma_config_tlv_size) {
		ipc_size += dma_config_tlv_size;

		/* we also need to increase the size at the gtw level */
		copier_data->gtw_cfg.config_length += dma_config_tlv_size / 4;
	}

	dev_dbg(sdev->dev, "copier %s, IPC size is %d", swidget->widget->name, ipc_size);

	*ipc_config_data = kzalloc(ipc_size, GFP_KERNEL);
	if (!*ipc_config_data)
		return -ENOMEM;

	*ipc_config_size = ipc_size;

	/* update pipeline memory usage */
	sof_ipc4_update_resource_usage(sdev, swidget, &copier_data->base_config);

	/* copy IPC data */
	memcpy(*ipc_config_data, (void *)copier_data, sizeof(*copier_data));
	if (gtw_cfg_config_length)
		memcpy(*ipc_config_data + sizeof(*copier_data),
		       *data, gtw_cfg_config_length);

	/* add DMA Config TLV, if configured */
	if (dma_config_tlv_size)
		memcpy(*ipc_config_data + sizeof(*copier_data) +
		       gtw_cfg_config_length,
		       &ipc4_copier->dma_config_tlv, dma_config_tlv_size);

	/*
	 * Restore gateway config length now that IPC payload is prepared. This avoids
	 * counting the DMA CONFIG TLV multiple times
	 */
	copier_data->gtw_cfg.config_length = gtw_cfg_config_length / 4;

	return 0;
}

static int sof_ipc4_prepare_gain_module(struct snd_sof_widget *swidget,
					struct snd_pcm_hw_params *fe_params,
					struct snd_sof_platform_stream_params *platform_params,
					struct snd_pcm_hw_params *pipeline_params, int dir)
{
	struct snd_soc_component *scomp = swidget->scomp;
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	struct sof_ipc4_gain *gain = swidget->private;
	struct sof_ipc4_available_audio_format *available_fmt = &gain->available_fmt;
	struct sof_ipc4_audio_format *in_fmt;
	u32 out_ref_rate, out_ref_channels, out_ref_valid_bits;
	int ret;

	ret = sof_ipc4_init_input_audio_fmt(sdev, swidget, &gain->data.base_config,
					    pipeline_params, available_fmt);
	if (ret < 0)
		return ret;

	in_fmt = &available_fmt->input_pin_fmts[ret].audio_fmt;
	out_ref_rate = in_fmt->sampling_frequency;
	out_ref_channels = SOF_IPC4_AUDIO_FORMAT_CFG_CHANNELS_COUNT(in_fmt->fmt_cfg);
	out_ref_valid_bits = SOF_IPC4_AUDIO_FORMAT_CFG_V_BIT_DEPTH(in_fmt->fmt_cfg);

	ret = sof_ipc4_init_output_audio_fmt(sdev, &gain->data.base_config, available_fmt,
					     out_ref_rate, out_ref_channels, out_ref_valid_bits);
	if (ret < 0) {
		dev_err(sdev->dev, "Failed to initialize output format for %s",
			swidget->widget->name);
		return ret;
	}

	/* update pipeline memory usage */
	sof_ipc4_update_resource_usage(sdev, swidget, &gain->data.base_config);

	return 0;
}

static int sof_ipc4_prepare_mixer_module(struct snd_sof_widget *swidget,
					 struct snd_pcm_hw_params *fe_params,
					 struct snd_sof_platform_stream_params *platform_params,
					 struct snd_pcm_hw_params *pipeline_params, int dir)
{
	struct snd_soc_component *scomp = swidget->scomp;
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	struct sof_ipc4_mixer *mixer = swidget->private;
	struct sof_ipc4_available_audio_format *available_fmt = &mixer->available_fmt;
	struct sof_ipc4_audio_format *in_fmt;
	u32 out_ref_rate, out_ref_channels, out_ref_valid_bits;
	int ret;

	ret = sof_ipc4_init_input_audio_fmt(sdev, swidget, &mixer->base_config,
					    pipeline_params, available_fmt);
	if (ret < 0)
		return ret;

	in_fmt = &available_fmt->input_pin_fmts[ret].audio_fmt;
	out_ref_rate = in_fmt->sampling_frequency;
	out_ref_channels = SOF_IPC4_AUDIO_FORMAT_CFG_CHANNELS_COUNT(in_fmt->fmt_cfg);
	out_ref_valid_bits = SOF_IPC4_AUDIO_FORMAT_CFG_V_BIT_DEPTH(in_fmt->fmt_cfg);

	ret = sof_ipc4_init_output_audio_fmt(sdev, &mixer->base_config, available_fmt,
					     out_ref_rate, out_ref_channels, out_ref_valid_bits);
	if (ret < 0) {
		dev_err(sdev->dev, "Failed to initialize output format for %s",
			swidget->widget->name);
		return ret;
	}

	/* update pipeline memory usage */
	sof_ipc4_update_resource_usage(sdev, swidget, &mixer->base_config);

	return 0;
}

static int sof_ipc4_prepare_src_module(struct snd_sof_widget *swidget,
				       struct snd_pcm_hw_params *fe_params,
				       struct snd_sof_platform_stream_params *platform_params,
				       struct snd_pcm_hw_params *pipeline_params, int dir)
{
	struct snd_soc_component *scomp = swidget->scomp;
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	struct sof_ipc4_src *src = swidget->private;
	struct sof_ipc4_available_audio_format *available_fmt = &src->available_fmt;
	struct sof_ipc4_audio_format *out_audio_fmt;
	struct sof_ipc4_audio_format *in_audio_fmt;
	u32 out_ref_rate, out_ref_channels, out_ref_valid_bits;
	int output_format_index, input_format_index;

	input_format_index = sof_ipc4_init_input_audio_fmt(sdev, swidget, &src->data.base_config,
							   pipeline_params, available_fmt);
	if (input_format_index < 0)
		return input_format_index;

	/*
	 * For playback, the SRC sink rate will be configured based on the requested output
	 * format, which is restricted to only deal with DAI's with a single format for now.
	 */
	if (dir == SNDRV_PCM_STREAM_PLAYBACK && available_fmt->num_output_formats > 1) {
		dev_err(sdev->dev, "Invalid number of output formats: %d for SRC %s\n",
			available_fmt->num_output_formats, swidget->widget->name);
		return -EINVAL;
	}

	/*
	 * SRC does not perform format conversion, so the output channels and valid bit depth must
	 * be the same as that of the input.
	 */
	in_audio_fmt = &available_fmt->input_pin_fmts[input_format_index].audio_fmt;
	out_ref_channels = SOF_IPC4_AUDIO_FORMAT_CFG_CHANNELS_COUNT(in_audio_fmt->fmt_cfg);
	out_ref_valid_bits = SOF_IPC4_AUDIO_FORMAT_CFG_V_BIT_DEPTH(in_audio_fmt->fmt_cfg);

	/*
	 * For capture, the SRC module should convert the rate to match the rate requested by the
	 * PCM hw_params. Set the reference params based on the fe_params unconditionally as it
	 * will be ignored for playback anyway.
	 */
	out_ref_rate = params_rate(fe_params);

	output_format_index = sof_ipc4_init_output_audio_fmt(sdev, &src->data.base_config,
							     available_fmt, out_ref_rate,
							     out_ref_channels, out_ref_valid_bits);
	if (output_format_index < 0) {
		dev_err(sdev->dev, "Failed to initialize output format for %s",
			swidget->widget->name);
		return output_format_index;
	}

	/* update pipeline memory usage */
	sof_ipc4_update_resource_usage(sdev, swidget, &src->data.base_config);

	out_audio_fmt = &available_fmt->output_pin_fmts[output_format_index].audio_fmt;
	src->data.sink_rate = out_audio_fmt->sampling_frequency;

	/* update pipeline_params for sink widgets */
	return sof_ipc4_update_hw_params(sdev, pipeline_params, out_audio_fmt,
					 BIT(SNDRV_PCM_HW_PARAM_FORMAT) |
					 BIT(SNDRV_PCM_HW_PARAM_CHANNELS) |
					 BIT(SNDRV_PCM_HW_PARAM_RATE));
}

static int
sof_ipc4_process_set_pin_formats(struct snd_sof_widget *swidget, int pin_type)
{
	struct sof_ipc4_process *process = swidget->private;
	struct sof_ipc4_base_module_cfg_ext *base_cfg_ext = process->base_config_ext;
	struct sof_ipc4_available_audio_format *available_fmt = &process->available_fmt;
	struct sof_ipc4_pin_format *pin_format, *format_list_to_search;
	struct snd_soc_component *scomp = swidget->scomp;
	int num_pins, format_list_count;
	int pin_format_offset = 0;
	int i, j;

	/* set number of pins, offset of pin format and format list to search based on pin type */
	if (pin_type == SOF_PIN_TYPE_INPUT) {
		num_pins = swidget->num_input_pins;
		format_list_to_search = available_fmt->input_pin_fmts;
		format_list_count = available_fmt->num_input_formats;
	} else {
		num_pins = swidget->num_output_pins;
		pin_format_offset = swidget->num_input_pins;
		format_list_to_search = available_fmt->output_pin_fmts;
		format_list_count = available_fmt->num_output_formats;
	}

	for (i = pin_format_offset; i < num_pins + pin_format_offset; i++) {
		pin_format = &base_cfg_ext->pin_formats[i];

		/* Pin 0 audio formats are derived from the base config input/output format */
		if (i == pin_format_offset) {
			if (pin_type == SOF_PIN_TYPE_INPUT) {
				pin_format->buffer_size = process->base_config.ibs;
				pin_format->audio_fmt = process->base_config.audio_fmt;
			} else {
				pin_format->buffer_size = process->base_config.obs;
				pin_format->audio_fmt = process->output_format;
			}
			continue;
		}

		/*
		 * For all other pins, find the pin formats from those set in topology. If there
		 * is more than one format specified for a pin, this will pick the first available
		 * one.
		 */
		for (j = 0; j < format_list_count; j++) {
			struct sof_ipc4_pin_format *pin_format_item = &format_list_to_search[j];

			if (pin_format_item->pin_index == i - pin_format_offset) {
				*pin_format = *pin_format_item;
				break;
			}
		}

		if (j == format_list_count) {
			dev_err(scomp->dev, "%s pin %d format not found for %s\n",
				(pin_type == SOF_PIN_TYPE_INPUT) ? "input" : "output",
				i - pin_format_offset, swidget->widget->name);
			return -EINVAL;
		}
	}

	return 0;
}

static int sof_ipc4_process_add_base_cfg_extn(struct snd_sof_widget *swidget)
{
	int ret, i;

	/* copy input and output pin formats */
	for (i = 0; i <= SOF_PIN_TYPE_OUTPUT; i++) {
		ret = sof_ipc4_process_set_pin_formats(swidget, i);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int sof_ipc4_prepare_process_module(struct snd_sof_widget *swidget,
					   struct snd_pcm_hw_params *fe_params,
					   struct snd_sof_platform_stream_params *platform_params,
					   struct snd_pcm_hw_params *pipeline_params, int dir)
{
	struct snd_soc_component *scomp = swidget->scomp;
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	struct sof_ipc4_process *process = swidget->private;
	struct sof_ipc4_available_audio_format *available_fmt = &process->available_fmt;
	struct sof_ipc4_audio_format *in_fmt;
	u32 out_ref_rate, out_ref_channels, out_ref_valid_bits;
	void *cfg = process->ipc_config_data;
	int output_fmt_index;
	int ret;

	ret = sof_ipc4_init_input_audio_fmt(sdev, swidget, &process->base_config,
					    pipeline_params, available_fmt);
	if (ret < 0)
		return ret;

	in_fmt = &available_fmt->input_pin_fmts[ret].audio_fmt;
	out_ref_rate = in_fmt->sampling_frequency;
	out_ref_channels = SOF_IPC4_AUDIO_FORMAT_CFG_CHANNELS_COUNT(in_fmt->fmt_cfg);
	out_ref_valid_bits = SOF_IPC4_AUDIO_FORMAT_CFG_V_BIT_DEPTH(in_fmt->fmt_cfg);

	output_fmt_index = sof_ipc4_init_output_audio_fmt(sdev, &process->base_config,
							  available_fmt, out_ref_rate,
							  out_ref_channels, out_ref_valid_bits);
	if (output_fmt_index < 0 && available_fmt->num_output_formats) {
		dev_err(sdev->dev, "Failed to initialize output format for %s",
			swidget->widget->name);
		return output_fmt_index;
	}

	/* copy Pin 0 output format */
	if (available_fmt->num_output_formats &&
	    output_fmt_index < available_fmt->num_output_formats &&
	    !available_fmt->output_pin_fmts[output_fmt_index].pin_index) {
		memcpy(&process->output_format,
		       &available_fmt->output_pin_fmts[output_fmt_index].audio_fmt,
		       sizeof(struct sof_ipc4_audio_format));

		/* modify the pipeline params with the pin 0 output format */
		ret = sof_ipc4_update_hw_params(sdev, pipeline_params,
						&process->output_format,
						BIT(SNDRV_PCM_HW_PARAM_FORMAT) |
						BIT(SNDRV_PCM_HW_PARAM_CHANNELS) |
						BIT(SNDRV_PCM_HW_PARAM_RATE));
		if (ret)
			return ret;
	}

	/* update pipeline memory usage */
	sof_ipc4_update_resource_usage(sdev, swidget, &process->base_config);

	/* ipc_config_data is composed of the base_config followed by an optional extension */
	memcpy(cfg, &process->base_config, sizeof(struct sof_ipc4_base_module_cfg));
	cfg += sizeof(struct sof_ipc4_base_module_cfg);

	if (process->init_config == SOF_IPC4_MODULE_INIT_CONFIG_TYPE_BASE_CFG_WITH_EXT) {
		struct sof_ipc4_base_module_cfg_ext *base_cfg_ext = process->base_config_ext;

		ret = sof_ipc4_process_add_base_cfg_extn(swidget);
		if (ret < 0)
			return ret;

		memcpy(cfg, base_cfg_ext, process->base_config_ext_size);
	}

	return 0;
}

static int sof_ipc4_control_load_volume(struct snd_sof_dev *sdev, struct snd_sof_control *scontrol)
{
	struct sof_ipc4_control_data *control_data;
	struct sof_ipc4_msg *msg;
	int i;

	scontrol->size = struct_size(control_data, chanv, scontrol->num_channels);

	/* scontrol->ipc_control_data will be freed in sof_control_unload */
	scontrol->ipc_control_data = kzalloc(scontrol->size, GFP_KERNEL);
	if (!scontrol->ipc_control_data)
		return -ENOMEM;

	control_data = scontrol->ipc_control_data;
	control_data->index = scontrol->index;

	msg = &control_data->msg;
	msg->primary = SOF_IPC4_MSG_TYPE_SET(SOF_IPC4_MOD_LARGE_CONFIG_SET);
	msg->primary |= SOF_IPC4_MSG_DIR(SOF_IPC4_MSG_REQUEST);
	msg->primary |= SOF_IPC4_MSG_TARGET(SOF_IPC4_MODULE_MSG);

	/* volume controls with range 0-1 (off/on) are switch controls */
	if (scontrol->max == 1)
		msg->extension = SOF_IPC4_MOD_EXT_MSG_PARAM_ID(SOF_IPC4_SWITCH_CONTROL_PARAM_ID);
	else
		msg->extension = SOF_IPC4_MOD_EXT_MSG_PARAM_ID(SOF_IPC4_GAIN_PARAM_ID);

	for (i = 0; i < scontrol->num_channels; i++) {
		control_data->chanv[i].channel = i;
		/*
		 * Default, initial values:
		 * - 0dB for volume controls
		 * - off (0) for switch controls - value already zero after
		 *				   memory allocation
		 */
		if (scontrol->max > 1)
			control_data->chanv[i].value = SOF_IPC4_VOL_ZERO_DB;
	}

	return 0;
}

static int sof_ipc4_control_load_enum(struct snd_sof_dev *sdev, struct snd_sof_control *scontrol)
{
	struct sof_ipc4_control_data *control_data;
	struct sof_ipc4_msg *msg;
	int i;

	scontrol->size = struct_size(control_data, chanv, scontrol->num_channels);

	/* scontrol->ipc_control_data will be freed in sof_control_unload */
	scontrol->ipc_control_data = kzalloc(scontrol->size, GFP_KERNEL);
	if (!scontrol->ipc_control_data)
		return -ENOMEM;

	control_data = scontrol->ipc_control_data;
	control_data->index = scontrol->index;

	msg = &control_data->msg;
	msg->primary = SOF_IPC4_MSG_TYPE_SET(SOF_IPC4_MOD_LARGE_CONFIG_SET);
	msg->primary |= SOF_IPC4_MSG_DIR(SOF_IPC4_MSG_REQUEST);
	msg->primary |= SOF_IPC4_MSG_TARGET(SOF_IPC4_MODULE_MSG);

	msg->extension = SOF_IPC4_MOD_EXT_MSG_PARAM_ID(SOF_IPC4_ENUM_CONTROL_PARAM_ID);

	/* Default, initial value for enums: first enum entry is selected (0) */
	for (i = 0; i < scontrol->num_channels; i++)
		control_data->chanv[i].channel = i;

	return 0;
}

static int sof_ipc4_control_load_bytes(struct snd_sof_dev *sdev, struct snd_sof_control *scontrol)
{
	struct sof_ipc4_control_data *control_data;
	struct sof_ipc4_msg *msg;
	int ret;

	if (scontrol->max_size < (sizeof(*control_data) + sizeof(struct sof_abi_hdr))) {
		dev_err(sdev->dev, "insufficient size for a bytes control %s: %zu.\n",
			scontrol->name, scontrol->max_size);
		return -EINVAL;
	}

	if (scontrol->priv_size > scontrol->max_size - sizeof(*control_data)) {
		dev_err(sdev->dev, "scontrol %s bytes data size %zu exceeds max %zu.\n",
			scontrol->name, scontrol->priv_size,
			scontrol->max_size - sizeof(*control_data));
		return -EINVAL;
	}

	scontrol->size = sizeof(struct sof_ipc4_control_data) + scontrol->priv_size;

	scontrol->ipc_control_data = kzalloc(scontrol->max_size, GFP_KERNEL);
	if (!scontrol->ipc_control_data)
		return -ENOMEM;

	control_data = scontrol->ipc_control_data;
	control_data->index = scontrol->index;
	if (scontrol->priv_size > 0) {
		memcpy(control_data->data, scontrol->priv, scontrol->priv_size);
		kfree(scontrol->priv);
		scontrol->priv = NULL;

		if (control_data->data->magic != SOF_IPC4_ABI_MAGIC) {
			dev_err(sdev->dev, "Wrong ABI magic (%#x) for control: %s\n",
				control_data->data->magic, scontrol->name);
			ret = -EINVAL;
			goto err;
		}

		/* TODO: check the ABI version */

		if (control_data->data->size + sizeof(struct sof_abi_hdr) !=
		    scontrol->priv_size) {
			dev_err(sdev->dev, "Control %s conflict in bytes %zu vs. priv size %zu.\n",
				scontrol->name,
				control_data->data->size + sizeof(struct sof_abi_hdr),
				scontrol->priv_size);
			ret = -EINVAL;
			goto err;
		}
	}

	msg = &control_data->msg;
	msg->primary = SOF_IPC4_MSG_TYPE_SET(SOF_IPC4_MOD_LARGE_CONFIG_SET);
	msg->primary |= SOF_IPC4_MSG_DIR(SOF_IPC4_MSG_REQUEST);
	msg->primary |= SOF_IPC4_MSG_TARGET(SOF_IPC4_MODULE_MSG);

	return 0;

err:
	kfree(scontrol->ipc_control_data);
	scontrol->ipc_control_data = NULL;
	return ret;
}

static int sof_ipc4_control_setup(struct snd_sof_dev *sdev, struct snd_sof_control *scontrol)
{
	switch (scontrol->info_type) {
	case SND_SOC_TPLG_CTL_VOLSW:
	case SND_SOC_TPLG_CTL_VOLSW_SX:
	case SND_SOC_TPLG_CTL_VOLSW_XR_SX:
		return sof_ipc4_control_load_volume(sdev, scontrol);
	case SND_SOC_TPLG_CTL_BYTES:
		return sof_ipc4_control_load_bytes(sdev, scontrol);
	case SND_SOC_TPLG_CTL_ENUM:
	case SND_SOC_TPLG_CTL_ENUM_VALUE:
		return sof_ipc4_control_load_enum(sdev, scontrol);
	default:
		break;
	}

	return 0;
}

static int sof_ipc4_widget_setup(struct snd_sof_dev *sdev, struct snd_sof_widget *swidget)
{
	struct snd_sof_widget *pipe_widget = swidget->spipe->pipe_widget;
	struct sof_ipc4_fw_data *ipc4_data = sdev->private;
	struct sof_ipc4_pipeline *pipeline;
	struct sof_ipc4_msg *msg;
	void *ipc_data = NULL;
	u32 ipc_size = 0;
	int ret;

	switch (swidget->id) {
	case snd_soc_dapm_scheduler:
		pipeline = swidget->private;

		if (pipeline->use_chain_dma) {
			dev_warn(sdev->dev, "use_chain_dma set for scheduler %s",
				 swidget->widget->name);
			return 0;
		}

		dev_dbg(sdev->dev, "pipeline: %d memory pages: %d\n", swidget->pipeline_id,
			pipeline->mem_usage);

		msg = &pipeline->msg;
		msg->primary |= pipeline->mem_usage;

		swidget->instance_id = ida_alloc_max(&pipeline_ida, ipc4_data->max_num_pipelines,
						     GFP_KERNEL);
		if (swidget->instance_id < 0) {
			dev_err(sdev->dev, "failed to assign pipeline id for %s: %d\n",
				swidget->widget->name, swidget->instance_id);
			return swidget->instance_id;
		}
		msg->primary &= ~SOF_IPC4_GLB_PIPE_INSTANCE_MASK;
		msg->primary |= SOF_IPC4_GLB_PIPE_INSTANCE_ID(swidget->instance_id);
		break;
	case snd_soc_dapm_aif_in:
	case snd_soc_dapm_aif_out:
	case snd_soc_dapm_buffer:
	{
		struct sof_ipc4_copier *ipc4_copier = swidget->private;

		pipeline = pipe_widget->private;
		if (pipeline->use_chain_dma)
			return 0;

		ipc_size = ipc4_copier->ipc_config_size;
		ipc_data = ipc4_copier->ipc_config_data;

		msg = &ipc4_copier->msg;
		break;
	}
	case snd_soc_dapm_dai_in:
	case snd_soc_dapm_dai_out:
	{
		struct snd_sof_dai *dai = swidget->private;
		struct sof_ipc4_copier *ipc4_copier = dai->private;

		pipeline = pipe_widget->private;
		if (pipeline->use_chain_dma)
			return 0;

		ipc_size = ipc4_copier->ipc_config_size;
		ipc_data = ipc4_copier->ipc_config_data;

		msg = &ipc4_copier->msg;
		break;
	}
	case snd_soc_dapm_pga:
	{
		struct sof_ipc4_gain *gain = swidget->private;

		ipc_size = sizeof(gain->data);
		ipc_data = &gain->data;

		msg = &gain->msg;
		break;
	}
	case snd_soc_dapm_mixer:
	{
		struct sof_ipc4_mixer *mixer = swidget->private;

		ipc_size = sizeof(mixer->base_config);
		ipc_data = &mixer->base_config;

		msg = &mixer->msg;
		break;
	}
	case snd_soc_dapm_src:
	{
		struct sof_ipc4_src *src = swidget->private;

		ipc_size = sizeof(src->data);
		ipc_data = &src->data;

		msg = &src->msg;
		break;
	}
	case snd_soc_dapm_effect:
	{
		struct sof_ipc4_process *process = swidget->private;

		if (!process->ipc_config_size) {
			dev_err(sdev->dev, "module %s has no config data!\n",
				swidget->widget->name);
			return -EINVAL;
		}

		ipc_size = process->ipc_config_size;
		ipc_data = process->ipc_config_data;

		msg = &process->msg;
		break;
	}
	default:
		dev_err(sdev->dev, "widget type %d not supported", swidget->id);
		return -EINVAL;
	}

	if (swidget->id != snd_soc_dapm_scheduler) {
		int module_id = msg->primary & SOF_IPC4_MOD_ID_MASK;

		ret = sof_ipc4_widget_assign_instance_id(sdev, swidget);
		if (ret < 0) {
			dev_err(sdev->dev, "failed to assign instance id for %s\n",
				swidget->widget->name);
			return ret;
		}

		msg->primary &= ~SOF_IPC4_MOD_INSTANCE_MASK;
		msg->primary |= SOF_IPC4_MOD_INSTANCE(swidget->instance_id);

		msg->extension &= ~SOF_IPC4_MOD_EXT_PARAM_SIZE_MASK;
		msg->extension |= ipc_size >> 2;

		msg->extension &= ~SOF_IPC4_MOD_EXT_PPL_ID_MASK;
		msg->extension |= SOF_IPC4_MOD_EXT_PPL_ID(pipe_widget->instance_id);

		dev_dbg(sdev->dev, "Create widget %s (pipe %d) - ID %d, instance %d, core %d\n",
			swidget->widget->name, swidget->pipeline_id, module_id,
			swidget->instance_id, swidget->core);
	} else {
		dev_dbg(sdev->dev, "Create pipeline %s (pipe %d) - instance %d, core %d\n",
			swidget->widget->name, swidget->pipeline_id,
			swidget->instance_id, swidget->core);
	}

	msg->data_size = ipc_size;
	msg->data_ptr = ipc_data;

	ret = sof_ipc_tx_message_no_reply(sdev->ipc, msg, ipc_size);
	if (ret < 0) {
		dev_err(sdev->dev, "failed to create module %s\n", swidget->widget->name);

		if (swidget->id != snd_soc_dapm_scheduler) {
			struct sof_ipc4_fw_module *fw_module = swidget->module_info;

			ida_free(&fw_module->m_ida, swidget->instance_id);
		} else {
			ida_free(&pipeline_ida, swidget->instance_id);
		}
	}

	return ret;
}

static int sof_ipc4_widget_free(struct snd_sof_dev *sdev, struct snd_sof_widget *swidget)
{
	struct sof_ipc4_fw_module *fw_module = swidget->module_info;
	struct sof_ipc4_fw_data *ipc4_data = sdev->private;
	int ret = 0;

	mutex_lock(&ipc4_data->pipeline_state_mutex);

	/* freeing a pipeline frees all the widgets associated with it */
	if (swidget->id == snd_soc_dapm_scheduler) {
		struct sof_ipc4_pipeline *pipeline = swidget->private;
		struct sof_ipc4_msg msg = {{ 0 }};
		u32 header;

		if (pipeline->use_chain_dma) {
			dev_warn(sdev->dev, "use_chain_dma set for scheduler %s",
				 swidget->widget->name);
			mutex_unlock(&ipc4_data->pipeline_state_mutex);
			return 0;
		}

		header = SOF_IPC4_GLB_PIPE_INSTANCE_ID(swidget->instance_id);
		header |= SOF_IPC4_MSG_TYPE_SET(SOF_IPC4_GLB_DELETE_PIPELINE);
		header |= SOF_IPC4_MSG_DIR(SOF_IPC4_MSG_REQUEST);
		header |= SOF_IPC4_MSG_TARGET(SOF_IPC4_FW_GEN_MSG);

		msg.primary = header;

		ret = sof_ipc_tx_message_no_reply(sdev->ipc, &msg, 0);
		if (ret < 0)
			dev_err(sdev->dev, "failed to free pipeline widget %s\n",
				swidget->widget->name);

		pipeline->mem_usage = 0;
		pipeline->state = SOF_IPC4_PIPE_UNINITIALIZED;
		ida_free(&pipeline_ida, swidget->instance_id);
		swidget->instance_id = -EINVAL;
	} else {
		struct snd_sof_widget *pipe_widget = swidget->spipe->pipe_widget;
		struct sof_ipc4_pipeline *pipeline = pipe_widget->private;

		if (!pipeline->use_chain_dma)
			ida_free(&fw_module->m_ida, swidget->instance_id);
	}

	mutex_unlock(&ipc4_data->pipeline_state_mutex);

	return ret;
}

static int sof_ipc4_get_queue_id(struct snd_sof_widget *src_widget,
				 struct snd_sof_widget *sink_widget, bool pin_type)
{
	struct snd_sof_widget *current_swidget;
	struct snd_soc_component *scomp;
	struct ida *queue_ida;
	const char *buddy_name;
	char **pin_binding;
	u32 num_pins;
	int i;

	if (pin_type == SOF_PIN_TYPE_OUTPUT) {
		current_swidget = src_widget;
		pin_binding = src_widget->output_pin_binding;
		queue_ida = &src_widget->output_queue_ida;
		num_pins = src_widget->num_output_pins;
		buddy_name = sink_widget->widget->name;
	} else {
		current_swidget = sink_widget;
		pin_binding = sink_widget->input_pin_binding;
		queue_ida = &sink_widget->input_queue_ida;
		num_pins = sink_widget->num_input_pins;
		buddy_name = src_widget->widget->name;
	}

	scomp = current_swidget->scomp;

	if (num_pins < 1) {
		dev_err(scomp->dev, "invalid %s num_pins: %d for queue allocation for %s\n",
			(pin_type == SOF_PIN_TYPE_OUTPUT ? "output" : "input"),
			num_pins, current_swidget->widget->name);
		return -EINVAL;
	}

	/* If there is only one input/output pin, queue id must be 0 */
	if (num_pins == 1)
		return 0;

	/* Allocate queue ID from pin binding array if it is defined in topology. */
	if (pin_binding) {
		for (i = 0; i < num_pins; i++) {
			if (!strcmp(pin_binding[i], buddy_name))
				return i;
		}
		/*
		 * Fail if no queue ID found from pin binding array, so that we don't
		 * mixed use pin binding array and ida for queue ID allocation.
		 */
		dev_err(scomp->dev, "no %s queue id found from pin binding array for %s\n",
			(pin_type == SOF_PIN_TYPE_OUTPUT ? "output" : "input"),
			current_swidget->widget->name);
		return -EINVAL;
	}

	/* If no pin binding array specified in topology, use ida to allocate one */
	return ida_alloc_max(queue_ida, num_pins, GFP_KERNEL);
}

static void sof_ipc4_put_queue_id(struct snd_sof_widget *swidget, int queue_id,
				  bool pin_type)
{
	struct ida *queue_ida;
	char **pin_binding;
	int num_pins;

	if (pin_type == SOF_PIN_TYPE_OUTPUT) {
		pin_binding = swidget->output_pin_binding;
		queue_ida = &swidget->output_queue_ida;
		num_pins = swidget->num_output_pins;
	} else {
		pin_binding = swidget->input_pin_binding;
		queue_ida = &swidget->input_queue_ida;
		num_pins = swidget->num_input_pins;
	}

	/* Nothing to free if queue ID is not allocated with ida. */
	if (num_pins == 1 || pin_binding)
		return;

	ida_free(queue_ida, queue_id);
}

static int sof_ipc4_set_copier_sink_format(struct snd_sof_dev *sdev,
					   struct snd_sof_widget *src_widget,
					   struct snd_sof_widget *sink_widget,
					   struct snd_sof_route *sroute)
{
	struct sof_ipc4_copier_config_set_sink_format format;
	const struct sof_ipc_ops *iops = sdev->ipc->ops;
	struct sof_ipc4_base_module_cfg *src_config;
	const struct sof_ipc4_audio_format *pin_fmt;
	struct sof_ipc4_fw_module *fw_module;
	struct sof_ipc4_msg msg = {{ 0 }};

	if (WIDGET_IS_DAI(src_widget->id)) {
		struct snd_sof_dai *dai = src_widget->private;

		src_config = dai->private;
	} else {
		src_config = src_widget->private;
	}

	fw_module = src_widget->module_info;

	format.sink_id = sroute->src_queue_id;
	memcpy(&format.source_fmt, &src_config->audio_fmt, sizeof(format.source_fmt));

	pin_fmt = sof_ipc4_get_input_pin_audio_fmt(sink_widget, sroute->dst_queue_id);
	if (!pin_fmt) {
		dev_err(sdev->dev,
			"Failed to get input audio format of %s:%d for output of %s:%d\n",
			sink_widget->widget->name, sroute->dst_queue_id,
			src_widget->widget->name, sroute->src_queue_id);
		return -EINVAL;
	}

	memcpy(&format.sink_fmt, pin_fmt, sizeof(format.sink_fmt));

	msg.data_size = sizeof(format);
	msg.data_ptr = &format;

	msg.primary = fw_module->man4_module_entry.id;
	msg.primary |= SOF_IPC4_MOD_INSTANCE(src_widget->instance_id);
	msg.primary |= SOF_IPC4_MSG_DIR(SOF_IPC4_MSG_REQUEST);
	msg.primary |= SOF_IPC4_MSG_TARGET(SOF_IPC4_MODULE_MSG);

	msg.extension =
		SOF_IPC4_MOD_EXT_MSG_PARAM_ID(SOF_IPC4_COPIER_MODULE_CFG_PARAM_SET_SINK_FORMAT);

	return iops->set_get_data(sdev, &msg, msg.data_size, true);
}

static int sof_ipc4_route_setup(struct snd_sof_dev *sdev, struct snd_sof_route *sroute)
{
	struct snd_sof_widget *src_widget = sroute->src_widget;
	struct snd_sof_widget *sink_widget = sroute->sink_widget;
	struct snd_sof_widget *src_pipe_widget = src_widget->spipe->pipe_widget;
	struct snd_sof_widget *sink_pipe_widget = sink_widget->spipe->pipe_widget;
	struct sof_ipc4_fw_module *src_fw_module = src_widget->module_info;
	struct sof_ipc4_fw_module *sink_fw_module = sink_widget->module_info;
	struct sof_ipc4_pipeline *src_pipeline = src_pipe_widget->private;
	struct sof_ipc4_pipeline *sink_pipeline = sink_pipe_widget->private;
	struct sof_ipc4_msg msg = {{ 0 }};
	u32 header, extension;
	int ret;

	/* no route set up if chain DMA is used */
	if (src_pipeline->use_chain_dma || sink_pipeline->use_chain_dma) {
		if (!src_pipeline->use_chain_dma || !sink_pipeline->use_chain_dma) {
			dev_err(sdev->dev,
				"use_chain_dma must be set for both src %s and sink %s pipelines\n",
				src_widget->widget->name, sink_widget->widget->name);
			return -EINVAL;
		}
		return 0;
	}

	if (!src_fw_module || !sink_fw_module) {
		dev_err(sdev->dev,
			"cannot bind %s -> %s, no firmware module for: %s%s\n",
			src_widget->widget->name, sink_widget->widget->name,
			src_fw_module ? "" : " source",
			sink_fw_module ? "" : " sink");

		return -ENODEV;
	}

	sroute->src_queue_id = sof_ipc4_get_queue_id(src_widget, sink_widget,
						     SOF_PIN_TYPE_OUTPUT);
	if (sroute->src_queue_id < 0) {
		dev_err(sdev->dev,
			"failed to get src_queue_id ID from source widget %s\n",
			src_widget->widget->name);
		return sroute->src_queue_id;
	}

	sroute->dst_queue_id = sof_ipc4_get_queue_id(src_widget, sink_widget,
						     SOF_PIN_TYPE_INPUT);
	if (sroute->dst_queue_id < 0) {
		dev_err(sdev->dev,
			"failed to get dst_queue_id ID from sink widget %s\n",
			sink_widget->widget->name);
		sof_ipc4_put_queue_id(src_widget, sroute->src_queue_id,
				      SOF_PIN_TYPE_OUTPUT);
		return sroute->dst_queue_id;
	}

	/* Pin 0 format is already set during copier module init */
	if (sroute->src_queue_id > 0 && WIDGET_IS_COPIER(src_widget->id)) {
		ret = sof_ipc4_set_copier_sink_format(sdev, src_widget,
						      sink_widget, sroute);
		if (ret < 0) {
			dev_err(sdev->dev,
				"failed to set sink format for source %s:%d\n",
				src_widget->widget->name, sroute->src_queue_id);
			goto out;
		}
	}

	dev_dbg(sdev->dev, "bind %s:%d -> %s:%d\n",
		src_widget->widget->name, sroute->src_queue_id,
		sink_widget->widget->name, sroute->dst_queue_id);

	header = src_fw_module->man4_module_entry.id;
	header |= SOF_IPC4_MOD_INSTANCE(src_widget->instance_id);
	header |= SOF_IPC4_MSG_TYPE_SET(SOF_IPC4_MOD_BIND);
	header |= SOF_IPC4_MSG_DIR(SOF_IPC4_MSG_REQUEST);
	header |= SOF_IPC4_MSG_TARGET(SOF_IPC4_MODULE_MSG);

	extension = sink_fw_module->man4_module_entry.id;
	extension |= SOF_IPC4_MOD_EXT_DST_MOD_INSTANCE(sink_widget->instance_id);
	extension |= SOF_IPC4_MOD_EXT_DST_MOD_QUEUE_ID(sroute->dst_queue_id);
	extension |= SOF_IPC4_MOD_EXT_SRC_MOD_QUEUE_ID(sroute->src_queue_id);

	msg.primary = header;
	msg.extension = extension;

	ret = sof_ipc_tx_message_no_reply(sdev->ipc, &msg, 0);
	if (ret < 0) {
		dev_err(sdev->dev, "failed to bind modules %s:%d -> %s:%d\n",
			src_widget->widget->name, sroute->src_queue_id,
			sink_widget->widget->name, sroute->dst_queue_id);
		goto out;
	}

	return ret;

out:
	sof_ipc4_put_queue_id(src_widget, sroute->src_queue_id, SOF_PIN_TYPE_OUTPUT);
	sof_ipc4_put_queue_id(sink_widget, sroute->dst_queue_id, SOF_PIN_TYPE_INPUT);
	return ret;
}

static int sof_ipc4_route_free(struct snd_sof_dev *sdev, struct snd_sof_route *sroute)
{
	struct snd_sof_widget *src_widget = sroute->src_widget;
	struct snd_sof_widget *sink_widget = sroute->sink_widget;
	struct sof_ipc4_fw_module *src_fw_module = src_widget->module_info;
	struct sof_ipc4_fw_module *sink_fw_module = sink_widget->module_info;
	struct sof_ipc4_msg msg = {{ 0 }};
	struct snd_sof_widget *src_pipe_widget = src_widget->spipe->pipe_widget;
	struct snd_sof_widget *sink_pipe_widget = sink_widget->spipe->pipe_widget;
	struct sof_ipc4_pipeline *src_pipeline = src_pipe_widget->private;
	struct sof_ipc4_pipeline *sink_pipeline = sink_pipe_widget->private;
	u32 header, extension;
	int ret = 0;

	/* no route is set up if chain DMA is used */
	if (src_pipeline->use_chain_dma || sink_pipeline->use_chain_dma)
		return 0;

	dev_dbg(sdev->dev, "unbind modules %s:%d -> %s:%d\n",
		src_widget->widget->name, sroute->src_queue_id,
		sink_widget->widget->name, sroute->dst_queue_id);

	/*
	 * routes belonging to the same pipeline will be disconnected by the FW when the pipeline
	 * is freed. So avoid sending this IPC which will be ignored by the FW anyway.
	 */
	if (src_widget->spipe->pipe_widget == sink_widget->spipe->pipe_widget)
		goto out;

	header = src_fw_module->man4_module_entry.id;
	header |= SOF_IPC4_MOD_INSTANCE(src_widget->instance_id);
	header |= SOF_IPC4_MSG_TYPE_SET(SOF_IPC4_MOD_UNBIND);
	header |= SOF_IPC4_MSG_DIR(SOF_IPC4_MSG_REQUEST);
	header |= SOF_IPC4_MSG_TARGET(SOF_IPC4_MODULE_MSG);

	extension = sink_fw_module->man4_module_entry.id;
	extension |= SOF_IPC4_MOD_EXT_DST_MOD_INSTANCE(sink_widget->instance_id);
	extension |= SOF_IPC4_MOD_EXT_DST_MOD_QUEUE_ID(sroute->dst_queue_id);
	extension |= SOF_IPC4_MOD_EXT_SRC_MOD_QUEUE_ID(sroute->src_queue_id);

	msg.primary = header;
	msg.extension = extension;

	ret = sof_ipc_tx_message_no_reply(sdev->ipc, &msg, 0);
	if (ret < 0)
		dev_err(sdev->dev, "failed to unbind modules %s:%d -> %s:%d\n",
			src_widget->widget->name, sroute->src_queue_id,
			sink_widget->widget->name, sroute->dst_queue_id);
out:
	sof_ipc4_put_queue_id(sink_widget, sroute->dst_queue_id, SOF_PIN_TYPE_INPUT);
	sof_ipc4_put_queue_id(src_widget, sroute->src_queue_id, SOF_PIN_TYPE_OUTPUT);

	return ret;
}

static int sof_ipc4_dai_config(struct snd_sof_dev *sdev, struct snd_sof_widget *swidget,
			       unsigned int flags, struct snd_sof_dai_config_data *data)
{
	struct snd_sof_widget *pipe_widget = swidget->spipe->pipe_widget;
	struct sof_ipc4_pipeline *pipeline = pipe_widget->private;
	struct snd_sof_dai *dai = swidget->private;
	struct sof_ipc4_gtw_attributes *gtw_attr;
	struct sof_ipc4_copier_data *copier_data;
	struct sof_ipc4_copier *ipc4_copier;

	if (!dai || !dai->private) {
		dev_err(sdev->dev, "Invalid DAI or DAI private data for %s\n",
			swidget->widget->name);
		return -EINVAL;
	}

	ipc4_copier = (struct sof_ipc4_copier *)dai->private;
	copier_data = &ipc4_copier->data;

	if (!data)
		return 0;

	if (pipeline->use_chain_dma) {
		/*
		 * Only configure the DMA Link ID for ChainDMA when this op is
		 * invoked with SOF_DAI_CONFIG_FLAGS_HW_PARAMS
		 */
		if (flags & SOF_DAI_CONFIG_FLAGS_HW_PARAMS) {
			pipeline->msg.primary &= ~SOF_IPC4_GLB_CHAIN_DMA_LINK_ID_MASK;
			pipeline->msg.primary |= SOF_IPC4_GLB_CHAIN_DMA_LINK_ID(data->dai_data);
		}
		return 0;
	}

	switch (ipc4_copier->dai_type) {
	case SOF_DAI_INTEL_HDA:
		gtw_attr = ipc4_copier->gtw_attr;
		gtw_attr->lp_buffer_alloc = pipeline->lp_mode;
		if (flags & SOF_DAI_CONFIG_FLAGS_HW_PARAMS) {
			copier_data->gtw_cfg.node_id &= ~SOF_IPC4_NODE_INDEX_MASK;
			copier_data->gtw_cfg.node_id |= SOF_IPC4_NODE_INDEX(data->dai_data);
		}
		break;
	case SOF_DAI_INTEL_ALH:
		/*
		 * Do not clear the node ID when this op is invoked with
		 * SOF_DAI_CONFIG_FLAGS_HW_FREE. It is needed to free the group_ida during
		 * unprepare. The node_id for multi-gateway DAI's will be overwritten with the
		 * group_id during copier's ipc_prepare op.
		 */
		if (flags & SOF_DAI_CONFIG_FLAGS_HW_PARAMS) {
			struct sof_ipc4_alh_configuration_blob *blob;

			blob = (struct sof_ipc4_alh_configuration_blob *)ipc4_copier->copier_config;
			ipc4_copier->dai_index = data->dai_node_id;

			/*
			 * no need to set the node_id for aggregated DAI's. These will be assigned
			 * a group_id during widget ipc_prepare
			 */
			if (blob->alh_cfg.device_count == 1) {
				copier_data->gtw_cfg.node_id &= ~SOF_IPC4_NODE_INDEX_MASK;
				copier_data->gtw_cfg.node_id |=
					SOF_IPC4_NODE_INDEX(data->dai_node_id);
			}
		}

		break;
	case SOF_DAI_INTEL_DMIC:
	case SOF_DAI_INTEL_SSP:
		/* nothing to do for SSP/DMIC */
		break;
	default:
		dev_err(sdev->dev, "%s: unsupported dai type %d\n", __func__,
			ipc4_copier->dai_type);
		return -EINVAL;
	}

	return 0;
}

static int sof_ipc4_parse_manifest(struct snd_soc_component *scomp, int index,
				   struct snd_soc_tplg_manifest *man)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	struct sof_ipc4_fw_data *ipc4_data = sdev->private;
	struct sof_manifest_tlv *manifest_tlv;
	struct sof_manifest *manifest;
	u32 size = le32_to_cpu(man->priv.size);
	u8 *man_ptr = man->priv.data;
	u32 len_check;
	int i;

	if (!size || size < SOF_IPC4_TPLG_ABI_SIZE) {
		dev_err(scomp->dev, "%s: Invalid topology ABI size: %u\n",
			__func__, size);
		return -EINVAL;
	}

	manifest = (struct sof_manifest *)man_ptr;

	dev_info(scomp->dev,
		 "Topology: ABI %d:%d:%d Kernel ABI %u:%u:%u\n",
		  le16_to_cpu(manifest->abi_major), le16_to_cpu(manifest->abi_minor),
		  le16_to_cpu(manifest->abi_patch),
		  SOF_ABI_MAJOR, SOF_ABI_MINOR, SOF_ABI_PATCH);

	/* TODO: Add ABI compatibility check */

	/* no more data after the ABI version */
	if (size <= SOF_IPC4_TPLG_ABI_SIZE)
		return 0;

	manifest_tlv = manifest->items;
	len_check = sizeof(struct sof_manifest);
	for (i = 0; i < le16_to_cpu(manifest->count); i++) {
		len_check += sizeof(struct sof_manifest_tlv) + le32_to_cpu(manifest_tlv->size);
		if (len_check > size)
			return -EINVAL;

		switch (le32_to_cpu(manifest_tlv->type)) {
		case SOF_MANIFEST_DATA_TYPE_NHLT:
			/* no NHLT in BIOS, so use the one from topology manifest */
			if (ipc4_data->nhlt)
				break;
			ipc4_data->nhlt = devm_kmemdup(sdev->dev, manifest_tlv->data,
						       le32_to_cpu(manifest_tlv->size), GFP_KERNEL);
			if (!ipc4_data->nhlt)
				return -ENOMEM;
			break;
		default:
			dev_warn(scomp->dev, "Skipping unknown manifest data type %d\n",
				 manifest_tlv->type);
			break;
		}
		man_ptr += sizeof(struct sof_manifest_tlv) + le32_to_cpu(manifest_tlv->size);
		manifest_tlv = (struct sof_manifest_tlv *)man_ptr;
	}

	return 0;
}

static int sof_ipc4_dai_get_param(struct snd_sof_dev *sdev, struct snd_sof_dai *dai, int param_type)
{
	struct sof_ipc4_copier *ipc4_copier = dai->private;
	struct snd_soc_tplg_hw_config *hw_config;
	struct snd_sof_dai_link *slink;
	bool dai_link_found = false;
	bool hw_cfg_found = false;
	int i;

	if (!ipc4_copier)
		return 0;

	list_for_each_entry(slink, &sdev->dai_link_list, list) {
		if (!strcmp(slink->link->name, dai->name)) {
			dai_link_found = true;
			break;
		}
	}

	if (!dai_link_found) {
		dev_err(sdev->dev, "no DAI link found for DAI %s\n", dai->name);
		return -EINVAL;
	}

	for (i = 0; i < slink->num_hw_configs; i++) {
		hw_config = &slink->hw_configs[i];
		if (dai->current_config == le32_to_cpu(hw_config->id)) {
			hw_cfg_found = true;
			break;
		}
	}

	if (!hw_cfg_found) {
		dev_err(sdev->dev, "no matching hw_config found for DAI %s\n", dai->name);
		return -EINVAL;
	}

	switch (ipc4_copier->dai_type) {
	case SOF_DAI_INTEL_SSP:
		switch (param_type) {
		case SOF_DAI_PARAM_INTEL_SSP_MCLK:
			return le32_to_cpu(hw_config->mclk_rate);
		case SOF_DAI_PARAM_INTEL_SSP_BCLK:
			return le32_to_cpu(hw_config->bclk_rate);
		case SOF_DAI_PARAM_INTEL_SSP_TDM_SLOTS:
			return le32_to_cpu(hw_config->tdm_slots);
		default:
			dev_err(sdev->dev, "invalid SSP param %d\n", param_type);
			break;
		}
		break;
	default:
		dev_err(sdev->dev, "DAI type %d not supported yet!\n", ipc4_copier->dai_type);
		break;
	}

	return -EINVAL;
}

static int sof_ipc4_tear_down_all_pipelines(struct snd_sof_dev *sdev, bool verify)
{
	struct snd_sof_pcm *spcm;
	int dir, ret;

	/*
	 * This function is called during system suspend, we need to make sure
	 * that all streams have been freed up.
	 * Freeing might have been skipped when xrun happened just at the start
	 * of the suspend and it sent a SNDRV_PCM_TRIGGER_STOP to the active
	 * stream. This will call sof_pcm_stream_free() with
	 * free_widget_list = false which will leave the kernel and firmware out
	 * of sync during suspend/resume.
	 *
	 * This will also make sure that paused streams handled correctly.
	 */
	list_for_each_entry(spcm, &sdev->pcm_list, list) {
		for_each_pcm_streams(dir) {
			struct snd_pcm_substream *substream = spcm->stream[dir].substream;

			if (!substream || !substream->runtime || spcm->stream[dir].suspend_ignored)
				continue;

			if (spcm->stream[dir].list) {
				ret = sof_pcm_stream_free(sdev, substream, spcm, dir, true);
				if (ret < 0)
					return ret;
			}
		}
	}
	return 0;
}

static int sof_ipc4_link_setup(struct snd_sof_dev *sdev, struct snd_soc_dai_link *link)
{
	if (link->no_pcm)
		return 0;

	/*
	 * set default trigger order for all links. Exceptions to
	 * the rule will be handled in sof_pcm_dai_link_fixup()
	 * For playback, the sequence is the following: start BE,
	 * start FE, stop FE, stop BE; for Capture the sequence is
	 * inverted start FE, start BE, stop BE, stop FE
	 */
	link->trigger[SNDRV_PCM_STREAM_PLAYBACK] = SND_SOC_DPCM_TRIGGER_POST;
	link->trigger[SNDRV_PCM_STREAM_CAPTURE] = SND_SOC_DPCM_TRIGGER_PRE;

	return 0;
}

/* Tokens needed for different copier variants (aif, dai and buffer) */
static enum sof_tokens copier_token_list[] = {
	SOF_COMP_TOKENS,
	SOF_COPIER_TOKENS,
	SOF_AUDIO_FMT_NUM_TOKENS,
	SOF_IN_AUDIO_FORMAT_TOKENS,
	SOF_OUT_AUDIO_FORMAT_TOKENS,
	SOF_COMP_EXT_TOKENS,

	SOF_COPIER_DEEP_BUFFER_TOKENS,	/* for AIF copier */
	SOF_DAI_TOKENS,			/* for DAI copier */
};

static enum sof_tokens pipeline_token_list[] = {
	SOF_SCHED_TOKENS,
	SOF_PIPELINE_TOKENS,
};

static enum sof_tokens pga_token_list[] = {
	SOF_COMP_TOKENS,
	SOF_GAIN_TOKENS,
	SOF_AUDIO_FMT_NUM_TOKENS,
	SOF_IN_AUDIO_FORMAT_TOKENS,
	SOF_OUT_AUDIO_FORMAT_TOKENS,
	SOF_COMP_EXT_TOKENS,
};

static enum sof_tokens mixer_token_list[] = {
	SOF_COMP_TOKENS,
	SOF_AUDIO_FMT_NUM_TOKENS,
	SOF_IN_AUDIO_FORMAT_TOKENS,
	SOF_OUT_AUDIO_FORMAT_TOKENS,
	SOF_COMP_EXT_TOKENS,
};

static enum sof_tokens src_token_list[] = {
	SOF_COMP_TOKENS,
	SOF_SRC_TOKENS,
	SOF_AUDIO_FMT_NUM_TOKENS,
	SOF_IN_AUDIO_FORMAT_TOKENS,
	SOF_OUT_AUDIO_FORMAT_TOKENS,
	SOF_COMP_EXT_TOKENS,
};

static enum sof_tokens process_token_list[] = {
	SOF_COMP_TOKENS,
	SOF_AUDIO_FMT_NUM_TOKENS,
	SOF_IN_AUDIO_FORMAT_TOKENS,
	SOF_OUT_AUDIO_FORMAT_TOKENS,
	SOF_COMP_EXT_TOKENS,
};

static const struct sof_ipc_tplg_widget_ops tplg_ipc4_widget_ops[SND_SOC_DAPM_TYPE_COUNT] = {
	[snd_soc_dapm_aif_in] =  {sof_ipc4_widget_setup_pcm, sof_ipc4_widget_free_comp_pcm,
				  copier_token_list, ARRAY_SIZE(copier_token_list),
				  NULL, sof_ipc4_prepare_copier_module,
				  sof_ipc4_unprepare_copier_module},
	[snd_soc_dapm_aif_out] = {sof_ipc4_widget_setup_pcm, sof_ipc4_widget_free_comp_pcm,
				  copier_token_list, ARRAY_SIZE(copier_token_list),
				  NULL, sof_ipc4_prepare_copier_module,
				  sof_ipc4_unprepare_copier_module},
	[snd_soc_dapm_dai_in] = {sof_ipc4_widget_setup_comp_dai, sof_ipc4_widget_free_comp_dai,
				 copier_token_list, ARRAY_SIZE(copier_token_list), NULL,
				 sof_ipc4_prepare_copier_module,
				 sof_ipc4_unprepare_copier_module},
	[snd_soc_dapm_dai_out] = {sof_ipc4_widget_setup_comp_dai, sof_ipc4_widget_free_comp_dai,
				  copier_token_list, ARRAY_SIZE(copier_token_list), NULL,
				  sof_ipc4_prepare_copier_module,
				  sof_ipc4_unprepare_copier_module},
	[snd_soc_dapm_buffer] = {sof_ipc4_widget_setup_pcm, sof_ipc4_widget_free_comp_pcm,
				 copier_token_list, ARRAY_SIZE(copier_token_list),
				 NULL, sof_ipc4_prepare_copier_module,
				 sof_ipc4_unprepare_copier_module},
	[snd_soc_dapm_scheduler] = {sof_ipc4_widget_setup_comp_pipeline,
				    sof_ipc4_widget_free_comp_pipeline,
				    pipeline_token_list, ARRAY_SIZE(pipeline_token_list), NULL,
				    NULL, NULL},
	[snd_soc_dapm_pga] = {sof_ipc4_widget_setup_comp_pga, sof_ipc4_widget_free_comp_pga,
			      pga_token_list, ARRAY_SIZE(pga_token_list), NULL,
			      sof_ipc4_prepare_gain_module,
			      NULL},
	[snd_soc_dapm_mixer] = {sof_ipc4_widget_setup_comp_mixer, sof_ipc4_widget_free_comp_mixer,
				mixer_token_list, ARRAY_SIZE(mixer_token_list),
				NULL, sof_ipc4_prepare_mixer_module,
				NULL},
	[snd_soc_dapm_src] = {sof_ipc4_widget_setup_comp_src, sof_ipc4_widget_free_comp_src,
				src_token_list, ARRAY_SIZE(src_token_list),
				NULL, sof_ipc4_prepare_src_module,
				NULL},
	[snd_soc_dapm_effect] = {sof_ipc4_widget_setup_comp_process,
				sof_ipc4_widget_free_comp_process,
				process_token_list, ARRAY_SIZE(process_token_list),
				NULL, sof_ipc4_prepare_process_module,
				NULL},
};

const struct sof_ipc_tplg_ops ipc4_tplg_ops = {
	.widget = tplg_ipc4_widget_ops,
	.token_list = ipc4_token_list,
	.control_setup = sof_ipc4_control_setup,
	.control = &tplg_ipc4_control_ops,
	.widget_setup = sof_ipc4_widget_setup,
	.widget_free = sof_ipc4_widget_free,
	.route_setup = sof_ipc4_route_setup,
	.route_free = sof_ipc4_route_free,
	.dai_config = sof_ipc4_dai_config,
	.parse_manifest = sof_ipc4_parse_manifest,
	.dai_get_param = sof_ipc4_dai_get_param,
	.tear_down_all_pipelines = sof_ipc4_tear_down_all_pipelines,
	.link_setup = sof_ipc4_link_setup,
};
