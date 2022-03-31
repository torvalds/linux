/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright(c) 2021 Intel Corporation. All rights reserved.
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
	u16 instance_id;

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

#endif
