// SPDX-License-Identifier: GPL-2.0-only
/*
 * soc-apci-intel-jsl-match.c - tables and support for JSL ACPI enumeration.
 *
 * Copyright (c) 2019-2020, Intel Corporation.
 *
 */

#include <sound/soc-acpi.h>
#include <sound/soc-acpi-intel-match.h>

static const struct snd_soc_acpi_codecs jsl_7219_98373_codecs = {
	.num_codecs = 1,
	.codecs = {"MX98373"}
};

static const struct snd_soc_acpi_codecs rt1015_spk = {
	.num_codecs = 1,
	.codecs = {"10EC1015"}
};

static const struct snd_soc_acpi_codecs rt1015p_spk = {
	.num_codecs = 1,
	.codecs = {"RTL1015"}
};

static const struct snd_soc_acpi_codecs mx98360a_spk = {
	.num_codecs = 1,
	.codecs = {"MX98360A"}
};

static const struct snd_soc_acpi_codecs rt5682_rt5682s_hp = {
	.num_codecs = 2,
	.codecs = {"10EC5682", "RTL5682"},
};

/*
 * When adding new entry to the snd_soc_acpi_intel_jsl_machines array,
 * use .quirk_data member to distinguish different machine driver,
 * and keep ACPI .id field unchanged for the common codec.
 */
struct snd_soc_acpi_mach snd_soc_acpi_intel_jsl_machines[] = {
	{
		.id = "DLGS7219",
		.drv_name = "sof_da7219_mx98373",
		.sof_fw_filename = "sof-jsl.ri",
		.sof_tplg_filename = "sof-jsl-da7219.tplg",
		.machine_quirk = snd_soc_acpi_codec_list,
		.quirk_data = &jsl_7219_98373_codecs,
	},
	{
		.id = "DLGS7219",
		.drv_name = "sof_da7219_mx98360a",
		.sof_fw_filename = "sof-jsl.ri",
		.sof_tplg_filename = "sof-jsl-da7219-mx98360a.tplg",
	},
	{
		.comp_ids = &rt5682_rt5682s_hp,
		.drv_name = "jsl_rt5682_rt1015",
		.sof_fw_filename = "sof-jsl.ri",
		.machine_quirk = snd_soc_acpi_codec_list,
		.quirk_data = &rt1015_spk,
		.sof_tplg_filename = "sof-jsl-rt5682-rt1015.tplg",
	},
	{
		.comp_ids = &rt5682_rt5682s_hp,
		.drv_name = "jsl_rt5682_rt1015p",
		.sof_fw_filename = "sof-jsl.ri",
		.machine_quirk = snd_soc_acpi_codec_list,
		.quirk_data = &rt1015p_spk,
		.sof_tplg_filename = "sof-jsl-rt5682-rt1015.tplg",
	},
	{
		.comp_ids = &rt5682_rt5682s_hp,
		.drv_name = "jsl_rt5682_mx98360",
		.sof_fw_filename = "sof-jsl.ri",
		.machine_quirk = snd_soc_acpi_codec_list,
		.quirk_data = &mx98360a_spk,
		.sof_tplg_filename = "sof-jsl-rt5682-mx98360a.tplg",
	},
	{
		.id = "10134242",
		.drv_name = "jsl_cs4242_mx98360a",
		.sof_fw_filename = "sof-jsl.ri",
		.machine_quirk = snd_soc_acpi_codec_list,
		.quirk_data = &mx98360a_spk,
		.sof_tplg_filename = "sof-jsl-cs42l42-mx98360a.tplg",
	},
	{
		.id = "ESSX8336",
		.drv_name = "sof-essx8336",
		.sof_fw_filename = "sof-jsl.ri",
		.sof_tplg_filename = "sof-jsl-es8336.tplg",
	},
	{},
};
EXPORT_SYMBOL_GPL(snd_soc_acpi_intel_jsl_machines);
