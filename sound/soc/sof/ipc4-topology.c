// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2022 Intel Corporation. All rights reserved.
//
//
#include <uapi/sound/sof/tokens.h>
#include <sound/pcm_params.h>
#include <sound/sof/ext_manifest4.h>
#include "sof-priv.h"
#include "sof-audio.h"
#include "ipc4-priv.h"
#include "ipc4-topology.h"
#include "ops.h"

#define SOF_IPC4_GAIN_PARAM_ID  0

static const struct sof_topology_token ipc4_sched_tokens[] = {
	{SOF_TKN_SCHED_LP_MODE, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc4_pipeline, lp_mode)}
};

static const struct sof_topology_token pipeline_tokens[] = {
	{SOF_TKN_SCHED_DYNAMIC_PIPELINE, SND_SOC_TPLG_TUPLE_TYPE_BOOL, get_token_u16,
		offsetof(struct snd_sof_widget, dynamic_pipeline_widget)},
};

static const struct sof_topology_token ipc4_comp_tokens[] = {
	{SOF_TKN_COMP_CPC, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc4_base_module_cfg, cpc)},
	{SOF_TKN_COMP_IS_PAGES, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc4_base_module_cfg, is_pages)},
};

static const struct sof_topology_token ipc4_audio_format_buffer_size_tokens[] = {
	{SOF_TKN_CAVS_AUDIO_FORMAT_IBS, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc4_base_module_cfg, ibs)},
	{SOF_TKN_CAVS_AUDIO_FORMAT_OBS, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc4_base_module_cfg, obs)},
};

static const struct sof_topology_token ipc4_in_audio_format_tokens[] = {
	{SOF_TKN_CAVS_AUDIO_FORMAT_IN_RATE, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc4_audio_format, sampling_frequency)},
	{SOF_TKN_CAVS_AUDIO_FORMAT_IN_BIT_DEPTH, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc4_audio_format, bit_depth)},
	{SOF_TKN_CAVS_AUDIO_FORMAT_IN_CH_MAP, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc4_audio_format, ch_map)},
	{SOF_TKN_CAVS_AUDIO_FORMAT_IN_CH_CFG, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc4_audio_format, ch_cfg)},
	{SOF_TKN_CAVS_AUDIO_FORMAT_IN_INTERLEAVING_STYLE, SND_SOC_TPLG_TUPLE_TYPE_WORD,
		get_token_u32, offsetof(struct sof_ipc4_audio_format, interleaving_style)},
	{SOF_TKN_CAVS_AUDIO_FORMAT_IN_FMT_CFG, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc4_audio_format, fmt_cfg)},
};

static const struct sof_topology_token ipc4_out_audio_format_tokens[] = {
	{SOF_TKN_CAVS_AUDIO_FORMAT_OUT_RATE, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc4_audio_format, sampling_frequency)},
	{SOF_TKN_CAVS_AUDIO_FORMAT_OUT_BIT_DEPTH, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc4_audio_format, bit_depth)},
	{SOF_TKN_CAVS_AUDIO_FORMAT_OUT_CH_MAP, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc4_audio_format, ch_map)},
	{SOF_TKN_CAVS_AUDIO_FORMAT_OUT_CH_CFG, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc4_audio_format, ch_cfg)},
	{SOF_TKN_CAVS_AUDIO_FORMAT_OUT_INTERLEAVING_STYLE, SND_SOC_TPLG_TUPLE_TYPE_WORD,
		get_token_u32, offsetof(struct sof_ipc4_audio_format, interleaving_style)},
	{SOF_TKN_CAVS_AUDIO_FORMAT_OUT_FMT_CFG, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc4_audio_format, fmt_cfg)},
};

static const struct sof_topology_token ipc4_copier_gateway_cfg_tokens[] = {
	{SOF_TKN_CAVS_AUDIO_FORMAT_DMA_BUFFER_SIZE, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32, 0},
};

static const struct sof_topology_token ipc4_copier_tokens[] = {
	{SOF_TKN_INTEL_COPIER_NODE_TYPE, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32, 0},
};

static const struct sof_topology_token ipc4_audio_fmt_num_tokens[] = {
	{SOF_TKN_COMP_NUM_AUDIO_FORMATS, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		0},
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
};

static const struct sof_topology_token gain_tokens[] = {
	{SOF_TKN_GAIN_RAMP_TYPE, SND_SOC_TPLG_TUPLE_TYPE_WORD,
		get_token_u32, offsetof(struct sof_ipc4_gain_data, curve_type)},
	{SOF_TKN_GAIN_RAMP_DURATION,
		SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc4_gain_data, curve_duration)},
	{SOF_TKN_GAIN_VAL, SND_SOC_TPLG_TUPLE_TYPE_WORD,
		get_token_u32, offsetof(struct sof_ipc4_gain_data, init_val)},
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
	[SOF_AUDIO_FORMAT_BUFFER_SIZE_TOKENS] = {"IPC4 Audio format buffer size tokens",
		ipc4_audio_format_buffer_size_tokens,
		ARRAY_SIZE(ipc4_audio_format_buffer_size_tokens)},
	[SOF_COPIER_GATEWAY_CFG_TOKENS] = {"IPC4 Copier gateway config tokens",
		ipc4_copier_gateway_cfg_tokens, ARRAY_SIZE(ipc4_copier_gateway_cfg_tokens)},
	[SOF_COPIER_TOKENS] = {"IPC4 Copier tokens", ipc4_copier_tokens,
		ARRAY_SIZE(ipc4_copier_tokens)},
	[SOF_AUDIO_FMT_NUM_TOKENS] = {"IPC4 Audio format number tokens",
		ipc4_audio_fmt_num_tokens, ARRAY_SIZE(ipc4_audio_fmt_num_tokens)},
	[SOF_GAIN_TOKENS] = {"Gain tokens", gain_tokens, ARRAY_SIZE(gain_tokens)},
};

static void sof_ipc4_dbg_audio_format(struct device *dev,
				      struct sof_ipc4_audio_format *format,
				      size_t object_size, int num_format)
{
	struct sof_ipc4_audio_format *fmt;
	void *ptr = format;
	int i;

	for (i = 0; i < num_format; i++, ptr = (u8 *)ptr + object_size) {
		fmt = ptr;
		dev_dbg(dev,
			" #%d: %uKHz, %ubit (ch_map %#x ch_cfg %u interleaving_style %u fmt_cfg %#x)\n",
			i, fmt->sampling_frequency, fmt->bit_depth, fmt->ch_map,
			fmt->ch_cfg, fmt->interleaving_style, fmt->fmt_cfg);
	}
}

