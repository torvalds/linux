// SPDX-License-Identifier: GPL-2.0-only
/*
 * soc-acpi-intel-cht-match.c - tables and support for CHT ACPI enumeration.
 *
 * Copyright (c) 2017, Intel Corporation.
 */

#include <linux/dmi.h>
#include <sound/soc-acpi.h>
#include <sound/soc-acpi-intel-match.h>

static unsigned long cht_machine_id;

#define CHT_SURFACE_MACH 1

static int cht_surface_quirk_cb(const struct dmi_system_id *id)
{
	cht_machine_id = CHT_SURFACE_MACH;
	return 1;
}

static const struct dmi_system_id cht_table[] = {
	{
		.callback = cht_surface_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Microsoft Corporation"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Surface 3"),
		},
	},
	{ }
};

static struct snd_soc_acpi_mach cht_surface_mach = {
	.id = "10EC5640",
	.drv_name = "cht-bsw-rt5645",
	.fw_filename = "intel/fw_sst_22a8.bin",
	.board = "cht-bsw",
	.sof_tplg_filename = "sof-cht-rt5645.tplg",
};

static struct snd_soc_acpi_mach *cht_quirk(void *arg)
{
	struct snd_soc_acpi_mach *mach = arg;

	dmi_check_system(cht_table);

	if (cht_machine_id == CHT_SURFACE_MACH)
		return &cht_surface_mach;
	else
		return mach;
}

/*
 * Some tablets with Android factory OS have buggy DSDTs with an ESSX8316 device
 * in the ACPI tables. While they are not using an ESS8316 codec. These DSDTs
 * also have an ACPI device for the correct codec, ignore the ESSX8316.
 */
static const struct dmi_system_id cht_ess8316_not_present_table[] = {
	{
		/* Nextbook Ares 8A */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Insyde"),
			DMI_MATCH(DMI_PRODUCT_NAME, "CherryTrail"),
			DMI_MATCH(DMI_BIOS_VERSION, "M882"),
		},
	},
	{ }
};

static struct snd_soc_acpi_mach *cht_ess8316_quirk(void *arg)
{
	if (dmi_check_system(cht_ess8316_not_present_table))
		return NULL;

	return arg;
}

/*
 * The Lenovo Yoga Tab 3 Pro YT3-X90, with Android factory OS has a buggy DSDT
 * with the coded not being listed at all.
 */
static const struct dmi_system_id lenovo_yoga_tab3_x90[] = {
	{
		/* Lenovo Yoga Tab 3 Pro YT3-X90, codec missing from DSDT */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Intel Corporation"),
			DMI_MATCH(DMI_PRODUCT_VERSION, "Blade3-10A-001"),
		},
	},
	{ }
};

static struct snd_soc_acpi_mach cht_lenovo_yoga_tab3_x90_mach = {
	.id = "10WM5102",
	.drv_name = "bytcr_wm5102",
	.fw_filename = "intel/fw_sst_22a8.bin",
	.board = "bytcr_wm5102",
	.sof_tplg_filename = "sof-cht-wm5102.tplg",
};

static struct snd_soc_acpi_mach *lenovo_yt3_x90_quirk(void *arg)
{
	if (dmi_check_system(lenovo_yoga_tab3_x90))
		return &cht_lenovo_yoga_tab3_x90_mach;

	/* Skip wildcard match snd_soc_acpi_intel_cherrytrail_machines[] entry */
	return NULL;
}

static const struct snd_soc_acpi_codecs rt5640_comp_ids = {
	.num_codecs = 2,
	.codecs = { "10EC5640", "10EC3276" },
};

static const struct snd_soc_acpi_codecs rt5670_comp_ids = {
	.num_codecs = 2,
	.codecs = { "10EC5670", "10EC5672" },
};

static const struct snd_soc_acpi_codecs rt5645_comp_ids = {
	.num_codecs = 3,
	.codecs = { "10EC5645", "10EC5650", "10EC3270" },
};

static const struct snd_soc_acpi_codecs da7213_comp_ids = {
	.num_codecs = 2,
	.codecs = { "DGLS7212", "DGLS7213"},

};

