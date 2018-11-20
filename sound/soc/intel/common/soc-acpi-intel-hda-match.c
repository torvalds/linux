// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2018, Intel Corporation.

/*
 * soc-apci-intel-hda-match.c - tables and support for HDA+ACPI enumeration.
 *
 */

#include <sound/soc-acpi.h>
#include <sound/soc-acpi-intel-match.h>
#include "../skylake/skl.h"

static struct skl_machine_pdata hda_pdata = {
	.use_tplg_pcm = true,
};

struct snd_soc_acpi_mach snd_soc_acpi_intel_hda_machines[] = {
	{
		/* .id is not used in this file */
		.drv_name = "skl_hda_dsp_generic",

		/* .fw_filename is dynamically set in skylake driver */

		/* .sof_fw_filename is dynamically set in sof/intel driver */

		.sof_tplg_filename = "intel/sof-hda-generic.tplg",

		/*
		 * .machine_quirk and .quirk_data are not used here but
		 * can be used if we need a more complicated machine driver
		 * combining HDA+other device (e.g. DMIC).
		 */
		.pdata = &hda_pdata,
	},
	{},
};
EXPORT_SYMBOL_GPL(snd_soc_acpi_intel_hda_machines);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Intel Common ACPI Match module");
