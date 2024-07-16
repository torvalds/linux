/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright(c) 2021 Intel Corporation. All rights reserved.
 *
 * Authors: Cezary Rojewski <cezary.rojewski@intel.com>
 *          Amadeusz Slawinski <amadeuszx.slawinski@linux.intel.com>
 */

#ifndef __SOUND_SOC_INTEL_AVS_TPLG_H
#define __SOUND_SOC_INTEL_AVS_TPLG_H

#include <linux/list.h>
#include "messages.h"

#define INVALID_OBJECT_ID	UINT_MAX

struct snd_soc_component;

struct avs_tplg {
	char name[SNDRV_CTL_ELEM_ID_NAME_MAXLEN];
	u32 version;
	struct snd_soc_component *comp;

	struct avs_tplg_library *libs;
	u32 num_libs;
	struct avs_audio_format *fmts;
	u32 num_fmts;
	struct avs_tplg_modcfg_base *modcfgs_base;
	u32 num_modcfgs_base;
	struct avs_tplg_modcfg_ext *modcfgs_ext;
	u32 num_modcfgs_ext;
	struct avs_tplg_pplcfg *pplcfgs;
	u32 num_pplcfgs;
	struct avs_tplg_binding *bindings;
	u32 num_bindings;

	struct list_head path_tmpl_list;
};

struct avs_tplg_library {
	char name[SNDRV_CTL_ELEM_ID_NAME_MAXLEN];
};

/* Matches header of struct avs_mod_cfg_base. */
struct avs_tplg_modcfg_base {
	u32 cpc;
	u32 ibs;
	u32 obs;
	u32 is_pages;
};

struct avs_tplg_pin_format {
	u32 pin_index;
	u32 iobs;
	struct avs_audio_format *fmt;
};

struct avs_tplg_modcfg_ext {
	guid_t type;

	union {
		struct {
			u16 num_input_pins;
			u16 num_output_pins;
			struct avs_tplg_pin_format *pin_fmts;
		} generic;
		struct {
			struct avs_audio_format *out_fmt;
			struct avs_audio_format *blob_fmt; /* optional override */
			u32 feature_mask;
			union avs_virtual_index vindex;
			u32 dma_type;
			u32 dma_buffer_size;
			u32 config_length;
			/* config_data part of priv data */
		} copier;
		struct {
			u32 out_channel_config;
			u32 coefficients_select;
			s32 coefficients[AVS_CHANNELS_MAX];
			u32 channel_map;
		} updown_mix;
		struct {
			u32 out_freq;
		} src;
		struct {
			u32 out_freq;
			u8 mode;
			u8 disable_jitter_buffer;
		} asrc;
		struct {
			u32 cpc_lp_mode;
		} wov;
		struct {
			struct avs_audio_format *ref_fmt;
			struct avs_audio_format *out_fmt;
			u32 cpc_lp_mode;
		} aec;
		struct {
			struct avs_audio_format *ref_fmt;
			struct avs_audio_format *out_fmt;
		} mux;
		struct {
			struct avs_audio_format *out_fmt;
		} micsel;
	};
};

/* Specifies path behaviour during PCM ->trigger(START) command. */
enum avs_tplg_trigger {
	AVS_TPLG_TRIGGER_AUTO = 0,
};

struct avs_tplg_pplcfg {
	u16 req_size;
	u8 priority;
	bool lp;
	u16 attributes;
	enum avs_tplg_trigger trigger;
};

struct avs_tplg_binding {
	char target_tplg_name[SNDRV_CTL_ELEM_ID_NAME_MAXLEN];
	u32 target_path_tmpl_id;
	u32 target_ppl_id;
	u32 target_mod_id;
	u8 target_mod_pin;
	u32 mod_id;
	u8 mod_pin;
	u8 is_sink;
};

struct avs_tplg_path_template_id {
	u32 id;
	char tplg_name[SNDRV_CTL_ELEM_ID_NAME_MAXLEN];
};

struct avs_tplg_path_template {
	u32 id;

	struct list_head path_list;

	struct avs_tplg *owner;
	/* Driver path templates management. */
	struct list_head node;
};

struct avs_tplg_path {
	u32 id;

	/* Path format requirements. */
	struct avs_audio_format *fe_fmt;
	struct avs_audio_format *be_fmt;

	struct list_head ppl_list;

	struct avs_tplg_path_template *owner;
	/* Path template path-variants management. */
	struct list_head node;
};

struct avs_tplg_pipeline {
	u32 id;

	struct avs_tplg_pplcfg *cfg;
	struct avs_tplg_binding **bindings;
	u32 num_bindings;
	struct list_head mod_list;

	struct avs_tplg_path *owner;
	/* Path pipelines management. */
	struct list_head node;
};

struct avs_tplg_module {
	u32 id;

	struct avs_tplg_modcfg_base *cfg_base;
	struct avs_audio_format *in_fmt;
	u8 core_id;
	u8 domain;
	struct avs_tplg_modcfg_ext *cfg_ext;

	struct avs_tplg_pipeline *owner;
	/* Pipeline modules management. */
	struct list_head node;
};

struct avs_tplg *avs_tplg_new(struct snd_soc_component *comp);

int avs_load_topology(struct snd_soc_component *comp, const char *filename);
int avs_remove_topology(struct snd_soc_component *comp);

#endif
