// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
//
// cs35l45.c - CS35L45 ALSA SoC audio driver
//
// Copyright 2019-2022 Cirrus Logic, Inc.
//
// Author: James Schulman <james.schulman@cirrus.com>

#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/property.h>
#include <linux/regulator/consumer.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/tlv.h>

#include "cs35l45.h"

static int cs35l45_global_en_ev(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct cs35l45_private *cs35l45 = snd_soc_component_get_drvdata(component);

	dev_dbg(cs35l45->dev, "%s event : %x\n", __func__, event);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		regmap_write(cs35l45->regmap, CS35L45_GLOBAL_ENABLES,
			     CS35L45_GLOBAL_EN_MASK);

		usleep_range(CS35L45_POST_GLOBAL_EN_US, CS35L45_POST_GLOBAL_EN_US + 100);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		usleep_range(CS35L45_PRE_GLOBAL_DIS_US, CS35L45_PRE_GLOBAL_DIS_US + 100);

		regmap_write(cs35l45->regmap, CS35L45_GLOBAL_ENABLES, 0);
		break;
	default:
		break;
	}

	return 0;
}

static const char * const cs35l45_asp_tx_txt[] = {
	"Zero", "ASP_RX1", "ASP_RX2",
	"VMON", "IMON", "ERR_VOL",
	"VDD_BATTMON", "VDD_BSTMON",
	"Interpolator", "IL_TARGET",
};

static const unsigned int cs35l45_asp_tx_val[] = {
	CS35L45_PCM_SRC_ZERO, CS35L45_PCM_SRC_ASP_RX1, CS35L45_PCM_SRC_ASP_RX2,
	CS35L45_PCM_SRC_VMON, CS35L45_PCM_SRC_IMON, CS35L45_PCM_SRC_ERR_VOL,
	CS35L45_PCM_SRC_VDD_BATTMON, CS35L45_PCM_SRC_VDD_BSTMON,
	CS35L45_PCM_SRC_INTERPOLATOR, CS35L45_PCM_SRC_IL_TARGET,
};

static const struct soc_enum cs35l45_asp_tx_enums[] = {
	SOC_VALUE_ENUM_SINGLE(CS35L45_ASPTX1_INPUT, 0, CS35L45_PCM_SRC_MASK,
			      ARRAY_SIZE(cs35l45_asp_tx_txt), cs35l45_asp_tx_txt,
			      cs35l45_asp_tx_val),
	SOC_VALUE_ENUM_SINGLE(CS35L45_ASPTX2_INPUT, 0, CS35L45_PCM_SRC_MASK,
			      ARRAY_SIZE(cs35l45_asp_tx_txt), cs35l45_asp_tx_txt,
			      cs35l45_asp_tx_val),
	SOC_VALUE_ENUM_SINGLE(CS35L45_ASPTX3_INPUT, 0, CS35L45_PCM_SRC_MASK,
			      ARRAY_SIZE(cs35l45_asp_tx_txt), cs35l45_asp_tx_txt,
			      cs35l45_asp_tx_val),
	SOC_VALUE_ENUM_SINGLE(CS35L45_ASPTX4_INPUT, 0, CS35L45_PCM_SRC_MASK,
			      ARRAY_SIZE(cs35l45_asp_tx_txt), cs35l45_asp_tx_txt,
			      cs35l45_asp_tx_val),
	SOC_VALUE_ENUM_SINGLE(CS35L45_ASPTX5_INPUT, 0, CS35L45_PCM_SRC_MASK,
			      ARRAY_SIZE(cs35l45_asp_tx_txt), cs35l45_asp_tx_txt,
			      cs35l45_asp_tx_val),
};

static const char * const cs35l45_dac_txt[] = {
	"Zero", "ASP_RX1", "ASP_RX2"
};

static const unsigned int cs35l45_dac_val[] = {
	CS35L45_PCM_SRC_ZERO, CS35L45_PCM_SRC_ASP_RX1, CS35L45_PCM_SRC_ASP_RX2
};

