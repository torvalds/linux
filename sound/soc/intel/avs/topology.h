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

#endif
