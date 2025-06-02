// SPDX-License-Identifier: GPL-2.0-only
/*
 * amd-acpi-match.c - tables and support for ACP platforms
 * ACPI enumeration.
 *
 * Copyright 2025 Advanced Micro Devices, Inc.
 */

#include <sound/soc-acpi.h>

struct snd_soc_acpi_codecs amp_rt1019 = {
	.num_codecs = 1,
	.codecs = {"10EC1019"}
};

struct snd_soc_acpi_codecs amp_max = {
	.num_codecs = 1,
	.codecs = {"MX98360A"}
};

struct snd_soc_acpi_mach snd_soc_acpi_amd_acp_machines[] = {
	{
		.id = "10EC5682",
		.drv_name = "acp3xalc56821019",
		.machine_quirk = snd_soc_acpi_codec_list,
		.quirk_data = &amp_rt1019,
	},
	{
		.id = "RTL5682",
		.drv_name = "acp3xalc5682sm98360",
		.machine_quirk = snd_soc_acpi_codec_list,
		.quirk_data = &amp_max,
	},
	{
		.id = "RTL5682",
		.drv_name = "acp3xalc5682s1019",
		.machine_quirk = snd_soc_acpi_codec_list,
		.quirk_data = &amp_rt1019,
	},
	{
		.id = "AMDI1019",
		.drv_name = "renoir-acp",
	},
	{
		.id = "ESSX8336",
		.drv_name = "acp3x-es83xx",
	},
	{},
};
EXPORT_SYMBOL_NS_GPL(snd_soc_acpi_amd_acp_machines, "SND_SOC_ACP_COMMON");

struct snd_soc_acpi_mach snd_soc_acpi_amd_rmb_acp_machines[] = {
	{
		.id = "10508825",
		.drv_name = "rmb-nau8825-max",
		.machine_quirk = snd_soc_acpi_codec_list,
		.quirk_data = &amp_max,
	},
	{
		.id = "AMDI0007",
		.drv_name = "rembrandt-acp",
	},
	{
		.id = "RTL5682",
		.drv_name = "rmb-rt5682s-rt1019",
		.machine_quirk = snd_soc_acpi_codec_list,
		.quirk_data = &amp_rt1019,
	},
	{},
};
EXPORT_SYMBOL_NS_GPL(snd_soc_acpi_amd_rmb_acp_machines, "SND_SOC_ACP_COMMON");

struct snd_soc_acpi_mach snd_soc_acpi_amd_acp63_acp_machines[] = {
	{
		.id = "AMDI0052",
		.drv_name = "acp63-acp",
	},
	{},
};
EXPORT_SYMBOL_NS_GPL(snd_soc_acpi_amd_acp63_acp_machines, "SND_SOC_ACP_COMMON");

struct snd_soc_acpi_mach snd_soc_acpi_amd_acp70_acp_machines[] = {
	{
		.id = "AMDI0029",
		.drv_name = "acp70-acp",
	},
	{},
};
EXPORT_SYMBOL_NS_GPL(snd_soc_acpi_amd_acp70_acp_machines, "SND_SOC_ACP_COMMON");

MODULE_DESCRIPTION("AMD ACP tables and support for ACPI enumeration");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Venkataprasad.potturu@amd.com");
