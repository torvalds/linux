// SPDX-License-Identifier: GPL-2.0-only
/*
 * soc-acpi-intel-tgl-match.c - tables and support for TGL ACPI enumeration.
 *
 * Copyright (c) 2019, Intel Corporation.
 *
 */

#include <sound/soc-acpi.h>
#include <sound/soc-acpi-intel-match.h>
#include "soc-acpi-intel-sdw-mockup-match.h"

static const struct snd_soc_acpi_codecs tgl_codecs = {
	.num_codecs = 1,
	.codecs = {"MX98357A"}
};

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

static const struct snd_soc_acpi_adr_device rt711_0_adr[] = {
	{
		.adr = 0x000020025D071100ull,
		.num_endpoints = 1,
		.endpoints = &single_endpoint,
		.name_prefix = "rt711"
	}
};

static const struct snd_soc_acpi_adr_device rt711_1_adr[] = {
	{
		.adr = 0x000120025D071100ull,
		.num_endpoints = 1,
		.endpoints = &single_endpoint,
		.name_prefix = "rt711"
	}
};

static const struct snd_soc_acpi_adr_device rt1308_1_dual_adr[] = {
	{
		.adr = 0x000120025D130800ull,
		.num_endpoints = 1,
		.endpoints = &spk_l_endpoint,
		.name_prefix = "rt1308-1"
	},
	{
		.adr = 0x000122025D130800ull,
		.num_endpoints = 1,
		.endpoints = &spk_r_endpoint,
		.name_prefix = "rt1308-2"
	}
};

static const struct snd_soc_acpi_adr_device rt1308_1_single_adr[] = {
	{
		.adr = 0x000120025D130800ull,
		.num_endpoints = 1,
		.endpoints = &single_endpoint,
		.name_prefix = "rt1308-1"
	}
};

static const struct snd_soc_acpi_adr_device rt1308_2_single_adr[] = {
	{
		.adr = 0x000220025D130800ull,
		.num_endpoints = 1,
		.endpoints = &single_endpoint,
		.name_prefix = "rt1308-1"
	}
};

static const struct snd_soc_acpi_adr_device rt1308_1_group1_adr[] = {
	{
		.adr = 0x000120025D130800ull,
		.num_endpoints = 1,
		.endpoints = &spk_l_endpoint,
		.name_prefix = "rt1308-1"
	}
};

static const struct snd_soc_acpi_adr_device rt1308_2_group1_adr[] = {
	{
		.adr = 0x000220025D130800ull,
		.num_endpoints = 1,
		.endpoints = &spk_r_endpoint,
		.name_prefix = "rt1308-2"
	}
};

static const struct snd_soc_acpi_adr_device rt715_0_adr[] = {
	{
		.adr = 0x000021025D071500ull,
		.num_endpoints = 1,
		.endpoints = &single_endpoint,
		.name_prefix = "rt715"
	}
};

static const struct snd_soc_acpi_adr_device rt715_3_adr[] = {
	{
		.adr = 0x000320025D071500ull,
		.num_endpoints = 1,
		.endpoints = &single_endpoint,
		.name_prefix = "rt715"
	}
};

static const struct snd_soc_acpi_adr_device mx8373_1_adr[] = {
	{
		.adr = 0x000123019F837300ull,
		.num_endpoints = 1,
		.endpoints = &spk_l_endpoint,
		.name_prefix = "Right"
	},
	{
		.adr = 0x000127019F837300ull,
		.num_endpoints = 1,
		.endpoints = &spk_r_endpoint,
		.name_prefix = "Left"
	}
};

static const struct snd_soc_acpi_adr_device rt5682_0_adr[] = {
	{
		.adr = 0x000021025D568200ull,
		.num_endpoints = 1,
		.endpoints = &single_endpoint,
		.name_prefix = "rt5682"
	}
};

static const struct snd_soc_acpi_adr_device rt711_sdca_0_adr[] = {
	{
		.adr = 0x000030025D071101ull,
		.num_endpoints = 1,
		.endpoints = &single_endpoint,
		.name_prefix = "rt711"
	}
};

