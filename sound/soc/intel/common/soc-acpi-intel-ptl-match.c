// SPDX-License-Identifier: GPL-2.0-only
/*
 * soc-acpi-intel-ptl-match.c - tables and support for PTL ACPI enumeration.
 *
 * Copyright (c) 2024, Intel Corporation.
 *
 */

#include <sound/soc-acpi.h>
#include <sound/soc-acpi-intel-match.h>
#include "soc-acpi-intel-sdca-quirks.h"
#include "soc-acpi-intel-sdw-mockup-match.h"
#include <sound/soc-acpi-intel-ssp-common.h>

static const struct snd_soc_acpi_codecs ptl_rt5682_rt5682s_hp = {
	.num_codecs = 2,
	.codecs = {RT5682_ACPI_HID, RT5682S_ACPI_HID},
};

struct snd_soc_acpi_mach snd_soc_acpi_intel_ptl_machines[] = {
	{
		.comp_ids = &ptl_rt5682_rt5682s_hp,
		.drv_name = "ptl_rt5682_def",
		.sof_tplg_filename = "sof-ptl", /* the tplg suffix is added at run time */
		.tplg_quirk_mask = SND_SOC_ACPI_TPLG_INTEL_AMP_NAME |
					SND_SOC_ACPI_TPLG_INTEL_CODEC_NAME,
	},
	{},
};
EXPORT_SYMBOL_GPL(snd_soc_acpi_intel_ptl_machines);

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

/*
 * Multi-function codecs with three endpoints created for
 * headset, amp and dmic functions.
 */
