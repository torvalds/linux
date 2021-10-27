// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2020, Linaro Limited

#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/pcm.h>
#include <sound/control.h>
#include <sound/asound.h>
#include <linux/firmware.h>
#include <sound/soc-topology.h>
#include <sound/soc-dpcm.h>
#include <uapi/sound/snd_ar_tokens.h>
#include <linux/kernel.h>
#include <linux/wait.h>
#include "q6apm.h"
#include "audioreach.h"

struct snd_ar_control {
	u32 sgid; /* Sub Graph ID */
	struct snd_soc_component *scomp;
};

static struct audioreach_graph_info *audioreach_tplg_alloc_graph_info(struct q6apm *apm,
								      uint32_t graph_id,
								      bool *found)
{
	struct audioreach_graph_info *info;
	int ret;

	mutex_lock(&apm->lock);
	info = idr_find(&apm->graph_info_idr, graph_id);
	mutex_unlock(&apm->lock);

	if (info) {
		*found = true;
		return info;
	}

	*found = false;
	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&info->sg_list);

	mutex_lock(&apm->lock);
	ret = idr_alloc(&apm->graph_info_idr, info, graph_id, graph_id + 1, GFP_KERNEL);
	mutex_unlock(&apm->lock);

	if (ret < 0) {
		dev_err(apm->dev, "Failed to allocate Graph ID (%x)\n", graph_id);
		kfree(info);
		return ERR_PTR(ret);
	}

	info->id = ret;

	return info;
}

static void audioreach_tplg_add_sub_graph(struct audioreach_sub_graph *sg,
					  struct audioreach_graph_info *info)
{
	list_add_tail(&sg->node, &info->sg_list);
	sg->info = info;
	info->num_sub_graphs++;
}

static struct audioreach_sub_graph *audioreach_tplg_alloc_sub_graph(struct q6apm *apm,
								    uint32_t sub_graph_id,
								    bool *found)
{
	struct audioreach_sub_graph *sg;
	int ret;

	if (!sub_graph_id)
		return ERR_PTR(-EINVAL);

	/* Find if there is already a matching sub-graph */
	mutex_lock(&apm->lock);
	sg = idr_find(&apm->sub_graphs_idr, sub_graph_id);
	mutex_unlock(&apm->lock);

	if (sg) {
		*found = true;
		return sg;
	}

	*found = false;
	sg = kzalloc(sizeof(*sg), GFP_KERNEL);
	if (!sg)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&sg->container_list);

	mutex_lock(&apm->lock);
	ret = idr_alloc(&apm->sub_graphs_idr, sg, sub_graph_id, sub_graph_id + 1, GFP_KERNEL);
	mutex_unlock(&apm->lock);

	if (ret < 0) {
		dev_err(apm->dev, "Failed to allocate Sub-Graph Instance ID (%x)\n", sub_graph_id);
		kfree(sg);
		return ERR_PTR(ret);
	}

	sg->sub_graph_id = ret;

	return sg;
}

static struct audioreach_container *audioreach_tplg_alloc_container(struct q6apm *apm,
							    struct audioreach_sub_graph *sg,
							    uint32_t container_id,
							    bool *found)
{
	struct audioreach_container *cont;
	int ret;

	if (!container_id)
		return ERR_PTR(-EINVAL);

	mutex_lock(&apm->lock);
	cont = idr_find(&apm->containers_idr, container_id);
	mutex_unlock(&apm->lock);

	if (cont) {
		*found = true;
		return cont;
	}
	*found = false;

	cont = kzalloc(sizeof(*cont), GFP_KERNEL);
	if (!cont)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&cont->modules_list);

	mutex_lock(&apm->lock);
	ret = idr_alloc(&apm->containers_idr, cont, container_id, container_id + 1, GFP_KERNEL);
	mutex_unlock(&apm->lock);

	if (ret < 0) {
		dev_err(apm->dev, "Failed to allocate Container Instance ID (%x)\n", container_id);
		kfree(cont);
		return ERR_PTR(ret);
	}

	cont->container_id = ret;
	cont->sub_graph = sg;
	/* add to container list */
	list_add_tail(&cont->node, &sg->container_list);
	sg->num_containers++;

	return cont;
}

