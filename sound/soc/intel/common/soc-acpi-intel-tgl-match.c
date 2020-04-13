// SPDX-License-Identifier: GPL-2.0
/*
 * soc-apci-intel-tgl-match.c - tables and support for ICL ACPI enumeration.
 *
 * Copyright (c) 2019, Intel Corporation.
 *
 */

#include <sound/soc-acpi.h>
#include <sound/soc-acpi-intel-match.h>

static struct snd_soc_acpi_codecs tgl_codecs = {
	.num_codecs = 1,
	.codecs = {"MX98357A"}
};

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
		.adr = 0x000120025D130800,
		.num_endpoints = 1,
		.endpoints = &spk_l_endpoint,
	},
	{
		.adr = 0x000122025D130800,
		.num_endpoints = 1,
		.endpoints = &spk_r_endpoint,
	}
};

static const struct snd_soc_acpi_adr_device rt5682_0_adr[] = {
	{
		.adr = 0x000021025D568200,
		.num_endpoints = 1,
		.endpoints = &single_endpoint,
	}
};

static const struct snd_soc_acpi_link_adr tgl_i2s_rt1308[] = {
	{
		.mask = BIT(0),
		.num_adr = ARRAY_SIZE(rt711_0_adr),
		.adr_d = rt711_0_adr,
	},
	{}
};

static const struct snd_soc_acpi_link_adr tgl_rvp[] = {
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
	{}
};

static const struct snd_soc_acpi_link_adr tgl_chromebook_base[] = {
	{
		.mask = BIT(0),
		.num_adr = ARRAY_SIZE(rt5682_0_adr),
		.adr_d = rt5682_0_adr,
	},
	{}
};

static struct snd_soc_acpi_codecs tgl_max98373_amp = {
	.num_codecs = 1,
	.codecs = {"MX98373"}
};

struct snd_soc_acpi_mach snd_soc_acpi_intel_tgl_machines[] = {
	{
		.id = "10EC1308",
		.drv_name = "sof_sdw",
		.link_mask = 0x1, /* RT711 on SoundWire link0 */
		.links = tgl_i2s_rt1308,
		.sof_fw_filename = "sof-tgl.ri",
		.sof_tplg_filename = "sof-tgl-rt711-i2s-rt1308.tplg",
	},
	{
		.id = "10EC5682",
		.drv_name = "tgl_max98357a_rt5682",
		.machine_quirk = snd_soc_acpi_codec_list,
		.quirk_data = &tgl_codecs,
		.sof_fw_filename = "sof-tgl.ri",
		.sof_tplg_filename = "sof-tgl-max98357a-rt5682.tplg",
	},
	{
		.id = "10EC5682",
		.drv_name = "tgl_max98373_rt5682",
		.machine_quirk = snd_soc_acpi_codec_list,
		.quirk_data = &tgl_max98373_amp,
		.sof_fw_filename = "sof-tgl.ri",
		.sof_tplg_filename = "sof-tgl-max98373-rt5682.tplg",
	},
	{},
};
EXPORT_SYMBOL_GPL(snd_soc_acpi_intel_tgl_machines);

/* this table is used when there is no I2S codec present */
struct snd_soc_acpi_mach snd_soc_acpi_intel_tgl_sdw_machines[] = {
	{
		.link_mask = 0x3, /* rt711 on link 0 and 2 rt1308s on link 1 */
		.links = tgl_rvp,
		.drv_name = "sof_sdw",
		.sof_fw_filename = "sof-tgl.ri",
		.sof_tplg_filename = "sof-tgl-rt711-rt1308.tplg",
	},
	{
		.link_mask = 0x1, /* this will only enable rt5682 for now */
		.links = tgl_chromebook_base,
		.drv_name = "sof_sdw",
		.sof_fw_filename = "sof-tgl.ri",
		.sof_tplg_filename = "sof-tgl-rt5682.tplg",
	},
	{},
};
EXPORT_SYMBOL_GPL(snd_soc_acpi_intel_tgl_sdw_machines);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Intel Common ACPI Match module");
