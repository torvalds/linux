/*
 *  sst-atom-controls.c - Intel MID Platform driver DPCM ALSA controls for Mrfld
 *
 *  Copyright (C) 2013-14 Intel Corp
 *  Author: Omair Mohammed Abdullah <omair.m.abdullah@intel.com>
 *	Vinod Koul <vinod.koul@intel.com>
 *  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/slab.h>
#include <sound/soc.h>
#include <sound/tlv.h>
#include "sst-mfld-platform.h"
#include "sst-atom-controls.h"

int sst_dsp_init_v2_dpcm(struct snd_soc_platform *platform)
{
	int ret = 0;
	struct sst_data *drv = snd_soc_platform_get_drvdata(platform);

	drv->byte_stream = devm_kzalloc(platform->dev,
					SST_MAX_BIN_BYTES, GFP_KERNEL);
	if (!drv->byte_stream)
		return -ENOMEM;

	return ret;
}
