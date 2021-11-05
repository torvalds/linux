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
		.adr = 0x000020025D071100ull,
		.num_endpoints = 1,
		.endpoints = &single_endpoint,
		.name_prefix = "rt711"
	}
};

static const struct snd_soc_acpi_adr_device rt1308_1_group1_adr[] = {
	{
		.adr = 0x000120025D130800ull,
		.num_endpoints = 1,
		.endpoints = &spk_l_endpoint,
		.name_prefix = "rt1308-1"
	}
};

static const struct snd_soc_acpi_adr_device rt1308_2_group1_adr[] = {
	{
		.adr = 0x000220025D130800ull,
		.num_endpoints = 1,
		.endpoints = &spk_r_endpoint,
		.name_prefix = "rt1308-2"
	}
};

static const struct snd_soc_acpi_adr_device rt715_3_adr[] = {
	{
		.adr = 0x000320025D071500ull,
		.num_endpoints = 1,
		.endpoints = &single_endpoint,
		.name_prefix = "rt715"
	}
};

static const struct snd_soc_acpi_adr_device rt711_sdca_0_adr[] = {
	{
		.adr = 0x000030025D071101ull,
		.num_endpoints = 1,
		.endpoints = &single_endpoint,
		.name_prefix = "rt711"
	}
};

static const struct snd_soc_acpi_adr_device rt711_sdca_2_adr[] = {
	{
		.adr = 0x000230025D071101ull,
		.num_endpoints = 1,
		.endpoints = &single_endpoint,
		.name_prefix = "rt711"
	}
};

static const struct snd_soc_acpi_adr_device rt1316_1_group1_adr[] = {
	{
		.adr = 0x000131025D131601ull, /* unique ID is set for some reason */
		.num_endpoints = 1,
		.endpoints = &spk_l_endpoint,
		.name_prefix = "rt1316-1"
	}
};

static const struct snd_soc_acpi_adr_device rt1316_2_group1_adr[] = {
	{
		.adr = 0x000230025D131601ull,
		.num_endpoints = 1,
		.endpoints = &spk_r_endpoint,
		.name_prefix = "rt1316-2"
	}
};

static const struct snd_soc_acpi_adr_device rt1316_3_group1_adr[] = {
	{
		.adr = 0x000330025D131601ull,
		.num_endpoints = 1,
		.endpoints = &spk_r_endpoint,
		.name_prefix = "rt1316-2"
	}
};

static const struct snd_soc_acpi_adr_device rt1316_0_group2_adr[] = {
	{
		.adr = 0x000031025D131601ull,
		.num_endpoints = 1,
		.endpoints = &spk_l_endpoint,
		.name_prefix = "rt1316-1"
	}
};

static const struct snd_soc_acpi_adr_device rt1316_1_group2_adr[] = {
	{
		.adr = 0x000130025D131601ull,
		.num_endpoints = 1,
		.endpoints = &spk_r_endpoint,
		.name_prefix = "rt1316-2"
	}
};

static const struct snd_soc_acpi_adr_device rt1316_2_single_adr[] = {
	{
		.adr = 0x000230025D131601ull,
		.num_endpoints = 1,
		.endpoints = &single_endpoint,
		.name_prefix = "rt1316-1"
	}
};

static const struct snd_soc_acpi_adr_device rt714_0_adr[] = {
	{
		.adr = 0x000030025D071401ull,
		.num_endpoints = 1,
		.endpoints = &single_endpoint,
		.name_prefix = "rt714"
	}
};

static const struct snd_soc_acpi_adr_device rt714_2_adr[] = {
	{
		.adr = 0x000230025D071401ull,
		.num_endpoints = 1,
		.endpoints = &single_endpoint,
		.name_prefix = "rt714"
	}
};

