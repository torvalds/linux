/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright(c) 2024 Intel Corporation
 *
 * Authors: Cezary Rojewski <cezary.rojewski@intel.com>
 *          Amadeusz Slawinski <amadeuszx.slawinski@linux.intel.com>
 */

#ifndef __SOUND_SOC_INTEL_AVS_PCM_H
#define __SOUND_SOC_INTEL_AVS_PCM_H

#include <sound/pcm.h>

void avs_period_elapsed(struct snd_pcm_substream *substream);

#endif
