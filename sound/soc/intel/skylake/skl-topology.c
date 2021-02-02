// SPDX-License-Identifier: GPL-2.0-only
/*
 *  skl-topology.c - Implements Platform component ALSA controls/widget
 *  handlers.
 *
 *  Copyright (C) 2014-2015 Intel Corp
 *  Author: Jeeja KP <jeeja.kp@intel.com>
 *  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

#include <linux/slab.h>
#include <linux/types.h>
#include <linux/firmware.h>
#include <linux/uuid.h>
#include <sound/intel-nhlt.h>
#include <sound/soc.h>
#include <sound/soc-acpi.h>
#include <sound/soc-topology.h>
#include <uapi/sound/snd_sst_tokens.h>
#include <uapi/sound/skl-tplg-interface.h>
#include "skl-sst-dsp.h"
#include "skl-sst-ipc.h"
#include "skl-topology.h"
#include "skl.h"
#include "../common/sst-dsp.h"
#include "../common/sst-dsp-priv.h"

#define SKL_CH_FIXUP_MASK		(1 << 0)
#define SKL_RATE_FIXUP_MASK		(1 << 1)
#define SKL_FMT_FIXUP_MASK		(1 << 2)
#define SKL_IN_DIR_BIT_MASK		BIT(0)
#define SKL_PIN_COUNT_MASK		GENMASK(7, 4)

static const int mic_mono_list[] = {
0, 1, 2, 3,
};
static const int mic_stereo_list[][SKL_CH_STEREO] = {
{0, 1}, {0, 2}, {0, 3}, {1, 2}, {1, 3}, {2, 3},
};
static const int mic_trio_list[][SKL_CH_TRIO] = {
{0, 1, 2}, {0, 1, 3}, {0, 2, 3}, {1, 2, 3},
};
static const int mic_quatro_list[][SKL_CH_QUATRO] = {
{0, 1, 2, 3},
};

#define CHECK_HW_PARAMS(ch, freq, bps, prm_ch, prm_freq, prm_bps) \
	((ch == prm_ch) && (bps == prm_bps) && (freq == prm_freq))

void skl_tplg_d0i3_get(struct skl_dev *skl, enum d0i3_capability caps)
{
	struct skl_d0i3_data *d0i3 =  &skl->d0i3;

	switch (caps) {
	case SKL_D0I3_NONE:
		d0i3->non_d0i3++;
		break;

	case SKL_D0I3_STREAMING:
		d0i3->streaming++;
		break;

	case SKL_D0I3_NON_STREAMING:
		d0i3->non_streaming++;
		break;
	}
}

void skl_tplg_d0i3_put(struct skl_dev *skl, enum d0i3_capability caps)
{
	struct skl_d0i3_data *d0i3 =  &skl->d0i3;

	switch (caps) {
	case SKL_D0I3_NONE:
		d0i3->non_d0i3--;
		break;

	case SKL_D0I3_STREAMING:
		d0i3->streaming--;
		break;

	case SKL_D0I3_NON_STREAMING:
		d0i3->non_streaming--;
		break;
	}
}

/*
 * SKL DSP driver modelling uses only few DAPM widgets so for rest we will
 * ignore. This helpers checks if the SKL driver handles this widget type
 */
static int is_skl_dsp_widget_type(struct snd_soc_dapm_widget *w,
				  struct device *dev)
{
	if (w->dapm->dev != dev)
		return false;

	switch (w->id) {
	case snd_soc_dapm_dai_link:
	case snd_soc_dapm_dai_in:
	case snd_soc_dapm_aif_in:
	case snd_soc_dapm_aif_out:
	case snd_soc_dapm_dai_out:
	case snd_soc_dapm_switch:
	case snd_soc_dapm_output:
	case snd_soc_dapm_mux:

		return false;
	default:
		return true;
	}
}

static void skl_dump_mconfig(struct skl_dev *skl, struct skl_module_cfg *mcfg)
{
	struct skl_module_iface *iface = &mcfg->module->formats[0];

	dev_dbg(skl->dev, "Dumping config\n");
	dev_dbg(skl->dev, "Input Format:\n");
	dev_dbg(skl->dev, "channels = %d\n", iface->inputs[0].fmt.channels);
	dev_dbg(skl->dev, "s_freq = %d\n", iface->inputs[0].fmt.s_freq);
	dev_dbg(skl->dev, "ch_cfg = %d\n", iface->inputs[0].fmt.ch_cfg);
	dev_dbg(skl->dev, "valid bit depth = %d\n",
				iface->inputs[0].fmt.valid_bit_depth);
	dev_dbg(skl->dev, "Output Format:\n");
	dev_dbg(skl->dev, "channels = %d\n", iface->outputs[0].fmt.channels);
	dev_dbg(skl->dev, "s_freq = %d\n", iface->outputs[0].fmt.s_freq);
	dev_dbg(skl->dev, "valid bit depth = %d\n",
				iface->outputs[0].fmt.valid_bit_depth);
	dev_dbg(skl->dev, "ch_cfg = %d\n", iface->outputs[0].fmt.ch_cfg);
}

static void skl_tplg_update_chmap(struct skl_module_fmt *fmt, int chs)
{
	int slot_map = 0xFFFFFFFF;
	int start_slot = 0;
	int i;

	for (i = 0; i < chs; i++) {
		/*
		 * For 2 channels with starting slot as 0, slot map will
		 * look like 0xFFFFFF10.
		 */
		slot_map &= (~(0xF << (4 * i)) | (start_slot << (4 * i)));
		start_slot++;
	}
	fmt->ch_map = slot_map;
}

static void skl_tplg_update_params(struct skl_module_fmt *fmt,
			struct skl_pipe_params *params, int fixup)
{
	if (fixup & SKL_RATE_FIXUP_MASK)
		fmt->s_freq = params->s_freq;
	if (fixup & SKL_CH_FIXUP_MASK) {
		fmt->channels = params->ch;
		skl_tplg_update_chmap(fmt, fmt->channels);
	}
	if (fixup & SKL_FMT_FIXUP_MASK) {
		fmt->valid_bit_depth = skl_get_bit_depth(params->s_fmt);

		/*
		 * 16 bit is 16 bit container whereas 24 bit is in 32 bit
		 * container so update bit depth accordingly
		 */
		switch (fmt->valid_bit_depth) {
		case SKL_DEPTH_16BIT:
			fmt->bit_depth = fmt->valid_bit_depth;
			break;

		default:
			fmt->bit_depth = SKL_DEPTH_32BIT;
			break;
		}
	}

}

/*
 * A pipeline may have modules which impact the pcm parameters, like SRC,
 * channel converter, format converter.
 * We need to calculate the output params by applying the 'fixup'
 * Topology will tell driver which type of fixup is to be applied by
 * supplying the fixup mask, so based on that we calculate the output
 *
 * Now In FE the pcm hw_params is source/target format. Same is applicable
 * for BE with its hw_params invoked.
 * here based on FE, BE pipeline and direction we calculate the input and
 * outfix and then apply that for a module
 */
static void skl_tplg_update_params_fixup(struct skl_module_cfg *m_cfg,
		struct skl_pipe_params *params, bool is_fe)
{
	int in_fixup, out_fixup;
	struct skl_module_fmt *in_fmt, *out_fmt;

	/* Fixups will be applied to pin 0 only */
	in_fmt = &m_cfg->module->formats[0].inputs[0].fmt;
	out_fmt = &m_cfg->module->formats[0].outputs[0].fmt;

	if (params->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		if (is_fe) {
			in_fixup = m_cfg->params_fixup;
			out_fixup = (~m_cfg->converter) &
					m_cfg->params_fixup;
		} else {
			out_fixup = m_cfg->params_fixup;
			in_fixup = (~m_cfg->converter) &
					m_cfg->params_fixup;
		}
	} else {
		if (is_fe) {
			out_fixup = m_cfg->params_fixup;
			in_fixup = (~m_cfg->converter) &
					m_cfg->params_fixup;
		} else {
			in_fixup = m_cfg->params_fixup;
			out_fixup = (~m_cfg->converter) &
					m_cfg->params_fixup;
		}
	}

	skl_tplg_update_params(in_fmt, params, in_fixup);
	skl_tplg_update_params(out_fmt, params, out_fixup);
}

/*
 * A module needs input and output buffers, which are dependent upon pcm
 * params, so once we have calculate params, we need buffer calculation as
 * well.
 */
static void skl_tplg_update_buffer_size(struct skl_dev *skl,
				struct skl_module_cfg *mcfg)
{
	int multiplier = 1;
	struct skl_module_fmt *in_fmt, *out_fmt;
	struct skl_module_res *res;

	/* Since fixups is applied to pin 0 only, ibs, obs needs
	 * change for pin 0 only
	 */
	res = &mcfg->module->resources[0];
	in_fmt = &mcfg->module->formats[0].inputs[0].fmt;
	out_fmt = &mcfg->module->formats[0].outputs[0].fmt;

	if (mcfg->m_type == SKL_MODULE_TYPE_SRCINT)
		multiplier = 5;

	res->ibs = DIV_ROUND_UP(in_fmt->s_freq, 1000) *
			in_fmt->channels * (in_fmt->bit_depth >> 3) *
			multiplier;

	res->obs = DIV_ROUND_UP(out_fmt->s_freq, 1000) *
			out_fmt->channels * (out_fmt->bit_depth >> 3) *
			multiplier;
}

static u8 skl_tplg_be_dev_type(int dev_type)
{
	int ret;

	switch (dev_type) {
	case SKL_DEVICE_BT:
		ret = NHLT_DEVICE_BT;
		break;

	case SKL_DEVICE_DMIC:
		ret = NHLT_DEVICE_DMIC;
		break;

	case SKL_DEVICE_I2S:
		ret = NHLT_DEVICE_I2S;
		break;

	default:
		ret = NHLT_DEVICE_INVALID;
		break;
	}

	return ret;
}

static int skl_tplg_update_be_blob(struct snd_soc_dapm_widget *w,
						struct skl_dev *skl)
{
	struct skl_module_cfg *m_cfg = w->priv;
	int link_type, dir;
	u32 ch, s_freq, s_fmt;
	struct nhlt_specific_cfg *cfg;
	u8 dev_type = skl_tplg_be_dev_type(m_cfg->dev_type);
	int fmt_idx = m_cfg->fmt_idx;
	struct skl_module_iface *m_iface = &m_cfg->module->formats[fmt_idx];

	/* check if we already have blob */
	if (m_cfg->formats_config.caps_size > 0)
		return 0;

	dev_dbg(skl->dev, "Applying default cfg blob\n");
	switch (m_cfg->dev_type) {
	case SKL_DEVICE_DMIC:
		link_type = NHLT_LINK_DMIC;
		dir = SNDRV_PCM_STREAM_CAPTURE;
		s_freq = m_iface->inputs[0].fmt.s_freq;
		s_fmt = m_iface->inputs[0].fmt.bit_depth;
		ch = m_iface->inputs[0].fmt.channels;
		break;

	case SKL_DEVICE_I2S:
		link_type = NHLT_LINK_SSP;
		if (m_cfg->hw_conn_type == SKL_CONN_SOURCE) {
			dir = SNDRV_PCM_STREAM_PLAYBACK;
			s_freq = m_iface->outputs[0].fmt.s_freq;
			s_fmt = m_iface->outputs[0].fmt.bit_depth;
			ch = m_iface->outputs[0].fmt.channels;
		} else {
			dir = SNDRV_PCM_STREAM_CAPTURE;
			s_freq = m_iface->inputs[0].fmt.s_freq;
			s_fmt = m_iface->inputs[0].fmt.bit_depth;
			ch = m_iface->inputs[0].fmt.channels;
		}
		break;

	default:
		return -EINVAL;
	}

	/* update the blob based on virtual bus_id and default params */
	cfg = skl_get_ep_blob(skl, m_cfg->vbus_id, link_type,
					s_fmt, ch, s_freq, dir, dev_type);
	if (cfg) {
		m_cfg->formats_config.caps_size = cfg->size;
		m_cfg->formats_config.caps = (u32 *) &cfg->caps;
	} else {
		dev_err(skl->dev, "Blob NULL for id %x type %d dirn %d\n",
					m_cfg->vbus_id, link_type, dir);
		dev_err(skl->dev, "PCM: ch %d, freq %d, fmt %d\n",
					ch, s_freq, s_fmt);
		return -EIO;
	}

	return 0;
}

static void skl_tplg_update_module_params(struct snd_soc_dapm_widget *w,
							struct skl_dev *skl)
{
	struct skl_module_cfg *m_cfg = w->priv;
	struct skl_pipe_params *params = m_cfg->pipe->p_params;
	int p_conn_type = m_cfg->pipe->conn_type;
	bool is_fe;

	if (!m_cfg->params_fixup)
		return;

	dev_dbg(skl->dev, "Mconfig for widget=%s BEFORE updation\n",
				w->name);

	skl_dump_mconfig(skl, m_cfg);

	if (p_conn_type == SKL_PIPE_CONN_TYPE_FE)
		is_fe = true;
	else
		is_fe = false;

	skl_tplg_update_params_fixup(m_cfg, params, is_fe);
	skl_tplg_update_buffer_size(skl, m_cfg);

	dev_dbg(skl->dev, "Mconfig for widget=%s AFTER updation\n",
				w->name);

	skl_dump_mconfig(skl, m_cfg);
}

/*
 * some modules can have multiple params set from user control and
 * need to be set after module is initialized. If set_param flag is
 * set module params will be done after module is initialised.
 */
static int skl_tplg_set_module_params(struct snd_soc_dapm_widget *w,
						struct skl_dev *skl)
{
	int i, ret;
	struct skl_module_cfg *mconfig = w->priv;
	const struct snd_kcontrol_new *k;
	struct soc_bytes_ext *sb;
	struct skl_algo_data *bc;
	struct skl_specific_cfg *sp_cfg;

	if (mconfig->formats_config.caps_size > 0 &&
		mconfig->formats_config.set_params == SKL_PARAM_SET) {
		sp_cfg = &mconfig->formats_config;
		ret = skl_set_module_params(skl, sp_cfg->caps,
					sp_cfg->caps_size,
					sp_cfg->param_id, mconfig);
		if (ret < 0)
			return ret;
	}

	for (i = 0; i < w->num_kcontrols; i++) {
		k = &w->kcontrol_news[i];
		if (k->access & SNDRV_CTL_ELEM_ACCESS_TLV_CALLBACK) {
			sb = (void *) k->private_value;
			bc = (struct skl_algo_data *)sb->dobj.private;

			if (bc->set_params == SKL_PARAM_SET) {
				ret = skl_set_module_params(skl,
						(u32 *)bc->params, bc->size,
						bc->param_id, mconfig);
				if (ret < 0)
					return ret;
			}
		}
	}

	return 0;
}

/*
 * some module param can set from user control and this is required as
 * when module is initailzed. if module param is required in init it is
 * identifed by set_param flag. if set_param flag is not set, then this
 * parameter needs to set as part of module init.
 */
