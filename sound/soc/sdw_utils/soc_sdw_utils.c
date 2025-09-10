// SPDX-License-Identifier: GPL-2.0-only
// This file incorporates work covered by the following copyright notice:
// Copyright (c) 2020 Intel Corporation
// Copyright(c) 2024 Advanced Micro Devices, Inc.
/*
 *  soc-sdw-utils.c - common SoundWire machine driver helper functions
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/soundwire/sdw.h>
#include <linux/soundwire/sdw_type.h>
#include <sound/sdca_function.h>
#include <sound/soc_sdw_utils.h>

static const struct snd_soc_dapm_widget generic_dmic_widgets[] = {
	SND_SOC_DAPM_MIC("DMIC", NULL),
};

static const struct snd_soc_dapm_widget generic_jack_widgets[] = {
	SND_SOC_DAPM_HP("Headphone", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
};

static const struct snd_kcontrol_new generic_jack_controls[] = {
	SOC_DAPM_PIN_SWITCH("Headphone"),
	SOC_DAPM_PIN_SWITCH("Headset Mic"),
};

static const struct snd_soc_dapm_widget generic_spk_widgets[] = {
	SND_SOC_DAPM_SPK("Speaker", NULL),
};

static const struct snd_kcontrol_new generic_spk_controls[] = {
	SOC_DAPM_PIN_SWITCH("Speaker"),
};

static const struct snd_soc_dapm_widget maxim_widgets[] = {
	SND_SOC_DAPM_SPK("Left Spk", NULL),
	SND_SOC_DAPM_SPK("Right Spk", NULL),
};

static const struct snd_kcontrol_new maxim_controls[] = {
	SOC_DAPM_PIN_SWITCH("Left Spk"),
	SOC_DAPM_PIN_SWITCH("Right Spk"),
};

static const struct snd_soc_dapm_widget rt700_widgets[] = {
	SND_SOC_DAPM_HP("Headphones", NULL),
	SND_SOC_DAPM_MIC("AMIC", NULL),
	SND_SOC_DAPM_SPK("Speaker", NULL),
};

static const struct snd_kcontrol_new rt700_controls[] = {
	SOC_DAPM_PIN_SWITCH("Headphones"),
	SOC_DAPM_PIN_SWITCH("AMIC"),
	SOC_DAPM_PIN_SWITCH("Speaker"),
};

struct asoc_sdw_codec_info codec_info_list[] = {
	{
		.part_id = 0x700,
		.dais = {
			{
				.direction = {true, true},
				.dai_name = "rt700-aif1",
				.dai_type = SOC_SDW_DAI_TYPE_JACK,
				.dailink = {SOC_SDW_JACK_OUT_DAI_ID, SOC_SDW_JACK_IN_DAI_ID},
				.rtd_init = asoc_sdw_rt700_rtd_init,
				.controls = rt700_controls,
				.num_controls = ARRAY_SIZE(rt700_controls),
				.widgets = rt700_widgets,
				.num_widgets = ARRAY_SIZE(rt700_widgets),
			},
		},
		.dai_num = 1,
	},
	{
		.part_id = 0x711,
		.version_id = 3,
		.dais = {
			{
				.direction = {true, true},
				.dai_name = "rt711-sdca-aif1",
				.dai_type = SOC_SDW_DAI_TYPE_JACK,
				.dailink = {SOC_SDW_JACK_OUT_DAI_ID, SOC_SDW_JACK_IN_DAI_ID},
				.init = asoc_sdw_rt_sdca_jack_init,
				.exit = asoc_sdw_rt_sdca_jack_exit,
				.rtd_init = asoc_sdw_rt_sdca_jack_rtd_init,
				.controls = generic_jack_controls,
				.num_controls = ARRAY_SIZE(generic_jack_controls),
				.widgets = generic_jack_widgets,
				.num_widgets = ARRAY_SIZE(generic_jack_widgets),
			},
		},
		.dai_num = 1,
	},
	{
		.part_id = 0x711,
		.version_id = 2,
		.dais = {
			{
				.direction = {true, true},
				.dai_name = "rt711-aif1",
				.dai_type = SOC_SDW_DAI_TYPE_JACK,
				.dailink = {SOC_SDW_JACK_OUT_DAI_ID, SOC_SDW_JACK_IN_DAI_ID},
				.init = asoc_sdw_rt711_init,
				.exit = asoc_sdw_rt711_exit,
				.rtd_init = asoc_sdw_rt711_rtd_init,
				.controls = generic_jack_controls,
				.num_controls = ARRAY_SIZE(generic_jack_controls),
				.widgets = generic_jack_widgets,
				.num_widgets = ARRAY_SIZE(generic_jack_widgets),
			},
		},
		.dai_num = 1,
	},
	{
		.part_id = 0x712,
		.version_id = 3,
		.dais =	{
			{
				.direction = {true, true},
				.dai_name = "rt712-sdca-aif1",
				.dai_type = SOC_SDW_DAI_TYPE_JACK,
				.dailink = {SOC_SDW_JACK_OUT_DAI_ID, SOC_SDW_JACK_IN_DAI_ID},
				.init = asoc_sdw_rt_sdca_jack_init,
				.exit = asoc_sdw_rt_sdca_jack_exit,
				.rtd_init = asoc_sdw_rt_sdca_jack_rtd_init,
				.controls = generic_jack_controls,
				.num_controls = ARRAY_SIZE(generic_jack_controls),
				.widgets = generic_jack_widgets,
				.num_widgets = ARRAY_SIZE(generic_jack_widgets),
			},
			{
				.direction = {true, false},
				.dai_name = "rt712-sdca-aif2",
				.component_name = "rt712",
				.dai_type = SOC_SDW_DAI_TYPE_AMP,
				.dailink = {SOC_SDW_AMP_OUT_DAI_ID, SOC_SDW_UNUSED_DAI_ID},
				.init = asoc_sdw_rt_amp_init,
				.exit = asoc_sdw_rt_amp_exit,
				.rtd_init = asoc_sdw_rt_mf_sdca_spk_rtd_init,
				.controls = generic_spk_controls,
				.num_controls = ARRAY_SIZE(generic_spk_controls),
				.widgets = generic_spk_widgets,
				.num_widgets = ARRAY_SIZE(generic_spk_widgets),
			},
			{
				.direction = {false, true},
				.dai_name = "rt712-sdca-aif3",
				.dai_type = SOC_SDW_DAI_TYPE_MIC,
				.dailink = {SOC_SDW_UNUSED_DAI_ID, SOC_SDW_DMIC_DAI_ID},
				.rtd_init = asoc_sdw_rt_dmic_rtd_init,
			},
		},
		.dai_num = 3,
	},
	{
		.part_id = 0x1712,
		.version_id = 3,
		.dais =	{
			{
				.direction = {false, true},
				.dai_name = "rt712-sdca-dmic-aif1",
				.dai_type = SOC_SDW_DAI_TYPE_MIC,
				.dailink = {SOC_SDW_UNUSED_DAI_ID, SOC_SDW_DMIC_DAI_ID},
				.rtd_init = asoc_sdw_rt_dmic_rtd_init,
			},
		},
		.dai_num = 1,
	},
	{
		.part_id = 0x713,
		.version_id = 3,
		.dais =	{
			{
				.direction = {true, true},
				.dai_name = "rt712-sdca-aif1",
				.dai_type = SOC_SDW_DAI_TYPE_JACK,
				.dailink = {SOC_SDW_JACK_OUT_DAI_ID, SOC_SDW_JACK_IN_DAI_ID},
				.init = asoc_sdw_rt_sdca_jack_init,
				.exit = asoc_sdw_rt_sdca_jack_exit,
				.rtd_init = asoc_sdw_rt_sdca_jack_rtd_init,
				.controls = generic_jack_controls,
				.num_controls = ARRAY_SIZE(generic_jack_controls),
				.widgets = generic_jack_widgets,
				.num_widgets = ARRAY_SIZE(generic_jack_widgets),
			},
			{
				.direction = {false, true},
				.dai_name = "rt712-sdca-aif3",
				.dai_type = SOC_SDW_DAI_TYPE_MIC,
				.dailink = {SOC_SDW_UNUSED_DAI_ID, SOC_SDW_DMIC_DAI_ID},
				.rtd_init = asoc_sdw_rt_dmic_rtd_init,
			},
		},
		.dai_num = 2,
	},
	{
		.part_id = 0x1713,
		.version_id = 3,
		.dais =	{
			{
				.direction = {false, true},
				.dai_name = "rt712-sdca-dmic-aif1",
				.dai_type = SOC_SDW_DAI_TYPE_MIC,
				.dailink = {SOC_SDW_UNUSED_DAI_ID, SOC_SDW_DMIC_DAI_ID},
				.rtd_init = asoc_sdw_rt_dmic_rtd_init,
			},
		},
		.dai_num = 1,
	},
	{
		.part_id = 0x1308,
		.acpi_id = "10EC1308",
		.dais = {
			{
				.direction = {true, false},
				.dai_name = "rt1308-aif",
				.component_name = "rt1308",
				.dai_type = SOC_SDW_DAI_TYPE_AMP,
				.dailink = {SOC_SDW_AMP_OUT_DAI_ID, SOC_SDW_UNUSED_DAI_ID},
				.init = asoc_sdw_rt_amp_init,
				.exit = asoc_sdw_rt_amp_exit,
				.rtd_init = asoc_sdw_rt_amp_spk_rtd_init,
				.controls = generic_spk_controls,
				.num_controls = ARRAY_SIZE(generic_spk_controls),
				.widgets = generic_spk_widgets,
				.num_widgets = ARRAY_SIZE(generic_spk_widgets),
			},
		},
		.dai_num = 1,
		.ops = &soc_sdw_rt1308_i2s_ops,
	},
	{
		.part_id = 0x1316,
		.dais = {
			{
				.direction = {true, true},
				.dai_name = "rt1316-aif",
				.component_name = "rt1316",
				.dai_type = SOC_SDW_DAI_TYPE_AMP,
				.dailink = {SOC_SDW_AMP_OUT_DAI_ID, SOC_SDW_AMP_IN_DAI_ID},
				.init = asoc_sdw_rt_amp_init,
				.exit = asoc_sdw_rt_amp_exit,
				.rtd_init = asoc_sdw_rt_amp_spk_rtd_init,
				.controls = generic_spk_controls,
				.num_controls = ARRAY_SIZE(generic_spk_controls),
				.widgets = generic_spk_widgets,
				.num_widgets = ARRAY_SIZE(generic_spk_widgets),
			},
		},
		.dai_num = 1,
	},
	{
		.part_id = 0x1318,
		.dais = {
			{
				.direction = {true, true},
				.dai_name = "rt1318-aif",
				.component_name = "rt1318",
				.dai_type = SOC_SDW_DAI_TYPE_AMP,
				.dailink = {SOC_SDW_AMP_OUT_DAI_ID, SOC_SDW_AMP_IN_DAI_ID},
				.init = asoc_sdw_rt_amp_init,
				.exit = asoc_sdw_rt_amp_exit,
				.rtd_init = asoc_sdw_rt_amp_spk_rtd_init,
				.controls = generic_spk_controls,
				.num_controls = ARRAY_SIZE(generic_spk_controls),
				.widgets = generic_spk_widgets,
				.num_widgets = ARRAY_SIZE(generic_spk_widgets),
			},
		},
		.dai_num = 1,
	},
	{
		.part_id = 0x1320,
		.dais = {
			{
				.direction = {true, false},
				.dai_name = "rt1320-aif1",
				.component_name = "rt1320",
				.dai_type = SOC_SDW_DAI_TYPE_AMP,
				.dailink = {SOC_SDW_AMP_OUT_DAI_ID, SOC_SDW_UNUSED_DAI_ID},
				.init = asoc_sdw_rt_amp_init,
				.exit = asoc_sdw_rt_amp_exit,
				.rtd_init = asoc_sdw_rt_amp_spk_rtd_init,
				.controls = generic_spk_controls,
				.num_controls = ARRAY_SIZE(generic_spk_controls),
				.widgets = generic_spk_widgets,
				.num_widgets = ARRAY_SIZE(generic_spk_widgets),
			},
		},
		.dai_num = 1,
	},
	{
		.part_id = 0x714,
		.version_id = 3,
		.ignore_internal_dmic = true,
		.dais = {
			{
				.direction = {false, true},
				.dai_name = "rt715-sdca-aif2",
				.dai_type = SOC_SDW_DAI_TYPE_MIC,
				.dailink = {SOC_SDW_UNUSED_DAI_ID, SOC_SDW_DMIC_DAI_ID},
				.rtd_init = asoc_sdw_rt_dmic_rtd_init,
			},
		},
		.dai_num = 1,
	},
	{
		.part_id = 0x715,
		.version_id = 3,
		.ignore_internal_dmic = true,
		.dais = {
			{
				.direction = {false, true},
				.dai_name = "rt715-sdca-aif2",
				.dai_type = SOC_SDW_DAI_TYPE_MIC,
				.dailink = {SOC_SDW_UNUSED_DAI_ID, SOC_SDW_DMIC_DAI_ID},
				.rtd_init = asoc_sdw_rt_dmic_rtd_init,
			},
		},
		.dai_num = 1,
	},
	{
		.part_id = 0x714,
		.version_id = 2,
		.ignore_internal_dmic = true,
		.dais = {
			{
				.direction = {false, true},
				.dai_name = "rt715-aif2",
				.dai_type = SOC_SDW_DAI_TYPE_MIC,
				.dailink = {SOC_SDW_UNUSED_DAI_ID, SOC_SDW_DMIC_DAI_ID},
				.rtd_init = asoc_sdw_rt_dmic_rtd_init,
			},
		},
		.dai_num = 1,
	},
	{
		.part_id = 0x715,
		.version_id = 2,
		.ignore_internal_dmic = true,
		.dais = {
			{
				.direction = {false, true},
				.dai_name = "rt715-aif2",
				.dai_type = SOC_SDW_DAI_TYPE_MIC,
				.dailink = {SOC_SDW_UNUSED_DAI_ID, SOC_SDW_DMIC_DAI_ID},
				.rtd_init = asoc_sdw_rt_dmic_rtd_init,
			},
		},
		.dai_num = 1,
	},
	{
		.part_id = 0x721,
		.version_id = 3,
		.dais = {
			{
				.direction = {true, true},
				.dai_name = "rt721-sdca-aif1",
				.dai_type = SOC_SDW_DAI_TYPE_JACK,
				.dailink = {SOC_SDW_JACK_OUT_DAI_ID, SOC_SDW_JACK_IN_DAI_ID},
				.init = asoc_sdw_rt_sdca_jack_init,
				.exit = asoc_sdw_rt_sdca_jack_exit,
				.rtd_init = asoc_sdw_rt_sdca_jack_rtd_init,
				.controls = generic_jack_controls,
				.num_controls = ARRAY_SIZE(generic_jack_controls),
				.widgets = generic_jack_widgets,
				.num_widgets = ARRAY_SIZE(generic_jack_widgets),
			},
			{
				.direction = {true, false},
				.dai_name = "rt721-sdca-aif2",
				.component_name = "rt721",
				.dai_type = SOC_SDW_DAI_TYPE_AMP,
				/* No feedback capability is provided by rt721-sdca codec driver*/
				.dailink = {SOC_SDW_AMP_OUT_DAI_ID, SOC_SDW_UNUSED_DAI_ID},
				.init = asoc_sdw_rt_amp_init,
				.exit = asoc_sdw_rt_amp_exit,
				.rtd_init = asoc_sdw_rt_mf_sdca_spk_rtd_init,
				.controls = generic_spk_controls,
				.num_controls = ARRAY_SIZE(generic_spk_controls),
				.widgets = generic_spk_widgets,
				.num_widgets = ARRAY_SIZE(generic_spk_widgets),
			},
			{
				.direction = {false, true},
				.dai_name = "rt721-sdca-aif3",
				.dai_type = SOC_SDW_DAI_TYPE_MIC,
				.dailink = {SOC_SDW_UNUSED_DAI_ID, SOC_SDW_DMIC_DAI_ID},
				.rtd_init = asoc_sdw_rt_dmic_rtd_init,
			},
		},
		.dai_num = 3,
	},
	{
		.part_id = 0x722,
		.version_id = 3,
		.dais = {
			{
				.direction = {true, true},
				.dai_name = "rt722-sdca-aif1",
				.dai_type = SOC_SDW_DAI_TYPE_JACK,
				.dailink = {SOC_SDW_JACK_OUT_DAI_ID, SOC_SDW_JACK_IN_DAI_ID},
				.init = asoc_sdw_rt_sdca_jack_init,
				.exit = asoc_sdw_rt_sdca_jack_exit,
				.rtd_init = asoc_sdw_rt_sdca_jack_rtd_init,
				.controls = generic_jack_controls,
				.num_controls = ARRAY_SIZE(generic_jack_controls),
				.widgets = generic_jack_widgets,
				.num_widgets = ARRAY_SIZE(generic_jack_widgets),
			},
			{
				.direction = {true, false},
				.dai_name = "rt722-sdca-aif2",
				.component_name = "rt722",
				.dai_type = SOC_SDW_DAI_TYPE_AMP,
				/* No feedback capability is provided by rt722-sdca codec driver*/
				.dailink = {SOC_SDW_AMP_OUT_DAI_ID, SOC_SDW_UNUSED_DAI_ID},
				.init = asoc_sdw_rt_amp_init,
				.exit = asoc_sdw_rt_amp_exit,
				.rtd_init = asoc_sdw_rt_mf_sdca_spk_rtd_init,
				.controls = generic_spk_controls,
				.num_controls = ARRAY_SIZE(generic_spk_controls),
				.widgets = generic_spk_widgets,
				.num_widgets = ARRAY_SIZE(generic_spk_widgets),
				.quirk = SOC_SDW_CODEC_SPKR,
				.quirk_exclude = true,
			},
			{
				.direction = {false, true},
				.dai_name = "rt722-sdca-aif3",
				.dai_type = SOC_SDW_DAI_TYPE_MIC,
				.dailink = {SOC_SDW_UNUSED_DAI_ID, SOC_SDW_DMIC_DAI_ID},
				.rtd_init = asoc_sdw_rt_dmic_rtd_init,
			},
		},
		.dai_num = 3,
	},
	{
		.part_id = 0x8373,
		.dais = {
			{
				.direction = {true, true},
				.dai_name = "max98373-aif1",
				.component_name = "mx8373",
				.dai_type = SOC_SDW_DAI_TYPE_AMP,
				.dailink = {SOC_SDW_AMP_OUT_DAI_ID, SOC_SDW_AMP_IN_DAI_ID},
				.init = asoc_sdw_maxim_init,
				.rtd_init = asoc_sdw_maxim_spk_rtd_init,
				.controls = maxim_controls,
				.num_controls = ARRAY_SIZE(maxim_controls),
				.widgets = maxim_widgets,
				.num_widgets = ARRAY_SIZE(maxim_widgets),
			},
		},
		.dai_num = 1,
	},
	{
		.part_id = 0x8363,
		.dais = {
			{
				.direction = {true, false},
				.dai_name = "max98363-aif1",
				.component_name = "mx8363",
				.dai_type = SOC_SDW_DAI_TYPE_AMP,
				.dailink = {SOC_SDW_AMP_OUT_DAI_ID, SOC_SDW_UNUSED_DAI_ID},
				.init = asoc_sdw_maxim_init,
				.rtd_init = asoc_sdw_maxim_spk_rtd_init,
				.controls = maxim_controls,
				.num_controls = ARRAY_SIZE(maxim_controls),
				.widgets = maxim_widgets,
				.num_widgets = ARRAY_SIZE(maxim_widgets),
			},
		},
		.dai_num = 1,
	},
	{
		.part_id = 0x5682,
		.dais = {
			{
				.direction = {true, true},
				.dai_name = "rt5682-sdw",
				.dai_type = SOC_SDW_DAI_TYPE_JACK,
				.dailink = {SOC_SDW_JACK_OUT_DAI_ID, SOC_SDW_JACK_IN_DAI_ID},
				.rtd_init = asoc_sdw_rt5682_rtd_init,
				.controls = generic_jack_controls,
				.num_controls = ARRAY_SIZE(generic_jack_controls),
				.widgets = generic_jack_widgets,
				.num_widgets = ARRAY_SIZE(generic_jack_widgets),
			},
		},
		.dai_num = 1,
	},
	{
		.part_id = 0x3556,
		.dais = {
			{
				.direction = {true, false},
				.dai_name = "cs35l56-sdw1",
				.component_name = "cs35l56",
				.dai_type = SOC_SDW_DAI_TYPE_AMP,
				.dailink = {SOC_SDW_AMP_OUT_DAI_ID, SOC_SDW_UNUSED_DAI_ID},
				.init = asoc_sdw_cs_amp_init,
				.rtd_init = asoc_sdw_cs_spk_rtd_init,
				.controls = generic_spk_controls,
				.num_controls = ARRAY_SIZE(generic_spk_controls),
				.widgets = generic_spk_widgets,
				.num_widgets = ARRAY_SIZE(generic_spk_widgets),
			},
			{
				.direction = {false, true},
				.dai_name = "cs35l56-sdw1c",
				.dai_type = SOC_SDW_DAI_TYPE_AMP,
				.dailink = {SOC_SDW_UNUSED_DAI_ID, SOC_SDW_AMP_IN_DAI_ID},
				.rtd_init = asoc_sdw_cs_spk_feedback_rtd_init,
			},
		},
		.dai_num = 2,
	},
	{
		.part_id = 0x3563,
		.dais = {
			{
				.direction = {true, false},
				.dai_name = "cs35l56-sdw1",
				.component_name = "cs35l56",
				.dai_type = SOC_SDW_DAI_TYPE_AMP,
				.dailink = {SOC_SDW_AMP_OUT_DAI_ID, SOC_SDW_UNUSED_DAI_ID},
				.init = asoc_sdw_cs_amp_init,
				.rtd_init = asoc_sdw_cs_spk_rtd_init,
				.controls = generic_spk_controls,
				.num_controls = ARRAY_SIZE(generic_spk_controls),
				.widgets = generic_spk_widgets,
				.num_widgets = ARRAY_SIZE(generic_spk_widgets),
			},
			{
				.direction = {false, true},
				.dai_name = "cs35l56-sdw1c",
				.dai_type = SOC_SDW_DAI_TYPE_AMP,
				.dailink = {SOC_SDW_UNUSED_DAI_ID, SOC_SDW_AMP_IN_DAI_ID},
				.rtd_init = asoc_sdw_cs_spk_feedback_rtd_init,
			},
		},
		.dai_num = 2,
	},
	{
		.part_id = 0x4242,
		.dais = {
			{
				.direction = {true, true},
				.dai_name = "cs42l42-sdw",
				.dai_type = SOC_SDW_DAI_TYPE_JACK,
				.dailink = {SOC_SDW_JACK_OUT_DAI_ID, SOC_SDW_JACK_IN_DAI_ID},
				.rtd_init = asoc_sdw_cs42l42_rtd_init,
				.controls = generic_jack_controls,
				.num_controls = ARRAY_SIZE(generic_jack_controls),
				.widgets = generic_jack_widgets,
				.num_widgets = ARRAY_SIZE(generic_jack_widgets),
			},
		},
		.dai_num = 1,
	},
	{
		.part_id = 0x4243,
		.codec_name = "cs42l43-codec",
		.count_sidecar = asoc_sdw_bridge_cs35l56_count_sidecar,
		.add_sidecar = asoc_sdw_bridge_cs35l56_add_sidecar,
		.dais = {
			{
				.direction = {true, false},
				.dai_name = "cs42l43-dp5",
				.dai_type = SOC_SDW_DAI_TYPE_JACK,
				.dailink = {SOC_SDW_JACK_OUT_DAI_ID, SOC_SDW_UNUSED_DAI_ID},
				.rtd_init = asoc_sdw_cs42l43_hs_rtd_init,
				.controls = generic_jack_controls,
				.num_controls = ARRAY_SIZE(generic_jack_controls),
				.widgets = generic_jack_widgets,
				.num_widgets = ARRAY_SIZE(generic_jack_widgets),
			},
			{
				.direction = {false, true},
				.dai_name = "cs42l43-dp1",
				.dai_type = SOC_SDW_DAI_TYPE_MIC,
				.dailink = {SOC_SDW_UNUSED_DAI_ID, SOC_SDW_DMIC_DAI_ID},
				.rtd_init = asoc_sdw_cs42l43_dmic_rtd_init,
				.widgets = generic_dmic_widgets,
				.num_widgets = ARRAY_SIZE(generic_dmic_widgets),
				.quirk = SOC_SDW_CODEC_MIC,
				.quirk_exclude = true,
			},
			{
				.direction = {false, true},
				.dai_name = "cs42l43-dp2",
				.dai_type = SOC_SDW_DAI_TYPE_JACK,
				.dailink = {SOC_SDW_UNUSED_DAI_ID, SOC_SDW_JACK_IN_DAI_ID},
			},
			{
				.direction = {true, false},
				.dai_name = "cs42l43-dp6",
				.component_name = "cs42l43",
				.dai_type = SOC_SDW_DAI_TYPE_AMP,
				.dailink = {SOC_SDW_AMP_OUT_DAI_ID, SOC_SDW_UNUSED_DAI_ID},
				.init = asoc_sdw_cs42l43_spk_init,
				.rtd_init = asoc_sdw_cs42l43_spk_rtd_init,
				.controls = generic_spk_controls,
				.num_controls = ARRAY_SIZE(generic_spk_controls),
				.widgets = generic_spk_widgets,
				.num_widgets = ARRAY_SIZE(generic_spk_widgets),
				.quirk = SOC_SDW_CODEC_SPKR | SOC_SDW_SIDECAR_AMPS,
			},
		},
		.dai_num = 4,
	},
	{
		.part_id = 0xaaaa, /* generic codec mockup */
		.version_id = 0,
		.dais = {
			{
				.direction = {true, true},
				.dai_name = "sdw-mockup-aif1",
				.dai_type = SOC_SDW_DAI_TYPE_JACK,
				.dailink = {SOC_SDW_JACK_OUT_DAI_ID, SOC_SDW_JACK_IN_DAI_ID},
			},
			{
				.direction = {true, false},
				.dai_name = "sdw-mockup-aif1",
				.dai_type = SOC_SDW_DAI_TYPE_AMP,
				.dailink = {SOC_SDW_AMP_OUT_DAI_ID, SOC_SDW_UNUSED_DAI_ID},
			},
			{
				.direction = {false, true},
				.dai_name = "sdw-mockup-aif1",
				.dai_type = SOC_SDW_DAI_TYPE_MIC,
				.dailink = {SOC_SDW_UNUSED_DAI_ID, SOC_SDW_DMIC_DAI_ID},
			},
		},
		.dai_num = 3,
	},
	{
		.part_id = 0xaa55, /* headset codec mockup */
		.version_id = 0,
		.dais = {
			{
				.direction = {true, true},
				.dai_name = "sdw-mockup-aif1",
				.dai_type = SOC_SDW_DAI_TYPE_JACK,
				.dailink = {SOC_SDW_JACK_OUT_DAI_ID, SOC_SDW_JACK_IN_DAI_ID},
			},
		},
		.dai_num = 1,
	},
	{
		.part_id = 0x55aa, /* amplifier mockup */
		.version_id = 0,
		.dais = {
			{
				.direction = {true, true},
				.dai_name = "sdw-mockup-aif1",
				.dai_type = SOC_SDW_DAI_TYPE_AMP,
				.dailink = {SOC_SDW_AMP_OUT_DAI_ID, SOC_SDW_AMP_IN_DAI_ID},
			},
		},
		.dai_num = 1,
	},
	{
		.part_id = 0x5555,
		.version_id = 0,
		.dais = {
			{
				.dai_name = "sdw-mockup-aif1",
				.direction = {false, true},
				.dai_type = SOC_SDW_DAI_TYPE_MIC,
				.dailink = {SOC_SDW_UNUSED_DAI_ID, SOC_SDW_DMIC_DAI_ID},
			},
		},
		.dai_num = 1,
	},
};
EXPORT_SYMBOL_NS(codec_info_list, "SND_SOC_SDW_UTILS");

