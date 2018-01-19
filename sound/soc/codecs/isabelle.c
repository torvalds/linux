/*
 * isabelle.c - Low power high fidelity audio codec driver
 *
 * Copyright (c) 2012 Texas Instruments, Inc
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 *
 * Initially based on sound/soc/codecs/twl6040.c
 *
 */
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/regmap.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/tlv.h>
#include <sound/jack.h>
#include <sound/initval.h>
#include <asm/div64.h>
#include "isabelle.h"


/* Register default values for ISABELLE driver. */
static const struct reg_default isabelle_reg_defs[] = {
	{ 0, 0x00 },
	{ 1, 0x00 },
	{ 2, 0x00 },
	{ 3, 0x00 },
	{ 4, 0x00 },
	{ 5, 0x00 },
	{ 6, 0x00 },
	{ 7, 0x00 },
	{ 8, 0x00 },
	{ 9, 0x00 },
	{ 10, 0x00 },
	{ 11, 0x00 },
	{ 12, 0x00 },
	{ 13, 0x00 },
	{ 14, 0x00 },
	{ 15, 0x00 },
	{ 16, 0x00 },
	{ 17, 0x00 },
	{ 18, 0x00 },
	{ 19, 0x00 },
	{ 20, 0x00 },
	{ 21, 0x02 },
	{ 22, 0x02 },
	{ 23, 0x02 },
	{ 24, 0x02 },
	{ 25, 0x0F },
	{ 26, 0x8F },
	{ 27, 0x0F },
	{ 28, 0x8F },
	{ 29, 0x00 },
	{ 30, 0x00 },
	{ 31, 0x00 },
	{ 32, 0x00 },
	{ 33, 0x00 },
	{ 34, 0x00 },
	{ 35, 0x00 },
	{ 36, 0x00 },
	{ 37, 0x00 },
	{ 38, 0x00 },
	{ 39, 0x00 },
	{ 40, 0x00 },
	{ 41, 0x00 },
	{ 42, 0x00 },
	{ 43, 0x00 },
	{ 44, 0x00 },
	{ 45, 0x00 },
	{ 46, 0x00 },
	{ 47, 0x00 },
	{ 48, 0x00 },
	{ 49, 0x00 },
	{ 50, 0x00 },
	{ 51, 0x00 },
	{ 52, 0x00 },
	{ 53, 0x00 },
	{ 54, 0x00 },
	{ 55, 0x00 },
	{ 56, 0x00 },
	{ 57, 0x00 },
	{ 58, 0x00 },
	{ 59, 0x00 },
	{ 60, 0x00 },
	{ 61, 0x00 },
	{ 62, 0x00 },
	{ 63, 0x00 },
	{ 64, 0x00 },
	{ 65, 0x00 },
	{ 66, 0x00 },
	{ 67, 0x00 },
	{ 68, 0x00 },
	{ 69, 0x90 },
	{ 70, 0x90 },
	{ 71, 0x90 },
	{ 72, 0x00 },
	{ 73, 0x00 },
	{ 74, 0x00 },
	{ 75, 0x00 },
	{ 76, 0x00 },
	{ 77, 0x00 },
	{ 78, 0x00 },
	{ 79, 0x00 },
	{ 80, 0x00 },
	{ 81, 0x00 },
	{ 82, 0x00 },
	{ 83, 0x00 },
	{ 84, 0x00 },
	{ 85, 0x07 },
	{ 86, 0x00 },
	{ 87, 0x00 },
	{ 88, 0x00 },
	{ 89, 0x07 },
	{ 90, 0x80 },
	{ 91, 0x07 },
	{ 92, 0x07 },
	{ 93, 0x00 },
	{ 94, 0x00 },
	{ 95, 0x00 },
	{ 96, 0x00 },
	{ 97, 0x00 },
	{ 98, 0x00 },
	{ 99, 0x00 },
};

static const char *isabelle_rx1_texts[] = {"VRX1", "ARX1"};
static const char *isabelle_rx2_texts[] = {"VRX2", "ARX2"};

static const struct soc_enum isabelle_rx1_enum[] = {
	SOC_ENUM_SINGLE(ISABELLE_VOICE_HPF_CFG_REG, 3,
			ARRAY_SIZE(isabelle_rx1_texts), isabelle_rx1_texts),
	SOC_ENUM_SINGLE(ISABELLE_AUDIO_HPF_CFG_REG, 5,
			ARRAY_SIZE(isabelle_rx1_texts), isabelle_rx1_texts),
};

static const struct soc_enum isabelle_rx2_enum[] = {
	SOC_ENUM_SINGLE(ISABELLE_VOICE_HPF_CFG_REG, 2,
			ARRAY_SIZE(isabelle_rx2_texts), isabelle_rx2_texts),
	SOC_ENUM_SINGLE(ISABELLE_AUDIO_HPF_CFG_REG, 4,
			ARRAY_SIZE(isabelle_rx2_texts), isabelle_rx2_texts),
};

/* Headset DAC playback switches */
static const struct snd_kcontrol_new rx1_mux_controls =
	SOC_DAPM_ENUM("Route", isabelle_rx1_enum);

static const struct snd_kcontrol_new rx2_mux_controls =
	SOC_DAPM_ENUM("Route", isabelle_rx2_enum);

/* TX input selection */
static const char *isabelle_atx_texts[] = {"AMIC1", "DMIC"};
static const char *isabelle_vtx_texts[] = {"AMIC2", "DMIC"};

static const struct soc_enum isabelle_atx_enum[] = {
	SOC_ENUM_SINGLE(ISABELLE_AMIC_CFG_REG, 7,
			ARRAY_SIZE(isabelle_atx_texts), isabelle_atx_texts),
	SOC_ENUM_SINGLE(ISABELLE_DMIC_CFG_REG, 0,
			ARRAY_SIZE(isabelle_atx_texts), isabelle_atx_texts),
};

static const struct soc_enum isabelle_vtx_enum[] = {
	SOC_ENUM_SINGLE(ISABELLE_AMIC_CFG_REG, 6,
			ARRAY_SIZE(isabelle_vtx_texts), isabelle_vtx_texts),
	SOC_ENUM_SINGLE(ISABELLE_DMIC_CFG_REG, 0,
			ARRAY_SIZE(isabelle_vtx_texts), isabelle_vtx_texts),
};

static const struct snd_kcontrol_new atx_mux_controls =
	SOC_DAPM_ENUM("Route", isabelle_atx_enum);

static const struct snd_kcontrol_new vtx_mux_controls =
	SOC_DAPM_ENUM("Route", isabelle_vtx_enum);

/* Left analog microphone selection */
static const char *isabelle_amic1_texts[] = {
	"Main Mic", "Headset Mic", "Aux/FM Left"};

/* Left analog microphone selection */
static const char *isabelle_amic2_texts[] = {"Sub Mic", "Aux/FM Right"};

static SOC_ENUM_SINGLE_DECL(isabelle_amic1_enum,
			    ISABELLE_AMIC_CFG_REG, 5,
			    isabelle_amic1_texts);

static SOC_ENUM_SINGLE_DECL(isabelle_amic2_enum,
			    ISABELLE_AMIC_CFG_REG, 4,
			    isabelle_amic2_texts);

static const struct snd_kcontrol_new amic1_control =
	SOC_DAPM_ENUM("Route", isabelle_amic1_enum);

static const struct snd_kcontrol_new amic2_control =
	SOC_DAPM_ENUM("Route", isabelle_amic2_enum);

