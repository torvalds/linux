// SPDX-License-Identifier: GPL-2.0
/*
 * MediaTek ALSA SoC Audio DAI eTDM Control
 *
 * Copyright (c) 2022 MediaTek Inc.
 * Author: Bicycle Tsai <bicycle.tsai@mediatek.com>
 *         Trevor Wu <trevor.wu@mediatek.com>
 *         Chun-Chia Chiu <chun-chia.chiu@mediatek.com>
 */

#include <linux/bitfield.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <sound/pcm_params.h>
#include "mt8188-afe-clk.h"
#include "mt8188-afe-common.h"
#include "mt8188-reg.h"

#define MT8188_ETDM_MAX_CHANNELS 16
#define MT8188_ETDM_NORMAL_MAX_BCK_RATE 24576000
#define ETDM_TO_DAI_ID(x) ((x) + MT8188_AFE_IO_ETDM_START)
#define ENUM_TO_STR(x)	#x

enum {
	MTK_DAI_ETDM_FORMAT_I2S = 0,
	MTK_DAI_ETDM_FORMAT_LJ,
	MTK_DAI_ETDM_FORMAT_RJ,
	MTK_DAI_ETDM_FORMAT_EIAJ,
	MTK_DAI_ETDM_FORMAT_DSPA,
	MTK_DAI_ETDM_FORMAT_DSPB,
};

enum {
	MTK_DAI_ETDM_DATA_ONE_PIN = 0,
	MTK_DAI_ETDM_DATA_MULTI_PIN,
};

enum {
	ETDM_IN,
	ETDM_OUT,
};

enum {
	COWORK_ETDM_NONE = 0,
	COWORK_ETDM_IN1_M = 2,
	COWORK_ETDM_IN1_S = 3,
	COWORK_ETDM_IN2_M = 4,
	COWORK_ETDM_IN2_S = 5,
	COWORK_ETDM_OUT1_M = 10,
	COWORK_ETDM_OUT1_S = 11,
	COWORK_ETDM_OUT2_M = 12,
	COWORK_ETDM_OUT2_S = 13,
	COWORK_ETDM_OUT3_M = 14,
	COWORK_ETDM_OUT3_S = 15,
};

enum {
	ETDM_RELATCH_TIMING_A1A2SYS,
	ETDM_RELATCH_TIMING_A3SYS,
	ETDM_RELATCH_TIMING_A4SYS,
};

enum {
	ETDM_SYNC_NONE,
	ETDM_SYNC_FROM_IN1 = 2,
	ETDM_SYNC_FROM_IN2 = 4,
	ETDM_SYNC_FROM_OUT1 = 10,
	ETDM_SYNC_FROM_OUT2 = 12,
	ETDM_SYNC_FROM_OUT3 = 14,
};

struct etdm_con_reg {
	unsigned int con0;
	unsigned int con1;
	unsigned int con2;
	unsigned int con3;
	unsigned int con4;
	unsigned int con5;
};

struct mtk_dai_etdm_rate {
	unsigned int rate;
	unsigned int reg_value;
};

struct mtk_dai_etdm_priv {
	unsigned int clock_mode;
	unsigned int data_mode;
	bool slave_mode;
	bool lrck_inv;
	bool bck_inv;
	unsigned int format;
	unsigned int slots;
	unsigned int lrck_width;
	unsigned int mclk_freq;
	unsigned int mclk_fixed_apll;
	unsigned int mclk_apll;
	unsigned int mclk_dir;
	int cowork_source_id; //dai id
	unsigned int cowork_slv_count;
	int cowork_slv_id[MT8188_AFE_IO_ETDM_NUM - 1]; //dai_id
	bool in_disable_ch[MT8188_ETDM_MAX_CHANNELS];
	unsigned int en_ref_cnt;
	bool is_prepared;
};

static const struct mtk_dai_etdm_rate mt8188_etdm_rates[] = {
	{ .rate = 8000, .reg_value = 0, },
	{ .rate = 12000, .reg_value = 1, },
	{ .rate = 16000, .reg_value = 2, },
	{ .rate = 24000, .reg_value = 3, },
	{ .rate = 32000, .reg_value = 4, },
	{ .rate = 48000, .reg_value = 5, },
	{ .rate = 96000, .reg_value = 7, },
	{ .rate = 192000, .reg_value = 9, },
	{ .rate = 384000, .reg_value = 11, },
	{ .rate = 11025, .reg_value = 16, },
	{ .rate = 22050, .reg_value = 17, },
	{ .rate = 44100, .reg_value = 18, },
	{ .rate = 88200, .reg_value = 19, },
	{ .rate = 176400, .reg_value = 20, },
	{ .rate = 352800, .reg_value = 21, },
};

static int get_etdm_fs_timing(unsigned int rate)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(mt8188_etdm_rates); i++)
		if (mt8188_etdm_rates[i].rate == rate)
			return mt8188_etdm_rates[i].reg_value;

	return -EINVAL;
}

static unsigned int get_etdm_ch_fixup(unsigned int channels)
{
	if (channels > 16)
		return 24;
	else if (channels > 8)
		return 16;
	else if (channels > 4)
		return 8;
	else if (channels > 2)
		return 4;
	else
		return 2;
}

static int get_etdm_reg(unsigned int dai_id, struct etdm_con_reg *etdm_reg)
{
	switch (dai_id) {
	case MT8188_AFE_IO_ETDM1_IN:
		etdm_reg->con0 = ETDM_IN1_CON0;
		etdm_reg->con1 = ETDM_IN1_CON1;
		etdm_reg->con2 = ETDM_IN1_CON2;
		etdm_reg->con3 = ETDM_IN1_CON3;
		etdm_reg->con4 = ETDM_IN1_CON4;
		etdm_reg->con5 = ETDM_IN1_CON5;
		break;
	case MT8188_AFE_IO_ETDM2_IN:
		etdm_reg->con0 = ETDM_IN2_CON0;
		etdm_reg->con1 = ETDM_IN2_CON1;
		etdm_reg->con2 = ETDM_IN2_CON2;
		etdm_reg->con3 = ETDM_IN2_CON3;
		etdm_reg->con4 = ETDM_IN2_CON4;
		etdm_reg->con5 = ETDM_IN2_CON5;
		break;
	case MT8188_AFE_IO_ETDM1_OUT:
		etdm_reg->con0 = ETDM_OUT1_CON0;
		etdm_reg->con1 = ETDM_OUT1_CON1;
		etdm_reg->con2 = ETDM_OUT1_CON2;
		etdm_reg->con3 = ETDM_OUT1_CON3;
		etdm_reg->con4 = ETDM_OUT1_CON4;
		etdm_reg->con5 = ETDM_OUT1_CON5;
		break;
	case MT8188_AFE_IO_ETDM2_OUT:
		etdm_reg->con0 = ETDM_OUT2_CON0;
		etdm_reg->con1 = ETDM_OUT2_CON1;
		etdm_reg->con2 = ETDM_OUT2_CON2;
		etdm_reg->con3 = ETDM_OUT2_CON3;
		etdm_reg->con4 = ETDM_OUT2_CON4;
		etdm_reg->con5 = ETDM_OUT2_CON5;
		break;
	case MT8188_AFE_IO_ETDM3_OUT:
	case MT8188_AFE_IO_DPTX:
		etdm_reg->con0 = ETDM_OUT3_CON0;
		etdm_reg->con1 = ETDM_OUT3_CON1;
		etdm_reg->con2 = ETDM_OUT3_CON2;
		etdm_reg->con3 = ETDM_OUT3_CON3;
		etdm_reg->con4 = ETDM_OUT3_CON4;
		etdm_reg->con5 = ETDM_OUT3_CON5;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int get_etdm_dir(unsigned int dai_id)
{
	switch (dai_id) {
	case MT8188_AFE_IO_ETDM1_IN:
	case MT8188_AFE_IO_ETDM2_IN:
		return ETDM_IN;
	case MT8188_AFE_IO_ETDM1_OUT:
	case MT8188_AFE_IO_ETDM2_OUT:
	case MT8188_AFE_IO_ETDM3_OUT:
		return ETDM_OUT;
	default:
		return -EINVAL;
	}
}

static int get_etdm_wlen(unsigned int bitwidth)
{
	return bitwidth <= 16 ? 16 : 32;
}

static bool is_valid_etdm_dai(int dai_id)
{
	switch (dai_id) {
	case MT8188_AFE_IO_ETDM1_IN:
		fallthrough;
	case MT8188_AFE_IO_ETDM2_IN:
		fallthrough;
	case MT8188_AFE_IO_ETDM1_OUT:
		fallthrough;
	case MT8188_AFE_IO_ETDM2_OUT:
		fallthrough;
	case MT8188_AFE_IO_DPTX:
		fallthrough;
	case MT8188_AFE_IO_ETDM3_OUT:
		return true;
	default:
		return false;
	}
}

static int is_cowork_mode(struct snd_soc_dai *dai)
{
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);
	struct mt8188_afe_private *afe_priv = afe->platform_priv;
	struct mtk_dai_etdm_priv *etdm_data;

	if (!is_valid_etdm_dai(dai->id))
		return -EINVAL;
	etdm_data = afe_priv->dai_priv[dai->id];

	return (etdm_data->cowork_slv_count > 0 ||
		etdm_data->cowork_source_id != COWORK_ETDM_NONE);
}

static int sync_to_dai_id(int source_sel)
{
	switch (source_sel) {
	case ETDM_SYNC_FROM_IN1:
		return MT8188_AFE_IO_ETDM1_IN;
	case ETDM_SYNC_FROM_IN2:
		return MT8188_AFE_IO_ETDM2_IN;
	case ETDM_SYNC_FROM_OUT1:
		return MT8188_AFE_IO_ETDM1_OUT;
	case ETDM_SYNC_FROM_OUT2:
		return MT8188_AFE_IO_ETDM2_OUT;
	case ETDM_SYNC_FROM_OUT3:
		return MT8188_AFE_IO_ETDM3_OUT;
	default:
		return 0;
	}
}

static int get_etdm_cowork_master_id(struct snd_soc_dai *dai)
{
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);
	struct mt8188_afe_private *afe_priv = afe->platform_priv;
	struct mtk_dai_etdm_priv *etdm_data;
	int dai_id;

	if (!is_valid_etdm_dai(dai->id))
		return -EINVAL;
	etdm_data = afe_priv->dai_priv[dai->id];
	dai_id = etdm_data->cowork_source_id;

	if (dai_id == COWORK_ETDM_NONE)
		dai_id = dai->id;

	return dai_id;
}

static int mtk_dai_etdm_get_cg_id_by_dai_id(int dai_id)
{
	switch (dai_id) {
	case MT8188_AFE_IO_DPTX:
		return MT8188_CLK_AUD_HDMI_OUT;
	case MT8188_AFE_IO_ETDM1_IN:
		return MT8188_CLK_AUD_TDM_IN;
	case MT8188_AFE_IO_ETDM2_IN:
		return MT8188_CLK_AUD_I2SIN;
	case MT8188_AFE_IO_ETDM1_OUT:
		return MT8188_CLK_AUD_TDM_OUT;
	case MT8188_AFE_IO_ETDM2_OUT:
		return MT8188_CLK_AUD_I2S_OUT;
	case MT8188_AFE_IO_ETDM3_OUT:
		return MT8188_CLK_AUD_HDMI_OUT;
	default:
		return -EINVAL;
	}
}

static int mtk_dai_etdm_get_clk_id_by_dai_id(int dai_id)
{
	switch (dai_id) {
	case MT8188_AFE_IO_DPTX:
		return MT8188_CLK_TOP_DPTX_M_SEL;
	case MT8188_AFE_IO_ETDM1_IN:
		return MT8188_CLK_TOP_I2SI1_M_SEL;
	case MT8188_AFE_IO_ETDM2_IN:
		return MT8188_CLK_TOP_I2SI2_M_SEL;
	case MT8188_AFE_IO_ETDM1_OUT:
		return MT8188_CLK_TOP_I2SO1_M_SEL;
	case MT8188_AFE_IO_ETDM2_OUT:
		return MT8188_CLK_TOP_I2SO2_M_SEL;
	case MT8188_AFE_IO_ETDM3_OUT:
	default:
		return -EINVAL;
	}
}

static int mtk_dai_etdm_get_clkdiv_id_by_dai_id(int dai_id)
{
	switch (dai_id) {
	case MT8188_AFE_IO_DPTX:
		return MT8188_CLK_TOP_APLL12_DIV9;
	case MT8188_AFE_IO_ETDM1_IN:
		return MT8188_CLK_TOP_APLL12_DIV0;
	case MT8188_AFE_IO_ETDM2_IN:
		return MT8188_CLK_TOP_APLL12_DIV1;
	case MT8188_AFE_IO_ETDM1_OUT:
		return MT8188_CLK_TOP_APLL12_DIV2;
	case MT8188_AFE_IO_ETDM2_OUT:
		return MT8188_CLK_TOP_APLL12_DIV3;
	case MT8188_AFE_IO_ETDM3_OUT:
	default:
		return -EINVAL;
	}
}

static int mtk_dai_etdm_enable_mclk(struct mtk_base_afe *afe, int dai_id)
{
	struct mt8188_afe_private *afe_priv = afe->platform_priv;
	int clkdiv_id = mtk_dai_etdm_get_clkdiv_id_by_dai_id(dai_id);

	if (clkdiv_id < 0)
		return -EINVAL;

	mt8188_afe_enable_clk(afe, afe_priv->clk[clkdiv_id]);

	return 0;
}