static const struct soc_enum cs35l45_dacpcm_enums[] = {
	SOC_VALUE_ENUM_SINGLE(CS35L45_DACPCM1_INPUT, 0, CS35L45_PCM_SRC_MASK,
			      ARRAY_SIZE(cs35l45_dac_txt), cs35l45_dac_txt,
			      cs35l45_dac_val),
};

static const struct snd_kcontrol_new cs35l45_asp_muxes[] = {
	SOC_DAPM_ENUM("ASP_TX1 Source", cs35l45_asp_tx_enums[0]),
	SOC_DAPM_ENUM("ASP_TX2 Source", cs35l45_asp_tx_enums[1]),
	SOC_DAPM_ENUM("ASP_TX3 Source", cs35l45_asp_tx_enums[2]),
	SOC_DAPM_ENUM("ASP_TX4 Source", cs35l45_asp_tx_enums[3]),
	SOC_DAPM_ENUM("ASP_TX5 Source", cs35l45_asp_tx_enums[4]),
};

static const struct snd_kcontrol_new cs35l45_dac_muxes[] = {
	SOC_DAPM_ENUM("DACPCM1 Source", cs35l45_dacpcm_enums[0]),
};

static const struct snd_soc_dapm_widget cs35l45_dapm_widgets[] = {
	SND_SOC_DAPM_SUPPLY("GLOBAL_EN", SND_SOC_NOPM, 0, 0,
			    cs35l45_global_en_ev,
			    SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_SUPPLY("ASP_EN", CS35L45_BLOCK_ENABLES2, CS35L45_ASP_EN_SHIFT, 0, NULL, 0),

	SND_SOC_DAPM_SIGGEN("VMON_SRC"),
	SND_SOC_DAPM_SIGGEN("IMON_SRC"),
	SND_SOC_DAPM_SIGGEN("VDD_BATTMON_SRC"),
	SND_SOC_DAPM_SIGGEN("VDD_BSTMON_SRC"),
	SND_SOC_DAPM_SIGGEN("ERR_VOL"),
	SND_SOC_DAPM_SIGGEN("AMP_INTP"),
	SND_SOC_DAPM_SIGGEN("IL_TARGET"),
	SND_SOC_DAPM_ADC("VMON", NULL, CS35L45_BLOCK_ENABLES, CS35L45_VMON_EN_SHIFT, 0),
	SND_SOC_DAPM_ADC("IMON", NULL, CS35L45_BLOCK_ENABLES, CS35L45_IMON_EN_SHIFT, 0),
	SND_SOC_DAPM_ADC("VDD_BATTMON", NULL, CS35L45_BLOCK_ENABLES,
			 CS35L45_VDD_BATTMON_EN_SHIFT, 0),
	SND_SOC_DAPM_ADC("VDD_BSTMON", NULL, CS35L45_BLOCK_ENABLES,
			 CS35L45_VDD_BSTMON_EN_SHIFT, 0),

	SND_SOC_DAPM_AIF_IN("ASP_RX1", NULL, 0, CS35L45_ASP_ENABLES1, CS35L45_ASP_RX1_EN_SHIFT, 0),
	SND_SOC_DAPM_AIF_IN("ASP_RX2", NULL, 1, CS35L45_ASP_ENABLES1, CS35L45_ASP_RX2_EN_SHIFT, 0),

	SND_SOC_DAPM_AIF_OUT("ASP_TX1", NULL, 0, CS35L45_ASP_ENABLES1, CS35L45_ASP_TX1_EN_SHIFT, 0),
	SND_SOC_DAPM_AIF_OUT("ASP_TX2", NULL, 1, CS35L45_ASP_ENABLES1, CS35L45_ASP_TX2_EN_SHIFT, 0),
	SND_SOC_DAPM_AIF_OUT("ASP_TX3", NULL, 2, CS35L45_ASP_ENABLES1, CS35L45_ASP_TX3_EN_SHIFT, 0),
	SND_SOC_DAPM_AIF_OUT("ASP_TX4", NULL, 3, CS35L45_ASP_ENABLES1, CS35L45_ASP_TX4_EN_SHIFT, 0),
	SND_SOC_DAPM_AIF_OUT("ASP_TX5", NULL, 3, CS35L45_ASP_ENABLES1, CS35L45_ASP_TX5_EN_SHIFT, 0),

	SND_SOC_DAPM_MUX("ASP_TX1 Source", SND_SOC_NOPM, 0, 0, &cs35l45_asp_muxes[0]),
	SND_SOC_DAPM_MUX("ASP_TX2 Source", SND_SOC_NOPM, 0, 0, &cs35l45_asp_muxes[1]),
	SND_SOC_DAPM_MUX("ASP_TX3 Source", SND_SOC_NOPM, 0, 0, &cs35l45_asp_muxes[2]),
	SND_SOC_DAPM_MUX("ASP_TX4 Source", SND_SOC_NOPM, 0, 0, &cs35l45_asp_muxes[3]),
	SND_SOC_DAPM_MUX("ASP_TX5 Source", SND_SOC_NOPM, 0, 0, &cs35l45_asp_muxes[4]),

	SND_SOC_DAPM_MUX("DACPCM1 Source", SND_SOC_NOPM, 0, 0, &cs35l45_dac_muxes[0]),

	SND_SOC_DAPM_OUT_DRV("AMP", SND_SOC_NOPM, 0, 0, NULL, 0),

	SND_SOC_DAPM_OUTPUT("SPK"),
};

#define CS35L45_ASP_MUX_ROUTE(name) \
	{ name" Source", "ASP_RX1",	 "ASP_RX1" }, \
	{ name" Source", "ASP_RX2",	 "ASP_RX2" }, \
	{ name" Source", "VMON",	 "VMON" }, \
	{ name" Source", "IMON",	 "IMON" }, \
	{ name" Source", "ERR_VOL",	 "ERR_VOL" }, \
	{ name" Source", "VDD_BATTMON",	 "VDD_BATTMON" }, \
	{ name" Source", "VDD_BSTMON",	 "VDD_BSTMON" }, \
	{ name" Source", "Interpolator", "AMP_INTP" }, \
	{ name" Source", "IL_TARGET",	 "IL_TARGET" }

#define CS35L45_DAC_MUX_ROUTE(name) \
	{ name" Source", "ASP_RX1",	"ASP_RX1" }, \
	{ name" Source", "ASP_RX2",	"ASP_RX2" }

static const struct snd_soc_dapm_route cs35l45_dapm_routes[] = {
	/* Feedback */
	{ "VMON", NULL, "VMON_SRC" },
	{ "IMON", NULL, "IMON_SRC" },
	{ "VDD_BATTMON", NULL, "VDD_BATTMON_SRC" },
	{ "VDD_BSTMON", NULL, "VDD_BSTMON_SRC" },

	{ "Capture", NULL, "ASP_TX1"},
	{ "Capture", NULL, "ASP_TX2"},
	{ "Capture", NULL, "ASP_TX3"},
	{ "Capture", NULL, "ASP_TX4"},
	{ "Capture", NULL, "ASP_TX5"},
	{ "ASP_TX1", NULL, "ASP_TX1 Source"},
	{ "ASP_TX2", NULL, "ASP_TX2 Source"},
	{ "ASP_TX3", NULL, "ASP_TX3 Source"},
	{ "ASP_TX4", NULL, "ASP_TX4 Source"},
	{ "ASP_TX5", NULL, "ASP_TX5 Source"},

	{ "ASP_TX1", NULL, "ASP_EN" },
	{ "ASP_TX2", NULL, "ASP_EN" },
	{ "ASP_TX3", NULL, "ASP_EN" },
	{ "ASP_TX4", NULL, "ASP_EN" },
	{ "ASP_TX1", NULL, "GLOBAL_EN" },
	{ "ASP_TX2", NULL, "GLOBAL_EN" },
	{ "ASP_TX3", NULL, "GLOBAL_EN" },
	{ "ASP_TX4", NULL, "GLOBAL_EN" },
	{ "ASP_TX5", NULL, "GLOBAL_EN" },

	CS35L45_ASP_MUX_ROUTE("ASP_TX1"),
	CS35L45_ASP_MUX_ROUTE("ASP_TX2"),
	CS35L45_ASP_MUX_ROUTE("ASP_TX3"),
	CS35L45_ASP_MUX_ROUTE("ASP_TX4"),
	CS35L45_ASP_MUX_ROUTE("ASP_TX5"),

	/* Playback */
	{ "ASP_RX1", NULL, "Playback" },
	{ "ASP_RX2", NULL, "Playback" },
	{ "ASP_RX1", NULL, "ASP_EN" },
	{ "ASP_RX2", NULL, "ASP_EN" },

	{ "AMP", NULL, "DACPCM1 Source"},
	{ "AMP", NULL, "GLOBAL_EN"},

	CS35L45_DAC_MUX_ROUTE("DACPCM1"),

	{ "SPK", NULL, "AMP"},
};

static const DECLARE_TLV_DB_SCALE(cs35l45_dig_pcm_vol_tlv, -10225, 25, true);

static const struct snd_kcontrol_new cs35l45_controls[] = {
	/* Ignore bit 0: it is beyond the resolution of TLV_DB_SCALE */
	SOC_SINGLE_S_TLV("Digital PCM Volume",
			 CS35L45_AMP_PCM_CONTROL,
			 CS35L45_AMP_VOL_PCM_SHIFT + 1,
			 -409, 48,
			 (CS35L45_AMP_VOL_PCM_WIDTH - 1) - 1,
			 0, cs35l45_dig_pcm_vol_tlv),
};

static int cs35l45_set_pll(struct cs35l45_private *cs35l45, unsigned int freq)
{
	unsigned int val;
	int freq_id;

	freq_id = cs35l45_get_clk_freq_id(freq);
	if (freq_id < 0) {
		dev_err(cs35l45->dev, "Invalid freq: %u\n", freq);
		return -EINVAL;
	}

	regmap_read(cs35l45->regmap, CS35L45_REFCLK_INPUT, &val);
	val = (val & CS35L45_PLL_REFCLK_FREQ_MASK) >> CS35L45_PLL_REFCLK_FREQ_SHIFT;
	if (val == freq_id)
		return 0;

	regmap_set_bits(cs35l45->regmap, CS35L45_REFCLK_INPUT, CS35L45_PLL_OPEN_LOOP_MASK);
	regmap_update_bits(cs35l45->regmap, CS35L45_REFCLK_INPUT,
			   CS35L45_PLL_REFCLK_FREQ_MASK,
			   freq_id << CS35L45_PLL_REFCLK_FREQ_SHIFT);
	regmap_clear_bits(cs35l45->regmap, CS35L45_REFCLK_INPUT, CS35L45_PLL_REFCLK_EN_MASK);
	regmap_clear_bits(cs35l45->regmap, CS35L45_REFCLK_INPUT, CS35L45_PLL_OPEN_LOOP_MASK);
	regmap_set_bits(cs35l45->regmap, CS35L45_REFCLK_INPUT, CS35L45_PLL_REFCLK_EN_MASK);

	return 0;
}

static int cs35l45_asp_set_fmt(struct snd_soc_dai *codec_dai, unsigned int fmt)
{
	struct cs35l45_private *cs35l45 = snd_soc_component_get_drvdata(codec_dai->component);
	unsigned int asp_fmt, fsync_inv, bclk_inv;

	switch (fmt & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) {
	case SND_SOC_DAIFMT_CBC_CFC:
		break;
	default:
		dev_err(cs35l45->dev, "Invalid DAI clocking\n");
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_DSP_A:
		asp_fmt = CS35l45_ASP_FMT_DSP_A;
		break;
	case SND_SOC_DAIFMT_I2S:
		asp_fmt = CS35L45_ASP_FMT_I2S;
		break;
	default:
		dev_err(cs35l45->dev, "Invalid DAI format\n");
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_IF:
		fsync_inv = 1;
		bclk_inv = 0;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		fsync_inv = 0;
		bclk_inv = 1;
		break;
	case SND_SOC_DAIFMT_IB_IF:
		fsync_inv = 1;
		bclk_inv = 1;
		break;
	case SND_SOC_DAIFMT_NB_NF:
		fsync_inv = 0;
		bclk_inv = 0;
		break;
	default:
		dev_warn(cs35l45->dev, "Invalid DAI clock polarity\n");
		return -EINVAL;
	}

	regmap_update_bits(cs35l45->regmap, CS35L45_ASP_CONTROL2,
			   CS35L45_ASP_FMT_MASK |
			   CS35L45_ASP_FSYNC_INV_MASK |
			   CS35L45_ASP_BCLK_INV_MASK,
			   (asp_fmt << CS35L45_ASP_FMT_SHIFT) |
			   (fsync_inv << CS35L45_ASP_FSYNC_INV_SHIFT) |
			   (bclk_inv << CS35L45_ASP_BCLK_INV_SHIFT));

	return 0;
}

static int cs35l45_asp_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params,
				 struct snd_soc_dai *dai)
{
	struct cs35l45_private *cs35l45 = snd_soc_component_get_drvdata(dai->component);
	unsigned int asp_width, asp_wl, global_fs, slot_multiple, asp_fmt;
	int bclk;

