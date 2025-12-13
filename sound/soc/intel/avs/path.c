// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright(c) 2021 Intel Corporation
//
// Authors: Cezary Rojewski <cezary.rojewski@intel.com>
//          Amadeusz Slawinski <amadeuszx.slawinski@linux.intel.com>
//

#include <linux/acpi.h>
#include <acpi/nhlt.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include "avs.h"
#include "control.h"
#include "path.h"
#include "topology.h"

/* Must be called with adev->comp_list_mutex held. */
static struct avs_tplg *
avs_path_find_tplg(struct avs_dev *adev, const char *name)
{
	struct avs_soc_component *acomp;

	list_for_each_entry(acomp, &adev->comp_list, node)
		if (!strcmp(acomp->tplg->name, name))
			return acomp->tplg;
	return NULL;
}

static struct avs_path_module *
avs_path_find_module(struct avs_path_pipeline *ppl, u32 template_id)
{
	struct avs_path_module *mod;

	list_for_each_entry(mod, &ppl->mod_list, node)
		if (mod->template->id == template_id)
			return mod;
	return NULL;
}

static struct avs_path_pipeline *
avs_path_find_pipeline(struct avs_path *path, u32 template_id)
{
	struct avs_path_pipeline *ppl;

	list_for_each_entry(ppl, &path->ppl_list, node)
		if (ppl->template->id == template_id)
			return ppl;
	return NULL;
}

static struct avs_path *
avs_path_find_path(struct avs_dev *adev, const char *name, u32 template_id)
{
	struct avs_tplg_path_template *pos, *template = NULL;
	struct avs_tplg *tplg;
	struct avs_path *path;

	tplg = avs_path_find_tplg(adev, name);
	if (!tplg)
		return NULL;

	list_for_each_entry(pos, &tplg->path_tmpl_list, node) {
		if (pos->id == template_id) {
			template = pos;
			break;
		}
	}
	if (!template)
		return NULL;

	spin_lock(&adev->path_list_lock);
	/* Only one variant of given path template may be instantiated at a time. */
	list_for_each_entry(path, &adev->path_list, node) {
		if (path->template->owner == template) {
			spin_unlock(&adev->path_list_lock);
			return path;
		}
	}

	spin_unlock(&adev->path_list_lock);
	return NULL;
}

static bool avs_test_hw_params(struct snd_pcm_hw_params *params,
			       struct avs_audio_format *fmt)
{
	return (params_rate(params) == fmt->sampling_freq &&
		params_channels(params) == fmt->num_channels &&
		params_physical_width(params) == fmt->bit_depth &&
		snd_pcm_hw_params_bits(params) == fmt->valid_bit_depth);
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

static struct avs_tplg_path *avs_condpath_find_variant(struct avs_dev *adev,
						       struct avs_tplg_path_template *template,
						       struct avs_path *source,
						       struct avs_path *sink)
{
	struct avs_tplg_path *variant;

	list_for_each_entry(variant, &template->path_list, node) {
		if (variant->source_path_id == source->template->id &&
		    variant->sink_path_id == sink->template->id)
			return variant;
	}

	return NULL;
}

static bool avs_tplg_path_template_id_equal(struct avs_tplg_path_template_id *id,
					    struct avs_tplg_path_template_id *id2)
{
	return id->id == id2->id && !strcmp(id->tplg_name, id2->tplg_name);
}

static struct avs_path *avs_condpath_find_match(struct avs_dev *adev,
						struct avs_tplg_path_template *template,
						struct avs_path *path, int dir)
{
	struct avs_tplg_path_template_id *id, *id2;

	if (dir) {
		id = &template->source;
		id2 = &template->sink;
	} else {
		id = &template->sink;
		id2 = &template->source;
	}

	/* Check whether this path is either source or sink of condpath template. */
	if (id->id != path->template->owner->id ||
	    strcmp(id->tplg_name, path->template->owner->owner->name))
		return NULL;

	/* Unidirectional condpaths are allowed. */
	if (avs_tplg_path_template_id_equal(id, id2))
		return path;

	/* Now find the counterpart. */
	return avs_path_find_path(adev, id2->tplg_name, id2->id);
}

static struct acpi_nhlt_config *
avs_nhlt_config_or_default(struct avs_dev *adev, struct avs_tplg_module *t);

int avs_path_set_constraint(struct avs_dev *adev, struct avs_tplg_path_template *template,
			    struct snd_pcm_hw_constraint_list *rate_list,
			    struct snd_pcm_hw_constraint_list *channels_list,
			    struct snd_pcm_hw_constraint_list *sample_bits_list)
{
	struct avs_tplg_path *path_template;
	unsigned int *rlist, *clist, *slist;
	size_t i;

	i = 0;
	list_for_each_entry(path_template, &template->path_list, node)
		i++;

	rlist = kcalloc(i, sizeof(*rlist), GFP_KERNEL);
	clist = kcalloc(i, sizeof(*clist), GFP_KERNEL);
	slist = kcalloc(i, sizeof(*slist), GFP_KERNEL);
	if (!rlist || !clist || !slist)
		return -ENOMEM;