static int skl_tplg_set_module_init_data(struct snd_soc_dapm_widget *w)
{
	const struct snd_kcontrol_new *k;
	struct soc_bytes_ext *sb;
	struct skl_algo_data *bc;
	struct skl_module_cfg *mconfig = w->priv;
	int i;

	for (i = 0; i < w->num_kcontrols; i++) {
		k = &w->kcontrol_news[i];
		if (k->access & SNDRV_CTL_ELEM_ACCESS_TLV_CALLBACK) {
			sb = (struct soc_bytes_ext *)k->private_value;
			bc = (struct skl_algo_data *)sb->dobj.private;

			if (bc->set_params != SKL_PARAM_INIT)
				continue;

			mconfig->formats_config.caps = (u32 *)bc->params;
			mconfig->formats_config.caps_size = bc->size;

			break;
		}
	}

	return 0;
}

static int skl_tplg_module_prepare(struct skl_dev *skl, struct skl_pipe *pipe,
		struct snd_soc_dapm_widget *w, struct skl_module_cfg *mcfg)
{
	switch (mcfg->dev_type) {
	case SKL_DEVICE_HDAHOST:
		return skl_pcm_host_dma_prepare(skl->dev, pipe->p_params);

	case SKL_DEVICE_HDALINK:
		return skl_pcm_link_dma_prepare(skl->dev, pipe->p_params);
	}

	return 0;
}

/*
 * Inside a pipe instance, we can have various modules. These modules need
 * to instantiated in DSP by invoking INIT_MODULE IPC, which is achieved by
 * skl_init_module() routine, so invoke that for all modules in a pipeline
 */
static int
skl_tplg_init_pipe_modules(struct skl_dev *skl, struct skl_pipe *pipe)
{
	struct skl_pipe_module *w_module;
	struct snd_soc_dapm_widget *w;
	struct skl_module_cfg *mconfig;
	u8 cfg_idx;
	int ret = 0;

	list_for_each_entry(w_module, &pipe->w_list, node) {
		guid_t *uuid_mod;
		w = w_module->w;
		mconfig = w->priv;

		/* check if module ids are populated */
		if (mconfig->id.module_id < 0) {
			dev_err(skl->dev,
					"module %pUL id not populated\n",
					(guid_t *)mconfig->guid);
			return -EIO;
		}

		cfg_idx = mconfig->pipe->cur_config_idx;
		mconfig->fmt_idx = mconfig->mod_cfg[cfg_idx].fmt_idx;
		mconfig->res_idx = mconfig->mod_cfg[cfg_idx].res_idx;

		if (mconfig->module->loadable && skl->dsp->fw_ops.load_mod) {
			ret = skl->dsp->fw_ops.load_mod(skl->dsp,
				mconfig->id.module_id, mconfig->guid);
			if (ret < 0)
				return ret;

			mconfig->m_state = SKL_MODULE_LOADED;
		}

		/* prepare the DMA if the module is gateway cpr */
		ret = skl_tplg_module_prepare(skl, pipe, w, mconfig);
		if (ret < 0)
			return ret;

		/* update blob if blob is null for be with default value */
		skl_tplg_update_be_blob(w, skl);

		/*
		 * apply fix/conversion to module params based on
		 * FE/BE params
		 */
		skl_tplg_update_module_params(w, skl);
		uuid_mod = (guid_t *)mconfig->guid;
		mconfig->id.pvt_id = skl_get_pvt_id(skl, uuid_mod,
						mconfig->id.instance_id);
		if (mconfig->id.pvt_id < 0)
			return ret;
		skl_tplg_set_module_init_data(w);

		ret = skl_dsp_get_core(skl->dsp, mconfig->core_id);
		if (ret < 0) {
			dev_err(skl->dev, "Failed to wake up core %d ret=%d\n",
						mconfig->core_id, ret);
			return ret;
		}

		ret = skl_init_module(skl, mconfig);
		if (ret < 0) {
			skl_put_pvt_id(skl, uuid_mod, &mconfig->id.pvt_id);
			goto err;
		}

		ret = skl_tplg_set_module_params(w, skl);
		if (ret < 0)
			goto err;
	}

	return 0;
err:
	skl_dsp_put_core(skl->dsp, mconfig->core_id);
	return ret;
}

static int skl_tplg_unload_pipe_modules(struct skl_dev *skl,
	 struct skl_pipe *pipe)
{
	int ret = 0;
	struct skl_pipe_module *w_module;
	struct skl_module_cfg *mconfig;

	list_for_each_entry(w_module, &pipe->w_list, node) {
		guid_t *uuid_mod;
		mconfig  = w_module->w->priv;
		uuid_mod = (guid_t *)mconfig->guid;

		if (mconfig->module->loadable && skl->dsp->fw_ops.unload_mod &&
			mconfig->m_state > SKL_MODULE_UNINIT) {
			ret = skl->dsp->fw_ops.unload_mod(skl->dsp,
						mconfig->id.module_id);
			if (ret < 0)
				return -EIO;
		}
		skl_put_pvt_id(skl, uuid_mod, &mconfig->id.pvt_id);

		ret = skl_dsp_put_core(skl->dsp, mconfig->core_id);
		if (ret < 0) {
			/* don't return; continue with other modules */
			dev_err(skl->dev, "Failed to sleep core %d ret=%d\n",
				mconfig->core_id, ret);
		}
	}

	/* no modules to unload in this path, so return */
	return ret;
}

static bool skl_tplg_is_multi_fmt(struct skl_dev *skl, struct skl_pipe *pipe)
{
	struct skl_pipe_fmt *cur_fmt;
	struct skl_pipe_fmt *next_fmt;
	int i;

	if (pipe->nr_cfgs <= 1)
		return false;

	if (pipe->conn_type != SKL_PIPE_CONN_TYPE_FE)
		return true;

	for (i = 0; i < pipe->nr_cfgs - 1; i++) {
		if (pipe->direction == SNDRV_PCM_STREAM_PLAYBACK) {
			cur_fmt = &pipe->configs[i].out_fmt;
			next_fmt = &pipe->configs[i + 1].out_fmt;
		} else {
			cur_fmt = &pipe->configs[i].in_fmt;
			next_fmt = &pipe->configs[i + 1].in_fmt;
		}

		if (!CHECK_HW_PARAMS(cur_fmt->channels, cur_fmt->freq,
				     cur_fmt->bps,
				     next_fmt->channels,
				     next_fmt->freq,
				     next_fmt->bps))
			return true;
	}

	return false;
}

/*
 * Here, we select pipe format based on the pipe type and pipe
 * direction to determine the current config index for the pipeline.
 * The config index is then used to select proper module resources.
 * Intermediate pipes currently have a fixed format hence we select the
 * 0th configuratation by default for such pipes.
 */
static int
skl_tplg_get_pipe_config(struct skl_dev *skl, struct skl_module_cfg *mconfig)
{
	struct skl_pipe *pipe = mconfig->pipe;
	struct skl_pipe_params *params = pipe->p_params;
	struct skl_path_config *pconfig = &pipe->configs[0];
	struct skl_pipe_fmt *fmt = NULL;
	bool in_fmt = false;
	int i;

	if (pipe->nr_cfgs == 0) {
		pipe->cur_config_idx = 0;
		return 0;
	}

	if (skl_tplg_is_multi_fmt(skl, pipe)) {
		pipe->cur_config_idx = pipe->pipe_config_idx;
		pipe->memory_pages = pconfig->mem_pages;
		dev_dbg(skl->dev, "found pipe config idx:%d\n",
			pipe->cur_config_idx);
		return 0;
	}

	if (pipe->conn_type == SKL_PIPE_CONN_TYPE_NONE) {
		dev_dbg(skl->dev, "No conn_type detected, take 0th config\n");
		pipe->cur_config_idx = 0;
		pipe->memory_pages = pconfig->mem_pages;

		return 0;
	}

	if ((pipe->conn_type == SKL_PIPE_CONN_TYPE_FE &&
	     pipe->direction == SNDRV_PCM_STREAM_PLAYBACK) ||
	     (pipe->conn_type == SKL_PIPE_CONN_TYPE_BE &&
	     pipe->direction == SNDRV_PCM_STREAM_CAPTURE))
		in_fmt = true;

	for (i = 0; i < pipe->nr_cfgs; i++) {
		pconfig = &pipe->configs[i];
		if (in_fmt)
			fmt = &pconfig->in_fmt;
		else
			fmt = &pconfig->out_fmt;

		if (CHECK_HW_PARAMS(params->ch, params->s_freq, params->s_fmt,
				    fmt->channels, fmt->freq, fmt->bps)) {
			pipe->cur_config_idx = i;
			pipe->memory_pages = pconfig->mem_pages;
			dev_dbg(skl->dev, "Using pipe config: %d\n", i);

			return 0;
		}
	}

	dev_err(skl->dev, "Invalid pipe config: %d %d %d for pipe: %d\n",
		params->ch, params->s_freq, params->s_fmt, pipe->ppl_id);
	return -EINVAL;
}

/*
 * Mixer module represents a pipeline. So in the Pre-PMU event of mixer we
 * need create the pipeline. So we do following:
 *   - Create the pipeline
 *   - Initialize the modules in pipeline
 *   - finally bind all modules together
 */
static int skl_tplg_mixer_dapm_pre_pmu_event(struct snd_soc_dapm_widget *w,
							struct skl_dev *skl)
{
	int ret;
	struct skl_module_cfg *mconfig = w->priv;
	struct skl_pipe_module *w_module;
	struct skl_pipe *s_pipe = mconfig->pipe;
	struct skl_module_cfg *src_module = NULL, *dst_module, *module;
	struct skl_module_deferred_bind *modules;

	ret = skl_tplg_get_pipe_config(skl, mconfig);
	if (ret < 0)
		return ret;

	/*
	 * Create a list of modules for pipe.
	 * This list contains modules from source to sink
	 */
	ret = skl_create_pipeline(skl, mconfig->pipe);
	if (ret < 0)
		return ret;

	/* Init all pipe modules from source to sink */
	ret = skl_tplg_init_pipe_modules(skl, s_pipe);
	if (ret < 0)
		return ret;

	/* Bind modules from source to sink */
	list_for_each_entry(w_module, &s_pipe->w_list, node) {
		dst_module = w_module->w->priv;

		if (src_module == NULL) {
			src_module = dst_module;
			continue;
		}

		ret = skl_bind_modules(skl, src_module, dst_module);
		if (ret < 0)
			return ret;

		src_module = dst_module;
	}

	/*
	 * When the destination module is initialized, check for these modules
	 * in deferred bind list. If found, bind them.
	 */
	list_for_each_entry(w_module, &s_pipe->w_list, node) {
		if (list_empty(&skl->bind_list))
			break;

		list_for_each_entry(modules, &skl->bind_list, node) {
			module = w_module->w->priv;
			if (modules->dst == module)
				skl_bind_modules(skl, modules->src,
							modules->dst);
		}
	}

	return 0;
}

static int skl_fill_sink_instance_id(struct skl_dev *skl, u32 *params,
				int size, struct skl_module_cfg *mcfg)
{
	int i, pvt_id;

	if (mcfg->m_type == SKL_MODULE_TYPE_KPB) {
		struct skl_kpb_params *kpb_params =
				(struct skl_kpb_params *)params;
		struct skl_mod_inst_map *inst = kpb_params->u.map;

		for (i = 0; i < kpb_params->num_modules; i++) {
			pvt_id = skl_get_pvt_instance_id_map(skl, inst->mod_id,
								inst->inst_id);
			if (pvt_id < 0)
				return -EINVAL;

			inst->inst_id = pvt_id;
			inst++;
		}
	}

	return 0;
}
/*
 * Some modules require params to be set after the module is bound to
 * all pins connected.
 *
 * The module provider initializes set_param flag for such modules and we
 * send params after binding
 */
static int skl_tplg_set_module_bind_params(struct snd_soc_dapm_widget *w,
			struct skl_module_cfg *mcfg, struct skl_dev *skl)
{
	int i, ret;
	struct skl_module_cfg *mconfig = w->priv;
	const struct snd_kcontrol_new *k;
	struct soc_bytes_ext *sb;
	struct skl_algo_data *bc;
	struct skl_specific_cfg *sp_cfg;
	u32 *params;

	/*
	 * check all out/in pins are in bind state.
	 * if so set the module param
	 */
	for (i = 0; i < mcfg->module->max_output_pins; i++) {
		if (mcfg->m_out_pin[i].pin_state != SKL_PIN_BIND_DONE)
			return 0;
	}

	for (i = 0; i < mcfg->module->max_input_pins; i++) {
		if (mcfg->m_in_pin[i].pin_state != SKL_PIN_BIND_DONE)
			return 0;
	}

	if (mconfig->formats_config.caps_size > 0 &&
		mconfig->formats_config.set_params == SKL_PARAM_BIND) {
		sp_cfg = &mconfig->formats_config;
		ret = skl_set_module_params(skl, sp_cfg->caps,
					sp_cfg->caps_size,
					sp_cfg->param_id, mconfig);
		if (ret < 0)
			return ret;
	}

	for (i = 0; i < w->num_kcontrols; i++) {
		k = &w->kcontrol_news[i];
		if (k->access & SNDRV_CTL_ELEM_ACCESS_TLV_CALLBACK) {
			sb = (void *) k->private_value;
			bc = (struct skl_algo_data *)sb->dobj.private;

			if (bc->set_params == SKL_PARAM_BIND) {
				params = kmemdup(bc->params, bc->max, GFP_KERNEL);
				if (!params)
					return -ENOMEM;

				skl_fill_sink_instance_id(skl, params, bc->max,
								mconfig);

				ret = skl_set_module_params(skl, params,
						bc->max, bc->param_id, mconfig);
				kfree(params);

				if (ret < 0)
					return ret;
			}
		}
	}

	return 0;
}

static int skl_get_module_id(struct skl_dev *skl, guid_t *uuid)
{
	struct uuid_module *module;

	list_for_each_entry(module, &skl->uuid_list, list) {
		if (guid_equal(uuid, &module->uuid))
			return module->id;
	}

	return -EINVAL;
}

static int skl_tplg_find_moduleid_from_uuid(struct skl_dev *skl,
					const struct snd_kcontrol_new *k)
{
	struct soc_bytes_ext *sb = (void *) k->private_value;
	struct skl_algo_data *bc = (struct skl_algo_data *)sb->dobj.private;
	struct skl_kpb_params *uuid_params, *params;
	struct hdac_bus *bus = skl_to_bus(skl);
	int i, size, module_id;

	if (bc->set_params == SKL_PARAM_BIND && bc->max) {
		uuid_params = (struct skl_kpb_params *)bc->params;
		size = struct_size(params, u.map, uuid_params->num_modules);

		params = devm_kzalloc(bus->dev, size, GFP_KERNEL);
		if (!params)
			return -ENOMEM;

		params->num_modules = uuid_params->num_modules;

		for (i = 0; i < uuid_params->num_modules; i++) {
			module_id = skl_get_module_id(skl,
				&uuid_params->u.map_uuid[i].mod_uuid);
			if (module_id < 0) {
				devm_kfree(bus->dev, params);
				return -EINVAL;
			}

			params->u.map[i].mod_id = module_id;
			params->u.map[i].inst_id =
				uuid_params->u.map_uuid[i].inst_id;
		}

		devm_kfree(bus->dev, bc->params);
		bc->params = (char *)params;
		bc->max = size;
	}

	return 0;
}

