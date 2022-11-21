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
#include "soc-acpi-intel-sdw-mockup-match.h"

static const struct snd_soc_acpi_codecs essx_83x6 = {
	.num_codecs = 3,
	.codecs = { "ESSX8316", "ESSX8326", "ESSX8336"},
};

static struct skl_machine_pdata cnl_pdata = {
	.use_tplg_pcm = true,
};

struct snd_soc_acpi_mach snd_soc_acpi_intel_cnl_machines[] = {
	{
		.id = "INT34C2",
		.drv_name = "cnl_rt274",
		.fw_filename = "intel/dsp_fw_cnl.bin",
		.pdata = &cnl_pdata,
		.sof_tplg_filename = "sof-cnl-rt274.tplg",
	},
	{
		.comp_ids = &essx_83x6,
		.drv_name = "sof-essx8336",
		/* cnl and cml are identical */
		.sof_tplg_filename = "sof-cml-es8336", /* the tplg suffix is added at run time */
		.tplg_quirk_mask = SND_SOC_ACPI_TPLG_INTEL_SSP_NUMBER |
					SND_SOC_ACPI_TPLG_INTEL_SSP_MSB |
					SND_SOC_ACPI_TPLG_INTEL_DMIC_NUMBER,
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
		.sof_tplg_filename = "sof-cnl-rt5682-sdw2.tplg"
	},
	{
		.link_mask = GENMASK(3, 0),
		.links = sdw_mockup_headset_2amps_mic,
		.drv_name = "sof_sdw",
		.sof_tplg_filename = "sof-cml-rt711-rt1308-rt715.tplg",
	},
	{
		.link_mask = BIT(0) | BIT(1) | BIT(3),
		.links = sdw_mockup_headset_1amp_mic,
		.drv_name = "sof_sdw",
		.sof_tplg_filename = "sof-cml-rt711-rt1308-mono-rt715.tplg",
	},
	{}
};
EXPORT_SYMBOL_GPL(snd_soc_acpi_intel_cnl_sdw_machines);
