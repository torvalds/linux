// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright(c) 2021 Intel Corporation. All rights reserved.
//
// Authors: Cezary Rojewski <cezary.rojewski@intel.com>
//          Amadeusz Slawinski <amadeuszx.slawinski@linux.intel.com>
//

#include <sound/intel-nhlt.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include "avs.h"
#include "path.h"
#include "topology.h"

static bool avs_test_hw_params(struct snd_pcm_hw_params *params,
			       struct avs_audio_format *fmt)
{
	return (params_rate(params) == fmt->sampling_freq &&
		params_channels(params) == fmt->num_channels &&
		params_physical_width(params) == fmt->bit_depth &&
		params_width(params) == fmt->valid_bit_depth);
}

static struct avs_tplg_path *
avs_path_find_variant(struct avs_dev *adev,
		      struct avs_tplg_path_template *template,
		      struct snd_pcm_hw_params *fe_params,
		      struct snd_pcm_hw_params *be_params)
{
	struct avs_tplg_path *variant;

	list_for_each_entry(variant, &template->path_list, node) {
		dev_dbg(adev->dev, "check FE rate %d chn %d vbd %d bd %d\n",
			variant->fe_fmt->sampling_freq, variant->fe_fmt->num_channels,
			variant->fe_fmt->valid_bit_depth, variant->fe_fmt->bit_depth);
		dev_dbg(adev->dev, "check BE rate %d chn %d vbd %d bd %d\n",
			variant->be_fmt->sampling_freq, variant->be_fmt->num_channels,
			variant->be_fmt->valid_bit_depth, variant->be_fmt->bit_depth);

		if (variant->fe_fmt && avs_test_hw_params(fe_params, variant->fe_fmt) &&
		    variant->be_fmt && avs_test_hw_params(be_params, variant->be_fmt))
			return variant;
	}

	return NULL;
}

static void avs_path_module_free(struct avs_dev *adev, struct avs_path_module *mod)
{
	kfree(mod);
}

static struct avs_path_module *
avs_path_module_create(struct avs_dev *adev,
		       struct avs_path_pipeline *owner,
		       struct avs_tplg_module *template)
{
	struct avs_path_module *mod;
	int module_id;

	module_id = avs_get_module_id(adev, &template->cfg_ext->type);
	if (module_id < 0)
		return ERR_PTR(module_id);

	mod = kzalloc(sizeof(*mod), GFP_KERNEL);
	if (!mod)
		return ERR_PTR(-ENOMEM);

	mod->template = template;
	mod->module_id = module_id;
	mod->owner = owner;
	INIT_LIST_HEAD(&mod->node);

	return mod;
}

static void avs_path_binding_free(struct avs_dev *adev, struct avs_path_binding *binding)
{
	kfree(binding);
}

static struct avs_path_binding *avs_path_binding_create(struct avs_dev *adev,
							struct avs_path_pipeline *owner,
							struct avs_tplg_binding *t)
{
	struct avs_path_binding *binding;

	binding = kzalloc(sizeof(*binding), GFP_KERNEL);
	if (!binding)
		return ERR_PTR(-ENOMEM);

	binding->template = t;
	binding->owner = owner;
	INIT_LIST_HEAD(&binding->node);

	return binding;
}

static void avs_path_pipeline_free(struct avs_dev *adev,
				   struct avs_path_pipeline *ppl)
{
	struct avs_path_binding *binding, *bsave;
	struct avs_path_module *mod, *save;

	list_for_each_entry_safe(binding, bsave, &ppl->binding_list, node) {
		list_del(&binding->node);
		avs_path_binding_free(adev, binding);
	}

	avs_dsp_delete_pipeline(adev, ppl->instance_id);

	/* Unload resources occupied by owned modules */
	list_for_each_entry_safe(mod, save, &ppl->mod_list, node) {
		avs_dsp_delete_module(adev, mod->module_id, mod->instance_id,
				      mod->owner->instance_id,
				      mod->template->core_id);
		avs_path_module_free(adev, mod);
	}

	list_del(&ppl->node);
	kfree(ppl);
}

static struct avs_path_pipeline *
avs_path_pipeline_create(struct avs_dev *adev, struct avs_path *owner,
			 struct avs_tplg_pipeline *template)
{
	struct avs_path_pipeline *ppl;
	struct avs_tplg_pplcfg *cfg = template->cfg;
	struct avs_tplg_module *tmod;
	int ret, i;