static int mtk_dai_etdm_disable_mclk(struct mtk_base_afe *afe, int dai_id)
{
	struct mt8188_afe_private *afe_priv = afe->platform_priv;
	int clkdiv_id = mtk_dai_etdm_get_clkdiv_id_by_dai_id(dai_id);

	if (clkdiv_id < 0)
		return -EINVAL;

	mt8188_afe_disable_clk(afe, afe_priv->clk[clkdiv_id]);

	return 0;
}

static const struct snd_kcontrol_new mtk_dai_etdm_o048_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I020 Switch", AFE_CONN48, 20, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I022 Switch", AFE_CONN48, 22, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I046 Switch", AFE_CONN48_1, 14, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I070 Switch", AFE_CONN48_2, 6, 1, 0),
};

static const struct snd_kcontrol_new mtk_dai_etdm_o049_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I021 Switch", AFE_CONN49, 21, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I023 Switch", AFE_CONN49, 23, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I047 Switch", AFE_CONN49_1, 15, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I071 Switch", AFE_CONN49_2, 7, 1, 0),
};

static const struct snd_kcontrol_new mtk_dai_etdm_o050_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I024 Switch", AFE_CONN50, 24, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I048 Switch", AFE_CONN50_1, 16, 1, 0),
};

static const struct snd_kcontrol_new mtk_dai_etdm_o051_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I025 Switch", AFE_CONN51, 25, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I049 Switch", AFE_CONN51_1, 17, 1, 0),
};

static const struct snd_kcontrol_new mtk_dai_etdm_o052_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I026 Switch", AFE_CONN52, 26, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I050 Switch", AFE_CONN52_1, 18, 1, 0),
};

static const struct snd_kcontrol_new mtk_dai_etdm_o053_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I027 Switch", AFE_CONN53, 27, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I051 Switch", AFE_CONN53_1, 19, 1, 0),
};

static const struct snd_kcontrol_new mtk_dai_etdm_o054_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I028 Switch", AFE_CONN54, 28, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I052 Switch", AFE_CONN54_1, 20, 1, 0),
};

static const struct snd_kcontrol_new mtk_dai_etdm_o055_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I029 Switch", AFE_CONN55, 29, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I053 Switch", AFE_CONN55_1, 21, 1, 0),
};

static const struct snd_kcontrol_new mtk_dai_etdm_o056_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I030 Switch", AFE_CONN56, 30, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I054 Switch", AFE_CONN56_1, 22, 1, 0),
};

static const struct snd_kcontrol_new mtk_dai_etdm_o057_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I031 Switch", AFE_CONN57, 31, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I055 Switch", AFE_CONN57_1, 23, 1, 0),
};

static const struct snd_kcontrol_new mtk_dai_etdm_o058_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I032 Switch", AFE_CONN58_1, 0, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I056 Switch", AFE_CONN58_1, 24, 1, 0),
};

static const struct snd_kcontrol_new mtk_dai_etdm_o059_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I033 Switch", AFE_CONN59_1, 1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I057 Switch", AFE_CONN59_1, 25, 1, 0),
};

static const struct snd_kcontrol_new mtk_dai_etdm_o060_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I034 Switch", AFE_CONN60_1, 2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I058 Switch", AFE_CONN60_1, 26, 1, 0),
};

static const struct snd_kcontrol_new mtk_dai_etdm_o061_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I035 Switch", AFE_CONN61_1, 3, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I059 Switch", AFE_CONN61_1, 27, 1, 0),
};

static const struct snd_kcontrol_new mtk_dai_etdm_o062_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I036 Switch", AFE_CONN62_1, 4, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I060 Switch", AFE_CONN62_1, 28, 1, 0),
};

static const struct snd_kcontrol_new mtk_dai_etdm_o063_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I037 Switch", AFE_CONN63_1, 5, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I061 Switch", AFE_CONN63_1, 29, 1, 0),
};

static const struct snd_kcontrol_new mtk_dai_etdm_o072_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I020 Switch", AFE_CONN72, 20, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I022 Switch", AFE_CONN72, 22, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I046 Switch", AFE_CONN72_1, 14, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I070 Switch", AFE_CONN72_2, 6, 1, 0),
};

static const struct snd_kcontrol_new mtk_dai_etdm_o073_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I021 Switch", AFE_CONN73, 21, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I023 Switch", AFE_CONN73, 23, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I047 Switch", AFE_CONN73_1, 15, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I071 Switch", AFE_CONN73_2, 7, 1, 0),
};

static const struct snd_kcontrol_new mtk_dai_etdm_o074_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I024 Switch", AFE_CONN74, 24, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I048 Switch", AFE_CONN74_1, 16, 1, 0),
};

static const struct snd_kcontrol_new mtk_dai_etdm_o075_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I025 Switch", AFE_CONN75, 25, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I049 Switch", AFE_CONN75_1, 17, 1, 0),
};

static const struct snd_kcontrol_new mtk_dai_etdm_o076_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I026 Switch", AFE_CONN76, 26, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I050 Switch", AFE_CONN76_1, 18, 1, 0),
};

static const struct snd_kcontrol_new mtk_dai_etdm_o077_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I027 Switch", AFE_CONN77, 27, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I051 Switch", AFE_CONN77_1, 19, 1, 0),
};

static const struct snd_kcontrol_new mtk_dai_etdm_o078_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I028 Switch", AFE_CONN78, 28, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I052 Switch", AFE_CONN78_1, 20, 1, 0),
};

static const struct snd_kcontrol_new mtk_dai_etdm_o079_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I029 Switch", AFE_CONN79, 29, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I053 Switch", AFE_CONN79_1, 21, 1, 0),
};

static const struct snd_kcontrol_new mtk_dai_etdm_o080_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I030 Switch", AFE_CONN80, 30, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I054 Switch", AFE_CONN80_1, 22, 1, 0),
};

static const struct snd_kcontrol_new mtk_dai_etdm_o081_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I031 Switch", AFE_CONN81, 31, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I055 Switch", AFE_CONN81_1, 23, 1, 0),
};

static const struct snd_kcontrol_new mtk_dai_etdm_o082_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I032 Switch", AFE_CONN82_1, 0, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I056 Switch", AFE_CONN82_1, 24, 1, 0),
};

static const struct snd_kcontrol_new mtk_dai_etdm_o083_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I033 Switch", AFE_CONN83_1, 1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I057 Switch", AFE_CONN83_1, 25, 1, 0),
};

static const struct snd_kcontrol_new mtk_dai_etdm_o084_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I034 Switch", AFE_CONN84_1, 2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I058 Switch", AFE_CONN84_1, 26, 1, 0),
};

static const struct snd_kcontrol_new mtk_dai_etdm_o085_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I035 Switch", AFE_CONN85_1, 3, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I059 Switch", AFE_CONN85_1, 27, 1, 0),
};

static const struct snd_kcontrol_new mtk_dai_etdm_o086_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I036 Switch", AFE_CONN86_1, 4, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I060 Switch", AFE_CONN86_1, 28, 1, 0),
};

static const struct snd_kcontrol_new mtk_dai_etdm_o087_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I037 Switch", AFE_CONN87_1, 5, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I061 Switch", AFE_CONN87_1, 29, 1, 0),
};

static const char * const mt8188_etdm_clk_src_sel_text[] = {
	"26m",
	"a1sys_a2sys",
	"a3sys",
	"a4sys",
};

static SOC_ENUM_SINGLE_EXT_DECL(etdmout_clk_src_enum,
	mt8188_etdm_clk_src_sel_text);

static const char * const hdmitx_dptx_mux_map[] = {
	"Disconnect", "Connect",
};

static int hdmitx_dptx_mux_map_value[] = {
	0, 1,
};

/* HDMI_OUT_MUX */
static SOC_VALUE_ENUM_SINGLE_AUTODISABLE_DECL(hdmi_out_mux_map_enum,
				SND_SOC_NOPM,
				0,
				1,
				hdmitx_dptx_mux_map,
				hdmitx_dptx_mux_map_value);

static const struct snd_kcontrol_new hdmi_out_mux_control =
	SOC_DAPM_ENUM("HDMI_OUT_MUX", hdmi_out_mux_map_enum);

/* DPTX_OUT_MUX */
static SOC_VALUE_ENUM_SINGLE_AUTODISABLE_DECL(dptx_out_mux_map_enum,
				SND_SOC_NOPM,
				0,
				1,
				hdmitx_dptx_mux_map,
				hdmitx_dptx_mux_map_value);

static const struct snd_kcontrol_new dptx_out_mux_control =
	SOC_DAPM_ENUM("DPTX_OUT_MUX", dptx_out_mux_map_enum);

/* HDMI_CH0_MUX ~ HDMI_CH7_MUX */
static const char *const afe_conn_hdmi_mux_map[] = {
	"CH0", "CH1", "CH2", "CH3", "CH4", "CH5", "CH6", "CH7",
};

static int afe_conn_hdmi_mux_map_value[] = {
	0, 1, 2, 3, 4, 5, 6, 7,
};

static SOC_VALUE_ENUM_SINGLE_DECL(hdmi_ch0_mux_map_enum,
				AFE_TDMOUT_CONN0,
				0,
				0xf,
				afe_conn_hdmi_mux_map,
				afe_conn_hdmi_mux_map_value);

static const struct snd_kcontrol_new hdmi_ch0_mux_control =
	SOC_DAPM_ENUM("HDMI_CH0_MUX", hdmi_ch0_mux_map_enum);

static SOC_VALUE_ENUM_SINGLE_DECL(hdmi_ch1_mux_map_enum,
				AFE_TDMOUT_CONN0,
				4,
				0xf,
				afe_conn_hdmi_mux_map,
				afe_conn_hdmi_mux_map_value);

static const struct snd_kcontrol_new hdmi_ch1_mux_control =
	SOC_DAPM_ENUM("HDMI_CH1_MUX", hdmi_ch1_mux_map_enum);

static SOC_VALUE_ENUM_SINGLE_DECL(hdmi_ch2_mux_map_enum,
				AFE_TDMOUT_CONN0,
				8,
				0xf,
				afe_conn_hdmi_mux_map,
				afe_conn_hdmi_mux_map_value);

static const struct snd_kcontrol_new hdmi_ch2_mux_control =
	SOC_DAPM_ENUM("HDMI_CH2_MUX", hdmi_ch2_mux_map_enum);

static SOC_VALUE_ENUM_SINGLE_DECL(hdmi_ch3_mux_map_enum,
				AFE_TDMOUT_CONN0,
				12,
				0xf,
				afe_conn_hdmi_mux_map,
				afe_conn_hdmi_mux_map_value);

static const struct snd_kcontrol_new hdmi_ch3_mux_control =
	SOC_DAPM_ENUM("HDMI_CH3_MUX", hdmi_ch3_mux_map_enum);

static SOC_VALUE_ENUM_SINGLE_DECL(hdmi_ch4_mux_map_enum,
				AFE_TDMOUT_CONN0,
				16,
				0xf,
				afe_conn_hdmi_mux_map,
				afe_conn_hdmi_mux_map_value);

static const struct snd_kcontrol_new hdmi_ch4_mux_control =
	SOC_DAPM_ENUM("HDMI_CH4_MUX", hdmi_ch4_mux_map_enum);

static SOC_VALUE_ENUM_SINGLE_DECL(hdmi_ch5_mux_map_enum,
				AFE_TDMOUT_CONN0,
				20,
				0xf,
				afe_conn_hdmi_mux_map,
				afe_conn_hdmi_mux_map_value);

static const struct snd_kcontrol_new hdmi_ch5_mux_control =
	SOC_DAPM_ENUM("HDMI_CH5_MUX", hdmi_ch5_mux_map_enum);

static SOC_VALUE_ENUM_SINGLE_DECL(hdmi_ch6_mux_map_enum,
				AFE_TDMOUT_CONN0,
				24,
				0xf,
				afe_conn_hdmi_mux_map,
				afe_conn_hdmi_mux_map_value);

static const struct snd_kcontrol_new hdmi_ch6_mux_control =
	SOC_DAPM_ENUM("HDMI_CH6_MUX", hdmi_ch6_mux_map_enum);

static SOC_VALUE_ENUM_SINGLE_DECL(hdmi_ch7_mux_map_enum,
				AFE_TDMOUT_CONN0,
				28,
				0xf,
				afe_conn_hdmi_mux_map,
				afe_conn_hdmi_mux_map_value);

static const struct snd_kcontrol_new hdmi_ch7_mux_control =
	SOC_DAPM_ENUM("HDMI_CH7_MUX", hdmi_ch7_mux_map_enum);

