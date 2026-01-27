// SPDX-License-Identifier: GPL-2.0-only
/*
 * amd-acp70-acpi-match.c - tables and support for ACP 7.0 & ACP7.1
 * ACPI enumeration.
 *
 * Copyright 2025 Advanced Micro Devices, Inc.
 */

#include <sound/soc-acpi.h>
#include "soc-acpi-amd-sdca-quirks.h"
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

static const struct snd_soc_acpi_endpoint spk_2_endpoint = {
	.num = 0,
	.aggregated = 1,
	.group_position = 2,
	.group_id = 1
};

static const struct snd_soc_acpi_endpoint spk_3_endpoint = {
	.num = 0,
	.aggregated = 1,
	.group_position = 3,
	.group_id = 1
};

static const struct snd_soc_acpi_endpoint jack_amp_g1_dmic_endpoints[] = {
	/* Jack Endpoint */
	{
		.num = 0,
		.aggregated = 0,
		.group_position = 0,
		.group_id = 0,
	},
	/* Amp Endpoint, work as spk_l_endpoint */
	{
		.num = 1,
		.aggregated = 1,
		.group_position = 0,
		.group_id = 1,
	},
	/* DMIC Endpoint */
	{
		.num = 2,
		.aggregated = 0,
		.group_position = 0,
		.group_id = 0,
	},
};

