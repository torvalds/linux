// SPDX-License-Identifier: GPL-2.0-only
/*
 * soc-acpi-intel-ptl-match.c - tables and support for PTL ACPI enumeration.
 *
 * Copyright (c) 2024, Intel Corporation.
 *
 */

#include <sound/soc-acpi.h>
#include <sound/soc-acpi-intel-match.h>
#include "soc-acpi-intel-sdw-mockup-match.h"

struct snd_soc_acpi_mach snd_soc_acpi_intel_ptl_machines[] = {
	{},
};
EXPORT_SYMBOL_GPL(snd_soc_acpi_intel_ptl_machines);

static const struct snd_soc_acpi_endpoint single_endpoint = {
	.num = 0,
	.aggregated = 0,
	.group_position = 0,
	.group_id = 0,
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
		.adr = 0x000030025d072201ull,
		.num_endpoints = ARRAY_SIZE(rt722_endpoints),
		.endpoints = rt722_endpoints,
		.name_prefix = "rt722"
	}
};

static const struct snd_soc_acpi_adr_device rt722_3_single_adr[] = {
	{
		.adr = 0x000330025d072201ull,
		.num_endpoints = ARRAY_SIZE(rt722_endpoints),
		.endpoints = rt722_endpoints,
		.name_prefix = "rt722"
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
		.link_mask = BIT(0),
		.links = ptl_rt722_only,
		.drv_name = "sof_sdw",
		.sof_tplg_filename = "sof-ptl-rt722.tplg",
	},
	{
		.link_mask = BIT(3),
		.links = ptl_rt722_l3,
		.drv_name = "sof_sdw",
		.sof_tplg_filename = "sof-ptl-rt722.tplg",
	},
	{},
};
EXPORT_SYMBOL_GPL(snd_soc_acpi_intel_ptl_sdw_machines);