/*
 * Retrieve the module id from UUID mentioned in the
 * post bind params
 */
void skl_tplg_add_moduleid_in_bind_params(struct skl_dev *skl,
				struct snd_soc_dapm_widget *w)
{
	struct skl_module_cfg *mconfig = w->priv;
	int i;

	/*
	 * Post bind params are used for only for KPB
	 * to set copier instances to drain the data
	 * in fast mode
	 */
	if (mconfig->m_type != SKL_MODULE_TYPE_KPB)
		return;

	for (i = 0; i < w->num_kcontrols; i++)
		if ((w->kcontrol_news[i].access &
			SNDRV_CTL_ELEM_ACCESS_TLV_CALLBACK) &&
			(skl_tplg_find_moduleid_from_uuid(skl,
			&w->kcontrol_news[i]) < 0))
			dev_err(skl->dev,
				"%s: invalid kpb post bind params\n",
				__func__);
}

static int skl_tplg_module_add_deferred_bind(struct skl_dev *skl,
	struct skl_module_cfg *src, struct skl_module_cfg *dst)
{
	struct skl_module_deferred_bind *m_list, *modules;
	int i;

	/* only supported for module with static pin connection */
	for (i = 0; i < dst->module->max_input_pins; i++) {
		struct skl_module_pin *pin = &dst->m_in_pin[i];

		if (pin->is_dynamic)
			continue;

		if ((pin->id.module_id  == src->id.module_id) &&
			(pin->id.instance_id  == src->id.instance_id)) {

			if (!list_empty(&skl->bind_list)) {
				list_for_each_entry(modules, &skl->bind_list, node) {
					if (modules->src == src && modules->dst == dst)
						return 0;
				}
			}

			m_list = kzalloc(sizeof(*m_list), GFP_KERNEL);
			if (!m_list)
				return -ENOMEM;

			m_list->src = src;
			m_list->dst = dst;

			list_add(&m_list->node, &skl->bind_list);
		}
	}

	return 0;
}

static int skl_tplg_bind_sinks(struct snd_soc_dapm_widget *w,
				struct skl_dev *skl,
				struct snd_soc_dapm_widget *src_w,
				struct skl_module_cfg *src_mconfig)
{
	struct snd_soc_dapm_path *p;
	struct snd_soc_dapm_widget *sink = NULL, *next_sink = NULL;
	struct skl_module_cfg *sink_mconfig;
	int ret;

	snd_soc_dapm_widget_for_each_sink_path(w, p) {
		if (!p->connect)
			continue;

		dev_dbg(skl->dev,
			"%s: src widget=%s\n", __func__, w->name);
		dev_dbg(skl->dev,
			"%s: sink widget=%s\n", __func__, p->sink->name);

		next_sink = p->sink;

		if (!is_skl_dsp_widget_type(p->sink, skl->dev))
			return skl_tplg_bind_sinks(p->sink, skl, src_w, src_mconfig);

		/*
		 * here we will check widgets in sink pipelines, so that
		 * can be any widgets type and we are only interested if
		 * they are ones used for SKL so check that first
		 */
		if ((p->sink->priv != NULL) &&
				is_skl_dsp_widget_type(p->sink, skl->dev)) {

			sink = p->sink;
			sink_mconfig = sink->priv;

			/*
			 * Modules other than PGA leaf can be connected
			 * directly or via switch to a module in another
			 * pipeline. EX: reference path
			 * when the path is enabled, the dst module that needs
			 * to be bound may not be initialized. if the module is
			 * not initialized, add these modules in the deferred
			 * bind list and when the dst module is initialised,
			 * bind this module to the dst_module in deferred list.
			 */
			if (((src_mconfig->m_state == SKL_MODULE_INIT_DONE)
				&& (sink_mconfig->m_state == SKL_MODULE_UNINIT))) {

				ret = skl_tplg_module_add_deferred_bind(skl,
						src_mconfig, sink_mconfig);

				if (ret < 0)
					return ret;

			}


			if (src_mconfig->m_state == SKL_MODULE_UNINIT ||
				sink_mconfig->m_state == SKL_MODULE_UNINIT)
				continue;

			/* Bind source to sink, mixin is always source */
			ret = skl_bind_modules(skl, src_mconfig, sink_mconfig);
			if (ret)
				return ret;

			/* set module params after bind */
			skl_tplg_set_module_bind_params(src_w,
					src_mconfig, skl);
			skl_tplg_set_module_bind_params(sink,
					sink_mconfig, skl);

			/* Start sinks pipe first */
			if (sink_mconfig->pipe->state != SKL_PIPE_STARTED) {
				if (sink_mconfig->pipe->conn_type !=
							SKL_PIPE_CONN_TYPE_FE)
					ret = skl_run_pipe(skl,
							sink_mconfig->pipe);
				if (ret)
					return ret;
			}
		}
	}

	if (!sink && next_sink)
		return skl_tplg_bind_sinks(next_sink, skl, src_w, src_mconfig);

	return 0;
}

/*
 * A PGA represents a module in a pipeline. So in the Pre-PMU event of PGA
 * we need to do following:
 *   - Bind to sink pipeline
 *      Since the sink pipes can be running and we don't get mixer event on
 *      connect for already running mixer, we need to find the sink pipes
 *      here and bind to them. This way dynamic connect works.
 *   - Start sink pipeline, if not running
 *   - Then run current pipe
 */
static int skl_tplg_pga_dapm_pre_pmu_event(struct snd_soc_dapm_widget *w,
							struct skl_dev *skl)
{
	struct skl_module_cfg *src_mconfig;
	int ret = 0;

	src_mconfig = w->priv;

	/*
	 * find which sink it is connected to, bind with the sink,
	 * if sink is not started, start sink pipe first, then start
	 * this pipe
	 */
	ret = skl_tplg_bind_sinks(w, skl, w, src_mconfig);
	if (ret)
		return ret;

	/* Start source pipe last after starting all sinks */
	if (src_mconfig->pipe->conn_type != SKL_PIPE_CONN_TYPE_FE)
		return skl_run_pipe(skl, src_mconfig->pipe);

	return 0;
}

static struct snd_soc_dapm_widget *skl_get_src_dsp_widget(
		struct snd_soc_dapm_widget *w, struct skl_dev *skl)
{
	struct snd_soc_dapm_path *p;
	struct snd_soc_dapm_widget *src_w = NULL;

	snd_soc_dapm_widget_for_each_source_path(w, p) {
		src_w = p->source;
		if (!p->connect)
			continue;

		dev_dbg(skl->dev, "sink widget=%s\n", w->name);
		dev_dbg(skl->dev, "src widget=%s\n", p->source->name);

		/*
		 * here we will check widgets in sink pipelines, so that can
		 * be any widgets type and we are only interested if they are
		 * ones used for SKL so check that first
		 */
		if ((p->source->priv != NULL) &&
				is_skl_dsp_widget_type(p->source, skl->dev)) {
			return p->source;
		}
	}

	if (src_w != NULL)
		return skl_get_src_dsp_widget(src_w, skl);

	return NULL;
}

/*
 * in the Post-PMU event of mixer we need to do following:
 *   - Check if this pipe is running
 *   - if not, then
 *	- bind this pipeline to its source pipeline
 *	  if source pipe is already running, this means it is a dynamic
 *	  connection and we need to bind only to that pipe
 *	- start this pipeline
 */
static int skl_tplg_mixer_dapm_post_pmu_event(struct snd_soc_dapm_widget *w,
							struct skl_dev *skl)
{
	int ret = 0;
	struct snd_soc_dapm_widget *source, *sink;
	struct skl_module_cfg *src_mconfig, *sink_mconfig;
	int src_pipe_started = 0;

	sink = w;
	sink_mconfig = sink->priv;

	/*
	 * If source pipe is already started, that means source is driving
	 * one more sink before this sink got connected, Since source is
	 * started, bind this sink to source and start this pipe.
	 */
	source = skl_get_src_dsp_widget(w, skl);
	if (source != NULL) {
		src_mconfig = source->priv;
		sink_mconfig = sink->priv;
		src_pipe_started = 1;

		/*
		 * check pipe state, then no need to bind or start the
		 * pipe
		 */
		if (src_mconfig->pipe->state != SKL_PIPE_STARTED)
			src_pipe_started = 0;
	}

	if (src_pipe_started) {
		ret = skl_bind_modules(skl, src_mconfig, sink_mconfig);
		if (ret)
			return ret;

		/* set module params after bind */
		skl_tplg_set_module_bind_params(source, src_mconfig, skl);
		skl_tplg_set_module_bind_params(sink, sink_mconfig, skl);

		if (sink_mconfig->pipe->conn_type != SKL_PIPE_CONN_TYPE_FE)
			ret = skl_run_pipe(skl, sink_mconfig->pipe);
	}

	return ret;
}

/*
 * in the Pre-PMD event of mixer we need to do following:
 *   - Stop the pipe
 *   - find the source connections and remove that from dapm_path_list
 *   - unbind with source pipelines if still connected
 */
static int skl_tplg_mixer_dapm_pre_pmd_event(struct snd_soc_dapm_widget *w,
							struct skl_dev *skl)
{
	struct skl_module_cfg *src_mconfig, *sink_mconfig;
	int ret = 0, i;

	sink_mconfig = w->priv;

	/* Stop the pipe */
	ret = skl_stop_pipe(skl, sink_mconfig->pipe);
	if (ret)
		return ret;

	for (i = 0; i < sink_mconfig->module->max_input_pins; i++) {
		if (sink_mconfig->m_in_pin[i].pin_state == SKL_PIN_BIND_DONE) {
			src_mconfig = sink_mconfig->m_in_pin[i].tgt_mcfg;
			if (!src_mconfig)
				continue;

			ret = skl_unbind_modules(skl,
						src_mconfig, sink_mconfig);
		}
	}

	return ret;
}

/*
 * in the Post-PMD event of mixer we need to do following:
 *   - Unbind the modules within the pipeline
 *   - Delete the pipeline (modules are not required to be explicitly
 *     deleted, pipeline delete is enough here
 */
static int skl_tplg_mixer_dapm_post_pmd_event(struct snd_soc_dapm_widget *w,
							struct skl_dev *skl)
{
	struct skl_module_cfg *mconfig = w->priv;
	struct skl_pipe_module *w_module;
	struct skl_module_cfg *src_module = NULL, *dst_module;
	struct skl_pipe *s_pipe = mconfig->pipe;
	struct skl_module_deferred_bind *modules, *tmp;

	if (s_pipe->state == SKL_PIPE_INVALID)
		return -EINVAL;

	list_for_each_entry(w_module, &s_pipe->w_list, node) {
		if (list_empty(&skl->bind_list))
			break;

		src_module = w_module->w->priv;

		list_for_each_entry_safe(modules, tmp, &skl->bind_list, node) {
			/*
			 * When the destination module is deleted, Unbind the
			 * modules from deferred bind list.
			 */
			if (modules->dst == src_module) {
				skl_unbind_modules(skl, modules->src,
						modules->dst);
			}

			/*
			 * When the source module is deleted, remove this entry
			 * from the deferred bind list.
			 */
			if (modules->src == src_module) {
				list_del(&modules->node);
				modules->src = NULL;
				modules->dst = NULL;
				kfree(modules);
			}
		}
	}

	list_for_each_entry(w_module, &s_pipe->w_list, node) {
		dst_module = w_module->w->priv;

		if (src_module == NULL) {
			src_module = dst_module;
			continue;
		}

		skl_unbind_modules(skl, src_module, dst_module);
		src_module = dst_module;
	}

	skl_delete_pipe(skl, mconfig->pipe);

	list_for_each_entry(w_module, &s_pipe->w_list, node) {
		src_module = w_module->w->priv;
		src_module->m_state = SKL_MODULE_UNINIT;
	}

	return skl_tplg_unload_pipe_modules(skl, s_pipe);
}

/*
 * in the Post-PMD event of PGA we need to do following:
 *   - Stop the pipeline
 *   - In source pipe is connected, unbind with source pipelines
 */
static int skl_tplg_pga_dapm_post_pmd_event(struct snd_soc_dapm_widget *w,
							struct skl_dev *skl)
{
	struct skl_module_cfg *src_mconfig, *sink_mconfig;
	int ret = 0, i;

	src_mconfig = w->priv;

	/* Stop the pipe since this is a mixin module */
	ret = skl_stop_pipe(skl, src_mconfig->pipe);
	if (ret)
		return ret;

	for (i = 0; i < src_mconfig->module->max_output_pins; i++) {
		if (src_mconfig->m_out_pin[i].pin_state == SKL_PIN_BIND_DONE) {
			sink_mconfig = src_mconfig->m_out_pin[i].tgt_mcfg;
			if (!sink_mconfig)
				continue;
			/*
			 * This is a connecter and if path is found that means
			 * unbind between source and sink has not happened yet
			 */
			ret = skl_unbind_modules(skl, src_mconfig,
							sink_mconfig);
		}
	}

	return ret;
}

/*
 * In modelling, we assume there will be ONLY one mixer in a pipeline. If a
 * second one is required that is created as another pipe entity.
 * The mixer is responsible for pipe management and represent a pipeline
 * instance
 */
static int skl_tplg_mixer_event(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *k, int event)
{
	struct snd_soc_dapm_context *dapm = w->dapm;
	struct skl_dev *skl = get_skl_ctx(dapm->dev);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		return skl_tplg_mixer_dapm_pre_pmu_event(w, skl);

	case SND_SOC_DAPM_POST_PMU:
		return skl_tplg_mixer_dapm_post_pmu_event(w, skl);

	case SND_SOC_DAPM_PRE_PMD:
		return skl_tplg_mixer_dapm_pre_pmd_event(w, skl);

	case SND_SOC_DAPM_POST_PMD:
		return skl_tplg_mixer_dapm_post_pmd_event(w, skl);
	}

	return 0;
}

/*
 * In modelling, we assumed rest of the modules in pipeline are PGA. But we
 * are interested in last PGA (leaf PGA) in a pipeline to disconnect with
 * the sink when it is running (two FE to one BE or one FE to two BE)
 * scenarios
 */
static int skl_tplg_pga_event(struct snd_soc_dapm_widget *w,
			struct snd_kcontrol *k, int event)

{
	struct snd_soc_dapm_context *dapm = w->dapm;
	struct skl_dev *skl = get_skl_ctx(dapm->dev);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		return skl_tplg_pga_dapm_pre_pmu_event(w, skl);

	case SND_SOC_DAPM_POST_PMD:
		return skl_tplg_pga_dapm_post_pmd_event(w, skl);
	}

	return 0;
}

static int skl_tplg_multi_config_set_get(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_value *ucontrol,
					 bool is_set)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct hdac_bus *bus = snd_soc_component_get_drvdata(component);
	struct skl_dev *skl = bus_to_skl(bus);
	struct skl_pipeline *ppl;
	struct skl_pipe *pipe = NULL;
	struct soc_enum *ec = (struct soc_enum *)kcontrol->private_value;
	u32 *pipe_id;

	if (!ec)
		return -EINVAL;

	if (is_set && ucontrol->value.enumerated.item[0] > ec->items)
		return -EINVAL;

	pipe_id = ec->dobj.private;

	list_for_each_entry(ppl, &skl->ppl_list, node) {
		if (ppl->pipe->ppl_id == *pipe_id) {
			pipe = ppl->pipe;
			break;
		}
	}
	if (!pipe)
		return -EIO;

	if (is_set)
		pipe->pipe_config_idx = ucontrol->value.enumerated.item[0];
	else
		ucontrol->value.enumerated.item[0]  =  pipe->pipe_config_idx;

	return 0;
}