int asoc_sdw_get_codec_info_list_count(void)
{
	return ARRAY_SIZE(codec_info_list);
};
EXPORT_SYMBOL_NS(asoc_sdw_get_codec_info_list_count, "SND_SOC_SDW_UTILS");

struct asoc_sdw_codec_info *asoc_sdw_find_codec_info_part(const u64 adr)
{
	unsigned int part_id, sdw_version;
	int i;

	part_id = SDW_PART_ID(adr);
	sdw_version = SDW_VERSION(adr);
	for (i = 0; i < ARRAY_SIZE(codec_info_list); i++)
		/*
		 * A codec info is for all sdw version with the part id if
		 * version_id is not specified in the codec info.
		 */
		if (part_id == codec_info_list[i].part_id &&
		    (!codec_info_list[i].version_id ||
		     sdw_version == codec_info_list[i].version_id))
			return &codec_info_list[i];

	return NULL;
}
EXPORT_SYMBOL_NS(asoc_sdw_find_codec_info_part, "SND_SOC_SDW_UTILS");

struct asoc_sdw_codec_info *asoc_sdw_find_codec_info_acpi(const u8 *acpi_id)
{
	int i;

	if (!acpi_id[0])
		return NULL;

	for (i = 0; i < ARRAY_SIZE(codec_info_list); i++)
		if (!memcmp(codec_info_list[i].acpi_id, acpi_id, ACPI_ID_LEN))
			return &codec_info_list[i];

