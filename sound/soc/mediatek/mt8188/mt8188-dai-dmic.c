// SPDX-License-Identifier: GPL-2.0
/*
 * MediaTek ALSA SoC Audio DAI DMIC I/F Control
 *
 * Copyright (c) 2020 MediaTek Inc.
 * Author: Bicycle Tsai <bicycle.tsai@mediatek.com>
 *         Trevor Wu <trevor.wu@mediatek.com>
 *         Parker Yang <parker.yang@mediatek.com>
 */

#include <linux/delay.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <sound/pcm_params.h>
#include "mt8188-afe-clk.h"
#include "mt8188-afe-common.h"
#include "mt8188-reg.h"

/* DMIC HW Gain configuration maximum value. */
#define DMIC_GAIN_MAX_STEP	GENMASK(19, 0)
#define DMIC_GAIN_MAX_PER_STEP	GENMASK(7, 0)
#define DMIC_GAIN_MAX_TARGET	GENMASK(27, 0)
#define DMIC_GAIN_MAX_CURRENT	GENMASK(27, 0)

#define CLK_PHASE_SEL_CH1 0
#define CLK_PHASE_SEL_CH2 ((CLK_PHASE_SEL_CH1) + 4)

#define DMIC1_SRC_SEL 0
#define DMIC2_SRC_SEL 0
#define DMIC3_SRC_SEL 2
#define DMIC4_SRC_SEL 0
#define DMIC5_SRC_SEL 4
#define DMIC6_SRC_SEL 0
#define DMIC7_SRC_SEL 6
#define DMIC8_SRC_SEL 0

enum {
	SUPPLY_SEQ_DMIC_GAIN,
	SUPPLY_SEQ_DMIC_CK,
};

enum {
	DMIC0,
	DMIC1,
	DMIC2,
	DMIC3,
	DMIC_NUM,
};

struct mtk_dai_dmic_ctrl_reg {
	unsigned int con0;
};

struct mtk_dai_dmic_hw_gain_ctrl_reg {
	unsigned int bypass;
	unsigned int con0;
};

struct mtk_dai_dmic_priv {
	unsigned int gain_on[DMIC_NUM];
	unsigned int channels;
	bool hires_required;
};

static const struct mtk_dai_dmic_ctrl_reg dmic_ctrl_regs[DMIC_NUM] = {
	[DMIC0] = {
		.con0 = AFE_DMIC0_UL_SRC_CON0,
	},
	[DMIC1] = {
		.con0 = AFE_DMIC1_UL_SRC_CON0,
	},
	[DMIC2] = {
		.con0 = AFE_DMIC2_UL_SRC_CON0,
	},
	[DMIC3] = {
		.con0 = AFE_DMIC3_UL_SRC_CON0,
	},
};

static const struct mtk_dai_dmic_ctrl_reg *get_dmic_ctrl_reg(int id)
{
	if (id < 0 || id >= DMIC_NUM)
		return NULL;

	return &dmic_ctrl_regs[id];
}

static const struct mtk_dai_dmic_hw_gain_ctrl_reg
	dmic_hw_gain_ctrl_regs[DMIC_NUM] = {
	[DMIC0] = {
		.bypass = DMIC_BYPASS_HW_GAIN,
		.con0 = DMIC_GAIN1_CON0,
	},
	[DMIC1] = {
		.bypass = DMIC_BYPASS_HW_GAIN,
		.con0 = DMIC_GAIN2_CON0,
	},
	[DMIC2] = {
		.bypass = DMIC_BYPASS_HW_GAIN,
		.con0 = DMIC_GAIN3_CON0,
	},
	[DMIC3] = {
		.bypass = DMIC_BYPASS_HW_GAIN,
		.con0 = DMIC_GAIN4_CON0,
	},
};

static const struct mtk_dai_dmic_hw_gain_ctrl_reg
	*get_dmic_hw_gain_ctrl_reg(struct mtk_base_afe *afe, int id)
{
	if ((id < 0) || (id >= DMIC_NUM)) {
		dev_dbg(afe->dev, "%s invalid id\n", __func__);
		return NULL;
	}

	return &dmic_hw_gain_ctrl_regs[id];
}

