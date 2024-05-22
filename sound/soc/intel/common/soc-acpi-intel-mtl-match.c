// SPDX-License-Identifier: GPL-2.0-only
/*
 * soc-acpi-intel-mtl-match.c - tables and support for MTL ACPI enumeration.
 *
 * Copyright (c) 2022, Intel Corporation.
 *
 */

#include <sound/soc-acpi.h>
#include <sound/soc-acpi-intel-match.h>
#include <sound/soc-acpi-intel-ssp-common.h>
#include "soc-acpi-intel-sdw-mockup-match.h"

static const struct snd_soc_acpi_codecs mtl_rt5682_rt5682s_hp = {
	.num_codecs = 2,
	.codecs = {RT5682_ACPI_HID, RT5682S_ACPI_HID},
};

static const struct snd_soc_acpi_codecs mtl_essx_83x6 = {
	.num_codecs = 3,
	.codecs = { "ESSX8316", "ESSX8326", "ESSX8336"},
};

static const struct snd_soc_acpi_codecs mtl_lt6911_hdmi = {
	.num_codecs = 1,
	.codecs = {"INTC10B0"}
};

struct snd_soc_acpi_mach snd_soc_acpi_intel_mtl_machines[] = {
	{
		.comp_ids = &mtl_essx_83x6,
		.drv_name = "mtl_es83x6_c1_h02",
		.machine_quirk = snd_soc_acpi_codec_list,
		.quirk_data = &mtl_lt6911_hdmi,
		.sof_tplg_filename = "sof-mtl-es83x6-ssp1-hdmi-ssp02.tplg",
	},
	{
		.comp_ids = &mtl_essx_83x6,
		.drv_name = "sof-essx8336",
		.sof_tplg_filename = "sof-mtl-es8336", /* the tplg suffix is added at run time */
		.tplg_quirk_mask = SND_SOC_ACPI_TPLG_INTEL_SSP_NUMBER |
					SND_SOC_ACPI_TPLG_INTEL_SSP_MSB |
					SND_SOC_ACPI_TPLG_INTEL_DMIC_NUMBER,
	},
	/* place boards for each headphone codec: sof driver will complete the
	 * tplg name and machine driver will detect the amp type
	 */
	{
		.id = CS42L42_ACPI_HID,
		.drv_name = "mtl_cs42l42_def",
		.sof_tplg_filename = "sof-mtl", /* the tplg suffix is added at run time */
		.tplg_quirk_mask = SND_SOC_ACPI_TPLG_INTEL_AMP_NAME |
					SND_SOC_ACPI_TPLG_INTEL_CODEC_NAME,
	},
	{
		.id = DA7219_ACPI_HID,
		.drv_name = "mtl_da7219_def",
		.sof_tplg_filename = "sof-mtl", /* the tplg suffix is added at run time */
		.tplg_quirk_mask = SND_SOC_ACPI_TPLG_INTEL_AMP_NAME |
					SND_SOC_ACPI_TPLG_INTEL_CODEC_NAME,
	},
	{
		.id = NAU8825_ACPI_HID,
		.drv_name = "mtl_nau8825_def",
		.sof_tplg_filename = "sof-mtl", /* the tplg suffix is added at run time */
		.tplg_quirk_mask = SND_SOC_ACPI_TPLG_INTEL_AMP_NAME |
					SND_SOC_ACPI_TPLG_INTEL_CODEC_NAME,
	},
	{
		.id = RT5650_ACPI_HID,
		.drv_name = "mtl_rt5682_def",
		.sof_tplg_filename = "sof-mtl", /* the tplg suffix is added at run time */
		.tplg_quirk_mask = SND_SOC_ACPI_TPLG_INTEL_AMP_NAME |
					SND_SOC_ACPI_TPLG_INTEL_CODEC_NAME,
	},
	{
		.comp_ids = &mtl_rt5682_rt5682s_hp,
		.drv_name = "mtl_rt5682_def",
		.sof_tplg_filename = "sof-mtl", /* the tplg suffix is added at run time */
		.tplg_quirk_mask = SND_SOC_ACPI_TPLG_INTEL_AMP_NAME |
					SND_SOC_ACPI_TPLG_INTEL_CODEC_NAME,
	},
	/* place amp-only boards in the end of table */
	{
		.id = "INTC10B0",
		.drv_name = "mtl_lt6911_hdmi_ssp",
		.sof_tplg_filename = "sof-mtl-hdmi-ssp02.tplg",
	},
	{},
};
EXPORT_SYMBOL_GPL(snd_soc_acpi_intel_mtl_machines);

