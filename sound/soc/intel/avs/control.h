/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright(c) 2021-2022 Intel Corporation
 *
 * Authors: Amadeusz Slawinski <amadeuszx.slawinski@linux.intel.com>
 *          Cezary Rojewski <cezary.rojewski@intel.com>
 */

#ifndef __SOUND_SOC_INTEL_AVS_CTRL_H
#define __SOUND_SOC_INTEL_AVS_CTRL_H

#include <sound/control.h>
#include <uapi/sound/asoc.h>

struct avs_control_data {
	u32 id;
	long values[SND_SOC_TPLG_MAX_CHAN];
};

int avs_control_volume_get(struct snd_kcontrol *kctl, struct snd_ctl_elem_value *uctl);
int avs_control_volume_put(struct snd_kcontrol *kctl, struct snd_ctl_elem_value *uctl);
int avs_control_volume_info(struct snd_kcontrol *kctl, struct snd_ctl_elem_info *uinfo);
int avs_control_mute_get(struct snd_kcontrol *kctl, struct snd_ctl_elem_value *uctl);
int avs_control_mute_put(struct snd_kcontrol *kctl, struct snd_ctl_elem_value *uctl);
int avs_control_mute_info(struct snd_kcontrol *kctl, struct snd_ctl_elem_info *uinfo);

#endif