static struct audioreach_module *audioreach_tplg_alloc_module(struct q6apm *apm,
							      struct audioreach_container *cont,
							      struct snd_soc_dapm_widget *w,
							      uint32_t module_id, bool *found)
{
	struct audioreach_module *mod;
	int ret;

	mutex_lock(&apm->lock);
	mod = idr_find(&apm->modules_idr, module_id);
	mutex_unlock(&apm->lock);

	if (mod) {
		*found = true;
		return mod;
	}
	*found = false;
	mod = kzalloc(sizeof(*mod), GFP_KERNEL);
	if (!mod)
		return ERR_PTR(-ENOMEM);

	mutex_lock(&apm->lock);
	if (!module_id) { /* alloc module id dynamically */
		ret = idr_alloc_cyclic(&apm->modules_idr, mod,
				       AR_MODULE_DYNAMIC_INSTANCE_ID_START,
				       AR_MODULE_DYNAMIC_INSTANCE_ID_END, GFP_KERNEL);
	} else {
		ret = idr_alloc(&apm->modules_idr, mod, module_id, module_id + 1, GFP_KERNEL);
	}
	mutex_unlock(&apm->lock);

	if (ret < 0) {
		dev_err(apm->dev, "Failed to allocate Module Instance ID (%x)\n", module_id);
		kfree(mod);
		return ERR_PTR(ret);
	}

	mod->instance_id = ret;
	/* add to module list */
	list_add_tail(&mod->node, &cont->modules_list);
	mod->container = cont;
	mod->widget = w;
	cont->num_modules++;

	return mod;
}

static struct snd_soc_tplg_vendor_array *audioreach_get_sg_array(
							struct snd_soc_tplg_private *private)
{
	struct snd_soc_tplg_vendor_array *sg_array = NULL;
	bool found = false;
	int sz;

	for (sz = 0; !found && (sz < le32_to_cpu(private->size)); ) {
		struct snd_soc_tplg_vendor_value_elem *sg_elem;
		int tkn_count = 0;

		sg_array = (struct snd_soc_tplg_vendor_array *)((u8 *)private->array + sz);
		sg_elem = sg_array->value;
		sz = sz + le32_to_cpu(sg_array->size);
		while (!found && tkn_count <= (le32_to_cpu(sg_array->num_elems) - 1)) {
			switch (le32_to_cpu(sg_elem->token)) {
			case AR_TKN_U32_SUB_GRAPH_INSTANCE_ID:
				found = true;
				break;
			default:
				break;
			}
			tkn_count++;
			sg_elem++;
		}
	}

	if (found)
		return sg_array;

	return NULL;
}

static struct snd_soc_tplg_vendor_array *audioreach_get_cont_array(
							struct snd_soc_tplg_private *private)
{
	struct snd_soc_tplg_vendor_array *cont_array = NULL;
	bool found = false;
	int sz;

	for (sz = 0; !found && (sz < le32_to_cpu(private->size)); ) {
		struct snd_soc_tplg_vendor_value_elem *cont_elem;
		int tkn_count = 0;

		cont_array = (struct snd_soc_tplg_vendor_array *)((u8 *)private->array + sz);
		cont_elem = cont_array->value;
		sz = sz + le32_to_cpu(cont_array->size);
		while (!found && tkn_count <= (le32_to_cpu(cont_array->num_elems) - 1)) {
			switch (le32_to_cpu(cont_elem->token)) {
			case AR_TKN_U32_CONTAINER_INSTANCE_ID:
				found = true;
				break;
			default:
				break;
			}
			tkn_count++;
			cont_elem++;
		}
	}

	if (found)
		return cont_array;

	return NULL;
}

static struct snd_soc_tplg_vendor_array *audioreach_get_module_array(
							     struct snd_soc_tplg_private *private)
{
	struct snd_soc_tplg_vendor_array *mod_array = NULL;
	bool found = false;
	int sz = 0;

	for (sz = 0; !found && (sz < le32_to_cpu(private->size)); ) {
		struct snd_soc_tplg_vendor_value_elem *mod_elem;
		int tkn_count = 0;

		mod_array = (struct snd_soc_tplg_vendor_array *)((u8 *)private->array + sz);
		mod_elem = mod_array->value;
		sz = sz + le32_to_cpu(mod_array->size);
		while (!found && tkn_count <= (le32_to_cpu(mod_array->num_elems) - 1)) {
			switch (le32_to_cpu(mod_elem->token)) {
			case AR_TKN_U32_MODULE_INSTANCE_ID:
				found = true;
				break;
			default:
				break;
			}
			tkn_count++;
			mod_elem++;
		}
	}

	if (found)
		return mod_array;

	return NULL;
}

static struct audioreach_sub_graph *audioreach_parse_sg_tokens(struct q6apm *apm,
						       struct snd_soc_tplg_private *private)
{
	struct snd_soc_tplg_vendor_value_elem *sg_elem;
	struct snd_soc_tplg_vendor_array *sg_array;
	struct audioreach_graph_info *info = NULL;
	int graph_id, sub_graph_id, tkn_count = 0;
	struct audioreach_sub_graph *sg;
	bool found;

	sg_array = audioreach_get_sg_array(private);
	sg_elem = sg_array->value;