static int mt8188_etdm_clk_src_sel_put(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(component);
	unsigned int source = ucontrol->value.enumerated.item[0];
	unsigned int val;
	unsigned int old_val;
	unsigned int mask;
	unsigned int reg;
	unsigned int shift;

	if (source >= e->items)
		return -EINVAL;

	if (!strcmp(kcontrol->id.name, "ETDM_OUT1_Clock_Source")) {
		reg = ETDM_OUT1_CON4;
		mask = ETDM_OUT_CON4_CLOCK_MASK;
		shift = ETDM_OUT_CON4_CLOCK_SHIFT;
		val = FIELD_PREP(ETDM_OUT_CON4_CLOCK_MASK, source);
	} else if (!strcmp(kcontrol->id.name, "ETDM_OUT2_Clock_Source")) {
		reg = ETDM_OUT2_CON4;
		mask = ETDM_OUT_CON4_CLOCK_MASK;
		shift = ETDM_OUT_CON4_CLOCK_SHIFT;
		val = FIELD_PREP(ETDM_OUT_CON4_CLOCK_MASK, source);
	} else if (!strcmp(kcontrol->id.name, "ETDM_OUT3_Clock_Source")) {
		reg = ETDM_OUT3_CON4;
		mask = ETDM_OUT_CON4_CLOCK_MASK;
		shift = ETDM_OUT_CON4_CLOCK_SHIFT;
		val = FIELD_PREP(ETDM_OUT_CON4_CLOCK_MASK, source);
	} else if (!strcmp(kcontrol->id.name, "ETDM_IN1_Clock_Source")) {
		reg = ETDM_IN1_CON2;
		mask = ETDM_IN_CON2_CLOCK_MASK;
		shift = ETDM_IN_CON2_CLOCK_SHIFT;
		val = FIELD_PREP(ETDM_IN_CON2_CLOCK_MASK, source);
	} else if (!strcmp(kcontrol->id.name, "ETDM_IN2_Clock_Source")) {
		reg = ETDM_IN2_CON2;
		mask = ETDM_IN_CON2_CLOCK_MASK;
		shift = ETDM_IN_CON2_CLOCK_SHIFT;
		val = FIELD_PREP(ETDM_IN_CON2_CLOCK_MASK, source);
	} else {
		return -EINVAL;
	}

	regmap_read(afe->regmap, reg, &old_val);
	old_val &= mask;
	old_val >>= shift;

	if (old_val == val)
		return 0;

	regmap_update_bits(afe->regmap, reg, mask, val);

	return 1;
}

static int mt8188_etdm_clk_src_sel_get(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(component);
	unsigned int value;
	unsigned int reg;
	unsigned int mask;
	unsigned int shift;

	if (!strcmp(kcontrol->id.name, "ETDM_OUT1_Clock_Source")) {
		reg = ETDM_OUT1_CON4;
		mask = ETDM_OUT_CON4_CLOCK_MASK;
		shift = ETDM_OUT_CON4_CLOCK_SHIFT;
	} else if (!strcmp(kcontrol->id.name, "ETDM_OUT2_Clock_Source")) {
		reg = ETDM_OUT2_CON4;
		mask = ETDM_OUT_CON4_CLOCK_MASK;
		shift = ETDM_OUT_CON4_CLOCK_SHIFT;
	} else if (!strcmp(kcontrol->id.name, "ETDM_OUT3_Clock_Source")) {
		reg = ETDM_OUT3_CON4;
		mask = ETDM_OUT_CON4_CLOCK_MASK;
		shift = ETDM_OUT_CON4_CLOCK_SHIFT;
	} else if (!strcmp(kcontrol->id.name, "ETDM_IN1_Clock_Source")) {
		reg = ETDM_IN1_CON2;
		mask = ETDM_IN_CON2_CLOCK_MASK;
		shift = ETDM_IN_CON2_CLOCK_SHIFT;
	} else if (!strcmp(kcontrol->id.name, "ETDM_IN2_Clock_Source")) {
		reg = ETDM_IN2_CON2;
		mask = ETDM_IN_CON2_CLOCK_MASK;
		shift = ETDM_IN_CON2_CLOCK_SHIFT;
	} else {
		return -EINVAL;
	}

	regmap_read(afe->regmap, reg, &value);

	value &= mask;
	value >>= shift;
	ucontrol->value.enumerated.item[0] = value;
	return 0;
}

static const struct snd_kcontrol_new mtk_dai_etdm_controls[] = {
	SOC_ENUM_EXT("ETDM_OUT1_Clock_Source", etdmout_clk_src_enum,
		     mt8188_etdm_clk_src_sel_get,
		     mt8188_etdm_clk_src_sel_put),
	SOC_ENUM_EXT("ETDM_OUT2_Clock_Source", etdmout_clk_src_enum,
		     mt8188_etdm_clk_src_sel_get,
		     mt8188_etdm_clk_src_sel_put),
	SOC_ENUM_EXT("ETDM_OUT3_Clock_Source", etdmout_clk_src_enum,
		     mt8188_etdm_clk_src_sel_get,
		     mt8188_etdm_clk_src_sel_put),
	SOC_ENUM_EXT("ETDM_IN1_Clock_Source", etdmout_clk_src_enum,
		     mt8188_etdm_clk_src_sel_get,
		     mt8188_etdm_clk_src_sel_put),
	SOC_ENUM_EXT("ETDM_IN2_Clock_Source", etdmout_clk_src_enum,
		     mt8188_etdm_clk_src_sel_get,
		     mt8188_etdm_clk_src_sel_put),
};