/**
 * sof_ipc4_get_audio_fmt - get available audio formats from swidget->tuples
 * @scomp: pointer to pointer to SOC component
 * @swidget: pointer to struct snd_sof_widget containing tuples
 * @available_fmt: pointer to struct sof_ipc4_available_audio_format being filling in
 * @has_out_format: true if available_fmt contains output format
 *
 * Return: 0 if successful
 */
static int sof_ipc4_get_audio_fmt(struct snd_soc_component *scomp,
				  struct snd_sof_widget *swidget,
				  struct sof_ipc4_available_audio_format *available_fmt,
				  bool has_out_format)
{
	struct sof_ipc4_base_module_cfg *base_config;
	struct sof_ipc4_audio_format *out_format;
	int audio_fmt_num = 0;
	int ret, i;

	ret = sof_update_ipc_object(scomp, &audio_fmt_num,
				    SOF_AUDIO_FMT_NUM_TOKENS, swidget->tuples,
				    swidget->num_tuples, sizeof(audio_fmt_num), 1);
	if (ret || audio_fmt_num <= 0) {
		dev_err(scomp->dev, "Invalid number of audio formats: %d\n", audio_fmt_num);
		return -EINVAL;
	}
	available_fmt->audio_fmt_num = audio_fmt_num;

	dev_dbg(scomp->dev, "Number of audio formats: %d\n", available_fmt->audio_fmt_num);

	base_config = kcalloc(available_fmt->audio_fmt_num, sizeof(*base_config), GFP_KERNEL);
	if (!base_config)
		return -ENOMEM;

	/* set cpc and is_pages for all base_cfg */
	for (i = 0; i < available_fmt->audio_fmt_num; i++) {
		ret = sof_update_ipc_object(scomp, &base_config[i],
					    SOF_COMP_TOKENS, swidget->tuples,
					    swidget->num_tuples, sizeof(*base_config), 1);
		if (ret) {
			dev_err(scomp->dev, "parse comp tokens failed %d\n", ret);
			goto err_in;
		}
	}

	/* copy the ibs/obs for each base_cfg */
	ret = sof_update_ipc_object(scomp, base_config,
				    SOF_AUDIO_FORMAT_BUFFER_SIZE_TOKENS, swidget->tuples,
				    swidget->num_tuples, sizeof(*base_config),
				    available_fmt->audio_fmt_num);
	if (ret) {
		dev_err(scomp->dev, "parse buffer size tokens failed %d\n", ret);
		goto err_in;
	}

	for (i = 0; i < available_fmt->audio_fmt_num; i++)
		dev_dbg(scomp->dev, "%d: ibs: %d obs: %d cpc: %d is_pages: %d\n", i,
			base_config[i].ibs, base_config[i].obs,
			base_config[i].cpc, base_config[i].is_pages);

	ret = sof_update_ipc_object(scomp, &base_config->audio_fmt,
				    SOF_IN_AUDIO_FORMAT_TOKENS, swidget->tuples,
				    swidget->num_tuples, sizeof(*base_config),
				    available_fmt->audio_fmt_num);
	if (ret) {
		dev_err(scomp->dev, "parse base_config audio_fmt tokens failed %d\n", ret);
		goto err_in;
	}

	dev_dbg(scomp->dev, "Get input audio formats for %s\n", swidget->widget->name);
	sof_ipc4_dbg_audio_format(scomp->dev, &base_config->audio_fmt,
				  sizeof(*base_config),
				  available_fmt->audio_fmt_num);

	available_fmt->base_config = base_config;

	if (!has_out_format)
		return 0;

	out_format = kcalloc(available_fmt->audio_fmt_num, sizeof(*out_format), GFP_KERNEL);
	if (!out_format) {
		ret = -ENOMEM;
		goto err_in;
	}

	ret = sof_update_ipc_object(scomp, out_format,
				    SOF_OUT_AUDIO_FORMAT_TOKENS, swidget->tuples,
				    swidget->num_tuples, sizeof(*out_format),
				    available_fmt->audio_fmt_num);

	if (ret) {
		dev_err(scomp->dev, "parse output audio_fmt tokens failed\n");
		goto err_out;
	}

	available_fmt->out_audio_fmt = out_format;
	dev_dbg(scomp->dev, "Get output audio formats for %s\n", swidget->widget->name);
	sof_ipc4_dbg_audio_format(scomp->dev, out_format, sizeof(*out_format),
				  available_fmt->audio_fmt_num);

	return 0;

err_out:
	kfree(out_format);
err_in:
	kfree(base_config);

	return ret;
}

static void sof_ipc4_widget_free_comp(struct snd_sof_widget *swidget)
{
	kfree(swidget->private);
}

static int sof_ipc4_widget_set_module_info(struct snd_sof_widget *swidget)
{
	struct snd_soc_component *scomp = swidget->scomp;
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	struct sof_ipc4_fw_data *ipc4_data = sdev->private;
	struct sof_ipc4_fw_module *fw_modules = ipc4_data->fw_modules;
	int i;

	if (!fw_modules) {
		dev_err(sdev->dev, "no fw_module information\n");
		return -EINVAL;
	}

	/* set module info */
	for (i = 0; i < ipc4_data->num_fw_modules; i++) {
		if (guid_equal(&swidget->uuid, &fw_modules[i].man4_module_entry.uuid)) {
			swidget->module_info = &fw_modules[i];
			return 0;
		}
	}

	dev_err(sdev->dev, "failed to find module info for widget %s with UUID %pUL\n",
		swidget->widget->name, &swidget->uuid);
	return -EINVAL;
}

static int sof_ipc4_widget_setup_msg(struct snd_sof_widget *swidget, struct sof_ipc4_msg *msg)
{
	struct sof_ipc4_fw_module *fw_module;
	int ret;

	ret = sof_ipc4_widget_set_module_info(swidget);
	if (ret)
		return ret;

	fw_module = swidget->module_info;

	msg->primary = fw_module->man4_module_entry.id;
	msg->primary |= SOF_IPC4_MSG_TYPE_SET(SOF_IPC4_MOD_INIT_INSTANCE);
	msg->primary |= SOF_IPC4_MSG_DIR(SOF_IPC4_MSG_REQUEST);
	msg->primary |= SOF_IPC4_MSG_TARGET(SOF_IPC4_MODULE_MSG);

	msg->extension = SOF_IPC4_MOD_EXT_PPL_ID(swidget->pipeline_id);
	msg->extension |= SOF_IPC4_MOD_EXT_CORE_ID(swidget->core);

	return 0;
}