	while (tkn_count <= (le32_to_cpu(sg_array->num_elems) - 1)) {
		switch (le32_to_cpu(sg_elem->token)) {
		case AR_TKN_U32_SUB_GRAPH_INSTANCE_ID:
			sub_graph_id = le32_to_cpu(sg_elem->value);
			sg = audioreach_tplg_alloc_sub_graph(apm, sub_graph_id, &found);
			if (IS_ERR(sg)) {
				return sg;
			} else if (found) {
				/* Already parsed data for this sub-graph */
				return sg;
			}
			break;
		case AR_TKN_DAI_INDEX:
			/* Sub graph is associated with predefined graph */
			graph_id = le32_to_cpu(sg_elem->value);
			info = audioreach_tplg_alloc_graph_info(apm, graph_id, &found);
			if (IS_ERR(info))
				return ERR_CAST(info);
			break;
		case AR_TKN_U32_SUB_GRAPH_PERF_MODE:
			sg->perf_mode = le32_to_cpu(sg_elem->value);
			break;
		case AR_TKN_U32_SUB_GRAPH_DIRECTION:
			sg->direction = le32_to_cpu(sg_elem->value);
			break;
		case AR_TKN_U32_SUB_GRAPH_SCENARIO_ID:
			sg->scenario_id = le32_to_cpu(sg_elem->value);
			break;
		default:
			dev_err(apm->dev, "Not a valid token %d for graph\n", sg_elem->token);
			break;

		}
		tkn_count++;
		sg_elem++;
	}

	/* Sub graph is associated with predefined graph */
	if (info)
		audioreach_tplg_add_sub_graph(sg, info);

	return sg;
}

static struct audioreach_container *audioreach_parse_cont_tokens(struct q6apm *apm,
							 struct audioreach_sub_graph *sg,
							 struct snd_soc_tplg_private *private)
{
	struct snd_soc_tplg_vendor_value_elem *cont_elem;
	struct snd_soc_tplg_vendor_array *cont_array;
	struct audioreach_container *cont;
	int container_id, tkn_count = 0;
	bool found = false;

	cont_array = audioreach_get_cont_array(private);
	cont_elem = cont_array->value;

	while (tkn_count <= (le32_to_cpu(cont_array->num_elems) - 1)) {
		switch (le32_to_cpu(cont_elem->token)) {
		case AR_TKN_U32_CONTAINER_INSTANCE_ID:
			container_id = le32_to_cpu(cont_elem->value);
			cont = audioreach_tplg_alloc_container(apm, sg, container_id, &found);
			if (IS_ERR(cont) || found)/* Error or Already parsed container data */
				return cont;
			break;
		case AR_TKN_U32_CONTAINER_CAPABILITY_ID:
			cont->capability_id = le32_to_cpu(cont_elem->value);
			break;
		case AR_TKN_U32_CONTAINER_STACK_SIZE:
			cont->stack_size = le32_to_cpu(cont_elem->value);
			break;
		case AR_TKN_U32_CONTAINER_GRAPH_POS:
			cont->graph_pos = le32_to_cpu(cont_elem->value);
			break;
		case AR_TKN_U32_CONTAINER_PROC_DOMAIN:
			cont->proc_domain = le32_to_cpu(cont_elem->value);
			break;
		default:
			dev_err(apm->dev, "Not a valid token %d for graph\n", cont_elem->token);
			break;

		}
		tkn_count++;
		cont_elem++;
	}

	return cont;
}

static struct audioreach_module *audioreach_parse_common_tokens(struct q6apm *apm,
							struct audioreach_container *cont,
							struct snd_soc_tplg_private *private,
							struct snd_soc_dapm_widget *w)
{
	uint32_t max_ip_port = 0, max_op_port = 0, in_port = 0, out_port = 0;
	uint32_t src_mod_inst_id = 0, src_mod_op_port_id = 0;
	uint32_t dst_mod_inst_id = 0, dst_mod_ip_port_id = 0;
	int module_id = 0, instance_id = 0, tkn_count = 0;
	struct snd_soc_tplg_vendor_value_elem *mod_elem;
	struct snd_soc_tplg_vendor_array *mod_array;
	struct audioreach_module *mod = NULL;
	bool found;

	mod_array = audioreach_get_module_array(private);
	mod_elem = mod_array->value;