static const struct snd_soc_acpi_endpoint single_endpoint = {
	.num = 0,
	.aggregated = 0,
	.group_position = 0,
	.group_id = 0,
};

static const struct snd_soc_acpi_endpoint spk_l_endpoint = {
	.num = 0,
	.aggregated = 1,
	.group_position = 0,
	.group_id = 1,
};

static const struct snd_soc_acpi_endpoint spk_r_endpoint = {
	.num = 0,
	.aggregated = 1,
	.group_position = 1,
	.group_id = 1,
};

static const struct snd_soc_acpi_endpoint rt712_endpoints[] = {
	{
		.num = 0,
		.aggregated = 0,
		.group_position = 0,
		.group_id = 0,
	},
	{
		.num = 1,
		.aggregated = 0,
		.group_position = 0,
		.group_id = 0,
	},
};

/*
 * RT722 is a multi-function codec, three endpoints are created for
 * its headset, amp and dmic functions.
 */
static const struct snd_soc_acpi_endpoint rt722_endpoints[] = {
	{
		.num = 0,
		.aggregated = 0,
		.group_position = 0,
		.group_id = 0,
	},
	{
		.num = 1,
		.aggregated = 0,
		.group_position = 0,
		.group_id = 0,
	},
	{
		.num = 2,
		.aggregated = 0,
		.group_position = 0,
		.group_id = 0,
	},
};

static const struct snd_soc_acpi_endpoint spk_2_endpoint = {
	.num = 0,
	.aggregated = 1,
	.group_position = 2,
	.group_id = 1,
};

static const struct snd_soc_acpi_endpoint spk_3_endpoint = {
	.num = 0,
	.aggregated = 1,
	.group_position = 3,
	.group_id = 1,
};

static const struct snd_soc_acpi_adr_device rt711_sdca_0_adr[] = {
	{
		.adr = 0x000030025D071101ull,
		.num_endpoints = 1,
		.endpoints = &single_endpoint,
		.name_prefix = "rt711"
	}
};

static const struct snd_soc_acpi_adr_device rt712_0_single_adr[] = {
	{
		.adr = 0x000030025D071201ull,
		.num_endpoints = ARRAY_SIZE(rt712_endpoints),
		.endpoints = rt712_endpoints,
		.name_prefix = "rt712"
	}
};

static const struct snd_soc_acpi_adr_device rt1712_3_single_adr[] = {
	{
		.adr = 0x000330025D171201ull,
		.num_endpoints = 1,
		.endpoints = &single_endpoint,
		.name_prefix = "rt712-dmic"
	}
};

static const struct snd_soc_acpi_adr_device rt722_0_single_adr[] = {
	{
		.adr = 0x000030025d072201ull,
		.num_endpoints = ARRAY_SIZE(rt722_endpoints),
		.endpoints = rt722_endpoints,
		.name_prefix = "rt722"
	}
};

static const struct snd_soc_acpi_adr_device rt713_0_single_adr[] = {
	{
		.adr = 0x000031025D071301ull,
		.num_endpoints = 1,
		.endpoints = &single_endpoint,
		.name_prefix = "rt713"
	}
};

static const struct snd_soc_acpi_adr_device rt1713_3_single_adr[] = {
	{
		.adr = 0x000331025D171301ull,
		.num_endpoints = 1,
		.endpoints = &single_endpoint,
		.name_prefix = "rt713-dmic"
	}
};

static const struct snd_soc_acpi_adr_device mx8373_0_adr[] = {
	{
		.adr = 0x000023019F837300ull,
		.num_endpoints = 1,
		.endpoints = &spk_l_endpoint,
		.name_prefix = "Left"
	},
	{
		.adr = 0x000027019F837300ull,
		.num_endpoints = 1,
		.endpoints = &spk_r_endpoint,
		.name_prefix = "Right"
	}
};

