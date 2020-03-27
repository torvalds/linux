// SPDX-License-Identifier: GPL-2.0
/*
 * soc-acpi-intel-cml-match.c - tables and support for CML ACPI enumeration.
 *
 * Copyright (c) 2019, Intel Corporation.
 *
 */

#include <sound/soc-acpi.h>
#include <sound/soc-acpi-intel-match.h>

static struct snd_soc_acpi_codecs rt1011_spk_codecs = {
	.num_codecs = 1,
	.codecs = {"10EC1011"}
};

static struct snd_soc_acpi_codecs max98357a_spk_codecs = {
	.num_codecs = 1,
	.codecs = {"MX98357A"}
};

/*
 * The order of the three entries with .id = "10EC5682" matters
 * here, because DSDT tables expose an ACPI HID for the MAX98357A
 * speaker amplifier which is not populated on the board.
 */
struct snd_soc_acpi_mach snd_soc_acpi_intel_cml_machines[] = {
	{
		.id = "10EC5682",
		.drv_name = "cml_rt1011_rt5682",
		.machine_quirk = snd_soc_acpi_codec_list,
		.quirk_data = &rt1011_spk_codecs,
		.sof_fw_filename = "sof-cml.ri",
		.sof_tplg_filename = "sof-cml-rt1011-rt5682.tplg",
	},
	{
		.id = "10EC5682",
		.drv_name = "sof_rt5682",
		.machine_quirk = snd_soc_acpi_codec_list,
		.quirk_data = &max98357a_spk_codecs,
		.sof_fw_filename = "sof-cml.ri",
		.sof_tplg_filename = "sof-cml-rt5682-max98357a.tplg",
	},
	{
		.id = "10EC5682",
		.drv_name = "sof_rt5682",
		.sof_fw_filename = "sof-cml.ri",
		.sof_tplg_filename = "sof-cml-rt5682.tplg",
	},
	{
		.id = "DLGS7219",
		.drv_name = "cml_da7219_max98357a",
		.machine_quirk = snd_soc_acpi_codec_list,
		.quirk_data = &max98357a_spk_codecs,
		.sof_fw_filename = "sof-cml.ri",
		.sof_tplg_filename = "sof-cml-da7219-max98357a.tplg",
	},
	{},
};
EXPORT_SYMBOL_GPL(snd_soc_acpi_intel_cml_machines);

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
		.adr = 0x000010025D071100,
		.num_endpoints = 1,
		.endpoints = &single_endpoint,
	}
};

static const struct snd_soc_acpi_adr_device rt1308_1_adr[] = {
	{
		.adr = 0x000110025D130800,
		.num_endpoints = 1,
		.endpoints = &single_endpoint,
	}
};

static const struct snd_soc_acpi_adr_device rt1308_2_adr[] = {
	{
		.adr = 0x000210025D130800,
		.num_endpoints = 1,
		.endpoints = &single_endpoint,
	}
};

static const struct snd_soc_acpi_adr_device rt1308_1_group1_adr[] = {
	{
		.adr = 0x000110025D130800,
		.num_endpoints = 1,
		.endpoints = &spk_l_endpoint,
	}
};

static const struct snd_soc_acpi_adr_device rt1308_2_group1_adr[] = {
	{
		.adr = 0x000210025D130800,
		.num_endpoints = 1,
		.endpoints = &spk_r_endpoint,
	}
};

static const struct snd_soc_acpi_adr_device rt715_3_adr[] = {
	{
		.adr = 0x000310025D071500,
		.num_endpoints = 1,
		.endpoints = &single_endpoint,
	}
};

static const struct snd_soc_acpi_link_adr cml_3_in_1_default[] = {
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

static const struct snd_soc_acpi_link_adr cml_3_in_1_mono_amp[] = {
	{
		.mask = BIT(0),
		.num_adr = ARRAY_SIZE(rt711_0_adr),
		.adr_d = rt711_0_adr,
	},
	{
		.mask = BIT(1),
		.num_adr = ARRAY_SIZE(rt1308_1_adr),
		.adr_d = rt1308_1_adr,
	},
	{
		.mask = BIT(3),
		.num_adr = ARRAY_SIZE(rt715_3_adr),
		.adr_d = rt715_3_adr,
	},
	{}
};

struct snd_soc_acpi_mach snd_soc_acpi_intel_cml_sdw_machines[] = {
	{
		.link_mask = 0xF, /* 4 active links required */
		.links = cml_3_in_1_default,
		.drv_name = "sdw_rt711_rt1308_rt715",
		.sof_fw_filename = "sof-cml.ri",
		.sof_tplg_filename = "sof-cml-rt711-rt1308-rt715.tplg",
	},
	{
		/*
		 * link_mask should be 0xB, but all links are enabled by BIOS.
		 * This entry will be selected if there is no rt1308 exposed
		 * on link2 since it will fail to match the above entry.
		 */
		.link_mask = 0xF,
		.links = cml_3_in_1_mono_amp,
		.drv_name = "sdw_rt711_rt1308_rt715",
		.sof_fw_filename = "sof-cml.ri",
		.sof_tplg_filename = "sof-cml-rt711-rt1308-mono-rt715.tplg",
	},
	{
		.link_mask = 0x2, /* RT700 connected on Link1 */
		.drv_name = "sdw_rt700",
		.sof_fw_filename = "sof-cml.ri",
		.sof_tplg_filename = "sof-cml-rt700.tplg",
	},
	{}
};
EXPORT_SYMBOL_GPL(snd_soc_acpi_intel_cml_sdw_machines);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Intel Common ACPI Match module");
