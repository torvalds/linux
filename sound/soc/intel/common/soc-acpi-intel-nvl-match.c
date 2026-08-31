// SPDX-License-Identifier: GPL-2.0-only
/*
 * soc-acpi-intel-nvl-match.c - tables and support for NVL ACPI enumeration.
 *
 * Copyright (c) 2025, Intel Corporation.
 *
 */

#include <sound/soc-acpi.h>
#include <sound/soc-acpi-intel-match.h>
#include <sound/soc-acpi-intel-ssp-common.h>
#include "soc-acpi-intel-sdw-mockup-match.h"

static const struct snd_soc_acpi_codecs nvl_essx_83x6 = {
	.num_codecs = 3,
	.codecs = { "ESSX8316", "ESSX8326", "ESSX8336"},
};

static const struct snd_soc_acpi_codecs nvl_lt6911_hdmi = {
	.num_codecs = 1,
	.codecs = {"INTC10B0"}
};

static const struct snd_soc_acpi_codecs nvl_rt5682_rt5682s_hp = {
	.num_codecs = 2,
	.codecs = {RT5682_ACPI_HID, RT5682S_ACPI_HID},
};

struct snd_soc_acpi_mach snd_soc_acpi_intel_nvl_machines[] = {
	{
		.comp_ids = &nvl_essx_83x6,
		.drv_name = "nvl_es83x6_c1_h02",
		.machine_quirk = snd_soc_acpi_codec_list,
		.quirk_data = &nvl_lt6911_hdmi,
		.sof_tplg_filename = "sof-nvl-es83x6-ssp1-hdmi-ssp02.tplg",
	},
	{
		.comp_ids = &nvl_essx_83x6,
		.drv_name = "sof-essx8336",
		.sof_tplg_filename = "sof-nvl-es8336", /* the tplg suffix is added at run time */
		.tplg_quirk_mask = SND_SOC_ACPI_TPLG_INTEL_SSP_NUMBER |
					SND_SOC_ACPI_TPLG_INTEL_SSP_MSB |
					SND_SOC_ACPI_TPLG_INTEL_DMIC_NUMBER,
	},
	{
		.comp_ids = &nvl_rt5682_rt5682s_hp,
		.drv_name = "nvl_rt5682_c1_h02",
		.machine_quirk = snd_soc_acpi_codec_list,
		.quirk_data = &nvl_lt6911_hdmi,
		.sof_tplg_filename = "sof-nvl-rt5682-ssp1-hdmi-ssp02.tplg",
	},
	{
		.comp_ids = &nvl_rt5682_rt5682s_hp,
		.drv_name = "sof_rt5682",
		.sof_tplg_filename = "sof-nvl-rt5682", /* the tplg suffix is added at run time */
		.tplg_quirk_mask = SND_SOC_ACPI_TPLG_INTEL_SSP_NUMBER |
					SND_SOC_ACPI_TPLG_INTEL_SSP_MSB,
	},
	/* place amp/hdmi-in only boards in the end of table */
	{
		.id = "INTC10B0",
		.drv_name = "nvl_lt6911_hdmi_ssp",
		.sof_tplg_filename = "sof-nvl-hdmi-ssp02.tplg",
	},
	{},
};
EXPORT_SYMBOL_GPL(snd_soc_acpi_intel_nvl_machines);

/* this table is used when there is no I2S codec present */
struct snd_soc_acpi_mach snd_soc_acpi_intel_nvl_sdw_machines[] = {
	/* mockup tests need to be first */
	{
		.link_mask = GENMASK(3, 0),
		.links = sdw_mockup_headset_2amps_mic,
		.drv_name = "sof_sdw",
		.sof_tplg_filename = "sof-nvl-rt711-rt1308-rt715.tplg",
	},
	{
		.link_mask = BIT(0) | BIT(1) | BIT(3),
		.links = sdw_mockup_headset_1amp_mic,
		.drv_name = "sof_sdw",
		.sof_tplg_filename = "sof-nvl-rt711-rt1308-mono-rt715.tplg",
	},
	{
		.link_mask = GENMASK(2, 0),
		.links = sdw_mockup_mic_headset_1amp,
		.drv_name = "sof_sdw",
		.sof_tplg_filename = "sof-nvl-rt715-rt711-rt1308-mono.tplg",
	},
	{},
};
EXPORT_SYMBOL_GPL(snd_soc_acpi_intel_nvl_sdw_machines);