static int skl_tplg_multi_config_get(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	return skl_tplg_multi_config_set_get(kcontrol, ucontrol, false);
}

static int skl_tplg_multi_config_set(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	return skl_tplg_multi_config_set_get(kcontrol, ucontrol, true);
}

static int skl_tplg_multi_config_get_dmic(struct snd_kcontrol *kcontrol,
					  struct snd_ctl_elem_value *ucontrol)
{
	return skl_tplg_multi_config_set_get(kcontrol, ucontrol, false);
}

static int skl_tplg_multi_config_set_dmic(struct snd_kcontrol *kcontrol,
					  struct snd_ctl_elem_value *ucontrol)
{
	return skl_tplg_multi_config_set_get(kcontrol, ucontrol, true);
}

static int skl_tplg_tlv_control_get(struct snd_kcontrol *kcontrol,
			unsigned int __user *data, unsigned int size)
{
	struct soc_bytes_ext *sb =
			(struct soc_bytes_ext *)kcontrol->private_value;
	struct skl_algo_data *bc = (struct skl_algo_data *)sb->dobj.private;
	struct snd_soc_dapm_widget *w = snd_soc_dapm_kcontrol_widget(kcontrol);
	struct skl_module_cfg *mconfig = w->priv;
	struct skl_dev *skl = get_skl_ctx(w->dapm->dev);

	if (w->power)
		skl_get_module_params(skl, (u32 *)bc->params,
				      bc->size, bc->param_id, mconfig);

	/* decrement size for TLV header */
	size -= 2 * sizeof(u32);

	/* check size as we don't want to send kernel data */
	if (size > bc->max)
		size = bc->max;

	if (bc->params) {
		if (copy_to_user(data, &bc->param_id, sizeof(u32)))
			return -EFAULT;
		if (copy_to_user(data + 1, &size, sizeof(u32)))
			return -EFAULT;
		if (copy_to_user(data + 2, bc->params, size))
			return -EFAULT;
	}

	return 0;
}

#define SKL_PARAM_VENDOR_ID 0xff

static int skl_tplg_tlv_control_set(struct snd_kcontrol *kcontrol,
			const unsigned int __user *data, unsigned int size)
{
	struct snd_soc_dapm_widget *w = snd_soc_dapm_kcontrol_widget(kcontrol);
	struct skl_module_cfg *mconfig = w->priv;
	struct soc_bytes_ext *sb =
			(struct soc_bytes_ext *)kcontrol->private_value;
	struct skl_algo_data *ac = (struct skl_algo_data *)sb->dobj.private;
	struct skl_dev *skl = get_skl_ctx(w->dapm->dev);

	if (ac->params) {
		/*
		 * Widget data is expected to be stripped of T and L
		 */
		size -= 2 * sizeof(unsigned int);
		data += 2;

		if (size > ac->max)
			return -EINVAL;
		ac->size = size;

		if (copy_from_user(ac->params, data, size))
			return -EFAULT;

		if (w->power)
			return skl_set_module_params(skl,
						(u32 *)ac->params, ac->size,
						ac->param_id, mconfig);
	}

	return 0;
}

static int skl_tplg_mic_control_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_widget *w = snd_soc_dapm_kcontrol_widget(kcontrol);
	struct skl_module_cfg *mconfig = w->priv;
	struct soc_enum *ec = (struct soc_enum *)kcontrol->private_value;
	u32 ch_type = *((u32 *)ec->dobj.private);

	if (mconfig->dmic_ch_type == ch_type)
		ucontrol->value.enumerated.item[0] =
					mconfig->dmic_ch_combo_index;
	else
		ucontrol->value.enumerated.item[0] = 0;

	return 0;
}

static int skl_fill_mic_sel_params(struct skl_module_cfg *mconfig,
	struct skl_mic_sel_config *mic_cfg, struct device *dev)
{
	struct skl_specific_cfg *sp_cfg = &mconfig->formats_config;

	sp_cfg->caps_size = sizeof(struct skl_mic_sel_config);
	sp_cfg->set_params = SKL_PARAM_SET;
	sp_cfg->param_id = 0x00;
	if (!sp_cfg->caps) {
		sp_cfg->caps = devm_kzalloc(dev, sp_cfg->caps_size, GFP_KERNEL);
		if (!sp_cfg->caps)
			return -ENOMEM;
	}

	mic_cfg->mic_switch = SKL_MIC_SEL_SWITCH;
	mic_cfg->flags = 0;
	memcpy(sp_cfg->caps, mic_cfg, sp_cfg->caps_size);

	return 0;
}

static int skl_tplg_mic_control_set(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_widget *w = snd_soc_dapm_kcontrol_widget(kcontrol);
	struct skl_module_cfg *mconfig = w->priv;
	struct skl_mic_sel_config mic_cfg = {0};
	struct soc_enum *ec = (struct soc_enum *)kcontrol->private_value;
	u32 ch_type = *((u32 *)ec->dobj.private);
	const int *list;
	u8 in_ch, out_ch, index;

	mconfig->dmic_ch_type = ch_type;
	mconfig->dmic_ch_combo_index = ucontrol->value.enumerated.item[0];

	/* enum control index 0 is INVALID, so no channels to be set */
	if (mconfig->dmic_ch_combo_index == 0)
		return 0;

	/* No valid channel selection map for index 0, so offset by 1 */
	index = mconfig->dmic_ch_combo_index - 1;

	switch (ch_type) {
	case SKL_CH_MONO:
		if (mconfig->dmic_ch_combo_index > ARRAY_SIZE(mic_mono_list))
			return -EINVAL;

		list = &mic_mono_list[index];
		break;

	case SKL_CH_STEREO:
		if (mconfig->dmic_ch_combo_index > ARRAY_SIZE(mic_stereo_list))
			return -EINVAL;

		list = mic_stereo_list[index];
		break;

	case SKL_CH_TRIO:
		if (mconfig->dmic_ch_combo_index > ARRAY_SIZE(mic_trio_list))
			return -EINVAL;

		list = mic_trio_list[index];
		break;

	case SKL_CH_QUATRO:
		if (mconfig->dmic_ch_combo_index > ARRAY_SIZE(mic_quatro_list))
			return -EINVAL;

		list = mic_quatro_list[index];
		break;

	default:
		dev_err(w->dapm->dev,
				"Invalid channel %d for mic_select module\n",
				ch_type);
		return -EINVAL;

	}

	/* channel type enum map to number of chanels for that type */
	for (out_ch = 0; out_ch < ch_type; out_ch++) {
		in_ch = list[out_ch];
		mic_cfg.blob[out_ch][in_ch] = SKL_DEFAULT_MIC_SEL_GAIN;
	}

	return skl_fill_mic_sel_params(mconfig, &mic_cfg, w->dapm->dev);
}

/*
 * Fill the dma id for host and link. In case of passthrough
 * pipeline, this will both host and link in the same
 * pipeline, so need to copy the link and host based on dev_type
 */
static void skl_tplg_fill_dma_id(struct skl_module_cfg *mcfg,
				struct skl_pipe_params *params)
{
	struct skl_pipe *pipe = mcfg->pipe;

	if (pipe->passthru) {
		switch (mcfg->dev_type) {
		case SKL_DEVICE_HDALINK:
			pipe->p_params->link_dma_id = params->link_dma_id;
			pipe->p_params->link_index = params->link_index;
			pipe->p_params->link_bps = params->link_bps;
			break;

		case SKL_DEVICE_HDAHOST:
			pipe->p_params->host_dma_id = params->host_dma_id;
			pipe->p_params->host_bps = params->host_bps;
			break;

		default:
			break;
		}
		pipe->p_params->s_fmt = params->s_fmt;
		pipe->p_params->ch = params->ch;
		pipe->p_params->s_freq = params->s_freq;
		pipe->p_params->stream = params->stream;
		pipe->p_params->format = params->format;

	} else {
		memcpy(pipe->p_params, params, sizeof(*params));
	}
}

/*
 * The FE params are passed by hw_params of the DAI.
 * On hw_params, the params are stored in Gateway module of the FE and we
 * need to calculate the format in DSP module configuration, that
 * conversion is done here
 */
int skl_tplg_update_pipe_params(struct device *dev,
			struct skl_module_cfg *mconfig,
			struct skl_pipe_params *params)
{
	struct skl_module_res *res = &mconfig->module->resources[0];
	struct skl_dev *skl = get_skl_ctx(dev);
	struct skl_module_fmt *format = NULL;
	u8 cfg_idx = mconfig->pipe->cur_config_idx;

	skl_tplg_fill_dma_id(mconfig, params);
	mconfig->fmt_idx = mconfig->mod_cfg[cfg_idx].fmt_idx;
	mconfig->res_idx = mconfig->mod_cfg[cfg_idx].res_idx;

	if (skl->nr_modules)
		return 0;

	if (params->stream == SNDRV_PCM_STREAM_PLAYBACK)
		format = &mconfig->module->formats[0].inputs[0].fmt;
	else
		format = &mconfig->module->formats[0].outputs[0].fmt;

	/* set the hw_params */
	format->s_freq = params->s_freq;
	format->channels = params->ch;
	format->valid_bit_depth = skl_get_bit_depth(params->s_fmt);

	/*
	 * 16 bit is 16 bit container whereas 24 bit is in 32 bit
	 * container so update bit depth accordingly
	 */
	switch (format->valid_bit_depth) {
	case SKL_DEPTH_16BIT:
		format->bit_depth = format->valid_bit_depth;
		break;

	case SKL_DEPTH_24BIT:
	case SKL_DEPTH_32BIT:
		format->bit_depth = SKL_DEPTH_32BIT;
		break;

	default:
		dev_err(dev, "Invalid bit depth %x for pipe\n",
				format->valid_bit_depth);
		return -EINVAL;
	}

	if (params->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		res->ibs = (format->s_freq / 1000) *
				(format->channels) *
				(format->bit_depth >> 3);
	} else {
		res->obs = (format->s_freq / 1000) *
				(format->channels) *
				(format->bit_depth >> 3);
	}

	return 0;
}

/*
 * Query the module config for the FE DAI
 * This is used to find the hw_params set for that DAI and apply to FE
 * pipeline
 */
struct skl_module_cfg *
skl_tplg_fe_get_cpr_module(struct snd_soc_dai *dai, int stream)
{
	struct snd_soc_dapm_widget *w;
	struct snd_soc_dapm_path *p = NULL;

	if (stream == SNDRV_PCM_STREAM_PLAYBACK) {
		w = dai->playback_widget;
		snd_soc_dapm_widget_for_each_sink_path(w, p) {
			if (p->connect && p->sink->power &&
				!is_skl_dsp_widget_type(p->sink, dai->dev))
				continue;

			if (p->sink->priv) {
				dev_dbg(dai->dev, "set params for %s\n",
						p->sink->name);
				return p->sink->priv;
			}
		}
	} else {
		w = dai->capture_widget;
		snd_soc_dapm_widget_for_each_source_path(w, p) {
			if (p->connect && p->source->power &&
				!is_skl_dsp_widget_type(p->source, dai->dev))
				continue;

			if (p->source->priv) {
				dev_dbg(dai->dev, "set params for %s\n",
						p->source->name);
				return p->source->priv;
			}
		}
	}

	return NULL;
}

static struct skl_module_cfg *skl_get_mconfig_pb_cpr(
		struct snd_soc_dai *dai, struct snd_soc_dapm_widget *w)
{
	struct snd_soc_dapm_path *p;
	struct skl_module_cfg *mconfig = NULL;

	snd_soc_dapm_widget_for_each_source_path(w, p) {
		if (w->endpoints[SND_SOC_DAPM_DIR_OUT] > 0) {
			if (p->connect &&
				    (p->sink->id == snd_soc_dapm_aif_out) &&
				    p->source->priv) {
				mconfig = p->source->priv;
				return mconfig;
			}
			mconfig = skl_get_mconfig_pb_cpr(dai, p->source);
			if (mconfig)
				return mconfig;
		}
	}
	return mconfig;
}

static struct skl_module_cfg *skl_get_mconfig_cap_cpr(
		struct snd_soc_dai *dai, struct snd_soc_dapm_widget *w)
{
	struct snd_soc_dapm_path *p;
	struct skl_module_cfg *mconfig = NULL;

	snd_soc_dapm_widget_for_each_sink_path(w, p) {
		if (w->endpoints[SND_SOC_DAPM_DIR_IN] > 0) {
			if (p->connect &&
				    (p->source->id == snd_soc_dapm_aif_in) &&
				    p->sink->priv) {
				mconfig = p->sink->priv;
				return mconfig;
			}
			mconfig = skl_get_mconfig_cap_cpr(dai, p->sink);
			if (mconfig)
				return mconfig;
		}
	}
	return mconfig;
}

struct skl_module_cfg *
skl_tplg_be_get_cpr_module(struct snd_soc_dai *dai, int stream)
{
	struct snd_soc_dapm_widget *w;
	struct skl_module_cfg *mconfig;

	if (stream == SNDRV_PCM_STREAM_PLAYBACK) {
		w = dai->playback_widget;
		mconfig = skl_get_mconfig_pb_cpr(dai, w);
	} else {
		w = dai->capture_widget;
		mconfig = skl_get_mconfig_cap_cpr(dai, w);
	}
	return mconfig;
}

static u8 skl_tplg_be_link_type(int dev_type)
{
	int ret;

	switch (dev_type) {
	case SKL_DEVICE_BT:
		ret = NHLT_LINK_SSP;
		break;

	case SKL_DEVICE_DMIC:
		ret = NHLT_LINK_DMIC;
		break;

	case SKL_DEVICE_I2S:
		ret = NHLT_LINK_SSP;
		break;

	case SKL_DEVICE_HDALINK:
		ret = NHLT_LINK_HDA;
		break;

	default:
		ret = NHLT_LINK_INVALID;
		break;
	}

	return ret;
}

/*
 * Fill the BE gateway parameters
 * The BE gateway expects a blob of parameters which are kept in the ACPI
 * NHLT blob, so query the blob for interface type (i2s/pdm) and instance.
 * The port can have multiple settings so pick based on the PCM
 * parameters
 */
static int skl_tplg_be_fill_pipe_params(struct snd_soc_dai *dai,
				struct skl_module_cfg *mconfig,
				struct skl_pipe_params *params)
{
	struct nhlt_specific_cfg *cfg;
	struct skl_dev *skl = get_skl_ctx(dai->dev);
	int link_type = skl_tplg_be_link_type(mconfig->dev_type);
	u8 dev_type = skl_tplg_be_dev_type(mconfig->dev_type);

	skl_tplg_fill_dma_id(mconfig, params);

	if (link_type == NHLT_LINK_HDA)
		return 0;

