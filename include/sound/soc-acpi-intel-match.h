/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (C) 2017, Intel Corporation. All rights reserved.
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
extern struct snd_soc_acpi_mach snd_soc_acpi_intel_cfl_machines[];
extern struct snd_soc_acpi_mach snd_soc_acpi_intel_cml_machines[];
extern struct snd_soc_acpi_mach snd_soc_acpi_intel_icl_machines[];
extern struct snd_soc_acpi_mach snd_soc_acpi_intel_tgl_machines[];
extern struct snd_soc_acpi_mach snd_soc_acpi_intel_ehl_machines[];
extern struct snd_soc_acpi_mach snd_soc_acpi_intel_jsl_machines[];

extern struct snd_soc_acpi_mach snd_soc_acpi_intel_cnl_sdw_machines[];
extern struct snd_soc_acpi_mach snd_soc_acpi_intel_cfl_sdw_machines[];
extern struct snd_soc_acpi_mach snd_soc_acpi_intel_cml_sdw_machines[];
extern struct snd_soc_acpi_mach snd_soc_acpi_intel_icl_sdw_machines[];
extern struct snd_soc_acpi_mach snd_soc_acpi_intel_tgl_sdw_machines[];

/*
 * generic table used for HDA codec-based platforms, possibly with
 * additional ACPI-enumerated codecs
 */
extern struct snd_soc_acpi_mach snd_soc_acpi_intel_hda_machines[];

#endif
