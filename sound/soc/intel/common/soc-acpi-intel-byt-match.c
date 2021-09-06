// SPDX-License-Identifier: GPL-2.0-only
/*
 * soc-acpi-intel-byt-match.c - tables and support for BYT ACPI enumeration.
 *
 * Copyright (c) 2017, Intel Corporation.
 */

#include <linux/dmi.h>
#include <sound/soc-acpi.h>
#include <sound/soc-acpi-intel-match.h>

static unsigned long byt_machine_id;

#define BYT_RT5672       1
#define BYT_POV_P1006W   2

static int byt_rt5672_quirk_cb(const struct dmi_system_id *id)
{
	byt_machine_id = BYT_RT5672;
	return 1;
}

static int byt_pov_p1006w_quirk_cb(const struct dmi_system_id *id)
{
	byt_machine_id = BYT_POV_P1006W;
	return 1;
}

static const struct dmi_system_id byt_table[] = {
	{
		.callback = byt_rt5672_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_VERSION, "ThinkPad 8"),
		},
	},
	{
		.callback = byt_rt5672_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_VERSION, "ThinkPad 10"),
		},
	},
	{
		.callback = byt_rt5672_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_VERSION, "ThinkPad Tablet B"),
		},
	},
	{
		.callback = byt_rt5672_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_VERSION, "Lenovo Miix 2 10"),
		},
	},
	{
		/* Point of View mobii wintab p1006w (v1.0) */
		.callback = byt_pov_p1006w_quirk_cb,
		.matches = {
			DMI_EXACT_MATCH(DMI_SYS_VENDOR, "Insyde"),
			DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "BayTrail"),
			/* Note 105b is Foxcon's USB/PCI vendor id */
			DMI_EXACT_MATCH(DMI_BOARD_VENDOR, "105B"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "0E57"),
		},
	},
	{
		/* Aegex 10 tablet (RU2) */
		.callback = byt_rt5672_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "AEGEX"),
			DMI_MATCH(DMI_PRODUCT_VERSION, "RU2"),
		},
	},
	{
		/* Dell Venue 10 Pro 5055 */
		.callback = byt_rt5672_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Venue 10 Pro 5055"),
		},
	},
	{ }
};

/* Various devices use an ACPI id of 10EC5640 while using a rt5672 codec */
static struct snd_soc_acpi_mach byt_rt5672 = {
	.id = "10EC5640",
	.drv_name = "cht-bsw-rt5672",
	.fw_filename = "intel/fw_sst_0f28.bin",
	.board = "cht-bsw",
	.sof_fw_filename = "sof-byt.ri",
	.sof_tplg_filename = "sof-byt-rt5670.tplg",
};

static struct snd_soc_acpi_mach byt_pov_p1006w = {
	.id = "10EC5640",
	.drv_name = "bytcr_rt5651",
	.fw_filename = "intel/fw_sst_0f28.bin",
	.board = "bytcr_rt5651",
	.sof_fw_filename = "sof-byt.ri",
	.sof_tplg_filename = "sof-byt-rt5651.tplg",
};

static struct snd_soc_acpi_mach *byt_quirk(void *arg)
{
	struct snd_soc_acpi_mach *mach = arg;

	dmi_check_system(byt_table);

	switch (byt_machine_id) {
	case BYT_RT5672:
		return &byt_rt5672;
	case BYT_POV_P1006W:
		return &byt_pov_p1006w;
	default:
		return mach;
	}
}