static const struct snd_soc_acpi_adr_device rt5682_2_adr[] = {
	{
		.adr = 0x000221025D568200ull,
		.num_endpoints = 1,
		.endpoints = &single_endpoint,
		.name_prefix = "rt5682"
	}
};

static const struct snd_soc_acpi_adr_device rt1316_2_group1_adr[] = {
	{
		.adr = 0x000230025D131601ull,
		.num_endpoints = 1,
		.endpoints = &spk_l_endpoint,
		.name_prefix = "rt1316-1"
	}
};

static const struct snd_soc_acpi_adr_device rt1316_3_group1_adr[] = {
	{
		.adr = 0x000331025D131601ull,
		.num_endpoints = 1,
		.endpoints = &spk_r_endpoint,
		.name_prefix = "rt1316-2"
	}
};

static const struct snd_soc_acpi_adr_device rt1316_1_group2_adr[] = {
	{
		.adr = 0x000131025D131601ull,
		.num_endpoints = 1,
		.endpoints = &spk_l_endpoint,
		.name_prefix = "rt1316-1"
	}
};

static const struct snd_soc_acpi_adr_device rt1316_2_group2_adr[] = {
	{
		.adr = 0x000230025D131601ull,
		.num_endpoints = 1,
		.endpoints = &spk_r_endpoint,
		.name_prefix = "rt1316-2"
	}
};

static const struct snd_soc_acpi_adr_device rt1316_3_single_adr[] = {
	{
		.adr = 0x000330025D131601ull,
		.num_endpoints = 1,
		.endpoints = &single_endpoint,
		.name_prefix = "rt1316-1"
	}
};

static const struct snd_soc_acpi_adr_device rt1318_1_single_adr[] = {
	{
		.adr = 0x000130025D131801,
		.num_endpoints = 1,
		.endpoints = &single_endpoint,
		.name_prefix = "rt1318"
	}
};

static const struct snd_soc_acpi_adr_device rt1318_1_group1_adr[] = {
	{
		.adr = 0x000130025D131801ull,
		.num_endpoints = 1,
		.endpoints = &spk_l_endpoint,
		.name_prefix = "rt1318-1"
	}
};

static const struct snd_soc_acpi_adr_device rt1318_2_group1_adr[] = {
	{
		.adr = 0x000232025D131801ull,
		.num_endpoints = 1,
		.endpoints = &spk_r_endpoint,
		.name_prefix = "rt1318-2"
	}
};

static const struct snd_soc_acpi_adr_device rt714_0_adr[] = {
	{
		.adr = 0x000030025D071401ull,
		.num_endpoints = 1,
		.endpoints = &single_endpoint,
		.name_prefix = "rt714"
	}
};

static const struct snd_soc_acpi_adr_device rt714_1_adr[] = {
	{
		.adr = 0x000130025D071401ull,
		.num_endpoints = 1,
		.endpoints = &single_endpoint,
		.name_prefix = "rt714"
	}
};

static const struct snd_soc_acpi_link_adr mtl_712_l0_1712_l3[] = {
	{
		.mask = BIT(0),
		.num_adr = ARRAY_SIZE(rt712_0_single_adr),
		.adr_d = rt712_0_single_adr,
	},
	{
		.mask = BIT(3),
		.num_adr = ARRAY_SIZE(rt1712_3_single_adr),
		.adr_d = rt1712_3_single_adr,
	},
	{}
};

static const struct snd_soc_acpi_link_adr mtl_712_l0[] = {
	{
		.mask = BIT(0),
		.num_adr = ARRAY_SIZE(rt712_0_single_adr),
		.adr_d = rt712_0_single_adr,
	},
	{}
};