static const struct snd_soc_dapm_widget mtk_dai_etdm_widgets[] = {
	/* eTDM_IN2 */
	SND_SOC_DAPM_MIXER("I012", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I013", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I014", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I015", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I016", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I017", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I018", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I019", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I188", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I189", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I190", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I191", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I192", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I193", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I194", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I195", SND_SOC_NOPM, 0, 0, NULL, 0),

	/* eTDM_IN1 */
	SND_SOC_DAPM_MIXER("I072", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I073", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I074", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I075", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I076", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I077", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I078", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I079", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I080", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I081", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I082", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I083", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I084", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I085", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I086", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I087", SND_SOC_NOPM, 0, 0, NULL, 0),

	/* eTDM_OUT2 */
	SND_SOC_DAPM_MIXER("O048", SND_SOC_NOPM, 0, 0,
			   mtk_dai_etdm_o048_mix, ARRAY_SIZE(mtk_dai_etdm_o048_mix)),
	SND_SOC_DAPM_MIXER("O049", SND_SOC_NOPM, 0, 0,
			   mtk_dai_etdm_o049_mix, ARRAY_SIZE(mtk_dai_etdm_o049_mix)),
	SND_SOC_DAPM_MIXER("O050", SND_SOC_NOPM, 0, 0,
			   mtk_dai_etdm_o050_mix, ARRAY_SIZE(mtk_dai_etdm_o050_mix)),
	SND_SOC_DAPM_MIXER("O051", SND_SOC_NOPM, 0, 0,
			   mtk_dai_etdm_o051_mix, ARRAY_SIZE(mtk_dai_etdm_o051_mix)),
	SND_SOC_DAPM_MIXER("O052", SND_SOC_NOPM, 0, 0,
			   mtk_dai_etdm_o052_mix, ARRAY_SIZE(mtk_dai_etdm_o052_mix)),
	SND_SOC_DAPM_MIXER("O053", SND_SOC_NOPM, 0, 0,
			   mtk_dai_etdm_o053_mix, ARRAY_SIZE(mtk_dai_etdm_o053_mix)),
	SND_SOC_DAPM_MIXER("O054", SND_SOC_NOPM, 0, 0,
			   mtk_dai_etdm_o054_mix, ARRAY_SIZE(mtk_dai_etdm_o054_mix)),
	SND_SOC_DAPM_MIXER("O055", SND_SOC_NOPM, 0, 0,
			   mtk_dai_etdm_o055_mix, ARRAY_SIZE(mtk_dai_etdm_o055_mix)),
	SND_SOC_DAPM_MIXER("O056", SND_SOC_NOPM, 0, 0,
			   mtk_dai_etdm_o056_mix, ARRAY_SIZE(mtk_dai_etdm_o056_mix)),
	SND_SOC_DAPM_MIXER("O057", SND_SOC_NOPM, 0, 0,
			   mtk_dai_etdm_o057_mix, ARRAY_SIZE(mtk_dai_etdm_o057_mix)),
	SND_SOC_DAPM_MIXER("O058", SND_SOC_NOPM, 0, 0,
			   mtk_dai_etdm_o058_mix, ARRAY_SIZE(mtk_dai_etdm_o058_mix)),
	SND_SOC_DAPM_MIXER("O059", SND_SOC_NOPM, 0, 0,
			   mtk_dai_etdm_o059_mix, ARRAY_SIZE(mtk_dai_etdm_o059_mix)),
	SND_SOC_DAPM_MIXER("O060", SND_SOC_NOPM, 0, 0,
			   mtk_dai_etdm_o060_mix, ARRAY_SIZE(mtk_dai_etdm_o060_mix)),
	SND_SOC_DAPM_MIXER("O061", SND_SOC_NOPM, 0, 0,
			   mtk_dai_etdm_o061_mix, ARRAY_SIZE(mtk_dai_etdm_o061_mix)),
	SND_SOC_DAPM_MIXER("O062", SND_SOC_NOPM, 0, 0,
			   mtk_dai_etdm_o062_mix, ARRAY_SIZE(mtk_dai_etdm_o062_mix)),
	SND_SOC_DAPM_MIXER("O063", SND_SOC_NOPM, 0, 0,
			   mtk_dai_etdm_o063_mix, ARRAY_SIZE(mtk_dai_etdm_o063_mix)),

	/* eTDM_OUT1 */
	SND_SOC_DAPM_MIXER("O072", SND_SOC_NOPM, 0, 0,
			   mtk_dai_etdm_o072_mix, ARRAY_SIZE(mtk_dai_etdm_o072_mix)),
	SND_SOC_DAPM_MIXER("O073", SND_SOC_NOPM, 0, 0,
			   mtk_dai_etdm_o073_mix, ARRAY_SIZE(mtk_dai_etdm_o073_mix)),
	SND_SOC_DAPM_MIXER("O074", SND_SOC_NOPM, 0, 0,
			   mtk_dai_etdm_o074_mix, ARRAY_SIZE(mtk_dai_etdm_o074_mix)),
	SND_SOC_DAPM_MIXER("O075", SND_SOC_NOPM, 0, 0,
			   mtk_dai_etdm_o075_mix, ARRAY_SIZE(mtk_dai_etdm_o075_mix)),
	SND_SOC_DAPM_MIXER("O076", SND_SOC_NOPM, 0, 0,
			   mtk_dai_etdm_o076_mix, ARRAY_SIZE(mtk_dai_etdm_o076_mix)),
	SND_SOC_DAPM_MIXER("O077", SND_SOC_NOPM, 0, 0,
			   mtk_dai_etdm_o077_mix, ARRAY_SIZE(mtk_dai_etdm_o077_mix)),
	SND_SOC_DAPM_MIXER("O078", SND_SOC_NOPM, 0, 0,
			   mtk_dai_etdm_o078_mix, ARRAY_SIZE(mtk_dai_etdm_o078_mix)),
	SND_SOC_DAPM_MIXER("O079", SND_SOC_NOPM, 0, 0,
			   mtk_dai_etdm_o079_mix, ARRAY_SIZE(mtk_dai_etdm_o079_mix)),
	SND_SOC_DAPM_MIXER("O080", SND_SOC_NOPM, 0, 0,
			   mtk_dai_etdm_o080_mix, ARRAY_SIZE(mtk_dai_etdm_o080_mix)),
	SND_SOC_DAPM_MIXER("O081", SND_SOC_NOPM, 0, 0,
			   mtk_dai_etdm_o081_mix, ARRAY_SIZE(mtk_dai_etdm_o081_mix)),
	SND_SOC_DAPM_MIXER("O082", SND_SOC_NOPM, 0, 0,
			   mtk_dai_etdm_o082_mix, ARRAY_SIZE(mtk_dai_etdm_o082_mix)),
	SND_SOC_DAPM_MIXER("O083", SND_SOC_NOPM, 0, 0,
			   mtk_dai_etdm_o083_mix, ARRAY_SIZE(mtk_dai_etdm_o083_mix)),
	SND_SOC_DAPM_MIXER("O084", SND_SOC_NOPM, 0, 0,
			   mtk_dai_etdm_o084_mix, ARRAY_SIZE(mtk_dai_etdm_o084_mix)),
	SND_SOC_DAPM_MIXER("O085", SND_SOC_NOPM, 0, 0,
			   mtk_dai_etdm_o085_mix, ARRAY_SIZE(mtk_dai_etdm_o085_mix)),
	SND_SOC_DAPM_MIXER("O086", SND_SOC_NOPM, 0, 0,
			   mtk_dai_etdm_o086_mix, ARRAY_SIZE(mtk_dai_etdm_o086_mix)),
	SND_SOC_DAPM_MIXER("O087", SND_SOC_NOPM, 0, 0,
			   mtk_dai_etdm_o087_mix, ARRAY_SIZE(mtk_dai_etdm_o087_mix)),

	/* eTDM_OUT3 */
	SND_SOC_DAPM_MUX("HDMI_OUT_MUX", SND_SOC_NOPM, 0, 0,
			 &hdmi_out_mux_control),
	SND_SOC_DAPM_MUX("DPTX_OUT_MUX", SND_SOC_NOPM, 0, 0,
			 &dptx_out_mux_control),

	SND_SOC_DAPM_MUX("HDMI_CH0_MUX", SND_SOC_NOPM, 0, 0,
			 &hdmi_ch0_mux_control),
	SND_SOC_DAPM_MUX("HDMI_CH1_MUX", SND_SOC_NOPM, 0, 0,
			 &hdmi_ch1_mux_control),
	SND_SOC_DAPM_MUX("HDMI_CH2_MUX", SND_SOC_NOPM, 0, 0,
			 &hdmi_ch2_mux_control),
	SND_SOC_DAPM_MUX("HDMI_CH3_MUX", SND_SOC_NOPM, 0, 0,
			 &hdmi_ch3_mux_control),
	SND_SOC_DAPM_MUX("HDMI_CH4_MUX", SND_SOC_NOPM, 0, 0,
			 &hdmi_ch4_mux_control),
	SND_SOC_DAPM_MUX("HDMI_CH5_MUX", SND_SOC_NOPM, 0, 0,
			 &hdmi_ch5_mux_control),
	SND_SOC_DAPM_MUX("HDMI_CH6_MUX", SND_SOC_NOPM, 0, 0,
			 &hdmi_ch6_mux_control),
	SND_SOC_DAPM_MUX("HDMI_CH7_MUX", SND_SOC_NOPM, 0, 0,
			 &hdmi_ch7_mux_control),

	SND_SOC_DAPM_INPUT("ETDM_INPUT"),
	SND_SOC_DAPM_OUTPUT("ETDM_OUTPUT"),
};

static const struct snd_soc_dapm_route mtk_dai_etdm_routes[] = {
	{"I012", NULL, "ETDM2_IN"},
	{"I013", NULL, "ETDM2_IN"},
	{"I014", NULL, "ETDM2_IN"},
	{"I015", NULL, "ETDM2_IN"},
	{"I016", NULL, "ETDM2_IN"},
	{"I017", NULL, "ETDM2_IN"},
	{"I018", NULL, "ETDM2_IN"},
	{"I019", NULL, "ETDM2_IN"},
	{"I188", NULL, "ETDM2_IN"},
	{"I189", NULL, "ETDM2_IN"},
	{"I190", NULL, "ETDM2_IN"},
	{"I191", NULL, "ETDM2_IN"},
	{"I192", NULL, "ETDM2_IN"},
	{"I193", NULL, "ETDM2_IN"},
	{"I194", NULL, "ETDM2_IN"},
	{"I195", NULL, "ETDM2_IN"},

	{"I072", NULL, "ETDM1_IN"},
	{"I073", NULL, "ETDM1_IN"},
	{"I074", NULL, "ETDM1_IN"},
	{"I075", NULL, "ETDM1_IN"},
	{"I076", NULL, "ETDM1_IN"},
	{"I077", NULL, "ETDM1_IN"},
	{"I078", NULL, "ETDM1_IN"},
	{"I079", NULL, "ETDM1_IN"},
	{"I080", NULL, "ETDM1_IN"},
	{"I081", NULL, "ETDM1_IN"},
	{"I082", NULL, "ETDM1_IN"},
	{"I083", NULL, "ETDM1_IN"},
	{"I084", NULL, "ETDM1_IN"},
	{"I085", NULL, "ETDM1_IN"},
	{"I086", NULL, "ETDM1_IN"},
	{"I087", NULL, "ETDM1_IN"},

	{"UL8", NULL, "ETDM1_IN"},
	{"UL3", NULL, "ETDM2_IN"},

	{"ETDM2_OUT", NULL, "O048"},
	{"ETDM2_OUT", NULL, "O049"},
	{"ETDM2_OUT", NULL, "O050"},
	{"ETDM2_OUT", NULL, "O051"},
	{"ETDM2_OUT", NULL, "O052"},
	{"ETDM2_OUT", NULL, "O053"},
	{"ETDM2_OUT", NULL, "O054"},
	{"ETDM2_OUT", NULL, "O055"},
	{"ETDM2_OUT", NULL, "O056"},
	{"ETDM2_OUT", NULL, "O057"},
	{"ETDM2_OUT", NULL, "O058"},
	{"ETDM2_OUT", NULL, "O059"},
	{"ETDM2_OUT", NULL, "O060"},
	{"ETDM2_OUT", NULL, "O061"},
	{"ETDM2_OUT", NULL, "O062"},
	{"ETDM2_OUT", NULL, "O063"},

	{"ETDM1_OUT", NULL, "O072"},
	{"ETDM1_OUT", NULL, "O073"},
	{"ETDM1_OUT", NULL, "O074"},
	{"ETDM1_OUT", NULL, "O075"},
	{"ETDM1_OUT", NULL, "O076"},
	{"ETDM1_OUT", NULL, "O077"},
	{"ETDM1_OUT", NULL, "O078"},
	{"ETDM1_OUT", NULL, "O079"},
	{"ETDM1_OUT", NULL, "O080"},
	{"ETDM1_OUT", NULL, "O081"},
	{"ETDM1_OUT", NULL, "O082"},
	{"ETDM1_OUT", NULL, "O083"},
	{"ETDM1_OUT", NULL, "O084"},
	{"ETDM1_OUT", NULL, "O085"},
	{"ETDM1_OUT", NULL, "O086"},
	{"ETDM1_OUT", NULL, "O087"},

	{"O048", "I020 Switch", "I020"},
	{"O049", "I021 Switch", "I021"},

	{"O048", "I022 Switch", "I022"},
	{"O049", "I023 Switch", "I023"},
	{"O050", "I024 Switch", "I024"},
	{"O051", "I025 Switch", "I025"},
	{"O052", "I026 Switch", "I026"},
	{"O053", "I027 Switch", "I027"},
	{"O054", "I028 Switch", "I028"},
	{"O055", "I029 Switch", "I029"},
	{"O056", "I030 Switch", "I030"},
	{"O057", "I031 Switch", "I031"},
	{"O058", "I032 Switch", "I032"},
	{"O059", "I033 Switch", "I033"},
	{"O060", "I034 Switch", "I034"},
	{"O061", "I035 Switch", "I035"},
	{"O062", "I036 Switch", "I036"},
	{"O063", "I037 Switch", "I037"},

	{"O048", "I046 Switch", "I046"},
	{"O049", "I047 Switch", "I047"},
	{"O050", "I048 Switch", "I048"},
	{"O051", "I049 Switch", "I049"},
	{"O052", "I050 Switch", "I050"},
	{"O053", "I051 Switch", "I051"},
	{"O054", "I052 Switch", "I052"},
	{"O055", "I053 Switch", "I053"},
	{"O056", "I054 Switch", "I054"},
	{"O057", "I055 Switch", "I055"},
	{"O058", "I056 Switch", "I056"},
	{"O059", "I057 Switch", "I057"},
	{"O060", "I058 Switch", "I058"},
	{"O061", "I059 Switch", "I059"},
	{"O062", "I060 Switch", "I060"},
	{"O063", "I061 Switch", "I061"},

	{"O048", "I070 Switch", "I070"},
	{"O049", "I071 Switch", "I071"},

	{"O072", "I020 Switch", "I020"},
	{"O073", "I021 Switch", "I021"},

	{"O072", "I022 Switch", "I022"},
	{"O073", "I023 Switch", "I023"},
	{"O074", "I024 Switch", "I024"},
	{"O075", "I025 Switch", "I025"},
	{"O076", "I026 Switch", "I026"},
	{"O077", "I027 Switch", "I027"},
	{"O078", "I028 Switch", "I028"},
	{"O079", "I029 Switch", "I029"},
	{"O080", "I030 Switch", "I030"},
	{"O081", "I031 Switch", "I031"},
	{"O082", "I032 Switch", "I032"},
	{"O083", "I033 Switch", "I033"},
	{"O084", "I034 Switch", "I034"},
	{"O085", "I035 Switch", "I035"},
	{"O086", "I036 Switch", "I036"},
	{"O087", "I037 Switch", "I037"},

	{"O072", "I046 Switch", "I046"},
	{"O073", "I047 Switch", "I047"},
	{"O074", "I048 Switch", "I048"},
	{"O075", "I049 Switch", "I049"},
	{"O076", "I050 Switch", "I050"},
	{"O077", "I051 Switch", "I051"},
	{"O078", "I052 Switch", "I052"},
	{"O079", "I053 Switch", "I053"},
	{"O080", "I054 Switch", "I054"},
	{"O081", "I055 Switch", "I055"},
	{"O082", "I056 Switch", "I056"},
	{"O083", "I057 Switch", "I057"},
	{"O084", "I058 Switch", "I058"},
	{"O085", "I059 Switch", "I059"},
	{"O086", "I060 Switch", "I060"},
	{"O087", "I061 Switch", "I061"},

	{"O072", "I070 Switch", "I070"},
	{"O073", "I071 Switch", "I071"},

	{"HDMI_CH0_MUX", "CH0", "DL10"},
	{"HDMI_CH0_MUX", "CH1", "DL10"},
	{"HDMI_CH0_MUX", "CH2", "DL10"},
	{"HDMI_CH0_MUX", "CH3", "DL10"},
	{"HDMI_CH0_MUX", "CH4", "DL10"},
	{"HDMI_CH0_MUX", "CH5", "DL10"},
	{"HDMI_CH0_MUX", "CH6", "DL10"},
	{"HDMI_CH0_MUX", "CH7", "DL10"},

	{"HDMI_CH1_MUX", "CH0", "DL10"},
	{"HDMI_CH1_MUX", "CH1", "DL10"},
	{"HDMI_CH1_MUX", "CH2", "DL10"},
	{"HDMI_CH1_MUX", "CH3", "DL10"},
	{"HDMI_CH1_MUX", "CH4", "DL10"},
	{"HDMI_CH1_MUX", "CH5", "DL10"},
	{"HDMI_CH1_MUX", "CH6", "DL10"},
	{"HDMI_CH1_MUX", "CH7", "DL10"},

	{"HDMI_CH2_MUX", "CH0", "DL10"},
	{"HDMI_CH2_MUX", "CH1", "DL10"},
	{"HDMI_CH2_MUX", "CH2", "DL10"},
	{"HDMI_CH2_MUX", "CH3", "DL10"},
	{"HDMI_CH2_MUX", "CH4", "DL10"},
	{"HDMI_CH2_MUX", "CH5", "DL10"},
	{"HDMI_CH2_MUX", "CH6", "DL10"},
	{"HDMI_CH2_MUX", "CH7", "DL10"},

	{"HDMI_CH3_MUX", "CH0", "DL10"},
	{"HDMI_CH3_MUX", "CH1", "DL10"},
	{"HDMI_CH3_MUX", "CH2", "DL10"},
	{"HDMI_CH3_MUX", "CH3", "DL10"},
	{"HDMI_CH3_MUX", "CH4", "DL10"},
	{"HDMI_CH3_MUX", "CH5", "DL10"},
	{"HDMI_CH3_MUX", "CH6", "DL10"},
	{"HDMI_CH3_MUX", "CH7", "DL10"},

	{"HDMI_CH4_MUX", "CH0", "DL10"},
	{"HDMI_CH4_MUX", "CH1", "DL10"},
	{"HDMI_CH4_MUX", "CH2", "DL10"},
	{"HDMI_CH4_MUX", "CH3", "DL10"},
	{"HDMI_CH4_MUX", "CH4", "DL10"},
	{"HDMI_CH4_MUX", "CH5", "DL10"},
	{"HDMI_CH4_MUX", "CH6", "DL10"},
	{"HDMI_CH4_MUX", "CH7", "DL10"},

	{"HDMI_CH5_MUX", "CH0", "DL10"},
	{"HDMI_CH5_MUX", "CH1", "DL10"},
	{"HDMI_CH5_MUX", "CH2", "DL10"},
	{"HDMI_CH5_MUX", "CH3", "DL10"},
	{"HDMI_CH5_MUX", "CH4", "DL10"},
	{"HDMI_CH5_MUX", "CH5", "DL10"},
	{"HDMI_CH5_MUX", "CH6", "DL10"},
	{"HDMI_CH5_MUX", "CH7", "DL10"},

	{"HDMI_CH6_MUX", "CH0", "DL10"},
	{"HDMI_CH6_MUX", "CH1", "DL10"},
	{"HDMI_CH6_MUX", "CH2", "DL10"},
	{"HDMI_CH6_MUX", "CH3", "DL10"},
	{"HDMI_CH6_MUX", "CH4", "DL10"},
	{"HDMI_CH6_MUX", "CH5", "DL10"},
	{"HDMI_CH6_MUX", "CH6", "DL10"},
	{"HDMI_CH6_MUX", "CH7", "DL10"},

	{"HDMI_CH7_MUX", "CH0", "DL10"},
	{"HDMI_CH7_MUX", "CH1", "DL10"},
	{"HDMI_CH7_MUX", "CH2", "DL10"},
	{"HDMI_CH7_MUX", "CH3", "DL10"},
	{"HDMI_CH7_MUX", "CH4", "DL10"},
	{"HDMI_CH7_MUX", "CH5", "DL10"},
	{"HDMI_CH7_MUX", "CH6", "DL10"},
	{"HDMI_CH7_MUX", "CH7", "DL10"},

	{"HDMI_OUT_MUX", "Connect", "HDMI_CH0_MUX"},
	{"HDMI_OUT_MUX", "Connect", "HDMI_CH1_MUX"},
	{"HDMI_OUT_MUX", "Connect", "HDMI_CH2_MUX"},
	{"HDMI_OUT_MUX", "Connect", "HDMI_CH3_MUX"},
	{"HDMI_OUT_MUX", "Connect", "HDMI_CH4_MUX"},
	{"HDMI_OUT_MUX", "Connect", "HDMI_CH5_MUX"},
	{"HDMI_OUT_MUX", "Connect", "HDMI_CH6_MUX"},
	{"HDMI_OUT_MUX", "Connect", "HDMI_CH7_MUX"},

	{"DPTX_OUT_MUX", "Connect", "HDMI_CH0_MUX"},
	{"DPTX_OUT_MUX", "Connect", "HDMI_CH1_MUX"},
	{"DPTX_OUT_MUX", "Connect", "HDMI_CH2_MUX"},
	{"DPTX_OUT_MUX", "Connect", "HDMI_CH3_MUX"},
	{"DPTX_OUT_MUX", "Connect", "HDMI_CH4_MUX"},
	{"DPTX_OUT_MUX", "Connect", "HDMI_CH5_MUX"},
	{"DPTX_OUT_MUX", "Connect", "HDMI_CH6_MUX"},
	{"DPTX_OUT_MUX", "Connect", "HDMI_CH7_MUX"},

	{"ETDM3_OUT", NULL, "HDMI_OUT_MUX"},
	{"DPTX", NULL, "DPTX_OUT_MUX"},

	{"ETDM_OUTPUT", NULL, "DPTX"},
	{"ETDM_OUTPUT", NULL, "ETDM1_OUT"},
	{"ETDM_OUTPUT", NULL, "ETDM2_OUT"},
	{"ETDM_OUTPUT", NULL, "ETDM3_OUT"},
	{"ETDM1_IN", NULL, "ETDM_INPUT"},
	{"ETDM2_IN", NULL, "ETDM_INPUT"},
};

static int mt8188_afe_enable_etdm(struct mtk_base_afe *afe, int dai_id)
{
	struct mt8188_afe_private *afe_priv = afe->platform_priv;
	struct mtk_dai_etdm_priv *etdm_data;
	struct etdm_con_reg etdm_reg;
	unsigned long flags;
	int ret = 0;

	if (!is_valid_etdm_dai(dai_id))
		return -EINVAL;
	etdm_data = afe_priv->dai_priv[dai_id];

	dev_dbg(afe->dev, "%s [%d]%d\n", __func__, dai_id, etdm_data->en_ref_cnt);
	spin_lock_irqsave(&afe_priv->afe_ctrl_lock, flags);
	etdm_data->en_ref_cnt++;
	if (etdm_data->en_ref_cnt == 1) {
		ret = get_etdm_reg(dai_id, &etdm_reg);
		if (ret < 0)
			goto out;

		regmap_set_bits(afe->regmap, etdm_reg.con0, ETDM_CON0_EN);
	}

out:
	spin_unlock_irqrestore(&afe_priv->afe_ctrl_lock, flags);
	return ret;
}

static int mt8188_afe_disable_etdm(struct mtk_base_afe *afe, int dai_id)
{
	struct mt8188_afe_private *afe_priv = afe->platform_priv;
	struct mtk_dai_etdm_priv *etdm_data;
	struct etdm_con_reg etdm_reg;
	unsigned long flags;
	int ret = 0;

	if (!is_valid_etdm_dai(dai_id))
		return -EINVAL;
	etdm_data = afe_priv->dai_priv[dai_id];

	dev_dbg(afe->dev, "%s [%d]%d\n", __func__, dai_id, etdm_data->en_ref_cnt);
	spin_lock_irqsave(&afe_priv->afe_ctrl_lock, flags);
	if (etdm_data->en_ref_cnt > 0) {
		etdm_data->en_ref_cnt--;
		if (etdm_data->en_ref_cnt == 0) {
			ret = get_etdm_reg(dai_id, &etdm_reg);
			if (ret < 0)
				goto out;
			regmap_clear_bits(afe->regmap, etdm_reg.con0,
					  ETDM_CON0_EN);
		}
	}

out:
	spin_unlock_irqrestore(&afe_priv->afe_ctrl_lock, flags);
	return ret;
}

static int etdm_cowork_slv_sel(int id, int slave_mode)
{
	if (slave_mode) {
		switch (id) {
		case MT8188_AFE_IO_ETDM1_IN:
			return COWORK_ETDM_IN1_S;
		case MT8188_AFE_IO_ETDM2_IN:
			return COWORK_ETDM_IN2_S;
		case MT8188_AFE_IO_ETDM1_OUT:
			return COWORK_ETDM_OUT1_S;
		case MT8188_AFE_IO_ETDM2_OUT:
			return COWORK_ETDM_OUT2_S;
		case MT8188_AFE_IO_ETDM3_OUT:
			return COWORK_ETDM_OUT3_S;
		default:
			return -EINVAL;
		}
	} else {
		switch (id) {
		case MT8188_AFE_IO_ETDM1_IN:
			return COWORK_ETDM_IN1_M;
		case MT8188_AFE_IO_ETDM2_IN:
			return COWORK_ETDM_IN2_M;
		case MT8188_AFE_IO_ETDM1_OUT:
			return COWORK_ETDM_OUT1_M;
		case MT8188_AFE_IO_ETDM2_OUT:
			return COWORK_ETDM_OUT2_M;
		case MT8188_AFE_IO_ETDM3_OUT:
			return COWORK_ETDM_OUT3_M;
		default:
			return -EINVAL;
		}
	}
}

static int etdm_cowork_sync_sel(int id)
{
	switch (id) {
	case MT8188_AFE_IO_ETDM1_IN:
		return ETDM_SYNC_FROM_IN1;
	case MT8188_AFE_IO_ETDM2_IN:
		return ETDM_SYNC_FROM_IN2;
	case MT8188_AFE_IO_ETDM1_OUT:
		return ETDM_SYNC_FROM_OUT1;
	case MT8188_AFE_IO_ETDM2_OUT:
		return ETDM_SYNC_FROM_OUT2;
	case MT8188_AFE_IO_ETDM3_OUT:
		return ETDM_SYNC_FROM_OUT3;
	default:
		return -EINVAL;
	}
}

static int mt8188_etdm_sync_mode_slv(struct mtk_base_afe *afe, int dai_id)
{
	struct mt8188_afe_private *afe_priv = afe->platform_priv;
	struct mtk_dai_etdm_priv *etdm_data;
	unsigned int reg = 0;
	unsigned int mask;
	unsigned int val;
	int cowork_source_sel;

	if (!is_valid_etdm_dai(dai_id))
		return -EINVAL;
	etdm_data = afe_priv->dai_priv[dai_id];

	cowork_source_sel = etdm_cowork_slv_sel(etdm_data->cowork_source_id,
						true);
	if (cowork_source_sel < 0)
		return cowork_source_sel;

	switch (dai_id) {
	case MT8188_AFE_IO_ETDM1_IN:
		reg = ETDM_COWORK_CON1;
		mask = ETDM_IN1_SLAVE_SEL_MASK;
		val = FIELD_PREP(ETDM_IN1_SLAVE_SEL_MASK, cowork_source_sel);
		break;
	case MT8188_AFE_IO_ETDM2_IN:
		reg = ETDM_COWORK_CON2;
		mask = ETDM_IN2_SLAVE_SEL_MASK;
		val = FIELD_PREP(ETDM_IN2_SLAVE_SEL_MASK, cowork_source_sel);
		break;
	case MT8188_AFE_IO_ETDM1_OUT:
		reg = ETDM_COWORK_CON0;
		mask = ETDM_OUT1_SLAVE_SEL_MASK;
		val = FIELD_PREP(ETDM_OUT1_SLAVE_SEL_MASK, cowork_source_sel);
		break;
	case MT8188_AFE_IO_ETDM2_OUT:
		reg = ETDM_COWORK_CON2;
		mask = ETDM_OUT2_SLAVE_SEL_MASK;
		val = FIELD_PREP(ETDM_OUT2_SLAVE_SEL_MASK, cowork_source_sel);
		break;
	case MT8188_AFE_IO_ETDM3_OUT:
		reg = ETDM_COWORK_CON2;
		mask = ETDM_OUT3_SLAVE_SEL_MASK;
		val = FIELD_PREP(ETDM_OUT3_SLAVE_SEL_MASK, cowork_source_sel);
		break;
	default:
		return 0;
	}

	regmap_update_bits(afe->regmap, reg, mask, val);

	return 0;
}

static int mt8188_etdm_sync_mode_mst(struct mtk_base_afe *afe, int dai_id)
{
	struct mt8188_afe_private *afe_priv = afe->platform_priv;
	struct mtk_dai_etdm_priv *etdm_data;
	struct etdm_con_reg etdm_reg;
	unsigned int reg = 0;
	unsigned int mask;
	unsigned int val;
	int cowork_source_sel;
	int ret;

	if (!is_valid_etdm_dai(dai_id))
		return -EINVAL;
	etdm_data = afe_priv->dai_priv[dai_id];

	cowork_source_sel = etdm_cowork_sync_sel(etdm_data->cowork_source_id);
	if (cowork_source_sel < 0)
		return cowork_source_sel;

	switch (dai_id) {
	case MT8188_AFE_IO_ETDM1_IN:
		reg = ETDM_COWORK_CON1;
		mask = ETDM_IN1_SYNC_SEL_MASK;
		val = FIELD_PREP(ETDM_IN1_SYNC_SEL_MASK, cowork_source_sel);
		break;
	case MT8188_AFE_IO_ETDM2_IN:
		reg = ETDM_COWORK_CON2;
		mask = ETDM_IN2_SYNC_SEL_MASK;
		val = FIELD_PREP(ETDM_IN2_SYNC_SEL_MASK, cowork_source_sel);
		break;
	case MT8188_AFE_IO_ETDM1_OUT:
		reg = ETDM_COWORK_CON0;
		mask = ETDM_OUT1_SYNC_SEL_MASK;
		val = FIELD_PREP(ETDM_OUT1_SYNC_SEL_MASK, cowork_source_sel);
		break;
	case MT8188_AFE_IO_ETDM2_OUT:
		reg = ETDM_COWORK_CON2;
		mask = ETDM_OUT2_SYNC_SEL_MASK;
		val = FIELD_PREP(ETDM_OUT2_SYNC_SEL_MASK, cowork_source_sel);
		break;
	case MT8188_AFE_IO_ETDM3_OUT:
		reg = ETDM_COWORK_CON2;
		mask = ETDM_OUT3_SYNC_SEL_MASK;
		val = FIELD_PREP(ETDM_OUT3_SYNC_SEL_MASK, cowork_source_sel);
		break;
	default:
		return 0;
	}

	ret = get_etdm_reg(dai_id, &etdm_reg);
	if (ret < 0)
		return ret;

	regmap_update_bits(afe->regmap, reg, mask, val);

	regmap_set_bits(afe->regmap, etdm_reg.con0, ETDM_CON0_SYNC_MODE);

	return 0;
}

static int mt8188_etdm_sync_mode_configure(struct mtk_base_afe *afe, int dai_id)
{
	struct mt8188_afe_private *afe_priv = afe->platform_priv;
	struct mtk_dai_etdm_priv *etdm_data;

	if (!is_valid_etdm_dai(dai_id))
		return -EINVAL;
	etdm_data = afe_priv->dai_priv[dai_id];

	if (etdm_data->cowork_source_id == COWORK_ETDM_NONE)
		return 0;

	if (etdm_data->slave_mode)
		mt8188_etdm_sync_mode_slv(afe, dai_id);
	else
		mt8188_etdm_sync_mode_mst(afe, dai_id);

	return 0;
}

/* dai ops */
static int mtk_dai_etdm_startup(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);
	struct mt8188_afe_private *afe_priv = afe->platform_priv;
	struct mtk_dai_etdm_priv *mst_etdm_data;
	int mst_dai_id;
	int slv_dai_id;
	int cg_id;
	int i;

	if (is_cowork_mode(dai)) {
		mst_dai_id = get_etdm_cowork_master_id(dai);
		if (!is_valid_etdm_dai(mst_dai_id))
			return -EINVAL;
		mtk_dai_etdm_enable_mclk(afe, mst_dai_id);

		cg_id = mtk_dai_etdm_get_cg_id_by_dai_id(mst_dai_id);
		if (cg_id >= 0)
			mt8188_afe_enable_clk(afe, afe_priv->clk[cg_id]);

		mst_etdm_data = afe_priv->dai_priv[mst_dai_id];

		for (i = 0; i < mst_etdm_data->cowork_slv_count; i++) {
			slv_dai_id = mst_etdm_data->cowork_slv_id[i];
			cg_id = mtk_dai_etdm_get_cg_id_by_dai_id(slv_dai_id);
			if (cg_id >= 0)
				mt8188_afe_enable_clk(afe,
						      afe_priv->clk[cg_id]);
		}
	} else {
		mtk_dai_etdm_enable_mclk(afe, dai->id);

		cg_id = mtk_dai_etdm_get_cg_id_by_dai_id(dai->id);
		if (cg_id >= 0)
			mt8188_afe_enable_clk(afe, afe_priv->clk[cg_id]);
	}

	return 0;
}

static void mtk_dai_etdm_shutdown(struct snd_pcm_substream *substream,
				  struct snd_soc_dai *dai)
{
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);
	struct mt8188_afe_private *afe_priv = afe->platform_priv;
	struct mtk_dai_etdm_priv *mst_etdm_data;
	int mst_dai_id;
	int slv_dai_id;
	int cg_id;
	int ret;
	int i;

	if (!is_valid_etdm_dai(dai->id))
		return;
	mst_etdm_data = afe_priv->dai_priv[dai->id];

	dev_dbg(afe->dev, "%s(), dai id %d, prepared %d\n", __func__, dai->id,
		mst_etdm_data->is_prepared);

	if (mst_etdm_data->is_prepared) {
		mst_etdm_data->is_prepared = false;

		if (is_cowork_mode(dai)) {
			mst_dai_id = get_etdm_cowork_master_id(dai);
			if (!is_valid_etdm_dai(mst_dai_id))
				return;
			mst_etdm_data = afe_priv->dai_priv[mst_dai_id];

			ret = mt8188_afe_disable_etdm(afe, mst_dai_id);
			if (ret)
				dev_dbg(afe->dev, "%s disable %d failed\n",
					__func__, mst_dai_id);

			for (i = 0; i < mst_etdm_data->cowork_slv_count; i++) {
				slv_dai_id = mst_etdm_data->cowork_slv_id[i];
				ret = mt8188_afe_disable_etdm(afe, slv_dai_id);
				if (ret)
					dev_dbg(afe->dev, "%s disable %d failed\n",
						__func__, slv_dai_id);
			}
		} else {
			ret = mt8188_afe_disable_etdm(afe, dai->id);
			if (ret)
				dev_dbg(afe->dev, "%s disable %d failed\n",
					__func__, dai->id);
		}
	}

	if (is_cowork_mode(dai)) {
		mst_dai_id = get_etdm_cowork_master_id(dai);
		if (!is_valid_etdm_dai(mst_dai_id))
			return;
		cg_id = mtk_dai_etdm_get_cg_id_by_dai_id(mst_dai_id);
		if (cg_id >= 0)
			mt8188_afe_disable_clk(afe, afe_priv->clk[cg_id]);

		mst_etdm_data = afe_priv->dai_priv[mst_dai_id];
		for (i = 0; i < mst_etdm_data->cowork_slv_count; i++) {
			slv_dai_id = mst_etdm_data->cowork_slv_id[i];
			cg_id = mtk_dai_etdm_get_cg_id_by_dai_id(slv_dai_id);
			if (cg_id >= 0)
				mt8188_afe_disable_clk(afe,
						       afe_priv->clk[cg_id]);
		}
		mtk_dai_etdm_disable_mclk(afe, mst_dai_id);
	} else {
		cg_id = mtk_dai_etdm_get_cg_id_by_dai_id(dai->id);
		if (cg_id >= 0)
			mt8188_afe_disable_clk(afe, afe_priv->clk[cg_id]);

		mtk_dai_etdm_disable_mclk(afe, dai->id);
	}
}