static int sof_ipc4_widget_setup_pcm(struct snd_sof_widget *swidget)
{
	struct sof_ipc4_available_audio_format *available_fmt;
	struct snd_soc_component *scomp = swidget->scomp;
	struct sof_ipc4_copier *ipc4_copier;
	int node_type = 0;
	int ret, i;

	ipc4_copier = kzalloc(sizeof(*ipc4_copier), GFP_KERNEL);
	if (!ipc4_copier)
		return -ENOMEM;

	swidget->private = ipc4_copier;
	available_fmt = &ipc4_copier->available_fmt;

	dev_dbg(scomp->dev, "Updating IPC structure for %s\n", swidget->widget->name);

	ret = sof_ipc4_get_audio_fmt(scomp, swidget, available_fmt, true);
	if (ret)
		goto free_copier;

	available_fmt->dma_buffer_size = kcalloc(available_fmt->audio_fmt_num, sizeof(u32),
						 GFP_KERNEL);
	if (!available_fmt->dma_buffer_size) {
		ret = -ENOMEM;
		goto free_copier;
	}

	ret = sof_update_ipc_object(scomp, available_fmt->dma_buffer_size,
				    SOF_COPIER_GATEWAY_CFG_TOKENS, swidget->tuples,
				    swidget->num_tuples, sizeof(u32),
				    available_fmt->audio_fmt_num);
	if (ret) {
		dev_err(scomp->dev, "Failed to parse dma buffer size in audio format for %s\n",
			swidget->widget->name);
		goto err;
	}

	dev_dbg(scomp->dev, "dma buffer size:\n");
	for (i = 0; i < available_fmt->audio_fmt_num; i++)
		dev_dbg(scomp->dev, "%d: %u\n", i,
			available_fmt->dma_buffer_size[i]);

	ret = sof_update_ipc_object(scomp, &node_type,
				    SOF_COPIER_TOKENS, swidget->tuples,
				    swidget->num_tuples, sizeof(node_type), 1);

	if (ret) {
		dev_err(scomp->dev, "parse host copier node type token failed %d\n",
			ret);
		goto err;
	}
	dev_dbg(scomp->dev, "host copier '%s' node_type %u\n", swidget->widget->name, node_type);

	ipc4_copier->data.gtw_cfg.node_id = SOF_IPC4_NODE_TYPE(node_type);
	ipc4_copier->gtw_attr = kzalloc(sizeof(*ipc4_copier->gtw_attr), GFP_KERNEL);
	if (!ipc4_copier->gtw_attr) {
		ret = -ENOMEM;
		goto err;
	}

	ipc4_copier->copier_config = (uint32_t *)ipc4_copier->gtw_attr;
	ipc4_copier->data.gtw_cfg.config_length =
		sizeof(struct sof_ipc4_gtw_attributes) >> 2;

	/* set up module info and message header */
	ret = sof_ipc4_widget_setup_msg(swidget, &ipc4_copier->msg);
	if (ret)
		goto free_gtw_attr;

	return 0;

free_gtw_attr:
	kfree(ipc4_copier->gtw_attr);
err:
	kfree(available_fmt->dma_buffer_size);
free_copier:
	kfree(ipc4_copier);
	return ret;
}

static void sof_ipc4_widget_free_comp_pcm(struct snd_sof_widget *swidget)
{
	struct sof_ipc4_copier *ipc4_copier = swidget->private;
	struct sof_ipc4_available_audio_format *available_fmt;

	if (!ipc4_copier)
		return;

	available_fmt = &ipc4_copier->available_fmt;
	kfree(available_fmt->dma_buffer_size);
	kfree(available_fmt->base_config);
	kfree(available_fmt->out_audio_fmt);
	kfree(ipc4_copier->gtw_attr);
	kfree(ipc4_copier);
	swidget->private = NULL;
}

static int sof_ipc4_widget_setup_comp_dai(struct snd_sof_widget *swidget)
{
	struct sof_ipc4_available_audio_format *available_fmt;
	struct snd_soc_component *scomp = swidget->scomp;
	struct snd_sof_dai *dai = swidget->private;
	struct sof_ipc4_copier *ipc4_copier;
	int node_type = 0;
	int ret, i;

	ipc4_copier = kzalloc(sizeof(*ipc4_copier), GFP_KERNEL);
	if (!ipc4_copier)
		return -ENOMEM;

	available_fmt = &ipc4_copier->available_fmt;

	dev_dbg(scomp->dev, "Updating IPC structure for %s\n", swidget->widget->name);

	ret = sof_ipc4_get_audio_fmt(scomp, swidget, available_fmt, true);
	if (ret)
		goto free_copier;

	available_fmt->dma_buffer_size = kcalloc(available_fmt->audio_fmt_num, sizeof(u32),
						 GFP_KERNEL);
	if (!available_fmt->dma_buffer_size) {
		ret = -ENOMEM;
		goto free_copier;
	}

	ret = sof_update_ipc_object(scomp, available_fmt->dma_buffer_size,
				    SOF_COPIER_GATEWAY_CFG_TOKENS, swidget->tuples,
				    swidget->num_tuples, sizeof(u32),
				    available_fmt->audio_fmt_num);
	if (ret) {
		dev_err(scomp->dev, "Failed to parse dma buffer size in audio format for %s\n",
			swidget->widget->name);
		goto err;
	}

	for (i = 0; i < available_fmt->audio_fmt_num; i++)
		dev_dbg(scomp->dev, "%d: dma buffer size: %u\n", i,
			available_fmt->dma_buffer_size[i]);

	ret = sof_update_ipc_object(scomp, &node_type,
				    SOF_COPIER_TOKENS, swidget->tuples,
				    swidget->num_tuples, sizeof(node_type), 1);
	if (ret) {
		dev_err(scomp->dev, "parse dai node type failed %d\n", ret);
		goto err;
	}

	ret = sof_update_ipc_object(scomp, ipc4_copier,
				    SOF_DAI_TOKENS, swidget->tuples,
				    swidget->num_tuples, sizeof(u32), 1);
	if (ret) {
		dev_err(scomp->dev, "parse dai copier node token failed %d\n", ret);
		goto err;
	}

	dev_dbg(scomp->dev, "dai %s node_type %u dai_type %u dai_index %d\n", swidget->widget->name,
		node_type, ipc4_copier->dai_type, ipc4_copier->dai_index);

	ipc4_copier->data.gtw_cfg.node_id = SOF_IPC4_NODE_TYPE(node_type);
	ipc4_copier->gtw_attr = kzalloc(sizeof(*ipc4_copier->gtw_attr), GFP_KERNEL);
	if (!ipc4_copier->gtw_attr) {
		ret = -ENOMEM;
		goto err;
	}

	ipc4_copier->copier_config = (uint32_t *)ipc4_copier->gtw_attr;
	ipc4_copier->data.gtw_cfg.config_length = sizeof(struct sof_ipc4_gtw_attributes) >> 2;

	dai->scomp = scomp;
	dai->private = ipc4_copier;

	/* set up module info and message header */
	ret = sof_ipc4_widget_setup_msg(swidget, &ipc4_copier->msg);
	if (ret)
		goto free_copier_config;

	return 0;

free_copier_config:
	kfree(ipc4_copier->copier_config);
err:
	kfree(available_fmt->dma_buffer_size);
free_copier:
	kfree(ipc4_copier);
	return ret;
}

