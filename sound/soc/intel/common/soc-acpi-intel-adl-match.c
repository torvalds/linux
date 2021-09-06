// SPDX-License-Identifier: GPL-2.0-only
/*
 * soc-apci-intel-adl-match.c - tables and support for ADL ACPI enumeration.
 *
 * Copyright (c) 2020, Intel Corporation.
 */

#include <sound/soc-acpi.h>
#include <sound/soc-acpi-intel-match.h>

static const struct snd_soc_acpi_endpoint single_endpoint = {
	.num = 0,
	.aggregated = 0,
	.group_position = 0,
	.group_id = 0,
};

static const struct snd_soc_acpi_endpoint spk_l_endpoint = {
	.num = 0,
	.aggregated = 1,
	.group_position = 0,
	.group_id = 1,
};

static const struct snd_soc_acpi_endpoint spk_r_endpoint = {
	.num = 0,
	.aggregated = 1,
	.group_position = 1,
	.group_id = 1,
};

static const struct snd_soc_acpi_adr_device rt711_0_adr[] = {
	{
		.adr = 0x000020025D071100,
		.num_endpoints = 1,
		.endpoints = &single_endpoint,
		.name_prefix = "rt711"
	}
};

static const struct snd_soc_acpi_adr_device rt1308_1_group1_adr[] = {
	{
		.adr = 0x000120025D130800,
		.num_endpoints = 1,
		.endpoints = &spk_l_endpoint,
		.name_prefix = "rt1308-1"
	}
};

static const struct snd_soc_acpi_adr_device rt1308_2_group1_adr[] = {
	{
		.adr = 0x000220025D130800,
		.num_endpoints = 1,
		.endpoints = &spk_r_endpoint,
		.name_prefix = "rt1308-2"
	}
};

static const struct snd_soc_acpi_adr_device rt715_3_adr[] = {
	{
		.adr = 0x000320025D071500,
		.num_endpoints = 1,
		.endpoints = &single_endpoint,
		.name_prefix = "rt715"
	}
};

static const struct snd_soc_acpi_adr_device rt711_sdca_0_adr[] = {
	{
		.adr = 0x000030025D071101,
		.num_endpoints = 1,
		.endpoints = &single_endpoint,
		.name_prefix = "rt711"
	}
};

static const struct snd_soc_acpi_adr_device rt1316_1_group1_adr[] = {
	{
		.adr = 0x000131025D131601, /* unique ID is set for some reason */
		.num_endpoints = 1,
		.endpoints = &spk_l_endpoint,
		.name_prefix = "rt1316-1"
	}
};

static const struct snd_soc_acpi_adr_device rt1316_2_group1_adr[] = {
	{
		.adr = 0x000230025D131601,
		.num_endpoints = 1,
		.endpoints = &spk_r_endpoint,
		.name_prefix = "rt1316-2"
	}
};

static const struct snd_soc_acpi_adr_device rt1316_3_group1_adr[] = {
	{
		.adr = 0x000330025D131601,
		.num_endpoints = 1,
		.endpoints = &spk_r_endpoint,
		.name_prefix = "rt1316-2"
	}
};

static const struct snd_soc_acpi_adr_device rt1316_2_single_adr[] = {
	{
		.adr = 0x000230025D131601,
		.num_endpoints = 1,
		.endpoints = &single_endpoint,
		.name_prefix = "rt1316-1"
	}
};

static const struct snd_soc_acpi_adr_device rt714_0_adr[] = {
	{
		.adr = 0x000030025D071401,
		.num_endpoints = 1,
		.endpoints = &single_endpoint,
		.name_prefix = "rt714"
	}
};

static const struct snd_soc_acpi_adr_device rt714_2_adr[] = {
	{
		.adr = 0x000230025D071401,
		.num_endpoints = 1,
		.endpoints = &single_endpoint,
		.name_prefix = "rt714"
	}
};

static const struct snd_soc_acpi_adr_device rt714_3_adr[] = {
	{
		.adr = 0x000330025D071401,
		.num_endpoints = 1,
		.endpoints = &single_endpoint,
		.name_prefix = "rt714"
	}
};