	while (tkn_count <= (le32_to_cpu(mod_array->num_elems) - 1)) {
		switch (le32_to_cpu(mod_elem->token)) {
		/* common module info */
		case AR_TKN_U32_MODULE_ID:
			module_id = le32_to_cpu(mod_elem->value);
			break;
		case AR_TKN_U32_MODULE_INSTANCE_ID:
			instance_id = le32_to_cpu(mod_elem->value);
			mod = audioreach_tplg_alloc_module(apm, cont, w,
							   instance_id, &found);
			if (IS_ERR(mod)) {
				return mod;
			} else if (found) {
				dev_err(apm->dev, "Duplicate Module Instance ID 0x%08x found\n",
					instance_id);
				return ERR_PTR(-EINVAL);
			}

			break;
		case AR_TKN_U32_MODULE_MAX_IP_PORTS:
			max_ip_port = le32_to_cpu(mod_elem->value);
			break;
		case AR_TKN_U32_MODULE_MAX_OP_PORTS:
			max_op_port = le32_to_cpu(mod_elem->value);
			break;
		case AR_TKN_U32_MODULE_IN_PORTS:
			in_port = le32_to_cpu(mod_elem->value);
			break;
		case AR_TKN_U32_MODULE_OUT_PORTS:
			out_port = le32_to_cpu(mod_elem->value);
			break;
		case AR_TKN_U32_MODULE_SRC_OP_PORT_ID:
			src_mod_op_port_id = le32_to_cpu(mod_elem->value);
			break;
		case AR_TKN_U32_MODULE_SRC_INSTANCE_ID:
			src_mod_inst_id = le32_to_cpu(mod_elem->value);
			break;
		case AR_TKN_U32_MODULE_DST_INSTANCE_ID:
			dst_mod_inst_id = le32_to_cpu(mod_elem->value);
			break;
		case AR_TKN_U32_MODULE_DST_IN_PORT_ID:
			dst_mod_ip_port_id = le32_to_cpu(mod_elem->value);
			break;
		default:
			break;

		}
		tkn_count++;
		mod_elem++;
	}

	if (mod) {
		mod->module_id = module_id;
		mod->max_ip_port = max_ip_port;
		mod->max_op_port = max_op_port;
		mod->in_port = in_port;
		mod->out_port = out_port;
		mod->src_mod_inst_id = src_mod_inst_id;
		mod->src_mod_op_port_id = src_mod_op_port_id;
		mod->dst_mod_inst_id = dst_mod_inst_id;
		mod->dst_mod_ip_port_id = dst_mod_ip_port_id;
	}

	return mod;
}

static int audioreach_widget_load_module_common(struct snd_soc_component *component,
						int index, struct snd_soc_dapm_widget *w,
						struct snd_soc_tplg_dapm_widget *tplg_w)
{
	struct q6apm *apm = dev_get_drvdata(component->dev);
	struct audioreach_container *cont;
	struct audioreach_sub_graph *sg;
	struct audioreach_module *mod;
	struct snd_soc_dobj *dobj;

	sg = audioreach_parse_sg_tokens(apm, &tplg_w->priv);
	if (IS_ERR(sg))
		return PTR_ERR(sg);

	cont = audioreach_parse_cont_tokens(apm, sg, &tplg_w->priv);
	if (IS_ERR(cont))
		return PTR_ERR(cont);

	mod = audioreach_parse_common_tokens(apm, cont, &tplg_w->priv, w);
	if (IS_ERR(mod))
		return PTR_ERR(mod);

	dobj = &w->dobj;
	dobj->private = mod;

	return 0;
}

static int audioreach_widget_load_enc_dec_cnv(struct snd_soc_component *component,
					      int index, struct snd_soc_dapm_widget *w,
					      struct snd_soc_tplg_dapm_widget *tplg_w)
{
	struct snd_soc_tplg_vendor_value_elem *mod_elem;
	struct snd_soc_tplg_vendor_array *mod_array;
	struct audioreach_module *mod;
	struct snd_soc_dobj *dobj;
	int tkn_count = 0;
	int ret;

	ret = audioreach_widget_load_module_common(component, index, w, tplg_w);
	if (ret)
		return ret;

	dobj = &w->dobj;
	mod = dobj->private;
	mod_array = audioreach_get_module_array(&tplg_w->priv);
	mod_elem = mod_array->value;

	while (tkn_count <= (le32_to_cpu(mod_array->num_elems) - 1)) {
		switch (le32_to_cpu(mod_elem->token)) {
		case AR_TKN_U32_MODULE_FMT_INTERLEAVE:
			mod->interleave_type = le32_to_cpu(mod_elem->value);
			break;
		case AR_TKN_U32_MODULE_FMT_SAMPLE_RATE:
			mod->rate = le32_to_cpu(mod_elem->value);
			break;
		case AR_TKN_U32_MODULE_FMT_BIT_DEPTH:
			mod->bit_depth = le32_to_cpu(mod_elem->value);
			break;
		default:
			break;
		}
		tkn_count++;
		mod_elem++;
	}

	return 0;
}

static int audioreach_widget_log_module_load(struct audioreach_module *mod,
					     struct snd_soc_tplg_vendor_array *mod_array)
{
	struct snd_soc_tplg_vendor_value_elem *mod_elem;
	int tkn_count = 0;

