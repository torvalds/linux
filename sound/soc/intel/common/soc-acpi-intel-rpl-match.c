// SPDX-License-Identifier: GPL-2.0-only
/*
 * soc-apci-intel-rpl-match.c - tables and support for RPL ACPI enumeration.
 *
 * Copyright (c) 2022 Intel Corporation.
 */

#include <sound/soc-acpi.h>
#include <sound/soc-acpi-intel-match.h>
#include <sound/soc-acpi-intel-ssp-common.h>

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

static const struct snd_soc_acpi_adr_device cs42l43_0_adr[] = {
	{
		.adr = 0x00003001FA424301ull,
		.num_endpoints = ARRAY_SIZE(cs42l43_endpoints),
		.endpoints = cs42l43_endpoints,
		.name_prefix = "cs42l43"
	}
};

static const struct snd_soc_acpi_adr_device rt711_0_adr[] = {
	{
		.adr = 0x000020025D071100ull,
		.num_endpoints = 1,
		.endpoints = &single_endpoint,
		.name_prefix = "rt711"
	}
};

static const struct snd_soc_acpi_link_adr rpl_rvp[] = {
	{
		.mask = BIT(0),
		.num_adr = ARRAY_SIZE(rt711_0_adr),
		.adr_d = rt711_0_adr,
	},
	{}
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
		.adr = 0x000030025D131601ull,
		.num_endpoints = 1,
		.endpoints = &spk_l_endpoint,
		.name_prefix = "rt1316-1"
	}
};

static const struct snd_soc_acpi_adr_device rt1316_1_group2_adr[] = {
	{
		.adr = 0x000131025D131601ull,
		.num_endpoints = 1,
		.endpoints = &spk_r_endpoint,
		.name_prefix = "rt1316-2"
	}
};

static const struct snd_soc_acpi_adr_device rt1318_1_group1_adr[] = {
	{
		.adr = 0x000132025D131801ull,
		.num_endpoints = 1,
		.endpoints = &spk_l_endpoint,
		.name_prefix = "rt1318-1"
	}
};