	return NULL;
}
EXPORT_SYMBOL_NS(asoc_sdw_find_codec_info_acpi, "SND_SOC_SDW_UTILS");

struct asoc_sdw_codec_info *asoc_sdw_find_codec_info_dai(const char *dai_name, int *dai_index)
{
	int i, j;

	for (i = 0; i < ARRAY_SIZE(codec_info_list); i++) {
		for (j = 0; j < codec_info_list[i].dai_num; j++) {
			if (!strcmp(codec_info_list[i].dais[j].dai_name, dai_name)) {
				*dai_index = j;
				return &codec_info_list[i];
			}
		}
	}

	return NULL;
}
EXPORT_SYMBOL_NS(asoc_sdw_find_codec_info_dai, "SND_SOC_SDW_UTILS");

int asoc_sdw_rtd_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_card *card = rtd->card;
	struct asoc_sdw_codec_info *codec_info;
	struct snd_soc_dai *dai;
	const char *spk_components="";
	int dai_index;
	int ret;
	int i;

	for_each_rtd_codec_dais(rtd, i, dai) {
		codec_info = asoc_sdw_find_codec_info_dai(dai->name, &dai_index);
		if (!codec_info)
			return -EINVAL;

		/*
		 * A codec dai can be connected to different dai links for capture and playback,
		 * but we only need to call the rtd_init function once.
		 * The rtd_init for each codec dai is independent. So, the order of rtd_init
		 * doesn't matter.
		 */
		if (codec_info->dais[dai_index].rtd_init_done)
			continue;

		/*
		 * Add card controls and dapm widgets for the first codec dai.
		 * The controls and widgets will be used for all codec dais.
		 */

		if (i > 0)
			goto skip_add_controls_widgets;

		if (codec_info->dais[dai_index].controls) {
			ret = snd_soc_add_card_controls(card, codec_info->dais[dai_index].controls,
							codec_info->dais[dai_index].num_controls);
			if (ret) {
				dev_err(card->dev, "%#x controls addition failed: %d\n",
					codec_info->part_id, ret);
				return ret;
			}
		}
		if (codec_info->dais[dai_index].widgets) {
			ret = snd_soc_dapm_new_controls(&card->dapm,
							codec_info->dais[dai_index].widgets,
							codec_info->dais[dai_index].num_widgets);
			if (ret) {
				dev_err(card->dev, "%#x widgets addition failed: %d\n",
					codec_info->part_id, ret);
				return ret;
			}
		}

skip_add_controls_widgets:
		if (codec_info->dais[dai_index].rtd_init) {
			ret = codec_info->dais[dai_index].rtd_init(rtd, dai);
			if (ret)
				return ret;
		}

		/* Generate the spk component string for card->components string */
		if (codec_info->dais[dai_index].dai_type == SOC_SDW_DAI_TYPE_AMP &&
		    codec_info->dais[dai_index].component_name) {
			if (strlen (spk_components) == 0)
				spk_components =
					devm_kasprintf(card->dev, GFP_KERNEL, "%s",
						       codec_info->dais[dai_index].component_name);
			else
				/* Append component name to spk_components */
				spk_components =
					devm_kasprintf(card->dev, GFP_KERNEL,
						       "%s+%s", spk_components,
						       codec_info->dais[dai_index].component_name);
		}

		codec_info->dais[dai_index].rtd_init_done = true;

	}

	if (strlen (spk_components) > 0) {
		/* Update card components for speaker components */
		card->components = devm_kasprintf(card->dev, GFP_KERNEL, "%s spk:%s",
						  card->components, spk_components);
		if (!card->components)
			return -ENOMEM;
	}

	return 0;
}
EXPORT_SYMBOL_NS(asoc_sdw_rtd_init, "SND_SOC_SDW_UTILS");

