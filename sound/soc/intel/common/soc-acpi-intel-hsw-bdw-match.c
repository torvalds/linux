// SPDX-License-Identifier: GPL-2.0-only
/*
 * soc-acpi-intel-hsw-bdw-match.c - tables and support for ACPI enumeration.
 *
 * Copyright (c) 2017, Intel Corporation.
 */

#include <linux/dmi.h>
#include <sound/soc-acpi.h>
#include <sound/soc-acpi-intel-match.h>

struct snd_soc_acpi_mach snd_soc_acpi_intel_haswell_machines[] = {
	{
		.id = "INT33CA",
		.drv_name = "haswell-audio",
		.fw_filename = "intel/IntcSST1.bin",
		.sof_fw_filename = "sof-hsw.ri",
		.sof_tplg_filename = "sof-hsw.tplg",
	},
	{}
};
EXPORT_SYMBOL_GPL(snd_soc_acpi_intel_haswell_machines);

struct snd_soc_acpi_mach snd_soc_acpi_intel_broadwell_machines[] = {
	{
		.id = "INT343A",
		.drv_name = "broadwell-audio",
		.fw_filename =  "intel/IntcSST2.bin",
		.sof_fw_filename = "sof-bdw.ri",
		.sof_tplg_filename = "sof-bdw-rt286.tplg",
	},
	{
		.id = "RT5677CE",
		.drv_name = "bdw-rt5677",
		.fw_filename =  "intel/IntcSST2.bin",
		.sof_fw_filename = "sof-bdw.ri",
		.sof_tplg_filename = "sof-bdw-rt5677.tplg",
	},
	{
		.id = "INT33CA",
		.drv_name = "haswell-audio",
		.fw_filename = "intel/IntcSST2.bin",
		.sof_fw_filename = "sof-bdw.ri",
		.sof_tplg_filename = "sof-bdw-rt5640.tplg",
	},
	{}
};
EXPORT_SYMBOL_GPL(snd_soc_acpi_intel_broadwell_machines);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Intel Common ACPI Match module");