static const struct snd_soc_acpi_link_adr adl_default[] = {
	{
		.mask = BIT(0),
		.num_adr = ARRAY_SIZE(rt711_0_adr),
		.adr_d = rt711_0_adr,
	},
	{
		.mask = BIT(1),
		.num_adr = ARRAY_SIZE(rt1308_1_group1_adr),
		.adr_d = rt1308_1_group1_adr,
	},
	{
		.mask = BIT(2),
		.num_adr = ARRAY_SIZE(rt1308_2_group1_adr),
		.adr_d = rt1308_2_group1_adr,
	},
	{
		.mask = BIT(3),
		.num_adr = ARRAY_SIZE(rt715_3_adr),
		.adr_d = rt715_3_adr,
	},
	{}
};

static const struct snd_soc_acpi_link_adr adl_sdca_default[] = {
	{
		.mask = BIT(0),
		.num_adr = ARRAY_SIZE(rt711_sdca_0_adr),
		.adr_d = rt711_sdca_0_adr,
	},
	{
		.mask = BIT(1),
		.num_adr = ARRAY_SIZE(rt1316_1_group1_adr),
		.adr_d = rt1316_1_group1_adr,
	},
	{
		.mask = BIT(2),
		.num_adr = ARRAY_SIZE(rt1316_2_group1_adr),
		.adr_d = rt1316_2_group1_adr,
	},
	{
		.mask = BIT(3),
		.num_adr = ARRAY_SIZE(rt714_3_adr),
		.adr_d = rt714_3_adr,
	},
	{}
};

static const struct snd_soc_acpi_link_adr adl_sdca_3_in_1[] = {
	{
		.mask = BIT(0),
		.num_adr = ARRAY_SIZE(rt711_sdca_0_adr),
		.adr_d = rt711_sdca_0_adr,
	},
	{
		.mask = BIT(1),
		.num_adr = ARRAY_SIZE(rt1316_1_group1_adr),
		.adr_d = rt1316_1_group1_adr,
	},
	{
		.mask = BIT(2),
		.num_adr = ARRAY_SIZE(rt714_2_adr),
		.adr_d = rt714_2_adr,
	},
	{
		.mask = BIT(3),
		.num_adr = ARRAY_SIZE(rt1316_3_group1_adr),
		.adr_d = rt1316_3_group1_adr,
	},
	{}
};

static const struct snd_soc_acpi_link_adr adl_sdw_rt1316_link2_rt714_link0[] = {
	{
		.mask = BIT(2),
		.num_adr = ARRAY_SIZE(rt1316_2_single_adr),
		.adr_d = rt1316_2_single_adr,
	},
	{
		.mask = BIT(0),
		.num_adr = ARRAY_SIZE(rt714_0_adr),
		.adr_d = rt714_0_adr,
	},
	{}
};

static const struct snd_soc_acpi_link_adr adl_rvp[] = {
	{
		.mask = BIT(0),
		.num_adr = ARRAY_SIZE(rt711_0_adr),
		.adr_d = rt711_0_adr,
	},
	{}
};

struct snd_soc_acpi_mach snd_soc_acpi_intel_adl_machines[] = {
	{},
};
EXPORT_SYMBOL_GPL(snd_soc_acpi_intel_adl_machines);

/* this table is used when there is no I2S codec present */
struct snd_soc_acpi_mach snd_soc_acpi_intel_adl_sdw_machines[] = {
	{
		.link_mask = 0xF, /* 4 active links required */
		.links = adl_default,
		.drv_name = "sof_sdw",
		.sof_tplg_filename = "sof-adl-rt711-l0-rt1308-l12-rt715-l3.tplg",
	},
	{
		.link_mask = 0xF, /* 4 active links required */
		.links = adl_sdca_default,
		.drv_name = "sof_sdw",
		.sof_tplg_filename = "sof-adl-rt711-l0-rt1316-l12-rt714-l3.tplg",
	},
	{
		.link_mask = 0xF, /* 4 active links required */
		.links = adl_sdca_3_in_1,
		.drv_name = "sof_sdw",
		.sof_tplg_filename = "sof-adl-rt711-l0-rt1316-l13-rt714-l2.tplg",
	},
	{
		.link_mask = 0x5, /* 2 active links required */
		.links = adl_sdw_rt1316_link2_rt714_link0,
		.drv_name = "sof_sdw",
		.sof_tplg_filename = "sof-adl-rt1316-l2-mono-rt714-l0.tplg",
	},
	{
		.link_mask = 0x1, /* link0 required */
		.links = adl_rvp,
		.drv_name = "sof_sdw",
		.sof_tplg_filename = "sof-adl-rt711.tplg",
	},
	{},
};
EXPORT_SYMBOL_GPL(snd_soc_acpi_intel_adl_sdw_machines);