static int mtk_dai_etdm_fifo_mode(struct mtk_base_afe *afe,
				  int dai_id, unsigned int rate)
{
	unsigned int mode = 0;
	unsigned int reg = 0;
	unsigned int val = 0;
	unsigned int mask = (ETDM_IN_AFIFO_MODE_MASK | ETDM_IN_USE_AFIFO);

	if (rate != 0)
		mode = mt8188_afe_fs_timing(rate);

	switch (dai_id) {
	case MT8188_AFE_IO_ETDM1_IN:
		reg = ETDM_IN1_AFIFO_CON;
		if (rate == 0)
			mode = MT8188_ETDM_IN1_1X_EN;
		break;
	case MT8188_AFE_IO_ETDM2_IN:
		reg = ETDM_IN2_AFIFO_CON;
		if (rate == 0)
			mode = MT8188_ETDM_IN2_1X_EN;
		break;
	default:
		return -EINVAL;
	}

	val = (mode | ETDM_IN_USE_AFIFO);

	regmap_update_bits(afe->regmap, reg, mask, val);
	return 0;
}

static int mtk_dai_etdm_in_configure(struct mtk_base_afe *afe,
				     unsigned int rate,
				     unsigned int channels,
				     int dai_id)
{
	struct mt8188_afe_private *afe_priv = afe->platform_priv;
	struct mtk_dai_etdm_priv *etdm_data;
	struct etdm_con_reg etdm_reg;
	bool slave_mode;
	unsigned int data_mode;
	unsigned int lrck_width;
	unsigned int val = 0;
	unsigned int mask = 0;
	int ret;
	int i;

	if (!is_valid_etdm_dai(dai_id))
		return -EINVAL;
	etdm_data = afe_priv->dai_priv[dai_id];
	slave_mode = etdm_data->slave_mode;
	data_mode = etdm_data->data_mode;
	lrck_width = etdm_data->lrck_width;

	dev_dbg(afe->dev, "%s rate %u channels %u, id %d\n",
		__func__, rate, channels, dai_id);

	ret = get_etdm_reg(dai_id, &etdm_reg);
	if (ret < 0)
		return ret;

	/* afifo */
	if (slave_mode)
		mtk_dai_etdm_fifo_mode(afe, dai_id, 0);
	else
		mtk_dai_etdm_fifo_mode(afe, dai_id, rate);

	/* con1 */
	if (lrck_width > 0) {
		mask |= (ETDM_IN_CON1_LRCK_AUTO_MODE |
			ETDM_IN_CON1_LRCK_WIDTH_MASK);
		val |= FIELD_PREP(ETDM_IN_CON1_LRCK_WIDTH_MASK, lrck_width - 1);
	}
	regmap_update_bits(afe->regmap, etdm_reg.con1, mask, val);

	mask = 0;
	val = 0;

	/* con2 */
	if (!slave_mode) {
		mask |= ETDM_IN_CON2_UPDATE_GAP_MASK;
		if (rate == 352800 || rate == 384000)
			val |= FIELD_PREP(ETDM_IN_CON2_UPDATE_GAP_MASK, 4);
		else
			val |= FIELD_PREP(ETDM_IN_CON2_UPDATE_GAP_MASK, 3);
	}
	mask |= (ETDM_IN_CON2_MULTI_IP_2CH_MODE |
		ETDM_IN_CON2_MULTI_IP_TOTAL_CH_MASK);
	if (data_mode == MTK_DAI_ETDM_DATA_MULTI_PIN) {
		val |= ETDM_IN_CON2_MULTI_IP_2CH_MODE |
		       FIELD_PREP(ETDM_IN_CON2_MULTI_IP_TOTAL_CH_MASK, channels - 1);
	}
	regmap_update_bits(afe->regmap, etdm_reg.con2, mask, val);

	mask = 0;
	val = 0;

	/* con3 */
	mask |= ETDM_IN_CON3_DISABLE_OUT_MASK;
	for (i = 0; i < channels; i += 2) {
		if (etdm_data->in_disable_ch[i] &&
		    etdm_data->in_disable_ch[i + 1])
			val |= ETDM_IN_CON3_DISABLE_OUT(i >> 1);
	}
	if (!slave_mode) {
		mask |= ETDM_IN_CON3_FS_MASK;
		val |= FIELD_PREP(ETDM_IN_CON3_FS_MASK, get_etdm_fs_timing(rate));
	}
	regmap_update_bits(afe->regmap, etdm_reg.con3, mask, val);

	mask = 0;
	val = 0;

	/* con4 */
	mask |= (ETDM_IN_CON4_MASTER_LRCK_INV | ETDM_IN_CON4_MASTER_BCK_INV |
		ETDM_IN_CON4_SLAVE_LRCK_INV | ETDM_IN_CON4_SLAVE_BCK_INV);
	if (slave_mode) {
		if (etdm_data->lrck_inv)
			val |= ETDM_IN_CON4_SLAVE_LRCK_INV;
		if (etdm_data->bck_inv)
			val |= ETDM_IN_CON4_SLAVE_BCK_INV;
	} else {
		if (etdm_data->lrck_inv)
			val |= ETDM_IN_CON4_MASTER_LRCK_INV;
		if (etdm_data->bck_inv)
			val |= ETDM_IN_CON4_MASTER_BCK_INV;
	}
	regmap_update_bits(afe->regmap, etdm_reg.con4, mask, val);

	mask = 0;
	val = 0;

	/* con5 */
	mask |= ETDM_IN_CON5_LR_SWAP_MASK;
	mask |= ETDM_IN_CON5_ENABLE_ODD_MASK;
	for (i = 0; i < channels; i += 2) {
		if (etdm_data->in_disable_ch[i] &&
		    !etdm_data->in_disable_ch[i + 1]) {
			val |= ETDM_IN_CON5_LR_SWAP(i >> 1);
			val |= ETDM_IN_CON5_ENABLE_ODD(i >> 1);
		} else if (!etdm_data->in_disable_ch[i] &&
			   etdm_data->in_disable_ch[i + 1]) {
			val |= ETDM_IN_CON5_ENABLE_ODD(i >> 1);
		}
	}
	regmap_update_bits(afe->regmap, etdm_reg.con5, mask, val);
	return 0;
}