static void mtk_dai_dmic_hw_gain_bypass(struct mtk_base_afe *afe,
					unsigned int id, bool bypass)
{
	const struct mtk_dai_dmic_hw_gain_ctrl_reg *reg;
	unsigned int msk;

	reg = get_dmic_hw_gain_ctrl_reg(afe, id);
	if (!reg)
		return;

	switch (id) {
	case DMIC0:
		msk = DMIC_BYPASS_HW_GAIN_DMIC1_BYPASS;
		break;
	case DMIC1:
		msk = DMIC_BYPASS_HW_GAIN_DMIC2_BYPASS;
		break;
	case DMIC2:
		msk = DMIC_BYPASS_HW_GAIN_DMIC3_BYPASS;
		break;
	case DMIC3:
		msk = DMIC_BYPASS_HW_GAIN_DMIC4_BYPASS;
		break;
	default:
		return;
	}

	if (bypass)
		regmap_set_bits(afe->regmap, reg->bypass, msk);
	else
		regmap_clear_bits(afe->regmap, reg->bypass, msk);
}

static void mtk_dai_dmic_hw_gain_on(struct mtk_base_afe *afe, unsigned int id,
				    bool on)
{
	const struct mtk_dai_dmic_hw_gain_ctrl_reg *reg = get_dmic_hw_gain_ctrl_reg(afe, id);

	if (!reg)
		return;

	if (on)
		regmap_set_bits(afe->regmap, reg->con0, DMIC_GAIN_CON0_GAIN_ON);
	else
		regmap_clear_bits(afe->regmap, reg->con0, DMIC_GAIN_CON0_GAIN_ON);
}

static const struct reg_sequence mtk_dai_dmic_iir_coeff_reg_defaults[] = {
	{ AFE_DMIC0_IIR_COEF_02_01, 0x00000000 },
	{ AFE_DMIC0_IIR_COEF_04_03, 0x00003FB8 },
	{ AFE_DMIC0_IIR_COEF_06_05, 0x3FB80000 },
	{ AFE_DMIC0_IIR_COEF_08_07, 0x3FB80000 },
	{ AFE_DMIC0_IIR_COEF_10_09, 0x0000C048 },
	{ AFE_DMIC1_IIR_COEF_02_01, 0x00000000 },
	{ AFE_DMIC1_IIR_COEF_04_03, 0x00003FB8 },
	{ AFE_DMIC1_IIR_COEF_06_05, 0x3FB80000 },
	{ AFE_DMIC1_IIR_COEF_08_07, 0x3FB80000 },
	{ AFE_DMIC1_IIR_COEF_10_09, 0x0000C048 },
	{ AFE_DMIC2_IIR_COEF_02_01, 0x00000000 },
	{ AFE_DMIC2_IIR_COEF_04_03, 0x00003FB8 },
	{ AFE_DMIC2_IIR_COEF_06_05, 0x3FB80000 },
	{ AFE_DMIC2_IIR_COEF_08_07, 0x3FB80000 },
	{ AFE_DMIC2_IIR_COEF_10_09, 0x0000C048 },
	{ AFE_DMIC3_IIR_COEF_02_01, 0x00000000 },
	{ AFE_DMIC3_IIR_COEF_04_03, 0x00003FB8 },
	{ AFE_DMIC3_IIR_COEF_06_05, 0x3FB80000 },
	{ AFE_DMIC3_IIR_COEF_08_07, 0x3FB80000 },
	{ AFE_DMIC3_IIR_COEF_10_09, 0x0000C048 },
};

static int mtk_dai_dmic_load_iir_coeff_table(struct mtk_base_afe *afe)
{
	return regmap_multi_reg_write(afe->regmap,
				      mtk_dai_dmic_iir_coeff_reg_defaults,
				      ARRAY_SIZE(mtk_dai_dmic_iir_coeff_reg_defaults));
}