	mod_elem = mod_array->value;

	while (tkn_count <= (le32_to_cpu(mod_array->num_elems) - 1)) {
		switch (le32_to_cpu(mod_elem->token)) {

		case AR_TKN_U32_MODULE_LOG_CODE:
			mod->log_code = le32_to_cpu(mod_elem->value);
			break;
		case AR_TKN_U32_MODULE_LOG_TAP_POINT_ID:
			mod->log_tap_point_id = le32_to_cpu(mod_elem->value);
			break;
		case AR_TKN_U32_MODULE_LOG_MODE:
			mod->log_mode = le32_to_cpu(mod_elem->value);
			break;
		default:
			break;
		}
		tkn_count++;
		mod_elem++;
	}

	return 0;
}

static int audioreach_widget_dma_module_load(struct audioreach_module *mod,
					     struct snd_soc_tplg_vendor_array *mod_array)
{
	struct snd_soc_tplg_vendor_value_elem *mod_elem;
	int tkn_count = 0;

	mod_elem = mod_array->value;

	while (tkn_count <= (le32_to_cpu(mod_array->num_elems) - 1)) {
		switch (le32_to_cpu(mod_elem->token)) {
		case AR_TKN_U32_MODULE_HW_IF_IDX:
			mod->hw_interface_idx = le32_to_cpu(mod_elem->value);
			break;
		case AR_TKN_U32_MODULE_FMT_DATA:
			mod->data_format = le32_to_cpu(mod_elem->value);
			break;
		case AR_TKN_U32_MODULE_HW_IF_TYPE:
			mod->hw_interface_type = le32_to_cpu(mod_elem->value);
			break;
		default:
			break;
		}
		tkn_count++;
		mod_elem++;
	}

	return 0;
}

static int audioreach_widget_i2s_module_load(struct audioreach_module *mod,
					     struct snd_soc_tplg_vendor_array *mod_array)
{
	struct snd_soc_tplg_vendor_value_elem *mod_elem;
	int tkn_count = 0;

	mod_elem = mod_array->value;

	while (tkn_count <= (le32_to_cpu(mod_array->num_elems) - 1)) {
		switch (le32_to_cpu(mod_elem->token)) {
		case AR_TKN_U32_MODULE_HW_IF_IDX:
			mod->hw_interface_idx = le32_to_cpu(mod_elem->value);
			break;
		case AR_TKN_U32_MODULE_FMT_DATA:
			mod->data_format = le32_to_cpu(mod_elem->value);
			break;
		case AR_TKN_U32_MODULE_HW_IF_TYPE:
			mod->hw_interface_type = le32_to_cpu(mod_elem->value);
			break;
		case AR_TKN_U32_MODULE_SD_LINE_IDX:
			mod->sd_line_idx = le32_to_cpu(mod_elem->value);
			break;
		case AR_TKN_U32_MODULE_WS_SRC:
			mod->ws_src = le32_to_cpu(mod_elem->value);
			break;
		default:
			break;
		}
		tkn_count++;
		mod_elem++;
	}

	return 0;
}

static int audioreach_widget_load_buffer(struct snd_soc_component *component,
					 int index, struct snd_soc_dapm_widget *w,
					 struct snd_soc_tplg_dapm_widget *tplg_w)
{
	struct snd_soc_tplg_vendor_array *mod_array;
	struct audioreach_module *mod;
	struct snd_soc_dobj *dobj;
	int ret;

	ret = audioreach_widget_load_module_common(component, index, w, tplg_w);
	if (ret)
		return ret;

	dobj = &w->dobj;
	mod = dobj->private;

	mod_array = audioreach_get_module_array(&tplg_w->priv);

