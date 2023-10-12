/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright(c) 2023 Intel Corporation. All rights reserved.
 *
 * Authors: Cezary Rojewski <cezary.rojewski@intel.com>
 *          Amadeusz Slawinski <amadeuszx.slawinski@linux.intel.com>
 */

#ifndef __SOUND_SOC_INTEL_AVS_UTILS_H
#define __SOUND_SOC_INTEL_AVS_UTILS_H

#include <sound/soc-acpi.h>

static inline bool avs_mach_singular_ssp(struct snd_soc_acpi_mach *mach)
{
	return hweight_long(mach->mach_params.i2s_link_mask) == 1;
}

static inline u32 avs_mach_ssp_port(struct snd_soc_acpi_mach *mach)
{
	return __ffs(mach->mach_params.i2s_link_mask);
}

static inline bool avs_mach_singular_tdm(struct snd_soc_acpi_mach *mach, u32 port)
{
	unsigned long *tdms = mach->pdata;

	return !tdms || (hweight_long(tdms[port]) == 1);
}

static inline u32 avs_mach_ssp_tdm(struct snd_soc_acpi_mach *mach, u32 port)
{
	unsigned long *tdms = mach->pdata;

	return tdms ? __ffs(tdms[port]) : 0;
}

static inline int avs_mach_get_ssp_tdm(struct device *dev, struct snd_soc_acpi_mach *mach,
				       int *ssp_port, int *tdm_slot)
{
	int port;

	if (!avs_mach_singular_ssp(mach)) {
		dev_err(dev, "Invalid SSP configuration\n");
		return -EINVAL;
	}
	port = avs_mach_ssp_port(mach);

	if (!avs_mach_singular_tdm(mach, port)) {
		dev_err(dev, "Invalid TDM configuration\n");
		return -EINVAL;
	}
	*ssp_port = port;
	*tdm_slot = avs_mach_ssp_tdm(mach, *ssp_port);

	return 0;
}

/*
 * Macro to easily generate format strings
 */
#define AVS_STRING_FMT(prefix, suffix, ssp, tdm) \
	(tdm) ? prefix "%d:%d" suffix : prefix "%d" suffix, (ssp), (tdm)

#endif