static int mtk_dai_dmic_configure_array(struct snd_soc_dai *dai)
{
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);
	const u32 mask = PWR2_TOP_CON_DMIC8_SRC_SEL_MASK |
			 PWR2_TOP_CON_DMIC7_SRC_SEL_MASK |
			 PWR2_TOP_CON_DMIC6_SRC_SEL_MASK |
			 PWR2_TOP_CON_DMIC5_SRC_SEL_MASK |
			 PWR2_TOP_CON_DMIC4_SRC_SEL_MASK |
			 PWR2_TOP_CON_DMIC3_SRC_SEL_MASK |
			 PWR2_TOP_CON_DMIC2_SRC_SEL_MASK |
			 PWR2_TOP_CON_DMIC1_SRC_SEL_MASK;
	const u32 val = PWR2_TOP_CON_DMIC8_SRC_SEL_VAL(DMIC8_SRC_SEL) |
			PWR2_TOP_CON_DMIC7_SRC_SEL_VAL(DMIC7_SRC_SEL) |
			PWR2_TOP_CON_DMIC6_SRC_SEL_VAL(DMIC6_SRC_SEL) |
			PWR2_TOP_CON_DMIC5_SRC_SEL_VAL(DMIC5_SRC_SEL) |
			PWR2_TOP_CON_DMIC4_SRC_SEL_VAL(DMIC4_SRC_SEL) |
			PWR2_TOP_CON_DMIC3_SRC_SEL_VAL(DMIC3_SRC_SEL) |
			PWR2_TOP_CON_DMIC2_SRC_SEL_VAL(DMIC2_SRC_SEL) |
			PWR2_TOP_CON_DMIC1_SRC_SEL_VAL(DMIC1_SRC_SEL);

	return regmap_update_bits(afe->regmap, PWR2_TOP_CON0, mask, val);
}

/* This function assumes that the caller checked that channels is valid */
static u8 mtk_dmic_channels_to_dmic_number(unsigned int channels)
{
	switch (channels) {
	case 1:
		return DMIC0;
	case 2:
		return DMIC1;
	case 3:
		return DMIC2;
	case 4:
	default:
		return DMIC3;
	}
}

static void mtk_dai_dmic_hw_gain_enable(struct mtk_base_afe *afe,
					unsigned int channels, bool enable)
{
	struct mt8188_afe_private *afe_priv = afe->platform_priv;
	struct mtk_dai_dmic_priv *dmic_priv = afe_priv->dai_priv[MT8188_AFE_IO_DMIC_IN];
	u8 dmic_num;
	int i;

	dmic_num = mtk_dmic_channels_to_dmic_number(channels);
	for (i = dmic_num; i >= DMIC0; i--) {
		if (enable && dmic_priv->gain_on[i]) {
			mtk_dai_dmic_hw_gain_bypass(afe, i, false);
			mtk_dai_dmic_hw_gain_on(afe, i, true);
		} else {
			mtk_dai_dmic_hw_gain_on(afe, i, false);
			mtk_dai_dmic_hw_gain_bypass(afe, i, true);
		}
	}
}

static int mtk_dmic_gain_event(struct snd_soc_dapm_widget *w,
			       struct snd_kcontrol *kcontrol,
			       int event)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(cmpnt);
	struct mt8188_afe_private *afe_priv = afe->platform_priv;
	struct mtk_dai_dmic_priv *dmic_priv = afe_priv->dai_priv[MT8188_AFE_IO_DMIC_IN];
	unsigned int channels = dmic_priv->channels;

	dev_dbg(afe->dev, "%s(), name %s, event 0x%x\n",
		__func__, w->name, event);

	if (!channels)
		return -EINVAL;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		mtk_dai_dmic_hw_gain_enable(afe, channels, true);
		break;
	case SND_SOC_DAPM_POST_PMD:
		mtk_dai_dmic_hw_gain_enable(afe, channels, false);
		break;
	default:
		break;
	}

	return 0;
}