static const struct snd_soc_acpi_endpoint cs42l43_endpoints[] = {
	{ /* Jack Playback Endpoint */
		.num = 0,
		.aggregated = 0,
		.group_position = 0,
		.group_id = 0,
	},
	{ /* DMIC Capture Endpoint */
		.num = 1,
		.aggregated = 0,
		.group_position = 0,
		.group_id = 0,
	},
	{ /* Jack Capture Endpoint */
		.num = 2,
		.aggregated = 0,
		.group_position = 0,
		.group_id = 0,
	},
	{ /* Speaker Playback Endpoint */
		.num = 3,
		.aggregated = 0,
		.group_position = 0,
		.group_id = 0,
	},
};

static const struct snd_soc_acpi_adr_device cs42l43_0_adr[] = {
	{
		.adr = 0x00003001FA424301ull,
		.num_endpoints = ARRAY_SIZE(cs42l43_endpoints),
		.endpoints = cs42l43_endpoints,
		.name_prefix = "cs42l43"
	}
};

static const struct snd_soc_acpi_adr_device cs35l56_1_adr[] = {
	{
		.adr = 0x00013701FA355601ull,
		.num_endpoints = 1,
		.endpoints = &spk_r_endpoint,
		.name_prefix = "AMP3"
	},
	{
		.adr = 0x00013601FA355601ull,
		.num_endpoints = 1,
		.endpoints = &spk_3_endpoint,
		.name_prefix = "AMP4"
	}
};

static const struct snd_soc_acpi_adr_device cs35l56_2_adr[] = {
	{
		.adr = 0x00023301FA355601ull,
		.num_endpoints = 1,
		.endpoints = &spk_l_endpoint,
		.name_prefix = "AMP1"
	},
	{
		.adr = 0x00023201FA355601ull,
		.num_endpoints = 1,
		.endpoints = &spk_2_endpoint,
		.name_prefix = "AMP2"
	}
};

static const struct snd_soc_acpi_adr_device cs35l56_2_r_adr[] = {
	{
		.adr = 0x00023201FA355601ull,
		.num_endpoints = 1,
		.endpoints = &spk_r_endpoint,
		.name_prefix = "AMP3"
	},
	{
		.adr = 0x00023301FA355601ull,
		.num_endpoints = 1,
		.endpoints = &spk_3_endpoint,
		.name_prefix = "AMP4"
	}

};

static const struct snd_soc_acpi_adr_device cs35l56_3_l_adr[] = {
	{
		.adr = 0x00033001fa355601ull,
		.num_endpoints = 1,
		.endpoints = &spk_l_endpoint,
		.name_prefix = "AMP1"
	},
	{
		.adr = 0x00033101fa355601ull,
		.num_endpoints = 1,
		.endpoints = &spk_2_endpoint,
		.name_prefix = "AMP2"
	}
};

static const struct snd_soc_acpi_link_adr rt5682_link2_max98373_link0[] = {
	/* Expected order: jack -> amp */
	{
		.mask = BIT(2),
		.num_adr = ARRAY_SIZE(rt5682_2_adr),
		.adr_d = rt5682_2_adr,
	},
	{
		.mask = BIT(0),
		.num_adr = ARRAY_SIZE(mx8373_0_adr),
		.adr_d = mx8373_0_adr,
	},
	{}
};

static const struct snd_soc_acpi_link_adr mtl_rvp[] = {
	{
		.mask = BIT(0),
		.num_adr = ARRAY_SIZE(rt711_sdca_0_adr),
		.adr_d = rt711_sdca_0_adr,
	},
	{}
};

static const struct snd_soc_acpi_link_adr mtl_rt722_only[] = {
	{
		.mask = BIT(0),
		.num_adr = ARRAY_SIZE(rt722_0_single_adr),
		.adr_d = rt722_0_single_adr,
	},
	{}
};

static const struct snd_soc_acpi_link_adr mtl_3_in_1_sdca[] = {
	{
		.mask = BIT(0),
		.num_adr = ARRAY_SIZE(rt711_sdca_0_adr),
		.adr_d = rt711_sdca_0_adr,
	},
	{
		.mask = BIT(2),
		.num_adr = ARRAY_SIZE(rt1316_2_group1_adr),
		.adr_d = rt1316_2_group1_adr,
	},
	{
		.mask = BIT(3),
		.num_adr = ARRAY_SIZE(rt1316_3_group1_adr),
		.adr_d = rt1316_3_group1_adr,
	},
	{
		.mask = BIT(1),
		.num_adr = ARRAY_SIZE(rt714_1_adr),
		.adr_d = rt714_1_adr,
	},
	{}
};