static const struct snd_soc_acpi_adr_device rt1316_1_single_adr[] = {
	{
		.adr = 0x000131025D131601ull,
		.num_endpoints = 1,
		.endpoints = &single_endpoint,
		.name_prefix = "rt1316-1"
	}
};

static const struct snd_soc_acpi_adr_device rt1316_1_group1_adr[] = {
	{
		.adr = 0x000131025D131601ull, /* unique ID is set for some reason */
		.num_endpoints = 1,
		.endpoints = &spk_l_endpoint,
		.name_prefix = "rt1316-1"
	}
};

static const struct snd_soc_acpi_adr_device rt1316_2_group1_adr[] = {
	{
		.adr = 0x000230025D131601ull,
		.num_endpoints = 1,
		.endpoints = &spk_r_endpoint,
		.name_prefix = "rt1316-2"
	}
};

static const struct snd_soc_acpi_adr_device rt714_3_adr[] = {
	{
		.adr = 0x000330025D071401ull,
		.num_endpoints = 1,
		.endpoints = &single_endpoint,
		.name_prefix = "rt714"
	}
};

static const struct snd_soc_acpi_link_adr tgl_rvp[] = {
	{
		.mask = BIT(0),
		.num_adr = ARRAY_SIZE(rt711_0_adr),
		.adr_d = rt711_0_adr,
	},
	{
		.mask = BIT(1),
		.num_adr = ARRAY_SIZE(rt1308_1_dual_adr),
		.adr_d = rt1308_1_dual_adr,
	},
	{}
};

static const struct snd_soc_acpi_link_adr tgl_rvp_headset_only[] = {
	{
		.mask = BIT(0),
		.num_adr = ARRAY_SIZE(rt711_0_adr),
		.adr_d = rt711_0_adr,
	},
	{}
};

static const struct snd_soc_acpi_link_adr tgl_hp[] = {
	{
		.mask = BIT(0),
		.num_adr = ARRAY_SIZE(rt711_0_adr),
		.adr_d = rt711_0_adr,
	},
	{
		.mask = BIT(1),
		.num_adr = ARRAY_SIZE(rt1308_1_single_adr),
		.adr_d = rt1308_1_single_adr,
	},
	{}
};

static const struct snd_soc_acpi_link_adr tgl_chromebook_base[] = {
	{
		.mask = BIT(0),
		.num_adr = ARRAY_SIZE(rt5682_0_adr),
		.adr_d = rt5682_0_adr,
	},
	{
		.mask = BIT(1),
		.num_adr = ARRAY_SIZE(mx8373_1_adr),
		.adr_d = mx8373_1_adr,
	},
	{}
};

static const struct snd_soc_acpi_link_adr tgl_3_in_1_default[] = {
	{
		.mask = BIT(0),
		.num_adr = ARRAY_SIZE(rt711_0_adr),
		.adr_d = rt711_0_adr,
	},
	{
		.mask = BIT(1),
		.num_adr = ARRAY_SIZE(rt1308_1_group1_adr),
		.adr_d = rt1308_1_group1_adr,
	},
	{
		.mask = BIT(2),
		.num_adr = ARRAY_SIZE(rt1308_2_group1_adr),
		.adr_d = rt1308_2_group1_adr,
	},
	{
		.mask = BIT(3),
		.num_adr = ARRAY_SIZE(rt715_3_adr),
		.adr_d = rt715_3_adr,
	},
	{}
};

static const struct snd_soc_acpi_link_adr tgl_3_in_1_mono_amp[] = {
	{
		.mask = BIT(0),
		.num_adr = ARRAY_SIZE(rt711_0_adr),
		.adr_d = rt711_0_adr,
	},
	{
		.mask = BIT(1),
		.num_adr = ARRAY_SIZE(rt1308_1_single_adr),
		.adr_d = rt1308_1_single_adr,
	},
	{
		.mask = BIT(3),
		.num_adr = ARRAY_SIZE(rt715_3_adr),
		.adr_d = rt715_3_adr,
	},
	{}
};