	switch (params_rate(params)) {
	case 44100:
		global_fs = CS35L45_44P100_KHZ;
		break;
	case 48000:
		global_fs = CS35L45_48P0_KHZ;
		break;
	case 88200:
		global_fs = CS35L45_88P200_KHZ;
		break;
	case 96000:
		global_fs = CS35L45_96P0_KHZ;
		break;
	default:
		dev_warn(cs35l45->dev, "Unsupported sample rate (%d)\n",
			 params_rate(params));
		return -EINVAL;
	}

	regmap_update_bits(cs35l45->regmap, CS35L45_GLOBAL_SAMPLE_RATE,
			   CS35L45_GLOBAL_FS_MASK,
			   global_fs << CS35L45_GLOBAL_FS_SHIFT);

	asp_wl = params_width(params);

	if (cs35l45->slot_width)
		asp_width = cs35l45->slot_width;
	else
		asp_width = params_width(params);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		regmap_update_bits(cs35l45->regmap, CS35L45_ASP_CONTROL2,
				   CS35L45_ASP_WIDTH_RX_MASK,
				   asp_width << CS35L45_ASP_WIDTH_RX_SHIFT);

		regmap_update_bits(cs35l45->regmap, CS35L45_ASP_DATA_CONTROL5,
				   CS35L45_ASP_WL_MASK,
				   asp_wl << CS35L45_ASP_WL_SHIFT);
	} else {
		regmap_update_bits(cs35l45->regmap, CS35L45_ASP_CONTROL2,
				   CS35L45_ASP_WIDTH_TX_MASK,
				   asp_width << CS35L45_ASP_WIDTH_TX_SHIFT);

		regmap_update_bits(cs35l45->regmap, CS35L45_ASP_DATA_CONTROL1,
				   CS35L45_ASP_WL_MASK,
				   asp_wl << CS35L45_ASP_WL_SHIFT);
	}

	if (cs35l45->sysclk_set)
		return 0;

	/* I2S always has an even number of channels */
	regmap_read(cs35l45->regmap, CS35L45_ASP_CONTROL2, &asp_fmt);
	asp_fmt = (asp_fmt & CS35L45_ASP_FMT_MASK) >> CS35L45_ASP_FMT_SHIFT;
	if (asp_fmt == CS35L45_ASP_FMT_I2S)
		slot_multiple = 2;
	else
		slot_multiple = 1;

	bclk = snd_soc_tdm_params_to_bclk(params, asp_width,
					  cs35l45->slot_count, slot_multiple);

	return cs35l45_set_pll(cs35l45, bclk);
}