static void sof_ipc4_widget_free_comp_dai(struct snd_sof_widget *swidget)
{
	struct sof_ipc4_available_audio_format *available_fmt;
	struct snd_sof_dai *dai = swidget->private;
	struct sof_ipc4_copier *ipc4_copier;

	if (!dai)
		return;

	ipc4_copier = dai->private;
	available_fmt = &ipc4_copier->available_fmt;

	kfree(available_fmt->dma_buffer_size);
	kfree(available_fmt->base_config);
	kfree(available_fmt->out_audio_fmt);
	kfree(ipc4_copier->copier_config);
	kfree(dai->private);
	kfree(dai);
	swidget->private = NULL;
}

static int sof_ipc4_widget_setup_comp_pipeline(struct snd_sof_widget *swidget)
{
	struct snd_soc_component *scomp = swidget->scomp;
	struct sof_ipc4_pipeline *pipeline;
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

	/* parse one set of pipeline tokens */
	ret = sof_update_ipc_object(scomp, swidget, SOF_PIPELINE_TOKENS, swidget->tuples,
				    swidget->num_tuples, sizeof(*swidget), 1);
	if (ret) {
		dev_err(scomp->dev, "parsing pipeline tokens failed\n");
		goto err;
	}

	/* TODO: Get priority from topology */
	pipeline->priority = 0;

	dev_dbg(scomp->dev, "pipeline '%s': id %d pri %d lp mode %d\n",
		swidget->widget->name, swidget->pipeline_id,
		pipeline->priority, pipeline->lp_mode);

	swidget->private = pipeline;

	pipeline->msg.primary = SOF_IPC4_GLB_PIPE_PRIORITY(pipeline->priority);
	pipeline->msg.primary |= SOF_IPC4_GLB_PIPE_INSTANCE_ID(swidget->pipeline_id);
	pipeline->msg.primary |= SOF_IPC4_MSG_TYPE_SET(SOF_IPC4_GLB_CREATE_PIPELINE);
	pipeline->msg.primary |= SOF_IPC4_MSG_DIR(SOF_IPC4_MSG_REQUEST);
	pipeline->msg.primary |= SOF_IPC4_MSG_TARGET(SOF_IPC4_FW_GEN_MSG);

	pipeline->msg.extension = pipeline->lp_mode;
	pipeline->state = SOF_IPC4_PIPE_UNINITIALIZED;

	return 0;
err:
	kfree(pipeline);
	return ret;
}

static int sof_ipc4_widget_setup_comp_pga(struct snd_sof_widget *swidget)
{
	struct snd_soc_component *scomp = swidget->scomp;
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	struct sof_ipc4_fw_module *fw_module;
	struct snd_sof_control *scontrol;
	struct sof_ipc4_gain *gain;
	int ret;

	gain = kzalloc(sizeof(*gain), GFP_KERNEL);
	if (!gain)
		return -ENOMEM;

	swidget->private = gain;

	gain->data.channels = SOF_IPC4_GAIN_ALL_CHANNELS_MASK;
	gain->data.init_val = SOF_IPC4_VOL_ZERO_DB;

	/* The out_audio_fmt in topology is ignored as it is not required to be sent to the FW */
	ret = sof_ipc4_get_audio_fmt(scomp, swidget, &gain->available_fmt, false);
	if (ret)
		goto err;

	ret = sof_update_ipc_object(scomp, &gain->data, SOF_GAIN_TOKENS, swidget->tuples,
				    swidget->num_tuples, sizeof(gain->data), 1);
	if (ret) {
		dev_err(scomp->dev, "Parsing gain tokens failed\n");
		goto err;
	}

	dev_dbg(scomp->dev,
		"pga widget %s: ramp type: %d, ramp duration %d, initial gain value: %#x, cpc %d\n",
		swidget->widget->name, gain->data.curve_type, gain->data.curve_duration,
		gain->data.init_val, gain->base_config.cpc);

	ret = sof_ipc4_widget_setup_msg(swidget, &gain->msg);
	if (ret)
		goto err;

	fw_module = swidget->module_info;

	/* update module ID for all kcontrols for this widget */
	list_for_each_entry(scontrol, &sdev->kcontrol_list, list)
		if (scontrol->comp_id == swidget->comp_id) {
			struct sof_ipc4_control_data *cdata = scontrol->ipc_control_data;
			struct sof_ipc4_msg *msg = &cdata->msg;

			msg->primary |= fw_module->man4_module_entry.id;
		}

	return 0;
err:
	kfree(gain);
	return ret;
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

	/* The out_audio_fmt in topology is ignored as it is not required to be sent to the FW */
	ret = sof_ipc4_get_audio_fmt(scomp, swidget, &mixer->available_fmt, false);
	if (ret)
		goto err;

	ret = sof_ipc4_widget_setup_msg(swidget, &mixer->msg);
	if (ret)
		goto err;

	return 0;
err:
	kfree(mixer);
	return ret;
}