static const struct snd_soc_acpi_link_adr tgl_sdw_rt711_link1_rt1308_link2_rt715_link0[] = {
	{
		.mask = BIT(1),
		.num_adr = ARRAY_SIZE(rt711_1_adr),
		.adr_d = rt711_1_adr,
	},
	{
		.mask = BIT(2),
		.num_adr = ARRAY_SIZE(rt1308_2_single_adr),
		.adr_d = rt1308_2_single_adr,
	},
	{
		.mask = BIT(0),
		.num_adr = ARRAY_SIZE(rt715_0_adr),
		.adr_d = rt715_0_adr,
	},
	{}
};

static const struct snd_soc_acpi_link_adr tgl_3_in_1_sdca[] = {
	{
		.mask = BIT(0),
		.num_adr = ARRAY_SIZE(rt711_sdca_0_adr),
		.adr_d = rt711_sdca_0_adr,
	},
	{
		.mask = BIT(1),
		.num_adr = ARRAY_SIZE(rt1316_1_group1_adr),
		.adr_d = rt1316_1_group1_adr,
	},
	{
		.mask = BIT(2),
		.num_adr = ARRAY_SIZE(rt1316_2_group1_adr),
		.adr_d = rt1316_2_group1_adr,
	},
	{
		.mask = BIT(3),
		.num_adr = ARRAY_SIZE(rt714_3_adr),
		.adr_d = rt714_3_adr,
	},
	{}
};

static const struct snd_soc_acpi_link_adr tgl_3_in_1_sdca_mono[] = {
	{
		.mask = BIT(0),
		.num_adr = ARRAY_SIZE(rt711_sdca_0_adr),
		.adr_d = rt711_sdca_0_adr,
	},
	{
		.mask = BIT(1),
		.num_adr = ARRAY_SIZE(rt1316_1_single_adr),
		.adr_d = rt1316_1_single_adr,
	},
	{
		.mask = BIT(3),
		.num_adr = ARRAY_SIZE(rt714_3_adr),
		.adr_d = rt714_3_adr,
	},
	{}
};

static const struct snd_soc_acpi_codecs tgl_max98373_amp = {
	.num_codecs = 1,
	.codecs = {"MX98373"}
};

static const struct snd_soc_acpi_codecs tgl_rt1011_amp = {
	.num_codecs = 1,
	.codecs = {"10EC1011"}
};

struct snd_soc_acpi_mach snd_soc_acpi_intel_tgl_machines[] = {
	{
		.id = "10EC5682",
		.drv_name = "tgl_mx98357_rt5682",
		.machine_quirk = snd_soc_acpi_codec_list,
		.quirk_data = &tgl_codecs,
		.sof_fw_filename = "sof-tgl.ri",
		.sof_tplg_filename = "sof-tgl-max98357a-rt5682.tplg",
	},
	{
		.id = "10EC5682",
		.drv_name = "tgl_mx98373_rt5682",
		.machine_quirk = snd_soc_acpi_codec_list,
		.quirk_data = &tgl_max98373_amp,
		.sof_fw_filename = "sof-tgl.ri",
		.sof_tplg_filename = "sof-tgl-max98373-rt5682.tplg",
	},
	{
		.id = "10EC5682",
		.drv_name = "tgl_rt1011_rt5682",
		.machine_quirk = snd_soc_acpi_codec_list,
		.quirk_data = &tgl_rt1011_amp,
		.sof_fw_filename = "sof-tgl.ri",
		.sof_tplg_filename = "sof-tgl-rt1011-rt5682.tplg",
	},
	{
		.id = "ESSX8336",
		.drv_name = "sof-essx8336",
		.sof_fw_filename = "sof-tgl.ri",
		.sof_tplg_filename = "sof-tgl-es8336.tplg",
	},
	{},
};
EXPORT_SYMBOL_GPL(snd_soc_acpi_intel_tgl_machines);