struct snd_soc_acpi_mach  snd_soc_acpi_intel_baytrail_machines[] = {
	{
		.id = "10EC5640",
		.drv_name = "bytcr_rt5640",
		.fw_filename = "intel/fw_sst_0f28.bin",
		.board = "bytcr_rt5640",
		.machine_quirk = byt_quirk,
		.sof_fw_filename = "sof-byt.ri",
		.sof_tplg_filename = "sof-byt-rt5640.tplg",
	},
	{
		.id = "10EC5642",
		.drv_name = "bytcr_rt5640",
		.fw_filename = "intel/fw_sst_0f28.bin",
		.board = "bytcr_rt5640",
		.sof_fw_filename = "sof-byt.ri",
		.sof_tplg_filename = "sof-byt-rt5640.tplg",
	},
	{
		.id = "INTCCFFD",
		.drv_name = "bytcr_rt5640",
		.fw_filename = "intel/fw_sst_0f28.bin",
		.board = "bytcr_rt5640",
		.sof_fw_filename = "sof-byt.ri",
		.sof_tplg_filename = "sof-byt-rt5640.tplg",
	},
	{
		.id = "10EC5651",
		.drv_name = "bytcr_rt5651",
		.fw_filename = "intel/fw_sst_0f28.bin",
		.board = "bytcr_rt5651",
		.sof_fw_filename = "sof-byt.ri",
		.sof_tplg_filename = "sof-byt-rt5651.tplg",
	},
	{
		.id = "WM510204",
		.drv_name = "bytcr_wm5102",
		.fw_filename = "intel/fw_sst_0f28.bin",
		.board = "bytcr_wm5102",
		.sof_fw_filename = "sof-byt.ri",
		.sof_tplg_filename = "sof-byt-wm5102.tplg",
	},
	{
		.id = "WM510205",
		.drv_name = "bytcr_wm5102",
		.fw_filename = "intel/fw_sst_0f28.bin",
		.board = "bytcr_wm5102",
		.sof_fw_filename = "sof-byt.ri",
		.sof_tplg_filename = "sof-byt-wm5102.tplg",
	},
	{
		.id = "DLGS7212",
		.drv_name = "bytcht_da7213",
		.fw_filename = "intel/fw_sst_0f28.bin",
		.board = "bytcht_da7213",
		.sof_fw_filename = "sof-byt.ri",
		.sof_tplg_filename = "sof-byt-da7213.tplg",
	},
	{
		.id = "DLGS7213",
		.drv_name = "bytcht_da7213",
		.fw_filename = "intel/fw_sst_0f28.bin",
		.board = "bytcht_da7213",
		.sof_fw_filename = "sof-byt.ri",
		.sof_tplg_filename = "sof-byt-da7213.tplg",
	},
	{
		.id = "ESSX8316",
		.drv_name = "bytcht_es8316",
		.fw_filename = "intel/fw_sst_0f28.bin",
		.board = "bytcht_es8316",
		.sof_fw_filename = "sof-byt.ri",
		.sof_tplg_filename = "sof-byt-es8316.tplg",
	},
	{
		.id = "10EC5682",
		.drv_name = "sof_rt5682",
		.sof_fw_filename = "sof-byt.ri",
		.sof_tplg_filename = "sof-byt-rt5682.tplg",
	},
	/* some Baytrail platforms rely on RT5645, use CHT machine driver */
	{
		.id = "10EC5645",
		.drv_name = "cht-bsw-rt5645",
		.fw_filename = "intel/fw_sst_0f28.bin",
		.board = "cht-bsw",
		.sof_fw_filename = "sof-byt.ri",
		.sof_tplg_filename = "sof-byt-rt5645.tplg",
	},
	{
		.id = "10EC5648",
		.drv_name = "cht-bsw-rt5645",
		.fw_filename = "intel/fw_sst_0f28.bin",
		.board = "cht-bsw",
		.sof_fw_filename = "sof-byt.ri",
		.sof_tplg_filename = "sof-byt-rt5645.tplg",
	},
	/* use CHT driver to Baytrail Chromebooks */
	{
		.id = "193C9890",
		.drv_name = "cht-bsw-max98090",
		.fw_filename = "intel/fw_sst_0f28.bin",
		.board = "cht-bsw",
		.sof_fw_filename = "sof-byt.ri",
		.sof_tplg_filename = "sof-byt-max98090.tplg",
	},
	{
		.id = "14F10720",
		.drv_name = "bytcht_cx2072x",
		.fw_filename = "intel/fw_sst_0f28.bin",
		.board = "bytcht_cx2072x",
		.sof_fw_filename = "sof-byt.ri",
		.sof_tplg_filename = "sof-byt-cx2072x.tplg",
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