static const struct snd_soc_acpi_link_adr mtl_sdw_rt1318_l12_rt714_l0[] = {
	{
		.mask = BIT(1),
		.num_adr = ARRAY_SIZE(rt1318_1_group1_adr),
		.adr_d = rt1318_1_group1_adr,
	},
	{
		.mask = BIT(2),
		.num_adr = ARRAY_SIZE(rt1318_2_group1_adr),
		.adr_d = rt1318_2_group1_adr,
	},
	{
		.mask = BIT(0),
		.num_adr = ARRAY_SIZE(rt714_0_adr),
		.adr_d = rt714_0_adr,
	},
	{}
};

static const struct snd_soc_acpi_link_adr mtl_rt713_l0_rt1316_l12_rt1713_l3[] = {
	{
		.mask = BIT(0),
		.num_adr = ARRAY_SIZE(rt713_0_single_adr),
		.adr_d = rt713_0_single_adr,
	},
	{
		.mask = BIT(1),
		.num_adr = ARRAY_SIZE(rt1316_1_group2_adr),
		.adr_d = rt1316_1_group2_adr,
	},
	{
		.mask = BIT(2),
		.num_adr = ARRAY_SIZE(rt1316_2_group2_adr),
		.adr_d = rt1316_2_group2_adr,
	},
	{
		.mask = BIT(3),
		.num_adr = ARRAY_SIZE(rt1713_3_single_adr),
		.adr_d = rt1713_3_single_adr,
	},
	{}
};

static const struct snd_soc_acpi_link_adr mtl_rt713_l0_rt1318_l1_rt1713_l3[] = {
	{
		.mask = BIT(0),
		.num_adr = ARRAY_SIZE(rt713_0_single_adr),
		.adr_d = rt713_0_single_adr,
	},
	{
		.mask = BIT(1),
		.num_adr = ARRAY_SIZE(rt1318_1_single_adr),
		.adr_d = rt1318_1_single_adr,
	},
	{
		.mask = BIT(3),
		.num_adr = ARRAY_SIZE(rt1713_3_single_adr),
		.adr_d = rt1713_3_single_adr,
	},
	{}
};

static const struct snd_soc_acpi_link_adr mtl_rt713_l0_rt1318_l12_rt1713_l3[] = {
	{
		.mask = BIT(0),
		.num_adr = ARRAY_SIZE(rt713_0_single_adr),
		.adr_d = rt713_0_single_adr,
	},
	{
		.mask = BIT(1),
		.num_adr = ARRAY_SIZE(rt1318_1_group1_adr),
		.adr_d = rt1318_1_group1_adr,
	},
	{
		.mask = BIT(2),
		.num_adr = ARRAY_SIZE(rt1318_2_group1_adr),
		.adr_d = rt1318_2_group1_adr,
	},
	{
		.mask = BIT(3),
		.num_adr = ARRAY_SIZE(rt1713_3_single_adr),
		.adr_d = rt1713_3_single_adr,
	},
	{}
};

static const struct snd_soc_acpi_link_adr mtl_rt713_l0_rt1316_l12[] = {
	{
		.mask = BIT(0),
		.num_adr = ARRAY_SIZE(rt713_0_single_adr),
		.adr_d = rt713_0_single_adr,
	},
	{
		.mask = BIT(1),
		.num_adr = ARRAY_SIZE(rt1316_1_group2_adr),
		.adr_d = rt1316_1_group2_adr,
	},
	{
		.mask = BIT(2),
		.num_adr = ARRAY_SIZE(rt1316_2_group2_adr),
		.adr_d = rt1316_2_group2_adr,
	},
	{}
};

static const struct snd_soc_acpi_link_adr mtl_rt711_l0_rt1316_l3[] = {
	{
		.mask = BIT(0),
		.num_adr = ARRAY_SIZE(rt711_sdca_0_adr),
		.adr_d = rt711_sdca_0_adr,
	},
	{
		.mask = BIT(3),
		.num_adr = ARRAY_SIZE(rt1316_3_single_adr),
		.adr_d = rt1316_3_single_adr,
	},
	{}
};

