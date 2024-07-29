// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright(c) 2023 Intel Corporation

#include <linux/device.h>
#include <sound/soc-acpi.h>
#include <sound/soc-acpi-intel-ssp-common.h>

/*
 * Codec probe function
 */
#define CODEC_MAP_ENTRY(n, s, h, t)	\
	{				\
		.name = n,		\
		.tplg_suffix = s,	\
		.acpi_hid = h,		\
		.codec_type = t,	\
	}

struct codec_map {
	const char *name;
	const char *tplg_suffix;
	const char *acpi_hid;
	enum snd_soc_acpi_intel_codec codec_type;
};

static const struct codec_map codecs[] = {
	/* Cirrus Logic */
	CODEC_MAP_ENTRY("CS42L42", "cs42l42", CS42L42_ACPI_HID, CODEC_CS42L42),

	/* Dialog */
	CODEC_MAP_ENTRY("DA7219", "da7219", DA7219_ACPI_HID, CODEC_DA7219),

	/* Everest */
	CODEC_MAP_ENTRY("ES8316", "es8336", ES8316_ACPI_HID, CODEC_ES8316),
	CODEC_MAP_ENTRY("ES8326", "es8336", ES8326_ACPI_HID, CODEC_ES8326),
	CODEC_MAP_ENTRY("ES8336", "es8336", ES8336_ACPI_HID, CODEC_ES8336),

	/* Nuvoton */
	CODEC_MAP_ENTRY("NAU8825", "nau8825", NAU8825_ACPI_HID, CODEC_NAU8825),

	/* Realtek */
	CODEC_MAP_ENTRY("RT5650", "rt5650", RT5650_ACPI_HID, CODEC_RT5650),
	CODEC_MAP_ENTRY("RT5682", "rt5682", RT5682_ACPI_HID, CODEC_RT5682),
	CODEC_MAP_ENTRY("RT5682S", "rt5682", RT5682S_ACPI_HID, CODEC_RT5682S),
};

static const struct codec_map amps[] = {
	/* Cirrus Logic */
	CODEC_MAP_ENTRY("CS35L41", "cs35l41", CS35L41_ACPI_HID, CODEC_CS35L41),

	/* Maxim */
	CODEC_MAP_ENTRY("MAX98357A", "max98357a", MAX_98357A_ACPI_HID, CODEC_MAX98357A),
	CODEC_MAP_ENTRY("MAX98360A", "max98360a", MAX_98360A_ACPI_HID, CODEC_MAX98360A),
	CODEC_MAP_ENTRY("MAX98373", "max98373", MAX_98373_ACPI_HID, CODEC_MAX98373),
	CODEC_MAP_ENTRY("MAX98390", "max98390", MAX_98390_ACPI_HID, CODEC_MAX98390),

	/* Nuvoton */
	CODEC_MAP_ENTRY("NAU8318", "nau8318", NAU8318_ACPI_HID, CODEC_NAU8318),

	/* Realtek */
	CODEC_MAP_ENTRY("RT1011", "rt1011", RT1011_ACPI_HID, CODEC_RT1011),
	CODEC_MAP_ENTRY("RT1015", "rt1015", RT1015_ACPI_HID, CODEC_RT1015),
	CODEC_MAP_ENTRY("RT1015P", "rt1015", RT1015P_ACPI_HID, CODEC_RT1015P),
	CODEC_MAP_ENTRY("RT1019P", "rt1019", RT1019P_ACPI_HID, CODEC_RT1019P),
	CODEC_MAP_ENTRY("RT1308", "rt1308", RT1308_ACPI_HID, CODEC_RT1308),

	/*
	 * Monolithic components
	 *
	 * Only put components that can serve as both the amp and the codec below this line.
	 * This will ensure that if the part is used just as a codec and there is an amp as well
	 * then the amp will be selected properly.
	 */
	CODEC_MAP_ENTRY("RT5650", "rt5650", RT5650_ACPI_HID, CODEC_RT5650),
};

enum snd_soc_acpi_intel_codec
snd_soc_acpi_intel_detect_codec_type(struct device *dev)
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
EXPORT_SYMBOL_NS(snd_soc_acpi_intel_detect_codec_type, SND_SOC_ACPI_INTEL_MATCH);

enum snd_soc_acpi_intel_codec
snd_soc_acpi_intel_detect_amp_type(struct device *dev)
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
EXPORT_SYMBOL_NS(snd_soc_acpi_intel_detect_amp_type, SND_SOC_ACPI_INTEL_MATCH);

const char *
snd_soc_acpi_intel_get_codec_name(enum snd_soc_acpi_intel_codec codec_type)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(codecs); i++) {
		if (codecs[i].codec_type != codec_type)
			continue;

		return codecs[i].name;
	}
	for (i = 0; i < ARRAY_SIZE(amps); i++) {
		if (amps[i].codec_type != codec_type)
			continue;

		return amps[i].name;
	}

	return NULL;
}
EXPORT_SYMBOL_NS(snd_soc_acpi_intel_get_codec_name, SND_SOC_ACPI_INTEL_MATCH);

const char *
snd_soc_acpi_intel_get_codec_tplg_suffix(enum snd_soc_acpi_intel_codec codec_type)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(codecs); i++) {
		if (codecs[i].codec_type != codec_type)
			continue;

		return codecs[i].tplg_suffix;
	}

	return NULL;
}
EXPORT_SYMBOL_NS(snd_soc_acpi_intel_get_codec_tplg_suffix, SND_SOC_ACPI_INTEL_MATCH);

const char *
snd_soc_acpi_intel_get_amp_tplg_suffix(enum snd_soc_acpi_intel_codec codec_type)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(amps); i++) {
		if (amps[i].codec_type != codec_type)
			continue;

		return amps[i].tplg_suffix;
	}

	return NULL;
}
EXPORT_SYMBOL_NS(snd_soc_acpi_intel_get_amp_tplg_suffix, SND_SOC_ACPI_INTEL_MATCH);

MODULE_DESCRIPTION("ASoC Intel SOF Common Machine Driver Helpers");
MODULE_AUTHOR("Brent Lu <brent.lu@intel.com>");
MODULE_LICENSE("GPL");