	ppl = kzalloc(sizeof(*ppl), GFP_KERNEL);
	if (!ppl)
		return ERR_PTR(-ENOMEM);

	ppl->template = template;
	ppl->owner = owner;
	INIT_LIST_HEAD(&ppl->binding_list);
	INIT_LIST_HEAD(&ppl->mod_list);
	INIT_LIST_HEAD(&ppl->node);

	ret = avs_dsp_create_pipeline(adev, cfg->req_size, cfg->priority,
				      cfg->lp, cfg->attributes,
				      &ppl->instance_id);
	if (ret) {
		dev_err(adev->dev, "error creating pipeline %d\n", ret);
		kfree(ppl);
		return ERR_PTR(ret);
	}

	list_for_each_entry(tmod, &template->mod_list, node) {
		struct avs_path_module *mod;

		mod = avs_path_module_create(adev, ppl, tmod);
		if (IS_ERR(mod)) {
			ret = PTR_ERR(mod);
			dev_err(adev->dev, "error creating module %d\n", ret);
			goto init_err;
		}

		list_add_tail(&mod->node, &ppl->mod_list);
	}

	for (i = 0; i < template->num_bindings; i++) {
		struct avs_path_binding *binding;

		binding = avs_path_binding_create(adev, ppl, template->bindings[i]);
		if (IS_ERR(binding)) {
			ret = PTR_ERR(binding);
			dev_err(adev->dev, "error creating binding %d\n", ret);
			goto init_err;
		}

		list_add_tail(&binding->node, &ppl->binding_list);
	}

	return ppl;

init_err:
	avs_path_pipeline_free(adev, ppl);
	return ERR_PTR(ret);
}

static int avs_path_init(struct avs_dev *adev, struct avs_path *path,
			 struct avs_tplg_path *template, u32 dma_id)
{
	struct avs_tplg_pipeline *tppl;

	path->owner = adev;
	path->template = template;
	path->dma_id = dma_id;
	INIT_LIST_HEAD(&path->ppl_list);
	INIT_LIST_HEAD(&path->node);

	/* create all the pipelines */
	list_for_each_entry(tppl, &template->ppl_list, node) {
		struct avs_path_pipeline *ppl;

		ppl = avs_path_pipeline_create(adev, path, tppl);
		if (IS_ERR(ppl))
			return PTR_ERR(ppl);

		list_add_tail(&ppl->node, &path->ppl_list);
	}

	spin_lock(&adev->path_list_lock);
	list_add_tail(&path->node, &adev->path_list);
	spin_unlock(&adev->path_list_lock);

	return 0;
}

static void avs_path_free_unlocked(struct avs_path *path)
{
	struct avs_path_pipeline *ppl, *save;

	spin_lock(&path->owner->path_list_lock);
	list_del(&path->node);
	spin_unlock(&path->owner->path_list_lock);

	list_for_each_entry_safe(ppl, save, &path->ppl_list, node)
		avs_path_pipeline_free(path->owner, ppl);

	kfree(path);
}

static struct avs_path *avs_path_create_unlocked(struct avs_dev *adev, u32 dma_id,
						 struct avs_tplg_path *template)
{
	struct avs_path *path;
	int ret;

	path = kzalloc(sizeof(*path), GFP_KERNEL);
	if (!path)
		return ERR_PTR(-ENOMEM);

	ret = avs_path_init(adev, path, template, dma_id);
	if (ret < 0)
		goto err;

	path->state = AVS_PPL_STATE_INVALID;
	return path;
err:
	avs_path_free_unlocked(path);
	return ERR_PTR(ret);
}

void avs_path_free(struct avs_path *path)
{
	struct avs_dev *adev = path->owner;

	mutex_lock(&adev->path_mutex);
	avs_path_free_unlocked(path);
	mutex_unlock(&adev->path_mutex);
}

struct avs_path *avs_path_create(struct avs_dev *adev, u32 dma_id,
				 struct avs_tplg_path_template *template,
				 struct snd_pcm_hw_params *fe_params,
				 struct snd_pcm_hw_params *be_params)
{
	struct avs_tplg_path *variant;
	struct avs_path *path;

	variant = avs_path_find_variant(adev, template, fe_params, be_params);
	if (!variant) {
		dev_err(adev->dev, "no matching variant found\n");
		return ERR_PTR(-ENOENT);
	}