static const struct snd_soc_acpi_adr_device mx8363_2_adr[] = {
	{
		.adr = 0x000230019F836300ull,
		.num_endpoints = 1,
		.endpoints = &spk_l_endpoint,
		.name_prefix = "Left"
	},
	{
		.adr = 0x000231019F836300ull,
		.num_endpoints = 1,
		.endpoints = &spk_r_endpoint,
		.name_prefix = "Right"
	}
};

static const struct snd_soc_acpi_adr_device cs42l42_0_adr[] = {
	{
		.adr = 0x00001001FA424200ull,
		.num_endpoints = 1,
		.endpoints = &single_endpoint,
		.name_prefix = "cs42l42"
	}
};

static const struct snd_soc_acpi_link_adr cs42l42_link0_max98363_link2[] = {
	/* Expected order: jack -> amp */
	{
		.mask = BIT(0),
		.num_adr = ARRAY_SIZE(cs42l42_0_adr),
		.adr_d = cs42l42_0_adr,
	},
	{
		.mask = BIT(2),
		.num_adr = ARRAY_SIZE(mx8363_2_adr),
		.adr_d = mx8363_2_adr,
	},
	{}
};

static const struct snd_soc_acpi_link_adr mtl_cs42l43_l0[] = {
	{
		.mask = BIT(0),
		.num_adr = ARRAY_SIZE(cs42l43_0_adr),
		.adr_d = cs42l43_0_adr,
	},
};

static const struct snd_soc_acpi_link_adr mtl_cs42l43_cs35l56[] = {
	{
		.mask = BIT(0),
		.num_adr = ARRAY_SIZE(cs42l43_0_adr),
		.adr_d = cs42l43_0_adr,
	},
	{
		.mask = BIT(1),
		.num_adr = ARRAY_SIZE(cs35l56_1_adr),
		.adr_d = cs35l56_1_adr,
	},
	{
		.mask = BIT(2),
		.num_adr = ARRAY_SIZE(cs35l56_2_adr),
		.adr_d = cs35l56_2_adr,
	},
	{}
};

static const struct snd_soc_acpi_link_adr cs42l43_link0_cs35l56_link2_link3[] = {
	/* Expected order: jack -> amp */
	{
		.mask = BIT(0),
		.num_adr = ARRAY_SIZE(cs42l43_0_adr),
		.adr_d = cs42l43_0_adr,
	},
	{
		.mask = BIT(2),
		.num_adr = ARRAY_SIZE(cs35l56_2_r_adr),
		.adr_d = cs35l56_2_r_adr,
	},
	{
		.mask = BIT(3),
		.num_adr = ARRAY_SIZE(cs35l56_3_l_adr),
		.adr_d = cs35l56_3_l_adr,
	},
	{}
};