	switch (mod->module_id) {
	case MODULE_ID_CODEC_DMA_SINK:
	case MODULE_ID_CODEC_DMA_SOURCE:
		audioreach_widget_dma_module_load(mod, mod_array);
		break;
	case MODULE_ID_DATA_LOGGING:
		audioreach_widget_log_module_load(mod, mod_array);
		break;
	case MODULE_ID_I2S_SINK:
	case MODULE_ID_I2S_SOURCE:
		audioreach_widget_i2s_module_load(mod, mod_array);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int audioreach_widget_load_mixer(struct snd_soc_component *component,
					int index, struct snd_soc_dapm_widget *w,
					struct snd_soc_tplg_dapm_widget *tplg_w)
{
	struct snd_soc_tplg_vendor_value_elem *w_elem;
	struct snd_soc_tplg_vendor_array *w_array;
	struct snd_ar_control *scontrol;
	struct snd_soc_dobj *dobj;
	int tkn_count = 0;

	w_array = &tplg_w->priv.array[0];

	scontrol = kzalloc(sizeof(*scontrol), GFP_KERNEL);
	if (!scontrol)
		return -ENOMEM;

	scontrol->scomp = component;
	dobj = &w->dobj;
	dobj->private = scontrol;

	w_elem = w_array->value;
	while (tkn_count <= (le32_to_cpu(w_array->num_elems) - 1)) {
		switch (le32_to_cpu(w_elem->token)) {
		case AR_TKN_U32_SUB_GRAPH_INSTANCE_ID:
			scontrol->sgid = le32_to_cpu(w_elem->value);
			break;
		default: /* ignore other tokens */
			break;
		}
		tkn_count++;
		w_elem++;
	}

	return 0;
}

static int audioreach_pga_event(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *kcontrol, int event)

{
	struct snd_soc_dapm_context *dapm = w->dapm;
	struct snd_soc_component *c = snd_soc_dapm_to_component(dapm);
	struct audioreach_module *mod = w->dobj.private;
	struct q6apm *apm = dev_get_drvdata(c->dev);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		/* apply gain after power up of widget */
		audioreach_gain_set_vol_ctrl(apm, mod, mod->gain);
		break;
	default:
		break;
	}

	return 0;
}

static const struct snd_soc_tplg_widget_events audioreach_widget_ops[] = {
	{ AR_PGA_DAPM_EVENT, audioreach_pga_event },
};

static int audioreach_widget_load_pga(struct snd_soc_component *component,
				      int index, struct snd_soc_dapm_widget *w,
				      struct snd_soc_tplg_dapm_widget *tplg_w)
{
	struct audioreach_module *mod;
	struct snd_soc_dobj *dobj;
	int ret;

	ret = audioreach_widget_load_module_common(component, index, w, tplg_w);
	if (ret)
		return ret;

	dobj = &w->dobj;
	mod = dobj->private;
	mod->gain = VOL_CTRL_DEFAULT_GAIN;

	ret = snd_soc_tplg_widget_bind_event(w, audioreach_widget_ops,
					     ARRAY_SIZE(audioreach_widget_ops),
					     le16_to_cpu(tplg_w->event_type));
	if (ret) {
		dev_err(component->dev, "matching event handlers NOT found for %d\n",
			le16_to_cpu(tplg_w->event_type));
		return -EINVAL;
	}

	return 0;
}

static int audioreach_widget_ready(struct snd_soc_component *component,
				   int index, struct snd_soc_dapm_widget *w,
				   struct snd_soc_tplg_dapm_widget *tplg_w)
{
	switch (w->id) {
	case snd_soc_dapm_aif_in:
	case snd_soc_dapm_aif_out:
		audioreach_widget_load_buffer(component, index, w, tplg_w);
		break;
	case snd_soc_dapm_decoder:
	case snd_soc_dapm_encoder:
	case snd_soc_dapm_src:
		audioreach_widget_load_enc_dec_cnv(component, index, w, tplg_w);
		break;
	case snd_soc_dapm_buffer:
		audioreach_widget_load_buffer(component, index, w, tplg_w);
		break;
	case snd_soc_dapm_mixer:
		return audioreach_widget_load_mixer(component, index, w, tplg_w);
	case snd_soc_dapm_pga:
		return audioreach_widget_load_pga(component, index, w, tplg_w);
	case snd_soc_dapm_dai_link:
	case snd_soc_dapm_scheduler:
	case snd_soc_dapm_out_drv:
	default:
		dev_err(component->dev, "Widget type (0x%x) not yet supported\n", w->id);
		break;
	}

	return 0;
}

static int audioreach_widget_unload(struct snd_soc_component *scomp,
				    struct snd_soc_dobj *dobj)
{
	struct snd_soc_dapm_widget *w = container_of(dobj, struct snd_soc_dapm_widget, dobj);
	struct q6apm *apm = dev_get_drvdata(scomp->dev);
	struct audioreach_container *cont;
	struct audioreach_module *mod;

	mod = dobj->private;
	cont = mod->container;

	if (w->id == snd_soc_dapm_mixer) {
		/* virtual widget */
		kfree(dobj->private);
		return 0;
	}

	mutex_lock(&apm->lock);
	idr_remove(&apm->modules_idr, mod->instance_id);
	cont->num_modules--;

	list_del(&mod->node);
	kfree(mod);
	/* Graph Info has N sub-graphs, sub-graph has N containers, Container has N Modules */
	if (list_empty(&cont->modules_list)) { /* if no modules in the container then remove it */
		struct audioreach_sub_graph *sg = cont->sub_graph;

		idr_remove(&apm->containers_idr, cont->container_id);
		list_del(&cont->node);
		sg->num_containers--;
		kfree(cont);
		/* check if there are no more containers in the sub graph and remove it */
		if (list_empty(&sg->container_list)) {
			struct audioreach_graph_info *info = sg->info;

			idr_remove(&apm->sub_graphs_idr, sg->sub_graph_id);
			list_del(&sg->node);
			info->num_sub_graphs--;
			kfree(sg);
			/* Check if there are no more sub-graphs left then remove graph info */
			if (list_empty(&info->sg_list)) {
				idr_remove(&apm->graph_info_idr, info->id);
				kfree(info);
			}
		}
	}

