/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright(c) 2021 Intel Corporation
 *
 * Authors: Cezary Rojewski <cezary.rojewski@intel.com>
 *          Amadeusz Slawinski <amadeuszx.slawinski@linux.intel.com>
 */

#ifndef __SOUND_SOC_INTEL_AVS_PATH_H
#define __SOUND_SOC_INTEL_AVS_PATH_H

#include <linux/list.h>
#include "avs.h"
#include "topology.h"

struct avs_path {
	u32 dma_id;
	struct list_head ppl_list;
	u32 state;

	struct avs_tplg_path *template;
	struct avs_dev *owner;
	/* device path management */
	struct list_head node;
};

struct avs_path_pipeline {
	u8 instance_id;
	struct list_head mod_list;
	struct list_head binding_list;

	struct avs_tplg_pipeline *template;
	struct avs_path *owner;
	/* path pipelines management */
	struct list_head node;
};

struct avs_path_module {
	u16 module_id;
	u8 instance_id;
	union avs_gtw_attributes gtw_attrs;

	struct avs_tplg_module *template;
	struct avs_path_pipeline *owner;
	/* pipeline modules management */
	struct list_head node;
};

struct avs_path_binding {
	struct avs_path_module *source;
	u8 source_pin;
	struct avs_path_module *sink;
	u8 sink_pin;

	struct avs_tplg_binding *template;
	struct avs_path_pipeline *owner;
	/* pipeline bindings management */
	struct list_head node;
};

void avs_path_free(struct avs_path *path);
struct avs_path *avs_path_create(struct avs_dev *adev, u32 dma_id,
				 struct avs_tplg_path_template *template,
				 struct snd_pcm_hw_params *fe_params,
				 struct snd_pcm_hw_params *be_params);
int avs_path_bind(struct avs_path *path);
int avs_path_unbind(struct avs_path *path);
int avs_path_reset(struct avs_path *path);
int avs_path_pause(struct avs_path *path);
int avs_path_run(struct avs_path *path, int trigger);

int avs_peakvol_set_volume(struct avs_dev *adev, struct avs_path_module *mod,
			   struct soc_mixer_control *mc, long *input);
int avs_peakvol_set_mute(struct avs_dev *adev, struct avs_path_module *mod,
			 struct soc_mixer_control *mc, long *input);

#endif