/* these wrappers are only needed to avoid typecast compilation errors */
int asoc_sdw_startup(struct snd_pcm_substream *substream)
{
	return sdw_startup_stream(substream);
}
EXPORT_SYMBOL_NS(asoc_sdw_startup, "SND_SOC_SDW_UTILS");

int asoc_sdw_prepare(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct sdw_stream_runtime *sdw_stream;
	struct snd_soc_dai *dai;

	/* Find stream from first CPU DAI */
	dai = snd_soc_rtd_to_cpu(rtd, 0);

	sdw_stream = snd_soc_dai_get_stream(dai, substream->stream);
	if (IS_ERR(sdw_stream)) {
		dev_err(rtd->dev, "no stream found for DAI %s\n", dai->name);
		return PTR_ERR(sdw_stream);
	}

	return sdw_prepare_stream(sdw_stream);
}
EXPORT_SYMBOL_NS(asoc_sdw_prepare, "SND_SOC_SDW_UTILS");

int asoc_sdw_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct sdw_stream_runtime *sdw_stream;
	struct snd_soc_dai *dai;
	int ret;

	/* Find stream from first CPU DAI */
	dai = snd_soc_rtd_to_cpu(rtd, 0);

	sdw_stream = snd_soc_dai_get_stream(dai, substream->stream);
	if (IS_ERR(sdw_stream)) {
		dev_err(rtd->dev, "no stream found for DAI %s\n", dai->name);
		return PTR_ERR(sdw_stream);
	}

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
	case SNDRV_PCM_TRIGGER_RESUME:
		ret = sdw_enable_stream(sdw_stream);
		break;

	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_STOP:
		ret = sdw_disable_stream(sdw_stream);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	if (ret)
		dev_err(rtd->dev, "%s trigger %d failed: %d\n", __func__, cmd, ret);

	return ret;
}
EXPORT_SYMBOL_NS(asoc_sdw_trigger, "SND_SOC_SDW_UTILS");