/* this table is used when there is no I2S codec present */
struct snd_soc_acpi_mach snd_soc_acpi_intel_mtl_sdw_machines[] = {
	/* mockup tests need to be first */
	{
		.link_mask = GENMASK(3, 0),
		.links = sdw_mockup_headset_2amps_mic,
		.drv_name = "sof_sdw",
		.sof_tplg_filename = "sof-mtl-rt711-rt1308-rt715.tplg",
	},
	{
		.link_mask = BIT(0) | BIT(1) | BIT(3),
		.links = sdw_mockup_headset_1amp_mic,
		.drv_name = "sof_sdw",
		.sof_tplg_filename = "sof-mtl-rt711-rt1308-mono-rt715.tplg",
	},
	{
		.link_mask = GENMASK(2, 0),
		.links = sdw_mockup_mic_headset_1amp,
		.drv_name = "sof_sdw",
		.sof_tplg_filename = "sof-mtl-rt715-rt711-rt1308-mono.tplg",
	},
	{
		.link_mask = GENMASK(3, 0),
		.links = mtl_rt713_l0_rt1316_l12_rt1713_l3,
		.drv_name = "sof_sdw",
		.sof_tplg_filename = "sof-mtl-rt713-l0-rt1316-l12-rt1713-l3.tplg",
	},
	{
		.link_mask = GENMASK(3, 0),
		.links = mtl_rt713_l0_rt1318_l12_rt1713_l3,
		.drv_name = "sof_sdw",
		.sof_tplg_filename = "sof-mtl-rt713-l0-rt1318-l12-rt1713-l3.tplg",
	},
	{
		.link_mask = BIT(0) | BIT(1) | BIT(3),
		.links = mtl_rt713_l0_rt1318_l1_rt1713_l3,
		.drv_name = "sof_sdw",
		.sof_tplg_filename = "sof-mtl-rt713-l0-rt1318-l1-rt1713-l3.tplg",
	},
	{
		.link_mask = GENMASK(2, 0),
		.links = mtl_rt713_l0_rt1316_l12,
		.drv_name = "sof_sdw",
		.sof_tplg_filename = "sof-mtl-rt713-l0-rt1316-l12.tplg",
	},
	{
		.link_mask = BIT(3) | BIT(0),
		.links = mtl_712_l0_1712_l3,
		.drv_name = "sof_sdw",
		.sof_tplg_filename = "sof-mtl-rt712-l0-rt1712-l3.tplg",
	},
	{
		.link_mask = BIT(0),
		.links = mtl_712_l0,
		.drv_name = "sof_sdw",
		.sof_tplg_filename = "sof-mtl-rt712-l0.tplg",
	},
	{
		.link_mask = GENMASK(2, 0),
		.links = mtl_sdw_rt1318_l12_rt714_l0,
		.drv_name = "sof_sdw",
		.sof_tplg_filename = "sof-mtl-rt1318-l12-rt714-l0.tplg"
	},
	{
		.link_mask = BIT(0) | BIT(2) | BIT(3),
		.links = cs42l43_link0_cs35l56_link2_link3,
		.drv_name = "sof_sdw",
		.sof_tplg_filename = "sof-mtl-cs42l43-l0-cs35l56-l23.tplg",
	},
	{
		.link_mask = GENMASK(2, 0),
		.links = mtl_cs42l43_cs35l56,
		.drv_name = "sof_sdw",
		.sof_tplg_filename = "sof-mtl-cs42l43-l0-cs35l56-l12.tplg",
	},
	{
		.link_mask = BIT(0),
		.links = mtl_cs42l43_l0,
		.drv_name = "sof_sdw",
		.sof_tplg_filename = "sof-mtl-cs42l43-l0.tplg",
	},
	{
		.link_mask = GENMASK(3, 0),
		.links = mtl_3_in_1_sdca,
		.drv_name = "sof_sdw",
		.sof_tplg_filename = "sof-mtl-rt711-l0-rt1316-l23-rt714-l1.tplg",
	},
	{
		.link_mask = 0x9, /* 2 active links required */
		.links = mtl_rt711_l0_rt1316_l3,
		.drv_name = "sof_sdw",
		.sof_tplg_filename = "sof-mtl-rt711-l0-rt1316-l3.tplg",
	},
	{
		.link_mask = BIT(0),
		.links = mtl_rt722_only,
		.drv_name = "sof_sdw",
		.sof_tplg_filename = "sof-mtl-rt722-l0.tplg",
	},
	{
		.link_mask = BIT(0),
		.links = mtl_rvp,
		.drv_name = "sof_sdw",
		.sof_tplg_filename = "sof-mtl-rt711.tplg",
	},
	{
		.link_mask = BIT(0) | BIT(2),
		.links = rt5682_link2_max98373_link0,
		.drv_name = "sof_sdw",
		.sof_tplg_filename = "sof-mtl-sdw-rt5682-l2-max98373-l0.tplg",
	},
	{
		.link_mask = BIT(0) | BIT(2),
		.links = cs42l42_link0_max98363_link2,
		.drv_name = "sof_sdw",
		.sof_tplg_filename = "sof-mtl-sdw-cs42l42-l0-max98363-l2.tplg",
	},
	{},
};
EXPORT_SYMBOL_GPL(snd_soc_acpi_intel_mtl_sdw_machines);