static void
sof_ipc4_update_pipeline_mem_usage(struct snd_sof_dev *sdev, struct snd_sof_widget *swidget,
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

	pipe_widget = swidget->pipe_widget;
	pipeline = pipe_widget->private;
	pipeline->mem_usage += total;
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

static int sof_ipc4_init_audio_fmt(struct snd_sof_dev *sdev,
				   struct snd_sof_widget *swidget,
				   struct sof_ipc4_base_module_cfg *base_config,
				   struct sof_ipc4_audio_format *out_format,
				   struct snd_pcm_hw_params *params,
				   struct sof_ipc4_available_audio_format *available_fmt,
				   size_t object_offset)
{
	void *ptr = available_fmt->ref_audio_fmt;
	u32 valid_bits;
	u32 channels;
	u32 rate;
	int sample_valid_bits;
	int i;

	if (!ptr) {
		dev_err(sdev->dev, "no reference formats for %s\n", swidget->widget->name);
		return -EINVAL;
	}

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		sample_valid_bits = 16;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		sample_valid_bits = 24;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		sample_valid_bits = 32;
		break;
	default:
		dev_err(sdev->dev, "invalid pcm frame format %d\n", params_format(params));
		return -EINVAL;
	}

	if (!available_fmt->audio_fmt_num) {
		dev_err(sdev->dev, "no formats available for %s\n", swidget->widget->name);
		return -EINVAL;
	}

	/*
	 * Search supported audio formats to match rate, channels ,and
	 * sample_valid_bytes from runtime params
	 */
	for (i = 0; i < available_fmt->audio_fmt_num; i++, ptr = (u8 *)ptr + object_offset) {
		struct sof_ipc4_audio_format *fmt = ptr;

		rate = fmt->sampling_frequency;
		channels = SOF_IPC4_AUDIO_FORMAT_CFG_CHANNELS_COUNT(fmt->fmt_cfg);
		valid_bits = SOF_IPC4_AUDIO_FORMAT_CFG_V_BIT_DEPTH(fmt->fmt_cfg);
		if (params_rate(params) == rate && params_channels(params) == channels &&
		    sample_valid_bits == valid_bits) {
			dev_dbg(sdev->dev, "%s: matching audio format index for %uHz, %ubit, %u channels: %d\n",
				__func__, rate, valid_bits, channels, i);

			/* copy ibs/obs and input format */
			memcpy(base_config, &available_fmt->base_config[i],
			       sizeof(struct sof_ipc4_base_module_cfg));

			/* copy output format */
			if (out_format)
				memcpy(out_format, &available_fmt->out_audio_fmt[i],
				       sizeof(struct sof_ipc4_audio_format));
			break;
		}
	}

	if (i == available_fmt->audio_fmt_num) {
		dev_err(sdev->dev, "%s: Unsupported audio format: %uHz, %ubit, %u channels\n",
			__func__, params_rate(params), sample_valid_bits, params_channels(params));
		return -EINVAL;
	}

	dev_dbg(sdev->dev, "Init input audio formats for %s\n", swidget->widget->name);
	sof_ipc4_dbg_audio_format(sdev->dev, &base_config->audio_fmt,
				  sizeof(*base_config), 1);
	if (out_format) {
		dev_dbg(sdev->dev, "Init output audio formats for %s\n", swidget->widget->name);
		sof_ipc4_dbg_audio_format(sdev->dev, out_format,
					  sizeof(*out_format), 1);
	}

	/* Return the index of the matched format */
	return i;
}

static void sof_ipc4_unprepare_copier_module(struct snd_sof_widget *swidget)
{
	struct sof_ipc4_fw_module *fw_module = swidget->module_info;
	struct sof_ipc4_copier *ipc4_copier = NULL;
	struct snd_sof_widget *pipe_widget;
	struct sof_ipc4_pipeline *pipeline;

	/* reset pipeline memory usage */
	pipe_widget = swidget->pipe_widget;
	pipeline = pipe_widget->private;
	pipeline->mem_usage = 0;

	if (WIDGET_IS_AIF(swidget->id)) {
		ipc4_copier = swidget->private;
	} else if (WIDGET_IS_DAI(swidget->id)) {
		struct snd_sof_dai *dai = swidget->private;

		ipc4_copier = dai->private;
	}

	if (ipc4_copier) {
		kfree(ipc4_copier->ipc_config_data);
		ipc4_copier->ipc_config_data = NULL;
		ipc4_copier->ipc_config_size = 0;
	}

	ida_free(&fw_module->m_ida, swidget->instance_id);
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
	struct snd_pcm_hw_params *ref_params;
	struct sof_ipc4_copier *ipc4_copier;
	struct snd_mask *fmt;
	int out_sample_valid_bits;
	size_t ref_audio_fmt_size;
	void **ipc_config_data;
	int *ipc_config_size;
	u32 **data;
	int ipc_size, ret;

	dev_dbg(sdev->dev, "%s: copier %s, type %d", __func__, swidget->widget->name, swidget->id);

	switch (swidget->id) {
	case snd_soc_dapm_aif_in:
	case snd_soc_dapm_aif_out:
	{
		struct sof_ipc4_gtw_attributes *gtw_attr;
		struct snd_sof_widget *pipe_widget;
		struct sof_ipc4_pipeline *pipeline;

		pipe_widget = swidget->pipe_widget;
		pipeline = pipe_widget->private;
		ipc4_copier = (struct sof_ipc4_copier *)swidget->private;
		gtw_attr = ipc4_copier->gtw_attr;
		copier_data = &ipc4_copier->data;
		available_fmt = &ipc4_copier->available_fmt;

		/*
		 * base_config->audio_fmt and out_audio_fmt represent the input and output audio
		 * formats. Use the input format as the reference to match pcm params for playback
		 * and the output format as reference for capture.
		 */
		if (dir == SNDRV_PCM_STREAM_PLAYBACK) {
			available_fmt->ref_audio_fmt = &available_fmt->base_config->audio_fmt;
			ref_audio_fmt_size = sizeof(struct sof_ipc4_base_module_cfg);
		} else {
			available_fmt->ref_audio_fmt = available_fmt->out_audio_fmt;
			ref_audio_fmt_size = sizeof(struct sof_ipc4_audio_format);
		}
		copier_data->gtw_cfg.node_id &= ~SOF_IPC4_NODE_INDEX_MASK;
		copier_data->gtw_cfg.node_id |=
			SOF_IPC4_NODE_INDEX(platform_params->stream_tag - 1);

		/* set gateway attributes */
		gtw_attr->lp_buffer_alloc = pipeline->lp_mode;
		ref_params = fe_params;
		break;
	}
	case snd_soc_dapm_dai_in:
	case snd_soc_dapm_dai_out:
	{
		struct snd_sof_dai *dai = swidget->private;

		ipc4_copier = (struct sof_ipc4_copier *)dai->private;
		copier_data = &ipc4_copier->data;
		available_fmt = &ipc4_copier->available_fmt;
		if (dir == SNDRV_PCM_STREAM_CAPTURE) {
			available_fmt->ref_audio_fmt = available_fmt->out_audio_fmt;
			ref_audio_fmt_size = sizeof(struct sof_ipc4_audio_format);

			/*
			 * modify the input params for the dai copier as it only supports
			 * 32-bit always
			 */
			fmt = hw_param_mask(pipeline_params, SNDRV_PCM_HW_PARAM_FORMAT);
			snd_mask_none(fmt);
			snd_mask_set_format(fmt, SNDRV_PCM_FORMAT_S32_LE);
		} else {
			available_fmt->ref_audio_fmt = &available_fmt->base_config->audio_fmt;
			ref_audio_fmt_size = sizeof(struct sof_ipc4_base_module_cfg);
		}

		ref_params = pipeline_params;

		break;
	}
	default:
		dev_err(sdev->dev, "unsupported type %d for copier %s",
			swidget->id, swidget->widget->name);
		return -EINVAL;
	}

	/* set input and output audio formats */
	ret = sof_ipc4_init_audio_fmt(sdev, swidget, &copier_data->base_config,
				      &copier_data->out_format, ref_params,
				      available_fmt, ref_audio_fmt_size);
	if (ret < 0)
		return ret;

	/* modify the input params for the next widget */
	fmt = hw_param_mask(pipeline_params, SNDRV_PCM_HW_PARAM_FORMAT);
	out_sample_valid_bits =
		SOF_IPC4_AUDIO_FORMAT_CFG_V_BIT_DEPTH(copier_data->out_format.fmt_cfg);
	snd_mask_none(fmt);
	switch (out_sample_valid_bits) {
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
		dev_err(sdev->dev, "invalid sample frame format %d\n",
			params_format(pipeline_params));
		return -EINVAL;
	}

	/* set the gateway dma_buffer_size using the matched ID returned above */
	copier_data->gtw_cfg.dma_buffer_size = available_fmt->dma_buffer_size[ret];

	data = &ipc4_copier->copier_config;
	ipc_config_size = &ipc4_copier->ipc_config_size;
	ipc_config_data = &ipc4_copier->ipc_config_data;

	/* config_length is DWORD based */
	ipc_size = sizeof(*copier_data) + copier_data->gtw_cfg.config_length * 4;

	dev_dbg(sdev->dev, "copier %s, IPC size is %d", swidget->widget->name, ipc_size);

	*ipc_config_data = kzalloc(ipc_size, GFP_KERNEL);
	if (!*ipc_config_data)
		return -ENOMEM;

	*ipc_config_size = ipc_size;

	/* copy IPC data */
	memcpy(*ipc_config_data, (void *)copier_data, sizeof(*copier_data));
	if (copier_data->gtw_cfg.config_length)
		memcpy(*ipc_config_data + sizeof(*copier_data),
		       *data, copier_data->gtw_cfg.config_length * 4);

	/* update pipeline memory usage */
	sof_ipc4_update_pipeline_mem_usage(sdev, swidget, &copier_data->base_config);

	/* assign instance ID */
	return sof_ipc4_widget_assign_instance_id(sdev, swidget);
}