static int cs35l45_asp_set_tdm_slot(struct snd_soc_dai *dai,
				    unsigned int tx_mask, unsigned int rx_mask,
				    int slots, int slot_width)
{
	struct cs35l45_private *cs35l45 = snd_soc_component_get_drvdata(dai->component);

	if (slot_width && ((slot_width < 16) || (slot_width > 128)))
		return -EINVAL;

	cs35l45->slot_width = slot_width;
	cs35l45->slot_count = slots;

	return 0;
}

static int cs35l45_asp_set_sysclk(struct snd_soc_dai *dai,
				  int clk_id, unsigned int freq, int dir)
{
	struct cs35l45_private *cs35l45 = snd_soc_component_get_drvdata(dai->component);
	int ret;

	if (clk_id != 0) {
		dev_err(cs35l45->dev, "Invalid clk_id %d\n", clk_id);
		return -EINVAL;
	}

	cs35l45->sysclk_set = false;
	if (freq == 0)
		return 0;

	ret = cs35l45_set_pll(cs35l45, freq);
	if (ret < 0)
		return -EINVAL;

	cs35l45->sysclk_set = true;

	return 0;
}

static int cs35l45_mute_stream(struct snd_soc_dai *dai, int mute, int stream)
{
	struct cs35l45_private *cs35l45 = snd_soc_component_get_drvdata(dai->component);
	unsigned int global_fs, val, hpf_tune;

	if (mute)
		return 0;

	regmap_read(cs35l45->regmap, CS35L45_GLOBAL_SAMPLE_RATE, &global_fs);
	global_fs = (global_fs & CS35L45_GLOBAL_FS_MASK) >> CS35L45_GLOBAL_FS_SHIFT;
	switch (global_fs) {
	case CS35L45_44P100_KHZ:
		hpf_tune = CS35L45_HPF_44P1;
		break;
	case CS35L45_88P200_KHZ:
		hpf_tune = CS35L45_HPF_88P2;
		break;
	default:
		hpf_tune = CS35l45_HPF_DEFAULT;
		break;
	}

	regmap_read(cs35l45->regmap, CS35L45_AMP_PCM_HPF_TST, &val);
	if (val != hpf_tune) {
		struct reg_sequence hpf_override_seq[] = {
			{ 0x00000040,			0x00000055 },
			{ 0x00000040,			0x000000AA },
			{ 0x00000044,			0x00000055 },
			{ 0x00000044,			0x000000AA },
			{ CS35L45_AMP_PCM_HPF_TST,	hpf_tune },
			{ 0x00000040,			0x00000000 },
			{ 0x00000044,			0x00000000 },
		};
		regmap_multi_reg_write(cs35l45->regmap, hpf_override_seq,
				       ARRAY_SIZE(hpf_override_seq));
	}

	return 0;
}