static const struct snd_soc_acpi_adr_device rt712_vb_1_group1_adr[] = {
	{
		.adr = 0x000130025D071201ull,
		.num_endpoints = ARRAY_SIZE(jack_amp_g1_dmic_endpoints),
		.endpoints = jack_amp_g1_dmic_endpoints,
		.name_prefix = "rt712"
	}
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

static const struct snd_soc_acpi_link_adr acp70_4_in_1_sdca[] = {
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

static const struct snd_soc_acpi_endpoint rt722_endpoints[] = {
	{
		.num = 0,
		.aggregated = 0,
		.group_position = 0,
		.group_id = 0,
	},
	{
		.num = 1,
		.aggregated = 0,
		.group_position = 0,
		.group_id = 0,
	},
	{
		.num = 2,
		.aggregated = 0,
		.group_position = 0,
		.group_id = 0,
	},
};

static const struct snd_soc_acpi_adr_device rt722_0_single_adr[] = {
	{
		.adr = 0x000030025d072201ull,
		.num_endpoints = ARRAY_SIZE(rt722_endpoints),
		.endpoints = rt722_endpoints,
		.name_prefix = "rt722"
	}
};

static const struct snd_soc_acpi_adr_device rt1320_1_single_adr[] = {
	{
		.adr = 0x000130025D132001ull,
		.num_endpoints = 1,
		.endpoints = &single_endpoint,
		.name_prefix = "rt1320-1"
	}
};

static const struct snd_soc_acpi_endpoint cs42l43_endpoints[] = {
	{ /* Jack Playback Endpoint */
		.num = 0,
		.aggregated = 0,
		.group_position = 0,
		.group_id = 0,
	},
	{ /* DMIC Capture Endpoint */
		.num = 1,
		.aggregated = 0,
		.group_position = 0,
		.group_id = 0,
	},
	{ /* Jack Capture Endpoint */
		.num = 2,
		.aggregated = 0,
		.group_position = 0,
		.group_id = 0,
	},
	{ /* Speaker Playback Endpoint */
		.num = 3,
		.aggregated = 0,
		.group_position = 0,
		.group_id = 0,
	},
};

static const struct snd_soc_acpi_adr_device cs35l56x4_l1u3210_adr[] = {
	{
		.adr = 0x00013301FA355601ull,
		.num_endpoints = 1,
		.endpoints = &spk_l_endpoint,
		.name_prefix = "AMP1"
	},
	{
		.adr = 0x00013201FA355601ull,
		.num_endpoints = 1,
		.endpoints = &spk_r_endpoint,
		.name_prefix = "AMP2"
	},
	{
		.adr = 0x00013101FA355601ull,
		.num_endpoints = 1,
		.endpoints = &spk_2_endpoint,
		.name_prefix = "AMP3"
	},
	{
		.adr = 0x00013001FA355601ull,
		.num_endpoints = 1,
		.endpoints = &spk_3_endpoint,
		.name_prefix = "AMP4"
	},
};

static const struct snd_soc_acpi_adr_device cs35l63x2_l0u01_adr[] = {
	{
		.adr = 0x00003001FA356301ull,
		.num_endpoints = 1,
		.endpoints = &spk_l_endpoint,
		.name_prefix = "AMP1"
	},
	{
		.adr = 0x00003101FA356301ull,
		.num_endpoints = 1,
		.endpoints = &spk_r_endpoint,
		.name_prefix = "AMP2"
	},
};

static const struct snd_soc_acpi_adr_device cs35l63x2_l1u01_adr[] = {
	{
		.adr = 0x00013001FA356301ull,
		.num_endpoints = 1,
		.endpoints = &spk_l_endpoint,
		.name_prefix = "AMP1"
	},
	{
		.adr = 0x00013101FA356301ull,
		.num_endpoints = 1,
		.endpoints = &spk_r_endpoint,
		.name_prefix = "AMP2"
	},
};

static const struct snd_soc_acpi_adr_device cs35l63x2_l1u13_adr[] = {
	{
		.adr = 0x00013101FA356301ull,
		.num_endpoints = 1,
		.endpoints = &spk_l_endpoint,
		.name_prefix = "AMP1"
	},
	{
		.adr = 0x00013301FA356301ull,
		.num_endpoints = 1,
		.endpoints = &spk_r_endpoint,
		.name_prefix = "AMP2"
	},
};

static const struct snd_soc_acpi_adr_device cs35l63x4_l0u0246_adr[] = {
	{
		.adr = 0x00003001FA356301ull,
		.num_endpoints = 1,
		.endpoints = &spk_l_endpoint,
		.name_prefix = "AMP1"
	},
	{
		.adr = 0x00003201FA356301ull,
		.num_endpoints = 1,
		.endpoints = &spk_r_endpoint,
		.name_prefix = "AMP2"
	},
	{
		.adr = 0x00003401FA356301ull,
		.num_endpoints = 1,
		.endpoints = &spk_2_endpoint,
		.name_prefix = "AMP3"
	},
	{
		.adr = 0x00003601FA356301ull,
		.num_endpoints = 1,
		.endpoints = &spk_3_endpoint,
		.name_prefix = "AMP4"
	},
};

static const struct snd_soc_acpi_adr_device cs42l43_l0u0_adr[] = {
	{
		.adr = 0x00003001FA424301ull,
		.num_endpoints = ARRAY_SIZE(cs42l43_endpoints),
		.endpoints = cs42l43_endpoints,
		.name_prefix = "cs42l43"
	}
};

static const struct snd_soc_acpi_adr_device cs42l43_l0u1_adr[] = {
	{
		.adr = 0x00003101FA424301ull,
		.num_endpoints = ARRAY_SIZE(cs42l43_endpoints),
		.endpoints = cs42l43_endpoints,
		.name_prefix = "cs42l43"
	}
};

static const struct snd_soc_acpi_adr_device cs42l43b_l0u1_adr[] = {
	{
		.adr = 0x00003101FA2A3B01ull,
		.num_endpoints = ARRAY_SIZE(cs42l43_endpoints),
		.endpoints = cs42l43_endpoints,
		.name_prefix = "cs42l43"
	}
};

static const struct snd_soc_acpi_adr_device cs42l43_l1u0_cs35l56x4_l1u0123_adr[] = {
	{
		.adr = 0x00013001FA424301ull,
		.num_endpoints = ARRAY_SIZE(cs42l43_endpoints),
		.endpoints = cs42l43_endpoints,
		.name_prefix = "cs42l43"
	},
	{
		.adr = 0x00013001FA355601ull,
		.num_endpoints = 1,
		.endpoints = &spk_l_endpoint,
		.name_prefix = "AMP1"
	},
	{
		.adr = 0x00013101FA355601ull,
		.num_endpoints = 1,
		.endpoints = &spk_r_endpoint,
		.name_prefix = "AMP2"
	},
	{
		.adr = 0x00013201FA355601ull,
		.num_endpoints = 1,
		.endpoints = &spk_2_endpoint,
		.name_prefix = "AMP3"
	},
	{
		.adr = 0x00013301FA355601ull,
		.num_endpoints = 1,
		.endpoints = &spk_3_endpoint,
		.name_prefix = "AMP4"
	},
};

static const struct snd_soc_acpi_adr_device cs42l45_l0u0_adr[] = {
	{
		.adr = 0x00003001FA424501ull,
		/* Re-use endpoints, but cs42l45 has no speaker */
		.num_endpoints = ARRAY_SIZE(cs42l43_endpoints) - 1,
		.endpoints = cs42l43_endpoints,
		.name_prefix = "cs42l45"
	}
};

static const struct snd_soc_acpi_adr_device cs42l45_l1u0_adr[] = {
	{
		.adr = 0x00013001FA424501ull,
		/* Re-use endpoints, but cs42l45 has no speaker */
		.num_endpoints = ARRAY_SIZE(cs42l43_endpoints) - 1,
		.endpoints = cs42l43_endpoints,
		.name_prefix = "cs42l45"
	}
};

static const struct snd_soc_acpi_link_adr acp70_cs35l56x4_l1u3210[] = {
	{
		.mask = BIT(1),
		.num_adr = ARRAY_SIZE(cs35l56x4_l1u3210_adr),
		.adr_d = cs35l56x4_l1u3210_adr,
	},
	{}
};

static const struct snd_soc_acpi_link_adr acp70_cs35l63x4_l0u0246[] = {
	{
		.mask = BIT(0),
		.num_adr = ARRAY_SIZE(cs35l63x4_l0u0246_adr),
		.adr_d = cs35l63x4_l0u0246_adr,
	},
	{}
};

static const struct snd_soc_acpi_link_adr acp70_cs42l43_l0u1[] = {
	{
		.mask = BIT(0),
		.num_adr = ARRAY_SIZE(cs42l43_l0u1_adr),
		.adr_d = cs42l43_l0u1_adr,
	},
	{}
};

static const struct snd_soc_acpi_link_adr acp70_cs42l43b_l0u1[] = {
	{
		.mask = BIT(0),
		.num_adr = ARRAY_SIZE(cs42l43b_l0u1_adr),
		.adr_d = cs42l43b_l0u1_adr,
	},
	{}
};

static const struct snd_soc_acpi_link_adr acp70_cs42l43_l0u0_cs35l56x4_l1u3210[] = {
	{
		.mask = BIT(0),
		.num_adr = ARRAY_SIZE(cs42l43_l0u0_adr),
		.adr_d = cs42l43_l0u0_adr,
	},
	{
		.mask = BIT(1),
		.num_adr = ARRAY_SIZE(cs35l56x4_l1u3210_adr),
		.adr_d = cs35l56x4_l1u3210_adr,
	},
	{}
};

static const struct snd_soc_acpi_link_adr acp70_cs42l43_l1u0_cs35l56x4_l1u0123[] = {
	{
		.mask = BIT(1),
		.num_adr = ARRAY_SIZE(cs42l43_l1u0_cs35l56x4_l1u0123_adr),
		.adr_d = cs42l43_l1u0_cs35l56x4_l1u0123_adr,
	},
	{}
};

static const struct snd_soc_acpi_link_adr acp70_cs42l45_l0u0[] = {
	{
		.mask = BIT(0),
		.num_adr = ARRAY_SIZE(cs42l45_l0u0_adr),
		.adr_d = cs42l45_l0u0_adr,
	},
	{}
};

static const struct snd_soc_acpi_link_adr acp70_cs42l45_l0u0_cs35l63x2_l1u01[] = {
	{
		.mask = BIT(0),
		.num_adr = ARRAY_SIZE(cs42l45_l0u0_adr),
		.adr_d = cs42l45_l0u0_adr,
	},
	{
		.mask = BIT(1),
		.num_adr = ARRAY_SIZE(cs35l63x2_l1u01_adr),
		.adr_d = cs35l63x2_l1u01_adr,
	},
	{}
};

static const struct snd_soc_acpi_link_adr acp70_cs42l45_l0u0_cs35l63x2_l1u13[] = {
	{
		.mask = BIT(0),
		.num_adr = ARRAY_SIZE(cs42l45_l0u0_adr),
		.adr_d = cs42l45_l0u0_adr,
	},
	{
		.mask = BIT(1),
		.num_adr = ARRAY_SIZE(cs35l63x2_l1u13_adr),
		.adr_d = cs35l63x2_l1u13_adr,
	},
	{}
};

static const struct snd_soc_acpi_link_adr acp70_cs42l45_l1u0[] = {
	{
		.mask = BIT(1),
		.num_adr = ARRAY_SIZE(cs42l45_l1u0_adr),
		.adr_d = cs42l45_l1u0_adr,
	},
	{}
};

static const struct snd_soc_acpi_link_adr acp70_cs42l45_l1u0_cs35l63x2_l0u01[] = {
	{
		.mask = BIT(1),
		.num_adr = ARRAY_SIZE(cs42l45_l1u0_adr),
		.adr_d = cs42l45_l1u0_adr,
	},
	{
		.mask = BIT(0),
		.num_adr = ARRAY_SIZE(cs35l63x2_l0u01_adr),
		.adr_d = cs35l63x2_l0u01_adr,
	},
	{}
};

static const struct snd_soc_acpi_link_adr acp70_cs42l45_l1u0_cs35l63x4_l0u0246[] = {
	{
		.mask = BIT(1),
		.num_adr = ARRAY_SIZE(cs42l45_l1u0_adr),
		.adr_d = cs42l45_l1u0_adr,
	},
	{
		.mask = BIT(0),
		.num_adr = ARRAY_SIZE(cs35l63x4_l0u0246_adr),
		.adr_d = cs35l63x4_l0u0246_adr,
	},
	{}
};

static const struct snd_soc_acpi_link_adr acp70_alc712_vb_l1[] = {
	{
		.mask = BIT(1),
		.num_adr = ARRAY_SIZE(rt712_vb_1_group1_adr),
		.adr_d = rt712_vb_1_group1_adr,
	},
	{}
};

static const struct snd_soc_acpi_link_adr acp70_rt722_only[] = {
	{
		.mask = BIT(0),
		.num_adr = ARRAY_SIZE(rt722_0_single_adr),
		.adr_d = rt722_0_single_adr,
	},
	{}
};

static const struct snd_soc_acpi_link_adr acp70_rt722_l0_rt1320_l1[] = {
	{
		.mask = BIT(0),
		.num_adr = ARRAY_SIZE(rt722_0_single_adr),
		.adr_d = rt722_0_single_adr,
	},
	{
		.mask = BIT(1),
		.num_adr = ARRAY_SIZE(rt1320_1_single_adr),
		.adr_d = rt1320_1_single_adr,
	},
	{}
};

struct snd_soc_acpi_mach snd_soc_acpi_amd_acp70_sdw_machines[] = {
	{
		.link_mask = BIT(0) | BIT(1),
		.links = acp70_rt722_l0_rt1320_l1,
		.drv_name = "amd_sdw",
	},
	{
		.link_mask = BIT(0) | BIT(1),
		.links = acp70_4_in_1_sdca,
		.drv_name = "amd_sdw",
	},
	{
		.link_mask = BIT(0) | BIT(1),
		.links = acp70_cs42l43_l0u0_cs35l56x4_l1u3210,
		.drv_name = "amd_sdw",
	},
	{
		.link_mask = BIT(0) | BIT(1),
		.links = acp70_cs42l45_l1u0_cs35l63x4_l0u0246,
		.drv_name = "amd_sdw",
	},
	{
		.link_mask = BIT(0) | BIT(1),
		.links = acp70_cs42l45_l0u0_cs35l63x2_l1u01,
		.drv_name = "amd_sdw",
	},
	{
		.link_mask = BIT(0) | BIT(1),
		.links = acp70_cs42l45_l0u0_cs35l63x2_l1u13,
		.drv_name = "amd_sdw",
	},
	{
		.link_mask = BIT(0) | BIT(1),
		.links = acp70_cs42l45_l1u0_cs35l63x2_l0u01,
		.drv_name = "amd_sdw",
	},
	{
		.link_mask = BIT(1),
		.links = acp70_cs42l43_l1u0_cs35l56x4_l1u0123,
		.drv_name = "amd_sdw",
	},
	{
		.link_mask = BIT(1),
		.links = acp70_cs35l56x4_l1u3210,
		.drv_name = "amd_sdw",
	},
	{
		.link_mask = BIT(0),
		.links = acp70_cs35l63x4_l0u0246,
		.drv_name = "amd_sdw",
	},
	{
		.link_mask = BIT(0),
		.links = acp70_rt722_only,
		.drv_name = "amd_sdw",
	},
	{
		.link_mask = BIT(0),
		.links = acp70_cs42l43_l0u1,
		.drv_name = "amd_sdw",
	},
	{
		.link_mask = BIT(0),
		.links = acp70_cs42l43b_l0u1,
		.drv_name = "amd_sdw",
	},
	{
		.link_mask = BIT(0),
		.links = acp70_cs42l45_l0u0,
		.drv_name = "amd_sdw",
	},
	{
		.link_mask = BIT(1),
		.links = acp70_cs42l45_l1u0,
		.drv_name = "amd_sdw",
	},
	{
		.link_mask = BIT(1),
		.links = acp70_alc712_vb_l1,
		.machine_check = snd_soc_acpi_amd_sdca_is_device_rt712_vb,
		.drv_name = "amd_sdw",
	},
	{},
};
EXPORT_SYMBOL(snd_soc_acpi_amd_acp70_sdw_machines);

struct snd_soc_acpi_mach snd_soc_acpi_amd_acp70_sof_sdw_machines[] = {
	{
		.link_mask = BIT(0),
		.links = acp70_rt722_only,
		.drv_name = "amd_sof_sdw",
		.sof_tplg_filename = "sof-acp_7_0-rt722-l0.tplg",
		.fw_filename = "sof-acp_7_0.ri",
	},
	{},
};
EXPORT_SYMBOL(snd_soc_acpi_amd_acp70_sof_sdw_machines);

MODULE_DESCRIPTION("AMD ACP7.0 & ACP7.1 tables and support for ACPI enumeration");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Vijendar.Mukunda@amd.com");
#if IS_ENABLED(CONFIG_SND_SOC_ACPI_AMD_SDCA_QUIRKS)
MODULE_IMPORT_NS("SND_SOC_ACPI_AMD_SDCA_QUIRKS");
#endif