static void sof_ipc4_unprepare_generic_module(struct snd_sof_widget *swidget)
{
	struct sof_ipc4_fw_module *fw_module = swidget->module_info;

	ida_free(&fw_module->m_ida, swidget->instance_id);
}

static int sof_ipc4_prepare_gain_module(struct snd_sof_widget *swidget,
					struct snd_pcm_hw_params *fe_params,
					struct snd_sof_platform_stream_params *platform_params,
					struct snd_pcm_hw_params *pipeline_params, int dir)
{
	struct snd_soc_component *scomp = swidget->scomp;
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	struct sof_ipc4_gain *gain = swidget->private;
	int ret;

	gain->available_fmt.ref_audio_fmt = &gain->available_fmt.base_config->audio_fmt;

	/* output format is not required to be sent to the FW for gain */
	ret = sof_ipc4_init_audio_fmt(sdev, swidget, &gain->base_config,
				      NULL, pipeline_params, &gain->available_fmt,
				      sizeof(gain->base_config));
	if (ret < 0)
		return ret;

	/* update pipeline memory usage */
	sof_ipc4_update_pipeline_mem_usage(sdev, swidget, &gain->base_config);

	/* assign instance ID */
	return sof_ipc4_widget_assign_instance_id(sdev, swidget);
}

static int sof_ipc4_prepare_mixer_module(struct snd_sof_widget *swidget,
					 struct snd_pcm_hw_params *fe_params,
					 struct snd_sof_platform_stream_params *platform_params,
					 struct snd_pcm_hw_params *pipeline_params, int dir)
{
	struct snd_soc_component *scomp = swidget->scomp;
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	struct sof_ipc4_mixer *mixer = swidget->private;
	int ret;

	/* only 32bit is supported by mixer */
	mixer->available_fmt.ref_audio_fmt = &mixer->available_fmt.base_config->audio_fmt;

	/* output format is not required to be sent to the FW for mixer */
	ret = sof_ipc4_init_audio_fmt(sdev, swidget, &mixer->base_config,
				      NULL, pipeline_params, &mixer->available_fmt,
				      sizeof(mixer->base_config));
	if (ret < 0)
		return ret;

	/* update pipeline memory usage */
	sof_ipc4_update_pipeline_mem_usage(sdev, swidget, &mixer->base_config);

	/* assign instance ID */
	return sof_ipc4_widget_assign_instance_id(sdev, swidget);
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

	msg->extension = SOF_IPC4_MOD_EXT_MSG_PARAM_ID(SOF_IPC4_GAIN_PARAM_ID);

	/* set default volume values to 0dB in control */
	for (i = 0; i < scontrol->num_channels; i++) {
		control_data->chanv[i].channel = i;
		control_data->chanv[i].value = SOF_IPC4_VOL_ZERO_DB;
	}

	return 0;
}

static int sof_ipc4_control_setup(struct snd_sof_dev *sdev, struct snd_sof_control *scontrol)
{
	switch (scontrol->info_type) {
	case SND_SOC_TPLG_CTL_VOLSW:
	case SND_SOC_TPLG_CTL_VOLSW_SX:
	case SND_SOC_TPLG_CTL_VOLSW_XR_SX:
		return sof_ipc4_control_load_volume(sdev, scontrol);
	default:
		break;
	}

	return 0;
}