static int mtk_dai_etdm_out_configure(struct mtk_base_afe *afe,
				      unsigned int rate,
				      unsigned int channels,
				      int dai_id)
{
	struct mt8188_afe_private *afe_priv = afe->platform_priv;
	struct mtk_dai_etdm_priv *etdm_data;
	struct etdm_con_reg etdm_reg;
	bool slave_mode;
	unsigned int lrck_width;
	unsigned int val = 0;
	unsigned int mask = 0;
	int fs = 0;
	int ret;

	if (!is_valid_etdm_dai(dai_id))
		return -EINVAL;
	etdm_data = afe_priv->dai_priv[dai_id];
	slave_mode = etdm_data->slave_mode;
	lrck_width = etdm_data->lrck_width;

	dev_dbg(afe->dev, "%s rate %u channels %u, id %d\n",
		__func__, rate, channels, dai_id);

	ret = get_etdm_reg(dai_id, &etdm_reg);
	if (ret < 0)
		return ret;

	/* con0 */
	mask = ETDM_OUT_CON0_RELATCH_DOMAIN_MASK;
	val = FIELD_PREP(ETDM_OUT_CON0_RELATCH_DOMAIN_MASK,
			 ETDM_RELATCH_TIMING_A1A2SYS);
	regmap_update_bits(afe->regmap, etdm_reg.con0, mask, val);

	mask = 0;
	val = 0;

	/* con1 */
	if (lrck_width > 0) {
		mask |= (ETDM_OUT_CON1_LRCK_AUTO_MODE |
			ETDM_OUT_CON1_LRCK_WIDTH_MASK);
		val |= FIELD_PREP(ETDM_OUT_CON1_LRCK_WIDTH_MASK, lrck_width - 1);
	}
	regmap_update_bits(afe->regmap, etdm_reg.con1, mask, val);

	mask = 0;
	val = 0;

	if (!slave_mode) {
		/* con4 */
		mask |= ETDM_OUT_CON4_FS_MASK;
		val |= FIELD_PREP(ETDM_OUT_CON4_FS_MASK, get_etdm_fs_timing(rate));
	}

	mask |= ETDM_OUT_CON4_RELATCH_EN_MASK;
	if (dai_id == MT8188_AFE_IO_ETDM1_OUT)
		fs = MT8188_ETDM_OUT1_1X_EN;
	else if (dai_id == MT8188_AFE_IO_ETDM2_OUT)
		fs = MT8188_ETDM_OUT2_1X_EN;

	val |= FIELD_PREP(ETDM_OUT_CON4_RELATCH_EN_MASK, fs);

	regmap_update_bits(afe->regmap, etdm_reg.con4, mask, val);

	mask = 0;
	val = 0;

	/* con5 */
	mask |= (ETDM_OUT_CON5_MASTER_LRCK_INV | ETDM_OUT_CON5_MASTER_BCK_INV |
		ETDM_OUT_CON5_SLAVE_LRCK_INV | ETDM_OUT_CON5_SLAVE_BCK_INV);
	if (slave_mode) {
		if (etdm_data->lrck_inv)
			val |= ETDM_OUT_CON5_SLAVE_LRCK_INV;
		if (etdm_data->bck_inv)
			val |= ETDM_OUT_CON5_SLAVE_BCK_INV;
	} else {
		if (etdm_data->lrck_inv)
			val |= ETDM_OUT_CON5_MASTER_LRCK_INV;
		if (etdm_data->bck_inv)
			val |= ETDM_OUT_CON5_MASTER_BCK_INV;
	}
	regmap_update_bits(afe->regmap, etdm_reg.con5, mask, val);

	return 0;
}

static int mtk_dai_etdm_mclk_configure(struct mtk_base_afe *afe, int dai_id)
{
	struct mt8188_afe_private *afe_priv = afe->platform_priv;
	struct mtk_dai_etdm_priv *etdm_data;
	struct etdm_con_reg etdm_reg;
	int clk_id = mtk_dai_etdm_get_clk_id_by_dai_id(dai_id);
	int clkdiv_id = mtk_dai_etdm_get_clkdiv_id_by_dai_id(dai_id);
	int apll_clk_id;
	int apll;
	int ret;

	if (clk_id < 0 || clkdiv_id < 0)
		return -EINVAL;

	if (!is_valid_etdm_dai(dai_id))
		return -EINVAL;
	etdm_data = afe_priv->dai_priv[dai_id];

	ret = get_etdm_reg(dai_id, &etdm_reg);
	if (ret < 0)
		return ret;

	if (etdm_data->mclk_dir == SND_SOC_CLOCK_OUT)
		regmap_set_bits(afe->regmap, etdm_reg.con1,
				ETDM_CON1_MCLK_OUTPUT);
	else
		regmap_clear_bits(afe->regmap, etdm_reg.con1,
				  ETDM_CON1_MCLK_OUTPUT);

	if (etdm_data->mclk_freq) {
		apll = etdm_data->mclk_apll;
		apll_clk_id = mt8188_afe_get_mclk_source_clk_id(apll);
		if (apll_clk_id < 0)
			return apll_clk_id;

		/* select apll */
		ret = mt8188_afe_set_clk_parent(afe, afe_priv->clk[clk_id],
						afe_priv->clk[apll_clk_id]);
		if (ret)
			return ret;

		/* set rate */
		ret = mt8188_afe_set_clk_rate(afe, afe_priv->clk[clkdiv_id],
					      etdm_data->mclk_freq);
		if (ret)
			return ret;
	} else {
		if (etdm_data->mclk_dir == SND_SOC_CLOCK_OUT)
			dev_dbg(afe->dev, "%s mclk freq = 0\n", __func__);
	}

	return 0;
}

