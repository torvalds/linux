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

static const struct sof_ipc_tplg_widget_ops tplg_ipc4_widget_ops[SND_SOC_DAPM_TYPE_COUNT] = {
	[snd_soc_dapm_aif_in] =  {sof_ipc4_widget_setup_pcm, sof_ipc4_widget_free_comp_pcm,
				  host_token_list, ARRAY_SIZE(host_token_list), NULL,
				  NULL, NULL},
	[snd_soc_dapm_aif_out] = {sof_ipc4_widget_setup_pcm, sof_ipc4_widget_free_comp_pcm,
				  host_token_list, ARRAY_SIZE(host_token_list), NULL,
				  NULL, NULL},
	[snd_soc_dapm_dai_in] = {sof_ipc4_widget_setup_comp_dai, sof_ipc4_widget_free_comp_dai,
				 dai_token_list, ARRAY_SIZE(dai_token_list), NULL, NULL, NULL},
	[snd_soc_dapm_dai_out] = {sof_ipc4_widget_setup_comp_dai, sof_ipc4_widget_free_comp_dai,
				  dai_token_list, ARRAY_SIZE(dai_token_list), NULL, NULL, NULL},
	[snd_soc_dapm_scheduler] = {sof_ipc4_widget_setup_comp_pipeline, sof_ipc4_widget_free_comp,
				    pipeline_token_list, ARRAY_SIZE(pipeline_token_list), NULL,
				    NULL, NULL},
};

const struct sof_ipc_tplg_ops ipc4_tplg_ops = {
	.widget = tplg_ipc4_widget_ops,
	.token_list = ipc4_token_list,
};