/* this table is used when there is no I2S codec present */
struct snd_soc_acpi_mach snd_soc_acpi_intel_tgl_sdw_machines[] = {
	/* mockup tests need to be first */
	{
		.link_mask = GENMASK(3, 0),
		.links = sdw_mockup_headset_2amps_mic,
		.drv_name = "sof_sdw",
		.sof_fw_filename = "sof-tgl.ri",
		.sof_tplg_filename = "sof-tgl-rt711-rt1308-rt715.tplg",
	},
	{
		.link_mask = BIT(0) | BIT(1) | BIT(3),
		.links = sdw_mockup_headset_1amp_mic,
		.drv_name = "sof_sdw",
		.sof_fw_filename = "sof-tgl.ri",
		.sof_tplg_filename = "sof-tgl-rt711-rt1308-mono-rt715.tplg",
	},
	{
		.link_mask = BIT(0) | BIT(1) | BIT(2),
		.links = sdw_mockup_mic_headset_1amp,
		.drv_name = "sof_sdw",
		.sof_fw_filename = "sof-tgl.ri",
		.sof_tplg_filename = "sof-tgl-rt715-rt711-rt1308-mono.tplg",
	},
	{
		.link_mask = 0x7,
		.links = tgl_sdw_rt711_link1_rt1308_link2_rt715_link0,
		.drv_name = "sof_sdw",
		.sof_tplg_filename = "sof-tgl-rt715-rt711-rt1308-mono.tplg",
	},
	{
		.link_mask = 0xF, /* 4 active links required */
		.links = tgl_3_in_1_default,
		.drv_name = "sof_sdw",
		.sof_tplg_filename = "sof-tgl-rt711-rt1308-rt715.tplg",
	},
	{
		/*
		 * link_mask should be 0xB, but all links are enabled by BIOS.
		 * This entry will be selected if there is no rt1308 exposed
		 * on link2 since it will fail to match the above entry.
		 */
		.link_mask = 0xF,
		.links = tgl_3_in_1_mono_amp,
		.drv_name = "sof_sdw",
		.sof_tplg_filename = "sof-tgl-rt711-rt1308-mono-rt715.tplg",
	},
	{
		.link_mask = 0xF, /* 4 active links required */
		.links = tgl_3_in_1_sdca,
		.drv_name = "sof_sdw",
		.sof_tplg_filename = "sof-tgl-rt711-rt1316-rt714.tplg",
	},
	{
		/*
		 * link_mask should be 0xB, but all links are enabled by BIOS.
		 * This entry will be selected if there is no rt1316 amplifier exposed
		 * on link2 since it will fail to match the above entry.
		 */

		.link_mask = 0xF, /* 4 active links required */
		.links = tgl_3_in_1_sdca_mono,
		.drv_name = "sof_sdw",
		.sof_tplg_filename = "sof-tgl-rt711-l0-rt1316-l1-mono-rt714-l3.tplg",
	},

	{
		.link_mask = 0x3, /* rt711 on link 0 and 1 rt1308 on link 1 */
		.links = tgl_hp,
		.drv_name = "sof_sdw",
		.sof_tplg_filename = "sof-tgl-rt711-rt1308.tplg",
	},
	{
		.link_mask = 0x3, /* rt711 on link 0 and 2 rt1308s on link 1 */
		.links = tgl_rvp,
		.drv_name = "sof_sdw",
		.sof_tplg_filename = "sof-tgl-rt711-rt1308.tplg",
	},
	{
		.link_mask = 0x3, /* rt5682 on link0 & 2xmax98373 on link 1 */
		.links = tgl_chromebook_base,
		.drv_name = "sof_sdw",
		.sof_tplg_filename = "sof-tgl-sdw-max98373-rt5682.tplg",
	},
	{
		.link_mask = 0x1, /* rt711 on link 0 */
		.links = tgl_rvp_headset_only,
		.drv_name = "sof_sdw",
		.sof_tplg_filename = "sof-tgl-rt711.tplg",
	},
	{},
};
EXPORT_SYMBOL_GPL(snd_soc_acpi_intel_tgl_sdw_machines);