static int mtk_dmic_event(struct snd_soc_dapm_widget *w,
			  struct snd_kcontrol *kcontrol,
			  int event)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(cmpnt);
	struct mt8188_afe_private *afe_priv = afe->platform_priv;
	struct mtk_dai_dmic_priv *dmic_priv = afe_priv->dai_priv[MT8188_AFE_IO_DMIC_IN];
	const struct mtk_dai_dmic_ctrl_reg *reg = NULL;
	unsigned int channels = dmic_priv->channels;
	unsigned int msk;
	u8 dmic_num;
	int i;

	dev_dbg(afe->dev, "%s(), name %s, event 0x%x\n",
		__func__, w->name, event);

	if (!channels)
		return -EINVAL;

	dmic_num = mtk_dmic_channels_to_dmic_number(channels);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		/* request fifo soft rst */
		msk = 0;
		for (i = dmic_num; i >= DMIC0; i--)
			msk |= PWR2_TOP_CON1_DMIC_FIFO_SOFT_RST_EN(i);

		regmap_set_bits(afe->regmap, PWR2_TOP_CON1, msk);

		msk = AFE_DMIC_UL_SRC_CON0_UL_MODE_3P25M_CH1_CTL |
		      AFE_DMIC_UL_SRC_CON0_UL_MODE_3P25M_CH2_CTL |
		      AFE_DMIC_UL_SRC_CON0_UL_SDM_3_LEVEL_CTL |
		      AFE_DMIC_UL_SRC_CON0_UL_IIR_ON_TMP_CTL;

		for (i = dmic_num; i >= DMIC0; i--) {
			reg = get_dmic_ctrl_reg(i);
			if (reg)
				regmap_set_bits(afe->regmap, reg->con0, msk);
		}
		break;
	case SND_SOC_DAPM_POST_PMU:
		msk = AFE_DMIC_UL_SRC_CON0_UL_SRC_ON_TMP_CTL;

		for (i = dmic_num; i >= DMIC0; i--) {
			reg = get_dmic_ctrl_reg(i);
			if (reg)
				regmap_set_bits(afe->regmap, reg->con0, msk);
		}

		if (dmic_priv->hires_required) {
			mt8188_afe_enable_clk(afe, afe_priv->clk[MT8188_CLK_AUD_DMIC_HIRES1]);
			mt8188_afe_enable_clk(afe, afe_priv->clk[MT8188_CLK_AUD_DMIC_HIRES2]);
			mt8188_afe_enable_clk(afe, afe_priv->clk[MT8188_CLK_AUD_DMIC_HIRES3]);
			mt8188_afe_enable_clk(afe, afe_priv->clk[MT8188_CLK_AUD_DMIC_HIRES4]);
		}

		mt8188_afe_enable_clk(afe, afe_priv->clk[MT8188_CLK_AUD_AFE_DMIC1]);
		mt8188_afe_enable_clk(afe, afe_priv->clk[MT8188_CLK_AUD_AFE_DMIC2]);
		mt8188_afe_enable_clk(afe, afe_priv->clk[MT8188_CLK_AUD_AFE_DMIC3]);
		mt8188_afe_enable_clk(afe, afe_priv->clk[MT8188_CLK_AUD_AFE_DMIC4]);

		/* release fifo soft rst */
		msk = 0;
		for (i = dmic_num; i >= DMIC0; i--)
			msk |= PWR2_TOP_CON1_DMIC_FIFO_SOFT_RST_EN(i);

		regmap_clear_bits(afe->regmap, PWR2_TOP_CON1, msk);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		msk = AFE_DMIC_UL_SRC_CON0_UL_MODE_3P25M_CH1_CTL |
		      AFE_DMIC_UL_SRC_CON0_UL_MODE_3P25M_CH2_CTL |
		      AFE_DMIC_UL_SRC_CON0_UL_SRC_ON_TMP_CTL |
		      AFE_DMIC_UL_SRC_CON0_UL_IIR_ON_TMP_CTL |
		      AFE_DMIC_UL_SRC_CON0_UL_SDM_3_LEVEL_CTL;

		for (i = dmic_num; i >= DMIC0; i--) {
			reg = get_dmic_ctrl_reg(i);
			if (reg)
				regmap_set_bits(afe->regmap, reg->con0, msk);
		}
		break;
	case SND_SOC_DAPM_POST_PMD:
		/* should delayed 1/fs(smallest is 8k) = 125us before afe off */
		usleep_range(125, 126);

		mt8188_afe_disable_clk(afe, afe_priv->clk[MT8188_CLK_AUD_AFE_DMIC1]);
		mt8188_afe_disable_clk(afe, afe_priv->clk[MT8188_CLK_AUD_AFE_DMIC2]);
		mt8188_afe_disable_clk(afe, afe_priv->clk[MT8188_CLK_AUD_AFE_DMIC3]);
		mt8188_afe_disable_clk(afe, afe_priv->clk[MT8188_CLK_AUD_AFE_DMIC4]);

		if (dmic_priv->hires_required) {
			mt8188_afe_disable_clk(afe, afe_priv->clk[MT8188_CLK_AUD_DMIC_HIRES1]);
			mt8188_afe_disable_clk(afe, afe_priv->clk[MT8188_CLK_AUD_DMIC_HIRES2]);
			mt8188_afe_disable_clk(afe, afe_priv->clk[MT8188_CLK_AUD_DMIC_HIRES3]);
			mt8188_afe_disable_clk(afe, afe_priv->clk[MT8188_CLK_AUD_DMIC_HIRES4]);
		}
		break;
	default:
		break;
	}

	return 0;
}

