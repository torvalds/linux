/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright(c) 2023 Intel Corporation.
 */

#ifndef __SOF_SSP_COMMON_H
#define __SOF_SSP_COMMON_H

/* Cirrus Logic */
#define CS35L41_ACPI_HID	"CSC3541"
#define CS42L42_ACPI_HID	"10134242"

/* Dialog */
#define DA7219_ACPI_HID		"DLGS7219"

/* Everest */
#define ES8316_ACPI_HID		"ESSX8316"
#define ES8326_ACPI_HID		"ESSX8326"
#define ES8336_ACPI_HID		"ESSX8336"

#define MAX_98357A_ACPI_HID	"MX98357A"
#define MAX_98360A_ACPI_HID	"MX98360A"
#define MAX_98373_ACPI_HID	"MX98373"
#define MAX_98390_ACPI_HID	"MX98390"

/* Nuvoton */
#define NAU8318_ACPI_HID	"NVTN2012"
#define NAU8825_ACPI_HID	"10508825"

/* Realtek */
#define RT1011_ACPI_HID		"10EC1011"
#define RT1015_ACPI_HID		"10EC1015"
#define RT1015P_ACPI_HID	"RTL1015"
#define RT1019P_ACPI_HID	"RTL1019"
#define RT1308_ACPI_HID		"10EC1308"
#define RT5650_ACPI_HID		"10EC5650"
#define RT5682_ACPI_HID		"10EC5682"
#define RT5682S_ACPI_HID	"RTL5682"

enum sof_ssp_codec {
	CODEC_NONE,

	/* headphone codec */
	CODEC_CS42L42,
	CODEC_DA7219,
	CODEC_ES8316,
	CODEC_ES8326,
	CODEC_ES8336,
	CODEC_NAU8825,
	CODEC_RT5650,
	CODEC_RT5682,
	CODEC_RT5682S,

	/* speaker amplifier */
	CODEC_CS35L41,
	CODEC_MAX98357A,
	CODEC_MAX98360A,
	CODEC_MAX98373,
	CODEC_MAX98390,
	CODEC_NAU8318,
	CODEC_RT1011,
	CODEC_RT1015,
	CODEC_RT1015P,
	CODEC_RT1019P,
	CODEC_RT1308,
};

enum sof_ssp_codec sof_ssp_detect_codec_type(struct device *dev);
enum sof_ssp_codec sof_ssp_detect_amp_type(struct device *dev);

#endif /* __SOF_SSP_COMMON_H */
