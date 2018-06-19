/*
 * soc-apci-intel-hsw-bdw-match.c - tables and support for ACPI enumeration.
 *
 * Copyright (c) 2017, Intel Corporation.
 *
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include <linux/dmi.h>
#include <sound/soc-acpi.h>
#include <sound/soc-acpi-intel-match.h>

struct snd_soc_acpi_mach snd_soc_acpi_intel_haswell_machines[] = {
	{
		.id = "INT33CA",
		.drv_name = "haswell-audio",
		.fw_filename = "intel/IntcSST1.bin",
		.sof_fw_filename = "intel/sof-hsw.ri",
		.sof_tplg_filename = "intel/sof-hsw.tplg",
		.asoc_plat_name = "haswell-pcm-audio",
	},
	{}
};
EXPORT_SYMBOL_GPL(snd_soc_acpi_intel_haswell_machines);

struct snd_soc_acpi_mach snd_soc_acpi_intel_broadwell_machines[] = {
	{
		.id = "INT343A",
		.drv_name = "broadwell-audio",
		.fw_filename =  "intel/IntcSST2.bin",
		.sof_fw_filename = "intel/sof-bdw.ri",
		.sof_tplg_filename = "intel/sof-bdw-rt286.tplg",
		.asoc_plat_name = "haswell-pcm-audio",
	},
	{
		.id = "RT5677CE",
		.drv_name = "bdw-rt5677",
		.fw_filename =  "intel/IntcSST2.bin",
		.sof_fw_filename = "intel/sof-bdw.ri",
		.sof_tplg_filename = "intel/sof-bdw-rt5677.tplg",
		.asoc_plat_name = "haswell-pcm-audio",
	},
	{
		.id = "INT33CA",
		.drv_name = "haswell-audio",
		.fw_filename = "intel/IntcSST2.bin",
		.sof_fw_filename = "intel/sof-bdw.ri",
		.sof_tplg_filename = "intel/sof-bdw-rt5640.tplg",
		.asoc_plat_name = "haswell-pcm-audio",
	},
	{}
};
EXPORT_SYMBOL_GPL(snd_soc_acpi_intel_broadwell_machines);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Intel Common ACPI Match module");
