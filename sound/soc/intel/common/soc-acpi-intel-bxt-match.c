// SPDX-License-Identifier: GPL-2.0
/*
 * soc-apci-intel-bxt-match.c - tables and support for BXT ACPI enumeration.
 *
 * Copyright (c) 2018, Intel Corporation.
 *
 */

#include <sound/soc-acpi.h>
#include <sound/soc-acpi-intel-match.h>

static struct snd_soc_acpi_codecs bxt_codecs = {
	.num_codecs = 1,
	.codecs = {"MX98357A"}
};

struct snd_soc_acpi_mach snd_soc_acpi_intel_bxt_machines[] = {
	{
		.id = "INT343A",
		.drv_name = "bxt_alc298s_i2s",
		.fw_filename = "intel/dsp_fw_bxtn.bin",
	},
	{
		.id = "DLGS7219",
		.drv_name = "bxt_da7219_max98357a",
		.fw_filename = "intel/dsp_fw_bxtn.bin",
		.machine_quirk = snd_soc_acpi_codec_list,
		.quirk_data = &bxt_codecs,
		.sof_fw_filename = "intel/sof-apl.ri",
		.sof_tplg_filename = "intel/sof-apl-da7219.tplg",
		.asoc_plat_name = "0000:00:0e.0",
	},
	{
		.id = "104C5122",
		.drv_name = "bxt-pcm512x",
		.sof_fw_filename = "intel/sof-apl.ri",
		.sof_tplg_filename = "intel/sof-apl-pcm512x.tplg",
		.asoc_plat_name = "0000:00:0e.0",
	},
	{
		.id = "1AEC8804",
		.drv_name = "bxt-wm8804",
		.sof_fw_filename = "intel/sof-apl.ri",
		.sof_tplg_filename = "intel/sof-apl-wm8804.tplg",
		.asoc_plat_name = "0000:00:0e.0",
	},
	{
		.id = "INT34C3",
		.drv_name = "bxt_tdf8532",
		.sof_fw_filename = "intel/sof-apl.ri",
		.sof_tplg_filename = "intel/sof-apl-tdf8532.tplg",
		.asoc_plat_name = "0000:00:0e.0",
	},
	{},
};
EXPORT_SYMBOL_GPL(snd_soc_acpi_intel_bxt_machines);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Intel Common ACPI Match module");