	i = 0;
	list_for_each_entry(path_template, &template->path_list, node) {
		struct avs_tplg_pipeline *pipeline_template;

		list_for_each_entry(pipeline_template, &path_template->ppl_list, node) {
			struct avs_tplg_module *module_template;

			list_for_each_entry(module_template, &pipeline_template->mod_list, node) {
				const guid_t *type = &module_template->cfg_ext->type;
				struct acpi_nhlt_config *blob;

				if (!guid_equal(type, &AVS_COPIER_MOD_UUID) &&
				    !guid_equal(type, &AVS_WOVHOSTM_MOD_UUID))
					continue;

				switch (module_template->cfg_ext->copier.dma_type) {
				case AVS_DMA_DMIC_LINK_INPUT:
				case AVS_DMA_I2S_LINK_OUTPUT:
				case AVS_DMA_I2S_LINK_INPUT:
					break;
				default:
					continue;
				}

				blob = avs_nhlt_config_or_default(adev, module_template);
				if (IS_ERR(blob))
					continue;

				rlist[i] = path_template->fe_fmt->sampling_freq;
				clist[i] = path_template->fe_fmt->num_channels;
				slist[i] = path_template->fe_fmt->bit_depth;
				i++;
			}
		}
	}

	if (i) {
		rate_list->count = i;
		rate_list->list = rlist;
		channels_list->count = i;
		channels_list->list = clist;
		sample_bits_list->count = i;
		sample_bits_list->list = slist;
	} else {
		kfree(rlist);
		kfree(clist);
		kfree(slist);
	}

	return i;
}

static void avs_init_node_id(union avs_connector_node_id *node_id,
			     struct avs_tplg_modcfg_ext *te, u32 dma_id)
{
	node_id->val = 0;
	node_id->dma_type = te->copier.dma_type;

	switch (node_id->dma_type) {
	case AVS_DMA_DMIC_LINK_INPUT:
	case AVS_DMA_I2S_LINK_OUTPUT:
	case AVS_DMA_I2S_LINK_INPUT:
		/* Gateway's virtual index is statically assigned in the topology. */
		node_id->vindex = te->copier.vindex.val;
		break;

	case AVS_DMA_HDA_HOST_OUTPUT:
	case AVS_DMA_HDA_HOST_INPUT:
		/* Gateway's virtual index is dynamically assigned with DMA ID */
		node_id->vindex = dma_id;
		break;

	case AVS_DMA_HDA_LINK_OUTPUT:
	case AVS_DMA_HDA_LINK_INPUT:
		node_id->vindex = te->copier.vindex.val | dma_id;
		break;

	default:
		*node_id = INVALID_NODE_ID;
		break;
	}
}

/* Every BLOB contains at least gateway attributes. */
static struct acpi_nhlt_config *default_blob = (struct acpi_nhlt_config *)&(u32[2]) {4};

static struct acpi_nhlt_config *
avs_nhlt_config_or_default(struct avs_dev *adev, struct avs_tplg_module *t)
{
	struct acpi_nhlt_format_config *fmtcfg;
	struct avs_tplg_modcfg_ext *te;
	struct avs_audio_format *fmt;
	int link_type, dev_type;
	int bus_id, dir;

	te = t->cfg_ext;

	switch (te->copier.dma_type) {
	case AVS_DMA_I2S_LINK_OUTPUT:
		link_type = ACPI_NHLT_LINKTYPE_SSP;
		dev_type = ACPI_NHLT_DEVICETYPE_CODEC;
		bus_id = te->copier.vindex.i2s.instance;
		dir = SNDRV_PCM_STREAM_PLAYBACK;
		fmt = te->copier.out_fmt;
		break;

	case AVS_DMA_I2S_LINK_INPUT:
		link_type = ACPI_NHLT_LINKTYPE_SSP;
		dev_type = ACPI_NHLT_DEVICETYPE_CODEC;
		bus_id = te->copier.vindex.i2s.instance;
		dir = SNDRV_PCM_STREAM_CAPTURE;
		fmt = t->in_fmt;
		break;

	case AVS_DMA_DMIC_LINK_INPUT:
		link_type = ACPI_NHLT_LINKTYPE_PDM;
		dev_type = -1; /* ignored */
		bus_id = 0;
		dir = SNDRV_PCM_STREAM_CAPTURE;
		fmt = t->in_fmt;
		break;

	default:
		return default_blob;
	}

	/* Override format selection if necessary. */
	if (te->copier.blob_fmt)
		fmt = te->copier.blob_fmt;

	fmtcfg = acpi_nhlt_find_fmtcfg(link_type, dev_type, dir, bus_id,
				       fmt->num_channels, fmt->sampling_freq, fmt->valid_bit_depth,
				       fmt->bit_depth);
	if (!fmtcfg) {
		dev_warn(adev->dev, "Endpoint format configuration not found.\n");
		return ERR_PTR(-ENOENT);
	}

	if (fmtcfg->config.capabilities_size < default_blob->capabilities_size)
		return ERR_PTR(-ETOOSMALL);
	/* The firmware expects the payload to be DWORD-aligned. */
	if (fmtcfg->config.capabilities_size % sizeof(u32))
		return ERR_PTR(-EINVAL);

	return &fmtcfg->config;
}

static int avs_append_dma_cfg(struct avs_dev *adev, struct avs_copier_gtw_cfg *gtw,
			      struct avs_tplg_module *t, u32 dma_id, size_t *cfg_size)
{
	u32 dma_type = t->cfg_ext->copier.dma_type;
	struct avs_dma_cfg *dma;
	struct avs_tlv *tlv;
	size_t tlv_size;

	if (!avs_platattr_test(adev, ALTHDA))
		return 0;

	switch (dma_type) {
	case AVS_DMA_HDA_HOST_OUTPUT:
	case AVS_DMA_HDA_HOST_INPUT:
	case AVS_DMA_HDA_LINK_OUTPUT:
	case AVS_DMA_HDA_LINK_INPUT:
		return 0;
	default:
		break;
	}

	tlv_size = sizeof(*tlv) + sizeof(*dma);
	if (*cfg_size + tlv_size > AVS_MAILBOX_SIZE)
		return -E2BIG;

