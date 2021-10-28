// SPDX-License-Identifier: GPL-2.0-only
/*
 * soc-acpi-intel-glk-match.c - tables and support for GLK ACPI enumeration.
 *
 * Copyright (c) 2018, Intel Corporation.
 *
 */

#include <sound/soc-acpi.h>
#include <sound/soc-acpi-intel-match.h>

static struct snd_soc_acpi_codecs glk_codecs = {
	.num_codecs = 1,
	.codecs = {"MX98357A"}
};

struct snd_soc_acpi_mach snd_soc_acpi_intel_glk_machines[] = {
	{
		.id = "INT343A",
		.drv_name = "glk_alc298s_i2s",
		.fw_filename = "intel/dsp_fw_glk.bin",
		.sof_fw_filename = "sof-glk.ri",
		.sof_tplg_filename = "sof-glk-alc298.tplg",
	},
	{
		.id = "DLGS7219",
		.drv_name = "glk_da7219_mx98357a",
		.fw_filename = "intel/dsp_fw_glk.bin",
		.machine_quirk = snd_soc_acpi_codec_list,
		.quirk_data = &glk_codecs,
		.sof_fw_filename = "sof-glk.ri",
		.sof_tplg_filename = "sof-glk-da7219.tplg",
	},
	{
		.id = "10EC5682",
		.drv_name = "glk_rt5682_mx98357a",
		.fw_filename = "intel/dsp_fw_glk.bin",
		.machine_quirk = snd_soc_acpi_codec_list,
		.quirk_data = &glk_codecs,
		.sof_fw_filename = "sof-glk.ri",
		.sof_tplg_filename = "sof-glk-rt5682.tplg",
	},
	{
		.id = "RTL5682",
		.drv_name = "glk_rt5682_max98357a",
		.machine_quirk = snd_soc_acpi_codec_list,
		.quirk_data = &glk_codecs,
		.sof_fw_filename = "sof-glk.ri",
		.sof_tplg_filename = "sof-glk-rt5682.tplg",
	},
	{
		.id = "10134242",
		.drv_name = "glk_cs4242_mx98357a",
		.fw_filename = "intel/dsp_fw_glk.bin",
		.machine_quirk = snd_soc_acpi_codec_list,
		.quirk_data = &glk_codecs,
		.sof_fw_filename = "sof-glk.ri",
		.sof_tplg_filename = "sof-glk-cs42l42.tplg",
	},
	{
		.id = "ESSX8336",
		.drv_name = "sof-essx8336",
		.sof_fw_filename = "sof-glk.ri",
		.sof_tplg_filename = "sof-glk-es8336.tplg",
	},
	{},
};
EXPORT_SYMBOL_GPL(snd_soc_acpi_intel_glk_machines);