	/* Serialize path and its components creation. */
	mutex_lock(&adev->path_mutex);
	/* Satisfy needs of avs_path_find_tplg(). */
	mutex_lock(&adev->comp_list_mutex);

	path = avs_path_create_unlocked(adev, dma_id, variant);

	mutex_unlock(&adev->comp_list_mutex);
	mutex_unlock(&adev->path_mutex);

	return path;
}

int avs_path_bind(struct avs_path *path)
{
	struct avs_path_pipeline *ppl;
	struct avs_dev *adev = path->owner;
	int ret;

	list_for_each_entry(ppl, &path->ppl_list, node) {
		struct avs_path_binding *binding;

		list_for_each_entry(binding, &ppl->binding_list, node) {
			struct avs_path_module *source, *sink;

			source = binding->source;
			sink = binding->sink;

			ret = avs_ipc_bind(adev, source->module_id,
					   source->instance_id, sink->module_id,
					   sink->instance_id, binding->sink_pin,
					   binding->source_pin);
			if (ret) {
				dev_err(adev->dev, "bind path failed: %d\n", ret);
				return AVS_IPC_RET(ret);
			}
		}
	}

	return 0;
}

int avs_path_unbind(struct avs_path *path)
{
	struct avs_path_pipeline *ppl;
	struct avs_dev *adev = path->owner;
	int ret;

	list_for_each_entry(ppl, &path->ppl_list, node) {
		struct avs_path_binding *binding;

		list_for_each_entry(binding, &ppl->binding_list, node) {
			struct avs_path_module *source, *sink;

			source = binding->source;
			sink = binding->sink;

			ret = avs_ipc_unbind(adev, source->module_id,
					     source->instance_id, sink->module_id,
					     sink->instance_id, binding->sink_pin,
					     binding->source_pin);
			if (ret) {
				dev_err(adev->dev, "unbind path failed: %d\n", ret);
				return AVS_IPC_RET(ret);
			}
		}
	}

	return 0;
}

int avs_path_reset(struct avs_path *path)
{
	struct avs_path_pipeline *ppl;
	struct avs_dev *adev = path->owner;
	int ret;

	if (path->state == AVS_PPL_STATE_RESET)
		return 0;

	list_for_each_entry(ppl, &path->ppl_list, node) {
		ret = avs_ipc_set_pipeline_state(adev, ppl->instance_id,
						 AVS_PPL_STATE_RESET);
		if (ret) {
			dev_err(adev->dev, "reset path failed: %d\n", ret);
			path->state = AVS_PPL_STATE_INVALID;
			return AVS_IPC_RET(ret);
		}
	}

	path->state = AVS_PPL_STATE_RESET;
	return 0;
}

int avs_path_pause(struct avs_path *path)
{
	struct avs_path_pipeline *ppl;
	struct avs_dev *adev = path->owner;
	int ret;

	if (path->state == AVS_PPL_STATE_PAUSED)
		return 0;

	list_for_each_entry_reverse(ppl, &path->ppl_list, node) {
		ret = avs_ipc_set_pipeline_state(adev, ppl->instance_id,
						 AVS_PPL_STATE_PAUSED);
		if (ret) {
			dev_err(adev->dev, "pause path failed: %d\n", ret);
			path->state = AVS_PPL_STATE_INVALID;
			return AVS_IPC_RET(ret);
		}
	}

	path->state = AVS_PPL_STATE_PAUSED;
	return 0;
}

int avs_path_run(struct avs_path *path, int trigger)
{
	struct avs_path_pipeline *ppl;
	struct avs_dev *adev = path->owner;
	int ret;

	if (path->state == AVS_PPL_STATE_RUNNING && trigger == AVS_TPLG_TRIGGER_AUTO)
		return 0;

	list_for_each_entry(ppl, &path->ppl_list, node) {
		if (ppl->template->cfg->trigger != trigger)
			continue;

		ret = avs_ipc_set_pipeline_state(adev, ppl->instance_id,
						 AVS_PPL_STATE_RUNNING);
		if (ret) {
			dev_err(adev->dev, "run path failed: %d\n", ret);
			path->state = AVS_PPL_STATE_INVALID;
			return AVS_IPC_RET(ret);
		}
	}

	path->state = AVS_PPL_STATE_RUNNING;
	return 0;
}