	mutex_unlock(&apm->lock);

	return 0;
}

static struct audioreach_module *audioreach_find_widget(struct snd_soc_component *comp,
							const char *name)
{
	struct q6apm *apm = dev_get_drvdata(comp->dev);
	struct audioreach_module *module;
	int id;

	idr_for_each_entry(&apm->modules_idr, module, id) {
		if (!strcmp(name, module->widget->name))
			return module;
	}

	return NULL;
}

static int audioreach_route_load(struct snd_soc_component *scomp, int index,
				 struct snd_soc_dapm_route *route)
{
	struct audioreach_module *src, *sink;

	src = audioreach_find_widget(scomp, route->source);
	sink = audioreach_find_widget(scomp, route->sink);

	if (src && sink) {
		src->dst_mod_inst_id = sink->instance_id;
		sink->src_mod_inst_id = src->instance_id;
	}

	return 0;
}

static int audioreach_route_unload(struct snd_soc_component *scomp,
				   struct snd_soc_dobj *dobj)
{
	return 0;
}

static int audioreach_tplg_complete(struct snd_soc_component *component)
{
	/* TBD */
	return 0;
}

/* DAI link - used for any driver specific init */
static int audioreach_link_load(struct snd_soc_component *component, int index,
				struct snd_soc_dai_link *link,
				struct snd_soc_tplg_link_config *cfg)
{
	link->nonatomic = true;
	link->dynamic = true;
	link->platforms->name = NULL;
	link->platforms->of_node = of_get_compatible_child(component->dev->of_node,
							   "qcom,q6apm-dais");
	return 0;
}

static int audioreach_get_audio_mixer(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc = (struct soc_mixer_control *)kcontrol->private_value;
	struct snd_soc_dapm_context *dapm = snd_soc_dapm_kcontrol_dapm(kcontrol);
	struct snd_soc_dapm_widget *dw = snd_soc_dapm_kcontrol_widget(kcontrol);
	struct snd_soc_component *c = snd_soc_dapm_to_component(dapm);
	struct snd_ar_control *dapm_scontrol = dw->dobj.private;
	struct snd_ar_control *scontrol = mc->dobj.private;
	struct q6apm *data = dev_get_drvdata(c->dev);
	bool connected;

	connected = q6apm_is_sub_graphs_connected(data, scontrol->sgid, dapm_scontrol->sgid);
	if (connected)
		ucontrol->value.integer.value[0] = 1;
	else
		ucontrol->value.integer.value[0] = 0;

	return 0;
}

static int audioreach_put_audio_mixer(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc = (struct soc_mixer_control *)kcontrol->private_value;
	struct snd_soc_dapm_context *dapm = snd_soc_dapm_kcontrol_dapm(kcontrol);
	struct snd_soc_dapm_widget *dw = snd_soc_dapm_kcontrol_widget(kcontrol);
	struct snd_soc_component *c = snd_soc_dapm_to_component(dapm);
	struct snd_ar_control *dapm_scontrol = dw->dobj.private;
	struct snd_ar_control *scontrol = mc->dobj.private;
	struct q6apm *data = dev_get_drvdata(c->dev);

	if (ucontrol->value.integer.value[0]) {
		q6apm_connect_sub_graphs(data, scontrol->sgid, dapm_scontrol->sgid, true);
		snd_soc_dapm_mixer_update_power(dapm, kcontrol, 1, NULL);
	} else {
		q6apm_connect_sub_graphs(data, scontrol->sgid, dapm_scontrol->sgid, false);
		snd_soc_dapm_mixer_update_power(dapm, kcontrol, 0, NULL);
	}
	return 0;
}

static int audioreach_get_vol_ctrl_audio_mixer(struct snd_kcontrol *kcontrol,
					       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_widget *dw = snd_soc_dapm_kcontrol_widget(kcontrol);
	struct audioreach_module *mod = dw->dobj.private;

	ucontrol->value.integer.value[0] = mod->gain;

	return 0;
}

static int audioreach_put_vol_ctrl_audio_mixer(struct snd_kcontrol *kcontrol,
					       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_widget *dw = snd_soc_dapm_kcontrol_widget(kcontrol);
	struct audioreach_module *mod = dw->dobj.private;

	mod->gain = ucontrol->value.integer.value[0];

	return 1;
}

static int audioreach_control_load_mix(struct snd_soc_component *scomp,
				       struct snd_ar_control *scontrol,
				       struct snd_kcontrol_new *kc,
				       struct snd_soc_tplg_ctl_hdr *hdr)
{
	struct snd_soc_tplg_vendor_value_elem *c_elem;
	struct snd_soc_tplg_vendor_array *c_array;
	struct snd_soc_tplg_mixer_control *mc;
	int tkn_count = 0;