static int mtk_dai_etdm_configure(struct mtk_base_afe *afe,
				  unsigned int rate,
				  unsigned int channels,
				  unsigned int bit_width,
				  int dai_id)
{
	struct mt8188_afe_private *afe_priv = afe->platform_priv;
	struct mtk_dai_etdm_priv *etdm_data;
	struct etdm_con_reg etdm_reg;
	bool slave_mode;
	unsigned int etdm_channels;
	unsigned int val = 0;
	unsigned int mask = 0;
	unsigned int bck;
	unsigned int wlen = get_etdm_wlen(bit_width);
	int ret;

	if (!is_valid_etdm_dai(dai_id))
		return -EINVAL;
	etdm_data = afe_priv->dai_priv[dai_id];
	slave_mode = etdm_data->slave_mode;

	ret = get_etdm_reg(dai_id, &etdm_reg);
	if (ret < 0)
		return ret;

	dev_dbg(afe->dev, "%s fmt %u data %u lrck %d-%u bck %d, clock %u slv %u\n",
		__func__, etdm_data->format, etdm_data->data_mode,
		etdm_data->lrck_inv, etdm_data->lrck_width, etdm_data->bck_inv,
		etdm_data->clock_mode, etdm_data->slave_mode);
	dev_dbg(afe->dev, "%s rate %u channels %u bitwidth %u, id %d\n",
		__func__, rate, channels, bit_width, dai_id);

	etdm_channels = (etdm_data->data_mode == MTK_DAI_ETDM_DATA_ONE_PIN) ?
			get_etdm_ch_fixup(channels) : 2;

	bck = rate * etdm_channels * wlen;
	if (bck > MT8188_ETDM_NORMAL_MAX_BCK_RATE) {
		dev_err(afe->dev, "%s bck rate %u not support\n",
			__func__, bck);
		return -EINVAL;
	}

	/* con0 */
	mask |= ETDM_CON0_BIT_LEN_MASK;
	val |= FIELD_PREP(ETDM_CON0_BIT_LEN_MASK, bit_width - 1);
	mask |= ETDM_CON0_WORD_LEN_MASK;
	val |= FIELD_PREP(ETDM_CON0_WORD_LEN_MASK, wlen - 1);
	mask |= ETDM_CON0_FORMAT_MASK;
	val |= FIELD_PREP(ETDM_CON0_FORMAT_MASK, etdm_data->format);
	mask |= ETDM_CON0_CH_NUM_MASK;
	val |= FIELD_PREP(ETDM_CON0_CH_NUM_MASK, etdm_channels - 1);

	mask |= ETDM_CON0_SLAVE_MODE;
	if (slave_mode) {
		if (dai_id == MT8188_AFE_IO_ETDM1_OUT) {
			dev_err(afe->dev, "%s id %d only support master mode\n",
				__func__, dai_id);
			return -EINVAL;
		}
		val |= ETDM_CON0_SLAVE_MODE;
	}
	regmap_update_bits(afe->regmap, etdm_reg.con0, mask, val);

	if (get_etdm_dir(dai_id) == ETDM_IN)
		mtk_dai_etdm_in_configure(afe, rate, channels, dai_id);
	else
		mtk_dai_etdm_out_configure(afe, rate, channels, dai_id);

	return 0;
}

static int mtk_dai_etdm_hw_params(struct snd_pcm_substream *substream,
				  struct snd_pcm_hw_params *params,
				  struct snd_soc_dai *dai)
{
	unsigned int rate = params_rate(params);
	unsigned int bit_width = params_width(params);
	unsigned int channels = params_channels(params);
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);
	struct mt8188_afe_private *afe_priv = afe->platform_priv;
	struct mtk_dai_etdm_priv *mst_etdm_data;
	int mst_dai_id;
	int slv_dai_id;
	int ret;
	int i;

	dev_dbg(afe->dev, "%s '%s' period %u-%u\n",
		__func__, snd_pcm_stream_str(substream),
		params_period_size(params), params_periods(params));

	if (is_cowork_mode(dai)) {
		mst_dai_id = get_etdm_cowork_master_id(dai);
		if (!is_valid_etdm_dai(mst_dai_id))
			return -EINVAL;

		ret = mtk_dai_etdm_mclk_configure(afe, mst_dai_id);
		if (ret)
			return ret;

		ret = mtk_dai_etdm_configure(afe, rate, channels,
					     bit_width, mst_dai_id);
		if (ret)
			return ret;

		mst_etdm_data = afe_priv->dai_priv[mst_dai_id];
		for (i = 0; i < mst_etdm_data->cowork_slv_count; i++) {
			slv_dai_id = mst_etdm_data->cowork_slv_id[i];
			ret = mtk_dai_etdm_configure(afe, rate, channels,
						     bit_width, slv_dai_id);
			if (ret)
				return ret;

			ret = mt8188_etdm_sync_mode_configure(afe, slv_dai_id);
			if (ret)
				return ret;
		}
	} else {
		ret = mtk_dai_etdm_mclk_configure(afe, dai->id);
		if (ret)
			return ret;

		ret = mtk_dai_etdm_configure(afe, rate, channels,
					     bit_width, dai->id);
		if (ret)
			return ret;
	}

	return 0;
}

static int mtk_dai_etdm_prepare(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);
	struct mt8188_afe_private *afe_priv = afe->platform_priv;
	struct mtk_dai_etdm_priv *mst_etdm_data;
	int mst_dai_id;
	int slv_dai_id;
	int ret;
	int i;

	if (!is_valid_etdm_dai(dai->id))
		return -EINVAL;
	mst_etdm_data = afe_priv->dai_priv[dai->id];

	dev_dbg(afe->dev, "%s(), dai id %d, prepared %d\n", __func__, dai->id,
		mst_etdm_data->is_prepared);

	if (mst_etdm_data->is_prepared)
		return 0;

	mst_etdm_data->is_prepared = true;

	if (is_cowork_mode(dai)) {
		mst_dai_id = get_etdm_cowork_master_id(dai);
		if (!is_valid_etdm_dai(mst_dai_id))
			return -EINVAL;
		mst_etdm_data = afe_priv->dai_priv[mst_dai_id];

		for (i = 0; i < mst_etdm_data->cowork_slv_count; i++) {
			slv_dai_id = mst_etdm_data->cowork_slv_id[i];
			ret = mt8188_afe_enable_etdm(afe, slv_dai_id);
			if (ret) {
				dev_dbg(afe->dev, "%s enable %d failed\n",
					__func__, slv_dai_id);

				return ret;
			}
		}

		ret = mt8188_afe_enable_etdm(afe, mst_dai_id);
		if (ret) {
			dev_dbg(afe->dev, "%s enable %d failed\n",
				__func__, mst_dai_id);

			return ret;
		}
	} else {
		ret = mt8188_afe_enable_etdm(afe, dai->id);
		if (ret) {
			dev_dbg(afe->dev, "%s enable %d failed\n",
				__func__, dai->id);

			return ret;
		}
	}

	return 0;
}

static int mtk_dai_etdm_cal_mclk(struct mtk_base_afe *afe, int freq, int dai_id)
{
	struct mt8188_afe_private *afe_priv = afe->platform_priv;
	struct mtk_dai_etdm_priv *etdm_data;
	int apll_rate;
	int apll;

	if (!is_valid_etdm_dai(dai_id))
		return -EINVAL;
	etdm_data = afe_priv->dai_priv[dai_id];

	if (freq == 0) {
		etdm_data->mclk_freq = freq;
		return 0;
	}

	if (etdm_data->mclk_fixed_apll == 0)
		apll = mt8188_afe_get_default_mclk_source_by_rate(freq);
	else
		apll = etdm_data->mclk_apll;

	apll_rate = mt8188_afe_get_mclk_source_rate(afe, apll);

	if (freq > apll_rate) {
		dev_err(afe->dev, "freq %d > apll rate %d\n", freq, apll_rate);
		return -EINVAL;
	}

	if (apll_rate % freq != 0) {
		dev_err(afe->dev, "APLL%d cannot generate freq Hz\n", apll);
		return -EINVAL;
	}

	if (etdm_data->mclk_fixed_apll == 0)
		etdm_data->mclk_apll = apll;
	etdm_data->mclk_freq = freq;

	return 0;
}

static int mtk_dai_etdm_set_sysclk(struct snd_soc_dai *dai,
				   int clk_id, unsigned int freq, int dir)
{
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);
	struct mt8188_afe_private *afe_priv = afe->platform_priv;
	struct mtk_dai_etdm_priv *etdm_data;
	int dai_id;

	dev_dbg(dai->dev, "%s id %d freq %u, dir %d\n",
		__func__, dai->id, freq, dir);
	if (is_cowork_mode(dai))
		dai_id = get_etdm_cowork_master_id(dai);
	else
		dai_id = dai->id;

	if (!is_valid_etdm_dai(dai_id))
		return -EINVAL;
	etdm_data = afe_priv->dai_priv[dai_id];
	etdm_data->mclk_dir = dir;
	return mtk_dai_etdm_cal_mclk(afe, freq, dai_id);
}

static int mtk_dai_etdm_set_tdm_slot(struct snd_soc_dai *dai,
				     unsigned int tx_mask, unsigned int rx_mask,
				     int slots, int slot_width)
{
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);
	struct mt8188_afe_private *afe_priv = afe->platform_priv;
	struct mtk_dai_etdm_priv *etdm_data;

	if (!is_valid_etdm_dai(dai->id))
		return -EINVAL;
	etdm_data = afe_priv->dai_priv[dai->id];

	dev_dbg(dai->dev, "%s id %d slot_width %d\n",
		__func__, dai->id, slot_width);

	etdm_data->slots = slots;
	etdm_data->lrck_width = slot_width;
	return 0;
}

static int mtk_dai_etdm_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);
	struct mt8188_afe_private *afe_priv = afe->platform_priv;
	struct mtk_dai_etdm_priv *etdm_data;

	if (!is_valid_etdm_dai(dai->id))
		return -EINVAL;
	etdm_data = afe_priv->dai_priv[dai->id];

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		etdm_data->format = MTK_DAI_ETDM_FORMAT_I2S;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		etdm_data->format = MTK_DAI_ETDM_FORMAT_LJ;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		etdm_data->format = MTK_DAI_ETDM_FORMAT_RJ;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		etdm_data->format = MTK_DAI_ETDM_FORMAT_DSPA;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		etdm_data->format = MTK_DAI_ETDM_FORMAT_DSPB;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		etdm_data->bck_inv = false;
		etdm_data->lrck_inv = false;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		etdm_data->bck_inv = false;
		etdm_data->lrck_inv = true;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		etdm_data->bck_inv = true;
		etdm_data->lrck_inv = false;
		break;
	case SND_SOC_DAIFMT_IB_IF:
		etdm_data->bck_inv = true;
		etdm_data->lrck_inv = true;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) {
	case SND_SOC_DAIFMT_BC_FC:
		etdm_data->slave_mode = true;
		break;
	case SND_SOC_DAIFMT_BP_FP:
		etdm_data->slave_mode = false;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int mtk_dai_hdmitx_dptx_startup(struct snd_pcm_substream *substream,
				       struct snd_soc_dai *dai)
{
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);
	struct mt8188_afe_private *afe_priv = afe->platform_priv;
	int cg_id = mtk_dai_etdm_get_cg_id_by_dai_id(dai->id);

	if (cg_id >= 0)
		mt8188_afe_enable_clk(afe, afe_priv->clk[cg_id]);

	mtk_dai_etdm_enable_mclk(afe, dai->id);

	return 0;
}

static void mtk_dai_hdmitx_dptx_shutdown(struct snd_pcm_substream *substream,
					 struct snd_soc_dai *dai)
{
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);
	struct mt8188_afe_private *afe_priv = afe->platform_priv;
	int cg_id = mtk_dai_etdm_get_cg_id_by_dai_id(dai->id);
	struct mtk_dai_etdm_priv *etdm_data;
	int ret;

	if (!is_valid_etdm_dai(dai->id))
		return;
	etdm_data = afe_priv->dai_priv[dai->id];

	if (etdm_data->is_prepared) {
		etdm_data->is_prepared = false;
		/* disable etdm_out3 */
		ret = mt8188_afe_disable_etdm(afe, dai->id);
		if (ret)
			dev_dbg(afe->dev, "%s disable failed\n", __func__);

		/* disable dptx interface */
		if (dai->id == MT8188_AFE_IO_DPTX)
			regmap_clear_bits(afe->regmap, AFE_DPTX_CON,
					  AFE_DPTX_CON_ON);
	}

	mtk_dai_etdm_disable_mclk(afe, dai->id);

	if (cg_id >= 0)
		mt8188_afe_disable_clk(afe, afe_priv->clk[cg_id]);
}

static unsigned int mtk_dai_get_dptx_ch_en(unsigned int channel)
{
	switch (channel) {
	case 1 ... 2:
		return AFE_DPTX_CON_CH_EN_2CH;
	case 3 ... 4:
		return AFE_DPTX_CON_CH_EN_4CH;
	case 5 ... 6:
		return AFE_DPTX_CON_CH_EN_6CH;
	case 7 ... 8:
		return AFE_DPTX_CON_CH_EN_8CH;
	default:
		return AFE_DPTX_CON_CH_EN_2CH;
	}
}

static unsigned int mtk_dai_get_dptx_ch(unsigned int ch)
{
	return (ch > 2) ?
		AFE_DPTX_CON_CH_NUM_8CH : AFE_DPTX_CON_CH_NUM_2CH;
}

