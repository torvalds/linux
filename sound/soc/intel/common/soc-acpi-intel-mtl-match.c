// SPDX-License-Identifier: GPL-2.0-only
/*
 * soc-acpi-intel-mtl-match.c - tables and support for MTL ACPI enumeration.
 *
 * Copyright (c) 2022, Intel Corporation.
 *
 */

#include <sound/soc-acpi.h>
#include <sound/soc-acpi-intel-match.h>
#include "soc-acpi-intel-sdw-mockup-match.h"

static const struct snd_soc_acpi_codecs mtl_max98357a_amp = {
	.num_codecs = 1,
	.codecs = {"MX98357A"}
};

static const struct snd_soc_acpi_codecs mtl_rt5682_rt5682s_hp = {
	.num_codecs = 2,
	.codecs = {"10EC5682", "RTL5682"},
};

static const struct snd_soc_acpi_codecs mtl_lt6911_hdmi = {
	.num_codecs = 1,
	.codecs = {"INTC10B0"}
};

static const struct snd_soc_acpi_codecs mtl_essx_83x6 = {
	.num_codecs = 3,
	.codecs = { "ESSX8316", "ESSX8326", "ESSX8336"},
};

struct snd_soc_acpi_mach snd_soc_acpi_intel_mtl_machines[] = {
	{
		.comp_ids = &mtl_rt5682_rt5682s_hp,
		.drv_name = "mtl_mx98357_rt5682",
		.machine_quirk = snd_soc_acpi_codec_list,
		.quirk_data = &mtl_max98357a_amp,
		.sof_tplg_filename = "sof-mtl-max98357a-rt5682.tplg",
	},
	{
		.comp_ids = &mtl_essx_83x6,
		.drv_name = "sof-essx8336",
		.sof_tplg_filename = "sof-mtl-es8336", /* the tplg suffix is added at run time */
		.tplg_quirk_mask = SND_SOC_ACPI_TPLG_INTEL_SSP_NUMBER |
					SND_SOC_ACPI_TPLG_INTEL_SSP_MSB |
					SND_SOC_ACPI_TPLG_INTEL_DMIC_NUMBER,
	},
	{},
};
EXPORT_SYMBOL_GPL(snd_soc_acpi_intel_mtl_machines);

static const struct snd_soc_acpi_endpoint single_endpoint = {
	.num = 0,
	.aggregated = 0,
	.group_position = 0,
	.group_id = 0,
};

static const struct snd_soc_acpi_adr_device rt711_sdca_0_adr[] = {
	{
		.adr = 0x000030025D071101ull,
		.num_endpoints = 1,
		.endpoints = &single_endpoint,
		.name_prefix = "rt711"
	}
};

static const struct snd_soc_acpi_link_adr mtl_rvp[] = {
	{
		.mask = BIT(0),
		.num_adr = ARRAY_SIZE(rt711_sdca_0_adr),
		.adr_d = rt711_sdca_0_adr,
	},
	{}
};

/* this table is used when there is no I2S codec present */
struct snd_soc_acpi_mach snd_soc_acpi_intel_mtl_sdw_machines[] = {
	/* mockup tests need to be first */
	{
		.link_mask = GENMASK(3, 0),
		.links = sdw_mockup_headset_2amps_mic,
		.drv_name = "sof_sdw",
		.sof_tplg_filename = "sof-mtl-rt711-rt1308-rt715.tplg",
	},
	{
		.comp_ids = &mtl_essx_83x6,
		.drv_name = "mtl_es83x6_c1_h02",
		.machine_quirk = snd_soc_acpi_codec_list,
		.quirk_data = &mtl_lt6911_hdmi,
		.sof_tplg_filename = "sof-mtl-es83x6-ssp1-hdmi-ssp02.tplg",
	},
	{
		.link_mask = BIT(0) | BIT(1) | BIT(3),
		.links = sdw_mockup_headset_1amp_mic,
		.drv_name = "sof_sdw",
		.sof_tplg_filename = "sof-mtl-rt711-rt1308-mono-rt715.tplg",
	},
	{
		.link_mask = GENMASK(2, 0),
		.links = sdw_mockup_mic_headset_1amp,
		.drv_name = "sof_sdw",
		.sof_tplg_filename = "sof-mtl-rt715-rt711-rt1308-mono.tplg",
	},
	{
		.link_mask = BIT(0),
		.links = mtl_rvp,
		.drv_name = "sof_sdw",
		.sof_tplg_filename = "sof-mtl-rt711.tplg",
	},
	{},
};
EXPORT_SYMBOL_GPL(snd_soc_acpi_intel_mtl_sdw_machines);
