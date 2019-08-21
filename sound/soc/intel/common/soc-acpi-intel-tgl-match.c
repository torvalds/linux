// SPDX-License-Identifier: GPL-2.0
/*
 * soc-apci-intel-tgl-match.c - tables and support for ICL ACPI enumeration.
 *
 * Copyright (c) 2019, Intel Corporation.
 *
 */

#include <sound/soc-acpi.h>
#include <sound/soc-acpi-intel-match.h>

struct snd_soc_acpi_mach snd_soc_acpi_intel_tgl_machines[] = {
	{
		.id = "10EC1308",
		.drv_name = "tgl_rt1308",
		.sof_fw_filename = "sof-tgl.ri",
		.sof_tplg_filename = "sof-tgl-rt1308.tplg",
	},
	{},
};
EXPORT_SYMBOL_GPL(snd_soc_acpi_intel_tgl_machines);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Intel Common ACPI Match module");