static const struct snd_soc_dai_ops cs35l45_asp_dai_ops = {
	.set_fmt = cs35l45_asp_set_fmt,
	.hw_params = cs35l45_asp_hw_params,
	.set_tdm_slot = cs35l45_asp_set_tdm_slot,
	.set_sysclk = cs35l45_asp_set_sysclk,
	.mute_stream = cs35l45_mute_stream,
};

static struct snd_soc_dai_driver cs35l45_dai[] = {
	{
		.name = "cs35l45",
		.playback = {
			.stream_name = "Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = CS35L45_RATES,
			.formats = CS35L45_FORMATS,
		},
		.capture = {
			.stream_name = "Capture",
			.channels_min = 1,
			.channels_max = 5,
			.rates = CS35L45_RATES,
			.formats = CS35L45_FORMATS,
		},
		.symmetric_rate = true,
		.symmetric_sample_bits = true,
		.ops = &cs35l45_asp_dai_ops,
	},
};

static const struct snd_soc_component_driver cs35l45_component = {
	.dapm_widgets = cs35l45_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(cs35l45_dapm_widgets),

	.dapm_routes = cs35l45_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(cs35l45_dapm_routes),

	.controls = cs35l45_controls,
	.num_controls = ARRAY_SIZE(cs35l45_controls),

	.name = "cs35l45",

	.endianness = 1,
};