/* Cherryview-based platforms: CherryTrail and Braswell */
struct snd_soc_acpi_mach  snd_soc_acpi_intel_cherrytrail_machines[] = {
	{
		.comp_ids = &rt5670_comp_ids,
		.drv_name = "cht-bsw-rt5672",
		.fw_filename = "intel/fw_sst_22a8.bin",
		.board = "cht-bsw",
		.sof_tplg_filename = "sof-cht-rt5670.tplg",
	},
	{
		.comp_ids = &rt5645_comp_ids,
		.drv_name = "cht-bsw-rt5645",
		.fw_filename = "intel/fw_sst_22a8.bin",
		.board = "cht-bsw",
		.sof_tplg_filename = "sof-cht-rt5645.tplg",
	},
	{
		.id = "193C9890",
		.drv_name = "cht-bsw-max98090",
		.fw_filename = "intel/fw_sst_22a8.bin",
		.board = "cht-bsw",
		.sof_tplg_filename = "sof-cht-max98090.tplg",
	},
	{
		.id = "10508824",
		.drv_name = "cht-bsw-nau8824",
		.fw_filename = "intel/fw_sst_22a8.bin",
		.board = "cht-bsw",
		.sof_tplg_filename = "sof-cht-nau8824.tplg",
	},
	{
		.comp_ids = &da7213_comp_ids,
		.drv_name = "bytcht_da7213",
		.fw_filename = "intel/fw_sst_22a8.bin",
		.board = "bytcht_da7213",
		.sof_tplg_filename = "sof-cht-da7213.tplg",
	},
	{
		.id = "ESSX8316",
		.drv_name = "bytcht_es8316",
		.fw_filename = "intel/fw_sst_22a8.bin",
		.board = "bytcht_es8316",
		.machine_quirk = cht_ess8316_quirk,
		.sof_tplg_filename = "sof-cht-es8316.tplg",
	},
	/* some CHT-T platforms rely on RT5640, use Baytrail machine driver */
	{
		.comp_ids = &rt5640_comp_ids,
		.drv_name = "bytcr_rt5640",
		.fw_filename = "intel/fw_sst_22a8.bin",
		.board = "bytcr_rt5640",
		.machine_quirk = cht_quirk,
		.sof_tplg_filename = "sof-cht-rt5640.tplg",
	},
	{
		.id = "10EC5682",
		.drv_name = "sof_rt5682",
		.sof_tplg_filename = "sof-cht-rt5682.tplg",
	},
	/* some CHT-T platforms rely on RT5651, use Baytrail machine driver */
	{
		.id = "10EC5651",
		.drv_name = "bytcr_rt5651",
		.fw_filename = "intel/fw_sst_22a8.bin",
		.board = "bytcr_rt5651",
		.sof_tplg_filename = "sof-cht-rt5651.tplg",
	},
	{
		.id = "14F10720",
		.drv_name = "bytcht_cx2072x",
		.fw_filename = "intel/fw_sst_22a8.bin",
		.board = "bytcht_cx2072x",
		.sof_tplg_filename = "sof-cht-cx2072x.tplg",
	},
	{
		.id = "104C5122",
		.drv_name = "sof_pcm512x",
		.sof_tplg_filename = "sof-cht-src-50khz-pcm512x.tplg",
	},
	/*
	 * Special case for the Lenovo Yoga Tab 3 Pro YT3-X90 where the DSDT
	 * misses the codec. Match on the SST id instead, lenovo_yt3_x90_quirk()
	 * will return a YT3 specific mach or NULL when called on other hw,
	 * skipping this entry.
	 */
	{
		.id = "808622A8",
		.machine_quirk = lenovo_yt3_x90_quirk,
	},

#if IS_ENABLED(CONFIG_SND_SOC_INTEL_BYT_CHT_NOCODEC_MACH)
	/*
	 * This is always last in the table so that it is selected only when
	 * enabled explicitly and there is no codec-related information in SSDT
	 */
	{
		.id = "808622A8",
		.drv_name = "bytcht_nocodec",
		.fw_filename = "intel/fw_sst_22a8.bin",
		.board = "bytcht_nocodec",
	},
#endif
	{},
};
EXPORT_SYMBOL_GPL(snd_soc_acpi_intel_cherrytrail_machines);
