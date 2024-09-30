// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2018, Intel Corporation.

/*
 * soc-acpi-intel-hda-match.c - tables and support for HDA+ACPI enumeration.
 *
 */

#include <sound/soc-acpi.h>
#include <sound/soc-acpi-intel-match.h>

struct snd_soc_acpi_mach snd_soc_acpi_intel_hda_machines[] = {
	{
		/* .id is not used in this file */
		.drv_name = "skl_hda_dsp_generic",
		.sof_tplg_filename = "sof-hda-generic", /* the tplg suffix is added at run time */
		.tplg_quirk_mask = SND_SOC_ACPI_TPLG_INTEL_DMIC_NUMBER,
	},
	{},
};
EXPORT_SYMBOL_GPL(snd_soc_acpi_intel_hda_machines);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Intel Common ACPI Match module");