static int __maybe_unused cs35l45_runtime_suspend(struct device *dev)
{
	struct cs35l45_private *cs35l45 = dev_get_drvdata(dev);

	regcache_cache_only(cs35l45->regmap, true);

	dev_dbg(cs35l45->dev, "Runtime suspended\n");

	return 0;
}

static int __maybe_unused cs35l45_runtime_resume(struct device *dev)
{
	struct cs35l45_private *cs35l45 = dev_get_drvdata(dev);
	int ret;

	dev_dbg(cs35l45->dev, "Runtime resume\n");

	regcache_cache_only(cs35l45->regmap, false);
	ret = regcache_sync(cs35l45->regmap);
	if (ret != 0)
		dev_warn(cs35l45->dev, "regcache_sync failed: %d\n", ret);

	/* Clear global error status */
	regmap_clear_bits(cs35l45->regmap, CS35L45_ERROR_RELEASE, CS35L45_GLOBAL_ERR_RLS_MASK);
	regmap_set_bits(cs35l45->regmap, CS35L45_ERROR_RELEASE, CS35L45_GLOBAL_ERR_RLS_MASK);
	regmap_clear_bits(cs35l45->regmap, CS35L45_ERROR_RELEASE, CS35L45_GLOBAL_ERR_RLS_MASK);
	return ret;
}