	/* DMA config is a TLV tailing the existing payload. */
	tlv = (struct avs_tlv *)&gtw->config.blob[gtw->config_length];
	tlv->type = AVS_GTW_DMA_CONFIG_ID;
	tlv->length = sizeof(*dma);

	dma = (struct avs_dma_cfg *)tlv->value;
	memset(dma, 0, sizeof(*dma));
	dma->dma_method = AVS_DMA_METHOD_HDA;
	dma->pre_allocated = true;
	dma->dma_channel_id = dma_id;
	dma->stream_id = dma_id + 1;

	gtw->config_length += tlv_size / sizeof(u32);
	*cfg_size += tlv_size;

	return 0;
}

static int avs_fill_gtw_config(struct avs_dev *adev, struct avs_copier_gtw_cfg *gtw,
			       struct avs_tplg_module *t, u32 dma_id, size_t *cfg_size)
{
	struct acpi_nhlt_config *blob;
	size_t gtw_size;

	blob = avs_nhlt_config_or_default(adev, t);
	if (IS_ERR(blob))
		return PTR_ERR(blob);

	gtw_size = blob->capabilities_size;
	if (*cfg_size + gtw_size > AVS_MAILBOX_SIZE)
		return -E2BIG;

	gtw->config_length = gtw_size / sizeof(u32);
	memcpy(gtw->config.blob, blob->capabilities, blob->capabilities_size);
	*cfg_size += gtw_size;

	return avs_append_dma_cfg(adev, gtw, t, dma_id, cfg_size);
}

static int avs_copier_create(struct avs_dev *adev, struct avs_path_module *mod)
{
	struct avs_tplg_module *t = mod->template;
	struct avs_tplg_modcfg_ext *te;
	struct avs_copier_cfg *cfg;
	size_t cfg_size;
	u32 dma_id;
	int ret;

	te = t->cfg_ext;
	cfg = adev->modcfg_buf;
	dma_id = mod->owner->owner->dma_id;
	cfg_size = offsetof(struct avs_copier_cfg, gtw_cfg.config);

	ret = avs_fill_gtw_config(adev, &cfg->gtw_cfg, t, dma_id, &cfg_size);
	if (ret)
		return ret;

	cfg->base.cpc = t->cfg_base->cpc;
	cfg->base.ibs = t->cfg_base->ibs;
	cfg->base.obs = t->cfg_base->obs;
	cfg->base.is_pages = t->cfg_base->is_pages;
	cfg->base.audio_fmt = *t->in_fmt;
	cfg->out_fmt = *te->copier.out_fmt;
	cfg->feature_mask = te->copier.feature_mask;
	avs_init_node_id(&cfg->gtw_cfg.node_id, te, dma_id);
	cfg->gtw_cfg.dma_buffer_size = te->copier.dma_buffer_size;
	mod->gtw_attrs = cfg->gtw_cfg.config.attrs;

	ret = avs_dsp_init_module(adev, mod->module_id, mod->owner->instance_id, t->core_id,
				  t->domain, cfg, cfg_size, &mod->instance_id);
	return ret;
}

static int avs_whm_create(struct avs_dev *adev, struct avs_path_module *mod)
{
	struct avs_tplg_module *t = mod->template;
	struct avs_tplg_modcfg_ext *te;
	struct avs_whm_cfg *cfg;
	size_t cfg_size;
	u32 dma_id;
	int ret;

	te = t->cfg_ext;
	cfg = adev->modcfg_buf;
	dma_id = mod->owner->owner->dma_id;
	cfg_size = offsetof(struct avs_whm_cfg, gtw_cfg.config);

	ret = avs_fill_gtw_config(adev, &cfg->gtw_cfg, t, dma_id, &cfg_size);
	if (ret)
		return ret;

	cfg->base.cpc = t->cfg_base->cpc;
	cfg->base.ibs = t->cfg_base->ibs;
	cfg->base.obs = t->cfg_base->obs;
	cfg->base.is_pages = t->cfg_base->is_pages;
	cfg->base.audio_fmt = *t->in_fmt;
	cfg->ref_fmt = *te->whm.ref_fmt;
	cfg->out_fmt = *te->whm.out_fmt;
	cfg->wake_tick_period = te->whm.wake_tick_period;
	avs_init_node_id(&cfg->gtw_cfg.node_id, te, dma_id);
	cfg->gtw_cfg.dma_buffer_size = te->whm.dma_buffer_size;
	mod->gtw_attrs = cfg->gtw_cfg.config.attrs;

	ret = avs_dsp_init_module(adev, mod->module_id, mod->owner->instance_id, t->core_id,
				  t->domain, cfg, cfg_size, &mod->instance_id);
	return ret;
}

static struct soc_mixer_control *avs_get_module_control(struct avs_path_module *mod,
							const char *name)
{
	struct avs_tplg_module *t = mod->template;
	struct avs_tplg_path_template *path_tmpl;
	struct snd_soc_dapm_widget *w;
	int i;

	path_tmpl = t->owner->owner->owner;
	w = path_tmpl->w;

	for (i = 0; i < w->num_kcontrols; i++) {
		struct avs_control_data *ctl_data;
		struct soc_mixer_control *mc;

		mc = (struct soc_mixer_control *)w->kcontrols[i]->private_value;
		ctl_data = (struct avs_control_data *)mc->dobj.private;
		if (ctl_data->id == t->ctl_id && strstr(w->kcontrols[i]->id.name, name))
			return mc;
	}

	return NULL;
}

int avs_peakvol_set_volume(struct avs_dev *adev, struct avs_path_module *mod,
			   struct soc_mixer_control *mc, long *input)
{
	struct avs_volume_cfg vols[SND_SOC_TPLG_MAX_CHAN] = {{0}};
	struct avs_control_data *ctl_data;
	struct avs_tplg_module *t;
	int ret, i;