static const char *isabelle_st_audio_texts[] = {"ATX1", "ATX2"};

static const char *isabelle_st_voice_texts[] = {"VTX1", "VTX2"};

static const struct soc_enum isabelle_st_audio_enum[] = {
	SOC_ENUM_SINGLE(ISABELLE_ATX_STPGA1_CFG_REG, 7,
			ARRAY_SIZE(isabelle_st_audio_texts),
			isabelle_st_audio_texts),
	SOC_ENUM_SINGLE(ISABELLE_ATX_STPGA2_CFG_REG, 7,
			ARRAY_SIZE(isabelle_st_audio_texts),
			isabelle_st_audio_texts),
};

static const struct soc_enum isabelle_st_voice_enum[] = {
	SOC_ENUM_SINGLE(ISABELLE_VTX_STPGA1_CFG_REG, 7,
			ARRAY_SIZE(isabelle_st_voice_texts),
			isabelle_st_voice_texts),
	SOC_ENUM_SINGLE(ISABELLE_VTX2_STPGA2_CFG_REG, 7,
			ARRAY_SIZE(isabelle_st_voice_texts),
			isabelle_st_voice_texts),
};

static const struct snd_kcontrol_new st_audio_control =
	SOC_DAPM_ENUM("Route", isabelle_st_audio_enum);

static const struct snd_kcontrol_new st_voice_control =
	SOC_DAPM_ENUM("Route", isabelle_st_voice_enum);

/* Mixer controls */
static const struct snd_kcontrol_new isabelle_hs_left_mixer_controls[] = {
SOC_DAPM_SINGLE("DAC1L Playback Switch", ISABELLE_HSDRV_CFG1_REG, 7, 1, 0),
SOC_DAPM_SINGLE("APGA1 Playback Switch", ISABELLE_HSDRV_CFG1_REG, 6, 1, 0),
};

static const struct snd_kcontrol_new isabelle_hs_right_mixer_controls[] = {
SOC_DAPM_SINGLE("DAC1R Playback Switch", ISABELLE_HSDRV_CFG1_REG, 5, 1, 0),
SOC_DAPM_SINGLE("APGA2 Playback Switch", ISABELLE_HSDRV_CFG1_REG, 4, 1, 0),
};

static const struct snd_kcontrol_new isabelle_hf_left_mixer_controls[] = {
SOC_DAPM_SINGLE("DAC2L Playback Switch", ISABELLE_HFLPGA_CFG_REG, 7, 1, 0),
SOC_DAPM_SINGLE("APGA1 Playback Switch", ISABELLE_HFLPGA_CFG_REG, 6, 1, 0),
};

static const struct snd_kcontrol_new isabelle_hf_right_mixer_controls[] = {
SOC_DAPM_SINGLE("DAC2R Playback Switch", ISABELLE_HFRPGA_CFG_REG, 7, 1, 0),
SOC_DAPM_SINGLE("APGA2 Playback Switch", ISABELLE_HFRPGA_CFG_REG, 6, 1, 0),
};

static const struct snd_kcontrol_new isabelle_ep_mixer_controls[] = {
SOC_DAPM_SINGLE("DAC2L Playback Switch", ISABELLE_EARDRV_CFG1_REG, 7, 1, 0),
SOC_DAPM_SINGLE("APGA1 Playback Switch", ISABELLE_EARDRV_CFG1_REG, 6, 1, 0),
};

static const struct snd_kcontrol_new isabelle_aux_left_mixer_controls[] = {
SOC_DAPM_SINGLE("DAC3L Playback Switch", ISABELLE_LINEAMP_CFG_REG, 7, 1, 0),
SOC_DAPM_SINGLE("APGA1 Playback Switch", ISABELLE_LINEAMP_CFG_REG, 6, 1, 0),
};

static const struct snd_kcontrol_new isabelle_aux_right_mixer_controls[] = {
SOC_DAPM_SINGLE("DAC3R Playback Switch", ISABELLE_LINEAMP_CFG_REG, 5, 1, 0),
SOC_DAPM_SINGLE("APGA2 Playback Switch", ISABELLE_LINEAMP_CFG_REG, 4, 1, 0),
};

static const struct snd_kcontrol_new isabelle_dpga1_left_mixer_controls[] = {
SOC_DAPM_SINGLE("RX1 Playback Switch", ISABELLE_DPGA1LR_IN_SEL_REG, 7, 1, 0),
SOC_DAPM_SINGLE("RX3 Playback Switch", ISABELLE_DPGA1LR_IN_SEL_REG, 6, 1, 0),
SOC_DAPM_SINGLE("RX5 Playback Switch", ISABELLE_DPGA1LR_IN_SEL_REG, 5, 1, 0),
};

static const struct snd_kcontrol_new isabelle_dpga1_right_mixer_controls[] = {
SOC_DAPM_SINGLE("RX2 Playback Switch", ISABELLE_DPGA1LR_IN_SEL_REG, 3, 1, 0),
SOC_DAPM_SINGLE("RX4 Playback Switch", ISABELLE_DPGA1LR_IN_SEL_REG, 2, 1, 0),
SOC_DAPM_SINGLE("RX6 Playback Switch", ISABELLE_DPGA1LR_IN_SEL_REG, 1, 1, 0),
};

static const struct snd_kcontrol_new isabelle_dpga2_left_mixer_controls[] = {
SOC_DAPM_SINGLE("RX1 Playback Switch", ISABELLE_DPGA2L_IN_SEL_REG, 7, 1, 0),
SOC_DAPM_SINGLE("RX2 Playback Switch", ISABELLE_DPGA2L_IN_SEL_REG, 6, 1, 0),
SOC_DAPM_SINGLE("RX3 Playback Switch", ISABELLE_DPGA2L_IN_SEL_REG, 5, 1, 0),
SOC_DAPM_SINGLE("RX4 Playback Switch", ISABELLE_DPGA2L_IN_SEL_REG, 4, 1, 0),
SOC_DAPM_SINGLE("RX5 Playback Switch", ISABELLE_DPGA2L_IN_SEL_REG, 3, 1, 0),
SOC_DAPM_SINGLE("RX6 Playback Switch", ISABELLE_DPGA2L_IN_SEL_REG, 2, 1, 0),
};

static const struct snd_kcontrol_new isabelle_dpga2_right_mixer_controls[] = {
SOC_DAPM_SINGLE("USNC Playback Switch", ISABELLE_DPGA2R_IN_SEL_REG, 7, 1, 0),
SOC_DAPM_SINGLE("RX2 Playback Switch", ISABELLE_DPGA2R_IN_SEL_REG, 3, 1, 0),
SOC_DAPM_SINGLE("RX4 Playback Switch", ISABELLE_DPGA2R_IN_SEL_REG, 2, 1, 0),
SOC_DAPM_SINGLE("RX6 Playback Switch", ISABELLE_DPGA2R_IN_SEL_REG, 1, 1, 0),
};

static const struct snd_kcontrol_new isabelle_dpga3_left_mixer_controls[] = {
SOC_DAPM_SINGLE("RX1 Playback Switch", ISABELLE_DPGA3LR_IN_SEL_REG, 7, 1, 0),
SOC_DAPM_SINGLE("RX3 Playback Switch", ISABELLE_DPGA3LR_IN_SEL_REG, 6, 1, 0),
SOC_DAPM_SINGLE("RX5 Playback Switch", ISABELLE_DPGA3LR_IN_SEL_REG, 5, 1, 0),
};