static const struct snd_soc_acpi_endpoint rt_mf_endpoints[] = {
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

static const struct snd_soc_acpi_endpoint jack_dmic_endpoints[] = {
	/* Jack Endpoint */
	{
		.num = 0,
		.aggregated = 0,
		.group_position = 0,
		.group_id = 0,
	},
	/* DMIC Endpoint */
	{
		.num = 1,
		.aggregated = 0,
		.group_position = 0,
		.group_id = 0,
	},
};

static const struct snd_soc_acpi_endpoint jack_amp_g1_dmic_endpoints_endpoints[] = {
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

static const struct snd_soc_acpi_adr_device rt711_sdca_0_adr[] = {
	{
		.adr = 0x000030025D071101ull,
		.num_endpoints = 1,
		.endpoints = &single_endpoint,
		.name_prefix = "rt711"
	}
};

static const struct snd_soc_acpi_adr_device rt712_vb_2_group1_adr[] = {
	{
		.adr = 0x000230025D071201ull,
		.num_endpoints = ARRAY_SIZE(jack_amp_g1_dmic_endpoints_endpoints),
		.endpoints = jack_amp_g1_dmic_endpoints_endpoints,
		.name_prefix = "rt712"
	}
};

static const struct snd_soc_acpi_adr_device rt713_vb_2_adr[] = {
	{
		.adr = 0x000230025d071301ull,
		.num_endpoints = ARRAY_SIZE(jack_dmic_endpoints),
		.endpoints = jack_dmic_endpoints,
		.name_prefix = "rt713"
	}
};

static const struct snd_soc_acpi_adr_device rt721_3_single_adr[] = {
	{
		.adr = 0x000330025d072101ull,
		.num_endpoints = ARRAY_SIZE(rt_mf_endpoints),
		.endpoints = rt_mf_endpoints,
		.name_prefix = "rt721"
	}
};

static const struct snd_soc_acpi_link_adr ptl_rt721_l3[] = {
	{
		.mask = BIT(3),
		.num_adr = ARRAY_SIZE(rt721_3_single_adr),
		.adr_d = rt721_3_single_adr,
	},
	{},
};

static const struct snd_soc_acpi_adr_device rt722_0_single_adr[] = {
	{
		.adr = 0x000030025d072201ull,
		.num_endpoints = ARRAY_SIZE(rt_mf_endpoints),
		.endpoints = rt_mf_endpoints,
		.name_prefix = "rt722"
	}
};

static const struct snd_soc_acpi_adr_device rt722_1_single_adr[] = {
	{
		.adr = 0x000130025d072201ull,
		.num_endpoints = ARRAY_SIZE(rt_mf_endpoints),
		.endpoints = rt_mf_endpoints,
		.name_prefix = "rt722"
	}
};

static const struct snd_soc_acpi_adr_device rt722_3_single_adr[] = {
	{
		.adr = 0x000330025d072201ull,
		.num_endpoints = ARRAY_SIZE(rt_mf_endpoints),
		.endpoints = rt_mf_endpoints,
		.name_prefix = "rt722"
	}
};

static const struct snd_soc_acpi_adr_device rt1320_1_group1_adr[] = {
	{
		.adr = 0x000130025D132001ull,
		.num_endpoints = 1,
		.endpoints = &spk_r_endpoint,
		.name_prefix = "rt1320-1"
	}
};

static const struct snd_soc_acpi_adr_device rt1320_1_group2_adr[] = {
	{
		.adr = 0x000130025D132001ull,
		.num_endpoints = 1,
		.endpoints = &spk_l_endpoint,
		.name_prefix = "rt1320-1"
	}
};

static const struct snd_soc_acpi_adr_device rt1320_3_group2_adr[] = {
	{
		.adr = 0x000330025D132001ull,
		.num_endpoints = 1,
		.endpoints = &spk_r_endpoint,
		.name_prefix = "rt1320-2"
	}
};

static const struct snd_soc_acpi_link_adr ptl_rt722_only[] = {
	{
		.mask = BIT(0),
		.num_adr = ARRAY_SIZE(rt722_0_single_adr),
		.adr_d = rt722_0_single_adr,
	},
	{}
};

static const struct snd_soc_acpi_link_adr ptl_rt722_l1[] = {
	{
		.mask = BIT(1),
		.num_adr = ARRAY_SIZE(rt722_1_single_adr),
		.adr_d = rt722_1_single_adr,
	},
	{}
};

static const struct snd_soc_acpi_link_adr ptl_rt722_l3[] = {
	{
		.mask = BIT(3),
		.num_adr = ARRAY_SIZE(rt722_3_single_adr),
		.adr_d = rt722_3_single_adr,
	},
	{}
};

static const struct snd_soc_acpi_link_adr ptl_rvp[] = {
	{
		.mask = BIT(0),
		.num_adr = ARRAY_SIZE(rt711_sdca_0_adr),
		.adr_d = rt711_sdca_0_adr,
	},
	{}
};

static const struct snd_soc_acpi_link_adr ptl_sdw_rt713_vb_l2_rt1320_l13[] = {
	{
		.mask = BIT(2),
		.num_adr = ARRAY_SIZE(rt713_vb_2_adr),
		.adr_d = rt713_vb_2_adr,
	},
	{
		.mask = BIT(1),
		.num_adr = ARRAY_SIZE(rt1320_1_group2_adr),
		.adr_d = rt1320_1_group2_adr,
	},
	{
		.mask = BIT(3),
		.num_adr = ARRAY_SIZE(rt1320_3_group2_adr),
		.adr_d = rt1320_3_group2_adr,
	},
	{}
};

static const struct snd_soc_acpi_link_adr ptl_sdw_rt712_vb_l2_rt1320_l1[] = {
	{
		.mask = BIT(2),
		.num_adr = ARRAY_SIZE(rt712_vb_2_group1_adr),
		.adr_d = rt712_vb_2_group1_adr,
	},
	{
		.mask = BIT(1),
		.num_adr = ARRAY_SIZE(rt1320_1_group1_adr),
		.adr_d = rt1320_1_group1_adr,
	},
	{}
};

/* this table is used when there is no I2S codec present */
struct snd_soc_acpi_mach snd_soc_acpi_intel_ptl_sdw_machines[] = {
	/* mockup tests need to be first */
	{
		.link_mask = GENMASK(3, 0),
		.links = sdw_mockup_headset_2amps_mic,
		.drv_name = "sof_sdw",
		.sof_tplg_filename = "sof-ptl-rt711-rt1308-rt715.tplg",
	},
	{
		.link_mask = BIT(0) | BIT(1) | BIT(3),
		.links = sdw_mockup_headset_1amp_mic,
		.drv_name = "sof_sdw",
		.sof_tplg_filename = "sof-ptl-rt711-rt1308-mono-rt715.tplg",
	},
	{
		.link_mask = GENMASK(2, 0),
		.links = sdw_mockup_mic_headset_1amp,
		.drv_name = "sof_sdw",
		.sof_tplg_filename = "sof-ptl-rt715-rt711-rt1308-mono.tplg",
	},
	{
		.link_mask = BIT(0),
		.links = ptl_rvp,
		.drv_name = "sof_sdw",
		.sof_tplg_filename = "sof-ptl-rt711.tplg",
	},
	{
		.link_mask = BIT(3),
		.links = ptl_rt721_l3,
		.drv_name = "sof_sdw",
		.sof_tplg_filename = "sof-ptl-rt721.tplg",
	},
	{
		.link_mask = BIT(0),
		.links = ptl_rt722_only,
		.drv_name = "sof_sdw",
		.sof_tplg_filename = "sof-ptl-rt722.tplg",
	},
	{
		.link_mask = BIT(1),
		.links = ptl_rt722_l1,
		.drv_name = "sof_sdw",
		.sof_tplg_filename = "sof-ptl-rt722.tplg",
	},
	{
		.link_mask = BIT(3),
		.links = ptl_rt722_l3,
		.drv_name = "sof_sdw",
		.sof_tplg_filename = "sof-ptl-rt722.tplg",
	},
	{
		.link_mask = BIT(1) | BIT(2),
		.links = ptl_sdw_rt712_vb_l2_rt1320_l1,
		.drv_name = "sof_sdw",
		.machine_check = snd_soc_acpi_intel_sdca_is_device_rt712_vb,
		.sof_tplg_filename = "sof-ptl-rt712-l2-rt1320-l1.tplg"
	},
	{
		.link_mask = BIT(1) | BIT(2) | BIT(3),
		.links = ptl_sdw_rt713_vb_l2_rt1320_l13,
		.drv_name = "sof_sdw",
		.machine_check = snd_soc_acpi_intel_sdca_is_device_rt712_vb,
		.sof_tplg_filename = "sof-ptl-rt713-l2-rt1320-l13.tplg"
	},
	{},
};
EXPORT_SYMBOL_GPL(snd_soc_acpi_intel_ptl_sdw_machines);
