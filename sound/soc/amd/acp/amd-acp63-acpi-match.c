// SPDX-License-Identifier: GPL-2.0-only
/*
 * amd-acp63-acpi-match.c - tables and support for ACP 6.3 platform
 * ACPI enumeration.
 *
 * Copyright 2024 Advanced Micro Devices, Inc.
 */

#include <sound/soc-acpi.h>
#include "../mach-config.h"

static const struct snd_soc_acpi_endpoint single_endpoint = {
	.num = 0,
	.aggregated = 0,
	.group_position = 0,
	.group_id = 0
};

static const struct snd_soc_acpi_endpoint spk_l_endpoint = {
	.num = 0,
	.aggregated = 1,
	.group_position = 0,
	.group_id = 1
};

static const struct snd_soc_acpi_endpoint spk_r_endpoint = {
	.num = 0,
	.aggregated = 1,
	.group_position = 1,
	.group_id = 1
};

static const struct snd_soc_acpi_adr_device rt711_rt1316_group_adr[] = {
	{
		.adr = 0x000030025D071101ull,
		.num_endpoints = 1,
		.endpoints = &single_endpoint,
		.name_prefix = "rt711"
	},
	{
		.adr = 0x000030025D131601ull,
		.num_endpoints = 1,
		.endpoints = &spk_l_endpoint,
		.name_prefix = "rt1316-1"
	},
	{
		.adr = 0x000032025D131601ull,
		.num_endpoints = 1,
		.endpoints = &spk_r_endpoint,
		.name_prefix = "rt1316-2"
	},
};

static const struct snd_soc_acpi_adr_device rt714_adr[] = {
	{
		.adr = 0x130025d071401ull,
		.num_endpoints = 1,
		.endpoints = &single_endpoint,
		.name_prefix = "rt714"
	}
};

static const struct snd_soc_acpi_link_adr acp63_4_in_1_sdca[] = {
	{	.mask = BIT(0),
		.num_adr = ARRAY_SIZE(rt711_rt1316_group_adr),
		.adr_d = rt711_rt1316_group_adr,
	},
	{
		.mask = BIT(1),
		.num_adr = ARRAY_SIZE(rt714_adr),
		.adr_d = rt714_adr,
	},
	{}
};

struct snd_soc_acpi_mach snd_soc_acpi_amd_acp63_sof_sdw_machines[] = {
	{
		.link_mask = BIT(0) | BIT(1),
		.links = acp63_4_in_1_sdca,
		.drv_name = "amd_sof_sdw",
		.sof_tplg_filename = "sof-acp_6_3-rt711-l0-rt1316-l0-rt714-l1.tplg",
		.fw_filename = "sof-acp_6_3.ri",
	},
	{},
};
EXPORT_SYMBOL(snd_soc_acpi_amd_acp63_sof_sdw_machines);

MODULE_DESCRIPTION("AMD ACP6.3 tables and support for ACPI enumeration");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Vijendar.Mukunda@amd.com");