int asoc_sdw_hw_params(struct snd_pcm_substream *substream,
		       struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai_link_ch_map *ch_maps;
	int ch = params_channels(params);
	unsigned int ch_mask;
	int num_codecs;
	int step;
	int i;

	if (!rtd->dai_link->ch_maps)
		return 0;

	/* Identical data will be sent to all codecs in playback */
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		ch_mask = GENMASK(ch - 1, 0);
		step = 0;
	} else {
		num_codecs = rtd->dai_link->num_codecs;

		if (ch < num_codecs || ch % num_codecs != 0) {
			dev_err(rtd->dev, "Channels number %d is invalid when codec number = %d\n",
				ch, num_codecs);
			return -EINVAL;
		}

		ch_mask = GENMASK(ch / num_codecs - 1, 0);
		step = hweight_long(ch_mask);
	}

	/*
	 * The captured data will be combined from each cpu DAI if the dai
	 * link has more than one codec DAIs. Set codec channel mask and
	 * ASoC will set the corresponding channel numbers for each cpu dai.
	 */
	for_each_link_ch_maps(rtd->dai_link, i, ch_maps)
		ch_maps->ch_mask = ch_mask << (i * step);

	return 0;
}
EXPORT_SYMBOL_NS(asoc_sdw_hw_params, "SND_SOC_SDW_UTILS");