	/* update the blob based on virtual bus_id*/
	cfg = skl_get_ep_blob(skl, mconfig->vbus_id, link_type,
					params->s_fmt, params->ch,
					params->s_freq, params->stream,
					dev_type);
	if (cfg) {
		mconfig->formats_config.caps_size = cfg->size;
		mconfig->formats_config.caps = (u32 *) &cfg->caps;
	} else {
		dev_err(dai->dev, "Blob NULL for id %x type %d dirn %d\n",
					mconfig->vbus_id, link_type,
					params->stream);
		dev_err(dai->dev, "PCM: ch %d, freq %d, fmt %d\n",
				 params->ch, params->s_freq, params->s_fmt);
		return -EINVAL;
	}

	return 0;
}

static int skl_tplg_be_set_src_pipe_params(struct snd_soc_dai *dai,
				struct snd_soc_dapm_widget *w,
				struct skl_pipe_params *params)
{
	struct snd_soc_dapm_path *p;
	int ret = -EIO;

	snd_soc_dapm_widget_for_each_source_path(w, p) {
		if (p->connect && is_skl_dsp_widget_type(p->source, dai->dev) &&
						p->source->priv) {

			ret = skl_tplg_be_fill_pipe_params(dai,
						p->source->priv, params);
			if (ret < 0)
				return ret;
		} else {
			ret = skl_tplg_be_set_src_pipe_params(dai,
						p->source, params);
			if (ret < 0)
				return ret;
		}
	}

	return ret;
}

static int skl_tplg_be_set_sink_pipe_params(struct snd_soc_dai *dai,
	struct snd_soc_dapm_widget *w, struct skl_pipe_params *params)
{
	struct snd_soc_dapm_path *p;
	int ret = -EIO;

	snd_soc_dapm_widget_for_each_sink_path(w, p) {
		if (p->connect && is_skl_dsp_widget_type(p->sink, dai->dev) &&
						p->sink->priv) {

			ret = skl_tplg_be_fill_pipe_params(dai,
						p->sink->priv, params);
			if (ret < 0)
				return ret;
		} else {
			ret = skl_tplg_be_set_sink_pipe_params(
						dai, p->sink, params);
			if (ret < 0)
				return ret;
		}
	}

	return ret;
}

/*
 * BE hw_params can be a source parameters (capture) or sink parameters
 * (playback). Based on sink and source we need to either find the source
 * list or the sink list and set the pipeline parameters
 */
int skl_tplg_be_update_params(struct snd_soc_dai *dai,
				struct skl_pipe_params *params)
{
	struct snd_soc_dapm_widget *w;

	if (params->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		w = dai->playback_widget;

		return skl_tplg_be_set_src_pipe_params(dai, w, params);

	} else {
		w = dai->capture_widget;

		return skl_tplg_be_set_sink_pipe_params(dai, w, params);
	}

	return 0;
}

static const struct snd_soc_tplg_widget_events skl_tplg_widget_ops[] = {
	{SKL_MIXER_EVENT, skl_tplg_mixer_event},
	{SKL_VMIXER_EVENT, skl_tplg_mixer_event},
	{SKL_PGA_EVENT, skl_tplg_pga_event},
};

static const struct snd_soc_tplg_bytes_ext_ops skl_tlv_ops[] = {
	{SKL_CONTROL_TYPE_BYTE_TLV, skl_tplg_tlv_control_get,
					skl_tplg_tlv_control_set},
};

static const struct snd_soc_tplg_kcontrol_ops skl_tplg_kcontrol_ops[] = {
	{
		.id = SKL_CONTROL_TYPE_MIC_SELECT,
		.get = skl_tplg_mic_control_get,
		.put = skl_tplg_mic_control_set,
	},
	{
		.id = SKL_CONTROL_TYPE_MULTI_IO_SELECT,
		.get = skl_tplg_multi_config_get,
		.put = skl_tplg_multi_config_set,
	},
	{
		.id = SKL_CONTROL_TYPE_MULTI_IO_SELECT_DMIC,
		.get = skl_tplg_multi_config_get_dmic,
		.put = skl_tplg_multi_config_set_dmic,
	}
};

static int skl_tplg_fill_pipe_cfg(struct device *dev,
			struct skl_pipe *pipe, u32 tkn,
			u32 tkn_val, int conf_idx, int dir)
{
	struct skl_pipe_fmt *fmt;
	struct skl_path_config *config;

	switch (dir) {
	case SKL_DIR_IN:
		fmt = &pipe->configs[conf_idx].in_fmt;
		break;

	case SKL_DIR_OUT:
		fmt = &pipe->configs[conf_idx].out_fmt;
		break;

	default:
		dev_err(dev, "Invalid direction: %d\n", dir);
		return -EINVAL;
	}

	config = &pipe->configs[conf_idx];

	switch (tkn) {
	case SKL_TKN_U32_CFG_FREQ:
		fmt->freq = tkn_val;
		break;

	case SKL_TKN_U8_CFG_CHAN:
		fmt->channels = tkn_val;
		break;

	case SKL_TKN_U8_CFG_BPS:
		fmt->bps = tkn_val;
		break;

	case SKL_TKN_U32_PATH_MEM_PGS:
		config->mem_pages = tkn_val;
		break;

	default:
		dev_err(dev, "Invalid token config: %d\n", tkn);
		return -EINVAL;
	}

	return 0;
}

static int skl_tplg_fill_pipe_tkn(struct device *dev,
			struct skl_pipe *pipe, u32 tkn,
			u32 tkn_val)
{

	switch (tkn) {
	case SKL_TKN_U32_PIPE_CONN_TYPE:
		pipe->conn_type = tkn_val;
		break;

	case SKL_TKN_U32_PIPE_PRIORITY:
		pipe->pipe_priority = tkn_val;
		break;

	case SKL_TKN_U32_PIPE_MEM_PGS:
		pipe->memory_pages = tkn_val;
		break;

	case SKL_TKN_U32_PMODE:
		pipe->lp_mode = tkn_val;
		break;

	case SKL_TKN_U32_PIPE_DIRECTION:
		pipe->direction = tkn_val;
		break;

	case SKL_TKN_U32_NUM_CONFIGS:
		pipe->nr_cfgs = tkn_val;
		break;

	default:
		dev_err(dev, "Token not handled %d\n", tkn);
		return -EINVAL;
	}

	return 0;
}

/*
 * Add pipeline by parsing the relevant tokens
 * Return an existing pipe if the pipe already exists.
 */
static int skl_tplg_add_pipe(struct device *dev,
		struct skl_module_cfg *mconfig, struct skl_dev *skl,
		struct snd_soc_tplg_vendor_value_elem *tkn_elem)
{
	struct skl_pipeline *ppl;
	struct skl_pipe *pipe;
	struct skl_pipe_params *params;

	list_for_each_entry(ppl, &skl->ppl_list, node) {
		if (ppl->pipe->ppl_id == tkn_elem->value) {
			mconfig->pipe = ppl->pipe;
			return -EEXIST;
		}
	}

	ppl = devm_kzalloc(dev, sizeof(*ppl), GFP_KERNEL);
	if (!ppl)
		return -ENOMEM;

	pipe = devm_kzalloc(dev, sizeof(*pipe), GFP_KERNEL);
	if (!pipe)
		return -ENOMEM;

	params = devm_kzalloc(dev, sizeof(*params), GFP_KERNEL);
	if (!params)
		return -ENOMEM;

	pipe->p_params = params;
	pipe->ppl_id = tkn_elem->value;
	INIT_LIST_HEAD(&pipe->w_list);

	ppl->pipe = pipe;
	list_add(&ppl->node, &skl->ppl_list);

	mconfig->pipe = pipe;
	mconfig->pipe->state = SKL_PIPE_INVALID;

	return 0;
}

static int skl_tplg_get_uuid(struct device *dev, guid_t *guid,
	      struct snd_soc_tplg_vendor_uuid_elem *uuid_tkn)
{
	if (uuid_tkn->token == SKL_TKN_UUID) {
		import_guid(guid, uuid_tkn->uuid);
		return 0;
	}

	dev_err(dev, "Not an UUID token %d\n", uuid_tkn->token);

	return -EINVAL;
}

static int skl_tplg_fill_pin(struct device *dev,
			struct snd_soc_tplg_vendor_value_elem *tkn_elem,
			struct skl_module_pin *m_pin,
			int pin_index)
{
	int ret;

	switch (tkn_elem->token) {
	case SKL_TKN_U32_PIN_MOD_ID:
		m_pin[pin_index].id.module_id = tkn_elem->value;
		break;

	case SKL_TKN_U32_PIN_INST_ID:
		m_pin[pin_index].id.instance_id = tkn_elem->value;
		break;

	case SKL_TKN_UUID:
		ret = skl_tplg_get_uuid(dev, &m_pin[pin_index].id.mod_uuid,
			(struct snd_soc_tplg_vendor_uuid_elem *)tkn_elem);
		if (ret < 0)
			return ret;

		break;

	default:
		dev_err(dev, "%d Not a pin token\n", tkn_elem->token);
		return -EINVAL;
	}

	return 0;
}

/*
 * Parse for pin config specific tokens to fill up the
 * module private data
 */
static int skl_tplg_fill_pins_info(struct device *dev,
		struct skl_module_cfg *mconfig,
		struct snd_soc_tplg_vendor_value_elem *tkn_elem,
		int dir, int pin_count)
{
	int ret;
	struct skl_module_pin *m_pin;

	switch (dir) {
	case SKL_DIR_IN:
		m_pin = mconfig->m_in_pin;
		break;

	case SKL_DIR_OUT:
		m_pin = mconfig->m_out_pin;
		break;

	default:
		dev_err(dev, "Invalid direction value\n");
		return -EINVAL;
	}

	ret = skl_tplg_fill_pin(dev, tkn_elem, m_pin, pin_count);
	if (ret < 0)
		return ret;

	m_pin[pin_count].in_use = false;
	m_pin[pin_count].pin_state = SKL_PIN_UNBIND;

	return 0;
}

/*
 * Fill up input/output module config format based
 * on the direction
 */
static int skl_tplg_fill_fmt(struct device *dev,
		struct skl_module_fmt *dst_fmt,
		u32 tkn, u32 value)
{
	switch (tkn) {
	case SKL_TKN_U32_FMT_CH:
		dst_fmt->channels  = value;
		break;

	case SKL_TKN_U32_FMT_FREQ:
		dst_fmt->s_freq = value;
		break;

	case SKL_TKN_U32_FMT_BIT_DEPTH:
		dst_fmt->bit_depth = value;
		break;

	case SKL_TKN_U32_FMT_SAMPLE_SIZE:
		dst_fmt->valid_bit_depth = value;
		break;

	case SKL_TKN_U32_FMT_CH_CONFIG:
		dst_fmt->ch_cfg = value;
		break;

	case SKL_TKN_U32_FMT_INTERLEAVE:
		dst_fmt->interleaving_style = value;
		break;

	case SKL_TKN_U32_FMT_SAMPLE_TYPE:
		dst_fmt->sample_type = value;
		break;

	case SKL_TKN_U32_FMT_CH_MAP:
		dst_fmt->ch_map = value;
		break;

	default:
		dev_err(dev, "Invalid token %d\n", tkn);
		return -EINVAL;
	}

	return 0;
}

static int skl_tplg_widget_fill_fmt(struct device *dev,
		struct skl_module_iface *fmt,
		u32 tkn, u32 val, u32 dir, int fmt_idx)
{
	struct skl_module_fmt *dst_fmt;

	if (!fmt)
		return -EINVAL;

	switch (dir) {
	case SKL_DIR_IN:
		dst_fmt = &fmt->inputs[fmt_idx].fmt;
		break;

	case SKL_DIR_OUT:
		dst_fmt = &fmt->outputs[fmt_idx].fmt;
		break;

	default:
		dev_err(dev, "Invalid direction: %d\n", dir);
		return -EINVAL;
	}

	return skl_tplg_fill_fmt(dev, dst_fmt, tkn, val);
}

static void skl_tplg_fill_pin_dynamic_val(
		struct skl_module_pin *mpin, u32 pin_count, u32 value)
{
	int i;

	for (i = 0; i < pin_count; i++)
		mpin[i].is_dynamic = value;
}

/*
 * Resource table in the manifest has pin specific resources
 * like pin and pin buffer size
 */
static int skl_tplg_manifest_pin_res_tkn(struct device *dev,
		struct snd_soc_tplg_vendor_value_elem *tkn_elem,
		struct skl_module_res *res, int pin_idx, int dir)
{
	struct skl_module_pin_resources *m_pin;

	switch (dir) {
	case SKL_DIR_IN:
		m_pin = &res->input[pin_idx];
		break;

	case SKL_DIR_OUT:
		m_pin = &res->output[pin_idx];
		break;

	default:
		dev_err(dev, "Invalid pin direction: %d\n", dir);
		return -EINVAL;
	}

	switch (tkn_elem->token) {
	case SKL_TKN_MM_U32_RES_PIN_ID:
		m_pin->pin_index = tkn_elem->value;
		break;

	case SKL_TKN_MM_U32_PIN_BUF:
		m_pin->buf_size = tkn_elem->value;
		break;

	default:
		dev_err(dev, "Invalid token: %d\n", tkn_elem->token);
		return -EINVAL;
	}

	return 0;
}

/*
 * Fill module specific resources from the manifest's resource
 * table like CPS, DMA size, mem_pages.
 */
static int skl_tplg_fill_res_tkn(struct device *dev,
		struct snd_soc_tplg_vendor_value_elem *tkn_elem,
		struct skl_module_res *res,
		int pin_idx, int dir)
{
	int ret, tkn_count = 0;

	if (!res)
		return -EINVAL;

	switch (tkn_elem->token) {
	case SKL_TKN_MM_U32_DMA_SIZE:
		res->dma_buffer_size = tkn_elem->value;
		break;

	case SKL_TKN_MM_U32_CPC:
		res->cpc = tkn_elem->value;
		break;

	case SKL_TKN_U32_MEM_PAGES:
		res->is_pages = tkn_elem->value;
		break;

	case SKL_TKN_U32_OBS:
		res->obs = tkn_elem->value;
		break;

	case SKL_TKN_U32_IBS:
		res->ibs = tkn_elem->value;
		break;

	case SKL_TKN_MM_U32_RES_PIN_ID:
	case SKL_TKN_MM_U32_PIN_BUF:
		ret = skl_tplg_manifest_pin_res_tkn(dev, tkn_elem, res,
						    pin_idx, dir);
		if (ret < 0)
			return ret;
		break;

	case SKL_TKN_MM_U32_CPS:
	case SKL_TKN_U32_MAX_MCPS:
		/* ignore unused tokens */
		break;

	default:
		dev_err(dev, "Not a res type token: %d", tkn_elem->token);
		return -EINVAL;

	}
	tkn_count++;

	return tkn_count;
}

/*
 * Parse tokens to fill up the module private data
 */
static int skl_tplg_get_token(struct device *dev,
		struct snd_soc_tplg_vendor_value_elem *tkn_elem,
		struct skl_dev *skl, struct skl_module_cfg *mconfig)
{
	int tkn_count = 0;
	int ret;
	static int is_pipe_exists;
	static int pin_index, dir, conf_idx;
	struct skl_module_iface *iface = NULL;
	struct skl_module_res *res = NULL;
	int res_idx = mconfig->res_idx;
	int fmt_idx = mconfig->fmt_idx;