static int cs35l45_apply_property_config(struct cs35l45_private *cs35l45)
{
	unsigned int val;

	if (device_property_read_u32(cs35l45->dev,
				     "cirrus,asp-sdout-hiz-ctrl", &val) == 0) {
		regmap_update_bits(cs35l45->regmap, CS35L45_ASP_CONTROL3,
				   CS35L45_ASP_DOUT_HIZ_CTRL_MASK,
				   val << CS35L45_ASP_DOUT_HIZ_CTRL_SHIFT);
	}

	return 0;
}

static int cs35l45_initialize(struct cs35l45_private *cs35l45)
{
	struct device *dev = cs35l45->dev;
	unsigned int dev_id[5];
	unsigned int sts;
	int ret;

	ret = regmap_read_poll_timeout(cs35l45->regmap, CS35L45_IRQ1_EINT_4, sts,
				       (sts & CS35L45_OTP_BOOT_DONE_STS_MASK),
				       1000, 5000);
	if (ret < 0) {
		dev_err(cs35l45->dev, "Timeout waiting for OTP boot\n");
		return ret;
	}

	ret = regmap_bulk_read(cs35l45->regmap, CS35L45_DEVID, dev_id, ARRAY_SIZE(dev_id));
	if (ret) {
		dev_err(cs35l45->dev, "Get Device ID failed: %d\n", ret);
		return ret;
	}

	switch (dev_id[0]) {
	case 0x35A450:
		break;
	default:
		dev_err(cs35l45->dev, "Bad DEVID 0x%x\n", dev_id[0]);
		return -ENODEV;
	}

	dev_info(cs35l45->dev, "Cirrus Logic CS35L45: REVID %02X OTPID %02X\n",
		 dev_id[1], dev_id[4]);

	regmap_write(cs35l45->regmap, CS35L45_IRQ1_EINT_4,
		     CS35L45_OTP_BOOT_DONE_STS_MASK | CS35L45_OTP_BUSY_MASK);

	ret = cs35l45_apply_patch(cs35l45);
	if (ret < 0) {
		dev_err(dev, "Failed to apply init patch %d\n", ret);
		return ret;
	}

	ret = cs35l45_apply_property_config(cs35l45);
	if (ret < 0)
		return ret;

	pm_runtime_set_autosuspend_delay(cs35l45->dev, 3000);
	pm_runtime_use_autosuspend(cs35l45->dev);
	pm_runtime_set_active(cs35l45->dev);
	pm_runtime_enable(cs35l45->dev);

	return 0;
}