int asoc_sdw_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct sdw_stream_runtime *sdw_stream;
	struct snd_soc_dai *dai;

	/* Find stream from first CPU DAI */
	dai = snd_soc_rtd_to_cpu(rtd, 0);

	sdw_stream = snd_soc_dai_get_stream(dai, substream->stream);
	if (IS_ERR(sdw_stream)) {
		dev_err(rtd->dev, "no stream found for DAI %s\n", dai->name);
		return PTR_ERR(sdw_stream);
	}

	return sdw_deprepare_stream(sdw_stream);
}
EXPORT_SYMBOL_NS(asoc_sdw_hw_free, "SND_SOC_SDW_UTILS");

void asoc_sdw_shutdown(struct snd_pcm_substream *substream)
{
	sdw_shutdown_stream(substream);
}
EXPORT_SYMBOL_NS(asoc_sdw_shutdown, "SND_SOC_SDW_UTILS");

static bool asoc_sdw_is_unique_device(const struct snd_soc_acpi_link_adr *adr_link,
				      unsigned int sdw_version,
				      unsigned int mfg_id,
				      unsigned int part_id,
				      unsigned int class_id,
				      int index_in_link)
{
	int i;

	for (i = 0; i < adr_link->num_adr; i++) {
		unsigned int sdw1_version, mfg1_id, part1_id, class1_id;
		u64 adr;

		/* skip itself */
		if (i == index_in_link)
			continue;

		adr = adr_link->adr_d[i].adr;

		sdw1_version = SDW_VERSION(adr);
		mfg1_id = SDW_MFG_ID(adr);
		part1_id = SDW_PART_ID(adr);
		class1_id = SDW_CLASS_ID(adr);

		if (sdw_version == sdw1_version &&
		    mfg_id == mfg1_id &&
		    part_id == part1_id &&
		    class_id == class1_id)
			return false;
	}

	return true;
}

static const char *_asoc_sdw_get_codec_name(struct device *dev,
					    const struct asoc_sdw_codec_info *codec_info,
					    const struct snd_soc_acpi_link_adr *adr_link,
					    int adr_index)
{
	u64 adr = adr_link->adr_d[adr_index].adr;
	unsigned int sdw_version = SDW_VERSION(adr);
	unsigned int link_id = SDW_DISCO_LINK_ID(adr);
	unsigned int unique_id = SDW_UNIQUE_ID(adr);
	unsigned int mfg_id = SDW_MFG_ID(adr);
	unsigned int part_id = SDW_PART_ID(adr);
	unsigned int class_id = SDW_CLASS_ID(adr);

	if (asoc_sdw_is_unique_device(adr_link, sdw_version, mfg_id, part_id,
				      class_id, adr_index))
		return devm_kasprintf(dev, GFP_KERNEL, "sdw:0:%01x:%04x:%04x:%02x",
				      link_id, mfg_id, part_id, class_id);

	return devm_kasprintf(dev, GFP_KERNEL, "sdw:0:%01x:%04x:%04x:%02x:%01x",
			      link_id, mfg_id, part_id, class_id, unique_id);
}

const char *asoc_sdw_get_codec_name(struct device *dev,
				    const struct asoc_sdw_codec_info *codec_info,
				    const struct snd_soc_acpi_link_adr *adr_link,
				    int adr_index)
{
	if (codec_info->codec_name)
		return devm_kstrdup(dev, codec_info->codec_name, GFP_KERNEL);

	return _asoc_sdw_get_codec_name(dev, codec_info, adr_link, adr_index);
}
EXPORT_SYMBOL_NS(asoc_sdw_get_codec_name, "SND_SOC_SDW_UTILS");

/* helper to get the link that the codec DAI is used */
struct snd_soc_dai_link *asoc_sdw_mc_find_codec_dai_used(struct snd_soc_card *card,
							 const char *dai_name)
{
	struct snd_soc_dai_link *dai_link;
	int i;
	int j;

	for_each_card_prelinks(card, i, dai_link) {
		for (j = 0; j < dai_link->num_codecs; j++) {
			/* Check each codec in a link */
			if (!strcmp(dai_link->codecs[j].dai_name, dai_name))
				return dai_link;
		}
	}
	return NULL;
}
EXPORT_SYMBOL_NS(asoc_sdw_mc_find_codec_dai_used, "SND_SOC_SDW_UTILS");

void asoc_sdw_mc_dailink_exit_loop(struct snd_soc_card *card)
{
	struct snd_soc_dai_link *dai_link;
	struct asoc_sdw_mc_private *ctx = snd_soc_card_get_drvdata(card);
	int ret;
	int i, j;

	for (i = 0; i < ctx->codec_info_list_count; i++) {
		for (j = 0; j < codec_info_list[i].dai_num; j++) {
			codec_info_list[i].dais[j].rtd_init_done = false;
			/* Check each dai in codec_info_lis to see if it is used in the link */
			if (!codec_info_list[i].dais[j].exit)
				continue;
			/*
			 * We don't need to call .exit function if there is no matched
			 * dai link found.
			 */
			dai_link = asoc_sdw_mc_find_codec_dai_used(card,
							  codec_info_list[i].dais[j].dai_name);
			if (dai_link) {
				/* Do the .exit function if the codec dai is used in the link */
				ret = codec_info_list[i].dais[j].exit(card, dai_link);
				if (ret)
					dev_warn(card->dev,
						 "codec exit failed %d\n",
						 ret);
				break;
			}
		}
	}
}
EXPORT_SYMBOL_NS(asoc_sdw_mc_dailink_exit_loop, "SND_SOC_SDW_UTILS");