	/*
	 * If the manifest structure contains no modules, fill all
	 * the module data to 0th index.
	 * res_idx and fmt_idx are default set to 0.
	 */
	if (skl->nr_modules == 0) {
		res = &mconfig->module->resources[res_idx];
		iface = &mconfig->module->formats[fmt_idx];
	}

	if (tkn_elem->token > SKL_TKN_MAX)
		return -EINVAL;

	switch (tkn_elem->token) {
	case SKL_TKN_U8_IN_QUEUE_COUNT:
		mconfig->module->max_input_pins = tkn_elem->value;
		break;

	case SKL_TKN_U8_OUT_QUEUE_COUNT:
		mconfig->module->max_output_pins = tkn_elem->value;
		break;

	case SKL_TKN_U8_DYN_IN_PIN:
		if (!mconfig->m_in_pin)
			mconfig->m_in_pin =
				devm_kcalloc(dev, MAX_IN_QUEUE,
					     sizeof(*mconfig->m_in_pin),
					     GFP_KERNEL);
		if (!mconfig->m_in_pin)
			return -ENOMEM;

		skl_tplg_fill_pin_dynamic_val(mconfig->m_in_pin, MAX_IN_QUEUE,
					      tkn_elem->value);
		break;

	case SKL_TKN_U8_DYN_OUT_PIN:
		if (!mconfig->m_out_pin)
			mconfig->m_out_pin =
				devm_kcalloc(dev, MAX_IN_QUEUE,
					     sizeof(*mconfig->m_in_pin),
					     GFP_KERNEL);
		if (!mconfig->m_out_pin)
			return -ENOMEM;

		skl_tplg_fill_pin_dynamic_val(mconfig->m_out_pin, MAX_OUT_QUEUE,
					      tkn_elem->value);
		break;

	case SKL_TKN_U8_TIME_SLOT:
		mconfig->time_slot = tkn_elem->value;
		break;

	case SKL_TKN_U8_CORE_ID:
		mconfig->core_id = tkn_elem->value;
		break;

	case SKL_TKN_U8_MOD_TYPE:
		mconfig->m_type = tkn_elem->value;
		break;

	case SKL_TKN_U8_DEV_TYPE:
		mconfig->dev_type = tkn_elem->value;
		break;

	case SKL_TKN_U8_HW_CONN_TYPE:
		mconfig->hw_conn_type = tkn_elem->value;
		break;

	case SKL_TKN_U16_MOD_INST_ID:
		mconfig->id.instance_id =
		tkn_elem->value;
		break;

	case SKL_TKN_U32_MEM_PAGES:
	case SKL_TKN_U32_MAX_MCPS:
	case SKL_TKN_U32_OBS:
	case SKL_TKN_U32_IBS:
		ret = skl_tplg_fill_res_tkn(dev, tkn_elem, res, pin_index, dir);
		if (ret < 0)
			return ret;

		break;

	case SKL_TKN_U32_VBUS_ID:
		mconfig->vbus_id = tkn_elem->value;
		break;

	case SKL_TKN_U32_PARAMS_FIXUP:
		mconfig->params_fixup = tkn_elem->value;
		break;

	case SKL_TKN_U32_CONVERTER:
		mconfig->converter = tkn_elem->value;
		break;

	case SKL_TKN_U32_D0I3_CAPS:
		mconfig->d0i3_caps = tkn_elem->value;
		break;

	case SKL_TKN_U32_PIPE_ID:
		ret = skl_tplg_add_pipe(dev,
				mconfig, skl, tkn_elem);

		if (ret < 0) {
			if (ret == -EEXIST) {
				is_pipe_exists = 1;
				break;
			}
			return is_pipe_exists;
		}

		break;

	case SKL_TKN_U32_PIPE_CONFIG_ID:
		conf_idx = tkn_elem->value;
		break;

	case SKL_TKN_U32_PIPE_CONN_TYPE:
	case SKL_TKN_U32_PIPE_PRIORITY:
	case SKL_TKN_U32_PIPE_MEM_PGS:
	case SKL_TKN_U32_PMODE:
	case SKL_TKN_U32_PIPE_DIRECTION:
	case SKL_TKN_U32_NUM_CONFIGS:
		if (is_pipe_exists) {
			ret = skl_tplg_fill_pipe_tkn(dev, mconfig->pipe,
					tkn_elem->token, tkn_elem->value);
			if (ret < 0)
				return ret;
		}

		break;

	case SKL_TKN_U32_PATH_MEM_PGS:
	case SKL_TKN_U32_CFG_FREQ:
	case SKL_TKN_U8_CFG_CHAN:
	case SKL_TKN_U8_CFG_BPS:
		if (mconfig->pipe->nr_cfgs) {
			ret = skl_tplg_fill_pipe_cfg(dev, mconfig->pipe,
					tkn_elem->token, tkn_elem->value,
					conf_idx, dir);
			if (ret < 0)
				return ret;
		}
		break;

	case SKL_TKN_CFG_MOD_RES_ID:
		mconfig->mod_cfg[conf_idx].res_idx = tkn_elem->value;
		break;

	case SKL_TKN_CFG_MOD_FMT_ID:
		mconfig->mod_cfg[conf_idx].fmt_idx = tkn_elem->value;
		break;

	/*
	 * SKL_TKN_U32_DIR_PIN_COUNT token has the value for both
	 * direction and the pin count. The first four bits represent
	 * direction and next four the pin count.
	 */
	case SKL_TKN_U32_DIR_PIN_COUNT:
		dir = tkn_elem->value & SKL_IN_DIR_BIT_MASK;
		pin_index = (tkn_elem->value &
			SKL_PIN_COUNT_MASK) >> 4;

		break;

	case SKL_TKN_U32_FMT_CH:
	case SKL_TKN_U32_FMT_FREQ:
	case SKL_TKN_U32_FMT_BIT_DEPTH:
	case SKL_TKN_U32_FMT_SAMPLE_SIZE:
	case SKL_TKN_U32_FMT_CH_CONFIG:
	case SKL_TKN_U32_FMT_INTERLEAVE:
	case SKL_TKN_U32_FMT_SAMPLE_TYPE:
	case SKL_TKN_U32_FMT_CH_MAP:
		ret = skl_tplg_widget_fill_fmt(dev, iface, tkn_elem->token,
				tkn_elem->value, dir, pin_index);

		if (ret < 0)
			return ret;

		break;

	case SKL_TKN_U32_PIN_MOD_ID:
	case SKL_TKN_U32_PIN_INST_ID:
	case SKL_TKN_UUID:
		ret = skl_tplg_fill_pins_info(dev,
				mconfig, tkn_elem, dir,
				pin_index);
		if (ret < 0)
			return ret;

		break;

	case SKL_TKN_U32_CAPS_SIZE:
		mconfig->formats_config.caps_size =
			tkn_elem->value;

		break;

	case SKL_TKN_U32_CAPS_SET_PARAMS:
		mconfig->formats_config.set_params =
				tkn_elem->value;
		break;

	case SKL_TKN_U32_CAPS_PARAMS_ID:
		mconfig->formats_config.param_id =
				tkn_elem->value;
		break;

	case SKL_TKN_U32_PROC_DOMAIN:
		mconfig->domain =
			tkn_elem->value;

		break;

	case SKL_TKN_U32_DMA_BUF_SIZE:
		mconfig->dma_buffer_size = tkn_elem->value;
		break;

	case SKL_TKN_U8_IN_PIN_TYPE:
	case SKL_TKN_U8_OUT_PIN_TYPE:
	case SKL_TKN_U8_CONN_TYPE:
		break;

	default:
		dev_err(dev, "Token %d not handled\n",
				tkn_elem->token);
		return -EINVAL;
	}

	tkn_count++;

	return tkn_count;
}

/*
 * Parse the vendor array for specific tokens to construct
 * module private data
 */
static int skl_tplg_get_tokens(struct device *dev,
		char *pvt_data,	struct skl_dev *skl,
		struct skl_module_cfg *mconfig, int block_size)
{
	struct snd_soc_tplg_vendor_array *array;
	struct snd_soc_tplg_vendor_value_elem *tkn_elem;
	int tkn_count = 0, ret;
	int off = 0, tuple_size = 0;
	bool is_module_guid = true;

	if (block_size <= 0)
		return -EINVAL;

	while (tuple_size < block_size) {
		array = (struct snd_soc_tplg_vendor_array *)(pvt_data + off);

		off += array->size;

		switch (array->type) {
		case SND_SOC_TPLG_TUPLE_TYPE_STRING:
			dev_warn(dev, "no string tokens expected for skl tplg\n");
			continue;

		case SND_SOC_TPLG_TUPLE_TYPE_UUID:
			if (is_module_guid) {
				ret = skl_tplg_get_uuid(dev, (guid_t *)mconfig->guid,
							array->uuid);
				is_module_guid = false;
			} else {
				ret = skl_tplg_get_token(dev, array->value, skl,
							 mconfig);
			}

			if (ret < 0)
				return ret;

			tuple_size += sizeof(*array->uuid);

			continue;

		default:
			tkn_elem = array->value;
			tkn_count = 0;
			break;
		}

		while (tkn_count <= (array->num_elems - 1)) {
			ret = skl_tplg_get_token(dev, tkn_elem,
					skl, mconfig);

			if (ret < 0)
				return ret;

			tkn_count = tkn_count + ret;
			tkn_elem++;
		}

		tuple_size += tkn_count * sizeof(*tkn_elem);
	}

	return off;
}

/*
 * Every data block is preceded by a descriptor to read the number
 * of data blocks, they type of the block and it's size
 */
static int skl_tplg_get_desc_blocks(struct device *dev,
		struct snd_soc_tplg_vendor_array *array)
{
	struct snd_soc_tplg_vendor_value_elem *tkn_elem;

	tkn_elem = array->value;

	switch (tkn_elem->token) {
	case SKL_TKN_U8_NUM_BLOCKS:
	case SKL_TKN_U8_BLOCK_TYPE:
	case SKL_TKN_U16_BLOCK_SIZE:
		return tkn_elem->value;

	default:
		dev_err(dev, "Invalid descriptor token %d\n", tkn_elem->token);
		break;
	}

	return -EINVAL;
}

/* Functions to parse private data from configuration file format v4 */

/*
 * Add pipeline from topology binary into driver pipeline list
 *
 * If already added we return that instance
 * Otherwise we create a new instance and add into driver list
 */
static int skl_tplg_add_pipe_v4(struct device *dev,
			struct skl_module_cfg *mconfig, struct skl_dev *skl,
			struct skl_dfw_v4_pipe *dfw_pipe)
{
	struct skl_pipeline *ppl;
	struct skl_pipe *pipe;
	struct skl_pipe_params *params;

	list_for_each_entry(ppl, &skl->ppl_list, node) {
		if (ppl->pipe->ppl_id == dfw_pipe->pipe_id) {
			mconfig->pipe = ppl->pipe;
			return 0;
		}
	}

	ppl = devm_kzalloc(dev, sizeof(*ppl), GFP_KERNEL);
	if (!ppl)
		return -ENOMEM;

	pipe = devm_kzalloc(dev, sizeof(*pipe), GFP_KERNEL);
	if (!pipe)
		return -ENOMEM;

	params = devm_kzalloc(dev, sizeof(*params), GFP_KERNEL);
	if (!params)
		return -ENOMEM;

	pipe->ppl_id = dfw_pipe->pipe_id;
	pipe->memory_pages = dfw_pipe->memory_pages;
	pipe->pipe_priority = dfw_pipe->pipe_priority;
	pipe->conn_type = dfw_pipe->conn_type;
	pipe->state = SKL_PIPE_INVALID;
	pipe->p_params = params;
	INIT_LIST_HEAD(&pipe->w_list);

	ppl->pipe = pipe;
	list_add(&ppl->node, &skl->ppl_list);

	mconfig->pipe = pipe;

	return 0;
}

static void skl_fill_module_pin_info_v4(struct skl_dfw_v4_module_pin *dfw_pin,
					struct skl_module_pin *m_pin,
					bool is_dynamic, int max_pin)
{
	int i;

	for (i = 0; i < max_pin; i++) {
		m_pin[i].id.module_id = dfw_pin[i].module_id;
		m_pin[i].id.instance_id = dfw_pin[i].instance_id;
		m_pin[i].in_use = false;
		m_pin[i].is_dynamic = is_dynamic;
		m_pin[i].pin_state = SKL_PIN_UNBIND;
	}
}

static void skl_tplg_fill_fmt_v4(struct skl_module_pin_fmt *dst_fmt,
				 struct skl_dfw_v4_module_fmt *src_fmt,
				 int pins)
{
	int i;

	for (i = 0; i < pins; i++) {
		dst_fmt[i].fmt.channels  = src_fmt[i].channels;
		dst_fmt[i].fmt.s_freq = src_fmt[i].freq;
		dst_fmt[i].fmt.bit_depth = src_fmt[i].bit_depth;
		dst_fmt[i].fmt.valid_bit_depth = src_fmt[i].valid_bit_depth;
		dst_fmt[i].fmt.ch_cfg = src_fmt[i].ch_cfg;
		dst_fmt[i].fmt.ch_map = src_fmt[i].ch_map;
		dst_fmt[i].fmt.interleaving_style =
						src_fmt[i].interleaving_style;
		dst_fmt[i].fmt.sample_type = src_fmt[i].sample_type;
	}
}

static int skl_tplg_get_pvt_data_v4(struct snd_soc_tplg_dapm_widget *tplg_w,
				    struct skl_dev *skl, struct device *dev,
				    struct skl_module_cfg *mconfig)
{
	struct skl_dfw_v4_module *dfw =
				(struct skl_dfw_v4_module *)tplg_w->priv.data;
	int ret;

	dev_dbg(dev, "Parsing Skylake v4 widget topology data\n");

	ret = guid_parse(dfw->uuid, (guid_t *)mconfig->guid);
	if (ret)
		return ret;
	mconfig->id.module_id = -1;
	mconfig->id.instance_id = dfw->instance_id;
	mconfig->module->resources[0].cpc = dfw->max_mcps / 1000;
	mconfig->module->resources[0].ibs = dfw->ibs;
	mconfig->module->resources[0].obs = dfw->obs;
	mconfig->core_id = dfw->core_id;
	mconfig->module->max_input_pins = dfw->max_in_queue;
	mconfig->module->max_output_pins = dfw->max_out_queue;
	mconfig->module->loadable = dfw->is_loadable;
	skl_tplg_fill_fmt_v4(mconfig->module->formats[0].inputs, dfw->in_fmt,
			     MAX_IN_QUEUE);
	skl_tplg_fill_fmt_v4(mconfig->module->formats[0].outputs, dfw->out_fmt,
			     MAX_OUT_QUEUE);

	mconfig->params_fixup = dfw->params_fixup;
	mconfig->converter = dfw->converter;
	mconfig->m_type = dfw->module_type;
	mconfig->vbus_id = dfw->vbus_id;
	mconfig->module->resources[0].is_pages = dfw->mem_pages;

