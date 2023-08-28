/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/*
 * This file is provided under a dual BSD/GPLv2 license. When using or
 * redistributing this file, you may do so under either license.
 *
 * Copyright(c) 2021 Advanced Micro Devices, Inc. All rights reserved.
 *
 * Author: Ajit Kumar Pandey <AjitKumar.Pandey@amd.com>
 */
#ifndef __AMD_MACH_CONFIG_H
#define __AMD_MACH_CONFIG_H

#include <sound/soc-acpi.h>

#define FLAG_AMD_SOF			BIT(1)
#define FLAG_AMD_SOF_ONLY_DMIC		BIT(2)
#define FLAG_AMD_LEGACY			BIT(3)

#define ACP_PCI_DEV_ID			0x15E2

extern struct snd_soc_acpi_mach snd_soc_acpi_amd_sof_machines[];
extern struct snd_soc_acpi_mach snd_soc_acpi_amd_rmb_sof_machines[];
extern struct snd_soc_acpi_mach snd_soc_acpi_amd_vangogh_sof_machines[];

struct config_entry {
	u32 flags;
	u16 device;
	const struct dmi_system_id *dmi_table;
};

#endif