static const struct snd_kcontrol_new isabelle_dpga3_right_mixer_controls[] = {
SOC_DAPM_SINGLE("RX2 Playback Switch", ISABELLE_DPGA3LR_IN_SEL_REG, 3, 1, 0),
SOC_DAPM_SINGLE("RX4 Playback Switch", ISABELLE_DPGA3LR_IN_SEL_REG, 2, 1, 0),
SOC_DAPM_SINGLE("RX6 Playback Switch", ISABELLE_DPGA3LR_IN_SEL_REG, 1, 1, 0),
};

static const struct snd_kcontrol_new isabelle_rx1_mixer_controls[] = {
SOC_DAPM_SINGLE("ST1 Playback Switch", ISABELLE_RX_INPUT_CFG_REG, 7, 1, 0),
SOC_DAPM_SINGLE("DL1 Playback Switch", ISABELLE_RX_INPUT_CFG_REG, 6, 1, 0),
};

static const struct snd_kcontrol_new isabelle_rx2_mixer_controls[] = {
SOC_DAPM_SINGLE("ST2 Playback Switch", ISABELLE_RX_INPUT_CFG_REG, 5, 1, 0),
SOC_DAPM_SINGLE("DL2 Playback Switch", ISABELLE_RX_INPUT_CFG_REG, 4, 1, 0),
};

static const struct snd_kcontrol_new isabelle_rx3_mixer_controls[] = {
SOC_DAPM_SINGLE("ST1 Playback Switch", ISABELLE_RX_INPUT_CFG_REG, 3, 1, 0),
SOC_DAPM_SINGLE("DL3 Playback Switch", ISABELLE_RX_INPUT_CFG_REG, 2, 1, 0),
};

static const struct snd_kcontrol_new isabelle_rx4_mixer_controls[] = {
SOC_DAPM_SINGLE("ST2 Playback Switch", ISABELLE_RX_INPUT_CFG_REG, 1, 1, 0),
SOC_DAPM_SINGLE("DL4 Playback Switch", ISABELLE_RX_INPUT_CFG_REG, 0, 1, 0),
};

static const struct snd_kcontrol_new isabelle_rx5_mixer_controls[] = {
SOC_DAPM_SINGLE("ST1 Playback Switch", ISABELLE_RX_INPUT_CFG2_REG, 7, 1, 0),
SOC_DAPM_SINGLE("DL5 Playback Switch", ISABELLE_RX_INPUT_CFG2_REG, 6, 1, 0),
};

static const struct snd_kcontrol_new isabelle_rx6_mixer_controls[] = {
SOC_DAPM_SINGLE("ST2 Playback Switch", ISABELLE_RX_INPUT_CFG2_REG, 5, 1, 0),
SOC_DAPM_SINGLE("DL6 Playback Switch", ISABELLE_RX_INPUT_CFG2_REG, 4, 1, 0),
};

static const struct snd_kcontrol_new ep_path_enable_control =
	SOC_DAPM_SINGLE("Switch", ISABELLE_EARDRV_CFG2_REG, 0, 1, 0);

/* TLV Declarations */
static const DECLARE_TLV_DB_SCALE(mic_amp_tlv, 0, 100, 0);
static const DECLARE_TLV_DB_SCALE(afm_amp_tlv, -3300, 300, 0);
static const DECLARE_TLV_DB_SCALE(dac_tlv, -1200, 200, 0);
static const DECLARE_TLV_DB_SCALE(hf_tlv, -5000, 200, 0);

/* from -63 to 0 dB in 1 dB steps */
static const DECLARE_TLV_DB_SCALE(dpga_tlv, -6300, 100, 1);

/* from -63 to 9 dB in 1 dB steps */
static const DECLARE_TLV_DB_SCALE(rx_tlv, -6300, 100, 1);

static const DECLARE_TLV_DB_SCALE(st_tlv, -2700, 300, 1);
static const DECLARE_TLV_DB_SCALE(tx_tlv, -600, 100, 0);

