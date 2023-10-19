// SPDX-License-Identifier: GPL-2.0-only
/*
 * soc-acpi-intel-hsw-bdw-match.c - tables and support for ACPI enumeration.
 *
 * Copyright (c) 2017, Intel Corporation.
 */

#include <linux/dmi.h>
#include <sound/soc-acpi.h>
#include <sound/soc-acpi-intel-match.h>

struct snd_soc_acpi_mach snd_soc_acpi_intel_broadwell_machines[] = {
	{
		.id = "INT343A",
		.drv_name = "bdw_rt286",
		.sof_tplg_filename = "sof-bdw-rt286.tplg",
	},
	{
		.id = "10EC5650",
		.drv_name = "bdw-rt5650",
		.sof_tplg_filename = "sof-bdw-rt5650.tplg",
	},
	{
		.id = "RT5677CE",
		.drv_name = "bdw-rt5677",
		.sof_tplg_filename = "sof-bdw-rt5677.tplg",
	},
	{
		.id = "INT33CA",
		.drv_name = "hsw_rt5640",
		.sof_tplg_filename = "sof-bdw-rt5640.tplg",
	},
	{}
};
EXPORT_SYMBOL_GPL(snd_soc_acpi_intel_broadwell_machines);
