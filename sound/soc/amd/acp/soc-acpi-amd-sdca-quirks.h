/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * soc-acpi-amd-sdca-quirks.h - tables and support for SDCA quirks
 *
 * Copyright(c) 2025 Advanced Micro Devices, Inc. All rights reserved.
 *
 */

#ifndef _SND_SOC_ACPI_AMD_SDCA_QUIRKS
#define _SND_SOC_ACPI_AMD_SDCA_QUIRKS

#if IS_ENABLED(CONFIG_SND_SOC_ACPI_AMD_SDCA_QUIRKS)

bool snd_soc_acpi_amd_sdca_is_device_rt712_vb(void *arg);

#else

static inline bool snd_soc_acpi_amd_sdca_is_device_rt712_vb(void *arg)
{
	return false;
}

#endif

#endif
