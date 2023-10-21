// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright(c) 2023 Intel Corporation. All rights reserved.

#include <linux/device.h>
#include <sound/soc-acpi.h>
#include "sof_ssp_common.h"

/*
 * Codec probe function
 */
#define CODEC_MAP_ENTRY(n, h, t)	\
	{				\
		.name = n,		\
		.acpi_hid = h,		\
		.codec_type = t,	\
	}

struct codec_map {
	const char *name;
	const char *acpi_hid;
	enum sof_ssp_codec codec_type;
};

static const struct codec_map codecs[] = {
	/* Cirrus Logic */
	CODEC_MAP_ENTRY("CS42L42", CS42L42_ACPI_HID, CODEC_CS42L42),

	/* Dialog */
	CODEC_MAP_ENTRY("DA7219", DA7219_ACPI_HID, CODEC_DA7219),

	/* Everest */
	CODEC_MAP_ENTRY("ES8316", ES8316_ACPI_HID, CODEC_ES8316),
	CODEC_MAP_ENTRY("ES8326", ES8326_ACPI_HID, CODEC_ES8326),
	CODEC_MAP_ENTRY("ES8336", ES8336_ACPI_HID, CODEC_ES8336),

	/* Nuvoton */
	CODEC_MAP_ENTRY("NAU8825", NAU8825_ACPI_HID, CODEC_NAU8825),

	/* Realtek */
	CODEC_MAP_ENTRY("RT5650", RT5650_ACPI_HID, CODEC_RT5650),
	CODEC_MAP_ENTRY("RT5682", RT5682_ACPI_HID, CODEC_RT5682),
	CODEC_MAP_ENTRY("RT5682S", RT5682S_ACPI_HID, CODEC_RT5682S),
};

static const struct codec_map amps[] = {
	/* Cirrus Logic */
	CODEC_MAP_ENTRY("CS35L41", CS35L41_ACPI_HID, CODEC_CS35L41),

	/* Maxim */
	CODEC_MAP_ENTRY("MAX98357A", MAX_98357A_ACPI_HID, CODEC_MAX98357A),
	CODEC_MAP_ENTRY("MAX98360A", MAX_98360A_ACPI_HID, CODEC_MAX98360A),
	CODEC_MAP_ENTRY("MAX98373", MAX_98373_ACPI_HID, CODEC_MAX98373),
	CODEC_MAP_ENTRY("MAX98390", MAX_98390_ACPI_HID, CODEC_MAX98390),

	/* Nuvoton */
	CODEC_MAP_ENTRY("NAU8318", NAU8318_ACPI_HID, CODEC_NAU8318),

	/* Realtek */
	CODEC_MAP_ENTRY("RT1011", RT1011_ACPI_HID, CODEC_RT1011),
	CODEC_MAP_ENTRY("RT1015", RT1015_ACPI_HID, CODEC_RT1015),
	CODEC_MAP_ENTRY("RT1015P", RT1015P_ACPI_HID, CODEC_RT1015P),
	CODEC_MAP_ENTRY("RT1019P", RT1019P_ACPI_HID, CODEC_RT1019P),
	CODEC_MAP_ENTRY("RT1308", RT1308_ACPI_HID, CODEC_RT1308),
};

enum sof_ssp_codec sof_ssp_detect_codec_type(struct device *dev)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(codecs); i++) {
		if (!acpi_dev_present(codecs[i].acpi_hid, NULL, -1))
			continue;

		dev_dbg(dev, "codec %s found\n", codecs[i].name);
		return codecs[i].codec_type;
	}

	return CODEC_NONE;
}
EXPORT_SYMBOL_NS(sof_ssp_detect_codec_type, SND_SOC_INTEL_SOF_SSP_COMMON);

enum sof_ssp_codec sof_ssp_detect_amp_type(struct device *dev)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(amps); i++) {
		if (!acpi_dev_present(amps[i].acpi_hid, NULL, -1))
			continue;

		dev_dbg(dev, "amp %s found\n", amps[i].name);
		return amps[i].codec_type;
	}

	return CODEC_NONE;
}
EXPORT_SYMBOL_NS(sof_ssp_detect_amp_type, SND_SOC_INTEL_SOF_SSP_COMMON);

MODULE_DESCRIPTION("ASoC Intel SOF Common Machine Driver Helpers");
MODULE_AUTHOR("Brent Lu <brent.lu@intel.com>");
MODULE_LICENSE("GPL");