static int mtk_dai_dmic_hw_params(struct snd_pcm_substream *substream,
				  struct snd_pcm_hw_params *params,
				  struct snd_soc_dai *dai)
{
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);
	struct mt8188_afe_private *afe_priv = afe->platform_priv;
	struct mtk_dai_dmic_priv *dmic_priv = afe_priv->dai_priv[MT8188_AFE_IO_DMIC_IN];
	unsigned int rate = params_rate(params);
	unsigned int channels = params_channels(params);
	const struct mtk_dai_dmic_ctrl_reg *reg = NULL;
	u32 val = AFE_DMIC_UL_SRC_CON0_UL_PHASE_SEL_CH1(CLK_PHASE_SEL_CH1) |
		  AFE_DMIC_UL_SRC_CON0_UL_PHASE_SEL_CH2(CLK_PHASE_SEL_CH2) |
		  AFE_DMIC_UL_SRC_CON0_UL_IIR_MODE_CTL(0);
	const u32 msk = AFE_DMIC_UL_SRC_CON0_UL_TWO_WIRE_MODE_CTL |
			AFE_DMIC_UL_SRC_CON0_UL_PHASE_SEL_MASK |
			AFE_DMIC_UL_SRC_CON0_UL_IIR_MODE_CTL_MASK |
			AFE_DMIC_UL_VOICE_MODE_MASK;
	u8 dmic_num;
	int ret;
	int i;

	if (!channels || channels > 8)
		return -EINVAL;

	ret = mtk_dai_dmic_configure_array(dai);
	if (ret < 0)
		return ret;

	ret = mtk_dai_dmic_load_iir_coeff_table(afe);
	if (ret < 0)
		return ret;

	switch (rate) {
	case 96000:
		val |= AFE_DMIC_UL_CON0_VOCIE_MODE_96K;
		dmic_priv->hires_required = 1;
		break;
	case 48000:
		val |= AFE_DMIC_UL_CON0_VOCIE_MODE_48K;
		dmic_priv->hires_required = 0;
		break;
	case 32000:
		val |= AFE_DMIC_UL_CON0_VOCIE_MODE_32K;
		dmic_priv->hires_required = 0;
		break;
	case 16000:
		val |= AFE_DMIC_UL_CON0_VOCIE_MODE_16K;
		dmic_priv->hires_required = 0;
		break;
	case 8000:
		val |= AFE_DMIC_UL_CON0_VOCIE_MODE_8K;
		dmic_priv->hires_required = 0;
		break;
	default:
		dev_dbg(afe->dev, "%s invalid rate %u, use 48000Hz\n", __func__, rate);
		val |= AFE_DMIC_UL_CON0_VOCIE_MODE_48K;
		dmic_priv->hires_required = 0;
		break;
	}

	dmic_num = mtk_dmic_channels_to_dmic_number(channels);
	for (i = dmic_num; i >= DMIC0; i--) {
		reg = get_dmic_ctrl_reg(i);
		if (reg) {
			ret = regmap_update_bits(afe->regmap, reg->con0, msk, val);
			if (ret < 0)
				return ret;
		}
	}

	dmic_priv->channels = channels;

	return 0;
}

