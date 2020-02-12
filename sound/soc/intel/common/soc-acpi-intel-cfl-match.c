// SPDX-License-Identifier: GPL-2.0
/*
 * soc-apci-intel-cfl-match.c - tables and support for CFL ACPI enumeration.
 *
 * Copyright (c) 2019, Intel Corporation.
 *
 */

#include <sound/soc-acpi.h>
#include <sound/soc-acpi-intel-match.h>

struct snd_soc_acpi_mach snd_soc_acpi_intel_cfl_machines[] = {
	{},
};
EXPORT_SYMBOL_GPL(snd_soc_acpi_intel_cfl_machines);

struct snd_soc_acpi_mach snd_soc_acpi_intel_cfl_sdw_machines[] = {
	{}
};
EXPORT_SYMBOL_GPL(snd_soc_acpi_intel_cfl_sdw_machines);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Intel Common ACPI Match module");
