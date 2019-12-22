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

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Intel Common ACPI Match module");
