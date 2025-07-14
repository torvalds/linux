// SPDX-License-Identifier: GPL-2.0-only
/*
 * soc-apci-intel-arl-match.c - tables and support for ARL ACPI enumeration.
 *
 * Copyright (c) 2023 Intel Corporation.
 */

#include <sound/soc-acpi.h>
#include <sound/soc-acpi-intel-match.h>
#include <sound/soc-acpi-intel-ssp-common.h>
#include "sof-function-topology-lib.h"

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

static const struct snd_soc_acpi_endpoint spk_2_endpoint = {
	.num = 0,
	.aggregated = 1,
	.group_position = 2,
	.group_id = 1,
};

static const struct snd_soc_acpi_endpoint spk_3_endpoint = {
	.num = 0,
	.aggregated = 1,
	.group_position = 3,
	.group_id = 1,
};

/*
 * RT722 is a multi-function codec, three endpoints are created for
 * its headset, amp and dmic functions.
 */
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

static const struct snd_soc_acpi_adr_device cs35l56_2_lr_adr[] = {
	{
		.adr = 0x00023001FA355601ull,
		.num_endpoints = 1,
		.endpoints = &spk_l_endpoint,
		.name_prefix = "AMP1"
	},
	{
		.adr = 0x00023101FA355601ull,
		.num_endpoints = 1,
		.endpoints = &spk_r_endpoint,
		.name_prefix = "AMP2"
	}
};

static const struct snd_soc_acpi_adr_device cs35l56_3_lr_adr[] = {
	{
		.adr = 0x00033001FA355601ull,
		.num_endpoints = 1,
		.endpoints = &spk_l_endpoint,
		.name_prefix = "AMP1"
	},
	{
		.adr = 0x00033401FA355601ull,
		.num_endpoints = 1,
		.endpoints = &spk_r_endpoint,
		.name_prefix = "AMP2"
	}
};

static const struct snd_soc_acpi_adr_device cs35l56_2_r_adr[] = {
	{
		.adr = 0x00023201FA355601ull,
		.num_endpoints = 1,
		.endpoints = &spk_r_endpoint,
		.name_prefix = "AMP3"
	},
	{
		.adr = 0x00023301FA355601ull,
		.num_endpoints = 1,
		.endpoints = &spk_3_endpoint,
		.name_prefix = "AMP4"
	}
};

static const struct snd_soc_acpi_adr_device cs35l56_3_l_adr[] = {
	{
		.adr = 0x00033001fa355601ull,
		.num_endpoints = 1,
		.endpoints = &spk_l_endpoint,
		.name_prefix = "AMP1"
	},
	{
		.adr = 0x00033101fa355601ull,
		.num_endpoints = 1,
		.endpoints = &spk_2_endpoint,
		.name_prefix = "AMP2"
	}
};

static const struct snd_soc_acpi_adr_device cs35l56_2_r1_adr[] = {
	{
		.adr = 0x00023101FA355601ull,
		.num_endpoints = 1,
		.endpoints = &spk_r_endpoint,
		.name_prefix = "AMP2"
	},
};

static const struct snd_soc_acpi_adr_device cs35l56_3_l3_adr[] = {
	{
		.adr = 0x00033301fa355601ull,
		.num_endpoints = 1,
		.endpoints = &spk_l_endpoint,
		.name_prefix = "AMP1"
	},
};

static const struct snd_soc_acpi_adr_device cs35l56_2_r3_adr[] = {
	{
		.adr = 0x00023301fa355601ull,
		.num_endpoints = 1,
		.endpoints = &spk_r_endpoint,
		.name_prefix = "AMP2"
	},
};

