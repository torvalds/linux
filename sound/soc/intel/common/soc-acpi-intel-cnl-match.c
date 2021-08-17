// SPDX-License-Identifier: GPL-2.0-only
/*
 * soc-acpi-intel-cnl-match.c - tables and support for CNL ACPI enumeration.
 *
 * Copyright (c) 2018, Intel Corporation.
 *
 */

#include <sound/soc-acpi.h>
#include <sound/soc-acpi-intel-match.h>
#include "../skylake/skl.h"

static struct skl_machine_pdata cnl_pdata = {
	.use_tplg_pcm = true,
};

struct snd_soc_acpi_mach snd_soc_acpi_intel_cnl_machines[] = {
	{
		.id = "INT34C2",
		.drv_name = "cnl_rt274",
		.fw_filename = "intel/dsp_fw_cnl.bin",
		.pdata = &cnl_pdata,
		.sof_fw_filename = "sof-cnl.ri",
		.sof_tplg_filename = "sof-cnl-rt274.tplg",
	},
	{},
};
EXPORT_SYMBOL_GPL(snd_soc_acpi_intel_cnl_machines);

static const struct snd_soc_acpi_endpoint single_endpoint = {
	.num = 0,
	.aggregated = 0,
	.group_position = 0,
	.group_id = 0,
};

static const struct snd_soc_acpi_adr_device rt5682_2_adr[] = {
	{
		.adr = 0x000220025D568200ull,
		.num_endpoints = 1,
		.endpoints = &single_endpoint,
		.name_prefix = "rt5682"
	}
};

static const struct snd_soc_acpi_link_adr up_extreme_rt5682_2[] = {
	{
		.mask = BIT(2),
		.num_adr = ARRAY_SIZE(rt5682_2_adr),
		.adr_d = rt5682_2_adr,
	},
	{}
};

struct snd_soc_acpi_mach snd_soc_acpi_intel_cnl_sdw_machines[] = {
	{
		.link_mask = BIT(2),
		.links = up_extreme_rt5682_2,
		.drv_name = "sof_sdw",
		.sof_fw_filename = "sof-cnl.ri",
		.sof_tplg_filename = "sof-cnl-rt5682-sdw2.tplg"
	},
	{}
};
EXPORT_SYMBOL_GPL(snd_soc_acpi_intel_cnl_sdw_machines);
