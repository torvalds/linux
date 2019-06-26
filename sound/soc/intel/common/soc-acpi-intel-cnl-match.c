// SPDX-License-Identifier: GPL-2.0
/*
 * soc-apci-intel-cnl-match.c - tables and support for CNL ACPI enumeration.
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

static struct snd_soc_acpi_codecs cml_codecs = {
	.num_codecs = 1,
	.codecs = {"10EC5682"}
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
	{
		.id = "MX98357A",
		.drv_name = "sof_rt5682",
		.quirk_data = &cml_codecs,
		.sof_fw_filename = "sof-cnl.ri",
		.sof_tplg_filename = "sof-cml-rt5682-max98357a.tplg",
	},
	{
		.id = "10EC5682",
		.drv_name = "sof_rt5682",
		.sof_fw_filename = "sof-cnl.ri",
		.sof_tplg_filename = "sof-cml-rt5682.tplg",
	},

	{},
};
EXPORT_SYMBOL_GPL(snd_soc_acpi_intel_cnl_machines);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Intel Common ACPI Match module");
