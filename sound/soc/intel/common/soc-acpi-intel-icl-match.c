// SPDX-License-Identifier: GPL-2.0
/*
 * soc-acpi-intel-icl-match.c - tables and support for ICL ACPI enumeration.
 *
 * Copyright (c) 2018, Intel Corporation.
 *
 */

#include <sound/soc-acpi.h>
#include <sound/soc-acpi-intel-match.h>
#include "../skylake/skl.h"

static struct skl_machine_pdata icl_pdata = {
	.use_tplg_pcm = true,
};

struct snd_soc_acpi_mach snd_soc_acpi_intel_icl_machines[] = {
	{
		.id = "INT34C2",
		.drv_name = "icl_rt274",
		.fw_filename = "intel/dsp_fw_icl.bin",
		.pdata = &icl_pdata,
		.sof_fw_filename = "sof-icl.ri",
		.sof_tplg_filename = "sof-icl-rt274.tplg",
	},
	{
		.id = "10EC5682",
		.drv_name = "sof_rt5682",
		.sof_fw_filename = "sof-icl.ri",
		.sof_tplg_filename = "sof-icl-rt5682.tplg",
	},
	{},
};
EXPORT_SYMBOL_GPL(snd_soc_acpi_intel_icl_machines);

static const u64 rt700_0_adr[] = {
	0x000010025D070000
};

static const struct snd_soc_acpi_link_adr icl_rvp[] = {
	{
		.mask = BIT(0),
		.num_adr = ARRAY_SIZE(rt700_0_adr),
		.adr = rt700_0_adr,
	},
	{}
};

static const u64 rt711_0_adr[] = {
	0x000010025D071100
};

static const u64 rt1308_1_adr[] = {
	0x000110025D130800
};

static const u64 rt1308_2_adr[] = {
	0x000210025D130800
};

static const u64 rt715_3_adr[] = {
	0x000310025D715000
};

static const struct snd_soc_acpi_link_adr icl_3_in_1_default[] = {
	{
		.mask = BIT(0),
		.num_adr = ARRAY_SIZE(rt711_0_adr),
		.adr = rt711_0_adr,
	},
	{
		.mask = BIT(1),
		.num_adr = ARRAY_SIZE(rt1308_1_adr),
		.adr = rt1308_1_adr,
	},
	{
		.mask = BIT(2),
		.num_adr = ARRAY_SIZE(rt1308_2_adr),
		.adr = rt1308_2_adr,
	},
	{
		.mask = BIT(3),
		.num_adr = ARRAY_SIZE(rt715_3_adr),
		.adr = rt715_3_adr,
	},
	{}
};

static const struct snd_soc_acpi_link_adr icl_3_in_1_mono_amp[] = {
	{
		.mask = BIT(0),
		.num_adr = ARRAY_SIZE(rt711_0_adr),
		.adr = rt711_0_adr,
	},
	{
		.mask = BIT(1),
		.num_adr = ARRAY_SIZE(rt1308_1_adr),
		.adr = rt1308_1_adr,
	},
	{
		.mask = BIT(3),
		.num_adr = ARRAY_SIZE(rt715_3_adr),
		.adr = rt715_3_adr,
	},
	{}
};

struct snd_soc_acpi_mach snd_soc_acpi_intel_icl_sdw_machines[] = {
	{
		.link_mask = 0xF, /* 4 active links required */
		.links = icl_3_in_1_default,
		.drv_name = "sdw_rt711_rt1308_rt715",
		.sof_fw_filename = "sof-icl.ri",
		.sof_tplg_filename = "sof-icl-rt711-rt1308-rt715.tplg",
	},
	{
		.link_mask = 0xB, /* 3 active links required */
		.links = icl_3_in_1_mono_amp,
		.drv_name = "sdw_rt711_rt1308_rt715",
		.sof_fw_filename = "sof-icl.ri",
		.sof_tplg_filename = "sof-icl-rt711-rt1308-rt715-mono.tplg",
	},
	{
		.link_mask = 0x1, /* rt700 connected on link0 */
		.links = icl_rvp,
		.drv_name = "sdw_rt700",
		.sof_fw_filename = "sof-icl.ri",
		.sof_tplg_filename = "sof-icl-rt700.tplg",
	},
	{},
};
EXPORT_SYMBOL_GPL(snd_soc_acpi_intel_icl_sdw_machines);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Intel Common ACPI Match module");