static const struct snd_soc_acpi_adr_device cs35l56_3_l1_adr[] = {
	{
		.adr = 0x00033101fa355601ull,
		.num_endpoints = 1,
		.endpoints = &spk_l_endpoint,
		.name_prefix = "AMP1"
	},
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

static const struct snd_soc_acpi_adr_device cs42l43_2_adr[] = {
	{
		.adr = 0x00023001FA424301ull,
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

static const struct snd_soc_acpi_adr_device rt711_sdca_0_adr[] = {
	{
		.adr = 0x000030025D071101ull,
		.num_endpoints = 1,
		.endpoints = &single_endpoint,
		.name_prefix = "rt711"
	}
};

static const struct snd_soc_acpi_adr_device rt722_0_single_adr[] = {
	{
		.adr = 0x000030025D072201ull,
		.num_endpoints = ARRAY_SIZE(rt722_endpoints),
		.endpoints = rt722_endpoints,
		.name_prefix = "rt722"
	}
};

static const struct snd_soc_acpi_adr_device rt1320_2_single_adr[] = {
	{
		.adr = 0x000230025D132001ull,
		.num_endpoints = 1,
		.endpoints = &single_endpoint,
		.name_prefix = "rt1320-1"
	}
};

static const struct snd_soc_acpi_link_adr arl_cs42l43_l0[] = {
	{
		.mask = BIT(0),
		.num_adr = ARRAY_SIZE(cs42l43_0_adr),
		.adr_d = cs42l43_0_adr,
	},
	{}
};

static const struct snd_soc_acpi_link_adr arl_cs42l43_l2[] = {
	{
		.mask = BIT(2),
		.num_adr = ARRAY_SIZE(cs42l43_2_adr),
		.adr_d = cs42l43_2_adr,
	},
	{}
};

static const struct snd_soc_acpi_link_adr arl_cs42l43_l2_cs35l56_l3[] = {
	{
		.mask = BIT(2),
		.num_adr = ARRAY_SIZE(cs42l43_2_adr),
		.adr_d = cs42l43_2_adr,
	},
	{
		.mask = BIT(3),
		.num_adr = ARRAY_SIZE(cs35l56_3_lr_adr),
		.adr_d = cs35l56_3_lr_adr,
	},
	{}
};

static const struct snd_soc_acpi_link_adr arl_cs42l43_l0_cs35l56_l2[] = {
	{
		.mask = BIT(0),
		.num_adr = ARRAY_SIZE(cs42l43_0_adr),
		.adr_d = cs42l43_0_adr,
	},
	{
		.mask = BIT(2),
		.num_adr = ARRAY_SIZE(cs35l56_2_lr_adr),
		.adr_d = cs35l56_2_lr_adr,
	},
	{}
};

static const struct snd_soc_acpi_link_adr arl_cs42l43_l0_cs35l56_l23[] = {
	{
		.mask = BIT(0),
		.num_adr = ARRAY_SIZE(cs42l43_0_adr),
		.adr_d = cs42l43_0_adr,
	},
	{
		.mask = BIT(2),
		.num_adr = ARRAY_SIZE(cs35l56_2_r_adr),
		.adr_d = cs35l56_2_r_adr,
	},
	{
		.mask = BIT(3),
		.num_adr = ARRAY_SIZE(cs35l56_3_l_adr),
		.adr_d = cs35l56_3_l_adr,
	},
	{}
};

static const struct snd_soc_acpi_link_adr arl_cs42l43_l0_cs35l56_2_l23[] = {
	{
		.mask = BIT(0),
		.num_adr = ARRAY_SIZE(cs42l43_0_adr),
		.adr_d = cs42l43_0_adr,
	},
	{
		.mask = BIT(2),
		.num_adr = ARRAY_SIZE(cs35l56_2_r1_adr),
		.adr_d = cs35l56_2_r1_adr,
	},
	{
		.mask = BIT(3),
		.num_adr = ARRAY_SIZE(cs35l56_3_l3_adr),
		.adr_d = cs35l56_3_l3_adr,
	},
	{}
};

static const struct snd_soc_acpi_link_adr arl_cs42l43_l0_cs35l56_3_l23[] = {
	{
		.mask = BIT(0),
		.num_adr = ARRAY_SIZE(cs42l43_0_adr),
		.adr_d = cs42l43_0_adr,
	},
	{
		.mask = BIT(2),
		.num_adr = ARRAY_SIZE(cs35l56_2_r3_adr),
		.adr_d = cs35l56_2_r3_adr,
	},
	{
		.mask = BIT(3),
		.num_adr = ARRAY_SIZE(cs35l56_3_l1_adr),
		.adr_d = cs35l56_3_l1_adr,
	},
	{}
};

static const struct snd_soc_acpi_link_adr arl_rvp[] = {
	{
		.mask = BIT(0),
		.num_adr = ARRAY_SIZE(rt711_0_adr),
		.adr_d = rt711_0_adr,
	},
	{}
};

static const struct snd_soc_acpi_link_adr arl_sdca_rvp[] = {
	{
		.mask = BIT(0),
		.num_adr = ARRAY_SIZE(rt711_sdca_0_adr),
		.adr_d = rt711_sdca_0_adr,
	},
	{}
};

static const struct snd_soc_acpi_link_adr arl_rt722_l0_rt1320_l2[] = {
	{
		.mask = BIT(0),
		.num_adr = ARRAY_SIZE(rt722_0_single_adr),
		.adr_d = rt722_0_single_adr,
	},
	{
		.mask = BIT(2),
		.num_adr = ARRAY_SIZE(rt1320_2_single_adr),
		.adr_d = rt1320_2_single_adr,
	},
	{}
};

static const struct snd_soc_acpi_codecs arl_essx_83x6 = {
	.num_codecs = 3,
	.codecs = { "ESSX8316", "ESSX8326", "ESSX8336"},
};

static const struct snd_soc_acpi_codecs arl_rt5682_hp = {
	.num_codecs = 2,
	.codecs = {RT5682_ACPI_HID, RT5682S_ACPI_HID},
};

static const struct snd_soc_acpi_codecs arl_lt6911_hdmi = {
	.num_codecs = 1,
	.codecs = {"INTC10B0"}
};

struct snd_soc_acpi_mach snd_soc_acpi_intel_arl_machines[] = {
	{
		.comp_ids = &arl_essx_83x6,
		.drv_name = "arl_es83x6_c1_h02",
		.machine_quirk = snd_soc_acpi_codec_list,
		.quirk_data = &arl_lt6911_hdmi,
		.sof_tplg_filename = "sof-arl-es83x6-ssp1-hdmi-ssp02.tplg",
	},
	{
		.comp_ids = &arl_essx_83x6,
		.drv_name = "sof-essx8336",
		.sof_tplg_filename = "sof-arl-es8336", /* the tplg suffix is added at run time */
		.tplg_quirk_mask = SND_SOC_ACPI_TPLG_INTEL_SSP_NUMBER |
			SND_SOC_ACPI_TPLG_INTEL_SSP_MSB |
			SND_SOC_ACPI_TPLG_INTEL_DMIC_NUMBER,
	},
	{
		.comp_ids = &arl_rt5682_hp,
		.drv_name = "arl_rt5682_c1_h02",
		.machine_quirk = snd_soc_acpi_codec_list,
		.quirk_data = &arl_lt6911_hdmi,
		.sof_tplg_filename = "sof-arl-rt5682-ssp1-hdmi-ssp02.tplg",
	},
	/* place amp-only boards in the end of table */
	{
		.id = "INTC10B0",
		.drv_name = "arl_lt6911_hdmi_ssp",
		.sof_tplg_filename = "sof-arl-hdmi-ssp02.tplg",
	},
	{},
};
EXPORT_SYMBOL_GPL(snd_soc_acpi_intel_arl_machines);

/* this table is used when there is no I2S codec present */
struct snd_soc_acpi_mach snd_soc_acpi_intel_arl_sdw_machines[] = {
	{
		.link_mask = BIT(0) | BIT(2) | BIT(3),
		.links = arl_cs42l43_l0_cs35l56_l23,
		.drv_name = "sof_sdw",
		.sof_tplg_filename = "sof-arl-cs42l43-l0-cs35l56-l23.tplg",
		.get_function_tplg_files = sof_sdw_get_tplg_files,
	},
	{
		.link_mask = BIT(0) | BIT(2) | BIT(3),
		.links = arl_cs42l43_l0_cs35l56_2_l23,
		.drv_name = "sof_sdw",
		.sof_tplg_filename = "sof-arl-cs42l43-l0-cs35l56-l23.tplg",
		.get_function_tplg_files = sof_sdw_get_tplg_files,
	},
	{
		.link_mask = BIT(0) | BIT(2) | BIT(3),
		.links = arl_cs42l43_l0_cs35l56_3_l23,
		.drv_name = "sof_sdw",
		.sof_tplg_filename = "sof-arl-cs42l43-l0-cs35l56-l23.tplg",
		.get_function_tplg_files = sof_sdw_get_tplg_files,
	},
	{
		.link_mask = BIT(0) | BIT(2),
		.links = arl_cs42l43_l0_cs35l56_l2,
		.drv_name = "sof_sdw",
		.sof_tplg_filename = "sof-arl-cs42l43-l0-cs35l56-l2.tplg",
		.get_function_tplg_files = sof_sdw_get_tplg_files,
	},
	{
		.link_mask = BIT(0),
		.links = arl_cs42l43_l0,
		.drv_name = "sof_sdw",
		.sof_tplg_filename = "sof-arl-cs42l43-l0.tplg",
		.get_function_tplg_files = sof_sdw_get_tplg_files,
	},
	{
		.link_mask = BIT(2) | BIT(3),
		.links = arl_cs42l43_l2_cs35l56_l3,
		.drv_name = "sof_sdw",
		.sof_tplg_filename = "sof-arl-cs42l43-l2-cs35l56-l3.tplg",
		.get_function_tplg_files = sof_sdw_get_tplg_files,
	},
	{
		.link_mask = BIT(2),
		.links = arl_cs42l43_l2,
		.drv_name = "sof_sdw",
		.sof_tplg_filename = "sof-arl-cs42l43-l2.tplg",
		.get_function_tplg_files = sof_sdw_get_tplg_files,
	},
	{
		.link_mask = 0x1, /* link0 required */
		.links = arl_rvp,
		.drv_name = "sof_sdw",
		.sof_tplg_filename = "sof-arl-rt711.tplg",
	},
	{
		.link_mask = 0x1, /* link0 required */
		.links = arl_sdca_rvp,
		.drv_name = "sof_sdw",
		.sof_tplg_filename = "sof-arl-rt711-l0.tplg",
	},
	{
		.link_mask = BIT(0) | BIT(2),
		.links = arl_rt722_l0_rt1320_l2,
		.drv_name = "sof_sdw",
		.sof_tplg_filename = "sof-arl-rt722-l0_rt1320-l2.tplg",
		.get_function_tplg_files = sof_sdw_get_tplg_files,
	},
	{},
};
EXPORT_SYMBOL_GPL(snd_soc_acpi_intel_arl_sdw_machines);