int cs35l45_probe(struct cs35l45_private *cs35l45)
{
	struct device *dev = cs35l45->dev;
	int ret;

	cs35l45->vdd_batt = devm_regulator_get(dev, "vdd-batt");
	if (IS_ERR(cs35l45->vdd_batt))
		return dev_err_probe(dev, PTR_ERR(cs35l45->vdd_batt),
				     "Failed to request vdd-batt\n");

	cs35l45->vdd_a = devm_regulator_get(dev, "vdd-a");
	if (IS_ERR(cs35l45->vdd_a))
		return dev_err_probe(dev, PTR_ERR(cs35l45->vdd_a),
				     "Failed to request vdd-a\n");

	/* VDD_BATT must always be enabled before other supplies */
	ret = regulator_enable(cs35l45->vdd_batt);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to enable vdd-batt\n");

	ret = regulator_enable(cs35l45->vdd_a);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to enable vdd-a\n");

	/* If reset is shared only one instance can claim it */
	cs35l45->reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(cs35l45->reset_gpio)) {
		ret = PTR_ERR(cs35l45->reset_gpio);
		cs35l45->reset_gpio = NULL;
		if (ret == -EBUSY) {
			dev_dbg(dev, "Reset line busy, assuming shared reset\n");
		} else {
			dev_err_probe(dev, ret, "Failed to get reset GPIO\n");
			goto err;
		}
	}

	if (cs35l45->reset_gpio) {
		usleep_range(CS35L45_RESET_HOLD_US, CS35L45_RESET_HOLD_US + 100);
		gpiod_set_value_cansleep(cs35l45->reset_gpio, 1);
	}

	usleep_range(CS35L45_RESET_US, CS35L45_RESET_US + 100);

	ret = cs35l45_initialize(cs35l45);
	if (ret < 0)
		goto err_reset;

	ret = devm_snd_soc_register_component(dev, &cs35l45_component,
					      cs35l45_dai,
					      ARRAY_SIZE(cs35l45_dai));
	if (ret < 0)
		goto err_reset;

	return 0;

err_reset:
	gpiod_set_value_cansleep(cs35l45->reset_gpio, 0);
err:
	regulator_disable(cs35l45->vdd_a);
	regulator_disable(cs35l45->vdd_batt);

	return ret;
}
EXPORT_SYMBOL_NS_GPL(cs35l45_probe, SND_SOC_CS35L45);

void cs35l45_remove(struct cs35l45_private *cs35l45)
{
	pm_runtime_disable(cs35l45->dev);

	gpiod_set_value_cansleep(cs35l45->reset_gpio, 0);
	regulator_disable(cs35l45->vdd_a);
	/* VDD_BATT must be the last to power-off */
	regulator_disable(cs35l45->vdd_batt);
}
EXPORT_SYMBOL_NS_GPL(cs35l45_remove, SND_SOC_CS35L45);

const struct dev_pm_ops cs35l45_pm_ops = {
	SET_RUNTIME_PM_OPS(cs35l45_runtime_suspend, cs35l45_runtime_resume, NULL)
};
EXPORT_SYMBOL_NS_GPL(cs35l45_pm_ops, SND_SOC_CS35L45);

MODULE_DESCRIPTION("ASoC CS35L45 driver");
MODULE_AUTHOR("James Schulman, Cirrus Logic Inc, <james.schulman@cirrus.com>");
MODULE_AUTHOR("Richard Fitzgerald <rf@opensource.cirrus.com>");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_IMPORT_NS(SND_SOC_CS35L45_TABLES);