static const struct snd_soc_acpi_adr_device rt714_3_adr[] = {
	{
		.adr = 0x000330025D071401ull,
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

static const struct snd_soc_acpi_link_adr adl_sdw_rt711_link2_rt1316_link01_rt714_link3[] = {
	{
		.mask = BIT(2),
		.num_adr = ARRAY_SIZE(rt711_sdca_2_adr),
		.adr_d = rt711_sdca_2_adr,
	},
	{
		.mask = BIT(0),
		.num_adr = ARRAY_SIZE(rt1316_0_group2_adr),
		.adr_d = rt1316_0_group2_adr,
	},
	{
		.mask = BIT(1),
		.num_adr = ARRAY_SIZE(rt1316_1_group2_adr),
		.adr_d = rt1316_1_group2_adr,
	},
	{
		.mask = BIT(3),
		.num_adr = ARRAY_SIZE(rt714_3_adr),
		.adr_d = rt714_3_adr,
	},
	{}
};

static const struct snd_soc_acpi_link_adr adl_sdw_rt1316_link12_rt714_link0[] = {
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
		.mask = BIT(0),
		.num_adr = ARRAY_SIZE(rt714_0_adr),
		.adr_d = rt714_0_adr,
	},
	{}
};

static const struct snd_soc_acpi_link_adr adl_sdw_rt1316_link2_rt714_link3[] = {
	{
		.mask = BIT(2),
		.num_adr = ARRAY_SIZE(rt1316_2_single_adr),
		.adr_d = rt1316_2_single_adr,
	},
	{
		.mask = BIT(3),
		.num_adr = ARRAY_SIZE(rt714_3_adr),
		.adr_d = rt714_3_adr,
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

static const struct snd_soc_acpi_adr_device mx8373_2_adr[] = {
	{
		.adr = 0x000223019F837300ull,
		.num_endpoints = 1,
		.endpoints = &spk_l_endpoint,
		.name_prefix = "Left"
	},
	{
		.adr = 0x000227019F837300ull,
		.num_endpoints = 1,
		.endpoints = &spk_r_endpoint,
		.name_prefix = "Right"
	}
};

static const struct snd_soc_acpi_adr_device rt5682_0_adr[] = {
	{
		.adr = 0x000021025D568200ull,
		.num_endpoints = 1,
		.endpoints = &single_endpoint,
		.name_prefix = "rt5682"
	}
};

static const struct snd_soc_acpi_link_adr adl_rvp[] = {
	{
		.mask = BIT(0),
		.num_adr = ARRAY_SIZE(rt711_0_adr),
		.adr_d = rt711_0_adr,
	},
	{}
};

static const struct snd_soc_acpi_link_adr adl_chromebook_base[] = {
	{
		.mask = BIT(0),
		.num_adr = ARRAY_SIZE(rt5682_0_adr),
		.adr_d = rt5682_0_adr,
	},
	{
		.mask = BIT(2),
		.num_adr = ARRAY_SIZE(mx8373_2_adr),
		.adr_d = mx8373_2_adr,
	},
	{}
};

static const struct snd_soc_acpi_codecs adl_max98373_amp = {
	.num_codecs = 1,
	.codecs = {"MX98373"}
};

static const struct snd_soc_acpi_codecs adl_max98357a_amp = {
	.num_codecs = 1,
	.codecs = {"MX98357A"}
};

static const struct snd_soc_acpi_codecs adl_max98360a_amp = {
	.num_codecs = 1,
	.codecs = {"MX98360A"}
};

static const struct snd_soc_acpi_codecs adl_rt5682_rt5682s_hp = {
	.num_codecs = 2,
	.codecs = {"10EC5682", "RTL5682"},
};

struct snd_soc_acpi_mach snd_soc_acpi_intel_adl_machines[] = {
	{
		.comp_ids = &adl_rt5682_rt5682s_hp,
		.drv_name = "adl_mx98373_rt5682",
		.machine_quirk = snd_soc_acpi_codec_list,
		.quirk_data = &adl_max98373_amp,
		.sof_fw_filename = "sof-adl.ri",
		.sof_tplg_filename = "sof-adl-max98373-rt5682.tplg",
	},
	{
		.comp_ids = &adl_rt5682_rt5682s_hp,
		.drv_name = "adl_mx98357_rt5682",
		.machine_quirk = snd_soc_acpi_codec_list,
		.quirk_data = &adl_max98357a_amp,
		.sof_fw_filename = "sof-adl.ri",
		.sof_tplg_filename = "sof-adl-max98357a-rt5682.tplg",
	},
	{
		.comp_ids = &adl_rt5682_rt5682s_hp,
		.drv_name = "adl_mx98360_rt5682",
		.machine_quirk = snd_soc_acpi_codec_list,
		.quirk_data = &adl_max98360a_amp,
		.sof_fw_filename = "sof-adl.ri",
		.sof_tplg_filename = "sof-adl-max98360a-rt5682.tplg",
	},
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
		.link_mask = 0xF, /* 4 active links required */
		.links = adl_sdw_rt711_link2_rt1316_link01_rt714_link3,
		.drv_name = "sof_sdw",
		.sof_fw_filename = "sof-adl.ri",
		.sof_tplg_filename = "sof-adl-rt711-l2-rt1316-l01-rt714-l3.tplg",
	},
	{
		.link_mask = 0xC, /* rt1316 on link2 & rt714 on link3 */
		.links = adl_sdw_rt1316_link2_rt714_link3,
		.drv_name = "sof_sdw",
		.sof_fw_filename = "sof-adl.ri",
		.sof_tplg_filename = "sof-adl-rt1316-l2-mono-rt714-l3.tplg",
	},
	{
		.link_mask = 0x7, /* rt714 on link0 & two rt1316s on link1 and link2 */
		.links = adl_sdw_rt1316_link12_rt714_link0,
		.drv_name = "sof_sdw",
		.sof_fw_filename = "sof-adl.ri",
		.sof_tplg_filename = "sof-adl-rt1316-l12-rt714-l0.tplg",
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
	{
		.link_mask = 0x5, /* rt5682 on link0 & 2xmax98373 on link 2 */
		.links = adl_chromebook_base,
		.drv_name = "sof_sdw",
		.sof_fw_filename = "sof-adl.ri",
		.sof_tplg_filename = "sof-adl-sdw-max98373-rt5682.tplg",
	},
	{},
};
EXPORT_SYMBOL_GPL(snd_soc_acpi_intel_adl_sdw_machines);