static const struct snd_soc_acpi_adr_device rt1318_2_group1_adr[] = {
	{
		.adr = 0x000230025D131801ull,
		.num_endpoints = 1,
		.endpoints = &spk_r_endpoint,
		.name_prefix = "rt1318-2"
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

static const struct snd_soc_acpi_link_adr rpl_cs42l43_l0[] = {
	{
		.mask = BIT(0),
		.num_adr = ARRAY_SIZE(cs42l43_0_adr),
		.adr_d = cs42l43_0_adr,
	},
};

static const struct snd_soc_acpi_link_adr rpl_sdca_3_in_1[] = {
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

static const struct snd_soc_acpi_link_adr rpl_sdw_rt711_link0_rt1316_link12_rt714_link3[] = {
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

static const struct snd_soc_acpi_link_adr rpl_sdw_rt711_link2_rt1316_link01_rt714_link3[] = {
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

static const struct snd_soc_acpi_link_adr rpl_sdw_rt711_link2_rt1316_link01[] = {
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
	{}
};

static const struct snd_soc_acpi_link_adr rpl_sdw_rt711_link0_rt1316_link12[] = {
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
	{}
};

static const struct snd_soc_acpi_link_adr rpl_sdw_rt711_link0_rt1318_link12_rt714_link3[] = {
	{
		.mask = BIT(0),
		.num_adr = ARRAY_SIZE(rt711_sdca_0_adr),
		.adr_d = rt711_sdca_0_adr,
	},
	{
		.mask = BIT(1),
		.num_adr = ARRAY_SIZE(rt1318_1_group1_adr),
		.adr_d = rt1318_1_group1_adr,
	},
	{
		.mask = BIT(2),
		.num_adr = ARRAY_SIZE(rt1318_2_group1_adr),
		.adr_d = rt1318_2_group1_adr,
	},
	{
		.mask = BIT(3),
		.num_adr = ARRAY_SIZE(rt714_3_adr),
		.adr_d = rt714_3_adr,
	},
	{}
};

static const struct snd_soc_acpi_link_adr rpl_sdw_rt711_link0_rt1318_link12[] = {
	{
		.mask = BIT(0),
		.num_adr = ARRAY_SIZE(rt711_sdca_0_adr),
		.adr_d = rt711_sdca_0_adr,
	},
	{
		.mask = BIT(1),
		.num_adr = ARRAY_SIZE(rt1318_1_group1_adr),
		.adr_d = rt1318_1_group1_adr,
	},
	{
		.mask = BIT(2),
		.num_adr = ARRAY_SIZE(rt1318_2_group1_adr),
		.adr_d = rt1318_2_group1_adr,
	},
	{}
};

static const struct snd_soc_acpi_link_adr rpl_sdw_rt1316_link12_rt714_link0[] = {
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

static const struct snd_soc_acpi_link_adr rpl_sdca_rvp[] = {
	{
		.mask = BIT(0),
		.num_adr = ARRAY_SIZE(rt711_sdca_0_adr),
		.adr_d = rt711_sdca_0_adr,
	},
	{}
};

static const struct snd_soc_acpi_link_adr rplp_crb[] = {
	{
		.mask = BIT(2),
		.num_adr = ARRAY_SIZE(rt711_sdca_2_adr),
		.adr_d = rt711_sdca_2_adr,
	},
	{}
};

static const struct snd_soc_acpi_codecs rpl_rt5682_hp = {
	.num_codecs = 2,
	.codecs = {RT5682_ACPI_HID, RT5682S_ACPI_HID},
};

static const struct snd_soc_acpi_codecs rpl_essx_83x6 = {
	.num_codecs = 3,
	.codecs = { "ESSX8316", "ESSX8326", "ESSX8336"},
};

static const struct snd_soc_acpi_codecs rpl_max98357a_amp = {
	.num_codecs = 1,
	.codecs = {"MX98357A"}
};

static const struct snd_soc_acpi_codecs rpl_lt6911_hdmi = {
	.num_codecs = 1,
	.codecs = {"INTC10B0"}
};

struct snd_soc_acpi_mach snd_soc_acpi_intel_rpl_machines[] = {
	{
		.comp_ids = &rpl_rt5682_hp,
		.drv_name = "rpl_mx98357_rt5682",
		.machine_quirk = snd_soc_acpi_codec_list,
		.quirk_data = &rpl_max98357a_amp,
		.sof_tplg_filename = "sof-rpl-max98357a-rt5682.tplg",
	},
	{
		.comp_ids = &rpl_rt5682_hp,
		.drv_name = "rpl_rt5682_c1_h02",
		.machine_quirk = snd_soc_acpi_codec_list,
		.quirk_data = &rpl_lt6911_hdmi,
		.sof_tplg_filename = "sof-rpl-rt5682-ssp1-hdmi-ssp02.tplg",
	},
	{
		.comp_ids = &rpl_essx_83x6,
		.drv_name = "rpl_es83x6_c1_h02",
		.machine_quirk = snd_soc_acpi_codec_list,
		.quirk_data = &rpl_lt6911_hdmi,
		.sof_tplg_filename = "sof-rpl-es83x6-ssp1-hdmi-ssp02.tplg",
	},
	{
		.comp_ids = &rpl_essx_83x6,
		.drv_name = "sof-essx8336",
		.sof_tplg_filename = "sof-rpl-es83x6", /* the tplg suffix is added at run time */
		.tplg_quirk_mask = SND_SOC_ACPI_TPLG_INTEL_SSP_NUMBER |
					SND_SOC_ACPI_TPLG_INTEL_SSP_MSB |
					SND_SOC_ACPI_TPLG_INTEL_DMIC_NUMBER,
	},
	/* place boards for each headphone codec: sof driver will complete the
	 * tplg name and machine driver will detect the amp type
	 */
	{
		.id = CS42L42_ACPI_HID,
		.drv_name = "rpl_cs42l42_def",
		.sof_tplg_filename = "sof-rpl", /* the tplg suffix is added at run time */
		.tplg_quirk_mask = SND_SOC_ACPI_TPLG_INTEL_AMP_NAME |
					SND_SOC_ACPI_TPLG_INTEL_CODEC_NAME,
	},
	{
		.id = DA7219_ACPI_HID,
		.drv_name = "rpl_da7219_def",
		.sof_tplg_filename = "sof-rpl", /* the tplg suffix is added at run time */
		.tplg_quirk_mask = SND_SOC_ACPI_TPLG_INTEL_AMP_NAME |
					SND_SOC_ACPI_TPLG_INTEL_CODEC_NAME,
	},
	{
		.id = NAU8825_ACPI_HID,
		.drv_name = "rpl_nau8825_def",
		.sof_tplg_filename = "sof-rpl", /* the tplg suffix is added at run time */
		.tplg_quirk_mask = SND_SOC_ACPI_TPLG_INTEL_AMP_NAME |
					SND_SOC_ACPI_TPLG_INTEL_CODEC_NAME,
	},
	{
		.id = RT5650_ACPI_HID,
		.drv_name = "rpl_rt5682_def",
		.sof_tplg_filename = "sof-rpl", /* the tplg suffix is added at run time */
		.tplg_quirk_mask = SND_SOC_ACPI_TPLG_INTEL_AMP_NAME |
					SND_SOC_ACPI_TPLG_INTEL_CODEC_NAME,
	},
	{
		.comp_ids = &rpl_rt5682_hp,
		.drv_name = "rpl_rt5682_def",
		.sof_tplg_filename = "sof-rpl", /* the tplg suffix is added at run time */
		.tplg_quirk_mask = SND_SOC_ACPI_TPLG_INTEL_AMP_NAME |
					SND_SOC_ACPI_TPLG_INTEL_CODEC_NAME,
	},
	/* place amp-only boards in the end of table */
	{
		.id = "INTC10B0",
		.drv_name = "rpl_lt6911_hdmi_ssp",
		.sof_tplg_filename = "sof-rpl-nocodec-hdmi-ssp02.tplg"
	},
	{},
};
EXPORT_SYMBOL_GPL(snd_soc_acpi_intel_rpl_machines);

/* this table is used when there is no I2S codec present */
struct snd_soc_acpi_mach snd_soc_acpi_intel_rpl_sdw_machines[] = {
	{
		.link_mask = BIT(0),
		.links = rpl_cs42l43_l0,
		.drv_name = "sof_sdw",
		.sof_tplg_filename = "sof-rpl-cs42l43-l0.tplg",
	},
	{
		.link_mask = 0xF, /* 4 active links required */
		.links = rpl_sdca_3_in_1,
		.drv_name = "sof_sdw",
		.sof_tplg_filename = "sof-rpl-rt711-l0-rt1316-l13-rt714-l2.tplg",
	},
	{
		.link_mask = 0xF, /* 4 active links required */
		.links = rpl_sdw_rt711_link2_rt1316_link01_rt714_link3,
		.drv_name = "sof_sdw",
		.sof_tplg_filename = "sof-rpl-rt711-l2-rt1316-l01-rt714-l3.tplg",
	},
	{
		.link_mask = 0xF, /* 4 active links required */
		.links = rpl_sdw_rt711_link0_rt1316_link12_rt714_link3,
		.drv_name = "sof_sdw",
		.sof_tplg_filename = "sof-rpl-rt711-l0-rt1316-l12-rt714-l3.tplg",
	},
	{
		.link_mask = 0xF, /* 4 active links required */
		.links = rpl_sdw_rt711_link0_rt1318_link12_rt714_link3,
		.drv_name = "sof_sdw",
		.sof_tplg_filename = "sof-rpl-rt711-l0-rt1318-l12-rt714-l3.tplg",
	},
	{
		.link_mask = 0x7, /* rt711 on link0 & two rt1316s on link1 and link2 */
		.links = rpl_sdw_rt711_link0_rt1316_link12,
		.drv_name = "sof_sdw",
		.sof_tplg_filename = "sof-rpl-rt711-l0-rt1316-l12.tplg",
	},
	{
		.link_mask = 0x7, /* rt711 on link0 & two rt1318s on link1 and link2 */
		.links = rpl_sdw_rt711_link0_rt1318_link12,
		.drv_name = "sof_sdw",
		.sof_tplg_filename = "sof-rpl-rt711-l0-rt1318-l12.tplg",
	},
	{
		.link_mask = 0x7, /* rt714 on link0 & two rt1316s on link1 and link2 */
		.links = rpl_sdw_rt1316_link12_rt714_link0,
		.drv_name = "sof_sdw",
		.sof_tplg_filename = "sof-rpl-rt1316-l12-rt714-l0.tplg",
	},
	{
		.link_mask = 0x7, /* rt711 on link2 & two rt1316s on link0 and link1 */
		.links = rpl_sdw_rt711_link2_rt1316_link01,
		.drv_name = "sof_sdw",
		.sof_tplg_filename = "sof-rpl-rt711-l2-rt1316-l01.tplg",
	},
	{
		.link_mask = 0x1, /* link0 required */
		.links = rpl_rvp,
		.drv_name = "sof_sdw",
		.sof_tplg_filename = "sof-rpl-rt711-l0.tplg",
	},
	{
		.link_mask = 0x1, /* link0 required */
		.links = rpl_sdca_rvp,
		.drv_name = "sof_sdw",
		.sof_tplg_filename = "sof-rpl-rt711-l0.tplg",
	},
	{
		.link_mask = 0x4, /* link2 required */
		.links = rplp_crb,
		.drv_name = "sof_sdw",
		.sof_tplg_filename = "sof-rpl-rt711-l2.tplg",
	},
	{},
};
EXPORT_SYMBOL_GPL(snd_soc_acpi_intel_rpl_sdw_machines);