static unsigned int mtk_dai_get_dptx_wlen(snd_pcm_format_t format)
{
	return snd_pcm_format_physical_width(format) <= 16 ?
		AFE_DPTX_CON_16BIT : AFE_DPTX_CON_24BIT;
}

static int mtk_dai_hdmitx_dptx_hw_params(struct snd_pcm_substream *substream,
					 struct snd_pcm_hw_params *params,
					 struct snd_soc_dai *dai)
{
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);
	struct mt8188_afe_private *afe_priv = afe->platform_priv;
	struct mtk_dai_etdm_priv *etdm_data;
	unsigned int rate = params_rate(params);
	unsigned int channels = params_channels(params);
	snd_pcm_format_t format = params_format(params);
	int width = snd_pcm_format_physical_width(format);
	int ret;

	if (!is_valid_etdm_dai(dai->id))
		return -EINVAL;
	etdm_data = afe_priv->dai_priv[dai->id];

	/* dptx configure */
	if (dai->id == MT8188_AFE_IO_DPTX) {
		regmap_update_bits(afe->regmap, AFE_DPTX_CON,
				   AFE_DPTX_CON_CH_EN_MASK,
				   mtk_dai_get_dptx_ch_en(channels));
		regmap_update_bits(afe->regmap, AFE_DPTX_CON,
				   AFE_DPTX_CON_CH_NUM_MASK,
				   mtk_dai_get_dptx_ch(channels));
		regmap_update_bits(afe->regmap, AFE_DPTX_CON,
				   AFE_DPTX_CON_16BIT_MASK,
				   mtk_dai_get_dptx_wlen(format));

		if (mtk_dai_get_dptx_ch(channels) == AFE_DPTX_CON_CH_NUM_8CH) {
			etdm_data->data_mode = MTK_DAI_ETDM_DATA_ONE_PIN;
			channels = 8;
		} else {
			channels = 2;
		}
	} else {
		etdm_data->data_mode = MTK_DAI_ETDM_DATA_MULTI_PIN;
	}

	ret = mtk_dai_etdm_mclk_configure(afe, dai->id);
	if (ret)
		return ret;

	ret = mtk_dai_etdm_configure(afe, rate, channels, width, dai->id);

	return ret;
}

static int mtk_dai_hdmitx_dptx_prepare(struct snd_pcm_substream *substream,
				       struct snd_soc_dai *dai)
{
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);
	struct mt8188_afe_private *afe_priv = afe->platform_priv;
	struct mtk_dai_etdm_priv *etdm_data;

	if (!is_valid_etdm_dai(dai->id))
		return -EINVAL;
	etdm_data = afe_priv->dai_priv[dai->id];

	dev_dbg(afe->dev, "%s(), dai id %d, prepared %d\n", __func__, dai->id,
		etdm_data->is_prepared);

	if (etdm_data->is_prepared)
		return 0;

	etdm_data->is_prepared = true;

	/* enable dptx interface */
	if (dai->id == MT8188_AFE_IO_DPTX)
		regmap_set_bits(afe->regmap, AFE_DPTX_CON, AFE_DPTX_CON_ON);

	/* enable etdm_out3 */
	return mt8188_afe_enable_etdm(afe, dai->id);
}

static int mtk_dai_hdmitx_dptx_set_sysclk(struct snd_soc_dai *dai,
					  int clk_id,
					  unsigned int freq,
					  int dir)
{
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);
	struct mt8188_afe_private *afe_priv = afe->platform_priv;
	struct mtk_dai_etdm_priv *etdm_data;

	if (!is_valid_etdm_dai(dai->id))
		return -EINVAL;
	etdm_data = afe_priv->dai_priv[dai->id];

	dev_dbg(dai->dev, "%s id %d freq %u, dir %d\n",
		__func__, dai->id, freq, dir);

	etdm_data->mclk_dir = dir;
	return mtk_dai_etdm_cal_mclk(afe, freq, dai->id);
}

static const struct snd_soc_dai_ops mtk_dai_etdm_ops = {
	.startup = mtk_dai_etdm_startup,
	.shutdown = mtk_dai_etdm_shutdown,
	.hw_params = mtk_dai_etdm_hw_params,
	.prepare = mtk_dai_etdm_prepare,
	.set_sysclk = mtk_dai_etdm_set_sysclk,
	.set_fmt = mtk_dai_etdm_set_fmt,
	.set_tdm_slot = mtk_dai_etdm_set_tdm_slot,
};

static const struct snd_soc_dai_ops mtk_dai_hdmitx_dptx_ops = {
	.startup	= mtk_dai_hdmitx_dptx_startup,
	.shutdown	= mtk_dai_hdmitx_dptx_shutdown,
	.hw_params	= mtk_dai_hdmitx_dptx_hw_params,
	.prepare	= mtk_dai_hdmitx_dptx_prepare,
	.set_sysclk	= mtk_dai_hdmitx_dptx_set_sysclk,
	.set_fmt	= mtk_dai_etdm_set_fmt,
};

/* dai driver */
#define MTK_ETDM_RATES (SNDRV_PCM_RATE_8000_192000)

#define MTK_ETDM_FORMATS (SNDRV_PCM_FMTBIT_S16_LE |\
			  SNDRV_PCM_FMTBIT_S24_LE |\
			  SNDRV_PCM_FMTBIT_S32_LE)

static struct snd_soc_dai_driver mtk_dai_etdm_driver[] = {
	{
		.name = "DPTX",
		.id = MT8188_AFE_IO_DPTX,
		.playback = {
			.stream_name = "DPTX",
			.channels_min = 1,
			.channels_max = 8,
			.rates = MTK_ETDM_RATES,
			.formats = MTK_ETDM_FORMATS,
		},
		.ops = &mtk_dai_hdmitx_dptx_ops,
	},
	{
		.name = "ETDM1_IN",
		.id = MT8188_AFE_IO_ETDM1_IN,
		.capture = {
			.stream_name = "ETDM1_IN",
			.channels_min = 1,
			.channels_max = 16,
			.rates = MTK_ETDM_RATES,
			.formats = MTK_ETDM_FORMATS,
		},
		.ops = &mtk_dai_etdm_ops,
	},
	{
		.name = "ETDM2_IN",
		.id = MT8188_AFE_IO_ETDM2_IN,
		.capture = {
			.stream_name = "ETDM2_IN",
			.channels_min = 1,
			.channels_max = 16,
			.rates = MTK_ETDM_RATES,
			.formats = MTK_ETDM_FORMATS,
		},
		.ops = &mtk_dai_etdm_ops,
	},
	{
		.name = "ETDM1_OUT",
		.id = MT8188_AFE_IO_ETDM1_OUT,
		.playback = {
			.stream_name = "ETDM1_OUT",
			.channels_min = 1,
			.channels_max = 16,
			.rates = MTK_ETDM_RATES,
			.formats = MTK_ETDM_FORMATS,
		},
		.ops = &mtk_dai_etdm_ops,
	},
	{
		.name = "ETDM2_OUT",
		.id = MT8188_AFE_IO_ETDM2_OUT,
		.playback = {
			.stream_name = "ETDM2_OUT",
			.channels_min = 1,
			.channels_max = 16,
			.rates = MTK_ETDM_RATES,
			.formats = MTK_ETDM_FORMATS,
		},
		.ops = &mtk_dai_etdm_ops,
	},
	{
		.name = "ETDM3_OUT",
		.id = MT8188_AFE_IO_ETDM3_OUT,
		.playback = {
			.stream_name = "ETDM3_OUT",
			.channels_min = 1,
			.channels_max = 8,
			.rates = MTK_ETDM_RATES,
			.formats = MTK_ETDM_FORMATS,
		},
		.ops = &mtk_dai_hdmitx_dptx_ops,
	},
};

static void mt8188_etdm_update_sync_info(struct mtk_base_afe *afe)
{
	struct mt8188_afe_private *afe_priv = afe->platform_priv;
	struct mtk_dai_etdm_priv *etdm_data;
	struct mtk_dai_etdm_priv *mst_data;
	int mst_dai_id;
	int i;

	for (i = MT8188_AFE_IO_ETDM_START; i < MT8188_AFE_IO_ETDM_END; i++) {
		etdm_data = afe_priv->dai_priv[i];
		if (etdm_data->cowork_source_id != COWORK_ETDM_NONE) {
			mst_dai_id = etdm_data->cowork_source_id;
			mst_data = afe_priv->dai_priv[mst_dai_id];
			if (mst_data->cowork_source_id != COWORK_ETDM_NONE)
				dev_err(afe->dev, "%s [%d] wrong sync source\n",
					__func__, i);
			mst_data->cowork_slv_id[mst_data->cowork_slv_count] = i;
			mst_data->cowork_slv_count++;
		}
	}
}

static void mt8188_dai_etdm_parse_of(struct mtk_base_afe *afe)
{
	const struct device_node *of_node = afe->dev->of_node;
	struct mt8188_afe_private *afe_priv = afe->platform_priv;
	struct mtk_dai_etdm_priv *etdm_data;
	char prop[48];
	u8 disable_chn[MT8188_ETDM_MAX_CHANNELS];
	int max_chn = MT8188_ETDM_MAX_CHANNELS;
	unsigned int sync_id;
	u32 sel;
	int ret;
	int dai_id;
	int i, j;
	struct {
		const char *name;
		const unsigned int sync_id;
	} of_afe_etdms[MT8188_AFE_IO_ETDM_NUM] = {
		{"etdm-in1", ETDM_SYNC_FROM_IN1},
		{"etdm-in2", ETDM_SYNC_FROM_IN2},
		{"etdm-out1", ETDM_SYNC_FROM_OUT1},
		{"etdm-out2", ETDM_SYNC_FROM_OUT2},
		{"etdm-out3", ETDM_SYNC_FROM_OUT3},
	};

	for (i = 0; i < MT8188_AFE_IO_ETDM_NUM; i++) {
		dai_id = ETDM_TO_DAI_ID(i);
		etdm_data = afe_priv->dai_priv[dai_id];

		snprintf(prop, sizeof(prop), "mediatek,%s-multi-pin-mode",
			 of_afe_etdms[i].name);

		etdm_data->data_mode = of_property_read_bool(of_node, prop);

		snprintf(prop, sizeof(prop), "mediatek,%s-cowork-source",
			 of_afe_etdms[i].name);

		ret = of_property_read_u32(of_node, prop, &sel);
		if (ret == 0) {
			if (sel >= MT8188_AFE_IO_ETDM_NUM) {
				dev_err(afe->dev, "%s invalid id=%d\n",
					__func__, sel);
				etdm_data->cowork_source_id = COWORK_ETDM_NONE;
			} else {
				sync_id = of_afe_etdms[sel].sync_id;
				etdm_data->cowork_source_id =
					sync_to_dai_id(sync_id);
			}
		} else {
			etdm_data->cowork_source_id = COWORK_ETDM_NONE;
		}
	}

	/* etdm in only */
	for (i = 0; i < 2; i++) {
		snprintf(prop, sizeof(prop), "mediatek,%s-chn-disabled",
			 of_afe_etdms[i].name);

		ret = of_property_read_variable_u8_array(of_node, prop,
							 disable_chn,
							 1, max_chn);
		if (ret < 0)
			continue;

		for (j = 0; j < ret; j++) {
			if (disable_chn[j] >= MT8188_ETDM_MAX_CHANNELS)
				dev_err(afe->dev, "%s [%d] invalid chn %u\n",
					__func__, j, disable_chn[j]);
			else
				etdm_data->in_disable_ch[disable_chn[j]] = true;
		}
	}
	mt8188_etdm_update_sync_info(afe);
}

static int init_etdm_priv_data(struct mtk_base_afe *afe)
{
	struct mt8188_afe_private *afe_priv = afe->platform_priv;
	struct mtk_dai_etdm_priv *etdm_priv;
	int i;

	for (i = MT8188_AFE_IO_ETDM_START; i < MT8188_AFE_IO_ETDM_END; i++) {
		etdm_priv = devm_kzalloc(afe->dev,
					 sizeof(struct mtk_dai_etdm_priv),
					 GFP_KERNEL);
		if (!etdm_priv)
			return -ENOMEM;

		afe_priv->dai_priv[i] = etdm_priv;
	}

	afe_priv->dai_priv[MT8188_AFE_IO_DPTX] =
		afe_priv->dai_priv[MT8188_AFE_IO_ETDM3_OUT];

	mt8188_dai_etdm_parse_of(afe);
	return 0;
}

int mt8188_dai_etdm_register(struct mtk_base_afe *afe)
{
	struct mtk_base_afe_dai *dai;

	dai = devm_kzalloc(afe->dev, sizeof(*dai), GFP_KERNEL);
	if (!dai)
		return -ENOMEM;

	list_add(&dai->list, &afe->sub_dais);

	dai->dai_drivers = mtk_dai_etdm_driver;
	dai->num_dai_drivers = ARRAY_SIZE(mtk_dai_etdm_driver);

	dai->dapm_widgets = mtk_dai_etdm_widgets;
	dai->num_dapm_widgets = ARRAY_SIZE(mtk_dai_etdm_widgets);
	dai->dapm_routes = mtk_dai_etdm_routes;
	dai->num_dapm_routes = ARRAY_SIZE(mtk_dai_etdm_routes);
	dai->controls = mtk_dai_etdm_controls;
	dai->num_controls = ARRAY_SIZE(mtk_dai_etdm_controls);

	return init_etdm_priv_data(afe);
}