	ctl_data = mc->dobj.private;
	t = mod->template;
	if (!input)
		input = ctl_data->values;

	if (mc->num_channels) {
		for (i = 0; i < mc->num_channels; i++) {
			vols[i].channel_id = i;
			vols[i].target_volume = input[i];
			vols[i].curve_type = t->cfg_ext->peakvol.curve_type;
			vols[i].curve_duration = t->cfg_ext->peakvol.curve_duration;
		}

		ret = avs_ipc_peakvol_set_volumes(adev, mod->module_id, mod->instance_id, vols,
						  mc->num_channels);
		return AVS_IPC_RET(ret);
	}

	/* Target all channels if no individual selected. */
	vols[0].channel_id = AVS_ALL_CHANNELS_MASK;
	vols[0].target_volume = input[0];
	vols[0].curve_type = t->cfg_ext->peakvol.curve_type;
	vols[0].curve_duration = t->cfg_ext->peakvol.curve_duration;

	ret = avs_ipc_peakvol_set_volume(adev, mod->module_id, mod->instance_id, &vols[0]);
	return AVS_IPC_RET(ret);
}

int avs_peakvol_set_mute(struct avs_dev *adev, struct avs_path_module *mod,
			 struct soc_mixer_control *mc, long *input)
{
	struct avs_mute_cfg mutes[SND_SOC_TPLG_MAX_CHAN] = {{0}};
	struct avs_control_data *ctl_data;
	struct avs_tplg_module *t;
	int ret, i;

	ctl_data = mc->dobj.private;
	t = mod->template;
	if (!input)
		input = ctl_data->values;

	if (mc->num_channels) {
		for (i = 0; i < mc->num_channels; i++) {
			mutes[i].channel_id = i;
			mutes[i].mute = !input[i];
			mutes[i].curve_type = t->cfg_ext->peakvol.curve_type;
			mutes[i].curve_duration = t->cfg_ext->peakvol.curve_duration;
		}

		ret = avs_ipc_peakvol_set_mutes(adev, mod->module_id, mod->instance_id, mutes,
						mc->num_channels);
		return AVS_IPC_RET(ret);
	}

	/* Target all channels if no individual selected. */
	mutes[0].channel_id = AVS_ALL_CHANNELS_MASK;
	mutes[0].mute = !input[0];
	mutes[0].curve_type = t->cfg_ext->peakvol.curve_type;
	mutes[0].curve_duration = t->cfg_ext->peakvol.curve_duration;

