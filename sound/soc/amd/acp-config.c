// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license. When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2021 Advanced Micro Devices, Inc.
//
// Authors: Ajit Kumar Pandey <AjitKumar.Pandey@amd.com>
//

/* ACP machine configuration module */

#include <linux/acpi.h>
#include <linux/bits.h>
#include <linux/dmi.h>
#include <linux/module.h>
#include <linux/pci.h>

#include "../sof/amd/acp.h"
#include "mach-config.h"

static int acp_quirk_data;

static const struct config_entry config_table[] = {
	{
		.flags = FLAG_AMD_SOF,
		.device = ACP_PCI_DEV_ID,
		.dmi_table = (const struct dmi_system_id []) {
			{
				.matches = {
					DMI_MATCH(DMI_SYS_VENDOR, "AMD"),
					DMI_MATCH(DMI_PRODUCT_NAME, "Majolica-CZN"),
				},
			},
			{}
		},
	},
};

int snd_amd_acp_find_config(struct pci_dev *pci)
{
	const struct config_entry *table = config_table;
	u16 device = pci->device;
	int i;

	for (i = 0; i < ARRAY_SIZE(config_table); i++, table++) {
		if (table->device != device)
			continue;
		if (table->dmi_table && !dmi_check_system(table->dmi_table))
			continue;
		acp_quirk_data = table->flags;
		return table->flags;
	}

	return 0;
}
EXPORT_SYMBOL(snd_amd_acp_find_config);

struct snd_soc_acpi_mach snd_soc_acpi_amd_sof_machines[] = {
	{
		.id = "AMDI1019",
		.drv_name = "renoir-dsp",
		.pdata = (void *)&acp_quirk_data,
		.fw_filename = "sof-rn.ri",
		.sof_tplg_filename = "sof-acp.tplg",
	},
	{},
};
EXPORT_SYMBOL(snd_soc_acpi_amd_sof_machines);

MODULE_LICENSE("Dual BSD/GPL");