static const struct snd_soc_dai_ops mtk_dai_dmic_ops = {
	.hw_params	= mtk_dai_dmic_hw_params,
};

#define MTK_DMIC_RATES (SNDRV_PCM_RATE_8000 |\
		       SNDRV_PCM_RATE_16000 |\
		       SNDRV_PCM_RATE_32000 |\
		       SNDRV_PCM_RATE_48000 |\
		       SNDRV_PCM_RATE_96000)

#define MTK_DMIC_FORMATS (SNDRV_PCM_FMTBIT_S16_LE |\
			 SNDRV_PCM_FMTBIT_S32_LE)

static struct snd_soc_dai_driver mtk_dai_dmic_driver[] = {
	{
		.name = "DMIC",
		.id = MT8188_AFE_IO_DMIC_IN,
		.capture = {
			.stream_name = "DMIC Capture",
			.channels_min = 1,
			.channels_max = 8,
			.rates = MTK_DMIC_RATES,
			.formats = MTK_DMIC_FORMATS,
		},
		.ops = &mtk_dai_dmic_ops,
	},
};

static const struct snd_soc_dapm_widget mtk_dai_dmic_widgets[] = {
	SND_SOC_DAPM_MIXER("I004", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I005", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I006", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I007", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I008", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I009", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I010", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I011", SND_SOC_NOPM, 0, 0, NULL, 0),

	SND_SOC_DAPM_SUPPLY_S("DMIC_GAIN_ON", SUPPLY_SEQ_DMIC_GAIN,
			      SND_SOC_NOPM, 0, 0,
			      mtk_dmic_gain_event,
			      SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY_S("DMIC_CK_ON", SUPPLY_SEQ_DMIC_CK,
			      PWR2_TOP_CON1,
			      PWR2_TOP_CON1_DMIC_CKDIV_ON_SHIFT, 0,
			      mtk_dmic_event,
			      SND_SOC_DAPM_PRE_POST_PMU |
			      SND_SOC_DAPM_PRE_POST_PMD),
	SND_SOC_DAPM_INPUT("DMIC_INPUT"),
};

static const struct snd_soc_dapm_route mtk_dai_dmic_routes[] = {
	{"I004", NULL, "DMIC Capture"},
	{"I005", NULL, "DMIC Capture"},
	{"I006", NULL, "DMIC Capture"},
	{"I007", NULL, "DMIC Capture"},
	{"I008", NULL, "DMIC Capture"},
	{"I009", NULL, "DMIC Capture"},
	{"I010", NULL, "DMIC Capture"},
	{"I011", NULL, "DMIC Capture"},
	{"DMIC Capture", NULL, "DMIC_CK_ON"},
	{"DMIC Capture", NULL, "DMIC_GAIN_ON"},
	{"DMIC Capture", NULL, "DMIC_INPUT"},
};

static const char * const mt8188_dmic_gain_enable_text[] = {
	"Bypass", "Connect",
};

static SOC_ENUM_SINGLE_EXT_DECL(dmic_gain_on_enum,
				mt8188_dmic_gain_enable_text);

static int mtk_dai_dmic_hw_gain_ctrl_put(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(component);
	struct mt8188_afe_private *afe_priv = afe->platform_priv;
	struct mtk_dai_dmic_priv *dmic_priv = afe_priv->dai_priv[MT8188_AFE_IO_DMIC_IN];
	unsigned int source = ucontrol->value.enumerated.item[0];
	unsigned int *cached;

	if (source >= e->items)
		return -EINVAL;

	if (!strcmp(kcontrol->id.name, "DMIC1_HW_GAIN_EN"))
		cached = &dmic_priv->gain_on[0];
	else if (!strcmp(kcontrol->id.name, "DMIC2_HW_GAIN_EN"))
		cached = &dmic_priv->gain_on[1];
	else if (!strcmp(kcontrol->id.name, "DMIC3_HW_GAIN_EN"))
		cached = &dmic_priv->gain_on[2];
	else if (!strcmp(kcontrol->id.name, "DMIC4_HW_GAIN_EN"))
		cached = &dmic_priv->gain_on[3];
	else
		return -EINVAL;

	if (source == *cached)
		return 0;

	*cached = source;
	return 1;
}

static int mtk_dai_dmic_hw_gain_ctrl_get(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(component);
	struct mt8188_afe_private *afe_priv = afe->platform_priv;
	struct mtk_dai_dmic_priv *dmic_priv = afe_priv->dai_priv[MT8188_AFE_IO_DMIC_IN];
	unsigned int val;

	if (!strcmp(kcontrol->id.name, "DMIC1_HW_GAIN_EN"))
		val = dmic_priv->gain_on[0];
	else if (!strcmp(kcontrol->id.name, "DMIC2_HW_GAIN_EN"))
		val = dmic_priv->gain_on[1];
	else if (!strcmp(kcontrol->id.name, "DMIC3_HW_GAIN_EN"))
		val = dmic_priv->gain_on[2];
	else if (!strcmp(kcontrol->id.name, "DMIC4_HW_GAIN_EN"))
		val = dmic_priv->gain_on[3];
	else
		return -EINVAL;

	ucontrol->value.enumerated.item[0] = val;
	return 0;
}

static const struct snd_kcontrol_new mtk_dai_dmic_controls[] = {
	SOC_ENUM_EXT("DMIC1_HW_GAIN_EN", dmic_gain_on_enum,
		     mtk_dai_dmic_hw_gain_ctrl_get,
		     mtk_dai_dmic_hw_gain_ctrl_put),
	SOC_ENUM_EXT("DMIC2_HW_GAIN_EN", dmic_gain_on_enum,
		     mtk_dai_dmic_hw_gain_ctrl_get,
		     mtk_dai_dmic_hw_gain_ctrl_put),
	SOC_ENUM_EXT("DMIC3_HW_GAIN_EN", dmic_gain_on_enum,
		     mtk_dai_dmic_hw_gain_ctrl_get,
		     mtk_dai_dmic_hw_gain_ctrl_put),
	SOC_ENUM_EXT("DMIC4_HW_GAIN_EN", dmic_gain_on_enum,
		     mtk_dai_dmic_hw_gain_ctrl_get,
		     mtk_dai_dmic_hw_gain_ctrl_put),
	SOC_SINGLE("DMIC1_HW_GAIN_TARGET", DMIC_GAIN1_CON1,
		   0, DMIC_GAIN_MAX_TARGET, 0),
	SOC_SINGLE("DMIC2_HW_GAIN_TARGET", DMIC_GAIN2_CON1,
		   0, DMIC_GAIN_MAX_TARGET, 0),
	SOC_SINGLE("DMIC3_HW_GAIN_TARGET", DMIC_GAIN3_CON1,
		   0, DMIC_GAIN_MAX_TARGET, 0),
	SOC_SINGLE("DMIC4_HW_GAIN_TARGET", DMIC_GAIN4_CON1,
		   0, DMIC_GAIN_MAX_TARGET, 0),
	SOC_SINGLE("DMIC1_HW_GAIN_CURRENT", DMIC_GAIN1_CUR,
		       0, DMIC_GAIN_MAX_CURRENT, 0),
	SOC_SINGLE("DMIC2_HW_GAIN_CURRENT", DMIC_GAIN2_CUR,
		       0, DMIC_GAIN_MAX_CURRENT, 0),
	SOC_SINGLE("DMIC3_HW_GAIN_CURRENT", DMIC_GAIN3_CUR,
		       0, DMIC_GAIN_MAX_CURRENT, 0),
	SOC_SINGLE("DMIC4_HW_GAIN_CURRENT", DMIC_GAIN4_CUR,
		       0, DMIC_GAIN_MAX_CURRENT, 0),
	SOC_SINGLE("DMIC1_HW_GAIN_UP_STEP", DMIC_GAIN1_CON3,
		   0, DMIC_GAIN_MAX_STEP, 0),
	SOC_SINGLE("DMIC2_HW_GAIN_UP_STEP", DMIC_GAIN2_CON3,
		   0, DMIC_GAIN_MAX_STEP, 0),
	SOC_SINGLE("DMIC3_HW_GAIN_UP_STEP", DMIC_GAIN3_CON3,
		   0, DMIC_GAIN_MAX_STEP, 0),
	SOC_SINGLE("DMIC4_HW_GAIN_UP_STEP", DMIC_GAIN4_CON3,
		   0, DMIC_GAIN_MAX_STEP, 0),
	SOC_SINGLE("DMIC1_HW_GAIN_DOWN_STEP", DMIC_GAIN1_CON2,
		   0, DMIC_GAIN_MAX_STEP, 0),
	SOC_SINGLE("DMIC2_HW_GAIN_DOWN_STEP", DMIC_GAIN2_CON2,
		   0, DMIC_GAIN_MAX_STEP, 0),
	SOC_SINGLE("DMIC3_HW_GAIN_DOWN_STEP", DMIC_GAIN3_CON2,
		   0, DMIC_GAIN_MAX_STEP, 0),
	SOC_SINGLE("DMIC4_HW_GAIN_DOWN_STEP", DMIC_GAIN4_CON2,
		   0, DMIC_GAIN_MAX_STEP, 0),
	SOC_SINGLE("DMIC1_HW_GAIN_SAMPLE_PER_STEP", DMIC_GAIN1_CON0,
		   DMIC_GAIN_CON0_SAMPLE_PER_STEP_SHIFT, DMIC_GAIN_MAX_PER_STEP, 0),
	SOC_SINGLE("DMIC2_HW_GAIN_SAMPLE_PER_STEP", DMIC_GAIN2_CON0,
		   DMIC_GAIN_CON0_SAMPLE_PER_STEP_SHIFT, DMIC_GAIN_MAX_PER_STEP, 0),
	SOC_SINGLE("DMIC3_HW_GAIN_SAMPLE_PER_STEP", DMIC_GAIN3_CON0,
		   DMIC_GAIN_CON0_SAMPLE_PER_STEP_SHIFT, DMIC_GAIN_MAX_PER_STEP, 0),
	SOC_SINGLE("DMIC4_HW_GAIN_SAMPLE_PER_STEP", DMIC_GAIN4_CON0,
		   DMIC_GAIN_CON0_SAMPLE_PER_STEP_SHIFT, DMIC_GAIN_MAX_PER_STEP, 0),
};

static int init_dmic_priv_data(struct mtk_base_afe *afe)
{
	struct mt8188_afe_private *afe_priv = afe->platform_priv;
	struct mtk_dai_dmic_priv *dmic_priv;

	dmic_priv = devm_kzalloc(afe->dev, sizeof(struct mtk_dai_dmic_priv),
				 GFP_KERNEL);
	if (!dmic_priv)
		return -ENOMEM;

	afe_priv->dai_priv[MT8188_AFE_IO_DMIC_IN] = dmic_priv;
	return 0;
}

int mt8188_dai_dmic_register(struct mtk_base_afe *afe)
{
	struct mtk_base_afe_dai *dai;

	dai = devm_kzalloc(afe->dev, sizeof(*dai), GFP_KERNEL);
	if (!dai)
		return -ENOMEM;

	list_add(&dai->list, &afe->sub_dais);

	dai->dai_drivers = mtk_dai_dmic_driver;
	dai->num_dai_drivers = ARRAY_SIZE(mtk_dai_dmic_driver);
	dai->dapm_widgets = mtk_dai_dmic_widgets;
	dai->num_dapm_widgets = ARRAY_SIZE(mtk_dai_dmic_widgets);
	dai->dapm_routes = mtk_dai_dmic_routes;
	dai->num_dapm_routes = ARRAY_SIZE(mtk_dai_dmic_routes);
	dai->controls = mtk_dai_dmic_controls;
	dai->num_controls = ARRAY_SIZE(mtk_dai_dmic_controls);

	return init_dmic_priv_data(afe);
}