	ret = avs_ipc_peakvol_set_mute(adev, mod->module_id, mod->instance_id, &mutes[0]);
	return AVS_IPC_RET(ret);
}

static int avs_peakvol_create(struct avs_dev *adev, struct avs_path_module *mod)
{
	struct avs_tplg_module *t = mod->template;
	struct soc_mixer_control *mc;
	struct avs_peakvol_cfg *cfg;
	size_t cfg_size;
	int ret;

	cfg_size = struct_size(cfg, vols, 1);
	if (cfg_size > AVS_MAILBOX_SIZE)
		return -EINVAL;

	cfg = adev->modcfg_buf;
	memset(cfg, 0, cfg_size);
	cfg->base.cpc = t->cfg_base->cpc;
	cfg->base.ibs = t->cfg_base->ibs;
	cfg->base.obs = t->cfg_base->obs;
	cfg->base.is_pages = t->cfg_base->is_pages;
	cfg->base.audio_fmt = *t->in_fmt;
	cfg->vols[0].channel_id = AVS_ALL_CHANNELS_MASK;
	cfg->vols[0].target_volume = S32_MAX;
	cfg->vols[0].curve_type = t->cfg_ext->peakvol.curve_type;
	cfg->vols[0].curve_duration = t->cfg_ext->peakvol.curve_duration;

	ret = avs_dsp_init_module(adev, mod->module_id, mod->owner->instance_id, t->core_id,
				  t->domain, cfg, cfg_size, &mod->instance_id);
	if (ret)
		return ret;

	/* Now configure both VOLUME and MUTE parameters. */
	mc = avs_get_module_control(mod, "Volume");
	if (mc) {
		ret = avs_peakvol_set_volume(adev, mod, mc, NULL);
		if (ret)
			return ret;
	}

	mc = avs_get_module_control(mod, "Switch");
	if (mc)
		return avs_peakvol_set_mute(adev, mod, mc, NULL);
	return 0;
}

static int avs_updown_mix_create(struct avs_dev *adev, struct avs_path_module *mod)
{
	struct avs_tplg_module *t = mod->template;
	struct avs_updown_mixer_cfg cfg;
	int i;

	cfg.base.cpc = t->cfg_base->cpc;
	cfg.base.ibs = t->cfg_base->ibs;
	cfg.base.obs = t->cfg_base->obs;
	cfg.base.is_pages = t->cfg_base->is_pages;
	cfg.base.audio_fmt = *t->in_fmt;
	cfg.out_channel_config = t->cfg_ext->updown_mix.out_channel_config;
	cfg.coefficients_select = t->cfg_ext->updown_mix.coefficients_select;
	for (i = 0; i < AVS_COEFF_CHANNELS_MAX; i++)
		cfg.coefficients[i] = t->cfg_ext->updown_mix.coefficients[i];
	cfg.channel_map = t->cfg_ext->updown_mix.channel_map;

	return avs_dsp_init_module(adev, mod->module_id, mod->owner->instance_id,
				   t->core_id, t->domain, &cfg, sizeof(cfg),
				   &mod->instance_id);
}

static int avs_src_create(struct avs_dev *adev, struct avs_path_module *mod)
{
	struct avs_tplg_module *t = mod->template;
	struct avs_src_cfg cfg;

	cfg.base.cpc = t->cfg_base->cpc;
	cfg.base.ibs = t->cfg_base->ibs;
	cfg.base.obs = t->cfg_base->obs;
	cfg.base.is_pages = t->cfg_base->is_pages;
	cfg.base.audio_fmt = *t->in_fmt;
	cfg.out_freq = t->cfg_ext->src.out_freq;

	return avs_dsp_init_module(adev, mod->module_id, mod->owner->instance_id,
				   t->core_id, t->domain, &cfg, sizeof(cfg),
				   &mod->instance_id);
}

static int avs_asrc_create(struct avs_dev *adev, struct avs_path_module *mod)
{
	struct avs_tplg_module *t = mod->template;
	struct avs_asrc_cfg cfg;

	memset(&cfg, 0, sizeof(cfg));
	cfg.base.cpc = t->cfg_base->cpc;
	cfg.base.ibs = t->cfg_base->ibs;
	cfg.base.obs = t->cfg_base->obs;
	cfg.base.is_pages = t->cfg_base->is_pages;
	cfg.base.audio_fmt = *t->in_fmt;
	cfg.out_freq = t->cfg_ext->asrc.out_freq;
	cfg.mode = t->cfg_ext->asrc.mode;
	cfg.disable_jitter_buffer = t->cfg_ext->asrc.disable_jitter_buffer;

	return avs_dsp_init_module(adev, mod->module_id, mod->owner->instance_id,
				   t->core_id, t->domain, &cfg, sizeof(cfg),
				   &mod->instance_id);
}

static int avs_aec_create(struct avs_dev *adev, struct avs_path_module *mod)
{
	struct avs_tplg_module *t = mod->template;
	struct avs_aec_cfg cfg;

	cfg.base.cpc = t->cfg_base->cpc;
	cfg.base.ibs = t->cfg_base->ibs;
	cfg.base.obs = t->cfg_base->obs;
	cfg.base.is_pages = t->cfg_base->is_pages;
	cfg.base.audio_fmt = *t->in_fmt;
	cfg.ref_fmt = *t->cfg_ext->aec.ref_fmt;
	cfg.out_fmt = *t->cfg_ext->aec.out_fmt;
	cfg.cpc_lp_mode = t->cfg_ext->aec.cpc_lp_mode;

	return avs_dsp_init_module(adev, mod->module_id, mod->owner->instance_id,
				   t->core_id, t->domain, &cfg, sizeof(cfg),
				   &mod->instance_id);
}

static int avs_mux_create(struct avs_dev *adev, struct avs_path_module *mod)
{
	struct avs_tplg_module *t = mod->template;
	struct avs_mux_cfg cfg;

	cfg.base.cpc = t->cfg_base->cpc;
	cfg.base.ibs = t->cfg_base->ibs;
	cfg.base.obs = t->cfg_base->obs;
	cfg.base.is_pages = t->cfg_base->is_pages;
	cfg.base.audio_fmt = *t->in_fmt;
	cfg.ref_fmt = *t->cfg_ext->mux.ref_fmt;
	cfg.out_fmt = *t->cfg_ext->mux.out_fmt;

	return avs_dsp_init_module(adev, mod->module_id, mod->owner->instance_id,
				   t->core_id, t->domain, &cfg, sizeof(cfg),
				   &mod->instance_id);
}

static int avs_wov_create(struct avs_dev *adev, struct avs_path_module *mod)
{
	struct avs_tplg_module *t = mod->template;
	struct avs_wov_cfg cfg;

	cfg.base.cpc = t->cfg_base->cpc;
	cfg.base.ibs = t->cfg_base->ibs;
	cfg.base.obs = t->cfg_base->obs;
	cfg.base.is_pages = t->cfg_base->is_pages;
	cfg.base.audio_fmt = *t->in_fmt;
	cfg.cpc_lp_mode = t->cfg_ext->wov.cpc_lp_mode;

	return avs_dsp_init_module(adev, mod->module_id, mod->owner->instance_id,
				   t->core_id, t->domain, &cfg, sizeof(cfg),
				   &mod->instance_id);
}

static int avs_micsel_create(struct avs_dev *adev, struct avs_path_module *mod)
{
	struct avs_tplg_module *t = mod->template;
	struct avs_micsel_cfg cfg;

	cfg.base.cpc = t->cfg_base->cpc;
	cfg.base.ibs = t->cfg_base->ibs;
	cfg.base.obs = t->cfg_base->obs;
	cfg.base.is_pages = t->cfg_base->is_pages;
	cfg.base.audio_fmt = *t->in_fmt;
	cfg.out_fmt = *t->cfg_ext->micsel.out_fmt;

	return avs_dsp_init_module(adev, mod->module_id, mod->owner->instance_id,
				   t->core_id, t->domain, &cfg, sizeof(cfg),
				   &mod->instance_id);
}

static int avs_modbase_create(struct avs_dev *adev, struct avs_path_module *mod)
{
	struct avs_tplg_module *t = mod->template;
	struct avs_modcfg_base cfg;

	cfg.cpc = t->cfg_base->cpc;
	cfg.ibs = t->cfg_base->ibs;
	cfg.obs = t->cfg_base->obs;
	cfg.is_pages = t->cfg_base->is_pages;
	cfg.audio_fmt = *t->in_fmt;

	return avs_dsp_init_module(adev, mod->module_id, mod->owner->instance_id,
				   t->core_id, t->domain, &cfg, sizeof(cfg),
				   &mod->instance_id);
}

static int avs_modext_create(struct avs_dev *adev, struct avs_path_module *mod)
{
	struct avs_tplg_module *t = mod->template;
	struct avs_tplg_modcfg_ext *tcfg = t->cfg_ext;
	struct avs_modcfg_ext *cfg;
	size_t cfg_size, num_pins;
	int ret, i;

	num_pins = tcfg->generic.num_input_pins + tcfg->generic.num_output_pins;
	cfg_size = struct_size(cfg, pin_fmts, num_pins);

	if (cfg_size > AVS_MAILBOX_SIZE)
		return -EINVAL;

	cfg = adev->modcfg_buf;
	memset(cfg, 0, cfg_size);
	cfg->base.cpc = t->cfg_base->cpc;
	cfg->base.ibs = t->cfg_base->ibs;
	cfg->base.obs = t->cfg_base->obs;
	cfg->base.is_pages = t->cfg_base->is_pages;
	cfg->base.audio_fmt = *t->in_fmt;
	cfg->num_input_pins = tcfg->generic.num_input_pins;
	cfg->num_output_pins = tcfg->generic.num_output_pins;

	/* configure pin formats */
	for (i = 0; i < num_pins; i++) {
		struct avs_tplg_pin_format *tpin = &tcfg->generic.pin_fmts[i];
		struct avs_pin_format *pin = &cfg->pin_fmts[i];

		pin->pin_index = tpin->pin_index;
		pin->iobs = tpin->iobs;
		pin->audio_fmt = *tpin->fmt;
	}

	ret = avs_dsp_init_module(adev, mod->module_id, mod->owner->instance_id,
				  t->core_id, t->domain, cfg, cfg_size,
				  &mod->instance_id);
	return ret;
}

static int avs_probe_create(struct avs_dev *adev, struct avs_path_module *mod)
{
	dev_err(adev->dev, "Probe module can't be instantiated by topology");
	return -EINVAL;
}

struct avs_module_create {
	guid_t *guid;
	int (*create)(struct avs_dev *adev, struct avs_path_module *mod);
};

static struct avs_module_create avs_module_create[] = {
	{ &AVS_MIXIN_MOD_UUID, avs_modbase_create },
	{ &AVS_MIXOUT_MOD_UUID, avs_modbase_create },
	{ &AVS_KPBUFF_MOD_UUID, avs_modbase_create },
	{ &AVS_COPIER_MOD_UUID, avs_copier_create },
	{ &AVS_PEAKVOL_MOD_UUID, avs_peakvol_create },
	{ &AVS_GAIN_MOD_UUID, avs_peakvol_create },
	{ &AVS_MICSEL_MOD_UUID, avs_micsel_create },
	{ &AVS_MUX_MOD_UUID, avs_mux_create },
	{ &AVS_UPDWMIX_MOD_UUID, avs_updown_mix_create },
	{ &AVS_SRCINTC_MOD_UUID, avs_src_create },
	{ &AVS_AEC_MOD_UUID, avs_aec_create },
	{ &AVS_ASRC_MOD_UUID, avs_asrc_create },
	{ &AVS_INTELWOV_MOD_UUID, avs_wov_create },
	{ &AVS_PROBE_MOD_UUID, avs_probe_create },
	{ &AVS_WOVHOSTM_MOD_UUID, avs_whm_create },
};

static int avs_path_module_type_create(struct avs_dev *adev, struct avs_path_module *mod)
{
	const guid_t *type = &mod->template->cfg_ext->type;

	for (int i = 0; i < ARRAY_SIZE(avs_module_create); i++)
		if (guid_equal(type, avs_module_create[i].guid))
			return avs_module_create[i].create(adev, mod);

	return avs_modext_create(adev, mod);
}

static int avs_path_module_send_init_configs(struct avs_dev *adev, struct avs_path_module *mod)
{
	struct avs_soc_component *acomp;

	acomp = to_avs_soc_component(mod->template->owner->owner->owner->owner->comp);

	u32 num_ids = mod->template->num_config_ids;
	u32 *ids = mod->template->config_ids;

	for (int i = 0; i < num_ids; i++) {
		struct avs_tplg_init_config *config = &acomp->tplg->init_configs[ids[i]];
		size_t len = config->length;
		void *data = config->data;
		u32 param = config->param;
		int ret;

		ret = avs_ipc_set_large_config(adev, mod->module_id, mod->instance_id,
					       param, data, len);
		if (ret) {
			dev_err(adev->dev, "send initial module config failed: %d\n", ret);
			return AVS_IPC_RET(ret);
		}
	}

	return 0;
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
	int module_id, ret;

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

	ret = avs_path_module_type_create(adev, mod);
	if (ret) {
		dev_err(adev->dev, "module-type create failed: %d\n", ret);
		kfree(mod);
		return ERR_PTR(ret);
	}

	ret = avs_path_module_send_init_configs(adev, mod);
	if (ret) {
		kfree(mod);
		return ERR_PTR(ret);
	}

	return mod;
}

static int avs_path_binding_arm(struct avs_dev *adev, struct avs_path_binding *binding)
{
	struct avs_path_module *this_mod, *target_mod;
	struct avs_path_pipeline *target_ppl;
	struct avs_path *target_path;
	struct avs_tplg_binding *t;

	t = binding->template;
	this_mod = avs_path_find_module(binding->owner,
					t->mod_id);
	if (!this_mod) {
		dev_err(adev->dev, "path mod %d not found\n", t->mod_id);
		return -EINVAL;
	}

	/* update with target_tplg_name too */
	target_path = avs_path_find_path(adev, t->target_tplg_name,
					 t->target_path_tmpl_id);
	if (!target_path) {
		dev_err(adev->dev, "target path %s:%d not found\n",
			t->target_tplg_name, t->target_path_tmpl_id);
		return -EINVAL;
	}

	target_ppl = avs_path_find_pipeline(target_path,
					    t->target_ppl_id);
	if (!target_ppl) {
		dev_err(adev->dev, "target ppl %d not found\n", t->target_ppl_id);
		return -EINVAL;
	}

	target_mod = avs_path_find_module(target_ppl, t->target_mod_id);
	if (!target_mod) {
		dev_err(adev->dev, "target mod %d not found\n", t->target_mod_id);
		return -EINVAL;
	}

	if (t->is_sink) {
		binding->sink = this_mod;
		binding->sink_pin = t->mod_pin;
		binding->source = target_mod;
		binding->source_pin = t->target_mod_pin;
	} else {
		binding->sink = target_mod;
		binding->sink_pin = t->target_mod_pin;
		binding->source = this_mod;
		binding->source_pin = t->mod_pin;
	}

	return 0;
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

static int avs_path_pipeline_arm(struct avs_dev *adev,
				 struct avs_path_pipeline *ppl)
{
	struct avs_path_module *mod;

	list_for_each_entry(mod, &ppl->mod_list, node) {
		struct avs_path_module *source, *sink;
		int ret;

		/*
		 * Only one module (so it's implicitly last) or it is the last
		 * one, either way we don't have next module to bind it to.
		 */
		if (mod == list_last_entry(&ppl->mod_list,
					   struct avs_path_module, node))
			break;

		/* bind current module to next module on list */
		source = mod;
		sink = list_next_entry(mod, node);

		ret = avs_ipc_bind(adev, source->module_id, source->instance_id,
				   sink->module_id, sink->instance_id, 0, 0);
		if (ret)
			return AVS_IPC_RET(ret);
	}

	return 0;
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
	INIT_LIST_HEAD(&path->source_list);
	INIT_LIST_HEAD(&path->sink_list);
	INIT_LIST_HEAD(&path->source_node);
	INIT_LIST_HEAD(&path->sink_node);

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

static int avs_path_arm(struct avs_dev *adev, struct avs_path *path)
{
	struct avs_path_pipeline *ppl;
	struct avs_path_binding *binding;
	int ret;

	list_for_each_entry(ppl, &path->ppl_list, node) {
		/*
		 * Arm all ppl bindings before binding internal modules
		 * as it costs no IPCs which isn't true for the latter.
		 */
		list_for_each_entry(binding, &ppl->binding_list, node) {
			ret = avs_path_binding_arm(adev, binding);
			if (ret < 0)
				return ret;
		}

		ret = avs_path_pipeline_arm(adev, ppl);
		if (ret < 0)
			return ret;
	}

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

	ret = avs_path_arm(adev, path);
	if (ret < 0)
		goto err;

	path->state = AVS_PPL_STATE_INVALID;
	return path;
err:
	avs_path_free_unlocked(path);
	return ERR_PTR(ret);
}

static void avs_condpath_free(struct avs_dev *adev, struct avs_path *path)
{
	int ret;

	list_del(&path->source_node);
	list_del(&path->sink_node);

	ret = avs_path_reset(path);
	if (ret < 0)
		dev_err(adev->dev, "reset condpath failed: %d\n", ret);

	ret = avs_path_unbind(path);
	if (ret < 0)
		dev_err(adev->dev, "unbind condpath failed: %d\n", ret);

	avs_path_free_unlocked(path);
}

static struct avs_path *avs_condpath_create(struct avs_dev *adev,
					    struct avs_tplg_path *template,
					    struct avs_path *source,
					    struct avs_path *sink)
{
	struct avs_path *path;
	int ret;

	path = avs_path_create_unlocked(adev, 0, template);
	if (IS_ERR(path))
		return path;

	ret = avs_path_bind(path);
	if (ret)
		goto err_bind;

	ret = avs_path_reset(path);
	if (ret)
		goto err_reset;

	path->source = source;
	path->sink = sink;
	list_add_tail(&path->source_node, &source->source_list);
	list_add_tail(&path->sink_node, &sink->sink_list);

	return path;

err_reset:
	avs_path_unbind(path);
err_bind:
	avs_path_free_unlocked(path);
	return ERR_PTR(ret);
}

static int avs_condpaths_walk(struct avs_dev *adev, struct avs_path *path, int dir)
{
	struct avs_soc_component *acomp;
	struct avs_path *source, *sink;
	struct avs_path **other;

	if (dir) {
		source = path;
		other = &sink;
	} else {
		sink = path;
		other = &source;
	}

	list_for_each_entry(acomp, &adev->comp_list, node) {
		for (int i = 0; i < acomp->tplg->num_condpath_tmpls; i++) {
			struct avs_tplg_path_template *template;
			struct avs_tplg_path *variant;
			struct avs_path *cpath;

			template = &acomp->tplg->condpath_tmpls[i];

			/* Do not create unidirectional condpaths twice. */
			if (avs_tplg_path_template_id_equal(&template->source,
							    &template->sink) && dir)
				continue;

			*other = avs_condpath_find_match(adev, template, path, dir);
			if (!*other)
				continue;

			variant = avs_condpath_find_variant(adev, template, source, sink);
			if (!variant)
				continue;

			cpath = avs_condpath_create(adev, variant, source, sink);
			if (IS_ERR(cpath))
				return PTR_ERR(cpath);
		}
	}

	return 0;
}

/* Caller responsible for holding adev->path_mutex. */
static int avs_condpaths_walk_all(struct avs_dev *adev, struct avs_path *path)
{
	int ret;

	ret = avs_condpaths_walk(adev, path, SNDRV_PCM_STREAM_CAPTURE);
	if (ret)
		return ret;

	return avs_condpaths_walk(adev, path, SNDRV_PCM_STREAM_PLAYBACK);
}

void avs_path_free(struct avs_path *path)
{
	struct avs_path *cpath, *csave;
	struct avs_dev *adev = path->owner;

	mutex_lock(&adev->path_mutex);

	/* Free all condpaths this path spawned. */
	list_for_each_entry_safe(cpath, csave, &path->source_list, source_node)
		avs_condpath_free(path->owner, cpath);
	list_for_each_entry_safe(cpath, csave, &path->sink_list, sink_node)
		avs_condpath_free(path->owner, cpath);

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
	int ret;

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
	if (IS_ERR(path))
		goto exit;

	ret = avs_condpaths_walk_all(adev, path);
	if (ret) {
		avs_path_free_unlocked(path);
		path = ERR_PTR(ret);
	}

exit:
	mutex_unlock(&adev->comp_list_mutex);
	mutex_unlock(&adev->path_mutex);

	return path;
}

static int avs_path_bind_prepare(struct avs_dev *adev,
				 struct avs_path_binding *binding)
{
	const struct avs_audio_format *src_fmt, *sink_fmt;
	struct avs_tplg_module *tsource = binding->source->template;
	struct avs_path_module *source = binding->source;
	int ret;

	/*
	 * only copier modules about to be bound
	 * to output pin other than 0 need preparation
	 */
	if (!binding->source_pin)
		return 0;
	if (!guid_equal(&tsource->cfg_ext->type, &AVS_COPIER_MOD_UUID))
		return 0;

	src_fmt = tsource->in_fmt;
	sink_fmt = binding->sink->template->in_fmt;

	ret = avs_ipc_copier_set_sink_format(adev, source->module_id,
					     source->instance_id, binding->source_pin,
					     src_fmt, sink_fmt);
	if (ret) {
		dev_err(adev->dev, "config copier failed: %d\n", ret);
		return AVS_IPC_RET(ret);
	}

	return 0;
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

			ret = avs_path_bind_prepare(adev, binding);
			if (ret < 0)
				return ret;

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

static int avs_condpath_pause(struct avs_dev *adev, struct avs_path *cpath)
{
	struct avs_path_pipeline *ppl;
	int ret;

	if (cpath->state == AVS_PPL_STATE_PAUSED)
		return 0;

	list_for_each_entry_reverse(ppl, &cpath->ppl_list, node) {
		ret = avs_ipc_set_pipeline_state(adev, ppl->instance_id, AVS_PPL_STATE_PAUSED);
		if (ret) {
			dev_err(adev->dev, "pause cpath failed: %d\n", ret);
			cpath->state = AVS_PPL_STATE_INVALID;
			return AVS_IPC_RET(ret);
		}
	}

	cpath->state = AVS_PPL_STATE_PAUSED;
	return 0;
}

static void avs_condpaths_pause(struct avs_dev *adev, struct avs_path *path)
{
	struct avs_path *cpath;

	mutex_lock(&adev->path_mutex);

	/* If either source or sink stops, so do the attached conditional paths. */
	list_for_each_entry(cpath, &path->source_list, source_node)
		avs_condpath_pause(adev, cpath);
	list_for_each_entry(cpath, &path->sink_list, sink_node)
		avs_condpath_pause(adev, cpath);

	mutex_unlock(&adev->path_mutex);
}

int avs_path_pause(struct avs_path *path)
{
	struct avs_path_pipeline *ppl;
	struct avs_dev *adev = path->owner;
	int ret;

	if (path->state == AVS_PPL_STATE_PAUSED)
		return 0;

	avs_condpaths_pause(adev, path);

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

static int avs_condpath_run(struct avs_dev *adev, struct avs_path *cpath, int trigger)
{
	struct avs_path_pipeline *ppl;
	int ret;

	if (cpath->state == AVS_PPL_STATE_RUNNING)
		return 0;

	list_for_each_entry(ppl, &cpath->ppl_list, node) {
		if (ppl->template->cfg->trigger != trigger)
			continue;

		ret = avs_ipc_set_pipeline_state(adev, ppl->instance_id, AVS_PPL_STATE_RUNNING);
		if (ret) {
			dev_err(adev->dev, "run cpath failed: %d\n", ret);
			cpath->state = AVS_PPL_STATE_INVALID;
			return AVS_IPC_RET(ret);
		}
	}

	cpath->state = AVS_PPL_STATE_RUNNING;
	return 0;
}

static void avs_condpaths_run(struct avs_dev *adev, struct avs_path *path, int trigger)
{
	struct avs_path *cpath;

	mutex_lock(&adev->path_mutex);

	/* Run conditional paths only if source and sink are both running. */
	list_for_each_entry(cpath, &path->source_list, source_node)
		if (cpath->source->state == AVS_PPL_STATE_RUNNING &&
		    cpath->sink->state == AVS_PPL_STATE_RUNNING)
			avs_condpath_run(adev, cpath, trigger);

	list_for_each_entry(cpath, &path->sink_list, sink_node)
		if (cpath->source->state == AVS_PPL_STATE_RUNNING &&
		    cpath->sink->state == AVS_PPL_STATE_RUNNING)
			avs_condpath_run(adev, cpath, trigger);

	mutex_unlock(&adev->path_mutex);
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

	/* Granular pipeline triggering not intended for conditional paths. */
	if (trigger == AVS_TPLG_TRIGGER_AUTO)
		avs_condpaths_run(adev, path, trigger);

	return 0;
}