static const struct snd_kcontrol_new isabelle_snd_controls[] = {
	SOC_DOUBLE_TLV("Headset Playback Volume", ISABELLE_HSDRV_GAIN_REG,
			4, 0, 0xF, 0, dac_tlv),
	SOC_DOUBLE_R_TLV("Handsfree Playback Volume",
			ISABELLE_HFLPGA_CFG_REG, ISABELLE_HFRPGA_CFG_REG,
			0, 0x1F, 0, hf_tlv),
	SOC_DOUBLE_TLV("Aux Playback Volume", ISABELLE_LINEAMP_GAIN_REG,
			4, 0, 0xF, 0, dac_tlv),
	SOC_SINGLE_TLV("Earpiece Playback Volume", ISABELLE_EARDRV_CFG1_REG,
			0, 0xF, 0, dac_tlv),

	SOC_DOUBLE_TLV("Aux FM Volume", ISABELLE_APGA_GAIN_REG, 4, 0, 0xF, 0,
			afm_amp_tlv),
	SOC_SINGLE_TLV("Mic1 Capture Volume", ISABELLE_MIC1_GAIN_REG, 3, 0x1F,
			0, mic_amp_tlv),
	SOC_SINGLE_TLV("Mic2 Capture Volume", ISABELLE_MIC2_GAIN_REG, 3, 0x1F,
			0, mic_amp_tlv),

	SOC_DOUBLE_R_TLV("DPGA1 Volume", ISABELLE_DPGA1L_GAIN_REG,
			ISABELLE_DPGA1R_GAIN_REG, 0, 0x3F, 0, dpga_tlv),
	SOC_DOUBLE_R_TLV("DPGA2 Volume", ISABELLE_DPGA2L_GAIN_REG,
			ISABELLE_DPGA2R_GAIN_REG, 0, 0x3F, 0, dpga_tlv),
	SOC_DOUBLE_R_TLV("DPGA3 Volume", ISABELLE_DPGA3L_GAIN_REG,
			ISABELLE_DPGA3R_GAIN_REG, 0, 0x3F, 0, dpga_tlv),

	SOC_SINGLE_TLV("Sidetone Audio TX1 Volume",
			ISABELLE_ATX_STPGA1_CFG_REG, 0, 0xF, 0, st_tlv),
	SOC_SINGLE_TLV("Sidetone Audio TX2 Volume",
			ISABELLE_ATX_STPGA2_CFG_REG, 0, 0xF, 0, st_tlv),
	SOC_SINGLE_TLV("Sidetone Voice TX1 Volume",
			ISABELLE_VTX_STPGA1_CFG_REG, 0, 0xF, 0, st_tlv),
	SOC_SINGLE_TLV("Sidetone Voice TX2 Volume",
			ISABELLE_VTX2_STPGA2_CFG_REG, 0, 0xF, 0, st_tlv),

	SOC_SINGLE_TLV("Audio TX1 Volume", ISABELLE_ATX1_DPGA_REG, 4, 0xF, 0,
			tx_tlv),
	SOC_SINGLE_TLV("Audio TX2 Volume", ISABELLE_ATX2_DPGA_REG, 4, 0xF, 0,
			tx_tlv),
	SOC_SINGLE_TLV("Voice TX1 Volume", ISABELLE_VTX1_DPGA_REG, 4, 0xF, 0,
			tx_tlv),
	SOC_SINGLE_TLV("Voice TX2 Volume", ISABELLE_VTX2_DPGA_REG, 4, 0xF, 0,
			tx_tlv),

	SOC_SINGLE_TLV("RX1 DPGA Volume", ISABELLE_RX1_DPGA_REG, 0, 0x3F, 0,
			rx_tlv),
	SOC_SINGLE_TLV("RX2 DPGA Volume", ISABELLE_RX2_DPGA_REG, 0, 0x3F, 0,
			rx_tlv),
	SOC_SINGLE_TLV("RX3 DPGA Volume", ISABELLE_RX3_DPGA_REG, 0, 0x3F, 0,
			rx_tlv),
	SOC_SINGLE_TLV("RX4 DPGA Volume", ISABELLE_RX4_DPGA_REG, 0, 0x3F, 0,
			rx_tlv),
	SOC_SINGLE_TLV("RX5 DPGA Volume", ISABELLE_RX5_DPGA_REG, 0, 0x3F, 0,
			rx_tlv),
	SOC_SINGLE_TLV("RX6 DPGA Volume", ISABELLE_RX6_DPGA_REG, 0, 0x3F, 0,
			rx_tlv),

	SOC_SINGLE("Headset Noise Gate", ISABELLE_HS_NG_CFG1_REG, 7, 1, 0),
	SOC_SINGLE("Handsfree Noise Gate", ISABELLE_HF_NG_CFG1_REG, 7, 1, 0),

	SOC_SINGLE("ATX1 Filter Bypass Switch", ISABELLE_AUDIO_HPF_CFG_REG,
		7, 1, 0),
	SOC_SINGLE("ATX2 Filter Bypass Switch", ISABELLE_AUDIO_HPF_CFG_REG,
		6, 1, 0),
	SOC_SINGLE("ARX1 Filter Bypass Switch", ISABELLE_AUDIO_HPF_CFG_REG,
		5, 1, 0),
	SOC_SINGLE("ARX2 Filter Bypass Switch", ISABELLE_AUDIO_HPF_CFG_REG,
		4, 1, 0),
	SOC_SINGLE("ARX3 Filter Bypass Switch", ISABELLE_AUDIO_HPF_CFG_REG,
		3, 1, 0),
	SOC_SINGLE("ARX4 Filter Bypass Switch", ISABELLE_AUDIO_HPF_CFG_REG,
		2, 1, 0),
	SOC_SINGLE("ARX5 Filter Bypass Switch", ISABELLE_AUDIO_HPF_CFG_REG,
		1, 1, 0),
	SOC_SINGLE("ARX6 Filter Bypass Switch", ISABELLE_AUDIO_HPF_CFG_REG,
		0, 1, 0),
	SOC_SINGLE("VRX1 Filter Bypass Switch", ISABELLE_AUDIO_HPF_CFG_REG,
		3, 1, 0),
	SOC_SINGLE("VRX2 Filter Bypass Switch", ISABELLE_AUDIO_HPF_CFG_REG,
		2, 1, 0),

	SOC_SINGLE("ATX1 Filter Enable Switch", ISABELLE_ALU_TX_EN_REG,
		7, 1, 0),
	SOC_SINGLE("ATX2 Filter Enable Switch", ISABELLE_ALU_TX_EN_REG,
		6, 1, 0),
	SOC_SINGLE("VTX1 Filter Enable Switch", ISABELLE_ALU_TX_EN_REG,
		5, 1, 0),
	SOC_SINGLE("VTX2 Filter Enable Switch", ISABELLE_ALU_TX_EN_REG,
		4, 1, 0),
	SOC_SINGLE("RX1 Filter Enable Switch", ISABELLE_ALU_RX_EN_REG,
		5, 1, 0),
	SOC_SINGLE("RX2 Filter Enable Switch", ISABELLE_ALU_RX_EN_REG,
		4, 1, 0),
	SOC_SINGLE("RX3 Filter Enable Switch", ISABELLE_ALU_RX_EN_REG,
		3, 1, 0),
	SOC_SINGLE("RX4 Filter Enable Switch", ISABELLE_ALU_RX_EN_REG,
		2, 1, 0),
	SOC_SINGLE("RX5 Filter Enable Switch", ISABELLE_ALU_RX_EN_REG,
		1, 1, 0),
	SOC_SINGLE("RX6 Filter Enable Switch", ISABELLE_ALU_RX_EN_REG,
		0, 1, 0),

	SOC_SINGLE("ULATX12 Capture Switch", ISABELLE_ULATX12_INTF_CFG_REG,
		7, 1, 0),

	SOC_SINGLE("DL12 Playback Switch", ISABELLE_DL12_INTF_CFG_REG,
		7, 1, 0),
	SOC_SINGLE("DL34 Playback Switch", ISABELLE_DL34_INTF_CFG_REG,
		7, 1, 0),
	SOC_SINGLE("DL56 Playback Switch", ISABELLE_DL56_INTF_CFG_REG,
		7, 1, 0),

	/* DMIC Switch */
	SOC_SINGLE("DMIC Switch", ISABELLE_DMIC_CFG_REG, 0, 1, 0),
};

