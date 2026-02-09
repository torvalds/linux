// SPDX-License-Identifier: GPL-2.0-only
/*
 * soc-acpi-amd-sdca-quirks.c - tables and support for SDCA quirks
 *
 * Copyright(c) 2025 Advanced Micro Devices, Inc. All rights reserved.
 *
 */

#include <linux/soundwire/sdw_amd.h>
#include <sound/sdca.h>
#include <sound/soc-acpi.h>
#include "soc-acpi-amd-sdca-quirks.h"

/*
 * Pretend machine quirk. The argument type is not the traditional
 * 'struct snd_soc_acpi_mach' pointer but instead the sdw_amd_ctx
 * which contains the peripheral information required for the
 * SoundWire/SDCA filter on the SMART_MIC setup and interface
 * revision. When the return value is false, the entry in the
 * 'snd_soc_acpi_mach' table needs to be skipped.
 */
bool snd_soc_acpi_amd_sdca_is_device_rt712_vb(void *arg)
{
	struct sdw_amd_ctx *ctx = arg;
	int i;

	if (!ctx)
		return false;

	for (i = 0; i < ctx->peripherals->num_peripherals; i++) {
		if (sdca_device_quirk_match(ctx->peripherals->array[i],
					    SDCA_QUIRKS_RT712_VB))
			return true;
	}

	return false;
}
EXPORT_SYMBOL_NS(snd_soc_acpi_amd_sdca_is_device_rt712_vb, "SND_SOC_ACPI_AMD_SDCA_QUIRKS");

MODULE_DESCRIPTION("ASoC ACPI AMD SDCA quirks");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("SND_SOC_SDCA");