static int sof_ipc4_widget_setup(struct snd_sof_dev *sdev, struct snd_sof_widget *swidget)
{
	struct snd_sof_widget *pipe_widget = swidget->pipe_widget;
	struct sof_ipc4_pipeline *pipeline;
	struct sof_ipc4_msg *msg;
	void *ipc_data = NULL;
	u32 ipc_size = 0;
	int ret;

	dev_dbg(sdev->dev, "Create widget %s instance %d - pipe %d - core %d\n",
		swidget->widget->name, swidget->instance_id, swidget->pipeline_id, swidget->core);

	switch (swidget->id) {
	case snd_soc_dapm_scheduler:
		pipeline = swidget->private;

		dev_dbg(sdev->dev, "pipeline: %d memory pages: %d\n", swidget->pipeline_id,
			pipeline->mem_usage);

		msg = &pipeline->msg;
		msg->primary |= pipeline->mem_usage;
		break;
	case snd_soc_dapm_aif_in:
	case snd_soc_dapm_aif_out:
	{
		struct sof_ipc4_copier *ipc4_copier = swidget->private;

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

		ipc_size = ipc4_copier->ipc_config_size;
		ipc_data = ipc4_copier->ipc_config_data;

		msg = &ipc4_copier->msg;
		break;
	}
	case snd_soc_dapm_pga:
	{
		struct sof_ipc4_gain *gain = swidget->private;

		ipc_size = sizeof(struct sof_ipc4_base_module_cfg) +
			   sizeof(struct sof_ipc4_gain_data);
		ipc_data = gain;

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
	default:
		dev_err(sdev->dev, "widget type %d not supported", swidget->id);
		return -EINVAL;
	}

	if (swidget->id != snd_soc_dapm_scheduler) {
		pipeline = pipe_widget->private;
		msg->primary &= ~SOF_IPC4_MOD_INSTANCE_MASK;
		msg->primary |= SOF_IPC4_MOD_INSTANCE(swidget->instance_id);

		msg->extension &= ~SOF_IPC4_MOD_EXT_PARAM_SIZE_MASK;
		msg->extension |= ipc_size >> 2;
		msg->extension &= ~SOF_IPC4_MOD_EXT_DOMAIN_MASK;
		msg->extension |= SOF_IPC4_MOD_EXT_DOMAIN(pipeline->lp_mode);
	}

	msg->data_size = ipc_size;
	msg->data_ptr = ipc_data;

	ret = sof_ipc_tx_message(sdev->ipc, msg, ipc_size, NULL, 0);
	if (ret < 0)
		dev_err(sdev->dev, "failed to create module %s\n", swidget->widget->name);

	return ret;
}

static int sof_ipc4_widget_free(struct snd_sof_dev *sdev, struct snd_sof_widget *swidget)
{
	int ret = 0;

	/* freeing a pipeline frees all the widgets associated with it */
	if (swidget->id == snd_soc_dapm_scheduler) {
		struct sof_ipc4_pipeline *pipeline = swidget->private;
		struct sof_ipc4_msg msg = {{ 0 }};
		u32 header;

		header = SOF_IPC4_GLB_PIPE_INSTANCE_ID(swidget->pipeline_id);
		header |= SOF_IPC4_MSG_TYPE_SET(SOF_IPC4_GLB_DELETE_PIPELINE);
		header |= SOF_IPC4_MSG_DIR(SOF_IPC4_MSG_REQUEST);
		header |= SOF_IPC4_MSG_TARGET(SOF_IPC4_FW_GEN_MSG);

		msg.primary = header;

		ret = sof_ipc_tx_message(sdev->ipc, &msg, 0, NULL, 0);
		if (ret < 0)
			dev_err(sdev->dev, "failed to free pipeline widget %s\n",
				swidget->widget->name);

		pipeline->mem_usage = 0;
		pipeline->state = SOF_IPC4_PIPE_UNINITIALIZED;
	}

	return ret;
}

static int sof_ipc4_route_setup(struct snd_sof_dev *sdev, struct snd_sof_route *sroute)
{
	struct snd_sof_widget *src_widget = sroute->src_widget;
	struct snd_sof_widget *sink_widget = sroute->sink_widget;
	struct sof_ipc4_fw_module *src_fw_module = src_widget->module_info;
	struct sof_ipc4_fw_module *sink_fw_module = sink_widget->module_info;
	struct sof_ipc4_msg msg = {{ 0 }};
	u32 header, extension;
	int src_queue = 0;
	int dst_queue = 0;
	int ret;

	dev_dbg(sdev->dev, "%s: bind %s -> %s\n", __func__,
		src_widget->widget->name, sink_widget->widget->name);

	header = src_fw_module->man4_module_entry.id;
	header |= SOF_IPC4_MOD_INSTANCE(src_widget->instance_id);
	header |= SOF_IPC4_MSG_TYPE_SET(SOF_IPC4_MOD_BIND);
	header |= SOF_IPC4_MSG_DIR(SOF_IPC4_MSG_REQUEST);
	header |= SOF_IPC4_MSG_TARGET(SOF_IPC4_MODULE_MSG);

	extension = sink_fw_module->man4_module_entry.id;
	extension |= SOF_IPC4_MOD_EXT_DST_MOD_INSTANCE(sink_widget->instance_id);
	extension |= SOF_IPC4_MOD_EXT_DST_MOD_QUEUE_ID(dst_queue);
	extension |= SOF_IPC4_MOD_EXT_SRC_MOD_QUEUE_ID(src_queue);

	msg.primary = header;
	msg.extension = extension;

	ret = sof_ipc_tx_message(sdev->ipc, &msg, 0, NULL, 0);
	if (ret < 0)
		dev_err(sdev->dev, "%s: failed to bind modules %s -> %s\n",
			__func__, src_widget->widget->name, sink_widget->widget->name);

	return ret;
}

static int sof_ipc4_route_free(struct snd_sof_dev *sdev, struct snd_sof_route *sroute)
{
	struct snd_sof_widget *src_widget = sroute->src_widget;
	struct snd_sof_widget *sink_widget = sroute->sink_widget;
	struct sof_ipc4_fw_module *src_fw_module = src_widget->module_info;
	struct sof_ipc4_fw_module *sink_fw_module = sink_widget->module_info;
	struct sof_ipc4_msg msg = {{ 0 }};
	u32 header, extension;
	int src_queue = 0;
	int dst_queue = 0;
	int ret;

	dev_dbg(sdev->dev, "%s: unbind modules %s -> %s\n", __func__,
		src_widget->widget->name, sink_widget->widget->name);

	header = src_fw_module->man4_module_entry.id;
	header |= SOF_IPC4_MOD_INSTANCE(src_widget->instance_id);
	header |= SOF_IPC4_MSG_TYPE_SET(SOF_IPC4_MOD_UNBIND);
	header |= SOF_IPC4_MSG_DIR(SOF_IPC4_MSG_REQUEST);
	header |= SOF_IPC4_MSG_TARGET(SOF_IPC4_MODULE_MSG);

	extension = sink_fw_module->man4_module_entry.id;
	extension |= SOF_IPC4_MOD_EXT_DST_MOD_INSTANCE(sink_widget->instance_id);
	extension |= SOF_IPC4_MOD_EXT_DST_MOD_QUEUE_ID(dst_queue);
	extension |= SOF_IPC4_MOD_EXT_SRC_MOD_QUEUE_ID(src_queue);

	msg.primary = header;
	msg.extension = extension;

	ret = sof_ipc_tx_message(sdev->ipc, &msg, 0, NULL, 0);
	if (ret < 0)
		dev_err(sdev->dev, "failed to unbind modules %s -> %s\n",
			src_widget->widget->name, sink_widget->widget->name);

	return ret;
}

static int sof_ipc4_dai_config(struct snd_sof_dev *sdev, struct snd_sof_widget *swidget,
			       unsigned int flags, struct snd_sof_dai_config_data *data)
{
	struct snd_sof_widget *pipe_widget = swidget->pipe_widget;
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

	switch (ipc4_copier->dai_type) {
	case SOF_DAI_INTEL_HDA:
		gtw_attr = ipc4_copier->gtw_attr;
		gtw_attr->lp_buffer_alloc = pipeline->lp_mode;
		fallthrough;
	case SOF_DAI_INTEL_ALH:
		copier_data->gtw_cfg.node_id &= ~SOF_IPC4_NODE_INDEX_MASK;
		copier_data->gtw_cfg.node_id |= SOF_IPC4_NODE_INDEX(data->dai_data);
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

static enum sof_tokens host_token_list[] = {
	SOF_COMP_TOKENS,
	SOF_AUDIO_FMT_NUM_TOKENS,
	SOF_AUDIO_FORMAT_BUFFER_SIZE_TOKENS,
	SOF_IN_AUDIO_FORMAT_TOKENS,
	SOF_OUT_AUDIO_FORMAT_TOKENS,
	SOF_COPIER_GATEWAY_CFG_TOKENS,
	SOF_COPIER_TOKENS,
	SOF_COMP_EXT_TOKENS,
};

static enum sof_tokens pipeline_token_list[] = {
	SOF_SCHED_TOKENS,
	SOF_PIPELINE_TOKENS,
};

static enum sof_tokens dai_token_list[] = {
	SOF_COMP_TOKENS,
	SOF_AUDIO_FMT_NUM_TOKENS,
	SOF_AUDIO_FORMAT_BUFFER_SIZE_TOKENS,
	SOF_IN_AUDIO_FORMAT_TOKENS,
	SOF_OUT_AUDIO_FORMAT_TOKENS,
	SOF_COPIER_GATEWAY_CFG_TOKENS,
	SOF_COPIER_TOKENS,
	SOF_DAI_TOKENS,
	SOF_COMP_EXT_TOKENS,
};

static enum sof_tokens pga_token_list[] = {
	SOF_COMP_TOKENS,
	SOF_GAIN_TOKENS,
	SOF_AUDIO_FMT_NUM_TOKENS,
	SOF_AUDIO_FORMAT_BUFFER_SIZE_TOKENS,
	SOF_IN_AUDIO_FORMAT_TOKENS,
	SOF_COMP_EXT_TOKENS,
};

static enum sof_tokens mixer_token_list[] = {
	SOF_COMP_TOKENS,
	SOF_AUDIO_FMT_NUM_TOKENS,
	SOF_IN_AUDIO_FORMAT_TOKENS,
	SOF_AUDIO_FORMAT_BUFFER_SIZE_TOKENS,
	SOF_COMP_EXT_TOKENS,
};

static const struct sof_ipc_tplg_widget_ops tplg_ipc4_widget_ops[SND_SOC_DAPM_TYPE_COUNT] = {
	[snd_soc_dapm_aif_in] =  {sof_ipc4_widget_setup_pcm, sof_ipc4_widget_free_comp_pcm,
				  host_token_list, ARRAY_SIZE(host_token_list), NULL,
				  sof_ipc4_prepare_copier_module,
				  sof_ipc4_unprepare_copier_module},
	[snd_soc_dapm_aif_out] = {sof_ipc4_widget_setup_pcm, sof_ipc4_widget_free_comp_pcm,
				  host_token_list, ARRAY_SIZE(host_token_list), NULL,
				  sof_ipc4_prepare_copier_module,
				  sof_ipc4_unprepare_copier_module},
	[snd_soc_dapm_dai_in] = {sof_ipc4_widget_setup_comp_dai, sof_ipc4_widget_free_comp_dai,
				 dai_token_list, ARRAY_SIZE(dai_token_list), NULL,
				 sof_ipc4_prepare_copier_module,
				 sof_ipc4_unprepare_copier_module},
	[snd_soc_dapm_dai_out] = {sof_ipc4_widget_setup_comp_dai, sof_ipc4_widget_free_comp_dai,
				  dai_token_list, ARRAY_SIZE(dai_token_list), NULL,
				  sof_ipc4_prepare_copier_module,
				  sof_ipc4_unprepare_copier_module},
	[snd_soc_dapm_scheduler] = {sof_ipc4_widget_setup_comp_pipeline, sof_ipc4_widget_free_comp,
				    pipeline_token_list, ARRAY_SIZE(pipeline_token_list), NULL,
				    NULL, NULL},
	[snd_soc_dapm_pga] = {sof_ipc4_widget_setup_comp_pga, sof_ipc4_widget_free_comp,
			      pga_token_list, ARRAY_SIZE(pga_token_list), NULL,
			      sof_ipc4_prepare_gain_module,
			      sof_ipc4_unprepare_generic_module},
	[snd_soc_dapm_mixer] = {sof_ipc4_widget_setup_comp_mixer, sof_ipc4_widget_free_comp,
				mixer_token_list, ARRAY_SIZE(mixer_token_list),
				NULL, sof_ipc4_prepare_mixer_module,
				sof_ipc4_unprepare_generic_module},
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
};