	ret = skl_tplg_add_pipe_v4(dev, mconfig, skl, &dfw->pipe);
	if (ret)
		return ret;

	mconfig->dev_type = dfw->dev_type;
	mconfig->hw_conn_type = dfw->hw_conn_type;
	mconfig->time_slot = dfw->time_slot;
	mconfig->formats_config.caps_size = dfw->caps.caps_size;

	mconfig->m_in_pin = devm_kcalloc(dev,
				MAX_IN_QUEUE, sizeof(*mconfig->m_in_pin),
				GFP_KERNEL);
	if (!mconfig->m_in_pin)
		return -ENOMEM;

	mconfig->m_out_pin = devm_kcalloc(dev,
				MAX_OUT_QUEUE, sizeof(*mconfig->m_out_pin),
				GFP_KERNEL);
	if (!mconfig->m_out_pin)
		return -ENOMEM;

	skl_fill_module_pin_info_v4(dfw->in_pin, mconfig->m_in_pin,
				    dfw->is_dynamic_in_pin,
				    mconfig->module->max_input_pins);
	skl_fill_module_pin_info_v4(dfw->out_pin, mconfig->m_out_pin,
				    dfw->is_dynamic_out_pin,
				    mconfig->module->max_output_pins);

	if (mconfig->formats_config.caps_size) {
		mconfig->formats_config.set_params = dfw->caps.set_params;
		mconfig->formats_config.param_id = dfw->caps.param_id;
		mconfig->formats_config.caps =
		devm_kzalloc(dev, mconfig->formats_config.caps_size,
			     GFP_KERNEL);
		if (!mconfig->formats_config.caps)
			return -ENOMEM;
		memcpy(mconfig->formats_config.caps, dfw->caps.caps,
		       dfw->caps.caps_size);
	}

	return 0;
}

/*
 * Parse the private data for the token and corresponding value.
 * The private data can have multiple data blocks. So, a data block
 * is preceded by a descriptor for number of blocks and a descriptor
 * for the type and size of the suceeding data block.
 */
static int skl_tplg_get_pvt_data(struct snd_soc_tplg_dapm_widget *tplg_w,
				struct skl_dev *skl, struct device *dev,
				struct skl_module_cfg *mconfig)
{
	struct snd_soc_tplg_vendor_array *array;
	int num_blocks, block_size, block_type, off = 0;
	char *data;
	int ret;

	/*
	 * v4 configuration files have a valid UUID at the start of
	 * the widget's private data.
	 */
	if (uuid_is_valid((char *)tplg_w->priv.data))
		return skl_tplg_get_pvt_data_v4(tplg_w, skl, dev, mconfig);

	/* Read the NUM_DATA_BLOCKS descriptor */
	array = (struct snd_soc_tplg_vendor_array *)tplg_w->priv.data;
	ret = skl_tplg_get_desc_blocks(dev, array);
	if (ret < 0)
		return ret;
	num_blocks = ret;

	off += array->size;
	/* Read the BLOCK_TYPE and BLOCK_SIZE descriptor */
	while (num_blocks > 0) {
		array = (struct snd_soc_tplg_vendor_array *)
				(tplg_w->priv.data + off);

		ret = skl_tplg_get_desc_blocks(dev, array);

		if (ret < 0)
			return ret;
		block_type = ret;
		off += array->size;

		array = (struct snd_soc_tplg_vendor_array *)
			(tplg_w->priv.data + off);

		ret = skl_tplg_get_desc_blocks(dev, array);

		if (ret < 0)
			return ret;
		block_size = ret;
		off += array->size;

		array = (struct snd_soc_tplg_vendor_array *)
			(tplg_w->priv.data + off);

		data = (tplg_w->priv.data + off);

		if (block_type == SKL_TYPE_TUPLE) {
			ret = skl_tplg_get_tokens(dev, data,
					skl, mconfig, block_size);

			if (ret < 0)
				return ret;

			--num_blocks;
		} else {
			if (mconfig->formats_config.caps_size > 0)
				memcpy(mconfig->formats_config.caps, data,
					mconfig->formats_config.caps_size);
			--num_blocks;
			ret = mconfig->formats_config.caps_size;
		}
		off += ret;
	}

	return 0;
}

static void skl_clear_pin_config(struct snd_soc_component *component,
				struct snd_soc_dapm_widget *w)
{
	int i;
	struct skl_module_cfg *mconfig;
	struct skl_pipe *pipe;

	if (!strncmp(w->dapm->component->name, component->name,
					strlen(component->name))) {
		mconfig = w->priv;
		pipe = mconfig->pipe;
		for (i = 0; i < mconfig->module->max_input_pins; i++) {
			mconfig->m_in_pin[i].in_use = false;
			mconfig->m_in_pin[i].pin_state = SKL_PIN_UNBIND;
		}
		for (i = 0; i < mconfig->module->max_output_pins; i++) {
			mconfig->m_out_pin[i].in_use = false;
			mconfig->m_out_pin[i].pin_state = SKL_PIN_UNBIND;
		}
		pipe->state = SKL_PIPE_INVALID;
		mconfig->m_state = SKL_MODULE_UNINIT;
	}
}

void skl_cleanup_resources(struct skl_dev *skl)
{
	struct snd_soc_component *soc_component = skl->component;
	struct snd_soc_dapm_widget *w;
	struct snd_soc_card *card;

	if (soc_component == NULL)
		return;

	card = soc_component->card;
	if (!card || !card->instantiated)
		return;

	list_for_each_entry(w, &card->widgets, list) {
		if (is_skl_dsp_widget_type(w, skl->dev) && w->priv != NULL)
			skl_clear_pin_config(soc_component, w);
	}

	skl_clear_module_cnt(skl->dsp);
}

/*
 * Topology core widget load callback
 *
 * This is used to save the private data for each widget which gives
 * information to the driver about module and pipeline parameters which DSP
 * FW expects like ids, resource values, formats etc
 */
static int skl_tplg_widget_load(struct snd_soc_component *cmpnt, int index,
				struct snd_soc_dapm_widget *w,
				struct snd_soc_tplg_dapm_widget *tplg_w)
{
	int ret;
	struct hdac_bus *bus = snd_soc_component_get_drvdata(cmpnt);
	struct skl_dev *skl = bus_to_skl(bus);
	struct skl_module_cfg *mconfig;

	if (!tplg_w->priv.size)
		goto bind_event;

	mconfig = devm_kzalloc(bus->dev, sizeof(*mconfig), GFP_KERNEL);

	if (!mconfig)
		return -ENOMEM;

	if (skl->nr_modules == 0) {
		mconfig->module = devm_kzalloc(bus->dev,
				sizeof(*mconfig->module), GFP_KERNEL);
		if (!mconfig->module)
			return -ENOMEM;
	}

	w->priv = mconfig;

	/*
	 * module binary can be loaded later, so set it to query when
	 * module is load for a use case
	 */
	mconfig->id.module_id = -1;

	/* Parse private data for tuples */
	ret = skl_tplg_get_pvt_data(tplg_w, skl, bus->dev, mconfig);
	if (ret < 0)
		return ret;

	skl_debug_init_module(skl->debugfs, w, mconfig);

bind_event:
	if (tplg_w->event_type == 0) {
		dev_dbg(bus->dev, "ASoC: No event handler required\n");
		return 0;
	}

	ret = snd_soc_tplg_widget_bind_event(w, skl_tplg_widget_ops,
					ARRAY_SIZE(skl_tplg_widget_ops),
					tplg_w->event_type);

	if (ret) {
		dev_err(bus->dev, "%s: No matching event handlers found for %d\n",
					__func__, tplg_w->event_type);
		return -EINVAL;
	}

	return 0;
}

static int skl_init_algo_data(struct device *dev, struct soc_bytes_ext *be,
					struct snd_soc_tplg_bytes_control *bc)
{
	struct skl_algo_data *ac;
	struct skl_dfw_algo_data *dfw_ac =
				(struct skl_dfw_algo_data *)bc->priv.data;

	ac = devm_kzalloc(dev, sizeof(*ac), GFP_KERNEL);
	if (!ac)
		return -ENOMEM;

	/* Fill private data */
	ac->max = dfw_ac->max;
	ac->param_id = dfw_ac->param_id;
	ac->set_params = dfw_ac->set_params;
	ac->size = dfw_ac->max;

	if (ac->max) {
		ac->params = devm_kzalloc(dev, ac->max, GFP_KERNEL);
		if (!ac->params)
			return -ENOMEM;

		memcpy(ac->params, dfw_ac->params, ac->max);
	}

	be->dobj.private  = ac;
	return 0;
}

static int skl_init_enum_data(struct device *dev, struct soc_enum *se,
				struct snd_soc_tplg_enum_control *ec)
{

	void *data;

	if (ec->priv.size) {
		data = devm_kzalloc(dev, sizeof(ec->priv.size), GFP_KERNEL);
		if (!data)
			return -ENOMEM;
		memcpy(data, ec->priv.data, ec->priv.size);
		se->dobj.private = data;
	}

	return 0;

}

static int skl_tplg_control_load(struct snd_soc_component *cmpnt,
				int index,
				struct snd_kcontrol_new *kctl,
				struct snd_soc_tplg_ctl_hdr *hdr)
{
	struct soc_bytes_ext *sb;
	struct snd_soc_tplg_bytes_control *tplg_bc;
	struct snd_soc_tplg_enum_control *tplg_ec;
	struct hdac_bus *bus  = snd_soc_component_get_drvdata(cmpnt);
	struct soc_enum *se;

	switch (hdr->ops.info) {
	case SND_SOC_TPLG_CTL_BYTES:
		tplg_bc = container_of(hdr,
				struct snd_soc_tplg_bytes_control, hdr);
		if (kctl->access & SNDRV_CTL_ELEM_ACCESS_TLV_CALLBACK) {
			sb = (struct soc_bytes_ext *)kctl->private_value;
			if (tplg_bc->priv.size)
				return skl_init_algo_data(
						bus->dev, sb, tplg_bc);
		}
		break;

	case SND_SOC_TPLG_CTL_ENUM:
		tplg_ec = container_of(hdr,
				struct snd_soc_tplg_enum_control, hdr);
		if (kctl->access & SNDRV_CTL_ELEM_ACCESS_READ) {
			se = (struct soc_enum *)kctl->private_value;
			if (tplg_ec->priv.size)
				skl_init_enum_data(bus->dev, se, tplg_ec);
		}

		/*
		 * now that the control initializations are done, remove
		 * write permission for the DMIC configuration enums to
		 * avoid conflicts between NHLT settings and user interaction
		 */

		if (hdr->ops.get == SKL_CONTROL_TYPE_MULTI_IO_SELECT_DMIC)
			kctl->access = SNDRV_CTL_ELEM_ACCESS_READ;

		break;

	default:
		dev_dbg(bus->dev, "Control load not supported %d:%d:%d\n",
			hdr->ops.get, hdr->ops.put, hdr->ops.info);
		break;
	}

	return 0;
}

static int skl_tplg_fill_str_mfest_tkn(struct device *dev,
		struct snd_soc_tplg_vendor_string_elem *str_elem,
		struct skl_dev *skl)
{
	int tkn_count = 0;
	static int ref_count;

	switch (str_elem->token) {
	case SKL_TKN_STR_LIB_NAME:
		if (ref_count > skl->lib_count - 1) {
			ref_count = 0;
			return -EINVAL;
		}

		strncpy(skl->lib_info[ref_count].name,
			str_elem->string,
			ARRAY_SIZE(skl->lib_info[ref_count].name));
		ref_count++;
		break;

	default:
		dev_err(dev, "Not a string token %d\n", str_elem->token);
		break;
	}
	tkn_count++;

	return tkn_count;
}

static int skl_tplg_get_str_tkn(struct device *dev,
		struct snd_soc_tplg_vendor_array *array,
		struct skl_dev *skl)
{
	int tkn_count = 0, ret;
	struct snd_soc_tplg_vendor_string_elem *str_elem;

	str_elem = (struct snd_soc_tplg_vendor_string_elem *)array->value;
	while (tkn_count < array->num_elems) {
		ret = skl_tplg_fill_str_mfest_tkn(dev, str_elem, skl);
		str_elem++;

		if (ret < 0)
			return ret;

		tkn_count = tkn_count + ret;
	}

	return tkn_count;
}

static int skl_tplg_manifest_fill_fmt(struct device *dev,
		struct skl_module_iface *fmt,
		struct snd_soc_tplg_vendor_value_elem *tkn_elem,
		u32 dir, int fmt_idx)
{
	struct skl_module_pin_fmt *dst_fmt;
	struct skl_module_fmt *mod_fmt;
	int ret;

	if (!fmt)
		return -EINVAL;

	switch (dir) {
	case SKL_DIR_IN:
		dst_fmt = &fmt->inputs[fmt_idx];
		break;

	case SKL_DIR_OUT:
		dst_fmt = &fmt->outputs[fmt_idx];
		break;

	default:
		dev_err(dev, "Invalid direction: %d\n", dir);
		return -EINVAL;
	}

	mod_fmt = &dst_fmt->fmt;

	switch (tkn_elem->token) {
	case SKL_TKN_MM_U32_INTF_PIN_ID:
		dst_fmt->id = tkn_elem->value;
		break;

	default:
		ret = skl_tplg_fill_fmt(dev, mod_fmt, tkn_elem->token,
					tkn_elem->value);
		if (ret < 0)
			return ret;
		break;
	}

	return 0;
}

static int skl_tplg_fill_mod_info(struct device *dev,
		struct snd_soc_tplg_vendor_value_elem *tkn_elem,
		struct skl_module *mod)
{

	if (!mod)
		return -EINVAL;

	switch (tkn_elem->token) {
	case SKL_TKN_U8_IN_PIN_TYPE:
		mod->input_pin_type = tkn_elem->value;
		break;

	case SKL_TKN_U8_OUT_PIN_TYPE:
		mod->output_pin_type = tkn_elem->value;
		break;

	case SKL_TKN_U8_IN_QUEUE_COUNT:
		mod->max_input_pins = tkn_elem->value;
		break;

	case SKL_TKN_U8_OUT_QUEUE_COUNT:
		mod->max_output_pins = tkn_elem->value;
		break;

	case SKL_TKN_MM_U8_NUM_RES:
		mod->nr_resources = tkn_elem->value;
		break;

	case SKL_TKN_MM_U8_NUM_INTF:
		mod->nr_interfaces = tkn_elem->value;
		break;

	default:
		dev_err(dev, "Invalid mod info token %d", tkn_elem->token);
		return -EINVAL;
	}

	return 0;
}


static int skl_tplg_get_int_tkn(struct device *dev,
		struct snd_soc_tplg_vendor_value_elem *tkn_elem,
		struct skl_dev *skl)
{
	int tkn_count = 0, ret;
	static int mod_idx, res_val_idx, intf_val_idx, dir, pin_idx;
	struct skl_module_res *res = NULL;
	struct skl_module_iface *fmt = NULL;
	struct skl_module *mod = NULL;
	static struct skl_astate_param *astate_table;
	static int astate_cfg_idx, count;
	int i;
	size_t size;

	if (skl->modules) {
		mod = skl->modules[mod_idx];
		res = &mod->resources[res_val_idx];
		fmt = &mod->formats[intf_val_idx];
	}