int asoc_sdw_card_late_probe(struct snd_soc_card *card)
{
	int ret = 0;
	int i;

	for (i = 0; i < ARRAY_SIZE(codec_info_list); i++) {
		if (codec_info_list[i].codec_card_late_probe) {
			ret = codec_info_list[i].codec_card_late_probe(card);
			if (ret < 0)
				return ret;
		}
	}
	return ret;
}
EXPORT_SYMBOL_NS(asoc_sdw_card_late_probe, "SND_SOC_SDW_UTILS");

void asoc_sdw_init_dai_link(struct device *dev, struct snd_soc_dai_link *dai_links,
			    int *be_id, char *name, int playback, int capture,
			    struct snd_soc_dai_link_component *cpus, int cpus_num,
			    struct snd_soc_dai_link_component *platform_component,
			    int num_platforms, struct snd_soc_dai_link_component *codecs,
			    int codecs_num, int no_pcm,
			    int (*init)(struct snd_soc_pcm_runtime *rtd),
			    const struct snd_soc_ops *ops)
{
	dev_dbg(dev, "create dai link %s, id %d\n", name, *be_id);
	dai_links->id = (*be_id)++;
	dai_links->name = name;
	dai_links->stream_name = name;
	dai_links->platforms = platform_component;
	dai_links->num_platforms = num_platforms;
	dai_links->no_pcm = no_pcm;
	dai_links->cpus = cpus;
	dai_links->num_cpus = cpus_num;
	dai_links->codecs = codecs;
	dai_links->num_codecs = codecs_num;
	dai_links->playback_only =  playback && !capture;
	dai_links->capture_only  = !playback &&  capture;
	dai_links->init = init;
	dai_links->ops = ops;
}
EXPORT_SYMBOL_NS(asoc_sdw_init_dai_link, "SND_SOC_SDW_UTILS");

int asoc_sdw_init_simple_dai_link(struct device *dev, struct snd_soc_dai_link *dai_links,
				  int *be_id, char *name, int playback, int capture,
				  const char *cpu_dai_name, const char *platform_comp_name,
				  const char *codec_name, const char *codec_dai_name,
				  int no_pcm, int (*init)(struct snd_soc_pcm_runtime *rtd),
				  const struct snd_soc_ops *ops)
{
	struct snd_soc_dai_link_component *dlc;

	/* Allocate three DLCs one for the CPU, one for platform and one for the CODEC */
	dlc = devm_kcalloc(dev, 3, sizeof(*dlc), GFP_KERNEL);
	if (!dlc || !name || !cpu_dai_name || !platform_comp_name || !codec_name || !codec_dai_name)
		return -ENOMEM;

	dlc[0].dai_name = cpu_dai_name;
	dlc[1].name = platform_comp_name;

	dlc[2].name = codec_name;
	dlc[2].dai_name = codec_dai_name;

	asoc_sdw_init_dai_link(dev, dai_links, be_id, name, playback, capture,
			       &dlc[0], 1, &dlc[1], 1, &dlc[2], 1,
			       no_pcm, init, ops);

	return 0;
}
EXPORT_SYMBOL_NS(asoc_sdw_init_simple_dai_link, "SND_SOC_SDW_UTILS");

int asoc_sdw_count_sdw_endpoints(struct snd_soc_card *card, int *num_devs, int *num_ends)
{
	struct device *dev = card->dev;
	struct snd_soc_acpi_mach *mach = dev_get_platdata(dev);
	struct snd_soc_acpi_mach_params *mach_params = &mach->mach_params;
	const struct snd_soc_acpi_link_adr *adr_link;
	int i;

	for (adr_link = mach_params->links; adr_link->num_adr; adr_link++) {
		*num_devs += adr_link->num_adr;

		for (i = 0; i < adr_link->num_adr; i++)
			*num_ends += adr_link->adr_d[i].num_endpoints;
	}

	dev_dbg(dev, "Found %d devices with %d endpoints\n", *num_devs, *num_ends);

	return 0;
}
EXPORT_SYMBOL_NS(asoc_sdw_count_sdw_endpoints, "SND_SOC_SDW_UTILS");

struct asoc_sdw_dailink *asoc_sdw_find_dailink(struct asoc_sdw_dailink *dailinks,
					       const struct snd_soc_acpi_endpoint *new)
{
	while (dailinks->initialised) {
		if (new->aggregated && dailinks->group_id == new->group_id)
			return dailinks;

		dailinks++;
	}

	INIT_LIST_HEAD(&dailinks->endpoints);
	dailinks->group_id = new->group_id;
	dailinks->initialised = true;

	return dailinks;
}
EXPORT_SYMBOL_NS(asoc_sdw_find_dailink, "SND_SOC_SDW_UTILS");

static int asoc_sdw_get_dai_type(u32 type)
{
	switch (type) {
	case SDCA_FUNCTION_TYPE_SMART_AMP:
	case SDCA_FUNCTION_TYPE_SIMPLE_AMP:
		return SOC_SDW_DAI_TYPE_AMP;
	case SDCA_FUNCTION_TYPE_SMART_MIC:
	case SDCA_FUNCTION_TYPE_SIMPLE_MIC:
	case SDCA_FUNCTION_TYPE_SPEAKER_MIC:
		return SOC_SDW_DAI_TYPE_MIC;
	case SDCA_FUNCTION_TYPE_UAJ:
	case SDCA_FUNCTION_TYPE_RJ:
	case SDCA_FUNCTION_TYPE_SIMPLE_JACK:
		return SOC_SDW_DAI_TYPE_JACK;
	default:
		return -EINVAL;
	}
}

/*
 * Check if the SDCA endpoint is present by the SDW peripheral
 *
 * @dev: Device pointer
 * @codec_info: Codec info pointer
 * @adr_link: ACPI link address
 * @adr_index: Index of the ACPI link address
 * @end_index: Index of the endpoint
 *
 * Return: 1 if the endpoint is present,
 *	   0 if the endpoint is not present,
 *	   negative error code.
 */

static int is_sdca_endpoint_present(struct device *dev,
				    struct asoc_sdw_codec_info *codec_info,
				    const struct snd_soc_acpi_link_adr *adr_link,
				    int adr_index, int end_index)
{
	const struct snd_soc_acpi_adr_device *adr_dev = &adr_link->adr_d[adr_index];
	const struct snd_soc_acpi_endpoint *adr_end;
	const struct asoc_sdw_dai_info *dai_info;
	struct snd_soc_dai_link_component *dlc;
	struct snd_soc_dai *codec_dai;
	struct sdw_slave *slave;
	struct device *sdw_dev;
	const char *sdw_codec_name;
	int i;

	dlc = kzalloc(sizeof(*dlc), GFP_KERNEL);
	if (!dlc)
		return -ENOMEM;

	adr_end = &adr_dev->endpoints[end_index];
	dai_info = &codec_info->dais[adr_end->num];

	dlc->dai_name = dai_info->dai_name;
	codec_dai = snd_soc_find_dai_with_mutex(dlc);
	if (!codec_dai) {
		dev_warn(dev, "codec dai %s not registered yet\n", dlc->dai_name);
		kfree(dlc);
		return -EPROBE_DEFER;
	}
	kfree(dlc);

	sdw_codec_name = _asoc_sdw_get_codec_name(dev, codec_info,
						  adr_link, adr_index);
	if (!sdw_codec_name)
		return -ENOMEM;

	sdw_dev = bus_find_device_by_name(&sdw_bus_type, NULL, sdw_codec_name);
	if (!sdw_dev) {
		dev_err(dev, "codec %s not found\n", sdw_codec_name);
		return -EINVAL;
	}

	slave = dev_to_sdw_dev(sdw_dev);
	if (!slave)
		return -EINVAL;

	/* Make sure BIOS provides SDCA properties */
	if (!slave->sdca_data.interface_revision) {
		dev_warn(&slave->dev, "SDCA properties not found in the BIOS\n");
		return 1;
	}

	for (i = 0; i < slave->sdca_data.num_functions; i++) {
		int dai_type = asoc_sdw_get_dai_type(slave->sdca_data.function[i].type);

		if (dai_type == dai_info->dai_type) {
			dev_dbg(&slave->dev, "DAI type %d sdca function %s found\n",
				dai_type, slave->sdca_data.function[i].name);
			return 1;
		}
	}

	dev_dbg(&slave->dev,
		"SDCA device function for DAI type %d not supported, skip endpoint\n",
		dai_info->dai_type);

	return 0;
}

