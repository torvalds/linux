
/*
 * Copyright (C) 2017, Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __LINUX_SND_SOC_ACPI_INTEL_MATCH_H
#define __LINUX_SND_SOC_ACPI_INTEL_MATCH_H

#include <linux/module.h>
#include <linux/stddef.h>
#include <linux/acpi.h>

/*
 * these tables are not constants, some fields can be used for
 * pdata or machine ops
 */
extern struct snd_soc_acpi_mach snd_soc_acpi_intel_haswell_machines[];
extern struct snd_soc_acpi_mach snd_soc_acpi_intel_broadwell_machines[];
extern struct snd_soc_acpi_mach snd_soc_acpi_intel_baytrail_legacy_machines[];
extern struct snd_soc_acpi_mach snd_soc_acpi_intel_baytrail_machines[];
extern struct snd_soc_acpi_mach snd_soc_acpi_intel_cherrytrail_machines[];
extern struct snd_soc_acpi_mach snd_soc_acpi_intel_skl_machines[];
extern struct snd_soc_acpi_mach snd_soc_acpi_intel_kbl_machines[];
extern struct snd_soc_acpi_mach snd_soc_acpi_intel_bxt_machines[];
extern struct snd_soc_acpi_mach snd_soc_acpi_intel_glk_machines[];
extern struct snd_soc_acpi_mach snd_soc_acpi_intel_cnl_machines[];
extern struct snd_soc_acpi_mach snd_soc_acpi_intel_icl_machines[];

#endif
