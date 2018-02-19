/*
 * soc-apci-intel-byt-match.c - tables and support for BYT ACPI enumeration.
 *
 * Copyright (c) 2017, Intel Corporation.
 *
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include <linux/dmi.h>
#include <sound/soc-acpi.h>
#include <sound/soc-acpi-intel-match.h>

static unsigned long byt_machine_id;

#define BYT_THINKPAD_10  1

static int byt_thinkpad10_quirk_cb(const struct dmi_system_id *id)
{
	byt_machine_id = BYT_THINKPAD_10;
	return 1;
}


static const struct dmi_system_id byt_table[] = {
	{
		.callback = byt_thinkpad10_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_VERSION, "ThinkPad 10"),
		},
	},
	{
		.callback = byt_thinkpad10_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_VERSION, "ThinkPad Tablet B"),
		},
	},
	{
		.callback = byt_thinkpad10_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_VERSION, "Lenovo Miix 2 10"),
		},
	},
	{ }
};

static struct snd_soc_acpi_mach byt_thinkpad_10 = {
	.id = "10EC5640",
	.drv_name = "cht-bsw-rt5672",
	.fw_filename = "intel/fw_sst_0f28.bin",
	.board = "cht-bsw",
	.sof_fw_filename = "intel/reef-byt.ri",
	.sof_tplg_filename = "intel/reef-byt-rt5670.tplg",
	.asoc_plat_name = "sst-mfld-platform",
};

static struct snd_soc_acpi_mach *byt_quirk(void *arg)
{
	struct snd_soc_acpi_mach *mach = arg;

	dmi_check_system(byt_table);

	if (byt_machine_id == BYT_THINKPAD_10)
		return &byt_thinkpad_10;
	else
		return mach;
}

struct snd_soc_acpi_mach snd_soc_acpi_intel_baytrail_legacy_machines[] = {
	{
		.id = "10EC5640",
		.drv_name = "byt-rt5640",
		.fw_filename = "intel/fw_sst_0f28.bin-48kHz_i2s_master",
	},
	{
		.id = "193C9890",
		.drv_name = "byt-max98090",
		.fw_filename = "intel/fw_sst_0f28.bin-48kHz_i2s_master",
	},
	{}
};
EXPORT_SYMBOL_GPL(snd_soc_acpi_intel_baytrail_legacy_machines);

struct snd_soc_acpi_mach  snd_soc_acpi_intel_baytrail_machines[] = {
	{
		.id = "10EC5640",
		.drv_name = "bytcr_rt5640",
		.fw_filename = "intel/fw_sst_0f28.bin",
		.board = "bytcr_rt5640",
		.machine_quirk = byt_quirk,
		.sof_fw_filename = "intel/reef-byt.ri",
		.sof_tplg_filename = "intel/reef-byt-rt5640.tplg",
		.asoc_plat_name = "sst-mfld-platform",
	},
	{
		.id = "10EC5642",
		.drv_name = "bytcr_rt5640",
		.fw_filename = "intel/fw_sst_0f28.bin",
		.board = "bytcr_rt5640",
		.sof_fw_filename = "intel/reef-byt.ri",
		.sof_tplg_filename = "intel/reef-byt-rt5640.tplg",
		.asoc_plat_name = "sst-mfld-platform",
	},
	{
		.id = "INTCCFFD",
		.drv_name = "bytcr_rt5640",
		.fw_filename = "intel/fw_sst_0f28.bin",
		.board = "bytcr_rt5640",
		.sof_fw_filename = "intel/reef-byt.ri",
		.sof_tplg_filename = "intel/reef-byt-rt5640.tplg",
		.asoc_plat_name = "sst-mfld-platform",
	},
	{
		.id = "10EC5651",
		.drv_name = "bytcr_rt5651",
		.fw_filename = "intel/fw_sst_0f28.bin",
		.board = "bytcr_rt5651",
		.sof_fw_filename = "intel/reef-byt.ri",
		.sof_tplg_filename = "intel/reef-byt-rt5651.tplg",
		.asoc_plat_name = "sst-mfld-platform",
	},
	{
		.id = "DLGS7212",
		.drv_name = "bytcht_da7213",
		.fw_filename = "intel/fw_sst_0f28.bin",
		.board = "bytcht_da7213",
		.sof_fw_filename = "intel/reef-byt.ri",
		.sof_tplg_filename = "intel/reef-byt-da7213.tplg",
		.asoc_plat_name = "sst-mfld-platform",
	},
	{
		.id = "DLGS7213",
		.drv_name = "bytcht_da7213",
		.fw_filename = "intel/fw_sst_0f28.bin",
		.board = "bytcht_da7213",
		.sof_fw_filename = "intel/reef-byt.ri",
		.sof_tplg_filename = "intel/reef-byt-da7213.tplg",
		.asoc_plat_name = "sst-mfld-platform",
	},
	/* some Baytrail platforms rely on RT5645, use CHT machine driver */
	{
		.id = "10EC5645",
		.drv_name = "cht-bsw-rt5645",
		.fw_filename = "intel/fw_sst_0f28.bin",
		.board = "cht-bsw",
		.sof_fw_filename = "intel/reef-byt.ri",
		.sof_tplg_filename = "intel/reef-byt-rt5645.tplg",
		.asoc_plat_name = "sst-mfld-platform",
	},
	{
		.id = "10EC5648",
		.drv_name = "cht-bsw-rt5645",
		.fw_filename = "intel/fw_sst_0f28.bin",
		.board = "cht-bsw",
		.sof_fw_filename = "intel/reef-byt.ri",
		.sof_tplg_filename = "intel/reef-byt-rt5645.tplg",
		.asoc_plat_name = "sst-mfld-platform",
	},
	/* use CHT driver to Baytrail Chromebooks */
	{
		.id = "193C9890",
		.drv_name = "cht-bsw-max98090",
		.fw_filename = "intel/fw_sst_0f28.bin",
		.board = "cht-bsw",
		.sof_fw_filename = "intel/reef-byt.ri",
		.sof_tplg_filename = "intel/reef-byt-max98090.tplg",
		.asoc_plat_name = "sst-mfld-platform",
	},
#if IS_ENABLED(CONFIG_SND_SOC_INTEL_BYT_CHT_NOCODEC_MACH)
	/*
	 * This is always last in the table so that it is selected only when
	 * enabled explicitly and there is no codec-related information in SSDT
	 */
	{
		.id = "80860F28",
		.drv_name = "bytcht_nocodec",
		.fw_filename = "intel/fw_sst_0f28.bin",
		.board = "bytcht_nocodec",
	},
#endif
	{},
};
EXPORT_SYMBOL_GPL(snd_soc_acpi_intel_baytrail_machines);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Intel Common ACPI Match module");
