// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright(c) 2021-2025 Intel Corporation
 *
 * Authors: Cezary Rojewski <cezary.rojewski@intel.com>
 *          Amadeusz Slawinski <amadeuszx.slawinski@linux.intel.com>
 */

#include <sound/hdaudio_ext.h>
#include "avs.h"
#include "debug.h"
#include "registers.h"

int avs_lnl_core_stall(struct avs_dev *adev, u32 core_mask, bool stall)
{
	struct hdac_bus *bus = &adev->base.core;
	struct hdac_ext_link *hlink;
	int ret;

	ret = avs_mtl_core_stall(adev, core_mask, stall);

	/* On unstall, route interrupts from the links to the DSP firmware. */
	if (!ret && !stall)
		list_for_each_entry(hlink, &bus->hlink_list, list)
			snd_hdac_updatel(hlink->ml_addr, AZX_REG_ML_LCTL, AZX_ML_LCTL_OFLEN,
					 AZX_ML_LCTL_OFLEN);
	return ret;
}