int asoc_sdw_parse_sdw_endpoints(struct snd_soc_card *card,
				 struct asoc_sdw_dailink *soc_dais,
				 struct asoc_sdw_endpoint *soc_ends,
				 int *num_devs)
{
	struct device *dev = card->dev;
	struct asoc_sdw_mc_private *ctx = snd_soc_card_get_drvdata(card);
	struct snd_soc_acpi_mach *mach = dev_get_platdata(dev);
	struct snd_soc_acpi_mach_params *mach_params = &mach->mach_params;
	const struct snd_soc_acpi_link_adr *adr_link;
	struct asoc_sdw_endpoint *soc_end = soc_ends;
	int num_dais = 0;
	int i, j;
	int ret;

	for (adr_link = mach_params->links; adr_link->num_adr; adr_link++) {
		int num_link_dailinks = 0;

		if (!is_power_of_2(adr_link->mask)) {
			dev_err(dev, "link with multiple mask bits: 0x%x\n",
				adr_link->mask);
			return -EINVAL;
		}

		for (i = 0; i < adr_link->num_adr; i++) {
			const struct snd_soc_acpi_adr_device *adr_dev = &adr_link->adr_d[i];
			struct asoc_sdw_codec_info *codec_info;
			const char *codec_name;
			bool check_sdca = false;

			if (!adr_dev->name_prefix) {
				dev_err(dev, "codec 0x%llx does not have a name prefix\n",
					adr_dev->adr);
				return -EINVAL;
			}

			codec_info = asoc_sdw_find_codec_info_part(adr_dev->adr);
			if (!codec_info)
				return -EINVAL;

			ctx->ignore_internal_dmic |= codec_info->ignore_internal_dmic;

			codec_name = asoc_sdw_get_codec_name(dev, codec_info, adr_link, i);
			if (!codec_name)
				return -ENOMEM;

			dev_dbg(dev, "Adding prefix %s for %s\n",
				adr_dev->name_prefix, codec_name);

			soc_end->name_prefix = adr_dev->name_prefix;

			if (codec_info->count_sidecar && codec_info->add_sidecar) {
				ret = codec_info->count_sidecar(card, &num_dais, num_devs);
				if (ret)
					return ret;

				soc_end->include_sidecar = true;
			}

			if (SDW_CLASS_ID(adr_dev->adr) && adr_dev->num_endpoints > 1)
				check_sdca = true;

			for (j = 0; j < adr_dev->num_endpoints; j++) {
				const struct snd_soc_acpi_endpoint *adr_end;
				const struct asoc_sdw_dai_info *dai_info;
				struct asoc_sdw_dailink *soc_dai;
				int stream;

				adr_end = &adr_dev->endpoints[j];
				dai_info = &codec_info->dais[adr_end->num];
				soc_dai = asoc_sdw_find_dailink(soc_dais, adr_end);

				/*
				 * quirk should have higher priority than the sdca properties
				 * in the BIOS. We can't always check the DAI quirk because we
				 * will set the mc_quirk when the BIOS doesn't provide the right
				 * information. The endpoint will be skipped if the dai_info->
				 * quirk_exclude and mc_quirk are both not set if we always skip
				 * the endpoint according to the quirk information. We need to
				 * keep the endpoint if it is present in the BIOS. So, only
				 * check the DAI quirk when the mc_quirk is set or SDCA endpoint
				 * present check is not needed.
				 */
				if (dai_info->quirk & ctx->mc_quirk || !check_sdca) {
					/*
					 * Check the endpoint if a matching quirk is set or SDCA
					 * endpoint check is not necessary
					 */
					if (dai_info->quirk &&
					    !(dai_info->quirk_exclude ^ !!(dai_info->quirk & ctx->mc_quirk)))
						continue;
				} else {
					/* Check SDCA codec endpoint if there is no matching quirk */
					ret = is_sdca_endpoint_present(dev, codec_info, adr_link, i, j);
					if (ret < 0)
						return ret;

					/* The endpoint is not present, skip */
					if (!ret)
						continue;
				}

				dev_dbg(dev,
					"Add dev: %d, 0x%llx end: %d, dai: %d, %c/%c to %s: %d\n",
					ffs(adr_link->mask) - 1, adr_dev->adr,
					adr_end->num, dai_info->dai_type,
					dai_info->direction[SNDRV_PCM_STREAM_PLAYBACK] ? 'P' : '-',
					dai_info->direction[SNDRV_PCM_STREAM_CAPTURE] ? 'C' : '-',
					adr_end->aggregated ? "group" : "solo",
					adr_end->group_id);

				if (adr_end->num >= codec_info->dai_num) {
					dev_err(dev,
						"%d is too many endpoints for codec: 0x%x\n",
						adr_end->num, codec_info->part_id);
					return -EINVAL;
				}

				for_each_pcm_streams(stream) {
					if (dai_info->direction[stream] &&
					    dai_info->dailink[stream] < 0) {
						dev_err(dev,
							"Invalid dailink id %d for codec: 0x%x\n",
							dai_info->dailink[stream],
							codec_info->part_id);
						return -EINVAL;
					}

					if (dai_info->direction[stream]) {
						num_dais += !soc_dai->num_devs[stream];
						soc_dai->num_devs[stream]++;
						soc_dai->link_mask[stream] |= adr_link->mask;
					}
				}

				num_link_dailinks += !!list_empty(&soc_dai->endpoints);
				list_add_tail(&soc_end->list, &soc_dai->endpoints);

				soc_end->link_mask = adr_link->mask;
				soc_end->codec_name = codec_name;
				soc_end->codec_info = codec_info;
				soc_end->dai_info = dai_info;
				soc_end++;
			}
		}

		ctx->append_dai_type |= (num_link_dailinks > 1);
	}

	return num_dais;
}
EXPORT_SYMBOL_NS(asoc_sdw_parse_sdw_endpoints, "SND_SOC_SDW_UTILS");

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SoundWire ASoC helpers");