	switch (tkn_elem->token) {
	case SKL_TKN_U32_LIB_COUNT:
		skl->lib_count = tkn_elem->value;
		break;

	case SKL_TKN_U8_NUM_MOD:
		skl->nr_modules = tkn_elem->value;
		skl->modules = devm_kcalloc(dev, skl->nr_modules,
				sizeof(*skl->modules), GFP_KERNEL);
		if (!skl->modules)
			return -ENOMEM;

		for (i = 0; i < skl->nr_modules; i++) {
			skl->modules[i] = devm_kzalloc(dev,
					sizeof(struct skl_module), GFP_KERNEL);
			if (!skl->modules[i])
				return -ENOMEM;
		}
		break;

	case SKL_TKN_MM_U8_MOD_IDX:
		mod_idx = tkn_elem->value;
		break;

	case SKL_TKN_U32_ASTATE_COUNT:
		if (astate_table != NULL) {
			dev_err(dev, "More than one entry for A-State count");
			return -EINVAL;
		}

		if (tkn_elem->value > SKL_MAX_ASTATE_CFG) {
			dev_err(dev, "Invalid A-State count %d\n",
				tkn_elem->value);
			return -EINVAL;
		}

		size = struct_size(skl->cfg.astate_cfg, astate_table,
				   tkn_elem->value);
		skl->cfg.astate_cfg = devm_kzalloc(dev, size, GFP_KERNEL);
		if (!skl->cfg.astate_cfg)
			return -ENOMEM;

		astate_table = skl->cfg.astate_cfg->astate_table;
		count = skl->cfg.astate_cfg->count = tkn_elem->value;
		break;

	case SKL_TKN_U32_ASTATE_IDX:
		if (tkn_elem->value >= count) {
			dev_err(dev, "Invalid A-State index %d\n",
				tkn_elem->value);
			return -EINVAL;
		}

		astate_cfg_idx = tkn_elem->value;
		break;

	case SKL_TKN_U32_ASTATE_KCPS:
		astate_table[astate_cfg_idx].kcps = tkn_elem->value;
		break;

	case SKL_TKN_U32_ASTATE_CLK_SRC:
		astate_table[astate_cfg_idx].clk_src = tkn_elem->value;
		break;

	case SKL_TKN_U8_IN_PIN_TYPE:
	case SKL_TKN_U8_OUT_PIN_TYPE:
	case SKL_TKN_U8_IN_QUEUE_COUNT:
	case SKL_TKN_U8_OUT_QUEUE_COUNT:
	case SKL_TKN_MM_U8_NUM_RES:
	case SKL_TKN_MM_U8_NUM_INTF:
		ret = skl_tplg_fill_mod_info(dev, tkn_elem, mod);
		if (ret < 0)
			return ret;
		break;

	case SKL_TKN_U32_DIR_PIN_COUNT:
		dir = tkn_elem->value & SKL_IN_DIR_BIT_MASK;
		pin_idx = (tkn_elem->value & SKL_PIN_COUNT_MASK) >> 4;
		break;

	case SKL_TKN_MM_U32_RES_ID:
		if (!res)
			return -EINVAL;

		res->id = tkn_elem->value;
		res_val_idx = tkn_elem->value;
		break;

	case SKL_TKN_MM_U32_FMT_ID:
		if (!fmt)
			return -EINVAL;

		fmt->fmt_idx = tkn_elem->value;
		intf_val_idx = tkn_elem->value;
		break;

	case SKL_TKN_MM_U32_CPS:
	case SKL_TKN_MM_U32_DMA_SIZE:
	case SKL_TKN_MM_U32_CPC:
	case SKL_TKN_U32_MEM_PAGES:
	case SKL_TKN_U32_OBS:
	case SKL_TKN_U32_IBS:
	case SKL_TKN_MM_U32_RES_PIN_ID:
	case SKL_TKN_MM_U32_PIN_BUF:
		ret = skl_tplg_fill_res_tkn(dev, tkn_elem, res, pin_idx, dir);
		if (ret < 0)
			return ret;

		break;

	case SKL_TKN_MM_U32_NUM_IN_FMT:
		if (!fmt)
			return -EINVAL;

		res->nr_input_pins = tkn_elem->value;
		break;

	case SKL_TKN_MM_U32_NUM_OUT_FMT:
		if (!fmt)
			return -EINVAL;

		res->nr_output_pins = tkn_elem->value;
		break;

	case SKL_TKN_U32_FMT_CH:
	case SKL_TKN_U32_FMT_FREQ:
	case SKL_TKN_U32_FMT_BIT_DEPTH:
	case SKL_TKN_U32_FMT_SAMPLE_SIZE:
	case SKL_TKN_U32_FMT_CH_CONFIG:
	case SKL_TKN_U32_FMT_INTERLEAVE:
	case SKL_TKN_U32_FMT_SAMPLE_TYPE:
	case SKL_TKN_U32_FMT_CH_MAP:
	case SKL_TKN_MM_U32_INTF_PIN_ID:
		ret = skl_tplg_manifest_fill_fmt(dev, fmt, tkn_elem,
						 dir, pin_idx);
		if (ret < 0)
			return ret;
		break;

	default:
		dev_err(dev, "Not a manifest token %d\n", tkn_elem->token);
		return -EINVAL;
	}
	tkn_count++;

	return tkn_count;
}

/*
 * Fill the manifest structure by parsing the tokens based on the
 * type.
 */
static int skl_tplg_get_manifest_tkn(struct device *dev,
		char *pvt_data, struct skl_dev *skl,
		int block_size)
{
	int tkn_count = 0, ret;
	int off = 0, tuple_size = 0;
	u8 uuid_index = 0;
	struct snd_soc_tplg_vendor_array *array;
	struct snd_soc_tplg_vendor_value_elem *tkn_elem;

	if (block_size <= 0)
		return -EINVAL;

	while (tuple_size < block_size) {
		array = (struct snd_soc_tplg_vendor_array *)(pvt_data + off);
		off += array->size;
		switch (array->type) {
		case SND_SOC_TPLG_TUPLE_TYPE_STRING:
			ret = skl_tplg_get_str_tkn(dev, array, skl);

			if (ret < 0)
				return ret;
			tkn_count = ret;

			tuple_size += tkn_count *
				sizeof(struct snd_soc_tplg_vendor_string_elem);
			continue;

		case SND_SOC_TPLG_TUPLE_TYPE_UUID:
			if (array->uuid->token != SKL_TKN_UUID) {
				dev_err(dev, "Not an UUID token: %d\n",
					array->uuid->token);
				return -EINVAL;
			}
			if (uuid_index >= skl->nr_modules) {
				dev_err(dev, "Too many UUID tokens\n");
				return -EINVAL;
			}
			import_guid(&skl->modules[uuid_index++]->uuid,
				    array->uuid->uuid);

			tuple_size += sizeof(*array->uuid);
			continue;

		default:
			tkn_elem = array->value;
			tkn_count = 0;
			break;
		}

		while (tkn_count <= array->num_elems - 1) {
			ret = skl_tplg_get_int_tkn(dev,
					tkn_elem, skl);
			if (ret < 0)
				return ret;

			tkn_count = tkn_count + ret;
			tkn_elem++;
		}
		tuple_size += (tkn_count * sizeof(*tkn_elem));
		tkn_count = 0;
	}

	return off;
}

/*
 * Parse manifest private data for tokens. The private data block is
 * preceded by descriptors for type and size of data block.
 */
static int skl_tplg_get_manifest_data(struct snd_soc_tplg_manifest *manifest,
			struct device *dev, struct skl_dev *skl)
{
	struct snd_soc_tplg_vendor_array *array;
	int num_blocks, block_size = 0, block_type, off = 0;
	char *data;
	int ret;

	/* Read the NUM_DATA_BLOCKS descriptor */
	array = (struct snd_soc_tplg_vendor_array *)manifest->priv.data;
	ret = skl_tplg_get_desc_blocks(dev, array);
	if (ret < 0)
		return ret;
	num_blocks = ret;

	off += array->size;
	/* Read the BLOCK_TYPE and BLOCK_SIZE descriptor */
	while (num_blocks > 0) {
		array = (struct snd_soc_tplg_vendor_array *)
				(manifest->priv.data + off);
		ret = skl_tplg_get_desc_blocks(dev, array);

		if (ret < 0)
			return ret;
		block_type = ret;
		off += array->size;

		array = (struct snd_soc_tplg_vendor_array *)
			(manifest->priv.data + off);

		ret = skl_tplg_get_desc_blocks(dev, array);

		if (ret < 0)
			return ret;
		block_size = ret;
		off += array->size;

		array = (struct snd_soc_tplg_vendor_array *)
			(manifest->priv.data + off);

		data = (manifest->priv.data + off);

		if (block_type == SKL_TYPE_TUPLE) {
			ret = skl_tplg_get_manifest_tkn(dev, data, skl,
					block_size);

			if (ret < 0)
				return ret;

			--num_blocks;
		} else {
			return -EINVAL;
		}
		off += ret;
	}

	return 0;
}

static int skl_manifest_load(struct snd_soc_component *cmpnt, int index,
				struct snd_soc_tplg_manifest *manifest)
{
	struct hdac_bus *bus = snd_soc_component_get_drvdata(cmpnt);
	struct skl_dev *skl = bus_to_skl(bus);

	/* proceed only if we have private data defined */
	if (manifest->priv.size == 0)
		return 0;

	skl_tplg_get_manifest_data(manifest, bus->dev, skl);

	if (skl->lib_count > SKL_MAX_LIB) {
		dev_err(bus->dev, "Exceeding max Library count. Got:%d\n",
					skl->lib_count);
		return  -EINVAL;
	}

	return 0;
}

static void skl_tplg_complete(struct snd_soc_component *component)
{
	struct snd_soc_dobj *dobj;
	struct snd_soc_acpi_mach *mach =
		dev_get_platdata(component->card->dev);
	int i;

	list_for_each_entry(dobj, &component->dobj_list, list) {
		struct snd_kcontrol *kcontrol = dobj->control.kcontrol;
		struct soc_enum *se =
			(struct soc_enum *)kcontrol->private_value;
		char **texts = dobj->control.dtexts;
		char chan_text[4];

		if (dobj->type != SND_SOC_DOBJ_ENUM ||
		    dobj->control.kcontrol->put !=
		    skl_tplg_multi_config_set_dmic)
			continue;
		sprintf(chan_text, "c%d", mach->mach_params.dmic_num);

		for (i = 0; i < se->items; i++) {
			struct snd_ctl_elem_value val;

			if (strstr(texts[i], chan_text)) {
				val.value.enumerated.item[0] = i;
				kcontrol->put(kcontrol, &val);
			}
		}
	}
}

static struct snd_soc_tplg_ops skl_tplg_ops  = {
	.widget_load = skl_tplg_widget_load,
	.control_load = skl_tplg_control_load,
	.bytes_ext_ops = skl_tlv_ops,
	.bytes_ext_ops_count = ARRAY_SIZE(skl_tlv_ops),
	.io_ops = skl_tplg_kcontrol_ops,
	.io_ops_count = ARRAY_SIZE(skl_tplg_kcontrol_ops),
	.manifest = skl_manifest_load,
	.dai_load = skl_dai_load,
	.complete = skl_tplg_complete,
};

/*
 * A pipe can have multiple modules, each of them will be a DAPM widget as
 * well. While managing a pipeline we need to get the list of all the
 * widgets in a pipelines, so this helper - skl_tplg_create_pipe_widget_list()
 * helps to get the SKL type widgets in that pipeline
 */
static int skl_tplg_create_pipe_widget_list(struct snd_soc_component *component)
{
	struct snd_soc_dapm_widget *w;
	struct skl_module_cfg *mcfg = NULL;
	struct skl_pipe_module *p_module = NULL;
	struct skl_pipe *pipe;

	list_for_each_entry(w, &component->card->widgets, list) {
		if (is_skl_dsp_widget_type(w, component->dev) && w->priv) {
			mcfg = w->priv;
			pipe = mcfg->pipe;

			p_module = devm_kzalloc(component->dev,
						sizeof(*p_module), GFP_KERNEL);
			if (!p_module)
				return -ENOMEM;

			p_module->w = w;
			list_add_tail(&p_module->node, &pipe->w_list);
		}
	}

	return 0;
}

static void skl_tplg_set_pipe_type(struct skl_dev *skl, struct skl_pipe *pipe)
{
	struct skl_pipe_module *w_module;
	struct snd_soc_dapm_widget *w;
	struct skl_module_cfg *mconfig;
	bool host_found = false, link_found = false;

	list_for_each_entry(w_module, &pipe->w_list, node) {
		w = w_module->w;
		mconfig = w->priv;

		if (mconfig->dev_type == SKL_DEVICE_HDAHOST)
			host_found = true;
		else if (mconfig->dev_type != SKL_DEVICE_NONE)
			link_found = true;
	}

	if (host_found && link_found)
		pipe->passthru = true;
	else
		pipe->passthru = false;
}

/*
 * SKL topology init routine
 */
int skl_tplg_init(struct snd_soc_component *component, struct hdac_bus *bus)
{
	int ret;
	const struct firmware *fw;
	struct skl_dev *skl = bus_to_skl(bus);
	struct skl_pipeline *ppl;

	ret = request_firmware(&fw, skl->tplg_name, bus->dev);
	if (ret < 0) {
		char alt_tplg_name[64];

		snprintf(alt_tplg_name, sizeof(alt_tplg_name), "%s-tplg.bin",
			 skl->mach->drv_name);
		dev_info(bus->dev, "tplg fw %s load failed with %d, trying alternative tplg name %s",
			 skl->tplg_name, ret, alt_tplg_name);

		ret = request_firmware(&fw, alt_tplg_name, bus->dev);
		if (!ret)
			goto component_load;

		dev_info(bus->dev, "tplg %s failed with %d, falling back to dfw_sst.bin",
			 alt_tplg_name, ret);

		ret = request_firmware(&fw, "dfw_sst.bin", bus->dev);
		if (ret < 0) {
			dev_err(bus->dev, "Fallback tplg fw %s load failed with %d\n",
					"dfw_sst.bin", ret);
			return ret;
		}
	}

component_load:
	ret = snd_soc_tplg_component_load(component, &skl_tplg_ops, fw);
	if (ret < 0) {
		dev_err(bus->dev, "tplg component load failed%d\n", ret);
		goto err;
	}

	ret = skl_tplg_create_pipe_widget_list(component);
	if (ret < 0) {
		dev_err(bus->dev, "tplg create pipe widget list failed%d\n",
				ret);
		goto err;
	}

	list_for_each_entry(ppl, &skl->ppl_list, node)
		skl_tplg_set_pipe_type(skl, ppl->pipe);

err:
	release_firmware(fw);
	return ret;
}

void skl_tplg_exit(struct snd_soc_component *component, struct hdac_bus *bus)
{
	struct skl_dev *skl = bus_to_skl(bus);
	struct skl_pipeline *ppl, *tmp;

	list_for_each_entry_safe(ppl, tmp, &skl->ppl_list, node)
		list_del(&ppl->node);

	/* clean up topology */
	snd_soc_tplg_component_remove(component);
}
