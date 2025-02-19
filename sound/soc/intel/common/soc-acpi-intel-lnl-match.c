// SPDX-License-Identifier: GPL-2.0-only
/*
 * soc-acpi-intel-lnl-match.c - tables and support for LNL ACPI enumeration.
 *
 * Copyright (c) 2023, Intel Corporation
 *
 */

#include <sound/soc-acpi.h>
#include <sound/soc-acpi-intel-match.h>
#include "soc-acpi-intel-sdca-quirks.h"
#include "soc-acpi-intel-sdw-mockup-match.h"

struct snd_soc_acpi_mach snd_soc_acpi_intel_lnl_machines[] = {
	{},
};
EXPORT_SYMBOL_GPL(snd_soc_acpi_intel_lnl_machines);

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

static const struct snd_soc_acpi_endpoint rt712_endpoints[] = {
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

static const struct snd_soc_acpi_adr_device cs35l56_2_l_adr[] = {
	{
		.adr = 0x00023001FA355601ull,
		.num_endpoints = 1,
		.endpoints = &spk_l_endpoint,
		.name_prefix = "AMP1"
	},
	{
		.adr = 0x00023101FA355601ull,
		.num_endpoints = 1,
		.endpoints = &spk_2_endpoint,
		.name_prefix = "AMP2"
	}
};

static const struct snd_soc_acpi_adr_device cs35l56_3_r_adr[] = {
	{
		.adr = 0x00033201fa355601ull,
		.num_endpoints = 1,
		.endpoints = &spk_r_endpoint,
		.name_prefix = "AMP3"
	},
	{
		.adr = 0x00033301fa355601ull,
		.num_endpoints = 1,
		.endpoints = &spk_3_endpoint,
		.name_prefix = "AMP4"
	}
};

static const struct snd_soc_acpi_adr_device cs35l56_3_lr_adr[] = {
	{
		.adr = 0x00033001fa355601ull,
		.num_endpoints = 1,
		.endpoints = &spk_l_endpoint,
		.name_prefix = "AMP1"
	},
	{
		.adr = 0x00033101fa355601ull,
		.num_endpoints = 1,
		.endpoints = &spk_r_endpoint,
		.name_prefix = "AMP2"
	}
};

static const struct snd_soc_acpi_adr_device cs42l43_0_adr[] = {
	{
		.adr = 0x00003001FA424301ull,
		.num_endpoints = ARRAY_SIZE(cs42l43_endpoints),
		.endpoints = cs42l43_endpoints,
		.name_prefix = "cs42l43"
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

static const struct snd_soc_acpi_adr_device rt712_2_single_adr[] = {
	{
		.adr = 0x000230025D071201ull,
		.num_endpoints = ARRAY_SIZE(rt712_endpoints),
		.endpoints = rt712_endpoints,
		.name_prefix = "rt712"
	}
};

static const struct snd_soc_acpi_adr_device rt1712_3_single_adr[] = {
	{
		.adr = 0x000330025D171201ull,
		.num_endpoints = 1,
		.endpoints = &single_endpoint,
		.name_prefix = "rt712-dmic"
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

static const struct snd_soc_acpi_adr_device rt722_0_single_adr[] = {
	{
		.adr = 0x000030025d072201ull,
		.num_endpoints = ARRAY_SIZE(rt722_endpoints),
		.endpoints = rt722_endpoints,
		.name_prefix = "rt722"
	}
};

static const struct snd_soc_acpi_adr_device rt1316_2_group1_adr[] = {
	{
		.adr = 0x000230025D131601ull,
		.num_endpoints = 1,
		.endpoints = &spk_l_endpoint,
		.name_prefix = "rt1316-1"
	}
};

static const struct snd_soc_acpi_adr_device rt1316_3_group1_adr[] = {
	{
		.adr = 0x000331025D131601ull,
		.num_endpoints = 1,
		.endpoints = &spk_r_endpoint,
		.name_prefix = "rt1316-2"
	}
};

static const struct snd_soc_acpi_adr_device rt1318_1_adr[] = {
	{
		.adr = 0x000133025D131801ull,
		.num_endpoints = 1,
		.endpoints = &single_endpoint,
		.name_prefix = "rt1318-1"
	}
};

static const struct snd_soc_acpi_adr_device rt1318_1_group1_adr[] = {
	{
		.adr = 0x000130025D131801ull,
		.num_endpoints = 1,
		.endpoints = &spk_l_endpoint,
		.name_prefix = "rt1318-1"
	}
};

static const struct snd_soc_acpi_adr_device rt1318_2_group1_adr[] = {
	{
		.adr = 0x000232025D131801ull,
		.num_endpoints = 1,
		.endpoints = &spk_r_endpoint,
		.name_prefix = "rt1318-2"
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

static const struct snd_soc_acpi_adr_device rt713_0_adr[] = {
	{
		.adr = 0x000031025D071301ull,
		.num_endpoints = 1,
		.endpoints = &single_endpoint,
		.name_prefix = "rt713"
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

static const struct snd_soc_acpi_adr_device rt714_0_adr[] = {
	{
		.adr = 0x000030025D071401ull,
		.num_endpoints = 1,
		.endpoints = &single_endpoint,
		.name_prefix = "rt714"
	}
};

static const struct snd_soc_acpi_adr_device rt714_1_adr[] = {
	{
		.adr = 0x000130025D071401ull,
		.num_endpoints = 1,
		.endpoints = &single_endpoint,
		.name_prefix = "rt714"
	}
};

static const struct snd_soc_acpi_link_adr lnl_cs42l43_l0[] = {
	{
		.mask = BIT(0),
		.num_adr = ARRAY_SIZE(cs42l43_0_adr),
		.adr_d = cs42l43_0_adr,
	},
	{}
};

static const struct snd_soc_acpi_link_adr lnl_cs42l43_l0_cs35l56_l3[] = {
	{
		.mask = BIT(0),
		.num_adr = ARRAY_SIZE(cs42l43_0_adr),
		.adr_d = cs42l43_0_adr,
	},
	{
		.mask = BIT(3),
		.num_adr = ARRAY_SIZE(cs35l56_3_lr_adr),
		.adr_d = cs35l56_3_lr_adr,
	},
	{}
};

static const struct snd_soc_acpi_link_adr lnl_cs42l43_l0_cs35l56_l23[] = {
	{
		.mask = BIT(0),
		.num_adr = ARRAY_SIZE(cs42l43_0_adr),
		.adr_d = cs42l43_0_adr,
	},
	{
		.mask = BIT(2),
		.num_adr = ARRAY_SIZE(cs35l56_2_l_adr),
		.adr_d = cs35l56_2_l_adr,
	},
	{
		.mask = BIT(3),
		.num_adr = ARRAY_SIZE(cs35l56_3_r_adr),
		.adr_d = cs35l56_3_r_adr,
	},
	{}
};

static const struct snd_soc_acpi_link_adr lnl_rvp[] = {
	{
		.mask = BIT(0),
		.num_adr = ARRAY_SIZE(rt711_sdca_0_adr),
		.adr_d = rt711_sdca_0_adr,
	},
	{}
};

static const struct snd_soc_acpi_link_adr lnl_712_only[] = {
	{
		.mask = BIT(2),
		.num_adr = ARRAY_SIZE(rt712_2_single_adr),
		.adr_d = rt712_2_single_adr,
	},
	{
		.mask = BIT(3),
		.num_adr = ARRAY_SIZE(rt1712_3_single_adr),
		.adr_d = rt1712_3_single_adr,
	},
	{}
};

static const struct snd_soc_acpi_link_adr lnl_rt722_only[] = {
	{
		.mask = BIT(0),
		.num_adr = ARRAY_SIZE(rt722_0_single_adr),
		.adr_d = rt722_0_single_adr,
	},
	{}
};

static const struct snd_soc_acpi_link_adr lnl_3_in_1_sdca[] = {
	{
		.mask = BIT(0),
		.num_adr = ARRAY_SIZE(rt711_sdca_0_adr),
		.adr_d = rt711_sdca_0_adr,
	},
	{
		.mask = BIT(2),
		.num_adr = ARRAY_SIZE(rt1316_2_group1_adr),
		.adr_d = rt1316_2_group1_adr,
	},
	{
		.mask = BIT(3),
		.num_adr = ARRAY_SIZE(rt1316_3_group1_adr),
		.adr_d = rt1316_3_group1_adr,
	},
	{
		.mask = BIT(1),
		.num_adr = ARRAY_SIZE(rt714_1_adr),
		.adr_d = rt714_1_adr,
	},
	{}
};

static const struct snd_soc_acpi_link_adr lnl_sdw_rt1318_l12_rt714_l0[] = {
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
		.mask = BIT(0),
		.num_adr = ARRAY_SIZE(rt714_0_adr),
		.adr_d = rt714_0_adr,
	},
	{}
};

static const struct snd_soc_acpi_link_adr lnl_sdw_rt713_l0_rt1318_l1[] = {
	{
		.mask = BIT(0),
		.num_adr = ARRAY_SIZE(rt713_0_adr),
		.adr_d = rt713_0_adr,
	},
	{
		.mask = BIT(1),
		.num_adr = ARRAY_SIZE(rt1318_1_adr),
		.adr_d = rt1318_1_adr,
	},
	{}
};

static const struct snd_soc_acpi_link_adr lnl_sdw_rt713_vb_l2_rt1320_l13[] = {
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

static const struct snd_soc_acpi_link_adr lnl_sdw_rt712_vb_l2_rt1320_l1[] = {
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
/* this table is used when there is no I2S codec present */
struct snd_soc_acpi_mach snd_soc_acpi_intel_lnl_sdw_machines[] = {
	/* mockup tests need to be first */
	{
		.link_mask = GENMASK(3, 0),
		.links = sdw_mockup_headset_2amps_mic,
		.drv_name = "sof_sdw",
		.sof_tplg_filename = "sof-lnl-rt711-rt1308-rt715.tplg",
	},
	{
		.link_mask = BIT(0) | BIT(1) | BIT(3),
		.links = sdw_mockup_headset_1amp_mic,
		.drv_name = "sof_sdw",
		.sof_tplg_filename = "sof-lnl-rt711-rt1308-mono-rt715.tplg",
	},
	{
		.link_mask = GENMASK(2, 0),
		.links = sdw_mockup_mic_headset_1amp,
		.drv_name = "sof_sdw",
		.sof_tplg_filename = "sof-lnl-rt715-rt711-rt1308-mono.tplg",
	},
	{
		.link_mask = GENMASK(3, 0),
		.links = lnl_3_in_1_sdca,
		.drv_name = "sof_sdw",
		.sof_tplg_filename = "sof-lnl-rt711-l0-rt1316-l23-rt714-l1.tplg",
	},
	{
		.link_mask = BIT(0) | BIT(2) | BIT(3),
		.links = lnl_cs42l43_l0_cs35l56_l23,
		.drv_name = "sof_sdw",
		.sof_tplg_filename = "sof-lnl-cs42l43-l0-cs35l56-l23.tplg",
	},
	{
		.link_mask = BIT(0) | BIT(3),
		.links = lnl_cs42l43_l0_cs35l56_l3,
		.drv_name = "sof_sdw",
		.sof_tplg_filename = "sof-lnl-cs42l43-l0-cs35l56-l3.tplg",
	},
	{
		.link_mask = BIT(0),
		.links = lnl_cs42l43_l0,
		.drv_name = "sof_sdw",
		.sof_tplg_filename = "sof-lnl-cs42l43-l0.tplg",
	},
	{
		.link_mask = BIT(0),
		.links = lnl_rvp,
		.drv_name = "sof_sdw",
		.sof_tplg_filename = "sof-lnl-rt711.tplg",
	},
	{
		.link_mask = BIT(2) | BIT(3),
		.links = lnl_712_only,
		.drv_name = "sof_sdw",
		.sof_tplg_filename = "sof-lnl-rt712-l2-rt1712-l3.tplg",
	},
	{
		.link_mask = BIT(0),
		.links = lnl_rt722_only,
		.drv_name = "sof_sdw",
		.sof_tplg_filename = "sof-lnl-rt722-l0.tplg",
	},
	{
		.link_mask = GENMASK(2, 0),
		.links = lnl_sdw_rt1318_l12_rt714_l0,
		.drv_name = "sof_sdw",
		.sof_tplg_filename = "sof-lnl-rt1318-l12-rt714-l0.tplg"
	},
	{
		.link_mask = BIT(0) | BIT(1),
		.links = lnl_sdw_rt713_l0_rt1318_l1,
		.drv_name = "sof_sdw",
		.sof_tplg_filename = "sof-lnl-rt713-l0-rt1318-l1.tplg"
	},
	{
		.link_mask = BIT(1) | BIT(2),
		.links = lnl_sdw_rt712_vb_l2_rt1320_l1,
		.drv_name = "sof_sdw",
		.machine_check = snd_soc_acpi_intel_sdca_is_device_rt712_vb,
		.sof_tplg_filename = "sof-lnl-rt712-l2-rt1320-l1.tplg"
	},
	{
		.link_mask = BIT(1) | BIT(2) | BIT(3),
		.links = lnl_sdw_rt713_vb_l2_rt1320_l13,
		.drv_name = "sof_sdw",
		.machine_check = snd_soc_acpi_intel_sdca_is_device_rt712_vb,
		.sof_tplg_filename = "sof-lnl-rt713-l2-rt1320-l13.tplg"
	},
	{},
};
EXPORT_SYMBOL_GPL(snd_soc_acpi_intel_lnl_sdw_machines);
