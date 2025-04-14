/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * soc-acpi-intel-get-tplg.h - get-tplg-files ops
 *
 * Copyright (c) 2025, Intel Corporation.
 *
 */

#ifndef _SND_SOC_ACPI_INTEL_GET_TPLG_H
#define _SND_SOC_ACPI_INTEL_GET_TPLG_H

int sof_sdw_get_tplg_files(struct snd_soc_card *card, const struct snd_soc_acpi_mach *mach,
			   const char *prefix, const char ***tplg_files);

#endif
