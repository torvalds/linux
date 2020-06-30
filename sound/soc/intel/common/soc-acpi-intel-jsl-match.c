// SPDX-License-Identifier: GPL-2.0-only
/*
 * soc-apci-intel-jsl-match.c - tables and support for JSL ACPI enumeration.
 *
 * Copyright (c) 2019-2020, Intel Corporation.
 *
 */

#include <sound/soc-acpi.h>
#include <sound/soc-acpi-intel-match.h>

static struct snd_soc_acpi_codecs jsl_7219_98373_codecs = {
	.num_codecs = 1,
	.codecs = {"MX98373"}
};

static struct snd_soc_acpi_codecs rt1015_spk = {
	.num_codecs = 1,
	.codecs = {"10EC1015"}
};

/*
 * When adding new entry to the snd_soc_acpi_intel_jsl_machines array,
 * use .quirk_data member to distinguish different machine driver,
 * and keep ACPI .id field unchanged for the common codec.
 */
struct snd_soc_acpi_mach snd_soc_acpi_intel_jsl_machines[] = {
	{
		.id = "DLGS7219",
		.drv_name = "sof_da7219_max98373",
		.sof_fw_filename = "sof-jsl.ri",
		.sof_tplg_filename = "sof-jsl-da7219.tplg",
		.machine_quirk = snd_soc_acpi_codec_list,
		.quirk_data = &jsl_7219_98373_codecs,
	},
	{
		.id = "DLGS7219",
		.drv_name = "sof_da7219_max98360a",
		.sof_fw_filename = "sof-jsl.ri",
		.sof_tplg_filename = "sof-jsl-da7219-mx98360a.tplg",
	},
	{
		.id = "10EC5682",
		.drv_name = "jsl_rt5682_rt1015",
		.sof_fw_filename = "sof-jsl.ri",
		.machine_quirk = snd_soc_acpi_codec_list,
		.quirk_data = &rt1015_spk,
		.sof_tplg_filename = "sof-jsl-rt5682-rt1015.tplg",
	},
	{},
};
EXPORT_SYMBOL_GPL(snd_soc_acpi_intel_jsl_machines);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Intel Common ACPI Match module");