	mc = container_of(hdr, struct snd_soc_tplg_mixer_control, hdr);
	c_array = (struct snd_soc_tplg_vendor_array *)mc->priv.data;

	c_elem = c_array->value;

	while (tkn_count <= (le32_to_cpu(c_array->num_elems) - 1)) {
		switch (le32_to_cpu(c_elem->token)) {
		case AR_TKN_U32_SUB_GRAPH_INSTANCE_ID:
			scontrol->sgid = le32_to_cpu(c_elem->value);
			break;
		default:
			/* Ignore other tokens */
			break;
		}
		c_elem++;
		tkn_count++;
	}

	return 0;
}

static int audioreach_control_load(struct snd_soc_component *scomp, int index,
				   struct snd_kcontrol_new *kc,
				   struct snd_soc_tplg_ctl_hdr *hdr)
{
	struct snd_ar_control *scontrol;
	struct soc_mixer_control *sm;
	struct snd_soc_dobj *dobj;
	int ret = 0;

	scontrol = kzalloc(sizeof(*scontrol), GFP_KERNEL);
	if (!scontrol)
		return -ENOMEM;

	scontrol->scomp = scomp;

	switch (le32_to_cpu(hdr->ops.get)) {
	case SND_SOC_AR_TPLG_FE_BE_GRAPH_CTL_MIX:
		sm = (struct soc_mixer_control *)kc->private_value;
		dobj = &sm->dobj;
		ret = audioreach_control_load_mix(scomp, scontrol, kc, hdr);
		break;
	case SND_SOC_AR_TPLG_VOL_CTL:
		sm = (struct soc_mixer_control *)kc->private_value;
		dobj = &sm->dobj;
		break;
	default:
		dev_warn(scomp->dev, "control type not supported %d:%d:%d\n",
			 hdr->ops.get, hdr->ops.put, hdr->ops.info);
		kfree(scontrol);
		return -EINVAL;
	}

	dobj->private = scontrol;
	return ret;
}

static int audioreach_control_unload(struct snd_soc_component *scomp,
				     struct snd_soc_dobj *dobj)
{
	struct snd_ar_control *scontrol = dobj->private;

	kfree(scontrol);

	return 0;
}

static const struct snd_soc_tplg_kcontrol_ops audioreach_io_ops[] = {
	{SND_SOC_AR_TPLG_FE_BE_GRAPH_CTL_MIX, audioreach_get_audio_mixer,
		audioreach_put_audio_mixer, snd_soc_info_volsw},
	{SND_SOC_AR_TPLG_VOL_CTL, audioreach_get_vol_ctrl_audio_mixer,
		audioreach_put_vol_ctrl_audio_mixer, snd_soc_info_volsw},
};

static struct snd_soc_tplg_ops audioreach_tplg_ops  = {
	.io_ops = audioreach_io_ops,
	.io_ops_count = ARRAY_SIZE(audioreach_io_ops),

	.control_load	= audioreach_control_load,
	.control_unload	= audioreach_control_unload,

	.widget_ready = audioreach_widget_ready,
	.widget_unload = audioreach_widget_unload,

	.complete = audioreach_tplg_complete,
	.link_load = audioreach_link_load,

	.dapm_route_load	= audioreach_route_load,
	.dapm_route_unload	= audioreach_route_unload,
};

int audioreach_tplg_init(struct snd_soc_component *component)
{
	struct snd_soc_card *card = component->card;
	struct device *dev = component->dev;
	const struct firmware *fw;
	char *tplg_fw_name;
	int ret;

	/* Inline with Qualcomm UCM configs and linux-firmware path */
	tplg_fw_name = kasprintf(GFP_KERNEL, "qcom/%s/%s-tplg.bin", card->driver_name, card->name);
	if (!tplg_fw_name)
		return -ENOMEM;

	ret = request_firmware(&fw, tplg_fw_name, dev);
	if (ret < 0) {
		dev_err(dev, "tplg firmware loading %s failed %d \n", tplg_fw_name, ret);
		goto err;
	}

	ret = snd_soc_tplg_component_load(component, &audioreach_tplg_ops, fw);
	if (ret < 0) {
		dev_err(dev, "tplg component load failed%d\n", ret);
		ret = -EINVAL;
	}

	release_firmware(fw);
err:
	kfree(tplg_fw_name);

	return ret;
}
EXPORT_SYMBOL_GPL(audioreach_tplg_init);
