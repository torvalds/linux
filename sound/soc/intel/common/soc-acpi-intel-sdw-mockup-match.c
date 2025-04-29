// SPDX-License-Identifier: GPL-2.0-only
//
// soc-acpi-intel-sdw-mockup-match.c - tables and support for SoundWire
// mockup device ACPI enumeration.
//
// Copyright (c) 2021, Intel Corporation.
//

#include <sound/soc-acpi.h>
#include <sound/soc-acpi-intel-match.h>
#include "soc-acpi-intel-sdw-mockup-match.h"

static const struct snd_soc_acpi_endpoint sdw_mockup_single_endpoint = {
	.num = 0,
	.aggregated = 0,
	.group_position = 0,
	.group_id = 0,
};

static const struct snd_soc_acpi_endpoint sdw_mockup_l_endpoint = {
	.num = 0,
	.aggregated = 1,
	.group_position = 0,
	.group_id = 1,
};

static const struct snd_soc_acpi_endpoint sdw_mockup_r_endpoint = {
	.num = 0,
	.aggregated = 1,
	.group_position = 1,
	.group_id = 1,
};

static const struct snd_soc_acpi_endpoint jack_amp_g1_dmic_endpoints[] = {
	/* Jack Endpoint */
	{
		.num = 0,
		.aggregated = 0,
		.group_position = 0,
		.group_id = 0,
	},
	/* Amp Endpoint, work as spk_l_endpoint */
	{
		.num = 1,
		.aggregated = 1,
		.group_position = 0,
		.group_id = 1,
	},
	/* DMIC Endpoint */
	{
		.num = 2,
		.aggregated = 0,
		.group_position = 0,
		.group_id = 0,
	},
};

static const struct snd_soc_acpi_adr_device sdw_mockup_headset_0_adr[] = {
	{
		.adr = 0x0000000105AA5500ull,
		.num_endpoints = 1,
		.endpoints = &sdw_mockup_single_endpoint,
		.name_prefix = "sdw_mockup_headset0"
	}
};

static const struct snd_soc_acpi_adr_device sdw_mockup_headset_1_adr[] = {
	{
		.adr = 0x0001000105AA5500ull,
		.num_endpoints = 1,
		.endpoints = &sdw_mockup_single_endpoint,
		.name_prefix = "sdw_mockup_headset1"
	}
};

static const struct snd_soc_acpi_adr_device sdw_mockup_amp_1_adr[] = {
	{
		.adr = 0x000100010555AA00ull,
		.num_endpoints = 1,
		.endpoints = &sdw_mockup_single_endpoint,
		.name_prefix = "sdw_mockup_amp1"
	}
};

static const struct snd_soc_acpi_adr_device sdw_mockup_amp_2_adr[] = {
	{
		.adr = 0x000200010555AA00ull,
		.num_endpoints = 1,
		.endpoints = &sdw_mockup_single_endpoint,
		.name_prefix = "sdw_mockup_amp2"
	}
};

static const struct snd_soc_acpi_adr_device sdw_mockup_mic_0_adr[] = {
	{
		.adr = 0x0000000105555500ull,
		.num_endpoints = 1,
		.endpoints = &sdw_mockup_single_endpoint,
		.name_prefix = "sdw_mockup_mic0"
	}
};

static const struct snd_soc_acpi_adr_device sdw_mockup_mic_3_adr[] = {
	{
		.adr = 0x0003000105555500ull,
		.num_endpoints = 1,
		.endpoints = &sdw_mockup_single_endpoint,
		.name_prefix = "sdw_mockup_mic3"
	}
};

static const struct snd_soc_acpi_adr_device sdw_mockup_amp_1_group1_adr[] = {
	{
		.adr = 0x000100010555AA00ull,
		.num_endpoints = 1,
		.endpoints = &sdw_mockup_l_endpoint,
		.name_prefix = "sdw_mockup_amp1_l"
	}
};

static const struct snd_soc_acpi_adr_device sdw_mockup_amp_2_group1_adr[] = {
	{
		.adr = 0x000200010555AA00ull,
		.num_endpoints = 1,
		.endpoints = &sdw_mockup_r_endpoint,
		.name_prefix = "sdw_mockup_amp2_r"
	}
};

static const struct snd_soc_acpi_adr_device sdw_mockup_multi_function_adr[] = {
	{
		.adr = 0x0000000105AAAA01ull,
		.num_endpoints = ARRAY_SIZE(jack_amp_g1_dmic_endpoints),
		.endpoints = jack_amp_g1_dmic_endpoints,
		.name_prefix = "sdw_mockup_mmulti-function"
	}
};

const struct snd_soc_acpi_link_adr sdw_mockup_headset_1amp_mic[] = {
	{
		.mask = BIT(0),
		.num_adr = ARRAY_SIZE(sdw_mockup_headset_0_adr),
		.adr_d = sdw_mockup_headset_0_adr,
	},
	{
		.mask = BIT(1),
		.num_adr = ARRAY_SIZE(sdw_mockup_amp_1_adr),
		.adr_d = sdw_mockup_amp_1_adr,
	},
	{
		.mask = BIT(3),
		.num_adr = ARRAY_SIZE(sdw_mockup_mic_3_adr),
		.adr_d = sdw_mockup_mic_3_adr,
	},
	{}
};

const struct snd_soc_acpi_link_adr sdw_mockup_headset_2amps_mic[] = {
	{
		.mask = BIT(0),
		.num_adr = ARRAY_SIZE(sdw_mockup_headset_0_adr),
		.adr_d = sdw_mockup_headset_0_adr,
	},
	{
		.mask = BIT(1),
		.num_adr = ARRAY_SIZE(sdw_mockup_amp_1_group1_adr),
		.adr_d = sdw_mockup_amp_1_group1_adr,
	},
	{
		.mask = BIT(2),
		.num_adr = ARRAY_SIZE(sdw_mockup_amp_2_group1_adr),
		.adr_d = sdw_mockup_amp_2_group1_adr,
	},
	{
		.mask = BIT(3),
		.num_adr = ARRAY_SIZE(sdw_mockup_mic_3_adr),
		.adr_d = sdw_mockup_mic_3_adr,
	},
	{}
};

const struct snd_soc_acpi_link_adr sdw_mockup_mic_headset_1amp[] = {
	{
		.mask = BIT(1),
		.num_adr = ARRAY_SIZE(sdw_mockup_headset_1_adr),
		.adr_d = sdw_mockup_headset_1_adr,
	},
	{
		.mask = BIT(2),
		.num_adr = ARRAY_SIZE(sdw_mockup_amp_2_adr),
		.adr_d = sdw_mockup_amp_2_adr,
	},
	{
		.mask = BIT(0),
		.num_adr = ARRAY_SIZE(sdw_mockup_mic_0_adr),
		.adr_d = sdw_mockup_mic_0_adr,
	},
	{}
};

const struct snd_soc_acpi_link_adr sdw_mockup_multi_func[] = {
	{
		.mask = BIT(0),
		.num_adr = ARRAY_SIZE(sdw_mockup_multi_function_adr),
		.adr_d = sdw_mockup_multi_function_adr,
	},
	{}
};