static const struct snd_soc_dapm_widget isabelle_dapm_widgets[] = {
	/* Inputs */
	SND_SOC_DAPM_INPUT("MAINMIC"),
	SND_SOC_DAPM_INPUT("HSMIC"),
	SND_SOC_DAPM_INPUT("SUBMIC"),
	SND_SOC_DAPM_INPUT("LINEIN1"),
	SND_SOC_DAPM_INPUT("LINEIN2"),
	SND_SOC_DAPM_INPUT("DMICDAT"),

	/* Outputs */
	SND_SOC_DAPM_OUTPUT("HSOL"),
	SND_SOC_DAPM_OUTPUT("HSOR"),
	SND_SOC_DAPM_OUTPUT("HFL"),
	SND_SOC_DAPM_OUTPUT("HFR"),
	SND_SOC_DAPM_OUTPUT("EP"),
	SND_SOC_DAPM_OUTPUT("LINEOUT1"),
	SND_SOC_DAPM_OUTPUT("LINEOUT2"),

	SND_SOC_DAPM_PGA("DL1", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("DL2", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("DL3", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("DL4", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("DL5", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("DL6", SND_SOC_NOPM, 0, 0, NULL, 0),

	/* Analog input muxes for the capture amplifiers */
	SND_SOC_DAPM_MUX("Analog Left Capture Route",
			SND_SOC_NOPM, 0, 0, &amic1_control),
	SND_SOC_DAPM_MUX("Analog Right Capture Route",
			SND_SOC_NOPM, 0, 0, &amic2_control),

	SND_SOC_DAPM_MUX("Sidetone Audio Playback", SND_SOC_NOPM, 0, 0,
			&st_audio_control),
	SND_SOC_DAPM_MUX("Sidetone Voice Playback", SND_SOC_NOPM, 0, 0,
			&st_voice_control),

	/* AIF */
	SND_SOC_DAPM_AIF_IN("INTF1_SDI", NULL, 0, ISABELLE_INTF_EN_REG, 7, 0),
	SND_SOC_DAPM_AIF_IN("INTF2_SDI", NULL, 0, ISABELLE_INTF_EN_REG, 6, 0),

	SND_SOC_DAPM_AIF_OUT("INTF1_SDO", NULL, 0, ISABELLE_INTF_EN_REG, 5, 0),
	SND_SOC_DAPM_AIF_OUT("INTF2_SDO", NULL, 0, ISABELLE_INTF_EN_REG, 4, 0),

	SND_SOC_DAPM_OUT_DRV("ULATX1", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_OUT_DRV("ULATX2", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_OUT_DRV("ULVTX1", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_OUT_DRV("ULVTX2", SND_SOC_NOPM, 0, 0, NULL, 0),

	/* Analog Capture PGAs */
	SND_SOC_DAPM_PGA("MicAmp1", ISABELLE_AMIC_CFG_REG, 5, 0, NULL, 0),
	SND_SOC_DAPM_PGA("MicAmp2", ISABELLE_AMIC_CFG_REG, 4, 0, NULL, 0),

	/* Auxiliary FM PGAs */
	SND_SOC_DAPM_PGA("APGA1", ISABELLE_APGA_CFG_REG, 7, 0, NULL, 0),
	SND_SOC_DAPM_PGA("APGA2", ISABELLE_APGA_CFG_REG, 6, 0, NULL, 0),

	/* ADCs */
	SND_SOC_DAPM_ADC("ADC1", "Left Front Capture",
			ISABELLE_AMIC_CFG_REG, 7, 0),
	SND_SOC_DAPM_ADC("ADC2", "Right Front Capture",
			ISABELLE_AMIC_CFG_REG, 6, 0),

	/* Microphone Bias */
	SND_SOC_DAPM_SUPPLY("Headset Mic Bias", ISABELLE_ABIAS_CFG_REG,
			3, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("Main Mic Bias", ISABELLE_ABIAS_CFG_REG,
			2, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("Digital Mic1 Bias",
			ISABELLE_DBIAS_CFG_REG, 3, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("Digital Mic2 Bias",
			ISABELLE_DBIAS_CFG_REG, 2, 0, NULL, 0),

	/* Mixers */
	SND_SOC_DAPM_MIXER("Headset Left Mixer", SND_SOC_NOPM, 0, 0,
			isabelle_hs_left_mixer_controls,
			ARRAY_SIZE(isabelle_hs_left_mixer_controls)),
	SND_SOC_DAPM_MIXER("Headset Right Mixer", SND_SOC_NOPM, 0, 0,
			isabelle_hs_right_mixer_controls,
			ARRAY_SIZE(isabelle_hs_right_mixer_controls)),
	SND_SOC_DAPM_MIXER("Handsfree Left Mixer", SND_SOC_NOPM, 0, 0,
			isabelle_hf_left_mixer_controls,
			ARRAY_SIZE(isabelle_hf_left_mixer_controls)),
	SND_SOC_DAPM_MIXER("Handsfree Right Mixer", SND_SOC_NOPM, 0, 0,
			isabelle_hf_right_mixer_controls,
			ARRAY_SIZE(isabelle_hf_right_mixer_controls)),
	SND_SOC_DAPM_MIXER("LINEOUT1 Mixer", SND_SOC_NOPM, 0, 0,
			isabelle_aux_left_mixer_controls,
			ARRAY_SIZE(isabelle_aux_left_mixer_controls)),
	SND_SOC_DAPM_MIXER("LINEOUT2 Mixer", SND_SOC_NOPM, 0, 0,
			isabelle_aux_right_mixer_controls,
			ARRAY_SIZE(isabelle_aux_right_mixer_controls)),
	SND_SOC_DAPM_MIXER("Earphone Mixer", SND_SOC_NOPM, 0, 0,
			isabelle_ep_mixer_controls,
			ARRAY_SIZE(isabelle_ep_mixer_controls)),

	SND_SOC_DAPM_MIXER("DPGA1L Mixer", SND_SOC_NOPM, 0, 0,
			isabelle_dpga1_left_mixer_controls,
			ARRAY_SIZE(isabelle_dpga1_left_mixer_controls)),
	SND_SOC_DAPM_MIXER("DPGA1R Mixer", SND_SOC_NOPM, 0, 0,
			isabelle_dpga1_right_mixer_controls,
			ARRAY_SIZE(isabelle_dpga1_right_mixer_controls)),
	SND_SOC_DAPM_MIXER("DPGA2L Mixer", SND_SOC_NOPM, 0, 0,
			isabelle_dpga2_left_mixer_controls,
			ARRAY_SIZE(isabelle_dpga2_left_mixer_controls)),
	SND_SOC_DAPM_MIXER("DPGA2R Mixer", SND_SOC_NOPM, 0, 0,
			isabelle_dpga2_right_mixer_controls,
			ARRAY_SIZE(isabelle_dpga2_right_mixer_controls)),
	SND_SOC_DAPM_MIXER("DPGA3L Mixer", SND_SOC_NOPM, 0, 0,
			isabelle_dpga3_left_mixer_controls,
			ARRAY_SIZE(isabelle_dpga3_left_mixer_controls)),
	SND_SOC_DAPM_MIXER("DPGA3R Mixer", SND_SOC_NOPM, 0, 0,
			isabelle_dpga3_right_mixer_controls,
			ARRAY_SIZE(isabelle_dpga3_right_mixer_controls)),

	SND_SOC_DAPM_MIXER("RX1 Mixer", SND_SOC_NOPM, 0, 0,
			isabelle_rx1_mixer_controls,
			ARRAY_SIZE(isabelle_rx1_mixer_controls)),
	SND_SOC_DAPM_MIXER("RX2 Mixer", SND_SOC_NOPM, 0, 0,
			isabelle_rx2_mixer_controls,
			ARRAY_SIZE(isabelle_rx2_mixer_controls)),
	SND_SOC_DAPM_MIXER("RX3 Mixer", SND_SOC_NOPM, 0, 0,
			isabelle_rx3_mixer_controls,
			ARRAY_SIZE(isabelle_rx3_mixer_controls)),
	SND_SOC_DAPM_MIXER("RX4 Mixer", SND_SOC_NOPM, 0, 0,
			isabelle_rx4_mixer_controls,
			ARRAY_SIZE(isabelle_rx4_mixer_controls)),
	SND_SOC_DAPM_MIXER("RX5 Mixer", SND_SOC_NOPM, 0, 0,
			isabelle_rx5_mixer_controls,
			ARRAY_SIZE(isabelle_rx5_mixer_controls)),
	SND_SOC_DAPM_MIXER("RX6 Mixer", SND_SOC_NOPM, 0, 0,
			isabelle_rx6_mixer_controls,
			ARRAY_SIZE(isabelle_rx6_mixer_controls)),

	/* DACs */
	SND_SOC_DAPM_DAC("DAC1L", "Headset Playback", ISABELLE_DAC_CFG_REG,
			5, 0),
	SND_SOC_DAPM_DAC("DAC1R", "Headset Playback", ISABELLE_DAC_CFG_REG,
			4, 0),
	SND_SOC_DAPM_DAC("DAC2L", "Handsfree Playback", ISABELLE_DAC_CFG_REG,
			3, 0),
	SND_SOC_DAPM_DAC("DAC2R", "Handsfree Playback", ISABELLE_DAC_CFG_REG,
			2, 0),
	SND_SOC_DAPM_DAC("DAC3L", "Lineout Playback", ISABELLE_DAC_CFG_REG,
			1, 0),
	SND_SOC_DAPM_DAC("DAC3R", "Lineout Playback", ISABELLE_DAC_CFG_REG,
			0, 0),

	/* Analog Playback PGAs */
	SND_SOC_DAPM_PGA("Sidetone Audio PGA", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Sidetone Voice PGA", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("HF Left PGA", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("HF Right PGA", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("DPGA1L", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("DPGA1R", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("DPGA2L", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("DPGA2R", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("DPGA3L", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("DPGA3R", SND_SOC_NOPM, 0, 0, NULL, 0),

	/* Analog Playback Mux */
	SND_SOC_DAPM_MUX("RX1 Playback", ISABELLE_ALU_RX_EN_REG, 5, 0,
			&rx1_mux_controls),
	SND_SOC_DAPM_MUX("RX2 Playback", ISABELLE_ALU_RX_EN_REG, 4, 0,
			&rx2_mux_controls),

	/* TX Select */
	SND_SOC_DAPM_MUX("ATX Select", ISABELLE_TX_INPUT_CFG_REG,
			7, 0, &atx_mux_controls),
	SND_SOC_DAPM_MUX("VTX Select", ISABELLE_TX_INPUT_CFG_REG,
			6, 0, &vtx_mux_controls),

	SND_SOC_DAPM_SWITCH("Earphone Playback", SND_SOC_NOPM, 0, 0,
			&ep_path_enable_control),

	/* Output Drivers */
	SND_SOC_DAPM_OUT_DRV("HS Left Driver", ISABELLE_HSDRV_CFG2_REG,
			1, 0, NULL, 0),
	SND_SOC_DAPM_OUT_DRV("HS Right Driver", ISABELLE_HSDRV_CFG2_REG,
			0, 0, NULL, 0),
	SND_SOC_DAPM_OUT_DRV("LINEOUT1 Left Driver", ISABELLE_LINEAMP_CFG_REG,
			1, 0, NULL, 0),
	SND_SOC_DAPM_OUT_DRV("LINEOUT2 Right Driver", ISABELLE_LINEAMP_CFG_REG,
			0, 0, NULL, 0),
	SND_SOC_DAPM_OUT_DRV("Earphone Driver", ISABELLE_EARDRV_CFG2_REG,
			1, 0, NULL, 0),

	SND_SOC_DAPM_OUT_DRV("HF Left Driver", ISABELLE_HFDRV_CFG_REG,
			1, 0, NULL, 0),
	SND_SOC_DAPM_OUT_DRV("HF Right Driver", ISABELLE_HFDRV_CFG_REG,
			0, 0, NULL, 0),
};

static const struct snd_soc_dapm_route isabelle_intercon[] = {
	/* Interface mapping */
	{ "DL1", "DL12 Playback Switch", "INTF1_SDI" },
	{ "DL2", "DL12 Playback Switch", "INTF1_SDI" },
	{ "DL3", "DL34 Playback Switch", "INTF1_SDI" },
	{ "DL4", "DL34 Playback Switch", "INTF1_SDI" },
	{ "DL5", "DL56 Playback Switch", "INTF1_SDI" },
	{ "DL6", "DL56 Playback Switch", "INTF1_SDI" },

	{ "DL1", "DL12 Playback Switch", "INTF2_SDI" },
	{ "DL2", "DL12 Playback Switch", "INTF2_SDI" },
	{ "DL3", "DL34 Playback Switch", "INTF2_SDI" },
	{ "DL4", "DL34 Playback Switch", "INTF2_SDI" },
	{ "DL5", "DL56 Playback Switch", "INTF2_SDI" },
	{ "DL6", "DL56 Playback Switch", "INTF2_SDI" },

	/* Input side mapping */
	{ "Sidetone Audio PGA", NULL, "Sidetone Audio Playback" },
	{ "Sidetone Voice PGA", NULL, "Sidetone Voice Playback" },

	{ "RX1 Mixer", "ST1 Playback Switch", "Sidetone Audio PGA" },

	{ "RX1 Mixer", "ST1 Playback Switch", "Sidetone Voice PGA" },
	{ "RX1 Mixer", "DL1 Playback Switch", "DL1" },

	{ "RX2 Mixer", "ST2 Playback Switch", "Sidetone Audio PGA" },

	{ "RX2 Mixer", "ST2 Playback Switch", "Sidetone Voice PGA" },
	{ "RX2 Mixer", "DL2 Playback Switch", "DL2" },

	{ "RX3 Mixer", "ST1 Playback Switch", "Sidetone Voice PGA" },
	{ "RX3 Mixer", "DL3 Playback Switch", "DL3" },

	{ "RX4 Mixer", "ST2 Playback Switch", "Sidetone Voice PGA" },
	{ "RX4 Mixer", "DL4 Playback Switch", "DL4" },

	{ "RX5 Mixer", "ST1 Playback Switch", "Sidetone Voice PGA" },
	{ "RX5 Mixer", "DL5 Playback Switch", "DL5" },

	{ "RX6 Mixer", "ST2 Playback Switch", "Sidetone Voice PGA" },
	{ "RX6 Mixer", "DL6 Playback Switch", "DL6" },

	/* Capture path */
	{ "Analog Left Capture Route", "Headset Mic", "HSMIC" },
	{ "Analog Left Capture Route", "Main Mic", "MAINMIC" },
	{ "Analog Left Capture Route", "Aux/FM Left", "LINEIN1" },

	{ "Analog Right Capture Route", "Sub Mic", "SUBMIC" },
	{ "Analog Right Capture Route", "Aux/FM Right", "LINEIN2" },

	{ "MicAmp1", NULL, "Analog Left Capture Route" },
	{ "MicAmp2", NULL, "Analog Right Capture Route" },

	{ "ADC1", NULL, "MicAmp1" },
	{ "ADC2", NULL, "MicAmp2" },

	{ "ATX Select", "AMIC1", "ADC1" },
	{ "ATX Select", "DMIC", "DMICDAT" },
	{ "ATX Select", "AMIC2", "ADC2" },

	{ "VTX Select", "AMIC1", "ADC1" },
	{ "VTX Select", "DMIC", "DMICDAT" },
	{ "VTX Select", "AMIC2", "ADC2" },

	{ "ULATX1", "ATX1 Filter Enable Switch", "ATX Select" },
	{ "ULATX1", "ATX1 Filter Bypass Switch", "ATX Select" },
	{ "ULATX2", "ATX2 Filter Enable Switch", "ATX Select" },
	{ "ULATX2", "ATX2 Filter Bypass Switch", "ATX Select" },

	{ "ULVTX1", "VTX1 Filter Enable Switch", "VTX Select" },
	{ "ULVTX1", "VTX1 Filter Bypass Switch", "VTX Select" },
	{ "ULVTX2", "VTX2 Filter Enable Switch", "VTX Select" },
	{ "ULVTX2", "VTX2 Filter Bypass Switch", "VTX Select" },

	{ "INTF1_SDO", "ULATX12 Capture Switch", "ULATX1" },
	{ "INTF1_SDO", "ULATX12 Capture Switch", "ULATX2" },
	{ "INTF2_SDO", "ULATX12 Capture Switch", "ULATX1" },
	{ "INTF2_SDO", "ULATX12 Capture Switch", "ULATX2" },

	{ "INTF1_SDO", NULL, "ULVTX1" },
	{ "INTF1_SDO", NULL, "ULVTX2" },
	{ "INTF2_SDO", NULL, "ULVTX1" },
	{ "INTF2_SDO", NULL, "ULVTX2" },

	/* AFM Path */
	{ "APGA1", NULL, "LINEIN1" },
	{ "APGA2", NULL, "LINEIN2" },

	{ "RX1 Playback", "VRX1 Filter Bypass Switch", "RX1 Mixer" },
	{ "RX1 Playback", "ARX1 Filter Bypass Switch", "RX1 Mixer" },
	{ "RX1 Playback", "RX1 Filter Enable Switch", "RX1 Mixer" },

	{ "RX2 Playback", "VRX2 Filter Bypass Switch", "RX2 Mixer" },
	{ "RX2 Playback", "ARX2 Filter Bypass Switch", "RX2 Mixer" },
	{ "RX2 Playback", "RX2 Filter Enable Switch", "RX2 Mixer" },

	{ "RX3 Playback", "ARX3 Filter Bypass Switch", "RX3 Mixer" },
	{ "RX3 Playback", "RX3 Filter Enable Switch", "RX3 Mixer" },

	{ "RX4 Playback", "ARX4 Filter Bypass Switch", "RX4 Mixer" },
	{ "RX4 Playback", "RX4 Filter Enable Switch", "RX4 Mixer" },

	{ "RX5 Playback", "ARX5 Filter Bypass Switch", "RX5 Mixer" },
	{ "RX5 Playback", "RX5 Filter Enable Switch", "RX5 Mixer" },

	{ "RX6 Playback", "ARX6 Filter Bypass Switch", "RX6 Mixer" },
	{ "RX6 Playback", "RX6 Filter Enable Switch", "RX6 Mixer" },

	{ "DPGA1L Mixer", "RX1 Playback Switch", "RX1 Playback" },
	{ "DPGA1L Mixer", "RX3 Playback Switch", "RX3 Playback" },
	{ "DPGA1L Mixer", "RX5 Playback Switch", "RX5 Playback" },

	{ "DPGA1R Mixer", "RX2 Playback Switch", "RX2 Playback" },
	{ "DPGA1R Mixer", "RX4 Playback Switch", "RX4 Playback" },
	{ "DPGA1R Mixer", "RX6 Playback Switch", "RX6 Playback" },

	{ "DPGA1L", NULL, "DPGA1L Mixer" },
	{ "DPGA1R", NULL, "DPGA1R Mixer" },

	{ "DAC1L", NULL, "DPGA1L" },
	{ "DAC1R", NULL, "DPGA1R" },

	{ "DPGA2L Mixer", "RX1 Playback Switch", "RX1 Playback" },
	{ "DPGA2L Mixer", "RX2 Playback Switch", "RX2 Playback" },
	{ "DPGA2L Mixer", "RX3 Playback Switch", "RX3 Playback" },
	{ "DPGA2L Mixer", "RX4 Playback Switch", "RX4 Playback" },
	{ "DPGA2L Mixer", "RX5 Playback Switch", "RX5 Playback" },
	{ "DPGA2L Mixer", "RX6 Playback Switch", "RX6 Playback" },

	{ "DPGA2R Mixer", "RX2 Playback Switch", "RX2 Playback" },
	{ "DPGA2R Mixer", "RX4 Playback Switch", "RX4 Playback" },
	{ "DPGA2R Mixer", "RX6 Playback Switch", "RX6 Playback" },

	{ "DPGA2L", NULL, "DPGA2L Mixer" },
	{ "DPGA2R", NULL, "DPGA2R Mixer" },

	{ "DAC2L", NULL, "DPGA2L" },
	{ "DAC2R", NULL, "DPGA2R" },

	{ "DPGA3L Mixer", "RX1 Playback Switch", "RX1 Playback" },
	{ "DPGA3L Mixer", "RX3 Playback Switch", "RX3 Playback" },
	{ "DPGA3L Mixer", "RX5 Playback Switch", "RX5 Playback" },

	{ "DPGA3R Mixer", "RX2 Playback Switch", "RX2 Playback" },
	{ "DPGA3R Mixer", "RX4 Playback Switch", "RX4 Playback" },
	{ "DPGA3R Mixer", "RX6 Playback Switch", "RX6 Playback" },

	{ "DPGA3L", NULL, "DPGA3L Mixer" },
	{ "DPGA3R", NULL, "DPGA3R Mixer" },

	{ "DAC3L", NULL, "DPGA3L" },
	{ "DAC3R", NULL, "DPGA3R" },

	{ "Headset Left Mixer", "DAC1L Playback Switch", "DAC1L" },
	{ "Headset Left Mixer", "APGA1 Playback Switch", "APGA1" },

	{ "Headset Right Mixer", "DAC1R Playback Switch", "DAC1R" },
	{ "Headset Right Mixer", "APGA2 Playback Switch", "APGA2" },

	{ "HS Left Driver", NULL, "Headset Left Mixer" },
	{ "HS Right Driver", NULL, "Headset Right Mixer" },

	{ "HSOL", NULL, "HS Left Driver" },
	{ "HSOR", NULL, "HS Right Driver" },

	/* Earphone playback path */
	{ "Earphone Mixer", "DAC2L Playback Switch", "DAC2L" },
	{ "Earphone Mixer", "APGA1 Playback Switch", "APGA1" },

	{ "Earphone Playback", "Switch", "Earphone Mixer" },
	{ "Earphone Driver", NULL, "Earphone Playback" },
	{ "EP", NULL, "Earphone Driver" },

	{ "Handsfree Left Mixer", "DAC2L Playback Switch", "DAC2L" },
	{ "Handsfree Left Mixer", "APGA1 Playback Switch", "APGA1" },

	{ "Handsfree Right Mixer", "DAC2R Playback Switch", "DAC2R" },
	{ "Handsfree Right Mixer", "APGA2 Playback Switch", "APGA2" },

	{ "HF Left PGA", NULL, "Handsfree Left Mixer" },
	{ "HF Right PGA", NULL, "Handsfree Right Mixer" },

	{ "HF Left Driver", NULL, "HF Left PGA" },
	{ "HF Right Driver", NULL, "HF Right PGA" },

	{ "HFL", NULL, "HF Left Driver" },
	{ "HFR", NULL, "HF Right Driver" },

	{ "LINEOUT1 Mixer", "DAC3L Playback Switch", "DAC3L" },
	{ "LINEOUT1 Mixer", "APGA1 Playback Switch", "APGA1" },

	{ "LINEOUT2 Mixer", "DAC3R Playback Switch", "DAC3R" },
	{ "LINEOUT2 Mixer", "APGA2 Playback Switch", "APGA2" },

	{ "LINEOUT1 Driver", NULL, "LINEOUT1 Mixer" },
	{ "LINEOUT2 Driver", NULL, "LINEOUT2 Mixer" },

	{ "LINEOUT1", NULL, "LINEOUT1 Driver" },
	{ "LINEOUT2", NULL, "LINEOUT2 Driver" },
};

static int isabelle_hs_mute(struct snd_soc_dai *dai, int mute)
{
	snd_soc_update_bits(dai->codec, ISABELLE_DAC1_SOFTRAMP_REG,
			BIT(4), (mute ? BIT(4) : 0));

	return 0;
}

static int isabelle_hf_mute(struct snd_soc_dai *dai, int mute)
{
	snd_soc_update_bits(dai->codec, ISABELLE_DAC2_SOFTRAMP_REG,
			BIT(4), (mute ? BIT(4) : 0));

	return 0;
}

static int isabelle_line_mute(struct snd_soc_dai *dai, int mute)
{
	snd_soc_update_bits(dai->codec, ISABELLE_DAC3_SOFTRAMP_REG,
			BIT(4), (mute ? BIT(4) : 0));

	return 0;
}

static int isabelle_set_bias_level(struct snd_soc_codec *codec,
				enum snd_soc_bias_level level)
{
	switch (level) {
	case SND_SOC_BIAS_ON:
		break;
	case SND_SOC_BIAS_PREPARE:
		break;

	case SND_SOC_BIAS_STANDBY:
		snd_soc_update_bits(codec, ISABELLE_PWR_EN_REG,
				ISABELLE_CHIP_EN, BIT(0));
		break;

	case SND_SOC_BIAS_OFF:
		snd_soc_update_bits(codec, ISABELLE_PWR_EN_REG,
				ISABELLE_CHIP_EN, 0);
		break;
	}

	return 0;
}

static int isabelle_hw_params(struct snd_pcm_substream *substream,
			      struct snd_pcm_hw_params *params,
			      struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	u16 aif = 0;
	unsigned int fs_val = 0;

	switch (params_rate(params)) {
	case 8000:
		fs_val = ISABELLE_FS_RATE_8;
		break;
	case 11025:
		fs_val = ISABELLE_FS_RATE_11;
		break;
	case 12000:
		fs_val = ISABELLE_FS_RATE_12;
		break;
	case 16000:
		fs_val = ISABELLE_FS_RATE_16;
		break;
	case 22050:
		fs_val = ISABELLE_FS_RATE_22;
		break;
	case 24000:
		fs_val = ISABELLE_FS_RATE_24;
		break;
	case 32000:
		fs_val = ISABELLE_FS_RATE_32;
		break;
	case 44100:
		fs_val = ISABELLE_FS_RATE_44;
		break;
	case 48000:
		fs_val = ISABELLE_FS_RATE_48;
		break;
	default:
		return -EINVAL;
	}

	snd_soc_update_bits(codec, ISABELLE_FS_RATE_CFG_REG,
			ISABELLE_FS_RATE_MASK, fs_val);

	/* bit size */
	switch (params_width(params)) {
	case 20:
		aif |= ISABELLE_AIF_LENGTH_20;
		break;
	case 32:
		aif |= ISABELLE_AIF_LENGTH_32;
		break;
	default:
		return -EINVAL;
	}

	snd_soc_update_bits(codec, ISABELLE_INTF_CFG_REG,
			ISABELLE_AIF_LENGTH_MASK, aif);

	return 0;
}

static int isabelle_set_dai_fmt(struct snd_soc_dai *codec_dai, unsigned int fmt)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	unsigned int aif_val = 0;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		aif_val &= ~ISABELLE_AIF_MS;
		break;
	case SND_SOC_DAIFMT_CBM_CFM:
		aif_val |= ISABELLE_AIF_MS;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		aif_val |= ISABELLE_I2S_MODE;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		aif_val |= ISABELLE_LEFT_J_MODE;
		break;
	case SND_SOC_DAIFMT_PDM:
		aif_val |= ISABELLE_PDM_MODE;
		break;
	default:
		return -EINVAL;
	}

	snd_soc_update_bits(codec, ISABELLE_INTF_CFG_REG,
			(ISABELLE_AIF_MS | ISABELLE_AIF_FMT_MASK), aif_val);

	return 0;
}

/* Rates supported by Isabelle driver */
#define ISABELLE_RATES		SNDRV_PCM_RATE_8000_48000

/* Formates supported by Isabelle driver. */
#define ISABELLE_FORMATS (SNDRV_PCM_FMTBIT_S20_3LE |\
			SNDRV_PCM_FMTBIT_S32_LE)

static const struct snd_soc_dai_ops isabelle_hs_dai_ops = {
	.hw_params	= isabelle_hw_params,
	.set_fmt	= isabelle_set_dai_fmt,
	.digital_mute	= isabelle_hs_mute,
};

static const struct snd_soc_dai_ops isabelle_hf_dai_ops = {
	.hw_params	= isabelle_hw_params,
	.set_fmt	= isabelle_set_dai_fmt,
	.digital_mute	= isabelle_hf_mute,
};

static const struct snd_soc_dai_ops isabelle_line_dai_ops = {
	.hw_params	= isabelle_hw_params,
	.set_fmt	= isabelle_set_dai_fmt,
	.digital_mute	= isabelle_line_mute,
};

static const struct snd_soc_dai_ops isabelle_ul_dai_ops = {
	.hw_params	= isabelle_hw_params,
	.set_fmt	= isabelle_set_dai_fmt,
};

/* ISABELLE dai structure */
static struct snd_soc_dai_driver isabelle_dai[] = {
	{
		.name = "isabelle-dl1",
		.playback = {
			.stream_name = "Headset Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = ISABELLE_RATES,
			.formats = ISABELLE_FORMATS,
		},
		.ops = &isabelle_hs_dai_ops,
	},
	{
		.name = "isabelle-dl2",
		.playback = {
			.stream_name = "Handsfree Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = ISABELLE_RATES,
			.formats = ISABELLE_FORMATS,
		},
		.ops = &isabelle_hf_dai_ops,
	},
	{
		.name = "isabelle-lineout",
		.playback = {
			.stream_name = "Lineout Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = ISABELLE_RATES,
			.formats = ISABELLE_FORMATS,
		},
		.ops = &isabelle_line_dai_ops,
	},
	{
		.name = "isabelle-ul",
		.capture = {
			.stream_name = "Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = ISABELLE_RATES,
			.formats = ISABELLE_FORMATS,
		},
		.ops = &isabelle_ul_dai_ops,
	},
};

static const struct snd_soc_codec_driver soc_codec_dev_isabelle = {
	.set_bias_level = isabelle_set_bias_level,
	.component_driver = {
		.controls		= isabelle_snd_controls,
		.num_controls		= ARRAY_SIZE(isabelle_snd_controls),
		.dapm_widgets		= isabelle_dapm_widgets,
		.num_dapm_widgets	= ARRAY_SIZE(isabelle_dapm_widgets),
		.dapm_routes		= isabelle_intercon,
		.num_dapm_routes	= ARRAY_SIZE(isabelle_intercon),
	},
	.idle_bias_off = true,
};

static const struct regmap_config isabelle_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = ISABELLE_MAX_REGISTER,
	.reg_defaults = isabelle_reg_defs,
	.num_reg_defaults = ARRAY_SIZE(isabelle_reg_defs),
	.cache_type = REGCACHE_RBTREE,
};

static int isabelle_i2c_probe(struct i2c_client *i2c,
			      const struct i2c_device_id *id)
{
	struct regmap *isabelle_regmap;
	int ret = 0;

	isabelle_regmap = devm_regmap_init_i2c(i2c, &isabelle_regmap_config);
	if (IS_ERR(isabelle_regmap)) {
		ret = PTR_ERR(isabelle_regmap);
		dev_err(&i2c->dev, "Failed to allocate register map: %d\n",
			ret);
		return ret;
	}
	i2c_set_clientdata(i2c, isabelle_regmap);

	ret =  snd_soc_register_codec(&i2c->dev,
				&soc_codec_dev_isabelle, isabelle_dai,
				ARRAY_SIZE(isabelle_dai));
	if (ret < 0) {
		dev_err(&i2c->dev, "Failed to register codec: %d\n", ret);
		return ret;
	}

	return ret;
}

static int isabelle_i2c_remove(struct i2c_client *client)
{
	snd_soc_unregister_codec(&client->dev);
	return 0;
}

static const struct i2c_device_id isabelle_i2c_id[] = {
	{ "isabelle", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, isabelle_i2c_id);

static struct i2c_driver isabelle_i2c_driver = {
	.driver = {
		.name = "isabelle",
	},
	.probe = isabelle_i2c_probe,
	.remove = isabelle_i2c_remove,
	.id_table = isabelle_i2c_id,
};

module_i2c_driver(isabelle_i2c_driver);

MODULE_DESCRIPTION("ASoC ISABELLE driver");
MODULE_AUTHOR("Vishwas A Deshpande <vishwas.a.deshpande@ti.com>");
MODULE_AUTHOR("M R Swami Reddy <MR.Swami.Reddy@ti.com>");
MODULE_LICENSE("GPL v2");
